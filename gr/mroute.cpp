// mroute.cpp
//
// Stage 5: a genuine 3D layered graph. Every prior router here (lroute,
// proute, groute, nproute) picks among a small menu of L/Z pattern shapes
// per tree edge, then locally chooses "whichever layer running in that
// direction has the most room" for each straight run -- a layer, once
// picked for a run, is fixed for that whole run. This router instead runs
// bounded Dijkstra over a graph where horizontal edges, vertical edges, and
// vias are all first-class priced edges, so a route can change layers
// mid-path to dodge congestion in a way no pattern-candidate router here
// ever could.
//
// Built in three steps (see the plan this was built from,
// async-shimmying-pancake.md, or the git history for the numbers at each
// step):
//   1. Bounded multi-source/multi-target Dijkstra over H/V/via edges, vias
//      free (matching every prior router) -- proves the graph/search/
//      reconcile integration before adding new pricing dimensions.
//   2. Via pricing, using a capacity derived from the .gr file's own "via
//      spacing" field (see viaCapacityAt's comment).
//   3. Layer-range pre-assignment (short nets restricted to lower layers,
//      long nets get the whole stack), per CLAUDE.md's design note that
//      most layer conflict disappears by construction this way.
//
// Everything else is carried over from nproute.cpp, the strongest prior
// architecture: MST decomposition, sequential per-net negotiation with
// immediate price updates after each commit, the tuned early-warning
// pricing curve, reconcile() for branch points and pin-declared layers, and
// the giant-net exemption (routed once, cheaply, without a full search --
// see routeGiantNet).
//
// Step 1 result, total overflow / scored wirelength, against the existing
// table (proute 221352/6111546, groute 484580/6076308, nproute
// 227468/6871155 on adaptec1; see nproute.cpp for the other two
// benchmarks' numbers):
//
//   benchmark   mroute (step 1)
//   adaptec1    218650 / 6837420   -- beats proute's overflow outright
//   newblue1     94060 / 6079307   -- worse than proute (58584) and nproute
//   bigblue1    346494 / 7008322   -- worse than proute (309822) and nproute
//
// Mixed, honestly: this is the first case in the whole project where a
// router wins on one benchmark and clearly loses on the other two, rather
// than winning or losing everywhere -- a real maze search finds routes a
// pattern menu structurally can't on a congested-in-a-particular-way design
// like adaptec1, but the pattern-menu routers' extensive prior tuning
// (the early-warning curve, selective rip-up, the cleanup pass) still wins
// where the congestion is more diffuse. Runtime is the real cost: 95-355s
// per benchmark here against nproute's 5-22s, almost entirely the cost of
// a genuine per-tree-edge shortest-path search instead of pricing ~8 fixed
// candidates. See routeSegment's comment for the margin-cap and A* fixes
// that already brought this down from an initial 105s for a *single round*
// on adaptec1 alone.
//
// Also caught here: an early version of routeGiantNet emitted segments but
// never charged usage, silently hiding giant nets' real capacity
// consumption from every other net's pricing decisions. Found by a
// discrepancy between this router's own tracked overflow and gr_verify's
// independent count (214920 vs 219288 on adaptec1) -- fixed, and the two
// now agree exactly on all three benchmarks.
//
// Step 2 result, total overflow / scored wirelength:
//
//   benchmark   mroute (step 1)      mroute (step 2, via pricing)
//   adaptec1    218650 / 6837420     216792 / 6670426   -- both improve
//   newblue1     94060 / 6079307      94814 / 6134955   -- slightly worse
//   bigblue1    346494 / 7008322     345480 / 7010610   -- slightly better
//
// Close to a wash on wire overflow, as expected: via congestion was never
// the bottleneck on these designs (derived via capacity is 35 per site on
// adaptec1, against per-layer wire capacities as low as 6), so the pricing
// signal rarely fires. The point of this step was making the router
// via-congestion-aware for designs where it would matter, not chasing
// overflow here -- see testViaPricingAvoidsSharedSite for proof the
// mechanism itself works on a design where via capacity is the bottleneck.
//
// Caught here: the first version priced a via as edgeCost(lambda, 1), i.e.
// 1.0 + lambda*1, reusing wire's edge-cost formula. With lambda seeded at
// 1.0 (the standard multiplicative-weights start), that made an
// *uncongested* via cost 2.0 -- double step 1's flat 1.0, and no longer
// matching the "+1 per via" scoring convention a via is supposed to mirror.
// It regressed adaptec1 to 229558, and crucially the regression barely
// moved (229558 -> 228994) when the congestion-response term was
// neutralized via an extreme threshold -- proof the damage was from the
// baseline cost shift, not from the pricing mechanism responding to real
// congestion. Fixed by giving vias their own cost function, lambda*amount
// with no added length term, so lambda=1.0 reproduces step 1's flat cost
// exactly and only rises above it once congestion appears. See
// viaEdgeCost's comment.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o mroute mroute.cpp
//
// Use:
//     ./mroute benchmark.gr solution.route
//     ./mroute --selftest

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
// Prices and usage -- same shape as nproute's, both persist across the
// whole run.
// ---------------------------------------------------------------------------

// Via sites, per adjacent layer pair (l, l+1), indexed by the *lower*
// layer: nLayers-1 planes of nx*ny tiles each. Dense, same convention as
// gr_io.h's own hIndex/vIndex.
size_t viaIndex(const RoutingGrid &g, int x, int y, int lowerLayer) {
  return (static_cast<size_t>(lowerLayer) * g.ny + y) * g.nx + x;
}
size_t viaCount(const RoutingGrid &g) {
  return static_cast<size_t>(std::max(g.nLayers - 1, 0)) * g.ny * g.nx;
}

// The .gr format has no via capacity field -- derived from its "via
// spacing" field instead. A via consumes a footprint; treating a tile as a
// square region, roughly min(tileW,tileH)/spacing via bodies fit across it,
// the same kind of 1D cross-section approximation the format itself uses
// for wire capacity (a track count, not a real 2D layout). The stricter of
// the two layers' spacing rules governs, since a via has to satisfy both.
// Capacity is uniform across every (x,y) for a given layer pair -- spacing
// and tile size are both grid-wide constants, not per-tile -- so this is
// one value per layer pair, not a dense per-tile array; only *usage* needs
// to vary by tile.
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

struct Prices {
  std::vector<double> h, v, via;
  explicit Prices(const RoutingGrid &g)
      : h(g.hCount(), 1.0), v(g.vCount(), 1.0), via(viaCount(g), 1.0) {}
};
struct Usage {
  std::vector<int> h, v, via;
  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0), via(viaCount(g), 0) {}
};

// Same curve shape for both wires and vias -- flat below the threshold,
// escalating faster than linearly as usage approaches and passes capacity
// (see nproute.cpp for the measured history of why this shape won out for
// wires) -- but with independently tunable constants, since there is no
// reason the right threshold/power for via congestion has to match wires'
// tuned 0.2/2.5; that needs its own empirical check, which is exactly what
// separate environment variables are for.
double priceExponentTerm(double usage, double cap, double threshold, double power) {
  if (threshold >= 1.0)
    return std::max(0.0, usage / cap - 1.0);
  double ratio = usage / cap;
  double r = std::max(0.0, (ratio - threshold) / (1.0 - threshold));
  return std::pow(r, power);
}
double wireExponentTerm(double usage, double cap) {
  static const double threshold = [] {
    const char *t = std::getenv("MROUTE_THRESHOLD");
    return t ? std::atof(t) : 0.2;
  }();
  static const double power = [] {
    const char *p = std::getenv("MROUTE_POWER");
    return p ? std::atof(p) : 2.5;
  }();
  return priceExponentTerm(usage, cap, threshold, power);
}
double viaExponentTerm(double usage, double cap) {
  static const double threshold = [] {
    const char *t = std::getenv("MROUTE_VIA_THRESHOLD");
    return t ? std::atof(t) : 0.2;
  }();
  static const double power = [] {
    const char *p = std::getenv("MROUTE_VIA_POWER");
    return p ? std::atof(p) : 2.5;
  }();
  return priceExponentTerm(usage, cap, threshold, power);
}

double edgeCost(double lambda, int amount) { return 1.0 + lambda * amount; }

// Vias don't get wire's separate "+1.0 length" term added on top of the
// price: a via's own base/scoring cost of 1 tile-equivalent (see the "+1 per
// via" scoring rule) IS the lambda_via baseline of 1.0, not an addition to
// it. Reusing edgeCost() here would silently double an uncongested via's
// cost relative to step 1's flat 1.0 -- measured to regress adaptec1's wire
// overflow by ~5% (229558 vs 218650) even with the congestion-response term
// neutralized, since it changes vias' relative attractiveness as a
// congestion escape valve everywhere, not just at hot via sites.
double viaEdgeCost(double lambda, int amount) { return lambda * amount; }

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

// ---------------------------------------------------------------------------
// MST over a net's distinct pin tiles -- identical to every prior router.
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
// The graph and the search
// ---------------------------------------------------------------------------

struct Seg {
  int x1, y1, l1, x2, y2, l2;
};
struct ChargeRecord {
  bool horizontal;
  int cross, a, b, layer, amount;
};

// A bounded local search region: [x0,x0+w) x [y0,y0+h) x all layers. Dense
// scratch arrays sized to the region, not the whole grid -- most nets have
// small spans, so this stays cheap even though the grid itself is large.
// A bounded local search region, reused across every Dijkstra call instead
// of allocated fresh each time. With ~300K+ tree-edge searches on a real
// benchmark, allocating and zeroing dist/parent vectors per call turned out
// to dominate the runtime (measured: 101s for a single round on adaptec1,
// against nproute's ~20s for its *entire* run including 30 rounds and a
// cleanup pass). Fixed with the standard trick for this: a generation
// counter instead of clearing. A cell's dist/parent are only meaningful if
// its stamp matches the current call's generation; touching a cell for the
// first time in a call stamps it, so "reset between calls" is an O(1)
// counter bump instead of an O(region size) clear, and the underlying
// vectors only grow (never shrink) to fit the largest region seen so far.
struct SearchRegion {
  int x0 = 0, y0 = 0, w = 0, h = 0, nLayers = 0;
  std::vector<double> dist;
  std::vector<int> parent; // local index of predecessor, -1 if a source
  std::vector<int> stamp;
  int generation = 0;

  // Re-points the region at new geometry, growing the scratch arrays only
  // if this region is bigger than any seen before, and bumping the
  // generation so every cell reads back as "untouched" without being
  // cleared.
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

// Bounded multi-source, multi-target Dijkstra between p1 and p2. If p1Layer
// is >= 0, only that layer is a valid source (p1 already has an established
// layer from an earlier tree edge in this net); otherwise every layer at
// p1's tile is a free source, matching every prior router's "unconstrained
// first touch" convention. Any layer at p2's tile is an acceptable target --
// reconcile() is still run by the caller afterward regardless, as a cheap
// safety net for whichever layer this search happens to land on.
//
// Vias have no price yet (step 1) -- see negotiateVia's comment for why
// that's a deliberate, temporary simplification, not an oversight.
// Diagnostic counters, printed at the end of main() when MROUTE_STATS is
// set -- used to find where Dijkstra's time actually goes rather than
// guessing.
struct { long long calls = 0, pops = 0, regionCells = 0, maxRegion = 0; } gStats;

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

  ++gStats.calls;
  {
    long long cells = static_cast<long long>(region.w) * region.h * region.nLayers;
    gStats.regionCells += cells;
    gStats.maxRegion = std::max(gStats.maxRegion, cells);
  }

  // A*, not plain Dijkstra: the queue orders by dist + a Manhattan-distance
  // heuristic to p2 (ignoring layer). Every edge -- H, V, or via -- costs at
  // least 1 (the base wirelength-unit cost every router here uses, before
  // any price term), so Manhattan distance is a lower bound on remaining
  // cost and the heuristic is admissible: this finds the same shortest path
  // Dijkstra would, it just explores less of the region to get there,
  // pulling the search toward the target instead of expanding uniformly in
  // every direction. Matters more the longer a net's span is, which is
  // exactly where the bounding box is unavoidably large regardless of
  // margin (see routeSegment's comment on the margin cap).
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
    ++gStats.pops;
    double d = top.dist;
    int i = top.index;
    if (d > region.distAt(i))
      continue; // stale entry
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
        double w = edgeCost(prices.h[e], amount);
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
        double w = edgeCost(prices.v[e], amount);
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
      int lowerLayer = std::min(l, nl);
      double w = viaEdgeCost(prices.via[viaIndex(g, x, y, lowerLayer)], 1);
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

// Doubles the margin and retries if the initial search fails to connect.
// Should not normally trigger -- the grid is fully connected within any
// margin >= 1 -- but it's a cheap safety net rather than a load-bearing
// assumption about every possible layer/capacity configuration.
//
// The margin is capped at a small absolute constant, not scaled with span:
// detour room needed to route around *local* congestion near the direct
// path doesn't grow with how far apart the two points are. Scaling it with
// span (the first version used span/2, uncapped) meant a net spanning much
// of the chip searched a region approaching the *entire grid* -- measured
// directly on adaptec1: one real call needed a 629856-cell region (the
// whole grid, 324x324x6) out of an average of 4184, and a small number of
// calls that large, at O(log region size) per priority-queue operation,
// were enough to make 564764 total calls take 1.61 *billion* pops and
// nearly 105 seconds for a single round. Capping the margin at 20 already
// cut total pops by ~2.9x; adding the A* heuristic below (see
// boundedDijkstra) cut them further; then a direct sweep of the cap itself
// (2 through 20) found runtime scaling smoothly with it while overflow
// stayed within noise (458-461K total, no trend) across the whole range --
// so there was still headroom being spent on search breadth that wasn't
// buying route quality. Settled on 6: a single round on adaptec1 went from
// 105s (uncapped) to 29s (capped at 20) to 20s (capped at 6), for
// statistically the same result. The retry loop below still exists for the
// genuine edge case where this margin doesn't connect, just starts and
// grows from a much saner base than span/2 ever was.
DijkstraResult routeSegment(const RoutingGrid &g, const Prices &prices, const LayerPlan &lp,
                             SearchRegion &region, Point p1, int p1Layer, Point p2, int width) {
  static const int maxMargin = [] {
    const char *m = std::getenv("MROUTE_MAX_MARGIN");
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

// ---------------------------------------------------------------------------
// Committing a path: emit segments/charges, negotiate prices immediately.
// ---------------------------------------------------------------------------

void undoCharge(const RoutingGrid &g, Usage &u, const ChargeRecord &c) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x)
      u.h[g.hIndex(x, c.cross, c.layer)] -= c.amount;
  } else {
    for (int y = c.a; y < c.b; ++y)
      u.v[g.vIndex(c.cross, y, c.layer)] -= c.amount;
  }
}

void negotiateWire(const RoutingGrid &g, Prices &pr, const Usage &u, const ChargeRecord &c,
                    double epsilon) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x) {
      size_t i = g.hIndex(x, c.cross, c.layer);
      if (g.hcap[i] <= 0)
        pr.h[i] = kForbiddenPrice;
      else
        pr.h[i] *= std::exp(epsilon * wireExponentTerm(u.h[i], g.hcap[i]));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] *= std::exp(epsilon * wireExponentTerm(u.v[i], g.vcap[i]));
    }
  }
}

// A via site's capacity, per the derived model above, can never be zero
// (viaCapacityPerLayerPair floors at 1), so there is no forbidden-price
// case to guard here the way wire edges need one.
void negotiateVia(const RoutingGrid &g, const std::vector<int> &viaCap, Prices &pr,
                   const Usage &u, int x, int y, int lowerLayer, double epsilon) {
  size_t i = viaIndex(g, x, y, lowerLayer);
  pr.via[i] *= std::exp(epsilon * viaExponentTerm(u.via[i], viaCap[lowerLayer]));
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
// Routing one net
// ---------------------------------------------------------------------------

// A single via crossing at (x,y) between layer lowerLayer and lowerLayer+1.
// Kept separate from ChargeRecord (which describes a wire *range*) rather
// than folding a via in as a degenerate zero-length range -- a via is a
// point event with its own capacity model, not a wire run.
struct ViaCharge {
  int x, y, lowerLayer;
};

struct RoutedNet {
  const Net *net = nullptr;
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> mst;
  std::vector<Seg> segs;
  std::vector<ChargeRecord> charges;
  std::vector<ViaCharge> viaCharges;
  bool giant = false;
};

void undoViaCharge(const RoutingGrid &g, Usage &u, const ViaCharge &vc) {
  u.via[viaIndex(g, vc.x, vc.y, vc.lowerLayer)] -= 1;
}

// Charges and emits one already-found path (a sequence of PathNodes),
// negotiating each wire edge's price immediately after charging it, exactly
// as nproute does per candidate leg. Runs of consecutive same-layer,
// same-direction steps are merged into one segment rather than one per
// tile, matching every prior router's output convention. Vias get the same
// immediate-negotiation treatment (see negotiateVia) using the derived
// per-layer-pair capacity in viaCap.
void commitPath(const RoutingGrid &g, Prices &prices, Usage &usage, const std::vector<int> &viaCap,
                double epsilon, int width, const std::vector<PathNode> &path,
                std::vector<Seg> &segs, std::vector<ChargeRecord> &charges,
                std::vector<ViaCharge> &viaCharges) {
  size_t i = 0;
  while (i + 1 < path.size()) {
    const PathNode &a = path[i];
    const PathNode &b = path[i + 1];
    if (a.l != b.l) {
      // Via: exactly one step, no run to extend (layer changes are never
      // batched together as multi-layer jumps by this search).
      int lowerLayer = std::min(a.l, b.l);
      usage.via[viaIndex(g, a.x, a.y, lowerLayer)] += 1;
      viaCharges.push_back({a.x, a.y, lowerLayer});
      negotiateVia(g, viaCap, prices, usage, a.x, a.y, lowerLayer, epsilon);
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
    negotiateWire(g, prices, usage, c, epsilon);
    segs.push_back({horizontal ? lo : a.x, horizontal ? a.y : lo, l, horizontal ? hi : a.x,
                     horizontal ? a.y : hi, l});
    i = j;
  }
}

void routeNet(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
              const std::vector<int> &viaCap, SearchRegion &region, double epsilon,
              RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  for (const ViaCharge &vc : rn.viaCharges)
    undoViaCharge(g, usage, vc);
  rn.segs.clear();
  rn.charges.clear();
  rn.viaCharges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    int p1Layer = nodeLayer[e.first];
    DijkstraResult r = routeSegment(g, prices, lp, region, p1, p1Layer, p2, rn.net->minWidth);
    if (!r.found)
      fail("mroute: no path found between tree points even after margin expansion -- "
           "this should not happen on a fully connected grid");

    commitPath(g, prices, usage, viaCap, epsilon, rn.net->minWidth, r.path, rn.segs, rn.charges,
               rn.viaCharges);
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

// Giant nets (>1000 pins) are exempt from the ISPD overflow/connectivity
// checks and routed once, never revisited (see every prior router's
// identical exemption). Unlike them, bounded Dijkstra is real per-edge work,
// and a giant net's tree can span much of the chip -- so giant nets get a
// plain, unpriced L-shape here instead of a search, the same way lroute
// routed everything. Cheap and not competitive, but their usage is charged
// exactly like any other net's: giant nets are exempt from being *checked*
// for legality, not from *consuming real capacity* that affects everyone
// else's congestion. An earlier version of this function skipped charging
// entirely along with skipping the search, silently hiding a large fraction
// of real usage from every other net's pricing decisions -- caught by a
// discrepancy between this router's own tracked overflow and gr_verify's:
// 214920 internally vs. 219288 actually, on adaptec1. The two now agree.
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
      if (xa != xb) {
        // via at the bend -- usage charged (so other nets see it as real
        // congestion, same as the wire-usage fix above), but never priced:
        // routeGiantNet has no access to a mutable Prices, on purpose, per
        // the same exemption every prior router gives giant nets (no
        // search, no negotiation, just a cheap fixed shape).
        usage.via[viaIndex(g, p2.x, p1.y, std::min(hl, vl))] += 1;
        rn.viaCharges.push_back({p2.x, p1.y, std::min(hl, vl)});
        rn.segs.push_back({p2.x, p1.y, hl, p2.x, p1.y, vl});
      }
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
// The negotiation loop -- structurally identical to nproute's.
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

struct RunReport {
  std::vector<RoutedNet> nets;
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1;
  int roundsRun = 0;
  bool convergedToZero = false;
  bool stalled = false;
};

// Much lower than nproute's 20: measured on adaptec1, overflow rises then
// falls in a quick arc (best at round 8, from a round-0 start of 458K),
// then climbs *monotonically* for the next 20 rounds without ever coming
// back down -- unlike nproute's slow-and-steady improvement curve, which
// justified a large patience window. Real Dijkstra always finds the
// currently-cheapest path exactly, with no small fixed candidate menu to
// anchor a net to a stable choice once found, so a rerouted net can drift
// to a new equally-priced alternative every round without ever settling.
// "Best" is still tracked and returned regardless of patience, so a smaller
// window changes nothing about routing quality -- it only stops paying for
// rounds that have already stopped helping. Measured cost of the difference
// on adaptec1: patience 20 ran to round 28 and took 653s; the same 214920
// best is found well before that.
constexpr int kDefaultPatience = 5;

RunReport route(const Design &d, int maxRounds, double epsilon) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Prices prices(g);
  Usage usage(g);
  LayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  SearchRegion region; // one reusable scratch buffer for every Dijkstra call
  int kPatience = kDefaultPatience;
  if (const char *p = std::getenv("MROUTE_PATIENCE"); p)
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

  for (RoutedNet &rn : nets)
    if (rn.giant)
      routeGiantNet(g, usage, lp, rn);

  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxRounds; ++round) {
    for (RoutedNet &rn : nets) {
      if (rn.giant)
        continue;
      if (round > 0 && !netOverflowed(g, usage, rn))
        continue;
      routeNet(g, prices, usage, lp, viaCap, region, epsilon, rn);
    }

    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    if (std::getenv("MROUTE_TRACE"))
      std::cerr << "round " << round << " overflow " << overflow << "\n";

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
  Design d = parse("mroute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design");
  check(rep.convergedToZero, "reports convergence, not a stall");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs), "netE (high-to-low bend) is one component");
}

// The key new capability this stage adds: a route whose only non-overflowing
// path requires changing layers *mid-route*, not just at the ends. Grid has
// two horizontal-capable layers (1 and 3) and one vertical (layer 2) in the
// middle, forcing any 2-pin horizontal net to cross through it. Layer 1's
// capacity is fully blocked over [2,5); layer 3 is open there. A
// pattern-candidate router (every prior one here) commits to a single layer
// for a whole straight run and could never produce this path -- it would
// have to route the entire span on layer 3 (or layer 1 and overflow), never
// "layer 1 up to the blockage, hop to layer 3, back down to layer 1 after."
// Real maze search can splice layers together mid-run; this test only
// requires it find *some* legal path when the direct one is blocked outright,
// which is a lower bar but already something no prior router could do this
// way (they route by whole-run layer choice, not tile by tile).
const char *kMidRouteHopGr = R"(grid 9 4 3
vertical capacity 0 4 0
horizontal capacity 4 0 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netHop 0 2 1
5 5 1
75 5 1
2
2 0 1 3 0 1 0
3 0 1 4 0 1 0
)";

void testMidRouteLayerHop() {
  Design d = parse("mroute_selftest_hop.gr", kMidRouteHopGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.nets.size() == 1, "netHop produced a route");
  check(rep.bestOverflow == 0,
        "finds a legal path even though layer 1 is blocked mid-span and the "
        "direct route must hop layers to get around it");
  check(segsConnected(rep.nets[0].segs), "netHop is one connected component");
}

bool hasViaAt(const std::vector<Seg> &segs, int x, int y) {
  for (const Seg &s : segs)
    if (s.x1 == x && s.y1 == y && s.x2 == x && s.y2 == y && s.l1 != s.l2)
      return true;
  return false;
}

// Step 2's own version of nproute's stampede test: two nets with identical
// L-shaped geometry (pin 1 needs layer 1, pin 2 needs layer 2, forcing
// exactly one bend and one via -- not one a free pin-layer reconciliation
// via could absorb, since both pins already declare the layer their own
// straight run naturally lands on). Via spacing is set so every site's
// derived capacity is exactly 1. Net 1 (routed first) takes the "obvious"
// HV bend, via-ing at the corner (2,0). Net 2, if via pricing did nothing,
// would want the identical path and collide there. With it working, net 2's
// mirror-image VH candidate -- via-ing at (0,2), untouched -- becomes
// cheaper once (2,0)'s price has risen, and it takes that one instead.
const char *kViaStampedeGr = R"(grid 5 5 2
vertical capacity 0 4
horizontal capacity 4 0
minimum width 1 1
minimum spacing 0 0
via spacing 10 10
0 0 10 10
num net 2
netViaA 0 2 1
5 5 1
25 25 2
netViaB 1 2 1
5 5 1
25 25 2
0
)";

void testViaPricingAvoidsSharedSite() {
  Design d = parse("mroute_selftest_via.gr", kViaStampedeGr);
  RunReport rep = route(d, 5, 0.5);
  check(rep.nets.size() == 2, "both via-stampede nets produced a route");
  check(rep.bestOverflow == 0, "no wire overflow -- this test is entirely about via pricing");
  check(segsConnected(rep.nets[0].segs), "netViaA is one connected component");
  check(segsConnected(rep.nets[1].segs), "netViaB is one connected component");
  check(hasViaAt(rep.nets[0].segs, 2, 0),
        "netViaA (routed first, sees an untouched grid) takes the obvious "
        "HV bend, via-ing at the corner");
  check(!hasViaAt(rep.nets[1].segs, 2, 0) && hasViaAt(rep.nets[1].segs, 0, 2),
        "netViaB avoids netViaA's now-priced via site and takes the "
        "mirror-image VH bend instead, via-ing at the opposite corner");
}

int run() {
  std::cout << "self test\n";
  testBasicShapes();
  testMidRouteLayerHop();
  testViaPricingAvoidsSharedSite();
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
  double epsilon = 0.02;
  if (const char *e = std::getenv("MROUTE_EPS"); e)
    epsilon = std::atof(e);
  if (const char *r = std::getenv("MROUTE_ROUNDS"); r)
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
  if (std::getenv("MROUTE_STATS"))
    std::cerr << "dijkstra: calls=" << gStats.calls << " pops=" << gStats.pops
              << " avgRegionCells=" << (gStats.calls ? gStats.regionCells / gStats.calls : 0)
              << " maxRegionCells=" << gStats.maxRegion << "\n";
  return 0;
}
