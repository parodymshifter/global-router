// fr4route.cpp
//
// Forked from fr3route.cpp -- inherits the 2D collapse (fr1route), vroute's
// MSMV maze growth, kroute's LIFO-relaxable history, and fr3route's own
// cheap degree-2 median Steinerization all completely unchanged; see those
// files' own headers for those mechanisms. Generation 4 of the `fr` species
// (CLAUDE.md's "Router naming convention" addendum). Unlike fr2route and
// fr3route, this one forks from fr3route rather than fr1route: it is not
// an independent alternative to fr3's Steinerization, it is a strict
// refinement of it (exact-solve the nets fr3's cheap pass structurally
// can't help, keep fr3's own proven cheap pass as the fallback for the
// rest) -- see the plan this was scoped from,
// /Users/bobboyd/.claude/plans/async-shimmying-pancake.md, for the full
// reasoning on that deviation.
//
// Why: fr3route's cheap, degree-2-only median Steinerization was a real,
// unanimous win over fr1route's plain-MST baseline (adaptec1 -3.75%,
// newblue1 -8.04%, bigblue1 -11.68%, HISTORY_COST_SWEEP.md Part 23), but
// mixed against the user's own "dramatic" (double-digit, all three)
// criterion, and it deliberately never touches degree>=3 branch points in
// the MST -- by construction, only a genuine Hanan-grid Steiner-tree
// algorithm (what FLUTE's lookup tables approximate) can improve those.
// Per the user's own stated sequence, this is the "try full FLUTE" step.
//
// Framing decision, disclosed up front: literal FLUTE is a *lookup-table
// speed hack* -- fast because Chu's original work precomputed, via an
// offline exhaustive/POWV search, the optimal Steiner topology for every
// terminal configuration up to degree 9, then ships that table. Honestly
// reproducing that table from scratch is a separate, multi-week research
// artifact, and this project's own conventions rule out vendoring an
// external dependency or datafile ("C++17, no dependencies beyond the
// standard library", CLAUDE.md). What this file builds instead is the
// **exact rectilinear Steiner minimal tree (RSMT)**, for the same degree
// range FLUTE targets (bounded terminal count, default <=8), via the
// classical Dreyfus-Wagner Steiner-tree dynamic program over the Hanan
// grid -- slower per net than a table lookup, but exact rather than
// looked-up-approximate, achieving the same practical goal (near-/fully-
// optimal small-net Steiner trees) FLUTE exists to deliver, from scratch,
// with no external data.
//
// The mechanism: for a net with unique terminal count k (already
// deduplicated by route()'s own net-construction loop before this runs),
// build the Hanan grid (every (x,y) where x is one terminal's x-coordinate
// and y is one terminal's y-coordinate -- k^2 candidate Steiner points; a
// classical result guarantees an optimal RSMT is realizable using only
// these). Under Manhattan distance this candidate set is a complete graph,
// so a standard Dreyfus-Wagner DP applies directly: dp[mask][v] is the
// minimum-length tree spanning terminal subset `mask` and touching Hanan
// vertex v, built by (1) a subset-split merge -- dp[mask][v] = min over
// nonempty proper submasks sub of dp[sub][v] + dp[mask\sub][v] -- then (2)
// a relax pass (an O(V^2) array Dijkstra over the complete Hanan-vertex
// graph, seeded with the merge step's own dp[mask][*] values) that lets a
// meeting point migrate to a cheaper nearby vertex. The final answer is
// min over v of dp[full_mask][v]; a parallel traceback table records which
// split or relax-edge produced each entry, so the actual tree (not just
// its cost) can be reconstructed recursively. New function
// `exactSteinerTree(pts) -> SteinerResult` (reusing fr3route's own
// SteinerResult{pts, edges} struct); every downstream function already
// operates generically on rn.pts/rn.mst, so no other code changes were
// needed to route a genuinely different topology, same as fr3route's own
// integration.
//
// Degree gating, matching FLUTE's own effective working range and this
// project's layered-fallback convention: k<=2 skips the DP entirely (no
// Steiner point can ever help 2 terminals -- mstEdges alone is already
// exact); 3<=k<=FR4ROUTE_EXACT_MAX_PINS (default 8) runs the exact DP;
// k above that cap falls through to fr3route's own inherited
// steinerizeTree on the plain MST (still gated by the inherited
// FR3ROUTE_STEINERIZE flag) -- never the bare plain MST when
// Steinerization is enabled at all, since fr3's cheap pass is already a
// proven win and there's no reason to discard it for the nets the exact
// solve skips.
//
// FR4ROUTE_EXACT_STEINER=0 disables the exact solve entirely, reproducing
// fr3route's own exact behavior (which itself reproduces fr1route's
// plain-MST when FR3ROUTE_STEINERIZE=0 too) -- the same layered "strict
// superset, each switch reproduces its parent" pattern every generation in
// this lineage uses. FR4ROUTE_EXACT_MAX_PINS sets the degree cap.
//
// No new lambda/hist/price/log/div formulas are introduced by this
// mechanism -- it is a purely geometric/combinatorial construction over
// integer Manhattan distances, so none of CLAUDE.md's "arithmetic edge
// cases" apply beyond what fr1route's own inherited pricing loop already
// tests.
//
// Self-tests include an exact, hand-derived decisive proof that the exact
// DP reaches a topology fr3route's cheap pass structurally cannot: on the
// fixture A=(0,3), B=(4,3), C=(2,6), D=(2,0), Prim's MST (from A) picks
// A-B (4), A-C (5), A-D (5) -- A ends up degree 3 in the MST, so fr3's
// degree-2-only pass is a no-op here and total span stays 14. The true
// exact Steiner point S=(2,3) connects all four at cost 2+2+3+3=10, a
// 28.6% reduction the cheap pass cannot reach on this fixture by
// construction -- reproduced end to end through the real production
// route() pipeline, not just exactSteinerTree in isolation. See this
// file's self-test namespace for the full suite (DP-vs-brute-force cross
// checks, degree-cap fallback-path exercise, tree-validity, and the
// FR4ROUTE_EXACT_STEINER=0 regression guard).
//
// Real result, default settings, same round budget convention as
// fr1route's and fr3route's own baseline, all three real ISPD
// benchmarks -- all legal per gr_verify (0 malformed/out-of-bounds/
// disconnected/incomplete/unrouted; its own "total overflow" verdict is
// not the comparable metric here, same as every prior fr*route entry --
// no layer assignment exists yet, so gr_verify's real-3D-capacity
// overflow check does not apply):
//
//   benchmark   fr1route (bar)   fr3route   fr4route   fr4 vs fr1   fr4 vs fr3
//   adaptec1    69394            66792      63966      -7.82%       -4.23%
//   newblue1    20040            18428      18156      -9.40%       -1.48%
//   bigblue1    207612           183362     180060      -13.27%     -1.80%
//
// A real, unanimous improvement over both prior variants -- no
// regression anywhere, essentially unchanged runtime. Against fr1route's
// "dramatic" bar, closer than fr3route was (bigblue1 crosses into double
// digits) but still not a clean sweep. Against fr3route specifically
// (the more informative comparison, since this file is a strict
// refinement of it): the incremental gain from exact-solving degree>=3
// branch points is real but modest (-1.48% to -4.23%) -- fr3route's
// cheap pass had already captured most of the low-hanging Steiner
// benefit on these benchmarks.
//
// **Decision, per the user's own explicit instruction ("see which of
// the variants gets to the lowest overflow and take that one to full
// layer assignment"): this file.** fr4route has the strictly lowest 2D
// overflow of all four variants (fr1/fr2/fr3/fr4) on every one of the
// three real benchmarks. See HISTORY_COST_SWEEP.md Part 24 for the full
// record. This is the topology carried forward into the full
// layer-assignment stage next.

// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o fr4route fr4route.cpp
//
// Use:
//     ./fr4route benchmark.gr solution.route
//     ./fr4route --selftest
//
// Environment variables (all optional; every VROUTE_*/KROUTE_*/
// FR1ROUTE_*/FR3ROUTE_* var below behaves exactly as in fr3route.cpp/
// fr1route.cpp/vroute.cpp/kroute.cpp, since the 2D collapse, MSMV, phase
// A/B, history, and cheap Steinerization fallback are unchanged):
//     FR4ROUTE_EXACT_STEINER=0|1  this file's own new mechanism, default 1
//                                 (enabled). 0 reproduces fr3route's exact
//                                 behavior (itself still gated by
//                                 FR3ROUTE_STEINERIZE) for direct
//                                 comparison.
//     FR4ROUTE_EXACT_MAX_PINS=<int>
//                                 degree cap for the exact DP, default 8.
//                                 Nets with more unique pins than this
//                                 fall through to fr3route's cheap
//                                 Steinerization instead.
//     FR3ROUTE_STEINERIZE=0|1     fr3route's own mechanism, default 1
//                                 (enabled). Governs the fallback path
//                                 for nets the exact solve skips (either
//                                 by degree cap or FR4ROUTE_EXACT_STEINER=0).
//     FR1ROUTE_TWOD_ONLY=1         reserved for a future layer-assignment
//                                 generation; still inert here (no such
//                                 stage exists yet).
//     VROUTE_MSMV=0|1              default 1. Selects the slow tier's
//                                 pathfinder: MSMV incremental growth
//                                 (default) or the inherited fixed
//                                 2D-MST-over-the-Steinerized-point-set
//                                 topology. Both operate on whatever
//                                 topology this file's Steinerization
//                                 pipeline produced.
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
//
// Known risks, disclosed up front (see the plan this was scoped from for
// the full discussion):
//   - Not literal FLUTE -- an exact from-scratch RSMT solver over the same
//     degree range FLUTE targets, not a reproduction of FLUTE's own
//     precomputed lookup tables. If a literal lookup-table algorithm is
//     wanted specifically (e.g. to match a published runtime profile),
//     this file does not deliver that.
//   - Runtime, not quality, is the open risk: the DP is exact and
//     provably at least as good as fr3route's cheap pass on every net it
//     covers, so a quality regression is not expected, but O(3^k) per net
//     for the (rare) 7-8-pin nets, multiplied across a whole benchmark, is
//     unmeasured until the real-benchmark run below.
//   - The degree cap (8) is a runtime/complexity choice (keeps the Hanan
//     grid at V<=64), not a quality ceiling discovered empirically.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
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
  // Reserved for a future layer-assignment generation, where it will
  // skip that generation's new stage -- this generation has no such
  // stage, so the variable is read (to fail loudly if ever misspelled
  // matters later) but does not change route()'s own behavior. See
  // testTwodOnlyFlagIsInertInThisGeneration.
  bool twoDOnly = std::getenv("FR1ROUTE_TWOD_ONLY") != nullptr;

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                         streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent,
                         exponentEscalation, useMSMV, useSteinerize, useExactSteiner,
                         maxExactPins);
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " nets, " << rep.escalatedCount
            << " escalated to the slow tier\n";
  // Labeled "2D overflow" rather than vroute's own "best overflow" --
  // this number means something different from every prior router's own
  // overflow count: it is measured against the 2D-collapsed, summed
  // cross-layer capacity (Usage::capH/capV), not per-layer 3D capacity,
  // and the output .route file places every segment on layer 1 (there is
  // no layer assignment in this generation yet -- see the file header).
  // Not directly comparable to vroute's own recorded numbers until
  // fr2route's layer assignment produces a real 3D result.
  std::cerr << "2D overflow" << (twoDOnly ? " (FR1ROUTE_TWOD_ONLY set, no effect yet)" : "")
            << ": " << rep.bestOverflow << " (" << rep.roundsRun << " total rounds run)\n";
  std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                     : rep.stalled ? "stalled: no improvement for "
                                                       "several rounds, stopping\n"
                                                   : "hit the round cap\n");
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
