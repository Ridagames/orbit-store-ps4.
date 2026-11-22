#!/usr/bin/env bash
# Orbit Store TV app - WebP decoder for the PS4 (FreeBSD) target.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-compiles the pinned libwebp sources into .deps/webp as a static
# archive for x86_64-pc-freebsd12-elf (the OpenOrbis target). The app decodes
# catalogue artwork only, so the encoder and demux objects are skipped.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/webp"
version="1.4.0"
source_dir="$cache/libwebp"
build_dir="$cache/build"
library="$cache/lib/libwebp.a"
include_dir="$cache/include"
stamp="$cache/.version"
archive="$root/../build/upstream/libwebp-$version.tar.gz"
archive_hash="61f873ec69e3be1b99535634340d5bde750b2e4447caa1db9f61be3fd49ab1e5"
url="https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$version.tar.gz"

for command in wget tar sha256sum llvm-ar-18 ranlib; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done
compiler=$(command -v clang-18 || command -v clang || true)
[[ -n $compiler ]] || { echo "clang is required to cross-build libwebp" >&2; exit 2; }

mkdir -p "$cache"
setup_environment=$(bash "$root/tools/setup-orbis-toolchain.sh")
eval "$setup_environment"
toolchain="$OO_PS4_TOOLCHAIN"
runtime_hash=$(sha256sum "$toolchain/provenance.json" | cut -d' ' -f1)
recipe_hash=$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)
compiler_version=$("$compiler" --version | head -n1)
build_version="$version:$runtime_hash:$recipe_hash:$compiler_version"
if [[ ! -f $stamp || $(<"$stamp") != "$build_version" ]]; then
    mkdir -p "$(dirname "$archive")"
    if [[ ! -f $archive ]]; then
        wget -q "$url" -O "$archive.download"
        printf '%s  %s\n' "$archive_hash" "$archive.download" | sha256sum --check --strict >&2
        mv "$archive.download" "$archive"
    fi
    printf '%s  %s\n' "$archive_hash" "$archive" | sha256sum --check --strict >&2
    rm -rf -- "$source_dir" "$build_dir" "$cache/lib" "$include_dir"
    mkdir -p "$source_dir"
    tar -xzf "$archive" --strip-components=1 -C "$source_dir"
    mkdir -p "$build_dir" "$cache/lib" "$include_dir"
    cp -a "$source_dir/src/webp" "$include_dir/"

    resource_dir="$("$compiler" -print-resource-dir)/include"
    jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || printf '2')}
    fail="$build_dir/failed"
    compile() {
        local source=$1 object=$2
        # cpu.h autodetects WEBP_HAVE_SSE2/SSE41 from the compiler's
        # predefines; -msse4.1 turns on the SSE4.1 dsp files (Jaguar has
        # them) without enabling anything newer. OpenOrbis's musl headers
        # stand in for the FreeBSD libc (the host's glibc headers must not
        # leak in) and the compiler's own resource dir provides
        # stddef/stdarg/immintrin.
        "$compiler" -target x86_64-pc-freebsd12-elf -std=c99 -O2 -fPIC -msse4.1 \
            -fno-exceptions -ffunction-sections -fdata-sections \
            -DWEBP_USE_THREAD -D_WCHAR_T -nostdinc \
            -isystem "$resource_dir" \
            -idirafter "$toolchain/include" \
            -I"$source_dir" -I"$source_dir/src" \
            -c "$source" -o "$object" || touch "$fail"
    }
    export -f compile
    export compiler source_dir build_dir fail toolchain resource_dir
    find "$source_dir/src" -type f -name '*.c' \
        \( -path '*/dec/*' -o -path '*/dsp/*' -o -path '*/utils/*' \
           -o -path '*/sharpyuv/*' \) \
        ! -name '*_neon.c' ! -name '*_mips*.c' ! -name '*_msa.c' \
        -print0 | sort -z |
        while IFS= read -r -d '' source; do
            object="$build_dir/$(basename "${source%.c}").o"
            printf '%s\0%s\0' "$source" "$object"
        done > "$build_dir/list"
    # Parallel compile through xargs so a slow host still makes progress.
    xargs -0 -n2 -P "$jobs" bash -c 'compile "$1" "$2"' _ \
        < "$build_dir/list" || true
    [[ ! -f $fail ]] || { echo "libwebp compilation failed" >&2; exit 2; }
    llvm-ar-18 rcs "$library" "$build_dir"/*.o
    ranlib "$library" 2>/dev/null || true
    printf '%s\n' "$build_version" >"$stamp"
fi

[[ -f $library && -f $include_dir/webp/decode.h ]] || {
    echo "the pinned libwebp build is incomplete" >&2
    exit 2
}
printf 'WEBP_INCLUDE=%s\nWEBP_ARCHIVE=%s\n' "$include_dir" "$library"
