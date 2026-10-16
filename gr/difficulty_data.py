#!/usr/bin/env python3
"""difficulty_data.py -- join the routability predictors (routability.cpp, one line per design) with the
routing outcomes recorded for each design, into one JSON file for the difficulty explorer page
(HISTORY_COST_SWEEP.md Part 130). Standard library only.

  python3 gr/difficulty_data.py FEATURES.txt ROUTE25.txt OUT.json [NETTIMES.txt]

Outcomes, per design (None where a set has no such measure):
  detour        routed 2D wire length (GCell steps) / total Steiner tree length of every net -- comparable
                across the three contests
  runtime_knet  router wall time per 1000 nets, seconds (each contest's own standard settings)
  runtime_mstep router wall time per million GCell steps of Steiner tree length (all nets), seconds
  over_share    ISPD 2008: overflowed edges / all 2D edges; ISPD 2024: GCells over capacity / all GCells
  overflow      ISPD 2008 total overflow (the contest's first ranking key)
  of_share      ISPD 2024 / 2025: the overflow term's share of the contest score (falls as wire length grows, so
                it tracks density as much as congestion)
  of_knet       ISPD 2024 / 2025: the overflow term per 1000 nets -- the cleaner congestion outcome
  score_knet    ISPD 2024 / 2025: contest score per 1000 nets
  nt_*          (with NETTIMES.txt, Part 131) per-net routing time: mean, coefficient of variation, percentiles,
                slowest 1%'s share, time per Steiner step (mean, CV), log time-length correlation, stage shares
                (CPU time per thread since Part 133; Part 131's elapsed-time numbers were wrong)
  cpu_knet, cpu_mstep  (Part 133) CPU time per 1000 nets / per million Steiner steps -- unlike wall time it
                does not count time asleep or waiting for a core
  of_gcell      (Part 133) ISPD 2024 / 2025: the overflow term per GCell (per-net normalization made dense
                designs, with many short nets, look uncongested)
  wns, tns_ep, power  (Part 133) ISPD 2025: timing and power of the unbiased route (the contest's OpenROAD),
                TNS per contest endpoint (registers + outputs)
  loop_gain     (Part 134) ISPD 2025: the timing loop's score improvement over the unbiased route
Sources: ISPD 2008 -- Part 133's re-measurement runs (pd39route, the 2008 standard @fast08, routes
byte-identical to Part 124's); ISPD 2024 -- Part 123's pd39route runs; ISPD 2025 -- ROUTE25.txt (pd39route,
the 2024 standard, scored by the contest evaluator), Part 133's OpenROAD scores, Part 134's timing loop.
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


def cpu_of(path):
    """user + system seconds from a /usr/bin/time -l (.time) or time -p (.err) output."""
    t = read_text(path)
    m = re.search(r'([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys', t)
    if m:
        return float(m.group(2)) + float(m.group(3))
    u, sy = num(r'^user ([\d.]+)$', t), num(r'^sys ([\d.]+)$', t)
    return u + sy if u is not None and sy is not None else None


P133 = os.path.expanduser('~/Code/GlobalRouterRuns/p133')
P134 = os.path.expanduser('~/Code/GlobalRouterRuns/p134')


def sta_of(s, n):
    """The unbiased route's OpenROAD scores (Part 133) and the contest endpoint count (Part 134's loop)."""
    which = s.replace('ispd25', '')
    cands = [os.path.join(P133, 's25', f'{which}.{n}.base.sta')] + (
        [os.path.join(P133, 'd3', 'base.sta' if n == 'ariane' else f'{n}.base.sta')] if which == 'visible' else [])
    t = next((read_text(c) for c in cands if os.path.exists(c)), '')
    loop = read_text(os.path.join(P134, f'bt.{which}.{n}', 'loop.tsv')) or read_text(os.path.join(P134, f'{which}.{n}', 'loop.tsv'))
    ep = num(r'endpoints (\d+)', loop)
    gain = num(r'^# best \S+ delta ([-+\d.]+)', loop)
    wns, tns = num(r'^wns (\S+)', t), num(r'^tns (\S+)', t)
    pw = re.search(r'^Total\s+\S+\s+\S+\s+\S+\s+(\S+)', t, re.M)
    return dict(wns=wns, tns_ep=tns / ep if tns is not None and ep else None, power=float(pw.group(1)) if pw else None,
                loop_gain=-gain if gain is not None else None)


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
    nt = {}
    if len(sys.argv) > 4 and os.path.exists(sys.argv[4]):
        for line in open(sys.argv[4]):
            if line.startswith('set='):
                d = kv(line)
                nt[(d['set'], d['design'])] = d
    for r in rows:
        s, n = r['set'], r['design']
        o = dict(detour=None, runtime_knet=None, runtime_mstep=None, over_share=None, overflow=None, of_share=None, of_knet=None,
                 score_knet=None, cpu_knet=None, cpu_mstep=None, of_gcell=None, wns=None, tns_ep=None, power=None, loop_gain=None)
        rsmt_total = r['rsmt_total']
        if s == 'ispd08':
            base = os.path.join(RUNS, 'p133nt', f'p133nt_{n}')
            v, summ = read_text(base + '.verify'), read_text(base + '.summary')
            cpu = cpu_of(base + '.time')
            o['cpu_knet'] = cpu / r['nets_all'] * 1000 if cpu else None
            o['cpu_mstep'] = cpu / rsmt_total * 1e6 if cpu and rsmt_total else None
            wl = num(r'^\s*wirelength\s+(\d+)', v)
            edges = (num(r'^\s*h edges\s+(\d+)', v) or 0) + (num(r'^\s*v edges\s+(\d+)', v) or 0)
            o['detour'] = wl / rsmt_total if wl and rsmt_total else None
            o['overflow'] = num(r'^\s*total overflow\s+(\d+)', v)
            oe = num(r'^\s*overflowed edges\s+(\d+)', v)
            o['over_share'] = oe / edges if oe is not None and edges else None
            t = num(r'real_s=([\d.]+)', summ)
            o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
            o['runtime_mstep'] = t / rsmt_total * 1e6 if t and rsmt_total else None
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
            o['of_gcell'] = of / r['tiles'] if of is not None else None
            o['score_knet'] = cost / r['nets_all'] * 1000 if cost else None
            cpu = cpu_of(base + '.time')
            o['cpu_knet'] = cpu / r['nets_all'] * 1000 if cpu else None
            o['cpu_mstep'] = cpu / rsmt_total * 1e6 if cpu and rsmt_total else None
            t = num(r'real_s=([\d.]+)', summ)
            o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
            o['runtime_mstep'] = t / rsmt_total * 1e6 if t and rsmt_total else None
            o['source'] = os.path.basename(base)
        else:
            d = r25.get((s, n))
            if d:
                which = s.replace('ispd25', '')
                unit_len, pitch = cap_header(os.path.join(BENCH, 'ispd25', 'ISPD2025_benchmarks', which, n, n + '.cap'))
                rp = os.path.expanduser(f'~/Code/GlobalRouterRuns/p130/{which}.{n}.route')
                steps = routed_steps(rp if os.path.exists(rp) else rp + '.xz', pitch)  # archive_compact.py xz's them
                o['detour'] = steps / rsmt_total if steps and rsmt_total else None
                o['of_share'] = d['of_cost'] / d['cost'] if d.get('cost') else None
                o['of_knet'] = d['of_cost'] / r['nets_all'] * 1000 if d.get('of_cost') is not None else None
                o['of_gcell'] = d['of_cost'] / r['tiles'] if d.get('of_cost') is not None else None
                err = os.path.expanduser(f'~/Code/GlobalRouterRuns/p130/{which}.{n}.err')
                cpu = cpu_of(err)
                o['cpu_knet'] = cpu / r['nets_all'] * 1000 if cpu else None
                o['cpu_mstep'] = cpu / rsmt_total * 1e6 if cpu and rsmt_total else None
                o.update(sta_of(s, n))
                o['score_knet'] = d['cost'] / r['nets_all'] * 1000 if d.get('cost') else None
                t = num(r'^real ([\d.]+)$', read_text(os.path.expanduser(f'~/Code/GlobalRouterRuns/p130/{which}.{n}.err')))
                o['runtime_knet'] = t / r['nets_all'] * 1000 if t else None
                o['runtime_mstep'] = t / rsmt_total * 1e6 if t and rsmt_total else None
                o['opens'] = d.get('opens')
                o['source'] = 'p130 route25'
        if (s, n) in nt:
            o.update({k: v for k, v in nt[(s, n)].items() if k.startswith('nt_')})
        r['outcome'] = o
    json.dump(rows, open(sys.argv[3], 'w'), indent=1)
    print(f'{len(rows)} designs -> {sys.argv[3]}')


if __name__ == '__main__':
    main()
