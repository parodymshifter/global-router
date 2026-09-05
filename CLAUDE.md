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
serialization point.

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

**Cross-check against the official Perl evaluator** (`eval2008.zip`). Total
overflow, max overflow, and wirelength must agree. If they diverge, assume our
code is wrong first: theirs scored an actual contest. Note that 2008-era Perl
may fail on modern interpreters, usually `defined(@array)`, removed in 5.22.

**Provenance tracking.** Record, for every committed edge, which net asked for
it, which constraint permitted it, and which input record supplied that
constraint. Then a verifier hit resolves in one step instead of a debugging
session: the failure either traces to a router decision (router bug) or to an
input record (data bug, display it). Costs 20 to 40 bytes per committed edge.
Usual compromise: run without it, re-run with it enabled on the failing region
only. Add this at the verification stage; it is painful to retrofit.

Limit worth knowing: provenance identifies which input record was involved. It
cannot tell you whether that record reflects what the designer meant.

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

- C++17, no dependencies beyond the standard library at the standalone stage.
- Build with `-Wall -Wextra`, keep it warning-clean.
- Tile coordinates zero based internally; `.gr` layers are one based, convert
  at the reader boundary.
- Prefer dense arrays over hash maps for per-edge data. The access pattern
  determines the parallel ceiling later.
