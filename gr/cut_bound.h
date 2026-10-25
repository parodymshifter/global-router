// cut_bound.h -- the cut-condition lower bound on overflow (HISTORY_COST_SWEEP.md Part 91), shared by the
// cutbound analysis tool and pd39route's forced-overflow report (Part 162). See cutbound.cpp's header for the
// argument: a rectangle's boundary must carry every net with a pin inside and a pin outside, so
// max(0, demand - boundary capacity) of overflow on that boundary is forced for any router; bounds of
// rectangles whose boundaries share no edge add up.
#pragma once

#include <algorithm>
#include <climits>
#include <cstdio>
#include <vector>

#include "gr_io.h"

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

inline long long hCap2(const gr_io::RoutingGrid &g, int x, int y) {
  long long c = 0;
  for (int l = 0; l < g.nLayers; ++l)
    c += g.hcap[g.hIndex(x, y, l)];
  return c;
}
inline long long vCap2(const gr_io::RoutingGrid &g, int x, int y) {
  long long c = 0;
  for (int l = 0; l < g.nLayers; ++l)
    c += g.vcap[g.vIndex(x, y, l)];
  return c;
}

// Boundary edges of a rectangle as (horizontal?, x, y) of the edge's
// lower/left tile; edges on the grid border do not exist and are skipped.
template <class F> void forBoundary(const gr_io::RoutingGrid &g, const Rect &r, F f) {
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

inline Result analyze(const gr_io::Design &d, int rx0, int ry0, int rx1, int ry1, int maxSide) {
  const gr_io::RoutingGrid &g = d.grid;
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

inline void print(const Result &r, int maxSide) {
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
