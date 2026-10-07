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

1. **Verifier/scorer -- done and cross-checked**: `gr/g24_verify.cpp`
   (hand-computed fixture, via-demand edge rules, zero-capacity rules, ten
   kinds of broken solution). On `mempool_tile_rank` (135666 nets, 429 x
   581 x 10) with solutions from the throwaway `gr/g24_lroute.cpp`, it
   agrees with the official evaluator to every printed digit: lowest layer
   pair -- WL cost 7828346.6313, via cost 4359252.0000, overflow cost
   9327069661506788392960.0000; nets spread over all layer pairs
   (`G24_LROUTE_SPREAD=1`) -- 7828346.6313 / 14457436.0000 /
   214189305.5220 / total 236475088.1534, 0 open nets both. Learned on the
   way: the evaluator reaches only access points the net itself covers,
   so a net whose pins share one access point still needs a via there
   (7575 such nets in this design; with no segment written they are open).
   An empty solution already costs 2538617.8 in overflow (idle GCells).
2. **Reader into the router's `Design`/`RoutingGrid`** (the porting seam:
   nothing below the grid learns the format). **Started
   (HISTORY_COST_SWEEP.md Part 103):** `gr/g24_io.h` (readers, `toDesign`,
   solution writer) and `gr/pd33route.cpp`, which reads .cap/.net
   directly. The defaults reproduce the bridge exactly. Done so far: via
   demand charged in layer assignment, and layer assignment priced in the
   score's own terms (`gr/standard24.env`). Still to do: access-point
   choice, non-uniform pitches in the wirelength term, and score-aware 2D
   pricing. What the 2008 path lacks:
   non-uniform pitches (the wirelength term becomes per-edge length),
   fractional per-layer capacities, pins as sets of access points (MSMV's
   multi-sink search already takes sets), access points on the unroutable
   layer 0 (a via stack up from each chosen access point), via demand
   charged to wire capacity (layer assignment's objective gains a via
   term), and the soft exponential objective.
3. **Writer** for the GCell-coordinate solution, then end-to-end runs
   scored by both `g24_verify` and the official evaluator. **First
   end-to-end result (HISTORY_COST_SWEEP.md Part 102)**, ahead of step 2,
   through a format bridge (`gr/g24_bridge.cpp`: .cap/.net -> .gr ->
   unchanged pd23route -> GCell solution; `gr/g24_score.sh` scores it and
   cross-checks against the official evaluator). On mempool_tile_rank: legal,
   total 14272259 (WL 7700947, vias 3414924, overflow 3156388 of which
   2538618 is the idle-GCell floor), 44 s. The bridge cannot carry via
   demand on wire capacity, access-point choice, non-uniform pitches, or
   per-layer overflow weights. Reserving M2 capacity for pin via stacks
   made it 17-86% worse, so the via term must enter layer assignment
   (step 2).
4. Benchmarks: the contest's set (Nangate45; ariane133, bsg_chip,
   mempool_tile, nvdla, mempool_group, mempool_cluster, mempool_cluster_large
   and variants, up to tens of millions of GCells) is on Google Drive;
   `cuhk-eda/InstantGR` carries one small case (`mempool_tile_rank`: .cap
   5.6 MB, .net 20 MB) that is enough for step 1's cross-check.
   Downloads need the user's approval.

## Published reference scores

From the contest page (liangrj2014.github.io/ISPD24_contest, read
2026-10-06). Raw score = WL cost + via cost + overflow cost. The contest
ranked a runtime-scaled score; our comparisons use the raw score. The
contest ran on 8 cores + 4 A100s, with runtime limits of 30 s to 50000 s
per design.

Hidden ("rank") benchmarks, final submissions, raw score:

| file | contest name | #1 | #2 | #3 |
|---|---|---|---|---|
| ariane_68_rank | Ariane_rank | 22602876 | 23093501 | 22821289 |
| bsg_chip_rank | BlackParrot_rank | 113109620 | 110986781 | 111778586 |
| nvdla_rank | Nvdla_rank | 43195979 | 44141552 | 43295092 |
| mempool_tile_rank | MemPool-Tile_rank | 13827142 | 14133269 | 13867124 |
| mempool_group_rank | MemPool-Group_rank | 383637652 | 395488860 | 388200338 |
| mempool_cluster_ranking | MemPool-Cluster_rank | 1782191834 | 1824527054 | 1795524941 |
| tera_cluster_rank | Tera-Cluster_rank | 12521927639 | 12676067016 | 12597498026 |

Public benchmarks, best beta submission (Feb 15, 2024), raw score:
ariane133_51 22765372, ariane133_68 20251238, bsg_chip 113537664, nvdla
49909986, mempool_tile 15402753, mempool_group 409309772, mempool_cluster
1663344708, tera_cluster 20438460750.

Downloaded (2026-10-06, user-approved) to `gr/bench/ispd24/` by
`download.sh` / `download.list`: every Simple_inputs design except
tera_cluster (two 10.7 GB .net files, ~50M cells). The InstantGR copy of
mempool_tile_rank is byte-identical to the contest's.
