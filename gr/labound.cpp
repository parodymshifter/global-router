// labound.cpp -- a certified lower bound on the vias any layer assignment of a fixed 2D route needs without overflow.
//
// Not a router: an analysis tool (HISTORY_COST_SWEEP.md Part 214, the via scoping step). Scored ISPD 2008 wirelength
// counts one unit per via layer crossed; on bigblue1 vias are 34.7% of the score, and the uncapacitated floor for the
// same 2D wire (every leg on its best layer, capacity ignored) is 820812 against the 1965453 the standard uses -- but
// layers 1-2 hold a fifth of layers 3-6's capacity, so that floor is unreachable. This tool bounds the capacitated
// optimum from below by Lagrangian relaxation of the per-layer wire capacities.
//
// Problem: each net's 2D wire (from a route file, projected to 2D) is fixed; a layer assignment puts every straight leg
// (a run between pins, branches and bends) on one layer of its direction; cost = the sum over tiles of the via stack
// span (max minus min layer meeting there, the pin layer included at a pin tile); constraint: on every tile edge and
// layer, the nets' demand (minimum width + the layer's spacing) is at most the capacity.
// Bound: for prices mu >= 0 on (edge, layer),
//     vias >= sum_nets min_assignment [ vias_net + sum over its legs' edges of mu(e, l) x demand ] - sum mu(e, l) cap(e, l)
// (weak duality; every term of a capacity-feasible assignment's price sum is <= the price x capacity). The inner
// minimum is an exact tree DP per net. The prices move by projected subgradient with Polyak steps against an upper
// value (the route's own via count, or a better feasible assignment found on the way). Each iteration's assignment is
// also checked for overflow: one with none is a feasible layer assignment, and its via count an upper bound.
//
// Holding a leg on one layer: a leg could switch layers part-way with vias, so a leg-per-layer DP over-estimates the
// true per-net minimum under prices -- which would make the "bound" invalid. So the DP here runs over unit edges, not
// legs: every tile edge chooses its layer, with stack spans at every tile (a straight pass-through tile counts the
// span between its two edges' layers). That is exact for the relaxation.
//
// Usage: labound <design.gr> <route> [--iters N] [--threads T] [--out prefix]
//        labound --selftest

#include "gr_io.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace gr_io;

namespace {

const double kInf = 1e300;

// One net's 2D tree over unit tile edges, rooted.
struct NetTree {
  int net = -1;
  int demandH = 2;                  // minimum width + spacing (all 2008 layers share spacing; checked at load)
  std::vector<int> nodeX, nodeY;    // tiles
  std::vector<int> parent;          // node -> parent node (-1 root)
  std::vector<int> parentEdge;      // node -> the 2D edge to its parent: index into the global edge list, -1 root
  std::vector<char> parentHoriz;    // node -> that edge's direction
  std::vector<std::vector<int>> kids;
  std::vector<int> order;           // root first (BFS)
  std::vector<int> pinLo, pinHi;    // node -> pin layer range (-1: no pin)
};

struct Problem {
  int nl = 0;
  std::vector<int> hl, vl; // layers per direction
  // a 2D edge id: h edges 0..nH-1 (x, y) -> y*(nx-1)+x, v edges nH + y*nx + x
  int nx = 0, ny = 0;
  size_t nH = 0, nV = 0;
  std::vector<int> capH, capV; // per (edge, layer): [edge * nl + l]
  std::vector<NetTree> nets;
  long long routeVias = 0; // the route file's own via count (its layer assignment)
};

size_t hEdge(const Problem &p, int x, int y) { return static_cast<size_t>(y) * (p.nx - 1) + x; }
size_t vEdge(const Problem &p, int x, int y) { return p.nH + static_cast<size_t>(y) * p.nx + x; }

int capOf(const Problem &p, size_t e, int l) {
  return e < p.nH ? p.capH[e * p.nl + l] : p.capV[(e - p.nH) * p.nl + l];
}

// Builds a rooted tree from a net's unit 2D edges and pins; returns false on a cycle or a disconnected net.
bool buildTree(const Problem &p, const std::vector<std::pair<char, std::pair<int, int>>> &edges,
               const std::map<std::pair<int, int>, std::pair<int, int>> &pinLayers, NetTree &t) {
  std::map<std::pair<int, int>, int> id;
  auto node = [&](int x, int y) {
    auto it = id.find({x, y});
    if (it != id.end())
      return it->second;
    int k = static_cast<int>(t.nodeX.size());
    id[{x, y}] = k;
    t.nodeX.push_back(x);
    t.nodeY.push_back(y);
    return k;
  };
  std::vector<std::vector<std::pair<int, std::pair<int, char>>>> adj; // node -> (other, (edge id, horiz))
  for (const auto &e : edges) {
    const char h = e.first;
    const int x = e.second.first, y = e.second.second;
    int a = node(x, y), b = h ? node(x + 1, y) : node(x, y + 1);
    if (static_cast<int>(adj.size()) <= std::max(a, b))
      adj.resize(std::max(a, b) + 1);
    const int eid = static_cast<int>(h ? hEdge(p, x, y) : vEdge(p, x, y));
    adj[a].push_back({b, {eid, h}});
    adj[b].push_back({a, {eid, h}});
  }
  for (const auto &kv : pinLayers)
    node(kv.first.first, kv.first.second);
  const int n = static_cast<int>(t.nodeX.size());
  adj.resize(n);
  if (static_cast<int>(edges.size()) != n - 1)
    return false;
  t.parent.assign(n, -2);
  t.parentEdge.assign(n, -1);
  t.parentHoriz.assign(n, 0);
  t.kids.assign(n, {});
  t.parent[0] = -1;
  t.order = {0};
  for (size_t i = 0; i < t.order.size(); ++i) {
    int u = t.order[i];
    for (auto &[v, eh] : adj[u])
      if (t.parent[v] == -2) {
        t.parent[v] = u;
        t.parentEdge[v] = eh.first;
        t.parentHoriz[v] = eh.second;
        t.kids[u].push_back(v);
        t.order.push_back(v);
      }
  }
  if (static_cast<int>(t.order.size()) != n)
    return false;
  t.pinLo.assign(n, -1);
  t.pinHi.assign(n, -1);
  for (const auto &kv : pinLayers) {
    int k = id[kv.first];
    t.pinLo[k] = kv.second.first;
    t.pinHi[k] = kv.second.second;
  }
  return true;
}

// The DP: per node, best[v][l] = least cost of v's subtree given the edge to v's parent sits on layer l.
// Returns the net's minimum of vias + priced wire; fills choice (per node, its parent edge's layer) if asked.
struct Scratch {
  std::vector<double> best;      // node * nl + l
  std::vector<double> kidBest;   // per kid, per layer (reused)
};

double solveNet(const Problem &p, const NetTree &t, const std::vector<double> &mu, Scratch &s, std::vector<int> *choice) {
  const int nl = p.nl, n = static_cast<int>(t.nodeX.size());
  s.best.assign(static_cast<size_t>(n) * nl, kInf);
  auto edgeCost = [&](const NetTree &tt, int v, int l) {
    const size_t e = static_cast<size_t>(tt.parentEdge[v]);
    return mu.empty() ? 0.0 : mu[e * nl + l] * tt.demandH;
  };
  // node range cost: given the layers that must meet at u (parent layer lp or none, pin range), choose [lo, hi]
  // covering them and each kid's layer inside it; kids' layers restricted to their direction's layers
  auto nodeBest = [&](int u, int lp, std::vector<int> *pick) {
    int lo0 = INT_MAX, hi0 = INT_MIN;
    if (lp >= 0)
      lo0 = hi0 = lp;
    if (t.pinLo[u] >= 0) {
      lo0 = std::min(lo0, t.pinLo[u]);
      hi0 = std::max(hi0, t.pinHi[u]);
    }
    double bestC = kInf;
    int bLo = 0, bHi = 0;
    for (int lo = 0; lo < nl; ++lo)
      for (int hi = lo; hi < nl; ++hi) {
        if (lo0 != INT_MAX && (lo > lo0 || hi < hi0))
          continue;
        double c = hi - lo;
        for (int v : t.kids[u]) {
          const std::vector<int> &cand = t.parentHoriz[v] ? p.hl : p.vl;
          double m = kInf;
          for (int l : cand)
            if (l >= lo && l <= hi)
              m = std::min(m, s.best[static_cast<size_t>(v) * nl + l]);
          c += m;
          if (c >= bestC)
            break;
        }
        if (c < bestC) {
          bestC = c;
          bLo = lo;
          bHi = hi;
        }
      }
    if (pick) {
      for (int v : t.kids[u]) {
        const std::vector<int> &cand = t.parentHoriz[v] ? p.hl : p.vl;
        double m = kInf;
        int arg = -1;
        for (int l : cand)
          if (l >= bLo && l <= bHi && s.best[static_cast<size_t>(v) * nl + l] < m) {
            m = s.best[static_cast<size_t>(v) * nl + l];
            arg = l;
          }
        (*pick)[v] = arg;
      }
    }
    return bestC;
  };
  for (int i = n - 1; i >= 1; --i) {
    const int v = t.order[i];
    const std::vector<int> &cand = t.parentHoriz[v] ? p.hl : p.vl;
    for (int l : cand)
      s.best[static_cast<size_t>(v) * nl + l] = edgeCost(t, v, l) + nodeBest(v, l, nullptr);
  }
  const double total = nodeBest(t.order[0], -1, nullptr);
  if (choice) {
    choice->assign(n, -1);
    // top-down: the root's pick, then each node's pick given its own parent layer
    std::vector<int> pick(n, -1);
    nodeBest(t.order[0], -1, &pick);
    for (int i = 1; i < n; ++i) {
      const int v = t.order[i];
      (*choice)[v] = pick[v];
      nodeBest(v, pick[v], &pick);
    }
  }
  return total;
}

// Vias of an assignment (the span at every node).
long long viasOf(const Problem &p, const NetTree &t, const std::vector<int> &choice) {
  long long v = 0;
  const int n = static_cast<int>(t.nodeX.size());
  for (int u = 0; u < n; ++u) {
    int lo = INT_MAX, hi = INT_MIN;
    if (choice[u] >= 0) {
      lo = std::min(lo, choice[u]);
      hi = std::max(hi, choice[u]);
    }
    for (int k : t.kids[u]) {
      lo = std::min(lo, choice[k]);
      hi = std::max(hi, choice[k]);
    }
    if (t.pinLo[u] >= 0) {
      lo = std::min(lo, t.pinLo[u]);
      hi = std::max(hi, t.pinHi[u]);
    }
    if (lo != INT_MAX)
      v += hi - lo;
  }
  (void)p;
  return v;
}

struct Result {
  double lower = 0;
  long long upper = LLONG_MAX; // best feasible assignment found (or the route's)
  int iters = 0;
};

// A feasible assignment from the prices: nets one at a time, each solved with the prices plus a near-prohibitive
// charge on every (edge, layer) the nets before it have filled; returns its vias, or -1 if one still overflows.
long long sequentialPrimal(const Problem &p, const std::vector<double> &mu) {
  const size_t nl = p.nl;
  std::vector<long long> res((p.nH + p.nV) * nl);
  for (size_t e = 0; e < p.nH + p.nV; ++e)
    for (size_t l = 0; l < nl; ++l)
      res[e * nl + l] = capOf(p, e, static_cast<int>(l));
  std::vector<double> price(mu.size());
  Scratch s;
  std::vector<int> ch;
  long long vias = 0;
  bool over = false;
  for (const NetTree &t : p.nets) {
    // price only this net's edges (the DP reads nothing else)
    for (size_t v = 1; v < t.nodeX.size(); ++v) {
      const size_t e = static_cast<size_t>(t.parentEdge[v]);
      for (size_t l = 0; l < nl; ++l)
        price[e * nl + l] = (mu.empty() ? 0.0 : mu[e * nl + l]) + (res[e * nl + l] < t.demandH ? 1e6 : 0.0);
    }
    solveNet(p, t, price, s, &ch);
    for (size_t v = 1; v < t.nodeX.size(); ++v) {
      const size_t i = static_cast<size_t>(t.parentEdge[v]) * nl + ch[v];
      res[i] -= t.demandH;
      over = over || res[i] < 0;
    }
    vias += viasOf(p, t, ch);
  }
  return over ? -1 : vias;
}

Result bound(Problem &p, int iters, int threads, bool trace) {
  auto t0 = std::chrono::steady_clock::now();
  const size_t nE = p.nH + p.nV, nl = p.nl;
  std::vector<double> mu(nE * nl, 0.0);
  std::vector<long long> use(nE * nl);
  Result r;
  r.upper = p.routeVias > 0 ? p.routeVias : LLONG_MAX;
  double theta = 1.0;
  int sinceGain = 0;
  std::vector<std::vector<int>> choice(p.nets.size());
  std::vector<double> cost(p.nets.size());
  for (int it = 0; it < iters; ++it) {
    std::atomic<size_t> next{0};
    auto work = [&]() {
      Scratch s;
      for (size_t k; (k = next++) < p.nets.size();)
        cost[k] = solveNet(p, p.nets[k], mu, s, &choice[k]);
    };
    std::vector<std::thread> th;
    for (int i = 0; i < threads; ++i)
      th.emplace_back(work);
    for (auto &x : th)
      x.join();
    double L = 0;
    for (double c : cost)
      L += c;
    std::fill(use.begin(), use.end(), 0);
    long long vias = 0;
    for (size_t k = 0; k < p.nets.size(); ++k) {
      const NetTree &t = p.nets[k];
      for (size_t v = 1; v < t.nodeX.size(); ++v)
        use[static_cast<size_t>(t.parentEdge[v]) * nl + choice[k][v]] += t.demandH;
      vias += viasOf(p, t, choice[k]);
    }
    double g2 = 0;
    long long over = 0;
    std::vector<double> grad(nE * nl);
    for (size_t e = 0; e < nE; ++e)
      for (size_t l = 0; l < nl; ++l) {
        const double cap = capOf(p, e, static_cast<int>(l));
        L -= mu[e * nl + l] * cap;
        double gr = static_cast<double>(use[e * nl + l]) - cap;
        if (use[e * nl + l] > cap)
          over += use[e * nl + l] - static_cast<long long>(cap);
        if (mu[e * nl + l] <= 0 && gr < 0)
          gr = 0; // projected: a zero price cannot fall
        grad[e * nl + l] = gr;
        g2 += gr * gr;
      }
    if (L > r.lower + 1e-6) {
      r.lower = L;
      sinceGain = 0;
    } else if (++sinceGain >= 10) {
      theta *= 0.5;
      sinceGain = 0;
    }
    if (over == 0 && vias < r.upper)
      r.upper = vias;
    if (it % 10 == 9 || it + 1 == iters) { // a feasible assignment from these prices
      const long long pv = sequentialPrimal(p, mu);
      if (pv >= 0 && pv < r.upper)
        r.upper = pv;
    }
    r.iters = it + 1;
    if (trace)
      std::cerr << "iter " << it << ": bound at these prices " << static_cast<long long>(L) << " (best "
                << static_cast<long long>(r.lower) << "), assignment vias " << vias << " overflow " << over
                << ", upper " << r.upper << ", theta " << theta << " ("
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s)\n";
    if (g2 <= 0 || r.upper - r.lower < 1)
      break;
    // Polyak step toward a target: the best feasible value when one is known, else an estimate above the bound
    const double target = r.upper != LLONG_MAX ? static_cast<double>(r.upper) : 1.2 * std::max(r.lower, L) + 10.0;
    const double step = theta * std::max(1e-9, target - L) / g2;
    for (size_t i = 0; i < mu.size(); ++i)
      mu[i] = std::max(0.0, mu[i] + step * grad[i]);
  }
  return r;
}

// Loads the design and the route (projected to 2D); nets above 1000 pins and nets without wire are left out.
bool load(const std::string &grPath, const std::string &routePath, Problem &p, std::string &err) {
  Design d = readGr(grPath);
  const RoutingGrid &g = d.grid;
  for (int l = 1; l < g.nLayers; ++l)
    if (g.minSpacing[l] != g.minSpacing[0]) {
      err = "layers differ in minimum spacing (one demand per net assumed)";
      return false;
    }
  p.nl = g.nLayers;
  p.nx = g.nx;
  p.ny = g.ny;
  for (int l = 0; l < g.nLayers; ++l) {
    if (g.hCapLayer[l] > 0)
      p.hl.push_back(l);
    if (g.vCapLayer[l] > 0)
      p.vl.push_back(l);
  }
  p.nH = static_cast<size_t>(g.ny) * (g.nx - 1);
  p.nV = static_cast<size_t>(g.ny - 1) * g.nx;
  p.capH.assign(p.nH * p.nl, 0);
  p.capV.assign(p.nV * p.nl, 0);
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        p.capH[hEdge(p, x, y) * p.nl + l] = g.hcap[g.hIndex(x, y, l)];
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        p.capV[(vEdge(p, x, y) - p.nH) * p.nl + l] = g.vcap[g.vIndex(x, y, l)];
  }
  std::unordered_map<std::string, int> byName;
  for (size_t i = 0; i < d.nets.size(); ++i)
    byName[d.nets[i].name] = static_cast<int>(i);
  std::ifstream in(routePath);
  if (!in) {
    err = "cannot read " + routePath;
    return false;
  }
  std::string line;
  int cur = -1;
  std::set<std::pair<char, std::pair<int, int>>> edges;
  long long cycles = 0;
  auto flush = [&]() {
    if (cur < 0 || edges.empty())
      return;
    const Net &n = d.nets[cur];
    if (n.pins.size() > 1000)
      return;
    std::map<std::pair<int, int>, std::pair<int, int>> pins;
    for (const Pin &pn : n.pins) {
      auto it = pins.find({pn.x, pn.y});
      if (it == pins.end())
        pins[{pn.x, pn.y}] = {pn.layer, pn.layer};
      else
        it->second = {std::min(it->second.first, pn.layer), std::max(it->second.second, pn.layer)};
    }
    NetTree t;
    t.net = cur;
    t.demandH = n.minWidth + g.minSpacing[0];
    std::vector<std::pair<char, std::pair<int, int>>> es(edges.begin(), edges.end());
    if (buildTree(p, es, pins, t))
      p.nets.push_back(std::move(t));
    else
      ++cycles;
  };
  while (std::getline(in, line)) {
    if (line.empty())
      continue;
    if (line[0] == '!') {
      flush();
      cur = -1;
      edges.clear();
      continue;
    }
    if (line[0] == '(') {
      long long x1, y1, l1, x2, y2, l2;
      if (std::sscanf(line.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        continue;
      if (l1 != l2) {
        p.routeVias += std::llabs(l2 - l1);
        continue;
      }
      int a = g.colOf(x1), b = g.rowOf(y1), c = g.colOf(x2), e = g.rowOf(y2);
      if (b == e)
        for (int x = std::min(a, c); x < std::max(a, c); ++x)
          edges.insert({1, {x, b}});
      else
        for (int y = std::min(b, e); y < std::max(b, e); ++y)
          edges.insert({0, {a, y}});
      continue;
    }
    std::istringstream hs(line);
    std::string name;
    hs >> name;
    auto it = byName.find(name);
    cur = it == byName.end() ? -1 : it->second;
  }
  flush();
  if (cycles)
    std::cerr << "load: " << cycles << " nets left out (their 2D wire is not a tree)\n";
  return true;
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

int failures = 0;
void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}

// A small problem by hand: a row of tiles, layers 0 (H) 1 (V) 2 (H).
Problem rowProblem(int nx, int cap0, int cap2) {
  Problem p;
  p.nl = 3;
  p.nx = nx;
  p.ny = 1;
  p.hl = {0, 2};
  p.vl = {1};
  p.nH = static_cast<size_t>(nx - 1);
  p.nV = 0;
  p.capH.assign(p.nH * 3, 0);
  for (size_t e = 0; e < p.nH; ++e) {
    p.capH[e * 3 + 0] = cap0;
    p.capH[e * 3 + 2] = cap2;
  }
  return p;
}

void addStraight(Problem &p, int x0, int x1, int pinLayer) {
  std::vector<std::pair<char, std::pair<int, int>>> es;
  for (int x = x0; x < x1; ++x)
    es.push_back({1, {x, 0}});
  std::map<std::pair<int, int>, std::pair<int, int>> pins{{{x0, 0}, {pinLayer, pinLayer}}, {{x1, 0}, {pinLayer, pinLayer}}};
  NetTree t;
  t.demandH = 2;
  buildTree(p, es, pins, t);
  p.nets.push_back(t);
}

void testUncapacitated() {
  Problem p = rowProblem(5, 100, 100);
  addStraight(p, 0, 4, 0);
  Scratch s;
  std::vector<int> ch;
  double c = solveNet(p, p.nets[0], {}, s, &ch);
  check(c == 0 && viasOf(p, p.nets[0], ch) == 0, "a straight net with pins on layer 0 and room on layer 0: 0 vias");
}

// Two nets on a row whose layer 0 holds one net: one net must go to layer 2 -- 2 vias at each end, 4 in all. The
// bound must reach 4 (never above), and the iterations find the feasible assignment with 4.
void testForcedUp() {
  Problem p = rowProblem(5, 2, 2);
  addStraight(p, 0, 4, 0);
  addStraight(p, 0, 4, 0);
  Result r = bound(p, 200, 1, false);
  // vias are whole numbers, so the bound rounded up is itself a certificate
  check(r.lower <= 4 + 1e-6 && std::ceil(r.lower - 1e-6) == 4 && r.upper == 4,
        "two nets, layer 0 holds one: bound " + std::to_string(r.lower) + " (rounded up 4) meets the feasible " +
            std::to_string(r.upper) + " -- optimum certified");
}

// Switching part-way: a net whose layer 0 is full on its middle edges only does best by going up for the middle
// (4 vias) rather than staying up all along (also 4) -- check the unit-edge DP prices a part-way switch exactly:
// layer 0 blocked (capacity 0) on edge 2 only, pins on layer 0, 0..4.
void testPartwaySwitch() {
  Problem p = rowProblem(5, 100, 100);
  p.capH[2 * 3 + 0] = 0;
  addStraight(p, 0, 4, 0);
  std::vector<double> mu(p.nH * 3, 0.0);
  mu[2 * 3 + 0] = 1000; // a huge price on the blocked edge's layer 0
  Scratch s;
  std::vector<int> ch;
  double c = solveNet(p, p.nets[0], mu, s, &ch);
  check(std::fabs(c - 4) < 1e-9 && viasOf(p, p.nets[0], ch) == 4 && ch[3] == 2,
        "a blocked middle edge: up and back down around it, 4 vias (got " + std::to_string(c) + ")");
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest") {
    std::cout << "self test\n";
    testUncapacitated();
    testForcedUp();
    testPartwaySwitch();
    std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
    return failures == 0 ? 0 : 1;
  }
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <design.gr> <route> [--iters N] [--threads T]\n       " << argv[0]
              << " --selftest\n";
    return 2;
  }
  int iters = 200, threads = 8;
  for (int i = 3; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--iters")
      iters = std::atoi(argv[i + 1]);
    else if (a == "--threads")
      threads = std::atoi(argv[i + 1]);
    else {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
  }
  Problem p;
  std::string err;
  if (!load(argv[1], argv[2], p, err)) {
    std::cerr << err << "\n";
    return 2;
  }
  std::cerr << "labound: " << p.nets.size() << " nets, " << p.nl << " layers, route vias " << p.routeVias << "\n";
  Result r = bound(p, iters, threads, true);
  std::cout << "certified lower bound on vias (this 2D route, no overflow): " << static_cast<long long>(std::ceil(r.lower - 1e-6))
            << "\nbest feasible assignment's vias (upper): " << r.upper << "\niterations " << r.iters << "\n";
  return 0;
}
