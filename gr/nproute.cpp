// nproute.cpp
//
// A second methodology, for comparison against groute's batch pricing loop:
// the same multiplicative-weights price update from CLAUDE.md,
//
//     lambda_e <- lambda_e * exp(epsilon * [how far over capacity])
//
// applied sequentially instead of in batch -- each net solves against the
// current price, and the touched edges' prices update immediately
// afterward, so the very next net in the same pass already sees it. This is
// closer to PathFinder-style negotiated-congestion routing (McMurchie and
// Ebeling 1995) than to Garg-Koenemann: negotiation happens continuously
// within a pass, not once per full round. Only nets currently sitting on an
// overflowed edge get ripped up and rerouted after the first round, the
// same discipline proute's rip-up loop uses.
//
// Why this exists: groute (stage 4) found that its batch rounds could not
// beat its own seed pass's sequential result on any real benchmark, even
// after 150 rounds -- a single price scalar per edge, updated only once
// per round, is a weaker signal than live sequential feedback. Building
// this to isolate that variable (identical cost formula and layer/shape
// machinery as groute; the only change is when prices update) confirmed it:
// with immediate per-net updates instead of once-per-round, the very same
// formula goes from groute's 2.06M-overflow round 0 to 576K on adaptec1 --
// batching, not the formula, was groute's problem.
//
// That still left a gap to proute's rip-up numbers (adaptec1: proute 221K,
// this router's first version ~440-475K depending on epsilon), which led to
// tuning the cost formula itself in two steps -- see priceExponentTerm's
// comment for the full measured history. First, pricing only actual
// overflow instead of any usage at all closed part of the gap (adaptec1
// 405K). Second, replacing that flat "nothing until 100%, then linear"
// shape with a soft early-warning curve -- rising, and escalating faster
// than linearly, starting well before an edge is full -- closed most of
// the rest: adaptec1 299K, newblue1 88K (proute: 59K), bigblue1 376K
// (proute: 310K). Beats groute everywhere, converges in a single round
// instead of dozens, and is within 1.5x of proute's rip-up quality on two
// of the three benchmarks -- the closest any variant of this router has
// gotten. The remaining gap to proute may well need a genuinely different
// idea, not another sweep of these same knobs; recording that honestly
// rather than continuing to tune them past where they're paying off.
//
// One idea that was tried and didn't pay off: varying the threshold across
// rounds instead of holding it fixed, hoping a later relaxation would let
// nets that took a defensive detour find something shorter once the worst
// congestion had already been resolved. See thresholdForRound's comment for
// the full result -- in short, both directions (rising and falling) come
// out worse than holding still, and not for a reason specific to either
// direction: any schedule needs to give up selective rip-up to let already-
// legal nets react at all, and full reroute every round is worse regardless
// of what threshold policy accompanies it. A follow-up tried excusing more
// nets from that reroute -- either by the criterion in markExcusedIfClear's
// comment, or, loosened further, excusing a net once *any single* edge it
// touches (not all of them) is clear -- and both directions got worse the
// more nets were protected, not better (rising's round-1 best: 323708 at
// the default all-clear rule, 413088 with the any-clear rule, both worse
// than not excusing anything at all, 310720). The wirelength question a
// schedule was built to answer never got answered by any variant of it,
// because the overflow cost of the mechanism needed to ask it dominates
// first.
//
// A different idea did pay off, though not the way it was aimed: instead of
// relaxing the threshold *during* the main loop, relaxedCleanup makes one
// supervised pass *after* it converges, giving each net one chance to
// reroute under a much more relaxed threshold and keeping the change only
// if it doesn't increase overflow (measured locally, so this scales to
// every net without rescanning the whole grid each time). Hoped-for result:
// lower wirelength. Actual result: overflow drops substantially instead,
// and wirelength goes *up* a little, the opposite of the goal, at every
// relaxation level tried. Given how close repeated passes get to proute's
// own numbers, this has likely reinvented a version of proute's rip-up
// loop -- supervised one-net-at-a-time relaxation under repetition
// resembles negotiated rip-up more than it resembles a "cleanup."
//
// A follow-up sweep of the cleanup pass's own threshold and pass count
// (0.3 to 1.0, 1 to 60 passes) found overflow keeps improving with more
// passes regardless of exactly which threshold drives it -- 0.3 through
// 0.6 all converge to within 1% of each other by 60 passes -- but the
// *combination* of threshold and wirelength mattered: 0.5 beat the
// original 1.0 (fully relaxed) on both overflow and wirelength at every
// pass count compared head to head, so it is not simply "more relaxed is
// more thorough." Settled on threshold 0.5, 30 passes as the point where
// most of the achievable improvement has landed without the diminishing
// returns of pushing toward 60. This is now the default, not an opt-in:
// the whole point of the per-net safety check is that it cannot make
// things worse, so there was no reason to gate it behind a flag once it
// was tuned. Final numbers, this router's whole progression on total
// overflow / scored wirelength:
//
//   benchmark   lroute            proute            groute
//   adaptec1    708474 / 6827491  221352 / 6111546  484580 / 6076308
//   newblue1    226840 / 6534376   58584 / 5546098  142670 / 5536176
//   bigblue1    728510 / 7270346  309822 / 6445711  547062 / 6398617
//
//   benchmark   nproute (pricing only)   nproute (+ cleanup, this version)
//   adaptec1    299304 / 6763811         227468 / 6871155
//   newblue1     87600 / 6322719          51426 / 6528216
//   bigblue1    376366 / 7178090         324554 / 7293465
//
// newblue1 now beats proute's overflow outright; adaptec1 and bigblue1 are
// within a few percent of it. Wirelength across all three stays
// consistently higher than proute's -- the trade this whole router keeps
// making, from the early-warning curve onward, is overflow for wirelength,
// and the cleanup pass continues that trade rather than reversing it.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o nproute nproute.cpp
//
// Use:
//     ./nproute benchmark.gr solution.route
//     ./nproute --selftest
//
// Environment variables, for tuning experiments (all optional):
//     NPROUTE_EPS=<float>        step size epsilon, default 0.02
//     NPROUTE_THRESHOLD=<float>  early-warning curve start, as a fraction
//                                of capacity, default 0.2. >= 1.0 disables
//                                the curve (flat until 100% full, matching
//                                this router's previous formula).
//     NPROUTE_POWER=<float>      how much the curve escalates approaching
//                                capacity (1.0 = linear), default 2.5
//     NPROUTE_ROUNDS=<int>       max rounds, default 30
//     NPROUTE_PATIENCE=<int>     rounds without improvement before giving
//                                up, default 20
//     NPROUTE_SCHEDULE=rising|falling
//                                vary the threshold across rounds instead of
//                                holding it fixed (see thresholdForRound's
//                                comment) -- measured worse than not setting
//                                this in every variant tried; kept for the
//                                record and in case a future idea revisits
//                                the mechanism rather than just the policy.
//     NPROUTE_SCHEDULE_START, NPROUTE_SCHEDULE_END, NPROUTE_SCHEDULE_ROUNDS
//                                override the schedule's bounds and ramp
//                                length (rounds); only meaningful with
//                                NPROUTE_SCHEDULE set
//     NPROUTE_EXCUSE_THRESHOLD=<float>
//                                in schedule mode, permanently exempt a net
//                                from rip-up once it completes a route
//                                without its edges reaching this fraction of
//                                capacity from other nets (default 0.2) --
//                                see markExcusedIfClear's comment. Measured
//                                worse the higher it's set; kept for the
//                                record alongside the schedule mechanism it
//                                was built to rescue.
//     NPROUTE_EXCUSE_MODE=all|any
//                                "all" (default) requires every edge a net
//                                touches to clear the threshold; "any"
//                                excuses it if even one edge does, even if
//                                others are heavily loaded. Measured worse
//                                than "all" in every schedule direction.
//     NPROUTE_CLEANUP=0          skip the post-convergence cleanup pass
//                                (relaxedCleanup). On by default: its
//                                per-net safety check means it cannot make
//                                overflow worse than the main loop already
//                                found, so there's no real reason to run
//                                without it -- this flag exists mainly for
//                                A/B comparison against the pricing-only
//                                numbers quoted above.
//     NPROUTE_CLEANUP_THRESHOLD=<float>
//                                the cleanup pass's relaxed threshold,
//                                default 0.5 -- swept 0.3 to 1.0 (fully
//                                relaxed) and 0.5 won on both overflow and
//                                wirelength at every pass count compared
//                                head to head, so "more relaxed" is not
//                                simply "more thorough."
//     NPROUTE_CLEANUP_PASSES=<int>
//                                how many full sweeps the cleanup pass
//                                makes, default 30. More passes keep
//                                improving overflow with diminishing
//                                returns (adaptec1 at threshold 0.5: 227K at
//                                30 passes, 226K at 60) at roughly linear
//                                extra cost in runtime; 30 was chosen as
//                                where the returns clearly start flattening.
//     NPROUTE_TRACE=1            print each round's overflow (and, while a
//                                schedule is active, its threshold; while a
//                                cleanup pass runs, its before/after) to
//                                stderr
//     NPROUTE_TRACE_EXCUSED=1    with NPROUTE_TRACE and a schedule active,
//                                also print how many nets are excused so far

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
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
constexpr double kForbiddenPrice = 1e9; // see groute.cpp's comment on this

// ---------------------------------------------------------------------------
// Prices and usage. Unlike groute, both persist across the whole run: usage
// is only ever adjusted by an explicit rip-up (subtract) or commit (add) for
// one net at a time, and price only ever moves up, per the formula.
// ---------------------------------------------------------------------------

struct Prices {
  std::vector<double> h, v;
  explicit Prices(const RoutingGrid &g) : h(g.hCount(), 1.0), v(g.vCount(), 1.0) {}
};
struct Usage {
  std::vector<int> h, v;
  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

double edgeCost(double lambda, int amount) { return 1.0 + lambda * amount; }

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
};

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
    for (int x = a; x < b; ++x)
      cost += edgeCost(pr.h[g.hIndex(x, y, l)], amount);
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
    for (int y = a; y < b; ++y)
      cost += edgeCost(pr.v[g.vIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost};
  }
  return best;
}

// ---------------------------------------------------------------------------
// MST, candidates -- identical to groute; the shape generator is not the
// variable this file is testing.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Pricing, committing, and -- the difference from groute -- an immediate,
// localized price update right after a net's charges land.
// ---------------------------------------------------------------------------

struct Seg {
  int x1, y1, l1, x2, y2, l2;
};
struct ChargeRecord {
  bool horizontal;
  int cross, a, b, layer, amount;
};

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

void undoCharge(const RoutingGrid &g, Usage &u, const ChargeRecord &c) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x)
      u.h[g.hIndex(x, c.cross, c.layer)] -= c.amount;
  } else {
    for (int y = c.a; y < c.b; ++y)
      u.v[g.vIndex(c.cross, y, c.layer)] -= c.amount;
  }
}

// The negotiation step: re-price exactly the edges one charge touched,
// against the usage as it stands right now (after that charge landed).
// Called once per charge, immediately after committing it, so the very next
// net processed -- in this same pass -- already sees the result. This is
// the entire difference from groute's updatePrices, which does this same
// per-edge formula but only after every net in a round has gone.
//
// The exponent's argument is a soft early-warning curve: 0 below
// NPROUTE_THRESHOLD (default 0.2, i.e. 20% full) of an edge's capacity,
// then ramps from 0 to 1 between the threshold and full capacity, raised to
// NPROUTE_POWER (default 2.5) so the approach to capacity escalates rather
// than growing linearly; at and beyond capacity it keeps extrapolating the
// same curve upward rather than switching formulas, so there's no seam
// right at 100% the way there was in every version before this one.
//
// Four variants were measured against each other on adaptec1 before
// settling here (see the stage-4-tuning commits for the full numbers):
//   usage/cap directly (the literal CLAUDE.md formula): best overflow
//     534762, and it gets *worse* with more rounds -- every
//     previously-touched edge keeps getting marginally pricier even
//     nowhere near congested, pushing nets into detours that buy nothing.
//   max(0, usage/cap - 1) (flat until 100%, i.e. threshold=1.0, power=1
//     below): best overflow 404910 at epsilon=0.002 -- a real improvement,
//     but converges slowly and has a hard seam right at capacity: an edge
//     at 99% and one at 40% look identical to the pricer.
//   max(0, usage - cap) (absolute overflow instead of a ratio, matching how
//     proute's own ad hoc penalty charges a flat amount per unit regardless
//     of capacity): best overflow 537904 -- worse. The plausible reason (a
//     1-unit overflow priced up to 5x weaker on adaptec1's capacity-32
//     layers than its capacity-6 ones under the ratio form) did not survive
//     contact with measurement.
//   this curve (threshold 0.2, power 2.5): best overflow 299304, converging
//     in a single round instead of dozens. Giving nets a genuine early
//     warning as an edge fills, escalating faster than linearly as it
//     approaches capacity, measurably beats waiting for the threshold to be
//     crossed before caring at all. threshold and power were swept jointly
//     (0 to 1 in steps of 0.05-0.1, power 0.5 to 4); the result is a broad,
//     stable plateau roughly in threshold [0.15, 0.45] x power [2, 3], not
//     a knife-edge optimum -- 0.2/2.5 sits comfortably inside it.
// `threshold` is now a parameter rather than a fixed static lookup, so a
// per-round schedule (see thresholdForRound below) can vary it call to
// call; the static default (env var or 0.2) lives in defaultThreshold()
// for callers that aren't scheduling anything.
double priceExponentTerm(double usage, double cap, double threshold) {
  static const double power = [] {
    const char *p = std::getenv("NPROUTE_POWER");
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
    const char *t = std::getenv("NPROUTE_THRESHOLD");
    return t ? std::atof(t) : 0.2;
  }();
  return threshold;
}

void negotiate(const RoutingGrid &g, Prices &pr, const Usage &u, const ChargeRecord &c,
               double epsilon, double threshold) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x) {
      size_t i = g.hIndex(x, c.cross, c.layer);
      if (g.hcap[i] <= 0)
        pr.h[i] = kForbiddenPrice;
      else
        pr.h[i] *= std::exp(epsilon * priceExponentTerm(u.h[i], g.hcap[i], threshold));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] *= std::exp(epsilon * priceExponentTerm(u.v[i], g.vcap[i], threshold));
    }
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

// ---------------------------------------------------------------------------
// Routing one net: solve against current price, commit, then negotiate --
// update price for exactly what was just touched, before returning.
// ---------------------------------------------------------------------------

struct RoutedNet {
  const Net *net = nullptr;
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> mst;
  std::vector<Seg> segs;
  std::vector<ChargeRecord> charges;
  bool giant = false;
  // Set once a net completes a route while every edge it touches was still
  // under the exclusion threshold (see markExcusedIfClear), from usage
  // contributed by everything else -- never cleared once set. Only
  // meaningful in schedule mode; see the schedule comment above route().
  bool excused = false;
};

void routeNet(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
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
      negotiate(g, prices, usage, rn.charges[i], epsilon, threshold);

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

// EXPERIMENTAL, schedule mode only: a net that completes while every edge
// it touches is still under this threshold, counting only what everything
// else has charged there, didn't have to make any congestion-influenced
// choice -- it just took the geometrically obvious path through open space.
// Rerouting it later can only make it react to a threshold it never had a
// reason to think about, so once a net earns this it's excused from rip-up
// for the rest of the run, schedule or no schedule. This is a fixed
// reference point, independent of wherever the schedule's own threshold
// currently sits -- otherwise a net routed at the far end of a rising
// schedule would need to clear its way against a much higher bar than one
// routed at round 0, for no principled reason.
//
// Measured result on adaptec1: at the default 0.2, only ~17% of nets
// (30822 of 176716) ever earn the excuse after round 0 -- most nets, by the
// time ~177K of them have been placed sequentially in a real, busy design,
// have already brushed against *some* edge at 20%+ utilization, so 20% is
// a low bar for "didn't encounter congestion" here. That alone didn't
// change either schedule's outcome much (rising: 310720 -> 323708 at round
// 1 -- very slightly *worse*, not better). Raising the excuse threshold
// does protect far more nets (86% at 0.9) but makes results worse, not
// better, and monotonically so: rising's round-1 best goes 323708 (0.2) ->
// 344802 (0.5) -> 366760 (0.7) -> 376250 (0.9); falling shows the same
// pattern. The counterintuitive but consistent lesson: nets that only
// brushed moderate usage are often exactly the ones with the slack needed
// to absorb a shift when the threshold moves, so excusing more of them
// doesn't reduce churn usefully, it just removes the flexibility a
// schedule needs to resolve overflow at all. No setting of this threshold,
// in either schedule direction, beat the static baseline (299304).
constexpr double kExcuseThresholdDefault = 0.2;

// NPROUTE_EXCUSE_MODE selects how many of a net's edges need to be clear:
//   all (default) -- every edge the net touches must be under threshold.
//   any            -- at least one edge under threshold is enough, even if
//                     others are heavily loaded. A much easier bar to
//                     clear, so far more nets qualify; see the measured
//                     result in kExcuseThresholdDefault's comment for
//                     whether that's actually better.
bool excuseModeAny() {
  static const bool any = [] {
    const char *m = std::getenv("NPROUTE_EXCUSE_MODE");
    return m && std::string(m) == "any";
  }();
  return any;
}

void markExcusedIfClear(const RoutingGrid &g, const Usage &usage, RoutedNet &rn) {
  if (rn.excused)
    return;
  static const double excuseThreshold = [] {
    const char *e = std::getenv("NPROUTE_EXCUSE_THRESHOLD");
    return e ? std::atof(e) : kExcuseThresholdDefault;
  }();
  bool any = excuseModeAny();
  bool sawAnyEdge = false;
  for (const ChargeRecord &c : rn.charges) {
    if (c.horizontal) {
      for (int x = c.a; x < c.b; ++x) {
        size_t i = g.hIndex(x, c.cross, c.layer);
        double preUsage = static_cast<double>(usage.h[i]) - c.amount;
        bool clear = preUsage / g.hcap[i] < excuseThreshold;
        sawAnyEdge = true;
        if (any && clear) {
          rn.excused = true;
          return;
        }
        if (!any && !clear)
          return;
      }
    } else {
      for (int y = c.a; y < c.b; ++y) {
        size_t i = g.vIndex(c.cross, y, c.layer);
        double preUsage = static_cast<double>(usage.v[i]) - c.amount;
        bool clear = preUsage / g.vcap[i] < excuseThreshold;
        sawAnyEdge = true;
        if (any && clear) {
          rn.excused = true;
          return;
        }
        if (!any && !clear)
          return;
      }
    }
  }
  // "all" mode: every edge passed (or there were none). "any" mode falling
  // through here means no edge ever qualified.
  if (!any || !sawAnyEdge)
    rn.excused = true;
}

// ---------------------------------------------------------------------------
// The negotiation loop
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

// Total overflow, but restricted to the union of edges two charge lists
// touch, deduplicated. Used by the cleanup pass to judge whether rerouting
// one net made things worse without rescanning the whole grid: every edge
// outside this union has unchanged usage before and after that net's
// re-route, so the delta computed here over just its own edges equals the
// true delta in global total overflow.
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
  bool convergedToZero = false;
  bool stalled = false;
};

// A one-shot, safe attempt to shrink wirelength after the main loop has
// already resolved overflow: reroute every non-giant net once more under a
// far more relaxed threshold (the schedule experiments' failure mode was
// giving *every* net a reason to reconsider every round, forever; this
// gives each net exactly one supervised chance instead). Each net's change
// is checked against overflowOverUnion immediately -- if it made things
// worse, even locally, it's reverted before moving to the next net, so a
// bad trade never has a chance to compound the way the schedule's
// unsupervised full reroute did.
void relaxedCleanup(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
                     double epsilon, RunReport &rep) {
  double relaxedThreshold = [] {
    const char *t = std::getenv("NPROUTE_CLEANUP_THRESHOLD");
    return t ? std::atof(t) : 0.5;
  }();
  int passes = [] {
    const char *p = std::getenv("NPROUTE_CLEANUP_PASSES");
    return p ? std::atoi(p) : 30;
  }();
  bool trace = std::getenv("NPROUTE_TRACE") != nullptr;
  long long before = rep.bestOverflow;

  for (int pass = 0; pass < passes; ++pass) {
    for (RoutedNet &rn : rep.nets) {
      if (rn.giant)
        continue;
      std::vector<ChargeRecord> oldCharges = rn.charges;
      std::vector<Seg> oldSegs = rn.segs;
      long long localBefore = overflowOverUnion(g, usage, oldCharges, oldCharges);

      routeNet(g, prices, usage, lp, epsilon, relaxedThreshold, rn);

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

// EXPERIMENTAL: a round-varying threshold, for testing whether relaxing (or
// tightening) the early-warning curve over time changes the wirelength/
// overflow trade-off -- see the tuning notes above route() for what this
// measured. Returns -1 (a sentinel, not a valid threshold) when
// NPROUTE_SCHEDULE isn't set, meaning "use the static default everywhere."
//
//   NPROUTE_SCHEDULE=rising   ramps threshold up: cautious early (catches
//                             the worst congestion while nothing has
//                             committed to anything yet), relaxed later
//                             (once things have stabilized, let nets that
//                             took a defensive detour reconsider under a
//                             calmer landscape). Default 0.0 -> 0.9.
//   NPROUTE_SCHEDULE=falling  the reverse: relaxed early, cautious late.
//                             Default 0.9 -> 0.0.
//   NPROUTE_SCHEDULE_START, NPROUTE_SCHEDULE_END, NPROUTE_SCHEDULE_ROUNDS
//                             override the bounds and the ramp length
//                             (rounds to go from start to end; holds at end
//                             afterward). Default ramp length 10 rounds.
//
// A schedule only makes sense if nets that are already legal get a chance
// to react to the changing landscape too, so route() reroutes every net
// every round while one is active, not just currently-overflowed ones.
//
// Measured result on adaptec1 (static baseline: 299304): rising (0.0 ->
// 0.9 over 10 rounds) bottoms out at 310720 in round 1, at threshold
// barely above 0 -- then gets monotonically worse every single round as
// the threshold climbs, reaching 645944 by round 29. Falling (0.9 -> 0.0)
// is worse from round 0 (531694, vs. rising's round-0 446004: starting
// cautious matters even before anything else happens) and, once it
// reaches threshold 0, plateaus around 410-412K -- still 37% worse than
// the static baseline. Neither schedule beat holding still.
//
// The isolating experiment: NPROUTE_SCHEDULE=falling with START=END=0.2 --
// a *constant* threshold, but run through this same code path, meaning
// full reroute every round instead of selective rip-up. Round 1 gives
// 307114, close to the static baseline's 299304 (both are, after all,
// pricing everything the same way at that point) -- then it drifts to
// 392306 by round 14, reproducing the exact churn selective rip-up was
// built to fix. That is the actual finding: it is not that rising is bad
// or falling is bad, it is that *any* schedule requires giving up
// selective rip-up to let already-legal nets react at all, and full
// reroute every round is a strictly worse dynamic than selective rip-up
// regardless of what threshold policy rides on top of it. The wirelength
// question this was built to answer never got a clean answer, because
// the overflow cost of the mechanism needed to ask it dominates before
// any wirelength effect has a chance to show up.
double thresholdForRound(int round) {
  static const std::string schedule = [] {
    const char *s = std::getenv("NPROUTE_SCHEDULE");
    return s ? std::string(s) : std::string();
  }();
  if (schedule.empty())
    return -1.0;
  static const bool rising = schedule == "rising";
  static const double start = [] {
    const char *s = std::getenv("NPROUTE_SCHEDULE_START");
    return s ? std::atof(s) : (rising ? 0.0 : 0.9);
  }();
  static const double end = [] {
    const char *s = std::getenv("NPROUTE_SCHEDULE_END");
    return s ? std::atof(s) : (rising ? 0.9 : 0.0);
  }();
  static const int rampRounds = [] {
    const char *s = std::getenv("NPROUTE_SCHEDULE_ROUNDS");
    return s ? std::atoi(s) : 10;
  }();
  double t = rampRounds > 0 ? std::min(1.0, static_cast<double>(round) / rampRounds) : 1.0;
  return start + (end - start) * t;
}

// With the early-warning curve, convergence is fast (round 1 on all three
// benchmarks tested) rather than the slow multi-round burn the flat
// overflow-only formula needed. Patience stays well above 1 anyway as a
// safety margin for benchmarks not in that set of three.
constexpr int kDefaultPatience = 20;

RunReport route(const Design &d, int maxRounds, double epsilon) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  int kPatience = kDefaultPatience;
  if (const char *p = std::getenv("NPROUTE_PATIENCE"); p)
    kPatience = std::atoi(p);

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

  double staticThreshold = defaultThreshold();

  // Giant nets: route once, negotiating as they go same as everyone else on
  // this first touch, then frozen -- never revisited by later rounds. Always
  // priced at the static threshold regardless of any schedule -- they are
  // never revisited anyway, so there is no "later, relaxed round" for them
  // to benefit from.
  for (RoutedNet &rn : nets)
    if (rn.giant)
      routeNet(g, prices, usage, lp, epsilon, staticThreshold, rn);

  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxRounds; ++round) {
    double scheduled = thresholdForRound(round);
    bool scheduling = scheduled >= 0.0;
    double threshold = scheduling ? scheduled : staticThreshold;
    if (scheduling && std::getenv("NPROUTE_TRACE"))
      std::cerr << "round " << round << " threshold " << threshold << "\n";

    // Round 0 gives every net its first route. After that, with no schedule
    // running, only nets currently sitting on an overflowed edge get ripped
    // up and rerouted -- matching proute's rip-up discipline. Rerouting an
    // already-legal net every round bought nothing but churn: measured on
    // adaptec1, "reroute everyone always" drifts from a round-1 low of 473K
    // up past 530K over the following rounds, since a net with a perfectly
    // good route can get shaken loose by *other* nets' price changes even
    // though nothing about its own situation changed.
    //
    // A schedule changes that calculus: the whole point of relaxing (or
    // tightening) the threshold over rounds is to let nets that are already
    // legal reconsider under the new landscape, so while one is running,
    // every net is rerouted every round -- except ones excused because they
    // completed without ever having to react to any real congestion (see
    // markExcusedIfClear). Excusing those is what makes a schedule's
    // "reroute everyone" different from the plain version that measured
    // worse: a net that took the obvious open-space path has no reason to
    // reconsider just because the threshold moved, and protecting it from
    // that churn is exactly the selective-rip-up discipline, applied using
    // a criterion a schedule can actually use (unlike "is it overflowed
    // right now," which excuses nothing while a threshold is deliberately
    // being made more permissive).
    for (RoutedNet &rn : nets) {
      if (rn.giant)
        continue;
      if (!scheduling && round > 0 && !netOverflowed(g, usage, rn))
        continue;
      if (scheduling && rn.excused)
        continue;
      routeNet(g, prices, usage, lp, epsilon, threshold, rn);
      if (scheduling)
        markExcusedIfClear(g, usage, rn);
    }

    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    if (std::getenv("NPROUTE_TRACE")) {
      std::cerr << "round " << round << " overflow " << overflow << "\n";
      if (scheduling && std::getenv("NPROUTE_TRACE_EXCUSED")) {
        long long excusedCount = 0;
        for (const RoutedNet &rn : nets)
          if (rn.excused)
            ++excusedCount;
        std::cerr << "  excused so far: " << excusedCount << " / " << nets.size() << "\n";
      }
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
      break;
    }
    if (roundsSinceImprovement >= kPatience) {
      rep.stalled = true;
      break;
    }
  }

  bool doCleanup = [] {
    const char *c = std::getenv("NPROUTE_CLEANUP");
    return !c || (std::string(c) != "0" && std::string(c) != "false");
  }();
  if (doCleanup && !rep.nets.empty()) {
    // rep.nets is the best round found, which is not necessarily the round
    // the loop actually ended on (a later round can regress after finding
    // the best one). `usage` and `nets` above reflect wherever the loop
    // stopped, not necessarily rep.nets, so usage is rebuilt from rep.nets'
    // own charges before the cleanup pass touches anything, rather than
    // trusting state that might belong to a different, worse configuration.
    Usage cleanupUsage(g);
    for (const RoutedNet &rn : rep.nets)
      for (const ChargeRecord &c : rn.charges) {
        if (c.horizontal)
          for (int x = c.a; x < c.b; ++x)
            cleanupUsage.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
        else
          for (int y = c.a; y < c.b; ++y)
            cleanupUsage.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
      }
    relaxedCleanup(g, prices, cleanupUsage, lp, epsilon, rep);
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

void testBasicShapes() {
  Design d = parse("nproute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design");
  check(rep.convergedToZero, "reports convergence, not a stall");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs), "netE (high-to-low bend) is one component");
}

// The same three-net stampede fixture groute's self test uses. There, a
// flat batch start makes all three collide identically in round 0, and only
// seeding (a separate sequential pre-pass) avoids it. Here there is no
// separate seed pass at all, and the price formula gives zero signal until
// an edge is actually pushed over capacity (see priceExponentTerm), so
// round 0 still lets the collision happen once, exactly like it would
// without any congestion awareness -- but the very next round, selective
// rip-up (only nets currently sitting on an overflowed edge get rerouted,
// same discipline proute uses) reroutes net 2 or 3 around it and resolves
// it. groute needed a whole separate mechanism (the seed pass) to avoid
// this; nproute's own round 1 does it with nothing extra.
const char *kStampedeGr = R"(grid 9 8 2
vertical capacity 0 3
horizontal capacity 3 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 3
net1 0 2 1
5 5 1
55 25 1
net2 1 2 1
15 5 1
65 45 1
net3 2 2 1
25 5 1
75 65 1
1
2 0 1 3 0 1 1
)";

void testNegotiationResolvesStampedeWithoutSeeding() {
  Design d = parse("nproute_selftest_stampede.gr", kStampedeGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.bestOverflow == 0,
        "selective rip-up resolves the collision without any separate "
        "seed pass, unlike groute");
  check(rep.bestRound <= 1,
        "resolved within a round of the collision happening -- no seeding "
        "mechanism needed to avoid it in advance");
  for (const RoutedNet &rn : rep.nets)
    check(segsConnected(rn.segs), "net " + rn.net->name + " is one component");
}

int run() {
  std::cout << "self test\n";
  testBasicShapes();
  testNegotiationResolvesStampedeWithoutSeeding();
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

  int maxRounds = 30;
  double epsilon = 0.02; // tuned empirically against adaptec1; see NPROUTE_EPS to explore
  if (const char *e = std::getenv("NPROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("NPROUTE_ROUNDS"); r)
    maxRounds = std::atoi(r);

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxRounds, epsilon);
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " nets\n";
  std::cerr << "best overflow: " << rep.bestOverflow << " (round " << rep.bestRound
            << " of " << rep.roundsRun << " run)\n";
  std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                     : rep.stalled ? "stalled: no improvement for "
                                                       "several rounds, stopping\n"
                                                   : "hit the round cap\n");
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
