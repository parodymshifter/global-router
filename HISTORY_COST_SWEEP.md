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

| benchmark | kroute (relaxable, default settings) |
|---|---|
| adaptec1 | (pending) |
| newblue1 | (pending) |
| bigblue1 | (pending) |

