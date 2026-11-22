#!/usr/bin/env python3
"""Prepare corresponding-source inputs locally; never publishes binaries."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(root, *args, **kwargs):
    return subprocess.check_output(['git', *args], cwd=root, **kwargs)


def source_paths(root):
    """Include current tracked/untracked source; honor archive exclusions on parents."""
    names = set(git(root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z')
                .decode().strip('\0').split('\0')) - {''}
    candidates = {name for name in names if (root / name).is_file() or (root / name).is_symlink()}
    ancestors = candidates | {str(parent) + '/' for name in candidates
                              for parent in Path(name).parents if str(parent) != '.'}
    attrs = git(root, 'check-attr', '-z', '--stdin', 'export-ignore',
                input=('\0'.join(sorted(ancestors)) + '\0').encode()).decode().split('\0')
    excluded = {attrs[i].rstrip('/') for i in range(0, len(attrs) - 1, 3) if attrs[i + 2] == 'set'}
    result = []
    for name in sorted(candidates):
        if name in excluded or any(str(parent) in excluded for parent in Path(name).parents):
            continue
        path = root / name
        if path.is_symlink() and not path.resolve().is_relative_to(root.resolve()):
            raise ValueError(f'Source symlink points outside the repository: {name}')
        result.append(name)
    return result


def validate_manifest(items):
    import re
    names = set()
    for item in items:
        name = item.get('name', '')
        if (not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', name) or name in names or
                not re.fullmatch(r'[a-f0-9]{64}', item.get('sha256', '')) or
                not item.get('url', '').startswith('https://') or
                item.get('kind') not in {'source', 'build-input'}):
            raise ValueError(f'Invalid or duplicate source-manifest entry: {name}')
        names.add(name)


def prepare(root, out, *, review=False, fetch=False):
    root = root.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError(f'Output directory must be empty: {out}')
    manifest = json.loads((root / 'tools/release-sources.json').read_text())
    validate_manifest(manifest)
    provenance = json.loads((root / 'tools/release-provenance.json').read_text())
    unresolved = [item for item in provenance['components'] if item['sourceVerified'] is not True]
    native_proof = None
    if provenance.get('nativeRuntimeProof'):
        runtime = root / provenance['nativeRuntimeProof']
        spec = importlib.util.spec_from_file_location('orbis_runtime', root / 'app/tools/setup-orbis-runtime.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        native_proof = module.verify(runtime.parent)
        for name, expected in native_proof['recipes'].items():
            if digest(root / name) != expected:
                raise ValueError(f'Native runtime recipe changed: {name}; rebuild before preparing source')
        indexed = {item['name']: item for item in manifest}
        for name, item in native_proof['inputs'].items():
            if indexed.get(name) != item:
                raise ValueError(f'Native runtime source pin changed: {name}; rebuild before preparing source')
    dirty = bool(git(root, 'status', '--porcelain').strip())
    if not review and (dirty or unresolved):
        raise ValueError('Release sources require a clean commit and resolved runtime source provenance; use --review for an explicitly incomplete local audit.')
    cache = root / 'build/upstream'
    # Docker may own the native-build cache. Read existing inputs there, but
    # download missing source-only archives into the caller's local state.
    downloads = root / '.state/source-downloads'
    paths = {}
    for item in manifest:
        path = cache / item['name']
        if not path.exists():
            path = downloads / item['name']
        if not path.exists():
            if not fetch:
                raise ValueError(f'Missing cached input: {path}; supply it or use --fetch')
            downloads.mkdir(parents=True, exist_ok=True)
            temporary = path.with_name(path.name + '.part')
            try:
                with urllib.request.urlopen(item['url'], timeout=90) as response, temporary.open('wb') as stream:
                    shutil.copyfileobj(response, stream)
                if digest(temporary) != item['sha256']:
                    raise ValueError(f'Checksum mismatch: {item["name"]}')
                temporary.rename(path)
            finally:
                temporary.unlink(missing_ok=True)
        if digest(path) != item['sha256']:
            raise ValueError(f'Checksum mismatch: {item["name"]}')
        paths[item['name']] = path
    names = source_paths(root)
    version = json.loads((root / 'package.json').read_text())['version']
    archive = f'orbit-store-{version}-source.tar.gz'
    out.mkdir(parents=True, exist_ok=True)
    with tarfile.open(out / archive, 'w:gz', compresslevel=6) as tar:
        for name in names:
            tar.add(root / name, arcname=f'orbit-store-{version}/{name}', recursive=False)
    for item in manifest:
        shutil.copy2(paths[item['name']], out / item['name'])
    for name in ['LICENSE', 'THIRD-PARTY-NOTICES.md', 'SOURCE-BUNDLE.md', 'BUILDING.md']:
        shutil.copy2(root / name, out / name)
    shutil.copytree(root / 'licenses', out / 'licenses')
    (out / 'build-inputs/cjson').mkdir(parents=True)
    for name in ['cJSON.c', 'cJSON.h', 'LICENSE']:
        shutil.copy2(root / '.deps/cjson' / name, out / 'build-inputs/cjson' / name)
    shutil.copy2(root / '.deps/cacert.pem', out / 'build-inputs/cacert.pem')
    if native_proof is not None:
        (out / 'build-inputs/openorbis-runtime-provenance.json').write_text(
            json.dumps(native_proof, indent=2) + '\n')
    metadata = {
        'platform': 'PS4', 'version': version,
        'sourceCommit': git(root, 'rev-parse', 'HEAD').decode().strip(),
        'workingTreeChangesIncluded': dirty, 'sourceArchive': archive,
        'reviewOnly': review, 'completeCorrespondingSource': not unresolved,
        'unresolvedComponents': unresolved, 'upstream': manifest,
    }
    (out / 'SOURCES.json').write_text(json.dumps(metadata, indent=2) + '\n')
    if review:
        (out / 'REVIEW-ONLY.txt').write_text(
            'LOCAL SOURCE AUDIT. Not a release or licence clearance.\n'
            'See SOURCES.json for unresolved component provenance. No binaries are included.\n')
    paths = sorted(path for path in out.rglob('*') if path.is_file())
    (out / 'SOURCE-SHA256SUMS').write_text(''.join(
        f'{digest(path)}  {path.relative_to(out).as_posix()}\n' for path in paths))
    with zipfile.ZipFile(out / f'orbit-store-{version}-source-bundle.zip', 'w', zipfile.ZIP_STORED) as bundle:
        for path in [*paths, out / 'SOURCE-SHA256SUMS']:
            bundle.write(path, path.relative_to(out).as_posix())
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--review', action='store_true', help='allow an explicitly incomplete current-working-tree audit')
    parser.add_argument('--fetch', action='store_true', help='download missing hash-pinned inputs')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    result = prepare(root, args.output.resolve(), review=args.review, fetch=args.fetch)
    print(json.dumps({'output': str(args.output), 'reviewOnly': result['reviewOnly'],
                      'unresolvedComponents': [item['name'] for item in result['unresolvedComponents']]}, indent=2))


if __name__ == '__main__':
    main()
