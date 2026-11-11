#!/usr/bin/env python3
"""search_bound_sim.py -- what any search heuristic could save on real region-stage searches (HISTORY_COST_SWEEP.md
Part 196). Standard library only.

  python3 gr/search_bound_sim.py 'PREFIX.*.bin' [--out table.tsv]

Each file is one search dumped by pd39route (PD39ROUTE_SEARCH_DUMP, Part 153): the box, its 2D edge costs as the
search saw them, sources, sinks, the found cost C, the router's expansions and its heuristic scale.

Part 138 measured that late searches expand nearly their whole box; Part 150 built one wall-aware bound (cheapest
edge per column / row) that pruned 0.4-2.2% and argued that the flood is a near-tie no admissible heuristic removes.
This measures that claim instead of arguing it. For each instance, exact distances g* (from the sources) and h*
(to the nearest sink) inside the box; then, with f*(v) = g*(v) + h*(v):

  * A* with a consistent heuristic h expands every node with g* + h < C and none with g* + h > C (nodes at exactly
    C depend on tie-breaking). So for each h the expansions lie between lo = #{g* + h < C} and hi = #{g* + h <= C}:
      none      h = 0 (Dijkstra)
      manh      the router's: hScale x Manhattan distance to the nearest sink
      colrow    Part 150's: per column (row) between the node and a sink, the box's cheapest horizontal (vertical)
                edge in that column (row), summed; nearest sink by this bound
      ring      Part 150's unbuilt suggestion: Chebyshev rings around the sinks' bounding box; a route to any sink
                crosses from ring r to ring r - 1 for every r down to 1, on disjoint edges, so the sum of each
                ring boundary's cheapest edge is a lower bound (consistent: a step changes it by at most that
                boundary's minimum)
      perfect   h = h*: f* >= C everywhere, so lo is 0 and hi counts the nodes on a cheapest route -- the floor of
                every exact search.
      perim-b   a bounded backward search as the bound (perimeter search): settle the b nodes nearest the sinks
                (S = {h* < T}, T the (b+1)-th smallest h*); h = h* inside S, and outside it T + hScale x the
                Manhattan distance to S's bounding box grown by one tile (the route's last node outside S has
                h* >= T and lies in that box, and every edge costs at least hScale); the larger of that and manh.
                Its work is b backward expansions plus the forward range.
  * where the cost is: the share of box nodes with g* < t x C (cheap to reach from the sources) and with
    h* < t x C (cheap to finish from), for t from 0.5 to 1 - 1e-6. A cost concentrated at the sink end shows as
    nearly every node cheap to reach and almost none cheap to finish from.
  * the near-optimal profile: the share of box nodes with f* <= C x (1 + eps): nodes on routes at most eps dearer
    than the best.
  * check: C from g* must equal the dumped found cost, and the router's own expansions must lie in manh's range
    (the dump counts pops, which include stale queue entries, so a small excess over hi is expected).
"""
import argparse
import glob
import heapq
import struct
import sys

INF = float('inf')
EPS = (1e-9, 1e-6, 1e-4, 1e-3, 1e-2, 1e-1)
TH = (0.5, 0.9, 0.99, 0.999, 1 - 1e-6)
BUD = (256, 1024, 4096)


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
    inb = lambda p: x0 <= p[0] < x0 + w and y0 <= p[1] < y0 + h
    S = sorted({(x - x0) + (y - y0) * w for x, y in src if inb((x, y))})
    T = sorted({(x - x0) + (y - y0) * w for x, y in snk if inb((x, y))})
    return w, h, hc, vc, S, T, C, pops, hs


def stage_of(path):
    """Part 197: the instance's stage tag (1 region stage, 2 slow tier, 0 elsewhere), or -1 for a dump without one."""
    b = open(path, 'rb').read()
    x0, y0, w, h, ns, nk = struct.unpack_from('6i', b, 0)
    o = 24 + 8 * ns + 8 * nk + 8 * ((w - 1) * h + w * (h - 1))
    return int(struct.unpack_from('d', b, o + 24)[0]) if len(b) >= o + 32 else -1


def dijkstra(w, h, hc, vc, starts):
    n = w * h
    dist = [INF] * n
    pq = []
    for s in starts:
        dist[s] = 0.0
        pq.append((0.0, s))
    heapq.heapify(pq)
    w1 = w - 1
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist[u]:
            continue
        x, y = u % w, u // w
        if x + 1 < w:
            c = hc[y * w1 + x]
            if c >= 0 and d + c < dist[u + 1]:
                dist[u + 1] = d + c
                heapq.heappush(pq, (d + c, u + 1))
        if x > 0:
            c = hc[y * w1 + x - 1]
            if c >= 0 and d + c < dist[u - 1]:
                dist[u - 1] = d + c
                heapq.heappush(pq, (d + c, u - 1))
        if y + 1 < h:
            c = vc[y * w + x]
            if c >= 0 and d + c < dist[u + w]:
                dist[u + w] = d + c
                heapq.heappush(pq, (d + c, u + w))
        if y > 0:
            c = vc[(y - 1) * w + x]
            if c >= 0 and d + c < dist[u - w]:
                dist[u - w] = d + c
                heapq.heappush(pq, (d + c, u - w))
    return dist


def heuristics(w, h, hc, vc, T, hs):
    """manh, colrow, ring as lists over nodes."""
    n = w * h
    w1 = w - 1
    tx = [t % w for t in T]
    ty = [t // w for t in T]
    # colrow: column c's cheapest horizontal edge x=c..c+1 over the box's rows; row r's cheapest vertical edge
    colmin = [min((hc[y * w1 + x] for y in range(h) if hc[y * w1 + x] >= 0), default=0.0) for x in range(w1)]
    rowmin = [min((vc[y * w + x] for x in range(w) if vc[y * w + x] >= 0), default=0.0) for y in range(h - 1)]
    P = [0.0]
    for c in colmin:
        P.append(P[-1] + c)
    Q = [0.0]
    for c in rowmin:
        Q.append(Q[-1] + c)
    # per x: min over sinks of |P[x] - P[sx]| is not separable with y, so take the min over sinks of the sum
    pairs = sorted(set(zip(tx, ty)))
    if len(pairs) > 64:  # many sink tiles (a whole component): the bounding box form, as the router's boxHeuristic
        bx0, bx1, by0, by1 = min(tx), max(tx), min(ty), max(ty)
        pairs = None
    manh, colrow = [0.0] * n, [0.0] * n
    for y in range(h):
        for x in range(w):
            if pairs is None:
                dx = bx0 - x if x < bx0 else x - bx1 if x > bx1 else 0
                dy = by0 - y if y < by0 else y - by1 if y > by1 else 0
                m = dx + dy
                cr = (P[bx0] - P[x] if x < bx0 else P[x] - P[bx1] if x > bx1 else 0.0) + \
                     (Q[by0] - Q[y] if y < by0 else Q[y] - Q[by1] if y > by1 else 0.0)
            else:
                m, cr = 1 << 60, INF
                for sx, sy in pairs:
                    d = abs(x - sx) + abs(y - sy)
                    if d < m:
                        m = d
                    v = abs(P[x] - P[sx]) + abs(Q[y] - Q[sy])
                    if v < cr:
                        cr = v
            manh[y * w + x] = hs * m
            colrow[y * w + x] = cr
    # ring: Chebyshev distance to the sinks' bounding box
    bx0, bx1, by0, by1 = min(tx), max(tx), min(ty), max(ty)

    def ringof(x, y):
        dx = bx0 - x if x < bx0 else x - bx1 if x > bx1 else 0
        dy = by0 - y if y < by0 else y - by1 if y > by1 else 0
        return dx if dx > dy else dy

    R = [ringof(i % w, i // w) for i in range(n)]
    maxr = max(R)
    m = [INF] * (maxr + 1)  # m[r]: cheapest edge between ring r and ring r - 1
    for y in range(h):
        for x in range(w1):
            c = hc[y * w1 + x]
            if c >= 0:
                a, z = R[y * w + x], R[y * w + x + 1]
                if a != z and c < m[max(a, z)]:
                    m[max(a, z)] = c
    for y in range(h - 1):
        for x in range(w):
            c = vc[y * w + x]
            if c >= 0:
                a, z = R[y * w + x], R[(y + 1) * w + x]
                if a != z and c < m[max(a, z)]:
                    m[max(a, z)] = c
    pref = [0.0] * (maxr + 1)
    for r in range(1, maxr + 1):
        pref[r] = pref[r - 1] + (m[r] if m[r] < INF else 0.0)
    ring = [pref[r] for r in R]
    return manh, colrow, ring


def analyse(path):
    w, h, hc, vc, S, T, C, pops, hs = load(path)
    if not S or not T:
        return None
    g = dijkstra(w, h, hc, vc, S)
    hstar = dijkstra(w, h, hc, vc, T)
    Cs = min(g[t] for t in T)
    ok = abs(Cs - C) <= 1e-9 * max(1.0, abs(C))
    manh, colrow, ring = heuristics(w, h, hc, vc, T, hs)
    n = sum(1 for d in g if d < INF)
    tol = 1e-12 * max(1.0, Cs)
    out = {'file': path.rsplit('/', 1)[-1], 'w': w, 'h': h, 'reach': n, 'C': Cs, 'ok': ok, 'pops': pops, 'hs': hs,
           'manh_share': hs * min(abs(s % w - t % w) + abs(s // w - t // w) for s in S for t in T[:64]) / Cs if Cs > 0 else 0.0}
    zero = [0.0] * (w * h)
    hmin = min([c for c in hc if c >= 0] + [c for c in vc if c >= 0])
    out['below_hs'] = sum(1 for c in hc if 0 <= c < hs * (1 - 1e-12)) + sum(1 for c in vc if 0 <= c < hs * (1 - 1e-12))
    out['hmin'] = hmin
    cands = [('none', zero), ('manh', manh), ('colrow', colrow), ('ring', ring), ('perfect', hstar)]
    order = sorted(d for d in hstar if d < INF)
    for b in BUD:
        if b >= len(order):
            continue
        T_ = order[b]
        xs = [v % w for v in range(w * h) if hstar[v] < T_]
        ys = [v // w for v in range(w * h) if hstar[v] < T_]
        bx0, bx1, by0, by1 = max(0, min(xs) - 1), min(w - 1, max(xs) + 1), max(0, min(ys) - 1), min(h - 1, max(ys) + 1)
        hp = [0.0] * (w * h)
        for v in range(w * h):
            if hstar[v] < T_:
                hp[v] = hstar[v]
            else:
                x, y = v % w, v // w
                dx = bx0 - x if x < bx0 else x - bx1 if x > bx1 else 0
                dy = by0 - y if y < by0 else y - by1 if y > by1 else 0
                e = T_ + hs * (dx + dy)
                hp[v] = e if e > manh[v] else manh[v]
        cands.append((f'perim{b}', hp))
        out[f'perim{b}_T'] = T_ / Cs if Cs > 0 else 0.0
    for name, hh in cands:
        lo = hi = 0
        bad = 0
        for v in range(w * h):
            gv = g[v]
            if gv == INF:
                continue
            f = gv + hh[v]
            if f < Cs - tol:
                lo += 1
            if f <= Cs + tol:
                hi += 1
            if hh[v] > hstar[v] + tol:  # admissibility check
                bad += 1
        out[name] = (lo, hi)
        out[name + '_bad'] = bad
    out['prof'] = [sum(1 for v in range(w * h) if g[v] < INF and g[v] + hstar[v] <= Cs * (1 + e)) for e in EPS]
    out['gprof'] = [sum(1 for v in range(w * h) if g[v] < t * Cs) for t in TH]
    out['hprof'] = [sum(1 for v in range(w * h) if g[v] < INF and hstar[v] < t * Cs) for t in TH]
    return out


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pattern')
    ap.add_argument('--out')
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    files = sorted(glob.glob(a.pattern), key=lambda p: (len(p), p))
    if a.limit:
        files = files[:a.limit]
    names = ['none', 'manh', 'colrow', 'ring', 'perfect'] + [f'perim{b}' for b in BUD]
    rows = []
    for p in files:
        r = analyse(p)
        if r:
            rows.append(r)
    n = len(rows)
    if not n:
        print('no instances')
        return
    if a.out:
        with open(a.out, 'w') as f:
            f.write('file\tw\th\treach\tC\tok\trouter_pops\thscale\tmin_edge\t' +
                    '\t'.join(f'{k}_{s_}' for k in names for s_ in ('lo', 'hi')) + '\t' +
                    '\t'.join(f'nearopt_{e:g}' for e in EPS) + '\t' + '\t'.join(f'g_below_{t:g}' for t in TH) + '\t' +
                    '\t'.join(f'h_below_{t:g}' for t in TH) + '\n')
            for r in rows:
                f.write(f"{r['file']}\t{r['w']}\t{r['h']}\t{r['reach']}\t{r['C']:.9g}\t{int(r['ok'])}\t{r['pops']:.0f}\t{r['hs']:.6g}\t{r['hmin']:.6g}\t" +
                        '\t'.join(f"{r[k][0]}\t{r[k][1]}" if k in r else '\t' for k in names) + '\t' +
                        '\t'.join(str(x) for x in r['prof'] + r['gprof'] + r['hprof']) + '\n')
    tot = sum(r['reach'] for r in rows)
    print(f"{n} instances, {sum(r['ok'] for r in rows)} reproduce the found cost; box nodes {tot}, router pops "
          f"{sum(r['pops'] for r in rows):.0f} ({100 * sum(r['pops'] for r in rows) / tot:.1f}% of box nodes); "
          f"edges cheaper than hScale: {sum(r['below_hs'] for r in rows)}")
    print('inadmissible values (must be 0): ' + ', '.join(f"{k} {sum(r.get(k + '_bad', 0) for r in rows)}" for k in names if k not in ('none', 'perfect')))
    print('bound\tinstances\tmust expand (lo)\tat most (hi)\tbackward\twork at most, share of box\tmedian instance, hi + backward')
    for k in names:
        rs = [r for r in rows if k in r]
        if not rs:
            continue
        t_ = sum(r['reach'] for r in rs)
        back = int(k[5:]) if k.startswith('perim') else 0
        lo, hi = sum(r[k][0] for r in rs), sum(r[k][1] for r in rs)
        print(f"{k}\t{len(rs)}\t{lo} ({100 * lo / t_:.1f}%)\t{hi} ({100 * hi / t_:.1f}%)\t{back * len(rs)}\t"
              f"{100 * (hi + back * len(rs)) / t_:.1f}%\t{100 * med([(r[k][1] + back) / r['reach'] for r in rs]):.1f}%")
    for b in BUD:
        rs = [r for r in rows if f'perim{b}' in r]
        if rs:
            print(f"perim{b}: threshold T over C, median {med([r[f'perim{b}_T'] for r in rs]):.6f}, "
                  f"min {min(r[f'perim{b}_T'] for r in rs):.6f}")
    print('where the cost is: share of box nodes cheap to reach (g* < t C) / cheap to finish from (h* < t C)')
    for i_, t in enumerate(TH):
        print(f"  t {t:g}\t{100 * sum(r['gprof'][i_] for r in rows) / tot:.2f}%\t{100 * sum(r['hprof'][i_] for r in rows) / tot:.2f}%"
              f"\tmedian instance {100 * med([r['gprof'][i_] / r['reach'] for r in rows]):.2f}% / {100 * med([r['hprof'][i_] / r['reach'] for r in rows]):.2f}%")
    print('near-optimal profile: share of box nodes with f* <= C x (1 + eps)')
    for i_, e in enumerate(EPS):
        print(f"  eps {e:g}\t{100 * sum(r['prof'][i_] for r in rows) / tot:.2f}%\tmedian instance {100 * med([r['prof'][i_] / r['reach'] for r in rows]):.2f}%")
    print(f"median share of the found cost that hScale x Manhattan explains: {100 * med([r['manh_share'] for r in rows]):.4f}%")


if __name__ == '__main__':
    main()
