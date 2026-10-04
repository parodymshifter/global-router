// cutbound.cpp -- a certified lower bound on total overflow from cuts.
//
// Not a router: an analysis tool that answers "is this residue forced?"
// (HISTORY_COST_SWEEP.md Part 91: newblue4's 144 matches the best published
// result and resists virtual capacity although free room sits 3-10 tiles
// away). lpbound (Part 65) answers the same question by LP duality but its
// Frank-Wolfe lengths never certified anything; this is the cut condition,
// exact and cheap.
//
// For a rectangle S of tiles, every net with a pin tile inside S and a pin
// tile outside S must cross S's boundary at least once, using at least
// d_k = minWidth + (smallest layer spacing) capacity units on some boundary
// edge. So the boundary's 2D usage is >= D(S) = sum of d_k over those nets,
// and the 2D-collapsed overflow on the boundary edges is >= D(S) - C(S),
// where C(S) is the boundary's 2D capacity (summed over layers). Real
// (per-layer) overflow is >= the 2D-collapsed overflow on the same edges,
// so max(0, D(S) - C(S)) bounds the scored overflow on S's boundary. For
// rectangles whose boundaries share no edge the bounds add up. Nets with
// more than 1000 pins (which need not be routed) are left out; leaving nets
// out only lowers D, so the bound stays valid.
//
// It enumerates every rectangle up to maxSide x maxSide inside a region,
// reports the best ones, and packs a greedy set with disjoint boundaries.
// A 0 means no cut of this shape forces overflow -- not that none is forced.
//
// Usage: cutbound <benchmark.gr> <x0> <y0> <x1> <y1> [maxSide]
//        cutbound --selftest

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "gr_io.h"

using gr_io::Design;
using gr_io::RoutingGrid;

namespace cut {

struct Rect {
  int x0, y0, x1, y1;
  long long demand = 0, capacity = 0, bound = 0, nets = 0;
};

struct Result {
  std::vector<Rect> best;      // positive bounds, largest first
  long long packed = 0;        // greedy sum over rectangles with disjoint boundaries
  std::vector<Rect> packedSet;
  long long rects = 0;
  std::vector<Rect> tightest; // highest demand / capacity, whatever the bound (how close a cut comes)
};

long long hCap2(const RoutingGrid &g, int x, int y) {
  long long c = 0;
  for (int l = 0; l < g.nLayers; ++l)
    c += g.hcap[g.hIndex(x, y, l)];
  return c;
}
long long vCap2(const RoutingGrid &g, int x, int y) {
  long long c = 0;
  for (int l = 0; l < g.nLayers; ++l)
    c += g.vcap[g.vIndex(x, y, l)];
  return c;
}

// Boundary edges of a rectangle as (horizontal?, x, y) of the edge's
// lower/left tile; edges on the grid border do not exist and are skipped.
template <class F> void forBoundary(const RoutingGrid &g, const Rect &r, F f) {
  for (int y = r.y0; y <= r.y1; ++y) {
    if (r.x0 > 0)
      f(true, r.x0 - 1, y);
    if (r.x1 + 1 < g.nx)
      f(true, r.x1, y);
  }
  for (int x = r.x0; x <= r.x1; ++x) {
    if (r.y0 > 0)
      f(false, x, r.y0 - 1);
    if (r.y1 + 1 < g.ny)
      f(false, x, r.y1);
  }
}

Result analyze(const Design &d, int rx0, int ry0, int rx1, int ry1, int maxSide) {
  const RoutingGrid &g = d.grid;
  int minSp = INT_MAX;
  for (int s : g.minSpacing)
    minSp = std::min(minSp, s);
  // Nets with a pin tile in the region: their pin bounding box, demand, and
  // the list of distinct pin tiles per region tile.
  struct NetInfo {
    int bx0, by0, bx1, by1;
    long long dk;
  };
  std::vector<NetInfo> info;
  int W = rx1 - rx0 + 1, H = ry1 - ry0 + 1;
  std::vector<std::vector<int>> tileNets(static_cast<size_t>(W) * H);
  for (const gr_io::Net &n : d.nets) {
    if (n.pins.size() > 1000 || n.pins.size() < 2)
      continue;
    int bx0 = INT_MAX, by0 = INT_MAX, bx1 = INT_MIN, by1 = INT_MIN;
    for (const gr_io::Pin &p : n.pins) {
      bx0 = std::min(bx0, p.x);
      by0 = std::min(by0, p.y);
      bx1 = std::max(bx1, p.x);
      by1 = std::max(by1, p.y);
    }
    if (bx0 == bx1 && by0 == by1)
      continue; // all pins in one tile
    int id = -1;
    for (const gr_io::Pin &p : n.pins) {
      if (p.x < rx0 || p.x > rx1 || p.y < ry0 || p.y > ry1)
        continue;
      if (id < 0) {
        id = static_cast<int>(info.size());
        info.push_back({bx0, by0, bx1, by1, static_cast<long long>(n.minWidth) + minSp});
      }
      std::vector<int> &v = tileNets[static_cast<size_t>(p.y - ry0) * W + (p.x - rx0)];
      if (v.empty() || v.back() != id)
        v.push_back(id);
    }
  }
  Result res;
  std::vector<int> stamp(info.size(), -1);
  int stampId = 0;
  std::vector<Rect> all;
  for (int h = 1; h <= maxSide; ++h)
    for (int w = 1; w <= maxSide; ++w)
      for (int y0 = ry0; y0 + h - 1 <= ry1; ++y0)
        for (int x0 = rx0; x0 + w - 1 <= rx1; ++x0) {
          Rect r{x0, y0, x0 + w - 1, y0 + h - 1};
          ++res.rects;
          ++stampId;
          for (int y = r.y0; y <= r.y1; ++y)
            for (int x = r.x0; x <= r.x1; ++x)
              for (int id : tileNets[static_cast<size_t>(y - ry0) * W + (x - rx0)]) {
                if (stamp[id] == stampId)
                  continue;
                stamp[id] = stampId;
                const NetInfo &ni = info[id];
                bool inside = ni.bx0 >= r.x0 && ni.bx1 <= r.x1 && ni.by0 >= r.y0 && ni.by1 <= r.y1;
                if (!inside) {
                  r.demand += ni.dk;
                  ++r.nets;
                }
              }
          forBoundary(g, r, [&](bool hz, int x, int y) { r.capacity += hz ? hCap2(g, x, y) : vCap2(g, x, y); });
          r.bound = std::max(0LL, r.demand - r.capacity);
          if (r.bound > 0)
            all.push_back(r);
          if (r.capacity > 0) {
            res.tightest.push_back(r);
            if (res.tightest.size() > 64) {
              std::sort(res.tightest.begin(), res.tightest.end(), [](const Rect &a, const Rect &b) {
                return a.demand * b.capacity > b.demand * a.capacity;
              });
              res.tightest.resize(5);
            }
          }
        }
  std::sort(all.begin(), all.end(), [](const Rect &a, const Rect &b) {
    if (a.bound != b.bound)
      return a.bound > b.bound;
    return (a.x1 - a.x0 + 1) * (a.y1 - a.y0 + 1) < (b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1);
  });
  // Greedy packing with disjoint boundary edges.
  std::vector<char> usedH(static_cast<size_t>(g.nx) * g.ny, 0), usedV(static_cast<size_t>(g.nx) * g.ny, 0);
  for (const Rect &r : all) {
    bool clash = false;
    forBoundary(g, r, [&](bool hz, int x, int y) {
      if ((hz ? usedH : usedV)[static_cast<size_t>(y) * g.nx + x])
        clash = true;
    });
    if (clash)
      continue;
    forBoundary(g, r, [&](bool hz, int x, int y) { (hz ? usedH : usedV)[static_cast<size_t>(y) * g.nx + x] = 1; });
    res.packed += r.bound;
    res.packedSet.push_back(r);
  }
  std::sort(res.tightest.begin(), res.tightest.end(),
            [](const Rect &a, const Rect &b) { return a.demand * b.capacity > b.demand * a.capacity; });
  if (res.tightest.size() > 5)
    res.tightest.resize(5);
  if (all.size() > 20)
    all.resize(20);
  res.best = all;
  return res;
}

void print(const Result &r, int maxSide) {
  std::printf("rectangles checked: %lld (sides 1-%d); with a positive bound: %zu shown below (largest first)\n",
              r.rects, maxSide, r.best.size());
  for (const Rect &x : r.best)
    std::printf("  x %d-%d y %d-%d: %lld crossing nets, demand %lld, boundary capacity %lld -> forced >= %lld\n",
                x.x0, x.x1, x.y0, x.y1, x.nets, x.demand, x.capacity, x.bound);
  std::printf("certified lower bound (greedy, disjoint boundaries): %lld over %zu rectangles\n", r.packed,
              r.packedSet.size());
  for (const Rect &x : r.packedSet)
    std::printf("  packed: x %d-%d y %d-%d: forced >= %lld\n", x.x0, x.x1, x.y0, x.y1, x.bound);
  std::printf("tightest cuts (demand / boundary capacity):\n");
  for (const Rect &x : r.tightest)
    std::printf("  x %d-%d y %d-%d: %lld crossing nets, demand %lld / capacity %lld = %.1f%%\n", x.x0, x.x1, x.y0,
                x.y1, x.nets, x.demand, x.capacity, 100.0 * x.demand / std::max(1LL, x.capacity));
}

} // namespace cut

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "cutbound self test\n";
  // 3 x 3 tiles, 2 layers: horizontal capacity 2 on layer 1, vertical 2 on
  // layer 2, width 1, spacing 1 -> d = 2, one net per edge. Five nets, each
  // with a pin in the centre tile (1,1) and one in a corner: the centre's 4
  // boundary edges hold 8 units, the 5 nets need 10 -> forced >= 2. A sixth
  // net entirely inside the centre tile does not count.
  const char *gr = "grid 3 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 6\n"
                   "a 0 2 1\n15 15 1\n5 5 1\n"
                   "b 1 2 1\n15 15 1\n25 5 1\n"
                   "c 2 2 1\n15 15 1\n5 25 1\n"
                   "d 3 2 1\n15 15 1\n25 25 1\n"
                   "e 4 2 1\n15 15 1\n5 5 1\n"
                   "f 5 2 1\n12 12 1\n18 18 1\n0\n";
  const char *tmp = "cutbound_selftest.gr";
  {
    std::ofstream f(tmp);
    f << gr;
  }
  Design d = gr_io::readGr(tmp);
  std::remove(tmp);
  cut::Result r = cut::analyze(d, 0, 0, 2, 2, 3);
  bool centre = false;
  for (const cut::Rect &x : r.best)
    if (x.x0 == 1 && x.x1 == 1 && x.y0 == 1 && x.y1 == 1)
      centre = x.nets == 5 && x.demand == 10 && x.capacity == 8 && x.bound == 2;
  check(centre, "centre tile: 5 crossing nets, demand 10, boundary capacity 8 -> forced >= 2 (f, inside, excluded)");
  check(!r.best.empty() && r.best.front().bound == 2 && r.packed == 2,
        "best single bound 2, packed bound 2 (got " + std::to_string(r.packed) + ")");
  // The whole grid has no boundary edges and every net is inside: never positive.
  bool wholePositive = false;
  for (const cut::Rect &x : r.best)
    wholePositive = wholePositive || (x.x0 == 0 && x.x1 == 2 && x.y0 == 0 && x.y1 == 2);
  check(!wholePositive, "the whole grid forces nothing");
  // Raise the vertical capacity to 6: the centre boundary holds 2+2+6+6 = 16 >= 10 -> no bound.
  const char *gr2 = "grid 3 3 2\nvertical capacity 0 6\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 1\na 0 2 1\n15 15 1\n5 5 1\n0\n";
  {
    std::ofstream f(tmp);
    f << gr2;
  }
  Design d2 = gr_io::readGr(tmp);
  std::remove(tmp);
  cut::Result r2 = cut::analyze(d2, 0, 0, 2, 2, 3);
  check(r2.best.empty() && r2.packed == 0, "uncongested: no positive bound");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc < 6 || argc > 7) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <x0> <y0> <x1> <y1> [maxSide]\n";
    return 2;
  }
  int maxSide = argc == 7 ? std::atoi(argv[6]) : 8;
  Design d = gr_io::readGr(argv[1]);
  cut::Result r = cut::analyze(d, std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]),
                               maxSide);
  cut::print(r, maxSide);
  return 0;
}
