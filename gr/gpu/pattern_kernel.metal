// pattern_kernel.metal -- Metal backend of the batched pattern-routing
// kernel. Contract: pattern_kernel.h. Every operation here mirrors
// CpuPatternBackend::solve exactly -- 64-bit integer prefix sums and an
// ascending candidate scan keeping the first strict minimum -- so results
// are bit-identical to the CPU reference.
#include <metal_stdlib>
using namespace metal;

struct Dims {
  uint nx, ny, n;
};

// One thread per row: rowP[y*nx + x] = sum of h over (0..x-1, y).
kernel void rowPrefix(device const long *h [[buffer(0)]], device long *rowP [[buffer(1)]],
                      constant Dims &d [[buffer(2)]], uint y [[thread_position_in_grid]]) {
  if (y >= d.ny)
    return;
  ulong base = ulong(y) * d.nx;
  long acc = 0;
  rowP[base] = 0;
  for (uint x = 1; x < d.nx; ++x) {
    acc += h[ulong(y) * (d.nx - 1) + (x - 1)];
    rowP[base + x] = acc;
  }
}

// One thread per column: colP[x*ny + y] = sum of v over (x, 0..y-1).
kernel void colPrefix(device const long *v [[buffer(0)]], device long *colP [[buffer(1)]],
                      constant Dims &d [[buffer(2)]], uint x [[thread_position_in_grid]]) {
  if (x >= d.nx)
    return;
  ulong base = ulong(x) * d.ny;
  long acc = 0;
  colP[base] = 0;
  for (uint y = 1; y < d.ny; ++y) {
    acc += v[ulong(y - 1) * d.nx + x];
    colP[base + y] = acc;
  }
}

static long runRow(device const long *rowP, uint nx, int y, int a, int c) {
  int lo = min(a, c), hi = max(a, c);
  return rowP[ulong(y) * nx + hi] - rowP[ulong(y) * nx + lo];
}
static long runCol(device const long *colP, uint ny, int x, int a, int c) {
  int lo = min(a, c), hi = max(a, c);
  return colP[ulong(x) * ny + hi] - colP[ulong(x) * ny + lo];
}

// One thread per piece.
kernel void bestPattern(device const long *rowP [[buffer(0)]], device const long *colP [[buffer(1)]],
                        device const int *px1 [[buffer(2)]], device const int *py1 [[buffer(3)]],
                        device const int *px2 [[buffer(4)]], device const int *py2 [[buffer(5)]],
                        device int *outBest [[buffer(6)]], device long *outCost [[buffer(7)]],
                        constant Dims &d [[buffer(8)]], uint i [[thread_position_in_grid]]) {
  if (i >= d.n)
    return;
  int x1 = px1[i], y1 = py1[i], x2 = px2[i], y2 = py2[i];
  uint nx = d.nx, ny = d.ny;
  if (x1 == x2 && y1 == y2) {
    outBest[i] = -1;
    outCost[i] = 0;
    return;
  }
  if (x1 == x2 || y1 == y2) {
    outBest[i] = 0;
    outCost[i] = (y1 == y2) ? runRow(rowP, nx, y1, x1, x2) : runCol(colP, ny, x1, y1, y2);
    return;
  }
  long best = runRow(rowP, nx, y1, x1, x2) + runCol(colP, ny, x2, y1, y2);
  int bi = 0;
  long c = runCol(colP, ny, x1, y1, y2) + runRow(rowP, nx, y2, x1, x2);
  if (c < best) {
    best = c;
    bi = 1;
  }
  int k = 2;
  int sx = x2 > x1 ? 1 : -1, sy = y2 > y1 ? 1 : -1;
  for (int m = x1 + sx; m != x2; m += sx, ++k) {
    c = runRow(rowP, nx, y1, x1, m) + runCol(colP, ny, m, y1, y2) + runRow(rowP, nx, y2, m, x2);
    if (c < best) {
      best = c;
      bi = k;
    }
  }
  for (int m = y1 + sy; m != y2; m += sy, ++k) {
    c = runCol(colP, ny, x1, y1, m) + runRow(rowP, nx, m, x1, x2) + runCol(colP, ny, x2, m, y2);
    if (c < best) {
      best = c;
      bi = k;
    }
  }
  outBest[i] = bi;
  outCost[i] = best;
}
