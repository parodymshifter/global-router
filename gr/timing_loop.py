#!/usr/bin/env python3
"""timing_loop.py -- ISPD 2025 with the contest's timing engine in the loop (HISTORY_COST_SWEEP.md Part 134).

The timing bias's best strength differs by design in a way neither the score weights nor wire geometry
predicts (Part 133: weight rules sum -14.1 / -14.4 over ten designs, a geometric estimate leave-one-out
-9.9, the best per design -17.0). So measure it: route the design at several strengths, score every
candidate with the contest's own OpenROAD (timing, power -- one session for all of them) and congestion
evaluator, and keep the best by the contest score's change against the unbiased route.

  python3 gr/timing_loop.py DESIGN_DIR WORK_DIR [--out OUT.route] [--classes both|routed|input]
      [--refine N] [--reuse EARLIER_WORK_DIR] [--router PATH] [--lean] [--resume] [--k3 a,b,c] [--elmore K1,K2,...|0]
      [--endpoints N|measured] [--patience P] [--two-sessions] [--class-weights w1,w2,w3] | --selftest

The ISPD 2025 standard flow (2026-10-16). Without OpenROAD, `@standard25.env` is the one-route fallback.

DESIGN_DIR holds <name>.cap/.net/.def/.sdc (the name is the folder's). Stage 1: unbiased plus top-class
timing strengths k3 in K3_LADDER (classes k3/4, k3/2, k3; power off; absolute, not weight-scaled).
Stage 0 (Part 134): the unbiased route is scored and OpenROAD measures its slack on every failing
endpoint's worst path. Stage 1 runs the ladder twice: classes from that routed slack (PD39ROUTE_TIMING_SLACK,
replace mode: other nets unclassed; tag suffix r) and from the input pin slack (suffix i) -- neither wins
everywhere (routed: visible ariane -3.31 vs -2.27; input: blind mempool_tile_wrap -1.15 vs -0.21, whose
unbiased route seemed to fail on only 665 nets -- Part 215: a slack-script bug dropped 81% of its failing paths;
fixed, 9263 nets, and routed classes win there too). `--classes routed|input` runs one. Stage 2: at the best, the power
term at KP_LADDER with the same classes. Stage 3 (Part 188, default since 2026-11-06; `--refine N`, 0 = off,
default 2, 12 since 2026-11-20, 24 then 48 since 2026-11-24, Parts 215-216): sticky re-classification -- the best route's own failing-path
slack merged with every slack measured before (per net, the worst), and a re-route at the best's setting, while it gains (ten
designs -23.97 -> -25.11, none worse). Since 2026-11-24 (`--patience P`, default 3; 0 = the old rule) a round without gain
does not stop it until P + 1 rounds in a row have none, and each round's route is scored by the session that measures its
slack (`--two-sessions`: a scoring session, then a slack session, as before). Every candidate is logged to WORK_DIR/loop.tsv.
`--lean` (Part 189, for designs whose routes are ~11 GB each, mempool_cluster): every route that can no
longer be the best is compressed (xz) once scored, and stage 1 is scored one class source at a time, so at
most four routes sit uncompressed (the scores are the same: each route is scored on its own in a session).
Every score is also written to WORK_DIR/scores.jsonl as soon as it is measured (Part 189: a guard kill 2 h 31 min
into a session lost a whole day's scores, which lived only in memory). `--resume` restarts a stopped loop in
the same WORK_DIR: a route that was compressed or evaluated before is not routed again, a candidate already
scored is not scored again, and a slack file already written is not measured again -- the rest runs as
before, so the result is the same as an uninterrupted run's.
Stage 4, the Elmore stage (Part 215; default since 2026-11-21, `--elmore K1,K2,...` to change the strengths, `--elmore
0` = off): after refinement, the best rerouted with per-net Elmore-delay rows (`gr/elmore_rows.py`,
`PD39ROUTE_TIMING_ROWS`) at each strength K in ELMORE_LADDER (35, 70, 140: the best of blind ariane, mempool_tile_wrap
and mempool_group; since 2026-11-22 also 280 and 560 when the best's timing strength is 540, unless --elmore is given), from the classes it was routed with, all scored in one session (`--lean`: one at a time); the best
kept as before (blind mempool_tile_wrap 0.4879 -> 0.1768 at K 70, ariane 1.4605 -> 1.4368, mempool_group 1.2959 ->
1.0946 at K 140, contest score). `--lean`: every other route stays compressed while a candidate is scored.
`--reuse EARLIER_WORK_DIR` (Part 188) refines an earlier loop's best instead of rerunning stages 0-2, then runs the
Elmore stage from the class file that best was routed with; `--reuse ... --refine 0` (Part 215) runs only the Elmore
stage (before 2026-11-21 it still ran one refinement round).
Endpoint count (Part 215; default since 2026-11-21): candidates are ranked with the contest's published N_endpoint
for the blind designs (CONTEST_ENDPOINTS, a DESIGN_DIR inside a folder named `blind`) and with the count OpenROAD
reports (registers + outputs) otherwise; they differ on the mempool designs (tile 13350 vs 19000), and twice that
changed the pick. `--endpoints N` gives the count; `--endpoints measured` uses OpenROAD's everywhere.
Router runs wait for a free slot (at most 3 tracked runs machine-wide, CLAUDE.md) and run under
/usr/bin/time -l. Standard library only.
"""
import filecmp, json, os, re, shutil, subprocess, sys, time

import flowcheck  # Part 216: every problem reported before any work

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
# Part 215 (the user, 2026-11-21: standard): the Elmore stage's strengths -- the best K was 35 on blind ariane, 70 on
# blind mempool_tile_wrap, 140 on blind mempool_group (bsg_chip and NVDLA indifferent)
ELMORE_LADDER = [35, 70, 140]
# Part 215 (the user, 2026-11-22: standard): a design whose best timing strength is the k3 ladder's top, 540, also tries
# these -- visible ariane -4.0584 -> -4.2695 and bsg_chip -2.0877 -> -2.2218, both at K 560 (1120 and 2240 worse on both);
# designs at 180 or below do not (blind mempool_group: K 140 1.0946, K 280 1.2214; blind bsg_chip indifferent)
ELMORE_HIGH, ELMORE_HIGH_K3 = [280, 560], 540
ELMORE_GIVEN = False  # --elmore K1,K2,... given: exactly those
# Part 215 (the user, 2026-11-23: test; 2026-11-24: standard): refinement's rounds without a new best before it stops
# (--patience P; 0: the first such round stops it, the standard until 2026-11-24). Visible mempool_tile_wrap's classic
# refinement stopped at m8 after m7 -1.1423; three more rounds were needed for a new best (m11 -1.1897). Nine designs at
# patience 3, cap 24: never worse; visible mempool_tile_wrap -1.4049 -> -1.6469, blind mempool_group 0.9689 -> 0.8855
PATIENCE = 3
REFINE_ROUNDS = 48  # --refine N (Part 188: 2; 2026-11-20: 12; 2026-11-24, with patience 3: 24 -- it bound on both gainers;
# 2026-11-24 15:33, the user: 48 -- visible mempool_tile_wrap gained to m32, -1.6469 -> -1.6738; patience stops it earlier
# wherever rounds stop gaining, so only designs still gaining at 24 run longer, Part 216)
# Part 215 (the user, 2026-11-24: standard): a refinement round's route is scored by the OpenROAD session that measures
# its slack (that session reads timing and power too), not by a scoring session first and a slack session in the next
# round -- the same measurements, one session per round instead of two (blind mempool_group: ~17 of every ~40 min).
# --two-sessions gives the old way back
ONE_SESSION = True
# Part 217 (the user, 2026-11-25: test method 7 of the 2026-11-24 review, per-class strength): the strength of timing
# classes 1, 2, 3 as fractions of the top class's (k3 x w, and the Elmore rows' K x w). --class-weights w1,w2,w3; the
# default is the standard's 1/4, 1/2, 1, whose candidates are routed exactly as before (the same settings strings)
CLASS_W_DEFAULT = (0.25, 0.5, 1.0)
CLASS_W = CLASS_W_DEFAULT
# Part 215 (the user, 2026-11-21: standard): the contest's N_endpoint for its blind designs (github.com/liangrj2014/
# ISPD25_contest index.md, read 2026-11-20). OpenROAD's registers + outputs agree on ariane and NVDLA and not on the
# mempool designs (19000 / 438269 / 1443997) or bsg_chip (214955). The visible designs' contest counts are not
# published there; for them the measured count is used (unverified whether it is the contest's on the mempool ones).
CONTEST_ENDPOINTS = {'ariane': 20218, 'bsg_chip': 214915, 'NV_NVDLA_partition_c': 45925, 'mempool_tile_wrap': 13350,
                     'mempool_group': 347869, 'mempool_cluster': 1082397}
KP_LADDER = [1, 4]
ROUTER = os.path.join(GR, 'pd39route')  # --router PATH to use another build
LEAN = False  # --lean (Part 189)
CKPT, RESUME, DONE, N_SAVED = None, False, {}, None  # scores.jsonl and --resume (Part 189)
SAVED = ('tag', 'k3', 'kp', 'src', 'of', 'opens', 'wns', 'tns', 'power', 'slack_nets', 'slack_unmatched', 'classed',
         'slack_skipped', 'slack_badpoints')
SCORED = ('wns', 'tns', 'power')


def save(c=None, n=None):
    """Append a candidate's measured fields (or the endpoint count) to scores.jsonl at once."""
    global N_SAVED
    if not CKPT:
        return
    rec = {'endpoints': n} if c is None else {k: c[k] for k in SAVED if k in c}
    with open(CKPT, 'a') as f:
        f.write(json.dumps(rec) + '\n')
    if c is None:
        N_SAVED = n
    else:
        DONE.setdefault(c['tag'], {}).update(rec)


def load(path):
    """scores.jsonl -> DONE (per tag, the latest value of every field) and N_SAVED."""
    global N_SAVED
    for line in open(path):
        if line.strip():
            rec = json.loads(line)
            if 'endpoints' in rec:
                N_SAVED = rec['endpoints']
            else:
                DONE.setdefault(rec['tag'], {}).update(rec)


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


def timing_layers(k3):
    """PD39ROUTE_TIMING_LAYERS for top-class strength k3: k3/4, k3/2, k3 as before, or k3 x CLASS_W (Part 217)."""
    if CLASS_W == CLASS_W_DEFAULT:
        return f'{k3 / 4},{k3 / 2},{k3}'
    return ','.join(repr(k3 * w) for w in CLASS_W)


def route(d, name, work, tag, k3, kp, slack_file=None, slack_mode='replace', extra=None):
    """One candidate: the 2024 standard plus the timing bias at top-class strength k3 and the power
    term kp; slack_file (the routed-slack file) replaces the input slack as the classes' source
    (slack_mode 'min': lowers the input slack where the file's is worse)."""
    base = {}
    for f in ('standard.env', 'standard24.env', 'fast24.env'):
        base.update(env_file(os.path.join(GR, f)))
    s25 = env_file(os.path.join(GR, 'standard25.env'))
    base['PD37ROUTE_THREADS'] = '8'
    if k3 > 0 or kp > 0:
        base['PD39ROUTE_TIMING_LAYERS'] = timing_layers(k3)
        base['PD39ROUTE_POWER_LAYERS'] = str(kp)
        base['PD39ROUTE_LAYER_R'] = s25['PD39ROUTE_LAYER_R']
        base['PD39ROUTE_LAYER_C'] = s25['PD39ROUTE_LAYER_C']
    if slack_file and (k3 > 0 or kp > 0):
        base['PD39ROUTE_TIMING_SLACK'] = slack_file
        base['PD39ROUTE_TIMING_SLACK_MODE'] = slack_mode
    base.update(extra or {})  # Part 215: e.g. PD39ROUTE_TIMING_ROWS (the Elmore stage)
    cls_src = {'sf': slack_file, 'smode': slack_mode}  # the classes this candidate was routed with (the Elmore stage)
    sol = os.path.join(work, f'{tag}.route')
    got = DONE.get(tag, {}) if RESUME else {}
    if 'of' in got and (os.path.exists(sol) or os.path.exists(sol + '.xz')):  # --resume: routed and evaluated
        print(f'{name}: resume: {tag} routed and evaluated before', flush=True)
        return {'tag': tag, 'k3': k3, 'kp': kp, 'route': sol, 'of': got['of'], 'opens': got['opens'], **cls_src}
    if RESUME and os.path.exists(sol + '.xz'):  # --resume: a route packed by --lean is complete; evaluate it
        print(f'{name}: resume: {tag} routed before, evaluating it', flush=True)
        with open(sol + '.tmp', 'wb') as g:
            subprocess.run(['xz', '-dc', sol + '.xz'], stdout=g, check=True)
        os.replace(sol + '.tmp', sol)
    else:
        wait_slot()
        with open(os.path.join(work, f'{tag}.err'), 'w') as err:
            subprocess.run(['/usr/bin/time', '-l', ROUTER, os.path.join(d, name + '.cap'),
                            os.path.join(d, name + '.net'), sol], env={**os.environ, **base}, stdout=subprocess.DEVNULL,
                           stderr=err, check=True)
    ev = subprocess.run([EVALUATOR, os.path.join(d, name + '.cap'), os.path.join(d, name + '.net'), sol],
                        capture_output=True, text=True).stdout
    m = re.search(r'^overflow cost ([\d.eE+-]+)', ev, re.M)
    opens = re.search(r'open nets : (\d+)', ev)
    c = {'tag': tag, 'k3': k3, 'kp': kp, 'route': sol, 'of': float(m.group(1)) if m else float('nan'),
         'opens': int(opens.group(1)) if opens else -1, **cls_src}
    save(c)
    return c


def pack(c):
    """--lean: compress a scored candidate's route (it can be unpacked again)."""
    if LEAN and os.path.exists(c['route']):
        subprocess.run(['xz', '-T8', '-2', '-f', c['route']], check=True)


def unpack(c):
    """Restore a packed route: from its own .xz, else (--reuse, Part 215) from the earlier loop's copy."""
    if not os.path.exists(c['route']):
        for xz in (c['route'] + '.xz', c.get('packed')):
            if xz and os.path.exists(xz):
                with open(c['route'] + '.tmp', 'wb') as g:
                    subprocess.run(['xz', '-dc', xz], stdout=g, check=True)
                os.replace(c['route'] + '.tmp', c['route'])
                break


def prune(cands, keep):
    """--lean: compress every scored candidate but `keep` (the best so far)."""
    for c in cands:
        if c is not keep:
            pack(c)


def sta(d, name, cands):
    """One OpenROAD session: timing and power of every candidate route, and the endpoint count. With
    --resume, candidates scored before take their saved numbers and only the rest are scored."""
    todo = [c for c in cands if not (RESUME and all(k in DONE.get(c['tag'], {}) for k in SCORED))]
    for c in cands:
        if all(c is not t for t in todo):
            c.update({k: DONE[c['tag']][k] for k in SCORED})
            print(f"{name}: resume: {c['tag']} scored before", flush=True)
    if not todo:
        return N_SAVED
    cands = todo
    for c in cands:
        unpack(c)
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
        save(c)
    n = int(re.search(r'ENDPOINTS (\d+)', out).group(1))
    save(n=n)
    return n


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
    if RESUME and os.path.exists(out) and os.path.exists(raw):  # measured before (written after the session)
        c['slack_nets'] = sum(1 for _ in open(out)) - 1
        c['slack_unmatched'] = sum(1 for _ in open(raw)) - c['slack_nets']
        got = DONE.get(c['tag'], {})
        if all(k in got for k in SCORED):  # otherwise the next scoring session scores c
            c.update({k: got[k] for k in SCORED})
        print(f"{name}: resume: {c['tag']} slack measured before", flush=True)
        save(c)
        return out
    unpack(c)  # --resume: the best of a stopped run may have been packed
    tcl = os.path.join(work, 'timing_slack.tcl')
    shutil.copyfile(SLACK_TCL, tcl)
    env = {**os.environ, 'DESIGN': name, 'DESIGN_DIR': os.path.dirname(d.rstrip('/')), 'ROUTE': c['route'],
           'SLACK_OUT': raw}
    txt = subprocess.run([OPENROAD, '-exit', tcl], cwd=EVAL_DIR, env=env, capture_output=True, text=True).stdout
    if 'SLACKNETS' not in txt:
        raise SystemExit('timing_loop.py: OpenROAD (slack) failed:\n' + txt[-2000:])
    c['slack_skipped'], c['slack_badpoints'] = slack_counts(txt)
    print(f"{name}: slack of {c['tag']}: {c['slack_skipped']} failing paths skipped, {c['slack_badpoints']} "
          f"unnamed points skipped", flush=True)
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
    save(c)
    return out


def slack_counts(txt):
    """Part 215: timing_slack.tcl's SKIPPEDPATHS / BADPOINTS (paths without a slack or points; points without a
    net name, skipped one by one). An older script printed only SKIPPEDPATHS: BADPOINTS is then None."""
    m = re.search(r'^SKIPPEDPATHS (\d+)(?: BADPOINTS (\d+))?', txt, re.M)
    if not m:
        raise SystemExit('timing_loop.py: OpenROAD (slack) printed no SKIPPEDPATHS line')
    return int(m.group(1)), (int(m.group(2)) if m.group(2) is not None else None)


N_ENDPOINTS = None  # Part 215: the contest's endpoint count, in place of the one OpenROAD reports (None: OpenROAD's)


def contest_endpoints(d):
    """Part 215: the contest's published N_endpoint for a blind design's folder (by its path as given or resolved), else
    None."""
    for p in (d, os.path.realpath(d)):
        p = p.rstrip('/')
        if os.path.basename(os.path.dirname(p)) == 'blind':
            return CONTEST_ENDPOINTS.get(os.path.basename(p))
    return None


def delta(c, b, w, n):
    """The contest score's change from the unbiased candidate (lower is better)."""
    n = N_ENDPOINTS or n
    return (w[0] * (c['wns'] - b['wns']) + w[1] * (c['tns'] - b['tns']) / n + w[2] * (c['power'] - b['power'])
            + w[3] * (c['of'] - b['of']))


def merge_slack(files, out):
    """Per net, the worst slack over several routed-slack files: a net that failed on any route measured
    so far stays classed (Part 188 -- re-classifying from one biased route alone dropped the nets its bias
    had fixed, Part 134)."""
    worst = {}
    for fn in files:
        for line in open(fn):
            if line.startswith('#') or not line.strip():
                continue
            i, v = line.split()
            worst[int(i)] = min(float(v), worst.get(int(i), float('inf')))
    with open(out, 'w') as g:
        g.write('# worst routed slack (seconds) over ' + ' '.join(os.path.basename(f) for f in files) + '\n')
        for i in sorted(worst):
            g.write(f'{i} {worst[i]:.6e}\n')
    return out


def tag_of(best, it):
    return f"k{best['k3']}{best['src']}" + (f"p{best['kp']}" if best['kp'] else '') + f'm{it}'


def remeasured(d, name, c, work, idx):
    """The slack of a candidate scored in an earlier session (refinement's first round, or every round with
    --two-sessions): the slack session's timing and power must agree with that session's, whose are kept."""
    keep = {k: c[k] for k in SCORED}
    out = sta_slack(d, name, c, work, idx)
    for k, v in keep.items():  # the same route: OpenROAD's numbers must agree with the scoring session
        # Part 215 (2026-11-23): power to 1e-4 relative -- visible mempool_tile_wrap's k180rp1m4 read 0.141794011 in
        # its slack session against 0.141788095 when scored, with TNS and WNS exact; the same route re-measured alone
        # gave 0.141788095 in two slack sessions and a scoring one. The scoring session's numbers are kept
        tol = 1e-4 * abs(v) if k == 'power' else 1e-6 * max(1.0, abs(v))
        if abs(c[k] - v) > tol:
            raise SystemExit(f"timing_loop.py: {c['tag']} {k} {v} re-measured as {c[k]}")
        if c[k] != v:
            print(f"{name}: {c['tag']} {k} {v} re-measured as {c[k]} (within {tol:.1e}; scored value kept)",
                  flush=True)
            c[k] = v
            save(c)  # sta_slack saved the re-measured value; --resume must read the scored one
    return out


def refine(d, name, work, b, best, measured, w, n, rounds, start=1):
    """Part 188, sticky re-classification: measure the best route's own failing-path slack, merge it
    with every slack measured so far (per net, the worst), and re-route at the best's strength and power
    term with those classes (routed source: replace; input source: the input slack lowered by them).
    Repeat while the new route beats the best, up to `rounds` times (numbered from `start`). Returns the
    new candidates. With PATIENCE > 0 (--patience, Part 215; 3 since 2026-11-24) a round that does not beat the best
    does not stop it: the next round goes on from that round's route (its slack merged in), until PATIENCE + 1 rounds
    in a row have not beaten the best; the best is still the candidate with the lowest change. With ONE_SESSION (since
    2026-11-24; --two-sessions turns it off) each new route is scored by the session that measures its slack."""
    idx = net_index(os.path.join(d, name + '.net'))
    measured, out, cur = list(measured), [], best
    top, misses = best, 0  # the best so far, and rounds since it (--patience)
    slack = None  # cur's slack file, when the session that scored cur measured it (ONE_SESSION)
    for it in range(start, start + rounds):
        if cur is b:
            break
        measured.append(slack if slack is not None else remeasured(d, name, cur, work, idx))
        slack = None
        merged = merge_slack(measured, os.path.join(work, f'merged{it}.slack'))
        prev = os.path.join(work, f'merged{it - 1}.slack')
        if os.path.exists(prev) and open(merged).read().split('\n', 1)[1] == \
                open(prev).read().split('\n', 1)[1]:
            print(f"{name}: refine {it}: the classes did not change, stopped", flush=True)
            break
        c = dict(route(d, name, work, tag_of(best, it), best['k3'], best['kp'], merged,
                       'replace' if best['src'] == 'r' else 'min'), src=best['src'])
        if ONE_SESSION:  # 2026-11-24: one session scores c and measures its slack for the next round
            slack = sta_slack(d, name, c, work, idx)
            if not all(k in c for k in SCORED):  # --resume: its slack was written before its scores were saved
                sta(d, name, [c])
        else:
            sta(d, name, [c])
        c['delta'] = delta(c, b, w, n)
        c['classed'] = sum(1 for _ in open(merged)) - 1  # nets in its classes (its own slack_nets is set if measured)
        save(c)
        out.append(c)
        print(f"{name}: refine {it}: {c['tag']} delta {c['delta']:+.4f} (from {cur['tag']} {cur['delta']:+.4f}, "
              f"{c['classed']} nets classed)", flush=True)
        unpack(cur)  # --resume: cur's slack may have been measured before and its route left packed
        unpack(c)  # --resume with --lean (Part 215): a replayed round's route may have been packed by a later prune
        if filecmp.cmp(c['route'], cur['route'], shallow=False):
            pack(c)
            break
        if c['delta'] < top['delta']:
            if top is not cur:
                pack(top)
            top, misses = c, 0
        else:  # --patience (Part 215): go on from this round's route up to PATIENCE rounds without a new best
            misses += 1
            if misses > PATIENCE:
                pack(c)
                break
            print(f"{name}: refine {it}: no gain on {top['tag']} {top['delta']:+.4f} ({misses} of {PATIENCE}), "
                  f"going on", flush=True)
        if cur is not top:
            pack(cur)
        cur = c
    if cur is not top:
        pack(cur)
    return out


def elmore_ks(best, ks):
    """The Elmore stage's strengths for this best: the ladder, plus ELMORE_HIGH when the best's timing strength is at
    least ELMORE_HIGH_K3 and the ladder was not given by --elmore."""
    if ks and not ELMORE_GIVEN and best['k3'] >= ELMORE_HIGH_K3:
        return list(ks) + [k for k in ELMORE_HIGH if k not in ks]
    return list(ks)


def elmore(d, name, work, b, best, w, n, ks):
    """Part 215: the best candidate rerouted with Elmore-delay rows (gr/elmore_rows.py, PD39ROUTE_TIMING_ROWS) at each
    strength in ks, from the classes it was routed with; returns the new candidates (all scored in one session)."""
    if best is b or not ks:
        return []
    ks = elmore_ks(best, ks)
    print(f"{name}: Elmore strengths for {best['tag']}: {','.join(f'{k:g}' for k in ks)}", flush=True)
    sf, smode = best.get('sf'), best.get('smode') or 'replace'
    out, keep = [], best

    def one(K):
        rows = os.path.join(work, f"{best['tag']}e{K:g}.rows")
        if not (RESUME and os.path.exists(rows)):  # in its own process (2026-11-21): mempool_cluster's build peaks at
            # 24 GB of footprint, which this process would otherwise keep through the route and the OpenROAD session;
            # written to .tmp first, so a stopped build is not taken as done by --resume
            info = subprocess.run([sys.executable, os.path.join(GR, 'elmore_rows.py'), d, sf or 'none',
                                   'min' if not sf else smode, rows + '.tmp', repr(float(K)), repr(float(best['kp']))]
                                  + ([] if CLASS_W == CLASS_W_DEFAULT else ['--weights', ','.join(map(repr, CLASS_W))]),
                                  capture_output=True, text=True, check=True).stdout.strip()
            os.replace(rows + '.tmp', rows)
            print(f"{name}: elmore K {K:g}: {info}", flush=True)
        return dict(route(d, name, work, f"{best['tag']}e{K:g}", best['k3'], best['kp'], sf, smode,
                          {'PD39ROUTE_TIMING_ROWS': rows}), src=best['src'])

    def scored(g):
        for c in g:
            c['delta'] = delta(c, b, w, n)
            print(f"{name}: elmore {c['tag']} delta {c['delta']:+.4f} (from {best['tag']} {best['delta']:+.4f})",
                  flush=True)
    if LEAN:  # 2026-11-21: one route at a time; every other route packed while it is scored, so only the candidate
        for K in ks:  # sits uncompressed during an OpenROAD session (mempool_cluster: 12.4 GB per route)
            c = one(K)
            prune([best] + out, c)
            sta(d, name, [c])
            scored([c])
            out.append(c)
            keep = min([keep, c], key=lambda x: x['delta'])
            prune([best] + out, keep)
    else:
        out = [one(K) for K in ks]
        sta(d, name, out)
        scored(out)
    return out


def write_tsv(path, allc, best, w, n, t0):
    with open(path, 'w') as f:
        f.write('tag\tk3\tkp\twns\ttns\tpower\tof\topens\tdelta\tslack_nets\tunmatched\tclassed\n')
        for c in allc:
            f.write(f"{c['tag']}\t{c['k3']}\t{c['kp']}\t{c['wns']}\t{c['tns']}\t{c['power']}\t{c['of']}\t{c['opens']}"
                    f"\t{c['delta']:+.4f}\t{c.get('slack_nets', '')}\t{c.get('slack_unmatched', '')}\t{c.get('classed', '')}\n")
        f.write(f"# best {best['tag']} delta {best['delta']:+.4f}; weights {w}; endpoints {N_ENDPOINTS or n}"
                f"{' (contest)' if N_ENDPOINTS else ''}; "
                f"{time.time() - t0:.0f} s for {len(allc)} candidates\n")


def reused_classes(c, src):
    """The class source an earlier loop routed candidate c with (--reuse, Part 215): a refined candidate (tag ...mK)
    used mergedK.slack, a stage 1-2 one with routed classes base.slack, with input classes the input slack (None)."""
    m = re.search(r'm(\d+)$', c['tag'])
    if m:
        return os.path.join(src, f'merged{m.group(1)}.slack'), 'replace' if c['src'] == 'r' else 'min'
    return (os.path.join(src, 'base.slack') if c['src'] == 'r' else None), 'replace'


def reuse(d, name, work, src, rounds, t0, ks=()):
    """--reuse SRC: refine an earlier loop's result (SRC/loop.tsv, its route files and base.slack) instead
    of re-running stages 0-2. Writes WORK/refine.tsv. SRC may itself be a refined loop (its refine.tsv and
    its latest mergedK.slack, which holds every slack measured there): rounds then continue from K + 1.
    Part 215: then the Elmore stage at ks from the best; with --refine 0 only the Elmore stage runs (the best's
    route is not unpacked unless it is the answer)."""
    rows, n = [], None
    tsv = os.path.join(src, 'refine.tsv')
    for line in open(tsv if os.path.exists(tsv) else os.path.join(src, 'loop.tsv')):
        if line.startswith('# best'):
            n = int(re.search(r'endpoints (\d+)', line).group(1))
        elif not line.startswith('tag\t') and line.strip():
            f = line.rstrip('\n').split('\t')
            m = re.match(r'k\d+([ri])', f[0])
            c = {'tag': f[0], 'k3': int(f[1]), 'kp': int(f[2]), 'wns': float(f[3]), 'tns': float(f[4]),
                 'power': float(f[5]), 'of': float(f[6]), 'opens': int(f[7]), 'delta': float(f[8]),
                 'src': m.group(1) if m else '', 'route': os.path.join(work, f[0] + '.route'),
                 'packed': os.path.join(src, f[0] + '.route.xz')}
            c['sf'], c['smode'] = reused_classes(c, src)
            rows.append(c)
    w = weights(os.path.join(d, name + '.cap'))
    for c in rows:  # loop.tsv prints the change to 4 decimals; recompute it from the full-precision terms
        c['delta'] = delta(c, rows[0], w, n)
    b, best = rows[0], min(rows, key=lambda c: c['delta'])
    if ks and best is not b and best['sf'] and not os.path.exists(best['sf']):  # the Elmore stage reads its classes
        raise SystemExit(f"timing_loop.py: --reuse: no class file {best['sf']} for {best['tag']}")
    if rounds > 0:
        unpack(best)
    base_slack = os.path.join(src, 'base.slack')
    its = [int(m.group(1)) for f in os.listdir(src) for m in [re.match(r'merged(\d+)\.slack$', f)] if m]
    measured = ([os.path.join(src, f'merged{max(its)}.slack')] if its else
                [base_slack] if os.path.exists(base_slack) else [])
    if best['src'] == 'r' and not measured:
        raise SystemExit('timing_loop.py: --reuse: routed classes but no base.slack in ' + src)
    m = re.search(r'm(\d+)$', best['tag'])
    new = refine(d, name, work, b, best, measured, w, n, rounds, int(m.group(1)) + 1 if m else 1) if rounds > 0 else []
    allc = rows + new
    top = min(allc, key=lambda c: c['delta'])
    if ks:  # Part 215: the Elmore stage at the best so far
        new += elmore(d, name, work, b, top, w, n, ks)
        allc = rows + new
        top = min(allc, key=lambda c: c['delta'])
        prune(new, top)
    write_tsv(os.path.join(work, 'refine.tsv'), allc, top, w, n, t0)
    print(f"{name}: reused {best['tag']} {best['delta']:+.4f} -> best {top['tag']} {top['delta']:+.4f}, "
          f"{len(new)} new, {time.time() - t0:.0f} s", flush=True)
    return top


def selftest_patience(t):
    """Part 215, --patience and the one-session rounds: refinement on scripted round scores (routing and OpenROAD
    stubbed). Each case runs in both session modes; the decisions must agree, and the session counts must be the
    ones each mode promises."""
    global sta_slack, route, sta, delta, net_index, PATIENCE, ONE_SESSION
    ok = True
    saved = (sta_slack, route, sta, delta, net_index, PATIENCE, ONE_SESSION)
    pw = os.path.join(t, 'pat')
    os.makedirs(pw)
    calls = {}

    def run(scores, pat, rounds=12, power=0.0, wobble=None, one=False, drop=False):
        global PATIENCE, ONE_SESSION
        PATIENCE, ONE_SESSION, cnt = pat, one, [0]
        calls.update(slack=0, sta=0)

        def fake_slack(d_, nm, c, wk_, idx_):  # like sta_slack: reads c's timing and power, and writes its slack
            if not (drop and c['tag'] != 'k180r'):  # drop: as a resumed slack file whose scores were never saved
                c.update(tns=0.0, wns=0.0, power=power * (1 + (wobble or 0.0)))
            save(c)
            cnt[0] += 1
            calls['slack'] += 1
            f = os.path.join(pw, f"s{cnt[0]}.slack")
            open(f, 'w').write(f'# s\n{cnt[0]} -1e-10\n')  # a new net each round, so the classes keep changing
            return f

        def fake_sta(d_, nm, cands):
            calls['sta'] += 1
            for c in cands:
                c.update(tns=0.0, wns=0.0, power=power)
                save(c)

        def fake_route(d_, nm, wk_, tag, *a, **k):  # like route: no timing or power until a session scores it
            r = os.path.join(pw, tag + '.route')
            open(r, 'w').write(tag)
            return {'tag': tag, 'route': r, 'k3': 180, 'kp': 0}
        global sta_slack, route, sta, delta, net_index
        sta_slack, route, sta, net_index = fake_slack, fake_route, fake_sta, (lambda f: {})
        delta = (lambda c, *a: scores[int(re.search(r'm(\d+)$', c['tag']).group(1)) - 1])
        b_ = {'tag': 'base', 'route': os.path.join(pw, 'base.route')}
        best_ = {'tag': 'k180r', 'k3': 180, 'kp': 0, 'src': 'r', 'delta': -1.0, 'route': os.path.join(pw, 'k180r.route'),
                 'tns': 0.0, 'wns': 0.0, 'power': power}
        open(best_['route'], 'w').write('k180r')
        for f in os.listdir(pw):
            if f.startswith('merged'):
                os.remove(os.path.join(pw, f))
        new = refine('/d', 'des', pw, b_, best_, [], None, 1, rounds)
        got = [c['tag'] for c in new], min([best_] + new, key=lambda c: c['delta'])['tag']
        return got + ([c['power'] for c in [best_] + new],) if wobble is not None else got
    # 2026-11-23: a slack session whose power moves 4e-5 relative keeps the scored value; 1e-3 is a different route
    global CKPT
    CKPT = os.path.join(pw, 'scores.jsonl')  # and --resume reads the scored value back, not the slack session's
    ok &= run([-1.1, -1.05], 0, power=0.14, wobble=4e-5) == (['k180rm1', 'k180rm2'], 'k180rm1', [0.14, 0.14, 0.14])
    DONE.clear()
    load(CKPT)
    ok &= DONE['k180r']['power'] == 0.14 and DONE['k180rm1']['power'] == 0.14
    # 2026-11-24, one session: the incoming best is still re-measured (scored value kept); a new route's own session
    # is its score
    DONE.clear()
    os.remove(CKPT)
    p1 = 0.14 * (1 + 4e-5)
    ok &= run([-1.1, -1.05], 0, power=0.14, wobble=4e-5, one=True) == (['k180rm1', 'k180rm2'], 'k180rm1', [0.14, p1, p1])
    DONE.clear()
    load(CKPT)
    ok &= DONE['k180r']['power'] == 0.14 and DONE['k180rm1']['power'] == p1
    DONE.clear()
    CKPT = None
    for one in (False, True):
        try:
            run([-1.1, -1.05], 0, power=0.14, wobble=1e-3, one=one)
            ok = False
        except SystemExit:
            pass
    sc = [-1.1, -1.05, -1.08, -1.09, -1.2, -1.15, -1.1, -1.0, -0.9]  # m1 gains, m2-m4 do not, m5 gains, then none
    cases = [((sc, 0), (['k180rm1', 'k180rm2'], 'k180rm1')),  # patience 0: the first round without gain stops it
             ((sc, 2), (['k180rm1', 'k180rm2', 'k180rm3', 'k180rm4'], 'k180rm1')),
             ((sc, 3), ([f'k180rm{i}' for i in range(1, 10)], 'k180rm5')),  # m6-m8 go on, m9 stops it
             ((sc, 3, 6), ([f'k180rm{i}' for i in range(1, 7)], 'k180rm5'))]  # the round cap still binds
    for args, want in cases:
        r = len(want[0])  # rounds run
        ok &= run(*args) == want and calls == {'slack': r, 'sta': r}  # two sessions: the last route's slack unmeasured
        ok &= run(*args, one=True) == want and calls == {'slack': r + 1, 'sta': 0}  # one per round, plus the best's
        ok &= run(*args, one=True, drop=True) == want and calls == {'slack': r + 1, 'sta': r}  # --resume: scored anyway
    sta_slack, route, sta, delta, net_index, PATIENCE, ONE_SESSION = saved
    return ok


def selftest():
    """The Part 188 helpers on hand-made files: the merge keeps every net and its worst slack, ignores
    comments, and the refined candidate's tag names its strength, source, power term and round."""
    import tempfile
    t = tempfile.mkdtemp()
    a, b = os.path.join(t, 'a.slack'), os.path.join(t, 'b.slack')
    open(a, 'w').write('# a\n3 -1.0e-10\n7 -5.0e-10\n')
    open(b, 'w').write('# b\n7 -2.0e-10\n9 -3.0e-10\n\n')
    got = {int(l.split()[0]): float(l.split()[1]) for l in open(merge_slack([a, b], os.path.join(t, 'm')))
           if not l.startswith('#')}
    ok = got == {3: -1.0e-10, 7: -5.0e-10, 9: -3.0e-10}
    ok &= tag_of({'k3': 540, 'src': 'r', 'kp': 0}, 1) == 'k540rm1'
    ok &= tag_of({'k3': 180, 'src': 'i', 'kp': 4}, 2) == 'k180ip4m2'
    global LEAN  # --lean: a pruned route is compressed, the kept one is not, and unpack restores it exactly
    r1, r2 = os.path.join(t, 'x.route'), os.path.join(t, 'y.route')
    open(r1, 'w').write('net0\n(\n0 0 1 0 5 1\n)\n'); shutil.copyfile(r1, r2)
    LEAN, c1, c2 = True, {'route': r1}, {'route': r2}
    prune([c1, c2], c2)
    ok &= not os.path.exists(r1) and os.path.exists(r1 + '.xz') and os.path.exists(r2)
    unpack(c1)
    ok &= filecmp.cmp(r1, r2, shallow=False)
    LEAN = False
    global CKPT, N_SAVED  # scores.jsonl: every field round-trips exactly, the latest value wins, endpoints kept
    CKPT, N_SAVED = os.path.join(t, 'scores.jsonl'), None
    save({'tag': 'k20r', 'k3': 20, 'kp': 0, 'route': r1, 'of': 178597927.0813, 'opens': 0})
    save({'tag': 'k20r', 'wns': -0.28017923, 'tns': -63180.6796875, 'power': 23.466699 + 1e-12})
    save(n=1443997)
    keep_done = dict(DONE)
    DONE.clear(); N_SAVED = None
    load(CKPT)
    ok &= DONE == keep_done and N_SAVED == 1443997 and 'route' not in DONE['k20r']
    ok &= DONE['k20r']['power'] == 23.466699 + 1e-12 and DONE['k20r']['of'] == 178597927.0813
    DONE.clear(); CKPT, N_SAVED = None, None
    # Part 215: the slack session's skip counts, from the fixed script's line and from the older one's
    ok &= slack_counts('x\nSKIPPEDPATHS 0 BADPOINTS 6278\nSLACKNETS 9263\n') == (0, 6278)
    ok &= slack_counts('SKIPPEDPATHS 6278\n') == (6278, None)
    global N_ENDPOINTS  # Part 215: --endpoints replaces the measured count in the score change
    b0 = {'wns': -1.0, 'tns': -100.0, 'power': 1.0, 'of': 0.0}
    c0 = {'wns': -1.0, 'tns': -50.0, 'power': 1.0, 'of': 0.0}
    ok &= abs(delta(c0, b0, [0, -1, 0, 0], 10) - -5.0) < 1e-12
    N_ENDPOINTS = 25
    ok &= abs(delta(c0, b0, [0, -1, 0, 0], 10) - -2.0) < 1e-12
    N_ENDPOINTS = None
    # the contest's counts: a blind design's folder, by path or through a symlink; a visible one gets none
    ok &= contest_endpoints('/x/blind/mempool_tile_wrap') == 13350 and contest_endpoints('/x/blind/mempool_group/') == 347869
    ok &= contest_endpoints('/x/visible/mempool_tile_wrap') is None and contest_endpoints('/x/blind/unknown') is None
    os.makedirs(os.path.join(t, 'blind', 'ariane'))
    os.symlink(os.path.join(t, 'blind', 'ariane'), os.path.join(t, 'ariane_link'))
    ok &= contest_endpoints(os.path.join(t, 'ariane_link')) == 20218
    # Part 215, --reuse: the class source of an earlier loop's candidate, and a route restored from the loop's copy
    ok &= reused_classes({'tag': 'k20ip1m1', 'src': 'i'}, '/s') == ('/s/merged1.slack', 'min')
    ok &= reused_classes({'tag': 'k540rm3', 'src': 'r'}, '/s') == ('/s/merged3.slack', 'replace')
    ok &= reused_classes({'tag': 'k180r', 'src': 'r'}, '/s') == ('/s/base.slack', 'replace')
    ok &= reused_classes({'tag': 'k20ip1', 'src': 'i'}, '/s') == (None, 'replace')
    os.makedirs(os.path.join(t, 'src'))
    subprocess.run(['xz', '-k', '-c', r2], stdout=open(os.path.join(t, 'src', 'z.route.xz'), 'wb'), check=True)
    c3 = {'route': os.path.join(t, 'z.route'), 'packed': os.path.join(t, 'src', 'z.route.xz')}
    unpack(c3)
    ok &= filecmp.cmp(c3['route'], r2, shallow=False) and not os.path.exists(c3['route'] + '.tmp')
    # --reuse --refine 0 hands the Elmore stage the strengths it was given and the best's classes (2026-11-21: a
    # local list of merged-file numbers once shadowed the strengths, and blind mempool_cluster was routed at K 1)
    global elmore
    src, wk, dd = os.path.join(t, 'loopsrc'), os.path.join(t, 'wk'), os.path.join(t, 'des')
    for f in (src, wk, dd):
        os.makedirs(f, exist_ok=True)
    open(os.path.join(dd, 'des.cap'), 'w').write('x\n1 1 -1 -10 2 1e-8\n')
    for f in ('merged1.slack', 'merged2.slack'):
        open(os.path.join(src, f), 'w').write('# m\n1 -1e-10\n')
    open(os.path.join(src, 'loop.tsv'), 'w').write(
        'tag\tk3\tkp\twns\ttns\tpower\tof\topens\tdelta\n'
        'base\t0\t0\t-1\t-100\t2\t10\t0\t+0\n'
        'k20ip1m1\t20\t1\t-0.5\t-50\t2\t10\t0\t-1\n# best k20ip1m1 delta -1; weights x; endpoints 100; 1 s\n')
    seen, real = {}, elmore
    elmore = lambda d_, nm, wk_, b_, best_, w_, n_, ks_: seen.update(ks=list(ks_), sf=best_['sf'], tag=best_['tag']) or []
    reuse(dd, 'des', wk, src, 0, time.time(), [35.0, 70.0])
    elmore = real
    ok &= seen == {'ks': [35.0, 70.0], 'sf': os.path.join(src, 'merged1.slack'), 'tag': 'k20ip1m1'}
    ok &= not os.path.exists(os.path.join(wk, 'k20ip1m1.route'))  # the best's route is not unpacked for the stage
    # 2026-11-22: the higher strengths are tried at timing strength 540 only, never when --elmore was given
    global ELMORE_GIVEN
    was, ELMORE_GIVEN = ELMORE_GIVEN, False
    ok &= elmore_ks({'k3': 540}, ELMORE_LADDER) == [35, 70, 140, 280, 560]
    ok &= elmore_ks({'k3': 180}, ELMORE_LADDER) == [35, 70, 140] and elmore_ks({'k3': 540}, []) == []
    ok &= elmore_ks({'k3': 540}, [280, 1000]) == [280, 1000, 560]
    ELMORE_GIVEN = True
    ok &= elmore_ks({'k3': 540}, [35.0]) == [35.0]
    # Part 217: --class-weights -- the default keeps the old settings string; others scale k3; bad values refused
    global CLASS_W
    ok &= timing_layers(180) == '45.0,90.0,180' and timing_layers(20) == '5.0,10.0,20'
    CLASS_W = class_weights('1,1,1')
    ok &= timing_layers(180) == '180.0,180.0,180.0'
    CLASS_W = class_weights('0.0625,0.25,1')
    ok &= timing_layers(540) == '33.75,135.0,540.0'
    CLASS_W = CLASS_W_DEFAULT
    for bad in ('1,1', '1,1,0', '-1,1,1', 'a,1,1', 'nan,1,1', '1,1,inf'):
        try:
            class_weights(bad)
            ok = False
        except ValueError:
            pass
    ELMORE_GIVEN = was
    ok &= selftest_patience(t)
    print('timing_loop selftest:', 'pass' if ok else f'FAIL {got} {seen}')
    shutil.rmtree(t)
    return ok


# Part 216 (the user, 2026-11-24: every option and input checked before any work, every problem reported): the options
# this script takes (None: a switch); check_run() checks them, their values, the design's files, the tools and the
# router's own settings check under the flow's settings files, and stops with every problem before anything runs.
OPTIONS = {'--out': 'value', '--classes': 'value', '--refine': 'value', '--reuse': 'value', '--router': 'value',
           '--lean': None, '--resume': None, '--k3': 'value', '--elmore': 'value', '--endpoints': 'value',
           '--patience': 'value', '--two-sessions': None, '--class-weights': 'value'}


def class_weights(v):
    """--class-weights w1,w2,w3 (Part 217): three finite numbers >= 0, w3 > 0; ValueError otherwise."""
    w = tuple(float(x) for x in v.split(','))
    if len(w) != 3 or any(not (x == x and abs(x) != float('inf')) or x < 0 for x in w) or not w[2] > 0:
        raise ValueError(v)
    return w


def check_run(argv):
    """Part 216: the command line and everything it names, before any work. Returns (design dir, work dir, options) or
    exits with every problem listed."""
    opts, args, p = flowcheck.parse_options(argv, OPTIONS, 2, 'timing_loop.py')

    def val(opt, conv, ok, what):
        if opt not in opts:
            return None
        try:
            v = conv(opts[opt])
            if ok(v):
                return v
        except ValueError:
            pass
        p.add(f"{opt} takes {what} (got '{opts[opt]}')")
        return None
    val('--classes', str, lambda v: v in ('both', 'routed', 'input'), 'both, routed or input')
    val('--refine', int, lambda v: v >= 0, 'a round count >= 0')
    val('--patience', int, lambda v: v >= 0, 'a round count >= 0')
    val('--k3', lambda v: [int(x) for x in v.split(',')], lambda v: v and min(v) > 0, 'positive integers, e.g. 20,180,540')
    val('--elmore', lambda v: [float(x) for x in v.split(',')], lambda v: v == [0.0] or (v and min(v) > 0),
        'positive strengths, e.g. 35,70,140, or 0 (off)')
    val('--endpoints', lambda v: v if v == 'measured' else int(v), lambda v: v == 'measured' or v > 0,
        'a positive count or measured')
    val('--class-weights', class_weights, lambda v: True, 'three numbers w1,w2,w3 >= 0 with w3 > 0, e.g. 0.25,0.5,1')
    d = work = None
    if len(args) == 2:
        d, work = os.path.abspath(args[0]), os.path.abspath(args[1])
        name = os.path.basename(d.rstrip('/'))
        if p.need_dir(d, 'DESIGN_DIR'):
            for ext in ('.cap', '.net', '.def', '.sdc'):
                p.need_file(os.path.join(d, name + ext), f'the design\'s {ext} file')
        p.need_workdir(work, 'WORK_DIR')
        if ' ' in work:
            p.add(f'WORK_DIR {work} contains a space (OpenROAD sources its scripts unquoted)')
        if '--resume' in opts:
            p.need_file(os.path.join(work, 'scores.jsonl'), '--resume: the earlier run\'s scores')
    if '--out' in opts:
        p.need_output(opts['--out'], '--out')
    if '--reuse' in opts and p.need_dir(opts['--reuse'], '--reuse'):
        if not any(os.path.exists(os.path.join(opts['--reuse'], f)) for f in ('loop.tsv', 'refine.tsv')):
            p.add(f"--reuse {opts['--reuse']} holds no loop.tsv or refine.tsv")
    router = os.path.abspath(opts['--router']) if '--router' in opts else ROUTER
    p.need_exe(OPENROAD, 'OpenROAD')
    p.need_exe(EVALUATOR, "the contest's evaluator")
    p.need_dir(EVAL_DIR, 'the evaluation folder (NanGate45 libraries, setRC.tcl)')
    for f, what in ((MULTI_TCL, 'the scoring script'), (SLACK_TCL, 'the slack script'),
                    (os.path.join(GR, 'elmore_rows.py'), 'the Elmore rows script')):
        p.need_file(f, what)
    envs = [os.path.join(GR, f) for f in ('standard.env', 'standard24.env', 'fast24.env', 'standard25.env')]
    if all(p.need_file(f, 'the settings file') for f in envs) and p.need_exe(router, 'the router'):
        base = {}  # the settings every candidate gets (route()); the router lists what it would refuse
        for f in envs[:3]:
            base.update(env_file(f))
        base['PD37ROUTE_THREADS'] = '8'
        r = subprocess.run([router, '--settings'], env={**os.environ, **base}, capture_output=True, text=True)
        for line in r.stdout.splitlines():
            if line.startswith('problem: '):
                p.add('the router refuses its settings: ' + line[len('problem: '):])
        if r.returncode != 0:
            p.add(f'{router} --settings exited {r.returncode}: ' + (r.stderr.strip().splitlines() or [''])[-1])
    p.stop_if_any('usage:\n' + next(b for b in __doc__.split('\n\n') if 'python3 gr/timing_loop.py DESIGN_DIR' in b))
    return d, work, opts


def main():
    if '--selftest' in sys.argv:
        sys.exit(0 if selftest() else 1)
    d, work, opts = check_run(sys.argv[1:])
    out = opts.get('--out')
    name = os.path.basename(d.rstrip('/'))
    os.makedirs(work, exist_ok=True)
    w = weights(os.path.join(d, name + '.cap'))
    t0 = time.time()
    mode = opts.get('--classes', 'both')
    global ROUTER, LEAN, CKPT, RESUME
    LEAN = '--lean' in opts
    RESUME = '--resume' in opts
    CKPT = os.path.join(work, 'scores.jsonl')
    if RESUME:
        load(CKPT)
        print(f"{name}: resume: {len(DONE)} candidates in scores.jsonl", flush=True)
    elif os.path.exists(CKPT):  # a fresh run starts its own record
        os.replace(CKPT, CKPT + '.prev')
    if '--router' in opts:
        ROUTER = os.path.abspath(opts['--router'])
    # Part 188 (the user, 2026-11-06): sticky re-classification by default; --refine 0 turns it off. Part 215 (the
    # user, 2026-11-20): up to 12 rounds (was 2) -- it stops by itself once a round gains nothing or the classes stop
    # changing; blind mempool_tile_wrap gained in every round to 8 (0.9238 at 2 -> 0.4879, contest score); 2026-11-24: 24
    rounds = int(opts['--refine']) if '--refine' in opts else REFINE_ROUNDS
    global PATIENCE, ONE_SESSION
    ONE_SESSION = '--two-sessions' not in opts
    if '--patience' in opts:  # Part 215: rounds without a new best before refinement stops (default PATIENCE)
        PATIENCE = int(opts['--patience'])
    global CLASS_W
    if '--class-weights' in opts:  # Part 217: per-class strengths (method 7)
        CLASS_W = class_weights(opts['--class-weights'])
    global K3_LADDER
    if '--k3' in opts:  # Part 215: the timing strengths to try, e.g. --k3 2,5,10,20 (default 20,180,540)
        K3_LADDER = [int(v) for v in opts['--k3'].split(',')]
    # Part 215: the Elmore stage -- after refinement, the best rerouted with Elmore-delay rows at each K (the user,
    # 2026-11-21: on by default at ELMORE_LADDER; --elmore 0 turns it off; blind mempool_tile_wrap 0.4879 -> 0.1768 at
    # K 70, blind ariane 1.4605 -> 1.4368 at K 35, blind mempool_group 1.2959 -> 1.0946 at K 140)
    global ELMORE_GIVEN
    ELMORE_GIVEN = '--elmore' in opts
    ks = [float(v) for v in opts['--elmore'].split(',')] if ELMORE_GIVEN else list(ELMORE_LADDER)
    if ks == [0.0]:
        ks = []
    global N_ENDPOINTS  # Part 215 (the user, 2026-11-21: standard): the contest's endpoint count for a blind design
    N_ENDPOINTS = contest_endpoints(d)
    if '--endpoints' in opts:
        v = opts['--endpoints']
        N_ENDPOINTS = None if v == 'measured' else int(v)
    print(f"{name}: endpoints for ranking: " + (f"{N_ENDPOINTS} (contest)" if N_ENDPOINTS else "OpenROAD's count")
          + f"; Elmore stage: {'K ' + ','.join(f'{k:g}' for k in ks) if ks else 'off'}"
          + (f" (+ {','.join(f'{k:g}' for k in ELMORE_HIGH)} at timing strength {ELMORE_HIGH_K3})"
             if ks and not ELMORE_GIVEN else ''), flush=True)
    if '--reuse' in opts:
        # Part 215: --refine 0 with --reuse skips refinement (it ran one round before), so only the Elmore stage runs
        top = reuse(d, name, work, os.path.abspath(opts['--reuse']), rounds, t0, ks)
        if out:
            unpack(top)
            shutil.copyfile(top['route'], out)
        return
    b = route(d, name, work, 'base', 0, 0)
    srcs = {}
    if mode in ('both', 'routed'):  # stage 0: the unbiased route scored, its failing-path nets' slack measured
        srcs['r'] = sta_slack(d, name, b, work, net_index(os.path.join(d, name + '.net')))
    if mode in ('both', 'input'):
        srcs['i'] = None  # the input pin slack
    if 'r' in srcs:
        pack(b)  # --lean: the unbiased route is not read again
    b['delta'], b['src'] = 0.0, ''
    if LEAN:  # one class source per session; the best so far stays unpacked
        s1, n, best = [], None, b
        for t, sf in srcs.items():
            g = [dict(route(d, name, work, f'k{k}{t}', k, 0, sf), src=t) for k in K3_LADDER]
            m = sta(d, name, ([b] if 'wns' not in b else []) + g)  # b: scored with the first group if not by stage 0
            if n is not None and m != n:
                raise SystemExit(f'timing_loop.py: endpoint count {m} != {n}')
            n = m
            for c in g:
                c['delta'] = delta(c, b, w, n)
            s1 += g
            best = min([b] + s1, key=lambda c: c['delta'])
            prune([b] + s1, best)
    else:
        s1 = [dict(route(d, name, work, f'k{k}{t}', k, 0, sf), src=t) for t, sf in srcs.items() for k in K3_LADDER]
        n = sta(d, name, ([b] if 'wns' not in b else []) + s1)
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
    prune(allc, best)
    if rounds > 0:  # Part 188: sticky re-classification from the best route's own slack
        allc += refine(d, name, work, b, best, [srcs['r']] if srcs.get('r') else [], w, n, rounds)
        best = min(allc, key=lambda c: c['delta'])
    if ks:  # Part 215: the Elmore stage (--elmore K1,K2,...), at the best so far
        allc += elmore(d, name, work, b, best, w, n, ks)
        best = min(allc, key=lambda c: c['delta'])
        prune(allc, best)
    write_tsv(os.path.join(work, 'loop.tsv'), allc, best, w, n, t0)
    if out:
        unpack(best)
        shutil.copyfile(best['route'], out)
    print(f"{name}: best {best['tag']} (k3 {best['k3']}, kp {best['kp']}) delta {best['delta']:+.4f}, "
          f"{len(allc)} candidates, {time.time() - t0:.0f} s")


if __name__ == '__main__':
    main()
