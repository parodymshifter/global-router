#!/usr/bin/env python3
"""instruments_report.py -- turn an Apple Instruments trace of a router run into a per-stage report
(HISTORY_COST_SWEEP.md Part 126).

Reads the Time Profiler samples and the router's stage markers (gr/signpost.h, GR_SIGNPOSTS=1) through
`xctrace export`, assigns every sample to the stage it fell in, and prints, as Markdown:
  - per stage: wall time, CPU time, average busy threads (CPU / wall), the share on performance cores,
    and the rounds seen (slow tier, region stage);
  - per stage and for the whole run: the functions with the most self time (where the CPU actually
    was), and the router functions with the most inclusive time (what they and their callees cost).

  python3 gr/instruments_report.py run.trace [--top N] [--process NAME] > run.report.md

Samples are 1 ms Time Profiler ticks per running thread, so CPU time is the count of running samples
times their weight; "busy threads" is that CPU time over the stage's wall time.
"""
import argparse
import bisect
import re
import collections
import subprocess
import sys
import xml.etree.ElementTree as ET


def export(trace, schema):
    xp = f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'
    out = subprocess.run(['xcrun', 'xctrace', 'export', '--input', trace, '--xpath', xp],
                         capture_output=True, check=True)
    return out.stdout


BY_ID = {}


def rows(xml_bytes):
    """Yield each <row> as a list of resolved child elements (xctrace writes a value once with an id
    and later as ref="id"; nested elements such as backtrace frames do the same)."""
    BY_ID.clear()
    root = ET.fromstring(xml_bytes)

    def index(e):
        i = e.get('id')
        if i is not None:
            BY_ID[i] = e
        for c in e:
            index(c)

    for node in root.iter('node'):
        for row in node.iter('row'):
            index(row)
            yield [resolve(c) for c in row]


def resolve(e):
    r = e.get('ref')
    return BY_ID[r] if r is not None else e


def short(name):
    """A readable C++ name: no std::__1::, no ABI tags, argument lists shown as (...)."""
    name = name.replace('std::__1::', 'std::')
    name = re.sub(r'\[abi:[^\]]*\]', '', name)
    out, depth_angle, i = [], 0, 0
    while i < len(name):
        c = name[i]
        if c == '<':
            depth_angle += 1
        elif c == '>':
            depth_angle = max(0, depth_angle - 1)
        if c == '(' and depth_angle == 0 and not name.startswith('()', i) and not name.startswith('(anonymous', i):
            depth, j = 0, i
            while j < len(name):
                if name[j] == '(':
                    depth += 1
                elif name[j] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            out.append('(...)' if j > i + 1 else '()')
            i = j + 1
            continue
        out.append(c)
        i += 1
    s = ''.join(out)
    return s if len(s) <= 100 else s[:97] + '...'


def text_int(e):
    try:
        return int(e.text)
    except (TypeError, ValueError):
        return 0


def frames(bt):
    out = []
    for f in bt:
        f = resolve(f)
        if f.tag != 'frame':
            continue
        name = f.get('name') or f.get('addr') or '?'
        b = f.find('binary')
        if b is not None:
            b = resolve(b)
        out.append((name, b.get('name') if b is not None and b.get('name') else ''))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--top', type=int, default=8)
    ap.add_argument('--process', default='', help='process name prefix (default: the busiest process)')
    a = ap.parse_args()

    # Samples: (time ns, thread, P core?, weight ns, frames leaf first, process)
    samples = []
    procs = collections.Counter()
    for r in rows(export(a.trace, 'time-profile')):
        t = text_int(r[0])
        thread = r[1].get('fmt', '')
        proc = r[2].get('fmt', '')
        core = r[3].get('fmt', '')
        state = r[4].get('fmt', '')
        w = text_int(r[5])
        bt = r[6] if len(r) > 6 else None
        if state != 'Running' or bt is None:
            continue
        fr = frames(bt)
        if not fr:  # no backtrace recorded for this tick
            fr = [('(no backtrace)', '')]
        samples.append((t, thread, 'P Core' in core, w, fr, proc))
        procs[proc] += w
    if not samples:
        sys.exit('no running samples in ' + a.trace)
    proc = next((p for p in procs if p.startswith(a.process)), None) if a.process else procs.most_common(1)[0][0]
    samples = [s for s in samples if s[5] == proc]
    samples.sort()
    binary = proc.split(' (')[0]
    t0, t1 = samples[0][0], samples[-1][0]

    # Stage intervals from the markers: Begin/End pairs by identifier; the End carries the name.
    begins, stages, events, seen_marks = {}, [], [], set()
    try:
        sp = export(a.trace, 'os-signpost')
        for r in rows(sp):
            cols = {c.tag: c for c in r}
            name = cols.get('signpost-name')
            if name is None:
                continue
            kind = cols['event-type'].get('fmt', '')
            t = text_int(cols['event-time'])
            ident = cols['os-signpost-identifier'].get('fmt', '') if 'os-signpost-identifier' in cols else ''
            msg = cols['os-log-metadata'].get('fmt', '') if 'os-log-metadata' in cols else ''
            mark = (t, ident, kind, name.get('fmt'), msg)
            if mark in seen_marks:  # the export lists each marker more than once
                continue
            seen_marks.add(mark)
            if name.get('fmt') == 'stage':
                if kind == 'Begin':
                    begins[ident] = t
                elif kind == 'End':
                    stages.append([begins.pop(ident, None), t, msg])
            elif name.get('fmt') == 'round':
                events.append((t, msg.rsplit(' ', 1)[0] if ' ' in msg else msg))
    except subprocess.CalledProcessError:
        pass
    stages.sort(key=lambda s: s[1])
    prev = t0
    for s in stages:
        if s[0] is None:  # began before the recording caught markers: from the previous end
            s[0] = prev
        prev = s[1]
    if not stages:
        stages = [[t0, t1, '(whole run; no stage markers -- set GR_SIGNPOSTS=1)']]
    elif t1 > stages[-1][1]:
        stages.append([stages[-1][1], t1, '(after the last marker)'])

    ends = [s[1] for s in stages]
    per = collections.defaultdict(lambda: {'cpu': 0, 'p': 0, 'self': collections.Counter(),
                                          'incl': collections.Counter(), 'threads': set()})
    total = per['__all__']
    for t, thread, pcore, w, fr, _ in samples:
        k = min(bisect.bisect_left(ends, t), len(stages) - 1)
        for d in (per[k], total):
            d['cpu'] += w
            d['p'] += w if pcore else 0
            d['threads'].add(thread)
            leaf, lb = fr[0]
            d['self'][short(leaf) if lb == binary else f'{short(leaf)}  [{lb}]'] += w
            seen = set()
            for name, b in fr:
                n = short(name)
                if b == binary and n not in seen:
                    seen.add(n)
                    d['incl'][n] += w
    # CPU Counters template: Apple's top-down split of cycles per 1 ms slice -- useful (retiring),
    # processing (back-end stalls, where memory waits show), delivery (front-end), discarded (bad
    # speculation). Absent from Time Profiler traces.
    topdown = collections.defaultdict(collections.Counter)
    try:
        for r in rows(export(a.trace, 'MetricAggregationForProcess')):
            # columns by position: start time, duration, process, int value, decimal value, metric name
            if len(r) < 6 or r[2].get('fmt') != proc:
                continue
            name = r[5].get('fmt', '')
            if name not in ('useful', 'processing', 'delivery', 'discarded'):
                continue
            try:
                v = float(r[4].text)
            except (TypeError, ValueError):
                continue
            t = text_int(r[0])
            topdown[min(bisect.bisect_left(ends, t), len(stages) - 1)][name] += v
            topdown['__all__'][name] += v
    except subprocess.CalledProcessError:
        pass
    rounds = collections.defaultdict(collections.Counter)
    for t, what in events:
        rounds[min(bisect.bisect_left(ends, t), len(stages) - 1)][what] += 1

    def ms(ns):
        return ns / 1e9

    print(f'# Instruments report: {a.trace}\n')
    print(f'Process `{proc}`, {len(samples)} running samples, {ms(t1 - t0):.1f} s sampled.\n')
    print('| stage | wall s | CPU s | busy threads | P-core share | threads seen | rounds |')
    print('|---|---|---|---|---|---|---|')
    for k, (b, e, name) in enumerate(stages):
        d = per[k]
        wall = max(e - b, 1)
        rd = ', '.join(f'{n} {w}' for w, n in rounds[k].items()) or '-'
        share = f'{100 * d["p"] / d["cpu"]:.0f}%' if d['cpu'] else '-'
        print(f'| {name} | {ms(wall):.2f} | {ms(d["cpu"]):.2f} | {d["cpu"] / wall:.2f} | {share} | '
              f'{len(d["threads"])} | {rd} |')
    allwall = max(t1 - t0, 1)
    print(f'| **total** | {ms(allwall):.2f} | {ms(total["cpu"]):.2f} | {total["cpu"] / allwall:.2f} | '
          f'{100 * total["p"] / max(total["cpu"], 1):.0f}% | {len(total["threads"])} | |\n')

    def table(title, counter, cpu):
        print(f'**{title}**\n')
        print('| function | s | share |')
        print('|---|---|---|')
        for name, w in counter.most_common(a.top):
            print(f'| `{name}` | {ms(w):.2f} | {100 * w / max(cpu, 1):.1f}% |')
        print()

    if topdown:
        print('**Where the cycles went** (CPU Counters, top-down; "processing" is back-end stalls,')
        print('where waiting on memory shows up)\n')
        print('| stage | useful | processing (back end) | delivery (front end) | discarded |')
        print('|---|---|---|---|---|')
        keys = [(k, stages[k][2]) for k in range(len(stages))] + [('__all__', '**total**')]
        for k, name in keys:
            c = topdown.get(k)
            tot = sum(c.values()) if c else 0
            if not tot:
                continue
            print(f'| {name} | ' + ' | '.join(f'{100 * c[m] / tot:.0f}%' for m in
                                             ('useful', 'processing', 'delivery', 'discarded')) + ' |')
        print()
    print('## Whole run\n')
    table('Self time (where the CPU was)', total['self'], total['cpu'])
    table('Inclusive time, router functions', total['incl'], total['cpu'])
    print('## By stage\n')
    for k, (b, e, name) in enumerate(stages):
        d = per[k]
        if d['cpu'] < 0.02 * total['cpu']:
            continue
        print(f'### {name} ({ms(e - b):.2f} s wall, {ms(d["cpu"]):.2f} s CPU)\n')
        table('Self time', d['self'], d['cpu'])
        table('Inclusive, router functions', d['incl'], d['cpu'])


if __name__ == '__main__':
    main()
