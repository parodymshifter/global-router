// g24_gap.cpp -- where an ISPD 2024 solution's cost goes, against lower
// bounds and achievable references (HISTORY_COST_SWEEP.md Part 105). An
// analysis tool, not a router.
//
//   g24_gap <design.cap> <design.net> <solution>
//   gunzip -c x.g24sol.gz | g24_gap <design.cap> <design.net> /dev/stdin
//   g24_gap --selftest
//
// Wirelength, in the evaluator's units (a wire over GCells [lo, hi) costs
// the pitches between them, times unitLength), per pin-count class:
//   ours: the solution's wire cost;
//   hpwl: half-perimeter of each net's bounding box over its pins' first
//     access points, in the same pitch units -- a lower bound on any tree
//     through those access points;
//   hpwl best AP: the same with each pin's access point re-chosen (two
//     greedy passes, each pin to the access point that shrinks the box
//     most) -- what access-point choice alone could save;
//   ours / hpwl: tree topology + detours. For 2- and 3-pin nets the
//     minimum tree equals the box, so the excess there is pure detour.
// Tree references over the first access points (each pin's GCell, pitch
// coordinates, duplicates merged): rmst, the rectilinear minimum spanning
// tree (Prim); steiner, iterated 1-Steiner on the Hanan grid (Kahng and
// Robins: add the Hanan point that shortens the spanning tree most, until
// none does) for nets of up to 9 distinct pin GCells, typically within ~1%
// of the optimal Steiner tree -- for larger nets the rmst is shown. ours /
// steiner on 4-9 pin nets is the topology + detour excess a near-optimal
// tree would remove.
// Vias: ours; a floor -- each distinct GCell holding a pin's access point
// on layer 0 needs one via (wires cannot run on layer 0), plus one for a
// net all in one GCell; and the per-net excess over it, by class.
// Overflow is left to g24_verify (its per-layer breakdown and floor).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "g24_io.h"

namespace gap {

using g24::Access;
using g24::Net;
using g24::Resources;

// Prefix sums of pitches: dist(a, b) over GCells a < b = P[b] - P[a].
struct Dist {
  std::vector<long long> px, py;
  explicit Dist(const Resources &r) : px(r.xSize, 0), py(r.ySize, 0) {
    for (int x = 1; x < r.xSize; ++x)
      px[x] = px[x - 1] + r.xPitch[x - 1];
    for (int y = 1; y < r.ySize; ++y)
      py[y] = py[y - 1] + r.yPitch[y - 1];
  }
  long long box(int x0, int x1, int y0, int y1) const { return px[x1] - px[x0] + py[y1] - py[y0]; }
};

long long hpwlOf(const Dist &d, const std::vector<Access> &pts) {
  if (pts.empty())
    return 0;
  int x0 = pts[0].x, x1 = x0, y0 = pts[0].y, y1 = y0;
  for (const Access &a : pts) {
    x0 = std::min(x0, a.x);
    x1 = std::max(x1, a.x);
    y0 = std::min(y0, a.y);
    y1 = std::max(y1, a.y);
  }
  return d.box(x0, x1, y0, y1);
}

// Each pin's access point re-chosen, two greedy passes.
long long hpwlBestAP(const Dist &d, const Net &n) {
  std::vector<Access> cur;
  for (const auto &p : n.pins)
    cur.push_back(p[0]);
  for (int pass = 0; pass < 2; ++pass)
    for (size_t i = 0; i < n.pins.size(); ++i) {
      if (n.pins[i].size() < 2)
        continue;
      long long best = -1;
      Access bestA = cur[i];
      for (const Access &a : n.pins[i]) {
        cur[i] = a;
        long long h = hpwlOf(d, cur);
        if (best < 0 || h < best) {
          best = h;
          bestA = a;
        }
      }
      cur[i] = bestA;
    }
  return hpwlOf(d, cur);
}

struct Pt {
  long long x, y;
};

long long mstLength(const std::vector<Pt> &p) {
  size_t n = p.size();
  if (n < 2)
    return 0;
  std::vector<long long> dist(n, -1);
  std::vector<char> in(n, 0);
  long long total = 0;
  in[0] = 1;
  for (size_t i = 1; i < n; ++i)
    dist[i] = std::llabs(p[i].x - p[0].x) + std::llabs(p[i].y - p[0].y);
  for (size_t k = 1; k < n; ++k) {
    size_t best = n;
    for (size_t i = 0; i < n; ++i)
      if (!in[i] && (best == n || dist[i] < dist[best]))
        best = i;
    in[best] = 1;
    total += dist[best];
    for (size_t i = 0; i < n; ++i)
      if (!in[i]) {
        long long d = std::llabs(p[i].x - p[best].x) + std::llabs(p[i].y - p[best].y);
        if (d < dist[i])
          dist[i] = d;
      }
  }
  return total;
}

// Iterated 1-Steiner (single best Hanan point per iteration).
long long steinerLength(std::vector<Pt> p) {
  std::vector<long long> xs, ys;
  for (const Pt &q : p) {
    xs.push_back(q.x);
    ys.push_back(q.y);
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  long long cur = mstLength(p);
  for (;;) {
    long long best = cur;
    Pt bestP{0, 0};
    for (long long x : xs)
      for (long long y : ys) {
        bool there = false;
        for (const Pt &q : p)
          there = there || (q.x == x && q.y == y);
        if (there)
          continue;
        p.push_back({x, y});
        long long l = mstLength(p);
        p.pop_back();
        if (l < best) {
          best = l;
          bestP = {x, y};
        }
      }
    if (best >= cur)
      return cur;
    p.push_back(bestP);
    cur = best;
  }
}

std::vector<Pt> pinPoints(const Dist &d, const Net &n) {
  std::set<std::pair<int, int>> g;
  for (const auto &pin : n.pins)
    g.insert({pin[0].x, pin[0].y});
  std::vector<Pt> p;
  for (const auto &xy : g)
    p.push_back({d.px[xy.first], d.py[xy.second]});
  return p;
}

long long viaFloor(const Net &n) {
  std::set<std::pair<int, int>> g0, all;
  for (const auto &p : n.pins) {
    all.insert({p[0].x, p[0].y});
    if (p[0].z == 0)
      g0.insert({p[0].x, p[0].y});
  }
  long long f = static_cast<long long>(g0.size());
  if (all.size() == 1 && f == 0)
    f = 1;
  return f;
}

struct NetCost {
  long long wl = 0, vias = 0;
  bool seen = false;
};

// Reads a solution: per net, its wire length (pitch units) and via count.
bool readSolution(const Resources &r, const std::vector<Net> &nets, const Dist &d, std::istream &in,
                  std::vector<NetCost> &out, std::string &err) {
  std::unordered_map<std::string, int> byName;
  for (size_t i = 0; i < nets.size(); ++i)
    byName[nets[i].name] = static_cast<int>(i);
  out.assign(nets.size(), NetCost());
  std::string line;
  int cur = -1;
  while (std::getline(in, line)) {
    line = g24::trimEnd(line);
    if (line.empty())
      continue;
    if (cur < 0) {
      auto it = byName.find(line);
      if (it == byName.end()) {
        err = "unknown net '" + line + "'";
        return false;
      }
      cur = it->second;
      out[cur].seen = true;
      continue;
    }
    if (line[0] == '(')
      continue;
    if (line[0] == ')') {
      cur = -1;
      continue;
    }
    int xl, yl, zl, xh, yh, zh;
    if (std::sscanf(line.c_str(), "%d %d %d %d %d %d", &xl, &yl, &zl, &xh, &yh, &zh) != 6) {
      err = "bad segment '" + line + "'";
      return false;
    }
    if (zl != zh)
      out[cur].vias += zh - zl;
    else if (r.layers[zl].dir == 0)
      out[cur].wl += d.px[xh] - d.px[xl];
    else
      out[cur].wl += d.py[yh] - d.py[yl];
  }
  return true;
}

int classOf(const Net &n) {
  size_t p = n.pins.size();
  return p <= 2 ? 0 : p == 3 ? 1 : p <= 9 ? 2 : 3;
}
const char *kClassName[] = {"2 pins", "3 pins", "4-9 pins", "10+ pins"};

} // namespace gap

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "g24_gap self test\n";
  using namespace gap;
  // 4 x 3 GCells, x pitches 10 20 30, y pitches 5 7; 3 layers, M2
  // horizontal, M3 vertical.
  const char *cap = "3 4 3\n1 4 1 1 1\n10 20 30\n5 7\n"
                    "M1 0 0\n1 1 1 1\n1 1 1 1\n1 1 1 1\n"
                    "M2 0 0\n1 1 1 1\n1 1 1 1\n1 1 1 1\n"
                    "M3 1 0\n1 1 1 1\n1 1 1 1\n1 1 1 1\n";
  // netA: pins (0,0) and (3,2) on M1 -> box 10+20+30 + 5+7 = 72. Its first
  // pin also has an access point at (2,2): the best box is (2..3, 2..2) =
  // 30. netB: one GCell, two pins there on M1 -> box 0, via floor 1.
  const char *net = "netA\n(\n[(0, 0, 0), (0, 2, 2)]\n[(0, 3, 2)]\n)\nnetB\n(\n[(0, 1, 1)]\n[(0, 1, 1)]\n)\n";
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream ci(cap), ni(net);
  check(g24::readResources(ci, r, err) && g24::readNets(ni, nets, err), "fixture reads");
  Dist d(r);
  std::vector<Access> first{nets[0].pins[0][0], nets[0].pins[1][0]};
  check(hpwlOf(d, first) == 72, "hpwl over first access points: 10 + 20 + 30 + 5 + 7 = 72");
  check(hpwlBestAP(d, nets[0]) == 30, "best access point: (2,2) to (3,2), 30");
  check(viaFloor(nets[0]) == 2 && viaFloor(nets[1]) == 1, "via floor: 2 layer-0 pin GCells; one-GCell net 1");
  // Trees: a plus shape -- (0,1), (2,1), (1,0), (1,2) in unit coordinates
  // -- has an MST of 6 (three 2-long L hops) and a Steiner tree of 4 (the
  // centre (1,1)).
  std::vector<Pt> plus{{0, 1}, {2, 1}, {1, 0}, {1, 2}};
  check(mstLength(plus) == 6 && steinerLength(plus) == 4, "plus shape: MST 6, 1-Steiner 4 (centre point)");
  // netA routed: M1-M2 via at (0,0), M2 (0,0)-(3,0) = 60, M2-M3 via, M3
  // (3,0)-(3,2) = 12, M3 down to M1 at (3,2): 72 wire, 1 + 1 + 2 = 4 vias.
  std::istringstream sol("netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 1 3 0 2\n3 0 2 3 2 2\n3 2 0 3 2 2\n)\n"
                         "netB\n(\n1 1 0 1 1 1\n)\n");
  std::vector<NetCost> nc;
  check(readSolution(r, nets, d, sol, nc, err) && nc[0].wl == 72 && nc[0].vias == 4 && nc[1].wl == 0 &&
            nc[1].vias == 1,
        "solution: netA 72 of wire (pitch units) and 4 vias; netB 1 via");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <design.cap> <design.net> <solution>\n";
    return 2;
  }
  using namespace gap;
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::ifstream ci(argv[1]), ni(argv[2]), si(argv[3]);
  if (!ci || !ni || !si || !g24::readResources(ci, r, err) || !g24::readNets(ni, nets, err)) {
    std::cerr << "g24_gap: " << (err.empty() ? "cannot open an input" : err) << "\n";
    return 1;
  }
  Dist d(r);
  // G24_GAP_STEINER_MAX: the largest net (distinct pin GCells) given the
  // iterated 1-Steiner reference (default 9; cost grows ~ n^5).
  int steinerMax = 9;
  if (const char *v = std::getenv("G24_GAP_STEINER_MAX"); v)
    steinerMax = std::atoi(v);
  std::vector<NetCost> nc;
  if (!readSolution(r, nets, d, si, nc, err)) {
    std::cerr << "g24_gap: " << err << "\n";
    return 1;
  }
  struct Acc {
    long long nets = 0, ours = 0, hpwl = 0, best = 0, rmst = 0, steiner = 0, vias = 0, viaFloor = 0;
  };
  Acc cls[4], tot;
  for (size_t i = 0; i < nets.size(); ++i) {
    std::vector<Access> first;
    for (const auto &p : nets[i].pins)
      first.push_back(p[0]);
    Acc a;
    a.nets = 1;
    a.ours = nc[i].wl;
    a.hpwl = hpwlOf(d, first);
    a.best = hpwlBestAP(d, nets[i]);
    std::vector<Pt> pts = pinPoints(d, nets[i]);
    a.rmst = mstLength(pts);
    a.steiner = static_cast<int>(pts.size()) <= steinerMax ? steinerLength(pts) : a.rmst;
    a.vias = nc[i].vias;
    a.viaFloor = viaFloor(nets[i]);
    for (Acc *t : {&cls[classOf(nets[i])], &tot}) {
      t->nets += a.nets;
      t->ours += a.ours;
      t->hpwl += a.hpwl;
      t->best += a.best;
      t->rmst += a.rmst;
      t->steiner += a.steiner;
      t->vias += a.vias;
      t->viaFloor += a.viaFloor;
    }
  }
  auto cost = [&](long long v) { return static_cast<double>(v) * r.unitLength; };
  std::printf("%-9s %8s %11s %11s %11s %11s %11s %7s %7s %8s %9s %9s %7s\n", "class", "nets", "WL ours",
              "WL hpwl", "hpwl bestAP", "rmst", "steiner", "ours/hp", "best/hp", "ours/st", "vias", "via floor",
              "vias/n");
  auto row = [&](const char *name, const Acc &a) {
    std::printf("%-9s %8lld %11.0f %11.0f %11.0f %11.0f %11.0f %7.4f %7.4f %8.4f %9lld %9lld %7.2f\n", name, a.nets,
                cost(a.ours), cost(a.hpwl), cost(a.best), cost(a.rmst), cost(a.steiner),
                a.hpwl ? static_cast<double>(a.ours) / a.hpwl : 0.0,
                a.hpwl ? static_cast<double>(a.best) / a.hpwl : 0.0,
                a.steiner ? static_cast<double>(a.ours) / a.steiner : 0.0, a.vias, a.viaFloor,
                a.nets ? static_cast<double>(a.vias) / a.nets : 0.0);
  };
  for (int c = 0; c < 4; ++c)
    row(kClassName[c], cls[c]);
  row("all", tot);
  std::printf("steiner column: iterated 1-Steiner for nets of <= %d pin GCells, the rmst above that\n", steinerMax);
  std::printf("via cost ours %.0f, via-floor cost %.0f\n", static_cast<double>(tot.vias) * r.unitVia,
              static_cast<double>(tot.viaFloor) * r.unitVia);
  return 0;
}
