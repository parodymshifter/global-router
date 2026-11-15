#!/usr/bin/env python3
"""hotspot_cg.py -- the ceiling of purpose-made candidate routes around an ISPD 2008 hotspot, by column generation
(HISTORY_COST_SWEEP.md Part 208).

  python3 gr/hotspot_cg.py DESIGN.gr BASE.route[.gz] WORK_PREFIX [--margin 30] [--box 10] [--rounds 15] [--time 600]
                           [--out ROUTE] [--no-dive] [--no-int] [--dive-price] [--dive-frac 0.25]
                           [--spare S[,g]] [--starts K] [--union] [--lns]
  python3 gr/hotspot_cg.py --rebalance DESIGN.gr IN.route OUT.route

Part 206 chose among each net's routes in six complete routings and found fractional room only (bigblue4 326 ->
252) and no whole-route mix below the base. This asks what routes made for the purpose could reach, before any
generator is built into the router. In 2D (the flow's layer assignment adds no overflow; every net here takes
width + spacing on every layer alike): the window is the bounding box of the base's over-capacity 2D edges grown by
--margin tiles. A net with wire inside the window keeps its wire outside and may re-wire inside: its pins inside
and the points where its outside wire meets the window must be joined again by wire inside the window. The master
program (HiGHS, interior point with crossover) picks a mix of each net's candidate inside-routes to minimise 2D
overflow; its edge prices (the duals) price a new candidate per net -- the net's parts re-joined by cheapest paths
under those prices, growing from one part to the nearest other (the router's own growth rule), inside the parts'
bounding box grown by --box tiles -- and the loop repeats until no net finds a cheaper route or --rounds is
reached. Every round's optimum is the best fractional mix of the candidates so far: an estimate from above of what
candidates of this kind can reach (the growth rule is a heuristic, so it is not a proof that nothing lower exists).
After the last round: a whole-route (integer) solution by single-net moves from the base and from the mix's
heaviest routes, as Part 206's --local. --out ROUTE writes the better of the two as a legal 3D route file (the
changed nets' inside wire put on layers with room, their vias rebuilt); score it with gr_verify and eval2008.pl.
Standard library only; needs `highs` on the PATH and gr/hotspot_mix.py beside it.
"""
import heapq
import os
import random
import re
import subprocess
import sys
import time
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hotspot_mix as hm  # noqa: E402

EPS = 1e-5  # wirelength tie-break per edge (overflow units)


def pins_of(gr_path, g, want):
    """{net: {pin tile: set of layers (0-based)}} for the nets in `want`."""
    out = {}
    f = open(gr_path)
    n = 0
    for line in f:
        if line.startswith('num net'):
            n = int(line.split()[-1])
            break
    for _ in range(n):
        a = f.readline().split()
        name, npins = a[0], int(a[2])
        if name in want:
            tiles = defaultdict(set)
            for _ in range(npins):
                p = f.readline().split()
                tiles[((int(float(p[0])) - g['llx']) // g['tw'], (int(float(p[1])) - g['lly']) // g['th'])].add(int(p[2]) - 1)
            out[name] = dict(tiles)
        else:
            for _ in range(npins):
                f.readline()
    return out


def ends(e):
    d, x, y = e
    return ((x, y), (x + 1, y)) if d == 0 else ((x, y), (x, y + 1))


def net_block(g, header, wire, pintiles):
    """A net's solution block from its 3D wire edges: the wire, then at every tile one via stack over the layers its
    wire and pins touch there."""
    X = lambda c: g['llx'] + c * g['tw']
    Y = lambda r: g['lly'] + r * g['th']
    lines = [header]
    at = defaultdict(set)
    for (x, y), ls in pintiles.items():
        at[(x, y)].update(ls)
    for d, x, y, l in sorted(set(wire)):
        q = (x + 1, y) if d == 0 else (x, y + 1)
        at[(x, y)].add(l)
        at[q].add(l)
        lines.append(f'({X(x)},{Y(y)},{l + 1})-({X(q[0])},{Y(q[1])},{l + 1})')
    for (x, y), ls in sorted(at.items()):
        for l in range(min(ls), max(ls)):
            lines.append(f'({X(x)},{Y(y)},{l + 1})-({X(x)},{Y(y)},{l + 2})')
    return '\n'.join(lines) + '\n!\n'


def rebalance(g, gr_path, src, dst):
    """Layer rebalancing of a route file: where a layer of a 2D edge is over capacity and another layer of the same
    edge has room for a net, one net's wire on that edge moves to the layer with room (its vias rebuilt as in
    net_block). Vias have no capacity in ISPD 2008, so every move takes that net's units off the overflow. Needed
    after --out: re-wiring opens room on upper layers of edges whose bottom layer the base left over capacity
    (bigblue4: 12 edges, 24 units)."""
    t0 = time.time()
    nl, sp = g['nl'], g['spacing']
    use3 = defaultdict(int)
    for name, _, segs in hm.nets_of(src):
        w = g['width'].get(name, 1)
        for e in hm.edges_of(g, segs):
            use3[e] += w + sp[e[3]]
    before = sum(u - hm.cap_of(g, e) for e, u in use3.items() if u > hm.cap_of(g, e))
    room = lambda e: hm.cap_of(g, e) - use3.get(e, 0)
    want = {}
    for e, u in use3.items():
        if u > hm.cap_of(g, e) and any(room(e[:3] + (l,)) > 0 for l in range(nl) if l != e[3]):
            want[e] = []
    if want:
        for name, _, segs in hm.nets_of(src):
            for e in set(hm.edges_of(g, segs)):
                if e in want:
                    want[e].append(name)
    moves = defaultdict(list)
    for e in sorted(want):
        for name in want[e]:
            if room(e) >= 0:
                break
            w = g['width'].get(name, 1)
            to = max((e[:3] + (l,) for l in range(nl) if l != e[3]), key=room)
            if room(to) < w + sp[to[3]]:
                break
            use3[e] -= w + sp[e[3]]
            use3[to] += w + sp[to[3]]
            moves[name].append((e, to))
    after = sum(u - hm.cap_of(g, e) for e, u in use3.items() if u > hm.cap_of(g, e))
    pins = pins_of(gr_path, g, moves) if moves else {}
    with open(dst, 'w') as f:
        for name, block, segs in hm.nets_of(src):
            if name not in moves:
                f.write(block)
                continue
            wire = set(hm.edges_of(g, segs))
            for e, to in moves[name]:
                wire.discard(e)
                wire.add(to)
            f.write(net_block(g, block.split('\n', 1)[0], wire, pins.get(name, {})))
    print(f'layer rebalancing: {sum(len(v) for v in moves.values())} wire edges of {len(moves)} nets moved to a layer with '
          f'room; predicted 3D overflow {before} -> {after}; wrote {dst} ({time.time() - t0:.0f} s)', flush=True)


def main():
    if '--rebalance' in sys.argv:  # hotspot_cg.py --rebalance DESIGN.gr IN.route OUT.route
        i = sys.argv.index('--rebalance')
        rebalance(hm.read_gr(sys.argv[i + 1]), sys.argv[i + 1], sys.argv[i + 2], sys.argv[i + 3])
        return
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if '--dive-frac' in sys.argv:
        args.remove(sys.argv[sys.argv.index('--dive-frac') + 1])
    spare, spare_w = 0, 0.05
    if '--spare' in sys.argv:  # --spare S[,g]: the last S units of every edge's capacity cost g per unit in the mix
        v = sys.argv[sys.argv.index('--spare') + 1]
        args.remove(v)
        spare = int(v.split(',')[0])
        spare_w = float(v.split(',')[1]) if ',' in v else 0.05
    opt = {'--margin': 30, '--box': 10, '--rounds': 15, '--time': 600, '--starts': 1}
    for k in list(opt):
        if k in sys.argv:
            opt[k] = int(sys.argv[sys.argv.index(k) + 1])
            args.remove(sys.argv[sys.argv.index(k) + 1])
    gr, base, prefix = args[0], args[1], args[2]
    starts = opt['--starts']
    t0 = time.time()
    g = hm.read_gr(gr)
    sp, nl = g['spacing'], g['nl']
    if len(set(sp)) != 1:
        print('layers differ in minimum spacing: the 2D projection is not exact for this design')
        return
    capc = {}

    def cap2(e):
        c = capc.get(e)
        if c is None:
            c = capc[e] = sum(hm.cap_of(g, (e[0], e[1], e[2], l)) for l in range(nl))
        return c

    # pass 1: 2D and 3D usage of the base
    use2, use3 = defaultdict(int), defaultdict(int)
    for name, _, segs in hm.nets_of(base):
        w = g['width'].get(name, 1)
        for e in hm.edges_of(g, segs):
            use3[e] += w + sp[e[3]]
            use2[e[:3]] += w + sp[e[3]]
    of3 = sum(u - hm.cap_of(g, e) for e, u in use3.items() if u > hm.cap_of(g, e))
    over = {e: u - cap2(e) for e, u in use2.items() if u > cap2(e)}
    of2 = sum(over.values())
    if not over:
        print('the base has no 2D overflow')
        return
    m = opt['--margin']
    x0 = max(0, min(e[1] for e in over) - m)
    x1 = min(g['nx'] - 1, max(e[1] + (e[0] == 0) for e in over) + m)
    y0 = max(0, min(e[2] for e in over) - m)
    y1 = min(g['ny'] - 1, max(e[2] + (e[0] == 1) for e in over) + m)

    # --union: the window is the tiles within the margin of an over-capacity edge (several hotspots: one box around
    # newblue7's holds most of the design), not their bounding box
    win = None
    if '--union' in sys.argv:
        win = set()
        for d, x, y in over:
            for dx in range(-m, m + 2 if d == 0 else m + 1):
                for dy in range(-m, m + 2 if d == 1 else m + 1):
                    if 0 <= x + dx < g['nx'] and 0 <= y + dy < g['ny']:
                        win.add((x + dx, y + dy))

    def in_win(n):
        return x0 <= n[0] <= x1 and y0 <= n[1] <= y1 and (win is None or n in win)

    def inside(e):
        d, x, y = e
        if d == 0:
            return in_win((x, y)) and in_win((x + 1, y))
        return in_win((x, y)) and in_win((x, y + 1))

    of_out = sum(v for e, v in over.items() if not inside(e))
    print(f'base {os.path.basename(base)}: overflow {of3} (3D), {of2} in 2D on {len(over)} 2D edges; window x {x0}-{x1} '
          f'y {y0}-{y1} ({len(win) if win is not None else (x1 - x0 + 1) * (y1 - y0 + 1)} tiles), 2D overflow outside it {of_out} ({time.time() - t0:.0f} s)',
          flush=True)

    # pass 2: the nets with wire inside the window
    route2 = {}
    base3 = {}  # a movable net's 3D wire edges in the base (for --out)
    for name, _, segs in hm.nets_of(base):
        d2 = None
        w = g['width'].get(name, 1)
        for e in hm.edges_of(g, segs):
            e2 = e[:3]
            if d2 is None:
                if not inside(e2):
                    continue
                d2 = defaultdict(int)
                for f_ in hm.edges_of(g, segs):
                    d2[f_[:3]] += w + sp[f_[3]]
                break
        if d2 is not None:
            route2[name] = dict(d2)
            base3[name] = hm.edges_of(g, segs)
    pins = pins_of(gr, g, route2)
    print(f'{len(route2)} nets with wire inside the window ({time.time() - t0:.0f} s)', flush=True)

    # per net: the parts to re-join (pins and outside wire), the base's inside route
    nets = []      # names of movable nets
    parts = {}     # name -> list of lists of nodes inside the window, one list per part
    cols = {}      # name -> [ (dict edge -> demand, edge count) ]
    unit = {}
    fix3 = {}      # 3D usage of the wire that stays, inside the window
    for e, u in use3.items():
        if inside(e[:3]):
            fix3[e] = u
    kept = 0
    for name, d2 in route2.items():
        ins = {e: a for e, a in d2.items() if inside(e)}
        par = {}

        def find(a):
            while par[a] != a:
                par[a] = par[par[a]]
                a = par[a]
            return a

        for p in pins.get(name, ()):
            par.setdefault(p, p)
        for e in d2:
            if e not in ins:
                a, b = ends(e)
                par.setdefault(a, a)
                par.setdefault(b, b)
                ra, rb = find(a), find(b)
                if ra != rb:
                    par[ra] = rb
        comp = defaultdict(list)
        for node in par:
            if in_win(node):
                comp[find(node)].append(node)
        if len(comp) < 2:  # nothing to join inside: its inside wire (if any) stays as it is
            kept += 1
            continue
        nets.append(name)
        parts[name] = list(comp.values())
        cols[name] = [(ins, len(ins))]
        w = g['width'].get(name, 1)
        unit[name] = w + sp[0]
        for e in base3[name]:
            if inside(e[:3]):
                fix3[e] -= w + sp[e[3]]
    del route2
    print(f'{len(nets)} movable nets ({kept} others keep their inside wire: one part only); '
          f'{sum(len(p) for p in parts.values())} part nodes ({time.time() - t0:.0f} s)', flush=True)

    # The exact 2D cost of an edge (Part 208: summing the layers' capacities was 12 units optimistic on bigblue4 --
    # a layer with an odd number of units left takes one more net for 1 unit of overflow, not 0). With every movable
    # net taking A units per layer: T = A x (nets that fit free, layer by layer), M = layers with a positive remainder
    # smaller than A (each takes one more net at less than full cost; counted as half cost per unit, exact for A = 2
    # and odd remainders), C = overflow of the wire that stays. Cost of u units of movable demand:
    # C + max(0, ceil((u - T) / 2), u - T - M).
    A = sp[0] + 1
    info = {}

    def edge_info(e):
        v = info.get(e)
        if v is None:
            T = M = C = 0
            for l in range(nl):
                c = hm.cap_of(g, e + (l,))
                u = fix3.get(e + (l,), 0)
                if c <= 0 and u <= 0:
                    continue
                r = c - u
                if r < 0:
                    C -= r
                else:
                    T += A * (r // A)
                    M += 1 if r % A else 0
            v = info[e] = (T, M, C)
        return v

    def cost_e(e, u):
        T, M, _ = edge_info(e)
        d = u - T
        return 0 if d <= 0 else max((d + 1) // 2, d - M)

    W, H = x1 - x0 + 1, y1 - y0 + 1

    def price_arrays(pi):
        """Per window edge, the price of one unit of demand: the master's dual where the edge is in it; elsewhere 1
        on an edge with no room left, 0.5 where only odd remainders are left, else 0."""
        ph = [[0.0] * H for _ in range(W)]
        pv = [[0.0] * H for _ in range(W)]
        for x in range(W):
            for y in range(H):
                for d, arr in ((0, ph), (1, pv)):
                    e = (d, x + x0, y + y0)
                    if not inside(e):
                        continue
                    v = pi.get(e)
                    if v is None:
                        T, M, _ = edge_info(e)
                        v = 0.0 if T > 0 else 0.5 if M > 0 else 1.0
                    arr[x][y] = v
        return ph, pv

    def col_cost(c, ph, pv):
        s = 0.0
        for e, dem in c[0].items():
            s += (ph if e[0] == 0 else pv)[e[1] - x0][e[2] - y0] * dem
        return s

    def regrow(name, ph, pv, first=0):
        """The net's parts joined again by cheapest paths under the prices: grow from the first part to the nearest
        other, add it and the path, repeat. Returns the set of inside edges, or None if a part cannot be reached."""
        ps = parts[name]
        a = unit[name]
        b = opt['--box']
        bx0 = max(x0, min(n[0] for p in ps for n in p) - b)
        bx1 = min(x1, max(n[0] for p in ps for n in p) + b)
        by0 = max(y0, min(n[1] for p in ps for n in p) - b)
        by1 = min(y1, max(n[1] for p in ps for n in p) + b)
        owner = {}
        for i, p in enumerate(ps):
            for n in p:
                owner[n] = i
        tree = set(ps[first])
        joined = {first}
        edges = set()
        while len(joined) < len(ps):
            dist = {n: 0.0 for n in tree}
            prev = {}
            heap = [(0.0, n) for n in tree]
            heapq.heapify(heap)
            hit = None
            while heap:
                d, n = heapq.heappop(heap)
                if d > dist.get(n, 1e30):
                    continue
                o = owner.get(n)
                if o is not None and o not in joined:
                    hit = n
                    break
                x, y = n
                if x < bx1 and (win is None or (x + 1, y) in win):
                    nd = d + EPS + a * ph[x - x0][y - y0]
                    q = (x + 1, y)
                    if nd < dist.get(q, 1e30):
                        dist[q] = nd
                        prev[q] = (n, (0, x, y))
                        heapq.heappush(heap, (nd, q))
                if x > bx0 and (win is None or (x - 1, y) in win):
                    nd = d + EPS + a * ph[x - 1 - x0][y - y0]
                    q = (x - 1, y)
                    if nd < dist.get(q, 1e30):
                        dist[q] = nd
                        prev[q] = (n, (0, x - 1, y))
                        heapq.heappush(heap, (nd, q))
                if y < by1 and (win is None or (x, y + 1) in win):
                    nd = d + EPS + a * pv[x - x0][y - y0]
                    q = (x, y + 1)
                    if nd < dist.get(q, 1e30):
                        dist[q] = nd
                        prev[q] = (n, (1, x, y))
                        heapq.heappush(heap, (nd, q))
                if y > by0 and (win is None or (x, y - 1) in win):
                    nd = d + EPS + a * pv[x - x0][y - 1 - y0]
                    q = (x, y - 1)
                    if nd < dist.get(q, 1e30):
                        dist[q] = nd
                        prev[q] = (n, (1, x, y - 1))
                        heapq.heappush(heap, (nd, q))
            if hit is None:
                return None
            n = hit
            while n not in tree:
                p_, e = prev[n]
                edges.add(e)
                tree.add(n)
                n = p_
            o = owner[hit]
            joined.add(o)
            tree.update(ps[o])
        return edges

    def all_edges():
        es = set()
        for n in nets:
            for c in cols[n]:
                es.update(c[0])
        return es

    def program(free, choice, integer, tag, limit=None):
        """Minimise the exact 2D overflow over the nets in `free` (name -> list of allowed column indices); every
        other net sits on choice.get(net, 0). The relaxation (integer False: interior point with crossover; returns
        the edge prices) or the integer program (HiGHS's branch and bound). Returns (overflow of the whole design in
        this model, prices, {net: {column: weight}}, status, rows, columns)."""
        load = defaultdict(int)   # demand of the nets that do not move
        worst = defaultdict(int)  # the most the free nets could add
        for n in nets:
            if n in free:
                mx = {}
                for k in free[n]:
                    for e, a in cols[n][k][0].items():
                        if a > mx.get(e, 0):
                            mx[e] = a
                for e, a in mx.items():
                    worst[e] += a
            else:
                for e, a in cols[n][choice.get(n, 0)][0].items():
                    load[e] += a
        hot = sorted(e for e, a in worst.items() if load[e] + a > edge_info(e)[0] - spare)
        eid = {e: i for i, e in enumerate(hot)}
        const = float(of_out) + sum(edge_info(e)[2] for e in fix_edges)
        const += sum(cost_e(e, u) for e, u in load.items() if e not in eid)
        lp, sol = f'{prefix}.{tag}.lp', f'{prefix}.{tag}.sol'
        rows = defaultdict(list)
        names = {}
        with open(lp, 'w') as f:
            f.write('Minimize\n obj:')
            for i in eid.values():
                f.write(f' + o{i}')
                if spare:
                    f.write(f' + {spare_w} q{i}')
            for ni, n in enumerate(sorted(free)):
                for k in free[n]:
                    c = cols[n][k]
                    v = names[(n, k)] = f'x{ni}_{k}'
                    for e, a in c[0].items():
                        if e in eid:
                            rows[eid[e]].append((a, v))
                    f.write(f' + {EPS * c[1]:.6f} {v}')
                f.write('\n')
            f.write('Subject To\n')
            for e, i in eid.items():
                T, M, _ = edge_info(e)
                f.write(f' c{i}: o{i}' + ''.join(f' - {a} {v}' for a, v in rows[i]) + f' >= {load[e] - T - M}\n')
                if M > 0:
                    f.write(f' h{i}: o{i}' + ''.join(f' - {0.5 * a} {v}' for a, v in rows[i]) + f' >= {0.5 * (load[e] - T)}\n')
                if spare:  # q: the units used inside the margin (the last `spare` units below capacity, and beyond)
                    f.write(f' m{i}: q{i}' + ''.join(f' - {a} {v}' for a, v in rows[i]) + f' >= {load[e] - T + spare}\n')
            for ni, n in enumerate(sorted(free)):
                f.write(f' s{ni}:' + ''.join(f' + x{ni}_{k}' for k in free[n]) + ' = 1\n')
            f.write('Bounds\n')
            for i in eid.values():
                f.write(f' o{i} >= 0\n')
                if spare:
                    f.write(f' q{i} >= 0\n')
            if integer:
                f.write('Binary\n')
                for v in names.values():
                    f.write(f' {v}\n')
            f.write('End\n')
        if os.path.exists(sol):
            os.remove(sol)
        cmd = ['highs', '--time_limit', str(limit or opt['--time']), '--solution_file', sol]
        r = subprocess.run(cmd + ([] if integer else ['--solver', 'ipm']) + [lp], capture_output=True, text=True)
        out_ = r.stdout.splitlines()
        status = next((' '.join(l.split()) for l in out_ if 'Model status' in l), None) or \
            ' | '.join(' '.join(l.split()) for l in out_ if re.search(r'^\s*(Status|Primal bound|Dual bound)', l)) or 'no status'
        pi, x = defaultdict(float), {}
        obj = None
        section = 0
        margin = 0.0
        if os.path.exists(sol):
            for line in open(sol):
                if line.startswith('# Dual solution'):
                    section = 2
                elif line.startswith('# Primal solution'):
                    section = 1
                elif line.startswith('# Basis'):
                    break
                a = line.split()
                if len(a) == 2 and a[0] == 'Objective' and section == 1:
                    obj = float(a[1])
                elif len(a) == 2 and a[0][0] in 'xchqm' and a[0][1:2].isdigit():
                    try:
                        v = float(a[1])
                    except ValueError:
                        continue
                    if section == 1 and a[0][0] == 'x':
                        x[a[0]] = v
                    elif section == 1 and a[0][0] == 'q':
                        margin += v
                    elif section == 2 and a[0][0] == 'm':
                        pi[hot[int(a[0][1:])]] += min(1.0, abs(v))
                    elif section == 2 and a[0][0] == 'c':
                        pi[hot[int(a[0][1:])]] += min(1.0, abs(v))
                    elif section == 2 and a[0][0] == 'h':
                        pi[hot[int(a[0][1:])]] += 0.5 * min(1.0, abs(v))
        os.remove(lp)
        weights = defaultdict(dict)
        tie = 0.0  # the wirelength tie-break's share of the objective: not overflow
        for (n, k), v in names.items():
            weights[n][k] = x.get(v, 0.0)
            tie += EPS * cols[n][k][1] * weights[n][k]
        program.margin = margin  # units of the mix inside the margin (reported, not part of the overflow)
        return (None if obj is None or not x else obj + const - tie - spare_w * margin), dict(pi), weights, status, len(hot), len(names)

    def price_round(pi, which, free):
        """One new candidate per net of `which` under the prices, where it beats the net's best (among the columns
        `free` allows it, if given). Returns (nets priced, routes added); a new column's index joins free[net]."""
        ph, pv = price_arrays(pi)
        added = tried = 0
        for n in which:
            a = unit[n]
            cs = cols[n]
            ks = free[n] if free is not None else range(len(cs))
            pay = [col_cost(cs[k], ph, pv) for k in ks]
            if min(pay) <= 1e-9:
                continue  # it already has a route that pays nothing
            sigma = min(v + EPS * cs[k][1] for v, k in zip(pay, ks))
            tried += 1
            es, cost = None, 1e30
            for first in range(min(starts, len(parts[n])) if len(parts[n]) > 2 else 1):
                # --starts K: the growth begun from each of the first K parts, the cheapest tree kept (the tree a
                # growth rule finds depends on where it starts; two parts have one answer)
                t_ = regrow(n, ph, pv, first)
                if t_ is None:
                    continue
                c_ = sum(EPS + a * (ph if e[0] == 0 else pv)[e[1] - x0][e[2] - y0] for e in t_)
                if c_ < cost - 1e-12:
                    es, cost = t_, c_
            if es is None:
                continue
            if cost < sigma - 1e-7:
                d = {e: a for e in es}
                same = next((k for k, c in enumerate(cs) if c[0] == d), None)
                if same is None:
                    cs.append((d, len(d)))
                    same = len(cs) - 1
                elif free is None or same in free[n]:
                    continue
                if free is not None:
                    free[n].append(same)
                added += 1
        return tried, added

    fix_edges = sorted({e[:3] for e in fix3})
    # round 0: no program yet -- price 1 on every inside edge the base overflows
    pi = {}
    load0 = defaultdict(int)
    for n in nets:
        for e, a in cols[n][0][0].items():
            load0[e] += a
    for e, u in load0.items():
        if cost_e(e, u) > 0:
            pi[e] = 1.0
    model0 = of_out + sum(edge_info(e)[2] for e in fix_edges) + sum(cost_e(e, u) for e, u in load0.items())
    print(f'the exact 2D model on the base: {model0} (3D {of3}; the layers\' capacities summed: {of2})', flush=True)
    weights = {}
    history = []
    for rnd in range(opt['--rounds'] + 1):
        t1 = time.time()
        tried, added = price_round(pi, nets, None)
        print(f'round {rnd}: {tried} nets priced, {added} new routes ({time.time() - t1:.0f} s)', flush=True)
        if added == 0:
            print('no net finds a cheaper route: the candidates are complete for this growth rule', flush=True)
            break
        if rnd == opt['--rounds']:
            break
        t1 = time.time()
        free = {n: list(range(len(cols[n]))) for n in nets if len(cols[n]) > 1}
        obj, pi_new, w_new, status, nhot, ncols = program(free, {}, False, f'r{rnd + 1}')
        if obj is None:  # keep the last solved mix (a failed master's empty weights sent every later step to the base)
            print(f'  master {rnd + 1}: {status}; stopping with the mix of round {rnd}', flush=True)
            break
        pi, weights = pi_new, w_new
        split = sum(1 for w_ in weights.values() if max(w_.values()) < 0.999)
        history.append(obj)
        print(f'  master {rnd + 1}: {status}; overflow of the best mix {obj:.2f} (base {model0}); {len(weights)} nets with '
              f'candidates, {ncols} routes, {nhot} edges that can overflow, {split} nets split'
              + (f', {program.margin:.1f} units inside the {spare}-unit margin' if spare else '') + f' ({time.time() - t1:.0f} s, '
              f'total {time.time() - t0:.0f} s)', flush=True)
    print(f'best mix by round: {", ".join(f"{h:.1f}" for h in history)} ({time.time() - t0:.0f} s)', flush=True)
    if weights:  # what the last mix does to the base
        moved = sum(1 for w_ in weights.values() if max(w_, key=w_.get) != 0)
        wl0 = sum(cols[n][0][1] for n in weights)
        wl1 = sum(v * cols[n][k][1] for n, w_ in weights.items() for k, v in w_.items())
        print(f'the last mix against the base: {moved} of {len(nets)} movable nets mostly on another route; their wire '
              f'inside the window {wl0} -> {wl1:.0f} edges ({100 * (wl1 / max(1, wl0) - 1):+.2f}%)', flush=True)

    # whole routes
    rng = random.Random(1)
    free_nets = sorted(n for n in nets if len(cols[n]) > 1)
    edges = all_edges()
    static = of_out + sum(edge_info(e)[2] for e in fix_edges)

    def search(choice, label):
        """Single-net moves with plateau walks (Part 206's --local) on the exact 2D cost."""
        load = dict.fromkeys(edges, 0)
        for n in nets:
            for e, a in cols[n][choice.get(n, 0)][0].items():
                load[e] += a
        obj = start = static + sum(cost_e(e, u) for e, u in load.items())
        t1, sweeps, idle, moves = time.time(), 0, 0, 0
        while idle < 60 and time.time() - t1 < opt['--time']:
            rng.shuffle(free_nets)
            before = obj
            for n in free_nets:
                k0 = choice.get(n, 0)
                cur = cols[n][k0][0]
                best_d, best_k = 1, -1
                for k, c in enumerate(cols[n]):
                    if k == k0:
                        continue
                    new, d = c[0], 0
                    for e, a in cur.items():
                        b = new.get(e, 0)
                        if a != b:
                            d += cost_e(e, load[e] - a + b) - cost_e(e, load[e])
                    for e, b in new.items():
                        if e not in cur:
                            d += cost_e(e, load[e] + b) - cost_e(e, load[e])
                    if d < best_d:
                        best_d, best_k = d, k
                if best_k >= 0 and (best_d < 0 or (best_d == 0 and rng.random() < 0.3)):
                    for e, a in cur.items():
                        load[e] -= a
                    for e, b in cols[n][best_k][0].items():
                        load[e] += b
                    choice[n] = best_k
                    obj += best_d
                    moves += 1
            sweeps += 1
            idle = 0 if obj < before else idle + 1
        print(f'whole routes from {label}: overflow {start} -> {obj} in {sweeps} sweeps, {moves} moves '
              f'({time.time() - t1:.0f} s)', flush=True)
        return obj, choice

    best_of, best = search({}, 'the base')
    if weights:
        heavy = {n: max(w_, key=w_.get) for n, w_ in weights.items()}
        o, ch = search(dict(heavy), "the mix's heaviest routes")
        if o < best_of:
            best_of, best = o, ch
        # the split nets settled exactly: every net whole in the mix (weight >= 0.999 on one route) stays there, the
        # integer program runs over the rest with the routes that carry weight (Part 206's reduction failed with
        # 41-78% of the nets split; here it is a few percent), then single-net moves over every net
        t1 = time.time()
        free = {n: sorted(k for k, v in w_.items() if v > 1e-6) for n, w_ in weights.items() if max(w_.values()) < 0.999}
        if '--no-int' in sys.argv:
            free = {}
        obj, _, wi, status, nhot, ncols = program(free, heavy, True, 'int') if free else (None, 0, 0, 'skipped', 0, 0)
        if obj is None:
            print(f'integer program over the {len(free)} split nets: {status}; no solution', flush=True)
        else:
            exact = dict(heavy)
            for n, w_ in wi.items():
                exact[n] = max(w_, key=w_.get)
            print(f'integer program over the {len(free)} split nets ({ncols} routes, {nhot} edges): {status}; overflow '
                  f'{obj:.2f} ({time.time() - t1:.0f} s)', flush=True)
            o, ch = search(exact, 'the integer program\'s solution')
            if o < best_of:
                best_of, best = o, ch
    if weights and '--no-dive' not in sys.argv:
        # a dive through the relaxation: fix every net that is whole in the mix and the quarter of the split nets
        # with the largest weight on one route, solve the relaxation over the rest again, repeat until none is left
        # (the integer program above settles the split nets against one fixed context; the dive lets the rest of
        # the mix answer each batch that is fixed)
        t1 = time.time()
        fixed = {n: 0 for n in nets if n not in weights}
        w_now = weights
        free = {n: list(range(len(cols[n]))) for n in weights}
        solves = priced = 0
        dive_frac = float(sys.argv[sys.argv.index('--dive-frac') + 1]) if '--dive-frac' in sys.argv else 0.25
        last = None
        base_floor = history[-1] if history else 0.0  # the full mix's overflow: no need to price below it
        while free:
            top = sorted(((max(w_.values()), n) for n, w_ in w_now.items() if n in free), reverse=True)
            whole = [n for v, n in top if v >= 0.999]
            split = [n for v, n in top if v < 0.999]
            for n in whole + split[:max(1, int(len(split) * dive_frac + 0.999))]:
                fixed[n] = max(w_now[n], key=w_now[n].get)
                del free[n]
            if not free:
                break
            obj, pi_d, w_new, status, _, _ = program(free, fixed, False, 'dive')
            solves += 1
            for _ in range(2 if obj is not None and '--dive-price' in sys.argv else 0):
                # the nets still free get new routes priced against the fixed ones (a dive that only re-mixes the
                # routes made for the full mix ended near 400 on newblue1)
                if obj < base_floor + 0.5:
                    break
                _, more = price_round(pi_d, sorted(free), free)
                priced += more
                if not more:
                    break
                obj2, pi2, w2, status, _, _ = program(free, fixed, False, 'dive')
                solves += 1
                if obj2 is None:
                    break
                obj, pi_d, w_new = obj2, pi2, w2
            if obj is None:
                print(f'dive: {status} with {len(free)} nets left; the rest take their heaviest routes', flush=True)
                for n in free:
                    fixed[n] = max(w_now[n], key=w_now[n].get)
                break
            last = obj
            w_now = w_new
        print(f'dive: {solves} relaxations, {priced} routes priced on the way, the last at '
              f'{last if last is None else round(last, 2)} ({time.time() - t1:.0f} s)', flush=True)
        edges = all_edges()
        free_nets = sorted(n for n in nets if len(cols[n]) > 1)
        o, ch = search(fixed, 'the dive')
        if o < best_of:
            best_of, best = o, ch
    if '--lns' in sys.argv:
        # --lns: neighbourhoods re-solved exactly. Around each over-capacity edge of the best whole-route solution,
        # every net now using an edge within a few tiles is set free over all its candidates and the integer program
        # is solved with every other net where it is (seconds: a few hundred nets); the answer is kept when the
        # whole design's overflow falls. Sweeps repeat until one gains nothing. Single-net moves cannot trade two
        # nets against each other; fixing the mix's whole nets fixes too much (the integer program over the split
        # nets has a bound well above the mix); this moves a neighbourhood's nets together from a whole-route start.
        t1 = time.time()

        def evaluate(choice):
            load = defaultdict(int)
            for n in nets:
                for e, a in cols[n][choice.get(n, 0)][0].items():
                    load[e] += a
            return static + sum(cost_e(e, u) for e, u in load.items()), load

        solves = wins = 0
        cur, load = evaluate(best)
        start = cur
        for sweep in range(6):
            gained = False
            seeds = sorted((e for e, u in load.items() if cost_e(e, u) > 0), key=lambda e: (-cost_e(e, load[e]), e))
            for seed in seeds:
                if cost_e(seed, load[seed]) <= 0 or time.time() - t1 > 6 * opt['--time']:
                    continue
                users = defaultdict(list)
                for n in nets:
                    if len(cols[n]) > 1:
                        for e in cols[n][best.get(n, 0)][0]:
                            if abs(e[1] - seed[1]) <= 4 and abs(e[2] - seed[2]) <= 4:
                                users[max(abs(e[1] - seed[1]), abs(e[2] - seed[2]))].append(n)
                free = {}
                for r in range(5):  # nearest first, up to 250 nets
                    for n in users[r]:
                        if n not in free and len(free) < 250:
                            free[n] = list(range(len(cols[n])))
                if not free:
                    continue
                obj, _, wi, status, _, _ = program(free, best, True, 'lns', 20)
                solves += 1
                if obj is None or obj > cur - 0.5:
                    continue
                trial = dict(best)
                for n, w_ in wi.items():
                    trial[n] = max(w_, key=w_.get)
                o, l_ = evaluate(trial)
                if o < cur:
                    best, cur, load = trial, o, l_
                    wins += 1
                    gained = True
            if not gained:
                break
        print(f'neighbourhoods re-solved: overflow {start} -> {cur} in {solves} integer programs, {wins} kept '
              f'({time.time() - t1:.0f} s)', flush=True)
        if cur < best_of:
            best_of = cur
            o, ch = search(dict(best), 'the re-solved neighbourhoods')
            if o < best_of:
                best_of, best = o, ch
    if '--out' not in sys.argv:
        return
    # --out ROUTE: the best whole-route solution as a legal 3D route file. A changed net keeps its base wire outside
    # the window on its layers; its new inside wire goes, one straight run at a time, on the layer of that direction
    # with the most room along the whole run (per edge where no single layer has room); all its vias are dropped and
    # rebuilt -- at every tile of the net one stack over the layers its wire and pins touch there. Vias have no
    # capacity in ISPD 2008, so this adds no overflow; it is not a via-minimal assignment (the wirelength it scores is
    # an upper limit).
    out = sys.argv[sys.argv.index('--out') + 1]
    if best_of >= model0:
        print(f'--out: no whole-route solution below the base ({best_of} against {model0}); no file written', flush=True)
        return
    changed = sorted(n for n in nets if best.get(n, 0) != 0)
    for n in changed:  # take the changed nets' old inside wire out of the 3D usage
        w = unit[n] - sp[0]
        for e in base3[n]:
            if inside(e[:3]):
                use3[e] -= w + sp[e[3]]
    hlay = [l for l in range(nl) if g['hcap'][l] > 0]
    vlay = [l for l in range(nl) if g['vcap'][l] > 0]
    new3 = {}
    lost = 0
    for n in changed:
        a = unit[n]
        es = sorted(cols[n][best[n]][0])
        seen = set(es)
        wire = [e for e in base3[n] if not inside(e[:3])]
        runs = []
        for e in es:  # maximal straight runs
            d, x, y = e
            if ((d, x - 1, y) if d == 0 else (d, x, y - 1)) in seen:
                continue
            run = [e]
            while True:
                x, y = (x + 1, y) if d == 0 else (x, y + 1)
                if (d, x, y) not in seen:
                    break
                run.append((d, x, y))
            runs.append(run)
        for run in runs:
            lays = hlay if run[0][0] == 0 else vlay
            slack = {l: min(hm.cap_of(g, e + (l,)) - use3.get(e + (l,), 0) for e in run) for l in lays}
            l = max(lays, key=lambda l_: slack[l_])
            if slack[l] >= a:
                for e in run:
                    use3[e + (l,)] += a
                    wire.append(e + (l,))
            else:
                for e in run:
                    l = max(lays, key=lambda l_: hm.cap_of(g, e + (l_,)) - use3.get(e + (l_,), 0))
                    if hm.cap_of(g, e + (l,)) - use3.get(e + (l,), 0) < a:
                        lost += 1
                    use3[e + (l,)] += a
                    wire.append(e + (l,))
        new3[n] = wire
    of_new = sum(u - hm.cap_of(g, e) for e, u in use3.items() if u > hm.cap_of(g, e))
    print(f'--out: {len(changed)} nets changed; predicted 3D overflow {of_new} (the 2D model {best_of}; base {of3}); '
          f'{lost} edges placed on a layer without room for a whole net', flush=True)
    raw = out + '.raw'
    with open(raw, 'w') as f:
        for name, block, _ in hm.nets_of(base):
            if name not in new3:
                f.write(block)
            else:
                f.write(net_block(g, block.split('\n', 1)[0], new3[name], pins.get(name, {})))
    rebalance(g, gr, raw, out)
    os.remove(raw)
    print(f'wrote {out} ({time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
