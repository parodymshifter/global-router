// grmap.cpp -- congestion map of an ISPD 2008 route file (CLAUDE.md build
// order, stage 1: the congestion visualizer). An analysis tool, not a
// router; nothing here changes a route.
//
//   grmap <benchmark.gr> <solution.route> <out.ppm> [pixels_per_tile]
//   grmap <benchmark.gr> <solution.route> <out.grid>   (for the 3D viewer, gpu/grview.mm)
//   gunzip -c x.route.gz | grmap <benchmark.gr> /dev/stdin out.ppm
//   grmap --selftest
//
// Usage is charged exactly as gr_verify / eval2008.pl do (minWidth +
// minSpacing per net per edge occurrence, per layer), then collapsed to 2D
// per tile edge (usage and capacity summed over layers -- what the router's
// 2D stages negotiate against; real overflow has equaled 2D overflow on
// every benchmark measured, Part 78). Each edge gets a class by its use /
// capacity:
//   violet  <= 50%      blue   50-75%     green  75-90%
//   yellow  90-<100%    orange exactly 100% (full)    red  over 100%
// and an edge with no capacity is "blocked" (red if anything uses it).
// A tile is drawn in the worst class among the 2D edges touching it, so a
// single overflowed edge shows; a tile whose edges are all blocked is dark
// grey. North is up (tile row 0 at the bottom). Also prints the number of
// 2D edges and tiles in each class.
//
// Output is binary PPM (P6), the simplest lossless format; convert with
// any image tool (e.g. a few lines of Python's zlib for PNG). An output
// path ending in ".grid" writes the per-tile data for the 3D viewer
// instead: "GRMAP1\0\0", int32 nx, int32 ny, then nx*ny float32 worst-edge
// use/capacity (0 for blocked tiles), then nx*ny uint8 classes (0 violet ..
// 5 red, 6 blocked), row 0 = the bottom row, little-endian.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "gr_io.h"

using gr_io::Design;
using gr_io::fail;
using gr_io::RoutingGrid;

namespace gmap {

enum Cls { kViolet = 0, kBlue, kGreen, kYellow, kOrange, kRed, kBlocked, kClasses };
const char *kNames[kClasses] = {"<=50% violet", "50-75% blue", "75-90% green", "90-99% yellow",
                                "100% orange", "overflow red", "blocked grey"};
const unsigned char kRgb[kClasses][3] = {{128, 64, 200}, {40, 100, 230}, {40, 170, 70}, {240, 210, 40},
                                         {245, 130, 30}, {215, 30, 30}, {60, 60, 60}};
// Order for "worst": blocked is the least informative, then violet up to red.
int rank(Cls c) { return c == kBlocked ? -1 : static_cast<int>(c); }

Cls classify(long long use, long long cap) {
  if (cap <= 0)
    return use > 0 ? kRed : kBlocked;
  if (use > cap)
    return kRed;
  if (use == cap)
    return kOrange;
  // Integer comparisons: use/cap <= p/100  <=>  100*use <= p*cap.
  if (100 * use <= 50 * cap)
    return kViolet;
  if (100 * use <= 75 * cap)
    return kBlue;
  if (100 * use <= 90 * cap)
    return kGreen;
  return kYellow;
}

struct Segment {
  int x1, y1, l1, x2, y2, l2;
};

struct Map {
  int nx = 0, ny = 0;
  std::vector<Cls> tile;           // ny * nx, row 0 = bottom
  std::vector<float> ratio;        // ny * nx: worst use / capacity over the tile's edges (0 if all blocked)
  long long edges[kClasses] = {0}, tiles[kClasses] = {0};
  long long overflow2D = 0; // sum over 2D edges of max(0, use - cap): a cross-check against the scorer
};

// Reads a solution and charges per-layer usage (gr_verify's rule).
void readAndCharge(std::istream &in, const Design &d, std::vector<long long> &hu, std::vector<long long> &vu) {
  const RoutingGrid &g = d.grid;
  std::unordered_map<std::string, int> width;
  for (const gr_io::Net &n : d.nets)
    width[n.name] = n.minWidth;
  hu.assign(g.hCount(), 0);
  vu.assign(g.vCount(), 0);
  std::string line, cur;
  bool open = false;
  while (std::getline(in, line)) {
    size_t b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
      continue;
    std::string s = line.substr(b, line.find_last_not_of(" \t\r\n") - b + 1);
    if (s == "!") {
      open = false;
      continue;
    }
    if (s[0] == '(') {
      long long x1, y1, l1, x2, y2, l2;
      if (!open || std::sscanf(s.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        fail("malformed route line: " + s);
      Segment sg{g.colOf(x1), g.rowOf(y1), static_cast<int>(l1) - 1, g.colOf(x2), g.rowOf(y2),
                 static_cast<int>(l2) - 1};
      if (sg.l1 != sg.l2)
        continue;
      auto it = width.find(cur);
      int w = it == width.end() ? 1 : it->second;
      int amt = w + g.minSpacing[sg.l1];
      if (sg.y1 == sg.y2 && sg.x1 != sg.x2)
        for (int x = std::min(sg.x1, sg.x2); x < std::max(sg.x1, sg.x2); ++x)
          hu[g.hIndex(x, sg.y1, sg.l1)] += amt;
      else if (sg.x1 == sg.x2 && sg.y1 != sg.y2)
        for (int y = std::min(sg.y1, sg.y2); y < std::max(sg.y1, sg.y2); ++y)
          vu[g.vIndex(sg.x1, y, sg.l1)] += amt;
      continue;
    }
    std::istringstream hs(s);
    hs >> cur;
    open = true;
  }
}

Map build(const Design &d, const std::vector<long long> &hu, const std::vector<long long> &vu) {
  const RoutingGrid &g = d.grid;
  Map m;
  m.nx = g.nx;
  m.ny = g.ny;
  m.tile.assign(static_cast<size_t>(g.nx) * g.ny, kBlocked);
  m.ratio.assign(static_cast<size_t>(g.nx) * g.ny, 0.0f);
  auto ratioMax = [&](int x, int y, long long u, long long c) {
    float r = c > 0 ? static_cast<float>(static_cast<double>(u) / static_cast<double>(c)) : (u > 0 ? 2.0f : 0.0f);
    float &t = m.ratio[static_cast<size_t>(y) * g.nx + x];
    t = std::max(t, r);
  };
  auto worst = [&](int x, int y, Cls c) {
    Cls &t = m.tile[static_cast<size_t>(y) * g.nx + x];
    if (rank(c) > rank(t))
      t = c;
  };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      long long u = 0, c = 0;
      for (int l = 0; l < g.nLayers; ++l) {
        u += hu[g.hIndex(x, y, l)];
        c += g.hcap[g.hIndex(x, y, l)];
      }
      Cls k = classify(u, c);
      ++m.edges[k];
      m.overflow2D += std::max(0LL, u - c);
      worst(x, y, k);
      worst(x + 1, y, k);
      ratioMax(x, y, u, c);
      ratioMax(x + 1, y, u, c);
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      long long u = 0, c = 0;
      for (int l = 0; l < g.nLayers; ++l) {
        u += vu[g.vIndex(x, y, l)];
        c += g.vcap[g.vIndex(x, y, l)];
      }
      Cls k = classify(u, c);
      ++m.edges[k];
      m.overflow2D += std::max(0LL, u - c);
      worst(x, y, k);
      worst(x, y + 1, k);
      ratioMax(x, y, u, c);
      ratioMax(x, y + 1, u, c);
    }
  for (Cls t : m.tile)
    ++m.tiles[t];
  return m;
}

void writePpm(const Map &m, const std::string &path, int scale) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    fail("cannot write " + path);
  int W = m.nx * scale, H = m.ny * scale;
  f << "P6\n" << W << " " << H << "\n255\n";
  std::vector<unsigned char> row(static_cast<size_t>(W) * 3);
  for (int py = 0; py < H; ++py) {
    int ty = m.ny - 1 - py / scale; // north up
    for (int px = 0; px < W; ++px) {
      const unsigned char *c = kRgb[m.tile[static_cast<size_t>(ty) * m.nx + px / scale]];
      row[3 * px] = c[0];
      row[3 * px + 1] = c[1];
      row[3 * px + 2] = c[2];
    }
    f.write(reinterpret_cast<const char *>(row.data()), static_cast<std::streamsize>(row.size()));
  }
}

void writeGrid(const Map &m, const std::string &path) {
  std::ofstream f(path, std::ios::binary);
  if (!f)
    fail("cannot write " + path);
  const char magic[8] = {'G', 'R', 'M', 'A', 'P', '1', 0, 0};
  f.write(magic, 8);
  int32_t dims[2] = {m.nx, m.ny};
  f.write(reinterpret_cast<const char *>(dims), sizeof dims);
  f.write(reinterpret_cast<const char *>(m.ratio.data()), static_cast<std::streamsize>(m.ratio.size() * sizeof(float)));
  std::vector<uint8_t> cls(m.tile.begin(), m.tile.end());
  f.write(reinterpret_cast<const char *>(cls.data()), static_cast<std::streamsize>(cls.size()));
}

void printCounts(const Map &m) {
  long long te = 0, tt = 0;
  for (int k = 0; k < kClasses; ++k) {
    te += m.edges[k];
    tt += m.tiles[k];
  }
  std::printf("grid %d x %d, 2D overflow %lld\n", m.nx, m.ny, m.overflow2D);
  for (int k = 0; k < kClasses; ++k)
    std::printf("  %-14s  2D edges %9lld (%5.1f%%)   tiles %8lld (%5.1f%%)\n", kNames[k], m.edges[k],
                100.0 * m.edges[k] / std::max(1LL, te), m.tiles[k], 100.0 * m.tiles[k] / std::max(1LL, tt));
}

} // namespace gmap

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "grmap self test\n";
  using namespace gmap;
  // Class boundaries, exactly as stated: 50% is violet, just over is blue,
  // 75% blue, 90% green, 95% yellow, exactly full orange, over red; no
  // capacity: blocked, or red if used.
  check(classify(0, 100) == kViolet && classify(50, 100) == kViolet && classify(51, 100) == kBlue,
        "0% and 50% violet, 51% blue");
  check(classify(75, 100) == kBlue && classify(76, 100) == kGreen && classify(90, 100) == kGreen &&
            classify(91, 100) == kYellow && classify(99, 100) == kYellow,
        "75% blue, 76% and 90% green, 91% and 99% yellow");
  check(classify(100, 100) == kOrange && classify(101, 100) == kRed && classify(2, 0) == kRed &&
            classify(0, 0) == kBlocked,
        "exactly full orange, over red, used with no capacity red, unused with no capacity blocked");
  check(classify(1, 3) == kViolet && classify(2, 3) == kBlue && classify(8, 9) == kGreen,
        "fractions without rounding: 1/3 violet, 2/3 blue, 8/9 (88.9%) green");
  // A 4 x 2 grid, horizontal capacity 4 on layer 1 (two nets of width 1 +
  // spacing 1), vertical 4 on layer 2. Net a and b both run along row 0
  // from x 0 to 2: edges (0,0)-(1,0) and (1,0)-(2,0) carry 4 = full
  // (orange); net c adds a third run on (0,0)-(1,0): 6 > 4 (red). Tiles 0
  // and 1 of row 0 touch the red edge; tile 2 touches only the full edge
  // and an unused one (orange); tile 3 of row 0 and all of row 1 touch only
  // unused edges (violet).
  const char *gr = "grid 4 2 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\na 0 2 1\n5 5 1\n25 5 1\nb 1 2 1\n5 5 1\n25 5 1\nc 2 2 1\n5 5 1\n15 5 1\n0\n";
  const char *tmp = "grmap_selftest.gr";
  {
    std::ofstream f(tmp);
    f << gr;
  }
  Design d = gr_io::readGr(tmp);
  std::remove(tmp);
  std::istringstream sol("a 0 1\n(5,5,1)-(25,5,1)\n!\nb 1 1\n(5,5,1)-(25,5,1)\n!\nc 2 1\n(5,5,1)-(15,5,1)\n!\n");
  std::vector<long long> hu, vu;
  readAndCharge(sol, d, hu, vu);
  Map m = build(d, hu, vu);
  auto at = [&](int x, int y) { return m.tile[static_cast<size_t>(y) * m.nx + x]; };
  check(at(0, 0) == kRed && at(1, 0) == kRed, "tiles on the overflowed edge are red");
  check(at(2, 0) == kOrange, "a tile whose worst edge is exactly full is orange");
  check(at(3, 0) == kViolet && at(0, 1) == kViolet && at(3, 1) == kViolet, "untouched tiles are violet");
  check(m.edges[kRed] == 1 && m.edges[kOrange] == 1 && m.tiles[kRed] == 2 && m.overflow2D == 2,
        "counts: 1 red edge, 1 full edge, 2 red tiles, 2D overflow 2");
  auto ratioAt = [&](int x, int y) { return m.ratio[static_cast<size_t>(y) * m.nx + x]; };
  check(ratioAt(0, 0) == 1.5f && ratioAt(2, 0) == 1.0f && ratioAt(3, 0) == 0.0f,
        "bar heights: worst edge ratio 6/4 = 1.5 on the overflowed tiles, 1.0 on the full one, 0 untouched");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc < 4 || argc > 5) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <solution.route> <out.ppm> [pixels_per_tile]\n";
    return 2;
  }
  int scale = argc == 5 ? std::max(1, std::atoi(argv[4])) : 1;
  Design d = gr_io::readGr(argv[1]);
  std::ifstream in(argv[2]);
  if (!in)
    fail(std::string("cannot open ") + argv[2]);
  std::vector<long long> hu, vu;
  gmap::readAndCharge(in, d, hu, vu);
  gmap::Map m = gmap::build(d, hu, vu);
  std::string out(argv[3]);
  if (out.size() > 5 && out.substr(out.size() - 5) == ".grid")
    gmap::writeGrid(m, out);
  else
    gmap::writePpm(m, out, scale);
  gmap::printCounts(m);
  return 0;
}
