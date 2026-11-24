// pd39_run.h -- a routing run, start to finish: the command line, the settings, the inputs, the work (RouterRun, Part 216).
// Part of pd39route.cpp, split out of it in R3 of the 2026-11-24 code review (HISTORY_COST_SWEEP.md Part 216): it is
// included once, at its old place near the end of pd39route.cpp, and is not a standalone header -- it uses the code
// above that point. pd39route still builds with its one-line g++ command; the routes are unchanged.
#pragma once

// ---------------------------------------------------------------------------
// A run, start to finish (HISTORY_COST_SWEEP.md Part 216, 2026-11-24; R4 of recaps/review-2026-11-24.html).
//
// The user's rule: every setting and every input is checked before any work starts, and any problem -- a bad command
// line, setting or input -- is reported, all of them in detail, and the run stops. main() used to be one 1,507-line
// function that read settings between stages, some only after hours of routing (a bad PD39ROUTE_PORTFOLIO stopped the
// run after the slow tier), and stopped at the first problem. Now a run is four steps:
//   commandLine()  the arguments: an unknown option, the wrong count, an input that cannot be read, an output that
//                  cannot be written, an output that is an input;
//   readSettings() every setting (the code main() had, now RouterRun's fields), each in its own step(): a problem is
//                  recorded and the next setting is still read; with settings::problems() (unknown names, numbers,
//                  flags, the allowed words of pd39_settings.h);
//   readInputs()   the design (the readers report every problem that leaves the file's structure intact, Part 216),
//                  the design-size checks, and the files settings name (price seed, PD3ROUTE_LA_FROM, and -- when every
//                  setting is valid -- the timing slack, multipliers and rows);
//   work()         the routing, only when the three found nothing.
// While the first three run, gr_io::fail() throws (gr_io::failThrows) and step() records the message; work() runs with
// fail() ending the program as before. The routes are unchanged (checked byte for byte, Part 216).
// ---------------------------------------------------------------------------

// Why an input path cannot be read ("" if it can).
std::string inputProblem(const std::string &path) {
  struct stat st;
  if (path.empty())
    return "the name is empty";
  if (stat(path.c_str(), &st) != 0)
    return std::string("it does not exist or cannot be reached (") + std::strerror(errno) + ")";
  if (S_ISDIR(st.st_mode))
    return "it is a folder";
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return "it cannot be opened for reading";
  return "";
}

// Why an output path cannot be written ("" if it can): its folder must exist and be writable, and the path must not be
// a folder or a file that cannot be written. Nothing is created.
std::string outputProblem(const std::string &path) {
  struct stat st;
  if (path.empty())
    return "the name is empty";
  if (stat(path.c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode))
      return "it is a folder";
    if (access(path.c_str(), W_OK) != 0)
      return "it exists and cannot be written";
    return "";
  }
  const size_t slash = path.rfind('/');
  std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
  if (stat(dir.c_str(), &st) != 0)
    return "its folder " + dir + " does not exist";
  if (!S_ISDIR(st.st_mode))
    return dir + " is not a folder";
  if (access(dir.c_str(), W_OK | X_OK) != 0)
    return "its folder " + dir + " cannot be written";
  return "";
}

// Why a folder cannot be written into ("" if it can).
std::string folderProblem(const std::string &path) {
  struct stat st;
  if (path.empty())
    return "the name is empty";
  if (stat(path.c_str(), &st) != 0)
    return "it does not exist";
  if (!S_ISDIR(st.st_mode))
    return "it is not a folder";
  if (access(path.c_str(), W_OK | X_OK) != 0)
    return "it cannot be written";
  return "";
}

// The same file, by its resolved path (false when either cannot be resolved).
bool sameFile(const std::string &a, const std::string &b) {
  char *ra = realpath(a.c_str(), nullptr), *rb = realpath(b.c_str(), nullptr);
  const bool same = ra && rb && std::strcmp(ra, rb) == 0;
  std::free(ra);
  std::free(rb);
  return same;
}

struct RouterRun {
  // ---- the command line ----
  std::string prog = "pd39route";
  bool g24Mode = false;
  std::string inGr, inCap, inNet, outPath;
  bool commandLineOk = true;
  // ---- every problem found before the work, in the order found ----
  std::vector<std::string> problems;
  bool timingFilesSkipped = false;
  template <class F> void step(F &&f) {
    try {
      f();
    } catch (const std::runtime_error &e) { // gr_io::Failure, gr_io::InputError
      problems.push_back(e.what());
    }
  }
  static void outputOk(const std::string &path, const char *what) {
    if (const std::string why = outputProblem(path); !why.empty())
      fail(std::string(what) + ": cannot write " + path + " (" + why + ")");
  }
  static void folderOk(const std::string &path, const char *what) {
    if (const std::string why = folderProblem(path); !why.empty())
      fail(std::string(what) + ": cannot write into the folder " + path + " (" + why + ")");
  }
  // ---- the settings (readSettings): main()'s locals before Part 216, with the same defaults ----
  int maxFastRounds = 60, maxSlowRounds = 150;
  double epsilon = 0.02;
  double histStep = 0.05;
  double histFactor = 1.1;
  HistMode histMode = HistMode::kAdditive;
  int streakThreshold = 1;
  bool scaleByOverflow = false;
  bool relaxEnabled = true;
  double ratioExponent = 1.0;
  double exponentEscalation = 0.0;
  bool useMSMV = true;
  bool useSteinerize = true;
  bool useExactSteiner = true;
  int maxExactPins = 8;
  BoxGrowth box;
  CostSettings cost;
  LaMode laMode{};
  const char *laFrom = nullptr;
  RegionSettings region;
  RefineSettings refine;
  AcceptMode accept{};
  bool twoDOnly = false;
  double viaWeight = 1.0;
  HistKnobs viaKnobs;
  int viaRounds = 60;
  int viaPatience = 20;
  HistKnobs wireKnobs;
  bool align = false;                     // ISPD 2024: PD33ROUTE_AP
  std::vector<double> timingK, layerR, layerC;  // ISPD 2025: PD39ROUTE_TIMING_LAYERS, _LAYER_R, _LAYER_C
  double powerK = 0;                      // ISPD 2025: PD39ROUTE_POWER_LAYERS
  bool skipGiant = false;                 // ISPD 2008: PD39ROUTE_SKIP_GIANT
  PatternStageSettings pattern;
  bool slowPieces = false;
  long long stopAt = -1;
  std::string lowMem = "auto";
  std::string priceSeedPath;
  bool laPins = true;
  RebuildSettings rebuild;
  WlSettings wl;
  bool vcapB = true;
  bool fallbackParallel = false;
  bool reclaimFallback = false;
  double costRel = 0.0;
  ReclaimSettings reclaimFb; // the fallback's path B reclaim (PD16ROUTE_RECLAIM not read there)
  bool bFirst = false;
  long long aMax = -1;
  ReclaimSettings reclaim; // the stand-alone reclaim pass
  const char *forcedReport = nullptr;
  int forcedSide = 16, forcedSpots = 8;
  const char *netTimes = nullptr;
  std::string slowTaskPath;
  // ---- the inputs (readInputs) and the work ----
  std::chrono::steady_clock::time_point t0;
  G24Input g24In;
  Design d;
  RunReport rep;

  int run(int argc, char **argv);
  void commandLine(int argc, char **argv);
  void readSettings();
  void readInputs();
  int report() const;
  int work();
};

void RouterRun::commandLine(int argc, char **argv) {
  if (argc > 0 && argv[0])
    prog = argv[0];
  std::vector<std::string> a(argv + 1, argv + argc);
  auto bad = [&](const std::string &m) {
    problems.push_back(m);
    commandLineOk = false;
  };
  for (const std::string &s : a)
    if (s.size() > 1 && s[0] == '-')
      bad("unknown option " + s + " (--selftest and --settings are given alone; a routing run takes file names only)");
  if (!commandLineOk)
    return;
  if (a.size() == 2) {
    inGr = a[0];
    outPath = a[1];
  } else if (a.size() == 3) {
    g24Mode = true;
    inCap = a[0];
    inNet = a[1];
    outPath = a[2];
  } else {
    bad(std::to_string(a.size()) + (a.size() == 1 ? " argument" : " arguments") + " given; a run takes 2 (ISPD 2008: "
        "design.gr solution.route) or 3 (ISPD 2024 / 2025: design.cap design.net solution)");
    return;
  }
  const std::vector<std::pair<const char *, const std::string *>> ins =
      g24Mode ? std::vector<std::pair<const char *, const std::string *>>{{"the .cap file", &inCap},
                                                                         {"the .net file", &inNet}}
              : std::vector<std::pair<const char *, const std::string *>>{{"the .gr file", &inGr}};
  for (const auto &[what, path] : ins)
    if (const std::string why = inputProblem(*path); !why.empty())
      bad(std::string("cannot read ") + what + " " + *path + ": " + why);
  if (const std::string why = outputProblem(outPath); !why.empty()) {
    problems.push_back("cannot write the solution " + outPath + ": " + why); // the inputs can still be checked
  }
  for (const auto &[what, path] : ins)
    if (sameFile(*path, outPath))
      bad("the solution " + outPath + " is " + what + " (it would be overwritten)");
}

void RouterRun::readSettings() {
  step([&] {
    if (const char *v = settings::get("PD37ROUTE_THREADS"); v) {
      char *end = nullptr;
      long t = std::strtol(v, &end, 10);
      if (!*v || *end || t < 1 || t > 256)
        fail(std::string("PD37ROUTE_THREADS must be 1..256 (got '") + v + "')");
      gThreads = static_cast<int>(t);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_SLOW_DAG"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_SLOW_DAG must be 0 or 1 (got '" + sv + "')");
      gSlowDag = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_FRAMES"); v && *v) {
      gFrames.on = true;
      gRecordCarried = true;
      gFrames.dir = v;
      folderOk(v, "PD38ROUTE_FRAMES");
      gFrames.t0 = std::chrono::steady_clock::now();
      if (const char *w = settings::get("PD38ROUTE_FRAME_SIDE"); w)
        gFrames.side = std::max(16, settings::intValue("PD38ROUTE_FRAME_SIDE", w));
      if (const char *w = settings::get("PD38ROUTE_FRAME_SPLIT"); w)
        gFrames.split = std::max(1, settings::intValue("PD38ROUTE_FRAME_SPLIT", w));
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_P3D_REUSE"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_P3D_REUSE must be 0 or 1 (got '" + sv + "')");
      gP3dReuse = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_P3D_SKIP"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_P3D_SKIP must be 0 or 1 (got '" + sv + "')");
      gP3dSkip = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_PATTERN_DAG"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_PATTERN_DAG must be 0 or 1 (got '" + sv + "')");
      gPatternDag = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_SLOW_BATCH"); v) {
      char *end = nullptr;
      long b = std::strtol(v, &end, 10);
      if (!*v || *end || b < 0 || b > 1000000)
        fail(std::string("PD38ROUTE_SLOW_BATCH must be 0..1000000 (got '") + v + "')");
      gSlowBatch = static_cast<int>(b);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_SLOW_SPREAD"); v) {
      char *end = nullptr;
      long t = std::strtol(v, &end, 10);
      if (!*v || *end || t < 0 || t > 100000)
        fail(std::string("PD38ROUTE_SLOW_SPREAD must be 0..100000 GCells (got '") + v + "')");
      gSlowSpread = static_cast<int>(t);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_SLOW_CONFLICT"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_SLOW_CONFLICT must be 0 or 1 (got '" + sv + "')");
      gSlowConflict = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SEARCH_BIDIR"); v) { // Part 197: read before route()
      const std::string sv(v);
      if (sv != "0" && sv != "1" && sv != "2")
        fail("PD39ROUTE_SEARCH_BIDIR must be 0, 1 or 2 (got '" + sv + "')");
      gSearchBidir = sv != "0";
      gSearchBidirPotentials = sv == "2"; // Part 199
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SLOW_SEARCHES"); v) { // Part 193: K; Part 195: K,below
      char *end = nullptr;
      const long k = std::strtol(v, &end, 10);
      long long below = std::numeric_limits<long long>::max();
      bool ok = *v && end != v && k >= 0 && k <= 1000000;
      if (ok && *end == ',') {
        char *e2 = nullptr;
        below = std::strtoll(end + 1, &e2, 10);
        ok = e2 != end + 1 && !*e2 && below >= 0;
      } else if (ok) {
        ok = !*end;
      }
      if (!ok)
        fail(std::string("PD39ROUTE_SLOW_SEARCHES must be K[,below] with K 0..1000000 and below >= 0 (got '") + v + "')");
      gSlowBatchSearches = static_cast<int>(k);
      gSlowBatchSearchesBelow = below;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SLOW_TASKS"); v && *v) { // Part 192 (trace only): file[,every]
      std::string sv(v), file = sv;
      const size_t c = sv.rfind(',');
      if (c != std::string::npos) {
        file = sv.substr(0, c);
        char *end = nullptr;
        const long e = std::strtol(sv.c_str() + c + 1, &end, 10);
        if (c + 1 == sv.size() || *end || e < 1)
          fail("PD39ROUTE_SLOW_TASKS must be file[,every] with every >= 1 (got '" + sv + "')");
        gSlowTaskEvery = static_cast<int>(e);
      }
      slowTaskPath = file; // opened when the work starts
      outputOk(file, "PD39ROUTE_SLOW_TASKS");
    }
  });
  step([&] {
    if (const char *v = settings::get("PD38ROUTE_SLOW_LPT"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD38ROUTE_SLOW_LPT must be 0 or 1 (got '" + sv + "')");
      gSlowBatchLpt = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD37ROUTE_CELL"); v) {
      char *end = nullptr;
      long c = std::strtol(v, &end, 10);
      if (!*v || *end || c < 1 || c > 1024)
        fail(std::string("PD37ROUTE_CELL must be 1..1024 (got '") + v + "')");
      gCommuteCell = static_cast<int>(c);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_LA_CELL"); v) { // Part 129
      char *end = nullptr;
      long c = std::strtol(v, &end, 10);
      if (!*v || *end || c < 0 || c > 1024)
        fail(std::string("PD39ROUTE_LA_CELL must be 0..1024 (got '") + v + "')");
      gLaCell = static_cast<int>(c);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_LA_CRIT"); v) { // Part 132
      if (std::strcmp(v, "0") && std::strcmp(v, "1"))
        fail(std::string("PD39ROUTE_LA_CRIT must be 0 or 1 (got '") + v + "')");
      gLaCrit = v[0] == '1';
    }
  });
  step([&] {
    if (const char *e = settings::get("VROUTE_EPS"); e)
      epsilon = settings::realValue("VROUTE_EPS", e);
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_FAST_ROUNDS"); r)
      maxFastRounds = settings::intValue("VROUTE_FAST_ROUNDS", r);
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_SLOW_ROUNDS"); r)
      maxSlowRounds = settings::intValue("VROUTE_SLOW_ROUNDS", r);
  });
  step([&] {
    if (const char *s = settings::get("VROUTE_HIST_STEP"); s)
      histStep = settings::realValue("VROUTE_HIST_STEP", s);
  });
  step([&] {
    if (const char *f = settings::get("VROUTE_HIST_FACTOR"); f)
      histFactor = settings::realValue("VROUTE_HIST_FACTOR", f);
  });
  step([&] {
    if (const char *m = settings::get("VROUTE_HIST_MODE"); m && std::string(m) == "mult")
      histMode = HistMode::kMultiplicative;
    else if (m && std::string(m) == "ratio") // m: the if's own (Part 215, review F5)
      histMode = HistMode::kRatio;
  });
  step([&] {
    if (const char *s = settings::get("VROUTE_HIST_STREAK"); s)
      streakThreshold = std::max(1, settings::intValue("VROUTE_HIST_STREAK", s));
  });
  step([&] {
    if (const char *s = settings::get("VROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
      scaleByOverflow = true;
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_HIST_RELAX"); r)
      relaxEnabled = std::string(r) != "0";
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_HIST_RATIO_EXPONENT"); r)
      ratioExponent = settings::realValue("VROUTE_HIST_RATIO_EXPONENT", r);
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_HIST_EXPONENT_ESCALATION"); r)
      exponentEscalation = settings::realValue("VROUTE_HIST_EXPONENT_ESCALATION", r);
  });
  step([&] {
    if (const char *r = settings::get("VROUTE_MSMV"); r)
      useMSMV = std::string(r) != "0";
  });
  // fr3route's own mechanism: FR3ROUTE_STEINERIZE=0 reproduces fr1route's
  // exact plain-MST topology for direct comparison. Still governs the
  // fallback path below this file's own exact solve (either the nets it
  // skips by degree cap, or all nets if the exact solve itself is off).
  step([&] {
    if (const char *r = settings::get("FR3ROUTE_STEINERIZE"); r)
      useSteinerize = std::string(r) != "0";
  });
  // This file's own new mechanism: FR4ROUTE_EXACT_STEINER=0 disables the
  // exact Dreyfus-Wagner solve entirely, reproducing fr3route's own exact
  // behavior for direct comparison.
  step([&] {
    if (const char *r = settings::get("FR4ROUTE_EXACT_STEINER"); r)
      useExactSteiner = std::string(r) != "0";
  });
  step([&] {
    if (const char *r = settings::get("FR4ROUTE_EXACT_MAX_PINS"); r)
      maxExactPins = settings::intValue("FR4ROUTE_EXACT_MAX_PINS", r);
  });
  // This file's own mechanism: slow-tier search-box growth.
  step([&] {
    {
      std::string err;
      if (!parseBoxGrowth(settings::get("NV11ROUTE_BOX_MODE"), settings::get("NV11ROUTE_BOX_INIT"),
                          settings::get("NV11ROUTE_BOX_GROW"), settings::get("NV11ROUTE_BOX_CAP"), box,
                          err))
        fail(err);
    }
  });
  // This file's own mechanism: the 2D pricing model.
  step([&] {
    {
      std::string err;
      if (!parseCostSettings(settings::get("PD1ROUTE_COST"), settings::get("PD1ROUTE_WL_FLOOR"), cost, err))
        fail(err);
    }
  });
  // pd3route's own mechanism: layer-assignment mode, and the
  // layer-assignment-only rerun from an existing route file.
  step([&] {
    {
      std::string err;
      if (!parseLaMode(settings::get("PD3ROUTE_LA"), laMode, err))
        fail(err);
    }
  });
  step([&] {
    laFrom = settings::get("PD3ROUTE_LA_FROM");
  });
  // pd5route's own mechanism 2: hotspot rip-up.
  step([&] {
    {
      std::string err;
      if (!parseRegionSettings(settings::get("PD5ROUTE_REGION"), settings::get("PD5ROUTE_REGION_EXPAND"),
                               settings::get("PD5ROUTE_REGION_ROUNDS"), settings::get("PD5ROUTE_REGION_BOX_INIT"),
                               settings::get("PD5ROUTE_REGION_BOX_GROW"), region, err))
        fail(err);
    }
  });
  // pd4route's own mechanism: the refinement stage.
  step([&] {
    {
      std::string err;
      if (!parseRefineSettings(settings::get("PD4ROUTE_REFINE"), settings::get("PD4ROUTE_REFINE_ITERS"),
                               settings::get("PD4ROUTE_REFINE_BOX_INIT"), settings::get("PD4ROUTE_REFINE_BOX_INC"),
                               settings::get("PD4ROUTE_REFINE_ACCEPT"), refine, err) ||
          !parseRefineOrder(settings::get("PD5ROUTE_REFINE_ORDER"), settings::get("PD5ROUTE_SEED"), refine, err))
        fail(err);
      if (const char *v = settings::get("PD39ROUTE_REFINE_SHIFT"); v) { // Part 152: k[,radius]
        int k = -1, r = 2;
        char tail = 0;
        const int n = std::sscanf(v, "%d,%d%c", &k, &r, &tail);
        if (!(n == 1 || n == 2) || k < 0 || k > 1000 || r < 0 || r > 1000)
          fail(std::string("PD39ROUTE_REFINE_SHIFT must be k[,radius] (0-1000 each; got '") + v + "')");
        refine.shiftK = k;
        refine.shiftRadius = r;
      }
      if (const char *v = settings::get("PD39ROUTE_REFINE_EVICT"); v) { // Part 191: nets[,margin[,blockers[,passes]]]
        int n = -1, m = 10, b = 8, p = 3;
        char tail = 0;
        const int got = std::sscanf(v, "%d,%d,%d,%d%c", &n, &m, &b, &p, &tail);
        if (got < 1 || got > 4 || n < 0 || n > 10000000 || m < 0 || m > 10000 || b < 1 || b > 10000 || p < 1 ||
            p > 1000)
          fail(std::string("PD39ROUTE_REFINE_EVICT must be nets[,margin[,blockers[,passes]]] (nets 0-1e7, margin "
                           "0-1e4, blockers 1-1e4, passes 1-1000; got '") + v + "')");
        refine.evictNets = n;
        refine.evictMargin = m;
        refine.evictBlockers = b;
        refine.evictPasses = p;
      }
      if (const char *v = settings::get("PD39ROUTE_REFINE_KEEP_ABOVE"); v) { // Part 213: N
        char *end = nullptr;
        const long long x = std::strtoll(v, &end, 10);
        if (!*v || *end || x < 0)
          fail(std::string("PD39ROUTE_REFINE_KEEP_ABOVE must be an integer >= 0 (got '") + v + "')");
        refine.keepAbove = x;
      }
      if (const char *v = settings::get("PD39ROUTE_REFINE_STOP"); v) { // Part 168: W
        char *end = nullptr;
        const long x = std::strtol(v, &end, 10);
        if (!*v || *end || x < 0 || x > 1000000)
          fail(std::string("PD39ROUTE_REFINE_STOP must be an integer >= 0 (got '") + v + "')");
        refine.lowStop = static_cast<int>(x);
        if (x > 0)
          std::cerr << "refinement: below PD12ROUTE_PLATEAU_MIN, stop after " << x
                    << " iterations in a row without gain (PD39ROUTE_REFINE_STOP)\n";
      }
      if (const char *v = settings::get("PD39ROUTE_REFINE_ANNEAL"); v) { // Part 147: T0,alpha
        double t0 = -1, alpha = -1;
        char tail = 0;
        if (std::sscanf(v, "%lf,%lf%c", &t0, &alpha, &tail) != 2 || !(t0 >= 0) || !(alpha > 0 && alpha <= 1))
          fail(std::string("PD39ROUTE_REFINE_ANNEAL must be T0,alpha (T0 >= 0, 0 < alpha <= 1; got '") + v + "')");
        refine.annealT0 = t0;
        refine.annealAlpha = alpha;
        if (t0 > 0 && refine.accept != AcceptMode::kRevert)
          fail("PD39ROUTE_REFINE_ANNEAL needs PD4ROUTE_REFINE_ACCEPT=revert (it softens the revert rule)");
        if (t0 > 0)
          std::cerr << "refinement annealing: T0 " << t0 << ", x" << alpha << " per iteration\n";
      }
    }
  });
  // pd12route's own mechanism: the plateau stop for both stages.
  step([&] {
    {
      std::string err;
      double gain = 0.01;
      long long minOverflow = 0;
      if (!parsePlateauSettings(settings::get("PD12ROUTE_REGION_PLATEAU"), settings::get("PD12ROUTE_REFINE_PLATEAU"),
                                settings::get("PD12ROUTE_PLATEAU_GAIN"), settings::get("PD12ROUTE_PLATEAU_MIN"),
                                region.plateau, refine.plateau, gain, minOverflow, err))
        fail(err);
      region.plateauGain = refine.plateauGain = gain;
      region.plateauMin = refine.plateauMin = minOverflow;
      // pd13route's own mechanism: the stage margin caps.
      if (!parseBoxCaps(settings::get("PD13ROUTE_REGION_BOX_CAP"), settings::get("PD13ROUTE_REFINE_BOX_CAP"), region.boxCap,
                        refine.boxCap, err))
        fail(err);
      if (const char *v = settings::get("PD39ROUTE_REGION_BOX_CAP_B"); v) { // Part 141
        char *end = nullptr;
        const long x = std::strtol(v, &end, 10);
        if (!*v || *end || x < 0 || x > 1000000)
          fail(std::string("PD39ROUTE_REGION_BOX_CAP_B must be an integer >= 0 (got '") + v + "')");
        region.boxCapB = static_cast<int>(x);
      }
      // pd14route's own mechanism: the work-gated region plateau stop.
      if (!parseCostPlateau(settings::get("PD14ROUTE_REGION_COST_WINDOW"), settings::get("PD14ROUTE_REGION_COST_POPS"),
                            region.costWindow, region.costPops, err))
        fail(err);
      if (const char *v = settings::get("PD39ROUTE_REGION_COST_REROUTES"); v) { // Part 199
        char *end = nullptr;
        const long long n = std::strtoll(v, &end, 10);
        if (!*v || *end || n < 0)
          fail(std::string("PD39ROUTE_REGION_COST_REROUTES must be an integer >= 0 (got '") + v + "')");
        region.costReroutes = n;
      }
    }
  });
  step([&] {
    if (laFrom && !*laFrom)
      fail("PD3ROUTE_LA_FROM is set but empty");
  });
  // pd2route's own mechanism: slow-tier acceptance rule.
  step([&] {
    {
      std::string err;
      if (!parseAcceptMode(settings::get("PD2ROUTE_ACCEPT"), accept, err))
        fail(err);
    }
  });
  // This file's own new mechanism, finally making this long-reserved flag
  // functional: FR1ROUTE_TWOD_ONLY=1 skips negotiateViaAssignment() entirely,
  // reproducing fr4route's exact single-layer, zero-via output.
  step([&] {
    twoDOnly = settings::get("FR1ROUTE_TWOD_ONLY") != nullptr;
  });

  // This file's own new mechanism: the via-negotiation loop's own knobs,
  // independent of the 2D stage's VROUTE_HIST_* above (separate ViaPrices
  // instance) -- see the file header for each one's role. Carried over
  // from nv1route's own NV1ROUTE_* vars, renamed NV2ROUTE_* for this
  // file's own functional reads.
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_VIA_WEIGHT"); r)
      viaWeight = settings::realValue("NV2ROUTE_VIA_WEIGHT", r);
  });
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_HIST_STEP"); s)
      viaKnobs.step = settings::realValue("NV2ROUTE_HIST_STEP", s);
  });
  step([&] {
    if (const char *f = settings::get("NV2ROUTE_HIST_FACTOR"); f)
      viaKnobs.factor = settings::realValue("NV2ROUTE_HIST_FACTOR", f);
  });
  step([&] {
    if (const char *m = settings::get("NV2ROUTE_HIST_MODE"); m && std::string(m) == "mult")
      viaKnobs.mode = HistMode::kMultiplicative;
    else if (m && std::string(m) == "ratio") // m: the if's own (Part 215, review F5)
      viaKnobs.mode = HistMode::kRatio;
  });
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_HIST_STREAK"); s)
      viaKnobs.streakThreshold = std::max(1, settings::intValue("NV2ROUTE_HIST_STREAK", s));
  });
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_HIST_SCALE"); s && std::string(s) == "overflow")
      viaKnobs.scaleByOverflow = true;
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_HIST_RELAX"); r)
      viaKnobs.relaxEnabled = std::string(r) != "0";
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_HIST_RATIO_EXPONENT"); r)
      viaKnobs.ratioExponent = settings::realValue("NV2ROUTE_HIST_RATIO_EXPONENT", r);
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_HIST_EXPONENT_ESCALATION"); r)
      viaKnobs.exponentEscalation = settings::realValue("NV2ROUTE_HIST_EXPONENT_ESCALATION", r);
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_VIA_ROUNDS"); r)
      viaRounds = settings::intValue("NV2ROUTE_VIA_ROUNDS", r);
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_VIA_PATIENCE"); r)
      viaPatience = settings::intValue("NV2ROUTE_VIA_PATIENCE", r);
  });

  // This file's own new mechanism: the wire-history knobs, same roles as
  // the via ones above, independent WirePrices instance.
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_WIRE_HIST_STEP"); s)
      wireKnobs.step = settings::realValue("NV2ROUTE_WIRE_HIST_STEP", s);
  });
  step([&] {
    if (const char *f = settings::get("NV2ROUTE_WIRE_HIST_FACTOR"); f)
      wireKnobs.factor = settings::realValue("NV2ROUTE_WIRE_HIST_FACTOR", f);
  });
  step([&] {
    if (const char *m = settings::get("NV2ROUTE_WIRE_HIST_MODE"); m && std::string(m) == "mult")
      wireKnobs.mode = HistMode::kMultiplicative;
    else if (m && std::string(m) == "ratio") // m: the if's own (Part 215, review F5)
      wireKnobs.mode = HistMode::kRatio;
  });
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_WIRE_HIST_STREAK"); s)
      wireKnobs.streakThreshold = std::max(1, settings::intValue("NV2ROUTE_WIRE_HIST_STREAK", s));
  });
  step([&] {
    if (const char *s = settings::get("NV2ROUTE_WIRE_HIST_SCALE"); s && std::string(s) == "overflow")
      wireKnobs.scaleByOverflow = true;
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_WIRE_HIST_RELAX"); r)
      wireKnobs.relaxEnabled = std::string(r) != "0";
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_WIRE_HIST_RATIO_EXPONENT"); r)
      wireKnobs.ratioExponent = settings::realValue("NV2ROUTE_WIRE_HIST_RATIO_EXPONENT", r);
  });
  step([&] {
    if (const char *r = settings::get("NV2ROUTE_WIRE_HIST_EXPONENT_ESCALATION"); r)
      wireKnobs.exponentEscalation = settings::realValue("NV2ROUTE_WIRE_HIST_EXPONENT_ESCALATION", r);
  });

  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_AP"); v) {
        std::string sv(v);
        if (sv != "first" && sv != "align")
          fail("PD33ROUTE_AP must be first or align (got '" + sv + "')");
        align = sv == "align";
        std::cerr << "ISPD 2024 access points: " << sv << "\n";
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD39ROUTE_POWER_LAYERS"); v) { // Part 133
        std::vector<double> pk;
        if (!parseDoubleList(v, pk) || pk.size() != 1 || pk[0] < 0)
          fail(std::string("PD39ROUTE_POWER_LAYERS must be one number kp >= 0 (got '") + v + "')");
        powerK = pk[0];
        const char *c = settings::get("PD39ROUTE_LAYER_C");
        if (!c || !parseDoubleList(c, layerC) || layerC.empty())
          fail("PD39ROUTE_POWER_LAYERS needs PD39ROUTE_LAYER_C=c1,...,cL (one capacitance per layer)");
        gTiming.on = true;
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD39ROUTE_TIMING_REF"); v) { // Part 133
        std::string sv(v);
        std::vector<double> q;
        if (sv == "worst")
          gTiming.refQ = -1;
        else if (parseDoubleList(v, q) && q.size() == 1 && q[0] >= 0 && q[0] < 1)
          gTiming.refQ = q[0];
        else
          fail("PD39ROUTE_TIMING_REF must be worst or a fraction 0 <= q < 1 (got '" + sv + "')");
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD39ROUTE_TIMING_LAYERS"); v) {
        if (!parseDoubleList(v, timingK) || timingK.size() != 3)
          fail(std::string("PD39ROUTE_TIMING_LAYERS must be three numbers k1,k2,k3 (got '") + v + "')");
        const char *r = settings::get("PD39ROUTE_LAYER_R");
        if (!r || !parseDoubleList(r, layerR) || layerR.empty())
          fail("PD39ROUTE_TIMING_LAYERS needs PD39ROUTE_LAYER_R=r1,...,rL (one resistance per layer)");
        gTiming.on = true;
        if (const char *gv = settings::get("PD39ROUTE_TIMING_GATE"); gv) {
          if (std::strcmp(gv, "0") && std::strcmp(gv, "1"))
            fail(std::string("PD39ROUTE_TIMING_GATE must be 0 or 1 (got '") + gv + "')");
          gTiming.gate = gv[0] == '1';
        }
      }
    }
  });
  step([&] {
    if (const char *mv = settings::get("PD39ROUTE_TIMING_SLACK_MODE"); mv) { // checked here; applied with the slack
      const std::string mode = mv;
      if (mode != "replace" && mode != "min")
        fail("PD39ROUTE_TIMING_SLACK_MODE must be replace or min (got '" + mode + "')");
    }
  });
  step([&] {
      if (const char *km = settings::get("PD39ROUTE_TIMING_KMIN"); km) { // Part 133 calibration
        std::vector<double> v;
        if (!parseDoubleList(km, v) || v.size() != 1 || v[0] < 0)
          fail(std::string("PD39ROUTE_TIMING_KMIN must be one number >= 0 (got '") + km + "')");
        gTiming.kMin = v[0];
      }
  });
  step([&] {
    {
      const char *sc = settings::get("PD39ROUTE_TIMING_SCALE");
      std::string scale = sc ? sc : "1";
      if (scale != "1" && scale != "weights")
        fail("PD39ROUTE_TIMING_SCALE must be 1 or weights (got '" + scale + "')");
    }
  });
  step([&] {
    if (const char *bv = settings::get("PD39ROUTE_TIMING_BENEFIT"); bv && *bv) { // checked here; applied with the timing set-up
        std::vector<double> v;
        if (!parseDoubleList(bv, v) || v.empty() || v.size() > 4 || v[0] < 0 || v[0] > 4 ||
            (v.size() > 1 && !(v[1] >= 1 && v[1] <= 1e6)) || (v.size() > 2 && !(v[2] >= 0 && v[2] <= 1e6)) ||
            (v.size() > 3 && !(v[3] == -1 || (v[3] >= 0 && v[3] <= 1))))
          fail(std::string("PD39ROUTE_TIMING_BENEFIT must be g[,M[,P[,q]]] with g 0-4, M >= 1, P >= 0, q -1 or 0-1 "
                           "(got '") + bv + "')");
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_VIA_DEMAND"); v) {
        std::string sv(v);
        if (sv != "0" && sv != "1")
          fail("PD33ROUTE_VIA_DEMAND must be 0 or 1 (got '" + sv + "')");
        gViaDemand.on = sv == "1";
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_LA_COST"); v) {
        std::string sv(v);
        if (sv != "nthu" && sv != "score")
          fail("PD33ROUTE_LA_COST must be nthu or score (got '" + sv + "')");
        if (sv == "score" && !gViaDemand.on)
          fail("PD33ROUTE_LA_COST=score needs PD33ROUTE_VIA_DEMAND=1 (the score counts via demand)");
        gScore.on = sv == "score";
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD34ROUTE_2D_COST"); v) {
        std::string sv(v);
        if (sv != "pd" && sv != "score")
          fail("PD34ROUTE_2D_COST must be pd or score (got '" + sv + "')");
        gScore2D.on = sv == "score";
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD34ROUTE_2D_HIST"); v) {
        std::string sv(v);
        if (sv != "0" && sv != "1")
          fail("PD34ROUTE_2D_HIST must be 0 or 1 (got '" + sv + "')");
        gScore2D.hist = sv == "1";
      }
    }
  });
  step([&] {
    if (g24Mode) {
      if (const char *v = settings::get("PD35ROUTE_PATTERN3D"); v) {
        char *end = nullptr;
        long k = std::strtol(v, &end, 10);
        if (!*v || *end || k < 0 || k > 100)
          fail(std::string("PD35ROUTE_PATTERN3D must be a pass count 0..100 (got '") + v + "')");
        if (k > 0 && !gScore.on)
          fail("PD35ROUTE_PATTERN3D needs PD33ROUTE_LA_COST=score (it prices with the score)");
        gPattern3D.passes = static_cast<int>(k);
      }
    }
  });
  step([&] {
    if (!g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_VIA_DEMAND"); v && std::string(v) != "0")
        fail("PD33ROUTE_VIA_DEMAND needs an ISPD 2024 design (.cap .net solution)");
    }
  });
  step([&] {
    if (!g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_LA_COST"); v && std::string(v) != "nthu")
        fail("PD33ROUTE_LA_COST=score needs an ISPD 2024 design (.cap .net solution)");
    }
  });
  step([&] {
    if (!g24Mode) {
      if (const char *v = settings::get("PD33ROUTE_AP"); v && std::string(v) != "first")
        fail("PD33ROUTE_AP=align needs an ISPD 2024 design (.cap .net solution)");
    }
  });
  step([&] {
    if (!g24Mode) {
      if (const char *v = settings::get("PD34ROUTE_2D_COST"); v && std::string(v) != "pd")
        fail("PD34ROUTE_2D_COST=score needs an ISPD 2024 design (.cap .net solution)");
    }
  });
  step([&] {
    if (!g24Mode) {
      if (const char *v = settings::get("PD35ROUTE_PATTERN3D"); v) {
        char *end = nullptr;
        long k = std::strtol(v, &end, 10);
        if (!*v || *end || k < 0 || k > 100)
          fail(std::string("PD35ROUTE_PATTERN3D must be a pass count 0..100 (got '") + v + "')");
        gPattern3D.passes = static_cast<int>(k);
        gPattern3D.mode2008 = k > 0;
        if (k > 0)
          std::cerr << "3D pattern reroute passes (ISPD 2008 cost): " << k << "\n";
      }
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SKIP_GIANT"); v) { // Part 212
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD39ROUTE_SKIP_GIANT must be 0 or 1 (got '" + sv + "')");
      skipGiant = sv == "1";
    }
  });
  // pd11route's own mechanism: the batched pattern stage.
  step([&] {
    {
      if (const char *v = settings::get("PD11ROUTE_PATTERN"); v) {
        std::string sv(v);
        if (sv != "0" && sv != "1")
          fail("PD11ROUTE_PATTERN must be 0 or 1 (got '" + sv + "')");
        pattern.enabled = sv == "1";
      }
      auto num = [&](const char *name, int lo, int &dst) {
        if (const char *v = settings::get(name); v) {
          char *end = nullptr;
          long x = std::strtol(v, &end, 10);
          if (end == v || *end != '\0' || x < lo || x > 1000000)
            fail(std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')");
          dst = static_cast<int>(x);
        }
      };
      num("PD11ROUTE_PATTERN_ROUNDS", 1, pattern.rounds);
      num("PD11ROUTE_PATTERN_CELL", 1, pattern.cell);
    }
  });
  // pd10route's own mechanism: pattern-first piece reroutes (default on).
  step([&] {
    patternFirst = true;
  });
  step([&] {
    if (const char *v = settings::get("PD10ROUTE_PATTERN_FIRST"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD10ROUTE_PATTERN_FIRST must be 0 or 1 (got '" + sv + "')");
      patternFirst = sv == "1";
    }
  });
  // pd9route's own mechanisms: search-stage piece reroutes and a threshold stop.
  step([&] {
    if (const char *v = settings::get("PD9ROUTE_SLOW_PIECES"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD9ROUTE_SLOW_PIECES must be 0 or 1 (got '" + sv + "')");
      slowPieces = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SEARCH_DUMP"); v && *v) { // Part 153 (trace only): prefix,every,minpops; Part 197: read before route(), so `all` covers the slow tier
      std::string sv(v);
      if (sv.size() > 4 && sv.compare(sv.size() - 4, 4, ",all") == 0) { // Part 197: every search of the run
        gSearchDump.all = true;
        sv.erase(sv.size() - 4);
      }
      const size_t c1 = sv.rfind(','), c0 = c1 == std::string::npos ? std::string::npos : sv.rfind(',', c1 - 1);
      if (c0 == std::string::npos)
        fail("PD39ROUTE_SEARCH_DUMP must be prefix,every,minpops (got '" + sv + "')");
      gSearchDump.prefix = sv.substr(0, c0);
      outputOk(gSearchDump.prefix + ".0.txt", "PD39ROUTE_SEARCH_DUMP");
      gSearchDump.every = settings::longValue("PD39ROUTE_SEARCH_DUMP (every)", sv.substr(c0 + 1, c1 - c0 - 1).c_str());
      gSearchDump.minPops = settings::longValue("PD39ROUTE_SEARCH_DUMP (minpops)", sv.substr(c1 + 1).c_str());
      if (gSearchDump.every <= 0 || gSearchDump.minPops < 0)
        fail("PD39ROUTE_SEARCH_DUMP: every must be >= 1 and minpops >= 0 (got '" + sv + "')");
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SLOW_PLATEAU"); v) { // Part 148: window,gain%,floor -- read before route() runs the slow tier
      int w = -1;
      double gp = -1;
      long long fl = -1;
      char tail = 0;
      if (std::sscanf(v, "%d,%lf,%lld%c", &w, &gp, &fl, &tail) != 3 || w < 0 || !(gp >= 0 && gp <= 100) || fl < 0)
        fail(std::string("PD39ROUTE_SLOW_PLATEAU must be window,gain%,floor (window >= 0, gain 0-100, floor >= 0; got '") +
             v + "')");
      gSlowPlateau.window = w;
      gSlowPlateau.gain = gp / 100.0;
      gSlowPlateau.floor = fl;
      if (w > 0)
        std::cerr << "slow tier plateau stop: best down < " << gp << "% over " << w << " rounds while >= " << fl << "\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SLOW_CUT"); v) { // Part 173: tracks,radius,below,every,full
      SlowCutSettings c;
      int full = -1;
      char tail = 0;
      if (std::sscanf(v, "%d,%d,%lld,%d,%d%c", &c.tracks, &c.radius, &c.below, &c.every, &full, &tail) != 5 ||
          c.tracks < 0 || c.tracks > 1000 || c.radius < 0 || c.radius > 1000 || c.below < 0 || c.every < 0 ||
          (full != 0 && full != 1))
        fail(std::string("PD39ROUTE_SLOW_CUT must be tracks,radius,below,every,full (tracks and radius 0-1000, below "
                         "and every >= 0, full 0 or 1; got '") +
             v + "')");
      c.full = full == 1;
      gSlowCut = c;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SLOW_OVER"); v) { // Part 186: slack[,below]
      SlowOverSettings o;
      long long below = -1;
      char tail = 0;
      const int n = std::sscanf(v, "%lf,%lld%c", &o.slack, &below, &tail);
      if (!((n == 1 && std::strchr(v, ',') == nullptr) || n == 2) || !(o.slack >= 0 && o.slack <= 1000) ||
          (n == 2 && below < 0))
        fail(std::string("PD39ROUTE_SLOW_OVER must be slack[,below] (slack 0-1000, below >= 0; got '") + v + "')");
      if (n == 2)
        o.below = below;
      gSlowOver = o;
      std::cerr << "slow tier over-capacity cost set: slack " << o.slack << ", from the first slow round at or below "
                << (n == 2 ? std::to_string(below) : std::string("any")) << " units of 2D overflow\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD9ROUTE_STOP_AT"); v) {
      char *end = nullptr;
      long long x = std::strtoll(v, &end, 10);
      if (end == v || *end != '\0' || x < 0)
        fail(std::string("PD9ROUTE_STOP_AT must be an integer >= 0 (got '") + v + "')");
      stopAt = x;
    }
  });
  // pd18route's own mechanism: the slow tier's reclaim settings.
  step([&] {
    {
      std::string err;
      ReclaimSettings rs;
      if (!parseReclaimSettings(nullptr, settings::get("PD16ROUTE_RECLAIM_RATIO"), settings::get("PD16ROUTE_RECLAIM_MIN_EXCESS"),
                                settings::get("PD16ROUTE_RECLAIM_MU"), settings::get("PD16ROUTE_RECLAIM_MARGIN"),
                                settings::get("PD16ROUTE_RECLAIM_PASSES"), rs, err) ||
          !parseReclaimAvoid(settings::get("PD17ROUTE_RECLAIM_AVOID"), rs, err) ||
          !parseSlowReclaim(settings::get("PD18ROUTE_RECLAIM_AT_STOP"), settings::get("PD18ROUTE_RECLAIM_EVERY"), gSlowReclaim,
                            err))
        fail(err);
      rs.enabled = true;
      gSlowReclaim.settings = rs;
    }
  });
  step([&] {
    {
      lowMem = settings::get("PD35ROUTE_LOWMEM") ? settings::get("PD35ROUTE_LOWMEM") : "auto";
      if (lowMem != "auto" && lowMem != "0" && lowMem != "1")
        fail("PD35ROUTE_LOWMEM must be auto, 0 or 1 (got '" + lowMem + "')");
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_ROUND0"); v && *v) { // Part 210
      const std::string sv(v);
      if (sv != "all" && sv != "over")
        fail("PD39ROUTE_REGION_ROUND0 must be all or over (got '" + sv + "')");
      gRegionRound0Over = sv == "over";
      if (gRegionRound0Over)
        std::cerr << "region stage: round 0 reroutes only nets on over-capacity edges (PD39ROUTE_REGION_ROUND0=over)\n";
    }
  });
  step([&] {
    if (g24Mode && settings::get("PD39ROUTE_SKIP_GIANT")) // Part 212: the 2024 / 2025 contests route every net
      fail("PD39ROUTE_SKIP_GIANT is for ISPD 2008 designs (that contest exempts nets above 1000 pins)");
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_PRICE_SEED"); v && *v) { // Part 210: file[,scale]
      std::string sv(v), path = sv;
      const size_t comma = sv.rfind(',');
      if (comma != std::string::npos) {
        path = sv.substr(0, comma);
        char *end = nullptr;
        const std::string sc = sv.substr(comma + 1);
        gSeedScale = std::strtod(sc.c_str(), &end);
        if (sc.empty() || *end || !std::isfinite(gSeedScale) || gSeedScale < 0 || gSeedScale > 1e6)
          fail("PD39ROUTE_PRICE_SEED must be <file>[,scale] with scale in [0, 1e6] (got '" + sv + "')");
      }
      if (g24Mode)
        fail("PD39ROUTE_PRICE_SEED is for ISPD 2008 designs (the present-demand cost); not for the 2024 / 2025 flows");
      priceSeedPath = path; // read with the design
    }
  });
  step([&] {
    if (const char *v = settings::get("PD7ROUTE_LA_PINS"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD7ROUTE_LA_PINS must be 0 or 1 (got '" + sv + "')");
      laPins = sv == "1";
    }
  });
  step([&] {
    {
      auto flag = [&](const char *name, bool &dst) {
        if (const char *v = settings::get(name); v) {
          std::string sv(v);
          if (sv != "0" && sv != "1")
            fail(std::string(name) + " must be 0 or 1 (got '" + sv + "')");
          dst = sv == "1";
        }
      };
      auto num = [&](const char *name, int lo, int &dst) {
        if (const char *v = settings::get(name); v) {
          char *end = nullptr;
          long x = std::strtol(v, &end, 10);
          if (end == v || *end != '\0' || x < lo || x > 1000000)
            fail(std::string(name) + " must be an integer >= " + std::to_string(lo) + " (got '" + v + "')");
          dst = static_cast<int>(x);
        }
      };
      flag("PD8ROUTE_REBUILD", rebuild.enabled);
      num("PD8ROUTE_REBUILD_MARGIN", 0, rebuild.margin);
      num("PD8ROUTE_REBUILD_MAX_PINS", 2, rebuild.maxPins);
    }
  });
  step([&] {
    {
      std::string err;
      if (!parseWlSettings(settings::get("PD7ROUTE_WL"), settings::get("PD7ROUTE_WL_PASSES"), settings::get("PD7ROUTE_WL_BEND"),
                           settings::get("PD7ROUTE_WL_MARGIN"), wl, err))
        fail(err);
    }
  });
  step([&] {
    if (const char *pv = settings::get("PD6ROUTE_PIECES"); pv) {
      std::string v(pv);
      if (v != "0" && v != "1")
        fail("PD6ROUTE_PIECES must be 0 or 1 (got '" + v + "')");
      refine.pieces = region.pieces = v == "1";
    }
  });
  // pd22route's own mechanisms: virtual capacity in the region stage, and
  // the fallback's paths on two threads.
  step([&] {
    {
      std::string err;
      if (!parseVirtualCap(settings::get("PD22ROUTE_VCAP"), settings::get("PD22ROUTE_VCAP_RADIUS"), region, err))
        fail(err);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_ABORT"); v) { // Part 146: above,rounds
      long long above = -1;
      int rounds = -1;
      char tail = 0;
      if (std::sscanf(v, "%lld,%d%c", &above, &rounds, &tail) != 2 || above < 0 || rounds < 1)
        fail(std::string("PD39ROUTE_REGION_ABORT must be above,rounds (above >= 0, rounds >= 1; got '") + v + "')");
      region.abortAbove = above;
      region.abortRounds = rounds;
      if (above > 0)
        std::cerr << "region abort: above " << above << " units of entering overflow, no gain by round " << rounds
                  << " -> stop\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_WALL_H"); v) { // Part 150: B[,from]
      int B = -1, from = 0;
      char tail = 0;
      const int n = std::sscanf(v, "%d,%d%c", &B, &from, &tail);
      if (!(n == 1 || n == 2) || B < 0 || B > 100000 || from < 0)
        fail(std::string("PD39ROUTE_REGION_WALL_H must be B[,from] (B 0-100000, from >= 0; got '") + v + "')");
      region.wallB = B;
      region.wallFrom = from;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_DAMP"); v) { // Part 144
      char *end = nullptr;
      const double x = std::strtod(v, &end);
      if (!*v || *end || !(x >= 0 && x <= 1))
        fail(std::string("PD39ROUTE_REGION_DAMP must be a number 0-1 (got '") + v + "')");
      region.damp = x;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_COOLDOWN"); v) { // Part 144
      char *end = nullptr;
      const long x = std::strtol(v, &end, 10);
      if (!*v || *end || x < 0 || x > 1000)
        fail(std::string("PD39ROUTE_REGION_COOLDOWN must be an integer 0-1000 (got '") + v + "')");
      region.cooldown = static_cast<int>(x);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_BATCH"); v && *v) { // Part 159: B[,spread]
      int B = -1, sp = 0, bx = 0;
      char tail = 0;
      const int n = std::sscanf(v, "%d,%d,%d%c", &B, &sp, &bx, &tail);
      if (!(n >= 1 && n <= 3) || B < 0 || B > 100000 || sp < 0 || sp > 100000 || (bx != 0 && bx != 1))
        fail(std::string("PD39ROUTE_REGION_BATCH must be B[,spread[,box]] (B 0-100000, spread 0-100000, box 0 or 1; "
                         "got '") + v + "')");
      region.batch = B;
      region.batchSpread = sp;
      region.batchBox = bx == 1;
      if (const char *u = settings::get("PD39ROUTE_REGION_BATCH_UNTIL"); u && *u) { // Part 160
        char *end = nullptr;
        const long k = std::strtol(u, &end, 10);
        if (*end || k < 0 || k > 1000000)
          fail(std::string("PD39ROUTE_REGION_BATCH_UNTIL must be a round number 0-1000000 (got '") + u + "')");
        region.batchUntil = static_cast<int>(k);
      }
      if (const char *a = settings::get("PD39ROUTE_REGION_BATCH_MAX"); a && *a) { // Part 165
        char *end = nullptr;
        const long long m = std::strtoll(a, &end, 10);
        if (*end || m < 0)
          fail(std::string("PD39ROUTE_REGION_BATCH_MAX must be a number of overflow units >= 0 (got '") + a + "')");
        region.batchMax = m;
      }
      if (const char *a = settings::get("PD39ROUTE_REGION_BATCH_ROUNDS"); a && *a) { // Part 163
        char *end = nullptr;
        const long f = std::strtol(a, &end, 10);
        if (*end || f < 0 || f > 100)
          fail(std::string("PD39ROUTE_REGION_BATCH_ROUNDS must be a round-budget factor 0-100 (got '") + a + "')");
        region.batchRounds = static_cast<int>(f);
      }
      if (const char *a = settings::get("PD39ROUTE_REGION_BATCH_ADAPT"); a && *a) { // Part 161
        char *end = nullptr;
        const long f = std::strtol(a, &end, 10);
        if (*end || f < 0 || f > 100)
          fail(std::string("PD39ROUTE_REGION_BATCH_ADAPT must be a round-budget factor 0-100 (got '") + a + "')");
        region.batchAdapt = static_cast<int>(f);
      }
      if (const char *a = settings::get("PD39ROUTE_REGION_BATCH_FALLBACK"); a && *a) { // Part 167
        char *end = nullptr;
        const long long gmin = std::strtoll(a, &end, 10);
        if (*end || gmin < 0)
          fail(std::string("PD39ROUTE_REGION_BATCH_FALLBACK must be a number of overflow units >= 0 (got '") + a +
               "')");
        region.batchFallback = gmin;
      }
      if (B > 0)
        std::cerr << "region stage batched (path A): " << B << " nets per batch, "
                  << (sp > 0 ? "spread over " + std::to_string(sp) + "-tile cells" : std::string("net order"))
                  << (region.batchBox ? ", box conflict rule" : "")
                  << (region.batchUntil > 0 ? ", rounds before " + std::to_string(region.batchUntil) + " only"
                                            : std::string())
                  << (region.batchRounds > 0 ? ", " + std::to_string(region.batchRounds) + "x the rounds"
                                             : std::string())
                  << (region.batchMax > 0 ? ", stages entering with at most " + std::to_string(region.batchMax) +
                                                " units of overflow only"
                                          : std::string())
                  << (region.batchAdapt > 0 ? ", adaptive: " + std::to_string(region.batchAdapt) +
                                                  "x the rounds, sequential once batched rounds stop improving"
                                            : std::string())
                  << (region.batchFallback > 0 ? ", rerun sequentially if the batched rounds gain less than " +
                                                     std::to_string(region.batchFallback) + " units"
                                               : std::string())
                  << "\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_A1"); v && *v) { // Part 157 (trace only): rounds r1,r2,...
      std::stringstream ss(v);
      std::string t;
      while (std::getline(ss, t, ',')) {
        char *end = nullptr;
        const long k = std::strtol(t.c_str(), &end, 10);
        if (t.empty() || *end || k < 0)
          fail(std::string("PD39ROUTE_REGION_A1 must be a list of round numbers r1,r2,... (got '") + v + "')");
        gRegionA1Rounds.push_back(static_cast<int>(k));
      }
      std::cerr << "region A1 replay (trace only) at rounds " << v << "\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_DUMP"); v && *v) { // Part 140 (trace only)
      gRegionDumpPrefix = v;
      outputOk(gRegionDumpPrefix + ".A.txt", "PD39ROUTE_REGION_DUMP");
      std::cerr << "region dump: " << gRegionDumpPrefix << ".<A|A<variant>|B>.txt\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_SEARCH_TRACE"); v && std::string(v) == "1") // Part 138
      gSearchTraceOn = true;
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_WL_FLOOR"); v) { // Part 138
      char *end = nullptr;
      const double x = std::strtod(v, &end);
      if (!*v || *end || !std::isfinite(x) || x < 0)
        fail(std::string("PD39ROUTE_REGION_WL_FLOOR must be a number >= 0 (got '") + v + "')");
      region.wlFloor = x;
      if (x > 0)
        std::cerr << "region WL floor: the region stage's WL term is max(NTHU's factor, " << x << ")\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_BOX_EVIDENCE"); v) { // Part 138
      char *end = nullptr;
      const long x = std::strtol(v, &end, 10);
      if (!*v || *end || x < 0 || x > 100000)
        fail(std::string("PD39ROUTE_REGION_BOX_EVIDENCE must be an integer 0-100000 (got '") + v + "')");
      region.evidenceStep = static_cast<int>(x);
      if (region.evidenceStep > 0)
        std::cerr << "region box evidence: each net's margin from " << region.boxInit << ", +" << region.evidenceStep
                  << " after a box-limited reroute that left it overflowed (to the region box cap)\n";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_LAGRANGE"); v) {
      char *end = nullptr;
      const double x = std::strtod(v, &end);
      if (!*v || *end || !std::isfinite(x) || x < 0)
        fail(std::string("PD39ROUTE_REGION_LAGRANGE must be a number >= 0 (got '") + v + "')");
      region.lagrange = x;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_REGION_OVER"); v) { // Part 179: the over-capacity cost's slack
      char *end = nullptr;
      const double x = std::strtod(v, &end);
      if (!*v || *end || !std::isfinite(x) || x < 0 || x > 1000)
        fail(std::string("PD39ROUTE_REGION_OVER must be a number 0-1000 (got '") + v + "')");
      region.overSlack = x;
    }
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_PORTFOLIO"); v && *v) {
      std::stringstream ss(v);
      std::string spec;
      while (std::getline(ss, spec, ';')) {
        RegionSettings probe = region;
        std::string err;
        if (!applyRegionVariant(spec, probe, err))
          fail("PD39ROUTE_PORTFOLIO: " + err);
        gPortfolio.push_back(spec);
      }
      if (const char *w = settings::get("PD39ROUTE_PORTFOLIO_WHEN"); w) {
        const std::string sw(w);
        if (sw != "after" && sw != "always" && sw != "speculative")
          fail("PD39ROUTE_PORTFOLIO_WHEN must be after, always or speculative (got '" + sw + "')");
        gPortfolioAfter = sw != "always";
        gPortfolioSpec = sw == "speculative";
      }
      if (const char *w = settings::get("PD39ROUTE_PORTFOLIO_DELAY"); w) { // Part 170
        char *end = nullptr;
        const double x = std::strtod(w, &end);
        if (end == w || *end != '\0' || !(x >= 0.0) || x > 1e9)
          fail(std::string("PD39ROUTE_PORTFOLIO_DELAY must be a number of seconds >= 0 (got '") + w + "')");
        if (x > 0.0 && !gPortfolioSpec)
          fail("PD39ROUTE_PORTFOLIO_DELAY needs PD39ROUTE_PORTFOLIO_WHEN=speculative");
        gPortfolioDelay = x;
      }
    }
  });
  // Settings that cannot run together -- checked after every region setting is parsed (Part 159: the
  // WALL_H / BOX_EVIDENCE check used to sit before BOX_EVIDENCE was read, so it never fired).
  step([&] {
    if (region.wallB > 0 && region.damp > 0) // Part 150: damping discounts edges below the table's minimum
      fail("PD39ROUTE_REGION_WALL_H cannot be combined with PD39ROUTE_REGION_DAMP (the bound would not hold)");
  });
  step([&] {
    if (region.wallB > 0 && region.evidenceStep > 0) // Part 150: its bound covers paths inside the box only
      fail("PD39ROUTE_REGION_WALL_H cannot be combined with PD39ROUTE_REGION_BOX_EVIDENCE (the box-limit proof "
           "needs a bound that also holds outside the box)");
  });
  step([&] {
    if (gSearchBidir) { // Part 197: both rest on the one-ended search's heuristic
      std::vector<std::string> specs = gPortfolio;
      specs.insert(specs.begin(), std::string());
      for (const std::string &spec : specs) {
        RegionSettings v = region;
        std::string err;
        if (!spec.empty() && !applyRegionVariant(spec, v, err))
          continue; // reported where the portfolio is parsed
        if (v.wallB > 0 || v.evidenceStep > 0)
          fail("PD39ROUTE_SEARCH_BIDIR cannot be combined with PD39ROUTE_REGION_WALL_H or PD39ROUTE_REGION_BOX_EVIDENCE"
               " (or the portfolio keys wall= / evidence=): both rest on the one-ended search's heuristic");
      }
    }
  });
  step([&] {
    if (region.batch > 0 && (region.evidenceStep > 0 || region.cooldown > 0 || region.damp > 0 || region.wallB > 0))
      fail("PD39ROUTE_REGION_BATCH cannot be combined with PD39ROUTE_REGION_BOX_EVIDENCE, _COOLDOWN, _DAMP or "
           "_WALL_H (they keep per-net state the batched round does not carry)");
  });
  step([&] {
    if (region.batch > 0 && !gRegionA1Rounds.empty())
      fail("PD39ROUTE_REGION_BATCH cannot be combined with PD39ROUTE_REGION_A1 (the replay measures the "
           "sequential round)");
  });
  step([&] {
    if (region.damp > 0 || region.cooldown > 0)
      std::cerr << "region damping: route-change discount " << region.damp << ", cooldown " << region.cooldown
                << " rounds\n";
  });
  step([&] {
    if (const char *v = settings::get("PD23ROUTE_VCAP_B"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD23ROUTE_VCAP_B must be 0 or 1 (got '" + sv + "')");
      vcapB = sv == "1";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD22ROUTE_FALLBACK_PARALLEL"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD22ROUTE_FALLBACK_PARALLEL must be 0 or 1 (got '" + sv + "')");
      fallbackParallel = sv == "1";
    }
  });
  // pd19route's own mechanism: the reclaim with a fallback.
  step([&] {
    if (const char *v = settings::get("PD19ROUTE_RECLAIM_FALLBACK"); v) {
      std::string sv(v);
      if (sv != "0" && sv != "1")
        fail("PD19ROUTE_RECLAIM_FALLBACK must be 0 or 1 (got '" + sv + "')");
      reclaimFallback = sv == "1";
    }
  });
  step([&] {
    {
      std::string err;
      if (!parseCostRel(settings::get("PD38ROUTE_REGION_COST_REL"), costRel, err))
        fail(err);
    }
  });
  step([&] {
    {
    std::string err;
    if (!parseReclaimSettings(nullptr, settings::get("PD16ROUTE_RECLAIM_RATIO"), settings::get("PD16ROUTE_RECLAIM_MIN_EXCESS"),
                              settings::get("PD16ROUTE_RECLAIM_MU"), settings::get("PD16ROUTE_RECLAIM_MARGIN"),
                              settings::get("PD16ROUTE_RECLAIM_PASSES"), reclaimFb, err) ||
        !parseReclaimAvoid(settings::get("PD17ROUTE_RECLAIM_AVOID"), reclaimFb, err))
      fail(err);
    }
  });
  step([&] {
    if (const char *v = settings::get("PD20ROUTE_FALLBACK_ORDER"); v) {
      std::string sv(v);
      if (sv != "both" && sv != "bfirst")
        fail("PD20ROUTE_FALLBACK_ORDER must be both or bfirst (got '" + sv + "')");
      bFirst = sv == "bfirst";
    }
  });
  step([&] {
    if (const char *v = settings::get("PD21ROUTE_FALLBACK_A_MAX"); v) {
      char *end = nullptr;
      long long x = std::strtoll(v, &end, 10);
      if (end == v || *end != '\0' || x < -1)
        fail(std::string("PD21ROUTE_FALLBACK_A_MAX must be an integer >= -1 (got '") + v + "')");
      aMax = x;
    }
  });
  step([&] {
    if (reclaimFallback && fallbackParallel && aMax < 0)
      fail("PD22ROUTE_FALLBACK_PARALLEL=1 needs PD21ROUTE_FALLBACK_A_MAX >= 0");
  });
  step([&] {
    {
    std::string err;
    if (!parseReclaimSettings(settings::get("PD16ROUTE_RECLAIM"), settings::get("PD16ROUTE_RECLAIM_RATIO"),
                              settings::get("PD16ROUTE_RECLAIM_MIN_EXCESS"), settings::get("PD16ROUTE_RECLAIM_MU"),
                              settings::get("PD16ROUTE_RECLAIM_MARGIN"), settings::get("PD16ROUTE_RECLAIM_PASSES"), reclaim,
                              err) ||
        !parseReclaimAvoid(settings::get("PD17ROUTE_RECLAIM_AVOID"), reclaim, err))
      fail(err);
    }
  });
  step([&] {
    forcedReport = settings::get("PD39ROUTE_FORCED_REPORT");
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_FORCED_REPORT_SIDE"); v && *v)
      forcedSide = settings::intValue("PD39ROUTE_FORCED_REPORT_SIDE", v);
  });
  step([&] {
    if (const char *v = settings::get("PD39ROUTE_FORCED_REPORT_HOTSPOTS"); v && *v)
      forcedSpots = settings::intValue("PD39ROUTE_FORCED_REPORT_HOTSPOTS", v);
  });
  step([&] {
    if (forcedSide < 1 || forcedSide > 64 || forcedSpots < 0 || forcedSpots > 1000)
      fail("PD39ROUTE_FORCED_REPORT_SIDE must be 1-64 and PD39ROUTE_FORCED_REPORT_HOTSPOTS 0-1000");
  });
  step([&] {
    if (forcedReport && *forcedReport && std::string(forcedReport) != "0" && std::string(forcedReport) != "1")
      outputOk(forcedReport, "PD39ROUTE_FORCED_REPORT");
  });
  step([&] {
    netTimes = settings::get("PD39ROUTE_NET_TIMES"); // Part 131
  });
  step([&] {
    if (netTimes)
      outputOk(netTimes, "PD39ROUTE_NET_TIMES");
  });
}

void RouterRun::readInputs() {
  gStages.begin(g24Mode ? "score (router's own)" : "real overflow"); // pd36route
  t0 = std::chrono::steady_clock::now();
  const bool settingsOk = problems.empty(); // the set-up below reads settings only when they are all valid
  bool read = false;
  if (g24Mode) {
    step([&] {
      readG24Streaming(inCap, inNet, align, g24In, d);
      read = true;
    });
    if (!read)
      return;
    g24Input = &g24In;
    if (settingsOk) {
      step([&] {
      if (gTiming.on) {
        auto relative = [&](const std::vector<double> &vals, const char *name, std::vector<double> &out) {
          if (vals.empty())
            return;
          if (static_cast<int>(vals.size()) != g24In.res.nLayers)
            fail(std::string(name) + " has " + std::to_string(vals.size()) + " values; the design has " +
                 std::to_string(g24In.res.nLayers) + " layers");
          const double mx = *std::max_element(vals.begin(), vals.end());
          if (!(mx > 0))
            fail(std::string(name) + ": at least one value must be positive");
          for (double x : vals)
            out.push_back(x / mx);
        };
        if (const char *sf = settings::get("PD39ROUTE_TIMING_SLACK"); sf) { // Part 134: routed slack
          // PD39ROUTE_TIMING_SLACK_MODE: replace (default) -- the file's slack only, unlisted nets
          // unclassed; min -- each net keeps its input slack, lowered to the file's where that is worse
          const char *mv = settings::get("PD39ROUTE_TIMING_SLACK_MODE");
          const std::string mode = mv ? mv : "replace";
          if (mode != "replace" && mode != "min")
            fail("PD39ROUTE_TIMING_SLACK_MODE must be replace or min (got '" + mode + "')");
          std::string err;
          std::vector<float> routed;
          if (!loadSlackOverride(sf, gTiming.slack.size(), routed, err))
            fail("PD39ROUTE_TIMING_SLACK: " + err);
          if (mode == "replace")
            gTiming.slack.swap(routed);
          else
            for (size_t i = 0; i < routed.size(); ++i)
              if (routed[i] == routed[i] && !(gTiming.slack[i] <= routed[i]))
                gTiming.slack[i] = routed[i];
          size_t listed = 0;
          for (float v : gTiming.slack)
            listed += v == v;
          std::cerr << "ISPD 2025 timing layers: routed slack for " << listed << " nets from " << sf << "\n";
        }
        relative(layerR, "PD39ROUTE_LAYER_R", gTiming.relR);
        relative(layerC, "PD39ROUTE_LAYER_C", gTiming.relC);
        gTiming.kp = powerK;
        for (int c = 1; c <= 3 && !timingK.empty(); ++c)
          gTiming.k[c] = timingK[static_cast<size_t>(c - 1)];
        double worst = gTiming.classify();
        // PD39ROUTE_TIMING_SCALE=weights: k times how much this design's score
        // values timing against overflow -- |w_TNS| / w_overflow x |worst
        // slack| (ns) -- relative to ariane's (100 / 3e-7 x 0.4687), where
        // 40, 80, 160 was tuned (Part 133). 1 (default): k as given.
        if (const char *km = settings::get("PD39ROUTE_TIMING_KMIN"); km) { // Part 133 calibration
          std::vector<double> v;
          if (!parseDoubleList(km, v) || v.size() != 1 || v[0] < 0)
            fail(std::string("PD39ROUTE_TIMING_KMIN must be one number >= 0 (got '") + km + "')");
          gTiming.kMin = v[0];
        }
        const char *sc = settings::get("PD39ROUTE_TIMING_SCALE");
        std::string scale = sc ? sc : "1";
        if (scale != "1" && scale != "weights")
          fail("PD39ROUTE_TIMING_SCALE must be 1 or weights (got '" + scale + "')");
        if (scale != "weights") {
          gTiming.applyFloor();
          worst = gTiming.classify();
        }
        if (scale == "weights") {
          if (!g24In.res.ppa || !(g24In.res.ppaWeight[3] > 0))
            fail("PD39ROUTE_TIMING_SCALE=weights needs a 2025 .cap with a positive overflow weight");
          const double ref = 100.0 / 3e-7 * 0.4687;
          // the worst slack even under PD39ROUTE_TIMING_REF: the reference
          // changes which nets are classed, not how strong the bias is
          const double f =
              std::fabs(g24In.res.ppaWeight[1]) / g24In.res.ppaWeight[3] * std::fabs(gTiming.worstSlack) * 1e9 / ref;
          for (int c = 1; c <= 3; ++c)
            gTiming.k[c] *= f;
          // the power term: w_power / w_overflow relative to ariane's (300 / 3e-7)
          const double fp = std::fabs(g24In.res.ppaWeight[2]) / g24In.res.ppaWeight[3] / (300.0 / 3e-7);
          gTiming.kp *= fp;
          gTiming.applyFloor();
          worst = gTiming.classify();
          std::cerr << "ISPD 2025 timing layers: strength scaled by the design's weights x " << f << " (timing), x " << fp
                    << " (power)\n";
        }
        std::cerr << "ISPD 2025 timing layers: " << (gTiming.refQ < 0 ? "worst" : "reference") << " slack " << worst
                  << " s; classes 1/2/3: " << gTiming.counts[1] << " / " << gTiming.counts[2] << " / " << gTiming.counts[3]
                  << " nets, k " << gTiming.k[1] << " / " << gTiming.k[2] << " / " << gTiming.k[3] << ", kp " << gTiming.kp << (gTiming.gate ? ", gated (no bias on a full edge)" : "") << "\n";
        if (const char *bv = settings::get("PD39ROUTE_TIMING_BENEFIT"); bv && *bv) { // Part 205: g[,M[,P]]
          std::vector<double> v;
          if (!parseDoubleList(bv, v) || v.empty() || v.size() > 4 || v[0] < 0 || v[0] > 4 ||
              (v.size() > 1 && !(v[1] >= 1 && v[1] <= 1e6)) || (v.size() > 2 && !(v[2] >= 0 && v[2] <= 1e6)) ||
              (v.size() > 3 && !(v[3] == -1 || (v[3] >= 0 && v[3] <= 1))))
            fail(std::string("PD39ROUTE_TIMING_BENEFIT must be g[,M[,P[,q]]] with g 0-4, M >= 1, P >= 0, q -1 or 0-1 "
                             "(got '") + bv + "')");
          if (v.size() > 3)
            gTiming.benefitQ = v[3];
          gTiming.benefitG = v[0];
          if (v.size() > 1)
            gTiming.benefitM = v[1];
          if (v.size() > 2)
            gTiming.benefitP = v[2];
          if (gTiming.benefitG > 0) {
            if (gTiming.hpwl.size() != gTiming.cls.size())
              fail("PD39ROUTE_TIMING_BENEFIT: the nets' geometry was not read with their slack");
            gTiming.setBenefit();
            std::cerr << "ISPD 2025 timing layers: benefit weighting, the timing term x (W / " << gTiming.benefitRef
                      << ")^" << gTiming.benefitG << " within [1/" << gTiming.benefitM << ", " << gTiming.benefitM
                      << "], W = (HPWL + " << gTiming.benefitP << " x sinks) x sinks, reference "
                      << (gTiming.benefitQ < 0 ? std::string("at an unchanged total pull")
                                               : "the " + std::to_string(gTiming.benefitQ) + " quantile")
                      << "; " << gTiming.benefitHi
                      << " classed nets at the upper bound, " << gTiming.benefitLo << " at the lower\n";
          }
        }
        if (const char *mf = settings::get("PD39ROUTE_TIMING_MULT"); mf && *mf) { // Part 207: a multiplier per net
          if (!gTiming.mult.empty())
            fail("PD39ROUTE_TIMING_MULT cannot be combined with PD39ROUTE_TIMING_BENEFIT (both set the timing term's "
                 "per-net multiplier)");
          std::string err;
          std::vector<float> m;
          size_t listed = 0;
          if (!loadTimingMult(mf, gTiming.cls.size(), m, listed, err))
            fail("PD39ROUTE_TIMING_MULT: " + err);
          size_t acting = 0;
          double lo = 1, hi = 1;
          for (size_t i = 0; i < m.size(); ++i)
            if (gTiming.cls[i] > 0 && m[i] != 1.0f) {
              ++acting;
              lo = std::min(lo, static_cast<double>(m[i]));
              hi = std::max(hi, static_cast<double>(m[i]));
            }
          gTiming.mult.swap(m);
          std::cerr << "ISPD 2025 timing layers: per-net multipliers from " << mf << ": " << listed << " nets listed, "
                    << acting << " classed nets with a multiplier other than 1 (" << lo << " to " << hi << ")\n";
        }
        if (const char *rf = settings::get("PD39ROUTE_TIMING_ROWS"); rf && *rf) { // Part 215: a whole row per net
          std::string err;
          const size_t L = static_cast<size_t>(g24In.res.nLayers);
          if (!loadTimingRows(rf, gTiming.cls.size(), L, gTiming.rowOf, gTiming.rowVals, err))
            fail("PD39ROUTE_TIMING_ROWS: " + err);
          gTiming.rowL = L;
          const size_t listed = gTiming.rowVals.size() / L;
          double lo = 0, hi = 0;
          if (listed) {
            lo = *std::min_element(gTiming.rowVals.begin(), gTiming.rowVals.end());
            hi = *std::max_element(gTiming.rowVals.begin(), gTiming.rowVals.end());
          }
          std::cerr << "ISPD 2025 timing layers: per-net rows from " << rf << ": " << listed << " nets, values " << lo
                    << " to " << hi << " (their class rows and multipliers replaced)\n";
        }
      }
      });
    } else {
      timingFilesSkipped = gTiming.on;
    }
    gViaDemand.dir.assign(g24In.res.nLayers, -1);
    for (int z = 1; z < g24In.res.nLayers; ++z)
      gViaDemand.dir[z] = g24In.res.layers[z].dir;
    std::cerr << "ISPD 2024 via demand in layer assignment: " << (gViaDemand.on ? "on" : "off") << "\n";
    if (gScore.on) {
      const g24::Resources &res = g24In.res;
      const RoutingGrid &gg = d.grid;
      gScore.unitLength = res.unitLength;
      gScore.xPitch = res.xPitch;
      gScore.yPitch = res.yPitch;
      gScore.w = res.overflowCost;
      gScore.unitVia = res.unitVia;
      // pd35route (memory): a table of the distinct capacities (0.0 for
      // layer 0 included) and a 1-byte index per edge when they fit.
      std::map<double, int> distinct{{0.0, 0}};
      for (double c : res.cap)
        distinct.emplace(c, 0);
      if (distinct.size() <= 256) {
        gScore.capTable.clear();
        for (auto &kv : distinct) {
          kv.second = static_cast<int>(gScore.capTable.size());
          gScore.capTable.push_back(kv.first);
        }
        gScore.capIdxH.assign(gg.hCount(), static_cast<uint8_t>(distinct[0.0]));
        gScore.capIdxV.assign(gg.vCount(), static_cast<uint8_t>(distinct[0.0]));
        for (int z = 1; z < res.nLayers; ++z) {
          for (int y = 0; y < gg.ny; ++y)
            for (int x = 0; x + 1 < gg.nx; ++x)
              gScore.capIdxH[gg.hIndex(x, y, z)] = static_cast<uint8_t>(distinct[res.capacity(z, x, y)]);
          for (int y = 0; y + 1 < gg.ny; ++y)
            for (int x = 0; x < gg.nx; ++x)
              gScore.capIdxV[gg.vIndex(x, y, z)] = static_cast<uint8_t>(distinct[res.capacity(z, x, y)]);
        }
        buildLossTable(); // pd36route
      } else {
        gScore.capH.assign(gg.hCount(), 0.0);
        gScore.capV.assign(gg.vCount(), 0.0);
        for (int z = 1; z < res.nLayers; ++z) {
          for (int y = 0; y < gg.ny; ++y)
            for (int x = 0; x + 1 < gg.nx; ++x)
              gScore.capH[gg.hIndex(x, y, z)] = res.capacity(z, x, y);
          for (int y = 0; y + 1 < gg.ny; ++y)
            for (int x = 0; x < gg.nx; ++x)
              gScore.capV[gg.vIndex(x, y, z)] = res.capacity(z, x, y);
        }
      }
    }
    std::cerr << "ISPD 2024 layer assignment cost: " << (gScore.on ? "score" : "nthu") << "\n";
    if (gScore2D.on)
      buildScore2D(g24In.res, d.grid);
    std::cerr << "ISPD 2024 3D pattern reroute passes: " << gPattern3D.passes << "\n";
    std::cerr << "ISPD 2024 2D cost: " << (gScore2D.on ? "score" : "pd")
              << (gScore2D.on && !gScore2D.hist ? " (no history)" : "") << "\n";
    // pd35route (memory): the per-GCell capacity table has been copied
    // into the Design, the score model and the 2D score model; nothing
    // reads it after setup (the solution writer needs only the layer count).
    std::vector<double>().swap(g24In.res.cap);
    std::cerr << "ISPD 2024 input: " << g24In.res.xSize << " x " << g24In.res.ySize << " x " << g24In.res.nLayers
              << ", " << g24In.nNets << " nets\n";
    memTrace("input and Design");
    gStages.mark("read input", nullptr, -1, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(d.nets.size()) + " nets");
  } else {
    step([&] {
      d = readGr(inGr);
      read = true;
    });
    if (!read)
      return;
    if (skipGiant) { // Part 212
        size_t k = dropGiantNets(d, gSkippedIds);
        std::cerr << "skip giant nets: " << k << " nets with more than " << kGiantNetPins
                  << " pins left unrouted (the contest exempts them; PD39ROUTE_SKIP_GIANT)\n";
    }
    gStages.mark("read input", nullptr, -1, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(d.nets.size()) + " nets");
  }
  step([&] { checkSegRange(d); }); // pd35route: 16-bit route records
  if (!twoDOnly)
    step([&] { checkIndexWidth(d.grid); }); // 32-bit indices (was checked before layer assignment)
  gLowMem = lowMem == "1" || (lowMem == "auto" && d.nets.size() > kLowMemNets);
  if (gLowMem)
    std::cerr << "low-memory mode: on (" << d.nets.size() << " nets)\n";
  if (!priceSeedPath.empty())
    step([&] {
      const std::string &path = priceSeedPath;
      std::string err;
      size_t listed = 0;
      if (!loadPriceSeed(path, d.grid, gSeedH, gSeedV, listed, err))
        fail("PD39ROUTE_PRICE_SEED: " + err);
      double mx = 0;
      size_t pos = 0;
      for (float x : gSeedH) {
        mx = std::max(mx, static_cast<double>(x));
        pos += x > 0;
      }
      for (float x : gSeedV) {
        mx = std::max(mx, static_cast<double>(x));
        pos += x > 0;
      }
      std::cerr << "price seed: " << listed << " edges listed in " << path << ", " << pos << " priced (max " << mx
                << "), scale " << gSeedScale << "\n";
    });
  if (laFrom)
    step([&] {
      rep.nets = readRouteAs2D(d, laFrom);
      prepareFor2DRefine(d, rep.nets);
    });
  if (!gPortfolio.empty())
    std::cerr << "late-stage portfolio: path A runs as " << gPortfolio.size() + 1
              << " variants on their own threads (variant 0 = the run's own settings)"
              << (gPortfolioSpec ? ", the others started beside it and kept only if path A ends above the "
                                   "fallback threshold (cancelled otherwise)" +
                                   (gPortfolioDelay > 0.0
                                        ? ", once path A has run " +
                                              std::to_string(static_cast<long long>(gPortfolioDelay + 0.5)) +
                                              " s (PD39ROUTE_PORTFOLIO_DELAY)"
                                        : std::string())
                  : gPortfolioAfter ? ", the others only if path A ends above the fallback threshold"
                                    : ", all from the start")
              << (gLowMem ? "; low-memory mode keeps a copy per variant while they run" : "") << "\n";
}

int RouterRun::work() {
  if (!slowTaskPath.empty()) { // Part 192 (trace only)
    gSlowTaskDump = std::fopen(slowTaskPath.c_str(), "w");
    if (!gSlowTaskDump)
      fail("PD39ROUTE_SLOW_TASKS: cannot write " + slowTaskPath);
  }
  if (!laFrom) {
    if (netTimes) { // Part 131: per-net time, by the input's net order
      gNetBase = d.nets.data();
      gNetTime.assign(d.nets.size(), std::array<float, 4>{0, 0, 0, 0});
    }
    rep = route(d, maxFastRounds, maxSlowRounds, epsilon, histStep, histMode, histFactor,
                streakThreshold, scaleByOverflow, relaxEnabled, ratioExponent, exponentEscalation,
                useMSMV, useSteinerize, useExactSteiner, maxExactPins, box, cost, accept, slowPieces, stopAt,
                &pattern);
  }
  // pd35route (memory): the slow tier's nets carry their vectors' growth
  // slack (the old full-copy snapshots used to compact them as a side
  // effect, ~28% of their size); compact them once here.
  for (RoutedNet &rn : rep.nets) {
    rn.pts.shrink_to_fit();
    rn.mst.shrink_to_fit();
    rn.segs.shrink_to_fit();
    rn.charges.shrink_to_fit();
  }
  detourTrace("slow tier", rep.nets);
  gStages.mark(laFrom ? "read 2D routes" : "escalation + slow tier", &rep.nets, laFrom ? -1 : rep.bestOverflow,
               std::numeric_limits<double>::quiet_NaN(),
               laFrom ? "" : std::to_string(rep.escalatedCount) + " nets escalated, " + std::to_string(rep.roundsRun) +
                                  " rounds in all");
  if (costRel > 0.0 && region.enabled && region.costWindow > 0) { // pd38route: the work gate scaled to the design
    long long before = gSearchPops; // this thread's count: every search so far
    long long scaled = relativeCostPops(region.costPops, costRel, before);
    std::cerr << "region work gate: " << before << " search expansions before the region stage, threshold "
              << region.costPops << " -> " << scaled << " (PD38ROUTE_REGION_COST_REL " << costRel << ")\n";
    region.costPops = scaled;
  }
  RegionReport regRep;
  RefineReport refRep;
  if (reclaimFallback) {
    FallbackReport fr = reclaimWithFallback(d, rep.nets, reclaimFb, region, refine, settings::get("VROUTE_TRACE") != nullptr,
                                            bFirst, aMax, fallbackParallel, vcapB);
    if (fr.parallel)
      std::cerr << "reclaim fallback: path B ran on a second thread (its per-round trace suppressed)\n";
    // Part 168: speculative variants were started and are cancelled rather than never run
    const std::string notRun = fr.portfolioCancelled ? "speculative variants cancelled (" +
                                                           std::to_string(fr.portfolioRunning) +
                                                           " still running when path A ended)"
                               : fr.portfolioNotStarted // Part 170
                                   ? std::string("speculative variants not started (path A ended within "
                                                 "PD39ROUTE_PORTFOLIO_DELAY; no copies made)")
                                   : std::string("variants not run");
    if (fr.portfolioSkipped && fr.regionA.aborted)
      std::cerr << "late-stage portfolio: path A's region stage stopped without gain (PD39ROUTE_REGION_ABORT), "
                << notRun << "\n";
    else if (fr.portfolioSkipped)
      std::cerr << "late-stage portfolio: path A ended at " << fr.overflowA << " <= " << aMax << ", " << notRun
                << "\n";
    if (gPortfolioSpec && !fr.portfolio.empty())
      std::cerr << "late-stage portfolio: variants ran beside path A ("
                << (fr.portfolioStartedAt < 0.0 ? std::string("started after it ended, ")
                    : gPortfolioDelay > 0.0       ? "started " + std::to_string(static_cast<long long>(fr.portfolioStartedAt + 0.5)) + " s into it, "
                                                  : std::string())
                << fr.portfolioRunning << " still running when path A ended)\n";
    for (size_t i = 0; i < fr.portfolio.size(); ++i) // Part 135
      std::cerr << "late-stage portfolio: variant " << i << " (" << fr.portfolio[i].spec << "): region "
                << fr.portfolio[i].region.before << " -> " << fr.portfolio[i].region.after << " in "
                << fr.portfolio[i].region.roundsRun << " rounds"
                << (fr.portfolio[i].region.fallbackFrom >= 0 // Part 167
                        ? " (sequential, after " + std::to_string(fr.portfolio[i].region.fallbackFrom) +
                              " batched rounds that reached only " +
                              std::to_string(fr.portfolio[i].region.fallbackBest) + ")"
                        : std::string())
                << ", then 2D overflow " << fr.portfolio[i].overflow
                << " (refinement: " << refineGains(fr.portfolio[i].refine) << ")"
                << ", wire " << fr.portfolio[i].wire
                << (fr.portfolio[i].region.threadsMax > 0
                        ? ", batched on " + std::to_string(fr.portfolio[i].region.threadsMin) + "-" +
                              std::to_string(fr.portfolio[i].region.threadsMax) + " threads"
                        : std::string())
                << (i == fr.portfolioPick ? "  <- kept" : "") << "\n";
    if (region.vcapTracks > 0)
      std::cerr << "region virtual capacity: path A " << fr.regionA.vcapEdges << " edges, path B "
                << fr.regionB.vcapEdges << " edges, " << region.vcapTracks << " tracks (" << fr.regionA.vcapReduction
                << " units), radius " << region.vcapRadius << "\n";
    std::cerr << "reclaim fallback: path A (standard) region " << fr.regionA.before << " -> " << fr.regionA.after
              << " (" << fr.regionA.roundsRun << " rounds, " << fr.regionA.seconds << "s"
              << (fr.regionA.fallbackFrom >= 0 // Part 167
                      ? "; sequential after " + std::to_string(fr.regionA.fallbackFrom) + " batched rounds, " +
                            std::to_string(static_cast<long long>(fr.regionA.fallbackSeconds)) + "s"
                      : std::string())
              << "), refinement -> "
              << fr.refineA.after << " (" << refineGains(fr.refineA) << "); path B (reclaim " << fr.reclaim.kept << " of " << fr.reclaim.candidates
              << ", 2D overflow " << fr.reclaim.overflowBefore << " -> " << fr.reclaim.overflowAfter << ") region "
              << fr.regionB.before << " -> " << fr.regionB.after << " (" << fr.regionB.roundsRun << " rounds, "
              << fr.regionB.seconds << "s), refinement -> " << fr.refineB.after << " ("
              << refineGains(fr.refineB) << ")\n";
    if (!fr.ranB)
      std::cerr << "reclaim fallback: A overflow " << fr.overflowA << " wire " << fr.wireA
                << " above PD21ROUTE_FALLBACK_A_MAX -> kept A (standard); B not run\n";
    else if (fr.ranA)
      std::cerr << "reclaim fallback: A overflow " << fr.overflowA << " wire " << fr.wireA << ", B overflow "
                << fr.overflowB << " wire " << fr.wireB << " -> kept " << (fr.tookB ? "B (reclaim)" : "A (standard)")
                << "\n";
    else
      std::cerr << "reclaim fallback: B overflow 0 wire " << fr.wireB << " -> kept B (reclaim); A not run\n";
    regRep = fr.tookB ? fr.regionB : fr.regionA;
    refRep = fr.tookB ? fr.refineB : fr.refineA;
    detourTrace("reclaim fallback (kept path)", rep.nets);
    gStages.mark("fallback (region + refinement, paths A/B)", &rep.nets, fr.tookB ? fr.overflowB : fr.overflowA,
                 std::numeric_limits<double>::quiet_NaN(),
                 std::string("kept ") + (fr.tookB ? "B" : "A") +
                     (fr.portfolio.empty() ? std::string()
                                           : " (portfolio of " + std::to_string(fr.portfolio.size()) + ", variant " +
                                                 std::to_string(fr.portfolioPick) + ")") +
                     "; region rounds A " +
                     std::to_string(fr.regionA.roundsRun) + ", B " + std::to_string(fr.regionB.roundsRun) +
                     "; expansions " + std::to_string(fr.regionA.pops + fr.regionB.pops));
  }
  // pd16route's own mechanism: the detour reclaim pass, before the region stage.
  if (!reclaimFallback) {
    if (reclaim.enabled) {
      ReclaimReport rr = reclaimDetours(d, rep.nets, reclaim);
      std::cerr << "detour reclaim: " << rr.kept << " of " << rr.candidates << " candidate reroutes kept ("
                << rr.avoided << " skipped near overflow), 2D wire "
                << rr.wireBefore << " -> " << rr.wireAfter << ", 2D overflow " << rr.overflowBefore << " -> "
                << rr.overflowAfter << " (" << rr.seconds << "s)\n";
      detourTrace("detour reclaim", rep.nets);
      gStages.mark("detour reclaim", &rep.nets, rr.overflowAfter, std::numeric_limits<double>::quiet_NaN(),
                   std::to_string(rr.kept) + " of " + std::to_string(rr.candidates) + " reroutes kept");
    }
  }
  // pd5route: hotspot rip-up first (if on), then refinement polishes.
  if (region.enabled && !reclaimFallback) {
    regRep = regionNegotiate(d, rep.nets, region, settings::get("VROUTE_TRACE") != nullptr);
    detourTrace("region stage", rep.nets);
    gStages.mark("region stage", &rep.nets, regRep.after, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(regRep.roundsRun) + " rounds, " + std::to_string(regRep.pops) + " expansions");
    std::cerr << "region: box x " << regRep.box.x0 << "-" << regRep.box.x1 << " y " << regRep.box.y0 << "-"
              << regRep.box.y1 << ", " << regRep.regionNets << " nets, 2D overflow " << regRep.before
              << " -> " << regRep.after << " (" << regRep.roundsRun << " rounds"
              << (regRep.plateaued ? ", plateau stop" : "") << (regRep.costStopped ? ", work-gated plateau stop" : "")
              << ", " << regRep.seconds << "s, " << regRep.pops << " search expansions"
              << (region.vcapTracks > 0 ? ", virtual capacity on " + std::to_string(regRep.vcapEdges) + " edges" : "")
              << ")\n";
  }
  if (refine.enabled && !reclaimFallback)
    refRep = refine2D(d, rep.nets, refine, settings::get("VROUTE_TRACE") != nullptr);
  if (refine.enabled && !reclaimFallback) {
    detourTrace("refinement", rep.nets);
    gStages.mark("refinement", &rep.nets, refRep.after, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(refRep.itersRun) + " iterations");
  }
  // pd8route: whole-net tree rebuild, before piece recovery polishes.
  if (rebuild.enabled) {
    RebuildReport rb = rebuild2D(d, rep.nets, wl.bend, rebuild, settings::get("VROUTE_TRACE") != nullptr);
    detourTrace("tree rebuild", rep.nets);
    gStages.mark("tree rebuild", &rep.nets, -1, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(rb.rebuilt) + " nets rebuilt");
    std::cerr << "tree rebuild: 2D wire " << rb.wireBefore << " -> " << rb.wireAfter << " (" << rb.rebuilt
              << " nets rebuilt)\n";
  }
  // pd7route: overflow-safe wirelength and bend recovery, last 2D stage.
  WlReport wlRep;
  if (wl.enabled) {
    wlRep = wlRecover2D(d, rep.nets, wl, settings::get("VROUTE_TRACE") != nullptr);
    detourTrace("WL recovery", rep.nets);
    gStages.mark("wirelength recovery", &rep.nets, -1, std::numeric_limits<double>::quiet_NaN(),
                 std::to_string(wlRep.piecesImproved) + " pieces improved");
    std::cerr << "wl recovery: 2D wire " << wlRep.wireBefore << " -> " << wlRep.wireAfter << " ("
              << wlRep.piecesImproved << " pieces improved)\n";
  }
  // Part 162: the forced-overflow report, on the final 2D routes (layer assignment frees them below)
  if (!g24Mode && !laFrom) {
    const char *fr = forcedReport;
    if (!(fr && std::string(fr) == "0")) {
      if (forcedOverflowReport(d, rep.nets, forcedSide, forcedSpots, fr && *fr && std::string(fr) != "1" ? fr : ""))
        gStages.mark("forced-overflow report", nullptr);
    }
  }
  ViaNegotiationReport viaRep;
  // pd35route (memory): layer assignment and everything after it read only
  // each net's segments and pins; its 2D charge log, spanning tree and pin
  // points are freed first.
  if (!twoDOnly) {
    for (RoutedNet &rn : rep.nets) {
      std::vector<ChargeRecord>().swap(rn.charges);
      std::vector<std::pair<int, int>>().swap(rn.mst);
      std::vector<Point>().swap(rn.pts);
    }
    releaseFreedMemory();
    memTrace("freeing the 2D-only net data", &rep.nets);
  }
  if (!twoDOnly)
    viaRep = negotiateViaAssignment(d.grid, rep.nets, viaRounds, viaPatience, viaWeight, viaKnobs,
                                     wireKnobs, laMode, laPins);
  memTrace("layer assignment", &rep.nets);
  if (g24Mode) {
    g24::SolutionReport sr = writeG24Solution(outPath, g24In, d.grid, rep.nets);
    std::cerr << (g24In.res.ppa ? "ISPD 2025 solution (DEF units): " : "ISPD 2024 solution: ") << sr.nets
              << " nets, " << sr.wires << " wires, " << sr.vias << " vias, " << sr.pinFixes << " pins given a via, "
              << sr.unroutedNets << " single-GCell nets given a via stack, " << sr.droppedZero
              << " zero-length pieces dropped";
    if (g24In.res.ppa) // Part 125: the evaluator rejects bottom-layer wires
      std::cerr << ", " << sr.layer0Wires << " bottom-layer wires; PPA weights " << g24In.res.ppaWeight[0] << " "
                << g24In.res.ppaWeight[1] << " " << g24In.res.ppaWeight[2] << " " << g24In.res.ppaWeight[3];
    std::cerr << "\n";
  } else {
    writeSolution(outPath, d.grid, rep.nets);
  }
  gStages.mark("write solution", nullptr);
  if (const char *path = netTimes; path && !gNetTime.empty()) { // Part 131
    std::FILE *f = std::fopen(path, "wb");
    if (!f)
      fail(std::string("PD39ROUTE_NET_TIMES: cannot write ") + path);
    const uint64_t n = gNetTime.size();
    bool ok = std::fwrite("GRNT1\n", 1, 6, f) == 6 && std::fwrite(&n, sizeof n, 1, f) == 1 &&
              std::fwrite(gNetTime.data(), sizeof(std::array<float, 4>), gNetTime.size(), f) == gNetTime.size();
    if (std::fclose(f) != 0 || !ok)
      fail(std::string("PD39ROUTE_NET_TIMES: write failed: ") + path);
    std::cerr << "per-net times: " << n << " nets written to " << path << "\n";
  }
  gStages.print(std::cerr);
  auto t1 = std::chrono::steady_clock::now();

  if (!gSeedH.empty()) // Part 210 (trace): evidence the seed acted
    std::cerr << "price seed: " << gSeedReads.load() << " wire-cost reads paid a seeded price\n";
  if (gSearchBidir) // Part 197 (trace)
    std::cerr << "two-ended search: " << gBidirSearches.load() << " searches, " << gBidirFwd.load()
              << " nodes settled from the sources, " << gBidirBwd.load() << " from the sinks\n";
  std::cerr << "routed " << rep.nets.size() << " nets, " << rep.escalatedCount
            << " escalated to the slow tier\n";
  // "2D overflow" here is still the 2D-collapsed search's own convergence
  // metric (measured against the summed cross-layer capacity, Usage::capH/
  // capV) -- it is what the 2D pricing loop actually converges against, and
  // remains meaningful as that. It is a different number from the real
  // per-layer total overflow (wire + via) the negotiation loop's own report
  // tracks below, which is what gr_verify reports on this file's actual
  // output (unless FR1ROUTE_TWOD_ONLY is set, in which case the output is
  // fr4route's old single-layer one and neither number below is comparable,
  // same as every prior generation).
  if (laFrom) {
    std::cerr << "layer assignment only (PD3ROUTE_LA_FROM=" << laFrom << "), "
              << (laMode == LaMode::kNthu ? "nthu" : "inherited") << " mode\n";
  } else {
    std::cerr << "2D overflow" << (twoDOnly ? " (FR1ROUTE_TWOD_ONLY set: no layer assignment ran)" : "")
              << ": " << rep.bestOverflow << " (" << rep.roundsRun << " total rounds run)\n";
    std::cerr << (rep.convergedToZero ? "converged to zero overflow\n"
                                       : rep.stalled ? "stalled: no improvement for "
                                                         "several rounds, stopping\n"
                                                     : "hit the round cap\n");
  }
  if (refine.enabled)
    std::cerr << "refinement: 2D overflow " << refRep.before << " -> " << refRep.after << " ("
              << refRep.itersRun << " iterations run" << (refRep.plateaued ? ", plateau stop" : "")
              << (refRep.lowStopped ? ", PD39ROUTE_REFINE_STOP" : "") << ", last gain at iteration "
              << refRep.lastGain << ", longest wait before a gain " << refRep.longestWait << ", " << refRep.seconds
              << "s)\n";
  if (refine.enabled && refRep.evictBefore >= 0) // Part 191
    std::cerr << "refine evict: 2D overflow " << refRep.evictBefore << " -> " << refRep.evictAfter << " ("
              << refRep.evictKept << " of " << refRep.evictTried << " chains kept, " << refRep.evictMoved
              << " blockers moved, +" << refRep.evictWire << " wire)\n";
 if (piecesTotal > 0)
    std::cerr << "piece reroutes: " << piecesTotal << ", by L/Z pattern (no maze): " << patternedTotal << "\n";
  if (!twoDOnly) {
    std::cerr << "real (wire+via) overflow: " << viaRep.bestOverflow << " (best at round "
              << viaRep.bestRound << " of " << viaRep.roundsRun << " run)\n";
    std::cerr << (viaRep.convergedToZero
                      ? "via negotiation converged to zero overflow\n"
                      : viaRep.stalled ? "via negotiation stalled: no improvement for "
                                          "several rounds, stopping\n"
                                       : "via negotiation hit the round cap\n");
  }
  std::cerr << std::chrono::duration<double>(t1 - t0).count() << "s\n";
  return 0;
}

// Every problem found before the work, numbered, each in full (a reader's report keeps its own lines); exit status 2
// for a command line that names no run, 1 otherwise. Nothing was routed and no solution written.
int RouterRun::report() const {
  std::vector<std::string> seen; // a setting checked twice (before and with the design) is reported once
  for (const std::string &p : problems)
    if (std::find(seen.begin(), seen.end(), p) == seen.end())
      seen.push_back(p);
  std::cerr << "error: " << seen.size() << (seen.size() == 1 ? " problem" : " problems")
            << " found before any work; nothing was routed and no solution was written:\n";
  for (size_t i = 0; i < seen.size(); ++i) {
    std::stringstream ss(seen[i]);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
      std::cerr << (first ? "  " + std::to_string(i + 1) + ". " : std::string(4 + std::to_string(i + 1).size(), ' '))
                << line << "\n";
      first = false;
    }
  }
  if (timingFilesSkipped)
    std::cerr << "(the files the timing settings name -- PD39ROUTE_TIMING_SLACK, _MULT, _ROWS -- are checked against the "
                 "design once every setting is valid)\n";
  if (!commandLineOk)
    std::cerr << "usage: " << prog << " <benchmark.gr> <solution.route>\n"
              << "       " << prog << " <design.cap> <design.net> <solution>   (ISPD 2024 / 2025)\n"
              << "       " << prog << " --selftest | --settings\n";
  return commandLineOk ? 1 : 2;
}

int RouterRun::run(int argc, char **argv) {
  gr_io::failThrows() = true; // until the work starts, a problem is recorded, not the end of the program
  // what the settings and the readers print about themselves is held back: shown as before if the run goes on, left
  // out of the problem report if it stops
  std::stringstream held;
  std::streambuf *const err = std::cerr.rdbuf(held.rdbuf());
  commandLine(argc, argv);
  for (const std::string &p : settings::problems(environ))
    problems.push_back(p);
  readSettings();
  if (commandLineOk)
    readInputs();
  std::cerr.rdbuf(err);
  gr_io::failThrows() = false;
  if (!problems.empty())
    return report();
  std::cerr << held.str() << std::flush;
  return work();
}
