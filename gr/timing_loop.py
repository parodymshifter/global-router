#!/usr/bin/env python3
"""timing_loop.py -- ISPD 2025 with the contest's timing engine in the loop (HISTORY_COST_SWEEP.md Part 134).

The timing bias's best strength differs by design in a way neither the score weights nor wire geometry
predicts (Part 133: weight rules sum -14.1 / -14.4 over ten designs, a geometric estimate leave-one-out
-9.9, the best per design -17.0). So measure it: route the design at several strengths, score every
candidate with the contest's own OpenROAD (timing, power -- one session for all of them) and congestion
evaluator, and keep the best by the contest score's change against the unbiased route.

  python3 gr/timing_loop.py DESIGN_DIR WORK_DIR [--out OUT.route]

The ISPD 2025 standard flow (2026-10-16). Without OpenROAD, `@standard25.env` is the one-route fallback.

DESIGN_DIR holds <name>.cap/.net/.def/.sdc (the name is the folder's). Stage 1: unbiased plus top-class
timing strengths k3 in K3_LADDER (classes k3/4, k3/2, k3; power off; absolute, not weight-scaled).
Stage 2: at stage 1's best, the power term at KP_LADDER. Every candidate is logged to WORK_DIR/loop.tsv.
Router runs wait for a free slot (at most 3 tracked runs machine-wide, CLAUDE.md) and run under
/usr/bin/time -l. Standard library only.
"""
import os, re, shutil, subprocess, sys, time

GR = os.path.dirname(os.path.abspath(__file__))
EVAL_DIR = os.path.expanduser('~/Code/GlobalRouterRuns/ispd25eval')       # NanGate45 libraries, setRC.tcl
OPENROAD = os.path.expanduser('~/Code/OpenROAD_ISPD25/build/src/openroad')  # the contest's fork
EVALUATOR = os.path.join(EVAL_DIR, 'evaluator')                             # the contest's congestion score
MULTI_TCL = os.path.join(GR, 'timing_loop.tcl')
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


def route(d, name, work, tag, k3, kp):
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
    s1 = [route(d, name, work, 'base', 0, 0)] + [route(d, name, work, f'k{k}', k, 0) for k in K3_LADDER]
    n = sta(d, name, s1)
    b = s1[0]
    for c in s1:
        c['delta'] = delta(c, b, w, n)
    best = min(s1, key=lambda c: c['delta'])
    s2 = [route(d, name, work, f'k{best["k3"]}p{kp}', best['k3'], kp) for kp in KP_LADDER]
    sta(d, name, s2)
    for c in s2:
        c['delta'] = delta(c, b, w, n)
    allc = s1 + s2
    best = min(allc, key=lambda c: c['delta'])
    with open(os.path.join(work, 'loop.tsv'), 'w') as f:
        f.write('tag\tk3\tkp\twns\ttns\tpower\tof\topens\tdelta\n')
        for c in allc:
            f.write(f"{c['tag']}\t{c['k3']}\t{c['kp']}\t{c['wns']}\t{c['tns']}\t{c['power']}\t{c['of']}\t{c['opens']}"
                    f"\t{c['delta']:+.4f}\n")
        f.write(f"# best {best['tag']} delta {best['delta']:+.4f}; weights {w}; endpoints {n}; "
                f"{time.time() - t0:.0f} s for {len(allc)} candidates\n")
    if out:
        shutil.copyfile(best['route'], out)
    print(f"{name}: best {best['tag']} (k3 {best['k3']}, kp {best['kp']}) delta {best['delta']:+.4f}, "
          f"{len(allc)} candidates, {time.time() - t0:.0f} s")


if __name__ == '__main__':
    main()
