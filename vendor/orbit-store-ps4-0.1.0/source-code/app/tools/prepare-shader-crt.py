#!/usr/bin/env python3
"""Give only the private shader compiler a larger system-library heap."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import hashlib
from pathlib import Path

PINNED_CRT_SHA256 = '57927fe1f9274c6119eb8f9111f1d649e7214778d3184826b1917656c3c64676'


def capture_crt(data):
    if hashlib.sha256(data).hexdigest() != PINNED_CRT_SHA256:
        raise ValueError('Unexpected PS4 CRT source; review its libc parameter layout first')
    text = data.decode()
    start = text.index('// sceLibcParam\n')
    end = text.index('// sceKernelMemParam\n', start)
    block = text[start:end]
    lines = block.splitlines(keepends=True)
    fields = [i for i, line in enumerate(lines) if '.quad' in line]
    if len(fields) != 18:
        raise ValueError('Unexpected PS4 libc parameter count')
    # Pointer fields, not immediate byte counts. The compiler's imported malloc
    # uses LibcInternal, independently of musl and Piglet's graphics allocation.
    for offset, symbol in [(0x58, 'orbit_capture_internal_memory_size'),
                           (0x68, 'orbit_capture_libc_max_system_size')]:
        index = fields[offset // 8]
        if lines[index].split('.quad', 1)[1].strip() != r'0 \n"':
            raise ValueError('Unexpected nonzero PS4 libc memory parameter')
        lines[index] = '"\\t.quad\\t' + symbol + ' \\n"\n'
    text = text[:start] + ''.join(lines) + text[end:]
    text += ('\n__asm__(".data\\n.align 8\\n'
             'orbit_capture_internal_memory_size: .quad 33554432\\n'
             'orbit_capture_libc_max_system_size: .quad 268435456\\n");\n')
    return text


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    args.destination.write_text(capture_crt(args.source.read_bytes()))
