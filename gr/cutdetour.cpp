// cutdetour.cpp -- how far along its own line an overflowed edge's nets
// would have to move to find room (HISTORY_COST_SWEEP.md Part 97). An
// analysis tool, not a router.
//
//   cutdetour <benchmark.gr> <solution.route>
//   gunzip -c x.route.gz | cutdetour <benchmark.gr> /dev/stdin
//   cutdetour --selftest
//
// A net crossing a horizontal tile edge (x,y)-(x+1,y) crosses the vertical
// line between columns x and x+1; to stop using that edge it must cross the
// same line at another row y' (and likewise for vertical edges, along a
// row line). So the room that matters for an overflowed edge is spare
// capacity on the same line, not free tiles anywhere nearby. Usage is
// charged as gr_verify does and collapsed to 2D (summed over layers). For
// every overflowed 2D edge, two distances along its line:
//   nearest: to the nearest edge on the line with room for one more
//     default net (capacity - use >= 1 + layer-1 spacing);
//   window: the smallest d such that the edges within d of it on the line
//     have, together, at least as much spare as overflow (sum over the
//     window of capacity - use >= 0) -- the stretch of line that can absorb
//     every overflow in it, its overflowed neighbours included. "none" if
//     the whole line is over capacity in aggregate (crossing it is forced).
// Reported: the number of overflowed edges, the median / 90th percentile /
// max of both distances, and how many need a window over 10, 25, 50, 100.

#include <algorithm>
#include <climits>
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

namespace cd {

struct Usage2D {
  std::vector<long long> hu, hc, vu, vc; // per 2D edge: h index y*(nx-1)+x, v index y*nx+x
};

Usage2D charge(std::istream &in, const Design &d) {
  const RoutingGrid &g = d.grid;
  std::unordered_map<std::string, int> width;
  for (const gr_io::Net &n : d.nets)
    width[n.name] = n.minWidth;
  Usage2D u;
  u.hu.assign(static_cast<size_t>(g.nx - 1) * g.ny, 0);
  u.hc.assign(u.hu.size(), 0);
  u.vu.assign(static_cast<size_t>(g.nx) * (g.ny - 1), 0);
  u.vc.assign(u.vu.size(), 0);
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x)
      for (int l = 0; l < g.nLayers; ++l)
        u.hc[static_cast<size_t>(y) * (g.nx - 1) + x] += g.hcap[g.hIndex(x, y, l)];
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x)
      for (int l = 0; l < g.nLayers; ++l)
        u.vc[static_cast<size_t>(y) * g.nx + x] += g.vcap[g.vIndex(x, y, l)];
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
      int ax = g.colOf(x1), ay = g.rowOf(y1), bx = g.colOf(x2), by = g.rowOf(y2), la = static_cast<int>(l1) - 1;
      if (l1 != l2)
        continue;
      auto it = width.find(cur);
      int amt = (it == width.end() ? 1 : it->second) + g.minSpacing[la];
      if (ay == by && ax != bx)
        for (int x = std::min(ax, bx); x < std::max(ax, bx); ++x)
          u.hu[static_cast<size_t>(ay) * (g.nx - 1) + x] += amt;
      else if (ax == bx && ay != by)
        for (int y = std::min(ay, by); y < std::max(ay, by); ++y)
          u.vu[static_cast<size_t>(y) * g.nx + ax] += amt;
      continue;
    }
    std::istringstream hs(s);
    hs >> cur;
    open = true;
  }
  return u;
}

struct Result {
  std::vector<int> nearest, window; // per overflowed edge; INT_MAX = none
  long long overflowedEdges = 0, overflow = 0;
};

// One line: spare[i] = cap - use along it; returns, for each overflowed
// position, the nearest distance and the window distance.
void scanLine(const std::vector<long long> &spare, long long unit, Result &r) {
  const int n = static_cast<int>(spare.size());
  long long total = 0;
  for (long long s : spare)
    total += s;
  for (int i = 0; i < n; ++i) {
    if (spare[i] >= 0)
      continue;
    ++r.overflowedEdges;
    r.overflow += -spare[i];
    int nearest = INT_MAX;
    for (int d = 1; d < n && nearest == INT_MAX; ++d)
      if ((i - d >= 0 && spare[i - d] >= unit) || (i + d < n && spare[i + d] >= unit))
        nearest = d;
    int window = INT_MAX;
    if (total >= 0) {
      long long sum = spare[i];
      for (int d = 1; d < n; ++d) {
        if (i - d >= 0)
          sum += spare[i - d];
        if (i + d < n)
          sum += spare[i + d];
        if (sum >= 0) {
          window = d;
          break;
        }
        if (i - d < 0 && i + d >= n)
          break;
      }
    }
    r.nearest.push_back(nearest);
    r.window.push_back(window);
  }
}

Result analyze(const RoutingGrid &g, const Usage2D &u) {
  Result r;
  long long unit = 1 + g.minSpacing[0];
  // Horizontal edges between columns x and x+1: the line runs over y.
  for (int x = 0; x + 1 < g.nx; ++x) {
    std::vector<long long> sp(g.ny);
    for (int y = 0; y < g.ny; ++y) {
      size_t i = static_cast<size_t>(y) * (g.nx - 1) + x;
      sp[y] = u.hc[i] - u.hu[i];
    }
    scanLine(sp, unit, r);
  }
  // Vertical edges between rows y and y+1: the line runs over x.
  for (int y = 0; y + 1 < g.ny; ++y) {
    std::vector<long long> sp(g.nx);
    for (int x = 0; x < g.nx; ++x) {
      size_t i = static_cast<size_t>(y) * g.nx + x;
      sp[x] = u.vc[i] - u.vu[i];
    }
    scanLine(sp, unit, r);
  }
  return r;
}

std::string pctStr(std::vector<int> v, double q) {
  if (v.empty())
    return "-";
  std::sort(v.begin(), v.end());
  int x = v[std::min(v.size() - 1, static_cast<size_t>(q * v.size()))];
  return x == INT_MAX ? "none" : std::to_string(x);
}

} // namespace cd

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "cutdetour self test\n";
  using namespace cd;
  // A line of 10 edges, unit 2: spare 4 everywhere except edges 4 and 5 at
  // -2 each (one net over). Nearest room: 1 from each. Window: from 4,
  // d = 1 covers 3,4,5 = 4 - 2 - 2 = 0 -> 1; from 5 likewise 1.
  Result r;
  std::vector<long long> sp = {4, 4, 4, 4, -2, -2, 4, 4, 4, 4};
  scanLine(sp, 2, r);
  check(r.overflowedEdges == 2 && r.overflow == 4 && r.nearest[0] == 1 && r.nearest[1] == 1 && r.window[0] == 1 &&
            r.window[1] == 1,
        "two overflowed edges in a roomy line: nearest 1, window 1");
  // A crowded line: spare 0 for 6 edges around a -4: nearest room 4 away,
  // window 4 (edges 0..8 hold 2+0+...: needs both 2-spare ends).
  Result r2;
  std::vector<long long> sp2 = {2, 0, 0, 0, -4, 0, 0, 0, 2, 0};
  scanLine(sp2, 2, r2);
  check(r2.nearest[0] == 4 && r2.window[0] == 4, "a full stretch: nearest room and window both 4 tiles away");
  // A line over capacity in aggregate: window none.
  Result r3;
  std::vector<long long> sp3 = {0, -2, 0, 1};
  scanLine(sp3, 2, r3);
  check(r3.window[0] == INT_MAX && r3.nearest[0] == INT_MAX, "a line over capacity in aggregate: no window, no room");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> <solution.route>\n";
    return 2;
  }
  Design d = gr_io::readGr(argv[1]);
  std::ifstream in(argv[2]);
  if (!in)
    fail(std::string("cannot open ") + argv[2]);
  cd::Usage2D u = cd::charge(in, d);
  cd::Result r = cd::analyze(d.grid, u);
  auto over = [&](int t) {
    long long n = 0;
    for (int w : r.window)
      n += w > t;
    return n;
  };
  std::printf("overflowed 2D edges %lld (2D overflow %lld)\n", r.overflowedEdges, r.overflow);
  std::printf("  nearest room along the line: median %s, 90th %s, max %s\n", cd::pctStr(r.nearest, 0.5).c_str(),
              cd::pctStr(r.nearest, 0.9).c_str(), cd::pctStr(r.nearest, 1.0).c_str());
  std::printf("  window that absorbs its overflow: median %s, 90th %s, max %s; over 10: %lld, over 25: %lld, over 50: "
              "%lld, over 100: %lld\n",
              cd::pctStr(r.window, 0.5).c_str(), cd::pctStr(r.window, 0.9).c_str(), cd::pctStr(r.window, 1.0).c_str(),
              over(10), over(25), over(50), over(100));
  return 0;
}
