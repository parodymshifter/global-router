// pd39_settings.h -- the table of every setting pd39route reads, and the checks built on it.
//
// HISTORY_COST_SWEEP.md Part 215, 2026-11-24 (R2 of recaps/review-2026-11-24.html, the user's go-ahead). Before it the
// router read 188 environment settings under 35 name prefixes with no list of them, so
//   - a misspelled name (PD39ROUTE_SKIP_GAINT=1) was ignored and the run went on with the default (review F4), and
//   - 31 settings read their value with atoi / atof, so a mistyped value (abc, 0,1) became 0 or a truncated number
//     (review F9); several of those are read lazily, the first time their function runs, hours into a route.
// Now:
//   - every read goes through settings::get(name), which stops the run if the name is not in kTable (a router change
//     that adds a setting must add it here; gr/settings_check.py checks the table against the source in CI);
//   - checkEnvironment(), at the start of every routing run, stops the run on any variable whose name has one of the
//     table's prefixes (PD39ROUTE_, VROUTE_, GR_, ...) but is not in the table, naming the nearest known setting;
//   - it also parses, once at start-up, the value of every Int and Real setting that is set, strictly (the whole value
//     a number, surrounding spaces allowed, finite), and the read sites use intValue / realValue, which do the same;
//   - `pd39route --settings` prints the table, and which settings the current environment sets.
// The kinds Int and Real mark the settings whose value nothing else checks; every other kind is documentation (Number,
// Flag, List and Text settings are checked by their own parsers at their read). Defaults stay at the read sites.
// Valid values parse exactly as atoi / atof parsed them, so routes are unchanged (checked byte for byte, Part 215).
#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

#include "gr_io.h"

extern char **environ;

namespace settings {

enum class Kind {
  Flag,    // 0 or 1, checked at start-up (Part 216; before it several took anything but 0 as on)
  OnIfSet, // on when set; its value must be 1, checked at start-up (Part 216; before it any value, even 0, was on)
  Int,     // a whole number, checked here
  Real,    // a number, checked here
  Number,  // a number checked by its own parser at its read
  List,    // several values, checked by its own parser
  Text,    // a word from a set: its `allowed` words checked at start-up, or its own parser at its read
  File     // a path
};

struct Info {
  const char *name;
  Kind kind;
  const char *meaning;
  const char *allowed = nullptr; // Part 216: a Text setting's words, '|'-separated, checked at start-up
};

// Sorted by name in byte order (strcmp), for find(); the self-test checks the order.
inline constexpr Info kTable[] = {
    {"FR1ROUTE_TWOD_ONLY", Kind::OnIfSet, "single-layer 2D output with no vias (fr1route's; a self-test reference)"},
    {"FR3ROUTE_STEINERIZE", Kind::Flag, "0: plain MST topology, not the Steinerized one (fr3route; 0 = off, else on)"},
    {"FR4ROUTE_EXACT_MAX_PINS", Kind::Int, "largest net (pins) given the exact Steiner solve (fr4route)"},
    {"FR4ROUTE_EXACT_STEINER", Kind::Flag, "0: no exact Dreyfus-Wagner Steiner solve (fr4route; 0 = off, else on)"},
    {"GR_GPU", Kind::Text, "pattern-stage backend auto, cpu, metal or cuda (GPU builds only; the g++ build is CPU)"},
    {"GR_METALLIB", Kind::File, "a precompiled Metal library (Metal builds only)"},
    {"GR_SIGNPOSTS", Kind::Flag, "Instruments stage markers (signpost.h, Part 126)"},
    {"NV11ROUTE_BOX_CAP", Kind::Number, "slow-tier search box: largest margin (nv11route box growth, Parts 51-57)"},
    {"NV11ROUTE_BOX_GROW", Kind::Number, "slow-tier search box: margin added per round"},
    {"NV11ROUTE_BOX_INIT", Kind::Number, "slow-tier search box: first margin"},
    {"NV11ROUTE_BOX_MODE", Kind::Text, "slow-tier search box growth mode (e.g. round)"},
    {"NV2ROUTE_HIST_EXPONENT_ESCALATION", Kind::Real, "via history: exponent added per round in ratio mode"},
    {"NV2ROUTE_HIST_FACTOR", Kind::Real, "via history: factor in mult mode"},
    {"NV2ROUTE_HIST_MODE", Kind::Text, "via history: mult or ratio (add: additive, the default)", "add|mult|ratio"},
    {"NV2ROUTE_HIST_RATIO_EXPONENT", Kind::Real, "via history: exponent in ratio mode"},
    {"NV2ROUTE_HIST_RELAX", Kind::Flag, "via history: 0 = permanent, no LIFO relaxation (0 or 1)"},
    {"NV2ROUTE_HIST_SCALE", Kind::Text, "via history: overflow: scale each step by the overflow (off: the default)", "off|overflow"},
    {"NV2ROUTE_HIST_STEP", Kind::Real, "via history: additive step"},
    {"NV2ROUTE_HIST_STREAK", Kind::Int, "via history: over-capacity rounds in a row before it grows"},
    {"NV2ROUTE_TRACE", Kind::OnIfSet, "per-round trace of via and wire pricing (nv2route)"},
    {"NV2ROUTE_VIA_AWARE", Kind::Flag, "0: via pricing off, nv1route's exact output (0 or 1)"},
    {"NV2ROUTE_VIA_PATIENCE", Kind::Int, "via negotiation: rounds without gain before it stops"},
    {"NV2ROUTE_VIA_ROUNDS", Kind::Int, "via negotiation: most rounds"},
    {"NV2ROUTE_VIA_WEIGHT", Kind::Real, "via cost weight"},
    {"NV2ROUTE_WIRE_AWARE", Kind::Flag, "0: wire-history pricing off (0 or 1)"},
    {"NV2ROUTE_WIRE_HIST_EXPONENT_ESCALATION", Kind::Real, "wire history: exponent added per round in ratio mode"},
    {"NV2ROUTE_WIRE_HIST_FACTOR", Kind::Real, "wire history: factor in mult mode"},
    {"NV2ROUTE_WIRE_HIST_MODE", Kind::Text, "wire history: mult or ratio (add: additive, the default)", "add|mult|ratio"},
    {"NV2ROUTE_WIRE_HIST_RATIO_EXPONENT", Kind::Real, "wire history: exponent in ratio mode"},
    {"NV2ROUTE_WIRE_HIST_RELAX", Kind::Flag, "wire history: 0 = permanent, no LIFO relaxation (0 or 1)"},
    {"NV2ROUTE_WIRE_HIST_SCALE", Kind::Text, "wire history: overflow: scale each step by the overflow (off: the default)", "off|overflow"},
    {"NV2ROUTE_WIRE_HIST_STEP", Kind::Real, "wire history: additive step"},
    {"NV2ROUTE_WIRE_HIST_STREAK", Kind::Int, "wire history: over-capacity rounds in a row before it grows"},
    {"NV3ROUTE_GIANT_FAST_TIER", Kind::Flag, "0: giant nets go through the slow tier too (nv3route; 0 = off, else on)"},
    {"PD10ROUTE_PATTERN_FIRST", Kind::Flag, "pattern route first when a piece is rerouted (default 1)"},
    {"PD11ROUTE_PATTERN", Kind::Flag, "the batched pattern stage (pd11route)"},
    {"PD11ROUTE_PATTERN_CELL", Kind::Number, "batched pattern stage: tile side its batches are dealt over"},
    {"PD11ROUTE_PATTERN_ROUNDS", Kind::Number, "batched pattern stage: rounds"},
    {"PD12ROUTE_PLATEAU_GAIN", Kind::Number, "plateau stop: least gain (% of the best N rounds earlier) that counts"},
    {"PD12ROUTE_PLATEAU_MIN", Kind::Number, "plateau stop: acts only at or above this overflow"},
    {"PD12ROUTE_REFINE_PLATEAU", Kind::Number, "refinement's plateau stop: window in rounds"},
    {"PD12ROUTE_REGION_PLATEAU", Kind::Number, "region stage's plateau stop: window in rounds"},
    {"PD13ROUTE_REFINE_BOX_CAP", Kind::Number, "refinement: largest search margin (Part 76)"},
    {"PD13ROUTE_REGION_BOX_CAP", Kind::Number, "region stage: largest search margin (Part 76)"},
    {"PD14ROUTE_REGION_COST_POPS", Kind::Number, "region stage's work-gated stop: search expansions (Part 80)"},
    {"PD14ROUTE_REGION_COST_WINDOW", Kind::Number, "region stage's work-gated stop: window in rounds (Part 80)"},
    {"PD15ROUTE_DETOUR_TRACE", Kind::Text, "1: per-stage detour trace (Part 82, trace only)", "0|1"},
    {"PD16ROUTE_RECLAIM", Kind::Flag, "the detour reclaim pass (Parts 82-88)"},
    {"PD16ROUTE_RECLAIM_MARGIN", Kind::Number, "reclaim: search margin"},
    {"PD16ROUTE_RECLAIM_MIN_EXCESS", Kind::Number, "reclaim: least detour excess for a net to qualify"},
    {"PD16ROUTE_RECLAIM_MU", Kind::Number, "reclaim: overflow price multiplier"},
    {"PD16ROUTE_RECLAIM_PASSES", Kind::Number, "reclaim: passes"},
    {"PD16ROUTE_RECLAIM_RATIO", Kind::Number, "reclaim: least detour ratio for a net to qualify"},
    {"PD17ROUTE_RECLAIM_AVOID", Kind::Text, "reclaim: what its reroutes avoid (pd17route)"},
    {"PD18ROUTE_RECLAIM_AT_STOP", Kind::Flag, "1: run the reclaim when the slow tier first reaches PD9ROUTE_STOP_AT"},
    {"PD18ROUTE_RECLAIM_EVERY", Kind::Number, "also reclaim after every K-th slow round"},
    {"PD19ROUTE_RECLAIM_FALLBACK", Kind::Flag, "the reclaim with a fallback (Part 88)"},
    {"PD1ROUTE_COST", Kind::Text, "the 2D pricing model (present-demand cost, Parts 59-60)"},
    {"PD1ROUTE_WL_FLOOR", Kind::Number, "2D pricing: wirelength floor"},
    {"PD20ROUTE_FALLBACK_ORDER", Kind::Number, "order of the fallback's paths"},
    {"PD21ROUTE_FALLBACK_A_MAX", Kind::Number, "path A overflow at most this for the reclaim path to run (Part 88)"},
    {"PD22ROUTE_FALLBACK_PARALLEL", Kind::Flag, "the fallback's paths on two threads (Part 90)"},
    {"PD22ROUTE_VCAP", Kind::Number, "virtual capacity: tracks taken off near the starting overflow (Part 89)"},
    {"PD22ROUTE_VCAP_RADIUS", Kind::Number, "virtual capacity: radius around the overflow"},
    {"PD23ROUTE_VCAP_B", Kind::Flag, "virtual capacity on fallback path B (0 or 1; unset 1)"},
    {"PD2ROUTE_ACCEPT", Kind::Text, "slow-tier acceptance rule (keep every reroute or revert, Parts 59-60)"},
    {"PD33ROUTE_AP", Kind::Text, "ISPD 2024 access points: first (default) or InstantGR's rule"},
    {"PD33ROUTE_LA_COST", Kind::Text, "ISPD 2024 layer assignment's cost (score: the 2024 score)"},
    {"PD33ROUTE_VIA_DEMAND", Kind::Flag, "ISPD 2024: via demand on wire capacity"},
    {"PD34ROUTE_2D_COST", Kind::Text, "ISPD 2024: the 2D searches' cost, pd (default) or score"},
    {"PD34ROUTE_2D_HIST", Kind::Number, "ISPD 2024: 0 = history stays 1 under the score cost"},
    {"PD35ROUTE_LOWMEM", Kind::Text, "low-memory mode: auto (default), 0 or 1"},
    {"PD35ROUTE_MEMTRACE", Kind::Text, "memory trace (diagnostic only)", "0|1"},
    {"PD35ROUTE_PATTERN3D", Kind::Number, "3D pattern passes after layer assignment (needs PD33ROUTE_LA_COST=score)"},
    {"PD37ROUTE_BATCH_TRACE", Kind::OnIfSet, "pattern batches' dependency trace (diagnostic)"},
    {"PD37ROUTE_CELL", Kind::Number, "pattern stage: tile side of its dependency cells"},
    {"PD37ROUTE_THREADS", Kind::Number, "threads, 1-256"},
    {"PD38ROUTE_FRAMES", Kind::File, "folder for congestion frames (trace only)"},
    {"PD38ROUTE_FRAME_SIDE", Kind::Int, "congestion frames: image side, at least 16"},
    {"PD38ROUTE_FRAME_SPLIT", Kind::Int, "congestion frames: split, at least 1"},
    {"PD38ROUTE_P3D_REUSE", Kind::Flag, "3D passes share one dependency graph (Part 120)"},
    {"PD38ROUTE_P3D_SKIP", Kind::Flag, "skip the 3D passes"},
    {"PD38ROUTE_PATTERN_DAG", Kind::Flag, "pattern stage's batches threaded on the dependency graph (Part 120)"},
    {"PD38ROUTE_REGION_COST_REL", Kind::Number, "region stage's work-gated stop scaled to the design (0 = off)"},
    {"PD38ROUTE_SLOW_BATCH", Kind::Number, "slow tier: nets per parallel batch (Part 118)"},
    {"PD38ROUTE_SLOW_CONFLICT", Kind::Flag, "slow tier: 0 = no conflict rule, every net commits as searched"},
    {"PD38ROUTE_SLOW_DAG", Kind::Flag, "slow tier on a dependency graph"},
    {"PD38ROUTE_SLOW_LPT", Kind::Flag, "slow tier: biggest search box first (1, default) or net order (0)"},
    {"PD38ROUTE_SLOW_SPREAD", Kind::Number, "slow tier: batches dealt round-robin over tiles of this side (Part 119)"},
    {"PD39ROUTE_FORCED_REPORT", Kind::Text, "0: no forced-overflow report (Part 162)"},
    {"PD39ROUTE_FORCED_REPORT_HOTSPOTS", Kind::Int, "forced-overflow report: hotspots"},
    {"PD39ROUTE_FORCED_REPORT_SIDE", Kind::Int, "forced-overflow report: hotspot side in tiles"},
    {"PD39ROUTE_LAYER_C", Kind::List, "ISPD 2025: capacitance per layer"},
    {"PD39ROUTE_LAYER_R", Kind::List, "ISPD 2025: resistance per layer"},
    {"PD39ROUTE_LA_CELL", Kind::Number, "layer assignment: tile side of its cells (Part 129)"},
    {"PD39ROUTE_LA_CRIT", Kind::Flag, "layer assignment: critical nets first (Part 132)"},
    {"PD39ROUTE_NET_TIMES", Kind::File, "per-net time, in the input's net order (Part 131)"},
    {"PD39ROUTE_PORTFOLIO", Kind::List, "the late-stage portfolio's variants, separated by ; (Parts 135, 172-185)"},
    {"PD39ROUTE_PORTFOLIO_DELAY", Kind::Number, "portfolio variants wait until path A has run this many seconds (Part 170)"},
    {"PD39ROUTE_PORTFOLIO_WHEN", Kind::Text, "portfolio: after, always or speculative (Part 168)"},
    {"PD39ROUTE_POWER_LAYERS", Kind::Number, "ISPD 2025: power term's strength (Part 133)"},
    {"PD39ROUTE_PRICE_SEED", Kind::List, "prices seeded from a file: file[,scale] (Part 210)"},
    {"PD39ROUTE_REFINE_ANNEAL", Kind::List, "refinement annealing: T0,alpha (Part 147)"},
    {"PD39ROUTE_REFINE_EVICT", Kind::List, "refinement eviction: nets[,margin[,blockers[,passes]]] (Part 191)"},
    {"PD39ROUTE_REFINE_KEEP_ABOVE", Kind::Number, "refinement keeps every reroute above this overflow (Part 213)"},
    {"PD39ROUTE_REFINE_SHIFT", Kind::List, "refinement shift: k[,radius] (Part 152)"},
    {"PD39ROUTE_REFINE_STOP", Kind::Number, "refinement below 1000 stops after this many iterations without gain (Part 168)"},
    {"PD39ROUTE_REGION_A1", Kind::List, "region stage trace rounds r1,r2,... (Part 157, trace only)"},
    {"PD39ROUTE_REGION_ABORT", Kind::List, "region stage's early stop: above,rounds (Part 146)"},
    {"PD39ROUTE_REGION_BATCH", Kind::List, "batched region stage: B[,spread] (Part 159)"},
    {"PD39ROUTE_REGION_BATCH_ADAPT", Kind::Number, "batched region stage: adaptive batches (Part 161)"},
    {"PD39ROUTE_REGION_BATCH_FALLBACK", Kind::Number, "batched region stage that never beat its start reruns sequentially (Part 167)"},
    {"PD39ROUTE_REGION_BATCH_MAX", Kind::Number, "batched region stage only at or below this entering overflow (Part 165)"},
    {"PD39ROUTE_REGION_BATCH_ROUNDS", Kind::Number, "batched region stage: rounds (Part 163)"},
    {"PD39ROUTE_REGION_BATCH_UNTIL", Kind::Number, "batched region stage until this round (Part 160)"},
    {"PD39ROUTE_REGION_BOX_CAP_B", Kind::Number, "region stage's margin cap on the reclaim path (Part 141)"},
    {"PD39ROUTE_REGION_BOX_EVIDENCE", Kind::Number, "region stage: box growth evidence (Part 138)"},
    {"PD39ROUTE_REGION_COOLDOWN", Kind::Number, "region stage: cooldown (Part 144)"},
    {"PD39ROUTE_REGION_COST_REROUTES", Kind::Number, "region stage's work-gated stop counted in reroutes (Part 199)"},
    {"PD39ROUTE_REGION_DAMP", Kind::Number, "region stage: damping (Part 144)"},
    {"PD39ROUTE_REGION_DUMP", Kind::Text, "region stage dump (Part 140, trace only)"},
    {"PD39ROUTE_REGION_LAGRANGE", Kind::Number, "region stage's Lagrangian multiplier step (Part 135)"},
    {"PD39ROUTE_REGION_OVER", Kind::Number, "region stage's over-capacity cost: slack (Part 179)"},
    {"PD39ROUTE_REGION_ROUND0", Kind::Text, "region stage's first round: all or over (Part 210)"},
    {"PD39ROUTE_REGION_WALL_H", Kind::List, "region stage wall bound: B[,from] (Part 150)"},
    {"PD39ROUTE_REGION_WL_FLOOR", Kind::Number, "region stage: wirelength floor (Part 138)"},
    {"PD39ROUTE_SEARCH_BIDIR", Kind::Number, "two-ended search (Parts 196-202; 0 = one-ended)"},
    {"PD39ROUTE_SEARCH_DUMP", Kind::List, "search dump: prefix,every,minpops[,all] (Parts 153, 197; trace only)"},
    {"PD39ROUTE_SEARCH_TRACE", Kind::Text, "1: search trace (Part 138, trace only)", "0|1"},
    {"PD39ROUTE_SKIP_GIANT", Kind::Flag, "leave nets over 1000 pins unrouted (ISPD 2008 only, Part 212)"},
    {"PD39ROUTE_SLOW_CUT", Kind::List, "slow tier cut: tracks,radius,below,every,full (Part 173)"},
    {"PD39ROUTE_SLOW_OVER", Kind::List, "slow tier over-capacity cost: slack[,below] (Part 186)"},
    {"PD39ROUTE_SLOW_PLATEAU", Kind::List, "slow tier's plateau stop: window,gain%,floor (Part 148)"},
    {"PD39ROUTE_SLOW_SEARCHES", Kind::List, "slow tier: searches per net K[,below] (Parts 193, 195)"},
    {"PD39ROUTE_SLOW_TASKS", Kind::List, "slow tier task trace: file[,every] (Part 192, trace only)"},
    {"PD39ROUTE_TIMING_BENEFIT", Kind::List, "ISPD 2025 timing benefit: g[,M[,P]] (Part 205)"},
    {"PD39ROUTE_TIMING_GATE", Kind::Flag, "ISPD 2025: timing gate"},
    {"PD39ROUTE_TIMING_KMIN", Kind::Number, "ISPD 2025: timing strength calibration (Part 133)"},
    {"PD39ROUTE_TIMING_LAYERS", Kind::List, "ISPD 2025: timing strengths k1,k2,k3 by class (Part 133)"},
    {"PD39ROUTE_TIMING_MULT", Kind::File, "ISPD 2025: a timing multiplier per net (Part 207)"},
    {"PD39ROUTE_TIMING_REF", Kind::Text, "ISPD 2025: timing reference, e.g. worst (Part 133)"},
    {"PD39ROUTE_TIMING_ROWS", Kind::File, "ISPD 2025: a timing row per net, the Elmore stage (Part 215)"},
    {"PD39ROUTE_TIMING_SCALE", Kind::Text, "ISPD 2025: timing scale, 1 or weights (Part 133)"},
    {"PD39ROUTE_TIMING_SLACK", Kind::File, "ISPD 2025: routed slack per net (Part 134)"},
    {"PD39ROUTE_TIMING_SLACK_MODE", Kind::Text, "ISPD 2025: replace or min, how routed slack meets input slack (Part 134)"},
    {"PD3ROUTE_LA", Kind::Text, "layer assignment mode (pd3route)"},
    {"PD3ROUTE_LA_FROM", Kind::File, "layer assignment only, from an existing route file"},
    {"PD4ROUTE_REFINE", Kind::Flag, "the refinement stage (pd4route)"},
    {"PD4ROUTE_REFINE_ACCEPT", Kind::Text, "refinement acceptance: keep or revert (Part 63b)"},
    {"PD4ROUTE_REFINE_BOX_INC", Kind::Number, "refinement: margin added per iteration"},
    {"PD4ROUTE_REFINE_BOX_INIT", Kind::Number, "refinement: first margin"},
    {"PD4ROUTE_REFINE_ITERS", Kind::Number, "refinement: iterations"},
    {"PD5ROUTE_REFINE_ORDER", Kind::Text, "refinement order (shuffle: seeded random)"},
    {"PD5ROUTE_REGION", Kind::Flag, "the region (hotspot rip-up) stage (pd5route)"},
    {"PD5ROUTE_REGION_BOX_GROW", Kind::Number, "region stage: margin growth"},
    {"PD5ROUTE_REGION_BOX_INIT", Kind::Number, "region stage: first margin"},
    {"PD5ROUTE_REGION_EXPAND", Kind::Number, "region stage: hotspot expansion"},
    {"PD5ROUTE_REGION_ROUNDS", Kind::Number, "region stage: rounds"},
    {"PD5ROUTE_SEED", Kind::Number, "seed for the shuffled refinement order"},
    {"PD6ROUTE_PIECES", Kind::Flag, "1: both late stages rip up 2-pin pieces (pd6route)"},
    {"PD7ROUTE_LA_PINS", Kind::Flag, "pin-aware layer assignment (default 1; 0 = pd6route's)"},
    {"PD7ROUTE_WL", Kind::Flag, "wirelength and bend recovery (pd7route)"},
    {"PD7ROUTE_WL_BEND", Kind::Number, "wirelength recovery: bend cost"},
    {"PD7ROUTE_WL_MARGIN", Kind::Number, "wirelength recovery: search margin"},
    {"PD7ROUTE_WL_PASSES", Kind::Number, "wirelength recovery: passes"},
    {"PD8ROUTE_REBUILD", Kind::Flag, "whole-net tree rebuild (pd8route)"},
    {"PD8ROUTE_REBUILD_MARGIN", Kind::Number, "tree rebuild: search margin"},
    {"PD8ROUTE_REBUILD_MAX_PINS", Kind::Number, "tree rebuild: largest net (pins)"},
    {"PD9ROUTE_SLOW_PIECES", Kind::Flag, "1: the slow tier reroutes only overflowed pieces (pd9route)"},
    {"PD9ROUTE_STOP_AT", Kind::Number, "slow tier stops at this overflow"},
    {"VROUTE_CLEANUP_PASSES", Kind::Int, "post-convergence cleanup: passes"},
    {"VROUTE_CLEANUP_THRESHOLD", Kind::Real, "post-convergence cleanup: relaxed pricing threshold"},
    {"VROUTE_EPS", Kind::Real, "price step epsilon"},
    {"VROUTE_FAST_ROUNDS", Kind::Int, "cheap tier: most rounds"},
    {"VROUTE_HIST_EXPONENT_ESCALATION", Kind::Real, "history: exponent added per round in ratio mode"},
    {"VROUTE_HIST_FACTOR", Kind::Real, "history: factor in mult mode"},
    {"VROUTE_HIST_MODE", Kind::Text, "history: mult or ratio (add: additive, the default)", "add|mult|ratio"},
    {"VROUTE_HIST_RATIO_EXPONENT", Kind::Real, "history: exponent in ratio mode"},
    {"VROUTE_HIST_RELAX", Kind::Flag, "history: 0 = permanent, no LIFO relaxation (0 or 1)"},
    {"VROUTE_HIST_SCALE", Kind::Text, "history: overflow: scale each step by the overflow (off: the default)", "off|overflow"},
    {"VROUTE_HIST_STEP", Kind::Real, "history: additive step"},
    {"VROUTE_HIST_STREAK", Kind::Int, "history: over-capacity rounds in a row before it grows"},
    {"VROUTE_MAX_MARGIN", Kind::Int, "largest search margin (-1: nv4route's rule)"},
    {"VROUTE_MSMV", Kind::Flag, "0: fixed-topology slow tier instead of MSMV maze growth (0 or 1)"},
    {"VROUTE_POWER", Kind::Real, "price curve exponent"},
    {"VROUTE_SLOW_ROUNDS", Kind::Int, "slow tier: most rounds"},
    {"VROUTE_THRESHOLD", Kind::Real, "per-round pricing threshold"},
    {"VROUTE_TRACE", Kind::OnIfSet, "per-round trace"},
};
inline constexpr size_t kCount = sizeof(kTable) / sizeof(kTable[0]);

inline const char *kindName(Kind k) {
  switch (k) {
  case Kind::Flag: return "flag";
  case Kind::OnIfSet: return "on-if-set";
  case Kind::Int: return "int";
  case Kind::Real: return "real";
  case Kind::Number: return "number";
  case Kind::List: return "list";
  case Kind::Text: return "text";
  case Kind::File: return "file";
  }
  return "?";
}

// The table entry for a name (the part of "NAME=value" before '=' when len is given), or nullptr.
inline const Info *find(const char *name, size_t len = std::string::npos) {
  if (len == std::string::npos)
    len = std::strlen(name);
  size_t lo = 0, hi = kCount;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    int c = std::strncmp(kTable[mid].name, name, len);
    if (c == 0 && kTable[mid].name[len] != '\0')
      c = 1; // the table's name is longer: it sorts after
    if (c == 0)
      return &kTable[mid];
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return nullptr;
}

// A setting's value as the environment gives it (nullptr when unset). Reading a name the table does not have is a
// router bug, not an input problem: it stops the run at once (so a new setting cannot skip the table).
inline const char *get(const char *name) {
  if (!find(name))
    gr_io::fail(std::string("internal: setting ") + name + " is read but not listed in pd39_settings.h");
  return std::getenv(name);
}

// The prefix a name is grouped under: up to and including the first "ROUTE_", or "GR_"; empty if neither.
inline std::string prefixOf(const char *name, size_t len) {
  const std::string s(name, len);
  if (s.compare(0, 3, "GR_") == 0)
    return "GR_";
  const size_t p = s.find("ROUTE_");
  return p == std::string::npos ? std::string() : s.substr(0, p + 6);
}

inline bool knownPrefix(const std::string &prefix) {
  if (prefix.empty())
    return false;
  for (const Info &i : kTable)
    if (std::strncmp(i.name, prefix.c_str(), prefix.size()) == 0)
      return true;
  return false;
}

// Edit distance (insert, delete, replace, swap of neighbours), for the nearest-name suggestion.
inline size_t distance(const std::string &a, const std::string &b) {
  const size_t n = a.size(), m = b.size();
  std::vector<std::vector<size_t>> d(n + 1, std::vector<size_t>(m + 1));
  for (size_t i = 0; i <= n; ++i)
    d[i][0] = i;
  for (size_t j = 0; j <= m; ++j)
    d[0][j] = j;
  for (size_t i = 1; i <= n; ++i)
    for (size_t j = 1; j <= m; ++j) {
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
        d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
    }
  return d[n][m];
}

// The known setting nearest to an unknown name, if it is near enough to be a likely misspelling; else "".
inline std::string nearest(const std::string &name) {
  std::string best;
  size_t bd = std::numeric_limits<size_t>::max();
  for (const Info &i : kTable) {
    const size_t dd = distance(name, i.name);
    if (dd < bd) {
      bd = dd;
      best = i.name;
    }
  }
  return bd <= std::max<size_t>(2, name.size() / 6) ? best : std::string();
}

// Strict parsing: the whole text a number (surrounding spaces and tabs allowed), in range, finite.
inline bool parseInt(const char *s, long long &out) {
  if (!s)
    return false;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return false;
  char *end = nullptr;
  errno = 0;
  const long long v = std::strtoll(s, &end, 10);
  if (end == s || errno == ERANGE)
    return false;
  while (*end == ' ' || *end == '\t')
    ++end;
  if (*end)
    return false;
  out = v;
  return true;
}

inline bool parseReal(const char *s, double &out) {
  if (!s)
    return false;
  while (*s == ' ' || *s == '\t')
    ++s;
  if (!*s)
    return false;
  char *end = nullptr;
  errno = 0;
  const double v = std::strtod(s, &end);
  if (end == s || errno == ERANGE || !std::isfinite(v))
    return false;
  while (*end == ' ' || *end == '\t')
    ++end;
  if (*end)
    return false;
  out = v;
  return true;
}

// A setting's value parsed strictly; a bad value stops the run naming the setting.
inline long long longValue(const char *name, const char *v) {
  long long x = 0;
  if (!parseInt(v, x))
    gr_io::fail(std::string(name) + " must be a whole number (got '" + (v ? v : "") + "')");
  return x;
}

inline int intValue(const char *name, const char *v) {
  const long long x = longValue(name, v);
  if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max())
    gr_io::fail(std::string(name) + " is out of range (got '" + v + "')");
  return static_cast<int>(x);
}

inline double realValue(const char *name, const char *v) {
  double x = 0;
  if (!parseReal(v, x))
    gr_io::fail(std::string(name) + " must be a number (got '" + (v ? v : "") + "')");
  return x;
}

// Every problem with an environment (an envp-style array): unknown names under a known prefix, and Int / Real
// settings whose value does not parse. Empty when all is well.
inline std::vector<std::string> problems(char *const *envp) {
  std::vector<std::string> out;
  for (char *const *e = envp; e && *e; ++e) {
    const char *eq = std::strchr(*e, '=');
    if (!eq)
      continue;
    const size_t len = static_cast<size_t>(eq - *e);
    const Info *i = find(*e, len);
    if (!i) {
      if (knownPrefix(prefixOf(*e, len))) {
        const std::string name(*e, len), near = nearest(name);
        out.push_back("unknown setting " + name + (near.empty() ? std::string() : " (did you mean " + near + "?)"));
      }
      continue;
    }
    const std::string val(eq + 1);
    if (i->kind == Kind::Flag && val != "0" && val != "1")
      out.push_back(std::string(i->name) + " must be 0 or 1 (got '" + val + "')");
    if (i->kind == Kind::OnIfSet && val != "1")
      out.push_back(std::string(i->name) + " is on when set: set it to 1, or leave it unset for off (got '" + val + "')");
    if (i->allowed) {
      bool ok = false;
      std::string words = i->allowed, w;
      for (size_t a = 0, b; a <= words.size(); a = b + 1) {
        b = words.find('|', a);
        b = b == std::string::npos ? words.size() : b;
        ok |= words.compare(a, b - a, val) == 0 && val.size() == b - a;
      }
      if (!ok) {
        std::string list = words;
        for (char &c : list)
          if (c == '|')
            c = '/';
        out.push_back(std::string(i->name) + " must be one of " + list + " (got '" + val + "')");
      }
    }
    long long x = 0;
    double r = 0;
    if (i->kind == Kind::Int && (!parseInt(eq + 1, x) || x < std::numeric_limits<int>::min() ||
                                 x > std::numeric_limits<int>::max()))
      out.push_back(std::string(i->name) + " must be a whole number (got '" + (eq + 1) + "')");
    else if (i->kind == Kind::Real && !parseReal(eq + 1, r))
      out.push_back(std::string(i->name) + " must be a number (got '" + (eq + 1) + "')");
  }
  return out;
}

// At the start of a routing run: stop on any problem, listing them all.
inline void checkEnvironment(char *const *envp = environ) {
  const std::vector<std::string> p = problems(envp);
  if (p.empty())
    return;
  std::string msg = p.size() == 1 ? p[0] : std::to_string(p.size()) + " settings problems:";
  if (p.size() > 1)
    for (const std::string &s : p)
      msg += "\n  " + s;
  gr_io::fail(msg + "\n(pd39route --settings lists every setting)");
}

// `pd39route --settings`: the table, with the value of every setting the environment sets.
inline void print(std::ostream &os, char *const *envp = environ) {
  size_t w = 0;
  for (const Info &i : kTable)
    w = std::max(w, std::strlen(i.name));
  size_t set = 0;
  for (const Info &i : kTable) {
    const char *v = std::getenv(i.name);
    set += v != nullptr;
    os << std::left << std::setw(static_cast<int>(w) + 2) << i.name << std::setw(11) << kindName(i.kind) << i.meaning;
    if (v)
      os << "  [set: " << v << "]";
    os << "\n";
  }
  os << kCount << " settings, " << set << " set in this environment\n";
  for (const std::string &s : problems(envp))
    os << "problem: " << s << "\n";
}

} // namespace settings

