// xroute.cpp
//
// Forked from kroute.cpp -- see that file's header for the LIFO-relaxable
// history-cost mechanism this file inherits completely unchanged (every
// XROUTE_HIST_* env var below behaves exactly as kroute's own KROUTE_HIST_*
// did). This file adds three new, independently-toggleable mechanisms, all
// proposed directly by the project's user in response to a real, measured
// finding: bigblue1's overflow concentrates hard in the die's own center
// (up to 4x the pin density of the corners, at essentially flat per-tile
// capacity -- see HISTORY_COST_SWEEP.md's Part 8 for the RUDY/pin-density
// analysis that established this), while peripheral capacity sits mostly
// idle. The user's diagnosis: kroute's own cost curve never gets
// prohibitively expensive near capacity, and never reorders nets by how
// much freedom each one actually has to avoid a contested area.
//
// 1. A genuine price barrier, replacing kroute's smooth threshold/power
//    curve for all normal routing (not the relaxed cleanup pass, which
//    keeps kroute's gentler curve on purpose -- see costExponentTerm).
//    kroute's own curve reaches only a normal, single-step exponent of 1.0
//    exactly at 100% capacity; this file's `barrierExponentTerm` stays flat
//    below 60%, escalates along a power curve from there so an edge is
//    already "VERY expensive" well before 95%, and treats 95-100% as a
//    must-go-only zone via a rational (1/(1-u)) blowup -- with any actual
//    overflow (>=100%) jumping straight to a fixed, deliberately huge
//    exponent on the very first touch, not building up gradually over many
//    rounds. All of it still funnels through the exact same
//    min(kForbiddenPrice, ...) clamp kroute already uses to keep the
//    mroute-era exp-to-infinity bug from ever recurring (see
//    negotiateWire's own comment) -- one safety net serves both curves.
//    XROUTE_BARRIER=0 disables this and reproduces kroute's exact curve.
//
// 2. RUDY-based net ordering, revived from droute.cpp -- a real, if modest,
//    win (adaptec1 -2.2%, bigblue1 -1.4%) that was proven years before
//    kroute's history-cost mechanism existed, and never actually combined
//    with it until now. `computeRudyDensity`/`netRudyRisk` are ported
//    verbatim; see droute.cpp's own header for the derivation.
//
// 3. Net centrality ordering, new in this file. A net whose pins sit around
//    its own bounding box's perimeter has a whole family of monotonic
//    paths hugging the boundary in either direction, none of which need to
//    touch the box's interior -- real freedom to route around whatever's
//    congested in the middle. A net whose pins are packed toward its own
//    box's center has no such option: every one of those pins already
//    sits in the same small patch, so the tree is forced to converge there
//    no matter what. `netCentralityScore` measures this directly (0 =
//    pins at the centroid, ~1 = pins at the corners) and orders inflexible
//    (low-score) nets *first*, while the grid is still open, leaving
//    flexible (high-score) nets to absorb whatever's left over -- the
//    classical "least-flexible-first" answer to negotiated-resource
//    scheduling. (An earlier version of this reasoning used a corner-
//    anchored 2-pin-plus-a-cluster example and concluded the opposite
//    direction; the user caught this directly, and the general case -- all
//    pins on the perimeter vs. all pins at the center -- supports the
//    direction described here, not that example.)
//
// XROUTE_ORDER selects none/rudy/centrality/combined (default), each
// measurable in isolation -- see testCentralityOrderingInvertsWhoClaimsSharedCorridor
// for a decisive, hand-verified proof that reordering actually changes
// which net wins a shared contested resource, not just that it computes a
// score.
//
// Measured result: pending. All three new mechanisms are self-tested and
// individually toggleable; a 3-benchmark sweep against kroute's own
// committed baseline (adaptec1 162584, newblue1 41014, bigblue1 292420)
// has not yet been run -- see HISTORY_COST_SWEEP.md for whatever has been
// measured by the time this is read.
//

// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o xroute xroute.cpp
//
// Use:
//     ./xroute benchmark.gr solution.route
//     ./xroute --selftest
//
// Environment variables (all optional; unlisted ones behave exactly as in
// hroute.cpp, since phase A/phase B are unchanged):
//     XROUTE_EPS=<float>          fast-tier step size epsilon, default 0.02
//     XROUTE_THRESHOLD=<float>    fast-tier early-warning curve start,
//                                 default 0.2
//     XROUTE_POWER=<float>        fast-tier curve escalation power, default 2.5
//     XROUTE_FAST_ROUNDS=<int>    max phase-A (fast tier) rounds, default 60
//                                 (every prior router in this lineage
//                                 defaults to 30 -- raised here because the
//                                 measured result below only shows up with
//                                 a much longer round budget; see Measured
//                                 result)
//     XROUTE_SLOW_ROUNDS=<int>    max phase-B (escalation) rounds, default
//                                 150 (same reason as XROUTE_FAST_ROUNDS
//                                 above)
//     XROUTE_CLEANUP_THRESHOLD=<float>, XROUTE_CLEANUP_PASSES=<int>
//                                 phase A's cleanup pass, same as nproute's
//                                 (defaults 0.5 / 30)
//     XROUTE_MAX_MARGIN=<int>     slow-tier Dijkstra search margin cap,
//                                 same as mroute's (default 6)
//     XROUTE_TRACE=1              print each round's overflow, the cleanup
//                                 pass's before/after, and phase B's
//                                 accepted/rejected counts, to stderr
//     XROUTE_HIST_STEP=<float>    per-round additive growth applied to an
//                                 over-capacity edge's history term,
//                                 default 0.05 (0 reproduces hroute's
//                                 pricing exactly -- history becomes a
//                                 permanent no-op multiplier of 1). The
//                                 first value tried, 1.0 (matching every
//                                 other per-round multiplicative step in
//                                 this codebase), made every benchmark
//                                 WORSE. 0.02 (this router's default for
//                                 a time) was the fix for that, but a
//                                 follow-up sweep (see HISTORY_COST_SWEEP.md
//                                 at the repo root, and Measured result
//                                 below) found 0.05 beats it slightly on
//                                 all three real benchmarks at the same
//                                 172-round budget -- both empirically
//                                 tuned values, not the naive one.
//     XROUTE_HIST_MODE=add|mult|ratio   growth rule: "add" (default) does
//                                 histH += XROUTE_HIST_STEP; "mult" does
//                                 histH *= XROUTE_HIST_FACTOR instead;
//                                 "ratio" does histH *= usage_e/capacity_e
//                                 directly (no separate step or factor at
//                                 all -- an edge at 150% of capacity
//                                 multiplies its history by 1.5 that
//                                 round, unconditionally). This
//                                 is the direct multiplicative analogue of
//                                 XROUTE_HIST_SCALE=overflow's *(1 + (factor-1)*ratio)
//                                 -- see the file header's Measured result
//                                 for why this specific formula was tried.
//     XROUTE_HIST_FACTOR=<float>  multiplicative growth factor, only used
//                                 when XROUTE_HIST_MODE=mult, default 1.1
//     XROUTE_HIST_STREAK=<int>    require this many *consecutive*
//                                 round-boundary over-capacity observations
//                                 before history grows at all, default 1
//                                 (grow on every over-capacity round, the
//                                 original unconditional behavior -- see
//                                 updateHistory's comment and the "Growth
//                                 trigger and magnitude" section below for
//                                 the swept results)
//     XROUTE_HIST_SCALE=flat|overflow
//                                 "flat" (default) applies the same step
//                                 (or factor) to every over-capacity edge;
//                                 "overflow" scales it by that edge's
//                                 current usage/capacity ratio, so an edge
//                                 3x over capacity accumulates 3x as fast
//                                 as one just barely over -- the one
//                                 asymmetry history previously had against
//                                 lambda's own usage-proportional update
//     XROUTE_HIST_RELAX=0|1       this file's own new mechanism, default 1
//                                 (enabled). On any clean round, pop and
//                                 undo the single most-recently-applied
//                                 growth operation on that edge (LIFO),
//                                 exactly per the header above. Set to 0 to
//                                 reproduce iroute's exact permanent,
//                                 never-decreasing behavior for direct
//                                 comparison.
//     XROUTE_HIST_RATIO_EXPONENT=<float>
//                                 only used when XROUTE_HIST_MODE=ratio;
//                                 raises the usage/capacity ratio to this
//                                 power before multiplying, default 1.0
//                                 (plain ratio, iroute's original "ratio"
//                                 formula). A user-proposed follow-up:
//                                 exponent 2 punishes a badly-over edge far
//                                 harder (10x capacity -> multiply by 100,
//                                 not 10) while barely changing a
//                                 barely-over edge (1.01x -> 1.0201, not
//                                 1.01) -- worth trying specifically here
//                                 rather than in iroute, since relaxation
//                                 can undo an overshoot on the very next
//                                 clean round, changing the risk profile of
//                                 an aggressive formula that overshot badly
//                                 without relaxation (see iroute's own
//                                 IROUTE_HIST_MODE=ratio result, which
//                                 lost clearly at exponent 1 with no
//                                 relaxation available to recover from it).
//     XROUTE_HIST_EXPONENT_ESCALATION=<float>
//                                 only used when XROUTE_HIST_MODE=ratio;
//                                 default 0.0 (no escalation, exponent stays
//                                 fixed at XROUTE_HIST_RATIO_EXPONENT).
//                                 User-proposed: an edge's *effective*
//                                 exponent for this round's growth is
//                                 XROUTE_HIST_RATIO_EXPONENT plus this value
//                                 times how many still-unrelaxed penalties
//                                 are already on that edge's stack -- so an
//                                 edge penalized once uses the base
//                                 exponent, penalized a second time (without
//                                 an intervening clean round to relax it)
//                                 uses base+escalation, a third time
//                                 base+2*escalation, and so on. Reuses the
//                                 relaxation stack's own size as the "how
//                                 many times has this been penalized"
//                                 count -- deliberately, so an edge that's
//                                 been relaxed back down also automatically
//                                 drops back to a gentler exponent for its
//                                 next penalty, not just a gentler history
//                                 value. Only takes effect when
//                                 XROUTE_HIST_RELAX=1 (the default): with
//                                 relax disabled the stack is never
//                                 populated, so there is no count to
//                                 escalate from. Try 0.5 or 1.0 as starting
//                                 points per the user's own suggestion.
//     XROUTE_BARRIER=0|1          this file's new steep price curve,
//                                 default 1 (enabled) for every normal
//                                 routing call site. 0 reproduces kroute's
//                                 exact inherited threshold/power curve
//                                 everywhere, for a direct A/B comparison.
//                                 Never affects the relaxed cleanup pass
//                                 (see costExponentTerm), which keeps
//                                 kroute's gentler curve regardless.
//     XROUTE_BARRIER_START=<float>       ratio where the barrier starts
//                                 rising above zero, default 0.6
//     XROUTE_BARRIER_MUST=<float>        ratio where the "must-go" rational
//                                 blowup segment begins, default 0.95
//     XROUTE_BARRIER_MID_POWER=<float>   power of the 60%-95% ramp,
//                                 default 3.0
//     XROUTE_BARRIER_MID_CEILING=<float> exponent value the 60%-95% ramp
//                                 reaches by XROUTE_BARRIER_MUST, and the
//                                 95-100% segment's own starting point,
//                                 default 20.0
//     XROUTE_BARRIER_OVERCAP_TERM=<float> fixed exponent used for any
//                                 ratio >= 1.0 (actual overflow), default
//                                 1000.0 -- deliberately large enough that
//                                 exp(epsilon * this) alone saturates the
//                                 existing kForbiddenPrice clamp on the
//                                 very first touch, at any reasonable
//                                 epsilon
//     All five XROUTE_BARRIER_* constants are a first empirically-motivated
//     starting point, not a tuned result -- exactly like
//     KROUTE_THRESHOLD/KROUTE_POWER before them, this curve needs its own
//     sweep once real numbers come in.
//     XROUTE_ORDER=none|rudy|centrality|combined
//                                 net routing order, computed once up front
//                                 from pin geometry alone, default
//                                 "combined". "none" reproduces kroute's
//                                 exact plain netlist order. "rudy" revives
//                                 droute.cpp's proven risk-based reordering
//                                 alone. "centrality" applies only this
//                                 file's new inflexibility-first ordering.
//                                 "combined" blends both via percentile
//                                 rank (not raw score -- the two signals
//                                 have very different natural scales) and
//                                 is this file's default.

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
// support XROUTE_HIST_STREAK, which gates history growth behind N
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

struct Prices {
  std::vector<double> h, v;
  std::vector<double> histH, histV;
  std::vector<int> streakH, streakV;
  // Per-edge LIFO stack of every still-"active" growth operation -- see
  // this file's header for why a stack (not just a running total) is
  // required: XROUTE_HIST_RELAX pops and undoes exactly one entry, in
  // reverse order, on each clean round, so an edge dirty for N rounds
  // needs exactly N consecutive clean rounds to fully return to baseline.
  // Empty for almost every edge (only edges that have ever gone over
  // capacity accumulate entries), so the memory cost is proportional to
  // actual congestion, not grid size.
  std::vector<std::vector<HistOp>> histOpsH, histOpsV;
  explicit Prices(const RoutingGrid &g)
      : h(g.hCount(), 1.0), v(g.vCount(), 1.0), histH(g.hCount(), 1.0), histV(g.vCount(), 1.0),
        streakH(g.hCount(), 0), streakV(g.vCount(), 0), histOpsH(g.hCount()),
        histOpsV(g.vCount()) {}
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
    const char *p = std::getenv("XROUTE_POWER");
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
    const char *t = std::getenv("XROUTE_THRESHOLD");
    return t ? std::atof(t) : 0.2;
  }();
  return threshold;
}

// This file's own new price curve, user-specified directly: near-zero cost
// below 60% of capacity, escalating rapidly from there so an edge is
// already "VERY expensive" well before it's full, and a 95-100% zone that's
// only ever worth entering when nothing cheaper exists anywhere -- "100%
// full is fine, even one net's overflow is not." kroute's own
// priceExponentTerm curve above reaches only a normal, single-step
// multiplier (exponent 1.0) exactly at 100% capacity -- this file's whole
// premise, per the user's own diagnosis, is that this is nowhere near
// aggressive enough to actually deter a net that would otherwise take a
// cheap central path.
//
// Two segments, not one smooth curve throughout: a single power law steep
// enough to matter by 95% is far too flat to have moved at all by 70%, and
// one gentle enough to start rising at 60% barely reaches anything by 95%.
// The user's spec needs both a real (if moderate) ramp through the middle
// band and a much sharper final one, not a single compromise curve.
//
//   ratio < XROUTE_BARRIER_START (0.6):  term = 0 (unchanged from baseline)
//   START <= ratio < XROUTE_BARRIER_MUST (0.95):
//       term rises 0 -> XROUTE_BARRIER_MID_CEILING along a power curve
//   MUST <= ratio < 1.0:
//       term rises from MID_CEILING toward a very large value via a
//       rational (1/(1-u)) blowup -- deliberately not clamped here, since
//       the call site's existing min(kForbiddenPrice, ...) is exactly the
//       safety net this codebase already built for this purpose (see
//       negotiateWire's own comment on the mroute-era exp-to-infinity bug).
//   ratio >= 1.0 (actual overflow): term = a fixed, deliberately huge
//       constant -- this needs to saturate toward kForbiddenPrice on the
//       very first touch, not build up gradually over many rounds, because
//       even one net's overflow is the thing this curve exists to prevent.
//
// All four constants are a first empirically-motivated starting point, not
// a tuned result -- exactly like KROUTE_THRESHOLD/KROUTE_POWER before them,
// this needs its own sweep (see XROUTE_BARRIER=0 for a direct A/B against
// the unmodified kroute curve at equal round budgets).
double barrierExponentTerm(double usage, double cap) {
  static const double start = [] {
    const char *s = std::getenv("XROUTE_BARRIER_START");
    return s ? std::atof(s) : 0.6;
  }();
  static const double must = [] {
    const char *s = std::getenv("XROUTE_BARRIER_MUST");
    return s ? std::atof(s) : 0.95;
  }();
  static const double midPower = [] {
    const char *p = std::getenv("XROUTE_BARRIER_MID_POWER");
    return p ? std::atof(p) : 3.0;
  }();
  static const double midCeiling = [] {
    const char *c = std::getenv("XROUTE_BARRIER_MID_CEILING");
    return c ? std::atof(c) : 20.0;
  }();
  static const double overCapTerm = [] {
    const char *o = std::getenv("XROUTE_BARRIER_OVERCAP_TERM");
    return o ? std::atof(o) : 1000.0;
  }();
  double ratio = usage / cap;
  if (ratio < start)
    return 0.0;
  if (ratio >= 1.0)
    return overCapTerm;
  if (ratio < must) {
    double u = (ratio - start) / (must - start);
    return midCeiling * std::pow(u, midPower);
  }
  double u = (ratio - must) / (1.0 - must); // 0 at MUST, -> 1 as ratio -> 1.0
  return midCeiling + 10.0 * (u / std::max(1e-9, 1.0 - u));
}

// Dispatches between the new barrier (this file's default) and kroute's
// original curve. `useBarrier` is false only for the relaxed cleanup pass
// (see relaxedCleanup/XROUTE_CLEANUP_THRESHOLD) -- that pass exists
// specifically to be lenient, so it deliberately keeps the gentler
// inherited curve rather than this file's much sharper one, regardless of
// XROUTE_BARRIER. XROUTE_BARRIER=0 disables the new curve everywhere,
// reproducing kroute's exact pricing for a direct A/B comparison.
double costExponentTerm(double usage, double cap, double threshold, bool useBarrier) {
  static const bool barrierEnabled = [] {
    const char *b = std::getenv("XROUTE_BARRIER");
    return b ? std::atoi(b) != 0 : true;
  }();
  if (!useBarrier || !barrierEnabled || threshold >= 1.0)
    return priceExponentTerm(usage, cap, threshold);
  return barrierExponentTerm(usage, cap);
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

// `streakThreshold` (XROUTE_HIST_STREAK, default 1): an edge must be over
// capacity for this many *consecutive* round boundaries before growth is
// applied at all -- not merely "was over capacity this round," which is
// what every call site did before this parameter existed. Threshold 1
// reproduces that exact prior behavior (every over-capacity round grows
// immediately), so this is a strict generalization, never a behavior
// change at its default. Below threshold, the streak counter still
// advances -- it just doesn't unlock growth yet.
//
// `scaleByOverflow` (XROUTE_HIST_SCALE=overflow): instead of a flat step
// (or a flat factor) applied uniformly to every over-capacity edge
// regardless of how far over it is, scale by the current round's
// usage/capacity ratio -- an edge at 3x capacity accumulates history three
// times as fast as one just barely over. This is the one asymmetry between
// history's update and lambda's own: lambda already scales with
// usage/capacity via priceExponentTerm; history previously did not. Ratio
// is computed from THIS round's usage even when growth is gated by a
// multi-round streak, matching "how bad is it right now that finally
// triggered growth," not an average over the streak.
// `relaxEnabled` (XROUTE_HIST_RELAX, default true): when an edge is over
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
// Net ordering: two independent priority signals, computed once up front
// from each net's own pin geometry -- no routing or pricing information
// involved, so this can run before a single net is routed. See the file
// header for the reasoning behind each; `nets` itself is left in netlist
// order (rep.nets and every other index-based assumption downstream still
// see that order) -- only the sequence route() *visits* nets in changes.
// ---------------------------------------------------------------------------

// Ported verbatim from droute.cpp -- see that file's header for the
// derivation. Each non-giant net's own bounding box contributes
// minWidth*HPWL/area, spread uniformly over every tile in that box, via a
// 2D difference array plus one prefix-sum pass (O(nets + grid area), not
// O(sum of bbox areas)).
std::vector<double> computeRudyDensity(const RoutingGrid &g,
                                        const std::vector<std::pair<std::vector<Point>, int>> &netPtsAndWidth) {
  std::vector<double> diff(static_cast<size_t>(g.nx + 1) * (g.ny + 1), 0.0);
  auto at = [&](int x, int y) -> double & { return diff[static_cast<size_t>(y) * (g.nx + 1) + x]; };
  for (const auto &[pts, width] : netPtsAndWidth) {
    int minX = pts[0].x, maxX = minX, minY = pts[0].y, maxY = minY;
    for (const Point &p : pts) {
      minX = std::min(minX, p.x);
      maxX = std::max(maxX, p.x);
      minY = std::min(minY, p.y);
      maxY = std::max(maxY, p.y);
    }
    int hpwl = (maxX - minX) + (maxY - minY);
    double area = static_cast<double>(maxX - minX + 1) * (maxY - minY + 1);
    double contribution = width * hpwl / area;
    at(minX, minY) += contribution;
    at(maxX + 1, minY) -= contribution;
    at(minX, maxY + 1) -= contribution;
    at(maxX + 1, maxY + 1) += contribution;
  }
  std::vector<double> density(static_cast<size_t>(g.nx) * g.ny, 0.0);
  for (int y = 0; y <= g.ny; ++y) {
    double running = 0.0;
    for (int x = 0; x <= g.nx; ++x) {
      running += at(x, y);
      if (x < g.nx && y < g.ny)
        density[static_cast<size_t>(y) * g.nx + x] = running;
    }
  }
  return density;
}

// Ported verbatim from droute.cpp: a net's own risk is the average
// pre-routing density over its own bounding box -- how contested the
// neighborhood it needs to route through already looks, before any actual
// routing decision has been made.
double netRudyRisk(const RoutingGrid &g, const std::vector<double> &density,
                    const std::vector<Point> &pts) {
  int minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
  for (const Point &p : pts) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
  }
  double sum = 0.0;
  long long count = 0;
  for (int y = minY; y <= maxY; ++y)
    for (int x = minX; x <= maxX; ++x) {
      sum += density[static_cast<size_t>(y) * g.nx + x];
      ++count;
    }
  return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

// New in this file: how *inflexible* a net's own topology is, independent
// of anything else routing that day. A net whose pins sit around its own
// bounding box's perimeter has a whole family of monotonic paths hugging
// the boundary in either direction, none of which need to touch the box's
// interior at all -- lots of freedom to route around whatever's congested
// in the middle. A net whose pins are packed toward the box's own center
// has no such option: every one of those pins already sits in the same
// small patch, so the tree is forced to converge there no matter what --
// real, unavoidable inflexibility, not a preference. (An earlier version
// of this reasoning used a corner-anchored 2-pin-plus-a-cluster example and
// concluded the opposite; that example doesn't represent the general case
// and the direction below is the corrected one.)
//
// Returns 0 (every pin exactly at the bbox centroid, maximally inflexible)
// up to roughly 1+ (pins out at the corners, maximally flexible) --
// average, over all pins, of each pin's distance from the bbox centroid
// normalized by the bbox's own half-diagonal, so it's comparable across
// nets of very different sizes. A single-point bbox (all pins coincident,
// including true single-pin footprints) returns 0 by convention: nothing
// to be flexible about, and nothing to be inflexible about either, but
// scheduling it as if maximally inflexible (route it early, while the grid
// is open) costs nothing since it has no real footprint to contend for.
double netCentralityScore(const std::vector<Point> &pts) {
  int minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
  for (const Point &p : pts) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
  }
  double cx = (minX + maxX) / 2.0, cy = (minY + maxY) / 2.0;
  double halfDiag = std::sqrt(std::pow((maxX - minX) / 2.0, 2) + std::pow((maxY - minY) / 2.0, 2));
  if (halfDiag < 1e-9)
    return 0.0;
  double sumNormDist = 0.0;
  for (const Point &p : pts) {
    double dx = p.x - cx, dy = p.y - cy;
    sumNormDist += std::sqrt(dx * dx + dy * dy) / halfDiag;
  }
  return sumNormDist / static_cast<double>(pts.size());
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
                    double epsilon, double threshold, bool useBarrier = true) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x) {
      size_t i = g.hIndex(x, c.cross, c.layer);
      if (g.hcap[i] <= 0)
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
        // tier's own routeNetFast doesn't route by real search. This same
        // clamp is exactly what keeps costExponentTerm's own barrier
        // (which is deliberately allowed to grow very large, even
        // unbounded in the overflow branch) from ever reaching literal
        // infinity either -- one safety net serves both curves.
        pr.h[i] = std::min(kForbiddenPrice, pr.h[i] * std::exp(epsilon * costExponentTerm(u.h[i], g.hcap[i], threshold, useBarrier)));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] = std::min(kForbiddenPrice, pr.v[i] * std::exp(epsilon * costExponentTerm(u.v[i], g.vcap[i], threshold, useBarrier)));
    }
  }
}

// Routes one net with the fast tier: same shape as nproute's routeNet.
void routeNetFast(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                   double epsilon, double threshold, RoutedNet &rn, bool useBarrier = true) {
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
      negotiateWire(g, prices, usage, rn.charges[i], epsilon, threshold, useBarrier);

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
    const char *m = std::getenv("XROUTE_MAX_MARGIN");
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
      fail("xroute: no path found between tree points even after margin expansion -- "
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
    const char *t = std::getenv("XROUTE_CLEANUP_THRESHOLD");
    return t ? std::atof(t) : 0.5;
  }();
  int passes = [] {
    const char *p = std::getenv("XROUTE_CLEANUP_PASSES");
    return p ? std::atoi(p) : 30;
  }();
  bool trace = std::getenv("XROUTE_TRACE") != nullptr;
  long long before = rep.bestOverflow;

  for (int pass = 0; pass < passes; ++pass) {
    for (RoutedNet &rn : rep.nets) {
      if (rn.giant)
        continue;
      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);

      routeNetFast(g, prices, usage, lp, epsilon, relaxedThreshold, rn, /*useBarrier=*/false);

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
                 double exponentEscalation = 0.0) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  SearchRegion region;
  bool trace = std::getenv("XROUTE_TRACE") != nullptr;

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

  // Net ordering: computed once, up front, from pin geometry alone -- see
  // the file header and the two functions above for what each signal means
  // and why. `nets` itself stays in netlist order; only the sequence the
  // rounds below *visit* it in changes. XROUTE_ORDER selects the mode so
  // each signal (and their combination) can be measured independently:
  //   none        plain netlist order -- kroute's exact behavior
  //   rudy        droute's proven risk-based ordering alone
  //   centrality  this file's new inflexibility ordering alone
  //   combined    both, blended (default)
  std::string orderMode = [] {
    const char *m = std::getenv("XROUTE_ORDER");
    return m ? std::string(m) : std::string("combined");
  }();
  std::vector<size_t> routeOrder;
  for (size_t i = 0; i < nets.size(); ++i)
    if (!nets[i].giant)
      routeOrder.push_back(i);
  if (orderMode != "none") {
    std::vector<double> density;
    if (orderMode == "rudy" || orderMode == "combined") {
      std::vector<std::pair<std::vector<Point>, int>> forDensity;
      for (const RoutedNet &rn : nets)
        if (!rn.giant)
          forDensity.emplace_back(rn.pts, rn.net->minWidth);
      density = computeRudyDensity(g, forDensity);
    }
    // Both signals are normalized to a comparable 0..1-ish scale before
    // blending: RUDY risk has no natural ceiling (raw width*hpwl/area
    // density), while centrality is already bounded, so combining them
    // unnormalized would let whichever has the larger natural range
    // dominate regardless of intent. Rank-based normalization (each net's
    // percentile position) sidesteps needing to guess at absolute scales --
    // the same self-normalizing move CLAUDE.md's own net-typing section
    // prescribes for bucketing F_i, applied here to blend two orderings
    // instead of one.
    std::vector<double> rudyOf(nets.size(), 0.0), centralityOf(nets.size(), 0.0);
    for (size_t idx : routeOrder) {
      if (orderMode == "rudy" || orderMode == "combined")
        rudyOf[idx] = netRudyRisk(g, density, nets[idx].pts);
      if (orderMode == "centrality" || orderMode == "combined")
        centralityOf[idx] = netCentralityScore(nets[idx].pts);
    }
    auto rankOf = [&](const std::vector<double> &score, bool descending) {
      std::vector<size_t> order = routeOrder;
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return descending ? score[a] > score[b] : score[a] < score[b];
      });
      std::vector<double> rank(nets.size(), 0.0);
      for (size_t r = 0; r < order.size(); ++r)
        rank[order[r]] = order.size() > 1 ? static_cast<double>(r) / (order.size() - 1) : 0.0;
      return rank;
    };
    // Rank 0 should mean "route first" for both signals: highest RUDY risk
    // first (descending), and lowest centrality score first (ascending --
    // a low score means pins packed near the bbox center, the inflexible,
    // route-it-while-the-grid-is-open case per the file header).
    std::vector<double> rudyRank = (orderMode == "rudy" || orderMode == "combined")
                                        ? rankOf(rudyOf, /*descending=*/true)
                                        : std::vector<double>(nets.size(), 0.0);
    std::vector<double> centralityRank = (orderMode == "centrality" || orderMode == "combined")
                                              ? rankOf(centralityOf, /*descending=*/false)
                                              : std::vector<double>(nets.size(), 0.0);
    std::stable_sort(routeOrder.begin(), routeOrder.end(), [&](size_t a, size_t b) {
      double sa, sb;
      if (orderMode == "rudy") {
        sa = rudyRank[a];
        sb = rudyRank[b];
      } else if (orderMode == "centrality") {
        sa = centralityRank[a];
        sb = centralityRank[b];
      } else {
        sa = rudyRank[a] + centralityRank[a];
        sb = rudyRank[b] + centralityRank[b];
      }
      return sa < sb; // lower combined rank routes first
    });
  }

  // Phase A: nproute's algorithm, unmodified in spirit, aside from visiting
  // nets in routeOrder rather than netlist order.
  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxFastRounds; ++round) {
    for (size_t idx : routeOrder) {
      RoutedNet &rn = nets[idx];
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
    for (size_t idx : routeOrder) {
      RoutedNet &rn = nets[idx];
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
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
  Design d = parse("xroute_selftest_zigzag.gr", kZigzagGr);
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
  Design d = parse("xroute_selftest_priceoverflow.gr", kPriceOverflowGr);
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
// XROUTE_HIST_STEP=0) layer 0's frozen-but-lower lambda wins every time,
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
  Design d = parse("xroute_selftest_history.gr", kHistoryTrapGr);
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
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
// XROUTE_HIST_RELAX=0 must reproduce iroute's exact permanent behavior --
// this is the regression guard for that claim, not the relaxation
// mechanism itself (see testRelaxationUndoesExactlyOneStepPerCleanRound and
// testRelaxationFullyRestoresBaselineAfterEqualCleanRounds below for that).
void testDisablingRelaxReproducesPermanentBehavior() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
        "with XROUTE_HIST_RELAX=0, history never decreases even after usage "
        "drops back under capacity -- reproduces iroute's exact permanent "
        "behavior, the fallback this file's relaxation mechanism can always "
        "be switched off back to");
}

// The actual new mechanism: an edge dirty for exactly one round, then one
// clean round, should have its single growth operation popped and undone
// exactly -- back to the 1.0 no-op baseline, not merely "decreased."
void testRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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

// XROUTE_HIST_STREAK: growth must be gated behind N *consecutive*
// over-capacity round boundaries, and a single clear round must reset the
// count to zero rather than merely pausing it -- otherwise an edge that
// flickers over/under/over capacity every other round would still
// eventually accumulate growth from unrelated non-consecutive rounds,
// which defeats the entire point of requiring a *streak*.
void testHistoryStreakRequiresConsecutiveOvercapacityRounds() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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

// XROUTE_HIST_SCALE=overflow: an edge sitting further over capacity should
// accumulate history faster than one just barely over, unlike the default
// flat step which treats "1 unit over" and "10x over" identically.
void testHistoryScalesWithOverflowMagnitude() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
        "XROUTE_HIST_SCALE=overflow makes the 10x-over edge accumulate "
        "history faster than the 1.5x-over edge in the very same round -- "
        "the decisive behavioral change this mode is supposed to buy");
  check(scaledWayOver > flatWayOver,
        "the way-over edge grows faster under overflow-scaling than it "
        "would have under the flat default");
}

// XROUTE_HIST_MODE=ratio: histH *= usage/capacity directly, no separate
// step or factor. A user-proposed formula, n2 = n1*(1 + overflow_percent/100),
// which is algebraically exactly usage/capacity when "overflow_percent" is
// the excess over capacity as a percentage (e.g. usage=150,cap=100 -> 50%
// -> 1 + 0.5 = 1.5 = 150/100).
void testHistoryRatioModeMultipliesByUsageOverCapacityExactly() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices pr(g);
  Usage fiftyOver(g);
  fiftyOver.h[idx] = 3; // capacity 2 -> ratio exactly 1.5
  updateHistory(g, pr, fiftyOver, /*histStep=*/0.0, HistMode::kRatio, /*histFactor=*/0.0);
  check(std::abs(pr.histH[idx] - 1.5) < 1e-9,
        "XROUTE_HIST_MODE=ratio on a 50%-over edge (usage 3, capacity 2) "
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

// XROUTE_HIST_RATIO_EXPONENT: squaring the ratio should punish a badly-over
// edge much harder while barely changing a barely-over one -- the decisive
// property the user proposed this for.
void testHistoryRatioExponentSquaresTheRatio() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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

// XROUTE_HIST_EXPONENT_ESCALATION: the effective exponent for a round's
// growth should climb with how many still-unrelaxed penalties are already
// on the stack, and a relaxed edge should use a *lower* effective exponent
// for its next penalty than an edge that stayed dirty continuously -- not
// merely end up at a lower history value, the escalation level itself must
// have dropped. Uses a fixed usage/capacity=2.0 throughout (capacity 2,
// usage 4) so every factor is an exact power of 2, checkable exactly.
void testExponentEscalationTracksStackSizeAndRelaxesWithIt() {
  Design d = parse("xroute_selftest_basic.gr", kBasicGr);
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
        "the property XROUTE_HIST_EXPONENT_ESCALATION's header claims by "
        "reusing the stack's own size as the escalation count");
}

// ---------------------------------------------------------------------------
// New in this file: the steep price barrier and the two net-ordering
// signals. See the file header for the reasoning; these tests check the
// formulas directly against hand-computed values, then prove the ordering
// mechanism changes a real routing outcome, not just a computed score.
// ---------------------------------------------------------------------------

void testBarrierIsFlatBelowStartThreshold() {
  check(barrierExponentTerm(30.0, 100.0) == 0.0,
        "barrier term is exactly zero well under the 60% start threshold");
  check(barrierExponentTerm(59.0, 100.0) == 0.0,
        "barrier term is exactly zero just under the start threshold");
}

void testBarrierMonotonicallyIncreasesTowardCapacity() {
  double at60 = barrierExponentTerm(60.0, 100.0);
  double at80 = barrierExponentTerm(80.0, 100.0);
  double at95 = barrierExponentTerm(95.0, 100.0);
  double at99 = barrierExponentTerm(99.0, 100.0);
  check(at60 <= at80 && at80 < at95 && at95 < at99,
        "barrier term rises monotonically from the 60% start threshold "
        "toward 100% capacity");
  check(at99 > at95 * 2.0,
        "the 95-100% 'must-go' band accelerates sharply, not just "
        "linearly, past the 60-95% ramp's own rate of increase");
}

void testBarrierSaturatesAtAndAboveCapacity() {
  double atCap = barrierExponentTerm(100.0, 100.0);
  double over = barrierExponentTerm(150.0, 100.0);
  check(atCap >= 1000.0,
        "barrier term jumps to its fixed, deliberately huge constant "
        "exactly at 100% capacity -- no gradual ramp-in once actually full");
  check(over >= 1000.0,
        "barrier term stays at that same huge constant for any degree of "
        "actual overflow -- 50% over capacity is not treated as any more "
        "forgivable than exactly at capacity");
}

void testBarrierIsFarSteeperThanBaselineCurveNearCapacity() {
  // kroute's own inherited curve (threshold 0.2, power 2.5) reaches only a
  // normal, single-step exponent of 1.0 exactly at 100% capacity -- this
  // file's whole premise is that this is nowhere near aggressive enough.
  // At 90% utilization the difference should already be dramatic.
  double oldTerm = priceExponentTerm(90.0, 100.0, defaultThreshold());
  double newTerm = barrierExponentTerm(90.0, 100.0);
  check(newTerm > oldTerm * 5.0,
        "at 90% utilization, this file's barrier term (" + std::to_string(newTerm) +
        ") is far larger than kroute's inherited curve at the same ratio (" +
        std::to_string(oldTerm) + ")");
}

void testCostExponentTermDispatchesOnUseBarrierAndEnvFlag() {
  // useBarrier=false (the relaxed cleanup pass's own call site) must keep
  // the gentle inherited curve regardless of the new barrier existing at
  // all -- cleanup's whole point is to be lenient.
  double relaxed = costExponentTerm(95.0, 100.0, defaultThreshold(), /*useBarrier=*/false);
  double normal = costExponentTerm(95.0, 100.0, defaultThreshold(), /*useBarrier=*/true);
  check(relaxed == priceExponentTerm(95.0, 100.0, defaultThreshold()),
        "useBarrier=false reproduces kroute's exact inherited curve, "
        "unaffected by this file's new barrier -- the relaxed cleanup "
        "pass stays lenient on purpose");
  check(normal > relaxed * 3.0,
        "useBarrier=true (every normal routing call site) uses the far "
        "steeper barrier instead, at the identical usage/capacity ratio");
}

// A rectangle's four corners are each exactly one half-diagonal from the
// centroid by construction, so every point's normalized distance is
// exactly 1.0 regardless of the rectangle's actual size -- an exact,
// hand-checkable value, not an approximation.
void testCentralityScoreIsExactlyOneForPerimeterCorners() {
  std::vector<Point> corners = {{0, 0}, {0, 10}, {10, 0}, {10, 10}};
  double score = netCentralityScore(corners);
  check(std::abs(score - 1.0) < 1e-9,
        "four pins at the exact corners of their own bounding box score "
        "exactly 1.0 (maximally flexible) -- score was " + std::to_string(score));
}

void testCentralityScoreIsLowWhenPinsClusterAtCenter() {
  // Same overall bounding box as the corners case (0,0)-(10,10), but four
  // of the six pins sit exactly at the centroid (distance 0) and only two
  // still define the box's actual corners (distance 1.0 each) -- average
  // (0+0+0+0+1+1)/6 = 1/3, an exact hand-computed value, and far below the
  // all-corners case above.
  std::vector<Point> mostlyCentral = {{5, 5}, {5, 5}, {5, 5}, {5, 5}, {0, 0}, {10, 10}};
  double score = netCentralityScore(mostlyCentral);
  check(std::abs(score - (1.0 / 3.0)) < 1e-9,
        "four centroid pins plus the two corner-defining pins score "
        "exactly 1/3 -- score was " + std::to_string(score));
  double cornersScore = netCentralityScore({{0, 0}, {0, 10}, {10, 0}, {10, 10}});
  check(score < cornersScore,
        "the center-clustered net scores lower (more inflexible) than the "
        "all-corners net sharing the same bounding box");
}

void testCentralityScoreIsZeroForDegenerateBbox() {
  check(netCentralityScore({{3, 3}, {3, 3}}) == 0.0,
        "a single-point bounding box (all pins coincident) scores exactly "
        "0.0 by convention -- nothing to be flexible or inflexible about");
}

// The decisive end-to-end test: two nets on a 7x5 grid whose only real
// bottleneck is a single H edge, capacity-adjusted down to 1, between tile
// columns 3 and 4 at row 2 (real coordinates x:30-40 at y:20). netFlexible's
// two pins (tile (0,2) and (6,4)) give it two genuinely different, equal-
// length L-shape candidates -- one whose horizontal leg runs along row 2
// (straight through the bottleneck), the other whose long horizontal leg
// runs along row 4 instead (clear of it entirely) -- real freedom to avoid
// the middle. netInflexible's six pins -- four coincident at tile (2,2),
// plus (0,4) and (6,0) still defining the same-sized bounding box -- score
// far lower on centrality (more inflexible, per the unit test above): its
// own natural route already runs along row 2 through the bottleneck with no
// equally-cheap alternative the fast tier's limited candidate set offers.
// Listed with the flexible net first, so plain netlist order hands the
// direct path through the bottleneck to the wrong net (the one that could
// easily have avoided it) and forces the inflexible one around instead --
// verified empirically against the real fast-tier candidate generator
// (segments confirmed directly, not assumed) before writing the assertions
// below. Under XROUTE_ORDER=centrality the inflexible net is visited first,
// claims the direct path, and the flexible net detours via row 4 instead --
// the roles invert, proving the ordering mechanism actually changes which
// net wins a shared resource.
const char *kCentralityOrderGr = R"(grid 7 5 1
vertical capacity 10
horizontal capacity 10
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 2
netFlexible 0 2 1
5 25 1
65 45 1
netInflexible 1 6 1
25 25 1
25 25 1
25 25 1
25 25 1
5 45 1
65 5 1
1
3 2 1 4 2 1 1
)";

void testCentralityOrderingInvertsWhoClaimsSharedCorridor() {
  // Baseline: plain netlist order (netFlexible, listed first, claims
  // whatever's cheapest first).
  Design dPlain = parse("xroute_selftest_centralorder.gr", kCentralityOrderGr);
  setenv("XROUTE_ORDER", "none", 1);
  RunReport plain = route(dPlain, 5, 5, 0.5, 0.0, HistMode::kAdditive, 0.0);
  setenv("XROUTE_ORDER", "combined", 1); // restore this file's own default
  const RoutedNet *plainFlex = nullptr;
  for (const RoutedNet &rn : plain.nets)
    if (rn.net->name == "netFlexible")
      plainFlex = &rn;
  check(plainFlex != nullptr, "netFlexible is present under plain netlist order");

  // With centrality ordering: netInflexible should be visited first even
  // though it's listed second.
  Design dOrdered = parse("xroute_selftest_centralorder2.gr", kCentralityOrderGr);
  setenv("XROUTE_ORDER", "centrality", 1);
  RunReport ordered = route(dOrdered, 5, 5, 0.5, 0.0, HistMode::kAdditive, 0.0);
  setenv("XROUTE_ORDER", "combined", 1);
  const RoutedNet *orderedFlex = nullptr;
  for (const RoutedNet &rn : ordered.nets)
    if (rn.net->name == "netFlexible")
      orderedFlex = &rn;
  check(orderedFlex != nullptr, "netFlexible is present under centrality ordering");

  check(plainFlex && segsConnected(plainFlex->segs), "netFlexible is one connected component under plain order");
  check(orderedFlex && segsConnected(orderedFlex->segs), "netFlexible is one connected component under centrality order");

  // The decisive check: does netFlexible's own path actually cross the
  // single capacity-1 bottleneck edge -- tile (3,2)-(4,2), the fixture's
  // capacity adjustment target? RoutedNet::segs are tile coordinates
  // throughout (writeSolution converts to real coordinates only when the
  // final .route file is written), so the check is tile column 3 to 4 at
  // tile row 2, not the real-unit x:30-40/y:20 an emitted solution would
  // show -- confirmed directly against this test's own output before
  // fixing this check (an earlier version compared tile-coordinate segs
  // against real-coordinate thresholds and failed for exactly that reason).
  auto crossesBottleneck = [](const RoutedNet &rn) {
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && s.y1 == s.y2 && s.y1 == 2 &&
          std::min(s.x1, s.x2) <= 3 && std::max(s.x1, s.x2) >= 4)
        return true;
    return false;
  };
  bool plainCrosses = plainFlex && crossesBottleneck(*plainFlex);
  bool orderedCrosses = orderedFlex && crossesBottleneck(*orderedFlex);
  check(plainCrosses,
        "under plain netlist order, netFlexible (listed first) claims the "
        "cheap direct path straight through the shared bottleneck");
  check(!orderedCrosses,
        "under centrality ordering, netInflexible is visited first instead "
        "and claims that same bottleneck -- netFlexible, which has the "
        "freedom to do so, detours around it via a different row. The "
        "inversion between this and the check above is the decisive proof "
        "that reordering changed which net wins a shared resource, not "
        "just that it computed a score");
  bool inflexPlainCrosses = false, inflexOrderedCrosses = false;
  for (const RoutedNet &rn : plain.nets)
    if (rn.net->name == "netInflexible")
      inflexPlainCrosses = crossesBottleneck(rn);
  for (const RoutedNet &rn : ordered.nets)
    if (rn.net->name == "netInflexible")
      inflexOrderedCrosses = crossesBottleneck(rn);
  check(!inflexPlainCrosses && inflexOrderedCrosses,
        "netInflexible's own use of the bottleneck exactly mirrors "
        "netFlexible's, in reverse: it detours under plain order (netFlexible "
        "got there first) and claims the direct path once centrality "
        "ordering visits it first instead");
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
  testBarrierIsFlatBelowStartThreshold();
  testBarrierMonotonicallyIncreasesTowardCapacity();
  testBarrierSaturatesAtAndAboveCapacity();
  testBarrierIsFarSteeperThanBaselineCurveNearCapacity();
  testCostExponentTermDispatchesOnUseBarrierAndEnvFlag();
  testCentralityScoreIsExactlyOneForPerimeterCorners();
  testCentralityScoreIsLowWhenPinsClusterAtCenter();
  testCentralityScoreIsZeroForDegenerateBbox();
  testCentralityOrderingInvertsWhoClaimsSharedCorridor();
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
  if (const char *e = std::getenv("XROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("XROUTE_FAST_ROUNDS"); r)
    maxFastRounds = std::atoi(r);
  if (const char *r = std::getenv("XROUTE_SLOW_ROUNDS"); r)
    maxSlowRounds = std::atoi(r);
  if (const char *s = std::getenv("XROUTE_HIST_STEP"); s)
    histStep = std::atof(s);
  if (const char *f = std::getenv("XROUTE_HIST_FACTOR"); f)
    histFactor = std::atof(f);
  if (const char *m = std::getenv("XROUTE_HIST_MODE"); m && std::string(m) == "mult")
    histMode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("XROUTE_HIST_MODE"); m && std::string(m) == "ratio")
    histMode = HistMode::kRatio;
  if (const char *s = std::getenv("XROUTE_HIST_STREAK"); s)
    streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("XROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
    scaleByOverflow = true;
  bool relaxEnabled = true;
  if (const char *r = std::getenv("XROUTE_HIST_RELAX"); r)
    relaxEnabled = std::string(r) != "0";
  double ratioExponent = 1.0;
  if (const char *r = std::getenv("XROUTE_HIST_RATIO_EXPONENT"); r)
    ratioExponent = std::atof(r);
  double exponentEscalation = 0.0;
  if (const char *r = std::getenv("XROUTE_HIST_EXPONENT_ESCALATION"); r)
    exponentEscalation = std::atof(r);

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                         streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent,
                         exponentEscalation);
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
