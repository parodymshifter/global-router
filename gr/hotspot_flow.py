#!/usr/bin/env python3
"""hotspot_flow.py -- ISPD 2008: the hotspot repair and the router's late stages after a routed design, one setting for
every design (HISTORY_COST_SWEEP.md Parts 208-211).

  python3 gr/hotspot_flow.py DESIGN.gr BASE.route[.gz] WORK_DIR [--out BEST.route] [--router ./pd39route]
      [--settings @standard.env @fast08.env @speed08.env] [--set VAR=value ...] [--cg-args "..."] | --selftest

Why (Parts 208-210). On a routed design that ends above zero, gr/hotspot_cg.py solves the hotspot as a program over
purpose-made candidate routes (column generation, the program's own edge prices pricing new candidates, a small price
on every edge's last track so the best mix keeps spare room) and rounds the best mix to whole routes. That repair took
newblue4 132 -> 112, bigblue4 318 -> 198 and newblue7 90 -> 68. On newblue1 no rounding beat the base (22), but the
router's late stages -- region stage, fallback, portfolio, refinement, layer assignment -- started from the rounding
(68) reached 0, which none of three ordinary routings of the design did as a start (18-22). Started from a repaired
route they add a little (newblue7 68 -> 62, bigblue4 198 -> 196).

What it does.
  1. Scores BASE with gr_verify and eval2008.pl. A legal route at zero overflow is the result: nothing else runs.
  2. Runs hotspot_cg.py with one setting (--margin 10 --rounds 14 --spare 2 --starts 6 --union, the integer step and
     the dive on): REPAIR = its best whole-route solution if it beats the base, ROUGH = its best rounding other than
     the base whatever its overflow.
  3. Runs the router's late stages (PD3ROUTE_LA_FROM) from REPAIR, and from ROUGH when that is a different file,
     through run_batch.sh / run_checked.sh (tracked memory and time, gr_verify, eval2008.pl, one summary line), at most
     two at a time.
  4. Keeps the best of BASE, REPAIR and the late-stage runs by the contest's order -- total overflow, max overflow,
     scored wirelength -- among those that are legal and on which gr_verify and eval2008.pl agree. Writes
     WORK_DIR/flow.tsv (every candidate, the kept one marked) and, with --out, the kept route.

Standard library only; needs `highs` on the PATH (hotspot_cg.py) and the evaluator (sh gr/fetch_eval2008.sh). WORK_DIR
must not contain a space.
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
DEFAULT_SETTINGS = ['@standard.env', '@fast08.env', '@speed08.env']
CG_ARGS = '--margin 10 --rounds 14 --spare 2 --starts 6 --union'
FIELDS = ('status', 'of', 'max', 'wl', 'eval_of', 'eval_max', 'eval_wl', 'real_s')


def key(rec):
    """The contest's order: total overflow, max overflow, scored wirelength."""
    return (int(rec['of']), int(rec['max']), int(rec['wl']))


def best_of(recs):
    ok = [r for r in recs if r.get('status') == 'OK']
    return min(ok, key=key) if ok else None


def parse_summary(line):
    f = line.split()
    rec = {'name': f[0]}
    for kv in f[1:]:
        if '=' in kv:
            k, v = kv.split('=', 1)
            rec[k] = v
    return rec


def score(bench, route, name):
    """gr_verify and eval2008.pl on a route file, as run_checked.sh judges a run: status OK when legal and both agree."""
    v = subprocess.run([os.path.join(GR, 'gr_verify'), bench, route], cwd=GR, capture_output=True, text=True).stdout
    e = subprocess.run(['perl', os.path.join(GR, 'eval2008', 'eval2008.pl'), bench, route], cwd=GR, capture_output=True,
                       text=True).stdout

    def field(label):
        m = re.search(r'^\s*' + label + r'\s+(\d+)', v, re.M)
        return int(m.group(1)) if m else None

    rec = {'name': name, 'of': field('total overflow'), 'max': field('max overflow'), 'wl': field('scored wirelength')}
    bad = [field(k) for k in ('malformed segments', 'out of bounds', 'disconnected nets', 'incomplete nets', 'unrouted nets')]
    last = [l for l in e.splitlines() if l.strip()][-1:] or ['']
    nums = re.findall(r'\s(\d+)', ' ' + last[0])
    ev = [int(x) for x in nums[-3:]] if len(nums) >= 3 else [None, None, None]
    rec['eval_of'], rec['eval_max'], rec['eval_wl'] = ev
    if None in (rec['of'], rec['max'], rec['wl']) or None in bad:
        rec['status'] = 'NO_VERIFY'
    elif any(bad):
        rec['status'] = 'ILLEGAL'
    elif (rec['of'], rec['max'], rec['wl']) != tuple(ev):
        rec['status'] = 'DISAGREE'
    else:
        rec['status'] = 'OK'
    rec = {k: (str(x) if x is not None else '') for k, x in rec.items()}
    return rec


def flow(design, base, work, out=None, router='./pd39route', settings=None, sets=None, cg_args=CG_ARGS, log=print):
    if ' ' in work:
        raise SystemExit('hotspot_flow.py: WORK_DIR must not contain a space: ' + work)
    os.makedirs(work, exist_ok=True)
    settings = settings or DEFAULT_SETTINGS
    sets = sets or []
    tag = re.sub(r'[^A-Za-z0-9]+', '_', os.path.basename(design).split('.')[0])
    bench = os.path.abspath(design)
    if ' ' in bench:
        link = os.path.join(work, os.path.basename(design))
        if os.path.lexists(link):
            os.remove(link)
        os.symlink(bench, link)
        bench = link
    t0 = time.time()
    base_plain = os.path.join(work, tag + '.base.route')
    if base.endswith('.gz'):
        with gzip.open(base, 'rb') as a, open(base_plain, 'wb') as b:
            shutil.copyfileobj(a, b)
    else:
        shutil.copyfile(base, base_plain)
    recs = [dict(score(bench, base_plain, 'base'), file=base_plain, real_s='0')]
    log(f"{tag}: base {recs[0]['status']}, overflow {recs[0]['of']} (max {recs[0]['max']}), wirelength {recs[0]['wl']}")
    if recs[0]['status'] == 'OK' and int(recs[0]['of']) == 0:
        log(f'{tag}: zero overflow: nothing to do')
    else:
        repair, rough = os.path.join(work, tag + '.repair.route'), os.path.join(work, tag + '.rough.route')
        for f in (repair, rough):
            if os.path.exists(f):
                os.remove(f)
        t1 = time.time()
        with open(os.path.join(work, tag + '.hotspot.txt'), 'w') as f:
            subprocess.run([sys.executable, os.path.join(GR, 'hotspot_cg.py'), bench, base_plain,
                            os.path.join(work, tag + '.cg')] + cg_args.split() + ['--out', repair, '--alt-out', rough],
                           cwd=GR, stdout=f, stderr=subprocess.STDOUT, check=True)
        log(f"{tag}: hotspot program {time.time() - t1:.0f} s: repair {'written' if os.path.exists(repair) else 'none'}, "
            f"rough start {'written' if os.path.exists(rough) else 'none'}")
        starts = []
        if os.path.exists(repair):
            recs.append(dict(score(bench, repair, 'repair'), file=repair, real_s=f'{time.time() - t1:.0f}'))
            starts.append(('late_repair', repair))
        if os.path.exists(rough) and not (os.path.exists(repair) and open(rough, 'rb').read() == open(repair, 'rb').read()):
            starts.append(('late_rough', rough))
        if starts:
            jobfile = os.path.join(work, tag + '.late.jobs')
            with open(jobfile, 'w') as f:
                for name, start in starts:
                    f.write(' '.join([f'{tag}_{name}', bench, router] + settings + [f'PD3ROUTE_LA_FROM={start}'] + sets) + '\n')
            env = dict(os.environ, MAX_JOBS=str(min(2, len(starts))))
            with open(os.path.join(work, tag + '.late.log'), 'w') as lg:
                subprocess.run([os.path.join(GR, 'run_batch.sh'), jobfile, work], cwd=GR, env=env, stdout=lg,
                               stderr=subprocess.STDOUT, check=True)
            for line in open(jobfile + '.summary'):
                if line.startswith(tag + '_late_'):
                    r = parse_summary(line)
                    r['file'] = os.path.join(work, r['name'] + '.route.gz')
                    r['name'] = r['name'][len(tag) + 1:]
                    recs.append(r)
    wall = time.time() - t0
    best = best_of(recs)
    with open(os.path.join(work, 'flow.tsv'), 'w') as f:
        f.write('candidate\t' + '\t'.join(FIELDS) + '\tkept\n')
        for r in recs:
            f.write(f"{r['name']}\t" + '\t'.join(r.get(k, '') for k in FIELDS) + f"\t{'kept' if r is best else ''}\n")
        f.write(f'# {tag}: {len(recs)} candidates in {wall:.0f} s wall; kept '
                + (f"{best['name']}: overflow {best['of']}, max {best['max']}, wirelength {best['wl']}" if best
                   else 'nothing (no candidate passed its checks)') + '\n')
    if best is None:
        log(f'{tag}: no candidate passed its checks')
        return None, recs, wall
    log(f"{tag}: kept {best['name']}: overflow {best['of']} (max {best['max']}), wirelength {best['wl']}; {wall:.0f} s; "
        + ', '.join(f"{r['name']}: {r.get('of', '?')}" for r in recs))
    if out:
        src = best['file']
        if src.endswith('.gz') == out.endswith('.gz'):
            shutil.copyfile(src, out)
        elif src.endswith('.gz'):
            with gzip.open(src, 'rb') as a, open(out, 'wb') as b:
                shutil.copyfileobj(a, b)
        else:
            with open(src, 'rb') as a, gzip.open(out, 'wb') as b:
                shutil.copyfileobj(a, b)
    return best, recs, wall


def selftest():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(('  ok   ' if cond else '  FAIL ') + what)
        ok = ok and cond

    recs = [{'name': 'base', 'status': 'OK', 'of': '22', 'max': '2', 'wl': '100'},
            {'name': 'repair', 'status': 'OK', 'of': '22', 'max': '2', 'wl': '90'},
            {'name': 'late_rough', 'status': 'OK', 'of': '0', 'max': '0', 'wl': '120'},
            {'name': 'late_repair', 'status': 'DISAGREE', 'of': '0', 'max': '0', 'wl': '80'}]
    b = best_of(recs)
    check(b is not None and b['name'] == 'late_rough', 'selection: overflow first, then max, then wirelength, among '
          'candidates that passed their checks only')
    check(best_of(recs[1:2])['name'] == 'repair' and best_of(recs[3:]) is None,
          'selection: a single candidate; nothing kept when none passed')
    # end to end on a small synthetic design: route it, then the flow from that route
    router = './pd39route'
    for cand in sorted((f for f in os.listdir(GR) if re.fullmatch(r'pd39route(\.p\d+\w*)?', f)), reverse=True):
        if os.access(os.path.join(GR, cand), os.X_OK):
            router = './' + cand
            break
    short = ['VROUTE_FAST_ROUNDS=6', 'VROUTE_SLOW_ROUNDS=30', 'PD5ROUTE_REGION_ROUNDS=40', 'PD4ROUTE_REFINE_ITERS=6']
    with tempfile.TemporaryDirectory(prefix='hotspot_flow_') as tmp:
        if ' ' in tmp:
            print('  skip end-to-end: the temporary folder has a space in its path')
            return ok
        gr = os.path.join(tmp, 'residue.gr')
        with open(gr, 'w') as f:
            subprocess.run([sys.executable, os.path.join(GR, '..', 'perf', 'gen_synth.py'), '60', '60', '4', '2200', '11'],
                           stdout=f, check=True)
        jobs = os.path.join(tmp, 'first.jobs')
        with open(jobs, 'w') as f:
            f.write(' '.join(['first', gr, router] + DEFAULT_SETTINGS + short) + '\n')
        subprocess.run([os.path.join(GR, 'run_batch.sh'), jobs, tmp], cwd=GR, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=True)
        first = parse_summary(open(jobs + '.summary').readline())
        check(first.get('status') == 'OK' and int(first['of']) > 0, f"the synthetic design ends above zero ({first.get('of')})")
        out = os.path.join(tmp, 'best.route')
        best, recs, _ = flow(gr, os.path.join(tmp, 'first.route.gz'), os.path.join(tmp, 'work'), out=out, router=router,
                             sets=short, cg_args='--margin 4 --rounds 4 --spare 2 --union --time 60',
                             log=lambda s: print('    ' + s))
        names = [r['name'] for r in recs]
        check(names[0] == 'base' and any(n.startswith('late_') for n in names),
              'the flow scores the base and runs the late stages from at least one start: ' + ', '.join(names))
        check(best is not None and key(best) <= key(recs[0]) and os.path.exists(out),
              f"the kept candidate is no worse than the base ({best['of'] if best else '?'} vs {recs[0]['of']}) and --out "
              'is written')
        rescored = score(gr, out, 'out') if best else {}
        check(bool(best) and rescored.get('status') == 'OK' and rescored['of'] == best['of'],
              '--out re-scores as the kept candidate (gr_verify = eval2008.pl, legal)')
        rows = [l.split('\t') for l in open(os.path.join(tmp, 'work', 'flow.tsv')).read().splitlines()[1:]
                if not l.startswith('#')]
        check(len(rows) == len(recs) and sum(r[-1] == 'kept' for r in rows) == 1, 'flow.tsv lists every candidate, one kept')
    return ok


def main():
    if '--selftest' in sys.argv:
        good = selftest()
        print('SELF TEST PASS' if good else 'SELF TEST FAIL')
        raise SystemExit(0 if good else 1)
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('design')
    ap.add_argument('base')
    ap.add_argument('work')
    ap.add_argument('--out')
    ap.add_argument('--router', default='./pd39route')
    ap.add_argument('--settings', nargs='+', default=DEFAULT_SETTINGS)
    ap.add_argument('--set', nargs='+', default=[], dest='sets')
    ap.add_argument('--cg-args', default=CG_ARGS)
    a = ap.parse_args()

    def log(s):
        print(f'{time.strftime("%Y-%m-%d %H:%M:%S")} {s}', flush=True)

    best, _, _ = flow(a.design, os.path.abspath(a.base), os.path.abspath(a.work), out=a.out, router=a.router,
                      settings=a.settings, sets=a.sets, cg_args=a.cg_args, log=log)
    raise SystemExit(0 if best else 1)


if __name__ == '__main__':
    main()
