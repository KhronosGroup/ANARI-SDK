#!/usr/bin/env python3
"""
fix_msl_buffers.py - Reorder Metal [[buffer(N)]] indices to match SDL3_gpu convention.

SDL3_gpu requires uniform buffers (constant address space) to occupy the lowest
[[buffer(N)]] indices, followed by storage buffers (device address space).
spirv-cross may interleave them depending on the SPIR-V variable ID ordering.
This script renumbers so that constant parameters come first.
"""
import re
import sys


def fix_buffers(src):
    # Match the entry-point function signature (vertex, fragment, or kernel).
    # Use a lookahead for the opening brace so the non-greedy .*? stops at the
    # correct closing paren of the parameter list, not at [[buffer(N)]] parens.
    sig_re = re.compile(
        r'((?:vertex|fragment|kernel)\s+\S+\s+\w+\s*\(.*?\))(?=\s*\n\s*\{)',
        re.DOTALL,
    )
    m = sig_re.search(src)
    if not m:
        return src

    sig = m.group(1)
    paren_open = sig.index('(')
    paren_close = sig.rindex(')')
    params_str = sig[paren_open + 1:paren_close]

    # Split on ', ' — safe because Metal resource types don't contain commas
    params = [p.strip() for p in params_str.split(',')]

    buf_re = re.compile(r'\[\[buffer\((\d+)\)\]\]')
    ubos = []   # (old_index, param_string) — constant address space
    ssbos = []  # (old_index, param_string) — device address space
    others = [] # parameters without a [[buffer(N)]] attribute

    for p in params:
        bm = buf_re.search(p)
        if bm:
            old_idx = int(bm.group(1))
            if re.search(r'\bconstant\b', p):
                ubos.append((old_idx, p))
            else:
                ssbos.append((old_idx, p))
        else:
            others.append(p)

    # Nothing to do if there are no mixed buffer types
    if not ubos or not ssbos:
        return src

    # Already correct if all UBO indices are lower than all SSBO indices
    if max(i for i, _ in ubos) < min(i for i, _ in ssbos):
        return src

    # Sort each group by old index to preserve relative ordering within the group,
    # then assign new indices: UBOs at 0..K-1, SSBOs at K..K+M-1
    ubos.sort(key=lambda x: x[0])
    ssbos.sort(key=lambda x: x[0])

    new_params = []
    for i, (_, p) in enumerate(ubos):
        new_params.append(buf_re.sub(f'[[buffer({i})]]', p))
    for i, (_, p) in enumerate(ssbos):
        new_params.append(buf_re.sub(f'[[buffer({len(ubos) + i})]]', p))
    new_params.extend(others)

    new_sig = sig[:paren_open + 1] + ', '.join(new_params) + sig[paren_close:]
    return src[:m.start()] + new_sig + src[m.end():]


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} input.msl output.msl', file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1]) as f:
        src = f.read()
    with open(sys.argv[2], 'w') as f:
        f.write(fix_buffers(src))
