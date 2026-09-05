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
// actually tuning the *formula itself* -- see priceExponentTerm's comment
// for what was tried and measured. The version below (price only actual
// overflow, not any usage at all; small epsilon; many rounds) reaches 405K
// on adaptec1, 142K on newblue1 (matching groute), 509K on bigblue1 --
// better than groute everywhere, still short of proute's rip-up quality
// everywhere, and the gap is not closing with more tuning of epsilon or
// round count alone at this point. Recording that honestly rather than
// keep turning the same two knobs.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o nproute nproute.cpp
//
// Use:
//     ./nproute benchmark.gr solution.route
//     ./nproute --selftest
//
// Environment variables, for tuning experiments (all optional):
//     NPROUTE_EPS=<float>       step size epsilon, default 0.005
//     NPROUTE_ROUNDS=<int>      max rounds, default 100
//     NPROUTE_PATIENCE=<int>    rounds without improvement before giving up,
//                               default 50
//     NPROUTE_TRACE=1           print each round's overflow to stderr

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
// The exponent's argument is max(0, usage/cap - 1): flat until an edge is
// actually pushed past capacity, not the literal CLAUDE.md usage/cap (which
// rises for any usage at all, even comfortably under capacity). Three
// variants were measured against each other on adaptec1 before settling
// here (see the stage-4-tuning commit for the numbers):
//   usage/cap directly (literal formula): best overflow 534762, and it gets
//     *worse* with more rounds -- every previously-touched edge keeps
//     getting marginally pricier even nowhere near congested, pushing nets
//     into detours that buy nothing and inflating wirelength.
//   max(0, usage/cap - 1) (this one): best overflow 404910 at epsilon=0.002,
//     genuinely still improving after 150 rounds rather than plateauing in
//     1-2 like the others.
//   max(0, usage - cap) (absolute overflow, matching how proute's own ad
//     hoc penalty charges a flat amount per unit regardless of the edge's
//     capacity): best overflow 537904 -- worse, not better. The plausible
//     reason (a 1-unit overflow being priced up to 5x weaker on adaptec1's
//     capacity-32 layers than its capacity-6 ones under the ratio form) did
//     not survive contact with measurement, so it's not what the ratio form
//     was missing.
double priceExponentTerm(double usage, double cap) {
  return std::max(0.0, usage / cap - 1.0);
}

void negotiate(const RoutingGrid &g, Prices &pr, const Usage &u, const ChargeRecord &c,
               double epsilon) {
  if (c.horizontal) {
    for (int x = c.a; x < c.b; ++x) {
      size_t i = g.hIndex(x, c.cross, c.layer);
      if (g.hcap[i] <= 0)
        pr.h[i] = kForbiddenPrice;
      else
        pr.h[i] *= std::exp(epsilon * priceExponentTerm(u.h[i], g.hcap[i]));
    }
  } else {
    for (int y = c.a; y < c.b; ++y) {
      size_t i = g.vIndex(c.cross, y, c.layer);
      if (g.vcap[i] <= 0)
        pr.v[i] = kForbiddenPrice;
      else
        pr.v[i] *= std::exp(epsilon * priceExponentTerm(u.v[i], g.vcap[i]));
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
};

void routeNet(const RoutingGrid &g, Prices &prices, Usage &usage, const LayerPlan &lp,
              double epsilon, RoutedNet &rn) {
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
      negotiate(g, prices, usage, rn.charges[i], epsilon);

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

struct RunReport {
  std::vector<RoutedNet> nets;
  long long bestOverflow = std::numeric_limits<long long>::max();
  int bestRound = -1;
  int roundsRun = 0;
  bool convergedToZero = false;
  bool stalled = false;
};

// Higher than groute's/proute's patience: with the overflow-only formula,
// improvement comes slowly and steadily rather than in a quick 1-2 round
// burst (adaptec1 was still setting new bests past round 60 at epsilon
// 0.005), so a short patience window gives up on real, ongoing progress.
constexpr int kDefaultPatience = 50;

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

  // Giant nets: route once, negotiating as they go same as everyone else on
  // this first touch, then frozen -- never revisited by later rounds.
  for (RoutedNet &rn : nets)
    if (rn.giant)
      routeNet(g, prices, usage, lp, epsilon, rn);

  int roundsSinceImprovement = 0;
  for (int round = 0; round < maxRounds; ++round) {
    // Round 0 gives every net its first route. After that, only nets
    // currently sitting on an overflowed edge get ripped up and rerouted --
    // matching proute's rip-up discipline. Rerouting an already-legal net
    // every round bought nothing but churn: measured on adaptec1, "reroute
    // everyone always" drifts from a round-1 low of 473K up past 530K over
    // the following rounds, since a net with a perfectly good route can get
    // shaken loose by *other* nets' price changes even though nothing about
    // its own situation changed.
    for (RoutedNet &rn : nets) {
      if (rn.giant)
        continue;
      if (round > 0 && !netOverflowed(g, usage, rn))
        continue;
      routeNet(g, prices, usage, lp, epsilon, rn);
    }

    long long overflow = totalOverflow(g, usage);
    rep.roundsRun = round + 1;
    if (std::getenv("NPROUTE_TRACE"))
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

  int maxRounds = 100;
  double epsilon = 0.005; // tuned empirically against adaptec1; see NPROUTE_EPS to explore
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
