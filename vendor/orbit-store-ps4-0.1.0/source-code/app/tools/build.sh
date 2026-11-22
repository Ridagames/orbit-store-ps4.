#!/usr/bin/env bash
# Orbit Store TV app - PS4 native application build.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles the app for x86_64-pc-freebsd12-elf against the pinned OpenOrbis
# musl/CRT/stub set, links the OELF with link.x, fake-signs it with
# create-fself, writes sce_sys/param.sfo, assembles the package folder, and
# optionally produces the installable .pkg via create-gp4 + LibOrbisPkg.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
format=${1:-Folder}
format=${format,,}
case "$format" in folder|pkg|all) ;; *)
    echo "usage: tools/build.sh [Folder|Pkg|All]" >&2
    exit 2
esac

for command in python3 sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done

param="$root/sce_sys/param.json"
title_id=$(python3 - "$param" <<'PY'
import json, re, sys

with open(sys.argv[1], encoding="utf-8") as source:
    value = json.load(source)

title_id = value.get("titleId", "")
concept_id = value.get("conceptId", "")
content_id = value.get("contentId", "")
if not re.fullmatch(r"ORBT\d{5}", title_id):
    raise SystemExit("param.json titleId must use ORBT followed by five digits")
if not re.fullmatch(r"\d{5}", concept_id):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-ORBT\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", value.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", value.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", value.get("appVersion", "")):
    raise SystemExit("param.json appVersion must use the PS4 NN.NN update counter")
size = value.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")

category = value.get("applicationCategoryType")
badge = value.get("contentBadgeType")
if (category, badge) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = value.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in value:
    raise SystemExit("media param.json must not contain gameIntent")

localized = value.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
print(title_id)
PY
)
content_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["contentId"])' "$param")

bash "$root/tools/validate-assets.sh" "$root/sce_sys"

build="$root/build"
dist="$root/dist"
mkdir -p "$build/obj" "$dist"

# Toolchain + libraries (pinned, cached under .deps/)
setup_environment=$(bash "$root/tools/setup-orbis-toolchain.sh")
eval "$setup_environment"
setup_environment=$(bash "$root/tools/setup-webp-dependencies.sh")
eval "$setup_environment"
toolchain="$OO_PS4_TOOLCHAIN"
gotools="$root/.deps/orbis/gotools/bin"
cc=$(command -v "${PS4_CC:-clang-18}")
cxx=$(command -v "${PS4_CXX:-clang++-18}")
lld=$(command -v "${PS4_LLD:-ld.lld-18}")
target="x86_64-pc-freebsd12-elf"
[[ -n $cc && -n $cxx && -n $lld ]] || {
    echo "clang-18/clang++-18/ld.lld-18 are required for the PS4 build" >&2
    exit 2
}

extra_definitions=()
extra_includes=()
[[ -z ${APP_DEFINITIONS:-} ]] || read -r -a extra_definitions <<< "$APP_DEFINITIONS"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a extra_includes <<< "$APP_INCLUDE_PATHS"
for definition in "${extra_definitions[@]}"; do
    [[ $definition =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$ ]] || {
        echo "invalid compile definition: $definition" >&2; exit 2;
    }
done
for include in "${extra_includes[@]}"; do
    [[ $include =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -d $root/$include ]] || {
        echo "invalid include path: $include" >&2; exit 2;
    }
done

mapfile -d '' -t source_paths < <(
    find "$root/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
        -print0 | sort -z
)
sources=()
for source in "${source_paths[@]}"; do
    sources+=("${source#"$root/"}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C or C++ sources" >&2; exit 2; }

source "$root/tools/ninja-build.sh"
ninja_begin "$build/app.ninja"

# OpenOrbis's crt1.o initialises native TLS, so no -femulated-tls (that flag
# belongs to the payload SDK's loaderless flow and needs __emutls_get_address,
# which the OpenOrbis builtins archive does not carry).
definition_flags=()
include_flags=()
for definition in "${extra_definitions[@]}"; do
    definition_flags+=("-D$definition")
done
for include in "${extra_includes[@]}"; do
    include_flags+=("-I$root/$include")
done
common_flags=(
    -target "$target" -O2 -fPIC
    -ffunction-sections -fdata-sections -fno-plt
    -DGL_GLES_PROTOTYPES=1 -DEGL_EGLEXT_PROTOTYPES=1 -D_GNU_SOURCE -DORBIT_PS4 -D__PIGLET__
    -Wno-narrowing
    "${definition_flags[@]}"
    -cxx-isystem "$toolchain/include/c++/v1"
    -isystem "$toolchain/include"
    -I"$root/src" -I"$WEBP_INCLUDE"
    "${include_flags[@]}"
)

objects=()
for source in "${sources[@]}"; do
    [[ $source =~ ^src/[A-Za-z0-9_./-]+\.(c|cc|cpp)$ && -f $root/$source ]] || {
        echo "invalid source: $source" >&2; exit 2;
    }
    object="$build/obj/$source.o"
    mkdir -p "$(dirname "$object")"
    if [[ $source == *.c ]]; then
        standard=-std=c11
        compiler_for_source=$cc
        if [[ $source == src/third_party/* ]]; then
            extra=(-w)
        else
            extra=(-Wall -Wextra)
        fi
    else
        standard=-std=c++20
        compiler_for_source=$cxx
        # Vendored upstream code is built as published; its warnings are not
        # ours to fix.
        compat=(-include "$root/src/platform/ps4/cxx_compat.hpp")
        if [[ $source == src/third_party/* ]]; then
            extra=(-w -fno-exceptions -fno-rtti "${compat[@]}")
        else
            extra=(-fno-exceptions -fno-rtti -Wall -Wextra "${compat[@]}")
        fi
    fi
    ninja_inputs=("$root/$source" "$compiler_for_source")
    ninja_edge CC "$object" "${compiler_cache[@]}" \
        "$compiler_for_source" "${common_flags[@]}" "$standard" "${extra[@]}" \
        -MD -MF "$object.d" -c "$root/$source" -o "$object"
    objects+=("$object")
done

# Import stubs resolved through the OpenOrbis stub set. Every DT_NEEDED this
# link emits is a module name create-fself maps during dynlib-data generation.
stub_libraries=(
    libkernel
    libScePigletv2VSH
    libSceAudioOut
    libScePad
    libSceUserService
    libSceSysmodule
    libSceSystemService
    libSceCommonDialog
    libSceMsgDialog
    libSceWebBrowserDialog
)
stub_flags=()
stub_paths=()
for stub in "${stub_libraries[@]}"; do
    stub_flags+=("-l:$stub.so")
    stub_paths+=("$toolchain/lib/$stub.so")
    [[ -f $toolchain/lib/$stub.so ]] || {
        echo "missing OpenOrbis stub: $stub" >&2; exit 2;
    }
done

wrap_options=(--wrap=sceSystemServiceHideSplashScreen)

ninja_inputs=("$toolchain/link.x" "$toolchain/lib/crt1.o" "$toolchain/lib/crti.o" \
    "$toolchain/lib/crtn.o" "${stub_paths[@]}" "$WEBP_ARCHIVE" \
    "$toolchain/lib/libc.a" "$toolchain/lib/libc++.a" "$toolchain/lib/libc++abi.a" \
    "$toolchain/lib/libunwind.a" "$toolchain/lib/libm.a" "$toolchain/lib/libpthread.a" \
    "$toolchain/lib/libclang_rt.builtins-x86_64.a" "${objects[@]}")
ninja_edge LINK "$build/eboot.elf" "$lld" \
    -pie -T "$toolchain/link.x" --eh-frame-hdr --gc-sections -e _start \
    -L"$toolchain/lib" \
    -o "$build/eboot.elf" \
    "$toolchain/lib/crt1.o" "$toolchain/lib/crti.o" \
    "${objects[@]}" \
    "${wrap_options[@]}" \
    --start-group \
    "$toolchain/lib/libc++.a" "$toolchain/lib/libc++abi.a" "$toolchain/lib/libunwind.a" \
    "$toolchain/lib/libm.a" "$toolchain/lib/libpthread.a" "$toolchain/lib/libc.a" \
    "$toolchain/lib/libclang_rt.builtins-x86_64.a" "$WEBP_ARCHIVE" \
    --end-group \
    --as-needed "${stub_flags[@]}" \
    "$toolchain/lib/crtn.o"

# ELF -> OELF -> fake-signed eboot.bin
ninja_inputs=("$build/eboot.elf" "$gotools/create-fself" "$root/tools/make-piglet-self.sh")
ninja_edge FSELF "$build/eboot.bin" env OO_PS4_TOOLCHAIN="$toolchain" \
    bash "$root/tools/make-piglet-self.sh" "$gotools/create-fself" \
    -in "$build/eboot.elf" -out "$build/eboot.oelf" \
    -eboot "$build/eboot.bin"
ninja_run

app="$dist/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
cp "$toolchain/sce_module/libc.prx" "$toolchain/sce_module/libSceFios2.prx" "$app/sce_module/"
cp "$build/eboot.bin" "$app/eboot.bin"
python3 "$root/tools/make_sfo.py" "$param" "$app/sce_sys/param.sfo"
# The app reads its running content version from param.json at runtime.
cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.png pic1.png snd0.at9; do
    [[ -f $root/sce_sys/$asset ]] && cp "$root/sce_sys/$asset" "$app/sce_sys/$asset"
done
[[ -f $app/sce_sys/icon0.png ]] || { echo "sce_sys/icon0.png is required" >&2; exit 2; }

[[ ! -d $root/assets ]] || cp -a "$root/assets" "$app/assets"
# Orbit Store: the interface catalogues, shared with the browser version
# (i18n/ at the repository root). English is the key and needs no file.
mkdir -p "$app/assets/i18n"
for catalogue in "$root"/../i18n/*.json; do
    [[ $(basename "$catalogue") == en.json ]] || cp "$catalogue" "$app/assets/i18n/"
done
# Orbit Store: extra files staged by the Makefile (Orbit itself, in orbit/).
[[ -z ${APP_EXTRA_DIR:-} || ! -d $root/$APP_EXTRA_DIR ]] || cp -a "$root/$APP_EXTRA_DIR/." "$app/"
# Directory listing returns nothing under /app0 on the console, so each
# audio folder carries an index of its files (read by save::list_files).
for dir in "$app/assets/audio/music" "$app"/assets/audio/sfx/*/; do
    [[ -d $dir ]] || continue
    (cd "$dir" && find . -maxdepth 1 -type f ! -name index.txt -printf '%f\n' | LC_ALL=C sort > index.txt)
done

"$gotools/readoelf" "$build/eboot.elf" >"$build/eboot.readoelf.txt" 2>/dev/null || true

printf '==> [zip] Archiving the application folder\n'
(cd "$dist" && python3 -m zipfile -c "$title_id.zip" "$title_id")

if [[ $format == pkg || $format == all ]]; then
    # create-gp4 writes app-root-relative orig_paths, so the project file
    # must sit inside the app folder for PkgTool's projdir to resolve them.
    gp4="$app/$title_id.gp4"
    rm -f -- "$gp4"
    "$gotools/create-gp4" -out "$gp4" -content-id "$content_id" -path "$app"
    rm -f -- "$dist"/*.pkg
    DOTNET_ROLL_FORWARD=LatestMajor \
        dotnet "$toolchain/pkgtool/PkgTool.Core.dll" pkg_build "$gp4" "$dist"
    pkg=$(find "$dist" -maxdepth 1 -name '*.pkg' -print -quit)
    [[ -f $pkg ]] || { echo "PkgTool produced no .pkg in $dist" >&2; exit 2; }
    printf 'PKG:         %s\n' "$pkg"
fi

printf 'Build complete.\nApp folder: %s\n' "$app"
printf 'Folder ZIP: %s\n' "$dist/$title_id.zip"
