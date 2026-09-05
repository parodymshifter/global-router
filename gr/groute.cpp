// groute.cpp
//
// Stage 4: the pricing loop. Replaces proute's ad hoc overflow penalty with
// the multiplicative-weights cost model from CLAUDE.md:
//
//     cost_e   = 1 + lambda_e * width_i / w_i
//     lambda_e <- lambda_e * exp(epsilon * usage_e / capacity_e)
//
// w_i (net priority weight) is 1 for every net -- net typing is stage 6 and
// hasn't happened yet, so there is no weight to divide by.
//
// This is architecturally different from proute, not just a swapped-in cost
// function, per the project's own module-structure diagram: "the solvers
// never talk to each other. The only shared write is the accumulator."
// Concretely:
//   - A round routes every (non-giant) net fresh, from scratch, against one
//     frozen price vector. A net's decision this round depends only on the
//     current prices -- never on another net's charge made earlier in the
//     same round. That is what "solvers never talk to each other" buys: the
//     per-net solves are order-independent and, later, parallelizable.
//   - Usage is only ever computed by summing that round's solutions, after
//     all of them are in. Prices update once per round from that sum.
//   - There is no notion of "current usage" during a solve at all: the cost
//     function reads price only, matching "prices flow into the solvers as a
//     read-only cost overlay."
// proute's rip-up loop, by contrast, mutates usage live as it reroutes nets
// one at a time within a round -- cheaper, but it breaks the independence
// the Garg-Koenemann convergence bound (see the LP-width caveat in
// CLAUDE.md) and this project's later parallelism plan both depend on.
//
// Seeding: CLAUDE.md suggests seeding prices from a demand estimate (RUDY or
// a cheap pattern route) specifically to avoid a stampede in the *parallel*
// setting. There is no parallelism yet (stage 7), so that motivation doesn't
// apply -- prices start flat at 1.0 and the loop is left to discover
// congestion on its own over a few rounds. Revisit this once stage 7 makes
// the stampede real.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o groute groute.cpp
//
// Use:
//     ./groute benchmark.gr solution.route
//     ./groute --selftest
//
// Environment variables, for tuning experiments (all optional):
//     GROUTE_EPS=<float>       step size epsilon, default 0.5
//     GROUTE_ROUNDS=<int>      max batch rounds, default 30
//     GROUTE_PATIENCE=<int>    rounds without improvement before giving up,
//                              default 5
//     GROUTE_TRACE=1           print each round's overflow to stderr

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

// Nets this large are exempt from the ISPD overflow/connectivity checks.
// Routing one fresh every round -- a thousand-edge tree, repeatedly -- would
// spend most of the router's time on nets nothing is actually checking.
// Route them once against the initial (flat) prices and freeze them: their
// real resource consumption still counts every round, only their own route
// choice does not change.
constexpr size_t kGiantNetPins = 1000;

// ---------------------------------------------------------------------------
// Prices: one lambda per edge, read-only during a solve, updated once after
// every net has solved against the current snapshot.
// ---------------------------------------------------------------------------

struct Prices {
  std::vector<double> h, v; // parallel to grid.hcap / grid.vcap
  explicit Prices(const RoutingGrid &g) : h(g.hCount(), 1.0), v(g.vCount(), 1.0) {}
};

// Usage, in real consumption units (width + layer spacing) -- recomputed from
// scratch each round by summing that round's solutions. Never mutated mid
// round; a solve never sees it.
struct Usage {
  std::vector<int> h, v;
  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

// Cost of routing `amount` of consumption over one edge at price `lambda`:
// 1 for the wire itself, plus the priced congestion term. w_i = 1 (see file
// header), so "amount / w_i" is just "amount".
double edgeCost(double lambda, int amount) { return 1.0 + lambda * amount; }

// ---------------------------------------------------------------------------
// Layer direction
// ---------------------------------------------------------------------------

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

// Cheapest layer for running `width` through [a,b) on row/col `cross`, priced
// purely from the current price snapshot -- no usage or capacity involved.
struct LayerChoice {
  int layer;
  double cost;
  int amount;
};

LayerChoice bestHLayer(const RoutingGrid &g, const Prices &pr, const std::vector<int> &layers,
                        int y, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<double>::max(), 0};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    double cost = 0;
    for (int x = a; x < b; ++x)
      cost += edgeCost(pr.h[g.hIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost, amount};
  }
  return best;
}
LayerChoice bestVLayer(const RoutingGrid &g, const Prices &pr, const std::vector<int> &layers,
                        int x, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<double>::max(), 0};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    double cost = 0;
    for (int y = a; y < b; ++y)
      cost += edgeCost(pr.v[g.vIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost, amount};
  }
  return best;
}

// ---------------------------------------------------------------------------
// Minimum spanning tree over a net's distinct pin tiles -- unchanged from
// proute. A round changes how a tree edge is drawn; it does not re-decide
// the tree's topology.
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

// ---------------------------------------------------------------------------
// Candidate routes for one tree edge -- geometry unchanged from proute (the
// shape generator is not what stage 4 changes; the cost function is).
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

// ---------------------------------------------------------------------------
// Pricing and committing a candidate against a fixed price snapshot
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
      p.cost += 1.0; // one via, matching the contest's 1-tile-per-via rule
    prevLayer = lc.layer;
    p.legLayer.push_back(lc.layer);
  }
  return p;
}

struct EdgeResult {
  int layerAtP1, layerAtP2;
};

// Emits segments (including inter-leg vias) and charges, using layers already
// chosen by priceCandidate. This only ever writes to `segs`/`charges` -- never
// to a shared usage array -- keeping one net's solve invisible to every
// other net solving against the same snapshot.
EdgeResult commitCandidate(const Candidate &cand, const std::vector<int> &legLayer,
                            const RoutingGrid &g, int width, std::vector<Seg> &segs,
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
    if (leg.horizontal)
      segs.push_back({leg.a, leg.cross, l, leg.b, leg.cross, l});
    else
      segs.push_back({leg.cross, leg.a, l, leg.cross, leg.b, l});
    charges.push_back({leg.horizontal, leg.cross, leg.a, leg.b, l, amount});
  }
  return {layerAtP1, curLayer};
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
// Routing one net against a fixed price snapshot, start to finish.
// ---------------------------------------------------------------------------

struct RoutedNet {
  const Net *net = nullptr;
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> mst;
  std::vector<Seg> segs;
  std::vector<ChargeRecord> charges;
  bool giant = false;
};

void routeNet(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
              RoutedNet &rn) {
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

    EdgeResult r = commitCandidate(candidates[bestIdx], bestPriced.legLayer, g,
                                    rn.net->minWidth, rn.segs, rn.charges);
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
// The pricing loop
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

// The accumulator: the only shared write in the whole loop. Summed fresh
// from every net's charges each round -- never carried over or mutated
// incrementally.
Usage accumulate(const RoutingGrid &g, const std::vector<RoutedNet> &nets) {
  Usage u(g);
  for (const RoutedNet &rn : nets) {
    for (const ChargeRecord &c : rn.charges) {
      if (c.horizontal) {
        for (int x = c.a; x < c.b; ++x)
          u.h[g.hIndex(x, c.cross, c.layer)] += c.amount;
      } else {
        for (int y = c.a; y < c.b; ++y)
          u.v[g.vIndex(c.cross, y, c.layer)] += c.amount;
      }
    }
  }
  return u;
}

// A handful of edges in a real benchmark have capacity reduced to exactly 0
// by an adjustment (a hard blockage) or belong to a layer with no capacity
// in this direction at all -- adaptec1 has 188 of the former. usage/capacity
// is 0/0 (NaN) before anything ever touches such an edge and x/0 (+Inf) the
// moment something does. Both are wrong and, once a price is NaN or Inf, it
// stays that way forever (multiplying by anything leaves it NaN, and Inf
// only grows) and any candidate that ever sums a cost across it is silently
// corrupted from then on. A zero-capacity edge is exactly what an
// infinite price should mean, so pin it there directly instead of deriving
// it from a division that breaks.
constexpr double kForbiddenPrice = 1e9;

void updatePrices(const RoutingGrid &g, Prices &prices, const Usage &u, double epsilon) {
  for (size_t i = 0; i < prices.h.size(); ++i) {
    if (g.hcap[i] <= 0)
      prices.h[i] = kForbiddenPrice;
    else
      prices.h[i] *= std::exp(epsilon * static_cast<double>(u.h[i]) / g.hcap[i]);
  }
  for (size_t i = 0; i < prices.v.size(); ++i) {
    if (g.vcap[i] <= 0)
      prices.v[i] = kForbiddenPrice;
    else
      prices.v[i] *= std::exp(epsilon * static_cast<double>(u.v[i]) / g.vcap[i]);
  }
}

// ---------------------------------------------------------------------------
// Seeding: a compact, self-contained sequential pass -- structurally the
// same shape as proute's stage-3 router -- used only to produce a usage
// estimate to seed prices from. Its cost model (usage vs. capacity, with a
// fixed overflow penalty) is deliberately not the batch loop's price-based
// one; it exists to bootstrap the loop, not to be the loop.
//
// Without this, round 0 has every net choosing under flat prices with zero
// mutual awareness: exactly the stampede CLAUDE.md warns a flat start
// causes ("many solvers reacting identically to the same signal, with
// nobody seeing anybody else's charge"). That is a property of the
// batch/Jacobi update rule itself, not of literal multithreading -- it is
// already the problem here, single threaded or not. Measured on adaptec1:
// without seeding, round 0 overflows by ~6.0M and the loop spends its whole
// round budget clawing back from that hole; with seeding, round 0 starts
// from what a congestion-aware sequential pass already found.
namespace seed {

constexpr long long kOverflowWeight = 50;

long long edgeCost(int used, int cap, int amount) {
  long long overflow = static_cast<long long>(used) + amount - cap;
  return 1 + (overflow > 0 ? overflow * kOverflowWeight : 0);
}

struct LayerChoice {
  int layer;
  long long cost;
};

LayerChoice bestHLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                        int y, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<long long>::max()};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    long long cost = 0;
    for (int x = a; x < b; ++x)
      cost += edgeCost(u.h[g.hIndex(x, y, l)], g.hcap[g.hIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost};
  }
  return best;
}
LayerChoice bestVLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                        int x, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<long long>::max()};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    long long cost = 0;
    for (int y = a; y < b; ++y)
      cost += edgeCost(u.v[g.vIndex(x, y, l)], g.vcap[g.vIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost};
  }
  return best;
}

// Total cost of a candidate against live usage; charges it too if `commit`.
long long evalCandidate(const RoutingGrid &g, Usage &u, const LayerPlan &lp,
                         const Candidate &cand, int width, bool commit) {
  long long total = 0;
  int prevLayer = -1;
  std::vector<int> layerOf(cand.size());
  for (size_t i = 0; i < cand.size(); ++i) {
    const Leg &leg = cand[i];
    LayerChoice lc = leg.horizontal
                          ? bestHLayer(g, u, lp.hLayers, leg.cross, leg.a, leg.b, width)
                          : bestVLayer(g, u, lp.vLayers, leg.cross, leg.a, leg.b, width);
    total += lc.cost;
    if (prevLayer != -1 && prevLayer != lc.layer)
      total += 1;
    prevLayer = lc.layer;
    layerOf[i] = lc.layer;
  }
  if (commit) {
    for (size_t i = 0; i < cand.size(); ++i) {
      const Leg &leg = cand[i];
      int amount = width + g.minSpacing[layerOf[i]];
      if (leg.horizontal)
        for (int x = leg.a; x < leg.b; ++x)
          u.h[g.hIndex(x, leg.cross, layerOf[i])] += amount;
      else
        for (int y = leg.a; y < leg.b; ++y)
          u.v[g.vIndex(leg.cross, y, layerOf[i])] += amount;
    }
  }
  return total;
}

// Sequential, usage-aware, exactly like proute: each net sees the real
// charges left by every net before it. Tree topology and pin-tile
// deduplication are reused from the caller so this doesn't redo that work.
// Populates every net's segs/charges directly -- this pass's own result is a
// legitimate, output-able routing, not just a usage estimate to derive a
// price from. See routeAll below for why that distinction matters now.
Usage routeAllSeq(const RoutingGrid &g, const LayerPlan &lp, std::vector<RoutedNet> &nets) {
  Usage u(g);
  for (RoutedNet &rn : nets) {
    rn.segs.clear();
    rn.charges.clear();
    std::vector<int> nodeLayer(rn.pts.size(), -1);

    for (auto &e : rn.mst) {
      Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
      auto candidates = generateCandidates(p1, p2);

      size_t bestIdx = 0;
      long long bestCost = evalCandidate(g, u, lp, candidates[0], rn.net->minWidth, false);
      for (size_t i = 1; i < candidates.size(); ++i) {
        long long c = evalCandidate(g, u, lp, candidates[i], rn.net->minWidth, false);
        if (c < bestCost) {
          bestCost = c;
          bestIdx = i;
        }
      }

      // Re-derive the winning candidate's per-leg layers (evalCandidate
      // doesn't hand them back) and charge/emit for real this time.
      const Candidate &cand = candidates[bestIdx];
      int curLayer = -1, layerAtP1 = -1;
      for (size_t i = 0; i < cand.size(); ++i) {
        const Leg &leg = cand[i];
        LayerChoice lc = leg.horizontal
                              ? bestHLayer(g, u, lp.hLayers, leg.cross, leg.a, leg.b, rn.net->minWidth)
                              : bestVLayer(g, u, lp.vLayers, leg.cross, leg.a, leg.b, rn.net->minWidth);
        int l = lc.layer;
        if (curLayer != -1 && curLayer != l) {
          if (leg.horizontal)
            rn.segs.push_back({leg.entry, leg.cross, curLayer, leg.entry, leg.cross, l});
          else
            rn.segs.push_back({leg.cross, leg.entry, curLayer, leg.cross, leg.entry, l});
        }
        curLayer = l;
        if (i == 0)
          layerAtP1 = l;

        int amount = rn.net->minWidth + g.minSpacing[l];
        if (leg.horizontal) {
          for (int x = leg.a; x < leg.b; ++x)
            u.h[g.hIndex(x, leg.cross, l)] += amount;
          rn.segs.push_back({leg.a, leg.cross, l, leg.b, leg.cross, l});
        } else {
          for (int y = leg.a; y < leg.b; ++y)
            u.v[g.vIndex(leg.cross, y, l)] += amount;
          rn.segs.push_back({leg.cross, leg.a, l, leg.cross, leg.b, l});
        }
        rn.charges.push_back({leg.horizontal, leg.cross, leg.a, leg.b, l, amount});
      }
      reconcile(nodeLayer, e.first, layerAtP1, p1, rn.segs);
      reconcile(nodeLayer, e.second, curLayer, p2, rn.segs);
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
  return u;
}

} // namespace seed

struct RunReport {
  std::vector<RoutedNet> nets; // the best candidate seen, not necessarily the last
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1; // -1 means the seed pass itself won; see route()
  int roundsRun = 0;
  bool convergedToZero = false;
  bool stalled = false; // stopped early: no improvement, more rounds won't help
};

// Rounds with no improvement over the best seen before giving up. This is
// this standalone tool's version of "if the routability check fails badly
// enough, the right answer is a return to placement, not another pricing
// round" -- there is no placement stage here to hand back to, so the honest
// move is to stop and say so rather than spend the rest of the round budget
// on a price vector that has stopped teaching the router anything.
constexpr int kDefaultPatience = 5;

RunReport route(const Design &d, int maxRounds, double epsilon, bool useSeed = true) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  LayerPlan lp(g);
  int kPatience = kDefaultPatience;
  if (const char *p = std::getenv("GROUTE_PATIENCE"); p)
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

  // Seed prices from a cheap sequential pass before round 0 starts, per
  // CLAUDE.md's "seed prices from a demand estimate" -- see the seed
  // namespace above for why this matters now, not just once stage 7 adds
  // real parallelism. This pass also gives every net (including giant ones,
  // which are never touched again) its first real route.
  //
  // That route is also, itself, a legitimate candidate answer -- and, in
  // practice, usually a good one: a single price scalar per edge is a much
  // weaker signal than the live usage the seed pass solves against directly,
  // so a batch round derived from it can come out *worse* than the seed
  // pass alone (measured on adaptec1: seed 484K overflow, round 0 from its
  // price 2.06M). That is not a bug, it is the normal cost of decentralizing
  // the decision -- the theory says more rounds should claw it back, but
  // there is no reason to make the loop's floor worse than its own
  // starting point while that tuning is still open. So the seed's routing
  // is recorded as the round -1 candidate the batch loop has to beat, the
  // same way every later round is judged.
  //
  // `useSeed=false` exists only so the self test can show the stampede
  // seeding prevents; production callers always want it on.
  if (useSeed) {
    Usage seedUsage = seed::routeAllSeq(g, lp, nets);
    rep.bestOverflow = totalOverflow(g, seedUsage);
    rep.bestRound = -1;
    rep.nets = nets;
    if (std::getenv("GROUTE_TRACE"))
      std::cerr << "seed pass overflow " << rep.bestOverflow << "\n";
    updatePrices(g, prices, seedUsage, epsilon);
  } else {
    for (RoutedNet &rn : nets)
      if (rn.giant)
        routeNet(g, prices, lp, rn);
  }

  // The seed pass may already be perfect; don't spend a batch round finding
  // that out.
  if (rep.bestOverflow == 0) {
    rep.convergedToZero = true;
    return rep;
  }

  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxRounds; ++round) {
    for (RoutedNet &rn : nets)
      if (!rn.giant)
        routeNet(g, prices, lp, rn);

    Usage u = accumulate(g, nets);
    long long overflow = totalOverflow(g, u);
    rep.roundsRun = round + 1;
    if (const char *dbg = std::getenv("GROUTE_TRACE"); dbg)
      std::cerr << "round " << round << " overflow " << overflow << "\n";

    bool improved = overflow < rep.bestOverflow;
    if (improved) {
      rep.bestOverflow = overflow;
      rep.bestRound = round;
      rep.nets = nets; // deep copy; cheap relative to a round's routing cost
      roundsSinceImprovement = 0;
    } else {
      ++roundsSinceImprovement;
      // Price the next round from the best usage seen, not this regression's.
      // Feeding a price update from a state we're about to discard taught
      // round r+1 to react to a mistake instead of to real congestion; this
      // alone turned wild oscillation (2.06M -> 3.05M -> plateau ~2.2-2.4M)
      // into steady, monotonic-ish improvement on adaptec1 (measured: still
      // trending down at round 150, ~1.91M). It is not, on its own, enough
      // to close the gap to the seed pass's 484K within any round budget
      // that finished in reasonable time -- see the seed-vs-batch comment
      // above route() for what that gap is actually telling us.
      u = accumulate(g, rep.nets);
    }

    // Checked against the tracked best, not this round's raw value: a round
    // that regresses after an earlier one (or the seed) already hit zero
    // should still report convergence, not stall out waiting for patience
    // to expire on a worse number.
    if (rep.bestOverflow == 0) {
      rep.convergedToZero = true;
      break;
    }
    if (roundsSinceImprovement >= kPatience) {
      rep.stalled = true;
      break;
    }
    updatePrices(g, prices, u, epsilon);
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

// Same basic-shapes fixture proute used, to catch a regression in the
// shared commit/reconcile logic the same way it would have shown up there.
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
  Design d = parse("groute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design");
  check(rep.convergedToZero, "reports convergence, not a stall");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs), "netE (high-to-low bend) is one component");
}

// Three nets whose cheapest candidate, under flat initial prices, all cross
// the *same* single unit edge -- capacity 1. Each net's own alternate
// candidate is on a private row/column nothing else touches, and is exactly
// as long as the shared one, so nothing prefers it until the shared edge's
// price has actually risen. This is the exact stampede the seed pass exists
// to prevent: with a flat start (useSeed=false) all three collide in round 0
// identically, since nothing has solved yet to tell them apart. With the
// seed pass on (the default), its sequential pre-pass sees net 1 grab the
// shared edge and routes nets 2 and 3 around it immediately -- the same way
// proute's live rip-up would -- so the price feeding round 0 already
// reflects the collision before any batch round runs at all.
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

void testSeedingPreventsStampede() {
  Design d = parse("groute_selftest_stampede.gr", kStampedeGr);

  RunReport unseeded = route(d, 1, 0.5, /*useSeed=*/false);
  check(unseeded.bestOverflow > 0,
        "without seeding, a flat start makes all three nets collide in round 0");

  RunReport seeded = route(d, 1, 0.5, /*useSeed=*/true);
  check(seeded.bestOverflow == 0,
        "with seeding, round 0 already starts from prices reflecting the "
        "collision the seed pass found and avoided -- no batch round needed");
  check(seeded.convergedToZero, "and it is reported as convergence");
  for (const RoutedNet &rn : seeded.nets)
    check(segsConnected(rn.segs), "net " + rn.net->name + " is one component");
}

int run() {
  std::cout << "self test\n";
  testBasicShapes();
  testSeedingPreventsStampede();
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
  double epsilon = 0.5;
  if (const char *e = std::getenv("GROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("GROUTE_ROUNDS"); r)
    maxRounds = std::atoi(r);

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, maxRounds, epsilon);
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " nets\n";
  std::cerr << "best overflow: " << rep.bestOverflow << " (";
  if (rep.bestRound < 0)
    std::cerr << "the seed pass, before any batch round";
  else
    std::cerr << "round " << rep.bestRound;
  std::cerr << ", of " << rep.roundsRun << " batch round(s) run)\n";
  std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                     : rep.stalled ? "stalled: no improvement for "
                                                       "several rounds, stopping\n"
                                                   : "hit the round cap\n");
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
