// qroute.cpp
//
// Forked from oroute.cpp -- inherits phase A/B, the history mechanism,
// the entire joint candidate-assignment machinery (cluster discovery,
// candidate generation via both the fixed-MST and MSMV generators, exact
// branch-and-bound assignment), and oroute's own cooperative-eviction
// mechanism (`recruitEvictionHelpers`), all completely unchanged --
// though eviction itself defaults to *off* in this file
// (`QROUTE_MAX_EVICTION_HELPERS` default 0, vs. oroute's 3), to isolate
// this file's own new variable from oroute's already-measured one. See
// broute.cpp's, froute.cpp's, aroute.cpp's, nroute.cpp's, and oroute.cpp's
// own headers for the full lineage.
//
// oroute.cpp's own real-benchmark result (HISTORY_COST_SWEEP.md Part 16)
// found the first accepted cluster anywhere in this whole sweep -- but
// traced directly against nroute's own cluster-by-cluster log, the win
// turned out *not* to come from a recruited helper physically relocating
// (0 of 218 clusters ever moved one, including the one accepted cluster).
// It came from a side effect of *ripping the helper up before candidate
// generation runs*: `negotiateWire` (called from inside `commitPath`,
// which every trial candidate `generateCandidatesForNet` tries commits
// through) raises an edge's *price* based on whatever `usage` is at that
// exact moment, and that price rise is never undone -- `undoCharge` only
// ever reverts `usage`, never `prices`. So the *sequence* of price bumps
// every trial commit across a whole cluster's candidate generation leaves
// behind depends on which nets' usage was present in the baseline at the
// time, and removing a helper's usage from that baseline -- even though
// the helper's own final route never changes -- changes what that
// sequence looks like for every other net's own candidate generation too.
//
// This file tests that mechanism directly, deliberately separated from
// whether the widening nets ever get to move at all. `recruitContextNets`
// (this file's own addition, sharing `rankLegalTouchingNets` with
// `recruitEvictionHelpers`) recruits up to `QROUTE_MAX_CONTEXT_NETS`
// (default 20 -- an order of magnitude past eviction's own default 3,
// affordable because these nets never enter the branch-and-bound's own
// search space at all) currently-legal, cluster-touching nets, ripped up
// alongside the cluster's real members for exactly the window candidate
// generation runs in, then restored to their *exact* original charges
// immediately afterward, before the greedy incumbent, the exact search,
// or the accept/reject gate ever run -- see
// `negotiateJointAssignment`'s own comments for precisely where the
// rip-up and restoration windows sit. A context net never appears in
// `netIdx`, never gets a candidate slot, and is never a possible outcome
// of the search at all: unlike a recruited eviction helper (an offer the
// search may or may not take), a context net's only role is to be
// temporarily absent, then present again, unconditionally.
//
// `QROUTE_MAX_CONTEXT_NETS=0` disables context widening entirely, and
// `QROUTE_MAX_EVICTION_HELPERS` can still be set nonzero to combine both
// mechanisms in the same run -- but they default to context widening
// alone, isolating it as this file's own single new variable per this
// project's own discipline.
//
// Measured result (newblue1, 30/40-round screen, both gr_verify- and
// eval2008.pl-cross-checked): 41954/22/6634267 -- the *exact same*
// **-2 overflow** win as oroute.cpp's own result, at less than a third
// the wirelength cost (+250 vs. oroute's +703 over the 6634017
// baseline). 218 clusters attempted, 1 accepted (cluster 18, 0 recruited
// helpers, 20 context nets, 12 nets/100 candidates -- the same candidate
// *count* nroute's own identical search generated for the identical 12
// nets, confirming the difference is genuinely in candidate *content*,
// not scope). Traced directly against nroute's and oroute's own logs for
// this exact cluster: nroute (no widening at all) finds nothing
// (18 -> 18); oroute (context widening entangled with 3 real B&B
// candidate-owning helpers, 15 nets/127 candidates) finds 18 -> 16;
// qroute (context widening alone, 12 nets/100 candidates, identical
// scope to nroute) finds the *same* 18 -> 16 -- confirming the win was
// never about the extra B&B participants at all.
//
// Follow-up width sweep (same screen, `QROUTE_MAX_CONTEXT_NETS` varied):
// 20 (default) -> 41954 (1 accepted); 50 -> 41952 (2 accepted, a
// *different* second cluster than 150 found); 150 -> 41952 (2 accepted,
// same cluster 18 win, cheaper wirelength than 50); unbounded -> **41956,
// 0 accepted** -- cluster 18's own win, present at both 50 and 150,
// reverses once its natural (uncapped) context pool of 241 nets is fully
// admitted. Widening is not monotonic: helpful in a narrow middle band,
// actively harmful past it, on this benchmark at this round budget. The
// default (20) is a first empirically-motivated guess only, not
// re-tuned from this result -- a proper sweep needs many more points
// than four to characterize the real shape of this curve, not just its
// non-monotonicity. Full discussion: HISTORY_COST_SWEEP.md Part 17.
//

// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o qroute qroute.cpp
//
// Use:
//     ./qroute benchmark.gr solution.route
//     ./qroute --selftest
//
// Environment variables (all optional; unlisted ones behave exactly as in
// hroute.cpp, since phase A/phase B are unchanged):
//     QROUTE_EPS=<float>          fast-tier step size epsilon, default 0.02
//     QROUTE_THRESHOLD=<float>    fast-tier early-warning curve start,
//                                 default 0.2
//     QROUTE_POWER=<float>        fast-tier curve escalation power, default 2.5
//     QROUTE_FAST_ROUNDS=<int>    max phase-A (fast tier) rounds, default 60
//                                 (every prior router in this lineage
//                                 defaults to 30 -- raised here because the
//                                 measured result below only shows up with
//                                 a much longer round budget; see Measured
//                                 result)
//     QROUTE_SLOW_ROUNDS=<int>    max phase-B (escalation) rounds, default
//                                 150 (same reason as QROUTE_FAST_ROUNDS
//                                 above)
//     QROUTE_CLEANUP_THRESHOLD=<float>, QROUTE_CLEANUP_PASSES=<int>
//                                 phase A's cleanup pass, same as nproute's
//                                 (defaults 0.5 / 30)
//     QROUTE_MAX_MARGIN=<int>     slow-tier Dijkstra search margin cap,
//                                 same as mroute's (default 6)
//     QROUTE_TRACE=1              print each round's overflow, the cleanup
//                                 pass's before/after, and phase B's
//                                 accepted/rejected counts, to stderr
//     QROUTE_HIST_STEP=<float>    per-round additive growth applied to an
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
//     QROUTE_HIST_MODE=add|mult|ratio   growth rule: "add" (default) does
//                                 histH += QROUTE_HIST_STEP; "mult" does
//                                 histH *= QROUTE_HIST_FACTOR instead;
//                                 "ratio" does histH *= usage_e/capacity_e
//                                 directly (no separate step or factor at
//                                 all -- an edge at 150% of capacity
//                                 multiplies its history by 1.5 that
//                                 round, unconditionally). This
//                                 is the direct multiplicative analogue of
//                                 QROUTE_HIST_SCALE=overflow's *(1 + (factor-1)*ratio)
//                                 -- see the file header's Measured result
//                                 for why this specific formula was tried.
//     QROUTE_HIST_FACTOR=<float>  multiplicative growth factor, only used
//                                 when QROUTE_HIST_MODE=mult, default 1.1
//     QROUTE_HIST_STREAK=<int>    require this many *consecutive*
//                                 round-boundary over-capacity observations
//                                 before history grows at all, default 1
//                                 (grow on every over-capacity round, the
//                                 original unconditional behavior -- see
//                                 updateHistory's comment and the "Growth
//                                 trigger and magnitude" section below for
//                                 the swept results)
//     QROUTE_HIST_SCALE=flat|overflow
//                                 "flat" (default) applies the same step
//                                 (or factor) to every over-capacity edge;
//                                 "overflow" scales it by that edge's
//                                 current usage/capacity ratio, so an edge
//                                 3x over capacity accumulates 3x as fast
//                                 as one just barely over -- the one
//                                 asymmetry history previously had against
//                                 lambda's own usage-proportional update
//     QROUTE_HIST_RELAX=0|1       this file's own new mechanism, default 1
//                                 (enabled). On any clean round, pop and
//                                 undo the single most-recently-applied
//                                 growth operation on that edge (LIFO),
//                                 exactly per the header above. Set to 0 to
//                                 reproduce iroute's exact permanent,
//                                 never-decreasing behavior for direct
//                                 comparison.
//     QROUTE_HIST_RATIO_EXPONENT=<float>
//                                 only used when QROUTE_HIST_MODE=ratio;
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
//     QROUTE_HIST_EXPONENT_ESCALATION=<float>
//                                 only used when QROUTE_HIST_MODE=ratio;
//                                 default 0.0 (no escalation, exponent stays
//                                 fixed at QROUTE_HIST_RATIO_EXPONENT).
//                                 User-proposed: an edge's *effective*
//                                 exponent for this round's growth is
//                                 QROUTE_HIST_RATIO_EXPONENT plus this value
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
//                                 QROUTE_HIST_RELAX=1 (the default): with
//                                 relax disabled the stack is never
//                                 populated, so there is no count to
//                                 escalate from. Try 0.5 or 1.0 as starting
//                                 points per the user's own suggestion.
//     QROUTE_JOINT=0|1            this file's own new mechanism, default 1
//                                 (enabled). Phase D: for each overflow
//                                 cluster, generate several candidate
//                                 routes per contending net against one
//                                 frozen baseline, then pick one candidate
//                                 per net jointly via exact
//                                 branch-and-bound to minimize the
//                                 cluster's total overflow, rather than
//                                 committing net by net and reacting to
//                                 whatever the last commit changed. Set to
//                                 0 to reproduce plain phase A/B behavior.
//     QROUTE_MAX_CLUSTER_NETS=<int>
//                                 caps how many overflowed nets a single
//                                 cluster's joint search ever considers at
//                                 once, default 12 -- same starting point
//                                 broute/froute already validated for the
//                                 identical reason (an uncapped cluster
//                                 can sprawl to thousands of nets; see
//                                 HISTORY_COST_SWEEP.md Part 12). Does not
//                                 count recruited eviction helpers, which
//                                 have their own separate cap below.
//     QROUTE_MAX_EVICTION_HELPERS=<int>
//                                 oroute.cpp's own addition, inherited
//                                 unchanged except for its default: caps
//                                 how many currently-legal nets
//                                 recruitEvictionHelpers ever recruits as
//                                 real branch-and-bound candidate owners,
//                                 default **0** in this file (vs. oroute's
//                                 3) -- cooperative eviction is off by
//                                 default here, to isolate this file's own
//                                 new variable (context widening) from
//                                 oroute's already-measured one. Set
//                                 nonzero to combine both mechanisms in
//                                 the same run.
//     QROUTE_MAX_CONTEXT_NETS=<int>
//                                 this file's own addition: caps how many
//                                 currently-legal nets recruitContextNets
//                                 ever recruits purely to widen the
//                                 candidate-generation baseline, default
//                                 20 -- an order of magnitude past
//                                 eviction's own default, affordable
//                                 because context nets never join the
//                                 branch-and-bound's own search space at
//                                 all (see the file header for the full
//                                 mechanism). Set to 0 to disable context
//                                 widening entirely.
//     QROUTE_CANDIDATE_WEIGHTS=<csv>
//                                 length-weight schedule for the fixed-MST
//                                 candidate generator (routeNetSlow),
//                                 default "1.0,0.1,0.01,0" -- same format
//                                 and mechanism as yroute.cpp's
//                                 YROUTE_RESCUE_WEIGHTS. Each net's own
//                                 original route is always an additional
//                                 standing candidate, regardless of this
//                                 schedule. Applies identically to
//                                 recruited eviction helpers.
//     QROUTE_MSMV_CANDIDATE_WEIGHTS=<csv>
//                                 nroute.cpp's own addition, inherited
//                                 unchanged: length-weight schedule for the
//                                 MSMV candidate generator
//                                 (routeNetSlowMSMV), default
//                                 "1.0,0.1,0.01,0" -- independently
//                                 tunable from QROUTE_CANDIDATE_WEIGHTS
//                                 since the two search methods find
//                                 candidates through mechanically
//                                 different means (fixed-MST attachment-
//                                 at-pins vs. attach-anywhere-on-
//                                 committed-wire). Only runs for nets with
//                                 more than 2 pins (a 2-pin net has one
//                                 tree edge, so MSMV and the fixed-MST
//                                 path coincide for it). Applies
//                                 identically to recruited eviction
//                                 helpers.
//     QROUTE_MAX_BNB_NODES=<int>  per-cluster branch-and-bound node-
//                                 expansion budget, default 200000 -- a
//                                 first empirically-motivated guess, not a
//                                 tuned value; needs its own sweep once
//                                 real cluster shapes are measured.
//                                 Hitting the cap falls back to whatever
//                                 the best complete assignment found so
//                                 far is, which is never worse than
//                                 leaving the cluster untouched.
//     QROUTE_JOINT_LOG=<path>    writes a CSV (cluster,nets,
//                                 candidates_total,bnb_nodes,
//                                 overflow_before,greedy_incumbent_overflow,
//                                 bnb_best_overflow,hit_node_cap,accepted)
//                                 with one row per cluster Phase D
//                                 attempted. Unset by default.
//     QROUTE_PROGRESS_LOG=<path> writes a CSV (phase,step,overflow,
//                                 wirelength,vias,scored_wirelength,
//                                 elapsed_s) at every round/pass/cluster
//                                 checkpoint -- overflow and wirelength
//                                 together, meant to be plotted, unlike
//                                 QROUTE_TRACE's stderr-only overflow
//                                 trace. Unset by default.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <unordered_map>
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
// support QROUTE_HIST_STREAK, which gates history growth behind N
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
  // required: QROUTE_HIST_RELAX pops and undoes exactly one entry, in
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
    const char *p = std::getenv("QROUTE_POWER");
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
    const char *t = std::getenv("QROUTE_THRESHOLD");
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
// `lengthWeight` (default 1.0, a no-op), ported verbatim from yroute.cpp:
// this file's own candidate generator (see the header) reuses yroute's
// length-weight-decay trick to get several genuinely different candidate
// routes per net cheaply, without inventing a new search.
double edgeCost(double lambda, double hist, int amount, double lengthWeight = 1.0) {
  return hist * (lengthWeight + lambda * amount);
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

// `streakThreshold` (QROUTE_HIST_STREAK, default 1): an edge must be over
// capacity for this many *consecutive* round boundaries before growth is
// applied at all -- not merely "was over capacity this round," which is
// what every call site did before this parameter existed. Threshold 1
// reproduces that exact prior behavior (every over-capacity round grows
// immediately), so this is a strict generalization, never a behavior
// change at its default. Below threshold, the streak counter still
// advances -- it just doesn't unlock growth yet.
//
// `scaleByOverflow` (QROUTE_HIST_SCALE=overflow): instead of a flat step
// (or a flat factor) applied uniformly to every over-capacity edge
// regardless of how far over it is, scale by the current round's
// usage/capacity ratio -- an edge at 3x capacity accumulates history three
// times as fast as one just barely over. This is the one asymmetry between
// history's update and lambda's own: lambda already scales with
// usage/capacity via priceExponentTerm; history previously did not. Ratio
// is computed from THIS round's usage even when growth is gated by a
// multi-round streak, matching "how bad is it right now that finally
// triggered growth," not an average over the streak.
// `relaxEnabled` (QROUTE_HIST_RELAX, default true): when an edge is over
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
        // tier's own routeNetFast doesn't route by real search.
        pr.h[i] = std::min(kForbiddenPrice, pr.h[i] * std::exp(epsilon * priceExponentTerm(u.h[i], g.hcap[i], threshold)));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] = std::min(kForbiddenPrice, pr.v[i] * std::exp(epsilon * priceExponentTerm(u.v[i], g.vcap[i], threshold)));
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
                                int margin, double lengthWeight = 1.0) {
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
        double w = edgeCost(prices.h[e], prices.histH[e], amount, lengthWeight);
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
        double w = edgeCost(prices.v[e], prices.histV[e], amount, lengthWeight);
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
      // Via base cost only -- no via price yet, matching mroute step 1.
      // Scaled by lengthWeight too, ported from yroute.cpp: a via crossing
      // scores as wirelength (1 tile per layer crossed) just as much as a
      // wire segment does, so it needs to become "free" as lengthWeight
      // falls too, or a net could still be deterred from a layer change
      // that would otherwise let it dodge congestion.
      double w = lengthWeight;
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
                             SearchRegion &region, Point p1, int p1Layer, Point p2, int width,
                             double lengthWeight = 1.0) {
  static const int maxMargin = [] {
    const char *m = std::getenv("QROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int span = std::abs(p1.x - p2.x) + std::abs(p1.y - p2.y);
  int margin = std::min(maxMargin, std::max(4, span / 8));
  for (int attempt = 0; attempt < 6; ++attempt) {
    DijkstraResult r = boundedDijkstra(g, prices, lp, region, p1, p1Layer, p2, width, margin, lengthWeight);
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
// `lengthWeight` (default 1.0, a no-op) is this file's own candidate-
// generation hook -- see the file header and generateCandidatesForNet
// below -- ported verbatim from yroute.cpp.
void routeNetSlow(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                   SearchRegion &region, double epsilon, RoutedNet &rn, double lengthWeight = 1.0) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.segs.clear();
  rn.charges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    int p1Layer = nodeLayer[e.first];
    DijkstraResult r =
        routeSegment(g, prices, lp, region, p1, p1Layer, p2, rn.net->minWidth, lengthWeight);
    if (!r.found)
      fail("qroute: no path found between tree points even after margin expansion -- "
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

// ---------------------------------------------------------------------------
// MSMV (multi-source multi-sink) maze growth, ported verbatim from
// zroute.cpp (itself from vroute.cpp): a second, structurally different
// candidate generator this file's own header explains the motivation for.
// `routeNetSlow` above always attaches at one of the two *pin* endpoints
// of a fixed, precomputed MST edge; MSMV instead treats every tile the
// net has *already committed* as a simultaneous source and every
// unconnected pin as a simultaneous sink, so a later pin can attach
// anywhere along already-built wire, not just at another pin -- finding
// genuinely different (and sometimes cheaper) Steiner topologies a
// fixed-MST decomposition cannot represent at all. See
// testMsmvCandidateFindsCheaperTopologyThanFixedMst below for a
// hand-verified, decisive example.
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
                                const std::vector<Point> &sinks, int width, int margin,
                                double lengthWeight = 1.0) {
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
  region.reset(x0, y0, x1 - x0 + 1, y1 - y0 + 1, g.nLayers);

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
    if (s.l >= 0) {
      int i = region.idx(s.x, s.y, s.l);
      region.setDist(i, 0.0, -1);
      pq.push({heuristic(s.x, s.y), 0.0, i});
    } else {
      for (int l = 0; l < g.nLayers; ++l) {
        int i = region.idx(s.x, s.y, l);
        region.setDist(i, 0.0, -1);
        pq.push({heuristic(s.x, s.y), 0.0, i});
      }
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
        double w = edgeCost(prices.h[e], prices.histH[e], amount, lengthWeight);
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
        double w = edgeCost(prices.v[e], prices.histV[e], amount, lengthWeight);
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
      double w = lengthWeight; // via base cost, scaled the same as yroute's own via handling
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

MSMVResult routeStepMSMV(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                          SearchRegion &region, const std::vector<PathNode> &sources,
                          const std::vector<Point> &sinks, int width, double lengthWeight = 1.0) {
  static const int maxMargin = [] {
    const char *m = std::getenv("QROUTE_MAX_MARGIN");
    return m ? std::atoi(m) : 6;
  }();
  int margin = maxMargin;
  for (int attempt = 0; attempt < 6; ++attempt) {
    MSMVResult r = multiSourceDijkstra(g, prices, lp, region, sources, sinks, width, margin, lengthWeight);
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
// process, not read from a precomputed MST (rn.mst is still populated by
// route()'s own setup, since this file's fast tier and phase A/B still
// use it; MSMV simply never reads it). Pin order (which pin starts
// "connected") doesn't matter for correctness, only for which of several
// equal-cost trees comes out; pts[0] is as good a start as any.
void routeNetSlowMSMV(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                       SearchRegion &region, double epsilon, RoutedNet &rn, double lengthWeight = 1.0) {
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

    MSMVResult r = routeStepMSMV(g, prices, lp, region, sourceSet, sinks, rn.net->minWidth, lengthWeight);
    if (!r.found)
      fail("qroute: MSMV found no path to any remaining pin even after margin "
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
// Progress logging: every prior router in this lineage traces overflow
// alone, round by round, to stderr under a TRACE flag -- useful to watch
// live, useless to plot afterward. QROUTE_PROGRESS_LOG adds wirelength
// alongside overflow, at every round/pass/cluster boundary this file
// already has a natural checkpoint at, into one CSV meant to be charted:
// does overflow trade off against wirelength the way the cost model
// assumes, does the new joint mechanism's phase move differently from
// phase A/B's own round-by-round curve, etc. -- questions a single final
// number can't answer.
// ---------------------------------------------------------------------------

struct WirelengthStats {
  long long wirelength = 0; // unit grid edges, unweighted -- matches
                             // gr_verify.cpp's own convention exactly
  long long vias = 0;       // layers crossed, summed across every via
                             // segment -- also gr_verify's convention
};

// Computed directly from in-memory RoutedNet state (ChargeRecord spans for
// wirelength, Seg entries with l1 != l2 for vias) rather than by writing
// and reparsing a solution file, so it can be sampled cheaply at every
// checkpoint without disk I/O. Matches gr_verify.cpp's own wirelength/
// viaCount definitions (see its own comments around line 280-305) exactly,
// so a plotted trajectory's final point is directly comparable to the run's
// eventual gr_verify/eval2008.pl numbers.
WirelengthStats wirelengthStats(const std::vector<RoutedNet> &nets) {
  WirelengthStats s;
  for (const RoutedNet &rn : nets) {
    for (const ChargeRecord &c : rn.charges)
      s.wirelength += (c.b - c.a);
    for (const Seg &seg : rn.segs)
      if (seg.l1 != seg.l2)
        s.vias += std::abs(seg.l2 - seg.l1);
  }
  return s;
}

// Lazily opens QROUTE_PROGRESS_LOG on first use (module-level singleton,
// the same pattern this codebase already uses for env-var-driven flags
// like `trace`, just holding an open file handle instead of a bool) so
// every call site -- phase A's round loop, the cleanup pass, phase B's
// round loop, and this file's own new Phase D -- can log a row without
// threading a new parameter through every function signature. Inactive
// (every `record` call a no-op) when the env var is unset, so this adds
// no overhead to a normal run.
struct ProgressLog {
  std::ofstream out;
  std::chrono::steady_clock::time_point t0;
  bool active = false;

  void open(const char *path) {
    out.open(path);
    if (!out)
      fail("qroute: could not open QROUTE_PROGRESS_LOG path for writing");
    out << "phase,step,overflow,wirelength,vias,scored_wirelength,elapsed_s\n";
    t0 = std::chrono::steady_clock::now();
    active = true;
  }

  void record(const char *phase, long long step, long long overflow,
              const std::vector<RoutedNet> &nets) {
    if (!active)
      return;
    WirelengthStats s = wirelengthStats(nets);
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out << phase << "," << step << "," << overflow << "," << s.wirelength << "," << s.vias << ","
        << (s.wirelength + s.vias) << "," << elapsed << "\n";
    out.flush(); // a run can be killed mid-way (round cap, node cap, or a
                 // user Ctrl-C on a long real-benchmark screen); flushing
                 // per row keeps the log usable even then, matching this
                 // project's own "write each step's state to a file, not
                 // just conversation context" standing practice.
  }
};

ProgressLog &progressLog() {
  static ProgressLog log;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    if (const char *path = std::getenv("QROUTE_PROGRESS_LOG"); path)
      log.open(path);
  }
  return log;
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
  // Phase D (joint candidate-assignment) stats -- see negotiateJointAssignment
  // below.
  int clustersAttempted = 0;
  int clustersAccepted = 0;
  int clusterNetsTouched = 0;
  long long greedyIncumbentTotal = 0; // sum, across clusters, of what plain
                                       // sequential search alone would have
                                       // achieved -- the direct comparison
                                       // point this mechanism exists to beat
  long long bnbBestTotal = 0;         // sum, across clusters, of what this
                                       // mechanism actually left in place --
                                       // the accepted result if the search
                                       // beat `before`, `before` unchanged
                                       // otherwise
  int clustersHitNodeCap = 0;
  long long totalBnbNodes = 0;
  // Cooperative eviction stats -- this file's own addition, see
  // recruitEvictionHelpers.
  int helpersRecruited = 0;
  int clustersWithHelpers = 0;
  int clustersAcceptedWithHelpers = 0; // of clustersAccepted, how many had
                                        // >=1 helper actually recruited --
                                        // the direct answer to "did
                                        // eviction itself cause any of the
                                        // wins," not just correlate with them
  // Context-widening stats -- this file's own addition, see
  // recruitContextNets.
  int contextNetsUsed = 0;
  int clustersWithContext = 0;
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
    const char *t = std::getenv("QROUTE_CLEANUP_THRESHOLD");
    return t ? std::atof(t) : 0.5;
  }();
  int passes = [] {
    const char *p = std::getenv("QROUTE_CLEANUP_PASSES");
    return p ? std::atoi(p) : 30;
  }();
  bool trace = std::getenv("QROUTE_TRACE") != nullptr;
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
  progressLog().record("cleanup", 0, rep.bestOverflow, rep.nets);
  if (trace)
    std::cerr << "cleanup pass: overflow " << before << " -> " << rep.bestOverflow << "\n";
}

constexpr int kFastPatience = 20;
constexpr int kSlowPatience = 5;

// ---------------------------------------------------------------------------
// Phase D: joint/batch rescue -- see the file header for the full
// motivation. Two building blocks (cluster discovery, net membership),
// then the negotiation itself.
// ---------------------------------------------------------------------------

// Groups every currently-overflowed 2D tile location into connected
// clusters -- two hot tiles join the same cluster if they're 4-
// directionally adjacent -- across all layers and both directions at
// once. Matches HISTORY_COST_SWEEP.md's own per-tile finding directly: a
// hot (x,y) location typically has every layer overloaded together, so
// clustering in the flat (x,y) projection recovers the same contiguous
// hot regions that analysis found by hand, without needing to know their
// shape in advance.
std::vector<std::vector<size_t>> findOverflowClusters(const RoutingGrid &g, const Usage &usage) {
  size_t numTiles = static_cast<size_t>(g.nx) * g.ny;
  std::vector<bool> hot(numTiles, false);
  auto tidx = [&](int x, int y) { return static_cast<size_t>(y) * g.nx + x; };
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x < g.nx - 1; ++x) {
        size_t e = g.hIndex(x, y, l);
        if (usage.h[e] > g.hcap[e]) {
          hot[tidx(x, y)] = true;
          hot[tidx(x + 1, y)] = true;
        }
      }
    for (int y = 0; y < g.ny - 1; ++y)
      for (int x = 0; x < g.nx; ++x) {
        size_t e = g.vIndex(x, y, l);
        if (usage.v[e] > g.vcap[e]) {
          hot[tidx(x, y)] = true;
          hot[tidx(x, y + 1)] = true;
        }
      }
  }

  std::vector<size_t> parent(numTiles);
  for (size_t i = 0; i < numTiles; ++i)
    parent[i] = i;
  std::function<size_t(size_t)> find = [&](size_t a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  auto unite = [&](size_t a, size_t b) {
    a = find(a);
    b = find(b);
    if (a != b)
      parent[a] = b;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      if (!hot[tidx(x, y)])
        continue;
      if (x + 1 < g.nx && hot[tidx(x + 1, y)])
        unite(tidx(x, y), tidx(x + 1, y));
      if (y + 1 < g.ny && hot[tidx(x, y + 1)])
        unite(tidx(x, y), tidx(x, y + 1));
    }

  std::unordered_map<size_t, std::vector<size_t>> byRoot;
  for (size_t i = 0; i < numTiles; ++i)
    if (hot[i])
      byRoot[find(i)].push_back(i);

  std::vector<std::vector<size_t>> clusters;
  clusters.reserve(byRoot.size());
  for (auto &entry : byRoot)
    clusters.push_back(std::move(entry.second));
  return clusters;
}

// Whether any of this net's charges touch a tile in `inCluster` -- the
// geometric test clusterNets and recruitEvictionHelpers both need, the
// second one deliberately without clusterNets' own netOverflowed() filter
// (see recruitEvictionHelpers' own comment for why).
bool netTouchesCluster(const RoutingGrid &g, const RoutedNet &rn, const std::vector<bool> &inCluster) {
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal) {
      for (int x = c.a; x <= c.b; ++x)
        if (inCluster[static_cast<size_t>(c.cross) * g.nx + x])
          return true;
    } else {
      for (int y = c.a; y <= c.b; ++y)
        if (inCluster[static_cast<size_t>(y) * g.nx + c.cross])
          return true;
    }
  }
  return false;
}

// Every non-giant, currently-overflowed net whose charges touch any tile
// in this cluster -- the set of nets actually contending for this patch,
// not innocent bystanders. A net that merely passes through a hot region
// without itself overflowing is left alone by this function; see
// recruitEvictionHelpers below for this file's own reason to reconsider
// that exclusion for a small, bounded, separately-tracked set.
std::vector<size_t> clusterNets(const RoutingGrid &g, const Usage &usage,
                                 const std::vector<RoutedNet> &nets,
                                 const std::vector<bool> &inCluster) {
  std::vector<size_t> result;
  for (size_t i = 0; i < nets.size(); ++i) {
    const RoutedNet &rn = nets[i];
    if (rn.giant || !netOverflowed(g, usage, rn))
      continue;
    if (netTouchesCluster(g, rn, inCluster))
      result.push_back(i);
  }
  return result;
}

// Cooperative eviction -- this file's own addition, see the header for
// the full motivation. broute's, froute's, aroute's, and nroute's own
// mechanisms (HISTORY_COST_SWEEP.md Parts 12-15) all restrict cluster
// membership to clusterNets' own rule: only nets already overflowed move
// at all. Five distinct coordination/search mechanisms confined to that
// rule land on the identical safe no-op. This function recruits a small,
// bounded number of currently-*legal* nets that also touch the cluster's
// tiles into the same joint search, on the reasoning that if the
// overflowed nets' own rearrangements among themselves cannot relieve a
// jointly-oversubscribed resource, the only way out may be asking a
// neighbor who isn't part of the problem to make room.
//
// Ranked by how much usage each candidate helper currently contributes on
// edges actually inside the cluster (not its whole route, which may
// extend well beyond it) -- a net occupying more of the contested tiles'
// own capacity has more to offer if it moves, not that it's somehow at
// fault; its own original route stays a standing candidate throughout
// (generateCandidatesForNet always includes it), so recruiting a helper
// can never force it to move, only offer the search the option. Capped
// at QROUTE_MAX_EVICTION_HELPERS (default 3), independently of
// QROUTE_MAX_CLUSTER_NETS, since recruiting many legal nets at once
// reproduces exactly the uncapped-cluster thundering-herd failure this
// sweep already confirmed (Part 12) -- a small number of well-targeted
// recruits is the point, not a wide net.
// Every currently-legal, non-giant, cluster-touching net NOT already in
// `alreadyIncluded`, ranked by how much usage it contributes on edges
// actually inside the cluster (descending) -- the shared ranking both
// recruitEvictionHelpers and recruitContextNets below cap differently for
// two different purposes. Credits an edge's amount if *either* of its two
// endpoint tiles is in the cluster -- `x` here is an edge index (matching
// hIndex/vIndex and every usage-accounting call site in this codebase),
// so the edge between tile x and tile x+1 must check both, not just x.
// This is deliberately not the same convention netTouchesCluster uses
// (which treats c.a/c.b as an inclusive *tile* range, correct for "does
// this whole charge touch the cluster anywhere") -- ranking by occupied
// capacity needs the per-edge answer instead, or a charge whose only
// in-cluster endpoint is its upper bound would silently rank as
// contributing nothing.
std::vector<std::pair<long long, size_t>> rankLegalTouchingNets(const RoutingGrid &g,
                                                                  const Usage &usage,
                                                                  const std::vector<RoutedNet> &nets,
                                                                  const std::vector<bool> &inCluster,
                                                                  const std::vector<size_t> &alreadyIncluded) {
  std::vector<bool> excluded(nets.size(), false);
  for (size_t idx : alreadyIncluded)
    excluded[idx] = true;

  std::vector<std::pair<long long, size_t>> ranked;
  for (size_t i = 0; i < nets.size(); ++i) {
    const RoutedNet &rn = nets[i];
    if (rn.giant || excluded[i] || netOverflowed(g, usage, rn))
      continue; // only currently-legal nets are eligible
    if (!netTouchesCluster(g, rn, inCluster))
      continue;
    long long occupied = 0;
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal) {
        for (int x = c.a; x < c.b; ++x)
          if (inCluster[static_cast<size_t>(c.cross) * g.nx + x] ||
              inCluster[static_cast<size_t>(c.cross) * g.nx + x + 1])
            occupied += c.amount;
      } else {
        for (int y = c.a; y < c.b; ++y)
          if (inCluster[static_cast<size_t>(y) * g.nx + c.cross] ||
              inCluster[static_cast<size_t>(y + 1) * g.nx + c.cross])
            occupied += c.amount;
      }
    }
    if (occupied > 0)
      ranked.emplace_back(occupied, i);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });
  return ranked;
}

std::vector<size_t> recruitEvictionHelpers(const RoutingGrid &g, const Usage &usage,
                                            const std::vector<RoutedNet> &nets,
                                            const std::vector<bool> &inCluster,
                                            const std::vector<size_t> &alreadyIncluded) {
  // Deliberately not `static` -- see rankAndCapClusterNets' identical
  // comment on this exact pitfall.
  const size_t maxHelpers = [] {
    const char *m = std::getenv("QROUTE_MAX_EVICTION_HELPERS");
    return m ? static_cast<size_t>(std::atoi(m)) : static_cast<size_t>(0);
  }();
  if (maxHelpers == 0)
    return {};
  std::vector<std::pair<long long, size_t>> ranked =
      rankLegalTouchingNets(g, usage, nets, inCluster, alreadyIncluded);
  size_t keep = std::min(ranked.size(), maxHelpers);
  std::vector<size_t> helpers;
  helpers.reserve(keep);
  for (size_t r = 0; r < keep; ++r)
    helpers.push_back(ranked[r].second);
  return helpers;
}

// This file's own addition -- see the file header for the full story.
// Recruits a much larger set of currently-legal, cluster-touching nets
// than recruitEvictionHelpers ever would (default 20 vs. oroute's 3),
// deliberately *not* to give the exact search any power to move them
// (they never get their own candidate slots, never appear in netIdx, and
// are always restored to their exact original route before the search
// runs -- see negotiateJointAssignment's own comment) -- purely to widen
// the usage baseline, and the accumulated price-side-effect trail
// negotiateWire leaves behind, that the cluster's own overflowed nets'
// candidate generation reacts to. Affordable at a much larger cap than
// eviction helpers precisely because these nets are never added to the
// branch-and-bound's own search space -- widening this set costs linear
// extra candidate-generation work, never combinatorial search blowup.
std::vector<size_t> recruitContextNets(const RoutingGrid &g, const Usage &usage,
                                        const std::vector<RoutedNet> &nets,
                                        const std::vector<bool> &inCluster,
                                        const std::vector<size_t> &alreadyIncluded) {
  // Deliberately not `static` -- see rankAndCapClusterNets' identical
  // comment on this exact pitfall.
  const size_t maxContext = [] {
    const char *m = std::getenv("QROUTE_MAX_CONTEXT_NETS");
    return m ? static_cast<size_t>(std::atoi(m)) : static_cast<size_t>(20);
  }();
  if (maxContext == 0)
    return {};
  std::vector<std::pair<long long, size_t>> ranked =
      rankLegalTouchingNets(g, usage, nets, inCluster, alreadyIncluded);
  size_t keep = std::min(ranked.size(), maxContext);
  std::vector<size_t> context;
  context.reserve(keep);
  for (size_t r = 0; r < keep; ++r)
    context.push_back(ranked[r].second);
  return context;
}

// Ranks a cluster's contending nets by individual overflow contribution
// and caps to QROUTE_MAX_CLUSTER_NETS (default 12) worst offenders when
// oversized -- lifted from froute.cpp's own standalone
// rankAndCapClusterNets, which itself extracted broute's validated inline
// cap logic. Reused here for exactly the same reason froute reused it:
// membership/ranking, no accept/reject gate attached.
std::vector<size_t> rankAndCapClusterNets(const RoutingGrid &g, const Usage &usage,
                                           const std::vector<RoutedNet> &nets,
                                           std::vector<size_t> netIdx) {
  // Deliberately not `static` -- see broute.cpp's identical comment on this
  // exact pitfall: a cached value would lock in whatever
  // QROUTE_MAX_CLUSTER_NETS happened to be on this function's first call
  // for the rest of the process, breaking any self-test that exercises
  // different caps via setenv within one test binary.
  const size_t maxClusterNets = [] {
    const char *m = std::getenv("QROUTE_MAX_CLUSTER_NETS");
    return m ? static_cast<size_t>(std::atoi(m)) : static_cast<size_t>(12);
  }();
  std::vector<std::pair<long long, size_t>> ranked;
  ranked.reserve(netIdx.size());
  for (size_t idx : netIdx) {
    const RoutedNet &rn = nets[idx];
    ranked.emplace_back(overflowOverUnion(g, usage, rn.charges, rn.charges), idx);
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });
  size_t keep = std::min(ranked.size(), maxClusterNets);
  std::vector<size_t> capped;
  capped.reserve(keep);
  for (size_t r = 0; r < keep; ++r)
    capped.push_back(ranked[r].second);
  return capped;
}

// ---------------------------------------------------------------------------
// Phase D: joint candidate-assignment -- see the file header for the full
// motivation. Where broute's own Phase D (and this file's direct
// ancestor) resolved a cluster by sequential greedy search -- one net at
// a time, each reacting only to whatever the previous net just committed
// -- this generates several candidate routes per net against a single
// frozen baseline, then picks one candidate per net *jointly*, via an
// exact branch-and-bound search over the small capped candidate space,
// to minimize the cluster's total overflow. This is the first mechanism
// in the whole sweep (Parts 12-13) to change the solve method itself
// rather than the scope or order of the same sequential greedy solve.
// ---------------------------------------------------------------------------

std::vector<double> candidateWeights() {
  static const std::vector<double> weights = [] {
    const char *w = std::getenv("QROUTE_CANDIDATE_WEIGHTS");
    std::string spec = w ? w : "1.0,0.1,0.01,0";
    std::vector<double> out;
    std::istringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ','))
      if (!tok.empty())
        out.push_back(std::atof(tok.c_str()));
    return out;
  }();
  return weights;
}

// A second, independently-tunable weight schedule for the MSMV candidate
// generator (see the file header) -- separate from candidateWeights()
// since the two search methods find candidates through completely
// different mechanisms (fixed-MST attachment-at-pins vs. attach-anywhere-
// on-committed-wire) and there is no reason to assume the same weight
// values matter equally to both.
std::vector<double> msmvCandidateWeights() {
  static const std::vector<double> weights = [] {
    const char *w = std::getenv("QROUTE_MSMV_CANDIDATE_WEIGHTS");
    std::string spec = w ? w : "1.0,0.1,0.01,0";
    std::vector<double> out;
    std::istringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ','))
      if (!tok.empty())
        out.push_back(std::atof(tok.c_str()));
    return out;
  }();
  return weights;
}

struct RouteCandidate {
  std::vector<ChargeRecord> charges;
  std::vector<Seg> segs;
};

// Every individual grid edge (h or v, at any layer) a charge list touches,
// as a sorted set -- used to dedupe candidates that converge to the same
// path at different weights, robust to how commitPath happened to group
// consecutive moves into ChargeRecords (the same technique froute.cpp's
// own decisive order-dependence test used for the identical reason).
std::vector<std::pair<bool, size_t>> chargeEdgeSet(const RoutingGrid &g,
                                                    const std::vector<ChargeRecord> &charges) {
  std::vector<std::pair<bool, size_t>> edges;
  for (const ChargeRecord &c : charges) {
    if (c.horizontal)
      for (int x = c.a; x < c.b; ++x)
        edges.push_back({true, g.hIndex(x, c.cross, c.layer)});
    else
      for (int y = c.a; y < c.b; ++y)
        edges.push_back({false, g.vIndex(c.cross, y, c.layer)});
  }
  std::sort(edges.begin(), edges.end());
  return edges;
}

// Generates one net's weight-based candidate routes. On entry, `rn` must
// already be empty (charges/segs cleared) and every OTHER cluster net's
// charges must already be absent from `usage` -- the caller (
// negotiateJointAssignment) is responsible for establishing and
// maintaining that frozen "cluster removed" baseline across every net's
// candidate generation, so every net's candidates are computed against
// the identical starting point, never against a sibling's tentative
// choice. On return, `rn` is empty again and `usage` reflects that
// emptiness, so the next net sees the same baseline this one started
// with.
//
// Each routeNetSlow call's own first line undoes whatever `rn.charges`
// *currently* holds (the previous weight's attempt, or nothing on the
// first iteration) before committing the next one -- exactly the chaining
// every sequential mechanism in this codebase already relies on. This
// function never manually touches `usage` mid-loop; only the final
// "back to empty" step at the end is a manual, explicit undo, exactly
// like every other mechanism's own give-up path (see yroute.cpp's and
// broute.cpp's own documented lesson on this exact double-undo pitfall).
std::vector<RouteCandidate> generateCandidatesForNet(const RoutingGrid &g, Prices &prices, Usage &usage,
                                                 const LayerPlan &lp, SearchRegion &region,
                                                 double epsilon, RoutedNet &rn) {
  std::vector<RouteCandidate> candidates;
  std::vector<std::vector<std::pair<bool, size_t>>> seenEdgeSets;

  auto tryCandidate = [&]() {
    std::vector<std::pair<bool, size_t>> edges = chargeEdgeSet(g, rn.charges);
    bool dup = false;
    for (const auto &s : seenEdgeSets)
      if (s == edges) {
        dup = true;
        break;
      }
    if (!dup) {
      candidates.push_back({rn.charges, rn.segs});
      seenEdgeSets.push_back(std::move(edges));
    }
  };

  for (double w : candidateWeights()) {
    routeNetSlow(g, prices, usage, lp, region, epsilon, rn, w);
    tryCandidate();
  }
  // MSMV candidates: this file's own addition on top of aroute.cpp's
  // original candidate generator (see the file header). A 2-pin net has
  // only one tree edge, so MSMV and the fixed-MST routeNetSlow above are
  // mechanically identical for it -- these calls still run (harmlessly
  // producing duplicates the dedup above filters out) rather than special-
  // casing net size, since the real payoff is on 3+ pin nets where MSMV's
  // attach-anywhere-on-committed-wire growth can find a genuinely
  // different (and sometimes cheaper) Steiner topology a fixed MST
  // decomposition structurally cannot represent at all -- see
  // testMsmvCandidateFindsCheaperTopologyThanFixedMst for a hand-verified
  // example.
  if (rn.pts.size() > 2) {
    for (double w : msmvCandidateWeights()) {
      routeNetSlowMSMV(g, prices, usage, lp, region, epsilon, rn, w);
      tryCandidate();
    }
  }

  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.charges.clear();
  rn.segs.clear();
  return candidates;
}

// The exact joint search. `candidates[i]` is net i's candidate list (index
// order matches `netIdx`, ranked worst-offender-first so the bound
// tightens fastest); `usage` must reflect the frozen "cluster removed"
// baseline throughout -- never mutated during the search, only read.
// `seed` is the value a complete assignment must beat to be recorded at
// all (this file passes the cluster's own pre-touch overflow, so a
// caller that finds nothing better than `seed` knows to leave the
// cluster untouched, exactly like every other mechanism's own
// accept/reject convention).
struct AssignmentResult {
  bool found = false;
  long long overflow = 0;
  std::vector<size_t> choice;
  long long nodesExpanded = 0;
  bool hitNodeCap = false;
};

AssignmentResult branchAndBoundAssign(const RoutingGrid &g, const Usage &usage,
                                       const std::vector<std::vector<RouteCandidate>> &candidates,
                                       long long seed, long long maxNodes) {
  size_t n = candidates.size();
  AssignmentResult best;
  best.overflow = seed; // `found` stays false until something strictly beats this
  std::vector<size_t> choice(n, 0);

  // Background overflow: every edge some candidate (across every net,
  // including each net's own "keep original" option) could ever touch,
  // at its *unperturbed* usage -- i.e. whatever overflow already exists
  // there from nets outside this cluster entirely, or from a cluster
  // member's original route sitting at an edge no candidate in the
  // winning combination ends up choosing. This has to seed the running
  // total, not just the incremental delta a chosen candidate adds on
  // top of it: an edge one net's original route touches but that the
  // winning combination abandons (that net moved to a different
  // candidate) never gets an `applyDelta` call at all in that
  // combination, so any background overflow already sitting there --
  // e.g. from other, uncapped-out contending nets clusterNets/
  // rankAndCapClusterNets excluded, or from nets that merely pass
  // through this cluster's tiles without being overflowed themselves --
  // would otherwise silently vanish from the search's own bookkeeping
  // even though it never actually goes away. First cut of this file
  // omitted this term entirely and it was caught immediately, on real
  // data, by this very function's own final independent-recomputation
  // gate (see negotiateJointAssignment's DEBUG check): the search
  // believed it had found large improvements that, once actually
  // applied and reverified via overflowOverUnion, turned out to be
  // ties with doing nothing at all.
  long long baselineOverflow = 0;
  {
    std::vector<std::pair<bool, size_t>> allEdges;
    for (const std::vector<RouteCandidate> &netCands : candidates)
      for (const RouteCandidate &c : netCands)
        for (const auto &e : chargeEdgeSet(g, c.charges))
          allEdges.push_back(e);
    std::sort(allEdges.begin(), allEdges.end());
    allEdges.erase(std::unique(allEdges.begin(), allEdges.end()), allEdges.end());
    for (const auto &e : allEdges)
      baselineOverflow += e.first ? std::max(0, usage.h[e.second] - g.hcap[e.second])
                                   : std::max(0, usage.v[e.second] - g.vcap[e.second]);
  }

  // Incremental delta-usage tracking: only edges some candidate actually
  // touches ever appear as keys here, so this never scans the whole grid
  // -- same discipline overflowOverUnion and every other mechanism in
  // this codebase already follows. `runningOverflow` starts at
  // `baselineOverflow`, not 0 -- see above.
  std::unordered_map<size_t, int> deltaH, deltaV;
  long long runningOverflow = baselineOverflow;
  long long nodes = 0;
  bool hitCap = false;

  // Adds (sign=+1) or removes (sign=-1) one candidate's charges from the
  // delta maps, returning the net change in total overflow this caused.
  auto applyDelta = [&](const RouteCandidate &cand, int sign) -> long long {
    long long deltaOverflow = 0;
    for (const ChargeRecord &c : cand.charges) {
      if (c.horizontal) {
        for (int x = c.a; x < c.b; ++x) {
          size_t e = g.hIndex(x, c.cross, c.layer);
          int &dh = deltaH[e];
          int before = std::max(0, usage.h[e] + dh - g.hcap[e]);
          dh += sign * c.amount;
          int after = std::max(0, usage.h[e] + dh - g.hcap[e]);
          deltaOverflow += (after - before);
        }
      } else {
        for (int y = c.a; y < c.b; ++y) {
          size_t e = g.vIndex(c.cross, y, c.layer);
          int &dv = deltaV[e];
          int before = std::max(0, usage.v[e] + dv - g.vcap[e]);
          dv += sign * c.amount;
          int after = std::max(0, usage.v[e] + dv - g.vcap[e]);
          deltaOverflow += (after - before);
        }
      }
    }
    return deltaOverflow;
  };

  // Overflow is monotonically non-decreasing as more charges are added
  // (never negative), so `runningOverflow` at any partial assignment is a
  // valid lower bound on every completion of it -- prune the instant it
  // is no better than the best complete assignment found so far.
  std::function<void(size_t)> recurse = [&](size_t i) {
    if (hitCap)
      return;
    if (++nodes > maxNodes) {
      hitCap = true;
      return;
    }
    if (runningOverflow >= best.overflow)
      return;
    if (i == n) {
      best.found = true;
      best.overflow = runningOverflow;
      best.choice = choice;
      return;
    }
    for (size_t c = 0; c < candidates[i].size() && !hitCap; ++c) {
      choice[i] = c;
      long long delta = applyDelta(candidates[i][c], +1);
      runningOverflow += delta;
      recurse(i + 1);
      runningOverflow -= delta;
      applyDelta(candidates[i][c], -1);
    }
  };
  recurse(0);

  best.nodesExpanded = nodes;
  best.hitNodeCap = hitCap;
  return best;
}

void negotiateJointAssignment(const RoutingGrid &g, Prices &prices, Usage &usage,
                               const LayerPlan &lp, SearchRegion &region, double epsilon,
                               RunReport &rep) {
  bool trace = std::getenv("QROUTE_TRACE") != nullptr;
  std::ofstream log;
  bool logging = false;
  if (const char *path = std::getenv("QROUTE_JOINT_LOG"); path) {
    log.open(path);
    if (!log)
      fail("qroute: could not open QROUTE_JOINT_LOG path for writing");
    log << "cluster,nets,helpers_recruited,context_nets,candidates_total,bnb_nodes,"
           "overflow_before,greedy_incumbent_overflow,bnb_best_overflow,hit_node_cap,accepted,"
           "helper_moved\n";
    logging = true;
  }
  const long long maxBnbNodes = [] {
    const char *m = std::getenv("QROUTE_MAX_BNB_NODES");
    return m ? std::atoll(m) : 200000LL;
  }();

  std::vector<std::vector<size_t>> clusters = findOverflowClusters(g, usage);
  std::sort(clusters.begin(), clusters.end(),
            [](const std::vector<size_t> &a, const std::vector<size_t> &b) { return a.size() > b.size(); });

  int clusterIdx = 0;
  for (const std::vector<size_t> &tiles : clusters) {
    ++clusterIdx;
    std::vector<bool> inCluster(static_cast<size_t>(g.nx) * g.ny, false);
    for (size_t t : tiles)
      inCluster[t] = true;
    std::vector<size_t> netIdx = clusterNets(g, usage, rep.nets, inCluster);
    if (netIdx.empty())
      continue;
    netIdx = rankAndCapClusterNets(g, usage, rep.nets, std::move(netIdx));

    // Cooperative eviction: OROUTE_MAX_EVICTION_HELPERS-equivalent
    // (QROUTE_MAX_EVICTION_HELPERS) is inherited but defaults to 0 in
    // this file -- disabled -- to isolate this file's own new variable
    // (context widening, below) from oroute.cpp's own already-measured
    // one. Set it nonzero to combine both mechanisms.
    std::vector<size_t> helperIdx = recruitEvictionHelpers(g, usage, rep.nets, inCluster, netIdx);
    for (size_t h : helperIdx)
      netIdx.push_back(h);
    if (!helperIdx.empty())
      ++rep.clustersWithHelpers;
    rep.helpersRecruited += static_cast<int>(helperIdx.size());

    ++rep.clustersAttempted;

    // Save every member net's true original state, compute the true
    // "before" baseline (usage still reflects the original committed
    // design here), then rip up exactly this cluster's members -- never
    // anything wider, per froute.cpp's own scope-restriction lesson.
    std::vector<ChargeRecord> oldCharges_flat;
    std::vector<std::vector<ChargeRecord>> oldCharges(netIdx.size());
    std::vector<std::vector<Seg>> oldSegs(netIdx.size());
    for (size_t k = 0; k < netIdx.size(); ++k) {
      RoutedNet &rn = rep.nets[netIdx[k]];
      oldCharges[k] = rn.charges;
      oldSegs[k] = rn.segs;
      for (const ChargeRecord &c : rn.charges)
        oldCharges_flat.push_back(c);
    }
    long long before = overflowOverUnion(g, usage, oldCharges_flat, oldCharges_flat);

    for (size_t k = 0; k < netIdx.size(); ++k) {
      RoutedNet &rn = rep.nets[netIdx[k]];
      for (const ChargeRecord &c : rn.charges)
        undoCharge(g, usage, c);
      rn.charges.clear();
      rn.segs.clear();
    }

    // Context widening -- this file's own addition, see the file header
    // for the full account of why this works (oroute.cpp's Part 16
    // finding: candidate generation's own trial commits leave a
    // *permanent* price residue via negotiateWire, since undoCharge only
    // ever reverts usage, never prices -- so a wider absent-usage
    // baseline changes what those trial commits' own price bumps look
    // like for the whole cluster's candidate generation, independent of
    // whether the widening nets ever move). Ranked and capped exactly
    // like eviction helpers (`recruitContextNets`, sharing
    // rankLegalTouchingNets), but at a much larger default cap since
    // these nets never join `netIdx` or the branch-and-bound's own search
    // space at all -- they are ripped up here, alongside the cluster's
    // real members, purely so candidate generation sees them absent, then
    // restored to their *exact* original charges immediately below,
    // before candidate generation for even the first cluster member
    // begins, so their rip-up window covers all of it uniformly.
    std::vector<size_t> contextIdx = recruitContextNets(g, usage, rep.nets, inCluster, netIdx);
    for (size_t idx : contextIdx)
      for (const ChargeRecord &c : rep.nets[idx].charges)
        undoCharge(g, usage, c);
    rep.contextNetsUsed += static_cast<int>(contextIdx.size());
    if (!contextIdx.empty())
      ++rep.clustersWithContext;

    // RouteCandidate generation: every net's candidates are computed against
    // the identical "cluster removed" baseline (usage never mutated
    // between nets by anything other than each net's own temporary,
    // fully-undone trial attempts). RouteCandidate 0 for each net is always
    // its own original route -- added directly as data, no usage
    // interaction needed, guaranteeing the search can always choose "no
    // change" for any net.
    std::vector<std::vector<RouteCandidate>> candidates(netIdx.size());
    int candidateTotal = 0;
    for (size_t k = 0; k < netIdx.size(); ++k) {
      candidates[k].push_back({oldCharges[k], oldSegs[k]});
      for (RouteCandidate &c : generateCandidatesForNet(g, prices, usage, lp, region, epsilon, rep.nets[netIdx[k]]))
        candidates[k].push_back(std::move(c));
      candidateTotal += static_cast<int>(candidates[k].size());
    }

    // Restore every context net to its exact original charges -- their
    // rip-up window covered candidate generation for the whole cluster,
    // and nothing else; they are never part of the search itself, so
    // `usage` must reflect their true, unchanged presence for every
    // computation from here on (the greedy incumbent, the search's own
    // baseline-overflow term, and the final accept/reject gate all read
    // live `usage` directly).
    for (size_t idx : contextIdx)
      for (const ChargeRecord &c : rep.nets[idx].charges) {
        if (c.horizontal)
          for (int x = c.a; x < c.b; ++x)
            usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
        else
          for (int y = c.a; y < c.b; ++y)
            usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
      }

    // Seed incumbent, for reporting only (not used as the search's own
    // prune bound -- see the file header for why the search prunes
    // against `before`, the true do-nothing baseline, instead): run the
    // exact sequential-greedy procedure broute's Phase D already uses --
    // each net's plain routeNetSlow call, in this same ranked order, each
    // committing on top of the previous net's fresh state -- once against
    // the identical frozen baseline, then undo it completely before the
    // real search starts. This is the direct, honest "what plain
    // sequential search already achieves for this exact cluster" number
    // this whole experiment exists to compare against.
    for (size_t k = 0; k < netIdx.size(); ++k)
      routeNetSlow(g, prices, usage, lp, region, epsilon, rep.nets[netIdx[k]]);
    std::vector<ChargeRecord> greedyAll;
    for (size_t k = 0; k < netIdx.size(); ++k)
      for (const ChargeRecord &c : rep.nets[netIdx[k]].charges)
        greedyAll.push_back(c);
    long long greedyOverflow = overflowOverUnion(g, usage, oldCharges_flat, greedyAll);
    for (size_t k = 0; k < netIdx.size(); ++k) {
      RoutedNet &rn = rep.nets[netIdx[k]];
      for (const ChargeRecord &c : rn.charges)
        undoCharge(g, usage, c);
      rn.charges.clear();
      rn.segs.clear();
    }
    rep.greedyIncumbentTotal += greedyOverflow;

    // The real search: usage is now back at the "cluster removed"
    // baseline (unchanged since candidate generation finished), exactly
    // what branchAndBoundAssign needs.
    AssignmentResult result = branchAndBoundAssign(g, usage, candidates, before, maxBnbNodes);
    rep.totalBnbNodes += result.nodesExpanded;
    if (result.hitNodeCap)
      ++rep.clustersHitNodeCap;

    bool accepted = false;
    long long after = before;
    if (result.found) {
      // Apply the winning combination for real.
      for (size_t k = 0; k < netIdx.size(); ++k) {
        RoutedNet &rn = rep.nets[netIdx[k]];
        const RouteCandidate &chosen = candidates[k][result.choice[k]];
        rn.charges = chosen.charges;
        rn.segs = chosen.segs;
        for (const ChargeRecord &c : rn.charges) {
          if (c.horizontal)
            for (int x = c.a; x < c.b; ++x)
              usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
          else
            for (int y = c.a; y < c.b; ++y)
              usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
        }
      }
      // A real, final gate anyway -- never an unconditional accept, per
      // this project's ironclad supervision rule, even though the search
      // only ever records a completion strictly better than `before` by
      // construction. Cheap insurance against a subtle bug in the
      // incremental delta bookkeeping being silently trusted.
      std::vector<ChargeRecord> newAll;
      for (size_t k = 0; k < netIdx.size(); ++k)
        for (const ChargeRecord &c : rep.nets[netIdx[k]].charges)
          newAll.push_back(c);
      after = overflowOverUnion(g, usage, oldCharges_flat, newAll);
      accepted = after <= before;
      // Permanent sanity signal, not just a one-off debug print: the two
      // numbers are computed by entirely different code paths (B&B's own
      // incremental delta tracking vs overflowOverUnion's independent
      // recomputation from the actually-committed charges) and should
      // always agree once the baseline-overflow fix above is applied. A
      // rare, small residual disagreement (single digits, not the
      // thousands-off gap the missing baseline term caused) has still
      // been observed on real data and not yet root-caused -- reported
      // honestly here rather than claimed fixed. It has never once
      // caused an incorrect accept: `after`, not `result.overflow`, is
      // what accept/reject and every CSV/trace number actually use, so
      // this imprecision only ever affects the search's own internal
      // pruning arithmetic, never the final committed result. A real
      // discrepancy here caught the missing background-overflow term
      // during this file's own first real-benchmark screen -- see
      // branchAndBoundAssign's own comment -- so this stays as a
      // standing check rather than being removed once that bug was found.
      if (trace && after != result.overflow)
        std::cerr << "cluster " << clusterIdx << ": WARNING bnb internal overflow="
                  << result.overflow << " disagrees with independently recomputed after="
                  << after << " -- accept/reject still used the trusted `after` value\n";
    }

    // Did eviction actually do anything here, or was a helper merely
    // recruited and never needed? Candidate index 0 is always a net's own
    // original route (see the candidate-generation loop below), so any
    // helper (the last helperIdx.size() entries in netIdx) landing on a
    // different index means it was genuinely moved as part of the
    // winning combination -- the real answer to "did cooperative eviction
    // cause this win," not just "was a helper present." Only meaningful
    // when `accepted` (an unaccepted attempt reverts every net, helpers
    // included, back to index 0 regardless of what the search considered).
    bool helperMoved = false;
    if (accepted) {
      ++rep.clustersAccepted;
      rep.clusterNetsTouched += static_cast<int>(netIdx.size());
      for (size_t k = netIdx.size() - helperIdx.size(); k < netIdx.size(); ++k)
        if (result.choice[k] != 0)
          helperMoved = true;
      if (helperMoved)
        ++rep.clustersAcceptedWithHelpers;
      if (trace)
        std::cerr << "cluster " << clusterIdx << ": " << netIdx.size() << " nets ("
                  << helperIdx.size() << " recruited, " << contextIdx.size() << " context), "
                  << candidateTotal << " candidates, " << result.nodesExpanded
                  << " bnb nodes, overflow " << before << " -> " << after << " (greedy would give "
                  << greedyOverflow << ", accepted"
                  << (helperMoved ? ", eviction exercised)" : ")") << "\n";
    } else {
      // Revert every net in the cluster to exactly its original state --
      // never a partial commit. Covers both "the search found nothing
      // better than doing nothing" and "the final gate above somehow
      // disagreed with the search's own bookkeeping."
      for (size_t k = 0; k < netIdx.size(); ++k) {
        RoutedNet &rn = rep.nets[netIdx[k]];
        for (const ChargeRecord &c : rn.charges)
          undoCharge(g, usage, c);
        rn.charges = oldCharges[k];
        rn.segs = oldSegs[k];
        for (const ChargeRecord &c : rn.charges) {
          if (c.horizontal)
            for (int x = c.a; x < c.b; ++x)
              usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
          else
            for (int y = c.a; y < c.b; ++y)
              usage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
        }
      }
      after = before;
      if (trace)
        std::cerr << "cluster " << clusterIdx << ": " << netIdx.size() << " nets ("
                  << helperIdx.size() << " recruited, " << contextIdx.size() << " context), "
                  << candidateTotal << " candidates, " << result.nodesExpanded
                  << " bnb nodes, overflow " << before << " unchanged (greedy would give "
                  << greedyOverflow << ", rejected)\n";
    }

    if (logging)
      log << clusterIdx << "," << netIdx.size() << "," << helperIdx.size() << ","
          << contextIdx.size() << "," << candidateTotal << "," << result.nodesExpanded << ","
          << before << "," << greedyOverflow << "," << after << "," << (result.hitNodeCap ? 1 : 0)
          << "," << (accepted ? 1 : 0) << "," << (helperMoved ? 1 : 0) << "\n";

    // Whatever this cluster is actually left with -- `after` if accepted,
    // `before` (unchanged) if not -- summed across every cluster, for the
    // run-level "did joint search beat greedy overall" comparison against
    // rep.greedyIncumbentTotal.
    rep.bnbBestTotal += after;

    progressLog().record("joint", clusterIdx, totalOverflow(g, usage), rep.nets);
  }

  rep.bestOverflow = totalOverflow(g, usage);
  if (trace)
    std::cerr << "joint assignment: " << rep.clustersAttempted << " clusters attempted, "
              << rep.clustersAccepted << " accepted, " << rep.clusterNetsTouched
              << " nets touched, " << rep.clustersHitNodeCap << " hit the node cap, "
              << rep.totalBnbNodes << " total bnb nodes, " << rep.clustersWithHelpers
              << " clusters recruited a helper (" << rep.helpersRecruited << " total), "
              << rep.clustersAcceptedWithHelpers << " of the accepted clusters actually moved a "
                 "recruited helper, " << rep.clustersWithContext << " clusters widened with "
                 "context nets (" << rep.contextNetsUsed << " total), overflow now "
              << rep.bestOverflow << "\n";
}

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
  bool trace = std::getenv("QROUTE_TRACE") != nullptr;

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
    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    progressLog().record("fast", round, overflow, nets);
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
    progressLog().record("slow", round, overflow, nets);
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

  // Phase D: the joint candidate-assignment mechanism, once phase B has
  // genuinely stopped moving. Resync nets/usage to rep.nets' own
  // best-seen snapshot first, same reasoning as every other phase
  // transition in this file. QROUTE_JOINT=0 skips this entirely,
  // reproducing plain phase A/B behavior.
  bool jointEnabled = [] {
    const char *b = std::getenv("QROUTE_JOINT");
    return b ? std::atoi(b) != 0 : true;
  }();
  if (jointEnabled && !rep.convergedToZero) {
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
    negotiateJointAssignment(g, prices, usage, lp, region, epsilon, rep);
    if (rep.bestOverflow == 0)
      rep.convergedToZero = true;
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
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
  Design d = parse("qroute_selftest_zigzag.gr", kZigzagGr);
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

// The decisive claim MSMV's own correctness rests on (ported verbatim
// from zroute.cpp/vroute.cpp): given several simultaneous sources,
// multiSourceDijkstra must find a path from whichever one is actually
// cheapest to reach a sink from -- not just the first source in the
// list. Coordinates are tile indices, matching kBasicGr's 4x4-tile grid:
// a "far" source at tile (0,0) standing in for a pin, and a "near" one at
// tile (2,0) standing in for a point somewhere in already-committed wire
// -- exactly what netFootprint() feeds this function during real net
// growth. A sink at tile (3,0) is 3 tiles from the far source but only 1
// from the near one.
void testMultiSourceDijkstraUsesCheapestSourceNotFirstInList() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
// search must stop at whichever is actually cheapest to reach. A single
// source at tile (0,0); a "far" sink at tile (3,0) listed first and a
// "near" sink at tile (1,0) listed second.
void testMultiSourceDijkstraStopsAtCheapestSink() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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

// The decisive proof of this file's own new addition (not just that MSMV
// itself works, which the two tests above already cover): a hand-verified
// 3-pin fixture where MSMV's candidate genuinely differs from -- and
// beats -- what routeNetSlow's fixed-MST decomposition can ever produce.
// Pins at tile (0,0), (4,0), (2,2) are a deliberately Manhattan-equilateral
// triangle: every pairwise distance is exactly 4 (|4-0|=4; |2-0|+|2-0|=4;
// |4-2|+|2-0|=4). mstEdges' own tie-breaking (strict `<`, so ties keep the
// earliest-seen index) always builds the tree from pins 0 and 1 and 0 and
// 2 -- two independent 4-tile paths sharing only the point (0,0), 8 tiles
// total. But the direct path between pins 0 and 1 passes straight through
// tile (2,0), which is only 2 tiles from pin 2 -- a true Steiner point no
// fixed-MST decomposition can ever attach to, since routeNetSlow only
// ever attaches at the *pins* an MST edge names, never at an arbitrary
// point mid-wire. MSMV's own attach-anywhere-on-committed-wire growth
// finds exactly this: connect pins 0 and 1 first (4 tiles, whichever the
// search reaches first -- both are equally cheap from pin 0), then attach
// the remaining pin to the closest point on *that already-committed
// wire*, which is tile (2,0) at distance 2, not either endpoint at
// distance 4 -- a 6-tile tree, 25% shorter, structurally unreachable by
// the fixed-MST mechanism.
const char *kMsmvSteinerGr = R"(grid 5 3 1
vertical capacity 4
horizontal capacity 4
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netSteiner 0 3 1
5 5 1
45 5 1
25 25 1
0
)";

void testMsmvCandidateFindsCheaperTopologyThanFixedMst() {
  Design d = parse("qroute_selftest_msmv_steiner.gr", kMsmvSteinerGr);
  const RoutingGrid &g = d.grid;

  auto totalLength = [](const RoutedNet &rn) {
    long long len = 0;
    for (const ChargeRecord &c : rn.charges)
      len += (c.b - c.a);
    return len;
  };

  RoutedNet mstNet;
  mstNet.net = &d.nets[0];
  mstNet.pts = {{0, 0}, {4, 0}, {2, 2}};
  mstNet.mst = mstEdges(mstNet.pts);
  {
    Prices prices(g);
    Usage usage(g);
    LayerPlan lp(g);
    SearchRegion region;
    routeNetSlow(g, prices, usage, lp, region, 0.5, mstNet);
  }
  check(totalLength(mstNet) == 8,
        "the fixed-MST decomposition commits two independent 4-tile paths "
        "(hand-verified: this fixture's three pins are pairwise 4 tiles "
        "apart, so mstEdges keeps both edges anchored at pin 0), 8 tiles "
        "total");

  RoutedNet msmvNet;
  msmvNet.net = &d.nets[0];
  msmvNet.pts = {{0, 0}, {4, 0}, {2, 2}};
  msmvNet.mst = mstEdges(msmvNet.pts); // unused by MSMV, kept for parity
  {
    Prices prices(g);
    Usage usage(g);
    LayerPlan lp(g);
    SearchRegion region;
    routeNetSlowMSMV(g, prices, usage, lp, region, 0.5, msmvNet);
  }
  check(totalLength(msmvNet) == 6,
        "MSMV finds the true Steiner-optimal 6-tile tree -- connecting the "
        "first two pins, then attaching the third to the midpoint of that "
        "already-committed wire rather than to either endpoint -- exactly "
        "the topology the fixed-MST decomposition structurally cannot "
        "represent");

  check(chargeEdgeSet(g, msmvNet.charges) != chargeEdgeSet(g, mstNet.charges),
        "the two mechanisms commit genuinely different edge sets, not "
        "just a coincidentally equal-cost alternative");
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
  Design d = parse("qroute_selftest_priceoverflow.gr", kPriceOverflowGr);
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
// QROUTE_HIST_STEP=0) layer 0's frozen-but-lower lambda wins every time,
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
  Design d = parse("qroute_selftest_history.gr", kHistoryTrapGr);
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
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
// QROUTE_HIST_RELAX=0 must reproduce iroute's exact permanent behavior --
// this is the regression guard for that claim, not the relaxation
// mechanism itself (see testRelaxationUndoesExactlyOneStepPerCleanRound and
// testRelaxationFullyRestoresBaselineAfterEqualCleanRounds below for that).
void testDisablingRelaxReproducesPermanentBehavior() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
        "with QROUTE_HIST_RELAX=0, history never decreases even after usage "
        "drops back under capacity -- reproduces iroute's exact permanent "
        "behavior, the fallback this file's relaxation mechanism can always "
        "be switched off back to");
}

// The actual new mechanism: an edge dirty for exactly one round, then one
// clean round, should have its single growth operation popped and undone
// exactly -- back to the 1.0 no-op baseline, not merely "decreased."
void testRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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

// QROUTE_HIST_STREAK: growth must be gated behind N *consecutive*
// over-capacity round boundaries, and a single clear round must reset the
// count to zero rather than merely pausing it -- otherwise an edge that
// flickers over/under/over capacity every other round would still
// eventually accumulate growth from unrelated non-consecutive rounds,
// which defeats the entire point of requiring a *streak*.
void testHistoryStreakRequiresConsecutiveOvercapacityRounds() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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

// QROUTE_HIST_SCALE=overflow: an edge sitting further over capacity should
// accumulate history faster than one just barely over, unlike the default
// flat step which treats "1 unit over" and "10x over" identically.
void testHistoryScalesWithOverflowMagnitude() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
        "QROUTE_HIST_SCALE=overflow makes the 10x-over edge accumulate "
        "history faster than the 1.5x-over edge in the very same round -- "
        "the decisive behavioral change this mode is supposed to buy");
  check(scaledWayOver > flatWayOver,
        "the way-over edge grows faster under overflow-scaling than it "
        "would have under the flat default");
}

// QROUTE_HIST_MODE=ratio: histH *= usage/capacity directly, no separate
// step or factor. A user-proposed formula, n2 = n1*(1 + overflow_percent/100),
// which is algebraically exactly usage/capacity when "overflow_percent" is
// the excess over capacity as a percentage (e.g. usage=150,cap=100 -> 50%
// -> 1 + 0.5 = 1.5 = 150/100).
void testHistoryRatioModeMultipliesByUsageOverCapacityExactly() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices pr(g);
  Usage fiftyOver(g);
  fiftyOver.h[idx] = 3; // capacity 2 -> ratio exactly 1.5
  updateHistory(g, pr, fiftyOver, /*histStep=*/0.0, HistMode::kRatio, /*histFactor=*/0.0);
  check(std::abs(pr.histH[idx] - 1.5) < 1e-9,
        "QROUTE_HIST_MODE=ratio on a 50%-over edge (usage 3, capacity 2) "
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

// QROUTE_HIST_RATIO_EXPONENT: squaring the ratio should punish a badly-over
// edge much harder while barely changing a barely-over one -- the decisive
// property the user proposed this for.
void testHistoryRatioExponentSquaresTheRatio() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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

// QROUTE_HIST_EXPONENT_ESCALATION: the effective exponent for a round's
// growth should climb with how many still-unrelaxed penalties are already
// on the stack, and a relaxed edge should use a *lower* effective exponent
// for its next penalty than an edge that stayed dirty continuously -- not
// merely end up at a lower history value, the escalation level itself must
// have dropped. Uses a fixed usage/capacity=2.0 throughout (capacity 2,
// usage 4) so every factor is an exact power of 2, checkable exactly.
void testExponentEscalationTracksStackSizeAndRelaxesWithIt() {
  Design d = parse("qroute_selftest_basic.gr", kBasicGr);
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
        "the property QROUTE_HIST_EXPONENT_ESCALATION's header claims by "
        "reusing the stack's own size as the escalation count");
}

// ---------------------------------------------------------------------------
// Phase D (joint candidate-assignment) tests.
// ---------------------------------------------------------------------------

// A 4x1 grid, one layer, purely synthetic -- no real net pins involved at
// all, since branchAndBoundAssign takes precomputed candidates directly.
// Base horizontal capacity 1 everywhere, with one capacity adjustment
// raising the rightmost edge (tiles 2-3) to capacity 2.
const char *kJointBnbGr = R"(grid 4 1 1
vertical capacity 1
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
dummy 0 2 1
5 5 1
15 5 1
1
2 0 1 3 0 1 2
)";

// The decisive proof of the solve method itself: three "nets," each with
// two candidates, referencing three edges -- P (tiles 0-1, cap 1), Q
// (tiles 1-2, cap 1), R (tiles 2-3, cap 2, raised by this fixture's own
// adjustment). Net1 = {P, R}, net2 = {P, Q}, net3 = {Q, R}. The naive
// "everyone picks their own first/cheapest candidate independently"
// combination -- net1->P, net2->P, net3->Q -- double-books P (usage 2,
// cap 1, overflow 1) while R sits completely idle; no per-net local
// reasoning about "which of my own candidates is cheapest" can discover
// that moving net1 onto the otherwise-unused R fully legalizes everything
// (P=1, Q=1, R=1-of-2, overflow 0) without first considering what net2
// and net3 are going to need. Only genuinely joint reasoning across all
// three at once finds it.
void testBranchAndBoundFindsTrueJointOptimumGreedyMisses() {
  Design d = parse("qroute_selftest_bnb.gr", kJointBnbGr);
  const RoutingGrid &g = d.grid;
  Usage usage(g); // all zero -- a pure synthetic instance

  auto mk = [&](int a, int b, int amount) {
    ChargeRecord c{true, 0, a, b, 0, amount};
    return RouteCandidate{{c}, {}};
  };
  RouteCandidate p = mk(0, 1, 1); // edge P, tiles 0-1
  RouteCandidate q = mk(1, 2, 1); // edge Q, tiles 1-2
  RouteCandidate r = mk(2, 3, 1); // edge R, tiles 2-3 (cap 2 here)

  std::vector<std::vector<RouteCandidate>> candidates = {
      {p, r}, // net1: candidate0=P, candidate1=R
      {p, q}, // net2: candidate0=P, candidate1=Q
      {q, r}, // net3: candidate0=Q, candidate1=R
  };

  // Seed deliberately loose -- the search must find 0 on its own merits,
  // not because the seed already ruled out anything worse than the naive
  // combination's overflow of 1.
  AssignmentResult result = branchAndBoundAssign(g, usage, candidates, 1000, 100000);

  check(result.found, "the search found a complete assignment strictly better than the loose seed");
  check(result.overflow == 0,
        "the search found the true joint optimum (overflow 0), not just *a* "
        "legal-ish combination -- the naive 'everyone picks their own first "
        "candidate' combination this fixture was built to expose leaves 1 "
        "unit of overflow on P while R sits fully idle");
  check(result.choice.size() == 3, "a complete combination was recorded for all three nets");

  // This fixture's own resource graph (net1-P-net2-Q-net3-R-net1, a
  // 6-cycle) has more than one perfect matching -- overflow 0 is
  // achievable more than one way -- so rather than assert one specific
  // tie-broken answer, verify the property that actually matters
  // independently: recompute usage from whichever combination the search
  // returned and confirm every edge it touches is genuinely within
  // capacity, and confirm it is not the naive "everyone picks their own
  // first/cheapest candidate" combination (net1=P, net2=P, net3=Q, i.e.
  // choice {0,0,0}) this fixture exists to expose as locally-plausible
  // but jointly wrong.
  int usageP = 0, usageQ = 0, usageR = 0;
  for (size_t i = 0; i < candidates.size(); ++i)
    for (const ChargeRecord &c : candidates[i][result.choice[i]].charges) {
      if (c.a == 0)
        usageP += c.amount;
      else if (c.a == 1)
        usageQ += c.amount;
      else
        usageR += c.amount;
    }
  check(usageP <= 1 && usageQ <= 1 && usageR <= 2,
        "recomputing usage independently from the chosen combination confirms "
        "every edge is genuinely within capacity (P<=1, Q<=1, R<=2), not just "
        "self-reported as overflow 0");
  check(!(result.choice[0] == 0 && result.choice[1] == 0 && result.choice[2] == 0),
        "the winning combination is not the naive 'everyone picks their own "
        "first candidate' one (net1=P, net2=P, net3=Q) -- a real joint "
        "rearrangement was found");
}

// A 7x5 grid, one layer, capacity 1 *everywhere*. Two nets share the exact
// same two pins (0,2) and (2,2); their only sensible direct path is the
// two-edge span between them at row 2.
const char *kJointContentionGr = R"(grid 7 5 1
vertical capacity 1
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 2
netP 0 2 1
5 25 1
25 25 1
netQ 1 2 1
5 25 1
25 25 1
0
)";

// Integration test on a real grid, through the full candidate-generation
// + branch-and-bound pipeline (real routeNetSlow calls, not hand-fed
// candidates). Asserts the one invariant that must always hold regardless
// of this specific fixture's geometry: the joint search's actual result
// never does worse than what plain sequential greedy alone would have
// achieved for the same cluster. Whether it does *strictly* better here
// is a separate, harder-to-guarantee-by-construction question (Dijkstra's
// own cost-minimization already does some of the same work greedily, the
// same reason broute's own two-nets-on-one-edge fixture never needed
// joint action to resolve) -- reported plainly rather than overclaiming.
void testJointAssignmentNeverWorseThanGreedyIncumbent() {
  Design d = parse("qroute_selftest_joint.gr", kJointContentionGr);
  const RoutingGrid &g = d.grid;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  SearchRegion region;

  RunReport rep;
  for (int n = 0; n < 2; ++n) {
    RoutedNet rn;
    rn.net = &d.nets[n];
    rn.pts = {{0, 2}, {2, 2}};
    rn.mst = {{0, 1}};
    ChargeRecord c{true, 2, 0, 2, 0, 1}; // horizontal, row 2, x:0->2, layer 0, amount 1
    rn.charges = {c};
    rn.segs = {{0, 2, 0, 2, 2, 0}};
    for (int x = c.a; x < c.b; ++x)
      usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
    rep.nets.push_back(std::move(rn));
  }

  negotiateJointAssignment(g, prices, usage, lp, region, 0.5, rep);

  check(rep.clustersAttempted == 1, "exactly one cluster was found and attempted");
  check(rep.bnbBestTotal <= rep.greedyIncumbentTotal,
        "the joint search's actual result never does worse than what plain "
        "sequential greedy would have achieved for the same cluster -- the "
        "structural invariant this mechanism must always hold");
  check(rep.bestOverflow == 0,
        "on this fixture, the joint search (like broute's own sequential "
        "mechanism before it) resolves the two nets to a fully legal split");
}

// Same regression guard as every other rescue mechanism in this lineage
// (yroute's Phase C, zroute's, broute's own Phase D, froute's Phase E):
// rebuild usage completely independently from rep.nets' final committed
// charges and require it to match the live usage this mechanism actually
// left behind, edge for edge. Reuses broute's own congested multi-net
// fixture shape (uniform tight capacity, several nets, tiny round budget)
// so Phase D genuinely runs through the full route() pipeline.
const char *kJointCongestedGr = R"(grid 7 5 1
vertical capacity 1
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 4
netA 0 2 1
5 25 1
65 25 1
netB 1 2 1
5 25 1
65 25 1
netC 2 2 1
5 5 1
65 45 1
netD 3 2 1
5 45 1
65 5 1
0
)";

void testJointAssignmentLeavesUsageConsistentWithFinalCommittedCharges() {
  Design d = parse("qroute_selftest_joint2.gr", kJointCongestedGr);
  const RoutingGrid &g = d.grid;
  RunReport rep = route(d, 1, 1, 0.5, 0.0, HistMode::kAdditive, 0.0);
  check(rep.clustersAttempted > 0,
        "the fixture actually forced Phase D to attempt at least one real "
        "cluster, not just confirm a no-op");

  Usage rebuilt(g);
  for (const RoutedNet &rn : rep.nets)
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          rebuilt.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      else
        for (int y = c.a; y < c.b; ++y)
          rebuilt.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
    }

  long long rebuiltOverflow = totalOverflow(g, rebuilt);
  check(rebuiltOverflow == rep.bestOverflow,
        "overflow recomputed independently from rep.nets' own final "
        "committed charges (" + std::to_string(rebuiltOverflow) + ") exactly "
        "matches this file's own self-reported bestOverflow (" +
        std::to_string(rep.bestOverflow) + ") -- Phase D's joint-assignment "
        "accept/reject logic never leaves usage diverged from what was "
        "actually committed, the same class of bug yroute's own rescue "
        "pass once had");
}

// Forces QROUTE_MAX_BNB_NODES down to essentially nothing and confirms the
// mechanism degrades safely: falls back to leaving the cluster exactly as
// it was, never crashes, never produces something worse than doing
// nothing. Reuses the same two-nets-on-one-edge fixture as the decisive
// invariant test above.
void testNodeBudgetSafetyValveFallsBackToDoingNothing() {
  Design d = parse("qroute_selftest_joint3.gr", kJointContentionGr);
  const RoutingGrid &g = d.grid;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  SearchRegion region;

  RunReport rep;
  for (int n = 0; n < 2; ++n) {
    RoutedNet rn;
    rn.net = &d.nets[n];
    rn.pts = {{0, 2}, {2, 2}};
    rn.mst = {{0, 1}};
    ChargeRecord c{true, 2, 0, 2, 0, 1};
    rn.charges = {c};
    rn.segs = {{0, 2, 0, 2, 2, 0}};
    for (int x = c.a; x < c.b; ++x)
      usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
    rep.nets.push_back(std::move(rn));
  }
  std::vector<ChargeRecord> origCharges0 = rep.nets[0].charges;
  std::vector<ChargeRecord> origCharges1 = rep.nets[1].charges;

  setenv("QROUTE_MAX_BNB_NODES", "1", 1);
  negotiateJointAssignment(g, prices, usage, lp, region, 0.5, rep);
  unsetenv("QROUTE_MAX_BNB_NODES");

  check(rep.clustersHitNodeCap == 1, "the tiny node budget was actually exhausted, not accidentally enough");
  check(rep.clustersAccepted == 0, "with essentially no search budget, nothing beat leaving the cluster alone");
  check(rep.bestOverflow == totalOverflow(g, usage), "sanity: self-reported overflow matches live usage");
  check(chargeEdgeSet(g, rep.nets[0].charges) == chargeEdgeSet(g, origCharges0) &&
            chargeEdgeSet(g, rep.nets[1].charges) == chargeEdgeSet(g, origCharges1),
        "both nets are left with exactly their original charges -- the "
        "node-cap fallback never leaves a partial or corrupted commit");
}

// A 7x5 grid with a genuinely clean, never-overflowed net elsewhere on
// the same grid alongside the two-nets-on-one-edge contention -- mirrors
// froute.cpp's own testReorderLeavesNeverOverflowedNetsCompletelyUntouched.
// Proves this mechanism's scope restriction actually holds: a net that
// never contributed to any overflow cluster is left completely
// untouched, not ripped up and immediately reproduced.
const char *kJointScopeGr = R"(grid 7 5 1
vertical capacity 1
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 3
netClean 0 2 1
45 5 1
55 5 1
netP 1 2 1
5 25 1
25 25 1
netQ 2 2 1
5 25 1
25 25 1
0
)";

void testJointAssignmentLeavesNeverOverflowedNetsCompletelyUntouched() {
  Design d = parse("qroute_selftest_joint_scope.gr", kJointScopeGr);
  const RoutingGrid &g = d.grid;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  SearchRegion region;
  RunReport rep;

  RoutedNet clean;
  clean.net = &d.nets[0];
  clean.pts = {{4, 0}, {5, 0}};
  clean.mst = {{0, 1}};
  ChargeRecord cc{true, 0, 4, 5, 0, 1}; // row0, x4->5, layer0, amount1 (cap1, legal)
  clean.charges = {cc};
  clean.segs = {{4, 0, 0, 5, 0, 0}};
  usage.h[g.hIndex(4, 0, 0)] += cc.amount;
  std::vector<ChargeRecord> cleanChargesBefore = clean.charges;
  size_t cleanSegsBefore = clean.segs.size();
  rep.nets.push_back(std::move(clean));

  for (int n = 1; n <= 2; ++n) {
    RoutedNet rn;
    rn.net = &d.nets[n];
    rn.pts = {{0, 2}, {2, 2}};
    rn.mst = {{0, 1}};
    ChargeRecord c{true, 2, 0, 2, 0, 1};
    rn.charges = {c};
    rn.segs = {{0, 2, 0, 2, 2, 0}};
    for (int x = c.a; x < c.b; ++x)
      usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
    rep.nets.push_back(std::move(rn));
  }

  negotiateJointAssignment(g, prices, usage, lp, region, 0.5, rep);

  check(rep.clustersAttempted == 1, "exactly the one contended cluster was found");
  check(chargeEdgeSet(g, rep.nets[0].charges) == chargeEdgeSet(g, cleanChargesBefore),
        "the never-overflowed net's charges are bit-for-bit unchanged -- "
        "Phase D never ripped it up, let alone reconsidered it");
  check(rep.nets[0].segs.size() == cleanSegsBefore, "its segment list wasn't touched either");
}

// A 9-tile-wide, 1-row, uniform-capacity-4 synthetic grid -- no real net
// pins involved (recruitEvictionHelpers takes hand-built nets/usage
// directly, the same style aroute's own
// testBranchAndBoundFindsTrueJointOptimumGreedyMisses uses for the
// underlying search). The cluster is tiles 1-6; seven distinct nets, each
// on its own edge so none interfere with another's usage, cover every
// exclusion rule the function must apply: already-included, legal-and-
// touching (three of these, to test the cap and the ranking), legal-but-
// not-touching, giant-and-touching, and overflowed-and-touching.
const char *kRecruitGr = R"(grid 9 1 1
vertical capacity 4
horizontal capacity 4
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
dummy 0 2 1
5 5 1
85 5 1
0
)";

void testRecruitEvictionHelpersRanksByOccupiedCapacityAndExcludesIneligibleNets() {
  Design d = parse("qroute_selftest_recruit.gr", kRecruitGr);
  const RoutingGrid &g = d.grid;
  Usage usage(g);

  std::vector<bool> inCluster(static_cast<size_t>(g.nx) * g.ny, false);
  for (int t = 1; t <= 6; ++t)
    inCluster[t] = true;

  std::vector<RoutedNet> nets(7);
  auto setCharge = [&](RoutedNet &rn, int x, int amount) {
    ChargeRecord c{true, 0, x, x + 1, 0, amount};
    rn.charges = {c};
    usage.h[g.hIndex(x, 0, 0)] += amount;
  };
  for (RoutedNet &rn : nets)
    rn.net = &d.nets[0];
  setCharge(nets[0], 1, 1); // already-included (excluded via alreadyIncluded)
  setCharge(nets[1], 2, 3); // netA: legal, touching, occupied=3 (top)
  setCharge(nets[2], 3, 2); // netB: legal, touching, occupied=2 (2nd)
  setCharge(nets[3], 4, 1); // netC: legal, touching, occupied=1 (3rd -- cut by the cap)
  setCharge(nets[4], 7, 4); // netFar: legal, amount=4, but tiles 7-8 don't touch the cluster
  setCharge(nets[5], 5, 4); // netGiant: legal and touching, but flagged giant below
  nets[5].giant = true;
  setCharge(nets[6], 6, 6); // netOver: touching, but amount 6 > capacity 4 -- itself overflowed

  check(!netOverflowed(g, usage, nets[1]) && !netOverflowed(g, usage, nets[2]) &&
            !netOverflowed(g, usage, nets[3]) && !netOverflowed(g, usage, nets[4]),
        "netA/netB/netC/netFar really are legal (amount <= capacity 4) -- hand-verified before "
        "testing the function under test");
  check(netOverflowed(g, usage, nets[6]),
        "netOver really is overflowed (amount 6 > capacity 4) -- hand-verified");

  setenv("QROUTE_MAX_EVICTION_HELPERS", "2", 1);
  std::vector<size_t> helpers = recruitEvictionHelpers(g, usage, nets, inCluster, {0});
  unsetenv("QROUTE_MAX_EVICTION_HELPERS");

  check(helpers.size() == 2,
        "the cap (2) is enforced even though three legal, touching, non-excluded nets exist");
  check(helpers.size() == 2 && helpers[0] == 1 && helpers[1] == 2,
        "ranked by occupied capacity descending -- netA (occupied 3) first, netB (occupied 2) "
        "second -- netC (occupied 1), netFar (untouching), netGiant, and netOver are all "
        "correctly excluded: three different disqualifying reasons plus one real ranking cut, "
        "not a single blanket rule");
}

// Reuses kRecruitGr's exact fixture and cast of nets -- recruitContextNets
// shares its whole ranking/eligibility logic with recruitEvictionHelpers
// (rankLegalTouchingNets), so the same exclusion rules must hold; the one
// real difference under test here is the cap itself, wide enough (with
// this fixture's cap set to 3) to admit all three eligible legal touching
// nets at once, unlike recruitEvictionHelpers' own deliberately narrow
// default.
void testRecruitContextNetsAdmitsAllEligibleNetsUpToItsOwnWiderCap() {
  Design d = parse("qroute_selftest_recruit2.gr", kRecruitGr);
  const RoutingGrid &g = d.grid;
  Usage usage(g);

  std::vector<bool> inCluster(static_cast<size_t>(g.nx) * g.ny, false);
  for (int t = 1; t <= 6; ++t)
    inCluster[t] = true;

  std::vector<RoutedNet> nets(7);
  auto setCharge = [&](RoutedNet &rn, int x, int amount) {
    ChargeRecord c{true, 0, x, x + 1, 0, amount};
    rn.charges = {c};
    usage.h[g.hIndex(x, 0, 0)] += amount;
  };
  for (RoutedNet &rn : nets)
    rn.net = &d.nets[0];
  setCharge(nets[0], 1, 1); // already-included
  setCharge(nets[1], 2, 3); // netA: legal, touching, occupied=3
  setCharge(nets[2], 3, 2); // netB: legal, touching, occupied=2
  setCharge(nets[3], 4, 1); // netC: legal, touching, occupied=1
  setCharge(nets[4], 7, 4); // netFar: untouching
  nets[5].net = &d.nets[0];
  setCharge(nets[5], 5, 4);
  nets[5].giant = true; // netGiant: touching but giant
  setCharge(nets[6], 6, 6); // netOver: touching but overflowed

  setenv("QROUTE_MAX_CONTEXT_NETS", "3", 1);
  std::vector<size_t> context = recruitContextNets(g, usage, nets, inCluster, {0});
  unsetenv("QROUTE_MAX_CONTEXT_NETS");

  check((context == std::vector<size_t>{1, 2, 3}),
        "with the cap wide enough (3), all three eligible legal touching nets are recruited, "
        "still ranked by occupied capacity descending -- and netFar, netGiant, and netOver "
        "remain correctly excluded regardless of the wider cap, proving those exclusion rules "
        "are about eligibility, not just a side effect of a narrow cap trimming them away");
}

// A 3x3-tile, single-layer, uniform-capacity-1 grid. netA and netB share
// the exact same two pins, (0,0) and (2,0), hand-jammed onto the direct
// row-0 span (usage 2, cap 1, overflow 2). netC's pins are (2,0) and
// (0,2) -- Manhattan distance 4, giving it genuine route freedom -- with
// its own endpoint sitting at (2,0), the same tile netA/netB's cluster
// occupies, so it is a real, geometrically legitimate eviction candidate
// (recruitEvictionHelpers' own touch test requires exactly this kind of
// shared-tile contact; see that function's header for why a net on a
// merely nearby, non-touching row can never be recruited at all -- an
// early version of this fixture put netC on a parallel row and it was
// never even considered). netC's own hand-set original route goes
// down-then-left (column 2, then row 2), deliberately never touching row
// 0 beyond that one shared pin, so it starts out fully legal.
const char *kEvictionGr = R"(grid 3 3 1
vertical capacity 1
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 3
netA 0 2 1
5 5 1
25 25 1
netB 1 2 1
5 5 1
25 25 1
netC 2 2 1
5 25 1
25 5 1
0
)";

// This is *not* the decisive "eviction strictly beats not evicting" proof
// this mechanism's whole premise argues for -- despite real, sustained
// effort to construct one (see the file's own commit history), no fixture
// small enough to hand-verify was found where recruiting netC actually
// changes the outcome on this exact grid. What this test *does* prove,
// exhaustively rather than by inspection: with eviction enabled, netC is
// correctly recruited (a real, geometrically legitimate helper, not
// excluded by any of recruitEvictionHelpers' own rules), genuinely
// considered by the exact branch-and-bound search alongside netA and
// netB's own candidates, and the search -- which is exact, not a
// heuristic, so if a strictly better combination existed among the
// generated candidates it would have been found and would have replaced
// this result -- still lands on exactly the same overflow with or
// without netC available to move. That is a real, decisive answer for
// *this* fixture (recruiting netC's specific candidate set does not
// help here), just not the headline "it works" result the mechanism is
// ultimately measured on; that measurement is the real-benchmark screen
// in HISTORY_COST_SWEEP.md, where cooperative opportunities like this
// one are far more likely to occur than in a 3x3 hand-built grid. Kept
// as a permanent regression/plumbing test (recruitment, candidate
// generation, and the exact search all wire together correctly and never
// regress) rather than deleted, matching froute.cpp's own precedent for
// when a decisive fixture proved elusive.
void testCooperativeEvictionRecruitsButExactSearchFindsNoBetterCombinationHere() {
  Design d = parse("qroute_selftest_eviction.gr", kEvictionGr);
  const RoutingGrid &g = d.grid;

  auto runScenario = [&](int maxHelpers) {
    RunReport rep;
    Usage usage(g);
    for (int n = 0; n < 2; ++n) { // netA, netB: hand-set jammed on row0
      RoutedNet rn;
      rn.net = &d.nets[n];
      rn.pts = {{0, 0}, {2, 0}};
      rn.mst = {{0, 1}};
      ChargeRecord c{true, 0, 0, 2, 0, 1};
      rn.charges = {c};
      rn.segs = {{0, 0, 0, 2, 0, 0}};
      for (int x = c.a; x < c.b; ++x)
        usage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      rep.nets.push_back(std::move(rn));
    }
    { // netC: pins (2,0)-(0,2), touching the row0 cluster via its own pin
      // at (2,0); hand-set original route goes down-then-left (col2 then
      // row2), deliberately never touching row0 beyond that shared pin.
      RoutedNet rn;
      rn.net = &d.nets[2];
      rn.pts = {{2, 0}, {0, 2}};
      rn.mst = {{0, 1}};
      ChargeRecord cv{false, 2, 0, 2, 0, 1};
      ChargeRecord ch{true, 2, 0, 2, 0, 1};
      rn.charges = {cv, ch};
      rn.segs = {{2, 0, 0, 2, 2, 0}, {0, 2, 0, 2, 2, 0}};
      usage.v[g.vIndex(cv.cross, 0, cv.layer)] += cv.amount;
      usage.v[g.vIndex(cv.cross, 1, cv.layer)] += cv.amount;
      usage.h[g.hIndex(0, ch.cross, ch.layer)] += ch.amount;
      usage.h[g.hIndex(1, ch.cross, ch.layer)] += ch.amount;
      rep.nets.push_back(std::move(rn));
    }

    Prices prices(g);
    LayerPlan lp(g);
    SearchRegion region;

    long long before = totalOverflow(g, usage);
    check(before == 2, "hand-set starting state really is overflowed by exactly 2 "
                        "(netA and netB both amount-1 on two capacity-1 row-0 edges)");
    check(!netOverflowed(g, usage, rep.nets[2]),
          "netC's own hand-set original route really is legal before eviction runs");

    std::string helpersStr = std::to_string(maxHelpers);
    setenv("QROUTE_MAX_EVICTION_HELPERS", helpersStr.c_str(), 1);
    negotiateJointAssignment(g, prices, usage, lp, region, 0.5, rep);
    unsetenv("QROUTE_MAX_EVICTION_HELPERS");
    return rep;
  };

  RunReport withoutEviction = runScenario(0);
  check(withoutEviction.clustersWithHelpers == 0,
        "with QROUTE_MAX_EVICTION_HELPERS=0, no helper is ever recruited");
  check(withoutEviction.bestOverflow == 1,
        "netA/netB alone still find a real, partial improvement (2 -> 1) via netB's own "
        "row-1 detour -- this fixture was never infeasible, just not fully resolvable "
        "without netC's own participation");

  RunReport withEviction = runScenario(4);
  check(withEviction.clustersWithHelpers == 1,
        "with eviction enabled, netC IS correctly recruited -- a real, geometrically "
        "legitimate helper, not excluded by any of recruitEvictionHelpers' own rules");
  check(withEviction.bestOverflow <= withoutEviction.bestOverflow,
        "the structural invariant that must always hold regardless of this specific "
        "fixture's geometry: recruiting a helper never makes the result worse");
  check(withEviction.bestOverflow == 1,
        "on this exact fixture, the exact search -- which would have found and kept any "
        "strictly better combination reachable from netC's own generated candidates, since "
        "branch-and-bound is exhaustive, not heuristic -- still lands on the identical "
        "overflow with netC available to move as without it. See this test's own comment "
        "for what that does and doesn't prove");
}

// Reuses kEvictionGr's own netA/netB-jammed/netC-legal-and-touching
// scenario, but with eviction disabled and context widening on, to prove
// context nets behave exactly as this mechanism's whole premise requires:
// ripped up during candidate generation, but *always* restored afterward
// regardless of what the search decides -- unlike a recruited eviction
// helper, a context net is never even offered a candidate slot, so there
// is no "did it move" question to ask at all, only "is it back."
void testContextNetsAreRippedUpDuringGenerationAndAlwaysFullyRestoredAfterward() {
  Design d = parse("qroute_selftest_context.gr", kEvictionGr);
  const RoutingGrid &g = d.grid;

  RunReport rep;
  for (int n = 0; n < 2; ++n) {
    RoutedNet rn;
    rn.net = &d.nets[n];
    rn.pts = {{0, 0}, {2, 0}};
    rn.mst = {{0, 1}};
    ChargeRecord c{true, 0, 0, 2, 0, 1};
    rn.charges = {c};
    rn.segs = {{0, 0, 0, 2, 0, 0}};
    rep.nets.push_back(std::move(rn));
  }
  RoutedNet cnet;
  cnet.net = &d.nets[2];
  cnet.pts = {{2, 0}, {0, 2}};
  cnet.mst = {{0, 1}};
  ChargeRecord cv{false, 2, 0, 2, 0, 1};
  ChargeRecord ch{true, 2, 0, 2, 0, 1};
  cnet.charges = {cv, ch};
  cnet.segs = {{2, 0, 0, 2, 2, 0}, {0, 2, 0, 2, 2, 0}};
  rep.nets.push_back(std::move(cnet));

  Usage usage(g);
  for (int n = 0; n < 2; ++n)
    for (int x = 0; x < 2; ++x)
      usage.h[g.hIndex(x, 0, 0)] += 1;
  usage.v[g.vIndex(2, 0, 0)] += 1;
  usage.v[g.vIndex(2, 1, 0)] += 1;
  usage.h[g.hIndex(0, 2, 0)] += 1;
  usage.h[g.hIndex(1, 2, 0)] += 1;

  std::vector<ChargeRecord> cChargesBefore = rep.nets[2].charges;
  std::vector<Seg> cSegsBefore = rep.nets[2].segs;

  Prices prices(g);
  LayerPlan lp(g);
  SearchRegion region;
  setenv("QROUTE_MAX_EVICTION_HELPERS", "0", 1);
  setenv("QROUTE_MAX_CONTEXT_NETS", "4", 1);
  negotiateJointAssignment(g, prices, usage, lp, region, 0.5, rep);
  unsetenv("QROUTE_MAX_EVICTION_HELPERS");
  unsetenv("QROUTE_MAX_CONTEXT_NETS");

  check(rep.clustersWithContext == 1,
        "netC IS recruited as context -- a real, geometrically legitimate touch, exactly the "
        "same eligibility rule recruitEvictionHelpers itself uses");
  check(rep.clustersWithHelpers == 0,
        "eviction stayed disabled -- netC's participation here is purely as context, never as "
        "a branch-and-bound candidate owner");
  check(chargeEdgeSet(g, rep.nets[2].charges) == chargeEdgeSet(g, cChargesBefore) &&
            rep.nets[2].segs.size() == cSegsBefore.size(),
        "netC's charges are bit-for-bit unchanged after the cluster is processed -- a context "
        "net is never a candidate at all, so unlike an eviction helper there is no accept/"
        "reject-dependent outcome to check, only unconditional restoration");

  Usage rebuilt(g);
  for (const RoutedNet &rn : rep.nets)
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal)
        for (int x = c.a; x < c.b; ++x)
          rebuilt.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      else
        for (int y = c.a; y < c.b; ++y)
          rebuilt.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
    }
  check(totalOverflow(g, rebuilt) == rep.bestOverflow,
        "overflow recomputed independently from every net's final committed charges (including "
        "netC's restored ones) exactly matches this file's own self-reported bestOverflow -- "
        "context widening never leaves usage diverged from what was actually committed");
}

int run() {
  std::cout << "self test\n";
  testBasicShapesStayOnFastTier();
  testEscalationSolvesWhatFastTierCannot();
  testMultiSourceDijkstraUsesCheapestSourceNotFirstInList();
  testMultiSourceDijkstraStopsAtCheapestSink();
  testMsmvCandidateFindsCheaperTopologyThanFixedMst();
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
  testBranchAndBoundFindsTrueJointOptimumGreedyMisses();
  testJointAssignmentNeverWorseThanGreedyIncumbent();
  testJointAssignmentLeavesUsageConsistentWithFinalCommittedCharges();
  testNodeBudgetSafetyValveFallsBackToDoingNothing();
  testJointAssignmentLeavesNeverOverflowedNetsCompletelyUntouched();
  testRecruitEvictionHelpersRanksByOccupiedCapacityAndExcludesIneligibleNets();
  testRecruitContextNetsAdmitsAllEligibleNetsUpToItsOwnWiderCap();
  testCooperativeEvictionRecruitsButExactSearchFindsNoBetterCombinationHere();
  testContextNetsAreRippedUpDuringGenerationAndAlwaysFullyRestoredAfterward();
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
  if (const char *e = std::getenv("QROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("QROUTE_FAST_ROUNDS"); r)
    maxFastRounds = std::atoi(r);
  if (const char *r = std::getenv("QROUTE_SLOW_ROUNDS"); r)
    maxSlowRounds = std::atoi(r);
  if (const char *s = std::getenv("QROUTE_HIST_STEP"); s)
    histStep = std::atof(s);
  if (const char *f = std::getenv("QROUTE_HIST_FACTOR"); f)
    histFactor = std::atof(f);
  if (const char *m = std::getenv("QROUTE_HIST_MODE"); m && std::string(m) == "mult")
    histMode = HistMode::kMultiplicative;
  else if (const char *m = std::getenv("QROUTE_HIST_MODE"); m && std::string(m) == "ratio")
    histMode = HistMode::kRatio;
  if (const char *s = std::getenv("QROUTE_HIST_STREAK"); s)
    streakThreshold = std::max(1, std::atoi(s));
  if (const char *s = std::getenv("QROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
    scaleByOverflow = true;
  bool relaxEnabled = true;
  if (const char *r = std::getenv("QROUTE_HIST_RELAX"); r)
    relaxEnabled = std::string(r) != "0";
  double ratioExponent = 1.0;
  if (const char *r = std::getenv("QROUTE_HIST_RATIO_EXPONENT"); r)
    ratioExponent = std::atof(r);
  double exponentEscalation = 0.0;
  if (const char *r = std::getenv("QROUTE_HIST_EXPONENT_ESCALATION"); r)
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
