# Multithreading, adaptive virtual capacity, and a long-net capacity limit

2026-10-05. Roadmap step 4 (CPU multithreading), plus the two overflow
mechanisms scoped to ride on it. Judged by the project's working bar
(user, 2026-10-05): a change counts when it moves something by ~25% or
more -- runtime multiples, or overflow on the residues with real headroom
(bigblue4 356 vs 166 published, newblue7 108 vs 68 / floor 54, newblue1 38
vs 0). Few-percent effects are reported as small.

## Where the time goes (standard configuration, M4 Pro, one thread)

| benchmark | total | region stage | refinement | rest (mostly the slow tier) |
|---|---|---|---|---|
| bigblue4 | 10.7 h | 5.7 h (53%) | 0.4 h | ~4.6 h |
| newblue7 | 19.6 h | 10.7 h (54%) | 0.1 h | ~8.8 h |
| newblue4 | 2.7 h | 1.8 h (67%) | <0.1 h | ~0.8 h |
| newblue1 | 1.6 h | 1.2 h (75%) | <0.1 h | ~0.4 h |
| newblue3 | 8.2 h | 0.6 h | <0.1 h | ~7.6 h |

Both expensive stages are the same loop: for each net that is still
overflowed, rip up its overflowed pieces and search new ones (MSMV maze
search, present-demand prices read from the live usage), one net after
another. Searches are almost all of the time. Parallelizing those two
loops covers 85-95% of every long run.

## Part 1: batched parallel rerouting (pd24route)

**Model.** Per round, the nets the loop would visit are cut into batches
of B consecutive nets (B fixed, e.g. 64, independent of the thread count).
For each batch:

1. *Search in parallel against the batch-start state S.* Each worker
   thread owns a replica of the 2D usage and a copy of the prices pointing
   at it (both equal to S). A worker runs the existing per-net code
   unchanged on a copy of the net -- the overflowed check, box margin,
   `rerouteNetPieces` / `routeNetSlowMSMV` -- then restores its replica
   to S by undoing the net's new charges and re-adding its old ones.
   Nothing shared is written during this phase.
2. *Commit in net order on the main thread.* For each net: remove its old
   charges from the live usage, check the new route against the live
   state, add it. Conflict rule (default): if an edge on the new route was
   changed by an earlier commit in this batch and would now overflow, the
   net is instead rerouted sequentially on the live state (exactly the
   one-thread semantics for that net). Alternative rule for measurement:
   accept every route (pure Jacobi within the batch).
3. *Sync the replicas* by applying the batch's committed changes (old
   charges out, new in) to each one -- small next to the searches.

Prices change only at round boundaries (history, iteration factor), so
worker price copies are refreshed once per round.

**Determinism.** Each net's search depends only on S and on the net; the
commit order and the conflict rule are fixed. So the routes depend on B
but not on the thread count or on scheduling: a run with 1 worker and one
with 10 must be byte-identical. That is the decisive self-test, plus a
benchmark check (newblue2, adaptec1 at T = 1 vs T = 8). Counters that are
global today (search pops for the work-gated stop, piece counts) become
per-thread and are summed per task in commit order. Threads = 0 keeps
pd23route's sequential loop byte for byte.

**What changes in quality.** Within a batch, nets do not see each other's
moves -- up to B-1 nets of staleness, the "stampede" CLAUDE.md warns about.
The conflict rule bounds the damage (a route that the batch made
overflowing is redone live). Results will differ from pd23route by
reordering, so the bar is: overflow within each benchmark's noise or
better, wirelength within its spread, on the 16 benchmarks.

**Expected speedup.** The M4 Pro has 10 performance cores. The design
notes expect 30-50% parallel efficiency (memory bandwidth); searches are
cache-local within a box, so maybe better. Target: **3-5x on the region
stage and the slow tier**, i.e. bigblue4 10.7 h -> ~3 h, newblue7 19.6 h ->
~5 h. Measured, not assumed: T = 1 / 4 / 8 on newblue1 and bigblue2 first.

**Order of work.** (a) slow tier and region stage (90% of the time);
(b) refinement (small but serial); (c) tree rebuild and WL recovery if
they show up in the profile afterwards. The fallback already runs its two
paths on two threads; with batching, the path threads share the cores
(threads split between them).

**Risks.** Replica memory: one 2D usage + price copy per worker (tens of
MB at most on newblue7). A search that grows past its box (the no-path
doubling) is fine -- replicas are full-grid. Any hidden shared write in the
per-net code is a race: the T = 1 vs T = N byte-identity test is what
catches it.

## Part 2: adaptive virtual capacity (pd25route, on pd24route)

**Why.** Static virtual capacity (Part 89) cut newblue1's residue a third
(48 -> 24-38), the one big swing of the last week. Its zone is fixed at the
region stage's start and its reduction is fixed (8 tracks). D3 (Part 91)
says it works where a hotspot is saturated by through-traffic (newblue1;
newblue7's 12 small hotspots, 36 of its 108) and cannot work where the
residue is pin-bound (newblue4, newblue7's main hotspot, newblue3).

**Mechanism** (closer to FastRoute 3.0's): a per-edge reduction r_e,
updated at every region-round boundary -- an edge overflowed in the
settled state gains a step (up to a cap); an edge with free room loses a
step (down to 0). The zone then follows the residue as it moves, and the
dose grows where overflow persists instead of being guessed. Optionally
skipped where the cut test says the residue is pin-bound (tightest cut
over ~95%), since pushing traffic out cannot help there.

**Fit with Part 1.** r_e changes only at round boundaries, like history:
read-only during a round, copied to workers with the prices. No new
synchronization.

**Test.** newblue1 cells 7 / 8 / 9 against static 8 tracks (24 / 36 / 38)
-- the bar is a large further drop (toward ~10, where the reclaim also
starts to run). newblue7 once (its through-traffic share is 36 of 108):
with Part 1 that run is ~5 h instead of ~20.

## Part 3: a separate capacity limit for long nets (pd26route)

**Why.** CLAUDE.md's "nested capacity constraints": every edge keeps its
real capacity for all nets, and long nets additionally get only a share
alpha * cap. Long nets then pay a second price and leave a contested edge
before local nets feel anything -- through-traffic is mostly long nets, so
this is the structural version of what virtual capacity approximates. Not
in any contest router we know of; the best candidate for the project's
novel contribution.

**Mechanism.** A net is long if its pin bounding box half-perimeter
exceeds L tiles. Usage gains a second per-edge count, long-net demand
(maintained by the same charge/undo code when the net is long). The
present-demand cost for a long net adds hist_L * (longDemand + amount) /
(alpha * cap))^5, with its own history term hist_L raised at round
boundaries where long demand exceeds alpha * cap. Short nets' cost is
unchanged. Overflow and scoring stay on the real capacity.

**Choosing alpha and L.** Per tile from a pre-routing estimate, as the
design note says, is the full form; first form: one alpha (e.g. 0.6) and
L at a percentile of net span (e.g. the longest 10% of nets), screened on
newblue1, then bigblue4 and newblue7 (bigblue4 has the largest gap to the
published best, 356 vs 166, and is between the two residue types).

**Fit with Part 1.** The long-demand count lives in Usage, so it is part
of every replica and every commit; its history updates at round
boundaries. The conflict rule extends to the long budget.

## Order and checkpoints

1. pd24route: batched slow tier + region stage; self-test byte identity
   T = 1 vs T = N; newblue2 / adaptec1 parity (T = 1 vs T = 8);
   speedup at T = 1 / 4 / 8 on newblue1 and bigblue2. **Checkpoint**: is it
   3x or better, and is quality within noise?
2. The full suite on the batched standard (overnight at the new speed),
   which becomes the baseline for Parts 2 and 3.
3. Parts 2 and 3 in that order, each screened on newblue1 (minutes, at the
   new speed), then the residue it targets.
