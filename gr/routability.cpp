// routability.cpp -- pre-route measurements of how hard a layout is to
// route, from the .gr file alone (HISTORY_COST_SWEEP.md Part 101). An
// analysis tool, not a router: nothing here routes anything.
//
//   routability <benchmark.gr>
//   routability --selftest
//
// Prints one line of key=value features:
//   nets, tiles: counts (non-giant nets with pins in >= 2 tiles).
//   util: total estimated demand / total 2D capacity, demand = sum over
//     nets of their pin bounding box half-perimeter x (minWidth + layer-1
//     spacing) -- the wire every net needs at least.
//   rudy_*: RUDY-style estimate per 2D edge. Each net spreads its
//     horizontal crossings uniformly over the rows of its pin bounding box
//     (each horizontal edge inside it gets amount / height) and its
//     vertical ones over the columns (amount / width); summed over nets and
//     divided by the edge's 2D capacity: rudy_p99 and rudy_p999 (99th and
//     99.9th percentile ratio over edges with capacity), rudy_over (share of
//     edges above 1.0), rudy_excess (sum over edges of max(0, estimate -
//     capacity) in units of the default net amount).
//   esc_*: pin escape -- for every tile, the demand of the nets that have
//     a pin in it and a pin outside it, over the 2D capacity of its four
//     edges (cutbound's cut ratio for a 1 x 1 rectangle); esc_max, esc_p999,
//     esc_over1 (tiles above 1.0: overflow forced there), esc_over09.
//   cut3_*: the same for every 3 x 3 block of tiles: cut3_max, cut3_over1.
//   long_share: share of nets whose half-perimeter exceeds 10% of the die's
//     half-perimeter.

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

namespace rt {

struct NetBox {
  int x0, y0, x1, y1;
  long long amount;
  std::vector<std::pair<int, int>> tiles; // distinct pin tiles
};

std::vector<NetBox> boxes(const Design &d) {
  const RoutingGrid &g = d.grid;
  std::vector<NetBox> out;
  for (const gr_io::Net &n : d.nets) {
    if (n.pins.size() > 1000 || n.pins.size() < 2)
      continue;
    NetBox b{INT_MAX, INT_MAX, INT_MIN, INT_MIN, static_cast<long long>(n.minWidth) + g.minSpacing[0], {}};
    for (const gr_io::Pin &p : n.pins) {
      b.x0 = std::min(b.x0, p.x);
      b.y0 = std::min(b.y0, p.y);
      b.x1 = std::max(b.x1, p.x);
      b.y1 = std::max(b.y1, p.y);
      std::pair<int, int> t{p.x, p.y};
      if (std::find(b.tiles.begin(), b.tiles.end(), t) == b.tiles.end())
        b.tiles.push_back(t);
    }
    if (b.tiles.size() < 2)
      continue;
    out.push_back(std::move(b));
  }
  return out;
}

struct Features {
  long long nets = 0, tiles = 0;
  double util = 0, rudyP99 = 0, rudyP999 = 0, rudyOver = 0, rudyExcess = 0;
  double escMax = 0, escP999 = 0, cut3Max = 0, longShare = 0;
  long long escOver1 = 0, escOver09 = 0, cut3Over1 = 0;
};

double pct(std::vector<double> v, double q) {
  if (v.empty())
    return 0;
  size_t k = std::min(v.size() - 1, static_cast<size_t>(q * v.size()));
  std::nth_element(v.begin(), v.begin() + static_cast<long>(k), v.end());
  return v[k];
}

Features analyze(const Design &d) {
  const RoutingGrid &g = d.grid;
  Features f;
  std::vector<NetBox> nb = boxes(d);
  f.nets = static_cast<long long>(nb.size());
  f.tiles = static_cast<long long>(g.nx) * g.ny;
  // 2D capacities.
  std::vector<double> capH(static_cast<size_t>(g.nx - 1) * g.ny, 0), capV(static_cast<size_t>(g.nx) * (g.ny - 1), 0);
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x)
      for (int l = 0; l < g.nLayers; ++l)
        capH[static_cast<size_t>(y) * (g.nx - 1) + x] += g.hcap[g.hIndex(x, y, l)];
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x)
      for (int l = 0; l < g.nLayers; ++l)
        capV[static_cast<size_t>(y) * g.nx + x] += g.vcap[g.vIndex(x, y, l)];
  double capTotal = 0;
  for (double c : capH)
    capTotal += c;
  for (double c : capV)
    capTotal += c;
  // Demand and RUDY via 2D difference arrays (each net adds a constant over
  // a rectangle of edges).
  std::vector<double> dH((g.nx + 1) * static_cast<size_t>(g.ny + 1), 0), dV(dH.size(), 0);
  auto addRect = [&](std::vector<double> &a, int x0, int y0, int x1, int y1, double v) { // inclusive
    a[static_cast<size_t>(y0) * (g.nx + 1) + x0] += v;
    a[static_cast<size_t>(y0) * (g.nx + 1) + x1 + 1] -= v;
    a[static_cast<size_t>(y1 + 1) * (g.nx + 1) + x0] -= v;
    a[static_cast<size_t>(y1 + 1) * (g.nx + 1) + x1 + 1] += v;
  };
  double demand = 0;
  long long longNets = 0;
  int dieHalf = g.nx + g.ny;
  for (const NetBox &b : nb) {
    int w = b.x1 - b.x0, h = b.y1 - b.y0;
    demand += static_cast<double>(w + h) * b.amount;
    longNets += (w + h) * 10 > dieHalf;
    if (w > 0) // horizontal edges x0..x1-1 in rows y0..y1
      addRect(dH, b.x0, b.y0, b.x1 - 1, b.y1, static_cast<double>(b.amount) / (h + 1));
    if (h > 0) // vertical edges y0..y1-1 in columns x0..x1
      addRect(dV, b.x0, b.y0, b.x1, b.y1 - 1, static_cast<double>(b.amount) / (w + 1));
  }
  f.util = capTotal > 0 ? demand / capTotal : 0;
  f.longShare = nb.empty() ? 0 : static_cast<double>(longNets) / nb.size();
  auto prefix = [&](std::vector<double> &a) {
    for (int y = 0; y <= g.ny; ++y)
      for (int x = 0; x <= g.nx; ++x) {
        double v = a[static_cast<size_t>(y) * (g.nx + 1) + x];
        if (x > 0)
          v += a[static_cast<size_t>(y) * (g.nx + 1) + x - 1];
        if (y > 0)
          v += a[static_cast<size_t>(y - 1) * (g.nx + 1) + x];
        if (x > 0 && y > 0)
          v -= a[static_cast<size_t>(y - 1) * (g.nx + 1) + x - 1];
        a[static_cast<size_t>(y) * (g.nx + 1) + x] = v;
      }
  };
  prefix(dH);
  prefix(dV);
  std::vector<double> ratios;
  long long over = 0;
  double excess = 0, unit = 1 + g.minSpacing[0];
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      double c = capH[static_cast<size_t>(y) * (g.nx - 1) + x], e = dH[static_cast<size_t>(y) * (g.nx + 1) + x];
      if (c <= 0)
        continue;
      ratios.push_back(e / c);
      over += e > c;
      excess += std::max(0.0, e - c) / unit;
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      double c = capV[static_cast<size_t>(y) * g.nx + x], e = dV[static_cast<size_t>(y) * (g.nx + 1) + x];
      if (c <= 0)
        continue;
      ratios.push_back(e / c);
      over += e > c;
      excess += std::max(0.0, e - c) / unit;
    }
  f.rudyP99 = pct(ratios, 0.99);
  f.rudyP999 = pct(ratios, 0.999);
  f.rudyOver = ratios.empty() ? 0 : static_cast<double>(over) / ratios.size();
  f.rudyExcess = excess;
  // Pin escape per tile and per 3 x 3 block: a net with pin tiles both
  // inside and outside the block must cross its boundary.
  auto blockRatios = [&](int side, std::vector<double> &out, long long &over1, long long &over09) {
    std::vector<double> dem(static_cast<size_t>(g.nx) * g.ny, 0);
    std::vector<int> stamp(dem.size(), -1);
    for (size_t k = 0; k < nb.size(); ++k) {
      const NetBox &b = nb[k];
      // Each block (anchored at its lower-left tile) that contains at least
      // one of the net's pin tiles but not its whole bounding box.
      for (const auto &t : b.tiles)
        for (int ay = std::max(0, t.second - side + 1); ay <= std::min(g.ny - side, t.second); ++ay)
          for (int ax = std::max(0, t.first - side + 1); ax <= std::min(g.nx - side, t.first); ++ax) {
            size_t a = static_cast<size_t>(ay) * g.nx + ax;
            if (stamp[a] == static_cast<int>(k))
              continue;
            stamp[a] = static_cast<int>(k);
            bool inside = b.x0 >= ax && b.x1 <= ax + side - 1 && b.y0 >= ay && b.y1 <= ay + side - 1;
            if (!inside)
              dem[a] += static_cast<double>(b.amount);
          }
    }
    for (int ay = 0; ay + side <= g.ny; ++ay)
      for (int ax = 0; ax + side <= g.nx; ++ax) {
        double cap = 0;
        for (int y = ay; y < ay + side; ++y) {
          if (ax > 0)
            cap += capH[static_cast<size_t>(y) * (g.nx - 1) + ax - 1];
          if (ax + side < g.nx)
            cap += capH[static_cast<size_t>(y) * (g.nx - 1) + ax + side - 1];
        }
        for (int x = ax; x < ax + side; ++x) {
          if (ay > 0)
            cap += capV[static_cast<size_t>(ay - 1) * g.nx + x];
          if (ay + side < g.ny)
            cap += capV[static_cast<size_t>(ay + side - 1) * g.nx + x];
        }
        double dm = dem[static_cast<size_t>(ay) * g.nx + ax];
        if (dm <= 0)
          continue;
        double r = cap > 0 ? dm / cap : 99.0;
        out.push_back(r);
        over1 += r > 1.0;
        over09 += r > 0.9;
      }
  };
  std::vector<double> esc, cut3;
  long long o1 = 0, o09 = 0, c1 = 0, c09 = 0;
  blockRatios(1, esc, o1, o09);
  blockRatios(3, cut3, c1, c09);
  f.escMax = esc.empty() ? 0 : *std::max_element(esc.begin(), esc.end());
  f.escP999 = pct(esc, 0.999);
  f.escOver1 = o1;
  f.escOver09 = o09;
  f.cut3Max = cut3.empty() ? 0 : *std::max_element(cut3.begin(), cut3.end());
  f.cut3Over1 = c1;
  return f;
}

} // namespace rt

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "routability self test\n";
  // 4 x 3 tiles, 2D capacity 4 per edge (one layer each way, width 1 +
  // spacing 1 -> 2 per net: two nets per edge). Net a: (0,0)-(3,0), span 3,
  // all in row 0: each horizontal edge of row 0 gets 2 / 1 = 2. Net b:
  // (0,0)-(3,2): w 3 h 2 -> each horizontal edge in rows 0-2 gets 2 / 3,
  // each vertical edge in columns 0-3 gets 2 / 4. Row 0's horizontal edges:
  // 2 + 0.667 = 2.667 of 4. Demand = (3 + 5) x 2 = 16; capacity: 9
  // horizontal + 8 vertical edges x 4 = 68 -> util 16 / 68.
  const char *gr = "grid 4 3 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\na 0 2 1\n5 5 1\n35 5 1\nb 1 2 1\n5 5 1\n35 25 1\nc 2 2 1\n15 15 1\n15 15 1\n0\n";
  const char *tmp = "routability_selftest.gr";
  {
    std::ofstream o(tmp);
    o << gr;
  }
  Design d = gr_io::readGr(tmp);
  std::remove(tmp);
  rt::Features f = rt::analyze(d);
  check(f.nets == 2, "two routable nets (c's pins share one tile)");
  check(std::abs(f.util - 16.0 / 68.0) < 1e-12, "util = 16 / 68");
  check(f.rudyOver == 0 && std::abs(f.rudyP999 - 2.0 / 3.0) < 1e-9,
        "RUDY: the busiest edges (row 0) at (2 + 2/3) / 4 = 0.667, none over");
  // Tile (0,0) holds a pin of both nets; both leave it: demand 4 against its
  // two existing edges (right and up) 4 + 4 = 8 -> 0.5.
  check(std::abs(f.escMax - 0.5) < 1e-12 && f.escOver1 == 0, "pin escape: tile (0,0) 4 / 8 = 0.5, the maximum");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr>\n";
    return 2;
  }
  Design d = gr_io::readGr(argv[1]);
  rt::Features f = rt::analyze(d);
  std::printf("nets=%lld tiles=%lld util=%.4f rudy_p99=%.3f rudy_p999=%.3f rudy_over=%.5f rudy_excess=%.0f "
              "esc_max=%.3f esc_p999=%.3f esc_over1=%lld esc_over09=%lld cut3_max=%.3f cut3_over1=%lld "
              "long_share=%.4f\n",
              f.nets, f.tiles, f.util, f.rudyP99, f.rudyP999, f.rudyOver, f.rudyExcess, f.escMax, f.escP999,
              f.escOver1, f.escOver09, f.cut3Max, f.cut3Over1, f.longShare);
  return 0;
}
