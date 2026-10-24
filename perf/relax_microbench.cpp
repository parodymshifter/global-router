// relax_microbench.cpp -- measures the two costs the in-net parallel search model needs that a profile of the
// sequential router cannot show (HISTORY_COST_SWEEP.md Part 153; gr/search_parallel_sim.py's --edge-ns and
// --sync-us): the time per edge relaxation with an atomic minimum when T threads share one box's distance array,
// and the time of the barrier that ends every delta-stepping phase.
//
//   g++ -std=c++17 -O2 -pthread -o relax_microbench perf/relax_microbench.cpp && ./relax_microbench [W H]
//
// Method: T threads (a persistent pool, the main thread among them) run phases in lockstep. A phase relaxes F
// edges split evenly over the threads -- each a random node of the W x H box and one of its four neighbours, a
// compare-and-swap minimum on the neighbour's distance (doubles stored as their bit patterns: for non-negative
// doubles integer order is numeric order), and on success an append to the thread's own bucket list, as
// delta-stepping does -- then a sense-reversing spin barrier. Phase time is fitted as a + b * ceil(F / T) over
// several F: a is the per-phase synchronisation cost, b the per-edge cost. The distance array is reset every 64
// phases so relaxations keep a realistic mix of successes and failures.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

struct Barrier {
  std::atomic<int> count{0};
  std::atomic<int> sense{0};
  int n;
  explicit Barrier(int n_) : n(n_) {}
  void wait(int &local) {
    local ^= 1;
    if (count.fetch_add(1, std::memory_order_acq_rel) == n - 1) {
      count.store(0, std::memory_order_relaxed);
      sense.store(local, std::memory_order_release);
    } else {
      while (sense.load(std::memory_order_acquire) != local) {
      }
    }
  }
};

inline uint64_t bitsOf(double d) {
  uint64_t u;
  std::memcpy(&u, &d, 8);
  return u;
}

inline uint64_t splitmix(uint64_t &s) {
  uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// Runs `phases` phases of F edges each on T threads; returns seconds per phase.
double timePhases(int T, int W, int H, long F, int phases) {
  const int N = W * H;
  std::vector<std::atomic<uint64_t>> dist(N);
  for (auto &d : dist) d.store(bitsOf(1e30), std::memory_order_relaxed);
  Barrier bar(T);
  std::atomic<bool> go{false};
  std::vector<std::vector<int>> bucket(T);
  double result = 0;
  auto body = [&](int t) {
    uint64_t seed = 0x1234 + 977 * t;
    int local = 0;
    const long lo = F * t / T, hi = F * (t + 1) / T;
    bucket[t].reserve(1 << 16);
    while (!go.load(std::memory_order_acquire)) {
    }
    bar.wait(local);
    auto t0 = std::chrono::steady_clock::now();
    for (int p = 0; p < phases; ++p) {
      if (p % 64 == 0) {
        for (int i = t; i < N; i += T) dist[i].store(bitsOf(1e30), std::memory_order_relaxed);
        bar.wait(local);
      }
      bucket[t].clear();
      for (long e = lo; e < hi; ++e) {
        uint64_t r = splitmix(seed);
        int v = int(r % uint64_t(N));
        int x = v % W, y = v / W;
        int dir = int((r >> 40) & 3);
        int u = dir == 0 ? (x + 1 < W ? v + 1 : v - 1)
              : dir == 1 ? (x > 0 ? v - 1 : v + 1)
              : dir == 2 ? (y + 1 < H ? v + W : v - W)
                         : (y > 0 ? v - W : v + W);
        double nd = 1.0 + double((r >> 44) & 0xfffff) * (1.0 / 1024);
        uint64_t nb = bitsOf(nd), old = dist[u].load(std::memory_order_relaxed);
        while (nb < old && !dist[u].compare_exchange_weak(old, nb, std::memory_order_relaxed)) {
        }
        if (nb < old) bucket[t].push_back(u);
      }
      bar.wait(local);
    }
    if (t == 0) result = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / phases;
  };
  std::vector<std::thread> pool;
  for (int t = 1; t < T; ++t) pool.emplace_back(body, t);
  go.store(true, std::memory_order_release);
  body(0);
  for (auto &th : pool) th.join();
  return result;
}

}  // namespace

int main(int argc, char **argv) {
  int W = argc > 2 ? std::atoi(argv[1]) : 200, H = argc > 2 ? std::atoi(argv[2]) : 200;
  std::printf("box %d x %d (%d nodes, %.0f KB of distances)\n", W, H, W * H, W * H * 8 / 1024.0);
  const long Fs[] = {8, 64, 256, 1024, 4096, 16384};
  for (int T : {1, 2, 4, 8, 10}) {
    // least-squares fit of phase time = a + b * ceil(F / T)
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    std::printf("threads %2d:", T);
    for (long F : Fs) {
      int phases = int(std::max(256L, 4000000L / F));
      timePhases(T, W, H, F, 64);  // warm up
      double s = timePhases(T, W, H, F, phases);
      double x = std::ceil(double(F) / T), y = s * 1e9;
      sx += x, sy += y, sxx += x * x, sxy += x * y, ++n;
      std::printf("  F %5ld %8.0f ns", F, y);
    }
    double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n;
    std::printf("\n            fit: sync %.2f us per phase, %.1f ns per edge per thread\n", a / 1000, b);
  }
  return 0;
}
