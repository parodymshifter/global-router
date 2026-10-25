#!/usr/bin/env python3
"""e2e_settings.py -- end-to-end checks that each recent setting takes effect in a real run (HISTORY_COST_SWEEP.md
Part 149). Standard library only.

  python3 gr/e2e_settings.py ROUTER [WORKDIR]

Why: a self-test that sets a setting's variable directly cannot catch a setting that main() reads too late, or
reads but never passes on (Part 148: PD39ROUTE_SLOW_PLATEAU was parsed after the slow tier had run; its start-up
message still printed). Each check here runs the router binary itself, with the setting given the way a run
gives it (the environment, after @standard.env @fast08.env loaded line by line as run_batch.sh does -- never
by sourcing them: fast08.env's PD39ROUTE_PORTFOLIO value contains ';'), on a small synthetic design
(perf/gen_synth.py), and looks for evidence that the mechanism acted: a trace line only the mechanism writes
while it runs, or a change in that stage's own trace against the same run without the setting. A start-up
echo never counts. Short round budgets keep the whole check to about a minute.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHORT = {'VROUTE_FAST_ROUNDS': '6', 'VROUTE_SLOW_ROUNDS': '30', 'PD5ROUTE_REGION_ROUNDS': '40',
         'PD4ROUTE_REFINE_ITERS': '6', 'PD12ROUTE_REGION_PLATEAU': '0', 'PD12ROUTE_REFINE_PLATEAU': '0'}
# hard: overflow that cannot be cleared (slow tier and region stage without gain); residue: a region stage with
# work to do and path A above the fallback's threshold; light: path A at or below it, so the reclaim path runs.
DESIGNS = {'hard': (40, 40, 2, 1800, 7), 'residue': (60, 60, 4, 2200, 11), 'light': (60, 60, 4, 1800, 11)}


def env_file(path):
    out = {}
    for line in open(path):
        line = line.rstrip('\n')
        if not line or line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        out[k] = v
    return out


def main():
    router = os.path.abspath(sys.argv[1])
    work = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix='e2e_settings_')
    os.makedirs(work, exist_ok=True)
    base = dict(os.environ)
    base.update(env_file(os.path.join(HERE, 'standard.env')))
    base.update(env_file(os.path.join(HERE, 'fast08.env')))
    base.update(SHORT)
    for name, args in DESIGNS.items():
        with open(os.path.join(work, name + '.gr'), 'w') as f:
            subprocess.run([sys.executable, os.path.join(HERE, '..', 'perf', 'gen_synth.py')] + [str(a) for a in args],
                           stdout=f, check=True)
    cache = {}

    def run(design, extra, tag):
        key = (design, tuple(sorted(extra.items())))
        if key in cache:
            return cache[key]
        env = dict(base)
        env.update(extra)
        for k in [k for k, v in env.items() if v is None]:
            del env[k]
        out = os.path.join(work, f'{design}.{tag}.route')
        p = subprocess.run([router, os.path.join(work, design + '.gr'), out], env=env, capture_output=True, text=True)
        if p.returncode != 0:
            raise RuntimeError(f'{design} {tag}: router exited {p.returncode}\n' + p.stderr[-2000:])
        cache[key] = p.stderr
        return p.stderr

    def lines(text, prefix):
        return [l for l in text.splitlines() if l.startswith(prefix)]

    def region_rounds(text):
        m = re.search(r'path A \(standard\) region \d+ -> \d+ \((\d+) rounds', text)
        return int(m.group(1)) if m else None

    checks = []

    def check(name, ok, detail=''):
        checks.append((name, ok, detail))
        print(f'  {"ok  " if ok else "FAIL"} {name}' + (f' -- {detail}' if detail else ''))

    off = {'PD39ROUTE_PORTFOLIO': ''}  # isolate a region-stage setting from the portfolio
    print(f'end-to-end settings check: {router}\n  work dir {work}')
    # PD39ROUTE_SLOW_PLATEAU (Part 148): a 3-round window at gain 100% stops any slow tier still above 0.
    a = run('hard', {}, 'base')
    b = run('hard', {'PD39ROUTE_SLOW_PLATEAU': '3,100,0'}, 'slowplateau')
    na, nb = len(lines(a, 'slow round ')), len(lines(b, 'slow round '))
    check('PD39ROUTE_SLOW_PLATEAU stops the slow tier', 'PD39ROUTE_SLOW_PLATEAU), after round' in b and nb < na,
          f'slow rounds {nb} vs {na}')
    # PD39ROUTE_REGION_ABORT (Part 146): on the hard design the region stage cannot beat its start.
    a = run('hard', {**off, 'PD39ROUTE_REGION_ABORT': '0,20'}, 'abortoff')
    b = run('hard', {**off, 'PD39ROUTE_REGION_ABORT': '1000,5'}, 'abort5')
    check('PD39ROUTE_REGION_ABORT stops a region stage without gain',
          region_rounds(b) == 5 and (region_rounds(a) or 0) > 5, f'region rounds {region_rounds(b)} vs {region_rounds(a)}')
    # ... and with the portfolio on, the variants are then skipped.
    b = run('hard', {'PD39ROUTE_REGION_ABORT': '1000,5'}, 'abort5pf')
    check('PD39ROUTE_REGION_ABORT skips the portfolio after a stopped stage', 'stopped without gain' in b and
          'late-stage portfolio: variant 1' not in b)
    # PD39ROUTE_PORTFOLIO / _WHEN (Part 135): residue design, path A above the threshold.
    b = run('residue', {'PD39ROUTE_PORTFOLIO': 'lagr=0.2'}, 'pf')
    check('PD39ROUTE_PORTFOLIO runs its variants', 'late-stage portfolio: variant 1 (lagr=0.2)' in b)
    b = run('light', {'PD39ROUTE_PORTFOLIO': 'lagr=0.2', 'PD39ROUTE_PORTFOLIO_WHEN': 'after'}, 'pfafter')
    c = run('light', {'PD39ROUTE_PORTFOLIO': 'lagr=0.2', 'PD39ROUTE_PORTFOLIO_WHEN': 'always'}, 'pfalways')
    check('PD39ROUTE_PORTFOLIO_WHEN gates the variants', 'variants not run' in b and
          'late-stage portfolio: variant 1 (lagr=0.2)' in c)
    # region-stage settings on the residue design: each changes the region stage's own trace.
    base_res = run('residue', off, 'base')
    rr0 = lines(base_res, 'region round ')
    # (a Lagrangian step of 0.5 changes nothing on this small design in 40 rounds -- the region stage did receive
    # it, per its dump header -- so the check uses a strong step; the dump-header check below covers arrival)
    for var, val in [('PD39ROUTE_REGION_LAGRANGE', '20'), ('PD39ROUTE_REGION_DAMP', '0.5'),
                     ('PD39ROUTE_REGION_COOLDOWN', '1'), ('PD39ROUTE_REGION_WL_FLOOR', '0.2')]:
        b = run('residue', {**off, var: val}, var.lower())
        check(f'{var} changes the region stage', lines(b, 'region round ') != rr0 and len(rr0) > 0)
    b = run('residue', {**off, 'PD39ROUTE_REGION_WALL_H': '8'}, 'wallh')
    pops = lambda t: sum(int(m.group(1)) for m in re.finditer(r'^region round \d+: .*?pops (\d+)\)', t, re.M))
    check('PD39ROUTE_REGION_WALL_H runs in the region stage and cuts its search work',
          any(' wall-h B 8 ' in l for l in lines(b, 'region round ')) and pops(b) < pops(base_res),
          f'region pops {pops(b)} vs {pops(base_res)}')
    b = run('residue', {**off, 'PD39ROUTE_REGION_BOX_EVIDENCE': '2'}, 'evidence')
    check('PD39ROUTE_REGION_BOX_EVIDENCE runs (box evidence in the region rounds)',
          any('box evidence: limited' in l for l in lines(b, 'region round ')))
    b = run('residue', {**off, 'PD39ROUTE_SEARCH_TRACE': '1'}, 'searchtrace')
    check('PD39ROUTE_SEARCH_TRACE counts the region searches',
          any(re.search(r' searches [1-9]', l) for l in lines(b, 'region round ')))
    prefix = os.path.join(work, 'dump')
    b = run('residue', {**off, 'PD39ROUTE_REGION_DUMP': prefix}, 'dump')
    nR = 0
    if os.path.exists(prefix + '.A.txt'):
        nR = sum(1 for l in open(prefix + '.A.txt') if l.startswith('R '))
    check('PD39ROUTE_REGION_DUMP writes one R line per region round', nR > 0 and nR == len(lines(b, 'region round ')),
          f'{nR} R lines')
    prefix2 = os.path.join(work, 'dumplag')
    run('residue', {**off, 'PD39ROUTE_REGION_DUMP': prefix2, 'PD39ROUTE_REGION_LAGRANGE': '0.5'}, 'dumplag')
    head = open(prefix2 + '.A.txt').readline() if os.path.exists(prefix2 + '.A.txt') else ''
    check('PD39ROUTE_REGION_LAGRANGE reaches the region stage (its dump header)', ' lagrange 0.5 ' in head, head.strip())
    # refinement annealing: the refinement trace reports its temperature.
    prefix3 = os.path.join(work, 'searchdump')
    run('residue', {**off, 'PD39ROUTE_SEARCH_DUMP': prefix3 + ',1,0'}, 'searchdump')
    nfiles = len([f for f in os.listdir(work) if f.startswith('searchdump.') and f.endswith('.bin')])
    check('PD39ROUTE_SEARCH_DUMP writes region-stage search instances', nfiles > 0, f'{nfiles} instance files')
    # PD39ROUTE_REGION_A1 (Part 157, trace only): replay lines at the listed rounds, and the routes unchanged.
    b = run('residue', {**off, 'PD39ROUTE_REGION_A1': '0,3'}, 'a1')
    same = open(os.path.join(work, 'residue.a1.route'), 'rb').read() == open(os.path.join(work, 'residue.base.route'), 'rb').read()
    rep = [l for l in b.splitlines() if l.startswith('region A1 round 3 batched B 64 net order')]
    check('PD39ROUTE_REGION_A1 replays the listed rounds and leaves the routes unchanged',
          len(rep) == 1 and any(l.startswith('region A1 round 0 sequential') for l in b.splitlines()) and same,
          (rep[0] if rep else 'no replay line') + ('' if same else ' -- ROUTES DIFFER'))
    # PD39ROUTE_REGION_BATCH (Part 159): the round lines report batches; the same routes on 1 thread as on 8.
    b = run('residue', {**off, 'PD39ROUTE_REGION_BATCH': '4,8'}, 'batch')
    c = run('residue', {**off, 'PD39ROUTE_REGION_BATCH': '4,8', 'PD37ROUTE_THREADS': '1'}, 'batch1')
    rb = [l for l in lines(b, 'region round ') if ' batched B 4 spread 8: ' in l]
    same = open(os.path.join(work, 'residue.batch.route'), 'rb').read() == open(os.path.join(work, 'residue.batch1.route'), 'rb').read()
    check('PD39ROUTE_REGION_BATCH batches the region rounds, the same routes on 1 and 8 threads',
          len(rb) > 0 and len(rb) == len(lines(b, 'region round ')) and lines(b, 'region round ') != rr0 and same,
          (rb[0][:150] if rb else 'no batched round line') + ('' if same else ' -- 1 AND 8 THREADS DIFFER'))
    # settings that cannot run together are refused at start-up (Part 159: the WALL_H / BOX_EVIDENCE guard sat
    # before BOX_EVIDENCE was parsed and could not fire)
    for combo, needle in [({'PD39ROUTE_REGION_WALL_H': '8', 'PD39ROUTE_REGION_BOX_EVIDENCE': '2'}, 'WALL_H cannot be combined with PD39ROUTE_REGION_BOX_EVIDENCE'),
                          ({'PD39ROUTE_REGION_BATCH': '4', 'PD39ROUTE_REGION_DAMP': '0.5'}, 'REGION_BATCH cannot be combined')]:
        env = dict(base)
        env.update({**off, **combo})
        p = subprocess.run([router, os.path.join(work, 'residue.gr'), os.path.join(work, 'refused.route')], env=env,
                           capture_output=True, text=True)
        check('refused at start-up: ' + ' + '.join(combo), p.returncode != 0 and needle in p.stderr,
              f'exit {p.returncode}')
    b = run('residue', {**off, 'PD39ROUTE_REFINE_SHIFT': '2'}, 'shift')
    m = re.search(r'^refine shift: k 2 radius 2: (\d+) legal nets', b, re.M)
    check('PD39ROUTE_REFINE_SHIFT runs before refinement', bool(m), m.group(0) if m else 'no refine shift line')
    b = run('residue', {**off, 'PD39ROUTE_REFINE_ANNEAL': '5,0.9'}, 'anneal')
    check('PD39ROUTE_REFINE_ANNEAL runs in refinement', any(' anneal T ' in l for l in lines(b, 'refine iter ')))
    # the reclaim path's own box cap: on the light design path B runs a region stage of its own.
    a = run('light', {**off}, 'base')
    b = run('light', {**off, 'PD39ROUTE_REGION_BOX_CAP_B': '1'}, 'capb')
    wires = lambda t: re.search(r'A overflow (\d+) wire (\d+), B overflow (\d+) wire (\d+)', t)
    wa, wb = wires(a), wires(b)
    check('PD39ROUTE_REGION_BOX_CAP_B changes only the reclaim path', bool(wa and wb) and wa.group(1, 2) == wb.group(1, 2)
          and wa.group(3, 4) != wb.group(3, 4), f'A {wb.group(1, 2) if wb else None} = {wa.group(1, 2) if wa else None}; '
          f'B {wb.group(3, 4) if wb else None} vs {wa.group(3, 4) if wa else None}')
    failed = [n for n, ok, _ in checks if not ok]
    print(f'\n{len(checks) - len(failed)} of {len(checks)} passed' + (': FAILED ' + ', '.join(failed) if failed else ''))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
