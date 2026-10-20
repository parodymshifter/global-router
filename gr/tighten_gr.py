#!/usr/bin/env python3
"""tighten_gr.py -- write a capacity-tightened copy of an ISPD 2008 .gr benchmark (HISTORY_COST_SWEEP.md Part 142):
related hard cases with a known parent, for studying whether difficult designs from one family share a
"personality". Standard library only.

  python3 gr/tighten_gr.py IN.gr OUT.gr --global F          every edge's capacity x F
  python3 gr/tighten_gr.py IN.gr OUT.gr --hotspot F [--side S]
        every edge inside the S x S-tile window (S = 0.3 x the die's side by default) with the highest mean
        RUDY ratio (each net's pin bounding box spread evenly, as routability.cpp's rudy_*) gets capacity x F

New capacities are rounded down to a whole number of tracks (minimum width + minimum spacing of the layer).
Nets are copied unchanged; the capacity adjustments are rewritten (existing ones kept or tightened, new ones
added, one line per edge). Prints what it changed.
"""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--global', dest='glob', type=float)
    g.add_argument('--hotspot', type=float)
    ap.add_argument('--side', type=float, default=0.3)
    a = ap.parse_args()
    f = a.glob if a.glob is not None else a.hotspot
    if not 0 < f <= 1:
        sys.exit('factor must be in (0, 1]')
    lines = open(a.inp).read().split('\n')
    i = 0

    def nxt():
        nonlocal i
        while i < len(lines) and not lines[i].strip():
            i += 1
        s = lines[i]
        i += 1
        return s
    grid = nxt().split()
    nx, ny, nl = int(grid[1]), int(grid[2]), int(grid[3])
    vcap = [int(v) for v in nxt().split()[2:]]
    hcap = [int(v) for v in nxt().split()[2:]]
    minw = [int(v) for v in nxt().split()[2:]]
    mins = [int(v) for v in nxt().split()[2:]]
    vias = nxt()
    org = nxt().split()
    llx, lly, tw, th = float(org[0]), float(org[1]), float(org[2]), float(org[3])
    head_end = i
    numnet = nxt()
    n = int(numnet.split()[2])
    net_start = i
    boxes = []
    for _ in range(n):
        h = nxt().split()
        k = int(h[2])
        xs, ys = [], []
        for _ in range(k):
            p = nxt().split()
            xs.append(int((float(p[0]) - llx) // tw))
            ys.append(int((float(p[1]) - lly) // th))
        amt = int(h[3]) + mins[0]
        boxes.append((min(xs), min(ys), max(xs), max(ys), amt))
    net_end = i
    nadj = int(nxt())
    adj = {}
    for _ in range(nadj):
        t = nxt().split()
        x1, y1, l1, x2, y2, l2, c = map(int, t)
        key = (min(x1, x2), min(y1, y2), l1, 'h' if y1 == y2 else 'v')
        adj[key] = c

    track = [minw[l] + mins[l] for l in range(nl)]
    def tighten(c, l):
        t = track[l]
        return (int(c * f) // t) * t

    def cap_of(x, y, l, d):
        key = (x, y, l + 1, d)
        if key in adj:
            return adj[key]
        return hcap[l] if d == 'h' else vcap[l]

    changed = 0
    out_v, out_h = vcap, hcap
    if a.glob is not None:
        out_v = [tighten(c, l) for l, c in enumerate(vcap)]
        out_h = [tighten(c, l) for l, c in enumerate(hcap)]
        for key, c in list(adj.items()):
            adj[key] = tighten(c, key[2] - 1)
        desc = f'global x{f}: default capacities {hcap} / {vcap} -> {out_h} / {out_v}; {len(adj)} adjustments scaled'
    else:
        # RUDY ratio per tile (the larger of horizontal and vertical demand over default capacity), then the window
        # with the highest mean, by 2D prefix sums.
        dh = [[0.0] * (nx + 1) for _ in range(ny + 1)]
        dv = [[0.0] * (nx + 1) for _ in range(ny + 1)]
        for x0, y0, x1, y1, amt in boxes:
            w, h = x1 - x0, y1 - y0
            if w > 0:
                v = amt / (h + 1)
                dh[y0][x0] += v; dh[y0][x1 + 1] -= v; dh[y1 + 1][x0] -= v; dh[y1 + 1][x1 + 1] += v
            if h > 0:
                v = amt / (w + 1)
                dv[y0][x0] += v; dv[y0][x1 + 1] -= v; dv[y1 + 1][x0] -= v; dv[y1 + 1][x1 + 1] += v
        for arr in (dh, dv):
            for y in range(ny + 1):
                for x in range(nx + 1):
                    s = arr[y][x]
                    if x: s += arr[y][x - 1]
                    if y: s += arr[y - 1][x]
                    if x and y: s -= arr[y - 1][x - 1]
                    arr[y][x] = s
        ch, cv = max(1, sum(hcap)), max(1, sum(vcap))
        P = [[0.0] * (nx + 1) for _ in range(ny + 1)]
        for y in range(ny):
            for x in range(nx):
                r = max(dh[y][x] / ch, dv[y][x] / cv)
                P[y + 1][x + 1] = r + P[y][x + 1] + P[y + 1][x] - P[y][x]
        side = max(2, int(a.side * min(nx, ny)))
        best, bx, by = -1.0, 0, 0
        for y in range(0, ny - side + 1):
            for x in range(0, nx - side + 1):
                s = P[y + side][x + side] - P[y][x + side] - P[y + side][x] + P[y][x]
                if s > best:
                    best, bx, by = s, x, y
        for l in range(nl):
            for y in range(by, by + side):
                for x in range(bx, bx + side):
                    if hcap[l] > 0 and x + 1 < nx and x + 1 < bx + side:
                        adj[(x, y, l + 1, 'h')] = tighten(cap_of(x, y, l, 'h'), l)
                        changed += 1
                    if vcap[l] > 0 and y + 1 < ny and y + 1 < by + side:
                        adj[(x, y, l + 1, 'v')] = tighten(cap_of(x, y, l, 'v'), l)
                        changed += 1
        desc = (f'hotspot x{f}: window x {bx}-{bx + side - 1}, y {by}-{by + side - 1} ({side} x {side} tiles, mean RUDY '
                f'ratio {best / side / side:.3f}); {changed} edge-layers tightened; {len(adj)} adjustments')
    with open(a.out, 'w') as o:
        o.write(f'grid\t{nx} {ny} {nl}\n')
        o.write('vertical capacity\t' + ' '.join(map(str, out_v)) + '\n')
        o.write('horizontal capacity\t' + ' '.join(map(str, out_h)) + '\n')
        o.write('minimum width\t' + ' '.join(map(str, minw)) + '\n')
        o.write('minimum spacing\t' + ' '.join(map(str, mins)) + '\n')
        o.write(vias + '\n')
        o.write(' '.join(org) + '\n\n')
        o.write(numnet + '\n')
        o.write('\n'.join(lines[net_start:net_end]) + '\n')
        o.write(f'{len(adj)}\n')
        for (x, y, l, d), c in sorted(adj.items(), key=lambda t: (t[0][2], t[0][3], t[0][1], t[0][0])):
            x2, y2 = (x + 1, y) if d == 'h' else (x, y + 1)
            o.write(f'{x} {y} {l}\t{x2} {y2} {l}\t{c}\n')
    print(f'{a.out}: {desc}')


if __name__ == '__main__':
    main()
