#!/usr/bin/env python3
"""variant_compare.py -- capacity-tightened variants (gr/tighten_gr.py, HISTORY_COST_SWEEP.md Parts 142, 145) beside
their parents. Standard library only.

  python3 gr/variant_compare.py RUNDIR [RUNDIR ...]

Each RUNDIR holds <part>_<parent>_<variant>.{summary,stderr} and the region dumps <...>.dump.<tag>.txt; the
parents are the 2008 standard's Part 124 runs (~/Code/GlobalRouterRuns/runs/p124/p124_fast_<parent>). Per variant:
overflow and wirelength (and change from the parent), the overflow handed to the late stages (2D, after the
slow tier), the run's stage-report time, which late-stage path was kept, the portfolio's results by variant
(and which won), and from the region dump of path A's own settings, the excess-weighted share of overflow on
edges oscillating at 2-4-round periods in the first and the last 50-round window (region_dump_analysis.py's
classes).
"""
import glob
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('rd', os.path.join(HERE, 'region_dump_analysis.py'))
rd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rd)
P124 = os.path.expanduser('~/Code/GlobalRouterRuns/runs/p124/p124_fast_')


def kv(path):
    try:
        return {k: v for k, v in (t.split('=', 1) for t in open(path).read().split() if '=' in t)}
    except OSError:
        return {}


def stage_total(path):
    try:
        m = re.search(r'stage report: total \| ([\d.]+) s', open(path, errors='replace').read())
        return float(m.group(1)) if m else None
    except OSError:
        return None


def osc_shares(dump):
    if not os.path.exists(dump):
        return None
    head, totals, over, rr = rd.read_dump(dump)
    if not totals:
        return None
    rounds = max(totals) + 1
    out = []
    for w0 in (0, max(0, (rounds // rd.WIN - 1) * rd.WIN)):
        cls = {'stuck': 0.0, 'oscillating': 0.0, 'transient': 0.0}
        n = min(rd.WIN, rounds - w0)
        if n < 8:
            out.append(None)
            continue
        for e, rec in over.items():
            ser = [1 if (w0 + t) in rec else 0 for t in range(n)]
            if any(ser):
                cls[rd.classify(ser)] += sum(rec.get(w0 + t, 0) for t in range(n))
        tot = sum(cls.values())
        out.append(cls['oscillating'] / tot if tot else None)
    return rounds, out


def main():
    rows = []
    for d in sys.argv[1:]:
        for s in sorted(glob.glob(os.path.join(d, '*.summary'))):
            base = s[:-len('.summary')]
            name = os.path.basename(base)
            m = re.match(r'p\d+_([a-z]+\d)_(\w+)$', name)
            if not m:
                continue
            parent, var = m.group(1), m.group(2)
            v, p = kv(s), kv(P124 + parent + '.summary')
            err = open(base + '.stderr', errors='replace').read() if os.path.exists(base + '.stderr') else ''
            kept = 'B' if 'kept B' in err else ('A' if 'kept A' in err else '?')
            pf = re.findall(r'late-stage portfolio: variant (\d) \((.*?)\): .*?then 2D overflow (\d+), wire \d+(  <- kept)?', err)
            dump = base + '.dump.' + ('A0' if pf else 'A') + '.txt'
            rows.append(dict(parent=parent, var=var, of=int(v.get('of', -1)), eval_of=v.get('eval_of'),
                             wl=int(v.get('wl', 0)), pwl=int(p.get('wl', 0) or 0), d2=v.get('2d'), pd2=p.get('2d'),
                             t=stage_total(base + '.stderr'), pt=stage_total(P124 + parent + '.stderr'), kept=kept,
                             pf=pf, osc=osc_shares(dump)))
    order = {'g95': 0, 'h85': 1, 'g90': 2, 'h70': 3, 'g85': 4, 'g80': 5}
    rows.sort(key=lambda r: (r['parent'], order.get(r['var'], 9)))
    print(f'{"design":9s} {"variant":7s} {"overflow":>8s} {"wire vs parent":>14s} {"2D in":>7s} {"time vs parent":>15s} '
          f'{"path":>4s}  portfolio (own / lagr .05 / .2 / 1 -> kept)   oscillating first / last window')
    for r in rows:
        wl = f'{100 * (r["wl"] / r["pwl"] - 1):+.1f}%' if r['pwl'] else '?'
        tm = f'{r["t"]:.0f}s vs {r["pt"]:.0f}s' if r['t'] and r['pt'] else '?'
        pf = ' / '.join(x[2] for x in r['pf']) + (' -> ' + next((x[1] for x in r['pf'] if x[3]), '?')) if r['pf'] else '-'
        osc = '-'
        if r['osc']:
            rounds, sh = r['osc']
            osc = ' / '.join('-' if x is None else f'{x:.2f}' for x in sh) + f' ({rounds} rounds)'
        print(f'{r["parent"]:9s} {r["var"]:7s} {r["of"]:8d} {wl:>14s} {str(r["d2"]):>7s} {tm:>15s} {r["kept"]:>4s}  '
              f'{pf:45s} {osc}')


if __name__ == '__main__':
    main()
