#!/usr/bin/env python3
"""elmore_rows.py -- per-net layer-bias rows from an Elmore delay model, for pd39route's PD39ROUTE_TIMING_ROWS
(HISTORY_COST_SWEEP.md Part 215). Standard library only.

  python3 gr/elmore_rows.py DESIGN_DIR SLACK_FILE|none MODE OUT.rows K KP [LIBERTY] [--weights w1,w2,w3]  |  --selftest

The timing bias of the 2025 loop (Part 133) pulls a classed net's wire toward low-resistance layers, per step:
k[class] x R_l / max R. A delay model says otherwise: one micron of wire on layer l adds about
    d_l = R_drv x C_l + R_l x C_down          (kOhm x fF/um = ps/um)
to the net's delay, R_drv the driver's resistance and C_down the capacitance below the wire. With NanGate45's values
the driver term dominates, so metal4-8 (the highest C) are the worst layers for delay and metal2 beats them, where the
resistance-only bias ranks metal7-8 next to metal9-10. On blind mempool_tile_wrap these rows took the contest score
from 0.4879 to 0.1768 at K 70 (1st place 0.3576); on blind ariane 1.4605 -> 1.4368 at K 35 (Part 215).

Classes are the router's own (TimingLayerModel::classify): a net's slack -- the file's (MODE replace), the input pin
slack lowered by the file's (MODE min), or the input pin slack alone (SLACK_FILE none) -- over the worst; >= 0.7, 0.4,
0.1 -> class 3, 2, 1, weights 1, 1/2, 1/4 (the loop's k3, k3/2, k3/4). For each classed net:
    row_l = K x weight x d_l + KP x C_l / max C
(the second term is the loop's power term, kept as the class rows have it). R_drv: the driver pin's drive resistance,
the slope of the Liberty cell_rise / cell_fall tables against load (the middle transition row, averaged over the
pin's arcs); a driver the library does not describe (a port, a macro) takes the median of the classed nets'. C_down:
the sink pins' input capacitance (1.5 fF for a pin the library does not list) x (1 for one sink, 1/2 for more) plus
half the wire's (the pins' half-perimeter x the mean C of metal2-10). R_l, C_l: standard25.env's PD39ROUTE_LAYER_R /
_LAYER_C (NanGate45's setRC.tcl). A net the file does not class is not listed (its class-0 row, the power term, stays).
"""
import os
import re
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LIBERTY = os.path.join(HERE, 'bench', 'ispd25', 'ISPD2025_benchmarks', 'NanGate45', 'lib',
                       'NangateOpenCellLibrary_typical.lib')
WEIGHT = {1: 0.25, 2: 0.5, 3: 1.0}


def layer_rc(env_path=os.path.join(HERE, 'standard25.env')):
    vals = {}
    for line in open(env_path):
        if '=' in line and not line.startswith('#'):
            k, v = line.rstrip('\n').split('=', 1)
            vals[k] = v
    return ([float(x) for x in vals['PD39ROUTE_LAYER_R'].split(',')],
            [float(x) for x in vals['PD39ROUTE_LAYER_C'].split(',')])


def liberty(path):
    """({cell: {output pin: kOhm}}, {cell: {input pin: fF}}) -- drive resistance from the delay tables' slope against
    load (ns per fF x 1000 = kOhm; NanGate45: time 1 ns, capacitance 1 fF), input pin capacitance."""
    acc, caps = {}, {}
    cell = pin = table = direction = None
    idx2, rows = None, []
    for line in open(path):
        s = line.strip()
        m = re.match(r'cell \((\w+)\)', s)
        if m:
            cell, pin = m.group(1), None
            continue
        m = re.match(r'pin \((\w+)\)', s)
        if m:
            pin, direction = m.group(1), None
            continue
        m = re.match(r'direction\s*:\s*(\w+)', s)
        if m:
            direction = m.group(1)
            continue
        m = re.match(r'capacitance\s*:\s*([\d.eE+-]+)', s)
        if m and cell and pin and direction == 'input':
            caps.setdefault(cell, {})[pin] = float(m.group(1))
            continue
        m = re.match(r'(cell_rise|cell_fall)\s*\(', s)
        if m:
            table, idx2, rows = m.group(1), None, []
            continue
        if table:
            if s.startswith('index_2'):
                idx2 = [float(v) for v in re.search(r'"([^"]*)"', s).group(1).split(',')]
            elif s.startswith('values') or s.startswith('"'):
                for q in re.findall(r'"([^"]*)"', s):
                    rows.append([float(v) for v in q.split(',')])
            if s.endswith('}'):
                if idx2 and rows and len(rows[len(rows) // 2]) == len(idx2) and idx2[-1] > idx2[0]:
                    r = rows[len(rows) // 2]
                    acc.setdefault((cell, pin), []).append(1000.0 * (r[-1] - r[0]) / (idx2[-1] - idx2[0]))
                table = None
    drive = {}
    for (c, p), v in acc.items():
        drive.setdefault(c, {})[p] = sum(v) / len(v)
    return drive, caps


def components(def_path):
    """{instance: cell} from the DEF's COMPONENTS."""
    comp, on = {}, False
    for line in open(def_path, errors='replace'):
        if line.startswith('COMPONENTS'):
            on = True
        elif line.startswith('END COMPONENTS'):
            break
        elif on and line.startswith('- '):
            a = line.split()
            comp[a[1].replace('\\', '')] = a[2]
    return comp


def nets(net_path):
    """Yield (net index, [(pin name, slack, (x, y) of its first access point)]) in the .net's order."""
    i, pins = -1, None
    for line in open(net_path):
        s = line.strip()
        if s == '(':
            i += 1
            pins = []
        elif s == ')':
            if pins is not None:
                yield i, pins
            pins = None
        elif pins is not None and ',' in s and '[' in s:
            nm, rest = s.split(',', 1)
            sl = rest.split(',', 1)[0].strip()
            m = re.search(r'\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', rest)
            if m:
                pins.append((nm.strip().replace('\\', ''), float(sl), (int(m.group(2)), int(m.group(3)))))


def classes(slack):
    """The router's classes from per-net slack (TimingLayerModel::classify, reference: the worst)."""
    neg = [v for v in slack.values() if v < 0]
    if not neg:
        return {}
    worst = min(neg)
    out = {}
    for i, v in slack.items():
        if v < 0:
            r = min(1.0, v / worst)
            c = 3 if r >= 0.7 else 2 if r >= 0.4 else 1 if r >= 0.1 else 0
            if c:
                out[i] = c
    return out


def build(design_dir, slack_file, mode, out, K, kp, lib=LIBERTY, rc=None, weights=None):
    """Write OUT (one '<net index> <row>' line per classed net); return a one-line summary."""
    d = design_dir.rstrip('/')
    name = os.path.basename(d)
    R, C = rc or layer_rc()
    file_slack = {}
    if slack_file != 'none':
        for line in open(slack_file):
            if line[0] != '#' and line.strip():
                a = line.split()
                file_slack[int(a[0])] = float(a[1])
    allnets = dict(nets(os.path.join(d, name + '.net')))
    if slack_file == 'none' or mode == 'min':
        slack = {i: min(p[1] for p in pins) for i, pins in allnets.items() if pins}
        for i, v in file_slack.items():
            if i in slack and v < slack[i]:
                slack[i] = v
    elif mode == 'replace':
        slack = file_slack
    else:
        raise ValueError('mode must be replace or min')
    cls = classes(slack)
    drive, caps = liberty(lib)
    comp = components(os.path.join(d, name + '.def'))
    with open(os.path.join(d, name + '.cap')) as f:
        f.readline()
        f.readline()
        um = float(f.readline().split()[0]) / 2000.0  # GCell width in microns (DEF units 2000 per micron)
    cmean = statistics.mean(C[1:])
    rdrv, cdown = {}, {}
    for i in cls:
        pins = allnets.get(i)
        if not pins:
            continue
        inst, _, p = pins[0][0].rpartition('/')
        rdrv[i] = drive.get(comp.get(inst), {}).get(p)
        cs = 0.0
        for nm, _, _ in pins[1:]:
            inst, _, p = nm.rpartition('/')
            cs += caps.get(comp.get(inst), {}).get(p, 1.5)
        xs = [q[2][0] for q in pins]
        ys = [q[2][1] for q in pins]
        hp = (max(xs) - min(xs) + max(ys) - min(ys)) * um
        cdown[i] = cs * (1.0 if len(pins) == 2 else 0.5) + hp * cmean / 2.0
    known = [v for v in rdrv.values() if v is not None]
    rmed = statistics.median(known) if known else 2.0
    cmax = max(C)
    wt = WEIGHT if weights is None else {1: weights[0], 2: weights[1], 3: weights[2]}  # Part 217: --weights
    with open(out, 'w') as f:
        f.write(f'# elmore_rows.py: K {K}, kp {kp}, slack {slack_file} ({mode}); {len(cdown)} classed nets\n')
        for i in sorted(cdown):
            rd = rdrv[i] if rdrv[i] is not None else rmed
            row = [K * wt[cls[i]] * (rd * C[l] + R[l] * cdown[i]) + kp * C[l] / cmax for l in range(len(R))]
            f.write(f'{i} ' + ' '.join(f'{v:.6g}' for v in row) + '\n')
    return (f'{len(cdown)} classed nets (1/2/3: ' + '/'.join(str(sum(1 for c in cls.values() if c == k)) for k in (1, 2, 3))
            + f'), {sum(1 for v in rdrv.values() if v is None)} without a library driver (median {rmed:.3f} kOhm used)')


def selftest():
    """A hand-made 3-layer design: two nets, one critical (class 3) with a known driver and sink, one with positive
    slack (unlisted); the row matches the formula by hand."""
    import tempfile
    t = tempfile.mkdtemp()
    d = os.path.join(t, 'tiny')
    os.makedirs(d)
    lib = os.path.join(t, 'tiny.lib')
    open(lib, 'w').write('cell (BUF) {\n pin (A) {\n  direction : input;\n  capacitance : 2.0;\n }\n'
                         ' pin (Z) {\n  direction : output;\n  timing () {\n   cell_rise (t) {\n'
                         '    index_1 ("0.1, 0.2, 0.3");\n    index_2 ("0, 10");\n'
                         '    values ("0.01, 0.03", \\\n     "0.02, 0.05", \\\n     "0.03, 0.07");\n   }\n  }\n }\n}\n')
    open(os.path.join(d, 'tiny.def'), 'w').write('COMPONENTS 2 ;\n- u1 BUF ;\n- u2 BUF ;\nEND COMPONENTS\n')
    open(os.path.join(d, 'tiny.cap'), 'w').write('3 4 4\n0.5 4 5 5 5 -1 -10 100 0.000001\n4200 4200 4200\n4200 4200 4200\n')
    open(os.path.join(d, 'tiny.net'), 'w').write(
        'a\n(\nu1/Z, -2e-10, [(0, 0, 0)]\nu2/A, -2e-10, [(0, 3, 1)]\n)\nb\n(\nu2/Z, 1e-10, [(0, 1, 1)]\nu1/A, 1e-10, [(0, 2, 1)]\n)\n')
    out = os.path.join(t, 'tiny.rows')
    R, C = [3.0, 2.0, 1.0], [0.1, 0.2, 0.05]
    build(d, 'none', 'min', out, 10.0, 1.0, lib, (R, C))
    rows = [l.split() for l in open(out) if not l.startswith('#')]
    rd = 1000.0 * (0.05 - 0.02) / 10.0           # the middle row's slope: 3 kOhm
    cdown = 2.0 * 1.0 + (3 + 1) * 2.1 * statistics.mean(C[1:]) / 2.0
    want = [10.0 * 1.0 * (rd * C[l] + R[l] * cdown) + 1.0 * C[l] / 0.2 for l in range(3)]
    ok = len(rows) == 1 and rows[0][0] == '0' and all(abs(float(rows[0][1 + l]) - want[l]) < 1e-3 * want[l] for l in range(3))
    ok &= classes({0: -1.0, 1: -0.5, 2: -0.2, 3: -0.05, 4: 0.3}) == {0: 3, 1: 2, 2: 1}
    # Part 217: --weights scales the timing term by class (class 3 here): weight 0.5 halves it, the power term stays
    build(d, 'none', 'min', out, 10.0, 1.0, lib, (R, C), (0.1, 0.2, 0.5))
    rows2 = [l.split() for l in open(out) if not l.startswith('#')]
    want2 = [10.0 * 0.5 * (rd * C[l] + R[l] * cdown) + 1.0 * C[l] / 0.2 for l in range(3)]
    ok &= len(rows2) == 1 and all(abs(float(rows2[0][1 + l]) - want2[l]) < 1e-3 * want2[l] for l in range(3))
    print('elmore_rows selftest:', 'pass' if ok else f'FAIL {rows} vs {want}')
    return ok


if __name__ == '__main__':
    if '--selftest' in sys.argv:
        sys.exit(0 if selftest() else 1)
    # Part 216 (the user, 2026-11-24): every argument and input checked first, every problem reported
    import flowcheck
    o, a, p = flowcheck.parse_options(sys.argv[1:], {'--weights': 'value'}, None, 'elmore_rows.py')
    wts = None
    if '--weights' in o:  # Part 217: class 1, 2, 3 weights (default 0.25, 0.5, 1)
        try:
            wts = tuple(float(x) for x in o['--weights'].split(','))
            if len(wts) != 3 or any(not (x == x and abs(x) != float('inf')) or x < 0 for x in wts) or not wts[2] > 0:
                raise ValueError
        except ValueError:
            p.add(f"--weights takes three numbers w1,w2,w3 >= 0 with w3 > 0 (got '{o['--weights']}')")
    if len(a) not in (6, 7):
        p.add(f'{len(a)} arguments given; 6 or 7 expected')
    else:
        d = a[0].rstrip('/')
        if p.need_dir(d, 'DESIGN_DIR'):
            for ext in ('.def', '.net', '.cap'):
                p.need_file(os.path.join(d, os.path.basename(d) + ext), f"the design's {ext} file")
        if a[1] != 'none':
            p.need_file(a[1], 'SLACK_FILE')
        if a[2] not in ('replace', 'min'):
            p.add(f"MODE must be replace or min (got '{a[2]}')")
        p.need_output(a[3], 'OUT.rows')
        for v, what, lo in ((a[4], 'K', 0.0), (a[5], 'KP', None)):
            try:
                x = float(v)
                if not (x == x and abs(x) != float('inf')) or (lo is not None and not x > lo) or x < 0:
                    raise ValueError
            except ValueError:
                p.add(f"{what} must be a number {'> 0' if lo is not None else '>= 0'} (got '{v}')")
        p.need_file(a[6] if len(a) > 6 else LIBERTY, 'LIBERTY')
        p.need_file(os.path.join(HERE, 'standard25.env'), 'standard25.env (the layer R and C)')
    p.stop_if_any('usage:\n  python3 gr/elmore_rows.py DESIGN_DIR SLACK_FILE|none MODE OUT.rows K KP [LIBERTY] [--weights w1,w2,w3]  |  '
                    '--selftest')
    print(build(a[0], a[1], a[2], a[3], float(a[4]), float(a[5]), a[6] if len(a) > 6 else LIBERTY, None, wts))
