#!/usr/bin/env bash
# Orbit Store TV app - PS4 (OpenOrbis) toolchain bootstrapper.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetches the pinned OpenOrbis packaging input into .deps/orbis and builds the Go
# packaging tools (create-fself/create-gp4/readoelf) from pinned commits.
# PkgTool.Core ships as architecture-neutral .NET assemblies inside the
# toolchain archive, so the pinned windows DLLs run under any dotnet.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/orbis"
toolchain="$cache/toolchain"
gobuild="$cache/gotools"
stamp_file="$toolchain/.version"

toolchain_version="0.5.4"
toolchain_url="https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v$toolchain_version/toolchain-llvm-18.tar.gz"
toolchain_hash="3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526"
toolchain_archive="$cache/toolchain-llvm-18.tar.gz"

# Pinned source commits for the packaging tools (the release ships x86-64
# binaries only; these host builds work on arm64 as well). The commit SHA is
# the integrity check — GitHub's archive tarballs are not byte-deterministic.
fself_commit="f2da6229684ce320f673c06fceac9c218f621788"
gp4_commit="bf8e66e9e9bdd92e41e1697893bc8098a151fa24"
readoelf_commit="9e45cd0c36b00e4468b59f386b9d594bb6617d23"

for command in wget tar sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done

fetch() {
    local hash=$1 archive=$2 url
    shift 2
    if [[ -f $archive ]] &&
        ! printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict >/dev/null 2>&1; then
        rm -f -- "$archive"
    fi
    [[ -f $archive ]] && return 0
    # Try each source: a mirror that answers with something else (a block
    # page, a truncated file) is skipped, not trusted.
    for url in "$@"; do
        if wget -q "$url" -O "$archive.download" &&
            printf '%s  %s\n' "$hash" "$archive.download" | sha256sum --check --strict >/dev/null 2>&1; then
            mv "$archive.download" "$archive"
            return 0
        fi
        rm -f -- "$archive.download"
    done
    return 1
}

mkdir -p "$cache" "$gobuild/bin" "$gobuild/src"

if [[ ! -f $stamp_file || $(<"$stamp_file") != "$toolchain_version" ]]; then
    fetch "$toolchain_hash" "$toolchain_archive" "$toolchain_url" || {
        echo "OpenOrbis toolchain $toolchain_version could not be downloaded" >&2
        exit 2
    }
    rm -rf -- "$toolchain" "$cache/extract"
    mkdir -p "$cache/extract"
    # Only the pieces the build needs: link script, crt objects, static
    # libraries, import stubs, headers, and the PkgTool .NET assemblies.
    tar -xzf "$toolchain_archive" -C "$cache/extract" \
        --wildcards 'OpenOrbis/PS4Toolchain/link.x' \
        'OpenOrbis/PS4Toolchain/lib' \
        'OpenOrbis/PS4Toolchain/include' \
        'OpenOrbis/PS4Toolchain/bin/windows/PkgTool.Core.dll' \
        'OpenOrbis/PS4Toolchain/bin/windows/LibOrbisPkg.dll' \
        'OpenOrbis/PS4Toolchain/bin/windows/LibOrbisPkg.Core.dll' \
        'OpenOrbis/PS4Toolchain/bin/windows/PkgTool.Core.runtimeconfig.json'
    mv "$cache/extract/OpenOrbis/PS4Toolchain" "$toolchain"
    mv "$toolchain/bin/windows" "$toolchain/pkgtool"
    rm -rf -- "$cache/extract"
    for required in link.x lib/crt1.o lib/libc.a lib/libc++.a lib/libunwind.a \
        lib/libkernel.so lib/libScePigletv2VSH.so lib/libSceAudioOut.so \
        lib/libScePad.so lib/libSceUserService.so lib/libSceSysmodule.so \
        lib/libSceSystemService.so lib/libSceCommonDialog.so \
        lib/libSceWebBrowserDialog.so include/GLES2/gl2.h include/EGL/egl.h \
        include/orbis/Pad.h pkgtool/PkgTool.Core.dll pkgtool/LibOrbisPkg.dll; do
        [[ -e $toolchain/$required ]] || {
            echo "the pinned OpenOrbis toolchain is missing $required" >&2
            exit 2
        }
    done
    printf '%s\n' "$toolchain_version" >"$stamp_file"
fi

build_go_tool() {
    local name=$1 commit=$2 build_dir=$3 modfile=${4:-}
    local src="$gobuild/src/$name" binary="$gobuild/bin/$name"
    [[ -x $binary ]] && return 0
    if [[ ! -d $src/.git ]]; then
        rm -rf -- "$src"
        git clone -q "https://github.com/OpenOrbis/$name" "$src" || {
            echo "$name source could not be cloned" >&2
            exit 2
        }
    fi
    git -C "$src" checkout -q "$commit"
    [[ $(git -C "$src" rev-parse HEAD) == "$commit" ]] || {
        echo "$name is not at the pinned commit" >&2
        exit 2
    }
    local flags=()
    [[ -z $modfile ]] || flags+=(-modfile="$modfile")
    (cd "$src/$build_dir" &&
        go build "${flags[@]}" -o "$binary" .) >"$gobuild/$name.build.log" 2>&1 || {
        echo "go build failed for $name (see $gobuild/$name.build.log)" >&2
        exit 2
    }
}

if [[ ! -x $gobuild/bin/create-fself || ! -x $gobuild/bin/create-gp4 ||
    ! -x $gobuild/bin/readoelf ]]; then
    for command in git go; do
        command -v "$command" >/dev/null || {
            echo "$command is required to build the packaging tools" >&2
            exit 2
        }
    done
    # create-fself's linux build uses its alternate module file (the default
    # go.mod targets windows-only paths).
    build_go_tool create-fself "$fself_commit" cmd/create-fself go-linux.mod
    build_go_tool create-gp4 "$gp4_commit" cmd/create-gp4
    build_go_tool readoelf "$readoelf_commit" cmd/readoelf
fi

# The SDK archive's floating musl/stub revisions cannot be recovered. Assemble
# the active target runtime from pinned sources and the byte-matched LLVM v1.2
# release. The original SDK cache remains available as recovery evidence.
runtime=$(python3 "$root/tools/setup-orbis-runtime.py" --base-toolchain "$toolchain")
printf 'OO_PS4_TOOLCHAIN=%s\nPKGTOOL=%s\n' "$runtime" "$toolchain/pkgtool"
