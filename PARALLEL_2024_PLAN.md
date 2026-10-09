# Step 3 for ISPD 2024: threads, then the GPU

2026-10-08, after HISTORY_COST_SWEEP.md Part 115. Starting point: the 2024
standard `./pd36route @standard.env @standard24.env @fast24.env`,
single-threaded, on an M4 Pro (10 performance + 4 efficiency cores).

## Where the time is (stage reports)

| stage | mempool_cluster_ranking (1599 s) | mempool_cluster (1861 s) | bsg_chip_rank (74 s) |
|---|---|---|---|
| layer assignment, round 0 | 710 s (45%) | 640 s (35%) | 31 s (42%) |
| 3D pattern passes (3) | 395 s (25%) | 360 s (19%) | 20 s (27%) |
| pattern stage | 299 s (19%) | 369 s (20%) | 12.5 s (17%) |
| escalation + slow tier | 70 s (4%) | 374 s (20%) | 0.5 s |
| read + write | 117 s (7%) | 111 s (6%) | 8 s (11%) |

Contest #1 on 8 cores + 4 A100s: 217 s (cluster rank), 5-46 s on the rest.

## The key property: net-parallel work can stay byte-identical

Layer assignment's first round and the 3D passes process nets one at a
time, each reading and writing usage only inside its own tree's tiles plus
one GCell around them (via demand reaches a neighbour). Two nets whose
expanded boxes do not overlap commute: either order gives the same usage
and the same routes. So:

- **Scheduler.** Walk the nets in their existing order; put each in the
  first batch after the last batch holding an earlier net whose expanded
  box overlaps it. Overlapping nets keep their relative order, and
  non-overlapping ones commute, so committing batch by batch is exactly
  the sequential result. Nets in one batch run on T threads.
- **Per-thread state.** Scratch only (pattern3DNet's is already
  `thread_local`; layerAssignDP's buffers become per-thread). No usage
  replicas: within a batch, no two nets touch the same GCell. That also
  keeps the 28 GB footprint flat. (The 2008 pd24-pd27 scheme replicated
  usage per worker, about 0.9 GB per copy at cluster size, and was not
  byte-identical.)
- **Giant nets** (boxes covering much of the die) would serialize the
  batches. They go in batches of their own.
- **Test that would fail if wrong.** 1 thread vs T threads must be
  byte-identical on fixtures and on real designs, every T. The 2D-ordered
  scheduler gets a self-test with deliberately overlapping nets.

## Order of work

1. **Batch scheduler + threaded layer assignment round 0** (largest
   stage). Measure per-batch parallel efficiency in the stage report
   (batches, mean batch size, idle time).
2. **Threaded 3D pattern passes** on the same scheduler. Each net's
   acceptance check reads only its own touched edges, so it fits too.
3. **Pattern stage:** its batches already have disjoint coarse boxes and
   their solve reads only the frozen cost grid, so the solve is
   parallel as is (threads first; then the Metal backend, which is
   already bit-identical to the CPU reference -- `GPU_BACKEND_PLAN.md`).
   The per-batch commit and incremental refresh stay sequential.
4. **I/O:** read and write are 7% at cluster size; the writer can format
   nets in parallel chunks.
5. **Slow tier** (20% on the public mempool_cluster only): the pd24-pd27
   batched search, re-measured; not byte-identical, so judged on score.
6. **GPU beyond the pattern stage** (layer assignment's DP on Metal) only
   if the threaded profile says it is still the bottleneck.

Rough expectation, not measured: 6-8x on the three net-parallel stages
with 8-10 threads, so mempool_cluster_ranking from 1599 s to about
300-400 s. Memory bandwidth, not cores, is the likely ceiling (CLAUDE.md,
Parallelism plan).

## Options outside this plan

- The third 3D pass costs 6-8% of a run for 0.03-0.04% of score. Dropping
  it is a quality trade, the user's call, not an exact speedup.
- Peak memory (28 GB on a 24 GB machine) is still layer assignment's
  first round; independent of threading.

## Status (2026-10-08, HISTORY_COST_SWEEP.md Part 116)

Done in pd37route, byte-identical at every thread count: steps 1-3 and the
writer half of step 4 (as a dependency graph, not batches). mempool_cluster
_ranking 1599 s -> 623 s on 8 threads. Next: input parsing, the slow tier
(step 5), the Metal backend for the pattern solve, memory.
