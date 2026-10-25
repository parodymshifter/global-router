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

#include "cut_bound.h"


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
