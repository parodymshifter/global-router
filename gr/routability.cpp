// routability.cpp -- pre-route measurements of how hard a layout is to
// route, from the input files alone (HISTORY_COST_SWEEP.md Parts 101, 130).
// An analysis tool, not a router: nothing here routes anything.
//
//   routability <benchmark.gr>                (ISPD 2008)
//   routability <design.cap> <design.net>     (ISPD 2024 / 2025; each pin at
//                                              its first access point)
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
// Part 130 (difficulty predictors; areas in GCells, the routing grid's
// cells; "nets" below are nets with pins in >= 2 GCells unless noted):
//   pins, pin_density: pins of every net with >= 2 pins, and per GCell.
//   pin_per_htrack, pin_per_vtrack: pins per track of horizontal (vertical)
//     2D routing capacity, summed over the die.
//   row_cv, col_cv: coefficient of variation (std / mean) of pins per GCell
//     row and per column -- how unevenly pins spread vertically / across.
//   ppn_mean, ppn_median, ppn_p90, ppn_p99, ppn_max, ppn_skew, ppn_share2,
//     ppn_share10: pins per net (nets with >= 2 pins), its sample skewness,
//     and the shares of 2-pin nets and nets with >= 10 pins.
//   big_nets, big_share: nets whose pin bounding box covers > 25% of the
//     die's GCells; left out of everything below.
//   regions, region_top: connected regions of the union of the remaining
//     nets' bounding boxes (GCells joined edge to edge), and the largest
//     region's share of those nets' pins.
//   reg{4,8,16}_cv, _maxmean, _gini: the die cut into k x k equal regions;
//     the spread of their pin densities (pins / GCells).
//   reg{4,8,16}_span2: share of the remaining nets with pins in more than
//     2 of those regions.
//   rsmt_per_net, rsmt_per_gcell, rsmt_per_track: total rectilinear Steiner
//     tree length (GCell steps) of the remaining nets, per net, per GCell
//     of the die, and per track of 2D capacity; rsmt_per_net_all with every
//     net. The tree: exact for 2-3 pin GCells, otherwise a rectilinear
//     minimum spanning tree improved by Steiner points (each node's best
//     median point, repeated until nothing improves; within a few percent).
//   pv_mean, pv_p99, pv_max: pins per GCell that holds at least one pin.
//   layers, layers_h, layers_v: routing layers with any capacity (in each
//     direction); tracks_h, tracks_v: 2D routing tracks per GCell, summed
//     over layers (the supply each GCell offers in each direction).

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <cmath>
#include <numeric>

#include "g24_io.h"
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


// ---- Part 130: difficulty predictors ----
struct Extra {
  long long pins = 0, nets = 0, bigNets = 0, regions = 0, pvMax = 0, ppnMax = 0;
  double pinDensity = 0, pinPerHTrack = 0, pinPerVTrack = 0, rowCv = 0, colCv = 0;
  double ppnMean = 0, ppnMedian = 0, ppnP90 = 0, ppnP99 = 0, ppnSkew = 0, ppnShare2 = 0, ppnShare10 = 0;
  double bigShare = 0, regionTop = 0;
  double regCv[3] = {0, 0, 0}, regMaxMean[3] = {0, 0, 0}, regGini[3] = {0, 0, 0}, regSpan2[3] = {0, 0, 0};
  double rsmtPerNet = 0, rsmtPerGcell = 0, rsmtPerTrack = 0, rsmtPerNetAll = 0;
  double pvMean = 0, pvP99 = 0;
  int layers = 0, layersH = 0, layersV = 0;
  double tracksH = 0, tracksV = 0;
};

double cv(const std::vector<double> &v) {
  if (v.empty())
    return 0;
  double m = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size()), s2 = 0;
  for (double x : v)
    s2 += (x - m) * (x - m);
  return m > 0 ? std::sqrt(s2 / static_cast<double>(v.size())) / m : 0;
}

// Gini coefficient of non-negative values (0: all equal; near 1: all in one).
double gini(std::vector<double> v) {
  if (v.empty())
    return 0;
  std::sort(v.begin(), v.end());
  double sum = 0, weighted = 0, n = static_cast<double>(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    sum += v[i];
    weighted += (2.0 * static_cast<double>(i + 1) - n - 1) * v[i];
  }
  return sum > 0 ? weighted / (n * sum) : 0;
}

// Rectilinear Steiner tree length over distinct points (GCells): exact for
// 2 and 3 points; otherwise Prim's rectilinear minimum spanning tree, then
// Steiner points -- for a node u with tree neighbours a and b, the median
// point s of (u, a, b) replaces edges u-a and u-b with u-s, s-a, s-b, never
// longer -- taken greedily (each node's best pair) until nothing improves.
long long rsmtLength(std::vector<std::pair<int, int>> pts) {
  auto d = [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
    return static_cast<long long>(std::abs(a.first - b.first)) + std::abs(a.second - b.second);
  };
  size_t k = pts.size();
  if (k < 2)
    return 0;
  if (k == 2)
    return d(pts[0], pts[1]);
  if (k == 3) {
    int x0 = std::min({pts[0].first, pts[1].first, pts[2].first});
    int x1 = std::max({pts[0].first, pts[1].first, pts[2].first});
    int y0 = std::min({pts[0].second, pts[1].second, pts[2].second});
    int y1 = std::max({pts[0].second, pts[1].second, pts[2].second});
    return static_cast<long long>(x1 - x0) + (y1 - y0);
  }
  std::vector<std::vector<int>> adj(k);
  std::vector<long long> best(k, LLONG_MAX);
  std::vector<int> from(k, -1);
  std::vector<char> in(k, 0);
  best[0] = 0;
  for (size_t it = 0; it < k; ++it) {
    int u = -1;
    for (size_t v = 0; v < k; ++v)
      if (!in[v] && (u < 0 || best[v] < best[static_cast<size_t>(u)]))
        u = static_cast<int>(v);
    in[static_cast<size_t>(u)] = 1;
    if (from[static_cast<size_t>(u)] >= 0) {
      adj[static_cast<size_t>(u)].push_back(from[static_cast<size_t>(u)]);
      adj[static_cast<size_t>(from[static_cast<size_t>(u)])].push_back(u);
    }
    for (size_t v = 0; v < k; ++v)
      if (!in[v]) {
        long long w = d(pts[static_cast<size_t>(u)], pts[v]);
        if (w < best[v]) {
          best[v] = w;
          from[v] = u;
        }
      }
  }
  auto unlink = [&](int a, int b) {
    auto &A = adj[static_cast<size_t>(a)], &B = adj[static_cast<size_t>(b)];
    A.erase(std::find(A.begin(), A.end(), b));
    B.erase(std::find(B.begin(), B.end(), a));
  };
  auto link = [&](int a, int b) {
    adj[static_cast<size_t>(a)].push_back(b);
    adj[static_cast<size_t>(b)].push_back(a);
  };
  for (int pass = 0; pass < 8; ++pass) {
    bool changed = false;
    for (size_t u = 0; u < pts.size(); ++u) {
      long long bestGain = 0;
      int ba = -1, bb = -1;
      std::pair<int, int> bs;
      const std::vector<int> nb = adj[u];
      for (size_t i = 0; i < nb.size(); ++i)
        for (size_t j = i + 1; j < nb.size(); ++j) {
          const std::pair<int, int> P = pts[u], A = pts[static_cast<size_t>(nb[i])], B = pts[static_cast<size_t>(nb[j])];
          std::pair<int, int> sp{std::max(std::min(P.first, A.first), std::min(std::max(P.first, A.first), B.first)),
                                 std::max(std::min(P.second, A.second), std::min(std::max(P.second, A.second), B.second))};
          long long gain = d(P, A) + d(P, B) - (d(P, sp) + d(sp, A) + d(sp, B));
          if (gain > bestGain) {
            bestGain = gain;
            ba = nb[i];
            bb = nb[j];
            bs = sp;
          }
        }
      if (ba < 0)
        continue;
      int s = static_cast<int>(pts.size());
      pts.push_back(bs);
      adj.emplace_back();
      unlink(static_cast<int>(u), ba);
      unlink(static_cast<int>(u), bb);
      link(static_cast<int>(u), s);
      link(s, ba);
      link(s, bb);
      changed = true;
    }
    if (!changed)
      break;
  }
  long long len = 0;
  for (size_t u = 0; u < pts.size(); ++u)
    for (int v : adj[u])
      if (static_cast<int>(u) < v)
        len += d(pts[u], pts[static_cast<size_t>(v)]);
  return len;
}

Extra analyzeExtra(const Design &d) {
  const RoutingGrid &g = d.grid;
  Extra e;
  const size_t nT = static_cast<size_t>(g.nx) * static_cast<size_t>(g.ny);
  const double unit = 1 + g.minSpacing[0];
  double hTracks = 0, vTracks = 0;
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        hTracks += g.hcap[g.hIndex(x, y, l)] / unit;
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        vTracks += g.vcap[g.vIndex(x, y, l)] / unit;
  }
  for (int l = 0; l < g.nLayers; ++l) {
    bool h = false, v = false;
    for (int y = 0; y < g.ny && !h; ++y)
      for (int x = 0; x + 1 < g.nx && !h; ++x)
        h = g.hcap[g.hIndex(x, y, l)] > 0;
    for (int y = 0; y + 1 < g.ny && !v; ++y)
      for (int x = 0; x < g.nx && !v; ++x)
        v = g.vcap[g.vIndex(x, y, l)] > 0;
    e.layersH += h;
    e.layersV += v;
    e.layers += h || v;
  }
  e.tracksH = hTracks / static_cast<double>(nT);
  e.tracksV = vTracks / static_cast<double>(nT);
  std::vector<long long> perTile(nT, 0);
  std::vector<double> rows(static_cast<size_t>(g.ny), 0), cols(static_cast<size_t>(g.nx), 0), ppn;
  struct Kept {
    std::vector<std::pair<int, int>> tiles;
    int x0, y0, x1, y1;
    long long pins;
  };
  std::vector<Kept> kept;
  long long rsmtAll = 0, nAll = 0;
  double keptRsmt = 0;
  for (const gr_io::Net &n : d.nets) {
    if (n.pins.size() < 2)
      continue;
    ppn.push_back(static_cast<double>(n.pins.size()));
    e.pins += static_cast<long long>(n.pins.size());
    Kept k{{}, INT_MAX, INT_MAX, INT_MIN, INT_MIN, static_cast<long long>(n.pins.size())};
    for (const gr_io::Pin &p : n.pins) {
      ++perTile[static_cast<size_t>(p.y) * static_cast<size_t>(g.nx) + static_cast<size_t>(p.x)];
      rows[static_cast<size_t>(p.y)] += 1;
      cols[static_cast<size_t>(p.x)] += 1;
      k.tiles.push_back({p.x, p.y});
      k.x0 = std::min(k.x0, p.x);
      k.y0 = std::min(k.y0, p.y);
      k.x1 = std::max(k.x1, p.x);
      k.y1 = std::max(k.y1, p.y);
    }
    std::sort(k.tiles.begin(), k.tiles.end());
    k.tiles.erase(std::unique(k.tiles.begin(), k.tiles.end()), k.tiles.end());
    if (k.tiles.size() < 2)
      continue;
    ++e.nets;
    long long len = rsmtLength(k.tiles);
    rsmtAll += len;
    ++nAll;
    double boxArea = static_cast<double>(k.x1 - k.x0 + 1) * static_cast<double>(k.y1 - k.y0 + 1);
    if (boxArea > 0.25 * static_cast<double>(nT)) {
      ++e.bigNets;
      continue;
    }
    keptRsmt += static_cast<double>(len);
    kept.push_back(std::move(k));
  }
  e.pinDensity = static_cast<double>(e.pins) / static_cast<double>(nT);
  e.pinPerHTrack = hTracks > 0 ? static_cast<double>(e.pins) / hTracks : 0;
  e.pinPerVTrack = vTracks > 0 ? static_cast<double>(e.pins) / vTracks : 0;
  e.rowCv = cv(rows);
  e.colCv = cv(cols);
  if (!ppn.empty()) {
    double n = static_cast<double>(ppn.size());
    double m = std::accumulate(ppn.begin(), ppn.end(), 0.0) / n, m2 = 0, m3 = 0;
    for (double x : ppn) {
      m2 += (x - m) * (x - m);
      m3 += (x - m) * (x - m) * (x - m);
    }
    m2 /= n;
    m3 /= n;
    e.ppnMean = m;
    e.ppnSkew = m2 > 0 ? m3 / std::pow(m2, 1.5) : 0;
    e.ppnMedian = pct(ppn, 0.5);
    e.ppnP90 = pct(ppn, 0.9);
    e.ppnP99 = pct(ppn, 0.99);
    e.ppnMax = static_cast<long long>(*std::max_element(ppn.begin(), ppn.end()));
    e.ppnShare2 = static_cast<double>(std::count(ppn.begin(), ppn.end(), 2.0)) / n;
    e.ppnShare10 = static_cast<double>(std::count_if(ppn.begin(), ppn.end(), [](double x) { return x >= 10; })) / n;
  }
  e.bigShare = e.nets ? static_cast<double>(e.bigNets) / static_cast<double>(e.nets) : 0;
  e.rsmtPerNet = kept.empty() ? 0 : keptRsmt / static_cast<double>(kept.size());
  e.rsmtPerGcell = keptRsmt / static_cast<double>(nT);
  e.rsmtPerTrack = (hTracks + vTracks) > 0 ? keptRsmt / (hTracks + vTracks) : 0;
  e.rsmtPerNetAll = nAll ? static_cast<double>(rsmtAll) / static_cast<double>(nAll) : 0;
  std::vector<double> pv;
  for (long long c : perTile)
    if (c > 0) {
      pv.push_back(static_cast<double>(c));
      e.pvMax = std::max(e.pvMax, c);
    }
  e.pvMean = pv.empty() ? 0 : std::accumulate(pv.begin(), pv.end(), 0.0) / static_cast<double>(pv.size());
  e.pvP99 = pct(pv, 0.99);
  // Regions: the union of the kept nets' boxes (a 2D difference array of
  // cover counts), GCells joined edge to edge.
  {
    const size_t W = static_cast<size_t>(g.nx) + 1;
    std::vector<int> cover(W * (static_cast<size_t>(g.ny) + 1), 0);
    auto at = [&](int x, int y) -> int & { return cover[static_cast<size_t>(y) * W + static_cast<size_t>(x)]; };
    for (const Kept &k : kept) {
      at(k.x0, k.y0) += 1;
      at(k.x1 + 1, k.y0) -= 1;
      at(k.x0, k.y1 + 1) -= 1;
      at(k.x1 + 1, k.y1 + 1) += 1;
    }
    for (int y = 0; y <= g.ny; ++y)
      for (int x = 0; x <= g.nx; ++x) {
        int v = at(x, y);
        if (x > 0)
          v += at(x - 1, y);
        if (y > 0)
          v += at(x, y - 1);
        if (x > 0 && y > 0)
          v -= at(x - 1, y - 1);
        at(x, y) = v;
      }
    std::vector<int> label(nT, -1);
    std::vector<long long> regionPins;
    std::vector<size_t> stack;
    for (size_t t = 0; t < nT; ++t) {
      int tx = static_cast<int>(t % static_cast<size_t>(g.nx)), ty = static_cast<int>(t / static_cast<size_t>(g.nx));
      if (label[t] >= 0 || at(tx, ty) <= 0)
        continue;
      int id = static_cast<int>(regionPins.size());
      regionPins.push_back(0);
      label[t] = id;
      stack.assign(1, t);
      while (!stack.empty()) {
        size_t c = stack.back();
        stack.pop_back();
        int cx = static_cast<int>(c % static_cast<size_t>(g.nx)), cy = static_cast<int>(c / static_cast<size_t>(g.nx));
        const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int q = 0; q < 4; ++q) {
          int x2 = cx + dx[q], y2 = cy + dy[q];
          if (x2 < 0 || y2 < 0 || x2 >= g.nx || y2 >= g.ny)
            continue;
          size_t nt = static_cast<size_t>(y2) * static_cast<size_t>(g.nx) + static_cast<size_t>(x2);
          if (label[nt] < 0 && at(x2, y2) > 0) {
            label[nt] = id;
            stack.push_back(nt);
          }
        }
      }
    }
    long long keptPins = 0;
    for (const Kept &k : kept) {
      keptPins += k.pins;
      size_t t0 = static_cast<size_t>(k.tiles[0].second) * static_cast<size_t>(g.nx) + static_cast<size_t>(k.tiles[0].first);
      regionPins[static_cast<size_t>(label[t0])] += k.pins;
    }
    e.regions = static_cast<long long>(regionPins.size());
    e.regionTop = keptPins && !regionPins.empty()
                      ? static_cast<double>(*std::max_element(regionPins.begin(), regionPins.end())) / static_cast<double>(keptPins)
                      : 0;
  }
  // k x k equal regions
  const int K[3] = {4, 8, 16};
  for (int i = 0; i < 3; ++i) {
    const long long k = K[i];
    auto reg = [&](int x, int y) {
      return static_cast<size_t>((static_cast<long long>(y) * k / g.ny) * k + static_cast<long long>(x) * k / g.nx);
    };
    std::vector<double> pinsR(static_cast<size_t>(k * k), 0), tilesR(pinsR.size(), 0);
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x) {
        tilesR[reg(x, y)] += 1;
        pinsR[reg(x, y)] += static_cast<double>(perTile[static_cast<size_t>(y) * static_cast<size_t>(g.nx) + static_cast<size_t>(x)]);
      }
    std::vector<double> dens;
    for (size_t r = 0; r < pinsR.size(); ++r)
      if (tilesR[r] > 0)
        dens.push_back(pinsR[r] / tilesR[r]);
    double mean = dens.empty() ? 0 : std::accumulate(dens.begin(), dens.end(), 0.0) / static_cast<double>(dens.size());
    e.regCv[i] = cv(dens);
    e.regMaxMean[i] = mean > 0 ? *std::max_element(dens.begin(), dens.end()) / mean : 0;
    e.regGini[i] = gini(dens);
    long long span = 0;
    std::vector<size_t> seen;
    for (const Kept &kp : kept) {
      seen.clear();
      for (const auto &t : kp.tiles) {
        size_t r = reg(t.first, t.second);
        if (std::find(seen.begin(), seen.end(), r) == seen.end()) {
          seen.push_back(r);
          if (seen.size() > 2)
            break;
        }
      }
      span += seen.size() > 2;
    }
    e.regSpan2[i] = kept.empty() ? 0 : static_cast<double>(span) / static_cast<double>(kept.size());
  }
  return e;
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
  // Part 130. Pins: a 2, b 2, c 2 (c's two in one GCell). Nets in >= 2
  // GCells: a and b, both with boxes over 25% of the 12 GCells (a 4, b 12).
  rt::Extra x = rt::analyzeExtra(d);
  check(x.pins == 6 && std::abs(x.pinDensity - 0.5) < 1e-12 && x.nets == 2 && x.bigNets == 2 && x.regions == 0,
        "predictors: 6 pins over 12 GCells; both multi-GCell nets cover > 25% of the die and are left out");
  check(std::abs(x.ppnMean - 2.0) < 1e-12 && x.ppnShare2 == 1.0 && x.ppnSkew == 0.0, "predictors: every net has 2 pins");
  check(std::abs(x.rsmtPerNetAll - 4.0) < 1e-12, "predictors: Steiner length a 3, b 5 -> 4 per net");
  // A plus shape: the spanning tree is 6, the Steiner tree (through the
  // centre) 4. Three points: half the box perimeter.
  check(rt::rsmtLength({{0, 1}, {2, 1}, {1, 0}, {1, 2}}) == 4, "Steiner tree: plus shape 4 (spanning tree 6)");
  check(rt::rsmtLength({{0, 0}, {4, 1}, {2, 5}}) == 9 && rt::rsmtLength({{0, 0}, {3, 4}}) == 7,
        "Steiner tree: 3 points = half perimeter 9, 2 points = distance 7");
  check(x.layers == 2 && x.layersH == 1 && x.layersV == 1 && std::abs(x.tracksH - 2.0 * 9 / 12) < 1e-12,
        "layers: one horizontal, one vertical; horizontal tracks per GCell 9 edges x 2 / 12 GCells");
  check(std::abs(rt::gini({1, 1, 1, 1})) < 1e-12 && std::abs(rt::gini({0, 0, 0, 4}) - 0.75) < 1e-12,
        "Gini: equal values 0, all in one of four 0.75");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> | <design.cap> <design.net>\n";
    return 2;
  }
  Design d;
  if (argc == 2) {
    d = gr_io::readGr(argv[1]);
  } else { // ISPD 2024 / 2025: each pin at its first access point
    g24::Resources r;
    std::string err;
    g24::FastText cf;
    if (!cf.open(argv[1]) || !g24::readResourcesFast(cf, r, err)) {
      std::cerr << "cannot read " << argv[1] << ": " << err << "\n";
      return 1;
    }
    g24::FastText nf;
    if (!nf.open(argv[2])) {
      std::cerr << "cannot open " << argv[2] << "\n";
      return 1;
    }
    g24::FastNetStream ns(nf);
    std::vector<g24::Net> nets;
    g24::Net n;
    while (ns.next(n, err)) {
      g24::chooseAccessPointsNet(n);
      n.timing.clear();
      nets.push_back(std::move(n));
      n = g24::Net();
    }
    if (!err.empty()) {
      std::cerr << "cannot read " << argv[2] << ": " << err << "\n";
      return 1;
    }
    d = g24::toDesign(r, nets);
  }
  rt::Features f = rt::analyze(d);
  rt::Extra x = rt::analyzeExtra(d);
  std::printf("nets=%lld tiles=%lld util=%.4f rudy_p99=%.3f rudy_p999=%.3f rudy_over=%.5f rudy_excess=%.0f "
              "esc_max=%.3f esc_p999=%.3f esc_over1=%lld esc_over09=%lld cut3_max=%.3f cut3_over1=%lld "
              "long_share=%.4f\n",
              f.nets, f.tiles, f.util, f.rudyP99, f.rudyP999, f.rudyOver, f.rudyExcess, f.escMax, f.escP999,
              f.escOver1, f.escOver09, f.cut3Max, f.cut3Over1, f.longShare);
  std::printf("pins=%lld pin_density=%.5f pin_per_htrack=%.5f pin_per_vtrack=%.5f row_cv=%.4f col_cv=%.4f "
              "ppn_mean=%.4f ppn_median=%.1f ppn_p90=%.1f ppn_p99=%.1f ppn_max=%lld ppn_skew=%.3f ppn_share2=%.4f "
              "ppn_share10=%.4f big_nets=%lld big_share=%.6f regions=%lld region_top=%.4f",
              x.pins, x.pinDensity, x.pinPerHTrack, x.pinPerVTrack, x.rowCv, x.colCv, x.ppnMean, x.ppnMedian,
              x.ppnP90, x.ppnP99, x.ppnMax, x.ppnSkew, x.ppnShare2, x.ppnShare10, x.bigNets, x.bigShare, x.regions,
              x.regionTop);
  const int K[3] = {4, 8, 16};
  for (int i = 0; i < 3; ++i)
    std::printf(" reg%d_cv=%.4f reg%d_maxmean=%.3f reg%d_gini=%.4f reg%d_span2=%.4f", K[i], x.regCv[i], K[i],
                x.regMaxMean[i], K[i], x.regGini[i], K[i], x.regSpan2[i]);
  std::printf(" rsmt_per_net=%.3f rsmt_per_gcell=%.4f rsmt_per_track=%.5f rsmt_per_net_all=%.3f pv_mean=%.3f "
              "pv_p99=%.1f pv_max=%lld nets_all=%lld rsmt_total=%.0f layers=%d layers_h=%d layers_v=%d tracks_h=%.3f "
              "tracks_v=%.3f\n",
              x.rsmtPerNet, x.rsmtPerGcell, x.rsmtPerTrack, x.rsmtPerNetAll, x.pvMean, x.pvP99, x.pvMax, x.nets,
              x.rsmtPerNetAll * static_cast<double>(x.nets), x.layers, x.layersH, x.layersV, x.tracksH, x.tracksV);
  return 0;
}
