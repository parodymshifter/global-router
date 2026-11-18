// cgbound.cpp -- a certified lower bound on total overflow around a hotspot, from the exact fractional relaxation.
//
// Not a router: an analysis tool (HISTORY_COST_SWEEP.md Part 213) that answers "how much of this overflow is
// forced?" more tightly than gr/cutbound.cpp (the cut condition) and gr/lpbound.cpp (the same duality bound as
// here, but at Frank-Wolfe lengths and with MST/2 Steiner bounds -- too weak to certify anything on newblue1,
// Part 65).
//
// The bound (weak LP duality for fractional min-overflow Steiner packing, as in lpbound.cpp): for ANY edge lengths
// 0 <= l_e <= 1,
//
//     total overflow >= sum_k d_k * T_k(l) - sum_e c_e * l_e,
//
// with d_k net k's demand per edge (minimum width + spacing), c_e the 2D capacity (summed over layers) and T_k(l) a
// lower bound on the cost under l of any Steiner tree joining net k's pins. Region tiles are kept, every tile
// outside merges into one node O reachable at zero cost (so the bound holds for the whole chip: a real routing
// projects onto a connected subgraph of this graph, with no more usage on any kept edge). Real (3D) overflow >= 2D
// overflow >= this. Nets above 1000 pins are left out (ISPD 2008 exempts them; dropping nets only lowers the bound).
//
// What is new here:
//   * the lengths are the optimal duals of the fractional problem itself, solved exactly by column generation: a
//     master LP (HiGHS, in memory) over generated trees per net -- min sum_e o_e, sum_j lambda_kj = 1 per net,
//     sum_kj d_k [e in S_kj] lambda_kj - o_e <= c_e per edge -- whose capacity-row duals give l, and whose value is
//     the overflow of a genuine fractional routing (an upper bound on the relaxed optimum);
//   * T_k is an exact Steiner tree (Dreyfus-Wagner, on the region graph) for nets with up to --exact pin nodes, the
//     same tree is the priced column, so pricing is exact for those nets; for larger nets T_k is the exact Steiner
//     tree of a subset of --exact of their pin nodes (still a valid lower bound: a tree joining all pins joins the
//     subset) and the column is the shortest-path heuristic tree.
// When no net has a cheaper tree than its columns and every net is priced exactly, the bound equals the master
// value: the fractional optimum, certified.
//
// Dependency: the HiGHS C++ library (MIT licence; Homebrew `highs`, /opt/homebrew), allowed by CLAUDE.md's
// conventions for tools that need it -- this file only; no router links it. Build:
//   g++ -std=c++17 -O2 -Wall -Wextra -pthread -isystem /opt/homebrew/include/highs -o cgbound cgbound.cpp \
//       -L/opt/homebrew/lib -lhighs -Wl,-rpath,/opt/homebrew/lib
//
// Usage: cgbound <benchmark.gr> <x0> <y0> <x1> <y1> [--rounds N] [--time S] [--exact K] [--threads T]
//                [--solver ipm|simplex] [--seed ROUTE] [--export FILE]
//                (--export: each net's heaviest tree in the last master, for gr/cg_merge.py;
//                 --seed: start the master from a route file's trees;
//                ipm, the default: interior point without crossover -- each master solve
//                from scratch, but central duals; simplex warm-starts but its vertex duals swing, and on newblue7
//                its fourth solve took 38 minutes)
//        cgbound --selftest

#include "gr_io.h"

#include "Highs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <cstdio>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace gr_io;

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kEps = 1e-11; // tie-break length on every edge: no zero-length cycles; corrected out of the bound

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
  int w = 0;                                          // region width (for node coordinates)
  std::unordered_map<long long, int> gridEdge;        // (x, y, dir) of a chip edge -> its region edge (--seed)
  std::vector<std::array<int, 3>> edgeXY;             // region edge -> (x, y, dir) of its chip edge (--export)
  static long long key(int x, int y, int dir) { return (static_cast<long long>(y) << 32) | (x << 1) | dir; }
};

struct NetTerms {
  std::vector<int> terms; // distinct nodes, sorted
  double demand;
  std::string name;
};

int nodeOf(const Region &r, int x, int y, int outside) {
  if (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1)
    return outside;
  return (y - r.y0) * (r.x1 - r.x0 + 1) + (x - r.x0);
}

Graph buildGraph(const RoutingGrid &g, const Region &r) {
  Graph G;
  int w = r.x1 - r.x0 + 1, h = r.y1 - r.y0 + 1;
  G.w = w;
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
  for (int y = std::max(0, r.y0 - 1); y <= std::min(g.ny - 1, r.y1 + 1); ++y)
    for (int x = std::max(0, r.x0 - 1); x + 1 < g.nx && x <= r.x1; ++x) {
      int a = nodeOf(r, x, y, O), b = nodeOf(r, x + 1, y, O);
      if (a == O && b == O)
        continue;
      G.gridEdge[Graph::key(x, y, 0)] = static_cast<int>(G.edges.size());
      G.edgeXY.push_back({x, y, 0});
      add(a, b, hcap2d(x, y));
    }
  for (int y = std::max(0, r.y0 - 1); y + 1 < g.ny && y <= r.y1; ++y)
    for (int x = std::max(0, r.x0 - 1); x <= std::min(g.nx - 1, r.x1 + 1); ++x) {
      int a = nodeOf(r, x, y, O), b = nodeOf(r, x, y + 1, O);
      if (a == O && b == O)
        continue;
      G.gridEdge[Graph::key(x, y, 1)] = static_cast<int>(G.edges.size());
      G.edgeXY.push_back({x, y, 1});
      add(a, b, vcap2d(x, y));
    }
  return G;
}

std::vector<NetTerms> buildNets(const Design &d, const Region &r, int O) {
  int sp = d.grid.minSpacing[0];
  for (int l = 1; l < d.grid.nLayers; ++l)
    sp = std::min(sp, d.grid.minSpacing[l]);
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
      continue;
    out.push_back({t, static_cast<double>(n.minWidth + sp), n.name});
  }
  return out;
}

// --seed: a route file's nets projected onto the region graph (their wire edges with at least one end in the
// region; vias and outside wire dropped), one starting column per net -- a real routing projects to a connected
// subgraph holding the net's nodes, so each is a valid column and the master starts at that routing's overflow
// instead of at shortest trees piled into the hotspot (newblue3: 150922 after round 0, still 95960 after 22).
// Returns per net (index into nets) its edges; nets the file lacks get none. A .gz file is read through gzip -dc.
std::vector<std::vector<int>> readSeed(const std::string &path, const RoutingGrid &g, const Graph &G,
                                       const std::vector<NetTerms> &nets, size_t &found) {
  std::unordered_map<std::string, size_t> byName;
  for (size_t k = 0; k < nets.size(); ++k)
    byName[nets[k].name] = k;
  std::vector<std::vector<int>> out(nets.size());
  bool gz = path.size() > 3 && path.compare(path.size() - 3, 3, ".gz") == 0;
  FILE *f = gz ? popen(("gzip -dc '" + path + "'").c_str(), "r") : std::fopen(path.c_str(), "r");
  if (!f) {
    std::cerr << "cannot read " << path << "\n";
    std::exit(2);
  }
  found = 0;
  long cur = -1;
  char buf[512];
  while (std::fgets(buf, sizeof buf, f)) {
    if (buf[0] == '!') {
      cur = -1;
      continue;
    }
    if (buf[0] == '(') {
      if (cur < 0)
        continue;
      long long x1, y1, l1, x2, y2, l2;
      if (std::sscanf(buf, "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6 || l1 != l2)
        continue;
      int a = g.colOf(x1), b = g.rowOf(y1), c = g.colOf(x2), e = g.rowOf(y2);
      if (b == e)
        for (int x = std::min(a, c); x < std::max(a, c); ++x) {
          auto it = G.gridEdge.find(Graph::key(x, b, 0));
          if (it != G.gridEdge.end())
            out[cur].push_back(it->second);
        }
      else if (a == c)
        for (int y = std::min(b, e); y < std::max(b, e); ++y) {
          auto it = G.gridEdge.find(Graph::key(a, y, 1));
          if (it != G.gridEdge.end())
            out[cur].push_back(it->second);
        }
      continue;
    }
    std::istringstream hs(buf);
    std::string name;
    hs >> name;
    auto it = byName.find(name);
    cur = it == byName.end() ? -1 : static_cast<long>(it->second);
    if (cur >= 0)
      ++found;
  }
  if (gz)
    pclose(f);
  else
    std::fclose(f);
  for (auto &v : out) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  }
  return out;
}

// Per-thread scratch for the Steiner solvers.
struct Scratch {
  std::vector<double> dp;
  std::vector<int> back;
  std::vector<double> dist;
  std::vector<int> par;
  std::vector<char> inTree;
};

const int kBase = INT_MIN;

// Dreyfus-Wagner: the exact minimum Steiner tree joining terms under len (every len > 0). Returns its cost; if
// tree is non-null, its edges.
double dreyfusWagner(const Graph &G, const std::vector<double> &len, const std::vector<int> &terms, Scratch &s,
                     std::vector<int> *tree) {
  const int n = G.nNodes;
  const int q = static_cast<int>(terms.size()) - 1; // terms[0] is the root
  if (q <= 0)
    return 0;
  const int full = (1 << q) - 1;
  s.dp.assign(static_cast<size_t>(full + 1) * n, kInf);
  s.back.assign(static_cast<size_t>(full + 1) * n, kBase);
  using QE = std::pair<double, int>;
  for (int S = 1; S <= full; ++S) {
    double *dS = &s.dp[static_cast<size_t>(S) * n];
    int *bS = &s.back[static_cast<size_t>(S) * n];
    if ((S & (S - 1)) == 0) {
      int i = __builtin_ctz(S);
      dS[terms[i + 1]] = 0;
      bS[terms[i + 1]] = kBase;
    } else {
      int low = S & -S;
      for (int A = (S - 1) & S; A > 0; A = (A - 1) & S) {
        if (!(A & low))
          continue;
        const double *dA = &s.dp[static_cast<size_t>(A) * n];
        const double *dB = &s.dp[static_cast<size_t>(S ^ A) * n];
        for (int v = 0; v < n; ++v) {
          double val = dA[v] + dB[v];
          if (val < dS[v]) {
            dS[v] = val;
            bS[v] = -A;
          }
        }
      }
    }
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (int v = 0; v < n; ++v)
      if (dS[v] < kInf)
        pq.push({dS[v], v});
    while (!pq.empty()) {
      auto [dv, v] = pq.top();
      pq.pop();
      if (dv > dS[v])
        continue;
      for (auto [u, e] : G.adj[v]) {
        double nd = dv + len[e];
        if (nd < dS[u]) {
          dS[u] = nd;
          bS[u] = e;
          pq.push({nd, u});
        }
      }
    }
  }
  double best = s.dp[static_cast<size_t>(full) * n + terms[0]];
  if (tree) {
    tree->clear();
    std::vector<std::pair<int, int>> stack{{full, terms[0]}};
    while (!stack.empty()) {
      auto [S, v] = stack.back();
      stack.pop_back();
      for (;;) {
        int b = s.back[static_cast<size_t>(S) * n + v];
        if (b == kBase)
          break;
        if (b >= 0) {
          tree->push_back(b);
          v = G.edges[b].a == v ? G.edges[b].b : G.edges[b].a;
          continue;
        }
        int A = -b;
        stack.push_back({A, v});
        stack.push_back({S ^ A, v});
        break;
      }
    }
    std::sort(tree->begin(), tree->end());
    tree->erase(std::unique(tree->begin(), tree->end()), tree->end());
  }
  return best;
}

// The shortest-path heuristic: grow a tree from terms[0], each time joining the nearest unjoined terminal by a
// shortest path from the whole tree. Returns its edges.
void shortestPathTree(const Graph &G, const std::vector<double> &len, const std::vector<int> &terms, Scratch &s,
                      std::vector<int> &tree) {
  const int n = G.nNodes;
  tree.clear();
  s.inTree.assign(n, 0);
  s.dist.assign(n, kInf);
  s.par.assign(n, -1);
  std::vector<char> isTerm(n, 0);
  for (int t : terms)
    isTerm[t] = 1;
  s.inTree[terms[0]] = 1;
  size_t joined = 1;
  using QE = std::pair<double, int>;
  while (joined < terms.size()) {
    std::fill(s.dist.begin(), s.dist.end(), kInf);
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    for (int v = 0; v < n; ++v)
      if (s.inTree[v]) {
        s.dist[v] = 0;
        s.par[v] = -1;
        pq.push({0.0, v});
      }
    int hit = -1;
    while (!pq.empty()) {
      auto [dv, v] = pq.top();
      pq.pop();
      if (dv > s.dist[v])
        continue;
      if (isTerm[v] && !s.inTree[v]) {
        hit = v;
        break;
      }
      for (auto [u, e] : G.adj[v]) {
        double nd = dv + len[e];
        if (nd < s.dist[u]) {
          s.dist[u] = nd;
          s.par[u] = e;
          pq.push({nd, u});
        }
      }
    }
    if (hit < 0)
      break; // disconnected region graph (cannot happen: every node reaches O or a neighbour)
    for (int v = hit; !s.inTree[v];) {
      int e = s.par[v];
      tree.push_back(e);
      s.inTree[v] = 1;
      if (isTerm[v])
        ++joined;
      v = G.edges[e].a == v ? G.edges[e].b : G.edges[e].a;
    }
  }
  std::sort(tree.begin(), tree.end());
  tree.erase(std::unique(tree.begin(), tree.end()), tree.end());
}

// A subset of `k` terminals for a net's lower bound: O first if present, then farthest-point by tile distance.
std::vector<int> spreadSubset(const Graph &G, const std::vector<int> &terms, int k) {
  const int O = G.nNodes - 1;
  std::vector<int> pick;
  std::vector<int> rest;
  for (int t : terms)
    (t == O ? pick : rest).push_back(t);
  auto dist = [&](int a, int b) {
    return std::abs(a % G.w - b % G.w) + std::abs(a / G.w - b / G.w);
  };
  if (pick.empty() && !rest.empty()) {
    pick.push_back(rest[0]);
    rest.erase(rest.begin());
  }
  while (static_cast<int>(pick.size()) < k && !rest.empty()) {
    size_t bi = 0;
    int bd = -1;
    for (size_t i = 0; i < rest.size(); ++i) {
      int dmin = INT_MAX;
      for (int p : pick)
        if (p != O)
          dmin = std::min(dmin, dist(p, rest[i]));
      if (dmin == INT_MAX)
        dmin = 0;
      if (dmin > bd) {
        bd = dmin;
        bi = i;
      }
    }
    pick.push_back(rest[bi]);
    rest.erase(rest.begin() + static_cast<long>(bi));
  }
  return pick;
}

struct Settings {
  int rounds = 200;
  double timeLimit = 36000;
  int exact = 7;
  int threads = 8;
  bool trace = true;
  double gapTol = 0.5; // stop once the certificate is this close to the fractional routing
  bool ipm = true; // interior point without crossover: central duals, which stabilize column generation
};

struct Result {
  double lower = 0;    // best certified lower bound
  double master = kInf; // last master value (fractional routing overflow)
  int rounds = 0;
  bool converged = false; // no cheaper column for any net
  bool allExact = false;  // every net priced exactly (then lower == master at convergence)
  std::vector<std::vector<int>> heaviest; // per net, the edges of its heaviest column in the last solved master
};

Result solve(const Graph &G, const std::vector<NetTerms> &nets, const Settings &st,
             const std::vector<std::vector<int>> *seed = nullptr) {
  auto t0 = std::chrono::steady_clock::now();
  const size_t E = G.edges.size(), K = nets.size();
  Result res;
  res.allExact = std::all_of(nets.begin(), nets.end(),
                             [&](const NetTerms &n) { return static_cast<int>(n.terms.size()) <= st.exact; });
  Highs h;
  h.setOptionValue("output_flag", false);
  if (st.ipm) {
    h.setOptionValue("solver", "ipm");
    h.setOptionValue("run_crossover", "off");
  } else {
    h.setOptionValue("threads", 1);
  }
  std::vector<int> edgeRow(E, -1);
  int nRows = 0, nCols = 0;
  // convexity rows
  for (size_t k = 0; k < K; ++k) {
    h.addRow(1.0, 1.0, 0, nullptr, nullptr);
    ++nRows;
  }
  std::vector<std::vector<std::vector<int>>> cols(K); // net -> its columns' edges
  std::vector<std::vector<int>> colIndex(K);          // net -> its columns' HiGHS indices
  res.heaviest.assign(K, {});
  auto ensureRow = [&](int e) {
    if (edgeRow[e] >= 0)
      return;
    h.addCol(1.0, 0.0, kHighsInf, 0, nullptr, nullptr); // o_e
    HighsInt oc = nCols++;
    double m1 = -1.0;
    h.addRow(-kHighsInf, G.edges[e].cap, 1, &oc, &m1);
    edgeRow[e] = nRows++;
  };
  auto addColumn = [&](size_t k, const std::vector<int> &tree) {
    for (int e : tree)
      ensureRow(e);
    std::vector<HighsInt> idx{static_cast<HighsInt>(k)};
    std::vector<double> val{1.0};
    for (int e : tree) {
      idx.push_back(edgeRow[e]);
      val.push_back(nets[k].demand);
    }
    h.addCol(0.0, 0.0, kHighsInf, static_cast<HighsInt>(idx.size()), idx.data(), val.data());
    colIndex[k].push_back(nCols);
    ++nCols;
    cols[k].push_back(tree);
  };
  // initial columns: shortest-path trees under unit lengths
  {
    std::vector<double> unit(E, 1.0);
    std::vector<std::vector<int>> init(K);
    std::atomic<size_t> next{0};
    auto work = [&]() {
      Scratch s;
      for (size_t k; (k = next++) < K;)
        shortestPathTree(G, unit, nets[k].terms, s, init[k]);
    };
    std::vector<std::thread> th;
    for (int i = 0; i < st.threads; ++i)
      th.emplace_back(work);
    for (auto &t : th)
      t.join();
    for (size_t k = 0; k < K; ++k)
      addColumn(k, init[k]);
    if (seed)
      for (size_t k = 0; k < K; ++k)
        if (!(*seed)[k].empty() && (*seed)[k] != init[k])
          addColumn(k, (*seed)[k]);
  }
  std::vector<double> len(E), lenEps(E);
  std::vector<double> lb(K);
  std::vector<std::vector<int>> cand(K);
  std::vector<char> better(K);
  for (int r = 0; r < st.rounds; ++r) {
    h.run();
    if (st.ipm && h.getModelStatus() != HighsModelStatus::kOptimal) {
      // Interior point stopped short (newblue3 round 2: status Unknown, a wrong objective): re-solve this round
      // with simplex, then go back to interior point.
      h.setOptionValue("solver", "simplex");
      h.run();
      h.setOptionValue("solver", "ipm");
    }
    const HighsSolution &sol = h.getSolution();
    if (h.getModelStatus() != HighsModelStatus::kOptimal) {
      std::cerr << "master LP not solved (status " << static_cast<int>(h.getModelStatus()) << "); stopping\n";
      break;
    }
    res.master = h.getInfo().objective_function_value;
    for (size_t k = 0; k < K; ++k) { // --export: the heaviest column per net in this (the last solved) master
      size_t bj = 0;
      for (size_t j = 1; j < colIndex[k].size(); ++j)
        if (sol.col_value[colIndex[k][j]] > sol.col_value[colIndex[k][bj]])
          bj = j;
      res.heaviest[k] = cols[k][bj];
    }
    const std::string status = h.modelStatusToString(h.getModelStatus());
    const long long ipmIters = h.getInfo().ipm_iteration_count;
    for (size_t e = 0; e < E; ++e) {
      len[e] = edgeRow[e] >= 0 ? std::min(1.0, std::max(0.0, -sol.row_dual[edgeRow[e]])) : 0.0;
      lenEps[e] = len[e] + kEps;
    }
    std::atomic<size_t> next{0};
    auto work = [&]() {
      Scratch s;
      std::vector<int> tree;
      for (size_t k; (k = next++) < K;) {
        const std::vector<int> &T = nets[k].terms;
        double cost;
        if (static_cast<int>(T.size()) <= st.exact) {
          double v = dreyfusWagner(G, lenEps, T, s, &tree);
          lb[k] = std::max(0.0, v - kEps * G.nNodes);
          cost = 0;
          for (int e : tree)
            cost += len[e];
        } else {
          std::vector<int> sub = spreadSubset(G, T, st.exact);
          double v = dreyfusWagner(G, lenEps, sub, s, nullptr);
          lb[k] = std::max(0.0, v - kEps * G.nNodes);
          shortestPathTree(G, lenEps, T, s, tree);
          cost = 0;
          for (int e : tree)
            cost += len[e];
        }
        double cheapest = kInf;
        for (const auto &c : cols[k]) {
          double cc = 0;
          for (int e : c)
            cc += len[e];
          cheapest = std::min(cheapest, cc);
        }
        better[k] = cost < cheapest - 1e-9;
        if (better[k])
          cand[k] = tree;
      }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < st.threads; ++i)
      th.emplace_back(work);
    for (auto &t : th)
      t.join();
    double bound = 0;
    for (size_t k = 0; k < K; ++k)
      bound += nets[k].demand * lb[k];
    for (size_t e = 0; e < E; ++e)
      bound -= G.edges[e].cap * len[e];
    res.lower = std::max(res.lower, bound);
    size_t added = 0;
    for (size_t k = 0; k < K; ++k)
      if (better[k]) {
        addColumn(k, cand[k]);
        ++added;
      }
    res.rounds = r + 1;
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (st.trace)
      std::cerr << "round " << r << " [" << status << ", " << ipmIters << " ipm iterations]: master (fractional routing) " << res.master
                << ", bound at these lengths "
                << bound << " (best " << res.lower << "), " << added << " new trees, " << nRows - K
                << " edge rows, " << secs << " s\n";
    // Converged: no cheaper column anywhere, or the certificate within half a unit of the fractional routing
    // (overflow comes in whole units, so the remaining gap cannot hide one).
    if (added == 0 || res.master - res.lower < st.gapTol) {
      res.converged = true;
      break;
    }
    if (secs > st.timeLimit)
      break;
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
  Design d = readGr(path);
  std::remove(path.c_str());
  return d;
}

// Dreyfus-Wagner against brute force (every set of Steiner nodes: the MST of the induced subgraph, if connected)
// on a 3 x 3 grid with assorted lengths.
void testSteinerExact() {
  Graph G;
  G.w = 3;
  G.nNodes = 9;
  G.adj.assign(9, {});
  std::vector<double> len;
  unsigned seed = 7;
  auto rnd = [&]() {
    seed = seed * 1103515245u + 12345u;
    return 0.1 + (seed >> 16) % 100 / 25.0;
  };
  for (int y = 0; y < 3; ++y)
    for (int x = 0; x < 3; ++x) {
      int v = y * 3 + x;
      if (x + 1 < 3) {
        G.adj[v].push_back({v + 1, static_cast<int>(G.edges.size())});
        G.adj[v + 1].push_back({v, static_cast<int>(G.edges.size())});
        G.edges.push_back({v, v + 1, 1});
        len.push_back(rnd());
      }
      if (y + 1 < 3) {
        G.adj[v].push_back({v + 3, static_cast<int>(G.edges.size())});
        G.adj[v + 3].push_back({v, static_cast<int>(G.edges.size())});
        G.edges.push_back({v, v + 3, 1});
        len.push_back(rnd());
      }
    }
  std::vector<int> terms{0, 2, 6, 8};
  auto brute = [&]() {
    double best = kInf;
    std::vector<int> others{1, 3, 4, 5, 7};
    for (int m = 0; m < 32; ++m) {
      std::vector<char> in(9, 0);
      for (int t : terms)
        in[t] = 1;
      for (int i = 0; i < 5; ++i)
        if (m >> i & 1)
          in[others[i]] = 1;
      // Prim over the induced subgraph
      std::vector<char> done(9, 0);
      std::vector<double> key(9, kInf);
      key[0] = 0;
      double tot = 0;
      int cnt = 0, need = 0;
      for (int v = 0; v < 9; ++v)
        need += in[v];
      for (;;) {
        int u = -1;
        for (int v = 0; v < 9; ++v)
          if (in[v] && !done[v] && key[v] < kInf && (u < 0 || key[v] < key[u]))
            u = v;
        if (u < 0)
          break;
        done[u] = 1;
        tot += key[u];
        ++cnt;
        for (auto [w, e] : G.adj[u])
          if (in[w] && !done[w] && len[e] < key[w])
            key[w] = len[e];
      }
      if (cnt == need)
        best = std::min(best, tot);
    }
    return best;
  };
  Scratch s;
  std::vector<int> tree;
  double dw = dreyfusWagner(G, len, terms, s, &tree);
  double bf = brute();
  double tc = 0;
  for (int e : tree)
    tc += len[e];
  check(std::fabs(dw - bf) < 1e-9, "Dreyfus-Wagner equals brute force on a 3 x 3 grid, 4 corners (" +
                                       std::to_string(dw) + " vs " + std::to_string(bf) + ")");
  check(std::fabs(tc - dw) < 1e-9, "its reconstructed tree costs what it reports");
  std::vector<int> spt;
  shortestPathTree(G, len, terms, s, spt);
  double sc = 0;
  for (int e : spt)
    sc += len[e];
  check(sc >= dw - 1e-9, "the shortest-path heuristic is never below the exact Steiner tree");
}

// Three nets must cross a 1-tile-wide corridor whose edges hold one net: forced overflow 8 (lpbound's test).
void testForcedCorridor() {
  Design d = parseText("cgbound_selftest_corridor.gr",
                       "grid 3 1 2\nvertical capacity 0 0\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 3\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\nc 2 2 1\n5 5 1\n25 5 1\n0\n");
  Region r{0, 0, 2, 0};
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  Settings st;
  st.trace = false;
  st.threads = 2;
  Result res = solve(G, nets, st);
  check(res.converged && std::fabs(res.lower - 8.0) < 1e-6 && std::fabs(res.master - 8.0) < 1e-6,
        "forced corridor: certified 8 = fractional optimum 8 (got " + std::to_string(res.lower) + " / " +
            std::to_string(res.master) + ")");
}

// A detour exists: two nets between the left and right ends of a 3 x 2 grid whose bottom row holds one net and
// whose top row holds one: zero overflow is reachable, so the bound must stay at 0 while the master finds 0.
void testDetourZero() {
  Design d = parseText("cgbound_selftest_detour.gr",
                       "grid 3 2 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 2\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\n0\n");
  Region r{0, 0, 2, 1};
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  Settings st;
  st.trace = false;
  st.threads = 2;
  Result res = solve(G, nets, st);
  check(res.converged && res.lower <= 1e-6 && res.master <= 1e-6,
        "detour available: bound 0 and fractional routing 0 (got " + std::to_string(res.lower) + " / " +
            std::to_string(res.master) + ")");
}

// --seed: a route for the detour instance with both nets on the bottom row -- read back, it gives each net the two
// bottom-row edges, and seeded the solve still converges to 0.
void testSeed() {
  Design d = parseText("cgbound_selftest_seed.gr",
                       "grid 3 2 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 2\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\n0\n");
  Region r{0, 0, 2, 1};
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  const std::string path = "cgbound_selftest_seed.route";
  std::ofstream(path) << "a 0 1\n(5,5,1)-(25,5,1)\n!\nb 1 3\n(5,5,1)-(25,5,1)\n(5,5,1)-(5,5,2)\n!\nzz 9 1\n(5,5,1)-(15,5,1)\n!\n";
  size_t found = 0;
  std::vector<std::vector<int>> seed = readSeed(path, d.grid, G, nets, found);
  std::remove(path.c_str());
  int h00 = G.gridEdge.at(Graph::key(0, 0, 0)), h10 = G.gridEdge.at(Graph::key(1, 0, 0));
  std::vector<int> want{std::min(h00, h10), std::max(h00, h10)};
  check(found == 2 && seed.size() == 2 && seed[0] == want && seed[1] == want,
        "seed route read back: both nets on the bottom-row edges, the via and the unknown net skipped");
  Settings st;
  st.trace = false;
  st.threads = 2;
  Result res = solve(G, nets, st, &seed);
  check(res.converged && res.lower <= 1e-6 && res.master <= 1e-6, "seeded with an overflowing route, still 0 / 0");
}

// The same corridor with the region cut to the middle tile: the end pins fall outside and merge into O, so every
// net costs 0 and the bound must fall to 0 (never above what the whole-chip problem forces).
void testOutsideMergeOnlyLowers() {
  Design d = parseText("cgbound_selftest_merge.gr",
                       "grid 3 1 2\nvertical capacity 0 0\nhorizontal capacity 2 0\n"
                       "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                       "num net 3\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\nc 2 2 1\n5 5 1\n25 5 1\n0\n");
  Region r{1, 0, 1, 0};
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  check(nets.empty(), "middle-tile region: both ends merge into O, no net has two nodes");
}

// Random small instances: the certified bound never exceeds the fractional routing's overflow (weak duality), and
// with every net exact it reaches it at convergence.
void testRandomTight() {
  unsigned seed = 11;
  auto rnd = [&](int m) {
    seed = seed * 1103515245u + 12345u;
    return static_cast<int>((seed >> 16) % static_cast<unsigned>(m));
  };
  bool ok = true, tight = true;
  for (int trial = 0; trial < 6; ++trial) {
    std::string gr = "grid 5 5 2\nvertical capacity 0 4\nhorizontal capacity 4 0\nminimum width 1 1\n"
                     "minimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 14\n";
    for (int k = 0; k < 14; ++k) {
      int np = 2 + rnd(3);
      gr += "n" + std::to_string(k) + " " + std::to_string(k) + " " + std::to_string(np) + " 1\n";
      for (int p = 0; p < np; ++p)
        gr += std::to_string(5 + 10 * rnd(5)) + " " + std::to_string(5 + 10 * rnd(5)) + " 1\n";
    }
    gr += "0\n";
    Design d = parseText("cgbound_selftest_random.gr", gr);
    Region r{0, 0, 4, 4};
    Graph G = buildGraph(d.grid, r);
    std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
    Settings st;
    st.trace = false;
    st.threads = 2;
    st.gapTol = 1e-6;
    Result res = solve(G, nets, st);
    ok = ok && res.lower <= res.master + 1e-6;
    if (!(res.converged && res.allExact && std::fabs(res.lower - res.master) < 1e-4))
      std::cout << "    trial " << trial << ": converged " << res.converged << ", exact " << res.allExact << ", bound "
                << res.lower << ", master " << res.master << "\n";
    tight = tight && res.converged && res.allExact && std::fabs(res.lower - res.master) < 1e-4;
  }
  check(ok, "random 5 x 5 instances: bound <= fractional routing overflow");
  check(tight, "and with every net exact the bound reaches the fractional optimum at convergence");
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest") {
    std::cout << "self test\n";
    testSteinerExact();
    testForcedCorridor();
    testDetourZero();
    testOutsideMergeOnlyLowers();
    testSeed();
    testRandomTight();
    std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
    return failures == 0 ? 0 : 1;
  }
  if (argc < 6) {
    std::cerr << "usage: " << argv[0]
              << " <benchmark.gr> <x0> <y0> <x1> <y1> [--rounds N] [--time S] [--exact K] [--threads T] [--solver ipm|simplex] [--seed ROUTE] [--export FILE]\n"
              << "       " << argv[0] << " --selftest\n";
    return 2;
  }
  Settings st;
  std::string seedPath, exportPath;
  for (int i = 6; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--rounds")
      st.rounds = std::atoi(argv[i + 1]);
    else if (a == "--time")
      st.timeLimit = std::atof(argv[i + 1]);
    else if (a == "--exact")
      st.exact = std::atoi(argv[i + 1]);
    else if (a == "--threads")
      st.threads = std::atoi(argv[i + 1]);
    else if (a == "--seed")
      seedPath = argv[i + 1];
    else if (a == "--export")
      exportPath = argv[i + 1];
    else if (a == "--solver" && (std::string(argv[i + 1]) == "ipm" || std::string(argv[i + 1]) == "simplex"))
      st.ipm = std::string(argv[i + 1]) == "ipm";
    else {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
  }
  if (st.exact < 2 || st.exact > 12 || st.threads < 1 || st.rounds < 1) {
    std::cerr << "--exact must be 2-12, --threads and --rounds at least 1\n";
    return 2;
  }
  Design d = readGr(argv[1]);
  Region r{std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5])};
  if (r.x0 < 0 || r.y0 < 0 || r.x1 >= d.grid.nx || r.y1 >= d.grid.ny || r.x0 > r.x1 || r.y0 > r.y1) {
    std::cerr << "region out of bounds\n";
    return 2;
  }
  Graph G = buildGraph(d.grid, r);
  std::vector<NetTerms> nets = buildNets(d, r, G.nNodes - 1);
  size_t big = 0;
  for (const NetTerms &n : nets)
    big += static_cast<int>(n.terms.size()) > st.exact;
  std::cerr << "region x " << r.x0 << "-" << r.x1 << " y " << r.y0 << "-" << r.y1 << ": " << G.nNodes << " nodes, "
            << G.edges.size() << " edges, " << nets.size() << " nets with two or more nodes (" << big
            << " above --exact " << st.exact << ": subset bound, heuristic trees)\n";
  std::vector<std::vector<int>> seed;
  if (!seedPath.empty()) {
    size_t found = 0;
    seed = readSeed(seedPath, d.grid, G, nets, found);
    std::cerr << "seed " << seedPath << ": " << found << " of " << nets.size() << " nets found\n";
  }
  Result res = solve(G, nets, st, seedPath.empty() ? nullptr : &seed);
  std::cout << "certified lower bound on total overflow: " << res.lower << "\nfractional routing overflow (master): "
            << res.master << "\nrounds " << res.rounds << (res.converged ? ", converged" : ", not converged")
            << (res.allExact ? ", every net exact" : "") << "\n";
  if (!exportPath.empty()) {
    // --export FILE: per net, its heaviest column in the last solved master as chip edges -- 'net <name> <edges>'
    // then '<x> <y> <H|V>' lines (an H edge joins tiles (x, y) and (x + 1, y), a V edge (x, y) and (x, y + 1)); an
    // edge to the outside node is the chip edge that leaves the region. gr/cg_merge.py joins these with the seed.
    std::ofstream f(exportPath);
    f << "# cgbound export: region x " << r.x0 << "-" << r.x1 << " y " << r.y0 << "-" << r.y1 << ", master "
      << res.master << ", rounds " << res.rounds << "\n";
    size_t written = 0;
    for (size_t k = 0; k < nets.size(); ++k) {
      if (res.heaviest[k].empty())
        continue;
      f << "net " << nets[k].name << " " << res.heaviest[k].size() << "\n";
      for (int e : res.heaviest[k])
        f << G.edgeXY[e][0] << " " << G.edgeXY[e][1] << " " << (G.edgeXY[e][2] == 0 ? 'H' : 'V') << "\n";
      ++written;
    }
    std::cerr << "export " << exportPath << ": " << written << " nets\n";
  }
  return 0;
}
