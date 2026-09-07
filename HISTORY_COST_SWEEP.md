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

