// lroute.cpp
//
// A cheap global router for the ISPD .gr benchmark format: pattern routing
// with single-bend (L-shaped) segments, no maze search, no rip-up-and-reroute.
// It exists to produce a legal, reasonably congestion-aware baseline solution
// fast, against which a real router (maze routing, negotiated congestion) can
// be compared.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o lroute lroute.cpp
//
// Use:
//     ./lroute benchmark.gr solution.route
//     ./lroute --selftest
//
// Algorithm, per net:
//   1. Distinct pin tiles are collected (duplicate-tile pins collapse to one).
//      A net with one distinct tile needs no wire.
//   2. Remaining tiles are connected by a minimum spanning tree over Manhattan
//      distance (Prim's -- nets are small, O(p^2) is fine).
//   3. Each tree edge is routed as one of two L shapes (horizontal-then-
//      vertical or vertical-then-horizontal). Whichever bend crosses less
//      congested edges, by current usage against capacity, is taken. This is
//      the only congestion awareness in the router: a greedy per-edge choice,
//      not global negotiation.
//   4. Each straight run within an L is assigned to whichever layer running
//      in that direction (the .gr format dedicates each layer to horizontal
//      or vertical routing) has the most remaining capacity along the run.
//   5. A tree node touched by more than one edge can land on a different
//      layer for each edge, since every edge picks its own best layer
//      independently. The verifier's connectivity check treats (x,y,layer)
//      as a distinct graph node, so that would silently split the net. Each
//      node's landing layer is recorded the first time an edge reaches it;
//      any later edge landing on a different layer gets an explicit
//      reconciling via there. Leaf nodes (touched once) never pay for this;
//      only branch points do.
//   6. The tree never otherwise touches a pin's own declared layer -- each
//      run just takes whichever layer has capacity in that direction. After
//      the tree is built, every pin gets the same reconciling-via treatment
//      as a branch point, down to its declared layer. Required: both the
//      verifier and the official ISPD scorer check that the route reaches
//      each pin's exact layer, not just its tile.
//   7. Usage is charged immediately, so later nets see earlier ones -- the
//      router is greedy and sequential, never revisits a decision.

#include <algorithm>
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

// ---------------------------------------------------------------------------
// Capacity usage tracked while routing, charged as nets are placed.
// ---------------------------------------------------------------------------

struct Usage {
  std::vector<int> h, v; // parallel to grid.hcap / grid.vcap

  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

int hSlack(const RoutingGrid &g, const Usage &u, int x, int y, int l) {
  size_t e = g.hIndex(x, y, l);
  return g.hcap[e] - u.h[e];
}
int vSlack(const RoutingGrid &g, const Usage &u, int x, int y, int l) {
  size_t e = g.vIndex(x, y, l);
  return g.vcap[e] - u.v[e];
}

// ---------------------------------------------------------------------------
// Layer direction: the .gr format gives each layer a horizontal or a vertical
// capacity, never both meaningfully (one of the two is typically 0).
// ---------------------------------------------------------------------------

struct LayerPlan {
  std::vector<int> hLayers, vLayers; // layer indices usable in each direction

  explicit LayerPlan(const RoutingGrid &g) {
    for (int l = 0; l < g.nLayers; ++l) {
      if (g.hCapLayer[l] > 0)
        hLayers.push_back(l);
      if (g.vCapLayer[l] > 0)
        vLayers.push_back(l);
    }
    // Degenerate benchmarks may dedicate no layer to a direction the router
    // still needs. Fall back to every layer rather than producing an
    // unroutable net.
    if (hLayers.empty())
      for (int l = 0; l < g.nLayers; ++l)
        hLayers.push_back(l);
    if (vLayers.empty())
      for (int l = 0; l < g.nLayers; ++l)
        vLayers.push_back(l);
  }
};

// Pick the layer in `layers` with the most slack over the run [a,b) at row/col
// `cross`. Ties favor the lowest layer index, which keeps solutions
// deterministic.
int bestHLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
               int y, int a, int b) {
  int best = layers.front();
  int bestSlack = std::numeric_limits<int>::min();
  for (int l : layers) {
    int worst = std::numeric_limits<int>::max();
    for (int x = a; x < b; ++x)
      worst = std::min(worst, hSlack(g, u, x, y, l));
    if (worst > bestSlack) {
      bestSlack = worst;
      best = l;
    }
  }
  return best;
}
int bestVLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
               int x, int a, int b) {
  int best = layers.front();
  int bestSlack = std::numeric_limits<int>::min();
  for (int l : layers) {
    int worst = std::numeric_limits<int>::max();
    for (int y = a; y < b; ++y)
      worst = std::min(worst, vSlack(g, u, x, y, l));
    if (worst > bestSlack) {
      bestSlack = worst;
      best = l;
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Minimum spanning tree over a net's distinct pin tiles.
// ---------------------------------------------------------------------------

struct Point {
  int x, y;
};

// Prim's algorithm, Manhattan distance. Nets in these benchmarks rarely
// exceed a few dozen pins, so O(p^2) needs no supporting data structure. Each
// non-root point appears as the second element of exactly one returned edge
// (the edge that first connects it to the tree); the first element can
// repeat for a branch point.
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
// Routing a single tree edge as an L
// ---------------------------------------------------------------------------

// One output segment, in tile coordinates with zero based layers. Converted
// to real coordinates / one based layers only when written out.
struct Seg {
  int x1, y1, l1, x2, y2, l2;
};

long long hRunCost(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                    int y, int a, int b) {
  if (a == b)
    return 0;
  int l = bestHLayer(g, u, layers, y, a, b);
  int worst = std::numeric_limits<int>::max();
  for (int x = a; x < b; ++x)
    worst = std::min(worst, hSlack(g, u, x, y, l));
  return -static_cast<long long>(worst);
}
long long vRunCost(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                    int x, int a, int b) {
  if (a == b)
    return 0;
  int l = bestVLayer(g, u, layers, x, a, b);
  int worst = std::numeric_limits<int>::max();
  for (int y = a; y < b; ++y)
    worst = std::min(worst, vSlack(g, u, x, y, l));
  return -static_cast<long long>(worst);
}

// Layer touching p1 and layer touching p2, so the caller can reconcile shared
// tree nodes across edges (see point 5 in the file header).
struct EdgeResult {
  int layerAtP1, layerAtP2;
};

// Routes one tree edge (p1 -> p2) as an L, charging usage and emitting
// segments plus a via between the two legs if they land on different layers.
EdgeResult routeL(const RoutingGrid &g, Usage &u, const LayerPlan &lp, Point p1,
                   Point p2, std::vector<Seg> &out) {
  int xa = std::min(p1.x, p2.x), xb = std::max(p1.x, p2.x);
  int ya = std::min(p1.y, p2.y), yb = std::max(p1.y, p2.y);

  // Option A: horizontal at p1.y, then vertical at p2.x (bend at p2.x,p1.y).
  // Option B: vertical at p1.x, then horizontal at p2.y (bend at p1.x,p2.y).
  long long costA = hRunCost(g, u, lp.hLayers, p1.y, xa, xb) +
                     vRunCost(g, u, lp.vLayers, p2.x, ya, yb);
  long long costB = vRunCost(g, u, lp.vLayers, p1.x, ya, yb) +
                     hRunCost(g, u, lp.hLayers, p2.y, xa, xb);
  bool horizontalFirst = costA <= costB;
  int bendX = horizontalFirst ? p2.x : p1.x;
  int bendY = horizontalFirst ? p1.y : p2.y;

  int curLayer = -1; // unset: no leg has touched a point yet in this edge

  // `entry` is the coordinate along the run where this leg connects to
  // whatever came before it (p1 for leg1, the bend for leg2) -- it is one of
  // the run's two ends, whichever end that actually is, not always the lower
  // one. Any layer-change via belongs there, not at the run's minimum.
  auto emitH = [&](int y, int a, int b, int entry) {
    if (a == b)
      return;
    int l = bestHLayer(g, u, lp.hLayers, y, a, b);
    if (curLayer != -1 && curLayer != l)
      out.push_back({entry, y, curLayer, entry, y, l});
    curLayer = l;
    for (int x = a; x < b; ++x)
      u.h[g.hIndex(x, y, l)] += 1;
    out.push_back({a, y, l, b, y, l});
  };
  auto emitV = [&](int x, int a, int b, int entry) {
    if (a == b)
      return;
    int l = bestVLayer(g, u, lp.vLayers, x, a, b);
    if (curLayer != -1 && curLayer != l)
      out.push_back({x, entry, curLayer, x, entry, l});
    curLayer = l;
    for (int y = a; y < b; ++y)
      u.v[g.vIndex(x, y, l)] += 1;
    out.push_back({x, a, l, x, b, l});
  };

  if (horizontalFirst)
    emitH(p1.y, xa, xb, p1.x);
  else
    emitV(p1.x, ya, yb, p1.y);
  // curLayer now holds the layer touching p1: if leg1 was non-empty it is
  // leg1's layer; if leg1 was a no-op (p1 and p2 already share that
  // coordinate), leg1 never ran and curLayer is still -1, but leg2 below
  // starts exactly at p1 in that case, so its assignment covers p1 too.
  int layerAtP1Pending = curLayer;

  if (horizontalFirst)
    emitV(bendX, ya, yb, bendY);
  else
    emitH(bendY, xa, xb, bendX);

  int layerAtP1 = (layerAtP1Pending != -1) ? layerAtP1Pending : curLayer;
  return {layerAtP1, curLayer};
}

// ---------------------------------------------------------------------------
// Whole design
// ---------------------------------------------------------------------------

struct RoutedNet {
  const Net *net;
  std::vector<Seg> segs;
};

// If `at` was already landed on a different layer by an earlier edge, add a
// via bridging the two so the net's routed subgraph stays one component.
void reconcile(std::vector<int> &nodeLayer, int idx, int layer, Point at,
               std::vector<Seg> &out) {
  if (nodeLayer[idx] == -1) {
    nodeLayer[idx] = layer;
  } else if (nodeLayer[idx] != layer) {
    out.push_back({at.x, at.y, nodeLayer[idx], at.x, at.y, layer});
    nodeLayer[idx] = layer;
  }
}

std::vector<RoutedNet> routeDesign(const Design &d) {
  const RoutingGrid &g = d.grid;
  Usage usage(g);
  LayerPlan lp(g);
  std::vector<RoutedNet> result;
  result.reserve(d.nets.size());

  for (const Net &n : d.nets) {
    if (n.pins.size() < 2)
      continue;

    // Distinct tiles only; a net entirely within one tile needs no wire.
    std::vector<Point> pts;
    for (const Pin &p : n.pins) {
      bool dup = false;
      for (const Point &q : pts)
        if (q.x == p.x && q.y == p.y) {
          dup = true;
          break;
        }
      if (!dup)
        pts.push_back({p.x, p.y});
    }
    if (pts.size() < 2)
      continue;

    RoutedNet rn{&n, {}};
    std::vector<int> nodeLayer(pts.size(), -1);
    for (auto &e : mstEdges(pts)) {
      EdgeResult r = routeL(g, usage, lp, pts[e.first], pts[e.second], rn.segs);
      reconcile(nodeLayer, e.first, r.layerAtP1, pts[e.first], rn.segs);
      reconcile(nodeLayer, e.second, r.layerAtP2, pts[e.second], rn.segs);
    }

    // The tree above only lands on whichever layer had capacity in each
    // run's direction; it never touches a pin's own declared layer. The
    // verifier (and the real ISPD scorer) require the route to reach that
    // exact layer, so via down to it at every pin's tile.
    for (const Pin &p : n.pins) {
      for (size_t i = 0; i < pts.size(); ++i) {
        if (pts[i].x == p.x && pts[i].y == p.y) {
          reconcile(nodeLayer, static_cast<int>(i), p.layer, pts[i], rn.segs);
          break;
        }
      }
    }
    result.push_back(std::move(rn));
  }
  return result;
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
    for (const Seg &s : rn.segs) {
      out << "(" << realX(s.x1) << "," << realY(s.y1) << "," << (s.l1 + 1)
          << ")-(" << realX(s.x2) << "," << realY(s.y2) << "," << (s.l2 + 1)
          << ")\n";
    }
    out << "!\n";
  }
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

namespace selftest {

const char *kGr = R"(grid 4 4 2
vertical capacity 0 2
horizontal capacity 2 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 5
netA 0 2 1
5 5 1
35 5 1
netB 1 2 1
5 15 1
35 15 1
netC 2 2 1
5 25 1
5 35 1
netD 3 3 1
5 5 1
35 5 1
5 35 1
netE 4 2 1
35 35 1
5 5 1
0
)";

int failures = 0;

void check(bool cond, const std::string &what) {
  std::cout << (cond ? "  ok   " : "  FAIL ") << what << "\n";
  if (!cond)
    ++failures;
}

// Whether a net's segments form one connected graph, using the same node
// identity the real verifier uses: (x,y,layer) triples, with a straight run
// expanded into every tile it crosses so two runs that merely touch end to
// end are seen as linked.
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
    } else { // via
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
    return static_cast<int>(std::lower_bound(nodes.begin(), nodes.end(), k) -
                             nodes.begin());
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

int run() {
  const std::string gr = "lroute_selftest.gr";
  {
    std::ofstream out(gr);
    out << kGr;
  }
  Design d = readGr(gr);
  std::cout << "self test\n";

  auto nets = routeDesign(d);
  check(nets.size() == 5, "all five nets produced a route");

  // netA and netB are pure horizontal 2 pin nets on a grid with only one
  // horizontal-capable layer: no bend, no via needed.
  check(nets[0].segs.size() == 1, "netA is a single straight run, no via");
  check(nets[1].segs.size() == 1, "netB is a single straight run, no via");
  // netC is pure vertical, so its run must go on layer 2 (0 vertical
  // capacity on layer 1), but both its pins are declared on layer 1 -- each
  // end needs a reconciling via down to the pin's actual layer.
  check(nets[2].segs.size() == 3, "netC gets a via at each pin down to layer 1");
  check(segsConnected(nets[2].segs), "netC stays one component after the vias");

  // netD is a 3 pin net whose MST has a branch point at (5,5): one edge runs
  // to (35,5) [horizontal], the other to (5,35) [vertical]. Each edge picks
  // its own layer independently, so the branch point needs a reconciling via
  // unless both edges happen to agree -- verify the net is still one
  // connected component regardless of which layers were chosen.
  check(segsConnected(nets[3].segs),
        "netD's branch point is reconciled into one component");

  // netE's pins are listed high-coordinate first: p1=(35,35), p2=(5,5). Both
  // legs of its L therefore run from the *high* end of their range down to
  // the low end, so the bend sits at the range's maximum, not its minimum.
  // A router that assumes the bend via always belongs at the low end (a real
  // bug this test caught) mislocates it and silently splits the net.
  check(segsConnected(nets[4].segs),
        "netE's bend via lands at the actual bend, not the range minimum");

  const std::string sol = "lroute_selftest.route";
  writeSolution(sol, d.grid, nets);

  std::ifstream in(sol);
  std::string first;
  std::getline(in, first);
  check(first == "netA 0", "solution file starts with the first net header");

  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL")
            << "\n";
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

  Design d = readGr(argv[1]);
  auto nets = routeDesign(d);
  writeSolution(argv[2], d.grid, nets);
  std::cerr << "routed " << nets.size() << " of " << d.nets.size()
            << " nets\n";
  return 0;
}
