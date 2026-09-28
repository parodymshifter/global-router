// pattern_selftest.cpp -- self-test and benchmark for the pattern-routing
// kernel backends (GPU_BACKEND_PLAN.md phase B).
//
//   pattern_selftest            hand fixtures on CPU; CPU-vs-selected-backend
//                               bit-identity on random batches; timings
//   GR_GPU=cpu|metal|auto ...   picks the backend compared against the CPU
//
// A GPU backend that disagrees with the CPU reference in a single best
// index or cost is wrong, whatever the reason.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "pattern_kernel.h"

using namespace gpu;

static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}

static uint64_t mix(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

static PatternGrid uniformGrid(int nx, int ny, int64_t c) {
  PatternGrid g;
  g.nx = nx;
  g.ny = ny;
  g.h.assign(static_cast<size_t>(nx - 1) * ny, c);
  g.v.assign(static_cast<size_t>(nx) * (ny - 1), c);
  return g;
}
static int64_t &hEdge(PatternGrid &g, int x, int y) { return g.h[static_cast<size_t>(y) * (g.nx - 1) + x]; }
static int64_t &vEdge(PatternGrid &g, int x, int y) { return g.v[static_cast<size_t>(y) * g.nx + x]; }

// Hand fixtures, CPU reference.
static void testHandFixtures() {
  CpuPatternBackend cpu;
  PatternResult r;
  // Uniform cost 1: (1,1)->(5,4): every monotone candidate costs 7 -> the
  // tie goes to index 0 (L, horizontal first); count = 2 + 3 + 2 = 7.
  PatternGrid g = uniformGrid(8, 8, kCostOne);
  PatternBatch b;
  b.x1 = {1, 2, 3, 6};
  b.y1 = {1, 2, 3, 6};
  b.x2 = {5, 2, 7, 6};
  b.y2 = {4, 6, 3, 6};
  cpu.solve(g, b, r);
  check(candidateCount(4, 3) == 7, "candidate count for a 4 x 3 span is 2 L + 3 + 2 Z = 7");
  check(r.best[0] == 0 && r.cost[0] == 7 * kCostOne, "uniform costs: all tie at 7, lowest index (L, horizontal first) wins");
  check(r.best[1] == 0 && r.cost[1] == 4 * kCostOne, "vertical straight run (2,2)-(2,6): candidate 0, cost 4");
  check(r.best[2] == 0 && r.cost[2] == 4 * kCostOne, "horizontal straight run (3,3)-(7,3): candidate 0, cost 4");
  check(r.best[3] == -1 && r.cost[3] == 0, "zero-length piece: no candidate");
  // Block row 1 between x=2..3 (the first L's horizontal leg) and column 1
  // between y=2..3 (the second L's vertical leg): the cheapest is a Z. With
  // the jog column m = 2 blocked too, the first free Z-column jog is m = 3
  // -> index 2 + (3 - 2) = 3.
  PatternGrid z = uniformGrid(8, 8, kCostOne);
  hEdge(z, 2, 1) = kCostMax;
  vEdge(z, 1, 2) = kCostMax;
  vEdge(z, 2, 2) = kCostMax;
  PatternBatch bz;
  bz.x1 = {1};
  bz.y1 = {1};
  bz.x2 = {5};
  bz.y2 = {4};
  cpu.solve(z, bz, r);
  // Z at column 3: row 1 x1->3 crosses the blocked (2,1) edge too. The
  // cheapest unblocked shapes are the row-jog Zs (column 1 y1->m crosses
  // (1,2) only when m > 2): m = 2 -> column 1 from 1 to 2 (free), row 2
  // from 1 to 5, column 5 from 2 to 4: cost 1 + 4 + 2 = 7, index
  // 2 + (|dx|-1 = 3) + (m - y1 - 1 = 0) = 5.
  check(r.best[0] == 5 && r.cost[0] == 7 * kCostOne,
        "blocked Ls and column jogs: the row-jog Z at y = 2 wins, index 5, cost 7 (got " +
            std::to_string(r.best[0]) + ")");
  check(quantizeCost(1.0) == kCostOne && quantizeCost(-3.0) == 0 && quantizeCost(1e300) == kCostMax &&
            quantizeCost(0.5) <= quantizeCost(0.5000001),
        "quantization: 1 -> kCostOne, negative -> 0, huge -> kCostMax, monotone");
}

static void randomCase(uint64_t seed, int nx, int ny, size_t n, int maxSpan, PatternGrid &g, PatternBatch &b) {
  g = uniformGrid(nx, ny, 0);
  for (size_t i = 0; i < g.h.size(); ++i) {
    uint64_t r = mix(seed * 1315423911u + i * 2 + 1);
    g.h[i] = (r % 97 == 0) ? kCostMax : static_cast<int64_t>(kCostOne + (r >> 20) % (8 * kCostOne));
  }
  for (size_t i = 0; i < g.v.size(); ++i) {
    uint64_t r = mix(seed * 2654435761u + i * 2);
    g.v[i] = (r % 97 == 0) ? kCostMax : static_cast<int64_t>(kCostOne + (r >> 20) % (8 * kCostOne));
  }
  b = PatternBatch{};
  for (size_t i = 0; i < n; ++i) {
    uint64_t r = mix(seed ^ (i * 0x9e3779b97f4a7c15ULL));
    int x1 = static_cast<int>(r % nx), y1 = static_cast<int>((r >> 16) % ny);
    int dx = static_cast<int>((r >> 32) % (2 * maxSpan + 1)) - maxSpan;
    int dy = static_cast<int>((r >> 44) % (2 * maxSpan + 1)) - maxSpan;
    int x2 = std::min(nx - 1, std::max(0, x1 + dx)), y2 = std::min(ny - 1, std::max(0, y1 + dy));
    b.x1.push_back(x1);
    b.y1.push_back(y1);
    b.x2.push_back(x2);
    b.y2.push_back(y2);
  }
}

static double seconds(std::chrono::steady_clock::time_point a) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

int main() {
  std::cout << "pattern kernel self test\n";
  testHandFixtures();

  std::string chosen, err;
  IPatternBackend *be = selectPatternBackend(chosen, err);
  check(be != nullptr, "backend selected: " + (be ? chosen : err));
  if (!be) {
    std::cout << "\nSELF TEST FAIL\n";
    return 1;
  }
  CpuPatternBackend cpu;

  // Bit-identity: many shapes of grid and batch, including tiny/degenerate.
  struct Case {
    int nx, ny;
    size_t n;
    int span;
  };
  Case cases[] = {{2, 2, 50, 1}, {3, 40, 500, 20}, {40, 3, 500, 20}, {64, 64, 5000, 16},
                  {200, 150, 20000, 60}, {399, 399, 100000, 40}, {324, 324, 50000, 200}};
  int identical = 0, total = 0;
  for (int s = 0; s < 3; ++s)
    for (const Case &c : cases) {
      PatternGrid g;
      PatternBatch b;
      randomCase(1000 + s * 17 + c.nx, c.nx, c.ny, c.n, c.span, g, b);
      PatternResult rc, rg;
      cpu.solve(g, b, rc);
      be->solve(g, b, rg);
      ++total;
      identical += rc.best == rg.best && rc.cost == rg.cost;
    }
  check(identical == total, chosen + " vs cpu: bit-identical best index and cost on " + std::to_string(identical) + "/" +
                                std::to_string(total) + " random batches (2x2 up to 399x399, up to 100000 pieces)");

  // Timing on a large batch (the scale the GPU is for).
  {
    PatternGrid g;
    PatternBatch b;
    randomCase(7, 399, 399, 1000000, 40, g, b);
    PatternResult rc, rg;
    auto t0 = std::chrono::steady_clock::now();
    cpu.solve(g, b, rc);
    double tc = seconds(t0);
    be->solve(g, b, rg); // warm-up (pipeline, buffers)
    t0 = std::chrono::steady_clock::now();
    be->solve(g, b, rg);
    double tg = seconds(t0);
    check(rc.best == rg.best && rc.cost == rg.cost, "1,000,000-piece batch: bit-identical");
    std::printf("  time  1,000,000 pieces on 399x399: cpu %.3fs, %s %.3fs (%.1fx)\n", tc, chosen.c_str(), tg,
                tc / (tg > 0 ? tg : 1e-9));
  }
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
