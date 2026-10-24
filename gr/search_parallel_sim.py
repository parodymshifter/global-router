#!/usr/bin/env python3
"""search_parallel_sim.py -- the ceiling of parallel search inside one net, measured on real region-stage searches
(HISTORY_COST_SWEEP.md Part 153). Standard library only.

  python3 gr/search_parallel_sim.py 'PREFIX.*.bin' [--threads 8] [--sync-us 2] [--edge-ns 60] [--pop-ns 150]

Each file is one search dumped by pd39route (PD39ROUTE_SEARCH_DUMP): the box, its 2D edge costs as the search saw
them (negative = no edge in that direction), sources, sinks, the cost the search found, its pops (A* expansions)
and its heuristic scale. For each:

  1. Dijkstra on the dumped costs must reproduce the found cost -- the check that the dump is the real instance;
  2. delta-stepping (Meyer and Sanders) is simulated for several bucket widths Delta: nodes are settled bucket by
     bucket; inside a bucket, light edges (cost <= Delta) are relaxed in phases until the bucket stays empty, then
     heavy edges once. Every phase is one parallel step ended by a barrier. Recorded: phases, edges scanned, nodes
     settled -- stopping once the cheapest sink is settled, as the router does;
  3. a time model: sequential = A* pops x pop-ns (the router's measured cost per expansion); parallel = per phase,
     edges / threads x edge-ns (a relaxation with an atomic minimum) plus sync-us. The speedup is their ratio.

Exact in cost (delta-stepping is an exact shortest-path method); the route among equal-cost ones may differ.
"""
import argparse
import glob
import heapq
import math
import struct
import sys


def load(path):
    b = open(path, 'rb').read()
    x0, y0, w, h, ns, nk = struct.unpack_from('6i', b, 0)
    o = 24
    src = [struct.unpack_from('2i', b, o + 8 * i) for i in range(ns)]
    o += 8 * ns
    snk = [struct.unpack_from('2i', b, o + 8 * i) for i in range(nk)]
    o += 8 * nk
    nh, nv = (w - 1) * h, w * (h - 1)
    hc = struct.unpack_from(f'{nh}d', b, o)
    o += 8 * nh
    vc = struct.unpack_from(f'{nv}d', b, o)
    o += 8 * nv
    C, pops, hs = struct.unpack_from('3d', b, o)
    nodes_src = [(x - x0) + (y - y0) * w for x, y in src if x0 <= x < x0 + w and y0 <= y < y0 + h]
    nodes_snk = {(x - x0) + (y - y0) * w for x, y in snk if x0 <= x < x0 + w and y0 <= y < y0 + h}
    # adjacency as (neighbour, cost) lists
    adj = [[] for _ in range(w * h)]
    for y in range(h):
        for x in range(w - 1):
            c = hc[y * (w - 1) + x]
            if c >= 0:
                a, z = x + y * w, x + 1 + y * w
                adj[a].append((z, c))
                adj[z].append((a, c))
    for y in range(h - 1):
        for x in range(w):
            c = vc[y * w + x]
            if c >= 0:
                a, z = x + y * w, x + (y + 1) * w
                adj[a].append((z, c))
                adj[z].append((a, c))
    return dict(w=w, h=h, src=nodes_src, snk=nodes_snk, adj=adj, C=C, pops=int(pops), hs=hs,
                costs=[c for c in hc if c >= 0] + [c for c in vc if c >= 0])


def dijkstra(inst):
    dist = {}
    q = [(0.0, s) for s in inst['src']]
    heapq.heapify(q)
    best = {s: 0.0 for s in inst['src']}
    pops = 0
    while q:
        d, v = heapq.heappop(q)
        if v in dist:
            continue
        dist[v] = d
        pops += 1
        if v in inst['snk']:
            return d, pops
        for u, c in inst['adj'][v]:
            nd = d + c
            if u not in dist and nd < best.get(u, math.inf):
                best[u] = nd
                heapq.heappush(q, (nd, u))
    return math.inf, pops


def delta_stepping(inst, delta):
    adj = inst['adj']
    dist = {}
    buckets = {}

    def relax(u, d):
        old = dist.get(u, math.inf)
        if d < old:
            if old < math.inf:
                buckets.get(int(old // delta), set()).discard(u)
            dist[u] = d
            buckets.setdefault(int(d // delta), set()).add(u)

    for s in inst['src']:
        relax(s, 0.0)
    phases, work, settled = [], 0, 0
    best_sink = math.inf
    while buckets:
        i = min(k for k, v in buckets.items() if v) if any(buckets.values()) else None
        if i is None:
            break
        if best_sink < i * delta:  # the cheapest sink is settled: stop, as the router does
            break
        S = set()
        while buckets.get(i):
            R = buckets.pop(i)
            S |= R
            ph = 0
            for v in R:
                dv = dist[v]
                for u, c in adj[v]:
                    if c <= delta:
                        ph += 1
                        relax(u, dv + c)
            phases.append(ph)
            work += ph
        ph = 0
        for v in S:
            dv = dist[v]
            for u, c in adj[v]:
                if c > delta:
                    ph += 1
                    relax(u, dv + c)
        phases.append(ph)
        work += ph
        settled += len(S)
        for v in S:
            if v in inst['snk']:
                best_sink = min(best_sink, dist[v])
        buckets = {k: v for k, v in buckets.items() if v}
    return phases, work, settled, best_sink


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pattern')
    ap.add_argument('--threads', type=int, default=8)
    ap.add_argument('--sync-us', type=float, default=2.0)
    ap.add_argument('--edge-ns', type=float, default=60.0)
    ap.add_argument('--pop-ns', type=float, default=150.0)
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    files = sorted(glob.glob(a.pattern), key=lambda f: int(f.rsplit('.', 2)[-2]))
    if a.limit:
        files = files[:: max(1, len(files) // a.limit)][: a.limit]
    totals = {}
    seq_total = 0.0
    bad = 0
    print(f'{len(files)} instances; threads {a.threads}, sync {a.sync_us} us, edge {a.edge_ns} ns, pop {a.pop_ns} ns')
    for f in files:
        inst = load(f)
        d, dpops = dijkstra(inst)
        ok = abs(d - inst['C']) <= 1e-9 * max(1.0, inst['C'])
        bad += not ok
        cs = sorted(inst['costs'])
        med = cs[len(cs) // 2]
        seq = inst['pops'] * a.pop_ns * 1e-9
        seq_total += seq
        line = f'{f.rsplit("/", 1)[-1]}: box {inst["w"]}x{inst["h"]}, A* pops {inst["pops"]}, Dijkstra pops {dpops}, cost {"ok" if ok else "MISMATCH"}'
        for name, delta in [('med', med), ('4med', 4 * med), ('C/100', inst['C'] / 100), ('C/20', inst['C'] / 20)]:
            if not delta > 0:
                continue
            phases, work, settled, sink = delta_stepping(inst, delta)
            par = sum(math.ceil(p / a.threads) * a.edge_ns * 1e-9 + a.sync_us * 1e-6 for p in phases)
            t = totals.setdefault(name, [0.0, 0, 0])
            t[0] += par
            t[1] += len(phases)
            t[2] += work
            line += f'; {name}: {len(phases)} phases, {work} edges, x{seq / par:.2f}'
        print('  ' + line)
    print(f'\ncost check: {len(files) - bad} of {len(files)} reproduce the found cost')
    for name, (par, nph, work) in totals.items():
        print(f'Delta {name}: phases {nph}, edges {work}, modelled speedup over the sequential A* x{seq_total / par:.2f}')


if __name__ == '__main__':
    main()
