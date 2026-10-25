# Best results on every benchmark -- scope (2026-10-12)

Goal: the best results this router can reach on every global-routing contest
benchmark set, no longer held to the 2008 standard's fixed settings. Quality
first; runtime matters because a faster router is a faster experiment loop,
and because the 2024 and 2025 contests scale scores by it. Every result stays
legal and cross-checked against the contest's own evaluator.

## Where we stand

| set | designs | ours | best known | gap |
|---|---|---|---|---|
| ISPD 2008 | 16 | 11 at zero overflow; newblue1 24-54, newblue4 144, newblue7 108, bigblue4 356, newblue3 38916 | newblue1 0 (LMgr), newblue4 144, newblue7 68, bigblue4 166, newblue3 ~31024 (NTUgr) | newblue3 +25% (at least 26264 is forced, Part 91), bigblue4 2.1x, newblue7 +40 (>= 54 forced), newblue1 +24-54; wirelength +0.3% to +9% vs NTHU-Route 2.0 before the reclaim pass |
| ISPD 2007 | 8 (3D) + 8 (2D) | the 3D designs are the 2008 contest's adaptec1-5, newblue1-3 | -- | the 2D (single-layer) variants are not yet run |
| ISPD 2024 | 13 | all six hidden designs beat the contest's best raw score, 12-38x faster (Parts 115-120) | contest #1 | already ahead; what remains is the score's own terms (overflow cost, vias) |
| ISPD 2025 | 6 visible + 6 blind | not started | contest winners (Hippo, PKU) | new problem: timing (WNS/TNS) and power, scored by OpenROAD |

Comparisons for 2008 are to published results through ~2013 (LMgr's re-run
table, Part 75); newer routers report on the 2018/2019/2024 sets instead.

## Where the 2008 time goes

newblue4, current standard (pd38route), 9504 s:

| stage | time | share | threaded? |
|---|---|---|---|
| region stage + refinement (fallback path A) | 6642 s | 70% | no |
| escalation + slow tier | 2691 s | 28% | batched slow tier exists (2024 only; fast08.env tests it, Part 124) |
| pattern stage, rebuild, WL recovery, layer assignment, I/O | ~170 s | 2% | mostly |

The 2024 pipeline dropped the region stage and refinement (no 2024 score
change), so nothing ever threaded them. They are where 2008's time is.

## Track A -- threading the region stage (the 70%)

The region stage reroutes every overflowed net in one box around the
overflow, each round, keeping every reroute, with a margin growing to 200.
Same shape as the slow tier -- but by construction one hotspot, which is
exactly where batched searches collide (Part 119: 78% of search work thrown
away on collisions before batches were spread over tiles).

A1. **Done, 2026-10-25 (HISTORY_COST_SWEEP.md Part 157):** exact split dead (chain = the whole round late);
    batched B 64 spread over 64-tile cells: carried 4-13%, work thrown away 9-28%, a round 3.7-7.4x faster on
    8 threads, but one batched round ends 10-26% higher in overflow late on. A2 next, judged on final residue.
    Original step: **Measure the ceiling first** (CLAUDE.md: measure a parallel scheme's
    ceiling before building on it). Instrument one region round on newblue4,
    bigblue4 and newblue7: for batches of 64/256 searched against a snapshot,
    the share that would be carried as conflicts, and the longest dependency
    chain. A day's work; decides A2 vs A3.
A2. **Batched region rounds** -- reuse `runSlowBatched` (snapshots, commit in
    net order, conflict carry, round-robin spread over tiles). Deterministic,
    the same at any thread count, not byte-identical to sequential: judged on
    overflow and wirelength. Worth it if A1 shows < ~50% carried.
A3. **Split the region into independent hotspots** -- overflow clusters more
    than two search margins apart share no edge, so they run on separate
    threads exactly (byte-identical per cluster). Helps where overflow is
    scattered (newblue7's several hotspots), not where it is one blob.
A4. **Exact per-round savings** -- keep the best state as a delta (the slow
    tier's `SlowBest`) instead of copying every member net's routes on each
    improvement (newblue7: 266k nets); skip the whole-grid overflow scan by
    tracking it incrementally. Byte-identical; size unknown until profiled.
A5. **Refinement** (supervised, revert if worse) -- batched with commit-time
    supervision in net order; smaller (~3% on newblue4).
A6. **Portfolio parallelism** -- a 2008 run uses ~1-2 of 14 cores. Run K
    variants of the post-slow-tier stages at once (virtual capacity on/off,
    margins, orders, seeds -- the A/B fallback is already a portfolio of 2)
    and keep the best by (overflow, max, wirelength). Not faster per run,
    but better results in the same wall time. Memory bounds K on the large
    designs (24 GB machine; newblue7 ~5.5 GB per state).

Expected: A2 or A3 plus A4 could take the large 2008 runs from 3-20 h to
1-8 h (unmeasured -- A1 decides). Every step judged at fastest turnaround:
newblue4 and bigblue4 first (the two with real hotspot time), threads on.

## Track B -- 2008 quality beyond the fixed settings

With faster runs, spend budget where the gaps are:

B1. **newblue3 (+25%)**: most of its overflow is forced (>= 26264). There the
    present-demand cost `hist * cong^5` keeps pushing nets out of edges that
    are over capacity whatever happens, onto long detours that add overflow
    elsewhere -- while the scored objective charges every unit beyond
    capacity the same. An objective-matched marginal cost for edges already
    past capacity (as the 2024 score-aware cost does for its own score) is
    the principled test. Also: the floor-gated plateau stopped its region
    stage at round 59 -- rerun with the stop off.
B2. **bigblue4 (2.1x the best)**: not cut-bound (tightest cut 89.3%), so
    breadth and budget: longer region budgets, wider boxes, virtual capacity
    tuned for it, the portfolio (A6).
B3. **newblue1 (24-54 vs 0)**: through-traffic, not pin-bound. LMgr reached
    0 with Lagrangian multipliers -- this project's own central idea
    (CLAUDE.md "Core cost model"). A Lagrangian-multiplier pricing variant
    for the region stage is the direct test.
B4. **newblue7 (108 vs 68; >= 54 forced)**: as B2, after A makes it cheap
    enough to iterate (currently ~20 h a run).
B5. **Wirelength on the 11 zero-overflow designs**: longer reclaim budgets
    first; then an LP/column-generation post-pass over candidate trees with
    capacity constraints (GRIP reported 9% better wirelength than the best
    published routers on the 2007/2008 sets) -- builds on `gr/lpbound.cpp`.

Decision needed (below): one setting for all sixteen, or the best setting
per benchmark.

## Track C -- ISPD 2024: squeeze the score

Already ahead of contest #1 on every hidden design. Remaining levers, from
the score's own terms: overflow cost (largest movable term on the cluster
designs), via cost, and the region/refinement stages that fast24 dropped
(re-test once Track A makes them cheap: 25 min bought 11 units of 2D
overflow on mempool_cluster_ranking in Part 115). Low priority next to B
and D unless a design's gap to its lower bound is large.

## Track D -- ISPD 2025: timing and power

New problem, same grid. Inputs: the 2024-style `.cap`/`.net` (plus the PPA
weights on line 2 of the `.cap`) and LEF/DEF/LIB/SDC; output: route segments
in DEF units with layer names (each via one layer at a time; metal1 unused).
Score: `w1*(WNS - WNS_ref) + w2*(TNS - TNS_ref)/N_endpoint + w3*(Power -
Power_ref) + w4*Overflow`, scaled by +-20% for runtime against the field
median; references are the submissions' medians. Evaluated by OpenROAD
(timing with the LIB/SDC, power from activity, congestion).

D1. **Reader and writer** (small): the `.cap` weights line, the 2025 output
    format. Route all 6 visible designs congestion-only.
D2. **An evaluation environment** (the blocker): OpenROAD with the contest's
    `run_evaluation.sh`. Options: build OpenROAD natively on the Mac (it
    supports macOS; a long build), a Linux cloud machine, or GitHub Actions
    (free here, but 7 GB runners and a 6 h job limit -- too small for
    mempool_cluster). Recommended: try the native build first.
D3. **Timing-driven routing** (CLAUDE.md build stage 6, "net typing"): net
    criticality from OpenSTA on a pre-route estimate, refreshed every few
    rounds; critical nets get the priority weight `w_i` that divides their
    congestion price, and a layer bias toward low-resistance upper metal.
    Power: shorter routes on high-activity nets (switching power ~ C).
D4. Compare with the published winners' visible-set numbers (leaderboard in
    the contest repo).

## Track E -- other sets (optional)

- ISPD 2007 2D (single layer): the 2D stage alone; small effort.
- ISPD 2018/2019 and ICCAD 2019 (LEF/DEF; the set CUGR, GAMER and later GPU
  routers report on): needs a LEF/DEF front end -- the OpenROAD move
  CLAUDE.md's porting seam already plans for, and D2 brings closer.

## Plan, 2026-10-14 (after Parts 124-132)

What changed since the scope above: fast08 is the 2008 standard (Part 124: zero-overflow designs
31-70% faster; residues newblue1 30, newblue4 154, newblue7 104, bigblue4 366, newblue3 38948);
D1 and D2 are done (Part 125: the 2025 reader/writer, the contest's OpenROAD fork built on this Mac;
ariane's congestion already beats 1st place, timing does not -- WNS +0.75, TNS +0.32 of score); the
difficulty atlas (Parts 130-131); layer assignment's scheduling is not the 2024 limit (Part 132).

**A2 was already tried.** Parts 92-99 batched the slow tier *and the region stage* (pd24-pd27route):
newblue1 2.3-2.5x faster, but mean overflow 39.3 vs 32.7 over three cells (+20%, p ~ 0.3), and the
"fresh" conflict rule was worse (50.0). pd37route and later descend from pd36route and do not carry
that code. So the region stage needs a different parallel idea, or the cores spent another way.

Order (runs: at most 3 at once; one long 2008 residue run plus short 2024 / 2025 runs at a time):

0. **Housekeeping (small).** Timing that survives sleep: every summary also records CPU time, and
   long batches ask for keep-awake (Part 124's newblue7 time was unreadable). Finish Part 132's
   follow-up (one large net's layer-assignment program split over its subtrees) if the phase
   breakdown shows the program is most of the large nets' time.
1. **A1 + A4: profile the region stage, then exact savings** (newblue4, the shortest residue run,
   2.6 h). Instruments with the stage markers (Part 126): where a region round's time goes --
   searches, best-state copies (newblue7: 266k nets per improvement), whole-grid overflow scans.
   A4's delta-kept best state and incremental overflow are byte-identical, so they need no quality
   judgment. Decision point: if searches are < ~70% of region time, A4 alone is worth weeks of A2.
2. **A3: independent hotspots in parallel (exact).** Overflow clusters more than two search
   margins apart share no edge: run their region loops on separate threads, byte-identical per
   cluster. Measure first how many such clusters newblue4 / bigblue4 / newblue7 have at the region
   stage's start (a grdiag pass on the saved routes -- no router run).
3. **A6: portfolio** -- with the faster runs, K variants of the post-slow-tier stages at once
   (virtual capacity on / off, margins, seeds), keep the best by (overflow, max, wirelength).
   Directly attacks the +4 to +10 that fast08 cost the residues, using idle cores rather than
   splitting one search. Memory bounds K (newblue7 ~5.5 GB per state, 24 GB machine).
   *Status 2026-10-19 (with B3, Part 135): done and in the 2008 standard, gated after path A --
   own settings + Lagrangian steps 0.05 / 0.2 / 1. newblue1 30 -> 24, newblue4 154 -> 140 (project
   best), bigblue4 366 -> 360, newblue3 and newblue7 flat; zero designs byte-identical. Four states
   fit easily (newblue7 peak 4.5 GB). Cost on residues: about +60% wall (the variants run after path A).*
4. **D3: timing-driven routing for 2025** (in parallel with 1-3: the 2025 runs are minutes).
   *Status 2026-10-16: D3a done; D3b's weight rule did not transfer to the blind designs (Part 133);
   the standard flow is now `gr/timing_loop.py` -- the contest's OpenROAD scores candidate strengths
   (Part 134: ten designs -22.86 vs -14.41). Next: per-net reclassification from routed slack.*
   D3a: from the evaluator's OpenSTA on our ariane route, the critical nets and how much of their
   delay is wire (vs. cell). D3b: those nets get a priority weight `w_i` (CLAUDE.md "Core cost
   model": priority divides the congestion price) and an upper-layer bias in layer assignment;
   bucketed into 3-5 classes (CLAUDE.md "Net typing"), refreshed every few rounds, not every round.
   D3c: all 6 visible designs vs the winners' published numbers (D4). Success = the score's timing
   terms below the beta medians on ariane without giving back its congestion lead.
5. **B: quality on the residues**, using 1-3's faster runs: B1 newblue3 (objective-matched cost
   past capacity; the floor-gated stop at region round 59 rerun off), B3 newblue1 (Lagrangian
   multipliers in the region stage -- this project's central idea), B2 / B4 bigblue4 / newblue7
   (budgets, boxes, the portfolio), B5 wirelength on the eleven.
6. **C and E as time allows**: re-test the region stage on 2024 once it is cheap (Part 115: 25 min
   bought 11 units of 2D overflow on mempool_cluster_ranking); ISPD 2007's 2D designs.

## Proposed order (2026-10-12; superseded by the plan above)

1. Part 124: fast08 on all sixteen -- done; adopted as the 2008 standard (2026-10-14).
2. A1 (ceiling) -> A4 (exact savings) -> A2 or A3 -> A6.
3. B1-B4 on the residue benchmarks, using the faster runs; B5 after.
4. D1 -> D2 -> D3 (D2 can start in parallel with 2, since it is setup).
5. C and E as time allows.

## Decisions (made 2026-10-12: all three as recommended)

1. **One setting or per-benchmark best for 2008?** The contest required one
   router for all; per-benchmark tuning finds lower numbers but is not
   comparable to published results. Recommended: report both -- the
   single-setting table as the headline, per-benchmark bests beside it.
2. **Runtime in "best"**: quality first with runtime reported (as now), or a
   time cap per run? Recommended: no cap for the final best-results runs;
   fastest-turnaround settings for every test run.
3. **The ISPD 2025 evaluation environment**: native OpenROAD build on this
   Mac (time, no cost), a cloud machine (cost), or skip timing for now.

Chosen: (1) both tables, single setting as the headline; (2) no time cap on
final best-results runs, fastest-turnaround settings for every test run;
(3) build OpenROAD natively on this Mac.
