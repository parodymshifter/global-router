# GPU backend plan: batched pattern routing on Metal (local) and CUDA (cloud CI)

Status (2026-09-28): **phase B's kernel library is built and verified**
(`gr/gpu/`): contract `pattern_kernel.h` (canonical candidate order,
fixed-point costs, CPU reference backend), Metal backend `pattern_metal.mm`
+ `pattern_kernel.metal` (Objective-C++ against the system Metal framework
rather than vendored metal-cpp -- no download needed, same C++ interface),
`backend_select.cpp` (`GR_GPU`), CMake build with offline `.metallib` and a
run-time-compile fallback, and `pattern_selftest`. Measured on the M4 Pro:
Metal bit-identical to the CPU reference on 21/21 random batches (2x2 to
399x399, up to 100000 pieces) and on a 1,000,000-piece batch, with both the
offline library and the run-time compile; 1M pieces on 399x399 in 0.018 s vs
0.087 s on one CPU core (4.7x, including per-call buffer setup) -- measured
under Rosetta; natively (arm64, after the CMake fix) 0.014 s vs 0.061 s,
4.4x. End to end, the native Metal build of pd11route is byte-identical to
the one-line g++ CPU build (HISTORY_COST_SWEEP.md Part 72). Build note: an
Intel `cmake` under Rosetta builds x86_64 targets unless told otherwise;
CMakeLists.txt forces arm64 on Apple silicon. Not yet
wired into a router (that is phase A's CPU pattern stage + the batch
driver). This fills in step 5 of
the roadmap agreed in the session that produced `pd9route`:

1. CPU runtime work, including a **CPU pattern-routing stage** (L/Z and
   3D pattern routes on present-demand costs; initial routes, and the
   first attempt for each overflowed piece before any maze search).
2. The remaining 13 ISPD 2008 benchmarks.
3. ISPD 2024 `.cap`/`.net` reader, writer, and scoring.
4. CPU multithreading by bounding-box coloring.
5. **GPU port of the pattern-routing kernel** (this document).
6. OpenROAD (ISPD 2025 timing/power, detailed-routing validation).

## Goals

- One source tree, one kernel written once in spirit, three backends: CPU
  (the reference), Metal (Apple GPUs, local experiments on the M4 Pro),
  CUDA (NVIDIA, cloud GPU in CI and contest-style runs).
- The binary picks its backend at run time from what the machine has:
  CUDA if compiled in and a device answers, else Metal if compiled in and
  a device answers, else CPU. `GR_GPU=auto|cuda|metal|cpu` overrides
  (`auto` is the default; asking for a backend that is unavailable fails
  loudly rather than silently falling back -- a quietly different run is
  the "wrong code still runs" failure this project guards against).
- **Every backend produces bit-identical results** for the same input.
  The CPU backend is the oracle; a GPU backend that disagrees is wrong.
  This keeps CLAUDE.md's reproducibility requirement and its rule that a
  parallel bug in an untrusted router is unfindable.

## What runs on the GPU

Only the batched pattern-routing kernel. Everything maze-shaped -- MSMV,
the region stage, refinement, WL recovery, layer assignment -- stays on
CPU threads (step 4). This is the split the ISPD 2024 winners use.

Per batch (one color class: nets whose boxes are disjoint, routed against
one frozen cost snapshot, as CLAUDE.md's scheduler section prescribes):

1. Upload (or update in place) the per-edge cost arrays for the snapshot.
2. Per row / per column prefix sums of edge cost (one thread per row or
   column), so the cost of any straight run is `prefix[b] - prefix[a]`.
3. One thread per (2-pin piece, candidate): L shapes (2), Z shapes (one
   per jog position in the box), later 3D candidates with a small layer DP.
   Each candidate's cost is O(number of legs) from the prefix sums.
4. Per piece, an argmin over its candidates. Ties are broken by candidate
   index, never by arrival order.
5. Download the chosen candidate indices. The CPU commits them, updates
   usage, and moves to the next color.

No atomics on the result path, and every thread writes its own slot.
Determinism therefore does not depend on scheduling.

## Numeric contract (the part that makes bit-identity possible)

- **No doubles in the kernel.** Apple GPUs have no FP64. Edge costs are
  **fixed-point integers**: 64-bit, Q32.32 or a similar scaled format,
  saturating at a stated ceiling, as CLAUDE.md's arithmetic-edge-case rules
  require.
- The present-demand cost (`f + hist * cong^5`) is evaluated **once per
  snapshot on the CPU**, in double, then quantized to that fixed point. The
  GPU only adds and compares integers. Integer addition and comparison are
  exact and order-independent on every backend, which is what makes the
  results bit-identical.
- MSL supports 64-bit integers (`long`/`ulong`) on Apple GPUs, and CUDA
  supports them natively. Both are fine for prefix sums over at most a few
  thousand tiles per row.
- Self-tests check that quantization is monotone: a strictly more
  expensive edge never quantizes cheaper. They also check the saturation
  bound, and zero/negative capacity mapping to the forbidden-price ceiling.

## Code structure

    gr/gpu/
      pattern_kernel.h        // PatternBatch (SoA inputs), PatternResult, IPatternBackend
      pattern_cpu.cpp         // reference backend (plain C++; also used when no GPU)
      pattern_metal.mm/.cpp   // Metal backend via metal-cpp (Apple's header-only C++ wrapper)
      pattern_kernel.metal    // MSL source, embedded as a string, compiled at run time
      pattern_cuda.cu         // CUDA backend
      backend_select.cpp      // probe + GR_GPU override + logging of the chosen backend

- **Structure-of-arrays inputs:** piece endpoints, box bounds and candidate
  counts. They are laid out identically for all three backends, so one
  packing function feeds every backend.
- **Metal shader build.** (Updated 2026-09-28.) Xcode 27.0 and its separately
  downloaded Metal toolchain (`xcodebuild -downloadComponent MetalToolchain`;
  metal 32023.921) are now installed on the M4 Pro and verified: a test kernel
  using 64-bit integers (`device long *`) compiles with `xcrun metal` and links
  with `xcrun metallib`. So the build compiles `pattern_kernel.metal` to a
  `.metallib` offline, which catches shader errors at build time and skips
  compilation at startup. The embedded-source runtime compile
  (`MTLDevice::newLibrary(source, ...)`, needing only `Metal.framework`) stays
  as the fallback for machines with only the Command Line Tools, and CMake
  chooses between them by probing for `xcrun -f metal`.
- **CUDA without a hard runtime dependency.** Link `cudart_static`. On a
  machine with no NVIDIA driver, `cudaGetDeviceCount` returns an error,
  which the probe treats as "no CUDA", and the run proceeds on CPU. The
  same Linux binary then runs on GPU and non-GPU hosts.
- A given build contains at most **{CPU, Metal}** (macOS) or **{CPU, CUDA}**
  (Linux). "Self-determining" means probing the backends compiled into that
  binary. There is no single binary holding both GPU kinds, because they
  are different OSes.

## Build

- **CMake** for anything that touches a GPU: options `GR_WITH_METAL`
  (default ON on Apple) and `GR_WITH_CUDA` (default ON when
  `check_language(CUDA)` finds `nvcc`). `metal-cpp` is vendored (a header
  drop, Apple's license) under `third_party/metal-cpp`.
- Every existing router keeps building with the one-line `g++ -std=c++17
  -O2 -Wall -Wextra` command. The GPU code lives only in new files and new
  routers (a new species or generation, per the naming rule), and the CPU
  backend is plain C++17, so the standalone CPU build never needs CMake or
  a GPU SDK.

## CI

Two layers, because GitHub-hosted runners have no GPUs, and this account's
Actions minutes are exhausted until 2026-10-01 anyway:

1. **Every push** (once CI runs again): the existing matrix, plus a
   **CUDA compile-only job** in an `nvidia/cuda:*-devel` container. It needs
   no GPU and catches build breaks in `pattern_cuda.cu`. The CPU backend's
   self-tests run as usual. The Metal backend is covered by a `macos-14`
   compile job; GitHub's macOS runners have Apple GPUs, so its self-tests
   (CPU-vs-Metal bit-identity) can run there too, at macOS runner rates.
2. **On demand, on a cloud GPU**: a separate workflow (`gpu.yml`,
   `workflow_dispatch` only, `runs-on: [self-hosted, linux, gpu]`)
   targeting a self-hosted runner on a rented NVIDIA VM, started only for
   the run. It builds with CUDA, runs the CUDA-vs-CPU bit-identity
   self-tests, and runs the benchmark batch. Cost is per hour of VM time,
   and nothing runs automatically.

## Self-tests (written before each backend exists, per CLAUDE.md)

- **CPU reference:** hand-checked L/Z costs from prefix sums on small
  grids; the argmin tie-break takes the lowest candidate index; a
  forbidden edge (capacity 0) is never chosen when an alternative exists;
  quantization is monotone and saturates.
- **Each GPU backend:**
  - **bit-identity with CPU** on (a) the hand fixtures, (b) hash-priced
    random batches (thousands of pieces, varied boxes), and (c) one real
    batch per ISPD benchmark captured from a CPU run;
  - the backend-selection logic under forced `GR_GPU` values, including
    loud failure when the requested backend is absent.
- **End to end:** a router run with `GR_GPU=cpu` and with the GPU backend
  on the same benchmark gives `cmp`-identical route files.

## Phases and exit criteria

| phase | work | exit criterion |
|---|---|---|
| A | CPU pattern stage (roadmap step 1): fixed-point costs, color batches, `IPatternBackend` + CPU backend | ISPD 2008 results legal, `eval2008.pl`-agreed, runtime down vs `pd9route`; overflow no worse |
| B | Metal backend, run-time kernel compile, backend probe, `GR_GPU` | bit-identical to CPU on all self-tests and on one real batch per benchmark; measured speedup per batch on the M4 Pro |
| C | CUDA backend (compile-only CI job) | builds clean in the CUDA container |
| D | Cloud GPU runner + `gpu.yml` | CUDA bit-identical to CPU on the cloud GPU; ISPD 2024 benchmark timings recorded |
| E | Tuning for ISPD 2024 scale (batch sizes, transfer overlap, memory layout) | contest-style runs within the published runtime caps |

## Risks

- **Transfer overhead dominates at ISPD 2008 scale** (~200K nets). Expect
  the GPU to pay only at millions of nets. Batch speedups are measured, not
  assumed.
- **Color classes may be small in dense regions,** too small to fill a GPU.
  Mitigations: batch by region tiles, or relax to "disjoint within a batch"
  pieces rather than whole nets.
- **Present-demand costs change after every commit.** Freezing a snapshot
  per color batch is a semantic change from the sequential router. It is
  validated on quality (same-budget comparisons against the sequential
  CPU router), not assumed harmless.
- Apple GPU limits (no FP64, threadgroup memory size) are covered by the
  integer contract; they only affect speed, not results.
