#!/usr/bin/env python3
"""cg_merge.py -- put gr/cgbound's exported trees into a route file, as a start for the router's late stages.

  python3 gr/cg_merge.py DESIGN.gr SEED.route[.gz] EXPORT OUT.route
  python3 gr/cg_merge.py --selftest

HISTORY_COST_SWEEP.md Part 213 (option 1: newblue1's recipe -- the late stages started from a rough rounding of a
fractional mix -- tried on newblue3). `cgbound --export` writes, per net with two or more nodes in its region, the
heaviest tree of its last master as chip edges; an edge to the region's merged outside node is the chip edge that
leaves the region. Per exported net the new route is: those edges, plus the seed's edges with both ends outside the
region (its outside wire stays). Because the outside is one free node in cgbound, a tree may leave the region at a
different place than the seed's outside wire enters it; the pieces are then joined by shortest grid paths (unit
cost, kept to the net's bounding box plus a margin), and wire that joins no pin is pruned (repeatedly removing
edges that end at a tile with no pin and no other wire). Every other net keeps its seed block unchanged.

The output is a 2D route for PD3ROUTE_LA_FROM (which projects every segment to 2D and drops vias): H edges are
written on the first layer with horizontal capacity, V edges on the first with vertical capacity, with no vias --
it is not a legal 3D file and is not meant to be scored as one. The 2D overflow of the result is printed.
Standard library only.
"""
import os
import sys
from collections import defaultdict, deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hotspot_mix as hm  # noqa: E402


def pin_tiles(gr_path, g, want):
    """name -> set of pin tiles, for the nets in `want`."""
    out = {}
    with open(gr_path) as f:
        for line in f:
            if line.startswith('num net'):
                break
        for line in f:
            t = line.split()
            if len(t) == 4 and not t[0][0].isdigit():
                name, n = t[0], int(t[2])
                tiles = set()
                for _ in range(n):
                    a = f.readline().split()
                    tiles.add(((int(a[0]) - g['llx']) // g['tw'], (int(a[1]) - g['lly']) // g['th']))
                if name in want:
                    out[name] = tiles
            elif t and len(t) == 1:
                break
    return out


def read_export(path):
    region, trees = None, {}
    with open(path) as f:
        for line in f:
            if line.startswith('#'):
                a = line.split()
                if 'region' in a:
                    i = a.index('region')
                    xs, ys = a[i + 2].rstrip(','), a[i + 4].rstrip(',')
                    region = tuple(int(v) for v in xs.split('-')) + tuple(int(v) for v in ys.split('-'))
                continue
            a = line.split()
            if a and a[0] == 'net':
                name, k = a[1], int(a[2])
                es = set()
                for _ in range(k):
                    x, y, d = f.readline().split()
                    es.add((0 if d == 'H' else 1, int(x), int(y)))
                trees[name] = es
    if region is None:
        sys.exit(f'{path}: no region line')
    x0, x1, y0, y1 = region
    return (x0, y0, x1, y1), trees


def ends(e):
    d, x, y = e
    return ((x, y), (x + 1, y)) if d == 0 else ((x, y), (x, y + 1))


def join_and_prune(edges, pins, nx, ny, margin=8):
    """Join the pieces of `edges` + `pins` by shortest grid paths, then prune wire that serves no pin."""
    edges = set(edges)
    par = {}

    def find(a):
        par.setdefault(a, a)
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            par[ra] = rb

    nodes = set(pins)
    for e in edges:
        a, b = ends(e)
        nodes.update((a, b))
        union(a, b)
    for p in pins:
        find(p)
    xs = [n[0] for n in nodes]
    ys = [n[1] for n in nodes]
    bx0, bx1 = max(0, min(xs) - margin), min(nx - 1, max(xs) + margin)
    by0, by1 = max(0, min(ys) - margin), min(ny - 1, max(ys) + margin)
    joins = 0
    while len({find(n) for n in nodes}) > 1:
        root = find(next(iter(pins)) if pins else next(iter(nodes)))
        src = [n for n in nodes if find(n) == root]
        prev = {n: None for n in src}
        q = deque(src)
        hit = None
        while q:
            v = q.popleft()
            if v in nodes and find(v) != root:
                hit = v
                break
            x, y = v
            for u in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if bx0 <= u[0] <= bx1 and by0 <= u[1] <= by1 and u not in prev:
                    prev[u] = v
                    q.append(u)
        if hit is None:
            raise RuntimeError('pieces cannot be joined inside the box')
        v = hit
        while prev[v] is not None:
            u = prev[v]
            e = (0, min(u[0], v[0]), u[1]) if u[1] == v[1] else (1, u[0], min(u[1], v[1]))
            edges.add(e)
            nodes.update((u, v))
            union(u, v)
            v = u
        joins += 1
    # prune: an edge ending at a tile with no pin and no other wire serves nothing
    deg = defaultdict(int)
    for e in edges:
        for n in ends(e):
            deg[n] += 1
    stack = [n for n, k in deg.items() if k == 1 and n not in pins]
    inc = defaultdict(set)
    for e in edges:
        for n in ends(e):
            inc[n].add(e)
    pruned = 0
    while stack:
        n = stack.pop()
        if deg[n] != 1 or n in pins:
            continue
        e = next(iter(inc[n]))
        edges.discard(e)
        pruned += 1
        for m in ends(e):
            inc[m].discard(e)
            deg[m] -= 1
            if deg[m] == 1 and m not in pins:
                stack.append(m)
    return edges, joins, pruned


def main():
    if '--selftest' in sys.argv:
        return selftest()
    gr, seed, export, out = sys.argv[1:5]
    g = hm.read_gr(gr)
    (x0, y0, x1, y1), trees = read_export(export)
    inn = lambda n: x0 <= n[0] <= x1 and y0 <= n[1] <= y1
    pins = pin_tiles(gr, g, set(trees))
    hl = next(l for l in range(g['nl']) if g['hcap'][l] > 0) + 1
    vl = next(l for l in range(g['nl']) if g['vcap'][l] > 0) + 1
    X = lambda c: g['llx'] + c * g['tw']
    Y = lambda r: g['lly'] + r * g['th']
    sp = g['spacing']
    use = defaultdict(int)
    stats = defaultdict(int)
    with open(out, 'w') as f:
        for name, block, segs in hm.nets_of(seed):
            w = g['width'].get(name, 1)
            if name not in trees:
                f.write(block)
                for e in set(hm.edges_of(g, segs)):
                    use[e[:3]] += w + sp[e[3]]
                continue
            outside = {e[:3] for e in hm.edges_of(g, segs) if not any(inn(n) for n in ends(e[:3]))}
            es, joins, pruned = join_and_prune(trees[name] | outside, pins.get(name, set()), g['nx'], g['ny'])
            stats['nets'] += 1
            stats['joined'] += joins > 0
            stats['joins'] += joins
            stats['pruned'] += pruned
            header = block.split('\n', 1)[0]
            f.write(header + '\n')
            for d, x, y in sorted(es):
                if d == 0:
                    f.write(f'({X(x)},{Y(y)},{hl})-({X(x + 1)},{Y(y)},{hl})\n')
                else:
                    f.write(f'({X(x)},{Y(y)},{vl})-({X(x)},{Y(y + 1)},{vl})\n')
                use[(d, x, y)] += w + sp[0]
            f.write('!\n')
    missing = set(trees) - set(pins)
    cap2 = lambda e: sum(hm.cap_of(g, e + (l,)) for l in range(g['nl']))
    of = sum(max(0, u - cap2(e)) for e, u in use.items())
    ofin = sum(max(0, u - cap2(e)) for e, u in use.items() if all(inn(n) for n in ends(e)))
    print(f'{stats["nets"]} nets from the export ({len(missing)} not in the design: {sorted(missing)[:3]}); '
          f'{stats["joined"]} needed joining ({stats["joins"]} paths), {stats["pruned"]} edges pruned; '
          f'2D overflow {of} ({ofin} on edges inside the region); wrote {out}', flush=True)


def selftest():
    fails = 0

    def check(c, what):
        nonlocal fails
        print(('  ok   ' if c else '  FAIL ') + what)
        fails += not c

    print('self test')
    # a tree that leaves the region at (5, 2) while the seed's outside wire enters at (5, 4): joined outside
    tree = {(0, 3, 2), (0, 4, 2)}            # (3,2)-(4,2)-(5,2): (5,2) is outside a region ending at x 4
    outside = {(1, 5, 4), (1, 5, 5), (0, 5, 6)}  # (5,4)-(5,5)-(5,6)-(6,6), a pin at (6,6)
    es, joins, pruned = join_and_prune(tree | outside, {(3, 2), (6, 6)}, 20, 20)
    nodes = defaultdict(int)
    for e in es:
        for n in ends(e):
            nodes[n] += 1
    leaves = {n for n, k in nodes.items() if k == 1}
    check(joins == 1 and leaves == {(3, 2), (6, 6)}, f'pieces joined once, only pins are leaves ({joins}, {sorted(leaves)})')
    check(len(es) == 7, f'a connected tree from (3,2) to (6,6): 2 tree + 2 joining + 3 outside = 7 edges ({len(es)})')
    # a dangling branch with no pin is pruned
    es2, j2, p2 = join_and_prune({(0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 1, 1)}, {(0, 0), (2, 0)}, 10, 10)
    check(j2 == 0 and p2 == 2 and es2 == {(0, 0, 0), (0, 1, 0)}, f'dangling branch pruned ({p2} edges)')
    # a pin unconnected to anything is joined
    es3, j3, _ = join_and_prune({(0, 0, 0)}, {(0, 0), (1, 0), (1, 3)}, 10, 10)
    check(j3 == 1 and (1, 1, 0) in es3 and (1, 1, 2) in es3, 'a lone pin joined by a straight path')
    print('\n' + ('SELF TEST PASS' if not fails else 'SELF TEST FAIL'))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main() or 0)
