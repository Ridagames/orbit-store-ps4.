#!/usr/bin/env python3
"""Assemble a source-traced native runtime; rebuild musl and link-only imports."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p)
            for directory in ['include', 'lib', 'sce_module']
            for p in sorted((root / directory).rglob('*')) if p.is_file()} | {
                'link.x': digest(root / 'link.x')}


def verify(root, fingerprint=None):
    proof = json.loads((root / 'provenance.json').read_text())
    if fingerprint is not None and proof['fingerprint'] != fingerprint:
        raise ValueError('Runtime build inputs changed')
    if inventory(root) != proof['files']:
        raise ValueError('Runtime files changed; rebuild before linking')
    return proof


def extract(archive, destination, *, strip_root=False, select=lambda name: True):
    """Extract selected files, allowing only relative internal symlinks."""
    with tarfile.open(archive, 'r:gz') as tar:
        for member in tar:
            name = member.name.removeprefix('./')
            if strip_root:
                name = name.partition('/')[2]
            if not name or not select(name):
                continue
            path = destination / name
            if Path(name).is_absolute() or '..' in Path(name).parts:
                raise ValueError(f'Unsafe archive member: {name}')
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                path.parent.mkdir(parents=True, exist_ok=True)
                with tar.extractfile(member) as source, path.open('wb') as output:
                    shutil.copyfileobj(source, output)
                path.chmod(member.mode & 0o777)
            elif member.issym():
                target = (path.parent / member.linkname).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError(f'Unsafe archive symlink: {name}')
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to(member.linkname)
            else:
                raise ValueError(f'Unsupported archive member: {name}')


def stub_source(symbols):
    if (not symbols or len(symbols) != len(set(symbols)) or
            any(not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', s) for s in symbols)):
        raise ValueError('Invalid or duplicate import symbol')
    # These bodies are never shipped or executed. create-fself records their
    # symbol/module names and the console resolves them to its system modules.
    lines = ['.text']
    for name in symbols:
        lines += [f'.globl {name}', f'.type {name}, @function', f'{name}:',
                  'ud2', f'.size {name}, .-{name}']
    lines += ['.section .note.GNU-stack,"",@progbits']
    return '\n'.join(lines) + '\n'


def fix_ps4_stat_abi(source):
    # The pinned musl PS4 arch override incorrectly widens mode_t to 32 bits.
    # PS4's FreeBSD-derived stat ABI has a 16-bit mode followed by a 16-bit
    # link count. Widening it shifts st_size from 72 to 80, reading st_blocks
    # as the file size. Fix the source before building libc and its headers.
    path = source / 'arch/ps4/bits/alltypes.h.in'
    original = 'TYPEDEF unsigned int mode_t;'
    text = path.read_text()
    if text.count(original) != 1:
        raise ValueError('Unexpected pinned PS4 mode_t definition')
    path.write_text(text.replace(original, 'TYPEDEF unsigned short mode_t;'))


def fix_ps4_clock_ids(source):
    # The pinned port imports clock_gettime directly from libkernel, but its
    # time.h retains Linux clock IDs. PS4/FreeBSD ID 1 is CPU time, not elapsed
    # time: animations and reconnect deadlines then barely advance while idle.
    path = source / 'include/time.h'
    text = path.read_text()
    for name, before, after in [('CLOCK_MONOTONIC', 1, 4),
                                ('CLOCK_PROCESS_CPUTIME_ID', 2, 15),
                                ('CLOCK_THREAD_CPUTIME_ID', 3, 14)]:
        pattern = rf'^(#define\s+{name}\s+){before}$'
        text, count = re.subn(pattern, lambda m: m[1] + str(after), text, flags=re.M)
        if count != 1:
            raise ValueError('Unexpected pinned PS4 clock definition: ' + name)
    path.write_text(text)


def fix_ps4_send_flag(source):
    # send() is imported from libkernel. The inherited Linux 0x4000 flag is
    # not PS4's MSG_NOSIGNAL and breaks native HTTP/ELF-loader transfers.
    path = source / 'include/sys/socket.h'
    text, count = re.subn(r'^(#define\s+MSG_NOSIGNAL\s+)0x4000$',
                          lambda m: m[1] + '0x20000', path.read_text(), flags=re.M)
    if count != 1:
        raise ValueError('Unexpected pinned PS4 MSG_NOSIGNAL definition')
    path.write_text(text)


def prepare(repo, base):
    recipe = Path(__file__).resolve()
    lock_path = repo / 'app/tooling/orbis-runtime-lock.json'
    imports_path = repo / 'app/tooling/orbis-imports.json'
    lock = json.loads(lock_path.read_text())
    manifest = {item['name']: item for item in
                json.loads((repo / 'tools/release-sources.json').read_text())}
    names = [lock[key] for key in ['toolchainSource', 'llvmBinary', 'llvmSource', 'muslSource']]
    inputs = {name: manifest[name] for name in names}
    commands = {name: shutil.which(os.environ.get(variable, default)) for name, variable, default in [
        ('cc', 'PS4_CC', 'clang-18'), ('ar', 'PS4_AR', 'llvm-ar-18'),
        ('ranlib', 'PS4_RANLIB', 'llvm-ranlib-18'), ('lld', 'PS4_LLD', 'ld.lld-18')]}
    if not all(commands.values()):
        raise ValueError('clang-18, llvm-ar-18, llvm-ranlib-18 and ld.lld-18 are required')
    compiler = {name: subprocess.check_output([path, '--version'], text=True).strip()
                for name, path in commands.items()}
    identity = {'inputs': inputs, 'compiler': compiler, 'target': lock['target'],
                'createFselfSha256': digest(base.parent / 'gotools/bin/create-fself'),
                'recipes': {p.relative_to(repo).as_posix(): digest(p)
                            for p in [recipe, lock_path, imports_path]}}
    fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    cache = base.parent
    runtime = cache / 'runtime'
    try:
        verify(runtime, fingerprint)
        return runtime
    except (OSError, ValueError, KeyError):
        pass
    upstream = repo / 'build/upstream'
    upstream.mkdir(parents=True, exist_ok=True)
    for name, item in inputs.items():
        archive = upstream / name
        if not archive.exists():
            temporary = archive.with_suffix(archive.suffix + '.part')
            try:
                with urllib.request.urlopen(item['url'], timeout=90) as source, temporary.open('wb') as out:
                    shutil.copyfileobj(source, out)
                if digest(temporary) != item['sha256']:
                    raise ValueError(f'Checksum mismatch: {name}')
                temporary.replace(archive)
            finally:
                temporary.unlink(missing_ok=True)
        if digest(archive) != item['sha256']:
            raise ValueError(f'Checksum mismatch: {name}')
    work = cache / 'runtime-build'
    if work.exists():
        shutil.rmtree(work)
    stage = work / 'install'
    stage.mkdir(parents=True)
    # Take headers/link script directly from the identified toolchain source,
    # not from unidentified binaries in the SDK bundle. SDL is not used.
    extract(upstream / lock['toolchainSource'], stage, strip_root=True,
            select=lambda name: name == 'link.x' or name.startswith('include/'))
    extract(upstream / lock['llvmBinary'], stage,
            select=lambda name: name.startswith(('include/', 'lib/')))
    for name, expected in lock['recoveredLibraries'].items():
        if digest(stage / name) != expected:
            raise ValueError(f'Recovered LLVM library mismatch: {name}')
    source = work / 'musl'
    extract(upstream / lock['muslSource'], source, strip_root=True)
    fix_ps4_stat_abi(source)
    fix_ps4_clock_ids(source)
    fix_ps4_send_flag(source)
    build = work / 'musl-build'
    build.mkdir()
    log_path = cache / 'runtime-build.log'
    print(f'Building pinned PS4 runtime (log: {log_path})', file=sys.stderr)
    with log_path.open('w') as log:
        def run(args, cwd=build):
            subprocess.run(args, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
        # configure's --target selects musl sources; Clang needs its own target
        # as well, especially when the build host is ARM64.
        run([str(source / 'configure'), '--target=x86_64-scei-ps4', '--disable-shared',
             '--disable-wrapper', f'--prefix={stage}',
             f'CC={commands["cc"]} --target={lock["target"]}',
             'CFLAGS=-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1',
             f'AR={commands["ar"]}', f'RANLIB={commands["ranlib"]}'])
        run(['make', '-j' + os.environ.get('BUILD_JOBS', '4')])
        run(['make', 'install'])
        abi = work / 'stat-abi.c'
        abi.write_text('#include <stddef.h>\n#include <sys/stat.h>\n#include <time.h>\n#include <sys/socket.h>\n'
                       '_Static_assert(sizeof(mode_t) == 2, "PS4 mode_t");\n'
                       '_Static_assert(offsetof(struct stat, st_size) == 72, "PS4 stat size offset");\n'
                       '_Static_assert(sizeof(struct stat) == 120, "PS4 stat size");\n'
                       '_Static_assert(CLOCK_MONOTONIC == 4, "PS4 elapsed clock");\n'
                       '_Static_assert(CLOCK_PROCESS_CPUTIME_ID == 15, "PS4 process clock");\n'
                       '_Static_assert(CLOCK_THREAD_CPUTIME_ID == 14, "PS4 thread clock");\n'
                       '_Static_assert(MSG_NOSIGNAL == 0x20000, "PS4 socket send flag");\n')
        run([commands['cc'], '--target=' + lock['target'], '-isystem', str(stage / 'include'),
             '-c', str(abi), '-o', str(work / 'stat-abi.o')])
        stubs = work / 'stubs'
        stubs.mkdir()
        libraries = json.loads(imports_path.read_text())['libraries']
        for name, symbols in libraries.items():
            if not re.fullmatch(r'lib[A-Za-z0-9_]+\.so', name):
                raise ValueError(f'Invalid import library name: {name}')
            assembly = stubs / (name + '.s')
            obj = assembly.with_suffix('.o')
            assembly.write_text(stub_source(symbols))
            run([commands['cc'], '--target=' + lock['target'], '-c', str(assembly), '-o', str(obj)])
            run([commands['lld'], '-shared', '--soname=' + name, str(obj), '-o', str(stage / 'lib' / name)])
        # libkernel loads these application-local modules before the entry point,
        # even when musl is statically linked. Omitting them produces
        # PRX_SCE_MODULE_LOAD_ERROR on hardware. Build the same open-source stubs
        # shipped with OpenOrbis samples; never copy Sony's system libraries.
        module_source = work / 'startup-sources'
        paths = ['src/crt/crtlib.c', 'src/modules/libc/libc/lib.c',
                 'src/modules/libSceFios2/libSceFios2/lib.c']
        extract(upstream / lock['toolchainSource'], module_source, strip_root=True,
                select=lambda name: name in paths)
        modules = stage / 'sce_module'
        modules.mkdir()
        flags = ['--target=' + lock['target'], '-fPIC', '-funwind-tables',
                 '-isystem', str(stage / 'include')]
        crt = work / 'crtlib.o'
        run([commands['cc'], *flags, '-c', str(module_source / paths[0]), '-o', str(crt)])
        for name, path in zip(['libc', 'libSceFios2'], paths[1:]):
            obj, elf, oelf = [work / (name + suffix) for suffix in ['.o', '.elf', '.oelf']]
            run([commands['cc'], *flags, '-c', str(module_source / path), '-o', str(obj)])
            run([commands['lld'], '-pie', '-T', str(stage / 'link.x'), '--eh-frame-hdr',
                 '-e', 'module_start', '-L' + str(stage / 'lib'), str(obj), str(crt),
                 '-l:libkernel.so', '-o', str(elf)])
            run(['env', 'OO_PS4_TOOLCHAIN=' + str(stage),
                 str(base.parent / 'gotools/bin/create-fself'), '-in', str(elf),
                 '-out', str(oelf), '-lib', str(modules / (name + '.prx')),
                 '-libname', name])
    # Host packaging tools are separate from target libraries.
    (stage / 'pkgtool').symlink_to('../toolchain/pkgtool')
    proof = {'fingerprint': fingerprint, **identity, 'files': inventory(stage),
             'muslConfig': (build / 'config.mak').read_text(),
             'muslSourceModified': True,
             'muslSourcePatches': ['PS4 mode_t uses the 16-bit kernel ABI'],
             'importsGeneratedFrom': 'app/tooling/orbis-imports.json'}
    (stage / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
    verify(stage, fingerprint)
    previous = cache / 'runtime.previous'
    if previous.exists():
        shutil.rmtree(previous)
    if runtime.exists():
        runtime.rename(previous)
    stage.rename(runtime)
    return runtime


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-toolchain', required=True, type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    base = args.base_toolchain.resolve()
    repo = Path(__file__).resolve().parents[2]
    with (base.parent / '.runtime-lock').open('w') as mutex:
        fcntl.flock(mutex, fcntl.LOCK_EX)
        if args.verify:
            verify(base.parent / 'runtime')
        else:
            print(prepare(repo, base))


if __name__ == '__main__':
    main()
