// backend_select.cpp -- chooses the pattern-kernel backend at run time
// (GPU_BACKEND_PLAN.md). GR_GPU=auto (default) takes the first available of
// cuda, metal, cpu among the backends compiled into this binary; GR_GPU=cuda
// or =metal asks for exactly that and is an error if it is unavailable --
// never a silent fallback, since a run on a different backend than asked for
// is the "wrong code still runs" failure this project guards against.

#include <cstdlib>
#include <string>

#include "pattern_kernel.h"

namespace gpu {

#ifdef GR_HAVE_METAL
IPatternBackend *makeMetalPatternBackend(std::string &err);
#endif
#ifdef GR_HAVE_CUDA
IPatternBackend *makeCudaPatternBackend(std::string &err);
#endif

IPatternBackend *selectPatternBackend(std::string &chosen, std::string &err) {
  static CpuPatternBackend cpu;
  const char *env = std::getenv("GR_GPU");
  std::string want = env && *env ? env : "auto";
  if (want != "auto" && want != "cpu" && want != "metal" && want != "cuda") {
    err = "GR_GPU must be auto, cpu, metal or cuda (got '" + want + "')";
    return nullptr;
  }
  auto tryCuda = [&](std::string &why) -> IPatternBackend * {
#ifdef GR_HAVE_CUDA
    return makeCudaPatternBackend(why);
#else
    why = "not compiled into this binary";
    return nullptr;
#endif
  };
  auto tryMetal = [&](std::string &why) -> IPatternBackend * {
#ifdef GR_HAVE_METAL
    return makeMetalPatternBackend(why);
#else
    why = "not compiled into this binary";
    return nullptr;
#endif
  };
  std::string why;
  IPatternBackend *b = nullptr;
  if (want == "cpu") {
    b = &cpu;
  } else if (want == "cuda") {
    if (!(b = tryCuda(why)))
      err = "GR_GPU=cuda requested but CUDA is unavailable: " + why;
  } else if (want == "metal") {
    if (!(b = tryMetal(why)))
      err = "GR_GPU=metal requested but Metal is unavailable: " + why;
  } else {
    std::string w1, w2;
    b = tryCuda(w1);
    if (!b)
      b = tryMetal(w2);
    if (!b)
      b = &cpu;
  }
  if (b)
    chosen = b->name();
  return b;
}

} // namespace gpu
