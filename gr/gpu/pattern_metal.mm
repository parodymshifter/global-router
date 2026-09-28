// pattern_metal.mm -- Metal backend of the batched pattern-routing kernel
// (contract: pattern_kernel.h; plan: GPU_BACKEND_PLAN.md phase B).
//
// Objective-C++ against the system Metal framework (the plan named
// metal-cpp; Objective-C++ needs no vendored download and keeps the same
// C++ interface, IPatternBackend). The kernel library is loaded from the
// offline-compiled pattern_kernel.metallib next to the executable (or
// $GR_METALLIB) when present, else compiled at run time from the embedded
// source -- which needs only Metal.framework, so a Command-Line-Tools-only
// machine still runs it. All buffers use shared storage: on Apple silicon
// CPU and GPU see the same memory, so nothing is copied.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>

#include "pattern_kernel.h"

namespace gpu {

static const char *kMsl =
#include "pattern_kernel_src.inc"
    ;

struct MetalDims {
  uint32_t nx, ny, n;
};

class MetalPatternBackend : public IPatternBackend {
  id<MTLDevice> dev_ = nil;
  id<MTLCommandQueue> queue_ = nil;
  id<MTLComputePipelineState> row_ = nil, col_ = nil, best_ = nil;
  std::string how_;

  id<MTLBuffer> buf(const void *data, size_t bytes) {
    size_t n = bytes ? bytes : 8; // Metal rejects zero-length buffers
    return data && bytes ? [dev_ newBufferWithBytes:data length:n options:MTLResourceStorageModeShared]
                         : [dev_ newBufferWithLength:n options:MTLResourceStorageModeShared];
  }

public:
  bool init(std::string &err) {
    dev_ = MTLCreateSystemDefaultDevice();
    if (!dev_) {
      err = "no Metal device";
      return false;
    }
    NSError *e = nil;
    id<MTLLibrary> lib = nil;
    std::string libPath;
    if (const char *env = std::getenv("GR_METALLIB"); env && *env) {
      libPath = env;
    } else {
      char exe[PATH_MAX];
      uint32_t sz = sizeof(exe);
      if (_NSGetExecutablePath(exe, &sz) == 0) {
        std::string p(exe);
        libPath = p.substr(0, p.find_last_of('/') + 1) + "pattern_kernel.metallib";
      }
    }
    if (!libPath.empty() && [[NSFileManager defaultManager] fileExistsAtPath:@(libPath.c_str())]) {
      lib = [dev_ newLibraryWithURL:[NSURL fileURLWithPath:@(libPath.c_str())] error:&e];
      if (lib)
        how_ = "offline metallib";
    }
    if (!lib) {
      e = nil;
      lib = [dev_ newLibraryWithSource:@(kMsl) options:nil error:&e];
      if (!lib) {
        err = std::string("Metal kernel compile failed: ") + (e ? e.localizedDescription.UTF8String : "?");
        return false;
      }
      how_ = "runtime-compiled source";
    }
    auto pipe = [&](const char *fn) -> id<MTLComputePipelineState> {
      id<MTLFunction> f = [lib newFunctionWithName:@(fn)];
      if (!f)
        return nil;
      NSError *pe = nil;
      return [dev_ newComputePipelineStateWithFunction:f error:&pe];
    };
    row_ = pipe("rowPrefix");
    col_ = pipe("colPrefix");
    best_ = pipe("bestPattern");
    queue_ = [dev_ newCommandQueue];
    if (!row_ || !col_ || !best_ || !queue_) {
      err = "Metal pipeline setup failed";
      return false;
    }
    return true;
  }

  std::string name() const override {
    return std::string("metal (") + dev_.name.UTF8String + ", " + how_ + ")";
  }

  void solve(const PatternGrid &g, const PatternBatch &b, PatternResult &r) override {
    @autoreleasepool {
      size_t n = b.size();
      size_t cells = static_cast<size_t>(g.nx) * g.ny;
      MetalDims dims{static_cast<uint32_t>(g.nx), static_cast<uint32_t>(g.ny), static_cast<uint32_t>(n)};
      id<MTLBuffer> h = buf(g.h.data(), g.h.size() * sizeof(int64_t));
      id<MTLBuffer> v = buf(g.v.data(), g.v.size() * sizeof(int64_t));
      id<MTLBuffer> rowP = buf(nullptr, cells * sizeof(int64_t));
      id<MTLBuffer> colP = buf(nullptr, cells * sizeof(int64_t));
      id<MTLBuffer> x1 = buf(b.x1.data(), n * sizeof(int32_t)), y1 = buf(b.y1.data(), n * sizeof(int32_t));
      id<MTLBuffer> x2 = buf(b.x2.data(), n * sizeof(int32_t)), y2 = buf(b.y2.data(), n * sizeof(int32_t));
      id<MTLBuffer> ob = buf(nullptr, n * sizeof(int32_t)), oc = buf(nullptr, n * sizeof(int64_t));

      id<MTLCommandBuffer> cb = [queue_ commandBuffer];
      auto groups = [](id<MTLComputePipelineState> p, size_t count) {
        NSUInteger w = std::min<NSUInteger>(p.maxTotalThreadsPerThreadgroup, 256);
        return std::make_pair(MTLSizeMake((count + w - 1) / w, 1, 1), MTLSizeMake(w, 1, 1));
      };
      // Pass 1: prefix sums (rows and columns are independent).
      id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
      [enc setComputePipelineState:row_];
      [enc setBuffer:h offset:0 atIndex:0];
      [enc setBuffer:rowP offset:0 atIndex:1];
      [enc setBytes:&dims length:sizeof(dims) atIndex:2];
      auto gr = groups(row_, g.ny);
      [enc dispatchThreadgroups:gr.first threadsPerThreadgroup:gr.second];
      [enc setComputePipelineState:col_];
      [enc setBuffer:v offset:0 atIndex:0];
      [enc setBuffer:colP offset:0 atIndex:1];
      [enc setBytes:&dims length:sizeof(dims) atIndex:2];
      auto gc = groups(col_, g.nx);
      [enc dispatchThreadgroups:gc.first threadsPerThreadgroup:gc.second];
      [enc endEncoding];
      // Pass 2 (a separate encoder: the prefix sums are complete and
      // visible before it starts): one thread per piece.
      if (n > 0) {
        enc = [cb computeCommandEncoder];
        [enc setComputePipelineState:best_];
        [enc setBuffer:rowP offset:0 atIndex:0];
        [enc setBuffer:colP offset:0 atIndex:1];
        [enc setBuffer:x1 offset:0 atIndex:2];
        [enc setBuffer:y1 offset:0 atIndex:3];
        [enc setBuffer:x2 offset:0 atIndex:4];
        [enc setBuffer:y2 offset:0 atIndex:5];
        [enc setBuffer:ob offset:0 atIndex:6];
        [enc setBuffer:oc offset:0 atIndex:7];
        [enc setBytes:&dims length:sizeof(dims) atIndex:8];
        auto gb = groups(best_, n);
        [enc dispatchThreadgroups:gb.first threadsPerThreadgroup:gb.second];
        [enc endEncoding];
      }
      [cb commit];
      [cb waitUntilCompleted];
      r.best.resize(n);
      r.cost.resize(n);
      if (n > 0) {
        std::memcpy(r.best.data(), ob.contents, n * sizeof(int32_t));
        std::memcpy(r.cost.data(), oc.contents, n * sizeof(int64_t));
      }
    }
  }
};

IPatternBackend *makeMetalPatternBackend(std::string &err) {
  static MetalPatternBackend backend;
  static std::string initErr;
  static bool ok = backend.init(initErr);
  if (!ok) {
    err = initErr;
    return nullptr;
  }
  return &backend;
}

} // namespace gpu
