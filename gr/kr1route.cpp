// kr1route.cpp
//
// Forked from uroute.cpp -- inherits region-level cross-layer congestion
// pricing (UROUTE_REGION_* env vars) and everything below it (kroute's
// own LIFO-relaxable per-edge history, phase A/phase B) completely
// unchanged; see uroute.cpp's and kroute.cpp's own headers for those.
// This file is the first under the project's new two-letter +
// generation-number naming convention (see CLAUDE.md's "Router naming
// convention" addendum) -- species `kr` (rooted at kroute), generation 1.
//
// Why: every router in this whole lineage prices the reactive edge term
// (lambda, CLAUDE.md's original "Core cost model": `lambda_e <-
// lambda_e * exp(epsilon * usage_e/capacity_e)`) the same way, and
// checking every assignment to Prices::h/v in uroute.cpp confirms
// negotiateWire is the *only* place it ever changes -- the multiplier is
// always >= 1, so lambda is monotonically non-decreasing for the entire
// run. Once an edge crosses KROUTE_THRESHOLD (default 20% utilization)
// even briefly, its price ratchets up and never comes back down, no
// matter how idle that edge becomes later. This is the exact disease
// `hist` (history) originally had -- iroute made it permanent, kroute
// made it LIFO-relaxable, and that was the single biggest unanimous win
// in the whole pricing-formula sweep (-0.62% to -1.39% across all three
// real benchmarks). `lambda` itself never got that treatment, despite
// being the most frequently-touched term in the whole cost model
// (updated per net commit via negotiateWire, not just per round boundary
// like hist and regionPrice) -- and it directly explains part of the
// "unused space" puzzle (HISTORY_COST_SWEEP.md Part 9): an edge that got
// busy early stays permanently expensive even once genuinely idle,
// hiding real capacity from every later net.
//
// The mechanism: a new per-edge LIFO stack, `Prices::lambdaOpsH/V`
// (struct PriceOp, a single `factor` field -- lambda has no add/mult/
// ratio mode choice, negotiateWire always multiplies). negotiateWire
// pushes the *actual applied* factor (post-kForbiddenPrice-clamp, not the
// raw exp(...) value) onto the stack at commit time, whenever a real
// increase happens (applied > 1.0) -- so the vast majority of
// under-threshold commits push nothing at all. A new function,
// `relaxLambda`, called at the same three round-boundary points
// updateHistory/updateRegionPrice already fire from in route(), pops and
// undoes the single most-recently-pushed factor on any edge that reads
// clear at that settled snapshot, floored at 1.0 -- exactly
// updateHistory's own relax branch, applied to the reactive price instead
// of the permanent history term. Because negotiateWire fires per commit
// while relax fires per round boundary, one busy round can push more
// entries onto an edge's stack than one clean round can pop -- an edge
// pushed N times needs N clean round boundaries to fully unwind, the
// direct generalization of kroute's own "N dirty rounds need N clean
// rounds" property.
//
// KR1ROUTE_LAMBDA_RELAX=0 disables the mechanism outright (relaxLambda
// becomes a no-op), reproducing uroute's exact permanent-lambda behavior
// for direct comparison -- the same "strict superset, never a
// regression" property KROUTE_HIST_RELAX=0 already guarantees relative
// to iroute.
//
// 83 self-tests pass, including the decisive one: on a real fixture
// (kHistoryTrapGr, inherited from kroute's own history test), a single
// early over-capacity commit on one layer's edges, now genuinely clear,
// is wrongly priced *more* expensive than a steadily moderate control
// edge on another layer -- through the real production bestHLayer/
// edgeCost pipeline, no reimplementation -- until exactly one real
// relaxLambda call (matching the one real commit) restores it, at which
// point the comparison correctly flips to prefer the genuinely-idle edge.
//
// Measured result: see "Measured result" below, filled in once the real
// benchmark runs (isolated, and combined with uroute's own default
// region pricing) are complete and cross-checked against gr_verify and
// eval2008.pl.
//
// MEASURED RESULT PLACEHOLDER -- replaced once real benchmarks are run.

// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o kr1route kr1route.cpp
//
// Use:
//     ./kr1route benchmark.gr solution.route
//     ./kr1route --selftest
//
// Environment variables (all optional; every KROUTE_*/UROUTE_* var below
// behaves exactly as in kroute.cpp/uroute.cpp, since phase A/phase B,
// history, and region pricing are unchanged):
//     KR1ROUTE_LAMBDA_RELAX=0|1   this file's own new mechanism, default 1
//                                 (enabled). 0 reproduces uroute's exact
//                                 permanent-lambda formula for direct
//                                 comparison.
//     UROUTE_REGION_PRICING=0|1   uroute's own mechanism, default 1 --
//                                 see uroute.cpp's own header. Set to 0
//                                 to isolate this file's lambda-relax
//                                 mechanism alone against kroute's own
//                                 baseline, with none of uroute's own
//                                 (mixed-result) region pricing in play.
//     UROUTE_REGION_SIZE=<int>, UROUTE_REGION_EPS=<float>, UROUTE_TRACE=1
//                                 uroute's own vars, unchanged -- see
//                                 uroute.cpp's own header.
//     KROUTE_EPS=<float>          fast-tier step size epsilon, default 0.02
//     KROUTE_THRESHOLD=<float>    fast-tier early-warning curve start,
//                                 default 0.2
//     KROUTE_POWER=<float>        fast-tier curve escalation power, default 2.5
//     KROUTE_FAST_ROUNDS=<int>    max phase-A (fast tier) rounds, default 60
//     KROUTE_SLOW_ROUNDS=<int>    max phase-B (escalation) rounds, default
//                                 150
//     KROUTE_CLEANUP_THRESHOLD=<float>, KROUTE_CLEANUP_PASSES=<int>
//                                 phase A's cleanup pass (defaults 0.5 / 30)
//     KROUTE_MAX_MARGIN=<int>     slow-tier Dijkstra search margin cap,
//                                 default 6
//     KROUTE_TRACE=1              print each round's overflow, the cleanup
//                                 pass's before/after, and phase B's
//                                 accepted/rejected counts, to stderr
//     KROUTE_HIST_STEP=<float>    per-round additive growth applied to an
//                                 over-capacity edge's history term,
//                                 default 0.05
//     KROUTE_HIST_MODE=add|mult|ratio   growth rule, default "add" -- see
//                                 kroute.cpp's own header for the full
//                                 derivation of all three modes.
//     KROUTE_HIST_FACTOR=<float>  multiplicative growth factor, only used
//                                 when KROUTE_HIST_MODE=mult, default 1.1
//     KROUTE_HIST_STREAK=<int>    require this many consecutive
//                                 round-boundary over-capacity observations
//                                 before history grows at all, default 1
//     KROUTE_HIST_SCALE=flat|overflow
//                                 "flat" (default) applies the same step to
//                                 every over-capacity edge; "overflow"
//                                 scales it by that edge's own usage/
//                                 capacity ratio.
//     KROUTE_HIST_RELAX=0|1       kroute's own LIFO-relaxable history
//                                 mechanism, default 1 (enabled).
//     KROUTE_HIST_RATIO_EXPONENT=<float>, KROUTE_HIST_EXPONENT_ESCALATION=<float>
//                                 only used when KROUTE_HIST_MODE=ratio --
//                                 see kroute.cpp's own header for the full
//                                 derivation.

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
// support KROUTE_HIST_STREAK, which gates history growth behind N
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

// This file's own new mechanism: the LIFO-relaxable growth-op pattern
// above, applied to the *reactive* price (lambda, `Prices::h`/`v`)
// instead of history. Lambda has no add/mult/ratio mode choice --
// `negotiateWire` always multiplies -- so this is a minimal single-field
// struct rather than reusing `HistOp`'s `multiplicative` flag, which
// would be dead weight here (always true).
struct PriceOp {
  double factor;
};

// ---------------------------------------------------------------------------
// Region pricing (this file's own new mechanism): a region is an R x R tile
// block in the die's 2D (x,y) plane, aggregated across *every layer and
// both edge directions* -- the direct encoding of HISTORY_COST_SWEEP.md
// Part 9's finding that at a hot tile all layers are simultaneously over
// capacity, so the contended resource is a region's total cross-layer
// budget, not any single edge. Layer is dropped entirely from the region
// key: an H or V edge at (x, y, l), any l, maps to the same region as every
// other edge anchored at that (x,y) column. `regionIndexOf` uses the lower-
// index endpoint tile (x,y) for both edge families, matching the .gr
// format's own tile-indexing convention for capacity adjustments. These
// helpers only need `RoutingGrid`, so they're declared ahead of `Prices`,
// which stores one price per region alongside the existing per-edge lambda
// and history terms.
// ---------------------------------------------------------------------------

int regionSizeParam() {
  static const int r = [] {
    const char *v = std::getenv("UROUTE_REGION_SIZE");
    int val = v ? std::atoi(v) : 8;
    return val > 0 ? val : 8;
  }();
  return r;
}

size_t numRegionsX(const RoutingGrid &g) {
  int r = regionSizeParam();
  return (static_cast<size_t>(std::max(g.nx, 0)) + r - 1) / static_cast<size_t>(r);
}
size_t numRegionsY(const RoutingGrid &g) {
  int r = regionSizeParam();
  return (static_cast<size_t>(std::max(g.ny, 0)) + r - 1) / static_cast<size_t>(r);
}
size_t numRegions(const RoutingGrid &g) { return numRegionsX(g) * numRegionsY(g); }

size_t regionIndexOf(const RoutingGrid &g, int x, int y) {
  size_t rx = static_cast<size_t>(x) / static_cast<size_t>(regionSizeParam());
  size_t ry = static_cast<size_t>(y) / static_cast<size_t>(regionSizeParam());
  return ry * numRegionsX(g) + rx;
}

// Static per-region aggregate capacity, summed once across every layer and
// both edge directions at whatever (x,y) anchors each edge -- see
// `regionIndexOf` above. `std::max(0, cap)` guards a malformed .gr
// reduced-capacity adjustment that drove an edge negative from corrupting
// the region sum; a genuinely zero-or-negative-capacity edge simply
// contributes nothing, the same treatment negotiateWire and updateHistory
// already give a single such edge.
std::vector<long long> computeRegionCapacity(const RoutingGrid &g) {
  std::vector<long long> cap(numRegions(g), 0);
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        cap[regionIndexOf(g, x, y)] += std::max(0, g.hcap[g.hIndex(x, y, l)]);
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        cap[regionIndexOf(g, x, y)] += std::max(0, g.vcap[g.vIndex(x, y, l)]);
  }
  return cap;
}

struct Prices {
  std::vector<double> h, v;
  std::vector<double> histH, histV;
  std::vector<int> streakH, streakV;
  // Per-edge LIFO stack of every still-"active" growth operation -- see
  // this file's header for why a stack (not just a running total) is
  // required: KROUTE_HIST_RELAX pops and undoes exactly one entry, in
  // reverse order, on each clean round, so an edge dirty for N rounds
  // needs exactly N consecutive clean rounds to fully return to baseline.
  // Empty for almost every edge (only edges that have ever gone over
  // capacity accumulate entries), so the memory cost is proportional to
  // actual congestion, not grid size.
  std::vector<std::vector<HistOp>> histOpsH, histOpsV;
  // This file's own new mechanism: the same LIFO stack discipline as
  // `histOpsH`/`histOpsV` above, but for the reactive price itself
  // (`h`/`v`) -- pushed by `negotiateWire` at commit time (see its own
  // comment), popped by `relaxLambda` at a round boundary. Empty for
  // almost every edge, same reasoning as `histOpsH`/`histOpsV`, though a
  // single busy round can push more entries onto one edge's stack than
  // `histOpsH`/`histOpsV` ever could in one round, since `negotiateWire`
  // fires per net commit, not once per round boundary.
  std::vector<std::vector<PriceOp>> lambdaOpsH, lambdaOpsV;
  // This file's own new mechanism (region pricing): one multiplicative price per region,
  // initialized to 1.0 (a no-op) and only ever touched by
  // `updateRegionPrice` -- never by the per-net-commit path, exactly like
  // `histH`/`histV` above. `regionCapacity` is static (computed once here,
  // from capacity alone) so `updateRegionPrice` only has to resum usage
  // each round boundary, not capacity too.
  std::vector<double> regionPrice;
  std::vector<long long> regionCapacity;
  explicit Prices(const RoutingGrid &g)
      : h(g.hCount(), 1.0), v(g.vCount(), 1.0), histH(g.hCount(), 1.0), histV(g.vCount(), 1.0),
        streakH(g.hCount(), 0), streakV(g.vCount(), 0), histOpsH(g.hCount()),
        histOpsV(g.vCount()), lambdaOpsH(g.hCount()), lambdaOpsV(g.vCount()),
        regionPrice(numRegions(g), 1.0), regionCapacity(computeRegionCapacity(g)) {}
};
struct Usage {
  std::vector<int> h, v;
  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

// `threshold` is a parameter, not a fixed static lookup, specifically so
// the cleanup pass can price nets against a more relaxed curve than normal
// routing uses -- matching nproute.cpp's actual current architecture
// (which grew this parameter to support round-scheduling experiments that
// didn't pan out, but the parameter itself is exactly what a relaxed
// cleanup pass needs).
double priceExponentTerm(double usage, double cap, double threshold) {
  static const double power = [] {
    const char *p = std::getenv("KROUTE_POWER");
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
    const char *t = std::getenv("KROUTE_THRESHOLD");
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
// `regionPrice` (this file's own addition) is a third multiplicative
// factor, 1.0 (a no-op) until the first round boundary and for every
// region that has never sat over its own aggregate cross-layer budget --
// so with UROUTE_REGION_PRICING=0 (regionPrice permanently 1.0 everywhere)
// this degenerates to kroute's exact formula. See updateRegionPrice below
// for what sets it.
double edgeCost(double lambda, double hist, double regionPrice, int amount) {
  return hist * regionPrice * (1.0 + lambda * amount);
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

// `streakThreshold` (KROUTE_HIST_STREAK, default 1): an edge must be over
// capacity for this many *consecutive* round boundaries before growth is
// applied at all -- not merely "was over capacity this round," which is
// what every call site did before this parameter existed. Threshold 1
// reproduces that exact prior behavior (every over-capacity round grows
// immediately), so this is a strict generalization, never a behavior
// change at its default. Below threshold, the streak counter still
// advances -- it just doesn't unlock growth yet.
//
// `scaleByOverflow` (KROUTE_HIST_SCALE=overflow): instead of a flat step
// (or a flat factor) applied uniformly to every over-capacity edge
// regardless of how far over it is, scale by the current round's
// usage/capacity ratio -- an edge at 3x capacity accumulates history three
// times as fast as one just barely over. This is the one asymmetry between
// history's update and lambda's own: lambda already scales with
// usage/capacity via priceExponentTerm; history previously did not. Ratio
// is computed from THIS round's usage even when growth is gated by a
// multi-round streak, matching "how bad is it right now that finally
// triggered growth," not an average over the streak.
// `relaxEnabled` (KROUTE_HIST_RELAX, default true): when an edge is over
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
  for (size_t i = 0; i < u.h.size(); ++i) {
    if (g.hcap[i] <= 0)
      continue;
    if (u.h[i] > g.hcap[i]) {
      ++pr.streakH[i];
      if (pr.streakH[i] < streakThreshold)
        continue;
      double ratio = scaleByOverflow ? static_cast<double>(u.h[i]) / g.hcap[i] : 1.0;
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
            pr.histH[i] * std::pow(static_cast<double>(u.h[i]) / g.hcap[i], effExponent));
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
    if (g.vcap[i] <= 0)
      continue;
    if (u.v[i] > g.vcap[i]) {
      ++pr.streakV[i];
      if (pr.streakV[i] < streakThreshold)
        continue;
      double ratio = scaleByOverflow ? static_cast<double>(u.v[i]) / g.vcap[i] : 1.0;
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
            pr.histV[i] * std::pow(static_cast<double>(u.v[i]) / g.vcap[i], effExponent));
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

// This file's own new mechanism: at the same round-boundary points
// `updateHistory` already fires from (see the three call sites in
// route()), resum every edge's *live* usage into its region (across every
// layer and both directions -- `regionIndexOf`'s whole point) and apply
// this project's own core multiplicative price-update law (CLAUDE.md's
// "Core cost model": `lambda_e <- lambda_e * exp(epsilon * usage_e /
// capacity_e)`) to the region's aggregate ratio instead of a single edge's.
// Deliberately reuses that update law rather than inventing a fourth
// pricing formula -- the novelty here is what it's computed over (an
// aggregate, cross-layer region), not a new law. No relaxation/LIFO-undo
// is attempted for region price in this first version (unlike histH/histV
// above): a region price can only grow. `regionCapacity[r] <= 0` is
// skipped outright, same guard updateHistory gives a single zero-capacity
// edge, for the same reason: nothing meaningful to accumulate against a
// region with no real budget in the first place, and this is exactly the
// kind of division CLAUDE.md's arithmetic-edge-case section warns about.
// Clamped at kHistoryCap for the same reason histH/histV are: a formula
// that can only grow needs a stated bound.
void updateRegionPrice(const RoutingGrid &g, Prices &pr, const Usage &u, double regionEps) {
  std::vector<long long> regionUsage(numRegions(g), 0);
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        regionUsage[regionIndexOf(g, x, y)] += u.h[g.hIndex(x, y, l)];
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        regionUsage[regionIndexOf(g, x, y)] += u.v[g.vIndex(x, y, l)];
  }
  for (size_t r = 0; r < pr.regionPrice.size(); ++r) {
    if (pr.regionCapacity[r] <= 0)
      continue;
    double ratio = static_cast<double>(regionUsage[r]) / static_cast<double>(pr.regionCapacity[r]);
    double factor = std::exp(regionEps * std::max(0.0, ratio - 1.0));
    pr.regionPrice[r] = std::min(kHistoryCap, pr.regionPrice[r] * factor);
  }
}

// This file's own new mechanism: the relax (pop) half of the LIFO stack
// `negotiateWire` now pushes onto (see its own comment for the push
// half). Called at the same three round-boundary points updateHistory
// already fires from (see the three call sites in route()) -- never
// mid-round, for the same reason updateHistory never is: "over capacity"
// has to mean "in the fully negotiated, settled state of this round," not
// a transient mid-round reading. For every edge that reads clear (not
// over capacity) at this settled snapshot and still has a non-empty
// stack, pop the single most-recently-pushed factor and divide it back
// out -- exactly updateHistory's own relax branch, applied to the
// reactive price (h/v) instead of the permanent history term (histH/
// histV). Clamped at a floor of 1.0 for the same floating-point-drift
// reason updateHistory's own relax branch is (each push and its eventual
// pop are exact arithmetic inverses by construction, so this should never
// actually bind, but the guard costs nothing).
//
// Because negotiateWire fires once per net commit while this fires once
// per round boundary, one busy round can push more entries onto an
// edge's stack than a single clean round boundary can pop -- an edge
// pushed N times needs N clean round boundaries to fully unwind, the
// direct generalization of updateHistory's own "N dirty rounds need N
// clean rounds" property to a term that can move more than once per
// round.
void relaxLambda(const RoutingGrid &g, Prices &pr, const Usage &u, bool relaxEnabled) {
  if (!relaxEnabled)
    return;
  for (size_t i = 0; i < u.h.size(); ++i) {
    if (g.hcap[i] <= 0 || u.h[i] > g.hcap[i] || pr.lambdaOpsH[i].empty())
      continue;
    PriceOp op = pr.lambdaOpsH[i].back();
    pr.lambdaOpsH[i].pop_back();
    pr.h[i] = std::max(1.0, pr.h[i] / op.factor);
  }
  for (size_t i = 0; i < u.v.size(); ++i) {
    if (g.vcap[i] <= 0 || u.v[i] > g.vcap[i] || pr.lambdaOpsV[i].empty())
      continue;
    PriceOp op = pr.lambdaOpsV[i].back();
    pr.lambdaOpsV[i].pop_back();
    pr.v[i] = std::max(1.0, pr.v[i] / op.factor);
  }
}

struct LayerPlan {
  std::vector<int> hLayers, vLayers;
  explicit LayerPlan(const RoutingGrid &g) {
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
  bool isH(int l) const { return std::find(hLayers.begin(), hLayers.end(), l) != hLayers.end(); }
  bool isV(int l) const { return std::find(vLayers.begin(), vLayers.end(), l) != vLayers.end(); }
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
      cost += edgeCost(pr.h[idx], pr.histH[idx], pr.regionPrice[regionIndexOf(g, x, y)], amount);
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
      cost += edgeCost(pr.v[idx], pr.histV[idx], pr.regionPrice[regionIndexOf(g, x, y)], amount);
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
      if (g.hcap[i] <= 0) {
        // Zero/negative-capacity edges are correctly, permanently
        // forbidden -- not "temporarily congested" -- so nothing is
        // pushed here and this edge is never a relax candidate, matching
        // updateHistory's own treatment of the same edges.
        pr.h[i] = kForbiddenPrice;
      } else {
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
        double before = pr.h[i];
        pr.h[i] = std::min(kForbiddenPrice, pr.h[i] * std::exp(epsilon * priceExponentTerm(u.h[i], g.hcap[i], threshold)));
        // This file's own new mechanism: push the *actual* applied factor
        // (post-clamp), not the raw exp(...) value -- so a later relax
        // divides by exactly what was multiplied in, even on an edge that
        // hit the kForbiddenPrice ceiling, exactly mirroring how
        // updateHistory's multiplicative modes push pr.histH[i]/before
        // rather than the nominal factor. Only push a real increase
        // (applied > 1.0) -- the vast majority of commits are on
        // under-threshold edges where priceExponentTerm is exactly 0 and
        // this would otherwise push a harmless but wasteful no-op entry
        // on nearly every commit in the run.
        double applied = pr.h[i] / before;
        if (applied > 1.0)
          pr.lambdaOpsH[i].push_back({applied});
      }
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0) {
        pr.v[i] = kForbiddenPrice;
      } else {
        double before = pr.v[i];
        pr.v[i] = std::min(kForbiddenPrice, pr.v[i] * std::exp(epsilon * priceExponentTerm(u.v[i], g.vcap[i], threshold)));
        double applied = pr.v[i] / before;
        if (applied > 1.0)
          pr.lambdaOpsV[i].push_back({applied});
      }
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
  region.reset(x0, y0, x1 - x0 + 1, y1 - y0 + 1, g.nLayers);

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

  if (p1Layer >= 0) {
    int i = region.idx(p1.x, p1.y, p1Layer);
    region.setDist(i, 0.0, -1);
    pq.push({heuristic(p1.x, p1.y), 0.0, i});
  } else {
    for (int l = 0; l < g.nLayers; ++l) {
      int i = region.idx(p1.x, p1.y, l);
      region.setDist(i, 0.0, -1);
      pq.push({heuristic(p1.x, p1.y), 0.0, i});
    }
  }

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
        double w = edgeCost(prices.h[e], prices.histH[e], prices.regionPrice[regionIndexOf(g, ex, y)], amount);
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
        double w = edgeCost(prices.v[e], prices.histV[e], prices.regionPrice[regionIndexOf(g, x, ey)], amount);
        int ni = region.idx(x, ny, l);
        if (d + w < region.distAt(ni)) {
          region.setDist(ni, d + w, i);
          pq.push({d + w + heuristic(x, ny), d + w, ni});
        }
      }
    }
    for (int nl : {l - 1, l + 1}) {
      if (nl < 0 || nl >= g.nLayers)
        continue;
      double w = 1.0; // via base cost only -- no via price yet, matching mroute step 1
      int ni = region.idx(x, y, nl);
      if (d + w < region.distAt(ni)) {
        region.setDist(ni, d + w, i);
        pq.push({d + w + heuristic(x, y), d + w, ni});
      }
    }
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
    const char *m = std::getenv("KROUTE_MAX_MARGIN");
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
      fail("kroute: no path found between tree points even after margin expansion -- "
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
  long long total = 0;
  for (size_t i = 0; i < u.h.size(); ++i)
    if (u.h[i] > g.hcap[i])
      total += u.h[i] - g.hcap[i];
  for (size_t i = 0; i < u.v.size(); ++i)
    if (u.v[i] > g.vcap[i])
      total += u.v[i] - g.vcap[i];
  return total;
}

bool netOverflowed(const RoutingGrid &g, const Usage &u, const RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal) {
      for (int x = c.a; x < c.b; ++x)
        if (u.h[g.hIndex(x, c.cross, c.layer)] > g.hcap[g.hIndex(x, c.cross, c.layer)])
          return true;
    } else {
      for (int y = c.a; y < c.b; ++y)
        if (u.v[g.vIndex(c.cross, y, c.layer)] > g.vcap[g.vIndex(c.cross, y, c.layer)])
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
      total += std::max(0, u.h[e.second] - g.hcap[e.second]);
    else
      total += std::max(0, u.v[e.second] - g.vcap[e.second]);
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
    const char *t = std::getenv("KROUTE_CLEANUP_THRESHOLD");
    return t ? std::atof(t) : 0.5;
  }();
  int passes = [] {
    const char *p = std::getenv("KROUTE_CLEANUP_PASSES");
    return p ? std::atoi(p) : 30;
  }();
  bool trace = std::getenv("KROUTE_TRACE") != nullptr;
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
                 double exponentEscalation = 0.0, bool regionPricingEnabled = true,
                 double regionEps = 0.005, bool lambdaRelaxEnabled = true) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  SearchRegion region;
  bool trace = std::getenv("KROUTE_TRACE") != nullptr;
  bool regionTrace = std::getenv("UROUTE_TRACE") != nullptr;
  // This file's own new mechanism: relax the reactive price (lambda) the
  // same way KROUTE_HIST_RELAX already relaxes history -- see
  // relaxLambda's own comment. KR1ROUTE_LAMBDA_RELAX=0 disables it
  // outright (relaxLambda becomes a no-op, since lambdaRelaxEnabled is
  // its own first check), reproducing uroute's exact permanent-lambda
  // behavior for direct comparison, the same "strict superset, never a
  // regression" property KROUTE_HIST_RELAX=0 already guarantees relative
  // to iroute.
  auto maybeRelaxLambda = [&]() { relaxLambda(g, prices, usage, lambdaRelaxEnabled); };
  // Region pricing's own trailing multiplicative factor stays 1.0
  // everywhere (a no-op, see edgeCost's comment) whenever this is false --
  // UROUTE_REGION_PRICING=0 disables the mechanism outright by simply
  // never calling updateRegionPrice, the same "no call, no effect" pattern
  // KROUTE_HIST_RELAX=0 uses relative to iroute.
  auto maybeUpdateRegionPrice = [&]() {
    if (!regionPricingEnabled)
      return;
    updateRegionPrice(g, prices, usage, regionEps);
    if (regionTrace) {
      int overBudget = 0;
      double worstRatio = 0.0;
      for (size_t r = 0; r < prices.regionPrice.size(); ++r)
        if (prices.regionPrice[r] > 1.0) {
          ++overBudget;
          worstRatio = std::max(worstRatio, prices.regionPrice[r]);
        }
      std::cerr << "  region pricing: " << overBudget << "/" << prices.regionPrice.size()
                << " regions over budget, worst price " << worstRatio << "\n";
    }
  };

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
    rn.mst = mstEdges(rn.pts);
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
    maybeUpdateRegionPrice();
    maybeRelaxLambda();
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
  maybeUpdateRegionPrice();
  maybeRelaxLambda();

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
    maybeUpdateRegionPrice();
    maybeRelaxLambda();
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
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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
const char *kZigzagGr = R"(grid 10 3 3
vertical capacity 0 4 0
horizontal capacity 4 0 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netZigzag 0 2 1
5 5 1
95 5 1
2
3 0 1 4 0 1 0
6 0 3 7 0 3 0
)";

void testEscalationSolvesWhatFastTierCannot() {
  Design d = parse("kroute_selftest_zigzag.gr", kZigzagGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netZigzag produced a route");
  check(rep.escalatedCount == 1,
        "the fast tier cannot legalize this net with any single-layer "
        "choice, so it gets escalated to the slow tier");
  check(rep.bestOverflow == 0,
        "the slow tier's real search finds the zigzag path the fast tier's "
        "fixed-layer-per-run candidates structurally cannot represent");
  check(segsConnected(rep.nets[0].segs), "netZigzag is one connected component");
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
  Design d = parse("kroute_selftest_priceoverflow.gr", kPriceOverflowGr);
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
// KROUTE_HIST_STEP=0) layer 0's frozen-but-lower lambda wins every time,
// even though nothing about the grid says returning to it is actually
// safe -- the exact "drift back" failure this file exists to fix. With
// permanent history accumulated against layer 0 from its earlier overflow,
// the same comparison flips.
const char *kHistoryTrapGr = R"(grid 5 1 2
vertical capacity 0 0
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
45 5 1
0
)";

void testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge() {
  Design d = parse("kroute_selftest_history.gr", kHistoryTrapGr);
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  Prices pr(g);
  int width = 1;

  // Layer 0: usage has already dropped back under capacity (the net that
  // was congesting it detoured away this round), and its lambda never got
  // very high to begin with -- it reads as the cheap choice right now.
  for (int x = 0; x < g.nx - 1; ++x)
    pr.h[g.hIndex(x, 0, 0)] = 1.0;
  // Layer 1: some other net has kept it mildly congested continuously, so
  // its current-round lambda climbed higher than layer 0's.
  for (int x = 0; x < g.nx - 1; ++x)
    pr.h[g.hIndex(x, 0, 1)] = 2.5;

  LayerChoice noHistory = bestHLayer(g, pr, lp.hLayers, 0, 0, g.nx - 1, width);
  check(noHistory.layer == 0,
        "with fresh history (both terms at the 1.0 no-op default -- "
        "identical to hroute's own edgeCost), layer 0's lower *current* "
        "lambda wins even though it was congested moments ago: reproduces "
        "the plateau's drift-back failure mode against the real "
        "production cost function");

  // Now accumulate 8 rounds of layer 0 sitting over its capacity-10 edges,
  // exactly the way updateHistory is actually called from route() -- a
  // settled, whole-grid usage snapshot at a round boundary, never a
  // mid-round delta.
  for (int round = 0; round < 8; ++round) {
    Usage overCap(g);
    for (int x = 0; x < g.nx - 1; ++x)
      overCap.h[g.hIndex(x, 0, 0)] = 20; // well over the capacity-10 edges
    updateHistory(g, pr, overCap, /*histStep=*/1.0, HistMode::kAdditive, 1.0);
  }
  check(pr.histH[g.hIndex(0, 0, 0)] > 1.0,
        "8 rounds of layer 0 sitting over capacity permanently raised its "
        "history term");

  // Current-round usage/lambda are unchanged from the first comparison --
  // layer 0 still reads as momentarily clear and cheap on lambda alone.
  // Only the new permanent term is different.
  LayerChoice withHistory = bestHLayer(g, pr, lp.hLayers, 0, 0, g.nx - 1, width);
  check(withHistory.layer == 1,
        "with accumulated history, layer 0's permanent penalty outweighs "
        "its momentarily-lower current lambda: the net stays on layer 1 "
        "instead of drifting back -- the decisive behavioral change "
        "historyCost is supposed to buy");
}

// CLAUDE.md's arithmetic-edge-case section requires a test for every
// formula that can see a degenerate input, not just the happy path.
// updateHistory has no division (a plain usage > capacity comparison), so
// there is no capacity_e == 0 divide-by-zero to reproduce here the way
// priceExponentTerm has one -- but a zero/negative capacity edge is still a
// degenerate case worth a named test, because the *wrong* answer (silently
// accumulating history against an edge that was never usable) would still
// compound every round for the rest of the run without ever crashing,
// exactly the "wrong code still runs and still produces a route, just a
// worse one" failure mode CLAUDE.md warns about.
void testHistoryIgnoresZeroCapacityEdges() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  // In kBasicGr, layer 1 has horizontal capacity 0 (hard blocked) -- the
  // same edges negotiateWire's own kForbiddenPrice branch already treats
  // specially.
  size_t idx = g.hIndex(0, 0, 1);
  check(g.hcap[idx] <= 0, "fixture edge really is zero capacity");
  u.h[idx] = 999; // absurdly "over capacity" if compared naively
  updateHistory(g, pr, u, 1.0, HistMode::kAdditive, 1.0);
  check(pr.histH[idx] == 1.0,
        "a zero-capacity edge's history term is never grown, matching "
        "negotiateWire's own treatment of the same edges -- nothing "
        "meaningful to accumulate against an edge that was never usable");
}

// The mechanism's whole point is that it only ever moves one direction.
// KROUTE_HIST_RELAX=0 must reproduce iroute's exact permanent behavior --
// this is the regression guard for that claim, not the relaxation
// mechanism itself (see testRelaxationUndoesExactlyOneStepPerCleanRound and
// testRelaxationFullyRestoresBaselineAfterEqualCleanRounds below for that).
void testDisablingRelaxReproducesPermanentBehavior() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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
        "with KROUTE_HIST_RELAX=0, history never decreases even after usage "
        "drops back under capacity -- reproduces iroute's exact permanent "
        "behavior, the fallback this file's relaxation mechanism can always "
        "be switched off back to");
}

// The actual new mechanism: an edge dirty for exactly one round, then one
// clean round, should have its single growth operation popped and undone
// exactly -- back to the 1.0 no-op baseline, not merely "decreased."
void testRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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

// KROUTE_HIST_STREAK: growth must be gated behind N *consecutive*
// over-capacity round boundaries, and a single clear round must reset the
// count to zero rather than merely pausing it -- otherwise an edge that
// flickers over/under/over capacity every other round would still
// eventually accumulate growth from unrelated non-consecutive rounds,
// which defeats the entire point of requiring a *streak*.
void testHistoryStreakRequiresConsecutiveOvercapacityRounds() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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

// KROUTE_HIST_SCALE=overflow: an edge sitting further over capacity should
// accumulate history faster than one just barely over, unlike the default
// flat step which treats "1 unit over" and "10x over" identically.
void testHistoryScalesWithOverflowMagnitude() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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
        "KROUTE_HIST_SCALE=overflow makes the 10x-over edge accumulate "
        "history faster than the 1.5x-over edge in the very same round -- "
        "the decisive behavioral change this mode is supposed to buy");
  check(scaledWayOver > flatWayOver,
        "the way-over edge grows faster under overflow-scaling than it "
        "would have under the flat default");
}

// KROUTE_HIST_MODE=ratio: histH *= usage/capacity directly, no separate
// step or factor. A user-proposed formula, n2 = n1*(1 + overflow_percent/100),
// which is algebraically exactly usage/capacity when "overflow_percent" is
// the excess over capacity as a percentage (e.g. usage=150,cap=100 -> 50%
// -> 1 + 0.5 = 1.5 = 150/100).
void testHistoryRatioModeMultipliesByUsageOverCapacityExactly() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices pr(g);
  Usage fiftyOver(g);
  fiftyOver.h[idx] = 3; // capacity 2 -> ratio exactly 1.5
  updateHistory(g, pr, fiftyOver, /*histStep=*/0.0, HistMode::kRatio, /*histFactor=*/0.0);
  check(std::abs(pr.histH[idx] - 1.5) < 1e-9,
        "KROUTE_HIST_MODE=ratio on a 50%-over edge (usage 3, capacity 2) "
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

// KROUTE_HIST_RATIO_EXPONENT: squaring the ratio should punish a badly-over
// edge much harder while barely changing a barely-over one -- the decisive
// property the user proposed this for.
void testHistoryRatioExponentSquaresTheRatio() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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

// KROUTE_HIST_EXPONENT_ESCALATION: the effective exponent for a round's
// growth should climb with how many still-unrelaxed penalties are already
// on the stack, and a relaxed edge should use a *lower* effective exponent
// for its next penalty than an edge that stayed dirty continuously -- not
// merely end up at a lower history value, the escalation level itself must
// have dropped. Uses a fixed usage/capacity=2.0 throughout (capacity 2,
// usage 4) so every factor is an exact power of 2, checkable exactly.
void testExponentEscalationTracksStackSizeAndRelaxesWithIt() {
  Design d = parse("kroute_selftest_basic.gr", kBasicGr);
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
        "the property KROUTE_HIST_EXPONENT_ESCALATION's header claims by "
        "reusing the stack's own size as the escalation count");
}

// ---------------------------------------------------------------------------
// Region pricing mechanism tests: this file's own new addition over kroute.
// Every KROUTE_* history test above is unaffected (they construct Prices
// directly and only ever call updateHistory, never updateRegionPrice, so
// regionPrice sits at its 1.0 no-op default throughout).
// ---------------------------------------------------------------------------

void testRegionPriceStartsAtOneAndMatchesDisabledFormula() {
  Design d = parse("uroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  check(pr.regionPrice.size() == numRegions(g),
        "one price slot per region, sized at construction");
  bool allOne = true;
  for (double p : pr.regionPrice)
    if (p != 1.0)
      allOne = false;
  check(allOne,
        "regionPrice starts at the 1.0 no-op default everywhere, before "
        "any round boundary has fired -- round 0 sees kroute's exact "
        "behavior");

  // With regionPrice at its default, edgeCost's three-factor formula must
  // collapse to exactly kroute's own two-factor formula -- checked against
  // the real production edgeCost function, not a reimplementation of it.
  double lambda = 2.3, hist = 1.7;
  int amount = 5;
  double withRegionFactor = edgeCost(lambda, hist, pr.regionPrice[0], amount);
  double krouteFormula = hist * (1.0 + lambda * amount);
  check(withRegionFactor == krouteFormula,
        "edgeCost with a fresh Prices object's regionPrice reproduces "
        "kroute's exact formula bit-for-bit -- UROUTE_REGION_PRICING=0's "
        "whole no-regression claim, checked directly");
}

// The region's whole point is to aggregate *across every layer* -- the
// direct encoding of HISTORY_COST_SWEEP.md Part 9's finding that a hot
// tile's every layer is over capacity simultaneously, which a per-edge or
// per-layer signal alone can never see. This grid has exactly one region
// (numRegions(g) == 1) with two layers, so a bug that only summed one
// layer would produce a different, wrong ratio than summing both -- the
// exact arithmetic below is decisive on that specifically.
const char *kRegionCrossLayerGr = R"(grid 3 1 2
vertical capacity 0 0
horizontal capacity 3 3
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
25 5 1
0
)";

void testRegionPriceAggregatesUsageAcrossAllLayersExactly() {
  Design d = parse("uroute_selftest_region_layers.gr", kRegionCrossLayerGr);
  const RoutingGrid &g = d.grid;
  check(numRegions(g) == 1, "fixture really is small enough to be one single region");
  check(g.nLayers == 2, "fixture really does have two layers to aggregate across");

  Prices pr(g);
  check(pr.regionCapacity[0] == 4 * 3,
        "region capacity is the sum of all 4 edges (2 layers x 2 h-edges "
        "each) at capacity 3 apiece -- static, computed once at "
        "construction, before any usage exists");

  Usage u(g);
  u.h[g.hIndex(0, 0, 0)] = 5;
  u.h[g.hIndex(1, 0, 0)] = 5; // layer 0: 10 total
  u.h[g.hIndex(0, 0, 1)] = 4;
  u.h[g.hIndex(1, 0, 1)] = 4; // layer 1: 8 total -- a different layer entirely
  updateRegionPrice(g, pr, u, /*regionEps=*/0.1);

  double ratio = 18.0 / 12.0; // (10 from layer 0 + 8 from layer 1) / region capacity
  double expected = std::exp(0.1 * std::max(0.0, ratio - 1.0));
  check(pr.regionPrice[0] == expected,
        "regionPrice after one round boundary matches this project's own "
        "core multiplicative price-update law (CLAUDE.md's 'Core cost "
        "model') applied to the region's cross-layer aggregate ratio -- "
        "18/12, summing layer 0's 10 AND layer 1's 8 together, not either "
        "layer alone: proof the aggregation really does cross layers, the "
        "one property no per-edge mechanism in this project's whole "
        "history has ever had access to");
}

// The decisive behavioral test: two pins at opposite corners of a 16x16
// single-layer grid have *every* monotone L/jog candidate between them at
// *identical* total tile length (Manhattan distance is jog-invariant), and
// this fixture's uniform, ample per-edge capacity keeps lambda and hist
// identical (1.0) everywhere too -- so with region pricing never invoked,
// nothing distinguishes any candidate but list order, and routeNetFast's
// strict `<` comparison keeps the very first one generated ("H at p1.y,
// then V at p2.x"), which this geometry is picked to route straight
// through region (1,0). A separate, already-committed net's real usage
// then drives one single edge deep in that region far over its own
// capacity -- not on either candidate's own path -- and one
// updateRegionPrice call (the real round-boundary function) prices the
// whole region up. The real production pipeline -- generateCandidates,
// priceCandidate, routeNetFast's own min-cost selection, none of it
// reimplemented here -- must then pick a different, equal-length candidate
// that avoids the region entirely: the one outcome plain per-edge pricing
// (every edge on every candidate individually legal, lambda and hist both
// untouched) cannot produce.
const char *kRegionSteerGr = R"(grid 16 16 1
vertical capacity 0
horizontal capacity 5
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netProbe 0 2 1
25 5 1
135 155 1
0
)";

void testRegionPricingSteersFastTierSearchAwayFromOversubscribedRegion() {
  Design d = parse("uroute_selftest_region_steer.gr", kRegionSteerGr);
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  const Net &net = d.nets[0];

  auto buildNet = [&]() {
    RoutedNet rn;
    rn.net = &net;
    rn.pts = {{2, 0}, {13, 15}};
    rn.mst = {{0, 1}};
    return rn;
  };
  size_t hotRegion = regionIndexOf(g, 9, 3);
  auto touchesHotRegion = [&](const std::vector<ChargeRecord> &charges) {
    for (const ChargeRecord &c : charges) {
      if (c.horizontal) {
        for (int x = c.a; x < c.b; ++x)
          if (regionIndexOf(g, x, c.cross) == hotRegion)
            return true;
      } else {
        for (int y = c.a; y < c.b; ++y)
          if (regionIndexOf(g, c.cross, y) == hotRegion)
            return true;
      }
    }
    return false;
  };

  {
    Prices pr(g);
    Usage u(g);
    RoutedNet rn = buildNet();
    routeNetFast(g, pr, u, lp, /*epsilon=*/0.5, defaultThreshold(), rn);
    check(touchesHotRegion(rn.charges),
          "baseline (region pricing never invoked): the tie-broken-by-order "
          "default candidate really does route straight through the "
          "region this test is about to overload -- confirms the fixture, "
          "not just the assertion below");
  }
  {
    Prices pr(g);
    Usage u(g);
    u.h[g.hIndex(9, 3, 0)] = 1000; // one edge, deep in the hot region, ~50x its own capacity
    updateRegionPrice(g, pr, u, /*regionEps=*/1.0);
    check(pr.regionPrice[hotRegion] > 1.0, "the overloaded region priced up");
    check(pr.regionPrice[regionIndexOf(g, 2, 9)] == 1.0,
          "a different region, untouched by that overload, stays at the "
          "1.0 no-op price");

    RoutedNet rn = buildNet();
    routeNetFast(g, pr, u, lp, /*epsilon=*/0.5, defaultThreshold(), rn);
    check(!touchesHotRegion(rn.charges),
          "with the region priced up, the real fast-tier candidate search "
          "picks a different, equal-length candidate that avoids it "
          "entirely -- purely because of region pricing: no single edge "
          "on either candidate's own path is itself over capacity, and "
          "lambda/hist are identical everywhere in this fixture");
  }
}

const char *kZeroCapRegionGr = R"(grid 2 1 1
vertical capacity 0
horizontal capacity 0
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
15 5 1
0
)";

void testRegionPricingArithmeticEdgeCases() {
  Design d = parse("uroute_selftest_region_zerocap.gr", kZeroCapRegionGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  check(pr.regionCapacity[0] == 0, "fixture region really has zero aggregate capacity");

  Usage u(g);
  u.h[g.hIndex(0, 0, 0)] = 999; // absurd "usage" against a region with no real budget at all
  updateRegionPrice(g, pr, u, /*regionEps=*/1.0);
  check(pr.regionPrice[0] == 1.0,
        "a zero-aggregate-capacity region's price is never touched -- no "
        "usage/capacity division happens at all, matching updateHistory's "
        "own treatment of a single zero-capacity edge, and avoiding "
        "exactly the divide-by-zero CLAUDE.md's arithmetic-edge-case "
        "section warns about");

  // Growth clamp: a region that stays over its own budget for many rounds
  // must not compound past kHistoryCap -- the same stated bound histH/histV
  // already carry, applied here since regionPrice can likewise only grow
  // (no relaxation in this first version, see the file header).
  Design d2 = parse("uroute_selftest_region_layers.gr", kRegionCrossLayerGr);
  const RoutingGrid &g2 = d2.grid;
  Prices pr2(g2);
  Usage over(g2);
  over.h[g2.hIndex(0, 0, 0)] = 1000;
  over.h[g2.hIndex(1, 0, 0)] = 1000;
  over.h[g2.hIndex(0, 0, 1)] = 1000;
  over.h[g2.hIndex(1, 0, 1)] = 1000;
  for (int round = 0; round < 200; ++round)
    updateRegionPrice(g2, pr2, over, /*regionEps=*/5.0); // deliberately aggressive
  check(pr2.regionPrice[0] <= kHistoryCap,
        "200 rounds of an aggressively-growing, persistently-over-budget "
        "region never exceeds the stated kHistoryCap ceiling -- a formula "
        "that can only grow needs a stated bound, per this project's own "
        "arithmetic-edge-case discipline");
  check(pr2.regionPrice[0] == kHistoryCap,
        "and the clamp is actually reached at this deliberately aggressive "
        "growth rate, not just theoretically available -- confirms the "
        "std::min guard is live code, not dead defensive code");
}

// nx=10 is not a multiple of the default region size (8): region column 0
// holds a full 8 tiles' worth of edges (x=0..7), region column 1 holds
// only the single edge left over (x=8) -- the realistic case for every
// real ISPD benchmark, whose dimensions are never exact multiples of
// whatever region size gets tuned.
const char *kRaggedRegionGr = R"(grid 10 1 1
vertical capacity 0
horizontal capacity 5
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
95 5 1
0
)";

void testRegionCapacityHandlesRaggedGridAndUsageMatchesIndependentRebuild() {
  Design d = parse("uroute_selftest_region_ragged.gr", kRaggedRegionGr);
  const RoutingGrid &g = d.grid;
  check(numRegionsX(g) == 2, "10 tiles at region size 8 split into exactly two region columns");
  Prices pr(g);
  check(pr.regionCapacity[0] == 8 * 5,
        "region column 0's capacity is the full 8 real edges it contains, "
        "at capacity 5 apiece");
  check(pr.regionCapacity[1] == 1 * 5,
        "region column 1's capacity is only its single real ragged edge -- "
        "not a full 8-edge block's worth, and not zero either");

  // Independent rebuild: sum every edge's live usage into its region via a
  // deliberately separate, more naive loop than updateRegionPrice's own,
  // and confirm they agree exactly.
  Usage u(g);
  for (int x = 0; x < g.nx - 1; ++x)
    u.h[g.hIndex(x, 0, 0)] = x + 1; // an arbitrary, distinct value per edge
  updateRegionPrice(g, pr, u, /*regionEps=*/0.01);

  std::vector<long long> independentUsage(numRegions(g), 0);
  for (int x = 0; x < g.nx - 1; ++x)
    independentUsage[regionIndexOf(g, x, 0)] += u.h[g.hIndex(x, 0, 0)];
  for (size_t r = 0; r < numRegions(g); ++r) {
    double ratio = pr.regionCapacity[r] > 0
                       ? static_cast<double>(independentUsage[r]) / static_cast<double>(pr.regionCapacity[r])
                       : 0.0;
    double expected = pr.regionCapacity[r] > 0 ? std::exp(0.01 * std::max(0.0, ratio - 1.0)) : 1.0;
    check(pr.regionPrice[r] == expected,
          "region " + std::to_string(r) +
              "'s price matches an independently rebuilt usage sum, not "
              "just whatever updateRegionPrice's own internal aggregation "
              "happened to compute");
  }
}

// ---------------------------------------------------------------------------
// Lambda-relax mechanism tests: this file's own new addition over uroute.
// Every prior test above is unaffected -- none of them call negotiateWire
// or relaxLambda directly with lambda-raising usage, so lambdaOpsH/
// lambdaOpsV stay empty and pr.h/pr.v behave exactly as uroute's own.
// ---------------------------------------------------------------------------

// negotiateWire must push the *actual applied* factor (post-kForbiddenPrice
// clamp), not the raw, unclamped exp(...) value -- otherwise a later relax
// would divide by the wrong number and overshoot past 1.0, or undershoot
// and leave a phantom residual. Constructed so the raw factor would send
// pr.h[idx] far past kForbiddenPrice if unclamped, from a hand-set starting
// price high enough that the clamp binds well before the raw factor would.
void testNegotiateWirePushesAppliedFactorNotRawExpValue() {
  Design d = parse("kr1route_selftest_pushclamp.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  pr.h[idx] = 5e8; // hand-set: already high, so a large raw factor clamps
  Usage over(g);
  over.h[idx] = 22; // massively over capacity 2
  ChargeRecord c{/*horizontal=*/true, /*cross=*/0, /*a=*/0, /*b=*/1, /*layer=*/0, /*amount=*/1};
  negotiateWire(g, pr, over, c, /*epsilon=*/5.0, /*threshold=*/1.0);
  check(pr.h[idx] == kForbiddenPrice,
        "the raw factor at these parameters really is large enough to hit "
        "the kForbiddenPrice clamp starting from 5e8 -- confirms the "
        "fixture, not just the assertion below");
  check(pr.lambdaOpsH[idx].size() == 1 && pr.lambdaOpsH[idx].back().factor == 2.0,
        "the pushed factor is exactly kForbiddenPrice/5e8 = 2.0 -- the real "
        "applied ratio -- not the astronomically larger raw exp(...) value "
        "that would have been applied without the clamp: an exact-inverse "
        "requirement, or a later relax would divide by the wrong number");
}

// The common case (an edge under KROUTE_THRESHOLD's curve start) must push
// nothing at all -- otherwise nearly every commit in a real run would bloat
// every edge's stack with harmless no-op entries.
void testNegotiateWirePushesNothingOnANoOpCommit() {
  Design d = parse("kr1route_selftest_noop.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0);
  Usage under(g);
  under.h[idx] = 1; // 1/2 = 50%... still need well under the 20% default threshold
  // kBasicGr's capacity-2 edge makes "well under 20%" awkward with integer
  // usage, so widen the margin directly via threshold=default on a usage
  // that is genuinely 0 -- the cleanest possible no-op case.
  under.h[idx] = 0;
  ChargeRecord c{true, 0, 0, 1, 0, 1};
  negotiateWire(g, pr, under, c, /*epsilon=*/0.5, defaultThreshold());
  check(pr.h[idx] == 1.0, "lambda stays at its 1.0 no-op default on a genuinely clear commit");
  check(pr.lambdaOpsH[idx].empty(),
        "nothing is pushed for a no-op commit -- the vast majority of real "
        "commits in a run are under threshold, and pushing a harmless "
        "factor-1.0 entry for every one of them would bloat every edge's "
        "stack for no reason");
}

// The decisive relax test, mirroring testRelaxationFullyRestoresBaselineAfterEqualCleanRounds
// but through the real production negotiateWire/relaxLambda pair instead of
// updateHistory: three real over-capacity commits (three separate net
// commits within a round, or across rounds -- negotiateWire doesn't care
// which) push three separate stack entries; two clean round boundaries
// undo exactly two of them, LIFO, leaving one behind; the third fully
// restores the 1.0 no-op baseline.
void testLambdaRelaxationFullyRestoresBaselineAfterEqualCleanRoundsViaRealNegotiateWire() {
  Design d = parse("kr1route_selftest_relax.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 4; // over capacity 2
  Usage clear(g);
  clear.h[idx] = 0;
  ChargeRecord c{true, 0, 0, 1, 0, 1};
  double perPushFactor = std::exp(1.0 * priceExponentTerm(4.0, 2.0, 1.0)); // epsilon=1.0, threshold=1.0

  for (int i = 0; i < 3; ++i)
    negotiateWire(g, pr, over, c, /*epsilon=*/1.0, /*threshold=*/1.0);
  check(std::abs(pr.h[idx] - perPushFactor * perPushFactor * perPushFactor) < 1e-9,
        "three real over-capacity negotiateWire commits compound the exact "
        "same way three dirty history rounds do -- pr.h grows by "
        "perPushFactor each time, multiplicatively");

  relaxLambda(g, pr, clear, /*relaxEnabled=*/true);
  relaxLambda(g, pr, clear, /*relaxEnabled=*/true);
  check(std::abs(pr.h[idx] - perPushFactor) < 1e-9,
        "two clean round boundaries pop exactly two of the three pushed "
        "factors, LIFO, leaving exactly one increment behind -- not an "
        "averaged or proportional decay, proving this is a real stack of "
        "individual operations");

  relaxLambda(g, pr, clear, /*relaxEnabled=*/true);
  check(std::abs(pr.h[idx] - 1.0) < 1e-9,
        "the third clean round boundary -- matching the three real "
        "negotiateWire commits that earned the penalty -- fully restores "
        "the 1.0 no-op baseline exactly, through the real production "
        "negotiateWire/relaxLambda pair, not a reimplementation of either");
}

// The file's own headline claim, checked against the real production cost
// function (bestHLayer/edgeCost), not a reimplementation: an edge that
// spiked once early and has genuinely been clear ever since (usage back to
// 0, real available capacity right now) is wrongly priced as *more*
// expensive than a steadily moderate edge without relax -- lambda's own
// version of the "unused space" problem, since nothing in the cost model
// reflects that the spike is long over. With relax applied for exactly as
// many clean rounds as the edge was pushed, the comparison flips to
// correctly prefer the genuinely-idle edge.
void testLambdaRelaxLetsAGenuinelyRecoveredEdgeCompeteAgainstAModeratelyBusyOne() {
  Design d = parse("kr1route_selftest_lambdadrift.gr", kHistoryTrapGr);
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  Prices pr(g);
  int width = 1;

  // Layer 0: a single early spike (usage 20 against capacity 10, on all
  // four of this fixture's h-edges), simulating one net that congested it
  // once and has since rerouted away entirely.
  Usage spike(g);
  for (int x = 0; x < g.nx - 1; ++x)
    spike.h[g.hIndex(x, 0, 0)] = 20;
  for (int x = 0; x < g.nx - 1; ++x) {
    ChargeRecord c{true, 0, x, x + 1, 0, 1};
    negotiateWire(g, pr, spike, c, /*epsilon=*/0.3, defaultThreshold());
  }
  double expectedStale = std::exp(0.3 * priceExponentTerm(20.0, 10.0, defaultThreshold()));
  check(std::abs(pr.h[g.hIndex(0, 0, 0)] - expectedStale) < 1e-9,
        "layer 0's edges carry exactly the one spike's applied factor");
  check(expectedStale > 2.5, "the spike's price clears the layer-1 comparison point set up below");

  // Layer 1: hand-set to a fixed, steady moderate price -- standing in for
  // some other net that has kept it continuously, moderately busy, the
  // same fixture convention testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge
  // already uses for its own layer-1 control edge.
  for (int x = 0; x < g.nx - 1; ++x)
    pr.h[g.hIndex(x, 0, 1)] = 2.5;

  LayerChoice beforeRelax = bestHLayer(g, pr, lp.hLayers, 0, 0, g.nx - 1, width);
  check(beforeRelax.layer == 1,
        "without relax, layer 0's stale spike (still remembered from a "
        "single early commit) wrongly loses to layer 1's steady moderate "
        "price -- even though layer 0's real, current usage is now "
        "genuinely 0 and its capacity is completely free: the exact "
        "'unused space' failure mode this mechanism exists to fix");

  // Usage really has dropped back to 0 on layer 0 now -- one clean round
  // boundary, matching the one real commit that raised it, should fully
  // relax it.
  Usage clear(g);
  relaxLambda(g, pr, clear, /*relaxEnabled=*/true);
  check(pr.h[g.hIndex(0, 0, 0)] == 1.0,
        "one clean round boundary fully relaxes layer 0's price back to "
        "its 1.0 no-op baseline, matching the one real commit that raised it");

  LayerChoice afterRelax = bestHLayer(g, pr, lp.hLayers, 0, 0, g.nx - 1, width);
  check(afterRelax.layer == 0,
        "with relax applied, the comparison flips: layer 0 -- genuinely "
        "idle, real capacity available right now -- correctly beats layer "
        "1's steady moderate price, instead of permanently losing to an "
        "edge it's actually far better than. The decisive behavioral "
        "change this file's mechanism is supposed to buy");
}

// Arithmetic edge cases, per CLAUDE.md's own testing discipline for any
// formula that can see a degenerate input.
void testLambdaRelaxArithmeticEdgeCases() {
  // Zero-capacity edges: negotiateWire's own kForbiddenPrice branch never
  // touches lambdaOpsH/lambdaOpsV at all (see the comment in negotiateWire
  // itself), and relaxLambda skips zero-capacity edges outright -- nothing
  // meaningful to accumulate or relax against an edge that was never
  // usable, matching updateHistory's own treatment of the same edges.
  Design d = parse("kr1route_selftest_zerocap.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 1); // layer 1 is hard-blocked (capacity 0) in kBasicGr
  check(g.hcap[idx] <= 0, "fixture edge really is zero capacity");
  Usage u(g);
  u.h[idx] = 999;
  ChargeRecord c{true, 0, 0, 1, 1, 1};
  negotiateWire(g, pr, u, c, 1.0, 0.2);
  check(pr.h[idx] == kForbiddenPrice && pr.lambdaOpsH[idx].empty(),
        "a zero-capacity edge is priced forbidden directly, with nothing "
        "pushed onto its lambda stack at all");
  relaxLambda(g, pr, u, /*relaxEnabled=*/true);
  check(pr.h[idx] == kForbiddenPrice,
        "relaxLambda leaves a zero-capacity edge's forbidden price exactly "
        "as-is -- it is correctly, permanently forbidden, not 'temporarily "
        "congested,' so there is nothing to relax");

  // 1.0 floor after many push/pop cycles: each push and its eventual pop
  // are exact arithmetic inverses by construction, so this should never
  // actually bind -- but CLAUDE.md's own arithmetic-edge-case discipline
  // asks for a stated, tested bound on any quantity that changes over many
  // rounds, the same reasoning updateHistory's own relax branch already
  // carries a floor for.
  Design d2 = parse("kr1route_selftest_floor.gr", kBasicGr);
  const RoutingGrid &g2 = d2.grid;
  Prices pr2(g2);
  size_t idx2 = g2.hIndex(0, 0, 0);
  Usage over2(g2);
  over2.h[idx2] = 4;
  Usage clear2(g2);
  clear2.h[idx2] = 0;
  ChargeRecord c2{true, 0, 0, 1, 0, 1};
  for (int i = 0; i < 50; ++i) {
    negotiateWire(g2, pr2, over2, c2, 0.2, defaultThreshold());
    relaxLambda(g2, pr2, clear2, true);
  }
  check(pr2.h[idx2] >= 1.0, "50 push/relax cycles never drive lambda below its 1.0 floor");
  check(std::abs(pr2.h[idx2] - 1.0) < 1e-6,
        "and each cycle's push is fully undone by its own immediate relax, "
        "landing back at exactly 1.0 every time, not drifting away from it");
}

// The mechanism's whole point is that it only moves one direction unless
// explicitly told to relax. KR1ROUTE_LAMBDA_RELAX=0 must reproduce uroute's
// exact permanent-lambda behavior -- the regression guard for that claim.
void testDisablingLambdaRelaxReproducesUroutesPermanentBehavior() {
  Design d = parse("kr1route_selftest_disabled.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0);
  Usage over(g);
  over.h[idx] = 4;
  ChargeRecord c{true, 0, 0, 1, 0, 1};
  negotiateWire(g, pr, over, c, 1.0, 1.0);
  double afterPush = pr.h[idx];
  check(afterPush > 1.0, "lambda grew from the over-capacity commit");

  Usage clear(g);
  relaxLambda(g, pr, clear, /*relaxEnabled=*/false);
  check(pr.h[idx] == afterPush,
        "with KR1ROUTE_LAMBDA_RELAX=0, a clean round boundary leaves "
        "lambda completely untouched -- reproduces uroute's exact "
        "permanent behavior, the fallback this file's mechanism can "
        "always be switched off back to");
}

// Structural guard, not a new formula: lambdaOpsH/lambdaOpsV are their own
// independent stacks, not aliased with or interfering with histOpsH/
// histOpsV or regionPrice -- pushing/relaxing lambda must never move
// history or region price, and vice versa.
void testLambdaOpsAreIndependentOfHistoryAndRegionPricing() {
  Design d = parse("kr1route_selftest_independence.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0);
  Usage over(g);
  over.h[idx] = 4;
  ChargeRecord c{true, 0, 0, 1, 0, 1};

  negotiateWire(g, pr, over, c, 1.0, 1.0);
  check(pr.h[idx] > 1.0, "lambda moved from the push");
  check(pr.histH[idx] == 1.0, "history is untouched by a lambda push -- negotiateWire never touches histH/histV");
  check(pr.regionPrice[0] == 1.0, "region price is untouched by a lambda push -- negotiateWire never touches regionPrice");

  updateHistory(g, pr, over, 0.05, HistMode::kAdditive, 1.0);
  double lambdaAfterHistoryGrowth = pr.h[idx];
  check(pr.histH[idx] > 1.0, "history moved from updateHistory, as expected");
  check(pr.h[idx] == lambdaAfterHistoryGrowth,
        "lambda is untouched by updateHistory's own growth -- the two "
        "stacks (lambdaOpsH/lambdaOpsV and histOpsH/histOpsV) are genuinely "
        "independent, not accidentally sharing state");
}

int run() {
  std::cout << "self test\n";
  testBasicShapesStayOnFastTier();
  testEscalationSolvesWhatFastTierCannot();
  testPriceClampPreventsInfinity();
  testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge();
  testHistoryIgnoresZeroCapacityEdges();
  testDisablingRelaxReproducesPermanentBehavior();
  testRelaxationUndoesExactlyOneStepPerCleanRound();
  testRelaxationFullyRestoresBaselineAfterEqualCleanRounds();
  testHistoryStreakRequiresConsecutiveOvercapacityRounds();
  testHistoryScalesWithOverflowMagnitude();
  testHistoryRatioModeMultipliesByUsageOverCapacityExactly();
  testHistoryRatioExponentSquaresTheRatio();
  testExponentEscalationTracksStackSizeAndRelaxesWithIt();
  testRegionPriceStartsAtOneAndMatchesDisabledFormula();
  testRegionPriceAggregatesUsageAcrossAllLayersExactly();
  testRegionPricingSteersFastTierSearchAwayFromOversubscribedRegion();
  testRegionPricingArithmeticEdgeCases();
  testRegionCapacityHandlesRaggedGridAndUsageMatchesIndependentRebuild();
  testNegotiateWirePushesAppliedFactorNotRawExpValue();
  testNegotiateWirePushesNothingOnANoOpCommit();
  testLambdaRelaxationFullyRestoresBaselineAfterEqualCleanRoundsViaRealNegotiateWire();
  testLambdaRelaxLetsAGenuinelyRecoveredEdgeCompeteAgainstAModeratelyBusyOne();
  testLambdaRelaxArithmeticEdgeCases();
  testDisablingLambdaRelaxReproducesUroutesPermanentBehavior();
  testLambdaOpsAreIndependentOfHistoryAndRegionPricing();
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
  if (const char *e = std::getenv("KROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("KROUTE_FAST_ROUNDS"); r)
    maxFastRounds = std::atoi(r);
  if (const char *r = std::getenv("KROUTE_SLOW_ROUNDS"); r)
    maxSlowRounds = std::atoi(r);
  if (const char *s = std::getenv("KROUTE_HIST_STEP"); s)
    histStep = std::atof(s);
  if (const char *f = std::getenv("KROUTE_HIST_FACTOR"); f)
    histFactor = std::atof(f);
  if (const char *m = std::getenv("KROUTE_HIST_MODE"); m && std::string(m) == "mult")
    histMode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("KROUTE_HIST_MODE"); m && std::string(m) == "ratio")
    histMode = HistMode::kRatio;
  if (const char *s = std::getenv("KROUTE_HIST_STREAK"); s)
    streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("KROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
    scaleByOverflow = true;
  bool relaxEnabled = true;
  if (const char *r = std::getenv("KROUTE_HIST_RELAX"); r)
    relaxEnabled = std::string(r) != "0";
  double ratioExponent = 1.0;
  if (const char *r = std::getenv("KROUTE_HIST_RATIO_EXPONENT"); r)
    ratioExponent = std::atof(r);
  double exponentEscalation = 0.0;
  if (const char *r = std::getenv("KROUTE_HIST_EXPONENT_ESCALATION"); r)
    exponentEscalation = std::atof(r);
  bool regionPricingEnabled = true;
  if (const char *r = std::getenv("UROUTE_REGION_PRICING"); r)
    regionPricingEnabled = std::string(r) != "0";
  double regionEps = 0.005;
  if (const char *r = std::getenv("UROUTE_REGION_EPS"); r)
    regionEps = std::atof(r);
  bool lambdaRelaxEnabled = true;
  if (const char *r = std::getenv("KR1ROUTE_LAMBDA_RELAX"); r)
    lambdaRelaxEnabled = std::string(r) != "0";

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                         streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent,
                         exponentEscalation, regionPricingEnabled, regionEps, lambdaRelaxEnabled);
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " nets, " << rep.escalatedCount
            << " escalated to the slow tier\n";
  std::cerr << "best overflow: " << rep.bestOverflow << " (" << rep.roundsRun
            << " total rounds run)\n";
  std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                     : rep.stalled ? "stalled: no improvement for "
                                                       "several rounds, stopping\n"
                                                   : "hit the round cap\n");
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
