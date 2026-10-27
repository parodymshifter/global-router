# Global router project

Context file for Claude Code. Read at session start.

## What this is

A global router for integrated circuits, built as a research prototype. The
central idea is that heterogeneous routing requirements (timing criticality,
wire width, via cost, layer preference) all reduce to one framework: a
Lagrangian relaxation where each net solves a min-cost Steiner tree in a graph
whose costs are shaped per net, and shared resources carry prices that rise
with use.

Target: results in the same range as published academic routers on the ISPD
global routing benchmarks, reproducible run to run, with at least one novel
contribution.

## Zero-overflow mandate (2026-09-05 addendum)

The Target line above was superseded, then partially restored. Full record
in `ZERO_OVERFLOW_AGENT_PROMPT.md` and `ZERO_OVERFLOW_CONVERGENCE_EXPERIMENT.md`
at the repo root; short version:

A session decided the single success metric should be **total overflow on
the three real ISPD benchmarks, and the goal is zero** -- not "in range of
published routers." First it measured (not assumed) that every router in
the `lroute`-through-`sroute` lineage plateaus well short of zero even with
3.3x more rounds (hroute: 174186/42642/302770 at default rounds ->
168644/41504/298682 at 172 rounds -- 1-3% further, and two of three
benchmarks going flat before the extended cap). Diagnosis: every router
here prices edges with exactly one reactive, current-round-only mechanism
(`lambda_e <- lambda_e * exp(epsilon * usage_e / capacity_e)`), which has no
memory of how long an edge has been congested, letting a net drift back
onto a recently-congested edge once some other edge's still-climbing price
overtakes it.

The prescribed fix, from the negotiated-congestion literature (Pathfinder,
FastRoute, NCTU-GR): a permanent, monotonically non-decreasing history-cost
term per edge, combined with the existing reactive price. Built as
`gr/iroute.cpp`, forked from `hroute.cpp` -- see that file's header for the
mechanism and `gr/iroute.cpp`'s own results table for what was actually
measured on all three benchmarks (gr_verify- and eval2008.pl-cross-checked).
**Read that table before assuming history-cost accumulation is either the
fix or a dead end** -- outcome, now measured: a real but modest win once
correctly tuned (a naive default growth rate made every benchmark worse;
a gentle one, combined with a much longer round budget than any prior
router here uses, beat hroute's own extended-round plateau on all three:
adaptec1 -2.70%, newblue1 -0.46%, bigblue1 -0.59%). Total overflow is
still in the tens of thousands (newblue1) to hundreds of thousands
(adaptec1, bigblue1) -- this is one validated technique on the path
toward zero, not the destination.

**2026-09-07 update**: `gr/kroute.cpp`, forked from iroute, replaced
iroute's permanent (monotonically non-decreasing) history term with a
LIFO-relaxable one -- every growth operation is pushed onto a per-edge
stack and popped/undone on a later clean round, so a genuinely-recovered
edge can earn its way back to usability instead of staying expensive
forever. A user-proposed refinement to their own proposed mechanism,
and both empirically validated: kroute beats iroute cleanly on all three
real benchmarks (adaptec1 -0.62%, newblue1 -0.63%, bigblue1 -1.39%, same
172-round budget both sides) -- the first mechanism in the whole
history-cost sweep (10+ configs) to win unanimously. Total overflow
remains in the tens to hundreds of thousands on every benchmark; see
`gr/kroute.cpp`'s header and `HISTORY_COST_SWEEP.md` at the repo root for
the full record, including a follow-up sweep of the growth formula's
exponent.

**2026-09-07 update (later the same day): `gr/vroute.cpp` is the new
current-best router in this lineage, by a wide margin, and it is not
another pricing-formula variant.** Every router through kroute prices
congestion on a fixed Steiner topology -- an abstract Manhattan-distance
MST built once, up front, from the pins alone, before any routing or
congestion information exists. `vroute` replaces that fixed topology with
MSMV (multi-source multi-sink) maze growth: at every step, every tile a
net has *already committed* becomes a simultaneous source and every
not-yet-connected pin becomes a simultaneous sink, and one real
congestion-aware search wave finds the genuinely cheapest next
connection, wherever it attaches -- a technique used by actual
ISPD-contest-winning routers (FastRoute, NTHU-Route) that nothing in this
lineage had tried before. Measured, full 172-round budget, against
kroute's own committed numbers, both legal per gr_verify and both
cross-checked exactly against eval2008.pl: **adaptec1 -36.55% (162584 ->
103172), newblue1 -25.23% (41014 -> 30668), bigblue1 -21.32% (292420 ->
230084)** -- unanimous, double-digit-percent, on all three real
benchmarks, dwarfing every pricing-formula experiment tried before or
since (iroute -0.6 to -2.7%, kroute another -0.6 to -1.4%, later
pricing-curve and net-ordering experiments low single digits at best, one
a regression -- see `HISTORY_COST_SWEEP.md`'s Parts 8-11). The lesson
this result actually teaches: the ceiling on how much tuning a *shared
cost formula's coefficients* can achieve was reached early in this
lineage; changing what the search is structurally allowed to do (attach
anywhere on already-built wire, not just fixed tree endpoints) moved the
number by an order of magnitude more than any coefficient has. Total
overflow is still real (hundreds of thousands on the two larger
benchmarks) -- this is a major step, not the destination -- and the cost
is real too: vroute takes roughly 7.5-8.25 hours per benchmark at this
round budget, versus kroute's several hundred seconds, a tradeoff a
production use of this technique would need to make practical. A
follow-up combining vroute's MSMV search with a post-convergence
length-weight rescue pass (`gr/zroute.cpp`) is a further, smaller
improvement on top (-21.36% vs kroute-equivalent on a reduced-round
screen, only marginally beyond MSMV alone) -- see `HISTORY_COST_SWEEP.md`
Parts 10-11.

**2026-09-26 update: `gr/pd2route.cpp` is the new current best, and it
reaches zero 2D overflow on adaptec1.** Two steps got here. First, search
boxes: the 6-tile slow-tier margin every router since mroute inherited was
the hidden limiter; growing boxes (`nv11route`, `round` 48/+2/200) took the
full-budget total from 277344 to 112086 (`HISTORY_COST_SWEEP.md` Parts
51-57), then flattened -- breadth was exhausted. Second, the negotiation
model: `pd1route`/`pd2route` replace the ever-rising lambda price with
NTHU-Route 2.0's present-demand cost (`cost = f + hist * cong^5`, recomputed
from current demand, so an edge gets cheap the moment nets leave it) *and*
NTHU's acceptance rule (keep every slow-tier reroute; no revert). Either half
alone loses (Parts 59-60): present-demand pricing under revert-if-worse
+38%; lambda pricing with keep-every-reroute never beat its starting point.
Together, at the reduced 30/75/100 screen budget: **adaptec1 7570 (2D 0),
newblue1 4694, bigblue1 2922 -- 15186 total, -86.5% vs 112086**, legal and
`eval2008.pl`-cross-checked, at a third of the runtime (Part 60b). The
remaining overflow is now mostly layer assignment's, not 2D routing's.

**Later the same day: adaptec1 reaches zero total overflow** (`gr/pd3route.cpp`,
`gr_verify` and `eval2008.pl` both 0 / max 0). `pd3route` replaces layer
assignment with NTHU-Route's approach -- per-tile layer choice, wire
capacity as a hard constraint, the via count as the objective (ISPD 2008
gives vias no capacity), NTHU's net order -- and real overflow now equals
2D overflow on all three benchmarks: adaptec1 0, bigblue1 58, newblue1 1186
(`HISTORY_COST_SWEEP.md` Part 62). What remains is the 2D stage's.

**2026-09-27: bigblue1 reaches zero too, and one command reproduces the
best on all three.** `gr/pd6route.cpp` (Part 68) adds NTHU's refinement
stage (`pd4route`), a hotspot rip-up that renegotiates every net around the
remaining overflow (`pd5route`), and NTHU's 2-pin-piece rip-up granularity
(`pd6route`). One setting, from scratch: **adaptec1 0, bigblue1 0, newblue1
66** (legal, `eval2008.pl`-cross-checked; the exact command is in Part 68).
newblue1's last few dozen sit in one 30 x 29-tile hotspot; `gr/lpbound.cpp`
(an LP-duality lower-bound tool, Part 65) could not certify any of it as
forced. Wirelength (7.8-8.7M scored) is well above NTHU-Route's (5.36M on
adaptec1) -- the next frontier after overflow.

**Wirelength, same day** (`gr/pd7route.cpp`, Part 69): pin-aware layer
assignment (a pin node's via stack must reach its pin layer inside the DP)
and an overflow-safe wirelength/bend recovery pass cut scored wirelength to
**5.99M / 5.75M / 5.06M** (-26 to -36%) at unchanged overflow 0 / 0 / 66 --
adaptec1 within 12% of NTHU-Route's 5.36M.

**2026-09-29: the full ISPD 2008 suite.** One setting (Part 71's), no
per-benchmark tuning, all sixteen benchmarks legal and
`eval2008.pl`-cross-checked: **eleven at zero overflow** (adaptec1-5,
bigblue1-3, newblue2, newblue5, newblue6); newblue1 54, newblue4 148,
newblue7 106, bigblue4 378, newblue3 38704 (every published router leaves
~31000 there). Wirelength +0.3% to +9.2% vs NTHU-Route 2.0. Runtime is
the remaining gap -- up to 27 h on the largest non-zero benchmarks.
Routers: `pd9route`, then `pd11route` (batched pattern stage, -37% runtime
at equal quality; Metal backend bit-identical to the CPU), then
`pd12route` (plateau stop for the late stages). Table:
`HISTORY_COST_SWEEP.md` Part 75.

**2026-10-01: the standard configuration.** `gr/pd14route` with
`gr/standard.env` (run_batch.sh job lines take `@standard.env`): Part 71's
pipeline plus three runtime mechanisms that each kept or improved
overflow -- a floor-gated plateau stop (Part 74), margin caps on the late
stages (Part 76), and a work-gated plateau stop counted in deterministic
search expansions (Part 80). The large benchmarks run 13-67% faster than
in Part 75 (bigblue4 23.3 h -> 7.7 h, newblue7 27.0 h -> 19.6 h). New
experiments start from this configuration. Run-to-run noise between
quality-neutral perturbations is ~2 units of overflow on the residues but
1.6-3.6% of wirelength (Part 78): judge wirelength ideas against each
benchmark's own spread, never one run.

**2026-10-03: the standard configuration moves to `gr/pd21route`** (same
`gr/standard.env`, plus the detour reclaim with a fallback, Parts 82-88).
A per-stage trace found the slow tier adds +24-34% detour that later
stages only partly recover; a reclaim pass reroutes the most-detoured nets
under fresh prices. It cut wirelength on every benchmark that reaches
zero (all 11: -0.16% to -4.1%; adaptec3 now 0.05% under NTHU-Route 2.0,
bigblue2 +5.7% -> +2.3%) but raised newblue1's residue in every variant
that let it run there, so it runs only when the standard path ends with
overflow <= 10 and is kept only if no worse on overflow: residues are
byte-identical to before and pay no runtime.

**2026-10-04: the standard configuration moves to `gr/pd23route`** (same
`gr/standard.env`, plus virtual capacity and the parallel fallback, Parts
89-90). Virtual capacity (FastRoute 3.0's idea, static form): the
standard path's region stage prices edges near its starting overflow 8
tracks lower, so full-but-legal hotspot edges stop attracting nets. It
moved newblue1's residue for the first time since Part 68 (48 -> 24 / 36 /
38, all three below six near-neutral runs at 44-54, p ~ 0.012) but left
newblue4's at 144. It runs on the standard path only -- on the reclaim
path its zone covers the deliberately injected overflow and made routes
0.4-1.7% longer -- so all 11 zero-overflow benchmarks are byte-identical
to before. The reclaim path now runs on a second thread (same routes,
bigblue2 -9% wall time). Reruns on it (Part 91): bigblue4 370 -> 356
(+39% runtime), newblue3 38916 (vs pd12route's 38704 -- not like for
like); newblue7 not yet rerun. Part 91 also certifies, by a cut bound
(`gr/cutbound.cpp`), that >= 26264 of newblue3's overflow and >= 54 of
newblue7's are forced for any router; newblue4's residue is pin-bound
(98.4% of its tightest cut).

**2026-10-06: first ISPD 2024 design routed** (Part 102). A format bridge
(`gr/g24_bridge.cpp`, `gr/g24_score.sh`) runs the unchanged standard
router on `mempool_tile_rank`: legal, 14.27M total cost (official
evaluator agrees exactly), 44 s; 2.54M of it is the idle-GCell floor. Via
demand on wire capacity cannot cross the bridge, and reserving capacity
for it made results worse, so the native reader (`ISPD24_FORMAT.md` step
2) with a via term in layer assignment comes next.

**2026-10-08: the ISPD 2024 standard is `./pd36route @standard.env
@standard24.env @fast24.env`** (Part 115). The contest winner routed
mempool_cluster in 217 s against our 16.9 h; most of the gap was work, not
hardware. Re-ablating the pipeline under the 2024 score dropped wirelength
recovery (it made the score worse), layer assignment's negotiation rounds
(+-0.01%), and the region/refinement/fallback stages (no change); pd36route
then removed whole-grid work per batch in the pattern stage and redundant
work in layer assignment and the 3D passes, byte-identical to pd35route.
All six hidden designs now beat the contest's best raw score, 12-38x
faster (mempool_cluster_ranking 61017 s -> 1599 s, -0.57% vs #1). Every
run ends with a `stage report:` table (per-stage time, footprint,
objective and its change, work): read it before tuning anything. The
2008 standard is unchanged.

**2026-10-09: the ISPD 2024 standard is `./pd38route @standard.env
@standard24.env @fast24.env PD37ROUTE_THREADS=8`** (Part 118). The slow
tier now runs in batches of 64 searched in parallel against snapshots and
committed in net order (`PD38ROUTE_SLOW_BATCH=64`, in `fast24.env`): a
better score on all 13 designs, the same at any thread count.
mempool_cluster_ranking takes 526 s (Part 114: 61017 s). An exact
dependency graph could not thread this stage: its hard nets crowd into
the same hotspots (longest chain 19176 of 54283 nets). Measure a parallel
scheme's ceiling before building on it.
Part 119 then dealt each round's nets round-robin over 64 x 64-GCell
tiles in batches of 256 (`PD38ROUTE_SLOW_SPREAD=64`, also in
`fast24.env`): consecutive nets sit together, so unspread batches piled
onto one hotspot and threw away 78% of their search work on collisions.
mempool_cluster 809 -> 658 s, mempool_cluster_ranking 511 s; scores
within +-0.02%.
Part 120 then threaded the pattern stage's batches on the dependency graph
and shared one graph across the 3D passes (`PD38ROUTE_PATTERN_DAG=1`,
`PD38ROUTE_P3D_REUSE=1`, in `fast24.env`), byte-identical:
mempool_cluster_ranking 461 s, mempool_cluster 584 s.

**2026-10-14: the ISPD 2008 standard is `./pd39route @standard.env @fast08.env`** (Part 124).
`gr/fast08.env` brings the 2024 runtime mechanisms to 2008: 8 threads, the pattern stage on the
dependency graph, and the slow tier batched (256, spread over 64 x 64 tiles). All sixteen legal and
`eval2008.pl`-cross-checked: the eleven zero-overflow designs stay at zero and run 31-70% faster
(wirelength within +-0.2%); the residues moved newblue4 144 -> 154, bigblue4 356 -> 366, newblue7
100 -> 104, newblue1 30 (inside its 24-38 spread), newblue3 38948. pd39route gives the same routes
on Mac and Linux (Part 123). Most of a residue run is now the region stage (newblue7: 88%), which
nothing threads yet -- BEST_RESULTS_PLAN.md track A.
Since 2026-10-19 (Part 135) `fast08.env` also carries the late-stage portfolio: when path A of the
fallback ends above 10, it is rerun from its start with Lagrangian multipliers (steps 0.05 / 0.2 / 1),
each on its own thread, and the best kept -- newblue1 24, newblue4 140, bigblue4 360, newblue3 38898,
newblue7 104; designs ending at or below 10 are byte-identical and pay nothing.
`gr/speed08.env` (2026-10-20, Parts 140-141) is a speed setting layered after the standard, not part of
it: the region stage's box capped at 100 (the reclaim path kept at 200) cuts the residue runs 17-26% with
13 of 16 designs identical and newblue1 / newblue4 4 units worse (inside their spreads). Since 2026-10-23 it
also carries the slow tier's plateau stop (`PD39ROUTE_SLOW_PLATEAU=25,1,1000`, Part 148): exact where the best
has stopped moving, at most +0.56% overflow elsewhere, for 14-45% less time on runs that never converge.
Since 2026-10-27 it also carries the batched region stage, gated (`PD39ROUTE_REGION_BATCH=64,64`, `_ROUNDS=2`,
`_MAX=1000`, Parts 163-165b): region stages entering at <= 1000 units of 2D overflow run in parallel batches,
portfolio variants sharing the cores round by round -- newblue3 byte-identical, bigblue4 372 and newblue7 106
(standard 360 / 104) at about half the standard's time; measured with the standard, not yet with the box cap.
Since 2026-10-22 (Part 146) `fast08.env` also carries the region stage's early stop
(`PD39ROUTE_REGION_ABORT=1000,20`): above 1000 units of entering 2D overflow, a region stage still no
better than its start after 20 rounds stops there -- exact (such stages returned their start; routes
byte-identical, runs 31-48% faster on hard variants), and it cannot act on the sixteen benchmarks.
Since 2026-10-24 (Part 154) pd39route's searches use a 4-ary heap with decrease-key (`SearchHeap`) in place of
`std::priority_queue`: the same expansions in the same order, routes byte-identical, newblue1 (region stage
capped at 120 rounds) 1589 s -> 1059 s (slow tier -35%, region stage -34%); Part 158's 16-byte cache-aligned
entries another -8% (1038 s -> 955 s), byte-identical. One record per node instead of parallel arrays was 6.6%
slower (Part 156). Track A step A1 (Part 157): an exact split of the region stage is dead (its dependency chain
is the whole round), batched rounds (64 per batch, spread over tiles) are 3.7-7.4x faster but end one round
10-26% higher in overflow -- step A2 must measure the final residue.

**2026-10-14: the ISPD 2025 standard is `./pd39route @standard.env @standard24.env @fast24.env
@standard25.env`** (Part 133). `gr/standard25.env` adds timing plus capacitance to the 2024
standard: nets with negative input pin slack pay resistance-weighted wirelength by class, every net
pays capacitance-weighted wirelength, and both strengths scale with the design's own score weights
(the R and C per layer are NanGate45's, from the contest's `setRC.tcl`). Against our own unbiased
routes: ariane's contest score 2.30 -> 0.14 (1st place 0.90), bsg_chip -1.38, NV_NVDLA_partition_c
-6.22, mempool_tile_wrap -0.51 (lower is better). Score timing with the contest's OpenROAD fork and
its endpoint count, registers + outputs; mempool_group, mempool_cluster and the blind designs are not
yet measured under it.

**2026-10-16: the ISPD 2025 standard flow is `python3 gr/timing_loop.py <design dir> <work dir>
--out <solution>`** (Part 134), with the contest's timing engine in the loop: it routes the design
unbiased and at top-class timing strengths 20 / 180 / 540, scores each with the contest's evaluator
and OpenROAD (one session), tries the power term (kp 1, 4) at the best, and keeps the best by the
contest score's change. Ten scored designs sum -22.86 against the unbiased routes (`standard25.env`
alone -14.41), no design worse; 6 routes and two OpenROAD sessions per design (3 min to ~1.5 h).
`@standard25.env` stays as the one-route fallback where OpenROAD is not available. Part 133 found why
a fixed rule fails: the best strength (20-540) and whether the power term helps depend on each
design's timing paths, which neither its score weights nor its wire geometry reveal.

What did not change: the "Target" line above is stale (kept for history,
not deleted) but the discipline below it -- verification first, cross-check
against eval2008.pl, supervised escalation, empirical tuning over reasoned
guesses -- is exactly what made this mandate answerable with real
measurements instead of argument, and still governs every router added
after this point.

## Build order

Each stage must pass the benchmarks before the next begins.

1. **Verification first.** `.gr` reader, overflow verifier, connectivity
   checker, congestion visualizer. Done before any router exists.
2. **L-router.** Throwaway pattern router, no congestion awareness. Exists only
   to produce valid solution files so the verifiers can be cross-checked.
3. **Pattern router.** L and Z shapes, 2D, rip-up and reroute.
4. **Pricing loop.** Replace ad hoc congestion cost with multiplicative weights.
5. **3D.** Layered graph, direction-split vertices, via pricing. Roughly triples
   the code. Most debugging lives here.
6. **Net typing.** Weights, widths, nested constraints. Needs a timing engine.
7. **Parallelism.** Last. A parallel bug in a router you do not yet trust is
   unfindable.

Scope cuts available if time runs short: synthetic criticality instead of real
static timing analysis; single threaded; shallow 3D. A minimum publishable
version is stages 1 through 4 plus one novel piece.

## Core cost model

Each net sees, on each edge:

    cost = length + lambda_e * width_i / w_i

where `lambda_e` is the edge price, `width_i` the net's wire width, and `w_i`
its priority weight. Priority divides the price, so a critical net ignores
congestion; width multiplies consumption, so a wide net detours first.

Prices update multiplicatively after each round:

    lambda_e  <-  lambda_e * exp(epsilon * usage_e / capacity_e)

Every heterogeneous requirement enters as either a per-net cost multiplier or a
per-resource consumption coefficient. Nothing needs a special case.

Caveat from Garg and Koenemann: iteration count scales with the LP's width,
meaning the largest consumption-to-capacity ratio. Mixing 1-track and 6-track
nets in one instance genuinely slows convergence. Bucket nets by width class or
rescale.

## Design decisions already made

**Vias are ordinary edges.** Build the 3D layered graph with vertical edges
between layers carrying via cost. Replace each vertex with direction-labeled
copies so turn and via penalties stay edge-additive and shortest-path still
works. Via sites get their own capacity and their own price.

**Wire width is a consumption coefficient, not a cost.** The capacity
constraint becomes `sum(x_ie * width_i) <= cap_e`. To let the router choose
width, expand each grid edge into parallel edges, one per allowed width, each
with its own (delay, consumption) pair. Dijkstra then picks width as a side
effect of picking the path.

**Tree shape is a per-net parameter.** Prim-Dijkstra with a single parameter
gamma slides continuously between minimum wirelength and minimum radius. Set
gamma per net from timing slack.

**Layer ranges are assigned up front and frozen.** Long nets to thick upper
metal, short nets to lower layers for pin access. Most conflict disappears by
construction. What survives is via and pin-access contention, which is why via
sites need their own price. Do not change layer range or width class mid-flow:
it invalidates every accumulated price on the affected layers.

**Nested capacity constraints, not a single budget.** Impose both
`sum(all nets) <= cap` and `sum(long-range nets) <= alpha * cap`. The dual
produces two prices; long nets pay both, short nets pay one. Long nets get
squeezed out of a contested edge before local nets feel anything. Set alpha per
tile from a pre-routing demand estimate.

**Consider a fair-share objective.** Minimizing `sum(w_i * cost_i)` will starve
a class, because the optimum happily inflicts a large penalty on many cheap
nets to save one expensive one. An alpha-fair or proportional-fair utility such
as `sum(log(slack remaining))` prevents collapse. Costs some wirelength, buys
routability. Measure both.

**Seed prices from a demand estimate.** Flat initial prices cause a stampede in
the parallel setting, and long nets stampede hardest because they touch the
most edges. Run RUDY or a cheap pattern route first.

**Decompose giant nets.** A 200-terminal net should be split at the coarse
level into subnets, not routed as a monolith. Also removes a parallelism
serialization point. A giant net that skips the search entirely for
performance must still charge usage for whatever route it emits — omitting
that silently hides real capacity consumption from every other net's pricing
decisions, and the bug won't show up in that net's own routability, only as
an unexplained mismatch against an independent overflow count.

**Size maze search boxes for real detours, and grow them for nets that stay
overflowed -- don't cap them at a small constant.** (Revised 2026-09-24; the
earlier rule said the opposite.) The earlier rule came from `mroute.cpp`,
where the box scaled with net span (`span/2`, uncapped) and a handful of long
nets searched nearly the whole grid -- one call covered all 629856 cells of
324x324x6 against a 4184-cell average, making a round take 105 seconds. A
sweep of a fixed cap from 2 to 20 then found overflow flat (458-461K) while
runtime rose, and 6 was chosen for speed. That sweep was real, but it ran at
~460K total overflow on a 3D fixed-topology router, where search breadth
wasn't the binding constraint; every later router inherited the 6 without
re-testing it. Re-tested on `nv4route` (MSMV, 2D collapse) in
`HISTORY_COST_SWEEP.md` Part 51, adaptec1 at the 30/75/100 screen budget:
margin 6 -> 74984 overflow, 12 -> 59124, 24 -> 44336, 48 -> 31778, still
improving; runtime grew far more slowly than the margin. The ISPD 2008
winner (NTHU-Route 2.0, Part 52), which reaches zero on adaptec1 and bigblue1
here, starts its search box at 10-25 tiles and grows it every iteration.

What still holds from the old rule: never scale the box with net span alone
(that's what blew up runtime), and keep the retry-on-no-path doubling as a
safety net. Grow the box by congestion history -- for nets that remain
overflowed, round after round -- with a hard cap, so breadth is spent where
it buys detours instead of on every search. Once real shortest-path search is
in play, use A* over plain Dijkstra: a Manhattan-distance heuristic (ignoring
layer) is admissible, since every H/V/via edge costs at least 1, so it
changes nothing about which path is found and roughly halves the work again;
it matters more, not less, with wider boxes.

**Combine a cheap solver and an expensive solver by escalating individual
nets, not by choosing one algorithm for the whole design.** A real per-tree-
edge shortest-path search finds routes a fixed pattern menu structurally
cannot, but at an order of magnitude more runtime, and which whole design
benefits is not predictable in advance from a handful of benchmarks — one
router beat another outright on one benchmark and lost clearly on the other
two. Run the cheap tier to convergence first; escalate to the expensive tier
only the nets still overflowed afterward, and once a net escalates, leave it
there for the rest of the run — no reason to downgrade after paying for a real
search.

**Escalation, or any switch to a more powerful mode, must be supervised per
net — never accepted as an unconditional batch.** Reroute, check the overflow
delta local to the edges that net touches (not a full grid rescan), and revert
on the spot if it made things worse — the same rule a post-convergence cleanup
pass should already follow. This is not a hypothetical guard: accepting every
escalated net's reroute unconditionally in one pass put 30% of a design's nets
on the expensive tier at once, spiked overflow to roughly 25x the cheap tier's
own result, and never recovered past it even after further rounds.

**This rule belongs to the pricing model it was found under** (revised
2026-09-26). Under the lambda price, which only ever rises, it is
load-bearing: `pd2route` with lambda pricing and every reroute kept sent 2D
overflow from 219786 to 8 million and never came back below its starting
point (`HISTORY_COST_SWEEP.md` Part 60, J3). Under NTHU-Route's
present-demand price it is the obstacle: the revert rule undid most
reroutes (an edge at capacity is barely dearer than one just under it, so
reroutes land on just-full edges), and the run plateaued at +38% vs lambda
(Part 59). Keeping every reroute under that price converged adaptec1's 2D
stage to zero (Part 60). So: supervise per net under a monotone price;
under a present-demand price with history, keep every reroute and let the
history term do the policing. Changing the pricing model means re-testing
this rule, not inheriting it.

**The per-round pricing threshold is fixed, not a scheduled or excused one.**
Ramping it up or down across rounds, or permanently excusing nets that stayed
under it, were both tried directly and both made results monotonically worse
the more aggressively they relaxed. The common cause: once already-routed
nets must react to a moving threshold, the loop has to reroute everyone every
round, which reintroduces exactly the churn selective rip-up exists to
prevent. Relaxation only pays off as a one-time, supervised, post-convergence
pass — never as a moving target inside the main loop.

## Net typing: detour slack

Timing slack is in picoseconds, routing spends microns. Convert with the Elmore
derivative at the current estimated length L0:

    dd/dL = R_d * c + r * C_L + r * c * L0
    L_slack = slack_i / (dd/dL)

Then divide by track pitch to get a detour budget in grid units.

Two properties worth noting. The derivative is net-specific, so a weak driver
into a heavy load has a much larger `dd/dL` than a strong driver into a light
one at identical length. And it grows with L0, so long nets pay more per
additional micron. That second effect is the real reason long nets look
inflexible, and it is a timing fact rather than a geometry fact.

Combine with route freedom:

    F_i = L_slack_i * log(route_count_i)

Route count proxies, cheapest first: bounding box entropy (for a two-pin net
with separation dx by dy, `log C(dx+dy, dx)`); randomized tie-break sampling;
blockage-aware sampling.

**Bucket F into three to five classes. Do not map it to a continuous weight.**
Early slack numbers are noisy, and a continuous weight fits the noise. Coarse
buckets are stable across iterations, which keeps the pricing loop from
thrashing.

Failure modes: nets crossing a clock domain boundary have meaningless slack
until the crossing is modeled, so class them separately. Nets with huge fanout
are driver-limited, so `dd/dL` is dominated by `R_d * c` and the net looks
falsely rigid; buffer first, type second. Nets whose slack sits within noise of
zero go to the middle bucket, not the critical one. The critical bucket is a
scarce resource and filling it with false positives starves the nets that
genuinely need it.

**Size is not the same as inflexibility.** A net spanning half the die has
combinatorially many near-shortest routes. A short net crossing one congested
channel may have two. Order by detour slack over topology count, not span.

## Module structure

Setup pass, runs once:

    netlist input -> net typing -> grid builder -> seed prices

Pricing loop, runs to convergence:

    price vector -> scheduler -> Steiner solvers -> usage accumulator
                 -> price update -> routability check -> output
                                 -> (loop back to price vector)

    usage accumulator -> timing analysis -> (weights back to price vector)

Only two things move inside the loop. Downward, prices flow into the solvers as
a read-only cost overlay. Upward, usage flows back as a sparse vector of
resource increments. The solvers never talk to each other. The only shared
write is the accumulator.

The scheduler is not just a work queue: it decides which nets are routed
against the same price snapshot, which sets the effective step size. Coloring
by disjoint bounding boxes gives run-to-run reproducibility *and* reduces the
stampede, since same-color nets cannot contend for the same edges.

Timing runs on a slower clock than the main loop. Refreshing criticality every
iteration is wasted work and destabilizes pricing, since weights and prices
then chase each other. Every third or fourth iteration.

**One arrow not in the diagram:** if the routability check fails badly enough,
the right answer is a return to placement, not another pricing round. Build
that exit explicitly. Routers that lack it burn hours proving something
unroutable.

## Parallelism plan (stage 7, not before)

(2026-09-28) Concrete GPU plan: `GPU_BACKEND_PLAN.md` -- batched pattern
routing on Metal (local, M4 Pro, kernel compiled at run time since only
the Command Line Tools are installed) and CUDA (cloud GPU CI), one binary
choosing its backend at run time (`GR_GPU=auto|cuda|metal|cpu`), integer
fixed-point costs so every backend is bit-identical to the CPU reference.

- **Over nets, given prices.** Once prices are fixed, every net's tree is
  independent. Hazard is stampede: all nets see the same stale prices and pile
  into the same cheap region. Parallel runs want a smaller epsilon than
  sequential ones, which eats part of the speedup.
- **Asynchronously.** Atomic fetch-and-add on prices, Hogwild style. Updates
  are sparse and nearly commutative.
- **Inside a net.** Maze routing is single-source shortest path. Delta-stepping
  on CPU. On GPU, bounded-hop Bellman-Ford wavefronts, which is a sparse
  matrix-vector product in the min-plus semiring. Batch nets as columns for one
  min-plus SpGEMM kernel per wavefront.
- **Geometrically.** Tile, route inside tiles, solve a small interface problem.
  Raecke and FRT tree embeddings turn the interface problem into a tree problem
  at O(log n) distortion.

Protect determinism: color the conflict graph so simultaneously routed nets
have disjoint bounding boxes.

Expect memory bandwidth, not core count, to be the ceiling. Random access into
a multi-gigabyte price array from many threads. Parallel efficiency 30 to 50
percent, not 90. This is invisible on a laptop with unified memory and a single
NUMA domain; it appears on a dual-socket server. Plan for a partitioned or
replicated price array from the start.

## ISPD `.gr` specification details

These are easy to get wrong and quietly corrupt results.

- **Capacity measures available space, not tracks.** Edge utilization is the
  sum of the widths of nets crossing it, plus the layer minimum spacing charged
  once per net. Their example: width 20, spacing 10, capacity 450 means 15
  tracks.
- **Nets with more than 1000 pins need not be routed.** Nor need nets whose
  pins all fall in one tile. Flagging these produces thousands of false
  positives.
- **Scoring charges 1 tile of wirelength per via crossing.** An M1 to M5 change
  costs 4.
- **Capacity adjustments use tile indices**, format
  `column row layer column row layer reduced_capacity`. Pin positions, by
  contrast, are real coordinates: convert with
  `floor((pin_x - llx) / tile_width)`.
- Preferred direction is expressed as zero capacity in the other direction.
- Solution net header is `<name> <id> <numSegments>` (three fields).
- Ranking is total overflow first, max overflow as tiebreak, then wirelength
  with a CPU time factor.

## Verification principles

**The verifier is not primarily for catching router bugs.** Historical
experience on production systems: its dominant yield is bad input data, meaning
circuit descriptions and geometry codings that were wrong before the router
touched them. Garbage inputs are indistinguishable from routing bugs when you
only look at the output, and every hour spent debugging a correct router
against corrupt data is wasted twice.

The ISPD benchmarks are curated and clean, so developing only against them
never builds the instinct that inputs might be lying. Anyone going from
benchmarks to real LEF and DEF hits that wall hard.

**In a router, wrong code still runs and still produces a route.** It just
produces a worse one. The danger is not a crash, it is quietly accepting a
subtly wrong cost function and spending months tuning around it. Every piece of
code needs a test that would fail if it were wrong.

**A test suite that only feeds good input is indistinguishable from one that
always passes.** The self test in `gr_verify.cpp` deliberately feeds broken
solutions: a net in two disconnected pieces, a net omitted, a diagonal segment.

**Cross-check against the official Perl evaluator** (`eval2008.zip`; not kept
in the repository since it carries no license -- `sh gr/fetch_eval2008.sh`
downloads it from ispd.cc into `gr/eval2008/` and checks its SHA-256). Total
overflow, max overflow, and wirelength must agree. If they diverge, assume our
code is wrong first: theirs scored an actual contest. Note that 2008-era Perl
may fail on modern interpreters, usually `defined(@array)`, removed in 5.22.

**The readers are strict** (2026-10-24, Part 155): `gr/gr_io.h` and `gr/g24_io.h` stop the run, naming the file
and line, on anything out of spec -- malformed or misaligned records, out-of-range values, pins outside the grid
(tile = floor((x - llx) / tileW)), duplicate names, adjustments that join layers or non-neighbours or raise
capacity, text after the end of the design -- instead of silently repairing or skipping it. `gr/input_check`
reads a design without routing it (`./input_check design.gr`, `./input_check design.cap design.net`); a new input
rule gets a broken-file case in its `--selftest`.

**Run the Perl evaluator every time, not just when something looks wrong.**
After any round that produces a new result on all three ISPD benchmarks
(adaptec1, newblue1, bigblue1) — a new router, a tuning change, a bug fix —
run the downloaded Perl evaluator against each of the three solution files
and compare its total overflow, max overflow, and wirelength against
`gr_verify`'s numbers for the same file before recording the result as
settled. A result only checked against our own verifier is checked against
our own possible bug; agreement with the independent scorer is what makes a
number trustworthy enough to compare across commits.

**Provenance tracking.** Record, for every committed edge, which net asked for
it, which constraint permitted it, and which input record supplied that
constraint. Then a verifier hit resolves in one step instead of a debugging
session: the failure either traces to a router decision (router bug) or to an
input record (data bug, display it). Costs 20 to 40 bytes per committed edge.
Usual compromise: run without it, re-run with it enabled on the failing region
only. Add this at the verification stage; it is painful to retrofit.

Limit worth knowing: provenance identifies which input record was involved. It
cannot tell you whether that record reflects what the designer meant.

## Testing arithmetic edge cases

Every formula in this project divides, exponentiates, or takes a log of a
quantity that can legitimately be zero, negative, or huge. Wrong behavior
there doesn't crash — it silently produces a worse route, which is the
failure mode "Verification principles" above already warns about. Any code
implementing one of these needs a test for its edge case, not just its
happy path:

- `cost = length + lambda_e * width_i / w_i` — `w_i == 0` (unset or
  zero priority weight).
- `lambda_e <- lambda_e * exp(epsilon * usage_e / capacity_e)` —
  `capacity_e == 0` (a `.gr` reduced-capacity adjustment can zero an edge),
  and `lambda_e` overflowing after many rounds on a persistently congested
  edge.
- `L_slack = slack_i / (dd/dL)` — `dd/dL == 0` or negative (a slack sign
  flip, or a driver/load combination that degenerates the Elmore term).
- `F_i = L_slack_i * log(route_count_i)` — `route_count_i <= 1`, where
  `log` is undefined or zero.
- `log C(dx+dy, dx)` for bounding-box entropy — `dx == 0` or `dy == 0`
  (a net whose pins share a row or column).
- `alpha * cap` nested capacity budgets — `alpha == 0`, and integer overflow
  when `width_i` or `cap_e` values are summed across many nets on one edge.

Write the test to fail on the current code before fixing it, per the
self-test pattern in `gr_verify.cpp`.

## Porting seam

`RoutingGrid` owns geometry, edge indexing, and capacity, and knows nothing
about file formats. Replacing the `.gr` reader with an OpenDB front end should
not touch anything below it. Keep it that way.

Plan is to move into OpenROAD at stage 5, where its LEF/DEF parsing, OpenSTA,
TritonRoute, and DRC checking start paying for themselves, and where its
existing FastRoute-derived global router provides a known-good baseline to
compare against. Standalone until then, because the way this project dies is
spending months on infrastructure before testing whether the central idea
helps.

## Conventions

- C++17. **Dependencies beyond the standard library are allowed where they
  enable the roadmap's required improvements** (revised 2026-09-28; the
  earlier rule was "no dependencies beyond the standard library at the
  standalone stage"). The roadmap, in order: CPU runtime work including a
  batched pattern-routing stage; the remaining ISPD 2008 benchmarks; the
  ISPD 2024 `.cap`/`.net` format; CPU multithreading; a GPU port of the
  pattern-routing kernel (Metal locally, CUDA on a cloud GPU -- see
  `GPU_BACKEND_PLAN.md`); OpenROAD. Conditions on every dependency:
  - It is isolated behind an interface in new files. The CPU path stays
    buildable and testable with only a C++17 compiler (the one-line `g++`
    build every router uses today), and the CPU implementation is the
    reference any accelerated path must match bit for bit.
  - It is optional at run time where the hardware may be absent: a GPU
    build detects its device and falls back to the CPU backend.
  - Its reason is recorded where it is introduced (the file header, and
    the plan or `HISTORY_COST_SWEEP.md` Part that motivated it). Vendored
    code (e.g. `third_party/metal-cpp`) keeps its license file.
  - A build system beyond the one-line `g++` (CMake) is allowed for the
    targets that need it.
- Build with `-Wall -Wextra`, keep it warning-clean.
- Tile coordinates zero based internally; `.gr` layers are one based, convert
  at the reader boundary.
- Prefer dense arrays over hash maps for per-edge data. The access pattern
  determines the parallel ceiling later.
- **Licensing** (2026-10-11): source code is Apache-2.0 (`LICENSE`),
  documents -- the top-level Markdown files and `recaps/` -- are CC BY 4.0
  (`LICENSE-DOCS`); `NOTICE` says which is which. New files follow the same
  split. Third-party code or data comes in only under a license compatible
  with Apache-2.0, with its license file kept beside it; anything without
  one (like the ISPD 2008 evaluator) is fetched from its source by a
  script, never committed.

## Long-running work and background agents

Learned the hard way during the zero-overflow effort: a background agent
that goes idle waiting on a long-running experiment cannot always be woken
back up to continue -- `SendMessage` to resume it is not reliably available,
and when it isn't, the parent session ends up re-deriving everything the
agent knew from scratch just to keep going. Two rules follow from that:

- **Write each step's state to a file, not just conversation context.**
  Tuning sweeps, convergence experiments, and multi-run comparisons should
  land their raw numbers in a file (a trace log, a small results table) as
  they're produced, not only summarized in a chat turn. A stuck or
  unresumable agent's work is still usable if the next session can read
  what it already measured off disk instead of starting over. `ZERO_OVERFLOW_CONVERGENCE_EXPERIMENT.md`
  is the pattern to follow: raw traces first, conclusion written up once
  they're in.
- **Cap concurrent background processes at 3.** A tuning sweep that
  launches one process per parameter value is tempting to run all at once
  (this project ran 6-7 router processes in parallel at one point, on the
  reasoning that there were enough CPU cores) -- but more concurrent runs
  means more of them finish while no one is watching, which is exactly
  when an unresumable agent's progress goes unobserved the longest. Batch
  sweeps in groups of 3 or fewer, even when hardware headroom would allow
  more.

## Automatic checks (2026-10-11)

- `QUALITY_CHECKS.md` explains the automatic checks: static analysis,
  sanitizers, a nightly Valgrind run and a timing canary
  (`.github/workflows/quality-checks.yml`), and GitHub's CodeQL scanner
  (`.github/workflows/codeql.yml`), next to the per-router build and
  self-test matrix (`build-and-validate.yml`).
- Do not use `[skip ci]` on commits that change routing code.
- When a new router generation (like `pd39route`) becomes current, change
  `ROUTER` at the top of `quality-checks.yml` and the file names in
  `codeql.yml`.
- Run `./<router> --selftest` on the Mac before each commit.
- **Every new setting read in `main()` gets a check in `gr/e2e_settings.py`** (2026-10-22, Part 149): run the
  router itself with the setting given through the environment, and look for evidence the mechanism acted (a
  trace line only it writes while it runs, or a change in its stage's own trace) -- never a start-up echo.
  A self-test that sets the variable directly cannot catch a setting `main()` reads after its stage has run
  (Part 148's slow-tier stop). Run `python3 gr/e2e_settings.py <router>` (about a minute) before committing a
  router change that adds a setting; CI runs it on every push.
- Load settings files line by line (as `gr/run_batch.sh` does), never by sourcing them with `.`:
  `fast08.env`'s `PD39ROUTE_PORTFOLIO` value contains `;`, which a shell treats as a command separator.
- Write a new router binary to a fresh file (`rm` it first, or build to a new name): copying over an
  existing binary in place leaves macOS's code-signature cache stale and later launches are killed
  at once (Part 134: three mempool_cluster runs and a test run lost that way).

## Profiling (2026-10-12)

`PROFILING.md`: Apple Instruments on any run -- `gr/instruments_profile.sh` (launch, or attach to a
running router for a window) and `gr/instruments_report.py` (per stage: CPU time, busy threads,
core types, cycle breakdown with CPU Counters, top functions). Stage markers are on in
`standard.env` (`GR_SIGNPOSTS=1`, pd39route on; routes unchanged). Profile before tuning runtime,
and on a quiet machine before comparing times.

## Memory tracking

Every run recorded as a measured result -- not just overflow, max
overflow, and wirelength -- should also capture memory, the same way
runtime already gets recorded. `gr/run_tracked.sh benchmark.gr out.route`
wraps a router invocation: `/usr/bin/time -l` gives the kernel-tracked
physical peak (maximum resident set size and peak memory footprint,
tracked over the whole process lifetime, not a poll-based guess), and a
5-second-interval `ps` poll gives visibility into virtual memory (VSZ)
over the run. Virtual size has no equivalent kernel-tracked peak on macOS
-- it is dominated by the dyld shared-cache mapping and reserved-but-
uncommitted address space, and was observed pinned at essentially the
same ~435GB across every router process regardless of actual workload
size or phase. A flat VSZ *is* the honest, complete result in that case,
not a metric this project needs to chase harder for a peak of; physical
RSS is where real signal about a router's actual footprint shows up.

Retrofitting memory tracking onto an already-running process does not
work -- there is no way to recover peak RSS or VSZ after the fact for a
process that already exited without something watching it while it ran.
Wrap the launch before starting it, every time, the same discipline
"write each step's state to a file" already asks for above.

## Router naming convention (2026-09-09 addendum)

Every single-letter prefix a-z is now used (`gr/aroute.cpp` through
`gr/zroute.cpp`). New routers use **two letters plus a generation
number**: the letters identify a *species* -- a core algorithmic
approach -- and the number increments for direct descendants that refine
that species (a new coefficient, a new formula term, a relaxation
mechanism) without changing its core algorithm. Only change the letters
when a new file's core solve method is genuinely different, not merely a
new tunable on an existing one -- e.g. the MSMV maze-growth lineage
(`vroute`/`zroute`) and the joint-candidate-assignment/branch-and-bound
lineage (`broute` through `qroute`) would each be their own species under
this scheme, had it existed when they were built; existing single-letter
files are not renamed retroactively.

First use: `gr/kr1route.cpp`, species `kr` (rooted at `kroute` -- the
ancestor that defined "negotiated congestion via a 2-tier fast/slow
pricing loop with LIFO-relaxable multiplicative terms" as this species'
defining trait, as opposed to MSMV search or joint B&B assignment),
generation 1. A further refinement of the same mechanism becomes
`kr2route.cpp`, and so on. Env var prefixes follow the same file-stem-in-
caps convention every prior router already uses (`KR1ROUTE_...`).
