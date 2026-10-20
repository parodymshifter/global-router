#!/usr/bin/env python3
"""region_dump_analysis.py -- read pd39route's region dump (PD39ROUTE_REGION_DUMP, HISTORY_COST_SWEEP.md Part 140)
for one design and ask two questions. Standard library only.

  python3 gr/region_dump_analysis.py PREFIX [STDERR]

PREFIX.<tag>.txt are the dump files (A0..A3 = the portfolio's variants, A = path A without a portfolio, B = the
reclaim path); STDERR, the run's log, gives each variant's final 2D overflow ("late-stage portfolio: variant").

1. Edge behaviour over rounds. For every 2D edge ever over capacity, its series over the rounds is the
   indicator "over capacity at the round's end". In each 50-round window an active edge (over at least once)
   is classed by its own discrete Fourier transform (mean removed): stuck (over >= 90% of the window),
   oscillating (>= half its power at periods of 2-4 rounds), or transient (the rest); excess-weighted shares
   per window. Then: do window 0's shares predict each variant's final overflow, and does a window's
   oscillating share predict the next window's gain (Spearman over every window of every variant)?
2. Halo against margin. Each reroute's halo (edge steps from its worst over-capacity edge to the nearest
   same-direction edge with room) against its search margin: how often halo > margin, whether that predicts a
   box-limited search, and the share of search work spent in boxes far wider than the halo.
"""
import cmath
import math
import re
import sys
from collections import defaultdict

WIN = 50


def spearman(x, y):
    def ranks(a):
        idx = sorted(range(len(a)), key=lambda i: a[i])
        r = [0.0] * len(a)
        i = 0
        while i < len(idx):
            j = i
            while j + 1 < len(idx) and a[idx[j + 1]] == a[idx[i]]:
                j += 1
            for k in range(i, j + 1):
                r[idx[k]] = (i + j) / 2 + 1
            i = j + 1
        return r
    if len(x) < 3:
        return None
    rx, ry = ranks(x), ranks(y)
    n = len(x)
    mx, my = sum(rx) / n, sum(ry) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    sxx = sum((a - mx) ** 2 for a in rx)
    syy = sum((b - my) ** 2 for b in ry)
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else None


def classify(series):
    """series: 0/1 per round of the window. Returns 'stuck', 'oscillating' or 'transient'."""
    n = len(series)
    duty = sum(series) / n
    if duty >= 0.9:
        return 'stuck'
    m = duty
    x = [v - m for v in series]
    tot = short = 0.0
    for k in range(1, n // 2 + 1):  # DFT power at k cycles per window; period n / k rounds
        c = sum(x[t] * cmath.exp(-2j * math.pi * k * t / n) for t in range(n))
        p = abs(c) ** 2
        tot += p
        if n / k <= 4:
            short += p
    return 'oscillating' if tot > 0 and short / tot >= 0.5 else 'transient'


def read_dump(path):
    over = defaultdict(dict)  # edge -> {round: excess}
    totals = {}
    reroutes = []  # (round, margin, limited, pops, halo)
    head = ''
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == 'N':
                reroutes.append((int(t[1]), int(t[3]), int(t[4]), int(t[5]), int(t[6])))
            elif t[0] == 'O':
                over[t[1] + t[2]][cur] = int(t[3])
            elif t[0] == 'R':
                cur = int(t[1])
                totals[cur] = int(t[2])
            elif t[0] == 'G':
                head = line.strip()
    return head, totals, over, reroutes


def main():
    prefix = sys.argv[1]
    finals = {}
    if len(sys.argv) > 2:
        for line in open(sys.argv[2], errors='replace'):
            m = re.match(r'late-stage portfolio: variant (\d+) \((.*?)\): region (\d+) -> (\d+) in (\d+) rounds, '
                         r'then 2D overflow (\d+)', line)
            if m:
                finals['A' + m.group(1)] = (m.group(2), int(m.group(4)), int(m.group(6)))
    tags = [t for t in ['A0', 'A1', 'A2', 'A3', 'A4', 'A5', 'A'] if _exists(f'{prefix}.{t}.txt')]
    perVariant = {}
    windowRows = []  # (tag, window, oscShare, stuckShare, gainNext)
    print(f'region dump analysis: {prefix}')
    for tag in tags:
        head, totals, over, reroutes = read_dump(f'{prefix}.{tag}.txt')
        rounds = max(totals) + 1 if totals else 0
        spec, region_after, final = finals.get(tag, ('?', None, None))
        print(f'\n== {tag} ({spec}): {rounds} rounds; {head}')
        print(f'   region best {region_after}, then 2D overflow after refinement {final}')
        best = []
        b = math.inf
        for r in range(rounds):
            b = min(b, totals.get(r, b))
            best.append(b)
        shares = []
        for w0 in range(0, rounds - WIN + 1, WIN):
            cls = defaultdict(float)
            nedge = defaultdict(int)
            for e, rec in over.items():
                ser = [1 if (w0 + t) in rec else 0 for t in range(WIN)]
                if not any(ser):
                    continue
                weight = sum(rec.get(w0 + t, 0) for t in range(WIN))
                c = classify(ser)
                cls[c] += weight
                nedge[c] += 1
            tot = sum(cls.values()) or 1
            s = {k: cls[k] / tot for k in ('stuck', 'oscillating', 'transient')}
            gain = None
            if w0 + 2 * WIN <= rounds:
                gain = best[w0 + WIN - 1] - best[w0 + 2 * WIN - 1]
            shares.append((w0, s, dict(nedge), gain))
            windowRows.append((tag, w0, s['oscillating'], s['stuck'], gain))
        for w0, s, ne, gain in shares[:3] + shares[-2:]:
            print(f'   rounds {w0:3d}-{w0 + WIN - 1:3d}: excess-weighted stuck {s["stuck"]:.2f} oscillating '
                  f'{s["oscillating"]:.2f} transient {s["transient"]:.2f}; edges {ne.get("stuck", 0)}/'
                  f'{ne.get("oscillating", 0)}/{ne.get("transient", 0)}; best-overflow gain over the next window '
                  f'{gain}')
        # halo vs margin
        rr = [x for x in reroutes if x[4] > 0]
        late = [x for x in rr if x[0] >= 50]
        if late:
            over_m = [x for x in late if x[4] > x[1]]
            lim = lambda xs: sum(x[2] for x in xs) / len(xs) if xs else float('nan')
            pops = sum(x[3] for x in late) or 1
            wide = sum(x[3] for x in late if x[1] >= 4 * x[4])
            halos = sorted(x[4] for x in late)
            q = lambda p: halos[min(len(halos) - 1, int(p * len(halos)))]
            print(f'   halo (reroutes with an over-capacity edge, rounds >= 50: {len(late)}): median {q(0.5)}, '
                  f'p90 {q(0.9)}, p99 {q(0.99)}, max {halos[-1]}; halo > margin in {len(over_m)} '
                  f'({100 * len(over_m) / len(late):.1f}%)')
            print(f'   box-limited share: halo > margin {lim(over_m):.2f}, halo <= margin '
                  f'{lim([x for x in late if x[4] <= x[1]]):.2f}; work in boxes >= 4x the halo: '
                  f'{100 * wide / pops:.1f}% of pops')
        perVariant[tag] = (final, shares[0][1] if shares else None)
    # 1b: window 0's shares against final overflow, over variants
    vs = [(t, f, s) for t, (f, s) in perVariant.items() if f is not None and s]
    if len(vs) >= 3:
        print('\n== window 0 (rounds 0-49) against each variant\'s final 2D overflow')
        for t, f, s in vs:
            print(f'   {t}: final {f}; oscillating {s["oscillating"]:.3f}, stuck {s["stuck"]:.3f}')
        r1 = spearman([s['oscillating'] for _, _, s in vs], [f for _, f, _ in vs])
        r2 = spearman([s['stuck'] for _, _, s in vs], [f for _, f, _ in vs])
        print(f'   Spearman (n {len(vs)}): oscillating vs final {r1}, stuck vs final {r2}')
    rows = [r for r in windowRows if r[4] is not None]
    if len(rows) >= 6:
        r = spearman([x[2] for x in rows], [x[4] for x in rows])
        r2 = spearman([x[3] for x in rows], [x[4] for x in rows])
        f2 = lambda v: 'undefined (constant)' if v is None else f'{v:.2f}'
        print(f'\n== every window of every variant (n {len(rows)}): oscillating share vs next window\'s gain '
              f'Spearman {f2(r)}; stuck share vs next gain {f2(r2)}')


def _exists(p):
    try:
        open(p).close()
        return True
    except OSError:
        return False


if __name__ == '__main__':
    main()
