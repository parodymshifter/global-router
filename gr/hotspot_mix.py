#!/usr/bin/env python3
"""hotspot_mix.py -- joint selection over complete routings of one ISPD 2008 design (HISTORY_COST_SWEEP.md Part 206).

  python3 gr/hotspot_mix.py DESIGN.gr OUT.route ROUTE1[.gz] ROUTE2[.gz] ... [--margin 10] [--time 600]

The first test of Part 151's option 5 (choose one route per net jointly, by integer programming), with no new
candidate generator: the candidates are each net's routes in several complete, legal routings of the design (the
reorderings portfolio leaves six). The first route file is the base. Every net with wire inside the window -- the
bounding box of the base's over-capacity edges grown by --margin tiles -- may take its route from any of the files;
every other net keeps its base route. An integer program (SCIP, the `scip` command, started from the base) picks one route per movable
net to minimise total overflow, counted as the evaluators count it: per 3D edge, the sum over nets of (the net's
minimum width + the layer's minimum spacing) above the edge's capacity. Exact for this candidate set (or the best
found in --time seconds, with the solver's bound). A tiny wirelength term breaks ties. The chosen routes are written
as OUT.route; score it with gr_verify and eval2008.pl -- the program's own count is only a prediction.
Standard library only; needs `scip` on the PATH.
"""
import gzip
import os
import re
import subprocess
import sys
import time
from collections import defaultdict


def read_gr(path):
    raw = open(path)

    class Lines:  # blank lines carry nothing in a .gr
        def readline(self):
            while True:
                line = raw.readline()
                if not line or line.strip():
                    return line

    f = Lines()
    nx, ny, nl = (int(v) for v in f.readline().split()[1:4])
    vcap = [int(v) for v in f.readline().split()[2:]]
    hcap = [int(v) for v in f.readline().split()[2:]]
    f.readline()  # minimum width
    spacing = [int(v) for v in f.readline().split()[2:]]
    f.readline()  # via spacing
    llx, lly, tw, th = (int(float(v)) for v in f.readline().split())
    nnets = int(f.readline().split()[-1])
    width = {}
    for _ in range(nnets):
        a = f.readline().split()
        width[a[0]] = int(a[3]) if len(a) > 3 else 1
        for _ in range(int(a[2])):
            f.readline()
    adj = {}
    line = f.readline()
    for _ in range(int(line.split()[0]) if line.strip() else 0):
        a = f.readline().split()
        c1, r1, l1, c2, r2, l2, cap = (int(v) for v in a)
        if r1 == r2:
            adj[(0, min(c1, c2), r1, l1 - 1)] = cap
        else:
            adj[(1, c1, min(r1, r2), l1 - 1)] = cap
    return {'nx': nx, 'ny': ny, 'nl': nl, 'vcap': vcap, 'hcap': hcap, 'spacing': spacing, 'llx': llx, 'lly': lly,
            'tw': tw, 'th': th, 'width': width, 'adj': adj}


def nets_of(path):
    """Yield (name, text block, [(x1, y1, l1, x2, y2, l2)]) per net of a solution file."""
    f = gzip.open(path, 'rt') if path.endswith('.gz') else open(path)
    name, block, segs = None, [], []
    for line in f:
        if line[0] == '(':
            block.append(line)
            a = re.split(r'[(),\-]+', line.strip())
            segs.append(tuple(int(float(v)) for v in a[1:7]))
        elif line[0] == '!':
            block.append(line)
            yield name, ''.join(block), segs
            name, block, segs = None, [], []
        elif line.strip():
            name = line.split()[0]
            block = [line]
            segs = []


def edges_of(g, segs):
    """The 3D unit edges of a net's wire segments, each as written (dir, x, y, layer)."""
    out = []
    for x1, y1, l1, x2, y2, l2 in segs:
        if l1 != l2:
            continue
        c1, c2 = (x1 - g['llx']) // g['tw'], (x2 - g['llx']) // g['tw']
        r1, r2 = (y1 - g['lly']) // g['th'], (y2 - g['lly']) // g['th']
        if r1 == r2:
            out.extend((0, x, r1, l1 - 1) for x in range(min(c1, c2), max(c1, c2)))
        else:
            out.extend((1, c1, y, l1 - 1) for y in range(min(r1, r2), max(r1, r2)))
    return out


def cap_of(g, e):
    c = g['adj'].get(e)
    return c if c is not None else (g['hcap'] if e[0] == 0 else g['vcap'])[e[3]]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    opt = {'--margin': 10, '--time': 600}
    for k in list(opt):
        if k in sys.argv:
            opt[k] = int(sys.argv[sys.argv.index(k) + 1])
            args.remove(sys.argv[sys.argv.index(k) + 1])
    gr, out, files = args[0], args[1], args[2:]
    t0 = time.time()
    g = read_gr(gr)
    sp = g['spacing']
    # pass 1: the base's usage, overflow and window
    use = defaultdict(int)
    for name, _, segs in nets_of(files[0]):
        w = g['width'].get(name, 1)
        for e in edges_of(g, segs):
            use[e] += w + sp[e[3]]
    over = {e: u - cap_of(g, e) for e, u in use.items() if u > cap_of(g, e)}
    base_of = sum(over.values())
    if not over:
        print('the base has no overflow')
        return
    m = opt['--margin']
    x0, x1 = min(e[1] for e in over) - m, max(e[1] for e in over) + m
    y0, y1 = min(e[2] for e in over) - m, max(e[2] for e in over) + m
    inwin = lambda e: x0 <= e[1] <= x1 and y0 <= e[2] <= y1
    print(f'base {os.path.basename(files[0])}: overflow {base_of} on {len(over)} edges; window x {x0}-{x1} y {y0}-{y1} '
          f'({time.time() - t0:.0f} s)', flush=True)
    # pass 2: movable nets (wire inside the window in the base) and their candidates
    cand = defaultdict(list)  # name -> [(edge multiset as dict, block, file index, wirelength)]
    movable = set()
    for name, block, segs in nets_of(files[0]):
        es = edges_of(g, segs)
        if any(inwin(e) for e in es):
            movable.add(name)
    for fi, path in enumerate(files):
        for name, block, segs in nets_of(path):
            if name not in movable:
                continue
            d = defaultdict(int)
            w = g['width'].get(name, 1)
            for e in edges_of(g, segs):
                d[e] += w + sp[e[3]]
            key = tuple(sorted(d.items()))
            if all(key != c[4] for c in cand[name]):
                cand[name].append((dict(d), block, fi, block.count('\n'), key))
    for name in movable:  # frozen usage: the base without the movable nets
        for e, a in cand[name][0][0].items():
            use[e] -= a
    multi = [n for n in movable if len(cand[n]) > 1]
    print(f'{len(movable)} movable nets, {len(multi)} with more than one distinct route '
          f'({sum(len(cand[n]) for n in multi)} candidates) ({time.time() - t0:.0f} s)', flush=True)
    # edges that can overflow: frozen + the largest candidate use of every net > capacity
    worst = defaultdict(int)
    for n in movable:
        mx = defaultdict(int)
        for c in cand[n]:
            for e, a in c[0].items():
                if a > mx[e]:
                    mx[e] = a
        for e, a in mx.items():
            worst[e] += a
    hot = {e for e, a in worst.items() if use[e] + a > cap_of(g, e)}
    const = sum(max(0, use[e] - cap_of(g, e)) for e in use if e not in hot)
    # the program
    lp = out + '.lp'
    eid = {e: i for i, e in enumerate(sorted(hot))}
    rows = defaultdict(list)
    obj = []
    with open(lp, 'w') as f:
        f.write('Minimize\n obj:')
        for e, i in eid.items():
            f.write(f' + o{i}')
        vid = {}
        for ni, n in enumerate(multi):
            for k, c in enumerate(cand[n]):
                vid[(n, k)] = f'x{ni}_{k}'
                f.write(f' + {1e-6 * c[3]:.6f} x{ni}_{k}')
                for e, a in c[0].items():
                    if e in eid:
                        rows[eid[e]].append((a, f'x{ni}_{k}'))
        f.write('\nSubject To\n')
        fixed = defaultdict(int)  # nets with one candidate: constant use on hot edges
        for n in movable:
            if len(cand[n]) == 1:
                for e, a in cand[n][0][0].items():
                    if e in eid:
                        fixed[eid[e]] += a
        for e, i in eid.items():
            rhs = use[e] + fixed[i] - cap_of(g, e)
            terms = ''.join(f' - {a} {v}' for a, v in rows[i])
            f.write(f' c{i}: o{i}{terms} >= {rhs}\n')
        for ni, n in enumerate(multi):
            f.write(f' s{ni}:' + ''.join(f' + x{ni}_{k}' for k in range(len(cand[n]))) + ' = 1\n')
        f.write('Bounds\n')
        for i in eid.values():
            f.write(f' o{i} >= 0\n')
        f.write('Binary\n')
        for v in vid.values():
            f.write(f' {v}\n')
        f.write('End\n')
    print(f'program: {len(vid)} route variables, {len(eid)} edges that can overflow, constant {const} '
          f'({time.time() - t0:.0f} s); solving (limit {opt["--time"]} s)', flush=True)
    sol = out + '.sol'
    # the base as the starting solution (every movable net on its first candidate), then SCIP: without a start the
    # first run (HiGHS, 300 s) never found the base again (primal 14232 against the base's 22, dual bound ~0)
    start = out + '.start.sol'
    with open(start, 'w') as f:
        load = defaultdict(int)
        for ni, n in enumerate(multi):
            f.write(f'x{ni}_0 1\n')
            for e, a_ in cand[n][0][0].items():
                if e in eid:
                    load[eid[e]] += a_
        for e, i in eid.items():
            o = use[e] + fixed[i] + load[i] - cap_of(g, e)
            if o > 0:
                f.write(f'o{i} {o}\n')
    if os.path.exists(sol):
        os.remove(sol)
    r = subprocess.run(['scip', '-c', f'read {lp}', '-c', f'read {start}', '-c', f'set limits time {opt["--time"]}',
                        '-c', 'optimize', '-c', f'write solution {sol}', '-c', 'quit'], capture_output=True, text=True)
    tail = [l for l in r.stdout.splitlines() if re.search(r'^(SCIP Status|Primal Bound|Dual Bound|Gap|Solving Time)', l)]
    print('  ' + ' | '.join(' '.join(l.split()) for l in tail)[:400], flush=True)
    chosen = {}
    vals = {}
    for line in open(sol):
        a = line.split()
        if len(a) >= 2 and a[0][0] == 'x':
            try:
                vals[a[0]] = float(a[1])
            except ValueError:
                pass
    for (n, k), v in vid.items():
        if vals.get(v, 0.0) > 0.5:
            chosen[n] = k
    pred = const
    tot = defaultdict(int)
    for n in movable:
        c = cand[n][chosen.get(n, 0)]
        for e, a in c[0].items():
            if e in eid:
                tot[e] += a
    for e in eid:
        pred += max(0, use[e] + tot[e] - cap_of(g, e))
    from_file = defaultdict(int)
    for n in movable:
        from_file[cand[n][chosen.get(n, 0)][2]] += 1
    print(f'predicted overflow {pred} (base {base_of}); movable nets by source file: '
          + ', '.join(f'{os.path.basename(files[i])}: {from_file[i]}' for i in sorted(from_file)), flush=True)
    with open(out, 'w') as f:
        for name, block, _ in nets_of(files[0]):
            f.write(cand[name][chosen.get(name, 0)][1] if name in movable else block)
    print(f'wrote {out} ({time.time() - t0:.0f} s)')


if __name__ == '__main__':
    main()
