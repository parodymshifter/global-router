// nv2route.cpp
//
// Forked from nv1route.cpp -- inherits the entire negotiated-via-pricing
// mechanism (NetCharge/undoCharge/applyCharge, ViaPrices/updateViaHistory,
// the DP's spanCost via-cost term, negotiateViaAssignment's own round loop
// with selective rip-up and supervised accept/revert, and the wire-only
// overflow metric fix) completely unchanged; see nv1route.cpp's own header
// for that mechanism's full derivation. Species `nv`, generation 2 -- a
// direct refinement (a second resource's own history term added to the
// existing round loop), not a new core algorithm, per CLAUDE.md's naming
// convention. Scoped in
// /Users/bobboyd/.claude/plans/async-shimmying-pancake.md.
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
//     clang++ -std=c++17 -O2 -Wall -Wextra -o nv2route nv2route.cpp
//
// Use:
//     ./nv2route benchmark.gr solution.route
//     ./nv2route --selftest
//
// Environment variables (all optional; every VROUTE_*/KROUTE_*/FR1ROUTE_*/
// FR3ROUTE_*/FR4ROUTE_* var below behaves exactly as in nv1route.cpp and
// its own ancestors, since the 2D collapse, MSMV, phases, history, and
// topology construction are unchanged):
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
//                                 default 0.2
//     VROUTE_POWER=<float>        fast-tier curve escalation power, default 2.5
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
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "gr_io.h"

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
};
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

// `threshold` is a parameter, not a fixed static lookup, specifically so
// the cleanup pass can price nets against a more relaxed curve than normal
// routing uses -- matching nproute.cpp's actual current architecture
// (which grew this parameter to support round-scheduling experiments that
// didn't pan out, but the parameter itself is exactly what a relaxed
// cleanup pass needs).
double priceExponentTerm(double usage, double cap, double threshold) {
  static const double power = [] {
    const char *p = std::getenv("VROUTE_POWER");
    return p ? std::atof(p) : 2.5;
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
    return t ? std::atof(t) : 0.2;
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
};

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
      cost += edgeCost(pr.h[idx], pr.histH[idx], amount);
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
      cost += edgeCost(pr.v[idx], pr.histV[idx], amount);
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

  auto heuristic = [&](int x, int y) {
    return static_cast<double>(std::abs(x - p2.x) + std::abs(y - p2.y));
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
        double w = edgeCost(prices.h[e], prices.histH[e], amount);
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
        double w = edgeCost(prices.v[e], prices.histV[e], amount);
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

DijkstraResult routeSegment(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                             SearchRegion &region, Point p1, int p1Layer, Point p2, int width) {
  static const int maxMargin = [] {
    const char *m = std::getenv("VROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int span = std::abs(p1.x - p2.x) + std::abs(p1.y - p2.y);
  int margin = std::min(maxMargin, std::max(4, span / 8));
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
MSMVResult multiSourceDijkstra(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                                SearchRegion &region, const std::vector<PathNode> &sources,
                                const std::vector<Point> &sinks, int width, int margin) {
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

  auto heuristic = [&](int x, int y) {
    int best = std::numeric_limits<int>::max();
    for (const Point &s : sinks)
      best = std::min(best, std::abs(x - s.x) + std::abs(y - s.y));
    return static_cast<double>(best);
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
        double w = edgeCost(prices.h[e], prices.histH[e], amount);
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
        double w = edgeCost(prices.v[e], prices.histV[e], amount);
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

MSMVResult routeStepMSMV(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                          SearchRegion &region, const std::vector<PathNode> &sources,
                          const std::vector<Point> &sinks, int width) {
  static const int maxMargin = [] {
    const char *m = std::getenv("VROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int margin = maxMargin;
  for (int attempt = 0; attempt < 6; ++attempt) {
    MSMVResult r = multiSourceDijkstra(g, prices, lp, region, sources, sinks, width, margin);
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

    MSMVResult r = routeStepMSMV(g, prices, lp, region, sourceSet, sinks, rn.net->minWidth);
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
    DijkstraResult r = routeSegment(g, prices, lp, region, p1, p1Layer, p2, rn.net->minWidth);
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

struct RunReport {
  std::vector<RoutedNet> nets;
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1;
  int roundsRun = 0;
  int escalatedCount = 0;
  bool convergedToZero = false;
  bool stalled = false;
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

RunReport route(const Design &d, int maxFastRounds, int maxSlowRounds, double epsilon,
                 double histStep, HistMode histMode, double histFactor,
                 int streakThreshold = 1, bool scaleByOverflow = false,
                 bool relaxEnabled = true, double ratioExponent = 1.0,
                 double exponentEscalation = 0.0, bool useMSMV = true,
                 bool useSteinerize = true, bool useExactSteiner = true,
                 int maxExactPins = 8) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  Usage usage(g);
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

  // Phase A: nproute's algorithm, unmodified in spirit.
  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxFastRounds; ++round) {
    for (RoutedNet &rn : nets) {
      if (rn.giant)
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
    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    if (trace)
      std::cerr << "fast round " << round << " overflow " << overflow << "\n";
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

  if (rep.bestOverflow == 0) {
    rep.convergedToZero = true;
    return rep;
  }

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
  for (int round = 0; round < maxSlowRounds; ++round) {
    int accepted = 0, rejected = 0;
    for (RoutedNet &rn : nets) {
      if (rn.giant)
        continue;
      if (!rn.escalated && netOverflowed(g, usage, rn))
        rn.escalated = true;
      if (!rn.escalated)
        continue; // fast tier already has this net legal; leave it alone
      if (round > 0 && !netOverflowed(g, usage, rn))
        continue; // already resolved by an earlier round's accepted move

      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);

      if (useMSMV)
        routeNetSlowMSMV(g, prices, usage, lp, region, epsilon, rn);
      else
        routeNetSlow(g, prices, usage, lp, region, epsilon, rn);

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
        ++rejected;
      } else {
        ++accepted;
      }
    }
    // Same round-boundary rule as phase A: the supervised accept/reject
    // decisions for every escalated net this round are final by this
    // point, so this is the settled state to judge "over capacity" against.
    updateHistory(g, prices, usage, histStep, histMode, histFactor, streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation);
    long long overflow = totalOverflow(g, usage);
    rep.roundsRun += 1;
    if (trace)
      std::cerr << "slow round " << round << " overflow " << overflow << " (accepted "
                << accepted << ", rejected " << rejected << ")\n";
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
    if (accepted == 0 || roundsSinceImprovement >= kSlowPatience) {
      rep.stalled = true;
      break;
    }
  }
  rep.escalatedCount = 0;
  for (const RoutedNet &rn : rep.nets)
    if (rn.escalated)
      ++rep.escalatedCount;
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
bool buildNetTree(const std::vector<Seg> &segs, NetTree &t) {
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
      if (!addEdge(prevNode, pr.second, horizontal, cross, prevCoord, pr.first))
        return false;
      prevNode = pr.second;
      prevCoord = pr.first;
    }
    int lastNode = nodeFor(horizontal ? hi : cross, horizontal ? cross : hi);
    if (!addEdge(prevNode, lastNode, horizontal, cross, prevCoord, hi))
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
bool layerAssignDP(const RoutingGrid &g, RealUsage &ru, RealViaUsage &rvu,
                    const std::vector<int> &viaCap, const RealLayerPlan &lp, int width,
                    const NetTree &t, std::vector<int> &edgeLayer, int &rootLayer,
                    std::vector<std::pair<int, int>> &nodeStackRange, double viaWeight = 1.0,
                    const std::vector<double> *historyVia = nullptr,
                    const std::vector<double> *historyWireH = nullptr,
                    const std::vector<double> *historyWireV = nullptr) {
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
  auto resolveNode = [&](int x, int y, int ref, const std::vector<int> &children, double &outTotal,
                          Range &outRange, std::vector<int> &outChoice) -> bool {
    outTotal = kInf;
    bool any = false;
    for (int lo = 0; lo <= ref; ++lo) {
      for (int hi = ref; hi < L; ++hi) {
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
      if (!resolveNode(t.nodes[u].x, t.nodes[u].y, l, children, nodeTotal, range, choice))
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
    if (!resolveNode(t.nodes[0].x, t.nodes[0].y, r, rootChildren, total, range, choice))
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
ViaNegotiationReport negotiateViaAssignment(const RoutingGrid &g, std::vector<RoutedNet> &nets,
                                             int maxRounds = 60, int patience = 20,
                                             double viaWeight = 1.0,
                                             HistKnobs viaKnobs = HistKnobs(),
                                             HistKnobs wireKnobs = HistKnobs()) {
  ViaNegotiationReport rep;
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
  for (size_t i = 0; i < nets.size(); ++i) {
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
    if (buildNetTree(rn.segs, trees[i]) &&
        layerAssignDP(g, ru, rvu, viaCap, lp, rn.net->minWidth, trees[i], edgeLayer, rootLayer,
                      nodeStackRange, viaWeight, &vp.hist, wireHistH, wireHistV)) {
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
      if (!netOverflowedVia(g, ru, rvu, viaCap, charges[i]))
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
                              rootLayer, nodeStackRange, viaWeight, &vp.hist, wireHistH, wireHistV);
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
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                         streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent,
                         exponentEscalation, useMSMV, useSteinerize, useExactSteiner,
                         maxExactPins);
  ViaNegotiationReport viaRep;
  if (!twoDOnly)
    viaRep = negotiateViaAssignment(d.grid, rep.nets, viaRounds, viaPatience, viaWeight, viaKnobs,
                                     wireKnobs);
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
  std::cerr << "2D overflow" << (twoDOnly ? " (FR1ROUTE_TWOD_ONLY set: no layer assignment ran)" : "")
            << ": " << rep.bestOverflow << " (" << rep.roundsRun << " total rounds run)\n";
  std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                     : rep.stalled ? "stalled: no improvement for "
                                                       "several rounds, stopping\n"
                                                   : "hit the round cap\n");
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
