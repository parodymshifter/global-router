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

## Part 24: fr4route -- exact rectilinear Steiner minimal tree (Dreyfus-Wagner DP), the "try full FLUTE" step

Decision from Part 23: the user chose to push further -- "try full FLUTE
-- then see which of the variants gets to the lowest overflow and take
that one to full layer assignment."

**Framing, disclosed up front**: literal FLUTE is a lookup-table speed
hack -- fast because Chu's original work precomputed, via an offline
exhaustive/POWV search, the optimal Steiner topology for every terminal
configuration up to degree 9, then ships that table. Reproducing that
table from scratch is a separate, multi-week research artifact, and this
project's own conventions rule out vendoring an external dependency or
datafile. `gr/fr4route.cpp` instead builds the **exact rectilinear
Steiner minimal tree (RSMT)**, for the same degree range FLUTE targets
(bounded terminal count, default <=8 unique pins), via the classical
Dreyfus-Wagner Steiner-tree dynamic program over the Hanan grid -- slower
per net than a table lookup, but exact rather than looked-up-approximate,
achieving the same practical goal FLUTE exists to deliver, from scratch,
with no external data.

Unlike fr2route and fr3route (each an independent alternative measured
against the common fr1route base), fr4route forks from `gr/fr3route.cpp`:
it is a strict refinement of fr3's cheap pass, not a parallel alternative
to it -- exact-solve the nets fr3's degree-2-only median trick
structurally cannot help (any branch point of degree>=3), and keep fr3's
own already-proven cheap pass as the fallback for nets above the degree
cap. Full derivation (Hanan-grid construction, the DP's merge/relax
steps, traceback reconstruction) in `gr/fr4route.cpp`'s own header.
`FR4ROUTE_EXACT_STEINER=0` disables the exact solve (falling back to
fr3route's own behavior, itself still gated by the inherited
`FR3ROUTE_STEINERIZE`); `FR4ROUTE_EXACT_MAX_PINS` (default 8) sets the
degree cap.

76 self-tests pass, including the decisive proof fr3route's own cheap
pass structurally cannot reach: a 4-point "hub" fixture (A=(0,3),
B=(4,3), C=(2,6), D=(2,0)) whose plain MST gives branch point A degree 3
-- outside fr3's degree-2-only scope, so its cheap pass is a no-op and
total span stays at the plain MST's 14. The real Dreyfus-Wagner DP finds
the true optimum, a "plus"-shaped tree through the exact hand-derived
Steiner point (2,3), totaling 10 -- a 28.6% reduction reproduced end to
end through the real `route()` pipeline (two independent copies of this
fixture as separate nets: 20 total charged tile-edges with the exact
solve enabled, 28 with both Steinerization mechanisms disabled). Also
verified: two additional hand-computed small fixtures cross-checking the
DP's cost against independently-derived optima; the degree-cap fallback
path routes a 9-unique-pin net identically to calling fr3route's own
`steinerizeTree` directly; disabling only the exact solve (leaving
fr3route's cheap pass on) falls back to it correctly rather than
silently reaching all the way down to the plain MST; tree-validity
(union-find) on the exact solve's own output; and 2-pin/already-optimal
nets are confirmed untouched.

**Real result, default settings (FR4ROUTE_EXACT_STEINER=1, same round
budget convention as fr1route's and fr3route's own baseline), all three
real ISPD benchmarks -- all legal (0 malformed/out-of-bounds/disconnected
/incomplete/unrouted segments per `gr_verify`; its own "total overflow"
verdict is not the comparable metric here, same as every prior
`fr*route` entry -- raw output has no layer assignment yet, so every
segment piles onto one synthetic layer and gr_verify's real-3D-capacity
overflow check does not apply until that stage exists):**

| benchmark | fr1route (2D overflow, bar) | fr3route (2D overflow) | fr4route (2D overflow) | fr4 vs fr1 | fr4 vs fr3 | runtime |
|---|---|---|---|---|---|---|
| adaptec1 | 69394 | 66792 | **63966** | **-7.82%** | -4.23% | 3016s |
| newblue1 | 20040 | 18428 | **18156** | **-9.40%** | -1.48% | 577s |
| bigblue1 | 207612 | 183362 | **180060** | **-13.27%** | -1.80% | 2943s |

**A real, unanimous improvement over both prior variants -- fr4route
beats fr1route's plain-MST baseline and fr3route's cheap Steinerization
on all three benchmarks, with no regression anywhere.** Runtime is
essentially unchanged from fr1route's and fr3route's own baseline (the
DP's cost is trivial per net at the default 8-pin cap; only the rare
higher-degree nets pay any real DP cost at all, and the vast majority of
real nets are 2-3 pins, which either skip the DP entirely or resolve it
almost instantly).

**Applying the user's own "dramatic" criterion honestly, one more time:**
closer than fr3route was, but still not a clean sweep of double digits.
bigblue1 (-13.27% vs fr1) crosses into double digits; adaptec1 (-7.82%)
and newblue1 (-9.40%) are high single digits, just short. Measured
against fr3route specifically (the more informative comparison, since
fr4route is a strict refinement of it, not an independent alternative),
the incremental gain from exact-solving degree>=3 branch points on top
of fr3route's already-captured degree-2 gains is real but modest
(-1.48% to -4.23%) -- most of the achievable Steiner benefit on these
benchmarks was already low-hanging and fr3route's cheap pass had already
picked most of it; the exact DP's own additional contribution, while
unanimous and free (no runtime cost, no regression), is a smaller slice
than fr3route's own jump over the plain MST was.

**Decision, per the user's own explicit instruction ("see which of the
variants gets to the lowest overflow and take that one to full layer
assignment"): fr4route.** It has the strictly lowest 2D overflow of all
four variants (fr1/fr2/fr3/fr4) on every one of the three real
benchmarks -- not a judgment call this time, a direct read of the
numbers above. `gr/fr4route.cpp` is the topology this project carries
forward into the full layer-assignment stage, the originally-deferred
final step from the plan that first scoped fr1route.

## Part 25: fr5route -- layer assignment, generation 1 (greedy real-layer selection + via stitching)

Every `fr*route` file through fr4route only solves the *2D-collapsed*
problem: every real layer's capacity is summed into one synthetic layer,
every routed segment is dumped onto a single placeholder real layer
(`Seg.l1==Seg.l2==0` always, vias always 0), and `gr_verify`'s own "total
overflow" -- this project's actual zero-overflow mandate metric -- is not
even meaningful on that output. `FR1ROUTE_TWOD_ONLY` has been carried,
documented but inert, through every generation since fr1route
specifically reserved it for this moment: **layer assignment**, the
second half of the FastRoute/NTHU-Route pipeline this whole species is
modeled on.

Built as `gr/fr5route.cpp`, forked from `gr/fr4route.cpp` (same species,
not a new core 2D-search algorithm -- this is the pipeline's second
stage, not a variation on the first). Scope for this generation,
disclosed up front: a full jointly-optimized (wire-congestion + via-cost)
layer assignment is a real DP problem -- the "capacity-aware DP" half of
the two-stage split the original fr1route plan first sketched. This
generation builds the cheaper half first, per this project's own repeated
"cheap version before the full one" discipline: **greedy, capacity-aware,
per-segment real-layer selection** (each leg independently picks the
direction-matching real layer with the lowest running usage-over-capacity
ratio, no negotiated pricing, no joint via-cost optimization), followed
by a **separate, unoptimized via-stitching pass** that guarantees
physical connectivity wherever two legs meeting at a point land on
different real layers (reference = the minimum layer present; a chain of
unit vias bridges every other layer to it). Via cost/capacity is not part
of the optimization objective in this generation -- a disclosed scope
cut. `FR1ROUTE_TWOD_ONLY=1` is now finally functional: it skips
`assignLayers()` entirely, reproducing fr4route's exact single-layer,
zero-via output.

All 30 of fr4route's own inherited self-tests still pass unchanged, plus
6 new ones added for this generation's own mechanism (36 test functions,
133 individual checks total, 0 failures) -- see `fr5route.cpp`'s own
self-test namespace. The 6 new tests cover: capacity-awareness
that's real, not a static rule (two nets forced to share one edge land on
two different real layers, the second net's choice driven by the first's
already-committed usage); direction preference never violated (a
horizontal-only layer never receives a vertical leg and vice versa); via
stitching verified directly on a hand-built three-layer mismatch (legs on
layers 0, 2, and 4 meeting at one hub point end up fully connected in 3D
via exactly 6 unit-step via segments, none wasted); a multi-tile-edge leg
charging every unit edge it covers, not just the first; `FR1ROUTE_TWOD_ONLY
=1` byte-for-byte reproducing fr4route's placeholder output; and an
end-to-end multi-net, multi-layer design confirmed fully connected in 3D
(real layers plus stitched vias) through the real `route()` ->
`assignLayers()` pipeline.

**Three real bugs were found and fixed during this generation's own
real-benchmark verification, before any result was recorded as
settled** -- exactly the discipline this project's verification
principles exist for. In order:

1. **Pin-declared-layer connectivity** (fix `5fe1185`): `gr_verify`
   (matching `eval2008.pl`'s own real scoring) requires a route to touch
   each pin's *exact* declared layer, not just its tile on any layer.
   fr1route through fr4route's uniform single-layer output happened to
   satisfy this by coincidence (these benchmarks apparently declare
   most/all pins on layer index 0), masking the gap entirely until this
   generation's own greedy layer choice broke it at massive scale (98%
   of adaptec1's nets came back "incomplete"). Fixed with
   `stitchPinAccessForNet`.
2. **Mid-span MSMV via-stitching** (fix `542821b`): the first via-
   stitching pass only checked segment *endpoints*, missing MSMV's own
   defining mechanism -- attaching new wire to any point on already-
   committed wire, mid-span. With 75993 of 176716 nets on adaptec1
   escalating to the slow/MSMV tier, this broke 5974 nets into multiple
   disconnected pieces. Fixed by replacing the endpoint-only scan with
   `coverageAt`, recording every real layer present at every unit tile a
   segment's full span covers.
3. **Degenerate zero-length segments** (fix `8c60a98`): `eval2008.pl`
   rejects any segment whose two endpoints match exactly on the same
   layer as a hard "null route" error. Traced to `commitCandidate`'s own
   inherited leg-commit code (unchanged since `vroute.cpp`), which emits
   exactly this whenever an L/Z-shape candidate's own leg is zero-length
   -- confirmed byte-for-byte identical in `fr4route`'s own earlier real
   output (net n7807 alone had 200+), never caught because `gr_verify`
   doesn't flag it and `eval2008.pl` was never run against any
   2D-collapsed generation's raw output before this one. Fixed with
   `dropDegenerateSegs`.

**Real result, all three fixes applied, same default round budget as
fr4route's own baseline, all three real ISPD benchmarks -- all legal per
`gr_verify` (0 malformed/out-of-bounds/disconnected/incomplete/unrouted),
and `gr_verify`'s own total overflow/max overflow/scored wirelength agree
exactly with the independent `eval2008.pl` scorer on every benchmark --
the first time in the whole `fr` lineage this cross-check has been
meaningful or run:**

| benchmark | vroute (real total overflow, historic best) | fr5route (real total overflow) | delta vs vroute | via count | runtime |
|---|---|---|---|---|---|
| adaptec1 | 103172 | **93764** | **-9.12%** | 3713931 | 2962s |
| newblue1 | 30668 | **26776** | **-12.69%** | 4340058 | 574s |
| bigblue1 | 230084 | **191976** | **-16.56%** | 4033049 | 2896s |

**A real, unanimous improvement over `vroute`'s own historic real
result -- the best total overflow this entire project has ever measured
against real per-layer capacity, on all three benchmarks.** This is the
first router in the whole project's history to beat vroute's real number
at all; every intermediate `fr1`-`fr4` generation was only ever
comparable to `vroute` via each router's own self-reported 2D-collapsed
number, never head-to-head on the actual mandate metric. Total overflow
remains far from zero (tens of thousands to nearly two hundred thousand)
-- this is a major step, not the destination -- and via count is large
relative to wirelength (roughly one via per wire tile-edge on average),
the direct, expected cost of this generation's disclosed scope cut: via
placement is driven purely by connectivity, never by cost or capacity.

**Why this beats vroute at all, despite `fr5route`'s own 2D-collapsed
search converging to a *worse* 2D overflow than vroute's real search
ever saw** (the 2D numbers were never apples-to-apples with vroute's own
3D ones): pooling every real layer's capacity into one synthetic 2D edge
before routing, then greedily spreading the resulting wires back across
real layers by congestion, evidently finds a materially better balance
of real per-layer utilization than vroute's real-3D-aware but
fixed-topology-free search did on its own -- consistent with this
project's own recurring lesson (first surfaced when vroute itself beat
kroute) that *what the search is structurally allowed to do* moves the
number more than tuning a shared cost formula ever has.

**This is the router carried into whatever comes next.** Per the user's
own original sequencing, the natural next steps -- not yet started,
genuine open choices rather than foregone conclusions -- are: (a) a via-
cost/capacity-aware generation 2 layer assignment (this generation's own
disclosed scope cut, given via count is large enough here to plausibly
matter), or (b) returning to the pricing loop itself now that the whole
pipeline produces a real, comparable 3D number for the first time.

## Part 26: fr6route -- layer assignment, generation 2 (joint via-cost/capacity-aware tree DP)

The user's own chosen next step from Part 25: address fr5route's
disclosed scope cut directly. fr5route's two layer-assignment passes
were structurally blind to via cost -- each segment picked its own
least-loaded real layer independently, and a separate stitching pass
added whatever vias connectivity required, after the fact, with no cost
or capacity model. The measured result showed via count roughly
matching total wirelength (adaptec1: 3.9M wirelength vs 3.7M vias).

Built as `gr/fr6route.cpp`, forked from `gr/fr5route.cpp` (same species,
same pipeline stage, generation 2 of layer assignment specifically).
Core mechanism: a **tree DP jointly choosing every net's own segment
layers to minimize wire cost and via cost together**, replacing the
independent-then-stitch approach entirely. Since `rn.mst` (the fixed
Steinerization tree) is stale for any net that escalates to the
MSMV/slow tier (>40% of nets on adaptec1 -- MSMV attaches new wire to
any point on its own already-committed wire, mid-span, and never
updates `rn.mst` again), the DP first **reconstructs** a clean tree
directly from `rn.segs`'s own geometry (splitting every leg at every
other leg's endpoint that falls strictly inside its own span), then runs
a standard bottom-up/top-down tree DP: for each edge and each candidate
real layer, cost = wire cost (fr5route's own usage/capacity ratio) plus,
for each child edge, the cheapest combination of the child's own layer
and the via chain needed if it disagrees with the parent -- a new real
via-capacity model (`viaIndex`/`viaCapacityPerLayerPair`/`RealViaUsage`,
adapted directly from `mroute.cpp`'s own, the only prior via-capacity
precedent in this project) makes that via cost genuinely capacity-aware,
not just a flat penalty. A root-level virtual-reference step makes even
a branch-point root correctly weigh its own multiple children against
each other, not just independently. If tree reconstruction or the DP
ever fails for a specific net (a redundant/overlapping leg producing a
cycle -- watched for via an explicit union-find check, not assumed
safe), that one net falls back to fr5route's own already-proven-correct
independent-greedy path rather than risking a new correctness bug on
unusual topology. `FR6ROUTE_VIA_AWARE=0` reproduces fr5route's exact
behavior, segment for segment, for direct comparison.

170 checks across 45 test functions pass (all of fr5route's own 39
carried over unchanged, plus 6 new ones), including: the decisive proof
the joint DP avoids an avoidable via a greedy independent choice would
create (a hand-engineered branch point where matching layers, accepting
a wire-cost penalty, beats paying for a tight via site -- confirmed
against fr5route's own inherited `assignSegmentLayer`, called on the
identical starting usage, which does mismatch); via capacity that
actually changes a later net's own decision (two sequential nets sharing
one via site -- the first accepts the cheap, uncongested via; the
second, after the first net's charge, switches to matching layers
instead, since the same via has become more expensive than the wire
penalty); tree reconstruction correctly splitting a mid-span MSMV-style
attachment and remaining a valid tree (edges==nodes-1, connected, no
cycle) on a real 4-way branch point; pin-access vias now genuinely
charged into `RealViaUsage` (entirely free in fr5route); and
`FR6ROUTE_VIA_AWARE=0` reproducing fr5route's own output exactly,
segment for segment.

**Real result, all three real ISPD benchmarks -- all legal per gr_verify
(0 malformed/out-of-bounds/disconnected/incomplete/unrouted), and
gr_verify's own total overflow/max overflow/scored wirelength agree
exactly with the independent eval2008.pl scorer on every benchmark:**

| benchmark | fr5route (real overflow) | fr6route (real overflow) | delta | fr5route vias | fr6route vias | via delta |
|---|---|---|---|---|---|---|
| adaptec1 | 93764 | **148746** | **+58.64%** | 3713931 | 3432150 | -7.59% |
| newblue1 | 26776 | **80164** | **+199.39%** | 4340058 | 4109510 | -5.31% |
| bigblue1 | 191976 | **248336** | **+29.36%** | 4033049 | 3714381 | -7.90% |

**A clean, unanimous regression on the actual mandate metric, real total
overflow, on all three benchmarks -- substantial (+29% to +199%), not a
close call, and consistent in direction across every benchmark, not an
outlier on one.** The via-count reduction this generation was built to
deliver did happen, consistently (-5% to -8%), but at a cost that far
outweighs it: fr6route is now worse than `vroute`'s own historic real
result on all three benchmarks too (adaptec1 +44.17%, newblue1 +161.39%,
bigblue1 +7.93% vs vroute), undoing fr5route's own milestone (the first
router in this project to beat vroute's real number at all). Applying
this project's own decision discipline (fr2route's escalation
architecture and kr1route's lambda-relax are the direct precedents for a
clean, unanimous rejection): **fr6route is rejected as the default; `gr/
fr5route.cpp` remains this project's best real result.**

**Diagnosis, offered as the most likely explanation, not yet confirmed
by further experiment**: the DP's cost model directly sums two cost
domains -- a wire usage/capacity ratio and a via usage/capacity ratio --
with no calibration between them, implicitly assuming one unit of "wire
ratio" and one unit of "via ratio" are comparable costs. A single hand-
checked via site on newblue1 (via capacity 30, derived from via-spacing
1 and tile 30x30) looked reasonable next to that benchmark's own typical
wire capacities (6-28 per layer) -- but that is one site in isolation,
not the aggregate picture. Across hundreds of thousands of real edges,
with many nets simultaneously competing for the same via sites the DP
now actively tries to *reuse* (matching layers to avoid new vias
concentrates traffic onto whichever via sites already got used, the
opposite of what spreading wire across layers independently would do),
a systemic imbalance in how the two ratios accumulate could easily
dominate even when one site's own numbers look fine -- exactly the kind
of aggregate effect a single hand-checked example can't reveal, and
exactly why this project's own discipline insists on measuring real
benchmarks rather than reasoning from a hand-picked case. Bearing this
out (or ruling it out) is future work, not attempted here -- per the
user's own explicit instruction, the cost formula is not being modified
without further direction; this generation's job right now was to
measure and report honestly.

**Decision, deferred to the user**: accept this as a rejected generation
and keep `fr5route` as the standing best (matching the disclosed-risk
framing this generation's own plan already flagged -- via-cost/capacity
awareness was always a real open question, not a foregone improvement),
or attempt a tuning follow-up (e.g. an explicit weighting coefficient
between the wire and via cost terms, tuned empirically against the real
benchmarks the same way every other coefficient in this project's
history has been) before deciding whether the underlying mechanism is
salvageable. Not yet decided.

## Part 27: fr6route -- via-weight sweep, screened on newblue1

The user's own chosen follow-up from Part 26: add an explicit weighting
coefficient between the DP's wire-cost and via-cost terms
(`FR6ROUTE_VIA_WEIGHT`, default 1.0, exactly reproducing Part 26's
rejected result) and sweep it empirically against the real benchmarks,
per this project's own "tune empirically, never by pure reasoning"
discipline, to see whether the diagnosed cost-domain miscalibration is
fixable with a single scale factor.

**Screened on newblue1 alone first** (the fast benchmark, ~575-600s per
run) rather than committing to full 3-benchmark trios per candidate
weight -- 14 weights tested across five orders of magnitude, using
`gr_verify` only at this stage (no `eval2008.pl` cross-check; that is
reserved for whichever configuration, if any, earns a full 3-benchmark
verification). fr5route's own newblue1 baseline: total overflow 26776,
vias 4340058.

| weight | total overflow | vias |
|---|---|---|
| 1.0 | 80164 | 4109510 |
| 0.3 | 43782 | 4099387 |
| 0.1 | 33752 | 4155408 |
| 0.03 | 29922 | 4230207 |
| 0.01 | 28792 | 4274605 |
| 0.003 | 28328 | 4288597 |
| 0.0025 | 28342 | 4289320 |
| 0.002 | 28236 | 4290371 |
| 0.0015 | 28310 | 4289920 |
| 0.001 | 28308 | 4290354 |
| 0.0005 | 28300 | 4290516 |
| 0.0001 | 28342 | 4289888 |
| 0.00003 | 28266 | 4290494 |
| 0.00001 | 28186 | 4290732 |
| **0.0** | **26654** | **4597425** |

All 14 legal per `gr_verify` (0 malformed/out-of-bounds/disconnected/
incomplete/unrouted).

**Finding: the transition from "any nonzero weight" to "exactly zero" is
a genuine step function, not a gradient, and no weight beats fr5route on
both overflow and via count at once.** Every weight from 1.0 down to
0.00001 -- five full orders of magnitude -- lands in one of exactly two
regimes:

- **Weight > 0, however small**: a tight, flat plateau once below ~0.01
  (overflow 28186-28342, vias 4288597-4290732) that never moves further
  no matter how close to zero the weight gets. Consistently ~5.3-5.8%
  *worse* than fr5route on overflow, ~1.1-1.2% *better* on vias.
- **Weight exactly 0**: a sharply different result (overflow 26654, vias
  4597425) -- marginally *better* than fr5route on overflow (-0.46%) but
  clearly *worse* on vias (+5.9%).

The likely mechanism, consistent with Part 26's own diagnosis: many real
edges have exactly tied wire cost across two or more candidate layers
(most commonly, multiple uncongested layers all costing the same
near-zero ratio). At any nonzero via weight, however astronomically
small, such a tie is always broken in favor of matching the neighboring
edge's layer, since doing so avoids some via cost while mismatching
never saves anything (the wire costs are tied). Only at literally zero
does that tie-break vanish, letting the DP's own more exhaustive
per-branch search freely pick among tied layers without regard for the
neighbor -- producing more mismatches, and therefore more vias, than
even fr5route's simpler independent pass finds. This is a real,
structural property of the cost model, not a resolution limit of the
sweep: probing five orders of magnitude below the plateau's own lower
edge (0.001 down to 0.00001) moved the result by less than 1%, while the
jump from 0.00001 to 0.0 moved it by 5-6%.

**Conclusion: no scalar weight rescues this generation on the actual
mandate metric.** The best any nonzero weight achieves is a ~5.3% real
overflow regression in exchange for a ~1.2% via reduction -- and the one
configuration that ties fr5route on overflow (weight 0) does so only by
abandoning via-awareness entirely in the one case that matters (exact
ties), while making via count meaningfully worse. Since this project's
success metric is total overflow, not via count, no point on this
frontier is a genuine improvement over fr5route. A full 3-benchmark
verification was not run for any weight -- the single-benchmark screen
already resolved the question this tuning pass was designed to answer,
and running the expensive full trio would not change the conclusion.

**Decision: `fr6route`'s via-cost/capacity-aware DP is closed out as
rejected, on this cost formulation, without further tuning.**
`gr/fr5route.cpp` remains this project's best real result (adaptec1
93764, newblue1 26776, bigblue1 191976 -- all beating `vroute`'s own
historic real numbers). `gr/fr6route.cpp` stays in the repository,
frozen, as a real, honestly-measured negative result and a base for any
future via-cost-aware attempt with a genuinely different cost model
(e.g. a true minimal-spanning-stack via cost instead of per-neighbor
chains, or a real negotiated/iterative via-pricing loop instead of a
static ratio) -- not modified further under this generation's own name,
per this project's own "build new mechanisms as separate frozen files"
discipline.

## Part 28: fr7route -- true minimal-spanning-stack via cost (layer assignment, generation 3)

The user's own chosen follow-up from Part 27: fix fr6route's actual
cost-model bug rather than continue scaling it. fr6route's own header
already flagged a second, independent hypothesis the weight sweep never
tested: `viaChainCost` charged a full chain from the parent's layer to
*every* child's layer independently, double-counting any shared portion
of a via stack at a node with two or more children. Concretely, a parent
at layer 0 with children at layers 3 and 5 was charged
`chain(0,3)=3 + chain(0,5)=5 = 8` unit-steps, when the true physical cost
is one stack spanning 0 to 5 -- 5 steps, since a single contiguous stack
lets every edge in between tap into it for free. This structurally
biases the DP toward over-avoiding vias exactly at real branch points
(Steiner points, multi-way MSMV attachments) -- exactly where fr6route's
own regression cost the most in sacrificed wire quality.

Built as `gr/fr7route.cpp`, forked from `gr/fr6route.cpp`: `layerAssignDP`
replaces the per-child independent minimization with a joint per-node
stack-range search -- for each candidate parent layer, try every
candidate via-stack range containing it (at most `nLayers^2`, `nLayers`
is 6 for every real benchmark here), and for each range, let every child
independently pick its own cheapest candidate layer that falls inside
it. This is exhaustive, not an approximation, since a child choosing
outside a given range would mean that range was wrong, and every range
is tried. `commitNetTree` now emits exactly one via chain per node,
spanning its own resolved range, instead of a separate chain per parent/
child pair. Everything else (`buildNetTree`, the real via-capacity
model, pin-access charging, the `FR7ROUTE_VIA_AWARE=0` fallback
reproducing fr5route exactly) carries over from fr6route unchanged.
`FR7ROUTE_VIA_WEIGHT` (default 1.0) carries the tuning knob forward,
now scaling the corrected cost.

47 test functions, 190 checks, 0 failures -- 46 inherited from fr6route
unchanged, plus one new decisive test: a hand-engineered real 3-edge
branch point (parent overwhelmingly cheapest at layer 0, two children
overwhelmingly cheapest at layers 3 and 5) confirms the new model
resolves the branch point's own via-stack range to exactly `[0,5]`,
charges exactly 5 via unit-steps into `RealViaUsage` (not 8, and no
layer-pair double-charged), while still letting every edge keep its own
genuinely cheapest layer -- every number hand-derived before running the
test, all matched exactly.

**Real result, default `FR7ROUTE_VIA_WEIGHT=1.0`, all three real ISPD
benchmarks -- all legal per `gr_verify` (0 malformed/out-of-bounds/
disconnected/incomplete/unrouted), `gr_verify` and `eval2008.pl`
agreeing exactly on every benchmark:**

| benchmark | fr5route (real) | fr6route (real, Part 26) | fr7route (real) | fr7 vs fr6 | fr7 vs fr5 | fr6 vias | fr7 vias | vias delta |
|---|---|---|---|---|---|---|---|---|
| adaptec1 | 93764 | 148746 | 148574 | -0.12% | +58.46% | 3432150 | 3413125 | -0.55% |
| newblue1 | 26776 | 80164 | 79420 | -0.93% | +196.61% | 4109510 | 4091578 | -0.44% |
| bigblue1 | 191976 | 248336 | 246352 | -0.80% | +28.32% | 3714381 | 3693453 | -0.56% |

**The overcounting-bug fix alone does not rescue this generation
either.** fr7route is essentially indistinguishable from fr6route's own
rejected result -- within -0.12% to -0.93% on overflow and -0.44% to
-0.56% on vias, on every benchmark, unanimously. Both remain far worse
than fr5route on the actual mandate metric (+28% to +197% real
overflow). The hypothesis that motivated this generation -- that
per-node overcounting was the *dominant* cause of fr6route's
regression -- does not hold up against real data: fixing it, correctly
and confirmed via a hand-derived decisive self-test, moved the real
result by less than 1% in either direction.

**Reading**: this is consistent with the "known risk" this generation's
own plan disclosed up front -- fixing the overcounting bug and the
cost-domain scale mismatch (Part 26's own diagnosis, tested without
rescue via the weight sweep in Part 27) are independent axes, and this
result suggests the scale mismatch, not the overcounting, is the
dominant factor after all. The overcounting bug was real (proven by the
decisive self-test: fr6route's model would have charged 8 via
unit-steps for a case where the true cost is 5) and worth fixing on its
own correctness merits, but apparently affects too small a fraction of
real branch points, or too small a magnitude relative to the scale
mismatch, to move the aggregate real-benchmark result measurably.

**Decision: `fr7route` is rejected as the default on the same grounds as
fr6route -- no improvement over `fr5route` on the actual mandate
metric.** `gr/fr5route.cpp` remains this project's best real result.
`gr/fr7route.cpp` stays in the repository, frozen, as a real negative
result and the corrected cost-model base for any future attempt --
notably, a `FR7ROUTE_VIA_WEIGHT` sweep on this now-corrected model
(the natural combined-fix follow-up the original plan already
anticipated) has not yet been tried, and remains a genuinely open
question deferred to the user: does combining the overcounting fix with
a tuned weight succeed where either alone did not, or has this whole
via-cost-aware line of attack now been reasonably exhausted.

**Follow-up: the combined-fix sweep, screened on newblue1.** The user
asked for this directly. Same methodology as Part 27's own sweep
(`gr_verify` only at the screening stage, output to `gr/runs/sweep7/`),
now on fr7route's corrected cost model:

| `FR7ROUTE_VIA_WEIGHT` | overflow | vias |
|---|---|---|
| 1.0 | 79420 | 4091578 |
| 0.3 | 42994 | 4085648 |
| 0.1 | 33782 | 4136017 |
| 0.03 | 29748 | 4199282 |
| 0.01 | 28768 | 4237207 |
| 0.003 | 28474 | 4248642 |
| 0.0 | 26654 | 4603407 |

(fr5route's own newblue1 baseline: overflow 26776, vias 4340058.)

Same step-function shape as fr6route's own sweep (Part 27): a flat
plateau from weight 0.003 up to 1.0, all worse than fr5route on
overflow, then a discontinuous jump to a different regime at exactly
weight 0 -- no weight bridges the two. fr7route is consistently a few
percent better than fr6route's own sweep at every matching nonzero
weight (the real, if small, overcounting-fix effect already measured at
weight 1.0), and at weight 0 the two generations land on *identical*
overflow (26654 vs fr6route's own 26654) with nearly identical vias --
expected, since at weight 0 the via-cost term is zeroed in both models,
so the overcounting bug (a bug in computing a cost multiplied by zero)
becomes irrelevant and both models mathematically coincide.

**Reading, combining Parts 26 through 28**: fixing the overcounting bug
and tuning the weight are now both tested, separately and together, on
two independent generations, and neither -- alone or combined -- finds
a weight that beats fr5route on both overflow and via count
simultaneously. The via-cost-aware DP line of attack, as a **single-pass,
one-shot cost formula**, is reasonably exhausted: two distinct
hypotheses for what was wrong with it (per-node cost aggregation, and
cost-domain scale) have each been tested to a null result. `gr/fr5route.cpp`
remains this project's best real result. The next escalation the user
chose to pursue is `gr/nv1route.cpp` (Part 29) -- not another cost-formula
variant, but a genuinely different mechanism: a multi-round negotiated-
congestion loop wrapping this same corrected DP, porting the one thing
in this domain that has never been tried (memory of persistent
congestion across multiple passes, the specific mechanism that won for
2D wire pricing via iroute/kroute) rather than another one-shot weight
or aggregation fix.

## Part 29: nv1route -- negotiated via pricing (new species, generation 1)

The user's own chosen next direction after Part 28's combined sweep
closed out the via-cost-aware single-pass line of attack: port
iroute/kroute's own negotiated-congestion mechanism (a permanent-then-
LIFO-relaxable history-cost term, layered on top of a reactive price,
inside a multi-round rip-up/reroute loop) from its original domain (2D
wire-edge congestion) to a domain that has never had it -- via/layer
congestion. Every `fr` generation through fr7route decides each net's
via-stack range exactly once, permanently, in a single pass over all
nets; there is no rip-up path, no memory of a via site being
persistently over capacity, and no way for a net decided early to react
to congestion that only becomes visible once later nets commit. This is
the same structural gap the zero-overflow mandate's own history
(hroute's single reactive price -> iroute/kroute's history-cost fix)
already diagnosed and solved once, in a different domain.

Built as `gr/nv1route.cpp`, forked from `gr/fr7route.cpp` -- a new
species (`nv`, per the 2026-09-09 naming addendum) rather than another
`fr` generation, since the core mechanism changes from a one-shot DP
pass to a multi-round negotiation loop wrapping that same DP. Three new
pieces, each a direct port of a kroute.cpp mechanism (see kroute.cpp's
own header for the original derivation):

- **Per-net charge logs** (`NetCharge`, `undoCharge`/`applyCharge`):
  `commitNetTree`/`stitchPinAccessForNetChargingVias` now optionally log
  every array index (and, for wire usage, the exact amount) they
  increment, so a net's committed layer assignment can be undone and
  redone exactly, cheaply -- no geometry re-walk. `nullptr` (every
  pre-existing self-test call site) preserves fr7route's own behavior
  unchanged.
- **`historyVia`** (`ViaPrices`, `updateViaHistory`): a per-via-site
  multiplicative memory term, grown via kroute's own exact
  streak-gated, LIFO-relaxable formula, keyed by via-site index. Layers
  on top of fr7route's own existing usage/capacity ratio term in
  `spanCost` (which plays lambda's role, left untouched) exactly the
  way kroute's own `hist * (1 + lambda*amount)` separates the two.
- **The round loop** (`negotiateViaAssignment`): round 0 reproduces
  fr7route's own single pass exactly (historyVia all 1.0). Each net's
  `NetTree` is built once and reused every round (rebuilding it from a
  net's own *committed* 3D output would corrupt topology reconstruction,
  since committed segs mix via segments into what `buildNetTree` expects
  to be pure 2D wire geometry). After each round boundary, historyVia
  updates and a net becomes a reprocessing candidate iff its own charge
  log currently touches an over-capacity resource (kroute's own
  `netOverflowed`, ported). Reprocessing is supervised exactly like
  kroute's own accept/revert (kroute's own `overflowOverUnion` pattern,
  ported verbatim): compare overflow over the union of old+new touched
  indices, revert byte-for-byte if it got worse. State rolls back to the
  best-known round at the end if the loop didn't end there, via exact
  incremental undo/redo (simpler than kroute's own rebuild-from-zero,
  since via/wire usage here are exact integer counts with no drift to
  guard against).

53 test functions, 239 checks, 0 failures -- 48 inherited from fr7route
unchanged, plus 5 new decisive tests, every one hand-derived exactly
before running:

1. **Charge-log round-trip identity**: commit a net, undo via its own
   logged charge (ru/rvu return to exact all-zero), redo with unchanged
   inputs (byte-identical ru/rvu and segs to the first commit) --
   confirms no charge site was missed, in particular pin-access
   stitching's own charge (logged outside `commitNetTree` proper, the
   specific risk the plan flagged up front).
2. **Negotiation fixes what a single pass structurally cannot**: five
   structurally identical branch nets (engineered via preload, same
   convention as `kModerateViaGr`) contend for one via site, capacity 1.
   Hand-derived via the DP's own real `resolveNode` mechanics (not a
   simplified abstraction): every net mismatches in round 0 regardless
   of processing order (usage settles at 5, overflow 4 -- provably
   un-fixable by any single pass, since spanCost there never exceeds 5
   against a 49-unit match threshold). `updateViaHistory` grows
   `historyVia` to exactly 11, then 99, across two real production
   calls; reprocessing with those exact values flips exactly 1 net in
   round 1 (usage 5->4) and the remaining 4 in round 2 (usage 4->0) --
   every intermediate number (11, 99, 55, 44, 396, 297, 198, 99) matched
   the hand derivation exactly on the first run.
3. **`NV1ROUTE_HIST_RELAX=0` reproduces permanent-only behavior**, and
   **`=1` undoes exactly one growth step per clean round** -- direct
   ports of the 2D stage's own two decisive relaxation tests, applied to
   `updateViaHistory`/`ViaPrices` instead of `updateHistory`/`Prices`.
4. **Supervised accept/revert primitives detect and undo a worsening
   move**: hand-computed two-resource scenario (old charge on a
   resource 2 over capacity, candidate new charge on a different
   resource that would end up 5 over) -- `localOverflow` correctly
   reports 6 > 2 (worse), and `undoCharge`+`applyCharge` restore both
   resources to their exact pre-attempt usage, byte-for-byte.
5. **`NV1ROUTE_VIA_AWARE=0` reproduces fr5route's exact behavior**,
   segment for segment -- the regression-continuity test every
   generation in this lineage carries.

A real end-to-end smoke test (not a self-test, run manually during this
build): a small deliberately-congested fixture (20 nets, 1 shared pin
tile, via capacity 1 per layer pair) through the actual `main()` CLI
path shows the negotiation loop doing real work -- round 0 overflow 32,
round 1 (18 accepted reroutes) drops it to 30, then plateaus and hits
the round cap -- and `gr_verify` confirms the output stays fully legal
throughout (0 malformed/out-of-bounds/disconnected/incomplete/unrouted;
the residual overflow is expected and not a legality issue -- no router
in this project reaches zero overflow on every input).

**Not yet measured: the real result on the three ISPD benchmarks.**
Round count and runtime at real-benchmark scale are genuinely unknown
going in -- the plan's own disclosed risk. Next step: real-benchmark
verification (`gr_verify` + `eval2008.pl` cross-check on all three),
starting with newblue1 alone to sanity-check round count/runtime before
committing to the full three-benchmark run, per this project's own
established screening discipline.

**Newblue1 screening result, and two bugs found and fixed along the
way.** First screening run (default knobs) legalized cleanly but showed
a puzzling internal number (the loop's own "best round" overflow, 108960)
*worse* than the final `gr_verify`-measured result (47242) -- the loop
was rolling back to the wrong notion of "best." Root cause: `viaCap` has
always been this project's own internal heuristic (inherited from
mroute, unchanged since fr6route), never a real ISPD-scored capacity --
confirmed by reading `gr_verify.cpp`'s own `totalOverflow` computation,
which sums overflow over wire h/v edges only. `localOverflow`/
`totalRealOverflow` were summing wire overflow *and* fictional via-site
overflow together, so the loop's own accept/revert and bestRound/
convergence bookkeeping was judging outcomes by a metric that didn't
match what actually gets scored. Fixed via a new `includeVia` parameter
(default true, every pre-existing call site including the self-tests
unaffected; the negotiation loop's own three internal call sites pass
false) -- historyVia and candidate selection are unchanged (via
congestion still steers the DP's cost function and still decides who
gets reprocessed), only the loop's own judgment of outcomes changed.

A second, smaller bug surfaced closing the remaining gap: the same three
functions clamped wire capacity to `std::max(1, cap)`, appropriate for
`wireCostForEdge`'s own cost *denominator* (avoiding division by zero)
but wrong for an overflow computation, where a genuinely zero-capacity
edge (a preferred-direction zeroing, or a `.gr` adjustment) legitimately
contributes its full usage as overflow -- `gr_verify.cpp`'s own `scan`
never clamps. Removing the clamp closed the internal-vs-`gr_verify` gap
from ~186 units to exactly zero (both report 46908 after the fix,
confirmed three ways: two independent runs matched each other, and each
matched `gr_verify` exactly on its own output).

**Newblue1, final clean number, all legal (0 malformed/out-of-bounds/
disconnected/incomplete/unrouted), `gr_verify`-confirmed:**

| | overflow | vs fr7route (79420) | vs fr5route (26776) |
|---|---|---|---|
| nv1route (default knobs, patience 20/rounds 60) | 46908 | -40.9% | +75.2% |

A large, real improvement over fr7route -- by far the biggest single
move in the whole via-cost-aware lineage, and a structurally different
kind of result than fr6route/fr7route's own sub-1%-either-way weight
sweeps. Runtime (602-607s across three runs) stayed within a few percent
of fr7route's own single-pass baseline, confirming the plan's own
expectation that reprocessing stays cheap.

**Follow-up knob sweep on newblue1** (`NV1ROUTE_VIA_PATIENCE`/
`NV1ROUTE_VIA_ROUNDS` raised to 100/150, three `NV1ROUTE_HIST_STEP`
values, 3 runs in parallel, capped at the project's own 3-concurrent
limit):

| variant | best round | rounds run | overflow | vs fr5route |
|---|---|---|---|---|
| step 0.05 (default), more patience | 19 | 120 | 46908 | +75.2% (identical to the 40-round run -- round 19 is a real plateau, not patience-starved) |
| step 0.2 (faster growth) | 6 | 107 | 47110 | +75.9% (worse -- overshoots) |
| step 0.01 (slower growth) | 85 | 151 (hit the round cap) | 46662 | +74.3% (best found, but did not stall -- still finding new bests at round 85) |

Slower growth finds a somewhat better plateau and hadn't converged when
the round cap stopped it, which is a genuine "there might be a little
more here" signal -- but the magnitude is small (-0.52% over the
default's own plateau, earned across 85 rounds) and, at every knob
setting tried, the result sits firmly in the +74% to +76% range above
fr5route. Extending patience alone (same growth rate) changed nothing at
all, which rules out "it just needed longer to stall" as an explanation
for the earlier runs' own numbers.

**Reading.** This generation answered the question it was built to ask:
does adding memory of persistent via congestion across multiple passes
-- the one mechanism untested by fr6route/fr7route's own single-pass
cost-formula work -- move the needle. It does, by far the largest margin
of any via-cost-aware attempt (-40.9% vs fr7route). But knob-tuning
*within* this mechanism (patience, round budget, history growth rate)
is already showing the same diminishing-returns shape CLAUDE.md's own
vroute section names directly: "the ceiling on how much tuning a shared
cost formula's coefficients can achieve was reached early in this
lineage." A five-generation-deep pattern is now on record -- fr5route's
own plain greedy layer assignment, with no via-cost-awareness at all,
has not been beaten by any of fr6route's, fr7route's, or nv1route's
(three knob variants) attempts to be smarter about via cost specifically
-- which is itself informative: on these benchmarks, via-cost-awareness
in the layer-assignment stage does not appear to be the dominant lever
left on real overflow, regardless of how it's implemented.

**Decision, pending user direction**: further tuning of nv1route's own
history formula (mode, streak threshold, a higher round cap for the
slow-growth variant) is not expected, on this evidence, to close the
remaining ~74% gap to fr5route -- flagged to the user rather than
pursued further on the strength of this screening data alone. `gr/
fr5route.cpp` remains this project's best real result; `gr/nv1route.cpp`
stays in the repository, frozen, as this lineage's best via-cost-aware
attempt and a real, structurally-informative negative result on the
larger question (via-cost-awareness is not, on this evidence, the
lever that closes the gap to zero overflow). Full three-benchmark
verification was not run -- the newblue1 screening pattern across every
prior generation in this lineage (fr6route, fr7route) has always held
unanimously across all three real benchmarks, so a result this far from
fr5route on the screening benchmark was judged not worth the ~50-80
minute full-verification cost before checking with the user first.

## Part 30: nv2route -- historyWire, negotiated real per-layer wire congestion (species `nv`, generation 2)

Part 29's own knob sweep closed out nv1route as a dead end on its own
terms (diminishing returns, +75% vs fr5route at every setting tried).
Rather than continue tuning nv1route's existing knobs, a read-only
diagnostic (not part of the repo) measured *where* nv1route's own
remaining real overflow on newblue1 (46908) actually sat, comparing it
against what the 2D pricing stage's own summed-across-layers metric saw
at the same grid positions: real per-layer overflow 46908 (matching
`gr_verify` exactly) vs. 2D-combined overflow 18156 (matching nv1route's
own reported "2D overflow" exactly). **Of the 46908, 27002 (57.6%) sat
at positions where the aggregate capacity across all 6 real layers was
actually sufficient** -- a pure per-layer distribution problem,
structurally invisible to the 2D stage's own metric, and one nv1route's
own negotiation loop couldn't reach either: it gives the DP memory of
via congestion (`historyVia`) but none for wire congestion. The
remaining 19906 (42.4%) was genuine aggregate shortage, matching the 2D
stage's own 18156 closely -- the only slice a 2D-pricing-loop retune
(the other candidate direction, considered and explicitly deferred)
could ever reach.

Built as `gr/nv2route.cpp`, forked from `gr/nv1route.cpp`: `historyWire`
(`WirePrices`/`updateWireHistory`) is a direct, verbatim-mechanism port
of `historyVia` to real per-layer wire edges instead of via sites --
same LIFO-relaxable, streak-gated formula, no capacity clamp on the
overflow trigger (the same lesson nv1route's own metric fix
established). `wireCostForEdge` multiplies its existing usage/capacity
ratio by the *maximum* history value along a leg's own span (backward
compatible: nullptr, every pre-existing call site, reproduces nv1route's
formula exactly). Candidate selection and the accept/revert judgment
needed no changes at all -- they already select and judge nets by real
wire overflow, exactly the resource historyWire now gives the DP memory
of. 57 test functions, 272 checks, 0 failures, including a hand-derived
decisive test (a single straight edge, no via at all) proving historyWire
fixes real overflow a single pass cannot, and a real end-to-end fixture
(5 real nets through the actual `route()` -> `negotiateViaAssignment()`
pipeline) proving the mechanism isn't a dead flag.

**Real result: the first unanimous win over fr5route in this project's
entire via-cost-aware exploration**, across five prior generations that
all failed (fr6route, fr7route, and nv1route's own three knob variants).
Screened on newblue1 alone first, default `NV2ROUTE_VIA_ROUNDS=60`:
22118, already -17.4% vs fr5route's own committed 26776. A round-cap
sweep followed to check how much more was available:

| round cap | newblue1 overflow | further gain |
|---|---|---|
| 60 | 22118 | -- |
| 200 | 21904 | -1.1% |
| 250 | 21874 | -0.1% (from 200) |
| 400 | 21824 | -0.4% (from 200) |

Real, but sharply diminishing past round cap 200 -- the same shape as
the coefficient-tuning sweeps this generation exists to move past, just
at a much better baseline. Full three-benchmark runs at round caps 200
and 400 (`gr_verify`-confirmed, `eval2008.pl`-cross-checked exactly on
every benchmark: adaptec1 and bigblue1 both matched to the OF/max-OF
digit, newblue1 likewise -- 0 malformed/out-of-bounds/disconnected/
incomplete/unrouted on all three at both round caps):

| benchmark | fr5route (real) | nv2route r=200 | nv2route r=400 | final delta vs fr5route |
|---|---|---|---|---|
| adaptec1 | 93764 | 75192 | 75028 | **-19.98%** |
| newblue1 | 26776 | 21904 | 21824 | **-18.49%** |
| bigblue1 | 191976 | 185258 | 184922 | **-3.67%** |

Doubling the round cap from 200 to 400 bought only -0.22%/-0.37%/-0.18%
further on adaptec1/newblue1/bigblue1 respectively -- all three still
hit the round cap without fully converging (none stalled even at 400),
but the marginal return per additional round has clearly collapsed by
that point. bigblue1's own margin is real but far thinner than the
other two's, for reasons not yet investigated.

**Decision: `gr/nv2route.cpp` is this project's new best real result,
replacing `gr/fr5route.cpp`.** Unanimous across all three real ISPD
benchmarks, non-trivial on two of three, verified both ways (`gr_verify`
and `eval2008.pl` agreeing exactly) -- the first result in the entire
via-cost-aware line of attack (fr6route through nv2route, six
generations) to actually close part of the gap this project's own
zero-overflow mandate exists to chase, rather than widen it.
`NV2ROUTE_VIA_ROUNDS=200` (not the file's own implicit default of 60) is
the recommended operating point: captures nearly all of the measured
gain at roughly half the runtime of 400. Total overflow remains far from
zero on all three benchmarks -- this is a real step, not the
destination. Open questions for a future session: why bigblue1's own
margin is so much thinner than the other two's, whether the deferred
2D-pricing-loop retune could still close more of the remaining
42.4%-scale aggregate-shortage gap on top of this, and whether the two
history mechanisms (via, wire) interact in ways worth tuning jointly
rather than in isolation.

## Part 31: mt1route and mt2route -- escalation-tier sophistication (species `mt`), both negative

A follow-up diagnostic re-ran nv2route's own real output through the
distribution-vs-aggregate-shortage split from Part 30 (`diag4`, a
throwaway tool, not part of the repo) to see how the remaining overflow
looked after historyWire had done its work. Result: 84-98% of what's
left on all three benchmarks is now aggregate-shortage overflow,
tracking the 2D pricing stage's own unresolved "2D overflow" almost
exactly (65644 vs 63966 on adaptec1, 181748 vs 180060 on bigblue1),
meaning historyWire's own target (the distribution slice) is now mostly
exhausted and the 2D pricing loop itself is the dominant remaining
lever. That loop had been deferred twice before -- Part 29 named it
directly as one of two open gaps, the other being "topology quality
(RSMT vs. plain MST)" -- a label carried forward unexamined from Part 21
(`fr1route`), where it was accurate. It stopped being accurate at Part 24
(`fr4route`), which added an exact Dreyfus-Wagner RSMT for the fast
tier's own fixed topology (nets up to 8 pins by default, heuristic
Steinerization above that) -- adopted forward unchanged through fr5-7,
nv1, and nv2. Checked directly against nv2route.cpp's own code before
writing this part: the fast tier already builds RSMT, not plain MST, and
the escalation tier (`routeStepMSMV`) abandons any fixed topology
entirely in favor of MSMV maze growth. "Topology quality" as originally
scoped is therefore not a live gap and is retired as a deferred item;
see the open item at the end of this part for what's actually still
open. This part covers escalation-tier sophistication only.

Every router through nv2route splits escalation into exactly two tiers:
a fixed L/Z pattern menu, and a full margin-6-and-doubling maze search
(`routeStepMSMV`). Real routers in this space (FastRoute) use a graduated
three-stage architecture -- pattern, then monotonic maze, then full maze
-- and this project's own history had never tried the middle stage.

**mt1route** (`gr/mt1route.cpp`, forked from nv2route.cpp) adds it: a new
medium tier (`routeStepMSMVMonotonic`) runs one `multiSourceDijkstra`
search at margin exactly 0 -- no expansion, no retry -- cheaper than the
full tier's cheapest attempt, more powerful than the fixed pattern menu.
`RoutedNet::escalated` (bool) became `RoutedNet::tier`
(`kTierFast`/`kTierMedium`/`kTierFull`, one-way). `route()` gained a new
phase mirroring the existing full-tier loop's own structure (candidate
selection, supervised accept/revert via `overflowOverUnion`), promoting
to `kTierFull` on failure instead of giving up. 63 test functions, 302
checks, 0 failures.

**Real result: a clean, unanimous regression**, despite the core
mechanical hypothesis being confirmed true -- the medium tier really did
shrink the full tier's own candidate pool:

| benchmark | nv2route full-tier pool | mt1route full-tier pool | reduction |
|---|---|---|---|
| adaptec1 | 75993 | 50172 | -34.0% |
| newblue1 | 53194 | 28507 | -46.4% |
| bigblue1 | 97873 | 84000 | -14.2% |

Fewer nets needing the expensive tier did not translate into a better
result. Real overflow, both `gr_verify`- and `eval2008.pl`-confirmed
legal and matching exactly (0 malformed/out-of-bounds/disconnected/
incomplete/unrouted on all three):

| benchmark | nv2route r=200 (best) | mt1route | delta |
|---|---|---|---|
| adaptec1 | 75192 | 81326 | **+8.16%** |
| newblue1 | 21904 | 22292 | **+1.77%** |
| bigblue1 | 185258 | 200608 | **+8.28%** |

Runtime also grew (adaptec1 3131s -> 3954s, bigblue1 3083s -> 3987s;
newblue1 615s -> 819s).

**Diagnosis:** escalation is one-way and permanent. The medium tier's
own supervised accept/revert only requires a candidate reroute to be not
worse than before (`localAfter > localBefore` -> revert, otherwise
accept) -- a bar a plain *tie* clears as easily as a genuine
improvement. Once accepted, `rn.tier` locks at `kTierMedium` forever,
permanently denying that net a shot at the full tier's own wider search
even where it would have found something genuinely better, not just
locally tied. A tied medium-tier "resolution" is often just whichever
arbitrary equal-cost path Dijkstra happened to settle on inside a
smaller search box.

**mt2route** (`gr/mt2route.cpp`, forked from mt1route.cpp, generation 2
of the same species) made medium-tier resolutions *provisional* rather
than permanent, scoped narrowly to the diagnosis: a strict local
improvement (`localAfter < localBefore`) stays durably at the medium
tier; a tie (`localAfter == localBefore`) gets promoted to the full tier
for one more chance, regardless of current overflow status --
deliberately not "retry every medium-tier net," which the plan reasoned
would degenerate toward nv2route's own original full-population
behavior on benchmarks that never converge to zero, losing the medium
tier's efficiency gain for nothing. `shouldPromoteMediumToFull` extracted
as a small pure function for direct, exhaustive testing. New env var
`MT2ROUTE_PROVISIONAL_TIES=0|1` (default 1; `0` reproduces mt1route's
exact output byte for byte, the standard toggle-reproduces-the-parent
regression-continuity check). 65 test functions, 318 checks, 0 failures.

**Real result: the fix barely moved the needle, and did not recover the
regression.** Verified legal on all three (`gr_verify`, 0 malformed/
out-of-bounds/disconnected/incomplete/unrouted; not cross-checked
against `eval2008.pl` -- see below):

| benchmark | nv2route (best) | mt1route | mt2route | mt2route vs mt1route | mt2route vs nv2route |
|---|---|---|---|---|---|
| adaptec1 | 75192 | 81326 | 81130 | -0.24% | +7.89% |
| newblue1 | 21904 | 22292 | 22238 | -0.24% | +1.53% |
| bigblue1 | 185258 | 200608 | 200370 | -0.12% | +8.15% |

Meanwhile the full-tier candidate pool grew back substantially -- most
of mt1route's own -34%/-46%/-14% reduction was given back for almost no
quality gain:

| benchmark | mt1route full-tier | mt2route full-tier | regrowth |
|---|---|---|---|
| adaptec1 | 50172 | 62831 | +25.2% |
| newblue1 | 28507 | 38081 | +33.6% |
| bigblue1 | 84000 | 88947 | +5.9% |

Runtime on adaptec1 and bigblue1 nearly doubled (3954s -> 7461s,
3987s -> 7465s); newblue1 held flat (819s -> 819s, a smaller pool
regrowth there in absolute terms).

Per this project's own screening discipline, the full `eval2008.pl`
cross-check was skipped: `gr_verify` alone was sufficient to show this
is another negative result, and paying for the independent cross-check
only makes sense once a result looks like a real candidate.

**Decision: the escalation-tier-sophistication line of attack (species
`mt`, two generations) is closed as a dead end**, and the committed best
real router remains `gr/nv2route.cpp` (Part 30's own numbers), unchanged
by either `mt1route` or `mt2route`. The result directly falsifies
mt2route's own hypothesis: ties were not the dominant source of
mt1route's regression. Whatever actually causes it is present even in
the resolutions that stayed durably at the medium tier, untouched by the
provisional-ties fix -- something about the medium tier's own
margin-0, no-retry search itself (not merely which of its results get
locked in) is producing worse global outcomes than either the cheap
pattern menu or the full margin-6-and-doubling search, despite
mechanically shrinking the full tier's own candidate pool both times.
Not further diagnosed in this pass; a future session revisiting
escalation architecture should look at *why* the medium tier's
accepted-and-kept resolutions themselves are harmful, not just how they
get locked in.

Per Part 29/30's own open item, the other previously-deferred gap was
recorded as "topology quality (RSMT vs. plain MST)" -- but as established
above, that gap was already closed at Part 24 and is not actually open.
What remains genuinely unexplored: the `maxExactPins=8` degree cap on
the exact RSMT DP (larger nets still get only heuristic Steinerization,
never the exact tree), whether the RSMT-seeded fast tier's own topology
biases which nets even reach MSMV in a way worth revisiting, and the 2D
pricing loop's own convergence gap named at the top of this part
(84-98% of remaining overflow is now aggregate-shortage, tracking that
loop's own unresolved state). None of these three has been scoped yet;
that is the open work for the next generation.

## Part 32: `FR4ROUTE_EXACT_MAX_PINS` sweep on nv2route -- degree cap for the exact RSMT DP (pure tuning, no new file)

Part 31's own open item picked the `maxExactPins=8` degree cap as the next
thing to investigate: `nv2route`'s fast tier only runs the exact
Dreyfus-Wagner RSMT DP (`exactSteinerTree()`, `gr/nv2route.cpp:699-840`)
for nets with at most 8 unique pins; larger nets fall back to
`steinerizeTree()`'s cheap heuristic, which structurally cannot improve
any degree>=3 branch point. The cap was picked with no empirical sweep
("the DP gets expensive past here," never measured), and no data existed
on how much this actually costs in real overflow. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**Stage 1 diagnostic** (`diag5`, throwaway, not part of the repo, matching
the `diag4` precedent from Part 30/31): built a fair-share overflow
attribution tool, adapting `gr_verify.cpp`'s own per-net edge-list and
usage-accumulation logic rather than reimplementing it. For every
overflowed edge, divided its overflow across the nets crossing it in
proportion to each net's own charged width, summed per net, and bucketed
by dedup'd pin count (self-check: the fair-share sum reconciled with
`gr_verify`'s own `totalOverflow` exactly, to the last unit, on all three
benchmarks). Result, on the three already-committed `nv2route` outputs
(`NV2ROUTE_VIA_ROUNDS=200`, `gr_verify`-reconfirmed clean before use):

| benchmark | nets >8 pins (share of routed population) | overflow attributed to them |
|---|---|---|
| adaptec1 | 16705 (9.45%) | **31.48%** |
| bigblue1 | 16193 (8.23%) | **39.22%** |
| newblue1 | 14986 (5.54%) | **39.57%** |

Unanimous, well past the 10% decision gate fixed before looking at the
number (see Part 31's plan file for the gate's own reasoning) -- a small
fraction of nets by count, carrying a disproportionate share of what's
still overflowed. This justified the sweep below, though as an upper
ceiling on possible gain, not an expected one (per the gate's own caveat:
Part 24 already measured the whole lever's full-population ceiling at only
1.5-4.2%, and raising the cap only reaches a strict subset of that
population while only affecting initial topology, not negotiation).

**Stage 1.5 timing microbenchmark**: `exactSteinerTree()` is a pure
function of a point set, so its real cost was measured directly (not
reasoned about) over real point sets pulled from each benchmark's own net
population, k=8..20, with a per-k wall-clock budget so an exponential blow-up
couldn't hang the tool. Nearly identical across all three benchmarks (the
DP's cost is a function of k alone, not benchmark-specific geometry):

| k | mean time/net |
|---|---|
| 8 | 1.5ms |
| 9 | 4.9ms |
| 10 | 15.3ms |
| 11 | 50.0ms |
| 12 | 150ms |
| 13 | 480ms |
| 14 | 1.5s |

A clean ~3.1-3.3x growth per unit k, matching the DP's own O(3^k) merge-step
term. k=15+ was not pursued -- already impractical by extrapolation (tens of
seconds to hours per net). Combined with adaptec1's own granular per-k
population, estimated one-time added setup cost: cap=10 ~36s, cap=12
~6min, cap=13 ~17.5min, cap=14 ~49min. Candidates 10, 12 (and 13, held in
reserve) were judged practical.

**Stage 3 cheap screen** (adaptec1 only, reduced rounds:
`VROUTE_FAST_ROUNDS=15 VROUTE_SLOW_ROUNDS=25 NV2ROUTE_VIA_ROUNDS=20`,
`gr_verify`-checked, all three confirmed legal: 0 malformed/out-of-bounds/
disconnected/incomplete/unrouted, self-reported overflow matching
`gr_verify` exactly):

| cap | real overflow | vs. cap=8 baseline | wall clock |
|---|---|---|---|
| 8 (baseline) | 93538 | -- | 726.5s |
| 10 | 94122 | **+0.62%** | 772.2s |
| 12 | 98300 | **+5.09%** | 1112.2s |

**Clean, monotonic regression.** Raising the cap makes real overflow
worse, and worse the higher it goes, while also costing more runtime.
Neither candidate beat baseline, so per this sweep's own promotion gate,
neither was promoted to a full three-benchmark run, and cap=13 (already
judged practical by Stage 1.5) was not run either -- the trend was already
unambiguous and a third point on an already-monotonic curve was not worth
the ~17.5 minutes.

**Diagnosis:** `exactSteinerTree()` is a pure geometric length-minimizer
with zero congestion awareness -- exactly like `steinerizeTree()`'s own
fallback. A shorter, more "optimal" tree is not necessarily a
less-congested one: it can concentrate a net's wire onto fewer, more
central Hanan-grid edges instead of the fallback heuristic's comparatively
spread-out path. This is consistent with Part 24's own original finding
that moving the *entire* eligible population to exact RSMT bought only a
modest 1.5-4.2% gain in the first place -- extending it further into
larger, more interconnected nets appears to cross into net-negative
territory. Not further diagnosed (e.g., no instrumented comparison of
which specific edges each topology choice prefers) -- a future revisit
would need that to say more precisely why.

**Decision: the exact-RSMT degree cap is not a lever worth raising, in
either direction.** `maxExactPins=8` (the existing default) stays as is;
`nv2route` is unchanged. This closes out the "Exact RSMT degree cap"
direction Part 31 opened. Of the three items Part 31 left open, two remain:
the RSMT-seeded fast tier's own topology bias into MSMV eligibility, and
the 2D pricing loop's own convergence gap (the larger, already-quantified
target: 84-98% of remaining overflow tracks that loop's own unresolved
state).

## Part 33: `VROUTE_EPS`/`VROUTE_THRESHOLD`/`VROUTE_POWER` sweep on nv2route's 2D pricing loop -- mixed, net negligible

Part 32 closed the RSMT-cap direction as negative. The user picked the
other, stronger-looking open item: the 2D pricing loop's own convergence
gap, since 84-98% of nv2route's remaining real overflow tracks that
loop's own unresolved "2D overflow" number almost exactly (Part 30/31).
Scoped in `/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**Diagnostic first** (`VROUTE_TRACE=1 FR1ROUTE_TWOD_ONLY=1`, zero new
code -- both env vars already existed): read the loop's own round-by-round
trace on adaptec1 before touching anything. Found the fast tier does NOT
converge -- it overshoots after round 1 (248780, its best-ever value),
climbs to 285736 by round 14, then oscillates in a 274000-283000 band,
patience-stopped at round 21 (`kFastPatience=20`), never beating round
1's own number. The cleanup pass reverts toward the best-seen state and
improves further (248780 -> 232190). The slow tier, by contrast, declines
smoothly with no oscillation, still improving at the round cap
(172072 -> 63966 over 150 rounds, "hit the round cap", never stalled),
with a flat ~72% reject rate that never shrinks across all 150 rounds.
`accepted` never hits exactly 0, closing the planned Stage 3
premature-stop probe as a non-issue before it was even run. The final 2D
overflow (63966) matched Part 30/31's own cited number exactly, confirming
the isolated 2D-only run faithfully reproduces the real pipeline.

The fast tier's overshoot and the slow tier's still-declining plateau
pointed in opposite directions for the shared coefficients (fast tier
wants gentler steps, slow tier's plateau suggested it could converge
faster with more aggressive ones) -- resolved by screening the full grid
in both directions rather than guessing.

**Screen** (adaptec1, `VROUTE_FAST_ROUNDS=15 VROUTE_SLOW_ROUNDS=25
NV2ROUTE_VIA_ROUNDS=20`, `gr_verify`-confirmed legal throughout, baseline
93538): a one-factor-at-a-time sweep around the defaults (eps=0.02,
threshold=0.2, power=2.5) plus follow-ups once a trend appeared:

| axis | value | overflow | delta |
|---|---|---|---|
| eps | 0.005 | 106416 | +13.8% |
| eps | 0.01 | 98172 | +5.0% |
| eps | 0.04 | 94086 | +0.6% |
| eps | 0.08 | 101974 | +9.0% |
| threshold | 0.4 | 96312 | +3.0% |
| threshold | 0.3 | 94482 | +1.0% |
| threshold | 0.1 | 93158 | -0.4% |
| threshold | 0.05 | 89738 | -4.1% |
| threshold | 0.0 | 89422 | -4.4% |
| power | 3.5 | 97974 | +4.7% |
| power | 5.0 | 90036 | -3.7% |
| power | 1.5 | 88086 | -5.8% |
| power | 1.0 | 85884 | -8.2% |
| power | 0.5 | 87066 | -6.9% |
| threshold=0.05 + power=1.0 | | **84610** | **-9.6%** |

Lower threshold and lower power both helped independently (power
non-monotonically -- 3.5 lost, 5.0 and below all won, with a sweet spot
around 1.0-1.5, not a clean single-direction trend), and combining the
two best single-axis values won further. `threshold=0.05, power=1.0` was
the best of 15 screened combinations.

**Round-budget sanity check, per the plan's own explicit risk flag**
(these knobs change convergence rate/shape directly, so a heavily
truncated round budget could reward a candidate that converges fast early
but plateaus worse at the real cap -- exactly the risk this check exists
to catch): re-ran the winning combination on adaptec1 at the REAL 2D
round budget (default 60/150, not the screen's 15/25), against a
same-conditions default-coefficient baseline run fresh for this purpose.
Result: 76264 vs 77444 -- **-1.5%**, both `gr_verify`-confirmed legal. The
screen's apparent -9.6% collapsed to a much more modest edge once given
the real round budget the committed operating point actually uses (2D
overflow itself: 63548 vs 63966, only -0.7%) -- confirming most of the
screen's advantage really was a truncated-round-budget artifact, exactly
as the plan anticipated, though a small real edge survived the check.

**Full three-benchmark run** at the committed operating point
(`NV2ROUTE_VIA_ROUNDS=200`, default 2D rounds,
`VROUTE_THRESHOLD=0.05 VROUTE_POWER=1.0`), `gr_verify`-confirmed legal on
all three (0 malformed/out-of-bounds/disconnected/incomplete/unrouted):

| benchmark | nv2route committed best | threshold=0.05/power=1.0 | delta |
|---|---|---|---|
| adaptec1 | 75192 | 74148 | -1.4% |
| newblue1 | 21904 | 21868 | -0.2% |
| bigblue1 | 185258 | 186340 | +0.6% |
| **sum** | **282354** | **282356** | **~0%** |

**Not unanimous -- a mixed result**, small wins on two benchmarks offset
by a small loss on the third, net essentially zero in aggregate (a
difference of 2 out of 282354, noise-level). Every result this project
has adopted as a new committed best won unanimously across all three
benchmarks (kroute, then nv2route); this doesn't clear that bar. Per this
project's own screening discipline, `eval2008.pl` was not run -- there's
no independent cross-check worth paying for on a result that isn't a real
win by `gr_verify` alone.

**Decision: `nv2route` is unchanged; the committed best stays Part 30's
own numbers.** The coefficient-tuning angle on the 2D pricing loop is
closed as mixed/negligible, not a regression and not a win. Of the three
items open after Part 31/32, one remains genuinely untried: the fast
tier's own persistent overshoot (never resolved by this sweep -- lowering
threshold/power helped the slow tier's plateau, not the fast tier's own
non-convergence, though the cleanup pass largely absorbs that damage
already) and the net-ordering/scheduler sensitivity question this
diagnostic's own flat ~72% reject rate motivates -- CLAUDE.md's own
scheduler section warns about exactly this shape (a fixed per-round net
order, every net reacting to a price state already perturbed by every
earlier-ordered net in that same round). Scoped as Stage 4 in the plan
file for the next session.

## Part 34: net-ordering/scheduler sensitivity on nv2route's 2D pricing loop -- real but negligible

Part 33 left net ordering as the least-explored remaining lever on the 2D
pricing loop, motivated by its own diagnostic (slow-tier reject rate flat
at ~72% for all 150 rounds, never shrinking) and CLAUDE.md's own
"Module structure" warning about a fixed per-round net order letting
early-processed nets systematically bias the price state later-processed
nets react to. `negotiateWire` mutates `prices` immediately per committed
leg, not once per round, so this mechanism is real and direct, not
speculative. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**Prior art found while scoping this**: net ordering was already tested
once, in isolation, with a real positive result -- `gr/xroute.cpp` (Part
8) found RUDY+centrality ordering alone won -1.05% on newblue1, cleanly
separated from an unrelated barrier-curve regression. That result predates
historyWire, MSMV, and RSMT-exact-Steiner (kroute-era pricing), so it
doesn't automatically transfer to nv2route's substantially different
dynamics -- worth re-testing specifically, not assuming.

**Part 1 -- position-bias diagnostic** (`diag6`, throwaway, not part of
the repo, adapting Part 32's own fair-share overflow-attribution machinery
but bucketed by each net's index in `route()`'s own processing order
instead of pin count): run on all three benchmarks' already-committed
real output (`gr_verify`-reconfirmed clean first). Self-check reconciled
exactly (fair-share sum = trusted `totalOverflow`, diff 0.000000) on all
three. Result: real, non-uniform skew on every benchmark, but
**inconsistent in shape**:

| benchmark | pattern | late-decile (6-9) share |
|---|---|---|
| adaptec1 | skews toward late-processed nets | 67.3% |
| bigblue1 | scattered, no clean trend | 36.1% |
| newblue1 | spikes in the middle (deciles 4-5) | 18.5% (middle carries 73%) |

If a genuine per-round processing-order bias were the cause, the same
relative shape (late deciles worst) should appear on all three, since the
round-loop mechanism is identical. Instead each benchmark spikes in a
different, benchmark-specific position -- more consistent with each
design's own physical congestion happening to correlate with wherever its
`.gr` file lists those nets, than with a generic order-causation effect.
Read as a soft signal, not a verdict, per the plan's own framing (this
measures settled post-negotiation attribution, not the process-level
reject-rate signal that motivated the stage) -- Part 2 ran regardless.

**Part 2 -- reversal sensitivity probe.** A throwaway scratch copy of
`nv2route.cpp` ported `xroute.cpp`'s own `routeOrder` indirection pattern
(`nets` stays in file order; only which net each round loop visits
first/last changes, via a `VROUTE_NET_ORDER=default|reverse` gate) --
safer than physically reordering `nets`, since every downstream reader of
`nets`' index position keeps working unchanged. Self-test suite passed
clean on the scratch copy. Screened on adaptec1 (reduced rounds,
`gr_verify`-confirmed legal both ways):

| order | overflow | vs. default |
|---|---|---|
| default (sanity check) | 93538 | -- (exact match to the existing baseline) |
| full reversal | 92900 | **-0.68%** |

The default-order run reproduced the existing baseline to the last unit,
confirming the scratch copy is trustworthy. Full reversal -- the most
extreme order perturbation possible -- moved the result by well under 1%,
far short of the ±5% sensitivity gate fixed before running it. If even
maximal reversal barely matters, milder structure-informed reorderings
(by span, by RUDY/centrality) are very unlikely to do better, since
they're gentler perturbations of the same mechanism.

**Decision: net-ordering sensitivity on nv2route's own 2D pricing loop is
real but negligible.** `nv2route` is unchanged; no reordering mechanism is
worth building. This also resolves Part 1's own ambiguity: the diag6 skew
reflects each benchmark's own incidental netlist-position/congestion
correlation, not order-causation -- consistent with reversal barely moving
the number. Closes out Stage 4. Two items remain genuinely open from this
investigation's own bookkeeping: the RSMT-seeded fast tier's own topology
bias into MSMV eligibility (untouched since Part 31), and the fast tier's
own persistent non-convergence/overshoot (diagnosed in Part 33 -- climbs
from round 1's 248780 to 285736 by round 14, never recovers, rescued only
by the cleanup pass -- never addressed).

## Part 35: fast-tier accept/revert supervision -- mechanism confirmed correct, result decisively negative

Part 34 left the fast tier's own persistent overshoot as the
better-characterized of the two remaining open items. Direct code reading
confirmed the root cause precisely: the fast tier's round loop
(`route()`, `nv2route.cpp:1866-1899`) has no accept/revert supervision on
its per-net reroute (`routeNetFast`) -- every still-overflowed net
reroutes to whatever's cheapest under current prices, unconditionally
committed, with zero check on global overflow impact. This is a direct
violation of CLAUDE.md's own explicit design rule ("Escalation... must be
supervised per net... revert on the spot if it made things worse"). The
slow tier and `relaxedCleanup` both already implement exactly this
supervision, proven and working -- the fix looked like a structurally
trivial copy-adapt of existing, battle-tested code. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**Stage 1 -- scratch probe and a real, self-correcting decisive test.**
A throwaway scratch copy (`VROUTE_FAST_SUPERVISED=0|1`, default
reproducing nv2route exactly) ported the slow tier's own snapshot/
compare/revert block around `routeNetFast`. The first hand-built fixture
(three small nets contesting a capacity-1 edge) immediately caught a real
bug before any real-benchmark run: supervising a net's very first-ever
route (always round 0, where `oldCharges` is empty) reverts it to
*nothing* on rejection -- worse than any overflow, since an unrouted net
violates connectivity outright. Fixed by skipping supervision when there
is no prior route to fall back to (mirroring the unsupervised path's own
implicit behavior for a first assignment). After the fix, the same
fixture no longer showed a decisive difference -- itself informative:
`overflowOverUnion` is computed over a net's own old-union-new edge set,
so it already correctly captures self-inflicted harm (vacating one
overloaded edge for another only "looks better" if the new edge is
genuinely less overflowed); what it structurally cannot see is a
same-round ordering interaction between different nets, which Part 34
already measured as real but tiny (-0.68% under full reversal). Given
that, the real test moved to Stage 2 -- an honest methodological note
worth recording, not a shortcut.

**Stage 2 -- real trace validation, adaptec1** (`VROUTE_TRACE=1
FR1ROUTE_TWOD_ONLY=1`, full real round budget). Result: supervision
**does exactly what it was designed to do** -- the fast tier's own
oscillation disappears completely, replaced by a smooth, monotonic
decline every single round (no exceptions, confirmed across all 60
rounds). But it converges to a **worse** place: round 1 alone resolves
far less (288650, vs. the unsupervised original's 248780) because
supervision rejects much of round 1's aggressive churn (48889 accepted,
16540 rejected) that the unsupervised version accepts unconditionally.
By the fast tier's own round cap and cleanup pass, supervised reaches
273164 -- **+17.7% worse** than the unsupervised original's own
248780-then-232190. The slow tier's own much larger round budget (150
rounds, vs. the fast tier's 60) narrows this gap only slightly and very
slowly (273164 -> 83648 over 150 rounds, still declining but by single
digits per round near the end) -- final 2D overflow **83648**, vs. the
unsupervised original's own **63966** -- **+30.8% worse**, confirmed
`FR1ROUTE_TWOD_ONLY`-isolated, both runs otherwise identical.

**Diagnosis**: the unsupervised fast tier's own round-1 unrestrained
churn -- accepting essentially every reroute regardless of individual
local effect -- achieves a large aggregate win specifically *because* it
doesn't stop to check each move; supervision's stricter round-1
rejection rate locks in a materially worse starting configuration too
early, and the extra 150 rounds of slow-tier work downstream cannot make
up the gap. This is the same shape as Part 31's mt1route finding: the
mechanism is mechanically exactly what was diagnosed and intended, and it
demonstrably fixes the specific symptom it targeted (the oscillation is
gone, confirmed), but the end-to-end result is decisively worse, not
better. Per the plan's own Stage 2 gate ("if the fix doesn't recover to
at least match the unsupervised baseline, this should be reported as a
real negative finding and not progressed to Stage 3"), this closes here
-- no screen, no promotion, no `eval2008.pl` cross-check attempted, since
the trace-level evidence alone is already unambiguous and would only cost
more runtime to reconfirm.

**Decision: fast-tier accept/revert supervision is a real, decisive
negative.** `nv2route` is unchanged. The apparent design-rule violation
CLAUDE.md warns against turns out, empirically, to be load-bearing here:
the fast tier's own lack of supervision is not an oversight this specific
loop suffers from, it's part of what makes its round-1 churn effective.
One item remains genuinely open from this investigation's own
bookkeeping: the RSMT-seeded fast tier's own topology bias into MSMV
eligibility, untouched since Part 31.

## Part 36: nv3route -- giant-net negotiation, the first real win since nv2route itself

A new per-net ranking diagnostic (`diag7`, throwaway, not part of the
repo, complementing `diag5`'s pin-count buckets and `diag6`'s
processing-order deciles with an actual per-net leaderboard) found, while
investigating a user question about which individual nets contribute most
to overflow, that in every one of the three real benchmarks the single
largest "giant net" (`n.pins.size() > kGiantNetPins`=1000 raw pins -- the
ISPD contest's own official "need not be routed" exemption threshold,
matching `gr_verify.cpp`'s identical exemption check, not a tuned
performance constant) is the #1 or #2 worst overflow offender by both
total attributed overflow and distinct-overflowed-edges-touched, out of a
population of 176,716 to 270,715 routed nets:

| benchmark | giant nets (raw pins) | rank by overflow breadth |
|---|---|---|
| adaptec1 | n7807 (2271) | #1 -- 344 distinct overflowed edges |
| bigblue1 | n10760 (2621), n10756 (1342) | #1 and #2 -- 530/531 edges |
| newblue1 | n71 (12335), n1211 (4682) | #1 and #2 -- 368/93 edges |

Only 1-2 such nets exist per benchmark (confirmed via direct grep of the
`.gr` files) -- a small, concentrated population, meaning per-net cost
tolerance for a fix is far higher than anywhere else in this codebase.
`diag7` reads the final `.route` output, so this is real, scored
overflow (self-check reconciled exactly with `gr_verify`'s own
`totalOverflow`), not `route()`'s own internal 2D metric. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`, run in
parallel with Part 35's own investigation per explicit user direction.

**Root cause, confirmed by direct code reading:** giant nets are routed
via `routeGiantNet`, which gets `steinerizeTree`'s topology (the same as
any net too large for exact RSMT) but commits each MST edge as a fixed
horizontal-then-vertical L-shape with zero price awareness
(`routeGiantNet`'s own signature takes `Usage&`, never `Prices&`) and
zero congestion awareness (no search, unconditional commit) -- and is
entirely excluded from `route()`'s fast tier, slow tier, and cleanup
pass. A giant net's initial, congestion-blind route is never revisited at
the 2D level, while every other net gets up to 60+150 rounds of
price-based negotiation to escape congestion. It does still charge usage
for whatever it emits, so its congestion is fully counted in the final
score -- it just never gets a chance to negotiate it away. An initial
hypothesis (giant nets stuck on fixed *real* layers) was checked and
found wrong: `route()`'s own `LayerPlan` is synthetic (`hLayers{0},
vLayers{0}`) for every net during the 2D-collapsed phase, giant or not;
real per-layer assignment already applies to giant-net segments
downstream with no special-casing. The actual gap is narrower and more
specific: a giant net's XY topology is locked in once, congestion-blind,
and nothing downstream can reroute the path itself, only choose which
layer a fixed path lives on.

**The new mechanism**: let giant nets participate in the fast tier's own
per-leg price-negotiated reroute (`routeNetFast`), unchanged from how
every other net already uses it -- no new function, no signature change,
gated by `NV3ROUTE_GIANT_FAST_TIER=0|1` (default 1, `=0` reproduces
nv2route's exact prior behavior). `routeGiantNet`'s own initial commit
still runs first, and giant nets still never reach the slow tier's MSMV
escalation or the cleanup pass -- this generation only widens fast-tier
eligibility, deliberately the narrowest possible test of "does
negotiation help at all," before considering CLAUDE.md's own bigger
prescribed fix (subnet decomposition) or slow-tier eligibility. Cost,
measured directly rather than assumed: `routeNetFast` on the real giant
net (adaptec1's n7807, 1926 unique points) took ~1.07ms per call --
negligible even across 60 rounds.

**Screen and mandatory sanity check.** A reduced-round screen on
adaptec1 showed -0.63% (93538 -> 92952, `gr_verify`-confirmed legal).
Per this investigation lineage's own hard-won discipline (Part 33 caught
a much larger screen-level effect collapse entirely at the real round
budget), a sanity check at the real 2D round budget was run before
trusting the screen number -- it held: 76384 vs. a same-conditions
default baseline of 77444, -1.37%, `gr_verify`-confirmed legal. This is
the first Stage in this whole lineage (Parts 32-35 all screened
positive-or-negative signals that either collapsed or were correctly
predicted to be modest) where a screen-level win survived the sanity
check un-diminished in direction and comparable in magnitude.

**Full three-benchmark result** at the committed operating point
(`NV2ROUTE_VIA_ROUNDS=200`, default 2D rounds), `gr_verify`-confirmed
legal on all three (0 malformed/out-of-bounds/disconnected/incomplete/
unrouted) and cross-checked exactly against `eval2008.pl` (total
overflow, max overflow, and scored wirelength matching to the unit on
every benchmark):

| benchmark | nv2route (committed best) | nv3route | delta |
|---|---|---|---|
| adaptec1 | 75192 | 74210 | **-1.31%** |
| newblue1 | 21904 | 21634 | **-1.23%** |
| bigblue1 | 185258 | 185246 | **-0.006%** (flat, not worse) |
| **sum** | **282354** | **281090** | **-0.448%** |

**Decision: `gr/nv3route.cpp` is this project's new best real result,
replacing `gr/nv2route.cpp`.** No benchmark regressed; two improved
meaningfully and the third held essentially flat -- clears this
project's own established promotion bar. Modest in absolute magnitude,
but the first real progress after four straight negative/mixed
generations (Parts 32-35), and it directly validates the diagnosis: a
small, disproportionately-congested population was being denied
negotiation entirely, not merely given a suboptimal topology. Self-tests:
a decisive hand-built fixture (a >1000-raw-pin net with only two unique
points after dedup, so the routed topology stays trivial while the giant
classification is genuinely tripped) proves `routeGiantNet`'s own fixed
corner can permanently contest an edge that the untested alternate corner
would avoid entirely -- `NV3ROUTE_GIANT_FAST_TIER=0` reproduces that
permanent overflow exactly (matching nv2route's own behavior); the
default (giant nets in the fast tier) resolves it completely by
discovering the untested corner under real price pressure. 57 test
functions, 275 checks, all passing.

Two directions are already scoped as the immediate next steps, in order:
(1) slow-tier (MSMV) eligibility for giant nets -- this generation still
withholds it even though the population's tiny size (1-2 nets) makes an
expensive per-net search entirely affordable; (2) a congestion-aware
corner choice inside `routeGiantNet`'s own initial commit itself (cheap,
no search, compounds with this fix by making the *first* commit less
blind rather than only correcting it afterward). CLAUDE.md's own
prescribed bigger fix (subnet decomposition) remains a further, heavier
option if 1-2 don't close the remaining gap.

## Part 37: giant-net follow-ups -- MSMV eligibility and corner choice, both negative

Part 36 scoped two immediate follow-ups to nv3route's own giant-fast-tier
win: (1) slow-tier (MSMV) eligibility for giant nets, (2) a
congestion-aware corner choice inside `routeGiantNet`'s own initial
commit. Scoped in `/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**#1, MSMV eligibility -- a real, disclosed risk, tested carefully, and a
clear negative.** Direct code reading found `multiSourceDijkstra`'s own
search-region bbox spans the net's ENTIRE committed footprint union all
remaining sinks (not bounded by margin for a wide-spanning net), its own
heuristic does a linear scan over all remaining sinks per queue push, and
`routeNetSlowMSMV` rebuilds its whole source list from scratch every
step -- closer to O(k^2) than the O(k) bounded cost normal escalated nets
pay. Worse, a margin-exhaustion path calls `gr_io::fail()`, a hard
`std::exit(1)` with zero recovery -- a real crash risk, not just a
performance one, for a net possibly spanning genuinely disconnected grid
regions. This needed a measure-first, safety-first staging distinct from
every prior generation's own "screen first" pattern.

Stage 1 (mandatory gate, before any real-benchmark run): a single
`routeNetSlowMSMV` call on the worst-case real giant net (newblue1's n71,
4622 unique points after dedup) using authentic negotiated prices (the
fast tier's own real state, not a freshly-flat-priced test, which would
badly underestimate cost) took 29.3s and succeeded cleanly -- no margin
exhaustion. This cleared the gate. A reduced-round screen on adaptec1
then showed a further real win on top of nv3route's own screen number
(92570 vs. 92952, and vs. 93538 with no giant mechanisms at all -- -1.03%),
with zero margin-exhaustion warnings across all 25 slow rounds.

The mandatory round-budget sanity check (this lineage's own hard-won
Part-33 discipline) reversed it: at the real 2D round budget, still zero
margin exhaustion (safety held at scale), but **77504 vs. nv3route's own
76384 -- +1.47% worse**, while taking **nearly double the runtime**
(5738s vs. 2919s). The screen's own apparent win did not survive scale --
the same shape Part 33 taught this project to watch for, but this time
the mechanism was never mechanically broken (it never crashed, never
mishandled the giant net) -- it simply made the broader 210-round
negotiation across hundreds of thousands of other nets worse in
aggregate, the same "confirmed-correct mechanism, still lost" pattern as
Part 31 (mt1route) and Part 35 (fast-tier supervision). Not promoted; no
`eval2008.pl` cross-check attempted.

**#2, congestion-aware corner choice -- cheap and safe, but also
negative.** `routeGiantNet` always committed each MST edge as a fixed
horizontal-then-vertical L-shape; the new mechanism compares that corner
against the untested alternative (vertical-then-horizontal) by summed
usage/capacity ratio over each corner's own specific edges (O(span), no
search, no `Prices&` needed -- `Usage&` alone suffices for a greedy
comparison), committing whichever is less loaded, ties keeping the
original corner. Unlike #1, this carries none of MSMV's own cost or
crash risk -- it was expected to be a safe, incremental addition on top
of nv3route's own fast-tier fix.

A reduced-round screen on adaptec1 (with nv3route's own giant-fast-tier
mechanism still active) showed a real, if modest, regression instead:
**93856 vs. nv3route's own screen number of 92952 -- +0.97% worse**,
`gr_verify`-confirmed legal. Per this project's own established
precedent (a clear screen-level loss doesn't need the round-budget
sanity check -- that safeguard exists specifically to catch *wins* that
might evaporate at scale, not to double-check losses; Part 32 skipped
`eval2008.pl` on a negative `gr_verify` result on the same reasoning),
no further real-budget verification was run. A plausible explanation,
consistent with #1's own lesson: a "better" initial commit changes which
specific edges the giant net's own topology touches at round 0, which
then propagates through the fast tier's own subsequent renegotiation
(already active from nv3route) along a different path -- better locally,
but not better once hundreds of thousands of other nets' own negotiation
dynamics react to it over the following rounds.

**Decision: neither follow-up improves on `gr/nv3route.cpp`.** `nv3route`
remains the committed best; no `nv4route.cpp` is created. Both negative
results are honest and informative in the same way: **this specific
router's own negotiation dynamics do not reward locally-better decisions
in isolation** -- three separate mechanisms now (mt1route's medium tier,
the fast tier's own accept/revert supervision, and now MSMV/corner-choice
for giant nets) have each demonstrably done exactly what they were
designed to do at the local level, and each has made the full,
many-rounds, many-thousands-of-nets aggregate result worse. CLAUDE.md's
own prescribed bigger fix for giant nets specifically -- subnet
decomposition, splitting a giant net into pieces that negotiate as
independent, ordinary-sized nets rather than one monolithic unit
reacting all at once -- remains untried and is the more structurally
different option still on the table; a piecemeal local tweak to the
existing monolithic mechanism has now failed twice.

## Part 38: subnet decomposition of large nets -- built, self-tested, a calibration bug caught and fixed, still negative once corrected

CLAUDE.md's own long-standing prescription -- *"Decompose giant nets... a
200-terminal net should be split at the coarse level into subnets, not
routed as a monolith"* -- was finally built. Qualification: a net
decomposes if its raw pin count exceeds `NV4ROUTE_DECOMPOSE_PIN_THRESHOLD`
(default 500) OR its own bounding-box half-perimeter, as a fraction of
the chip's own half-perimeter, exceeds `NV4ROUTE_DECOMPOSE_BBOX_PCT`
(default 0.30) -- design confirmed with the user, replacing
`routeGiantNet` entirely for any qualifying net (it survives only as a
fallback for a >1000-pin net that somehow misses the lower decompose
threshold -- dead code at default settings). Clustering: cut the k-1
longest edges of the net's own already-computed plain MST, no new
algorithm. Each cluster becomes its own ordinary (non-giant) `RoutedNet`,
plus a backbone connecting one representative point per cluster -- every
piece gets full fast-tier negotiation and slow-tier escalation eligibility,
unlike a monolithic giant net. Since nothing downstream of `route()`
(`negotiateViaAssignment`, `writeSolution` -- confirmed by direct grep)
tolerates more than one output block per net name, all pieces plus the
backbone are merged into a single `RoutedNet` immediately before
`route()` returns. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**Self-tests**: 58 functions, 278 checks, all passing, including two
decisive fixtures built specifically for this mechanism -- one proving a
monolithic giant net can be permanently stuck (no alternate corner exists
for a same-row edge, and it structurally never reaches the slow tier)
while decomposed pieces reach the slow tier's real MSMV search and find a
genuine detour the monolith never gets a chance to search for; a second
confirming `NV4ROUTE_DECOMPOSE=0` reproduces nv3route's exact prior giant-
net behavior. Regression continuity also confirmed on a real benchmark:
`NV4ROUTE_DECOMPOSE=0` on adaptec1's own reduced-round screen matched
nv3route's committed screen result exactly (92952, `gr_verify`-confirmed
legal).

**First real screen, default settings: a large, decisive-looking
regression** -- 121860 vs. the 92952 baseline, **+31.1% worse**,
`gr_verify`-confirmed legal. Before writing this off as decomposition
failing, a throwaway diagnostic (`diag_decompose_qual.cpp`, not part of
the repo) counted how many of adaptec1's 176716 routed nets actually
qualify for decomposition under the default bbox threshold: **2 by pin
count** (exactly the population Parts 36-37 characterized) but **1596 by
bbox ratio** -- 800x more than intended. A sweep of the threshold showed
why: population size collapses steeply as the percentage rises (1596 at
30% -> 181 at 50% -> 11 at 70% -> 7 at 80% -> 2 at 90%, converging on the
same population pin-count already catches). **The bug: a purely
percentage-based spatial threshold, calibrated by a plausible-sounding
guess (30%), doesn't scale the way a fixed pin-count threshold does --**
on adaptec1's real 324x324 grid, an entirely ordinary long net can easily
span 30% of the chip's own half-perimeter; nothing about that guess
accounted for real chip scale. The original screen's own +31.1% mostly
reflects 1594 unrelated ordinary nets getting needlessly decomposed, not
decomposition hurting its actual intended target.

**Corrected screens, properly scoped to the intended small population**:
at `NV4ROUTE_DECOMPOSE_BBOX_PCT=0.70` (11 qualifying nets), 92978 vs.
92952 -- **+0.028%, an effective tie**. Isolating decomposition's effect
on exactly the 2 pin-count-qualifying nets alone (bbox disabled
entirely), 93700 vs. 92952 -- **+0.81% worse**. Both `gr_verify`-confirmed
legal. Neither is the large, obviously-miscalibrated regression the
original screen showed, confirming the diagnosis -- but neither is a real
win either, unlike nv3route's own -0.63% fast-tier-only screen result for
the identical population. Per this project's own precedent (a clear
screen-level result -- tie or loss -- doesn't need the round-budget
sanity check, which exists to catch wins evaporating at scale, not to
double-check a result that was never a win), no further real-budget
verification was run.

**Diagnosis**: the pins-only comparison is the cleanest test available --
it decomposes exactly the same 2 nets nv3route's own giant-fast-tier fix
already treats specially, so the result is a direct, apples-to-apples
"decompose them" vs. "keep them monolithic but let them use fast-tier
negotiation" comparison, and decomposition loses it. Decomposition's own
unique differentiator over nv3route's already-committed fix is slow-tier
(MSMV) eligibility for pieces -- but Part 37 already found MSMV
eligibility for this exact population to be net-negative in aggregate,
despite succeeding cleanly at the individual-call level. It's plausible
decomposition's own pieces inherit the same underlying issue at smaller
scale: whatever makes MSMV's own real search interact badly with the
broader many-rounds negotiation once applied to former-giant-net
material doesn't disappear just because the material is now split into
smaller pieces first.

**Decision: subnet decomposition, as implemented, is not an improvement
over `gr/nv3route.cpp`.** `nv3route` remains the committed best; no
`gr/nv4route.cpp` is created. This closes CLAUDE.md's own long-prescribed
mechanism as honestly tried and negative, not merely deferred --
`routeGiantNet`'s replacement, backbone construction, and the
merge-before-return step all worked exactly as designed (confirmed by
the decisive self-tests and the clean regression-continuity match), but
the mechanism's own real value-add over the simpler already-committed fix
doesn't materialize once correctly scoped. A separate, genuinely useful
methodological finding survives this investigation regardless of the
mechanism's own fate: **a percentage-based spatial qualifier needs an
absolute floor sized to the real problem's own scale, not just a
plausible starting guess** -- worth remembering for any future mechanism
that reasons about "a significant fraction of the chip."

## Part 39: delayed fast-tier supervision -- a well-motivated joint-mechanism hypothesis, still reverses at the real budget

With `gr/nv3route.cpp` the sole committed win after six straight
negative/mixed generations (Parts 32-38), the user asked whether some of
those individually-negative mechanisms might combine into something
positive -- this project's own repeated finding that locally-good ideas
fail in aggregate cuts both ways: the same nonlinear interaction effects
that make good ideas fail alone could plausibly make two failed ideas
cancel each other's specific flaw. This part tests the most directly
motivated such combination available: Part 33's own trace found the fast
tier's *unsupervised* round-1 churn drives its entire early win (347048
-> 248780) but the tier then climbs/oscillates for the rest of the run;
Part 35 found *full* accept/revert supervision (every round) prevents
that oscillation but costs the early win itself (round 1 only reaches
288650 when every move is checked), landing +30.8% worse overall.
**Delayed supervision** -- unsupervised for the first N rounds, then
supervised from round N onward -- combines both diagnoses directly rather
than guessing blindly: keep the early win, then stabilize before it
oscillates away. Scoped in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`.

**A fast-tier-only trace (N=2, `FR1ROUTE_TWOD_ONLY=1`, 25 rounds) looked
dramatic**: round 1 reaches 248800 (matching unsupervised exactly), then
rounds 2-24 decline smoothly and monotonically with zero oscillation,
reaching 230876 by round 24 -- roughly 16% better than either pure
approach at the same round count in isolation.

**A screen sweep of N (adaptec1, reduced rounds) found that signal
mostly does not survive contact with the rest of the real pipeline**
(cleanup pass, slow tier, via negotiation) -- only N=2 was a real win, and
a modest one:

| N | screen overflow | vs. baseline (92952) |
|---|---|---|
| 1 | 105738 | +13.75% |
| 2 | 92404 | **-0.59%** |
| 3 | 92954 | +0.002% (tie) |
| 5 | 96408 | +3.72% |

All four `gr_verify`-confirmed legal. N=2's own -0.59% is a real signal,
but an order of magnitude smaller than the isolated fast-tier trace's own
16% -- itself an informative, disclosed-in-advance risk that materialized
exactly as expected: a fast-tier-only improvement measures a necessary
but not sufficient condition for a real end-to-end win.

**The mandatory round-budget sanity check reversed N=2's own screen win
entirely**: at the real 2D round budget, 87072 vs. the same-conditions
baseline of 76384 -- **+14.0% worse**, `gr_verify`-confirmed legal. This
is the third time this session a promising screen-level result has
completely reversed at real scale (after Part 33's coefficient sweep and
Part 37's MSMV eligibility for giant nets), reinforcing the same
practical lesson each time: this specific system's own convergence
dynamics change character over the FULL round budget in ways a truncated
screen can systematically misjudge, in either direction. Per this
project's own established gate, no promotion, no `eval2008.pl`
cross-check was run.

**Decision: delayed fast-tier supervision is a real, decisive negative,
despite being directly motivated by two already-diagnosed root causes
from this exact session.** `nv3route` remains the committed best; no
`gr/nv4route.cpp` is created from this mechanism. The finding itself is
still useful: it's not enough for a joint hypothesis to correctly address
each individual diagnosis in isolation (delayed supervision genuinely
does keep the early win AND prevent the fast-tier's own oscillation,
confirmed directly in the trace) -- the two diagnosed problems don't
compose additively into their two respective fixes any more than the
individual mechanisms' own local correctness composed into global
improvement earlier this session. The search for a genuinely positive
joint-mechanism combination continues; this specific combination is
closed.

## Part 40: RSMT topology bias into MSMV escalation eligibility -- checked, not a real separate effect

Part 31 originally named three open items. Two were investigated and
closed (the RSMT degree cap, Part 32; the 2D pricing loop's own
convergence gap, Parts 33-35). The third -- *"the RSMT-seeded fast tier's
own topology bias into MSMV eligibility"* -- had never been touched by
anything since. The question: since a net's own initial topology
(`exactSteinerTree` for K<=8, `steinerizeTree`'s cheap heuristic
fallback above that, both purely geometric and price-blind) is built
once and frozen for the fast tier's entire life, and since
`steinerizeTree`'s own fallback leaves real, measured slack at degree>=3
branch points (Part 24), a longer/worse fallback topology touches
strictly more grid edges by construction -- independent of real
congestion -- which could mechanically raise a net's odds of tripping
`netOverflowed`'s edge-based check and escalating to the slow tier,
whether or not escalation is actually warranted. Direct code reading
confirmed the topology is genuinely discarded on escalation (`
routeNetSlowMSMV` regrows the tree from scratch via `netFootprint`,
never reading `rn.mst` -- confirmed by an explicit comment in the file
itself), so any effect is necessarily confined to the fast-tier side:
does better topology change *how many* nets escalate, not what happens
once they do.

Part 32 already measured the overall effect of raising the exact-Steiner
degree cap (giving more nets the truly-optimal topology instead of the
fallback) and found it real-overflow-negative -- but never recorded
escalation counts, only final overflow. The cheapest possible test: rerun
Part 32's own exact configuration (cap=8/10/12, same reduced-round
adaptec1 screen) with `VROUTE_TRACE=1` added, purely to read the
"escalating N of M nets" line Part 32 never looked at. Result: escalation
count does not drop as topology quality rises -- it rises slightly and
monotonically alongside the cap (60075 -> 60368 -> 60542 nets initially
escalated; 74945 -> 75295 -> 75209 nets in the slow tier by the end),
the same direction as the already-known overflow regression, not counter
to it. Final overflow numbers (92952/94778/98416) reproduce Part 32's own
original pattern closely (small differences from nv3route's own
giant-fast-tier fix layered on top, expected and immaterial to this
question).

**Decision: topology quality does not measurably bias escalation count in
the hypothesized direction.** The mechanism is real and code-confirmed
(a longer fallback tree does touch more edges, and the tree is frozen
through the fast tier), but it is not producing the predicted "better
topology -> less over-escalation" effect in practice -- if anything, the
opposite, mildly. This is not a separate, exploitable lever; it's fully
subsumed by Part 32's own already-recorded negative finding, not an
independent channel worth its own dedicated mechanism. This closes the
last of Part 31's three originally-named open items -- every direction
that investigation flagged has now been checked at least once, all three
negative or null.

## Part 41: `gr/nv4route.cpp` -- retuning fast-tier coefficients on top of nv3route's giant-fast-tier win, the first positive joint-mechanism combination this session

Every open item from Part 31's own list closed by Part 40 (see that
Part's own closing line). The user's own next direction was to pursue
joint-mechanism combinations specifically: retuning Part 33's coefficient
sweep (`VROUTE_THRESHOLD`/`VROUTE_POWER`) on top of nv3route's own
already-committed giant-fast-tier mechanism, rather than in isolation the
way Part 33 originally tried it (before nv3route existed). Two joint
attempts earlier this session -- corner-choice combined with
giant-fast-tier, and delayed supervision's own two-phase combination
(Parts 37/39) -- were both negative, leaving the search for a genuinely
positive combination explicitly open at the end of Part 39.

**Methodology change.** Three separate dynamics- or coefficient-altering
mechanisms this session had their screen-level result reverse or
collapse at the real round budget (Part 33's coefficient sweep -9.6% ->
~0%; Part 37's MSMV eligibility -1.03% -> +1.47% worse; Part 39's delayed
supervision -0.59% -> +14.0% worse), all screened at the lineage's
original reduced budget (`VROUTE_FAST_ROUNDS=15 VROUTE_SLOW_ROUNDS=25
NV2ROUTE_VIA_ROUNDS=20`). Rather than repeat that budget a fourth time,
this sweep screened at roughly half the real operating point
(`VROUTE_FAST_ROUNDS=30 VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`,
~35 minutes per run on adaptec1 instead of under a minute) -- a real
cost/reliability tradeoff, made affordable by the abundant-compute
authorization already in effect (10 concurrent processes) for this
investigation.

**Screen** (adaptec1, nv3route's own `nv3route` binary, giant-fast-tier
left at its default-on setting, baseline 76550 at this larger budget): a
3x3 grid of `VROUTE_THRESHOLD` x `VROUTE_POWER` around Part 33's own
winning region, all 10 combinations run at once:

| threshold \ power | 0.5 | 1.0 | 1.5 |
|---|---|---|---|
| 0.0 | 78052 (+2.0%) | 74984 (-2.1%) | 76390 (-0.2%) |
| 0.05 | 76432 (-0.2%) | 76048 (-0.7%) | 74984 (-2.1%) |
| 0.1 | 79186 (+3.5%) | 75522 (-1.3%) | 76026 (-0.7%) |

All 10 `gr_verify`-confirmed legal (0 malformed/disconnected/incomplete/
unrouted). Two combinations tied for best at -2.05%: threshold=0.0/
power=1.0 and threshold=0.05/power=1.5 -- both visibly smaller edges than
Part 33's own -9.6% screen-level result at the shorter budget, exactly
the pattern the larger screen budget was chosen to surface rather than
be misled by.

**Mandatory round-budget sanity check**, both tied candidates, adaptec1,
real committed operating point (default 60/150 2D rounds,
`NV2ROUTE_VIA_ROUNDS=200`), against nv3route's own already-committed
adaptec1 baseline (74210, Part 36):

| combination | overflow | delta |
|---|---|---|
| threshold=0.0, power=1.0 | 73024 | -1.60% |
| threshold=0.05, power=1.5 | 72882 | **-1.79%** |

Both `gr_verify`-confirmed legal. **Neither reversed** -- the first
coefficient- or dynamics-related sweep in this entire session whose
screen-stage signal survived the real-budget check intact, after three
straight reversals (Parts 33, 37, 39). threshold=0.05/power=1.5 was
slightly better and carried forward.

**Full three-benchmark result** at the committed operating point,
`gr_verify`-confirmed legal on all three (0 malformed/out-of-bounds/
disconnected/incomplete/unrouted) and cross-checked exactly against
`eval2008.pl` (total overflow, max overflow, and scored wirelength
matching to the unit on every benchmark):

| benchmark | nv3route (committed best) | nv4route (threshold=0.05, power=1.5) | delta |
|---|---|---|---|
| adaptec1 | 74210 | 72882 | **-1.79%** |
| newblue1 | 21634 | 21418 | **-1.00%** |
| bigblue1 | 185246 | 183044 | **-1.19%** |
| **sum** | **281090** | **277344** | **-1.33%** |

**Decision: `gr/nv4route.cpp` is this project's new best real result,
replacing `gr/nv3route.cpp`.** Unanimous across all three benchmarks --
clears this project's own promotion bar cleanly, the same bar kroute and
nv2route (both unanimous) and nv3route (unanimous non-negative) cleared.
Self-tests: a decisive hand-built fixture proving the two retuned
defaults are wired through the real `priceExponentTerm`/
`defaultThreshold` functions and produce a materially different pricing
curve than nv3route's old defaults at an identical usage/capacity ratio
(0.44 vs. 0.18, not a rounding-level difference), plus every inherited
nv3route self-test unchanged and passing (58 test functions total, clean
`-Wall -Wextra` build).

**This is the first genuinely positive joint-mechanism combination found
in this session's own explicit search for one**, after two negative
attempts (Parts 37, 39). It is a concrete, if small-sample, data point in
favor of a specific shape of combination: a structural fix (nv3route --
changes *which nets* reach fast-tier negotiation) composing well with a
dynamics fix (this generation -- changes *how* that negotiation prices
congestion once nets reach it), where two dynamics-only fixes stacked on
each other (delayed supervision) or a structural fix stacked with a
second small structural tweak (corner-choice) both failed. Not proof
that's the general rule for this router's own many-body negotiation
dynamics -- a sample of three joint attempts is far too small to
generalize from -- but worth keeping in mind when scoping the next
candidate. `nv4route.cpp`'s own header carries the full derivation and
result table.

## Part 42: `gr/nv5route.cpp` -- checkpoint jolt + halo pricing, a clean and decisive negative

Motivated by three new diagnostics run against nv4route.cpp's own committed
output (adaptec1 72882 / newblue1 21418 / bigblue1 183044): overflowed edges
cluster into a small number of large contiguous regions (bigblue1: one
connected component covers 71.9% of every overflow-touched tile); 96-99.98%
of nets touching more than one overflowed edge are already escalated to
slow-tier MSMV and still overflow; and the median overflow-touching net
shows essentially zero detour. User-proposed mechanism, refined over
several rounds of discussion (full design in
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`): a discrete
"jolt" -- a significant, threshold-triggered multiplicative cost penalty
on currently-overflowed edges, distinct from the existing smooth `hist`
term -- plus a smaller "halo" penalty on grid-adjacent (not themselves
overflowed) edges, plus a continuous relax-rate knob spanning fully-sticky
through gradual capacity-slack-proportional relaxation through
effectively-full-reset.

Built as `gr/nv5route.cpp`, forked from `gr/nv4route.cpp`. New `updateJolt`
function called at the same three round-boundary checkpoints
`updateHistory` already uses; a hand-built grid-adjacency helper
(`forEachAdjacent2dEdge`, none existed anywhere in this codebase); a
second independent multiplicative array (`joltH`/`joltV`) riding the same
`edgeCost` choke point `histH`/`histV` already uses (6 call sites, one
token each). Precedence (an edge both overflowed and halo-adjacent only
ever gets the full jolt) and the relax-rate's promotion behavior (a halo
edge that later genuinely overflows is promoted to full jolt and stays
there) both fall out of the same value-based comparison
(`jolt < haloFactor`) with no separate bookkeeping needed. 63 test
functions, 303 checks, 0 failures, clean `-Wall -Wextra` build -- 5 new
decisive fixtures: pure-function jolt/halo/precedence/trigger-gate
correctness on a hand-built usage grid; the relax-rate continuum
(sticky/gradual/full-reset) including a direct check that more free
capacity relaxes by a strictly larger step than an edge just barely
clearing capacity; a halo tie-break fixture proving the real production
`priceCandidate` function breaks an exact geometric tie toward the
non-adjacent corner once halo is enabled; a real two-round
`routeNetFast` demonstration (an exact-tie fixture has no deadzone --
any nonzero perturbation flips it regardless of magnitude, so this
fixture deliberately gives the alternate candidate its own small,
independent handicap first, so a small history step provably isn't
enough to overcome it while a jolt is); and a toggle-inertness check
proving `NV5ROUTE_JOLT=0` is unaffected by even extreme values of every
other jolt/halo/relax knob.

**Screen** (adaptec1, half the real round budget --
`VROUTE_FAST_ROUNDS=30 VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`,
this session's own established discipline after three short-screen
reversals earlier this session), direct jolt alone
(`NV5ROUTE_HALO_RADIUS=0`), against a same-conditions baseline
(`NV5ROUTE_JOLT=0`, real overflow 74984), all `gr_verify`-confirmed legal
(0 malformed/disconnected/incomplete/unrouted):

| `NV5ROUTE_JOLT_FACTOR` | real overflow | delta |
|---|---|---|
| 1.1 (gentlest tested) | 75212 | +0.30% |
| 1.5 | 78432 | +4.60% |
| 2.0 | 82142 | +9.55% |
| 3.0 | 87678 | +16.93% |

Monotonic and unambiguous across the entire tested range -- there is no
magnitude, from barely-there to strong, at which the direct jolt helps.
`NV5ROUTE_JOLT_MIN_OVERFLOW` (20/100/500) made no measurable difference
at any factor: at this problem scale the checkpoint's own overflowed-edge
count already vastly exceeds all three thresholds within the first few
rounds, so every value tested was equivalent to "always triggered" --
this specific threshold dimension turned out not to be a live variable
at real-benchmark scale, the same shape of lesson Part 38's own
percentage-threshold calibration bug taught for a different mechanism.

**Halo check**: layered on top of the gentlest (least-bad) jolt setting
(`NV5ROUTE_HALO_RADIUS=1 NV5ROUTE_HALO_FACTOR=1.3`, `JOLT_FACTOR=1.1`),
real overflow 75660 (+0.90%) -- slightly *worse* than jolt alone at the
same factor (75212, +0.30%), not better. `gr_verify`-confirmed legal.

Per the plan's own explicit contingency ("if radius-1 itself washes out,
that's the answer; don't chase a larger radius hoping to fix it"), the
same logic closes the whole line here: halo compounding rather than
reversing an already-losing base mechanism means the mandatory
real-budget sanity check, a relax-rate sweep, and a three-benchmark run
are all skipped -- per this project's own established discipline, a
clear screen-level *loss* doesn't need the sanity check (that safeguard
exists specifically to catch wins evaporating, per Parts 32/37's own
precedent, not to double-check losses), and there is no plausible path
by which retuning the relaxation dynamics of a base mechanism that loses
monotonically across its entire tested magnitude range would reverse
that trend.

**Decision: `gr/nv5route.cpp` does not improve on `gr/nv4route.cpp`.**
`nv4route` remains the committed best (adaptec1 72882 / newblue1 21418 /
bigblue1 183044); `nv5route.cpp` stays in the repository, frozen, as a
real, honestly-measured negative result. **Diagnosis, tying directly back
to the diagnostics that motivated this generation**: 33.3% (newblue1) to
60.0% (bigblue1) of every net that ever escalates to slow-tier MSMV
still overflows despite the best search this router has -- the honest
read of that number, now confirmed by this generation's own result, is
genuine shared-capacity contention, not a pricing gap. Making a
contested edge more expensive doesn't create capacity that isn't there;
it only changes which net loses the contest, or pushes a net into a
detour that lands on a different already-scarce resource instead -- and
the halo's own added cost on top compounds that redistribution rather
than resolving it. This is the same lesson region-level aggregate
congestion pricing taught at a coarser spatial scale (`uroute`/Part 18,
`vr1route`/Part 20: a real, decisively self-tested signal that
nonetheless landed as a wash on both the weakest and strongest search
architectures this project has), now confirmed again at the finer,
single-hop scale this generation tested, on the most powerful search
architecture available (nv4route's own negotiated fast/slow-tier
pipeline). **The "nearby congestion also matters" family of mechanisms
has now been tried at two very different spatial scales, on two very
different search strengths, and lost every time** -- a considerably
stronger basis than either result alone for treating this as a real
property of this router's own negotiation dynamics on these benchmarks,
not an artifact of one particular implementation.

## Part 43: `gr/nv6route.cpp` -- post-convergence cluster rescue, a second closed-negative on the same idea

Motivated by the same clustering diagnostic that motivated Part 42
(overflow concentrates into a small number of large contiguous regions --
bigblue1: one connected component covers 71.9% of every overflow-touched
tile) and the tier cross-tab finding that 33.3%-60.0% of every net that
ever escalates to slow-tier MSMV still overflows despite the best search
available. Where Part 42 (`nv5route`, jolt/halo pricing) tested pricing a
congested region and found it redistributes who loses rather than
helping, this generation tests the other natural response: jointly
reroute the nets sharing a contiguous region, as one supervised batch,
instead of pricing it.

**Extremely relevant precedent, checked before building anything**:
`gr/broute.cpp` (Part 12, this project's older pre-MSMV, pre-2D-collapse
architecture) already tried almost exactly this -- cluster overflowed
tiles by tile-adjacency, batch-reroute contending nets sequentially,
accept/reject the whole cluster as one supervised unit. Result: 218
clusters attempted, 0 accepted, on two variants (uncapped and capped to
the top-12 worst-offending nets by overflow magnitude). Its own
post-mortem named three not-yet-distinguished reasons: (a) sequential
per-net rerouting within a batch is a better-ordered greedy pass, not a
real joint solve; (b) the round budget tested may have left clusters
still actively forming, not at a stable local minimum; (c) member nets
were selected by overflow magnitude but *processed* in net-id order, not
magnitude order.

Built as `gr/nv6route.cpp`, forked from `gr/nv4route.cpp` (the committed
best -- not `nv5route.cpp`, whose own mechanism is a separate, already-
closed experiment). Specifically targets all three reasons using
capabilities broute never had: runs only after nv4route's own full
fast+slow-tier convergence completes (addressing (b)); processes each
cluster's members in descending overflow-magnitude order, **re-ranked at
the start of every pass** rather than fixed once (addressing (c) --
broute capped by magnitude but then processed in net-id order); uses
real MSMV (`routeNetSlowMSMV`, attach-anywhere-on-committed-wire search)
instead of broute's older fixed-topology `routeNetSlow` (broute was
forked from the pre-MSMV kroute lineage); and a bounded multi-pass sweep
per cluster (default 5, capped since MSMV is markedly more expensive per
call than broute's own fixed-topology Dijkstra), partially addressing
(a). New `findOverflowClusters2D`/`clusterNets2D` adapt broute's own
proven clustering/membership logic to nv4route's 2D-collapsed `Usage`
directly (no per-layer loop needed). A critical correctness step,
confirmed necessary by direct code reading before writing anything: the
live `usage` after nv4route's slow-tier loop exits may not correspond to
`rep.nets` (only reassigned mid-loop when a round improves on the
historical best) -- the existing usage-rebuild idiom already used twice
elsewhere in `route()` (after phase A, after cleanup) is applied a third
time before the new pass runs, so it starts from a genuinely
self-consistent snapshot. 68 test functions total, 0 failures, clean
`-Wall -Wextra` build -- 5 new decisive fixtures adapted directly from
broute's own proven-correct test suite: pure clustering correctness on a
hand-built two-group `Usage`; cap-trims-by-magnitude-not-list-order
(3 nets inserted in ascending overflow order specifically so a
list-order bug would fail visibly); a hand-verified two-net jammed-edge
fixture resolving cleanly to zero overflow (with the same honest framing
broute's own equivalent test used -- proves the mechanics are correct,
not that MSMV specifically was necessary for that particular synthetic
case, which a hand-verifiable fixture can't settle by construction any
better than broute's own could); toggle-disabled inertness; and a
usage-consistency regression guard.

One inherited self-test (`testGiantNetJoinsFastTierAndEscapesA...`)
needed updating: the cluster-rescue pass, left at its default-on
setting, correctly discovered it could reroute the fixture's own
`blocker` net (not giant, which cluster rescue always excludes) off the
shared edge on its own -- a real, correct interaction, but one that
defeated that fixture's own purpose of isolating
`NV3ROUTE_GIANT_FAST_TIER`'s effect in isolation. Fixed by explicitly
passing `clusterRescueEnabled=false` in that one test's own two calls,
same "isolate the flag under test" fix pattern Part 41/42 both needed
for their own inherited fixtures.

**Honest assessment, stated in the plan before any empirical run**:
reason (b) (post-convergence timing) was judged the strongest of the
three candidate fixes -- a genuinely different architectural condition.
Reason (c) (magnitude-first ordering) was judged plausible but modest --
it changes *which* net claims freed capacity first, not whether joint
capacity exists at all. Real MSMV was judged the one difference with a
mechanism that could actually change the feasible set. Multi-pass was
disclosed as only a partial mitigation of (a) -- if two nets must move
*simultaneously* to both legalize, no number of sequential passes finds
that, a structural ceiling on the whole approach. The plan's own stated
stop condition: given `broute`, `froute`, and the escalation-still-fails
diagnostic already point at the same "real shared-capacity contention"
read, a second closed-negative result here should end this specific
line, not motivate a third variant.

**Cheap first diagnostic checkpoint** (per the plan's own explicit
staging, given the strength of the prior precedent): adaptec1, a very
reduced round budget (`VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=15
NV2ROUTE_VIA_ROUNDS=20`, well under even the half-budget screen),
`NV6ROUTE_CLUSTER_LOG` + `VROUTE_TRACE=1` on, purely to check whether
clustering/membership/capping fire on real data and whether the accept
rate is obviously zero again before spending a full screen's time.
`gr_verify`-confirmed legal (0 malformed/disconnected/incomplete/
unrouted):

    370 clusters attempted, 0 accepted (0%)
    370 of 370 batches (100%) made local overflow strictly worse --
      not one tie, not one improvement, across every single cluster
    every cluster ran its full 5-pass cap without ever early-stopping
      (MSMV kept finding different routes each pass, never converging
      to a stable, no-further-change state within a cluster)

**This is broute's own exact failure signature, reproduced immediately
on the current best architecture**, despite all three targeted
mechanism differences and a multi-pass allowance broute never had. The
"every cluster used its full pass budget without ever converging"
pattern is itself informative: it points at reason (a) -- sequential
rerouting is still a better-ordered greedy churn, not a joint solve, and
more passes just means more churn, not convergence toward a stable
optimum -- rather than a simple capacity-exhaustion-with-nothing-to-try
signature (which would show early-stop-at-pass-1 with zero changes
instead). Per the plan's own pre-committed stop condition, this result
alone is decisive enough to close the investigation without spending the
full screen's time: **skipped the half-budget screen, the mandatory
real-budget sanity check, and the three-benchmark run** -- all
conditioned on the screen showing a win, which this checkpoint already
rules out unambiguously.

**Decision: `gr/nv6route.cpp` does not improve on `gr/nv4route.cpp`.**
`nv4route` remains the committed best; `nv6route.cpp` stays in the
repository, frozen, as a real negative result. **Three separate
mechanisms now point at the same conclusion** -- `broute`'s original
batch rescue, `froute`'s heat-map reorder, and this generation's
post-convergence/magnitude-ordered/MSMV-powered/multi-pass variant --
joined by the diagnostic finding that motivated this generation in the
first place (escalated nets failing MSMV at a 33-60% rate is real
shared-capacity contention, not a search-quality or batching-strategy
gap). Per the plan's own explicit stop condition, no third variant of
"reroute a contiguous overflow region as one supervised batch" should be
attempted without a genuinely new capability beyond what MSMV +
magnitude-ordering + post-convergence timing + multi-pass already bring.
Combined with Part 42's own closed-negative result on the pricing side
(jolt/halo), **both natural responses to the spatial-clustering finding
-- price it, or batch-reroute it -- are now closed.** The remaining
overflow this diagnostic pair characterized appears to be a genuine
structural property of these benchmarks' placement and capacity, not a
lever this router's own negotiation or batching machinery can pull
further without a fundamentally different capability.

## Part 44: `gr/nv7route.cpp` -- reversed ordering + demand-seeded prices, both closed negative

A literature-reading pass (reading NTHU-Route 2.0's own ICCAD 2008 paper --
the router that won 1st place at the ISPD 2008 global routing contest on
these exact benchmarks) found two things directly relevant to open
questions in this project. First: every batch mechanism built here
(`broute.cpp` Part 12, `nv6route.cpp` Part 43) sorts clusters largest-first
and member nets by worst-overflow-magnitude first, unquestioned --
NTHU-Route 2.0's own validated ablation found the OPPOSITE of both wins on
these benchmarks (least-congested region first, releasing capacity inward
rather than pushing nets into unprocessed regions; largest-bounding-box net
first, since it has more routing freedom to clear out of the way). Second:
CLAUDE.md's own "Seed prices from a demand estimate" design decision has
never been implemented anywhere in this project's lineage -- `gr/groute.cpp`
has an on-the-record deferral note ("Revisit this once stage 7 makes the
stampede real"), never revisited since.

Built as `gr/nv7route.cpp`, forked from `gr/nv6route.cpp` (Mechanism A
modifies nv6's own `clusterRescuePass` directly). `hpwlOf` and
`computeRudyDensity` ported verbatim from `gr/droute.cpp`. Both mechanisms
independently toggleable (`NV7ROUTE_CLUSTER_LEAST_FIRST`,
`NV7ROUTE_NET_LARGEST_BBOX_FIRST`, `NV7ROUTE_PRICE_SEED` +
`NV7ROUTE_PRICE_SEED_STRENGTH`), each defaulting to the new behavior, each
toggle-off reproducing nv6/nv4's own exact prior behavior. 72 test
functions, 0 failures, clean `-Wall -Wextra` build -- 4 new decisive
fixtures, including a genuinely hard-won one: the first version of the
per-pass net-order test gave an order-INDEPENDENT result (the same net
always won, regardless of the toggle) because the test net with the
"boosted magnitude" carried an extra overflowed edge that formed its OWN
separate cluster; since clusters are always processed smallest-first
regardless of the net-order toggle, that second cluster silently got
rescued (rebuilding the shared-edge charges) before the shared cluster's
own ranking was even computed. Fixed by keeping the magnitude-boosting
charge within the SAME cluster's own tile set -- a real, instructive
example of exactly the kind of confound this project's own testing
discipline exists to catch before trusting an assertion.

**Mechanism A screen (cheap first checkpoint, per the plan's own explicit
staging given the strength of the prior negative precedent)**: adaptec1,
the same very reduced round budget nv6route's own checkpoint used
(`VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=15 NV2ROUTE_VIA_ROUNDS=20`), all
three non-trivial toggle combinations, `gr_verify`-confirmed legal:

| `(CLUSTER_LEAST_FIRST, NET_LARGEST_BBOX_FIRST)` | clusters attempted | accepted | real overflow |
|---|---|---|---|
| (1,1) | 370 | 0 | 96166 |
| (1,0) | 370 | 0 | 96166 |
| (0,1) | 370 | 0 | 96166 |

All three identical, and identical to nv6route's own original (unreversed)
checkpoint number (96166) -- not just the same cluster-rescue counters but
the entire downstream pipeline landed byte-for-byte the same. Reversing
either ordering dimension, or both together, changed **nothing at all**.
Per the plan's own pre-committed stop condition, this closes Mechanism A
immediately without spending a full screen.

**Mechanism B screen**: adaptec1, half the real round budget
(`VROUTE_FAST_ROUNDS=30 VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`),
cluster-rescue ordering left at nv6's own defaults, sweeping
`NV7ROUTE_PRICE_SEED_STRENGTH` against a same-conditions unseeded baseline
(74984), all `gr_verify`-confirmed legal:

| `NV7ROUTE_PRICE_SEED_STRENGTH` | real overflow | delta |
|---|---|---|
| 0.02 | 75522 | +0.72% |
| 0.05 | 74736 | **-0.33%** (best) |
| 0.1 | 74862 | -0.16% |
| 0.2 (calibrated default) | 75750 | +1.02% (worst) |
| 0.4 | 75712 | +0.97% |

No monotonic trend, no value clearing a real improvement -- every tested
strength lands within about 1% of baseline, bouncing in both directions.
Given no candidate value shows a genuine win, the mandatory real-budget
sanity check and three-benchmark run are both skipped per this project's
own discipline (nothing here is worth confirming further).

**Decision: neither mechanism improves on `gr/nv6route.cpp`** (equivalently
on `gr/nv4route.cpp`, the actual committed best, since nv6's own mechanism
is itself already closed-negative). `nv4route` remains the committed best;
`nv7route.cpp` stays in the repository, frozen, as two real,
independently-measured negative results.

**Mechanism A is the more informative of the two.** A validated technique
from a real ISPD-contest-winning router, tested directly against the exact
same benchmarks it was validated on, produced literally zero measurable
effect on this codebase's own batch-rescue mechanism -- not a small loss,
not noise, an EXACT match to the unreversed baseline across every metric
checked. This is strong, direct confirmation of nv6's own structural
diagnosis: the failure mode here isn't which end of the congestion you
start clearing from, it's that sequential single-net rerouting inside a
batch is a greedy churn that never becomes a joint solve, regardless of
order. Three generations (`broute`, `nv6route`, `nv7route`) have now
exhausted ordering, magnitude, real MSMV, and multi-pass as levers on this
specific mechanism; per the plan's own stop condition, a fourth variant of
"reroute a contiguous region as one supervised batch" should not be
attempted without a genuinely different capability. **Mechanism B lands in
the same "real idea, negligible effect" category this project has now
found repeatedly for coefficient-level and dynamics-level tuning** (Part 33's
own coefficient sweep, Part 40's topology-bias check) -- seeding changes
*when* the fast tier's own convergence happens, not reliably *where* it
ends up, exactly as this file's own header predicted before any number was
measured.

## Part 45: `gr/nv8route.cpp` -- joint test of three more NTHU-Route 2.0
## mechanisms, combined with Part 44's two: does not win

Direct follow-up to a question raised after Part 44: NTHU-Route 2.0 is a
single integrated pipeline, tuned as a whole, not as independently-ablated
pieces -- what if its real win only shows up *in combination*, and Part
44's one-mechanism-at-a-time approach could never show that, structurally?
Three more pieces of that pipeline had never been tried here, even alone:
a congestion-seeded, self-limiting clustering primitive (replacing plain
tile-adjacency union-find), a Gompertz-decaying base wirelength cost, and
a two-stage overflow-prediction pricing term (folding in iteration count
and how long an edge has stayed overflowed, not just its current usage/
capacity ratio -- exact formulas in `gr/nv8route.cpp`'s own header and
`/Users/bobboyd/.claude/plans/async-shimmying-pancake.md`).

Built as `gr/nv8route.cpp`, forked from `gr/nv7route.cpp` (inheriting
Part 44's own two mechanisms, both left ON by default per this
generation's own "include them, test in combination" design). All five
mechanisms independently toggleable; each toggle's off setting reproduces
its parent generation's exact prior behavior -- confirmed by a shell-level
parity run (all four new toggles off) producing a byte-identical `.route`
file against `nv7route.cpp` on the same adaptec1 input. 5 new self-tests,
73 total, 0 failures, clean `-Wall -Wextra` build.

**Results**, adaptec1, the same screen round budget Part 44 used
(`VROUTE_FAST_ROUNDS=30 VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`),
all `gr_verify`-confirmed legal:

| config (nv7's two mechanisms ON unless noted) | real overflow | delta* |
|---|---|---|
| baseline (nv7's two ON, C/D/E all OFF) | 75750 | -- |
| Mechanism C alone (seeded clustering) | 75750 | +0.00% |
| Mechanism D alone (Gompertz decay) | 74968 | -1.03% |
| Mechanism E alone (overflow prediction) | 78134 | +3.15% |
| Joint: nv7's two + C + D + E (all five ON) | 78140 | +3.15% |
| Bonus: C+D+E alone, nv7's two OFF | 76898 | +2.55%** |

\* vs the 75750 baseline. \** vs nv7's own recorded 74984 all-off baseline
at this same budget (Part 44).

**Mechanism C (seeded clustering): zero measurable effect, and now
explained structurally rather than merely observed.** A `VROUTE_TRACE`
diagnostic at a cheaper round budget shows `clusterRescuePass` attempts
933 clusters and accepts exactly 0 with seeded clustering on -- the
identical 0-accepted signature Part 44's own Mechanism A found for
reordering. This extends that finding past processing order to the
clustering PRIMITIVE itself: when every attempted batch reverts regardless
of composition, which specific nets got grouped together cannot matter,
because rejection restores usage to its exact pre-attempt state either
way. The mechanism's own self-test proves it genuinely produces a
different grouping on a fixture built to trigger it (1 union-find cluster
splits into 2 seeded ones under a sharp within-cluster congestion
gradient) -- it works exactly as designed; it simply cannot matter
downstream of a batch-acceptance gate that three prior generations
(`broute`, `nv6route`, `nv7route`) already found rejects everything on
real data.

**Mechanism D (Gompertz decay): a small, real, consistent win in
isolation** -- -1.03% against the nv7-mechanisms baseline, and -0.02%
against nv7's own all-off 74984 baseline (i.e. Gompertz decay alone very
nearly matches nv6's own committed number even riding on top of nv7's own
net-negative-to-negligible mechanisms). Small, but the only one of the
three new pieces that moved in the direction the literature predicted.

**Mechanism E (overflow prediction): a consistent, meaningful regression
everywhere it appears** -- +3.15% alone, +3.15% in the full joint
combination (nearly identical to its own solo number, meaning it
dominates whatever else is present in the mix), and the C+D+E-alone
configuration (76898) sits almost exactly between D's own gain and E's own
loss -- consistent with E's drag simply outweighing D's gain, not a
sharper negative interaction. `k1`/`k2`/`k3` were explicit, disclosed-up-
front unvalidated guesses (not paper-sourced -- only beta=5, gamma=0.1
were actually recovered from the paper text); this result indicts *this
specific untuned coefficient choice*, not necessarily the underlying
formula. A coefficient sweep was not run here -- out of scope for this
generation's own approved plan (which asked whether the five mechanisms
combine to a win, not to re-run a Part 33/44-style coefficient-tuning
exercise a third time) -- left as a disclosed, real candidate for a future
generation, not chased further here.

**Decision: the joint five-mechanism combination does not win** -- 78140
vs the 75750 same-conditions baseline, a real regression, not noise. The
"must they be combined" hypothesis is not supported by this evidence: the
combination is dominated by its worst component (Mechanism E), not
elevated by any synergy between the three new pieces or with nv7's own
two. Per the plan's own gate, the mandatory real-budget sanity check and
the three-benchmark run are both skipped -- no configuration measured here
clears a real win to justify spending them. `gr/nv4route.cpp` remains the
committed best. `nv8route.cpp` stays in the repository, frozen, as a real,
decisive answer to a real question rather than a positive result:
replicating more of a competition router's own literature, even genuinely
combined rather than tested piecemeal, does not automatically transfer a
win onto a differently-structured codebase -- a batch-acceptance gate
already closed by three prior generations, and one untuned pricing
formula, both mattered more here than whether the pieces were tested
together or apart.

### Part 45 addendum: cross-benchmark check confirms the negative, unevenly

User-requested follow-up beyond the plan's own gate (Stage 5's three-
benchmark run is normally skipped after a losing screen) -- run
baseline-vs-joint on newblue1 and bigblue1 too, to see whether adaptec1's
own regression is representative. Same screen round budget, `gr_verify`-
confirmed legal and `eval2008.pl`-cross-checked exactly on both:

| benchmark | baseline | joint (all five ON) | delta |
|---|---|---|---|
| adaptec1 | 75750 | 78140 | +3.16% |
| newblue1 | 22442 | 22404 | -0.17% |
| bigblue1 | 186864 | 199102 | +6.55% |

Genuinely different behavior across benchmarks, not a uniform effect --
bigblue1 regresses roughly twice as badly as adaptec1, while newblue1 is
essentially flat (a hair better, well inside noise). This strengthens
rather than complicates Part 45's own "does not win" conclusion: two of
three benchmarks show a real regression (one of them, bigblue1, the worst
single number in the whole experiment), and the third shows no real
movement in either direction -- no benchmark clears a win. Bigblue1's own
larger regression is consistent with Mechanism E's disclosed-unvalidated
k1/k2/k3 constants generalizing poorly to its own congestion profile,
rather than adaptec1's own number being an unrepresentative outlier.

## Part 46: `gr/nv9route.cpp` -- congestion-based region ordering and
## multi-round cluster rescue, both closed negative, for two different
## reasons

Direct follow-up to a question raised after Part 45's own negative
result: why does NTHU-Route 2.0's own literature keep failing to transfer
here -- is something missing that only shows up in real contest-winning
code, or something written about but not fully integrated? Direct code
inspection (not speculation) found two concrete, disclosed gaps in what
this project had actually built versus what the paper describes:

1. `clusterRescuePass`'s own cluster-level sort has always used tile
   COUNT, even after Part 45 built a real congestion-seeded clustering
   primitive that computes each region's own average congestion as a
   byproduct. NTHU-Route 2.0's own finding is specifically "process the
   least CONGESTED region first" -- never actually tried until this
   generation (Mechanism F).
2. `clusterRescuePass` runs exactly once, post-convergence, but direct
   inspection of `undoCharge` (nv8route.cpp:1630) confirms it only
   subtracts from `usage.h/v` -- it never touches `Prices`. Every rejected
   cluster's own attempt still called `negotiateWire` on every edge it
   tried (via `routeNetSlowMSMV` -> `commitPath`), raising prices
   unconditionally at commit time. So a rejected batch leaves usage
   exactly as it was but leaves prices permanently higher on the contended
   edges. Four generations (`broute`, `nv6`, `nv7`, `nv8`) all call this
   pass exactly once and report the result as final -- never giving a
   later attempt the chance to react to price pressure an earlier,
   rejected attempt already left behind (Mechanism G).

Built as `gr/nv9route.cpp`, forked from `gr/nv8route.cpp`. Mechanism E
(overflow-prediction pricing) defaults OFF here -- Part 45 found it a
consistent regression on all three benchmarks with disclosed-unvalidated
constants, and carrying a known-bad mechanism into this experiment would
confound the result. 7 new self-tests, 76 total, 0 failures, clean
`-Wall -Wextra` build. `NV9ROUTE_CLUSTER_ORDER_BY_CONGESTION=0
NV9ROUTE_MULTIROUND_RESCUE=0 NV8ROUTE_OVERFLOW_PREDICTION=1` reproduces
nv8's own default run byte-for-byte (confirmed via a shell-level parity
diff, adaptec1, reduced round budget: 81746/94936 identical on both
sides).

**Mechanism G's own cheap checkpoint (mandatory before any screen spend,
given the size of the claim): closed immediately, without a screen.**
adaptec1, a very reduced round budget, `VROUTE_TRACE=1`, Mechanism F off
to isolate G alone. Overflow is completely FLAT across all three
meta-rounds the patience budget (3) allowed before stopping:

    cluster rescue meta-round 0: overflow now 81196 (best so far 81196)
    cluster rescue meta-round 1: overflow now 81196 (best so far 81196)
    cluster rescue meta-round 2: overflow now 81196 (best so far 81196)

Re-run with `NV6ROUTE_CLUSTER_PASSES=1` (forcing exactly one internal pass
per meta-round, specifically to rule out "5 internal passes already
reaches whatever equilibrium is reachable" as the explanation before
closing this out): identical, still completely flat. The mechanism's own
crux self-test
(`testMultiRoundRescueSucceedsWherePriceStaysElevatedAfterRevert`) proves
the underlying claim is mechanically real -- a hand-built 2-net fixture,
prices starting flat at 1.0, genuinely resolves on meta-round 3 where a
single `clusterRescuePass` call cannot, because `undoCharge` really never
reverts prices and enough cumulative growth (~2x) eventually flips a path
preference. But real edges reach `clusterRescuePass` only after dozens of
main-loop rounds have already shaped their prices, often already
substantially elevated -- a few more rescue-attempt price bumps are a
small RELATIVE change against an already-large base, and within a
patience-bounded handful of meta-rounds, apparently never enough to flip
any real net's own path preference. The mechanism is real; the scale
mismatch between what a hand-built fixture needs and what a fully-
negotiated real benchmark's prices already look like is what closes it.
Per the plan's own gate, the full screen is skipped.

**Mechanism F's screen: zero measurable effect, a third independent
confirmation of the same structural finding.** adaptec1, screen round
budget, `gr_verify`-clean:

| config | real overflow |
|---|---|
| baseline (Mechanism F off) | 74968 |
| Mechanism F alone (order by congestion) | 74968 |

Byte-for-byte identical -- same wirelength (3914397), vias (3336673), max
overflow (26), overflowed edges (22735). `orderByCongestion` genuinely
reorders clusters (proven by its own three self-tests, including a fixture
where congestion and size orderings disagree), but a `VROUTE_TRACE`
diagnostic confirms `clusterRescuePass` still attempts 928 clusters and
accepts exactly 0 -- the identical 0%-acceptance signature Part 44's
Mechanism A (reorder by size) and Part 45's Mechanism C (swap the
clustering primitive) already found for their own respective changes.
Three independently-varied changes to what gets clustered and what order
clusters get processed in all land on the identical wall.

**Decision: neither mechanism improves on `gr/nv8route.cpp`'s own
individually-positive-or-neutral baseline (nv7's two mechanisms +
Mechanism D, 74968).** `gr/nv4route.cpp` remains the committed best.
`nv9route.cpp` stays in the repository, frozen, as two more real
negatives -- but unusually informative ones. Repeated batch-supervision
has now been tested past ordering (Part 44), past the clustering primitive
(Part 45), and past congestion-aware ordering AND repetition itself (this
generation) without a single acceptance-rate improvement anywhere across
five independently-varied changes and four generations
(`broute`, `nv6`, `nv7`, `nv8`, `nv9`). The honest reading is that
`clusterRescuePass`'s own core design -- propose a whole batch, judge it
once against its own pre-batch baseline, keep or revert as one unit -- is
itself the ceiling, not any of the specific things that have been varied
around it. A future attempt at this family would need to change that core
judgment itself (e.g. a genuinely joint/simultaneous solve across a
cluster's members, rather than sequential per-net reroutes judged as one
batch), not another clustering, ordering, or repetition variant.

## Part 47: `gr/nv10route.cpp` -- joint branch-and-bound candidate
## assignment for cluster rescue, at real scale, closes negative -- but
## with the first nonzero signal this family has ever produced

Direct follow-up to Part 46's own diagnosis: across five independently-
varied changes and four generations, `clusterRescuePass`'s own sequential-
then-judged design never moved past 0% batch acceptance. The one thing
never varied was the mechanism's own core judgment: propose a whole
batch, judge it once against its own pre-batch baseline, keep or revert
as one unit -- never a genuinely joint/simultaneous solve across a
cluster's members.

That mechanism already exists in this codebase, under a different
species. CLAUDE.md's own naming convention names "the joint-candidate-
assignment/branch-and-bound lineage (`broute` through `qroute`)" as
distinct from the MSMV lineage this project's `nv` species descends from.
`gr/aroute.cpp` is the file in that span where the real B&B starts:
generate a few candidate routes per net against one frozen "cluster
removed" baseline, then pick one candidate per net JOINTLY via an exact,
admissibly-pruned branch-and-bound search minimizing the cluster's total
overflow.

**Honest prior, disclosed before any new measurement.** This is not an
untested idea. `aroute.cpp`'s own header reports an EXHAUSTIVE 0/218
result at 12-net cluster scale (every search ran to true completion, 0
hit the node cap) -- the seeded sequential-greedy incumbent was worse
than doing nothing in all 218/218 clusters too (Parts 12-17). Two things
made a fifth attempt worth running rather than a re-run: real clusters on
these benchmarks average roughly 40 nets (`nv9route.cpp`'s own
eligibility cap is already 40, over 3x aroute's own tested scale), and
`aroute.cpp` predates the MSMV refactor entirely -- its own candidates
were length-weight-tuned variants of the same fixed-topology search, never
exploring MSMV's own attach-anywhere flexibility.

Built as `gr/nv10route.cpp`, forked from `gr/nv9route.cpp`, porting
`aroute.cpp`'s own B&B machinery (`branchAndBoundAssign`, the frozen-
baseline candidate-generation discipline, and critically the background-
overflow-seed correctness term -- a real bug aroute's own header documents
finding and fixing) onto this file's 2D-collapsed types. Candidates
generated via `routeNetSlowMSMV` under varied `wirelengthWeight` (reusing
Mechanism D's own field for the diversity role aroute's length-weight
sweep served) plus one new candidate generated by penalizing a net's own
most-contended touched edge before re-searching -- a technique aroute
never had access to. Gated behind a new, separate eligibility cap
(`NV10ROUTE_JOINT_MAX_CLUSTER_NETS`) so large clusters can keep the cheap
sequential path. 6 new self-tests, 88 total, 0 failures, clean
`-Wall -Wextra` build. Shell-level Stage 0 parity (toggle off) confirmed
byte-for-byte against nv9route's own default run.

**A real methodological finding before any routing result at all.** The
first checkpoint attempt showed 0 clusters ever reaching Mechanism H's own
code path -- `NV10ROUTE_JOINT_MAX_CLUSTER_NETS`'s own default (20) sat
below the existing `NV6ROUTE_MAX_CLUSTER_NETS` truncation (40), so every
real cluster was truncated to 40 nets before eligibility was checked, and
40 > 20 silently routed everything to the sequential fallback. Caught by
reading the trace output directly (it was in the OLD single-line summary
format, not Mechanism H's own per-cluster format) rather than trusting
the "0 accepted" exit summary at face value -- a reminder that "0
accepted" can mean "tested and failed" or "silently never ran," and these
look identical unless the trace is actually inspected.

**Checkpoint at 20-net scale** (adaptec1, reduced round budget,
`NV6ROUTE_MAX_CLUSTER_NETS=20` to align both caps so every cluster is
joint-eligible), `gr_verify`-legal:

    928 clusters, 0 accepted
    819 (88.3%) ran to TRUE exhaustion, found nothing better than baseline
    109 (11.7%) hit the node cap

Reproduces aroute's own historical 0/218 at nearly double its own tested
scale, now with MSMV-quality candidates.

**Checkpoint at the REAL 40-net scale** (the plan's own actual motivating
question -- clusters at their natural, untruncated size):

    928 clusters, 1 accepted (overflow 81196 -> 81194, -0.0025%)
    388 (41.8%) hit the node cap

The first NONZERO result across five generations that have tested this
exact family (`broute`, `nv6`, `nv7`, `nv8`, `nv9` all landed on exactly
0 acceptances, every single time). Genuinely real, not a measurement
artifact -- but tiny, smaller in relative terms than even the original
`oroute`/`qroute` lineage's own -0.019% best case, which came from a
completely different lever (cooperative eviction of context nets, not the
B&B search itself finding a better combination).

**Screen budget, same 40-net scale: the signal does not survive.**
adaptec1, full screen round budget, `gr_verify`-clean:

| config | real overflow |
|---|---|
| baseline (Mechanism H off) | 74968 |
| Mechanism H (joint B&B, 40-net scale) | 74968 |

Byte-for-byte identical -- same wirelength (3914397), vias (3336673), max
overflow (26), overflowed edges (22735). Whatever specific congestion
configuration let one cluster's B&B search find a real 2-unit improvement
at the reduced-round checkpoint doesn't exist in the same form once the
main loop has had the full round budget to converge further before
cluster rescue ever runs.

**Decision: Mechanism H does not improve on `gr/nv9route.cpp`'s own
baseline.** The checkpoint's nonzero signal is the most genuine "almost"
this whole cluster-rescue family has produced across five generations --
real, exhaustively-verified improvement exists in the B&B search space at
real cluster scale, just at a magnitude roughly two orders of magnitude
too small to survive contact with a realistic round budget. `gr/nv4route.cpp`
remains the committed best. `nv10route.cpp` stays in the repository,
frozen: the most thorough test this project has run of "a genuinely joint
solve helps" -- at real scale, with MSMV-quality candidates the original
aroute-qroute lineage never had access to -- still closes negative. Any
further attempt at this family would need a structurally different lever
entirely (the "cooperative eviction" idea oroute/qroute found, or
something else), not another refinement of joint candidate search itself,
which this generation has now tested about as thoroughly as this
codebase's own tooling allows.

### Part 47 addendum: cross-benchmark check confirms the negative, more completely than adaptec1 alone

User-requested follow-up beyond the plan's own gate. Same screen round
budget, same 40-net Mechanism H scale, baseline vs joint on all three
benchmarks, `gr_verify`-legal and `eval2008.pl`-cross-checked exactly:

| benchmark | baseline | Mechanism H | delta |
|---|---|---|---|
| adaptec1 | 74968 | 74968 | exact tie |
| newblue1 | 22472 | 22484 | +0.05% (regression) |
| bigblue1 | 187256 | 187256 | exact tie |

Unlike Part 45's own cross-benchmark check (which found real, benchmark-
dependent effects on nv8's mechanisms -- flat on one, a real regression on
another), the story here doesn't change across benchmarks: two of three
are byte-for-byte ties, the third a small regression. No benchmark shows
a win. This is the most complete negative this project has produced for
the "does a genuinely joint solve help" question -- real signal exists in
the search itself (the 40-net checkpoint's own 1-of-928 acceptance), it
just never translates into a real-budget, real-benchmark win anywhere.

## Part 48: ant colony optimization + marginal-cost pricing (`gr/ac1route.cpp`) -- both close negative, both mechanistically understood

User-requested pivot after Part 47's own comprehensive negative closed
the joint-B&B question: research cross-domain paradigms (traffic
engineering, structural engineering, ant colony optimization) for
genuinely different approaches, rather than another refinement of the
mechanisms already swept. ACO stood out as the one candidate that is
actually a different core solve method -- every router from `kroute`
through `nv10route` (roughly five dozen files) is stochastic-free, single-
shot deterministic search judged and kept-or-reverted; ACO is population-
based and stochastic, with a pheromone trail that learns across rounds
instead of one net committing to one guess. Marginal-cost pricing was
scoped alongside it per the user's own explicit request, motivated by
real congestion-pricing theory: this project's price has always tracked
usage/capacity ratio, a proxy for AVERAGE congestion cost; the Pigouvian-
tax result says the incentive-aligning price is the MARGINAL cost, the
derivative of that curve.

New species `ac` (ant colony), generation 1, forked from `nv10route.cpp`.
Two mechanisms, tested in isolation (not combined). Adaptec1 only.

*Corrected 2026-09-24 (see "Corrections to Parts 48-50" below): this
section originally said "checkpoint scale throughout:
`VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=3`" and that both mechanisms'
results were `eval2008.pl`-cross-checked. Stage 0 and Stage 1a actually
ran at 10/15; only Stage 1b ran at 10/3. The `eval2008.pl` cross-check
for Stages 0 and 1a was first run on 2026-09-24 and agrees with
`gr_verify` exactly. Stage 1b has no route file, so no legality check
exists for it.*

**Stage 0 (parity, both mechanisms off, 10/15):** byte-for-byte identical to
`nv10route` on the same fixture (81196 2D overflow / 95040 real overflow,
both sides). Confirms the fork introduces zero behavioral change when
untoggled.

**Stage 1a -- Mechanism 2 alone, marginal-cost pricing, ACO off:**

| config | real overflow |
|---|---|
| baseline (`nv10route`, 10/15) | 95040 |
| marginal-cost pricing (global scope, 10/15) | 111744 |

+17.6%, legal, `eval2008.pl`-cross-checked (95040/22/7167738 and
111744/26/7146052). A theoretically sharper, "more correct" congestion
signal made results worse, not better -- consistent with every prior
experiment in this project that made the reactive price signal more
aggressive (overflow-prediction terms, threshold tuning). The proposed
reason -- iterative negotiated-congestion search wants a smoother signal,
because a sharper derivative near the threshold amplifies round-to-round
oscillation -- is a hypothesis; oscillation was not measured.

**Stage 1b -- Mechanism 1 alone, ACO, marginal-cost off**
(`AC1ROUTE_ANTS=2`): the first colony round escalated ~60000 nets at
once. Every net's own best-of-K ant candidate (K=2, pheromone-biased,
noise-perturbed MSMV) was generated against a FROZEN round-start
baseline, then every winner committed SIMULTANEOUSLY -- round-level
supervision, deliberately matching Phase A's own trusted pattern instead
of Phase B's finer-grained one, on the reasoning that K-ants-per-net
already costs an order of magnitude more per net than a single
deterministic search, so re-serializing on top of that would be
prohibitively slow.

That reasoning was wrong in exactly the way CLAUDE.md's own escalation-
supervision rule already warns about. Round 0's overflow spiked to
**5,883,060** -- over 26x the 219,866 fast-tier baseline -- an almost
exact replay of Phase B's own inherited, already-documented 5.6M-overflow
historical incident (`nv10route.cpp`'s own header: 53,840 nets escalated
at once, overflow spiked to 5.6 million). The circuit breaker
(`circuitBreakerMultiple=5.0`) tripped correctly and rolled the whole
round back to the frozen baseline cleanly -- the safety net itself worked
exactly as designed, with zero net damage. But it exists to catch a
design choice that should not have been made in the first place: the
underlying ant-construction primitives (pheromone bias, noise-driven
diversity, best-of-K selection, evaporation/reinforcement) all passed
their own self-tests on the first try and are not implicated -- the
failure is specifically the round-level batch-commit architecture, the
same failure shape Phase B's own sequential per-net loop was built to
prevent and that this mechanism knowingly bypassed.

**Diagnosis independent of Mechanism 2.** Both mechanisms were tested in
isolation, never combined -- the ACO spike happened with marginal-cost
pricing OFF. The two findings are orthogonal: one is a pricing-model
question (does a sharper signal help), the other is a supervision-
architecture question (must escalation stay per-net). Neither result
implicates the other's mechanism.

**Decision: neither mechanism, as built, is a net win, and both closes
are mechanistically understood rather than merely negative.** Both point
at concrete, narrower follow-ups rather than a dead end: Mechanism 2's
failure mode (global-scope aggressive signal destabilizing iterative
search) suggests scoping it to a context that doesn't iterate the same
way -- cluster-rescue's own joint B&B, which generates a fixed candidate
set once per cluster and searches it near-exhaustively in one shot,
independently re-verified before any commit. Mechanism 1's failure mode
is a pure architecture bug, not a primitive failure -- reverting to
Phase B's own per-net sequential commit-or-revert structure, keeping the
already-proven ant-construction pieces unchanged, directly restores
CLAUDE.md's own escalation-supervision rule. Both follow-ups, plus a
genuinely new AIMD-style price update motivated by ACO's own need for
price stability (pheromone learning chases a moving target if price
whipsaws), are implemented as `gr/ac2route.cpp` (species `ac`, generation
2) -- see that file's own header and this document's next Part for the
result.

## Part 49: per-net-supervised ACO, AIMD pricing, and B&B-scoped marginal-cost pricing (`gr/ac2route.cpp`) -- the supervision fix works, two mechanisms close negative, the third's first measurement was invalid (corrected below)

Direct follow-up to Part 48. `ac1route.cpp` tested two ideas and both
taught something concrete but neither won; this generation (species `ac`,
generation 2, forked from `ac1route.cpp`) responds to both findings with
three targeted, narrower mechanisms, plus a user-asked question mid-run
("are there any other router paradigms that have promise with marginal-
cost pricing, or a different pricing model with ACO?") that motivated two
of the three.

**Mechanism 1: per-net-supervised ACO.** Reverts ac1route's own colony
round from round-level (frozen-baseline, simultaneous-commit) supervision
back to Phase B's original single-pass sequential structure -- K ants per
net, best-of-K, commit-or-revert immediately, exactly like Phase B's own
existing revert rule. Circuit breaker removed (unnecessary once
supervision is per-net). Reuses `constructAntRoute`/`trialEvaluateCandidate`
completely unchanged from ac1route.

**Mechanism 2: AIMD-style price update.** A genuinely different price
UPDATE RULE -- additive growth over capacity, multiplicative decay
(floored at 1.0) under it -- instead of another term inside the existing
`price *= exp(epsilon * term)` formula. Motivated by TCP's own AIMD
convergence theory and specifically by ACO's own need for price
stability (pheromone reinforcement chases a moving target if price
whipsaws).

**Mechanism 3: marginal-cost pricing scoped to joint B&B only.**
`jointClusterAssign` gains a `Prices::marginalCostScopeActive` flag (RAII-
guarded, restored on every return including early ones) and its own new
`negotiateWire` call for an ACCEPTED cluster's committed edges -- nv10's
own inherited `jointClusterAssign` never called `negotiateWire` at all,
so this mechanism needed that added before the scope flag had anything
real to gate.

All three self-tested (9 new tests beyond ac1route's own 361 checks, 382
total, all passing), including a decisive per-net-revert fixture (a
pheromone-biased ant forced onto an already-at-capacity path, confirmed
individually reverted), a real multi-net congestion-pileup proof that
per-net supervision's own monotonic-non-increase guarantee holds through
the REAL `route()` pipeline (not a hand-rolled mirror), and an
integration-level check that the marginal-cost scope guard actually
restores itself inside the real `jointClusterAssign`, not just in
isolation.

*Round budgets (added 2026-09-24; the original text did not state them):
Stage 0 ran at `VROUTE_FAST_ROUNDS=3 VROUTE_SLOW_ROUNDS=2` with cluster
rescue off; Stages 1-3 ran at `VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=3`,
not the 10/15 budget Part 48's own baseline used. Numbers in this Part
compare only with each other, not with Part 48's 95040.*

**Stage 0 -- shell-level parity.** All three toggles set to reproduce
ac1route's own exact behavior: byte-for-byte identical output, confirmed
directly (`gr_verify`-legal, `eval2008.pl`-cross-checked: 261614/76/6618144
on both sides at a small reduced-round fixture). The fork changed nothing
when configured to reproduce its parent.

**Stage 1 -- Mechanism 1 alone, no cluster rescue (isolating Phase A/B).**
The actual fix works at the level it targeted. Re-running the exact
scenario that spiked ac1route to 5,883,060 (26x its own fast-tier
baseline): slow round 0 now lands at 173,460 -- already BELOW every
fast-tier round's own overflow -- and keeps improving round over round
(168,128, then 166,256), no spike, no rollback needed. But at this
checkpoint, correctly-supervised ACO still doesn't beat the plain
sequential baseline it replaces:

| config | real overflow (no cluster rescue) |
|---|---|
| baseline (no ACO, no marginal-cost, no AIMD) | 146066 |
| Mechanism 1 alone (per-net-supervised ACO) | 180820 |

+23.8%, legal, cross-checked. K=2 noisy ants with best-of-2 selection
lose more often than they win against one deterministic MSMV search at
this scale -- per-net revert guarantees no net's own state gets WORSE,
not that the stochastic search finds routes as good as the deterministic
one finds by default.

**Stage 2 -- Mechanism 2 alone, no cluster rescue.**

| config | real overflow (no cluster rescue) |
|---|---|
| baseline (same as above) | 146066 |
| Mechanism 2 alone (AIMD pricing) | 156506 |

+7.1%, legal, cross-checked. A real, working alternative pricing rule
that simply routes worse on its own -- price stability for pheromone's
own future benefit (never tested combined with ACO here, since neither
piece showed enough signal alone to justify Stage 4) didn't translate
into better routing by itself.

**Stage 3 -- Mechanism 3 alone, cluster rescue ON (required for B&B to
run at all).**

Mechanism 3 alone (marginal-cost, B&B-scoped), cluster rescue on,
`VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=3`: **136086** real overflow,
legal, `eval2008.pl`-cross-checked.

*Corrected 2026-09-24 (see "Corrections to Parts 48-50" below): this
section originally compared 136086 against the 95040 "nv10/ac1route clean
baseline", reported "+43.2%, WORSE than ac1route's own globally-applied
version's +17.6% regression", and said this "falsifies the scoping
hypothesis outright." That comparison was invalid: 95040 was measured at
`VROUTE_SLOW_ROUNDS=15`, this run at `VROUTE_SLOW_ROUNDS=3`, so the gap
between them is mostly round budget, not Mechanism 3. The explanation
offered for the "regression" -- accepted-cluster price commits rippling
into later rounds, driven by a high cluster-rescue acceptance rate at this
checkpoint -- was never checked (the acceptance rate was not measured) and
is withdrawn. The valid comparison is a same-budget control with
Mechanism 3 off; its result is recorded below.*

**Decision (corrected): Mechanisms 1 and 2 close negative on valid,
same-budget comparisons; Mechanism 3's first measurement was invalid.**
No combination stage was run. Mechanism 1 solves the SAFETY problem (no
catastrophic spike) without solving the PERFORMANCE problem (stochastic
search still loses to deterministic search here); Mechanism 2 is a
genuinely different, working pricing rule that still routes worse.
`gr/nv4route.cpp` remains the committed best.

## Part 50: fractional-relaxation joint assignment (`gr/fw1route.cpp`) -- a small, real regression at equal footing against exact B&B

Direct response to a user question raised while reviewing this project's
own full retrospective: every mechanism that tried to make net routing
"cooperative" rather than independently-greedy has been either exact
discrete joint search (B&B, `broute` through `qroute`/`nv6route` through
`nv10route`'s own Mechanism H -- combinatorial, capped at 20-40 nets
across five generations, near-zero real acceptance at that cap) or
independent sequential search with cross-round bias (MSMV/ACO -- order-
dependent within a round, no genuine within-round coordination). Neither
tested the third option from combinatorial optimization: a continuous
relaxation solved to local convergence for a batch of contending nets,
then rounded to an integral choice -- the LP-relaxation-then-round
paradigm, mathematically continuous with CLAUDE.md's own Garg-Koenemann
citation, just applied to per-candidate probability within a small batch
instead of per-edge price across the whole design.

New species `fw` (fractional weights), generation 1, forked from
`ac2route.cpp`. `fractionalRelaxationAssign` is a drop-in alternative to
`branchAndBoundAssign`, dispatched from the identical `jointClusterAssign`
slot on the identical clusters/candidates `generateJointCandidatesForNet`
already produces -- a direct A/B test, not a new pipeline stage. Each
net's own candidate set starts with a uniform fractional weight; a fixed
number of cheap inner iterations compute the fractional usage every batch
edge would see under the current weights, price each candidate's own
local cost against that via the existing, plain `priceExponentTerm`
(deliberately not `marginalCostTerm` -- changing the pricing curve at the
same time as the assignment algorithm would make any result unreadable;
the original text here also cited `ac2route`'s Stage 3 "regression" as a
reason, which turned out to be an invalid comparison, see the corrections
below), and multiplicatively reweight
(`exp(-eta * cost)`, renormalized). After convergence (or the inner-
iteration cap), each net picks its own single committed candidate via a
seeded-deterministic weighted sample -- not argmax, since argmax alone
would just be independent per-net greedy picking again.

8 new self-tests, all passing (403 total checks, up from `ac2route`'s
382), including: hand-computed fractional-usage and reweighting-factor
checks (exact logistic-function values verified to 1e-9), a renormalization
invariant, seeded-sample determinism plus genuine sample diversity across
20 seeds, a decisive fixture where the new mechanism reaches the exact
same hand-verified true optimum `testJointBranchAndBoundFindsTrueOptimum`
already proves for exact B&B (via the fundamentally different continuous
mechanism, not exhaustive enumeration), the background-overflow-seed term
carrying over correctly, a `found=false` correctness check, and shell-
level parity through the dispatch wrapper at its default.

**A note on honesty about scope, disclosed up front rather than
discovered late:** the original framing for this generation's own
decisive self-test (item 5 in the plan) was "construct a fixture where
sequential order-dependent greedy is provably worse than joint
relaxation." That fixture could not be honestly constructed using this
project's own real, purely overflow-based cost function -- real-time
per-net price/usage feedback (the same reactive mechanism this whole
project has run since `hroute`) turns out to be surprisingly hard to fool
on small assignment instances, because any option that would actually
cause real overflow is already priced at a value no clean zero-overflow
alternative can lose to. The test that shipped instead proves a real,
still-decisive claim (reaches the known true optimum via a structurally
different mechanism) rather than the more ambitious, unconstructable one.

**Stage 0 -- shell-level parity.** `FW1ROUTE_ASSIGNMENT_METHOD` unset:
byte-for-byte identical to `ac2route`'s own output, `gr_verify`-legal
(193262/42/6815126 both sides, small reduced-round fixture).

**Stage 1 -- the headline comparison.** `FW1ROUTE_MAX_CLUSTER_NETS=20`
(matching `jointMaxClusterNets`' own default exactly, adaptec1,
`VROUTE_FAST_ROUNDS=10 VROUTE_SLOW_ROUNDS=3`, both sides otherwise at
`ac2route`'s own current defaults).

*Clarified 2026-09-24: those defaults include per-net-supervised ACO (a
measured regression -- 148962 with it on vs 136086 with it off at this same
budget, +9.5%) and B&B-scoped marginal-cost pricing (+0.11%, essentially no
effect, per the same-budget control in the corrections below), both active equally on both
arms. The A/B comparison below is fair, but it ran on a weaker
configuration than a clean baseline would have been.*

| config | real overflow |
|---|---|
| `bnb` (exact B&B, this project's own trusted baseline) | 148962 |
| `fractional` (this file's own new mechanism) | 151842 |

+1.93%, legal per `gr_verify` (0 malformed/out-of-bounds/disconnected/
incomplete/unrouted), cross-checked exactly against `eval2008.pl`
(148962/36/6978563 and 151842/34/6969270 respectively, both sides
matching to the unit). A small, real regression at equal batch size, not
the positive signal the plan's own Stage 2 gate required before sweeping
`FW1ROUTE_MAX_CLUSTER_NETS` to sizes B&B cannot structurally attempt.

**Decision: no further stage run, per this project's own consistent
gating discipline** (matching `nv9route`'s own header and `ac1route`/
`ac2route`'s own Stage-4 gates: no combination or further sweep without a
positive signal to chase first). The continuous relaxation's own
approximation error -- fractional usage feeding the inner iterations,
then a weighted-sample rounding step that can pick something other than
the converged argmax -- has no obvious reason to shrink at larger batch
sizes rather than compound further, so extending the sweep without first
clearing the equal-footing bar wasn't justified.

**The retrospective question this generation set out to answer gets a
partial, honest answer.** "Does B&B's own near-zero acceptance rate
across five generations reflect nothing left to find, or batches that
were always too small?" -- at EQUAL batch size, continuous relaxation
does not find something exact search misses; if anything it loses a
little to search's own exactness. Whether relaxation's real selling
point -- tolerating batches B&B cannot even attempt at all -- would ever
pay for this small per-batch quality gap remains genuinely untested; the
negative result at equal footing was reason enough to stop here rather
than commit to a further multi-hour sweep chasing it. `gr/nv4route.cpp`
remains this project's own best real result.

## Corrections to Parts 48-50 (2026-09-24)

A review of what was still unverified, checked against the exact commands
recorded in the session transcript, found these errors in Parts 48-50 and
the matching headers in `gr/ac1route.cpp`, `gr/ac2route.cpp`, and
`gr/fw1route.cpp`. All were comment/documentation errors; no router's
behavior changes. Each affected passage above now carries an inline
correction note.

1. **Invalid cross-budget comparison (Part 49, Stage 3).** Mechanism 3
   (B&B-scoped marginal-cost pricing) was run at `VROUTE_FAST_ROUNDS=10
   VROUTE_SLOW_ROUNDS=3` (136086) and compared against Part 48's 95040
   baseline, which ran at `VROUTE_SLOW_ROUNDS=15`. The reported "+43.2%,
   worse than the global version" measured round budget, not the
   mechanism. The explanation written for it (price commits rippling
   forward, driven by a high cluster-rescue acceptance rate) was never
   checked and is withdrawn. This error went into `ac2route.cpp`'s
   header, Part 49, `fw1route.cpp`'s own design rationale, and Part 50's
   design rationale.
2. **Wrong round budget stated (Part 48).** Part 48 and `ac1route.cpp`
   said everything ran at 10/3. Stage 0 and Stage 1a ran at 10/15; only
   Stage 1b ran at 10/3. The Stage 1a comparison itself was same-budget
   and stands.
3. **Cross-check claimed but not run (Part 48).** Part 48 and
   `ac1route.cpp` said results were `eval2008.pl`-cross-checked; they had
   not been. The check was run on 2026-09-24 against the still-present
   route files and agrees with `gr_verify` exactly: 95040/22/7167738
   (`nv10route` and `ac1route` parity, identical) and 111744/26/7146052
   (Stage 1a). Stage 1b never produced a route file, so it has no
   legality result at all; its numbers are from the router's trace.
4. **Unstated defaults (Part 50).** `fw1route`'s bnb-vs-fractional
   comparison ran with `ac2route`'s defaults, which include per-net ACO
   (a measured +9.5% regression at the same budget: 148962 on vs 136086
   off, both with cluster rescue and Mechanism 3 on) and Mechanism 3.
   The comparison is still fair (identical on both arms) but was not run
   on a clean baseline.

**Same-budget control for Mechanism 3.** `ac2route`, `VROUTE_FAST_ROUNDS=10
VROUTE_SLOW_ROUNDS=3 NV2ROUTE_VIA_ROUNDS=20`, cluster rescue on, ACO off,
global marginal cost off, AIMD off -- identical to Part 49's Stage 3 run
except `AC2ROUTE_MARGINAL_COST_SCOPE=0`. Run under `gr/run_tracked.sh`.

| config (10/3, cluster rescue on, ACO off, global marginal off, AIMD off) | real overflow | max | scored WL |
|---|---|---|---|
| Mechanism 3 off (control) | 135938 | 32 | 7022688 |
| Mechanism 3 on (Part 49's Stage 3) | 136086 | 34 | 7023298 |

Both `gr_verify`-legal (0 malformed/out-of-bounds/disconnected/incomplete/
unrouted) and `eval2008.pl`-cross-checked, matching `gr_verify` exactly
(Stage 3's max overflow and wirelength were not written into Part 49; they
were re-read from its route file on 2026-09-24). Control runtime 16819s,
peak RSS 986MB.

**Corrected result: B&B-scoped marginal-cost pricing is +0.11% --
essentially no effect**, not the "+43.2%" originally recorded. The whole
original gap was the round-budget mismatch. The scoping hypothesis is
neither confirmed nor refuted in any meaningful sense; the mechanism just
doesn't move the number at this budget.

## Part 51: search-box margin sweep on `gr/nv4route.cpp` -- margin 24 cuts total overflow 42.5% at full budget on all three benchmarks (new best)

A re-check of CLAUDE.md's "Bound maze search with a fixed margin" rule
in the current architecture. That rule's supporting sweep ran on the full
3D grid (it cites a call covering the whole 324x324x6 grid), before MSMV
and the 2D collapse. `nv4route` (the committed best) caps both
`routeSegment`'s and `routeStepMSMV`'s search box at `VROUTE_MAX_MARGIN`
tiles (default 6) and widens it only when no path exists at all -- never
when every path inside the box is congested. For comparison, NTHU-Route
2.0 (ISPD 2008 winner) starts its main-stage search box at 25 tiles on
adaptec1 and grows it by 1 tile per iteration (per its own benchmark
script, `luckyrantanplan/nthu-route`'s `bin/benchmark.sh`).

No code change: `VROUTE_MAX_MARGIN` is an existing env var. Screen budget
is this project's own 30/75/100 (`VROUTE_FAST_ROUNDS=30
VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`), adaptec1, `nv4route`
binary, every run under `gr/run_tracked.sh`.

| `VROUTE_MAX_MARGIN` | 2D overflow | real overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|
| 6 (default) | 63910 | 74984 | 24 | 7247067 | 1790s | 1021MB |
| 12 | 48386 | 59124 (-21.2%) | 24 | 7383636 (+1.9%) | 2292s | 878MB |
| 24 | 33978 | 44336 (-40.9%) | 20 | 7584937 (+4.7%) | 3349s | 989MB |
| 48 | 22410 | 31778 (-57.6%) | 18 | 7845406 (+8.3%) | 5724s | 972MB |
| 96 | 18642 | **28452** (-62.1%) | 16 | 8059455 (+11.2%) | 10238s | 1013MB |

All four `gr_verify`-legal (0 malformed/out-of-bounds/disconnected/
incomplete/unrouted) and `eval2008.pl`-cross-checked, matching `gr_verify`
exactly. Every doubling of the margin still helps: the absolute gain per
doubling shrinks slightly (15860, then 14788, then 12558), but the
relative gain per doubling grows (-21.2%, then -25.0%, then -28.3% against
the previous step) through 48. Margin 96 (added later, also verified both
ways: 28452 / 16 / 8059455) breaks that trend: the 48 -> 96 doubling
gains only -10.5%, at nearly double the runtime (10238s) -- returns from
a uniform fixed box are now flattening. Runtime through 48 grew much more
slowly than the margin (1790s, 2292s, 3349s, 5724s: 8x the margin, 3.2x
the runtime).

*Measurement note (2026-09-24):* `gr/run_tracked.sh` split
`/usr/bin/time`'s report from router stderr by matching lines containing
"real ", which also matched the router's own "real (wire+via) overflow"
summary and filed it in `<prefix>.time` instead of `<prefix>.stderr`. No
number was lost (every real-overflow figure in Parts 51-53 was read from
`.time` and independently re-scored with `gr_verify` and `eval2008.pl`),
but the split was wrong. Fixed by anchoring the patterns to `time -l`'s
line shape (leading whitespace and a number); runs launched before the
fix still have the line in `.time`. Wirelength grows as detours
lengthen (+8.3% at 48). Margin 6 reproduces Part 41's previously recorded 74984 at this
budget exactly (same settings run via the `nv3route` binary then), so the
baseline is sound.

**Full-budget confirmation, margin 24, all three benchmarks**
(`VROUTE_FAST_ROUNDS=60 VROUTE_SLOW_ROUNDS=150 NV2ROUTE_VIA_ROUNDS=200`,
the Part 41 committed operating point; `nv4route` binary; the margin-6
column is `nv4route`'s own committed Part 41 result at that same budget):

| benchmark | margin 6 (committed) | margin 24 | delta | 2D overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|---|---|
| adaptec1 | 72882 | **42052** | **-42.3%** | 32112 | 20 | 7602387 | 5405s | 994MB |
| newblue1 | 21418 | **13294** | **-37.9%** | 9742 | 20 | 6753642 | 2228s | 1072MB |
| bigblue1 | 183044 | **104246** | **-43.0%** | 99076 | 32 | 7913617 | 4448s | 1042MB |
| **sum** | **277344** | **159592** | **-42.5%** | | | | | |

All three `gr_verify`-legal (0 malformed/out-of-bounds/disconnected/
incomplete/unrouted) and `eval2008.pl`-cross-checked, matching `gr_verify`
exactly. The screen result survived the full budget -- unlike Parts 33,
37 and 39 -- and it is unanimous and large on every benchmark.

**Longer budget, margin 24** (queue step 1: does "more rounds don't help"
survive wide boxes?). adaptec1, `VROUTE_SLOW_ROUNDS=300` (double the full
budget's 150; fast 60, via 200): **41750** real overflow vs 42052 at 150 --
only -0.7% for about 60% more runtime (8698s vs 5405s). `gr_verify`-legal,
`eval2008.pl`-cross-checked (41750 / 18 / 7604119). The trace is a real
plateau (2D overflow 33978 at slow round 74, 32112 at 149, 31678 at 224),
and `kSlowPatience=5` ended the run at slow round 295 of 300 -- so that
compile-time limit does bind on long runs (Part 55). Conclusion: at a
fixed margin, more rounds still don't help, even with wide boxes. The
growth schemes in Part 53 kept improving to the round cap only because
their boxes kept widening -- width, not round count, is the lever.

**Decision: `nv4route` with `VROUTE_MAX_MARGIN=24` is this project's new
best real result** (sum 159592, down from 277344), the largest single
improvement since `vroute`'s MSMV and `fr1route`'s 2D collapse. No code
changed: the gain came entirely from undoing a search-box cap inherited,
untested, from `mroute.cpp` (see CLAUDE.md's revised rule). Margins 48 and
96 and growth schemes that widen boxes only where needed are the next
tests.

## Part 52: a known-good reference router, run and scored here -- zero overflow on adaptec1 is reachable, and this project's scoring is correct

CLAUDE.md planned a known-good baseline (a FastRoute-derived router at
the OpenROAD stage) to compare against; it was never run in ~70 routers'
worth of work. Every claim that "contest routers reach zero on these
benchmarks" came from papers, never from a run on this machine scored by
this project's own tools -- which left open whether the remaining gap was
algorithmic or an input/capacity/scoring interpretation problem (CLAUDE.md:
"the verifier's dominant yield is bad input data").

**The reference.** NTHU-Route 2.0 (ISPD 2008 Global Routing Contest, 1st
place), as the modern C++ port `luckyrantanplan/nthu-route` on GitHub
(GPL-3.0; port by Florian Prud'homme of the original by Yen-Jung Chang,
Yu-Ting Lee, Tsung-Hsien Lee, Jhih-Rong Gao and Pei-Ci Wu, advised by
Ting-Chi Wang; commit `ce22279`, 2026-02-16). It reads the ISPD'08 `.gr`
format directly. Cloned to `~/Code/nthu-route`, deliberately outside this
repository (license, and this project's own no-dependency rule for its own
code). Built from source on Apple Silicon; Boost 1.92.0 headers from
Homebrew. Its bundled adaptec1 input is byte-identical (SHA-256) to this
project's `gr/bench` copy.

**The port did not run as published; three local fixes were needed, none
touching routing logic:**
1. Build fix for Clang: a friend declaration named a nested class before
   it was declared (GCC accepts this, Clang does not); forward-declared
   the class.
2. Real memory bug: `RoutingRegion`'s per-layer wire-width, wire-spacing
   and via-spacing vectors were brace-initialized with the layer count,
   which for `std::vector<int>` creates ONE element holding that count
   instead of that many elements. Parsing any multi-layer `.gr` file
   writes past the end (it segfaulted immediately here). Introduced by the
   port's own "refactoring constructors" commit; fixed with parenthesized
   construction.
3. AddressSanitizer-reported container overflow in the two-pin-list
   construction: a reference to `path.back()` was held across a call that
   clears and refills `path`. Benign in practice (the refilled element is
   the same coordinate), but undefined behavior; fixed by copying the value.

After fix 3, a full adaptec1 run under AddressSanitizer reported no
further memory errors.

**Result, adaptec1** (authors' own adaptec1 settings from the port's
`bin/benchmark.sh`: main stage 150 iterations, initial box 25, box grows
by 1 per iteration, overflow threshold 200, refinement 20 iterations with
initial box 10 growing by 15, monotonic routing off). Run under
AddressSanitizer, so its runtime is inflated:

| router | real overflow | max | wirelength | vias | scored WL | runtime |
|---|---|---|---|---|---|---|
| NTHU-Route 2.0 (port, patched) | **0** | **0** | 3585305 | 1774596 | 5359901 | 760s (ASan build) |
| `nv4route` (committed best, full budget, Part 41) | 72882 | -- | -- | -- | -- | -- |
| `nv4route`, margin 6, 30/75/100 screen (Part 51) | 74984 | 24 | -- | -- | 7247067 | 1790s |
| `nv4route`, margin 24, 30/75/100 screen (Part 51) | 44336 | 20 | -- | -- | 7584937 | 3349s |

**This project's own `gr_verify` scores the reference output PASS** -- 0
total overflow, 0 max overflow, 0 overflowed edges, 0 malformed/out-of-
bounds/disconnected/incomplete/unrouted -- the first `gr_verify` PASS on a
real benchmark recorded anywhere in this file. `eval2008.pl` agrees exactly
(0 / 0 / 5359901).

What this settles:
- **The input parsing, capacity model and scoring in this project are
  correct.** A known-good router's solution scores zero under this
  project's own tools. The gap is algorithmic, not an interpretation
  problem.
- **Zero is reachable on adaptec1 on this machine**, in about 12.5 minutes
  even with AddressSanitizer's slowdown -- against `nv4route`'s multi-hour
  full budget ending at 72882.
- NTHU-Route's own trace shows its 2D stage reaching 2D overflow 0 before
  layer assignment, and its layer assignment preserving zero. The gap is
  in the 2D stage first; `nv4route`'s 2D overflow at the screen budget is
  63910.
- Minor: NTHU-Route routes 176715 nets, this project's routers 176716.
  `gr_verify` reports no unrouted nets for the reference, so the extra
  net this project routes is not one the contest rules require (worth a
  look, not a correctness issue).

**bigblue1** (authors' settings: initial box 10, grows by 1, overflow
threshold 340, monotonic off; release build): **0 total overflow, 0 max,
`gr_verify` PASS**, `eval2008.pl` agrees exactly (0 / 0 / 5657290;
wirelength 3765813 + vias 1891477). 268s, peak RSS 1029MB. `nv4route`'s
committed full-budget bigblue1 result is 183044.

**newblue1** (authors' settings: initial box 12, grows by 1, overflow
threshold 4, monotonic ON; release build): the port's own trace shows its
2D stage finishing at 2D overflow 18, but its layer assignment then
produced 859138 real overflow (max 2) across 429569 edges -- every
overflowed edge over by exactly 2, one net's width+spacing. Tallying the
output by direction and layer shows why: horizontal wires on vertical-only
layers (38474 H segments on layer 2, 32292 on layer 4, 10934 on layer 6)
and vertical wires on horizontal-only layers (49421 V on layer 1, 34838 on
layer 3, 12386 on layer 5) -- every one of which lands on a zero-capacity
edge. The port's own log reports the same 3D overflow, so this is a real
failure of the port's layer assignment on this benchmark, not a scoring
disagreement (`gr_verify` and `eval2008.pl` agree: 859138 / 2 / 4133320).
It is not a routing-quality result and says nothing about what the
original NTHU-Route 2.0 achieved on newblue1. A diagnostic re-run with
monotonic routing off (newblue1's settings also differ in initial box
size and overflow threshold, but monotonic routing is the one on/off mode
switch the two successful runs never enabled) produced identical output --
same 2D overflow (18), same 859138 real overflow, same wirelength -- so
monotonic routing is not the trigger. One observable difference remains:
newblue1 is the only benchmark of the three where the port's 2D stage
ended above zero, and its layer assignment may mishandle leftover 2D
overflow. That is an untested hypothesis about third-party code and was
not pursued; the net result is that this port provides no usable newblue1
reference. (For context, `fr1route.cpp`'s own summary of the contest
record names adaptec1 and bigblue1 among the benchmarks solved to zero and
does not mention newblue1 either way; whether contest routers reached
zero on newblue1 has not been checked.)

## Part 53: `gr/nv11route.cpp` -- search-box growth (per-round and per-net) -- in progress

Forked from `nv4route.cpp` (the committed best; the nv5-nv10 mechanisms,
all closed negative, are deliberately not carried). Each net carries its
own search-box margin, passed to both slow-tier search entry points in
place of `VROUTE_MAX_MARGIN`. `NV11ROUTE_BOX_MODE`:
- `fixed` (default): `nv4route` exactly.
- `round`: NTHU-Route 2.0's main-stage scheme, confirmed from its source
  (the port's `Construct_2d_tree.cpp`): every net's margin in slow round
  k is `INIT + k*GROW`. (NTHU-Route also only maze-routes a two-pin
  connection when its cheap monotonic route overflows -- `nv4route`'s
  existing fast-tier/escalation split plays the same role here.)
- `pernet`: a net starts at `INIT` and widens by `GROW` after each round in
  which it is still overflowed; nets that resolve stop growing.
Both clamp at `NV11ROUTE_BOX_CAP`. Invalid settings fail loudly.

Self-tests (308 checks, all passing), including the decisive one on the
real `routeStepMSMV`: on a 40x40 grid with a 1e6-priced wall across the
direct route and one cheap gap 15 rows away, margin 6 is forced through
the wall (the gap is outside its box, and a path exists, so the no-path
doubling never fires), margin 16 detours through the gap, and no override
behaves exactly like margin 6. Plus hand-verified growth arithmetic (cap
clamp, int-overflow guard), the Phase B helpers for all three modes, and
bad-input rejection for the settings parser.

**Stage 0 -- parity.** `NV11ROUTE_BOX_MODE` unset, adaptec1 at
`VROUTE_FAST_ROUNDS=3 VROUTE_SLOW_ROUNDS=2 NV2ROUTE_VIA_ROUNDS=3`: route
file byte-identical to `nv4route`'s (164174 real overflow both).

**Stage 1 -- screens** (adaptec1, 30/75/100), against Part 51's fixed
margins 6 / 12 / 24 / 48 (74984 / 59124 / 44336 / 31778 real overflow):

| config | margin over the run | 2D overflow | real overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|---|
| `round` 24 / +1 / cap 160 | 24 -> 98 (mean per trace) | 21112 | 30428 | 18 | 7838364 | 5781s | 993MB |
| `pernet` 6 / +6 / cap 96 | mean 6 -> 27 (round 4) -> 82 (round 14) -> 93-95 (round 24 on) | 18400 | 27394 | 16 | 7914430 | 7753s | 1108MB |
| `round` 48 / +2 / cap 200 | 48 -> 96 (round 24) -> 146 (round 49) -> 196 (round 74) | 18022 | **27094** | 16 | 8004059 | 9214s | 1096MB |

`gr_verify`-legal, `eval2008.pl`-cross-checked (matching exactly). The
trace confirms the wiring: mean margin 24 in slow round 0, 48 at round
24, 73 at round 49, 98 at round 74, none at cap. Slightly better than
fixed margin 48 (31778) at nearly the same runtime (5724s).

`pernet` 6 / +6 / cap 96: `gr_verify`-legal, `eval2008.pl`-cross-checked
(27394 / 16 / 7914430). Best screen so far, but the trace shows why and at
what cost: margins climb fast, and from slow round 24 on nearly every net
still being rerouted sits at the cap (26746 of 28947 at round 24, 22901 of
22973 at round 74) -- in practice "fixed 96 for the persistent nets." It
is also the slowest screen (7753s vs `round`'s 5781s).

`round` 48 / +2 / cap 200: `gr_verify`-legal, `eval2008.pl`-cross-checked
(27094 / 16 / 8004059). Best screen so far, narrowly ahead of `pernet`
(27394) but slower (9214s vs 7753s); by the last rounds its boxes average
196 tiles on a 324-tile-wide grid. Fixed margin 96 (Part 51) scored 28452
in 10238s -- both growth schemes beat it on overflow, and `pernet` on
runtime too.

## Part 54: `gr/nv12route.cpp` -- evidence-driven progressive box growth (`pressure` mode) -- sound, but on the same trade-off curve as blind growth

The user asked for a search perimeter that grows progressively rather
than in big jumps, and chose the evidence-driven version: widen a net's
box by a small step only when its own search was actually limited by it.
`nv11route`'s schemes grow blindly (`round` widens everyone every round;
`pernet` +6 drove nearly every hard net to the 96 cap by round 24).

**The signal.** For each node the MSMV search expands on an extendable
box edge (grid-border edges can't grow and don't count), record a lower
bound on any route leaving the box there: its exact cost so far plus the
cheapest possible edge cost (`1 + amount`) times its Manhattan distance to
the nearest sink. If no such bound is below the found route's cost, no
route outside the box can be cheaper. The proof (in `multiSourceDijkstra`)
rests on A* with a consistent heuristic popping nodes in nondecreasing
f = g + h, and on every edge costing at least `1 + amount` -- true because
prices start at 1 and never decrease in this lineage and history never
drops below 1; `main()` rejects pressure mode with history settings that
could break that. A first version used plain pop order ("an edge node was
expanded before the sink"); worked arithmetic showed that would flag most
long nets as box-limited even when uncongested (the heuristic counts 1
per tile while real edges cost at least 2), so the tighter bound replaced
it before any run.

**`pressure` mode** (inherited `NV11ROUTE_BOX_MODE=pressure`, with nv11's
INIT/GROW/CAP): start at INIT; after each supervised reroute, grow by GROW
only if the net is still overflowed AND its last search was box-limited.

**Self-tests** (323 checks, all passing): binding detected on a walled
fixture and not detected on an open one; a box covering the whole grid
never reports binding even when forced through a wall; the helper grows
only when overflowed AND binding and clamps at CAP; and the soundness
property on the real search -- 60 deterministic fixtures shaped like real
router state (floor prices everywhere, 1-3 hot spots near the pins): in
all 38 cases reporting "not binding", a whole-grid search found exactly
the same cost; 22 reported binding, 4 of which really were cheaper
outside. (A first fixture priced every edge 1..20 -- nothing like real
router state -- and there the floor-based bound was too loose for any
binding case to be genuinely limiting; the fixture was made realistic
rather than the assertion weakened.) So the signal never widens a box
that provably can't help, but it is conservative: most flagged searches
turn out not to be limited.

**Stage 0 -- parity.** adaptec1 at 3/2/3: `nv12route` vs `nv11route`
route files byte-identical in the default (fixed) mode and in `pernet`
6 / +6 / 96.

**Stage 1 -- screens** (adaptec1, 30/75/100), with Parts 51/53 for
comparison:

| config | real overflow | 2D overflow | max | scored WL | runtime |
|---|---|---|---|---|---|
| fixed 24 (Part 51) | 44336 | 33978 | 20 | 7584937 | 3349s |
| fixed 48 (Part 51) | 31778 | 22410 | 18 | 7845406 | 5724s |
| `round` 24 / +1 (Part 53) | 30428 | 21112 | 18 | 7838364 | 5781s |
| `pernet` 6 / +6 (Part 53) | 27394 | 18400 | 16 | 7914430 | 7753s |
| `round` 48 / +2 (Part 53) | 27094 | 18022 | 16 | 8004059 | 9214s |
| **`pressure` 6 / +1 / 96** | **31916** | 22622 | 18 | 7650573 | **4722s** |
| **`pressure` 12 / +2 / 96** | **29422** | 20088 | 18 | 7820802 | **6499s** |

`pressure` 6 / +1: `gr_verify`-legal, `eval2008.pl`-cross-checked (31916 /
18 / 7650573), peak RSS 1054MB. It matches fixed 48's overflow in 18% less
time and with lower wirelength, but trails the best screens. The trace
shows why: its boxes grew slowly (mean margin 6 -> 13 at slow round 14 ->
18 at 24 -> 29 at 49 -> 38 at 74, none at the cap) and overflow was still
falling at the end (2D 28678 -> 22622 from round 49 to 74) -- the +1 step,
not the evidence gate, is what limits it. The gate filters more as the run
goes: 47254 of 64163 searches (74%) were box-limited in round 0, 8123 of
25905 (31%) in round 74.

`pressure` 12 / +2: `gr_verify`-legal, `eval2008.pl`-cross-checked (29422 /
18 / 7820802), peak RSS 1104MB. Mean margin 12 -> 26 (round 14) -> 34
(24) -> 48 (49) -> 49 (74). By round 49, 7312 nets sat at the 96 cap and
growth had nearly stopped (276 nets grew in round 74) -- the cap, not the
evidence gate, became the limit; `round` 48 / +2 (cap 200) is the scheme
that pulled ahead.

**Stage 1 verdict.** Both `pressure` screens sit on the same overflow-vs-
runtime trade-off curve as the blind schemes rather than below it: 31916 at
4722s, 29422 at 6499s, against `round` 24 / +1 30428 at 5781s, `pernet`
27394 at 7753s, `round` 48 / +2 27094 at 9214s. `pressure` 6 / +1 does
match fixed 48's overflow in 18% less time, but the plan's success bar --
match `pernet`'s overflow at clearly lower runtime, or beat it -- is not
met (12 / +2 is 7% worse at 16% less time). The evidence gate is sound and
does filter (31-74% of searches box-limited, falling over the run), yet
filtering out non-binding nets saves less than hoped. A plausible but
unmeasured reason: the searches it skips widening may be the ones where
extra breadth was cheap anyway (a non-binding search's A* frontier never
reaches the box edge, so a wider box costs it little). Per the plan, no
full-budget Stage 2 for `pressure` on this evidence.

## Part 55: provenance audit of `nv4route`'s inherited defaults

Prompted by Part 51: the 6-tile search-box margin was a constant set once
in `mroute.cpp` for speed, inherited untested by every later router, and
turned out to be the biggest hidden limiter in the project. This audit
asks the same question of every other default in `nv4route` (the base of
`nv11route`/`nv12route`): when was it set, on which architecture, and has
it been re-tested since. Architecture milestones: MSMV maze growth
(`vroute`, 2026-09-07, Part 9); 2D collapse (`fr1route`, 2026-09-12, Part
21); negotiated via pricing (`nv1route`, 2026-09-15, Part 29). And the
new confound: every sweep before Part 51 ran inside the 6-tile box.
Sources: `git log -S` for the introducing commit; every mention in this
file for whether it was actually varied.

| default (where) | value | set in | architecture then | varied since? | re-test priority |
|---|---|---|---|---|---|
| search-box margin `VROUTE_MAX_MARGIN` | 6 | `mroute`, 09-05 | 3D, fixed topology | Part 51 | done: 24 at full budget -42.5% |
| history step `VROUTE_HIST_STEP` | 0.05 | `iroute`, 09-06 (9-config sweep, Parts 1-5) | 3D, fixed topology, pre-MSMV | never | **high**: history cost is what pushes nets onto detours, and its right strength depends on whether detours are reachable -- tuned when they mostly weren't |
| price step `VROUTE_EPS` | 0.02 | `nproute`, 09-04 | pattern router | once, Part 33 (2D era, 6-tile box): mixed, negligible | **high**: same reasoning, and its only sweep was inside the narrow box |
| cleanup pass `VROUTE_CLEANUP_THRESHOLD` / `_PASSES` | 0.5 / 30 | pass from `nproute`; these variables added in `vroute`, 09-07 | MSMV, 3D | never mentioned in this file | medium |
| slow-tier patience `kSlowPatience` | 5 (compile-time) | `hroute`, 09-05 | 3D, fixed topology | never | medium: not binding in the wide-box screens (all hit the round cap), but decides when longer runs stop -- step 1's long run will show it |
| fast-tier patience `kFastPatience` | 20 (compile-time) | `hroute`, 09-05 | 3D, fixed topology | never (Part 33 observed it stopping the fast tier at round 21) | medium |
| via rounds `NV2ROUTE_VIA_ROUNDS` | **60 in the binary, 200 in every committed result** | `nv1route`, 09-15 | 2D + negotiated vias | used as a budget knob, not swept | fix the mismatch: an unconfigured run is weaker than the recorded best |
| via patience `NV2ROUTE_VIA_PATIENCE` | 20 | `nv1route`, 09-15 | same | never | low |
| via weight `NV2ROUTE_VIA_WEIGHT` | 1.0 | `fr6route`, 09-13 | 2D + one-pass layer DP | Part 27 -- before negotiated via pricing existed | low-medium |
| threshold / power `VROUTE_THRESHOLD` / `_POWER` | 0.05 / 1.5 | retuned `nv4route` | 2D + MSMV | Parts 33, 41 (6-tile box) | medium |
| exact-Steiner pin cap `FR4ROUTE_EXACT_MAX_PINS` | 8 | `fr4route`, 09-13 | 2D | Part 32 | low: shapes fast-tier trees, which never use the search box |
| no-path retry (6 doublings), `routeSegment` span/8 | -- | `mroute`, 09-05 | 3D | never | low: only fire when no path exists, or on the non-default fixed-topology path |
| history factor `VROUTE_HIST_FACTOR` | 1.1 | `iroute`, 09-05 | 3D | Part 1 | none: unused (default history mode is additive) |

**Reading.** Two defaults stand out the way the margin did: the history
step and the price step. Both control how hard the negotiation pushes a
net off a congested edge, both were tuned when a net pushed off an edge
had nowhere far to go, and neither has been varied since boxes were
widened. They are the natural next sweep once the box-growth work
settles. One trap is not a tuning question at all: `NV2ROUTE_VIA_ROUNDS`
defaults to 60 in the binary while every committed result used 200, so
running any router without that variable silently under-performs the
recorded best.

## Part 56: cluster rescue / joint B&B re-tested with wide boxes -- still accepts nothing

Queue step 2. Parts 44-47's cluster-rescue and joint branch-and-bound
mechanisms (`nv6route`-`nv10route`) accepted essentially nothing at real
scale. Part 55's reading suggested a cause: their candidate routes came
from `routeNetSlowMSMV` inside the same 6-tile box, so the candidates may
have been near-duplicates. Re-tested with the box widened.

`nv10route`, adaptec1, 30/75/100 screen, `VROUTE_MAX_MARGIN=24`, its own
defaults (cluster rescue + joint B&B on) vs the same with
`NV6ROUTE_CLUSTER_RESCUE=0`:

| config | 2D overflow | real overflow | max | scored WL | runtime |
|---|---|---|---|---|---|
| rescue on (default) | 33766 | 44264 | 20 | 7601615 | 6958s |
| rescue off | 33766 | 44264 | 20 | 7601615 | 3546s |
| (`nv4route` margin 24, Part 51) | 33978 | 44336 | 20 | 7584937 | 3349s |

Both `nv10route` runs `gr_verify`-legal and `eval2008.pl`-cross-checked;
their route files are **byte-identical**. The trace: 1443 clusters
attempted over three meta-rounds, **0 accepted**, overflow unchanged at
33766 throughout. The rescue stage contributes nothing and nearly doubles
runtime.

**The narrow-box explanation for B&B's near-zero acceptance is refuted, at
least at margin 24.** Whatever limits these mechanisms, it isn't the 6-tile
box. (`nv10route`'s small edge over `nv4route`, 44264 vs 44336, -0.16%, comes
from its other inherited nv5-nv9 defaults, not the rescue stage -- not
pursued.) No further rescue/B&B work is planned.

## Part 57: full-budget confirmation of the two best growth schemes

The two best adaptec1 screens (Parts 53-54) -- `round` 48 / +2 / cap 200
(27094) and `pernet` 6 / +6 / cap 96 (27394), both ~38% below fixed margin
24's 44336 at the same 30/75/100 screen -- have only been measured on one
benchmark at a reduced budget. This Part runs both at the full committed
budget (`VROUTE_FAST_ROUNDS=60 VROUTE_SLOW_ROUNDS=150
NV2ROUTE_VIA_ROUNDS=200`) on all three benchmarks, `nv11route`,
`run_tracked.sh`, `VROUTE_TRACE=1`, against the current best, `nv4route`
margin 24 at the same budget: 42052 / 13294 / 104246 (sum 159592, Part 51).

Batch 1: `pernet` 6 / +6 / 96 on adaptec1, newblue1, bigblue1.
Batch 2: `round` 48 / +2 / 200 on all three, each launched as a batch-1 slot
frees.

| benchmark | margin 24 (current best) | `pernet` 6/+6/96 | delta | `round` 48/+2/200 | delta |
|---|---|---|---|---|---|
| adaptec1 | 42052 | **26480** (max 16, scored WL 7931976, 11042s, 1130MB) | **-37.0%** | 25848 (max 18, scored WL 8012799, 12653s, 1104MB) | -38.5% |
| newblue1 | 13294 | **12536** (max 18, scored WL 6853028, 12103s, 1061MB) | **-5.7%** | 12524 (max 18, scored WL 6938408, 21313s, 1136MB) | -5.8% |
| bigblue1 | 104246 | **74062** (max 28, scored WL 8044846, 7361s, 1019MB) | **-29.0%** | 73714 (max 28, scored WL 8071598, 7737s, 1148MB) | -29.3% |

Every entry `gr_verify`-legal and `eval2008.pl`-cross-checked before it is
entered here.

**`pernet` 6 / +6 / 96, all three benchmarks: 26480 + 12536 + 74062 =
113078, down from margin 24's 159592 (-29.1%), and down from the committed
margin-6 `nv4route`'s 277344 (-59.2%).** Better on every benchmark, verified
both ways -- it clears this project's promotion bar. **Decision:
`nv11route` with `NV11ROUTE_BOX_MODE=pernet NV11ROUTE_BOX_INIT=6
NV11ROUTE_BOX_GROW=6 NV11ROUTE_BOX_CAP=96` at the 60/150/200 budget is the
new best real result.** (Superseded below by `round` 48 / +2 / 200.)

The gain is uneven: -37.0% on adaptec1, -29.0% on bigblue1, only -5.7% on
newblue1. newblue1's trace shows the likely reason: from slow round 24 on,
nearly every net still being rerouted sits at the 96-tile cap (10052 of
11216 at round 24, 7282 of 7297 at round 149) and 2D overflow barely moves
(9460 at round 74, 9018 at round 149). The cap is the binding limit there --
the planned cap-raise step targets exactly this. For scale: NTHU-Route's own
2D stage ended at 2D overflow 18 on newblue1 (Part 52; its layer assignment
then failed), against 9018 here.

**`round` 48 / +2 / 200 on bigblue1: 73714** (`gr_verify`-legal,
`eval2008.pl`-cross-checked), marginally better than `pernet` (-0.5%). Its
trace is the more important result: bigblue1's grid is 227 tiles wide, so
from slow round ~76 the box (200-tile cap) covers nearly all of it, yet 2D
overflow barely moves after that (69662 at slow round 75, 68606 at 149).
**On bigblue1, search breadth is exhausted**: the remaining overflow is not
a matter of how far nets may look. That points at the pricing /
negotiation model (Part 58) rather than further box growth. At 150 slow rounds its
margin reaches the 200 cap at slow round 76 and stays there -- the screen
(75 slow rounds) never reached the cap, so its second half is untested
territory.

**`round` 48 / +2 / 200 on newblue1: 12524** (`gr_verify`-legal,
`eval2008.pl`-cross-checked: 12524 / max 18 / scored WL 6938408 both ways),
-0.1% vs `pernet`, at 1.76x its runtime (21313s vs 12103s). The trace
answers the planned cap-raise question for newblue1 directly: this run's
boxes pass `pernet`'s 96 cap at slow round 24 and sit at 200 for every
rerouted net from round ~100 on (6949 of 6949 at round 149), yet 2D overflow
ends at 8990 vs `pernet`'s 9018 (-0.3%). Slow-round 2D overflow: 34184 (0),
11962 (24), 10218 (49), 9602 (74), 9302 (99), 9058 (124), 8990 (149).
**Raising the cap from 96 to 200 buys newblue1 essentially nothing -- search
breadth is exhausted there too.**

**`round` 48 / +2 / 200, all three benchmarks: 25848 + 12524 + 73714 =
112086**, vs `pernet`'s 113078 (-0.9%), better on all three (-2.4%, -0.1%,
-0.5%), at more runtime (41703s vs 30506s summed, +37%). **Decision:
`nv11route` with `NV11ROUTE_BOX_MODE=round NV11ROUTE_BOX_INIT=48
NV11ROUTE_BOX_GROW=2 NV11ROUTE_BOX_CAP=200` at the 60/150/200 budget is the
new best real result (112086; -29.8% vs margin 24's 159592, -59.6% vs the
committed margin-6 `nv4route`'s 277344).** `pernet` 6/+6/96 stays the
recommended setting when runtime matters: within 0.9% at 73% of the time.

What Part 57 settles: on bigblue1 and newblue1, boxes covering most of the
grid leave 2D overflow flat for the last ~75 slow rounds, and on adaptec1
the 96 -> 200 cap difference is worth 2.4%. Box growth has delivered its
gain (277344 -> 112086); the remaining overflow is not a search-breadth
problem. The next lever is the pricing / negotiation model -- see Part 58
for how NTHU-Route 2.0, which reaches zero on adaptec1 and bigblue1 here,
differs.

## Part 58: side-by-side -- NTHU-Route 2.0 (zero overflow here) vs `nv4route` (this project's best)

NTHU-Route 2.0 reaches zero overflow on adaptec1 and bigblue1 on this
machine (Part 52). Its source (the patched `luckyrantanplan/nthu-route`
port in `~/Code/nthu-route`, ~5300 lines) was read directly; every item
below is from the code, not from the port's own `docs/algorithms.md`,
which gets at least one thing wrong (it places post-processing after
layer assignment; the code does all 2D work, refinement included, first).
The `nv4route` side was re-checked against `gr/nv4route.cpp`.

| aspect | NTHU-Route 2.0 | `nv4route` |
|---|---|---|
| **edge cost** | recomputed from *current* demand on every insert/remove: `cost = WL + history * cong^5`, `cong = (demand+1) / (cap * (1 - (history-1) / (iter * (1.5 + 3*factor))))` -- falls the moment nets leave an edge | `hist * (1 + lambda * amount)` with `lambda <- lambda * exp(eps * term)` on every commit -- **never decreases**; only the separate LIFO history term relaxes |
| **history** | per-edge count of iterations overflowed (starts at 1); multiplies the congestion term AND shrinks effective capacity ("overflow prediction") | LIFO-relaxable additive term (kroute); prediction tried once (`nv8route` Mechanism E, on top of the monotone lambda, unvalidated constants -- regressed) |
| **wirelength weight** | scheduled per iteration: `factor = 1 - exp(-5 * exp(-0.1 * iter))`, decays from ~0.99 toward 0; bend penalty `int(4 * factor)` | fixed 1 (the Gompertz schedule was tried in the `nv8` lineage, not in `nv4`) |
| **initial routing** | FLUTE trees; a pre-routing demand estimate (each 2-pin segment's bounding-box frame, +1); nets routed smallest bounding box first, each removing its own estimate, running **edge shifting** (move Steiner points to cheaper spots), then the cheaper of two L-shapes | exact RSMT (<= 8 pins) / cheap Steinerization; L/Z pattern menu; no demand estimate, no edge shifting |
| **what gets rerouted** | only **2-pin segments crossing an overflowed edge**; the rest of the net's tree stays | the **whole net**, regrown from scratch by MSMV, for every escalated net still overflowed |
| **acceptance** | the new route is **always kept** (no revert) | **reverted** if local overflow got worse (CLAUDE.md's supervision rule) |
| **order** | overflowed edges bucketed into 10 congestion bands, **least congested band first**; around each edge a region grows until its border's congestion falls below the band floor (+ iter/10) | net order, every escalated net each round |
| **search** | cheap **monotonic route first** (in the segment's bounding box); maze (multi-source multi-sink at the segment level; the net's own wires cost 0) only if that fails or overflows | MSMV maze for every escalated net |
| **box** | segment bounding box + `BOXSIZE_INC`: starts 25 (adaptec1) / 10 (bigblue1), **+1 per iteration** | `VROUTE_MAX_MARGIN` (6 in the committed best; Parts 51-54 test wider / growing) |
| **topology** | 2-pin decomposition **rebuilt from the actually-routed tree every iteration** | fixed Steiner topology for fast-tier nets; MSMV nets are topology-free |
| **stages** | main stage until overflow <= threshold (200 adaptec1, 340 bigblue1), then **refinement**: cost switches to 1 per full edge / 0 otherwise (minimise full edges crossed), box 10 **+15 per iteration**, up to 20 iterations | fast tier -> cleanup -> slow tier, one pricing model throughout |
| **capacity** | per-layer sum / 2, 1 unit per net | per-layer sum, width + spacing (= 2) per net -- equivalent here |

**What stands out.**
1. **The pricing model is the deepest difference.** NTHU-Route's cost is
   a steep function of the edge's *present* demand; ours is an
   accumulated price that only ever rises. Under NTHU's model, ripping a
   net off an edge makes that edge cheap again immediately; under ours it
   stays as expensive as its worst history made it. This is not a
   coefficient: it's a different negotiation dynamic, and it is also the
   model under which "always keep the new route" converges.
2. **Rip-up granularity and acceptance** follow from it: small pieces (2-pin
   segments crossing overflow), always accepted, with topology rebuilt
   each iteration -- vs whole-net regrowth with revert-if-worse.
3. **A two-regime schedule**: congestion-and-wirelength pricing until
   overflow is small, then a pure "cross fewer full edges" objective with
   fast-expanding boxes.
4. **Cheap monotonic routing before maze search** -- a runtime lever that
   would make the wide boxes of Parts 51-54 cheaper.

**Implication for the queued steps.** Step 2 as planned (sweep the history
step and price step `VROUTE_EPS`) tunes a pricing model NTHU-Route doesn't
use. The side-by-side suggests a structurally different step 2 instead: a
new router that swaps in NTHU-Route's present-demand cost (with its
history count and capacity shrink), tested first on its own and then with
segment-level rip-up and no-revert acceptance -- CLAUDE.md's supervision
rule was established under the ever-rising-price model and would need
re-testing under the new one rather than being assumed. To be planned once
Part 57's full-budget results are in.

## Part 59: `gr/pd1route.cpp` -- NTHU-Route 2.0's present-demand pricing -- in progress

Step 2 of the post-Part-57 plan. Part 57 showed box growth has delivered its
gain (277344 -> 112086) and that breadth is exhausted on bigblue1 and
newblue1; Part 58 found the pricing model to be the deepest difference from
NTHU-Route 2.0. `pd1route` (new species `pd`, forked from `nv11route`)
replaces the one thing: every 2D wire-edge cost becomes NTHU's

    cong = (demand + inc) / (cap * (1 - (hist - 1) / (iter * (1.5 + 3 f))))
    cost = f + hist * cong^5,   f = 1 - exp(-5 exp(-0.1 iter))

read from live demand (a net leaving an edge makes it cheaper at once),
with hist = round boundaries at which the edge ended overflowed (from 1),
and iter advancing at every round boundary of the 2D loop. The search,
rip-up, supervision (revert if local overflow got worse), box growth and
layer assignment are `nv11route`'s. `PD1ROUTE_COST=lambda` reproduces
`nv11route`; `PD1ROUTE_WL_FLOOR` holds the WL term at max(f, floor).

A* needed one change: under this cost an edge can cost far below 1 (f is
0.0017 by iteration 80), so the inherited Manhattan heuristic overestimates.
It is now scaled by a true per-edge lower bound (1 under lambda, the WL term
under pd). Self-test `testAStarStaysExactUnderPdPricing` checks the scaled
search against an in-test reference Dijkstra on 30 hash-priced fixtures
(30/30 exact) and shows the unscaled heuristic returning a strictly costlier
path on 30/30 -- so the scaling is load-bearing. The runtime consequence
(A* degrading toward Dijkstra as f decays) is measured, not assumed.

Self-test: 347 checks pass, including hand-computed NTHU cost values, the
zero/negative-capacity cases, the hist <= iter invariant that keeps the
capacity shrink above 1/3 (500 iterations of a permanently overflowed edge),
the cost returning exactly to its prior value when demand leaves (the
lambda model's stays raised), and an end-to-end detour-only fixture reaching
zero.

**Stage 0 parity**: `PD1ROUTE_COST=lambda pd1route` vs `nv11route`, both
`round` 48/+2/200, adaptec1 at 3/2/3 -- route files byte-identical (121349321
bytes, `cmp` clean; real overflow 122872 both).

**Smoke** (pd, `round` 48/+2/200, adaptec1, 3/3/3, 126s): legal per
`gr_verify` (0 malformed / disconnected / incomplete / unrouted), 2D
overflow 42342, real 60668. Trace: fast rounds 343082 -> 240286 -> 219704
(lambda at full budget, same rounds: 336938 -> 236446 -> 229744), cleanup ->
214106, slow rounds 83702 -> 57498 -> 42342 (lambda's first three slow
rounds in its full-budget run: 135632 -> 110290 -> 96686). An early signal
only -- not a same-budget result.

**Screen** (adaptec1, 30/75/100, `run_tracked.sh`, `VROUTE_TRACE=1`), against
the same-budget `nv11route` screens: `round` 48/+2/200 = 27094, `pernet`
6/+6/96 = 27394 (Parts 53-54):

| run | setting | overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|
| S1 | pd, `round` 48/+2/200 | 37494 (2D 27644) | 18 | 7833781 | 40518s | 1124MB |
| S2 | pd, `pernet` 6/+6/96 | 37578 (2D 27968) | 20 | 7713380 | 29576s | 1039MB |
| S3 | pd, `round` 48/+2/200, WL floor 1 | 43250 (2D 32346) | 20 | 7127504 | 29560s | 961MB |

S3 separates the two halves of NTHU's cost: present-demand congestion
pricing with a WL weight that never decays (and a full-strength A*).

S2 and S3 are legal (`gr_verify`: 0 malformed / out-of-bounds /
disconnected / incomplete / unrouted) and `eval2008.pl` agrees on total
overflow, max and scored WL for both. **Both lose to their same-budget
lambda baselines: S2 37578 vs 27394 (+37.2%), S3 43250 vs 27094 (+59.6%),
at 3.2-3.8x the runtime (29576s vs 7753s, 29560s vs 9214s).** Neither had
stalled -- both hit the round cap still improving (S2 -8.4%, S3 -3.9% over
their last 15 slow rounds) -- but the lambda runs are far flatter by then
and far lower.

Slow-tier 2D overflow, same budget, same box settings (lambda = the Part 53
`nv11route` screens):

| slow round | lambda `round` | S3 pd `round` floor 1 | lambda `pernet` | S2 pd `pernet` |
|---|---|---|---|---|
| 0 | 135632 (acc 24166) | **101980** (acc 22418) | 163440 (acc 23815) | **128356** (acc 23740) |
| 9 | **52742** (acc 13731) | 72152 (acc 9026) | **61214** (acc 15370) | 71894 (acc 9585) |
| 29 | **25810** (acc 8251) | 46050 (acc 6533) | **26524** (acc 8826) | 42982 (acc 6473) |
| 49 | **20096** (acc 6525) | 36126 (acc 5064) | **20540** (acc 6850) | 32396 (acc 4322) |
| 74 | **18022** (acc 5477) | 32346 (acc 3421) | **18400** (acc 5446) | 27968 (acc 3412) |

(acc = reroutes kept by the supervision rule that round.) Present-demand
pricing starts the slow tier ~25% lower, then falls behind by round 9 and
stays behind. The trace points at why: the supervision rule keeps far fewer
of its reroutes -- by round 9, 9026 / 9585 accepted against lambda's 13731 /
15370, with most of the rest reverted as locally worse. Under NTHU's cost an
edge exactly at capacity is barely more expensive than one just below it
(cong^5 is ~1 there, not a cliff), and a net ripped off an edge makes it
cheap again immediately, so reroutes often land on just-full edges and push
them over -- which the revert-if-worse rule then undoes. NTHU never reverts:
it keeps every new route and lets history sort it out, and it rips up
2-pin segments, not whole nets. **The pricing model and the acceptance rule
look coupled: present-demand pricing under this project's supervision rule
is worse than lambda pricing under it.** That is a result about the pair,
not about present-demand pricing alone.

**S1 (NTHU's cost exactly, WL term decaying, `round` 48/+2/200): 37494**,
legal, `eval2008.pl`-agreed (37494 / 18 / 7833781) -- +38.4% vs lambda's
27094 at 4.4x its runtime (40518s vs 9214s). Same shape as S2/S3: 95702 at
slow round 0 (lambda 135632), behind by round 9 (69318 vs 52742, accepted
8707 vs 13731), 27644 2D at round 74 vs lambda's 18022. All three pd1route
screens lose under revert-if-worse; Part 60 tests the keep rule.

Runtime: S3 runs A* at full strength (WL term 1, heuristic scale 1) and is
still 3.2x slower than lambda while rerouting a similar number of nets per
round, so the decaying heuristic is not the main cost. The likely cost is
`std::pow(cong, 5)` in every edge relaxation, where lambda pricing does one
multiply-add; any follow-up should compute cong^5 by multiplication and
re-check.

## Part 60: `gr/pd2route.cpp` -- present-demand pricing with NTHU's acceptance rule -- in progress

Part 59's diagnosis: present-demand pricing lost under this project's
revert-if-worse supervision rule because the rule undid most of its
reroutes. NTHU-Route keeps every reroute. `pd2route` (forked from
`pd1route`) adds `PD2ROUTE_ACCEPT=keep`: every slow-tier reroute is kept;
the slow tier's patience stop is off under keep (the best snapshot is still
returned); the cleanup pass stays supervised. It also computes cong^5 by
multiplication instead of `std::pow` (agrees to 1e-13 relative over 2100
evaluations), the suspected cause of pd1route's 3.2-3.8x runtime.

Self-test: 361 checks pass. Stage 0 parity: `PD1ROUTE_COST=lambda
pd2route` vs `nv11route`, `round` 48/+2/200, adaptec1 3/2/3 --
byte-identical.

Screens, one `run_batch.sh` batch (each job `gr_verify`- and
`eval2008.pl`-checked in the batch; summary in the job file's `.summary`):
adaptec1, 30/75/100, `round` 48/+2/200, vs the same-budget lambda + revert
screen's 27094 (Part 53) and pd1route S1 (pd + revert, `std::pow`).

| run | pricing | acceptance | overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|---|
| J1 | pd | keep | **7570** (2D **0**) | 14 | 8497680 | 3643s | 1089MB |
| J3 | lambda | keep | 231136 (2D 219786 = the pre-escalation snapshot) | 74 | 6592644 | 7499s | 997MB |
| J2 | pd | revert (control; speed fix vs S1) | stopped at slow round 42 (2D 34602), 4.2h | | | | |

Part 60b (launched): J1's exact settings on bigblue1 and newblue1, one
batch. No same-budget (30/75/100) lambda screens exist for them; the
comparison points are the full-budget (60/150/200) `round` 48/+2/200
results, 73714 / 12524 (Part 57) -- a looser bar than same-budget, since a
screen of the same lambda setting would score no better than its full
budget. A win at a quarter of the budget is therefore conclusive; a loss
would need same-budget controls before any claim.

**Part 60b results** (both `status=OK`: legal, `gr_verify` and
`eval2008.pl` agree on total, max and scored WL):

| benchmark | pd2 pd+keep, 30/75/100 | 2D overflow | full-budget best (Part 57, `round`, 60/150/200) | delta | runtime (vs full-budget best's) |
|---|---|---|---|---|---|
| adaptec1 | **7570** (max 14, WL 8497680) | **0** (slow round 65) | 25848 | **-70.7%** | 3643s (12653s) |
| newblue1 | **4694** (max 10, WL 7219904) | 1186 | 12524 | **-62.5%** | 5899s (21313s) |
| bigblue1 | **2922** (max 8, WL 8288492) | 58 | 73714 | **-96.0%** | 4772s (7737s) |
| **total** | **15186** | 1244 | 112086 | **-86.5%** | 14314s (41703s) |

**`pd2route` with `PD1ROUTE_COST=pd PD2ROUTE_ACCEPT=keep` and `round`
48/+2/200 at the 30/75/100 screen budget is the new best real result:
15186 total, -86.5% vs the full-budget best (112086) at a quarter of the
budget and a third of the runtime, better on all three.** (Cross-budget
in the conservative direction, per the note above.)

Traces:
- bigblue1 2D: 2287636 (slow round 0) -> 313264 (9) -> 51040 (19) -> 4668
  (29) -> 838 (39) -> 178 (49) -> 102 (59) -> 66 (74), still falling at the
  round cap. Its layer assignment stalled (patience) at real 2922.
- newblue1 2D: 376334 -> 28320 (9) -> 6092 (19) -> 2052 (29) -> 1426 (39)
  -> 1274 (49) -> 1222 (59) -> 1214 (74): a plateau from ~round 40, ~4600
  nets still rerouted every round. newblue1 is also where NTHU-Route's own
  2D stage stopped short (2D 18) and its layer assignment failed (Part 52).

Where the remaining 15186 is: adaptec1's 7570 is all layer assignment (2D
is 0); bigblue1's 2922 is ~98% layer assignment (2D 58); newblue1's 4694 is
layer assignment on top of a 2D residue of 1186. **Layer assignment is now
the dominant source of real overflow on all three.**

**J1 -- present-demand pricing + keep every reroute: the 2D stage converged
to zero overflow on adaptec1 at slow round 65**, the first time any router
in this project has reached 2D zero on a real benchmark. After layer
assignment the real (wire + via) overflow is **7570**, max 14 -- legal
(`gr_verify` 0/0/0/0/0) and `eval2008.pl`-agreed (7570 / 14 / 8497680) --
**-72.1% vs the same-budget lambda + revert screen's 27094**, and -70.7% vs
the full-budget best's adaptec1 25848 (Part 57) at a quarter of the budget.
Runtime 3643s: 2.5x *faster* than the lambda screen (9214s) and 11x faster
than pd1route S1 (40518s). Scored WL 8497680, +6.2% vs the lambda screen's
8004059 (NTHU-Route: 5359901).

Slow-tier 2D overflow (every reroute kept; "kept-worse" = kept reroutes
that made local overflow worse):

| slow round | 2D overflow | rerouted | kept-worse | box margin | WL term |
|---|---|---|---|---|---|
| 0 | 1387698 | 90356 | 36916 | 48 | 0.168 |
| 1 | 688352 | 86032 | 32066 | 50 | 0.154 |
| 5 | 253908 | 63387 | 16328 | 58 | 0.106 |
| 9 | 123430 | 51354 | 9931 | 66 | 0.072 |
| 19 | 15630 | 26798 | 2713 | 86 | 0.027 |
| 29 | 1144 | 8873 | 380 | 106 | 0.010 |
| 39 | 148 | 2139 | 42 | 126 | 0.0037 |
| 49 | 50 | 706 | 38 | 146 | 0.0014 |
| 59 | 6 | 132 | 6 | 166 | 0.0005 |
| 65 | **0** | 10 | 0 | 178 | 0.0003 |

The dynamic is NTHU's, not this project's: the first keep round blows 2D
overflow up tenfold (202780 after cleanup -> 1387698), then it falls
monotonically-ish by roughly a quarter to a third per round, with the
number of nets still being rerouted shrinking with it -- where every
revert-rule run in Parts 53-59 plateaued in the tens of thousands.
**Part 59's hypothesis holds: present-demand pricing and keep-every-reroute
are a matched pair; either one alone loses (Part 59 for pd + revert; J3
for lambda + keep).**

**J3 -- lambda pricing + keep every reroute: 231136**, legal and
`eval2008.pl`-agreed. Its slow tier never beat the fast tier's own snapshot
(2D 219786 after cleanup): keeping every reroute under lambda pricing sent
2D overflow to 8026318 in slow round 0 (67784 of 103404 reroutes locally
worse), and it wandered between ~0.75M and ~1.0M for the remaining 74
rounds (754234 at round 29, 950108 at round 74) without converging. This is
CLAUDE.md's "supervise every escalation" finding reproduced exactly: under
an ever-rising price the revert rule is load-bearing. Under present-demand
pricing it is the obstacle. **The supervision rule is right for the pricing
model it was found under and wrong for NTHU's** -- CLAUDE.md's rule needs
that qualification, not deletion.

What the 7570 now is: all of it comes from layer assignment
(`negotiateViaAssignment`), which has to fit a 2D solution with zero 2D
overflow onto 6 real layers plus vias. The binding stage for adaptec1 has
moved from 2D routing to layer assignment. NTHU-Route reaches real 0 on
adaptec1 from its own 2D-zero solution, so its layer assignment (Part 58:
dynamic-programming over each net's tree, per-layer capacity aware) is the
next thing to compare against.

J2 (pd + revert with the multiplication speed fix) was stopped by decision
after 4.2h at slow round 42, to free its slot once J1/J3 had settled the
question it controlled for. Its trajectory tracked pd1route S1 closely (2D
94622 / 68054 / 42220 at slow rounds 0 / 9 / 29 vs S1's 95702 / 69318 /
41592), as expected -- the fix changes only the last bits. The speed fix's
runtime effect on the revert configuration was not measured to completion;
pd+keep's speed (J1: 3643s) comes mainly from the shrinking set of nets
rerouted each round, not from the fix alone.

## Part 61: `pd2route` pd + keep at the full budget

Part 60b's settings (`PD1ROUTE_COST=pd PD2ROUTE_ACCEPT=keep`, `round`
48/+2/200) at the full committed budget (`VROUTE_FAST_ROUNDS=60
VROUTE_SLOW_ROUNDS=150 NV2ROUTE_VIA_ROUNDS=200`) on all three, one
`run_batch.sh` batch. Questions: does bigblue1's 2D (58 at the screen's cap,
still falling) reach zero; does newblue1's 2D plateau (~1200) move; and how
much do the extra layer-assignment rounds (200 vs 100) cut the
layer-assignment overflow that now dominates. Baseline: Part 60b's screen,
7570 / 4694 / 2922 = 15186.

## Part 62: `gr/pd3route.cpp` -- NTHU-Route 2.0's layer assignment

Part 60b left layer assignment as the dominant source of real overflow
(adaptec1: 2D 0, real 7570). The inherited DP (`nv2route`) assigns one
layer per whole straight leg, prices wire with a soft usage/capacity ratio
averaged over the leg (no cliff at capacity), and prices vias against a
via-site capacity model ISPD 2008 scoring does not have. NTHU-Route's
`Layerassignment.cpp` (read in full, 413 lines): per-tile layer choice via a
DP over the net's tree; wire capacity as a hard constraint (a layer is only
entered where its 3D edge still has room, unless the 2D edge is itself
overflowed); objective = via overflow then via count (its via edges have
no capacity in ISPD terms either); nets ordered by pins per 2D edge used,
descending, ties to fewer pins; one pass.

`pd3route` (forked from `pd2route`; 2D stage unchanged) ports that as
`PD3ROUTE_LA=nthu` (default; `inherited` restores the old DP): trees split
to unit edges, a per-tile overflow penalty (1e6 per unit, dominating any
via count) with a 1e-4 fill tie-break, vias at 1 per layer crossed with no
via-capacity term, NTHU's net order, and the existing repair rounds limited
to nets with real wire overflow. `PD3ROUTE_LA_FROM=<route>` reruns layer
assignment alone on an existing solution, projected back to 2D.

Self-test: 383 checks pass, including a fixture where the inherited
whole-leg DP must overflow and the nthu DP switches layers mid-leg to
zero overflow despite saturated via sites, and a route() -> nthu layer
assignment -> write -> project round trip that recovers exactly the 2D
stage's tile edges.

**Results -- layer assignment alone, on Part 60b's J1 2D solutions**
(`run_checked.sh`: all `status=OK`, `gr_verify` and `eval2008.pl` agree):

| benchmark | pd2route (inherited LA) | **pd3route nthu LA** | 2D overflow of the input | scored WL (vs pd2route) | LA time |
|---|---|---|---|---|---|
| adaptec1 | 7570 | **16** (max 2) | 0 | 8607270 (+1.3%) | 9.5s |
| bigblue1 | 2922 | **58** (max 2) | 58 | 8480063 (+2.3%) | 11.5s |
| newblue1 | 4694 | **1212** (max 4) | 1186 | 7442304 (+3.1%) | 11.4s |
| **total** | 15186 | **1286** | 1244 | | |

**-91.5% vs Part 60b, and -98.9% vs the lambda-pricing full-budget best
(112086).** Real overflow now equals the 2D overflow on bigblue1 and is
within 26 of it on newblue1: layer assignment went from the dominant loss
to almost none. The repair rounds find nothing further (adaptec1 stalls
after one round at 16). Control: the inherited LA on the same projected
input gives 7528 (vs 7570 from the original in-process run -- the
projection reorders segments, nothing else), so the gain is the method,
not the projection.

The 16 on adaptec1 (8 tile edges at overflow 2 = one net each, with 2D
overflow 0) is probably capacity rounding: the 2D stage pools raw
capacity across layers, and a layer whose capacity is not a multiple of
a net's width + spacing (2) wastes the remainder -- e.g. layer capacities 3
+ 3 + 3 pool to 9 = 4 nets in 2D, but hold only 3 on real layers. Not yet
verified. If it holds, the 2D stage should pool floor(cap_l / 2) * 2 per
layer instead.

Part 61's full-budget pd2route runs are still going; their 2D solutions
feed straight into `PD3ROUTE_LA_FROM` when they finish.

**Part 62 follow-up: adaptec1 reaches ZERO total overflow.** The 16 was not
capacity rounding (no adjusted layer edge on adaptec1 has an odd capacity;
the hypothesis above is refuted). All 8 overflowed layer edges belonged to
one net, n7807 (2271 pins, a giant net routed by the fixed-pattern
`routeGiantNet`), sitting on layers whose adjusted capacity is 0 there while
other layers had room (2D demand 26-34 against 38 usable). Its wires form
cycles, so `buildNetTree` rejected it and it fell back to the old greedy
per-segment layer choice. Fix (kNthu only): `buildNetTree(dropCycles)`
skips a cycle-closing stretch (redundant wire; its ends are already
connected) instead of failing the net, and pin tiles lying on a wire become
nodes, so a dropped stretch can never contain a pin. Self-test 388 checks
(a ring net with a mid-side pin keeps a spanning tree with the pin
attached; two disjoint wires are still rejected).

Rerun, layer assignment alone on the same J1 2D solutions (`run_checked.sh`,
all `status=OK`, `gr_verify` and `eval2008.pl` agree):

| benchmark | real overflow | max | scored WL | 2D overflow of the input |
|---|---|---|---|---|
| adaptec1 | **0** | **0** | 8572564 | 0 |
| bigblue1 | **58** | 2 | 8470679 | 58 |
| newblue1 | **1186** | 2 | 7410777 | 1186 |
| **total** | **1244** | | | 1244 |

**adaptec1: zero total overflow, zero max overflow -- the first real ISPD
benchmark this project has routed with no overflow at all.** On all three,
real overflow now equals the 2D overflow exactly: layer assignment is
lossless, and everything left is the 2D stage's (bigblue1 58, still
falling when the screen hit its round cap; newblue1 1186, a plateau).
Total 1244, -98.9% vs the lambda-pricing full-budget best (112086).

**Part 61 results (full budget 60/150/200, pd2route pd + keep; then
pd3route's nthu layer assignment on each output via `PD3ROUTE_LA_FROM`,
200 repair rounds).** Every run `status=OK` (legal; `gr_verify` and
`eval2008.pl` agree):

| benchmark | pd2route real (inherited LA) | 2D overflow | + pd3route nthu LA | scored WL | 2D runtime |
|---|---|---|---|---|---|
| adaptec1 | 6216 | 0 | **0** (max 0) | 8678482 | 5013s |
| bigblue1 | 2712 | 60 | **60** (max 2) | 8512687 | 6982s |
| newblue1 | 3994 | 1108 | **1108** (max 2) | 7923750 | 16063s |
| **total** | 12922 | 1168 | **1168** | | |

vs the 30/75/100 screen + nthu LA (Part 62): 0 / 58 / 1186 = 1244. The
full budget buys -6% in total (newblue1 -78, bigblue1 +2) at 2-3x the
runtime and 1-7% more wirelength. **Both remaining benchmarks sit on 2D
plateaus, not round caps:** bigblue1's 2D reaches ~100 by slow round 70 and
oscillates 84-126 for the last 80 rounds (best 60), still rerouting ~2500
nets a round; newblue1's reaches ~1200 by round 50 and oscillates
1108-1186, rerouting ~4700 a round. More of the same negotiation will not
close them. NTHU-Route switches regimes at this point: its main stage stops
once overflow falls below a threshold (340 on bigblue1), and a refinement
stage takes over whose cost is 1 per *full* edge crossed and 0 otherwise,
with search boxes starting at 10 and growing 15 per iteration (Part 58).

## Part 63: `gr/pd4route.cpp` -- NTHU-Route 2.0's refinement stage

After Part 62 the remaining overflow was the 2D stage's, on plateaus
(Part 61). NTHU's `Post_processing.cpp` (read in full): once the main
stage ends, cost switches to 1 per *full* edge, 0 otherwise (`MADEOF_COST`;
the maze breaks ties by full-edge distance, then bends); 2-pin pieces that
cross overflow are rerouted, sorted by (their overflow, bounding box)
descending, with boxes 10 + 15 per iteration for up to 20 iterations, every
reroute kept.

`pd4route` (forked from `pd3route`) adds that as `refine2D`, run after the
2D stage (or after `PD3ROUTE_LA_FROM`'s projection) and before layer
assignment: `CostModel::kRefine` (1 for an edge one more net would
overflow, 1e-5 otherwise -- length only breaks ties; A* scaled by 1e-5),
whole-net MSMV reroutes in NTHU's order, `PD4ROUTE_REFINE_{ITERS,BOX_INIT,
BOX_INC,ACCEPT}` defaulting to NTHU's 20 / 10 / 15 / keep, and the best
state seen passed on. Self-test 408 checks (including a wall of full edges
the refinement search detours around by 10 tiles where the lambda search
goes straight through, and a three-nets-on-a-one-net-row fixture refined
from 2D overflow 28 to 0 with every net connected).

**Part 63a -- refinement from Part 61's full-budget 2D solutions, then
nthu layer assignment** (`run_checked.sh`, both `status=OK`, `gr_verify`
and `eval2008.pl` agree):

| benchmark | 2D before | refinement | real overflow | max | scored WL | time |
|---|---|---|---|---|---|---|
| bigblue1 | 60 | -> 4 (iter 0, 24 nets rerouted) -> **0** (iter 1, 2 nets) | **0** | **0** | 8510946 | 9.9s |
| newblue1 | 1108 | keep: -> 7850 (iter 0) -> 2018 (iter 19); best kept = 1108 | 1108 | 2 | 7923750 | 187s |

**bigblue1: zero total overflow, zero max overflow** -- the second real
ISPD benchmark at zero (adaptec1, Part 62). On newblue1 NTHU's keep rule
made the refinement worse (the first iteration's ~4000 whole-net reroutes
pushed 2D overflow up 7x and 20 iterations never recovered); the
best-state rule passed the unrefined 1108 through. Part 63b runs newblue1's
refinement with revert-if-worse.

**Part 63b -- newblue1 refinement with `PD4ROUTE_REFINE_ACCEPT=revert`**,
from the same Part 61 2D solution (2D 1108), then nthu layer assignment
(all `status=OK`; `gr_verify` and `eval2008.pl` agree):

| variant | boxes | iterations | 2D after refine | real overflow | max | scored WL | time |
|---|---|---|---|---|---|---|---|
| NTHU boxes | 10 + 15/it | 20 | 488 | 488 | 2 | 7827708 | 168s |
| wide boxes | 50 + 25/it | 20 | 486 | 486 | 2 | 7825021 | 281s |
| long, slow growth | 10 + 5/it | 60 | **484** | **484** | 2 | 7820777 | 515s |

Revert-if-worse refinement cuts newblue1 by 56% (1108 -> 484-488), almost
all of it in the first 10 iterations (744 after iteration 0), then stops
moving whatever the box size or iteration count -- while still rerouting
~2900 nets per iteration and reverting ~170-190. On bigblue1 the keep rule
already reached 0 (Part 63a). **Best per benchmark now: adaptec1 0,
bigblue1 0, newblue1 484 -- total 484** (-99.6% vs the lambda-pricing
full-budget best 112086; -99.8% vs the committed margin-6 `nv4route`'s
277344). newblue1 is also the benchmark NTHU-Route itself did not finish
here (Part 52: 2D 18, layer assignment failed).

Open question: is newblue1's ~484 a routing limit or close to an
unavoidable floor (e.g. pin tiles with more nets leaving them than their
four edges can carry)? A per-tile lower bound over a checkerboard of
disjoint tiles would say, without any routing.

**newblue1's 484 -- where it is and whether it is forced** (analysis only,
no routing; scripts in the session scratchpad):
- All of it is one hotspot: 233 2D edges, each over by exactly one net,
  bounding box x 174-203, y 47-75 (30 x 29 tiles); none on a
  capacity-adjusted edge.
- Lower bounds find nothing forcing it. Per-tile (nets that must leave a
  pin tile vs its four edges' capacity, summed over a checkerboard of
  disjoint tiles): 0. Rectangle cuts around the hotspot (nets with pins both
  inside and outside vs the rectangle's boundary capacity): demand 5552 vs
  capacity 7316 on the hotspot's own box, with slack growing as the box
  grows (expand 40: 8498 vs 26260). Full-chip straight cuts: every one has
  >11000 units of slack.
- So the hotspot is internal congestion, not a boundary shortage these
  bounds can see: the 484 is probably a routing limit rather than a floor,
  though not proven avoidable (these bounds are weak -- they ignore demand
  from nets routed wholly inside the region and through-traffic).

## Part 64: `gr/pd5route.cpp` -- two attacks on newblue1's hotspot

`pd5route` (forked from `pd4route`) adds two switches, both off by default
(defaults byte-identical to `pd4route`: bigblue1 refinement output `cmp`-
identical, and identical to Part 63a's zero-overflow file). Self-test 424
checks.

1. `PD5ROUTE_REFINE_ORDER=shuffle` (`PD5ROUTE_SEED`): each refinement
   iteration reroutes its overflowed nets in a seeded permutation that
   differs per iteration, instead of NTHU's fixed order.
2. `PD5ROUTE_REGION=1`: hotspot rip-up before refinement -- every non-giant
   net with wire in the overflow's bbox (+ `PD5ROUTE_REGION_EXPAND`, 5) is
   rerouted, overflowed or not, under pd2route's negotiation (present-demand
   pricing, keep every reroute), then the still-overflowed ones for
   `PD5ROUTE_REGION_ROUNDS` (60) rounds with margin 10 + 2/round; best state
   kept.

**Part 64a -- option 1, shuffled refinement** (newblue1, revert, 60
iterations, box 10 + 5/it; all `status=OK`, `gr_verify` and `eval2008.pl`
agree):

| start | seed | real overflow | max | scored WL | time |
|---|---|---|---|---|---|
| Part 61 2D (1108) | 1 | 490 | 2 | 7823884 | 519s |
| Part 61 2D (1108) | 2 | **480** | 2 | 7816381 | 522s |
| Part 63b best (484) | 1 | 482 | 2 | 7819995 | 506s |

Shuffling the order moves the plateau by at most 4 nets (480 vs 484) and
seed-to-seed spread is ~10: the refinement stage itself is exhausted on
newblue1, not stuck in an order-induced cycle. newblue1 best: 480. Part 64b
(option 2) is running.

**Part 64b -- option 2, hotspot rip-up** (newblue1; region 60 rounds, then
revert refinement 60 iterations box 10 + 5/it, then nthu LA; all
`status=OK`, `gr_verify` and `eval2008.pl` agree):

| start | region box | region nets | region 2D (best) | after refinement = real | max | scored WL | time |
|---|---|---|---|---|---|---|---|
| Part 61 2D (1108) | x 165-210, y 39-83 | 14446 | 1108 -> 556 | **344** | 2 | 7702952 | 1229s |
| best 484 state | x 169-208, y 42-81 | 11537 | 484 -> 484 (548 at round 59) | 482 | 2 | 7819552 | 1284s |
| best 484, expand 15 | x 159-218, y 32-91 | 22062 | 484 -> 484 (580 at round 59) | 482 | 2 | 7819552 | 1294s |

**newblue1: 344** (-28% vs 480). The region negotiation's first round --
rerouting every net in the box -- blows 2D overflow up to ~16000-18000, then
it falls steadily (from 1108: 16488 / 11168 / 8110 / 1556 / 874 / 706 / 556
at rounds 0 / 1 / 2 / 14 / 29 / 44 / 59) and was **still falling when its 60
rounds ran out in all three runs**; from the 484 state it had not yet come
back below its start, so the best-state rule kept the input. The refinement
after it then took the 1108 run's 556 to 344. Next: the same with 200
region rounds (Part 64c).

**Part 64c -- hotspot rip-up with 200 region rounds** (then revert
refinement 60 iterations, nthu LA; all `status=OK`, `gr_verify` and
`eval2008.pl` agree):

| start | region nets | region 2D, rounds 24 / 49 / 99 / 149 / 199 | region best | after refinement = real | max | scored WL | time |
|---|---|---|---|---|---|---|---|
| Part 61 2D (1108) | 14446 | 1006 / 630 / 418 / 370 / 318 | 318 | **236** | 2 | 7890296 | 14596s |
| best 484 state | 11537 | 1042 / 670 / 448 / 414 / 364 | 330 | 260 | 2 | 7866279 | 14563s |
| 344 state (64b) | 10527 | 1010 / 738 / 422 / 392 / 354 | 320 | 252 | 2 | 7847046 | 14622s |

**newblue1: 236** (-31% vs 344). More region rounds keep paying, but slowly
and noisily: from rounds 100 to 200 the region's 2D overflow drifts from
~420 to ~320-360, and refinement takes another ~70-80 off. Restarting from
a better state does not compound (from 344 -> 252, from 484 -> 260, from
1108 -> 236): each run's first region round reroutes every net in the box
and discards most of the starting state. Runtime is the cost: ~4h per run
for 200 rounds (vs ~20 min for 60), since the set of overflowed nets
rerouted each round stays in the thousands (3000-3500) and the MSMV margin
grows 2 per round.

**Best per benchmark: adaptec1 0, bigblue1 0, newblue1 236 -- total 236**
(-99.8% vs 112086; -99.9% vs the committed margin-6 nv4route's 277344).

## Part 65: is newblue1's last overflow forced? `gr/lpbound.cpp`

`lpbound` (an analysis tool, not a router) computes a certified lower bound
on total overflow by weak LP duality for fractional min-overflow routing:
for any edge lengths 0 <= l_e <= 1, overflow >= sum_k d_k T_k(l) - sum_e
c_e l_e, with T_k a lower bound on net k's Steiner cost under l (exact for 2
terminals; max(longest pair, MST/2) otherwise). Lengths are nonzero only
around a region; outside tiles merge into one free node, which keeps the
bound valid for the whole chip. The lengths are chosen by Frank-Wolfe on a
smoothed overflow (gradient = sigmoid, always in (0,1), so every iterate is
a valid dual point); the averaged Frank-Wolfe usage is a genuine fractional
routing, giving an upper bound on the relaxed optimum. Self-test: a forced
corridor (3 nets through two 1-net edges) -- bound 8 = true minimum, and the
averaged routing also 8; an uncongested net -- bound 0, routing 0. (A first
version with projected subgradient ascent bang-banged its lengths and never
left 0; replaced.)

newblue1, box x 165-210, y 39-83 (2071 nodes, 4231 edges, 14442 nets with a
pin inside), beta 50, 150 iterations: certified lower bound 0; averaged
fractional routing overflow 1894 and still falling ~10% per 10 iterations
-- inconclusive (Frank-Wolfe converges at O(1/t)). 1000 iterations running.

1000 iterations (same box, beta 50, ~70 min): averaged fractional routing
overflow 1267 / 788 / 609 / 518 / 463 / 427 / 404 / 386 / **374** at
iterations 200 / 300 / ... / 1000, still falling but slowly; certified lower
bound still **0** (every gradient iterate's dual value negative, -9320 at
the end).

**Verdict: inconclusive, and not worth pushing further this way.** Nothing
certifies any of newblue1's 236 as forced, and the Frank-Wolfe fractional
routing (374) is still worse than the router's own integral 236 -- partly
because its linear step routes each net on an MST of shortest paths, up to
2x the Steiner minimum, so it is not a strong primal either. Settling the
question needs a real LP solver (and exact or tighter Steiner handling), a
much larger build with no guarantee of a decisive answer. `lpbound` stays
as a tool: it gives valid certificates whenever one exists at its lengths
(the forced-corridor self-test reaches the exact minimum).

## Part 66: one command, one setting, all three benchmarks

`pd5route` from scratch, identical settings everywhere: 2D full budget
(60/150/200, `round` 48/+2/200, pd + keep) -> hotspot rip-up 200 rounds ->
revert refinement 60 iterations (box 10 + 5) -> nthu layer assignment. Note:
in-process, a net's `pts` still carry the 2D stage's Steiner points (the
from-file runs of Parts 63-64 used pins only).

## Part 67: `gr/pd6route.cpp` -- 2-pin-piece reroutes

`pd6route` (forked from `pd5route`) adds `PD6ROUTE_PIECES=1`: refinement
and region negotiation reroute a net's pieces (maximal paths between pins
and branch tiles) instead of the whole net -- the piece is removed and the
two parts of the tree reconnected by one MSMV search from every tile of one
part to any tile of the other; the rest of the net stays put. Pieces-off
default is byte-identical to `pd5route` (bigblue1 refinement output
`cmp`-identical). Self-test 436 checks (a T-shaped net where only the piece
through a full edge moves, the left piece stays tile for tile, usage stays
exact; an admissible sink-bbox heuristic finds the same optimal costs as
nearest-sink on 20 multi-sink fixtures; both stages clear the
three-on-one-row fixture in piece mode). Screen on newblue1 from the Part
61 2D solution (1108): region(pieces, 200) + revert refine(pieces); keep
refine(pieces) alone; revert refine(pieces) alone.

**Part 67 results** (newblue1, from the Part 61 2D solution 1108, then
nthu LA; all `status=OK`, `gr_verify` and `eval2008.pl` agree):

| run | stage trace | real overflow | max | scored WL | time |
|---|---|---|---|---|---|
| region (pieces, 200 rounds) + revert refine (pieces, 60) | region 1108 -> 13430 (round 0, 48905 pieces rerouted) -> 8324 -> 498 (39) -> 198 (79) -> 160 (119) -> 152 (159) -> 116 (199), best 114; refine -> 100 -> 94 -> 90 | **90** | 2 | 7854897 | 2260s |
| keep refine (pieces, 60) alone | 2996 (iter 0) -> 2522 -> 1634 (19) -> 1588; best kept 1108 | 1108 | 2 | 7923750 | 398s |
| revert refine (pieces, 60) alone | 890 -> 854 -> 788 -> 784 | 784 | 2 | 7871481 | 545s |

**newblue1: 90** (-62% vs 236), in 38 minutes. Piece granularity is what
the hotspot rip-up needed: whole-net region negotiation (Part 64c) peaked
~16500 and plateaued ~320-360 after 200 rounds; by pieces it peaks 13430
and reaches 116, still falling at the round cap. For refinement alone,
pieces are *worse* than whole nets under revert (784 vs 484) and keep
still fails -- piece-level moves need the negotiation (present-demand
pricing + history), not the 0/1 full-edge cost.

**Part 66 (one command) so far: adaptec1 0 (max 0, WL 8679261, 4797s) and
bigblue1 0 (max 0, WL 7962328, 7261s)** -- both `status=OK` -- from
`pd5route` with identical settings; newblue1 still running.

Follow-up (Part 67b): piece-mode region 500 rounds from 1108, and 200 more
region rounds from the 90 state.

**Part 67b** (newblue1, piece mode; all `status=OK`, both scorers agree):

| run | region 2D at rounds 49 / 99 / 199 / 299 / 399 / 499 | region best | after revert refine = real | max | scored WL | time |
|---|---|---|---|---|---|---|
| region 500 rounds from 1108 | 328 / 164 / 116 / 86 / 70 / 88 | 62 | **56** | 2 | 7946278 | 5646s |
| region 200 rounds from the 90 state | 302 / 182 / 120 (199) | 90 (kept input) | 90 | 2 | 7853091 | 2178s |

**newblue1: 56.** More piece-mode region rounds keep paying (200 -> 90,
500 -> 56), with diminishing, noisy returns (86 at round 299, 70 at 399,
88 at 499). Continuing from a good state does not: the region's round 0
reroutes every piece in the box and discards it (the from-90 run climbs back
to hundreds and never returns below 90 in 200 rounds).

**Best per benchmark: adaptec1 0, bigblue1 0, newblue1 56 -- total 56.**

**Part 66 results** (`pd5route`, one setting, from scratch; all
`status=OK`, `gr_verify` and `eval2008.pl` agree):

| benchmark | 2D after search | region (whole nets, 200) | refine (revert) | real | max | scored WL | time |
|---|---|---|---|---|---|---|---|
| adaptec1 | 0 | -- | -- | **0** | 0 | 8679261 | 4797s |
| bigblue1 | 60 | -> ... | -> 0 | **0** | 0 | 7962328 | 7261s |
| newblue1 | 1108 | 1108 -> **1056** | -> 818 | 818 | 2 | 7898515 | 39239s |

adaptec1 and bigblue1 reach zero from one command. newblue1 does not
reproduce the from-file result for the same settings (Part 64c: region
1108 -> 318, final 236): in-process, each net's `pts` still carry the 2D
stage's Steiner points (route() replaces pts with the exact-Steiner
solution's points), so every whole-net MSMV reroute must reconnect those
old branch points too, which pins the region negotiation in place; the
from-file path (`prepareFor2DRefine`) uses pin tiles only. The fix is to
give the post-2D stages pin-only terminals in-process as well (a new
generation, since pd5route is committed). Piece mode (pd6route, Part 68,
running) builds its trees from the wiring and the pins, not from `pts`, so
it may not be affected.

## Part 68: one `pd6route` command, one setting, all three benchmarks

    VROUTE_FAST_ROUNDS=60 VROUTE_SLOW_ROUNDS=150 NV2ROUTE_VIA_ROUNDS=200
    NV11ROUTE_BOX_MODE=round NV11ROUTE_BOX_INIT=48 NV11ROUTE_BOX_GROW=2 NV11ROUTE_BOX_CAP=200
    PD1ROUTE_COST=pd PD2ROUTE_ACCEPT=keep PD6ROUTE_PIECES=1
    PD5ROUTE_REGION=1 PD5ROUTE_REGION_ROUNDS=500
    PD4ROUTE_REFINE_ACCEPT=revert PD4ROUTE_REFINE_ITERS=60 PD4ROUTE_REFINE_BOX_INC=5
    ./pd6route <benchmark.gr> <out.route>        (layer assignment: nthu, the default)

All `status=OK` (legal; `gr_verify` and `eval2008.pl` agree):

| benchmark | 2D after search | region (pieces) | refine | **real overflow** | max | scored WL | time |
|---|---|---|---|---|---|---|---|
| adaptec1 | 0 | -- | -- | **0** | 0 | 8679261 | 4817s |
| bigblue1 | 60 | box x 0-220 y 85-155, 85716 nets: 60 -> 0 in 31 rounds | -- | **0** | 0 | 7798929 | 7085s |
| newblue1 | 1108 | 1108 -> 80 (500 rounds) | -> 66 | **66** | 2 | 7919994 | 21759s |
| **total** | | | | **66** | | | 33661s |

**One reproducible command, one setting: adaptec1 0, bigblue1 0, newblue1
66 -- total 66.** Piece mode also removes Part 66's in-process problem
(whole-net region negotiation pinned by Steiner-point terminals: 1108 ->
1056 there, 1108 -> 80 here). newblue1's 66 vs the from-file 56 (Part 67b)
is within the region negotiation's round-to-round noise (70-88 over rounds
399-499 there). Against the session's starting point -- `nv11route`
`round` 48/+2/200 at 112086 -- that is -99.94%; against the committed
margin-6 `nv4route` (277344), -99.98%.

## Part 69: `gr/pd7route.cpp` -- wirelength

With overflow at 0 / 0 / 66, the next ranking criterion is scored
wirelength (wire + 1 per via), where Part 68 scores 8.68M / 7.80M / 7.92M
against NTHU-Route's 5.36M on adaptec1 (regenerated here: wire 3585305,
vias 1774596). Via breakdown (script over the route files; a via is
charged to a tile that is a 2D corner of its net, else to a pin tile, else
"straight"):

| adaptec1 | wire | 2D corners | vias | at corners | at pins | straight |
|---|---|---|---|---|---|---|
| Part 68 + WL recovery (bend 2), old LA | 4039828 | 619040 | 3452675 | 1179408 | **2218029** | 55238 |
| + pin-aware LA | 4039828 | 619040 | 2028932 | 922757 | 1029367 | 76808 |
| NTHU-Route 2.0 | 3585305 | 379140 | 1774596 | 655127 | 1074241 | 45228 |

Two mechanisms, both unable to raise overflow:
1. **Pin-aware layer assignment** (`PD7ROUTE_LA_PINS=1`): a pin node's
   via stack in the nthu DP must reach its pins' layers, so pin access is
   priced in the DP (as NTHU's `rec_count` does); before, the DP chose
   layers blind to pins and pin stitching then added 4-5 vias per pin to
   reach wires on the top layers. Alone: adaptec1 8.68M -> 7.23M.
2. **Wirelength and bend recovery** (`PD7ROUTE_WL=1`): every piece of every
   net regrown by a bend-aware search (1 per tile edge + `bend` per corner)
   over only tile edges with room, kept only if strictly cheaper.

adaptec1 sweep from the Part 68 solution (all `status=OK`, 0 overflow):
old LA + recovery bend 1 / 2 -> 7511021 / 7492503; pin-aware LA alone
7233158; both, bend 2 -> 6068760; bend 3 / 4 passes -> 6050170; bend 4 ->
6048934; bend 3 / 4 passes / margin 8 -> **5991707** (defaults). Parity:
`PD7ROUTE_WL=0 PD7ROUTE_LA_PINS=0` is byte-identical to pd6route.
Self-test 457 checks.

**All three, pd7route defaults on the Part 68 2D solutions** (`status=OK`;
`gr_verify` and `eval2008.pl` agree):

| benchmark | overflow | scored WL, Part 68 | **scored WL, pd7route** | change | post-2D time |
|---|---|---|---|---|---|
| adaptec1 | 0 | 8679261 | **5991707** | -31.0% | 107s |
| bigblue1 | 0 | 7798929 | **5751121** | -26.3% | 91s |
| newblue1 | 66 | 7919994 | **5056905** | -36.1% | 59s |

adaptec1 is now within 11.8% of NTHU-Route's 5359901 at the same zero
overflow. What is left against NTHU: 2D corners (+63%) and wire (+13%) --
the negotiation's trees carry more branches and turns than NTHU's
FLUTE-plus-L-shape starting routes, and recovery only reroutes a piece
within its own tree.

## Part 70: `gr/pd8route.cpp` -- whole-net tree rebuild (small)

`pd8route` (forked from `pd7route`) adds `rebuild2D` before piece recovery:
each net (<= 32 pins) regrown from its pins alone, Prim-style, by the
bend-aware search over only tile edges with room, kept if its shape cost
(tile edges + 3 x corner tiles) is strictly lower. `PD8ROUTE_REBUILD=0` is
byte-identical to pd7route (adaptec1 from the Part 68 solution, `cmp`).
Self-test 464 checks (a wasteful 3-pin tree, cost 34 -> 13, rebuilt to the
optimal 10 tile edges; an optimal L left untouched).

adaptec1 from the Part 68 solution (all `status=OK`, 0 overflow; pd7route
default 5991707):

| rebuild setting | nets rebuilt | 2D wire after rebuild | scored WL |
|---|---|---|---|
| margin 8, <= 32 pins | 48989 | 4172124 | 5991122 |
| **margin 16**, <= 32 pins | 49411 | 4133703 | **5958029** (-0.6%) |
| margin 8, <= 200 pins | 50378 | 4152523 | 5981535 |

A small gain: the rebuild does shorten ~49000 nets, but pd7route's piece
recovery had already captured almost all of it. Near zero overflow nearly
every edge is full, so capacity-respecting local moves are locked in. The
extra wire is made earlier, by the search stage: pd2route's slow tier
reroutes *whole* nets under a WL weight decayed to ~1e-3, so every part of
an escalated net takes congestion-only shapes, where NTHU reroutes only the
2-pin piece that crosses overflow and leaves the rest on its short initial
route. That is the runtime item too (next).

## Part 71: `gr/pd9route.cpp` -- the search stage's cost

Why NTHU-Route takes minutes where this pipeline takes hours (from its
code, Parts 52/58/63, and this project's traces): it reroutes only the
2-pin pieces crossing overflow (this project's slow tier regrows ~90000
whole nets in adaptec1's first slow round); its boxes are the piece's bbox
+ 10-25 growing 1/iteration (here net bbox + 48 growing 2/round to 200,
near-whole-grid); it tries a cheap monotonic route before any maze search
(here always MSMV A*, which degrades toward Dijkstra as the WL weight
decays to ~1e-4); it stops the main stage at an overflow threshold and
refines at most 20 iterations (here 60 fast + 150 slow + up to 500 region
rounds); and its FLUTE/edge-shifting/L-shape start leaves most nets never
rerouted. The whole-net slow tier is also where this project's extra
wirelength is made (Part 70).

`pd9route` (forked from `pd8route`): `PD9ROUTE_SLOW_PIECES=1` makes the
slow tier reroute only a net's pieces that cross overflow
(`rerouteNetPieces`, under the stage's pricing and acceptance), and
`PD9ROUTE_STOP_AT=N` ends the slow tier once 2D overflow <= N. Both off by
default. Self-test 467 checks (slow-tier pieces clear the detour fixture;
on a fixture that cannot converge, 21 rounds without the threshold vs 2
with it).

Screen, adaptec1, full pipeline from scratch (Part 68 settings, pd7/pd8
defaults): base; slow pieces; slow pieces + stop at 300.

**Part 71 results, adaptec1** (full pipeline from scratch, Part 68 settings
+ pd7/pd8 defaults; all `status=OK`, `gr_verify` and `eval2008.pl` agree):

| run | 2D after slow tier | real overflow | scored WL | runtime |
|---|---|---|---|---|
| base | 0 (slow round 104) | 0 | 5967713 | 5032s |
| `PD9ROUTE_SLOW_PIECES=1` | 0 (slow round 83) | 0 | 5988471 | 3334s (-34%) |
| **pieces + `PD9ROUTE_STOP_AT=300`** | 264 (stopped at slow round 45) | **0** | **5410200** (-9.3%) | **3283s (-35%)** |

**Stopping the slow tier early is a wirelength win as well as a runtime
one:** the region / refinement / recovery stages cleared the remaining 264
with no wire penalty, while the extra 40-60 slow rounds of congestion-only
negotiation (WL weight ~1e-3) had cost ~0.56M of scored wirelength.
**adaptec1: 0 overflow at 5410200 -- within 0.9% of NTHU-Route's 5359901.**
Running bigblue1 and newblue1 with the same settings (Part 71b).

**Part 71b -- the same settings (pieces + stop at 300) on bigblue1 and
newblue1**, full pipeline from scratch (`status=OK`, both scorers agree):

| benchmark | slow tier stopped at | region / refine | real overflow | max | scored WL | runtime |
|---|---|---|---|---|---|---|
| adaptec1 (71a) | 264 | -> 0 | **0** | 0 | 5410200 | 3283s |
| bigblue1 | 280 | region 280 -> 0 in 44 rounds | **0** | 0 | 5701936 | 3835s |
| newblue1 | 296 | region 296 -> 68 (500 rounds), refine -> 54 | **54** | 2 | 5021599 | 7741s |

**One pd9route command, one setting: 0 / 0 / 54 -- the best on all three
(newblue1 54 < the from-file 56), at 3.3-7.7 thousand seconds per
benchmark** vs ~4.8-21.8 thousand for Part 68's one command (newblue1 -64%,
bigblue1 -46%), with scored WL 5.41M / 5.70M / 5.02M (adaptec1 within 0.9%
of NTHU-Route).

## Part 73: the full ISPD 2008 suite -- downloaded

The 13 remaining ISPD 2008 benchmarks, downloaded 2026-09-28 from the
contest site (ispd.cc/contests/07/rcontest/benchmark/ and
ispd.cc/contests/08/benchmark/, 311 MB gzipped, 1.1 GB total in
`gr/bench/`, git-ignored) and parse-checked with `gr_verify`:

| benchmark | grid | nets |
|---|---|---|
| adaptec1 | 324 x 324 x 6 | 219794 |
| adaptec2 | 424 x 424 x 6 | 260159 |
| adaptec3 | 774 x 779 x 6 | 466295 |
| adaptec4 | 774 x 779 x 6 | 515304 |
| adaptec5 | 465 x 468 x 6 | 867441 |
| bigblue1 | 227 x 227 x 6 | 282974 |
| bigblue2 | 468 x 471 x 6 | 576816 |
| bigblue3 | 555 x 557 x 8 | 1122340 |
| bigblue4 | 403 x 405 x 8 | 2228903 |
| newblue1 | 399 x 399 x 6 | 331663 |
| newblue2 | 557 x 463 x 6 | 463213 |
| newblue3 | 973 x 1256 x 6 | 551667 |
| newblue4 | 455 x 458 x 6 | 636195 |
| newblue5 | 637 x 640 x 6 | 1257555 |
| newblue6 | 463 x 464 x 6 | 1286452 |
| newblue7 | 488 x 490 x 8 | 2635625 |

Runs queue behind Part 72 (which picks the router/settings), smallest
first, as `run_batch.sh` batches.

Plan (user instruction, 2026-09-28): Part 73a runs the five smallest new
benchmarks (adaptec2, newblue2, adaptec3, bigblue2, newblue4) on pd9route's
Part 71 settings. **The larger ones (adaptec4, adaptec5, bigblue3, bigblue4,
newblue3, newblue5, newblue6, newblue7) run only on whichever code proves
significantly faster** -- if Part 72's pattern stage (pd11route, CPU or
Metal) cuts runtime materially at equal quality, they run on that; they do
not start on pd9route.

## Part 72: `gr/pd11route.cpp` -- the batched pattern stage

**Part 72a** (adaptec1, full pipeline from scratch, Part 71 settings; all
`status=OK`, `gr_verify` and `eval2008.pl` agree):

| run | pattern stage | slow tier stopped at | real overflow | scored WL | runtime |
|---|---|---|---|---|---|
| pd9route (Part 71a) | -- (60 fast rounds + cleanup) | 264 | 0 | 5410200 | 3283s |
| **pd11route, CPU backend** | 602280 pieces, 30 rounds, 2D 175338 | 224 | **0** | **5414046** (+0.07%) | **2065s (-37%)** |
| pd11route_gpu, Metal backend | same, 2D 175338 | 264 | 0 | 5416153 | 2669s |

The pattern stage replaces the 60 fast rounds and the 30-pass cleanup at
the same quality and 37% less total runtime. Of 2938504 slow-tier piece
reroutes in the CPU run, 443400 (15%) were settled by an L/Z shape without a
maze search (pd10route's pattern-first).

Parity (small budget, `cmp`): pd10route with `PD10ROUTE_PATTERN_FIRST=0` ==
pd9route; pd11route with `PD11ROUTE_PATTERN=0` == pd10route.

**The CPU and Metal full runs are not byte-identical** although the kernel is
(pattern_selftest) and both runs leave the pattern stage at the same 2D
overflow; they diverge in the slow tier (stopped at 224 vs 264). Suspected
cause: the binaries differ -- `pd11route` is the one-line `g++ -O2` build,
`pd11route_gpu` CMake's Release (`-O3`) -- and the router's floating-point
paths (pricing, pow/exp) can round differently between them. Being isolated
at a small budget (same CMake binary, `GR_GPU=cpu` vs `=metal`; and the g++
build vs the CMake build on CPU).

**Resolved: it was the build, not the backend.** Isolation (small budget):
the same CMake binary gave byte-identical routes with `GR_GPU=cpu` and
`GR_GPU=metal`, but differed from the g++ binary. Cause: `/usr/local/bin/cmake`
is an Intel binary running under Rosetta, so CMake built **x86_64** targets
(`-arch x86_64` in flags.make) that ran translated on the M4 Pro -- different
floating point (arm64 fuses multiply-adds; this x86_64 build does not) and
slower (the Metal full run's 2669s vs 2065s). Fix in `gr/gpu/CMakeLists.txt`:
on Apple silicon (`sysctl hw.optional.arm64`), force
`CMAKE_OSX_ARCHITECTURES=arm64`, and pin `-O2` to match the one-line build.
Now **the g++ CPU build and the native CMake Metal build produce
byte-identical route files end to end.** Native kernel timing (1M pieces,
399x399): CPU 0.061s, Metal 0.014s (4.4x; the earlier 4.7x was measured
under Rosetta).

**Decision, per the Part 73 rule:** the pattern stage is significantly
faster at equal quality, so the eight larger ISPD 2008 benchmarks run on
`pd11route` (CPU build, the verified configuration) -- Part 73b.

**Part 73a -- the five smallest new benchmarks, pd9route, Part 71 settings
unchanged** (all `status=OK`, `gr_verify` and `eval2008.pl` agree):

| benchmark | nets | slow tier stopped at | region / refine | real overflow | max | scored WL | runtime |
|---|---|---|---|---|---|---|---|
| adaptec2 | 260159 | 230 | region -> 0 (20 rounds) | **0** | 0 | 5267444 | 1933s |
| newblue2 | 463213 | 286 | region -> 0 (7 rounds) | **0** | 0 | 7614709 | 141s |
| adaptec3 | 466295 | 280 | region -> 0 (22 rounds) | **0** | 0 | 13235180 | 5899s |
| bigblue2 | 576816 | 276 | region -> 0 (437 rounds) | **0** | 0 | 9443492 | 4148s |
| newblue4 | 636195 | 292 | region -> 168 (500), refine -> 148 | 148 | 2 | 13069979 | 21026s |

The one setting tuned on adaptec1 / bigblue1 / newblue1 generalizes: four
of five new benchmarks reach zero overflow with no per-benchmark tuning.
newblue4, like newblue1, keeps a residue after the region stage's 500
rounds. Across the eight ISPD 2008 benchmarks routed so far: seven at zero
(adaptec1-3, bigblue1-2, newblue2), newblue1 54, newblue4 148.

**Part 73b (in progress) -- the eight larger benchmarks, pd11route (CPU
build), Part 71 settings unchanged.** Finished so far (all `status=OK`,
`gr_verify` and `eval2008.pl` agree):

| benchmark | nets | 2D at slow-tier stop | real overflow | max | scored WL | runtime | peak RSS |
|---|---|---|---|---|---|---|---|
| adaptec4 | 515304 | 260 | **0** | 0 | 12234727 | 545s | 2822MB |
| adaptec5 | 867441 | 296 | **0** | 0 | 15851373 | 5253s | 3480MB |
| bigblue3 | 1122340 | 266 | **0** | 0 | 13189107 | 1605s | 2816MB |
| newblue5 | 1257555 | 212 | **0** | 0 | 23383430 | 6572s | 3920MB |
| newblue6 | 1286452 | 288 | **0** | 0 | 18321622 | 6382s | 3707MB |
| bigblue4 | 1133537 | 408 | 378 | 2 | 24851982 | 83888s | 4363MB |
| newblue7 | 1647412 | 634 | 106 | 2 | 36163657 | 97346s | 4665MB |

bigblue4: region box x 197-217 y 127-152 (9887 nets), 408 -> 406 over all
500 rounds -- ~22 of its 23.3 hours for 2 units; refinement 406 -> 378.
newblue7: region box x 98-234 y 240-354 (265927 nets), 634 -> 138 (500
rounds), refinement -> 106. Against the contest routers (LMgr Table II,
above): bigblue4 378 is worse than all four (166-256), wirelength +9% vs
NTHU-Route 2.0's 22.80M; newblue7 106 beats NTUgr (364) and FastRoute 3.0
(580), behind NTHU-Route 2.0 (68) and LMgr (88), wirelength +1.8% vs
NTHU's 35.52M. The region stage's MSMV margin grows boxInit + 2 per round
with no cap (10 -> 1008 tiles by round 499), which is a likely reason its
late rounds cost ~2.5 min each even on bigblue4's 20 x 25-tile box.

Running: newblue3, bigblue4, newblue7. **Thirteen ISPD 2008 benchmarks
routed so far, eleven at zero overflow** (adaptec1-5, bigblue1-3,
newblue2, newblue5, newblue6; newblue1 54, newblue4 148), from one
setting with no per-benchmark tuning.

Mid-run (2026-09-29 08:24), 2D overflow, best so far: bigblue4 region
stage done (500 rounds, best 406, reached long before round 500),
refinement 378 at iteration 55 of 60; newblue7 region round 375, best 166,
~2.4 min/round; newblue3 region round 229, best 38836 (flat within 0.1%
since round ~50), ~13 min/round -- ~59 more hours of region rounds alone.
Slow tier (all 150 rounds, none reached the 300 stop): newblue3 best 41000
(42968 at round 50, 41682 at 100), bigblue4 408, newblue7 634.

For reference, the contest routers on these three (LMgr, ISQED 2013,
Table II: all re-run on one 1.6 GHz quad core; overflow / WL x 1e5 /
minutes): newblue3 -- NTHU-Route 2.0 31810 / 106.3 / 22 (iterations cut
to finish), NTUgr 31024 / 188.3 / 884 (from its paper), FastRoute 3.0
31642 / 109.5 / 271, LMgr 34860 / 105.4 / 245; bigblue4 -- 256 / 228.0 /
135, 184 / 240.5 / 1237, 228 / 244.2 / 52, LMgr 166 / 224.8 / 197;
newblue7 -- 68 / 355.2 / 1491, 364 / 365.4 / 1404, 580 / 358.7 / 347,
LMgr 88 / 348.1 / 183. Same table, for the two finished residues:
newblue1 164 / 32 / 64 / LMgr 0 (ours 54); newblue4 222 / 152 / 208 /
LMgr 144 (ours 148). No contest router reaches zero on newblue3,
bigblue4, newblue4 or newblue7.

## Part 74 -- pd12route: a plateau stop for the hotspot and refinement stages

The region (hotspot) stage runs all `PD5ROUTE_REGION_ROUNDS` (500) and
refinement all `PD4ROUTE_REFINE_ITERS` (60) unless overflow reaches zero;
on the large non-zero benchmarks that is most of the run (Part 73b). Both
stages already keep the best state seen, so a stop can only lose
improvements later rounds would have found. `gr/pd12route.cpp`
(forked from pd11route): `PD12ROUTE_REGION_PLATEAU=N` / `_REFINE_PLATEAU=N`
stop a stage once its best has not fallen by `max(1, GAIN% of the best N
rounds earlier)` over the last N rounds (`PD12ROUTE_PLATEAU_GAIN`, default
1), applied only while the best is `>= PD12ROUTE_PLATEAU_MIN`. All off by
default (pd11route byte for byte); the stage summaries now print each
stage's wall time. 504 checks (474 inherited), including: on a stuck
fixture (three nets, one-track row) each stage in both whole-net and piece
mode runs all 40 rounds without a window, exactly 5 with window 5, and all
40 again when the floor is one above the stuck overflow.

**Replay of the rule on the recorded traces (no router time)** -- round at
which it would stop -> best at that point, vs the full run:

| run | stage | full run | W25 g1% | W50 g1% | W100 g1% |
|---|---|---|---|---|---|
| newblue1 (Part 71) | region | 500 rounds, best 68 | 169 -> 94 | 194 -> 94 | 244 -> 94 |
| bigblue2 (73a) | region | 437 rounds -> **0** | 104 -> 10 | 129 -> 10 | 337 -> 4 |
| newblue4 (73a) | region | 500, best 168 | 86 -> 220 | 259 -> 174 | 408 -> 168 |
| bigblue4 (73b) | region | 500, best 406 | 99 -> 414 | 154 -> 406 | 204 -> 406 |
| newblue7 (73b) | region | 377 so far, best 166 | 174 -> 212 | 199 -> 212 | (no stop) |
| newblue3 (73b) | region | 231 so far, best 38836 | 34 -> 38836 | 59 -> 38836 | 109 -> 38836 |
| newblue4 (73a) | refine | 60, best 148 | W5: 11 -> 150 | W10: 16 -> 150 | W20: 26 -> 150 |

A window alone would cost results: bigblue2 reached zero only at round 437
after long flat stretches (10 -> 4 -> 0), and newblue1's best region state
came late (68 vs 94). The long tail is where small residues close. Hence
the floor: the rule applies only to residues far from zero. With
`MIN=1000` only newblue3 (~39000, unroutable to zero by every published
router) is affected -- its region stage would stop at round 59 instead of
500 with the same best (38836), roughly 70-90 hours saved. The slow tier
is not given the rule: on newblue3 it still improves ~1% per 25 rounds at
round 150.

**Runs (batch `part74.jobs`)**: parity at a small budget on adaptec1 with
region (8 rounds) and refinement (4 iterations) exercised -- pd11route vs
pd12route default, and pd12route with windows set but floor 10^9 -- all
three route files must be byte-identical; pd12route with an active window
(floor 0, 50% gain) must stop early and stay legal. Then newblue3, Part 71
settings + `PD12ROUTE_REGION_PLATEAU=50 _REFINE_PLATEAU=10 _PLATEAU_GAIN=1
_PLATEAU_MIN=1000`, alongside the still-running pd11route newblue3.

Parity (all `status=OK`, `gr_verify` = `eval2008.pl`): pd11route,
pd12route default and pd12route with windows 2/1 but floor 10^9 -- route
files **byte-identical** (156556683 bytes each), real overflow 310, WL
5470515; stage times now reported (region 8 rounds 79.1s, refinement 4
iterations 10.3s). With an active window (floor 0, gain 50%): region stops
after 2 rounds ("plateau stop", best still the starting 26046 -- round 0
reroutes every net in the box and overflow spikes before it falls, so a
window must be longer than that recovery), refinement after 1; legal,
real overflow 21986.

**newblue3 on pd12route** (Part 71 settings + region window 50, refine
window 10, gain 1%, floor 1000; `status=OK`, `gr_verify` = `eval2008.pl`):
real overflow **38704**, max 364, scored WL 10779692, **29754s (8.3 h)**,
peak RSS 3144MB. Slow tier 150 rounds -> 41000; region stopped by the
plateau rule after **59 rounds, 2035s** (41000 -> 38836: 62286 at round 0,
39052 at 9, 38864 at 19, then flat); refinement stopped after 10
iterations, 87s (38836 -> 38704); tree rebuild and WL recovery 7886356 ->
7598221 2D wire. The pd11route run of the same benchmark was, at the same
moment, 37 h in and at region round 265 with the same best 38836.

The stage times say where pd11route's region hours went: 59 rounds took
34 s each here, while pd11route's rounds 200+ take 10-13 minutes each --
the region MSMV margin grows 10 + 2 per round, uncapped, so late rounds
search boxes of hundreds of tiles for every net still overflowed. The
plateau stop removes the rounds; a margin cap would make each remaining
round cheap (the likely fix for bigblue4, whose residue is under the
floor).

Against the contest routers (LMgr Table II): 38704 is 22-25% above NTUgr
31024, FastRoute 3.0 31642 and NTHU-Route 2.0 31810; wirelength 10.78M vs
NTHU 10.63M (+1.4%) and LMgr 10.54M (+2.3%).

**pd11route newblue3 stopped** (2026-09-29, on the user's instruction):
killed at 37 h, region round 273 of 500, best 38836 (reached at round 16,
unchanged for the ~257 rounds since); `status=ROUTER_FAILED exit=143`, no
route file. pd12route's result above (38704) is newblue3's entry.

## Part 75 -- the full ISPD 2008 suite

All sixteen benchmarks, one setting (Part 71's), no per-benchmark tuning;
every result `status=OK`, `gr_verify` and `eval2008.pl` agreeing on total
overflow, max overflow and wirelength. Router: pd9route (Parts 71, 73a),
pd11route (73b; same pipeline plus the pattern stage), pd12route (74;
plus the plateau stop -- newblue3 only). Reference: NTHU-Route 2.0, the
2008 winner, as re-run in LMgr (ISQED 2013) Table II on a 1.6 GHz quad
core (overflow / WL x 1e5 / minutes), and the best overflow of the four
routers in that table (NTHU-Route 2.0, NTUgr, FastRoute 3.0, LMgr).

| benchmark | overflow | max | scored WL | runtime | router | NTHU-Route 2.0 (LMgr re-run) | best of four | WL vs NTHU |
|---|---|---|---|---|---|---|---|---|
| adaptec1 | **0** | 0 | 5410200 | 3283s | pd9route | 0 / 53.6 / 10 min | 0 | +0.9% |
| adaptec2 | **0** | 0 | 5267444 | 1933s | pd9route | 0 / 52.4 / 5 min | 0 | +0.5% |
| adaptec3 | **0** | 0 | 13235180 | 5899s | pd9route | 0 / 131.5 / 10 min | 0 | +0.6% |
| adaptec4 | **0** | 0 | 12234727 | 545s | pd11route | 0 / 121.8 / 3 min | 0 | +0.4% |
| adaptec5 | **0** | 0 | 15851373 | 5253s | pd11route | 0 / 155.4 / 27 min | 0 | +2.0% |
| bigblue1 | **0** | 0 | 5701936 | 3835s | pd9route | 0 / 55.7 / 20 min | 0 | +2.4% |
| bigblue2 | **0** | 0 | 9443492 | 4148s | pd9route | 86 / 90.0 / 21 min | 0 | +4.9% |
| bigblue3 | **0** | 0 | 13189107 | 1605s | pd11route | 32 / 130.7 / 15 min | 0 | +0.9% |
| bigblue4 | 378 | 2 | 24851982 | 83888s | pd11route | 256 / 228.0 / 135 min | 166 | +9.0% |
| newblue1 | 54 | 2 | 5021599 | 7741s | pd9route | 164 / 46.0 / 28 min | 0 | +9.2% |
| newblue2 | **0** | 0 | 7614709 | 141s | pd9route | 0 / 75.9 / 2 min | 0 | +0.3% |
| newblue3 | 38704 | 364 | 10779692 | 29754s | pd12route | 31810 / 106.3 / 22 min | 31024 | +1.4% |
| newblue4 | 148 | 2 | 13069979 | 21026s | pd9route | 222 / 128.9 / 56 min | 144 | +1.4% |
| newblue5 | **0** | 0 | 23383430 | 6572s | pd11route | 18 / 231.4 / 40 min | 0 | +1.1% |
| newblue6 | **0** | 0 | 18321622 | 6382s | pd11route | 0 / 176.9 / 52 min | 0 | +3.6% |
| newblue7 | 106 | 2 | 36163657 | 97346s | pd11route | 68 / 355.2 / 1491 min | 68 | +1.8% |

**Eleven of sixteen at zero overflow**, every benchmark any router in the
table routes to zero except newblue1 (LMgr reaches 0; ours 54). Against
NTHU-Route 2.0's re-run: better overflow on five (bigblue2, bigblue3,
newblue5 -- zero where it is not -- and newblue1, newblue4), worse on
three (bigblue4, newblue3, newblue7). Wirelength within +0.3% to +2.4% on
twelve; +3.6% to +9.2% on bigblue2, newblue6, bigblue4 and newblue1.
(LMgr's re-run of NTHU-Route 2.0 left residues on some benchmarks its
contest submission cleared -- the comparison is to that re-run, on slower
hardware, not to the contest-day numbers, which are no longer online.)

Runtime is the gap: 2 minutes (newblue2) to 27 hours (newblue7) here, vs
2 minutes to 25 hours for NTHU-Route 2.0 on a slower machine, and the
slow cases are the non-zero ones -- the region stage's uncapped margin
growth (Part 74) and the slow tier's 150 rounds on residues that never
reach the 300 stop. Next runtime targets, in order: cap the region
margin; extend the plateau rule's evidence to the slow tier; then roadmap
step 4 (multithreading).

## Part 76 -- pd13route: a hard cap on the late stages' search margins

`gr/pd13route.cpp` (forked from pd12route): `PD13ROUTE_REGION_BOX_CAP` and
`PD13ROUTE_REFINE_BOX_CAP` cap the region margin (10 + 2 per round) and
the refinement margin (10 + 5 per iteration under Part 71's settings) --
the one breadth knob CLAUDE.md's box rule says must be capped that the
slow tier had (NV11ROUTE_BOX_CAP 200) and these stages did not. Both 0 by
default (pd12route byte for byte). 517 checks.

Parity (adaptec1, small budget, region and refinement exercised; all
`status=OK`, `gr_verify` = `eval2008.pl`): pd12route, pd13route default,
and pd13route with both caps 100000 -- route files **byte-identical**,
real overflow 310, WL 5470515.

Screens (batch `part76.jobs`, Part 71 settings + Part 74 plateau settings
+ both caps 200): bigblue2 (the case whose zero came at region round 437,
margin 882 -- the risk), newblue4, bigblue4 (the runtime target: 23.3 h,
~22 h of it in region rounds), newblue1, newblue7. Judged on overflow
(must not lose bigblue2's zero or worsen the residues) and runtime.

## Part 77 -- ISPD 2024 format: verifier cross-checked against the contest evaluator

Roadmap step 3, verification first. `gr/g24_verify.cpp` replicates the
contest evaluator's scoring (`ISPD24_FORMAT.md`); `gr/g24_lroute.cpp` is
the throwaway L-router that exists to feed it. Downloaded (user-approved)
from `cuhk-eda/InstantGR`: `run/evaluator.cpp` (the evaluator source,
compiled locally, BSD-3 license kept beside it) and the small benchmark
`mempool_tile_rank` (.cap 5.6 MB, .net 20 MB: 135666 nets, 482811 pins,
556569 access points -- 552966 on metal 1, 2895 on metal 3, 708 on metal 5
-- on a 429 x 581 x 10 grid, first column pitch 1995, the rest 3800).

| solution | WL cost | via cost | overflow cost | total | opens |
|---|---|---|---|---|---|
| empty | 0 | 0 | 2538617.8007 | -- | (all missing) |
| L-router, lowest layer pair (ours = official) | 7828346.6313 | 4359252.0000 | 9327069661506788392960.0000 | 9327069661506799927296.0000 | 0 |
| L-router, all layer pairs (ours = official) | 7828346.6313 | 14457436.0000 | 214189305.5220 | 236475088.1534 | 0 |

Every printed digit agrees in both. The first L-router run left 7575 nets
open in both scorers (the official one exits on the first): nets whose
pins share one access point, for which it wrote nothing -- the evaluator
only reaches access points a net covers.

## Part 78 -- IDEAS_TEST_PLAN phase 0 and phase 1

**Phase 0, noise floor** (batch `part78.jobs`, queued): newblue1 and
newblue4 on Part 76's configuration (pd13route, caps 200) with the pattern
stage's color cell 7 and 9 instead of 8. With Part 76's c200 runs these
give a three-point spread per benchmark for judging quality ideas.

**Phase 1, diagnostics** -- `gr/grdiag.cpp` (analysis tool, self-tested;
its overflow, wire and via totals match `gr_verify` exactly on every file
below):

| route | real overflow | LA share | hotspots | nets: one box / per-hotspot sum | wire over pin MST | vias | vias per corner |
|---|---|---|---|---|---|---|---|
| NTHU-Route 2.0 adaptec1 | 0 | -- | -- | -- | +2.17% | 1774596 (33.1%) | 1.73 |
| ours adaptec1 (p11) | 0 | -- | -- | -- | +1.94% | 1836126 (33.9%) | 1.69 |
| newblue1 (p9s) | 54 | 0 | 5 (40 in one 17x18) | 8555 / 13191 | **+12.37%** | 2327759 (46.4%) | 1.73 |
| newblue4 (s) | 148 | 0 | 1 (19x15) | 7986 / 7986 | +1.54% | 4771537 (36.5%) | 1.65 |
| bigblue4 (l) | 378 | 0 | 1 (9x16) | 9146 / 9146 | **+13.38%** | 11443228 (46.0%) | 2.03 |
| newblue7 (l) | 106 | 0 | 7 (70 in one 9x7) | **246366 / 42867** | +5.91% | 16921917 (46.8%) | 1.98 |
| bigblue2 (s) | 0 | -- | -- | -- | +6.67% | 4374465 (46.3%) | 1.83 |
| newblue6 (l) | 0 | -- | -- | -- | +5.92% | 7756495 (42.3%) | 1.85 |

Findings:
1. **Every residue is the 2D stage's** -- layer assignment adds nothing on
   any benchmark. Via work (idea 5) cannot reduce overflow here.
2. **Hotspots**: one on newblue4 and bigblue4; newblue1's five sit inside
   one 38 x 30 area (per-hotspot boxes touch *more* nets, overlapping).
   Only newblue7's are scattered -- seven boxes would touch 5.7x fewer
   nets than one. Idea 2 is a newblue7 idea.
3. **The wirelength gap tracks detour, not via share.** Wire over the
   pins' rectilinear MST (NTHU on adaptec1: +2.2%) ranks the benchmarks
   the way their gap to NTHU does: bigblue4 +13.4% / gap +9.0%, newblue1
   +12.4% / +9.2%, bigblue2 +6.7% / +4.9%, newblue6 +5.9% / +3.6%,
   newblue4 +1.5% / +1.4% (newblue7 +5.9% / +1.8% the exception). On
   adaptec1, against NTHU's own route, our wire is shorter (3577920 vs
   3585305) and the whole gap is vias (+61530: 10% more 2D corners).
4. **bigblue4's detour is global, newblue1's is local**: nets touching
   bigblue4's hotspot carry 88554 of its 1.73M excess; newblue1's hotspot
   nets carry ~173000 of 343208 (+154% over their MST).
5. A likely cause of global detour: the present-demand WL term decays to
   ~1e-7 by slow round 150 (traces: `wl 6.2e-08`), so with margins up to
   200 the slow tier chooses shapes on congestion alone, and WL recovery
   can shorten only through edges with room. `PD1ROUTE_WL_FLOOR` holds the
   term up; it was screened only at floor 1 under the revert rule (Part
   58, S3: +59.6% overflow), never at small floors under keep.

**D3, slow-tier plateau replay** (floor 1000, window 25, gain 1%): fires
only on newblue3 -- round 96 of 150, best 41744 handed to the region
stage instead of 41000 (~2.5 of its 8.3 hours). Every other slow tier
reaches its 300 stop or is still gaining > 1% per 25 rounds.

## Part 79 -- WL floor screens (IDEAS_TEST_PLAN phase 2, step 1), queued

`PD1ROUTE_WL_FLOOR` 0.01 and 0.05 on bigblue2 and newblue6 (zero overflow,
wire +6.7% / +5.9% over the pins' MST, Part 78), Part 76's configuration
(pd13route, caps 200). Baselines: c200_bigblue2 (Part 76) and l_newblue6
(Part 73b: 0 / 18321622 / 6382s; equivalent, since neither the caps nor
the plateau stop engage on it). Pass: scored wirelength better by > 0.5%
with overflow still 0; then newblue1 against its noise spread.

**Results so far (2026-09-30 07:37; all `status=OK`, `gr_verify` = `eval2008.pl`).**

Part 76, caps 200 (pd13route; the baselines are pd9route's Part 73a runs,
which lack the pattern stage, so these compare pipelines, not only caps):

| benchmark | overflow | scored WL | runtime | baseline (pd9) |
|---|---|---|---|---|
| bigblue2 | **0** | 9508527 | 2735s | 0 / 9443492 / 4148s |
| newblue1 | 48 | 4824414 | 5608s | 54 / 5021599 / 7741s |
| newblue4 | 144 | 13660587 | 8871s | 148 / 13069979 / 21026s |

Part 78, noise floor (same configuration, pattern color cell 7 / 9):
newblue1 46 / 4882558 / 5106s and 48 / 4803733 / 5273s -- with c200:
overflow 46-48, WL 4.80-4.88M (1.6% spread). newblue4 cell 7: 144 /
13190527 / 9070s -- WL 13.19-13.66M across two runs (3.5% spread) at
identical overflow. **Wirelength noise between quality-neutral
perturbations reaches 3.5%, far above the plan's 0.5% pass line.**

Part 79, WL floors on bigblue2: 0.01 -> 0 / 9434596 / 2802s (-0.78% vs
c200_bigblue2); 0.05 -> 0 / 9476788 / 2636s (-0.33%). Both inside the
spread the noise runs show elsewhere; bigblue2's own noise is unmeasured.

**Part 79 complete** (all `status=OK`, `gr_verify` = `eval2008.pl`),
with the mechanism checked directly by `grdiag` (wire over the pins' MST):

| run | overflow | scored WL | vs baseline | wire | vias | detour | runtime |
|---|---|---|---|---|---|---|---|
| bigblue2 c200 (baseline) | 0 | 9508527 | -- | 5113222 | 4395305 | +7.60% | 2735s |
| bigblue2 floor 0.01 | 0 | 9434596 | -0.78% | 5061041 | 4373555 | +6.50% | 2802s |
| bigblue2 floor 0.05 | 0 | 9476788 | -0.33% | 5089076 | 4387712 | +7.09% | 2636s |
| newblue6 l (baseline) | 0 | 18321622 | -- | 10565127 | 7756495 | +5.92% | 6382s |
| newblue6 floor 0.01 | 0 | 18524032 | +1.10% | 10726147 | 7797885 | +7.54% | 6062s |
| newblue6 floor 0.05 | 0 | 18333295 | +0.06% | 10576574 | 7756721 | +6.04% | 6106s |

No consistent direction, and not monotone in the floor: the stronger
floor (0.05) cuts detour less than 0.01 on bigblue2, and 0.01 *adds*
detour on newblue6. If the decayed WL term were the cause of the global
detour, raising it should cut detour more the higher it goes. It does
not, so on this evidence the WL-term decay is **not** the detour's
source. Verdict pending bigblue2's own noise runs (Part 78b); detour
itself varies by ~1 point between runs.

**Part 78 noise floor complete.** newblue4 cell 9: 146 / 13661746 /
9408s. Three-run spreads (c200 + cells 7, 9), same configuration:
newblue1 overflow 46-48, WL 4803733-4882558 (1.6%); newblue4 overflow
144-146, WL 13190527-13661746 (3.6%). Overflow is stable to a few units;
wirelength is not.

**Part 78b, bigblue2 noise floor** (c200 configuration, pattern cell 7 /
9; `status=OK`, `gr_verify` = `eval2008.pl`): cell 7 -> overflow **2**
(max 2) / 9622819 / 2684s, detour +9.36%; cell 9 -> 0 / 9675318 / 2662s,
detour +10.23%. With c200 (0 / 9508527, +7.60%): **bigblue2's zero is
fragile** -- one of three quality-neutral perturbations ends at 2 -- and
its WL spread is 9508527-9675318 (1.8%), detour +7.6 to +10.2%.

Against that spread, both WL floors land **below all three baseline
runs** on bigblue2 in WL (9434596, 9476788) and detour (+6.50%, +7.09%),
and keep the zero. On newblue6 they do not (0.01: +1.10% WL, +7.54%
detour; 0.05: +0.06%) -- but newblue6's own noise is unmeasured. Verdict:
**suggestive on bigblue2, contradicted or neutral on newblue6; not
established.** The guard rule "losing a zero rejects" also needs revising:
a neutral perturbation can lose bigblue2's zero, so one lost zero is
evidence, not proof.

**Part 78c, newblue6 noise floor** (c200 configuration, pattern cell 7 /
9; `status=OK`, `gr_verify` = `eval2008.pl`): cell 7 -> 0 / 18615319 /
6092s, detour +8.30%; cell 9 -> 0 / 18522873 / 6248s, detour +7.54%. With
the Part 73b run (0 / 18321622, +5.92%; same configuration in effect):
WL 18321622-18615319 (1.6%), detour +5.9 to +8.3%.

**WL floor verdict (Part 79), both benchmarks against their own noise:**

| | baseline runs (mean) | floor 0.01 | floor 0.05 |
|---|---|---|---|
| bigblue2 WL | 9508527 / 9622819 / 9675318 (9602221) | 9434596 (-1.75% vs mean, below all three) | 9476788 (-1.31%, below all three) |
| newblue6 WL | 18321622 / 18615319 / 18522873 (18486605) | 18524032 (+0.20%, inside) | 18333295 (-0.83%, inside) |

Neither floor hurts; 0.05 is at or below the baseline mean on both, 0.01
helps bigblue2 and is neutral on newblue6. A small, benchmark-dependent
gain at best -- nothing near the 4-9% gap to NTHU-Route 2.0. Next check
where overflow is the reliable signal: newblue1 (overflow noise 46-48).

**Region stage cost, per benchmark (c200 runs):** bigblue2 500 rounds in
277s (its zero comes late -- rounds 308-442 across the five runs -- but
cheaply); newblue1 500 in 4181s; newblue4 500 in 5802s; **bigblue4 500 in
54346s (15.1 h) for 408 -> 404**, even at margin cap 200 (vs ~22 h
uncapped). bigblue4 finished at 2D 370 after refinement, 72896s total
(vs 83888s uncapped); verification pending.

**bigblue4, caps 200, verified**: `status=OK`, `gr_verify` = `eval2008.pl`:
overflow **370** (max 2), scored WL 24861323, 72897s (20.2 h), peak RSS
3769MB -- vs uncapped 378 / 24851982 / 83888s: overflow -8, WL +0.04%,
runtime -13%.

## Part 80 -- pd14route: a work-gated plateau stop for the region stage

The floor-gated stop (Part 74) cannot reach bigblue4 (residue ~400 < floor
1000), and the floor exists only to protect late zeros like bigblue2's --
which cost bigblue2 almost nothing (500 rounds in 198-361 s) while
bigblue4's 500 cost 15.1 h. `gr/pd14route.cpp` adds a second rule gated on
the *work* of the recent rounds instead of the residue's size:
`PD14ROUTE_REGION_COST_WINDOW=N` / `_COST_POPS=P` stop the region stage
when its best has not fallen by max(1, gain%) over the last N rounds and
those N rounds expanded at least P search nodes. Work is counted as
search-node expansions, not wall time, so stops and routes stay
deterministic. Off by default; 528 checks (stuck fixture: all 40 rounds
with no window, exactly 5 with window 5 and the threshold at 5 rounds'
work, all 40 with the threshold above the total work; the count is
identical across runs).

Planned setting: window 100 (the Part 74 replay: W100 g1% leaves newblue7
unstopped -- its region best kept improving to round 375 -- and stops
bigblue4 at round 204 with its full-run best), P = about an hour's worth
of expansions per 100 rounds (bigblue4 ~10900 s per 100 rounds; newblue4
~1160 s, newblue1 ~840 s, bigblue2 ~55 s), calibrated from the parity
runs' expansions per second. Queued (`part80.jobs`): parity on adaptec1 at
a small budget -- pd13route, pd14route default, pd14route with a window
and an unreachable threshold -- route files must be byte-identical.

**newblue7, caps 200, verified** (`status=OK`, `gr_verify` = `eval2008.pl`):
108 (max 2) / 36104404 / 70692s (19.6 h), peak RSS 5528MB -- vs uncapped
106 / 36163657 / 97346s: overflow +2 (inside the 2-unit overflow noise
seen elsewhere), WL -0.16%, **runtime -27%**. Region 634 -> 136 in 500
rounds, 38528 s (~77 s a round); refinement -> 108. Part 76 summary: caps
200 kept or improved every overflow (bigblue2 0, newblue1 48 vs 54,
newblue4 144 vs 148, bigblue4 370 vs 378, newblue7 108 vs 106) at -13% to
-58% runtime.

**Window choice for pd14route, replayed on the region traces** (gain 1%;
the work gate decides which benchmarks the rule may touch):

| run | start -> full best (round) | W100 | W150 | W200 |
|---|---|---|---|---|
| newblue7 c200 | 634 -> 136 (487) | no stop | no stop | no stop |
| newblue7 uncapped | 634 -> 138 (493) | no stop | no stop | no stop |
| bigblue4 c200 | 408 -> 404 (115) | 100 -> 408 | **150 -> 404** | 200 -> 404 |
| newblue4 c200 | 296 -> 172 (415) | 332 -> 178 | 382 -> 178 | no stop |
| newblue1 c200 | 298 -> 64 (397) | 497 -> 64 | no stop | no stop |

W150: bigblue4 stops at round 150 with its full-run best (~350 rounds,
~10 h saved); newblue7 never plateaus; newblue4 and newblue1 would, so the
work threshold must sit above their cost (~1740 s and ~1250 s per 150
rounds) and well below bigblue4's (~16300 s): P = the expansions of about
5000 s.

**Part 80 parity** (adaptec1, small budget; `status=OK`, `gr_verify` =
`eval2008.pl`): pd13route, pd14route default, and pd14route with window 2
and an unreachable threshold -- route files **byte-identical** (310 /
5470515). Calibration: the region stage expanded 503216413 nodes in 78.7 s
(**6.4M expansions per second**, 54-66M per round). P = 3.2e10 (~5000 s):
newblue4 ~1.1e10 per 150 rounds, bigblue4 ~1.0e11 -- about 3x margin each
side. Queued (`part80b.jobs`): bigblue4, window 150, gain 1%, P 3.2e10.

**Part 79b, WL floors on newblue1** (c200 configuration; `status=OK`,
`gr_verify` = `eval2008.pl`): floor 0.01 -> 48 / 4877199 / 5027s, detour
+7.20%; floor 0.05 -> 46 / 4862174 / 4959s, detour +6.66%. The three
neutral runs (Part 78): overflow 46-48, WL 4803733-4882558, detour +4.91
to +7.36% -- **both floors fall inside the noise on every measure.**

**WL floor verdict, all three benchmarks: no effect distinguishable from
noise** (bigblue2 the only case outside its spread, newblue6 and newblue1
inside). Idea 4's "decayed WL term" hypothesis is retired. Side finding:
newblue1's detour is +4.9 to +7.4% on the pd13route pipeline against
+12.4% on the Part 71 pd9route run, and its WL 4.80-4.88M against 5.02M --
the pattern stage (pd11route) removed about half of newblue1's excess.

**Part 80b, bigblue4 with the work-gated stop** (pd14route, window 150,
gain 1%, P 3.2e10, otherwise c200; `status=OK`, `gr_verify` =
`eval2008.pl`): **370 (max 2) / 24861323 / 27722 s (7.7 h)**, peak RSS
4571MB. Region stopped at round 150 by the work-gated rule (408 -> 404,
9685 s, 7.96e10 expansions -- 8.2M/s here); refinement -> 370. Overflow
and wirelength are **identical** to the capped run's (370 / 24861323): the
replay's prediction held exactly -- the 350 rounds removed found nothing.
Runtime: 72897 s -> 27722 s (-62%); against the original uncapped run
(378 / 24851982 / 83888 s), -67% at 8 less overflow.

## Part 81 -- the standard configuration

`gr/standard.env` (with `gr/pd14route`): Part 71's settings, the Part 72
pattern stage (default on), Part 74's floor-gated plateau (W50 / 1% /
floor 1000; refinement W10), Part 76's margin caps (200 / 200), Part 80's
work-gated plateau (W150, 3.2e10 expansions). `run_batch.sh` now expands
`@file` tokens into the file's VAR=value lines. Benchmarks whose last runs
were on the same pipeline with none of the new stops firing need no rerun
(the stops only end loops early; the caps only bind past region round 95
or refinement iteration 38): adaptec1, adaptec4-5, bigblue3, newblue5-6
(Part 73b, pd11route: regions finished in <= 43 rounds), bigblue2,
newblue1, newblue4, newblue7 (Part 76, pd13route caps 200: work gate below
threshold or never plateaued), bigblue4 (Part 80b), newblue3 (Part 74:
stopped at round 59 by the floor rule, caps never binding). Queued
(`part81.jobs`) on the standard configuration: newblue2, adaptec2,
bigblue1, adaptec3 (last run on pd9route, no pattern stage), and adaptec4
as an equivalence check (must reproduce 0 / 12234727).

## Part 82 -- where the detour comes from: a per-stage trace

`gr/pd15route.cpp` (forked from pd14route): `PD15ROUTE_DETOUR_TRACE=1`
prints, after the pattern stage, the slow tier, the region stage,
refinement, the tree rebuild and WL recovery, the non-giant nets' 2D wire,
the sum of their pin-tile MSTs, and the excess carried by nets over 1.5x
their MST. Trace only (routes = pd14route's). 531 checks. Queued
(`part82.jobs`, standard configuration): bigblue2 (must reproduce c200's 0
/ 9508527), newblue6, newblue1, bigblue4.

**Part 81 results** (standard configuration; all `status=OK`, `gr_verify`
= `eval2008.pl`):

| benchmark | overflow | scored WL | runtime | previous (pd9route) |
|---|---|---|---|---|
| newblue2 | **0** | 7635770 | 174s | 0 / 7614709 / 141s |
| adaptec2 | **0** | 5261814 | 1251s | 0 / 5267444 / 1933s |
| bigblue1 | **0** | 5754158 | 2778s | 0 / 5701936 / 3835s |
| adaptec3 | **0** | 13253518 | 2062s | 0 / 13235180 / 5899s |
| adaptec4 (equivalence check) | **0** | **12234727** | 558s | 0 / 12234727 / 545s (pd11route) |

All four zeros kept; WL -0.11% to +0.92% (inside the 1.6-3.6% noise);
runtime -28% to -65% except newblue2 (2-3 min either way). adaptec4
reproduces its pd11route result exactly, as the equivalence argument
predicts. The ISPD 2008 suite on the standard configuration: 11 of 16 at
zero (newblue1 48, newblue4 144, newblue7 108, bigblue4 370, newblue3
38704).

**Part 82, bigblue2 detour trace** (`status=OK`; route file
**byte-identical** to c200_bigblue2's, 0 / 9508527):

| after | 2D wire | over pin MST | nets > 1.5x MST (excess they carry) |
|---|---|---|---|
| pattern stage | 4645070 | -0.28% | 0 |
| **slow tier** | 6161985 | **+32.28%** | 20175 (1176533) |
| region stage | 5802831 | +24.57% | 15845 (828741) |
| refinement | 5802854 | +24.57% | 15846 (828766) |
| tree rebuild | 5244954 | +12.59% | 6817 (326913) |
| WL recovery | 5078309 | +9.02% | 5247 (234050) |

The detour enters in the slow tier: the pattern stage's routes sit at the
pins' MST (its Z/L shapes over MST pieces), the slow tier adds 1.5M tile
edges (+32%), and the stages after it recover 72% of that. What is left
(+9.0% here; +7.6% by grdiag over the 3D route, which counts segments, not
charge records) is the gap.

**Part 82, more traces** (standard configuration; newblue1 and newblue6
reproduce their earlier results exactly -- 48 / 4824414 and 0 / 18321622):

| benchmark | after pattern | after slow tier (nets > 1.5x MST: count, share of excess) | after region | after refinement | after rebuild | after WL recovery |
|---|---|---|---|---|---|---|
| bigblue2 | -0.28% | **+32.28%** (20175, 78%) | +24.57% | +24.57% | +12.59% | +9.02% |
| newblue6 | -0.20% | **+34.42%** (25493, 85%) | +14.26% | +14.26% | +8.71% | +7.16% |
| newblue1 | -0.30% | **+24.15%** (4256, 90%) | +12.59% | +12.00% | +8.43% | +7.00% |
| bigblue4 | -0.17% | **+29.85%** (29993, 89%) | (running) | | | |

Same shape on every benchmark: the pattern stage ends at the pins' MST,
the slow tier adds +24-34%, and the stages after it recover 70-80%. The
slow tier's excess is concentrated -- 1.6-3.4% of the nets carry 78-90% of
it -- so a fix aimed at those nets touches very little of the design.

## Part 83 -- detour reclaim (pd16route)

`gr/pd16route.cpp` (forked from pd15route): `PD16ROUTE_RECLAIM=1` adds a
pass between the slow tier and the region stage. Every non-giant net with
2D wire > 1.5x its pin-tile MST (and at least 4 tiles over) is rerouted
whole by MSMV (margin 16) under fresh present-demand prices, whose WL
weight is back near 1, most excess first. A reroute is kept only if the
wire it saves exceeds mu (8) times the local overflow it adds; overflow it
adds is left to the region and refinement stages. Off = pd15route. 538
checks (a 9-tile detour of a 7-tile MST rerouted straight when the row is
free; left alone when the straight row is full; a net under the ratio is
not a candidate).

Test design (the user's: the fastest benchmark plus one or two problem
ones under ~2 h, statistics first, then the rest). newblue2 is the guard
(3 min; no detour: its wire is 1.6% *under* the pins' MST, as is
adaptec4's at -1.4%). bigblue2 (46 min, +7.6% detour, fragile zero) and
newblue1 (93 min, 48 overflow) are the targets. Significance: three runs
of the candidate (pattern cells 7 / 8 / 9) against the three baseline runs
of Part 78; all three below all three is an exact one-sided permutation
test at p = 0.05 per benchmark (1 in 20 orderings). Screen first
(`part83a.jobs`): newblue2 parity (pd16route off vs the standard run),
newblue2 on, bigblue2 at mu 8 and mu 2.

Scope of idea 2 (one hotspot box per cluster): `IDEAS_TEST_PLAN.md`.

**Part 83a, first results** (`status=OK`, `gr_verify` = `eval2008.pl`):
newblue2 with the reclaim pass off reproduces the standard run's route
file **byte for byte** (0 / 7635770). newblue2 with it on (mu 8): 928 of
1680 candidates kept, 2D wire 4716440 -> 4681493 (-0.74%) at a cost of
2D overflow 0 -> 746, which the region and refinement stages took back to
0; final 0 / **7617454** (-0.24%, inside newblue2's noise until measured),
180 s. The other jobs (bigblue2 at mu 8 and mu 2, and Part 82's bigblue4
trace at region round 116) were killed at ~15:28 when the desktop app
hung during an unrelated PDF export; relaunched as `part83a_retry.jobs`.

**Part 83a, bigblue2 screen** (relaunched; `status=OK`, `gr_verify` =
`eval2008.pl`):

| run | scored WL | vs c200 baseline | final detour | reclaim kept / candidates | overflow injected | region | runtime |
|---|---|---|---|---|---|---|---|
| baseline runs (Part 78b) | 9508527 / 9622819 / 9675318 | -- | +7.6 / +9.4 / +10.2% | -- | -- | -- | ~2700s |
| reclaim mu 8 | **0 / 9205885** | **-3.18%** | +4.37% | 9046 / 18537 | 298 -> 19974 | -> 0 in 359 rounds, 283 s | 2692s |
| reclaim mu 2 | **0 / 9196970** | **-3.28%** | +4.23% | 12979 / 18537 | 298 -> 53320 | -> 0 in 393 rounds, 335 s | 2742s |

Both below all three baseline runs by 1.4x+ the 1.8% spread (vs the
baseline mean -4.1% / -4.2%), zero kept, runtime unchanged. Against
NTHU-Route 2.0's re-run (9.00M): +5.7% -> +2.3% / +2.2%. The pass took the
slow tier's +32.3% detour to +15.6% (mu 8) / +10.2% (mu 2); the region
stage, whose box now spans the whole chip because the injected overflow is
everywhere, cleared the overflow in ~5 minutes; the stages after it then
start from much less detour. mu 8 goes to the 3-vs-3 test (less injected
overflow, safer where a residue remains): `part83b.jobs` -- bigblue2 cells
7 / 9, newblue1 cells 7 / 8 / 9, newblue2 cells 7 / 9 plus two newblue2
baseline noise runs.

**Part 82, bigblue4 trace complete** (`status=OK`, 370 / 24861323, route
file byte-identical to Part 80b's): pattern -0.17%, slow tier **+29.85%**,
region +29.36%, refinement +29.18%, tree rebuild +16.84%, WL recovery
**+14.65%** (13844 nets > 1.5x MST carry 1.46M of the 1.71M excess). Its
region box is tiny (9887 nets), so -- unlike the other three -- the region
stage removes almost none of the slow tier's detour; the most remaining
excess of any benchmark.

**Part 83b, the 3-vs-3 test of detour reclaim (mu 8)** (all `status=OK`,
`gr_verify` = `eval2008.pl`; pattern cells 7 / 8 / 9 both sides):

| benchmark | baseline overflow | reclaim overflow | baseline WL | reclaim WL | WL vs baseline mean | runtime (s), baseline / reclaim |
|---|---|---|---|---|---|---|
| newblue2 | 0 / 0 / 0 | 0 / 0 / 0 | 7637309 / 7635770 / 7635916 | 7618285 / 7617454 / 7618092 | **-0.24%** | ~171 / 180-244 |
| bigblue2 | 2 / 0 / 0 | **0 / 0 / 0** | 9622819 / 9508527 / 9675318 | 9209761 / 9205885 / 9207304 | **-4.11%** | ~2690 / 2692-2887 |
| newblue1 | 46 / 48 / 48 | **70 / 54 / 56** | 4882558 / 4824414 / 4803733 | 4758156 / 4762042 / 4763382 | **-1.56%** | ~5330 / 5448-5841 |

(Order within each cell: cell 7 / 8 / 9.) Wirelength: every reclaim run
is below every baseline run on all three benchmarks -- an exact one-sided
permutation p = 0.05 each, ~1.25e-4 jointly. Overflow: the zeros hold (and
bigblue2 holds it 3/3 against 2/3), but **newblue1's overflow is worse in
every run (p = 0.05 in the wrong direction)**. Since overflow ranks first,
mu 8 cannot join the standard configuration as is: on newblue1 the region
stage absorbs the injected overflow but settles above the old residue.
Next candidate: keep only reroutes that add no overflow (mu = infinity).

**Part 83c, strict reclaim, queued** (`part83c.jobs`): mu = 1e9, so a
reroute is kept only if it adds no local overflow at all (the shorter
route must fit in edges with room). newblue1 at pattern cells 7 / 8 / 9
(against baseline overflow 46 / 48 / 48 and mu 8's 70 / 54 / 56), and
bigblue2 cell 8 (against mu 8's 9205885 and the baseline 9508527).
Caveat: the reroute is still chosen by present-demand cost, so where that
cost prefers a path through a full edge the reroute is rejected even if
an overflow-free shorter path exists; this variant measures the safe
lower bound of the gain, not its maximum.

**Part 83c results, strict reclaim (mu 1e9)** (all `status=OK`,
`gr_verify` = `eval2008.pl`):

| benchmark | baseline (3 runs) | mu 8 (3 runs) | strict (runs) |
|---|---|---|---|
| newblue1 overflow | 46 / 48 / 48 | 70 / 54 / 56 | 52 / 54 / 44 |
| newblue1 WL | 4882558 / 4824414 / 4803733 | 4758156 / 4762042 / 4763382 | 4871603 / 4870458 / 4867067 |
| bigblue2 overflow | 2 / 0 / 0 | 0 / 0 / 0 | 0 (cell 8) |
| bigblue2 WL | 9622819 / 9508527 / 9675318 | 9209761 / 9205885 / 9207304 | 9487328 (cell 8) |

(Cells 7 / 8 / 9.) The strict pass keeps ~1000 of ~4000 candidates on
newblue1 (2D wire -2.2%, no overflow added) but the later stages give the
gain back: newblue1 WL lands inside its baseline range, overflow mixed.
On bigblue2 it keeps only 9487328 -- just under the best baseline run
(-0.22%), most of mu 8's -4.1% gone. **The WL gain needs the temporary
overflow and the renegotiation that clears it**; a reroute restricted to
edges with room finds little the later WL stages would not find anyway.
So the open question is how to keep mu 8's renegotiation away from the
residue that newblue1 cannot absorb.

## Part 84 -- option 1: reclaim that avoids the remaining overflow (pd17route)

`gr/pd17route.cpp`: `PD17ROUTE_RECLAIM_AVOID=D` skips reclaim candidates
with any wire tile within D tiles (x and y) of an edge overflowed when the
pass starts -- proximity to each overflowed edge, because the leftover
overflow is scattered (the region stage's single box at that point: 80 x
338 tiles on bigblue2, 101 x 217 on newblue1). Off (-1) = pd16route. 543
checks. Queued (`part84a.jobs`): newblue2 parity (avoid off must reproduce
rc8_newblue2), newblue1 cells 7 / 8 / 9 and bigblue2 cell 8 at mu 8, D = 5
(the region stage's own expand).

## Part 85 -- option 2: reclaim inside the slow tier (pd18route)

`gr/pd18route.cpp`: `PD18ROUTE_RECLAIM_AT_STOP=1` runs the reclaim pass
when the slow tier first reaches PD9ROUTE_STOP_AT, resets the tier's
best-state tracking (otherwise the pre-reclaim best would be returned and
the reclaim silently discarded), and keeps running slow rounds until the
stop holds again; `PD18ROUTE_RECLAIM_EVERY=K` also runs it every K rounds
for benchmarks that never reach the stop. The injected overflow is then
renegotiated by slow rounds (growing boxes, history) rather than the
region stage. Off = pd17route. 547 checks (on a non-convergent fixture the
stop ends the tier after round 0 without it, one round later with it, nets
connected). Queued after Part 84 (`part85a.jobs`): newblue2 parity,
newblue1 cells 7 / 8 / 9 and bigblue2 cell 8, at-stop reclaim alone (no
pre-region pass, no avoid) so the two options are judged separately.

**Part 84 results, option 1 (reclaim mu 8 skipping nets within 5 tiles of
overflow)** (all `status=OK`, `gr_verify` = `eval2008.pl`): newblue2
parity -- avoid off reproduces rc8_newblue2 byte for byte (7617454).
newblue1 cells 8 / 7 / 9: **54 / 56 / 60** overflow, WL 4755838 / 4748436
/ 4753707 (-1.7% vs the baseline mean). On cell 8 the pass skipped 1185
candidates near overflow and kept 1697 of 2869, injecting 298 -> 3618
(vs mu 8's larger injection) -- yet the residue still ends higher.
bigblue2 cell 8: **0 / 9210509** -- the full mu 8 gain survives (9205885).

newblue1's overflow noise is wider than the first three baselines showed:
the strict pass of Part 83c, which left WL inside the baseline range,
ended at 44 / 52 / 54. Against those six near-neutral runs (44, 46, 48,
48, 52, 54), option 1's 54 / 56 / 60 is above all but one tie
(Mann-Whitney U = 17.5 of 18, one-sided p ~ 0.02): still worse. Keeping
the reroutes away from the overflow does not keep the residue down; the
injected overflow elsewhere still draws the region stage away from it.

**Part 85 results, option 2 (reclaim mu 8 at the slow tier's first stop,
then more slow rounds)** (all `status=OK`, `gr_verify` = `eval2008.pl`):
newblue2 parity -- everything off reproduces the standard run byte for
byte. The mechanism works as built: the reclaim fires at slow round 73-79
(about 2000 of 4060 candidates kept, 2D wire -12%, overflow ~290 ->
~5750), and 13-26 more slow rounds bring it back under the 300 stop.

| benchmark | baseline | option 1 (Part 84) | option 2 |
|---|---|---|---|
| newblue1 overflow (cells 8 / 7 / 9) | 48 / 46 / 48 (strict, near-neutral: 54 / 52 / 44) | 54 / 56 / 60 | 54 / 58 / 50 |
| newblue1 WL | 4824414 / 4882558 / 4803733 | 4755838 / 4748436 / 4753707 | 4770440 / 4770304 / **4930754** |
| bigblue2 overflow / WL (cell 8) | 0 / 9508527 | 0 / 9210509 | 0 / 9403565 |

Option 2 is weaker on wirelength (bigblue2 -1.1% vs option 1's -3.1%;
newblue1's cell-9 run ends above every baseline -- the extra slow rounds,
WL weight again near zero, rebuilt the detours) and does not protect
newblue1's overflow either (54 / 58 / 50 against near-neutral 44-54:
inside to above). Neither option keeps newblue1's residue where it was.

What the five variants together say: on a benchmark that reaches zero,
the reclaim's injected overflow is cleared and the WL gain is kept
(bigblue2 3/3 zero, -4.1%; newblue2 3/3 zero, -0.24%); on newblue1, whose
residue does not clear, any renegotiation the reclaim starts ends a few
units higher. The safe form is therefore not a better trigger but a
comparison: run the reclaim and the stages after it from a saved state and
keep the result only if it is no worse on overflow (then lower on WL) --
proposed as the next variant.

## Part 86 -- reclaim with a fallback (pd19route)

`gr/pd19route.cpp`: `PD19ROUTE_RECLAIM_FALLBACK=1` saves the 2D state the
slow tier hands on and runs two paths from it: A, the region stage and
refinement as standard; B, the reclaim pass (mu 8) then the region stage
and refinement. B is kept only if its 2D overflow is lower than A's, or
equal with less 2D wire. Real overflow equals 2D overflow on every
benchmark measured (Part 78), so the choice is made on the number that
is scored. Off = pd18route. 553 checks (the choice rule; on a fixture
where both detours are reclaimable B wins 0/22 vs 0/26, the kept state
carries the kept path's numbers, nets stay connected). Cost: one more
region stage and refinement (minutes on bigblue2, about an hour on
newblue1). Queued (`part86a.jobs`): newblue2 parity and fallback,
newblue1 cells 7 / 8 / 9, bigblue2 cell 8. Expected: newblue1 keeps A
wherever B's overflow is higher -- so its overflow can only match the
standard run's -- and bigblue2 keeps B (9.21M).

**Part 86 results** (all `status=OK`, `gr_verify` = `eval2008.pl`):

| run | path A (standard) 2D overflow / wire | path B (reclaim) | kept | final overflow / scored WL | runtime | same as |
|---|---|---|---|---|---|---|
| newblue2 parity (off) | -- | -- | -- | 0 / 7635770 | 173s | standard run, byte for byte |
| newblue2 | 0 / 4734459 | 0 / 4672897 | **B** | 0 / 7617454 | 181s | rc8_newblue2, byte for byte |
| bigblue2 cell 8 | 0 / 5838363 | 0 / 5054094 | **B** | **0 / 9205885** | 2911s | rc8_bigblue2, byte for byte |
| newblue1 cell 8 | 48 / ~2.65M | 54 / ~2.52M | A | 48 / 4824414 | 10055s | c200_newblue1, byte for byte |
| newblue1 cell 7 | 48 / 2885165 | 70 / 2525356 | A | 46 / 4882558 | 9813s | nf7_newblue1 |
| newblue1 cell 9 | 48 / 2653306 | 56 / 2523945 | A | 48 / 4803733 | 10100s | nf9_newblue1 |

The fallback does what it was built for: where the reclaim path clears
its overflow it is kept and the WL gain lands (bigblue2 -4.1% vs the
baseline mean, newblue2 -0.24%); where it does not, the standard result
is kept unchanged, byte for byte -- overflow can never get worse. The
cost is the second region stage and refinement: +5% on newblue2, +8% on
bigblue2, **+85% on newblue1** (~5300 s -> ~10000 s), where it bought
nothing. A cheaper ordering is possible: run B first and run A only when
B ends with overflow > 0 -- on benchmarks that reach zero this would cost
nothing, and it changes the choice only if A and B both reach zero and A
is shorter, which no run here came near (A was 0.4-16% longer every
time both were zero).

## Part 87 -- the fallback in the cheaper order, on the zero-overflow benchmarks

`gr/pd20route.cpp`: `PD20ROUTE_FALLBACK_ORDER=bfirst` runs the reclaim
path first and the standard path only if the reclaim path ends with
overflow > 0. Default (`both`) = pd19route. 555 checks (on the fixture B
reaches zero, A is never run, and the kept routes are identical to the
both-paths order's). Queued (`part87.jobs`, longest first): parity on
newblue2 and bigblue2 (must reproduce Part 86's fb_newblue2 and
fbc8_bigblue2 byte for byte), then the nine other zero-overflow
benchmarks -- adaptec1-5, bigblue1, bigblue3, newblue5, newblue6 --
against their standard-configuration results (Part 81 / 73b). One run
each; judged by consistency across the nine (all nine shorter would be a
sign test at p = 1/512), with each benchmark's own noise where measured
(newblue6 1.6%). The long residue benchmarks (bigblue4, newblue4,
newblue7, newblue3) wait on this result.

**Part 87 results** (all `status=OK`, `gr_verify` = `eval2008.pl`; every
run's reclaim path reached zero, so the standard path was never run):
parity -- newblue2 and bigblue2 reproduce Part 86's both-paths routes byte
for byte (7617454; 9205885 in 2824 s vs 2911 s).

| benchmark | standard (Part 81 / 73b) | fallback, B-first | change | vs NTHU-Route 2.0 before -> after | runtime (s) standard / fallback |
|---|---|---|---|---|---|
| adaptec1 | 5414046 | **5405278** | -0.16% | +1.0% -> +0.8% | 2065 / 2186 |
| adaptec2 | 5261814 | **5243664** | -0.34% | +0.4% -> +0.07% | 1251 / 1225 |
| adaptec3 | 13253518 | **13142861** | -0.83% | +0.8% -> **-0.05%** | 2062 / 2064 |
| adaptec4 | 12234727 | **12205838** | -0.24% | +0.4% -> +0.2% | 558 / 577 |
| adaptec5 | 15851373 | **15651405** | -1.26% | +2.0% -> +0.7% | 5253 / 5333 |
| bigblue1 | 5754158 | **5700481** | -0.93% | +3.3% -> +2.3% | 2778 / 2861 |
| bigblue3 | 13189107 | **13166745** | -0.17% | +0.9% -> +0.7% | 1605 / 1608 |
| newblue5 | 23383430 | **23324129** | -0.25% | +1.1% -> +0.8% | 6572 / 6510 |
| newblue6 | 18321622 | **17936527** | -2.10% | +3.6% -> +1.4% | 6382 / 6144 |

All nine zeros kept and all nine shorter: a sign test at p = 1/512 (~0.002)
on its own, before counting bigblue2 (-4.1%, 3/3) and newblue2 (-0.24%,
3/3). newblue6 is also outside its own measured noise (below all three
baseline runs, 18321622-18615319). Runtime is unchanged within a few
percent: the B-first order skips the standard path on every benchmark
that reaches zero.

## Part 88 -- the fallback ordered for residue benchmarks (pd21route)

`gr/pd21route.cpp`: `PD21ROUTE_FALLBACK_A_MAX=T` runs the standard path
first and the reclaim path only if the standard path ends with 2D
overflow <= T; above T the standard result is kept and the reclaim never
runs. Zero-overflow benchmarks then pay one extra region stage and
refinement on the reclaim path (minutes; it starts from the injected
overflow, ~5 min on bigblue2), and residue benchmarks pay nothing. With
T = 10, bigblue2's fragile zero is still covered (its standard path has
ended at 2). 557 checks (threshold 0 on the fixture reproduces the
both-paths result; a non-clearing residue above the threshold never runs
B). Validation queued (`part88.jobs`): newblue1 must reproduce
c200_newblue1 byte for byte at standard runtime; bigblue2 and newblue2
must reproduce the reclaim results (fbc8_bigblue2, fb_newblue2). If they
do, the standard configuration moves to pd21route with the fallback on.

**Part 88 results** (all `status=OK`, `gr_verify` = `eval2008.pl`; each
route byte-identical to the one it had to reproduce):

| benchmark | standard path A | reclaim path B | kept | final | runtime | identical to |
|---|---|---|---|---|---|---|
| newblue2 | 0 / 4734459 | 0 / 4672897 | B | 0 / 7617454 | 183 s (standard 171) | fb_newblue2 |
| bigblue2 | 0 / 5838363 | 0 / 5054094 | B | 0 / 9205885 | 3011 s (standard ~2730) | fbc8_bigblue2 |
| newblue1 | 48 / 2649703 | not run (48 > 10) | A | 48 / 4824414 | 5625 s (standard 5608) | c200_newblue1 |

**Adopted** as the standard configuration (2026-10-03): `gr/pd21route`
with `gr/standard.env`, which now adds `PD19ROUTE_RECLAIM_FALLBACK=1
PD21ROUTE_FALLBACK_A_MAX=10`. The ISPD 2008 suite on it: the 11
zero-overflow benchmarks at their fallback results (adaptec1 5405278,
adaptec2 5243664, adaptec3 13142861, adaptec4 12205838, adaptec5 15651405,
bigblue1 5700481, bigblue2 9205885, bigblue3 13166745, newblue2 7617454,
newblue5 23324129, newblue6 17936527 -- the reclaim path's result does not
depend on the order the paths run in, so Part 87's B-first runs stand for
pd21route); the five residues unchanged (newblue1 48, newblue4 144,
newblue7 108, bigblue4 370, newblue3 38704: standard path above 10, so
nothing else runs). The long residue benchmarks were not rerun: on them
the fallback cannot change the result, and newblue1 showed it costs no
runtime there.

## Part 89 -- virtual capacity in the region stage, and the fallback on two threads (pd22route)

Where newblue1's residue forms (standard run, Part 88's af_newblue1): the
slow tier stops at 298 after 74 rounds; the region stage's round 0
reroutes every piece in its box under fresh prices (WL weight ~1) and
overflow jumps to 22766, then falls to ~300 by round 50 and oscillates
between ~76 and ~124 for the remaining 450 rounds, around a best of 64
(4192 s, 75% of the run); refinement takes 64 to 48 in 3 iterations and
then never moves. NTHU's history term only raises the cost of edges that
are overflowed; an edge exactly at capacity costs at most wl + hist, so
inside the hotspot every full-but-legal edge still attracts nets and an
overflowed net has nowhere free to go.

`gr/pd22route.cpp` (forked from pd21route), two independent mechanisms,
both off by default (pd21route byte for byte):
- `PD22ROUTE_VCAP=k`, `PD22ROUTE_VCAP_RADIUS=R` (default 5): at the
  region stage's start, every 2D edge with an end tile within R tiles of
  an overflowed edge is priced against capacity max(cap - k(1 +
  spacing), ceil(cap/2)). Only the present-demand cost reads it; overflow,
  the best-state rule, the stops, refinement and the fallback choice use
  the real capacity. FastRoute 3.0's virtual capacity in its simplest
  (static) form.
- `PD22ROUTE_FALLBACK_PARALLEL=1`: the fallback's reclaim path runs on a
  second thread from the start, cancelled if the standard path ends above
  `PD21ROUTE_FALLBACK_A_MAX`. The search-pop counter is now thread_local,
  so each path's work-gated stop sees only its own work and the kept
  routes equal the sequential order's.

579 checks (the arithmetic; the cost seam, exact when off; the zone by
hand count, 71 edges; on the real search a straight route through a row
at 60% leaves it once the row is priced 2 tracks lower; the region stage
reports its zone; the parallel fallback keeps routes identical to the
sequential order's both when B wins and when B is cancelled).

Queued (`part89.jobs`): newblue2 parity, everything off and parallel on
(both must reproduce af_newblue2, 0 / 7617454); newblue1 cell 8 at k = 1,
2, 4 tracks, R = 5 (against 48; near-neutral range 44-54); parallel-on
timing on newblue1 (must be 48 / 4824414 at ~5625 s) and bigblue2 (must be
9205885; sequential 3011 s). A virtual-capacity setting that screens below
44 goes to cells 7 and 9: three runs all below the six near-neutral runs
is a one-sided Mann-Whitney p = 1/84 (~0.012).

**Part 89, parity** (`status=OK`, `gr_verify` = `eval2008.pl`): newblue2
with everything off and with the parallel fallback both reproduce
af_newblue2's route file **byte for byte** (0 / 7617454), 185 s / 183 s.
No time saved here, as expected: newblue2's standard path has nothing for
its region stage to do (0 overflow, 0 rounds), so B's extra work (the
reclaim and an 8 s region stage) already overlapped nothing. bigblue2 is
the timing case.

**Part 89, virtual capacity screen, newblue1 cell 8** (all `status=OK`,
`gr_verify` = `eval2008.pl`; zone 3086 edges within 5 tiles of the 298
overflow the region stage starts from):

| tracks (units) | region best | region rounds 400-499: mean (range) | refinement -> | final overflow / scored WL | runtime |
|---|---|---|---|---|---|
| 0 (af_newblue1) | 64 | 89 (64-116) | 48 | 48 / 4824414 | 5625 s |
| 1 (2) | 62 | 85 (62-126) | 48 | 48 / 4829245 | 5487 s |
| 2 (4) | 58 | 75 (58-90) | 56 | 56 / 4832346 | 5444 s |
| 4 (8) | **50** | 75 (50-108) | **44** | **44** / 4843729 | 5323 s |

The region stage responds to the dose: its best falls 64 -> 62 -> 58 ->
50 and its late oscillation drops (mean 89 -> 75). The final overflow
follows only loosely, because refinement's share varies widely (-16, -14,
-2, -6): 4 tracks ends at 44, tying the lowest near-neutral run (44-54) --
not yet a significant gain. WL within noise (+0.4%). 8 and 16 tracks
queued behind the parallel-fallback timing runs.

**Part 89, parallel fallback on bigblue2** (`status=OK`, `gr_verify` =
`eval2008.pl`): route file **byte-identical** to af_bigblue2's (0 /
9205885), both paths' stage reports equal to the sequential run's (A
region 298 -> 2 in 500 rounds, B 19974 -> 0 in 359), in **2711 s vs 3011
s** (-10%) -- back to the pre-reclaim standard runtime (~2730 s): the
reclaim's cost on zero-overflow benchmarks disappears into the second
thread. Peak RSS 2622 MB vs 2434 MB (+8%, both paths' state live at once).

**Part 89, 8 tracks (16 units)** (`status=OK`, `gr_verify` =
`eval2008.pl`): region best **46** (rounds 400-499 mean 63, range 46-80),
refinement -> 38; final **38 / 4854999** (WL +0.6%), 5186 s. The first
newblue1 result below all six near-neutral runs (44-54) and the lowest
overflow this project has reached on it (Part 68's 66, Part 75's 54, the
standard's 48). The dose response continues (region best 64 / 62 / 58 /
50 / 46 at 0 / 1 / 2 / 4 / 8 tracks). Queued: cells 7 and 9 at 8 tracks
for the 3-vs-6 test; 16 tracks running.

**Part 89, parallel fallback on newblue1** (`status=OK`, `gr_verify` =
`eval2008.pl`): route file **byte-identical** to af_newblue1's (48 /
4824414), 5645 s vs 5625 s. B ran on the second thread and was cancelled
when A ended at 48 (> 10): no wall-time cost on a residue benchmark, one
otherwise idle core used. With bigblue2 (-10%) and newblue2 (identical,
nothing to overlap), the parallel fallback is ready for the standard
configuration.

**Part 89, 16 tracks (32 units)** (`status=OK`, `gr_verify` =
`eval2008.pl`): region best 48 (rounds 400-499 mean 70, range 48-96),
refinement -> 32; final **32 / 4900178** (WL +1.6%), 6030 s (+7%). The
region stage did not improve on 8 tracks (46); refinement took 16 more
off. Full screen, final overflow by tracks 0 / 1 / 2 / 4 / 8 / 16: 48 /
48 / 56 / 44 / 38 / 32; region best 64 / 62 / 58 / 50 / 46 / 48. Cells 7
and 9 at 16 tracks queued too (`part89c.jobs`), so both doses get the
3-vs-6 test (two tests: Bonferroni-adjusted p = 2/84 ~ 0.024 each).

**Part 89b, the 3-vs-6 test at 8 tracks** (all `status=OK`, `gr_verify` =
`eval2008.pl`):

| cell | baseline (standard) | near-neutral strict runs (Part 83c) | 8 tracks: region best -> refinement = final | scored WL | runtime |
|---|---|---|---|---|---|
| 7 | 46 / 4882558 | 52 | 32 -> **24** | 4904825 | 4787 s |
| 8 | 48 / 4824414 | 54 | 46 -> **38** | 4854999 | 5186 s |
| 9 | 48 / 4803733 | 44 | 44 -> **36** | 4856062 | 5145 s |

**All three 8-track runs (24, 36, 38) are below all six near-neutral
runs (44-54)**: one-sided Mann-Whitney U = 0, p = 1/84 ~ 0.012; with both
doses tested (Bonferroni x2) p ~ 0.024. Mean overflow 32.7 vs 48.7 for the
six (-33%); best newblue1 result to date 24 (LMgr 0, NTHU-Route 2.0's
re-run 164). Wirelength +0.7% vs the three standard runs' mean (inside
newblue1's 1.6% spread); runtime 4787-5186 s vs 5300-5625 s (shorter
region stages, 3287-3781 s vs ~4200 s). Virtual capacity is the first
mechanism to move newblue1's residue significantly since Part 68.

**Part 89c, the 3-vs-6 test at 16 tracks** (all `status=OK`, `gr_verify`
= `eval2008.pl`):

| cell | 8 tracks: overflow / WL / runtime | 16 tracks: overflow / WL / runtime |
|---|---|---|
| 7 | 24 / 4904825 / 4787 s | 28 / 4948600 / 5305 s |
| 8 | 38 / 4854999 / 5186 s | 32 / 4900178 / 6030 s |
| 9 | 36 / 4856062 / 5145 s | 24 / 4912915 / 5391 s |
| mean | 32.7 / +0.7% / 5039 s | 28.0 / +1.7% / 5575 s |

(WL against the mean of the three standard runs, 4836902.) 16 tracks is
also below all six near-neutral runs (p ~ 0.012, ~0.024 Bonferroni). 8 vs
16 on overflow is not distinguishable (24/36/38 vs 24/28/32: Mann-Whitney
U = 2.5 of 9, one-sided p ~ 0.2); 16 costs +1.0% more WL and +11% more
runtime. **8 tracks is the setting carried forward**: the overflow gain is
the same within what three runs can show, at lower WL and runtime cost.

## Part 90 -- the candidate standard: virtual capacity 8 tracks + parallel fallback

`part90.jobs`: pd22route with `@standard.env PD22ROUTE_VCAP=8
PD22ROUTE_FALLBACK_PARALLEL=1` on newblue4 (residue 144, ~2.5 h) and the
11 zero-overflow benchmarks (longest first). Virtual capacity changes the
region stage wherever it runs, which includes most zero-overflow
benchmarks (their slow tier stops near 300): the zeros must hold, and WL
is judged against Parts 87/88. bigblue4 and newblue7 follow only if
newblue4 improves.

**Part 90 results** (all `status=OK`, `gr_verify` = `eval2008.pl`):

| benchmark | standard (Part 87/88) | candidate | WL change | runtime (s), standard / candidate | path kept; region rounds A / B; zone edges A / B |
|---|---|---|---|---|---|
| newblue6 | 0 / 17936527 | **0** / 18001765 | +0.36% (spread 1.6%) | 6144 (B-first) / 6122 | B; 28 / 20; 12886 / 111745 |
| newblue5 | 0 / 23324129 | **0** / 23408252 | +0.36% | 6510 (B-first) / 6521 | B; 26 / 36; 9870 / 124476 (checks rerun on the saved route) |
| bigblue1 | 0 / 5700481 | **0** / 5796812 | **+1.69%** | 2861 (B-first) / 2864 | **A**; 49 / 61; 13249 / 46061 |

Part 90 was launched under a background-command wrapper with a 2-hour
limit, which killed the batch and its routers at 2 h: newblue4 (~2 h into
the region stage), adaptec5 and bigblue1 were lost; newblue5's router had
finished (6520 s) and only its checks were cut off -- rechecked on the
saved route file. The remaining ten jobs were relaunched detached as
`part90b.jobs`.

All zeros held, but virtual capacity also ran in the reclaim path B's
region stage, where its zone is built from the overflow the reclaim
injects on purpose (spread over the chip: 46061-124476 zone edges vs
9870-13249 on A). On bigblue1 that doubled B's region rounds (61 vs 31
without it) and left B with 4.2% more 2D wire (3992403 vs 3830113), so the
fallback kept A and the final WL rose 1.7%; on newblue5 and newblue6 B
still won but ~0.4% longer. The queued jobs not yet started were dropped
(newblue4, adaptec5 and bigblue2, already running, were left to finish).

`gr/pd23route.cpp` (forked from pd22route): `PD23ROUTE_VCAP_B=0` runs the
reclaim path's region stage without virtual capacity (path A keeps it);
default 1 = pd22route. B is then exactly pd21route's B. 582 checks (on the
fallback fixture with it off for B, A gets an 18-edge zone, B none, and
B's wire equals the no-virtual-capacity run's). Queued (`part90c.jobs`):
the 11 zero-overflow benchmarks with virtual capacity 8 on A only plus the
parallel fallback; prediction: byte-identical to the standard's routes
wherever B wins.

**Part 90, more** (`status=OK`, `gr_verify` = `eval2008.pl`):
- bigblue2 on pd22route (virtual capacity on both paths): 0 / 9325503
  (+1.30% vs 9205885), 2628 s. B's region stage with the 167959-edge zone
  took 115 rounds (vs 359 without -- it converged faster) but left B with
  5312099 2D wire vs 5054094 (+5.1%) -- the same harm as bigblue1. Path A
  with virtual capacity reached 0 in the region stage itself (standard A:
  2, then refinement 0).
- **newblue2 on pd23route** (virtual capacity on A only): 0 / 7617454, route
  file **byte-identical** to af_newblue2's, 193 s -- as predicted.
- adaptec5 on pd22route (both paths): 0 / 15738120 (+0.55% vs 15651405), 5399 s.
- **newblue4 on pd22route, virtual capacity 8 tracks** (B never runs:
  identical to pd23route here): **144** / 13711471 (+0.37% WL), 9583 s
  (+8% vs 8871). Region best 170 by round 447 vs the three standard runs'
  170 / 172 / 178, refinement -> 144 as all three did (144 / 144 / 146).
  **No effect on newblue4's residue** (zone 2803 edges). By the plan,
  bigblue4 and newblue7 are not run on this result.
- pd23route, byte-identical to the standard's route files (fb_*): adaptec4
  (12205838, 676 s), adaptec2 (5243664, 1382 s), bigblue3 (13166745, 1730
  s), adaptec3 (13142861, 2195 s).
- pd23route, the rest of the zero-overflow suite, all byte-identical to the
  standard's route files: adaptec1 (5405278, 2181 s), bigblue1 (5700481,
  2804 s), bigblue2 (9205885, 2729 s vs 3011 sequential), adaptec5
  (15651405, 5353 s), newblue6 (17936527, 6096 s), newblue5 (23324129,
  6465 s), with newblue2, adaptec4, adaptec2, bigblue3, adaptec3 above:
  **11 of 11 identical** (`part90c.jobs`, 0 not OK). With virtual capacity
  on path A only, path B is pd21route's B, and B won on every benchmark.

**Part 90 summary.** Virtual capacity 8 tracks on the standard path only
(pd23route, `PD22ROUTE_VCAP=8 PD23ROUTE_VCAP_B=0`) plus the parallel
fallback: zero-overflow benchmarks unchanged byte for byte (11/11), several
faster where B and A overlap (bigblue2 -9%); newblue1 significantly lower
(24 / 36 / 38 vs 44-54, Part 89b); newblue4 unchanged (144, +0.37% WL,
+8% runtime). Not measured: bigblue4, newblue7, newblue3.

**Adopted** as the standard configuration (2026-10-04): `gr/pd23route`
with `gr/standard.env`, which now adds `PD22ROUTE_VCAP=8
PD23ROUTE_VCAP_B=0 PD22ROUTE_FALLBACK_PARALLEL=1`. The ISPD 2008 suite on
it: the 11 zero-overflow benchmarks byte-identical to Part 88's; newblue1
**38 / 4854999** (cell 8; B never runs above 10, so pd22route's vc8 run is
pd23route's); newblue4 144 / 13711471 (likewise); bigblue4 370, newblue7
108, newblue3 38704 not yet rerun (recorded under pd13/pd14route). Total
overflow 39374 -> 39364.

## Part 91 -- why virtual capacity moved newblue1 but not newblue4, and which residues are forced

Item 1 of the 2026-10-04 plan runs in the background (`part91.jobs`:
bigblue4 and newblue3 on the new standard, pd23route). Item 2, the
diagnosis, used two analysis tools (not routers):

- `gr/grdiag.cpp` gains **D3, hotspot neighborhoods**: for the largest
  hotspots, the 2D capacity of their overflowed edges, and in rings of
  tiles at distance 0 / 1-2 / 3-5 / 6-10 / 11-20 per direction: usage over
  capacity, free units and edges with room for one more net. Self-test
  extended (ring 0 of the fixture by hand: 4 H edges, cap 8, use 12, free 2,
  room 1; 4 V edges free 8; overflowed-edge capacity 2).
- New `gr/cutbound.cpp`: a certified lower bound from the **cut condition**.
  For a rectangle S, every net with a pin tile inside and one outside
  crosses S's boundary at least once, so the boundary's 2D overflow is >=
  D(S) - C(S) (demand of those nets at minWidth + smallest spacing, minus
  the boundary's 2D capacity); real overflow >= 2D overflow; rectangles
  with disjoint boundaries add. Enumerates every rectangle up to a side
  limit in a region and packs a disjoint set greedily; also reports the
  tightest cuts (D/C). Self-test: 5 nets out of a centre tile with 8 units
  of boundary capacity -> forced >= 2; an inside-only net excluded; an
  uncongested case 0. Runs in seconds (minutes for 40-tile sides).

**D3** (standard-configuration routes):

| hotspot | overflowed-edge 2D cap | ring 1-2 use | ring 3-5 use | ring 6-10 use |
|---|---|---|---|---|
| newblue1 main (30) | 62 | 98.7 / 98.4% | 97.0 / 96.7% | 96.2 / 95.2% |
| newblue4 main (142) | 84 | 90.8 / 87.7% | 79.5 / 79.5% | 61.3 / 78.6% |
| bigblue4 (370, one hotspot) | 192-204 | 92.3 / 92.7% | 74.6 / 76.6% | 64.6 / 57.2% |
| newblue7 main (72) | 198-212 | 93.0 / 84.4% | 85.4 / 65.9% | 71.2 / 54.5% |
| newblue7 x 161 (6; 12 small ones total 36) | 198-212 | 97.4 / 97.4% | 95.8 / 97.1% | 91.4 / 96.3% |

(H / V.) newblue1's neighborhood is saturated for 10 tiles; newblue4's
has 5000-21000 free units 3-10 tiles out.

**cutbound** (tightest cut = the rectangle whose must-cross demand is the
largest share of its boundary capacity):

| benchmark | ours (standard) | best published | tightest cut | certified lower bound |
|---|---|---|---|---|
| newblue1 | 38 | 0 (LMgr) | 92.6% | 0 |
| newblue4 | 144 | 144 | **98.4%** (x 59-73 y 70-81: 2232 nets, 4464 of 4536) | 0 |
| bigblue4 | 370 | 166 | 89.3% | 0 |
| newblue7 | 108 | 68 | **101.1%** (x 222-228 y 246-250: 2522 nets, 5044 > 4990) | **54** |
| newblue3 | 38704 | ~31024 | single tile (487,668): 2299 nets, 4598 vs 248 | **>= 26264** |

newblue3: 13 concentric squares around tile (487,668) (sides 1-25, borders
disjoint) certify 25336, plus isolated tiles (447,458) 380, (504,461) 292,
(522,579) 256 -- **at least 26264 of newblue3's overflow is unavoidable
for any router**, 85% of the best published result. newblue7: at least 54
is forced in its main hotspot (ours 72 there, 108 total; best published 68).

**Reading.** Two kinds of residue:
- *Through-traffic* (newblue1, newblue7's small hotspots): must-cross
  demand well under the boundary (newblue1 92.6%) but the neighborhood
  saturated by nets passing through. Pushing traffic out is what helps --
  virtual capacity did (newblue1 48 -> 24-38).
- *Pin-bound* (newblue4, newblue7's main hotspot, newblue3): nets with pins
  inside need nearly all (newblue4 98.4%) or more than all (newblue7,
  newblue3) of the boundary just to get out once each; there is little or
  no through-traffic to push. Virtual capacity cannot help, which is what
  newblue4 showed; newblue4's 144 equals the best published result and is
  likely at or near its floor.
bigblue4 is between (89.3% tightest cut, free room 3+ tiles out): the
overnight run will show whether virtual capacity reaches it.

**Part 91, item 1 interrupted for an OS update** (2026-10-04, ~7.6 h in):
bigblue4 on pd23route had reached region round 155, best 400 (the
standard run's best by the same round 404, its whole-stage best 404 ->
refinement 370; virtual-capacity zone 1045 edges); newblue3 was at region
round 6 (slow tier ended at 41000 after 150 rounds). Neither finished; both
are relaunched after the update. Run outputs from Parts 72-91 were copied
from the session scratchpad (cleared on reboot) to
`~/Code/GlobalRouterRuns/` (4.4 GB).

**Part 91, item 1 results** (relaunched after the OS update; outputs in
`~/Code/GlobalRouterRuns/runs`; both `status=OK`, `gr_verify` =
`eval2008.pl`):

| benchmark | previous standard | pd23route (virtual capacity 8) | region stage | refinement | runtime |
|---|---|---|---|---|---|
| bigblue4 | 370 / 24861323 (pd14route, wg_bigblue4) | **356** / 24863041 (WL +0.01%) | 408 -> 400, 232 rounds, 20465 s (was 408 -> 404, 150 rounds, 9685 s) | 400 -> 356 (was 404 -> 370) | **38634 s (10.7 h, +39%)** |
| newblue3 | 38704 / 10779692 (pd12route) | 38916 / 10796239 (+0.55% overflow, +0.15% WL) | 41000 -> 39010, 59 rounds (plateau stop), 2135 s | 39010 -> 38916 (plateau stop) | 29612 s (8.2 h, -0.5%) |

bigblue4: -14 overflow (-3.8%), its first improvement since the margin
caps; the zone (1045 edges) cost 88 s per region round vs 65 s, and the
work-gated stop came later (232 rounds) because the best kept inching
down, so the run took 39% longer. Single run; bigblue4's run-to-run
spread under pattern-cell perturbation is unmeasured (its three earlier
runs share one cell and are identical).

newblue3: +212 overflow, but **not a like-for-like comparison**: 38704
is pd12route's (no margin caps, no work-gated stop), and newblue3 was
never rerun on pd14route; a pd23route run with `PD22ROUTE_VCAP=0` would
isolate virtual capacity's share. newblue3 is pin-bound (cutbound: >=
26264 forced) and its zone covered 4993 edges around 41000 overflow, so
no gain was expected.

Suite on the standard configuration (pd23route): 11 zero-overflow
benchmarks unchanged; newblue1 38, newblue4 144, bigblue4 **356**, newblue7
108 (pd14-era, not rerun), newblue3 38916. Total 39562.

## Part 92 -- multithreading: batched parallel rerouting (pd24route)

Plan: `MULTITHREADING_PLAN.md` (with adaptive virtual capacity and the
long-net capacity limit scoped to build on it). Time on one thread is
85-95% the slow tier and the region stage on every long benchmark (region:
bigblue4 53%, newblue7 54%, newblue4 67%, newblue1 75%; newblue3 mostly
the slow tier).

`gr/pd24route.cpp` (forked from pd23route): `PD24ROUTE_THREADS=T` runs
both stages in batches of `PD24ROUTE_BATCH` (64) nets: searched in parallel
against the batch-start state on per-worker usage replicas (the per-net
code unchanged), committed in net order, a net whose new route crosses an
edge changed by an earlier commit in the batch and now overflowing is
rerouted live (`PD24ROUTE_REDO=1`). Routes depend on the batch size and
the rule, not on T. Threads 0 = pd23route. 598 checks, among them: three
nets stampeding onto one free row in a batch are spread to 0 overflow by
the redo rule (2 redone) and pile up (overflow 36) without it; on a
congested 220-net synthetic design, 1 and 4 workers give identical routes
through the slow tier (329 batches) and the region stage (163), with every
replica checked against the live state after every batch.

Queued (`part92a.jobs`, one at a time for clean timing): newblue2 at
threads 0 (must equal af_newblue2), 1 and 8 (must equal each other);
newblue1 and bigblue2 at 8 threads.

**Part 92a results** (pd24route; one job at a time; all `status=OK`,
`gr_verify` = `eval2008.pl`):

| run | overflow / scored WL | runtime | notes |
|---|---|---|---|
| newblue2, threads 0 | 0 / 7617454 | 179 s | **byte-identical** to af_newblue2 (pd23route) |
| newblue2, threads 1 (batch 64) | 0 / 7618400 | 183 s | batched order: slow tier ends at 42 (sequential: 0); +0.01% WL |
| newblue2, threads 8 | 0 / 7618400 | 171 s | **byte-identical to threads 1** -- routes independent of the thread count at full scale |
| newblue1, threads 8 | 40 / 4895006 (WL +0.8%) | **2783 s vs 5625 s (2.0x)** | region 1540 s vs 4192 s (2.7x); slow tier 90 rounds (74 sequential); overflow inside the 24-38 / 44-54 ranges |

newblue2 gains little (its slow tier is 2 rounds; most of its 3 minutes
is unthreaded stages). On newblue1 the conflict rule fired on more changed
routes than it passed (slow tier 165417 redone vs 103230 committed; region
150511 vs 115638), and pd24route redoes those one at a time on the main
thread -- the cap on the speedup.

`gr/pd25route.cpp` (forked from pd24route): `PD25ROUTE_REDO_PASSES=1`
(default; 0 = pd24route) re-searches a batch's conflicting nets in further
parallel passes against the updated state until none are left (the first
net of a pass always commits). Self-test: the stampede spreads to 0 in 2
extra passes, 1 vs 3 and 1 vs 4 workers identical. Queued
(`part92b.jobs`): newblue1 and bigblue2 at 8 threads.

**Part 92 results, continued** (8 threads, one job at a time; all
`status=OK`, `gr_verify` = `eval2008.pl`):

| run | overflow / scored WL | runtime (1 thread: newblue1 5625 s, bigblue2 2729 s) | region stage | conflicts |
|---|---|---|---|---|
| bigblue2, pd24route (serial redo) | 0 / 9214150 (+0.09%) | 1971 s (1.38x) | -- | slow tier 491340 redone vs 397100 committed |
| newblue1, pd25route (parallel passes) | 44 / 4888708 (+0.7%) | **2303 s (2.44x)**; pd24route 2783 s | 1334 s (3.1x vs 4192 s) | region 277394 re-searched in 41885 extra passes |

Overflow 44 is a single run; newblue1's range is 24-38 with virtual
capacity on one thread (cells 7/8/9), 40 for pd24route -- small either way.
The remaining serial time is now the stages outside the two batched loops
(pattern stage, tree rebuild, WL recovery, layer assignment): newblue1's
1-thread non-region time 1376 s fell only to ~910 s.
- bigblue2, pd25route, 8 threads: **0** / 9212202 (+0.07%), **1739 s (1.57x**; pd24route 1971 s). bigblue2's run is
  short on maze work after the slow tier (A's region stage reaches 0 in minutes), so its serial share is large.

**Part 92c, newblue1, pd26route, 10 threads** (`status=OK`, `gr_verify` =
`eval2008.pl`): 44 / 4883229, 2820 s wall -- **including a 288 s system
sleep** (low battery, 19:31:35-19:36:23, during route()), so ~2532 s of
running. Stage timers: route() (pattern stage, fast and slow tiers) 1447 s
(~1159 s awake), region + refinement 1347 s (region 1285 s vs 1334 s at 8
threads), tree rebuild 2.7 s, WL recovery 15.9 s, layer assignment 5.0 s.

So the serial remainder is not the cleanup stages (24 s together) but
route(): ~1300 s on one thread -> ~900-1160 s batched, only ~1.2-1.45x. And
the region stage barely gains from 8 to 10 threads. CPU time says why:
~202 CPU-minutes by 39 minutes of wall time (~5.9 cores busy while awake)
against ~94 CPU-minutes for the whole one-thread run -- the conflict rule's
re-searches (region 277394, slow tier 343359) inflate the work ~2x, and
small conflict passes leave most workers idle at each pass barrier.

`gr/pd27route.cpp` (forked from pd26route): `PD27ROUTE_CONFLICT=fresh`
(default; `any` = pd26route) re-searches a net only if its new route
crosses an edge that had room when it was searched and that an earlier
commit in the pass filled -- a net that knowingly took an already-full
edge is committed as searched; `PD27ROUTE_CARRY=1` (default) moves
conflicting nets to the front of the next batch instead of a small pass of
their own. route() now reports its pattern+fast vs slow split. 614 checks
(the stampede still spreads to 0 with carry + fresh, 3 carried; 1 vs 3 and
1 vs 4 workers identical). Queued (`part92d.jobs`): newblue1, 10 threads.

## Part 93 -- congestion maps: grmap (2D) and grview (3D, Metal)

The congestion visualizer from CLAUDE.md's build stage 1, which was never
built. `gr/grmap.cpp` (std-only, in CI): charges a route file as gr_verify
does, collapses to 2D per tile edge, classes each edge by use/capacity
(<= 50% violet, 50-75% blue, 75-90% green, 90-99% yellow, exactly 100%
orange, over red; no capacity: blocked grey, or red if used), draws each
tile in the worst class of its edges (PPM), prints counts per class and
the 2D overflow (a cross-check: newblue1 38, newblue4 144, bigblue4 356,
newblue7 108, newblue3 38916, adaptec1 0, bigblue2 0 -- all equal to the
scored results), and with a `.grid` output writes per-tile ratios and
classes for the 3D viewer. Self-test: every class boundary exactly, a
4 x 2 fixture's tiles and counts, bar heights 1.5 / 1.0 / 0.
`gr/gpu/grview.mm` (macOS only: Cocoa, Metal, MetalKit, ImageIO; shaders
compiled at run time; not in the routers or CI): every tile a bar of
height use/capacity, instanced in one draw; drag rotates, scroll zooms,
Shift-drag pans, +/- bar height, H hides violet and blue, B toggles a
lift on red bars (overflow is 1-2 units on 60-200, so true red bars barely
clear the orange), R resets; `--snapshot` renders offscreen to PNG.
Self-test: the grid reader (good, truncated, wrong magic), instance
colors, the orbit camera's projection. Maps and snapshots of seven
benchmarks: `~/Code/GlobalRouterRuns/maps/` (page under `maps/page/`).

**Part 92c/d results** (newblue1, 10 threads, one job at a time; all
`status=OK`, `gr_verify` = `eval2008.pl`):

| run | overflow / scored WL | runtime | stages (s) | conflicts |
|---|---|---|---|---|
| pd26route, batch 32 | 42 / 4892941 | 2249 s (2.50x) | -- | -- |
| **pd27route** (carry + fresh), batch 64 | **54** / 4904625 | **1474 s (3.82x)** | pattern+fast 75, slow tier 434, region 875 (4.8x vs 4192), refinement ~66, rebuild 2, WL 15, LA 5 | slow tier 137586 redone vs 308309 committed (was 343359 vs 249010); region 82608 vs 320364 (was 277394 vs 252432); 0 extra passes |

pd27route: the redo work fell by 60-70% and no pass waits on a handful of
nets any more -- 3.8x end to end. But 54 is above all three one-thread
virtual-capacity runs (24 / 36 / 38) and at the top of the old no-VC range
(44-54); region stage best 68 (pd25route 58). If real, the "fresh" rule
(committing routes chosen against stale state when the edge was already
full) costs newblue1 a large share of its virtual-capacity gain. Queued
(`part92e.jobs`): pd27route cells 7 and 9, and cell 8 with carry but the
old conflict rule (`PD27ROUTE_CONFLICT=any`).

## Part 94 -- how far out from each hot center the chip has room again (hotring)

The congestion maps (Part 93) show dense hot centers with rings cooling
outward. `gr/hotring.cpp` (an analysis tool on grmap's .grid files):
a tile is in a hot center if the mean worst-edge ratio of the 7 x 7 block
around it is >= 95% (scattered full tiles -- 10-25% of tiles even at zero
overflow -- are not), or if it is overflowed; center tiles within 2 tiles
join. From each center's bounding box, every side moves outward until the
tiles along it are cool (<= 75%, violet or blue) for >= 90% of them ("most
of the way around") or all of them; a side at the die edge stops there.
Self-test: a 4 x 4 core with a 2-tile warm band gives 10 x 10 at 90% and
moves one tile further past a single warm tile at 100%; a die-edge
hotspot is flagged; scattered full tiles are not a center, a solid block
and a lone overflowed tile are. (A first version clustered raw >= 95%
tiles within 5 of each other; that chained scattered full tiles into
die-sized "centers" and was replaced.)

Overflowed centers (standard routes; % of the die width x height):

| benchmark | die | hot center | cool on 90% of each side | cool all the way around | band thickness L / R / B / T (90%) |
|---|---|---|---|---|---|
| newblue1 | 399 x 399 | 87 x 87 (22% x 22%), 37 overflowed tiles | 245 x 279 (61% x 70%) | 318 x 290 (80% x 73%) | 56 / 102 / 4 / 188 |
| newblue4 | 455 x 458 | 43 x 39 (9% x 9%), 119 | 93 x 383 (20% x 84%) | 447 x 398 (98% x 87%) | 45 / 5 / 4 / 340 |
| bigblue4 | 403 x 405 | 95 x 100 (24% x 25%), 127 | 234 x 272 (58% x 67%) | 371 x 318 (92% x 79%) | 13 / 126 / 7 / 165 |
| newblue7 | 488 x 490 | 244 x 280 (50% x 57%), 81 | 293 x 472 (60% x 96%) | 395 x 483 (81% x 99%) | 33 / 16 / 170 / 22 |
| newblue3 | 973 x 1256 | 135 x 88 (14% x 7%), 747 | 157 x 995 (16% x 79%) | 215 x 1213 (22% x 97%) | 12 / 10 / 474 / 433 |

Largest hot center of each zero-overflow benchmark, cool on 90% of each
side: adaptec1 70x66% -> 90x88%; adaptec2 18x23% -> 24x27%; adaptec3
23x31% -> 28x73%; adaptec4 13x11% -> 51x54%; adaptec5 46x46% -> 75x47%;
bigblue1 98x81% -> 100x90% (right edge); bigblue2 27x56% -> 80x92%;
bigblue3 22x13% -> 33x31%; newblue2 5x27% -> 38x69%; newblue5 41x54% ->
88x79%; newblue6 64x67% -> 69x75%. (Full output:
`~/Code/GlobalRouterRuns/maps/hotring_*.txt`.)

Readings: (1) the warm band around an overflowed center is wide -- the
90%-cool rectangle spans 58-70% of the die on newblue1 and bigblue4, the
band 50-190 tiles thick on its far sides -- while the region stage's box is
the overflow's bounding box plus 5 tiles; (2) it is lopsided: cool comes
within a few tiles on one or two sides (often a die edge) and only hundreds
of tiles away on the others (newblue1 bottom 4 / top 188; newblue4 right 5
/ top 340) -- the hot centers sit at the edge of larger warm regions, not
as islands; (3) "all the way around" reaches 73-99% of the die almost
everywhere (a single warm tile on a long line blocks it), so the 90% figure
is the useful one; (4) zero-overflow benchmarks show equally large warm
regions (adaptec1, bigblue1, bigblue2, newblue5: 80-100% of the die) and
still reach zero, so a wide warm band alone does not make overflow -- the
center's own demand does (Part 91's cut ratios).

## Part 95 -- the ring pull (pd28route)

The user's proposal after Part 94: move load from the warm band around a
hotspot out to the cool tiles beyond it, then move the hotspot's load
into the band that freed. `gr/pd28route.cpp` (forked from pd27route):
`PD28ROUTE_RING=s` computes, at the region stage's start, the ring
rectangle (Part 94's rule: each side out from the overflow's box until
>= s of its tiles are <= `PD28ROUTE_RING_COOL` 0.75); then
`PD28ROUTE_RING_PASSES` (1) passes reroute every net's pieces with wire in
it under present-demand prices with the band's warm edges priced
`PD28ROUTE_RING_PULL` (4) tracks lower and the core (overflow box +
expand) 8 tracks lower, each piece kept only if it adds no local overflow;
then the region stage runs as before (virtual capacity on the core).
`PD28ROUTE_RING_BOX=1` also makes the ring rectangle the region stage's
box. Off = pd27route. 623 checks (the ring rectangle on hotring's fixture;
on a 16 x 9 design with an overflowed row between two 80% rows the pull
takes the band's warm edges from 0.80 to 0.20 mean use/capacity, overflow
10 -> 0, nets connected; off: no ring). Queued (`part95a.jobs`, one
thread): newblue1 cell 8 with the pull, and with the pull + ring box.
(q10_bigblue2's timing in part92c overlaps these runs.)

**Part 95a, the pull on newblue1** (from the trace): ring x 63-308 y 0-288
(246 x 289 of 399 x 399), 231469 nets, 621202 pieces rerouted (4501
reverted) in 11 s; the band's 18342 warm edges 0.870 -> 0.832 mean
use/capacity, core 0.801 -> 0.762, 2D overflow 298 -> 262. Modest: the
pieces search only 16 tiles beyond their span while the band is 50-190
tiles thick, and 4 tracks is a quarter of what virtual capacity allows
(its floor is half the capacity, ~15 tracks here). Queued
(`part95b.jobs`): pull 15 tracks alone; pull 15 + margin 64 + 3 passes.

## Part 96 -- the band profile: how warm the ring really is, and how far room is

`gr/hotring.cpp --profile` (self-tested: distance to cool on the Part 94
fixture -- core 4, band 2, cool 0; shell means 0.85 / 0.85 / 0.3; natural
bands recover three planted groups exactly): per side, the mean ratio of
the one-tile shells at distance d from the center's box and the first d at
which it falls to 100 / 85 / 70 / 55 / 40%; the distance from every tile
to the nearest tile <= 75% (Chebyshev, multi-source); and 1D k-means
"natural bands" (6 groups) of the tile ratios inside the 90% ring
rectangle. Overflowed centers (outputs `maps/profile_*.txt`):

| benchmark | shell mean at d = 1 (L / R / B / T) | first d at <= 55% (L / R / B / T) | distance to cool: center median / 90th / max | band median / 90th / max | natural band boundaries |
|---|---|---|---|---|---|
| newblue1 | 0.77 / 0.57 / 0.45 / 0.75 | 26 / 2 / 1 / 131 | 3 / 14 / 24 | 0 / 3 / 16 | 0.24 0.47 0.68 0.84 0.95 |
| newblue4 | 0.69 / 0.69 / 0.51 / 0.74 | 30 / 5 / 1 / >256 | 3 / 7 / 11 | 1 / 8 / 25 | 0.36 0.55 0.70 0.83 0.94 |
| bigblue4 | 0.70 / 0.82 / 0.72 / 0.80 | 13 / 116 / 7 / 38 | 1 / 7 / 18 | 1 / 9 / 28 | 0.35 0.52 0.68 0.81 0.93 |
| newblue7 | 0.69 / 0.42 / 0.65 / 0.68 | 29 / 1 / 5 / 14 | 1 / 12 / 39 | 0 / 3 / 11 | 0.26 0.45 0.63 0.78 0.92 |
| newblue3 | 0.85 / 0.81 / 0.82 / 0.80 | 12 / 10 / 89 / 75 | 3 / 15 / 28 | 0 / 2 / 13 | 0.36 0.55 0.70 0.83 0.94 |

Readings, which revise Part 94's: (1) the ring is mottled, not a solid
warm wall -- even the first shell outside a center averages 42-85%, and
the 90% ring rectangle is wide because a long side line rarely gets to
90% cool tiles, not because the band is uniformly warm; (2) room is close:
a tile at <= 75% is a median 1-3 tiles from a center's tiles and 0-1 from
the band's (90th percentile 7-15 and 2-9; max 11-39) -- well inside the
16-tile margin the pull already searches, so reach is not what limits
the pull (and a per-piece reach from this map would mostly shrink it);
(3) the natural bands are nearly the same on every layout -- boundaries
about 0.24-0.36 / 0.45-0.55 / 0.63-0.70 / 0.78-0.84 / 0.92-0.95 -- close to
the user's proposed 40 / 55 / 70 / 85 / 100.

## Part 97 -- where the room is along the crossing line, and a price for using a full edge

`gr/cutdetour.cpp` (analysis; self-test: two overflowed edges in a roomy
line -> nearest 1, window 1; a full stretch -> 4 and 4; a line over
capacity in aggregate -> none): a net leaving an overflowed edge must
cross the same line elsewhere, so for every overflowed 2D edge it measures,
along that line, the distance to the nearest edge with room for one net,
and the half-width of the window whose total spare covers all the
overflow in it (`maps/cutdetour.txt`):

| benchmark | overflowed edges | nearest room: median / 90th / max | absorbing window: median / 90th / max | windows over 25 / 50 |
|---|---|---|---|---|
| newblue1 | 19 | 7 / 10 / 11 | 9 / 11 / 11 | 0 / 0 |
| newblue4 | 71 | 7 / 12 / 14 | 9 / 17 / 19 | 0 / 0 |
| bigblue4 | 144 | 7 / 12 / 17 | 11 / 19 / 26 | 2 / 0 |
| newblue7 | 43 | 5 / 16 / 23 | 6 / 29 / 43 | 6 / 0 |
| newblue3 | 1042 | 7 / 12 / 21 | 35 / 69 / 83 | 756 / 259 |

Every overflowed edge outside newblue3 has the spare to absorb its
overflow within 11-43 tiles along its own line, a detour of ~20-80 tiles.
The region stage's margins reach that within its first rounds (10 + 2 per
round, cap 200). What stops it is the price: by mid-stage the WL weight
has decayed, so each detour tile through nearly full edges costs ~1, while
an edge 1-2 units over a 60-200 capacity costs ~1.2 x its history.

`gr/pd29route.cpp` (forked from pd28route): `PD29ROUTE_OVER_PENALTY=P`
adds P to the cost of any edge one more net would push over its real
capacity, in the region stage (path A only, like virtual capacity). Off =
pd28route. 626 checks (cost seam exact; on a 5 x 3 grid the search crosses
a full edge at P = 0 -- 5.14 vs a 6.93 detour -- and detours at P = 5).
Queued (`part97a.jobs`): newblue1 cell 8 at P = 20 and P = 100.

## Part 98 -- the penalty scaled by the distance to room (pd30route)

The user's refinement of Part 97: price each hot edge by how far its nets
must go to find room. `gr/pd30route.cpp` (forked from pd29route):
`PD30ROUTE_WINDOW_PENALTY=a` gives every 2D edge with less spare than one
net the penalty a x 2 x w, w its absorbing window along its own line
(the smallest half-width whose edges together hold one net's spare; cap
200), added when one more net would overflow it; windows recomputed from
the live usage every `PD30ROUTE_WINDOW_EVERY` (10) rounds; region stage,
path A only. Off = pd29route. 631 checks (windows on hand-made lines:
1 / 1 / 0, the cap for a line without spare, 4 for a full stretch; on a
grid the full edge's window 1 gives penalty 3 at a = 1.5; the cost seam
adds exactly the edge's own penalty). Queued (`part98a.jobs`): newblue1
cell 8 at a = 1 and a = 2.

Part 95b's second job (pull 15 + margin 64 + 3 passes) was cancelled before
it started (user's decision): Part 96's distance-to-room (median 1-3 tiles,
90th percentile 7-15) and the 4 -> 15 track result (warm edges 0.832 ->
0.816) say the pull is not reach-limited; the slot goes to the penalty
runs (Parts 97-98).

**Part 95a results** (newblue1 cell 8, one thread; `status=OK`, `gr_verify`
= `eval2008.pl`; standard: 38 / 4854999, region best 46, 5186 s):

| run | region best | final overflow / scored WL | runtime |
|---|---|---|---|
| pull 4 tracks | 48 | 36 / 4842258 (-0.3%) | 5129 s |
| pull 4 tracks + ring box | 46 | 36 / 4788240 (-1.4%) | 5123 s |

Both inside newblue1's virtual-capacity range (24 / 36 / 38): no
measurable overflow effect, wirelength within its 1.6% spread. Small.
- pull 15 tracks (ring15_newblue1): region best **32** (standard 46), refinement -> 32; final **30** / 4849835
  (-0.1% WL), 4991 s. The region stage itself ended 30% lower than the standard's; the final 30 is 21% below
  the standard's 38 at this cell but inside the 24-38 range across cells. One run.

**Part 97a results** (newblue1 cell 8, one thread; `status=OK`,
`gr_verify` = `eval2008.pl`; standard 38 / 4854999):
- flat penalty 20 (op20_newblue1): **76** / 4883507 (+0.6% WL), 8192 s wall including a 21.5-minute system
  sleep (~6900 s awake, +33%). **Overflow doubled**: penalizing every full edge equally makes nets leave full
  edges into neighbours that then fill, and the stage churns instead of settling.

**Part 98a result** (newblue1 cell 8, one thread; `status=OK`, `gr_verify` = `eval2008.pl`):
- window penalty a = 1 (wp1_newblue1): **36** / 4855408 (WL +0.0%), 7303 s wall (the run spanned the 21.5-minute
  sleep only partly; slower than the standard's 5186 s by ~30% -- the penalty adds detours to the searches).
  Same overflow as the ring pulls' 36, inside the 24-38 range: no measurable effect.
- flat penalty 100 (op100_newblue1): **136** / 4884366 (+0.6% WL), 10481 s wall (incl. the 21.5-min sleep).
  3.6x the standard's overflow: the bigger the uniform penalty, the worse the churn. The flat form is retired.
- window penalty a = 2 (wp2_newblue1): region best 74, final **66** / 4826906 (-0.6% WL), 6002 s.

**Parts 97-98, verdict.** Every penalty on full edges made newblue1 worse
or left it in its noise: flat 20 -> 76, flat 100 -> 136, window a = 1 -> 36,
window a = 2 -> 66 (standard 38; range 24-38). The stronger the push off full
edges, the worse the result: moved nets land on neighbouring edges that then
fill, and the stage churns. Room along the line exists (Part 97), but pricing
alone does not get the nets there without displacing others. The only variant
that moved the region stage itself is the 15-track ring pull (region best 32
vs 46, final 30) -- a one-time, overflow-safe pre-pass, not a standing price.

**Part 92e results** (pd27route, 10 threads, carry + fresh; `status=OK`,
`gr_verify` = `eval2008.pl`): newblue1 cell 7 **48** / 4918113 in 1421 s;
cell 9 **48** / 4912774 in 1376 s; with cell 8's 54: mean **50.0** against
the one-thread standard's 24 / 38 / 36 (mean 32.7) -- **+53% overflow** at
~3.9x speed. pd27route's defaults give back virtual capacity's newblue1
gain (back in the old 44-54 range) and cannot become the standard as they
are. Pending: cell 8 with carry but the old conflict rule (r10any).
- cell 8, carry + the old conflict rule (`PD27ROUTE_CONFLICT=any`, r10any_newblue1): **34** / 4885350 (+0.6% WL)
  in **2083 s** (2.5x the one-thread 5186 s), against 54 with carry + fresh. **The "fresh" rule is what costs
  newblue1 its overflow**; carrying conflicts into the next batch does not. Carry + any is the threading
  candidate; cells 7 and 9 would confirm it.

## Part 99 -- the threading candidate, three cells

pd27route, 10 threads, carry + the old conflict rule (`PD27ROUTE_CONFLICT=any`),
newblue1 (all `status=OK`, `gr_verify` = `eval2008.pl`):

| cell | one thread (pd23route standard) | threaded candidate | runtime |
|---|---|---|---|
| 7 | 24 | 38 / 4899002 | 2266 s |
| 8 | 38 | 34 / 4885350 | 2083 s |
| 9 | 36 | 46 / 4890884 | 2339 s |
| mean | 32.7 | **39.3** (+20%) | ~2230 s (**2.3-2.5x**) |

Not distinguishable from the one-thread runs at three runs each
(Mann-Whitney U = 6.5 of 9, p ~ 0.3), but the mean is 20% higher -- by the
project's bar, a borderline result, not a clean pass. WL +0.8% (inside
newblue1's 1.6% spread). Next (user's order): the 15-track ring pull,
threaded the same way, on cells 7 / 8 / 9 against these three runs (routes
do not depend on the thread count, so three jobs at 4 threads each give the
same results as one at a time at 10).

**Part 99b results** -- the 15-track ring pull, threaded like the candidate
(carry + any; 3 jobs x 4 threads at once), newblue1 (all `status=OK`,
`gr_verify` = `eval2008.pl`):

| cell | threaded candidate (no pull) | threaded + 15-track pull |
|---|---|---|
| 7 | 38 | 38 / 4901328 |
| 8 | 34 | 44 / 4886846 |
| 9 | 46 | 36 / 4900319 |
| mean | 39.3 | **39.3** |

Identical means: the pull has no effect, and the one-thread run's 30 (Part
95a) was noise. **Verdict on Parts 95-98:** neither the ring pull (4 or 15
tracks, with or without the ring box) nor any over-capacity penalty (flat
20 / 100, window-scaled a = 1 / 2) improves newblue1; the flat and a = 2
penalties make it clearly worse. Retired. Virtual capacity (Part 89)
remains the only mechanism of this family that moved newblue1's residue.
(Runtimes here, ~5570 s, reflect three 4-thread jobs sharing the machine.)

## Part 100 -- batch 32, and the long-net capacity budget (pd31route)

Queued (user's order): (a) the threading candidate with batch 32
(`part100a.jobs`: pd27route, 10 threads, carry + any, newblue1 cells 7 / 8
/ 9, one at a time); then (b) the long-net budget.

`gr/pd31route.cpp` (forked from pd30route): CLAUDE.md's nested capacity
constraint. Long nets (pin bbox half-perimeter at or above the (100 -
`PD31ROUTE_LONG_PCT`)th percentile, default 10) pay a second
present-demand term on every edge, `PD31ROUTE_LONG_WEIGHT` (1) x histL x
((longDemand + amount) / (`PD31ROUTE_LONG_ALPHA` x cap))^5, its history
raised at every round boundary where long demand exceeds alpha x cap;
short nets' cost is unchanged. Slow tier and region stage (path A). The
long demand is a round-boundary snapshot (thread-count independence kept).
Off = pd30route. 639 checks (top 30% of spans 1..10 -> 8 / 9 / 10; long
demand counts only long nets; a long net pays exactly ((2 + 2) / 6)^5 more,
a short one nothing; on an 8 x 3 grid a short net goes straight and a long
net with its row budget full detours; 1 vs 4 workers identical with the
budget on). Queued (`part100b.jobs`, after part100a): newblue1 cells 7 / 8 /
9, one thread, alpha 0.6.

**Part 100a results** (batch 32; pd27route, 10 threads, carry + any; all
`status=OK`, `gr_verify` = `eval2008.pl`): newblue1 cells 7 / 8 / 9 ->
**38 / 44 / 44** (mean 42.0; WL 4809480 / 4893904 / 4894162) in 2053 /
1973 / 1948 s (~2.6x). Batch 64: 38 / 34 / 46 (39.3) in ~2230 s; one
thread: 24 / 38 / 36 (32.7). Smaller batches are slightly faster and no
better on overflow. The threaded configurations sit 20-28% above the
one-thread mean on newblue1 -- not significant at three runs each, but
consistently on the wrong side; the one-thread standard stays.

**Part 100b, stopped** (user's decision): pd31route alpha 0.6 / weight 1
in both stages kept newblue1's slow tier from converging -- overflow 5094 /
9378 / 4326 at round ~65 and 7258 / 7968 / 8010 at round ~130 (cells 7 / 8
/ 9), against the standard's 298 by round 74. Long nets (28430, the top
10%) carry most of the congested middle's traffic; capping them at 60% of
every edge fought the ordinary price instead of relieving it.

`gr/pd32route.cpp` (forked from pd31route): `PD32ROUTE_LONG_STAGES=region`
keeps the budget out of the slow tier (default both = pd31route). Self-test:
region-only leaves the slow tier's routes equal to the budget-off run's
and marks long nets only in the region stage. Queued (`part100c.jobs`):
alpha 0.8, weight 0.25, region only, newblue1 cells 7 / 8 / 9, one thread.

**Part 100c results** (pd32route, long budget alpha 0.8 / weight 0.25,
region stage only; one thread; all `status=OK`, `gr_verify` =
`eval2008.pl`): newblue1 cells 7 / 8 / 9 -> **42 / 34 / 34** (mean 36.7)
/ WL 4951823 / 4943459 / 4938353 (+1.4% to +2.0%), ~6200 s each with three
sharing the machine. Standard: 24 / 38 / 36 (32.7). No gain; slightly worse
overflow and longer wire. **The long-net budget is retired in both forms
tried** (0.6 / 1 in both stages stalls the slow tier, Part 100b; 0.8 / 0.25
in the region stage only is neutral-to-worse).

**Where the newblue1 residue work stands (Parts 89-100).** Of everything
tried to move it -- virtual capacity, the ring pull, flat and window-scaled
over-capacity penalties, and the long-net budget -- only virtual capacity
(Part 89) moved it. Threading (Parts 92-100) is 2.3-2.6x faster at a
not-significant +20-28% on newblue1's overflow; the one-thread standard
stays.
