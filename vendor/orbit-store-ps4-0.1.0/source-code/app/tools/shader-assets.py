#!/usr/bin/env python3
"""Export exact Piglet shader inputs or verify captured release assets.

No Sony module is downloaded or bundled. Capture the eight stages using a
working compiler, then place the .bin/.glsl pairs in assets/shaders/.
"""
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def string_initializer(text, name):
    # A GLSL declaration's semicolon is inside a C++ string. Match complete
    # adjacent literals before looking for the C++ statement terminator.
    literal = r'"(?:[^"\\]|\\.)*"'
    match = re.search(rf'\b{re.escape(name)}\s*=\s*((?:{literal}\s*)+);', text)
    if not match:
        raise ValueError(f'Missing or unsupported shader prefix: {name}')
    return ''.join(ast.literal_eval(value) for value in re.findall(literal, match.group(1)))


def programs():
    program = (ROOT / 'src/gfx/gl_program.cpp').read_text()
    prefixes = {}
    for stage in ('vertex', 'fragment'):
        prefixes[stage] = string_initializer(program, f'g_{stage}_prefix')
    result = []
    for file in ('backdrop.cpp', 'gl_batch.cpp', 'renderer.cpp'):
        text = (ROOT / 'src/gfx' / file).read_text()
        constants = dict(re.findall(r'constexpr const char \*(\w+)\s*=\s*R"\((.*?)\)";', text, re.S))
        for name, vertex, fragment, attrs in re.findall(
                r'build_program\("([\w-]+)",\s*(\w+),\s*(\w+),\s*\{([^}]+)\}\)', text):
            result.append({'name': name,
                           'sources': [prefixes['vertex'] + constants[vertex],
                                       prefixes['fragment'] + constants[fragment]],
                           'attributes': re.findall(r'"(\w+)"', attrs)})
    if len(result) != 4 or len({p['name'] for p in result}) != 4:
        raise ValueError('Expected four distinct shader programs; update the inventory for renderer changes')
    return result


def sources():
    return {program['name'] + '.' + stage: program['sources'][i].encode()
            for program in programs() for i, stage in enumerate(('vert', 'frag'))}


def capture_header():
    lines = ['// Generated from the exact renderer sources and attribute order.',
             'struct CaptureProgram { const char *name; const char *sources[2]; const char *attributes[8]; };',
             'static const CaptureProgram programs[] = {']
    for program in programs():
        if len(program['attributes']) > 7:
            raise ValueError('Too many shader attributes for the capture table')
        lines.append('{' + json.dumps(program['name']) + ', {' +
                     ', '.join(json.dumps(s) for s in program['sources']) + '}, {' +
                     ', '.join([*(json.dumps(a) for a in program['attributes']), 'nullptr']) + '}},')
    return '\n'.join([*lines, '};', ''])


def verify(directory):
    evidence = {}
    for name, text in sources().items():
        source, binary = directory / (name + '.glsl'), directory / (name + '.bin')
        if not source.is_file() or source.read_bytes() != text:
            raise ValueError(f'Missing or stale Piglet shader source: {source}')
        if not binary.is_file() or not 4 < binary.stat().st_size <= 1024 * 1024:
            raise ValueError(f'Missing or invalid Piglet shader binary: {binary}')
        evidence[name] = hashlib.sha256(binary.read_bytes()).hexdigest()
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['export', 'verify', 'capture-header'])
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    if args.action == 'capture-header':
        args.directory.parent.mkdir(parents=True, exist_ok=True)
        args.directory.write_text(capture_header())
    elif args.action == 'export':
        args.directory.mkdir(parents=True, exist_ok=True)
        for name, text in sources().items():
            (args.directory / (name + '.glsl')).write_bytes(text)
    else:
        try:
            print(json.dumps(verify(args.directory), indent=2))
        except ValueError as error:
            raise SystemExit(str(error))
