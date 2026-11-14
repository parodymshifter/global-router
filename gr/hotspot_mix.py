#!/usr/bin/env python3
"""hotspot_mix.py -- joint selection over complete routings of one ISPD 2008 design (HISTORY_COST_SWEEP.md Part 206).

  python3 gr/hotspot_mix.py DESIGN.gr OUT.route ROUTE1[.gz] ROUTE2[.gz] ... [--margin 10] [--time 600]
                            [--relax-only] [--no-reduce] [--highs] [--local]

The first test of Part 151's option 5 (choose one route per net jointly, by integer programming), with no new
candidate generator: the candidates are each net's routes in several complete, legal routings of the design (the
reorderings portfolio leaves six). The first route file is the base. Every net with wire inside the window -- the
bounding box of the base's over-capacity edges grown by --margin tiles -- may take its route from any of the files;
every other net keeps its base route. An integer program (SCIP, the `scip` command, started from the base) picks one route per movable
net to minimise total overflow, counted as the evaluators count it: per 3D edge, the sum over nets of (the net's
minimum width + the layer's minimum spacing) above the edge's capacity. Exact for this candidate set (or the best
found in --time seconds, with the solver's bound). A tiny wirelength term breaks ties. The chosen routes are written
as OUT.route; score it with gr_verify and eval2008.pl -- the program's own count is only a prediction.
Modes (Part 206): --relax-only stops after the relaxation (HiGHS, interior point: the bound alone); --no-reduce
keeps every candidate in the integer program and starts it from the base; --highs solves it with HiGHS instead of
SCIP; --local replaces the integer program by a single-net-move search with plateau walks, from the base and from
the relaxation's heaviest routes. Standard library only; needs `highs` (and `scip` unless --highs or --local) on the
PATH.
"""
import gzip
import os
import random
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
    # the window: every tile within `margin` of an over-capacity edge (the union of those neighbourhoods, not their
    # bounding box: newblue7's overflow sits in several hotspots, and one box around them held 207831 nets)
    near = set()
    for e in over:
        for dx in range(-m, m + 2):
            for dy in range(-m, m + 2):
                near.add((e[1] + dx, e[2] + dy))
    inwin = lambda e: (e[1], e[2]) in near
    print(f'base {os.path.basename(files[0])}: overflow {base_of} on {len(over)} edges; window x {x0}-{x1} y {y0}-{y1} '
          f'({time.time() - t0:.0f} s); the window is {len(near)} tiles around the over-capacity edges', flush=True)
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
    def solve(relax):
        """Build the program over the current candidates and solve it: the relaxation with HiGHS, or the integer
        program with SCIP started from every net's first candidate."""
        multi = [n for n in movable if len(cand[n]) > 1]
        worst = defaultdict(int)
        for n in movable:
            mx = defaultdict(int)
            for c in cand[n]:
                for e, a_ in c[0].items():
                    if a_ > mx[e]:
                        mx[e] = a_
            for e, a_ in mx.items():
                worst[e] += a_
        hot = {e for e, a_ in worst.items() if use[e] + a_ > cap_of(g, e)}
        const = sum(max(0, use[e] - cap_of(g, e)) for e in use if e not in hot)
        lp = out + ('.relax.lp' if relax else '.lp')
        eid = {e: i for i, e in enumerate(sorted(hot))}
        rows = defaultdict(list)
        vid = {}
        with open(lp, 'w') as f:
            f.write('Minimize\n obj:')
            for i in eid.values():
                f.write(f' + o{i}')
            for ni, n in enumerate(multi):
                for k, c in enumerate(cand[n]):
                    vid[(n, k)] = f'x{ni}_{k}'
                    f.write(f' + {1e-6 * c[3]:.6f} x{ni}_{k}')
                    for e, a_ in c[0].items():
                        if e in eid:
                            rows[eid[e]].append((a_, f'x{ni}_{k}'))
            f.write('\nSubject To\n')
            fixed = defaultdict(int)
            for n in movable:
                if len(cand[n]) == 1:
                    for e, a_ in cand[n][0][0].items():
                        if e in eid:
                            fixed[eid[e]] += a_
            for e, i in eid.items():
                f.write(f' c{i}: o{i}' + ''.join(f' - {a_} {v}' for a_, v in rows[i]) + f' >= {use[e] + fixed[i] - cap_of(g, e)}\n')
            for ni, n in enumerate(multi):
                f.write(f' s{ni}:' + ''.join(f' + x{ni}_{k}' for k in range(len(cand[n]))) + ' = 1\n')
            f.write('Bounds\n')
            for i in eid.values():
                f.write(f' o{i} >= 0\n')
            if not relax:
                f.write('Binary\n')
                for v in vid.values():
                    f.write(f' {v}\n')
            f.write('End\n')
        print(f'{"relaxation" if relax else "integer program"}: {len(multi)} free nets, {len(vid)} route variables, '
              f'{len(eid)} edges that can overflow, constant {const} ({time.time() - t0:.0f} s)', flush=True)
        sol = out + ('.relax.sol' if relax else '.sol')
        if os.path.exists(sol):
            os.remove(sol)
        vals = {}
        if not vid:
            return multi, vid, vals, eid, const
        if relax:
            # interior point: the simplex did not finish the margin-10 relaxation in 600 s
            r = subprocess.run(['highs', '--solver', 'ipm', '--time_limit', str(opt['--time']), '--solution_file', sol, lp],
                               capture_output=True, text=True)
            tail = [l for l in r.stdout.splitlines() if re.search(r'Objective value|Model status|HiGHS run time', l)]
        else:
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
            if '--highs' in sys.argv:
                # HiGHS for the integer program (SCIP's simplex did not finish newblue4's root relaxation in 1800 s:
                # dual bound -5.98; HiGHS solves it in seconds). Its start must be in its own solution format, so it
                # writes one itself: the same program with every route variable fixed to the base.
                fixed_lp, fixed_sol = out + '.fixed.lp', out + '.fixed.sol'
                text = open(lp).read()
                fix = ''.join(f' {v} = {1 if k == 0 else 0}\n' for (n_, k), v in vid.items())
                with open(fixed_lp, 'w') as f:
                    f.write(text.replace('Binary\n', fix + 'Binary\n'))
                subprocess.run(['highs', '--solution_file', fixed_sol, fixed_lp], capture_output=True, text=True)
                r = subprocess.run(['highs', '--time_limit', str(opt['--time']), '--read_solution_file', fixed_sol,
                                    '--solution_file', sol, lp], capture_output=True, text=True)
                tail = [l for l in r.stdout.splitlines() if re.search(r'Primal bound|Dual bound|Status|Solution status|MIP start', l)]
            else:
                r = subprocess.run(['scip', '-c', f'read {lp}', '-c', f'read {start}', '-c', f'set limits time {opt["--time"]}',
                                    '-c', 'optimize', '-c', f'write solution {sol}', '-c', 'quit'], capture_output=True, text=True)
                tail = [l for l in r.stdout.splitlines() if re.search(r'^(SCIP Status|Primal Bound|Dual Bound|Solving Time)', l)]
        print('  ' + ' | '.join(' '.join(l.split()) for l in tail)[:400], flush=True)
        for line in open(sol):
            if line.startswith('# Dual solution'):
                break  # an LP's file goes on with reduced costs under the same names: primal values only
            a_ = line.split()
            if len(a_) >= 2 and a_[0][0] == 'x':
                try:
                    vals[a_[0]] = float(a_[1])
                except ValueError:
                    pass
        return multi, vid, vals, eid, const

    def local_search(lp_first):
        """--local: a search built for this program instead of a generic solver. From a start (the base; then the
        relaxation's heaviest route per net), sweep the free nets in random order and move each to its route with the
        largest drop in overflow; a move that leaves overflow unchanged is taken 3 times in 10 (a walk along the
        plateau), a move that raises it never. Ends after 60 sweeps without a gain or --time seconds per start.
        Returns the better end state and its overflow."""
        rng = random.Random(1)
        free = sorted(n for n in movable if len(cand[n]) > 1)
        edges = set()
        for n in movable:
            for c in cand[n]:
                edges.update(c[0])
        capd = {e: cap_of(g, e) for e in edges}

        def run(choice, label):
            load = {e: use[e] for e in edges}
            for n in movable:
                for e, a_ in cand[n][choice.get(n, 0)][0].items():
                    load[e] += a_
            obj = start = sum(max(0, load[e] - capd[e]) for e in edges)
            t1, sweeps, idle, moves = time.time(), 0, 0, 0
            while idle < 60 and time.time() - t1 < opt['--time']:
                rng.shuffle(free)
                before = obj
                for n in free:
                    k0 = choice.get(n, 0)
                    cur = cand[n][k0][0]
                    best_d, best_k = 1, -1
                    for k, c in enumerate(cand[n]):
                        if k == k0:
                            continue
                        new, d = c[0], 0
                        for e, a_ in cur.items():
                            b_ = new.get(e, 0)
                            if a_ != b_:
                                over_ = load[e] - capd[e]
                                d += max(0, over_ - a_ + b_) - max(0, over_)
                        for e, b_ in new.items():
                            if e not in cur:
                                over_ = load[e] - capd[e]
                                d += max(0, over_ + b_) - max(0, over_)
                        if d < best_d:
                            best_d, best_k = d, k
                    if best_k >= 0 and (best_d < 0 or (best_d == 0 and rng.random() < 0.3)):
                        for e, a_ in cur.items():
                            load[e] -= a_
                        for e, b_ in cand[n][best_k][0].items():
                            load[e] += b_
                        choice[n] = best_k
                        obj += best_d
                        moves += 1
                sweeps += 1
                idle = 0 if obj < before else idle + 1
            print(f'local search from {label}: overflow on the candidates\' edges {start} -> {obj} in {sweeps} sweeps, '
                  f'{moves} moves ({time.time() - t1:.0f} s)', flush=True)
            return obj, start, choice

        obj_b, start_b, ch_b = run({}, 'the base')
        best_obj, best_ch = obj_b, ch_b
        if lp_first:
            obj_l, _, ch_l = run(dict(lp_first), 'the relaxation\'s heaviest routes')
            if obj_l < best_obj:
                best_obj, best_ch = obj_l, ch_l
        return best_ch, base_of + best_obj - start_b

    # Part 206, step 1: the relaxation first. A net whose weight sits on one route (>= 0.999) is fixed to it; a split
    # net keeps the routes that carry weight, the heaviest first (the integer program's start); the integer program
    # then runs over the split nets only.
    if len(movable) > 150000:
        print(f'{len(movable)} movable nets: too many for this tool (the overflow is not one hotspot)')
        return
    multi, vid, vals, eid, const = solve(relax=True)
    if '--relax-only' in sys.argv:  # Part 206: the bound alone
        return
    whole = split = 0
    if not vals:
        print('relaxation: no solution in the time limit; the integer program keeps every candidate', flush=True)
    lp_first = {}  # per net, the route carrying the most weight in the relaxation (the local search's second start)
    for n in (multi if vals else []):
        w_ = [vals.get(vid[(n, k)], 0.0) for k in range(len(cand[n]))]
        lp_first[n] = max(range(len(w_)), key=lambda k: w_[k])
    if '--no-reduce' in sys.argv or '--local' in sys.argv:  # every candidate kept, started from the base (fixing the
        vals = {}                  # relaxation's whole nets left a program SCIP ended far above the base: newblue4 486)
        print('no reduction: the integer program keeps every candidate and starts from the base', flush=True)
    for n in (multi if vals else []):
        w_ = [vals.get(vid[(n, k)], 0.0) for k in range(len(cand[n]))]
        order = sorted(range(len(w_)), key=lambda k: -w_[k])
        if w_[order[0]] >= 0.999:
            cand[n] = [cand[n][order[0]]]
            whole += 1
        else:
            cand[n] = [cand[n][k] for k in order if w_[k] > 1e-6]
            split += 1
    moved = sum(1 for n in multi if len(cand[n]) == 1 and cand[n][0][2] != 0)
    print(f'relaxation: {whole} nets whole on one route ({moved} of them not the base\'s), {split} split', flush=True)
    if '--local' in sys.argv:
        chosen, pred = local_search(lp_first)
    else:
        multi, vid, vals, eid, const = solve(relax=False)
        chosen = {}
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
