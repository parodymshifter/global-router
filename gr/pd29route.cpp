// pd29route.cpp
//
// Forked from pd28route.cpp (committed). Species `pd`, generation 29: a
// flat penalty for crossing an edge that is already full, in the region
// stage (HISTORY_COST_SWEEP.md Part 97).
//
// cutdetour (Part 97) measured, for every overflowed edge, how far along
// its own crossing line the room is: the nearest edge with space for one
// more net is a median 5-7 tiles away, and a window of 6-11 tiles (90th
// percentile 11-29, max 11-43) on either side holds enough spare for all
// the overflow in it, on every residue benchmark but newblue3. So a detour
// of some 20-80 tiles would clear each overflowed edge. The present-demand
// cost does not pay for it: an edge 1-2 units over a 60-200 capacity costs
// hist * (1.03)^5 ~ 1.2 hist, while each tile of a detour through nearly
// full edges costs ~1 once the WL weight has decayed -- so staying on the
// overflowed edge stays cheaper until its history has grown for many
// rounds.
//   PD29ROUTE_OVER_PENALTY=P (default 0 = pd28route byte for byte): in the
//     region stage, an edge that one more net of this amount would push
//     over its real capacity costs P more. The reclaim path's region stage
//     runs without it (as with virtual capacity, PD23ROUTE_VCAP_B=0's
//     rule). Additive and non-negative, so A* stays admissible.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 97.
//
// --- pd28route.cpp's own header, inherited unchanged: ---
//
// pd28route.cpp
//
// Forked from pd27route.cpp (committed). Species `pd`, generation 28: the
// "ring pull" -- move load from the warm band around a hotspot out to cool
// tiles first, so the region stage can then move the hotspot's load into
// the band it freed (HISTORY_COST_SWEEP.md Part 95; the user's proposal
// after Part 94).
//
// Part 94 measured the warm band around each overflowed center: to reach
// tiles at <= 75% on 90% of each side, the rectangle must span 58-70% of
// the die on newblue1 and bigblue4 (bands 50-190 tiles thick), while the
// region stage reroutes only nets touching the overflow's box plus 5
// tiles. Its nets look for room the band does not have.
//   PD28ROUTE_RING=s (share, default 0 = off = pd27route byte for byte):
//     at the region stage's start, grow a rectangle from the overflow's
//     bounding box, each side moving outward until the tiles along it are
//     cool (worst 2D edge use/capacity <= PD28ROUTE_RING_COOL, default
//     0.75) for at least a share s of them, or it reaches the die edge.
//     Then, before the region stage's rounds, PD28ROUTE_RING_PASSES (1)
//     passes reroute every net's pieces with wire in that rectangle under
//     present-demand prices where the warm edges inside it (use/capacity >
//     cool) are priced PD28ROUTE_RING_PULL tracks lower (default 4) and the
//     core (the overflow's box plus the region expand) 2x that, each piece
//     kept only if it adds no local overflow (revert) -- load moves from
//     warm edges to cool ones, none moves into the core. Margin
//     PD28ROUTE_RING_MARGIN (16).
//   PD28ROUTE_RING_BOX=1: the region stage then also uses the ring
//     rectangle as its box (members: every net with wire in it), instead of
//     the overflow's box plus 5.
// The report gives the ring's size, the pieces moved, and the mean use /
// capacity of the band's warm edges (those above `cool` at the start) and
// of the core's edges, before and after the pull.
// With PD24ROUTE_THREADS > 0 the pull runs through the batched executor.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 95.
//
// --- pd27route.cpp's own header, inherited unchanged: ---
//
// pd27route.cpp
//
// Forked from pd26route.cpp (committed). Species `pd`, generation 27: less
// wasted work and fewer idle workers in the batched executor.
//
// pd26route's stage timers (newblue1, 10 threads, HISTORY_COST_SWEEP.md
// Part 92c) put the cleanup stages at 24 s together; what holds the speedup
// to ~2.4x is the executor itself: the conflict rule re-searched 277394
// region and 343359 slow-tier nets (about 2x the one-thread CPU work), and
// each conflict pass re-searches only a few nets while the other workers
// wait at the pass barrier.
//   PD27ROUTE_CONFLICT=fresh (default here; any = pd26route): a net is
//     re-searched only if its new route crosses an edge that had room for
//     it when it was searched and that an earlier commit in the pass has
//     since filled. A net that knowingly chose an already-full edge (the
//     present-demand price allows it) is committed as searched.
//   PD27ROUTE_CARRY=1 (default here; 0 = pd26route's passes): conflicting
//     nets are not re-searched in a pass of their own; they join the front
//     of the next batch, so every pass keeps the workers busy. After the
//     last batch, passes continue until none are left.
// Routes still do not depend on the thread count (both rules depend only
// on the pass-start state and the commit order).
// Also: route() reports its own split (pattern stage and fast tier vs the
// slow tier) in the stage-timer line.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 92.
//
// --- pd26route.cpp's own header, inherited unchanged: ---
//
// pd26route.cpp
//
// Forked from pd25route.cpp (committed). Species `pd`, generation 26: the
// tree rebuild and the WL recovery batched too, and stage timers.
//
// With the slow tier and the region stage batched (pd25route: newblue1
// 2.44x at 8 threads, region stage 3.1x), the stages outside those loops
// are the serial remainder (newblue1: ~910 s of 2303 s). Two of them are
// the same per-net shape:
//   - tree rebuild (rebuildNet: regrow a net from its pins over edges with
//     room, keep it only if shorter),
//   - WL recovery (wlRecoverNet: re-search each piece over edges with room).
// With PD24ROUTE_THREADS > 0 both run through the same batched executor
// (worker replicas, commit in net order, conflicting nets re-searched in
// passes). Both only ever use edges with room in the state they search
// against, and the conflict rule re-searches any net whose route crosses an
// edge an earlier commit in the batch filled -- so neither can raise
// overflow (each stage still fails loudly if it does). Routes still do not
// depend on the thread count.
// Stage timers (always printed): seconds in route() (pattern stage, fast
// and slow tiers), the region stage and refinement (or the fallback), the
// tree rebuild, the WL recovery, and layer assignment.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 92.
//
// --- pd25route.cpp's own header, inherited unchanged: ---
//
// pd25route.cpp
//
// Forked from pd24route.cpp (committed). Species `pd`, generation 25:
// conflicting nets are re-searched in parallel passes instead of one by one.
//
// pd24route's batched rerouting gave newblue1 2.0x at 8 threads (region
// stage 2.7x) at unchanged quality, but more than half of the changed
// routes hit the conflict rule (slow tier 165417 redone vs 103230
// committed; region 150511 vs 115638), and pd24route redoes each of them
// sequentially on the main thread (HISTORY_COST_SWEEP.md Part 92).
//   PD25ROUTE_REDO_PASSES=1 (default here; 0 = pd24route's serial redo):
//     a batch's conflicting nets are not redone at once; after the pass,
//     the replicas are synced with the pass's commits and the conflicting
//     nets form the next pass -- searched in parallel against the updated
//     state, committed in order with the same rule -- until none are left.
//     The first net of every pass always commits (only the pass's own
//     commits mark edges dirty), so the passes end. Routes still do not
//     depend on the thread count.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 92.
//
// --- pd24route.cpp's own header, inherited unchanged: ---
//
// pd24route.cpp
//
// Forked from pd23route.cpp (committed). Species `pd`, generation 24:
// batched parallel rerouting in the slow tier and the region stage
// (MULTITHREADING_PLAN.md, part 1; roadmap step 4).
//
// Both stages are one loop: for each net still overflowed, rip up its
// overflowed pieces and search new ones against the live usage, one net
// after another. They are 85-95% of every long run (bigblue4 10.7 h,
// newblue7 19.6 h on one thread).
//   PD24ROUTE_THREADS=T (default 0 = pd23route's sequential loops, byte for
//     byte): the nets a round would visit are cut into batches of B. Each
//     batch is searched in parallel by T workers against the batch-start
//     state -- every worker owns a replica of the 2D usage and a copy of
//     the prices pointing at it, runs the unchanged per-net code on a copy
//     of the net, then restores its replica. The main thread then commits
//     the batch in net order; a net whose new route crosses an edge that an
//     earlier commit in the batch changed and that would now overflow is
//     rerouted again on the live state (PD24ROUTE_REDO=1, default), or kept
//     anyway (0). Replicas are refreshed from the live state every round
//     and synced with each batch's commits.
//   PD24ROUTE_BATCH=B (default 64): batch size. Routes depend on B and on
//     the conflict rule, never on T or on scheduling -- T = 1 and T = 10 are
//     byte-identical (the self-test checks it).
// A round's candidate nets are fixed at the round's start (sequentially, a
// net pushed into overflow by an earlier net in the same round was visited
// in that round; batched, it waits for the next). Search-pop and piece
// counters are per thread and summed in commit order, so the work-gated
// stop sees the same deterministic count at any T. Requires present-demand
// pricing (PD1ROUTE_COST=pd, the default): lambda prices are written during
// commits and are not replicated.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 92.
//
// --- pd23route.cpp's own header, inherited unchanged: ---
//
// pd23route.cpp
//
// Forked from pd22route.cpp (committed). Species `pd`, generation 23:
// virtual capacity on the fallback's standard path only.
//
// pd22route's virtual capacity (PD22ROUTE_VCAP) applies to every region
// stage, including the reclaim path B's. There the zone is built from the
// overflow the reclaim injects on purpose -- spread over the whole chip,
// not a hotspot (bigblue1: 46061 zone edges on B vs 13249 on A) -- and it
// made B's renegotiation slower and longer: bigblue1's B region stage took
// 61 rounds instead of 31 and ended with 4.2% more 2D wire, so the
// fallback kept A and the final WL rose 1.7% (HISTORY_COST_SWEEP.md Part
// 90). The mechanism is meant for the residue the standard path cannot
// clear.
//   PD23ROUTE_VCAP_B=0: the reclaim path's region stage runs without
//     virtual capacity (path A keeps PD22ROUTE_VCAP). Default 1: pd22route
//     byte for byte.
// With it, B is exactly pd21route's B, so wherever B wins (every
// zero-overflow benchmark measured so far) the kept routes are the
// current standard's.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 90.
//
// --- pd22route.cpp's own header, inherited unchanged: ---
//
// pd22route.cpp
//
// Forked from pd21route.cpp (committed). Species `pd`, generation 22: two
// independent mechanisms, both off by default (pd21route byte for byte).
//
// 1. Virtual capacity in the region stage (FastRoute 3.0's idea, in a
//    static form). newblue1's last 48 overflow sits in one hotspot the
//    region stage cannot clear: 500 rounds oscillate between ~76 and ~124
//    around a best of 64 (HISTORY_COST_SWEEP.md Part 89). NTHU's history
//    term only targets edges that are overflowed; an edge exactly at
//    capacity stays at cong^5 <= 1, so the hotspot's full-but-legal edges
//    keep attracting nets and an overflowed net finds no free path through
//    them. Pricing the hotspot's edges against a lowered capacity makes
//    full edges there look overflowed, so nets with room elsewhere leave
//    and free tracks -- negotiation over-corrects on purpose.
//      PD22ROUTE_VCAP=k (tracks, default 0 = off): at the region stage's
//        start, every 2D edge with an end tile within R tiles (x and y) of
//        an overflowed edge is priced against capacity
//        max(cap - k * (1 + minSpacing), ceil(cap / 2)). Only the
//        present-demand cost reads it: overflow, the best-state rule, the
//        stops, refinement and the fallback's choice all use the real
//        capacity.
//      PD22ROUTE_VCAP_RADIUS=R (default 5, the region stage's own expand).
//    FastRoute 3.0 adapts its virtual capacities every iteration; this is
//    the simplest form (one zone, fixed at the stage start, a fixed
//    reduction), to learn whether the idea moves the residue at all.
//
// 2. The fallback's two paths on two threads. With PD21ROUTE_FALLBACK_A_MAX
//    the reclaim path B runs after the standard path A ends at or below
//    the threshold: on zero-overflow benchmarks that adds B's region stage
//    and refinement to the wall time (+7% newblue2, +10% bigblue2, Part
//    88). Both paths start from the same saved state and share nothing.
//      PD22ROUTE_FALLBACK_PARALLEL=1 (needs PD21ROUTE_FALLBACK_A_MAX >= 0):
//        start B on a second thread when A starts; if A ends above the
//        threshold, cancel B at its next region round and discard it.
//    Kept routes are identical to the sequential order's: the search-pop
//    counter the work-gated stop reads is per thread (thread_local), so
//    each path's stop sees exactly its own work. B's per-round trace lines
//    are suppressed in this mode (two threads would interleave them); its
//    summary is still printed.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 89.
//
// --- pd21route.cpp's own header, inherited unchanged: ---
//
// pd21route.cpp
//
// Forked from pd20route.cpp (committed). Species `pd`, generation 21: the
// reclaim fallback ordered so residue benchmarks pay nothing for it.
//
// The fallback (pd19route, B-first in pd20route) shortened every
// zero-overflow benchmark (all 11; -0.16% to -4.1%) and never cost
// overflow, but on newblue1 -- whose residue the reclaim path never
// matched -- it paid a second region stage for nothing (+85% runtime);
// the B-first order pays that on every benchmark that does not reach zero
// (HISTORY_COST_SWEEP.md Parts 86-87).
//   PD21ROUTE_FALLBACK_A_MAX=T (with PD19ROUTE_RECLAIM_FALLBACK=1): run the
//     standard path A first; run the reclaim path B only if A ends with 2D
//     overflow <= T, then choose exactly as pd19route does. Above T, A is
//     kept and B never runs. Default -1: pd20route's behavior (its
//     PD20ROUTE_FALLBACK_ORDER applies), byte for byte.
// Where B runs, its result is the same as in either earlier order (it
// starts from the same saved state), so the kept routes match pd19route's
// wherever the choice is the same.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 88.
//
// --- pd20route.cpp's own header, inherited unchanged: ---
//
// pd20route.cpp
//
// Forked from pd19route.cpp (committed). Species `pd`, generation 20: the
// reclaim fallback in the cheaper order.
//
// pd19route runs both paths from the slow tier's state -- A, the region
// stage and refinement as standard; B, the reclaim pass and then the same
// -- and keeps B only if no worse on overflow and shorter. On newblue1
// (B's overflow higher every time) that second region stage cost +85%
// runtime for nothing; on bigblue2 and newblue2 B reached zero and won
// (HISTORY_COST_SWEEP.md Part 86).
//   PD20ROUTE_FALLBACK_ORDER=bfirst: run B first; if B ends with 2D
//     overflow 0, keep it without running A (A cannot have lower overflow);
//     otherwise run A and choose exactly as pd19route does. The kept result
//     differs from pd19route's only when A and B both reach zero and A is
//     shorter -- in every pd19route run where both reached zero, B was
//     shorter (by 0.4-16%).
//   PD20ROUTE_FALLBACK_ORDER=both (default): pd19route's order.
// Used with PD19ROUTE_RECLAIM_FALLBACK=1. Default: pd19route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 87.
//
// --- pd19route.cpp's own header, inherited unchanged: ---
//
// pd19route.cpp
//
// Forked from pd18route.cpp (committed). Species `pd`, generation 19: the
// detour reclaim pass with a fallback, so it can never cost overflow.
//
// The reclaim (pd16route, mu 8) cut wirelength significantly wherever it
// ran -- bigblue2 -4.1% with its zero kept 3/3, newblue2 -0.24% -- but on
// newblue1, whose residue never clears, every variant tried (before the
// region stage, avoiding the overflow, inside the slow tier) ended a few
// units of overflow higher (HISTORY_COST_SWEEP.md Parts 83-85). Choosing
// which nets or when did not fix that; comparing does.
//   PD19ROUTE_RECLAIM_FALLBACK=1: save the 2D state the slow tier hands
//     on. Path A: the region stage and refinement on it, as standard.
//     Path B: the reclaim pass (PD16ROUTE_RECLAIM_* / PD17ROUTE_RECLAIM_AVOID
//     settings), then the region stage and refinement, on a copy of the
//     same saved state. B is kept only if its 2D overflow is lower than
//     A's, or equal with less 2D wire; otherwise A is kept. Every later
//     stage runs on the kept one. (Real overflow equals 2D overflow on
//     every benchmark measured -- layer assignment adds none, Part 78.)
//     PD16ROUTE_RECLAIM is ignored in this mode (B is the reclaim path).
// Off by default: pd18route byte for byte. Cost: one more region stage
// and refinement.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 86.
//
// --- pd18route.cpp's own header, inherited unchanged: ---
//
// pd18route.cpp
//
// Forked from pd17route.cpp (committed). Species `pd`, generation 18: the
// detour reclaim pass inside the slow tier, so its injected overflow is
// renegotiated by more slow rounds (growing boxes, history) instead of
// being handed to the region stage, which on newblue1 settled above the
// old residue (HISTORY_COST_SWEEP.md Parts 83-84).
//   PD18ROUTE_RECLAIM_AT_STOP=1: when the slow tier reaches PD9ROUTE_STOP_AT
//     the first time, run the reclaim pass (PD16ROUTE_RECLAIM_* and
//     PD17ROUTE_RECLAIM_AVOID settings), reset the best-state tracking (so
//     the post-reclaim states are what the tier returns) and keep running
//     slow rounds until the stop condition holds again or the round budget
//     ends.
//   PD18ROUTE_RECLAIM_EVERY=K: also run it after every K-th slow round
//     (same reset), for benchmarks that never reach the stop.
// Both off by default: pd17route byte for byte. PD16ROUTE_RECLAIM still
// controls the separate pass before the region stage.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 85.
//
// --- pd17route.cpp's own header, inherited unchanged: ---
//
// pd17route.cpp
//
// Forked from pd16route.cpp (committed). Species `pd`, generation 17: keep
// the detour reclaim pass away from the remaining overflow.
//
// pd16route's reclaim at mu 8 cut wirelength on every benchmark tested
// (bigblue2 -4.1%, newblue1 -1.6%, newblue2 -0.24%; all three runs below all
// three baselines) but worsened newblue1's overflow (54-70 vs 46-48), and a
// strict version that adds no overflow kept almost none of the gain
// (HISTORY_COST_SWEEP.md Part 83). The gain needs the temporary overflow
// and the renegotiation that clears it; newblue1's residue does not absorb
// overflow injected near its hotspot.
//   PD17ROUTE_RECLAIM_AVOID=D: a net is not a reclaim candidate if any of
//     its wire's tiles lies within D tiles (in x and in y) of a tile of an
//     edge overflowed when the pass starts. Proximity to each overflowed
//     edge, not one bounding box: the leftover overflow is scattered (the
//     region stage's single box was 80 x 338 tiles on bigblue2).
// Default -1 (off): pd16route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 84.
//
// --- pd16route.cpp's own header, inherited unchanged: ---
//
// pd16route.cpp
//
// Forked from pd15route.cpp (committed). Species `pd`, generation 16: a
// detour reclaim pass between the slow tier and the region stage
// (IDEAS_TEST_PLAN.md idea 4; HISTORY_COST_SWEEP.md Part 83).
//
// The per-stage trace (Part 82) showed the same shape on every benchmark:
// the pattern stage ends at the pins' MST, the slow tier adds +24-34%
// detour, and 1.6-3.4% of the nets carry 78-90% of it. Those nets were
// detoured while congestion was everywhere and are never rerouted again
// once it clears, because the slow tier only reroutes overflowed pieces --
// and by then its WL weight has decayed to ~1e-7. A fresh present-demand
// price puts the WL weight back near 1 (pdFactorForIter(0) = 0.993).
//   PD16ROUTE_RECLAIM=1: before the region stage, every non-giant net
//     whose 2D wire exceeds PD16ROUTE_RECLAIM_RATIO (1.5) x its pin-tile
//     MST and the MST by at least PD16ROUTE_RECLAIM_MIN_EXCESS (4) tiles is
//     rerouted whole (MSMV, margin PD16ROUTE_RECLAIM_MARGIN = 16) under
//     fresh present-demand prices, most excess first. A reroute is kept
//     only if (wire saved) > PD16ROUTE_RECLAIM_MU (8) x (local overflow
//     added, capacity units); otherwise it is undone. Overflow it adds is
//     left to the region and refinement stages.
//     PD16ROUTE_RECLAIM_PASSES (1) repeats the selection and reroutes.
// Off by default: pd15route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 83.
//
// --- pd15route.cpp's own header, inherited unchanged: ---
//
// pd15route.cpp
//
// Forked from pd14route.cpp (committed). Species `pd`, generation 15: a
// stage-by-stage detour trace for the wirelength investigation
// (IDEAS_TEST_PLAN.md idea 4; HISTORY_COST_SWEEP.md Parts 78-79). The
// remaining wirelength gap to NTHU-Route 2.0 follows detour -- 2D wire over
// each net's pin-tile rectilinear MST (+5.9 to +13.4% on the outliers vs
// NTHU's +2.2% on adaptec1) -- and holding the slow tier's WL term up did
// not reduce it. This says which stage puts the detour in.
//   PD15ROUTE_DETOUR_TRACE=1: after the pattern stage, the slow tier, the
//     region stage, refinement, the tree rebuild and WL recovery, print the
//     non-giant nets' total 2D wire (tile edges charged), the sum of their
//     pin-tile MSTs, the excess, and how much of it sits in nets more than
//     1.5x their MST.
// Trace only: routes are pd14route's byte for byte, on or off.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 82.
//
// --- pd14route.cpp's own header, inherited unchanged: ---
//
// pd14route.cpp
//
// Forked from pd13route.cpp (committed). Species `pd`, generation 14: a
// work-gated plateau stop for the hotspot (region) stage (roadmap step 1,
// runtime). pd12route's plateau rule only applies while the best overflow
// is >= PD12ROUTE_PLATEAU_MIN, because small residues close in the long
// tail -- bigblue2 reached zero at region rounds 308-442 across five runs.
// But that tail costs bigblue2 almost nothing (500 rounds in 198-361 s),
// while bigblue4 spent 15.1 h of 500 rounds going 408 -> 404 even at
// margin cap 200 (HISTORY_COST_SWEEP.md Parts 76-79). The cost of a
// round, not the size of the residue, is what should decide.
//   PD14ROUTE_REGION_COST_WINDOW=N, PD14ROUTE_REGION_COST_POPS=P: the region
//     stage also stops when its best has not fallen by
//     max(1, PD12ROUTE_PLATEAU_GAIN% of the best N rounds earlier) over
//     the last N rounds AND those N rounds expanded at least P search
//     nodes, whatever the residue.
// Work is counted as search-node expansions (heap pops past the stale-
// entry check in multiSourceDijkstra), not wall time, so the stop -- and
// the route -- stay deterministic run to run. The region trace line and
// the stage summary report the expansions. Both settings default to 0
// (off): pd13route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 80.
//
// --- pd13route.cpp's own header, inherited unchanged: ---
//
// pd13route.cpp
//
// Forked from pd12route.cpp (committed). Species `pd`, generation 13: a hard
// cap on the search margin of the two post-search stages (roadmap step 1,
// runtime). The hotspot stage (regionNegotiate) searches each reroute with
// margin PD5ROUTE_REGION_BOX_INIT + round * PD5ROUTE_REGION_BOX_GROW (10 +
// 2r by default) and refinement with PD4ROUTE_REFINE_BOX_INIT + iteration *
// PD4ROUTE_REFINE_BOX_INC -- neither capped, unlike the slow tier's
// NV11ROUTE_BOX_CAP (200). By round 499 a region search spans 1008 tiles
// each way: newblue3's first 59 region rounds took 34 s each, its rounds
// past 200 took 10-13 minutes, and bigblue4 spent ~22 of its 23.3 hours in
// 500 region rounds on a 20 x 25-tile hotspot (HISTORY_COST_SWEEP.md Parts
// 73b-74). CLAUDE.md's box rule: grow by congestion history, with a hard
// cap.
//   PD13ROUTE_REGION_BOX_CAP=N: the region margin never exceeds N.
//   PD13ROUTE_REFINE_BOX_CAP=N: the refinement margin never exceeds N.
// Both default to 0 (uncapped): pd12route byte for byte. The retry-on-no-
// path doubling inside the search is untouched.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 76.
//
// --- pd12route.cpp's own header, inherited unchanged: ---
//
// pd12route.cpp
//
// Forked from pd11route.cpp (committed). Species `pd`, generation 12: a
// plateau stop for the two post-search stages (roadmap step 1, runtime).
// pd11route's hotspot rip-up (regionNegotiate) always runs all
// PD5ROUTE_REGION_ROUNDS rounds and refinement (refine2D) all
// PD4ROUTE_REFINE_ITERS iterations unless overflow reaches zero -- on the
// large ISPD 2008 benchmarks that is most of the run: bigblue4's hotspot
// stage found its best (406) and then spent hundreds more rounds within a
// few percent of it; newblue3's sat within 0.1% of 39100 from round 49 on at
// ~13 minutes a round (HISTORY_COST_SWEEP.md Part 73b). Both stages already
// keep the best state seen, so stopping early can only lose improvements
// the remaining rounds would have found.
//   PD12ROUTE_REGION_PLATEAU=N: stop the hotspot stage once its best 2D
//     overflow has not fallen by the required gain over the last N rounds.
//   PD12ROUTE_REFINE_PLATEAU=N: the same for refinement iterations.
//   PD12ROUTE_PLATEAU_GAIN=P: the required gain, percent of the best N
//     rounds earlier (default 1); at least 1 overflow unit always.
//   PD12ROUTE_PLATEAU_MIN=M: the rule applies only while the best is >= M
//     (default 0: always). Replaying the recorded traces showed why: the
//     hotspot stage's long tail is where small residuals reach zero --
//     bigblue2 went 10 -> 4 -> 0 at round 437 after long flat stretches, so
//     a 50- or 100-round window alone would have cost its zero -- while
//     newblue3 (~39000, not routable to zero) gains nothing after round ~50.
// Both windows default to 0 (off): pd11route byte for byte. The stage
// summaries also report each stage's wall time.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 74.
//
// --- pd11route.cpp's own header, inherited unchanged: ---
//
// pd11route.cpp
//
// Forked from pd10route.cpp (committed). Species `pd`, generation 11: the
// batched pattern-routing stage (roadmap step 1; GPU_BACKEND_PLAN.md phase
// A), replacing phase A's 60 whole-net L/Z fast rounds and the cleanup pass
// (patternStage2D; PD11ROUTE_PATTERN=1, default on; 0 reproduces pd10route).
// Pieces are the nets' topology edges; each round routes a set of them in
// color batches (disjoint coarse boxes, PD11ROUTE_PATTERN_CELL = 8 tiles),
// one IPatternBackend::solve per batch against a frozen fixed-point cost
// snapshot (gr/gpu/pattern_kernel.h: straight, both Ls, a Z at every interior
// column and row). Round 0 routes everything; later rounds only pieces on
// overflowed edges; PD11ROUTE_PATTERN_ROUNDS (30). The slow tier and every
// later stage follow unchanged.
//
// Backends: the one-line g++ build uses the CPU reference (header-only).
// Built through gr/gpu/CMakeLists.txt with GR_PATTERN_SELECT, the backend is
// chosen at run time (GR_GPU=auto|cpu|metal|cuda).
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 72.
//
// --- pd10route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd9route.cpp (committed). Species `pd`, generation 10: roadmap
// step 1's pattern-routing stage, first part -- NTHU-Route's
// monotonic-before-maze rule at piece granularity. PD10ROUTE_PATTERN_FIRST=1
// (default): when rerouteNetPieces takes a piece out, its two endpoints are
// first joined by the cheapest L or Z shape (generateCandidates) whose every
// tile edge has room for the net and which touches the net's tree only at
// those endpoints; only if no shape fits does the MSMV maze search run. The
// shape evaluation is the part GPU_BACKEND_PLAN.md later batches onto a GPU.
// PD10ROUTE_PATTERN_FIRST=0 reproduces pd9route.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 72.
//
// --- pd9route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd8route.cpp (committed). Species `pd`, generation 9: the
// search stage's cost. pd8route's pipeline takes ~1.3h (adaptec1) to ~6h
// (newblue1), almost all of it in pd2route's slow tier, which regrows every
// escalated overflowed net as a whole each round (~90000 whole-net reroutes
// in adaptec1's first slow round) -- and, under a WL weight decayed to ~1e-3,
// gives every part of such a net a congestion-only shape, which is where the
// extra wirelength comes from (Part 70). NTHU-Route reroutes only the 2-pin
// pieces crossing overflow and stops its main stage at an overflow
// threshold.
//   PD9ROUTE_SLOW_PIECES=1: the slow tier reroutes only the pieces of a net
//     that cross overflow (rerouteNetPieces, under the stage's pricing and
//     acceptance rule), falling back to the whole net when its wiring is not
//     a tree.
//   PD9ROUTE_STOP_AT=N: the slow tier stops once 2D overflow <= N; the
//     region / refinement stages finish.
// Both off by default: pd8route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 71.
//
// --- pd8route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd7route.cpp (committed). Species `pd`, generation 8: a
// whole-net tree rebuild (rebuild2D) before pd7route's piece recovery.
// Each net (<= PD8ROUTE_REBUILD_MAX_PINS pins, default 32) is regrown from
// its pins alone, Prim-style, by the bend-aware search over only tile edges
// with room for it (box: pins' bbox + PD8ROUTE_REBUILD_MARGIN, default 16),
// and kept only if its shape cost (tile edges + bend * corner tiles) is
// strictly lower -- piece recovery can shorten a piece but never change a
// net's topology. Overflow cannot rise. PD8ROUTE_REBUILD=0 reproduces
// pd7route.
//
// Measured (HISTORY_COST_SWEEP.md Part 70): small -- adaptec1 5991707 ->
// 5958029 (-0.6%) with ~49000 nets rebuilt. Near zero overflow almost every
// edge is full, so capacity-respecting local moves are locked in; the
// extra wire is created earlier, by the search stage's whole-net reroutes
// under a WL weight decayed to ~1e-3.
//
// --- pd7route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd6route.cpp (committed). Species `pd`, generation 7:
// wirelength. pd6route reaches 0 / 0 / 66 overflow but scores 7.8-8.7M
// wirelength, against NTHU-Route's 5.36M on adaptec1 (HISTORY_COST_SWEEP.md
// Part 69). The gap was mostly vias (adaptec1: 3.93M vs NTHU's 1.77M),
// and 64% of ours sat at pin tiles.
//
// Two mechanisms, both overflow-neutral:
//   1. PD7ROUTE_LA_PINS=1 (default): pin-aware layer assignment. In the
//      nthu DP a pin node's via stack must reach its pins' layers
//      (nodePinRange), so pin access is priced inside the DP -- NTHU's
//      rec_count does the same -- instead of stitchPinAccess bolting a stack
//      on after the DP chose layers blind to it (it put wires next to pins on
//      the top layers: 4-5 vias per pin).
//   2. PD7ROUTE_WL=1 (default): wirelength and bend recovery (wlRecover2D),
//      the last 2D stage. Every piece of every net is regrown by a
//      bend-aware search (1 per tile edge + PD7ROUTE_WL_BEND per corner) over
//      only tile edges with room for it, and kept only if strictly cheaper --
//      so overflow can never rise. Defaults bend 3, 4 passes, margin 8
//      (adaptec1 sweep).
// PD7ROUTE_WL=0 PD7ROUTE_LA_PINS=0 reproduces pd6route byte for byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 69.
//
// --- pd6route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd5route.cpp (committed). Species `pd`, generation 6:
// NTHU-Route 2.0's rip-up granularity for the post-2D stages.
//
// Why: pd5route's hotspot rip-up on newblue1 reroutes every net touching
// the box as a whole -- 10000-22000 nets -- and its first round blows 2D
// overflow up 30-40x before recovering (HISTORY_COST_SWEEP.md Part 64). NTHU
// rips up only the 2-pin pieces of a net that cross overflow and leaves the
// rest of the net's tree in place.
//
// The mechanism (rerouteNetPieces; PD6ROUTE_PIECES=1 switches both
// refine2D and regionNegotiate to it): a net's 2D wiring is its tree of
// unit tile edges; a piece is a maximal path between key tiles (pins, and
// tiles of degree != 2). A piece is taken out and the two parts of the tree
// it joined are reconnected by one MSMV search from every tile of one part
// to any tile of the other (NTHU reconnects only the piece's endpoints).
// Refinement reroutes pieces with an overflowed tile edge, under its
// keep/revert rule per piece; region round 0 reroutes every piece with a
// tile in the box, later rounds the overflowed ones, all kept. Nets whose
// wiring is not a tree fall back to the whole-net reroute. MSMV gains a
// sink-bounding-box heuristic (admissible; O(1) per push) for these
// many-sink searches. Default (pieces off) reproduces pd5route byte for
// byte.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 67.
//
// --- pd5route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd4route.cpp (committed). Species `pd`, generation 5: two
// attacks on newblue1's last 484 (HISTORY_COST_SWEEP.md Part 63), one
// 30 x 29-tile hotspot that pd4route's refinement stops moving on.
//   1. PD5ROUTE_REFINE_ORDER=shuffle (PD5ROUTE_SEED): each refinement
//      iteration reroutes its overflowed nets in a seeded random order,
//      differing per iteration, instead of NTHU's fixed (overflow, bbox)
//      order -- which re-derived the same routes every iteration once the
//      plateau set in (~2900 reroutes, ~180 reverts, no change).
//   2. PD5ROUTE_REGION=1: hotspot rip-up (regionNegotiate) before
//      refinement. The box around every overflowed 2D edge, expanded by
//      PD5ROUTE_REGION_EXPAND (5); every non-giant net with wire in it --
//      overflowed or not -- is rerouted under pd2route's winning
//      negotiation (present-demand pricing, keep every reroute), then the
//      still-overflowed ones for PD5ROUTE_REGION_ROUNDS (60) rounds with
//      MSMV margin BOX_INIT (10) + round * BOX_GROW (2); the best state is
//      kept. Refinement only ever moved overflowed nets; this also moves
//      the legal nets packed around them.
// Defaults (nthu order, region off) reproduce pd4route.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 64.
//
// --- pd4route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd3route.cpp (committed). Species `pd`, generation 4: adds
// NTHU-Route 2.0's refinement stage (Post_processing.cpp, read directly)
// between the 2D stage and layer assignment.
//
// Why: after pd3route made layer assignment lossless, the remaining
// overflow was the 2D stage's, and on bigblue1 and newblue1 it sat on
// plateaus the negotiation could not leave (HISTORY_COST_SWEEP.md Part 61:
// 2D oscillating 84-126 and 1108-1186 for the last ~80 rounds of a full
// budget). NTHU switches regime there: a refinement stage whose edge cost
// is 1 for a full edge and 0 otherwise, rerouting only what crosses
// overflow, most overflowed and largest first, with search boxes of 10 + 15
// per iteration for up to 20 iterations.
//
// The mechanism (refine2D, after route() or after PD3ROUTE_LA_FROM's
// projection; CostModel::kRefine, refineCost):
//   - cost: 1 for an edge one more net would overflow (or with capacity
//     <= 0), kRefineEps otherwise -- fewest full edges, length only a
//     tie-break;
//   - each iteration, every non-giant net crossing an overflowed edge is
//     regrown by MSMV (whole nets; NTHU reroutes 2-pin pieces), ordered by
//     its overflow then its pin bounding box, descending;
//   - margin = PD4ROUTE_REFINE_BOX_INIT + it * PD4ROUTE_REFINE_BOX_INC
//     (10, 15), PD4ROUTE_REFINE_ITERS (20);
//   - PD4ROUTE_REFINE_ACCEPT=keep (NTHU, default) or revert (undo a reroute
//     whose local overflow got worse);
//   - the best whole-grid state seen is what goes on to layer assignment.
// PD4ROUTE_REFINE=0 turns it off, reproducing pd3route.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 63.
//
// --- pd3route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd2route.cpp (committed). Species `pd`, generation 3: the 2D
// stage is pd2route's unchanged; layer assignment is replaced by NTHU-Route
// 2.0's approach (its Layerassignment.cpp, read directly).
//
// Why: pd2route (present-demand pricing + keep every reroute) converged
// adaptec1's 2D stage to zero overflow, yet the real result was 7570 --
// all of it added by the inherited layer assignment (nv2route's DP). That
// DP assigns one layer per whole straight leg, prices wire with a soft
// usage/capacity ratio averaged over the leg (no cliff at capacity, so one
// full tile inside a long leg barely registers), and prices vias against a
// via-site capacity model that ISPD 2008 scoring does not have. NTHU's
// layer assignment reaches real zero on adaptec1 from its own 2D-zero
// solution: per-tile layer choice, wire capacity as a hard constraint, the
// via count as the objective, nets ordered by pins per unit length.
//
// The mechanisms (PD3ROUTE_LA=nthu, the default; =inherited restores
// pd2route's layer assignment exactly):
//   - splitToUnitEdges: each net's tree is split into one-tile edges, so the
//     DP may change layer at any tile.
//   - layerAssignDP(nthuCost): a tile edge the net would push over its real
//     capacity costs kLaOverflowPenalty per unit of overflow (times wire
//     history, for the repair rounds); vias cost 1 per layer crossed, no
//     via-site capacity term. Overflow first, vias second.
//   - laNetOrder: NTHU's order, most pins per 2D tile edge first, ties to
//     fewer pins.
//   - Repair rounds re-assign only nets with real wire overflow.
// PD3ROUTE_LA_FROM=<route file> skips the 2D stage and reruns layer
// assignment alone on an existing solution, projected back to 2D
// (readRouteAs2D) -- seconds instead of an hour per variant.
//
// RESULTS: see HISTORY_COST_SWEEP.md Part 62.
//
// --- pd2route.cpp's own header, inherited unchanged: ---
//
//
// Forked from pd1route.cpp (committed). Species `pd`, generation 2: same
// present-demand pricing, plus the other half of NTHU-Route 2.0's
// negotiation -- its acceptance rule -- and a speed fix.
//
// Why: pd1route's screens (HISTORY_COST_SWEEP.md Part 59) lost to lambda
// pricing at the same budget (+37% / +60% on adaptec1) at 3.2-3.8x the
// runtime. The traces showed the inherited supervision rule (undo a reroute
// that made its local overflow worse) keeping far fewer reroutes under
// present-demand pricing (by slow round 9, ~9000 per round vs lambda's
// 13700-15400). Under NTHU's cost an edge exactly at capacity is barely
// dearer than one just below, so reroutes land on just-full edges and get
// undone. NTHU never undoes a reroute; it keeps every new route and lets
// history pricing sort it out. The two look like a matched pair.
//
// The mechanisms:
//   PD2ROUTE_ACCEPT=revert (default) -- the inherited rule, unchanged.
//   PD2ROUTE_ACCEPT=keep             -- every slow-tier reroute is kept
//       (revertReroute). The cleanup pass stays supervised. The slow tier's
//       patience stop is off under keep (overflow is expected to rise before
//       it falls; NTHU runs a fixed budget), and the best snapshot is still
//       what route() returns. VROUTE_TRACE adds each round's count of kept
//       reroutes that made local overflow worse.
//   cong^5 is computed by multiplication, not std::pow, in every edge
//       relaxation (agrees to 1e-13 relative;
//       testPdCongPowerByMultiplicationMatchesPow). pd results therefore
//       differ from pd1route's in the last bits; lambda results do not, and
//       stay byte-identical to nv11route.
// Knobs inherited unchanged: PD1ROUTE_COST, PD1ROUTE_WL_FLOOR, NV11ROUTE_BOX_*.
//
// RESULTS: pending -- see HISTORY_COST_SWEEP.md Part 60.
//
// --- pd1route.cpp's own header, inherited unchanged: ---
//
//
// Forked from nv11route.cpp (committed; its `round` 48/+2/200 setting is the
// current best, 112086 total -- HISTORY_COST_SWEEP.md Part 57). New species
// `pd` ("present demand"), generation 1: the search, rip-up and supervision
// machinery are nv11route's, but the negotiation model -- how an edge's cost
// responds to the nets on it -- is replaced, which is a different core
// dynamic, not a new coefficient.
//
// Why: Part 57 showed wider search boxes have given what they can (bigblue1
// and newblue1 stay flat with boxes covering most of the grid), and Part
// 58's side-by-side with NTHU-Route 2.0 -- which reaches zero overflow on
// adaptec1 and bigblue1 on this machine -- found the pricing model to be the
// deepest difference. Every router in this lineage prices an edge with
// lambda <- lambda * exp(eps * term), which only ever rises; an edge that
// was once badly congested stays expensive after the nets leave it (only
// the separate LIFO history term relaxes). NTHU-Route prices from the
// edge's *present* demand, recomputed on every insert/remove:
//
//   cong = (demand + inc) / (cap * (1 - (hist - 1) / (iter * (1.5 + 3 f))))
//   cost = WL + hist * cong^5,   WL = f = 1 - exp(-5 exp(-0.1 iter))
//
// hist counts iterations the edge has ended overflowed (from 1) and also
// shrinks the edge's effective capacity ("overflow prediction"); the WL
// weight f decays from ~0.99 toward 0, so late iterations price congestion
// almost alone. See presentDemandCost / advancePresentDemand.
//
// The mechanism: every 2D wire-edge cost -- the fast tier's pattern and
// layer choice and both slow-tier searches -- goes through one seam
// (wireCostH/wireCostV). PD1ROUTE_COST picks the model:
//   pd (default) -- NTHU's cost above, read from live Usage. iter advances
//                   and hist grows at every round boundary (after each fast
//                   round, the cleanup pass, and each slow round), so one
//                   schedule spans the whole 2D rip-up loop, as NTHU's main
//                   stage does. The lambda/LIFO-history updates still run
//                   but nothing reads them.
//   lambda       -- nv11route's cost exactly; byte-identical output (Stage
//                   0 parity check).
// PD1ROUTE_WL_FLOOR (default 0 = NTHU) keeps the WL term at max(f, floor).
// A* scales its Manhattan heuristic by a true lower bound on an edge's cost
// (1 under lambda, the WL term under pd) -- under pd an edge can cost far
// below 1, and the unscaled heuristic returns wrong paths (self-test
// testAStarStaysExactUnderPdPricing). The cost: as f decays, A* degrades
// toward Dijkstra.
//
// Not ported (on purpose, to test the pricing change alone): NTHU's
// segment-level rip-up with no revert, congestion-band ordering, monotonic
// routing, per-iteration topology rebuild, refinement stage, and 2D bend
// cost. The supervision rule (revert if local overflow got worse) stays.
// The box-growth knobs are nv11route's (NV11ROUTE_BOX_*).
//
// RESULTS: pending -- see HISTORY_COST_SWEEP.md Part 59.
//
// --- nv11route.cpp's own header, inherited unchanged: ---
//
//
// Forked from nv4route.cpp (the committed best -- NOT from nv10route; the
// nv5-nv10 generations' cluster-rescue/joint-assignment mechanisms all
// closed negative and are deliberately left out). Species `nv`,
// generation 11: no new core algorithm, a change to how wide the slow
// tier's maze searches may look.
//
// Why: every router from mroute onward capped the slow tier's search box
// at VROUTE_MAX_MARGIN=6 tiles past the net's own bounding box, widening
// it only when no path existed at all -- never when every path inside the
// box was congested. That cap came from a speed fix in mroute.cpp (see
// CLAUDE.md's revised rule) and was never re-tested. HISTORY_COST_SWEEP.md
// Part 51 re-tested it on nv4route: margin 24 alone cut total overflow
// 42.5% at the full budget on all three benchmarks (277344 -> 159592), and
// the screen showed margin 48 better still. The ISPD 2008 winner,
// NTHU-Route 2.0 (Part 52), which reaches zero here on adaptec1 and
// bigblue1, starts its box at 10-25 tiles and grows it by 1 every
// iteration, applied only to nets whose cheap monotonic route overflows.
//
// The mechanism: each net carries its own boxMargin, passed to both slow-
// tier search entry points (routeStepMSMV, routeSegment) as the starting
// margin instead of VROUTE_MAX_MARGIN. NV11ROUTE_BOX_MODE picks how it's set:
//   fixed  (default) -- boxMargin stays -1, i.e. VROUTE_MAX_MARGIN: exactly
//                       nv4route. Stage 0 parity checks this byte-for-byte.
//   round  -- NTHU-Route style: in slow round k every net's margin is
//             NV11ROUTE_BOX_INIT + k * NV11ROUTE_BOX_GROW.
//   pernet -- a net starts at INIT and grows by GROW after each round in
//             which it is still overflowed; nets that resolve stop
//             growing. Spends breadth only where it buys detours.
// Both non-fixed modes clamp at NV11ROUTE_BOX_CAP. The no-path doubling
// retry is unchanged. Invalid settings (unknown mode, INIT < 1, GROW < 0,
// CAP < INIT, non-numeric) fail loudly rather than being clamped.
// VROUTE_TRACE=1 adds each slow round's mean margin and at-cap count.
//
// RESULTS: pending -- see HISTORY_COST_SWEEP.md Part 53.
//
// --- nv4route.cpp's own header material below, inherited unchanged as
// accurate history of the mechanisms this generation builds on. ---
//
// nv4route.cpp
//
// Forked from nv3route.cpp -- inherits the giant-net-into-fast-tier
// mechanism and everything below it completely unchanged; see
// nv3route.cpp's own header (retained below) for that mechanism's
// derivation. Species `nv`, generation 4 -- no new core algorithm, just a
// coefficient retune (`VROUTE_THRESHOLD` 0.2->0.05, `VROUTE_POWER`
// 2.5->1.5) of the same fast-tier pricing curve nv3route already uses,
// per CLAUDE.md's naming convention.
//
// Why this is worth trying again: Part 33 (HISTORY_COST_SWEEP.md) swept
// these same two coefficients on nv2route -- *before* nv3route's own
// giant-fast-tier mechanism existed -- and found threshold=0.05/power=1.0
// the best of 15 screened combinations, but it reversed at the real round
// budget to a mixed, net-negligible three-benchmark result (282354 ->
// 282356, one benchmark up, two down). That closed the coefficient-tuning
// angle on its own. It was never tried *combined* with nv3route's later,
// structurally different giant-net fix -- the two mechanisms touch the
// same fast-tier pricing curve from different angles (nv3route changes
// *which nets* reach it; a coefficient retune changes *how it prices*
// once they're there) and this session's own repeated finding is that
// individually-diagnosed fixes do not compose additively by default (see
// Part 39, delayed supervision: two real root causes addressed correctly,
// still a net loss). Two prior joint-mechanism attempts this session
// (corner-choice + giant-fast-tier, delayed supervision) were both
// negative -- this is the third, and the first to actually hold.
//
// Methodology change, motivated by this session's own track record:
// every coefficient- or dynamics-altering mechanism screened so far at
// the lineage's original reduced budget (`VROUTE_FAST_ROUNDS=15
// VROUTE_SLOW_ROUNDS=25 NV2ROUTE_VIA_ROUNDS=20`) has had its screen-level
// result reverse or collapse at the real round budget at least once
// (Part 33's own coefficient sweep, Part 37's MSMV eligibility, Part 39's
// delayed supervision -- three separate mechanisms, three reversals).
// This sweep screened instead at roughly half the real budget
// (`VROUTE_FAST_ROUNDS=30 VROUTE_SLOW_ROUNDS=75 NV2ROUTE_VIA_ROUNDS=100`),
// a genuine cost/reliability tradeoff (each screen run ~35 minutes
// instead of a few), made affordable by the same abundant-compute
// authorization that raised this project's own concurrent-process cap
// from 3 to 10 for this investigation.
//
// Screen (adaptec1, nv3route's own giant-fast-tier mechanism left at its
// default-on setting, baseline 76550 at this larger budget): a 3x3 grid
// around Part 33's own winning region --
//
//   threshold \ power   0.5      1.0      1.5
//   0.0                 78052    74984    76390
//   0.05                76432    76048    74984
//   0.1                 79186    75522    76026
//
// Two combinations tied for best at -2.05% (threshold=0.0/power=1.0 and
// threshold=0.05/power=1.5) -- both real per `gr_verify` (0 malformed/
// disconnected/incomplete/unrouted), and both a visibly smaller edge than
// Part 33's own -9.6% screen-level result at the shorter budget, exactly
// the pattern this methodology change was meant to surface rather than
// be misled by.
//
// **Mandatory round-budget sanity check** (this investigation lineage's
// own hard-won discipline -- no promotion without it) on both tied
// candidates, at the real committed operating point (default 60/150 2D
// rounds, `NV2ROUTE_VIA_ROUNDS=200`), against nv3route's own already-
// committed adaptec1 baseline (74210, Part 36): threshold=0.0/power=1.0
// held at 73024 (-1.60%); threshold=0.05/power=1.5 held slightly better
// at 72882 (-1.79%). **Neither reversed** -- the first coefficient- or
// dynamics-related sweep in this entire session (after three straight
// reversals: Parts 33, 37, 39) whose screen-stage signal survived the
// real-budget check intact. threshold=0.05/power=1.5 was carried forward
// as the winner.
//
// **Full three-benchmark result** at the committed operating point,
// `gr_verify`-confirmed legal on all three (0 malformed/out-of-bounds/
// disconnected/incomplete/unrouted) and cross-checked exactly against
// `eval2008.pl` (total overflow, max overflow, and scored wirelength
// matching to the unit on every benchmark):
//
//   benchmark   nv3route (committed best)   nv4route   delta
//   adaptec1    74210                       72882      -1.79%
//   newblue1    21634                       21418      -1.00%
//   bigblue1    185246                      183044      -1.19%
//   sum         281090                      277344      -1.33%
//
// **Decision: `gr/nv4route.cpp` is this project's new best real result,
// replacing `gr/nv3route.cpp`.** Unanimous on all three benchmarks --
// clears this project's own promotion bar cleanly, no hedging needed.
// This is the first genuinely positive joint-mechanism combination found
// in this session's own explicit search for one (see HISTORY_COST_SWEEP.md
// Part 39's closing line), and it answers the user's own question from
// earlier in this session more concretely than either individual result
// could: combining a structural fix (which nets can negotiate) with a
// dynamics fix (how negotiation prices congestion once they can) compounds
// where combining two dynamics-only fixes (delayed supervision) or a
// structural fix with a second small structural tweak (corner-choice) did
// not. Not proof that's the general rule -- a sample of three joint
// attempts is far too small -- but a concrete, verified data point in
// that direction, worth keeping in mind for future joint-mechanism
// candidates. See `HISTORY_COST_SWEEP.md` Part 41 for the full record.
//
// --- nv3route.cpp's own header material below, inherited unchanged as
// accurate history of the mechanism this generation builds on. ---
//
// Forked from nv2route.cpp -- inherits historyWire, historyVia, MSMV
// escalation, RSMT/Steinerize topology construction, and the entire
// negotiated-congestion 2D pricing loop completely unchanged; see
// nv2route.cpp's own header for that mechanism's full derivation. Species
// `nv`, generation 3 -- a direct refinement (widening which nets
// participate in the existing fast tier's own per-leg price negotiation),
// not a new core algorithm, per CLAUDE.md's naming convention. Scoped in
// /Users/bobboyd/.claude/plans/async-shimmying-pancake.md.
//
// Why: a per-net ranking diagnostic (`diag7`, throwaway, not part of the
// repo, complementing `diag5`'s pin-count buckets and `diag6`'s
// processing-order deciles with an actual per-net leaderboard) found that
// in every one of the three real benchmarks, the single largest "giant
// net" (`n.pins.size() > kGiantNetPins`=1000 raw pins -- the ISPD contest's
// own official "need not be routed" exemption threshold, not a tuned
// performance constant, matching `gr_verify.cpp`'s identical exemption
// check) is the #1 or #2 worst overflow offender by both total attributed
// overflow and distinct-overflowed-edges-touched, out of a population of
// 176,716 to 270,715 routed nets:
//
//   benchmark   giant nets (raw pins)         rank by overflow breadth
//   adaptec1    n7807 (2271)                  #1 -- 344 distinct edges
//   bigblue1    n10760 (2621), n10756 (1342)  #1 and #2 -- 530/531 edges
//   newblue1    n71 (12335), n1211 (4682)     #1 and #2 -- 368/93 edges
//
// Only 1-2 such nets exist per benchmark -- a small, concentrated
// population, meaning per-net cost tolerance for a fix is much higher
// than anywhere else in this codebase.
//
// Root cause, confirmed by direct code reading: giant nets are routed via
// `routeGiantNet`, which gets `steinerizeTree`'s topology (the same as
// any net too large for exact RSMT) but then commits each MST edge as a
// fixed horizontal-then-vertical L-shape with zero price awareness
// (`routeGiantNet`'s own signature takes `Usage&`, never `Prices&`) and
// zero congestion awareness (no search, unconditional commit) -- and is
// entirely excluded from `route()`'s fast tier, slow tier, and cleanup
// pass. A giant net's initial, congestion-blind route is never revisited
// at the 2D level, while every other net gets up to 60+150 rounds of
// price-based negotiation to escape congestion. It does still charge
// usage for whatever it emits (`chargeH`/`chargeV` call `usage.h`/
// `usage.v` directly), so its congestion is fully counted in the final
// score -- it just never gets a chance to negotiate it away. CLAUDE.md's
// own "Design decisions already made" section already prescribes a
// bigger fix for this class of net ("Decompose giant nets... route it as
// subnets, not a monolith") -- this generation takes the narrower, first
// question directly: is the problem "no negotiation at all," or is it
// structural (topology)? Answered empirically below, not assumed.
//
// The new mechanism: let giant nets participate in the fast tier's own
// per-leg price-negotiated reroute (`routeNetFast`), unchanged from how
// every other net already uses it -- no new function, no signature
// change. `routeGiantNet`'s own initial commit still runs first (every
// net needs a starting route), and giant nets still never reach the slow
// tier's MSMV escalation or the cleanup pass -- this generation only
// widens fast-tier eligibility, nothing else. Cost, measured directly
// (not assumed): `routeNetFast` on the real giant net (adaptec1's n7807,
// 1926 unique points) took ~1.07ms per call -- negligible even across 60
// rounds.
//
// Known risk, disclosed up front: this is the narrowest possible test of
// "does negotiation help at all," deliberately not yet answering whether
// slow-tier eligibility or genuine subnet decomposition would help
// further -- both are real, named next steps, not implied by this
// generation's own result either way.
//
// Real result: a genuine, verified win on all three real benchmarks --
// the first positive result in this investigation lineage since
// nv2route itself (RSMT-cap sweep, coefficient sweep, net-ordering
// probe, and fast-tier accept/revert supervision were all negative or
// negligible; see HISTORY_COST_SWEEP.md Parts 32-35). A reduced-round
// screen showed a promising delta that a mandatory round-budget sanity
// check (this investigation lineage's own hard-won discipline, after
// Part 33 caught a screen-level effect collapsing at the real budget)
// confirmed held at the real 2D round budget. Full three-benchmark
// result at the committed operating point (`NV2ROUTE_VIA_ROUNDS=200`),
// `gr_verify`-confirmed legal on all three (0 malformed/out-of-bounds/
// disconnected/incomplete/unrouted) and cross-checked exactly against
// `eval2008.pl` (total overflow, max overflow, and scored wirelength all
// matching to the unit on every benchmark):
//
//   benchmark   nv2route (committed best)   nv3route   delta
//   adaptec1    75192                       74210      -1.31%
//   newblue1    21904                       21634      -1.23%
//   bigblue1    185258                      185246     -0.006% (flat, not worse)
//
// **Decision: `gr/nv3route.cpp` is this project's new best real result,
// replacing `gr/nv2route.cpp`.** No benchmark regressed; two improved
// meaningfully and the third held essentially flat -- clears this
// project's own established bar (every prior adopted result won
// unanimously or, as here, unanimously non-negative with a clearly
// net-positive aggregate: 281090 vs. 282354, -0.448%). Modest in
// magnitude, but the first real progress after four straight
// negative/mixed generations, and it directly validates that
// negotiation -- not just topology -- was genuinely withheld from this
// small, concentrated, disproportionately-congested population. See
// `HISTORY_COST_SWEEP.md` Part 36 for the full record. Two directions
// are already scoped as immediate next steps: slow-tier (MSMV) eligibility
// for giant nets (this generation still withholds it), and a
// congestion-aware corner choice inside `routeGiantNet`'s own initial
// commit (cheap, compounds with this fix).
//
// --- nv2route.cpp's own header material below, inherited unchanged as
// accurate history of the mechanisms this generation builds on. ---
//
// Why: nv1route's own real newblue1 result (46908 -- a real, large
// improvement over fr7route's 79420, but still +75.2% over fr5route's
// committed 26776) prompted a knob sweep (patience, round budget, history
// growth rate) that found only sub-1% further gains -- the same
// diminishing-returns shape this project's own vroute header already names
// for coefficient tuning generally. Rather than keep tuning nv1route's
// existing knobs, a read-only diagnostic (not part of the repo) measured
// *where* the remaining real overflow on that exact output actually comes
// from, comparing real per-layer overflow against what the 2D pricing
// stage's own summed-across-layers metric sees at the same grid positions:
//
//   real per-layer overflow:             46908  (matches gr_verify exactly)
//   2D-combined (summed-across-layers):  18156  (matches nv1route's own
//                                                reported "2D overflow"
//                                                exactly)
//
// Of the 46908, **27002 (57.6%) sits at (x,y) positions where the
// aggregate capacity across all 6 real layers was actually sufficient** --
// a pure per-layer *distribution* problem, structurally invisible to the
// 2D pricing stage's own metric (it only ever sees the summed total, by
// construction, since it prices a 2D-collapsed grid -- see route()'s own
// header comment). The remaining 19906 (42.4%) sits at positions with
// genuine aggregate shortage, closely matching the 2D stage's own 18156
// (confirming that smaller slice is already correctly identified there).
//
// This matters because nv1route's own negotiateViaAssignment already has
// every piece of infrastructure needed to negotiate real wire congestion --
// charge logs, a round loop, supervised accept/revert, and (after this
// file's own predecessor's metric fix) real-overflow-driven bestRound
// tracking -- it just gives the DP no *memory* of wire congestion, only of
// via congestion (historyVia). layerAssignDP's own wireCostForEdge reacts
// only to the current round's usage/capacity ratio: exactly the single-
// round-only limitation this project's zero-overflow mandate already
// diagnosed and fixed once before (hroute -> iroute/kroute), just never
// applied to *wire* congestion inside the layer-assignment stage
// specifically. Retuning the 2D pricing stage itself was considered and
// explicitly deferred: it is a bigger, previously-untried change (no prior
// generation has ever fed real per-layer capacity back into it), and could
// in any case only ever reach the smaller 42.4% aggregate-shortage slice.
//
// The new mechanism: historyWire, a direct port of historyVia's own
// mechanism (itself ported from kroute.cpp -- see nv1route.cpp's header)
// to real per-layer wire edges instead of via sites:
//   - WirePrices (hist/streak/histOps, sized g.hCount()/g.vCount()) and
//     updateWireHistory -- verbatim port of updateViaHistory's own
//     streak-gated, LIFO-relaxable formula, keyed by real wire-edge index.
//     No std::max(1, ...) clamp on the trigger condition (usage > cap) --
//     the same lesson nv1route's own metric fix already established: a
//     genuinely zero-capacity edge with any usage is a real, full-
//     magnitude overflow, not one clamped short. The clamp still appears,
//     correctly, wherever a capacity is used as a *divisor* (the
//     scaleByOverflow ratio term) -- the same distinction that fix drew
//     between a cost denominator and an overflow computation.
//   - wireCostForEdge multiplies its existing (unchanged) usage/capacity
//     ratio by the *maximum* historyWireH/V value found among the unit
//     tile-edges the leg spans -- not a per-tile sum-of-ratios, which
//     would change behavior even at history=1.0 whenever capacity varies
//     along a span. The max-over-span choice is exactly backward
//     compatible (nullptr, every pre-existing call site, reproduces
//     nv1route's own formula unchanged) and means a whole leg is
//     discouraged from a layer if *any* tile-edge along its span has grown
//     historically expensive.
//   - negotiateViaAssignment gains a second WirePrices instance alongside
//     its existing ViaPrices, and calls updateWireHistory at the same
//     round boundaries updateViaHistory already fires at. Candidate
//     selection (netOverflowedVia, already checking wire overflow) and the
//     accept/revert judgment (localOverflow, already wire-only per the
//     prior generation's own metric fix) needed **no changes at all** --
//     they already select and judge by exactly the resource historyWire
//     now gives the DP genuine memory of. historyVia and historyWire stay
//     cleanly separated: via history only affects spanCost, wire history
//     only affects wireCostForEdge, no mixing.
//   - Both history mechanisms' own knob sets (8 each) are bundled into one
//     HistKnobs struct per mechanism, passed by value, to keep
//     negotiateViaAssignment's own signature from growing past 20 raw
//     parameters -- every pre-existing self-test call site already used
//     all-default trailing arguments, so this is a safe, contained change.
//
// Known risks, disclosed up front (see the plan this was scoped from for
// the full discussion):
//   - The distribution-only gap being theoretically reachable doesn't
//     guarantee this specific mechanism reaches all of it -- a max-over-
//     span multiplicative term discourages a whole leg, not a sub-span,
//     from a hot layer, and could still leave some of the 27002 units
//     unaddressed or trade overflow at one location for overflow at
//     another (the same interaction-effect risk nv1route's own best-round
//     rollback guards against at the round level, not perfectly at the
//     per-net level within a round).
//   - Two independent history mechanisms (via, wire) now compound,
//     tuned/validated in isolation from each other -- whether they
//     interact well together is untested until the real-benchmark run.
//   - This still cannot reach the 42.4% genuine-aggregate-shortage slice
//     -- if the real result still falls well short of fr5route, that
//     remaining gap is the strongest evidence yet that the 2D-pricing-loop
//     retune (deferred, not attempted here) is the next place to look, not
//     further tuning of this mechanism.
//
// Real result: the first unanimous win over fr5route in this project's
// entire via-cost-aware exploration. Screened on newblue1 alone first
// (default NV2ROUTE_VIA_ROUNDS=60): 22118, already -17.4% vs fr5route's
// own committed 26776 -- the first generation in the fr6route/fr7route/
// nv1route/nv2route lineage to beat it at all. A round-cap sweep followed
// (60 -> 200 -> 400, on newblue1 alone, then all three real benchmarks at
// 200 and 400) to check how much of that was still-improving vs.
// converged: 60->200 bought another -1.1% (22118 -> 21904), 200->400
// bought only -0.37% more (21904 -> 21824) -- real, but sharply
// diminishing, the same shape as the discredited coefficient-tuning
// sweeps this generation was built specifically to move past. Full
// three-benchmark result at NV2ROUTE_VIA_ROUNDS=200/400 (essentially
// equivalent; 200 is nearly as good and roughly half the runtime), all
// legal per `gr_verify` (0 malformed/out-of-bounds/disconnected/
// incomplete/unrouted) and cross-checked exactly against `eval2008.pl`
// on every benchmark:
//
//   benchmark   fr5route (real)   nv2route (real, r=400)   delta
//   adaptec1    93764             75028                     -19.98%
//   newblue1    26776             21824                     -18.49%
//   bigblue1    191976            184922                     -3.67%
//
// **Decision: `gr/nv2route.cpp` is this project's new best real result,
// replacing `gr/fr5route.cpp`.** Unanimous, verified, non-trivial on two
// of three benchmarks; bigblue1's own margin is real but much thinner,
// and none of the three had fully converged even at 400 rounds (all
// still finding new bests on the final rounds) -- diminishing, not
// exhausted. `NV2ROUTE_VIA_ROUNDS=200` (default 60; not the file's own
// implicit default) is the recommended operating point: captures
// essentially all of the measured gain for about half the runtime of
// 400. See `HISTORY_COST_SWEEP.md` Part 30 for the full record,
// including the round-cap sweep's own numbers on all three benchmarks
// and the historical context (five prior generations -- fr6route,
// fr7route, and nv1route's own three knob variants -- all failed to
// beat fr5route before this one did).

// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o nv3route nv3route.cpp
//
// Use:
//     ./nv3route benchmark.gr solution.route
//     ./nv3route --selftest
//
// Environment variables (all optional; every VROUTE_*/KROUTE_*/FR1ROUTE_*/
// FR3ROUTE_*/FR4ROUTE_*/NV2ROUTE_* var below behaves exactly as in
// nv2route.cpp and its own ancestors, since the 2D collapse, MSMV,
// phases, history, and topology construction are unchanged):
//     NV3ROUTE_GIANT_FAST_TIER=0|1  this file's own new mechanism,
//                                 default 1 (enabled) -- 0 excludes giant
//                                 nets from the fast tier's own per-leg
//                                 reroute, reproducing nv2route's exact
//                                 output for direct comparison, the same
//                                 toggle-reproduces-the-parent pattern
//                                 every generation in this lineage uses.
//     NV2ROUTE_VIA_AWARE=0|1       carried over from NV1ROUTE_VIA_AWARE,
//                                 default 1 (enabled) -- 0 bypasses the
//                                 whole negotiation loop and reproduces
//                                 fr5route's exact independent-greedy-
//                                 plus-stitching behavior for direct
//                                 comparison.
//     NV2ROUTE_VIA_WEIGHT=<float>  carried over unchanged from
//                                 NV1ROUTE_VIA_WEIGHT, scales the DP's
//                                 spanCost term relative to wire cost,
//                                 default 1.0.
//     NV2ROUTE_HIST_STEP=<float>, NV2ROUTE_HIST_MODE=add|mult|ratio,
//     NV2ROUTE_HIST_FACTOR=<float>, NV2ROUTE_HIST_STREAK=<int>,
//     NV2ROUTE_HIST_SCALE=flat|overflow, NV2ROUTE_HIST_RELAX=0|1,
//     NV2ROUTE_HIST_RATIO_EXPONENT=<float>,
//     NV2ROUTE_HIST_EXPONENT_ESCALATION=<float>
//                                 the historyVia mechanism's own knobs,
//                                 carried over unchanged in role from
//                                 NV1ROUTE_HIST_*, renamed for this file's
//                                 own functional reads.
//     NV2ROUTE_VIA_ROUNDS=<int>    round cap for the negotiation loop,
//                                 default 60. Carried over from
//                                 NV1ROUTE_VIA_ROUNDS.
//     NV2ROUTE_VIA_PATIENCE=<int>  stop after this many rounds with no
//                                 improvement in best-known total overflow,
//                                 default 20. Carried over from
//                                 NV1ROUTE_VIA_PATIENCE.
//     NV2ROUTE_WIRE_AWARE=0|1      this file's own new mechanism, default 1
//                                 (enabled) -- 0 passes nullptr for both
//                                 historyWire pointers, reproducing
//                                 nv1route's exact output for direct
//                                 comparison, the same toggle-reproduces-
//                                 the-parent pattern every generation in
//                                 this lineage uses.
//     NV2ROUTE_WIRE_HIST_STEP=<float>, NV2ROUTE_WIRE_HIST_MODE=add|mult|ratio,
//     NV2ROUTE_WIRE_HIST_FACTOR=<float>, NV2ROUTE_WIRE_HIST_STREAK=<int>,
//     NV2ROUTE_WIRE_HIST_SCALE=flat|overflow, NV2ROUTE_WIRE_HIST_RELAX=0|1,
//     NV2ROUTE_WIRE_HIST_RATIO_EXPONENT=<float>,
//     NV2ROUTE_WIRE_HIST_EXPONENT_ESCALATION=<float>
//                                 this file's own new historyWire
//                                 mechanism's own knob set, same roles as
//                                 the via ones above, independent WirePrices
//                                 instance.
//     NV2ROUTE_TRACE=1             print each round's overflow and
//                                 accepted/rejected counts to stderr.
//     FR1ROUTE_TWOD_ONLY=1         skips negotiateViaAssignment() entirely,
//                                 reproducing fr4route's exact
//                                 single-layer, zero-via output.
//     FR4ROUTE_EXACT_STEINER=0|1  default 1. See fr4route.cpp's header.
//     FR4ROUTE_EXACT_MAX_PINS=<int>
//                                 default 8. See fr4route.cpp's header.
//     FR3ROUTE_STEINERIZE=0|1     default 1. See fr3route.cpp's header.
//     VROUTE_MSMV=0|1              default 1. See fr1route.cpp's header.
//     VROUTE_EPS=<float>          fast-tier step size epsilon, default 0.02
//     VROUTE_THRESHOLD=<float>    fast-tier early-warning curve start,
//                                 default 0.05 (nv3route's default was 0.2;
//                                 see this file's own header for why it
//                                 changed)
//     VROUTE_POWER=<float>        fast-tier curve escalation power,
//                                 default 1.5 (nv3route's default was 2.5)
//     VROUTE_FAST_ROUNDS=<int>    max phase-A (fast tier) rounds, default 60
//     VROUTE_SLOW_ROUNDS=<int>    max phase-B (slow tier) rounds, default
//                                 150
//     VROUTE_CLEANUP_THRESHOLD=<float>, VROUTE_CLEANUP_PASSES=<int>
//                                 phase A's cleanup pass (defaults 0.5 / 30)
//     VROUTE_MAX_MARGIN=<int>     slow-tier Dijkstra search margin cap,
//                                 default 6
//     VROUTE_TRACE=1              print each round's overflow, the cleanup
//                                 pass's before/after, and phase B's
//                                 accepted/rejected counts, to stderr
//     VROUTE_HIST_STEP=<float>, VROUTE_HIST_MODE=add|mult|ratio,
//     VROUTE_HIST_FACTOR=<float>, VROUTE_HIST_STREAK=<int>,
//     VROUTE_HIST_SCALE=flat|overflow, VROUTE_HIST_RELAX=0|1,
//     VROUTE_HIST_RATIO_EXPONENT=<float>, VROUTE_HIST_EXPONENT_ESCALATION=<float>
//                                 kroute's own history mechanism, unchanged
//                                 -- see kroute.cpp's own header for the
//                                 full derivation of each.
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <tuple>
#include <cstdio>
#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <unordered_map>
#include <vector>

#include "gr_io.h"
#include "gpu/pattern_kernel.h"

using gr_io::Design;
using gr_io::fail;
using gr_io::Net;
using gr_io::Pin;
using gr_io::readGr;
using gr_io::RoutingGrid;

constexpr size_t kGiantNetPins = 1000;
constexpr double kForbiddenPrice = 1e9;

// ---------------------------------------------------------------------------
// Shared: prices, usage, layer plan, cost curve -- identical to nproute's
// and mroute's (which are identical to each other; mroute inherited this
// from nproute unchanged).
// ---------------------------------------------------------------------------

// `histH`/`histV` are the new permanent term this file adds on top of
// hroute's `h`/`v` (lambda, the reactive current-round price): initialized
// to 1.0 (a no-op multiplier) and only ever grown by `updateHistory`, never
// read or written by the per-net-commit code path lambda uses. Keeping them
// in the same struct rather than a parallel one is deliberate -- every call
// site that prices an edge (bestHLayer, bestVLayer, boundedDijkstra) needs
// both terms together, and threading two structs through the same call
// chains that already thread one is a needless chance to pass the wrong
// pair.
// `streakH`/`streakV` count consecutive round-boundary over-capacity
// observations per edge -- reset to 0 the instant an edge reads clear,
// incremented (never decremented, unlike history itself which never
// decreases even across a clear round) whenever it's still over. Added to
// support VROUTE_HIST_STREAK, which gates history growth behind N
// consecutive over-capacity rounds instead of every single one -- see the
// file header's "Growth trigger" section for why this was worth trying:
// the default (N=1) reproduces every prior call site's behavior exactly.
// A single history-growth operation applied to one edge on one round,
// recorded so it can be undone exactly (not just decayed approximately) on
// a later clean round. `multiplicative` distinguishes an additive delta
// (undo: subtract) from a multiplicative factor (undo: divide) -- kroute's
// three inherited growth modes (add/mult/ratio) map onto exactly these two
// undo operations, so one struct covers all of them.
struct HistOp {
  double amountOrFactor;
  bool multiplicative;
};

// ---------------------------------------------------------------------------
// This file's own new mechanism: collapse the 3D grid to a single 2D plane
// with summed cross-layer capacity, per HISTORY_COST_SWEEP.md Part 9 and
// the ISPD-competitive routers' own architecture (FastRoute, NTHU-Route
// 2.0, NCTU-GR 2.0: "compress the grid graph to an overflow-free 2D
// solution, then assign layers"). `h2dCount`/`v2dCount` size the 2D edge
// arrays; `computeCapH2d`/`computeCapV2d` sum every real layer's capacity
// at a given (x,y) into that one 2D slot. A subtle but load-bearing
// identity used throughout the rest of this file: `g.hIndex(x, y, 0)`
// (layer 0's own 3D index formula, `(0*ny+y)*(nx-1)+x`) already reduces to
// exactly `y*(nx-1)+x` -- the correct 2D index -- so every existing
// `g.hIndex(x, y, l)`/`g.vIndex(x, y, l)` call site in the code inherited
// from vroute works unmodified as long as `l` is always 0, which this
// file's own edits (LayerPlan, boundedDijkstra, multiSourceDijkstra)
// guarantee. No separate 2D index function is introduced; reusing layer
// 0's own formula avoids a second parallel indexing scheme to keep in
// sync with the first.
// ---------------------------------------------------------------------------

size_t h2dCount(const RoutingGrid &g) {
  return static_cast<size_t>(g.ny) * static_cast<size_t>(std::max(g.nx - 1, 0));
}
size_t v2dCount(const RoutingGrid &g) {
  return static_cast<size_t>(std::max(g.ny - 1, 0)) * static_cast<size_t>(g.nx);
}
std::vector<int> computeCapH2d(const RoutingGrid &g) {
  std::vector<int> cap(h2dCount(g), 0);
  for (int l = 0; l < g.nLayers; ++l)
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        cap[g.hIndex(x, y, 0)] += std::max(0, g.hcap[g.hIndex(x, y, l)]);
  return cap;
}
std::vector<int> computeCapV2d(const RoutingGrid &g) {
  std::vector<int> cap(v2dCount(g), 0);
  for (int l = 0; l < g.nLayers; ++l)
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        cap[g.vIndex(x, y, 0)] += std::max(0, g.vcap[g.vIndex(x, y, l)]);
  return cap;
}

struct Usage {
  std::vector<int> h, v;
  // This file's own new mechanism: 2D-collapsed capacity, computed once
  // per Usage construction and carried alongside live usage so every
  // function that already threads a `Usage` reference (negotiateWire,
  // updateHistory, totalOverflow, netOverflowed, overflowOverUnion) has
  // capacity available without a second grid-like parameter.
  std::vector<int> capH, capV;
  explicit Usage(const RoutingGrid &g)
      : h(h2dCount(g), 0), v(v2dCount(g), 0), capH(computeCapH2d(g)), capV(computeCapV2d(g)) {}
};

// This file's own mechanism: which pricing model every 2D wire-edge cost
// reads (see the file header). kLambda is the inherited nv11route model,
// byte for byte; kPresentDemand is NTHU-Route 2.0's.
enum class CostModel { kLambda, kPresentDemand, kRefine };

struct CostSettings {
  CostModel model = CostModel::kLambda; // route()'s default; main() defaults to pd
  double wlFloor = 0.0;                 // PD1ROUTE_WL_FLOOR: WL term = max(factor, floor)
};

// NTHU-Route 2.0's per-iteration wirelength weight (Construct_2d_tree.cpp):
// 1 - exp(-5 exp(-0.1 iter)). ~0.989 at iter 1, ~0.49 at 20, ~0.033 at 50.
double pdFactorForIter(int iter) { return 1.0 - std::exp(-5.0 * std::exp(-0.1 * iter)); }

struct Prices {
  std::vector<double> h, v;
  std::vector<double> histH, histV;
  std::vector<int> streakH, streakV;
  // Per-edge LIFO stack of every still-"active" growth operation -- see
  // this file's header for why a stack (not just a running total) is
  // required: VROUTE_HIST_RELAX pops and undoes exactly one entry, in
  // reverse order, on each clean round, so an edge dirty for N rounds
  // needs exactly N consecutive clean rounds to fully return to baseline.
  // Empty for almost every edge (only edges that have ever gone over
  // capacity accumulate entries), so the memory cost is proportional to
  // actual congestion, not grid size.
  std::vector<std::vector<HistOp>> histOpsH, histOpsV;
  explicit Prices(const RoutingGrid &g)
      : h(h2dCount(g), 1.0), v(v2dCount(g), 1.0), histH(h2dCount(g), 1.0), histV(v2dCount(g), 1.0),
        streakH(h2dCount(g), 0), streakV(v2dCount(g), 0), histOpsH(h2dCount(g)),
        histOpsV(v2dCount(g)) {}

  // This file's own mechanism: present-demand pricing state. Unused (and
  // left empty) under kLambda. `live` points at route()'s one Usage object,
  // so every cost read sees demand as of this instant -- a net ripped off
  // an edge makes it cheaper at once, no price to decay. pdHist counts the
  // round boundaries at which the edge was overflowed, starting at 1
  // (NTHU's Edge_2d::history). Invariant: pdHist <= pdIter, because each
  // round boundary raises pdHist by at most 1 and pdIter by exactly 1 --
  // it is what keeps pdShrink above 1/3 (see presentDemandCost).
  CostModel model = CostModel::kLambda;
  const Usage *live = nullptr;
  std::vector<int> pdHistH, pdHistV;
  int pdIter = 1;
  double pdFactor = pdFactorForIter(1);
  double pdWlFloor = 0.0;
  // Self-test hook only: >= 0 replaces heuristicScale()'s answer, so a
  // test can show what an inadmissible (unscaled) heuristic does.
  double heuristicScaleOverride = -1.0;
  // pd22route: virtual capacity. When set (the region stage only), the
  // present-demand cost prices edge e against virtualCap(cap, red[e]).
  const std::vector<int> *vredH = nullptr, *vredV = nullptr;
  // pd29route: extra cost of an edge one more net would overflow (region stage).
  double overPenalty = 0;
};

// `threshold` is a parameter, not a fixed static lookup, specifically so
// the cleanup pass can price nets against a more relaxed curve than normal
// routing uses -- matching nproute.cpp's actual current architecture
// (which grew this parameter to support round-scheduling experiments that
// didn't pan out, but the parameter itself is exactly what a relaxed
// cleanup pass needs).
// Defaults changed from nv3route's 2.5/0.2 to 1.5/0.05 -- this file's own
// joint-mechanism result (header below): retuning these two coefficients
// on top of nv3route's already-committed giant-fast-tier mechanism, not
// merely reproducing nv2route's Part 33 coefficient sweep (which never
// coexisted with the giant-fast-tier win and reversed to net-zero at the
// real round budget). Both values are still overridable via VROUTE_POWER/
// VROUTE_THRESHOLD for further tuning; VROUTE_POWER=2.5 VROUTE_THRESHOLD=0.2
// reproduces nv3route's own exact curve.
double priceExponentTerm(double usage, double cap, double threshold) {
  static const double power = [] {
    const char *p = std::getenv("VROUTE_POWER");
    return p ? std::atof(p) : 1.5;
  }();
  if (threshold >= 1.0)
    return std::max(0.0, usage / cap - 1.0);
  double ratio = usage / cap;
  double r = std::max(0.0, (ratio - threshold) / (1.0 - threshold));
  return std::pow(r, power);
}

double defaultThreshold() {
  static const double threshold = [] {
    const char *t = std::getenv("VROUTE_THRESHOLD");
    return t ? std::atof(t) : 0.05;
  }();
  return threshold;
}

// `hist` multiplies the whole present-congestion cost (base wirelength
// included), not just the lambda*amount term -- see the file header for
// why: an edge with a permanent history penalty needs to look expensive
// to pass through at all, not merely more expensive to add usage to,
// otherwise a net with slack elsewhere still has no reason to avoid a
// zero-marginal-cost pass through a historically bad edge. `hist` is 1.0
// (a no-op) for every edge that has never sat over capacity at a round
// boundary, so this is exactly hroute's edgeCost when history is unused.
double edgeCost(double lambda, double hist, int amount) { return hist * (1.0 + lambda * amount); }

// This file's own mechanism: NTHU-Route 2.0's edge cost
// (Congestion.cpp, pre_evaluate_congestion_cost_all, HISTORY_COST branch):
//   cong = (demand + inc) / (cap * (1 - (hist - 1) / (iter * (1.5 + 3 factor))))
//   cost = WL + hist * cong^5
// `inc` is one more net: `amount` (width + spacing) in this project's raw
// capacity units, where NTHU counts nets against cap/2 tracks -- the same
// ratio. The shrink term is NTHU's overflow prediction: an edge overflowed
// for many iterations looks fuller than it is. With pdHist <= pdIter (see
// Prices) the shrink is > 1 - 1/1.5 = 1/3, so kPdMinShrink never binds in a
// real run; it is there so a broken invariant degrades to a large finite
// cost instead of a division by zero or a negative "congestion". Zero or
// negative capacity (a .gr reduced-capacity adjustment) -> kForbiddenPrice,
// the same deterrent negotiateWire gives such an edge.
constexpr double kPdMinShrink = 1.0 / 3.0;

double pdShrink(int hist, int iter, double factor) {
  double s = 1.0 - static_cast<double>(hist - 1) /
                       (static_cast<double>(std::max(1, iter)) * (1.5 + 3.0 * factor));
  return std::max(kPdMinShrink, s);
}

double presentDemandCost(long long demand, int cap, int amount, int hist, int iter, double factor,
                         double wl) {
  if (cap <= 0)
    return kForbiddenPrice;
  double cong = static_cast<double>(demand + amount) /
                (static_cast<double>(cap) * pdShrink(hist, iter, factor));
  // pd2route: cong^5 by multiplication. pd1route called std::pow here, in
  // every edge relaxation; its screens ran 3.2-3.8x slower than lambda
  // pricing (HISTORY_COST_SWEEP.md Part 59). Agrees with std::pow to a few
  // ulps (testPdCongPowerByMultiplicationMatchesPow), so pd results differ
  // from pd1route's only in the last bits.
  double c2 = cong * cong;
  return std::min(kForbiddenPrice, wl + hist * (c2 * c2 * cong));
}

// The WL term: NTHU sets WL_Cost = factor each iteration. PD1ROUTE_WL_FLOOR
// (default 0 = NTHU exactly) keeps it from decaying below a floor.
double pdWl(const Prices &pr) { return std::max(pr.pdFactor, pr.pdWlFloor); }

// The single seam every 2D wire-edge cost goes through (fast-tier layer
// choice, both slow-tier searches).
// pd4route: NTHU-Route 2.0's refinement cost (Congestion.cpp, the
// MADEOF_COST branch; Post_processing.cpp): an edge that is already full --
// one more net would overflow it -- costs 1, any other edge costs nothing.
// Here "nothing" is kRefineEps, a length tie-break (NTHU breaks ties by
// full-edge distance, then bends) small enough that no path inside any
// search box can trade a full edge for length: 1e-5 * (324 * 324 tiles) ~ 1.
// Zero or negative capacity counts as full.
constexpr double kRefineEps = 1e-5;
double refineCost(long long demand, int cap, int amount) {
  return (cap <= 0 || demand + amount > cap) ? 1.0 : kRefineEps;
}

// pd22route: the capacity the present-demand cost prices against when
// virtual capacity is on: `red` capacity units less, but never below half
// the real capacity (rounded up), so a reduced edge is dearer, never
// forbidden. Zero or negative capacity is returned unchanged (it stays
// kForbiddenPrice in presentDemandCost).
int virtualCap(int cap, int red) {
  if (cap <= 0 || red <= 0)
    return cap;
  return std::max(cap - red, (cap + 1) / 2);
}

double wireCostH(const Prices &pr, size_t e, int amount) {
  if (pr.model == CostModel::kRefine)
    return refineCost(pr.live->h[e], pr.live->capH[e], amount);
  if (pr.model == CostModel::kLambda)
    return edgeCost(pr.h[e], pr.histH[e], amount);
  int cap = pr.vredH ? virtualCap(pr.live->capH[e], (*pr.vredH)[e]) : pr.live->capH[e];
  double c = presentDemandCost(pr.live->h[e], cap, amount, pr.pdHistH[e], pr.pdIter, pr.pdFactor, pdWl(pr));
  if (pr.overPenalty > 0 && pr.live->h[e] + amount > pr.live->capH[e])
    c = std::min(kForbiddenPrice, c + pr.overPenalty);
  return c;
}
double wireCostV(const Prices &pr, size_t e, int amount) {
  if (pr.model == CostModel::kRefine)
    return refineCost(pr.live->v[e], pr.live->capV[e], amount);
  if (pr.model == CostModel::kLambda)
    return edgeCost(pr.v[e], pr.histV[e], amount);
  int cap = pr.vredV ? virtualCap(pr.live->capV[e], (*pr.vredV)[e]) : pr.live->capV[e];
  double c = presentDemandCost(pr.live->v[e], cap, amount, pr.pdHistV[e], pr.pdIter, pr.pdFactor, pdWl(pr));
  if (pr.overPenalty > 0 && pr.live->v[e] + amount > pr.live->capV[e])
    c = std::min(kForbiddenPrice, c + pr.overPenalty);
  return c;
}

// A* multiplies its Manhattan heuristic by a lower bound on any one edge's
// cost, which keeps it admissible and consistent. kLambda: every edge costs
// hist * (1 + lambda * amount) >= 1, so 1.0 -- exactly the inherited
// heuristic (x * 1.0 == x in IEEE arithmetic, so byte parity holds).
// kPresentDemand: cost = wl + hist * cong^5 > wl (hist >= 1, cong > 0), and
// kForbiddenPrice >= wl, so the WL term. It decays toward 0 as the factor
// does, and A* degrades toward plain Dijkstra with it -- the runtime price
// of NTHU's schedule, measured rather than assumed away.
double heuristicScale(const Prices &pr) {
  if (pr.heuristicScaleOverride >= 0.0)
    return pr.heuristicScaleOverride;
  if (pr.model == CostModel::kRefine)
    return kRefineEps; // every refinement edge costs >= kRefineEps
  return pr.model == CostModel::kLambda ? 1.0 : pdWl(pr);
}

void configureCostModel(Prices &pr, const Usage &u, const CostSettings &cs) {
  pr.model = cs.model;
  pr.pdWlFloor = cs.wlFloor;
  if (cs.model == CostModel::kPresentDemand) {
    pr.live = &u;
    pr.pdHistH.assign(u.h.size(), 1);
    pr.pdHistV.assign(u.v.size(), 1);
    pr.pdIter = 1;
    pr.pdFactor = pdFactorForIter(1);
  }
}

// Round boundary under kPresentDemand (called right after updateHistory at
// all three of route()'s round boundaries; a no-op under kLambda): every
// edge overflowed in the settled state gains 1 history, then the iteration
// counter and its WL factor advance. NTHU's pre_evaluate_congestion_cost
// does the same at the start of each iteration.
void advancePresentDemand(Prices &pr, const Usage &u) {
  if (pr.model != CostModel::kPresentDemand)
    return;
  for (size_t i = 0; i < u.h.size(); ++i)
    if (u.h[i] > u.capH[i])
      ++pr.pdHistH[i];
  for (size_t i = 0; i < u.v.size(); ++i)
    if (u.v[i] > u.capV[i])
      ++pr.pdHistV[i];
  ++pr.pdIter;
  pr.pdFactor = pdFactorForIter(pr.pdIter);
}

// Parses PD1ROUTE_COST (pd | lambda; unset = pd) and PD1ROUTE_WL_FLOOR (a
// finite number >= 0; unset = 0). Fails loudly on anything else.
bool parseCostSettings(const char *model, const char *floor, CostSettings &out, std::string &err) {
  CostSettings c;
  c.model = CostModel::kPresentDemand;
  if (model) {
    std::string m(model);
    if (m == "pd")
      c.model = CostModel::kPresentDemand;
    else if (m == "lambda")
      c.model = CostModel::kLambda;
    else {
      err = "PD1ROUTE_COST must be pd or lambda (got '" + m + "')";
      return false;
    }
  }
  if (floor) {
    char *end = nullptr;
    double f = std::strtod(floor, &end);
    if (end == floor || *end != '\0' || !std::isfinite(f) || f < 0.0) {
      err = std::string("PD1ROUTE_WL_FLOOR must be a finite number >= 0 (got '") + floor + "')";
      return false;
    }
    c.wlFloor = f;
  }
  out = c;
  return true;
}

// The permanent term itself. `usage` must be the settled, whole-grid usage
// snapshot at a round boundary -- never a mid-round, single-net delta, or
// this stops meaning "has this edge been over capacity for a while" and
// starts meaning something round-schedule-dependent and noisy. Called once
// per round (see the three call sites in route()): after phase A's fast
// tier, after the cleanup pass, and after each phase B slow-tier round.
//
// Zero (or negative, from a malformed .gr adjustment) capacity edges are
// skipped outright, same as negotiateWire's own kForbiddenPrice branch --
// they are already maximally deterred, and "usage_e / capacity_e" is
// exactly the kind of division CLAUDE.md's arithmetic-edge-case section
// warns about when capacity_e == 0. There is no division here (a plain
// usage > capacity comparison), but the *reason* to skip is the same one:
// nothing meaningful to accumulate against an edge that was never usable
// in the first place. See testHistoryIgnoresZeroCapacityEdges.
enum class HistMode { kAdditive, kMultiplicative, kRatio };

constexpr double kHistoryCap = 1e6; // defensive ceiling; never reached in
                                     // practice at realistic round counts,
                                     // but a formula that can only grow
                                     // needs a stated bound per this
                                     // project's own arithmetic-edge-case
                                     // discipline, the same reasoning
                                     // negotiateWire's kForbiddenPrice
                                     // clamp already applies to lambda.

// `streakThreshold` (VROUTE_HIST_STREAK, default 1): an edge must be over
// capacity for this many *consecutive* round boundaries before growth is
// applied at all -- not merely "was over capacity this round," which is
// what every call site did before this parameter existed. Threshold 1
// reproduces that exact prior behavior (every over-capacity round grows
// immediately), so this is a strict generalization, never a behavior
// change at its default. Below threshold, the streak counter still
// advances -- it just doesn't unlock growth yet.
//
// `scaleByOverflow` (VROUTE_HIST_SCALE=overflow): instead of a flat step
// (or a flat factor) applied uniformly to every over-capacity edge
// regardless of how far over it is, scale by the current round's
// usage/capacity ratio -- an edge at 3x capacity accumulates history three
// times as fast as one just barely over. This is the one asymmetry between
// history's update and lambda's own: lambda already scales with
// usage/capacity via priceExponentTerm; history previously did not. Ratio
// is computed from THIS round's usage even when growth is gated by a
// multi-round streak, matching "how bad is it right now that finally
// triggered growth," not an average over the streak.
// `relaxEnabled` (VROUTE_HIST_RELAX, default true): when an edge is over
// capacity, the growth applied this round is pushed onto that edge's
// histOps stack as well as applied to histH -- exactly what was applied,
// not the nominal step/factor, so a clamp against kHistoryCap still undoes
// cleanly. When an edge is *not* over capacity and its stack is non-empty,
// the single most-recently-pushed operation is popped and undone (additive:
// subtract; multiplicative: divide), clamped at the 1.0 floor as a defensive
// guard against floating-point drift after many push/pop cycles -- by
// construction this should never actually bind (each push and its eventual
// pop are exact inverses), but the guard costs nothing and this project's
// own arithmetic-edge-case discipline (CLAUDE.md) asks for a stated bound on
// any quantity that changes over many rounds. `relaxEnabled=false`
// reproduces iroute's exact permanent, non-decreasing behavior.
void updateHistory(const RoutingGrid &g, Prices &pr, const Usage &u, double histStep,
                    HistMode mode, double histFactor, int streakThreshold = 1,
                    bool scaleByOverflow = false, bool relaxEnabled = true,
                    double ratioExponent = 1.0, double exponentEscalation = 0.0) {
  // This file's own new mechanism moved capacity into Usage (u.capH/capV)
  // -- g is kept in the signature for call-site consistency with the rest
  // of the inherited pricing loop, but no longer read here.
  (void)g;
  for (size_t i = 0; i < u.h.size(); ++i) {
    if (u.capH[i] <= 0)
      continue;
    if (u.h[i] > u.capH[i]) {
      ++pr.streakH[i];
      if (pr.streakH[i] < streakThreshold)
        continue;
      double ratio = scaleByOverflow ? static_cast<double>(u.h[i]) / u.capH[i] : 1.0;
      double before = pr.histH[i];
      if (mode == HistMode::kAdditive) {
        pr.histH[i] = std::min(kHistoryCap, pr.histH[i] + histStep * ratio);
        if (relaxEnabled)
          pr.histOpsH[i].push_back({pr.histH[i] - before, false});
      } else if (mode == HistMode::kMultiplicative) {
        pr.histH[i] = std::min(kHistoryCap, pr.histH[i] * (1.0 + (histFactor - 1.0) * ratio));
        if (relaxEnabled)
          pr.histOpsH[i].push_back({pr.histH[i] / before, true});
      } else {
        double effExponent =
            ratioExponent + exponentEscalation * static_cast<double>(pr.histOpsH[i].size());
        pr.histH[i] = std::min(
            kHistoryCap,
            pr.histH[i] * std::pow(static_cast<double>(u.h[i]) / u.capH[i], effExponent));
        if (relaxEnabled)
          pr.histOpsH[i].push_back({pr.histH[i] / before, true});
      }
    } else {
      pr.streakH[i] = 0;
      if (relaxEnabled && !pr.histOpsH[i].empty()) {
        HistOp op = pr.histOpsH[i].back();
        pr.histOpsH[i].pop_back();
        pr.histH[i] = op.multiplicative ? std::max(1.0, pr.histH[i] / op.amountOrFactor)
                                         : std::max(1.0, pr.histH[i] - op.amountOrFactor);
      }
    }
  }
  for (size_t i = 0; i < u.v.size(); ++i) {
    if (u.capV[i] <= 0)
      continue;
    if (u.v[i] > u.capV[i]) {
      ++pr.streakV[i];
      if (pr.streakV[i] < streakThreshold)
        continue;
      double ratio = scaleByOverflow ? static_cast<double>(u.v[i]) / u.capV[i] : 1.0;
      double before = pr.histV[i];
      if (mode == HistMode::kAdditive) {
        pr.histV[i] = std::min(kHistoryCap, pr.histV[i] + histStep * ratio);
        if (relaxEnabled)
          pr.histOpsV[i].push_back({pr.histV[i] - before, false});
      } else if (mode == HistMode::kMultiplicative) {
        pr.histV[i] = std::min(kHistoryCap, pr.histV[i] * (1.0 + (histFactor - 1.0) * ratio));
        if (relaxEnabled)
          pr.histOpsV[i].push_back({pr.histV[i] / before, true});
      } else {
        double effExponent =
            ratioExponent + exponentEscalation * static_cast<double>(pr.histOpsV[i].size());
        pr.histV[i] = std::min(
            kHistoryCap,
            pr.histV[i] * std::pow(static_cast<double>(u.v[i]) / u.capV[i], effExponent));
        if (relaxEnabled)
          pr.histOpsV[i].push_back({pr.histV[i] / before, true});
      }
    } else {
      pr.streakV[i] = 0;
      if (relaxEnabled && !pr.histOpsV[i].empty()) {
        HistOp op = pr.histOpsV[i].back();
        pr.histOpsV[i].pop_back();
        pr.histV[i] = op.multiplicative ? std::max(1.0, pr.histV[i] / op.amountOrFactor)
                                         : std::max(1.0, pr.histV[i] - op.amountOrFactor);
      }
    }
  }
}

// This file's own new mechanism: the 2D collapse means there is exactly
// one synthetic "layer" -- index 0 -- shared by both the H and V edge
// arrays (which are already separate, so no real ambiguity), standing in
// for "the 2D plane" rather than any real physical layer. Every call site
// that asks "is layer l horizontal/vertical" (bestHLayer/bestVLayer,
// boundedDijkstra, multiSourceDijkstra) is only ever asked about l == 0,
// and a 2D node must be able to relax in *both* directions from the same
// synthetic layer (unlike a real layer, which is exclusively one
// direction) -- hence isH/isV both always true, unconditionally, rather
// than looked up from vroute's own per-layer hCapLayer/vCapLayer scan.
struct LayerPlan {
  std::vector<int> hLayers{0}, vLayers{0};
  explicit LayerPlan(const RoutingGrid &) {}
  bool isH(int) const { return true; }
  bool isV(int) const { return true; }
};

struct Point {
  int x, y;
};

std::vector<std::pair<int, int>> mstEdges(const std::vector<Point> &pts) {
  size_t n = pts.size();
  std::vector<std::pair<int, int>> edges;
  if (n < 2)
    return edges;
  std::vector<bool> inTree(n, false);
  std::vector<int> bestDist(n, std::numeric_limits<int>::max());
  std::vector<int> bestFrom(n, -1);
  inTree[0] = true;
  for (size_t i = 1; i < n; ++i) {
    bestDist[i] = std::abs(pts[i].x - pts[0].x) + std::abs(pts[i].y - pts[0].y);
    bestFrom[i] = 0;
  }
  for (size_t step = 1; step < n; ++step) {
    int pick = -1;
    for (size_t i = 0; i < n; ++i)
      if (!inTree[i] && (pick < 0 || bestDist[i] < bestDist[pick]))
        pick = static_cast<int>(i);
    inTree[pick] = true;
    edges.emplace_back(bestFrom[pick], pick);
    for (size_t i = 0; i < n; ++i) {
      if (inTree[i])
        continue;
      int d = std::abs(pts[i].x - pts[pick].x) + std::abs(pts[i].y - pts[pick].y);
      if (d < bestDist[i]) {
        bestDist[i] = d;
        bestFrom[i] = pick;
      }
    }
  }
  return edges;
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: cheap, local Steinerization of the plain
// MST, applied once per net at construction time. See the file header
// for the full derivation (Ho-Vijayan-Wong style, scoped to degree-2
// nodes only, since the exact optimal Steiner point for exactly 3
// terminals is the well-known coordinate-wise median -- degree >= 3
// branch points would need the more complex Hanan-grid Steiner DP FLUTE
// actually solves, deliberately deferred).
// ---------------------------------------------------------------------------

int manhattan(const Point &a, const Point &b) {
  return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

// The exact rectilinear Steiner point for 3 terminals: the median of the
// three x-coordinates and the median of the three y-coordinates,
// independently -- a classical result (x and y are separable in
// Manhattan distance, so each axis's total span is minimized by its own
// median regardless of the other axis). A plain 3-element sort, not
// hand-branched comparisons, to keep this obviously correct.
int median3(int a, int b, int c) {
  int arr[3] = {a, b, c};
  std::sort(arr, arr + 3);
  return arr[1];
}

struct SteinerResult {
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> edges;
};

// For every degree-2 node i (tree-neighbors a, b): compute the median
// Steiner point S. If S is a genuinely new point and connecting all
// three of {i, a, b} through S has strictly less total Manhattan span
// than the plain MST's own two edges (i,a) and (i,b), replace those two
// edges with three new ones through a newly-appended point S. Every
// other node (degree 1 leaves, degree >= 3 branch points, or a degree-2
// node whose edges were already consumed by an earlier transformation in
// this same pass) is left exactly as the plain MST already had it --
// processing happens in a fixed order (point index), so two *adjacent*
// degree-2 nodes are Steinerized greedily front-to-back, not jointly
// optimized (a disclosed, deliberate simplification, not a bug).
SteinerResult steinerizeTree(const std::vector<Point> &pts,
                              const std::vector<std::pair<int, int>> &mst) {
  SteinerResult result;
  result.pts = pts;
  size_t n = pts.size();

  std::vector<std::vector<std::pair<int, int>>> adj(n); // node -> (neighbor, edge index)
  for (size_t e = 0; e < mst.size(); ++e) {
    int a = mst[e].first, b = mst[e].second;
    adj[a].push_back({b, static_cast<int>(e)});
    adj[b].push_back({a, static_cast<int>(e)});
  }

  std::vector<bool> edgeConsumed(mst.size(), false);
  std::vector<std::pair<int, int>> outEdges;

  for (size_t i = 0; i < n; ++i) {
    if (adj[i].size() != 2)
      continue; // only degree-2 nodes qualify -- see the function's own header
    int e0 = adj[i][0].second, e1 = adj[i][1].second;
    if (edgeConsumed[e0] || edgeConsumed[e1])
      continue; // an earlier node's transformation already claimed one of these edges
    int a = adj[i][0].first, b = adj[i][1].first;
    Point pi = pts[i], pa = pts[a], pb = pts[b];
    Point s{median3(pi.x, pa.x, pb.x), median3(pi.y, pa.y, pb.y)};
    if (s.x == pi.x && s.y == pi.y)
      continue; // the median coincides with the node itself -- nothing to gain
    int before = manhattan(pi, pa) + manhattan(pi, pb);
    int after = manhattan(pi, s) + manhattan(pa, s) + manhattan(pb, s);
    if (after >= before)
      continue; // not strictly better -- leave the plain MST edges alone

    edgeConsumed[e0] = true;
    edgeConsumed[e1] = true;
    int sIdx = static_cast<int>(result.pts.size());
    result.pts.push_back(s);
    outEdges.push_back({static_cast<int>(i), sIdx});
    outEdges.push_back({a, sIdx});
    outEdges.push_back({b, sIdx});
  }

  for (size_t e = 0; e < mst.size(); ++e)
    if (!edgeConsumed[e])
      outEdges.push_back(mst[e]);

  result.edges = std::move(outEdges);
  return result;
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: an exact rectilinear Steiner minimal tree
// (RSMT) via the classical Dreyfus-Wagner dynamic program over the Hanan
// grid. See the file header for the full derivation and the framing
// decision (why this, not literal FLUTE's own lookup tables). Applied to
// nets with unique terminal count in [3, FR4ROUTE_EXACT_MAX_PINS];
// steinerizeTree (above) remains the fallback for larger nets.
// ---------------------------------------------------------------------------

// Exact RSMT for a small terminal set `pts` (3 <= pts.size() <= ~8 in
// practice -- the DP is correct for any k, but cost is exponential in k).
// Returns the reconstructed tree, not just its cost: new Steiner points
// that don't coincide with an existing point get appended to result.pts,
// mirroring steinerizeTree's own append pattern.
SteinerResult exactSteinerTree(const std::vector<Point> &pts) {
  int k = static_cast<int>(pts.size());

  // Hanan grid: every (x, y) pairing one terminal's x with one terminal's
  // y -- k^2 candidate Steiner points. A classical result guarantees an
  // optimal RSMT is realizable using only these. Under Manhattan distance
  // this set is a complete graph, so no separate adjacency is needed.
  std::vector<Point> verts;
  verts.reserve(static_cast<size_t>(k) * static_cast<size_t>(k));
  for (const Point &px : pts)
    for (const Point &py : pts)
      verts.push_back({px.x, py.y});
  int V = static_cast<int>(verts.size());

  auto dist = [](const Point &a, const Point &b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
  };

  int numMasks = 1 << k;
  const long long kInf = std::numeric_limits<long long>::max() / 4;

  // dp[mask][v]: min length of a Steiner tree spanning terminal subset
  // `mask` and touching Hanan vertex v. parentSub[mask][v]: which submask
  // split produced the merge-step value at (mask, v) (0 for a singleton
  // mask, since it never merges). relayFrom[mask][v]: the vertex the
  // relax step routed through to reach v more cheaply than the raw merge
  // value (-1 if the merge value was never improved).
  std::vector<std::vector<long long>> dp(numMasks, std::vector<long long>(V, kInf));
  std::vector<std::vector<int>> parentSub(numMasks, std::vector<int>(V, -1));
  std::vector<std::vector<int>> relayFrom(numMasks, std::vector<int>(V, -1));

  for (int i = 0; i < k; ++i) {
    int mask = 1 << i;
    for (int v = 0; v < V; ++v)
      dp[mask][v] = dist(pts[i], verts[v]);
    // No relax pass for singletons: a direct Manhattan edge from a lone
    // terminal to v is already optimal -- no intermediate hop can beat it.
  }

  for (int mask = 3; mask < numMasks; ++mask) {
    if ((mask & (mask - 1)) == 0)
      continue; // singleton -- already seeded above

    // Merge: split mask's terminals into two nonempty parts that both
    // reach the same meeting point v.
    for (int v = 0; v < V; ++v) {
      long long best = kInf;
      int bestSub = -1;
      for (int sub = (mask - 1) & mask; sub > 0; sub = (sub - 1) & mask) {
        int comp = mask ^ sub;
        if (sub > comp)
          continue; // process each unordered split once
        long long cand = dp[sub][v] + dp[comp][v];
        if (cand < best) {
          best = cand;
          bestSub = sub;
        }
      }
      dp[mask][v] = best;
      parentSub[mask][v] = bestSub;
    }

    // Relax: let a cheaper nearby vertex take over as the meeting point --
    // a plain O(V^2) array Dijkstra over the complete Hanan-vertex graph,
    // seeded with the merge step's own dp[mask][*] values. V stays small
    // (<= 64 at the default 8-pin cap), so no heap is needed.
    std::vector<bool> done(V, false);
    for (int iter = 0; iter < V; ++iter) {
      int u = -1;
      for (int v = 0; v < V; ++v)
        if (!done[v] && (u < 0 || dp[mask][v] < dp[mask][u]))
          u = v;
      if (u < 0 || dp[mask][u] >= kInf)
        break;
      done[u] = true;
      for (int v = 0; v < V; ++v) {
        if (done[v])
          continue;
        long long cand = dp[mask][u] + dist(verts[u], verts[v]);
        if (cand < dp[mask][v]) {
          dp[mask][v] = cand;
          relayFrom[mask][v] = u;
        }
      }
    }
  }

  int fullMask = numMasks - 1;
  int bestV = 0;
  for (int v = 1; v < V; ++v)
    if (dp[fullMask][v] < dp[fullMask][bestV])
      bestV = v;

  SteinerResult result;
  result.pts = pts;
  std::vector<std::pair<int, int>> edges;

  // pointIndexOf(p): index into result.pts for point p, appending a new
  // Steiner point only if p isn't already present (a Hanan vertex that
  // happens to coincide with a terminal must reuse that terminal's own
  // index, not create a duplicate).
  auto pointIndexOf = [&](const Point &p) -> int {
    for (size_t i = 0; i < result.pts.size(); ++i)
      if (result.pts[i].x == p.x && result.pts[i].y == p.y)
        return static_cast<int>(i);
    result.pts.push_back(p);
    return static_cast<int>(result.pts.size()) - 1;
  };

  // Traceback over (mask, v): emit the edges of the subtree covering
  // `mask` that meets at Hanan vertex v, following relayFrom chains first
  // (each is one relax hop) and otherwise the merge split recorded in
  // parentSub, down to singleton masks (a direct terminal-to-v edge).
  std::function<void(int, int)> build = [&](int mask, int v) {
    if (relayFrom[mask][v] >= 0) {
      int u = relayFrom[mask][v];
      int vi = pointIndexOf(verts[v]);
      int ui = pointIndexOf(verts[u]);
      if (vi != ui)
        edges.push_back({ui, vi});
      build(mask, u);
      return;
    }
    if ((mask & (mask - 1)) == 0) {
      int i = 0;
      while (!((mask >> i) & 1))
        ++i;
      int ti = pointIndexOf(pts[i]);
      int vi = pointIndexOf(verts[v]);
      if (ti != vi)
        edges.push_back({ti, vi});
      return;
    }
    int sub = parentSub[mask][v];
    build(sub, v);
    build(mask ^ sub, v);
  };
  build(fullMask, bestV);

  result.edges = std::move(edges);
  return result;
}

struct Seg {
  int x1, y1, l1, x2, y2, l2;
};
struct ChargeRecord {
  bool horizontal;
  int cross, a, b, layer, amount;
};

void undoCharge(const RoutingGrid &g, Usage &u, const ChargeRecord &c) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x)
      u.h[g.hIndex(x, c.cross, c.layer)] -= c.amount;
  } else {
    for (int y = c.a; y < c.b; ++y)
      u.v[g.vIndex(c.cross, y, c.layer)] -= c.amount;
  }
}

void reconcile(std::vector<int> &nodeLayer, int idx, int layer, Point at,
               std::vector<Seg> &out) {
  if (nodeLayer[idx] == -1) {
    nodeLayer[idx] = layer;
  } else if (nodeLayer[idx] != layer) {
    out.push_back({at.x, at.y, nodeLayer[idx], at.x, at.y, layer});
    nodeLayer[idx] = layer;
  }
}

struct RoutedNet {
  const Net *net = nullptr;
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> mst;
  std::vector<Seg> segs;
  std::vector<ChargeRecord> charges;
  bool giant = false;
  // Once true, this net is always routed by the Dijkstra tier from here on
  // -- see the file header for why there's no reason to downgrade back.
  bool escalated = false;
  // This file's own mechanism: the slow tier's search-box margin for this
  // net, in tiles. -1 means "not set" -- the searches then fall back to
  // VROUTE_MAX_MARGIN exactly as nv4route does (BoxMode::Fixed).
  int boxMargin = -1;
};

// This file's own mechanism: how the slow tier's search-box margin is
// chosen. Fixed reproduces nv4route (VROUTE_MAX_MARGIN for every search).
// Round mirrors NTHU-Route 2.0's main stage: every net's margin in slow
// round k is init + k*grow. PerNet widens only the nets that stay
// overflowed: a net starts at init and grows by grow after each round it
// is still overflowed. Both non-Fixed modes clamp at cap.
enum class BoxMode { Fixed, Round, PerNet };

struct BoxGrowth {
  BoxMode mode = BoxMode::Fixed;
  int init = 24;
  int grow = 4;
  int cap = 160;
};

int boxMarginForRound(const BoxGrowth &box, int round) {
  long long m = static_cast<long long>(box.init) + static_cast<long long>(round) * box.grow;
  return static_cast<int>(std::min<long long>(box.cap, m));
}

int grownBoxMargin(const BoxGrowth &box, int current) {
  long long m = static_cast<long long>(current) + box.grow;
  return static_cast<int>(std::min<long long>(box.cap, m));
}

// Phase B, before a net's slow-tier search: Fixed leaves boxMargin at -1
// (nv4route's VROUTE_MAX_MARGIN); Round sets it from the round index for
// every net alike; PerNet sets init the first time and otherwise keeps
// whatever growBoxMarginIfStillOverflowed left it at.
void assignBoxMarginForSearch(const BoxGrowth &box, int round, RoutedNet &rn) {
  if (box.mode == BoxMode::Round)
    rn.boxMargin = boxMarginForRound(box, round);
  else if (box.mode == BoxMode::PerNet && rn.boxMargin < 1)
    rn.boxMargin = box.init;
}

// Phase B, after a net's supervised reroute (PerNet only): widen the box
// for next round if the net is still overflowed; leave it alone otherwise.
void growBoxMarginIfStillOverflowed(const BoxGrowth &box, bool stillOverflowed, RoutedNet &rn) {
  if (box.mode == BoxMode::PerNet && stillOverflowed)
    rn.boxMargin = grownBoxMargin(box, rn.boxMargin);
}

// Parses NV11ROUTE_BOX_{MODE,INIT,GROW,CAP} (any may be null = default).
// Returns false with a message in *err on invalid input rather than
// silently clamping: an unintended margin quietly changes routing quality
// and runtime, the "wrong code still runs" failure this project guards
// against. Rules: mode is fixed|round|pernet; init >= 1; grow >= 0;
// cap >= init. Non-numeric or trailing-garbage numbers are rejected.
bool parseBoxGrowth(const char *mode, const char *init, const char *grow, const char *cap,
                    BoxGrowth &out, std::string &err) {
  BoxGrowth b;
  if (mode) {
    std::string m(mode);
    if (m == "fixed")
      b.mode = BoxMode::Fixed;
    else if (m == "round")
      b.mode = BoxMode::Round;
    else if (m == "pernet")
      b.mode = BoxMode::PerNet;
    else {
      err = "NV11ROUTE_BOX_MODE must be fixed, round, or pernet (got '" + m + "')";
      return false;
    }
  }
  auto parseInt = [&](const char *s, const char *name, int &v) {
    if (!s)
      return true;
    char *end = nullptr;
    long x = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || x < INT_MIN || x > INT_MAX) {
      err = std::string(name) + " must be an integer (got '" + s + "')";
      return false;
    }
    v = static_cast<int>(x);
    return true;
  };
  if (!parseInt(init, "NV11ROUTE_BOX_INIT", b.init) || !parseInt(grow, "NV11ROUTE_BOX_GROW", b.grow) ||
      !parseInt(cap, "NV11ROUTE_BOX_CAP", b.cap))
    return false;
  if (b.init < 1) {
    err = "NV11ROUTE_BOX_INIT must be >= 1 (got " + std::to_string(b.init) + ")";
    return false;
  }
  if (b.grow < 0) {
    err = "NV11ROUTE_BOX_GROW must be >= 0 (got " + std::to_string(b.grow) + ")";
    return false;
  }
  if (b.cap < b.init) {
    err = "NV11ROUTE_BOX_CAP (" + std::to_string(b.cap) + ") must be >= NV11ROUTE_BOX_INIT (" +
          std::to_string(b.init) + ")";
    return false;
  }
  out = b;
  return true;
}

// pd2route's own mechanism: what the slow tier does with a reroute that
// made its local overflow worse. kRevert (default) is the inherited
// supervision rule: undo it. kKeep is NTHU-Route 2.0's rule: keep every new
// route and let history pricing move nets off over later rounds.
enum class AcceptMode { kRevert, kKeep };

bool revertReroute(AcceptMode mode, long long localBefore, long long localAfter) {
  return mode == AcceptMode::kRevert && localAfter > localBefore;
}

// Parses PD2ROUTE_ACCEPT (revert | keep; unset = revert). Fails loudly on
// anything else.
bool parseAcceptMode(const char *v, AcceptMode &out, std::string &err) {
  if (!v) {
    out = AcceptMode::kRevert;
    return true;
  }
  std::string m(v);
  if (m == "revert")
    out = AcceptMode::kRevert;
  else if (m == "keep")
    out = AcceptMode::kKeep;
  else {
    err = "PD2ROUTE_ACCEPT must be revert or keep (got '" + m + "')";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Fast tier: nproute's L/Z pattern candidates, verbatim.
// ---------------------------------------------------------------------------

struct Leg {
  bool horizontal;
  int cross;
  int a, b;
  int entry;
};
using Candidate = std::vector<Leg>;

std::vector<int> jogPositions(int lo, int hi) {
  std::vector<int> out;
  int span = hi - lo;
  if (span < 2)
    return out;
  for (int num : {1, 2, 3}) {
    int p = lo + (span * num) / 4;
    if (p > lo && p < hi)
      out.push_back(p);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::vector<Candidate> generateCandidates(Point p1, Point p2) {
  std::vector<Candidate> out;
  int xa = std::min(p1.x, p2.x), xb = std::max(p1.x, p2.x);
  int ya = std::min(p1.y, p2.y), yb = std::max(p1.y, p2.y);
  bool dx = xa != xb, dy = ya != yb;

  if (!dx && !dy)
    return out;
  if (!dy) {
    out.push_back({{true, p1.y, xa, xb, p1.x}});
    return out;
  }
  if (!dx) {
    out.push_back({{false, p1.x, ya, yb, p1.y}});
    return out;
  }

  out.push_back({{true, p1.y, xa, xb, p1.x}, {false, p2.x, ya, yb, p1.y}});
  out.push_back({{false, p1.x, ya, yb, p1.y}, {true, p2.y, xa, xb, p1.x}});

  for (int mid : jogPositions(xa, xb)) {
    Candidate c;
    c.push_back({true, p1.y, std::min(p1.x, mid), std::max(p1.x, mid), p1.x});
    c.push_back({false, mid, ya, yb, p1.y});
    c.push_back({true, p2.y, std::min(mid, p2.x), std::max(mid, p2.x), mid});
    out.push_back(std::move(c));
  }
  for (int mid : jogPositions(ya, yb)) {
    Candidate c;
    c.push_back({false, p1.x, std::min(p1.y, mid), std::max(p1.y, mid), p1.y});
    c.push_back({true, mid, xa, xb, p1.x});
    c.push_back({false, p2.x, std::min(mid, p2.y), std::max(mid, p2.y), mid});
    out.push_back(std::move(c));
  }
  return out;
}

struct LayerChoice {
  int layer;
  double cost;
};

LayerChoice bestHLayer(const RoutingGrid &g, const Prices &pr, const std::vector<int> &layers,
                        int y, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<double>::max()};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    double cost = 0;
    for (int x = a; x < b; ++x) {
      size_t idx = g.hIndex(x, y, l);
      cost += wireCostH(pr, idx, amount);
    }
    if (cost < best.cost)
      best = {l, cost};
  }
  return best;
}
LayerChoice bestVLayer(const RoutingGrid &g, const Prices &pr, const std::vector<int> &layers,
                        int x, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<double>::max()};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    double cost = 0;
    for (int y = a; y < b; ++y) {
      size_t idx = g.vIndex(x, y, l);
      cost += wireCostV(pr, idx, amount);
    }
    if (cost < best.cost)
      best = {l, cost};
  }
  return best;
}

struct Priced {
  double cost;
  std::vector<int> legLayer;
};

Priced priceCandidate(const RoutingGrid &g, const Prices &pr, const LayerPlan &lp,
                       const Candidate &cand, int width) {
  Priced p;
  p.cost = 0;
  int prevLayer = -1;
  for (const Leg &leg : cand) {
    LayerChoice lc = leg.horizontal
                          ? bestHLayer(g, pr, lp.hLayers, leg.cross, leg.a, leg.b, width)
                          : bestVLayer(g, pr, lp.vLayers, leg.cross, leg.a, leg.b, width);
    p.cost += lc.cost;
    if (prevLayer != -1 && prevLayer != lc.layer)
      p.cost += 1.0;
    prevLayer = lc.layer;
    p.legLayer.push_back(lc.layer);
  }
  return p;
}

struct EdgeResult {
  int layerAtP1, layerAtP2;
};

EdgeResult commitCandidate(const Candidate &cand, const std::vector<int> &legLayer,
                            const RoutingGrid &g, Usage &u, int width, std::vector<Seg> &segs,
                            std::vector<ChargeRecord> &charges) {
  int curLayer = -1;
  int layerAtP1 = -1;
  for (size_t i = 0; i < cand.size(); ++i) {
    const Leg &leg = cand[i];
    int l = legLayer[i];
    if (curLayer != -1 && curLayer != l) {
      if (leg.horizontal)
        segs.push_back({leg.entry, leg.cross, curLayer, leg.entry, leg.cross, l});
      else
        segs.push_back({leg.cross, leg.entry, curLayer, leg.cross, leg.entry, l});
    }
    curLayer = l;
    if (i == 0)
      layerAtP1 = l;

    int amount = width + g.minSpacing[l];
    if (leg.horizontal) {
      for (int x = leg.a; x < leg.b; ++x)
        u.h[g.hIndex(x, leg.cross, l)] += amount;
      segs.push_back({leg.a, leg.cross, l, leg.b, leg.cross, l});
    } else {
      for (int y = leg.a; y < leg.b; ++y)
        u.v[g.vIndex(leg.cross, y, l)] += amount;
      segs.push_back({leg.cross, leg.a, l, leg.cross, leg.b, l});
    }
    charges.push_back({leg.horizontal, leg.cross, leg.a, leg.b, l, amount});
  }
  return {layerAtP1, curLayer};
}

void negotiateWire(const RoutingGrid &g, Prices &pr, const Usage &u, const ChargeRecord &c,
                    double epsilon, double threshold) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x) {
      size_t i = g.hIndex(x, c.cross, c.layer);
      if (u.capH[i] <= 0)
        pr.h[i] = kForbiddenPrice;
      else
        // Clamped at kForbiddenPrice, the same finite deterrent a
        // hard-blocked edge already gets: ported from mroute.cpp, where an
        // edge under sustained deep overflow for enough rounds was found to
        // compound std::exp(...) past DBL_MAX to literal infinity. inf is
        // never "<" a search's own infinity sentinel, so the slow tier's
        // Dijkstra can't relax that edge at all -- and if every alternative
        // is in the same state, the search reports no path on a grid that
        // is in fact still connected. See mroute.cpp's negotiateWire
        // comment and testPriceClampPreventsInfinity for the full story;
        // the bug is in the shared price-update pattern, not anything
        // mroute-specific, so it applies here too even though the fast
        // tier's own routeNetFast doesn't route by real search.
        pr.h[i] = std::min(kForbiddenPrice, pr.h[i] * std::exp(epsilon * priceExponentTerm(u.h[i], u.capH[i], threshold)));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (u.capV[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] = std::min(kForbiddenPrice, pr.v[i] * std::exp(epsilon * priceExponentTerm(u.v[i], u.capV[i], threshold)));
    }
  }
}

// Routes one net with the fast tier: same shape as nproute's routeNet.
void routeNetFast(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                   double epsilon, double threshold, RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.segs.clear();
  rn.charges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    auto candidates = generateCandidates(p1, p2);

    size_t bestIdx = 0;
    Priced bestPriced = priceCandidate(g, prices, lp, candidates[0], rn.net->minWidth);
    for (size_t i = 1; i < candidates.size(); ++i) {
      Priced pr = priceCandidate(g, prices, lp, candidates[i], rn.net->minWidth);
      if (pr.cost < bestPriced.cost) {
        bestPriced = std::move(pr);
        bestIdx = i;
      }
    }

    size_t chargesBefore = rn.charges.size();
    EdgeResult r = commitCandidate(candidates[bestIdx], bestPriced.legLayer, g, usage,
                                    rn.net->minWidth, rn.segs, rn.charges);
    for (size_t i = chargesBefore; i < rn.charges.size(); ++i)
      negotiateWire(g, prices, usage, rn.charges[i], epsilon, threshold);

    reconcile(nodeLayer, e.first, r.layerAtP1, p1, rn.segs);
    reconcile(nodeLayer, e.second, r.layerAtP2, p2, rn.segs);
  }

  for (const Pin &p : rn.net->pins) {
    for (size_t i = 0; i < rn.pts.size(); ++i) {
      if (rn.pts[i].x == p.x && rn.pts[i].y == p.y) {
        reconcile(nodeLayer, static_cast<int>(i), p.layer, rn.pts[i], rn.segs);
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Slow tier: mroute's bounded Dijkstra, verbatim (including the margin cap
// and A* fixes that brought it from 105s to 20s per round on adaptec1).
// ---------------------------------------------------------------------------

struct SearchRegion {
  int x0 = 0, y0 = 0, w = 0, h = 0, nLayers = 0;
  std::vector<double> dist;
  std::vector<int> parent;
  std::vector<int> stamp;
  int generation = 0;

  void reset(int x0_, int y0_, int w_, int h_, int nLayers_) {
    x0 = x0_;
    y0 = y0_;
    w = w_;
    h = h_;
    nLayers = nLayers_;
    size_t needed = static_cast<size_t>(w) * h * nLayers;
    if (dist.size() < needed) {
      dist.resize(needed);
      parent.resize(needed);
      stamp.resize(needed, -1);
    }
    ++generation;
  }

  bool inRegion(int x, int y) const { return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h; }
  int idx(int x, int y, int l) const { return (l * h + (y - y0)) * w + (x - x0); }
  void coordsOf(int i, int &x, int &y, int &l) const {
    l = i / (w * h);
    int rem = i % (w * h);
    y = y0 + rem / w;
    x = x0 + rem % w;
  }

  double distAt(int i) {
    if (stamp[i] != generation) {
      stamp[i] = generation;
      dist[i] = std::numeric_limits<double>::infinity();
      parent[i] = -1;
    }
    return dist[i];
  }
  void setDist(int i, double d, int p) {
    stamp[i] = generation;
    dist[i] = d;
    parent[i] = p;
  }
};

struct PathNode {
  int x, y, l;
};
struct DijkstraResult {
  bool found = false;
  std::vector<PathNode> path;
};

DijkstraResult boundedDijkstra(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                                SearchRegion &region, Point p1, int p1Layer, Point p2, int width,
                                int margin) {
  int xa = std::min(p1.x, p2.x), xb = std::max(p1.x, p2.x);
  int ya = std::min(p1.y, p2.y), yb = std::max(p1.y, p2.y);
  int x0 = std::max(0, xa - margin), x1 = std::min(g.nx - 1, xb + margin);
  int y0 = std::max(0, ya - margin), y1 = std::min(g.ny - 1, yb + margin);
  // This file's own new mechanism: one synthetic layer (see LayerPlan's
  // own comment) -- SearchRegion is sized for nLayers=1, not g.nLayers,
  // and every node lives at l=0.
  region.reset(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 1);

  const double hScale = heuristicScale(prices);
  auto heuristic = [&](int x, int y) {
    return hScale * static_cast<double>(std::abs(x - p2.x) + std::abs(y - p2.y));
  };
  struct QueueEntry {
    double priority, dist;
    int index;
  };
  struct Cmp {
    bool operator()(const QueueEntry &a, const QueueEntry &b) const {
      return a.priority > b.priority;
    }
  };
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, Cmp> pq;

  // vroute's p1Layer<0 ("reachable at any layer") case collapses to a
  // single push at the one synthetic layer -- there is no "any layer" to
  // search among anymore, so p1Layer's value (0 or -1, per reconcile's
  // own convention) no longer changes anything.
  {
    int i = region.idx(p1.x, p1.y, 0);
    region.setDist(i, 0.0, -1);
    pq.push({heuristic(p1.x, p1.y), 0.0, i});
  }
  (void)p1Layer;

  DijkstraResult result;
  int foundIdx = -1;
  while (!pq.empty()) {
    QueueEntry top = pq.top();
    pq.pop();
    double d = top.dist;
    int i = top.index;
    if (d > region.distAt(i))
      continue;
    int x, y, l;
    region.coordsOf(i, x, y, l);
    if (x == p2.x && y == p2.y) {
      foundIdx = i;
      break;
    }
    int amount = width + g.minSpacing[l];

    if (lp.isH(l)) {
      for (int nx : {x - 1, x + 1}) {
        if (!region.inRegion(nx, y))
          continue;
        int ex = std::min(x, nx);
        size_t e = g.hIndex(ex, y, l);
        double w = wireCostH(prices, e, amount);
        int ni = region.idx(nx, y, l);
        if (d + w < region.distAt(ni)) {
          region.setDist(ni, d + w, i);
          pq.push({d + w + heuristic(nx, y), d + w, ni});
        }
      }
    }
    if (lp.isV(l)) {
      for (int ny : {y - 1, y + 1}) {
        if (!region.inRegion(x, ny))
          continue;
        int ey = std::min(y, ny);
        size_t e = g.vIndex(x, ey, l);
        double w = wireCostV(prices, e, amount);
        int ni = region.idx(x, ny, l);
        if (d + w < region.distAt(ni)) {
          region.setDist(ni, d + w, i);
          pq.push({d + w + heuristic(x, ny), d + w, ni});
        }
      }
    }
    // No via block here -- this file's own 2D collapse has exactly one
    // synthetic layer, so there is no layer to change to. See this file's
    // header for why via cost/placement is deferred to a later
    // generation's layer-assignment stage entirely.
  }

  if (foundIdx < 0)
    return result;

  result.found = true;
  for (int i = foundIdx; i != -1; i = region.parent[i]) {
    int x, y, l;
    region.coordsOf(i, x, y, l);
    result.path.push_back({x, y, l});
  }
  std::reverse(result.path.begin(), result.path.end());
  return result;
}

// marginOverride >= 1 (this file's own mechanism: the net's own boxMargin)
// replaces VROUTE_MAX_MARGIN as the cap; -1 reproduces nv4route exactly.
DijkstraResult routeSegment(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                             SearchRegion &region, Point p1, int p1Layer, Point p2, int width,
                             int marginOverride = -1) {
  static const int maxMargin = [] {
    const char *m = std::getenv("VROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int cap = marginOverride >= 1 ? marginOverride : maxMargin;
  int span = std::abs(p1.x - p2.x) + std::abs(p1.y - p2.y);
  int margin = std::min(cap, std::max(4, span / 8));
  for (int attempt = 0; attempt < 6; ++attempt) {
    DijkstraResult r = boundedDijkstra(g, prices, lp, region, p1, p1Layer, p2, width, margin);
    if (r.found)
      return r;
    margin *= 2;
  }
  return DijkstraResult{};
}

void commitPath(const RoutingGrid &g, Prices &prices, Usage &usage, double epsilon, int width,
                const std::vector<PathNode> &path, std::vector<Seg> &segs,
                std::vector<ChargeRecord> &charges) {
  size_t i = 0;
  while (i + 1 < path.size()) {
    const PathNode &a = path[i];
    const PathNode &b = path[i + 1];
    if (a.l != b.l) {
      segs.push_back({a.x, a.y, a.l, b.x, b.y, b.l});
      ++i;
      continue;
    }
    bool horizontal = a.y == b.y;
    int l = a.l;
    size_t j = i + 1;
    while (j + 1 < path.size() && path[j + 1].l == l &&
           (horizontal ? (path[j + 1].y == a.y) : (path[j + 1].x == a.x)))
      ++j;
    int aCoord = horizontal ? a.x : a.y;
    int bCoord = horizontal ? path[j].x : path[j].y;
    int lo = std::min(aCoord, bCoord), hi = std::max(aCoord, bCoord);
    int amount = width + g.minSpacing[l];
    ChargeRecord c{horizontal, horizontal ? a.y : a.x, lo, hi, l, amount};
    if (horizontal) {
      for (int x = lo; x < hi; ++x)
        usage.h[g.hIndex(x, c.cross, l)] += amount;
    } else {
      for (int y = lo; y < hi; ++y)
        usage.v[g.vIndex(c.cross, y, l)] += amount;
    }
    charges.push_back(c);
    negotiateWire(g, prices, usage, c, epsilon, defaultThreshold());
    segs.push_back({horizontal ? lo : a.x, horizontal ? a.y : lo, l, horizontal ? hi : a.x,
                     horizontal ? a.y : hi, l});
    i = j;
  }
}

// ---------------------------------------------------------------------------
// Multi-source multi-sink (MSMV) maze routing -- this file's own new
// mechanism. See the file header for the full motivation. Every prior
// router in this lineage builds a net's Steiner topology once, up front,
// from an abstract Manhattan-distance MST over the pins (`mstEdges`), then
// routes each fixed tree edge as an independent 2-terminal search between
// its two specific endpoint pins. That has two structural limits real
// competition routers (FastRoute, NTHU-Route) avoid: the topology itself is
// decided before any congestion information exists, and a tree edge can
// only attach at its two designated pin endpoints, never at some other
// point along wire the net has *already* committed elsewhere.
//
// MSMV routing removes both limits by growing the tree incrementally with
// real search instead of committing to an abstract topology at all: start
// connected with just one pin; repeatedly treat every tile the net has
// committed so far as a simultaneous source and every not-yet-connected
// pin as a simultaneous sink, and run one Dijkstra wave to find the
// genuinely cheapest next connection, wherever it attaches. Whichever pin
// is reached first joins "connected"; repeat until every pin is in the
// tree. This is literally Prim's algorithm, but each step's edge weight is
// a real congestion-aware grid search instead of an abstract Manhattan
// distance, and reconnection can tap into the middle of already-built wire
// (see testMSMVAttachesToExistingWireNotJustPinEndpoints below for a fixture
// where this measurably shortens the tree relative to a fixed topology).
// ---------------------------------------------------------------------------

struct MSMVResult {
  bool found = false;
  std::vector<PathNode> path;
  int sinkIndex = -1;
};

// `sources` are tiles already part of the growing tree; `layer == -1` means
// "reachable at any layer" (used for a pin that hasn't been routed to yet),
// matching boundedDijkstra's own p1Layer convention exactly. `sinks` are
// the remaining unconnected pin locations (2D only -- like boundedDijkstra,
// a sink is reached at whatever layer is cheapest; reconcile() stitches the
// layer afterward). The search stops at the first sink node popped off the
// queue, which by Dijkstra's own correctness guarantee is the cheapest
// reachable sink from the whole source set combined -- exactly "which pin
// is cheapest to connect next," decided by real cost, not distance.
// pd14route: search-node expansions, the deterministic work measure the
// region stage's work-gated plateau stop counts (never read by routing).
// pd22route: per thread, so each fallback path counts only its own work.
thread_local long long gSearchPops = 0;

// pd24route: batched parallel rerouting settings (set once in main();
// tests set and restore them).
struct ParallelSettings {
  int threads = 0; // PD24ROUTE_THREADS (0 = sequential loops)
  int batch = 64;  // PD24ROUTE_BATCH
  bool redo = true; // PD24ROUTE_REDO
  bool passes = true; // pd25route: PD25ROUTE_REDO_PASSES (conflicts re-searched in parallel passes)
  bool fresh = true;  // pd27route: PD27ROUTE_CONFLICT (fresh | any)
  bool carry = true;  // pd27route: PD27ROUTE_CARRY (conflicts join the next batch)
  bool checkReplicas = false; // self-test hook: verify every replica equals the live state after each batch
};
ParallelSettings gParallel;

// pd24route: PD24ROUTE_THREADS (0-256), PD24ROUTE_BATCH (1-100000),
// PD24ROUTE_REDO (0 | 1); unset keeps the defaults. Fails loudly otherwise.
bool parseParallel(const char *threads, const char *batch, const char *redo, ParallelSettings &out, std::string &err) {
  ParallelSettings p = out;
  auto num = [&](const char *v, const char *name, long lo, long hi, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > hi) {
      err = std::string(name) + " must be an integer " + std::to_string(lo) + "-" + std::to_string(hi) + " (got '" + v +
            "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!num(threads, "PD24ROUTE_THREADS", 0, 256, p.threads) || !num(batch, "PD24ROUTE_BATCH", 1, 100000, p.batch))
    return false;
  if (redo) {
    std::string r(redo);
    if (r != "0" && r != "1") {
      err = "PD24ROUTE_REDO must be 0 or 1 (got '" + r + "')";
      return false;
    }
    p.redo = r == "1";
  }
  out = p;
  return true;
}

MSMVResult multiSourceDijkstra(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                                SearchRegion &region, const std::vector<PathNode> &sources,
                                const std::vector<Point> &sinks, int width, int margin,
                                bool boxHeuristic = false) {
  int xa = sources[0].x, xb = sources[0].x, ya = sources[0].y, yb = sources[0].y;
  for (const PathNode &s : sources) {
    xa = std::min(xa, s.x);
    xb = std::max(xb, s.x);
    ya = std::min(ya, s.y);
    yb = std::max(yb, s.y);
  }
  for (const Point &s : sinks) {
    xa = std::min(xa, s.x);
    xb = std::max(xb, s.x);
    ya = std::min(ya, s.y);
    yb = std::max(yb, s.y);
  }
  int x0 = std::max(0, xa - margin), x1 = std::min(g.nx - 1, xb + margin);
  int y0 = std::max(0, ya - margin), y1 = std::min(g.ny - 1, yb + margin);
  // This file's own new mechanism: one synthetic layer, see boundedDijkstra's
  // own identical comment.
  region.reset(x0, y0, x1 - x0 + 1, y1 - y0 + 1, 1);

  // O(1) "is this tile a sink, and which one" lookup, scoped to this call
  // only -- sinks can number in the hundreds for a large net, and a linear
  // scan per popped node would make this quadratic in the net's pin count.
  std::unordered_map<long long, int> sinkAt;
  auto key = [](int x, int y) { return static_cast<long long>(x) * 1000000 + y; };
  for (size_t i = 0; i < sinks.size(); ++i)
    sinkAt[key(sinks[i].x, sinks[i].y)] = static_cast<int>(i);

  const double hScale = heuristicScale(prices);
  // pd6route: boxHeuristic uses the Manhattan distance to the sinks'
  // bounding box instead of the nearest sink -- still a lower bound on the
  // distance to every sink, and 1-Lipschitz, so still admissible and
  // consistent, but O(1) per push; piece reroutes can have hundreds of sink
  // tiles (a whole component of the net).
  int sx0 = sinks[0].x, sx1 = sinks[0].x, sy0 = sinks[0].y, sy1 = sinks[0].y;
  for (const Point &s : sinks) {
    sx0 = std::min(sx0, s.x);
    sx1 = std::max(sx1, s.x);
    sy0 = std::min(sy0, s.y);
    sy1 = std::max(sy1, s.y);
  }
  auto heuristic = [&](int x, int y) {
    if (boxHeuristic)
      return hScale * static_cast<double>(std::max({0, sx0 - x, x - sx1}) + std::max({0, sy0 - y, y - sy1}));
    int best = std::numeric_limits<int>::max();
    for (const Point &s : sinks)
      best = std::min(best, std::abs(x - s.x) + std::abs(y - s.y));
    return hScale * static_cast<double>(best);
  };
  struct QueueEntry {
    double priority, dist;
    int index;
  };
  struct Cmp {
    bool operator()(const QueueEntry &a, const QueueEntry &b) const {
      return a.priority > b.priority;
    }
  };
  std::priority_queue<QueueEntry, std::vector<QueueEntry>, Cmp> pq;

  for (const PathNode &s : sources) {
    if (!region.inRegion(s.x, s.y))
      continue;
    // vroute's s.l<0 ("reachable at any layer") case collapses to the one
    // synthetic layer, same as boundedDijkstra's own p1Layer -- there is
    // no "any layer" to search among anymore.
    {
      int i = region.idx(s.x, s.y, 0);
      region.setDist(i, 0.0, -1);
      pq.push({heuristic(s.x, s.y), 0.0, i});
    }
  }

  MSMVResult result;
  int foundIdx = -1;
  while (!pq.empty()) {
    QueueEntry top = pq.top();
    pq.pop();
    double d = top.dist;
    int i = top.index;
    if (d > region.distAt(i))
      continue;
    ++gSearchPops;
    int x, y, l;
    region.coordsOf(i, x, y, l);
    if (auto it = sinkAt.find(key(x, y)); it != sinkAt.end()) {
      foundIdx = i;
      result.sinkIndex = it->second;
      break;
    }
    int amount = width + g.minSpacing[l];

    if (lp.isH(l)) {
      for (int nx : {x - 1, x + 1}) {
        if (!region.inRegion(nx, y))
          continue;
        int ex = std::min(x, nx);
        size_t e = g.hIndex(ex, y, l);
        double w = wireCostH(prices, e, amount);
        int ni = region.idx(nx, y, l);
        if (d + w < region.distAt(ni)) {
          region.setDist(ni, d + w, i);
          pq.push({d + w + heuristic(nx, y), d + w, ni});
        }
      }
    }
    if (lp.isV(l)) {
      for (int ny : {y - 1, y + 1}) {
        if (!region.inRegion(x, ny))
          continue;
        int ey = std::min(y, ny);
        size_t e = g.vIndex(x, ey, l);
        double w = wireCostV(prices, e, amount);
        int ni = region.idx(x, ny, l);
        if (d + w < region.distAt(ni)) {
          region.setDist(ni, d + w, i);
          pq.push({d + w + heuristic(x, ny), d + w, ni});
        }
      }
    }
    // No via block here -- see boundedDijkstra's own identical comment.
  }

  if (foundIdx < 0)
    return result;

  result.found = true;
  for (int i = foundIdx; i != -1; i = region.parent[i]) {
    int x, y, l;
    region.coordsOf(i, x, y, l);
    result.path.push_back({x, y, l});
  }
  std::reverse(result.path.begin(), result.path.end());
  return result;
}

// marginOverride >= 1 (this file's own mechanism: the net's own boxMargin)
// replaces VROUTE_MAX_MARGIN as the starting margin; -1 reproduces
// nv4route exactly. The no-path doubling retry is unchanged either way.
MSMVResult routeStepMSMV(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                          SearchRegion &region, const std::vector<PathNode> &sources,
                          const std::vector<Point> &sinks, int width, int marginOverride = -1,
                          bool boxHeuristic = false) {
  static const int maxMargin = [] {
    const char *m = std::getenv("VROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int margin = marginOverride >= 1 ? marginOverride : maxMargin;
  for (int attempt = 0; attempt < 6; ++attempt) {
    MSMVResult r = multiSourceDijkstra(g, prices, lp, region, sources, sinks, width, margin, boxHeuristic);
    if (r.found)
      return r;
    margin *= 2;
  }
  return MSMVResult{};
}

// Every tile currently occupied by this net's committed wire, one PathNode
// per tile per layer it's actually on -- the full "already part of the
// tree" source set MSMV needs, not just the original pin locations. Via
// segments (c.l1 != c.l2 at a single x,y) contribute one tile at each of
// the two layers they connect.
std::vector<PathNode> netFootprint(const std::vector<Seg> &segs) {
  std::vector<PathNode> out;
  for (const Seg &s : segs) {
    if (s.l1 != s.l2) {
      out.push_back({s.x1, s.y1, s.l1});
      out.push_back({s.x1, s.y1, s.l2});
      continue;
    }
    if (s.y1 == s.y2) {
      for (int x = std::min(s.x1, s.x2); x <= std::max(s.x1, s.x2); ++x)
        out.push_back({x, s.y1, s.l1});
    } else {
      for (int y = std::min(s.y1, s.y2); y <= std::max(s.y1, s.y2); ++y)
        out.push_back({s.x1, y, s.l1});
    }
  }
  return out;
}

// Grows the whole net incrementally via MSMV, replacing rn.mst's fixed
// per-edge loop entirely -- the topology itself is decided by this
// process, not read from a precomputed MST. Pin order (which pin starts
// "connected") doesn't matter for correctness, only for which of several
// equal-cost trees comes out; pts[0] is as good a start as any.
void routeNetSlowMSMV(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                       SearchRegion &region, double epsilon, RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.segs.clear();
  rn.charges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  std::vector<size_t> remaining;
  for (size_t i = 1; i < rn.pts.size(); ++i)
    remaining.push_back(i);
  // The starting pin is "connected" with no committed wire yet -- source at
  // "any layer," matching how a fresh pin has never been assigned one.
  std::vector<PathNode> sourceSet = {{rn.pts[0].x, rn.pts[0].y, -1}};

  while (!remaining.empty()) {
    std::vector<Point> sinks;
    for (size_t idx : remaining)
      sinks.push_back(rn.pts[idx]);

    MSMVResult r = routeStepMSMV(g, prices, lp, region, sourceSet, sinks, rn.net->minWidth,
                                 rn.boxMargin);
    if (!r.found)
      fail("vroute: MSMV found no path to any remaining pin even after margin "
           "expansion -- this should not happen on a fully connected grid");

    commitPath(g, prices, usage, epsilon, rn.net->minWidth, r.path, rn.segs, rn.charges);

    size_t reachedIdx = remaining[r.sinkIndex];
    reconcile(nodeLayer, static_cast<int>(reachedIdx), r.path.back().l, rn.pts[reachedIdx], rn.segs);
    // The other end of this path attached somewhere in the existing
    // source set -- record that attachment's layer too, in case it lands
    // on a pin's own point rather than mid-wire.
    for (size_t i = 0; i < rn.pts.size(); ++i)
      if (rn.pts[i].x == r.path.front().x && rn.pts[i].y == r.path.front().y)
        reconcile(nodeLayer, static_cast<int>(i), r.path.front().l, rn.pts[i], rn.segs);

    remaining.erase(remaining.begin() + r.sinkIndex);
    sourceSet = netFootprint(rn.segs);
  }

  for (const Pin &p : rn.net->pins) {
    for (size_t i = 0; i < rn.pts.size(); ++i) {
      if (rn.pts[i].x == p.x && rn.pts[i].y == p.y) {
        reconcile(nodeLayer, static_cast<int>(i), p.layer, rn.pts[i], rn.segs);
        break;
      }
    }
  }
}

// Routes one net with the slow tier: same shape as mroute's routeNet.
void routeNetSlow(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                   SearchRegion &region, double epsilon, RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.segs.clear();
  rn.charges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    int p1Layer = nodeLayer[e.first];
    DijkstraResult r = routeSegment(g, prices, lp, region, p1, p1Layer, p2, rn.net->minWidth,
                                    rn.boxMargin);
    if (!r.found)
      fail("vroute: no path found between tree points even after margin expansion -- "
           "this should not happen on a fully connected grid");

    commitPath(g, prices, usage, epsilon, rn.net->minWidth, r.path, rn.segs, rn.charges);
    reconcile(nodeLayer, e.first, r.path.front().l, p1, rn.segs);
    reconcile(nodeLayer, e.second, r.path.back().l, p2, rn.segs);
  }

  for (const Pin &p : rn.net->pins) {
    for (size_t i = 0; i < rn.pts.size(); ++i) {
      if (rn.pts[i].x == p.x && rn.pts[i].y == p.y) {
        reconcile(nodeLayer, static_cast<int>(i), p.layer, rn.pts[i], rn.segs);
        break;
      }
    }
  }
}

// Giant nets: same fixed-shape, usage-charging exemption as mroute's, using
// whichever tier's charging convention (the two are identical) since giant
// nets never search either way.
void routeGiantNet(const RoutingGrid &g, Usage &usage, const LayerPlan &lp, RoutedNet &rn) {
  std::vector<int> nodeLayer(rn.pts.size(), -1);
  auto chargeH = [&](int y, int a, int b, int l) {
    if (a == b)
      return;
    int amount = rn.net->minWidth + g.minSpacing[l];
    for (int x = a; x < b; ++x)
      usage.h[g.hIndex(x, y, l)] += amount;
    rn.charges.push_back({true, y, a, b, l, amount});
  };
  auto chargeV = [&](int x, int a, int b, int l) {
    if (a == b)
      return;
    int amount = rn.net->minWidth + g.minSpacing[l];
    for (int y = a; y < b; ++y)
      usage.v[g.vIndex(x, y, l)] += amount;
    rn.charges.push_back({false, x, a, b, l, amount});
  };
  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    int hl = lp.hLayers.front(), vl = lp.vLayers.front();
    int xa = std::min(p1.x, p2.x), xb = std::max(p1.x, p2.x);
    int ya = std::min(p1.y, p2.y), yb = std::max(p1.y, p2.y);
    int layerAtP1 = hl, layerAtP2 = vl;
    if (xa != xb) {
      chargeH(p1.y, xa, xb, hl);
      rn.segs.push_back({xa, p1.y, hl, xb, p1.y, hl});
    } else {
      layerAtP1 = layerAtP2 = (ya != yb) ? vl : hl;
    }
    if (ya != yb) {
      if (xa != xb)
        rn.segs.push_back({p2.x, p1.y, hl, p2.x, p1.y, vl});
      chargeV(p2.x, ya, yb, vl);
      rn.segs.push_back({p2.x, ya, vl, p2.x, yb, vl});
    }
    reconcile(nodeLayer, e.first, layerAtP1, p1, rn.segs);
    reconcile(nodeLayer, e.second, layerAtP2, p2, rn.segs);
  }
  for (const Pin &p : rn.net->pins) {
    for (size_t i = 0; i < rn.pts.size(); ++i) {
      if (rn.pts[i].x == p.x && rn.pts[i].y == p.y) {
        reconcile(nodeLayer, static_cast<int>(i), p.layer, rn.pts[i], rn.segs);
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// The loop: phase A (fast tier, exactly nproute's algorithm including its
// cleanup pass) then phase B (escalate whatever's still overflowed to the
// slow tier and keep negotiating).
// ---------------------------------------------------------------------------

long long totalOverflow(const RoutingGrid &g, const Usage &u) {
  (void)g; // capacity now lives in Usage (u.capH/capV); g kept for call-site consistency
  long long total = 0;
  for (size_t i = 0; i < u.h.size(); ++i)
    if (u.h[i] > u.capH[i])
      total += u.h[i] - u.capH[i];
  for (size_t i = 0; i < u.v.size(); ++i)
    if (u.v[i] > u.capV[i])
      total += u.v[i] - u.capV[i];
  return total;
}

bool netOverflowed(const RoutingGrid &g, const Usage &u, const RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal) {
      for (int x = c.a; x < c.b; ++x)
        if (u.h[g.hIndex(x, c.cross, c.layer)] > u.capH[g.hIndex(x, c.cross, c.layer)])
          return true;
    } else {
      for (int y = c.a; y < c.b; ++y)
        if (u.v[g.vIndex(c.cross, y, c.layer)] > u.capV[g.vIndex(c.cross, y, c.layer)])
          return true;
    }
  }
  return false;
}

long long overflowOverUnion(const RoutingGrid &g, const Usage &u,
                             const std::vector<ChargeRecord> &a,
                             const std::vector<ChargeRecord> &b) {
  std::vector<std::pair<bool, size_t>> edges;
  auto add = [&](const std::vector<ChargeRecord> &charges) {
    for (const ChargeRecord &c : charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          edges.push_back({true, g.hIndex(x, c.cross, c.layer)});
      else
        for (int y = c.a; y < c.b; ++y)
          edges.push_back({false, g.vIndex(c.cross, y, c.layer)});
    }
  };
  add(a);
  add(b);
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  long long total = 0;
  for (auto &e : edges) {
    if (e.first)
      total += std::max(0, u.h[e.second] - u.capH[e.second]);
    else
      total += std::max(0, u.v[e.second] - u.capV[e.second]);
  }
  return total;
}

// ---------------------------------------------------------------------------
// pd4route's own mechanism: NTHU-Route 2.0's refinement stage
// (Post_processing.cpp), run on the finished 2D solution before layer
// assignment. See the file header.
// ---------------------------------------------------------------------------

struct RefineSettings {
  bool enabled = true;
  int iters = 20;  // NTHU's iteration_p3
  int boxInit = 10; // NTHU's init_box_size_p3
  int boxInc = 15;  // NTHU's box_size_inc_p3
  AcceptMode accept = AcceptMode::kKeep; // NTHU keeps every reroute
  // pd5route: PD5ROUTE_REFINE_ORDER=shuffle reroutes each iteration's
  // overflowed nets in a seeded random order (seed PD5ROUTE_SEED, mixed with
  // the iteration) instead of NTHU's fixed (overflow, bbox) order, which on
  // newblue1 re-derived the same routes every iteration once it plateaued.
  bool shuffle = false;
  unsigned seed = 1;
  bool pieces = false; // pd6route: PD6ROUTE_PIECES
  int plateau = 0;            // pd12route: PD12ROUTE_REFINE_PLATEAU (0 = off)
  double plateauGain = 0.01;  // pd12route: PD12ROUTE_PLATEAU_GAIN / 100
  long long plateauMin = 0;   // pd12route: PD12ROUTE_PLATEAU_MIN
  int boxCap = 0;             // pd13route: PD13ROUTE_*_BOX_CAP (0 = uncapped)
};

// pd5route: parses PD5ROUTE_REFINE_ORDER (nthu | shuffle; unset = nthu) and
// PD5ROUTE_SEED (integer >= 0; unset = 1). Fails loudly on anything else.
bool parseRefineOrder(const char *order, const char *seed, RefineSettings &r, std::string &err) {
  if (order) {
    std::string o(order);
    if (o == "nthu")
      r.shuffle = false;
    else if (o == "shuffle")
      r.shuffle = true;
    else {
      err = "PD5ROUTE_REFINE_ORDER must be nthu or shuffle (got '" + o + "')";
      return false;
    }
  }
  if (seed) {
    char *end = nullptr;
    long v = std::strtol(seed, &end, 10);
    if (end == seed || *end != '\0' || v < 0 || v > 2000000000L) {
      err = std::string("PD5ROUTE_SEED must be an integer >= 0 (got '") + seed + "')";
      return false;
    }
    r.seed = static_cast<unsigned>(v);
  }
  return true;
}

// PD4ROUTE_REFINE (0|1), _ITERS (>= 0), _BOX_INIT (>= 1), _BOX_INC (>= 0),
// _ACCEPT (keep|revert). Unset = NTHU's values above. Fails loudly.
bool parseRefineSettings(const char *enabled, const char *iters, const char *boxInit,
                         const char *boxInc, const char *accept, RefineSettings &out,
                         std::string &err) {
  RefineSettings r;
  if (enabled) {
    std::string e(enabled);
    if (e == "1")
      r.enabled = true;
    else if (e == "0")
      r.enabled = false;
    else {
      err = "PD4ROUTE_REFINE must be 0 or 1 (got '" + e + "')";
      return false;
    }
  }
  auto parseInt = [&](const char *v, const char *name, int lo, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > 1000000) {
      err = std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!parseInt(iters, "PD4ROUTE_REFINE_ITERS", 0, r.iters) ||
      !parseInt(boxInit, "PD4ROUTE_REFINE_BOX_INIT", 1, r.boxInit) ||
      !parseInt(boxInc, "PD4ROUTE_REFINE_BOX_INC", 0, r.boxInc))
    return false;
  if (accept) {
    std::string a(accept);
    if (a == "keep")
      r.accept = AcceptMode::kKeep;
    else if (a == "revert")
      r.accept = AcceptMode::kRevert;
    else {
      err = "PD4ROUTE_REFINE_ACCEPT must be keep or revert (got '" + a + "')";
      return false;
    }
  }
  out = r;
  return true;
}

// Sum over a net's charged 2D tile edges of that edge's overflow -- NTHU's
// per-path total_overflow (an edge shared by several of this net's pieces
// is counted each time, as NTHU counts per 2-pin path).
long long netOverflowAmount(const RoutingGrid &g, const Usage &u, const RoutedNet &rn) {
  long long total = 0;
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal) {
      for (int x = c.a; x < c.b; ++x) {
        size_t i = g.hIndex(x, c.cross, c.layer);
        total += std::max(0, u.h[i] - u.capH[i]);
      }
    } else {
      for (int y = c.a; y < c.b; ++y) {
        size_t i = g.vIndex(c.cross, y, c.layer);
        total += std::max(0, u.v[i] - u.capV[i]);
      }
    }
  }
  return total;
}

// NTHU's refinement order (Post_processing.cpp: COUNTER sorted descending by
// (total_overflow, bsize)): most overflow first, then the larger pin
// bounding box. Only nets with overflow > 0 are returned. Stable.
std::vector<size_t> refineOrder(const std::vector<long long> &overflow, const std::vector<int> &bbox) {
  std::vector<size_t> order;
  for (size_t i = 0; i < overflow.size(); ++i)
    if (overflow[i] > 0)
      order.push_back(i);
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (overflow[a] != overflow[b])
      return overflow[a] > overflow[b];
    return bbox[a] > bbox[b];
  });
  return order;
}

// The order refine2D reroutes in, iteration `it`: NTHU's, or (shuffle) the
// same set in a seeded permutation that differs per iteration and is
// reproducible run to run.
std::vector<size_t> refineOrderFor(const std::vector<long long> &overflow, const std::vector<int> &bbox,
                                   const RefineSettings &rs, int it) {
  std::vector<size_t> order = refineOrder(overflow, bbox);
  if (rs.shuffle) {
    std::mt19937 rng(rs.seed * 1000003u + static_cast<unsigned>(it));
    std::shuffle(order.begin(), order.end(), rng);
  }
  return order;
}

// Gives nets read back from a route file (readRouteAs2D: 2D wire segs
// only) what the 2D stage's own nets carry: unique pin tiles as pts, the
// giant flag, and 2D charges for every wire seg (amount = width + layer-0
// spacing, commitPath's own rule on the synthetic layer).
void prepareFor2DRefine(const Design &d, std::vector<RoutedNet> &nets) {
  const RoutingGrid &g = d.grid;
  for (RoutedNet &rn : nets) {
    rn.pts.clear();
    for (const Pin &p : rn.net->pins) {
      bool dup = false;
      for (const Point &q : rn.pts)
        dup = dup || (q.x == p.x && q.y == p.y);
      if (!dup)
        rn.pts.push_back({p.x, p.y});
    }
    rn.giant = rn.net->pins.size() > kGiantNetPins;
    rn.charges.clear();
    int amount = rn.net->minWidth + g.minSpacing[0];
    for (const Seg &sg : rn.segs) {
      if (sg.l1 != sg.l2 || (sg.x1 == sg.x2 && sg.y1 == sg.y2))
        continue;
      bool h = sg.y1 == sg.y2;
      int lo = h ? std::min(sg.x1, sg.x2) : std::min(sg.y1, sg.y2);
      int hi = h ? std::max(sg.x1, sg.x2) : std::max(sg.y1, sg.y2);
      rn.charges.push_back({h, h ? sg.y1 : sg.x1, lo, hi, 0, amount});
    }
  }
}

Usage usageFromCharges(const RoutingGrid &g, const std::vector<RoutedNet> &nets) {
  Usage u(g);
  for (const RoutedNet &rn : nets)
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          u.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      else
        for (int y = c.a; y < c.b; ++y)
          u.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
    }
  return u;
}

// pd13route: a stage's search margin in round k -- init + k * step, never
// above cap when cap > 0 (computed in 64 bits, so a long run cannot wrap).
int stageMargin(int init, int step, int k, int cap) {
  long long m = static_cast<long long>(init) + static_cast<long long>(step) * k;
  if (cap > 0 && m > cap)
    m = cap;
  return static_cast<int>(std::min<long long>(m, std::numeric_limits<int>::max()));
}

// pd13route: PD13ROUTE_REGION_BOX_CAP and PD13ROUTE_REFINE_BOX_CAP, integers
// >= 0 (0 = uncapped). Unset leaves a value as it is.
bool parseBoxCaps(const char *region, const char *refine, int &regionCap, int &refineCap, std::string &err) {
  auto one = [&](const char *v, const char *name, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < 0 || x > 1000000) {
      err = std::string(name) + " must be an integer >= 0 (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  return one(region, "PD13ROUTE_REGION_BOX_CAP", regionCap) && one(refine, "PD13ROUTE_REFINE_BOX_CAP", refineCap);
}

struct RefineReport {
  long long before = 0, after = 0;
  int itersRun = 0;
  bool plateaued = false; // pd12route: stopped by the plateau rule
  double seconds = 0;     // pd12route: stage wall time
  int lastMargin = 0;     // pd13route: the margin of the last round run
};

// pd12route: the plateau rule. bestAfter[k] is the best overflow after k
// rounds (bestAfter[0] = the stage's starting overflow). With window N > 0,
// the stage stops after round k >= N when the best has not fallen by at
// least max(1, gain * bestAfter[k - N]) over those N rounds -- and only
// while that best is still >= minOverflow. Window 0: never.
bool plateauReached(const std::vector<long long> &bestAfter, int window, double gain, long long minOverflow = 0) {
  if (window <= 0 || bestAfter.size() < static_cast<size_t>(window) + 1)
    return false;
  size_t k = bestAfter.size() - 1;
  long long then = bestAfter[k - window], now = bestAfter[k];
  if (now < minOverflow)
    return false;
  double need = std::max(1.0, gain * static_cast<double>(then));
  return static_cast<double>(then - now) < need;
}

// pd12route: PD12ROUTE_REGION_PLATEAU, PD12ROUTE_REFINE_PLATEAU (rounds,
// >= 0) and PD12ROUTE_PLATEAU_GAIN (percent, 0-100). Unset leaves a value
// as it is.
bool parsePlateauSettings(const char *region, const char *refine, const char *gain, const char *minOverflow,
                          int &regionWindow, int &refineWindow, double &gainFrac, long long &minOut,
                          std::string &err) {
  auto window = [&](const char *v, const char *name, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < 0 || x > 1000000) {
      err = std::string(name) + " must be an integer >= 0 (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!window(region, "PD12ROUTE_REGION_PLATEAU", regionWindow) ||
      !window(refine, "PD12ROUTE_REFINE_PLATEAU", refineWindow))
    return false;
  if (gain) {
    char *end = nullptr;
    double x = std::strtod(gain, &end);
    if (end == gain || *end != '\0' || !(x >= 0.0 && x <= 100.0)) {
      err = std::string("PD12ROUTE_PLATEAU_GAIN must be a percentage in [0, 100] (got '") + gain + "')";
      return false;
    }
    gainFrac = x / 100.0;
  }
  if (minOverflow) {
    char *end = nullptr;
    long long x = std::strtoll(minOverflow, &end, 10);
    if (end == minOverflow || *end != '\0' || x < 0) {
      err = std::string("PD12ROUTE_PLATEAU_MIN must be an integer >= 0 (got '") + minOverflow + "')";
      return false;
    }
    minOut = x;
  }
  return true;
}

// NTHU's refinement, on whole nets (this project's MSMV reroutes a net, not
// a 2-pin piece): each iteration, every non-giant net whose route crosses
// an overflowed edge -- most overflow first, then largest bounding box --
// is ripped up and regrown by MSMV under refineCost (fewest full edges;
// length only breaks ties), with a search-box margin of boxInit + it *
// boxInc. kKeep keeps every reroute (NTHU); kRevert undoes one whose local
// overflow got worse. The best whole-grid state seen is what `nets` ends
// holding.
// pd6route's own mechanism: 2-pin-piece reroutes (defined after the tree
// helpers it needs, buildNetTree/splitToUnitEdges; see its definition).
struct TileBox;
struct PieceStats {
  bool ok = true; // false: the net's wiring is not a tree -- caller falls back to a whole-net reroute
  int pieces = 0, reverted = 0;
  int patterned = 0; // pd10route: pieces rerouted by an L/Z shape, no maze search
};

// pd10route: PD10ROUTE_PATTERN_FIRST (set once in main()).
bool patternFirst = false;
// pd24route: per thread; worker threads' shares are added to the main
// thread's in commit order.
thread_local long long patternedTotal = 0, piecesTotal = 0; // run-wide counts for the report line
PieceStats rerouteNetPieces(const RoutingGrid &g, Usage &usage, const Prices &prices, const LayerPlan &lp,
                            SearchRegion &region, RoutedNet &rn, int margin, AcceptMode accept,
                            const TileBox *box);

// pd24route: batched parallel rerouting (see the file header).
void addCharge(const RoutingGrid &g, Usage &u, const ChargeRecord &c) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x)
      u.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
  } else {
    for (int y = c.a; y < c.b; ++y)
      u.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
  }
}

bool sameCharges(const std::vector<ChargeRecord> &a, const std::vector<ChargeRecord> &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].horizontal != b[i].horizontal || a[i].cross != b[i].cross || a[i].a != b[i].a || a[i].b != b[i].b ||
        a[i].layer != b[i].layer || a[i].amount != b[i].amount)
      return false;
  return true;
}

// Per-task counters a caller can use as it likes (summed over a round).
struct TaskStats {
  long long n[6] = {0, 0, 0, 0, 0, 0};
  TaskStats &operator+=(const TaskStats &o) {
    for (int i = 0; i < 6; ++i)
      n[i] += o.n[i];
    return *this;
  }
};

struct ParallelWorker {
  Usage usage;
  Prices prices;
  SearchRegion region;
  LayerPlan lp;
  ParallelWorker(const RoutingGrid &g, const Usage &u, const Prices &p) : usage(u), prices(p), lp(g) {
    prices.live = &usage;
  }
};

struct BatchReport {
  long long batches = 0, committed = 0, unchanged = 0, redone = 0;
  long long passes = 0; // pd25route: extra passes over conflicting nets
};

// One round of `task` over the nets in `order`, in batches. `task(usage,
// prices, region, lp, rn)` must change nothing but `rn` and the usage it is
// given (by charging and undoing rn's routes); it returns its counters.
// `live` and `mainPrices` (whose `live` points at `live`) are the real
// state; worker replicas are rebuilt from them at the start of the call.
template <class Task>
TaskStats runBatchedRound(const RoutingGrid &g, std::vector<RoutedNet> &nets, const std::vector<size_t> &order,
                          Usage &live, Prices &mainPrices, SearchRegion &mainRegion, const LayerPlan &lp,
                          const ParallelSettings &ps, Task task, BatchReport &br) {
  TaskStats total;
  if (order.empty())
    return total;
  const int T = std::max(1, ps.threads);
  const size_t B = static_cast<size_t>(std::max(1, ps.batch));
  std::vector<std::unique_ptr<ParallelWorker>> workers;
  for (int t = 0; t < T; ++t)
    workers.push_back(std::make_unique<ParallelWorker>(g, live, mainPrices));
  std::vector<int> dirtyH(live.h.size(), -1), dirtyV(live.v.size(), -1);
  struct Delta {
    std::vector<ChargeRecord> oldC, newC;
  };
  int stampCounter = 0;
  // pd27route: each pass's net change per edge (valid where dStamp == the
  // pass's stamp), for the "fresh" conflict rule.
  std::vector<int> dStampH(live.h.size(), -1), dStampV(live.v.size(), -1);
  std::vector<int> passDH(live.h.size(), 0), passDV(live.v.size(), 0);
  size_t nextFresh = 0;
  std::vector<size_t> carried;
  while (nextFresh < order.size() || !carried.empty()) {
    // pd25route without carry: a batch, then passes over its conflicting
    // nets. pd27route with carry: conflicting nets lead the next batch.
    std::vector<size_t> pending;
    bool firstPass = true;
    if (!ps.carry && !carried.empty()) {
      pending = std::move(carried);
      firstPass = false;
    } else {
      pending = std::move(carried);
      while (pending.size() < B && nextFresh < order.size())
        pending.push_back(order[nextFresh++]);
    }
    carried.clear();
    if (firstPass)
      ++br.batches;
    else
      ++br.passes;
    {
    const size_t n = pending.size();
    const int stamp = stampCounter++;
    std::vector<size_t> conflicted;
    std::vector<RoutedNet> out(n);
    std::vector<TaskStats> st(n);
    std::vector<long long> pops(n, 0), pieces(n, 0), patterned(n, 0);
    std::atomic<size_t> next{0};
    auto work = [&](ParallelWorker &w) {
      for (;;) {
        size_t k = next.fetch_add(1);
        if (k >= n)
          break;
        size_t i = pending[k];
        RoutedNet tmp = nets[i];
        long long p0 = gSearchPops, pc0 = piecesTotal, pt0 = patternedTotal;
        st[k] = task(w.usage, w.prices, w.region, w.lp, tmp);
        pops[k] = gSearchPops - p0;
        pieces[k] = piecesTotal - pc0;
        patterned[k] = patternedTotal - pt0;
        for (const ChargeRecord &c : tmp.charges)
          undoCharge(g, w.usage, c);
        for (const ChargeRecord &c : nets[i].charges)
          addCharge(g, w.usage, c);
        out[k] = std::move(tmp);
      }
    };
    if (T == 1) {
      work(*workers[0]);
    } else {
      std::vector<std::thread> th;
      for (int t = 0; t < T; ++t)
        th.emplace_back(work, std::ref(*workers[t]));
      for (std::thread &x : th)
        x.join();
    }
    // Commit in net order.
    std::vector<Delta> deltas;
    for (size_t k = 0; k < n; ++k) {
      size_t i = pending[k];
      RoutedNet &rn = nets[i];
      if (sameCharges(rn.charges, out[k].charges)) {
        rn = std::move(out[k]); // flags and margins still carry over
        gSearchPops += pops[k];
        piecesTotal += pieces[k];
        patternedTotal += patterned[k];
        total += st[k];
        ++br.unchanged;
        continue;
      }
      for (const ChargeRecord &c : rn.charges)
        undoCharge(g, live, c);
      bool conflict = false;
      // pd27route: under "fresh", only an edge that had room at the pass's
      // start (live minus this pass's changes) counts.
      auto passD = [&](bool hz, size_t e) {
        return hz ? (dStampH[e] == stamp ? passDH[e] : 0) : (dStampV[e] == stamp ? passDV[e] : 0);
      };
      if (ps.redo)
        for (const ChargeRecord &c : out[k].charges) {
          if (c.horizontal) {
            for (int x = c.a; x < c.b && !conflict; ++x) {
              size_t e = g.hIndex(x, c.cross, c.layer);
              conflict = dirtyH[e] == stamp && live.h[e] + c.amount > live.capH[e] &&
                         (!ps.fresh || live.h[e] - passD(true, e) + c.amount <= live.capH[e]);
            }
          } else {
            for (int y = c.a; y < c.b && !conflict; ++y) {
              size_t e = g.vIndex(c.cross, y, c.layer);
              conflict = dirtyV[e] == stamp && live.v[e] + c.amount > live.capV[e] &&
                         (!ps.fresh || live.v[e] - passD(false, e) + c.amount <= live.capV[e]);
            }
          }
          if (conflict)
            break;
        }
      Delta dl;
      dl.oldC = rn.charges;
      if (conflict && ps.passes) {
        // pd25route: put the old route back and leave the net to the next pass.
        for (const ChargeRecord &c : rn.charges)
          addCharge(g, live, c);
        conflicted.push_back(i);
        ++br.redone;
        continue;
      }
      if (conflict) {
        for (const ChargeRecord &c : rn.charges)
          addCharge(g, live, c);
        st[k] = task(live, mainPrices, mainRegion, lp, rn);
        ++br.redone;
      } else {
        for (const ChargeRecord &c : out[k].charges)
          addCharge(g, live, c);
        rn = std::move(out[k]);
        gSearchPops += pops[k];
        piecesTotal += pieces[k];
        patternedTotal += patterned[k];
        ++br.committed;
      }
      total += st[k];
      dl.newC = rn.charges;
      // pd27route: this pass's per-edge change (old route out, new in).
      auto bump = [&](const ChargeRecord &c, int sign) {
        if (c.horizontal)
          for (int x = c.a; x < c.b; ++x) {
            size_t e = g.hIndex(x, c.cross, c.layer);
            if (dStampH[e] != stamp) {
              dStampH[e] = stamp;
              passDH[e] = 0;
            }
            passDH[e] += sign * c.amount;
          }
        else
          for (int y = c.a; y < c.b; ++y) {
            size_t e = g.vIndex(c.cross, y, c.layer);
            if (dStampV[e] != stamp) {
              dStampV[e] = stamp;
              passDV[e] = 0;
            }
            passDV[e] += sign * c.amount;
          }
      };
      for (const ChargeRecord &c : dl.oldC)
        bump(c, -1);
      for (const ChargeRecord &c : dl.newC)
        bump(c, +1);
      for (const ChargeRecord &c : dl.newC) {
        if (c.horizontal)
          for (int x = c.a; x < c.b; ++x)
            dirtyH[g.hIndex(x, c.cross, c.layer)] = stamp;
        else
          for (int y = c.a; y < c.b; ++y)
            dirtyV[g.vIndex(c.cross, y, c.layer)] = stamp;
      }
      deltas.push_back(std::move(dl));
    }
    // Sync the replicas with the batch's commits.
    auto sync = [&](ParallelWorker &w) {
      for (const Delta &dl : deltas) {
        for (const ChargeRecord &c : dl.oldC)
          undoCharge(g, w.usage, c);
        for (const ChargeRecord &c : dl.newC)
          addCharge(g, w.usage, c);
      }
    };
    if (T == 1 || deltas.empty()) {
      for (int t = 0; t < T; ++t)
        sync(*workers[t]);
    } else {
      std::vector<std::thread> th;
      for (int t = 0; t < T; ++t)
        th.emplace_back(sync, std::ref(*workers[t]));
      for (std::thread &x : th)
        x.join();
    }
    if (ps.checkReplicas)
      for (int t = 0; t < T; ++t)
        if (workers[t]->usage.h != live.h || workers[t]->usage.v != live.v)
          fail("pd24route: a worker's usage replica diverged from the live state after a batch");
    if (!conflicted.empty() && conflicted.size() == n)
      fail("pd25route: a pass committed nothing (its first net cannot conflict)");
    carried = std::move(conflicted);
    }
  }
  return total;
}

RefineReport refine2D(const Design &d, std::vector<RoutedNet> &nets, const RefineSettings &rs,
                      bool trace) {
  const RoutingGrid &g = d.grid;
  RefineReport rep;
  Usage usage = usageFromCharges(g, nets);
  Prices prices(g);
  prices.model = CostModel::kRefine;
  prices.live = &usage;
  LayerPlan lp(g);
  SearchRegion region;
  auto stageStart = std::chrono::steady_clock::now();
  long long total = totalOverflow(g, usage);
  rep.before = rep.after = total;
  std::vector<long long> bestAfter{rep.after};
  std::vector<RoutedNet> best = nets;
  for (int it = 0; it < rs.iters && total > 0; ++it) {
    std::vector<long long> ov(nets.size(), 0);
    std::vector<int> bbox(nets.size(), 0);
    for (size_t i = 0; i < nets.size(); ++i) {
      const RoutedNet &rn = nets[i];
      if (rn.giant || rn.pts.size() < 2)
        continue;
      ov[i] = netOverflowAmount(g, usage, rn);
      int x0 = rn.pts[0].x, x1 = x0, y0 = rn.pts[0].y, y1 = y0;
      for (const Point &p : rn.pts) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
      }
      bbox[i] = (x1 - x0) + (y1 - y0);
    }
    int margin = stageMargin(rs.boxInit, rs.boxInc, it, rs.boxCap);
    rep.lastMargin = margin;
    int rerouted = 0, reverted = 0;
    for (size_t i : refineOrderFor(ov, bbox, rs, it)) {
      RoutedNet &rn = nets[i];
      if (!netOverflowed(g, usage, rn))
        continue; // an earlier reroute this iteration already cleared it
      if (rs.pieces) {
        PieceStats ps = rerouteNetPieces(g, usage, prices, lp, region, rn, margin, rs.accept, nullptr);
        if (ps.ok) {
          rerouted += ps.pieces;
          reverted += ps.reverted;
          continue;
        }
      }
      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);
      rn.boxMargin = margin;
      routeNetSlowMSMV(g, prices, usage, lp, region, 0.0, rn);
      ++rerouted;
      long long localAfter = overflowOverUnion(g, usage, oldCharges, rn.charges);
      if (revertReroute(rs.accept, localBefore, localAfter)) {
        for (const ChargeRecord &c : rn.charges)
          undoCharge(g, usage, c);
        rn.charges = oldCharges;
        rn.segs = oldSegs;
        for (const ChargeRecord &c : rn.charges) {
          if (c.horizontal)
            for (int x = c.a; x < c.b; ++x)
              usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
          else
            for (int y = c.a; y < c.b; ++y)
              usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
        }
        ++reverted;
      }
    }
    total = totalOverflow(g, usage);
    rep.itersRun = it + 1;
    if (trace)
      std::cerr << "refine iter " << it << " margin " << margin << ": 2D overflow " << total
                << " (rerouted " << rerouted << ", reverted " << reverted << ")\n";
    if (total < rep.after) {
      rep.after = total;
      best = nets;
    }
    bestAfter.push_back(rep.after);
    if (total > 0 && plateauReached(bestAfter, rs.plateau, rs.plateauGain, rs.plateauMin)) {
      rep.plateaued = true;
      break;
    }
  }
  nets = std::move(best);
  rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stageStart).count();
  return rep;
}

// ---------------------------------------------------------------------------
// pd5route's own mechanism 2: hotspot rip-up. Every net touching the box
// around the remaining overflow is rerouted -- overflowed or not -- under
// pd2route's winning negotiation (present-demand pricing, every reroute
// kept), round after round for the nets still overflowed, and the best
// whole-grid state seen is kept. Refinement reroutes only overflowed nets;
// this also moves the legal nets packed around them.
// ---------------------------------------------------------------------------

// pd14route: the work-gated plateau rule. bestAfter[k] and popsAfter[k]
// are the best overflow and the cumulative search expansions after k rounds
// ([0] = at the stage's start). With window N > 0: stop after round k >= N
// when those N rounds expanded at least `pops` nodes and the best fell by
// less than max(1, gain * bestAfter[k - N]).
bool workGatedPlateau(const std::vector<long long> &bestAfter, const std::vector<long long> &popsAfter, int window,
                      long long pops, double gain) {
  if (window <= 0 || bestAfter.size() != popsAfter.size() || bestAfter.size() < static_cast<size_t>(window) + 1)
    return false;
  size_t k = bestAfter.size() - 1;
  if (popsAfter[k] - popsAfter[k - window] < pops)
    return false;
  return plateauReached(bestAfter, window, gain, 0);
}

// pd14route: PD14ROUTE_REGION_COST_WINDOW (rounds) and _COST_POPS
// (expansions), integers >= 0. Unset leaves a value as it is.
bool parseCostPlateau(const char *window, const char *pops, int &windowOut, long long &popsOut, std::string &err) {
  if (window) {
    char *end = nullptr;
    long x = std::strtol(window, &end, 10);
    if (end == window || *end != '\0' || x < 0 || x > 1000000) {
      err = std::string("PD14ROUTE_REGION_COST_WINDOW must be an integer >= 0 (got '") + window + "')";
      return false;
    }
    windowOut = static_cast<int>(x);
  }
  if (pops) {
    char *end = nullptr;
    long long x = std::strtoll(pops, &end, 10);
    if (end == pops || *end != '\0' || x < 0) {
      err = std::string("PD14ROUTE_REGION_COST_POPS must be an integer >= 0 (got '") + pops + "')";
      return false;
    }
    popsOut = x;
  }
  return true;
}

struct RegionSettings {
  bool enabled = false; // PD5ROUTE_REGION
  int expand = 5;       // PD5ROUTE_REGION_EXPAND: tiles added around the overflow's bbox
  int rounds = 60;      // PD5ROUTE_REGION_ROUNDS
  int boxInit = 10;     // PD5ROUTE_REGION_BOX_INIT: MSMV margin in round 0
  int boxGrow = 2;      // PD5ROUTE_REGION_BOX_GROW: added per round
  bool pieces = false;  // pd6route: PD6ROUTE_PIECES
  int costWindow = 0;         // pd14route: PD14ROUTE_REGION_COST_WINDOW (0 = off)
  long long costPops = 0;     // pd14route: PD14ROUTE_REGION_COST_POPS
  int plateau = 0;            // pd12route: PD12ROUTE_REGION_PLATEAU (0 = off)
  double plateauGain = 0.01;  // pd12route: PD12ROUTE_PLATEAU_GAIN / 100
  long long plateauMin = 0;   // pd12route: PD12ROUTE_PLATEAU_MIN
  int boxCap = 0;             // pd13route: PD13ROUTE_*_BOX_CAP (0 = uncapped)
  int vcapTracks = 0;         // pd22route: PD22ROUTE_VCAP (0 = off)
  double ringShare = 0;       // pd28route: PD28ROUTE_RING (0 = off)
  double ringCool = 0.75;     // pd28route: PD28ROUTE_RING_COOL
  int ringPull = 4;           // pd28route: PD28ROUTE_RING_PULL (tracks)
  int ringPasses = 1;         // pd28route: PD28ROUTE_RING_PASSES
  int ringMargin = 16;        // pd28route: PD28ROUTE_RING_MARGIN
  bool ringBox = false;       // pd28route: PD28ROUTE_RING_BOX
  double overPenalty = 0;     // pd29route: PD29ROUTE_OVER_PENALTY
  int vcapRadius = 5;         // pd22route: PD22ROUTE_VCAP_RADIUS
};

// pd22route: PD22ROUTE_VCAP (tracks, 0-1000) and PD22ROUTE_VCAP_RADIUS
// (tiles, 0-1000); unset keeps the defaults. Fails loudly on anything else.
bool parseVirtualCap(const char *tracks, const char *radius, RegionSettings &out, std::string &err) {
  auto one = [&](const char *v, const char *name, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < 0 || x > 1000) {
      err = std::string(name) + " must be an integer 0-1000 (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  int t = out.vcapTracks, r = out.vcapRadius;
  if (!one(tracks, "PD22ROUTE_VCAP", t) || !one(radius, "PD22ROUTE_VCAP_RADIUS", r))
    return false;
  out.vcapTracks = t;
  out.vcapRadius = r;
  return true;
}

// pd28route: the ring-pull settings; unset keeps the defaults (off).
bool parseRing(const char *share, const char *cool, const char *pull, const char *passes, const char *margin,
               const char *box, RegionSettings &out, std::string &err) {
  RegionSettings r = out;
  auto real = [&](const char *v, const char *name, double lo, double hi, double &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    double x = std::strtod(v, &end);
    if (end == v || *end != '\0' || !(x >= lo && x <= hi)) {
      err = std::string(name) + " must be a number in [" + std::to_string(lo) + ", " + std::to_string(hi) + "] (got '" +
            v + "')";
      return false;
    }
    dst = x;
    return true;
  };
  auto num = [&](const char *v, const char *name, long lo, long hi, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > hi) {
      err = std::string(name) + " must be an integer " + std::to_string(lo) + "-" + std::to_string(hi) + " (got '" + v +
            "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!real(share, "PD28ROUTE_RING", 0, 1, r.ringShare) || !real(cool, "PD28ROUTE_RING_COOL", 0, 1, r.ringCool) ||
      !num(pull, "PD28ROUTE_RING_PULL", 0, 1000, r.ringPull) || !num(passes, "PD28ROUTE_RING_PASSES", 1, 100, r.ringPasses) ||
      !num(margin, "PD28ROUTE_RING_MARGIN", 1, 1000, r.ringMargin))
    return false;
  if (box) {
    std::string b(box);
    if (b != "0" && b != "1") {
      err = "PD28ROUTE_RING_BOX must be 0 or 1 (got '" + b + "')";
      return false;
    }
    r.ringBox = b == "1";
  }
  out = r;
  return true;
}

bool parseRegionSettings(const char *enabled, const char *expand, const char *rounds, const char *boxInit,
                         const char *boxGrow, RegionSettings &out, std::string &err) {
  RegionSettings r;
  if (enabled) {
    std::string e(enabled);
    if (e == "1")
      r.enabled = true;
    else if (e == "0")
      r.enabled = false;
    else {
      err = "PD5ROUTE_REGION must be 0 or 1 (got '" + e + "')";
      return false;
    }
  }
  auto parseInt = [&](const char *v, const char *name, int lo, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > 1000000) {
      err = std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!parseInt(expand, "PD5ROUTE_REGION_EXPAND", 0, r.expand) ||
      !parseInt(rounds, "PD5ROUTE_REGION_ROUNDS", 0, r.rounds) ||
      !parseInt(boxInit, "PD5ROUTE_REGION_BOX_INIT", 1, r.boxInit) ||
      !parseInt(boxGrow, "PD5ROUTE_REGION_BOX_GROW", 0, r.boxGrow))
    return false;
  out = r;
  return true;
}

struct TileBox {
  int x0, y0, x1, y1; // inclusive tile bounds
};

bool tileInBox(const TileBox *box, int x, int y) {
  return x >= box->x0 && x <= box->x1 && y >= box->y0 && y <= box->y1;
}

// The bounding box of every overflowed 2D edge's tiles (both ends),
// expanded by `expand` and clipped to the grid. False when nothing
// overflows.
bool overflowRegion(const RoutingGrid &g, const Usage &u, int expand, TileBox &box) {
  bool any = false;
  box = {g.nx, g.ny, -1, -1};
  auto add = [&](int x, int y) {
    box.x0 = std::min(box.x0, x);
    box.y0 = std::min(box.y0, y);
    box.x1 = std::max(box.x1, x);
    box.y1 = std::max(box.y1, y);
    any = true;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      size_t i = g.hIndex(x, y, 0);
      if (u.h[i] > u.capH[i]) {
        add(x, y);
        add(x + 1, y);
      }
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      size_t i = g.vIndex(x, y, 0);
      if (u.v[i] > u.capV[i]) {
        add(x, y);
        add(x, y + 1);
      }
    }
  if (!any)
    return false;
  box.x0 = std::max(0, box.x0 - expand);
  box.y0 = std::max(0, box.y0 - expand);
  box.x1 = std::min(g.nx - 1, box.x1 + expand);
  box.y1 = std::min(g.ny - 1, box.y1 + expand);
  return true;
}

// Does any of this net's charged 2D tile edges have an end inside `box`?
bool netTouchesBox(const RoutedNet &rn, const TileBox &box) {
  auto in = [&](int x, int y) { return x >= box.x0 && x <= box.x1 && y >= box.y0 && y <= box.y1; };
  for (const ChargeRecord &c : rn.charges)
    for (int k = c.a; k <= c.b; ++k)
      if (c.horizontal ? in(k, c.cross) : in(c.cross, k))
        return true;
  return false;
}

struct RegionReport {
  long long before = 0, after = 0;
  int regionNets = 0, roundsRun = 0;
  TileBox box{0, 0, -1, -1};
  bool costStopped = false; // pd14route: stopped by the work-gated rule
  long long pops = 0;       // pd14route: search-node expansions in the stage
  bool plateaued = false; // pd12route: stopped by the plateau rule
  double seconds = 0;     // pd12route: stage wall time
  int lastMargin = 0;     // pd13route: the margin of the last round run
  long long vcapEdges = 0; // pd22route: 2D edges priced against a virtual capacity
  int vcapReduction = 0;   // pd22route: capacity units taken off each
  bool cancelled = false;  // pd22route: stopped by the fallback's cancel flag
  BatchReport batches;     // pd24route: batched-round bookkeeping
  // pd28route: the ring pull.
  TileBox ring{0, 0, -1, -1};
  long long ringPieces = 0, ringReverted = 0;
  double bandBefore = 0, bandAfter = 0, coreBefore = 0, coreAfter = 0; // band: mean use/cap of its warm edges
  long long warmEdges = 0;
  long long overflowBeforePull = 0, overflowAfterPull = 0;
  double ringSeconds = 0;
};

// pd28route: each tile's worst 2D edge use / capacity (an edge with no
// capacity counts 2 if used, 0 if not).
std::vector<float> tileWorstRatio(const RoutingGrid &g, const Usage &u) {
  std::vector<float> t(static_cast<size_t>(g.nx) * g.ny, 0.0f);
  auto put = [&](int x, int y, long long use, long long cap) {
    float r = cap > 0 ? static_cast<float>(static_cast<double>(use) / cap) : (use > 0 ? 2.0f : 0.0f);
    float &v = t[static_cast<size_t>(y) * g.nx + x];
    v = std::max(v, r);
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      size_t i = g.hIndex(x, y, 0);
      put(x, y, u.h[i], u.capH[i]);
      put(x + 1, y, u.h[i], u.capH[i]);
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      size_t i = g.vIndex(x, y, 0);
      put(x, y, u.v[i], u.capV[i]);
      put(x, y + 1, u.v[i], u.capV[i]);
    }
  return t;
}

// pd28route: grow a rectangle from `core`, each side outward until the
// tiles along it are cool (ratio <= cool) for at least `share` of them, or
// it reaches the die edge (hotring.cpp's rule, Part 94).
TileBox ringRect(const RoutingGrid &g, const std::vector<float> &ratio, const TileBox &core, double cool,
                 double share) {
  TileBox r = core;
  auto coolShare = [&](int side) {
    long long n = 0, ok = 0;
    if (side < 2) {
      int x = side == 0 ? r.x0 : r.x1;
      for (int y = r.y0; y <= r.y1; ++y, ++n)
        ok += ratio[static_cast<size_t>(y) * g.nx + x] <= cool;
    } else {
      int y = side == 2 ? r.y0 : r.y1;
      for (int x = r.x0; x <= r.x1; ++x, ++n)
        ok += ratio[static_cast<size_t>(y) * g.nx + x] <= cool;
    }
    return n ? static_cast<double>(ok) / n : 1.0;
  };
  bool done[4] = {false, false, false, false};
  for (;;) {
    bool moved = false;
    for (int s = 0; s < 4; ++s) {
      if (done[s])
        continue;
      bool atCore = (s == 0 && r.x0 == core.x0) || (s == 1 && r.x1 == core.x1) || (s == 2 && r.y0 == core.y0) ||
                    (s == 3 && r.y1 == core.y1);
      if (!atCore && coolShare(s) >= share) {
        done[s] = true;
        continue;
      }
      int &e = s == 0 ? r.x0 : s == 1 ? r.x1 : s == 2 ? r.y0 : r.y1;
      int lim = s == 0 ? 0 : s == 1 ? g.nx - 1 : s == 2 ? 0 : g.ny - 1;
      if (e == lim) {
        done[s] = true;
        continue;
      }
      e += (s == 0 || s == 2) ? -1 : 1;
      moved = true;
    }
    if (!moved)
      break;
  }
  return r;
}

// pd28route: mean use / capacity over the 2D edges with an end tile in
// `box` (and, when `exclude` is set, not in `exclude`).
double meanEdgeRatio(const RoutingGrid &g, const Usage &u, const TileBox &box, const TileBox *exclude) {
  double sum = 0;
  long long n = 0;
  auto in = [&](const TileBox &b, int x, int y) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; };
  auto take = [&](int xa, int ya, int xb, int yb) {
    bool inBox = in(box, xa, ya) || in(box, xb, yb);
    bool inEx = exclude && (in(*exclude, xa, ya) || in(*exclude, xb, yb));
    return inBox && !inEx;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x)
      if (take(x, y, x + 1, y)) {
        size_t i = g.hIndex(x, y, 0);
        if (u.capH[i] > 0) {
          sum += static_cast<double>(u.h[i]) / u.capH[i];
          ++n;
        }
      }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x)
      if (take(x, y, x, y + 1)) {
        size_t i = g.vIndex(x, y, 0);
        if (u.capV[i] > 0) {
          sum += static_cast<double>(u.v[i]) / u.capV[i];
          ++n;
        }
      }
  return n ? sum / n : 0.0;
}

// pd22route: the virtual-capacity zone. Every 2D edge with an end tile
// within `radius` tiles (x and y) of a tile of an edge overflowed in `u`
// gets reduction `reduction`; every other edge 0. Returns the number of
// edges reduced (0 when reduction <= 0 or nothing overflows).
long long virtualCapZone(const RoutingGrid &g, const Usage &u, int reduction, int radius, std::vector<int> &redH,
                         std::vector<int> &redV) {
  redH.assign(u.h.size(), 0);
  redV.assign(u.v.size(), 0);
  if (reduction <= 0)
    return 0;
  std::vector<char> hot(static_cast<size_t>(g.nx) * g.ny, 0);
  auto mark = [&](int x, int y) {
    for (int yy = std::max(0, y - radius); yy <= std::min(g.ny - 1, y + radius); ++yy)
      for (int xx = std::max(0, x - radius); xx <= std::min(g.nx - 1, x + radius); ++xx)
        hot[static_cast<size_t>(yy) * g.nx + xx] = 1;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      size_t i = g.hIndex(x, y, 0);
      if (u.h[i] > u.capH[i]) {
        mark(x, y);
        mark(x + 1, y);
      }
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      size_t i = g.vIndex(x, y, 0);
      if (u.v[i] > u.capV[i]) {
        mark(x, y);
        mark(x, y + 1);
      }
    }
  auto isHot = [&](int x, int y) { return hot[static_cast<size_t>(y) * g.nx + x] != 0; };
  long long n = 0;
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x)
      if (isHot(x, y) || isHot(x + 1, y)) {
        redH[g.hIndex(x, y, 0)] = reduction;
        ++n;
      }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x)
      if (isHot(x, y) || isHot(x, y + 1)) {
        redV[g.vIndex(x, y, 0)] = reduction;
        ++n;
      }
  return n;
}

RegionReport regionNegotiate(const Design &d, std::vector<RoutedNet> &nets, const RegionSettings &rs,
                             bool trace, const std::atomic<bool> *cancel = nullptr) {
  const RoutingGrid &g = d.grid;
  RegionReport rep;
  auto stageStart = std::chrono::steady_clock::now();
  Usage usage = usageFromCharges(g, nets);
  rep.before = rep.after = totalOverflow(g, usage);
  if (!overflowRegion(g, usage, rs.expand, rep.box))
    return rep;
  if (rs.ringShare > 0) {
    // pd28route: the ring pull (see the file header).
    auto t0 = std::chrono::steady_clock::now();
    TileBox core;
    overflowRegion(g, usage, 0, core);
    std::vector<float> ratio = tileWorstRatio(g, usage);
    rep.ring = ringRect(g, ratio, core, rs.ringCool, rs.ringShare);
    const TileBox &coreBox = rep.box; // overflow box + expand
    rep.coreBefore = meanEdgeRatio(g, usage, coreBox, nullptr);
    rep.overflowBeforePull = totalOverflow(g, usage);
    Prices pa(g);
    CostSettings csa;
    csa.model = CostModel::kPresentDemand;
    configureCostModel(pa, usage, csa);
    const int unit = 1 + g.minSpacing[0];
    std::vector<int> redH(usage.h.size(), 0), redV(usage.v.size(), 0);
    std::vector<char> warmH(usage.h.size(), 0), warmV(usage.v.size(), 0); // the band's warm edges at the start
    auto inB = [](const TileBox &b, int x, int y) { return x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1; };
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x) {
        size_t i = g.hIndex(x, y, 0);
        if (inB(coreBox, x, y) || inB(coreBox, x + 1, y))
          redH[i] = 2 * rs.ringPull * unit;
        else if ((inB(rep.ring, x, y) || inB(rep.ring, x + 1, y)) && usage.capH[i] > 0 &&
                 usage.h[i] > rs.ringCool * usage.capH[i]) {
          redH[i] = rs.ringPull * unit;
          warmH[i] = 1;
        }
      }
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x) {
        size_t i = g.vIndex(x, y, 0);
        if (inB(coreBox, x, y) || inB(coreBox, x, y + 1))
          redV[i] = 2 * rs.ringPull * unit;
        else if ((inB(rep.ring, x, y) || inB(rep.ring, x, y + 1)) && usage.capV[i] > 0 &&
                 usage.v[i] > rs.ringCool * usage.capV[i]) {
          redV[i] = rs.ringPull * unit;
          warmV[i] = 1;
        }
      }
    pa.vredH = &redH;
    pa.vredV = &redV;
    auto warmMean = [&]() {
      double sum = 0;
      long long n = 0;
      for (size_t i = 0; i < warmH.size(); ++i)
        if (warmH[i]) {
          sum += static_cast<double>(usage.h[i]) / usage.capH[i];
          ++n;
        }
      for (size_t i = 0; i < warmV.size(); ++i)
        if (warmV[i]) {
          sum += static_cast<double>(usage.v[i]) / usage.capV[i];
          ++n;
        }
      rep.warmEdges = n;
      return n ? sum / n : 0.0;
    };
    rep.bandBefore = warmMean();
    LayerPlan lpa(g);
    SearchRegion rga;
    std::vector<size_t> order;
    for (size_t i = 0; i < nets.size(); ++i)
      if (!nets[i].giant && nets[i].pts.size() >= 2 && netTouchesBox(nets[i], rep.ring))
        order.push_back(i);
    const TileBox ringBoxCopy = rep.ring;
    for (int pass = 0; pass < rs.ringPasses; ++pass) {
      auto task = [&](Usage &u, Prices &p, SearchRegion &reg, const LayerPlan &lpw, RoutedNet &rn) {
        TaskStats s;
        PieceStats ps = rerouteNetPieces(g, u, p, lpw, reg, rn, rs.ringMargin, AcceptMode::kRevert, &ringBoxCopy);
        if (ps.ok) {
          s.n[0] += ps.pieces;
          s.n[1] += ps.reverted;
        }
        return s;
      };
      TaskStats ts;
      if (gParallel.threads > 0) {
        BatchReport br;
        ts = runBatchedRound(g, nets, order, usage, pa, rga, lpa, gParallel, task, br);
      } else {
        for (size_t i : order)
          ts += task(usage, pa, rga, lpa, nets[i]);
      }
      rep.ringPieces += ts.n[0];
      rep.ringReverted += ts.n[1];
    }
    rep.bandAfter = warmMean();
    rep.coreAfter = meanEdgeRatio(g, usage, coreBox, nullptr);
    rep.overflowAfterPull = totalOverflow(g, usage);
    rep.after = rep.overflowAfterPull;
    rep.ringSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (trace)
      std::cerr << "ring pull: ring x " << rep.ring.x0 << "-" << rep.ring.x1 << " y " << rep.ring.y0 << "-"
                << rep.ring.y1 << " (" << (rep.ring.x1 - rep.ring.x0 + 1) << " x " << (rep.ring.y1 - rep.ring.y0 + 1)
                << " of " << g.nx << " x " << g.ny << "), " << order.size() << " nets, " << rep.ringPieces
                << " pieces rerouted (" << rep.ringReverted << " reverted); the band's " << rep.warmEdges
                << " warm edges, mean use/cap " << rep.bandBefore
                << " -> " << rep.bandAfter << ", core " << rep.coreBefore << " -> " << rep.coreAfter
                << ", 2D overflow " << rep.overflowBeforePull << " -> " << rep.overflowAfterPull << " ("
                << rep.ringSeconds << "s)\n";
    if (rs.ringBox)
      rep.box = rep.ring;
  }
  std::vector<long long> bestAfter{rep.after};
  long long popsAtStart = gSearchPops;
  std::vector<long long> popsAfter{gSearchPops};
  std::vector<size_t> members;
  for (size_t i = 0; i < nets.size(); ++i)
    if (!nets[i].giant && nets[i].pts.size() >= 2 && netTouchesBox(nets[i], rep.box))
      members.push_back(i);
  rep.regionNets = static_cast<int>(members.size());
  Prices prices(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(prices, usage, cs);
  prices.overPenalty = rs.overPenalty; // pd29route
  // pd22route: virtual capacity around the overflow the stage starts from.
  std::vector<int> vredH, vredV;
  if (rs.vcapTracks > 0) {
    rep.vcapReduction = rs.vcapTracks * (1 + g.minSpacing[0]);
    rep.vcapEdges = virtualCapZone(g, usage, rep.vcapReduction, rs.vcapRadius, vredH, vredV);
    prices.vredH = &vredH;
    prices.vredV = &vredV;
    if (trace)
      std::cerr << "region virtual capacity: " << rep.vcapEdges << " edges within " << rs.vcapRadius
                << " tiles of the overflow priced " << rep.vcapReduction << " capacity units lower ("
                << rs.vcapTracks << " tracks)\n";
  }
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<std::pair<std::vector<ChargeRecord>, std::vector<Seg>>> best;
  for (size_t i : members)
    best.push_back({nets[i].charges, nets[i].segs});
  for (int r = 0; r < rs.rounds; ++r) {
    if (cancel && cancel->load()) {
      rep.cancelled = true;
      break;
    }
    int rerouted = 0;
    rep.lastMargin = stageMargin(rs.boxInit, rs.boxGrow, r, rs.boxCap);
    if (gParallel.threads > 0) {
      // pd24route: batched parallel round over the members overflowed at the
      // round's start (every member in round 0).
      std::vector<size_t> order;
      for (size_t i : members)
        if (r == 0 || netOverflowed(g, usage, nets[i]))
          order.push_back(i);
      const int margin = stageMargin(rs.boxInit, rs.boxGrow, r, rs.boxCap);
      const TileBox *pieceBox = r == 0 ? &rep.box : nullptr;
      auto task = [&](Usage &u, Prices &p, SearchRegion &reg, const LayerPlan &lpw, RoutedNet &rn) {
        TaskStats s;
        if (r > 0 && !netOverflowed(g, u, rn))
          return s;
        if (rs.pieces) {
          PieceStats ps = rerouteNetPieces(g, u, p, lpw, reg, rn, margin, AcceptMode::kKeep, pieceBox);
          if (ps.ok) {
            s.n[0] += ps.pieces;
            return s;
          }
        }
        rn.boxMargin = margin;
        routeNetSlowMSMV(g, p, u, lpw, reg, 0.0, rn);
        s.n[0] += 1;
        return s;
      };
      TaskStats ts = runBatchedRound(g, nets, order, usage, prices, region, lp, gParallel, task, rep.batches);
      rerouted = static_cast<int>(ts.n[0]);
    }
    for (size_t i : members) {
      if (gParallel.threads > 0)
        break;
      RoutedNet &rn = nets[i];
      if (r > 0 && !netOverflowed(g, usage, rn))
        continue;
      if (rs.pieces) {
        // Round 0: every piece with wire in the box; later: overflowed ones.
        PieceStats ps = rerouteNetPieces(g, usage, prices, lp, region, rn, stageMargin(rs.boxInit, rs.boxGrow, r, rs.boxCap),
                                         AcceptMode::kKeep, r == 0 ? &rep.box : nullptr);
        if (ps.ok) {
          rerouted += ps.pieces;
          continue;
        }
      }
      rn.boxMargin = stageMargin(rs.boxInit, rs.boxGrow, r, rs.boxCap);
      routeNetSlowMSMV(g, prices, usage, lp, region, 0.0, rn);
      ++rerouted;
    }
    advancePresentDemand(prices, usage);
    long long total = totalOverflow(g, usage);
    rep.roundsRun = r + 1;
    if (trace)
      std::cerr << "region round " << r << ": 2D overflow " << total << " (rerouted " << rerouted << ", pops "
                << gSearchPops - (popsAfter.empty() ? popsAtStart : popsAfter.back()) << ")\n";
    if (total < rep.after) {
      rep.after = total;
      for (size_t k = 0; k < members.size(); ++k)
        best[k] = {nets[members[k]].charges, nets[members[k]].segs};
    }
    if (total == 0)
      break;
    bestAfter.push_back(rep.after);
    popsAfter.push_back(gSearchPops);
    if (plateauReached(bestAfter, rs.plateau, rs.plateauGain, rs.plateauMin)) {
      rep.plateaued = true;
      break;
    }
    if (workGatedPlateau(bestAfter, popsAfter, rs.costWindow, rs.costPops, rs.plateauGain)) {
      rep.costStopped = true;
      break;
    }
  }
  for (size_t k = 0; k < members.size(); ++k) {
    nets[members[k]].charges = best[k].first;
    nets[members[k]].segs = best[k].second;
  }
  rep.pops = gSearchPops - popsAtStart;
  rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stageStart).count();
  return rep;
}

struct RunReport {
  int slowReclaims = 0; // pd18route: reclaim passes run inside the slow tier
  std::vector<RoutedNet> nets;
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1;
  int roundsRun = 0;
  int escalatedCount = 0;
  bool convergedToZero = false;
  bool stalled = false;
  BatchReport batches; // pd24route: batched-round bookkeeping (slow tier)
  double secondsBeforeSlow = 0, secondsSlow = 0; // pd27route: route()'s own split
};

// nproute's post-convergence cleanup pass, unmodified: one supervised sweep
// per net under a far more relaxed threshold than normal routing uses,
// reverting on the spot (checked locally via overflowOverUnion, not by
// rescanning the whole grid) if a net's change made things worse. Runs
// before phase B's escalation decision -- a net only gets marked overflowed
// for escalation once the fast tier's best effort, cleanup included, still
// couldn't legalize it.
void relaxedCleanup(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                     double epsilon, RunReport &rep) {
  double relaxedThreshold = [] {
    const char *t = std::getenv("VROUTE_CLEANUP_THRESHOLD");
    return t ? std::atof(t) : 0.5;
  }();
  int passes = [] {
    const char *p = std::getenv("VROUTE_CLEANUP_PASSES");
    return p ? std::atoi(p) : 30;
  }();
  bool trace = std::getenv("VROUTE_TRACE") != nullptr;
  long long before = rep.bestOverflow;

  for (int pass = 0; pass < passes; ++pass) {
    for (RoutedNet &rn : rep.nets) {
      if (rn.giant)
        continue;
      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);

      routeNetFast(g, prices, usage, lp, epsilon, relaxedThreshold, rn);

      long long localAfter = overflowOverUnion(g, usage, oldCharges, rn.charges);
      if (localAfter > localBefore) {
        for (const ChargeRecord &c : rn.charges)
          undoCharge(g, usage, c);
        rn.charges = oldCharges;
        rn.segs = oldSegs;
        for (const ChargeRecord &c : rn.charges) {
          if (c.horizontal)
            for (int x = c.a; x < c.b; ++x)
              usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
          else
            for (int y = c.a; y < c.b; ++y)
              usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
        }
      }
    }
  }

  rep.bestOverflow = totalOverflow(g, usage);
  if (trace)
    std::cerr << "cleanup pass: overflow " << before << " -> " << rep.bestOverflow << "\n";
}

constexpr int kFastPatience = 20;
constexpr int kSlowPatience = 5;

// ---------------------------------------------------------------------------
// pd11route's own mechanism: the batched pattern-routing stage (roadmap step
// 1; GPU_BACKEND_PLAN.md phase A). Replaces phase A's 60 whole-net L/Z fast
// rounds and the cleanup pass when PD11ROUTE_PATTERN=1.
//
// Each non-giant net's topology edges (rn.mst over rn.pts) are its 2-pin
// pieces. A round routes a set of pieces in color batches: pieces whose
// bounding boxes are disjoint on a coarse grid (PD11ROUTE_PATTERN_CELL
// tiles per cell, default 8) share a batch and are solved together against
// one frozen, fixed-point cost snapshot -- so no two pieces of a batch can
// see each other's stale prices on a shared edge, and each batch is exactly
// one IPatternBackend::solve call (CPU reference here; Metal/CUDA when built
// with GR_PATTERN_SELECT). Per batch: take its pieces off the grid, snapshot
// the present-demand edge costs (quantizeCost of wireCost, at the typical
// per-net amount width 1 + spacing), solve, commit the chosen shapes. Round
// 0 routes every piece; later rounds reroute only pieces crossing an
// overflowed edge, with present-demand history advancing each round (as
// pd2route's rounds do). At the end each net's wiring is the union of its
// pieces' tile edges.
// ---------------------------------------------------------------------------

// pd15route: wire vs pin-tile MST over the non-giant nets (the detour
// trace). Wire counts the tile edges of a net's charge records.
struct DetourStats {
  long long wire = 0, mst = 0;
  long long excessHigh = 0; // excess carried by nets with wire > 1.5 * MST
  long long netsHigh = 0, nets = 0;
};

long long pinTileMst(const std::vector<Point> &pts) {
  std::vector<Point> p;
  for (const Point &q : pts)
    if (std::none_of(p.begin(), p.end(), [&](const Point &r) { return r.x == q.x && r.y == q.y; }))
      p.push_back(q);
  size_t k = p.size();
  if (k < 2)
    return 0;
  std::vector<long long> best(k, std::numeric_limits<long long>::max());
  std::vector<char> in(k, 0);
  best[0] = 0;
  long long total = 0;
  for (size_t it = 0; it < k; ++it) {
    size_t u = k;
    for (size_t i = 0; i < k; ++i)
      if (!in[i] && (u == k || best[i] < best[u]))
        u = i;
    in[u] = 1;
    total += best[u];
    for (size_t i = 0; i < k; ++i)
      if (!in[i])
        best[i] = std::min(best[i], static_cast<long long>(std::abs(p[i].x - p[u].x) + std::abs(p[i].y - p[u].y)));
  }
  return total;
}

DetourStats detourStats(const std::vector<RoutedNet> &nets) {
  DetourStats st;
  for (const RoutedNet &rn : nets) {
    if (rn.giant || rn.pts.size() < 2)
      continue;
    long long w = 0;
    for (const ChargeRecord &c : rn.charges)
      w += c.b - c.a;
    long long m = pinTileMst(rn.pts);
    ++st.nets;
    st.wire += w;
    st.mst += m;
    if (2 * w > 3 * m) {
      ++st.netsHigh;
      st.excessHigh += w - m;
    }
  }
  return st;
}

bool detourTraceOn() {
  const char *v = std::getenv("PD15ROUTE_DETOUR_TRACE");
  return v && std::string(v) == "1";
}

void detourTrace(const char *stage, const std::vector<RoutedNet> &nets) {
  if (!detourTraceOn())
    return;
  DetourStats st = detourStats(nets);
  std::cerr << "detour after " << stage << ": wire " << st.wire << ", pin MST " << st.mst << ", excess "
            << (st.wire - st.mst) << " (" << (st.mst ? 100.0 * (st.wire - st.mst) / st.mst : 0.0) << "%); nets > 1.5x MST "
            << st.netsHigh << " of " << st.nets << " carry " << st.excessHigh << "\n";
}

// ---------------------------------------------------------------------------
// pd16route's own mechanism: the detour reclaim pass (see the header).
// ---------------------------------------------------------------------------

struct ReclaimSettings {
  bool enabled = false;   // PD16ROUTE_RECLAIM
  double ratio = 1.5;     // PD16ROUTE_RECLAIM_RATIO: wire > ratio * pin MST
  int minExcess = 4;      // PD16ROUTE_RECLAIM_MIN_EXCESS: and wire - MST >= this
  double mu = 8.0;        // PD16ROUTE_RECLAIM_MU: keep iff saved wire > mu * added overflow
  int margin = 16;        // PD16ROUTE_RECLAIM_MARGIN: MSMV box margin
  int passes = 1;         // PD16ROUTE_RECLAIM_PASSES
  int avoid = -1;         // pd17route: PD17ROUTE_RECLAIM_AVOID (-1 = off)
};

// pd18route: the slow tier's reclaim (set once in main()).
struct SlowReclaim {
  bool atStop = false; // PD18ROUTE_RECLAIM_AT_STOP
  int every = 0;       // PD18ROUTE_RECLAIM_EVERY
  ReclaimSettings settings;
};
SlowReclaim gSlowReclaim;

struct ReclaimReport {
  long long candidates = 0, kept = 0;
  long long avoided = 0; // pd17route: nets over the ratio skipped for being near overflow
  long long wireBefore = 0, wireAfter = 0, overflowBefore = 0, overflowAfter = 0;
  double seconds = 0;
};

long long netWire(const RoutedNet &rn) {
  long long w = 0;
  for (const ChargeRecord &c : rn.charges)
    w += c.b - c.a;
  return w;
}

ReclaimReport reclaimDetours(const Design &d, std::vector<RoutedNet> &nets, const ReclaimSettings &rs) {
  const RoutingGrid &g = d.grid;
  ReclaimReport rep;
  auto stageStart = std::chrono::steady_clock::now();
  Usage usage = usageFromCharges(g, nets);
  rep.overflowBefore = totalOverflow(g, usage);
  for (const RoutedNet &rn : nets)
    if (!rn.giant && rn.pts.size() >= 2)
      rep.wireBefore += netWire(rn);
  Prices prices(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(prices, usage, cs);
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<long long> mst(nets.size(), 0);
  for (size_t i = 0; i < nets.size(); ++i)
    if (!nets[i].giant && nets[i].pts.size() >= 2)
      mst[i] = pinTileMst(nets[i].pts);
  // pd17route: tiles within `avoid` of an edge overflowed at the start.
  std::vector<char> nearOverflow;
  if (rs.avoid >= 0) {
    nearOverflow.assign(static_cast<size_t>(g.nx) * g.ny, 0);
    auto markAround = [&](int x0, int y0, int x1, int y1) {
      for (int y = std::max(0, y0 - rs.avoid); y <= std::min(g.ny - 1, y1 + rs.avoid); ++y)
        for (int x = std::max(0, x0 - rs.avoid); x <= std::min(g.nx - 1, x1 + rs.avoid); ++x)
          nearOverflow[static_cast<size_t>(y) * g.nx + x] = 1;
    };
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x) {
        size_t i = g.hIndex(x, y, 0);
        if (usage.h[i] > usage.capH[i])
          markAround(x, y, x + 1, y);
      }
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x) {
        size_t i = g.vIndex(x, y, 0);
        if (usage.v[i] > usage.capV[i])
          markAround(x, y, x, y + 1);
      }
  }
  auto nearAnyOverflow = [&](const RoutedNet &rn) {
    if (nearOverflow.empty())
      return false;
    for (const ChargeRecord &c : rn.charges)
      for (int k = c.a; k <= c.b; ++k) {
        int x = c.horizontal ? k : c.cross, y = c.horizontal ? c.cross : k;
        if (nearOverflow[static_cast<size_t>(y) * g.nx + x])
          return true;
      }
    return false;
  };
  for (int pass = 0; pass < rs.passes; ++pass) {
    std::vector<std::pair<long long, size_t>> cand; // (excess, net)
    for (size_t i = 0; i < nets.size(); ++i) {
      const RoutedNet &rn = nets[i];
      if (rn.giant || rn.pts.size() < 2)
        continue;
      long long w = netWire(rn);
      if (static_cast<double>(w) > rs.ratio * static_cast<double>(mst[i]) && w - mst[i] >= rs.minExcess) {
        if (nearAnyOverflow(rn)) {
          ++rep.avoided;
          continue;
        }
        cand.push_back({w - mst[i], i});
      }
    }
    std::sort(cand.begin(), cand.end(), [](const auto &a, const auto &b) {
      return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    rep.candidates += static_cast<long long>(cand.size());
    for (const auto &[excess, i] : cand) {
      (void)excess;
      RoutedNet &rn = nets[i];
      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long wireBefore = netWire(rn);
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);
      int oldMargin = rn.boxMargin;
      rn.boxMargin = rs.margin;
      routeNetSlowMSMV(g, prices, usage, lp, region, 0.0, rn);
      rn.boxMargin = oldMargin;
      long long saved = wireBefore - netWire(rn);
      long long added = overflowOverUnion(g, usage, oldCharges, rn.charges) - localBefore;
      if (static_cast<double>(saved) > rs.mu * static_cast<double>(std::max(0LL, added)) && saved > 0) {
        ++rep.kept;
        continue;
      }
      for (const ChargeRecord &c : rn.charges)
        undoCharge(g, usage, c);
      rn.charges = oldCharges;
      rn.segs = oldSegs;
      for (const ChargeRecord &c : rn.charges) {
        if (c.horizontal)
          for (int x = c.a; x < c.b; ++x)
            usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
        else
          for (int y = c.a; y < c.b; ++y)
            usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
      }
    }
  }
  rep.overflowAfter = totalOverflow(g, usage);
  for (const RoutedNet &rn : nets)
    if (!rn.giant && rn.pts.size() >= 2)
      rep.wireAfter += netWire(rn);
  rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - stageStart).count();
  return rep;
}

// PD16ROUTE_RECLAIM (0/1), _RATIO (> 1), _MIN_EXCESS (>= 0), _MU (>= 0),
// _MARGIN (>= 1), _PASSES (>= 1). Unset leaves a value as it is.
bool parseReclaimSettings(const char *on, const char *ratio, const char *minExcess, const char *mu, const char *margin,
                          const char *passes, ReclaimSettings &out, std::string &err) {
  ReclaimSettings r = out;
  if (on) {
    std::string v(on);
    if (v != "0" && v != "1") {
      err = "PD16ROUTE_RECLAIM must be 0 or 1 (got '" + v + "')";
      return false;
    }
    r.enabled = v == "1";
  }
  auto num = [&](const char *v, const char *name, double lo, bool strict, double &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    double x = std::strtod(v, &end);
    if (end == v || *end != '\0' || !std::isfinite(x) || (strict ? !(x > lo) : !(x >= lo))) {
      err = std::string(name) + " must be a number " + (strict ? "> " : ">= ") + std::to_string(lo) + " (got '" + v +
            "')";
      return false;
    }
    dst = x;
    return true;
  };
  double rat = r.ratio, mx = r.minExcess, m = r.mu, mg = r.margin, ps = r.passes;
  if (!num(ratio, "PD16ROUTE_RECLAIM_RATIO", 1.0, true, rat) ||
      !num(minExcess, "PD16ROUTE_RECLAIM_MIN_EXCESS", 0.0, false, mx) ||
      !num(mu, "PD16ROUTE_RECLAIM_MU", 0.0, false, m) || !num(margin, "PD16ROUTE_RECLAIM_MARGIN", 1.0, false, mg) ||
      !num(passes, "PD16ROUTE_RECLAIM_PASSES", 1.0, false, ps))
    return false;
  r.ratio = rat;
  r.minExcess = static_cast<int>(mx);
  r.mu = m;
  r.margin = static_cast<int>(mg);
  r.passes = static_cast<int>(ps);
  out = r;
  return true;
}

// pd17route: PD17ROUTE_RECLAIM_AVOID, an integer >= -1 (-1 = off).
bool parseReclaimAvoid(const char *v, ReclaimSettings &out, std::string &err) {
  if (!v)
    return true;
  char *end = nullptr;
  long x = std::strtol(v, &end, 10);
  if (end == v || *end != '\0' || x < -1 || x > 100000) {
    err = std::string("PD17ROUTE_RECLAIM_AVOID must be an integer >= -1 (got '") + v + "')";
    return false;
  }
  out.avoid = static_cast<int>(x);
  return true;
}

// pd18route: PD18ROUTE_RECLAIM_AT_STOP (0/1) and PD18ROUTE_RECLAIM_EVERY
// (integer >= 0, 0 = off). Unset leaves a value as it is.
bool parseSlowReclaim(const char *atStop, const char *every, SlowReclaim &out, std::string &err) {
  if (atStop) {
    std::string v(atStop);
    if (v != "0" && v != "1") {
      err = "PD18ROUTE_RECLAIM_AT_STOP must be 0 or 1 (got '" + v + "')";
      return false;
    }
    out.atStop = v == "1";
  }
  if (every) {
    char *end = nullptr;
    long x = std::strtol(every, &end, 10);
    if (end == every || *end != '\0' || x < 0 || x > 1000000) {
      err = std::string("PD18ROUTE_RECLAIM_EVERY must be an integer >= 0 (got '") + every + "')";
      return false;
    }
    out.every = static_cast<int>(x);
  }
  return true;
}

// pd19route: B (the reclaim path) is kept only if it is no worse on
// overflow and, when tied, shorter.
bool fallbackPrefersB(long long overflowA, long long wireA, long long overflowB, long long wireB) {
  return overflowB < overflowA || (overflowB == overflowA && wireB < wireA);
}

long long totalWire2D(const std::vector<RoutedNet> &nets) {
  long long w = 0;
  for (const RoutedNet &rn : nets)
    w += netWire(rn);
  return w;
}

struct FallbackReport {
  long long overflowA = 0, wireA = 0, overflowB = 0, wireB = 0;
  bool tookB = false;
  bool ranA = true; // pd20route: false when B reached zero first and A was skipped
  bool ranB = true; // pd21route: false when A ended above the threshold and B was skipped
  bool parallel = false; // pd22route: B ran on a second thread alongside A
  ReclaimReport reclaim;
  RegionReport regionA, regionB;
  RefineReport refineA, refineB;
};

// Runs both paths from the state in `nets` and leaves the kept one there.
FallbackReport reclaimWithFallback(const Design &d, std::vector<RoutedNet> &nets, const ReclaimSettings &reclaim,
                                   const RegionSettings &region, const RefineSettings &refine, bool trace,
                                   bool bFirst = false, long long aMax = -1, bool parallel = false,
                                   bool vcapB = true) {
  FallbackReport fr;
  const RoutingGrid &g = d.grid;
  std::vector<RoutedNet> b = nets;
  // pd23route: the reclaim path's region settings (no virtual capacity
  // unless vcapB).
  RegionSettings regionB = region;
  if (!vcapB)
    regionB.vcapTracks = 0;
  if (!vcapB)
    regionB.overPenalty = 0; // pd29route: the penalty rides with virtual capacity (path A only)
  auto runA = [&]() {
    if (region.enabled)
      fr.regionA = regionNegotiate(d, nets, region, trace);
    if (refine.enabled)
      fr.refineA = refine2D(d, nets, refine, trace);
  };
  auto runB = [&]() {
    ReclaimSettings rs = reclaim;
    rs.enabled = true;
    fr.reclaim = reclaimDetours(d, b, rs);
    if (region.enabled)
      fr.regionB = regionNegotiate(d, b, regionB, trace);
    if (refine.enabled)
      fr.refineB = refine2D(d, b, refine, trace);
  };
  if (aMax >= 0 && parallel) {
    // pd22route: B on a second thread from the start; cancelled (and
    // discarded) if A ends above the threshold. `nets` belongs to this
    // thread, `b` to B's; `d` is read-only; each writes its own fields of fr.
    std::atomic<bool> cancelB{false};
    std::thread tb([&]() {
      ReclaimSettings rs = reclaim;
      rs.enabled = true;
      fr.reclaim = reclaimDetours(d, b, rs);
      if (region.enabled)
        fr.regionB = regionNegotiate(d, b, regionB, false, &cancelB);
      if (refine.enabled && !cancelB.load())
        fr.refineB = refine2D(d, b, refine, false);
    });
    runA();
    fr.overflowA = totalOverflow(g, usageFromCharges(g, nets));
    fr.wireA = totalWire2D(nets);
    if (fr.overflowA > aMax)
      cancelB.store(true);
    tb.join();
    fr.parallel = true;
    if (fr.overflowA > aMax) {
      fr.ranB = false;
      fr.tookB = false;
      fr.reclaim = ReclaimReport{};
      fr.regionB = RegionReport{};
      fr.refineB = RefineReport{};
      return fr;
    }
  } else if (aMax >= 0) {
    // pd21route: A first; B only when A is at or below the threshold.
    runA();
    fr.overflowA = totalOverflow(g, usageFromCharges(g, nets));
    fr.wireA = totalWire2D(nets);
    if (fr.overflowA > aMax) {
      fr.ranB = false;
      fr.tookB = false;
      return fr;
    }
    runB();
  } else if (bFirst) {
    // pd20route: B first; at zero overflow A cannot do better on overflow.
    runB();
    fr.overflowB = totalOverflow(g, usageFromCharges(g, b));
    fr.wireB = totalWire2D(b);
    if (fr.overflowB == 0) {
      fr.ranA = false;
      fr.tookB = true;
      nets = std::move(b);
      return fr;
    }
    runA();
  } else {
    runA();
    runB();
  }
  fr.overflowA = totalOverflow(g, usageFromCharges(g, nets));
  fr.overflowB = totalOverflow(g, usageFromCharges(g, b));
  fr.wireA = totalWire2D(nets);
  fr.wireB = totalWire2D(b);
  fr.tookB = fallbackPrefersB(fr.overflowA, fr.wireA, fr.overflowB, fr.wireB);
  if (fr.tookB)
    nets = std::move(b);
  return fr;
}

struct PatternStageSettings {
  bool enabled = true; // PD11ROUTE_PATTERN
  int rounds = 30;     // PD11ROUTE_PATTERN_ROUNDS
  int cell = 8;        // PD11ROUTE_PATTERN_CELL
};

struct PatternStageReport {
  int rounds = 0;
  long long overflow = 0;
  size_t pieces = 0;
  std::string backend;
};

PatternStageReport patternStage2D(const RoutingGrid &g, std::vector<RoutedNet> &nets, Usage &usage,
                                  Prices &prices, const PatternStageSettings &ps, bool trace) {
  PatternStageReport rep;
  static gpu::CpuPatternBackend cpuBackend;
  gpu::IPatternBackend *backend = &cpuBackend;
#ifdef GR_PATTERN_SELECT
  {
    std::string chosen, err;
    backend = gpu::selectPatternBackend(chosen, err);
    if (!backend)
      fail("pd11route: " + err);
  }
#endif
  rep.backend = backend->name();
  struct Piece {
    int net;
    Point a, b;
    std::vector<int> edges; // encoded tile edges: (h ? 0 : 1) + 2 * index
  };
  std::vector<Piece> pieces;
  for (int i = 0; i < static_cast<int>(nets.size()); ++i) {
    const RoutedNet &rn = nets[i];
    if (rn.giant)
      continue;
    for (auto &e : rn.mst)
      if (!(rn.pts[e.first].x == rn.pts[e.second].x && rn.pts[e.first].y == rn.pts[e.second].y))
        pieces.push_back({i, rn.pts[e.first], rn.pts[e.second], {}});
  }
  rep.pieces = pieces.size();
  const int amount = 1 + g.minSpacing[0];
  auto addEdges = [&](const Piece &p, int sign) {
    for (int code : p.edges) {
      size_t idx = static_cast<size_t>(code >> 1);
      if (code & 1)
        usage.v[idx] += sign * amount;
      else
        usage.h[idx] += sign * amount;
    }
  };
  auto overflowed = [&](const Piece &p) {
    for (int code : p.edges) {
      size_t idx = static_cast<size_t>(code >> 1);
      if ((code & 1) ? usage.v[idx] > usage.capV[idx] : usage.h[idx] > usage.capH[idx])
        return true;
    }
    return false;
  };
  int cw = (g.nx + ps.cell - 1) / ps.cell, ch = (g.ny + ps.cell - 1) / ps.cell;
  gpu::PatternGrid pg;
  pg.nx = g.nx;
  pg.ny = g.ny;
  pg.h.assign(static_cast<size_t>(g.nx - 1) * g.ny, 0);
  pg.v.assign(static_cast<size_t>(g.nx) * (g.ny - 1), 0);
  long long best = std::numeric_limits<long long>::max();
  std::vector<std::vector<int>> bestEdges;
  for (int round = 0; round < ps.rounds; ++round) {
    std::vector<int> todo;
    for (int k = 0; k < static_cast<int>(pieces.size()); ++k)
      if (round == 0 || overflowed(pieces[k]))
        todo.push_back(k);
    if (todo.empty())
      break;
    // Color into batches: disjoint coarse bounding boxes within a batch.
    std::vector<std::vector<int>> batches;
    std::vector<std::vector<char>> occ;
    for (int k : todo) {
      const Piece &p = pieces[k];
      int cx0 = std::min(p.a.x, p.b.x) / ps.cell, cx1 = std::max(p.a.x, p.b.x) / ps.cell;
      int cy0 = std::min(p.a.y, p.b.y) / ps.cell, cy1 = std::max(p.a.y, p.b.y) / ps.cell;
      size_t chosen = batches.size();
      for (size_t bi = 0; bi < batches.size() && chosen == batches.size(); ++bi) {
        bool free = true;
        for (int cy = cy0; cy <= cy1 && free; ++cy)
          for (int cx = cx0; cx <= cx1 && free; ++cx)
            free = !occ[bi][static_cast<size_t>(cy) * cw + cx];
        if (free)
          chosen = bi;
      }
      if (chosen == batches.size()) {
        batches.emplace_back();
        occ.emplace_back(static_cast<size_t>(cw) * ch, 0);
      }
      batches[chosen].push_back(k);
      for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
          occ[chosen][static_cast<size_t>(cy) * cw + cx] = 1;
    }
    for (const std::vector<int> &batch : batches) {
      for (int k : batch) {
        addEdges(pieces[k], -1);
        pieces[k].edges.clear();
      }
      for (size_t i = 0; i < pg.h.size(); ++i) {
        int y = static_cast<int>(i / (g.nx - 1)), x = static_cast<int>(i % (g.nx - 1));
        pg.h[i] = gpu::quantizeCost(wireCostH(prices, g.hIndex(x, y, 0), amount));
      }
      for (size_t i = 0; i < pg.v.size(); ++i) {
        int y = static_cast<int>(i / g.nx), x = static_cast<int>(i % g.nx);
        pg.v[i] = gpu::quantizeCost(wireCostV(prices, g.vIndex(x, y, 0), amount));
      }
      gpu::PatternBatch pb;
      for (int k : batch) {
        pb.x1.push_back(pieces[k].a.x);
        pb.y1.push_back(pieces[k].a.y);
        pb.x2.push_back(pieces[k].b.x);
        pb.y2.push_back(pieces[k].b.y);
      }
      gpu::PatternResult pr;
      backend->solve(pg, pb, pr);
      for (size_t j = 0; j < batch.size(); ++j) {
        Piece &p = pieces[batch[j]];
        gpu::forEachCandidateEdge(p.a.x, p.a.y, p.b.x, p.b.y, pr.best[j], [&](bool h, int x, int y) {
          size_t idx = h ? g.hIndex(x, y, 0) : g.vIndex(x, y, 0);
          p.edges.push_back(static_cast<int>(idx * 2 + (h ? 0 : 1)));
        });
        addEdges(p, +1);
      }
    }
    advancePresentDemand(prices, usage);
    long long ov = totalOverflow(g, usage);
    rep.rounds = round + 1;
    if (trace)
      std::cerr << "pattern round " << round << ": " << todo.size() << " pieces in " << batches.size()
                << " batches, 2D overflow " << ov << " (per-piece count)\n";
    if (ov < best) {
      best = ov;
      bestEdges.clear();
      for (const Piece &p : pieces)
        bestEdges.push_back(p.edges);
    }
    if (ov == 0)
      break;
  }
  // The best round's pieces become each net's wiring: the union of its
  // pieces' tile edges, charged once per net.
  std::vector<std::set<int>> netEdges(nets.size());
  for (size_t k = 0; k < pieces.size(); ++k)
    for (int code : bestEdges[k])
      netEdges[pieces[k].net].insert(code);
  for (size_t i = 0; i < nets.size(); ++i) {
    RoutedNet &rn = nets[i];
    if (rn.giant)
      continue;
    rn.segs.clear();
    rn.charges.clear();
    int netAmount = rn.net->minWidth + g.minSpacing[0];
    for (int code : netEdges[i]) {
      size_t idx = static_cast<size_t>(code >> 1);
      if (code & 1) {
        int x = static_cast<int>(idx % g.nx), y = static_cast<int>(idx / g.nx);
        rn.charges.push_back({false, x, y, y + 1, 0, netAmount});
        rn.segs.push_back({x, y, 0, x, y + 1, 0});
      } else {
        int x = static_cast<int>(idx % (g.nx - 1)), y = static_cast<int>(idx / (g.nx - 1));
        rn.charges.push_back({true, y, x, x + 1, 0, netAmount});
        rn.segs.push_back({x, y, 0, x + 1, y, 0});
      }
    }
  }
  usage = usageFromCharges(g, nets);
  rep.overflow = totalOverflow(g, usage);
  return rep;
}

RunReport route(const Design &d, int maxFastRounds, int maxSlowRounds, double epsilon,
                 double histStep, HistMode histMode, double histFactor,
                 int streakThreshold = 1, bool scaleByOverflow = false,
                 bool relaxEnabled = true, double ratioExponent = 1.0,
                 double exponentEscalation = 0.0, bool useMSMV = true,
                 bool useSteinerize = true, bool useExactSteiner = true,
                 int maxExactPins = 8, BoxGrowth box = BoxGrowth{},
                 CostSettings cost = CostSettings{},
                 AcceptMode accept = AcceptMode::kRevert, bool slowPieces = false,
                 long long stopAt = -1, const PatternStageSettings *pattern = nullptr) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  const auto routeStart = std::chrono::steady_clock::now(); // pd27route
  Prices prices(g);
  Usage usage(g);
  // This file's own mechanism. `usage` is only ever reassigned in place
  // below (usage = Usage(g)), never replaced by another object, so the
  // pointer configureCostModel keeps stays valid for the whole run.
  configureCostModel(prices, usage, cost);
  LayerPlan lp(g);
  SearchRegion region;
  bool trace = std::getenv("VROUTE_TRACE") != nullptr;

  std::vector<RoutedNet> nets;
  for (const Net &n : d.nets) {
    if (n.pins.size() < 2)
      continue;
    RoutedNet rn;
    rn.net = &n;
    for (const Pin &p : n.pins) {
      bool dup = false;
      for (const Point &q : rn.pts)
        if (q.x == p.x && q.y == p.y) {
          dup = true;
          break;
        }
      if (!dup)
        rn.pts.push_back({p.x, p.y});
    }
    if (rn.pts.size() < 2)
      continue;
    std::vector<std::pair<int, int>> plainMst = mstEdges(rn.pts);
    // This file's own new mechanism: exact-solve small nets via the
    // Dreyfus-Wagner Steiner-tree DP (FR4ROUTE_EXACT_STEINER=0 disables
    // it); nets above the degree cap, or with the exact solve disabled,
    // fall through to fr3route's own inherited cheap Steinerization
    // (FR3ROUTE_STEINERIZE=0 disables that too, reproducing fr1route's
    // exact plain-MST topology) -- the same layered "each switch
    // reproduces its parent" pattern every prior mechanism uses.
    if (useExactSteiner && rn.pts.size() >= 3 &&
        rn.pts.size() <= static_cast<size_t>(maxExactPins)) {
      SteinerResult sr = exactSteinerTree(rn.pts);
      rn.pts = std::move(sr.pts);
      rn.mst = std::move(sr.edges);
    } else if (useSteinerize) {
      SteinerResult sr = steinerizeTree(rn.pts, plainMst);
      rn.pts = std::move(sr.pts);
      rn.mst = std::move(sr.edges);
    } else {
      rn.mst = std::move(plainMst);
    }
    rn.giant = n.pins.size() > kGiantNetPins;
    nets.push_back(std::move(rn));
  }

  for (RoutedNet &rn : nets)
    if (rn.giant)
      routeGiantNet(g, usage, lp, rn);

  // This file's own new mechanism: let giant nets participate in the
  // fast tier's own per-leg price-negotiated reroute below, instead of
  // staying frozen at routeGiantNet's one-shot commit above for the rest
  // of the run. routeGiantNet still runs first (every net needs a
  // starting route) and giant nets still never reach the slow tier's
  // MSMV escalation or the cleanup pass -- this widens fast-tier
  // eligibility only. NV3ROUTE_GIANT_FAST_TIER=0 excludes them again,
  // reproducing nv2route's exact prior behavior.
  bool giantFastTier = std::getenv("NV3ROUTE_GIANT_FAST_TIER") == nullptr ||
                        std::string(std::getenv("NV3ROUTE_GIANT_FAST_TIER")) != "0";

  // pd11route: the batched pattern stage replaces phase A and the cleanup
  // pass (maxFastRounds -> 0 skips phase A's loop below).
  const bool patternOn = pattern && pattern->enabled;
  if (patternOn) {
    PatternStageReport psr = patternStage2D(g, nets, usage, prices, *pattern, trace);
    std::cerr << "pattern stage (" << psr.backend << "): " << psr.pieces << " pieces, " << psr.rounds
              << " rounds, 2D overflow " << psr.overflow << "\n";
    rep.nets = nets;
    rep.bestOverflow = psr.overflow;
    rep.roundsRun = psr.rounds;
    maxFastRounds = 0;
    detourTrace("pattern stage", nets);
  }

  // Phase A: nproute's algorithm, unmodified in spirit, aside from giant
  // nets' own new fast-tier eligibility above.
  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxFastRounds; ++round) {
    for (RoutedNet &rn : nets) {
      if (rn.giant && !giantFastTier)
        continue;
      if (round > 0 && !netOverflowed(g, usage, rn))
        continue;
      routeNetFast(g, prices, usage, lp, epsilon, defaultThreshold(), rn);
    }
    // Round boundary: the settled, whole-grid usage snapshot for this round
    // is final now (every net that was going to move this round already
    // has). This is the one place per fast-tier round where updateHistory
    // may run -- never mid-round, never per-net, so "over capacity" always
    // means "over capacity in the fully negotiated state of this round,"
    // not a transient partial-round reading.
    updateHistory(g, prices, usage, histStep, histMode, histFactor, streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation);
    advancePresentDemand(prices, usage);
    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    if (trace)
    {
      std::cerr << "fast round " << round << " overflow " << overflow;
      if (prices.model == CostModel::kPresentDemand)
        std::cerr << " (pd next iter " << prices.pdIter << ", wl " << pdWl(prices) << ")";
      std::cerr << "\n";
    }
    if (overflow < rep.bestOverflow) {
      rep.bestOverflow = overflow;
      rep.bestRound = round;
      rep.nets = nets;
      roundsSinceImprovement = 0;
    } else {
      ++roundsSinceImprovement;
    }
    if (rep.bestOverflow == 0) {
      rep.convergedToZero = true;
      return rep;
    }
    if (roundsSinceImprovement >= kFastPatience)
      break;
  }

  if (rep.nets.empty())
    rep.nets = nets; // never improved on the initial max; still need output

  // Rebuild usage/nets to match the fast tier's best snapshot before the
  // cleanup pass and escalation touch anything -- the loop may have ended
  // on a later, worse round than its best (see mroute.cpp's identical note).
  nets = rep.nets;
  usage = Usage(g);
  for (const RoutedNet &rn : nets)
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      else
        for (int y = c.a; y < c.b; ++y)
          usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
    }

  if (!patternOn)
    relaxedCleanup(g, prices, usage, lp, epsilon, rep);
  nets = rep.nets;
  usage = Usage(g);
  for (const RoutedNet &rn : nets)
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      else
        for (int y = c.a; y < c.b; ++y)
          usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
    }
  // The cleanup pass is its own round boundary -- it can move many nets
  // across its (default 30) internal supervised passes, and the usage
  // array above is its final settled state, so history advances once here
  // too rather than either 30 times (noisy, mid-cleanup) or not at all
  // (would let a net that the fast tier never resolved but cleanup also
  // couldn't fix sit at history 1.0 right up to escalation).
  updateHistory(g, prices, usage, histStep, histMode, histFactor, streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation);
  advancePresentDemand(prices, usage);

  if (rep.bestOverflow == 0) {
    rep.convergedToZero = true;
    return rep;
  }

  rep.secondsBeforeSlow = std::chrono::duration<double>(std::chrono::steady_clock::now() - routeStart).count();
  // Phase B: escalate whatever the fast tier (including its cleanup pass)
  // still could not legalize, and try the slow tier on each one --
  // *supervised*, exactly like the cleanup pass: reroute, check the local
  // overflow delta, keep only if it didn't get worse. This is not optional
  // caution. The first version of this loop accepted every escalated net's
  // Dijkstra reroute unconditionally, all in the same round -- on adaptec1,
  // 53840 nets (30% of the design) escalated at once, and overflow spiked
  // to 5.6 *million* before slowly recovering over the following rounds,
  // never getting back below the fast tier's own 227914. Undoing a fast-
  // tier charge to try a Dijkstra alternative, unconditionally, for tens of
  // thousands of nets in one pass is the multi-net version of the exact
  // anti-pattern selective rip-up exists to prevent (reroute-everyone-every-
  // round churn) -- Dijkstra's larger flexibility just made it worse, not
  // better, applied at that scale with no safety net. "Best" tracking
  // caught the regression and the final output was still correct (227914,
  // matching the fast tier alone), but ~150s were spent finding nothing.
  for (RoutedNet &rn : nets)
    if (!rn.giant && netOverflowed(g, usage, rn))
      rn.escalated = true;
  int initialEscalated = 0;
  for (const RoutedNet &rn : nets)
    if (rn.escalated)
      ++initialEscalated;
  if (trace)
    std::cerr << "escalating " << initialEscalated << " of " << nets.size() << " nets\n";

  roundsSinceImprovement = 0;
  bool reclaimedAtStop = false; // pd18route
  int slowReclaims = 0;         // pd18route
  for (int round = 0; round < maxSlowRounds; ++round) {
    int accepted = 0, rejected = 0, keptWorse = 0;
    // This file's own mechanism: search-box margin bookkeeping for trace.
    long long marginSum = 0;
    int marginCount = 0, atCap = 0;
    if (gParallel.threads > 0) {
      // pd24route: batched parallel round over the nets that are (or become)
      // escalated and, after round 0, overflowed at the round's start.
      if (prices.model != CostModel::kPresentDemand)
        fail("PD24ROUTE_THREADS needs present-demand pricing (PD1ROUTE_COST=pd)");
      std::vector<size_t> order;
      for (size_t i = 0; i < nets.size(); ++i) {
        const RoutedNet &rn = nets[i];
        if (rn.giant)
          continue;
        bool ov = netOverflowed(g, usage, rn);
        if (!(rn.escalated || ov))
          continue;
        if (round > 0 && !ov)
          continue;
        order.push_back(i);
      }
      auto task = [&](Usage &u, Prices &p, SearchRegion &reg, const LayerPlan &lpw, RoutedNet &rn) {
        TaskStats s; // 0 accepted, 1 rejected, 2 kept-worse, 3 margin sum, 4 margin count, 5 at cap
        if (!rn.escalated && netOverflowed(g, u, rn))
          rn.escalated = true;
        if (!rn.escalated)
          return s;
        if (round > 0 && !netOverflowed(g, u, rn))
          return s;
        assignBoxMarginForSearch(box, round, rn);
        if (rn.boxMargin >= 1) {
          s.n[3] += rn.boxMargin;
          s.n[4] += 1;
          if (rn.boxMargin >= box.cap)
            s.n[5] += 1;
        }
        if (slowPieces && useMSMV) {
          PieceStats ps = rerouteNetPieces(g, u, p, lpw, reg, rn, rn.boxMargin >= 1 ? rn.boxMargin : 6, accept,
                                           nullptr);
          if (ps.ok) {
            s.n[0] += ps.pieces - ps.reverted;
            s.n[1] += ps.reverted;
            if (box.mode == BoxMode::PerNet)
              growBoxMarginIfStillOverflowed(box, netOverflowed(g, u, rn), rn);
            return s;
          }
        }
        std::vector<ChargeRecord> oldCharges = rn.charges;
        std::vector<Seg> oldSegs = rn.segs;
        long long localBefore = overflowOverUnion(g, u, oldCharges, oldCharges);
        if (useMSMV)
          routeNetSlowMSMV(g, p, u, lpw, reg, epsilon, rn);
        else
          routeNetSlow(g, p, u, lpw, reg, epsilon, rn);
        long long localAfter = overflowOverUnion(g, u, oldCharges, rn.charges);
        if (revertReroute(accept, localBefore, localAfter)) {
          for (const ChargeRecord &c : rn.charges)
            undoCharge(g, u, c);
          rn.charges = oldCharges;
          rn.segs = oldSegs;
          for (const ChargeRecord &c : rn.charges)
            addCharge(g, u, c);
          s.n[1] += 1;
        } else {
          s.n[0] += 1;
          if (localAfter > localBefore)
            s.n[2] += 1;
        }
        if (box.mode == BoxMode::PerNet)
          growBoxMarginIfStillOverflowed(box, netOverflowed(g, u, rn), rn);
        return s;
      };
      TaskStats ts = runBatchedRound(g, nets, order, usage, prices, region, lp, gParallel, task, rep.batches);
      accepted = static_cast<int>(ts.n[0]);
      rejected = static_cast<int>(ts.n[1]);
      keptWorse = static_cast<int>(ts.n[2]);
      marginSum = ts.n[3];
      marginCount = static_cast<int>(ts.n[4]);
      atCap = static_cast<int>(ts.n[5]);
    }
    for (RoutedNet &rn : nets) {
      if (gParallel.threads > 0)
        break;
      if (rn.giant)
        continue;
      if (!rn.escalated && netOverflowed(g, usage, rn))
        rn.escalated = true;
      if (!rn.escalated)
        continue; // fast tier already has this net legal; leave it alone
      if (round > 0 && !netOverflowed(g, usage, rn))
        continue; // already resolved by an earlier round's accepted move

      // This file's own mechanism: pick this search's box margin.
      assignBoxMarginForSearch(box, round, rn);
      if (rn.boxMargin >= 1) {
        marginSum += rn.boxMargin;
        ++marginCount;
        if (rn.boxMargin >= box.cap)
          ++atCap;
      }

      // pd9route: reroute only the net's pieces that cross overflow (NTHU's
      // granularity) instead of regrowing the whole net; the rest of the
      // net keeps its shape. Falls through to the whole-net reroute when the
      // wiring is not a tree.
      if (slowPieces && useMSMV) {
        PieceStats ps = rerouteNetPieces(g, usage, prices, lp, region, rn,
                                         rn.boxMargin >= 1 ? rn.boxMargin : 6, accept, nullptr);
        if (ps.ok) {
          accepted += ps.pieces - ps.reverted;
          rejected += ps.reverted;
          if (box.mode == BoxMode::PerNet)
            growBoxMarginIfStillOverflowed(box, netOverflowed(g, usage, rn), rn);
          continue;
        }
      }

      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);

      if (useMSMV)
        routeNetSlowMSMV(g, prices, usage, lp, region, epsilon, rn);
      else
        routeNetSlow(g, prices, usage, lp, region, epsilon, rn);

      long long localAfter = overflowOverUnion(g, usage, oldCharges, rn.charges);
      if (revertReroute(accept, localBefore, localAfter)) {
        for (const ChargeRecord &c : rn.charges)
          undoCharge(g, usage, c);
        rn.charges = oldCharges;
        rn.segs = oldSegs;
        for (const ChargeRecord &c : rn.charges) {
          if (c.horizontal)
            for (int x = c.a; x < c.b; ++x)
              usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
          else
            for (int y = c.a; y < c.b; ++y)
              usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
        }
        ++rejected;
      } else {
        ++accepted;
        if (localAfter > localBefore)
          ++keptWorse; // only possible under kKeep
      }

      // This file's own mechanism: a net still overflowed after this
      // round's supervised reroute (whether its new route was kept or
      // reverted) gets a wider box next round, up to the cap. Judged
      // against the live usage at this point in the round, the same state
      // the accept/reject decision above just used.
      if (box.mode == BoxMode::PerNet)
        growBoxMarginIfStillOverflowed(box, netOverflowed(g, usage, rn), rn);
    }
    // Same round-boundary rule as phase A: the supervised accept/reject
    // decisions for every escalated net this round are final by this
    // point, so this is the settled state to judge "over capacity" against.
    updateHistory(g, prices, usage, histStep, histMode, histFactor, streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation);
    advancePresentDemand(prices, usage);
    long long overflow = totalOverflow(g, usage);
    rep.roundsRun += 1;
    if (trace) {
      std::cerr << "slow round " << round << " overflow " << overflow << " (accepted "
                << accepted << ", rejected " << rejected;
      if (accept == AcceptMode::kKeep)
        std::cerr << ", kept-worse " << keptWorse;
      std::cerr << ")";
      if (marginCount > 0)
        std::cerr << " box margin mean " << (marginSum / marginCount) << ", at cap "
                  << atCap << " of " << marginCount;
      if (prices.model == CostModel::kPresentDemand)
        std::cerr << " (pd next iter " << prices.pdIter << ", wl " << pdWl(prices) << ")";
      std::cerr << "\n";
    }
    if (overflow < rep.bestOverflow) {
      rep.bestOverflow = overflow;
      rep.nets = nets;
      roundsSinceImprovement = 0;
    } else {
      ++roundsSinceImprovement;
    }
    if (rep.bestOverflow == 0) {
      rep.convergedToZero = true;
      break;
    }
    // pd2route: under kKeep every reroute is kept and overflow is expected
    // to rise before it falls (NTHU runs a fixed iteration budget), so the
    // patience stop is off and the run uses its whole slow-round budget; the
    // best snapshot above is still what gets returned.
    if (accept == AcceptMode::kRevert && (accepted == 0 || roundsSinceImprovement >= kSlowPatience)) {
      rep.stalled = true;
      break;
    }
    // pd9route: NTHU's main stage ends once overflow is at or below a
    // threshold and leaves the rest to refinement; PD9ROUTE_STOP_AT does
    // the same (the post-2D stages -- region, refinement -- finish the job).
    // pd18route: the reclaim pass inside the slow tier.
    bool stopNow = stopAt >= 0 && overflow <= stopAt;
    bool reclaimNow = round + 1 < maxSlowRounds &&
                      ((stopNow && gSlowReclaim.atStop && !reclaimedAtStop) ||
                       (gSlowReclaim.every > 0 && (round + 1) % gSlowReclaim.every == 0 && !stopNow));
    if (reclaimNow) {
      if (stopNow)
        reclaimedAtStop = true;
      ReclaimReport rr = reclaimDetours(d, nets, gSlowReclaim.settings);
      usage = usageFromCharges(g, nets);
      rep.bestOverflow = std::numeric_limits<long long>::max(); // post-reclaim states are what counts
      ++slowReclaims;
      if (trace)
        std::cerr << "slow-tier reclaim after round " << round << ": " << rr.kept << " of " << rr.candidates
                  << " kept (" << rr.avoided << " skipped near overflow), 2D wire " << rr.wireBefore << " -> "
                  << rr.wireAfter << ", 2D overflow " << rr.overflowBefore << " -> " << rr.overflowAfter << "\n";
      continue;
    }
    if (stopNow) {
      if (trace)
        std::cerr << "slow tier stopped: 2D overflow " << overflow << " <= PD9ROUTE_STOP_AT " << stopAt << "\n";
      break;
    }
  }
  rep.slowReclaims = slowReclaims;
  rep.escalatedCount = 0;
  for (const RoutedNet &rn : rep.nets)
    if (rn.escalated)
      ++rep.escalatedCount;
  rep.secondsSlow =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - routeStart).count() - rep.secondsBeforeSlow;
  return rep;
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: layer assignment. See the file header for
// the full derivation. Maps the already-converged 2D topology onto real
// layers (RealLayerPlan/RealUsage/assignSegmentLayer -- greedy, capacity-
// aware, via-cost-blind), then physically stitches any resulting layer
// mismatches together with real vias (a separate, unoptimized pass).
// ---------------------------------------------------------------------------

// The real per-layer analogue of this file's own (inherited) 2D-collapsed
// LayerPlan, which discards all real per-layer information. Built exactly
// the way the pre-collapse routers built theirs (vroute.cpp/mroute.cpp's
// own LayerPlan): a real layer is a horizontal candidate if hCapLayer[l]>0,
// a vertical candidate if vCapLayer[l]>0 -- CLAUDE.md's "preferred
// direction is expressed as zero capacity in the other direction" made
// concrete. The empty-fallback branches reproduce the same degenerate-case
// handling (route either direction on any layer) when a whole design
// declares zero capacity in one direction on every layer.
struct RealLayerPlan {
  std::vector<int> hLayers, vLayers;
  explicit RealLayerPlan(const RoutingGrid &g) {
    for (int l = 0; l < g.nLayers; ++l) {
      if (g.hCapLayer[l] > 0)
        hLayers.push_back(l);
      if (g.vCapLayer[l] > 0)
        vLayers.push_back(l);
    }
    if (hLayers.empty())
      for (int l = 0; l < g.nLayers; ++l)
        hLayers.push_back(l);
    if (vLayers.empty())
      for (int l = 0; l < g.nLayers; ++l)
        vLayers.push_back(l);
  }
};

// Fresh, independent per-real-layer usage tracking -- this file's own
// Usage/Prices are sized only for the synthetic 2D grid (h2dCount/
// v2dCount) and cannot represent real per-layer state at all.
struct RealUsage {
  std::vector<long long> h, v;
  explicit RealUsage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

// ---------------------------------------------------------------------------
// This file's own new mechanism: a joint via-cost/capacity-aware tree DP,
// replacing fr5route's independent-per-segment-then-stitch approach. See
// the file header for the full derivation. Via capacity model adapted
// directly from mroute.cpp (mroute.cpp:173-202), the only existing
// via-capacity precedent in this project.
// ---------------------------------------------------------------------------

// Via sites, per adjacent real layer pair (l, l+1), indexed by the lower
// layer: nLayers-1 planes of nx*ny tiles each.
size_t viaIndex(const RoutingGrid &g, int x, int y, int lowerLayer) {
  return (static_cast<size_t>(lowerLayer) * g.ny + y) * g.nx + x;
}
size_t viaCount(const RoutingGrid &g) {
  return static_cast<size_t>(std::max(g.nLayers - 1, 0)) * g.ny * g.nx;
}

// The .gr format has no via capacity field -- derived from "via spacing"
// instead, the same 1D cross-section approximation the format already
// uses for wire capacity. Uniform across every (x,y) for a given layer
// pair (spacing and tile size are grid-wide constants), so one value per
// layer pair, not a dense per-tile array; only usage varies by tile.
std::vector<int> viaCapacityPerLayerPair(const RoutingGrid &g) {
  std::vector<int> cap;
  for (int l = 0; l + 1 < g.nLayers; ++l) {
    long long spacing = std::max(g.viaSpacing[l], g.viaSpacing[l + 1]);
    long long dim = std::min(g.tileW, g.tileH);
    cap.push_back(spacing > 0 ? static_cast<int>(std::max(1LL, dim / spacing))
                               : std::numeric_limits<int>::max() / 4);
  }
  return cap;
}

// Real per-via-site usage, count-based (a flat +1 per crossing, matching
// mroute.cpp's own convention -- via capacity is about physical site
// count, not net width, unlike wire capacity).
struct RealViaUsage {
  std::vector<long long> usage;
  explicit RealViaUsage(const RoutingGrid &g) : usage(viaCount(g), 0) {}
};

// ---------------------------------------------------------------------------
// This file's own new mechanism: per-net charge logs, so a net's committed
// layer assignment (both the wire usage its chosen layers charged and the
// via usage its chosen stacks charged) can be undone and redone exactly,
// cheaply, without a geometry re-walk -- the missing piece every `fr`
// generation through fr7route lacked, since assignLayers wrote directly
// into ru.usage/rvu.usage with no record of which indices were touched.
// Mirrors kroute.cpp's own ChargeRecord/undoCharge (kroute.cpp:798-ish/
// 520-528), ported: kroute logs one record per charged wire-edge run,
// this logs one entry per charged array index (ru.h/ru.v index plus the
// exact amount added, since wire charges are width-dependent, not a flat
// +1; rvu.usage entries are always a flat +1 so a bare index suffices).
// ---------------------------------------------------------------------------
struct ChargeEntry {
  size_t idx;
  long long amount;
};
struct NetCharge {
  std::vector<ChargeEntry> ruH, ruV;
  std::vector<size_t> rvuIdx;
};

void undoCharge(RealUsage &ru, RealViaUsage &rvu, const NetCharge &c) {
  for (const ChargeEntry &e : c.ruH)
    ru.h[e.idx] -= e.amount;
  for (const ChargeEntry &e : c.ruV)
    ru.v[e.idx] -= e.amount;
  for (size_t idx : c.rvuIdx)
    rvu.usage[idx] -= 1;
}

// The exact inverse of undoCharge, replaying the same logged entries
// forward -- used both to redo a rejected reprocessing attempt's old
// charge and to restore a net to a best-known-round snapshot at the end
// of the negotiation loop.
void applyCharge(RealUsage &ru, RealViaUsage &rvu, const NetCharge &c) {
  for (const ChargeEntry &e : c.ruH)
    ru.h[e.idx] += e.amount;
  for (const ChargeEntry &e : c.ruV)
    ru.v[e.idx] += e.amount;
  for (size_t idx : c.rvuIdx)
    rvu.usage[idx] += 1;
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: LIFO-relaxable history cost on via sites,
// a direct port of kroute.cpp's own Prices::histH/streakH/histOpsH and
// updateHistory (kroute.cpp's own header has the full derivation) -- keyed
// by via-site index (viaIndex, one flat array) instead of separate H/V
// wire-edge arrays, since a via site is a single resource, not a
// direction-split pair. Reuses HistOp and kHistoryCap, already defined
// above for the 2D stage's own Prices/updateHistory, unchanged.
// ---------------------------------------------------------------------------
struct ViaPrices {
  std::vector<double> hist;
  std::vector<int> streak;
  std::vector<std::vector<HistOp>> histOps;
  explicit ViaPrices(const RoutingGrid &g)
      : hist(viaCount(g), 1.0), streak(viaCount(g), 0), histOps(viaCount(g)) {}
};

// Called once per round boundary (never mid-round, never per-net -- same
// discipline updateHistory's own header requires: `rvu` must be the
// settled, whole-run usage snapshot at a round boundary). `viaCap` is
// indexed by layer (viaCapacityPerLayerPair's own convention); a via site's
// layer is recovered from its flat index via `idx / (g.nx*g.ny)`, the exact
// inverse of viaIndex's own `(lowerLayer*ny+y)*nx+x` formula.
void updateViaHistory(const RoutingGrid &g, ViaPrices &vp, const RealViaUsage &rvu,
                       const std::vector<int> &viaCap, double histStep, HistMode mode,
                       double histFactor, int streakThreshold = 1, bool scaleByOverflow = false,
                       bool relaxEnabled = true, double ratioExponent = 1.0,
                       double exponentEscalation = 0.0) {
  size_t plane = static_cast<size_t>(g.nx) * static_cast<size_t>(g.ny);
  for (size_t idx = 0; idx < rvu.usage.size(); ++idx) {
    int l = static_cast<int>(idx / plane);
    int cap = (l >= 0 && l < static_cast<int>(viaCap.size())) ? viaCap[l] : 1;
    if (cap <= 0)
      continue;
    long long usage = rvu.usage[idx];
    if (usage > cap) {
      ++vp.streak[idx];
      if (vp.streak[idx] < streakThreshold)
        continue;
      double ratio = scaleByOverflow ? static_cast<double>(usage) / cap : 1.0;
      double before = vp.hist[idx];
      if (mode == HistMode::kAdditive) {
        vp.hist[idx] = std::min(kHistoryCap, vp.hist[idx] + histStep * ratio);
        if (relaxEnabled)
          vp.histOps[idx].push_back({vp.hist[idx] - before, false});
      } else if (mode == HistMode::kMultiplicative) {
        vp.hist[idx] = std::min(kHistoryCap, vp.hist[idx] * (1.0 + (histFactor - 1.0) * ratio));
        if (relaxEnabled)
          vp.histOps[idx].push_back({vp.hist[idx] / before, true});
      } else {
        double effExponent =
            ratioExponent + exponentEscalation * static_cast<double>(vp.histOps[idx].size());
        vp.hist[idx] =
            std::min(kHistoryCap, vp.hist[idx] * std::pow(static_cast<double>(usage) / cap, effExponent));
        if (relaxEnabled)
          vp.histOps[idx].push_back({vp.hist[idx] / before, true});
      }
    } else {
      vp.streak[idx] = 0;
      if (relaxEnabled && !vp.histOps[idx].empty()) {
        HistOp op = vp.histOps[idx].back();
        vp.histOps[idx].pop_back();
        vp.hist[idx] = op.multiplicative ? std::max(1.0, vp.hist[idx] / op.amountOrFactor)
                                          : std::max(1.0, vp.hist[idx] - op.amountOrFactor);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: LIFO-relaxable history cost on real
// per-layer wire edges -- a direct port of ViaPrices/updateViaHistory
// above (itself ported from kroute.cpp), keyed by real wire-edge index
// (g.hIndex/g.vIndex) instead of via-site index. See the file header for
// why this specifically targets the 57.6% "distribution-only" share of
// the real overflow gap that historyVia alone cannot reach.
// ---------------------------------------------------------------------------
struct WirePrices {
  std::vector<double> histH, histV;
  std::vector<int> streakH, streakV;
  std::vector<std::vector<HistOp>> histOpsH, histOpsV;
  explicit WirePrices(const RoutingGrid &g)
      : histH(g.hCount(), 1.0), histV(g.vCount(), 1.0), streakH(g.hCount(), 0),
        streakV(g.vCount(), 0), histOpsH(g.hCount()), histOpsV(g.vCount()) {}
};

// Called once per round boundary, same discipline as updateViaHistory.
// Deliberately no std::max(1, ...) clamp on the `usage > cap` trigger --
// unlike viaCap (an internal heuristic that is never legitimately zero),
// a real per-layer wire edge's declared capacity CAN legitimately be zero
// (a preferred-direction zeroing, or a .gr adjustment), and any usage
// there is a real, full-magnitude overflow gr_verify would charge in
// full -- the same lesson this file's own predecessor's metric fix
// established for the overflow-computation functions. The clamp still
// applies, correctly, inside the scaleByOverflow ratio term below, where
// capacity is used as a divisor, not compared for overflow.
void updateWireHistory(const RoutingGrid &g, WirePrices &wp, const RealUsage &ru, double histStep,
                        HistMode mode, double histFactor, int streakThreshold = 1,
                        bool scaleByOverflow = false, bool relaxEnabled = true,
                        double ratioExponent = 1.0, double exponentEscalation = 0.0) {
  auto update = [&](std::vector<double> &hist, std::vector<int> &streak,
                     std::vector<std::vector<HistOp>> &histOps, const std::vector<long long> &usage,
                     const std::vector<int> &cap) {
    for (size_t idx = 0; idx < usage.size(); ++idx) {
      long long u = usage[idx];
      int c = cap[idx];
      if (u > c) {
        ++streak[idx];
        if (streak[idx] < streakThreshold)
          continue;
        double ratio = scaleByOverflow ? static_cast<double>(u) / std::max(1, c) : 1.0;
        double before = hist[idx];
        if (mode == HistMode::kAdditive) {
          hist[idx] = std::min(kHistoryCap, hist[idx] + histStep * ratio);
          if (relaxEnabled)
            histOps[idx].push_back({hist[idx] - before, false});
        } else if (mode == HistMode::kMultiplicative) {
          hist[idx] = std::min(kHistoryCap, hist[idx] * (1.0 + (histFactor - 1.0) * ratio));
          if (relaxEnabled)
            histOps[idx].push_back({hist[idx] / before, true});
        } else {
          double effExponent =
              ratioExponent + exponentEscalation * static_cast<double>(histOps[idx].size());
          hist[idx] = std::min(
              kHistoryCap,
              hist[idx] * std::pow(static_cast<double>(u) / std::max(1, c), effExponent));
          if (relaxEnabled)
            histOps[idx].push_back({hist[idx] / before, true});
        }
      } else {
        streak[idx] = 0;
        if (relaxEnabled && !histOps[idx].empty()) {
          HistOp op = histOps[idx].back();
          histOps[idx].pop_back();
          hist[idx] = op.multiplicative ? std::max(1.0, hist[idx] / op.amountOrFactor)
                                         : std::max(1.0, hist[idx] - op.amountOrFactor);
        }
      }
    }
  };
  update(wp.histH, wp.streakH, wp.histOpsH, ru.h, g.hcap);
  update(wp.histV, wp.streakV, wp.histOpsV, ru.v, g.vcap);
}

// A net's real topology, reconstructed from rn.segs geometry rather than
// read from rn.mst -- see the file header for why rn.mst cannot be
// trusted (escalated/MSMV nets never update it). Every wire leg gets
// split at every other leg's endpoint that lies strictly inside its own
// span (a mid-span MSMV attachment point), so no edge's endpoint is ever
// buried inside another edge's span -- the result is a genuine tree.
struct NetTree {
  std::vector<Point> nodes;
  struct Edge {
    int a, b; // node indices
    bool horizontal;
    int cross, lo, hi;
  };
  std::vector<Edge> edges;
  std::vector<std::vector<int>> adj; // node index -> incident edge indices
};

// Returns false if the raw segment geometry doesn't reduce to a clean
// tree (a redundant/overlapping leg produced a cycle, or some other
// unexpected disconnection) -- callers must fall back to the
// independent-greedy path on failure, never treat a failed
// reconstruction as safe to route around silently. This is the
// self-protecting escape hatch this generation's own known risks
// disclose: giant nets and other least-exercised shapes degrade to
// fr5route's own already-proven-correct behavior rather than risking a
// new correctness bug on unusual topology.
// pd3route: `dropCycles` (kNthu mode) skips a tile stretch that would
// close a cycle instead of failing the whole net -- a cycle-closing
// stretch is redundant wire (its ends are already connected), so dropping
// it keeps the net connected and only removes wire. Without it a net whose
// wires form a cycle (e.g. a giant net's fixed-pattern route) fell back to
// the old greedy per-segment layer choice, which put adaptec1's 2271-pin
// n7807 on zero-capacity layers (all 16 of its remaining overflow, Part
// 62). `pinTiles`: pins lying on a wire become nodes too, so every edge is
// split at pins and a dropped stretch can never contain one.
bool buildNetTree(const std::vector<Seg> &segs, NetTree &t, bool dropCycles = false,
                  const std::vector<Point> *pinTiles = nullptr) {
  t = NetTree();
  std::map<std::pair<int, int>, int> nodeIndex;
  auto nodeFor = [&](int x, int y) -> int {
    auto key = std::make_pair(x, y);
    auto it = nodeIndex.find(key);
    if (it != nodeIndex.end())
      return it->second;
    int idx = static_cast<int>(t.nodes.size());
    t.nodes.push_back({x, y});
    nodeIndex[key] = idx;
    return idx;
  };

  std::vector<const Seg *> legs;
  for (const Seg &s : segs)
    if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2))
      legs.push_back(&s);
  if (legs.empty()) {
    if (!segs.empty())
      nodeFor(segs.front().x1, segs.front().y1);
    return true; // a single-tile net -- nothing to split or assign
  }

  for (const Seg *s : legs) {
    nodeFor(s->x1, s->y1);
    nodeFor(s->x2, s->y2);
  }
  if (pinTiles)
    for (const Point &p : *pinTiles)
      for (const Seg *s : legs)
        if ((s->y1 == s->y2 && p.y == s->y1 && p.x >= std::min(s->x1, s->x2) && p.x <= std::max(s->x1, s->x2)) ||
            (s->x1 == s->x2 && p.x == s->x1 && p.y >= std::min(s->y1, s->y2) && p.y <= std::max(s->y1, s->y2))) {
          nodeFor(p.x, p.y);
          break;
        }

  std::map<int, std::vector<int>> pointsByY, pointsByX;
  for (int i = 0; i < static_cast<int>(t.nodes.size()); ++i) {
    pointsByY[t.nodes[i].y].push_back(i);
    pointsByX[t.nodes[i].x].push_back(i);
  }

  std::vector<int> uf(t.nodes.size());
  for (size_t i = 0; i < uf.size(); ++i)
    uf[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (uf[a] != a) {
      uf[a] = uf[uf[a]];
      a = uf[a];
    }
    return a;
  };
  auto addEdge = [&](int a, int b, bool horizontal, int cross, int lo, int hi) {
    int ra = find(a), rb = find(b);
    if (ra == rb)
      return false; // a cycle -- two legs already connect these nodes
    uf[ra] = rb;
    t.edges.push_back({a, b, horizontal, cross, lo, hi});
    return true;
  };

  for (const Seg *s : legs) {
    bool horizontal = s->y1 == s->y2;
    int lo = horizontal ? std::min(s->x1, s->x2) : std::min(s->y1, s->y2);
    int hi = horizontal ? std::max(s->x1, s->x2) : std::max(s->y1, s->y2);
    int cross = horizontal ? s->y1 : s->x1;

    std::vector<std::pair<int, int>> onLine; // (coord along axis, node index)
    auto ptIt = (horizontal ? pointsByY : pointsByX).find(cross);
    if (ptIt != (horizontal ? pointsByY : pointsByX).end()) {
      for (int ni : ptIt->second) {
        int coord = horizontal ? t.nodes[ni].x : t.nodes[ni].y;
        if (coord > lo && coord < hi)
          onLine.push_back({coord, ni});
      }
    }
    std::sort(onLine.begin(), onLine.end());

    int prevNode = nodeFor(horizontal ? lo : cross, horizontal ? cross : lo);
    int prevCoord = lo;
    for (auto &pr : onLine) {
      if (!addEdge(prevNode, pr.second, horizontal, cross, prevCoord, pr.first) && !dropCycles)
        return false;
      prevNode = pr.second;
      prevCoord = pr.first;
    }
    int lastNode = nodeFor(horizontal ? hi : cross, horizontal ? cross : hi);
    if (!addEdge(prevNode, lastNode, horizontal, cross, prevCoord, hi) && !dropCycles)
      return false;
  }

  t.adj.assign(t.nodes.size(), {});
  for (int i = 0; i < static_cast<int>(t.edges.size()); ++i) {
    t.adj[t.edges[i].a].push_back(i);
    t.adj[t.edges[i].b].push_back(i);
  }

  // A genuine tree has exactly nodes-1 edges; addEdge already rejects
  // any cycle, so an explicit connectivity check (rather than trusting
  // the count alone) is what actually rules out a stray, disconnected
  // node.
  if (t.edges.size() != t.nodes.size() - 1)
    return false;
  int root = find(0);
  for (size_t i = 1; i < t.nodes.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      return false;
  return true;
}

// The tree DP itself, this generation's own fix (see the file header for
// the full derivation): rooted at node 0, for each edge e and each of
// its direction-matching candidate real layers l, dp[e][l] is the total
// cost of e's own wire on layer l plus the cheapest joint resolution of
// every child edge hanging off e's own far (child-side) node, resolved
// via `resolveNode` below -- one shared via-stack range per node, not a
// separate chain per child (fr6route's own overcounting bug). Computed
// bottom-up (leaves first); `childChoice[e][l]` and `edgeRangeTable[e][l]`
// record, respectively, which layer each child took and which stack
// range the node settled on to achieve that minimum, both read back
// during the top-down traceback into the `nodeStackRange` out-parameter
// (indexed by node, for `commitNetTree` to emit exactly one via chain
// per node from). The root itself has no parent edge to agree with, so
// its own direct children are resolved against every candidate "virtual
// reference layer" via the same `resolveNode` helper, and whichever is
// cheapest overall is kept (`rootLayer` out-parameter, plus
// `nodeStackRange[0]`).
// pd3route's layer-assignment cost constants (nthuCost mode). The penalty
// must exceed any via count one net can accumulate; nets here have at most
// ~1000 pins and a few thousand tree nodes, each stack at most 5 layers.
constexpr double kLaOverflowPenalty = 1e6;
constexpr double kLaBalance = 1e-4;

bool layerAssignDP(const RoutingGrid &g, RealUsage &ru, RealViaUsage &rvu,
                    const std::vector<int> &viaCap, const RealLayerPlan &lp, int width,
                    const NetTree &t, std::vector<int> &edgeLayer, int &rootLayer,
                    std::vector<std::pair<int, int>> &nodeStackRange, double viaWeight = 1.0,
                    const std::vector<double> *historyVia = nullptr,
                    const std::vector<double> *historyWireH = nullptr,
                    const std::vector<double> *historyWireV = nullptr, bool nthuCost = false,
                    const std::vector<std::pair<int, int>> *nodePinRange = nullptr) {
  int n = static_cast<int>(t.nodes.size());
  edgeLayer.assign(t.edges.size(), 0);
  nodeStackRange.assign(n, {0, 0});
  rootLayer = 0;
  if (t.edges.empty())
    return true; // single-tile net -- nothing to assign

  std::vector<int> parentEdge(n, -1);
  std::vector<int> order;
  std::vector<char> visited(n, 0);
  std::vector<int> q{0};
  visited[0] = 1;
  for (size_t qi = 0; qi < q.size(); ++qi) {
    int u = q[qi];
    order.push_back(u);
    for (int ei : t.adj[u]) {
      int v = (t.edges[ei].a == u) ? t.edges[ei].b : t.edges[ei].a;
      if (visited[v])
        continue;
      visited[v] = 1;
      parentEdge[v] = ei;
      q.push_back(v);
    }
  }
  if (static_cast<int>(q.size()) != n)
    return false; // buildNetTree should already guarantee this never fires

  std::vector<std::vector<int>> childOfNode(n);
  for (int v = 0; v < n; ++v)
    for (int ei : t.adj[v])
      if (ei != parentEdge[v])
        childOfNode[v].push_back(ei);

  int L = g.nLayers;
  const double kInf = std::numeric_limits<double>::max() / 4;
  using Range = std::pair<int, int>;

  // This file's own new mechanism: `historyWireH`/`historyWireV`, when
  // supplied, multiply the whole (unchanged) usage/capacity ratio by the
  // *maximum* history value found among the unit tile-edges this leg
  // spans -- not a per-tile sum-of-ratios, which would change behavior
  // even at history=1.0 whenever capacity varies along a span. nullptr
  // (every self-test call site that predates this file) reproduces
  // nv1route's own formula exactly, since max(1.0, ...) over an unused
  // history is just 1.0.
  auto wireCostForEdge = [&](const NetTree::Edge &e, int l) -> double {
    const std::vector<int> &candidates = e.horizontal ? lp.hLayers : lp.vLayers;
    if (std::find(candidates.begin(), candidates.end(), l) == candidates.end())
      return kInf;
    int amount = width + g.minSpacing[l];
    long long usage = 0, cap = 0;
    double hist = 1.0;
    const std::vector<double> *historyWire = e.horizontal ? historyWireH : historyWireV;
    if (nthuCost) {
      // pd3route: NTHU-Route 2.0's layer assignment treats wire capacity as
      // a hard constraint (Layerassignment.cpp's DP only descends into a
      // layer whose 3D edge still has room). Here: each tile edge that this
      // net would push over its real capacity costs kLaOverflowPenalty per
      // unit of overflow (times its wire history, so later negotiation
      // rounds still see memory), which dominates any via count a net can
      // have -- overflow first, vias second, lexicographically. A tile with
      // room costs only kLaBalance * fill, a tie-break toward emptier
      // layers too small to ever outweigh one via. Per tile, not averaged
      // over the leg: one full tile inside a long leg costs as much as a
      // full short leg.
      double total = 0.0;
      for (int c = e.lo; c < e.hi; ++c) {
        size_t idx = e.horizontal ? g.hIndex(c, e.cross, l) : g.vIndex(e.cross, c, l);
        long long u = (e.horizontal ? ru.h[idx] : ru.v[idx]) + amount;
        long long capT = e.horizontal ? g.hcap[idx] : g.vcap[idx];
        double h = historyWire ? (*historyWire)[idx] : 1.0;
        if (u > capT)
          total += kLaOverflowPenalty * h * static_cast<double>(u - capT);
        else
          total += kLaBalance * static_cast<double>(u) / static_cast<double>(std::max<long long>(1, capT));
      }
      return total;
    }
    for (int c = e.lo; c < e.hi; ++c) {
      size_t idx = e.horizontal ? g.hIndex(c, e.cross, l) : g.vIndex(e.cross, c, l);
      usage += (e.horizontal ? ru.h[idx] : ru.v[idx]) + amount;
      cap += std::max(1, e.horizontal ? g.hcap[idx] : g.vcap[idx]);
      if (historyWire)
        hist = std::max(hist, (*historyWire)[idx]);
    }
    return hist * static_cast<double>(usage) / static_cast<double>(cap);
  };

  // One shared via stack spanning [lo,hi) -- the actual physical cost of
  // connecting every layer in that range, charged once, not once per
  // edge that taps into it (fr6route's own bug: it charged a separate
  // chain per parent/child pair, double-counting any shared portion).
  // This file's own new mechanism: `historyVia`, when supplied, multiplies
  // each unit step by that via site's own accumulated memory of
  // persistent over-capacity (updateViaHistory, ported from kroute's own
  // updateHistory) -- nullptr (every self-test call site that predates
  // this file) means every site's multiplier is exactly 1.0, reproducing
  // fr7route's own spanCost formula unchanged.
  auto spanCost = [&](int x, int y, int lo, int hi) -> double {
    if (lo >= hi)
      return 0.0;
    // pd3route: ISPD 2008 scoring gives vias no capacity -- a via costs one
    // unit of wirelength per layer crossed, nothing more -- so NTHU's
    // objective is the via count. This project's via-site capacity model
    // is not scored and is left out of the cost entirely.
    if (nthuCost)
      return static_cast<double>(hi - lo);
    double total = 0;
    for (int l = lo; l < hi; ++l) {
      size_t idx = viaIndex(g, x, y, l);
      double hist = historyVia ? (*historyVia)[idx] : 1.0;
      total +=
          hist * static_cast<double>(rvu.usage[idx] + 1) / static_cast<double>(std::max(1, viaCap[l]));
    }
    return total * viaWeight;
  };

  std::vector<std::vector<double>> dp_(t.edges.size(), std::vector<double>(L, kInf));

  // Resolves the best via-stack range and per-child layer choices for a
  // node, given the reference layer `ref` the edge above it (a real
  // parent edge, or the root's own virtual reference) is fixed at.
  // Exhaustive, not an approximation: tries every candidate range
  // [lo,hi] with lo<=ref<=hi (nLayers is small -- 6 for every real
  // benchmark here), and for each, every child independently picks its
  // own cheapest candidate layer that falls inside that exact range --
  // a child choosing outside it would mean the range was wrong, and
  // every range is tried, so the true joint optimum is always found.
  // pd7route: nodePinRange[u] = (lowest, highest) pin layer at node u, or
  // (-1, -1). A pin node's via stack must reach its pins' layers, so the
  // DP pays for pin access itself (NTHU's rec_count: a pin node's interval
  // starts at layer 0) instead of stitchPinAccess bolting a stack on after
  // the DP chose layers blind to it.
  auto resolveNode = [&](int u, int x, int y, int ref, const std::vector<int> &children, double &outTotal,
                          Range &outRange, std::vector<int> &outChoice) -> bool {
    outTotal = kInf;
    bool any = false;
    int loMax = ref, hiMin = ref;
    if (nodePinRange && (*nodePinRange)[u].first >= 0) {
      loMax = std::min(loMax, (*nodePinRange)[u].first);
      hiMin = std::max(hiMin, (*nodePinRange)[u].second);
    }
    for (int lo = 0; lo <= loMax; ++lo) {
      for (int hi = hiMin; hi < L; ++hi) {
        double total = spanCost(x, y, lo, hi);
        std::vector<int> choice(children.size(), 0);
        bool feasible = true;
        for (size_t k = 0; k < children.size(); ++k) {
          int ce = children[k];
          double best = kInf;
          int bestLayer = 0;
          for (int cl = lo; cl <= hi; ++cl) {
            if (dp_[ce][cl] >= kInf)
              continue;
            if (dp_[ce][cl] < best) {
              best = dp_[ce][cl];
              bestLayer = cl;
            }
          }
          if (best >= kInf) {
            feasible = false;
            break;
          }
          total += best;
          choice[k] = bestLayer;
        }
        if (!feasible)
          continue;
        any = true;
        if (total < outTotal) {
          outTotal = total;
          outRange = {lo, hi};
          outChoice = std::move(choice);
        }
      }
    }
    return any;
  };

  std::vector<std::vector<std::vector<int>>> childChoice(t.edges.size());
  std::vector<std::vector<Range>> edgeRangeTable(t.edges.size());

  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    int u = *it;
    int pe = parentEdge[u];
    if (pe < 0)
      continue; // root -- resolved after this loop
    const NetTree::Edge &e = t.edges[pe];
    const std::vector<int> &children = childOfNode[u];
    childChoice[pe].assign(L, std::vector<int>(children.size(), 0));
    edgeRangeTable[pe].assign(L, Range{0, 0});

    for (int l = 0; l < L; ++l) {
      double base = wireCostForEdge(e, l);
      if (base >= kInf)
        continue;
      double nodeTotal;
      Range range;
      std::vector<int> choice;
      if (!resolveNode(u, t.nodes[u].x, t.nodes[u].y, l, children, nodeTotal, range, choice))
        continue; // no via-stack range covers every child at this parent layer
      dp_[pe][l] = base + nodeTotal;
      edgeRangeTable[pe][l] = range;
      childChoice[pe][l] = std::move(choice);
    }
  }

  const std::vector<int> &rootChildren = childOfNode[0];
  double bestRootCost = kInf;
  int bestRootLayer = 0;
  Range bestRootRange{0, 0};
  std::vector<int> bestRootChoice(rootChildren.size(), 0);
  for (int r = 0; r < L; ++r) {
    double total;
    Range range;
    std::vector<int> choice;
    if (!resolveNode(0, t.nodes[0].x, t.nodes[0].y, r, rootChildren, total, range, choice))
      continue;
    if (total < bestRootCost) {
      bestRootCost = total;
      bestRootLayer = r;
      bestRootRange = range;
      bestRootChoice = std::move(choice);
    }
  }
  if (bestRootCost >= kInf)
    return false; // every real layer pair has finite via cost -- defensive only

  rootLayer = bestRootLayer;
  nodeStackRange[0] = bestRootRange;
  std::vector<int> stack;
  for (size_t k = 0; k < rootChildren.size(); ++k) {
    int ce = rootChildren[k];
    edgeLayer[ce] = bestRootChoice[k];
    stack.push_back(ce);
  }
  while (!stack.empty()) {
    int e = stack.back();
    stack.pop_back();
    int l = edgeLayer[e];
    int u = (parentEdge[t.edges[e].a] == e) ? t.edges[e].a : t.edges[e].b;
    nodeStackRange[u] = edgeRangeTable[e][l];
    const std::vector<int> &children = childOfNode[u];
    for (size_t k = 0; k < children.size(); ++k) {
      int ce = children[k];
      edgeLayer[ce] = childChoice[e][l][k];
      stack.push_back(ce);
    }
  }
  return true;
}

// Commits the DP's own chosen layers: rebuilds rn.segs entirely from the
// reconstructed tree (replacing whatever it held going in), charges real
// wire usage for every edge, and emits + charges exactly one via chain
// per node, spanning that node's own resolved stack range
// (`nodeStackRange`, from `layerAssignDP` above) -- skipped entirely
// when a node's range is empty (every edge there already agrees).
// `charge`, this file's own new parameter (nullptr default preserves every
// pre-existing self-test call site's own behavior), logs every increment
// made here so the negotiation loop can undo/redo this exact commit later
// without a geometry re-walk -- see NetCharge's own comment above.
void commitNetTree(const RoutingGrid &g, RealUsage &ru, RealViaUsage &rvu, int width,
                    const NetTree &t, const std::vector<int> &edgeLayer,
                    const std::vector<std::pair<int, int>> &nodeStackRange, RoutedNet &rn,
                    NetCharge *charge = nullptr) {
  std::vector<Seg> segs;
  for (size_t ei = 0; ei < t.edges.size(); ++ei) {
    const NetTree::Edge &e = t.edges[ei];
    int l = edgeLayer[ei];
    int amount = width + g.minSpacing[l];
    for (int c = e.lo; c < e.hi; ++c) {
      size_t idx = e.horizontal ? g.hIndex(c, e.cross, l) : g.vIndex(e.cross, c, l);
      if (e.horizontal) {
        ru.h[idx] += amount;
        if (charge)
          charge->ruH.push_back({idx, amount});
      } else {
        ru.v[idx] += amount;
        if (charge)
          charge->ruV.push_back({idx, amount});
      }
    }
    if (e.horizontal)
      segs.push_back({e.lo, e.cross, l, e.hi, e.cross, l});
    else
      segs.push_back({e.cross, e.lo, l, e.cross, e.hi, l});
  }

  for (size_t u = 0; u < t.nodes.size(); ++u) {
    int lo = nodeStackRange[u].first, hi = nodeStackRange[u].second;
    if (lo >= hi)
      continue; // every edge at this node already agrees -- no stack needed
    int x = t.nodes[u].x, y = t.nodes[u].y;
    for (int l = lo; l < hi; ++l) {
      size_t idx = viaIndex(g, x, y, l);
      rvu.usage[idx] += 1;
      if (charge)
        charge->rvuIdx.push_back(idx);
      segs.push_back({x, y, l, x, y, l + 1});
    }
  }

  rn.segs = std::move(segs);
}

// Assigns one real layer to an already-merged straight-leg Seg (every wire
// Seg in this file's whole lineage already spans a full leg, not a unit
// tile-edge -- confirmed via commitPath's and commitCandidate's own merge-
// adjacent-same-direction-steps logic -- so this assigns one real layer
// per leg, matching the granularity every prior real-layer router in this
// project already uses). Picks, among the direction-matching real-layer
// candidates, the one with the lowest usage-over-capacity ratio summed
// over every unit tile-edge the leg covers -- then charges every one of
// those covered edges into `ru`, not just the first. Deliberately simpler
// than this file's own (permanently degenerate, single-candidate in this
// lineage) bestHLayer/bestVLayer: a plain usage/capacity ratio, no
// negotiated pricing, is the right amount of mechanism for a single-pass
// greedy fill -- see the file header for why a full parallel real-layer
// negotiation loop is out of scope for this generation.
void assignSegmentLayer(const RoutingGrid &g, RealUsage &ru, const RealLayerPlan &lp,
                         int width, Seg &s) {
  bool horizontal = s.y1 == s.y2;
  const std::vector<int> &candidates = horizontal ? lp.hLayers : lp.vLayers;
  int a = horizontal ? std::min(s.x1, s.x2) : std::min(s.y1, s.y2);
  int b = horizontal ? std::max(s.x1, s.x2) : std::max(s.y1, s.y2);
  int cross = horizontal ? s.y1 : s.x1;

  int best = candidates.front();
  double bestCost = std::numeric_limits<double>::max();
  for (int l : candidates) {
    int amount = width + g.minSpacing[l];
    long long usage = 0, cap = 0;
    for (int t = a; t < b; ++t) {
      size_t idx = horizontal ? g.hIndex(t, cross, l) : g.vIndex(cross, t, l);
      usage += (horizontal ? ru.h[idx] : ru.v[idx]) + amount;
      cap += std::max(1, horizontal ? g.hcap[idx] : g.vcap[idx]);
    }
    double cost = static_cast<double>(usage) / static_cast<double>(cap);
    if (cost < bestCost) {
      bestCost = cost;
      best = l;
    }
  }

  int amount = width + g.minSpacing[best];
  for (int t = a; t < b; ++t) {
    size_t idx = horizontal ? g.hIndex(t, cross, best) : g.vIndex(cross, t, best);
    if (horizontal)
      ru.h[idx] += amount;
    else
      ru.v[idx] += amount;
  }
  s.l1 = s.l2 = best;
}

// Bug found and fixed during this generation's own real-benchmark
// verification: the first version of stitchViasForNet only grouped by
// *segment endpoints*, but MSMV's whole defining mechanism (see
// fr1route.cpp's/vroute.cpp's header) is attaching a net's growing wire
// to any point on its own already-committed wire, mid-span, not just at
// the two ends of some other tree edge. Once a majority of nets escalate
// to the slow/MSMV tier (75993 of 176716 on adaptec1), a huge number of
// real layer mismatches happen exactly at these mid-span attachment
// points, which the endpoint-only version silently never saw -- gr_verify
// caught this as "route is in more than one piece" on thousands of real
// nets (5974 on adaptec1) even after the pin-access fix resolved the
// separate incomplete-nets bug. `coverageAt` below fixes this by
// recording every real layer present at *every unit tile* a Seg's full
// span covers, not just its two endpoints -- exactly matching how
// gr_verify's own connectivity check (and the real physical wire) treats
// two same-layer segments sharing any tile as already connected there,
// and two different-layer segments sharing a tile as needing a real via.
std::map<std::pair<int, int>, std::set<int>> coverageAt(const std::vector<Seg> &segs) {
  std::map<std::pair<int, int>, std::set<int>> cov;
  for (const Seg &s : segs) {
    if (s.l1 != s.l2) {
      // A via: a single point, present at both layers it bridges.
      cov[{s.x1, s.y1}].insert(s.l1);
      cov[{s.x1, s.y1}].insert(s.l2);
      continue;
    }
    if (s.x1 == s.x2) {
      for (int y = std::min(s.y1, s.y2); y <= std::max(s.y1, s.y2); ++y)
        cov[{s.x1, y}].insert(s.l1);
    } else {
      for (int x = std::min(s.x1, s.x2); x <= std::max(s.x1, s.x2); ++x)
        cov[{x, s.y1}].insert(s.l1);
    }
  }
  return cov;
}

// After every Seg in a net has a real layer, physically stitch together
// any tile where two legs (or a leg and a mid-span MSMV attachment)
// occupy the same (x,y) on different real layers -- a separate,
// unoptimized connectivity pass (via cost/capacity is not charged or
// optimized in this generation, see the file header). For every tile
// with more than one distinct layer present, picks the minimum as the
// reference layer and appends a chain of unit via Segs stepping from the
// reference to every other distinct layer present, one real layer at a
// time -- transitively connecting everything at that tile through the
// reference layer.
void stitchViasForNet(RoutedNet &rn) {
  auto cov = coverageAt(rn.segs);
  std::vector<Seg> vias;
  for (auto &entry : cov) {
    if (entry.second.size() < 2)
      continue; // every leg at this tile already agrees -- nothing to stitch
    std::vector<int> layers(entry.second.begin(), entry.second.end()); // std::set: already sorted
    int ref = layers.front();
    int x = entry.first.first, y = entry.first.second;
    for (size_t i = 1; i < layers.size(); ++i)
      for (int l = ref; l < layers[i]; ++l)
        vias.push_back({x, y, l, x, y, l + 1});
  }
  rn.segs.insert(rn.segs.end(), vias.begin(), vias.end());
}

// Bug found and fixed during this generation's own real-benchmark
// verification, not caught by any hand-built self-test until one was
// added specifically to catch it (see testPinAccessLayerIsAlwaysReached):
// gr_verify (matching eval2008.pl's own real scoring -- gr_verify.cpp's
// own comment: "The route must reach the pin's exact declared layer, not
// merely its tile on some other layer") requires a net's route to touch
// each pin's exact (x, y, layer) triple, not just its tile on any layer.
// fr1route through fr4route's uniform single-layer output happened to
// satisfy this by coincidence -- these benchmarks apparently declare
// most/all pins on layer index 0 -- masking the gap entirely; this file's
// own greedy layer choice, once wires genuinely spread across multiple
// real layers, broke it at massive scale (98% of adaptec1's routed nets
// came back "incomplete" before this fix). assignSegmentLayer/
// stitchViasForNet only ever reason about the *tree's own* internal
// layer choices -- neither one knows where a net's real pins declared
// they must be reached, because RoutedNet::pts never retained pin layers
// in the first place (route()'s own net-construction loop only ever kept
// {x, y}). This pass re-derives that requirement directly from
// `rn.net->pins` (untouched by anything upstream, including
// Steinerization, which only ever appends new points and never touches
// an original pin's own (x, y)) rather than threading pin layers through
// the whole Steinerization pipeline. Must run after stitchViasForNet (and
// re-derive coverage fresh, including its new vias), so a pin's own tile
// is checked against the single, fully-connected layer set
// stitchViasForNet already established there -- not a stale or partial
// view of it.
void stitchPinAccessForNet(RoutedNet &rn) {
  auto cov = coverageAt(rn.segs);
  std::vector<Seg> vias;
  for (const Pin &p : rn.net->pins) {
    auto it = cov.find({p.x, p.y});
    if (it == cov.end() || it->second.empty())
      continue; // the tree already spans every pin's (x,y) by construction
    if (it->second.count(p.layer))
      continue; // already reached
    int have = *it->second.begin(); // any layer already connected at this tile
    if (p.layer > have)
      for (int l = have; l < p.layer; ++l)
        vias.push_back({p.x, p.y, l, p.x, p.y, l + 1});
    else
      for (int l = have; l > p.layer; --l)
        vias.push_back({p.x, p.y, l, p.x, p.y, l - 1});
  }
  rn.segs.insert(rn.segs.end(), vias.begin(), vias.end());
}

// Bug found (a third one) during this generation's own real-benchmark
// verification: eval2008.pl's own scorer treats any segment whose two
// endpoints match exactly, on the same layer -- (x,y,l)-(x,y,l) -- as a
// hard error ("null route"). route()'s own inherited candidate-commit
// code (commitCandidate, this file's own priceCandidate/commitCandidate,
// unchanged since vroute.cpp) emits exactly this whenever an L/Z-shape
// candidate's own leg happens to be zero-length (its two endpoints
// already share that axis coordinate) -- `segs.push_back({leg.a,
// leg.cross, l, leg.b, leg.cross, l})` with leg.a==leg.b. This has been
// present, silently, in every router in this whole lineage since it was
// first written -- confirmed byte-for-byte identical in fr4route's own
// earlier real adaptec1 output (net n7807 alone had over 200 of them).
// It was never caught because gr_verify doesn't flag it and eval2008.pl
// was never run against any 2D-collapsed generation's raw single-layer
// output before this one. A zero-length segment contributes nothing to
// real wirelength or overflow either way, so dropping it changes no
// measured behavior -- it only removes dead output eval2008.pl is
// stricter about than gr_verify. Rather than patch multiple frozen,
// inherited emission sites (commitCandidate, commitPath, routeGiantNet),
// this generation -- already doing substantial segment post-processing
// -- filters every one out before layer assignment even runs.
void dropDegenerateSegs(RoutedNet &rn) {
  rn.segs.erase(std::remove_if(rn.segs.begin(), rn.segs.end(),
                                [](const Seg &s) {
                                  return s.x1 == s.x2 && s.y1 == s.y2 && s.l1 == s.l2;
                                }),
                rn.segs.end());
}

// This file's own new mechanism: identical to the inherited
// stitchPinAccessForNet above (a pin's own required layer is a fixed
// constraint, not a DP variable -- see the file header), except it also
// charges the new RealViaUsage for every via step it inserts, previously
// entirely uncharged even though via capacity is now modeled everywhere
// else. Kept as a separate function, rather than a flag on the original,
// so NV2ROUTE_VIA_AWARE=0's fallback path stays a byte-for-byte
// reproduction of fr5route's own uncharged behavior. `charge` (nullptr
// default) logs every rvu increment here too -- easy to forget, since it's
// charged outside commitNetTree proper; testChargeLogRoundTripIsExact
// exists specifically to catch a miss here.
void stitchPinAccessForNetChargingVias(const RoutingGrid &g, RealViaUsage &rvu, RoutedNet &rn,
                                        NetCharge *charge = nullptr) {
  auto cov = coverageAt(rn.segs);
  std::vector<Seg> vias;
  for (const Pin &p : rn.net->pins) {
    auto it = cov.find({p.x, p.y});
    if (it == cov.end() || it->second.empty())
      continue;
    if (it->second.count(p.layer))
      continue;
    int have = *it->second.begin();
    int lo = std::min(have, p.layer), hi = std::max(have, p.layer);
    for (int l = lo; l < hi; ++l) {
      size_t idx = viaIndex(g, p.x, p.y, l);
      rvu.usage[idx] += 1;
      if (charge)
        charge->rvuIdx.push_back(idx);
    }
    if (p.layer > have)
      for (int l = have; l < p.layer; ++l)
        vias.push_back({p.x, p.y, l, p.x, p.y, l + 1});
    else
      for (int l = have; l > p.layer; --l)
        vias.push_back({p.x, p.y, l, p.x, p.y, l - 1});
  }
  rn.segs.insert(rn.segs.end(), vias.begin(), vias.end());
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: the negotiation loop itself. Three small
// helpers first, each a direct port of a kroute.cpp counterpart (named in
// each one's own comment), generalized from kroute's separate H/V wire-edge
// arrays to this file's own combined "wire index + via index" charge log.
// ---------------------------------------------------------------------------

// Overflow summed over the union of two charge logs' own touched indices --
// kroute.cpp's own overflowOverUnion, ported. Deduplicates first (a
// resource touched by both `a` and `b` is counted once), so the caller is
// free to pass the same charge for both arguments (as the negotiation loop
// does for its own "before" measurement) without double-counting.
//
// `includeVia` (default true, preserving every pre-existing call site's own
// behavior): whether via-site usage-over-viaCap counts toward the total.
// Confirmed by reading gr_verify.cpp's own totalOverflow computation
// (gr_verify.cpp's own `scan` calls, husage/g.hcap and vusage/g.vcap only)
// -- the real ISPD scoring charges overflow against wire edges only; via
// sites are never a capacitied resource in the actual metric. `viaCap`
// here has always been this project's own internal heuristic (inherited
// from mroute, unchanged since fr6route) for steering the DP's cost
// function away from busy via sites, not a real constraint. The
// negotiation loop's own accept/revert decision passes false, so it judges
// outcomes by the metric that is actually scored; this function's own
// general-purpose default stays true so it remains reusable wherever via
// congestion genuinely is the thing being measured (as in this file's own
// testSupervisedRevertPrimitivesDetectAndUndoAWorseningMove).
long long localOverflow(const RoutingGrid &g, const RealUsage &ru, const RealViaUsage &rvu,
                         const std::vector<int> &viaCap, const NetCharge &a, const NetCharge &b,
                         bool includeVia = true) {
  std::set<size_t> hSeen, vSeen, rvuSeen;
  auto addRu = [&](const NetCharge &c) {
    for (const ChargeEntry &e : c.ruH)
      hSeen.insert(e.idx);
    for (const ChargeEntry &e : c.ruV)
      vSeen.insert(e.idx);
  };
  addRu(a);
  addRu(b);
  if (includeVia) {
    for (size_t idx : a.rvuIdx)
      rvuSeen.insert(idx);
    for (size_t idx : b.rvuIdx)
      rvuSeen.insert(idx);
  }
  size_t plane = static_cast<size_t>(g.nx) * static_cast<size_t>(g.ny);
  long long total = 0;
  // No std::max(1, ...) clamp here, deliberately -- unlike wireCostForEdge's
  // own use of that clamp (protecting a cost *denominator* from division by
  // zero), gr_verify.cpp's own totalOverflow computation (its `scan` calls:
  // `ov = use[i] - cap[i]`) never clamps capacity, so a real declared-zero
  // edge (a preferred-direction zeroing, or a .gr adjustment that reduces
  // capacity to 0) legitimately contributes its full usage as overflow, not
  // usage-1. Clamping here would silently undercount by exactly 1 per such
  // edge relative to the real scored metric -- found via the small (~0.4%)
  // gap between this file's own internal "best round" number and gr_verify's
  // independently-computed real overflow on the same output.
  for (size_t idx : hSeen)
    total += std::max(0LL, ru.h[idx] - g.hcap[idx]);
  for (size_t idx : vSeen)
    total += std::max(0LL, ru.v[idx] - g.vcap[idx]);
  for (size_t idx : rvuSeen) {
    int l = static_cast<int>(idx / plane);
    int cap = (l >= 0 && l < static_cast<int>(viaCap.size())) ? viaCap[l] : 1;
    total += std::max(0LL, rvu.usage[idx] - std::max(1, cap));
  }
  return total;
}

// Whether a net's own currently-charged resources are, right now, over
// capacity anywhere -- kroute.cpp's own netOverflowed, ported: O(that net's
// own charged cells), never a grid scan, so candidate selection stays cheap
// regardless of grid size. Deliberately keeps checking via sites against
// `viaCap` even though it is only a heuristic, not a real scored capacity
// (see localOverflow's own comment): this function only decides who is
// *eligible* for reprocessing, not whether a reprocessing attempt is kept,
// and a net sitting at a heavily-used via site is exactly the net whose own
// choice might change once historyVia (also driven by this same heuristic)
// has grown -- casting that wider net costs nothing but a cheap reprocessing
// attempt, which the real-overflow-only accept/revert step below will
// reject if it doesn't actually help.
bool netOverflowedVia(const RoutingGrid &g, const RealUsage &ru, const RealViaUsage &rvu,
                       const std::vector<int> &viaCap, const NetCharge &c) {
  // No std::max(1, ...) clamp on wire capacity here either -- same reason
  // as localOverflow's own comment: gr_verify's real definition never
  // clamps, and a genuinely zero-capacity edge with nonzero usage is a
  // real overflow, worth flagging as a reprocessing candidate.
  for (const ChargeEntry &e : c.ruH)
    if (ru.h[e.idx] > g.hcap[e.idx])
      return true;
  for (const ChargeEntry &e : c.ruV)
    if (ru.v[e.idx] > g.vcap[e.idx])
      return true;
  size_t plane = static_cast<size_t>(g.nx) * static_cast<size_t>(g.ny);
  for (size_t idx : c.rvuIdx) {
    int l = static_cast<int>(idx / plane);
    int cap = (l >= 0 && l < static_cast<int>(viaCap.size())) ? viaCap[l] : 1;
    if (rvu.usage[idx] > std::max(1, cap))
      return true;
  }
  return false;
}

// pd3route: netOverflowedVia without the via-site check (kNthu mode).
bool netOverflowedWire(const RoutingGrid &g, const RealUsage &ru, const NetCharge &c) {
  for (const ChargeEntry &e : c.ruH)
    if (ru.h[e.idx] > g.hcap[e.idx])
      return true;
  for (const ChargeEntry &e : c.ruV)
    if (ru.v[e.idx] > g.vcap[e.idx])
      return true;
  return false;
}

// Whole-grid total overflow -- kroute.cpp's own totalOverflow, generalized
// with the same `includeVia` escape hatch as localOverflow above (default
// true for general-purpose reuse; the negotiation loop's own round-boundary
// bookkeeping passes false so bestRound/convergence/patience all track the
// real, wire-only scored metric gr_verify reports, not a combined metric
// inflated by via sites that were never a real capacity to begin with).
long long totalRealOverflow(const RoutingGrid &g, const RealUsage &ru, const RealViaUsage &rvu,
                             const std::vector<int> &viaCap, bool includeVia = true) {
  long long total = 0;
  // No clamp -- see localOverflow's own comment.
  for (size_t i = 0; i < ru.h.size(); ++i)
    total += std::max(0LL, ru.h[i] - g.hcap[i]);
  for (size_t i = 0; i < ru.v.size(); ++i)
    total += std::max(0LL, ru.v[i] - g.vcap[i]);
  if (includeVia) {
    for (int l = 0; l < static_cast<int>(viaCap.size()); ++l) {
      int cap = std::max(1, viaCap[l]);
      for (int y = 0; y < g.ny; ++y)
        for (int x = 0; x < g.nx; ++x)
          total += std::max(0LL, rvu.usage[viaIndex(g, x, y, l)] - cap);
    }
  }
  return total;
}

struct ViaNegotiationReport {
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1;
  int roundsRun = 0;
  bool convergedToZero = false;
  bool stalled = false;
};

// This file's own new mechanism: bundles one history mechanism's own 8
// knobs into a single by-value struct, so negotiateViaAssignment's own
// signature (now configuring two independent history mechanisms, via and
// wire) doesn't grow past 20 raw parameters. Every field's default matches
// nv1route's own previous raw-parameter defaults exactly.
struct HistKnobs {
  double step = 0.05;
  HistMode mode = HistMode::kAdditive;
  double factor = 1.1;
  int streakThreshold = 1;
  bool scaleByOverflow = false;
  bool relaxEnabled = true;
  double ratioExponent = 1.0;
  double exponentEscalation = 0.0;
};

// The whole new stage, replacing fr7route's own one-shot assignLayers: drop
// any inherited degenerate zero-length segments, then either (default)
// reconstruct each net's real tree once and run round 0 of the joint
// via-cost/capacity-aware DP (see buildNetTree/layerAssignDP/commitNetTree
// above) exactly like fr7route's own single pass, then keep negotiating --
// or, if NV2ROUTE_VIA_AWARE=0 opts out globally, fall back to fr5route's
// own already-proven-correct independent-greedy-plus-stitching path (no
// negotiation loop makes sense without via-awareness). A net whose tree
// reconstruction or DP fails in round 0 (should be rare -- see
// buildNetTree's own comment) falls back to the same greedy path for that
// one net, still charged for consistency, and is never revisited again
// (there is no DP result to renegotiate). Mutates `nets` in place -- called
// once, after route() has fully converged, and before writeSolution.
// pd3route: which layer assignment runs. kInherited is nv2route's, byte
// for byte. kNthu is NTHU-Route 2.0's approach (Layerassignment.cpp): unit
// tile edges, hard wire capacity, via count as the objective, and NTHU's
// net order -- see the file header.
enum class LaMode { kInherited, kNthu };

bool parseLaMode(const char *v, LaMode &out, std::string &err) {
  if (!v) {
    out = LaMode::kNthu;
    return true;
  }
  std::string m(v);
  if (m == "nthu")
    out = LaMode::kNthu;
  else if (m == "inherited")
    out = LaMode::kInherited;
  else {
    err = "PD3ROUTE_LA must be nthu or inherited (got '" + m + "')";
    return false;
  }
  return true;
}

// Splits every multi-tile tree edge into unit (one-tile) edges, so the DP
// can change layer at any tile instead of holding a whole straight leg on
// one layer. Interior tiles get new nodes; they are never existing nodes
// (buildNetTree already splits a leg at every node lying on it), so the
// result is still a tree with the same tile coverage.
void splitToUnitEdges(const NetTree &in, NetTree &out) {
  out = NetTree();
  out.nodes = in.nodes;
  for (const NetTree::Edge &e : in.edges) {
    int prev = e.a;
    Point pa = in.nodes[e.a];
    int startCoord = e.horizontal ? pa.x : pa.y;
    int endCoord = e.horizontal ? in.nodes[e.b].x : in.nodes[e.b].y;
    if (startCoord == endCoord) { // defensive: buildNetTree never emits one
      out.edges.push_back(e);
      continue;
    }
    int step = endCoord > startCoord ? 1 : -1;
    for (int c = startCoord + step; c != endCoord; c += step) {
      int nn = static_cast<int>(out.nodes.size());
      out.nodes.push_back(e.horizontal ? Point{c, e.cross} : Point{e.cross, c});
      int lo = std::min(c - step, c);
      out.edges.push_back({prev, nn, e.horizontal, e.cross, lo, lo + 1});
      prev = nn;
    }
    int lo = std::min(endCoord - step, endCoord);
    out.edges.push_back({prev, e.b, e.horizontal, e.cross, lo, lo + 1});
  }
  out.adj.assign(out.nodes.size(), {});
  for (int ei = 0; ei < static_cast<int>(out.edges.size()); ++ei) {
    out.adj[out.edges[ei].a].push_back(ei);
    out.adj[out.edges[ei].b].push_back(ei);
  }
}

// ---------------------------------------------------------------------------
// pd6route's own mechanism: 2-pin-piece reroutes (NTHU-Route's granularity).
//
// A net's 2D wiring is taken as a tree of unit tile edges (buildNetTree with
// dropCycles and pin nodes, split to unit edges). Its key tiles are its pin
// tiles and every tile whose degree is not 2; a piece is a maximal path
// between two key tiles. Rerouting a piece: take it out (the tree falls into
// two parts, A holding one end and B the other), then reconnect A to B with
// one MSMV search from every tile of A to any tile of B -- NTHU reconnects
// only the piece's two endpoints; attaching anywhere on either part is
// strictly more freedom. The rest of the net never moves. The result is
// still a tree: the path's interior tiles are neither A's (the search would
// have started there) nor B's (it stops at the first B tile).
//
// Which pieces: those with an overflowed tile edge, or (box != nullptr)
// those with a tile inside the box. Each is attempted once per call, in the
// net's current state; `accept` decides whether a locally worse piece
// reroute is kept (kKeep) or undone (kRevert). On return the net's segs and
// charges are its unit tile edges on layer 0, usage matches them, and a
// dropped redundant cycle stretch is no longer charged.
// ---------------------------------------------------------------------------

struct TileBox; // defined with the region stage; only its fields are read here
bool tileInBox(const TileBox *box, int x, int y);

PieceStats rerouteNetPieces(const RoutingGrid &g, Usage &usage, const Prices &prices, const LayerPlan &lp,
                            SearchRegion &region, RoutedNet &rn, int margin, AcceptMode accept,
                            const TileBox *box) {
  PieceStats st;
  std::vector<Point> pinTiles;
  for (const Pin &p : rn.net->pins)
    pinTiles.push_back({p.x, p.y});
  NetTree legs, tree;
  if (!buildNetTree(rn.segs, legs, true, &pinTiles) || legs.edges.empty()) {
    st.ok = false;
    return st;
  }
  splitToUnitEdges(legs, tree);
  struct UE {
    bool h;
    int x, y; // h: (x,y)-(x+1,y); v: (x,y)-(x,y+1)
  };
  std::vector<UE> ue;
  for (const NetTree::Edge &e : tree.edges)
    ue.push_back(e.horizontal ? UE{true, e.lo, e.cross} : UE{false, e.cross, e.lo});
  const int amount = rn.net->minWidth + g.minSpacing[0];
  auto charge = [&](const UE &u) { return ChargeRecord{u.h, u.h ? u.y : u.x, u.h ? u.x : u.y, (u.h ? u.x : u.y) + 1, 0, amount}; };
  auto apply = [&](const ChargeRecord &c, int sign) {
    if (c.horizontal)
      usage.h[g.hIndex(c.a, c.cross, 0)] += sign * c.amount;
    else
      usage.v[g.vIndex(c.cross, c.a, 0)] += sign * c.amount;
  };
  // Re-sync the net to its tree: its old charges out, the tree's in.
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  for (const UE &u : ue)
    apply(charge(u), +1);
  auto key = [](int x, int y) { return (static_cast<long long>(x) << 32) | static_cast<unsigned>(y); };
  std::set<long long> pins;
  for (const Point &p : pinTiles)
    pins.insert(key(p.x, p.y));
  auto overflowed = [&](const UE &u) {
    size_t i = u.h ? g.hIndex(u.x, u.y, 0) : g.vIndex(u.x, u.y, 0);
    return u.h ? usage.h[i] > usage.capH[i] : usage.v[i] > usage.capV[i];
  };
  auto ends = [](const UE &u) {
    return std::make_pair(Point{u.x, u.y}, u.h ? Point{u.x + 1, u.y} : Point{u.x, u.y + 1});
  };
  std::set<std::pair<long long, long long>> tried; // piece endpoint pairs attempted this call
  for (int guard = 0; guard < 100000; ++guard) {
    // Tile adjacency and degree in the net's current state.
    std::map<long long, std::vector<int>> adj;
    for (int i = 0; i < static_cast<int>(ue.size()); ++i) {
      auto [a, b] = ends(ue[i]);
      adj[key(a.x, a.y)].push_back(i);
      adj[key(b.x, b.y)].push_back(i);
    }
    auto isKey = [&](long long t) { return pins.count(t) || adj[t].size() != 2; };
    // Find the next candidate piece.
    std::vector<char> used(ue.size(), 0);
    std::vector<int> piece;
    long long endA = 0, endB = 0;
    for (int start = 0; start < static_cast<int>(ue.size()) && piece.empty(); ++start) {
      if (used[start])
        continue;
      // Grow the maximal path through `start` in both directions.
      std::vector<int> path = {start};
      used[start] = 1;
      long long tips[2];
      auto [a0, b0] = ends(ue[start]);
      long long from[2] = {key(a0.x, a0.y), key(b0.x, b0.y)};
      for (int side = 0; side < 2; ++side) {
        long long t = from[side];
        int prevEdge = start;
        while (!isKey(t)) {
          int nextEdge = adj[t][0] == prevEdge ? adj[t][1] : adj[t][0];
          if (used[nextEdge])
            break;
          used[nextEdge] = 1;
          path.push_back(nextEdge);
          auto [na, nb] = ends(ue[nextEdge]);
          t = key(na.x, na.y) == t ? key(nb.x, nb.y) : key(na.x, na.y);
          prevEdge = nextEdge;
        }
        tips[side] = t;
      }
      auto pr = std::minmax(tips[0], tips[1]);
      if (tried.count(pr))
        continue;
      bool want = false;
      for (int e : path) {
        if (box) {
          auto [a, b] = ends(ue[e]);
          want = want || tileInBox(box, a.x, a.y) || tileInBox(box, b.x, b.y);
        } else {
          want = want || overflowed(ue[e]);
        }
      }
      if (want) {
        piece = path;
        endA = tips[0];
        endB = tips[1];
      }
    }
    if (piece.empty())
      break;
    tried.insert(std::minmax(endA, endB));
    // Split the rest of the tree into the part holding each end.
    std::vector<char> inPiece(ue.size(), 0);
    for (int e : piece)
      inPiece[e] = 1;
    std::map<long long, long long> uf;
    std::function<long long(long long)> find = [&](long long a) {
      auto it = uf.find(a);
      if (it == uf.end() || it->second == a)
        return a;
      return it->second = find(it->second);
    };
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i]) {
        auto [a, b] = ends(ue[i]);
        long long ra = find(key(a.x, a.y)), rb = find(key(b.x, b.y));
        if (ra != rb)
          uf[ra] = rb;
      }
    long long rootA = find(endA), rootB = find(endB);
    if (rootA == rootB)
      continue; // not a tree after all -- leave this piece alone
    std::vector<PathNode> sources;
    std::vector<Point> sinks;
    std::set<long long> seenT;
    auto addTile = [&](long long t) {
      if (!seenT.insert(t).second)
        return;
      int x = static_cast<int>(t >> 32), y = static_cast<int>(static_cast<unsigned>(t & 0xffffffffLL));
      long long r = find(t);
      if (r == rootA)
        sources.push_back({x, y, 0});
      else if (r == rootB)
        sinks.push_back({x, y});
    };
    addTile(endA);
    addTile(endB);
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i]) {
        auto [a, b] = ends(ue[i]);
        addTile(key(a.x, a.y));
        addTile(key(b.x, b.y));
      }
    std::vector<ChargeRecord> oldCh;
    for (int e : piece)
      oldCh.push_back(charge(ue[e]));
    long long before = overflowOverUnion(g, usage, oldCh, oldCh);
    for (const ChargeRecord &c : oldCh)
      apply(c, -1);
    std::vector<UE> fresh;
    // pd10route: pattern first (NTHU's monotonic-before-maze rule). The
    // piece's own two endpoints are joined by the cheapest L or Z shape
    // that fits -- every tile edge has room for this net, and no tile of it
    // other than the two endpoints is already on the net's tree. Only when
    // no shape fits does the maze search run.
    bool patterned = false;
    if (patternFirst) {
      Point pa{static_cast<int>(endA >> 32), static_cast<int>(static_cast<unsigned>(endA & 0xffffffffLL))};
      Point pb{static_cast<int>(endB >> 32), static_cast<int>(static_cast<unsigned>(endB & 0xffffffffLL))};
      double bestCost = std::numeric_limits<double>::infinity();
      std::vector<UE> best;
      for (const Candidate &cand : generateCandidates(pa, pb)) {
        std::vector<UE> edges;
        for (const Leg &leg : cand)
          for (int c = leg.a; c < leg.b; ++c)
            edges.push_back(leg.horizontal ? UE{true, c, leg.cross} : UE{false, leg.cross, c});
        bool fits = true;
        double cost = 0;
        for (const UE &u : edges) {
          size_t i = u.h ? g.hIndex(u.x, u.y, 0) : g.vIndex(u.x, u.y, 0);
          long long room = u.h ? usage.capH[i] - usage.h[i] : usage.capV[i] - usage.v[i];
          if (room < amount) {
            fits = false;
            break;
          }
          auto [ta, tb] = ends(u);
          for (const Point &t : {ta, tb}) {
            long long k = key(t.x, t.y);
            if (k != endA && k != endB && seenT.count(k)) {
              fits = false;
              break;
            }
          }
          if (!fits)
            break;
          cost += u.h ? wireCostH(prices, i, amount) : wireCostV(prices, i, amount);
        }
        if (fits && cost < bestCost) {
          bestCost = cost;
          best = std::move(edges);
        }
      }
      if (!best.empty()) {
        fresh = std::move(best);
        patterned = true;
        ++st.patterned;
        ++patternedTotal;
      }
    }
    if (!patterned) {
      MSMVResult r = routeStepMSMV(g, prices, lp, region, sources, sinks, rn.net->minWidth, margin, true);
      if (!r.found) {
        for (const ChargeRecord &c : oldCh)
          apply(c, +1);
        continue;
      }
      for (size_t k = 1; k < r.path.size(); ++k) {
        const PathNode &p = r.path[k - 1], &q = r.path[k];
        fresh.push_back(p.y == q.y ? UE{true, std::min(p.x, q.x), p.y} : UE{false, p.x, std::min(p.y, q.y)});
      }
    }
    ++piecesTotal;
    std::vector<ChargeRecord> newCh;
    for (const UE &u : fresh) {
      newCh.push_back(charge(u));
      apply(newCh.back(), +1);
    }
    ++st.pieces;
    long long after = overflowOverUnion(g, usage, oldCh, newCh);
    if (revertReroute(accept, before, after)) {
      for (const ChargeRecord &c : newCh)
        apply(c, -1);
      for (const ChargeRecord &c : oldCh)
        apply(c, +1);
      ++st.reverted;
      continue;
    }
    std::vector<UE> next;
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i])
        next.push_back(ue[i]);
    next.insert(next.end(), fresh.begin(), fresh.end());
    ue = std::move(next);
  }
  rn.segs.clear();
  rn.charges.clear();
  for (const UE &u : ue) {
    rn.charges.push_back(charge(u));
    rn.segs.push_back(u.h ? Seg{u.x, u.y, 0, u.x + 1, u.y, 0} : Seg{u.x, u.y, 0, u.x, u.y + 1, 0});
  }
  return st;
}

// ---------------------------------------------------------------------------
// pd7route's own mechanism: overflow-safe wirelength and bend recovery.
//
// After the 2D stages, every piece of every net (pieces as in
// rerouteNetPieces) is taken out and the two parts of the net's tree are
// reconnected by a bend-aware search: 1 per tile edge plus `bend` per change
// of direction, and a tile edge is usable only if this piece fits in it
// (usage + amount <= capacity). The new piece is kept only if its cost is
// strictly lower than the old piece's (its length plus `bend` per interior
// corner). Since every tile edge the new piece uses has room for it, the
// local overflow never rises -- a zero-overflow solution stays zero.
//
// Why bends: every 2D corner forces at least one via (horizontal and
// vertical wires live on different layers), and on adaptec1 this project's
// vias outnumber NTHU-Route's by 2.2x (3.93M vs 1.77M) against 1.3x for wire
// (4.75M vs 3.59M). The negotiation stages price congestion only (the WL
// weight decays to ~1e-4) and never price a turn.
// ---------------------------------------------------------------------------

struct WlSettings {
  bool enabled = true; // PD7ROUTE_WL
  // Defaults from the adaptec1 sweep (HISTORY_COST_SWEEP.md Part 69): the
  // box margin mattered more than the corner cost (bend 3 / 4 passes:
  // margin 3 -> 6050170 scored WL, margin 8 -> 5991707).
  int passes = 4;      // PD7ROUTE_WL_PASSES
  double bend = 3.0;   // PD7ROUTE_WL_BEND: cost of a corner, in tile edges
  int margin = 8;      // PD7ROUTE_WL_MARGIN: box around the two parts
};

bool parseWlSettings(const char *enabled, const char *passes, const char *bend, const char *margin,
                     WlSettings &out, std::string &err) {
  WlSettings w;
  if (enabled) {
    std::string e(enabled);
    if (e != "0" && e != "1") {
      err = "PD7ROUTE_WL must be 0 or 1 (got '" + e + "')";
      return false;
    }
    w.enabled = e == "1";
  }
  auto parseInt = [&](const char *v, const char *name, int lo, int &dst) {
    if (!v)
      return true;
    char *end = nullptr;
    long x = std::strtol(v, &end, 10);
    if (end == v || *end != '\0' || x < lo || x > 1000000) {
      err = std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')";
      return false;
    }
    dst = static_cast<int>(x);
    return true;
  };
  if (!parseInt(passes, "PD7ROUTE_WL_PASSES", 0, w.passes) || !parseInt(margin, "PD7ROUTE_WL_MARGIN", 0, w.margin))
    return false;
  if (bend) {
    char *end = nullptr;
    double b = std::strtod(bend, &end);
    if (end == bend || *end != '\0' || !std::isfinite(b) || b < 0.0) {
      err = std::string("PD7ROUTE_WL_BEND must be a finite number >= 0 (got '") + bend + "')";
      return false;
    }
    w.bend = b;
  }
  out = w;
  return true;
}

// Bend-aware multi-source multi-sink search over (tile, arrival direction)
// states. Sources start in both directions at cost 0 (the attachment is
// free to leave either way); reaching any sink tile ends the search.
// `usable(h, x, y)` says whether the tile edge (h: (x,y)-(x+1,y); else
// (x,y)-(x,y+1)) may be used. A* with Manhattan distance to the sinks'
// bounding box (every step costs >= 1): admissible and consistent.
// Returns the path's tiles, source first, and its cost; empty if none.
std::vector<Point> bendAwareSearch(int x0, int y0, int x1, int y1, const std::vector<Point> &sources,
                                   const std::vector<Point> &sinks, double bend,
                                   const std::function<bool(bool, int, int)> &usable, double &cost) {
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  auto id = [&](int x, int y, int d) { return ((y - y0) * w + (x - x0)) * 2 + d; };
  std::vector<double> dist(static_cast<size_t>(w) * h * 2, std::numeric_limits<double>::infinity());
  std::vector<int> par(dist.size(), -1);
  std::vector<char> isSink(static_cast<size_t>(w) * h, 0);
  int sx0 = sinks[0].x, sx1 = sinks[0].x, sy0 = sinks[0].y, sy1 = sinks[0].y;
  for (const Point &s : sinks) {
    if (s.x < x0 || s.x > x1 || s.y < y0 || s.y > y1)
      continue;
    isSink[(s.y - y0) * w + (s.x - x0)] = 1;
    sx0 = std::min(sx0, s.x);
    sx1 = std::max(sx1, s.x);
    sy0 = std::min(sy0, s.y);
    sy1 = std::max(sy1, s.y);
  }
  auto hval = [&](int x, int y) {
    return static_cast<double>(std::max({0, sx0 - x, x - sx1}) + std::max({0, sy0 - y, y - sy1}));
  };
  using QE = std::pair<double, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
  for (const Point &s : sources) {
    if (s.x < x0 || s.x > x1 || s.y < y0 || s.y > y1)
      continue;
    for (int d = 0; d < 2; ++d) {
      dist[id(s.x, s.y, d)] = 0;
      q.push({hval(s.x, s.y), id(s.x, s.y, d)});
    }
  }
  int found = -1;
  while (!q.empty()) {
    auto [f, st] = q.top();
    q.pop();
    int tile = st / 2, d = st % 2;
    int x = x0 + tile % w, y = y0 + tile / w;
    double dv = dist[st];
    if (f > dv + hval(x, y) + 1e-9)
      continue;
    if (isSink[tile]) {
      found = st;
      break;
    }
    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int k = 0; k < 4; ++k) {
      int nx = x + dx[k], ny = y + dy[k];
      if (nx < x0 || nx > x1 || ny < y0 || ny > y1)
        continue;
      bool hz = k < 2;
      if (!usable(hz, std::min(x, nx), std::min(y, ny)))
        continue;
      int nd = hz ? 0 : 1;
      double nc = dv + 1.0 + (nd != d ? bend : 0.0);
      int ns = id(nx, ny, nd);
      if (nc < dist[ns] - 1e-12) {
        dist[ns] = nc;
        par[ns] = st;
        q.push({nc + hval(nx, ny), ns});
      }
    }
  }
  std::vector<Point> path;
  if (found < 0)
    return path;
  cost = dist[found];
  for (int st = found; st != -1; st = par[st])
    path.push_back({x0 + (st / 2) % w, y0 + (st / 2) / w});
  std::reverse(path.begin(), path.end());
  // Consecutive states on the same tile cannot occur (every move changes
  // tile), so the path is a tile sequence.
  return path;
}

struct WlReport {
  long long wireBefore = 0, wireAfter = 0;
  int piecesImproved = 0;
  BatchReport batches; // pd26route
};

long long total2DWire(const std::vector<RoutedNet> &nets) {
  long long t = 0;
  for (const RoutedNet &rn : nets)
    for (const ChargeRecord &c : rn.charges)
      t += c.b - c.a;
  return t;
}

// One net: every piece once, in the net's current state.
int wlRecoverNet(const RoutingGrid &g, Usage &usage, RoutedNet &rn, const WlSettings &ws) {
  std::vector<Point> pinTiles;
  for (const Pin &p : rn.net->pins)
    pinTiles.push_back({p.x, p.y});
  NetTree legs, tree;
  if (!buildNetTree(rn.segs, legs, true, &pinTiles) || legs.edges.empty())
    return 0;
  splitToUnitEdges(legs, tree);
  struct UE {
    bool h;
    int x, y;
  };
  std::vector<UE> ue;
  for (const NetTree::Edge &e : tree.edges)
    ue.push_back(e.horizontal ? UE{true, e.lo, e.cross} : UE{false, e.cross, e.lo});
  const int amount = rn.net->minWidth + g.minSpacing[0];
  auto idx = [&](const UE &u) { return u.h ? g.hIndex(u.x, u.y, 0) : g.vIndex(u.x, u.y, 0); };
  auto add = [&](const UE &u, int sign) {
    if (u.h)
      usage.h[idx(u)] += sign * amount;
    else
      usage.v[idx(u)] += sign * amount;
  };
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  for (const UE &u : ue)
    add(u, +1);
  auto key = [](int x, int y) { return (static_cast<long long>(x) << 32) | static_cast<unsigned>(y); };
  auto ends = [](const UE &u) {
    return std::make_pair(Point{u.x, u.y}, u.h ? Point{u.x + 1, u.y} : Point{u.x, u.y + 1});
  };
  std::set<long long> pins;
  for (const Point &p : pinTiles)
    pins.insert(key(p.x, p.y));
  std::set<std::pair<long long, long long>> tried;
  int improved = 0;
  for (int guard = 0; guard < 100000; ++guard) {
    std::map<long long, std::vector<int>> adj;
    for (int i = 0; i < static_cast<int>(ue.size()); ++i) {
      auto [a, b] = ends(ue[i]);
      adj[key(a.x, a.y)].push_back(i);
      adj[key(b.x, b.y)].push_back(i);
    }
    auto isKey = [&](long long t) { return pins.count(t) || adj[t].size() != 2; };
    std::vector<char> used(ue.size(), 0);
    std::vector<int> piece;
    long long endA = 0, endB = 0;
    for (int start = 0; start < static_cast<int>(ue.size()) && piece.empty(); ++start) {
      if (used[start])
        continue;
      std::vector<int> path = {start};
      used[start] = 1;
      long long tips[2];
      auto [a0, b0] = ends(ue[start]);
      long long from[2] = {key(a0.x, a0.y), key(b0.x, b0.y)};
      for (int side = 0; side < 2; ++side) {
        long long t = from[side];
        int prevEdge = start;
        while (!isKey(t)) {
          int nextEdge = adj[t][0] == prevEdge ? adj[t][1] : adj[t][0];
          if (used[nextEdge])
            break;
          used[nextEdge] = 1;
          path.push_back(nextEdge);
          auto [na, nb] = ends(ue[nextEdge]);
          t = key(na.x, na.y) == t ? key(nb.x, nb.y) : key(na.x, na.y);
          prevEdge = nextEdge;
        }
        tips[side] = t;
      }
      if (tried.count(std::minmax(tips[0], tips[1])))
        continue;
      piece = path;
      endA = tips[0];
      endB = tips[1];
    }
    if (piece.empty())
      break;
    tried.insert(std::minmax(endA, endB));
    // Old cost: length + bend per interior corner.
    std::map<long long, int> orient; // tile -> bit 1 = has H piece edge, bit 2 = has V
    for (int e : piece) {
      auto [a, b] = ends(ue[e]);
      orient[key(a.x, a.y)] |= ue[e].h ? 1 : 2;
      orient[key(b.x, b.y)] |= ue[e].h ? 1 : 2;
    }
    int corners = 0;
    for (auto &[t, o] : orient)
      if (o == 3 && t != endA && t != endB)
        ++corners;
    double oldCost = static_cast<double>(piece.size()) + ws.bend * corners;
    std::vector<char> inPiece(ue.size(), 0);
    for (int e : piece)
      inPiece[e] = 1;
    std::map<long long, long long> uf;
    std::function<long long(long long)> find = [&](long long a) {
      auto it = uf.find(a);
      if (it == uf.end() || it->second == a)
        return a;
      return it->second = find(it->second);
    };
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i]) {
        auto [a, b] = ends(ue[i]);
        long long ra = find(key(a.x, a.y)), rb = find(key(b.x, b.y));
        if (ra != rb)
          uf[ra] = rb;
      }
    long long rootA = find(endA), rootB = find(endB);
    if (rootA == rootB)
      continue;
    std::vector<Point> src, snk;
    std::set<long long> seenT;
    int bx0 = g.nx, by0 = g.ny, bx1 = -1, by1 = -1;
    auto addTile = [&](long long t) {
      if (!seenT.insert(t).second)
        return;
      int x = static_cast<int>(t >> 32), y = static_cast<int>(static_cast<unsigned>(t & 0xffffffffLL));
      long long r = find(t);
      if (r != rootA && r != rootB)
        return;
      (r == rootA ? src : snk).push_back({x, y});
      bx0 = std::min(bx0, x);
      bx1 = std::max(bx1, x);
      by0 = std::min(by0, y);
      by1 = std::max(by1, y);
    };
    addTile(endA);
    addTile(endB);
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i]) {
        auto [a, b] = ends(ue[i]);
        addTile(key(a.x, a.y));
        addTile(key(b.x, b.y));
      }
    for (int e : piece)
      add(ue[e], -1);
    auto usable = [&](bool hz, int x, int y) {
      size_t i = hz ? g.hIndex(x, y, 0) : g.vIndex(x, y, 0);
      return hz ? usage.h[i] + amount <= usage.capH[i] : usage.v[i] + amount <= usage.capV[i];
    };
    double newCost = 0;
    std::vector<Point> p = bendAwareSearch(std::max(0, bx0 - ws.margin), std::max(0, by0 - ws.margin),
                                           std::min(g.nx - 1, bx1 + ws.margin), std::min(g.ny - 1, by1 + ws.margin),
                                           src, snk, ws.bend, usable, newCost);
    if (p.empty() || newCost >= oldCost - 1e-9) {
      for (int e : piece)
        add(ue[e], +1);
      continue;
    }
    std::vector<UE> next;
    for (int i = 0; i < static_cast<int>(ue.size()); ++i)
      if (!inPiece[i])
        next.push_back(ue[i]);
    for (size_t k = 1; k < p.size(); ++k) {
      UE u = p[k].y == p[k - 1].y ? UE{true, std::min(p[k].x, p[k - 1].x), p[k].y}
                                  : UE{false, p[k].x, std::min(p[k].y, p[k - 1].y)};
      add(u, +1);
      next.push_back(u);
    }
    ue = std::move(next);
    ++improved;
  }
  rn.segs.clear();
  rn.charges.clear();
  for (const UE &u : ue) {
    rn.charges.push_back({u.h, u.h ? u.y : u.x, u.h ? u.x : u.y, (u.h ? u.x : u.y) + 1, 0, amount});
    rn.segs.push_back(u.h ? Seg{u.x, u.y, 0, u.x + 1, u.y, 0} : Seg{u.x, u.y, 0, u.x, u.y + 1, 0});
  }
  return improved;
}

WlReport wlRecover2D(const Design &d, std::vector<RoutedNet> &nets, const WlSettings &ws, bool trace) {
  const RoutingGrid &g = d.grid;
  WlReport rep;
  Usage usage = usageFromCharges(g, nets);
  rep.wireBefore = total2DWire(nets);
  long long overflowBefore = totalOverflow(g, usage);
  for (int pass = 0; pass < ws.passes; ++pass) {
    int improved = 0;
    if (gParallel.threads > 0) {
      // pd26route: batched.
      std::vector<size_t> order;
      for (size_t i = 0; i < nets.size(); ++i)
        if (!nets[i].giant && nets[i].net->pins.size() >= 2)
          order.push_back(i);
      Prices pr(g);
      pr.live = &usage;
      SearchRegion reg;
      LayerPlan lp(g);
      auto task = [&](Usage &u, Prices &, SearchRegion &, const LayerPlan &, RoutedNet &rn) {
        TaskStats s;
        s.n[0] = wlRecoverNet(g, u, rn, ws);
        return s;
      };
      TaskStats ts = runBatchedRound(g, nets, order, usage, pr, reg, lp, gParallel, task, rep.batches);
      improved = static_cast<int>(ts.n[0]);
    }
    for (RoutedNet &rn : nets)
      if (gParallel.threads == 0 && !rn.giant && rn.net->pins.size() >= 2)
        improved += wlRecoverNet(g, usage, rn, ws);
    rep.piecesImproved += improved;
    if (trace)
      std::cerr << "wl recovery pass " << pass << ": " << improved << " pieces improved, 2D wire "
                << total2DWire(nets) << ", 2D overflow " << totalOverflow(g, usage) << "\n";
    if (improved == 0)
      break;
  }
  rep.wireAfter = total2DWire(nets);
  if (totalOverflow(g, usage) > overflowBefore)
    fail("pd7route: wirelength recovery raised 2D overflow -- the capacity guard is broken");
  return rep;
}

// ---------------------------------------------------------------------------
// pd8route's own mechanism: overflow-safe whole-net tree rebuild.
//
// Piece recovery (pd7route) improves each piece but never changes a net's
// topology; on adaptec1 the negotiated trees still carry 63% more 2D
// corners and 13% more wire than NTHU-Route's (Part 69). Here each net is
// taken out and regrown from its pin tiles alone, Prim-style: starting at
// one pin, the bend-aware search repeatedly connects the cheapest remaining
// pin to anywhere on the tree built so far, over only tile edges with room
// for this net. The new tree replaces the old one only if its shape cost --
// tile edges + bend * corner tiles (tiles where the net has both a
// horizontal and a vertical edge) -- is strictly lower; otherwise the old
// tree is restored exactly. Every tile edge the new tree uses has room, so
// overflow cannot rise.
// ---------------------------------------------------------------------------

struct RebuildSettings {
  bool enabled = true; // PD8ROUTE_REBUILD
  int margin = 16;     // PD8ROUTE_REBUILD_MARGIN: tiles around the pins' bbox (adaptec1: 8 -> 5991122, 16 -> 5958029)
  int maxPins = 32;    // PD8ROUTE_REBUILD_MAX_PINS: larger nets are left alone (cost)
};

struct UnitEdge {
  bool h;
  int x, y; // h: (x,y)-(x+1,y); else (x,y)-(x,y+1)
  bool operator<(const UnitEdge &o) const { return std::tie(h, x, y) < std::tie(o.h, o.x, o.y); }
};

std::set<UnitEdge> unitEdgesOf(const std::vector<Seg> &segs) {
  std::set<UnitEdge> out;
  for (const Seg &s : segs) {
    if (s.l1 != s.l2)
      continue;
    if (s.y1 == s.y2)
      for (int x = std::min(s.x1, s.x2); x < std::max(s.x1, s.x2); ++x)
        out.insert({true, x, s.y1});
    else
      for (int y = std::min(s.y1, s.y2); y < std::max(s.y1, s.y2); ++y)
        out.insert({false, s.x1, y});
  }
  return out;
}

double shapeCost(const std::set<UnitEdge> &ue, double bend) {
  std::map<std::pair<int, int>, int> o;
  for (const UnitEdge &u : ue) {
    int b = u.h ? 1 : 2;
    o[{u.x, u.y}] |= b;
    o[u.h ? std::make_pair(u.x + 1, u.y) : std::make_pair(u.x, u.y + 1)] |= b;
  }
  int corners = 0;
  for (auto &[t, b] : o)
    corners += b == 3;
  return static_cast<double>(ue.size()) + bend * corners;
}

// Returns true if the net was replaced by a cheaper tree.
bool rebuildNet(const RoutingGrid &g, Usage &usage, RoutedNet &rn, double bend, const RebuildSettings &rs) {
  std::vector<Point> pins;
  std::set<std::pair<int, int>> seenPin;
  for (const Pin &p : rn.net->pins)
    if (seenPin.insert({p.x, p.y}).second)
      pins.push_back({p.x, p.y});
  if (pins.size() < 2 || static_cast<int>(pins.size()) > rs.maxPins)
    return false;
  std::set<UnitEdge> old = unitEdgesOf(rn.segs);
  if (old.empty())
    return false;
  const int amount = rn.net->minWidth + g.minSpacing[0];
  auto add = [&](const UnitEdge &u, int sign) {
    if (u.h)
      usage.h[g.hIndex(u.x, u.y, 0)] += sign * amount;
    else
      usage.v[g.vIndex(u.x, u.y, 0)] += sign * amount;
  };
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  int x0 = pins[0].x, x1 = x0, y0 = pins[0].y, y1 = y0;
  for (const Point &p : pins) {
    x0 = std::min(x0, p.x);
    x1 = std::max(x1, p.x);
    y0 = std::min(y0, p.y);
    y1 = std::max(y1, p.y);
  }
  x0 = std::max(0, x0 - rs.margin);
  y0 = std::max(0, y0 - rs.margin);
  x1 = std::min(g.nx - 1, x1 + rs.margin);
  y1 = std::min(g.ny - 1, y1 + rs.margin);
  auto usable = [&](bool hz, int x, int y) {
    size_t i = hz ? g.hIndex(x, y, 0) : g.vIndex(x, y, 0);
    return hz ? usage.h[i] + amount <= usage.capH[i] : usage.v[i] + amount <= usage.capV[i];
  };
  std::set<UnitEdge> fresh;
  std::set<std::pair<int, int>> treeTiles = {{pins[0].x, pins[0].y}};
  std::vector<Point> remaining(pins.begin() + 1, pins.end());
  bool ok = true;
  while (!remaining.empty()) {
    std::vector<Point> src;
    for (auto &t : treeTiles)
      src.push_back({t.first, t.second});
    double c = 0;
    std::vector<Point> p = bendAwareSearch(x0, y0, x1, y1, src, remaining, bend, usable, c);
    if (p.empty()) {
      ok = false;
      break;
    }
    for (size_t k = 1; k < p.size(); ++k) {
      UnitEdge u = p[k].y == p[k - 1].y ? UnitEdge{true, std::min(p[k].x, p[k - 1].x), p[k].y}
                                        : UnitEdge{false, p[k].x, std::min(p[k].y, p[k - 1].y)};
      if (fresh.insert(u).second)
        add(u, +1);
    }
    for (const Point &q : p)
      treeTiles.insert({q.x, q.y});
    std::vector<Point> still;
    for (const Point &r : remaining)
      if (!treeTiles.count({r.x, r.y}))
        still.push_back(r);
    remaining = std::move(still);
  }
  if (ok && shapeCost(fresh, bend) < shapeCost(old, bend) - 1e-9) {
    rn.segs.clear();
    rn.charges.clear();
    for (const UnitEdge &u : fresh) {
      rn.charges.push_back({u.h, u.h ? u.y : u.x, u.h ? u.x : u.y, (u.h ? u.x : u.y) + 1, 0, amount});
      rn.segs.push_back(u.h ? Seg{u.x, u.y, 0, u.x + 1, u.y, 0} : Seg{u.x, u.y, 0, u.x, u.y + 1, 0});
    }
    return true;
  }
  for (const UnitEdge &u : fresh)
    add(u, -1);
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal)
      for (int x = c.a; x < c.b; ++x)
        usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
    else
      for (int y = c.a; y < c.b; ++y)
        usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
  }
  return false;
}

struct RebuildReport {
  long long wireBefore = 0, wireAfter = 0;
  int rebuilt = 0;
  BatchReport batches; // pd26route
};

RebuildReport rebuild2D(const Design &d, std::vector<RoutedNet> &nets, double bend, const RebuildSettings &rs,
                        bool trace) {
  const RoutingGrid &g = d.grid;
  RebuildReport rep;
  Usage usage = usageFromCharges(g, nets);
  long long overflowBefore = totalOverflow(g, usage);
  rep.wireBefore = total2DWire(nets);
  if (gParallel.threads > 0) {
    // pd26route: batched.
    std::vector<size_t> order;
    for (size_t i = 0; i < nets.size(); ++i)
      if (!nets[i].giant)
        order.push_back(i);
    Prices pr(g);
    pr.live = &usage;
    SearchRegion reg;
    LayerPlan lp(g);
    auto task = [&](Usage &u, Prices &, SearchRegion &, const LayerPlan &, RoutedNet &rn) {
      TaskStats s;
      s.n[0] = rebuildNet(g, u, rn, bend, rs) ? 1 : 0;
      return s;
    };
    TaskStats ts = runBatchedRound(g, nets, order, usage, pr, reg, lp, gParallel, task, rep.batches);
    rep.rebuilt = static_cast<int>(ts.n[0]);
  }
  for (RoutedNet &rn : nets)
    if (gParallel.threads == 0 && !rn.giant && rebuildNet(g, usage, rn, bend, rs))
      ++rep.rebuilt;
  rep.wireAfter = total2DWire(nets);
  if (trace)
    std::cerr << "tree rebuild: " << rep.rebuilt << " nets rebuilt, 2D wire " << rep.wireBefore << " -> "
              << rep.wireAfter << ", 2D overflow " << totalOverflow(g, usage) << "\n";
  if (totalOverflow(g, usage) > overflowBefore)
    fail("pd8route: tree rebuild raised 2D overflow -- the capacity guard is broken");
  return rep;
}

// NTHU's layer-assignment net order (Layerassignment.cpp, find_group /
// comp_temp_net_order): nets with the most pins per 2D tile edge used go
// first; ties go to the net with fewer pins. `tileEdges[i]` is net i's 2D
// wirelength in tile edges; a net with none is placed first (nothing to
// assign, no contention). Stable, so equal keys keep input order.
std::vector<size_t> laNetOrder(const std::vector<size_t> &pins, const std::vector<long long> &tileEdges) {
  std::vector<size_t> order(pins.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  auto avg = [&](size_t i) {
    return tileEdges[i] <= 0 ? std::numeric_limits<double>::infinity()
                             : static_cast<double>(pins[i]) / static_cast<double>(tileEdges[i]);
  };
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    double fa = avg(a), fb = avg(b);
    if (fa != fb)
      return fa > fb;
    return pins[a] < pins[b];
  });
  return order;
}

ViaNegotiationReport negotiateViaAssignment(const RoutingGrid &g, std::vector<RoutedNet> &nets,
                                             int maxRounds = 60, int patience = 20,
                                             double viaWeight = 1.0,
                                             HistKnobs viaKnobs = HistKnobs(),
                                             HistKnobs wireKnobs = HistKnobs(),
                                             LaMode laMode = LaMode::kInherited, bool laPins = false) {
  ViaNegotiationReport rep;
  const bool nthu = laMode == LaMode::kNthu;
  // pd7route: per-net, per-tree-node pin layer ranges (kNthu with laPins).
  std::vector<std::vector<std::pair<int, int>>> pinRange(nets.size());
  RealLayerPlan lp(g);
  RealUsage ru(g);
  RealViaUsage rvu(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  ViaPrices vp(g);
  WirePrices wireHist(g);
  bool trace = std::getenv("NV2ROUTE_TRACE") != nullptr;

  bool viaAware = true;
  if (const char *r = std::getenv("NV2ROUTE_VIA_AWARE"); r)
    viaAware = std::string(r) != "0";
  // This file's own new mechanism: independent of viaAware -- a run can be
  // via-aware, wire-aware, both, or neither, for clean ablation testing.
  // 0 passes nullptr for both historyWire pointers below, reproducing
  // nv1route's exact output for direct comparison.
  bool wireAware = true;
  if (const char *r = std::getenv("NV2ROUTE_WIRE_AWARE"); r)
    wireAware = std::string(r) != "0";
  const std::vector<double> *wireHistH = wireAware ? &wireHist.histH : nullptr;
  const std::vector<double> *wireHistV = wireAware ? &wireHist.histV : nullptr;

  std::vector<NetCharge> charges(nets.size());
  std::vector<NetTree> trees(nets.size());
  std::vector<char> usesTreeDP(nets.size(), 0);

  // Round 0 -- exactly fr7route's own single pass. With historyVia and
  // historyWire all 1.0 (their own initial values) this is byte-identical
  // to fr7route's own output at this point, a free regression checkpoint
  // before any negotiation begins. Each net's own NetTree is built here and
  // reused every later round -- buildNetTree assumes pure 2D wire geometry,
  // and rebuilding it from a net's own *committed* 3D output (which mixes
  // wire segs with via segs) would corrupt topology reconstruction, so
  // storing the tree once sidesteps that entirely and is cheaper besides.
  // pd3route: under kNthu every tree is built (and split to unit edges)
  // up front so NTHU's net order can be computed before round 0 assigns
  // anything. kInherited keeps the identity order and builds lazily below,
  // exactly as before.
  std::vector<size_t> order(nets.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::vector<char> prebuilt(nets.size(), 0);
  if (nthu && viaAware) {
    std::vector<size_t> pins(nets.size());
    std::vector<long long> tileEdges(nets.size(), 0);
    for (size_t i = 0; i < nets.size(); ++i) {
      dropDegenerateSegs(nets[i]);
      NetTree legs;
      std::vector<Point> pinTiles;
      for (const Pin &pn : nets[i].net->pins)
        pinTiles.push_back({pn.x, pn.y});
      if (buildNetTree(nets[i].segs, legs, /*dropCycles=*/true, &pinTiles)) {
        splitToUnitEdges(legs, trees[i]);
        prebuilt[i] = 1;
        if (laPins) {
          std::map<std::pair<int, int>, std::pair<int, int>> at;
          for (const Pin &pn : nets[i].net->pins) {
            auto it = at.find({pn.x, pn.y});
            if (it == at.end())
              at[{pn.x, pn.y}] = {pn.layer, pn.layer};
            else
              it->second = {std::min(it->second.first, pn.layer), std::max(it->second.second, pn.layer)};
          }
          pinRange[i].assign(trees[i].nodes.size(), {-1, -1});
          for (size_t u = 0; u < trees[i].nodes.size(); ++u) {
            auto it = at.find({trees[i].nodes[u].x, trees[i].nodes[u].y});
            if (it != at.end())
              pinRange[i][u] = it->second;
          }
        }
      }
      pins[i] = nets[i].net->pins.size();
      for (const Seg &sg : nets[i].segs)
        if (sg.l1 == sg.l2)
          tileEdges[i] += std::abs(sg.x2 - sg.x1) + std::abs(sg.y2 - sg.y1);
    }
    order = laNetOrder(pins, tileEdges);
  }
  for (size_t k = 0; k < nets.size(); ++k) {
    size_t i = order[k];
    RoutedNet &rn = nets[i];
    dropDegenerateSegs(rn);
    if (!viaAware) {
      for (Seg &s : rn.segs)
        assignSegmentLayer(g, ru, lp, rn.net->minWidth, s);
      stitchViasForNet(rn);
      stitchPinAccessForNet(rn);
      continue;
    }
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    bool built = nthu ? prebuilt[i] != 0 : buildNetTree(rn.segs, trees[i]);
    if (built &&
        layerAssignDP(g, ru, rvu, viaCap, lp, rn.net->minWidth, trees[i], edgeLayer, rootLayer,
                      nodeStackRange, viaWeight, &vp.hist, wireHistH, wireHistV, nthu,
                      pinRange[i].empty() ? nullptr : &pinRange[i])) {
      commitNetTree(g, ru, rvu, rn.net->minWidth, trees[i], edgeLayer, nodeStackRange, rn,
                    &charges[i]);
      stitchPinAccessForNetChargingVias(g, rvu, rn, &charges[i]);
      usesTreeDP[i] = 1;
      continue;
    }
    for (Seg &s : rn.segs)
      assignSegmentLayer(g, ru, lp, rn.net->minWidth, s);
    stitchViasForNet(rn);
    stitchPinAccessForNetChargingVias(g, rvu, rn, &charges[i]);
  }

  if (!viaAware) {
    rep.roundsRun = 1;
    return rep;
  }

  rep.bestOverflow = totalRealOverflow(g, ru, rvu, viaCap, /*includeVia=*/false);
  rep.bestRound = 0;
  rep.roundsRun = 1;
  if (trace)
    std::cerr << "round 0: overflow " << rep.bestOverflow << "\n";
  std::vector<NetCharge> bestCharges = charges;
  std::vector<std::vector<Seg>> bestSegs(nets.size());
  for (size_t i = 0; i < nets.size(); ++i)
    bestSegs[i] = nets[i].segs;

  if (rep.bestOverflow == 0) {
    rep.convergedToZero = true;
    return rep;
  }

  updateViaHistory(g, vp, rvu, viaCap, viaKnobs.step, viaKnobs.mode, viaKnobs.factor,
                    viaKnobs.streakThreshold, viaKnobs.scaleByOverflow, viaKnobs.relaxEnabled,
                    viaKnobs.ratioExponent, viaKnobs.exponentEscalation);
  if (wireAware)
    updateWireHistory(g, wireHist, ru, wireKnobs.step, wireKnobs.mode, wireKnobs.factor,
                      wireKnobs.streakThreshold, wireKnobs.scaleByOverflow, wireKnobs.relaxEnabled,
                      wireKnobs.ratioExponent, wireKnobs.exponentEscalation);

  int sinceImprovement = 0;
  long long finalOverflow = rep.bestOverflow;
  for (int round = 1; round <= maxRounds; ++round) {
    int accepted = 0, rejected = 0;
    for (size_t i = 0; i < nets.size(); ++i) {
      if (!usesTreeDP[i])
        continue;
      // pd3route: under kNthu only real (scored) wire overflow earns a
      // reroute -- via-site capacity is not scored and not in the cost.
      if (nthu ? !netOverflowedWire(g, ru, charges[i]) : !netOverflowedVia(g, ru, rvu, viaCap, charges[i]))
        continue;

      NetCharge oldCharge = charges[i];
      std::vector<Seg> oldSegs = nets[i].segs;
      // "Before" is measured over the old charge's own indices, while it
      // is still live -- kroute's own idiom (overflowOverUnion(g, usage,
      // oldCharges, oldCharges)), computed before anything is undone.
      // includeVia=false: judge this net's outcome by real (wire-only)
      // overflow, the metric gr_verify actually scores -- see
      // localOverflow's own comment for why via sites don't belong here.
      long long localBefore = localOverflow(g, ru, rvu, viaCap, oldCharge, oldCharge, false);

      undoCharge(ru, rvu, oldCharge);
      std::vector<int> edgeLayer;
      int rootLayer = 0;
      std::vector<std::pair<int, int>> nodeStackRange;
      NetCharge newCharge;
      bool ok = layerAssignDP(g, ru, rvu, viaCap, lp, nets[i].net->minWidth, trees[i], edgeLayer,
                              rootLayer, nodeStackRange, viaWeight, &vp.hist, wireHistH, wireHistV,
                              nthu, pinRange[i].empty() ? nullptr : &pinRange[i]);
      if (ok) {
        commitNetTree(g, ru, rvu, nets[i].net->minWidth, trees[i], edgeLayer, nodeStackRange,
                      nets[i], &newCharge);
        stitchPinAccessForNetChargingVias(g, rvu, nets[i], &newCharge);
      } else {
        // Defensive only -- this exact tree already succeeded in round 0
        // with the same viaCap, so feasibility cannot have changed;
        // restore the old charge and leave this net untouched.
        applyCharge(ru, rvu, oldCharge);
        continue;
      }

      long long localAfter = localOverflow(g, ru, rvu, viaCap, oldCharge, newCharge, false);
      if (localAfter > localBefore) {
        undoCharge(ru, rvu, newCharge);
        applyCharge(ru, rvu, oldCharge);
        nets[i].segs = std::move(oldSegs);
        ++rejected;
      } else {
        charges[i] = std::move(newCharge);
        ++accepted;
      }
    }

    finalOverflow = totalRealOverflow(g, ru, rvu, viaCap, /*includeVia=*/false);
    rep.roundsRun = round + 1;
    if (trace)
      std::cerr << "round " << round << ": overflow " << finalOverflow << " (accepted "
                << accepted << ", rejected " << rejected << ")\n";
    if (finalOverflow < rep.bestOverflow) {
      rep.bestOverflow = finalOverflow;
      rep.bestRound = round;
      sinceImprovement = 0;
      for (size_t i = 0; i < nets.size(); ++i)
        if (usesTreeDP[i]) {
          bestCharges[i] = charges[i];
          bestSegs[i] = nets[i].segs;
        }
    } else {
      ++sinceImprovement;
    }
    updateViaHistory(g, vp, rvu, viaCap, viaKnobs.step, viaKnobs.mode, viaKnobs.factor,
                      viaKnobs.streakThreshold, viaKnobs.scaleByOverflow, viaKnobs.relaxEnabled,
                      viaKnobs.ratioExponent, viaKnobs.exponentEscalation);
    if (wireAware)
      updateWireHistory(g, wireHist, ru, wireKnobs.step, wireKnobs.mode, wireKnobs.factor,
                        wireKnobs.streakThreshold, wireKnobs.scaleByOverflow,
                        wireKnobs.relaxEnabled, wireKnobs.ratioExponent,
                        wireKnobs.exponentEscalation);
    if (finalOverflow == 0) {
      rep.convergedToZero = true;
      break;
    }
    if (accepted == 0) {
      rep.stalled = true;
      break;
    }
    if (sinceImprovement >= patience) {
      rep.stalled = true;
      break;
    }
  }

  // Roll back to the best-known round if the loop didn't end there --
  // kroute.cpp's own snapshot-restore (kroute.cpp:1301-1323), adapted to
  // exact incremental undo/redo (simpler than kroute's own rebuild-from-
  // zero, since via/wire usage here are exact integer counts with no drift
  // concern, and only DP-managed nets ever change after round 0, so
  // greedy-fallback nets never need to be touched).
  if (rep.bestRound >= 0 && rep.bestOverflow < finalOverflow) {
    for (size_t i = 0; i < nets.size(); ++i) {
      if (!usesTreeDP[i])
        continue;
      undoCharge(ru, rvu, charges[i]);
      applyCharge(ru, rvu, bestCharges[i]);
      nets[i].segs = bestSegs[i];
      charges[i] = bestCharges[i];
    }
  }

  return rep;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

void writeSolution(const std::string &path, const RoutingGrid &g,
                    const std::vector<RoutedNet> &nets) {
  std::ofstream out(path);
  if (!out)
    fail("cannot write " + path);
  auto realX = [&](int x) { return g.llx + static_cast<long long>(x) * g.tileW; };
  auto realY = [&](int y) { return g.lly + static_cast<long long>(y) * g.tileH; };
  for (const RoutedNet &rn : nets) {
    out << rn.net->name << " " << rn.net->id << "\n";
    for (const Seg &s : rn.segs)
      out << "(" << realX(s.x1) << "," << realY(s.y1) << "," << (s.l1 + 1) << ")-("
          << realX(s.x2) << "," << realY(s.y2) << "," << (s.l2 + 1) << ")\n";
    out << "!\n";
  }
}

// pd3route: PD3ROUTE_LA_FROM=<route file> reruns layer assignment alone on
// an existing solution. Each net's segments are projected back to the 2D
// stage's form -- wire segments onto the one synthetic layer 0, vias
// dropped, real coordinates converted to tiles with the same
// floor((coord - ll) / tile) rule as the .gr reader -- which is exactly what
// the 2D stage handed layer assignment: layer assignment only ever adds a
// layer to each wire and via stacks between them. Nets are matched by id;
// an unknown id, a malformed line, or a diagonal wire fails loudly.
std::vector<RoutedNet> readRouteAs2D(const Design &d, const std::string &path) {
  std::ifstream in(path);
  if (!in)
    fail("cannot read " + path);
  const RoutingGrid &g = d.grid;
  std::map<long long, size_t> byId;
  for (size_t i = 0; i < d.nets.size(); ++i)
    byId[d.nets[i].id] = i;
  auto tileX = [&](long long x) { return static_cast<int>((x - g.llx) / g.tileW); };
  auto tileY = [&](long long y) { return static_cast<int>((y - g.lly) / g.tileH); };
  std::vector<RoutedNet> nets;
  std::string line;
  RoutedNet *cur = nullptr;
  std::set<std::tuple<int, int, int, int>> seen;
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    if (line[0] == '!') {
      cur = nullptr;
      continue;
    }
    if (line[0] == '(') {
      if (!cur)
        fail("route file: segment outside a net: " + line);
      long long x1, y1, l1, x2, y2, l2;
      if (std::sscanf(line.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        fail("route file: malformed segment: " + line);
      if (l1 != l2)
        continue; // a via: layer assignment rebuilds these
      int a = tileX(x1), b = tileY(y1), c = tileX(x2), e = tileY(y2);
      if (a != c && b != e)
        fail("route file: diagonal segment: " + line);
      if (a == c && b == e)
        continue;
      auto key = std::make_tuple(std::min(a, c), std::min(b, e), std::max(a, c), std::max(b, e));
      if (!seen.insert(key).second)
        continue; // the same 2D stretch on two layers -- once in 2D
      cur->segs.push_back({std::get<0>(key), std::get<1>(key), 0, std::get<2>(key), std::get<3>(key), 0});
      continue;
    }
    std::istringstream hs(line);
    std::string name;
    long long id;
    if (!(hs >> name >> id))
      fail("route file: malformed net header: " + line);
    auto it = byId.find(id);
    if (it == byId.end())
      fail("route file: unknown net id " + std::to_string(id));
    nets.emplace_back();
    cur = &nets.back();
    cur->net = &d.nets[it->second];
    seen.clear();
  }
  return nets;
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

namespace selftest {

int failures = 0;
void check(bool cond, const std::string &what) {
  std::cout << (cond ? "  ok   " : "  FAIL ") << what << "\n";
  if (!cond)
    ++failures;
}

Design parse(const std::string &path, const char *text) {
  std::ofstream out(path);
  out << text;
  out.close();
  return readGr(path);
}

bool segsConnected(const std::vector<Seg> &segs) {
  std::vector<long long> nodes;
  std::vector<std::pair<long long, long long>> links;
  auto key = [&](int x, int y, int l) {
    return (static_cast<long long>(l) * 100000 + y) * 100000 + x;
  };
  for (const Seg &s : segs) {
    bool sameLayer = s.l1 == s.l2;
    if (sameLayer && s.x1 == s.x2) {
      for (int y = std::min(s.y1, s.y2); y <= std::max(s.y1, s.y2); ++y)
        nodes.push_back(key(s.x1, y, s.l1));
      for (int y = std::min(s.y1, s.y2); y < std::max(s.y1, s.y2); ++y)
        links.emplace_back(key(s.x1, y, s.l1), key(s.x1, y + 1, s.l1));
    } else if (sameLayer && s.y1 == s.y2) {
      for (int x = std::min(s.x1, s.x2); x <= std::max(s.x1, s.x2); ++x)
        nodes.push_back(key(x, s.y1, s.l1));
      for (int x = std::min(s.x1, s.x2); x < std::max(s.x1, s.x2); ++x)
        links.emplace_back(key(x, s.y1, s.l1), key(x + 1, s.y1, s.l1));
    } else {
      nodes.push_back(key(s.x1, s.y1, s.l1));
      nodes.push_back(key(s.x1, s.y1, s.l2));
      links.emplace_back(key(s.x1, s.y1, s.l1), key(s.x1, s.y1, s.l2));
    }
  }
  std::sort(nodes.begin(), nodes.end());
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  if (nodes.empty())
    return true;
  std::vector<int> parent(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  auto idx = [&](long long k) {
    return static_cast<int>(std::lower_bound(nodes.begin(), nodes.end(), k) - nodes.begin());
  };
  for (auto &lk : links) {
    int a = find(idx(lk.first)), b = find(idx(lk.second));
    if (a != b)
      parent[a] = b;
  }
  int root = find(0);
  for (size_t i = 0; i < nodes.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      return false;
  return true;
}

const char *kBasicGr = R"(grid 4 4 2
vertical capacity 0 2
horizontal capacity 2 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 3
netA 0 2 1
5 5 1
35 5 1
netD 1 3 1
5 5 1
35 5 1
5 35 1
netE 2 2 1
35 35 1
5 5 1
0
)";

void testBasicShapesStayOnFastTier() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design");
  check(rep.escalatedCount == 0,
        "nothing escalates when the fast tier alone already succeeds");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs), "netE (high-to-low bend) is one component");
}

// A net whose only legal path needs two layer changes mid-route, avoiding
// two blockages on two different layers that don't overlap in position:
// layer 1 blocked at columns 3-4, layer 3 blocked at columns 6-7, spanning
// columns 0-9. No single fixed layer clears the whole span (that's the
// point -- unlike mroute's own self-test, where one layer happened to be
// fully open and a fixed-layer-per-run router could have found it by
// accident). The fast tier's routeNetFast can only pick one layer for the
// whole run and will land on ~1e9 forbidden-price cost no matter which it
// picks, leaving this net legally unresolvable by the fast tier alone --
// this is exactly what escalation exists to catch. The slow tier can zigzag
// layer1[0-3] -> hop -> layer3[3-6] -> hop -> layer1[6-9], clearing both
// blockages, at the cost of extra vias through the layer-2 via chain.
// vroute's own testEscalationSolvesWhatFastTierCannot is removed here: its
// fixture (kZigzagGr) specifically required a multi-layer detour -- two
// different real layers each blocked over a different x-range, so no
// single fixed layer choice clears the whole span -- which this file's 2D
// collapse (exactly one synthetic layer, summing every real layer's
// capacity into it) makes structurally inapplicable. There is no longer a
// "which layer" question for the fast tier to get stuck on. See
// HISTORY_COST_SWEEP.md's Part 21 for the fr1route entry documenting this
// and every other test removed for the same reason.

// The decisive claim this file's whole mechanism rests on: given several
// simultaneous sources, multiSourceDijkstra must find a path from whichever
// one is actually cheapest to reach a sink from -- not just the first
// source in the list, and not some fixed "primary" source with the rest
// treated as an afterthought. Coordinates here are tile indices (as the
// router sees them internally, after gr_io.h's own real-coordinate ->
// tile conversion, not the raw .gr file's micron values), matching
// kBasicGr's 4x4-tile grid: a "far" source at tile (0,0) standing in for
// where a pin might sit, and a "near" one at tile (2,0) -- standing in for
// a point somewhere in the middle of already-committed wire, which is
// exactly what netFootprint() feeds this function with during real net
// growth. A sink at tile (3,0) is 3 tiles from the far source but only 1
// from the near one.
void testMultiSourceDijkstraUsesCheapestSourceNotFirstInList() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;

  std::vector<PathNode> sources = {{0, 0, 0}, {2, 0, 0}}; // far, then near
  std::vector<Point> sinks = {{3, 0}};
  MSMVResult r = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(r.found, "a path exists between the sources and the sink (sanity)");
  check(r.path.front().x == 2 && r.path.front().y == 0,
        "the search used the near source (tile 2,0), not the far one "
        "(tile 0,0) listed first -- proves genuine multi-source selection "
        "by real cost, not list order");
  check(r.path.size() == 2,
        "the path length (2 grid points, tiles 2..3) matches the near "
        "source's 1-tile distance, not the far source's 3-tile one");
}

// The mirror claim on the sink side: given several simultaneous sinks, the
// search must stop at whichever is actually cheapest to reach -- exactly
// "which remaining pin is cheapest to connect next," the real question
// MSMV net growth asks every step. A single source at tile (0,0); a "far"
// sink at tile (3,0) listed first and a "near" sink at tile (1,0) listed
// second.
void testMultiSourceDijkstraStopsAtCheapestSink() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;

  std::vector<PathNode> sources = {{0, 0, 0}};
  std::vector<Point> sinks = {{3, 0}, {1, 0}}; // far, then near
  MSMVResult r = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(r.found, "a path exists between the source and a sink (sanity)");
  check(r.sinkIndex == 1,
        "the search stopped at the near sink (index 1, tile 1,0), not the "
        "far one listed first -- proves genuine multi-sink selection by "
        "real cost, not list order");
  check(r.path.size() == 2,
        "the path length (2 grid points, tiles 0..1) matches the near "
        "sink's 1-tile distance, not the far sink's 3-tile one");
}

// Ported from mroute.cpp's testPriceClampPreventsInfinity: a single-layer,
// single-track (capacity 1) corridor with a very wide net (minWidth 1000)
// blows usage/capacity to ~1000 the moment the net's own first commit
// charges it, sending negotiateWire's price toward literal infinity within
// a handful of rounds. With only one layer and one possible path, the fast
// tier can never legalize this net either, so it escalates to the slow
// tier -- exercising the exact code path (a real Dijkstra search relaxing a
// poisoned edge) where mroute's own version of this bug turned into a
// process-ending fail(). Proves the clamp ported into this file's
// negotiateWire actually prevents the same failure here, not just in
// mroute.cpp.
const char *kPriceOverflowGr = R"(grid 5 1 1
vertical capacity 0
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netWide 0 2 1000
5 5 1
45 5 1
0
)";

void testPriceClampPreventsInfinity() {
  Design d = parse("vroute_selftest_priceoverflow.gr", kPriceOverflowGr);
  RunReport rep = route(d, 5, 5, 0.5, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1,
        "netWide still finds a route across multiple rounds on both tiers "
        "even though its own width alone drives its one shared edge far "
        "enough past capacity to overflow price to infinity without the "
        "clamp");
  check(segsConnected(rep.nets[0].segs), "netWide is one connected component");
}

// ---------------------------------------------------------------------------
// History-cost mechanism tests: the decisive behavioral change this file
// adds over hroute, plus the arithmetic edge cases CLAUDE.md's testing
// section requires for any formula that can legitimately see a zero.
// ---------------------------------------------------------------------------

// Two parallel horizontal corridors (layer 0 and layer 1) between the same
// two endpoints. This reproduces, directly against bestHLayer -- the real
// production cost function every fast-tier route decision goes through --
// exactly the plateau dynamic ZERO_OVERFLOW_CONVERGENCE_EXPERIMENT.md
// diagnosed: layer 0 was over capacity for a while, a net detoured off it,
// and its usage now reads comfortably clear. Layer 1 was never over
// capacity but has quietly kept the current-round pricing (lambda) climbing
// this whole time for unrelated reasons (other nets still contending for
// it). Under hroute's pricing alone (no history, or equivalently
// VROUTE_HIST_STEP=0) layer 0's frozen-but-lower lambda wins every time,
// even though nothing about the grid says returning to it is actually
// safe -- the exact "drift back" failure this file exists to fix. With
// permanent history accumulated against layer 0 from its earlier overflow,
// the same comparison flips.
// vroute's own testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge
// is removed here: its whole premise was comparing bestHLayer's choice
// between two different *real* layers (one recently congested and now
// clear, one steadily moderate) -- a comparison this file's 2D collapse
// makes inapplicable, since there is only ever one synthetic layer to
// choose (LayerPlan::hLayers/vLayers are both {0}). The history-relax
// mechanism itself remains fully covered by the direct-arithmetic tests
// below (testRelaxationUndoesExactlyOneStepPerCleanRound and its
// siblings), which exercise updateHistory's own real production code, not
// a layer-choice comparison built on top of it.

// CLAUDE.md's arithmetic-edge-case section requires a test for every
// formula that can see a degenerate input, not just the happy path.
// updateHistory has no division (a plain usage > capacity comparison), so
// there is no capacity_e == 0 divide-by-zero to reproduce here the way
// priceExponentTerm has one -- but a zero/negative capacity edge is still a
// degenerate case worth a named test, because the *wrong* answer (silently
// accumulating history against an edge that was never usable) would still
// compound every round for the rest of the run without ever crashing,
// exactly the "wrong code still runs and still produces a route, just a
// worse one" failure mode CLAUDE.md warns about. Adapted for this file's
// own 2D collapse: kBasicGr's layer 1 alone being blocked (hcap 0) is not
// enough here, since the *summed* 2D capacity is layer 0's nonzero value
// plus layer 1's zero -- a genuinely nonzero 2D edge. The fixture needs
// every real layer blocked so the 2D-collapsed capacity itself is zero.
const char *kZeroCap2dGr = R"(grid 4 4 2
vertical capacity 0 0
horizontal capacity 0 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
15 5 1
0
)";

void testHistoryIgnoresZeroCapacityEdges() {
  Design d = parse("fr1route_selftest_zerocap2d.gr", kZeroCap2dGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  size_t idx = g.hIndex(0, 0, 0);
  check(u.capH[idx] <= 0,
        "fixture edge's 2D-collapsed capacity really is zero -- every real "
        "layer is blocked, not just one, so the sum genuinely vanishes");
  u.h[idx] = 999; // absurdly "over capacity" if compared naively
  updateHistory(g, pr, u, 1.0, HistMode::kAdditive, 1.0);
  check(pr.histH[idx] == 1.0,
        "a zero-2D-capacity edge's history term is never grown, matching "
        "negotiateWire's own treatment of the same edges -- nothing "
        "meaningful to accumulate against an edge that was never usable");
}

// The mechanism's whole point is that it only ever moves one direction.
// VROUTE_HIST_RELAX=0 must reproduce iroute's exact permanent behavior --
// this is the regression guard for that claim, not the relaxation
// mechanism itself (see testRelaxationUndoesExactlyOneStepPerCleanRound and
// testRelaxationFullyRestoresBaselineAfterEqualCleanRounds below for that).
void testDisablingRelaxReproducesPermanentBehavior() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5; // over capacity
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, 1, false, /*relaxEnabled=*/false);
  double afterOver = pr.histH[idx];
  check(afterOver > 1.0, "history grew while the edge was over capacity");

  Usage under(g);
  under.h[idx] = 0; // now comfortably clear
  updateHistory(g, pr, under, 1.0, HistMode::kAdditive, 1.0, 1, false, /*relaxEnabled=*/false);
  check(pr.histH[idx] == afterOver,
        "with VROUTE_HIST_RELAX=0, history never decreases even after usage "
        "drops back under capacity -- reproduces iroute's exact permanent "
        "behavior, the fallback this file's relaxation mechanism can always "
        "be switched off back to");
}

// The actual new mechanism: an edge dirty for exactly one round, then one
// clean round, should have its single growth operation popped and undone
// exactly -- back to the 1.0 no-op baseline, not merely "decreased."
void testRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5; // over capacity
  updateHistory(g, pr, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows history to exactly 1.05");

  Usage under(g);
  under.h[idx] = 0; // clear
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- history "
        "is back to the 1.0 no-op baseline, not just 'lower than before', "
        "the decisive behavioral change this file's relaxation mechanism "
        "is supposed to buy");
}

// The full claim from this file's header: an edge dirty for N rounds needs
// exactly N consecutive clean rounds to fully return to baseline -- not
// more, not less, and a partial run of clean rounds should leave a
// partial, LIFO-consistent penalty behind (proving the stack, not just a
// running total, is actually doing the work).
void testRelaxationFullyRestoresBaselineAfterEqualCleanRounds() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5;
  Usage under(g);
  under.h[idx] = 0;

  // Three dirty rounds: 1.0 -> 1.05 -> 1.10 -> 1.15.
  for (int i = 0; i < 3; ++i)
    updateHistory(g, pr, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.15) < 1e-9,
        "three dirty rounds at step=0.05 grow history to exactly 1.15");

  // Two clean rounds should only partially unwind it -- LIFO pops two of
  // the three pushed 0.05 increments, leaving exactly one behind (1.05),
  // not an averaged or proportional decay to some other value.
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.05) < 1e-9,
        "two clean rounds after three dirty ones leaves exactly one "
        "increment (1.05), not fully relaxed yet -- proves this is a LIFO "
        "stack of individual operations, not a running total that would "
        "have no way to represent a partial unwind");

  // The third consecutive clean round finishes the job exactly.
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.0) < 1e-9,
        "the third consecutive clean round -- matching the three dirty "
        "rounds that earned the penalty -- fully restores the 1.0 no-op "
        "baseline exactly, per this file's header claim");
}

// VROUTE_HIST_STREAK: growth must be gated behind N *consecutive*
// over-capacity round boundaries, and a single clear round must reset the
// count to zero rather than merely pausing it -- otherwise an edge that
// flickers over/under/over capacity every other round would still
// eventually accumulate growth from unrelated non-consecutive rounds,
// which defeats the entire point of requiring a *streak*.
void testHistoryStreakRequiresConsecutiveOvercapacityRounds() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5;
  Usage under(g);
  under.h[idx] = 0;

  const int streakThreshold = 3;
  // Two consecutive over-capacity rounds -- short of the threshold.
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] == 1.0,
        "two consecutive over-capacity rounds is still short of a streak "
        "threshold of 3: no growth yet");

  // A single clear round in between breaks the streak entirely.
  updateHistory(g, pr, under, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] == 1.0,
        "2 over + 1 clear + 2 over never reaches 3 *consecutive* "
        "over-capacity rounds -- the interruption resets the streak, so "
        "history still hasn't grown even though 4 of 5 rounds were over "
        "capacity");

  // The third consecutive over-capacity round finally unlocks growth.
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] > 1.0,
        "the 3rd consecutive over-capacity round reaches the streak "
        "threshold and history finally grows");
}

// VROUTE_HIST_SCALE=overflow: an edge sitting further over capacity should
// accumulate history faster than one just barely over, unlike the default
// flat step which treats "1 unit over" and "10x over" identically.
void testHistoryScalesWithOverflowMagnitude() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices prFlat(g), prScaled(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage barelyOver(g);
  barelyOver.h[idx] = 3; // capacity 2 -> ratio 1.5x
  Usage wayOver(g);
  wayOver.h[idx] = 20; // capacity 2 -> ratio 10x

  updateHistory(g, prFlat, barelyOver, 1.0, HistMode::kAdditive, 1.0, 1, false);
  double flatBarely = prFlat.histH[idx];
  Prices prFlat2(g);
  updateHistory(g, prFlat2, wayOver, 1.0, HistMode::kAdditive, 1.0, 1, false);
  double flatWayOver = prFlat2.histH[idx];
  check(flatBarely == flatWayOver,
        "flat scaling (the default) grows a barely-over edge and a "
        "way-over edge by the identical step -- it does not distinguish "
        "overflow magnitude at all");

  updateHistory(g, prScaled, barelyOver, 1.0, HistMode::kAdditive, 1.0, 1, true);
  double scaledBarely = prScaled.histH[idx];
  Prices prScaled2(g);
  updateHistory(g, prScaled2, wayOver, 1.0, HistMode::kAdditive, 1.0, 1, true);
  double scaledWayOver = prScaled2.histH[idx];
  check(scaledWayOver > scaledBarely,
        "VROUTE_HIST_SCALE=overflow makes the 10x-over edge accumulate "
        "history faster than the 1.5x-over edge in the very same round -- "
        "the decisive behavioral change this mode is supposed to buy");
  check(scaledWayOver > flatWayOver,
        "the way-over edge grows faster under overflow-scaling than it "
        "would have under the flat default");
}

// VROUTE_HIST_MODE=ratio: histH *= usage/capacity directly, no separate
// step or factor. A user-proposed formula, n2 = n1*(1 + overflow_percent/100),
// which is algebraically exactly usage/capacity when "overflow_percent" is
// the excess over capacity as a percentage (e.g. usage=150,cap=100 -> 50%
// -> 1 + 0.5 = 1.5 = 150/100).
void testHistoryRatioModeMultipliesByUsageOverCapacityExactly() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices pr(g);
  Usage fiftyOver(g);
  fiftyOver.h[idx] = 3; // capacity 2 -> ratio exactly 1.5
  updateHistory(g, pr, fiftyOver, /*histStep=*/0.0, HistMode::kRatio, /*histFactor=*/0.0);
  check(std::abs(pr.histH[idx] - 1.5) < 1e-9,
        "VROUTE_HIST_MODE=ratio on a 50%-over edge (usage 3, capacity 2) "
        "multiplies history by exactly 1.5 -- histStep/histFactor play no "
        "role at all in this mode, only the live usage/capacity ratio");

  Usage tenXOver(g);
  tenXOver.h[idx] = 20; // capacity 2 -> ratio exactly 10.0
  updateHistory(g, pr, tenXOver, 0.0, HistMode::kRatio, 0.0);
  check(std::abs(pr.histH[idx] - 15.0) < 1e-9,
        "a second round at 10x capacity multiplies the already-1.5 history "
        "by exactly 10 more (1.5*10=15) -- confirms this mode compounds "
        "multiplicatively round over round against the real production "
        "history term, not just a single isolated call");
}

// VROUTE_HIST_RATIO_EXPONENT: squaring the ratio should punish a badly-over
// edge much harder while barely changing a barely-over one -- the decisive
// property the user proposed this for.
void testHistoryRatioExponentSquaresTheRatio() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices prBarely(g);
  Usage barelyOver(g);
  barelyOver.h[idx] = 3; // capacity 2 -> ratio 1.5
  updateHistory(g, prBarely, barelyOver, 0.0, HistMode::kRatio, 0.0, 1, false, true,
                /*ratioExponent=*/2.0);
  check(std::abs(prBarely.histH[idx] - 2.25) < 1e-9,
        "exponent 2 on a 1.5x-over edge multiplies history by exactly "
        "1.5^2=2.25, not the plain 1.5 exponent-1 would give");

  Prices prWayOver(g);
  Usage tenXOver(g);
  tenXOver.h[idx] = 20; // capacity 2 -> ratio 10
  updateHistory(g, prWayOver, tenXOver, 0.0, HistMode::kRatio, 0.0, 1, false, true,
                /*ratioExponent=*/2.0);
  check(std::abs(prWayOver.histH[idx] - 100.0) < 1e-9,
        "exponent 2 on a 10x-over edge multiplies history by exactly "
        "10^2=100 -- squaring punishes the badly-over edge far more "
        "aggressively relative to the barely-over one (100/2.25=44.4x "
        "apart) than plain ratio mode would (10/1.5=6.7x apart), the "
        "decisive behavioral difference this parameter is supposed to buy");
}

// VROUTE_HIST_EXPONENT_ESCALATION: the effective exponent for a round's
// growth should climb with how many still-unrelaxed penalties are already
// on the stack, and a relaxed edge should use a *lower* effective exponent
// for its next penalty than an edge that stayed dirty continuously -- not
// merely end up at a lower history value, the escalation level itself must
// have dropped. Uses a fixed usage/capacity=2.0 throughout (capacity 2,
// usage 4) so every factor is an exact power of 2, checkable exactly.
void testExponentEscalationTracksStackSizeAndRelaxesWithIt() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 4; // ratio exactly 2.0
  Usage under(g);
  under.h[idx] = 0;
  constexpr double base = 1.0, escalation = 1.0;

  // Edge A: three straight dirty rounds, no relaxation in between.
  // exponents used: 1.0 (stack 0), 2.0 (stack 1), 3.0 (stack 2) ->
  // history: 2 -> 2*4=8 -> 8*8=64.
  Prices prA(g);
  for (int i = 0; i < 3; ++i)
    updateHistory(g, prA, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prA.histH[idx] - 64.0) < 1e-6,
        "three straight dirty rounds with escalation=1.0 produce exactly "
        "2*4*8=64 (exponents 1,2,3 in turn) -- confirms escalation climbs "
        "with consecutive, unrelaxed penalties");

  // Edge B: same three dirty rounds, then two clean rounds (relaxes the
  // stack from 3 down to 1), then a fourth dirty round.
  Prices prB(g);
  for (int i = 0; i < 3; ++i)
    updateHistory(g, prB, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  updateHistory(g, prB, under, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  updateHistory(g, prB, under, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prB.histH[idx] - 2.0) < 1e-6,
        "two clean rounds after the same three dirty ones relax the stack "
        "from size 3 down to 1 (64 -> /8 -> 8 -> /4 -> 2), exactly "
        "mirroring testRelaxationFullyRestoresBaselineAfterEqualCleanRounds' "
        "own LIFO-undo arithmetic");
  updateHistory(g, prB, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prB.histH[idx] - 8.0) < 1e-6,
        "edge B's fourth dirty round, with only 1 unrelaxed penalty on its "
        "stack, uses effective exponent 1.0+1.0*1=2.0 (2^2=4, 2*4=8)");

  Usage overStill(g); // fourth straight dirty round for edge A, no relax
  overStill.h[idx] = 4;
  updateHistory(g, prA, overStill, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prA.histH[idx] - 1024.0) < 1e-3,
        "edge A's fourth dirty round, having never relaxed (stack still "
        "at 3), uses effective exponent 1.0+1.0*3=4.0 (2^4=16, 64*16=1024)");
  check(prA.histH[idx] > prB.histH[idx] * 100,
        "the decisive comparison: two edges on their 4th dirty round each, "
        "but the one that got two clean rounds in between (edge B, "
        "history=8) ends up over 100x lower than the one that never "
        "relaxed (edge A, history=1024) -- relaxation lowers the "
        "escalation level itself, not just the raw history value, exactly "
        "the property VROUTE_HIST_EXPONENT_ESCALATION's header claims by "
        "reusing the stack's own size as the escalation count");
}

// ---------------------------------------------------------------------------
// 2D-collapse mechanism tests: this file's own new addition over vroute.
// ---------------------------------------------------------------------------

// A capacity *adjustment* (not just the nominal per-layer default) must
// carry through the 2D sum -- computeCapH2d/computeCapV2d read the
// dense, post-adjustment g.hcap/g.vcap arrays, not g.hCapLayer/vCapLayer,
// so a real .gr reduced-capacity record on one layer must change exactly
// that layer's contribution to the 2D total, not the whole thing.
const char *kCapAdjust2dGr = R"(grid 4 1 2
vertical capacity 0 0
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
35 5 1
1
1 0 1 2 0 1 3
)";

void testGrid2dCapacityIsExactPostAdjustmentSumAcrossLayers() {
  Design d = parse("fr1route_selftest_capadjust.gr", kCapAdjust2dGr);
  const RoutingGrid &g = d.grid;
  Usage u(g);
  // Untouched edge: both layers at their nominal 10 -> 20.
  check(u.capH[g.hIndex(0, 0, 0)] == 20,
        "an untouched edge's 2D capacity is the plain sum of both layers' "
        "nominal capacity (10+10=20)");
  // Adjusted edge: layer 0 was set to 3 by the .gr adjustment record,
  // layer 1 stays at its nominal 10 -> 13, not 20.
  check(u.capH[g.hIndex(1, 0, 0)] == 13,
        "the adjusted edge's 2D capacity reflects the real post-adjustment "
        "value (3) on layer 0 plus layer 1's untouched nominal 10 -- 13, "
        "not the nominal-only 20 -- proving the sum reads g.hcap (dense, "
        "post-adjustment), not g.hCapLayer (nominal per-layer defaults)");
}

// The decisive claim this whole file exists to test: a net whose required
// width exceeds *every individual layer's* capacity, but not their sum,
// is unroutable without overflow under any single-layer choice a 3D
// router could make (10 > 4 on layer 0, 1, or 2 -- picking the least-bad
// layer still overflows by exactly 10-4=6, unavoidably, since this net's
// one edge is its only edge), yet is perfectly routable at 0 overflow
// once every layer's capacity is pooled into one 2D edge (4+4+4=12 >= 10).
// This is the real HISTORY_COST_SWEEP.md Part 9 mechanism, reproduced on
// a fixture small enough to verify by inspection rather than by running
// a second router: no single-layer choice can ever clear this net, by
// construction, and this file's own real production route() clears it
// exactly.
const char *kPoolsAcrossLayersGr = R"(grid 2 1 3
vertical capacity 0 0 0
horizontal capacity 4 4 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netWide 0 2 10
5 5 1
15 5 1
0
)";

void testTwoDRoutingClearsAnOverflowNoSingleLayerChoiceCanClear() {
  Design d = parse("fr1route_selftest_pools.gr", kPoolsAcrossLayersGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netWide produced a route");
  check(rep.convergedToZero && rep.bestOverflow == 0,
        "0 overflow -- on any single real layer this net's own width (10) "
        "alone exceeds that layer's whole capacity (4), so no per-layer "
        "choice could ever avoid a 6-unit overflow on this edge; pooling "
        "every layer's capacity into one 2D edge (4+4+4=12) is what makes "
        "0 achievable at all, the direct test of this file's whole reason "
        "to exist");
  check(segsConnected(rep.nets[0].segs), "netWide is one connected component");
}

// VROUTE_MSMV=0's inherited fixed-topology slow tier (routeNetSlow /
// boundedDijkstra, not routeNetSlowMSMV / multiSourceDijkstra) must still
// work correctly after the 2D collapse -- exercises the *other* search
// path this file also collapsed, not just the MSMV one every other test
// here happens to exercise via route()'s own default.
void testFixedTopologySlowTierStillWorksAfterTheCollapse() {
  Design d = parse("fr1route_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/false);
  check(rep.nets.size() == 3, "all three basic nets produced a route with VROUTE_MSMV=0");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design via the fixed-topology path either");
  for (const RoutedNet &rn : rep.nets)
    check(segsConnected(rn.segs), "each net routed via the fixed-topology 2D path is one component");
}

// FR1ROUTE_TWOD_ONLY is accepted as a reserved knob in this generation but
// has no effect yet -- this file has no layer-assignment stage to stop
// before (that is fr2route's own job). Confirms that claim directly: the
// same fixture, routed with and without the env var set, produces
// identical results, so the variable is a genuinely safe no-op placeholder
// right now, not a silently-wrong one.
void testTwodOnlyFlagIsInertInThisGeneration() {
  Design d1 = parse("fr1route_selftest_twod_a.gr", kBasicGr);
  RunReport without = route(d1, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);

  setenv("FR1ROUTE_TWOD_ONLY", "1", 1);
  Design d2 = parse("fr1route_selftest_twod_b.gr", kBasicGr);
  RunReport with = route(d2, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  unsetenv("FR1ROUTE_TWOD_ONLY");

  check(with.bestOverflow == without.bestOverflow,
        "FR1ROUTE_TWOD_ONLY=1 changes nothing yet -- reserved for fr2route, "
        "where it will skip that generation's new layer-assignment stage; "
        "this generation has no such stage to skip, so route() itself "
        "never reads this variable at all");
}

// ---------------------------------------------------------------------------
// Steinerization mechanism tests: this file's own new addition over
// fr1route. The same hand-derived 3-point shape anchors most of these:
// a=(0,0), b=(10,0), i=(5,10) [the hub -- degree 2 in the plain MST,
// index 0 in the point list passed to steinerizeTree]. The plain MST
// connects a-b (span 10) and a-i (span 15), total 25. The exact median
// Steiner point is S=(5,0): connecting a-S (5), b-S (5), i-S (10) totals
// 20 -- strictly less, and hand-checkable exactly.
// ---------------------------------------------------------------------------

void testMedianSteinerPointIsExact() {
  check(median3(1, 5, 3) == 3, "median of {1,5,3} is 3");
  check(median3(7, 2, 2) == 2, "median of {7,2,2} (a repeated value) is 2");
  check(median3(9, 9, 9) == 9, "median of three equal values is that value");

  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}}; // a, b, i
  std::vector<std::pair<int, int>> mst = {{0, 1}, {0, 2}}; // plain MST: a-b, a-i
  SteinerResult sr = steinerizeTree(pts, mst);
  check(sr.pts.size() == 4, "one new Steiner point was appended (sanity)");
  check(sr.pts[3].x == 5 && sr.pts[3].y == 0,
        "the real production steinerizeTree computes exactly S=(5,0) -- "
        "median(0,10,5)=5 on x, median(0,0,10)=0 on y -- matching the "
        "hand-derived Steiner point exactly, not just 'some' new point");
}

// The decisive test: steinerizeTree's real output has strictly less total
// Manhattan span (20) than the plain MST it replaced (25) on the hand-
// derived hub shape above.
void testSteinerizationStrictlyReducesTotalSpan() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}};
  std::vector<std::pair<int, int>> mst = {{0, 1}, {0, 2}};

  int plainSpan = 0;
  for (auto &e : mst)
    plainSpan += manhattan(pts[e.first], pts[e.second]);
  check(plainSpan == 25, "the plain MST's own total span is 25 (sanity)");

  SteinerResult sr = steinerizeTree(pts, mst);
  int steinerSpan = 0;
  for (auto &e : sr.edges)
    steinerSpan += manhattan(sr.pts[e.first], sr.pts[e.second]);
  check(steinerSpan == 20,
        "the real production steinerizeTree's own output totals exactly "
        "20 -- strictly less than the plain MST's 25 -- the direct, "
        "hand-verified proof this mechanism does what it claims");
}

// No-op when it wouldn't help: three collinear points, where the middle
// point is already its own coordinate median -- steinerizeTree must
// return the tree completely unchanged (no new point, identical edges),
// proving the "strictly better only" gate is real, not always-on.
void testSteinerizationIsANoOpWhenTheHubIsAlreadyOptimal() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 0}}; // a, b, i -- i is the midpoint
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  check(mst.size() == 2, "three points form a 2-edge tree (sanity)");

  SteinerResult sr = steinerizeTree(pts, mst);
  check(sr.pts.size() == pts.size(),
        "no new point was appended -- the collinear midpoint is already "
        "its own median, so there is nothing to gain");
  check(sr.edges.size() == mst.size(),
        "the edge list is unchanged in size");
  std::vector<std::pair<int, int>> sortedOrig = mst, sortedNew = sr.edges;
  auto normalize = [](std::pair<int, int> &e) {
    if (e.first > e.second)
      std::swap(e.first, e.second);
  };
  for (auto &e : sortedOrig)
    normalize(e);
  for (auto &e : sortedNew)
    normalize(e);
  std::sort(sortedOrig.begin(), sortedOrig.end());
  std::sort(sortedNew.begin(), sortedNew.end());
  check(sortedOrig == sortedNew,
        "the exact same edges (endpoint pairs), not just the same count, "
        "come back out -- a genuinely inert no-op, not a coincidentally "
        "equal-cost rewrite");
}

// Tree validity: steinerizeTree's output must remain a valid tree (exactly
// pts.size()-1 edges, one connected component, no cycles) on a larger,
// multi-branch point set -- checked by direct union-find over the output
// edge list, independent of steinerizeTree's own internal bookkeeping.
void testSteinerizedOutputIsAlwaysAValidTree() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}, {5, -10}, {20, 0}, {5, 20}};
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  SteinerResult sr = steinerizeTree(pts, mst);

  check(sr.edges.size() == sr.pts.size() - 1,
        "edge count is exactly one less than point count -- the tree "
        "invariant holds however many nodes were Steinerized");

  std::vector<int> parent(sr.pts.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : sr.edges) {
    int ra = find(e.first), rb = find(e.second);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle was introduced by any Steinerization");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < sr.pts.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected, "every point, original and Steiner both, is in one connected component");
}

// FR3ROUTE_STEINERIZE=0 must reproduce fr1route's exact plain-MST
// topology -- checked directly and structurally: with it disabled, a net
// built from the hub fixture above must come back with exactly its
// original 3 points, no 4th Steiner point ever appended.
const char *kHubGr = R"(grid 12 12 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netHub 0 3 1
5 5 1
105 5 1
55 105 1
0
)";

void testDisablingSteinerizeReproducesPlainMstTopology() {
  Design d = parse("fr3route_selftest_notier.gr", kHubGr);
  // This fixture has only 3 pins, squarely inside this file's own exact-
  // Steiner degree range -- both switches must be off to actually reach
  // fr1route's plain MST, not just FR3ROUTE_STEINERIZE.
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                         /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(rep.nets.size() == 1, "netHub produced a route with FR3ROUTE_STEINERIZE=0");
  check(rep.nets[0].pts.size() == 3,
        "with Steinerization disabled, the net comes back with exactly "
        "its original 3 pins -- no Steiner point was ever appended, "
        "confirming the toggle genuinely disables the mechanism rather "
        "than just leaving it unused");
}

// The end-to-end decisive test: routed through the real production
// pipeline (route(), not steinerizeTree in isolation), the hub net's
// total charged wirelength (summed segment span) is exactly 20 with
// Steinerization enabled versus exactly 25 with it disabled -- the same
// hand-derived numbers as testSteinerizationStrictlyReducesTotalSpan,
// now reproduced through real candidate generation and commit. Every
// segment here is a pure single-axis run (no jog needed either way,
// since each Steinerized edge is purely horizontal or vertical and the
// plain MST's diagonal edge's total span is jog-invariant, the same
// Manhattan-distance property this project's own history already relies
// on elsewhere), so the real fast tier's specific candidate choice cannot
// change the total either way.
void testEndToEndSteinerizationReducesRealChargedWirelength() {
  auto totalSpan = [](const RunReport &rep) {
    long long total = 0;
    for (const RoutedNet &rn : rep.nets)
      for (const Seg &s : rn.segs)
        total += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
    return total;
  };

  Design dWith = parse("fr3route_selftest_e2e_with.gr", kHubGr);
  RunReport withSteiner = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(withSteiner.bestOverflow == 0, "the hub net is uncongested (sanity)");
  check(totalSpan(withSteiner) == 20,
        "with Steinerization enabled (default), the real production "
        "route() charges exactly 20 total tile-edges for this net -- this "
        "file's own exact Dreyfus-Wagner solve reaches the same S=(5,0) "
        "topology fr3route's cheap median pass found (the coordinate-wise "
        "median is the true 3-terminal optimum, so exact and cheap agree "
        "exactly on a 3-pin net), not the plain MST");

  Design dWithout = parse("fr3route_selftest_e2e_without.gr", kHubGr);
  // Both switches off: this fixture's 3 pins are squarely inside the
  // exact solve's degree range, so useExactSteiner must also be disabled
  // to actually reach fr1route's plain MST.
  RunReport withoutSteiner = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                                    /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                                    /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                                    /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                                    /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(totalSpan(withoutSteiner) == 25,
        "with Steinerization disabled, the same net charges exactly 25 -- "
        "the plain MST's own total span, confirmed through the real "
        "production pipeline end to end, not just steinerizeTree alone");
}

// ---------------------------------------------------------------------------
// Exact Steiner-tree (Dreyfus-Wagner) mechanism tests: this file's own new
// addition over fr3route. The decisive fixture is a 4-point "hub" shape
// where the plain MST's own branch point has degree 3 -- fr3route's cheap
// pass (degree-2-only) is a structural no-op here, so any improvement
// measured can only have come from the real Hanan-grid DP.
// ---------------------------------------------------------------------------

void testExactSteinerBeatsBothPlainMstAndCheapPassOnADegreeThreeHub() {
  // A=(0,3), B=(4,3), C=(2,6), D=(2,0) -- shifted to non-negative
  // coordinates from the hand-derived diamond (0,0),(4,0),(2,3),(2,-3).
  std::vector<Point> pts = {{0, 3}, {4, 3}, {2, 6}, {2, 0}};

  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  int plainSpan = 0;
  for (auto &e : mst)
    plainSpan += manhattan(pts[e.first], pts[e.second]);
  check(plainSpan == 14, "the plain MST's own total span is 14 (sanity)");

  // fr3's cheap pass is a structural no-op here: node 0 (A) ends up
  // degree 3 in the plain MST (Prim's from A connects A-B, A-C, A-D), so
  // no degree-2 node exists for the median trick to fire on.
  SteinerResult cheap = steinerizeTree(pts, mst);
  check(cheap.pts.size() == pts.size(),
        "fr3route's cheap degree-2-only pass appends no Steiner point on "
        "this fixture -- the branch point is degree 3, outside its scope, "
        "confirming this really does test what the exact DP alone can do");
  int cheapSpan = 0;
  for (auto &e : cheap.edges)
    cheapSpan += manhattan(cheap.pts[e.first], cheap.pts[e.second]);
  check(cheapSpan == 14, "the cheap pass's own total span is unchanged at 14");

  SteinerResult exact = exactSteinerTree(pts);
  int exactSpan = 0;
  for (auto &e : exact.edges)
    exactSpan += manhattan(exact.pts[e.first], exact.pts[e.second]);
  check(exactSpan == 10,
        "the exact Dreyfus-Wagner DP finds the true optimum, 10 -- a "
        "28.6% reduction from 14 that fr3route's cheap pass structurally "
        "cannot reach on this fixture (no degree-2 node to trigger it)");

  bool foundHub = false;
  for (const Point &p : exact.pts)
    if (p.x == 2 && p.y == 3)
      foundHub = true;
  check(foundHub,
        "the reconstructed tree passes through the exact hand-derived "
        "Steiner point (2,3) -- dist to A=2, B=2, C=3, D=3, totaling 10");

  check(exact.edges.size() == exact.pts.size() - 1,
        "the exact solve's own output is a valid tree (edges = points-1)");
}

void testExactSteinerMatchesHandVerifiedOptimaOnSmallFixtures() {
  // Case 1: three collinear points -- the plain MST is already optimal
  // (no Steiner point can beat a straight line), matching the same
  // fixture steinerizeTree's own no-op test uses.
  std::vector<Point> collinear = {{0, 0}, {10, 0}, {5, 0}};
  SteinerResult r1 = exactSteinerTree(collinear);
  int span1 = 0;
  for (auto &e : r1.edges)
    span1 += manhattan(r1.pts[e.first], r1.pts[e.second]);
  check(span1 == 10,
        "collinear points: exact DP matches the already-optimal "
        "plain-line cost of 10, no improvement possible");

  // Case 2: a hand-computed 3-point case where the Steiner point strictly
  // beats the plain MST -- P0=(0,0), P1=(2,0), P2=(1,5). Plain MST (Prim
  // from P0): P0-P1 (2) + P0-P2 (6) = 8. The coordinate-wise median
  // S=(1,0) [median(0,2,1)=1, median(0,0,5)=0] gives P0-S(1)+P1-S(1)+
  // P2-S(5) = 7 -- provably optimal for exactly 3 terminals (the same
  // classical result median3's own header cites).
  std::vector<Point> tri = {{0, 0}, {2, 0}, {1, 5}};
  std::vector<std::pair<int, int>> triMst = mstEdges(tri);
  int triMstSpan = 0;
  for (auto &e : triMst)
    triMstSpan += manhattan(tri[e.first], tri[e.second]);
  check(triMstSpan == 8, "this fixture's plain MST totals 8 (sanity)");

  SteinerResult r2 = exactSteinerTree(tri);
  int span2 = 0;
  for (auto &e : r2.edges)
    span2 += manhattan(r2.pts[e.first], r2.pts[e.second]);
  check(span2 == 7,
        "exact DP finds the true 3-terminal optimum, 7 -- strictly less "
        "than the plain MST's 8, matching the hand-derived median Steiner "
        "point exactly");
}

void testExactSteinerOutputIsAlwaysAValidTree() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}, {5, 20}, {20, 0}, {15, 15}};
  SteinerResult sr = exactSteinerTree(pts);

  check(sr.edges.size() == sr.pts.size() - 1,
        "edge count is exactly one less than point count");

  std::vector<int> parent(sr.pts.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : sr.edges) {
    int ra = find(e.first), rb = find(e.second);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle in the exact solve's own output");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < sr.pts.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected,
        "every point, original and Steiner both, is in one connected component");
}

const char *kNinePinGr = R"(grid 12 6 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netNine 0 9 1
5 5 1
25 5 1
45 5 1
65 5 1
85 5 1
15 35 1
35 35 1
55 35 1
75 35 1
0
)";

// The degree cap: this fixture's 9 unique pins exceed FR4ROUTE_EXACT_MAX_
// PINS's default of 8, so the exact solve must be skipped entirely for
// this net -- exercising fr3route's inherited cheap-Steinerization
// fallback, not the exact path silently running anyway (which it is
// mathematically capable of; the cap is a runtime/complexity choice, not
// a correctness limit -- see the file header).
void testDegreeCapGatingExercisesFr3sFallbackExactly() {
  Design d = parse("fr4route_selftest_ninepin.gr", kNinePinGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netNine produced a route");
  check(rep.bestOverflow == 0, "netNine is uncongested (sanity)");

  std::vector<Point> pts = {{0, 0}, {2, 0}, {4, 0}, {6, 0}, {8, 0},
                             {1, 3}, {3, 3}, {5, 3}, {7, 3}};
  check(pts.size() == 9,
        "this fixture's 9 unique pins exceed the default exact-solve cap "
        "of 8 (sanity)");
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  SteinerResult cheap = steinerizeTree(pts, mst);
  long long cheapSpan = 0;
  for (auto &e : cheap.edges)
    cheapSpan += manhattan(cheap.pts[e.first], cheap.pts[e.second]);

  long long routedSpan = 0;
  for (const Seg &s : rep.nets[0].segs)
    routedSpan += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
  check(routedSpan == cheapSpan,
        "the real production pipeline's total charged wirelength for this "
        "9-pin net matches fr3route's own cheap Steinerization exactly -- "
        "proving the degree cap genuinely routed it through the fallback, "
        "not the exact solver silently running anyway");
}

// FR4ROUTE_EXACT_STEINER=0 alone (fr3route's own FR3ROUTE_STEINERIZE left
// at its default of enabled) must fall back to fr3route's cheap pass, not
// all the way down to fr1route's plain MST -- a distinct check from
// testDisablingSteinerizeReproducesPlainMstTopology, which disables both.
void testDisablingExactSteinerAloneFallsBackToCheapPassNotPlainMst() {
  Design d = parse("fr4route_selftest_exactoff.gr", kHubGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                         /*useSteinerize=*/true, /*useExactSteiner=*/false);
  check(rep.nets.size() == 1, "netHub produced a route with FR4ROUTE_EXACT_STEINER=0");
  check(rep.nets[0].pts.size() == 4,
        "with only the exact solve disabled (fr3route's own cheap pass "
        "still enabled), a 4th Steiner point is still appended -- the "
        "fallback engaged, this did not silently degrade to the plain MST");
  long long span = 0;
  for (const Seg &s : rep.nets[0].segs)
    span += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
  check(span == 20,
        "the cheap pass alone reaches the same optimal 20 on this "
        "3-terminal fixture (the coordinate-wise median is exact for 3 "
        "terminals, so cheap and exact agree here) -- FR4ROUTE_EXACT_"
        "STEINER=0 fell back to fr3route's mechanism, not to fr1route's "
        "plain MST (which would total 25)");
}

const char *kTwoHubsGr = R"(grid 18 8 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 2
netHubA 0 4 1
5 35 1
45 35 1
25 65 1
25 5 1
netHubB 1 4 1
105 35 1
145 35 1
125 65 1
125 5 1
0
)";

// The end-to-end decisive test: two copies of the degree-three hub shape
// (each unreachable by fr3route's cheap pass, per the decisive unit test
// above) routed through the real production pipeline. Both mechanisms
// disabled reproduces the plain MST's 14 per net (28 total); the exact
// solve enabled (default) reaches 10 per net (20 total).
void testEndToEndExactSteinerizationReducesRealChargedWirelength() {
  auto totalSpan = [](const RunReport &rep) {
    long long total = 0;
    for (const RoutedNet &rn : rep.nets)
      for (const Seg &s : rn.segs)
        total += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
    return total;
  };

  Design dWith = parse("fr4route_selftest_e2e_with.gr", kTwoHubsGr);
  RunReport withExact = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(withExact.bestOverflow == 0, "both hub nets are uncongested (sanity)");
  check(totalSpan(withExact) == 20,
        "with the exact solve enabled (default), both 4-pin hub nets "
        "reach their true 10-tile-edge optimum each, totaling 20");

  Design dWithout = parse("fr4route_selftest_e2e_without.gr", kTwoHubsGr);
  RunReport withoutExact = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                                  /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                                  /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                                  /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                                  /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(totalSpan(withoutExact) == 28,
        "with both Steinerization mechanisms disabled, the same two nets "
        "fall back to the plain MST's own 14-tile-edge span each, "
        "totaling 28 -- confirming the exact solve's real benefit through "
        "the full production pipeline, not exactSteinerTree in isolation");
  check(totalSpan(withExact) < totalSpan(withoutExact),
        "the exact solve strictly reduces real charged wirelength on "
        "fixtures fr3route's cheap pass alone cannot touch");
}

const char *kTwoPinGr = R"(grid 6 6 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netTwo 0 2 1
5 5 1
45 45 1
0
)";

// 2-pin nets and already-optimal (collinear) small nets must come back
// untouched by the exact solve, the same "strictly better only" gate
// fr3route's own cheap pass already proves for itself.
void testTwoPinAndAlreadyOptimalNetsAreUntouchedByTheExactSolve() {
  Design d2 = parse("fr4route_selftest_twopin.gr", kTwoPinGr);
  RunReport rep2 = route(d2, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep2.nets.size() == 1, "netTwo produced a route");
  check(rep2.nets[0].pts.size() == 2,
        "a 2-pin net is never handed to the exact solve (k>=3 required) "
        "-- no Steiner point is ever possible for 2 terminals anyway");

  std::vector<Point> collinear = {{0, 0}, {10, 0}, {5, 0}};
  SteinerResult sr = exactSteinerTree(collinear);
  check(sr.pts.size() == collinear.size(),
        "the exact solve appends no Steiner point on an already-optimal "
        "collinear fixture");
  check(sr.edges.size() == collinear.size() - 1,
        "still a valid tree (edges = points-1)");
}

// ---------------------------------------------------------------------------
// Layer-assignment mechanism tests: this file's own new addition over
// fr4route. Two real layers, both bidirectional, horizontal capacity 1
// each -- tight enough that two nets sharing the exact same horizontal
// edge must land on different real layers to both fit.
// ---------------------------------------------------------------------------

const char *kTwoLayerBidirGr = R"(grid 6 6 2
vertical capacity 20 20
horizontal capacity 1 1
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 2
netA 0 2 1
5 25 1
35 25 1
netB 1 2 1
5 25 1
35 25 1
0
)";

// The decisive test: assignSegmentLayer's choice is driven by real,
// running per-layer usage, not a static "always pick the first candidate"
// rule. Both nets need the identical horizontal edge; the 2D-collapsed
// solve (capacity 1+1=2) converges both onto it with 0 overflow, but the
// real per-layer capacity (1 each) forces the second net's segment onto
// the other real layer once the first has committed.
void testCapacityAwareLayerChoiceReactsToRealUsageNotJustPreference() {
  Design d = parse("fr5route_selftest_capaware.gr", kTwoLayerBidirGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 2, "both nets produced a route");
  check(rep.bestOverflow == 0,
        "the 2D-collapsed solve sees combined capacity 2 for exactly 2 "
        "units of demand -- no overflow there (sanity)");
  negotiateViaAssignment(d.grid, rep.nets);

  const RoutedNet *netA = nullptr, *netB = nullptr;
  for (const RoutedNet &rn : rep.nets) {
    if (rn.net->name == "netA")
      netA = &rn;
    if (rn.net->name == "netB")
      netB = &rn;
  }
  check(netA != nullptr && netB != nullptr, "found both nets by name (sanity)");

  // Find each net's own wire leg specifically (not any via segment
  // stitchPinAccessForNet may since have added -- both nets' pins are
  // declared on layer index 0 in this fixture, so whichever net's wire
  // landed on a different layer now also carries pin-access vias, which
  // is correct behavior, not this test's own concern).
  auto wireLayer = [](const RoutedNet &rn) {
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2))
        return s.l1;
    return -1;
  };
  check(wireLayer(*netA) == 0,
        "the first-committed net ties on empty-usage cost and lands on "
        "the lower-indexed real layer, 0");
  check(wireLayer(*netB) == 1,
        "the second net's identical-position segment sees layer 0 "
        "already at its real capacity (usage 1, cap 1) and is pushed to "
        "real layer 1 instead -- driven by RealUsage, not a static rule");
}

// Direction preference: layer 0 horizontal-only, layer 1 vertical-only.
// A net whose tree needs both a horizontal and a vertical leg must never
// place either on the wrong-direction layer.
const char *kDirectionPreferenceGr = R"(grid 6 6 2
vertical capacity 0 20
horizontal capacity 20 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netL 0 2 1
5 5 1
35 35 1
0
)";

void testDirectionPreferenceIsNeverViolated() {
  Design d = parse("fr5route_selftest_direction.gr", kDirectionPreferenceGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netL produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  bool sawHorizontal = false, sawVertical = false;
  for (const Seg &s : rep.nets[0].segs) {
    if (s.l1 != s.l2)
      continue; // a via, not a wire leg -- direction is meaningless here
    if (s.y1 == s.y2) {
      sawHorizontal = true;
      check(s.l1 == 0, "a horizontal leg landed on layer 0 (the only "
                        "horizontal-capable real layer)");
    } else {
      sawVertical = true;
      check(s.l1 == 1, "a vertical leg landed on layer 1 (the only "
                        "vertical-capable real layer)");
    }
  }
  check(sawHorizontal && sawVertical,
        "this L-shaped net actually exercises both directions (sanity -- "
        "otherwise the two checks above would be vacuously true)");
}

// Via stitching, tested directly on stitchViasForNet with a hand-built,
// controlled RoutedNet: three legs meeting at hub point (5,5), on three
// different real layers (0, 2, 4) by construction (not derived from real
// routing -- isolating this mechanism from assignSegmentLayer's own
// choices, which the two tests above already cover).
void testViaStitchingConnectsAThreeWayLayerMismatch() {
  RoutedNet rn;
  rn.segs = {
      {0, 5, 0, 5, 5, 0},  // layer 0, from (0,5) to the hub
      {5, 5, 2, 10, 5, 2}, // layer 2, from the hub outward
      {5, 0, 4, 5, 5, 4},  // layer 4, from (5,0) to the hub
  };
  size_t before = rn.segs.size();
  stitchViasForNet(rn);
  check(rn.segs.size() > before, "via segments were actually inserted");
  check(segsConnected(rn.segs),
        "after stitching, all three legs -- on three different real "
        "layers -- are connected in full 3D, the actual point of this "
        "stage");

  int viaCount = 0;
  for (const Seg &s : rn.segs) {
    if (s.x1 != s.x2 || s.y1 != s.y2 || s.l1 == s.l2)
      continue;
    check(s.x1 == 5 && s.y1 == 5, "every via sits at the hub point (5,5)");
    check(std::abs(s.l1 - s.l2) == 1, "every via is a single real-layer "
                                       "step, matching the real 'one tile "
                                       "of wirelength per via crossing' "
                                       "scoring rule");
    ++viaCount;
  }
  check(viaCount == 2 + 4,
        "the reference layer is the minimum present (0); reaching layer "
        "2 costs 2 unit steps and reaching layer 4 costs 4 -- 6 via "
        "segments total, none wasted");
}

// The decisive regression test for the second bug found during this
// generation's own real-benchmark verification (see coverageAt's own
// comment): the first version of this stage only checked segment
// *endpoints* for layer mismatches, missing MSMV's own defining
// mechanism -- attaching new wire to any point on already-committed
// wire, mid-span, not just at another tree edge's two ends. This fixture
// reproduces exactly that shape: a long already-committed leg, and a
// second leg attaching at a point strictly inside the first leg's own
// span (not one of its two declared endpoints), on a different layer.
void testViaStitchingCatchesMidSpanMsmvAttachmentNotJustEndpoints() {
  RoutedNet rn;
  rn.segs = {
      {0, 5, 0, 10, 5, 0}, // an already-committed wire, layer 0, x=0..10
      {5, 5, 2, 5, 8, 2},  // a new leg attaching mid-span at (5,5), layer 2
  };
  size_t before = rn.segs.size();
  stitchViasForNet(rn);
  check(rn.segs.size() > before,
        "a via was inserted at the mid-span attachment point (5,5), not "
        "just at segment endpoints -- the exact gap this fix closes");
  check(segsConnected(rn.segs),
        "the two legs are connected in full 3D even though their shared "
        "point is the middle of the first leg's own span, not one of its "
        "declared endpoints -- exactly what MSMV produces at scale on "
        "real benchmarks (5974 disconnected nets on adaptec1 before this "
        "fix)");
}

const char *kLongEdgeGr = R"(grid 8 2 1
vertical capacity 20
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netLong 0 2 1
5 5 1
75 5 1
0
)";

// A single Seg spanning several unit tile-edges must charge every one of
// them into RealUsage, not just the first -- the same class of bug
// CLAUDE.md's own "a giant net must still charge usage for whatever route
// it emits" warning describes, applied here to per-leg real-layer
// charging. Calls assignSegmentLayer directly, bypassing route()
// entirely, so the only thing under test is the charging loop itself.
void testMultiTileSegmentChargesEveryUnitEdgeNotJustOnce() {
  Design d = parse("fr5route_selftest_longedge.gr", kLongEdgeGr);
  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  Seg s{0, 0, 0, 5, 0, 0}; // horizontal leg spanning tile-edges x=0..4
  assignSegmentLayer(d.grid, ru, lp, /*width=*/1, s);
  check(s.l1 == 0 && s.l2 == 0, "only one real layer exists here (sanity)");
  for (int x = 0; x < 5; ++x) {
    size_t idx = d.grid.hIndex(x, 0, 0);
    check(ru.h[idx] == 1,
          "unit tile-edge x=" + std::to_string(x) +
              " was charged exactly once by this single 5-tile-edge leg "
              "-- not skipped, not double-charged");
  }
}

// The decisive regression test for the third bug found during this
// generation's own real-benchmark verification (see dropDegenerateSegs's
// own comment): route()'s own inherited candidate-commit code has always
// emitted zero-length, same-layer segments -- (x,y,l)-(x,y,l) -- which
// eval2008.pl's real scorer rejects outright as a "null route", even
// though gr_verify never flagged them and they contribute nothing to
// real wirelength either way. A hand-built RoutedNet mixing one real
// wire leg with two degenerate ones (matching the exact shape found in
// fr4route's own real adaptec1 output, net n7807) confirms only the
// degenerate ones are removed.
void testDegenerateZeroLengthSegmentsAreDropped() {
  RoutedNet rn;
  rn.segs = {
      {0, 0, 0, 10, 0, 0},  // a real wire leg
      {5, 5, 1, 5, 5, 1},   // degenerate: same point, same layer
      {20, 20, 2, 20, 20, 2}, // another, elsewhere in the net
  };
  dropDegenerateSegs(rn);
  check(rn.segs.size() == 1, "both degenerate segments were removed, "
                              "the real leg was not");
  check(rn.segs[0].x1 == 0 && rn.segs[0].x2 == 10,
        "the surviving segment is exactly the real wire leg, untouched");
}

// FR1ROUTE_TWOD_ONLY=1's own long-documented purpose, finally made real:
// main() never calls negotiateViaAssignment() at all when it is set, reproducing
// fr4route's exact single-layer output. Self-tests call route() directly
// (not main()), so main()'s own gating is reproduced here explicitly;
// reuses kTwoLayerBidirGr so the two branches genuinely differ (a
// single-layer fixture would make both branches look identical and prove
// nothing).
void testTwodOnlyReproducesFr4routesExactSingleLayerOutput() {
  Design dWith = parse("fr5route_selftest_twod_with.gr", kTwoLayerBidirGr);
  RunReport repWith = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  // FR1ROUTE_TWOD_ONLY=1 means main() skips negotiateViaAssignment() entirely.
  for (const RoutedNet &rn : repWith.nets)
    for (const Seg &s : rn.segs)
      check(s.l1 == 0 && s.l2 == 0,
            "with negotiateViaAssignment() skipped, every segment stays on "
            "fr4route's exact placeholder layer 0 -- no vias, byte for "
            "byte what fr4route itself produces");

  Design dWithout = parse("fr5route_selftest_twod_without.gr", kTwoLayerBidirGr);
  RunReport repWithout = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dWithout.grid, repWithout.nets);
  bool sawLayer1 = false;
  for (const RoutedNet &rn : repWithout.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == 1)
        sawLayer1 = true;
  check(sawLayer1,
        "without the flag, negotiateViaAssignment() actually ran and moved at "
        "least one net's segment to real layer 1 -- the two branches "
        "genuinely differ, not just an inert flag read");
}

const char *kPinLayerMismatchGr = R"(grid 6 2 3
vertical capacity 20 20 20
horizontal capacity 20 20 20
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netPin 0 2 1
5 5 1
35 5 3
0
)";

// The decisive regression test for the bug found during this generation's
// own real-benchmark verification (see stitchPinAccessForNet's own
// comment): a 2-pin net where one pin is declared on layer 1 (file,
// 1-indexed -- index 0) and the other on layer 3 (index 2). The greedy
// wire assignment has no congestion-driven reason to ever choose layer 2
// on its own (every layer here is ample and equally cheap, so it ties to
// the lowest index, 0) -- reproducing exactly the real-benchmark failure
// mode where a pin's own declared layer is never naturally reached.
// gr_verify's own connectivity check (matching eval2008.pl) is reproduced
// directly here: the route must touch each pin's exact (x, y, layer).
void testPinAccessLayerIsAlwaysReached() {
  Design d = parse("fr5route_selftest_pinlayer.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  RoutedNet &rn = rep.nets[0];

  check(segsConnected(rn.segs),
        "the net is fully connected in 3D after pin-access stitching");

  auto touches = [&](int x, int y, int l) {
    for (const Seg &s : rn.segs)
      if ((s.x1 == x && s.y1 == y && s.l1 == l) ||
          (s.x2 == x && s.y2 == y && s.l2 == l))
        return true;
    return false;
  };
  check(touches(0, 0, 0), "pin A's own declared layer (index 0) is reached");
  check(touches(3, 0, 2),
        "pin B's own declared layer (index 2) is reached -- even though "
        "the wire's own greedy choice, driven purely by congestion, has "
        "no reason to land there on its own, exactly the gap "
        "stitchPinAccessForNet exists to close");
}

const char *kMultiNetMultiLayerGr = R"(grid 10 10 3
vertical capacity 4 4 4
horizontal capacity 4 4 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 3
netA 0 2 1
5 5 1
85 85 1
netB 1 4 1
15 5 1
75 5 1
15 95 1
15 15 1
netC 2 2 1
95 5 1
5 95 1
0
)";

// The end-to-end decisive test: a multi-net, multi-layer design routed
// through the real production pipeline (route() -> negotiateViaAssignment(), not
// any single mechanism in isolation) -- every net must be one connected
// component in full 3D (real layers plus any stitched vias), the actual
// property this whole stage exists to guarantee. The authoritative
// gr_verify/eval2008.pl cross-check happens separately against the real
// benchmark runs below, matching every prior generation's own convention
// (self-tests here are pure in-process checks; the real external-tool
// cross-check is part of this file's own real-benchmark verification,
// not --selftest).
void testEndToEndRoutingAndLayerAssignmentProducesAConnectedThreeDResult() {
  Design d = parse("fr5route_selftest_e2e.gr", kMultiNetMultiLayerGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 3, "all three nets produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  for (const RoutedNet &rn : rep.nets)
    check(segsConnected(rn.segs),
          "net '" + rn.net->name +
              "' is a single connected component in full 3D after the "
              "real route() -> negotiateViaAssignment() pipeline");
}

// ---------------------------------------------------------------------------
// Joint via-cost/capacity-aware tree DP tests: this file's own new
// addition over fr5route. A shared 2-layer, tight-via fixture anchors
// most of these -- a horizontal leg A=(0,0)-(5,0) and a vertical leg
// B=(5,0)-(5,5) meeting at (5,0), the smallest possible real branch
// point (exactly one via decision to make).
// ---------------------------------------------------------------------------

const char *kTightViaGr = R"(grid 8 8 2
vertical capacity 10 10
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 10 10
0 0 10 10
num net 1
netAB 0 2 1
5 5 1
55 55 1
0
)";

// The decisive test: the joint DP avoids an avoidable via a greedy,
// independent choice would create. Real usage is pre-loaded so A's own
// cheapest layer in isolation is 0 and B's is 1 (a moderate margin), but
// the via site at (5,0) is tight enough (capacity 1, via spacing 10 on a
// 10x10 tile) that its cost dominates the wire-cost margin.
void testJointDPAvoidsAnAvoidableViaGreedyWouldCreate() {
  Design d = parse("fr6route_selftest_dpvia.gr", kTightViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 1,
        "via capacity between the two layers is tight, exactly 1 (sanity)");

  RealUsage ru(d.grid);
  for (int x = 0; x < 5; ++x)
    ru.h[d.grid.hIndex(x, 0, 1)] += 1; // layer 1 slightly more loaded for A
  for (int y = 0; y < 5; ++y)
    ru.v[d.grid.vIndex(5, y, 0)] += 1; // layer 0 slightly more loaded for B

  std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.nodes.size() == 3 && t.edges.size() == 2,
        "two tree edges meeting at one branch point (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result (sanity)");
  check(edgeLayer[0] == edgeLayer[1],
        "the joint DP matches both edges to the same layer -- accepting "
        "a wire-cost penalty on one of them to avoid the tight via site "
        "entirely, which a per-edge independent choice has no reason to "
        "do");

  // Contrast directly: fr5route's own inherited, independent
  // assignSegmentLayer, given the identical starting usage, has no
  // via-cost awareness and picks each edge's own local optimum
  // regardless of the other.
  RealUsage ru2(d.grid);
  for (int x = 0; x < 5; ++x)
    ru2.h[d.grid.hIndex(x, 0, 1)] += 1;
  for (int y = 0; y < 5; ++y)
    ru2.v[d.grid.vIndex(5, y, 0)] += 1;
  Seg sa{0, 0, 0, 5, 0, 0}, sb{5, 0, 0, 5, 5, 0};
  assignSegmentLayer(d.grid, ru2, lp, 1, sa);
  assignSegmentLayer(d.grid, ru2, lp, 1, sb);
  check(sa.l1 != sb.l1,
        "the OLD independent-greedy assignSegmentLayer picks different "
        "layers for A and B on the identical starting usage -- proving "
        "the joint DP's matching choice above is real, new behavior, "
        "not a coincidence both mechanisms would reach anyway");
}

// This file's own tuning knob (added after the real-benchmark result in
// HISTORY_COST_SWEEP.md Part 26) has a real, direct effect: on the
// identical tight-via fixture above, a low via weight should make the
// DP behave like the old independent-greedy path (mismatch, since the
// via cost that used to deter it is now negligible), not like the
// default weight=1.0 case (match).
void testViaWeightKnobHasRealEffect() {
  Design d = parse("fr6route_selftest_viaweight.gr", kTightViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);

  RealUsage ru(d.grid);
  for (int x = 0; x < 5; ++x)
    ru.h[d.grid.hIndex(x, 0, 1)] += 1;
  for (int y = 0; y < 5; ++y)
    ru.v[d.grid.vIndex(5, y, 0)] += 1;

  std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, nodeStackRange,
                       /*viaWeight=*/0.01),
        "the DP produced a result at a low via weight (sanity)");
  check(edgeLayer[0] != edgeLayer[1],
        "at a low via weight, the same tight-via site that made the "
        "default (1.0) match layers is no longer worth avoiding -- the "
        "DP mismatches instead, same as the old independent-greedy path "
        "-- proving the weight parameter genuinely reaches the DP's own "
        "cost computation, not just accepted and ignored");
}

// Via capacity is charged, and genuinely changes a later net's own
// decision, not just passively tracked. A separate fixture (via spacing
// 3, not 10) gives via capacity 3, headroom enough that a first net's
// own moderate wire-cost-vs-via tradeoff can go either way.
const char *kModerateViaGr = R"(grid 8 8 2
vertical capacity 10 10
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 3 3
0 0 10 10
num net 1
netAB 0 2 1
5 5 1
55 55 1
0
)";

void testViaCapacityIsChargedAndInfluencesLaterDecisions() {
  Design d = parse("fr6route_selftest_viacap.gr", kModerateViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 3, "derived via capacity is 3 (sanity)");

  auto runOneNet = [&]() -> bool {
    RealUsage ru(d.grid); // fresh per net -- isolates the wire-cost side
    for (int x = 0; x < 5; ++x)
      ru.h[d.grid.hIndex(x, 0, 1)] += 5;
    for (int y = 0; y < 5; ++y)
      ru.v[d.grid.vIndex(5, y, 0)] += 5;
    std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
    NetTree t;
    if (!buildNetTree(segs, t))
      return false;
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    if (!layerAssignDP(d.grid, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, nodeStackRange))
      return false;
    RoutedNet rn;
    commitNetTree(d.grid, ru, rvu, 1, t, edgeLayer, nodeStackRange, rn);
    return edgeLayer[0] != edgeLayer[1];
  };

  bool net1Mismatched = runOneNet();
  check(net1Mismatched,
        "the first net, seeing an uncongested via site, prefers "
        "mismatching layers and paying for the still-cheap via over a "
        "larger wire-cost penalty");
  check(rvu.usage[viaIndex(d.grid, 5, 0, 0)] == 1,
        "the first net's via choice was actually charged into RealViaUsage");

  bool net2Mismatched = runOneNet();
  check(!net2Mismatched,
        "the second net, at the identical site now carrying the first "
        "net's charge, finds the same via has become more expensive than "
        "the wire-cost penalty and matches layers instead -- via capacity "
        "genuinely influences the decision, not just tracked for show");
}

// A real 3-edge branch point: parent P arrives at (5,0) from the left,
// child C1 goes up, child C2 continues right. Wire costs are engineered
// so each edge's own truly cheapest layer is overwhelmingly dominant --
// P at layer 0, C1 at layer 3, C2 at layer 5 -- forcing exactly the
// shape fr6route's own header flagged as a likely overcounting case.
const char *kThreeWayBranchGr = R"(grid 12 12 6
vertical capacity 100 100 100 100 100 100
horizontal capacity 100 100 100 100 100 100
minimum width 1 1 1 1 1 1
minimum spacing 0 0 0 0 0 0
via spacing 1 1 1 1 1 1
0 0 10 10
num net 1
netBranch 0 2 1
5 5 1
105 5 1
0
)";

// The decisive test for this generation's own fix. fr6route's own
// pairwise-chain model would have charged chain(0,3)=3 plus chain(0,5)=5
// = 8 via unit-steps to connect this exact solution (double-counting the
// shared 0->3 portion); the true physical cost is one stack spanning
// 0 to 5, 5 steps. Confirms the new model finds the true span, charges
// exactly 5 -- not 8 -- and still lets every edge keep its own
// genuinely cheapest layer rather than being forced to compromise.
void testSharedViaStackReplacesOvercountedPairwiseChains() {
  Design d = parse("fr7route_selftest_branch.gr", kThreeWayBranchGr);
  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 5, "5 layer-pairs among 6 layers (sanity)");
  for (int cap : viaCap)
    check(cap == 10, "derived via capacity is 10 per layer-pair (sanity)");

  // P: (0,0)-(5,0) horizontal -- overwhelmingly prefers layer 0.
  for (int x = 0; x < 5; ++x)
    for (int l = 1; l < 6; ++l)
      ru.h[d.grid.hIndex(x, 0, l)] += 1000;
  // C1: (5,0)-(5,5) vertical -- overwhelmingly prefers layer 3.
  for (int y = 0; y < 5; ++y)
    for (int l = 0; l < 6; ++l)
      if (l != 3)
        ru.v[d.grid.vIndex(5, y, l)] += 1000;
  // C2: (5,0)-(10,0) horizontal -- overwhelmingly prefers layer 5.
  for (int x = 5; x < 10; ++x)
    for (int l = 0; l < 5; ++l)
      ru.h[d.grid.hIndex(x, 0, l)] += 1000;

  std::vector<Seg> segs = {
      {0, 0, 0, 5, 0, 0},  // P
      {5, 0, 0, 5, 5, 0},  // C1
      {5, 0, 0, 10, 0, 0}, // C2
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.nodes.size() == 4 && t.edges.size() == 3,
        "4 nodes, 3 edges meeting at one real branch point (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result");

  // Identify each edge by its own endpoints, not index order, since
  // buildNetTree's own internal ordering is an implementation detail
  // this test shouldn't depend on.
  int pLayer = -1, c1Layer = -1, c2Layer = -1;
  for (size_t i = 0; i < t.edges.size(); ++i) {
    const auto &e = t.edges[i];
    Point a = t.nodes[e.a], b = t.nodes[e.b];
    if ((a.x == 0 && a.y == 0) || (b.x == 0 && b.y == 0))
      pLayer = edgeLayer[i];
    else if ((a.x == 5 && a.y == 5) || (b.x == 5 && b.y == 5))
      c1Layer = edgeLayer[i];
    else if ((a.x == 10 && a.y == 0) || (b.x == 10 && b.y == 0))
      c2Layer = edgeLayer[i];
  }
  check(pLayer == 0, "P keeps its own overwhelmingly cheapest layer, 0");
  check(c1Layer == 3,
        "C1 keeps its own overwhelmingly cheapest layer, 3 -- not forced "
        "to match P, since even a shared stack is far cheaper than "
        "abandoning its own cheap layer");
  check(c2Layer == 5, "C2 keeps its own overwhelmingly cheapest layer, 5, for the same reason");

  int branchNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 5 && t.nodes[i].y == 0)
      branchNode = static_cast<int>(i);
  check(branchNode >= 0, "found the branch point (5,0) (sanity)");
  check(nodeStackRange[branchNode] == std::make_pair(0, 5),
        "the branch point's own resolved via-stack range is exactly "
        "[0,5] -- one shared stack spanning every layer actually used "
        "there, not fr6route's own separate per-neighbor chains");

  RoutedNet rn;
  commitNetTree(d.grid, ru, rvu, 1, t, edgeLayer, nodeStackRange, rn);
  check(segsConnected(rn.segs),
        "the committed net is fully connected in 3D through the shared "
        "stack");

  int viaSteps = 0;
  for (const Seg &s : rn.segs)
    if (s.x1 == s.x2 && s.y1 == s.y2 && s.l1 != s.l2)
      ++viaSteps;
  check(viaSteps == 5,
        "exactly 5 via unit-steps were emitted at the branch point -- the "
        "true cost of one 0->5 stack. fr6route's own pairwise-chain model "
        "would have charged chain(0,3)=3 plus chain(0,5)=5 = 8 for this "
        "exact same solution, double-counting the shared 0->3 portion");

  long long totalViaUsage = 0;
  for (long long u : rvu.usage)
    totalViaUsage += u;
  check(totalViaUsage == 5,
        "RealViaUsage was charged exactly 5 times total, matching the "
        "shared stack -- not 8, and no layer-pair double-charged");
}

// Tree reconstruction correctly splits a mid-span MSMV-style attachment
// -- the same fixture shape as fr5route's own decisive via-stitching
// regression test, now at the tree-reconstruction level.
void testTreeReconstructionSplitsAMidSpanAttachment() {
  std::vector<Seg> segs = {
      {0, 5, 0, 10, 5, 0}, // an already-committed leg, x=0..10 at y=5
      {5, 5, 0, 5, 8, 0},  // a new leg attaching mid-span at (5,5)
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded");
  check(t.nodes.size() == 4,
        "4 nodes: (0,5), (5,5) [the split point], (10,5), (5,8)");
  check(t.edges.size() == 3,
        "the long leg was split into 2 edges at the attachment point, "
        "plus the attaching leg itself -- 3 total, not the original 2 "
        "raw segments");
}

// Tree validity on a real 4-way branch point (a "plus" shape, no
// mid-span splitting needed since every leg's own endpoint already
// coincides with the hub) -- edges == nodes-1, connected, no cycle, the
// same convention every prior generation's own tests already use.
void testTreeReconstructionIsAlwaysAValidTree() {
  std::vector<Seg> segs = {
      {0, 3, 0, 2, 3, 0}, // A - hub
      {2, 3, 0, 4, 3, 0}, // hub - B
      {2, 0, 0, 2, 3, 0}, // D - hub
      {2, 3, 0, 2, 6, 0}, // hub - C
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded");
  check(t.edges.size() == t.nodes.size() - 1,
        "edge count is exactly one less than node count on a real "
        "4-way branch point");

  std::vector<int> parent(t.nodes.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : t.edges) {
    int ra = find(e.a), rb = find(e.b);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle in the reconstructed tree");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected, "every node is in one connected component");
}

// Pin access remains correctly enforced through the new DP-based
// pipeline, and its own via is now charged into RealViaUsage --
// previously (fr5route) entirely free. Reuses fr5route's own decisive
// pin-layer-mismatch fixture.
void testPinAccessViaIsChargedIntoRealViaUsage() {
  Design d = parse("fr6route_selftest_pinlayer.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route");

  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  RoutedNet &rn = rep.nets[0];
  dropDegenerateSegs(rn);
  NetTree t;
  check(buildNetTree(rn.segs, t), "tree reconstruction succeeded");
  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, rn.net->minWidth, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result");
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rn);

  long long viaUsageBefore = 0;
  for (long long u : rvu.usage)
    viaUsageBefore += u;
  stitchPinAccessForNetChargingVias(d.grid, rvu, rn);
  long long viaUsageAfter = 0;
  for (long long u : rvu.usage)
    viaUsageAfter += u;
  check(viaUsageAfter > viaUsageBefore,
        "the pin-access stitch actually charged new via usage -- "
        "entirely uncharged in fr5route");

  auto touches = [&](int x, int y, int l) {
    for (const Seg &s : rn.segs)
      if ((s.x1 == x && s.y1 == y && s.l1 == l) || (s.x2 == x && s.y2 == y && s.l2 == l))
        return true;
    return false;
  };
  check(touches(0, 0, 0), "pin A's own declared layer is still reached");
  check(touches(3, 0, 2), "pin B's own declared layer is still reached");
}

// NV2ROUTE_VIA_AWARE=0 must reproduce fr5route's exact independent-
// greedy-plus-stitching behavior, segment for segment -- the same
// "toggle reproduces the parent" pattern every mechanism in this
// project uses.
void testViaAwareOffReproducesFr5routesExactBehavior() {
  Design dOff = parse("fr6route_selftest_viaoff.gr", kTwoLayerBidirGr);
  setenv("NV2ROUTE_VIA_AWARE", "0", 1);
  RunReport repOff = route(dOff, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dOff.grid, repOff.nets);
  unsetenv("NV2ROUTE_VIA_AWARE");

  Design dRef = parse("fr6route_selftest_viaoff_ref.gr", kTwoLayerBidirGr);
  RunReport repRef = route(dRef, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  RealLayerPlan lp(dRef.grid);
  RealUsage ru(dRef.grid);
  for (RoutedNet &rn : repRef.nets) {
    dropDegenerateSegs(rn);
    for (Seg &s : rn.segs)
      assignSegmentLayer(dRef.grid, ru, lp, rn.net->minWidth, s);
    stitchViasForNet(rn);
    stitchPinAccessForNet(rn);
  }

  check(repOff.nets.size() == repRef.nets.size(), "same net count (sanity)");
  for (size_t i = 0; i < repOff.nets.size(); ++i) {
    check(repOff.nets[i].segs.size() == repRef.nets[i].segs.size(),
          "NV2ROUTE_VIA_AWARE=0 produces the same segment count as fr5route's "
          "own independent-greedy pipeline, replayed manually here");
    for (size_t j = 0; j < repOff.nets[i].segs.size() && j < repRef.nets[i].segs.size(); ++j) {
      const Seg &a = repOff.nets[i].segs[j];
      const Seg &b = repRef.nets[i].segs[j];
      check(a.x1 == b.x1 && a.y1 == b.y1 && a.l1 == b.l1 && a.x2 == b.x2 && a.y2 == b.y2 &&
                a.l2 == b.l2,
            "segment " + std::to_string(j) + " of net " + std::to_string(i) +
                " matches fr5route's own output exactly, field for field");
    }
  }
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: decisive tests for the negotiation loop
// itself -- charge-log exactness, the LIFO-relaxable historyVia term, the
// negotiation loop actually fixing something a single pass structurally
// cannot, and the supervised accept/revert primitives it's built from.
// ---------------------------------------------------------------------------

// The risk this file's own header discloses up front: more moving parts
// than any prior generation means more places for a subtle bug to hide,
// and a missed charge-log site (in particular pin-access stitching, logged
// outside commitNetTree proper) would silently corrupt every round after
// the first. Decisive, exact check: commit a net, undo via its own logged
// charge, recommit with unchanged inputs -- ru/rvu must return to their
// pre-commit all-zero state after the undo, and to byte-identical usage
// after the redo, or some increment somewhere was never logged.
void testChargeLogRoundTripIsExact() {
  Design d = parse("nv1route_selftest_chargelog.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route (sanity)");
  RoutedNet rn = rep.nets[0];
  dropDegenerateSegs(rn);

  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);

  NetTree t;
  check(buildNetTree(rn.segs, t), "tree reconstruction succeeded (sanity)");
  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, rn.net->minWidth, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result (sanity)");

  RoutedNet rnA = rn;
  NetCharge chargeA;
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rnA, &chargeA);
  stitchPinAccessForNetChargingVias(d.grid, rvu, rnA, &chargeA);
  check(!chargeA.rvuIdx.empty(),
        "pin-access stitching logged at least one via charge (sanity -- this "
        "fixture is engineered, same as testPinAccessViaIsChargedIntoRealViaUsage, "
        "to need one)");

  std::vector<long long> ruHAfter = ru.h, ruVAfter = ru.v, rvuAfter = rvu.usage;

  undoCharge(ru, rvu, chargeA);
  bool allZero = true;
  for (long long v : ru.h)
    if (v != 0)
      allZero = false;
  for (long long v : ru.v)
    if (v != 0)
      allZero = false;
  for (long long v : rvu.usage)
    if (v != 0)
      allZero = false;
  check(allZero,
        "undoing the logged charge returns ru/rvu exactly to their pre-commit "
        "all-zero state -- every increment commitNetTree and "
        "stitchPinAccessForNetChargingVias made was logged");

  RoutedNet rnB = rn;
  NetCharge chargeB;
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rnB, &chargeB);
  stitchPinAccessForNetChargingVias(d.grid, rvu, rnB, &chargeB);

  check(ru.h == ruHAfter && ru.v == ruVAfter && rvu.usage == rvuAfter,
        "redoing the identical commit with unchanged inputs reproduces "
        "byte-identical ru/rvu usage -- no charge site was missed");
  check(rnA.segs.size() == rnB.segs.size(), "same segment count on redo (sanity)");
  for (size_t j = 0; j < rnA.segs.size() && j < rnB.segs.size(); ++j) {
    const Seg &a = rnA.segs[j], &b = rnB.segs[j];
    check(a.x1 == b.x1 && a.y1 == b.y1 && a.l1 == b.l1 && a.x2 == b.x2 && a.y2 == b.y2 &&
              a.l2 == b.l2,
          "segment " + std::to_string(j) + " identical on redo");
  }
}

// A two-layer, single-via-layer-pair fixture, capacity 1: five structurally
// identical branch nets (parent leg overwhelmingly prefers layer 0, child
// leg overwhelmingly prefers layer 1 -- same convention as kModerateViaGr/
// testViaCapacityIsChargedAndInfluencesLaterDecisions) contend for one via
// site. Each net's own wire cost is isolated via a fresh RealUsage per net
// (that same established convention) -- the only resource genuinely shared
// across all "5 nets" here is RealViaUsage and this file's own new
// historyVia term, exactly isolating the one new mechanism under test.
// Hand-derived exactly, via the DP's own real resolveNode mechanics (not a
// simplified abstraction): at the branch node, "mismatch" (child free to
// pick its own cheapest layer, 1, plus the via) costs spanCost+1; "match"
// (child forced to the parent's layer, 0, at its own 49-unit-preload
// penalty) costs exactly 50. resolveNode picks match iff spanCost+1>=50,
// i.e. spanCost>=49 (ties favor the first-evaluated candidate, (0,0)).
const char *kFiveWayViaContentionGr = R"(grid 4 4 2
vertical capacity 1 1
horizontal capacity 1 1
minimum width 1 1
minimum spacing 0 0
via spacing 10 10
0 0 10 10
num net 1
netDummy 0 2 1
0 0 1
10 10 1
0
)";

// The decisive test for this file's own core new claim: negotiation
// (history-driven reprocessing across rounds, using the real
// layerAssignDP and updateViaHistory production functions) reduces via
// overflow that a single pass -- fr7route's own one-shot assignLayers, or
// equivalently this file's own round 0 alone -- has no way to fix,
// regardless of net processing order.
void testNegotiationReducesViaOverflowASinglePassCannotFix() {
  Design d = parse("nv1route_selftest_contention.gr", kFiveWayViaContentionGr);
  RealLayerPlan lp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 1, "derived via capacity is exactly 1 (sanity)");

  std::vector<Seg> segs = {{0, 0, 0, 1, 0, 0}, {1, 0, 0, 1, 1, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  int branchNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 1 && t.nodes[i].y == 0)
      branchNode = static_cast<int>(i);
  check(branchNode >= 0, "found the branch node (1,0) (sanity)");
  size_t viaIdx = viaIndex(d.grid, 1, 0, 0);

  RealViaUsage rvu(d.grid);
  ViaPrices vp(d.grid);

  auto freshRu = [&]() {
    RealUsage ru(d.grid);
    ru.h[d.grid.hIndex(0, 0, 1)] += 49; // parent leg's one tile, its wrong layer
    ru.v[d.grid.vIndex(1, 0, 0)] += 49; // child leg's one tile, its wrong layer
    return ru;
  };
  // true if the DP chose to mismatch (via) at the branch node.
  auto runOne = [&]() -> bool {
    RealUsage ru = freshRu();
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                         nodeStackRange, /*viaWeight=*/1.0, &vp.hist),
          "the DP produced a result");
    return nodeStackRange[branchNode].first < nodeStackRange[branchNode].second;
  };

  // Round 0: every net mismatches regardless of order -- spanCost never
  // exceeds 5 (the 5th net's own usage-based cost), nowhere near the
  // 49-unit margin needed to flip even one net, let alone all five.
  for (int i = 0; i < 5; ++i) {
    bool mismatched = runOne();
    check(mismatched, "round 0, net " + std::to_string(i + 1) +
                           " mismatches: spanCost stays well under the 49-unit "
                           "match threshold for every one of the 5 nets");
    if (mismatched)
      rvu.usage[viaIdx] += 1;
  }
  check(rvu.usage[viaIdx] == 5, "round 0 settles at usage 5 against capacity 1 (sanity)");

  updateViaHistory(d.grid, vp, rvu, viaCap, /*histStep=*/0.0, HistMode::kMultiplicative,
                    /*histFactor=*/3.0, /*streakThreshold=*/1, /*scaleByOverflow=*/true,
                    /*relaxEnabled=*/true, 1.0, 0.0);
  check(std::abs(vp.hist[viaIdx] - 11.0) < 1e-9,
        "historyVia grows to exactly 1*(1+(3-1)*5)=11 after round 0's boundary "
        "update (usage/cap ratio 5.0, multiplicative factor 3.0) -- the real "
        "production updateViaHistory function, not a stand-in");

  // Round 1: reprocess all 5 (all still touch the now-overflowed via site),
  // historyVia held at 11 for the whole round (never mid-round). The first
  // net reprocessed sees spanCost=11*(4+1)=55>=49 -- flips to match. Every
  // subsequent net in this same round sees spanCost=11*(3+1)=44<49 --
  // stays mismatched. This is the decisive claim: round 0 (any order)
  // could never produce this, since round 0's own spanCost values (1..5)
  // never came close to 49.
  int flips = 0;
  for (int i = 0; i < 5; ++i) {
    rvu.usage[viaIdx] -= 1; // undo this net's own prior mismatch charge
    bool mismatched = runOne();
    if (mismatched)
      rvu.usage[viaIdx] += 1;
    else
      ++flips;
  }
  check(flips == 1, "exactly one net flips to match in round 1");
  check(rvu.usage[viaIdx] == 4,
        "round 1 reduces via usage from 5 to 4 (overflow 4 to 3) -- something no "
        "single pass, at any net order, could ever produce");

  updateViaHistory(d.grid, vp, rvu, viaCap, 0.0, HistMode::kMultiplicative, 3.0, 1, true, true,
                    1.0, 0.0);
  check(std::abs(vp.hist[viaIdx] - 99.0) < 1e-9,
        "historyVia grows to exactly 11*(1+(3-1)*4)=99 after round 1's boundary "
        "update");

  // Round 2: the 4 remaining mismatched nets are reprocessed (the one that
  // already flipped no longer touches this via site). historyVia=99 now
  // dwarfs the 50-cost match alternative at every step (spanCost 396, 297,
  // 198, 99 -- all comfortably over 49) -- every one flips this round.
  int flips2 = 0;
  for (int i = 0; i < 4; ++i) {
    rvu.usage[viaIdx] -= 1;
    bool mismatched = runOne();
    if (mismatched)
      rvu.usage[viaIdx] += 1;
    else
      ++flips2;
  }
  check(flips2 == 4, "all 4 remaining nets flip to match in round 2");
  check(rvu.usage[viaIdx] == 0,
        "via usage reaches exactly 0 -- zero overflow at this site -- two "
        "negotiation rounds after a single pass left it permanently 4 over "
        "capacity");
}

// Direct ports of this file's own 2D-stage relaxation tests
// (testDisablingRelaxReproducesPermanentBehavior,
// testRelaxationUndoesExactlyOneStepPerCleanRound), applied to
// updateViaHistory/ViaPrices instead of updateHistory/Prices -- same
// mechanism, new domain, so the same decisive tests apply verbatim.
void testViaHistoryRelaxDisabledReproducesPermanentBehavior() {
  Design d = parse("nv1route_selftest_viahist_permanent.gr", kModerateViaGr);
  ViaPrices vp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  size_t idx = viaIndex(d.grid, 0, 0, 0);

  RealViaUsage over(d.grid);
  over.usage[idx] = 100; // comfortably over capacity 3
  updateViaHistory(d.grid, vp, over, viaCap, 1.0, HistMode::kAdditive, 1.0, 1, false,
                    /*relaxEnabled=*/false);
  double afterOver = vp.hist[idx];
  check(afterOver > 1.0, "historyVia grew while the site was over capacity");

  RealViaUsage under(d.grid);
  under.usage[idx] = 0; // now comfortably clear
  updateViaHistory(d.grid, vp, under, viaCap, 1.0, HistMode::kAdditive, 1.0, 1, false,
                    /*relaxEnabled=*/false);
  check(vp.hist[idx] == afterOver,
        "with NV2ROUTE_HIST_RELAX=0, historyVia never decreases even after "
        "usage drops back under capacity -- reproduces iroute's exact "
        "permanent behavior, the fallback this mechanism can always be "
        "switched off back to, same as the 2D stage's own history term");
}

void testViaHistoryRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("nv1route_selftest_viahist_relax.gr", kModerateViaGr);
  ViaPrices vp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  size_t idx = viaIndex(d.grid, 0, 0, 0);

  RealViaUsage over(d.grid);
  over.usage[idx] = 100;
  updateViaHistory(d.grid, vp, over, viaCap, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(vp.hist[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows historyVia to exactly 1.05");

  RealViaUsage under(d.grid);
  under.usage[idx] = 0;
  updateViaHistory(d.grid, vp, under, viaCap, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(vp.hist[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- historyVia "
        "is back to the 1.0 no-op baseline, not just 'lower than before', "
        "proving the LIFO stack genuinely drives this, not just growth being "
        "disabled");
}

// Tests the exact primitives the negotiation loop's own accept/revert step
// (negotiateViaAssignment's own inline logic) is built from --
// localOverflow, undoCharge, applyCharge -- with fully hand-computed
// numbers, independent of the DP: a net's old charge sits at a resource
// already 2 over capacity (cap 3, usage 5); its candidate new charge would
// move it to a *different* resource sitting 5 over (cap 2, usage 7).
// localOverflow must detect the new charge is strictly worse over the
// union of both resources, and after undo(new)+apply(old) -- exactly what
// negotiateViaAssignment's own revert branch does -- both resources must
// be back to their exact pre-attempt usage.
void testSupervisedRevertPrimitivesDetectAndUndoAWorseningMove() {
  Design d = parse("nv1route_selftest_revert.gr", kModerateViaGr);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid); // [3]
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);

  size_t oldIdx = viaIndex(d.grid, 0, 0, 0);
  size_t newIdx = viaIndex(d.grid, 1, 0, 0);
  rvu.usage[oldIdx] = 5; // 2 over capacity 3
  rvu.usage[newIdx] = 7; // 4 over capacity 3 (before this net's own +1 below)

  NetCharge oldCharge;
  oldCharge.rvuIdx = {oldIdx}; // this net's own current contribution: 1 of the 5 at oldIdx
  NetCharge newCharge;
  newCharge.rvuIdx = {newIdx}; // the candidate: 1 more at newIdx, now 8 total there

  long long localBefore = localOverflow(d.grid, ru, rvu, viaCap, oldCharge, oldCharge);
  check(localBefore == 2, "before: only oldIdx is in the union (old,old) -- 5-3=2 over (sanity)");

  // Mirror negotiateViaAssignment's own sequence exactly: undo old, then
  // apply new (standing in for the DP's own commit, since this test is
  // about the revert primitives, not the DP's decision).
  undoCharge(ru, rvu, oldCharge);
  check(rvu.usage[oldIdx] == 4, "undoing the old charge removes exactly this net's own 1 (sanity)");
  applyCharge(ru, rvu, newCharge);
  check(rvu.usage[newIdx] == 8, "applying the new charge adds exactly this net's own 1 (sanity)");

  long long localAfter = localOverflow(d.grid, ru, rvu, viaCap, oldCharge, newCharge);
  check(localAfter == (4 - 3) + (8 - 3), "after: union of (old,new) = {oldIdx,newIdx} -- 1+5=6 over");
  check(localAfter > localBefore,
        "localOverflow correctly detects the candidate move is strictly "
        "worse (6 > 2) -- this is exactly the condition negotiateViaAssignment "
        "reverts on");

  // The revert itself: undo new, reapply old -- negotiateViaAssignment's
  // own rejection branch, verbatim.
  undoCharge(ru, rvu, newCharge);
  applyCharge(ru, rvu, oldCharge);
  check(rvu.usage[oldIdx] == 5 && rvu.usage[newIdx] == 7,
        "after undo(new)+apply(old), both resources are back to their exact "
        "pre-attempt usage -- the revert is byte-for-byte, not approximate");
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: decisive tests for historyWire.
// ---------------------------------------------------------------------------

// A two-layer, single straight edge, no via/branch at all -- cleanly
// isolates wire history from via history entirely. `minimum spacing 0 10`
// gives layer 0 amount=1, layer 1 amount=11: a large, fixed gap so layer 1
// (never used, never overflowed, its own historyWire never grows) stays a
// constant 11/3 alternative throughout, exactly mirroring why the via test
// needed a similarly dominant, fixed gap.
const char *kWireContentionGr = R"(grid 4 4 2
vertical capacity 3 3
horizontal capacity 3 3
minimum width 1 1
minimum spacing 0 10
via spacing 10 10
0 0 10 10
num net 1
netDummy 0 2 1
0 0 1
10 10 1
0
)";

// The decisive test for this file's own core new claim: historyWire
// (history-driven reprocessing across rounds, using the real layerAssignDP
// and updateWireHistory production functions) fixes real per-layer wire
// overflow that a single pass -- nv1route's own one-shot round 0 -- has no
// way to fix, regardless of net processing order. Direct counterpart to
// nv1route's own testNegotiationReducesViaOverflowASinglePassCannotFix,
// same structure, wire domain instead of via.
void testWireHistoryFixesRealOverflowASinglePassCannotFix() {
  Design d = parse("nv2route_selftest_contention.gr", kWireContentionGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);                             // unused: no via in this fixture
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid); // unused
  RealUsage ru(d.grid);
  WirePrices wireHist(d.grid);

  std::vector<Seg> segs = {{0, 0, 0, 1, 0, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.edges.size() == 1, "a single straight edge, no branch/via (sanity)");

  size_t idx0 = d.grid.hIndex(0, 0, 0); // layer 0's tile-edge
  size_t idx1 = d.grid.hIndex(0, 0, 1); // layer 1's tile-edge (never used)

  // true if the DP chose layer 0 (the cheap, contended layer).
  auto runOne = [&]() -> bool {
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                         nodeStackRange, /*viaWeight=*/1.0, /*historyVia=*/nullptr, &wireHist.histH,
                         &wireHist.histV),
          "the DP produced a result");
    return edgeLayer[0] == 0;
  };

  // Round 0: every net picks layer 0 regardless of order -- ratio stays
  // under layer 1's fixed 11/3≈3.667 even at usage=4 ((4+1)/3=1.667).
  for (int i = 0; i < 5; ++i) {
    bool pickedLayer0 = runOne();
    check(pickedLayer0, "round 0, net " + std::to_string(i + 1) +
                             " picks layer 0: ratio stays well under layer 1's fixed 11/3 "
                             "for every one of the 5 nets");
    if (pickedLayer0)
      ru.h[idx0] += 1;
  }
  check(ru.h[idx0] == 5, "round 0 settles at usage 5 against capacity 3 (sanity)");
  check(ru.h[idx1] == 0, "layer 1 never used in round 0 (sanity)");

  updateWireHistory(d.grid, wireHist, ru, /*histStep=*/0.0, HistMode::kMultiplicative,
                     /*histFactor=*/3.0, /*streakThreshold=*/1, /*scaleByOverflow=*/true,
                     /*relaxEnabled=*/true, 1.0, 0.0);
  check(std::abs(wireHist.histH[idx0] - 13.0 / 3.0) < 1e-9,
        "historyWireH grows to exactly 1*(1+(3-1)*(5/3))=13/3 after round 0's "
        "boundary update (usage/cap ratio 5/3, multiplicative factor 3.0) -- "
        "the real production updateWireHistory function, not a stand-in");
  check(wireHist.histH[idx1] == 1.0, "layer 1's own historyWireH never grew (sanity)");

  // Round 1: reprocess all 5 (all still touch the now-overflowed edge),
  // historyWireH held at 13/3 for the whole round. Hand-derived: nets 1-3
  // flip to layer 1 (hist-scaled ratio 65/9, 52/9, 13/3 all exceed 11/3);
  // nets 4-5 stay on layer 0 (26/9 < 11/3). This is the decisive claim:
  // round 0 (any order) could never produce this, since round 0's own
  // ratio never came close to 11/3.
  int flips = 0;
  for (int i = 0; i < 5; ++i) {
    ru.h[idx0] -= 1; // undo this net's own prior layer-0 charge
    bool pickedLayer0 = runOne();
    if (pickedLayer0)
      ru.h[idx0] += 1;
    else
      ++flips;
  }
  check(flips == 3, "exactly 3 of the 5 nets flip to layer 1 in round 1");
  check(ru.h[idx0] == 2,
        "round 1 reduces layer 0's usage from 5 to 2 -- zero overflow (capacity 3) -- "
        "something no single pass, at any net order, could ever produce");
}

// Direct ports of nv1route's own two decisive via-history relaxation
// tests, applied to updateWireHistory/WirePrices instead of
// updateViaHistory/ViaPrices -- same mechanism, new domain.
void testWireHistoryRelaxDisabledReproducesPermanentBehavior() {
  Design d = parse("nv2route_selftest_wirehist_permanent.gr", kModerateViaGr);
  WirePrices wp(d.grid);
  size_t idx = d.grid.hIndex(0, 0, 0);

  RealUsage over(d.grid);
  over.h[idx] = 100; // comfortably over whatever capacity is declared there
  updateWireHistory(d.grid, wp, over, 1.0, HistMode::kAdditive, 1.0, 1, false,
                     /*relaxEnabled=*/false);
  double afterOver = wp.histH[idx];
  check(afterOver > 1.0, "historyWireH grew while the edge was over capacity");

  RealUsage under(d.grid);
  under.h[idx] = 0; // now comfortably clear
  updateWireHistory(d.grid, wp, under, 1.0, HistMode::kAdditive, 1.0, 1, false,
                     /*relaxEnabled=*/false);
  check(wp.histH[idx] == afterOver,
        "with NV2ROUTE_WIRE_HIST_RELAX=0, historyWireH never decreases even "
        "after usage drops back under capacity -- reproduces iroute's exact "
        "permanent behavior, the fallback this mechanism can always be "
        "switched off back to, same as historyVia's own relax test");
}

void testWireHistoryRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("nv2route_selftest_wirehist_relax.gr", kModerateViaGr);
  WirePrices wp(d.grid);
  size_t idx = d.grid.hIndex(0, 0, 0);

  RealUsage over(d.grid);
  over.h[idx] = 100;
  updateWireHistory(d.grid, wp, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(wp.histH[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows historyWireH to exactly 1.05");

  RealUsage under(d.grid);
  under.h[idx] = 0;
  updateWireHistory(d.grid, wp, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(wp.histH[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- historyWireH "
        "is back to the 1.0 no-op baseline, proving the LIFO stack genuinely "
        "drives this, not just growth being disabled");
}

// NV2ROUTE_WIRE_AWARE=0 must reproduce nv1route's exact output, segment for
// segment -- the regression-continuity test every generation in this
// lineage carries, this file's own version for the new mechanism.
// 5 real, independent 2-pin nets, each with the identical pair of pins --
// each net's own 2D route (a straight, deterministic, no-maze-search leg,
// route()'s own simplest case) lands on the exact same single horizontal
// tile-edge, reproducing kWireContentionGr's own hand-derived scenario
// through the REAL end-to-end pipeline (route() -> negotiateViaAssignment())
// instead of hand-rolled layerAssignDP calls. 2D-collapsed capacity here is
// 4+2=6 against 5 nets' demand, so the 2D stage itself sees no overflow at
// all -- this is purely a real-per-layer distribution problem, exactly the
// gap this generation exists to close. Capacities are deliberately more
// modest than kWireContentionGr's own (which relied on layer 1 never
// actually being committed to -- fine for a hand-rolled DP-only test, but
// a real commit charges layer 1's own declared capacity too, so this
// fixture keeps layer 1 physically able to hold one real net's worth of
// traffic without a separate, larger overflow appearing there instead).
const char *kFiveNetWireContentionGr = R"(grid 4 4 2
vertical capacity 3 3
horizontal capacity 4 2
minimum width 1 1
minimum spacing 0 2
via spacing 10 10
0 0 10 10
num net 5
net0 0 2 1
0 0 1
10 0 1
net1 1 2 1
0 0 1
10 0 1
net2 2 2 1
0 0 1
10 0 1
net3 3 2 1
0 0 1
10 0 1
net4 4 2 1
0 0 1
10 0 1
0
)";

// NV2ROUTE_WIRE_AWARE=0 must reproduce nv1route's own structural
// limitation exactly (no memory of wire congestion, so every net keeps
// picking the same layer forever) -- and, on the same real fixture with
// wire-awareness on and a fast-growing history, at least one net's real
// layer choice must differ, proving the flag genuinely changes the real
// end-to-end outcome rather than being an inert switch. (Round cap 1 for
// the "on" case: this fixture's own capacities are tight enough that a
// later round would relax the very history that caused the first round's
// flip and negotiate it back -- an real, honest dynamic, not a bug, but
// this test's own job is only to prove the mechanism is *not inert*, so it
// looks at the first round's own decisive effect rather than long-run
// convergence, which the real-benchmark run is what actually settles.)
void testWireAwareOffReproducesNv1routesLimitAndIsNotADeadFlag() {
  Design dOff = parse("nv2route_selftest_wireoff.gr", kFiveNetWireContentionGr);
  setenv("NV2ROUTE_WIRE_AWARE", "0", 1);
  RunReport repOff = route(dOff, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dOff.grid, repOff.nets);
  unsetenv("NV2ROUTE_WIRE_AWARE");
  for (const RoutedNet &rn : repOff.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2))
        check(s.l1 == 0, "with wire-awareness off, every net's wire leg stays on "
                          "layer 0 -- the same round-0 decision repeated forever, "
                          "nv1route's own exact structural limitation");

  Design dOn = parse("nv2route_selftest_wireon.gr", kFiveNetWireContentionGr);
  RunReport repOn = route(dOn, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  HistKnobs fastWireGrowth{/*step=*/0.0, HistMode::kMultiplicative, /*factor=*/3.0,
                            /*streakThreshold=*/1,   /*scaleByOverflow=*/true,
                            /*relaxEnabled=*/true,   /*ratioExponent=*/1.0,
                            /*exponentEscalation=*/0.0};
  negotiateViaAssignment(dOn.grid, repOn.nets, /*maxRounds=*/1, 20, 1.0, HistKnobs(),
                          fastWireGrowth);
  bool sawLayer1 = false;
  for (const RoutedNet &rn : repOn.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2) && s.l1 == 1)
        sawLayer1 = true;
  check(sawLayer1,
        "the SAME fixture, wire-awareness on with a fast-growing history, "
        "moves at least one net's real wire leg to layer 1 after just one "
        "negotiation round -- the flag genuinely changes the real "
        "end-to-end outcome, not a dead switch");
}

// This file's own decisive claim: a giant net's fixed-corner initial
// commit (routeGiantNet always routes horizontal-then-vertical) can be
// stuck permanently contesting an edge that an alternate corner
// (vertical-then-horizontal) would avoid entirely -- and letting the
// giant net into the fast tier's own price-aware corner choice, not
// merely running more rounds, is what escapes it.
//
// Fixture: a 4x2x1 grid, horizontal capacity 1 (tight), vertical capacity
// 10 (generous, never the bottleneck). "giant" has 1006 raw pins --
// >kGiantNetPins, so `n.pins.size() > kGiantNetPins` is genuinely true,
// not just close -- but only two *unique* points after dedup: 1005
// duplicate pins at (0,0) and one real pin at (3,1), so the routed
// topology itself stays a single trivial two-point leg. routeGiantNet's
// own fixed corner for that leg is horizontal along y=0 from x=0 to 3,
// then vertical along x=3 from y=0 to 1 -- crossing edge (1,0)-(2,0).
// "blocker" is a plain two-pin net occupying exactly that same edge
// (pins at (1,0) and (2,0)), so with capacity 1, both nets sharing it is
// a real overflow of 1, permanently, unless something reroutes. The
// untested alternate corner (vertical along x=0 from y=0 to 1, then
// horizontal along y=1 from x=0 to 3) shares no edge with blocker at all.
void testGiantNetJoinsFastTierAndEscapesAPermanentlyCongestedCorner() {
  std::string gr = "grid 4 2 1\n"
                    "vertical capacity 10\n"
                    "horizontal capacity 1\n"
                    "minimum width 1\n"
                    "minimum spacing 0\n"
                    "via spacing 0\n"
                    "0 0 1 1\n"
                    "num net 2\n";
  const int giantPins = 1006; // > kGiantNetPins (1000)
  gr += "giant 1 " + std::to_string(giantPins) + " 1\n";
  for (int i = 0; i < giantPins - 1; ++i)
    gr += "0 0 1\n";
  gr += "3 1 1\n";
  gr += "blocker 2 2 1\n";
  gr += "1 0 1\n";
  gr += "2 0 1\n";
  gr += "0\n";

  Design d = parse("nv3route_selftest_giant.gr", gr.c_str());
  check(d.nets[0].pins.size() > kGiantNetPins,
        "the fixture's giant net genuinely exceeds the giant-net pin threshold");

  setenv("NV3ROUTE_GIANT_FAST_TIER", "0", 1);
  RunReport frozen = route(d, 5, 0, 0.02, 1.0, HistMode::kAdditive, 1.0);
  unsetenv("NV3ROUTE_GIANT_FAST_TIER");
  check(frozen.bestOverflow == 1,
        "with the giant net excluded from the fast tier (NV3ROUTE_GIANT_FAST_TIER=0, "
        "reproducing nv2route's exact prior behavior), it stays frozen on "
        "routeGiantNet's own fixed corner forever, permanently contesting "
        "blocker's edge -- overflow never resolves");

  RunReport joined = route(d, 5, 0, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(joined.bestOverflow == 0,
        "with the giant net included in the fast tier (this file's own new "
        "default), it discovers the untested alternate corner under real "
        "price pressure and moves off blocker's edge entirely -- the flag "
        "genuinely changes the real end-to-end outcome, not a dead switch");
}

// This file's own real change over nv3route is entirely two compiled-in
// default values (VROUTE_THRESHOLD 0.2->0.05, VROUTE_POWER 2.5->1.5) --
// no new control flow. Both `power` inside priceExponentTerm and the
// `threshold` returned by defaultThreshold() are cached in a function-
// local `static const double`, latched permanently on that function's
// first call in this process (see priceExponentTerm/defaultThreshold's
// own definitions) -- so a test cannot toggle them mid-process via
// setenv/unsetenv the way testGiantNetJoinsFastTierAndEscapesA... toggles
// NV3ROUTE_GIANT_FAST_TIER (read fresh via std::getenv on every call,
// never cached). This test instead calls the real, unmodified
// priceExponentTerm/defaultThreshold functions directly and checks their
// output against an independently hand-computed reference for nv3route's
// OLD defaults at the same usage/capacity ratio -- proving both new
// defaults are wired through to the real formula and that the curve they
// produce is materially different from nv3route's own, not merely a
// cosmetic renumbering.
void testRetunedCoefficientsProduceAMateriallyDifferentPricingCurve() {
  const double usage = 0.6, cap = 1.0; // ratio 0.6, comfortably over both thresholds
  double threshold = defaultThreshold();
  check(threshold == 0.05, "this file's own default VROUTE_THRESHOLD is 0.05, not "
                            "nv3route's 0.2 -- defaultThreshold() returns the value "
                            "this file actually compiled in");

  double newCurve = priceExponentTerm(usage, cap, threshold);
  // Hand-computed for threshold=0.05, power=1.5 (this file's own defaults):
  //   r = (0.6 - 0.05) / (1.0 - 0.05) = 0.55 / 0.95
  //   result = r^1.5
  double expectedNew = std::pow((0.6 - 0.05) / (1.0 - 0.05), 1.5);
  check(std::abs(newCurve - expectedNew) < 1e-9,
        "priceExponentTerm(0.6, 1.0, defaultThreshold()) matches the hand-computed "
        "threshold=0.05/power=1.5 formula exactly -- the real production function, "
        "not a copy of it");

  // nv3route's own OLD defaults (threshold=0.2, power=2.5) at the identical
  // ratio, computed independently (not through priceExponentTerm, whose
  // cached `power` static is now permanently 1.5 for this process) --
  // this is the reference this file's own real behavioral change is
  // measured against.
  double oldCurve = std::pow((0.6 - 0.2) / (1.0 - 0.2), 2.5);
  check(std::abs(newCurve - oldCurve) > 0.2,
        "the retuned curve differs from nv3route's own old-default curve by a "
        "large, unmistakable margin at the same usage/capacity ratio (0.44 vs. "
        "0.18) -- a real change to how hard the fast tier prices moderate "
        "congestion, not a rounding-level tweak");
}

// This file's own mechanism: the decisive claim, on the real search. A
// 40x40 grid; source tile (5,20), sink tile (15,20). Every horizontal edge
// crossing from x=10 to x=11 is priced at 1e6 (a wall across the direct
// route) except the one at y=5 (price 1, a gap 15 rows off the pins'
// shared row). The pins' bounding box is rows 20..20, so margin 6 searches
// rows 14..26 -- the gap is outside, a path still exists (through the
// wall), so the no-path doubling never fires and the search crosses the
// wall. Margin 16 searches rows 4..36, which contains the gap: the detour
// (~30 extra tiles at price ~1) is far cheaper than one 1e6 edge.
void testWiderBoxFindsDetourFixedMarginCannot() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 0 0\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnetW 0 2 1\n55 205 1\n155 205 1\n0\n";
  Design d = parse("nv11route_selftest_wall.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;
  for (int y = 0; y < g.ny; ++y)
    pr.h[g.hIndex(10, y, 0)] = (y == 5) ? 1.0 : 1e6;

  std::vector<PathNode> sources = {{5, 20, -1}};
  std::vector<Point> sinks = {{15, 20}};
  auto wallCrossingRow = [&](const MSMVResult &r) {
    for (size_t i = 1; i < r.path.size(); ++i) {
      const PathNode &a = r.path[i - 1], &b = r.path[i];
      if (a.y == b.y && std::min(a.x, b.x) == 10 && std::max(a.x, b.x) == 11)
        return a.y;
    }
    return -1;
  };

  MSMVResult narrow = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, /*marginOverride=*/6);
  check(narrow.found, "margin 6 still finds a path (through the wall) -- the doubling retry never fires");
  int narrowRow = wallCrossingRow(narrow);
  check(narrowRow >= 14 && narrowRow <= 26,
        "with margin 6 the route crosses the wall inside rows 14..26 (at a 1e6 edge) -- "
        "the cheap gap at row 5 is outside the search box");

  MSMVResult wide = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, /*marginOverride=*/16);
  check(wide.found, "margin 16 finds a path");
  check(wallCrossingRow(wide) == 5,
        "with margin 16 the route detours through the cheap gap at row 5 -- the "
        "structural limit a small fixed box imposes, removed by a wider one");

  MSMVResult dflt = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(wallCrossingRow(dflt) == narrowRow,
        "no override (-1) behaves exactly like margin 6, VROUTE_MAX_MARGIN's default -- "
        "nv4route's behavior is unchanged when the mechanism is off");
}

// This file's own mechanism: the growth arithmetic, hand-verified,
// including the cap clamp and the long-long guard against int overflow on
// absurd round counts.
void testBoxGrowthArithmetic() {
  BoxGrowth b;
  b.mode = BoxMode::Round;
  b.init = 24;
  b.grow = 1;
  b.cap = 30;
  check(boxMarginForRound(b, 0) == 24, "round 0 -> init (24)");
  check(boxMarginForRound(b, 5) == 29, "round 5 -> 24 + 5*1 = 29");
  check(boxMarginForRound(b, 6) == 30, "round 6 -> 30, exactly the cap");
  check(boxMarginForRound(b, 100) == 30, "round 100 -> clamped at cap 30");
  b.grow = 1000000;
  check(boxMarginForRound(b, INT_MAX) == 30,
        "an absurd round*grow product that would overflow int is still clamped at cap");
  b.grow = 0;
  check(boxMarginForRound(b, 50) == 24, "grow 0 -> margin stays at init forever");

  BoxGrowth p;
  p.mode = BoxMode::PerNet;
  p.init = 6;
  p.grow = 6;
  p.cap = 20;
  check(grownBoxMargin(p, 6) == 12 && grownBoxMargin(p, 12) == 18, "6 -> 12 -> 18 by +6 steps");
  check(grownBoxMargin(p, 18) == 20 && grownBoxMargin(p, 20) == 20, "18 -> 20 (cap), then stays at 20");
}

// This file's own mechanism: Phase B's per-search margin decisions, via the
// same two helpers route()'s own loop calls.
void testBoxMarginModesInPhaseBHelpers() {
  BoxGrowth fixed;
  RoutedNet a;
  assignBoxMarginForSearch(fixed, 7, a);
  growBoxMarginIfStillOverflowed(fixed, true, a);
  check(a.boxMargin == -1,
        "Fixed mode never touches boxMargin (stays -1, i.e. VROUTE_MAX_MARGIN) -- nv4route parity");

  BoxGrowth round;
  round.mode = BoxMode::Round;
  round.init = 10;
  round.grow = 2;
  round.cap = 100;
  RoutedNet b;
  assignBoxMarginForSearch(round, 3, b);
  check(b.boxMargin == 16, "Round mode, round 3: 10 + 3*2 = 16");
  growBoxMarginIfStillOverflowed(round, true, b);
  check(b.boxMargin == 16, "Round mode ignores per-net overflow -- growth is by round only");

  BoxGrowth per;
  per.mode = BoxMode::PerNet;
  per.init = 6;
  per.grow = 6;
  per.cap = 96;
  RoutedNet c;
  assignBoxMarginForSearch(per, 0, c);
  check(c.boxMargin == 6, "PerNet mode: first search starts at init (6)");
  growBoxMarginIfStillOverflowed(per, true, c);
  assignBoxMarginForSearch(per, 1, c);
  check(c.boxMargin == 12, "still overflowed after round 0 -> 12 for round 1, and assign keeps it");
  growBoxMarginIfStillOverflowed(per, false, c);
  assignBoxMarginForSearch(per, 2, c);
  check(c.boxMargin == 12, "resolved after round 1 -> no growth; stays 12");
}

// This file's own mechanism: bad input is rejected, never silently
// clamped (an unintended margin quietly changes quality and runtime).
void testParseBoxGrowthRejectsBadInput() {
  BoxGrowth b;
  std::string err;
  check(parseBoxGrowth(nullptr, nullptr, nullptr, nullptr, b, err) && b.mode == BoxMode::Fixed,
        "no env vars -> Fixed mode (nv4route parity by default)");
  check(parseBoxGrowth("pernet", "6", "6", "96", b, err) && b.mode == BoxMode::PerNet &&
            b.init == 6 && b.grow == 6 && b.cap == 96,
        "valid pernet settings parse exactly");
  check(parseBoxGrowth("round", "24", "1", "120", b, err) && b.mode == BoxMode::Round,
        "valid round settings parse");
  check(!parseBoxGrowth("wide", nullptr, nullptr, nullptr, b, err), "unknown mode rejected");
  check(!parseBoxGrowth("pernet", "0", "6", "96", b, err), "init 0 rejected");
  check(!parseBoxGrowth("pernet", "-3", "6", "96", b, err), "negative init rejected");
  check(!parseBoxGrowth("pernet", "6", "-1", "96", b, err), "negative grow rejected");
  check(!parseBoxGrowth("pernet", "24", "6", "12", b, err), "cap below init rejected");
  check(!parseBoxGrowth("pernet", "12abc", "6", "96", b, err), "trailing garbage rejected");
  check(!parseBoxGrowth("pernet", "", "6", "96", b, err), "empty string rejected");
  check(!parseBoxGrowth("pernet", "6", "6", "99999999999", b, err), "out-of-int-range rejected");
}

// ---------------------------------------------------------------------------
// This file's own mechanism: present-demand pricing (NTHU-Route 2.0's cost).
// ---------------------------------------------------------------------------

// The factor schedule, against values computed independently (Python,
// 1 - exp(-5 exp(-0.1 i))), not through pdFactorForIter.
void testPdFactorScheduleMatchesNthu() {
  check(std::abs(pdFactorForIter(1) - 0.9891564743369182) < 1e-12, "factor(1) = 0.98916 (NTHU iteration 1)");
  check(std::abs(pdFactorForIter(20) - 0.49169642239602784) < 1e-12, "factor(20) = 0.49170");
  check(std::abs(pdFactorForIter(50) - 0.03312855552217531) < 1e-12, "factor(50) = 0.03313");
  check(std::abs(pdFactorForIter(80) - 0.0016759072359852079) < 1e-12, "factor(80) = 0.00168");
  bool decreasing = true;
  for (int i = 1; i < 300; ++i)
    decreasing = decreasing && pdFactorForIter(i + 1) < pdFactorForIter(i) && pdFactorForIter(i + 1) > 0.0;
  check(decreasing, "factor strictly decreases and stays > 0 over iterations 1..300");
}

// NTHU's formula, hand-computed. (a) fresh edge: demand 4 + one net of
// amount 2 over cap 10 -> cong 0.6, cost = wl + 1 * 0.6^5 = wl + 0.07776.
// (b) hist 3 at iter 4, factor 0.5: shrink = 1 - 2 / (4 * 3) = 5/6, so
// cong = 6 / (10 * 5/6) = 0.72 and cost = 0.5 + 3 * 0.72^5 = 1.0804752896.
void testPresentDemandCostHandComputed() {
  double f1 = pdFactorForIter(1);
  check(std::abs(presentDemandCost(4, 10, 2, 1, 1, f1, f1) - (f1 + 0.07776)) < 1e-12,
        "fresh edge: cost = WL + (demand + amount) / cap ^ 5 = factor + 0.07776");
  check(std::abs(pdShrink(3, 4, 0.5) - 5.0 / 6.0) < 1e-12, "history 3 at iteration 4 shrinks capacity to 5/6");
  check(std::abs(presentDemandCost(4, 10, 2, 3, 4, 0.5, 0.5) - 1.0804752895999998) < 1e-12,
        "history multiplies AND shrinks: 0.5 + 3 * 0.72^5 = 1.08048");
  check(presentDemandCost(20, 10, 2, 1, 1, f1, f1) - f1 > 600.0 * (presentDemandCost(4, 10, 2, 1, 1, f1, f1) - f1),
        "the congestion term at 2.2x capacity is over 600x a 60%-full edge's (2.2^5 / 0.6^5 = 663: the ^5 steepness)");
}

// CLAUDE.md's arithmetic edge cases for this formula: zero and negative
// capacity (a .gr adjustment can zero an edge) must give a finite
// deterrent, never inf/NaN from the division.
void testPdCostZeroCapacityIsForbiddenAndFinite() {
  double f = pdFactorForIter(1);
  check(presentDemandCost(0, 0, 2, 1, 1, f, f) == kForbiddenPrice, "capacity 0 -> kForbiddenPrice");
  check(presentDemandCost(5, -3, 2, 1, 1, f, f) == kForbiddenPrice, "negative capacity -> kForbiddenPrice");
  double huge = presentDemandCost(1000000000LL, 1, 2, 1000000, 1, f, f);
  check(std::isfinite(huge) && huge == kForbiddenPrice,
        "absurd demand and history clamp to kForbiddenPrice, still finite");
}

// The shrink invariant: an edge overflowed at every round boundary gains
// history exactly as fast as the iteration counter, never faster, so the
// shrink stays strictly above the 1/3 floor -- the floor never binds in a
// real run. And the floor itself: if the invariant were ever broken (history
// far above the iteration), the shrink clamps at 1/3 instead of reaching 0
// or going negative.
void testPdHistoryInvariantKeepsShrinkAboveFloor() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t e = g.hIndex(1, 0, 0);
  u.h[e] = u.capH[e] + 5; // overflowed at every boundary
  bool invariant = true, aboveFloor = true;
  for (int r = 0; r < 500; ++r) {
    advancePresentDemand(pr, u);
    invariant = invariant && pr.pdHistH[e] <= pr.pdIter && pr.pdHistH[e] == pr.pdIter;
    aboveFloor = aboveFloor && pdShrink(pr.pdHistH[e], pr.pdIter, pr.pdFactor) > kPdMinShrink;
  }
  check(invariant, "a permanently overflowed edge's history tracks the iteration exactly (hist <= iter)");
  check(aboveFloor, "so its shrink stays strictly above the 1/3 floor for 500 iterations");
  size_t clean = g.hIndex(2, 0, 0);
  check(pr.pdHistH[clean] == 1, "a never-overflowed edge keeps history 1");
  check(pdShrink(1000, 1, 1.0) == kPdMinShrink, "a broken invariant clamps at 1/3, not 0 or negative");
  check(std::isfinite(presentDemandCost(10, 10, 2, 1000, 1, 1.0, 1.0)), "and the cost stays finite");
}

// The mechanism's point: under present-demand pricing, taking a net off an
// edge makes the edge exactly as cheap as it was before the net arrived.
// Under the inherited lambda model the same charge/undo leaves the edge
// permanently dearer (negotiateWire only ever raises lambda).
void testPdCostFallsTheMomentDemandLeaves() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  std::vector<PathNode> path = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
  size_t e = g.hIndex(1, 0, 0);
  auto chargeTwiceThenUndo = [&](CostModel model, double &before, double &during, double &after) {
    Prices pr(g);
    Usage u(g);
    CostSettings cs;
    cs.model = model;
    configureCostModel(pr, u, cs);
    before = wireCostH(pr, e, 1);
    std::vector<Seg> segs;
    std::vector<ChargeRecord> charges;
    for (int k = 0; k < 3; ++k) // 3 nets on a capacity-2 edge: overflowed
      commitPath(g, pr, u, 0.5, 1, path, segs, charges);
    during = wireCostH(pr, e, 1);
    for (const ChargeRecord &c : charges)
      undoCharge(g, u, c);
    after = wireCostH(pr, e, 1);
  };
  double pb, pd, pa, lb, ld, la;
  chargeTwiceThenUndo(CostModel::kPresentDemand, pb, pd, pa);
  chargeTwiceThenUndo(CostModel::kLambda, lb, ld, la);
  check(pd > 10.0 * pb, "present demand: an overflowed edge is far dearer while the nets sit on it");
  check(pa == pb, "present demand: once they leave, the cost is exactly what it was before");
  check(ld > lb, "lambda (inherited): the charge raises the price too");
  check(la > lb, "lambda (inherited): but it stays raised after the nets leave -- the difference this file tests");
}

// Under kLambda (route()'s default, PD1ROUTE_COST=lambda) the cost seam and
// heuristic scale are the inherited ones exactly -- the in-code half of the
// byte-parity check against nv11route.
void testLambdaModeIsTheInheritedCostExactly() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  pr.h[g.hIndex(1, 0, 0)] = 3.7;
  pr.histH[g.hIndex(1, 0, 0)] = 1.9;
  pr.v[g.vIndex(0, 1, 0)] = 2.2;
  bool same = true;
  for (size_t e = 0; e < pr.h.size(); ++e)
    same = same && wireCostH(pr, e, 2) == edgeCost(pr.h[e], pr.histH[e], 2);
  for (size_t e = 0; e < pr.v.size(); ++e)
    same = same && wireCostV(pr, e, 2) == edgeCost(pr.v[e], pr.histV[e], 2);
  check(same, "kLambda: every edge's cost is edgeCost(lambda, hist, amount), bit for bit");
  check(heuristicScale(pr) == 1.0, "kLambda: A* heuristic scale is exactly 1 (the inherited heuristic)");
}

// A* stays exact under present-demand pricing. Late in NTHU's schedule the
// WL term is ~0.0017, so an edge can cost far below 1 and the inherited
// Manhattan heuristic (scale 1) overestimates. heuristicScale() multiplies
// it by the WL term, a true lower bound. On 30 hash-priced fixtures (random
// demand, many edges overflowed, random history <= iteration), the path
// multiSourceDijkstra returns must cost exactly what an in-test reference
// Dijkstra (no heuristic) finds. The fixtures mimic that late state: ~90%
// of edges well under capacity (cost well below 1), ~10% hot spots over
// it with history. Then the teeth: with the scale forced back
// to 1, at least one fixture must come back strictly costlier -- proving
// the scaling is needed, not decorative.
void testAStarStaysExactUnderPdPricing() {
  std::string gr = "grid 30 30 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd1route_selftest_astar.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  auto hsh = [](unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
  };
  auto pathCost = [&](const Prices &pr, const std::vector<PathNode> &p) {
    double c = 0;
    for (size_t i = 1; i < p.size(); ++i) {
      if (p[i].y == p[i - 1].y)
        c += wireCostH(pr, g.hIndex(std::min(p[i].x, p[i - 1].x), p[i].y, 0), 2);
      else
        c += wireCostV(pr, g.vIndex(p[i].x, std::min(p[i].y, p[i - 1].y), 0), 2);
    }
    return c;
  };
  auto reference = [&](const Prices &pr, const std::vector<PathNode> &src, const std::vector<Point> &snk) {
    int n = g.nx * g.ny;
    std::vector<double> dist(n, std::numeric_limits<double>::infinity());
    using QE = std::pair<double, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    for (const PathNode &s : src) {
      dist[s.y * g.nx + s.x] = 0;
      q.push({0.0, s.y * g.nx + s.x});
    }
    while (!q.empty()) {
      double dd = q.top().first;
      int i = q.top().second;
      q.pop();
      if (dd > dist[i])
        continue;
      int x = i % g.nx, y = i / g.nx;
      auto relax = [&](int nx2, int ny2, double w) {
        int j = ny2 * g.nx + nx2;
        if (dd + w < dist[j]) {
          dist[j] = dd + w;
          q.push({dist[j], j});
        }
      };
      if (x > 0) relax(x - 1, y, wireCostH(pr, g.hIndex(x - 1, y, 0), 2));
      if (x + 1 < g.nx) relax(x + 1, y, wireCostH(pr, g.hIndex(x, y, 0), 2));
      if (y > 0) relax(x, y - 1, wireCostV(pr, g.vIndex(x, y - 1, 0), 2));
      if (y + 1 < g.ny) relax(x, y + 1, wireCostV(pr, g.vIndex(x, y, 0), 2));
    }
    double best = std::numeric_limits<double>::infinity();
    for (const Point &s : snk)
      best = std::min(best, dist[s.y * g.nx + s.x]);
    return best;
  };

  int exact = 0, unscaledWorse = 0, overflowedEdges = 0;
  const int kFixtures = 30;
  for (int f = 0; f < kFixtures; ++f) {
    Prices pr(g);
    Usage u(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    pr.pdIter = 80;
    pr.pdFactor = pdFactorForIter(80);
    auto fill = [&](unsigned salt, int &demand, int &hist) {
      unsigned a = hsh(f * 7919 + salt), b = hsh(f * 104729 + salt);
      bool hot = a % 10 == 0;
      demand = hot ? 25 + static_cast<int>((a >> 8) % 16) : static_cast<int>((a >> 8) % 15);
      hist = hot ? 1 + static_cast<int>(b % 80) : 1;
    };
    for (size_t e = 0; e < u.h.size(); ++e) {
      fill(static_cast<unsigned>(e * 2 + 1), u.h[e], pr.pdHistH[e]);
      overflowedEdges += u.h[e] > u.capH[e];
    }
    for (size_t e = 0; e < u.v.size(); ++e)
      fill(static_cast<unsigned>(e * 2), u.v[e], pr.pdHistV[e]);
    unsigned r = hsh(f + 12345);
    std::vector<PathNode> src = {{static_cast<int>(r % 30), static_cast<int>((r >> 8) % 30), -1}};
    std::vector<Point> snk = {{static_cast<int>((r >> 16) % 30), static_cast<int>((r >> 24) % 30)},
                              {static_cast<int>(hsh(r) % 30), static_cast<int>((hsh(r) >> 8) % 30)}};
    if (snk[0].x == src[0].x && snk[0].y == src[0].y)
      snk[0].x = (snk[0].x + 7) % 30;
    if (snk[1].x == src[0].x && snk[1].y == src[0].y)
      snk[1].y = (snk[1].y + 11) % 30;
    double ref = reference(pr, src, snk);
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, /*margin=*/60);
    if (a.found && std::abs(pathCost(pr, a.path) - ref) <= 1e-9 * std::max(1.0, ref))
      ++exact;
    pr.heuristicScaleOverride = 1.0;
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, /*margin=*/60);
    if (b.found && pathCost(pr, b.path) > ref * (1.0 + 1e-9) + 1e-12)
      ++unscaledWorse;
  }
  check(overflowedEdges > 1000, "fixtures are genuinely congested (over 1000 overflowed edges across them)");
  check(exact == kFixtures, "scaled heuristic: A* finds the true minimum cost on all 30 fixtures (" +
                                std::to_string(exact) + "/30)");
  check(unscaledWorse >= 1, "unscaled heuristic returns a strictly costlier path on " +
                                std::to_string(unscaledWorse) + " fixture(s) -- the scaling is load-bearing");
}

// End to end: three nets between the same two tiles of one row, which
// holds one net per edge. Only detours through the rows above and below
// fit all three; the fast tier's patterns for a same-row pair can't make
// them. route() under present-demand pricing must reach zero 2D overflow.
void testPdRouteReachesZeroOnADetourOnlyFixture() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd1route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0,
                        true, true, true, 8, BoxGrowth{}, cs);
  check(rep.bestOverflow == 0, "present-demand pricing reaches zero 2D overflow (got " +
                                   std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "and every net's route is connected");
}

void testParseCostSettings() {
  CostSettings c;
  std::string err;
  check(parseCostSettings(nullptr, nullptr, c, err) && c.model == CostModel::kPresentDemand && c.wlFloor == 0.0,
        "no env vars -> present-demand pricing, NTHU's WL term (floor 0)");
  check(parseCostSettings("lambda", nullptr, c, err) && c.model == CostModel::kLambda, "lambda parses");
  check(parseCostSettings("pd", "1", c, err) && c.model == CostModel::kPresentDemand && c.wlFloor == 1.0,
        "pd with floor 1 parses exactly");
  check(parseCostSettings("pd", "0.25", c, err) && c.wlFloor == 0.25, "fractional floor parses");
  check(!parseCostSettings("nthu", nullptr, c, err), "unknown model rejected");
  check(!parseCostSettings("pd", "-1", c, err), "negative floor rejected");
  check(!parseCostSettings("pd", "abc", c, err), "non-numeric floor rejected");
  check(!parseCostSettings("pd", "1x", c, err), "trailing garbage rejected");
  check(!parseCostSettings("pd", "", c, err), "empty floor rejected");
  check(!parseCostSettings("pd", "nan", c, err), "NaN floor rejected");
  check(!parseCostSettings("pd", "inf", c, err), "infinite floor rejected");
}

// ---------------------------------------------------------------------------
// pd2route's own mechanisms: cong^5 by multiplication, and the acceptance rule.
// ---------------------------------------------------------------------------

// The multiplication must agree with the std::pow formula pd1route used,
// across the whole range a run sees: empty to 3x-overflowed edges, history
// 1..iter, early and late iterations, with and without the capacity shrink.
void testPdCongPowerByMultiplicationMatchesPow() {
  double worst = 0.0;
  int cases = 0;
  for (int cap : {2, 7, 20, 90, 450})
    for (int demand = 0; demand <= 3 * cap; demand += std::max(1, cap / 7))
      for (int iter : {1, 5, 31, 80, 150})
        for (int hist : {1, 2, iter / 2 + 1, iter}) {
          double f = pdFactorForIter(iter);
          double cong = static_cast<double>(demand + 2) / (static_cast<double>(cap) * pdShrink(hist, iter, f));
          double ref = std::min(kForbiddenPrice, f + hist * std::pow(cong, 5.0));
          double got = presentDemandCost(demand, cap, 2, hist, iter, f, f);
          worst = std::max(worst, std::abs(got - ref) / std::max(1.0, std::abs(ref)));
          ++cases;
        }
  check(cases > 1000, "power check covers " + std::to_string(cases) + " cost evaluations");
  check(worst <= 1e-13, "cong^5 by multiplication matches std::pow(cong, 5) to 1e-13 relative everywhere");
}

// The whole acceptance rule, both modes, every ordering of before/after.
void testRevertRerouteDecision() {
  check(revertReroute(AcceptMode::kRevert, 5, 6), "revert: a locally worse reroute is undone");
  check(!revertReroute(AcceptMode::kRevert, 5, 5), "revert: an equal one is kept (inherited rule: undo only if worse)");
  check(!revertReroute(AcceptMode::kRevert, 5, 4), "revert: a better one is kept");
  check(!revertReroute(AcceptMode::kKeep, 5, 6), "keep: a locally worse reroute is kept (NTHU's rule)");
  check(!revertReroute(AcceptMode::kKeep, 5, 5) && !revertReroute(AcceptMode::kKeep, 5, 4),
        "keep: equal and better reroutes are kept too");
}

// End to end under keep: the detour-only fixture (three nets, one row that
// holds one) must still reach zero with no reroute ever undone.
void testKeepModeReachesZeroOnTheDetourOnlyFixture() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd2route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0,
                        true, true, true, 8, BoxGrowth{}, cs, AcceptMode::kKeep);
  check(rep.bestOverflow == 0, "keep + present demand reaches zero 2D overflow (got " +
                                   std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "and every net's route is connected");
}

void testParseAcceptMode() {
  AcceptMode m = AcceptMode::kKeep;
  std::string err;
  check(parseAcceptMode(nullptr, m, err) && m == AcceptMode::kRevert, "unset -> revert (inherited rule)");
  check(parseAcceptMode("keep", m, err) && m == AcceptMode::kKeep, "keep parses");
  check(parseAcceptMode("revert", m, err) && m == AcceptMode::kRevert, "revert parses");
  check(!parseAcceptMode("always", m, err), "unknown value rejected");
  check(!parseAcceptMode("", m, err), "empty value rejected");
}

// ---------------------------------------------------------------------------
// pd3route's own mechanism: NTHU-style layer assignment.
// ---------------------------------------------------------------------------

// An L-shaped two-leg tree (3 tiles east, then 2 tiles north) splits into 5
// unit edges over 6 nodes, still a tree, covering exactly the same tiles.
void testSplitToUnitEdgesKeepsTheTreeAndItsTiles() {
  std::vector<Seg> segs = {{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}};
  NetTree legs, unit;
  check(buildNetTree(segs, legs) && legs.edges.size() == 2, "two-leg tree builds (sanity)");
  splitToUnitEdges(legs, unit);
  bool allUnit = true;
  for (const NetTree::Edge &e : unit.edges)
    allUnit = allUnit && e.hi - e.lo == 1 &&
              std::abs(unit.nodes[e.a].x - unit.nodes[e.b].x) + std::abs(unit.nodes[e.a].y - unit.nodes[e.b].y) == 1;
  check(unit.edges.size() == 5 && unit.nodes.size() == 6, "5 unit edges over 6 nodes");
  check(allUnit, "every edge is one tile long and its lo/hi match its endpoints");
  std::set<std::tuple<bool, int, int>> before, after;
  for (const NetTree::Edge &e : legs.edges)
    for (int c = e.lo; c < e.hi; ++c)
      before.insert({e.horizontal, e.cross, c});
  for (const NetTree::Edge &e : unit.edges)
    after.insert({e.horizontal, e.cross, e.lo});
  check(before == after, "the unit edges cover exactly the legs' tiles");
  bool degreesOk = unit.adj.size() == 6;
  int leaves = 0;
  for (const auto &a : unit.adj)
    leaves += a.size() == 1;
  check(degreesOk && leaves == 2, "a path: exactly two leaves");
}

// The decisive layer-assignment fixture. Two horizontal layers, one net per
// layer per tile edge. Other nets already fill layer 0 on tile edges 0-1 and
// layer 1 on tile edges 2-3, and every via site at the switch point is far
// over this project's via-site capacity. A net running tile 0 -> 4:
//  - inherited DP (whole leg on one layer, soft averaged cost) must overflow;
//  - nthu DP on unit edges puts edges 0-1 on layer 1 and 2-3 on layer 0
//    with one via stack -- zero overflow, the via-site "capacity" ignored
//    because ISPD scoring gives vias none.
void testNthuLayerAssignmentSwitchesMidLegToAvoidOverflow() {
  const char *gr = "grid 6 2 3\nvertical capacity 0 0 2\nhorizontal capacity 2 2 0\n"
                   "minimum width 1 1 1\nminimum spacing 1 1 1\nvia spacing 1 1 1\n0 0 10 10\n"
                   "num net 1\nnX 0 2 1\n5 5 1\n45 5 1\n0\n";
  Design d = parse("pd3route_selftest_switch.gr", gr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  auto run = [&](bool nthu, long long &overflow, std::vector<int> &edgeLayer, NetTree &t) {
    RealUsage ru(g);
    RealViaUsage rvu(g);
    ru.h[g.hIndex(0, 0, 0)] = ru.h[g.hIndex(1, 0, 0)] = 2;
    ru.h[g.hIndex(2, 0, 1)] = ru.h[g.hIndex(3, 0, 1)] = 2;
    for (int l = 0; l + 1 < g.nLayers; ++l)
      rvu.usage[viaIndex(g, 2, 0, l)] = 1000;
    NetTree legs;
    buildNetTree({{0, 0, 0, 4, 0, 0}}, legs);
    if (nthu)
      splitToUnitEdges(legs, t);
    else
      t = legs;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> ranges;
    bool ok = layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, ranges, 1.0, nullptr,
                            nullptr, nullptr, nthu);
    RoutedNet rn;
    rn.net = &d.nets[0];
    commitNetTree(g, ru, rvu, 1, t, edgeLayer, ranges, rn);
    overflow = 0;
    for (int c = 0; c < 4; ++c)
      for (int l = 0; l < 2; ++l)
        overflow += std::max<long long>(0, ru.h[g.hIndex(c, 0, l)] - g.hcap[g.hIndex(c, 0, l)]);
    return ok;
  };
  long long ovInh, ovNthu;
  std::vector<int> layInh, layNthu;
  NetTree tInh, tNthu;
  check(run(false, ovInh, layInh, tInh), "inherited DP finds an assignment (sanity)");
  check(run(true, ovNthu, layNthu, tNthu), "nthu DP finds an assignment");
  check(ovInh > 0, "inherited: one layer for the whole leg must overflow (" + std::to_string(ovInh) + ")");
  check(ovNthu == 0, "nthu: switching layers mid-leg reaches zero overflow");
  bool layersRight = tNthu.edges.size() == 4;
  for (size_t i = 0; i < tNthu.edges.size() && layersRight; ++i)
    layersRight = layNthu[i] == (tNthu.edges[i].lo < 2 ? 1 : 0);
  check(layersRight, "nthu: tile edges 0-1 on layer 1, 2-3 on layer 0 -- despite the saturated via sites");
}

// NTHU's order, hand-checked: C (2 pins / 2 edges = 1.0) and B (3 / 3 =
// 1.0) tie, fewer pins first -> C before B; then A (2 / 10 = 0.2); a net
// with no edges (D) goes first.
void testLaNetOrderMatchesNthu() {
  std::vector<size_t> pins = {2, 3, 2, 2};
  std::vector<long long> edges = {10, 3, 2, 0};
  std::vector<size_t> order = laNetOrder(pins, edges);
  check(order == std::vector<size_t>({3, 2, 1, 0}),
        "order is D (no edges), C (1.0, 2 pins), B (1.0, 3 pins), A (0.2)");
}

// The route-file projection: real coordinates -> tiles, vias dropped, a
// stretch present on two layers kept once, everything on layer 0.
void testReadRouteAs2DProjectsWiresAndDropsVias() {
  Design d = parse("pd3route_selftest_basic.gr", kBasicGr);
  std::string path = "pd3route_selftest_proj.route";
  {
    std::ofstream out(path);
    out << "netA 0\n(5,5,1)-(35,5,1)\n(35,5,1)-(35,5,2)\n(35,5,2)-(35,35,2)\n"
           "(35,35,2)-(35,35,1)\n(35,5,1)-(35,35,1)\n!\n";
  }
  std::vector<RoutedNet> nets = readRouteAs2D(d, path);
  check(nets.size() == 1 && nets[0].net == &d.nets[0], "one net, matched to netA by id");
  bool expected = nets.size() == 1 && nets[0].segs.size() == 2 &&
                  nets[0].segs[0].x1 == 0 && nets[0].segs[0].y1 == 0 && nets[0].segs[0].x2 == 3 &&
                  nets[0].segs[0].y2 == 0 && nets[0].segs[1].x1 == 3 && nets[0].segs[1].y1 == 0 &&
                  nets[0].segs[1].x2 == 3 && nets[0].segs[1].y2 == 3;
  check(expected, "wires (0,0)-(3,0) and (3,0)-(3,3) in tiles; both vias dropped; the stretch on two layers kept once");
  bool layer0 = true;
  for (const Seg &sg : nets[0].segs)
    layer0 = layer0 && sg.l1 == 0 && sg.l2 == 0;
  check(layer0, "every projected wire is on the 2D stage's synthetic layer 0");
}

// End to end, nthu mode: route() -> nthu layer assignment on the
// multi-net, multi-layer fixture. Every net is connected in 3D, real
// overflow is zero when 2D overflow is, and projecting the written solution
// back gives exactly the 2D stage's tile edges -- the round trip
// PD3ROUTE_LA_FROM relies on.
void testNthuLayerAssignmentEndToEndAndRoundTrip() {
  Design d = parse("pd3route_selftest_e2e.gr", kMultiNetMultiLayerGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  auto tileEdges = [](const std::vector<Seg> &segs) {
    std::set<std::tuple<bool, int, int>> out;
    for (const Seg &sg : segs) {
      if (sg.l1 != sg.l2)
        continue;
      if (sg.y1 == sg.y2)
        for (int x = std::min(sg.x1, sg.x2); x < std::max(sg.x1, sg.x2); ++x)
          out.insert({true, sg.y1, x});
      else
        for (int y = std::min(sg.y1, sg.y2); y < std::max(sg.y1, sg.y2); ++y)
          out.insert({false, sg.x1, y});
    }
    return out;
  };
  std::vector<std::set<std::tuple<bool, int, int>>> twoD;
  for (const RoutedNet &rn : rep.nets)
    twoD.push_back(tileEdges(rn.segs));
  ViaNegotiationReport vr = negotiateViaAssignment(d.grid, rep.nets, 60, 20, 1.0, HistKnobs(), HistKnobs(),
                                                   LaMode::kNthu);
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "nthu: every net is one connected component in 3D");
  check(rep.bestOverflow == 0 && vr.bestOverflow == 0, "nthu: zero 2D overflow gives zero real overflow here");
  std::string path = "pd3route_selftest_rt.route";
  writeSolution(path, d.grid, rep.nets);
  std::vector<RoutedNet> back = readRouteAs2D(d, path);
  bool same = back.size() == twoD.size();
  for (size_t i = 0; i < back.size() && same; ++i)
    same = tileEdges(back[i].segs) == twoD[i];
  check(same, "written -> projected solution has exactly the 2D stage's tile edges, net by net");
}

// A net whose wires form a cycle -- a square ring, with a pin partway along
// its bottom side. The inherited reconstruction rejects it (and the net
// falls back to greedy per-segment layers); dropCycles keeps a spanning
// tree of the same nodes, with the pin as a node of degree >= 1, so no
// dropped stretch can have cut it off.
void testDropCyclesKeepsARingNetAndItsPin() {
  std::vector<Seg> ring = {{0, 0, 0, 4, 0, 0}, {4, 0, 0, 4, 4, 0}, {4, 4, 0, 0, 4, 0}, {0, 4, 0, 0, 0, 0}};
  std::vector<Point> pins = {{2, 0}, {4, 4}};
  NetTree t;
  check(!buildNetTree(ring, t), "inherited reconstruction rejects a ring (sanity)");
  check(buildNetTree(ring, t, true, &pins), "dropCycles reconstruction accepts it");
  check(t.nodes.size() == 5 && t.edges.size() == 4, "5 nodes (4 corners + the pin), 4 edges: a spanning tree");
  int pinNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 2 && t.nodes[i].y == 0)
      pinNode = static_cast<int>(i);
  check(pinNode >= 0 && !t.adj[pinNode].empty(), "the mid-side pin is a node, still attached");
  NetTree loose;
  check(buildNetTree({{0, 0, 0, 4, 0, 0}, {0, 2, 0, 4, 2, 0}}, loose, true) == false,
        "two disjoint wires are still rejected -- dropCycles never hides a real disconnection");
}

void testParseLaMode() {
  LaMode m = LaMode::kInherited;
  std::string err;
  check(parseLaMode(nullptr, m, err) && m == LaMode::kNthu, "unset -> nthu (this file's default)");
  check(parseLaMode("inherited", m, err) && m == LaMode::kInherited, "inherited parses");
  check(parseLaMode("nthu", m, err) && m == LaMode::kNthu, "nthu parses");
  check(!parseLaMode("fast", m, err), "unknown value rejected");
  check(!parseLaMode("", m, err), "empty value rejected");
}

// ---------------------------------------------------------------------------
// pd4route's own mechanism: the refinement stage.
// ---------------------------------------------------------------------------

// NTHU's refinement cost: 1 for an edge one more net would overflow, a
// length tie-break otherwise; zero/negative capacity counts as full.
void testRefineCostCountsOnlyFullEdges() {
  check(refineCost(8, 10, 2) == kRefineEps, "demand 8 + 2 = cap 10: fits exactly, not full -> tie-break only");
  check(refineCost(9, 10, 2) == 1.0, "demand 9 + 2 > cap 10: full -> 1");
  check(refineCost(0, 0, 2) == 1.0 && refineCost(0, -4, 2) == 1.0, "zero or negative capacity is full");
  Design d = parse("pd4route_selftest_basic.gr", kBasicGr);
  Prices pr(d.grid);
  pr.model = CostModel::kRefine;
  check(heuristicScale(pr) == kRefineEps, "A* scale under refinement is kRefineEps, a true per-edge lower bound");
}

// A wall of full edges at x=10 on every row but row 0. Under refinement cost
// the search crosses at row 0 -- a 10-tile detour -- because length only
// breaks ties; the inherited lambda search (all prices 1) goes straight
// through the wall.
void testRefineDetoursAroundFullEdgesRegardlessOfLength() {
  std::string gr = "grid 30 10 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n25 55 1\n205 55 1\n0\n";
  Design d = parse("pd4route_selftest_wall.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Usage u(g);
  for (int y = 1; y < g.ny; ++y)
    u.h[g.hIndex(10, y, 0)] = u.capH[g.hIndex(10, y, 0)];
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<PathNode> src = {{2, 5, -1}};
  std::vector<Point> snk = {{20, 5}};
  auto crossing = [&](const MSMVResult &r) {
    for (size_t i = 1; i < r.path.size(); ++i)
      if (r.path[i].y == r.path[i - 1].y && std::min(r.path[i].x, r.path[i - 1].x) == 10)
        return r.path[i].y;
    return -1;
  };
  Prices refine(g);
  refine.model = CostModel::kRefine;
  refine.live = &u;
  MSMVResult r = routeStepMSMV(g, refine, lp, region, src, snk, 1, 10);
  check(r.found && crossing(r) == 0, "refinement crosses the wall at the free row 0, a 10-tile detour");
  Prices lambda(g);
  MSMVResult l = routeStepMSMV(g, lambda, lp, region, src, snk, 1, 10);
  check(l.found && crossing(l) == 5, "the lambda search (no usage awareness at price 1) goes straight through");
}

void testRefineOrderMatchesNthu() {
  std::vector<size_t> order = refineOrder({0, 5, 5, 2}, {9, 3, 7, 1});
  check(order == std::vector<size_t>({2, 1, 3}),
        "overflow-free net 0 skipped; 5-overflow nets by larger bbox first (2 before 1); then net 3");
}

const char *kThreeOnOneRowGr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                               "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                               "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                               "nC 2 2 1\n5 15 1\n75 15 1\n0\n";

// A route file's nets, given pts/charges/giant by prepareFor2DRefine,
// charge exactly the tiles their wires cover.
void testPrepareFor2DRefineChargesExactlyTheWires() {
  Design d = parse("pd4route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(d.grid, nets);
  bool row1 = true, elsewhere = true;
  for (int x = 0; x + 1 < 8; ++x) {
    row1 = row1 && u.h[d.grid.hIndex(x, 1, 0)] == 6;
    elsewhere = elsewhere && u.h[d.grid.hIndex(x, 0, 0)] == 0 && u.h[d.grid.hIndex(x, 2, 0)] == 0;
  }
  check(row1 && elsewhere, "three nets x amount 2 on each row-1 tile edge, nothing elsewhere");
  check(nets[0].pts.size() == 2 && !nets[0].giant, "two unique pin tiles, not giant");
}

// End to end: all three nets start straight along the one-net row (7 tile
// edges at 3x demand, 2D overflow 28). Refinement must spread them onto
// rows 0, 1 and 2 -- zero overflow -- with every net still connected.
void testRefine2DClearsTheThreeOnOneRowFixture() {
  Design d = parse("pd4route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  RefineSettings rs;
  RefineReport rr = refine2D(d, nets, rs, false);
  check(rr.before == 28, "starts at 2D overflow 28 (sanity)");
  check(rr.after == 0, "refinement reaches zero (got " + std::to_string(rr.after) + ")");
  check(totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0, "and the returned nets really are overflow-free");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
}

void testParseRefineSettings() {
  RefineSettings r;
  std::string err;
  check(parseRefineSettings(nullptr, nullptr, nullptr, nullptr, nullptr, r, err) && r.enabled && r.iters == 20 &&
            r.boxInit == 10 && r.boxInc == 15 && r.accept == AcceptMode::kKeep,
        "unset -> NTHU's refinement: on, 20 iterations, box 10 + 15, keep");
  check(parseRefineSettings("0", "5", "4", "0", "revert", r, err) && !r.enabled && r.iters == 5 && r.boxInit == 4 &&
            r.boxInc == 0 && r.accept == AcceptMode::kRevert,
        "explicit values parse exactly");
  check(!parseRefineSettings("yes", nullptr, nullptr, nullptr, nullptr, r, err), "PD4ROUTE_REFINE must be 0 or 1");
  check(!parseRefineSettings(nullptr, "-1", nullptr, nullptr, nullptr, r, err), "negative iterations rejected");
  check(!parseRefineSettings(nullptr, nullptr, "0", nullptr, nullptr, r, err), "box init 0 rejected");
  check(!parseRefineSettings(nullptr, nullptr, nullptr, "x", nullptr, r, err), "non-numeric box inc rejected");
  check(!parseRefineSettings(nullptr, nullptr, nullptr, nullptr, "always", r, err), "unknown accept rejected");
}

// pd5route: the shuffled refinement order is a permutation of NTHU's set,
// reproducible for a seed and iteration, different across iterations and
// seeds; nthu order is unchanged.
void testRefineShuffleIsASeededPermutation() {
  std::vector<long long> ov(40);
  std::vector<int> bb(40);
  for (int i = 0; i < 40; ++i) {
    ov[i] = (i % 5 == 0) ? 0 : 1 + (i * 7) % 11;
    bb[i] = (i * 13) % 17;
  }
  RefineSettings nthu, sh;
  sh.shuffle = true;
  sh.seed = 3;
  std::vector<size_t> base = refineOrder(ov, bb);
  check(refineOrderFor(ov, bb, nthu, 4) == base, "nthu mode: exactly refineOrder, every iteration");
  std::vector<size_t> a = refineOrderFor(ov, bb, sh, 0), b = refineOrderFor(ov, bb, sh, 0),
                      c = refineOrderFor(ov, bb, sh, 1);
  std::vector<size_t> sa = a, sb = base;
  std::sort(sa.begin(), sa.end());
  std::sort(sb.begin(), sb.end());
  check(sa == sb, "shuffle: the same nets (only overflowed ones), permuted");
  check(a == b, "shuffle: reproducible for the same seed and iteration");
  check(a != c && a != base, "shuffle: differs across iterations and from NTHU's order");
  RefineSettings other = sh;
  other.seed = 4;
  check(refineOrderFor(ov, bb, other, 0) != a, "shuffle: differs across seeds");
  RefineSettings r;
  std::string err;
  check(parseRefineOrder("shuffle", "7", r, err) && r.shuffle && r.seed == 7, "shuffle / seed 7 parse");
  check(!parseRefineOrder("random", nullptr, r, err) && !parseRefineOrder(nullptr, "-2", r, err) &&
            !parseRefineOrder(nullptr, "x", r, err),
        "unknown order, negative and non-numeric seeds rejected");
}

// pd5route mechanism 2: the region is the overflow's bbox (both ends of
// each overflowed edge), expanded and clipped; netTouchesBox picks the nets
// with wire in it; and on the three-nets-on-one-row fixture the region
// negotiation alone reaches zero with every net connected.
void testRegionNegotiationClearsTheThreeOnOneRowFixture() {
  Design d = parse("pd5route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(d.grid, nets);
  TileBox b;
  check(overflowRegion(d.grid, u, 0, b) && b.x0 == 0 && b.x1 == 7 && b.y0 == 1 && b.y1 == 1,
        "unexpanded region: tiles x 0-7 of row 1 (both ends of every overflowed edge)");
  check(overflowRegion(d.grid, u, 5, b) && b.x0 == 0 && b.x1 == 7 && b.y0 == 0 && b.y1 == 2,
        "expanded by 5 and clipped to the 8 x 3 grid");
  TileBox corner{6, 2, 7, 2};
  check(netTouchesBox(nets[0], TileBox{3, 1, 3, 1}) && !netTouchesBox(nets[0], corner),
        "a net touches a box its wire passes through, not one it misses");
  RegionSettings rs;
  rs.enabled = true;
  RegionReport rr = regionNegotiate(d, nets, rs, false);
  check(rr.before == 28 && rr.regionNets == 3, "starts at 28 with all three nets in the region");
  check(rr.after == 0 && totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0,
        "region negotiation reaches zero, and the returned nets really are overflow-free");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
  Usage clean = usageFromCharges(d.grid, nets);
  check(!overflowRegion(d.grid, clean, 5, b), "no overflow -> no region");
  RegionSettings p;
  std::string err;
  check(parseRegionSettings("1", "8", "30", "12", "3", p, err) && p.enabled && p.expand == 8 && p.rounds == 30 &&
            p.boxInit == 12 && p.boxGrow == 3,
        "region settings parse exactly");
  check(!parseRegionSettings("on", nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseRegionSettings(nullptr, "-1", nullptr, nullptr, nullptr, p, err) &&
            !parseRegionSettings(nullptr, nullptr, nullptr, "0", nullptr, p, err),
        "bad enable flag, negative expand, zero box init rejected");
}

// pd6route: 2-pin-piece reroutes.
// A T-shaped net -- pins (0,2), (10,2), (5,4); wire (0,2)-(10,2) and
// (5,2)-(5,4) -- has three pieces meeting at the branch tile (5,2). Other
// nets fill tile edge (7,2)-(8,2). Refinement-cost piece rerouting must
// reroute only the piece through that edge, reach zero overflow, leave the
// left piece exactly where it was, keep the net a connected tree covering
// its pins, and keep live usage equal to the charges.
void testPieceRerouteMovesOnlyTheOverflowedPiece() {
  std::string gr = "grid 12 5 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnT 0 3 1\n5 25 1\n105 25 1\n55 45 1\n0\n";
  Design d = parse("pd6route_selftest_tee.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 2, 0, 10, 2, 0}, {5, 2, 0, 5, 4, 0}};
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(g, nets);
  size_t hot = g.hIndex(7, 2, 0);
  u.h[hot] += u.capH[hot]; // other nets fill it to capacity; this net's wire pushes it over
  Usage other(g);
  other.h[hot] = u.capH[hot];
  Prices pr(g);
  pr.model = CostModel::kRefine;
  pr.live = &u;
  LayerPlan lp(g);
  SearchRegion region;
  PieceStats ps = rerouteNetPieces(g, u, pr, lp, region, nets[0], 10, AcceptMode::kKeep, nullptr);
  check(ps.ok && ps.pieces == 1, "exactly one piece rerouted (" + std::to_string(ps.pieces) + ")");
  check(totalOverflow(g, u) == 0, "zero overflow after");
  bool leftKept = true;
  for (int x = 0; x < 5; ++x) {
    bool has = false;
    for (const Seg &sg : nets[0].segs)
      has = has || (sg.y1 == 2 && sg.y2 == 2 && std::min(sg.x1, sg.x2) == x && std::max(sg.x1, sg.x2) == x + 1);
    leftKept = leftKept && has;
  }
  check(leftKept, "the left piece (0,2)-(5,2) is untouched, tile edge by tile edge");
  bool usesHot = false;
  for (const Seg &sg : nets[0].segs)
    usesHot = usesHot || (sg.y1 == 2 && sg.y2 == 2 && std::min(sg.x1, sg.x2) == 7);
  check(!usesHot, "the rerouted piece avoids the full tile edge");
  check(segsConnected(nets[0].segs), "the net is still one connected tree");
  Usage expect = usageFromCharges(g, nets);
  bool same = true;
  for (size_t i = 0; i < u.h.size(); ++i)
    same = same && u.h[i] == expect.h[i] + other.h[i];
  for (size_t i = 0; i < u.v.size(); ++i)
    same = same && u.v[i] == expect.v[i];
  check(same, "live usage = this net's charges + the other nets', exactly");
  std::vector<RoutedNet> loose(1);
  loose[0].net = &d.nets[0];
  loose[0].segs = {{0, 0, 0, 3, 0, 0}, {0, 4, 0, 3, 4, 0}};
  prepareFor2DRefine(d, loose);
  Usage u2 = usageFromCharges(g, loose);
  Usage u2copy = u2;
  PieceStats bad = rerouteNetPieces(g, u2, pr, lp, region, loose[0], 10, AcceptMode::kKeep, nullptr);
  check(!bad.ok && u2.h == u2copy.h, "wiring that is not a tree: not ok, usage untouched (caller falls back)");
}

// The box heuristic (distance to the sinks' bounding box) must find paths
// of exactly the same cost as the nearest-sink heuristic: both admissible.
void testBoxHeuristicFindsTheSameCost() {
  std::string gr = "grid 30 30 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd6route_selftest_boxh.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  int same = 0;
  for (int f = 0; f < 20; ++f) {
    Prices pr(g);
    for (size_t e = 0; e < pr.h.size(); ++e)
      pr.h[e] = 1.0 + static_cast<double>((e * 2654435761u + f * 97u) % 17);
    for (size_t e = 0; e < pr.v.size(); ++e)
      pr.v[e] = 1.0 + static_cast<double>((e * 40503u + f * 131u) % 13);
    std::vector<PathNode> src = {{3 + f % 5, 4, 0}, {4, 6 + f % 3, 0}};
    std::vector<Point> snk;
    for (int k = 0; k < 12; ++k)
      snk.push_back({18 + (k * 7 + f) % 9, 15 + (k * 5 + f) % 11});
    auto cost = [&](const MSMVResult &r) {
      double c = 0;
      for (size_t i = 1; i < r.path.size(); ++i)
        c += r.path[i].y == r.path[i - 1].y
                 ? wireCostH(pr, g.hIndex(std::min(r.path[i].x, r.path[i - 1].x), r.path[i].y, 0), 2)
                 : wireCostV(pr, g.vIndex(r.path[i].x, std::min(r.path[i].y, r.path[i - 1].y), 0), 2);
      return c;
    };
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 40, false);
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 40, true);
    same += a.found && b.found && std::abs(cost(a) - cost(b)) < 1e-9;
  }
  check(same == 20, "box heuristic: same optimal cost as nearest-sink on 20 multi-sink fixtures (" +
                        std::to_string(same) + "/20)");
}

// End to end on the three-nets-on-one-row fixture: refinement and region
// negotiation in piece mode each reach zero with every net connected.
void testPieceModeStagesClearTheThreeOnOneRowFixture() {
  for (int stage = 0; stage < 2; ++stage) {
    Design d = parse("pd6route_selftest_row.gr", kThreeOnOneRowGr);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    long long after;
    if (stage == 0) {
      RefineSettings rs;
      rs.pieces = true;
      after = refine2D(d, nets, rs, false).after;
    } else {
      RegionSettings rs;
      rs.enabled = rs.pieces = true;
      after = regionNegotiate(d, nets, rs, false).after;
    }
    bool connected = true;
    for (const RoutedNet &rn : nets)
      connected = connected && segsConnected(rn.segs);
    std::string name = stage == 0 ? "refinement" : "region negotiation";
    check(after == 0 && totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0,
          name + " in piece mode reaches zero");
    check(connected, name + " in piece mode keeps every net connected");
  }
}

// ---------------------------------------------------------------------------
// pd7route: wirelength and bend recovery.
// ---------------------------------------------------------------------------

// (0,0) -> (3,3) with everything usable: 6 tile edges, and the cheapest
// shape has one corner, so cost 6 + 1 * bend.
void testBendAwareSearchCountsCorners() {
  auto all = [](bool, int, int) { return true; };
  double c = 0;
  std::vector<Point> p = bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 1.0, all, c);
  check(p.size() == 7 && std::abs(c - 7.0) < 1e-9, "L connection: 7 tiles, cost 6 + 1 corner = 7");
  int corners = 0;
  for (size_t i = 1; i + 1 < p.size(); ++i)
    corners += (p[i - 1].y == p[i].y) != (p[i].y == p[i + 1].y);
  check(corners == 1, "exactly one corner on the returned path");
  double c0 = 0;
  bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 0.0, all, c0);
  check(std::abs(c0 - 6.0) < 1e-9, "bend 0: cost is plain length 6");
  auto wall = [](bool h, int x, int) { return !(h && x == 1); }; // no horizontal edge (1,y)-(2,y) anywhere
  double cw = 0;
  check(bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 1.0, wall, cw).empty(),
        "a full column wall with no gap: no path");
}

const char *kWlGr = "grid 12 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 1\nnW 0 2 1\n5 15 1\n85 15 1\n0\n";

// A two-pin net (1,1)-(8,1) routed as a detour: up to row 5 and back
// (8 + 2 * 4 = 16 tile edges, 2 corners). Recovery must straighten it to
// the 8-edge row-1 route, keep usage exact, keep the net connected.
void testWlRecoveryShortensADetour() {
  Design d = parse("pd7route_selftest_wl.gr", kWlGr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 0, 5, 0}, {0, 5, 0, 8, 5, 0}, {8, 5, 0, 8, 1, 0}};
  // pins are (0,1) and (8,1): tiles of "5 15" and "85 15"
  prepareFor2DRefine(d, nets);
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  check(r.wireBefore == 16 && r.wireAfter == 8, "detour of 16 tile edges straightened to 8 (" +
                                                    std::to_string(r.wireBefore) + " -> " + std::to_string(r.wireAfter) + ")");
  check(segsConnected(nets[0].segs), "still connected");
  Usage u = usageFromCharges(d.grid, nets);
  check(totalOverflow(d.grid, u) == 0, "no overflow");
}

// Same detour, but the straight row is full at tile edge (4,1)-(5,1) (other
// nets fill it to capacity). Recovery must never route through it: the
// result may shorten via row 0 or 2, but never uses the full edge, and 2D
// overflow does not rise.
void testWlRecoveryNeverUsesAFullEdge() {
  Design d = parse("pd7route_selftest_wl.gr", kWlGr);
  std::vector<RoutedNet> nets(2);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 0, 5, 0}, {0, 5, 0, 8, 5, 0}, {8, 5, 0, 8, 1, 0}};
  // A second "net" is just two parallel blockers occupying (4,1)-(5,1): same
  // Net record, used only for its charges -- width 1 + spacing 1 = 2 each.
  nets[1].net = &d.nets[0];
  nets[1].giant = true; // never touched by recovery
  nets[1].charges = {{true, 1, 4, 5, 0, 2}, {true, 1, 4, 5, 0, 2}};
  nets[1].segs = {{4, 1, 0, 5, 1, 0}};
  std::vector<RoutedNet> one(nets.begin(), nets.begin() + 1);
  prepareFor2DRefine(d, one);
  nets[0] = one[0];
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  bool usesFull = false;
  for (const Seg &sg : nets[0].segs)
    usesFull = usesFull || (sg.y1 == 1 && sg.y2 == 1 && std::min(sg.x1, sg.x2) == 4);
  check(!usesFull, "the full tile edge (4,1)-(5,1) is never used");
  check(r.wireAfter < r.wireBefore, "it still shortens around the blockage (" + std::to_string(r.wireBefore) +
                                         " -> " + std::to_string(r.wireAfter) + ")");
  check(totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0, "no overflow created");
  check(segsConnected(nets[0].segs), "still connected");
}

// A staircase (0,1) -> (6,4) with 5 corners and minimum length 9 is
// replaced by a one-corner route of the same length: bends alone pay.
void testWlRecoveryRemovesCornersAtEqualLength() {
  const char *gr = "grid 12 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnS 0 2 1\n5 15 1\n65 45 1\n0\n";
  Design d = parse("pd7route_selftest_stair.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 2, 1, 0}, {2, 1, 0, 2, 2, 0}, {2, 2, 0, 4, 2, 0},
                  {4, 2, 0, 4, 3, 0}, {4, 3, 0, 6, 3, 0}, {6, 3, 0, 6, 4, 0}};
  prepareFor2DRefine(d, nets);
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  std::map<std::pair<int, int>, int> o;
  for (const Seg &sg : nets[0].segs) {
    int bit = sg.y1 == sg.y2 ? 1 : 2;
    o[{sg.x1, sg.y1}] |= bit;
    o[{sg.x2, sg.y2}] |= bit;
  }
  int corners = 0;
  for (auto &[t, b] : o)
    corners += b == 3;
  check(r.wireBefore == 9 && r.wireAfter == 9, "length unchanged at the minimum 9");
  check(corners == 1, "corners 5 -> " + std::to_string(corners));
  check(segsConnected(nets[0].segs), "still connected");
}

void testParseWlSettings() {
  WlSettings w;
  std::string err;
  check(parseWlSettings(nullptr, nullptr, nullptr, nullptr, w, err) && w.enabled && w.passes == 4 && w.bend == 3.0 &&
            w.margin == 8,
        "defaults: on, 4 passes, bend 3, margin 8");
  check(parseWlSettings("0", "5", "2.5", "0", w, err) && !w.enabled && w.passes == 5 && w.bend == 2.5 && w.margin == 0,
        "explicit values parse exactly");
  check(!parseWlSettings("yes", nullptr, nullptr, nullptr, w, err) && !parseWlSettings(nullptr, "-1", nullptr, nullptr, w, err) &&
            !parseWlSettings(nullptr, nullptr, "-1", nullptr, w, err) && !parseWlSettings(nullptr, nullptr, "nan", nullptr, w, err),
        "bad flag, negative passes, negative or NaN bend rejected");
}

// Pin-aware layer assignment: a straight two-pin net on row 0 whose pins
// are on layer 0; horizontal layers 0 and 2, layer 0 half full, layer 2
// empty. Blind to pins, the nthu DP takes the emptier layer 2 (its fill
// tie-break) and the pins then need 2 vias each. With pin ranges the DP
// counts that and stays on layer 0: no via at either end.
void testPinAwareLayerAssignmentAvoidsPinStacks() {
  const char *gr = "grid 6 2 3\nvertical capacity 0 4 0\nhorizontal capacity 4 0 4\n"
                   "minimum width 1 1 1\nminimum spacing 1 1 1\nvia spacing 1 1 1\n0 0 10 10\n"
                   "num net 1\nnP 0 2 1\n5 5 1\n45 5 1\n0\n";
  Design d = parse("pd7route_selftest_pins.gr", gr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  NetTree legs, t;
  buildNetTree({{0, 0, 0, 4, 0, 0}}, legs);
  splitToUnitEdges(legs, t);
  auto run = [&](bool pins, std::vector<int> &edgeLayer, std::vector<std::pair<int, int>> &ranges) {
    RealUsage ru(g);
    RealViaUsage rvu(g);
    for (int x = 0; x < 4; ++x)
      ru.h[g.hIndex(x, 0, 0)] = 2;
    std::vector<std::pair<int, int>> pr(t.nodes.size(), {-1, -1});
    for (size_t u = 0; u < t.nodes.size(); ++u)
      if (t.nodes[u].y == 0 && (t.nodes[u].x == 0 || t.nodes[u].x == 4))
        pr[u] = {0, 0};
    int root = 0;
    return layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, root, ranges, 1.0, nullptr, nullptr, nullptr,
                         true, pins ? &pr : nullptr);
  };
  std::vector<int> blind, aware;
  std::vector<std::pair<int, int>> rb, ra;
  check(run(false, blind, rb) && run(true, aware, ra), "both DPs find assignments (sanity)");
  bool blindHigh = true, awareLow = true;
  for (size_t e = 0; e < t.edges.size(); ++e) {
    blindHigh = blindHigh && blind[e] == 2;
    awareLow = awareLow && aware[e] == 0;
  }
  check(blindHigh, "blind to pins: the emptier layer 2 (then 2 pin vias per end)");
  check(awareLow, "pin-aware: layer 0, where the pins are");
  int stacks = 0;
  for (auto &r : ra)
    stacks += r.second - r.first;
  check(stacks == 0, "pin-aware: no via stack anywhere on the net");
}

// pd8route: whole-net tree rebuild.
// shapeCost: tile edges + bend * corner tiles. An L of 3 + 2 tile edges has
// one corner: 5 + 3 * 1 = 8 at bend 3.
void testShapeCostCountsCornerTiles() {
  std::set<UnitEdge> l = unitEdgesOf({{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}});
  check(l.size() == 5 && std::abs(shapeCost(l, 3.0) - 8.0) < 1e-9, "L of 5 tile edges, 1 corner: cost 8 at bend 3");
  std::set<UnitEdge> straight = unitEdgesOf({{0, 0, 0, 5, 0, 0}});
  check(std::abs(shapeCost(straight, 3.0) - 5.0) < 1e-9, "straight 5: cost 5");
}

// A 3-pin net -- pins (0,0), (6,0), (3,4) -- wired as a wasteful tree:
// row 0 from 0 to 6, plus a separate staircase from (0,0) up to (3,4).
// Rebuild must replace it with a cheaper tree (a stub up from row 0),
// connected, covering all pins, charges consistent with usage.
void testRebuildReplacesAWastefulTree() {
  const char *gr = "grid 10 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnR 0 3 1\n5 5 1\n65 5 1\n35 45 1\n0\n";
  Design d = parse("pd8route_selftest_rb.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 0, 0, 6, 0, 0}, {0, 0, 0, 0, 1, 0}, {0, 1, 0, 1, 1, 0}, {1, 1, 0, 1, 2, 0},
                  {1, 2, 0, 2, 2, 0}, {2, 2, 0, 2, 3, 0}, {2, 3, 0, 3, 3, 0}, {3, 3, 0, 3, 4, 0}};
  prepareFor2DRefine(d, nets);
  double before = shapeCost(unitEdgesOf(nets[0].segs), 3.0);
  RebuildSettings rs;
  RebuildReport r = rebuild2D(d, nets, 3.0, rs, false);
  double after = shapeCost(unitEdgesOf(nets[0].segs), 3.0);
  check(r.rebuilt == 1 && after < before, "rebuilt, shape cost " + std::to_string(before) + " -> " + std::to_string(after));
  check(r.wireAfter == 10, "optimal: row 0 (6) + stub (4) = 10 tile edges (got " + std::to_string(r.wireAfter) + ")");
  check(segsConnected(nets[0].segs), "connected");
  Usage u = usageFromCharges(d.grid, nets);
  check(totalOverflow(d.grid, u) == 0, "no overflow");
}

// Already optimal: an L-shaped 2-pin net. Rebuild must keep it untouched
// (no strict gain), segs byte for byte.
void testRebuildKeepsAnOptimalTree() {
  const char *gr = "grid 10 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnK 0 2 1\n5 5 1\n35 25 1\n0\n";
  Design d = parse("pd8route_selftest_keep.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}};
  prepareFor2DRefine(d, nets);
  std::vector<Seg> before = nets[0].segs;
  RebuildReport r = rebuild2D(d, nets, 3.0, RebuildSettings{}, false);
  bool same = nets[0].segs.size() == before.size();
  for (size_t i = 0; same && i < before.size(); ++i)
    same = nets[0].segs[i].x1 == before[i].x1 && nets[0].segs[i].y1 == before[i].y1 &&
           nets[0].segs[i].x2 == before[i].x2 && nets[0].segs[i].y2 == before[i].y2;
  check(r.rebuilt == 0 && same, "optimal L: not rebuilt, segs unchanged");
}

// pd9route: search-stage piece reroutes still clear the detour-only
// fixture (three nets on a one-net row) under present-demand pricing and
// keep, with every net connected; and PD9ROUTE_STOP_AT ends the slow tier
// once overflow is at or below it (a huge threshold: stop after slow
// round 0, so fewer rounds than without it).
void testSlowPiecesAndStopAt() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd9route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                        true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, true, -1);
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(rep.bestOverflow == 0 && connected, "slow-tier pieces + keep: zero 2D overflow, every net connected");
  // Stop-at needs a run that cannot converge: the same three nets on a
  // single row (no room to detour), so keep mode runs every slow round.
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d2 = parse("pd9route_selftest_row.gr", row);
  RunReport full = route(d2, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                         true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1);
  Design d3 = parse("pd9route_selftest_row.gr", row);
  RunReport early = route(d3, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                          true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, false, 1000000);
  check(!full.convergedToZero && full.roundsRun == 21, "without a threshold: 1 fast + all 20 slow rounds (" +
                                                           std::to_string(full.roundsRun) + ")");
  check(early.roundsRun == 2, "threshold above the overflow: stops after slow round 0 (" +
                                  std::to_string(early.roundsRun) + " rounds)");
}

// pd10route: pattern-first piece reroutes. A 2-pin net (0,1) -> (8,3)
// wired as the L along row 1 then up column 8, with row 1's tile edge
// (4,1)-(5,1) full: the other L (up column 0, along row 3) fits, so the
// piece is rerouted by a pattern -- no maze -- avoids the full edge, and
// the net stays connected. With the (4,y)-(5,y) edge full on every row, no
// L or Z shape can cross x = 4..5, and the maze search takes over.
// (Endpoints on one row admit only the straight shape, NTHU's monotonic
// routing likewise -- a piece along a blocked row always goes to the maze.)
void testPatternFirstPieceReroute() {
  std::string gr = "grid 12 5 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnL 0 2 1\n5 15 1\n85 35 1\n0\n";
  for (int blockRows = 0; blockRows < 2; ++blockRows) {
    Design d = parse("pd10route_selftest_l.gr", gr.c_str());
    const RoutingGrid &g = d.grid;
    std::vector<RoutedNet> nets(1);
    nets[0].net = &d.nets[0];
    nets[0].segs = {{0, 1, 0, 8, 1, 0}, {8, 1, 0, 8, 3, 0}};
    prepareFor2DRefine(d, nets);
    Usage u = usageFromCharges(g, nets);
    size_t hot = g.hIndex(4, 1, 0);
    u.h[hot] += u.capH[hot];
    if (blockRows)
      for (int y = 0; y < g.ny; ++y)
        if (y != 1)
          u.h[g.hIndex(4, y, 0)] = u.capH[g.hIndex(4, y, 0)];
    Prices pr(g);
    pr.model = CostModel::kRefine;
    pr.live = &u;
    LayerPlan lp(g);
    SearchRegion region;
    patternFirst = true;
    PieceStats ps = rerouteNetPieces(g, u, pr, lp, region, nets[0], 10, AcceptMode::kKeep, nullptr);
    patternFirst = false;
    bool usesHot = false;
    for (const Seg &sg : nets[0].segs)
      usesHot = usesHot || (sg.y1 == 1 && sg.y2 == 1 && std::min(sg.x1, sg.x2) == 4);
    if (!blockRows) {
      check(ps.ok && ps.pieces == 1 && ps.patterned == 1, "a fitting L exists: rerouted by pattern, no maze");
      check(!usesHot && segsConnected(nets[0].segs), "avoids the full edge; net still connected");
      check(totalOverflow(g, u) == 0, "no overflow left");
    } else {
      check(ps.ok && ps.pieces == 1 && ps.patterned == 0, "every L/Z shape blocked: the maze search takes over");
    }
  }
}

// pd11route: the batched pattern stage. Two nets with identical corner
// pins (0,0) -> (5,3) on a grid whose tile edges hold one net each: the
// only zero-overflow answer is one net on each L. The stage (CPU backend,
// run through route() with no slow rounds) must find it -- the two pieces'
// boxes overlap, so they land in different color batches and the second
// sees the first's usage -- and leave both nets connected.
void testPatternStageSeparatesTwoNetsOntoTheTwoLs() {
  const char *gr = "grid 8 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 2\nnA 0 2 1\n5 5 1\n55 35 1\nnB 1 2 1\n5 5 1\n55 35 1\n0\n";
  Design d = parse("pd11route_selftest_ls.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  PatternStageSettings ps;
  RunReport rep = route(d, 60, 0, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true,
                        8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1, &ps);
  check(rep.bestOverflow == 0, "pattern stage alone: zero 2D overflow (got " + std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 2;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "both nets connected");
  std::set<std::pair<bool, std::pair<int, int>>> a, b;
  for (const ChargeRecord &c : rep.nets[0].charges)
    a.insert({c.horizontal, {c.cross, c.a}});
  for (const ChargeRecord &c : rep.nets[1].charges)
    b.insert({c.horizontal, {c.cross, c.a}});
  bool disjoint = true;
  for (auto &e : a)
    disjoint = disjoint && !b.count(e);
  check(disjoint && a.size() == 8 && b.size() == 8, "the two nets take the two disjoint 8-edge Ls");
}

// pd12route: the plateau rule itself, then both stages on a fixture that
// cannot improve (three nets on a one-row grid with room for one: every
// round leaves overflow exactly where it was) -- without the rule each runs
// its whole budget; with window N it stops after exactly N rounds, keeping
// the same (best) state -- and on the three-nets-on-one-row fixture that
// can reach zero, where the rule must not cut the stage short.
void testPlateauStop() {
  std::vector<long long> h{100, 90, 89, 89};
  check(!plateauReached(h, 0, 0.01), "window 0: never stops");
  check(!plateauReached(h, 3, 0.01), "window 3 after 3 rounds: 100 -> 89 is a real gain, continue");
  check(!plateauReached({100, 90, 89}, 2, 0.01),
        "window 2: 100 -> 89 gains 11 >= max(1, 1% of 100) = 1, continue");
  check(!plateauReached(h, 2, 0.01), "window 2: 90 -> 89 gains 1 = the 1-unit floor, continue");
  check(plateauReached({100, 90, 89, 89, 89}, 2, 0.01), "window 2: 89 -> 89, no gain, stop");
  check(plateauReached({100, 100, 100}, 2, 0.0), "gain 0 still needs 1 unit: flat for 2 rounds stops");
  check(!plateauReached({100, 100, 99}, 2, 0.0), "gain 0: one unit in the window is enough to continue");
  check(plateauReached({1000, 995, 990}, 2, 0.02) && !plateauReached({1000, 990, 980}, 2, 0.02),
        "gain 2%: 1000 -> 990 (1%) stops, 1000 -> 980 (2%) continues");
  check(!plateauReached({5}, 1, 0.5), "no rounds run yet: never stops");
  check(plateauReached({100, 100, 100}, 2, 0.0, 100) && !plateauReached({100, 100, 100}, 2, 0.0, 101),
        "floor: a flat best of 100 stops with floor 100, not with floor 101");

  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto stuck = [&](Design &d) {
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    return nets;
  };
  for (int stage = 0; stage < 2; ++stage) {
    for (int pieces = 0; pieces < 2; ++pieces) {
      std::string name = std::string(stage == 0 ? "region" : "refinement") + (pieces ? " (pieces)" : " (whole nets)");
      Design d1 = parse("pd12route_selftest_stuck.gr", row), d2 = parse("pd12route_selftest_stuck.gr", row);
      std::vector<RoutedNet> n1 = stuck(d1), n2 = stuck(d2);
      long long start = totalOverflow(d1.grid, usageFromCharges(d1.grid, n1));
      int runFull, runStop;
      long long afterFull, afterStop;
      bool plFull, plStop;
      int runFloorAbove = -1;
      if (stage == 0) {
        RegionSettings rs;
        rs.enabled = true;
        rs.pieces = pieces;
        rs.rounds = 40;
        RegionReport a = regionNegotiate(d1, n1, rs, false);
        rs.plateau = 5;
        rs.plateauMin = start; // the stuck overflow itself: the rule applies
        RegionReport b = regionNegotiate(d2, n2, rs, false);
        Design d3 = parse("pd12route_selftest_stuck.gr", row);
        std::vector<RoutedNet> n3 = stuck(d3);
        rs.plateauMin = start + 1; // above it: the rule never applies
        runFloorAbove = regionNegotiate(d3, n3, rs, false).roundsRun;
        runFull = a.roundsRun, runStop = b.roundsRun, afterFull = a.after, afterStop = b.after;
        plFull = a.plateaued, plStop = b.plateaued;
      } else {
        RefineSettings rs;
        rs.pieces = pieces;
        rs.iters = 40;
        RefineReport a = refine2D(d1, n1, rs, false);
        rs.plateau = 5;
        rs.plateauMin = start;
        RefineReport b = refine2D(d2, n2, rs, false);
        Design d3 = parse("pd12route_selftest_stuck.gr", row);
        std::vector<RoutedNet> n3 = stuck(d3);
        rs.plateauMin = start + 1;
        runFloorAbove = refine2D(d3, n3, rs, false).itersRun;
        runFull = a.itersRun, runStop = b.itersRun, afterFull = a.after, afterStop = b.after;
        plFull = a.plateaued, plStop = b.plateaued;
      }
      check(start > 0 && runFull == 40 && !plFull,
            name + ", stuck fixture, no window: runs all 40 rounds (" + std::to_string(runFull) + ")");
      check(runStop == 5 && plStop, name + ", window 5, floor = the stuck overflow " + std::to_string(start) +
                                        ": stops after exactly 5 rounds (" + std::to_string(runStop) + ")");
      check(runFloorAbove == 40, name + ", window 5, floor one above it: runs all 40 rounds (" +
                                     std::to_string(runFloorAbove) + ")");
      check(afterStop == afterFull && afterStop == totalOverflow(d2.grid, usageFromCharges(d2.grid, n2)),
            name + ": same best overflow either way, and the returned nets really carry it");
    }
  }
  // A stage that can reach zero is not cut short by a window of 1 round and
  // a demanding 50% gain -- as long as each round really improves; and it
  // reports no plateau stop when it converges.
  Design dz = parse("pd12route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nz(3);
  for (int i = 0; i < 3; ++i) {
    nz[i].net = &dz.nets[i];
    nz[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(dz, nz);
  RegionSettings rz;
  rz.enabled = true;
  rz.plateau = 3;
  RegionReport rr = regionNegotiate(dz, nz, rz, false);
  check(rr.after == 0 && !rr.plateaued, "solvable fixture with window 3: region still reaches zero (" +
                                             std::to_string(rr.after) + " after " + std::to_string(rr.roundsRun) +
                                             " rounds)");
  int rw = 0, fw = 0;
  double gf = 0.01;
  long long mn = 0;
  std::string err;
  check(parsePlateauSettings("50", "10", "2.5", "1000", rw, fw, gf, mn, err) && rw == 50 && fw == 10 &&
            gf == 0.025 && mn == 1000,
        "plateau settings parse exactly (percent -> fraction)");
  rw = 7;
  check(parsePlateauSettings(nullptr, nullptr, nullptr, nullptr, rw, fw, gf, mn, err) && rw == 7 && mn == 1000,
        "unset values leave the defaults alone");
  check(!parsePlateauSettings("-1", nullptr, nullptr, nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, "x", nullptr, nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, "101", nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, "-0.5", nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, nullptr, "-3", rw, fw, gf, mn, err),
        "negative or non-numeric windows or floor, gain outside [0, 100] rejected");
}

// pd13route: the margin formula and its cap, then both stages on the stuck
// fixture (each stage reports the margin of its last round): 40 rounds grow the region margin 10 + 2r to 88 and the
// refinement margin 10 + 15i to 595 uncapped; with a cap of 20 neither
// goes past 20, and the best overflow is unchanged (the fixture has no
// detour at any margin).
void testStageMarginCap() {
  check(stageMargin(10, 2, 0, 0) == 10 && stageMargin(10, 2, 499, 0) == 1008,
        "uncapped: 10 + 2r, 1008 by round 499");
  check(stageMargin(10, 2, 499, 200) == 200 && stageMargin(10, 2, 94, 200) == 198 && stageMargin(10, 2, 95, 200) == 200,
        "cap 200: follows 10 + 2r up to round 94 (198), 200 from round 95 on");
  check(stageMargin(10, 0, 1000000, 5) == 5 && stageMargin(300, 5, 0, 200) == 200,
        "a cap below the starting margin still caps it");
  check(stageMargin(10, 1000000, 1000000, 0) == std::numeric_limits<int>::max(),
        "a huge uncapped product saturates instead of wrapping");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  for (int stage = 0; stage < 2; ++stage) {
    int maxMargin[2] = {0, 0};
    long long after[2] = {0, 0};
    for (int capped = 0; capped < 2; ++capped) {
      Design d = parse("pd13route_selftest_stuck.gr", row);
      std::vector<RoutedNet> nets(3);
      for (int i = 0; i < 3; ++i) {
        nets[i].net = &d.nets[i];
        nets[i].segs = {{0, 0, 0, 7, 0, 0}};
      }
      prepareFor2DRefine(d, nets);
      if (stage == 0) {
        RegionSettings rs;
        rs.enabled = true;
        rs.rounds = 40;
        rs.boxCap = capped ? 20 : 0;
        RegionReport rr = regionNegotiate(d, nets, rs, false);
        after[capped] = rr.after;
        maxMargin[capped] = rr.lastMargin;
      } else {
        RefineSettings rs;
        rs.iters = 40;
        rs.boxCap = capped ? 20 : 0;
        RefineReport fr = refine2D(d, nets, rs, false);
        after[capped] = fr.after;
        maxMargin[capped] = fr.lastMargin;
      }
    }
    std::string name = stage == 0 ? "region" : "refinement";
    int uncappedWant = stage == 0 ? 88 : 595;
    check(maxMargin[0] == uncappedWant, name + " uncapped: last margin " + std::to_string(maxMargin[0]) + " (want " +
                                            std::to_string(uncappedWant) + ")");
    check(maxMargin[1] == 20, name + " with cap 20: last margin " + std::to_string(maxMargin[1]));
    check(after[0] == after[1], name + ": same best overflow capped and uncapped on a fixture with no detour");
  }
  int rc = 7, fc = 9;
  std::string err;
  check(parseBoxCaps("200", "150", rc, fc, err) && rc == 200 && fc == 150, "box caps parse exactly");
  check(parseBoxCaps(nullptr, nullptr, rc, fc, err) && rc == 200 && fc == 150, "unset caps leave values alone");
  check(!parseBoxCaps("-1", nullptr, rc, fc, err) && !parseBoxCaps(nullptr, "big", rc, fc, err),
        "negative or non-numeric caps rejected");
}

// pd14route: the work-gated rule itself, the expansion counter, and the
// region stage on the stuck fixture: no window -> all 40 rounds; window 5
// with a work threshold the rounds clear -> exactly 5; the same window with
// a threshold above the work actually done -> all 40.
void testWorkGatedPlateau() {
  std::vector<long long> flat{100, 100, 100, 100}, pops{0, 10, 20, 30};
  check(!workGatedPlateau(flat, pops, 0, 0, 0.01), "window 0: never stops");
  check(workGatedPlateau(flat, pops, 3, 30, 0.01) && !workGatedPlateau(flat, pops, 3, 31, 0.01),
        "flat for 3 rounds: stops when those rounds did >= 30 expansions, not when 31 are required");
  check(!workGatedPlateau({100, 90, 80, 70}, pops, 3, 0, 0.01), "still improving: never stops, however much work");
  check(!workGatedPlateau(flat, {0, 10, 20}, 3, 0, 0.01), "mismatched histories: never stops");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto runRegion = [&](int window, long long popsNeeded) {
    Design d = parse("pd14route_selftest_stuck.gr", row);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings rs;
    rs.enabled = true;
    rs.rounds = 40;
    rs.costWindow = window;
    rs.costPops = popsNeeded;
    return regionNegotiate(d, nets, rs, false);
  };
  RegionReport full = runRegion(0, 0);
  RegionReport again = runRegion(0, 0);
  check(full.roundsRun == 40 && !full.costStopped && full.pops > 0,
        "no window: all 40 rounds, " + std::to_string(full.pops) + " expansions counted");
  check(again.pops == full.pops, "the expansion count is deterministic (same fixture, same count)");
  long long perWindow = full.pops / 8; // 5 of 40 identical rounds
  RegionReport stop = runRegion(5, perWindow);
  check(stop.roundsRun == 5 && stop.costStopped && stop.after == full.after,
        "window 5, threshold = the work of 5 rounds: stops after exactly 5, same best (" +
            std::to_string(stop.roundsRun) + " rounds)");
  RegionReport cheap = runRegion(5, full.pops + 1);
  check(cheap.roundsRun == 40 && !cheap.costStopped,
        "window 5, threshold above all the work done: all 40 rounds (" + std::to_string(cheap.roundsRun) + ")");
  int w = 7;
  long long p = 9;
  std::string err;
  check(parseCostPlateau("100", "2000000000", w, p, err) && w == 100 && p == 2000000000LL,
        "work-gated settings parse exactly");
  check(parseCostPlateau(nullptr, nullptr, w, p, err) && w == 100, "unset settings leave values alone");
  check(!parseCostPlateau("-1", nullptr, w, p, err) && !parseCostPlateau(nullptr, "lots", w, p, err),
        "negative or non-numeric settings rejected");
}

// pd15route: the detour statistics on hand-built nets. Net A: pins (0,0),
// (3,0), wire straight along row 0 -> wire 3 = MST 3. Net B: pins (0,2),
// (2,2), wired up to row 4 and back (0,2)-(0,4)-(2,4)-(2,2) -> wire 6, MST
// 2, > 1.5x. Net C: three pins at (5,0), (7,0), (5,0) again -> MST counts
// distinct tiles: 2. A giant net is skipped.
void testDetourStats() {
  std::vector<RoutedNet> nets(4);
  nets[0].pts = {{0, 0}, {3, 0}};
  nets[0].charges = {{true, 0, 0, 3, 0, 1}};
  nets[1].pts = {{0, 2}, {2, 2}};
  nets[1].charges = {{false, 0, 2, 4, 0, 1}, {true, 4, 0, 2, 0, 1}, {false, 2, 2, 4, 0, 1}};
  nets[2].pts = {{5, 0}, {7, 0}, {5, 0}};
  nets[2].charges = {{true, 0, 5, 7, 0, 1}};
  nets[3].pts = {{0, 0}, {9, 9}};
  nets[3].giant = true;
  nets[3].charges = {{true, 0, 0, 9, 0, 1}};
  DetourStats st = detourStats(nets);
  check(st.nets == 3 && st.wire == 3 + 6 + 2 && st.mst == 3 + 2 + 2,
        "wire 11, pin-tile MST 7 over the three non-giant nets (got " + std::to_string(st.wire) + ", " +
            std::to_string(st.mst) + ")");
  check(st.netsHigh == 1 && st.excessHigh == 4, "one net over 1.5x its MST, carrying all 4 of the excess");
  check(pinTileMst({{0, 0}, {4, 0}, {4, 3}, {0, 3}}) == 10 && pinTileMst({{1, 1}, {1, 1}}) == 0,
        "MST of a 4 x 3 rectangle's corners 10; duplicate pins cost nothing");
}

// pd16route: the reclaim pass. On the 8 x 3 one-track fixture, net A runs
// (0,1) -> (7,1) the long way: down to row 0, along it, back up (wire 9,
// MST 7; ratio 1.2 and minimum excess 2 for this small case). With row 1
// free the pass reroutes it straight (wire 7) and keeps
// it. With net B occupying row 1 end to end (row 1 then has no room), A's
// straight route would overflow every edge, so A is left as it was (the
// search prefers its detour, and a shorter route that overflows is not worth
// it at mu 8). A net within the ratio is never a candidate.
void testDetourReclaim() {
  auto build = [&](bool withB, Design &d, std::vector<RoutedNet> &nets) {
    d = parse("pd16route_selftest_row.gr", kThreeOnOneRowGr);
    nets.assign(withB ? 2 : 1, RoutedNet{});
    nets[0].net = &d.nets[0];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 7, 0, 0}, {7, 0, 0, 7, 1, 0}};
    if (withB) {
      nets[1].net = &d.nets[1];
      nets[1].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
  };
  Design d1;
  std::vector<RoutedNet> n1;
  build(false, d1, n1);
  check(netWire(n1[0]) == 9 && pinTileMst(n1[0].pts) == 7, "fixture: A's wire 9 against an MST of 7");
  ReclaimSettings rs;
  rs.enabled = true;
  rs.ratio = 1.2; // 9 / 7 = 1.29: a candidate at 1.2 (not at the default 1.5)
  rs.minExcess = 2;
  ReclaimReport r1 = reclaimDetours(d1, n1, rs);
  check(r1.candidates == 1 && r1.kept == 1 && netWire(n1[0]) == 7 && segsConnected(n1[0].segs),
        "row 1 free: A rerouted straight (wire 7), kept, still connected (wire " + std::to_string(netWire(n1[0])) + ")");
  check(r1.overflowBefore == 0 && r1.overflowAfter == 0, "no overflow before or after");
  Design d2;
  std::vector<RoutedNet> n2;
  build(true, d2, n2);
  ReclaimReport r2 = reclaimDetours(d2, n2, rs);
  check(r2.candidates == 1 && r2.kept == 0 && netWire(n2[0]) == 9 && r2.overflowAfter == r2.overflowBefore,
        "row 1 full: A left as it was, overflow unchanged (kept " + std::to_string(r2.kept) + ")");
  Design d3;
  std::vector<RoutedNet> n3;
  build(false, d3, n3);
  ReclaimSettings loose = rs;
  loose.ratio = 1.4; // 9 > 1.4 * 7 = 9.8? no
  ReclaimReport r3 = reclaimDetours(d3, n3, loose);
  check(r3.candidates == 0 && netWire(n3[0]) == 9, "ratio 1.4: 9 is not > 9.8, A is not a candidate");
  ReclaimSettings p;
  std::string err;
  check(parseReclaimSettings("1", "2", "6", "0", "24", "3", p, err) && p.enabled && p.ratio == 2 && p.minExcess == 6 &&
            p.mu == 0 && p.margin == 24 && p.passes == 3,
        "reclaim settings parse exactly");
  check(!parseReclaimSettings("yes", nullptr, nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, "1", nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, "-1", nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, nullptr, "0", nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, nullptr, nullptr, "0", p, err),
        "bad flag, ratio <= 1, negative mu, margin 0, passes 0 rejected");
}

// pd17route: on a 16 x 5 one-track grid, nets A (x 0-5) and D (x 10-15)
// each run row 1 the long way (down to row 0 and back: wire 7, MST 5),
// while B and C share row 3 from x 10 to 15, overflowing it. With avoid
// off both A and D are rerouted straight; with avoid 2, D (its tiles 2 rows
// from the overflow) is skipped and only A is rerouted; with avoid 1 both
// are rerouted again (row 1 is 2 rows from row 3).
void testReclaimAvoidsOverflow() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  auto run = [&](int avoid, long long &avoided, long long &wireA, long long &wireD, long long &ovBefore) {
    Design d = parse("pd17route_selftest_avoid.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    ReclaimSettings rs;
    rs.enabled = true;
    rs.ratio = 1.2;
    rs.minExcess = 2;
    rs.avoid = avoid;
    ReclaimReport r = reclaimDetours(d, nets, rs);
    avoided = r.avoided;
    wireA = netWire(nets[0]);
    wireD = netWire(nets[1]);
    ovBefore = r.overflowBefore;
  };
  long long av, wa, wd, ov;
  run(-1, av, wa, wd, ov);
  check(ov > 0 && av == 0 && wa == 5 && wd == 5, "avoid off: overflow on row 3, both A and D rerouted straight (" +
                                                       std::to_string(wa) + ", " + std::to_string(wd) + ")");
  run(2, av, wa, wd, ov);
  check(av == 1 && wa == 5 && wd == 7, "avoid 2: D (2 rows from the overflow) skipped, only A rerouted");
  run(1, av, wa, wd, ov);
  check(av == 0 && wa == 5 && wd == 5, "avoid 1: row 1 is 2 rows from row 3, so neither is skipped");
  ReclaimSettings p;
  std::string err;
  check(parseReclaimAvoid("5", p, err) && p.avoid == 5 && parseReclaimAvoid(nullptr, p, err) && p.avoid == 5,
        "avoid parses; unset leaves it");
  check(!parseReclaimAvoid("-2", p, err) && !parseReclaimAvoid("near", p, err), "avoid below -1 or non-numeric rejected");
}

// pd18route: on pd9route's single-row fixture (three nets, 8 x 1, slow
// tier with keep), a stop threshold above the starting overflow stops the tier
// after slow round 0 (2 rounds run in all). With the reclaim at the stop
// on, the first stop runs the pass and the tier continues: more rounds run,
// one reclaim is reported, and every net is still connected. The settings
// parse and reject bad input.
void testSlowTierReclaim() {
  // pd9route's non-convergent fixture: three nets on a single row with
  // room for one, so overflow can never reach zero and the stop threshold
  // is what ends the tier.
  const char *gr = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                   "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  auto runOnce = [&](bool atStop) {
    Design d = parse("pd18route_selftest_row.gr", gr);
    SlowReclaim saved = gSlowReclaim;
    gSlowReclaim = SlowReclaim{};
    gSlowReclaim.atStop = atStop;
    gSlowReclaim.settings.enabled = true;
    gSlowReclaim.settings.ratio = 1.01;
    gSlowReclaim.settings.minExcess = 1;
    RunReport rep = route(d, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true, 8,
                          BoxGrowth{}, cs, AcceptMode::kKeep, true, 1000000);
    gSlowReclaim = saved;
    bool connected = rep.nets.size() == 3;
    for (const RoutedNet &rn : rep.nets)
      connected = connected && segsConnected(rn.segs);
    return std::make_tuple(rep.roundsRun, rep.slowReclaims, connected);
  };
  auto [roundsOff, reclaimsOff, okOff] = runOnce(false);
  auto [roundsOn, reclaimsOn, okOn] = runOnce(true);
  check(roundsOff == 2 && reclaimsOff == 0 && okOff, "no reclaim: the threshold stops the tier after slow round 0 (" +
                                                          std::to_string(roundsOff) + " rounds)");
  check(reclaimsOn == 1 && roundsOn == roundsOff + 1 && okOn,
        "reclaim at the stop: one pass, the tier continues (" + std::to_string(roundsOn) + " rounds), nets connected");
  SlowReclaim p;
  std::string err;
  check(parseSlowReclaim("1", "25", p, err) && p.atStop && p.every == 25, "slow-tier reclaim settings parse exactly");
  check(!parseSlowReclaim("on", nullptr, p, err) && !parseSlowReclaim(nullptr, "-1", p, err),
        "bad flag or negative period rejected");
}

// pd19route: the choice rule, then both paths on pd17route's 16 x 5
// fixture (A and D detoured, B and C sharing row 3). Whatever the region
// stage and refinement make of each path, the kept state is the better of
// the two by (overflow, then wire), and the report's numbers are the kept
// state's own.
void testReclaimFallback() {
  check(fallbackPrefersB(5, 100, 4, 200) && !fallbackPrefersB(4, 200, 5, 100), "lower overflow wins, whatever the wire");
  check(fallbackPrefersB(4, 200, 4, 199) && !fallbackPrefersB(4, 200, 4, 200) && !fallbackPrefersB(4, 199, 4, 200),
        "tied overflow: strictly less wire wins; a full tie keeps A");
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  Design d = parse("pd19route_selftest_fallback.gr", gr);
  std::vector<RoutedNet> nets(4);
  for (int i = 0; i < 4; ++i)
    nets[i].net = &d.nets[i];
  nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
  nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
  nets[2].segs = {{10, 3, 0, 15, 3, 0}};
  nets[3].segs = {{10, 3, 0, 15, 3, 0}};
  prepareFor2DRefine(d, nets);
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0; // the box stays on row 3, so path A's region stage leaves A's and D's detours alone
  RefineSettings refine;
  refine.iters = 5;
  FallbackReport fr = reclaimWithFallback(d, nets, rs, region, refine, false);
  long long ov = totalOverflow(d.grid, usageFromCharges(d.grid, nets)), w = totalWire2D(nets);
  long long wantOv = fr.tookB ? fr.overflowB : fr.overflowA, wantW = fr.tookB ? fr.wireB : fr.wireA;
  check(ov == wantOv && w == wantW, "the kept state carries the kept path's numbers (overflow " + std::to_string(ov) +
                                        ", wire " + std::to_string(w) + ")");
  check(fr.tookB == fallbackPrefersB(fr.overflowA, fr.wireA, fr.overflowB, fr.wireB) &&
            !fallbackPrefersB(fr.tookB ? fr.overflowB : fr.overflowA, fr.tookB ? fr.wireB : fr.wireA,
                              fr.tookB ? fr.overflowA : fr.overflowB, fr.tookB ? fr.wireA : fr.wireB),
        "the kept path is never worse than the other (A " + std::to_string(fr.overflowA) + "/" + std::to_string(fr.wireA) +
            ", B " + std::to_string(fr.overflowB) + "/" + std::to_string(fr.wireB) + ")");
  check(fr.reclaim.candidates == 2 && fr.tookB && fr.wireB < fr.wireA,
        "here both detours are reclaimable, so B wins on wire");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
}

// pd20route: on pd19route's fallback fixture, where B reaches zero, the
// B-first order keeps B without running A, and its kept state is byte for
// byte the both-paths order's.
void testFallbackBFirst() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  auto run = [&](bool bFirst, FallbackReport &fr) {
    Design d = parse("pd20route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    ReclaimSettings rs;
    rs.ratio = 1.2;
    rs.minExcess = 2;
    RegionSettings region;
    region.enabled = true;
    region.rounds = 20;
    region.expand = 0;
    RefineSettings refine;
    refine.iters = 5;
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, bFirst);
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    return out;
  };
  FallbackReport both, bfirst;
  auto segsBoth = run(false, both);
  auto segsB = run(true, bfirst);
  bool same = segsBoth.size() == segsB.size();
  for (size_t i = 0; same && i < segsBoth.size(); ++i) {
    same = segsBoth[i].size() == segsB[i].size();
    for (size_t k = 0; same && k < segsBoth[i].size(); ++k) {
      const Seg &a = segsBoth[i][k], &c = segsB[i][k];
      same = a.x1 == c.x1 && a.y1 == c.y1 && a.l1 == c.l1 && a.x2 == c.x2 && a.y2 == c.y2 && a.l2 == c.l2;
    }
  }
  check(both.ranA && both.tookB && !bfirst.ranA && bfirst.tookB && bfirst.overflowB == 0,
        "B reaches zero: the B-first order keeps B and never runs A");
  check(same && bfirst.wireB == both.wireB, "and the kept routes are identical to the both-paths order's");
}

// pd21route: on pd19route's fallback fixture A reaches 0 overflow (wire
// 26) and B 0 (wire 22). With threshold 0, A is at the threshold, B runs
// and wins, and the kept routes equal the both-paths order's. With a
// threshold of -1 the call is pd20route's (both paths). A case where A
// ends above the threshold: three nets on a one-track row (residue never
// clears) -- B is never run and A is kept.
void testFallbackAFirstThreshold() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0;
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](long long aMax, FallbackReport &fr) {
    Design d = parse("pd21route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, aMax);
    return totalWire2D(nets);
  };
  FallbackReport both, afirst;
  long long wBoth = run(-1, both), wA = run(0, afirst);
  check(afirst.ranA && afirst.ranB && afirst.tookB && wA == wBoth && afirst.overflowA == 0,
        "A ends at 0 <= threshold 0: B runs and wins, same kept wire as both paths (" + std::to_string(wA) + ")");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd21route_selftest_row.gr", row);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 0, 0, 7, 0, 0}};
  }
  prepareFor2DRefine(d, nets);
  FallbackReport stuck = reclaimWithFallback(d, nets, rs, region, refine, false, false, 10);
  check(stuck.overflowA > 10 && !stuck.ranB && !stuck.tookB,
        "A ends above the threshold (" + std::to_string(stuck.overflowA) + " > 10): B never runs, A kept");
}

// pd22route: the virtual capacity arithmetic, hand-checked: k units less,
// floored at ceil(cap / 2); zero / negative capacity and zero reduction
// unchanged.
void testVirtualCapArithmetic() {
  check(virtualCap(10, 4) == 6 && virtualCap(10, 0) == 10, "cap 10: reduction 4 -> 6, reduction 0 -> 10");
  check(virtualCap(10, 8) == 5 && virtualCap(11, 100) == 6 && virtualCap(1, 5) == 1,
        "never below ceil(cap / 2): 10-8 -> 5, 11-100 -> 6, 1-5 -> 1");
  check(virtualCap(0, 4) == 0 && virtualCap(-3, 2) == -3, "zero or negative capacity unchanged (stays forbidden)");
}

// pd22route: the cost seam. With no reduction vector the cost is the
// inherited one exactly; with one, it is presentDemandCost against the
// virtual capacity, and dearer.
void testVirtualCapCostSeam() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t eh = g.hIndex(1, 0, 0), ev = g.vIndex(0, 0, 0);
  u.h[eh] = 1;
  u.v[ev] = 1;
  double h0 = wireCostH(pr, eh, 1), v0 = wireCostV(pr, ev, 1);
  check(h0 == presentDemandCost(1, u.capH[eh], 1, 1, pr.pdIter, pr.pdFactor, pdWl(pr)),
        "no reduction vector: the inherited cost exactly");
  std::vector<int> redH(u.h.size(), 0), redV(u.v.size(), 0);
  redH[eh] = 1;
  redV[ev] = 1;
  pr.vredH = &redH;
  pr.vredV = &redV;
  double h1 = wireCostH(pr, eh, 1), v1 = wireCostV(pr, ev, 1);
  check(h1 == presentDemandCost(1, virtualCap(u.capH[eh], 1), 1, 1, pr.pdIter, pr.pdFactor, pdWl(pr)) && h1 > h0 &&
            v1 > v0,
        "with a reduction: priced against the virtual capacity, and dearer (h " + std::to_string(h0) + " -> " +
            std::to_string(h1) + ")");
  check(wireCostH(pr, g.hIndex(2, 0, 0), 1) == ([&] {
          Prices q(g);
          configureCostModel(q, u, cs);
          return wireCostH(q, g.hIndex(2, 0, 0), 1);
        })(),
        "an edge with reduction 0 costs exactly what it would without the mechanism");
}

// pd22route: the zone. One overflowed horizontal edge (10,10)-(11,10) on a
// 20 x 20 grid, radius 2: hot tiles x 8..13, y 8..12. Horizontal edges with
// an end hot: rows 8..12 x left ends 7..13 = 5 x 7 = 35. Vertical edges:
// columns 8..13 x lower ends 7..12 = 6 x 6 = 36. Total 71.
void testVirtualCapZone() {
  std::string gr = "grid 20 20 2\nvertical capacity 0 10\nhorizontal capacity 10 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 0\n0\n";
  Design d = parse("pd22route_selftest_zone.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Usage u(g);
  u.h[g.hIndex(10, 10, 0)] = u.capH[g.hIndex(10, 10, 0)] + 1;
  std::vector<int> redH, redV;
  long long n = virtualCapZone(g, u, 4, 2, redH, redV);
  check(n == 71, "71 edges in the zone by hand count (got " + std::to_string(n) + ")");
  check(redH[g.hIndex(7, 8, 0)] == 4 && redH[g.hIndex(13, 12, 0)] == 4 && redV[g.vIndex(8, 7, 0)] == 4 &&
            redV[g.vIndex(13, 12, 0)] == 4,
        "the zone's corner edges carry the reduction");
  check(redH[g.hIndex(6, 8, 0)] == 0 && redH[g.hIndex(10, 13, 0)] == 0 && redV[g.vIndex(7, 9, 0)] == 0 &&
            redV[g.vIndex(10, 13, 0)] == 0 && redH[g.hIndex(0, 0, 0)] == 0,
        "edges just outside the zone, and far away, do not");
  check(virtualCapZone(g, u, 0, 2, redH, redV) == 0 && redH[g.hIndex(10, 10, 0)] == 0, "reduction 0: no zone");
  Usage clean(g);
  check(virtualCapZone(g, clean, 4, 2, redH, redV) == 0, "nothing overflowed: no zone");
}

// pd22route: the decisive claim on the real search. An 8 x 3 grid, 2D
// capacity 10 everywhere, net amount 2, source (0,1), sink (7,1); row 1
// already carries demand 4. Straight along row 1: 7 edges at cong 6/10,
// cost 7 * (0.989 + 0.078) = 7.47. The detour through row 0: 9 edges at
// cong 0.2, ~8.9 -- so the search goes straight. With row 1 priced 4 units
// lower (capacity 6, cong 1): 7 * 1.989 = 13.9 > 8.9, so it detours.
void testVirtualCapSteersSearch() {
  std::string gr = "grid 8 3 2\nvertical capacity 0 10\nhorizontal capacity 10 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 0\n0\n";
  Design d = parse("pd22route_selftest_steer.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  for (int x = 0; x + 1 < g.nx; ++x)
    u.h[g.hIndex(x, 1, 0)] = 4;
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<PathNode> sources = {{0, 1, -1}};
  std::vector<Point> sinks = {{7, 1}};
  auto leavesRow1 = [](const MSMVResult &r) {
    for (const PathNode &p : r.path)
      if (p.y != 1)
        return true;
    return false;
  };
  MSMVResult plain = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(plain.found && !leavesRow1(plain), "without virtual capacity the route goes straight along row 1");
  std::vector<int> redH(u.h.size(), 0), redV(u.v.size(), 0);
  for (int x = 0; x + 1 < g.nx; ++x)
    redH[g.hIndex(x, 1, 0)] = 4;
  pr.vredH = &redH;
  pr.vredV = &redV;
  MSMVResult vc = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(vc.found && leavesRow1(vc), "with row 1 priced 4 units lower the route leaves it");
}

// pd22route: the region stage wires the zone in. pd21route's one-track row
// (three nets on a capacity-2 row, a residue that never clears): with 1
// track (2 units) and radius 0, the zone is the row's 7 edges; nets stay
// connected; with it off the report says 0.
void testRegionVirtualCapReported() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto run = [&](int tracks) {
    Design d = parse("pd22route_selftest_row.gr", row);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings region;
    region.enabled = true;
    region.rounds = 5;
    region.vcapTracks = tracks;
    region.vcapRadius = 0;
    RegionReport rr = regionNegotiate(d, nets, region, false);
    bool connected = true;
    for (const RoutedNet &rn : nets)
      connected = connected && segsConnected(rn.segs);
    check(connected, "every net still connects its pins (vcap " + std::to_string(tracks) + ")");
    return rr;
  };
  RegionReport on = run(1), off = run(0);
  check(on.vcapEdges == 7 && on.vcapReduction == 2, "1 track, radius 0: the row's 7 edges, 2 units each (got " +
                                                        std::to_string(on.vcapEdges) + ")");
  check(off.vcapEdges == 0 && off.vcapReduction == 0, "off: no zone");
}

// pd22route: the parallel fallback keeps exactly what the sequential order
// keeps -- B taken on pd19route's fixture (routes compared seg by seg),
// and A kept with B cancelled on the one-track row.
void testFallbackParallelMatchesSequential() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0;
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](const char *text, bool fixture, bool parallel, FallbackReport &fr) {
    Design d = parse("pd22route_selftest_parallel.gr", text);
    std::vector<RoutedNet> nets(fixture ? 4 : 3);
    for (size_t i = 0; i < nets.size(); ++i)
      nets[i].net = &d.nets[i];
    if (fixture) {
      nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
      nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
      nets[2].segs = {{10, 3, 0, 15, 3, 0}};
      nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    } else {
      for (RoutedNet &rn : nets)
        rn.segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, fixture ? 0 : 10, parallel);
    std::vector<std::vector<int>> out;
    for (const RoutedNet &rn : nets) {
      out.emplace_back();
      for (const Seg &s : rn.segs)
        out.back().insert(out.back().end(), {s.x1, s.y1, s.l1, s.x2, s.y2, s.l2});
    }
    return out;
  };
  FallbackReport seq, par, seqStuck, parStuck;
  auto a = run(gr, true, false, seq), b = run(gr, true, true, par);
  check(par.parallel && par.ranB && par.tookB == seq.tookB && seq.tookB && a == b && par.wireB == seq.wireB,
        "B reaches zero: the parallel order keeps B, routes identical to the sequential order's");
  auto c = run(row, false, false, seqStuck), e = run(row, false, true, parStuck);
  check(parStuck.parallel && !parStuck.ranB && !parStuck.tookB && c == e && parStuck.overflowA == seqStuck.overflowA &&
            parStuck.overflowA > 10,
        "A above the threshold: B cancelled and discarded, A kept as in the sequential order");
}

void testParseVirtualCap() {
  RegionSettings r;
  std::string err;
  check(parseVirtualCap(nullptr, nullptr, r, err) && r.vcapTracks == 0 && r.vcapRadius == 5, "unset: off, radius 5");
  check(parseVirtualCap("2", "8", r, err) && r.vcapTracks == 2 && r.vcapRadius == 8, "2 tracks, radius 8");
  RegionSettings s;
  check(!parseVirtualCap("-1", nullptr, s, err) && !parseVirtualCap("x", nullptr, s, err) &&
            !parseVirtualCap(nullptr, "1001", s, err) && s.vcapTracks == 0 && s.vcapRadius == 5,
        "negative, non-numeric or out-of-range values rejected, settings untouched");
}

// pd23route: with PD23ROUTE_VCAP_B=0 the reclaim path runs without virtual
// capacity -- its region report shows no zone and its result (wire) is
// exactly the no-virtual-capacity run's B -- while path A still gets the
// zone. pd19route's fixture, both paths (row 3 overflows at A's start).
void testVirtualCapPathAOnly() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](int tracks, bool vcapB) {
    Design d = parse("pd23route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    RegionSettings region;
    region.enabled = true;
    region.rounds = 20;
    region.expand = 0;
    region.vcapTracks = tracks;
    region.vcapRadius = 0;
    return reclaimWithFallback(d, nets, rs, region, refine, false, false, -1, false, vcapB);
  };
  FallbackReport off = run(0, true), aOnly = run(1, false), both = run(1, true);
  check(aOnly.regionA.vcapEdges > 0 && aOnly.regionB.vcapEdges == 0,
        "PD23ROUTE_VCAP_B=0: path A gets the zone (" + std::to_string(aOnly.regionA.vcapEdges) +
            " edges), path B none");
  check(aOnly.wireB == off.wireB && aOnly.overflowB == off.overflowB,
        "and path B's result is exactly the no-virtual-capacity run's (wire " + std::to_string(off.wireB) + ")");
  check(both.regionA.vcapEdges == aOnly.regionA.vcapEdges, "default (1): pd22route's behavior, path A's zone unchanged");
}

// pd24route: a congested synthetic design for the batched-round tests: a
// 24 x 24 grid, 2D capacity 4 per direction (2 nets of amount 2), and 220
// two- and three-pin nets placed by a fixed linear congruential generator --
// enough that overflow survives the slow tier into the region stage.
std::string batchedFixtureGr(int cap = 4, int netCount = 220) {
  std::string s = "grid 24 24 2\nvertical capacity 0 " + std::to_string(cap) + "\nhorizontal capacity " +
                  std::to_string(cap) + " 0\nminimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net " +
                  std::to_string(netCount) + "\n";
  unsigned long long st = 12345;
  auto rnd = [&](int m) {
    st = st * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<int>((st >> 33) % static_cast<unsigned long long>(m));
  };
  for (int k = 0; k < netCount; ++k) {
    int pins = 2 + (k % 3 == 0 ? 1 : 0);
    s += "n" + std::to_string(k) + " " + std::to_string(k) + " " + std::to_string(pins) + " 1\n";
    int cx = 6 + rnd(12), cy = 6 + rnd(12); // crowd the middle
    for (int p = 0; p < pins; ++p) {
      int x = std::min(23, std::max(0, cx + rnd(11) - 5)), y = std::min(23, std::max(0, cy + rnd(11) - 5));
      s += std::to_string(x * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    }
  }
  return s + "0\n";
}

std::vector<std::vector<int>> flatSegs(const std::vector<RoutedNet> &nets) {
  std::vector<std::vector<int>> out;
  for (const RoutedNet &rn : nets) {
    out.emplace_back();
    for (const Seg &s : rn.segs)
      out.back().insert(out.back().end(), {s.x1, s.y1, s.l1, s.x2, s.y2, s.l2});
  }
  return out;
}

// pd24route: the conflict rule, on the executor directly. An 8 x 3 grid,
// one net (amount 2) per edge per row; three nets from (0,1) to (7,1) all
// routed straight along row 1 (overflow 4 per edge). In one batch each net
// is searched against the same start state, where row 1 is over capacity
// and rows 0 and 2 are empty: all three choose the same free row. With the
// redo rule the second and third commits find their row now full, are
// rerouted on the live state, and the three spread over rows 0, 1, 2 --
// zero overflow. Accepting every route instead piles them onto one row.
void testBatchedRedoSpreadsAStampede() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\nnC 2 2 1\n5 15 1\n75 15 1\n0\n";
  bool passes = false;
  bool carryMode = false;
  auto run = [&](bool redo, int threads, BatchReport &br) {
    Design d = parse("pd24route_selftest_stampede.gr", gr);
    const RoutingGrid &g = d.grid;
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    Usage u = usageFromCharges(g, nets);
    Prices pr(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    SearchRegion reg;
    LayerPlan lp(g);
    ParallelSettings ps;
    ps.threads = threads;
    ps.batch = 3;
    ps.redo = redo;
    ps.passes = passes;
    ps.carry = carryMode;
    ps.fresh = carryMode;
    ps.checkReplicas = true;
    auto task = [&](Usage &uu, Prices &pp, SearchRegion &rr, const LayerPlan &ll, RoutedNet &rn) {
      rn.boxMargin = 2;
      routeNetSlowMSMV(g, pp, uu, ll, rr, 0.0, rn);
      return TaskStats{};
    };
    runBatchedRound(g, nets, {0, 1, 2}, u, pr, reg, lp, ps, task, br);
    bool connected = true;
    for (const RoutedNet &rn : nets)
      connected = connected && segsConnected(rn.segs);
    check(connected, "every net still connects its pins (redo " + std::to_string(redo) + ")");
    check(u.h == usageFromCharges(g, nets).h && u.v == usageFromCharges(g, nets).v,
          "the live usage equals the nets' charges after the batch");
    return std::make_pair(totalOverflow(g, u), flatSegs(nets));
  };
  BatchReport r1, r2, r3;
  auto redo1 = run(true, 1, r1), redo3 = run(true, 3, r2), jacobi = run(false, 1, r3);
  check(redo1.first == 0 && r1.redone >= 1,
        "redo: the stampede is caught and spread, overflow 0 (" + std::to_string(r1.redone) + " redone)");
  check(jacobi.first > 0 && r3.redone == 0,
        "accept-all: the three pile onto one row, overflow " + std::to_string(jacobi.first));
  check(redo1.second == redo3.second, "1 worker and 3 workers keep identical routes");
  // pd25route: the same with conflicts re-searched in parallel passes.
  passes = true;
  BatchReport q1, q3;
  auto pass1 = run(true, 1, q1), pass3 = run(true, 3, q3);
  check(pass1.first == 0 && q1.passes >= 1 && q1.redone >= 1,
        "passes: the conflicting nets are re-searched in a further pass and spread, overflow 0 (" +
            std::to_string(q1.passes) + " extra passes)");
  check(pass1.second == pass3.second && q1.passes == q3.passes, "passes: 1 and 3 workers keep identical routes");
  // pd27route: carried into a following pass and the "fresh" rule: the
  // nets chose rows that were empty at the pass's start, so the rule fires.
  carryMode = true;
  BatchReport c1, c3;
  auto car1 = run(true, 1, c1), car3 = run(true, 3, c3);
  check(car1.first == 0 && c1.redone >= 1,
        "carry + fresh: the stampede's conflicting nets are carried and spread, overflow 0 (" +
            std::to_string(c1.redone) + " carried)");
  check(car1.second == car3.second, "carry + fresh: 1 and 3 workers keep identical routes");
}

// pd24route: the decisive determinism claim on both batched stages. The
// synthetic fixture through route() (fast tier, then the batched slow tier
// with pieces, present-demand pricing, keep) and then the batched region
// stage: 1 worker and 4 workers give identical routes, replicas checked
// against the live state after every batch; and the batched run differs
// from the sequential one (the batches really ran).
void testBatchedThreadCountInvariance() {
  std::string gr = batchedFixtureGr();
  ParallelSettings saved = gParallel;
  long long regionBatches = 0;
  bool passesMode = false;
  bool carryMode = false;
  auto run = [&](int threads) {
    gParallel = ParallelSettings{};
    gParallel.threads = threads;
    gParallel.batch = 8;
    gParallel.passes = passesMode;
    gParallel.carry = carryMode;
    gParallel.fresh = carryMode;
    gParallel.checkReplicas = true;
    Design d = parse("pd24route_selftest_batched.gr", gr.c_str());
    BoxGrowth box;
    box.mode = BoxMode::Round;
    box.init = 4;
    box.grow = 1;
    box.cap = 20;
    CostSettings cost;
    cost.model = CostModel::kPresentDemand;
    RunReport rep = route(d, 3, 6, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, true, true, true, 8,
                          box, cost, AcceptMode::kKeep, true, -1, nullptr);
    long long slowOv = totalOverflow(d.grid, usageFromCharges(d.grid, rep.nets));
    RegionSettings region;
    region.enabled = true;
    region.rounds = 6;
    region.pieces = true;
    RegionReport rr = regionNegotiate(d, rep.nets, region, false);
    long long batches = rep.batches.batches + rr.batches.batches;
    regionBatches = rr.batches.batches;
    return std::make_tuple(flatSegs(rep.nets), slowOv, rr.after, batches);
  };
  auto seq = run(0), one = run(1), four = run(4);
  gParallel = saved;
  check(std::get<3>(one) > 0 && std::get<3>(four) == std::get<3>(one),
        "the batched stages ran (" + std::to_string(std::get<3>(one)) + " batches at 1 and 4 workers)");
  check(std::get<0>(one) == std::get<0>(four) && std::get<1>(one) == std::get<1>(four) &&
            std::get<2>(one) == std::get<2>(four),
        "1 worker and 4 workers: identical routes after the slow tier and the region stage (overflow " +
            std::to_string(std::get<1>(one)) + " -> " + std::to_string(std::get<2>(one)) + ")");
  check(std::get<1>(one) > 0 && regionBatches > 0,
        "overflow survives the slow tier, so the region stage ran batched too (" + std::to_string(regionBatches) +
            " region batches at 4 workers)");
  check(std::get<3>(seq) == 0, "threads 0: the sequential loops, no batches");
  // pd25route: the same invariance with conflicts re-searched in passes.
  passesMode = true;
  auto p1 = run(1), p4 = run(4);
  gParallel = saved;
  check(std::get<0>(p1) == std::get<0>(p4) && std::get<2>(p1) == std::get<2>(p4),
        "passes: 1 and 4 workers give identical routes through both stages");
  // pd27route: the same with conflicts carried and the "fresh" rule.
  carryMode = true;
  auto k1 = run(1), k4 = run(4);
  gParallel = saved;
  check(std::get<0>(k1) == std::get<0>(k4) && std::get<2>(k1) == std::get<2>(k4),
        "carry + fresh: 1 and 4 workers give identical routes through both stages");
}

void testParseParallel() {
  ParallelSettings p;
  std::string err;
  check(parseParallel(nullptr, nullptr, nullptr, p, err) && p.threads == 0 && p.batch == 64 && p.redo,
        "unset: sequential, batch 64, redo on");
  check(parseParallel("8", "32", "0", p, err) && p.threads == 8 && p.batch == 32 && !p.redo, "8 threads, batch 32, redo off");
  ParallelSettings q;
  check(!parseParallel("-1", nullptr, nullptr, q, err) && !parseParallel(nullptr, "0", nullptr, q, err) &&
            !parseParallel(nullptr, nullptr, "2", q, err) && !parseParallel("x", nullptr, nullptr, q, err),
        "negative threads, batch 0, a bad redo flag, non-numeric: rejected");
}

// pd26route: the tree rebuild and the WL recovery, batched: on the
// synthetic fixture after route() and the region stage, 1 and 4 workers
// give identical routes through both, and neither raises overflow (each
// stage fails loudly if it does), replicas checked after every batch.
void testBatchedCleanupThreadInvariance() {
  // Lighter than the slow-tier fixture (capacity 6, 160 nets), so the
  // negotiated routes carry detours for the cleanup stages to remove.
  std::string gr = batchedFixtureGr(6, 160);
  ParallelSettings saved = gParallel;
  auto run = [&](int threads, long long &rebuilt, long long &improved, long long &batches) {
    gParallel = ParallelSettings{};
    gParallel.threads = threads;
    gParallel.batch = 8;
    gParallel.checkReplicas = true;
    Design d = parse("pd26route_selftest_cleanup.gr", gr.c_str());
    BoxGrowth box;
    box.mode = BoxMode::Round;
    box.init = 4;
    box.grow = 1;
    box.cap = 20;
    CostSettings cost;
    cost.model = CostModel::kPresentDemand;
    RunReport rep = route(d, 3, 6, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, true, true, true, 8,
                          box, cost, AcceptMode::kKeep, true, -1, nullptr);
    RegionSettings region;
    region.enabled = true;
    region.rounds = 6;
    region.pieces = true;
    regionNegotiate(d, rep.nets, region, false);
    RebuildReport rb = rebuild2D(d, rep.nets, 3.0, RebuildSettings{}, false);
    WlSettings ws;
    WlReport wr = wlRecover2D(d, rep.nets, ws, false);
    rebuilt = rb.rebuilt;
    improved = wr.piecesImproved;
    batches = rb.batches.batches + wr.batches.batches;
    return flatSegs(rep.nets);
  };
  long long r1, i1, b1, r4, i4, b4;
  auto one = run(1, r1, i1, b1), four = run(4, r4, i4, b4);
  gParallel = saved;
  check(b1 > 0 && b1 == b4 && (r1 > 0 || i1 > 0),
        "the batched rebuild and WL recovery ran and changed routes (" + std::to_string(r1) + " rebuilt, " +
            std::to_string(i1) + " pieces improved, " + std::to_string(b1) + " batches)");
  check(one == four && r1 == r4 && i1 == i4, "1 and 4 workers: identical routes after the rebuild and the WL recovery");
}

// pd28route: the ring rectangle, on hotring's hand-checked fixture (Part
// 94): a 4 x 4 core at x 18-21 y 13-16, a warm band (0.85) two tiles
// thick, cool (0.3) elsewhere, one warm tile at (24,15). At 90%: x 15-24
// y 10-19; at 100% the right side passes the warm tile: x 15-25.
void testRingRect() {
  RoutingGrid g;
  g.nx = 40;
  g.ny = 30;
  std::vector<float> r(1200, 0.3f);
  for (int y = 11; y <= 18; ++y)
    for (int x = 16; x <= 23; ++x)
      r[static_cast<size_t>(y) * 40 + x] = 0.85f;
  for (int y = 13; y <= 16; ++y)
    for (int x = 18; x <= 21; ++x)
      r[static_cast<size_t>(y) * 40 + x] = 1.0f;
  r[static_cast<size_t>(15) * 40 + 24] = 0.8f;
  TileBox core{18, 13, 21, 16};
  TileBox m = ringRect(g, r, core, 0.75, 0.9), a = ringRect(g, r, core, 0.75, 1.0);
  check(m.x0 == 15 && m.x1 == 24 && m.y0 == 10 && m.y1 == 19, "ring at 90%: x 15-24 y 10-19");
  check(a.x0 == 15 && a.x1 == 25 && a.y0 == 10 && a.y1 == 19, "ring at 100%: the right side passes the warm tile, x 15-25");
}

// pd28route: the pull on a small design. 16 x 9 tiles, 2D capacity 10 per
// edge (5 nets of amount 2). Six nets run along row 4 over x 5-10 (12 >
// 10: the overflow); four nets each run along rows 3 and 5 over x 1-14
// (8 of 10, warm); everything else is empty (cool). With the region stage
// told to run no rounds, only the pull acts: it must take load off the warm
// rows (band mean use/capacity falls), add no overflow, and keep every net
// connected.
void testRingPullCoolsTheBand() {
  std::string gr = "grid 16 9 2\nvertical capacity 0 10\nhorizontal capacity 10 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 14\n";
  int k = 0;
  auto net = [&](int x1, int x2, int y) {
    gr += "n" + std::to_string(k) + " " + std::to_string(k) + " 2 1\n" + std::to_string(x1 * 10 + 5) + " " +
          std::to_string(y * 10 + 5) + " 1\n" + std::to_string(x2 * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    ++k;
  };
  for (int i = 0; i < 6; ++i)
    net(5, 10, 4);
  for (int i = 0; i < 4; ++i)
    net(1, 14, 3);
  for (int i = 0; i < 4; ++i)
    net(1, 14, 5);
  gr += "0\n";
  Design d = parse("pd28route_selftest_pull.gr", gr.c_str());
  std::vector<RoutedNet> nets(14);
  int rows[14] = {4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 5, 5, 5, 5};
  for (int i = 0; i < 14; ++i) {
    nets[i].net = &d.nets[i];
    int x1 = i < 6 ? 5 : 1, x2 = i < 6 ? 10 : 14;
    nets[i].segs = {{x1, rows[i], 0, x2, rows[i], 0}};
  }
  prepareFor2DRefine(d, nets);
  RegionSettings region;
  region.enabled = true;
  region.rounds = 0;
  region.expand = 0;
  region.ringShare = 0.9;
  region.ringPull = 4;
  region.ringMargin = 4;
  RegionReport rr = regionNegotiate(d, nets, region, false);
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(rr.ring.y0 <= 3 && rr.ring.y1 >= 5, "the ring rectangle covers both warm rows (y " + std::to_string(rr.ring.y0) +
                                                "-" + std::to_string(rr.ring.y1) + ")");
  check(rr.ringPieces > 0 && rr.warmEdges > 0 && rr.bandAfter < rr.bandBefore,
        "the pull moves load off the band's warm edges (" + std::to_string(rr.warmEdges) + " edges, mean use/cap " +
            std::to_string(rr.bandBefore) + " -> " + std::to_string(rr.bandAfter) + ")");
  check(rr.overflowAfterPull <= rr.overflowBeforePull && connected,
        "and adds no overflow (" + std::to_string(rr.overflowBeforePull) + " -> " + std::to_string(rr.overflowAfterPull) +
            "), every net still connected");
  RegionSettings off = region;
  off.ringShare = 0;
  std::vector<RoutedNet> n2(14);
  for (int i = 0; i < 14; ++i) {
    n2[i].net = &d.nets[i];
    int x1 = i < 6 ? 5 : 1, x2 = i < 6 ? 10 : 14;
    n2[i].segs = {{x1, rows[i], 0, x2, rows[i], 0}};
  }
  prepareFor2DRefine(d, n2);
  RegionReport r2 = regionNegotiate(d, n2, off, false);
  check(r2.ringPieces == 0 && r2.ring.x1 < r2.ring.x0, "off (PD28ROUTE_RING unset): no ring, no pull");
}

void testParseRing() {
  RegionSettings r;
  std::string err;
  check(parseRing(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, r, err) && r.ringShare == 0 && r.ringPull == 4 &&
            !r.ringBox,
        "unset: off, pull 4 tracks");
  check(parseRing("0.9", "0.7", "6", "2", "20", "1", r, err) && r.ringShare == 0.9 && r.ringCool == 0.7 && r.ringPull == 6 &&
            r.ringPasses == 2 && r.ringMargin == 20 && r.ringBox,
        "share 0.9, cool 0.7, pull 6, 2 passes, margin 20, ring box on");
  RegionSettings q;
  check(!parseRing("1.5", nullptr, nullptr, nullptr, nullptr, nullptr, q, err) &&
            !parseRing(nullptr, nullptr, nullptr, "0", nullptr, nullptr, q, err) &&
            !parseRing(nullptr, nullptr, nullptr, nullptr, nullptr, "yes", q, err),
        "share above 1, zero passes, a bad box flag: rejected");
}

// pd29route: the over-capacity penalty. Cost seam: an edge that one more
// net would push over capacity costs exactly P more; an edge with room, and
// every edge at P = 0, cost what they did.
void testOverPenaltyCostSeam() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Usage u(g);
  Prices pr(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t full = g.hIndex(1, 0, 0), roomy = g.hIndex(2, 0, 0);
  u.h[full] = u.capH[full];
  double f0 = wireCostH(pr, full, 1), r0 = wireCostH(pr, roomy, 1);
  pr.overPenalty = 7.5;
  check(wireCostH(pr, full, 1) == std::min(kForbiddenPrice, f0 + 7.5) && wireCostH(pr, roomy, 1) == r0,
        "penalty 7.5: the full edge costs exactly 7.5 more, the edge with room the same");
}

// pd29route: the decisive claim on the real search. A 5 x 3 grid, 2D
// capacity 60, nets of amount 2, from (0,1) to (4,1). Row 1 is empty but
// for the edge (2,1)-(3,1), already at 60. Straight: 3 empty edges + that
// edge at (62/60)^5 = 1.18 -> 4 x 0.989 + 1.18 = 5.14. The detour over
// row 2, whose edge (2,2)-(3,2) holds 58 (exactly full after this net):
// 5 empty edges + (60/60)^5 = 1 -> 6 x 0.989 + 1 = 6.93. Without the
// penalty the route crosses the full edge; with 5 it detours.
void testOverPenaltySteersSearch() {
  std::string gr = "grid 5 3 2\nvertical capacity 0 60\nhorizontal capacity 60 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 0\n0\n";
  Design d = parse("pd29route_selftest_penalty.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Usage u(g);
  Prices pr(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  u.h[g.hIndex(2, 1, 0)] = 60;
  u.h[g.hIndex(2, 2, 0)] = 58;
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<PathNode> sources = {{0, 1, -1}};
  std::vector<Point> sinks = {{4, 1}};
  auto crossesFull = [&](const MSMVResult &r) {
    for (size_t i = 1; i < r.path.size(); ++i)
      if (r.path[i - 1].y == 1 && r.path[i].y == 1 && std::min(r.path[i - 1].x, r.path[i].x) == 2 &&
          std::max(r.path[i - 1].x, r.path[i].x) == 3)
        return true;
    return false;
  };
  MSMVResult plain = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(plain.found && crossesFull(plain), "no penalty: the route crosses the full edge (5.14 < 6.93)");
  pr.overPenalty = 5;
  MSMVResult pen = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(pen.found && !crossesFull(pen), "penalty 5: the route detours around it (10.14 > 6.93)");
}

int run() {
  std::cout << "self test\n";
  testBasicShapesStayOnFastTier();
  testMultiSourceDijkstraUsesCheapestSourceNotFirstInList();
  testMultiSourceDijkstraStopsAtCheapestSink();
  testPriceClampPreventsInfinity();
  testHistoryIgnoresZeroCapacityEdges();
  testDisablingRelaxReproducesPermanentBehavior();
  testRelaxationUndoesExactlyOneStepPerCleanRound();
  testRelaxationFullyRestoresBaselineAfterEqualCleanRounds();
  testHistoryStreakRequiresConsecutiveOvercapacityRounds();
  testHistoryScalesWithOverflowMagnitude();
  testHistoryRatioModeMultipliesByUsageOverCapacityExactly();
  testHistoryRatioExponentSquaresTheRatio();
  testExponentEscalationTracksStackSizeAndRelaxesWithIt();
  testGrid2dCapacityIsExactPostAdjustmentSumAcrossLayers();
  testTwoDRoutingClearsAnOverflowNoSingleLayerChoiceCanClear();
  testFixedTopologySlowTierStillWorksAfterTheCollapse();
  testTwodOnlyFlagIsInertInThisGeneration();
  testMedianSteinerPointIsExact();
  testSteinerizationStrictlyReducesTotalSpan();
  testSteinerizationIsANoOpWhenTheHubIsAlreadyOptimal();
  testSteinerizedOutputIsAlwaysAValidTree();
  testDisablingSteinerizeReproducesPlainMstTopology();
  testEndToEndSteinerizationReducesRealChargedWirelength();
  testExactSteinerBeatsBothPlainMstAndCheapPassOnADegreeThreeHub();
  testExactSteinerMatchesHandVerifiedOptimaOnSmallFixtures();
  testExactSteinerOutputIsAlwaysAValidTree();
  testDegreeCapGatingExercisesFr3sFallbackExactly();
  testDisablingExactSteinerAloneFallsBackToCheapPassNotPlainMst();
  testEndToEndExactSteinerizationReducesRealChargedWirelength();
  testTwoPinAndAlreadyOptimalNetsAreUntouchedByTheExactSolve();
  testCapacityAwareLayerChoiceReactsToRealUsageNotJustPreference();
  testDirectionPreferenceIsNeverViolated();
  testViaStitchingConnectsAThreeWayLayerMismatch();
  testViaStitchingCatchesMidSpanMsmvAttachmentNotJustEndpoints();
  testMultiTileSegmentChargesEveryUnitEdgeNotJustOnce();
  testDegenerateZeroLengthSegmentsAreDropped();
  testTwodOnlyReproducesFr4routesExactSingleLayerOutput();
  testPinAccessLayerIsAlwaysReached();
  testEndToEndRoutingAndLayerAssignmentProducesAConnectedThreeDResult();
  testJointDPAvoidsAnAvoidableViaGreedyWouldCreate();
  testViaWeightKnobHasRealEffect();
  testViaCapacityIsChargedAndInfluencesLaterDecisions();
  testSharedViaStackReplacesOvercountedPairwiseChains();
  testTreeReconstructionSplitsAMidSpanAttachment();
  testTreeReconstructionIsAlwaysAValidTree();
  testPinAccessViaIsChargedIntoRealViaUsage();
  testViaAwareOffReproducesFr5routesExactBehavior();
  testChargeLogRoundTripIsExact();
  testNegotiationReducesViaOverflowASinglePassCannotFix();
  testViaHistoryRelaxDisabledReproducesPermanentBehavior();
  testViaHistoryRelaxationUndoesExactlyOneStepPerCleanRound();
  testSupervisedRevertPrimitivesDetectAndUndoAWorseningMove();
  testWireHistoryFixesRealOverflowASinglePassCannotFix();
  testWireHistoryRelaxDisabledReproducesPermanentBehavior();
  testWireHistoryRelaxationUndoesExactlyOneStepPerCleanRound();
  testWireAwareOffReproducesNv1routesLimitAndIsNotADeadFlag();
  testGiantNetJoinsFastTierAndEscapesAPermanentlyCongestedCorner();
  testRetunedCoefficientsProduceAMateriallyDifferentPricingCurve();
  testWiderBoxFindsDetourFixedMarginCannot();
  testBoxGrowthArithmetic();
  testBoxMarginModesInPhaseBHelpers();
  testParseBoxGrowthRejectsBadInput();
  testPdFactorScheduleMatchesNthu();
  testPresentDemandCostHandComputed();
  testPdCostZeroCapacityIsForbiddenAndFinite();
  testPdHistoryInvariantKeepsShrinkAboveFloor();
  testPdCostFallsTheMomentDemandLeaves();
  testLambdaModeIsTheInheritedCostExactly();
  testAStarStaysExactUnderPdPricing();
  testPdRouteReachesZeroOnADetourOnlyFixture();
  testParseCostSettings();
  testPdCongPowerByMultiplicationMatchesPow();
  testRevertRerouteDecision();
  testKeepModeReachesZeroOnTheDetourOnlyFixture();
  testParseAcceptMode();
  testSplitToUnitEdgesKeepsTheTreeAndItsTiles();
  testNthuLayerAssignmentSwitchesMidLegToAvoidOverflow();
  testLaNetOrderMatchesNthu();
  testReadRouteAs2DProjectsWiresAndDropsVias();
  testNthuLayerAssignmentEndToEndAndRoundTrip();
  testParseLaMode();
  testDropCyclesKeepsARingNetAndItsPin();
  testRefineCostCountsOnlyFullEdges();
  testRefineDetoursAroundFullEdgesRegardlessOfLength();
  testRefineOrderMatchesNthu();
  testPrepareFor2DRefineChargesExactlyTheWires();
  testRefine2DClearsTheThreeOnOneRowFixture();
  testParseRefineSettings();
  testRefineShuffleIsASeededPermutation();
  testRegionNegotiationClearsTheThreeOnOneRowFixture();
  testPieceRerouteMovesOnlyTheOverflowedPiece();
  testBoxHeuristicFindsTheSameCost();
  testPieceModeStagesClearTheThreeOnOneRowFixture();
  testBendAwareSearchCountsCorners();
  testWlRecoveryShortensADetour();
  testWlRecoveryNeverUsesAFullEdge();
  testWlRecoveryRemovesCornersAtEqualLength();
  testParseWlSettings();
  testPinAwareLayerAssignmentAvoidsPinStacks();
  testShapeCostCountsCornerTiles();
  testRebuildReplacesAWastefulTree();
  testRebuildKeepsAnOptimalTree();
  testSlowPiecesAndStopAt();
  testPatternFirstPieceReroute();
  testPatternStageSeparatesTwoNetsOntoTheTwoLs();
  testPlateauStop();
  testStageMarginCap();
  testWorkGatedPlateau();
  testDetourStats();
  testDetourReclaim();
  testReclaimAvoidsOverflow();
  testSlowTierReclaim();
  testReclaimFallback();
  testFallbackBFirst();
  testFallbackAFirstThreshold();
  testVirtualCapArithmetic();
  testVirtualCapCostSeam();
  testVirtualCapZone();
  testVirtualCapSteersSearch();
  testRegionVirtualCapReported();
  testFallbackParallelMatchesSequential();
  testParseVirtualCap();
  testVirtualCapPathAOnly();
  testBatchedRedoSpreadsAStampede();
  testBatchedThreadCountInvariance();
  testParseParallel();
  testBatchedCleanupThreadInvariance();
  testRingRect();
  testRingPullCoolsTheBand();
  testParseRing();
  testOverPenaltyCostSeam();
  testOverPenaltySteersSearch();
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}

} // namespace selftest

int main(int argc, char **argv) {
  std::ios::sync_with_stdio(false);

  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();

  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <solution.route>\n";
    std::cerr << "       " << argv[0] << " --selftest\n";
    return 2;
  }

  // pd24route: batched parallel rerouting.
  {
    std::string err;
    if (!parseParallel(std::getenv("PD24ROUTE_THREADS"), std::getenv("PD24ROUTE_BATCH"), std::getenv("PD24ROUTE_REDO"),
                       gParallel, err))
      fail(err);
    if (const char *v = std::getenv("PD25ROUTE_REDO_PASSES"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD25ROUTE_REDO_PASSES must be 0 or 1 (got '" + sv + "')");
      gParallel.passes = sv == "1";
    }
    if (const char *v = std::getenv("PD27ROUTE_CONFLICT"); v) {
      std::string sv(v);
      if (sv != "fresh" && sv != "any")
        fail("PD27ROUTE_CONFLICT must be fresh or any (got '" + sv + "')");
      gParallel.fresh = sv == "fresh";
    }
    if (const char *v = std::getenv("PD27ROUTE_CARRY"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD27ROUTE_CARRY must be 0 or 1 (got '" + sv + "')");
      gParallel.carry = sv == "1";
    }
  }

  int maxFastRounds = 60, maxSlowRounds = 150;
  double epsilon = 0.02;
  double histStep = 0.05;
  double histFactor = 1.1;
  HistMode histMode = HistMode::kAdditive;
  int streakThreshold = 1;
  bool scaleByOverflow = false;
  if (const char *e = std::getenv("VROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("VROUTE_FAST_ROUNDS"); r)
    maxFastRounds = std::atoi(r);
  if (const char *r = std::getenv("VROUTE_SLOW_ROUNDS"); r)
    maxSlowRounds = std::atoi(r);
  if (const char *s = std::getenv("VROUTE_HIST_STEP"); s)
    histStep = std::atof(s);
  if (const char *f = std::getenv("VROUTE_HIST_FACTOR"); f)
    histFactor = std::atof(f);
  if (const char *m = std::getenv("VROUTE_HIST_MODE"); m && std::string(m) == "mult")
    histMode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("VROUTE_HIST_MODE"); m && std::string(m) == "ratio")
    histMode = HistMode::kRatio;
  if (const char *s = std::getenv("VROUTE_HIST_STREAK"); s)
    streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("VROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
    scaleByOverflow = true;
  bool relaxEnabled = true;
  if (const char *r = std::getenv("VROUTE_HIST_RELAX"); r)
    relaxEnabled = std::string(r) != "0";
  double ratioExponent = 1.0;
  if (const char *r = std::getenv("VROUTE_HIST_RATIO_EXPONENT"); r)
    ratioExponent = std::atof(r);
  double exponentEscalation = 0.0;
  if (const char *r = std::getenv("VROUTE_HIST_EXPONENT_ESCALATION"); r)
    exponentEscalation = std::atof(r);
  bool useMSMV = true;
  if (const char *r = std::getenv("VROUTE_MSMV"); r)
    useMSMV = std::string(r) != "0";
  // fr3route's own mechanism: FR3ROUTE_STEINERIZE=0 reproduces fr1route's
  // exact plain-MST topology for direct comparison. Still governs the
  // fallback path below this file's own exact solve (either the nets it
  // skips by degree cap, or all nets if the exact solve itself is off).
  bool useSteinerize = true;
  if (const char *r = std::getenv("FR3ROUTE_STEINERIZE"); r)
    useSteinerize = std::string(r) != "0";
  // This file's own new mechanism: FR4ROUTE_EXACT_STEINER=0 disables the
  // exact Dreyfus-Wagner solve entirely, reproducing fr3route's own exact
  // behavior for direct comparison.
  bool useExactSteiner = true;
  if (const char *r = std::getenv("FR4ROUTE_EXACT_STEINER"); r)
    useExactSteiner = std::string(r) != "0";
  int maxExactPins = 8;
  if (const char *r = std::getenv("FR4ROUTE_EXACT_MAX_PINS"); r)
    maxExactPins = std::atoi(r);
  // This file's own mechanism: slow-tier search-box growth.
  BoxGrowth box;
  {
    std::string err;
    if (!parseBoxGrowth(std::getenv("NV11ROUTE_BOX_MODE"), std::getenv("NV11ROUTE_BOX_INIT"),
                        std::getenv("NV11ROUTE_BOX_GROW"), std::getenv("NV11ROUTE_BOX_CAP"), box,
                        err))
      fail(err);
  }
  // This file's own mechanism: the 2D pricing model.
  CostSettings cost;
  {
    std::string err;
    if (!parseCostSettings(std::getenv("PD1ROUTE_COST"), std::getenv("PD1ROUTE_WL_FLOOR"), cost, err))
      fail(err);
  }
  // pd3route's own mechanism: layer-assignment mode, and the
  // layer-assignment-only rerun from an existing route file.
  LaMode laMode;
  {
    std::string err;
    if (!parseLaMode(std::getenv("PD3ROUTE_LA"), laMode, err))
      fail(err);
  }
  const char *laFrom = std::getenv("PD3ROUTE_LA_FROM");
  // pd5route's own mechanism 2: hotspot rip-up.
  RegionSettings region;
  {
    std::string err;
    if (!parseRegionSettings(std::getenv("PD5ROUTE_REGION"), std::getenv("PD5ROUTE_REGION_EXPAND"),
                             std::getenv("PD5ROUTE_REGION_ROUNDS"), std::getenv("PD5ROUTE_REGION_BOX_INIT"),
                             std::getenv("PD5ROUTE_REGION_BOX_GROW"), region, err))
      fail(err);
  }
  // pd4route's own mechanism: the refinement stage.
  RefineSettings refine;
  {
    std::string err;
    if (!parseRefineSettings(std::getenv("PD4ROUTE_REFINE"), std::getenv("PD4ROUTE_REFINE_ITERS"),
                             std::getenv("PD4ROUTE_REFINE_BOX_INIT"), std::getenv("PD4ROUTE_REFINE_BOX_INC"),
                             std::getenv("PD4ROUTE_REFINE_ACCEPT"), refine, err) ||
        !parseRefineOrder(std::getenv("PD5ROUTE_REFINE_ORDER"), std::getenv("PD5ROUTE_SEED"), refine, err))
      fail(err);
  }
  // pd12route's own mechanism: the plateau stop for both stages.
  {
    std::string err;
    double gain = 0.01;
    long long minOverflow = 0;
    if (!parsePlateauSettings(std::getenv("PD12ROUTE_REGION_PLATEAU"), std::getenv("PD12ROUTE_REFINE_PLATEAU"),
                              std::getenv("PD12ROUTE_PLATEAU_GAIN"), std::getenv("PD12ROUTE_PLATEAU_MIN"),
                              region.plateau, refine.plateau, gain, minOverflow, err))
      fail(err);
    region.plateauGain = refine.plateauGain = gain;
    region.plateauMin = refine.plateauMin = minOverflow;
    // pd13route's own mechanism: the stage margin caps.
    if (!parseBoxCaps(std::getenv("PD13ROUTE_REGION_BOX_CAP"), std::getenv("PD13ROUTE_REFINE_BOX_CAP"), region.boxCap,
                      refine.boxCap, err))
      fail(err);
    // pd14route's own mechanism: the work-gated region plateau stop.
    if (!parseCostPlateau(std::getenv("PD14ROUTE_REGION_COST_WINDOW"), std::getenv("PD14ROUTE_REGION_COST_POPS"),
                          region.costWindow, region.costPops, err))
      fail(err);
  }
  if (laFrom && !*laFrom)
    fail("PD3ROUTE_LA_FROM is set but empty");
  // pd2route's own mechanism: slow-tier acceptance rule.
  AcceptMode accept;
  {
    std::string err;
    if (!parseAcceptMode(std::getenv("PD2ROUTE_ACCEPT"), accept, err))
      fail(err);
  }
  // This file's own new mechanism, finally making this long-reserved flag
  // functional: FR1ROUTE_TWOD_ONLY=1 skips negotiateViaAssignment() entirely,
  // reproducing fr4route's exact single-layer, zero-via output.
  bool twoDOnly = std::getenv("FR1ROUTE_TWOD_ONLY") != nullptr;

  // This file's own new mechanism: the via-negotiation loop's own knobs,
  // independent of the 2D stage's VROUTE_HIST_* above (separate ViaPrices
  // instance) -- see the file header for each one's role. Carried over
  // from nv1route's own NV1ROUTE_* vars, renamed NV2ROUTE_* for this
  // file's own functional reads.
  double viaWeight = 1.0;
  if (const char *r = std::getenv("NV2ROUTE_VIA_WEIGHT"); r)
    viaWeight = std::atof(r);
  HistKnobs viaKnobs;
  if (const char *s = std::getenv("NV2ROUTE_HIST_STEP"); s)
    viaKnobs.step = std::atof(s);
  if (const char *f = std::getenv("NV2ROUTE_HIST_FACTOR"); f)
    viaKnobs.factor = std::atof(f);
  if (const char *m = std::getenv("NV2ROUTE_HIST_MODE"); m && std::string(m) == "mult")
    viaKnobs.mode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("NV2ROUTE_HIST_MODE"); m && std::string(m) == "ratio")
    viaKnobs.mode = HistMode::kRatio;
  if (const char *s = std::getenv("NV2ROUTE_HIST_STREAK"); s)
    viaKnobs.streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("NV2ROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
    viaKnobs.scaleByOverflow = true;
  if (const char *r = std::getenv("NV2ROUTE_HIST_RELAX"); r)
    viaKnobs.relaxEnabled = std::string(r) != "0";
  if (const char *r = std::getenv("NV2ROUTE_HIST_RATIO_EXPONENT"); r)
    viaKnobs.ratioExponent = std::atof(r);
  if (const char *r = std::getenv("NV2ROUTE_HIST_EXPONENT_ESCALATION"); r)
    viaKnobs.exponentEscalation = std::atof(r);
  int viaRounds = 60;
  if (const char *r = std::getenv("NV2ROUTE_VIA_ROUNDS"); r)
    viaRounds = std::atoi(r);
  int viaPatience = 20;
  if (const char *r = std::getenv("NV2ROUTE_VIA_PATIENCE"); r)
    viaPatience = std::atoi(r);

  // This file's own new mechanism: the wire-history knobs, same roles as
  // the via ones above, independent WirePrices instance.
  HistKnobs wireKnobs;
  if (const char *s = std::getenv("NV2ROUTE_WIRE_HIST_STEP"); s)
    wireKnobs.step = std::atof(s);
  if (const char *f = std::getenv("NV2ROUTE_WIRE_HIST_FACTOR"); f)
    wireKnobs.factor = std::atof(f);
  if (const char *m = std::getenv("NV2ROUTE_WIRE_HIST_MODE"); m && std::string(m) == "mult")
    wireKnobs.mode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("NV2ROUTE_WIRE_HIST_MODE"); m && std::string(m) == "ratio")
    wireKnobs.mode = HistMode::kRatio;
  if (const char *s = std::getenv("NV2ROUTE_WIRE_HIST_STREAK"); s)
    wireKnobs.streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("NV2ROUTE_WIRE_HIST_SCALE"); s && std::string(s) == "overflow")
    wireKnobs.scaleByOverflow = true;
  if (const char *r = std::getenv("NV2ROUTE_WIRE_HIST_RELAX"); r)
    wireKnobs.relaxEnabled = std::string(r) != "0";
  if (const char *r = std::getenv("NV2ROUTE_WIRE_HIST_RATIO_EXPONENT"); r)
    wireKnobs.ratioExponent = std::atof(r);
  if (const char *r = std::getenv("NV2ROUTE_WIRE_HIST_EXPONENT_ESCALATION"); r)
    wireKnobs.exponentEscalation = std::atof(r);

  auto t0 = std::chrono::steady_clock::now();
  // pd26route: stage timers.
  auto tRoute0 = t0;
  auto secsSince = [](std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
  };
  double sRoute = 0, sRegion = 0, sRebuild = 0, sWl = 0, sLa = 0;
  Design d = readGr(argv[1]);
  // pd11route's own mechanism: the batched pattern stage.
  PatternStageSettings pattern;
  {
    if (const char *v = std::getenv("PD11ROUTE_PATTERN"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD11ROUTE_PATTERN must be 0 or 1 (got '" + sv + "')");
      pattern.enabled = sv == "1";
    }
    auto num = [&](const char *name, int lo, int &dst) {
      if (const char *v = std::getenv(name); v) {
        char *end = nullptr;
        long x = std::strtol(v, &end, 10);
        if (end == v || *end != '\0' || x < lo || x > 1000000)
          fail(std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')");
        dst = static_cast<int>(x);
      }
    };
    num("PD11ROUTE_PATTERN_ROUNDS", 1, pattern.rounds);
    num("PD11ROUTE_PATTERN_CELL", 1, pattern.cell);
  }
  // pd10route's own mechanism: pattern-first piece reroutes (default on).
  patternFirst = true;
  if (const char *v = std::getenv("PD10ROUTE_PATTERN_FIRST"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD10ROUTE_PATTERN_FIRST must be 0 or 1 (got '" + sv + "')");
    patternFirst = sv == "1";
  }
  // pd9route's own mechanisms: search-stage piece reroutes and a threshold stop.
  bool slowPieces = false;
  long long stopAt = -1;
  if (const char *v = std::getenv("PD9ROUTE_SLOW_PIECES"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD9ROUTE_SLOW_PIECES must be 0 or 1 (got '" + sv + "')");
    slowPieces = sv == "1";
  }
  if (const char *v = std::getenv("PD9ROUTE_STOP_AT"); v) {
    char *end = nullptr;
    long long x = std::strtoll(v, &end, 10);
    if (end == v || *end != '\0' || x < 0)
      fail(std::string("PD9ROUTE_STOP_AT must be an integer >= 0 (got '") + v + "')");
    stopAt = x;
  }
  // pd18route's own mechanism: the slow tier's reclaim settings.
  {
    std::string err;
    ReclaimSettings rs;
    if (!parseReclaimSettings(nullptr, std::getenv("PD16ROUTE_RECLAIM_RATIO"), std::getenv("PD16ROUTE_RECLAIM_MIN_EXCESS"),
                              std::getenv("PD16ROUTE_RECLAIM_MU"), std::getenv("PD16ROUTE_RECLAIM_MARGIN"),
                              std::getenv("PD16ROUTE_RECLAIM_PASSES"), rs, err) ||
        !parseReclaimAvoid(std::getenv("PD17ROUTE_RECLAIM_AVOID"), rs, err) ||
        !parseSlowReclaim(std::getenv("PD18ROUTE_RECLAIM_AT_STOP"), std::getenv("PD18ROUTE_RECLAIM_EVERY"), gSlowReclaim,
                          err))
      fail(err);
    rs.enabled = true;
    gSlowReclaim.settings = rs;
  }
  RunReport rep;
  if (laFrom) {
    rep.nets = readRouteAs2D(d, laFrom);
    prepareFor2DRefine(d, rep.nets);
  } else {
    tRoute0 = std::chrono::steady_clock::now();
    rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation,
                useMSMV, useSteinerize, useExactSteiner, maxExactPins, box, cost, accept, slowPieces, stopAt,
                &pattern);
  }
  sRoute = secsSince(tRoute0);
  auto tRegion0 = std::chrono::steady_clock::now();
  detourTrace("slow tier", rep.nets);
  // pd7route's own mechanism: pin-aware layer assignment (PD7ROUTE_LA_PINS,
  // default 1; 0 reproduces pd6route's layer assignment).
  bool laPins = true;
  if (const char *v = std::getenv("PD7ROUTE_LA_PINS"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD7ROUTE_LA_PINS must be 0 or 1 (got '" + sv + "')");
    laPins = sv == "1";
  }
  // pd8route's own mechanism: whole-net tree rebuild.
  RebuildSettings rebuild;
  {
    auto flag = [&](const char *name, bool &dst) {
      if (const char *v = std::getenv(name); v) {
        std::string sv(v);
        if (sv != "0" && sv != "1")
          fail(std::string(name) + " must be 0 or 1 (got '" + sv + "')");
        dst = sv == "1";
      }
    };
    auto num = [&](const char *name, int lo, int &dst) {
      if (const char *v = std::getenv(name); v) {
        char *end = nullptr;
        long x = std::strtol(v, &end, 10);
        if (end == v || *end != '\0' || x < lo || x > 1000000)
          fail(std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')");
        dst = static_cast<int>(x);
      }
    };
    flag("PD8ROUTE_REBUILD", rebuild.enabled);
    num("PD8ROUTE_REBUILD_MARGIN", 0, rebuild.margin);
    num("PD8ROUTE_REBUILD_MAX_PINS", 2, rebuild.maxPins);
  }
  // pd7route's own mechanism: wirelength and bend recovery.
  WlSettings wl;
  {
    std::string err;
    if (!parseWlSettings(std::getenv("PD7ROUTE_WL"), std::getenv("PD7ROUTE_WL_PASSES"), std::getenv("PD7ROUTE_WL_BEND"),
                         std::getenv("PD7ROUTE_WL_MARGIN"), wl, err))
      fail(err);
  }
  // pd6route: PD6ROUTE_PIECES=1 switches both stages to 2-pin-piece reroutes.
  if (const char *pv = std::getenv("PD6ROUTE_PIECES"); pv) {
    std::string v(pv);
    if (v != "0" && v != "1")
      fail("PD6ROUTE_PIECES must be 0 or 1 (got '" + v + "')");
    refine.pieces = region.pieces = v == "1";
  }
  // pd22route's own mechanisms: virtual capacity in the region stage, and
  // the fallback's paths on two threads.
  {
    std::string err;
    if (!parseVirtualCap(std::getenv("PD22ROUTE_VCAP"), std::getenv("PD22ROUTE_VCAP_RADIUS"), region, err))
      fail(err);
    if (const char *v = std::getenv("PD29ROUTE_OVER_PENALTY"); v) {
      char *end = nullptr;
      double x = std::strtod(v, &end);
      if (end == v || *end != '\0' || !(x >= 0 && x <= 1e6))
        fail(std::string("PD29ROUTE_OVER_PENALTY must be a number 0-1e6 (got '") + v + "')");
      region.overPenalty = x;
    }
    if (!parseRing(std::getenv("PD28ROUTE_RING"), std::getenv("PD28ROUTE_RING_COOL"), std::getenv("PD28ROUTE_RING_PULL"),
                   std::getenv("PD28ROUTE_RING_PASSES"), std::getenv("PD28ROUTE_RING_MARGIN"),
                   std::getenv("PD28ROUTE_RING_BOX"), region, err))
      fail(err);
  }
  // pd24route: batched parallel rerouting (parsed here; the slow tier ran
  // above, so its settings are parsed earlier -- see the top of main()).
  // pd23route: PD23ROUTE_VCAP_B (0 | 1; unset = 1).
  bool vcapB = true;
  if (const char *v = std::getenv("PD23ROUTE_VCAP_B"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD23ROUTE_VCAP_B must be 0 or 1 (got '" + sv + "')");
    vcapB = sv == "1";
  }
  bool fallbackParallel = false;
  if (const char *v = std::getenv("PD22ROUTE_FALLBACK_PARALLEL"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD22ROUTE_FALLBACK_PARALLEL must be 0 or 1 (got '" + sv + "')");
    fallbackParallel = sv == "1";
  }
  // pd19route's own mechanism: the reclaim with a fallback.
  bool reclaimFallback = false;
  if (const char *v = std::getenv("PD19ROUTE_RECLAIM_FALLBACK"); v) {
    std::string sv(v);
    if (sv != "0" && sv != "1")
      fail("PD19ROUTE_RECLAIM_FALLBACK must be 0 or 1 (got '" + sv + "')");
    reclaimFallback = sv == "1";
  }
  RegionReport regRep;
  RefineReport refRep;
  if (reclaimFallback) {
    ReclaimSettings reclaim;
    std::string err;
    if (!parseReclaimSettings(nullptr, std::getenv("PD16ROUTE_RECLAIM_RATIO"), std::getenv("PD16ROUTE_RECLAIM_MIN_EXCESS"),
                              std::getenv("PD16ROUTE_RECLAIM_MU"), std::getenv("PD16ROUTE_RECLAIM_MARGIN"),
                              std::getenv("PD16ROUTE_RECLAIM_PASSES"), reclaim, err) ||
        !parseReclaimAvoid(std::getenv("PD17ROUTE_RECLAIM_AVOID"), reclaim, err))
      fail(err);
    bool bFirst = false;
    if (const char *v = std::getenv("PD20ROUTE_FALLBACK_ORDER"); v) {
      std::string sv(v);
      if (sv != "both" && sv != "bfirst")
        fail("PD20ROUTE_FALLBACK_ORDER must be both or bfirst (got '" + sv + "')");
      bFirst = sv == "bfirst";
    }
    long long aMax = -1;
    if (const char *v = std::getenv("PD21ROUTE_FALLBACK_A_MAX"); v) {
      char *end = nullptr;
      long long x = std::strtoll(v, &end, 10);
      if (end == v || *end != '\0' || x < -1)
        fail(std::string("PD21ROUTE_FALLBACK_A_MAX must be an integer >= -1 (got '") + v + "')");
      aMax = x;
    }
    if (fallbackParallel && aMax < 0)
      fail("PD22ROUTE_FALLBACK_PARALLEL=1 needs PD21ROUTE_FALLBACK_A_MAX >= 0");
    FallbackReport fr = reclaimWithFallback(d, rep.nets, reclaim, region, refine, std::getenv("VROUTE_TRACE") != nullptr,
                                            bFirst, aMax, fallbackParallel, vcapB);
    if (fr.parallel)
      std::cerr << "reclaim fallback: path B ran on a second thread (its per-round trace suppressed)\n";
    if (region.ringShare > 0)
      std::cerr << "ring pull (path A): ring " << (fr.regionA.ring.x1 - fr.regionA.ring.x0 + 1) << " x "
                << (fr.regionA.ring.y1 - fr.regionA.ring.y0 + 1) << " tiles, " << fr.regionA.ringPieces
                << " pieces rerouted (" << fr.regionA.ringReverted << " reverted), the band's " << fr.regionA.warmEdges
                << " warm edges' mean use/cap "
                << fr.regionA.bandBefore << " -> " << fr.regionA.bandAfter << ", core " << fr.regionA.coreBefore << " -> "
                << fr.regionA.coreAfter << ", 2D overflow " << fr.regionA.overflowBeforePull << " -> "
                << fr.regionA.overflowAfterPull << " (" << fr.regionA.ringSeconds << "s)\n";
    if (region.vcapTracks > 0)
      std::cerr << "region virtual capacity: path A " << fr.regionA.vcapEdges << " edges, path B "
                << fr.regionB.vcapEdges << " edges, " << region.vcapTracks << " tracks (" << fr.regionA.vcapReduction
                << " units), radius " << region.vcapRadius << "\n";
    std::cerr << "reclaim fallback: path A (standard) region " << fr.regionA.before << " -> " << fr.regionA.after
              << " (" << fr.regionA.roundsRun << " rounds, " << fr.regionA.seconds << "s), refinement -> "
              << fr.refineA.after << "; path B (reclaim " << fr.reclaim.kept << " of " << fr.reclaim.candidates
              << ", 2D overflow " << fr.reclaim.overflowBefore << " -> " << fr.reclaim.overflowAfter << ") region "
              << fr.regionB.before << " -> " << fr.regionB.after << " (" << fr.regionB.roundsRun << " rounds, "
              << fr.regionB.seconds << "s), refinement -> " << fr.refineB.after << "\n";
    if (!fr.ranB)
      std::cerr << "reclaim fallback: A overflow " << fr.overflowA << " wire " << fr.wireA
                << " above PD21ROUTE_FALLBACK_A_MAX -> kept A (standard); B not run\n";
    else if (fr.ranA)
      std::cerr << "reclaim fallback: A overflow " << fr.overflowA << " wire " << fr.wireA << ", B overflow "
                << fr.overflowB << " wire " << fr.wireB << " -> kept " << (fr.tookB ? "B (reclaim)" : "A (standard)")
                << "\n";
    else
      std::cerr << "reclaim fallback: B overflow 0 wire " << fr.wireB << " -> kept B (reclaim); A not run\n";
    regRep = fr.tookB ? fr.regionB : fr.regionA;
    refRep = fr.tookB ? fr.refineB : fr.refineA;
    detourTrace("reclaim fallback (kept path)", rep.nets);
  }
  // pd16route's own mechanism: the detour reclaim pass, before the region stage.
  if (!reclaimFallback) {
    ReclaimSettings reclaim;
    std::string err;
    if (!parseReclaimSettings(std::getenv("PD16ROUTE_RECLAIM"), std::getenv("PD16ROUTE_RECLAIM_RATIO"),
                              std::getenv("PD16ROUTE_RECLAIM_MIN_EXCESS"), std::getenv("PD16ROUTE_RECLAIM_MU"),
                              std::getenv("PD16ROUTE_RECLAIM_MARGIN"), std::getenv("PD16ROUTE_RECLAIM_PASSES"), reclaim,
                              err) ||
        !parseReclaimAvoid(std::getenv("PD17ROUTE_RECLAIM_AVOID"), reclaim, err))
      fail(err);
    if (reclaim.enabled) {
      ReclaimReport rr = reclaimDetours(d, rep.nets, reclaim);
      std::cerr << "detour reclaim: " << rr.kept << " of " << rr.candidates << " candidate reroutes kept ("
                << rr.avoided << " skipped near overflow), 2D wire "
                << rr.wireBefore << " -> " << rr.wireAfter << ", 2D overflow " << rr.overflowBefore << " -> "
                << rr.overflowAfter << " (" << rr.seconds << "s)\n";
      detourTrace("detour reclaim", rep.nets);
    }
  }
  // pd5route: hotspot rip-up first (if on), then refinement polishes.
  if (region.enabled && !reclaimFallback) {
    regRep = regionNegotiate(d, rep.nets, region, std::getenv("VROUTE_TRACE") != nullptr);
    detourTrace("region stage", rep.nets);
    std::cerr << "region: box x " << regRep.box.x0 << "-" << regRep.box.x1 << " y " << regRep.box.y0 << "-"
              << regRep.box.y1 << ", " << regRep.regionNets << " nets, 2D overflow " << regRep.before
              << " -> " << regRep.after << " (" << regRep.roundsRun << " rounds"
              << (regRep.plateaued ? ", plateau stop" : "") << (regRep.costStopped ? ", work-gated plateau stop" : "")
              << ", " << regRep.seconds << "s, " << regRep.pops << " search expansions"
              << (region.vcapTracks > 0 ? ", virtual capacity on " + std::to_string(regRep.vcapEdges) + " edges" : "")
              << ")\n";
  }
  if (refine.enabled && !reclaimFallback)
    refRep = refine2D(d, rep.nets, refine, std::getenv("VROUTE_TRACE") != nullptr);
  if (refine.enabled && !reclaimFallback)
    detourTrace("refinement", rep.nets);
  sRegion = secsSince(tRegion0);
  auto tRebuild0 = std::chrono::steady_clock::now();
  // pd8route: whole-net tree rebuild, before piece recovery polishes.
  if (rebuild.enabled) {
    RebuildReport rb = rebuild2D(d, rep.nets, wl.bend, rebuild, std::getenv("VROUTE_TRACE") != nullptr);
    detourTrace("tree rebuild", rep.nets);
    std::cerr << "tree rebuild: 2D wire " << rb.wireBefore << " -> " << rb.wireAfter << " (" << rb.rebuilt
              << " nets rebuilt)\n";
  }
  sRebuild = secsSince(tRebuild0);
  auto tWl0 = std::chrono::steady_clock::now();
  // pd7route: overflow-safe wirelength and bend recovery, last 2D stage.
  WlReport wlRep;
  if (wl.enabled) {
    wlRep = wlRecover2D(d, rep.nets, wl, std::getenv("VROUTE_TRACE") != nullptr);
    detourTrace("WL recovery", rep.nets);
    std::cerr << "wl recovery: 2D wire " << wlRep.wireBefore << " -> " << wlRep.wireAfter << " ("
              << wlRep.piecesImproved << " pieces improved)\n";
  }
  sWl = secsSince(tWl0);
  auto tLa0 = std::chrono::steady_clock::now();
  ViaNegotiationReport viaRep;
  if (!twoDOnly)
    viaRep = negotiateViaAssignment(d.grid, rep.nets, viaRounds, viaPatience, viaWeight, viaKnobs,
                                     wireKnobs, laMode, laPins);
  sLa = secsSince(tLa0);
  std::cerr << "stage seconds: route (pattern, fast, slow) " << sRoute << " [pattern + fast " << rep.secondsBeforeSlow
            << ", slow " << rep.secondsSlow << "], region + refinement " << sRegion
            << ", tree rebuild " << sRebuild << ", WL recovery " << sWl << ", layer assignment " << sLa << "\n";
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " nets, " << rep.escalatedCount
            << " escalated to the slow tier\n";
  // "2D overflow" here is still the 2D-collapsed search's own convergence
  // metric (measured against the summed cross-layer capacity, Usage::capH/
  // capV) -- it is what the 2D pricing loop actually converges against, and
  // remains meaningful as that. It is a different number from the real
  // per-layer total overflow (wire + via) the negotiation loop's own report
  // tracks below, which is what gr_verify reports on this file's actual
  // output (unless FR1ROUTE_TWOD_ONLY is set, in which case the output is
  // fr4route's old single-layer one and neither number below is comparable,
  // same as every prior generation).
  if (laFrom) {
    std::cerr << "layer assignment only (PD3ROUTE_LA_FROM=" << laFrom << "), "
              << (laMode == LaMode::kNthu ? "nthu" : "inherited") << " mode\n";
  } else {
    std::cerr << "2D overflow" << (twoDOnly ? " (FR1ROUTE_TWOD_ONLY set: no layer assignment ran)" : "")
              << ": " << rep.bestOverflow << " (" << rep.roundsRun << " total rounds run)\n";
    std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                       : rep.stalled ? "stalled: no improvement for "
                                                         "several rounds, stopping\n"
                                                     : "hit the round cap\n");
  }
  if (refine.enabled)
    std::cerr << "refinement: 2D overflow " << refRep.before << " -> " << refRep.after << " ("
              << refRep.itersRun << " iterations run" << (refRep.plateaued ? ", plateau stop" : "") << ", "
              << refRep.seconds << "s)\n";
 if (gParallel.threads > 0)
    std::cerr << "parallel: " << gParallel.threads << " threads, batch " << gParallel.batch << ", redo "
              << gParallel.redo << "; slow tier " << rep.batches.batches << " batches (" << rep.batches.committed
              << " committed, " << rep.batches.redone << " redone, " << rep.batches.unchanged << " unchanged); region "
              << regRep.batches.batches << " batches (" << regRep.batches.committed << " committed, "
              << regRep.batches.redone << " redone); conflict passes: slow tier " << rep.batches.passes << ", region "
              << regRep.batches.passes << "\n";
 if (piecesTotal > 0)
    std::cerr << "piece reroutes: " << piecesTotal << ", by L/Z pattern (no maze): " << patternedTotal << "\n";
  if (!twoDOnly) {
    std::cerr << "real (wire+via) overflow: " << viaRep.bestOverflow << " (best at round "
              << viaRep.bestRound << " of " << viaRep.roundsRun << " run)\n";
    std::cerr << (viaRep.convergedToZero
                      ? "via negotiation converged to zero overflow\n"
                      : viaRep.stalled ? "via negotiation stalled: no improvement for "
                                          "several rounds, stopping\n"
                                       : "via negotiation hit the round cap\n");
  }
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
