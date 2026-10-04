// grdiag.cpp -- diagnostics over an ISPD 2008 route file (IDEAS_TEST_PLAN.md
// phase 1). An analysis tool, not a router; nothing here changes a route.
//
//   grdiag <benchmark.gr> <solution.route> [merge_radius]
//   gunzip -c x.route.gz | grdiag <benchmark.gr> /dev/stdin
//   grdiag --selftest
//
// D1, residue anatomy (ideas 2 and 3): the real (per-layer) overflow,
// charged exactly as gr_verify / eval2008.pl do (minWidth + minSpacing per
// net per edge occurrence); the same usage collapsed to 2D (per tile edge,
// summed over layers against summed capacity -- what the router's 2D stage
// negotiates against), so the residue splits into what 2D routing left and
// what layer assignment added; and the overflowed edges clustered into
// hotspots (tiles joined when within merge_radius tiles, default 3, in both
// x and y), each with its overflow, bounding box and the number of routed
// nets with wire inside the box grown by 5 (the region stage's expand),
// against the same count for one box around everything.
//
// D2, wirelength anatomy (ideas 4 and 5): scored wirelength = wire tile
// edges + vias; vias split as in HISTORY_COST_SWEEP.md Part 69 (at a tile
// that is a 2D corner of the net, else at a pin tile, else "straight");
// per net, 2D wire against the rectilinear MST of its distinct pin tiles --
// the excess over the MST is detour (plus Steiner savings, which make it
// negative), reported separately for nets touching a hotspot box.
//
// D3, hotspot neighborhoods (HISTORY_COST_SWEEP.md Part 91: why virtual
// capacity moved newblue1's residue but not newblue4's): for the largest
// hotspots, the 2D capacity of their overflowed edges (against which a
// virtual-capacity reduction of k units is a share), and, in rings of tiles
// at Chebyshev distance 0, 1-2, 3-5, 6-10 and 11-20 from the hotspot's tile
// box, per direction: edges, 2D capacity and usage summed, free units
// (sum of max(0, cap - use)), and edges with room for one more default net
// (cap - use >= 1 + layer-1 spacing). An edge's distance is its nearer
// tile's.

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gr_io.h"

using gr_io::Design;
using gr_io::fail;
using gr_io::Net;
using gr_io::RoutingGrid;

namespace diag {

struct Segment {
  int x1, y1, l1, x2, y2, l2;
};
struct RouteNet {
  std::string name;
  std::vector<Segment> segs;
};

std::vector<RouteNet> readSolution(std::istream &in, const RoutingGrid &g) {
  std::vector<RouteNet> out;
  std::string line;
  bool open = false;
  RouteNet cur;
  while (std::getline(in, line)) {
    size_t b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
      continue;
    std::string s = line.substr(b, line.find_last_not_of(" \t\r\n") - b + 1);
    if (s == "!") {
      if (open)
        out.push_back(std::move(cur));
      cur = RouteNet();
      open = false;
      continue;
    }
    if (s[0] == '(') {
      long long x1, y1, l1, x2, y2, l2;
      if (!open || std::sscanf(s.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        fail("malformed route line: " + s);
      cur.segs.push_back({g.colOf(x1), g.rowOf(y1), static_cast<int>(l1) - 1, g.colOf(x2), g.rowOf(y2),
                          static_cast<int>(l2) - 1});
      continue;
    }
    if (open)
      out.push_back(std::move(cur));
    cur = RouteNet();
    std::istringstream hs(s);
    hs >> cur.name;
    open = true;
  }
  if (open)
    out.push_back(std::move(cur));
  return out;
}

struct Box {
  int x0, y0, x1, y1;
  bool has(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

struct Ring {
  long long edges = 0, cap = 0, use = 0, freeUnits = 0, roomy = 0;
};
constexpr int kRings = 5;
constexpr int kRingLo[kRings] = {0, 1, 3, 6, 11}, kRingHi[kRings] = {0, 2, 5, 10, 20};

struct Cluster {
  long long overflow = 0, edges = 0, nets = 0;
  Box box{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
  // D3 (filled for the largest hotspots only).
  bool hood = false;
  std::vector<long long> ovCaps; // 2D capacity of each overflowed 2D edge with a tile in the box
  Ring h[kRings], v[kRings];
};

struct NetWl {
  long long wire = 0, vias = 0, mst = 0, hpwl = 0;
  long long viaCorner = 0, viaPin = 0, viaStraight = 0, corners = 0;
  bool inHotspot = false;
};

struct Result {
  long long overflow3D = 0, overflow2D = 0, overflowOnClean2D = 0, overflowedEdges = 0;
  std::vector<Cluster> clusters; // largest overflow first
  long long oneBoxNets = 0;
  Box oneBox{0, 0, -1, -1};
  long long wire = 0, vias = 0, mst = 0, hpwl = 0, viaCorner = 0, viaPin = 0, viaStraight = 0, corners = 0;
  long long detourPos = 0, detourNeg = 0, hotNets = 0, hotWire = 0, hotMst = 0, hotVias = 0;
  long long netsScored = 0;
};

// Rectilinear MST length over distinct points (Prim, O(k^2)).
long long rmst(const std::vector<std::pair<int, int>> &p) {
  size_t k = p.size();
  if (k < 2)
    return 0;
  std::vector<long long> d(k, LLONG_MAX);
  std::vector<char> in(k, 0);
  d[0] = 0;
  long long total = 0;
  for (size_t it = 0; it < k; ++it) {
    size_t u = k;
    for (size_t i = 0; i < k; ++i)
      if (!in[i] && (u == k || d[i] < d[u]))
        u = i;
    in[u] = 1;
    total += d[u];
    for (size_t i = 0; i < k; ++i)
      if (!in[i]) {
        long long w = std::abs(p[i].first - p[u].first) + std::abs(p[i].second - p[u].second);
        d[i] = std::min(d[i], w);
      }
  }
  return total;
}

Result analyze(const Design &d, const std::vector<RouteNet> &sol, int radius) {
  const RoutingGrid &g = d.grid;
  Result r;
  std::unordered_map<std::string, const Net *> byName;
  for (const Net &n : d.nets)
    byName[n.name] = &n;
  std::vector<int> hu(g.hCount(), 0), vu(g.vCount(), 0);
  for (const RouteNet &rn : sol) {
    auto it = byName.find(rn.name);
    int width = it == byName.end() ? 1 : it->second->minWidth;
    for (const Segment &s : rn.segs) {
      if (s.l1 != s.l2)
        continue;
      if (s.y1 == s.y2 && s.x1 != s.x2)
        for (int x = std::min(s.x1, s.x2); x < std::max(s.x1, s.x2); ++x)
          hu[g.hIndex(x, s.y1, s.l1)] += width + g.minSpacing[s.l1];
      else if (s.x1 == s.x2 && s.y1 != s.y2)
        for (int y = std::min(s.y1, s.y2); y < std::max(s.y1, s.y2); ++y)
          vu[g.vIndex(s.x1, y, s.l1)] += width + g.minSpacing[s.l1];
    }
  }
  // D1: real overflow, 2D collapse, clusters.
  std::vector<char> mark(static_cast<size_t>(g.nx) * g.ny, 0);
  std::vector<long long> tileOv(static_cast<size_t>(g.nx) * g.ny, 0);
  auto markTile = [&](int x, int y, long long ov) {
    mark[static_cast<size_t>(y) * g.nx + x] = 1;
    tileOv[static_cast<size_t>(y) * g.nx + x] += ov;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      long long u2 = 0, c2 = 0, ov3 = 0;
      for (int l = 0; l < g.nLayers; ++l) {
        size_t i = g.hIndex(x, y, l);
        u2 += hu[i];
        c2 += g.hcap[i];
        if (hu[i] > g.hcap[i]) {
          ov3 += hu[i] - g.hcap[i];
          ++r.overflowedEdges;
        }
      }
      long long ov2 = std::max(0LL, u2 - c2);
      r.overflow3D += ov3;
      r.overflow2D += ov2;
      if (ov2 == 0)
        r.overflowOnClean2D += ov3;
      if (ov3 > 0) {
        markTile(x, y, ov3); // the edge's overflow is charged to its left tile
        mark[static_cast<size_t>(y) * g.nx + x + 1] = 1;
      }
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      long long u2 = 0, c2 = 0, ov3 = 0;
      for (int l = 0; l < g.nLayers; ++l) {
        size_t i = g.vIndex(x, y, l);
        u2 += vu[i];
        c2 += g.vcap[i];
        if (vu[i] > g.vcap[i]) {
          ov3 += vu[i] - g.vcap[i];
          ++r.overflowedEdges;
        }
      }
      long long ov2 = std::max(0LL, u2 - c2);
      r.overflow3D += ov3;
      r.overflow2D += ov2;
      if (ov2 == 0)
        r.overflowOnClean2D += ov3;
      if (ov3 > 0) {
        markTile(x, y, ov3);
        mark[static_cast<size_t>(y + 1) * g.nx + x] = 1;
      }
    }
  // Cluster marked tiles: BFS joining marked tiles within `radius` in x and y.
  std::vector<int> comp(mark.size(), -1);
  for (size_t start = 0; start < mark.size(); ++start) {
    if (!mark[start] || comp[start] >= 0)
      continue;
    Cluster c;
    int id = static_cast<int>(r.clusters.size());
    std::vector<size_t> stack{start};
    comp[start] = id;
    while (!stack.empty()) {
      size_t t = stack.back();
      stack.pop_back();
      int x = static_cast<int>(t % g.nx), y = static_cast<int>(t / g.nx);
      c.overflow += tileOv[t];
      c.box.x0 = std::min(c.box.x0, x);
      c.box.y0 = std::min(c.box.y0, y);
      c.box.x1 = std::max(c.box.x1, x);
      c.box.y1 = std::max(c.box.y1, y);
      for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
          int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= g.nx || ny >= g.ny)
            continue;
          size_t n = static_cast<size_t>(ny) * g.nx + nx;
          if (mark[n] && comp[n] < 0) {
            comp[n] = id;
            stack.push_back(n);
          }
        }
    }
    r.clusters.push_back(c);
  }
  auto grow = [&](Box b, int e) {
    return Box{std::max(0, b.x0 - e), std::max(0, b.y0 - e), std::min(g.nx - 1, b.x1 + e), std::min(g.ny - 1, b.y1 + e)};
  };
  std::vector<Box> boxes;
  for (const Cluster &c : r.clusters)
    boxes.push_back(grow(c.box, 5));
  if (!r.clusters.empty()) {
    Box all{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
    for (const Cluster &c : r.clusters) {
      all.x0 = std::min(all.x0, c.box.x0);
      all.y0 = std::min(all.y0, c.box.y0);
      all.x1 = std::max(all.x1, c.box.x1);
      all.y1 = std::max(all.y1, c.box.y1);
    }
    r.oneBox = grow(all, 5);
  }

  // D2 and net counts per box.
  for (const RouteNet &rn : sol) {
    auto it = byName.find(rn.name);
    if (it == byName.end())
      continue;
    const Net &n = *it->second;
    NetWl w;
    std::unordered_map<long long, int> orient; // tile -> 1 (h) | 2 (v)
    std::map<std::pair<int, int>, long long> viaAt;
    std::vector<char> touches(boxes.size(), 0);
    bool touchesOne = false;
    auto touch = [&](int x, int y) {
      for (size_t b = 0; b < boxes.size(); ++b)
        if (!touches[b] && boxes[b].has(x, y))
          touches[b] = 1;
      if (r.oneBox.has(x, y))
        touchesOne = true;
    };
    for (const Segment &s : rn.segs) {
      if (s.l1 != s.l2) {
        viaAt[{s.x1, s.y1}] += std::abs(s.l2 - s.l1);
        w.vias += std::abs(s.l2 - s.l1);
        continue;
      }
      if (s.y1 == s.y2 && s.x1 != s.x2) {
        for (int x = std::min(s.x1, s.x2); x < std::max(s.x1, s.x2); ++x) {
          ++w.wire;
          orient[static_cast<long long>(s.y1) * g.nx + x] |= 1;
          orient[static_cast<long long>(s.y1) * g.nx + x + 1] |= 1;
          touch(x, s.y1);
          touch(x + 1, s.y1);
        }
      } else if (s.x1 == s.x2 && s.y1 != s.y2) {
        for (int y = std::min(s.y1, s.y2); y < std::max(s.y1, s.y2); ++y) {
          ++w.wire;
          orient[static_cast<long long>(y) * g.nx + s.x1] |= 2;
          orient[static_cast<long long>(y + 1) * g.nx + s.x1] |= 2;
          touch(s.x1, y);
          touch(s.x1, y + 1);
        }
      }
    }
    std::unordered_set<long long> pinTiles;
    std::vector<std::pair<int, int>> pts;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const gr_io::Pin &p : n.pins)
      if (pinTiles.insert(static_cast<long long>(p.y) * g.nx + p.x).second) {
        pts.push_back({p.x, p.y});
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
      }
    if (pts.size() >= 2) {
      w.hpwl = (x1 - x0) + (y1 - y0);
      w.mst = pts.size() <= 1000 ? rmst(pts) : w.hpwl;
    }
    for (const auto &[t, b] : orient)
      w.corners += b == 3;
    for (const auto &[xy, cnt] : viaAt) {
      long long t = static_cast<long long>(xy.second) * g.nx + xy.first;
      auto o = orient.find(t);
      if (o != orient.end() && o->second == 3)
        w.viaCorner += cnt;
      else if (pinTiles.count(t))
        w.viaPin += cnt;
      else
        w.viaStraight += cnt;
    }
    for (size_t b = 0; b < boxes.size(); ++b)
      r.clusters[b].nets += touches[b];
    r.oneBoxNets += touchesOne;
    w.inHotspot = std::any_of(touches.begin(), touches.end(), [](char c) { return c != 0; });
    ++r.netsScored;
    r.wire += w.wire;
    r.vias += w.vias;
    r.mst += w.mst;
    r.hpwl += w.hpwl;
    r.corners += w.corners;
    r.viaCorner += w.viaCorner;
    r.viaPin += w.viaPin;
    r.viaStraight += w.viaStraight;
    long long det = w.wire - w.mst;
    (det >= 0 ? r.detourPos : r.detourNeg) += det;
    if (w.inHotspot) {
      ++r.hotNets;
      r.hotWire += w.wire;
      r.hotMst += w.mst;
      r.hotVias += w.vias;
    }
  }
  std::sort(r.clusters.begin(), r.clusters.end(),
            [](const Cluster &a, const Cluster &b) { return a.overflow > b.overflow; });
  // D3: neighborhoods of the largest hotspots, on 2D-collapsed usage.
  const int amount = 1 + g.minSpacing[0];
  auto sumH = [&](int x, int y, long long &u2, long long &c2) {
    u2 = c2 = 0;
    for (int l = 0; l < g.nLayers; ++l) {
      size_t i = g.hIndex(x, y, l);
      u2 += hu[i];
      c2 += g.hcap[i];
    }
  };
  auto sumV = [&](int x, int y, long long &u2, long long &c2) {
    u2 = c2 = 0;
    for (int l = 0; l < g.nLayers; ++l) {
      size_t i = g.vIndex(x, y, l);
      u2 += vu[i];
      c2 += g.vcap[i];
    }
  };
  for (size_t k = 0; k < r.clusters.size() && k < 4; ++k) {
    Cluster &c = r.clusters[k];
    c.hood = true;
    auto dist = [&](int x, int y) {
      int dx = std::max({0, c.box.x0 - x, x - c.box.x1}), dy = std::max({0, c.box.y0 - y, y - c.box.y1});
      return std::max(dx, dy);
    };
    auto ringOf = [&](int dd) {
      for (int q = 0; q < kRings; ++q)
        if (dd >= kRingLo[q] && dd <= kRingHi[q])
          return q;
      return -1;
    };
    auto add = [&](Ring &rg, long long u2, long long c2) {
      ++rg.edges;
      rg.cap += c2;
      rg.use += u2;
      rg.freeUnits += std::max(0LL, c2 - u2);
      rg.roomy += (c2 - u2 >= amount);
    };
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x) {
        int dd = std::min(dist(x, y), dist(x + 1, y)), q = ringOf(dd);
        if (q < 0)
          continue;
        long long u2, c2;
        sumH(x, y, u2, c2);
        add(c.h[q], u2, c2);
        if (dd == 0 && u2 > c2)
          c.ovCaps.push_back(c2);
      }
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x) {
        int dd = std::min(dist(x, y), dist(x, y + 1)), q = ringOf(dd);
        if (q < 0)
          continue;
        long long u2, c2;
        sumV(x, y, u2, c2);
        add(c.v[q], u2, c2);
        if (dd == 0 && u2 > c2)
          c.ovCaps.push_back(c2);
      }
    std::sort(c.ovCaps.begin(), c.ovCaps.end());
  }
  return r;
}

void print(const Result &r) {
  std::printf("D1 residue anatomy\n");
  std::printf("  real overflow %lld on %lld edges; 2D-collapsed overflow %lld; real overflow on edges "
              "whose 2D collapse is within capacity (layer assignment's share) %lld\n",
              r.overflow3D, r.overflowedEdges, r.overflow2D, r.overflowOnClean2D);
  std::printf("  hotspots %zu; one box around all: x %d-%d y %d-%d, %lld nets touch it\n", r.clusters.size(),
              r.oneBox.x0, r.oneBox.x1, r.oneBox.y0, r.oneBox.y1, r.oneBoxNets);
  long long sumNets = 0;
  for (const Cluster &c : r.clusters)
    sumNets += c.nets;
  std::printf("  nets touching the per-hotspot boxes, summed: %lld\n", sumNets);
  size_t shown = 0;
  for (const Cluster &c : r.clusters) {
    if (++shown > 12)
      break;
    std::printf("    overflow %6lld  box x %d-%d y %d-%d (%d x %d)  nets %lld\n", c.overflow, c.box.x0, c.box.x1,
                c.box.y0, c.box.y1, c.box.x1 - c.box.x0 + 1, c.box.y1 - c.box.y0 + 1, c.nets);
  }
  std::printf("D3 hotspot neighborhoods (2D; free = sum max(0, cap - use); room = edges with space for one more net)\n");
  for (const Cluster &c : r.clusters) {
    if (!c.hood)
      continue;
    std::printf("  hotspot overflow %lld, box x %d-%d y %d-%d: %zu overflowed edges, 2D capacity min %lld median "
                "%lld max %lld\n",
                c.overflow, c.box.x0, c.box.x1, c.box.y0, c.box.y1, c.ovCaps.size(),
                c.ovCaps.empty() ? 0 : c.ovCaps.front(), c.ovCaps.empty() ? 0 : c.ovCaps[c.ovCaps.size() / 2],
                c.ovCaps.empty() ? 0 : c.ovCaps.back());
    for (int q = 0; q < kRings; ++q) {
      const Ring &h = c.h[q], &v = c.v[q];
      std::printf("    ring %2d-%-2d  H: %5lld edges, use/cap %5.1f%%, free %6lld, room %5lld   V: %5lld edges, use/cap "
                  "%5.1f%%, free %6lld, room %5lld\n",
                  kRingLo[q], kRingHi[q], h.edges, 100.0 * h.use / std::max(1LL, h.cap), h.freeUnits, h.roomy,
                  v.edges, 100.0 * v.use / std::max(1LL, v.cap), v.freeUnits, v.roomy);
    }
  }
  std::printf("D2 wirelength anatomy (%lld routed nets)\n", r.netsScored);
  std::printf("  scored %lld = wire %lld + vias %lld (vias %.1f%%)\n", r.wire + r.vias, r.wire, r.vias,
              100.0 * r.vias / std::max(1LL, r.wire + r.vias));
  std::printf("  vias at corners %lld, at pin tiles %lld, straight %lld; 2D corners %lld (vias per corner %.2f)\n",
              r.viaCorner, r.viaPin, r.viaStraight, r.corners, double(r.viaCorner) / std::max(1LL, r.corners));
  std::printf("  wire vs pin-tile MST: MST %lld, HPWL %lld; wire - MST: +%lld over nets above their MST, %lld "
              "below (Steiner savings) -> net %+.2f%%\n",
              r.mst, r.hpwl, r.detourPos, r.detourNeg, 100.0 * (r.wire - r.mst) / std::max(1LL, r.mst));
  std::printf("  nets touching a hotspot box: %lld, wire %lld vs MST %lld (%+.2f%%), vias %lld\n", r.hotNets,
              r.hotWire, r.hotMst, 100.0 * (r.hotWire - r.hotMst) / std::max(1LL, r.hotMst), r.hotVias);
}

} // namespace diag

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "grdiag self test\n";
  // 10 x 3 tiles, 2 layers (layer 1 horizontal cap 2, layer 2 vertical cap
  // 2), width 1 spacing 1: one net per edge fits. Tiles 10 x 10.
  const char *gr = "grid 10 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\n"
                   "a 0 2 1\n5 5 1\n35 5 1\n"
                   "b 1 2 1\n5 5 1\n35 5 1\n"
                   "c 2 3 1\n85 5 1\n95 5 1\n95 25 1\n0\n";
  const char *tmp = "grdiag_selftest.gr";
  {
    std::ofstream f(tmp);
    f << gr;
  }
  Design d = gr_io::readGr(tmp);
  std::remove(tmp);
  // a and b both along row 0, x 0 -> 3 on layer 1: 3 edges each 2 over
  // (usage 4, cap 2) -> real overflow 6, 2D overflow 6 (layer 2 has no
  // horizontal capacity). c: pin tiles (8,0), (9,0), (9,2): row 0 x 8 -> 9 on
  // layer 1, via 1-2 at (9,0) (a corner), column 9 y 0 -> 2 on layer 2, and
  // a detour: MST = 1 + 2 = 3, wire 3. Its via is at a corner tile.
  std::istringstream sol("a 0\n(5,5,1)-(35,5,1)\n!\n"
                         "b 1\n(5,5,1)-(35,5,1)\n!\n"
                         "c 2\n(85,5,1)-(95,5,1)\n(95,5,1)-(95,5,2)\n(95,5,2)-(95,25,2)\n!\n");
  std::vector<diag::RouteNet> nets = diag::readSolution(sol, d.grid);
  diag::Result r = diag::analyze(d, nets, 3);
  check(nets.size() == 3 && nets[2].segs.size() == 3, "route file read: 3 nets, c has 3 segments");
  check(r.overflow3D == 6 && r.overflowedEdges == 3 && r.overflow2D == 6 && r.overflowOnClean2D == 0,
        "real overflow 6 on 3 edges, all of it 2D (" + std::to_string(r.overflow3D) + ", " +
            std::to_string(r.overflow2D) + ")");
  check(r.clusters.size() == 1 && r.clusters[0].overflow == 6 && r.clusters[0].box.x0 == 0 &&
            r.clusters[0].box.x1 == 3,
        "one hotspot, tiles x 0-3 of row 0, overflow 6");
  // Box grown by 5: x 0-8, y 0-2 -> a, b, and c (its wire starts at x 8).
  check(r.clusters[0].nets == 3 && r.oneBoxNets == 3, "nets touching the grown box: 3");
  check(r.wire == 9 && r.vias == 1 && r.viaCorner == 1 && r.corners == 1, "wire 3+3+3, one via at c's corner");
  check(r.mst == 3 + 3 + 3 && r.detourPos == 0 && r.detourNeg == 0, "each net's wire equals its pin-tile MST");
  check(diag::rmst({{0, 0}, {4, 0}, {4, 3}, {0, 3}}) == 10, "RMST of a 4 x 3 rectangle's corners is 10");
  // D3, ring 0 (tiles x 0-3 of row 0): horizontal edges x 0, 1, 2 (usage
  // 4, cap 2 each) and x 3 (unused, cap 2): 4 edges, cap 8, use 12, free 2,
  // one with room for a net (amount 2); the 3 overflowed edges have 2D
  // capacity 2. Vertical edges touching the box: (x 0-3, y 0-1), unused,
  // vertical capacity 2 each: 4 edges, cap 8, free 8, all roomy.
  const diag::Cluster &h0 = r.clusters[0];
  check(h0.hood && h0.h[0].edges == 4 && h0.h[0].cap == 8 && h0.h[0].use == 12 && h0.h[0].freeUnits == 2 &&
            h0.h[0].roomy == 1,
        "D3 ring 0, horizontal: 4 edges, cap 8, use 12, free 2, room 1");
  check(h0.v[0].edges == 4 && h0.v[0].cap == 8 && h0.v[0].freeUnits == 8 && h0.v[0].roomy == 4,
        "D3 ring 0, vertical: 4 edges, cap 8, free 8, room 4");
  check(h0.ovCaps.size() == 3 && h0.ovCaps.front() == 2 && h0.ovCaps.back() == 2,
        "D3: the 3 overflowed edges have 2D capacity 2");
  // Overflowed runs on tiles x 0-2 and x 6-8 (4 tiles apart): radius 3
  // keeps them apart, radius 4 joins them.
  std::istringstream sol2("a 0\n(5,5,1)-(25,5,1)\n(65,5,1)-(85,5,1)\n!\nb 1\n(5,5,1)-(25,5,1)\n(65,5,1)-"
                          "(85,5,1)\n!\n");
  std::vector<diag::RouteNet> n2 = diag::readSolution(sol2, d.grid);
  diag::Result r3 = diag::analyze(d, n2, 3), r4 = diag::analyze(d, n2, 4);
  check(r3.clusters.size() == 2 && r4.clusters.size() == 1,
        "hotspots 4 tiles apart: two at merge radius 3, one at radius 4 (" + std::to_string(r3.clusters.size()) +
            ", " + std::to_string(r4.clusters.size()) + ")");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <solution.route> [merge_radius]\n";
    return 2;
  }
  int radius = argc == 4 ? std::atoi(argv[3]) : 3;
  Design d = gr_io::readGr(argv[1]);
  std::ifstream in(argv[2]);
  if (!in)
    fail(std::string("cannot open ") + argv[2]);
  std::vector<diag::RouteNet> sol = diag::readSolution(in, d.grid);
  diag::print(diag::analyze(d, sol, radius));
  return 0;
}
