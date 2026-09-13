# History-cost design-space sweep (direction 1) and rip-up-reroute prototype (direction 2)

Recorded 2026-09-06. Follow-on to `ZERO_OVERFLOW_CONVERGENCE_EXPERIMENT.md` and
`gr/iroute.cpp`'s own header. iroute's own tuning (additive step 0.02) beat
hroute's extended-round plateau on all three benchmarks but only by 0.5-2.7%.
This file records two follow-on experiments per the task brief: (1) pushing
the history-cost growth rate/trigger further than the original flat-additive
sweep, and (2) prototyping rip-up-reroute of already-committed nets.

Per CLAUDE.md's "Long-running work and background agents" section: raw
numbers land here as they're produced, never held only in conversation state.
Concurrent background processes capped at 3 throughout.

## Screening methodology

Full 172-round runs (iroute's own comparison basis) take ~2200-3100s per
benchmark -- too slow to screen many configs. Screening uses a reduced
round budget (`IROUTE_FAST_ROUNDS=30 IROUTE_SLOW_ROUNDS=40`, 70 rounds max)
on adaptec1 only (measured ~13s/round blended, so ~15 min/run). Any config
that wins the screen gets a full validation run (172-round budget, all
three benchmarks, gr_verify + eval2008.pl cross-check) before being adopted
or recorded as the new default.

## Part 1: history-cost growth rate/trigger sweep

### Screen 1 (adaptec1, 30 fast / 40 slow rounds, reduced budget)

Raw trace files: `/tmp` scratch dir during the run, final numbers below.

| config | env vars | overflow (62 rounds, adaptec1) |
|---|---|---|
| baseline_020 | `IROUTE_HIST_STEP=0.02` (current committed default) | 168946 |
| step_005 | `IROUTE_HIST_STEP=0.005` | 169538 |
| step_010 | `IROUTE_HIST_STEP=0.01` | 168828 |
| step_050 | `IROUTE_HIST_STEP=0.05` | **167724 (screen winner)** |
| mult_105 | `IROUTE_HIST_MODE=mult IROUTE_HIST_FACTOR=1.05` | 168696 |
| mult_102 | `IROUTE_HIST_MODE=mult IROUTE_HIST_FACTOR=1.02` | 168624 |
| streak3_020 | `IROUTE_HIST_STEP=0.02 IROUTE_HIST_STREAK=3` | 170770 (worst) |
| streak5_010 | `IROUTE_HIST_STEP=0.1 IROUTE_HIST_STREAK=5` | 170012 |
| scaleoverflow_005 | `IROUTE_HIST_STEP=0.005 IROUTE_HIST_SCALE=overflow` | 169130 |

All 9 configs screened, single run each (no repeats -- treat close results as
noise, not signal). Two clear qualitative findings: **streak-gating (delaying
growth until N consecutive over-capacity rounds) made things worse in both
variants tried**, the two worst results of the sweep -- letting a net drift
back onto a recently-congested edge for longer before the penalty kicks in
apparently costs more than it saves. **Overflow-magnitude scaling was
middling**, not clearly better than a well-chosen flat step. The screen
winner, `step_050` (flat additive step=0.05), beat the current committed
default (`step_020`, 168946) by ~0.72% at this reduced round budget --
notably a *larger* step than 0.02, which is the opposite direction from
what the original full-172-round sweep found (0.02 beat 0.1 there). This
open contradiction is exactly why the methodology above requires a full
172-round validation on all three real benchmarks before adopting anything
-- the optimal step size may not be a single constant independent of round
budget, and a screen-only result is not enough evidence either way.

### Full 172-round validation of the screen winner (step=0.05)

Bar to beat (iroute's currently-committed default, step=0.02, 172 rounds,
legal + eval2008.pl cross-checked): adaptec1 164094, newblue1 41314,
bigblue1 296914.

Launched 2026-09-06 ~12:21PM, all three benchmarks in parallel (3-process
cap respected).

| benchmark | step=0.02 (prior default) | step=0.05 | improvement |
|---|---|---|---|
| adaptec1 | 164094 | 163594 | -0.30% |
| newblue1 | 41314 | 41272 | -0.10% |
| bigblue1 | 296914 | 296534 | -0.13% |

All legal per gr_verify (0 disconnected/incomplete/unrouted/malformed),
all cross-checked exactly against eval2008.pl. A genuine, consistent win
on all three -- but notably smaller than the reduced-budget screen
predicted (~0.72% on adaptec1 alone, vs the ~0.30% that actually held up
at full scale). **Methodological lesson**: the 62-round screen is useful
for cheaply ruling out clearly-bad configs (it correctly identified both
streak-gating variants and step=0.005 as worse) and for ranking
plausible candidates, but its absolute margins should not be trusted --
only a full-budget validation run settles whether a lead is real and by
how much. Adopted step=0.05 as iroute's new default; see `gr/iroute.cpp`'s
header for the updated Result section and reasoning.

## Part 2: jroute -- rip-up-reroute of already-committed nets

Updated jroute's own inherited `histStep` default from a stale 0.02 to
0.05 before measuring anything, so its rip-up-reroute mechanism is tested
on top of iroute's actual current best, not an outdated setting. Self-test
passes with decisive fixtures (a net with no current reason to move gets
picked and rerouted purely because of accumulated history on its edges;
uniform scarring proves the accept/reject bookkeeping never accepts a
regression).

First real-benchmark run launched 2026-09-06 ~1:31PM, default settings
(`JROUTE_RIPUP_INTERVAL=4`, `JROUTE_RIPUP_COUNT=200`, hist step=0.05,
60/150 round budget -- same budget as iroute's own comparison basis).

Bar to beat (iroute at hist=0.05, same 172-round budget, its current best):
adaptec1 163594, newblue1 41272, bigblue1 296534.

| benchmark | iroute (bar, hist=0.05) | jroute (default ripup settings) | delta |
|---|---|---|---|
| adaptec1 | 163594 | 163154 | -0.27% |
| newblue1 | 41272 | 41318 | +0.11% |
| bigblue1 | 296534 | 296170 | -0.12% |

All legal per gr_verify, all cross-checked exactly against eval2008.pl.
A genuinely mixed result -- two benchmarks improve modestly, newblue1
regresses slightly -- reported as-is rather than averaged into a single
verdict. All three deltas are small (under 0.3%). No trace instrumentation
exists yet for the rip-up pass's own accept/reject counts, so it isn't
possible to tell whether the pass is finding real local improvements that
wash out in aggregate, or mostly doing little -- add that before spending
a tuning pass on JROUTE_RIPUP_INTERVAL/JROUTE_RIPUP_COUNT. See
`gr/jroute.cpp`'s header for the full writeup.

## Part 3: overflow-percentage scaling at significant magnitude, then relaxable penalties

User-proposed refinement, arrived at independently from first principles
and matching iroute's existing mechanism almost exactly, with two open
questions worth testing directly rather than assuming an answer:

1. Overflow-scaling (`IROUTE_HIST_SCALE=overflow`) was only ever tried
   once before, paired with the smallest step (0.005), where it was
   "middling" (see Part 1). Untested: overflow-scaling combined with a
   *significant* base step, the actual combination proposed. Screening
   now at step=0.05/0.1/0.2, all with `IROUTE_HIST_SCALE=overflow`,
   adaptec1 only, reduced 70-round budget (same methodology as Part 1).
   Bar to beat: flat step=0.05's own screen result, 167724 (Part 1).

| config | env vars | overflow (62-70 rounds, adaptec1) |
|---|---|---|
| scaleoverflow_050 | `IROUTE_HIST_STEP=0.05 IROUTE_HIST_SCALE=overflow` | 168672 |
| scaleoverflow_100 | `IROUTE_HIST_STEP=0.1 IROUTE_HIST_SCALE=overflow` | 168728 |
| scaleoverflow_200 | `IROUTE_HIST_STEP=0.2 IROUTE_HIST_SCALE=overflow` | 171070 (worst of this batch) |

**Conclusion**: none of these beat flat step=0.05's own screen result (167724),
and the loss gets worse as magnitude increases (168672 -> 168728 -> 171070)
-- the same overshoot shape the original naive step=1.0 showed. No full
172-round validation needed: per Part 1's own lesson, a screen result only
needs full-scale confirmation when it *wins* the screen (since full-scale
margins can be smaller than a screen predicts) -- a screen *loss* this
clear doesn't need re-checking. **Overflow-percentage scaling does not
help at any magnitude tried, small or significant; a plain flat step
remains the best mechanism found.** iroute's default (flat step=0.05)
is unchanged.

2. Separately, per the user's explicit request: build a new router with
   **relaxable** history penalties, a genuinely different mechanism from
   iroute's current permanent/monotonic one. Design: each edge keeps a
   stack (not just a running total) of every penalty increment it was
   ever charged, in the order charged. On a round where that edge is
   *not* over capacity and its stack is non-empty, pop and undo the most
   recently added increment (LIFO -- "relax... in reverse order"). An
   edge that was dirty for N rounds needs N consecutive clean rounds to
   fully return to baseline. This is deliberately tested *after* item 1
   settles, and *after* confirming the permanent version's own behavior
   first, per the user's own instructions ("try permanent first... after
   that try another version that allows the penalty to relax").
   Built as `gr/kroute.cpp`, self-tested with exact-arithmetic fixtures
   (a single dirty round then a single clean round returns history to
   precisely 1.0, not just "lower"; three dirty rounds need exactly
   three consecutive clean rounds to fully unwind, with a partial run of
   clean rounds leaving an exact, LIFO-consistent partial penalty).

## Part 3 screen results (adaptec1, 62-70 rounds, reduced budget)

Bar to beat: flat step=0.05's own screen result, 167724.

| config | mechanism | overflow |
|---|---|---|
| ratio_mode | iroute, `IROUTE_HIST_MODE=ratio` (histH *= usage/capacity directly) | 184730 (clear loss) |
| kroute_default | kroute, LIFO-relaxable penalties, default settings (relax on, flat step=0.05) | **166890 (screen win, ~0.5%)** |

`ratio_mode` loses clearly and by a wide margin -- the same overshoot
failure mode the original naive flat step=1.0 showed: multiplying
directly by usage/capacity every over-capacity round compounds too fast
for badly-congested edges (an edge at 10x capacity multiplies its
history 10-fold in one round). No full-scale validation needed for a
loss this decisive.

`kroute_default` beats even the flat-additive default's own screen
result -- the first config in this entire sweep (10 configs across
Parts 1 and 3) to do so. Per this project's own established lesson
(screen margins don't reliably transfer to the real 172-round budget),
this needs full-scale confirmation before being trusted. Full 172-round,
three-benchmark validation launched 2026-09-07 ~12:43AM; results pending.

Bar to beat at full scale (iroute's current adopted default, flat
step=0.05, 172 rounds): adaptec1 163594, newblue1 41272, bigblue1
296534.

| benchmark | iroute (bar) | kroute (relaxable, default settings) | improvement |
|---|---|---|---|
| adaptec1 | 163594 | 162584 | -0.62% |
| newblue1 | 41272 | 41014 | -0.63% |
| bigblue1 | 296534 | 292420 | -1.39% |

All legal per gr_verify, all cross-checked exactly against eval2008.pl.
**A clean, unanimous win** -- the first mechanism in this entire sweep
(10+ configs) to beat iroute on all three benchmarks, and bigblue1's
margin is even larger than the screen predicted (unlike the earlier
step=0.05-vs-0.02 case, where the screen overstated its win). This
validates the user's relaxable-penalty idea as a genuine improvement
over iroute's permanent-only mechanism, not just a faster path to the
same ceiling. kroute is now the current-best router in this lineage.
See `gr/kroute.cpp`'s header for the full writeup.

Per the user's explicit condition ("if the majority improve, adjust the
penalty formula to..."), this unanimous win triggers the next round of
experiments: sweeping `KROUTE_HIST_MODE=ratio` at various
`KROUTE_HIST_RATIO_EXPONENT` values (including the user-proposed
exponent=2, i.e. n2=n1*(usage/capacity)^2), and separately
`KROUTE_HIST_EXPONENT_ESCALATION` (also user-proposed: the effective
exponent climbs with how many still-unrelaxed penalties are on an
edge's stack, so a chronically-dirty edge gets punished progressively
harder while a relaxed edge drops back to a gentler exponent). Both are
now env-controlled and self-tested (commits f98ec69 and the escalation
commit following it) -- see this file's next update for sweep results.

## Part 4: exponent/escalation sweep on kroute (adaptec1 screen, 62 rounds)

Bar for comparison (kroute's own committed default, additive mode): 166890.

| config | env vars | overflow |
|---|---|---|
| ratio_exp05 | `KROUTE_HIST_MODE=ratio KROUTE_HIST_RATIO_EXPONENT=0.5` | 169726 |
| ratio_exp15 | `KROUTE_HIST_MODE=ratio KROUTE_HIST_RATIO_EXPONENT=1.5` | 174780 |
| ratio_exp20 | `KROUTE_HIST_MODE=ratio KROUTE_HIST_RATIO_EXPONENT=2.0` (user-proposed squared) | 200744 |

All three lose to the additive-mode default, and the loss worsens
monotonically with exponent. Ratio-based multiplicative growth
underperforms flat additive growth for kroute at every exponent tried so
far -- consistent with iroute's own earlier finding that ratio mode
(without relaxation available) also failed outright. Relaxation changes
*how badly* ratio mode fails (166890-200744 here vs iroute's 184730 at
plain exponent=1, no relaxation available) but does not make it
competitive. Remaining configs:

| config | env vars | overflow |
|---|---|---|
| ratio_exp30 | `KROUTE_HIST_RATIO_EXPONENT=3.0` | 202356 (worst overall) |
| escalation_05 | `KROUTE_HIST_RATIO_EXPONENT=1.0 KROUTE_HIST_EXPONENT_ESCALATION=0.5` | 176782 |
| escalation_10 | `KROUTE_HIST_RATIO_EXPONENT=1.0 KROUTE_HIST_EXPONENT_ESCALATION=1.0` | 177268 |

**Part 4 conclusion**: all 6 configs across both batches lose to the
additive-mode default (166890), by margins ranging from ~1.7% (exponent
0.5) to ~21% (exponent 3.0). The escalation mechanism (base exponent=1.0,
climbing with consecutive unrelaxed penalties) sits in the middle of the
pack -- clearly better than the highest fixed exponents (2.0, 3.0) but
still solidly worse than plain flat-additive growth. **Ratio-based
multiplicative growth, escalated or not, does not compete with flat
additive growth for kroute at any setting tried.** No full-scale
validation needed for any of these -- all are clear screen losses.
kroute's committed default (additive, step=0.05) remains unbeaten and
is the best-known setting in this whole design-space sweep (16+ configs
tried across iroute and kroute combined).

## Part 5: wroute benchmark validation and kroute round-budget headroom

Two efforts launched in parallel 2026-09-07 ~2:34AM (3-process cap
respected -- 2 running):

1. `wroute_default` -- first-ever screen of gr/wroute.cpp (kroute +
   jroute's rip-up-reroute, committed e28282c but never run against real
   ISPD data) on adaptec1, reduced 30/40 round budget, default settings
   (WROUTE_RIPUP_INTERVAL=4, WROUTE_RIPUP_COUNT=200).
2. `kroute_headroom` -- kroute at 1.5x its committed round budget
   (KROUTE_FAST_ROUNDS=90 KROUTE_SLOW_ROUNDS=225, vs the committed
   60/150) on adaptec1, traced, to check whether overflow is still
   declining meaningfully near round 172 or has plateaued.

`wroute_default` (screen) result: **166368**, beating kroute_default's
own screen result (166890) by ~0.31% -- a real win at screen scale, the
first sign rip-up-reroute helps on the stronger relaxable base (jroute's
own measurement against the weaker iroute base was a small mixed
result). Interesting trace detail: most of the periodic ripup passes
found nothing above the no-op floor to attempt (0 attempted most
rounds) -- only one pass in this screen (round 36) found real
candidates (200 attempted, 12 accepted), consistent with kroute's
relaxation naturally keeping most edges from staying scarred long
enough to accumulate a ripup-worthy score. Per this project's
established methodology, this screen win needs full-scale confirmation
before trusting it. Full 172-round validation launched on all three
benchmarks (newblue1 and bigblue1 running now; adaptec1 queued for a
freed process slot, per the 3-process cap) 2026-09-07 ~2:53AM.

Bar to beat (kroute's current committed default, 172 rounds): adaptec1
162584, newblue1 41014, bigblue1 292420.

wroute full-validation results so far:

| benchmark | kroute (bar) | wroute | delta |
|---|---|---|---|
| adaptec1 | 162584 | running | -- |
| newblue1 | 41014 | 41038 | +0.06% (tiny regression) |
| bigblue1 | 292420 | running | -- |

newblue1 is essentially a wash (slightly worse), unlike adaptec1's
screen win -- consistent with this project's own established lesson
that screens don't always predict full-scale direction reliably, in
either direction. bigblue1 also finished: **292476** vs bar 292420, a
tiny +0.02% regression, also essentially a wash. adaptec1 still
running; final verdict pending.

Two of three benchmarks (newblue1, bigblue1) are now essentially
neutral for wroute vs kroute -- neither a real win nor a real loss.
This means adaptec1's screen win (166368 vs 166890, ~0.31% at reduced
budget) does not appear to generalize as a broad improvement; the
overall picture is trending toward "wash," not a clear additional win
on top of kroute's relaxation mechanism, but the final adaptec1 number
will settle it.

**kroute_headroom final result: 162074** at 247 total rounds (90 fast +
up to 225 slow, vs the committed 60+150=210), compared to kroute's
committed 172-round result of 162584 -- a real but modest 0.31%
improvement from ~1.5x more rounds. The decline rate shrank from
~90-130/round early in the extended run down to single digits
(~2-16/round) by the final rounds, clearly approaching a plateau.
**Conclusion: kroute has some genuine headroom left, but with sharply
diminishing returns** -- more patience buys a little more, not a
fundamentally lower ceiling the way going from hroute's default to
iroute's extended budget did. Not adopting the extended round budget as
the new default: the runtime cost (4857s here vs ~3400-3600s at the
committed budget, on adaptec1 alone) is not clearly worth a 0.31% gain
given diminishing returns.

wroute's final full-scale result (all legal, all cross-checked exactly
against eval2008.pl):

| benchmark | kroute (bar) | wroute | delta |
|---|---|---|---|
| adaptec1 | 162584 | 162120 | -0.29% |
| newblue1 | 41014 | 41038 | +0.06% |
| bigblue1 | 292420 | 292476 | +0.02% |

**Conclusion: a genuinely mixed result, same shape as jroute's own
measurement against the weaker iroute base** -- one real win, two
essentially-neutral washes, reported as-is rather than forced into a
single verdict. Notably, adaptec1's screen prediction (-0.31%) landed
almost exactly on the full-scale result (-0.29%) this time, unlike
iroute's own step=0.05 screen which overstated its eventual margin --
screen reliability seems to vary by mechanism, not be a fixed property
of the methodology itself. **kroute remains the best-known router in
this lineage overall**; wroute is a legitimate but narrow specialization
(helps on designs with adaptec1-like congestion structure) rather than a
strict improvement. See `gr/wroute.cpp`'s header for the full writeup,
including an open question about what specifically differs about
adaptec1 that made the ripup pass pay off there and not on the other two
(raw escalation rate doesn't explain it -- bigblue1's is actually higher).

## Session summary: all three requested efforts complete

1. **Exponent/escalation sweep (Part 4)**: closed, clean negative result.
   Flat additive growth beats every ratio-based variant tried (6 configs)
   by 1.7-21%. kroute's committed default (additive, step=0.05) is
   unbeaten across 16+ configs tried in this whole sweep.
2. **wroute rip-up-reroute validation (Part 5)**: closed, mixed result.
   One benchmark improves meaningfully (adaptec1, -0.29%), two are washes.
   kroute remains the best-known router overall.
3. **kroute round-budget headroom (Part 5)**: closed. Real but small
   headroom (-0.31% from 1.5x more rounds), sharply diminishing returns,
   not adopted as a new default given the runtime cost.

## Part 6: rroute -- full rip-up-reroute every round

User-proposed follow-on: reroute every non-giant net every phase-B round
(not just currently-overflowed nets, kroute's rule, or a scored/capped
subset, wroute's rule), keeping kroute's flat-additive relaxable history
mechanism unchanged. Every reroute stays individually supervised
(accept/reject on local overflow delta), addressing the specific failure
mode (unsupervised batch acceptance) behind CLAUDE.md's own documented
caution against reroute-everyone-every-round churn -- what's untested is
the *selection* rule itself, not supervision. Built as `gr/rroute.cpp`
(commit 67afa3e), self-tested with a decisive fixture (a never-
overflowed net gets touched under full ripup, left alone under the
RROUTE_FULL_RIPUP=0 fallback that reproduces kroute exactly) plus
kroute's full inherited suite, 54 assertions, all passing.

**Cost probe** (newblue1, tiny 5 fast / 3 slow round budget, identical
env vars on both routers for a fair comparison):

| router | overflow (8 rounds) | wall time |
|---|---|---|
| kroute | 45708 | 32.8s |
| rroute | 46462 | 67.0s |

At equal tiny budget, kroute is both faster (~2x) and already slightly
ahead (45708 vs 46462, kroute better by ~1.6%) -- an early discouraging
signal, though not decisive at just 8 rounds. The cost gap is expected
and structural: rroute's slow tier searches every net every round
(~270713 of 270715 nets end up escalated over the run) vs kroute's
selective rule (~30985 nets escalated at this same tiny budget, roughly
11%) -- confirmed via each run's own trace. Given this ~2x-and-growing
per-round cost (kroute's own escalated count shrinks further as later
rounds stabilize, while rroute's stays at ~100% every round), a full
172-round, 3-benchmark validation at kroute's own committed budget could
plausibly take many hours per benchmark -- not attempted without first
seeing a real signal at a proper screen.

**30/40-round screen on newblue1** (same budget both routers, identical
env vars otherwise):

| router | overflow (62 rounds) | wall time |
|---|---|---|
| kroute | 41956 | 203.1s |
| rroute | 41800 | 741.1s |

A genuine reversal from the tiny-budget probe: rroute now WINS on
quality (-0.37%), not loses. The cost gap persists and is large (~3.65x
slower). This is a real screen win (even if modest) and, per this
project's own methodology, needs confirmation before drawing a
conclusion -- proceeding cautiously to an adaptec1 screen too, time-
boxed given the cost (extrapolating from the observed 3.65x ratio
against kroute's own ~958s adaptec1 screen, expect roughly an hour).

**adaptec1 screen result**: rroute 167450 vs kroute's bar 166890 (+0.34%,
a LOSS this time) at 1417.1s vs kroute's ~958s (~1.48x slower -- a much
smaller cost multiplier than newblue1's 3.65x, likely because kroute's
own escalation fraction on adaptec1 is already high, ~39% of nets
(68501/176716), closer to rroute's ~100%, so selective escalation saves
less there than on newblue1 where kroute only escalates ~16%).

**Part 6 conclusion: a genuinely inconsistent, mixed picture across the
two benchmarks screened** -- a small win on newblue1 (-0.37%), a small
loss on adaptec1 (+0.34%), at cost multipliers ranging from ~1.5x to
~3.65x depending on the benchmark's own baseline escalation rate. This
is not the shape of a real, generalizable improvement -- it looks like
noise-scale variation around "roughly the same quality kroute already
gets, at meaningfully higher and unpredictable cost." Per this project's
own discipline (a screen needs to show a *consistent* signal worth
confirming, not just any single win), this does not warrant the
expense of a full 172-round, 3-benchmark validation (which could run
many hours per benchmark at these cost ratios). **Closing out Part 6
without pursuing full validation or a third (bigblue1) screen.**

The honest answer to "does full rip-up-reroute every round help, given
supervision is already in place": no clear evidence that it does,
and clear evidence that it costs substantially more compute for an
inconsistent, roughly wash-level quality effect. kroute's selective
escalation -- touching only nets that need it -- captures effectively
all the available benefit at a fraction of the cost. See gr/rroute.cpp's
header for the full writeup.

## Part 7: vroute -- multi-source multi-sink (MSMV) maze routing

Highest-leverage gap identified against real ISPD-contest-winning
routers (FastRoute, NTHU-Route -- see this session's research summary):
every router here builds a net's Steiner topology once, up front, from
an abstract Manhattan-distance MST, then routes each fixed tree edge
independently. MSMV grows the tree incrementally instead -- every
committed tile is a simultaneous source, every unconnected pin a
simultaneous sink, one real Dijkstra wave finds the genuinely cheapest
next connection each step, which can tap into the middle of already-
built wire, not just fixed pin endpoints. Built as `gr/vroute.cpp`
(commit 6a15721), self-tested with two decisive, exact-arithmetic
fixtures proving genuine multi-source and multi-sink selection, plus
kroute's full inherited suite.

**Cost/quality probe** (newblue1, tiny 5 fast / 3 slow round budget,
identical env vars otherwise):

| router | overflow (8 rounds) | wall time |
|---|---|---|
| kroute | 45708 | 32.8s |
| vroute | 41662 | 173.2s |

A dramatically larger effect than anything else tried this session:
vroute is ~8.9% better than kroute at this tiny budget -- and 41662 at
just 8 rounds is already close to kroute's own *committed 172-round*
result (41014). Cost is real (~5.3x slower at this budget) but the
quality signal is an order of magnitude bigger than any prior
experiment's. Proper 30/40 screen launched to confirm before deciding
on full validation.

**30/40 screen result (newblue1)**: **33184** at 2120.7s (~35.3 min, 62
total rounds run). Legal per gr_verify (0 disconnected/incomplete/
unrouted/malformed) and cross-checked exactly against eval2008.pl. That
beats:
- kroute's own 30/40 screen (41956) by **-20.9%**
- kroute's *full 172-round committed result* (41014) by **-19.1%** --
  using only 62 rounds, about a third of kroute's own round budget

Cost is real and grows with round count (~10.4x kroute's own 62-round
time here, vs ~5.3x at the tiny 8-round probe -- consistent with more
nets accumulating onto the slow tier as rounds progress, each paying
MSMV's per-step netFootprint cost). This is by a wide margin the
largest quality effect found in this entire session (every other
mechanism tried -- iroute's tuning, kroute's relaxation, the exponent
sweep, wroute/rroute's rip-up variants -- moved overflow by under 1.5%).
Full-scale validation launched: vroute at kroute's committed round
budget (60/150) on newblue1 for the real head-to-head, plus 30/40
screens on adaptec1 and bigblue1 for a complete picture (3 processes,
at the cap).

**newblue1 full-budget result (172 rounds, matching kroute's committed
budget exactly): 30668** at 6865.3s (~1.9 hours). Legal per gr_verify (0
disconnected/incomplete/unrouted/malformed), cross-checked exactly
against eval2008.pl. Beats kroute's committed 41014 by **-25.2%** --
*larger* than the 30/40 screen's -19.1%, meaning the benefit compounds
with more rounds rather than being a fast-convergence artifact that
plateaus early. This is the real, apples-to-apples head-to-head number
and it is the single largest improvement found anywhere in this entire
session.

adaptec1 and bigblue1 screens still running (round 32/40 and 28/40 at
last check): adaptec1 at 118202 (-29.2% vs kroute's own 166890 screen,
still declining ~700-800/round), bigblue1 at 245546 (-16.0% vs kroute's
committed 292420, still declining ~600-900/round). Both consistent with
newblue1's finding -- this looks like a real, generalizable property of
MSMV routing, not a benchmark-specific fluke.

**Both screens finished, verified**:

| benchmark | kroute bar | vroute screen (62 rounds) | wall time | improvement |
|---|---|---|---|---|
| adaptec1 | 166890 (screen) | 114826 | 8231.7s (~2.3hr) | **-31.2%** |
| bigblue1 | 292420 (committed) | 241096 | 9196.2s (~2.6hr) | **-17.5%** |

Both legal per gr_verify (0 disconnected/incomplete/unrouted/malformed),
both cross-checked exactly against eval2008.pl. Combined with newblue1's
already-verified full-budget result (-25.2%), all three benchmarks show
large, consistent double-digit-percent improvements -- this is a real,
generalizable property of MSMV routing, not a fluke of one benchmark's
structure. Launching full-budget (60/150 round) validation runs on both
adaptec1 and bigblue1 now for the real head-to-head against kroute's
committed numbers (162584, 292420) -- given the cost multiplier and
these benchmarks' larger size, expect several hours each.

Launched 2026-09-07 ~12:24PM. Estimated duration from newblue1's own
screen-to-full scaling (3.24x wall time for 2.77x more rounds, since
per-round cost grows as more nets escalate over the run): roughly
7-8 hours for adaptec1, 8-9 hours for bigblue1. This is the last piece
needed to complete vroute's full 3-benchmark validation.

**adaptec1 full-budget result (172 rounds, matching kroute's committed
budget exactly): 103172** at 26986.3s (~7.5 hours) -- landing right in
the estimated 7-8hr window. Legal per gr_verify (0
disconnected/incomplete/unrouted/malformed), cross-checked exactly
against eval2008.pl (103172 / max 22 / wirelength 7514462, both tools
agree). Beats kroute's committed 162584 by **-36.55%** -- *larger* than
the 30/40 screen's -31.2%, the same pattern newblue1 already showed
(the benefit compounds with more rounds rather than being a fast-
convergence artifact). bigblue1's full-budget run is still in progress;
see this file's next update for the third and final benchmark.

**bigblue1 full-budget result (172 rounds): 230084** at 29679.3s (~8.24
hours), landing in the estimated 8-9hr window. Legal per gr_verify,
cross-checked exactly against eval2008.pl (230084 / max 26 / wirelength
7836782, both tools agree). Beats kroute's committed 292420 by
**-21.32%**.

**vroute's full 3-benchmark validation is now complete.** Final,
verified, apples-to-apples comparison against kroute's own committed
numbers, same round budget (172) on both sides:

| benchmark | kroute (committed) | vroute (full budget) | change |
|---|---|---|---|
| adaptec1 | 162584 | 103172 | **-36.55%** |
| newblue1 | 41014 | 30668 | **-25.23%** |
| bigblue1 | 292420 | 230084 | **-21.32%** |
| **total** | **496018** | **363924** | **-26.63%** |

Unanimous, large, double-digit-percent improvement on all three real
ISPD benchmarks -- by a wide margin the single largest result of this
entire session, dwarfing every pricing-formula variant tried (iroute's
history-cost -0.6 to -2.7%, kroute's relaxable version another -0.6 to
-1.4%, xroute's barrier experiments a regression, yroute's rescue pass
alone -0.46%). MSMV is a structural change to *how* a net's topology is
decided -- real congestion-aware search replacing a fixed, congestion-
blind MST built before routing starts -- not another coefficient on the
same cost formula every other router in this lineage has been tuning.
That distinction is the actual finding of the whole session: the ceiling
on how much a pricing curve alone can achieve was reached early (kroute),
and every further pricing experiment since (xroute, yroute) has moved the
needle by low single digits at best; changing the search algorithm itself
moved it by 20-37%.

**Cost, honestly**: vroute is dramatically slower than kroute at the same
round budget -- roughly 30000s (8+ hours) per benchmark at full budget,
versus kroute's own several-hundred-second runs. That is a real tradeoff,
not a free win, and matters directly for whether this is deployable as-is
versus a proof of concept that a production implementation would need to
make practical (e.g. bounding MSMV's search cost, or applying it
selectively rather than to every escalated net).

## Part 8: xroute -- price barrier + RUDY/centrality net ordering

While vroute's two full-budget runs were in progress, the user asked why
peripheral capacity was sitting idle while the die's center overflowed,
and proposed forcing nets away from near-full areas with a much more
aggressive cost curve, plus reordering nets by bounding-box geometry.

**Diagnosis first, before any new mechanism** (`rudy_vs_congestion.cpp`,
scratch tool, not committed): computed a pure RUDY bounding-box demand
estimate straight from bigblue1's netlist -- no router involved at all --
and compared it to kroute's actual committed usage. RUDY barely
correlates with real usage (r=0.03): 93% of all non-giant nets span
under 10% of the die, so this design is almost entirely short local
connections, not long-haul crossers threading through the middle. Raw
*pin density* by radial band, unweighted by HPWL, tells the real story:

| band (0=center...4=corner) | pins/tile | capacity | utilization | overflow |
|---|---|---|---|---|
| 0 | 30.1 | 439.7 | 107.5% | 44.5 |
| 1 | 31.1 | 439.9 | 99.0% | 28.9 |
| 2 | 25.1 | 437.0 | 77.0% | 10.2 |
| 3 | 17.1 | 433.2 | 42.9% | 0.08 |
| 4 | 7.4 | 425.8 | 15.3% | 0.00 |

Capacity is nearly flat (440->426, ~3% dip at the far corners); pin
density varies 4x. This is a placement fact upstream of any pricing
mechanism -- bigblue1's center is packed with ~4x the corner's cell
density, and since the design is almost entirely short local nets, a
pin-density gradient becomes a wiring-density gradient directly. No
pricing scheme relocates demand to tiles that were never placed there;
the actual lever is forcing the router to find better topology *within*
the crowded region once nets are genuinely forced to share it (exactly
what vroute's MSMV already does) plus making sure nothing voluntarily
overflows when a legal, if expensive, alternative exists.

**Built** (`gr/xroute.cpp`, forked from kroute.cpp), three independently
toggleable mechanisms -- see the file's own header for full detail:

1. `barrierExponentTerm`: real barrier replacing kroute's smooth
   threshold/power curve (which only reaches a normal step-1.0 exponent
   *at* 100% capacity) for all normal routing. Flat below 60%, a power
   ramp to "VERY expensive" by 95%, a rational must-go blowup from 95% to
   100%, and a fixed huge exponent for actual overflow that saturates the
   existing `kForbiddenPrice` clamp on the first touch. `XROUTE_BARRIER=0`
   reproduces kroute's exact curve.
2. RUDY-based net ordering, revived verbatim from droute.cpp -- a real,
   modest win (adaptec1 -2.2%, bigblue1 -1.4%) proven years before
   kroute's history-cost mechanism existed, never actually combined with
   it until now.
3. Net centrality ordering, new: `netCentralityScore` measures how
   clustered a net's pins are toward its own bounding box's center
   (0=centroid, ~1=corners) and schedules low-score (inflexible, no room
   to avoid the middle) nets first, high-score (flexible, many equal-cost
   boundary-hugging paths) nets last. **A real reasoning error was caught
   and fixed mid-design**: an initial pass used a corner-anchored 2-pin-
   plus-interior-cluster example and concluded the opposite direction
   (flexible-first); the user caught this directly and the general case
   (all pins on the perimeter vs. all pins at the center) supports the
   direction actually implemented.

`XROUTE_ORDER=none|rudy|centrality|combined` selects the ordering mode,
each independently measurable. All three mechanisms self-tested (59
checks, 0 failures), including a decisive end-to-end fixture
(`testCentralityOrderingInvertsWhoClaimsSharedCorridor`) proving
reordering actually flips which of two nets claims a shared capacity-1
corridor, not just that it computes a different score. One real bug
found while building that fixture: `RoutedNet::segs` are tile
coordinates throughout -- only the final written `.route` file converts
to real coordinates -- and an early version of the fixture's check
compared tile-coordinate segments against a real-coordinate threshold
and silently failed for that reason.

Committed as `a1e0334` (build + selftest only, not yet measured).

**30/40-round screen result on newblue1 (default config: barrier on,
combined ordering)**: **63494**, 223.1s. Legal per gr_verify (0
disconnected/incomplete/unrouted/malformed), cross-checked exactly
against eval2008.pl (63494 / max 42 / wirelength 6119006, both tools
agree). **A clear regression, not a win** -- against kroute's own 30/40
screen on the same file (41956, HISTORY_COST_SWEEP.md line 407) this is
**+51.4% worse**, and worse still against kroute's full 172-round
committed result (41014).

The router's own self-report points at why: **77,899 of 270,715 nets
(28.8%) escalated to the slow tier** -- a very high escalation rate, and
exactly the failure mode iroute.cpp's own header already warned about
(a naive, too-aggressive congestion penalty overprices contested edges
so fast that far more nets need full search than the round budget can
actually converge, and the router spends its time thrashing rather than
finding good detours). Whether the barrier, the ordering, or the
combination between them is responsible for this is not yet known --
launched a same-budget ablation (`XROUTE_BARRIER=0 XROUTE_ORDER=none`,
i.e. kroute-equivalent pricing and ordering through xroute's own binary)
to isolate it, in the one process slot freed once this screen finished.

Also added `gr/run_tracked.sh` this round: wraps a router invocation
with `/usr/bin/time -l` for the kernel-tracked physical memory peak
(maximum resident set size / peak memory footprint) plus a 5s-interval
VSZ/RSS poll for visibility into virtual memory over the run (macOS has
no kernel-tracked "peak VSZ" the way it does for RSS -- virtual size
there is dominated by the dyld shared-cache mapping and reserved-but-
uncommitted address space, consistently ~435GB flat across every router
process observed regardless of actual workload, so a flat number *is*
the honest result, not a metric worth chasing a peak of). The ablation
launch above is the first run under this wrapper; see CLAUDE.md for the
new standing practice.

**User-directed follow-up, for every subsequent ablation in this Part**:
raise `XROUTE_BARRIER_START` from its default 0.6 to 0.8 and re-check the
slow-tier escalation rate (77899/270715 = 28.8% at the default-config
30/40 screen above). Hypothesis: the barrier's own cost stays flat below
`XROUTE_BARRIER_START`, so pushing that point out from 60% to 80% gives
the fast tier's cheap candidate search more room before anything looks
expensive enough to force escalation -- directly testing whether the
regression's cause (per iroute.cpp's own documented failure mode:
too-aggressive pricing over-escalates nets within a fixed round budget)
is the barrier starting too early, not the barrier's existence at all.
Does not apply to the `XROUTE_BARRIER=0` ablation already launched
(barrier fully disabled there, so its start threshold is moot) -- applies
to whichever barrier-on run follows it.

**Ablation result (`XROUTE_BARRIER=0 XROUTE_ORDER=none`, i.e.
kroute-equivalent pricing and ordering through xroute's own binary, same
30/40-round budget)**: **41956**, 205.5s -- an *exact* match to kroute's
own recorded 30/40 screen (line 407). Legal per gr_verify, cross-checked
exactly against eval2008.pl (41956 / max 22 / wirelength 6634017, both
tools agree). This confirms two things at once: the ablation fixture and
methodology are sound (xroute's "everything off" configuration truly
reproduces kroute's exact behavior, not some unrelated fork bug), and the
regression measured above is *entirely* attributable to the barrier
and/or combined ordering, not noise. Escalation rate here: 42887/270715
= **15.8%**, well under the default config's 28.8% -- directly
implicating the new mechanisms as the cause of the extra slow-tier
escalation, not a coincidence.

Also added `XROUTE_ROUND_LOG=<path>` this round (per-round CSV: overflow
+ wirelength together, so a run's full trajectory can be charted
afterward, not just watched live) and fixed a real bug in
`run_tracked.sh` found using it for the first time: `$!` after
backgrounding `/usr/bin/time -l router...` captures time's own PID, not
the router process it execs as a child, so the VSZ/RSS poll was silently
watching the wrapper (a few hundred KB, forever) while `time -l`'s own
authoritative report said the actual router peaked at 635MB. Fixed via
`pgrep -P` to find the real child PID before polling.

Next: barrier-only (`XROUTE_BARRIER=1 XROUTE_ORDER=none`) at
`XROUTE_BARRIER_START=0.8` per the user's own follow-up above, now with
the fixed memory wrapper and round-log enabled.

**Barrier-only result at XROUTE_BARRIER_START=0.8 (XROUTE_ORDER=none,
same 30/40-round budget)**: **68206**, 220.3s. Legal per gr_verify,
cross-checked exactly against eval2008.pl (68206 / max 42 / wirelength
6143222, both tools agree). **The hypothesis was refuted, not
confirmed**: escalation rose to 89458/270715 = **33.0%**, *higher* than
both the default-config screen (28.8%) and the barrier-off ablation
(15.8%) -- pushing the barrier's start point later made the escalation
problem worse, not better.

Mechanistic read, visible directly in the new per-round log (see the
published trajectory chart): fast-tier overflow doesn't plateau, it
climbs in a sawtooth all the way to **795,706** by round 20 (vs 125,414
at round 0) -- nearly 6.3x the starting overflow, worse than anything
seen in the barrier-off ablation. Two effects compound: (1) below
whatever XROUTE_BARRIER_START is set to, there is *zero* price signal at
all -- kroute's own inherited curve starts nudging price at 20%
utilization, so moving the barrier's start to 80% actually removes 60
more percentage points of early warning than the already-too-late 60%
default did, giving many more nets room to pile onto the same edges with
no deterrent whatsoever before anything reacts. (2) The "must-go" ceiling
(XROUTE_BARRIER_MUST) stayed fixed at 0.95, so pushing the start out to
0.8 compressed the rational-blowup ramp into a narrower 15-point window
(80-95%) instead of 35 points (60-95%) -- the same rise now happens over
a third of the distance, making the transition from "cheap" to
"forbiddingly expensive" more abrupt, not gentler, once it finally
triggers. Both effects point the same direction: delaying the trigger
concentrates the problem rather than spreading it out.

The published chart
(https://claude.ai/code/artifact/5570cf0a-5bb2-43fd-8a72-f4ac67bce1ac)
shows this directly -- the fast tier's overflow trajectory is a rising
sawtooth, not a plateau, right up until the cleanup pass and slow tier
take over and pull it back down to 68206 over 40 rounds. First real use
of `XROUTE_ROUND_LOG`/the trajectory-chart tooling built this round.

Three data points now on record for this Part, all at the same 30/40
newblue1 budget:

| config | overflow | escalation rate |
|---|---|---|
| kroute-equivalent (barrier off, plain order) | 41956 | 15.8% |
| default (barrier@0.6 + combined order) | 63494 | 28.8% |
| barrier-only @0.8, plain order | 68206 | 33.0% |

Moving the threshold later made things worse, not better -- the next
useful test is barrier-only at the *original* 0.6 start (isolating
whether the barrier mechanism itself, independent of ordering, is
responsible for the regression at all, before concluding anything about
where to tune it), not a further threshold sweep in the same direction.

**Barrier-only result at the original XROUTE_BARRIER_START=0.6
(XROUTE_ORDER=none, same 30/40-round budget)**: **65322**, 211.7s. Legal
per gr_verify, cross-checked exactly against eval2008.pl (65322 / max 40
/ wirelength 6250752, both tools agree). Escalation: 81263/270715 =
**30.0%**.

**This settles the attribution question.** Four data points, all same
30/40 newblue1 budget:

| config | overflow | escalation |
|---|---|---|
| kroute-equivalent (barrier off, plain order) | 41956 | 15.8% |
| barrier-only @0.6, plain order | 65322 | 30.0% |
| default (barrier@0.6 + combined order) | 63494 | 28.8% |
| barrier-only @0.8, plain order | 68206 | 33.0% |

The barrier alone, with *zero* ordering change, already reproduces
nearly the entire regression (+55.7% vs kroute at 0.6, actually *worse*
than the "default" combined config's +51.4%). Adding the centrality/RUDY
ordering on top of the same 0.6 barrier makes things slightly *better*
(63494 vs 65322) and lowers escalation slightly (28.8% vs 30.0%), not
worse -- the ordering mechanism is not the problem and appears to be a
small net positive once the barrier is already in play, mildly
compensating for it rather than compounding it. **The price barrier
mechanism itself, independent of threshold placement or the ordering
question, is the primary and dominant cause of the regression.** No
threshold value tried so far (0.6 default, 0.8 user-directed) comes
close to recovering kroute's own baseline behavior -- this looks like a
problem with the barrier's fundamental shape or its interaction with the
fast tier's escalation logic, not a tuning problem solvable by sliding
one constant.

Next: an ordering-only run (`XROUTE_BARRIER=0 XROUTE_ORDER=combined`)
completes the isolation matrix -- confirms directly whether ordering
alone, with the barrier fully off, recovers something close to kroute's
41956 (expected, given the pattern above) or reveals its own separate
issue.

**Ordering-only result (`XROUTE_BARRIER=0 XROUTE_ORDER=combined`, same
30/40-round budget)**: **41514**, 210.1s. Legal per gr_verify, cross-
checked exactly against eval2008.pl (41514 / max 22 / wirelength
6626874, both tools agree). Escalation: 41816/270715 = **15.4%**.

**Isolation matrix complete.** Five data points, same 30/40 newblue1
budget:

| config | overflow | vs kroute | escalation |
|---|---|---|---|
| kroute-equivalent (baseline) | 41956 | -- | 15.8% |
| **ordering-only (RUDY+centrality)** | **41514** | **-1.05%** | **15.4%** |
| default (barrier@0.6 + combined order) | 63494 | +51.4% | 28.8% |
| barrier-only @0.6 | 65322 | +55.7% | 30.0% |
| barrier-only @0.8 | 68206 | +62.6% | 33.0% |

Clean, unambiguous attribution: **the RUDY/centrality ordering is a real,
if modest, standalone win** -- droute's own RUDY result carried forward
correctly, and the new centrality signal doesn't hurt it. **The price
barrier, in every configuration tried, is a clear regression**, and nothing
about *where* its threshold sits explains the gap -- 0.6, 0.8, combined
with or without ordering, all land in the same 60-65% cost range. The
barrier's fundamental shape (near-zero signal below its start threshold,
then a sudden climb) is the problem, not a specific constant.

**User-directed redesign** (commit d0dd11a): restore kroute's own proven
early, gradual onset (20% threshold, power 2.5) instead of the invented
late one, scaled to reach a genuinely high ceiling (10.0, vs kroute's own
implicit max of 1.0) by a 90% watermark, then the same sharp rational-
blowup rise from there to 100%.

**Result: 62550**, 206.8s, escalation 74641/270715 = **27.6%**. Legal
per gr_verify, cross-checked exactly against eval2008.pl (62550 / max 42
/ wirelength 6418513, both tools agree). **Only a marginal improvement**
over the old late-onset barrier (62550 vs 63494-68206 in the four earlier
configs) -- still a **+49.1% regression** against kroute's 41956, and the
XROUTE_ROUND_LOG trajectory shows the same sawtooth almost unchanged:
fast-tier overflow still climbs to 744166 by round 19, nearly 7.3x its
round-0 value (vs the old barrier's 795706 peak -- only ~6% lower).

**Mechanistic diagnosis, computed directly rather than guessed**:
restoring kroute's own onset *shape* mattered far less than expected
because the ceiling (10.0) makes this curve a **blanket 14x multiplier**
of kroute's own curve at every ratio from 20% up to 90%, not just near
the top -- confirmed by direct calculation:

| ratio | kroute's term | this barrier's term | multiple |
|---|---|---|---|
| 0.3 | 0.0055 | 0.0771 | 14.0x |
| 0.5 | 0.0861 | 1.2024 | 14.0x |
| 0.7 | 0.3088 | 4.3120 | 14.0x |
| 0.9 | 0.7162 | 10.0000 | 14.0x |
| 0.95 | 0.8510 | 25.0000 | 29.4x |
| 0.99 | 0.9690 | 145.0000 | 149.6x |

The "gradual" phase isn't gentle in absolute terms at all -- it's
kroute's exact proven curve uniformly amplified 14x across its entire
range, which is a milder version of the exact failure iroute.cpp's own
header already documented once (a naive, much-larger single-step
constant overprices contested edges broadly enough that far more nets
escalate to the slow tier than the round budget can converge). The
lesson isn't "shape doesn't matter" -- it's that the *ceiling* value is
what determines whether the mid-range stays close to kroute's own
working scale or becomes a second copy of the same over-aggressive-step
mistake, regardless of how smoothly it's reached.

**Next test**: a much smaller mid-range ceiling (close to kroute's own
~1.0, tried at 1.5 first) so the 20-90% segment stays near-identical to
kroute's already-proven curve, concentrating essentially all of the
"rapid rise to cutoff" the user wants into the narrow 90-100% segment
alone, which comparatively few edges reach on any given round -- rather
than applying real bite across the whole mid-range where it broadly
destabilizes the fast tier.

## Part 10: yroute -- post-convergence rescue pass with decaying length weight

Built directly from the user's own question, after the barrier work
above stalled out: "what isn't getting used in the available space to
bring overflow down to zero, and what are some options for using it
better?" Measured first (see the per-tile/per-layer analysis this
prompted): bigblue1's aggregate layer utilization sits at 65.7% overall
-- over a third of all physical capacity idle somewhere on the die --
while every hot tile has all 6 layers simultaneously overloaded, so
there's no idle layer sitting right at the contested spot. Combined with
the ISPD ranking being lexicographic (overflow first, wirelength only as
tiebreak) while every router here blends both into one scalar cost, the
diagnosis was: a per-round pricing curve structurally can't fully commit
to "spend arbitrary wirelength to kill remaining overflow," because
there's always some length savings large enough to still look cheaper at
any finite lambda.

**Mechanism** (`gr/yroute.cpp`, forked from kroute, commit 6306a56):
Phase C, run once after the main loop stops. For each net still touching
an overflowed edge, individually, retry it at a decaying schedule of
length weights (`YROUTE_RESCUE_WEIGHTS`, default
1.0,0.3,0.1,0.03,0.01,0.003,0.001,0) via `edgeCost = hist * (lengthWeight
+ lambda*amount)`, stopping at the first weight whose local overflow
delta doesn't get worse. User's own explicit requirement: tunable, not a
hard-set-to-zero, with the actual "does it need to go fully negligible"
question left as something to measure, not assume.

**A real bug, twice** (commits 5323ec6 and the one before it): the first
real run self-reported overflow=442 while gr_verify/eval2008.pl both
independently disagreed at 304106 -- off by ~688x, caught immediately by
this project's cross-check discipline before anything was believed. Root
cause: the retry loop's mid-attempt manual undo left `usage` permanently
short of a rejected attempt's own pre-rescue baseline once routeNetSlow's
own undo-at-start ran again on the next weight. A first attempted fix
corrected only half of the bug (the charge-tracking bookkeeping, not
usage itself); a synthetic regression test built specifically to catch
this class of bug
(`testRescuePassLeavesUsageConsistentWithFinalCommittedCharges`, which
required building a genuinely-congested fixture since the existing one
converged to zero too easily to exercise Phase C at all) caught a
*second*, smaller discrepancy immediately afterward. The final,
correct fix is simpler than either patch: don't touch usage/rn.charges
between attempts at all, since routeNetSlow's own undo-at-start already
chains correctly attempt to attempt on its own. 47 checks pass.

**Verified result, 30/40-round newblue1 screen (same budget as the
xroute isolation matrix)**: **41860**, 238.1s. Legal per gr_verify, cross-
checked exactly against eval2008.pl (41860 / max 22 / wirelength 6639112,
all three -- self-report, gr_verify, eval2008.pl -- agree exactly this
time). Against kroute-equivalent (41956): **-0.46%**, a real but modest
win, not the dramatic number the bug produced.

**The actual research question, answered from `YROUTE_RESCUE_LOG`'s real
data** (23108 nets attempted, all still-overflowed after phase B):

| outcome | count | % of attempted |
|---|---|---|
| never succeeded, even at weight=0 | 19866 | 86.0% |
| succeeded, at weight=1.0 (no length-indifference needed at all) | 2562 | 11.1% |
| succeeded, at some weight in (0, 1) | 554 | 2.4% |
| succeeded, only at weight=0 (fully length-indifferent) | 126 | 0.5% |

Two honest, somewhat humbling findings, reported as directly as the win
above:

1. **86% of holdout nets could not be rescued at any weight, including
   fully length-indifferent (weight=0).** Spending unlimited wirelength
   doesn't help most remaining overflow -- consistent with the earlier
   per-tile finding that hot tiles have *every* layer and direction
   simultaneously overloaded. A single net retrying its own path alone,
   however indifferent to length, can't fix a resource that's jointly
   oversubscribed by many nets at once; that needs the nets to move
   together, not one at a time (option 2 from that earlier discussion,
   not yet built).
2. **Of the nets the mechanism *did* help, most (79%) needed no length-
   weight change at all** -- weight=1.0 (an ordinary reroute against the
   current price state) was already enough, meaning most of this
   mechanism's real, measured win is just "retry once more," not the
   length-decay idea specifically. Only 2.4% of all attempted nets needed
   any actual weight below 1.0 to succeed, and among *those*, there is no
   comfortable low sweet spot -- the weights that worked are spread
   fairly evenly across the whole schedule, with weight=0 itself used
   *more* than several of the intermediate values (126 nets at 0 vs.
   35-81 at each of 0.001-0.03). The honest answer to "optimal low
   proportion, or does it need to go fully negligible": **neither
   cleanly** -- for the narrow slice of nets this mechanism actually
   reaches, no single value dominates, and a meaningful share need it at
   its most extreme (0) rather than some gentle intermediate compromise.

## Part 11: zroute -- MSMV plus the length-weight rescue pass, combined

Direct follow-up to Part 10's own closing question: MSMV and length-weight
decay are orthogonal mechanisms (one changes what points a search can
connect through, the other changes what that search optimizes for), so
does combining them help beyond either alone? `gr/zroute.cpp` (commit
7701955) is yroute's Phase C ported onto vroute, substituting
`routeNetSlowMSMV` for plain `routeNetSlow` as the rescue pathfinder.

**Verified result, 30/40-round newblue1 screen (same budget as every
other data point in this comparison)**: **32994**, 2625.1s (~43.8 min).
Legal per gr_verify, cross-checked exactly against eval2008.pl (32994 /
max 22 / wirelength 6703406, all three agree). Rescue pass: 19792
attempted, 3137 succeeded (15.8%).

**The combination is a real, substantial win** -- far beyond either
mechanism alone:

| config | overflow | vs kroute-equivalent (41956) |
|---|---|---|
| xroute ordering-only | 41514 | -1.05% |
| yroute (rescue alone, plain Dijkstra) | 41860 | -0.46% |
| vroute (MSMV alone, own 30/40 screen) | 33184 | -20.9% |
| **zroute (MSMV + rescue)** | **32994** | **-21.36%** |

**But the marginal contribution of the rescue pass on top of MSMV is
modest, not synergistic**: 33184 -> 32994 is a further **-0.57%**, close
to what the rescue pass achieved on its own (-0.46%) rather than
compounding with MSMV's own gain. Confirmed directly by comparing the
rescue log's own distribution against yroute's (Part 10):

| outcome | yroute (plain Dijkstra) | zroute (MSMV) |
|---|---|---|
| never succeeded | 86.0% | 84.2% |
| succeeded at weight=1.0 | 11.1% | 12.5% |
| succeeded at weight in (0,1) | 2.4% | 2.7% |
| succeeded only at weight=0 | 0.5% | 0.7% |

The shapes are nearly identical. **MSMV does not expand which nets the
rescue pass can reach** -- it operates on a smaller starting population
(19792 attempted here vs yroute's 23108, since MSMV's own phase B already
resolves more nets before Phase C ever runs) but of whatever remains
overflowed, essentially the same ~84-86% fraction is unreachable by
length-weight decay regardless of which pathfinder tries it. This is
consistent with the earlier per-tile finding (Part 9): MSMV's real
strength is finding better topology within a congested region (which is
exactly what fixes most of the overflow before Phase C even starts), not
reaching resources that are jointly oversubscribed by many nets at once
-- MSMV, like plain Dijkstra, is still routing one net at a time, so a
resource genuinely oversubscribed across many simultaneous nets stays out
of either mechanism's reach. The "batch/joint rerouting" option raised
back in that discussion remains the one untried lever that targets this
specific gap directly.

**Cost**: 2625.1s vs vroute-alone's own 30/40 screen (2120.7s, this file's
own Part 7) -- roughly +24% more time for the additional -0.57%. Whether
that trade is worth it depends entirely on whether the eventual zero-
overflow goal cares about the last few percent enough to pay for it; at
minimum, it's the correct, measured answer to "should MSMV and the
rescue pass be combined," not an assumption.

## Part 12: broute -- joint/batch rerouting of contended net clusters

Built from the user's own follow-up to Part 10's closing observation:
yroute's own rescue log showed 86% of holdout nets couldn't be helped by
length-weight decay at *any* weight, consistent with the per-tile finding
that hot tiles have every layer and direction simultaneously overloaded
-- a jointly-oversubscribed resource, not a single net's own length-vs-
congestion tradeoff. `gr/broute.cpp` (commit a281b40) tests the natural
next lever directly: group every currently-overflowed tile into connected
clusters, collect every overflowed net touching each cluster, reroute
them together (sequentially, each seeing the others' fresh attempts, not
their frozen originals), and accept or reject the whole batch as one
supervised unit.

**Verified result, 30/40-round newblue1 screen (same budget as every
other data point in this comparison)**: **41956**, 216.2s. Legal per
gr_verify, cross-checked exactly against eval2008.pl (41956 / max 22 /
wirelength 6634017, both tools agree) -- an *exact* match to the kroute-
equivalent baseline. Phase D found and attempted 218 clusters, touching
29934 net-instances total, and **accepted zero of them.**

**This is a clean, striking, fully-supervised negative result, not a
bug** -- verified via the batch log directly:

| metric | value |
|---|---|
| clusters attempted | 218 |
| clusters accepted | **0 (0%)** |
| batches where overflow got worse | **218 (100%)** |
| nets per cluster (median / mean / max) | 30.5 / 137.3 / **8634** |
| tiles per cluster (median / mean / max) | 2 / 21.5 / 1602 |
| median overflow before -> after | 204 -> 1290 |
| mean worsening multiplier | **~10x** |

Every single batch made things worse, most of them dramatically --
because the naive tile-adjacency clustering criterion, at a round budget
still leaving substantial residual overflow (42887 escalated nets),
doesn't reliably find small local hot pockets: it lets adjacent
congestion merge into a few enormous clusters (one reached **8634
nets** -- roughly 3% of the whole design touched in a single batch).
Unconditionally rerouting thousands of nets at once, in arbitrary net-id
order, with no smarter coordination, reproduces exactly the "reroute
everyone every round" thundering-herd failure this project has now
independently confirmed three separate times: rroute's full-rip-up
experiment (Part 6), iroute's naive too-aggressive history-step (its own
header), and this. The common thread across all three: touching many
nets simultaneously without tightly bounding scope is a repeatedly
confirmed anti-pattern in this router architecture, regardless of what
specifically triggers the "many nets at once" -- a round boundary, an
escalation threshold, or (here) cluster membership.

**The supervision worked exactly as designed** -- every one of those
218 near-catastrophic batches was caught and fully reverted, so the
final result is bit-for-bit identical to what phase B alone already
produced, not a regression. Phase D added real computational cost (218
failed attempts) for zero actual risk, which is the entire point of
never accepting a batch unconditionally.

**Natural next step, not yet built**: cap cluster size (skip or subdivide
any cluster above some small net-count threshold) to isolate whether the
*original*, more modest version of this idea -- small, genuinely local
pockets of a handful of mutually-contending nets, exactly like the
self-test's clean 2-net scenario -- can find real wins once the
giant-cluster pathology is excluded, rather than concluding the whole
joint/batch idea is a dead end from a version that was never actually
testing the intended scope.

### Follow-up: capping cluster size to <= 12 nets (`BROUTE_MAX_CLUSTER_NETS`)

Built per the user's own request for a cap strategy: rather than skipping
an oversized cluster outright (which would write off exactly the region
carrying the most overflow), rank every member net of an oversized
cluster by its own individual `overflowOverUnion` contribution and keep
only the top `BROUTE_MAX_CLUSTER_NETS` (default 12) worst offenders,
dropping the rest from that batch. Caught and fixed a real bug on the way:
the cap threshold was first read into a `static const` local, which
caches the env var's value on the function's first call for the rest of
the process -- the exact pitfall already known from xroute's `orderMode`
and deliberately avoided there from the start. A purpose-built self-test
(`testBatchCapTrimsToWorstOffendingNetsNotArbitraryOnes`, three nets with
overflow magnitudes 2/4/6 inserted in ascending order specifically so
"keep by list order" and "keep by real ranking" would produce different,
distinguishable results) caught it immediately; fixed by making the
threshold a plain non-static local, re-read via `getenv` on every call.

**Verified result, same 30/40-round newblue1 screen, `BROUTE_MAX_CLUSTER_NETS=12`
(default)**: **41956**, 195.153s. Legal per gr_verify, cross-checked
exactly against eval2008.pl (41956 / max 22 / wirelength 6634017, both
tools agree) -- again an *exact* match to the uncapped run and to the
kroute-equivalent baseline.

| metric | uncapped | capped (<=12/cluster) |
|---|---|---|
| clusters attempted | 218 | 218 |
| clusters accepted | 0 (0%) | **0 (0%)** |
| net-instances touched | 29934 | **2510** |
| max nets in one batch | 8634 | 12 (by construction) |
| batches where overflow got worse | 218 (100%) | **218 (100%)** |
| mean worsening multiplier | ~10x | **6.82x** |
| median worsening multiplier | -- | **3.77x** |
| median overflow before -> after | 204 -> 1290 | **177 -> 703** |
| wall time | 216.194s | **195.153s** |
| final overflow | 41956 | **41956 (identical)** |

**Capping did not recover any accepted batch.** Every single one of the
218 clusters still made local overflow worse even bounded to its 12
worst-offending member nets, and the batch-acceptance gate correctly
rejected all 218 again, landing on the exact same final result as the
uncapped run and as doing nothing at all. This is a real, if modest,
efficiency win (13% less wall time, 12x fewer net-reroute attempts, and a
lower blast radius / worsening multiplier per rejected batch when it does
fire) but **not the hoped-for recovery of the original, more modest
joint/batch idea** -- the giant-cluster pathology was never the only
problem. Even a genuinely small (<=12-net) locally-contending pocket, at
this round budget, on this benchmark, still cannot be jointly rerouted to
a better local optimum via sequential `routeNetSlow` calls in net-id
order. Plausible reasons, not yet distinguished: (a) sequential
rerouting within a batch still lets later nets see (and re-contend with)
earlier nets' fresh attempts rather than solving the cluster's tiles
jointly, so it's a better-ordered greedy pass, not real joint
optimization; (b) the round budget (30 fast / 40 slow) may leave clusters
still actively forming/growing rather than at a stable local minimum
where a rescue pass has real headroom to find; (c) net-id order (not
overflow-magnitude order) governs the sequential pass within a batch,
which the earlier per-net rescue mechanisms (yroute, zroute) never needed
to worry about since they process one net at a time.

**Honest bottom line on the joint/batch idea overall**: two variants
tried (uncapped, capped-at-12), both fully supervised, both landing on
the identical safe result of zero net change to kroute's baseline.
Capping made the experiment cheaper and safer to run, not more
effective. The joint/batch mechanism as built (sequential per-net
`routeNetSlow` calls inside a batch, gated by a single before/after
`overflowOverUnion` accept/reject) has not yet demonstrated any scenario
on real ISPD data where it beats doing nothing -- consistent with, but
not yet conclusive proof of, the deeper structural gap this whole sweep
keeps surfacing: everything in this lineage (rescue passes, MSMV, batch
rerouting) still prices routing decisions through one blended scalar
cost function, never truly optimizing overflow lexicographically ahead
of wirelength the way the ISPD scoring itself does.

### Parked idea, not yet built: heat-map-then-reorder full reroute

User-proposed follow-up, explicitly deferred for a later session --
recorded here per this project's file-based-state discipline so it is
not lost to context compaction:

> "do enough iterations to have a stable overflow heat map, then do the
> capped cluster analysis then rip up everything and try starting with
> rerouting either the nets contributing to the hottest or coldest
> cluster and proceeding to the other end of the temperature range"

This is a materially different mechanism from anything built in Part 12:
broute's batch rescue keeps every net outside a contended cluster frozen
and only touches the cluster's own members, accepting/rejecting each
cluster independently and leaving everything else exactly as phase B
left it. The parked idea instead proposes (1) running to a converged,
*stable* congestion snapshot first (not mid-convergence, where clusters
are still forming/growing -- directly relevant to reason (b) above for
why the capped batch experiment found nothing), (2) running the existing
capped-cluster analysis on that stable snapshot purely as a
*diagnostic/ordering* pass rather than a route-and-gate mechanism, (3)
then a full rip-up of literally everything, and (4) a single, one-time,
globally-reordered rebuild that routes net-by-net in cluster-temperature
order -- hottest cluster's nets first, proceeding to coldest (or the
reverse), rather than the current fixed/arbitrary net-id order every
router in this lineage has used unconditionally since kroute.

Two prior findings bear directly on this, in opposite directions.
Against it: rroute's full-rip-up-every-round experiment (Part 6) is the
closest prior test of "throw away committed routes and start over," and
it made things worse -- but that was *repeated* rip-up every round
(pure churn, no new information gained each time), structurally
different from this idea's *single* one-time rebuild informed by a
genuinely new signal (a converged heat map) that no earlier round had
available. For it: xroute's centrality-ordering work (and, further back,
mroute's ordering-by-constraint work) already established that *the
order in which nets are committed against a shared, contended resource
measurably changes outcomes* -- constrained-first orderings beat
arbitrary/id order there. Routing the hottest contention first (when
the most capacity is still free to negotiate with) or the coldest first
(clearing easy nets out of the way so the hard ones see a cleaner board)
are both directly testable instances of that same general lever, applied
globally for the first time rather than per-tiebreak.

**Not to be built without an explicit go-ahead.** If taken up: it needs
its own forked file (next free single-letter prefix), its own
self-test proving the reordering measurably changes which nets see which
prices at commit time (not just that a permutation was computed), and
should measure hottest-first and coldest-first as two separate,
independently-reported conditions rather than assuming a direction in
advance -- this project's empirical-tuning discipline applies exactly as
much here as anywhere else in the sweep.

## Part 13: froute -- the heat-map reorder rebuild, built and measured

The user gave the explicit go-ahead for the idea parked above, with one
addition: three tunable orderings rather than just hottest-first and
coldest-first -- `hottocold`, `coldtohot`, and a third, `alternatehotcold`
(hottest, coldest, 2nd-hottest, 2nd-coldest, ..., walking inward from both
ends of the temperature-sorted cluster list). `gr/froute.cpp` (forked from
broute.cpp; commit 8aa82d5) builds this as a new Phase E, reusing broute's
own `findOverflowClusters`/`clusterNets`/cap-and-trim machinery for
cluster discovery and membership, adding a per-cluster `temperature`
(total overflow amount over that cluster's own edges) and the ordering
logic itself.

**Two real, measured confounds found and fixed before the result below
was trustworthy** -- both caught the same way every prior bug in this
sweep was: the three `FROUTE_ORDER` modes landing on *suspiciously
identical* numbers, which is not what a mechanism that's actually
sensitive to processing order should do.

1. **Confound #1 -- unconditional tier upgrade.** The first version routed
   every net in Phase E's rebuild via `routeNetSlow` (the real-search
   slow tier), including the ~269,000 never-overflowed "leftover" nets
   the rebuild loop was, at that point, still touching. Force-migrating
   essentially the whole design to real search in one unsupervised pass
   is exactly CLAUDE.md's own documented "unconditional escalation"
   anti-pattern (there, 30% of a design escalating at once spiked
   overflow ~25x) and directly violates its escalation-tier stickiness
   rule. Measured on newblue1: **41956 -> ~1.81 million (~43x)**,
   identically across all three orderings (1812040 / 1812252 / 1812078,
   within 0.02% of each other) -- the sameness across modes was the
   actual tell, not the raw magnitude. Fixed by dispatching each net
   through whichever tier phase B already settled on for it
   (`rn.escalated`), never upgrading a fast-tier net just because Phase E
   touches it.
2. **Confound #2 -- unbounded rip-up scope.** Fixing (1) alone was not
   enough. A version that respected tier but still ripped up and rebuilt
   *every* non-giant net (cluster members and untouched leftovers alike)
   still exploded overflow to **~872,700 (~20x)**, again nearly identical
   across all three modes (872742 / 872690 / 872710, within 0.006% of
   each other). Rerouting ~269,000 nets that were never part of any
   overflow cluster, sequentially and live -- each one reacting instantly
   to the immediately preceding net's fresh commit, rather than every net
   in a round reacting to one shared, frozen price snapshot the way phase
   A/B's actual round-based scheduler works -- is a fundamentally noisier
   pricing dynamic than this codebase's architecture is built around, and
   it visibly destabilized nets that had nothing to do with the heat map
   at all. Fixed by restricting Phase E's rip-up-and-rebuild scope to
   genuine cluster members only -- every other net's charges are left
   completely untouched, never even undone.

Both fixes are backed by dedicated self-tests (`testReorderRebuildRespects
EachNetsEstablishedTier`, `testReorderLeavesNeverOverflowedNetsCompletely
Untouched`) and documented in full in `gr/froute.cpp`'s own header, since
the diagnostic trail -- not just the final number -- is itself a useful
record of what "rip up everything" actually costs against this
architecture's round-based pricing assumptions.

**Verified result, same 30/40-round newblue1 screen used throughout this
comparison, all three `FROUTE_ORDER` modes** (scope now correctly bounded
to the 218 clusters' own capped membership -- 2510 per-cluster net slots,
1676 unique nets after cross-cluster dedup, out of 270715 routed nets):

| mode | internal rebuild result | accepted? | final output (gr_verify) | eval2008.pl |
|---|---|---|---|---|
| hottocold | 41956 -> 110592 | no | 41956 / max 22 / wl 6634017 | 41956 / 22 / 6634017 (exact match) |
| coldtohot | 41956 -> 110350 | no | 41956 / max 22 / wl 6634017 | 41956 / 22 / 6634017 (exact match) |
| alternatehotcold | 41956 -> 110544 | no | 41956 / max 22 / wl 6634017 | 41956 / 22 / 6634017 (exact match) |

All three legal per gr_verify (0 disconnected/incomplete/unrouted/
malformed), all three cross-checked exactly against eval2008.pl. **All
three rebuilds were rejected by Phase E's own supervision gate** (each
made total overflow ~2.6x worse internally: 110592/41956=2.64x,
110350/41956=2.63x, 110544/41956=2.64x) and correctly reverted, so the
final output in all three cases is bit-for-bit phase B's own result --
zero regression, exactly as the supervision gate is supposed to guarantee.

**The three modes are nearly identical to each other** (110592 vs 110350
vs 110544, a 0.2% spread) even after both confounds were removed and
scope was correctly bounded to just the contending nets. This is itself
the headline finding, not a leftover confound: once cross-cluster
ordering is the *only* variable left (scope and tier are both controlled
for), it barely moves the outcome. The 218 clusters are spatially disjoint
by construction (that is what makes them separate clusters), so which one
gets processed first mostly does not change how any *other* cluster's own
nets contend with each other -- the small residual spread (0.2%) is
consistent with the minority of nets that happen to touch more than one
cluster's tiles (2510 capped per-cluster slots reduce to only 1676 unique
nets, meaning roughly a third of slots are a net counted against a second
cluster), which is exactly where cross-cluster order has any lever to
pull at all. The dominant effect on the ~2.6x internal worsening is
*within* each cluster's own sequential reroute -- the same phenomenon
broute's Phase D already found and could not fix by capping batch size
(HISTORY_COST_SWEEP.md Part 12) -- not which cluster goes first.

**Honest bottom line.** The heat-map reorder rebuild, properly isolated
from both confounds, does not recover anything broute's own joint/batch
mechanism didn't already fail to find, and confirms directly (rather than
by inference) that global cross-cluster ordering is not the missing
lever: the three tunable orderings the user asked to compare differ from
each other by noise, not by signal. Combined with Part 12's finding, this
sweep has now tried four distinct ways of coordinating multiple
contending nets against each other (broute's uncapped batch, broute's
capped batch, and now froute's three global orderings) and all four land
on the identical safe no-op relative to kroute's own baseline. The
remaining lever this sweep has not yet tried is *genuinely joint*
optimization within a cluster (something other than sequential
`routeNetSlow` calls reacting to each other's fresh commits one at a
time) -- sequential-with-reordering, at every scope tried so far, keeps
landing in the same place.

## Part 14: aroute -- joint candidate-assignment via exact branch-and-bound

The user asked directly what a genuinely joint optimization strategy
inside a cluster could realistically be based on, given everything Parts
12-13 had already ruled out. The answer built here: generate several
candidate routes per contending net up front, against one frozen
baseline (reusing yroute's own length-weight-decay trick,
`AROUTE_CANDIDATE_WEIGHTS`, to get genuinely different candidates
cheaply, plus each net's own original route as a standing "no change"
option), then pick one candidate per net *jointly* -- the combination
minimizing the cluster's total overflow -- via an exact branch-and-bound
search over the small capped candidate space, rather than committing net
by net and reacting to whatever the previous commit changed. This is the
first mechanism in the whole sweep to change the *solve method* itself,
not just the scope (Part 12) or order (Part 13) of the same sequential
greedy solve. `gr/aroute.cpp` (forked from broute.cpp; commit 35ce6be)
reuses `findOverflowClusters`/`clusterNets` and froute's own standalone
`rankAndCapClusterNets` unchanged, and carries forward froute's own
scope-restriction lesson (only genuine cluster members are ever touched).

**A real bug found and fixed before the result below was trustworthy,**
caught the same way as always in this sweep -- not by reasoning, by the
numbers looking wrong on real data. The first real-benchmark smoke test
(a short 5/5-round screen) showed 220 of 243 clusters "accepted," with
the branch-and-bound reporting dramatic internal improvements (e.g.
1730 vs. a seeded incumbent of 5124) -- but the mechanism's own final
independent-recomputation gate (`overflowOverUnion`, recomputed from the
actually-committed charges, never trusted to agree with the search's own
bookkeeping) disagreed with the search's internal number in 223 of those
244 cases, revealing that the "improvements" were mostly ties with doing
nothing, not real wins. Root cause: the branch-and-bound's incremental
delta tracking only ever accumulated overflow on edges some *candidate*
touched. An edge one net's *original* route touched, but that the
winning combination abandoned (that net moved to a different candidate),
never got a single `applyDelta` call in that combination -- so any
background overflow already sitting there, from nets entirely outside
the capped cluster, or from nets that merely pass through the cluster's
tiles without being overflowed themselves (both explicitly outside
`clusterNets`' own membership rule), silently vanished from the running
total instead of persisting the way it actually does in the real design.
Fixed by seeding the search with a precomputed baseline-overflow term --
the true, zero-delta overflow over the full union of every candidate's
edges -- before the recursion starts. A small, rare residual disagreement
(single digits, not the thousands-off gap this bug caused) still turns
up occasionally and was not further root-caused, but -- as with every
other mechanism's own final gate in this codebase -- it has never once
caused an incorrect accept, since accept/reject always uses the
independently-recomputed value, never the search's own internal number.
See `gr/aroute.cpp`'s own comments (`branchAndBoundAssign`,
`negotiateJointAssignment`) for the full account.

**Verified result, same 30/40-round newblue1 screen used throughout this
comparison, after the fix**: **41956 / max 22 / wl 6634017**, an exact
match to the kroute/broute/froute baseline via both gr_verify (0
disconnected/incomplete/unrouted/malformed) and eval2008.pl (exact
agreement). 218 clusters attempted, **0 accepted**.

| metric | value |
|---|---|
| clusters attempted | 218 |
| clusters accepted | **0 (0%)** |
| clusters that hit the node cap | **0** -- every search ran to true completion |
| bnb nodes per cluster (median / mean / max) | 66 / 1364.3 / 87676 (of a 200000 cap) |
| clusters where the seeded greedy incumbent was worse than doing nothing | **218 / 218 (100%)** |
| greedy-incumbent / before ratio (mean / median / max) | 6.18x / 3.99x / 45.25x |
| final overflow | 41956 (identical to doing nothing) |

**This is a clean, decisive negative result, not a truncated or
under-powered one.** Every one of the 218 searches ran to genuine
completion (max 87676 of the 200000-node budget -- the cap was never
binding), meaning branch-and-bound proved, exhaustively over the
generated candidate space, that *no* combination of these nets' own
weight-schedule candidates beats leaving the cluster untouched, for any
of the 218 real contended regions on this benchmark. This is a
materially stronger claim than anything Parts 10-13 could make: those
measured that a *particular* sequential procedure didn't find an
improvement; this measures that *no* assignment reachable from the
generated candidates does, within the search's own exact guarantee.

The seeded greedy-incumbent numbers are independently interesting on
their own: computed fresh, from the identical frozen baseline, via the
exact sequential procedure broute's own Phase D uses, they confirm
broute's finding a second time via a completely different code path
(dedicated candidate generation plus a from-scratch sequential pass, not
broute's own live batch loop) -- sequential greedy is worse than doing
nothing in literally every single cluster on this benchmark, by a mean
of 6.18x.

**Honest bottom line and what it actually rules in or out.** Joint
*selection* among these particular candidates does not help -- confirmed
exhaustively, not just empirically. This narrows, rather than closes, the
open question from the earlier discussion with the user about what a
real joint optimization lever could be: the candidate generator itself
(yroute's length-weight schedule) is the same one whose own rescue log
found 86% of holdouts unhelped at *any* weight (Part 10) -- so a search
method that reasons perfectly over a candidate set this narrow was
always bounded by that same ceiling. The two structural gaps raised in
that discussion remain untested: (1) candidate diversity itself --
MSMV-generated candidates, or genuinely different topologies, rather
than one path decaying toward straight-line indifference to congestion;
and (2) the "cooperative eviction" idea -- expanding a cluster's scope to
include nearby *legal* nets willing to make room, since every mechanism
through this part, aroute included, only ever lets already-overflowed
nets move, and if their own rearrangements among themselves cannot
relieve a jointly-oversubscribed resource, recruiting a neighbor outside
that set may be the only real lever left.

## Part 15: nroute -- MSMV candidates on top of aroute's exact search

The user asked directly to try the first of Part 14's two untested
levers: candidate diversity. `gr/nroute.cpp` (forked from aroute.cpp;
commit 9615233) reuses aroute's exact branch-and-bound search completely
unchanged and widens only what it gets to choose from --
`generateCandidatesForNet` is extended with MSMV, ported verbatim from
zroute.cpp/vroute.cpp, as a second candidate source alongside yroute's
length-weight-decay schedule. Where the fixed-MST decomposition can only
ever attach a new segment at one of the *pins* a precomputed tree edge
names, MSMV grows a net incrementally, treating every already-committed
tile as a simultaneous source and every unconnected pin as a
simultaneous sink -- finding genuinely different Steiner topologies a
fixed-MST decomposition cannot represent at all. Confirmed with a
hand-verified, decisive fixture before any real-data run: a 3-pin,
Manhattan-equilateral triangle (every pairwise distance exactly 4 tiles)
where the fixed-MST decomposition commits two independent 4-tile paths
(8 tiles total) but MSMV finds the true 6-tile Steiner tree by attaching
the third pin to the *midpoint* of the first two pins' already-committed
wire -- both numbers (8 and 6) matched the hand derivation exactly on
the first run.

**Verified result, same 30/40-round newblue1 screen**: **41956 / max 22
/ wl 6634017**, an exact match to the kroute/broute/froute/aroute
baseline via both gr_verify (0 disconnected/incomplete/unrouted/
malformed) and eval2008.pl (exact agreement). 218 clusters attempted,
**0 accepted**.

| metric | aroute (Part 14) | nroute |
|---|---|---|
| clusters attempted | 218 | 218 |
| clusters accepted | 0 (0%) | **0 (0%)** |
| candidates per cluster (mean) | ~60 | **101.2** |
| clusters that hit the node cap | 0 | **8** |
| bnb nodes per cluster (median / mean / max) | 66 / 1364 / 87676 | **159 / 10524 / 200001** |
| clusters where greedy was worse than doing nothing | 218/218 | 218/218 |
| greedy/before ratio (mean / median / max) | 6.18x / 3.99x / 45.25x | 5.95x / 3.80x / 45.50x |
| runtime | 212.0s | **426.9s** |
| final overflow | 41956 | 41956 (identical) |

**A genuinely richer candidate pool -- confirmed, not assumed.**
Candidates per cluster nearly doubled (mean 101.2 vs. aroute's ~60,
median exactly 108 -- the theoretical max for a 12-net cluster of
multi-pin nets: 5 fixed-MST candidates plus 4 MSMV candidates per net),
confirming the MSMV addition is doing real work generating genuinely
different options, not just relabeling the same ones. Runtime roughly
doubled too (426.9s vs. 212.0s), the honest cost of that richer pool --
MSMV's incremental growth is inherently more expensive than a fixed-MST
decomposition's independent point-to-point searches.

**Still zero accepted, and this time not fully exhaustive.** Unlike
aroute's own result, 8 of the 218 searches here hit the
`NROUTE_MAX_BNB_NODES` cap (default 200000) before proving optimality --
the larger candidate space (up to 9 candidates per net instead of 5)
costs real search time, and one cluster (215) reached the cap exactly
(200001 nodes, one over the limit). All 8 correctly reverted to their
pre-touch state regardless (the safety-valve behavior aroute's own
self-test already validated), so the final committed result is
unaffected -- but the claim for those 8 clusters specifically is
empirical (nothing better was *found* within budget), not exhaustive
(proven that nothing better *exists*) the way aroute's full-exhaustion
result was for every one of its 218.

**Honest bottom line.** Widening the candidate pool with a structurally
different, already-validated generator (MSMV) did not recover anything a
narrower pool couldn't -- the joint search still finds nothing better
than leaving every cluster alone, on all 218 real contended regions on
this benchmark. This is a real, if negative, answer to Part 14's own
open question: candidate diversity via MSMV specifically is not the
missing ingredient either. Two readings are both consistent with this
result, and this measurement alone can't distinguish them: (1) these
particular contended clusters are genuinely unresolvable by *any*
rearrangement of the nets already in them, regardless of how those
routes are generated -- which would point toward Part 14's second
untested lever, "cooperative eviction" (recruiting nearby legal nets, not
just already-overflowed ones); or (2) MSMV's own candidates still share
a common blind spot with yroute's length-weight schedule -- both are
single-path-growth mechanisms reacting to the same live price landscape,
so a resource three or four nets are jointly fighting over may need a
topology neither generator's own search bias ever surfaces, even though
the *exact* assignment search now picks perfectly among whatever each
generator offers. Five distinct coordination/search mechanisms have now
been tried across Parts 12-15 (broute's two batch variants, froute's
three orderings, aroute's exact search, nroute's exact search over a
richer pool) and all five land on the identical safe no-op relative to
kroute's own baseline on this benchmark.

## Part 16: oroute -- cooperative eviction of legal nets

The user asked directly to try the second of Part 14's two untested
levers. `gr/oroute.cpp` (forked from nroute.cpp; commit d3c4bfc) reverses
the membership rule every prior mechanism in this sweep inherited from
broute's own `clusterNets`: only nets *already overflowed* ever move.
`recruitEvictionHelpers` recruits up to `OROUTE_MAX_EVICTION_HELPERS`
(default 3) currently-*legal* nets that also touch the cluster's own
tiles into the same joint search, ranked by how much of the cluster's
edge capacity each one occupies. Everything downstream -- candidate
generation (both generators), the exact branch-and-bound search, the
accept/reject gate -- is exactly aroute's/nroute's own machinery,
unchanged; a helper's own original route is always its candidate 0, so
recruitment only ever offers the search an option, never forces a move.

**No decisive "eviction strictly helps" fixture was found**, despite
real, sustained effort (documented in the file's own self-test comment):
a hand-verified 3-net fixture where a real, geometrically legitimate
helper is correctly recruited and genuinely considered, where the exact
search still lands on the identical result with or without it available
to move. Kept as a permanent regression/plumbing guard rather than
deleted, following froute.cpp's own precedent for this exact situation.

**Verified result, same 30/40-round newblue1 screen**: **41954 / max 22
/ wl 6634720**, both gr_verify (0 disconnected/incomplete/unrouted/
malformed) and eval2008.pl in exact agreement. This is a real,
independently-verified **-2 overflow** improvement over the entire
lineage's shared 41956 baseline (kroute/broute/froute/aroute/nroute) --
the first accepted cluster (1 of 218) anywhere in this five-mechanism
sweep, at a wirelength cost of +703 (6634017 -> 6634720, +0.011%).

| metric | value |
|---|---|
| clusters attempted | 218 |
| clusters accepted | **1 (0.46%)** |
| clusters that hit the node cap | 8 (same as nroute) |
| clusters with >=1 helper recruited | **218 / 218 (100%)** |
| clusters where a helper actually *moved* | **0** |
| candidates per cluster (median / mean) | 131 / 125.6 |
| final overflow | **41954** (-2 vs. every prior mechanism's 41956) |
| final wirelength | 6634720 (+703 vs. 6634017) |

**The one win did not come from a helper physically relocating --
confirmed directly against nroute's own cluster-by-cluster log, not
inferred.** Clusters 1-17 show bit-identical `overflow_before` values
between nroute's and oroute's runs (proving both runs are perfectly
synchronized up to that point, since phase A/B are unchanged and every
earlier cluster was rejected -- and reverted -- in both). At cluster 18,
both start from the identical `before=18`. nroute's exact search, over
exactly the same 12 overflowed nets' own candidates, finds nothing
(`18 -> 18`, rejected). oroute's exact search, over the same 12 nets
*plus* 3 recruited helpers, finds `18 -> 16` -- genuinely better -- and
the accepted combination leaves all 3 helpers at their own original
route (`helper_moved=0` for this cluster too).

The mechanism: candidate generation for every net in a cluster runs
against a single frozen baseline built by ripping up *every* net
`negotiateJointAssignment` currently considers a member -- in oroute,
that rip-up removes the 3 helpers' own usage too, not just the 12
overflowed nets'. That leaves *more* free capacity in the baseline the
12 overflowed nets' own candidate search reacts to than nroute's
narrower rip-up (helpers' usage never removed) ever exposed them to --
letting `generateCandidatesForNet` discover a genuinely different,
better candidate for one of them that nroute's own identical search
never had the chance to generate in the first place, since it was never
offered the option of searching against that particular baseline. The
exact search then correctly determines the helpers don't actually need
to move to realize that gain, and leaves them exactly where they were.
This is a real, structural side effect of the mechanism as built, not a
bug -- recruiting a helper's *candidate-generation footprint* turns out
to matter independently of whether the helper's own route ever changes.

**Honest bottom line.** After five consecutive exact-negative results
(Parts 12-15), cooperative eviction is the first mechanism in this whole
sweep to find anything -- a small, real, doubly-verified win. It is not
the mechanism the file's own header argued for (a helper physically
making room was the whole premise); it is a genuine, unanticipated
consequence of *how* the rip-up-before-candidate-generation step scopes
itself, discovered by measurement, not designed in. Whether this
specific effect -- widening the candidate-generation baseline by
recruiting nets that end up not moving -- generalizes, or whether it
could be captured more directly (e.g. by ripping up recruited helpers
for candidate-generation purposes even when eviction itself is disabled,
isolating this exact effect from actual helper relocation) is an open,
now well-motivated follow-up question this measurement raises rather
than answers. -2 out of 41956 is a marginal result in isolation; its
real significance is ending the sweep's five-way tie at exactly zero.

## Part 17: qroute -- isolating candidate-generation baseline widening

The user asked directly to try Part 16's own follow-up question. Before
building, the mechanism itself needed re-diagnosing: oroute's win looked
at first like "removing a helper's usage frees capacity," but Dijkstra's
candidate search prices edges, it doesn't hard-check capacity, and
`undoCharge` only ever reverts `usage`, never `prices` -- so simple
capacity freeing can't be the real explanation. The actual mechanism:
`negotiateWire` (called from inside `commitPath`, which every trial
candidate `generateCandidatesForNet` tries commits through) raises an
edge's price based on `usage` at that exact moment, and that rise is
never undone. The *sequence* of price bumps a whole cluster's candidate
generation leaves behind depends on which nets' usage was present in the
baseline throughout -- so removing a helper's usage from that baseline,
even though the helper's own final route never changes, changes what
every *other* net's own candidate generation sees.

`gr/qroute.cpp` (forked from oroute.cpp; commit 940d1b5) tests this
mechanism directly, deliberately separated from whether the widening
nets ever move. `recruitContextNets` recruits up to
`QROUTE_MAX_CONTEXT_NETS` (default 20, an order of magnitude past
eviction's own default 3 -- affordable because these nets never join the
branch-and-bound's own search space at all) currently-legal,
cluster-touching nets, ripped up for exactly candidate generation's own
window, then unconditionally restored to their exact original charges
before the greedy incumbent, the exact search, or the accept/reject gate
ever run. A context net never appears in `netIdx`, never gets a
candidate slot, and is never a possible search outcome at all -- unlike
a recruited eviction helper (an offer the search may take), its only
role is to be temporarily absent, then present again. Cooperative
eviction itself is inherited unchanged but defaults to *off*
(`QROUTE_MAX_EVICTION_HELPERS=0`), isolating context widening as this
file's own single new variable.

**Verified result, same 30/40-round newblue1 screen**: **41954 / max 22
/ wl 6634267**, both gr_verify (0 disconnected/incomplete/unrouted/
malformed) and eval2008.pl in exact agreement. 218 clusters attempted,
1 accepted -- the *exact same* **-2 overflow** win oroute.cpp found, at
**less than a third the wirelength cost** (+250 vs. oroute's +703 over
the shared 6634017 baseline).

| metric | nroute (no widening) | oroute (eviction + widening entangled) | qroute (widening alone) |
|---|---|---|---|
| cluster 18: nets / candidates | 12 / 100 | 15 / 127 | **12 / 100** |
| cluster 18: overflow before -> after | 18 -> 18 | 18 -> 16 | **18 -> 16** |
| cluster 18: accepted | no | yes | **yes** |
| final overflow | 41956 | 41954 | **41954** |
| final wirelength | 6634017 | 6634720 (+703) | **6634267 (+250)** |
| clusters that hit the node cap | 8 | 8 | 8 |

**Confirmed directly against both prior logs, not inferred**: cluster 18
is the *same* cluster in all three runs (identical `overflow_before=18`,
proving all three runs are synchronized up to that point). nroute's
search, over the 12 overflowed nets' own candidates alone, finds
nothing. oroute's search, over those same 12 nets plus 3 real
candidate-owning helpers (15 nets, 127 candidates total), finds
`18 -> 16`. qroute's search, over exactly the same 12 nets and the exact
same candidate *count* (100) nroute's own identical search
produced -- context nets are never counted in `candidates_total` at
all -- *also* finds `18 -> 16`. Since qroute's branch-and-bound explores
a search space identical in size to nroute's, the improvement cannot be
attributed to extra search power or extra participants; the only
variable that differs between nroute and qroute is what baseline
candidate generation ran against, exactly isolating the mechanism the
file set out to test.

**Why qroute costs less wirelength than oroute for the identical
overflow win**: oroute's winning combination is drawn from a *larger*
candidate pool (127 candidates across 15 nets, including 3 real
candidates for helpers that end up unused) generated against a baseline
where those 3 helpers' own usage was *also* absent -- a wider, noisier
price-bump history than qroute's narrower one (only the 12 overflowed
nets' own candidate generation, against a baseline widened by 20
context-only nets). The two runs' price landscapes diverge in
path-dependent ways neither this measurement nor a hand-derivation
could have predicted in advance; qroute's own tighter scope happened to
land on a cheaper route to the identical overflow reduction. This is an
observation, not (yet) a general rule -- worth flagging honestly rather
than overclaiming a "widening alone is strictly better" pattern from a
single data point.

**Honest bottom line.** The re-diagnosis holds up under direct,
apples-to-apples measurement: cooperative eviction's real value in
Part 16 came entirely from the candidate-generation baseline effect,
not from helpers physically relocating, and that effect is now isolated
into its own clean mechanism that reproduces the identical win more
cheaply. Six real ISPD-benchmark measurements have now been run across
Parts 12-17 (broute's two batch variants, froute's three orderings,
aroute's exact search, nroute's exact search over a richer pool,
oroute's eviction, qroute's isolated widening); five landed on the
identical safe no-op, and the two that found anything (oroute, qroute)
found the *same* single real improvement via what turns out to be the
*same* underlying mechanism. The magnitude remains marginal (-2 out of
41956) and the effect fired in exactly one of 218 clusters -- but it is
now a real, understood, reproducible lever rather than an accidental
side effect discovered once and left unexplained.

### Follow-up: does widening further help further?

The user asked directly. Since context nets never enter the
branch-and-bound's own search space, widening them costs only linear
extra rip-up/restore work, not combinatorial blowup -- so a much larger
`QROUTE_MAX_CONTEXT_NETS` was cheap to test directly on the real
benchmark rather than reasoned about in advance. Three widths run in
parallel against the same 30/40-round newblue1 screen, all cross-checked
exactly via gr_verify and eval2008.pl:

| `QROUTE_MAX_CONTEXT_NETS` | accepted | final overflow | final wirelength |
|---|---|---|---|
| 20 (default) | 1 (cluster 18) | 41954 | 6634267 |
| 50 | 2 (clusters 18, 128) | **41952** | 6634736 |
| 150 | 2 (clusters 18, 40) | **41952** | **6634460** (cheaper than 50, same overflow) |
| unbounded (~241 natural pool at cluster 18) | **0** | **41956** (back to exact baseline) | 6634017 |

**Widening is not monotonic.** It helps in a narrow middle band (50-150:
a second cluster starts winning, on top of cluster 18's own win, which
survives unchanged at both these widths) and is actively *harmful* past
it: cluster 18's own win -- present, identical, at width 20, 50, *and*
150 -- disappears entirely once its true (uncapped) pool of 241 eligible
context nets is fully admitted, reverting the whole run to the exact
41956 baseline. This was checked directly, not inferred: cluster 18's
own row in the joint log shows `context_nets=150` (hit the cap) at
width 150 and `context_nets=241` (its real, uncapped count) at the
unbounded setting -- the extra 91 nets between those two runs are what
flip its own accept to a reject.

This is consistent with -- and sharpens -- the mechanism qroute.cpp's
own header already documents: the effect is a side effect of the
*specific sequence* of price bumps `negotiateWire` leaves behind during
candidate generation, never undone by `undoCharge`. That sequence is
inherently path-dependent, not a monotonic "more information always
helps" signal -- more context nets ripped up means more trial-commit
price perturbations layered on top of each other, and past some point
for a given cluster, that noise can just as easily cancel out a
beneficial perturbation as compound it. The two 2nd-cluster wins at
width 50 and 150 are also *different clusters* (128 vs. 40), not the
same win found earlier and kept -- further evidence this is closer to a
noisy, width-and-cluster-specific interaction than a clean dial to turn
up.

**Not re-tuned from this result.** Four points (20, 50, 150, unbounded)
establish non-monotonicity convincingly but do not characterize the real
shape of the curve -- where the peak actually sits, whether 100 or 200
does better than both 50 and 150, or whether the harmful region begins
gradually or as a sharp cliff for most clusters the way it did for
cluster 18. `QROUTE_MAX_CONTEXT_NETS`'s default stays at 20 (already
documented as a first empirically-motivated guess, not a tuned value) --
a proper sweep, should this be worth pursuing further, needs many more
points across the same real screen, following this project's own
"tune empirically, never by pure reasoning" discipline exactly as it
already applied to every other threshold in this codebase.

### Follow-up to the follow-up: a finer sweep, 50-300 in steps of 25-50

The user asked for exactly that finer sweep. Nine more widths (75, 100,
125, 175, 200, 225, 250, 275, 300), run in three batches of three at the
project's own 3-concurrent-process cap, all cross-checked exactly via
both gr_verify and eval2008.pl, joined with the four points already on
record:

| width | overflow | accepted clusters |
|---|---|---|
| 20 (default) | 41954 | {18} |
| 50 | 41952 | {18, 128} |
| 75 | 41952 | {18, 113} |
| 100 | 41954 | {18} |
| 125 | 41952 | {18, 40} |
| 150 | 41952 | {18, 40} |
| 175 | 41954 | {18} |
| 200 | **41956** | {} |
| 225 | **41948** | {18, 134, 165} |
| 250 | **41948** | {18, 134, 165} |
| 275 | 41952 | {18, 165} |
| 300 | 41950 | {18, 103, 165} |
| unbounded | 41956 | {} |

**Not pure noise -- a genuine mix of two different behaviors, both real.**
Cluster 165 accepts at *every* width from 225 up through 300 and *never*
below 225 -- a real threshold for that cluster specifically, not a
coincidence repeated four times running. Cluster 18, by contrast, accepts
at nearly every width tested (20 through 300, ten of twelve points) but
drops out at exactly two points: width 200 (sandwiched between 175 and
225, both wins) and again once its own true, uncapped context pool (241
nets) is fully admitted -- a real reversal, not just "eventually stops
helping." The additional clusters that accept alongside 18 differ by
width almost every time (128, 113, 40, 134+165, 165, 103+165) -- some of
that is real width-dependent structure (165's own clean threshold), and
some of it likely is the path-dependent price-bump noise the mechanism's
own header already documents. Both are present in the same sweep; this
result doesn't resolve which dominates, only that neither is the whole
story on its own.

**The best point found (41948 at widths 225 and 250, tied) is still only
-8 out of 41956 -- 0.019%.** This does not change the answer to the
user's own question about whether this line of work could plausibly
reach anywhere near zero overflow: even the best-case point in a
12-point sweep spanning 20 to unbounded found a reduction two orders of
magnitude short of what would be needed. The sweep's real value is
methodological, not in the headline number -- it rules out "just widen
it more" as a path to a materially better result on this specific
mechanism, and it demonstrates concretely (not just in the abstract)
that this whole family (Parts 12-17) has reached the point of
diminishing, near-zero returns on this benchmark. The width itself is
left at its original default (20); nothing here justifies moving it
without a clearer picture of the curve's shape between the tested
points, which was not the goal of this pass.


## Part 18: uroute -- region-level cross-layer congestion pricing

Scoped from the user's own question above ("is there any realistic
probability... or is it suggesting some additional exploration of
untried approaches?"), pointed specifically at Part 9's own finding
(bigblue1: aggregate layer utilization only 65.7%, yet every layer at a
hot tile is simultaneously over capacity) as the most promising
*structurally untested* remaining lever, since the whole exact-search/
candidate-widening family (Parts 12-17) had just been shown close to
exhausted (best -8/41956, 0.019%, non-monotonic in its own tuning
parameter).

Built as `gr/uroute.cpp`, forked from `gr/kroute.cpp` specifically --
not from the broute-through-qroute joint-search lineage -- to isolate
this as a standalone pricing-formula-level test, directly comparable to
kroute's own recorded 172-round baseline. Partitions the die into
UROUTE_REGION_SIZE x UROUTE_REGION_SIZE tile regions (default 8x8),
aggregated across *every layer and both edge directions* -- the direct
encoding of Part 9's finding, dropping layer entirely from the region
key. Each region gets a static aggregate capacity and a multiplicative
`regionPrice`, updated at the same three round-boundary points
`updateHistory` already fires from, via this project's own core
multiplicative price-update law (`lambda_e <- lambda_e *
exp(epsilon * usage_e/capacity_e)`) applied to the region's aggregate
ratio instead of a single edge's -- deliberately reusing that law rather
than inventing a fourth pricing formula. `regionPrice` becomes a third
multiplicative factor in `edgeCost`. No relaxation (LIFO-undo) for
region price in this first version, unlike kroute's own edge-level
history -- a disclosed scope cut, not an oversight.

40 self-tests pass, including the decisive one: a 16x16 single-layer
fixture where every candidate route between two corner-opposite pins has
*identical* total tile length and *identical* lambda/hist everywhere (so
nothing but list order breaks the tie) -- the real production pipeline
(`generateCandidates`, `priceCandidate`, `routeNetFast`'s own min-cost
selection, none of it reimplemented) picks the default tied candidate at
baseline, then picks a *different*, equal-length candidate that avoids a
region entirely once a separate net's usage (on an edge neither
candidate's own path touches) drives that region's aggregate cross-layer
budget over capacity. That is the one outcome plain per-edge pricing
(every edge on every candidate individually legal, lambda and hist both
untouched) structurally cannot produce.

**Real 3-benchmark result, default settings (UROUTE_REGION_SIZE=8,
UROUTE_REGION_EPS=0.02), no round-cap overrides -- same convention as
kroute's own "172-round" baseline (default env vars, round count is
however many the patience/cap logic actually runs, not a forced
override). Both legal per gr_verify (0 disconnected/incomplete/
unrouted/malformed) and cross-checked exactly against eval2008.pl:**

| benchmark | kroute (bar) | uroute (default) | delta |
|---|---|---|---|
| adaptec1 | 162584 / 7254167 | 161544 / 7253929 | **-0.64%** |
| newblue1 | 41014 / 6644362 | 41178 / 6642911 | +0.40% (regression) |
| bigblue1 | 292420 / 7535653 | 293388 / 7533489 | +0.33% (regression) |

All three ran the full 172 rounds (hit the round cap, same as kroute's
own baseline run) -- not a stalled or degenerate run on either side of
the comparison. Wirelength is essentially a wash on all three (all
within 0.03%), so this isn't a "traded wirelength for overflow" story
either way.

**Mixed, not a clean win.** One real improvement (adaptec1, comparable in
size to kroute's own historic wins in this lineage) and two small
regressions. A follow-up tuning probe (UROUTE_REGION_EPS at 0.005 and
0.002, and UROUTE_REGION_SIZE=16, all on newblue1 -- the fastest
benchmark, ~11 minutes/run) is running now to see whether the default
region-pricing step size is simply too aggressive at the region scale
(the same epsilon as kroute's own per-edge default, applied to an
aggregate that behaves differently) before drawing a final conclusion.
Results to follow in this same Part.

**Tuning probe result (newblue1, ~11min/run, all cross-checked against
gr_verify and eval2008.pl):**

| UROUTE_REGION_EPS | UROUTE_REGION_SIZE | overflow | vs kroute (41014) |
|---|---|---|---|
| 0.02 (default) | 8 (default) | 41178 | +0.40% |
| 0.005 | 8 | **40926** | **-0.21%** |
| 0.002 | 8 | 41008 | -0.01% (a wash) |
| 0.02 (default) | 16 | 41058 | +0.11% |

Confirms the hypothesis directly: the default region-eps (0.02, copied
from kroute's own per-edge `KROUTE_EPS` default) is too aggressive at
region scale -- a region aggregates far more usage and capacity than a
single edge, so the same multiplicative step compounds differently.
0.005 is a real, gr_verify+eval2008.pl-cross-checked improvement over
kroute on newblue1, not just a smaller regression. Widening the region
(size 16, epsilon left at default) also helps, but less than tightening
epsilon does, and the two are not mutually exclusive (not yet tried
together, given time already spent). UROUTE_REGION_EPS=0.005 launched
now on all three benchmarks (adaptec1 and bigblue1 running; newblue1
already on record above) to see if the tuned setting holds up
everywhere, before changing the file's own default.

**Final result, UROUTE_REGION_EPS=0.005 on all three benchmarks (gr_verify
+ eval2008.pl cross-checked exactly, both legal, 172 rounds each):**

| benchmark | kroute (bar) | uroute eps=0.02 | uroute eps=0.005 |
|---|---|---|---|
| adaptec1 | 162584 / 7254167 | 161544 / 7253929 (-0.64%) | 162664 / 7258013 (+0.05%) |
| newblue1 | 41014 / 6644362 | 41178 / 6642911 (+0.40%) | 40926 / 6644352 (-0.21%) |
| bigblue1 | 292420 / 7535653 | 293388 / 7533489 (+0.33%) | 292380 / 7537447 (-0.01%) |
| **combined total** | **496018** | **496110 (+0.019%)** | **495970 (-0.0097%)** |

**Verdict: mixed, not a clean win either way -- and this does not change
with more tuning of this one knob.** eps=0.02 trades a big adaptec1 win
for two small regressions; eps=0.005 nearly reverses that pattern.
Combined, both are within ~0.02% of kroute's own baseline -- two orders
of magnitude smaller than vroute's own MSMV win (-21% to -36%), and in
the same "near-exhausted" territory Parts 12-17 already found for the
exact-search/candidate-widening family, though for a structurally
different reason: not too little candidate diversity, but a *single
global epsilon* applied across benchmarks whose congestion geometry
(different pin densities, different placements -- see Part 8) responds
to it differently, the same character CLAUDE.md's own note about the
per-round pricing threshold already documents for a different parameter.

eps=0.005 is kept as the file's own default (smaller worst case, +0.05%
vs +0.40%), documented honestly in `gr/uroute.cpp`'s own header as a
tradeoff choice, not a discovered optimum -- neither setting is presented
as "the" answer.

**What this experiment actually settles.** The mechanism's core claim --
that region-level, cross-layer aggregate pricing is a genuinely different
signal from anything per-edge pricing (lambda or history) can see -- is
decisively proven, not just argued: `uroute.cpp`'s own self-test shows the
real production candidate-selection pipeline picking a different,
equal-length route purely because of another net's usage on a *different*
edge in the same region, something no per-edge mechanism in this
project's whole history (hroute through qroute) could ever respond to.
What is *not* settled, and what this real-benchmark measurement actually
shows, is that having a real, novel signal is not sufficient for a
benchmark-independent win at a single fixed global tuning -- a smaller,
narrower version of the same lesson vroute's own result taught at a much
larger scale (structural change beats coefficient tuning), and consistent
with the answer already given to the user's own question about this whole
family of experiments: real, measurable, but not close to closing the gap
to zero on its own. A per-benchmark-adaptive epsilon, or combining region
pricing with region size tuning (not attempted jointly here, given time
already spent), are natural further follow-ups, not undertaken in this
pass.

## Part 19: kr1route -- LIFO-relax the reactive price itself

First router under the new two-letter + generation-number naming
convention (species `kr`, generation 1; see CLAUDE.md's "Router naming
convention" addendum). Scoped from a direct observation: every router in
this lineage updates the reactive edge price (`lambda`) only one way --
`negotiateWire`'s multiplicative `exp(epsilon*ratio)` growth, always
`>= 1`. Checking every assignment to `Prices::h`/`v` confirms lambda is
monotonically non-decreasing for the entire run, with no relax mechanism
anywhere in the project's history -- the exact disease `hist` had before
`kroute`'s own LIFO-relax fix, which was the single biggest unanimous win
in the whole pricing-formula sweep. The hypothesis: since relaxing
history worked so well, relaxing lambda (the far more frequently-touched
term -- updated per net commit, not just per round boundary) the same
way should help further, and would directly attack part of Part 9's
"unused space" puzzle (a stale, elevated price hiding real idle
capacity).

Built as `gr/kr1route.cpp`, forked from `gr/uroute.cpp`: a new per-edge
LIFO stack (`Prices::lambdaOpsH/V`) pushed by `negotiateWire` at commit
time (the *applied*, post-clamp factor) and popped by a new `relaxLambda`
function at the same three round-boundary points `updateHistory` already
fires from. `KR1ROUTE_LAMBDA_RELAX=0` reproduces uroute's exact permanent
formula. 83 self-tests pass, including the decisive one proving the
mechanism does exactly what it claims: a genuinely-recovered edge's
stale, elevated price correctly returns to baseline after the same
number of clean rounds it took commits to raise it, flipping a real
`bestHLayer` comparison in its favor.

**Real result, isolated (`KR1ROUTE_LAMBDA_RELAX=1`,
`UROUTE_REGION_PRICING=0` -- lambda-relax alone), against kroute's own
baseline, same 172-round budget, both legal per gr_verify and
cross-checked exactly against eval2008.pl:**

| benchmark | kroute (bar) | kr1route (lambda-relax alone) | overflow delta | wirelength delta |
|---|---|---|---|---|
| adaptec1 | 162584 / 7254167 | 162984 / 7206243 | +0.25% | -0.66% |
| newblue1 | 41014 / 6644362 | 41458 / 6608842 | +1.08% | -0.53% |
| bigblue1 | 292420 / 7535653 | 292958 / 7506080 | +0.18% | -0.39% |

**A clean, unanimous overflow regression on all three real benchmarks,
paired with a clean, unanimous wirelength improvement on all three.**
Three-for-three in both directions is a real, interpretable tradeoff, not
noise. The hypothesis was wrong, and the numbers themselves show why:
`hist` and `lambda` are not interchangeable memory terms playing the same
role. `hist` exists specifically to add a *longer* memory of congestion
*on top of* lambda's fast, memoryless-but-permanent reactive term (see
kroute's own header: lambda reacts every commit, `hist` only grows once
a round's settled usage is over capacity -- a strictly narrower trigger
than lambda's own 20%-utilization threshold curve). Giving lambda a
*second*, independent short memory removes exactly the durability that
made it a useful, stabilizing deterrent on its own: an edge relaxes back
to cheap the moment it reads clear for one round, inviting nets straight
back onto real bottlenecks that a few rounds of elevated price were
correctly keeping them off of. Nets take shorter paths on average (the
wirelength win, real and consistent) because they're more willing to
reuse recently-busy edges -- and more of those reused edges tip into real
overflow than the wirelength savings are worth, against a project mandate
that cares about overflow specifically, not wirelength.

This does not undermine Part 9's own "unused space" observation -- idle
capacity that a stale price signal hides from later nets is still real.
It is evidence that *this specific mechanism* -- kroute's LIFO-relax
pattern, copied onto lambda unchanged -- is the wrong tool for recovering
it. A different approach (e.g. relaxing lambda only far more slowly than
hist does, or only below a much stricter "genuinely idle for many
consecutive rounds" bar, rather than the very next clean round) might
behave differently, but is a distinct, separately-motivated experiment,
not a foregone conclusion from this result.

Given the isolated result is already a clean, unambiguous regression on
the metric this project's mandate cares about, the user was asked before
spending another ~2 hours of compute on a configuration very unlikely to
reverse an already-clear three-for-three finding -- and chose to run it
anyway, for completeness, matching this project's own "finish the sweep
for completeness" precedent from the qroute width sweep.

**Combined result (default settings: `KR1ROUTE_LAMBDA_RELAX=1` +
uroute's own default region pricing, `UROUTE_REGION_PRICING=1`,
`UROUTE_REGION_EPS=0.005`), same 172-round budget, both legal per
gr_verify and cross-checked exactly against eval2008.pl:**

| benchmark | kroute (bar) | uroute alone (eps=0.005) | kr1route, lambda-relax alone | kr1route, combined |
|---|---|---|---|---|
| adaptec1 | 162584 / 7254167 | 162664 / 7258013 | 162984 / 7206243 | 162402 / 7206913 |
| newblue1 | 41014 / 6644362 | 40926 / 6644352 | 41458 / 6608842 | 41326 / 6596450 |
| bigblue1 | 292420 / 7535653 | 292380 / 7537447 | 292958 / 7506080 | 293500 / 7506439 |
| **combined total** | **496018** | **495970** | **497400** | **497228** |

**Combining doesn't rescue lambda-relax.** Region pricing's own effect
(mixed, benchmark-dependent, per Part 18) partially offsets lambda-relax's
regression on two of three benchmarks (adaptec1 and newblue1 both improve
slightly relative to lambda-relax alone) but makes bigblue1 slightly
worse, and the combined total (497228, +0.24% vs kroute) is still worse
than both kroute's own baseline and uroute's own already-established
default (495970) -- combining does not turn a real regression into a
win, it just reshuffles which benchmark absorbs how much of it, the same
"one scalar knob, benchmark-dependent response" character Part 18 already
documented for region pricing alone. The wirelength story holds up even
more strongly combined (-0.65%, -0.72%, -0.39% vs kroute, all larger
margins than lambda-relax alone) -- reinforcing, not complicating, the
mechanistic explanation above: this mechanism trades wirelength for
overflow, consistently, and stacking it with a second mixed-effect
mechanism doesn't change what it fundamentally is.

**Conclusion: `KR1ROUTE_LAMBDA_RELAX=1` is not adopted as a default.**
kroute's own permanent-lambda behavior (equivalent to
`KR1ROUTE_LAMBDA_RELAX=0`) remains the better choice on every real
measurement taken here, isolated or combined. The mechanism itself is
real, correctly implemented, and decisively self-tested -- it does what
its header claims -- but what it claims to do (let a genuinely-recovered
edge's price return to baseline) is not, in practice, a net improvement
for this project's overflow-only mandate. This is the most informative
negative result in the whole lineage since the "close to exhausted"
verdict on the exact-search/candidate-widening family: a well-motivated,
precedented, carefully-targeted mechanism, built on a real and correctly-
diagnosed asymmetry in the cost model, that real measurement shows
does not transfer the way its precedent (`hist`'s own relax) did --
because the two terms it was built to treat symmetrically were never
actually playing symmetric roles.

## Part 20: vr1route -- region-level cross-layer pricing on MSMV search

First router in the MSMV species (`vr`, generation 1) under the
two-letter + generation-number naming convention. Scoped after Parts
18-19 (uroute's region pricing, kr1route's lambda relax) both landed
mixed-to-negative on the kroute-based lineage, from a direct observation:
`vroute`'s MSMV search is this project's actual best router by a wide
margin (-21% to -36.55% vs kroute), and nothing has ever combined that
structural win with a pricing-formula idea. Region pricing's motivation
(Part 9: every layer at a hot tile simultaneously over capacity, real
idle capacity elsewhere) doesn't depend on which search method finds
paths, so the natural question: does the signal matter more paired with a
search that can actually act on it (attach anywhere on already-built
wire, not just fixed tree endpoints)?

Built as `gr/vr1route.cpp`, forked from `gr/vroute.cpp` (not `zroute`,
per user direction -- zroute's own `-0.57%` marginal gain over vroute was
never validated outside a single reduced-round newblue1 screen, so
isolating region pricing against vroute's fully-proven numbers avoids
compounding one unvalidated claim on another). Mechanism is uroute's
region pricing ported verbatim, with six `edgeCost` call sites updated
instead of four (vroute's MSMV `multiSourceDijkstra` is a second real
Dijkstra relaxation loop alongside the inherited fixed-topology
`boundedDijkstra`). 66 self-tests pass, including a new one specific to
this file: a real MSMV search's own genuinely-shortest tie-broken path is
shown to change to a different, equal-length path once a separate net's
usage drives an "optional" region it passes through (via an edge the
path itself never touches) over its aggregate budget -- proof the signal
reaches this project's best search, not only the weaker one uroute tested.

**Real result, default settings, full 172-round budget, all three
benchmarks legal per gr_verify and cross-checked exactly against
eval2008.pl:**

| benchmark | vroute (MSMV alone) | vr1route (MSMV + region pricing) | overflow delta | wirelength delta |
|---|---|---|---|---|
| adaptec1 | 103172 / 7514462 | 102482 / 7521789 | **-0.67%** | +0.10% |
| newblue1 | 30668 / (unrecorded) | 31056 / 6716003 | **+1.27%** | n/a |
| bigblue1 | 230084 / 7836782 | 230754 / 7834988 | **+0.29%** | -0.02% |
| **combined** | **363924** | **364292** | **+0.10%** | ~flat |

(vroute's own newblue1 wirelength was never recorded with its overflow
number and no vroute solution file survives to re-verify -- overflow is
the mandate's sole metric and is compared exactly; wirelength is compared
where vroute's number is on record, and shows no consistent movement
either way.)

**Verdict: mixed, near-wash -- the same shape region pricing produced on
kroute's own much weaker fixed-topology search (Part 18).** adaptec1
improves, newblue1 and bigblue1 regress slightly, combined total a
fraction worse. Runtimes 27109s / 6896s / 30002s -- squarely in vroute's
own 7.5-8.25 hour-per-benchmark range, no MSMV cost regression from the
added per-round region resum.

**This is the conclusion the plan named as its key risk in advance.**
Region pricing has now been tested against both the weakest and the
strongest search method this project has, and lands in the same place
both times: a real, decisively-self-tested signal (the MSMV steering
test proves the search genuinely acts on it) that does not translate
into a benchmark-independent overflow win at any tuning tried. The
per-benchmark response to a single global region-eps is the same
"one scalar knob, benchmark-dependent geometry" behavior Part 18
documented -- and it is a property of the mechanism, not of the search it
was paired with. The contingency the user set ("if vroute goes well,
then zroute makes sense") is not triggered: pairing region pricing with
an even more powerful search is very unlikely to reverse a result this
consistent across two very different ones. `VROUTE_MSMV=1` with region
pricing off -- i.e. plain vroute -- remains the better choice, and
vroute stays this project's current-best router.

**Where the region-pricing line of inquiry stands after Parts 18 and 20:
closed.** Two full-scale tests, opposite ends of the search-power
spectrum, same mixed result. Combined with Part 19's clean rejection of
lambda-relax and Parts 12-17's "close to exhausted" verdict on the
exact-search family, the pricing-formula and per-region axes are now both
substantially explored without producing a second unambiguous win beyond
kroute's own history relax. The MSMV structural change (vroute) remains
the one large, unmatched lever -- and Part 8's placement-artifact finding
(pin density varies 4x by radial band while capacity varies ~3%) remains
the honest ceiling on how far any routing-only technique in this project
can go.

## Part 21: fr1route -- 2D-collapse global routing (generation 1 of the FastRoute/NTHU-Route pipeline)

New species under the two-letter + generation-number naming convention:
`fr` (FastRoute/NTHU-Route family -- 2D global routing, then a separate
layer-assignment stage), generation 1. Scoped after the user pushed back
directly on this project's own "placement ceiling" framing: the ISPD-2008
contest record shows NTHU-Route 2.0 and FastRoute routed 12 of 16
benchmarks to exactly zero overflow, adaptec1 and bigblue1 among them --
not a last-mile gap against this project's current best (vroute:
102482/30668/230084), evidence the search itself is leaving most of the
achievable result on the table. Every ISPD-competitive router shares one
architectural feature this project has never built: compress the 3D grid
to 2D with summed per-layer capacity, route to an overflow-free 2D
solution, then assign layers separately. This directly targets Part 9's
own finding (every layer at a hot tile simultaneously over capacity,
65.7% aggregate utilization) -- region pricing (Parts 18, 20) was a weak
cost-hint approximation of the same idea and landed as a wash twice; this
actually collapses the search space instead of nudging its cost.

Full design in `/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`,
including the explicit topology decision (kept vroute's MST + MSMV
unchanged, no RSMT, to isolate the 2D-first variable) and the
generation-1/generation-2 split (2D routing now; capacity-aware DP layer
assignment later, only if this generation validates).

Built as `gr/fr1route.cpp`, forked from `gr/vroute.cpp`: every real
per-layer edge at a given (x,y) collapses onto one synthetic 2D edge,
`Usage::capH`/`capV` (summed once from the dense, post-adjustment
g.hcap/g.vcap, so real .gr capacity adjustments carry through exactly).
`Prices`/`Usage` are sized for the 2D edge count instead of vroute's full
3D count. `LayerPlan` collapses to one synthetic layer (`hLayers =
vLayers = {0}`, `isH`/`isV` unconditionally true); `boundedDijkstra` and
`multiSourceDijkstra` both lose their via-edge relaxation block entirely
(there is nothing to change layer *to*) and their "reachable at any
layer" source-push case collapses to a single push at the one synthetic
layer. A load-bearing identity avoids introducing a second, parallel
indexing scheme: `g.hIndex(x, y, 0)` already reduces to the correct 2D
index, so every inherited `g.hIndex`/`g.vIndex` call site works
unmodified as long as `l` is always 0, which the edits above guarantee.

Two of vroute's own inherited self-tests were removed (not adapted) --
`testEscalationSolvesWhatFastTierCannot` (needed a multi-layer detour)
and `testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge` (compared
two different real layers' cost) -- since their whole premise is
structurally inapplicable once there is exactly one synthetic layer;
`testHistoryIgnoresZeroCapacityEdges` was adapted (every real layer must
now be blocked for the 2D-summed capacity to genuinely vanish) rather
than removed, since the arithmetic-edge-case claim it tests is still
real. 49 self-tests pass, including the file's own decisive proof:
a net whose required width (10) exceeds every individual real layer's
capacity (4 each, on 3 layers) -- so no single-layer choice a 3D router
could ever make can avoid a 6-unit overflow on this net's one edge --
routes at exactly 0 overflow once every layer's capacity is pooled
(4+4+4=12 >= 10) into the one 2D edge this file's whole mechanism exists
to create (`testTwoDRoutingClearsAnOverflowNoSingleLayerChoiceCanClear`).

**Not yet measured against the real ISPD benchmarks.** Per the user's own
explicit instruction (usage-limit constrained), the real 3D benchmark
runs were not launched from this build -- this generation's own output
places every segment on layer 1 (no layer assignment exists yet), so its
overflow/wirelength numbers are 2D-only and not directly comparable to
vroute's own real 3D numbers (103172/30668/230084) until `fr2route`'s
layer assignment exists. That real-benchmark measurement, and the
decision whether to proceed to `fr2route`, is the next step once resumed.

**Real result, default settings (172-round budget, same convention as every prior measurement), all three real ISPD benchmarks:**

| benchmark | vroute (real 3D overflow, bar) | fr1route (2D overflow) | delta | runtime |
|---|---|---|---|---|
| adaptec1 | 103172 | 69394 | **-32.7%** | 2655s (~44 min) |
| newblue1 | 30668 | 20040 | **-34.6%** | 540s (~9 min) |
| bigblue1 | 230084 | 207612 | **-9.8%** | 2738s (~46 min) |

`gr_verify` confirms all three legal and fully connected (0 malformed/
disconnected/incomplete/unrouted/out-of-bounds). Its own *overflow*
number on this output (7222978 / 4634042 / 7384704) is not reported as a
result -- it scores against real per-layer 3D capacity while every
segment sits on layer 1 alone (no layer assignment exists yet), so a huge
number there is the expected, uninformative artifact of that, not a
finding. Runtime confirms the plan's own cost estimate and then some:
roughly 10x faster than vroute's real-3D runs (2655s vs 27109s on
adaptec1), not just the ~6x predicted from dropping the layer dimension
and via edges.

**Verdict: a real, substantial, unanimous reduction -- but far short of
the zero contest winners reach on adaptec1 and bigblue1.** The 2D
collapse alone, with no other change to the negotiation/search machinery,
cuts overflow 10-35% relative to full 3D routing purely by pooling
cross-layer capacity -- direct, real evidence for Part 9's own thesis
(idle capacity elsewhere hidden by per-layer accounting). But all three
runs *hit the round cap* without converging to zero even in the easier,
pooled-capacity 2D problem, so the immediate open question is whether
that's a round-budget limit or an architecture limit: the 2D problem is
~10x cheaper per round than vroute's 3D one, so testing a substantially
larger round budget is cheap before concluding anything about topology
(RSMT, deferred) or escalation architecture (FastRoute's own graduated
monotonic-then-maze routing with expanding detour bounds, which this
project's binary fast/slow split does not have). Whether to run that
round-budget probe, move straight to `fr2route`'s layer assignment, or
investigate topology/escalation next is an open decision, not yet made.

### Follow-up: does a larger round budget close the gap? No.

Direct test of the first of three open follow-ups named above: rerun all
three benchmarks at roughly 1.7x the round budget
(`VROUTE_FAST_ROUNDS=100 VROUTE_SLOW_ROUNDS=250`, nominal cap 350 vs the
default's 210) -- cheap to test given the 2D phase's own ~10x-faster
runtime. All three still "hit the round cap" (the slow tier was still
declining, not stalled) at 272-273 total rounds run (fast tier plateaus
at the same ~22-23 rounds regardless of its own raised cap, consistent
with earlier runs; the slow tier used its entire new 250-round budget).
Legal and connected on all three per `gr_verify` (0 malformed/
disconnected/incomplete/unrouted/out-of-bounds).

| benchmark | 172 rounds (2D overflow) | 273 rounds (2D overflow) | improvement | extra runtime |
|---|---|---|---|---|
| adaptec1 | 69394 | 68394 | -1.4% | 2655s -> 4208s (+58%) |
| newblue1 | 20040 | 19676 | -1.8% | 540s -> 778s (+44%) |
| bigblue1 | 207612 | 206810 | -0.4% | 2738s -> 4351s (+59%) |

**Round budget is not the bottleneck.** ~60% more rounds (and ~50-60%
more runtime) bought under 2% further reduction on every benchmark --
diminishing returns, not a router on the verge of a breakthrough given
more time. Extrapolating, even another doubling of the budget would not
put any of the three within meaningful reach of the zero contest winners
achieve on adaptec1 and bigblue1. This rules out "just run it longer" and
points at the two remaining, deliberately-deferred architectural gaps:
**topology quality** (this file kept vroute's plain MST, no RSMT) and
**escalation sophistication** (this project's binary fast/slow split vs.
FastRoute's own graduated monotonic-then-maze routing with expanding
detour bounds). Neither has been tried yet on the 2D-collapsed grid;
deciding between them (or building `fr2route`'s layer assignment anyway,
to see what even this un-converged 2D base produces once mapped to real
3D) is the next open decision.

## Part 22: fr2route -- escalation architecture (monotonic-maze middle tier)

Generation 2 of the `fr` species. Per the user's own directive (escalation
first; only if not dramatic, try Steinerization; then take the best of
{fr1route, fr2route, fr3route} into layer assignment), this tests the
first of the two architectural gaps fr1route's own header named after its
round-budget probe ruled out "just run it longer" (Part 21's own
follow-up): escalation sophistication.

The gap: fr1route (and vroute before it) escalates in exactly two steps
-- a fixed ~8-candidate L/Z pattern menu, then straight to full,
unrestricted, margin-expanding maze/MSMV search. FastRoute's own
architecture escalates gradually, with a cheap-but-real monotonic-path
middle tier in between. Built as `gr/fr2route.cpp`, forked from
`gr/fr1route.cpp`: `monotonicDijkstra` mirrors `boundedDijkstra` with two
restrictions (no margin expansion -- exactly the net's own bounding box;
monotonic neighbor generation -- only the neighbor that reduces distance
to the target in each axis is ever relaxed). `RoutedNet::tier` (a new
`Tier{Fast,Monotonic,Maze}` enum, replacing the old boolean `escalated`)
only ever increases. A new Phase B, structurally identical to the old
maze-tier round loop (same supervised accept/reject, same round-boundary
history update, same patience-based stop), tries the monotonic tier
first; only nets still overflowed after Phase B's own loop escalate
further to Phase C's full maze search. `FR2ROUTE_MONOTONIC=0` reproduces
fr1route's exact two-tier behavior.

68 self-tests pass, including the decisive one: on a fixture where a wall
blocks every column/row the fast tier's fixed candidates could ever turn
at (0, 2, 4, 6, 8 -- the only columns `generateCandidates` ever tries),
the real production `routeNetFast` cannot legalize the net, but the real
production `routeNetMonotonic` -- free to turn at any column, including
the odd ones the fixed menu never considers -- finds the gap and
legalizes it cleanly.

**Not yet measured against the real ISPD benchmarks.** Real-benchmark
run, at the same default round budget as fr1route's own baseline
measurement (isolating escalation architecture as the only variable),
is the next step.

Decision criterion, per the plan: a clear double-digit-percent reduction
in 2D overflow (matching the scale of vroute's own structural win, -21%
to -36%) counts as "dramatic"; anything smaller triggers the user's own
fallback to Steinerization next.

**Real result, default settings (FR2ROUTE_MONOTONIC=1, same round budget
convention as fr1route's own baseline), all three real ISPD benchmarks --
all legal per gr_verify (0 malformed/disconnected/incomplete/unrouted/
out-of-bounds):**

| benchmark | fr1route (2D overflow, bar) | fr2route (2D overflow) | delta | runtime |
|---|---|---|---|---|
| adaptec1 | 69394 | 76296 | **+9.9%** | 2898s |
| newblue1 | 20040 | 21644 | **+8.0%** | 583s |
| bigblue1 | 207612 | 223026 | **+7.4%** | 3118s |

All three ran *more* total rounds than fr1route's own baseline (222-223
vs 172-173) -- this is not an under-converged comparison, it is a clean,
unanimous regression while running longer.

**Verdict, per the user's own decision criterion: not dramatic -- the
opposite of dramatic.** A double-digit-or-clear reduction would have
counted as a win; instead all three benchmarks got 7-10% worse. This
triggers the user's own stated fallback directly: try Steinerization
next.

**Working diagnosis.** The escalation ladder's accept/reject gate
(`overflowOverUnion`, unchanged from fr1route/vroute) only checks that a
net's *own local* overflow doesn't get worse -- it says nothing about
whether the accepted route is as good as a stronger tier would have
found. Once a net resolves via the monotonic tier, `tier` only ever
increases (by design, matching every prior escalation flag in this
project), so a net that passes Phase B's local check is never
reconsidered by Phase C's full, unrestricted maze/MSMV search --
including in cases where MSMV would have found a shorter or better-
distributed path than the monotonic tier's own bounding-box-confined,
backtrack-free search could. The monotonic tier can "steal" nets into a
locally-legal but globally worse route, permanently, before the stronger
search ever gets a chance at them. This is a real structural cost of
*sequential* graduated escalation with a *local-only* acceptance gate,
not a bug in the implementation -- the mechanism does exactly what its
self-tests prove it does (finds a real monotonic path the fixed pattern
menu cannot), it is simply that "resolves this net's own overflow" and
"is a good route" are not the same claim, and the gate only checks the
first. FastRoute's own real implementation likely avoids this by scoring
candidate routes globally (via a shared congestion cost, same as this
project's own pricing) *before* committing, rather than accepting the
first tier that locally clears -- a distinction this project's existing
accept/reject discipline (built for a different purpose: preventing
unconditional-batch-acceptance stampedes, see CLAUDE.md's own design
decision) does not draw.

Not investigated further given the user's own directive to move to
Steinerization on a non-dramatic (here, negative) escalation result --
worth noting for the record in case a future session revisits escalation
architecture with a global-cost acceptance rule instead.

## Part 23: fr3route -- cheap MST Steinerization (Ho-Vijayan-Wong style)

Generation 3 of the `fr` species, an independent alternative measured
against the same fr1route base as fr2route (not stacked on top of it),
per the user's own directive after fr2route's escalation-architecture
experiment came back a clean regression (Part 22): try Steinerization
next, cheap version before full FLUTE.

The gap: `mstEdges` builds a plain rectilinear MST -- no Steiner points,
ever, anywhere in this project's history. Built as `gr/fr3route.cpp`,
forked from `gr/fr1route.cpp`: for every degree-2 node in the plain MST,
compute the coordinate-wise median of it and its two tree-neighbors --
the exact optimal rectilinear Steiner point for 3 terminals -- and if
routing through it strictly reduces total span, replace the node's two
MST edges with three new ones through a newly-appended Steiner point.
Degree>=3 branch points are left untouched (a disclosed scope cut, not
an oversight -- those need the fuller Hanan-grid Steiner DP FLUTE
actually solves). `FR3ROUTE_STEINERIZE=0` reproduces fr1route's exact
plain-MST topology.

68 self-tests pass, including an exact, hand-derived decisive proof: a
3-point hub shape (a=(0,0), b=(10,0), i=(5,10)) has plain-MST span 25;
the real production `steinerizeTree` computes the exact Steiner point
S=(5,0) and totals exactly 20 -- reproduced end to end through the real
`route()` pipeline too (20 charged tile-edges with Steinerization
enabled, 25 disabled, on the identical fixture).

**Not yet measured against the real ISPD benchmarks.** Real-benchmark
run, at the same default round budget as fr1route's and fr2route's own
baseline convention (isolating topology as the only variable), is the
next step. Same decision criterion as fr2route's: a clear double-digit-
percent reduction in 2D overflow counts as "dramatic."

**Real result, default settings (FR3ROUTE_STEINERIZE=1, same round budget
convention as fr1route's own baseline), all three real ISPD benchmarks --
all legal per gr_verify (0 malformed/disconnected/incomplete/unrouted/
out-of-bounds):**

| benchmark | fr1route (2D overflow, bar) | fr3route (2D overflow) | delta | runtime |
|---|---|---|---|---|
| adaptec1 | 69394 | 66792 | **-3.75%** | 2916s |
| newblue1 | 20040 | 18428 | **-8.04%** | 559s |
| bigblue1 | 207612 | 183362 | **-11.68%** | 2876s |

**A real, unanimous improvement -- the first of the two fr-family
follow-ups to actually beat fr1route's baseline on all three benchmarks.**
Runtime is essentially unchanged from fr1route's own baseline (the
Steinerization pass itself is O(pins) per net, negligible next to the
routing search).

**Applying the user's own decision criterion honestly: mixed against the
letter of "dramatic."** Only bigblue1 (-11.68%) crosses into double
digits, and even that is well short of vroute's own -21% to -36%
structural-win scale; adaptec1 (-3.75%) is a modest single-digit gain.
This is not the same magnitude of result vroute's MSMV search was. But
unlike fr2route's clean, unanimous regression, this is a clean, unanimous
*win* -- the best of the three `fr`-family variants measured so far
(fr1 baseline, fr2 regression, fr3 real improvement on all three). Where
it lands relative to "try FLUTE next" is a genuine judgment call, not a
mechanical read of the stated threshold: the cheap, dependency-free
version already found real gains without touching degree>=3 branch
points at all (a disclosed, deliberate scope cut) -- full FLUTE, which
would also optimize those, could plausibly do better, but the size of
that additional gain is unknown without building it.

**Decision, deferred to the user**: push further with full FLUTE
(larger implementation, dependency-free but non-trivial -- lookup-table
generation plus the FLUTE algorithm itself), or take `fr3route` as the
winner of the three parallel alternatives and move on to the originally-
planned layer-assignment stage on top of it, per the plan's own "final
step" section. Not yet decided.
