// hotring.cpp -- how far out from a hotspot the chip has room again
// (HISTORY_COST_SWEEP.md Part 94). An analysis tool, not a router; it reads
// the per-tile .grid file grmap writes (each tile's worst 2D edge use /
// capacity and its class).
//
//   hotring <file.grid> [hot=0.95] [cool=0.75] [merge=2] [minTiles=20] [top=3] [window=7]
//   hotring --selftest
//
// Hot centers: a tile is in a center if the mean worst-edge ratio over the
// window x window block around it (ratios capped at 1.5, so one extreme
// tile cannot carry a block) is >= hot -- a dense hot area, not scattered
// full tiles, which are 10-25% of all tiles even on zero-overflow designs
// -- or if it is overflowed itself. Center tiles within `merge` tiles (x and
// y) join one cluster. Reported: every cluster with an overflowed tile,
// plus the `top` largest other clusters of at least `minTiles` tiles.
//
// For each cluster, a rectangle starts at the cluster's tile bounding box
// and each side moves outward one tile at a time until the tiles along that
// side are cool -- worst-edge ratio <= cool (75%: violet or blue, "plenty
// of capacity") -- for at least a share s of them; sides that are not yet
// cool keep moving (the other sides' lines grow as they do), and a side
// that reaches the die edge stops there (flagged). Two shares: s = 0.9
// ("most of the way around") and s = 1.0 ("all the way around").
// Reported per rectangle: width x height in tiles, as a share of the die's
// width and height, and the band thickness on each side (rectangle minus
// the hot cluster's own box).

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace hr {

struct Grid {
  int nx = 0, ny = 0;
  std::vector<float> ratio;
  std::vector<uint8_t> cls; // 5 = overflowed
  float at(int x, int y) const { return ratio[static_cast<size_t>(y) * nx + x]; }
};

bool readGrid(const std::string &path, Grid &g, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  char magic[8];
  int32_t d[2];
  if (!f || !f.read(magic, 8) || std::memcmp(magic, "GRMAP1\0\0", 8) != 0 ||
      !f.read(reinterpret_cast<char *>(d), sizeof d) || d[0] <= 0 || d[1] <= 0) {
    err = "not a grmap .grid file: " + path;
    return false;
  }
  g.nx = d[0];
  g.ny = d[1];
  size_t n = static_cast<size_t>(g.nx) * g.ny;
  g.ratio.resize(n);
  g.cls.resize(n);
  if (!f.read(reinterpret_cast<char *>(g.ratio.data()), static_cast<std::streamsize>(n * sizeof(float))) ||
      !f.read(reinterpret_cast<char *>(g.cls.data()), static_cast<std::streamsize>(n))) {
    err = "truncated: " + path;
    return false;
  }
  return true;
}

struct Box {
  int x0, y0, x1, y1;
  int w() const { return x1 - x0 + 1; }
  int h() const { return y1 - y0 + 1; }
};

struct Cluster {
  Box box{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
  long long hot = 0, red = 0;
};

// Which tiles are in a hot center (see the header).
std::vector<char> centerMask(const Grid &g, float hot, int window) {
  // Summed-area table of min(ratio, 1.5).
  std::vector<double> sat(static_cast<size_t>(g.nx + 1) * (g.ny + 1), 0.0);
  auto S = [&](int x, int y) -> double & { return sat[static_cast<size_t>(y) * (g.nx + 1) + x]; };
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x)
      S(x + 1, y + 1) = std::min(1.5, static_cast<double>(g.at(x, y))) + S(x, y + 1) + S(x + 1, y) - S(x, y);
  std::vector<char> m(g.ratio.size(), 0);
  int r = window / 2;
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      int x0 = std::max(0, x - r), x1 = std::min(g.nx - 1, x + r), y0 = std::max(0, y - r), y1 = std::min(g.ny - 1, y + r);
      double sum = S(x1 + 1, y1 + 1) - S(x0, y1 + 1) - S(x1 + 1, y0) + S(x0, y0);
      double mean = sum / ((x1 - x0 + 1) * (y1 - y0 + 1));
      size_t i = static_cast<size_t>(y) * g.nx + x;
      m[i] = mean >= hot || g.cls[i] == 5;
    }
  return m;
}

std::vector<Cluster> clusters(const Grid &g, const std::vector<char> &mask, int merge) {
  std::vector<int> comp(g.ratio.size(), -1);
  std::vector<Cluster> out;
  for (size_t s = 0; s < g.ratio.size(); ++s) {
    if (!mask[s] || comp[s] >= 0)
      continue;
    Cluster c;
    int id = static_cast<int>(out.size());
    std::vector<size_t> st{s};
    comp[s] = id;
    while (!st.empty()) {
      size_t t = st.back();
      st.pop_back();
      int x = static_cast<int>(t % g.nx), y = static_cast<int>(t / g.nx);
      ++c.hot;
      c.red += g.cls[t] == 5;
      c.box.x0 = std::min(c.box.x0, x);
      c.box.y0 = std::min(c.box.y0, y);
      c.box.x1 = std::max(c.box.x1, x);
      c.box.y1 = std::max(c.box.y1, y);
      for (int dy = -merge; dy <= merge; ++dy)
        for (int dx = -merge; dx <= merge; ++dx) {
          int u = x + dx, v = y + dy;
          if (u < 0 || v < 0 || u >= g.nx || v >= g.ny)
            continue;
          size_t n = static_cast<size_t>(v) * g.nx + u;
          if (mask[n] && comp[n] < 0) {
            comp[n] = id;
            st.push_back(n);
          }
        }
    }
    out.push_back(c);
  }
  return out;
}

struct Ring {
  Box rect;
  bool edgeHit[4] = {false, false, false, false}; // left, right, bottom, top reached the die edge before cooling
};

// Share of cool tiles along one side line of the rectangle.
double coolShare(const Grid &g, const Box &r, int side, float cool) {
  long long n = 0, ok = 0;
  if (side == 0 || side == 1) {
    int x = side == 0 ? r.x0 : r.x1;
    for (int y = r.y0; y <= r.y1; ++y, ++n)
      ok += g.at(x, y) <= cool;
  } else {
    int y = side == 2 ? r.y0 : r.y1;
    for (int x = r.x0; x <= r.x1; ++x, ++n)
      ok += g.at(x, y) <= cool;
  }
  return n ? static_cast<double>(ok) / static_cast<double>(n) : 1.0;
}

Ring grow(const Grid &g, const Box &core, float cool, double share) {
  Ring rg;
  rg.rect = core;
  bool done[4] = {false, false, false, false};
  for (;;) {
    bool moved = false;
    for (int s = 0; s < 4; ++s) {
      if (done[s])
        continue;
      // The side sits on the rectangle's outer line; it is satisfied once
      // that line is cool enough. Start from one tile outside the core.
      if (coolShare(g, rg.rect, s, cool) >= share && !(rg.rect.x0 == core.x0 && s == 0) &&
          !(rg.rect.x1 == core.x1 && s == 1) && !(rg.rect.y0 == core.y0 && s == 2) &&
          !(rg.rect.y1 == core.y1 && s == 3)) {
        done[s] = true;
        continue;
      }
      int &edge = s == 0 ? rg.rect.x0 : s == 1 ? rg.rect.x1 : s == 2 ? rg.rect.y0 : rg.rect.y1;
      int limit = s == 0 ? 0 : s == 1 ? g.nx - 1 : s == 2 ? 0 : g.ny - 1;
      if (edge == limit) {
        done[s] = true;
        rg.edgeHit[s] = coolShare(g, rg.rect, s, cool) < share;
        continue;
      }
      edge += (s == 0 || s == 2) ? -1 : 1;
      moved = true;
    }
    if (!moved)
      break;
  }
  return rg;
}

} // namespace hr

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "hotring self test\n";
  using namespace hr;
  // A 40 x 30 die: a 4 x 4 hot core at x 18-21, y 13-16 (one red tile),
  // a warm band (ratio 0.85) two tiles thick around it, then cool (0.3)
  // everywhere -- except one warm tile on the right side's first cool line.
  Grid g;
  g.nx = 40;
  g.ny = 30;
  g.ratio.assign(1200, 0.3f);
  g.cls.assign(1200, 0);
  auto set = [&](int x, int y, float r, int c) {
    g.ratio[static_cast<size_t>(y) * 40 + x] = r;
    g.cls[static_cast<size_t>(y) * 40 + x] = static_cast<uint8_t>(c);
  };
  for (int y = 11; y <= 18; ++y)
    for (int x = 16; x <= 23; ++x)
      set(x, y, 0.85f, 2);
  for (int y = 13; y <= 16; ++y)
    for (int x = 18; x <= 21; ++x)
      set(x, y, 1.0f, 4);
  set(19, 14, 1.05f, 5);
  set(24, 15, 0.8f, 2); // one warm tile on the right's first outer line
  std::vector<Cluster> cl = clusters(g, centerMask(g, 0.95f, 1), 5);
  check(cl.size() == 1 && cl[0].hot == 16 && cl[0].red == 1 && cl[0].box.x0 == 18 && cl[0].box.x1 == 21 &&
            cl[0].box.y0 == 13 && cl[0].box.y1 == 16,
        "one cluster: 16 hot tiles, 1 red, box x 18-21 y 13-16");
  // "most" (90%): left goes 18 -> 15 (x 17 and 16 are warm), bottom 13 ->
  // 10, top 16 -> 19; right: x 24 has 1 warm tile of 10 on its line (90%
  // cool) -> stops at 24. Rectangle x 15-24, y 10-19: 10 x 10.
  Ring most = grow(g, cl[0].box, 0.75f, 0.9);
  check(most.rect.x0 == 15 && most.rect.x1 == 24 && most.rect.y0 == 10 && most.rect.y1 == 19,
        "most of the way around: x 15-24 y 10-19 (got x " + std::to_string(most.rect.x0) + "-" +
            std::to_string(most.rect.x1) + " y " + std::to_string(most.rect.y0) + "-" + std::to_string(most.rect.y1) +
            ")");
  // "all" (100%): the right side must pass the warm tile at x 24 -> x 25.
  Ring all = grow(g, cl[0].box, 0.75f, 1.0);
  check(all.rect.x1 == 25 && all.rect.x0 == 15, "all the way around: the right side moves past the warm tile, x 15-25");
  // A hotspot against the die edge: the left side stops at x 0, flagged.
  Grid e = g;
  for (int y = 11; y <= 18; ++y)
    for (int x = 0; x <= 3; ++x)
      e.ratio[static_cast<size_t>(y) * 40 + x] = 1.0f;
  std::vector<Cluster> ce = clusters(e, centerMask(e, 0.95f, 1), 2);
  const Cluster *edgeC = nullptr;
  for (const Cluster &c : ce)
    if (c.box.x0 == 0)
      edgeC = &c;
  Ring er = edgeC ? grow(e, edgeC->box, 0.75f, 0.9) : Ring{};
  check(edgeC && er.rect.x0 == 0 && er.edgeHit[0] && !er.edgeHit[1],
        "a hotspot touching the die edge: the left side stops at the edge, flagged");
  // Centers by block mean: scattered full tiles (every 3rd tile in a field
  // at 0.6) are not a center at window 7; a solid 9 x 9 full block is, and
  // so is a lone overflowed tile.
  Grid s;
  s.nx = s.ny = 40;
  s.ratio.assign(1600, 0.6f);
  s.cls.assign(1600, 1);
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 40; x += 3)
      s.ratio[static_cast<size_t>(y) * 40 + x] = 1.0f;
  for (int y = 25; y <= 33; ++y)
    for (int x = 25; x <= 33; ++x)
      s.ratio[static_cast<size_t>(y) * 40 + x] = 1.0f;
  s.ratio[static_cast<size_t>(35) * 40 + 5] = 1.04f;
  s.cls[static_cast<size_t>(35) * 40 + 5] = 5;
  std::vector<char> mask = centerMask(s, 0.95f, 7);
  long long scattered = 0;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 40; ++x)
      scattered += mask[static_cast<size_t>(y) * 40 + x];
  check(scattered == 0 && mask[static_cast<size_t>(29) * 40 + 29] && mask[static_cast<size_t>(35) * 40 + 5],
        "window 7: scattered full tiles are not a center; the solid block's middle and the overflowed tile are");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " <file.grid> [hot] [cool] [merge] [minTiles] [top]\n";
    return 2;
  }
  float hot = argc > 2 ? std::atof(argv[2]) : 0.95f, cool = argc > 3 ? std::atof(argv[3]) : 0.75f;
  int merge = argc > 4 ? std::atoi(argv[4]) : 2, minTiles = argc > 5 ? std::atoi(argv[5]) : 20,
      top = argc > 6 ? std::atoi(argv[6]) : 3, window = argc > 7 ? std::atoi(argv[7]) : 7;
  hr::Grid g;
  std::string err;
  if (!hr::readGrid(argv[1], g, err)) {
    std::cerr << err << "\n";
    return 1;
  }
  std::vector<hr::Cluster> cl = hr::clusters(g, hr::centerMask(g, hot, window), merge);
  std::vector<size_t> pick;
  std::vector<size_t> bySize(cl.size());
  for (size_t i = 0; i < cl.size(); ++i)
    bySize[i] = i;
  std::sort(bySize.begin(), bySize.end(), [&](size_t a, size_t b) { return cl[a].hot > cl[b].hot; });
  for (size_t i : bySize)
    if (cl[i].red > 0)
      pick.push_back(i);
  int added = 0;
  for (size_t i : bySize)
    if (added < top && cl[i].red == 0 && cl[i].hot >= minTiles) {
      pick.push_back(i);
      ++added;
    }
  long long hotTotal = 0;
  for (const hr::Cluster &c : cl)
    hotTotal += c.hot;
  std::printf("die %d x %d tiles; %lld center tiles (%dx%d block mean >= %.0f%%, or overflowed) in %zu clusters "
              "(merge %d); cool = <= %.0f%%\n",
              g.nx, g.ny, hotTotal, window, window, hot * 100, cl.size(), merge, cool * 100);
  for (size_t i : pick) {
    const hr::Cluster &c = cl[i];
    std::printf("center: %lld tiles, %lld overflowed, box x %d-%d y %d-%d (%d x %d = %.1f%% x %.1f%%)\n", c.hot,
                c.red, c.box.x0, c.box.x1, c.box.y0, c.box.y1, c.box.w(), c.box.h(), 100.0 * c.box.w() / g.nx,
                100.0 * c.box.h() / g.ny);
    for (double share : {0.9, 1.0}) {
      hr::Ring r = hr::grow(g, c.box, cool, share);
      const char *sides[4] = {"left", "right", "bottom", "top"};
      std::string hits;
      for (int s = 0; s < 4; ++s)
        if (r.edgeHit[s])
          hits += std::string(hits.empty() ? "" : ",") + sides[s];
      std::printf("  %s: rect x %d-%d y %d-%d = %d x %d tiles = %.1f%% x %.1f%% of the die; band L %d R %d B %d T %d%s%s\n",
                  share < 1 ? "cool on 90% of each side" : "cool all the way around", r.rect.x0, r.rect.x1, r.rect.y0,
                  r.rect.y1, r.rect.w(), r.rect.h(), 100.0 * r.rect.w() / g.nx, 100.0 * r.rect.h() / g.ny,
                  c.box.x0 - r.rect.x0, r.rect.x1 - c.box.x1, c.box.y0 - r.rect.y0, r.rect.y1 - c.box.y1,
                  hits.empty() ? "" : "; reached the die edge without cooling: ", hits.c_str());
    }
  }
  return 0;
}
