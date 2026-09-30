# ISPD 2024 format, scoring, and the plan for roadmap step 3

Source of truth: the contest evaluator (`evaluator.cpp`, as distributed in
`cuhk-eda/InstantGR` under `run/`), read line by line on 2026-09-29. The
contest page (liangrj2014.github.io/ISPD24_contest) points to a Google
Drive introduction document for the spec; the evaluator is what scored the
contest, so where the two could differ, the evaluator wins -- the same rule
CLAUDE.md applies to `eval2008.pl`.

## Files

**`.cap` (routing resources)**

    nLayers xSize ySize
    unitLengthCost unitViaCost overflowCost[0] ... overflowCost[nLayers-1]
    xPitch[0] ... xPitch[xSize-2]        GCell-to-GCell distances, DBU
    yPitch[0] ... yPitch[ySize-2]
    then per layer z = 0 .. nLayers-1:
      name direction minLength           direction 0 = horizontal, 1 = vertical
      ySize rows of xSize capacities     tracks, may be fractional

Layer 0 (metal 1) is not routable: its capacities are read and ignored, and
wires on it are illegal. Pitches are **not uniform**, unlike ISPD 2008 --
wirelength is the sum of the pitches crossed. The capacity of GCell
(x, y) on a horizontal layer is the capacity of the edge from x to x + 1
(on a vertical layer, y to y + 1); the last column (row) is never used by
a wire.

**`.net` (netlist)**: a net-name line (no parentheses, longer than one
character), `(`, one line per pin `[(z, x, y), (z, x, y), ...]` listing
the pin's access points (layer first), `)`. A pin is connected when any
one of its access points is reached.

**Solution (GCell coordinates)**: a net-name line, `(`, segment lines
`xl yl zl xh yh zh`, `)`. A wire lies on one layer along its direction
with `xl < xh` (horizontal) or `yl < yh` (vertical); a via has equal x and
y and `zl < zh`, and crosses every layer from zl to zh. (ISPD 2025 moved
to layout coordinates and metal names; this is the 2024 form.)

## Scoring

Demand is in half tracks. A wire adds 2 to each GCell `[lo, hi)` it runs
along. A via between layers z and z + 1 at (x, y) adds demand on layer z
**unless the net already covers (z, x, y)** with a wire end or another via:
1 to each of the two layer-z GCells beside it along that layer's direction,
or 2 to the one inside at the grid edge. Per routing-layer GCell with
capacity c and demand d:

    c > 0.001:          exp(0.5 * (d/2 - c))       every GCell, used or not
    c <= 0.001, d > 0:  exp(0.75 * d)

    total = unitLengthCost * sum(pitches crossed)
          + unitViaCost * (layer crossings of all vias)
          + sum_z overflowCost[z] * sum over layer z's GCells of the above

The overflow term is soft and exponential: nothing is forbidden, an idle
GCell still costs `exp(-c/2)`, and one track over capacity costs `e^0.5`
times one exactly at capacity. That is a different objective from ISPD
2008's lexicographic "total overflow first": here a router trades
overflow against wirelength and vias continuously, and the zero-overflow
mandate becomes "minimize the weighted total", with overflow weights set
per benchmark. The evaluator also exits on the first unconnected net and
asserts on malformed segments; `gr/g24_verify.cpp` reports every problem
instead.

## Status and plan

1. **Verifier/scorer -- done**: `gr/g24_verify.cpp` (hand-computed
   fixture, via-demand edge rules, zero-capacity rules, ten kinds of
   broken solution). Next: cross-check against the official evaluator on
   a real benchmark and solution, to the printed digit.
2. **Reader into the router's `Design`/`RoutingGrid`** (the porting seam:
   nothing below the grid learns the format). What the 2008 path lacks:
   non-uniform pitches (the wirelength term becomes per-edge length),
   fractional per-layer capacities, pins as sets of access points (MSMV's
   multi-sink search already takes sets), access points on the unroutable
   layer 0 (a via stack up from each chosen access point), via demand
   charged to wire capacity (layer assignment's objective gains a via
   term), and the soft exponential objective.
3. **Writer** for the GCell-coordinate solution, then end-to-end runs
   scored by both `g24_verify` and the official evaluator.
4. Benchmarks: the contest's set (Nangate45; ariane133, bsg_chip,
   mempool_tile, nvdla, mempool_group, mempool_cluster, mempool_cluster_large
   and variants, up to tens of millions of GCells) is on Google Drive;
   `cuhk-eda/InstantGR` carries one small case (`mempool_tile_rank`: .cap
   5.6 MB, .net 20 MB) that is enough for step 1's cross-check.
   Downloads need the user's approval.
