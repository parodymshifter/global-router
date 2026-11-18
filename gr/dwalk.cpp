// dwalk.cpp -- random-walk (discrepancy-style) rounding of a fractional route mix to whole routes.
//
// Not a router: a rounding step for gr/hotspot_cg.py (HISTORY_COST_SWEEP.md Part 213). hotspot_cg.py solves the
// fractional mix (each net a convex combination of candidate routes); turning it into one route per net is where
// overflow comes back -- on newblue7's edge (161, 329) the mix reaches 0 and every existing rounding (whole-route
// search from the heaviest routes, the integer program over the split nets, the dive, exact neighbourhoods) ends at
// 2 (Part 213). The idea is Lovett-Meka's constructive discrepancy walk ("walking on the edges", FOCS 2012; Bansal
// and Nagarajan's approximation-friendly version names low-congestion routing as an application): move the mix in
// a random direction that keeps every held constraint exactly where it is -- each net's weights summing to 1, and
// the load of every edge that is at its capacity -- until a weight reaches 0 or 1 (it freezes) or another edge
// reaches its capacity (it joins the held set). Each step freezes a weight or holds an edge, so the walk ends in at
// most (weights + edges) steps with every weight 0 or 1. Only when the held rows leave no room to move is one edge
// released: the one carrying the least movable demand, which bounds how far that edge can then go over (the
// Beck-Fiala argument). Every other edge ends at or under its capacity.
//
// The step direction is a Gaussian vector projected onto the null space of the held rows: d = g - B y with
// y = argmin ||B y - g|| (B = the held rows' transpose, restricted to the weights still moving), solved by Eigen's
// least-squares conjugate gradient -- the held rows' Gram matrix is far denser than the rows themselves (a route of
// a hundred edges couples them all), so it is never formed.
//
// Dependency: Eigen (MPL-2.0, header only; Homebrew `eigen`, /opt/homebrew/include/eigen3) -- this tool only.
// Build: g++ -std=c++17 -O2 -Wall -Wextra -isystem /opt/homebrew/include/eigen3 -o dwalk dwalk.cpp
//
// Input (written by hotspot_cg.py --dwalk):
//   dwalk 1 <nets> <edges>
//   E <T> <M> <fixed load>                      one line per edge, index = order (T, M: hotspot_cg's exact 2D model:
//                                                cost of u movable units = max(0, ceil((u - T) / 2), u - T - M))
//   N <columns>                                 per net, then one line per column:
//   C <weight> <edges> <edge> <demand> ...
// Output: one line per net, the index of its chosen column (in the order given); trace on stderr.
//
// Usage: dwalk <in> <out> [--seed S] [--tol X]
//        dwalk --selftest

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Edge {
  long T = 0, M = 0;
  double fixed = 0; // load of the nets that do not move
};

struct Var {
  int net = -1;
  int col = -1;
  double x = 0;
  std::vector<std::pair<int, double>> use; // (edge, demand)
};

struct Problem {
  std::vector<Edge> edges;
  std::vector<std::vector<int>> netVars; // net -> its variables
  std::vector<Var> vars;
};

double costOf(const Edge &e, double u) {
  double d = u - e.T;
  if (d <= 0)
    return 0;
  return std::max(std::ceil(d / 2.0), d - e.M);
}

bool readProblem(std::istream &in, Problem &p, std::string &err) {
  std::string tag;
  int version = 0;
  long nNets = 0, nEdges = 0;
  if (!(in >> tag >> version >> nNets >> nEdges) || tag != "dwalk" || version != 1 || nNets < 0 || nEdges < 0) {
    err = "expected 'dwalk 1 <nets> <edges>'";
    return false;
  }
  p.edges.resize(nEdges);
  for (long i = 0; i < nEdges; ++i) {
    if (!(in >> tag >> p.edges[i].T >> p.edges[i].M >> p.edges[i].fixed) || tag != "E") {
      err = "edge " + std::to_string(i) + ": expected 'E <T> <M> <fixed>'";
      return false;
    }
  }
  p.netVars.resize(nNets);
  for (long n = 0; n < nNets; ++n) {
    long k = 0;
    if (!(in >> tag >> k) || tag != "N" || k < 1) {
      err = "net " + std::to_string(n) + ": expected 'N <columns>' (at least 1)";
      return false;
    }
    double wsum = 0;
    for (long c = 0; c < k; ++c) {
      Var v;
      v.net = static_cast<int>(n);
      v.col = static_cast<int>(c);
      long m = 0;
      if (!(in >> tag >> v.x >> m) || tag != "C" || m < 0 || !(v.x >= -1e-9 && v.x <= 1 + 1e-9)) {
        err = "net " + std::to_string(n) + " column " + std::to_string(c) + ": expected 'C <weight in [0,1]> <edges> ...'";
        return false;
      }
      for (long j = 0; j < m; ++j) {
        long e;
        double a;
        if (!(in >> e >> a) || e < 0 || e >= nEdges || a <= 0) {
          err = "net " + std::to_string(n) + " column " + std::to_string(c) + ": bad edge/demand pair";
          return false;
        }
        v.use.push_back({static_cast<int>(e), a});
      }
      v.x = std::min(1.0, std::max(0.0, v.x));
      wsum += v.x;
      p.netVars[n].push_back(static_cast<int>(p.vars.size()));
      p.vars.push_back(std::move(v));
    }
    if (std::fabs(wsum - 1.0) > 1e-4) {
      err = "net " + std::to_string(n) + ": weights sum to " + std::to_string(wsum) + ", not 1";
      return false;
    }
    for (int vi : p.netVars[n]) // exact convexity
      p.vars[vi].x /= wsum;
  }
  return true;
}

struct WalkStats {
  int steps = 0, released = 0, heldMax = 0;
  double before = 0, after = 0; // cost over the listed edges: fractional mix, rounded
  double maxResidual = 0;       // the largest held-row residual seen in a step (projection accuracy)
};

// The walk. Returns the chosen column per net.
std::vector<int> walk(Problem p, unsigned seed, double tol, WalkStats &st, bool trace) {
  const double kFreeze = 1e-9;
  const int nV = static_cast<int>(p.vars.size()), nE = static_cast<int>(p.edges.size());
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> gauss(0.0, 1.0);
  std::vector<double> load(nE, 0.0);
  for (const Var &v : p.vars)
    for (auto [e, a] : v.use)
      load[e] += a * v.x;
  for (int e = 0; e < nE; ++e)
    st.before += costOf(p.edges[e], p.edges[e].fixed + load[e]);
  std::vector<char> alive(nV, 0), held(nE, 0), released(nE, 0);
  for (int i = 0; i < nV; ++i)
    alive[i] = p.vars[i].x > kFreeze && p.vars[i].x < 1 - kFreeze;
  auto slack = [&](int e) { return static_cast<double>(p.edges[e].T) - p.edges[e].fixed - load[e]; };
  // edges at (or over) capacity in the mix start held
  for (int e = 0; e < nE; ++e)
    held[e] = slack(e) <= 1e-7;
  std::vector<std::vector<int>> edgeVars(nE);
  for (int i = 0; i < nV; ++i)
    for (auto [e, a] : p.vars[i].use)
      edgeVars[e].push_back(i);
  auto t0 = std::chrono::steady_clock::now();
  for (;;) {
    std::vector<int> av;
    std::vector<int> pos(nV, -1);
    for (int i = 0; i < nV; ++i)
      if (alive[i]) {
        pos[i] = static_cast<int>(av.size());
        av.push_back(i);
      }
    if (av.empty())
      break;
    // held rows: every net with a moving weight, every held edge with a moving weight
    std::vector<int> rowNet, rowEdge;
    {
      std::vector<char> seen(p.netVars.size(), 0);
      for (int i : av)
        if (!seen[p.vars[i].net]) {
          seen[p.vars[i].net] = 1;
          rowNet.push_back(p.vars[i].net);
        }
      for (int e = 0; e < nE; ++e)
        if (held[e]) {
          bool any = false;
          for (int i : edgeVars[e])
            if (alive[i]) {
              any = true;
              break;
            }
          if (any)
            rowEdge.push_back(e);
        }
    }
    st.heldMax = std::max(st.heldMax, static_cast<int>(rowEdge.size()));
    const int nRows = static_cast<int>(rowNet.size() + rowEdge.size());
    // B: (moving weights) x (held rows)
    std::vector<Eigen::Triplet<double>> tr;
    for (size_t r = 0; r < rowNet.size(); ++r)
      for (int i : p.netVars[rowNet[r]])
        if (alive[i])
          tr.emplace_back(pos[i], static_cast<int>(r), 1.0);
    for (size_t r = 0; r < rowEdge.size(); ++r)
      for (int i : edgeVars[rowEdge[r]])
        if (alive[i])
          for (auto [e, a] : p.vars[i].use)
            if (e == rowEdge[r])
              tr.emplace_back(pos[i], static_cast<int>(rowNet.size() + r), a);
    Eigen::SparseMatrix<double> B(static_cast<int>(av.size()), nRows);
    B.setFromTriplets(tr.begin(), tr.end());
    B.makeCompressed();
    Eigen::VectorXd g(av.size());
    for (size_t k = 0; k < av.size(); ++k)
      g[k] = gauss(rng);
    Eigen::VectorXd d = g;
    if (nRows > 0) {
      Eigen::LeastSquaresConjugateGradient<Eigen::SparseMatrix<double>> ls;
      ls.setTolerance(tol);
      ls.setMaxIterations(4 * nRows + 100);
      ls.compute(B);
      Eigen::VectorXd y = ls.solve(g);
      d = g - B * y;
    }
    double dn = d.norm();
    // residual of the held rows along d, relative to |d|
    double res = 0;
    if (nRows > 0 && dn > 0)
      res = (B.transpose() * d).cwiseAbs().maxCoeff() / dn;
    if (dn < 1e-7 * std::sqrt(static_cast<double>(av.size())) || res > 1e-6) {
      // no room to move (or the projection is not trustworthy): release the held edge with the least movable demand
      int best = -1;
      double bestDem = 1e300;
      for (int e : rowEdge) {
        double dem = 0;
        for (int i : edgeVars[e])
          if (alive[i])
            for (auto [f, a] : p.vars[i].use)
              if (f == e)
                dem += a;
        if (dem < bestDem) {
          bestDem = dem;
          best = e;
        }
      }
      if (best < 0) {
        // only convexity rows and still no direction: every moving net has one moving weight -- impossible with
        // exact convexity (a lone moving weight in a net is 1 minus frozen ones, so it is 0 or 1); freeze by rounding
        for (int i : av)
          alive[i] = 0;
        break;
      }
      held[best] = 0;
      released[best] = 1;
      ++st.released;
      continue;
    }
    st.maxResidual = std::max(st.maxResidual, res);
    if (rng() & 1)
      d = -d;
    // the longest step that keeps every weight in [0, 1] and every non-held edge at or under its capacity
    double tmax = 1e300;
    for (size_t k = 0; k < av.size(); ++k) {
      double xi = p.vars[av[k]].x;
      if (d[k] > 1e-15)
        tmax = std::min(tmax, (1.0 - xi) / d[k]);
      else if (d[k] < -1e-15)
        tmax = std::min(tmax, -xi / d[k]);
    }
    std::vector<double> dl(nE, 0.0);
    std::vector<int> touched;
    for (size_t k = 0; k < av.size(); ++k)
      for (auto [e, a] : p.vars[av[k]].use) {
        if (dl[e] == 0.0)
          touched.push_back(e);
        dl[e] += a * d[k];
      }
    int stopEdge = -1;
    for (int e : touched)
      if (!held[e] && !released[e] && dl[e] > 1e-12) {
        double s = std::max(0.0, slack(e));
        if (s / dl[e] < tmax) {
          tmax = s / dl[e];
          stopEdge = e;
        }
      }
    if (!(tmax < 1e299))
      tmax = 0;
    for (size_t k = 0; k < av.size(); ++k) {
      Var &v = p.vars[av[k]];
      v.x += tmax * d[k];
      for (auto [e, a] : v.use)
        load[e] += a * tmax * d[k];
    }
    for (size_t k = 0; k < av.size(); ++k) {
      Var &v = p.vars[av[k]];
      if (v.x <= kFreeze || v.x >= 1 - kFreeze) {
        double target = v.x >= 0.5 ? 1.0 : 0.0;
        for (auto [e, a] : v.use)
          load[e] += a * (target - v.x);
        v.x = target;
        alive[av[k]] = 0;
      }
    }
    if (stopEdge >= 0)
      held[stopEdge] = 1;
    ++st.steps;
    if (trace && st.steps % 500 == 0)
      std::cerr << "  step " << st.steps << ": " << av.size() << " weights moving, " << rowEdge.size()
                << " edges held, " << st.released << " released ("
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s)\n";
  }
  std::vector<int> choice(p.netVars.size(), 0);
  std::fill(load.begin(), load.end(), 0.0);
  for (size_t n = 0; n < p.netVars.size(); ++n) {
    int best = p.netVars[n][0];
    for (int i : p.netVars[n])
      if (p.vars[i].x > p.vars[best].x)
        best = i;
    choice[n] = p.vars[best].col;
    for (auto [e, a] : p.vars[best].use)
      load[e] += a;
  }
  for (int e = 0; e < nE; ++e)
    st.after += costOf(p.edges[e], p.edges[e].fixed + load[e]);
  return choice;
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

Problem parse(const std::string &text) {
  Problem p;
  std::istringstream in(text);
  std::string err;
  if (!readProblem(in, p, err))
    std::cout << "  (parse error: " << err << ")\n";
  return p;
}

// Two nets, two edges with room for one net each, every net half on each edge: the mix is at capacity everywhere
// and the only whole-route answers at 0 put the nets on different edges -- a coordinated move (moving one net alone
// from a heaviest-route start overflows). Every seed must find 0.
void testCoordinated() {
  Problem p = parse("dwalk 1 2 2\nE 2 0 0\nE 2 0 0\n"
                    "N 2\nC 0.5 1 0 2\nC 0.5 1 1 2\n"
                    "N 2\nC 0.5 1 0 2\nC 0.5 1 1 2\n");
  bool all = true;
  for (unsigned s = 1; s <= 20; ++s) {
    WalkStats st;
    std::vector<int> c = walk(p, s, 1e-12, st, false);
    all = all && c.size() == 2 && c[0] != c[1] && st.after == 0 && st.released == 0;
  }
  check(all, "coordinated pair: 20 seeds, every one puts the nets on different edges (overflow 0, nothing released)");
}

// A chain: four nets, five edges with room for one net each; net i half on edge i, half on edge i+1. The mix is at
// capacity on edges 1-3 and at half on edges 0 and 4; an answer at 0 exists for every alignment (all nets shift the
// same way). Every seed must find 0.
void testChain() {
  std::string s = "dwalk 1 4 5\n";
  for (int e = 0; e < 5; ++e)
    s += "E 2 0 0\n";
  for (int i = 0; i < 4; ++i)
    s += "N 2\nC 0.5 1 " + std::to_string(i) + " 2\nC 0.5 1 " + std::to_string(i + 1) + " 2\n";
  Problem p = parse(s);
  bool all = true;
  for (unsigned sd = 1; sd <= 20; ++sd) {
    WalkStats st;
    walk(p, sd, 1e-12, st, false);
    all = all && st.after == 0;
  }
  check(all, "chain of four nets over five one-net edges: 20 seeds, overflow 0 every time");
}

// Forced overflow: three nets that must each use one of two one-net edges. Any answer overflows by 2 (one edge
// carries two nets). The walk must return a valid choice, released at least one edge, and cost exactly 2.
void testForced() {
  std::string s = "dwalk 1 3 2\nE 2 0 0\nE 2 0 0\n";
  for (int i = 0; i < 3; ++i)
    s += "N 2\nC 0.5 1 0 2\nC 0.5 1 1 2\n";
  Problem p = parse(s);
  bool ok = true;
  for (unsigned sd = 1; sd <= 10; ++sd) {
    WalkStats st;
    std::vector<int> c = walk(p, sd, 1e-12, st, false);
    ok = ok && c.size() == 3 && st.after == 2 && st.released >= 1;
  }
  check(ok, "forced: three nets over two one-net edges -> overflow exactly 2, an edge released, every seed");
}

// Random instances: the projection keeps the held rows (residual tiny), every net gets exactly one column, and the
// rounded cost on edges never released stays at or under capacity.
void testRandom() {
  std::mt19937 rng(5);
  bool ok = true;
  double worst = 0;
  for (int trial = 0; trial < 20; ++trial) {
    int nE = 30, nN = 25;
    std::ostringstream s;
    s << "dwalk 1 " << nN << " " << nE << "\n";
    for (int e = 0; e < nE; ++e)
      s << "E " << 2 * (2 + rng() % 4) << " 1 " << 0 << "\n";
    for (int n = 0; n < nN; ++n) {
      int k = 2 + rng() % 3;
      std::vector<double> w(k);
      double tot = 0;
      for (double &v : w)
        tot += (v = 0.1 + (rng() % 100) / 100.0);
      s << "N " << k << "\n";
      for (int c = 0; c < k; ++c) {
        int m = 2 + rng() % 5;
        s << "C " << w[c] / tot << " " << m;
        std::vector<int> es;
        while (static_cast<int>(es.size()) < m) {
          int e = rng() % nE;
          if (std::find(es.begin(), es.end(), e) == es.end())
            es.push_back(e);
        }
        for (int e : es)
          s << " " << e << " 2";
        s << "\n";
      }
    }
    Problem p = parse(s.str());
    WalkStats st;
    std::vector<int> c = walk(p, 100 + trial, 1e-12, st, false);
    ok = ok && static_cast<int>(c.size()) == nN && st.maxResidual < 1e-6;
    for (int n = 0; n < nN; ++n)
      ok = ok && c[n] >= 0 && c[n] < static_cast<int>(p.netVars[n].size());
    worst = std::max(worst, st.maxResidual);
  }
  check(ok, "20 random instances: one column per net, held rows kept (worst relative residual " +
                std::to_string(worst) + ")");
}

void testReaderRefuses() {
  Problem p;
  std::string err;
  bool bad = true;
  for (const char *t : {"dwalk 2 1 1\n", "dwalk 1 1 1\nE 2 0 0\nN 1\nC 0.5 1 0 2\n",
                        "dwalk 1 1 1\nE 2 0 0\nN 1\nC 1 1 3 2\n", "dwalk 1 1 1\nE 2 0 0\nN 0\n",
                        "dwalk 1 1 1\nE 2 0 0\nN 1\nC 1.5 1 0 2\n"}) {
    std::istringstream in(t);
    Problem q;
    bad = bad && !readProblem(in, q, err);
  }
  (void)p;
  check(bad, "reader refuses: a wrong version, weights not summing to 1, an edge out of range, no columns, weight > 1");
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest") {
    std::cout << "self test\n";
    testCoordinated();
    testChain();
    testForced();
    testRandom();
    testReaderRefuses();
    std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
    return failures == 0 ? 0 : 1;
  }
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <in> <out> [--seed S] [--tol X]\n       " << argv[0] << " --selftest\n";
    return 2;
  }
  unsigned seed = 1;
  double tol = 1e-10;
  for (int i = 3; i + 1 < argc; i += 2) {
    std::string a = argv[i];
    if (a == "--seed")
      seed = static_cast<unsigned>(std::stoul(argv[i + 1]));
    else if (a == "--tol")
      tol = std::stod(argv[i + 1]);
    else {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 2;
  }
  Problem p;
  std::string err;
  if (!readProblem(in, p, err)) {
    std::cerr << argv[1] << ": " << err << "\n";
    return 2;
  }
  size_t moving = 0;
  for (const Var &v : p.vars)
    moving += v.x > 1e-9 && v.x < 1 - 1e-9;
  std::cerr << "dwalk: " << p.netVars.size() << " nets, " << p.vars.size() << " columns (" << moving << " fractional), "
            << p.edges.size() << " edges, seed " << seed << "\n";
  WalkStats st;
  auto t0 = std::chrono::steady_clock::now();
  std::vector<int> c = walk(p, seed, tol, st, true);
  std::ofstream out(argv[2]);
  for (int k : c)
    out << k << "\n";
  std::cerr << "dwalk: cost over these edges " << st.before << " (mix) -> " << st.after << " (whole routes); "
            << st.steps << " steps, " << st.released << " edges released, at most " << st.heldMax
            << " held, worst residual " << st.maxResidual << " ("
            << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << " s)\n";
  return 0;
}
