#!/usr/bin/env python3
"""search_bidir_sim.py -- two-ended search on real region-stage searches (HISTORY_COST_SWEEP.md Part 196).
Standard library only.

  python3 gr/search_bidir_sim.py 'PREFIX.*.bin' [--out table.tsv]

Reads pd39route's search dumps (PD39ROUTE_SEARCH_DUMP) with search_bound_sim.load. For each instance, exact
bidirectional Dijkstra: one search from the sources, one from the sinks, the side with fewer settled nodes expanded
next (so the work is about twice the cheaper side's, whichever that is); mu is the cheapest meeting seen when an
edge is relaxed onto a node the other side has reached; stop when the two queue tops sum to at least mu. Exact: mu
must equal the dumped found cost. Reported: settled nodes (both sides) against the router's own expansions, and the
one-ended counts for reference: forward = #{g* < C}, backward = #{h* < C}.
"""
import argparse
import glob
import heapq
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from search_bound_sim import load, dijkstra, INF  # noqa: E402


def bidir(w, h, hc, vc, S, T):
    n = w * h
    w1 = w - 1
    dist = ([INF] * n, [INF] * n)
    done = ([False] * n, [False] * n)
    pq = ([], [])
    for side, starts in ((0, S), (1, T)):
        for s in starts:
            dist[side][s] = 0.0
            pq[side].append((0.0, s))
        heapq.heapify(pq[side])
    mu = INF
    for s in S:
        if dist[1][s] == 0.0:
            mu = 0.0
    settled = [0, 0]
    while pq[0] and pq[1]:
        if pq[0][0][0] + pq[1][0][0] >= mu:
            break
        side = 0 if settled[0] <= settled[1] else 1
        d, u = heapq.heappop(pq[side])
        if done[side][u] or d > dist[side][u]:
            continue
        done[side][u] = True
        settled[side] += 1
        D, O = dist[side], dist[1 - side]
        x, y = u % w, u // w
        for v, c in ((u + 1, hc[y * w1 + x] if x + 1 < w else -1.0), (u - 1, hc[y * w1 + x - 1] if x > 0 else -1.0),
                     (u + w, vc[y * w + x] if y + 1 < h else -1.0), (u - w, vc[(y - 1) * w + x] if y > 0 else -1.0)):
            if c < 0:
                continue
            nd = d + c
            if nd < D[v]:
                D[v] = nd
                heapq.heappush(pq[side], (nd, v))
            if O[v] < INF and nd + O[v] < mu:
                mu = nd + O[v]
    return mu, settled


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pattern')
    ap.add_argument('--out')
    a = ap.parse_args()
    files = sorted(glob.glob(a.pattern), key=lambda p: (len(p), p))
    rows = []
    for p in files:
        w, h, hc, vc, S, T, C, pops, hs = load(p)
        if not S or not T:
            continue
        mu, st = bidir(w, h, hc, vc, S, T)
        g = dijkstra(w, h, hc, vc, S)
        hb = dijkstra(w, h, hc, vc, T)
        Cs = min(g[t] for t in T)
        tol = 1e-12 * max(1.0, Cs)
        rows.append({'file': os.path.basename(p), 'box': sum(1 for d in g if d < INF), 'pops': pops,
                     'ok': abs(mu - C) <= 1e-9 * max(1.0, abs(C)), 'f': st[0], 'b': st[1],
                     'fwd': sum(1 for d in g if d < Cs - tol), 'bwd': sum(1 for v in range(w * h) if g[v] < INF and hb[v] < Cs - tol),
                     'ns': len(S), 'nt': len(T)})
    n = len(rows)
    if a.out:
        with open(a.out, 'w') as f:
            f.write('file\tbox\trouter_pops\tok\tbidir_fwd\tbidir_bwd\tone_ended_fwd\tone_ended_bwd\tsources\tsinks\n')
            for r in rows:
                f.write(f"{r['file']}\t{r['box']}\t{r['pops']:.0f}\t{int(r['ok'])}\t{r['f']}\t{r['b']}\t{r['fwd']}\t{r['bwd']}\t{r['ns']}\t{r['nt']}\n")
    P = sum(r['pops'] for r in rows)
    B = sum(r['f'] + r['b'] for r in rows)
    print(f"{n} instances, {sum(r['ok'] for r in rows)} reproduce the found cost (bidirectional)")
    print(f"router expansions {P:.0f}; one-ended forward {sum(r['fwd'] for r in rows)}; one-ended backward {sum(r['bwd'] for r in rows)} "
          f"({100 * sum(r['bwd'] for r in rows) / P:.1f}% of the router's); the smaller of the two per instance "
          f"{sum(min(r['fwd'], r['bwd']) for r in rows)} ({100 * sum(min(r['fwd'], r['bwd']) for r in rows) / P:.1f}%)")
    print(f"bidirectional settled {B} ({100 * B / P:.2f}% of the router's): {P / B:.1f}x fewer; forward side {sum(r['f'] for r in rows)}, backward {sum(r['b'] for r in rows)}")
    ratios = sorted(r['pops'] / max(1, r['f'] + r['b']) for r in rows)
    print(f"per instance, router / bidirectional: min {ratios[0]:.1f}x, 10th percentile {ratios[n // 10]:.1f}x, median {med(ratios):.1f}x, "
          f"90th {ratios[(9 * n) // 10]:.1f}x, max {ratios[-1]:.0f}x; instances under 2x: {sum(1 for x in ratios if x < 2)}")
    top = sorted(rows, key=lambda r: -(r['f'] + r['b']))[:5]
    print('largest bidirectional instances: ' + '; '.join(f"{r['file']} {r['f'] + r['b']} of box {r['box']} ({r['ns']} sources, {r['nt']} sinks)" for r in top))
    print(f"share of bidirectional work in the 10 largest instances: {100 * sum(r['f'] + r['b'] for r in sorted(rows, key=lambda r: -(r['f'] + r['b']))[:10]) / B:.1f}%")


if __name__ == '__main__':
    main()
