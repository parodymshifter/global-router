#!/usr/bin/env python3
"""churn.py -- Part 216: how many nets' routes change between consecutive refinement rounds of a timing loop (the
case for warm-started rounds, method 2 of the 2026-11-24 review), against how many nets change class.

  python3 churn.py WORK_DIR TAG_PREFIX        e.g.  .../cap48.visible.mempool_tile_wrap k180rp1m

For each pair of consecutive rounds (route files TAG_PREFIX<k>.route[.xz]) it reports: nets whose 3D route differs,
nets whose 2D footprint (wire tiles, layers and vias ignored) differs, and nets whose merged slack changed (the
classes' input: merged<k>.slack). Routes in DEF units, one segment per line.
"""
import lzma
import os
import re
import sys


def routes(path):
    op = lzma.open if path.endswith('.xz') else open
    out, name, segs = {}, None, []
    with op(path, 'rt') as f:
        for line in f:
            line = line.strip()
            if line == '(':
                segs = []
            elif line == ')':
                out[name] = segs
            elif line and '(' not in line and ' ' not in line:
                name = line
            elif line:
                segs.append(line)
    return out


def footprint(segs):
    fp = set()
    for s in segs:
        x1, y1, l1, x2, y2, l2 = s.split()
        if l1 != l2:
            continue  # a via
        a, b = sorted((int(x1), int(x2))), sorted((int(y1), int(y2)))
        fp.add((a[0], b[0], a[1], b[1]))
    return frozenset(fp)


def slack(path):
    d = {}
    for line in open(path):
        if line.startswith('#') or not line.strip():
            continue
        i, v = line.split()
        d[int(i)] = float(v)
    return d


def main():
    work, pre = sys.argv[1], sys.argv[2]
    ks = sorted(int(m.group(1)) for f in os.listdir(work) for m in [re.match(re.escape(pre) + r'(\d+)\.route(\.xz)?$', f)] if m)
    prev = None
    print('round  nets  3D-changed  2D-changed  slack-changed')
    for k in ks:
        p = os.path.join(work, f'{pre}{k}.route')
        r = routes(p if os.path.exists(p) else p + '.xz')
        ms = os.path.join(work, f'merged{k}.slack')
        s = slack(ms) if os.path.exists(ms) else None
        if prev is not None:
            pr, ps = prev
            common = set(r) & set(pr)
            d3 = sum(1 for n in common if r[n] != pr[n])
            d2 = sum(1 for n in common if footprint(r[n]) != footprint(pr[n]))
            dsl = (sum(1 for i in set(s) | set(ps) if s.get(i) != ps.get(i)) if s is not None and ps is not None else -1)
            print(f'{k-1:>3}->{k:<3} {len(common):>6} {d3:>8} ({100*d3/len(common):4.1f}%) {d2:>8} ({100*d2/len(common):4.1f}%)'
                  f' {dsl:>8}')
        prev = (r, s)


if __name__ == '__main__':
    main()
