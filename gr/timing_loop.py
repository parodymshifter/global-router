#!/usr/bin/env python3
"""timing_loop.py -- ISPD 2025 with the contest's timing engine in the loop (HISTORY_COST_SWEEP.md Part 134).

The timing bias's best strength differs by design in a way neither the score weights nor wire geometry
predicts (Part 133: weight rules sum -14.1 / -14.4 over ten designs, a geometric estimate leave-one-out
-9.9, the best per design -17.0). So measure it: route the design at several strengths, score every
candidate with the contest's own OpenROAD (timing, power -- one session for all of them) and congestion
evaluator, and keep the best by the contest score's change against the unbiased route.

  python3 gr/timing_loop.py DESIGN_DIR WORK_DIR [--out OUT.route] [--classes both|routed|input]

The ISPD 2025 standard flow (2026-10-16). Without OpenROAD, `@standard25.env` is the one-route fallback.

DESIGN_DIR holds <name>.cap/.net/.def/.sdc (the name is the folder's). Stage 1: unbiased plus top-class
timing strengths k3 in K3_LADDER (classes k3/4, k3/2, k3; power off; absolute, not weight-scaled).
Stage 0 (Part 134): the unbiased route is scored and OpenROAD measures its slack on every failing
endpoint's worst path. Stage 1 runs the ladder twice: classes from that routed slack (PD39ROUTE_TIMING_SLACK,
replace mode: other nets unclassed; tag suffix r) and from the input pin slack (suffix i) -- neither wins
everywhere (routed: visible ariane -3.31 vs -2.27; input: blind mempool_tile_wrap -1.15 vs -0.21, whose
unbiased route fails on only 665 nets). `--classes routed|input` runs one. Stage 2: at the best, the power
term at KP_LADDER with the same classes. Every candidate is logged to WORK_DIR/loop.tsv.
Router runs wait for a free slot (at most 3 tracked runs machine-wide, CLAUDE.md) and run under
/usr/bin/time -l. Standard library only.
"""
import os, re, shutil, subprocess, sys, time

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


def route(d, name, work, tag, k3, kp, slack_file=None):
    """One candidate: the 2024 standard plus the timing bias at top-class strength k3 and the power
    term kp; slack_file (the routed-slack file) replaces the input slack as the classes' source."""
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
        base['PD39ROUTE_TIMING_SLACK_MODE'] = 'replace'
    sol = os.path.join(work, f'{tag}.route')
    wait_slot()
    with open(os.path.join(work, f'{tag}.err'), 'w') as err:
        subprocess.run(['/usr/bin/time', '-l', os.path.join(GR, 'pd39route'), os.path.join(d, name + '.cap'),
                        os.path.join(d, name + '.net'), sol], env={**os.environ, **base}, stdout=subprocess.DEVNULL,
                       stderr=err, check=True)
    ev = subprocess.run([EVALUATOR, os.path.join(d, name + '.cap'), os.path.join(d, name + '.net'), sol],
                        capture_output=True, text=True).stdout
    m = re.search(r'^overflow cost ([\d.eE+-]+)', ev, re.M)
    opens = re.search(r'open nets : (\d+)', ev)
    return {'tag': tag, 'k3': k3, 'kp': kp, 'route': sol, 'of': float(m.group(1)) if m else float('nan'),
            'opens': int(opens.group(1)) if opens else -1}


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


def main():
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
    b = route(d, name, work, 'base', 0, 0)
    srcs = {}
    if mode in ('both', 'routed'):  # stage 0: the unbiased route scored, its failing-path nets' slack measured
        srcs['r'] = sta_slack(d, name, b, work, net_index(os.path.join(d, name + '.net')))
    if mode in ('both', 'input'):
        srcs['i'] = None  # the input pin slack
    s1 = [dict(route(d, name, work, f'k{k}{t}', k, 0, sf), src=t) for t, sf in srcs.items() for k in K3_LADDER]
    n = sta(d, name, s1 if 'r' in srcs else [b] + s1)
    b['delta'], b['src'] = 0.0, ''
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
    with open(os.path.join(work, 'loop.tsv'), 'w') as f:
        f.write('tag\tk3\tkp\twns\ttns\tpower\tof\topens\tdelta\tslack_nets\tunmatched\n')
        for c in allc:
            f.write(f"{c['tag']}\t{c['k3']}\t{c['kp']}\t{c['wns']}\t{c['tns']}\t{c['power']}\t{c['of']}\t{c['opens']}"
                    f"\t{c['delta']:+.4f}\t{c.get('slack_nets', '')}\t{c.get('slack_unmatched', '')}\n")
        f.write(f"# best {best['tag']} delta {best['delta']:+.4f}; weights {w}; endpoints {n}; "
                f"{time.time() - t0:.0f} s for {len(allc)} candidates\n")
    if out:
        shutil.copyfile(best['route'], out)
    print(f"{name}: best {best['tag']} (k3 {best['k3']}, kp {best['kp']}) delta {best['delta']:+.4f}, "
          f"{len(allc)} candidates, {time.time() - t0:.0f} s")


if __name__ == '__main__':
    main()
