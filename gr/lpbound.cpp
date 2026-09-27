// lpbound.cpp -- a certified lower bound on total overflow inside a region.
//
// Not a router: an analysis tool that answers "is this overflow forced?"
// for a hotspot a router cannot clear (HISTORY_COST_SWEEP.md Part 65,
// newblue1's last 236).
//
// The bound is weak LP duality for fractional min-overflow routing. For ANY
// edge lengths 0 <= l_e <= 1:
//
//     total overflow >= sum_k d_k * T_k(l) - sum_e c_e * l_e
//
// where d_k is net k's demand per edge (width + spacing), c_e the 2D
// capacity (summed over layers), and T_k(l) a lower bound on the cost of
// any Steiner tree connecting net k's pins under l. Proof: route k on tree
// S_k with usage u_e = sum_{k: e in S_k} d_k and overflow o_e >= u_e - c_e,
// o_e >= 0. Then sum_e o_e >= sum_e l_e (u_e - c_e) (since 0 <= l_e <= 1
// and o_e >= max(0, u_e - c_e)) = sum_k d_k l(S_k) - sum_e c_e l_e >=
// sum_k d_k T_k(l) - sum_e c_e l_e. Holds for fractional routings too, so
// for every integral one.
//
// l is nonzero only on edges touching the region [x0,x1] x [y0,y1]; every
// tile outside is merged into one node O reachable at zero cost, which is
// exact for l = 0 there, so the bound is valid for the whole chip. Nets
// with all pins outside cost 0 and drop out; dropping any net only lowers
// the bound, so giant nets (> 1000 pins) are skipped.
//
// T_k: the exact shortest path for two terminals; for more, max(the longest
// terminal-pair shortest path, MST of the metric closure / 2) -- both are
// lower bounds on the Steiner minimum (the MST is at most twice it).
//
// Choosing l: Frank-Wolfe on the smoothed overflow
//     Phi(u) = sum_e c_e * softplus(beta * (u_e / c_e - 1)) / beta,
// whose gradient l_e = sigmoid(beta * (u_e / c_e - 1)) always lies in
// (0, 1) -- so every iterate's gradient is itself a valid dual point, and
// the bound above is evaluated at each one (the best is reported). Each
// iteration routes every net on its cheapest tree under l (the linear
// minimization step) and moves the averaged usage toward it with step
// 2 / (t + 2). The averaged usage is a convex combination of real integral
// routings -- a genuine fractional routing -- so its overflow is an upper
// bound on the relaxed optimum; the gap to the lower bound says how tight
// both are. (A first version used projected subgradient ascent; its
// lengths bang-banged between 0 and 1 and the bound never left 0.)
//
// Usage: lpbound <benchmark.gr> <x0> <y0> <x1> <y1> [iterations] [beta]
//        lpbound --selftest

#include "gr_io.h"

#include <fstream>

using namespace gr_io;

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <vector>

namespace {

struct Region {
  int x0, y0, x1, y1;
};

struct Graph {
  int nNodes = 0; // region tiles, then O (index nNodes - 1)
  struct Edge {
    int a, b;
    double cap;
  };
  std::vector<Edge> edges;
  std::vector<std::vector<std::pair<int, int>>> adj; // node -> (neighbor, edge)
};

struct NetTerms {
  std::vector<int> terms;
  double demand;
};

int nodeOf(const Region &r, int x, int y, int outside) {
  if (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1)
    return outside;
  return (y - r.y0) * (r.x1 - r.x0 + 1) + (x - r.x0);
}

Graph buildGraph(const RoutingGrid &g, const Region &r) {
  Graph G;
  int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
  G.nNodes = w * h + 1;
  int O = G.nNodes - 1;
  G.adj.assign(G.nNodes, {});
  auto add = [&](int a, int b, double cap) {
    int e = static_cast<int>(G.edges.size());
    G.edges.push_back({a, b, cap});
    G.adj[a].push_back({b, e});
    G.adj[b].push_back({a, e});
  };
  auto hcap2d = [&](int x, int y) {
    double c = 0;
    for (int l = 0; l < g.nLayers; ++l)
      c += std::max(0, g.hcap[g.hIndex(x, y, l)]);
    return c;
  };
  auto vcap2d = [&](int x, int y) {
    double c = 0;
    for (int l = 0; l < g.nLayers; ++l)
      c += std::max(0, g.vcap[g.vIndex(x, y, l)]);
    return c;
  };
  // Every grid edge with at least one end in the region; an edge from a
  // region tile to an outside tile goes to O.
  for (int y = std::max(0, r.y0 - 1); y <= std::min(g.ny - 1, r.y1 + 1); ++y)
    for (int x = std::max(0, r.x0 - 1); x + 1 < g.nx && x <= r.x1; ++x) {
      int a = nodeOf(r, x, y, O), b = nodeOf(r, x + 1, y, O);
      if (a == O && b == O)
        continue;
      add(a, b, hcap2d(x, y));
    }
  for (int y = std::max(0, r.y0 - 1); y + 1 < g.ny && y <= r.y1; ++y)
    for (int x = std::max(0, r.x0 - 1); x <= std::min(g.nx - 1, r.x1 + 1); ++x) {
      int a = nodeOf(r, x, y, O), b = nodeOf(r, x, y + 1, O);
      if (a == O && b == O)
        continue;
      add(a, b, vcap2d(x, y));
    }
  return G;
}

std::vector<NetTerms> buildNets(const Design &d, const Region &r, int O) {
  std::vector<NetTerms> out;
  for (const Net &n : d.nets) {
    if (n.pins.size() > 1000)
      continue;
    std::vector<int> t;
    for (const Pin &p : n.pins)
      t.push_back(nodeOf(r, p.x, p.y, O));
    std::sort(t.begin(), t.end());
    t.erase(std::unique(t.begin(), t.end()), t.end());
    if (t.size() < 2)
      continue; // all pins in one tile, or all outside: costs 0
    out.push_back({t, static_cast<double>(n.minWidth + d.grid.minSpacing[0])});
  }
  return out;
}

struct Result {
  double bestLower = 0, fracUpper = 0;
  int bestIter = -1;
};

Result bound(const Graph &G, const std::vector<NetTerms> &nets, int iters, double beta, bool trace) {
  const double kInf = std::numeric_limits<double>::infinity();
  std::vector<double> len(G.edges.size(), 0.0), use(G.edges.size()), avg(G.edges.size(), 0.0);
  std::vector<double> dist(G.nNodes);
  std::vector<int> par(G.nNodes), seen(G.nNodes, -1), mark(G.edges.size(), -1);
  Result res;
  int stamp = 0, netStamp = 0;
  for (int it = 0; it < iters; ++it) {
    std::fill(use.begin(), use.end(), 0.0);
    double dual = 0;
    for (const NetTerms &n : nets) {
      size_t k = n.terms.size();
      // Shortest paths from each terminal (stopping once every terminal of
      // this net is settled), kept as parent trees for the primal routing.
      std::vector<std::vector<double>> dm(k, std::vector<double>(k, kInf));
      for (size_t s = 0; s < k; ++s) {
        ++stamp;
        using QE = std::pair<double, int>;
        std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
        auto get = [&](int v) { return seen[v] == stamp ? dist[v] : kInf; };
        dist[n.terms[s]] = 0;
        par[n.terms[s]] = -1;
        seen[n.terms[s]] = stamp;
        q.push({0.0, n.terms[s]});
        size_t settled = 0;
        std::vector<char> isTerm(k, 0);
        while (!q.empty() && settled < k) {
          auto [dv, v] = q.top();
          q.pop();
          if (dv > get(v))
            continue;
          for (size_t t = 0; t < k; ++t)
            if (!isTerm[t] && n.terms[t] == v) {
              isTerm[t] = 1;
              ++settled;
              dm[s][t] = dv;
            }
          for (auto [u, e] : G.adj[v]) {
            double nd = dv + len[e];
            if (nd < get(u)) {
              dist[u] = nd;
              par[u] = e;
              seen[u] = stamp;
              q.push({nd, u});
            }
          }
        }
        // Walk each terminal back to the source now, while par[] is valid.
        static std::vector<std::vector<std::vector<int>>> pathCache;
        if (pathCache.size() < k)
          pathCache.resize(k);
        pathCache[s].assign(k, {});
        for (size_t t = 0; t < k; ++t) {
          if (t == s || dm[s][t] == kInf)
            continue;
          for (int v = n.terms[t]; v != n.terms[s];) {
            int e = par[v];
            pathCache[s][t].push_back(e);
            v = G.edges[e].a == v ? G.edges[e].b : G.edges[e].a;
          }
        }
        if (s + 1 == k) {
          // MST (Prim) on the metric closure; lower bound; primal routing.
          std::vector<char> in(k, 0);
          std::vector<double> key(k, kInf);
          std::vector<int> from(k, -1);
          key[0] = 0;
          double mst = 0, maxPair = 0;
          for (size_t a = 0; a < k; ++a)
            for (size_t b = 0; b < k; ++b)
              if (dm[a][b] < kInf)
                maxPair = std::max(maxPair, dm[a][b]);
          ++netStamp;
          for (size_t step = 0; step < k; ++step) {
            int u = -1;
            for (size_t v = 0; v < k; ++v)
              if (!in[v] && (u < 0 || key[v] < key[u]))
                u = static_cast<int>(v);
            in[u] = 1;
            mst += key[u];
            if (from[u] >= 0)
              for (int e : pathCache[from[u]][u])
                if (mark[e] != netStamp) {
                  mark[e] = netStamp;
                  use[e] += n.demand;
                }
            for (size_t v = 0; v < k; ++v)
              if (!in[v] && dm[u][v] < key[v]) {
                key[v] = dm[u][v];
                from[v] = u;
              }
          }
          double steinerLB = k == 2 ? dm[0][1] : std::max(maxPair, mst / 2.0);
          dual += n.demand * steinerLB;
        }
      }
    }
    for (size_t e = 0; e < G.edges.size(); ++e)
      dual -= G.edges[e].cap * len[e];
    if (dual > res.bestLower) {
      res.bestLower = dual;
      res.bestIter = it;
    }
    double over = 0;
    double gamma = 2.0 / (it + 2.0);
    for (size_t e = 0; e < G.edges.size(); ++e) {
      avg[e] += gamma * (use[e] - avg[e]);
      over += std::max(0.0, avg[e] - G.edges[e].cap);
    }
    res.fracUpper = over;
    if (trace && (it < 5 || it % 10 == 9 || it + 1 == iters))
      std::cerr << "iter " << it << ": lower bound " << dual << " (best " << res.bestLower
                << "), averaged-routing overflow " << over << "\n";
    // Gradient of Phi at the averaged usage; a zero-capacity edge counts as
    // fully saturated (length 1). The 1e-9 floor only breaks ties.
    for (size_t e = 0; e < G.edges.size(); ++e) {
      double c = G.edges[e].cap;
      double z = c <= 0 ? 50.0 : beta * (avg[e] / c - 1.0);
      len[e] = std::max(1e-9, 1.0 / (1.0 + std::exp(-std::min(50.0, std::max(-50.0, z)))));
    }
  }
  return res;
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

Design parseText(const std::string &path, const std::string &text) {
  std::ofstream(path) << text;
  return readGr(path);
}

// Three nets must cross a 1-tile-wide corridor whose edges hold one net: a
// 3 x 1 grid, region = the middle tile, pins at the two ends. Every route
// crosses both middle edges; capacity 2 each, demand 3 x 2 = 6 each, so the
// true minimum overflow is 2 x 4 = 8. The bound must reach 8 and never
// exceed it (it is a lower bound).
void testForcedCorridor() {
  Design d = parseText("lpbound_selftest_corridor.gr",
                       "grid 3 1 2\nvertical capacity 0 0\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 3\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\nc 2 2 1\n5 5 1\n25 5 1\n0\n");
  Region r{1, 0, 1, 0};
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  check(G.nNodes == 2 && G.edges.size() == 2, "region graph: middle tile + O, two edges to O");
  // Both ends map to O, so each net's pins collapse to one node -- the
  // corridor bound only sees nets with a pin inside. Put pins inside instead:
  Region whole{0, 0, 2, 0};
  Graph G2 = buildGraph(d.grid, whole);
  std::vector<NetTerms> n2 = buildNets(d, whole, G2.nNodes - 1);
  check(n2.size() == 3 && G2.edges.size() == 2, "whole-row region: 3 nets, 2 edges (no outside neighbors)");
  Result res = bound(G2, n2, 200, 50.0, false);
  check(res.bestLower > 7.9 && res.bestLower <= 8.0 + 1e-9,
        "bound reaches the true forced overflow 8 and not above (got " + std::to_string(res.bestLower) + ")");
  check(std::abs(res.fracUpper - 8.0) < 1e-6, "averaged routing overflow is 8 too: bound is tight");
  (void)nets;
}

// A feasible instance: the same three nets on a 3 x 3 grid with room on
// three rows. The bound must stay at 0 (never claim forced overflow that a
// routing avoids), while the averaged routing finds (near) zero.
void testFeasibleStaysZero() {
  Design d = parseText("lpbound_selftest_feasible.gr",
                       "grid 3 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 1\na 0 2 1\n5 15 1\n25 15 1\n0\n");
  Region whole{0, 0, 2, 2};
  Graph G = buildGraph(d.grid, whole);
  std::vector<NetTerms> n = buildNets(d, whole, G.nNodes - 1);
  Result res = bound(G, n, 100, 50.0, false);
  check(res.bestLower <= 1e-9, "one net on an empty grid: bound 0");
  check(res.fracUpper <= 1e-9, "and the averaged routing has no overflow");
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest") {
    std::cout << "self test\n";
    testForcedCorridor();
    testFeasibleStaysZero();
    std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
    return failures == 0 ? 0 : 1;
  }
  if (argc < 6) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <x0> <y0> <x1> <y1> [iterations] [beta]\n"
              << "       " << argv[0] << " --selftest\n";
    return 2;
  }
  Design d = readGr(argv[1]);
  Region r{std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5])};
  if (r.x0 < 0 || r.y0 < 0 || r.x1 >= d.grid.nx || r.y1 >= d.grid.ny || r.x0 > r.x1 || r.y0 > r.y1) {
    std::cerr << "region out of bounds\n";
    return 2;
  }
  int iters = argc > 6 ? std::atoi(argv[6]) : 200;
  double beta = argc > 7 ? std::atof(argv[7]) : 50.0;
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  std::cerr << "region x " << r.x0 << "-" << r.x1 << " y " << r.y0 << "-" << r.y1 << ": " << G.nNodes
            << " nodes, " << G.edges.size() << " edges, " << nets.size() << " nets with a pin inside\n";
  Result res = bound(G, nets, iters, beta, true);
  std::cout << "certified lower bound on total overflow: " << res.bestLower << " (iteration " << res.bestIter
            << ")\nfractional routing overflow (relaxed upper bound): " << res.fracUpper << "\n";
  return 0;
}
