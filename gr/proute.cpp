// proute.cpp
//
// Stage 3 pattern router: L and Z shaped candidates per tree edge, chosen by
// an ad hoc congestion cost, with rip-up-and-reroute rounds against real
// overflow.
//
// "2D" per the project's staging means what it meant for lroute: no shared
// layered graph and no via pricing yet (that is stage 5). Layer choice is
// still the same per-run trick -- whichever layer running in a run's
// direction has the most room gets it -- just folded into one cost function
// alongside the shape choice instead of living in a separate helper.
//
// This supersedes lroute.cpp as the router under active development. lroute
// stays put, unmodified, as the throwaway stage-2 baseline: its wirelength
// and overflow numbers are the comparison point that shows this stage is
// actually better, not just different.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o proute proute.cpp
//
// Use:
//     ./proute benchmark.gr solution.route
//     ./proute --selftest
//
// Algorithm, per net:
//   1. Distinct pin tiles, minimum spanning tree over them -- identical to
//      lroute. Rip-up-and-reroute changes how a tree edge is *drawn*, not the
//      tree's topology; re-deciding topology is maze/Steiner territory, out
//      of scope for a pattern router.
//   2. Each tree edge gets a shortlist of candidate routes: the two L shapes,
//      plus a handful of Z shapes (HVH and VHV, each with a small number of
//      candidate jog positions). A collinear edge has only one candidate,
//      full stop.
//   3. Every candidate is priced: each unit edge it crosses costs 1 (wirelength)
//      plus a fixed penalty for each unit of overflow adding this net's width
//      there would cause, plus 1 per layer change (matching the contest's own
//      "1 tile per via" scoring). The layer for each run is chosen to minimize
//      that run's own cost. Cheapest whole candidate wins; ties favor the
//      earlier candidate in a fixed generation order (L before Z, ascending
//      jog position), which keeps results reproducible.
//      The overflow penalty is a placeholder -- stage 4 replaces it with
//      prices from the Lagrangian dual. Keeping it isolated in one function
//      (edgeCost) is what makes that swap possible without touching anything
//      else here.
//   4. Branch points and pins get the same reconciling-via treatment lroute
//      used: a tree node's landing layer is recorded on first touch, and any
//      later touch on a different layer gets an explicit via there, so the
//      net's routed subgraph never silently splits across layers.
//   5. After every net has an initial route, nets touching an overflowed edge
//      are ripped up and rerouted, in net order, against the usage left by
//      everyone else -- including nets already rerouted earlier in the same
//      round. Repeat until overflow reaches zero, stops improving, or a round
//      cap is hit. Giant nets (over 1000 pins) are routed once but excluded
//      from rip-up: the ISPD spec itself exempts them from the overflow and
//      connectivity checks, and rerouting a thousand-edge tree every round is
//      the wrong place to spend a pattern router's budget.

#include <algorithm>
#include <chrono>
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
// Capacity usage, charged in real consumption units (width + layer spacing),
// matching how the verifier and the ISPD scorer both charge it.
// ---------------------------------------------------------------------------

struct Usage {
  std::vector<int> h, v; // parallel to grid.hcap / grid.vcap
  explicit Usage(const RoutingGrid &g) : h(g.hCount(), 0), v(g.vCount(), 0) {}
};

// Ad hoc congestion weight: cost per unit of overflow a charge would cause on
// one edge, on top of the flat 1-per-edge wirelength cost. Large enough that
// a detour of a few dozen tiles is preferred over adding overflow, small
// enough that truly unavoidable overflow doesn't dominate every comparison
// into meaninglessness. Stage 4 replaces this constant with a per-edge price
// that adapts over rounds; until then it is a fixed stand-in.
constexpr long long kOverflowWeight = 50;
constexpr long long kViaCost = 1; // matches the contest's 1-tile-per-via rule
// Nets this large are exempt from the ISPD overflow/connectivity checks
// (more than 1000 pins). Route them once; don't spend rip-up rounds on them.
constexpr size_t kGiantNetPins = 1000;

// ---------------------------------------------------------------------------
// Layer direction: each layer's capacity is nonzero in at most one direction.
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

// Cost of charging `amount` consumption to one unit edge currently at `used`
// against `cap`: 1 for the wire itself, plus the overflow penalty for
// whatever portion of `amount` pushes past capacity.
long long edgeCost(int used, int cap, int amount) {
  long long overflow = static_cast<long long>(used) + amount - cap;
  long long penalty = overflow > 0 ? overflow * kOverflowWeight : 0;
  return 1 + penalty;
}

// Cheapest layer (and its cost and consumption amount) for running `width`
// through [a,b) on row/col `cross`, among `layers`.
struct LayerChoice {
  int layer;
  long long cost;
  int amount;
};

LayerChoice bestHLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                        int y, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<long long>::max(), 0};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    long long cost = 0;
    for (int x = a; x < b; ++x)
      cost += edgeCost(u.h[g.hIndex(x, y, l)], g.hcap[g.hIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost, amount};
  }
  return best;
}
LayerChoice bestVLayer(const RoutingGrid &g, const Usage &u, const std::vector<int> &layers,
                        int x, int a, int b, int width) {
  LayerChoice best{layers.front(), std::numeric_limits<long long>::max(), 0};
  for (int l : layers) {
    int amount = width + g.minSpacing[l];
    long long cost = 0;
    for (int y = a; y < b; ++y)
      cost += edgeCost(u.v[g.vIndex(x, y, l)], g.vcap[g.vIndex(x, y, l)], amount);
    if (cost < best.cost)
      best = {l, cost, amount};
  }
  return best;
}

// ---------------------------------------------------------------------------
// Minimum spanning tree over a net's distinct pin tiles (unchanged from
// lroute: rip-up-and-reroute here means re-picking a tree edge's shape, not
// its topology).
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
// Candidate routes for one tree edge
// ---------------------------------------------------------------------------

// One leg of a candidate: a straight run at a fixed row (horizontal) or
// column (vertical), spanning [a,b], with `entry` marking which end (a or b)
// connects to whatever came before -- p1 for the first leg, the previous
// leg's far end for any later one. The other end is where the next leg (or
// p2, if this is the last leg) attaches.
struct Leg {
  bool horizontal;
  int cross; // row if horizontal, column if vertical
  int a, b;  // a <= b
  int entry; // == a or == b
};

using Candidate = std::vector<Leg>;

// Interior jog positions for a Z shape spanning [lo,hi]: a few evenly spread
// points strictly inside the range, deduplicated. Empty if the range has no
// room for one (span under 2).
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

// Generates every candidate for one tree edge, in a fixed order so cost ties
// resolve the same way every run: straight (if collinear), then the two L
// shapes, then HVH jogs ascending, then VHV jogs ascending.
std::vector<Candidate> generateCandidates(Point p1, Point p2) {
  std::vector<Candidate> out;
  int xa = std::min(p1.x, p2.x), xb = std::max(p1.x, p2.x);
  int ya = std::min(p1.y, p2.y), yb = std::max(p1.y, p2.y);
  bool dx = xa != xb, dy = ya != yb;

  if (!dx && !dy)
    return out; // identical points; caller should not ask for this
  if (!dy) {
    out.push_back({{true, p1.y, xa, xb, p1.x}});
    return out;
  }
  if (!dx) {
    out.push_back({{false, p1.x, ya, yb, p1.y}});
    return out;
  }

  // L: horizontal at p1.y, then vertical at p2.x.
  out.push_back({{true, p1.y, xa, xb, p1.x}, {false, p2.x, ya, yb, p1.y}});
  // L: vertical at p1.x, then horizontal at p2.y.
  out.push_back({{false, p1.x, ya, yb, p1.y}, {true, p2.y, xa, xb, p1.x}});

  // Z (HVH): horizontal to a jog column, vertical the full height, horizontal
  // the rest of the way.
  for (int mid : jogPositions(xa, xb)) {
    Candidate c;
    c.push_back({true, p1.y, std::min(p1.x, mid), std::max(p1.x, mid), p1.x});
    c.push_back({false, mid, ya, yb, p1.y});
    c.push_back({true, p2.y, std::min(mid, p2.x), std::max(mid, p2.x), mid});
    out.push_back(std::move(c));
  }
  // Z (VHV): vertical to a jog row, horizontal the full width, vertical the
  // rest of the way.
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
// Pricing and committing a candidate
// ---------------------------------------------------------------------------

struct Seg {
  int x1, y1, l1, x2, y2, l2;
};
struct ChargeRecord {
  bool horizontal;
  int cross, a, b, layer, amount;
};

struct Priced {
  long long cost;
  std::vector<int> legLayer; // chosen layer per leg, parallel to the Candidate
};

// Prices a candidate against the current usage without mutating it. The
// layer chosen for each leg is the one minimizing that leg's own cost; a via
// is charged whenever consecutive legs land on different layers.
Priced priceCandidate(const RoutingGrid &g, const Usage &u, const LayerPlan &lp,
                       const Candidate &cand, int width) {
  Priced p;
  p.cost = 0;
  int prevLayer = -1;
  for (const Leg &leg : cand) {
    LayerChoice lc = leg.horizontal
                          ? bestHLayer(g, u, lp.hLayers, leg.cross, leg.a, leg.b, width)
                          : bestVLayer(g, u, lp.vLayers, leg.cross, leg.a, leg.b, width);
    p.cost += lc.cost;
    if (prevLayer != -1 && prevLayer != lc.layer)
      p.cost += kViaCost;
    prevLayer = lc.layer;
    p.legLayer.push_back(lc.layer);
  }
  return p;
}

// Layer touching p1 and layer touching p2, for cross-edge reconciliation at
// shared tree nodes (see the file header, point 4).
struct EdgeResult {
  int layerAtP1, layerAtP2;
};

// Commits a candidate using layers already chosen by priceCandidate: charges
// usage, emits segments (including any inter-leg vias), and records charges
// so a future rip-up can undo them exactly.
EdgeResult commitCandidate(const RoutingGrid &g, Usage &u, const Candidate &cand,
                            const std::vector<int> &legLayer, int width,
                            std::vector<Seg> &segs, std::vector<ChargeRecord> &charges) {
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

// If `at` was already landed on a different layer by an earlier edge or pin,
// add a via bridging the two so the net's routed subgraph stays one piece.
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
// Routing one net, start to finish: pick the cheapest candidate per tree
// edge, commit it, reconcile branch points, then via each pin down to its
// declared layer. Used both for a net's first route and for rerouting it
// after rip-up -- the two are identical once usage reflects the rip-up.
// ---------------------------------------------------------------------------

struct RoutedNet {
  const Net *net;
  std::vector<Point> pts;
  std::vector<std::pair<int, int>> mst;
  std::vector<Seg> segs;
  std::vector<ChargeRecord> charges;
};

void routeNet(const RoutingGrid &g, Usage &usage, const LayerPlan &lp, RoutedNet &rn) {
  rn.segs.clear();
  rn.charges.clear();
  std::vector<int> nodeLayer(rn.pts.size(), -1);

  for (auto &e : rn.mst) {
    Point p1 = rn.pts[e.first], p2 = rn.pts[e.second];
    auto candidates = generateCandidates(p1, p2);

    size_t bestIdx = 0;
    Priced bestPriced = priceCandidate(g, usage, lp, candidates[0], rn.net->minWidth);
    for (size_t i = 1; i < candidates.size(); ++i) {
      Priced pr = priceCandidate(g, usage, lp, candidates[i], rn.net->minWidth);
      if (pr.cost < bestPriced.cost) {
        bestPriced = std::move(pr);
        bestIdx = i;
      }
    }

    EdgeResult r = commitCandidate(g, usage, candidates[bestIdx], bestPriced.legLayer,
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

void ripUp(const RoutingGrid &g, Usage &usage, RoutedNet &rn) {
  for (const ChargeRecord &c : rn.charges)
    undoCharge(g, usage, c);
  rn.segs.clear();
  rn.charges.clear();
}

// ---------------------------------------------------------------------------
// Whole design: initial routing pass, then rip-up-and-reroute rounds.
// ---------------------------------------------------------------------------

struct RunReport {
  std::vector<RoutedNet> nets;
  long long overflowAfterInitial = 0;
  long long overflowFinal = 0;
  int rounds = 0;
};

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

RunReport route(const Design &d, int maxRounds) {
  const RoutingGrid &g = d.grid;
  RunReport rep;
  Usage usage(g);
  LayerPlan lp(g);

  for (const Net &n : d.nets) {
    RoutedNet rn;
    rn.net = &n;
    if (n.pins.size() < 2)
      continue;
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
    routeNet(g, usage, lp, rn);
    rep.nets.push_back(std::move(rn));
  }

  rep.overflowAfterInitial = totalOverflow(g, usage);
  rep.overflowFinal = rep.overflowAfterInitial;

  long long prevOverflow = rep.overflowAfterInitial;
  for (int round = 0; round < maxRounds && prevOverflow > 0; ++round) {
    bool anyRerouted = false;
    for (RoutedNet &rn : rep.nets) {
      if (rn.net->pins.size() > kGiantNetPins)
        continue;
      if (!netOverflowed(g, usage, rn))
        continue;
      ripUp(g, usage, rn);
      routeNet(g, usage, lp, rn);
      anyRerouted = true;
    }
    rep.rounds = round + 1;
    long long now = totalOverflow(g, usage);
    rep.overflowFinal = now;
    if (now == 0 || now >= prevOverflow || !anyRerouted)
      break; // converged, or no more progress being made
    prevOverflow = now;
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

// Basic shapes: same fixture lroute used, so a regression in the shared
// commit/reconcile logic would show up here the same way it did there.
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
  Design d = parse("proute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 5);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.overflowFinal == 0, "no overflow on an uncongested design");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs),
        "netE (high-to-low bend) is one component");
}

// A net whose bend must dodge a blocked cell that both L shapes cross: one L
// blocks its horizontal run, the other blocks its vertical run, and only a Z
// jog placed between the two obstacles gets through clean. Grid is 8 wide, 4
// tall so there's room for the geometry; capacities are large everywhere
// except the two adjustments, so the only way to see overflow is by routing
// through one of the two blocked edges.
const char *kZGr = R"(grid 8 4 2
vertical capacity 0 20
horizontal capacity 20 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netZ 0 2 1
5 5 1
65 25 1
2
4 0 1 5 0 1 0
0 0 2 0 1 2 0
)";

void testZShapeAvoidsBlockage() {
  Design d = parse("proute_selftest_z.gr", kZGr);
  // p1=(0,0), p2=(6,2). L-HV crosses the blocked row-0 edge at x=4..5. L-VH
  // and every VHV jog cross the blocked column-0 edge at y=0..1 (both use a
  // vertical run fixed at x=0). Only an HVH jog placed between x=1 and x=4
  // avoids both, since it never touches column 0 or the x=4..5 cell.
  RunReport rep = route(d, 1);
  check(rep.nets.size() == 1, "netZ produced a route");
  check(rep.overflowAfterInitial == 0,
        "netZ's first pass already finds the one clean Z shape, no overflow");
  check(segsConnected(rep.nets[0].segs), "netZ is one connected component");
}

// Two nets that both want the same single-capacity edge. net1 (routed first)
// has two equal-cost L options and, seeing an empty grid, picks the one using
// the shared edge -- a tie broken toward whichever is listed first, which is
// the one this fixture is built to make collide. net2 (routed second) is
// collinear: it has no alternative at all and must use that same edge,
// overflowing it. A one-shot router has no way back from this. Rip-up must
// revisit net1 after net2 exists, discover the untaken L option is now
// strictly cheaper, and move -- freeing the edge for net2 alone.
const char *kRipUpGr = R"(grid 12 6 2
vertical capacity 0 20
horizontal capacity 20 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 2
net1 0 2 1
15 15 1
75 35 1
net2 1 2 1
45 15 1
55 15 1
1
4 1 1 5 1 1 1
)";

void testRipUpResolvesOverflow() {
  Design d = parse("proute_selftest_ripup.gr", kRipUpGr);
  RunReport once = route(d, 0);
  check(once.overflowAfterInitial > 0,
        "without rip-up, net1's tie-break collision with net2 overflows");

  RunReport fixed = route(d, 5);
  check(fixed.overflowAfterInitial == once.overflowAfterInitial,
        "the initial pass is identical regardless of the round budget");
  check(fixed.overflowFinal == 0, "rip-up-and-reroute resolves the overflow");
  check(fixed.rounds >= 1 && fixed.rounds <= 5,
        "it converges well within the round cap");
}

int run() {
  std::cout << "self test\n";
  testBasicShapes();
  testZShapeAvoidsBlockage();
  testRipUpResolvesOverflow();
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

  auto t0 = std::chrono::steady_clock::now();
  Design d = readGr(argv[1]);
  RunReport rep = route(d, 20);
  writeSolution(argv[2], d.grid, rep.nets);
  auto t1 = std::chrono::steady_clock::now();

  std::cerr << "routed " << rep.nets.size() << " of " << d.nets.size() << " nets\n";
  std::cerr << "overflow after initial pass: " << rep.overflowAfterInitial << "\n";
  std::cerr << "overflow after " << rep.rounds
            << " rip-up round(s): " << rep.overflowFinal << "\n";
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}
