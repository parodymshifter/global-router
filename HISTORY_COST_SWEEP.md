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

