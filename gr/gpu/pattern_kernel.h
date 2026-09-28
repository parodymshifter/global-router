// pattern_kernel.h -- the batched pattern-routing kernel's contract, shared
// by every backend (CPU reference, Metal, later CUDA). See
// GPU_BACKEND_PLAN.md at the repo root.
//
// Problem: for each 2-pin piece (x1,y1) -> (x2,y2) on a 2D grid whose tile
// edges carry fixed-point integer costs, find the cheapest pattern route
// among a canonical candidate list, and its cost.
//
// Candidates, in this exact order (the order IS the tie-break -- lowest
// index wins -- so every backend must enumerate identically):
//   dx == 0 && dy == 0 : none (best = -1, cost = 0)
//   dx == 0 || dy == 0 : 0 = the straight run
//   otherwise           : 0 = L, horizontal first (row y1 from x1 to x2, then
//                             column x2 from y1 to y2)
//                         1 = L, vertical first (column x1 from y1 to y2,
//                             then row y2 from x1 to x2)
//                         2 .. 2+(|dx|-2) : Z with its vertical jog at column
//                             m, for m strictly between x1 and x2, stepping
//                             from x1 toward x2: row y1 x1->m, column m
//                             y1->y2, row y2 m->x2
//                         then          : Z with its horizontal jog at row m,
//                             m strictly between y1 and y2, stepping from y1
//                             toward y2: column x1 y1->m, row m x1->x2,
//                             column x2 m->y2
//
// Costs are int64 fixed point (kCostOne per unit). Every edge cost must lie
// in [0, kCostMax]; a forbidden edge uses kCostMax. With kCostMax = 2^40 and
// no route longer than 2^20 tile edges, a candidate's sum stays below 2^60
// -- no overflow anywhere, on any backend. Integer addition and comparison
// are exact and order independent, which is what makes CPU and GPU results
// bit-identical (Apple GPUs have no FP64; floats would not be).

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gpu {

constexpr int64_t kCostOne = int64_t(1) << 16;
constexpr int64_t kCostMax = int64_t(1) << 40;

// Quantizes a nonnegative double cost to fixed point: monotone (a larger
// double never maps lower), NaN/negative -> 0, huge/inf -> kCostMax.
inline int64_t quantizeCost(double c) {
  if (!(c > 0.0))
    return 0;
  double scaled = c * static_cast<double>(kCostOne);
  if (!(scaled < static_cast<double>(kCostMax)))
    return kCostMax;
  return static_cast<int64_t>(scaled); // truncation is monotone
}

struct PatternGrid {
  int nx = 0, ny = 0;
  std::vector<int64_t> h; // (nx-1) * ny: edge (x,y)-(x+1,y) at y*(nx-1)+x
  std::vector<int64_t> v; // nx * (ny-1): edge (x,y)-(x,y+1) at y*nx+x
};

struct PatternBatch {
  std::vector<int32_t> x1, y1, x2, y2;
  size_t size() const { return x1.size(); }
};

struct PatternResult {
  std::vector<int32_t> best; // candidate index, -1 if none
  std::vector<int64_t> cost;
};

inline int64_t candidateCount(int dx, int dy) {
  dx = dx < 0 ? -dx : dx;
  dy = dy < 0 ? -dy : dy;
  if (dx == 0 && dy == 0)
    return 0;
  if (dx == 0 || dy == 0)
    return 1;
  return 2 + (dx - 1) + (dy - 1);
}

// Calls f(horizontal, x, y) for every tile edge of candidate k of the piece
// (x1,y1)->(x2,y2), in the canonical order above (horizontal edge (x,y) is
// (x,y)-(x+1,y); vertical is (x,y)-(x,y+1)). Out-of-range k emits nothing.
// This is how a chosen index is committed; its edges' costs sum to exactly
// the cost the backend reported (pattern_selftest checks it).
template <class F> inline void forEachCandidateEdge(int x1, int y1, int x2, int y2, int64_t k, F f) {
  auto rowRun = [&](int y, int a, int c) {
    for (int x = a < c ? a : c; x < (a < c ? c : a); ++x)
      f(true, x, y);
  };
  auto colRun = [&](int x, int a, int c) {
    for (int y = a < c ? a : c; y < (a < c ? c : a); ++y)
      f(false, x, y);
  };
  if (k < 0 || (x1 == x2 && y1 == y2))
    return;
  if (x1 == x2 || y1 == y2) {
    if (k == 0) {
      if (y1 == y2)
        rowRun(y1, x1, x2);
      else
        colRun(x1, y1, y2);
    }
    return;
  }
  int adx = x1 < x2 ? x2 - x1 : x1 - x2, ady = y1 < y2 ? y2 - y1 : y1 - y2;
  int sx = x2 > x1 ? 1 : -1, sy = y2 > y1 ? 1 : -1;
  if (k == 0) {
    rowRun(y1, x1, x2);
    colRun(x2, y1, y2);
  } else if (k == 1) {
    colRun(x1, y1, y2);
    rowRun(y2, x1, x2);
  } else if (k < 2 + (adx - 1)) {
    int m = x1 + sx * static_cast<int>(k - 1);
    rowRun(y1, x1, m);
    colRun(m, y1, y2);
    rowRun(y2, m, x2);
  } else if (k < 2 + (adx - 1) + (ady - 1)) {
    int m = y1 + sy * static_cast<int>(k - 1 - (adx - 1));
    colRun(x1, y1, m);
    rowRun(m, x1, x2);
    colRun(x2, m, y2);
  }
}

class IPatternBackend {
public:
  virtual ~IPatternBackend() = default;
  virtual std::string name() const = 0;
  virtual void solve(const PatternGrid &g, const PatternBatch &b, PatternResult &r) = 0;
};

// The CPU reference: exactly the algorithm every GPU backend implements --
// per-row and per-column prefix sums, then per piece an ascending scan of
// its candidates keeping the first strict minimum.
class CpuPatternBackend : public IPatternBackend {
public:
  std::string name() const override { return "cpu"; }
  void solve(const PatternGrid &g, const PatternBatch &b, PatternResult &r) override {
    int nx = g.nx, ny = g.ny;
    // rowP[y*nx + x] = sum of h edges (0..x-1, y); colP[x*ny + y] = sum of v edges (x, 0..y-1)
    std::vector<int64_t> rowP(static_cast<size_t>(nx) * ny, 0), colP(static_cast<size_t>(nx) * ny, 0);
    for (int y = 0; y < ny; ++y)
      for (int x = 1; x < nx; ++x)
        rowP[y * nx + x] = rowP[y * nx + x - 1] + g.h[static_cast<size_t>(y) * (nx - 1) + (x - 1)];
    for (int x = 0; x < nx; ++x)
      for (int y = 1; y < ny; ++y)
        colP[static_cast<size_t>(x) * ny + y] = colP[static_cast<size_t>(x) * ny + y - 1] + g.v[static_cast<size_t>(y - 1) * nx + x];
    auto row = [&](int y, int a, int c) { // |run| along row y between columns a and c
      int lo = a < c ? a : c, hi = a < c ? c : a;
      return rowP[static_cast<size_t>(y) * nx + hi] - rowP[static_cast<size_t>(y) * nx + lo];
    };
    auto col = [&](int x, int a, int c) {
      int lo = a < c ? a : c, hi = a < c ? c : a;
      return colP[static_cast<size_t>(x) * ny + hi] - colP[static_cast<size_t>(x) * ny + lo];
    };
    size_t n = b.size();
    r.best.assign(n, -1);
    r.cost.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
      int x1 = b.x1[i], y1 = b.y1[i], x2 = b.x2[i], y2 = b.y2[i];
      if (x1 == x2 && y1 == y2)
        continue;
      if (x1 == x2 || y1 == y2) {
        r.best[i] = 0;
        r.cost[i] = y1 == y2 ? row(y1, x1, x2) : col(x1, y1, y2);
        continue;
      }
      int64_t best = row(y1, x1, x2) + col(x2, y1, y2);
      int32_t bi = 0;
      int64_t c = col(x1, y1, y2) + row(y2, x1, x2);
      if (c < best) {
        best = c;
        bi = 1;
      }
      int32_t k = 2;
      int sx = x2 > x1 ? 1 : -1, sy = y2 > y1 ? 1 : -1;
      for (int m = x1 + sx; m != x2; m += sx, ++k) {
        c = row(y1, x1, m) + col(m, y1, y2) + row(y2, m, x2);
        if (c < best) {
          best = c;
          bi = k;
        }
      }
      for (int m = y1 + sy; m != y2; m += sy, ++k) {
        c = col(x1, y1, m) + row(m, x1, x2) + col(x2, m, y2);
        if (c < best) {
          best = c;
          bi = k;
        }
      }
      r.best[i] = bi;
      r.cost[i] = best;
    }
  }
};

// Backend selection (backend_select.cpp): GR_GPU=auto|metal|cuda|cpu.
// auto picks the first available of cuda, metal, cpu; an explicitly
// requested backend that is not available is an error (returns nullptr and
// sets err), never a silent fallback.
IPatternBackend *selectPatternBackend(std::string &chosen, std::string &err);

} // namespace gpu
