#!/usr/bin/env python3
"""timing_loop.py -- ISPD 2025 with the contest's timing engine in the loop (HISTORY_COST_SWEEP.md Part 134).

The timing bias's best strength differs by design in a way neither the score weights nor wire geometry
predicts (Part 133: weight rules sum -14.1 / -14.4 over ten designs, a geometric estimate leave-one-out
-9.9, the best per design -17.0). So measure it: route the design at several strengths, score every
candidate with the contest's own OpenROAD (timing, power -- one session for all of them) and congestion
evaluator, and keep the best by the contest score's change against the unbiased route.

  python3 gr/timing_loop.py DESIGN_DIR WORK_DIR [--out OUT.route] [--classes both|routed|input]
      [--refine N] [--reuse EARLIER_WORK_DIR] [--router PATH] [--lean] | --selftest

The ISPD 2025 standard flow (2026-10-16). Without OpenROAD, `@standard25.env` is the one-route fallback.

DESIGN_DIR holds <name>.cap/.net/.def/.sdc (the name is the folder's). Stage 1: unbiased plus top-class
timing strengths k3 in K3_LADDER (classes k3/4, k3/2, k3; power off; absolute, not weight-scaled).
Stage 0 (Part 134): the unbiased route is scored and OpenROAD measures its slack on every failing
endpoint's worst path. Stage 1 runs the ladder twice: classes from that routed slack (PD39ROUTE_TIMING_SLACK,
replace mode: other nets unclassed; tag suffix r) and from the input pin slack (suffix i) -- neither wins
everywhere (routed: visible ariane -3.31 vs -2.27; input: blind mempool_tile_wrap -1.15 vs -0.21, whose
unbiased route fails on only 665 nets). `--classes routed|input` runs one. Stage 2: at the best, the power
term at KP_LADDER with the same classes. Stage 3 (Part 188, default since 2026-11-06; `--refine N`, 0 = off,
default 2): sticky re-classification -- the best route's own failing-path slack merged with every slack
measured before (per net, the worst), and a re-route at the best's setting, while it gains (ten designs
-23.97 -> -25.11, none worse). Every candidate is logged to WORK_DIR/loop.tsv.
`--lean` (Part 189, for designs whose routes are ~11 GB each, mempool_cluster): every route that can no
longer be the best is compressed (xz) once scored, and stage 1 is scored one class source at a time, so at
most four routes sit uncompressed (the scores are the same: each route is scored on its own in a session).
Router runs wait for a free slot (at most 3 tracked runs machine-wide, CLAUDE.md) and run under
/usr/bin/time -l. Standard library only.
"""
import filecmp, os, re, shutil, subprocess, sys, time

GR = os.path.dirname(os.path.abspath(__file__))
EVAL_DIR = os.path.expanduser('~/Code/GlobalRouterRuns/ispd25eval')       # NanGate45 libraries, setRC.tcl
OPENROAD = os.path.expanduser('~/Code/OpenROAD_ISPD25/build/src/openroad')  # the contest's fork
EVALUATOR = os.path.join(EVAL_DIR, 'evaluator')                             # the contest's congestion score
MULTI_TCL = os.path.join(GR, 'timing_loop.tcl')
SLACK_TCL = os.path.join(GR, 'timing_slack.tcl')
# Part 134: the classes come from the unbiased route's slack as OpenROAD measures it (every net on a
# failing endpoint's worst path; the rest unclassed) -- on visible ariane -2.90 vs -2.27 from the input
# slack at the same strength, with a third less added overflow. `--classes input` keeps the input slack.
# (Re-classifying from the best biased route instead dropped the nets the bias had fixed: -1.53.)
# 2026-10-16 (Part 134, the user: fewer steps): 20 / 180 / 540 -- dropping 60 and 1600 loses nothing on the
# ten scored designs (each was best only on blind bsg_chip, by 0.0006); 8 routes per design -> 6
K3_LADDER = [20, 180, 540]
KP_LADDER = [1, 4]
ROUTER = os.path.join(GR, 'pd39route')  # --router PATH to use another build
LEAN = False  # --lean (Part 189)


def env_file(path):
    out = {}
    for line in open(path):
        line = line.strip()
        if line and not line.startswith('#') and '=' in line:
            k, v = line.split('=', 1)
            out[k] = v
    return out


def weights(cap):
    with open(cap) as f:
        f.readline()
        return [float(x) for x in f.readline().split()[-4:]]


def wait_slot():
    while int(subprocess.run("pgrep -f '^/usr/bin/time -l ' | wc -l", shell=True, capture_output=True,
                             text=True).stdout.strip() or 0) >= 3:
        time.sleep(5)


def route(d, name, work, tag, k3, kp, slack_file=None, slack_mode='replace'):
    """One candidate: the 2024 standard plus the timing bias at top-class strength k3 and the power
    term kp; slack_file (the routed-slack file) replaces the input slack as the classes' source
    (slack_mode 'min': lowers the input slack where the file's is worse)."""
    base = {}
    for f in ('standard.env', 'standard24.env', 'fast24.env'):
        base.update(env_file(os.path.join(GR, f)))
    s25 = env_file(os.path.join(GR, 'standard25.env'))
    base['PD37ROUTE_THREADS'] = '8'
    if k3 > 0 or kp > 0:
        base['PD39ROUTE_TIMING_LAYERS'] = f'{k3 / 4},{k3 / 2},{k3}'
        base['PD39ROUTE_POWER_LAYERS'] = str(kp)
        base['PD39ROUTE_LAYER_R'] = s25['PD39ROUTE_LAYER_R']
        base['PD39ROUTE_LAYER_C'] = s25['PD39ROUTE_LAYER_C']
    if slack_file and (k3 > 0 or kp > 0):
        base['PD39ROUTE_TIMING_SLACK'] = slack_file
        base['PD39ROUTE_TIMING_SLACK_MODE'] = slack_mode
    sol = os.path.join(work, f'{tag}.route')
    wait_slot()
    with open(os.path.join(work, f'{tag}.err'), 'w') as err:
        subprocess.run(['/usr/bin/time', '-l', ROUTER, os.path.join(d, name + '.cap'),
                        os.path.join(d, name + '.net'), sol], env={**os.environ, **base}, stdout=subprocess.DEVNULL,
                       stderr=err, check=True)
    ev = subprocess.run([EVALUATOR, os.path.join(d, name + '.cap'), os.path.join(d, name + '.net'), sol],
                        capture_output=True, text=True).stdout
    m = re.search(r'^overflow cost ([\d.eE+-]+)', ev, re.M)
    opens = re.search(r'open nets : (\d+)', ev)
    return {'tag': tag, 'k3': k3, 'kp': kp, 'route': sol, 'of': float(m.group(1)) if m else float('nan'),
            'opens': int(opens.group(1)) if opens else -1}


def pack(c):
    """--lean: compress a scored candidate's route (it can be unpacked again)."""
    if LEAN and os.path.exists(c['route']):
        subprocess.run(['xz', '-T8', '-2', '-f', c['route']], check=True)


def unpack(c):
    if not os.path.exists(c['route']) and os.path.exists(c['route'] + '.xz'):
        with open(c['route'], 'wb') as g:
            subprocess.run(['xz', '-dc', c['route'] + '.xz'], stdout=g, check=True)


def prune(cands, keep):
    """--lean: compress every scored candidate but `keep` (the best so far)."""
    for c in cands:
        if c is not keep:
            pack(c)


def sta(d, name, cands):
    """One OpenROAD session: timing and power of every candidate route, and the endpoint count."""
    env = {**os.environ, 'DESIGN': name, 'DESIGN_DIR': os.path.dirname(d.rstrip('/')),
           'ROUTES': ' '.join(c['route'] for c in cands)}
    # OpenROAD sources its script unquoted, so the script's path must not contain a space ("Global Router")
    tcl = os.path.join(os.path.dirname(cands[0]['route']), 'timing_loop.tcl')
    shutil.copyfile(MULTI_TCL, tcl)
    if ' ' in tcl:
        raise SystemExit('timing_loop.py: the work folder must not contain a space (OpenROAD): ' + tcl)
    out = subprocess.run([OPENROAD, '-exit', tcl], cwd=EVAL_DIR, env=env, capture_output=True, text=True).stdout
    if 'ENDPOINTS' not in out:
        raise SystemExit('timing_loop.py: OpenROAD failed:\n' + out[-2000:])
    blocks = out.split('\nROUTE ')[1:]
    for c, b in zip(cands, blocks):
        c['tns'] = float(re.search(r'^tns (\S+)', b, re.M).group(1))
        c['wns'] = float(re.search(r'^wns (\S+)', b, re.M).group(1))
        c['power'] = float(re.search(r'^Total\s+\S+\s+\S+\s+\S+\s+(\S+)', b, re.M).group(1))
    return int(re.search(r'ENDPOINTS (\d+)', out).group(1))


def net_index(net_path):
    """Net name -> its index in the .net's order (the router's net id)."""
    idx = {}
    with open(net_path) as f:
        for line in f:
            line = line.strip()
            if line and line not in '()' and ',' not in line:
                idx[line.replace('\\', '')] = len(idx)
    return idx


def sta_slack(d, name, c, work, idx):
    """One OpenROAD session: candidate c's timing and power, and its failing-path nets' slack, written
    as the router's routed-slack file (net index, seconds). Returns that file."""
    raw, out = os.path.join(work, c['tag'] + '.slack_names'), os.path.join(work, c['tag'] + '.slack')
    tcl = os.path.join(work, 'timing_slack.tcl')
    shutil.copyfile(SLACK_TCL, tcl)
    env = {**os.environ, 'DESIGN': name, 'DESIGN_DIR': os.path.dirname(d.rstrip('/')), 'ROUTE': c['route'],
           'SLACK_OUT': raw}
    txt = subprocess.run([OPENROAD, '-exit', tcl], cwd=EVAL_DIR, env=env, capture_output=True, text=True).stdout
    if 'SLACKNETS' not in txt:
        raise SystemExit('timing_loop.py: OpenROAD (slack) failed:\n' + txt[-2000:])
    c['tns'] = float(re.search(r'^tns (\S+)', txt, re.M).group(1))
    c['wns'] = float(re.search(r'^wns (\S+)', txt, re.M).group(1))
    c['power'] = float(re.search(r'^Total\s+\S+\s+\S+\s+\S+\s+(\S+)', txt, re.M).group(1))
    missing = 0
    with open(raw) as f, open(out, 'w') as g:
        g.write(f"# routed slack of {c['tag']} (seconds), from OpenROAD\n")
        for line in f:
            nm, v = line.rsplit(' ', 1)
            i = idx.get(nm.replace('\\', ''))
            if i is None:
                missing += 1
                continue
            g.write(f'{i} {float(v) * 1e-9:.6e}\n')
    c['slack_nets'] = sum(1 for _ in open(out)) - 1
    c['slack_unmatched'] = missing
    return out


def delta(c, b, w, n):
    """The contest score's change from the unbiased candidate (lower is better)."""
    return (w[0] * (c['wns'] - b['wns']) + w[1] * (c['tns'] - b['tns']) / n + w[2] * (c['power'] - b['power'])
            + w[3] * (c['of'] - b['of']))


def merge_slack(files, out):
    """Per net, the worst slack over several routed-slack files: a net that failed on any route measured
    so far stays classed (Part 188 -- re-classifying from one biased route alone dropped the nets its bias
    had fixed, Part 134)."""
    worst = {}
    for fn in files:
        for line in open(fn):
            if line.startswith('#') or not line.strip():
                continue
            i, v = line.split()
            worst[int(i)] = min(float(v), worst.get(int(i), float('inf')))
    with open(out, 'w') as g:
        g.write('# worst routed slack (seconds) over ' + ' '.join(os.path.basename(f) for f in files) + '\n')
        for i in sorted(worst):
            g.write(f'{i} {worst[i]:.6e}\n')
    return out


def tag_of(best, it):
    return f"k{best['k3']}{best['src']}" + (f"p{best['kp']}" if best['kp'] else '') + f'm{it}'


def refine(d, name, work, b, best, measured, w, n, rounds, start=1):
    """Part 188, sticky re-classification: measure the best route's own failing-path slack, merge it
    with every slack measured so far (per net, the worst), and re-route at the best's strength and power
    term with those classes (routed source: replace; input source: the input slack lowered by them).
    Repeat while the new route beats the best, up to `rounds` times (numbered from `start`). Returns the
    new candidates."""
    idx = net_index(os.path.join(d, name + '.net'))
    measured, out, cur = list(measured), [], best
    for it in range(start, start + rounds):
        if cur is b:
            break
        keep = {k: cur[k] for k in ('tns', 'wns', 'power')}
        measured.append(sta_slack(d, name, cur, work, idx))
        for k, v in keep.items():  # the same route: OpenROAD's numbers must agree with the scoring session
            if abs(cur[k] - v) > 1e-6 * max(1.0, abs(v)):
                raise SystemExit(f"timing_loop.py: {cur['tag']} {k} {v} re-measured as {cur[k]}")
        merged = merge_slack(measured, os.path.join(work, f'merged{it}.slack'))
        prev = os.path.join(work, f'merged{it - 1}.slack')
        if os.path.exists(prev) and open(merged).read().split('\n', 1)[1] == \
                open(prev).read().split('\n', 1)[1]:
            print(f"{name}: refine {it}: the classes did not change, stopped", flush=True)
            break
        c = dict(route(d, name, work, tag_of(best, it), best['k3'], best['kp'], merged,
                       'replace' if best['src'] == 'r' else 'min'), src=best['src'])
        sta(d, name, [c])
        c['delta'] = delta(c, b, w, n)
        c['classed'] = sum(1 for _ in open(merged)) - 1  # nets in its classes (its own slack_nets is set if measured)
        out.append(c)
        print(f"{name}: refine {it}: {c['tag']} delta {c['delta']:+.4f} (from {cur['tag']} {cur['delta']:+.4f}, "
              f"{c['classed']} nets classed)", flush=True)
        if c['delta'] >= cur['delta'] or filecmp.cmp(c['route'], cur['route'], shallow=False):
            pack(c)
            break
        pack(cur)
        cur = c
    return out


def write_tsv(path, allc, best, w, n, t0):
    with open(path, 'w') as f:
        f.write('tag\tk3\tkp\twns\ttns\tpower\tof\topens\tdelta\tslack_nets\tunmatched\tclassed\n')
        for c in allc:
            f.write(f"{c['tag']}\t{c['k3']}\t{c['kp']}\t{c['wns']}\t{c['tns']}\t{c['power']}\t{c['of']}\t{c['opens']}"
                    f"\t{c['delta']:+.4f}\t{c.get('slack_nets', '')}\t{c.get('slack_unmatched', '')}\t{c.get('classed', '')}\n")
        f.write(f"# best {best['tag']} delta {best['delta']:+.4f}; weights {w}; endpoints {n}; "
                f"{time.time() - t0:.0f} s for {len(allc)} candidates\n")


def reuse(d, name, work, src, rounds, t0):
    """--reuse SRC: refine an earlier loop's result (SRC/loop.tsv, its route files and base.slack) instead
    of re-running stages 0-2. Writes WORK/refine.tsv. SRC may itself be a refined loop (its refine.tsv and
    its latest mergedK.slack, which holds every slack measured there): rounds then continue from K + 1."""
    rows, n = [], None
    tsv = os.path.join(src, 'refine.tsv')
    for line in open(tsv if os.path.exists(tsv) else os.path.join(src, 'loop.tsv')):
        if line.startswith('# best'):
            n = int(re.search(r'endpoints (\d+)', line).group(1))
        elif not line.startswith('tag\t') and line.strip():
            f = line.rstrip('\n').split('\t')
            m = re.match(r'k\d+([ri])', f[0])
            c = {'tag': f[0], 'k3': int(f[1]), 'kp': int(f[2]), 'wns': float(f[3]), 'tns': float(f[4]),
                 'power': float(f[5]), 'of': float(f[6]), 'opens': int(f[7]), 'delta': float(f[8]),
                 'src': m.group(1) if m else '', 'route': os.path.join(work, f[0] + '.route')}
            rows.append(c)
    w = weights(os.path.join(d, name + '.cap'))
    for c in rows:  # loop.tsv prints the change to 4 decimals; recompute it from the full-precision terms
        c['delta'] = delta(c, rows[0], w, n)
    b, best = rows[0], min(rows, key=lambda c: c['delta'])
    for c in (best,):
        if not os.path.exists(c['route']):
            with open(c['route'], 'wb') as g:
                subprocess.run(['xz', '-dc', os.path.join(src, c['tag'] + '.route.xz')], stdout=g, check=True)
    base_slack = os.path.join(src, 'base.slack')
    ks = [int(m.group(1)) for f in os.listdir(src) for m in [re.match(r'merged(\d+)\.slack$', f)] if m]
    measured = ([os.path.join(src, f'merged{max(ks)}.slack')] if ks else
                [base_slack] if os.path.exists(base_slack) else [])
    if best['src'] == 'r' and not measured:
        raise SystemExit('timing_loop.py: --reuse: routed classes but no base.slack in ' + src)
    m = re.search(r'm(\d+)$', best['tag'])
    new = refine(d, name, work, b, best, measured, w, n, rounds, int(m.group(1)) + 1 if m else 1)
    allc = rows + new
    top = min(allc, key=lambda c: c['delta'])
    write_tsv(os.path.join(work, 'refine.tsv'), allc, top, w, n, t0)
    print(f"{name}: reused {best['tag']} {best['delta']:+.4f} -> best {top['tag']} {top['delta']:+.4f}, "
          f"{len(new)} new, {time.time() - t0:.0f} s", flush=True)
    return top


def selftest():
    """The Part 188 helpers on hand-made files: the merge keeps every net and its worst slack, ignores
    comments, and the refined candidate's tag names its strength, source, power term and round."""
    import tempfile
    t = tempfile.mkdtemp()
    a, b = os.path.join(t, 'a.slack'), os.path.join(t, 'b.slack')
    open(a, 'w').write('# a\n3 -1.0e-10\n7 -5.0e-10\n')
    open(b, 'w').write('# b\n7 -2.0e-10\n9 -3.0e-10\n\n')
    got = {int(l.split()[0]): float(l.split()[1]) for l in open(merge_slack([a, b], os.path.join(t, 'm')))
           if not l.startswith('#')}
    ok = got == {3: -1.0e-10, 7: -5.0e-10, 9: -3.0e-10}
    ok &= tag_of({'k3': 540, 'src': 'r', 'kp': 0}, 1) == 'k540rm1'
    ok &= tag_of({'k3': 180, 'src': 'i', 'kp': 4}, 2) == 'k180ip4m2'
    global LEAN  # --lean: a pruned route is compressed, the kept one is not, and unpack restores it exactly
    r1, r2 = os.path.join(t, 'x.route'), os.path.join(t, 'y.route')
    open(r1, 'w').write('net0\n(\n0 0 1 0 5 1\n)\n'); shutil.copyfile(r1, r2)
    LEAN, c1, c2 = True, {'route': r1}, {'route': r2}
    prune([c1, c2], c2)
    ok &= not os.path.exists(r1) and os.path.exists(r1 + '.xz') and os.path.exists(r2)
    unpack(c1)
    ok &= filecmp.cmp(r1, r2, shallow=False)
    LEAN = False
    print('timing_loop selftest:', 'pass' if ok else f'FAIL {got}')
    shutil.rmtree(t)
    return ok


def main():
    if '--selftest' in sys.argv:
        sys.exit(0 if selftest() else 1)
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    d, work = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else None
    name = os.path.basename(d.rstrip('/'))
    os.makedirs(work, exist_ok=True)
    w = weights(os.path.join(d, name + '.cap'))
    t0 = time.time()
    mode = sys.argv[sys.argv.index('--classes') + 1] if '--classes' in sys.argv else 'both'
    if mode not in ('both', 'routed', 'input'):
        raise SystemExit('timing_loop.py: --classes must be both, routed or input')
    global ROUTER, LEAN
    LEAN = '--lean' in sys.argv
    if '--router' in sys.argv:
        ROUTER = os.path.abspath(sys.argv[sys.argv.index('--router') + 1])
    # Part 188 (the user, 2026-11-06): two rounds of sticky re-classification by default; --refine 0 turns it off
    rounds = int(sys.argv[sys.argv.index('--refine') + 1]) if '--refine' in sys.argv else 2
    if '--reuse' in sys.argv:
        top = reuse(d, name, work, os.path.abspath(sys.argv[sys.argv.index('--reuse') + 1]), max(rounds, 1), t0)
        if out:
            unpack(top)
            shutil.copyfile(top['route'], out)
        return
    b = route(d, name, work, 'base', 0, 0)
    srcs = {}
    if mode in ('both', 'routed'):  # stage 0: the unbiased route scored, its failing-path nets' slack measured
        srcs['r'] = sta_slack(d, name, b, work, net_index(os.path.join(d, name + '.net')))
    if mode in ('both', 'input'):
        srcs['i'] = None  # the input pin slack
    if 'r' in srcs:
        pack(b)  # --lean: the unbiased route is not read again
    b['delta'], b['src'] = 0.0, ''
    if LEAN:  # one class source per session; the best so far stays unpacked
        s1, n, best = [], None, b
        for t, sf in srcs.items():
            g = [dict(route(d, name, work, f'k{k}{t}', k, 0, sf), src=t) for k in K3_LADDER]
            m = sta(d, name, g if 'r' in srcs or n is not None else [b] + g)
            if n is not None and m != n:
                raise SystemExit(f'timing_loop.py: endpoint count {m} != {n}')
            n = m
            for c in g:
                c['delta'] = delta(c, b, w, n)
            s1 += g
            best = min([b] + s1, key=lambda c: c['delta'])
            prune([b] + s1, best)
    else:
        s1 = [dict(route(d, name, work, f'k{k}{t}', k, 0, sf), src=t) for t, sf in srcs.items() for k in K3_LADDER]
        n = sta(d, name, s1 if 'r' in srcs else [b] + s1)
        for c in s1:
            c['delta'] = delta(c, b, w, n)
        best = min([b] + s1, key=lambda c: c['delta'])
    sf = srcs.get(best['src']) if best['src'] else srcs.get('r')
    s2 = [dict(route(d, name, work, f'k{best["k3"]}{best["src"]}p{kp}', best['k3'], kp, sf), src=best['src'])
          for kp in KP_LADDER]
    sta(d, name, s2)
    for c in s2:
        c['delta'] = delta(c, b, w, n)
    allc = [b] + s1 + s2
    best = min(allc, key=lambda c: c['delta'])
    prune(allc, best)
    if rounds > 0:  # Part 188: sticky re-classification from the best route's own slack
        allc += refine(d, name, work, b, best, [srcs['r']] if srcs.get('r') else [], w, n, rounds)
        best = min(allc, key=lambda c: c['delta'])
    write_tsv(os.path.join(work, 'loop.tsv'), allc, best, w, n, t0)
    if out:
        unpack(best)
        shutil.copyfile(best['route'], out)
    print(f"{name}: best {best['tag']} (k3 {best['k3']}, kp {best['kp']}) delta {best['delta']:+.4f}, "
          f"{len(allc)} candidates, {time.time() - t0:.0f} s")


if __name__ == '__main__':
    main()
