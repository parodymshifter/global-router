// signpost.h -- Apple Instruments markers for the router's stages and rounds
// (os_signpost, shown on the Points of Interest track; HISTORY_COST_SWEEP.md
// Part 126). gr/instruments_profile.sh records a run with Instruments and
// gr/instruments_report.py turns the trace into a per-stage report: wall
// time, CPU time, threads busy, performance vs efficiency cores, and the
// functions the time went to.
//
// Off unless GR_SIGNPOSTS=1 (one flag test per call when off), and nothing
// at all outside macOS, so the router builds and runs unchanged elsewhere.
// Markers never touch routing state: routes are the same with them on.
//
//   stageBegin()          opens a "stage" interval
//   stageEnd(name)        closes it; the end message carries the stage name
//   event(what, n)        a point event ("slow round", 12)
#pragma once

#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <os/log.h>
#include <os/signpost.h>
#endif

namespace grprof {

inline bool enabled() {
  static const bool on = [] {
    const char *v = std::getenv("GR_SIGNPOSTS");
    return v && std::strcmp(v, "1") == 0;
  }();
  return on;
}

#if defined(__APPLE__)
inline os_log_t log() {
  static os_log_t l = os_log_create("org.globalrouter.router", OS_LOG_CATEGORY_POINTS_OF_INTEREST);
  return l;
}
inline os_signpost_id_t &stageId() {
  static os_signpost_id_t id = OS_SIGNPOST_ID_NULL;
  return id;
}
inline void stageBegin() {
  if (!enabled())
    return;
  stageId() = os_signpost_id_generate(log());
  os_signpost_interval_begin(log(), stageId(), "stage");
}
inline void stageEnd(const char *name) {
  if (!enabled() || stageId() == OS_SIGNPOST_ID_NULL)
    return;
  os_signpost_interval_end(log(), stageId(), "stage", "%{public}s", name);
  stageId() = OS_SIGNPOST_ID_NULL;
}
inline void event(const char *what, long long n) {
  if (!enabled())
    return;
  os_signpost_event_emit(log(), OS_SIGNPOST_ID_EXCLUSIVE, "round", "%{public}s %lld", what, n);
}
#else
inline void stageBegin() {}
inline void stageEnd(const char *) {}
inline void event(const char *, long long) {}
#endif

} // namespace grprof
