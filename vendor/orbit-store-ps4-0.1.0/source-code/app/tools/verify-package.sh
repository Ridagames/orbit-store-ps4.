#!/usr/bin/env bash
# Verify the packaged bytes, not just the staging folder. Run in the app build image.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

eval "$(bash tools/setup-orbis-toolchain.sh)"
gotools="$root/.deps/orbis/gotools/bin"
title_id=$(python3 -c 'import json; print(json.load(open("sce_sys/param.json"))["titleId"])')
content_id=$(python3 -c 'import json; print(json.load(open("sce_sys/param.json"))["contentId"])')
pkg="dist/$content_id.pkg"
# A failed verification must never leave an older success record behind.
rm -f build/package-verification.json
[[ -f $pkg && -f dist/$title_id/eboot.bin && -f build/eboot.oelf ]] || {
    echo "build dist/ and the .pkg first (make pkg)" >&2
    exit 2
}

mkdir -p build
temporary=$(mktemp -d "$root/build/verify-package.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT

# The OELF (create-fself's intermediate) is inspectable: imports must be
# real PS4 modules. The plain ELF predates dynlib-data generation.
"$gotools/readoelf" -d build/eboot.oelf >"$temporary/eboot.txt"
for module in libScePigletv2VSH libSceAudioOut libScePad libkernel; do
    grep -q "Shared library: \[$module.prx\]" "$temporary/eboot.txt" || {
        echo "eboot.oelf does not import $module.prx" >&2
        exit 1
    }
done

# pkg_makegp4 materializes the full app root (eboot.bin, sce_sys/, assets/)
# plus a regenerated project file; pkg_extract leaves header entries such as
# param.sfo unmaterialized.
DOTNET_ROLL_FORWARD=LatestMajor \
dotnet "$OO_PS4_TOOLCHAIN/pkgtool/PkgTool.Core.dll" pkg_makegp4 "$pkg" \
    "$temporary/pkg" >"$temporary/extract.log" 2>&1 || {
    echo "pkg_makegp4 failed" >&2
    cat "$temporary/extract.log" >&2
    exit 1
}
extracted="$temporary/pkg"
[[ -f $extracted/eboot.bin && -f $extracted/sce_sys/param.sfo ]] || {
    echo "pkg_makegp4 produced no app root with eboot.bin" >&2
    exit 1
}

DOTNET_ROLL_FORWARD=LatestMajor \
dotnet "$OO_PS4_TOOLCHAIN/pkgtool/PkgTool.Core.dll" pkg_validate "$pkg" \
    >"$temporary/validate.log" 2>&1 || {
    echo "pkg_validate failed" >&2
    cat "$temporary/validate.log" >&2
    exit 1
}

python3 - "$extracted" "$pkg" "$PWD" <<'PY'
import hashlib, json, os, re, struct, subprocess, sys
from pathlib import Path

extracted = Path(sys.argv[1])
pkg = Path(sys.argv[2])
assert extracted.name == "Image0" or (extracted / "eboot.bin").exists(), \
    f"unexpected pkg extraction layout: {extracted}"
image = extracted if (extracted / "eboot.bin").exists() else extracted / "Image0"

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

# Release evidence requires the current payload, the staged copy, and the
# extracted copy to be identical; staging alone can be left over from an old build.
repo = Path(sys.argv[3]).resolve()
payload = repo / '../build/orbit_store.elf'
staged = repo / 'dist' / json.loads((repo / 'sce_sys/param.json').read_text())['titleId'] / 'orbit/orbit_store.elf'
packaged = image / 'orbit/orbit_store.elf'
assert payload.is_file() and staged.is_file() and packaged.is_file(), \
    'release verification requires a bundled Orbit payload'
payload_hash = digest(packaged)
assert payload_hash == digest(staged) == digest(payload), \
    'pkg payload differs from staging or the current service build; rebuild make pkg'
header = (repo / '../backend/orbit.h').read_text()
match = re.search(r'#define ORBIT_VERSION "([^"]+)"', header)
assert match, 'missing service version'
version = (image / 'orbit/version.txt').read_text().strip()
assert version == match.group(1), 'packaged service version differs from source'
assert b'Orbit-Store/' + version.encode() + b'\0' in packaged.read_bytes(), \
    'payload version marker differs'
# The patched elfldr.elf and its GoldHEN BinLoader bootstrap must both match
# the fresh SDK-image outputs and the bytes staged into the package.
source_elfldr = repo / '../.deps/elfldr/elfldr.elf'
staged_elfldr = repo / 'build/package-extra/orbit/elfldr.elf'
packaged_elfldr = image / 'orbit/elfldr.elf'
assert source_elfldr.is_file() and staged_elfldr.is_file() and packaged_elfldr.is_file(), \
    'pkg elfldr.elf missing'
assert packaged_elfldr.read_bytes()[:6] == b'\x7fELF\x02\x01', \
    'pkg elfldr.elf has an invalid ELF64 header'
assert digest(packaged_elfldr) == digest(staged_elfldr) == digest(source_elfldr), \
    'pkg elfldr.elf differs from the fresh guarded 14.02 SDK build'
source_elfldr_bin = repo / '../.deps/elfldr/elfldr.bin'
staged_elfldr_bin = repo / 'build/package-extra/orbit/elfldr.bin'
packaged_elfldr_bin = image / 'orbit/elfldr.bin'
assert source_elfldr_bin.is_file() and staged_elfldr_bin.is_file() and packaged_elfldr_bin.is_file(), \
    'pkg elfldr.bin bootstrap missing'
assert 64 <= packaged_elfldr_bin.stat().st_size <= 16 * 1024 * 1024, \
    'pkg elfldr.bin has an invalid size'
assert digest(packaged_elfldr_bin) == digest(staged_elfldr_bin) == digest(source_elfldr_bin), \
    'pkg elfldr.bin differs from the fresh guarded 14.02 SDK build'

assert digest(image / 'eboot.bin') == digest(Path('build/eboot.bin')), \
    'pkg eboot.bin differs from the signed build output'

runtime = repo / '.deps/orbis/runtime'
for module in ['libc.prx', 'libSceFios2.prx']:
    assert digest(image / 'sce_module' / module) == digest(runtime / 'sce_module' / module), \
        f'packaged startup module missing or changed: {module}'

# param.sfo: TITLE_ID and CONTENT_ID must match sce_sys/param.json.
raw = (image / 'sce_sys/param.sfo').read_bytes()
magic, sfo_version, key_start, data_start, count = struct.unpack_from('<5I', raw)
assert magic == 0x46535000, 'param.sfo magic'
assert data_start % 4 == 0, 'param.sfo value table is not aligned'
entries = {}
with pkg.open('rb') as package:
    package.seek(0x40)
    header_content_id = package.read(48)
for i in range(count):
    key_off, fmt, dlen, dmax, doff = struct.unpack_from('<HHIII', raw, 0x14 + 16 * i)
    name = raw[key_start + key_off:raw.find(b'\0', key_start + key_off)].decode()
    if name == 'CONTENT_ID':
        assert dmax == 48, 'CONTENT_ID must reserve the full 48-byte PKG field'
        assert raw[data_start + doff:data_start + doff + dmax] == header_content_id, \
            'CONTENT_ID differs from the fixed-width PKG header'
    entries[name] = raw[data_start + doff:data_start + doff + dlen].rstrip(b'\0')
param = json.loads((image / 'sce_sys/param.json').read_text())
assert param == json.loads(Path('sce_sys/param.json').read_text()), \
    'packaged native metadata differs from source; rebuild make pkg'
assert entries['APP_VER'].decode() == param['appVersion'], 'APP_VER mismatch'
assert entries['VERSION'].decode() == param['masterVersion'], 'VERSION mismatch'
assert entries['TITLE_ID'].decode() == param['titleId'], 'TITLE_ID mismatch'
assert entries['CONTENT_ID'].decode() == param['contentId'], 'CONTENT_ID mismatch'
assert entries['CATEGORY'].decode() == param['category'] == 'gde', 'Piglet CATEGORY must be gde'
assert entries['TITLE'].decode().startswith('Orbit'), 'TITLE missing'

for language in Path('../i18n').glob('*.json'):
    if language.stem != 'en':
        assert digest(image / 'assets/i18n' / language.name) == digest(language), \
            f'packaged translation differs: {language.name}'
for font in Path('assets/fonts').iterdir():
    if font.is_file():
        assert digest(image / 'assets/fonts' / font.name) == digest(font), \
            f'packaged font/licence differs: {font.name}'

# The preview's GLSL compiler cannot stand in for retail Piglet. A release
# package must carry every shader and its exact matching source.
import importlib.util
spec = importlib.util.spec_from_file_location('shader_assets', repo / 'tools/shader-assets.py')
shaders = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shaders)
shader_hashes = shaders.verify(image / 'assets/shaders')
assert shader_hashes == shaders.verify(repo / 'assets/shaders'), 'packaged shaders differ from source assets'

print(f'verified: {pkg.name} ({pkg.stat().st_size} bytes, {count} sfo keys)')

# Stale-detection inputs for tools/package-release.mjs: the exact payload,
# package and source this verification covered.
commit = os.environ.get('ORBIT_SOURCE_COMMIT', '')
if not commit:
    try:
        commit = subprocess.run(['git', '-c', 'safe.directory=*', 'rev-parse', 'HEAD'],
                                cwd=repo, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        bundle = repo / '../../SOURCES.json'
        if bundle.is_file():
            commit = json.loads(bundle.read_text()).get('sourceCommit', '')
assert commit, 'Set ORBIT_SOURCE_COMMIT when building outside a checkout or source bundle'
verification = {
    'sourceCommit': commit,
    'payloadVersion': version,
    'contentVersion': param['contentVersion'],
    'appVersion': param['appVersion'],
    'titleId': param['titleId'],
    'payloadSha256': payload_hash,
    'imageSha256': digest(pkg),
    'shaderAssetsVerified': True,
    'shaderHashes': shader_hashes,
}
(Path('build') / 'package-verification.json').write_text(
    json.dumps(verification, indent=2) + '\n')
PY
