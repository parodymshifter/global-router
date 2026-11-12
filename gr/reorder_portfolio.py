#!/usr/bin/env python3
"""reorder_portfolio.py -- ISPD 2008: several reorderings of the slow tier under the two-ended search, the best kept
(HISTORY_COST_SWEEP.md Part 202).

  python3 gr/reorder_portfolio.py DESIGN.gr WORK_DIR [--out BEST.route] [--spreads 64,56,80,68,60,72] [--jobs 2]
      [--router ./pd39route] [--set VAR=value ...] [--settings @standard.env @fast08.env @speed08.env] | --selftest

Why (Parts 198-201). The two-ended search (PD39ROUTE_SEARCH_BIDIR=2) routes the sixteen designs 3.1x faster and
leaves the eleven zero-overflow designs as they were, but each residue comes out a little worse (newblue1 over six
reorderings: 27.7 on average against the one-ended 24.0) with a wide scatter (22-36): which of the equal-cost routes a
search returns is not neutral over hundreds of negotiation rounds. The scatter is also the opportunity: a reordering
of the slow tier's batches (PD38ROUTE_SLOW_SPREAD, quality-neutral on average) costs one fast run, and the best of a
few is as good as the one-ended runs' best. So: spend part of the saved time on overflow.

What it does.
  1. Routes the design once at the first spread (64, the standard's). Every run goes through run_batch.sh /
     run_checked.sh: tracked memory and time, gr_verify, eval2008.pl, one summary line.
  2. If that run is legal and ends at zero overflow, it is the result: a design that reaches zero pays nothing.
  3. Otherwise routes the remaining spreads, --jobs at a time (never more than 3, the machine-wide cap; the routes
     do not depend on how many run at once), and keeps the best by the contest's order: total overflow, then max
     overflow, then scored wirelength -- among runs with status=OK only (legal, and gr_verify = eval2008.pl).
  4. Writes WORK_DIR/portfolio.tsv (every run, the kept one marked, wall time of the whole portfolio) and, with
     --out, the kept route (uncompressed unless the name ends in .gz).

Settings: --settings (default the 2008 standard with the speed layer) plus PD39ROUTE_SEARCH_BIDIR=2 and
PD39ROUTE_REGION_COST_REROUTES=1600000 (Part 199's stop, which the two-ended search needs), plus any --set pairs,
which come last and so override. Standard library only. WORK_DIR must not contain a space (job lines are split on
spaces); a design path with a space is linked into WORK_DIR.
"""
import argparse
import gzip
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

GR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SPREADS = '64,56,80,68,60,72'
DEFAULT_SETTINGS = ['@standard.env', '@fast08.env', '@speed08.env']
SEARCH = ['PD39ROUTE_SEARCH_BIDIR=2', 'PD39ROUTE_REGION_COST_REROUTES=1600000']
FIELDS = ('status', 'of', 'max', 'wl', 'eval_of', 'eval_max', 'eval_wl', '2d', 'real_s', 'cpu_s', 'rss_mb')


def parse_summary(line):
    f = line.split()
    rec = {'name': f[0]}
    for kv in f[1:]:
        if '=' in kv:
            k, v = kv.split('=', 1)
            rec[k] = v
    return rec


def key(rec):
    """The contest's order: total overflow, max overflow, scored wirelength."""
    return (int(rec['of']), int(rec['max']), int(rec['wl']))


def best_of(recs):
    ok = [r for r in recs if r.get('status') == 'OK']
    return min(ok, key=key) if ok else None


def run_batch(work, tag, bench, router, settings, sets, spreads, jobs, part):
    jobfile = os.path.join(work, f'{tag}.{part}.jobs')
    with open(jobfile, 'w') as f:
        for sp in spreads:
            f.write(' '.join([f'{tag}_sp{sp}', bench, router] + settings + SEARCH + [f'PD38ROUTE_SLOW_SPREAD={sp}'] + sets) + '\n')
    env = dict(os.environ, MAX_JOBS=str(jobs))
    with open(os.path.join(work, f'{tag}.{part}.log'), 'w') as log:
        subprocess.run([os.path.join(GR, 'run_batch.sh'), jobfile, work], cwd=GR, env=env, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    recs = []
    for line in open(jobfile + '.summary'):
        if line.startswith(tag + '_sp'):
            r = parse_summary(line)
            r['spread'] = int(r['name'].rsplit('_sp', 1)[1])
            recs.append(r)
    return recs


def portfolio(design, work, out=None, spreads=DEFAULT_SPREADS, jobs=2, router='./pd39route', settings=None, sets=None,
              log=print):
    if ' ' in work:
        raise SystemExit('reorder_portfolio.py: WORK_DIR must not contain a space: ' + work)
    os.makedirs(work, exist_ok=True)
    jobs = max(1, min(3, jobs))
    settings = settings or DEFAULT_SETTINGS
    sets = sets or []
    spreads = [int(x) for x in str(spreads).split(',') if x]
    if not spreads:
        raise SystemExit('reorder_portfolio.py: no spreads given')
    tag = re.sub(r'[^A-Za-z0-9]+', '_', os.path.basename(design).split('.')[0])
    bench = os.path.abspath(design)
    if ' ' in bench:
        link = os.path.join(work, os.path.basename(design))
        if os.path.lexists(link):
            os.remove(link)
        os.symlink(bench, link)
        bench = link
    t0 = time.time()
    log(f'{tag}: spread {spreads[0]} first')
    recs = run_batch(work, tag, bench, router, settings, sets, spreads[:1], 1, 'first')
    first = recs[0] if recs else None
    # the router must be a build that has the two-ended search: an older one ignores the setting without a word
    err = os.path.join(work, f'{tag}_sp{spreads[0]}.stderr')
    if any(x.startswith('PD39ROUTE_SEARCH_BIDIR=') and not x.endswith('=0') for x in SEARCH + sets) and not (
            os.path.exists(err) and re.search(r'^two-ended search: \d+ searches', open(err, errors='replace').read(), re.M)):
        raise SystemExit(f'reorder_portfolio.py: {router} did not search from both ends (no "two-ended search:" line in '
                         f'{err}): pass --router a build with PD39ROUTE_SEARCH_BIDIR')
    early = bool(first) and first.get('status') == 'OK' and int(first['of']) == 0
    if early:
        log(f"{tag}: zero overflow at spread {spreads[0]} ({float(first['real_s']):.0f} s): no further runs")
    elif len(spreads) > 1:
        log(f"{tag}: spread {spreads[0]} ended {first.get('status') if first else 'without a summary'} at overflow "
            f"{first.get('of') if first else '?'}; routing spreads {spreads[1:]}, {jobs} at a time")
        recs += run_batch(work, tag, bench, router, settings, sets, spreads[1:], jobs, 'rest')
    wall = time.time() - t0
    best = best_of(recs)
    with open(os.path.join(work, 'portfolio.tsv'), 'w') as f:
        f.write('spread\t' + '\t'.join(FIELDS) + '\tkept\n')
        for r in recs:
            f.write(f"{r['spread']}\t" + '\t'.join(r.get(k, '') for k in FIELDS) + f"\t{'kept' if r is best else ''}\n")
        f.write(f"# {tag}: {len(recs)} runs in {wall:.0f} s wall ({jobs} at a time after the first); kept "
                + (f"spread {best['spread']}: overflow {best['of']}, max {best['max']}, wirelength {best['wl']}" if best
                   else 'nothing (no run with status=OK)') + '\n')
    if best is None:
        log(f'{tag}: no run ended with status=OK')
        return None, recs, wall
    log(f"{tag}: kept spread {best['spread']}: overflow {best['of']} (max {best['max']}), wirelength {best['wl']}; "
        f"{len(recs)} runs, {wall:.0f} s wall; overflow by spread: "
        + ', '.join(f"{r['spread']}: {r.get('of', '?')}" for r in recs))
    if out:
        src = os.path.join(work, best['name'] + '.route.gz')
        if out.endswith('.gz'):
            shutil.copyfile(src, out)
        else:
            with gzip.open(src, 'rb') as a, open(out, 'wb') as b:
                shutil.copyfileobj(a, b)
    return best, recs, wall


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(('  ok   ' if cond else '  FAIL ') + what)
        ok = ok and cond

    # the selection rule, with made-up summaries: overflow first, then max overflow, then wirelength; status=OK only
    lines = ['a_sp64 status=OK of=30 max=2 wl=100 eval_of=30 eval_max=2 eval_wl=100 2d=1 real_s=1 cpu_s=1 rss_mb=1',
             'a_sp56 status=OK of=22 max=4 wl=300 eval_of=22 eval_max=4 eval_wl=300 2d=1 real_s=1 cpu_s=1 rss_mb=1',
             'a_sp80 status=OK of=22 max=2 wl=250 eval_of=22 eval_max=2 eval_wl=250 2d=1 real_s=1 cpu_s=1 rss_mb=1',
             'a_sp68 status=OK of=22 max=2 wl=200 eval_of=22 eval_max=2 eval_wl=200 2d=1 real_s=1 cpu_s=1 rss_mb=1',
             'a_sp60 status=DISAGREE of=10 max=2 wl=50 eval_of=12 eval_max=2 eval_wl=50 2d=1 real_s=1 cpu_s=1 rss_mb=1',
             'a_sp72 status=ILLEGAL of=0 max=0 wl=10 eval_of=0 eval_max=0 eval_wl=10 2d=1 real_s=1 cpu_s=1 rss_mb=1']
    recs = [parse_summary(l) for l in lines]
    b = best_of(recs)
    check(b is not None and b['name'] == 'a_sp68', 'selection: overflow, then max overflow, then wirelength, among '
          'status=OK only (a lower-overflow run that failed its checks is never kept)')
    check(best_of(recs[4:]) is None, 'selection: nothing kept when no run passed its checks')
    # end to end on two small synthetic designs (the router's own short test settings)
    router = './pd39route'
    for cand in sorted((f for f in os.listdir(GR) if re.fullmatch(r'pd39route(\.p\d+\w*)?', f)), reverse=True):
        if os.access(os.path.join(GR, cand), os.X_OK):
            router = './' + cand  # the newest build present
            break
    short = ['VROUTE_FAST_ROUNDS=6', 'VROUTE_SLOW_ROUNDS=30', 'PD5ROUTE_REGION_ROUNDS=40', 'PD4ROUTE_REFINE_ITERS=6']
    with tempfile.TemporaryDirectory(prefix='reorder_portfolio_') as tmp:
        if ' ' in tmp:
            print('  skip end-to-end: the temporary folder has a space in its path')
            return ok
        for name, args in (('residue', (60, 60, 4, 2200, 11)), ('light', (60, 60, 4, 1500, 11))):
            gr = os.path.join(tmp, name + '.gr')
            with open(gr, 'w') as f:
                subprocess.run([sys.executable, os.path.join(GR, '..', 'perf', 'gen_synth.py')] + [str(a) for a in args],
                               stdout=f, check=True)
            work = os.path.join(tmp, name)
            out = os.path.join(tmp, name + '.best.route')
            best, recs, _ = portfolio(gr, work, out=out, spreads='64,56,80', jobs=3, router=router, sets=short,
                                      log=lambda s: print('    ' + s))
            oks = [r for r in recs if r.get('status') == 'OK']
            if name == 'residue':
                check(len(recs) == 3 and len(oks) == 3, f'residue design: three runs, all status=OK ({len(oks)} of {len(recs)})')
                check(best is not None and key(best) == min(key(r) for r in oks),
                      'residue design: the kept run is the best by (overflow, max, wirelength): '
                      + ', '.join(f"{r['spread']}: {r['of']}" for r in recs))
                kept = os.path.join(work, best['name'] + '.route.gz') if best else ''
                check(bool(best) and os.path.exists(out) and gzip.open(kept, 'rb').read() == open(out, 'rb').read(),
                      'residue design: --out is the kept run\'s route')
                tsv = open(os.path.join(work, 'portfolio.tsv')).read()
                rows = [l.split('\t') for l in tsv.splitlines()[1:] if not l.startswith('#')]
                check(len(rows) == 3 and sum(r[-1] == 'kept' for r in rows) == 1,
                      'residue design: portfolio.tsv lists every run and marks one')
            else:
                check(len(recs) == 1 and best is not None and int(best['of']) == 0,
                      f"light design: zero overflow at the first spread, so one run only ({len(recs)} runs, overflow "
                      f"{best['of'] if best else '?'})")
    return ok


def main():
    if '--selftest' in sys.argv:
        good = selftest()
        print('SELF TEST PASS' if good else 'SELF TEST FAIL')
        raise SystemExit(0 if good else 1)
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('design')
    ap.add_argument('work')
    ap.add_argument('--out')
    ap.add_argument('--spreads', default=DEFAULT_SPREADS)
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--router', default='./pd39route')
    ap.add_argument('--settings', nargs='+', default=DEFAULT_SETTINGS)
    ap.add_argument('--set', nargs='+', default=[], dest='sets')
    a = ap.parse_args()

    def log(s):
        print(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {s}', flush=True)

    best, _, _ = portfolio(a.design, os.path.abspath(a.work), out=a.out, spreads=a.spreads, jobs=a.jobs, router=a.router,
                           settings=a.settings, sets=a.sets, log=log)
    raise SystemExit(0 if best else 1)


if __name__ == '__main__':
    main()
