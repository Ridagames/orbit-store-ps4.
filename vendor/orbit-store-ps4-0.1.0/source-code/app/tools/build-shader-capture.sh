#!/usr/bin/env bash
# Build the private on-console compiler diagnostic, never the release title.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
eval "$(bash "$root/tools/setup-orbis-toolchain.sh")"
toolchain=$OO_PS4_TOOLCHAIN
gotools="$root/.deps/orbis/gotools/bin"
build="$root/build/shader-capture"
title=ORBT00003
content=UP9000-ORBT00003_00-ORBITSHADER00001
app="$build/$title"
mkdir -p "$build"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
cp "$toolchain/sce_module/libc.prx" "$toolchain/sce_module/libSceFios2.prx" "$app/sce_module/"
python3 "$root/tools/shader-assets.py" capture-header "$build/shader_capture_inputs.hpp"
flags=(-target x86_64-pc-freebsd12-elf -O2 -fPIC -std=c++20 -fno-exceptions -fno-rtti -D__PIGLET__
    -ffunction-sections -fdata-sections -D_GNU_SOURCE -DORBIT_PS4
    -cxx-isystem "$toolchain/include/c++/v1" -isystem "$toolchain/include"
    -include "$root/src/platform/ps4/cxx_compat.hpp" -I"$root/src" -I"$build")
clang++-18 "${flags[@]}" -c "$root/tooling/shader_capture.cpp" -o "$build/capture.o"
clang++-18 "${flags[@]}" -c "$root/src/gfx/shader_binary.cpp" -o "$build/binary.o"
# Build a capture-only CRT from the pinned source. Do not change the ordinary
# runtime's memory budgets or depend on private diagnostic build scripts.
musl="$root/.deps/orbis/runtime-build/musl"
mbuild="$root/.deps/orbis/runtime-build/musl-build"
python3 "$root/tools/prepare-shader-crt.py" "$musl/arch/ps4/crt_arch.h" "$build/crt_arch.h"
clang-18 --target=x86_64-pc-freebsd12-elf -std=c99 -nostdinc -ffreestanding \
    -fno-stack-protector -fPIC -DPS4 -DCRT -D_XOPEN_SOURCE=700 \
    -I"$build" -I"$musl/arch/ps4" -I"$musl/arch/generic" \
    -I"$mbuild/obj/src/internal" -I"$musl/src/include" -I"$musl/src/internal" \
    -I"$mbuild/obj/include" -I"$musl/include" \
    -c "$musl/crt/ps4/crt1.c" -o "$build/crt1-capture.o"
ld.lld-18 -pie -T "$toolchain/link.x" --eh-frame-hdr --gc-sections -e _start \
    -L"$toolchain/lib" "$build/crt1-capture.o" "$toolchain/lib/crti.o" \
    "$build/capture.o" "$build/binary.o" --start-group \
    -lc++ -lc++abi -lunwind -lm -lpthread -lc -lclang_rt.builtins-x86_64 --end-group \
    --as-needed -l:libkernel.so -l:libSceSystemService.so -l:libSceSysmodule.so \
    -l:libSceCommonDialog.so -l:libSceMsgDialog.so "$toolchain/lib/crtn.o" -o "$build/eboot.elf"
llvm-readelf-18 -d "$build/eboot.elf" > "$build/imports.txt"
if grep -q 'NEEDED.*libScePiglet' "$build/imports.txt"; then
    echo 'Capture must resolve graphics from the explicitly loaded module' >&2
    exit 1
fi
OO_PS4_TOOLCHAIN="$toolchain" bash "$root/tools/make-piglet-self.sh" "$gotools/create-fself" -in "$build/eboot.elf" \
    -out "$build/eboot.oelf" -eboot "$app/eboot.bin"
python3 - "$root" "$app" "$title" "$content" <<'PY'
import hashlib, json, os, shutil, sys
from pathlib import Path
root, app = map(Path, sys.argv[1:3])
param = {'titleId': sys.argv[3], 'contentId': sys.argv[4],
         'category': 'gde',
         'masterVersion': '01.00', 'appVersion': '01.00',
         'downloadDataSize': 256,
         'requiredSystemSoftwareVersion': '0x05050000',
         'localizedParameters': {'defaultLanguage': 'en-US',
                                 'en-US': {'titleName': 'Orbit Shader Capture'}}}
(app / 'sce_sys/param.json').write_text(json.dumps(param, indent=2) + '\n')
shutil.copy2(root / 'sce_sys/icon0.png', app / 'sce_sys/icon0.png')
modules = os.environ.get('PIGLET_MODULE_DIR')
if modules:
    (app / 'sce_module').mkdir(exist_ok=True)
    recorded = {}
    for name in ['libScePigletv2VSH.sprx', 'libSceShaccVSH.sprx']:
        source = Path(modules) / name
        if not source.is_file() or source.stat().st_size < 4096:
            raise SystemExit('Missing/invalid locally supplied module: ' + str(source))
        shutil.copy2(source, app / 'sce_module' / name)
        recorded[name] = hashlib.sha256(source.read_bytes()).hexdigest()
    (app.parent / 'private-module-hashes.json').write_text(json.dumps(recorded, indent=2) + '\n')
else:
    (app.parent / 'private-module-hashes.json').unlink(missing_ok=True)
    print('No modules bundled. The PS4 must provide the matching pair under /data/self/system/common/lib/.')
PY
python3 "$root/tools/make_sfo.py" "$app/sce_sys/param.json" "$app/sce_sys/param.sfo"
"$gotools/create-gp4" -out "$app/$title.gp4" -content-id "$content" -path "$app"
rm -f -- "$build/$content.pkg"
DOTNET_ROLL_FORWARD=LatestMajor dotnet "$toolchain/pkgtool/PkgTool.Core.dll" pkg_build "$app/$title.gp4" "$build"
DOTNET_ROLL_FORWARD=LatestMajor dotnet "$toolchain/pkgtool/PkgTool.Core.dll" pkg_validate "$build/$content.pkg"
printf 'Private capture diagnostic: %s\n' "$build/$content.pkg"
