#!/usr/bin/env bash
# Build a real PS4 shortcut application, not a PS5 param.json-only web tile.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
eval "$(bash app/tools/setup-orbis-toolchain.sh)"
toolchain=$OO_PS4_TOOLCHAIN
gotools="$root/app/.deps/orbis/gotools/bin"
build="$root/build/browser-shortcut"
app="$build/ORBT00001"
mkdir -p "$app/sce_sys" "$app/sce_module"
cp "$toolchain/sce_module/libc.prx" "$toolchain/sce_module/libSceFios2.prx" "$app/sce_module/"
clang-18 -target x86_64-pc-freebsd12-elf -O2 -Wall -Wextra -fPIC -ffunction-sections -fdata-sections -fno-plt \
  -D_GNU_SOURCE -isystem "$toolchain/include" -Ibuild/generated -c launcher/browser.c -o "$build/browser.o"
ld.lld-18 -pie -T "$toolchain/link.x" --eh-frame-hdr --gc-sections -e _start -L"$toolchain/lib" \
  "$toolchain/lib/crt1.o" "$toolchain/lib/crti.o" "$build/browser.o" \
  --start-group -lc -lm -lpthread -lclang_rt.builtins-x86_64 --end-group \
  --as-needed -l:libkernel.so -l:libSceUserService.so -l:libSceSystemService.so \
  -l:libSceSysmodule.so -l:libSceCommonDialog.so -l:libSceWebBrowserDialog.so \
  "$toolchain/lib/crtn.o" -o "$build/eboot.elf"
OO_PS4_TOOLCHAIN="$toolchain" "$gotools/create-fself" -in "$build/eboot.elf" \
  -out "$build/eboot.oelf" -eboot "$app/eboot.bin"
python3 app/tools/make_sfo.py launcher/sce_sys/param.json "$app/sce_sys/param.sfo"
cp launcher/sce_sys/icon0.png "$app/sce_sys/icon0.png"
content_id=$(python3 -c 'import json; print(json.load(open("launcher/sce_sys/param.json"))["contentId"])')
rm -f "$app/ORBT00001.gp4"
"$gotools/create-gp4" -out "$app/ORBT00001.gp4" -content-id "$content_id" -path "$app"
rm -f "$build/$content_id.pkg"
DOTNET_ROLL_FORWARD=LatestMajor dotnet "$toolchain/pkgtool/PkgTool.Core.dll" pkg_build "$app/ORBT00001.gp4" "$build"
DOTNET_ROLL_FORWARD=LatestMajor dotnet "$toolchain/pkgtool/PkgTool.Core.dll" pkg_validate "$build/$content_id.pkg"
cp "$build/$content_id.pkg" "$root/build/orbit-browser.pkg"
