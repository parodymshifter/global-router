#!/usr/bin/env python3
"""difficulty_data.py -- join the routability predictors (routability.cpp, one line per design) with the
routing outcomes recorded for each design, into one JSON file for the difficulty explorer page
(HISTORY_COST_SWEEP.md Part 130). Standard library only.

  python3 gr/difficulty_data.py FEATURES.txt ROUTE25.txt OUT.json

Outcomes, per design (None where a set has no such measure):
  detour        routed 2D wire length (GCell steps) / total Steiner tree length of every net -- comparable
                across the three contests
  runtime_knet  router wall time per 1000 nets, seconds (each contest's own standard settings)
  over_share    ISPD 2008: overflowed edges / all 2D edges; ISPD 2024: GCells over capacity / all GCells
  overflow      ISPD 2008 total overflow (the contest's first ranking key)
  of_share      ISPD 2024 / 2025: the overflow term's share of the contest score (falls as wire length grows, so
                it tracks density as much as congestion)
  of_knet       ISPD 2024 / 2025: the overflow term per 1000 nets -- the cleaner congestion outcome
  score_knet    ISPD 2024 / 2025: contest score per 1000 nets
Sources: ISPD 2008 -- Part 124's fast08 runs (pd39route), and for bigblue4 and newblue7 (still running there)
the standard-configuration records s23_bigblue4 (pd23route) and c200_newblue7 (pd13route); ISPD 2024 -- Part
123's pd39route runs; ISPD 2025 -- ROUTE25.txt (pd39route, the 2024 standard, scored by the contest evaluator).
"""
import json
import os
import re
import sys

RUNS = os.path.expanduser('~/Code/GlobalRouterRuns/runs')
BENCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bench')


def kv(line):
    out = {}
    for tok in line.split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            try:
                out[k] = float(v)
            except ValueError:
                out[k] = v
    return out


def read_text(path):
    try:
        return open(path, errors='replace').read()
    except OSError:
        return ''


def num(pattern, text):
    m = re.search(pattern, text, re.M)
    return float(m.group(1)) if m else None


def routed_steps(path, dbu_per_step=None):
    """Wire length of a 2024 / 2025 solution in GCell steps: every segment whose two ends share a layer
    (2024: GCell coordinates; 2025: DEF units, divided by the GCell size)."""
    import subprocess
    if not os.path.exists(path):
        return None
    cat = ['xz', '-dc', path] if path.endswith('.xz') else ['cat', path]
    awk = 'NF==6 && $3==$6 {w += ($4>$1?$4-$1:$1-$4) + ($5>$2?$5-$2:$2-$5)} END {printf "%.0f", w}'
    p1 = subprocess.Popen(cat, stdout=subprocess.PIPE)
    out = subprocess.run(['awk', awk], stdin=p1.stdout, capture_output=True, text=True).stdout
    p1.wait()
    w = float(out or 0)
    return w / dbu_per_step if dbu_per_step else w


def cap_header(cap):
    with open(cap) as f:
        f.readline()
        unit_len = float(f.readline().split()[0])
        pitches = [int(x) for x in f.readline().split()]
    pitches.sort()
    return unit_len, pitches[len(pitches) // 2]


def main():
    feats, route25 = sys.argv[1], sys.argv[2]
    rows = []
    for line in open(feats):
        if not line.startswith('set=') or 'skipped=' in line:
            continue
        rows.append(kv(line))
    r25 = {}
    for rec in re.split(r'(?=set=)', open(route25).read()):  # a record may span lines
        if rec.startswith('set='):
            d = kv(rec.replace('\n', ' '))
            r25[(d['set'], d['design'])] = d
    for r in rows:
        s, n = r['set'], r['design']
        o = dict(detour=None, runtime_knet=None, over_share=None, overflow=None, of_share=None, of_knet=None, score_knet=None)
        rsmt_total = r['rsmt_total']
        if s == 'ispd08':
            base = os.path.join(RUNS, 'p124', f'p124_fast_{n}')
            if not os.path.exists(base + '.verify'):
                base = os.path.join(RUNS, {'bigblue4': 's23_bigblue4', 'newblue7': 'c200_newblue7'}.get(n, ''))
            v, summ = read_text(base + '.verify'), read_text(base + '.summary')
            wl = num(r'^\s*wirelength\s+(\d+)', v)
            edges = (num(r'^\s*h edges\s+(\d+)', v) or 0) + (num(r'^\s*v edges\s+(\d+)', v) or 0)
            o['detour'] = wl / rsmt_total if wl and rsmt_total else None
            o['overflow'] = num(r'^\s*total overflow\s+(\d+)', v)
            oe = num(r'^\s*overflowed edges\s+(\d+)', v)
            o['over_share'] = oe / edges if oe is not None and edges else None
            t = num(r'real_s=([\d.]+)', summ)
            o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
            o['source'] = os.path.basename(base)
        elif s == 'ispd24':
            base = os.path.join(RUNS, 'p123', f'p123_pd39_{n}')
            summ, ver = read_text(base + '.summary'), read_text(base + '.g24verify')
            steps = routed_steps(base + '.g24sol.xz')
            o['detour'] = steps / rsmt_total if steps and rsmt_total else None
            over = num(r'(\d+) GCells over capacity', ver)
            o['over_share'] = over / r['tiles'] if over is not None else None
            cost, of = num(r' cost=([\d.]+)', summ), num(r'of_cost=([\d.]+)', summ)
            o['of_share'] = of / cost if cost and of is not None else None
            o['of_knet'] = of / r['nets_all'] * 1000 if of is not None else None
            o['score_knet'] = cost / r['nets_all'] * 1000 if cost else None
            t = num(r'real_s=([\d.]+)', summ)
            o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
            o['source'] = os.path.basename(base)
        else:
            d = r25.get((s, n))
            if d:
                which = s.replace('ispd25', '')
                unit_len, pitch = cap_header(os.path.join(BENCH, 'ispd25', 'ISPD2025_benchmarks', which, n, n + '.cap'))
                steps = routed_steps(os.path.expanduser(f'~/Code/GlobalRouterRuns/p130/{which}.{n}.route'), pitch)
                o['detour'] = steps / rsmt_total if steps and rsmt_total else None
                o['of_share'] = d['of_cost'] / d['cost'] if d.get('cost') else None
                o['of_knet'] = d['of_cost'] / r['nets_all'] * 1000 if d.get('of_cost') is not None else None
                o['score_knet'] = d['cost'] / r['nets_all'] * 1000 if d.get('cost') else None
                t = num(r'^real ([\d.]+)$', read_text(os.path.expanduser(f'~/Code/GlobalRouterRuns/p130/{which}.{n}.err')))
                o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
                o['opens'] = d.get('opens')
                o['source'] = 'p130 route25'
        r['outcome'] = o
    json.dump(rows, open(sys.argv[3], 'w'), indent=1)
    print(f'{len(rows)} designs -> {sys.argv[3]}')


if __name__ == '__main__':
    main()
