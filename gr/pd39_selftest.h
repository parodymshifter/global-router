// pd39_selftest.h -- pd39route's self-test (./pd39route --selftest): every test, then selftest::run().
// Part of pd39route.cpp, split out of it in R3 of the 2026-11-24 code review (HISTORY_COST_SWEEP.md Part 216): it is
// included once, at its old place near the end of pd39route.cpp, and is not a standalone header -- it uses the code
// above that point. pd39route still builds with its one-line g++ command; the routes are unchanged.
#pragma once

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

namespace selftest {

int failures = 0;
void check(bool cond, const std::string &what) {
  std::cout << (cond ? "  ok   " : "  FAIL ") << what << "\n";
  if (!cond)
    ++failures;
}

Design parse(const std::string &path, const char *text) {
  std::ofstream out(path);
  out << text;
  out.close();
  return readGr(path);
}

bool segsConnected(const std::vector<Seg> &segs) {
  std::vector<long long> nodes;
  std::vector<std::pair<long long, long long>> links;
  auto key = [&](int x, int y, int l) {
    return (static_cast<long long>(l) * 100000 + y) * 100000 + x;
  };
  for (const Seg &s : segs) {
    bool sameLayer = s.l1 == s.l2;
    if (sameLayer && s.x1 == s.x2) {
      for (int y = std::min(s.y1, s.y2); y <= std::max(s.y1, s.y2); ++y)
        nodes.push_back(key(s.x1, y, s.l1));
      for (int y = std::min(s.y1, s.y2); y < std::max(s.y1, s.y2); ++y)
        links.emplace_back(key(s.x1, y, s.l1), key(s.x1, y + 1, s.l1));
    } else if (sameLayer && s.y1 == s.y2) {
      for (int x = std::min(s.x1, s.x2); x <= std::max(s.x1, s.x2); ++x)
        nodes.push_back(key(x, s.y1, s.l1));
      for (int x = std::min(s.x1, s.x2); x < std::max(s.x1, s.x2); ++x)
        links.emplace_back(key(x, s.y1, s.l1), key(x + 1, s.y1, s.l1));
    } else {
      nodes.push_back(key(s.x1, s.y1, s.l1));
      nodes.push_back(key(s.x1, s.y1, s.l2));
      links.emplace_back(key(s.x1, s.y1, s.l1), key(s.x1, s.y1, s.l2));
    }
  }
  std::sort(nodes.begin(), nodes.end());
  nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
  if (nodes.empty())
    return true;
  std::vector<int> parent(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  auto idx = [&](long long k) {
    return static_cast<int>(std::lower_bound(nodes.begin(), nodes.end(), k) - nodes.begin());
  };
  for (auto &lk : links) {
    int a = find(idx(lk.first)), b = find(idx(lk.second));
    if (a != b)
      parent[a] = b;
  }
  int root = find(0);
  for (size_t i = 0; i < nodes.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      return false;
  return true;
}

const char *kBasicGr = R"(grid 4 4 2
vertical capacity 0 2
horizontal capacity 2 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 3
netA 0 2 1
5 5 1
35 5 1
netD 1 3 1
5 5 1
35 5 1
5 35 1
netE 2 2 1
35 35 1
5 5 1
0
)";

void testBasicShapesStayOnFastTier() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 3, "all three basic nets produced a route");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design");
  check(rep.escalatedCount == 0,
        "nothing escalates when the fast tier alone already succeeds");
  check(segsConnected(rep.nets[0].segs), "netA (straight run) is one component");
  check(segsConnected(rep.nets[1].segs), "netD (branch point) is one component");
  check(segsConnected(rep.nets[2].segs), "netE (high-to-low bend) is one component");
}

// A net whose only legal path needs two layer changes mid-route, avoiding
// two blockages on two different layers that don't overlap in position:
// layer 1 blocked at columns 3-4, layer 3 blocked at columns 6-7, spanning
// columns 0-9. No single fixed layer clears the whole span (that's the
// point -- unlike mroute's own self-test, where one layer happened to be
// fully open and a fixed-layer-per-run router could have found it by
// accident). The fast tier's routeNetFast can only pick one layer for the
// whole run and will land on ~1e9 forbidden-price cost no matter which it
// picks, leaving this net legally unresolvable by the fast tier alone --
// this is exactly what escalation exists to catch. The slow tier can zigzag
// layer1[0-3] -> hop -> layer3[3-6] -> hop -> layer1[6-9], clearing both
// blockages, at the cost of extra vias through the layer-2 via chain.
// vroute's own testEscalationSolvesWhatFastTierCannot is removed here: its
// fixture (kZigzagGr) specifically required a multi-layer detour -- two
// different real layers each blocked over a different x-range, so no
// single fixed layer choice clears the whole span -- which this file's 2D
// collapse (exactly one synthetic layer, summing every real layer's
// capacity into it) makes structurally inapplicable. There is no longer a
// "which layer" question for the fast tier to get stuck on. See
// HISTORY_COST_SWEEP.md's Part 21 for the fr1route entry documenting this
// and every other test removed for the same reason.

// The decisive claim this file's whole mechanism rests on: given several
// simultaneous sources, multiSourceDijkstra must find a path from whichever
// one is actually cheapest to reach a sink from -- not just the first
// source in the list, and not some fixed "primary" source with the rest
// treated as an afterthought. Coordinates here are tile indices (as the
// router sees them internally, after gr_io.h's own real-coordinate ->
// tile conversion, not the raw .gr file's micron values), matching
// kBasicGr's 4x4-tile grid: a "far" source at tile (0,0) standing in for
// where a pin might sit, and a "near" one at tile (2,0) -- standing in for
// a point somewhere in the middle of already-committed wire, which is
// exactly what netFootprint() feeds this function with during real net
// growth. A sink at tile (3,0) is 3 tiles from the far source but only 1
// from the near one.
void testMultiSourceDijkstraUsesCheapestSourceNotFirstInList() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;

  std::vector<PathNode> sources = {{0, 0, 0}, {2, 0, 0}}; // far, then near
  std::vector<Point> sinks = {{3, 0}};
  MSMVResult r = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(r.found, "a path exists between the sources and the sink (sanity)");
  check(r.path.front().x == 2 && r.path.front().y == 0,
        "the search used the near source (tile 2,0), not the far one "
        "(tile 0,0) listed first -- proves genuine multi-source selection "
        "by real cost, not list order");
  check(r.path.size() == 2,
        "the path length (2 grid points, tiles 2..3) matches the near "
        "source's 1-tile distance, not the far source's 3-tile one");
}

// The mirror claim on the sink side: given several simultaneous sinks, the
// search must stop at whichever is actually cheapest to reach -- exactly
// "which remaining pin is cheapest to connect next," the real question
// MSMV net growth asks every step. A single source at tile (0,0); a "far"
// sink at tile (3,0) listed first and a "near" sink at tile (1,0) listed
// second.
void testMultiSourceDijkstraStopsAtCheapestSink() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;

  std::vector<PathNode> sources = {{0, 0, 0}};
  std::vector<Point> sinks = {{3, 0}, {1, 0}}; // far, then near
  MSMVResult r = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(r.found, "a path exists between the source and a sink (sanity)");
  check(r.sinkIndex == 1,
        "the search stopped at the near sink (index 1, tile 1,0), not the "
        "far one listed first -- proves genuine multi-sink selection by "
        "real cost, not list order");
  check(r.path.size() == 2,
        "the path length (2 grid points, tiles 0..1) matches the near "
        "sink's 1-tile distance, not the far sink's 3-tile one");
}

// Ported from mroute.cpp's testPriceClampPreventsInfinity: a single-layer,
// single-track (capacity 1) corridor with a very wide net (minWidth 1000)
// blows usage/capacity to ~1000 the moment the net's own first commit
// charges it, sending negotiateWire's price toward literal infinity within
// a handful of rounds. With only one layer and one possible path, the fast
// tier can never legalize this net either, so it escalates to the slow
// tier -- exercising the exact code path (a real Dijkstra search relaxing a
// poisoned edge) where mroute's own version of this bug turned into a
// process-ending fail(). Proves the clamp ported into this file's
// negotiateWire actually prevents the same failure here, not just in
// mroute.cpp.
const char *kPriceOverflowGr = R"(grid 5 1 1
vertical capacity 0
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netWide 0 2 1000
5 5 1
45 5 1
0
)";

void testPriceClampPreventsInfinity() {
  Design d = parse("vroute_selftest_priceoverflow.gr", kPriceOverflowGr);
  RunReport rep = route(d, 5, 5, 0.5, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1,
        "netWide still finds a route across multiple rounds on both tiers "
        "even though its own width alone drives its one shared edge far "
        "enough past capacity to overflow price to infinity without the "
        "clamp");
  check(segsConnected(rep.nets[0].segs), "netWide is one connected component");
}

// ---------------------------------------------------------------------------
// History-cost mechanism tests: the decisive behavioral change this file
// adds over hroute, plus the arithmetic edge cases CLAUDE.md's testing
// section requires for any formula that can legitimately see a zero.
// ---------------------------------------------------------------------------

// Two parallel horizontal corridors (layer 0 and layer 1) between the same
// two endpoints. This reproduces, directly against bestHLayer -- the real
// production cost function every fast-tier route decision goes through --
// exactly the plateau dynamic ZERO_OVERFLOW_CONVERGENCE_EXPERIMENT.md
// diagnosed: layer 0 was over capacity for a while, a net detoured off it,
// and its usage now reads comfortably clear. Layer 1 was never over
// capacity but has quietly kept the current-round pricing (lambda) climbing
// this whole time for unrelated reasons (other nets still contending for
// it). Under hroute's pricing alone (no history, or equivalently
// VROUTE_HIST_STEP=0) layer 0's frozen-but-lower lambda wins every time,
// even though nothing about the grid says returning to it is actually
// safe -- the exact "drift back" failure this file exists to fix. With
// permanent history accumulated against layer 0 from its earlier overflow,
// the same comparison flips.
// vroute's own testHistoryCostPreventsDriftBackToPreviouslyCongestedEdge
// is removed here: its whole premise was comparing bestHLayer's choice
// between two different *real* layers (one recently congested and now
// clear, one steadily moderate) -- a comparison this file's 2D collapse
// makes inapplicable, since there is only ever one synthetic layer to
// choose (LayerPlan::hLayers/vLayers are both {0}). The history-relax
// mechanism itself remains fully covered by the direct-arithmetic tests
// below (testRelaxationUndoesExactlyOneStepPerCleanRound and its
// siblings), which exercise updateHistory's own real production code, not
// a layer-choice comparison built on top of it.

// CLAUDE.md's arithmetic-edge-case section requires a test for every
// formula that can see a degenerate input, not just the happy path.
// updateHistory has no division (a plain usage > capacity comparison), so
// there is no capacity_e == 0 divide-by-zero to reproduce here the way
// priceExponentTerm has one -- but a zero/negative capacity edge is still a
// degenerate case worth a named test, because the *wrong* answer (silently
// accumulating history against an edge that was never usable) would still
// compound every round for the rest of the run without ever crashing,
// exactly the "wrong code still runs and still produces a route, just a
// worse one" failure mode CLAUDE.md warns about. Adapted for this file's
// own 2D collapse: kBasicGr's layer 1 alone being blocked (hcap 0) is not
// enough here, since the *summed* 2D capacity is layer 0's nonzero value
// plus layer 1's zero -- a genuinely nonzero 2D edge. The fixture needs
// every real layer blocked so the 2D-collapsed capacity itself is zero.
const char *kZeroCap2dGr = R"(grid 4 4 2
vertical capacity 0 0
horizontal capacity 0 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
15 5 1
0
)";

void testHistoryIgnoresZeroCapacityEdges() {
  Design d = parse("fr1route_selftest_zerocap2d.gr", kZeroCap2dGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  size_t idx = g.hIndex(0, 0, 0);
  check(u.capH[idx] <= 0,
        "fixture edge's 2D-collapsed capacity really is zero -- every real "
        "layer is blocked, not just one, so the sum genuinely vanishes");
  u.h[idx] = 999; // absurdly "over capacity" if compared naively
  updateHistory(g, pr, u, 1.0, HistMode::kAdditive, 1.0);
  check(pr.histH[idx] == 1.0,
        "a zero-2D-capacity edge's history term is never grown, matching "
        "negotiateWire's own treatment of the same edges -- nothing "
        "meaningful to accumulate against an edge that was never usable");
}

// The mechanism's whole point is that it only ever moves one direction.
// VROUTE_HIST_RELAX=0 must reproduce iroute's exact permanent behavior --
// this is the regression guard for that claim, not the relaxation
// mechanism itself (see testRelaxationUndoesExactlyOneStepPerCleanRound and
// testRelaxationFullyRestoresBaselineAfterEqualCleanRounds below for that).
void testDisablingRelaxReproducesPermanentBehavior() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5; // over capacity
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, 1, false, /*relaxEnabled=*/false);
  double afterOver = pr.histH[idx];
  check(afterOver > 1.0, "history grew while the edge was over capacity");

  Usage under(g);
  under.h[idx] = 0; // now comfortably clear
  updateHistory(g, pr, under, 1.0, HistMode::kAdditive, 1.0, 1, false, /*relaxEnabled=*/false);
  check(pr.histH[idx] == afterOver,
        "with VROUTE_HIST_RELAX=0, history never decreases even after usage "
        "drops back under capacity -- reproduces iroute's exact permanent "
        "behavior, the fallback this file's relaxation mechanism can always "
        "be switched off back to");
}

// The actual new mechanism: an edge dirty for exactly one round, then one
// clean round, should have its single growth operation popped and undone
// exactly -- back to the 1.0 no-op baseline, not merely "decreased."
void testRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5; // over capacity
  updateHistory(g, pr, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows history to exactly 1.05");

  Usage under(g);
  under.h[idx] = 0; // clear
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- history "
        "is back to the 1.0 no-op baseline, not just 'lower than before', "
        "the decisive behavioral change this file's relaxation mechanism "
        "is supposed to buy");
}

// The full claim from this file's header: an edge dirty for N rounds needs
// exactly N consecutive clean rounds to fully return to baseline -- not
// more, not less, and a partial run of clean rounds should leave a
// partial, LIFO-consistent penalty behind (proving the stack, not just a
// running total, is actually doing the work).
void testRelaxationFullyRestoresBaselineAfterEqualCleanRounds() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5;
  Usage under(g);
  under.h[idx] = 0;

  // Three dirty rounds: 1.0 -> 1.05 -> 1.10 -> 1.15.
  for (int i = 0; i < 3; ++i)
    updateHistory(g, pr, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.15) < 1e-9,
        "three dirty rounds at step=0.05 grow history to exactly 1.15");

  // Two clean rounds should only partially unwind it -- LIFO pops two of
  // the three pushed 0.05 increments, leaving exactly one behind (1.05),
  // not an averaged or proportional decay to some other value.
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.05) < 1e-9,
        "two clean rounds after three dirty ones leaves exactly one "
        "increment (1.05), not fully relaxed yet -- proves this is a LIFO "
        "stack of individual operations, not a running total that would "
        "have no way to represent a partial unwind");

  // The third consecutive clean round finishes the job exactly.
  updateHistory(g, pr, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(pr.histH[idx] - 1.0) < 1e-9,
        "the third consecutive clean round -- matching the three dirty "
        "rounds that earned the penalty -- fully restores the 1.0 no-op "
        "baseline exactly, per this file's header claim");
}

// VROUTE_HIST_STREAK: growth must be gated behind N *consecutive*
// over-capacity round boundaries, and a single clear round must reset the
// count to zero rather than merely pausing it -- otherwise an edge that
// flickers over/under/over capacity every other round would still
// eventually accumulate growth from unrelated non-consecutive rounds,
// which defeats the entire point of requiring a *streak*.
void testHistoryStreakRequiresConsecutiveOvercapacityRounds() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 5;
  Usage under(g);
  under.h[idx] = 0;

  const int streakThreshold = 3;
  // Two consecutive over-capacity rounds -- short of the threshold.
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] == 1.0,
        "two consecutive over-capacity rounds is still short of a streak "
        "threshold of 3: no growth yet");

  // A single clear round in between breaks the streak entirely.
  updateHistory(g, pr, under, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] == 1.0,
        "2 over + 1 clear + 2 over never reaches 3 *consecutive* "
        "over-capacity rounds -- the interruption resets the streak, so "
        "history still hasn't grown even though 4 of 5 rounds were over "
        "capacity");

  // The third consecutive over-capacity round finally unlocks growth.
  updateHistory(g, pr, over, 1.0, HistMode::kAdditive, 1.0, streakThreshold, false);
  check(pr.histH[idx] > 1.0,
        "the 3rd consecutive over-capacity round reaches the streak "
        "threshold and history finally grows");
}

// VROUTE_HIST_SCALE=overflow: an edge sitting further over capacity should
// accumulate history faster than one just barely over, unlike the default
// flat step which treats "1 unit over" and "10x over" identically.
void testHistoryScalesWithOverflowMagnitude() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices prFlat(g), prScaled(g);
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage barelyOver(g);
  barelyOver.h[idx] = 3; // capacity 2 -> ratio 1.5x
  Usage wayOver(g);
  wayOver.h[idx] = 20; // capacity 2 -> ratio 10x

  updateHistory(g, prFlat, barelyOver, 1.0, HistMode::kAdditive, 1.0, 1, false);
  double flatBarely = prFlat.histH[idx];
  Prices prFlat2(g);
  updateHistory(g, prFlat2, wayOver, 1.0, HistMode::kAdditive, 1.0, 1, false);
  double flatWayOver = prFlat2.histH[idx];
  check(flatBarely == flatWayOver,
        "flat scaling (the default) grows a barely-over edge and a "
        "way-over edge by the identical step -- it does not distinguish "
        "overflow magnitude at all");

  updateHistory(g, prScaled, barelyOver, 1.0, HistMode::kAdditive, 1.0, 1, true);
  double scaledBarely = prScaled.histH[idx];
  Prices prScaled2(g);
  updateHistory(g, prScaled2, wayOver, 1.0, HistMode::kAdditive, 1.0, 1, true);
  double scaledWayOver = prScaled2.histH[idx];
  check(scaledWayOver > scaledBarely,
        "VROUTE_HIST_SCALE=overflow makes the 10x-over edge accumulate "
        "history faster than the 1.5x-over edge in the very same round -- "
        "the decisive behavioral change this mode is supposed to buy");
  check(scaledWayOver > flatWayOver,
        "the way-over edge grows faster under overflow-scaling than it "
        "would have under the flat default");
}

// VROUTE_HIST_MODE=ratio: histH *= usage/capacity directly, no separate
// step or factor. A user-proposed formula, n2 = n1*(1 + overflow_percent/100),
// which is algebraically exactly usage/capacity when "overflow_percent" is
// the excess over capacity as a percentage (e.g. usage=150,cap=100 -> 50%
// -> 1 + 0.5 = 1.5 = 150/100).
void testHistoryRatioModeMultipliesByUsageOverCapacityExactly() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices pr(g);
  Usage fiftyOver(g);
  fiftyOver.h[idx] = 3; // capacity 2 -> ratio exactly 1.5
  updateHistory(g, pr, fiftyOver, /*histStep=*/0.0, HistMode::kRatio, /*histFactor=*/0.0);
  check(std::abs(pr.histH[idx] - 1.5) < 1e-9,
        "VROUTE_HIST_MODE=ratio on a 50%-over edge (usage 3, capacity 2) "
        "multiplies history by exactly 1.5 -- histStep/histFactor play no "
        "role at all in this mode, only the live usage/capacity ratio");

  Usage tenXOver(g);
  tenXOver.h[idx] = 20; // capacity 2 -> ratio exactly 10.0
  updateHistory(g, pr, tenXOver, 0.0, HistMode::kRatio, 0.0);
  check(std::abs(pr.histH[idx] - 15.0) < 1e-9,
        "a second round at 10x capacity multiplies the already-1.5 history "
        "by exactly 10 more (1.5*10=15) -- confirms this mode compounds "
        "multiplicatively round over round against the real production "
        "history term, not just a single isolated call");
}

// VROUTE_HIST_RATIO_EXPONENT: squaring the ratio should punish a badly-over
// edge much harder while barely changing a barely-over one -- the decisive
// property the user proposed this for.
void testHistoryRatioExponentSquaresTheRatio() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Prices prBarely(g);
  Usage barelyOver(g);
  barelyOver.h[idx] = 3; // capacity 2 -> ratio 1.5
  updateHistory(g, prBarely, barelyOver, 0.0, HistMode::kRatio, 0.0, 1, false, true,
                /*ratioExponent=*/2.0);
  check(std::abs(prBarely.histH[idx] - 2.25) < 1e-9,
        "exponent 2 on a 1.5x-over edge multiplies history by exactly "
        "1.5^2=2.25, not the plain 1.5 exponent-1 would give");

  Prices prWayOver(g);
  Usage tenXOver(g);
  tenXOver.h[idx] = 20; // capacity 2 -> ratio 10
  updateHistory(g, prWayOver, tenXOver, 0.0, HistMode::kRatio, 0.0, 1, false, true,
                /*ratioExponent=*/2.0);
  check(std::abs(prWayOver.histH[idx] - 100.0) < 1e-9,
        "exponent 2 on a 10x-over edge multiplies history by exactly "
        "10^2=100 -- squaring punishes the badly-over edge far more "
        "aggressively relative to the barely-over one (100/2.25=44.4x "
        "apart) than plain ratio mode would (10/1.5=6.7x apart), the "
        "decisive behavioral difference this parameter is supposed to buy");
}

// VROUTE_HIST_EXPONENT_ESCALATION: the effective exponent for a round's
// growth should climb with how many still-unrelaxed penalties are already
// on the stack, and a relaxed edge should use a *lower* effective exponent
// for its next penalty than an edge that stayed dirty continuously -- not
// merely end up at a lower history value, the escalation level itself must
// have dropped. Uses a fixed usage/capacity=2.0 throughout (capacity 2,
// usage 4) so every factor is an exact power of 2, checkable exactly.
void testExponentEscalationTracksStackSizeAndRelaxesWithIt() {
  Design d = parse("vroute_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  size_t idx = g.hIndex(0, 0, 0); // layer 0, horizontal capacity 2
  Usage over(g);
  over.h[idx] = 4; // ratio exactly 2.0
  Usage under(g);
  under.h[idx] = 0;
  constexpr double base = 1.0, escalation = 1.0;

  // Edge A: three straight dirty rounds, no relaxation in between.
  // exponents used: 1.0 (stack 0), 2.0 (stack 1), 3.0 (stack 2) ->
  // history: 2 -> 2*4=8 -> 8*8=64.
  Prices prA(g);
  for (int i = 0; i < 3; ++i)
    updateHistory(g, prA, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prA.histH[idx] - 64.0) < 1e-6,
        "three straight dirty rounds with escalation=1.0 produce exactly "
        "2*4*8=64 (exponents 1,2,3 in turn) -- confirms escalation climbs "
        "with consecutive, unrelaxed penalties");

  // Edge B: same three dirty rounds, then two clean rounds (relaxes the
  // stack from 3 down to 1), then a fourth dirty round.
  Prices prB(g);
  for (int i = 0; i < 3; ++i)
    updateHistory(g, prB, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  updateHistory(g, prB, under, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  updateHistory(g, prB, under, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prB.histH[idx] - 2.0) < 1e-6,
        "two clean rounds after the same three dirty ones relax the stack "
        "from size 3 down to 1 (64 -> /8 -> 8 -> /4 -> 2), exactly "
        "mirroring testRelaxationFullyRestoresBaselineAfterEqualCleanRounds' "
        "own LIFO-undo arithmetic");
  updateHistory(g, prB, over, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prB.histH[idx] - 8.0) < 1e-6,
        "edge B's fourth dirty round, with only 1 unrelaxed penalty on its "
        "stack, uses effective exponent 1.0+1.0*1=2.0 (2^2=4, 2*4=8)");

  Usage overStill(g); // fourth straight dirty round for edge A, no relax
  overStill.h[idx] = 4;
  updateHistory(g, prA, overStill, 0.0, HistMode::kRatio, 0.0, 1, false, true, base, escalation);
  check(std::abs(prA.histH[idx] - 1024.0) < 1e-3,
        "edge A's fourth dirty round, having never relaxed (stack still "
        "at 3), uses effective exponent 1.0+1.0*3=4.0 (2^4=16, 64*16=1024)");
  check(prA.histH[idx] > prB.histH[idx] * 100,
        "the decisive comparison: two edges on their 4th dirty round each, "
        "but the one that got two clean rounds in between (edge B, "
        "history=8) ends up over 100x lower than the one that never "
        "relaxed (edge A, history=1024) -- relaxation lowers the "
        "escalation level itself, not just the raw history value, exactly "
        "the property VROUTE_HIST_EXPONENT_ESCALATION's header claims by "
        "reusing the stack's own size as the escalation count");
}

// ---------------------------------------------------------------------------
// 2D-collapse mechanism tests: this file's own new addition over vroute.
// ---------------------------------------------------------------------------

// A capacity *adjustment* (not just the nominal per-layer default) must
// carry through the 2D sum -- computeCapH2d/computeCapV2d read the
// dense, post-adjustment g.hcap/g.vcap arrays, not g.hCapLayer/vCapLayer,
// so a real .gr reduced-capacity record on one layer must change exactly
// that layer's contribution to the 2D total, not the whole thing.
const char *kCapAdjust2dGr = R"(grid 4 1 2
vertical capacity 0 0
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netProbe 0 2 1
5 5 1
35 5 1
1
1 0 1 2 0 1 3
)";

void testGrid2dCapacityIsExactPostAdjustmentSumAcrossLayers() {
  Design d = parse("fr1route_selftest_capadjust.gr", kCapAdjust2dGr);
  const RoutingGrid &g = d.grid;
  Usage u(g);
  // Untouched edge: both layers at their nominal 10 -> 20.
  check(u.capH[g.hIndex(0, 0, 0)] == 20,
        "an untouched edge's 2D capacity is the plain sum of both layers' "
        "nominal capacity (10+10=20)");
  // Adjusted edge: layer 0 was set to 3 by the .gr adjustment record,
  // layer 1 stays at its nominal 10 -> 13, not 20.
  check(u.capH[g.hIndex(1, 0, 0)] == 13,
        "the adjusted edge's 2D capacity reflects the real post-adjustment "
        "value (3) on layer 0 plus layer 1's untouched nominal 10 -- 13, "
        "not the nominal-only 20 -- proving the sum reads g.hcap (dense, "
        "post-adjustment), not g.hCapLayer (nominal per-layer defaults)");
}

// The decisive claim this whole file exists to test: a net whose required
// width exceeds *every individual layer's* capacity, but not their sum,
// is unroutable without overflow under any single-layer choice a 3D
// router could make (10 > 4 on layer 0, 1, or 2 -- picking the least-bad
// layer still overflows by exactly 10-4=6, unavoidably, since this net's
// one edge is its only edge), yet is perfectly routable at 0 overflow
// once every layer's capacity is pooled into one 2D edge (4+4+4=12 >= 10).
// This is the real HISTORY_COST_SWEEP.md Part 9 mechanism, reproduced on
// a fixture small enough to verify by inspection rather than by running
// a second router: no single-layer choice can ever clear this net, by
// construction, and this file's own real production route() clears it
// exactly.
const char *kPoolsAcrossLayersGr = R"(grid 2 1 3
vertical capacity 0 0 0
horizontal capacity 4 4 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netWide 0 2 10
5 5 1
15 5 1
0
)";

void testTwoDRoutingClearsAnOverflowNoSingleLayerChoiceCanClear() {
  Design d = parse("fr1route_selftest_pools.gr", kPoolsAcrossLayersGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netWide produced a route");
  check(rep.convergedToZero && rep.bestOverflow == 0,
        "0 overflow -- on any single real layer this net's own width (10) "
        "alone exceeds that layer's whole capacity (4), so no per-layer "
        "choice could ever avoid a 6-unit overflow on this edge; pooling "
        "every layer's capacity into one 2D edge (4+4+4=12) is what makes "
        "0 achievable at all, the direct test of this file's whole reason "
        "to exist");
  check(segsConnected(rep.nets[0].segs), "netWide is one connected component");
}

// VROUTE_MSMV=0's inherited fixed-topology slow tier (routeNetSlow /
// boundedDijkstra, not routeNetSlowMSMV / multiSourceDijkstra) must still
// work correctly after the 2D collapse -- exercises the *other* search
// path this file also collapsed, not just the MSMV one every other test
// here happens to exercise via route()'s own default.
void testFixedTopologySlowTierStillWorksAfterTheCollapse() {
  Design d = parse("fr1route_selftest_basic.gr", kBasicGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/false);
  check(rep.nets.size() == 3, "all three basic nets produced a route with VROUTE_MSMV=0");
  check(rep.bestOverflow == 0, "no overflow on an uncongested design via the fixed-topology path either");
  for (const RoutedNet &rn : rep.nets)
    check(segsConnected(rn.segs), "each net routed via the fixed-topology 2D path is one component");
  // Part 215 (2026-11-24, review F2): the basic design never congests, so no net escalates and the lines above passed
  // with routeNetSlow, routeSegment and boundedDijkstra never run. A congested design (testSlowTierBestStateIsKept's
  // kind: 250 small nets on a 40 x 40 grid of capacity 2, with two fast rounds) escalates nets to the fixed-topology
  // tier; the counter shows its searches ran, and the routes must still be whole and no worse than the start.
  std::string gr = "grid 40 40 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 250\n";
  unsigned st = 777;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  for (int i = 0; i < 250; ++i) {
    int np = 2 + rnd(3), cx = rnd(40), cy = rnd(40);
    gr += "n" + std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(np) + " 1\n";
    for (int k = 0; k < np; ++k) {
      int x = std::min(39, std::max(0, cx + rnd(15) - 7)), y = std::min(39, std::max(0, cy + rnd(15) - 7));
      gr += std::to_string(x * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    }
  }
  gr += "0\n";
  Design dc = parse("pd39route_selftest_fixedslow.gr", gr.c_str());
  RunReport fast = route(dc, 2, 0, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, false);
  const long long before = gFixedTopologySlowRoutes.load();
  RunReport slow = route(dc, 2, 6, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, false);
  const long long searches = gFixedTopologySlowRoutes.load() - before;
  bool whole = slow.nets.size() == fast.nets.size() && slow.nets.size() >= 240; // 248: two nets sit in one tile
  for (const RoutedNet &rn : slow.nets) { // connected, and on every pin tile (an empty route is "connected" too)
    std::set<std::pair<int, int>> on;
    for (const Seg &sg : rn.segs)
      for (int x = std::min(sg.x1, sg.x2); x <= std::max(sg.x1, sg.x2); ++x)
        for (int y = std::min(sg.y1, sg.y2); y <= std::max(sg.y1, sg.y2); ++y)
          on.insert({x, y});
    whole &= segsConnected(rn.segs) && rn.pts.size() >= 2;
    for (const Point &pt : rn.pts)
      whole &= on.count({pt.x, pt.y}) == 1;
  }
  check(fast.bestOverflow > 0 && slow.escalatedCount > 0 && searches > 0,
        "congested design, VROUTE_MSMV=0: nets escalate and the fixed-topology slow tier searches (" +
            std::to_string(slow.escalatedCount) + " escalated, " + std::to_string(searches) + " searches)");
  check(whole && slow.bestOverflow <= fast.bestOverflow,
        "congested design, VROUTE_MSMV=0: every net whole, overflow " + std::to_string(slow.bestOverflow) +
            " no worse than the fast tier's " + std::to_string(fast.bestOverflow));
}

// FR1ROUTE_TWOD_ONLY is accepted as a reserved knob in this generation but
// has no effect yet -- this file has no layer-assignment stage to stop
// before (that is fr2route's own job). Confirms that claim directly: the
// same fixture, routed with and without the env var set, produces
// identical results, so the variable is a genuinely safe no-op placeholder
// right now, not a silently-wrong one.
void testTwodOnlyFlagIsInertInThisGeneration() {
  Design d1 = parse("fr1route_selftest_twod_a.gr", kBasicGr);
  RunReport without = route(d1, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);

  setenv("FR1ROUTE_TWOD_ONLY", "1", 1);
  Design d2 = parse("fr1route_selftest_twod_b.gr", kBasicGr);
  RunReport with = route(d2, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  unsetenv("FR1ROUTE_TWOD_ONLY");

  check(with.bestOverflow == without.bestOverflow,
        "FR1ROUTE_TWOD_ONLY=1 changes nothing yet -- reserved for fr2route, "
        "where it will skip that generation's new layer-assignment stage; "
        "this generation has no such stage to skip, so route() itself "
        "never reads this variable at all");
}

// ---------------------------------------------------------------------------
// Steinerization mechanism tests: this file's own new addition over
// fr1route. The same hand-derived 3-point shape anchors most of these:
// a=(0,0), b=(10,0), i=(5,10) [the hub -- degree 2 in the plain MST,
// index 0 in the point list passed to steinerizeTree]. The plain MST
// connects a-b (span 10) and a-i (span 15), total 25. The exact median
// Steiner point is S=(5,0): connecting a-S (5), b-S (5), i-S (10) totals
// 20 -- strictly less, and hand-checkable exactly.
// ---------------------------------------------------------------------------

void testMedianSteinerPointIsExact() {
  check(median3(1, 5, 3) == 3, "median of {1,5,3} is 3");
  check(median3(7, 2, 2) == 2, "median of {7,2,2} (a repeated value) is 2");
  check(median3(9, 9, 9) == 9, "median of three equal values is that value");

  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}}; // a, b, i
  std::vector<std::pair<int, int>> mst = {{0, 1}, {0, 2}}; // plain MST: a-b, a-i
  SteinerResult sr = steinerizeTree(pts, mst);
  check(sr.pts.size() == 4, "one new Steiner point was appended (sanity)");
  check(sr.pts[3].x == 5 && sr.pts[3].y == 0,
        "the real production steinerizeTree computes exactly S=(5,0) -- "
        "median(0,10,5)=5 on x, median(0,0,10)=0 on y -- matching the "
        "hand-derived Steiner point exactly, not just 'some' new point");
}

// The decisive test: steinerizeTree's real output has strictly less total
// Manhattan span (20) than the plain MST it replaced (25) on the hand-
// derived hub shape above.
void testSteinerizationStrictlyReducesTotalSpan() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}};
  std::vector<std::pair<int, int>> mst = {{0, 1}, {0, 2}};

  int plainSpan = 0;
  for (auto &e : mst)
    plainSpan += manhattan(pts[e.first], pts[e.second]);
  check(plainSpan == 25, "the plain MST's own total span is 25 (sanity)");

  SteinerResult sr = steinerizeTree(pts, mst);
  int steinerSpan = 0;
  for (auto &e : sr.edges)
    steinerSpan += manhattan(sr.pts[e.first], sr.pts[e.second]);
  check(steinerSpan == 20,
        "the real production steinerizeTree's own output totals exactly "
        "20 -- strictly less than the plain MST's 25 -- the direct, "
        "hand-verified proof this mechanism does what it claims");
}

// No-op when it wouldn't help: three collinear points, where the middle
// point is already its own coordinate median -- steinerizeTree must
// return the tree completely unchanged (no new point, identical edges),
// proving the "strictly better only" gate is real, not always-on.
void testSteinerizationIsANoOpWhenTheHubIsAlreadyOptimal() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 0}}; // a, b, i -- i is the midpoint
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  check(mst.size() == 2, "three points form a 2-edge tree (sanity)");

  SteinerResult sr = steinerizeTree(pts, mst);
  check(sr.pts.size() == pts.size(),
        "no new point was appended -- the collinear midpoint is already "
        "its own median, so there is nothing to gain");
  check(sr.edges.size() == mst.size(),
        "the edge list is unchanged in size");
  std::vector<std::pair<int, int>> sortedOrig = mst, sortedNew = sr.edges;
  auto normalize = [](std::pair<int, int> &e) {
    if (e.first > e.second)
      std::swap(e.first, e.second);
  };
  for (auto &e : sortedOrig)
    normalize(e);
  for (auto &e : sortedNew)
    normalize(e);
  std::sort(sortedOrig.begin(), sortedOrig.end());
  std::sort(sortedNew.begin(), sortedNew.end());
  check(sortedOrig == sortedNew,
        "the exact same edges (endpoint pairs), not just the same count, "
        "come back out -- a genuinely inert no-op, not a coincidentally "
        "equal-cost rewrite");
}

// Tree validity: steinerizeTree's output must remain a valid tree (exactly
// pts.size()-1 edges, one connected component, no cycles) on a larger,
// multi-branch point set -- checked by direct union-find over the output
// edge list, independent of steinerizeTree's own internal bookkeeping.
void testSteinerizedOutputIsAlwaysAValidTree() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}, {5, -10}, {20, 0}, {5, 20}};
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  SteinerResult sr = steinerizeTree(pts, mst);

  check(sr.edges.size() == sr.pts.size() - 1,
        "edge count is exactly one less than point count -- the tree "
        "invariant holds however many nodes were Steinerized");

  std::vector<int> parent(sr.pts.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : sr.edges) {
    int ra = find(e.first), rb = find(e.second);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle was introduced by any Steinerization");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < sr.pts.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected, "every point, original and Steiner both, is in one connected component");
}

// FR3ROUTE_STEINERIZE=0 must reproduce fr1route's exact plain-MST
// topology -- checked directly and structurally: with it disabled, a net
// built from the hub fixture above must come back with exactly its
// original 3 points, no 4th Steiner point ever appended.
const char *kHubGr = R"(grid 12 12 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netHub 0 3 1
5 5 1
105 5 1
55 105 1
0
)";

void testDisablingSteinerizeReproducesPlainMstTopology() {
  Design d = parse("fr3route_selftest_notier.gr", kHubGr);
  // This fixture has only 3 pins, squarely inside this file's own exact-
  // Steiner degree range -- both switches must be off to actually reach
  // fr1route's plain MST, not just FR3ROUTE_STEINERIZE.
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                         /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(rep.nets.size() == 1, "netHub produced a route with FR3ROUTE_STEINERIZE=0");
  check(rep.nets[0].pts.size() == 3,
        "with Steinerization disabled, the net comes back with exactly "
        "its original 3 pins -- no Steiner point was ever appended, "
        "confirming the toggle genuinely disables the mechanism rather "
        "than just leaving it unused");
}

// The end-to-end decisive test: routed through the real production
// pipeline (route(), not steinerizeTree in isolation), the hub net's
// total charged wirelength (summed segment span) is exactly 20 with
// Steinerization enabled versus exactly 25 with it disabled -- the same
// hand-derived numbers as testSteinerizationStrictlyReducesTotalSpan,
// now reproduced through real candidate generation and commit. Every
// segment here is a pure single-axis run (no jog needed either way,
// since each Steinerized edge is purely horizontal or vertical and the
// plain MST's diagonal edge's total span is jog-invariant, the same
// Manhattan-distance property this project's own history already relies
// on elsewhere), so the real fast tier's specific candidate choice cannot
// change the total either way.
void testEndToEndSteinerizationReducesRealChargedWirelength() {
  auto totalSpan = [](const RunReport &rep) {
    long long total = 0;
    for (const RoutedNet &rn : rep.nets)
      for (const Seg &s : rn.segs)
        total += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
    return total;
  };

  Design dWith = parse("fr3route_selftest_e2e_with.gr", kHubGr);
  RunReport withSteiner = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(withSteiner.bestOverflow == 0, "the hub net is uncongested (sanity)");
  check(totalSpan(withSteiner) == 20,
        "with Steinerization enabled (default), the real production "
        "route() charges exactly 20 total tile-edges for this net -- this "
        "file's own exact Dreyfus-Wagner solve reaches the same S=(5,0) "
        "topology fr3route's cheap median pass found (the coordinate-wise "
        "median is the true 3-terminal optimum, so exact and cheap agree "
        "exactly on a 3-pin net), not the plain MST");

  Design dWithout = parse("fr3route_selftest_e2e_without.gr", kHubGr);
  // Both switches off: this fixture's 3 pins are squarely inside the
  // exact solve's degree range, so useExactSteiner must also be disabled
  // to actually reach fr1route's plain MST.
  RunReport withoutSteiner = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                                    /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                                    /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                                    /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                                    /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(totalSpan(withoutSteiner) == 25,
        "with Steinerization disabled, the same net charges exactly 25 -- "
        "the plain MST's own total span, confirmed through the real "
        "production pipeline end to end, not just steinerizeTree alone");
}

// ---------------------------------------------------------------------------
// Exact Steiner-tree (Dreyfus-Wagner) mechanism tests: this file's own new
// addition over fr3route. The decisive fixture is a 4-point "hub" shape
// where the plain MST's own branch point has degree 3 -- fr3route's cheap
// pass (degree-2-only) is a structural no-op here, so any improvement
// measured can only have come from the real Hanan-grid DP.
// ---------------------------------------------------------------------------

void testExactSteinerBeatsBothPlainMstAndCheapPassOnADegreeThreeHub() {
  // A=(0,3), B=(4,3), C=(2,6), D=(2,0) -- shifted to non-negative
  // coordinates from the hand-derived diamond (0,0),(4,0),(2,3),(2,-3).
  std::vector<Point> pts = {{0, 3}, {4, 3}, {2, 6}, {2, 0}};

  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  int plainSpan = 0;
  for (auto &e : mst)
    plainSpan += manhattan(pts[e.first], pts[e.second]);
  check(plainSpan == 14, "the plain MST's own total span is 14 (sanity)");

  // fr3's cheap pass is a structural no-op here: node 0 (A) ends up
  // degree 3 in the plain MST (Prim's from A connects A-B, A-C, A-D), so
  // no degree-2 node exists for the median trick to fire on.
  SteinerResult cheap = steinerizeTree(pts, mst);
  check(cheap.pts.size() == pts.size(),
        "fr3route's cheap degree-2-only pass appends no Steiner point on "
        "this fixture -- the branch point is degree 3, outside its scope, "
        "confirming this really does test what the exact DP alone can do");
  int cheapSpan = 0;
  for (auto &e : cheap.edges)
    cheapSpan += manhattan(cheap.pts[e.first], cheap.pts[e.second]);
  check(cheapSpan == 14, "the cheap pass's own total span is unchanged at 14");

  SteinerResult exact = exactSteinerTree(pts);
  int exactSpan = 0;
  for (auto &e : exact.edges)
    exactSpan += manhattan(exact.pts[e.first], exact.pts[e.second]);
  check(exactSpan == 10,
        "the exact Dreyfus-Wagner DP finds the true optimum, 10 -- a "
        "28.6% reduction from 14 that fr3route's cheap pass structurally "
        "cannot reach on this fixture (no degree-2 node to trigger it)");

  bool foundHub = false;
  for (const Point &p : exact.pts)
    if (p.x == 2 && p.y == 3)
      foundHub = true;
  check(foundHub,
        "the reconstructed tree passes through the exact hand-derived "
        "Steiner point (2,3) -- dist to A=2, B=2, C=3, D=3, totaling 10");

  check(exact.edges.size() == exact.pts.size() - 1,
        "the exact solve's own output is a valid tree (edges = points-1)");
}

void testExactSteinerMatchesHandVerifiedOptimaOnSmallFixtures() {
  // Case 1: three collinear points -- the plain MST is already optimal
  // (no Steiner point can beat a straight line), matching the same
  // fixture steinerizeTree's own no-op test uses.
  std::vector<Point> collinear = {{0, 0}, {10, 0}, {5, 0}};
  SteinerResult r1 = exactSteinerTree(collinear);
  int span1 = 0;
  for (auto &e : r1.edges)
    span1 += manhattan(r1.pts[e.first], r1.pts[e.second]);
  check(span1 == 10,
        "collinear points: exact DP matches the already-optimal "
        "plain-line cost of 10, no improvement possible");

  // Case 2: a hand-computed 3-point case where the Steiner point strictly
  // beats the plain MST -- P0=(0,0), P1=(2,0), P2=(1,5). Plain MST (Prim
  // from P0): P0-P1 (2) + P0-P2 (6) = 8. The coordinate-wise median
  // S=(1,0) [median(0,2,1)=1, median(0,0,5)=0] gives P0-S(1)+P1-S(1)+
  // P2-S(5) = 7 -- provably optimal for exactly 3 terminals (the same
  // classical result median3's own header cites).
  std::vector<Point> tri = {{0, 0}, {2, 0}, {1, 5}};
  std::vector<std::pair<int, int>> triMst = mstEdges(tri);
  int triMstSpan = 0;
  for (auto &e : triMst)
    triMstSpan += manhattan(tri[e.first], tri[e.second]);
  check(triMstSpan == 8, "this fixture's plain MST totals 8 (sanity)");

  SteinerResult r2 = exactSteinerTree(tri);
  int span2 = 0;
  for (auto &e : r2.edges)
    span2 += manhattan(r2.pts[e.first], r2.pts[e.second]);
  check(span2 == 7,
        "exact DP finds the true 3-terminal optimum, 7 -- strictly less "
        "than the plain MST's 8, matching the hand-derived median Steiner "
        "point exactly");
}

void testExactSteinerOutputIsAlwaysAValidTree() {
  std::vector<Point> pts = {{0, 0}, {10, 0}, {5, 10}, {5, 20}, {20, 0}, {15, 15}};
  SteinerResult sr = exactSteinerTree(pts);

  check(sr.edges.size() == sr.pts.size() - 1,
        "edge count is exactly one less than point count");

  std::vector<int> parent(sr.pts.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : sr.edges) {
    int ra = find(e.first), rb = find(e.second);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle in the exact solve's own output");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < sr.pts.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected,
        "every point, original and Steiner both, is in one connected component");
}

const char *kNinePinGr = R"(grid 12 6 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netNine 0 9 1
5 5 1
25 5 1
45 5 1
65 5 1
85 5 1
15 35 1
35 35 1
55 35 1
75 35 1
0
)";

// The degree cap: this fixture's 9 unique pins exceed FR4ROUTE_EXACT_MAX_
// PINS's default of 8, so the exact solve must be skipped entirely for
// this net -- exercising fr3route's inherited cheap-Steinerization
// fallback, not the exact path silently running anyway (which it is
// mathematically capable of; the cap is a runtime/complexity choice, not
// a correctness limit -- see the file header).
void testDegreeCapGatingExercisesFr3sFallbackExactly() {
  Design d = parse("fr4route_selftest_ninepin.gr", kNinePinGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netNine produced a route");
  check(rep.bestOverflow == 0, "netNine is uncongested (sanity)");

  std::vector<Point> pts = {{0, 0}, {2, 0}, {4, 0}, {6, 0}, {8, 0},
                             {1, 3}, {3, 3}, {5, 3}, {7, 3}};
  check(pts.size() == 9,
        "this fixture's 9 unique pins exceed the default exact-solve cap "
        "of 8 (sanity)");
  std::vector<std::pair<int, int>> mst = mstEdges(pts);
  SteinerResult cheap = steinerizeTree(pts, mst);
  long long cheapSpan = 0;
  for (auto &e : cheap.edges)
    cheapSpan += manhattan(cheap.pts[e.first], cheap.pts[e.second]);

  long long routedSpan = 0;
  for (const Seg &s : rep.nets[0].segs)
    routedSpan += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
  check(routedSpan == cheapSpan,
        "the real production pipeline's total charged wirelength for this "
        "9-pin net matches fr3route's own cheap Steinerization exactly -- "
        "proving the degree cap genuinely routed it through the fallback, "
        "not the exact solver silently running anyway");
}

// FR4ROUTE_EXACT_STEINER=0 alone (fr3route's own FR3ROUTE_STEINERIZE left
// at its default of enabled) must fall back to fr3route's cheap pass, not
// all the way down to fr1route's plain MST -- a distinct check from
// testDisablingSteinerizeReproducesPlainMstTopology, which disables both.
void testDisablingExactSteinerAloneFallsBackToCheapPassNotPlainMst() {
  Design d = parse("fr4route_selftest_exactoff.gr", kHubGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                         /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                         /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                         /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                         /*useSteinerize=*/true, /*useExactSteiner=*/false);
  check(rep.nets.size() == 1, "netHub produced a route with FR4ROUTE_EXACT_STEINER=0");
  check(rep.nets[0].pts.size() == 4,
        "with only the exact solve disabled (fr3route's own cheap pass "
        "still enabled), a 4th Steiner point is still appended -- the "
        "fallback engaged, this did not silently degrade to the plain MST");
  long long span = 0;
  for (const Seg &s : rep.nets[0].segs)
    span += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
  check(span == 20,
        "the cheap pass alone reaches the same optimal 20 on this "
        "3-terminal fixture (the coordinate-wise median is exact for 3 "
        "terminals, so cheap and exact agree here) -- FR4ROUTE_EXACT_"
        "STEINER=0 fell back to fr3route's mechanism, not to fr1route's "
        "plain MST (which would total 25)");
}

const char *kTwoHubsGr = R"(grid 18 8 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 2
netHubA 0 4 1
5 35 1
45 35 1
25 65 1
25 5 1
netHubB 1 4 1
105 35 1
145 35 1
125 65 1
125 5 1
0
)";

// The end-to-end decisive test: two copies of the degree-three hub shape
// (each unreachable by fr3route's cheap pass, per the decisive unit test
// above) routed through the real production pipeline. Both mechanisms
// disabled reproduces the plain MST's 14 per net (28 total); the exact
// solve enabled (default) reaches 10 per net (20 total).
void testEndToEndExactSteinerizationReducesRealChargedWirelength() {
  auto totalSpan = [](const RunReport &rep) {
    long long total = 0;
    for (const RoutedNet &rn : rep.nets)
      for (const Seg &s : rn.segs)
        total += std::abs(s.x2 - s.x1) + std::abs(s.y2 - s.y1);
    return total;
  };

  Design dWith = parse("fr4route_selftest_e2e_with.gr", kTwoHubsGr);
  RunReport withExact = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(withExact.bestOverflow == 0, "both hub nets are uncongested (sanity)");
  check(totalSpan(withExact) == 20,
        "with the exact solve enabled (default), both 4-pin hub nets "
        "reach their true 10-tile-edge optimum each, totaling 20");

  Design dWithout = parse("fr4route_selftest_e2e_without.gr", kTwoHubsGr);
  RunReport withoutExact = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0,
                                  /*streakThreshold=*/1, /*scaleByOverflow=*/false,
                                  /*relaxEnabled=*/true, /*ratioExponent=*/1.0,
                                  /*exponentEscalation=*/0.0, /*useMSMV=*/true,
                                  /*useSteinerize=*/false, /*useExactSteiner=*/false);
  check(totalSpan(withoutExact) == 28,
        "with both Steinerization mechanisms disabled, the same two nets "
        "fall back to the plain MST's own 14-tile-edge span each, "
        "totaling 28 -- confirming the exact solve's real benefit through "
        "the full production pipeline, not exactSteinerTree in isolation");
  check(totalSpan(withExact) < totalSpan(withoutExact),
        "the exact solve strictly reduces real charged wirelength on "
        "fixtures fr3route's cheap pass alone cannot touch");
}

const char *kTwoPinGr = R"(grid 6 6 1
vertical capacity 20
horizontal capacity 20
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netTwo 0 2 1
5 5 1
45 45 1
0
)";

// 2-pin nets and already-optimal (collinear) small nets must come back
// untouched by the exact solve, the same "strictly better only" gate
// fr3route's own cheap pass already proves for itself.
void testTwoPinAndAlreadyOptimalNetsAreUntouchedByTheExactSolve() {
  Design d2 = parse("fr4route_selftest_twopin.gr", kTwoPinGr);
  RunReport rep2 = route(d2, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep2.nets.size() == 1, "netTwo produced a route");
  check(rep2.nets[0].pts.size() == 2,
        "a 2-pin net is never handed to the exact solve (k>=3 required) "
        "-- no Steiner point is ever possible for 2 terminals anyway");

  std::vector<Point> collinear = {{0, 0}, {10, 0}, {5, 0}};
  SteinerResult sr = exactSteinerTree(collinear);
  check(sr.pts.size() == collinear.size(),
        "the exact solve appends no Steiner point on an already-optimal "
        "collinear fixture");
  check(sr.edges.size() == collinear.size() - 1,
        "still a valid tree (edges = points-1)");
}

// ---------------------------------------------------------------------------
// Layer-assignment mechanism tests: this file's own new addition over
// fr4route. Two real layers, both bidirectional, horizontal capacity 1
// each -- tight enough that two nets sharing the exact same horizontal
// edge must land on different real layers to both fit.
// ---------------------------------------------------------------------------

const char *kTwoLayerBidirGr = R"(grid 6 6 2
vertical capacity 20 20
horizontal capacity 1 1
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 2
netA 0 2 1
5 25 1
35 25 1
netB 1 2 1
5 25 1
35 25 1
0
)";

// The decisive test: assignSegmentLayer's choice is driven by real,
// running per-layer usage, not a static "always pick the first candidate"
// rule. Both nets need the identical horizontal edge; the 2D-collapsed
// solve (capacity 1+1=2) converges both onto it with 0 overflow, but the
// real per-layer capacity (1 each) forces the second net's segment onto
// the other real layer once the first has committed.
void testCapacityAwareLayerChoiceReactsToRealUsageNotJustPreference() {
  Design d = parse("fr5route_selftest_capaware.gr", kTwoLayerBidirGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 2, "both nets produced a route");
  check(rep.bestOverflow == 0,
        "the 2D-collapsed solve sees combined capacity 2 for exactly 2 "
        "units of demand -- no overflow there (sanity)");
  negotiateViaAssignment(d.grid, rep.nets);

  const RoutedNet *netA = nullptr, *netB = nullptr;
  for (const RoutedNet &rn : rep.nets) {
    if (rn.net->name == "netA")
      netA = &rn;
    if (rn.net->name == "netB")
      netB = &rn;
  }
  check(netA != nullptr && netB != nullptr, "found both nets by name (sanity)");

  // Find each net's own wire leg specifically (not any via segment
  // stitchPinAccessForNet may since have added -- both nets' pins are
  // declared on layer index 0 in this fixture, so whichever net's wire
  // landed on a different layer now also carries pin-access vias, which
  // is correct behavior, not this test's own concern).
  auto wireLayer = [](const RoutedNet &rn) {
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2))
        return static_cast<int>(s.l1);
    return -1;
  };
  check(wireLayer(*netA) == 0,
        "the first-committed net ties on empty-usage cost and lands on "
        "the lower-indexed real layer, 0");
  check(wireLayer(*netB) == 1,
        "the second net's identical-position segment sees layer 0 "
        "already at its real capacity (usage 1, cap 1) and is pushed to "
        "real layer 1 instead -- driven by RealUsage, not a static rule");
}

// Direction preference: layer 0 horizontal-only, layer 1 vertical-only.
// A net whose tree needs both a horizontal and a vertical leg must never
// place either on the wrong-direction layer.
const char *kDirectionPreferenceGr = R"(grid 6 6 2
vertical capacity 0 20
horizontal capacity 20 0
minimum width 1 1
minimum spacing 0 0
via spacing 0 0
0 0 10 10
num net 1
netL 0 2 1
5 5 1
35 35 1
0
)";

void testDirectionPreferenceIsNeverViolated() {
  Design d = parse("fr5route_selftest_direction.gr", kDirectionPreferenceGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netL produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  bool sawHorizontal = false, sawVertical = false;
  for (const Seg &s : rep.nets[0].segs) {
    if (s.l1 != s.l2)
      continue; // a via, not a wire leg -- direction is meaningless here
    if (s.y1 == s.y2) {
      sawHorizontal = true;
      check(s.l1 == 0, "a horizontal leg landed on layer 0 (the only "
                        "horizontal-capable real layer)");
    } else {
      sawVertical = true;
      check(s.l1 == 1, "a vertical leg landed on layer 1 (the only "
                        "vertical-capable real layer)");
    }
  }
  check(sawHorizontal && sawVertical,
        "this L-shaped net actually exercises both directions (sanity -- "
        "otherwise the two checks above would be vacuously true)");
}

// Via stitching, tested directly on stitchViasForNet with a hand-built,
// controlled RoutedNet: three legs meeting at hub point (5,5), on three
// different real layers (0, 2, 4) by construction (not derived from real
// routing -- isolating this mechanism from assignSegmentLayer's own
// choices, which the two tests above already cover).
void testViaStitchingConnectsAThreeWayLayerMismatch() {
  RoutedNet rn;
  rn.segs = {
      {0, 5, 0, 5, 5, 0},  // layer 0, from (0,5) to the hub
      {5, 5, 2, 10, 5, 2}, // layer 2, from the hub outward
      {5, 0, 4, 5, 5, 4},  // layer 4, from (5,0) to the hub
  };
  size_t before = rn.segs.size();
  stitchViasForNet(rn);
  check(rn.segs.size() > before, "via segments were actually inserted");
  check(segsConnected(rn.segs),
        "after stitching, all three legs -- on three different real "
        "layers -- are connected in full 3D, the actual point of this "
        "stage");

  int viaCount = 0;
  for (const Seg &s : rn.segs) {
    if (s.x1 != s.x2 || s.y1 != s.y2 || s.l1 == s.l2)
      continue;
    check(s.x1 == 5 && s.y1 == 5, "every via sits at the hub point (5,5)");
    check(std::abs(s.l1 - s.l2) == 1, "every via is a single real-layer "
                                       "step, matching the real 'one tile "
                                       "of wirelength per via crossing' "
                                       "scoring rule");
    ++viaCount;
  }
  check(viaCount == 2 + 4,
        "the reference layer is the minimum present (0); reaching layer "
        "2 costs 2 unit steps and reaching layer 4 costs 4 -- 6 via "
        "segments total, none wasted");
}

// The decisive regression test for the second bug found during this
// generation's own real-benchmark verification (see coverageAt's own
// comment): the first version of this stage only checked segment
// *endpoints* for layer mismatches, missing MSMV's own defining
// mechanism -- attaching new wire to any point on already-committed
// wire, mid-span, not just at another tree edge's two ends. This fixture
// reproduces exactly that shape: a long already-committed leg, and a
// second leg attaching at a point strictly inside the first leg's own
// span (not one of its two declared endpoints), on a different layer.
void testViaStitchingCatchesMidSpanMsmvAttachmentNotJustEndpoints() {
  RoutedNet rn;
  rn.segs = {
      {0, 5, 0, 10, 5, 0}, // an already-committed wire, layer 0, x=0..10
      {5, 5, 2, 5, 8, 2},  // a new leg attaching mid-span at (5,5), layer 2
  };
  size_t before = rn.segs.size();
  stitchViasForNet(rn);
  check(rn.segs.size() > before,
        "a via was inserted at the mid-span attachment point (5,5), not "
        "just at segment endpoints -- the exact gap this fix closes");
  check(segsConnected(rn.segs),
        "the two legs are connected in full 3D even though their shared "
        "point is the middle of the first leg's own span, not one of its "
        "declared endpoints -- exactly what MSMV produces at scale on "
        "real benchmarks (5974 disconnected nets on adaptec1 before this "
        "fix)");
}

const char *kLongEdgeGr = R"(grid 8 2 1
vertical capacity 20
horizontal capacity 1
minimum width 1
minimum spacing 0
via spacing 0
0 0 10 10
num net 1
netLong 0 2 1
5 5 1
75 5 1
0
)";

// A single Seg spanning several unit tile-edges must charge every one of
// them into RealUsage, not just the first -- the same class of bug
// CLAUDE.md's own "a giant net must still charge usage for whatever route
// it emits" warning describes, applied here to per-leg real-layer
// charging. Calls assignSegmentLayer directly, bypassing route()
// entirely, so the only thing under test is the charging loop itself.
void testMultiTileSegmentChargesEveryUnitEdgeNotJustOnce() {
  Design d = parse("fr5route_selftest_longedge.gr", kLongEdgeGr);
  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  Seg s{0, 0, 0, 5, 0, 0}; // horizontal leg spanning tile-edges x=0..4
  assignSegmentLayer(d.grid, ru, lp, /*width=*/1, s);
  check(s.l1 == 0 && s.l2 == 0, "only one real layer exists here (sanity)");
  for (int x = 0; x < 5; ++x) {
    size_t idx = d.grid.hIndex(x, 0, 0);
    check(ru.h[idx] == 1,
          "unit tile-edge x=" + std::to_string(x) +
              " was charged exactly once by this single 5-tile-edge leg "
              "-- not skipped, not double-charged");
  }
}

// The decisive regression test for the third bug found during this
// generation's own real-benchmark verification (see dropDegenerateSegs's
// own comment): route()'s own inherited candidate-commit code has always
// emitted zero-length, same-layer segments -- (x,y,l)-(x,y,l) -- which
// eval2008.pl's real scorer rejects outright as a "null route", even
// though gr_verify never flagged them and they contribute nothing to
// real wirelength either way. A hand-built RoutedNet mixing one real
// wire leg with two degenerate ones (matching the exact shape found in
// fr4route's own real adaptec1 output, net n7807) confirms only the
// degenerate ones are removed.
void testDegenerateZeroLengthSegmentsAreDropped() {
  RoutedNet rn;
  rn.segs = {
      {0, 0, 0, 10, 0, 0},  // a real wire leg
      {5, 5, 1, 5, 5, 1},   // degenerate: same point, same layer
      {20, 20, 2, 20, 20, 2}, // another, elsewhere in the net
  };
  dropDegenerateSegs(rn);
  check(rn.segs.size() == 1, "both degenerate segments were removed, "
                              "the real leg was not");
  check(rn.segs[0].x1 == 0 && rn.segs[0].x2 == 10,
        "the surviving segment is exactly the real wire leg, untouched");
}

// FR1ROUTE_TWOD_ONLY=1's own long-documented purpose, finally made real:
// main() never calls negotiateViaAssignment() at all when it is set, reproducing
// fr4route's exact single-layer output. Self-tests call route() directly
// (not main()), so main()'s own gating is reproduced here explicitly;
// reuses kTwoLayerBidirGr so the two branches genuinely differ (a
// single-layer fixture would make both branches look identical and prove
// nothing).
void testTwodOnlyReproducesFr4routesExactSingleLayerOutput() {
  Design dWith = parse("fr5route_selftest_twod_with.gr", kTwoLayerBidirGr);
  RunReport repWith = route(dWith, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  // FR1ROUTE_TWOD_ONLY=1 means main() skips negotiateViaAssignment() entirely.
  for (const RoutedNet &rn : repWith.nets)
    for (const Seg &s : rn.segs)
      check(s.l1 == 0 && s.l2 == 0,
            "with negotiateViaAssignment() skipped, every segment stays on "
            "fr4route's exact placeholder layer 0 -- no vias, byte for "
            "byte what fr4route itself produces");

  Design dWithout = parse("fr5route_selftest_twod_without.gr", kTwoLayerBidirGr);
  RunReport repWithout = route(dWithout, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dWithout.grid, repWithout.nets);
  bool sawLayer1 = false;
  for (const RoutedNet &rn : repWithout.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == 1)
        sawLayer1 = true;
  check(sawLayer1,
        "without the flag, negotiateViaAssignment() actually ran and moved at "
        "least one net's segment to real layer 1 -- the two branches "
        "genuinely differ, not just an inert flag read");
}

const char *kPinLayerMismatchGr = R"(grid 6 2 3
vertical capacity 20 20 20
horizontal capacity 20 20 20
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 1
netPin 0 2 1
5 5 1
35 5 3
0
)";

// The decisive regression test for the bug found during this generation's
// own real-benchmark verification (see stitchPinAccessForNet's own
// comment): a 2-pin net where one pin is declared on layer 1 (file,
// 1-indexed -- index 0) and the other on layer 3 (index 2). The greedy
// wire assignment has no congestion-driven reason to ever choose layer 2
// on its own (every layer here is ample and equally cheap, so it ties to
// the lowest index, 0) -- reproducing exactly the real-benchmark failure
// mode where a pin's own declared layer is never naturally reached.
// gr_verify's own connectivity check (matching eval2008.pl) is reproduced
// directly here: the route must touch each pin's exact (x, y, layer).
void testPinAccessLayerIsAlwaysReached() {
  Design d = parse("fr5route_selftest_pinlayer.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  RoutedNet &rn = rep.nets[0];

  check(segsConnected(rn.segs),
        "the net is fully connected in 3D after pin-access stitching");

  auto touches = [&](int x, int y, int l) {
    for (const Seg &s : rn.segs)
      if ((s.x1 == x && s.y1 == y && s.l1 == l) ||
          (s.x2 == x && s.y2 == y && s.l2 == l))
        return true;
    return false;
  };
  check(touches(0, 0, 0), "pin A's own declared layer (index 0) is reached");
  check(touches(3, 0, 2),
        "pin B's own declared layer (index 2) is reached -- even though "
        "the wire's own greedy choice, driven purely by congestion, has "
        "no reason to land there on its own, exactly the gap "
        "stitchPinAccessForNet exists to close");
}

const char *kMultiNetMultiLayerGr = R"(grid 10 10 3
vertical capacity 4 4 4
horizontal capacity 4 4 4
minimum width 1 1 1
minimum spacing 0 0 0
via spacing 0 0 0
0 0 10 10
num net 3
netA 0 2 1
5 5 1
85 85 1
netB 1 4 1
15 5 1
75 5 1
15 95 1
15 15 1
netC 2 2 1
95 5 1
5 95 1
0
)";

// The end-to-end decisive test: a multi-net, multi-layer design routed
// through the real production pipeline (route() -> negotiateViaAssignment(), not
// any single mechanism in isolation) -- every net must be one connected
// component in full 3D (real layers plus any stitched vias), the actual
// property this whole stage exists to guarantee. The authoritative
// gr_verify/eval2008.pl cross-check happens separately against the real
// benchmark runs below, matching every prior generation's own convention
// (self-tests here are pure in-process checks; the real external-tool
// cross-check is part of this file's own real-benchmark verification,
// not --selftest).
void testEndToEndRoutingAndLayerAssignmentProducesAConnectedThreeDResult() {
  Design d = parse("fr5route_selftest_e2e.gr", kMultiNetMultiLayerGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 3, "all three nets produced a route");
  negotiateViaAssignment(d.grid, rep.nets);
  for (const RoutedNet &rn : rep.nets)
    check(segsConnected(rn.segs),
          "net '" + rn.net->name +
              "' is a single connected component in full 3D after the "
              "real route() -> negotiateViaAssignment() pipeline");
}

// ---------------------------------------------------------------------------
// Joint via-cost/capacity-aware tree DP tests: this file's own new
// addition over fr5route. A shared 2-layer, tight-via fixture anchors
// most of these -- a horizontal leg A=(0,0)-(5,0) and a vertical leg
// B=(5,0)-(5,5) meeting at (5,0), the smallest possible real branch
// point (exactly one via decision to make).
// ---------------------------------------------------------------------------

const char *kTightViaGr = R"(grid 8 8 2
vertical capacity 10 10
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 10 10
0 0 10 10
num net 1
netAB 0 2 1
5 5 1
55 55 1
0
)";

// The decisive test: the joint DP avoids an avoidable via a greedy,
// independent choice would create. Real usage is pre-loaded so A's own
// cheapest layer in isolation is 0 and B's is 1 (a moderate margin), but
// the via site at (5,0) is tight enough (capacity 1, via spacing 10 on a
// 10x10 tile) that its cost dominates the wire-cost margin.
void testJointDPAvoidsAnAvoidableViaGreedyWouldCreate() {
  Design d = parse("fr6route_selftest_dpvia.gr", kTightViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 1,
        "via capacity between the two layers is tight, exactly 1 (sanity)");

  RealUsage ru(d.grid);
  for (int x = 0; x < 5; ++x)
    ru.h[d.grid.hIndex(x, 0, 1)] += 1; // layer 1 slightly more loaded for A
  for (int y = 0; y < 5; ++y)
    ru.v[d.grid.vIndex(5, y, 0)] += 1; // layer 0 slightly more loaded for B

  std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.nodes.size() == 3 && t.edges.size() == 2,
        "two tree edges meeting at one branch point (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result (sanity)");
  check(edgeLayer[0] == edgeLayer[1],
        "the joint DP matches both edges to the same layer -- accepting "
        "a wire-cost penalty on one of them to avoid the tight via site "
        "entirely, which a per-edge independent choice has no reason to "
        "do");

  // Contrast directly: fr5route's own inherited, independent
  // assignSegmentLayer, given the identical starting usage, has no
  // via-cost awareness and picks each edge's own local optimum
  // regardless of the other.
  RealUsage ru2(d.grid);
  for (int x = 0; x < 5; ++x)
    ru2.h[d.grid.hIndex(x, 0, 1)] += 1;
  for (int y = 0; y < 5; ++y)
    ru2.v[d.grid.vIndex(5, y, 0)] += 1;
  Seg sa{0, 0, 0, 5, 0, 0}, sb{5, 0, 0, 5, 5, 0};
  assignSegmentLayer(d.grid, ru2, lp, 1, sa);
  assignSegmentLayer(d.grid, ru2, lp, 1, sb);
  check(sa.l1 != sb.l1,
        "the OLD independent-greedy assignSegmentLayer picks different "
        "layers for A and B on the identical starting usage -- proving "
        "the joint DP's matching choice above is real, new behavior, "
        "not a coincidence both mechanisms would reach anyway");
}

// This file's own tuning knob (added after the real-benchmark result in
// HISTORY_COST_SWEEP.md Part 26) has a real, direct effect: on the
// identical tight-via fixture above, a low via weight should make the
// DP behave like the old independent-greedy path (mismatch, since the
// via cost that used to deter it is now negligible), not like the
// default weight=1.0 case (match).
void testViaWeightKnobHasRealEffect() {
  Design d = parse("fr6route_selftest_viaweight.gr", kTightViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);

  RealUsage ru(d.grid);
  for (int x = 0; x < 5; ++x)
    ru.h[d.grid.hIndex(x, 0, 1)] += 1;
  for (int y = 0; y < 5; ++y)
    ru.v[d.grid.vIndex(5, y, 0)] += 1;

  std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, nodeStackRange,
                       /*viaWeight=*/0.01),
        "the DP produced a result at a low via weight (sanity)");
  check(edgeLayer[0] != edgeLayer[1],
        "at a low via weight, the same tight-via site that made the "
        "default (1.0) match layers is no longer worth avoiding -- the "
        "DP mismatches instead, same as the old independent-greedy path "
        "-- proving the weight parameter genuinely reaches the DP's own "
        "cost computation, not just accepted and ignored");
}

// Via capacity is charged, and genuinely changes a later net's own
// decision, not just passively tracked. A separate fixture (via spacing
// 3, not 10) gives via capacity 3, headroom enough that a first net's
// own moderate wire-cost-vs-via tradeoff can go either way.
const char *kModerateViaGr = R"(grid 8 8 2
vertical capacity 10 10
horizontal capacity 10 10
minimum width 1 1
minimum spacing 0 0
via spacing 3 3
0 0 10 10
num net 1
netAB 0 2 1
5 5 1
55 55 1
0
)";

void testViaCapacityIsChargedAndInfluencesLaterDecisions() {
  Design d = parse("fr6route_selftest_viacap.gr", kModerateViaGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 3, "derived via capacity is 3 (sanity)");

  auto runOneNet = [&]() -> bool {
    RealUsage ru(d.grid); // fresh per net -- isolates the wire-cost side
    for (int x = 0; x < 5; ++x)
      ru.h[d.grid.hIndex(x, 0, 1)] += 5;
    for (int y = 0; y < 5; ++y)
      ru.v[d.grid.vIndex(5, y, 0)] += 5;
    std::vector<Seg> segs = {{0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 5, 0}};
    NetTree t;
    if (!buildNetTree(segs, t))
      return false;
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    if (!layerAssignDP(d.grid, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, nodeStackRange))
      return false;
    RoutedNet rn;
    commitNetTree(d.grid, ru, rvu, 1, t, edgeLayer, nodeStackRange, rn);
    return edgeLayer[0] != edgeLayer[1];
  };

  bool net1Mismatched = runOneNet();
  check(net1Mismatched,
        "the first net, seeing an uncongested via site, prefers "
        "mismatching layers and paying for the still-cheap via over a "
        "larger wire-cost penalty");
  check(rvu.usage[viaIndex(d.grid, 5, 0, 0)] == 1,
        "the first net's via choice was actually charged into RealViaUsage");

  bool net2Mismatched = runOneNet();
  check(!net2Mismatched,
        "the second net, at the identical site now carrying the first "
        "net's charge, finds the same via has become more expensive than "
        "the wire-cost penalty and matches layers instead -- via capacity "
        "genuinely influences the decision, not just tracked for show");
}

// A real 3-edge branch point: parent P arrives at (5,0) from the left,
// child C1 goes up, child C2 continues right. Wire costs are engineered
// so each edge's own truly cheapest layer is overwhelmingly dominant --
// P at layer 0, C1 at layer 3, C2 at layer 5 -- forcing exactly the
// shape fr6route's own header flagged as a likely overcounting case.
const char *kThreeWayBranchGr = R"(grid 12 12 6
vertical capacity 100 100 100 100 100 100
horizontal capacity 100 100 100 100 100 100
minimum width 1 1 1 1 1 1
minimum spacing 0 0 0 0 0 0
via spacing 1 1 1 1 1 1
0 0 10 10
num net 1
netBranch 0 2 1
5 5 1
105 5 1
0
)";

// The decisive test for this generation's own fix. fr6route's own
// pairwise-chain model would have charged chain(0,3)=3 plus chain(0,5)=5
// = 8 via unit-steps to connect this exact solution (double-counting the
// shared 0->3 portion); the true physical cost is one stack spanning
// 0 to 5, 5 steps. Confirms the new model finds the true span, charges
// exactly 5 -- not 8 -- and still lets every edge keep its own
// genuinely cheapest layer rather than being forced to compromise.
void testSharedViaStackReplacesOvercountedPairwiseChains() {
  Design d = parse("fr7route_selftest_branch.gr", kThreeWayBranchGr);
  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 5, "5 layer-pairs among 6 layers (sanity)");
  for (int cap : viaCap)
    check(cap == 10, "derived via capacity is 10 per layer-pair (sanity)");

  // P: (0,0)-(5,0) horizontal -- overwhelmingly prefers layer 0.
  for (int x = 0; x < 5; ++x)
    for (int l = 1; l < 6; ++l)
      ru.h[d.grid.hIndex(x, 0, l)] += 1000;
  // C1: (5,0)-(5,5) vertical -- overwhelmingly prefers layer 3.
  for (int y = 0; y < 5; ++y)
    for (int l = 0; l < 6; ++l)
      if (l != 3)
        ru.v[d.grid.vIndex(5, y, l)] += 1000;
  // C2: (5,0)-(10,0) horizontal -- overwhelmingly prefers layer 5.
  for (int x = 5; x < 10; ++x)
    for (int l = 0; l < 5; ++l)
      ru.h[d.grid.hIndex(x, 0, l)] += 1000;

  std::vector<Seg> segs = {
      {0, 0, 0, 5, 0, 0},  // P
      {5, 0, 0, 5, 5, 0},  // C1
      {5, 0, 0, 10, 0, 0}, // C2
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.nodes.size() == 4 && t.edges.size() == 3,
        "4 nodes, 3 edges meeting at one real branch point (sanity)");

  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result");

  // Identify each edge by its own endpoints, not index order, since
  // buildNetTree's own internal ordering is an implementation detail
  // this test shouldn't depend on.
  int pLayer = -1, c1Layer = -1, c2Layer = -1;
  for (size_t i = 0; i < t.edges.size(); ++i) {
    const auto &e = t.edges[i];
    Point a = t.nodes[e.a], b = t.nodes[e.b];
    if ((a.x == 0 && a.y == 0) || (b.x == 0 && b.y == 0))
      pLayer = edgeLayer[i];
    else if ((a.x == 5 && a.y == 5) || (b.x == 5 && b.y == 5))
      c1Layer = edgeLayer[i];
    else if ((a.x == 10 && a.y == 0) || (b.x == 10 && b.y == 0))
      c2Layer = edgeLayer[i];
  }
  check(pLayer == 0, "P keeps its own overwhelmingly cheapest layer, 0");
  check(c1Layer == 3,
        "C1 keeps its own overwhelmingly cheapest layer, 3 -- not forced "
        "to match P, since even a shared stack is far cheaper than "
        "abandoning its own cheap layer");
  check(c2Layer == 5, "C2 keeps its own overwhelmingly cheapest layer, 5, for the same reason");

  int branchNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 5 && t.nodes[i].y == 0)
      branchNode = static_cast<int>(i);
  check(branchNode >= 0, "found the branch point (5,0) (sanity)");
  if (branchNode < 0)
    return; // Part 215 (review F5): indexing with -1 below would be undefined; the check above has failed already
  check(nodeStackRange[branchNode] == std::make_pair(0, 5),
        "the branch point's own resolved via-stack range is exactly "
        "[0,5] -- one shared stack spanning every layer actually used "
        "there, not fr6route's own separate per-neighbor chains");

  RoutedNet rn;
  commitNetTree(d.grid, ru, rvu, 1, t, edgeLayer, nodeStackRange, rn);
  check(segsConnected(rn.segs),
        "the committed net is fully connected in 3D through the shared "
        "stack");

  int viaSteps = 0;
  for (const Seg &s : rn.segs)
    if (s.x1 == s.x2 && s.y1 == s.y2 && s.l1 != s.l2)
      ++viaSteps;
  check(viaSteps == 5,
        "exactly 5 via unit-steps were emitted at the branch point -- the "
        "true cost of one 0->5 stack. fr6route's own pairwise-chain model "
        "would have charged chain(0,3)=3 plus chain(0,5)=5 = 8 for this "
        "exact same solution, double-counting the shared 0->3 portion");

  long long totalViaUsage = 0;
  for (long long u : rvu.usage)
    totalViaUsage += u;
  check(totalViaUsage == 5,
        "RealViaUsage was charged exactly 5 times total, matching the "
        "shared stack -- not 8, and no layer-pair double-charged");
}

// Tree reconstruction correctly splits a mid-span MSMV-style attachment
// -- the same fixture shape as fr5route's own decisive via-stitching
// regression test, now at the tree-reconstruction level.
void testTreeReconstructionSplitsAMidSpanAttachment() {
  std::vector<Seg> segs = {
      {0, 5, 0, 10, 5, 0}, // an already-committed leg, x=0..10 at y=5
      {5, 5, 0, 5, 8, 0},  // a new leg attaching mid-span at (5,5)
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded");
  check(t.nodes.size() == 4,
        "4 nodes: (0,5), (5,5) [the split point], (10,5), (5,8)");
  check(t.edges.size() == 3,
        "the long leg was split into 2 edges at the attachment point, "
        "plus the attaching leg itself -- 3 total, not the original 2 "
        "raw segments");
}

// Tree validity on a real 4-way branch point (a "plus" shape, no
// mid-span splitting needed since every leg's own endpoint already
// coincides with the hub) -- edges == nodes-1, connected, no cycle, the
// same convention every prior generation's own tests already use.
void testTreeReconstructionIsAlwaysAValidTree() {
  std::vector<Seg> segs = {
      {0, 3, 0, 2, 3, 0}, // A - hub
      {2, 3, 0, 4, 3, 0}, // hub - B
      {2, 0, 0, 2, 3, 0}, // D - hub
      {2, 3, 0, 2, 6, 0}, // hub - C
  };
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded");
  check(t.edges.size() == t.nodes.size() - 1,
        "edge count is exactly one less than node count on a real "
        "4-way branch point");

  std::vector<int> parent(t.nodes.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  bool anyCycle = false;
  for (auto &e : t.edges) {
    int ra = find(e.a), rb = find(e.b);
    if (ra == rb)
      anyCycle = true;
    else
      parent[ra] = rb;
  }
  check(!anyCycle, "no cycle in the reconstructed tree");
  int root = find(0);
  bool allConnected = true;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (find(static_cast<int>(i)) != root)
      allConnected = false;
  check(allConnected, "every node is in one connected component");
}

// Pin access remains correctly enforced through the new DP-based
// pipeline, and its own via is now charged into RealViaUsage --
// previously (fr5route) entirely free. Reuses fr5route's own decisive
// pin-layer-mismatch fixture.
void testPinAccessViaIsChargedIntoRealViaUsage() {
  Design d = parse("fr6route_selftest_pinlayer.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route");

  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  RoutedNet &rn = rep.nets[0];
  dropDegenerateSegs(rn);
  NetTree t;
  check(buildNetTree(rn.segs, t), "tree reconstruction succeeded");
  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, rn.net->minWidth, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result");
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rn);

  long long viaUsageBefore = 0;
  for (long long u : rvu.usage)
    viaUsageBefore += u;
  stitchPinAccessForNetChargingVias(d.grid, rvu, rn);
  long long viaUsageAfter = 0;
  for (long long u : rvu.usage)
    viaUsageAfter += u;
  check(viaUsageAfter > viaUsageBefore,
        "the pin-access stitch actually charged new via usage -- "
        "entirely uncharged in fr5route");

  auto touches = [&](int x, int y, int l) {
    for (const Seg &s : rn.segs)
      if ((s.x1 == x && s.y1 == y && s.l1 == l) || (s.x2 == x && s.y2 == y && s.l2 == l))
        return true;
    return false;
  };
  check(touches(0, 0, 0), "pin A's own declared layer is still reached");
  check(touches(3, 0, 2), "pin B's own declared layer is still reached");
}

// NV2ROUTE_VIA_AWARE=0 must reproduce fr5route's exact independent-
// greedy-plus-stitching behavior, segment for segment -- the same
// "toggle reproduces the parent" pattern every mechanism in this
// project uses.
void testViaAwareOffReproducesFr5routesExactBehavior() {
  Design dOff = parse("fr6route_selftest_viaoff.gr", kTwoLayerBidirGr);
  setenv("NV2ROUTE_VIA_AWARE", "0", 1);
  RunReport repOff = route(dOff, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dOff.grid, repOff.nets);
  unsetenv("NV2ROUTE_VIA_AWARE");

  Design dRef = parse("fr6route_selftest_viaoff_ref.gr", kTwoLayerBidirGr);
  RunReport repRef = route(dRef, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  RealLayerPlan lp(dRef.grid);
  RealUsage ru(dRef.grid);
  for (RoutedNet &rn : repRef.nets) {
    dropDegenerateSegs(rn);
    for (Seg &s : rn.segs)
      assignSegmentLayer(dRef.grid, ru, lp, rn.net->minWidth, s);
    stitchViasForNet(rn);
    stitchPinAccessForNet(rn);
  }

  check(repOff.nets.size() == repRef.nets.size(), "same net count (sanity)");
  for (size_t i = 0; i < repOff.nets.size(); ++i) {
    check(repOff.nets[i].segs.size() == repRef.nets[i].segs.size(),
          "NV2ROUTE_VIA_AWARE=0 produces the same segment count as fr5route's "
          "own independent-greedy pipeline, replayed manually here");
    for (size_t j = 0; j < repOff.nets[i].segs.size() && j < repRef.nets[i].segs.size(); ++j) {
      const Seg &a = repOff.nets[i].segs[j];
      const Seg &b = repRef.nets[i].segs[j];
      check(a.x1 == b.x1 && a.y1 == b.y1 && a.l1 == b.l1 && a.x2 == b.x2 && a.y2 == b.y2 &&
                a.l2 == b.l2,
            "segment " + std::to_string(j) + " of net " + std::to_string(i) +
                " matches fr5route's own output exactly, field for field");
    }
  }
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: decisive tests for the negotiation loop
// itself -- charge-log exactness, the LIFO-relaxable historyVia term, the
// negotiation loop actually fixing something a single pass structurally
// cannot, and the supervised accept/revert primitives it's built from.
// ---------------------------------------------------------------------------

// The risk this file's own header discloses up front: more moving parts
// than any prior generation means more places for a subtle bug to hide,
// and a missed charge-log site (in particular pin-access stitching, logged
// outside commitNetTree proper) would silently corrupt every round after
// the first. Decisive, exact check: commit a net, undo via its own logged
// charge, recommit with unchanged inputs -- ru/rvu must return to their
// pre-commit all-zero state after the undo, and to byte-identical usage
// after the redo, or some increment somewhere was never logged.
void testChargeLogRoundTripIsExact() {
  Design d = parse("nv1route_selftest_chargelog.gr", kPinLayerMismatchGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(rep.nets.size() == 1, "netPin produced a route (sanity)");
  RoutedNet rn = rep.nets[0];
  dropDegenerateSegs(rn);

  RealLayerPlan lp(d.grid);
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);

  NetTree t;
  check(buildNetTree(rn.segs, t), "tree reconstruction succeeded (sanity)");
  std::vector<int> edgeLayer;
  int rootLayer = 0;
  std::vector<std::pair<int, int>> nodeStackRange;
  check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, rn.net->minWidth, t, edgeLayer, rootLayer,
                       nodeStackRange),
        "the DP produced a result (sanity)");

  RoutedNet rnA = rn;
  NetCharge chargeA;
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rnA, &chargeA);
  stitchPinAccessForNetChargingVias(d.grid, rvu, rnA, &chargeA);
  check(!chargeA.rvuIdx.empty(),
        "pin-access stitching logged at least one via charge (sanity -- this "
        "fixture is engineered, same as testPinAccessViaIsChargedIntoRealViaUsage, "
        "to need one)");

  std::vector<int32_t> ruHAfter = ru.h, ruVAfter = ru.v, rvuAfter = rvu.usage;

  undoCharge(ru, rvu, chargeA);
  bool allZero = true;
  for (long long v : ru.h)
    if (v != 0)
      allZero = false;
  for (long long v : ru.v)
    if (v != 0)
      allZero = false;
  for (long long v : rvu.usage)
    if (v != 0)
      allZero = false;
  check(allZero,
        "undoing the logged charge returns ru/rvu exactly to their pre-commit "
        "all-zero state -- every increment commitNetTree and "
        "stitchPinAccessForNetChargingVias made was logged");

  RoutedNet rnB = rn;
  NetCharge chargeB;
  commitNetTree(d.grid, ru, rvu, rn.net->minWidth, t, edgeLayer, nodeStackRange, rnB, &chargeB);
  stitchPinAccessForNetChargingVias(d.grid, rvu, rnB, &chargeB);

  check(ru.h == ruHAfter && ru.v == ruVAfter && rvu.usage == rvuAfter,
        "redoing the identical commit with unchanged inputs reproduces "
        "byte-identical ru/rvu usage -- no charge site was missed");
  check(rnA.segs.size() == rnB.segs.size(), "same segment count on redo (sanity)");
  for (size_t j = 0; j < rnA.segs.size() && j < rnB.segs.size(); ++j) {
    const Seg &a = rnA.segs[j], &b = rnB.segs[j];
    check(a.x1 == b.x1 && a.y1 == b.y1 && a.l1 == b.l1 && a.x2 == b.x2 && a.y2 == b.y2 &&
              a.l2 == b.l2,
          "segment " + std::to_string(j) + " identical on redo");
  }
}

// A two-layer, single-via-layer-pair fixture, capacity 1: five structurally
// identical branch nets (parent leg overwhelmingly prefers layer 0, child
// leg overwhelmingly prefers layer 1 -- same convention as kModerateViaGr/
// testViaCapacityIsChargedAndInfluencesLaterDecisions) contend for one via
// site. Each net's own wire cost is isolated via a fresh RealUsage per net
// (that same established convention) -- the only resource genuinely shared
// across all "5 nets" here is RealViaUsage and this file's own new
// historyVia term, exactly isolating the one new mechanism under test.
// Hand-derived exactly, via the DP's own real resolveNode mechanics (not a
// simplified abstraction): at the branch node, "mismatch" (child free to
// pick its own cheapest layer, 1, plus the via) costs spanCost+1; "match"
// (child forced to the parent's layer, 0, at its own 49-unit-preload
// penalty) costs exactly 50. resolveNode picks match iff spanCost+1>=50,
// i.e. spanCost>=49 (ties favor the first-evaluated candidate, (0,0)).
const char *kFiveWayViaContentionGr = R"(grid 4 4 2
vertical capacity 1 1
horizontal capacity 1 1
minimum width 1 1
minimum spacing 0 0
via spacing 10 10
0 0 10 10
num net 1
netDummy 0 2 1
0 0 1
10 10 1
0
)";

// The decisive test for this file's own core new claim: negotiation
// (history-driven reprocessing across rounds, using the real
// layerAssignDP and updateViaHistory production functions) reduces via
// overflow that a single pass -- fr7route's own one-shot assignLayers, or
// equivalently this file's own round 0 alone -- has no way to fix,
// regardless of net processing order.
void testNegotiationReducesViaOverflowASinglePassCannotFix() {
  Design d = parse("nv1route_selftest_contention.gr", kFiveWayViaContentionGr);
  RealLayerPlan lp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  check(viaCap.size() == 1 && viaCap[0] == 1, "derived via capacity is exactly 1 (sanity)");

  std::vector<Seg> segs = {{0, 0, 0, 1, 0, 0}, {1, 0, 0, 1, 1, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  int branchNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 1 && t.nodes[i].y == 0)
      branchNode = static_cast<int>(i);
  check(branchNode >= 0, "found the branch node (1,0) (sanity)");
  if (branchNode < 0)
    return; // Part 215 (review F5): nodeStackRange[-1] below would be undefined; the check above has failed
  size_t viaIdx = viaIndex(d.grid, 1, 0, 0);

  RealViaUsage rvu(d.grid);
  ViaPrices vp(d.grid);

  auto freshRu = [&]() {
    RealUsage ru(d.grid);
    ru.h[d.grid.hIndex(0, 0, 1)] += 49; // parent leg's one tile, its wrong layer
    ru.v[d.grid.vIndex(1, 0, 0)] += 49; // child leg's one tile, its wrong layer
    return ru;
  };
  // true if the DP chose to mismatch (via) at the branch node.
  auto runOne = [&]() -> bool {
    RealUsage ru = freshRu();
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                         nodeStackRange, /*viaWeight=*/1.0, &vp.hist),
          "the DP produced a result");
    return nodeStackRange[branchNode].first < nodeStackRange[branchNode].second;
  };

  // Round 0: every net mismatches regardless of order -- spanCost never
  // exceeds 5 (the 5th net's own usage-based cost), nowhere near the
  // 49-unit margin needed to flip even one net, let alone all five.
  for (int i = 0; i < 5; ++i) {
    bool mismatched = runOne();
    check(mismatched, "round 0, net " + std::to_string(i + 1) +
                           " mismatches: spanCost stays well under the 49-unit "
                           "match threshold for every one of the 5 nets");
    if (mismatched)
      rvu.usage[viaIdx] += 1;
  }
  check(rvu.usage[viaIdx] == 5, "round 0 settles at usage 5 against capacity 1 (sanity)");

  updateViaHistory(d.grid, vp, rvu, viaCap, /*histStep=*/0.0, HistMode::kMultiplicative,
                    /*histFactor=*/3.0, /*streakThreshold=*/1, /*scaleByOverflow=*/true,
                    /*relaxEnabled=*/true, 1.0, 0.0);
  check(std::abs(vp.hist[viaIdx] - 11.0) < 1e-9,
        "historyVia grows to exactly 1*(1+(3-1)*5)=11 after round 0's boundary "
        "update (usage/cap ratio 5.0, multiplicative factor 3.0) -- the real "
        "production updateViaHistory function, not a stand-in");

  // Round 1: reprocess all 5 (all still touch the now-overflowed via site),
  // historyVia held at 11 for the whole round (never mid-round). The first
  // net reprocessed sees spanCost=11*(4+1)=55>=49 -- flips to match. Every
  // subsequent net in this same round sees spanCost=11*(3+1)=44<49 --
  // stays mismatched. This is the decisive claim: round 0 (any order)
  // could never produce this, since round 0's own spanCost values (1..5)
  // never came close to 49.
  int flips = 0;
  for (int i = 0; i < 5; ++i) {
    rvu.usage[viaIdx] -= 1; // undo this net's own prior mismatch charge
    bool mismatched = runOne();
    if (mismatched)
      rvu.usage[viaIdx] += 1;
    else
      ++flips;
  }
  check(flips == 1, "exactly one net flips to match in round 1");
  check(rvu.usage[viaIdx] == 4,
        "round 1 reduces via usage from 5 to 4 (overflow 4 to 3) -- something no "
        "single pass, at any net order, could ever produce");

  updateViaHistory(d.grid, vp, rvu, viaCap, 0.0, HistMode::kMultiplicative, 3.0, 1, true, true,
                    1.0, 0.0);
  check(std::abs(vp.hist[viaIdx] - 99.0) < 1e-9,
        "historyVia grows to exactly 11*(1+(3-1)*4)=99 after round 1's boundary "
        "update");

  // Round 2: the 4 remaining mismatched nets are reprocessed (the one that
  // already flipped no longer touches this via site). historyVia=99 now
  // dwarfs the 50-cost match alternative at every step (spanCost 396, 297,
  // 198, 99 -- all comfortably over 49) -- every one flips this round.
  int flips2 = 0;
  for (int i = 0; i < 4; ++i) {
    rvu.usage[viaIdx] -= 1;
    bool mismatched = runOne();
    if (mismatched)
      rvu.usage[viaIdx] += 1;
    else
      ++flips2;
  }
  check(flips2 == 4, "all 4 remaining nets flip to match in round 2");
  check(rvu.usage[viaIdx] == 0,
        "via usage reaches exactly 0 -- zero overflow at this site -- two "
        "negotiation rounds after a single pass left it permanently 4 over "
        "capacity");
}

// Direct ports of this file's own 2D-stage relaxation tests
// (testDisablingRelaxReproducesPermanentBehavior,
// testRelaxationUndoesExactlyOneStepPerCleanRound), applied to
// updateViaHistory/ViaPrices instead of updateHistory/Prices -- same
// mechanism, new domain, so the same decisive tests apply verbatim.
void testViaHistoryRelaxDisabledReproducesPermanentBehavior() {
  Design d = parse("nv1route_selftest_viahist_permanent.gr", kModerateViaGr);
  ViaPrices vp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  size_t idx = viaIndex(d.grid, 0, 0, 0);

  RealViaUsage over(d.grid);
  over.usage[idx] = 100; // comfortably over capacity 3
  updateViaHistory(d.grid, vp, over, viaCap, 1.0, HistMode::kAdditive, 1.0, 1, false,
                    /*relaxEnabled=*/false);
  double afterOver = vp.hist[idx];
  check(afterOver > 1.0, "historyVia grew while the site was over capacity");

  RealViaUsage under(d.grid);
  under.usage[idx] = 0; // now comfortably clear
  updateViaHistory(d.grid, vp, under, viaCap, 1.0, HistMode::kAdditive, 1.0, 1, false,
                    /*relaxEnabled=*/false);
  check(vp.hist[idx] == afterOver,
        "with NV2ROUTE_HIST_RELAX=0, historyVia never decreases even after "
        "usage drops back under capacity -- reproduces iroute's exact "
        "permanent behavior, the fallback this mechanism can always be "
        "switched off back to, same as the 2D stage's own history term");
}

void testViaHistoryRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("nv1route_selftest_viahist_relax.gr", kModerateViaGr);
  ViaPrices vp(d.grid);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid);
  size_t idx = viaIndex(d.grid, 0, 0, 0);

  RealViaUsage over(d.grid);
  over.usage[idx] = 100;
  updateViaHistory(d.grid, vp, over, viaCap, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(vp.hist[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows historyVia to exactly 1.05");

  RealViaUsage under(d.grid);
  under.usage[idx] = 0;
  updateViaHistory(d.grid, vp, under, viaCap, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(vp.hist[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- historyVia "
        "is back to the 1.0 no-op baseline, not just 'lower than before', "
        "proving the LIFO stack genuinely drives this, not just growth being "
        "disabled");
}

// Tests the exact primitives the negotiation loop's own accept/revert step
// (negotiateViaAssignment's own inline logic) is built from --
// localOverflow, undoCharge, applyCharge -- with fully hand-computed
// numbers, independent of the DP: a net's old charge sits at a resource
// already 2 over capacity (cap 3, usage 5); its candidate new charge would
// move it to a *different* resource sitting 5 over (cap 2, usage 7).
// localOverflow must detect the new charge is strictly worse over the
// union of both resources, and after undo(new)+apply(old) -- exactly what
// negotiateViaAssignment's own revert branch does -- both resources must
// be back to their exact pre-attempt usage.
void testSupervisedRevertPrimitivesDetectAndUndoAWorseningMove() {
  Design d = parse("nv1route_selftest_revert.gr", kModerateViaGr);
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid); // [3]
  RealUsage ru(d.grid);
  RealViaUsage rvu(d.grid);

  size_t oldIdx = viaIndex(d.grid, 0, 0, 0);
  size_t newIdx = viaIndex(d.grid, 1, 0, 0);
  rvu.usage[oldIdx] = 5; // 2 over capacity 3
  rvu.usage[newIdx] = 7; // 4 over capacity 3 (before this net's own +1 below)

  NetCharge oldCharge;
  oldCharge.rvuIdx = {static_cast<uint32_t>(oldIdx)}; // this net's own current contribution: 1 of the 5 at oldIdx
  NetCharge newCharge;
  newCharge.rvuIdx = {static_cast<uint32_t>(newIdx)}; // the candidate: 1 more at newIdx, now 8 total there

  long long localBefore = localOverflow(d.grid, ru, rvu, viaCap, oldCharge, oldCharge);
  check(localBefore == 2, "before: only oldIdx is in the union (old,old) -- 5-3=2 over (sanity)");

  // Mirror negotiateViaAssignment's own sequence exactly: undo old, then
  // apply new (standing in for the DP's own commit, since this test is
  // about the revert primitives, not the DP's decision).
  undoCharge(ru, rvu, oldCharge);
  check(rvu.usage[oldIdx] == 4, "undoing the old charge removes exactly this net's own 1 (sanity)");
  applyCharge(ru, rvu, newCharge);
  check(rvu.usage[newIdx] == 8, "applying the new charge adds exactly this net's own 1 (sanity)");

  long long localAfter = localOverflow(d.grid, ru, rvu, viaCap, oldCharge, newCharge);
  check(localAfter == (4 - 3) + (8 - 3), "after: union of (old,new) = {oldIdx,newIdx} -- 1+5=6 over");
  check(localAfter > localBefore,
        "localOverflow correctly detects the candidate move is strictly "
        "worse (6 > 2) -- this is exactly the condition negotiateViaAssignment "
        "reverts on");

  // The revert itself: undo new, reapply old -- negotiateViaAssignment's
  // own rejection branch, verbatim.
  undoCharge(ru, rvu, newCharge);
  applyCharge(ru, rvu, oldCharge);
  check(rvu.usage[oldIdx] == 5 && rvu.usage[newIdx] == 7,
        "after undo(new)+apply(old), both resources are back to their exact "
        "pre-attempt usage -- the revert is byte-for-byte, not approximate");
}

// ---------------------------------------------------------------------------
// This file's own new mechanism: decisive tests for historyWire.
// ---------------------------------------------------------------------------

// A two-layer, single straight edge, no via/branch at all -- cleanly
// isolates wire history from via history entirely. `minimum spacing 0 10`
// gives layer 0 amount=1, layer 1 amount=11: a large, fixed gap so layer 1
// (never used, never overflowed, its own historyWire never grows) stays a
// constant 11/3 alternative throughout, exactly mirroring why the via test
// needed a similarly dominant, fixed gap.
const char *kWireContentionGr = R"(grid 4 4 2
vertical capacity 3 3
horizontal capacity 3 3
minimum width 1 1
minimum spacing 0 10
via spacing 10 10
0 0 10 10
num net 1
netDummy 0 2 1
0 0 1
10 10 1
0
)";

// The decisive test for this file's own core new claim: historyWire
// (history-driven reprocessing across rounds, using the real layerAssignDP
// and updateWireHistory production functions) fixes real per-layer wire
// overflow that a single pass -- nv1route's own one-shot round 0 -- has no
// way to fix, regardless of net processing order. Direct counterpart to
// nv1route's own testNegotiationReducesViaOverflowASinglePassCannotFix,
// same structure, wire domain instead of via.
void testWireHistoryFixesRealOverflowASinglePassCannotFix() {
  Design d = parse("nv2route_selftest_contention.gr", kWireContentionGr);
  RealLayerPlan lp(d.grid);
  RealViaUsage rvu(d.grid);                             // unused: no via in this fixture
  std::vector<int> viaCap = viaCapacityPerLayerPair(d.grid); // unused
  RealUsage ru(d.grid);
  WirePrices wireHist(d.grid);

  std::vector<Seg> segs = {{0, 0, 0, 1, 0, 0}};
  NetTree t;
  check(buildNetTree(segs, t), "tree reconstruction succeeded (sanity)");
  check(t.edges.size() == 1, "a single straight edge, no branch/via (sanity)");

  size_t idx0 = d.grid.hIndex(0, 0, 0); // layer 0's tile-edge
  size_t idx1 = d.grid.hIndex(0, 0, 1); // layer 1's tile-edge (never used)

  // true if the DP chose layer 0 (the cheap, contended layer).
  auto runOne = [&]() -> bool {
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nodeStackRange;
    check(layerAssignDP(d.grid, ru, rvu, viaCap, lp, /*width=*/1, t, edgeLayer, rootLayer,
                         nodeStackRange, /*viaWeight=*/1.0, /*historyVia=*/nullptr, &wireHist.histH,
                         &wireHist.histV),
          "the DP produced a result");
    return edgeLayer[0] == 0;
  };

  // Round 0: every net picks layer 0 regardless of order -- ratio stays
  // under layer 1's fixed 11/3≈3.667 even at usage=4 ((4+1)/3=1.667).
  for (int i = 0; i < 5; ++i) {
    bool pickedLayer0 = runOne();
    check(pickedLayer0, "round 0, net " + std::to_string(i + 1) +
                             " picks layer 0: ratio stays well under layer 1's fixed 11/3 "
                             "for every one of the 5 nets");
    if (pickedLayer0)
      ru.h[idx0] += 1;
  }
  check(ru.h[idx0] == 5, "round 0 settles at usage 5 against capacity 3 (sanity)");
  check(ru.h[idx1] == 0, "layer 1 never used in round 0 (sanity)");

  updateWireHistory(d.grid, wireHist, ru, /*histStep=*/0.0, HistMode::kMultiplicative,
                     /*histFactor=*/3.0, /*streakThreshold=*/1, /*scaleByOverflow=*/true,
                     /*relaxEnabled=*/true, 1.0, 0.0);
  check(std::abs(wireHist.histH[idx0] - 13.0 / 3.0) < 1e-9,
        "historyWireH grows to exactly 1*(1+(3-1)*(5/3))=13/3 after round 0's "
        "boundary update (usage/cap ratio 5/3, multiplicative factor 3.0) -- "
        "the real production updateWireHistory function, not a stand-in");
  check(wireHist.histH[idx1] == 1.0, "layer 1's own historyWireH never grew (sanity)");

  // Round 1: reprocess all 5 (all still touch the now-overflowed edge),
  // historyWireH held at 13/3 for the whole round. Hand-derived: nets 1-3
  // flip to layer 1 (hist-scaled ratio 65/9, 52/9, 13/3 all exceed 11/3);
  // nets 4-5 stay on layer 0 (26/9 < 11/3). This is the decisive claim:
  // round 0 (any order) could never produce this, since round 0's own
  // ratio never came close to 11/3.
  int flips = 0;
  for (int i = 0; i < 5; ++i) {
    ru.h[idx0] -= 1; // undo this net's own prior layer-0 charge
    bool pickedLayer0 = runOne();
    if (pickedLayer0)
      ru.h[idx0] += 1;
    else
      ++flips;
  }
  check(flips == 3, "exactly 3 of the 5 nets flip to layer 1 in round 1");
  check(ru.h[idx0] == 2,
        "round 1 reduces layer 0's usage from 5 to 2 -- zero overflow (capacity 3) -- "
        "something no single pass, at any net order, could ever produce");
}

// Direct ports of nv1route's own two decisive via-history relaxation
// tests, applied to updateWireHistory/WirePrices instead of
// updateViaHistory/ViaPrices -- same mechanism, new domain.
void testWireHistoryRelaxDisabledReproducesPermanentBehavior() {
  Design d = parse("nv2route_selftest_wirehist_permanent.gr", kModerateViaGr);
  WirePrices wp(d.grid);
  size_t idx = d.grid.hIndex(0, 0, 0);

  RealUsage over(d.grid);
  over.h[idx] = 100; // comfortably over whatever capacity is declared there
  updateWireHistory(d.grid, wp, over, 1.0, HistMode::kAdditive, 1.0, 1, false,
                     /*relaxEnabled=*/false);
  double afterOver = wp.histH[idx];
  check(afterOver > 1.0, "historyWireH grew while the edge was over capacity");

  RealUsage under(d.grid);
  under.h[idx] = 0; // now comfortably clear
  updateWireHistory(d.grid, wp, under, 1.0, HistMode::kAdditive, 1.0, 1, false,
                     /*relaxEnabled=*/false);
  check(wp.histH[idx] == afterOver,
        "with NV2ROUTE_WIRE_HIST_RELAX=0, historyWireH never decreases even "
        "after usage drops back under capacity -- reproduces iroute's exact "
        "permanent behavior, the fallback this mechanism can always be "
        "switched off back to, same as historyVia's own relax test");
}

void testWireHistoryRelaxationUndoesExactlyOneStepPerCleanRound() {
  Design d = parse("nv2route_selftest_wirehist_relax.gr", kModerateViaGr);
  WirePrices wp(d.grid);
  size_t idx = d.grid.hIndex(0, 0, 0);

  RealUsage over(d.grid);
  over.h[idx] = 100;
  updateWireHistory(d.grid, wp, over, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(wp.histH[idx] - 1.05) < 1e-9,
        "one dirty round with step=0.05 grows historyWireH to exactly 1.05");

  RealUsage under(d.grid);
  under.h[idx] = 0;
  updateWireHistory(d.grid, wp, under, 0.05, HistMode::kAdditive, 1.0);
  check(std::abs(wp.histH[idx] - 1.0) < 1e-9,
        "one clean round pops and undoes that exact 0.05 step -- historyWireH "
        "is back to the 1.0 no-op baseline, proving the LIFO stack genuinely "
        "drives this, not just growth being disabled");
}

// NV2ROUTE_WIRE_AWARE=0 must reproduce nv1route's exact output, segment for
// segment -- the regression-continuity test every generation in this
// lineage carries, this file's own version for the new mechanism.
// 5 real, independent 2-pin nets, each with the identical pair of pins --
// each net's own 2D route (a straight, deterministic, no-maze-search leg,
// route()'s own simplest case) lands on the exact same single horizontal
// tile-edge, reproducing kWireContentionGr's own hand-derived scenario
// through the REAL end-to-end pipeline (route() -> negotiateViaAssignment())
// instead of hand-rolled layerAssignDP calls. 2D-collapsed capacity here is
// 4+2=6 against 5 nets' demand, so the 2D stage itself sees no overflow at
// all -- this is purely a real-per-layer distribution problem, exactly the
// gap this generation exists to close. Capacities are deliberately more
// modest than kWireContentionGr's own (which relied on layer 1 never
// actually being committed to -- fine for a hand-rolled DP-only test, but
// a real commit charges layer 1's own declared capacity too, so this
// fixture keeps layer 1 physically able to hold one real net's worth of
// traffic without a separate, larger overflow appearing there instead).
const char *kFiveNetWireContentionGr = R"(grid 4 4 2
vertical capacity 3 3
horizontal capacity 4 2
minimum width 1 1
minimum spacing 0 2
via spacing 10 10
0 0 10 10
num net 5
net0 0 2 1
0 0 1
10 0 1
net1 1 2 1
0 0 1
10 0 1
net2 2 2 1
0 0 1
10 0 1
net3 3 2 1
0 0 1
10 0 1
net4 4 2 1
0 0 1
10 0 1
0
)";

// NV2ROUTE_WIRE_AWARE=0 must reproduce nv1route's own structural
// limitation exactly (no memory of wire congestion, so every net keeps
// picking the same layer forever) -- and, on the same real fixture with
// wire-awareness on and a fast-growing history, at least one net's real
// layer choice must differ, proving the flag genuinely changes the real
// end-to-end outcome rather than being an inert switch. (Round cap 1 for
// the "on" case: this fixture's own capacities are tight enough that a
// later round would relax the very history that caused the first round's
// flip and negotiate it back -- an real, honest dynamic, not a bug, but
// this test's own job is only to prove the mechanism is *not inert*, so it
// looks at the first round's own decisive effect rather than long-run
// convergence, which the real-benchmark run is what actually settles.)
void testWireAwareOffReproducesNv1routesLimitAndIsNotADeadFlag() {
  Design dOff = parse("nv2route_selftest_wireoff.gr", kFiveNetWireContentionGr);
  setenv("NV2ROUTE_WIRE_AWARE", "0", 1);
  RunReport repOff = route(dOff, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  negotiateViaAssignment(dOff.grid, repOff.nets);
  unsetenv("NV2ROUTE_WIRE_AWARE");
  for (const RoutedNet &rn : repOff.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2))
        check(s.l1 == 0, "with wire-awareness off, every net's wire leg stays on "
                          "layer 0 -- the same round-0 decision repeated forever, "
                          "nv1route's own exact structural limitation");

  Design dOn = parse("nv2route_selftest_wireon.gr", kFiveNetWireContentionGr);
  RunReport repOn = route(dOn, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  HistKnobs fastWireGrowth{/*step=*/0.0, HistMode::kMultiplicative, /*factor=*/3.0,
                            /*streakThreshold=*/1,   /*scaleByOverflow=*/true,
                            /*relaxEnabled=*/true,   /*ratioExponent=*/1.0,
                            /*exponentEscalation=*/0.0};
  negotiateViaAssignment(dOn.grid, repOn.nets, /*maxRounds=*/1, 20, 1.0, HistKnobs(),
                          fastWireGrowth);
  bool sawLayer1 = false;
  for (const RoutedNet &rn : repOn.nets)
    for (const Seg &s : rn.segs)
      if (s.l1 == s.l2 && (s.x1 != s.x2 || s.y1 != s.y2) && s.l1 == 1)
        sawLayer1 = true;
  check(sawLayer1,
        "the SAME fixture, wire-awareness on with a fast-growing history, "
        "moves at least one net's real wire leg to layer 1 after just one "
        "negotiation round -- the flag genuinely changes the real "
        "end-to-end outcome, not a dead switch");
}

// This file's own decisive claim: a giant net's fixed-corner initial
// commit (routeGiantNet always routes horizontal-then-vertical) can be
// stuck permanently contesting an edge that an alternate corner
// (vertical-then-horizontal) would avoid entirely -- and letting the
// giant net into the fast tier's own price-aware corner choice, not
// merely running more rounds, is what escapes it.
//
// Fixture: a 4x2x1 grid, horizontal capacity 1 (tight), vertical capacity
// 10 (generous, never the bottleneck). "giant" has 1006 raw pins --
// >kGiantNetPins, so `n.pins.size() > kGiantNetPins` is genuinely true,
// not just close -- but only two *unique* points after dedup: 1005
// duplicate pins at (0,0) and one real pin at (3,1), so the routed
// topology itself stays a single trivial two-point leg. routeGiantNet's
// own fixed corner for that leg is horizontal along y=0 from x=0 to 3,
// then vertical along x=3 from y=0 to 1 -- crossing edge (1,0)-(2,0).
// "blocker" is a plain two-pin net occupying exactly that same edge
// (pins at (1,0) and (2,0)), so with capacity 1, both nets sharing it is
// a real overflow of 1, permanently, unless something reroutes. The
// untested alternate corner (vertical along x=0 from y=0 to 1, then
// horizontal along y=1 from x=0 to 3) shares no edge with blocker at all.
void testGiantNetJoinsFastTierAndEscapesAPermanentlyCongestedCorner() {
  std::string gr = "grid 4 2 1\n"
                    "vertical capacity 10\n"
                    "horizontal capacity 1\n"
                    "minimum width 1\n"
                    "minimum spacing 0\n"
                    "via spacing 0\n"
                    "0 0 1 1\n"
                    "num net 2\n";
  const int giantPins = 1006; // > kGiantNetPins (1000)
  gr += "giant 1 " + std::to_string(giantPins) + " 1\n";
  for (int i = 0; i < giantPins - 1; ++i)
    gr += "0 0 1\n";
  gr += "3 1 1\n";
  gr += "blocker 2 2 1\n";
  gr += "1 0 1\n";
  gr += "2 0 1\n";
  gr += "0\n";

  Design d = parse("nv3route_selftest_giant.gr", gr.c_str());
  check(d.nets[0].pins.size() > kGiantNetPins,
        "the fixture's giant net genuinely exceeds the giant-net pin threshold");

  setenv("NV3ROUTE_GIANT_FAST_TIER", "0", 1);
  RunReport frozen = route(d, 5, 0, 0.02, 1.0, HistMode::kAdditive, 1.0);
  unsetenv("NV3ROUTE_GIANT_FAST_TIER");
  check(frozen.bestOverflow == 1,
        "with the giant net excluded from the fast tier (NV3ROUTE_GIANT_FAST_TIER=0, "
        "reproducing nv2route's exact prior behavior), it stays frozen on "
        "routeGiantNet's own fixed corner forever, permanently contesting "
        "blocker's edge -- overflow never resolves");

  RunReport joined = route(d, 5, 0, 0.02, 1.0, HistMode::kAdditive, 1.0);
  check(joined.bestOverflow == 0,
        "with the giant net included in the fast tier (this file's own new "
        "default), it discovers the untested alternate corner under real "
        "price pressure and moves off blocker's edge entirely -- the flag "
        "genuinely changes the real end-to-end outcome, not a dead switch");
}

// This file's own real change over nv3route is entirely two compiled-in
// default values (VROUTE_THRESHOLD 0.2->0.05, VROUTE_POWER 2.5->1.5) --
// no new control flow. Both `power` inside priceExponentTerm and the
// `threshold` returned by defaultThreshold() are cached in a function-
// local `static const double`, latched permanently on that function's
// first call in this process (see priceExponentTerm/defaultThreshold's
// own definitions) -- so a test cannot toggle them mid-process via
// setenv/unsetenv the way testGiantNetJoinsFastTierAndEscapesA... toggles
// NV3ROUTE_GIANT_FAST_TIER (read fresh via std::getenv on every call,
// never cached). This test instead calls the real, unmodified
// priceExponentTerm/defaultThreshold functions directly and checks their
// output against an independently hand-computed reference for nv3route's
// OLD defaults at the same usage/capacity ratio -- proving both new
// defaults are wired through to the real formula and that the curve they
// produce is materially different from nv3route's own, not merely a
// cosmetic renumbering.
void testRetunedCoefficientsProduceAMateriallyDifferentPricingCurve() {
  const double usage = 0.6, cap = 1.0; // ratio 0.6, comfortably over both thresholds
  double threshold = defaultThreshold();
  check(threshold == 0.05, "this file's own default VROUTE_THRESHOLD is 0.05, not "
                            "nv3route's 0.2 -- defaultThreshold() returns the value "
                            "this file actually compiled in");

  double newCurve = priceExponentTerm(usage, cap, threshold);
  // Hand-computed for threshold=0.05, power=1.5 (this file's own defaults):
  //   r = (0.6 - 0.05) / (1.0 - 0.05) = 0.55 / 0.95
  //   result = r^1.5
  double expectedNew = std::pow((0.6 - 0.05) / (1.0 - 0.05), 1.5);
  check(std::abs(newCurve - expectedNew) < 1e-9,
        "priceExponentTerm(0.6, 1.0, defaultThreshold()) matches the hand-computed "
        "threshold=0.05/power=1.5 formula exactly -- the real production function, "
        "not a copy of it");

  // nv3route's own OLD defaults (threshold=0.2, power=2.5) at the identical
  // ratio, computed independently (not through priceExponentTerm, whose
  // cached `power` static is now permanently 1.5 for this process) --
  // this is the reference this file's own real behavioral change is
  // measured against.
  double oldCurve = std::pow((0.6 - 0.2) / (1.0 - 0.2), 2.5);
  check(std::abs(newCurve - oldCurve) > 0.2,
        "the retuned curve differs from nv3route's own old-default curve by a "
        "large, unmistakable margin at the same usage/capacity ratio (0.44 vs. "
        "0.18) -- a real change to how hard the fast tier prices moderate "
        "congestion, not a rounding-level tweak");
}

// This file's own mechanism: the decisive claim, on the real search. A
// 40x40 grid; source tile (5,20), sink tile (15,20). Every horizontal edge
// crossing from x=10 to x=11 is priced at 1e6 (a wall across the direct
// route) except the one at y=5 (price 1, a gap 15 rows off the pins'
// shared row). The pins' bounding box is rows 20..20, so margin 6 searches
// rows 14..26 -- the gap is outside, a path still exists (through the
// wall), so the no-path doubling never fires and the search crosses the
// wall. Margin 16 searches rows 4..36, which contains the gap: the detour
// (~30 extra tiles at price ~1) is far cheaper than one 1e6 edge.
void testWiderBoxFindsDetourFixedMarginCannot() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 0 0\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnetW 0 2 1\n55 205 1\n155 205 1\n0\n";
  Design d = parse("nv11route_selftest_wall.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  LayerPlan lp(g);
  SearchRegion region;
  for (int y = 0; y < g.ny; ++y)
    pr.h[g.hIndex(10, y, 0)] = (y == 5) ? 1.0 : 1e6;

  std::vector<PathNode> sources = {{5, 20, -1}};
  std::vector<Point> sinks = {{15, 20}};
  auto wallCrossingRow = [&](const MSMVResult &r) {
    for (size_t i = 1; i < r.path.size(); ++i) {
      const PathNode &a = r.path[i - 1], &b = r.path[i];
      if (a.y == b.y && std::min(a.x, b.x) == 10 && std::max(a.x, b.x) == 11)
        return a.y;
    }
    return -1;
  };

  MSMVResult narrow = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, /*marginOverride=*/6);
  check(narrow.found, "margin 6 still finds a path (through the wall) -- the doubling retry never fires");
  int narrowRow = wallCrossingRow(narrow);
  check(narrowRow >= 14 && narrowRow <= 26,
        "with margin 6 the route crosses the wall inside rows 14..26 (at a 1e6 edge) -- "
        "the cheap gap at row 5 is outside the search box");

  MSMVResult wide = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, /*marginOverride=*/16);
  check(wide.found, "margin 16 finds a path");
  check(wallCrossingRow(wide) == 5,
        "with margin 16 the route detours through the cheap gap at row 5 -- the "
        "structural limit a small fixed box imposes, removed by a wider one");

  MSMVResult dflt = routeStepMSMV(g, pr, lp, region, sources, sinks, 1);
  check(wallCrossingRow(dflt) == narrowRow,
        "no override (-1) behaves exactly like margin 6, VROUTE_MAX_MARGIN's default -- "
        "nv4route's behavior is unchanged when the mechanism is off");
}

// This file's own mechanism: the growth arithmetic, hand-verified,
// including the cap clamp and the long-long guard against int overflow on
// absurd round counts.
void testBoxGrowthArithmetic() {
  BoxGrowth b;
  b.mode = BoxMode::Round;
  b.init = 24;
  b.grow = 1;
  b.cap = 30;
  check(boxMarginForRound(b, 0) == 24, "round 0 -> init (24)");
  check(boxMarginForRound(b, 5) == 29, "round 5 -> 24 + 5*1 = 29");
  check(boxMarginForRound(b, 6) == 30, "round 6 -> 30, exactly the cap");
  check(boxMarginForRound(b, 100) == 30, "round 100 -> clamped at cap 30");
  b.grow = 1000000;
  check(boxMarginForRound(b, INT_MAX) == 30,
        "an absurd round*grow product that would overflow int is still clamped at cap");
  b.grow = 0;
  check(boxMarginForRound(b, 50) == 24, "grow 0 -> margin stays at init forever");

  BoxGrowth p;
  p.mode = BoxMode::PerNet;
  p.init = 6;
  p.grow = 6;
  p.cap = 20;
  check(grownBoxMargin(p, 6) == 12 && grownBoxMargin(p, 12) == 18, "6 -> 12 -> 18 by +6 steps");
  check(grownBoxMargin(p, 18) == 20 && grownBoxMargin(p, 20) == 20, "18 -> 20 (cap), then stays at 20");
}

// This file's own mechanism: Phase B's per-search margin decisions, via the
// same two helpers route()'s own loop calls.
void testBoxMarginModesInPhaseBHelpers() {
  BoxGrowth fixed;
  RoutedNet a;
  assignBoxMarginForSearch(fixed, 7, a);
  growBoxMarginIfStillOverflowed(fixed, true, a);
  check(a.boxMargin == -1,
        "Fixed mode never touches boxMargin (stays -1, i.e. VROUTE_MAX_MARGIN) -- nv4route parity");

  BoxGrowth round;
  round.mode = BoxMode::Round;
  round.init = 10;
  round.grow = 2;
  round.cap = 100;
  RoutedNet b;
  assignBoxMarginForSearch(round, 3, b);
  check(b.boxMargin == 16, "Round mode, round 3: 10 + 3*2 = 16");
  growBoxMarginIfStillOverflowed(round, true, b);
  check(b.boxMargin == 16, "Round mode ignores per-net overflow -- growth is by round only");

  BoxGrowth per;
  per.mode = BoxMode::PerNet;
  per.init = 6;
  per.grow = 6;
  per.cap = 96;
  RoutedNet c;
  assignBoxMarginForSearch(per, 0, c);
  check(c.boxMargin == 6, "PerNet mode: first search starts at init (6)");
  growBoxMarginIfStillOverflowed(per, true, c);
  assignBoxMarginForSearch(per, 1, c);
  check(c.boxMargin == 12, "still overflowed after round 0 -> 12 for round 1, and assign keeps it");
  growBoxMarginIfStillOverflowed(per, false, c);
  assignBoxMarginForSearch(per, 2, c);
  check(c.boxMargin == 12, "resolved after round 1 -> no growth; stays 12");
}

// This file's own mechanism: bad input is rejected, never silently
// clamped (an unintended margin quietly changes quality and runtime).
void testParseBoxGrowthRejectsBadInput() {
  BoxGrowth b;
  std::string err;
  check(parseBoxGrowth(nullptr, nullptr, nullptr, nullptr, b, err) && b.mode == BoxMode::Fixed,
        "no env vars -> Fixed mode (nv4route parity by default)");
  check(parseBoxGrowth("pernet", "6", "6", "96", b, err) && b.mode == BoxMode::PerNet &&
            b.init == 6 && b.grow == 6 && b.cap == 96,
        "valid pernet settings parse exactly");
  check(parseBoxGrowth("round", "24", "1", "120", b, err) && b.mode == BoxMode::Round,
        "valid round settings parse");
  check(!parseBoxGrowth("wide", nullptr, nullptr, nullptr, b, err), "unknown mode rejected");
  check(!parseBoxGrowth("pernet", "0", "6", "96", b, err), "init 0 rejected");
  check(!parseBoxGrowth("pernet", "-3", "6", "96", b, err), "negative init rejected");
  check(!parseBoxGrowth("pernet", "6", "-1", "96", b, err), "negative grow rejected");
  check(!parseBoxGrowth("pernet", "24", "6", "12", b, err), "cap below init rejected");
  check(!parseBoxGrowth("pernet", "12abc", "6", "96", b, err), "trailing garbage rejected");
  check(!parseBoxGrowth("pernet", "", "6", "96", b, err), "empty string rejected");
  check(!parseBoxGrowth("pernet", "6", "6", "99999999999", b, err), "out-of-int-range rejected");
}

// ---------------------------------------------------------------------------
// This file's own mechanism: present-demand pricing (NTHU-Route 2.0's cost).
// ---------------------------------------------------------------------------

// The factor schedule, against values computed independently (Python,
// 1 - exp(-5 exp(-0.1 i))), not through pdFactorForIter.
void testPdFactorScheduleMatchesNthu() {
  check(std::abs(pdFactorForIter(1) - 0.9891564743369182) < 1e-12, "factor(1) = 0.98916 (NTHU iteration 1)");
  check(std::abs(pdFactorForIter(20) - 0.49169642239602784) < 1e-12, "factor(20) = 0.49170");
  check(std::abs(pdFactorForIter(50) - 0.03312855552217531) < 1e-12, "factor(50) = 0.03313");
  check(std::abs(pdFactorForIter(80) - 0.0016759072359852079) < 1e-12, "factor(80) = 0.00168");
  bool decreasing = true;
  for (int i = 1; i < 300; ++i)
    decreasing = decreasing && pdFactorForIter(i + 1) < pdFactorForIter(i) && pdFactorForIter(i + 1) > 0.0;
  check(decreasing, "factor strictly decreases and stays > 0 over iterations 1..300");
}

// NTHU's formula, hand-computed. (a) fresh edge: demand 4 + one net of
// amount 2 over cap 10 -> cong 0.6, cost = wl + 1 * 0.6^5 = wl + 0.07776.
// (b) hist 3 at iter 4, factor 0.5: shrink = 1 - 2 / (4 * 3) = 5/6, so
// cong = 6 / (10 * 5/6) = 0.72 and cost = 0.5 + 3 * 0.72^5 = 1.0804752896.
void testPresentDemandCostHandComputed() {
  double f1 = pdFactorForIter(1);
  check(std::abs(presentDemandCost(4, 10, 2, 1, 1, f1, f1) - (f1 + 0.07776)) < 1e-12,
        "fresh edge: cost = WL + (demand + amount) / cap ^ 5 = factor + 0.07776");
  check(std::abs(pdShrink(3, 4, 0.5) - 5.0 / 6.0) < 1e-12, "history 3 at iteration 4 shrinks capacity to 5/6");
  check(std::abs(presentDemandCost(4, 10, 2, 3, 4, 0.5, 0.5) - 1.0804752895999998) < 1e-12,
        "history multiplies AND shrinks: 0.5 + 3 * 0.72^5 = 1.08048");
  check(presentDemandCost(20, 10, 2, 1, 1, f1, f1) - f1 > 600.0 * (presentDemandCost(4, 10, 2, 1, 1, f1, f1) - f1),
        "the congestion term at 2.2x capacity is over 600x a 60%-full edge's (2.2^5 / 0.6^5 = 663: the ^5 steepness)");
}

// CLAUDE.md's arithmetic edge cases for this formula: zero and negative
// capacity (a .gr adjustment can zero an edge) must give a finite
// deterrent, never inf/NaN from the division.
void testPdCostZeroCapacityIsForbiddenAndFinite() {
  double f = pdFactorForIter(1);
  check(presentDemandCost(0, 0, 2, 1, 1, f, f) == kForbiddenPrice, "capacity 0 -> kForbiddenPrice");
  check(presentDemandCost(5, -3, 2, 1, 1, f, f) == kForbiddenPrice, "negative capacity -> kForbiddenPrice");
  double huge = presentDemandCost(1000000000LL, 1, 2, 1000000, 1, f, f);
  check(std::isfinite(huge) && huge == kForbiddenPrice,
        "absurd demand and history clamp to kForbiddenPrice, still finite");
}

// The shrink invariant: an edge overflowed at every round boundary gains
// history exactly as fast as the iteration counter, never faster, so the
// shrink stays strictly above the 1/3 floor -- the floor never binds in a
// real run. And the floor itself: if the invariant were ever broken (history
// far above the iteration), the shrink clamps at 1/3 instead of reaching 0
// or going negative.
void testPdHistoryInvariantKeepsShrinkAboveFloor() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t e = g.hIndex(1, 0, 0);
  u.h[e] = u.capH[e] + 5; // overflowed at every boundary
  bool invariant = true, aboveFloor = true;
  for (int r = 0; r < 500; ++r) {
    advancePresentDemand(pr, u);
    invariant = invariant && pr.pdHistH[e] <= pr.pdIter && pr.pdHistH[e] == pr.pdIter;
    aboveFloor = aboveFloor && pdShrink(pr.pdHistH[e], pr.pdIter, pr.pdFactor) > kPdMinShrink;
  }
  check(invariant, "a permanently overflowed edge's history tracks the iteration exactly (hist <= iter)");
  check(aboveFloor, "so its shrink stays strictly above the 1/3 floor for 500 iterations");
  size_t clean = g.hIndex(2, 0, 0);
  check(pr.pdHistH[clean] == 1, "a never-overflowed edge keeps history 1");
  check(pdShrink(1000, 1, 1.0) == kPdMinShrink, "a broken invariant clamps at 1/3, not 0 or negative");
  check(std::isfinite(presentDemandCost(10, 10, 2, 1000, 1, 1.0, 1.0)), "and the cost stays finite");
}

// The mechanism's point: under present-demand pricing, taking a net off an
// edge makes the edge exactly as cheap as it was before the net arrived.
// Under the inherited lambda model the same charge/undo leaves the edge
// permanently dearer (negotiateWire only ever raises lambda).
void testPdCostFallsTheMomentDemandLeaves() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  std::vector<PathNode> path = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
  size_t e = g.hIndex(1, 0, 0);
  auto chargeTwiceThenUndo = [&](CostModel model, double &before, double &during, double &after) {
    Prices pr(g);
    Usage u(g);
    CostSettings cs;
    cs.model = model;
    configureCostModel(pr, u, cs);
    before = wireCostH(pr, e, 1);
    std::vector<Seg> segs;
    std::vector<ChargeRecord> charges;
    for (int k = 0; k < 3; ++k) // 3 nets on a capacity-2 edge: overflowed
      commitPath(g, pr, u, 0.5, 1, path, segs, charges);
    during = wireCostH(pr, e, 1);
    for (const ChargeRecord &c : charges)
      undoCharge(g, u, c);
    after = wireCostH(pr, e, 1);
  };
  double pb, pd, pa, lb, ld, la;
  chargeTwiceThenUndo(CostModel::kPresentDemand, pb, pd, pa);
  chargeTwiceThenUndo(CostModel::kLambda, lb, ld, la);
  check(pd > 10.0 * pb, "present demand: an overflowed edge is far dearer while the nets sit on it");
  check(pa == pb, "present demand: once they leave, the cost is exactly what it was before");
  check(ld > lb, "lambda (inherited): the charge raises the price too");
  check(la > lb, "lambda (inherited): but it stays raised after the nets leave -- the difference this file tests");
}

// Under kLambda (route()'s default, PD1ROUTE_COST=lambda) the cost seam and
// heuristic scale are the inherited ones exactly -- the in-code half of the
// byte-parity check against nv11route.
void testLambdaModeIsTheInheritedCostExactly() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  pr.h[g.hIndex(1, 0, 0)] = 3.7;
  pr.histH[g.hIndex(1, 0, 0)] = 1.9;
  pr.v[g.vIndex(0, 1, 0)] = 2.2;
  bool same = true;
  for (size_t e = 0; e < pr.h.size(); ++e)
    same = same && wireCostH(pr, e, 2) == edgeCost(pr.h[e], pr.histH[e], 2);
  for (size_t e = 0; e < pr.v.size(); ++e)
    same = same && wireCostV(pr, e, 2) == edgeCost(pr.v[e], pr.histV[e], 2);
  check(same, "kLambda: every edge's cost is edgeCost(lambda, hist, amount), bit for bit");
  check(heuristicScale(pr) == 1.0, "kLambda: A* heuristic scale is exactly 1 (the inherited heuristic)");
}

// A* stays exact under present-demand pricing. Late in NTHU's schedule the
// WL term is ~0.0017, so an edge can cost far below 1 and the inherited
// Manhattan heuristic (scale 1) overestimates. heuristicScale() multiplies
// it by the WL term, a true lower bound. On 30 hash-priced fixtures (random
// demand, many edges overflowed, random history <= iteration), the path
// multiSourceDijkstra returns must cost exactly what an in-test reference
// Dijkstra (no heuristic) finds. The fixtures mimic that late state: ~90%
// of edges well under capacity (cost well below 1), ~10% hot spots over
// it with history. Then the teeth: with the scale forced back
// to 1, at least one fixture must come back strictly costlier -- proving
// the scaling is needed, not decorative.
void testAStarStaysExactUnderPdPricing() {
  std::string gr = "grid 30 30 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd1route_selftest_astar.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  auto hsh = [](unsigned x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
  };
  auto pathCost = [&](const Prices &pr, const std::vector<PathNode> &p) {
    double c = 0;
    for (size_t i = 1; i < p.size(); ++i) {
      if (p[i].y == p[i - 1].y)
        c += wireCostH(pr, g.hIndex(std::min(p[i].x, p[i - 1].x), p[i].y, 0), 2);
      else
        c += wireCostV(pr, g.vIndex(p[i].x, std::min(p[i].y, p[i - 1].y), 0), 2);
    }
    return c;
  };
  auto reference = [&](const Prices &pr, const std::vector<PathNode> &src, const std::vector<Point> &snk) {
    int n = g.nx * g.ny;
    std::vector<double> dist(n, std::numeric_limits<double>::infinity());
    using QE = std::pair<double, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> q;
    for (const PathNode &s : src) {
      dist[s.y * g.nx + s.x] = 0;
      q.push({0.0, s.y * g.nx + s.x});
    }
    while (!q.empty()) {
      double dd = q.top().first;
      int i = q.top().second;
      q.pop();
      if (dd > dist[i])
        continue;
      int x = i % g.nx, y = i / g.nx;
      auto relax = [&](int nx2, int ny2, double w) {
        int j = ny2 * g.nx + nx2;
        if (dd + w < dist[j]) {
          dist[j] = dd + w;
          q.push({dist[j], j});
        }
      };
      if (x > 0) relax(x - 1, y, wireCostH(pr, g.hIndex(x - 1, y, 0), 2));
      if (x + 1 < g.nx) relax(x + 1, y, wireCostH(pr, g.hIndex(x, y, 0), 2));
      if (y > 0) relax(x, y - 1, wireCostV(pr, g.vIndex(x, y - 1, 0), 2));
      if (y + 1 < g.ny) relax(x, y + 1, wireCostV(pr, g.vIndex(x, y, 0), 2));
    }
    double best = std::numeric_limits<double>::infinity();
    for (const Point &s : snk)
      best = std::min(best, dist[s.y * g.nx + s.x]);
    return best;
  };

  int exact = 0, unscaledWorse = 0, overflowedEdges = 0;
  const int kFixtures = 30;
  for (int f = 0; f < kFixtures; ++f) {
    Prices pr(g);
    Usage u(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    pr.pdIter = 80;
    pr.pdFactor = pdFactorForIter(80);
    auto fill = [&](unsigned salt, int &demand, int &hist) {
      unsigned a = hsh(f * 7919 + salt), b = hsh(f * 104729 + salt);
      bool hot = a % 10 == 0;
      demand = hot ? 25 + static_cast<int>((a >> 8) % 16) : static_cast<int>((a >> 8) % 15);
      hist = hot ? 1 + static_cast<int>(b % 80) : 1;
    };
    for (size_t e = 0; e < u.h.size(); ++e) {
      fill(static_cast<unsigned>(e * 2 + 1), u.h[e], pr.pdHistH[e]);
      overflowedEdges += u.h[e] > u.capH[e];
    }
    for (size_t e = 0; e < u.v.size(); ++e)
      fill(static_cast<unsigned>(e * 2), u.v[e], pr.pdHistV[e]);
    unsigned r = hsh(f + 12345);
    std::vector<PathNode> src = {{static_cast<int>(r % 30), static_cast<int>((r >> 8) % 30), -1}};
    std::vector<Point> snk = {{static_cast<int>((r >> 16) % 30), static_cast<int>((r >> 24) % 30)},
                              {static_cast<int>(hsh(r) % 30), static_cast<int>((hsh(r) >> 8) % 30)}};
    if (snk[0].x == src[0].x && snk[0].y == src[0].y)
      snk[0].x = (snk[0].x + 7) % 30;
    if (snk[1].x == src[0].x && snk[1].y == src[0].y)
      snk[1].y = (snk[1].y + 11) % 30;
    double ref = reference(pr, src, snk);
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, /*margin=*/60);
    if (a.found && std::abs(pathCost(pr, a.path) - ref) <= 1e-9 * std::max(1.0, ref))
      ++exact;
    pr.heuristicScaleOverride = 1.0;
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, /*margin=*/60);
    if (b.found && pathCost(pr, b.path) > ref * (1.0 + 1e-9) + 1e-12)
      ++unscaledWorse;
  }
  check(overflowedEdges > 1000, "fixtures are genuinely congested (over 1000 overflowed edges across them)");
  check(exact == kFixtures, "scaled heuristic: A* finds the true minimum cost on all 30 fixtures (" +
                                std::to_string(exact) + "/30)");
  check(unscaledWorse >= 1, "unscaled heuristic returns a strictly costlier path on " +
                                std::to_string(unscaledWorse) + " fixture(s) -- the scaling is load-bearing");
}

// End to end: three nets between the same two tiles of one row, which
// holds one net per edge. Only detours through the rows above and below
// fit all three; the fast tier's patterns for a same-row pair can't make
// them. route() under present-demand pricing must reach zero 2D overflow.
void testPdRouteReachesZeroOnADetourOnlyFixture() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd1route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0,
                        true, true, true, 8, BoxGrowth{}, cs);
  check(rep.bestOverflow == 0, "present-demand pricing reaches zero 2D overflow (got " +
                                   std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "and every net's route is connected");
}

void testParseCostSettings() {
  CostSettings c;
  std::string err;
  check(parseCostSettings(nullptr, nullptr, c, err) && c.model == CostModel::kPresentDemand && c.wlFloor == 0.0,
        "no env vars -> present-demand pricing, NTHU's WL term (floor 0)");
  check(parseCostSettings("lambda", nullptr, c, err) && c.model == CostModel::kLambda, "lambda parses");
  check(parseCostSettings("pd", "1", c, err) && c.model == CostModel::kPresentDemand && c.wlFloor == 1.0,
        "pd with floor 1 parses exactly");
  check(parseCostSettings("pd", "0.25", c, err) && c.wlFloor == 0.25, "fractional floor parses");
  check(!parseCostSettings("nthu", nullptr, c, err), "unknown model rejected");
  check(!parseCostSettings("pd", "-1", c, err), "negative floor rejected");
  check(!parseCostSettings("pd", "abc", c, err), "non-numeric floor rejected");
  check(!parseCostSettings("pd", "1x", c, err), "trailing garbage rejected");
  check(!parseCostSettings("pd", "", c, err), "empty floor rejected");
  check(!parseCostSettings("pd", "nan", c, err), "NaN floor rejected");
  check(!parseCostSettings("pd", "inf", c, err), "infinite floor rejected");
}

// ---------------------------------------------------------------------------
// pd2route's own mechanisms: cong^5 by multiplication, and the acceptance rule.
// ---------------------------------------------------------------------------

// The multiplication must agree with the std::pow formula pd1route used,
// across the whole range a run sees: empty to 3x-overflowed edges, history
// 1..iter, early and late iterations, with and without the capacity shrink.
// pd39route: detmath within a few units in the last place of the platform's
// libm, and exact on exact cases. (The label carries no measured numbers, so
// the self-test's output is the same text on every platform.)
void testDetMathAccuracy() {
  double worstE = 0, worstL = 0, worstP = 0;
  for (int i = -7000; i <= 7000; ++i) {
    double x = i * 0.1013;
    double a = detmath::exp(x), b = std::exp(x);
    if (b > 0 && std::isfinite(b))
      worstE = std::max(worstE, std::fabs(a - b) / b);
    double y = std::exp(i * 0.0101);
    double l = detmath::log(y), lr = std::log(y);
    if (lr != 0)
      worstL = std::max(worstL, std::fabs(l - lr) / std::fabs(lr));
    double q = detmath::pow(1.0 + (i + 7000) * 1e-4, 2.5), qr = std::pow(1.0 + (i + 7000) * 1e-4, 2.5);
    worstP = std::max(worstP, std::fabs(q - qr) / qr);
  }
  bool ok = worstE < 5e-16 && worstL < 5e-16 && worstP < 1e-14;
  if (!ok)
    std::cout << "  detmath worst relative error: exp " << worstE << ", log " << worstL << ", pow " << worstP << "\n";
  check(ok, "detmath: exp and log within 5e-16, pow within 1e-14, of the platform libm (relative)");
  check(detmath::exp(0.0) == 1.0 && detmath::pow(3.0, 5.0) == 243.0 && detmath::pow(2.0, -2.0) == 0.25 &&
            detmath::exp(-800.0) == 0.0 && std::isinf(detmath::exp(800.0)) && detmath::log(1.0) == 0.0 &&
            std::isinf(detmath::log(0.0)) && std::isnan(detmath::log(-1.0)) && std::isnan(detmath::exp(std::nan(""))),
        "detmath: exact values, overflow, underflow, log of 0 and of a negative, NaN");
}

// pd39route: a*b + c with a = 1 + 2^-30, b = 1 - 2^-30, c = -1. Rounded
// twice (no contraction), a*b = 1 - 2^-60 rounds to 1 and the sum is 0;
// fused into one instruction it is -2^-60. Read through volatiles so the
// compiler cannot fold it at build time.
double contractionProbe(double a, double b, double c) { return a * b + c; }
void testBuiltWithoutContraction() {
  volatile double va = 1.0 + std::ldexp(1.0, -30), vb = 1.0 - std::ldexp(1.0, -30), vc = -1.0;
  check(contractionProbe(va, vb, vc) == 0.0,
        "built without floating-point contraction (a*b + c rounds twice, as on every platform)");
}

// pd39route (Part 123; option A's margin check): the path where the slow
// tier's best state is the one kept -- its round restored from the saved
// changes, its nets escalated -- on a fixture where the slow tier beats the
// pattern stage by a clear margin. With pd39route's arithmetic the numbers
// are the same on every platform.
void testSlowTierBestStateIsKept() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 250\n";
  unsigned st = 777;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  for (int i = 0; i < 250; ++i) {
    int np = 2 + rnd(3), cx = 0 + rnd(40), cy = 0 + rnd(40);
    gr += "n" + std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(np) + " 1\n";
    for (int k = 0; k < np; ++k) {
      int x = std::min(39, std::max(0, cx + rnd(15) - 7)), y = std::min(39, std::max(0, cy + rnd(15) - 7));
      gr += std::to_string(x * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    }
  }
  gr += "0\n";
  auto run = [&](int patternRounds, int slowRounds, std::vector<SlowRoundRecord> &h) {
    Design d = parse("pd39route_selftest_kept.gr", gr.c_str());
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    PatternStageSettings ps;
    ps.rounds = patternRounds;
    BoxGrowth bx;
    bx.mode = BoxMode::Round;
    bx.init = 4;
    bx.grow = 2;
    bx.cap = 20;
    gSlowHistory = &h;
    RunReport rep = route(d, 60, slowRounds, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true,
                          true, true, 8, bx, cs, AcceptMode::kKeep, true, -1, &ps);
    gSlowHistory = nullptr;
    return rep;
  };
  std::vector<SlowRoundRecord> none, h;
  RunReport pat = run(1, 0, none); // one pattern round: a clearly beatable start
  RunReport rep = run(1, 40, h);
  size_t bestRound = 0;
  for (size_t r = 0; r < h.size(); ++r)
    if (h[r].overflow < h[bestRound].overflow)
      bestRound = r; // the first round reaching the minimum is the one kept
  bool ranAll = h.size() == 40 && none.empty();
  check(ranAll && rep.bestOverflow == h[bestRound].overflow &&
            rep.bestOverflow * 100 <= pat.bestOverflow * 95,
        "slow tier kept: its best round (" + std::to_string(rep.bestOverflow) + ") is the state returned, >= 5% below "
        "the pattern stage's best (" + std::to_string(pat.bestOverflow) + ")");
  check(ranAll && routeHashOf(rep.nets) == h[bestRound].routeHash,
        "slow tier kept: the returned routes are exactly that round's (restored from the saved changes)");
  check(rep.escalatedCount > 0 && pat.escalatedCount == 0,
        "slow tier kept: its escalated nets carry the flag (" + std::to_string(rep.escalatedCount) +
            "), the pattern stage's alone carry none");
}

void testPdCongPowerByMultiplicationMatchesPow() {
  double worst = 0.0;
  int cases = 0;
  for (int cap : {2, 7, 20, 90, 450})
    for (int demand = 0; demand <= 3 * cap; demand += std::max(1, cap / 7))
      for (int iter : {1, 5, 31, 80, 150})
        for (int hist : {1, 2, iter / 2 + 1, iter}) {
          double f = pdFactorForIter(iter);
          double cong = static_cast<double>(demand + 2) / (static_cast<double>(cap) * pdShrink(hist, iter, f));
          double ref = std::min(kForbiddenPrice, f + hist * std::pow(cong, 5.0));
          double got = presentDemandCost(demand, cap, 2, hist, iter, f, f);
          worst = std::max(worst, std::abs(got - ref) / std::max(1.0, std::abs(ref)));
          ++cases;
        }
  check(cases > 1000, "power check covers " + std::to_string(cases) + " cost evaluations");
  check(worst <= 1e-13, "cong^5 by multiplication matches std::pow(cong, 5) to 1e-13 relative everywhere");
}

// The whole acceptance rule, both modes, every ordering of before/after.
void testRevertRerouteDecision() {
  check(revertReroute(AcceptMode::kRevert, 5, 6), "revert: a locally worse reroute is undone");
  check(!revertReroute(AcceptMode::kRevert, 5, 5), "revert: an equal one is kept (inherited rule: undo only if worse)");
  check(!revertReroute(AcceptMode::kRevert, 5, 4), "revert: a better one is kept");
  check(!revertReroute(AcceptMode::kKeep, 5, 6), "keep: a locally worse reroute is kept (NTHU's rule)");
  check(!revertReroute(AcceptMode::kKeep, 5, 5) && !revertReroute(AcceptMode::kKeep, 5, 4),
        "keep: equal and better reroutes are kept too");
}

// End to end under keep: the detour-only fixture (three nets, one row that
// holds one) must still reach zero with no reroute ever undone.
void testKeepModeReachesZeroOnTheDetourOnlyFixture() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd2route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0,
                        true, true, true, 8, BoxGrowth{}, cs, AcceptMode::kKeep);
  check(rep.bestOverflow == 0, "keep + present demand reaches zero 2D overflow (got " +
                                   std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "and every net's route is connected");
}

void testParseAcceptMode() {
  AcceptMode m = AcceptMode::kKeep;
  std::string err;
  check(parseAcceptMode(nullptr, m, err) && m == AcceptMode::kRevert, "unset -> revert (inherited rule)");
  check(parseAcceptMode("keep", m, err) && m == AcceptMode::kKeep, "keep parses");
  check(parseAcceptMode("revert", m, err) && m == AcceptMode::kRevert, "revert parses");
  check(!parseAcceptMode("always", m, err), "unknown value rejected");
  check(!parseAcceptMode("", m, err), "empty value rejected");
}

// ---------------------------------------------------------------------------
// pd3route's own mechanism: NTHU-style layer assignment.
// ---------------------------------------------------------------------------

// An L-shaped two-leg tree (3 tiles east, then 2 tiles north) splits into 5
// unit edges over 6 nodes, still a tree, covering exactly the same tiles.
void testSplitToUnitEdgesKeepsTheTreeAndItsTiles() {
  std::vector<Seg> segs = {{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}};
  NetTree legs, unit;
  check(buildNetTree(segs, legs) && legs.edges.size() == 2, "two-leg tree builds (sanity)");
  splitToUnitEdges(legs, unit);
  bool allUnit = true;
  for (const NetTree::Edge &e : unit.edges)
    allUnit = allUnit && e.hi - e.lo == 1 &&
              std::abs(unit.nodes[e.a].x - unit.nodes[e.b].x) + std::abs(unit.nodes[e.a].y - unit.nodes[e.b].y) == 1;
  check(unit.edges.size() == 5 && unit.nodes.size() == 6, "5 unit edges over 6 nodes");
  check(allUnit, "every edge is one tile long and its lo/hi match its endpoints");
  std::set<std::tuple<bool, int, int>> before, after;
  for (const NetTree::Edge &e : legs.edges)
    for (int c = e.lo; c < e.hi; ++c)
      before.insert({e.horizontal, e.cross, c});
  for (const NetTree::Edge &e : unit.edges)
    after.insert({e.horizontal, e.cross, e.lo});
  check(before == after, "the unit edges cover exactly the legs' tiles");
  bool degreesOk = unit.adj.size() == 6;
  int leaves = 0;
  for (const auto &a : unit.adj)
    leaves += a.size() == 1;
  check(degreesOk && leaves == 2, "a path: exactly two leaves");
}

// The decisive layer-assignment fixture. Two horizontal layers, one net per
// layer per tile edge. Other nets already fill layer 0 on tile edges 0-1 and
// layer 1 on tile edges 2-3, and every via site at the switch point is far
// over this project's via-site capacity. A net running tile 0 -> 4:
//  - inherited DP (whole leg on one layer, soft averaged cost) must overflow;
//  - nthu DP on unit edges puts edges 0-1 on layer 1 and 2-3 on layer 0
//    with one via stack -- zero overflow, the via-site "capacity" ignored
//    because ISPD scoring gives vias none.
void testNthuLayerAssignmentSwitchesMidLegToAvoidOverflow() {
  const char *gr = "grid 6 2 3\nvertical capacity 0 0 2\nhorizontal capacity 2 2 0\n"
                   "minimum width 1 1 1\nminimum spacing 1 1 1\nvia spacing 1 1 1\n0 0 10 10\n"
                   "num net 1\nnX 0 2 1\n5 5 1\n45 5 1\n0\n";
  Design d = parse("pd3route_selftest_switch.gr", gr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  auto run = [&](bool nthu, long long &overflow, std::vector<int> &edgeLayer, NetTree &t) {
    RealUsage ru(g);
    RealViaUsage rvu(g);
    ru.h[g.hIndex(0, 0, 0)] = ru.h[g.hIndex(1, 0, 0)] = 2;
    ru.h[g.hIndex(2, 0, 1)] = ru.h[g.hIndex(3, 0, 1)] = 2;
    for (int l = 0; l + 1 < g.nLayers; ++l)
      rvu.usage[viaIndex(g, 2, 0, l)] = 1000;
    NetTree legs;
    buildNetTree({{0, 0, 0, 4, 0, 0}}, legs);
    if (nthu)
      splitToUnitEdges(legs, t);
    else
      t = legs;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> ranges;
    bool ok = layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, ranges, 1.0, nullptr,
                            nullptr, nullptr, nthu);
    RoutedNet rn;
    rn.net = &d.nets[0];
    commitNetTree(g, ru, rvu, 1, t, edgeLayer, ranges, rn);
    overflow = 0;
    for (int c = 0; c < 4; ++c)
      for (int l = 0; l < 2; ++l)
        overflow += std::max<long long>(0, ru.h[g.hIndex(c, 0, l)] - g.hcap[g.hIndex(c, 0, l)]);
    return ok;
  };
  long long ovInh, ovNthu;
  std::vector<int> layInh, layNthu;
  NetTree tInh, tNthu;
  check(run(false, ovInh, layInh, tInh), "inherited DP finds an assignment (sanity)");
  check(run(true, ovNthu, layNthu, tNthu), "nthu DP finds an assignment");
  check(ovInh > 0, "inherited: one layer for the whole leg must overflow (" + std::to_string(ovInh) + ")");
  check(ovNthu == 0, "nthu: switching layers mid-leg reaches zero overflow");
  bool layersRight = tNthu.edges.size() == 4;
  for (size_t i = 0; i < tNthu.edges.size() && layersRight; ++i)
    layersRight = layNthu[i] == (tNthu.edges[i].lo < 2 ? 1 : 0);
  check(layersRight, "nthu: tile edges 0-1 on layer 1, 2-3 on layer 0 -- despite the saturated via sites");
}

// NTHU's order, hand-checked: C (2 pins / 2 edges = 1.0) and B (3 / 3 =
// 1.0) tie, fewer pins first -> C before B; then A (2 / 10 = 0.2); a net
// with no edges (D) goes first.
void testLaNetOrderMatchesNthu() {
  std::vector<size_t> pins = {2, 3, 2, 2};
  std::vector<long long> edges = {10, 3, 2, 0};
  std::vector<size_t> order = laNetOrder(pins, edges);
  check(order == std::vector<size_t>({3, 2, 1, 0}),
        "order is D (no edges), C (1.0, 2 pins), B (1.0, 3 pins), A (0.2)");
}

// The route-file projection: real coordinates -> tiles, vias dropped, a
// stretch present on two layers kept once, everything on layer 0.
void testReadRouteAs2DProjectsWiresAndDropsVias() {
  Design d = parse("pd3route_selftest_basic.gr", kBasicGr);
  std::string path = "pd3route_selftest_proj.route";
  {
    std::ofstream out(path);
    out << "netA 0\n(5,5,1)-(35,5,1)\n(35,5,1)-(35,5,2)\n(35,5,2)-(35,35,2)\n"
           "(35,35,2)-(35,35,1)\n(35,5,1)-(35,35,1)\n!\n";
  }
  std::vector<RoutedNet> nets = readRouteAs2D(d, path);
  check(nets.size() == 1 && nets[0].net == &d.nets[0], "one net, matched to netA by id");
  bool expected = nets.size() == 1 && nets[0].segs.size() == 2 &&
                  nets[0].segs[0].x1 == 0 && nets[0].segs[0].y1 == 0 && nets[0].segs[0].x2 == 3 &&
                  nets[0].segs[0].y2 == 0 && nets[0].segs[1].x1 == 3 && nets[0].segs[1].y1 == 0 &&
                  nets[0].segs[1].x2 == 3 && nets[0].segs[1].y2 == 3;
  check(expected, "wires (0,0)-(3,0) and (3,0)-(3,3) in tiles; both vias dropped; the stretch on two layers kept once");
  bool layer0 = true;
  for (const Seg &sg : nets[0].segs)
    layer0 = layer0 && sg.l1 == 0 && sg.l2 == 0;
  check(layer0, "every projected wire is on the 2D stage's synthetic layer 0");
}

// Part 213: an X crossing (the vertical segment crosses the horizontal one inside both) is split into four legs and
// the net then builds as a tree; a T junction and a net with no crossing are left exactly as they were.
void testSplitInteriorCrossings() {
  std::vector<Seg> x = {{0, 2, 0, 4, 2, 0}, {2, 0, 0, 2, 4, 0}};
  std::vector<Point> pins = {{0, 2}, {4, 2}, {2, 0}, {2, 4}};
  NetTree t0;
  const bool before = buildNetTree(x, t0, true, &pins);
  const size_t c = splitInteriorCrossings(x);
  NetTree t1;
  const bool after = buildNetTree(x, t1, true, &pins);
  check(!before && c == 1 && x.size() == 4 && after && t1.edges.size() == 4,
        "X crossing: the tree build fails before, the split gives 4 legs and a tree after");
  std::vector<Seg> tj = {{0, 2, 0, 4, 2, 0}, {2, 2, 0, 2, 4, 0}};
  std::vector<Seg> tj0 = tj;
  const size_t ct = splitInteriorCrossings(tj);
  bool same = ct == 0 && tj.size() == tj0.size();
  for (size_t i = 0; same && i < tj.size(); ++i)
    same = tj[i].x1 == tj0[i].x1 && tj[i].y1 == tj0[i].y1 && tj[i].x2 == tj0[i].x2 && tj[i].y2 == tj0[i].y2;
  check(same, "T junction (an endpoint on the other segment): unchanged");
  // NTHU-Route's newblue3 net n502836 (tiles): the vertical at x 598 crosses the rows 670 and 671 inside both
  std::vector<Seg> n = {{596, 669, 0, 596, 671, 0}, {598, 669, 0, 598, 672, 0}, {595, 669, 0, 596, 669, 0},
                        {597, 670, 0, 599, 670, 0}, {596, 671, 0, 599, 671, 0}, {597, 672, 0, 599, 672, 0}};
  std::vector<Point> np = {{596, 669}, {597, 670}, {597, 671}, {599, 670}, {595, 669}, {596, 670}};
  NetTree tn;
  const size_t cn = splitInteriorCrossings(n);
  check(cn == 2 && buildNetTree(n, tn, true, &np), "newblue3's n502836 (NTHU-Route): two crossings split, a tree after");
}

// End to end, nthu mode: route() -> nthu layer assignment on the
// multi-net, multi-layer fixture. Every net is connected in 3D, real
// overflow is zero when 2D overflow is, and projecting the written solution
// back gives exactly the 2D stage's tile edges -- the round trip
// PD3ROUTE_LA_FROM relies on.
void testNthuLayerAssignmentEndToEndAndRoundTrip() {
  Design d = parse("pd3route_selftest_e2e.gr", kMultiNetMultiLayerGr);
  RunReport rep = route(d, 30, 5, 0.02, 1.0, HistMode::kAdditive, 1.0);
  auto tileEdges = [](const std::vector<Seg> &segs) {
    std::set<std::tuple<bool, int, int>> out;
    for (const Seg &sg : segs) {
      if (sg.l1 != sg.l2)
        continue;
      if (sg.y1 == sg.y2)
        for (int x = std::min(sg.x1, sg.x2); x < std::max(sg.x1, sg.x2); ++x)
          out.insert({true, sg.y1, x});
      else
        for (int y = std::min(sg.y1, sg.y2); y < std::max(sg.y1, sg.y2); ++y)
          out.insert({false, sg.x1, y});
    }
    return out;
  };
  std::vector<std::set<std::tuple<bool, int, int>>> twoD;
  for (const RoutedNet &rn : rep.nets)
    twoD.push_back(tileEdges(rn.segs));
  ViaNegotiationReport vr = negotiateViaAssignment(d.grid, rep.nets, 60, 20, 1.0, HistKnobs(), HistKnobs(),
                                                   LaMode::kNthu);
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "nthu: every net is one connected component in 3D");
  check(rep.bestOverflow == 0 && vr.bestOverflow == 0, "nthu: zero 2D overflow gives zero real overflow here");
  std::string path = "pd3route_selftest_rt.route";
  writeSolution(path, d.grid, rep.nets);
  std::vector<RoutedNet> back = readRouteAs2D(d, path);
  bool same = back.size() == twoD.size();
  for (size_t i = 0; i < back.size() && same; ++i)
    same = tileEdges(back[i].segs) == twoD[i];
  check(same, "written -> projected solution has exactly the 2D stage's tile edges, net by net");
}

// A net whose wires form a cycle -- a square ring, with a pin partway along
// its bottom side. The inherited reconstruction rejects it (and the net
// falls back to greedy per-segment layers); dropCycles keeps a spanning
// tree of the same nodes, with the pin as a node of degree >= 1, so no
// dropped stretch can have cut it off.
void testDropCyclesKeepsARingNetAndItsPin() {
  std::vector<Seg> ring = {{0, 0, 0, 4, 0, 0}, {4, 0, 0, 4, 4, 0}, {4, 4, 0, 0, 4, 0}, {0, 4, 0, 0, 0, 0}};
  std::vector<Point> pins = {{2, 0}, {4, 4}};
  NetTree t;
  check(!buildNetTree(ring, t), "inherited reconstruction rejects a ring (sanity)");
  check(buildNetTree(ring, t, true, &pins), "dropCycles reconstruction accepts it");
  check(t.nodes.size() == 5 && t.edges.size() == 4, "5 nodes (4 corners + the pin), 4 edges: a spanning tree");
  int pinNode = -1;
  for (size_t i = 0; i < t.nodes.size(); ++i)
    if (t.nodes[i].x == 2 && t.nodes[i].y == 0)
      pinNode = static_cast<int>(i);
  check(pinNode >= 0 && !t.adj[pinNode].empty(), "the mid-side pin is a node, still attached");
  NetTree loose;
  check(buildNetTree({{0, 0, 0, 4, 0, 0}, {0, 2, 0, 4, 2, 0}}, loose, true) == false,
        "two disjoint wires are still rejected -- dropCycles never hides a real disconnection");
}

void testParseLaMode() {
  LaMode m = LaMode::kInherited;
  std::string err;
  check(parseLaMode(nullptr, m, err) && m == LaMode::kNthu, "unset -> nthu (this file's default)");
  check(parseLaMode("inherited", m, err) && m == LaMode::kInherited, "inherited parses");
  check(parseLaMode("nthu", m, err) && m == LaMode::kNthu, "nthu parses");
  check(!parseLaMode("fast", m, err), "unknown value rejected");
  check(!parseLaMode("", m, err), "empty value rejected");
}

// ---------------------------------------------------------------------------
// pd4route's own mechanism: the refinement stage.
// ---------------------------------------------------------------------------

// NTHU's refinement cost: 1 for an edge one more net would overflow, a
// length tie-break otherwise; zero/negative capacity counts as full.
void testRefineCostCountsOnlyFullEdges() {
  check(refineCost(8, 10, 2) == kRefineEps, "demand 8 + 2 = cap 10: fits exactly, not full -> tie-break only");
  check(refineCost(9, 10, 2) == 1.0, "demand 9 + 2 > cap 10: full -> 1");
  check(refineCost(0, 0, 2) == 1.0 && refineCost(0, -4, 2) == 1.0, "zero or negative capacity is full");
  Design d = parse("pd4route_selftest_basic.gr", kBasicGr);
  Prices pr(d.grid);
  pr.model = CostModel::kRefine;
  check(heuristicScale(pr) == kRefineEps, "A* scale under refinement is kRefineEps, a true per-edge lower bound");
}

// A wall of full edges at x=10 on every row but row 0. Under refinement cost
// the search crosses at row 0 -- a 10-tile detour -- because length only
// breaks ties; the inherited lambda search (all prices 1) goes straight
// through the wall.
void testRefineDetoursAroundFullEdgesRegardlessOfLength() {
  std::string gr = "grid 30 10 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n25 55 1\n205 55 1\n0\n";
  Design d = parse("pd4route_selftest_wall.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Usage u(g);
  for (int y = 1; y < g.ny; ++y)
    u.h[g.hIndex(10, y, 0)] = u.capH[g.hIndex(10, y, 0)];
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<PathNode> src = {{2, 5, -1}};
  std::vector<Point> snk = {{20, 5}};
  auto crossing = [&](const MSMVResult &r) {
    for (size_t i = 1; i < r.path.size(); ++i)
      if (r.path[i].y == r.path[i - 1].y && std::min(r.path[i].x, r.path[i - 1].x) == 10)
        return r.path[i].y;
    return -1;
  };
  Prices refine(g);
  refine.model = CostModel::kRefine;
  refine.live = &u;
  MSMVResult r = routeStepMSMV(g, refine, lp, region, src, snk, 1, 10);
  check(r.found && crossing(r) == 0, "refinement crosses the wall at the free row 0, a 10-tile detour");
  Prices lambda(g);
  MSMVResult l = routeStepMSMV(g, lambda, lp, region, src, snk, 1, 10);
  check(l.found && crossing(l) == 5, "the lambda search (no usage awareness at price 1) goes straight through");
}

void testRefineOrderMatchesNthu() {
  std::vector<size_t> order = refineOrder({0, 5, 5, 2}, {9, 3, 7, 1});
  check(order == std::vector<size_t>({2, 1, 3}),
        "overflow-free net 0 skipped; 5-overflow nets by larger bbox first (2 before 1); then net 3");
}

const char *kThreeOnOneRowGr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                               "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                               "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                               "nC 2 2 1\n5 15 1\n75 15 1\n0\n";

// A route file's nets, given pts/charges/giant by prepareFor2DRefine,
// charge exactly the tiles their wires cover.
void testPrepareFor2DRefineChargesExactlyTheWires() {
  Design d = parse("pd4route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(d.grid, nets);
  bool row1 = true, elsewhere = true;
  for (int x = 0; x + 1 < 8; ++x) {
    row1 = row1 && u.h[d.grid.hIndex(x, 1, 0)] == 6;
    elsewhere = elsewhere && u.h[d.grid.hIndex(x, 0, 0)] == 0 && u.h[d.grid.hIndex(x, 2, 0)] == 0;
  }
  check(row1 && elsewhere, "three nets x amount 2 on each row-1 tile edge, nothing elsewhere");
  check(nets[0].pts.size() == 2 && !nets[0].giant, "two unique pin tiles, not giant");
}

// End to end: all three nets start straight along the one-net row (7 tile
// edges at 3x demand, 2D overflow 28). Refinement must spread them onto
// rows 0, 1 and 2 -- zero overflow -- with every net still connected.
void testRefine2DClearsTheThreeOnOneRowFixture() {
  Design d = parse("pd4route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  RefineSettings rs;
  RefineReport rr = refine2D(d, nets, rs, false);
  check(rr.before == 28, "starts at 2D overflow 28 (sanity)");
  check(rr.after == 0, "refinement reaches zero (got " + std::to_string(rr.after) + ")");
  check(totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0, "and the returned nets really are overflow-free");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
}

void testParseRefineSettings() {
  RefineSettings r;
  std::string err;
  check(parseRefineSettings(nullptr, nullptr, nullptr, nullptr, nullptr, r, err) && r.enabled && r.iters == 20 &&
            r.boxInit == 10 && r.boxInc == 15 && r.accept == AcceptMode::kKeep,
        "unset -> NTHU's refinement: on, 20 iterations, box 10 + 15, keep");
  check(parseRefineSettings("0", "5", "4", "0", "revert", r, err) && !r.enabled && r.iters == 5 && r.boxInit == 4 &&
            r.boxInc == 0 && r.accept == AcceptMode::kRevert,
        "explicit values parse exactly");
  check(!parseRefineSettings("yes", nullptr, nullptr, nullptr, nullptr, r, err), "PD4ROUTE_REFINE must be 0 or 1");
  check(!parseRefineSettings(nullptr, "-1", nullptr, nullptr, nullptr, r, err), "negative iterations rejected");
  check(!parseRefineSettings(nullptr, nullptr, "0", nullptr, nullptr, r, err), "box init 0 rejected");
  check(!parseRefineSettings(nullptr, nullptr, nullptr, "x", nullptr, r, err), "non-numeric box inc rejected");
  check(!parseRefineSettings(nullptr, nullptr, nullptr, nullptr, "always", r, err), "unknown accept rejected");
}

// pd5route: the shuffled refinement order is a permutation of NTHU's set,
// reproducible for a seed and iteration, different across iterations and
// seeds; nthu order is unchanged.
void testRefineShuffleIsASeededPermutation() {
  std::vector<long long> ov(40);
  std::vector<int> bb(40);
  for (int i = 0; i < 40; ++i) {
    ov[i] = (i % 5 == 0) ? 0 : 1 + (i * 7) % 11;
    bb[i] = (i * 13) % 17;
  }
  RefineSettings nthu, sh;
  sh.shuffle = true;
  sh.seed = 3;
  std::vector<size_t> base = refineOrder(ov, bb);
  check(refineOrderFor(ov, bb, nthu, 4) == base, "nthu mode: exactly refineOrder, every iteration");
  std::vector<size_t> a = refineOrderFor(ov, bb, sh, 0), b = refineOrderFor(ov, bb, sh, 0),
                      c = refineOrderFor(ov, bb, sh, 1);
  std::vector<size_t> sa = a, sb = base;
  std::sort(sa.begin(), sa.end());
  std::sort(sb.begin(), sb.end());
  check(sa == sb, "shuffle: the same nets (only overflowed ones), permuted");
  check(a == b, "shuffle: reproducible for the same seed and iteration");
  check(a != c && a != base, "shuffle: differs across iterations and from NTHU's order");
  RefineSettings other = sh;
  other.seed = 4;
  check(refineOrderFor(ov, bb, other, 0) != a, "shuffle: differs across seeds");
  RefineSettings r;
  std::string err;
  check(parseRefineOrder("shuffle", "7", r, err) && r.shuffle && r.seed == 7, "shuffle / seed 7 parse");
  check(!parseRefineOrder("random", nullptr, r, err) && !parseRefineOrder(nullptr, "-2", r, err) &&
            !parseRefineOrder(nullptr, "x", r, err),
        "unknown order, negative and non-numeric seeds rejected");
}

// pd5route mechanism 2: the region is the overflow's bbox (both ends of
// each overflowed edge), expanded and clipped; netTouchesBox picks the nets
// with wire in it; and on the three-nets-on-one-row fixture the region
// negotiation alone reaches zero with every net connected.
void testRegionNegotiationClearsTheThreeOnOneRowFixture() {
  Design d = parse("pd5route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(d.grid, nets);
  TileBox b;
  check(overflowRegion(d.grid, u, 0, b) && b.x0 == 0 && b.x1 == 7 && b.y0 == 1 && b.y1 == 1,
        "unexpanded region: tiles x 0-7 of row 1 (both ends of every overflowed edge)");
  check(overflowRegion(d.grid, u, 5, b) && b.x0 == 0 && b.x1 == 7 && b.y0 == 0 && b.y1 == 2,
        "expanded by 5 and clipped to the 8 x 3 grid");
  TileBox corner{6, 2, 7, 2};
  check(netTouchesBox(nets[0], TileBox{3, 1, 3, 1}) && !netTouchesBox(nets[0], corner),
        "a net touches a box its wire passes through, not one it misses");
  RegionSettings rs;
  rs.enabled = true;
  RegionReport rr = regionNegotiate(d, nets, rs, false);
  check(rr.before == 28 && rr.regionNets == 3, "starts at 28 with all three nets in the region");
  check(rr.after == 0 && totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0,
        "region negotiation reaches zero, and the returned nets really are overflow-free");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
  Usage clean = usageFromCharges(d.grid, nets);
  check(!overflowRegion(d.grid, clean, 5, b), "no overflow -> no region");
  RegionSettings p;
  std::string err;
  check(parseRegionSettings("1", "8", "30", "12", "3", p, err) && p.enabled && p.expand == 8 && p.rounds == 30 &&
            p.boxInit == 12 && p.boxGrow == 3,
        "region settings parse exactly");
  check(!parseRegionSettings("on", nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseRegionSettings(nullptr, "-1", nullptr, nullptr, nullptr, p, err) &&
            !parseRegionSettings(nullptr, nullptr, nullptr, "0", nullptr, p, err),
        "bad enable flag, negative expand, zero box init rejected");
}

// pd6route: 2-pin-piece reroutes.
// A T-shaped net -- pins (0,2), (10,2), (5,4); wire (0,2)-(10,2) and
// (5,2)-(5,4) -- has three pieces meeting at the branch tile (5,2). Other
// nets fill tile edge (7,2)-(8,2). Refinement-cost piece rerouting must
// reroute only the piece through that edge, reach zero overflow, leave the
// left piece exactly where it was, keep the net a connected tree covering
// its pins, and keep live usage equal to the charges.
void testPieceRerouteMovesOnlyTheOverflowedPiece() {
  std::string gr = "grid 12 5 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnT 0 3 1\n5 25 1\n105 25 1\n55 45 1\n0\n";
  Design d = parse("pd6route_selftest_tee.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 2, 0, 10, 2, 0}, {5, 2, 0, 5, 4, 0}};
  prepareFor2DRefine(d, nets);
  Usage u = usageFromCharges(g, nets);
  size_t hot = g.hIndex(7, 2, 0);
  u.h[hot] += u.capH[hot]; // other nets fill it to capacity; this net's wire pushes it over
  Usage other(g);
  other.h[hot] = u.capH[hot];
  Prices pr(g);
  pr.model = CostModel::kRefine;
  pr.live = &u;
  LayerPlan lp(g);
  SearchRegion region;
  PieceStats ps = rerouteNetPieces(g, u, pr, lp, region, nets[0], 10, AcceptMode::kKeep, nullptr);
  check(ps.ok && ps.pieces == 1, "exactly one piece rerouted (" + std::to_string(ps.pieces) + ")");
  check(totalOverflow(g, u) == 0, "zero overflow after");
  bool leftKept = true;
  for (int x = 0; x < 5; ++x) {
    bool has = false;
    for (const Seg &sg : nets[0].segs)
      has = has || (sg.y1 == 2 && sg.y2 == 2 && std::min(sg.x1, sg.x2) == x && std::max(sg.x1, sg.x2) == x + 1);
    leftKept = leftKept && has;
  }
  check(leftKept, "the left piece (0,2)-(5,2) is untouched, tile edge by tile edge");
  bool usesHot = false;
  for (const Seg &sg : nets[0].segs)
    usesHot = usesHot || (sg.y1 == 2 && sg.y2 == 2 && std::min(sg.x1, sg.x2) == 7);
  check(!usesHot, "the rerouted piece avoids the full tile edge");
  check(segsConnected(nets[0].segs), "the net is still one connected tree");
  Usage expect = usageFromCharges(g, nets);
  bool same = true;
  for (size_t i = 0; i < u.h.size(); ++i)
    same = same && u.h[i] == expect.h[i] + other.h[i];
  for (size_t i = 0; i < u.v.size(); ++i)
    same = same && u.v[i] == expect.v[i];
  check(same, "live usage = this net's charges + the other nets', exactly");
  std::vector<RoutedNet> loose(1);
  loose[0].net = &d.nets[0];
  loose[0].segs = {{0, 0, 0, 3, 0, 0}, {0, 4, 0, 3, 4, 0}};
  prepareFor2DRefine(d, loose);
  Usage u2 = usageFromCharges(g, loose);
  Usage u2copy = u2;
  PieceStats bad = rerouteNetPieces(g, u2, pr, lp, region, loose[0], 10, AcceptMode::kKeep, nullptr);
  check(!bad.ok && u2.h == u2copy.h, "wiring that is not a tree: not ok, usage untouched (caller falls back)");
}

// The box heuristic (distance to the sinks' bounding box) must find paths
// of exactly the same cost as the nearest-sink heuristic: both admissible.
// Part 138: the box-limit signal (MSMVResult::boxLimited) and the region stage's evidence rule.
// (1) A walled grid whose cheap gap lies outside a margin-6 box: limited; the same search over the whole
// grid (no growable edge): not limited. (2) A straight, uncongested route: not limited. (3) The claim the
// rule rests on, on the region stage's own cost model (present demand, a decayed WL term, history and
// hot spots): whenever a search reports "not limited", a whole-grid search finds exactly the same cost --
// with both heuristics; and some limited searches really were cheaper outside (the fixtures have teeth).
// (4) The region stage with the evidence rule clears the three-nets-on-one-row fixture; (5) the variant key.
void testBoxLimitSignalAndEvidenceRule() {
  {
    std::string gr = "grid 40 40 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                     "minimum width 1 1\nminimum spacing 0 0\nvia spacing 0 0\n0 0 10 10\n"
                     "num net 1\nnetW 0 2 1\n55 205 1\n155 205 1\n0\n";
    Design d = parse("pd39route_selftest_boxlimit_wall.gr", gr.c_str());
    const RoutingGrid &g = d.grid;
    Prices pr(g);
    LayerPlan lp(g);
    SearchRegion region;
    for (int y = 0; y < g.ny; ++y)
      pr.h[g.hIndex(10, y, 0)] = (y == 5) ? 1.0 : 1e6;
    std::vector<PathNode> src = {{5, 20, -1}};
    std::vector<Point> snk = {{15, 20}};
    MSMVResult narrow = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 6);
    MSMVResult whole = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 100);
    check(narrow.found && narrow.boxLimited, "box-limit signal: the wall's cheap gap outside a margin-6 box -> limited");
    check(whole.found && !whole.boxLimited, "box-limit signal: a box covering the whole grid is never limited");
    Prices flat(g);
    MSMVResult open = multiSourceDijkstra(g, flat, lp, region, src, snk, 1, 6);
    check(open.found && !open.boxLimited, "box-limit signal: a straight route on an uncongested grid -> not limited");
  }
  {
    std::string gr = "grid 30 30 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                     "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                     "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
    Design d = parse("pd39route_selftest_boxlimit_pd.gr", gr.c_str());
    const RoutingGrid &g = d.grid;
    LayerPlan lp(g);
    SearchRegion region;
    int notLimited = 0, sameWhenNot = 0, limitedCheaper = 0, cases = 0;
    for (int f = 0; f < 80; ++f) {
      Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
      Prices pr(g);
      CostSettings cs;
      cs.model = CostModel::kPresentDemand;
      configureCostModel(pr, u, cs);
      pr.pdIter = 40; // a late region round: the WL term has decayed to ~0.07
      pr.pdFactor = pdFactorForIter(pr.pdIter);
      auto hot = [&](int cx, int cy, int r, int level) {
        for (int y = std::max(0, cy - r); y <= std::min(g.ny - 1, cy + r); ++y)
          for (int x = std::max(0, cx - r); x + 1 < g.nx && x <= cx + r; ++x) {
            const size_t e = g.hIndex(x, y, 0);
            u.h[e] = u.capH[e] * level / 4;
            pr.pdHistH[e] = 1 + (x + y + f) % 6;
          }
        for (int y = std::max(0, cy - r); y + 1 < g.ny && y <= cy + r; ++y)
          for (int x = std::max(0, cx - r); x <= std::min(g.nx - 1, cx + r); ++x) {
            const size_t e = g.vIndex(x, y, 0);
            u.v[e] = u.capV[e] * level / 4;
            pr.pdHistV[e] = 1 + (x * 3 + y + f) % 6;
          }
      };
      const int sx = 4 + (f * 7) % 20, sy = 4 + (f * 11) % 20;
      const int tx = 4 + (f * 13 + 9) % 20, ty = 4 + (f * 5 + 3) % 20;
      hot((sx + tx) / 2, (sy + ty) / 2, 1 + f % 3, 5 + f % 4);
      if (f % 2)
        hot((sx + tx) / 2 + 3, (sy + ty) / 2 - 2, 1, 7);
      std::vector<PathNode> src = {{sx, sy, 0}};
      std::vector<Point> snk = {{tx, ty}};
      if (f % 3 == 0)
        snk.push_back({(tx + 5) % g.nx, ty});
      const int margin = 1 + f % 4;
      const bool boxH = f % 2 == 0;
      MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, margin, boxH);
      const double ca = a.found ? region.distAt(region.idx(a.path.back().x, a.path.back().y, 0)) : 0;
      MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 100, boxH);
      const double cb = b.found ? region.distAt(region.idx(b.path.back().x, b.path.back().y, 0)) : 0;
      if (!a.found || !b.found)
        continue;
      ++cases;
      if (!a.boxLimited) {
        ++notLimited;
        sameWhenNot += std::abs(ca - cb) <= 1e-9 * std::max(1.0, cb);
      } else if (cb < ca - 1e-9 * std::max(1.0, ca)) {
        ++limitedCheaper;
      }
    }
    check(cases == 80 && notLimited >= 10 && sameWhenNot == notLimited && limitedCheaper >= 1,
          "box-limit signal is sound on 80 present-demand fixtures: every 'not limited' search (" +
              std::to_string(sameWhenNot) + "/" + std::to_string(notLimited) +
              ") has the whole-grid cost; " + std::to_string(limitedCheaper) + " limited ones were cheaper outside");
  }
  {
    Design d = parse("pd39route_selftest_evidence_row.gr", kThreeOnOneRowGr);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings rs;
    rs.enabled = true;
    rs.evidenceStep = 2;
    RegionReport rr = regionNegotiate(d, nets, rs, false);
    bool conn = true;
    for (const RoutedNet &rn : nets)
      conn = conn && segsConnected(rn.segs);
    check(rr.after == 0 && conn && rr.evReroutes > 0 && rr.evMeanMargin >= rs.boxInit,
          "region stage with the evidence rule: zero overflow, every net connected, margins from boxInit");
  }
  {
    // The region WL floor: a late round's decayed factor is held at the floor, the heuristic's scale with
    // it (so every edge still costs at least the scale), and the stage still clears the row fixture.
    Design d = parse("pd39route_selftest_wlfloor_row.gr", kThreeOnOneRowGr);
    Usage u = usageFromCharges(d.grid, std::vector<RoutedNet>{});
    Prices pr(d.grid);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    cs.wlFloor = 0.2;
    configureCostModel(pr, u, cs);
    pr.pdIter = 100;
    pr.pdFactor = pdFactorForIter(pr.pdIter);
    double minCost = std::numeric_limits<double>::infinity();
    for (size_t e = 0; e < u.h.size(); ++e)
      minCost = std::min(minCost, wireCostH(pr, e, 2));
    check(pr.pdFactor < 0.001 && heuristicScale(pr) == 0.2 && minCost >= 0.2,
          "region WL floor: a decayed factor is held at the floor, as is the heuristic's scale");
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings rs;
    rs.enabled = true;
    rs.wlFloor = 0.2;
    RegionReport rr = regionNegotiate(d, nets, rs, false);
    check(rr.after == 0, "region stage with a WL floor of 0.2: zero overflow on the row fixture");
  }
  {
    // Part 167: the batched stage's sequential rerun. On the row fixture as path A: with a fallback gain no
    // batched stage can reach, the batched rounds run, the members go back to their entry routes, and the
    // result is the sequential stage's exactly; with a gain of 1, a batched stage that beats its start keeps
    // its own result (no rerun).
    auto runRow = [&](int batch, long long fallback, std::vector<std::vector<Seg>> &out) {
      Design d = parse("pd39route_selftest_fallback_row.gr", kThreeOnOneRowGr);
      std::vector<RoutedNet> nets(3);
      for (int i = 0; i < 3; ++i) {
        nets[i].net = &d.nets[i];
        nets[i].segs = {{0, 1, 0, 7, 1, 0}};
      }
      prepareFor2DRefine(d, nets);
      RegionSettings rs;
      rs.enabled = true;
      rs.dumpTag = "A";
      rs.batch = batch;
      rs.batchFallback = fallback;
      RegionReport rr = regionNegotiate(d, nets, rs, false);
      out.clear();
      for (const RoutedNet &rn : nets)
        out.push_back(rn.segs);
      return rr;
    };
    auto sameSegs = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
      if (a.size() != b.size())
        return false;
      for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size())
          return false;
        for (size_t k = 0; k < a[i].size(); ++k)
          if (a[i][k].x1 != b[i][k].x1 || a[i][k].y1 != b[i][k].y1 || a[i][k].x2 != b[i][k].x2 ||
              a[i][k].y2 != b[i][k].y2 || a[i][k].l1 != b[i][k].l1 || a[i][k].l2 != b[i][k].l2)
            return false;
      }
      return true;
    };
    std::vector<std::vector<Seg>> seqSegs, batchSegs, forcedSegs, keptSegs;
    const RegionReport seqR = runRow(0, 0, seqSegs);
    const RegionReport batchR = runRow(2, 0, batchSegs);
    const RegionReport forcedR = runRow(2, 1LL << 40, forcedSegs);
    const RegionReport keptR = runRow(2, 1, keptSegs);
    check(seqR.after == 0 && seqR.fallbackFrom == -1 && batchR.threadsMax > 0 && batchR.after < batchR.before &&
              forcedR.fallbackFrom == batchR.roundsRun && forcedR.threadsMax > 0 && sameSegs(forcedSegs, seqSegs) &&
              forcedR.after == seqR.after && forcedR.roundsRun == seqR.roundsRun &&
              forcedR.seconds >= forcedR.fallbackSeconds && keptR.fallbackFrom == -1 && sameSegs(keptSegs, batchSegs),
          "batched region stage short of PD39ROUTE_REGION_BATCH_FALLBACK reruns as the sequential stage exactly (" +
              std::to_string(forcedR.fallbackFrom) + " batched rounds first); one that beats its start keeps "
              "its own routes");
  }
  {
    // Part 140: the halo. A 6 x 3 grid, one-track horizontal edges (capacity 2, a net needs 2), all full but
    // edge (4,1): its distances run 0 at (4,1), 1 beside it, ...; the vertical edges all have room (0).
    const char *gr6 = "grid 6 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                      "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 0\n0\n";
    Design d6 = parse("pd39route_selftest_halo.gr", gr6);
    const RoutingGrid &g6 = d6.grid;
    Usage u6(g6);
    for (size_t e = 0; e < u6.h.size(); ++e)
      u6.h[e] = u6.capH[e];
    u6.h[g6.hIndex(4, 1, 0)] = 0;
    u6.h[g6.hIndex(0, 0, 0)] = u6.capH[g6.hIndex(0, 0, 0)] + 2; // over capacity, 4 + 1 = 5 steps from room
    std::vector<int> hh, hv;
    edgeHalo(g6, u6, 2, hh, hv);
    bool vRoom = true;
    for (int x : hv)
      vRoom = vRoom && x == 0;
    check(hh[g6.hIndex(4, 1, 0)] == 0 && hh[g6.hIndex(3, 1, 0)] == 1 && hh[g6.hIndex(4, 0, 0)] == 1 &&
              hh[g6.hIndex(0, 0, 0)] == 5 && hh[g6.hIndex(0, 2, 0)] == 5 && vRoom,
          "halo: distance to the nearest same-direction edge with room (0 there, 5 at the far corner)");
    // The dump changes nothing: the region stage on the row fixture, dump on and off.
    auto runRow = [&](bool on, std::vector<std::vector<Seg>> &out) {
      Design d = parse("pd39route_selftest_dump_row.gr", kThreeOnOneRowGr);
      std::vector<RoutedNet> nets(3);
      for (int i = 0; i < 3; ++i) {
        nets[i].net = &d.nets[i];
        nets[i].segs = {{0, 1, 0, 7, 1, 0}};
      }
      prepareFor2DRefine(d, nets);
      RegionSettings rs;
      rs.enabled = true;
      const std::string saved = gRegionDumpPrefix;
      gRegionDumpPrefix = on ? "pd39route_selftest_dump" : "";
      regionNegotiate(d, nets, rs, false);
      gRegionDumpPrefix = saved;
      out.clear();
      for (const RoutedNet &rn : nets)
        out.push_back(rn.segs);
    };
    std::vector<std::vector<Seg>> off, on;
    runRow(false, off);
    runRow(true, on);
    std::ifstream df("pd39route_selftest_dump.A.txt");
    std::string first;
    std::getline(df, first);
    int rLines = 0;
    for (std::string ln; std::getline(df, ln);)
      rLines += ln.rfind("R ", 0) == 0;
    df.close();
    std::remove("pd39route_selftest_dump.A.txt");
    bool same = off.size() == on.size();
    for (size_t i = 0; same && i < off.size(); ++i) {
      same = off[i].size() == on[i].size();
      for (size_t k = 0; same && k < off[i].size(); ++k)
        same = off[i][k].x1 == on[i][k].x1 && off[i][k].y1 == on[i][k].y1 && off[i][k].x2 == on[i][k].x2 &&
               off[i][k].y2 == on[i][k].y2;
    }
    check(same && first.rfind("G 8 3 members 3 start 28", 0) == 0 && rLines >= 1,
          "region dump: same routes with it on; a header and one R line per round");
  }
  RegionSettings v;
  std::string err;
  check(applyRegionVariant("evidence=3", v, err) && v.evidenceStep == 3 && !applyRegionVariant("evidence=1.5", v, err),
        "portfolio variant parse: evidence (an integer step)");
  check(applyRegionVariant("wlfloor=0.05", v, err) && v.wlFloor == 0.05 && !applyRegionVariant("wlfloor=-1", v, err),
        "portfolio variant parse: wlfloor (a number >= 0)");
}

void testBoxHeuristicFindsTheSameCost() {
  std::string gr = "grid 30 30 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd6route_selftest_boxh.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  int same = 0;
  for (int f = 0; f < 20; ++f) {
    Prices pr(g);
    for (size_t e = 0; e < pr.h.size(); ++e)
      pr.h[e] = 1.0 + static_cast<double>((e * 2654435761u + f * 97u) % 17);
    for (size_t e = 0; e < pr.v.size(); ++e)
      pr.v[e] = 1.0 + static_cast<double>((e * 40503u + f * 131u) % 13);
    std::vector<PathNode> src = {{3 + f % 5, 4, 0}, {4, 6 + f % 3, 0}};
    std::vector<Point> snk;
    for (int k = 0; k < 12; ++k)
      snk.push_back({18 + (k * 7 + f) % 9, 15 + (k * 5 + f) % 11});
    auto cost = [&](const MSMVResult &r) {
      double c = 0;
      for (size_t i = 1; i < r.path.size(); ++i)
        c += r.path[i].y == r.path[i - 1].y
                 ? wireCostH(pr, g.hIndex(std::min(r.path[i].x, r.path[i - 1].x), r.path[i].y, 0), 2)
                 : wireCostV(pr, g.vIndex(r.path[i].x, std::min(r.path[i].y, r.path[i - 1].y), 0), 2);
      return c;
    };
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 40, false);
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 40, true);
    same += a.found && b.found && std::abs(cost(a) - cost(b)) < 1e-9;
  }
  check(same == 20, "box heuristic: same optimal cost as nearest-sink on 20 multi-sink fixtures (" +
                        std::to_string(same) + "/20)");
}

// End to end on the three-nets-on-one-row fixture: refinement and region
// negotiation in piece mode each reach zero with every net connected.
void testPieceModeStagesClearTheThreeOnOneRowFixture() {
  for (int stage = 0; stage < 2; ++stage) {
    Design d = parse("pd6route_selftest_row.gr", kThreeOnOneRowGr);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    long long after;
    if (stage == 0) {
      RefineSettings rs;
      rs.pieces = true;
      after = refine2D(d, nets, rs, false).after;
    } else {
      RegionSettings rs;
      rs.enabled = rs.pieces = true;
      after = regionNegotiate(d, nets, rs, false).after;
    }
    bool connected = true;
    for (const RoutedNet &rn : nets)
      connected = connected && segsConnected(rn.segs);
    std::string name = stage == 0 ? "refinement" : "region negotiation";
    check(after == 0 && totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0,
          name + " in piece mode reaches zero");
    check(connected, name + " in piece mode keeps every net connected");
  }
}

// ---------------------------------------------------------------------------
// pd7route: wirelength and bend recovery.
// ---------------------------------------------------------------------------

// (0,0) -> (3,3) with everything usable: 6 tile edges, and the cheapest
// shape has one corner, so cost 6 + 1 * bend.
void testBendAwareSearchCountsCorners() {
  auto all = [](bool, int, int) { return true; };
  double c = 0;
  std::vector<Point> p = bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 1.0, all, c);
  check(p.size() == 7 && std::abs(c - 7.0) < 1e-9, "L connection: 7 tiles, cost 6 + 1 corner = 7");
  int corners = 0;
  for (size_t i = 1; i + 1 < p.size(); ++i)
    corners += (p[i - 1].y == p[i].y) != (p[i].y == p[i + 1].y);
  check(corners == 1, "exactly one corner on the returned path");
  double c0 = 0;
  bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 0.0, all, c0);
  check(std::abs(c0 - 6.0) < 1e-9, "bend 0: cost is plain length 6");
  auto wall = [](bool h, int x, int) { return !(h && x == 1); }; // no horizontal edge (1,y)-(2,y) anywhere
  double cw = 0;
  check(bendAwareSearch(0, 0, 5, 5, {{0, 0}}, {{3, 3}}, 1.0, wall, cw).empty(),
        "a full column wall with no gap: no path");
}

const char *kWlGr = "grid 12 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 1\nnW 0 2 1\n5 15 1\n85 15 1\n0\n";

// A two-pin net (1,1)-(8,1) routed as a detour: up to row 5 and back
// (8 + 2 * 4 = 16 tile edges, 2 corners). Recovery must straighten it to
// the 8-edge row-1 route, keep usage exact, keep the net connected.
void testWlRecoveryShortensADetour() {
  Design d = parse("pd7route_selftest_wl.gr", kWlGr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 0, 5, 0}, {0, 5, 0, 8, 5, 0}, {8, 5, 0, 8, 1, 0}};
  // pins are (0,1) and (8,1): tiles of "5 15" and "85 15"
  prepareFor2DRefine(d, nets);
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  check(r.wireBefore == 16 && r.wireAfter == 8, "detour of 16 tile edges straightened to 8 (" +
                                                    std::to_string(r.wireBefore) + " -> " + std::to_string(r.wireAfter) + ")");
  check(segsConnected(nets[0].segs), "still connected");
  Usage u = usageFromCharges(d.grid, nets);
  check(totalOverflow(d.grid, u) == 0, "no overflow");
}

// Same detour, but the straight row is full at tile edge (4,1)-(5,1) (other
// nets fill it to capacity). Recovery must never route through it: the
// result may shorten via row 0 or 2, but never uses the full edge, and 2D
// overflow does not rise.
void testWlRecoveryNeverUsesAFullEdge() {
  Design d = parse("pd7route_selftest_wl.gr", kWlGr);
  std::vector<RoutedNet> nets(2);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 0, 5, 0}, {0, 5, 0, 8, 5, 0}, {8, 5, 0, 8, 1, 0}};
  // A second "net" is just two parallel blockers occupying (4,1)-(5,1): same
  // Net record, used only for its charges -- width 1 + spacing 1 = 2 each.
  nets[1].net = &d.nets[0];
  nets[1].giant = true; // never touched by recovery
  nets[1].charges = {{true, 1, 4, 5, 0, 2}, {true, 1, 4, 5, 0, 2}};
  nets[1].segs = {{4, 1, 0, 5, 1, 0}};
  std::vector<RoutedNet> one(nets.begin(), nets.begin() + 1);
  prepareFor2DRefine(d, one);
  nets[0] = one[0];
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  bool usesFull = false;
  for (const Seg &sg : nets[0].segs)
    usesFull = usesFull || (sg.y1 == 1 && sg.y2 == 1 && std::min(sg.x1, sg.x2) == 4);
  check(!usesFull, "the full tile edge (4,1)-(5,1) is never used");
  check(r.wireAfter < r.wireBefore, "it still shortens around the blockage (" + std::to_string(r.wireBefore) +
                                         " -> " + std::to_string(r.wireAfter) + ")");
  check(totalOverflow(d.grid, usageFromCharges(d.grid, nets)) == 0, "no overflow created");
  check(segsConnected(nets[0].segs), "still connected");
}

// A staircase (0,1) -> (6,4) with 5 corners and minimum length 9 is
// replaced by a one-corner route of the same length: bends alone pay.
void testWlRecoveryRemovesCornersAtEqualLength() {
  const char *gr = "grid 12 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnS 0 2 1\n5 15 1\n65 45 1\n0\n";
  Design d = parse("pd7route_selftest_stair.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 1, 0, 2, 1, 0}, {2, 1, 0, 2, 2, 0}, {2, 2, 0, 4, 2, 0},
                  {4, 2, 0, 4, 3, 0}, {4, 3, 0, 6, 3, 0}, {6, 3, 0, 6, 4, 0}};
  prepareFor2DRefine(d, nets);
  WlSettings ws;
  WlReport r = wlRecover2D(d, nets, ws, false);
  std::map<std::pair<int, int>, int> o;
  for (const Seg &sg : nets[0].segs) {
    int bit = sg.y1 == sg.y2 ? 1 : 2;
    o[{sg.x1, sg.y1}] |= bit;
    o[{sg.x2, sg.y2}] |= bit;
  }
  int corners = 0;
  for (auto &[t, b] : o)
    corners += b == 3;
  check(r.wireBefore == 9 && r.wireAfter == 9, "length unchanged at the minimum 9");
  check(corners == 1, "corners 5 -> " + std::to_string(corners));
  check(segsConnected(nets[0].segs), "still connected");
}

void testParseWlSettings() {
  WlSettings w;
  std::string err;
  check(parseWlSettings(nullptr, nullptr, nullptr, nullptr, w, err) && w.enabled && w.passes == 4 && w.bend == 3.0 &&
            w.margin == 8,
        "defaults: on, 4 passes, bend 3, margin 8");
  check(parseWlSettings("0", "5", "2.5", "0", w, err) && !w.enabled && w.passes == 5 && w.bend == 2.5 && w.margin == 0,
        "explicit values parse exactly");
  check(!parseWlSettings("yes", nullptr, nullptr, nullptr, w, err) && !parseWlSettings(nullptr, "-1", nullptr, nullptr, w, err) &&
            !parseWlSettings(nullptr, nullptr, "-1", nullptr, w, err) && !parseWlSettings(nullptr, nullptr, "nan", nullptr, w, err),
        "bad flag, negative passes, negative or NaN bend rejected");
}

// Pin-aware layer assignment: a straight two-pin net on row 0 whose pins
// are on layer 0; horizontal layers 0 and 2, layer 0 half full, layer 2
// empty. Blind to pins, the nthu DP takes the emptier layer 2 (its fill
// tie-break) and the pins then need 2 vias each. With pin ranges the DP
// counts that and stays on layer 0: no via at either end.
void testPinAwareLayerAssignmentAvoidsPinStacks() {
  const char *gr = "grid 6 2 3\nvertical capacity 0 4 0\nhorizontal capacity 4 0 4\n"
                   "minimum width 1 1 1\nminimum spacing 1 1 1\nvia spacing 1 1 1\n0 0 10 10\n"
                   "num net 1\nnP 0 2 1\n5 5 1\n45 5 1\n0\n";
  Design d = parse("pd7route_selftest_pins.gr", gr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  NetTree legs, t;
  buildNetTree({{0, 0, 0, 4, 0, 0}}, legs);
  splitToUnitEdges(legs, t);
  auto run = [&](bool pins, std::vector<int> &edgeLayer, std::vector<std::pair<int, int>> &ranges) {
    RealUsage ru(g);
    RealViaUsage rvu(g);
    for (int x = 0; x < 4; ++x)
      ru.h[g.hIndex(x, 0, 0)] = 2;
    std::vector<std::pair<int, int>> pr(t.nodes.size(), {-1, -1});
    for (size_t u = 0; u < t.nodes.size(); ++u)
      if (t.nodes[u].y == 0 && (t.nodes[u].x == 0 || t.nodes[u].x == 4))
        pr[u] = {0, 0};
    int root = 0;
    return layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, root, ranges, 1.0, nullptr, nullptr, nullptr,
                         true, pins ? &pr : nullptr);
  };
  std::vector<int> blind, aware;
  std::vector<std::pair<int, int>> rb, ra;
  check(run(false, blind, rb) && run(true, aware, ra), "both DPs find assignments (sanity)");
  bool blindHigh = true, awareLow = true;
  for (size_t e = 0; e < t.edges.size(); ++e) {
    blindHigh = blindHigh && blind[e] == 2;
    awareLow = awareLow && aware[e] == 0;
  }
  check(blindHigh, "blind to pins: the emptier layer 2 (then 2 pin vias per end)");
  check(awareLow, "pin-aware: layer 0, where the pins are");
  int stacks = 0;
  for (auto &r : ra)
    stacks += r.second - r.first;
  check(stacks == 0, "pin-aware: no via stack anywhere on the net");
}

// pd8route: whole-net tree rebuild.
// shapeCost: tile edges + bend * corner tiles. An L of 3 + 2 tile edges has
// one corner: 5 + 3 * 1 = 8 at bend 3.
void testShapeCostCountsCornerTiles() {
  std::set<UnitEdge> l = unitEdgesOf({{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}});
  check(l.size() == 5 && std::abs(shapeCost(l, 3.0) - 8.0) < 1e-9, "L of 5 tile edges, 1 corner: cost 8 at bend 3");
  std::set<UnitEdge> straight = unitEdgesOf({{0, 0, 0, 5, 0, 0}});
  check(std::abs(shapeCost(straight, 3.0) - 5.0) < 1e-9, "straight 5: cost 5");
}

// A 3-pin net -- pins (0,0), (6,0), (3,4) -- wired as a wasteful tree:
// row 0 from 0 to 6, plus a separate staircase from (0,0) up to (3,4).
// Rebuild must replace it with a cheaper tree (a stub up from row 0),
// connected, covering all pins, charges consistent with usage.
void testRebuildReplacesAWastefulTree() {
  const char *gr = "grid 10 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnR 0 3 1\n5 5 1\n65 5 1\n35 45 1\n0\n";
  Design d = parse("pd8route_selftest_rb.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 0, 0, 6, 0, 0}, {0, 0, 0, 0, 1, 0}, {0, 1, 0, 1, 1, 0}, {1, 1, 0, 1, 2, 0},
                  {1, 2, 0, 2, 2, 0}, {2, 2, 0, 2, 3, 0}, {2, 3, 0, 3, 3, 0}, {3, 3, 0, 3, 4, 0}};
  prepareFor2DRefine(d, nets);
  double before = shapeCost(unitEdgesOf(nets[0].segs), 3.0);
  RebuildSettings rs;
  RebuildReport r = rebuild2D(d, nets, 3.0, rs, false);
  double after = shapeCost(unitEdgesOf(nets[0].segs), 3.0);
  check(r.rebuilt == 1 && after < before, "rebuilt, shape cost " + std::to_string(before) + " -> " + std::to_string(after));
  check(r.wireAfter == 10, "optimal: row 0 (6) + stub (4) = 10 tile edges (got " + std::to_string(r.wireAfter) + ")");
  check(segsConnected(nets[0].segs), "connected");
  Usage u = usageFromCharges(d.grid, nets);
  check(totalOverflow(d.grid, u) == 0, "no overflow");
}

// Already optimal: an L-shaped 2-pin net. Rebuild must keep it untouched
// (no strict gain), segs byte for byte.
void testRebuildKeepsAnOptimalTree() {
  const char *gr = "grid 10 8 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnK 0 2 1\n5 5 1\n35 25 1\n0\n";
  Design d = parse("pd8route_selftest_keep.gr", gr);
  std::vector<RoutedNet> nets(1);
  nets[0].net = &d.nets[0];
  nets[0].segs = {{0, 0, 0, 3, 0, 0}, {3, 0, 0, 3, 2, 0}};
  prepareFor2DRefine(d, nets);
  std::vector<Seg> before = nets[0].segs;
  RebuildReport r = rebuild2D(d, nets, 3.0, RebuildSettings{}, false);
  bool same = nets[0].segs.size() == before.size();
  for (size_t i = 0; same && i < before.size(); ++i)
    same = nets[0].segs[i].x1 == before[i].x1 && nets[0].segs[i].y1 == before[i].y1 &&
           nets[0].segs[i].x2 == before[i].x2 && nets[0].segs[i].y2 == before[i].y2;
  check(r.rebuilt == 0 && same, "optimal L: not rebuilt, segs unchanged");
}

// pd9route: search-stage piece reroutes still clear the detour-only
// fixture (three nets on a one-net row) under present-demand pricing and
// keep, with every net connected; and PD9ROUTE_STOP_AT ends the slow tier
// once overflow is at or below it (a huge threshold: stop after slow
// round 0, so fewer rounds than without it).
void testSlowPiecesAndStopAt() {
  const char *gr = "grid 8 3 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 15 1\n75 15 1\nnB 1 2 1\n5 15 1\n75 15 1\n"
                   "nC 2 2 1\n5 15 1\n75 15 1\n0\n";
  Design d = parse("pd9route_selftest_detour.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  RunReport rep = route(d, 10, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                        true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, true, -1);
  bool connected = rep.nets.size() == 3;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(rep.bestOverflow == 0 && connected, "slow-tier pieces + keep: zero 2D overflow, every net connected");
  // Stop-at needs a run that cannot converge: the same three nets on a
  // single row (no room to detour), so keep mode runs every slow round.
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d2 = parse("pd9route_selftest_row.gr", row);
  RunReport full = route(d2, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                         true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1);
  Design d3 = parse("pd9route_selftest_row.gr", row);
  RunReport early = route(d3, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                          true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, false, 1000000);
  check(!full.convergedToZero && full.roundsRun == 21, "without a threshold: 1 fast + all 20 slow rounds (" +
                                                           std::to_string(full.roundsRun) + ")");
  check(early.roundsRun == 2, "threshold above the overflow: stops after slow round 0 (" +
                                  std::to_string(early.roundsRun) + " rounds)");
}

// pd10route: pattern-first piece reroutes. A 2-pin net (0,1) -> (8,3)
// wired as the L along row 1 then up column 8, with row 1's tile edge
// (4,1)-(5,1) full: the other L (up column 0, along row 3) fits, so the
// piece is rerouted by a pattern -- no maze -- avoids the full edge, and
// the net stays connected. With the (4,y)-(5,y) edge full on every row, no
// L or Z shape can cross x = 4..5, and the maze search takes over.
// (Endpoints on one row admit only the straight shape, NTHU's monotonic
// routing likewise -- a piece along a blocked row always goes to the maze.)
void testPatternFirstPieceReroute() {
  std::string gr = "grid 12 5 2\nvertical capacity 0 4\nhorizontal capacity 4 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nnL 0 2 1\n5 15 1\n85 35 1\n0\n";
  for (int blockRows = 0; blockRows < 2; ++blockRows) {
    Design d = parse("pd10route_selftest_l.gr", gr.c_str());
    const RoutingGrid &g = d.grid;
    std::vector<RoutedNet> nets(1);
    nets[0].net = &d.nets[0];
    nets[0].segs = {{0, 1, 0, 8, 1, 0}, {8, 1, 0, 8, 3, 0}};
    prepareFor2DRefine(d, nets);
    Usage u = usageFromCharges(g, nets);
    size_t hot = g.hIndex(4, 1, 0);
    u.h[hot] += u.capH[hot];
    if (blockRows)
      for (int y = 0; y < g.ny; ++y)
        if (y != 1)
          u.h[g.hIndex(4, y, 0)] = u.capH[g.hIndex(4, y, 0)];
    Prices pr(g);
    pr.model = CostModel::kRefine;
    pr.live = &u;
    LayerPlan lp(g);
    SearchRegion region;
    patternFirst = true;
    PieceStats ps = rerouteNetPieces(g, u, pr, lp, region, nets[0], 10, AcceptMode::kKeep, nullptr);
    patternFirst = false;
    bool usesHot = false;
    for (const Seg &sg : nets[0].segs)
      usesHot = usesHot || (sg.y1 == 1 && sg.y2 == 1 && std::min(sg.x1, sg.x2) == 4);
    if (!blockRows) {
      check(ps.ok && ps.pieces == 1 && ps.patterned == 1, "a fitting L exists: rerouted by pattern, no maze");
      check(!usesHot && segsConnected(nets[0].segs), "avoids the full edge; net still connected");
      check(totalOverflow(g, u) == 0, "no overflow left");
    } else {
      check(ps.ok && ps.pieces == 1 && ps.patterned == 0, "every L/Z shape blocked: the maze search takes over");
    }
  }
}

// pd11route: the batched pattern stage. Two nets with identical corner
// pins (0,0) -> (5,3) on a grid whose tile edges hold one net each: the
// only zero-overflow answer is one net on each L. The stage (CPU backend,
// run through route() with no slow rounds) must find it -- the two pieces'
// boxes overlap, so they land in different color batches and the second
// sees the first's usage -- and leave both nets connected.
void testPatternStageSeparatesTwoNetsOntoTheTwoLs() {
  const char *gr = "grid 8 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 2\nnA 0 2 1\n5 5 1\n55 35 1\nnB 1 2 1\n5 5 1\n55 35 1\n0\n";
  Design d = parse("pd11route_selftest_ls.gr", gr);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  PatternStageSettings ps;
  RunReport rep = route(d, 60, 0, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true,
                        8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1, &ps);
  check(rep.bestOverflow == 0, "pattern stage alone: zero 2D overflow (got " + std::to_string(rep.bestOverflow) + ")");
  bool connected = rep.nets.size() == 2;
  for (const RoutedNet &rn : rep.nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "both nets connected");
  std::set<std::pair<bool, std::pair<int, int>>> a, b;
  for (const ChargeRecord &c : rep.nets[0].charges)
    a.insert({c.horizontal, {c.cross, c.a}});
  for (const ChargeRecord &c : rep.nets[1].charges)
    b.insert({c.horizontal, {c.cross, c.a}});
  bool disjoint = true;
  for (auto &e : a)
    disjoint = disjoint && !b.count(e);
  check(disjoint && a.size() == 8 && b.size() == 8, "the two nets take the two disjoint 8-edge Ls");
}

// pd12route: the plateau rule itself, then both stages on a fixture that
// cannot improve (three nets on a one-row grid with room for one: every
// round leaves overflow exactly where it was) -- without the rule each runs
// its whole budget; with window N it stops after exactly N rounds, keeping
// the same (best) state -- and on the three-nets-on-one-row fixture that
// can reach zero, where the rule must not cut the stage short.
void testPlateauStop() {
  std::vector<long long> h{100, 90, 89, 89};
  check(!plateauReached(h, 0, 0.01), "window 0: never stops");
  check(!plateauReached(h, 3, 0.01), "window 3 after 3 rounds: 100 -> 89 is a real gain, continue");
  check(!plateauReached({100, 90, 89}, 2, 0.01),
        "window 2: 100 -> 89 gains 11 >= max(1, 1% of 100) = 1, continue");
  check(!plateauReached(h, 2, 0.01), "window 2: 90 -> 89 gains 1 = the 1-unit floor, continue");
  check(plateauReached({100, 90, 89, 89, 89}, 2, 0.01), "window 2: 89 -> 89, no gain, stop");
  check(plateauReached({100, 100, 100}, 2, 0.0), "gain 0 still needs 1 unit: flat for 2 rounds stops");
  check(!plateauReached({100, 100, 99}, 2, 0.0), "gain 0: one unit in the window is enough to continue");
  check(plateauReached({1000, 995, 990}, 2, 0.02) && !plateauReached({1000, 990, 980}, 2, 0.02),
        "gain 2%: 1000 -> 990 (1%) stops, 1000 -> 980 (2%) continues");
  check(!plateauReached({5}, 1, 0.5), "no rounds run yet: never stops");
  check(plateauReached({100, 100, 100}, 2, 0.0, 100) && !plateauReached({100, 100, 100}, 2, 0.0, 101),
        "floor: a flat best of 100 stops with floor 100, not with floor 101");

  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto stuck = [&](Design &d) {
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    return nets;
  };
  for (int stage = 0; stage < 2; ++stage) {
    for (int pieces = 0; pieces < 2; ++pieces) {
      std::string name = std::string(stage == 0 ? "region" : "refinement") + (pieces ? " (pieces)" : " (whole nets)");
      Design d1 = parse("pd12route_selftest_stuck.gr", row), d2 = parse("pd12route_selftest_stuck.gr", row);
      std::vector<RoutedNet> n1 = stuck(d1), n2 = stuck(d2);
      long long start = totalOverflow(d1.grid, usageFromCharges(d1.grid, n1));
      int runFull, runStop;
      long long afterFull, afterStop;
      bool plFull, plStop;
      int runFloorAbove = -1;
      if (stage == 0) {
        RegionSettings rs;
        rs.enabled = true;
        rs.pieces = pieces;
        rs.rounds = 40;
        RegionReport a = regionNegotiate(d1, n1, rs, false);
        rs.plateau = 5;
        rs.plateauMin = start; // the stuck overflow itself: the rule applies
        RegionReport b = regionNegotiate(d2, n2, rs, false);
        Design d3 = parse("pd12route_selftest_stuck.gr", row);
        std::vector<RoutedNet> n3 = stuck(d3);
        rs.plateauMin = start + 1; // above it: the rule never applies
        runFloorAbove = regionNegotiate(d3, n3, rs, false).roundsRun;
        runFull = a.roundsRun, runStop = b.roundsRun, afterFull = a.after, afterStop = b.after;
        plFull = a.plateaued, plStop = b.plateaued;
      } else {
        RefineSettings rs;
        rs.pieces = pieces;
        rs.iters = 40;
        RefineReport a = refine2D(d1, n1, rs, false);
        rs.plateau = 5;
        rs.plateauMin = start;
        RefineReport b = refine2D(d2, n2, rs, false);
        Design d3 = parse("pd12route_selftest_stuck.gr", row);
        std::vector<RoutedNet> n3 = stuck(d3);
        rs.plateauMin = start + 1;
        runFloorAbove = refine2D(d3, n3, rs, false).itersRun;
        runFull = a.itersRun, runStop = b.itersRun, afterFull = a.after, afterStop = b.after;
        plFull = a.plateaued, plStop = b.plateaued;
      }
      check(start > 0 && runFull == 40 && !plFull,
            name + ", stuck fixture, no window: runs all 40 rounds (" + std::to_string(runFull) + ")");
      check(runStop == 5 && plStop, name + ", window 5, floor = the stuck overflow " + std::to_string(start) +
                                        ": stops after exactly 5 rounds (" + std::to_string(runStop) + ")");
      check(runFloorAbove == 40, name + ", window 5, floor one above it: runs all 40 rounds (" +
                                     std::to_string(runFloorAbove) + ")");
      check(afterStop == afterFull && afterStop == totalOverflow(d2.grid, usageFromCharges(d2.grid, n2)),
            name + ": same best overflow either way, and the returned nets really carry it");
    }
  }
  // A stage that can reach zero is not cut short by a window of 1 round and
  // a demanding 50% gain -- as long as each round really improves; and it
  // reports no plateau stop when it converges.
  Design dz = parse("pd12route_selftest_row.gr", kThreeOnOneRowGr);
  std::vector<RoutedNet> nz(3);
  for (int i = 0; i < 3; ++i) {
    nz[i].net = &dz.nets[i];
    nz[i].segs = {{0, 1, 0, 7, 1, 0}};
  }
  prepareFor2DRefine(dz, nz);
  RegionSettings rz;
  rz.enabled = true;
  rz.plateau = 3;
  RegionReport rr = regionNegotiate(dz, nz, rz, false);
  check(rr.after == 0 && !rr.plateaued, "solvable fixture with window 3: region still reaches zero (" +
                                             std::to_string(rr.after) + " after " + std::to_string(rr.roundsRun) +
                                             " rounds)");
  int rw = 0, fw = 0;
  double gf = 0.01;
  long long mn = 0;
  std::string err;
  check(parsePlateauSettings("50", "10", "2.5", "1000", rw, fw, gf, mn, err) && rw == 50 && fw == 10 &&
            gf == 0.025 && mn == 1000,
        "plateau settings parse exactly (percent -> fraction)");
  rw = 7;
  check(parsePlateauSettings(nullptr, nullptr, nullptr, nullptr, rw, fw, gf, mn, err) && rw == 7 && mn == 1000,
        "unset values leave the defaults alone");
  check(!parsePlateauSettings("-1", nullptr, nullptr, nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, "x", nullptr, nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, "101", nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, "-0.5", nullptr, rw, fw, gf, mn, err) &&
            !parsePlateauSettings(nullptr, nullptr, nullptr, "-3", rw, fw, gf, mn, err),
        "negative or non-numeric windows or floor, gain outside [0, 100] rejected");
}

// pd13route: the margin formula and its cap, then both stages on the stuck
// fixture (each stage reports the margin of its last round): 40 rounds grow the region margin 10 + 2r to 88 and the
// refinement margin 10 + 15i to 595 uncapped; with a cap of 20 neither
// goes past 20, and the best overflow is unchanged (the fixture has no
// detour at any margin).
void testStageMarginCap() {
  check(stageMargin(10, 2, 0, 0) == 10 && stageMargin(10, 2, 499, 0) == 1008,
        "uncapped: 10 + 2r, 1008 by round 499");
  check(stageMargin(10, 2, 499, 200) == 200 && stageMargin(10, 2, 94, 200) == 198 && stageMargin(10, 2, 95, 200) == 200,
        "cap 200: follows 10 + 2r up to round 94 (198), 200 from round 95 on");
  check(stageMargin(10, 0, 1000000, 5) == 5 && stageMargin(300, 5, 0, 200) == 200,
        "a cap below the starting margin still caps it");
  check(stageMargin(10, 1000000, 1000000, 0) == std::numeric_limits<int>::max(),
        "a huge uncapped product saturates instead of wrapping");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  for (int stage = 0; stage < 2; ++stage) {
    int maxMargin[2] = {0, 0};
    long long after[2] = {0, 0};
    for (int capped = 0; capped < 2; ++capped) {
      Design d = parse("pd13route_selftest_stuck.gr", row);
      std::vector<RoutedNet> nets(3);
      for (int i = 0; i < 3; ++i) {
        nets[i].net = &d.nets[i];
        nets[i].segs = {{0, 0, 0, 7, 0, 0}};
      }
      prepareFor2DRefine(d, nets);
      if (stage == 0) {
        RegionSettings rs;
        rs.enabled = true;
        rs.rounds = 40;
        rs.boxCap = capped ? 20 : 0;
        RegionReport rr = regionNegotiate(d, nets, rs, false);
        after[capped] = rr.after;
        maxMargin[capped] = rr.lastMargin;
      } else {
        RefineSettings rs;
        rs.iters = 40;
        rs.boxCap = capped ? 20 : 0;
        RefineReport fr = refine2D(d, nets, rs, false);
        after[capped] = fr.after;
        maxMargin[capped] = fr.lastMargin;
      }
    }
    std::string name = stage == 0 ? "region" : "refinement";
    int uncappedWant = stage == 0 ? 88 : 595;
    check(maxMargin[0] == uncappedWant, name + " uncapped: last margin " + std::to_string(maxMargin[0]) + " (want " +
                                            std::to_string(uncappedWant) + ")");
    check(maxMargin[1] == 20, name + " with cap 20: last margin " + std::to_string(maxMargin[1]));
    check(after[0] == after[1], name + ": same best overflow capped and uncapped on a fixture with no detour");
  }
  int rc = 7, fc = 9;
  std::string err;
  check(parseBoxCaps("200", "150", rc, fc, err) && rc == 200 && fc == 150, "box caps parse exactly");
  check(parseBoxCaps(nullptr, nullptr, rc, fc, err) && rc == 200 && fc == 150, "unset caps leave values alone");
  check(!parseBoxCaps("-1", nullptr, rc, fc, err) && !parseBoxCaps(nullptr, "big", rc, fc, err),
        "negative or non-numeric caps rejected");
}

// Part 168: PD39ROUTE_REFINE_STOP. On the stuck one-row fixture (no gain is possible) refinement stops after W
// iterations without gain and returns what all 40 iterations return; with the best at or above the floor the rule
// does not act; on the clearing fixture it changes nothing. The gain trace: no gain -> -1, else the last one.
// Part 213: PD39ROUTE_REFINE_KEEP_ABOVE -- off (0) leaves the configured acceptance; at or above N a revert stage
// keeps; below N it stays revert; a keep stage stays keep.
void testRefineKeepAbove() {
  RefineSettings rs;
  rs.accept = AcceptMode::kRevert;
  bool ok = refineAcceptFor(rs, 1000000) == AcceptMode::kRevert;
  rs.keepAbove = 1000;
  ok = ok && refineAcceptFor(rs, 1000) == AcceptMode::kKeep && refineAcceptFor(rs, 33000) == AcceptMode::kKeep &&
       refineAcceptFor(rs, 999) == AcceptMode::kRevert && refineAcceptFor(rs, 0) == AcceptMode::kRevert;
  rs.accept = AcceptMode::kKeep;
  ok = ok && refineAcceptFor(rs, 5) == AcceptMode::kKeep;
  check(ok, "refine keep-above: off leaves revert; >= N keeps, < N reverts; keep stays keep");
}

void testRefineLowStop() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto runOn = [](const char *name, const char *text, int y, int lowStop, long long floor, RefineReport &rr) {
    Design d = parse(name, text);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, y, 0, 7, y, 0}};
    }
    prepareFor2DRefine(d, nets);
    RefineSettings rs;
    rs.iters = 40;
    rs.accept = AcceptMode::kRevert;
    rs.lowStop = lowStop;
    rs.plateauMin = floor;
    rr = refine2D(d, nets, rs, false);
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    return out;
  };
  auto same = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size())
        return false;
      for (size_t k = 0; k < a[i].size(); ++k)
        if (a[i][k].x1 != b[i][k].x1 || a[i][k].y1 != b[i][k].y1 || a[i][k].x2 != b[i][k].x2 ||
            a[i][k].y2 != b[i][k].y2 || a[i][k].l1 != b[i][k].l1 || a[i][k].l2 != b[i][k].l2)
          return false;
    }
    return true;
  };
  RefineReport full, stop, above;
  const auto sFull = runOn("pd39route_selftest_lowstop.gr", row, 0, 0, 1000, full);
  const auto sStop = runOn("pd39route_selftest_lowstop.gr", row, 0, 5, 1000, stop);
  const auto sAbove = runOn("pd39route_selftest_lowstop.gr", row, 0, 5, 0, above);
  check(full.itersRun == 40 && full.after == full.before && full.lastGain == -1 && !full.lowStopped,
        "stuck fixture, no stop: all 40 iterations, no gain (trace: last gain -1)");
  check(stop.itersRun == 5 && stop.lowStopped && stop.after == full.after && same(sFull, sStop),
        "PD39ROUTE_REFINE_STOP 5 below the floor: stops after 5 iterations without gain, the same routes as 40");
  check(above.itersRun == 40 && !above.lowStopped, "PD39ROUTE_REFINE_STOP does not act at or above the floor");
  RefineReport cFull, cStop;
  const auto tFull = runOn("pd39route_selftest_lowstop_clear.gr", kThreeOnOneRowGr, 1, 0, 1000, cFull);
  const auto tStop = runOn("pd39route_selftest_lowstop_clear.gr", kThreeOnOneRowGr, 1, 1, 1000, cStop);
  check(cFull.after == 0 && cFull.lastGain >= 0 && cStop.after == 0 && !cStop.lowStopped && same(tFull, tStop),
        "clearing fixture: PD39ROUTE_REFINE_STOP 1 changes nothing (trace: last gain at iteration " +
            std::to_string(cFull.lastGain) + ")");
}

// pd14route: the work-gated rule itself, the expansion counter, and the
// region stage on the stuck fixture: no window -> all 40 rounds; window 5
// with a work threshold the rounds clear -> exactly 5; the same window with
// a threshold above the work actually done -> all 40.
void testWorkGatedPlateau() {
  std::vector<long long> flat{100, 100, 100, 100}, pops{0, 10, 20, 30};
  check(!workGatedPlateau(flat, pops, 0, 0, 0.01), "window 0: never stops");
  check(workGatedPlateau(flat, pops, 3, 30, 0.01) && !workGatedPlateau(flat, pops, 3, 31, 0.01),
        "flat for 3 rounds: stops when those rounds did >= 30 expansions, not when 31 are required");
  check(!workGatedPlateau({100, 90, 80, 70}, pops, 3, 0, 0.01), "still improving: never stops, however much work");
  check(!workGatedPlateau(flat, {0, 10, 20}, 3, 0, 0.01), "mismatched histories: never stops");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto runRegion = [&](int window, long long popsNeeded) {
    Design d = parse("pd14route_selftest_stuck.gr", row);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings rs;
    rs.enabled = true;
    rs.rounds = 40;
    rs.costWindow = window;
    rs.costPops = popsNeeded;
    return regionNegotiate(d, nets, rs, false);
  };
  RegionReport full = runRegion(0, 0);
  RegionReport again = runRegion(0, 0);
  check(full.roundsRun == 40 && !full.costStopped && full.pops > 0,
        "no window: all 40 rounds, " + std::to_string(full.pops) + " expansions counted");
  check(again.pops == full.pops, "the expansion count is deterministic (same fixture, same count)");
  long long perWindow = full.pops / 8; // 5 of 40 identical rounds
  RegionReport stop = runRegion(5, perWindow);
  check(stop.roundsRun == 5 && stop.costStopped && stop.after == full.after,
        "window 5, threshold = the work of 5 rounds: stops after exactly 5, same best (" +
            std::to_string(stop.roundsRun) + " rounds)");
  RegionReport cheap = runRegion(5, full.pops + 1);
  check(cheap.roundsRun == 40 && !cheap.costStopped,
        "window 5, threshold above all the work done: all 40 rounds (" + std::to_string(cheap.roundsRun) + ")");
  { // Part 199: the same stop counted in reroutes (the fixture reroutes its 3 nets every round)
    auto runReroutes = [&](long long n) {
      Design d = parse("pd14route_selftest_stuck.gr", row);
      std::vector<RoutedNet> nets(3);
      for (int i = 0; i < 3; ++i) {
        nets[i].net = &d.nets[i];
        nets[i].segs = {{0, 0, 0, 7, 0, 0}};
      }
      prepareFor2DRefine(d, nets);
      RegionSettings rs;
      rs.enabled = true;
      rs.rounds = 40;
      rs.costWindow = 5;
      rs.costPops = full.pops + 1; // the expansion rule cannot fire
      rs.costReroutes = n;
      return regionNegotiate(d, nets, rs, false);
    };
    RegionReport few = runReroutes(1), many = runReroutes(1000000);
    check(few.roundsRun == 5 && few.costStopped && few.after == full.after && many.roundsRun == 40 && !many.costStopped,
          "work-gated stop counted in reroutes: window 5 at >= 1 reroute stops after 5 rounds (" +
              std::to_string(few.roundsRun) + "), at a million never (" + std::to_string(many.roundsRun) + ")");
  }
  int w = 7;
  long long p = 9;
  std::string err;
  check(parseCostPlateau("100", "2000000000", w, p, err) && w == 100 && p == 2000000000LL,
        "work-gated settings parse exactly");
  // pd38route: the threshold scaled to the design (PD38ROUTE_REGION_COST_REL).
  check(relativeCostPops(32000000000LL, 0.0, 5) == 32000000000LL, "relative scale off: the absolute threshold");
  check(relativeCostPops(32000000000LL, 10.0, 150000) == 1500000,
        "small design: 10 x 150000 expansions before = 1.5M, below the absolute threshold");
  check(relativeCostPops(32000000000LL, 10.0, 8000000000LL) == 32000000000LL,
        "large design: 10 x 8e9 is above the absolute threshold, which stays");
  check(relativeCostPops(32000000000LL, 10.0, 0) == 0, "no search before the region stage: threshold 0");
  check(relativeCostPops(0, 4.0, 25) == 100, "no absolute threshold: the scaled one alone");
  check(relativeCostPops(0, 1.0e6, 9000000000000000000LL) == std::numeric_limits<long long>::max(),
        "a huge product saturates instead of overflowing");
  check(relativeCostPops(500, 2.0, -7) == 0, "a negative count is treated as 0");
  {
    // The scaled threshold drives the same stop: the stuck fixture's 40 rounds
    // with `before` = a quarter of a window's work and F = 4 stop after 5.
    RegionReport rel = runRegion(5, relativeCostPops(full.pops + 1, 4.0, perWindow / 4));
    check(rel.roundsRun == 5 && rel.costStopped && rel.after == full.after,
          "scaled threshold (4 x a quarter window) stops the stuck fixture after 5 rounds, same best");
  }
  double rel = 3.0;
  check(parseCostRel(nullptr, rel, err) && rel == 3.0, "unset relative scale leaves the value");
  check(parseCostRel("10", rel, err) && rel == 10.0 && parseCostRel("0.5", rel, err) && rel == 0.5,
        "relative scale parses");
  check(!parseCostRel("-1", rel, err) && !parseCostRel("x", rel, err) && !parseCostRel("nan", rel, err) &&
            !parseCostRel("1e7", rel, err),
        "negative, non-numeric, NaN or huge relative scale rejected");
  check(parseCostPlateau(nullptr, nullptr, w, p, err) && w == 100, "unset settings leave values alone");
  check(!parseCostPlateau("-1", nullptr, w, p, err) && !parseCostPlateau(nullptr, "lots", w, p, err),
        "negative or non-numeric settings rejected");
}

// pd15route: the detour statistics on hand-built nets. Net A: pins (0,0),
// (3,0), wire straight along row 0 -> wire 3 = MST 3. Net B: pins (0,2),
// (2,2), wired up to row 4 and back (0,2)-(0,4)-(2,4)-(2,2) -> wire 6, MST
// 2, > 1.5x. Net C: three pins at (5,0), (7,0), (5,0) again -> MST counts
// distinct tiles: 2. A giant net is skipped.
void testDetourStats() {
  std::vector<RoutedNet> nets(4);
  nets[0].pts = {{0, 0}, {3, 0}};
  nets[0].charges = {{true, 0, 0, 3, 0, 1}};
  nets[1].pts = {{0, 2}, {2, 2}};
  nets[1].charges = {{false, 0, 2, 4, 0, 1}, {true, 4, 0, 2, 0, 1}, {false, 2, 2, 4, 0, 1}};
  nets[2].pts = {{5, 0}, {7, 0}, {5, 0}};
  nets[2].charges = {{true, 0, 5, 7, 0, 1}};
  nets[3].pts = {{0, 0}, {9, 9}};
  nets[3].giant = true;
  nets[3].charges = {{true, 0, 0, 9, 0, 1}};
  DetourStats st = detourStats(nets);
  check(st.nets == 3 && st.wire == 3 + 6 + 2 && st.mst == 3 + 2 + 2,
        "wire 11, pin-tile MST 7 over the three non-giant nets (got " + std::to_string(st.wire) + ", " +
            std::to_string(st.mst) + ")");
  check(st.netsHigh == 1 && st.excessHigh == 4, "one net over 1.5x its MST, carrying all 4 of the excess");
  check(pinTileMst({{0, 0}, {4, 0}, {4, 3}, {0, 3}}) == 10 && pinTileMst({{1, 1}, {1, 1}}) == 0,
        "MST of a 4 x 3 rectangle's corners 10; duplicate pins cost nothing");
}

// pd16route: the reclaim pass. On the 8 x 3 one-track fixture, net A runs
// (0,1) -> (7,1) the long way: down to row 0, along it, back up (wire 9,
// MST 7; ratio 1.2 and minimum excess 2 for this small case). With row 1
// free the pass reroutes it straight (wire 7) and keeps
// it. With net B occupying row 1 end to end (row 1 then has no room), A's
// straight route would overflow every edge, so A is left as it was (the
// search prefers its detour, and a shorter route that overflows is not worth
// it at mu 8). A net within the ratio is never a candidate.
void testDetourReclaim() {
  auto build = [&](bool withB, Design &d, std::vector<RoutedNet> &nets) {
    d = parse("pd16route_selftest_row.gr", kThreeOnOneRowGr);
    nets.assign(withB ? 2 : 1, RoutedNet{});
    nets[0].net = &d.nets[0];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 7, 0, 0}, {7, 0, 0, 7, 1, 0}};
    if (withB) {
      nets[1].net = &d.nets[1];
      nets[1].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
  };
  Design d1;
  std::vector<RoutedNet> n1;
  build(false, d1, n1);
  check(netWire(n1[0]) == 9 && pinTileMst(n1[0].pts) == 7, "fixture: A's wire 9 against an MST of 7");
  ReclaimSettings rs;
  rs.enabled = true;
  rs.ratio = 1.2; // 9 / 7 = 1.29: a candidate at 1.2 (not at the default 1.5)
  rs.minExcess = 2;
  ReclaimReport r1 = reclaimDetours(d1, n1, rs);
  check(r1.candidates == 1 && r1.kept == 1 && netWire(n1[0]) == 7 && segsConnected(n1[0].segs),
        "row 1 free: A rerouted straight (wire 7), kept, still connected (wire " + std::to_string(netWire(n1[0])) + ")");
  check(r1.overflowBefore == 0 && r1.overflowAfter == 0, "no overflow before or after");
  Design d2;
  std::vector<RoutedNet> n2;
  build(true, d2, n2);
  ReclaimReport r2 = reclaimDetours(d2, n2, rs);
  check(r2.candidates == 1 && r2.kept == 0 && netWire(n2[0]) == 9 && r2.overflowAfter == r2.overflowBefore,
        "row 1 full: A left as it was, overflow unchanged (kept " + std::to_string(r2.kept) + ")");
  Design d3;
  std::vector<RoutedNet> n3;
  build(false, d3, n3);
  ReclaimSettings loose = rs;
  loose.ratio = 1.4; // 9 > 1.4 * 7 = 9.8? no
  ReclaimReport r3 = reclaimDetours(d3, n3, loose);
  check(r3.candidates == 0 && netWire(n3[0]) == 9, "ratio 1.4: 9 is not > 9.8, A is not a candidate");
  ReclaimSettings p;
  std::string err;
  check(parseReclaimSettings("1", "2", "6", "0", "24", "3", p, err) && p.enabled && p.ratio == 2 && p.minExcess == 6 &&
            p.mu == 0 && p.margin == 24 && p.passes == 3,
        "reclaim settings parse exactly");
  check(!parseReclaimSettings("yes", nullptr, nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, "1", nullptr, nullptr, nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, "-1", nullptr, nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, nullptr, "0", nullptr, p, err) &&
            !parseReclaimSettings(nullptr, nullptr, nullptr, nullptr, nullptr, "0", p, err),
        "bad flag, ratio <= 1, negative mu, margin 0, passes 0 rejected");
}

// pd17route: on a 16 x 5 one-track grid, nets A (x 0-5) and D (x 10-15)
// each run row 1 the long way (down to row 0 and back: wire 7, MST 5),
// while B and C share row 3 from x 10 to 15, overflowing it. With avoid
// off both A and D are rerouted straight; with avoid 2, D (its tiles 2 rows
// from the overflow) is skipped and only A is rerouted; with avoid 1 both
// are rerouted again (row 1 is 2 rows from row 3).
void testReclaimAvoidsOverflow() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  auto run = [&](int avoid, long long &avoided, long long &wireA, long long &wireD, long long &ovBefore) {
    Design d = parse("pd17route_selftest_avoid.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    ReclaimSettings rs;
    rs.enabled = true;
    rs.ratio = 1.2;
    rs.minExcess = 2;
    rs.avoid = avoid;
    ReclaimReport r = reclaimDetours(d, nets, rs);
    avoided = r.avoided;
    wireA = netWire(nets[0]);
    wireD = netWire(nets[1]);
    ovBefore = r.overflowBefore;
  };
  long long av, wa, wd, ov;
  run(-1, av, wa, wd, ov);
  check(ov > 0 && av == 0 && wa == 5 && wd == 5, "avoid off: overflow on row 3, both A and D rerouted straight (" +
                                                       std::to_string(wa) + ", " + std::to_string(wd) + ")");
  run(2, av, wa, wd, ov);
  check(av == 1 && wa == 5 && wd == 7, "avoid 2: D (2 rows from the overflow) skipped, only A rerouted");
  run(1, av, wa, wd, ov);
  check(av == 0 && wa == 5 && wd == 5, "avoid 1: row 1 is 2 rows from row 3, so neither is skipped");
  ReclaimSettings p;
  std::string err;
  check(parseReclaimAvoid("5", p, err) && p.avoid == 5 && parseReclaimAvoid(nullptr, p, err) && p.avoid == 5,
        "avoid parses; unset leaves it");
  check(!parseReclaimAvoid("-2", p, err) && !parseReclaimAvoid("near", p, err), "avoid below -1 or non-numeric rejected");
}

// pd18route: on pd9route's single-row fixture (three nets, 8 x 1, slow
// tier with keep), a stop threshold above the starting overflow stops the tier
// after slow round 0 (2 rounds run in all). With the reclaim at the stop
// on, the first stop runs the pass and the tier continues: more rounds run,
// one reclaim is reported, and every net is still connected. The settings
// parse and reject bad input.
void testSlowTierReclaim() {
  // pd9route's non-convergent fixture: three nets on a single row with
  // room for one, so overflow can never reach zero and the stop threshold
  // is what ends the tier.
  const char *gr = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                   "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  auto runOnce = [&](bool atStop) {
    Design d = parse("pd18route_selftest_row.gr", gr);
    SlowReclaim saved = gSlowReclaim;
    gSlowReclaim = SlowReclaim{};
    gSlowReclaim.atStop = atStop;
    gSlowReclaim.settings.enabled = true;
    gSlowReclaim.settings.ratio = 1.01;
    gSlowReclaim.settings.minExcess = 1;
    RunReport rep = route(d, 1, 20, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true, 8,
                          BoxGrowth{}, cs, AcceptMode::kKeep, true, 1000000);
    gSlowReclaim = saved;
    bool connected = rep.nets.size() == 3;
    for (const RoutedNet &rn : rep.nets)
      connected = connected && segsConnected(rn.segs);
    return std::make_tuple(rep.roundsRun, rep.slowReclaims, connected);
  };
  auto [roundsOff, reclaimsOff, okOff] = runOnce(false);
  auto [roundsOn, reclaimsOn, okOn] = runOnce(true);
  check(roundsOff == 2 && reclaimsOff == 0 && okOff, "no reclaim: the threshold stops the tier after slow round 0 (" +
                                                          std::to_string(roundsOff) + " rounds)");
  check(reclaimsOn == 1 && roundsOn == roundsOff + 1 && okOn,
        "reclaim at the stop: one pass, the tier continues (" + std::to_string(roundsOn) + " rounds), nets connected");
  SlowReclaim p;
  std::string err;
  check(parseSlowReclaim("1", "25", p, err) && p.atStop && p.every == 25, "slow-tier reclaim settings parse exactly");
  check(!parseSlowReclaim("on", nullptr, p, err) && !parseSlowReclaim(nullptr, "-1", p, err),
        "bad flag or negative period rejected");
}

// pd19route: the choice rule, then both paths on pd17route's 16 x 5
// fixture (A and D detoured, B and C sharing row 3). Whatever the region
// stage and refinement make of each path, the kept state is the better of
// the two by (overflow, then wire), and the report's numbers are the kept
// state's own.
void testReclaimFallback() {
  check(fallbackPrefersB(5, 100, 4, 200) && !fallbackPrefersB(4, 200, 5, 100), "lower overflow wins, whatever the wire");
  check(fallbackPrefersB(4, 200, 4, 199) && !fallbackPrefersB(4, 200, 4, 200) && !fallbackPrefersB(4, 199, 4, 200),
        "tied overflow: strictly less wire wins; a full tie keeps A");
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  Design d = parse("pd19route_selftest_fallback.gr", gr);
  std::vector<RoutedNet> nets(4);
  for (int i = 0; i < 4; ++i)
    nets[i].net = &d.nets[i];
  nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
  nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
  nets[2].segs = {{10, 3, 0, 15, 3, 0}};
  nets[3].segs = {{10, 3, 0, 15, 3, 0}};
  prepareFor2DRefine(d, nets);
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0; // the box stays on row 3, so path A's region stage leaves A's and D's detours alone
  RefineSettings refine;
  refine.iters = 5;
  FallbackReport fr = reclaimWithFallback(d, nets, rs, region, refine, false);
  long long ov = totalOverflow(d.grid, usageFromCharges(d.grid, nets)), w = totalWire2D(nets);
  long long wantOv = fr.tookB ? fr.overflowB : fr.overflowA, wantW = fr.tookB ? fr.wireB : fr.wireA;
  check(ov == wantOv && w == wantW, "the kept state carries the kept path's numbers (overflow " + std::to_string(ov) +
                                        ", wire " + std::to_string(w) + ")");
  check(fr.tookB == fallbackPrefersB(fr.overflowA, fr.wireA, fr.overflowB, fr.wireB) &&
            !fallbackPrefersB(fr.tookB ? fr.overflowB : fr.overflowA, fr.tookB ? fr.wireB : fr.wireA,
                              fr.tookB ? fr.overflowA : fr.overflowB, fr.tookB ? fr.wireA : fr.wireB),
        "the kept path is never worse than the other (A " + std::to_string(fr.overflowA) + "/" + std::to_string(fr.wireA) +
            ", B " + std::to_string(fr.overflowB) + "/" + std::to_string(fr.wireB) + ")");
  check(fr.reclaim.candidates == 2 && fr.tookB && fr.wireB < fr.wireA,
        "here both detours are reclaimable, so B wins on wire");
  bool connected = true;
  for (const RoutedNet &rn : nets)
    connected = connected && segsConnected(rn.segs);
  check(connected, "every net still connects its pins");
}

// pd20route: on pd19route's fallback fixture, where B reaches zero, the
// B-first order keeps B without running A, and its kept state is byte for
// byte the both-paths order's.
void testFallbackBFirst() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  auto run = [&](bool bFirst, FallbackReport &fr) {
    Design d = parse("pd20route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    ReclaimSettings rs;
    rs.ratio = 1.2;
    rs.minExcess = 2;
    RegionSettings region;
    region.enabled = true;
    region.rounds = 20;
    region.expand = 0;
    RefineSettings refine;
    refine.iters = 5;
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, bFirst);
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    return out;
  };
  FallbackReport both, bfirst;
  auto segsBoth = run(false, both);
  auto segsB = run(true, bfirst);
  bool same = segsBoth.size() == segsB.size();
  for (size_t i = 0; same && i < segsBoth.size(); ++i) {
    same = segsBoth[i].size() == segsB[i].size();
    for (size_t k = 0; same && k < segsBoth[i].size(); ++k) {
      const Seg &a = segsBoth[i][k], &c = segsB[i][k];
      same = a.x1 == c.x1 && a.y1 == c.y1 && a.l1 == c.l1 && a.x2 == c.x2 && a.y2 == c.y2 && a.l2 == c.l2;
    }
  }
  check(both.ranA && both.tookB && !bfirst.ranA && bfirst.tookB && bfirst.overflowB == 0,
        "B reaches zero: the B-first order keeps B and never runs A");
  check(same && bfirst.wireB == both.wireB, "and the kept routes are identical to the both-paths order's");
}

// pd21route: on pd19route's fallback fixture A reaches 0 overflow (wire
// 26) and B 0 (wire 22). With threshold 0, A is at the threshold, B runs
// and wins, and the kept routes equal the both-paths order's. With a
// threshold of -1 the call is pd20route's (both paths). A case where A
// ends above the threshold: three nets on a one-track row (residue never
// clears) -- B is never run and A is kept.
void testFallbackAFirstThreshold() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0;
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](long long aMax, FallbackReport &fr) {
    Design d = parse("pd21route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, aMax);
    return totalWire2D(nets);
  };
  FallbackReport both, afirst;
  long long wBoth = run(-1, both), wA = run(0, afirst);
  check(afirst.ranA && afirst.ranB && afirst.tookB && wA == wBoth && afirst.overflowA == 0,
        "A ends at 0 <= threshold 0: B runs and wins, same kept wire as both paths (" + std::to_string(wA) + ")");
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd21route_selftest_row.gr", row);
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i) {
    nets[i].net = &d.nets[i];
    nets[i].segs = {{0, 0, 0, 7, 0, 0}};
  }
  prepareFor2DRefine(d, nets);
  FallbackReport stuck = reclaimWithFallback(d, nets, rs, region, refine, false, false, 10);
  check(stuck.overflowA > 10 && !stuck.ranB && !stuck.tookB,
        "A ends above the threshold (" + std::to_string(stuck.overflowA) + " > 10): B never runs, A kept");
}

// pd22route: the virtual capacity arithmetic, hand-checked: k units less,
// floored at ceil(cap / 2); zero / negative capacity and zero reduction
// unchanged.
void testVirtualCapArithmetic() {
  check(virtualCap(10, 4) == 6 && virtualCap(10, 0) == 10, "cap 10: reduction 4 -> 6, reduction 0 -> 10");
  check(virtualCap(10, 8) == 5 && virtualCap(11, 100) == 6 && virtualCap(1, 5) == 1,
        "never below ceil(cap / 2): 10-8 -> 5, 11-100 -> 6, 1-5 -> 1");
  check(virtualCap(0, 4) == 0 && virtualCap(-3, 2) == -3, "zero or negative capacity unchanged (stays forbidden)");
}

// pd22route: the cost seam. With no reduction vector the cost is the
// inherited one exactly; with one, it is presentDemandCost against the
// virtual capacity, and dearer.
void testVirtualCapCostSeam() {
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t eh = g.hIndex(1, 0, 0), ev = g.vIndex(0, 0, 0);
  u.h[eh] = 1;
  u.v[ev] = 1;
  double h0 = wireCostH(pr, eh, 1), v0 = wireCostV(pr, ev, 1);
  check(h0 == presentDemandCost(1, u.capH[eh], 1, 1, pr.pdIter, pr.pdFactor, pdWl(pr)),
        "no reduction vector: the inherited cost exactly");
  std::vector<int> redH(u.h.size(), 0), redV(u.v.size(), 0);
  redH[eh] = 1;
  redV[ev] = 1;
  pr.vredH = &redH;
  pr.vredV = &redV;
  double h1 = wireCostH(pr, eh, 1), v1 = wireCostV(pr, ev, 1);
  check(h1 == presentDemandCost(1, virtualCap(u.capH[eh], 1), 1, 1, pr.pdIter, pr.pdFactor, pdWl(pr)) && h1 > h0 &&
            v1 > v0,
        "with a reduction: priced against the virtual capacity, and dearer (h " + std::to_string(h0) + " -> " +
            std::to_string(h1) + ")");
  check(wireCostH(pr, g.hIndex(2, 0, 0), 1) == ([&] {
          Prices q(g);
          configureCostModel(q, u, cs);
          return wireCostH(q, g.hIndex(2, 0, 0), 1);
        })(),
        "an edge with reduction 0 costs exactly what it would without the mechanism");
}

// Part 179: the over-capacity cost. overClampDemand caps demand at cap + floor(s x cap) and leaves it alone when
// off or at zero capacity; through the cost seam, every full or over-full edge then costs what a just-full one
// does at s = 0, an edge below the cap is priced exactly as before, and the portfolio key and dump reach it.
void testOverCapacityCost() {
  check(overClampDemand(50, 10, -1) == 50 && overClampDemand(50, 0, 0) == 50 && overClampDemand(50, -3, 0) == 50,
        "over-capacity cost off (slack < 0) or capacity <= 0: demand unchanged");
  check(overClampDemand(50, 10, 0) == 10 && overClampDemand(7, 10, 0) == 7 && overClampDemand(10, 10, 0) == 10,
        "slack 0: demand capped at the capacity, below it unchanged");
  check(overClampDemand(50, 10, 0.25) == 12 && overClampDemand(50, 10, 1) == 20 && overClampDemand(11, 10, 1) == 11,
        "slack 0.25 / 1: capped at cap + floor(s x cap)");
  check(overClampDemand(4000000000LL, 2000000000, 1000) == 4000000000LL,
        "a huge slack on a huge capacity does not overflow the limit");
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t eh = g.hIndex(1, 0, 0), ev = g.vIndex(0, 0, 0);
  const int capH = u.capH[eh], capV = u.capV[ev];
  check(capH > 0 && capV > 0, "the fixture's edges have capacity");
  u.h[eh] = capH;
  const double fullOff = wireCostH(pr, eh, 1);
  u.h[eh] = 5 * capH;
  const double overOff = wireCostH(pr, eh, 1);
  u.v[ev] = capV / 2;
  const double halfOff = wireCostV(pr, ev, 1);
  pr.overSlack = 0;
  const double overOn = wireCostH(pr, eh, 1);
  u.h[eh] = capH;
  check(overOff > fullOff && overOn == fullOff && wireCostH(pr, eh, 1) == fullOff,
        "slack 0: an edge 4 x capacity over costs what a just-full one does (" + std::to_string(overOff) + " -> " +
            std::to_string(overOn) + ")");
  check(wireCostV(pr, ev, 1) == halfOff, "an edge below capacity is priced exactly as without the mechanism");
  RegionSettings rs;
  std::string err;
  check(applyRegionVariant("over=0.5,lagr=0.2", rs, err) && rs.overSlack == 0.5 && rs.lagrange == 0.2,
        "portfolio key over= sets the region stage's slack");
  check(!applyRegionVariant("over=-1", rs, err), "portfolio key over= refuses a negative slack");
}

// pd22route: the zone. One overflowed horizontal edge (10,10)-(11,10) on a
// 20 x 20 grid, radius 2: hot tiles x 8..13, y 8..12. Horizontal edges with
// an end hot: rows 8..12 x left ends 7..13 = 5 x 7 = 35. Vertical edges:
// columns 8..13 x lower ends 7..12 = 6 x 6 = 36. Total 71.
void testVirtualCapZone() {
  std::string gr = "grid 20 20 2\nvertical capacity 0 10\nhorizontal capacity 10 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 0\n0\n";
  Design d = parse("pd22route_selftest_zone.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Usage u(g);
  u.h[g.hIndex(10, 10, 0)] = u.capH[g.hIndex(10, 10, 0)] + 1;
  std::vector<int> redH, redV;
  long long n = virtualCapZone(g, u, 4, 2, redH, redV);
  check(n == 71, "71 edges in the zone by hand count (got " + std::to_string(n) + ")");
  check(redH[g.hIndex(7, 8, 0)] == 4 && redH[g.hIndex(13, 12, 0)] == 4 && redV[g.vIndex(8, 7, 0)] == 4 &&
            redV[g.vIndex(13, 12, 0)] == 4,
        "the zone's corner edges carry the reduction");
  check(redH[g.hIndex(6, 8, 0)] == 0 && redH[g.hIndex(10, 13, 0)] == 0 && redV[g.vIndex(7, 9, 0)] == 0 &&
            redV[g.vIndex(10, 13, 0)] == 0 && redH[g.hIndex(0, 0, 0)] == 0,
        "edges just outside the zone, and far away, do not");
  check(virtualCapZone(g, u, 0, 2, redH, redV) == 0 && redH[g.hIndex(10, 10, 0)] == 0, "reduction 0: no zone");
  Usage clean(g);
  check(virtualCapZone(g, clean, 4, 2, redH, redV) == 0, "nothing overflowed: no zone");
}

// pd22route: the decisive claim on the real search. An 8 x 3 grid, 2D
// capacity 10 everywhere, net amount 2, source (0,1), sink (7,1); row 1
// already carries demand 4. Straight along row 1: 7 edges at cong 6/10,
// cost 7 * (0.989 + 0.078) = 7.47. The detour through row 0: 9 edges at
// cong 0.2, ~8.9 -- so the search goes straight. With row 1 priced 4 units
// lower (capacity 6, cong 1): 7 * 1.989 = 13.9 > 8.9, so it detours.
void testVirtualCapSteersSearch() {
  std::string gr = "grid 8 3 2\nvertical capacity 0 10\nhorizontal capacity 10 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 0\n0\n";
  Design d = parse("pd22route_selftest_steer.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  Prices pr(g);
  Usage u(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  for (int x = 0; x + 1 < g.nx; ++x)
    u.h[g.hIndex(x, 1, 0)] = 4;
  LayerPlan lp(g);
  SearchRegion region;
  std::vector<PathNode> sources = {{0, 1, -1}};
  std::vector<Point> sinks = {{7, 1}};
  auto leavesRow1 = [](const MSMVResult &r) {
    for (const PathNode &p : r.path)
      if (p.y != 1)
        return true;
    return false;
  };
  MSMVResult plain = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(plain.found && !leavesRow1(plain), "without virtual capacity the route goes straight along row 1");
  std::vector<int> redH(u.h.size(), 0), redV(u.v.size(), 0);
  for (int x = 0; x + 1 < g.nx; ++x)
    redH[g.hIndex(x, 1, 0)] = 4;
  pr.vredH = &redH;
  pr.vredV = &redV;
  MSMVResult vc = routeStepMSMV(g, pr, lp, region, sources, sinks, 1, 2);
  check(vc.found && leavesRow1(vc), "with row 1 priced 4 units lower the route leaves it");
}

// pd22route: the region stage wires the zone in. pd21route's one-track row
// (three nets on a capacity-2 row, a residue that never clears): with 1
// track (2 units) and radius 0, the zone is the row's 7 edges; nets stay
// connected; with it off the report says 0.
void testRegionVirtualCapReported() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto run = [&](int tracks) {
    Design d = parse("pd22route_selftest_row.gr", row);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings region;
    region.enabled = true;
    region.rounds = 5;
    region.vcapTracks = tracks;
    region.vcapRadius = 0;
    RegionReport rr = regionNegotiate(d, nets, region, false);
    bool connected = true;
    for (const RoutedNet &rn : nets)
      connected = connected && segsConnected(rn.segs);
    check(connected, "every net still connects its pins (vcap " + std::to_string(tracks) + ")");
    return rr;
  };
  RegionReport on = run(1), off = run(0);
  check(on.vcapEdges == 7 && on.vcapReduction == 2, "1 track, radius 0: the row's 7 edges, 2 units each (got " +
                                                        std::to_string(on.vcapEdges) + ")");
  check(off.vcapEdges == 0 && off.vcapReduction == 0, "off: no zone");
}

// pd22route: the parallel fallback keeps exactly what the sequential order
// keeps -- B taken on pd19route's fixture (routes compared seg by seg),
// and A kept with B cancelled on the one-track row.
void testFallbackParallelMatchesSequential() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0;
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](const char *text, bool fixture, bool parallel, FallbackReport &fr) {
    Design d = parse("pd22route_selftest_parallel.gr", text);
    std::vector<RoutedNet> nets(fixture ? 4 : 3);
    for (size_t i = 0; i < nets.size(); ++i)
      nets[i].net = &d.nets[i];
    if (fixture) {
      nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
      nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
      nets[2].segs = {{10, 3, 0, 15, 3, 0}};
      nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    } else {
      for (RoutedNet &rn : nets)
        rn.segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, fixture ? 0 : 10, parallel);
    std::vector<std::vector<int>> out;
    for (const RoutedNet &rn : nets) {
      out.emplace_back();
      for (const Seg &s : rn.segs)
        out.back().insert(out.back().end(), {s.x1, s.y1, s.l1, s.x2, s.y2, s.l2});
    }
    return out;
  };
  FallbackReport seq, par, seqStuck, parStuck;
  auto a = run(gr, true, false, seq), b = run(gr, true, true, par);
  check(par.parallel && par.ranB && par.tookB == seq.tookB && seq.tookB && a == b && par.wireB == seq.wireB,
        "B reaches zero: the parallel order keeps B, routes identical to the sequential order's");
  auto c = run(row, false, false, seqStuck), e = run(row, false, true, parStuck);
  check(parStuck.parallel && !parStuck.ranB && !parStuck.tookB && c == e && parStuck.overflowA == seqStuck.overflowA &&
            parStuck.overflowA > 10,
        "A above the threshold: B cancelled and discarded, A kept as in the sequential order");
}

void testParseVirtualCap() {
  RegionSettings r;
  std::string err;
  check(parseVirtualCap(nullptr, nullptr, r, err) && r.vcapTracks == 0 && r.vcapRadius == 5, "unset: off, radius 5");
  check(parseVirtualCap("2", "8", r, err) && r.vcapTracks == 2 && r.vcapRadius == 8, "2 tracks, radius 8");
  RegionSettings s;
  check(!parseVirtualCap("-1", nullptr, s, err) && !parseVirtualCap("x", nullptr, s, err) &&
            !parseVirtualCap(nullptr, "1001", s, err) && s.vcapTracks == 0 && s.vcapRadius == 5,
        "negative, non-numeric or out-of-range values rejected, settings untouched");
}

// pd23route: with PD23ROUTE_VCAP_B=0 the reclaim path runs without virtual
// capacity -- its region report shows no zone and its result (wire) is
// exactly the no-virtual-capacity run's B -- while path A still gets the
// zone. pd19route's fixture, both paths (row 3 overflows at A's start).
void testVirtualCapPathAOnly() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](int tracks, bool vcapB) {
    Design d = parse("pd23route_selftest_fallback.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    RegionSettings region;
    region.enabled = true;
    region.rounds = 20;
    region.expand = 0;
    region.vcapTracks = tracks;
    region.vcapRadius = 0;
    return reclaimWithFallback(d, nets, rs, region, refine, false, false, -1, false, vcapB);
  };
  FallbackReport off = run(0, true), aOnly = run(1, false), both = run(1, true);
  check(aOnly.regionA.vcapEdges > 0 && aOnly.regionB.vcapEdges == 0,
        "PD23ROUTE_VCAP_B=0: path A gets the zone (" + std::to_string(aOnly.regionA.vcapEdges) +
            " edges), path B none");
  check(aOnly.wireB == off.wireB && aOnly.overflowB == off.overflowB,
        "and path B's result is exactly the no-virtual-capacity run's (wire " + std::to_string(off.wireB) + ")");
  check(both.regionA.vcapEdges == aOnly.regionA.vcapEdges, "default (1): pd22route's behavior, path A's zone unchanged");
}

// pd33route: the native ISPD 2024 path. A 4 x 3 GCell, 3-layer fixture
// (M1 not routable, M2 horizontal, M3 vertical; M2 1.5 tracks at (1,1), M3
// 0 at (2,0)) -- the g24_bridge self test's.
void testG24DesignMatchesBridge() {
  const char *cap = "3 4 3\n0.5 2 1 1 1\n100 100 100\n100 100\n"
                    "M1 0 0\n9 9 9 9\n9 9 9 9\n9 9 9 9\n"
                    "M2 0 0\n2 2 2 2\n2 1.5 2 2\n2 2 2 2\n"
                    "M3 1 0\n3 3 0 3\n3 3 3 3\n3 3 3 3\n";
  const char *net = "netA\n(\n[(0, 0, 0), (0, 0, 1)]\n[(0, 3, 2)]\n)\nnetB\n(\n[(0, 2, 2)]\n[(1, 2, 2)]\n)\n";
  G24Input in;
  std::string err;
  std::istringstream ci(cap), ni(net);
  check(g24::readResources(ci, in.res, err) && g24::readNets(ni, in.nets, err) &&
            g24::checkAccessPoints(in.res, in.nets, err),
        "g24: fixture reads");
  // 1. The native Design equals what the Part 102 bridge's .gr reads into:
  // every edge capacity, nominal capacity, pin, name and id.
  Design nat = g24::toDesign(in.res, in.nets);
  std::string path = "pd33route_selftest.gr";
  {
    std::ofstream o(path);
    g24::writeGr(nat, o);
  }
  Design br = readGr(path);
  std::remove(path.c_str());
  bool same = nat.grid.hcap == br.grid.hcap && nat.grid.vcap == br.grid.vcap &&
              nat.grid.hCapLayer == br.grid.hCapLayer && nat.grid.vCapLayer == br.grid.vCapLayer &&
              nat.grid.tileW == br.grid.tileW && nat.nets.size() == br.nets.size();
  for (size_t i = 0; same && i < nat.nets.size(); ++i) {
    same = nat.nets[i].name == br.nets[i].name && nat.nets[i].id == br.nets[i].id &&
           nat.nets[i].pins.size() == br.nets[i].pins.size();
    for (size_t k = 0; same && k < nat.nets[i].pins.size(); ++k)
      same = nat.nets[i].pins[k].x == br.nets[i].pins[k].x && nat.nets[i].pins[k].y == br.nets[i].pins[k].y &&
             nat.nets[i].pins[k].layer == br.nets[i].pins[k].layer;
  }
  check(same, "g24: the native Design equals the bridge's .gr Design, edge for edge and pin for pin");
  check(nat.grid.hcap[nat.grid.hIndex(1, 1, 1)] == 3 && nat.grid.vcap[nat.grid.vIndex(2, 0, 2)] == 0 &&
            nat.grid.hCapLayer[1] == 4 && nat.grid.vCapLayer[2] == 6 && nat.grid.hCapLayer[0] == 0,
        "g24: capacities in half tracks (1.5 tracks -> 3, defaults 4 and 6, M1 0)");
  // 2. The solution writer: netA routed M1 -> M2 at (0,0), M2 to (3,0), up
  // to M3, M3 to (3,2) -- its second pin is on M1 at (3,2), so the writer
  // adds a via M1-M3 there; netB (one GCell) is not routed and gets a via
  // stack M1-M2.
  std::vector<RoutedNet> nets(2);
  nets[0].net = &nat.nets[0];
  nets[0].segs = {{0, 0, 0, 0, 0, 1}, {0, 0, 1, 3, 0, 1}, {3, 0, 1, 3, 0, 2}, {3, 0, 2, 3, 2, 2}};
  nets[1].net = &nat.nets[1];
  std::string sol = "pd33route_selftest.g24sol";
  g24::SolutionReport sr = writeG24Solution(sol, in, nat.grid, nets);
  std::ifstream f(sol);
  std::stringstream text;
  text << f.rdbuf();
  std::remove(sol.c_str());
  check(text.str() == "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 1 3 0 2\n3 0 2 3 2 2\n3 2 0 3 2 2\n)\n"
                      "netB\n(\n2 2 0 2 2 1\n)\n" &&
            sr.pinFixes == 1 && sr.unroutedNets == 1 && sr.vias == 4 && sr.wires == 2,
        "g24: solution in GCell segments, uncovered pin given a via, single-GCell net given a stack");
}

// pd33route: via demand. A 3 x 3 grid, 5 layers: 0 none, 1 horizontal (4
// half tracks), 2 vertical, 3 horizontal, 4 vertical (40 each).
const char *kViaDemandGr = "grid 3 3 5\nvertical capacity 0 0 40 0 40\nhorizontal capacity 0 4 0 40 0\n"
                           "minimum width 1 1 1 1 1\nminimum spacing 1 1 1 1 1\nvia spacing 0 0 0 0 0\n"
                           "0 0 10 10\nnum net 1\nnD 0 2 1\n5 15 1\n25 15 1\n0\n";

// The evaluator's rule, by hand: a stack M1..M4 at interior (1,1) whose net
// has a layer-2 (vertical) wire through (1,1) charges layer 1 only (layer 0
// is never charged, layer 2 is covered, layer 3 is the stack's top): 1 on
// each of the two layer-1 edges at (1,1). A second via over the same layers
// there adds nothing (once per net). A via M2-M3 at (0,0), on the grid
// edge, charges layer 1's one edge there 2.
void testViaDemandFollowsTheEvaluatorRule() {
  Design d = parse("pd33route_selftest_vd.gr", kViaDemandGr);
  const RoutingGrid &g = d.grid;
  ViaDemandModel saved = gViaDemand;
  gViaDemand.on = true;
  gViaDemand.dir = {-1, 0, 1, 0, 1};
  RealUsage ru(g);
  NetCharge c;
  chargeViaDemand(g, ru, {{1, 1, 0, 1, 1, 3}, {1, 0, 2, 1, 2, 2}, {1, 1, 1, 1, 1, 2}, {0, 0, 1, 0, 0, 2}}, &c);
  long long hTotal = 0, vTotal = 0;
  for (long long u : ru.h)
    hTotal += u;
  for (long long u : ru.v)
    vTotal += u;
  check(ru.h[g.hIndex(0, 1, 1)] == 1 && ru.h[g.hIndex(1, 1, 1)] == 1 && ru.h[g.hIndex(0, 0, 1)] == 2 &&
            hTotal == 4 && vTotal == 0,
        "via demand: interior 1 + 1, grid edge 2, covered and repeated layers free, layer 0 free");
  RealViaUsage rvu(g);
  undoCharge(ru, rvu, c);
  bool zero = true;
  for (long long u : ru.h)
    zero = zero && u == 0;
  check(zero, "via demand: logged in the net's charge, so undo removes it exactly");
  gViaDemand.on = false;
  RealUsage ru2(g);
  chargeViaDemand(g, ru2, {{1, 1, 0, 1, 1, 3}}, nullptr);
  bool none = true;
  for (long long u : ru2.h)
    none = none && u == 0;
  check(none, "via demand off: nothing charged");
  gViaDemand = saved;
}

// Layer assignment sees the stacks' demand. A two-pin net, pins on layer 0
// at (0,1) and (2,1); layer 1 is full on both edges. Blind to via demand,
// layer 3 looks free (4 more vias, no overflow). Seeing it, layer 3's pin
// stacks pass layer 1 at both ends and overflow it exactly as much as a
// layer-1 wire would (2 + 2 half tracks), so layer 1 wins on vias -- what
// the evaluator would score.
void testLayerAssignmentPricesViaDemand() {
  Design d = parse("pd33route_selftest_vd2.gr", kViaDemandGr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  NetTree legs, t;
  buildNetTree({{0, 1, 0, 2, 1, 0}}, legs);
  splitToUnitEdges(legs, t);
  ViaDemandModel saved = gViaDemand;
  gViaDemand.dir = {-1, 0, 1, 0, 1};
  auto run = [&](bool on, std::vector<int> &edgeLayer) {
    gViaDemand.on = on;
    RealUsage ru(g);
    RealViaUsage rvu(g);
    ru.h[g.hIndex(0, 1, 1)] = 4;
    ru.h[g.hIndex(1, 1, 1)] = 4;
    std::vector<std::pair<int, int>> pr(t.nodes.size(), {-1, -1});
    for (size_t u = 0; u < t.nodes.size(); ++u)
      if (t.nodes[u].x == 0 || t.nodes[u].x == 2)
        pr[u] = {0, 0};
    int root = 0;
    std::vector<std::pair<int, int>> ranges;
    return layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, root, ranges, 1.0, nullptr, nullptr, nullptr,
                         true, &pr);
  };
  std::vector<int> blind, aware;
  check(run(false, blind) && run(true, aware), "via demand DP: both find assignments (sanity)");
  bool blind3 = blind.size() == 2 && blind[0] == 3 && blind[1] == 3;
  bool aware1 = aware.size() == 2 && aware[0] == 1 && aware[1] == 1;
  check(blind3, "blind to via demand: layer 3 (the stacks' layer-1 overflow unseen)");
  check(aware1, "pricing via demand: layer 1 (same overflow, 4 fewer vias)");
  gViaDemand = saved;
}

// pd33route: the 2024 score's loss, by hand: 2 tracks of capacity, demand 4
// half tracks (2 tracks) -> e^0; 6 -> e^0.5; capacity 0 with demand 2 ->
// e^1.5; capacity 0, demand 0 -> 0.
void testG24LossMatchesTheEvaluator() {
  check(std::fabs(g24Loss(4, 2.0) - 1.0) < 1e-12 && std::fabs(g24Loss(6, 2.0) - std::exp(0.5)) < 1e-12 &&
            std::fabs(g24Loss(2, 0.0) - std::exp(1.5)) < 1e-12 && g24Loss(0, 0.0) == 0.0,
        "score loss: e^0, e^0.5, e^1.5, 0 (g24_verify's rule)");
}

// The score cost trades overflow for vias. 7 x 3 grid, pins on layer 0 at
// (1,1) and (5,1); layer 1 (2 tracks) is full on edge (3,1)-(4,1) only, away
// from the pin stacks. NTHU's cost treats that overflow as forbidden and
// leaves layer 1 somewhere on the way; the score cost sees one track over
// a full GCell at 5 (e^0.5 - 1) = 3.2, cheaper than any detour up a layer
// (at least 2 more vias, 8), and stays on layer 1 end to end.
void testScoreCostAcceptsCheapOverflow() {
  const char *gr = "grid 7 3 5\nvertical capacity 0 0 40 0 40\nhorizontal capacity 0 4 0 40 0\n"
                   "minimum width 1 1 1 1 1\nminimum spacing 1 1 1 1 1\nvia spacing 0 0 0 0 0\n"
                   "0 0 10 10\nnum net 1\nnS 0 2 1\n15 15 1\n55 15 1\n0\n";
  Design d = parse("pd33route_selftest_sc.gr", gr);
  const RoutingGrid &g = d.grid;
  RealLayerPlan lp(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  NetTree legs, t;
  buildNetTree({{1, 1, 0, 5, 1, 0}}, legs);
  splitToUnitEdges(legs, t);
  ViaDemandModel savedV = gViaDemand;
  ScoreModel savedS = gScore;
  gViaDemand.on = true;
  gViaDemand.dir = {-1, 0, 1, 0, 1};
  gScore.w = {5, 5, 5, 5, 5};
  gScore.unitVia = 4;
  gScore.capH.assign(g.hCount(), 0.0);
  gScore.capV.assign(g.vCount(), 0.0);
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x) {
      gScore.capH[g.hIndex(x, y, 1)] = 2;
      gScore.capH[g.hIndex(x, y, 3)] = 20;
    }
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      gScore.capV[g.vIndex(x, y, 2)] = 20;
      gScore.capV[g.vIndex(x, y, 4)] = 20;
    }
  auto run = [&](bool score, std::vector<int> &edgeLayer) {
    gScore.on = score;
    RealUsage ru(g);
    RealViaUsage rvu(g);
    ru.h[g.hIndex(3, 1, 1)] = 4;
    std::vector<std::pair<int, int>> pr(t.nodes.size(), {-1, -1});
    for (size_t u = 0; u < t.nodes.size(); ++u)
      if (t.nodes[u].x == 1 || t.nodes[u].x == 5)
        pr[u] = {0, 0};
    int root = 0;
    std::vector<std::pair<int, int>> ranges;
    return layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, root, ranges, 1.0, nullptr, nullptr, nullptr,
                         true, &pr);
  };
  std::vector<int> nthu, score;
  check(run(false, nthu) && run(true, score), "score cost DP: both find assignments (sanity)");
  bool nthuLeaves = false, scoreStays = score.size() == 4;
  for (int l : nthu)
    nthuLeaves = nthuLeaves || l != 1;
  for (int l : score)
    scoreStays = scoreStays && l == 1;
  check(nthuLeaves, "NTHU cost: leaves layer 1 to avoid the one track of overflow");
  check(scoreStays, "score cost: stays on layer 1 (3.2 of overflow beats 8+ of vias)");
  gViaDemand = savedV;
  gScore = savedS;
}

// pd33route (Part 104): a layer blocked in most GCells is still a routing
// layer. M2 (horizontal) of a 4 x 3 fixture has capacity 0 in 8 of its 9
// edges' GCells and 2 tracks in one: its nominal capacity must be that open
// value (4 half tracks), not the most common 0 -- RealLayerPlan reads a
// layer's direction from it -- and the blocked edges keep 0.
void testG24MostlyBlockedLayerStaysRoutable() {
  const char *cap = "3 4 3\n0.5 2 1 1 1\n100 100 100\n100 100\n"
                    "M1 0 0\n9 9 9 9\n9 9 9 9\n9 9 9 9\n"
                    "M2 0 0\n0 0 0 0\n0 2 0 0\n0 0 0 0\n"
                    "M3 1 0\n3 3 3 3\n3 3 3 3\n3 3 3 3\n";
  const char *net = "netA\n(\n[(0, 0, 0)]\n[(0, 3, 2)]\n)\n";
  g24::Resources r;
  std::vector<g24::Net> nets;
  std::string err;
  std::istringstream ci(cap), ni(net);
  check(g24::readResources(ci, r, err) && g24::readNets(ni, nets, err), "blocked layer: fixture reads");
  Design d = g24::toDesign(r, nets);
  RealLayerPlan lp(d.grid);
  bool m2 = std::find(lp.hLayers.begin(), lp.hLayers.end(), 1) != lp.hLayers.end();
  check(d.grid.hCapLayer[1] == 4 && m2 && d.grid.hcap[d.grid.hIndex(1, 1, 1)] == 4 &&
            d.grid.hcap[d.grid.hIndex(0, 0, 1)] == 0,
        "blocked layer: nominal = the open value, M2 is a horizontal layer, blocked edges stay 0");
}

// pd33route (Part 105): InstantGR's access-point rule. netA: pin 0 has
// access points (0,0) and (2,1), pin 1 one at (2,2): (2,1) shares x with
// (2,2) -- one differing coordinate instead of two -- so it is chosen and
// moved first. netB (5 pins, > 4): each pin nearest the centre of the box
// over all access points; pin 0's (0,0) / (3,3) / (2,2) with box (0..4,
// 0..4), centre (2,2): (2,2). netC: ties keep the first combination.
void testG24AccessPointChoice() {
  std::vector<g24::Net> nets(3);
  nets[0].name = "netA";
  nets[0].pins = {{{0, 0, 0}, {2, 1, 0}}, {{2, 2, 0}}};
  nets[1].name = "netB";
  nets[1].pins = {{{0, 0, 0}, {3, 3, 0}, {2, 2, 0}}, {{0, 4, 0}}, {{4, 0, 0}}, {{4, 4, 0}}, {{0, 0, 0}}};
  nets[2].name = "netC";
  nets[2].pins = {{{1, 1, 0}, {1, 1, 1}}, {{3, 3, 0}}};
  std::vector<g24::Net> c = g24::chooseAccessPoints(nets);
  check(c[0].pins[0][0].x == 2 && c[0].pins[0][0].y == 1 && c[0].pins[0].size() == 2,
        "access points: an aligned access point is chosen and moved first");
  check(c[1].pins[0][0].x == 2 && c[1].pins[0][0].y == 2, "access points: > 4 pins, the one nearest the centre");
  check(c[2].pins[0][0].z == 0, "access points: a tie keeps the first");
}

// pd34route: the score-aware 2D cost.
// By hand: C = 10 tracks over n = 2 layers, w = 5, D = 16 half tracks (8
// tracks), one net (a = 2): M = 2 x 5 (e^(0.5 (9 - 10)/2) - e^(0.5 (8 - 10)/2))
// = 10 (e^-0.25 - e^-0.5) = 1.72270...; cost = wl 5 + hist x M. No routable
// layer: w (e^(0.75 a) - 0) at D = 0.
void testScore2DCostByHand() {
  Score2D saved = gScore2D;
  gScore2D.hist = true;
  double m = score2dMarginal(10.0, 2, 5.0, 16, 2);
  double want = 10.0 * (std::exp(-0.25) - std::exp(-0.5));
  check(std::fabs(m - want) < 1e-12, "score 2D: marginal 10 (e^-0.25 - e^-0.5)");
  check(std::fabs(score2dCost(5.0, 10.0, 2, 5.0, 16, 2, 3) - (5.0 + 3 * want)) < 1e-12,
        "score 2D: cost = wl + hist x marginal");
  gScore2D.hist = false;
  check(std::fabs(score2dCost(5.0, 10.0, 2, 5.0, 16, 2, 3) - (5.0 + want)) < 1e-12,
        "score 2D: PD34ROUTE_2D_HIST=0 ignores hist");
  check(std::fabs(score2dMarginal(0.0, 0, 5.0, 0, 2) - 5.0 * std::exp(1.5)) < 1e-9,
        "score 2D: no routable layer, the zero-capacity rule");
  gScore2D = saved;
}

// The decision the score cost changes. A straight 4-tile 2D run with one
// edge exactly full (one layer, capacity 1 track, D = 2 half tracks), versus
// a 2-tile-longer detour on empty edges. Present-demand late in the
// schedule (iteration 40) prices the full edge far above two tiles and
// detours; the score prices one track of overflow at 5 (e^0.5 - 1) = 3.24
// against two more tiles at wl 5 each, and stays straight.
void testScore2DPrefersCheapOverflowToDetour() {
  Score2D saved = gScore2D;
  gScore2D.hist = true;
  const double wl = 5.0;
  // present demand, iteration 40, hist 1, raw capacity 2 (one track)
  double f = pdFactorForIter(40);
  double pdFull = presentDemandCost(2, 2, 2, 1, 40, f, f), pdEmpty = presentDemandCost(0, 2, 2, 1, 40, f, f);
  double pdStraight = 3 * pdEmpty + pdFull, pdDetour = 6 * pdEmpty;
  double scFull = score2dCost(wl, 1.0, 1, 5.0, 2, 2, 1), scEmpty = score2dCost(wl, 1.0, 1, 5.0, 0, 2, 1);
  double scStraight = 3 * scEmpty + scFull, scDetour = 6 * scEmpty;
  check(pdDetour < pdStraight, "present demand (iteration 40): the detour around a full edge is cheaper");
  check(scStraight < scDetour, "score 2D: one track of overflow (3.24) beats a 2-tile detour (10)");
  gScore2D = saved;
}

// The seam: with the score cost on, wireCostH/V and the A* scale read it;
// off, present demand exactly as before.
void testScore2DSeam() {
  const char *gr = "grid 4 3 2\nvertical capacity 0 4\nhorizontal capacity 4 0\nminimum width 1 1\n"
                   "minimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 0\n0\n";
  Design d = parse("pd34route_selftest_seam.gr", gr);
  const RoutingGrid &g = d.grid;
  Usage u(g);
  Prices pr(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  size_t e = g.hIndex(1, 1, 0);
  u.h[e] = 2;
  double off = wireCostH(pr, e, 2);
  Score2D saved = gScore2D;
  gScore2D.on = true;
  gScore2D.hist = true;
  gScore2D.wlH.assign(h2dCount(g), 7.0);
  gScore2D.wlV.assign(v2dCount(g), 7.0);
  gScore2D.capH.assign(h2dCount(g), 2.0);
  gScore2D.capV.assign(v2dCount(g), 2.0);
  gScore2D.nH.assign(h2dCount(g), 1);
  gScore2D.nV.assign(v2dCount(g), 1);
  gScore2D.minWl = 7.0;
  double on = wireCostH(pr, e, 2);
  check(std::fabs(on - score2dCost(7.0, 2.0, 1, gScore2D.w, 2, 2, 1)) < 1e-12 && heuristicScale(pr) == 7.0,
        "score 2D seam: wireCostH reads the score cost, A* scale = smallest wl");
  gScore2D = saved;
  check(wireCostH(pr, e, 2) == off && off == presentDemandCost(2, 4, 2, 1, 1, pdFactorForIter(1), pdWl(pr)),
        "score 2D seam: off, present demand exactly");
}

// pd35route: the joint 3D pattern reroute. A 6 x 4 grid, 4 layers: 0 none
// (pins), 1 horizontal, 2 vertical, 3 horizontal, capacity 2 tracks
// everywhere (raw 4). Score model: weight 5, unitVia 4, pitch 100,
// unitLength 0.05 (5 per GCell).
struct P3DFixture {
  Design d;
  ViaDemandModel savedV;
  ScoreModel savedS;
  P3DFixture(const char *name, const char *pins) {
    std::string gr = std::string("grid 6 4 4\nvertical capacity 0 0 4 0\nhorizontal capacity 0 4 0 4\n"
                                 "minimum width 1 1 1 1\nminimum spacing 1 1 1 1\nvia spacing 0 0 0 0\n"
                                 "0 0 10 10\nnum net 1\nnP 0 2 1\n") +
                     pins + "0\n";
    d = parse(name, gr.c_str());
    savedV = gViaDemand;
    savedS = gScore;
    gViaDemand.on = true;
    gViaDemand.dir = {-1, 0, 1, 0};
    gScore.on = true;
    gScore.w = {5, 5, 5, 5};
    gScore.unitVia = 4;
    gScore.unitLength = 0.05;
    gScore.xPitch.assign(5, 100);
    gScore.yPitch.assign(3, 100);
    gScore.capH.assign(d.grid.hCount(), 2.0);
    gScore.capV.assign(d.grid.vCount(), 2.0);
  }
  ~P3DFixture() {
    gViaDemand = savedV;
    gScore = savedS;
  }
};

// Charges a route the way layer assignment's commit does: wires, vias,
// via demand, all logged.
NetCharge p3dCharge(const RoutingGrid &g, RealUsage &ru, RealViaUsage &rvu, const std::vector<Seg> &segs) {
  NetCharge c;
  for (const Seg &sg : segs) {
    if (sg.l1 == sg.l2) {
      if (sg.y1 == sg.y2)
        for (int x = std::min(sg.x1, sg.x2); x < std::max(sg.x1, sg.x2); ++x) {
          ru.h[g.hIndex(x, sg.y1, sg.l1)] += 2;
          c.ruH.push_back({static_cast<uint32_t>(g.hIndex(x, sg.y1, sg.l1)), 2});
        }
      else
        for (int y = std::min(sg.y1, sg.y2); y < std::max(sg.y1, sg.y2); ++y) {
          ru.v[g.vIndex(sg.x1, y, sg.l1)] += 2;
          c.ruV.push_back({static_cast<uint32_t>(g.vIndex(sg.x1, y, sg.l1)), 2});
        }
    } else {
      for (int l = std::min(sg.l1, sg.l2); l < std::max(sg.l1, sg.l2); ++l) {
        rvu.usage[viaIndex(g, sg.x1, sg.y1, l)] += 1;
        c.rvuIdx.push_back(static_cast<uint32_t>(viaIndex(g, sg.x1, sg.y1, l)));
      }
    }
  }
  chargeViaDemand(g, ru, segs, &c);
  return c;
}

// 1. Pins on layer 0 at (0,0) and (4,3); routed horizontal-first along row
// 0 (layer 1), then up column 4 (layer 2). Row 0 is packed on both
// horizontal layers (6 tracks over); row 3 is empty. Layer assignment
// alone cannot leave row 0; the pattern pass flips the L to vertical-first
// (row 3), strictly lowering the score, and leaves usage exactly equal to
// the background plus the kept route's charge.
void testPattern3DFlipsOutOfACongestedRow() {
  P3DFixture f("pd35route_selftest_p1.gr", "5 5 1\n45 35 1\n");
  const RoutingGrid &g = f.d.grid;
  RealUsage ru(g);
  RealViaUsage rvu(g);
  for (int x = 0; x < 5; ++x) {
    ru.h[g.hIndex(x, 0, 1)] = 16;
    ru.h[g.hIndex(x, 0, 3)] = 16;
  }
  RealUsage background = ru;
  RoutedNet rn;
  rn.net = &f.d.nets[0];
  rn.segs = {{0, 0, 0, 0, 0, 1}, {0, 0, 1, 4, 0, 1}, {4, 0, 1, 4, 0, 2}, {4, 0, 2, 4, 3, 2}, {4, 3, 0, 4, 3, 2}};
  NetCharge c = p3dCharge(g, ru, rvu, rn.segs);
  RealLayerPlan lp(g);
  double gain = 0;
  bool ok = pattern3DNet(g, ru, rvu, lp, rn, c, gain);
  bool row3 = false, row0 = false;
  for (const Seg &sg : rn.segs)
    if (sg.l1 == sg.l2 && sg.y1 == sg.y2) {
      row3 = row3 || sg.y1 == 3;
      row0 = row0 || sg.y1 == 0;
    }
  check(ok && gain > 0 && row3 && !row0, "pattern 3D: the L flips out of the congested row, score falls");
  undoCharge(ru, rvu, c);
  check(ru.h == background.h && ru.v == background.v, "pattern 3D: usage = background + the kept route's charge");
  bool pinA = false, pinB = false;
  for (const Seg &sg : rn.segs)
    if (sg.l1 != sg.l2 && std::min(sg.l1, sg.l2) == 0) {
      pinA = pinA || (sg.x1 == 0 && sg.y1 == 0);
      pinB = pinB || (sg.x1 == 4 && sg.y1 == 3);
    }
  check(pinA && pinB, "pattern 3D: both pins' stacks reach layer 0");
}

// 2. The same net on an empty grid: either L costs the same, so the
// current route stays (a strict gain is required).
void testPattern3DKeepsAnEquallyGoodRoute() {
  P3DFixture f("pd35route_selftest_p2.gr", "5 5 1\n45 35 1\n");
  const RoutingGrid &g = f.d.grid;
  RealUsage ru(g);
  RealViaUsage rvu(g);
  RoutedNet rn;
  rn.net = &f.d.nets[0];
  rn.segs = {{0, 0, 0, 0, 0, 1}, {0, 0, 1, 4, 0, 1}, {4, 0, 1, 4, 0, 2}, {4, 0, 2, 4, 3, 2}, {4, 3, 0, 4, 3, 2}};
  std::vector<Seg> before = rn.segs;
  NetCharge c = p3dCharge(g, ru, rvu, rn.segs);
  RealLayerPlan lp(g);
  double gain = 0;
  bool ok = pattern3DNet(g, ru, rvu, lp, rn, c, gain);
  check(!ok && gain == 0 && rn.segs.size() == before.size(), "pattern 3D: an equally good route is kept");
}

// 3. Pins at (0,1) and (4,1), aligned, routed around a detour (up to row 3,
// across, back down): the straight run on row 1 replaces it.
void testPattern3DRemovesADetour() {
  P3DFixture f("pd35route_selftest_p3.gr", "5 15 1\n45 15 1\n");
  const RoutingGrid &g = f.d.grid;
  RealUsage ru(g);
  RealViaUsage rvu(g);
  RoutedNet rn;
  rn.net = &f.d.nets[0];
  rn.segs = {{0, 1, 0, 0, 1, 2}, {0, 1, 2, 0, 3, 2}, {0, 3, 2, 0, 3, 3}, {0, 3, 3, 4, 3, 3},
             {4, 3, 2, 4, 3, 3}, {4, 1, 2, 4, 3, 2}, {4, 1, 0, 4, 1, 2}};
  NetCharge c = p3dCharge(g, ru, rvu, rn.segs);
  RealLayerPlan lp(g);
  double gain = 0;
  bool ok = pattern3DNet(g, ru, rvu, lp, rn, c, gain);
  int wireTiles = 0;
  bool straight = true;
  for (const Seg &sg : rn.segs)
    if (sg.l1 == sg.l2) {
      wireTiles += std::abs(sg.x2 - sg.x1) + std::abs(sg.y2 - sg.y1);
      straight = straight && sg.y1 == 1 && sg.y2 == 1;
    }
  check(ok && straight && wireTiles == 4, "pattern 3D: the detour (8 tiles) becomes the straight run (4)");
}

// pd35route, the 2008 cost: the same fixtures, priced by overflow against
// the .gr capacities (4 raw per layer) and 1 per tile and per via.
void testPattern3D2008FlipsToRemoveOverflow() {
  P3DFixture f("pd35route_selftest_p4.gr", "5 5 1\n45 35 1\n");
  Pattern3DSettings saved = gPattern3D;
  gPattern3D.mode2008 = true;
  gViaDemand.on = false;
  const RoutingGrid &g = f.d.grid;
  RealUsage ru(g);
  RealViaUsage rvu(g);
  for (int x = 0; x < 5; ++x) {
    ru.h[g.hIndex(x, 0, 1)] = 4; // full: one more net overflows by 2
    ru.h[g.hIndex(x, 0, 3)] = 4;
  }
  RoutedNet rn;
  rn.net = &f.d.nets[0];
  rn.segs = {{0, 0, 0, 0, 0, 1}, {0, 0, 1, 4, 0, 1}, {4, 0, 1, 4, 0, 2}, {4, 0, 2, 4, 3, 2}, {4, 3, 0, 4, 3, 2}};
  NetCharge c = p3dCharge(g, ru, rvu, rn.segs);
  long long overBefore = 0, overAfter = 0;
  for (size_t i = 0; i < ru.h.size(); ++i)
    overBefore += std::max(0LL, static_cast<long long>(ru.h[i]) - g.hcap[i]);
  RealLayerPlan lp(g);
  double gain = 0;
  bool ok = pattern3DNet(g, ru, rvu, lp, rn, c, gain);
  for (size_t i = 0; i < ru.h.size(); ++i)
    overAfter += std::max(0LL, static_cast<long long>(ru.h[i]) - g.hcap[i]);
  check(ok && overBefore == 8 && overAfter == 0, "pattern 3D, 2008 cost: the flip removes the row's overflow (8 -> 0)");
  gPattern3D = saved;
}

void testPattern3D2008RemovesADetour() {
  P3DFixture f("pd35route_selftest_p5.gr", "5 15 1\n45 15 1\n");
  Pattern3DSettings saved = gPattern3D;
  gPattern3D.mode2008 = true;
  gViaDemand.on = false;
  const RoutingGrid &g = f.d.grid;
  RealUsage ru(g);
  RealViaUsage rvu(g);
  RoutedNet rn;
  rn.net = &f.d.nets[0];
  rn.segs = {{0, 1, 0, 0, 1, 2}, {0, 1, 2, 0, 3, 2}, {0, 3, 2, 0, 3, 3}, {0, 3, 3, 4, 3, 3},
             {4, 3, 2, 4, 3, 3}, {4, 1, 2, 4, 3, 2}, {4, 1, 0, 4, 1, 2}};
  NetCharge c = p3dCharge(g, ru, rvu, rn.segs);
  RealLayerPlan lp(g);
  double gain = 0;
  bool ok = pattern3DNet(g, ru, rvu, lp, rn, c, gain);
  int tiles = 0, vias = 0;
  for (const Seg &sg : rn.segs) {
    if (sg.l1 == sg.l2)
      tiles += std::abs(sg.x2 - sg.x1) + std::abs(sg.y2 - sg.y1);
    else
      vias += std::abs(sg.l2 - sg.l1);
  }
  // before: 8 tiles + 2 + 1 + 1 + 2 = 6 vias = 14; after: a straight run on
  // layer 1 with a 0-1 via at each end: 4 + 2 = 6.
  check(ok && tiles == 4 && vias == 2 && std::fabs(gain - 8.0) < 1e-9,
        "pattern 3D, 2008 cost: 8 tiles + 6 vias -> 4 tiles + 2 vias (gain 8)");
  gPattern3D = saved;
}

// pd35route: OpStacks, the sparse LIFO history stacks: pushes and pops per
// index in last-in first-out order, sizes per index, nothing stored for an
// index never pushed, and an emptied index forgotten.
void testOpStacksIsASparseLifo() {
  OpStacks st;
  check(st.empty(7) && st.size(7) == 0 && st.m.empty(), "OpStacks: an untouched index is empty, nothing stored");
  st.push(7, {1.5, false});
  st.push(7, {2.0, true});
  st.push(3, {0.25, false});
  check(st.size(7) == 2 && st.size(3) == 1 && st.m.size() == 2, "OpStacks: sizes per index, two indices stored");
  HistOp a = st.pop(7), b = st.pop(7);
  check(a.amountOrFactor == 2.0 && a.multiplicative && b.amountOrFactor == 1.5 && !b.multiplicative &&
            st.empty(7) && st.m.size() == 1,
        "OpStacks: LIFO order; an emptied index is erased");
}

// pd35route (memory): NetDelta reproduces a full copy. Nets 0..2; mark;
// change net 1 twice and net 2 once; restore -> exactly the marked state.
// mark() after the change makes the changed state the snapshot.
void testNetDeltaMatchesAFullCopy() {
  std::vector<RoutedNet> nets(3);
  for (int i = 0; i < 3; ++i)
    nets[i].segs = {{i, 0, 0, i + 1, 0, 0}};
  std::vector<RoutedNet> copy = nets;
  NetDelta d;
  d.save(nets, 1);
  nets[1].segs.push_back({5, 5, 0, 6, 5, 0});
  d.save(nets, 1); // a second save keeps the first original
  nets[1].escalated = true;
  d.save(nets, 2);
  nets[2].boxMargin = 9;
  bool sizeOk = d.orig.size() == 2;
  d.restore(nets);
  bool same = nets.size() == copy.size();
  for (size_t i = 0; same && i < nets.size(); ++i)
    same = nets[i].segs.size() == copy[i].segs.size() && nets[i].escalated == copy[i].escalated &&
           nets[i].boxMargin == copy[i].boxMargin;
  check(sizeOk && same && d.orig.empty(), "NetDelta: restore returns exactly the marked state");
  d.save(nets, 0);
  nets[0].giant = true;
  d.mark();
  d.restore(nets);
  check(nets[0].giant, "NetDelta: mark() makes the current state the snapshot");
}

// pd35route (memory): NetSpill round trip -- pointers (as indices), flags,
// margin and all four vectors.
// pd38route: a ChargeRecord built over dirty memory, by either constructor,
// has every byte set -- the one after the bool is zero -- so writing whole
// records (NetSpill) writes no uninitialised bytes.
void testChargeRecordHasNoUninitialisedBytes() {
  alignas(ChargeRecord) unsigned char buf[sizeof(ChargeRecord)];
  std::memset(buf, 0xAB, sizeof buf);
  ChargeRecord *c = new (buf) ChargeRecord(true, 1, 2, 3, 4, 5);
  bool ok = buf[1] == 0 && c->horizontal && c->cross == 1 && c->amount == 5;
  std::memset(buf, 0xAB, sizeof buf);
  new (buf) ChargeRecord();
  ok = ok && buf[1] == 0;
  check(ok, "ChargeRecord: the byte after the bool is zero whichever constructor runs, even over dirty memory");
}

void testNetSpillRoundTrip() {
  const char *gr = "grid 4 3 2\nvertical capacity 0 4\nhorizontal capacity 4 0\nminimum width 1 1\n"
                   "minimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 2\n"
                   "n0 0 2 1\n5 5 1\n35 5 1\nn1 1 2 1\n5 15 1\n35 25 1\n0\n";
  Design d = parse("pd35route_selftest_spill.gr", gr);
  std::vector<RoutedNet> nets(2);
  for (int i = 0; i < 2; ++i)
    nets[i].net = &d.nets[i];
  prepareFor2DRefine(d, nets);
  nets[0].segs = {{0, 0, 0, 3, 0, 0}};
  nets[0].charges = {{true, 0, 0, 3, 0, 2}};
  nets[1].escalated = true;
  nets[1].boxMargin = 17;
  nets[1].mst = {{0, 1}};
  NetSpill sp;
  sp.write(nets, d.nets.data());
  std::vector<RoutedNet> back;
  sp.read(back, d.nets.data());
  bool same = back.size() == 2;
  for (size_t i = 0; same && i < 2; ++i)
    same = back[i].net == nets[i].net && back[i].giant == nets[i].giant && back[i].escalated == nets[i].escalated &&
           back[i].boxMargin == nets[i].boxMargin && back[i].pts.size() == nets[i].pts.size() &&
           back[i].mst == nets[i].mst && back[i].segs.size() == nets[i].segs.size() &&
           back[i].charges.size() == nets[i].charges.size();
  same = same && back[0].segs[0].x2 == 3 && back[0].charges[0].amount == 2 && back[0].charges[0].horizontal;
  check(same, "NetSpill: a spilled and re-read state equals the original");
}

// Part 152: lateral shifts. An 8 x 6 grid, one track per horizontal edge. Nets A and B share row 2 between
// columns 2 and 5 (over capacity there); legal net L runs along row 3 from column 1 to 6 and M along row 4 --
// full edges within 2 rows of the overflow. With k 1, L cannot move (row 2 over, row 4 full) and stays as it
// was, while M moves to row 5 (free): a U through columns 1 and 6, two edges longer, still connected; overflow is
// unchanged and the usage equals what the nets' charges add up to. With k 2, L also moves (two rows down). With
// no overflow anywhere, nothing is a candidate.
void testLateralShifts() {
  const char *gr = "grid 8 6 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n25 25 1\n55 25 1\n"
                   "nB 1 2 1\n25 25 1\n55 25 1\n"
                   "nL 2 2 1\n15 35 1\n65 35 1\n"
                   "nM 3 2 1\n15 45 1\n65 45 1\n0\n";
  auto setup = [&](Design &d, std::vector<RoutedNet> &nets) {
    nets.assign(4, RoutedNet());
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{2, 2, 0, 5, 2, 0}};
    nets[1].segs = {{2, 2, 0, 5, 2, 0}};
    nets[2].segs = {{1, 3, 0, 6, 3, 0}};
    nets[3].segs = {{1, 4, 0, 6, 4, 0}};
    prepareFor2DRefine(d, nets);
  };
  Design d = parse("pd39route_selftest_shift.gr", gr);
  std::vector<RoutedNet> nets;
  setup(d, nets);
  Usage u = usageFromCharges(d.grid, nets);
  const long long ov0 = totalOverflow(d.grid, u);
  const auto Lsegs = nets[2].segs;
  const long long wireM0 = totalWire2D(std::vector<RoutedNet>{nets[3]});
  ShiftOutcome o = shiftLegalPieces(d.grid, u, nets, 1, 2);
  Usage again = usageFromCharges(d.grid, nets);
  bool rowM5 = false;
  for (const Seg &sg : nets[3].segs)
    rowM5 = rowM5 || (sg.y1 == 5 && sg.y2 == 5);
  bool Lsame = nets[2].segs.size() == Lsegs.size();
  for (size_t i = 0; Lsame && i < Lsegs.size(); ++i)
    Lsame = nets[2].segs[i].x1 == Lsegs[i].x1 && nets[2].segs[i].y1 == Lsegs[i].y1 &&
            nets[2].segs[i].x2 == Lsegs[i].x2 && nets[2].segs[i].y2 == Lsegs[i].y2;
  check(o.candidates == 2 && o.shifted == 1 && o.wireAdded == 2 && rowM5 && Lsame && segsConnected(nets[3].segs) &&
            totalOverflow(d.grid, u) == ov0 && ov0 > 0 && again.h == u.h && again.v == u.v &&
            totalWire2D(std::vector<RoutedNet>{nets[3]}) == wireM0 + 2,
        "lateral shifts (k 1): the boxed-in net stays, the other moves to the free row; connected, +2 wire, "
        "overflow unchanged, usage = the charges");
  setup(d, nets);
  Usage u2 = usageFromCharges(d.grid, nets);
  ShiftOutcome o2 = shiftLegalPieces(d.grid, u2, nets, 2, 2);
  bool Lmoved = false;
  for (const Seg &sg : nets[2].segs)
    Lmoved = Lmoved || (sg.y1 == 5 && sg.y2 == 5) || (sg.y1 == 1 && sg.y2 == 1);
  check(o2.shifted >= 1 && Lmoved && segsConnected(nets[2].segs) && totalOverflow(d.grid, u2) == ov0,
        "lateral shifts (k 2): the first net can now move two rows, still connected, overflow unchanged");
  // no overflow anywhere: A and B removed
  std::vector<RoutedNet> two = {nets[2], nets[3]};
  setup(d, nets);
  two = {nets[2], nets[3]};
  Usage u3 = usageFromCharges(d.grid, two);
  ShiftOutcome o3 = shiftLegalPieces(d.grid, u3, two, 2, 2);
  check(o3.candidates == 0 && o3.shifted == 0, "lateral shifts: no overflow, no candidates");
}

// Part 191: blocker eviction chains. One track per edge. Nets A and B share row 0 from column 0 to 5 (over
// capacity: 10 units); legal net L fills row 1 between the same columns. With box margin 1, A and B see rows 0-1
// only, so plain refinement cannot clear them (every way across is full); L's box reaches row 2, which is free.
// (1) Control: refinement's reroute of B leaves the overflow at 10. (2) The chain: A's wanted route takes row 1,
// L is evicted to row 2, the total falls to 0 and the chain is kept -- every net connected and covering its pins,
// usage equal to what the charges add up to, +4 wire (two vias' worth of jogs each for A and L). (3) On a grid
// without row 2, L cannot leave: no chain is kept, and every net's route and the usage are exactly as before.
// (4) No overflow: nothing is tried.
void testEvictChains() {
  auto gr = [](int rows) {
    return std::string("grid 6 ") + std::to_string(rows) +
           " 2\nvertical capacity 0 2\nhorizontal capacity 2 0\nminimum width 1 1\nminimum spacing 1 1\n"
           "via spacing 0 0\n0 0 10 10\nnum net 3\n"
           "nA 0 2 1\n5 5 1\n55 5 1\nnB 1 2 1\n5 5 1\n55 5 1\nnL 2 2 1\n5 15 1\n55 15 1\n0\n";
  };
  auto setup = [](Design &d, std::vector<RoutedNet> &nets) {
    nets.assign(3, RoutedNet());
    for (int i = 0; i < 3; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 0, 0, 5, 0, 0}};
    nets[1].segs = {{0, 0, 0, 5, 0, 0}};
    nets[2].segs = {{0, 1, 0, 5, 1, 0}};
    prepareFor2DRefine(d, nets);
  };
  auto covers = [](const RoutedNet &rn) {
    for (const Point &p : rn.pts) {
      bool on = false;
      for (const Seg &sg : rn.segs)
        on = on || (sg.x1 == p.x && sg.y1 == p.y) || (sg.x2 == p.x && sg.y2 == p.y);
      if (!on)
        return false;
    }
    return segsConnected(rn.segs);
  };
  auto sameSegs = [](const std::vector<Seg> &a, const std::vector<Seg> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i)
      if (a[i].x1 != b[i].x1 || a[i].y1 != b[i].y1 || a[i].l1 != b[i].l1 || a[i].x2 != b[i].x2 ||
          a[i].y2 != b[i].y2 || a[i].l2 != b[i].l2)
        return false;
    return true;
  };
  auto wire = [](const std::vector<RoutedNet> &ns) {
    long long w = 0;
    for (const RoutedNet &rn : ns)
      for (const ChargeRecord &c : rn.charges)
        w += c.b - c.a;
    return w;
  };
  {
    const std::string t3 = gr(3);
    Design d = parse("pd39route_selftest_evict3.gr", t3.c_str());
    std::vector<RoutedNet> nets;
    setup(d, nets);
    Usage u = usageFromCharges(d.grid, nets);
    Prices pr(d.grid);
    pr.model = CostModel::kRefine;
    pr.live = &u;
    LayerPlan lp(d.grid);
    SearchRegion region;
    const long long ov0 = totalOverflow(d.grid, u);
    nets[1].boxMargin = 1;
    routeNetSlowMSMV(d.grid, pr, u, lp, region, 0.0, nets[1]);
    check(ov0 == 10 && totalOverflow(d.grid, u) == 10,
          "eviction chains, control: refinement's own reroute of B (margin 1) cannot clear the overflow (10)");
    setup(d, nets);
    Usage u2 = usageFromCharges(d.grid, nets);
    pr.live = &u2;
    const long long w0 = wire(nets);
    const EvictOutcome eo = evictChains(d.grid, u2, pr, lp, region, nets, 10, 1, 8);
    const Usage again = usageFromCharges(d.grid, nets);
    bool rowL2 = false;
    for (const Seg &sg : nets[2].segs)
      rowL2 = rowL2 || (sg.y1 == 2 && sg.y2 == 2);
    check(eo.before == 10 && eo.after == 0 && eo.kept == 1 && eo.blockersMoved == 1 && totalOverflow(d.grid, u2) == 0 &&
              again.h == u2.h && again.v == u2.v && rowL2 && covers(nets[0]) && covers(nets[1]) && covers(nets[2]) &&
              wire(nets) == w0 + 4 && eo.wireAdded == 4,
          "eviction chains: A takes row 1, L is evicted to row 2, overflow 10 -> 0, connected, usage = the charges, "
          "+4 wire");
  }
  {
    const std::string t2 = gr(2);
    Design d = parse("pd39route_selftest_evict2.gr", t2.c_str());
    std::vector<RoutedNet> nets;
    setup(d, nets);
    const std::vector<RoutedNet> start = nets;
    Usage u = usageFromCharges(d.grid, nets);
    const Usage u0 = u;
    Prices pr(d.grid);
    pr.model = CostModel::kRefine;
    pr.live = &u;
    LayerPlan lp(d.grid);
    SearchRegion region;
    const EvictOutcome eo = evictChains(d.grid, u, pr, lp, region, nets, 10, 1, 8);
    bool same = u.h == u0.h && u.v == u0.v;
    for (size_t i = 0; i < nets.size(); ++i)
      same = same && sameSegs(nets[i].segs, start[i].segs) && sameCharges2D(nets[i].charges, start[i].charges);
    check(eo.tried >= 1 && eo.kept == 0 && eo.after == 10 && same,
          "eviction chains: L has nowhere to go (no row 2), no chain kept, every route and the usage exactly as "
          "before");
    std::vector<RoutedNet> legal = {nets[0], nets[2]};
    Usage u3 = usageFromCharges(d.grid, legal);
    pr.live = &u3;
    const EvictOutcome e3 = evictChains(d.grid, u3, pr, lp, region, legal, 10, 1, 8);
    check(e3.tried == 0 && e3.kept == 0 && e3.after == 0, "eviction chains: no overflow, nothing tried");
  }
}

// Part 150: the wall-aware heuristic. (1) On 40 present-demand fixtures with a congested wall (gaps at varied
// rows, a decayed WL term, history), the search with the coarse bound finds exactly the cost the plain search
// finds, and expands fewer nodes in all. (2) Consistency, checked edge by edge: h(u) <= cost(u, v) + h(v) with h =
// max(Manhattan bound, coarse bound). (3) After coarseLower, a boundary's minimum is at most the cost its edge
// has once the net's usage is removed. (4) The region stage with the bound clears the row fixture; the key parses.
// Part 154: SearchHeap expands what the old lazy std::priority_queue did. A lockstep search on random graphs: the
// old queue (comparator: priority, then index, then dist; stale entries skipped) against SearchHeap, sharing one
// distance array; each expansion relaxes random neighbours (strictly shorter distances only, as the searches do),
// with priorities from a coarse random heuristic so ties are common, and sources listed twice. Fails if the
// sequence of expanded (node, priority) pairs differs, or the count of expansions does (the work-gated stops
// count them); also checks the priority encoding round-trips, -0.0 and negatives included.
void testSearchHeapMatchesPriorityQueue() {
  struct QE {
    double priority, dist;
    int index;
  };
  struct Cmp {
    bool operator()(const QE &a, const QE &b) const {
      if (a.priority != b.priority)
        return a.priority > b.priority;
      if (a.index != b.index)
        return a.index > b.index;
      return a.dist > b.dist;
    }
  };
  uint64_t seed = 12345;
  auto next = [&]() {
    uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  };
  int mismatches = 0;
  long long expansions = 0;
  for (int trial = 0; trial < 300; ++trial) {
    const int n = 20 + static_cast<int>(next() % 400);
    std::vector<double> hv(n), distRef(n, std::numeric_limits<double>::infinity());
    for (double &v : hv)
      v = static_cast<double>(next() % 4) * 0.5;
    std::vector<int> pos(n, -1);
    std::priority_queue<QE, std::vector<QE>, Cmp> ref;
    SearchHeap hp;
    hp.start(pos.data());
    const int ns = 1 + static_cast<int>(next() % 4);
    for (int k = 0; k < ns; ++k) {
      const int v = static_cast<int>(next() % std::min(n, 6)); // small range: sources repeat
      distRef[v] = 0.0;
      ref.push({hv[v], 0.0, v});
      hp.push(hv[v], v, /*source=*/true);
    }
    long long eRef = 0, eNew = 0;
    for (;;) {
      QE a{0, 0, -1};
      while (!ref.empty()) {
        a = ref.top();
        ref.pop();
        if (a.dist > distRef[a.index]) {
          a.index = -1;
          continue;
        }
        break;
      }
      SearchHeap::Item b{0, -1}; // Part 158: entries never go stale (decrease-key), so no skipping here
      if (!hp.empty()) {
        b = hp.top();
        hp.pop();
      }
      if (a.index != b.index || (a.index >= 0 && !(a.priority == b.priority))) {
        ++mismatches;
        break;
      }
      if (a.index < 0)
        break;
      ++eRef, ++eNew;
      const int v = a.index;
      const int deg = static_cast<int>(next() % 5);
      for (int k = 0; k < deg; ++k) {
        const int u = static_cast<int>(next() % n);
        const double w = static_cast<double>(next() % 3); // zero-cost edges too: ties and equal f
        const double nd = distRef[v] + w;
        if (nd < distRef[u]) {
          distRef[u] = nd;
          ref.push({nd + hv[u], nd, u});
          hp.push(nd + hv[u], u);
        }
      }
    }
    expansions += eNew;
    if (eRef != eNew || !ref.empty() != !hp.empty())
      ++mismatches;
  }
  int decodeBad = 0;
  const double specials[] = {0.0, -0.0, -1.5, 1e-300, 3.0, std::numeric_limits<double>::infinity(), 1e300};
  for (double p : specials)
    if (SearchHeap::fromOrdered(SearchHeap::orderedBits(p)) != p)
      ++decodeBad;
  for (double p : specials)
    for (double q : specials)
      if ((SearchHeap::orderedBits(p) < SearchHeap::orderedBits(q)) != (p < q))
        ++decodeBad;
  check(mismatches == 0, "SearchHeap expands the old queue's nodes in its order (" + std::to_string(mismatches) +
                             " mismatching trials, " + std::to_string(expansions) + " expansions)");
  check(decodeBad == 0, "SearchHeap's priority key keeps the order and decodes exactly");
}

void testWallAwareHeuristic() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd39route_selftest_coarseh.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  int same = 0, cases = 0, consistentBad = 0;
  long long popsPlain = 0, popsCoarse = 0;
  for (int f = 0; f < 40; ++f) {
    Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
    Prices pr(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    pr.pdIter = 40;
    pr.pdFactor = pdFactorForIter(pr.pdIter);
    const int wx = 16 + f % 6, gap = 3 + (f * 7) % 33;
    for (int y = 0; y < g.ny; ++y)
      for (int x = wx; x < wx + 3; ++x) {
        const size_t e = g.hIndex(x, y, 0);
        if (f % 2 == 1 || std::abs(y - gap) > 1) { // odd fixtures: a wall with no gap (every route must cross it)
          u.h[e] = u.capH[e] * (5 + f % 3) / 4;
          pr.pdHistH[e] = 1 + (x + y + f) % 8;
        }
      }
    std::vector<PathNode> src = {{3 + f % 5, 5 + (f * 3) % 30, 0}};
    std::vector<Point> snk = {{34 + f % 4, 4 + (f * 11) % 32}};
    WallBound wb;
    wb.B = 4 + 4 * ((f / 2) % 2);
    wb.amount = 2;
    wallBuild(wb, g, pr);
    const long long p0 = gSearchPops;
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
    const double ca = a.found ? region.distAt(region.idx(a.path.back().x, a.path.back().y, 0)) : -1;
    const long long p1 = gSearchPops;
    WallBound *saved = gWall;
    gWall = &wb;
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
    const double cbst = b.found ? region.distAt(region.idx(b.path.back().x, b.path.back().y, 0)) : -2;
    // consistency over every edge, with the distances this search used
    auto h = [&](int x, int y) { // the box is the whole grid here (margin 60), so P / Q start at 0
      return std::abs(wb.P[x] - wb.P[snk[0].x]) + std::abs(wb.Q[y] - wb.Q[snk[0].y]);
    };
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x) {
        const double c = wireCostH(pr, g.hIndex(x, y, 0), 2);
        consistentBad += h(x, y) > c + h(x + 1, y) + 1e-9 || h(x + 1, y) > c + h(x, y) + 1e-9;
      }
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x) {
        const double c = wireCostV(pr, g.vIndex(x, y, 0), 2);
        consistentBad += h(x, y) > c + h(x, y + 1) + 1e-9 || h(x, y + 1) > c + h(x, y) + 1e-9;
      }
    gWall = saved;
    const long long p2 = gSearchPops;
    ++cases;
    same += a.found && b.found && std::abs(ca - cbst) <= 1e-9 * std::max(1.0, ca);
    popsPlain += p1 - p0;
    popsCoarse += p2 - p1;
  }
  check(cases == 40 && same == 40 && popsCoarse < popsPlain,
        "wall-aware heuristic: the same cheapest cost on 40 walled fixtures (" + std::to_string(same) +
            "/40), expansions " + std::to_string(popsCoarse) + " vs " + std::to_string(popsPlain));
  check(consistentBad == 0, "wall-aware heuristic: consistent on every edge (" + std::to_string(consistentBad) + " violations)");
  {
    // (3) a rip-up lowers what an edge costs; coarseLower must have lowered its boundary to at most that
    Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
    Prices pr(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    for (int y = 0; y < g.ny; ++y)
      u.h[g.hIndex(7, y, 0)] = u.capH[g.hIndex(7, y, 0)] * 2; // every edge crossing boundary x 7|8 over capacity
    WallBound wb;
    wb.B = 8;
    wb.amount = 2;
    wallBuild(wb, g, pr);
    const size_t slot = static_cast<size_t>(7) * wb.nby + 3 / wb.B;
    const double before = wb.minH[slot];
    std::vector<ChargeRecord> net = {{true, 3, 6, 9, 0, 6}}; // a net of amount 6 crossing 7 -> 8 in row 3
    wallLower(wb, g, pr, u, net);
    u.h[g.hIndex(7, 3, 0)] -= 6; // the rip-up
    const double after = wireCostH(pr, g.hIndex(7, 3, 0), 2);
    check(wb.minH[slot] <= after + 1e-12 && wb.minH[slot] < before && u.h[g.hIndex(6, 3, 0)] == 0,
          "wall-aware heuristic: a rip-up's boundary is lowered first, so the table stays a lower bound");
  }
  {
    Design dr = parse("pd39route_selftest_coarseh_row.gr", kThreeOnOneRowGr);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &dr.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(dr, nets);
    RegionSettings rs;
    rs.enabled = true;
    rs.wallB = 2;
    RegionReport rr = regionNegotiate(dr, nets, rs, false);
    bool conn = true;
    for (const RoutedNet &rn : nets)
      conn = conn && segsConnected(rn.segs);
    RegionSettings v;
    std::string err;
    check(rr.after == 0 && conn && rr.wallBuilds > 0 && gWall == nullptr && applyRegionVariant("wall=4", v, err) &&
              v.wallB == 4,
          "wall-aware heuristic: the region stage clears the row fixture with it (and leaves it off afterwards)");
  }
}

// Part 197: the two-ended search (PD39ROUTE_SEARCH_BIDIR) against the one-ended one, search by search.
void testBidirSearch() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 20\nhorizontal capacity 20 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 1\nn0 0 2 1\n5 5 1\n15 5 1\n0\n";
  Design d = parse("pd39route_selftest_bidir.gr", gr.c_str());
  const RoutingGrid &g = d.grid;
  LayerPlan lp(g);
  SearchRegion region;
  const bool saved = gSearchBidir;
  const bool savedPot = gSearchBidirPotentials;
  int cases = 0, sameCost = 0, valid = 0, sameFound = 0, zeroCases = 0, pocketCases = 0, potOk = 0;
  long long popsOne = 0, popsTwo = 0, pocketOne = 0, pocketTwo = 0, popsPot = 0;
  std::string firstBad;
  for (int f = 0; f < 120; ++f) {
    Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
    Prices pr(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    const bool zero = f % 4 == 3; // the WL term underflowed to 0: free edges cost exactly nothing
    pr.pdIter = zero ? 600 : 40;
    pr.pdFactor = pdFactorForIter(pr.pdIter);
    const int kind = f % 3; // 0: a wall (with or without a gap); 1: the sink in a pocket; 2: both ends in pockets
    std::vector<PathNode> src = {{3 + f % 5, 5 + (f * 3) % 30, 0}};
    std::vector<Point> snk = {{34 + f % 4, 4 + (f * 11) % 32}};
    if (kind == 1) // the real case (Part 196): a short connection in a box many times its span
      snk[0] = {src[0].x + 7 + f % 3, std::min(g.ny - 2, std::max(1, src[0].y + f % 5 - 2))};
    if (f % 5 == 0) { // a tree as the source set, one tile listed twice
      for (int k = 1; k <= 4; ++k)
        src.push_back({src[0].x + k, src[0].y, 0});
      src.push_back(src[1]);
    }
    if (f % 7 == 0) { // several sinks, one tile listed twice
      snk.push_back({30, (snk[0].y + 9) % 38 + 1});
      snk.push_back({snk[0].x, snk[0].y});
    }
    auto pocket = [&](int cx, int cy, int r) {
      for (int y = std::max(0, cy - r); y <= std::min(g.ny - 1, cy + r); ++y)
        for (int x = std::max(0, cx - r); x <= std::min(g.nx - 1, cx + r); ++x) {
          if (x + 1 < g.nx) {
            const size_t e = g.hIndex(x, y, 0);
            u.h[e] = u.capH[e] * (5 + (x + f) % 3) / 4;
            pr.pdHistH[e] = 1 + (x + 2 * y + f) % 8;
          }
          if (y + 1 < g.ny) {
            const size_t e = g.vIndex(x, y, 0);
            u.v[e] = u.capV[e] * (5 + (y + f) % 3) / 4;
            pr.pdHistV[e] = 1 + (2 * x + y + f) % 8;
          }
        }
    };
    if (kind == 0) {
      const int wx = 16 + f % 6, gap = 3 + (f * 7) % 33;
      for (int y = 0; y < g.ny; ++y)
        for (int x = wx; x < wx + 3; ++x)
          if (f % 2 == 1 || std::abs(y - gap) > 1) {
            const size_t e = g.hIndex(x, y, 0);
            u.h[e] = u.capH[e] * (5 + f % 3) / 4;
            pr.pdHistH[e] = 1 + (x + y + f) % 8;
          }
    } else {
      pocket(snk[0].x, snk[0].y, 2 + f % 3);
      if (kind == 2)
        pocket(src[0].x, src[0].y, 2);
    }
    const int margin = f % 6 == 5 ? 6 : 60; // a small box now and then (some of its sides can grow)
    const int amount = 1 + g.minSpacing[0];
    auto costOf = [&](const MSMVResult &r) {
      double c = 0;
      for (size_t k = 0; k + 1 < r.path.size(); ++k) {
        const PathNode &a = r.path[k], &b = r.path[k + 1];
        c += a.y == b.y ? wireCostH(pr, g.hIndex(std::min(a.x, b.x), a.y, 0), amount)
                        : wireCostV(pr, g.vIndex(a.x, std::min(a.y, b.y), 0), amount);
      }
      return c;
    };
    auto isSrc = [&](const PathNode &n) {
      for (const PathNode &q : src)
        if (q.x == n.x && q.y == n.y)
          return true;
      return false;
    };
    auto isSnk = [&](const PathNode &n) {
      for (const Point &q : snk)
        if (q.x == n.x && q.y == n.y)
          return true;
      return false;
    };
    auto wellFormed = [&](const MSMVResult &r) {
      if (!r.found || r.path.empty() || r.sinkIndex < 0 || r.sinkIndex >= static_cast<int>(snk.size()))
        return false;
      const PathNode &last = r.path.back();
      bool ok = isSrc(r.path.front()) && snk[r.sinkIndex].x == last.x && snk[r.sinkIndex].y == last.y;
      for (size_t k = 0; ok && k + 1 < r.path.size(); ++k) {
        ok = std::abs(r.path[k].x - r.path[k + 1].x) + std::abs(r.path[k].y - r.path[k + 1].y) == 1;
        ok = ok && !isSnk(r.path[k]) && (k == 0 || !isSrc(r.path[k]));
        for (size_t m = 0; ok && m < k; ++m)
          ok = r.path[m].x != r.path[k].x || r.path[m].y != r.path[k].y;
      }
      return ok;
    };
    gSearchBidir = false;
    const long long p0 = gSearchPops;
    MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, margin);
    const long long p1 = gSearchPops;
    gSearchBidir = true;
    gSearchBidirPotentials = false;
    MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, margin);
    const long long p2 = gSearchPops;
    gSearchBidirPotentials = true; // Part 199
    MSMVResult c = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, margin);
    const long long p3 = gSearchPops;
    gSearchBidir = saved;
    gSearchBidirPotentials = savedPot;
    potOk += a.found == c.found && c.found && wellFormed(c) &&
             std::abs(costOf(a) - costOf(c)) <= 1e-9 * std::max(1.0, costOf(a));
    popsPot += p3 - p2;
    ++cases;
    zeroCases += zero;
    sameFound += a.found == b.found;
    const double ca = a.found ? costOf(a) : -1, cb = b.found ? costOf(b) : -2;
    const bool same = a.found && b.found && std::abs(ca - cb) <= 1e-9 * std::max(1.0, ca);
    const bool wf = wellFormed(b);
    sameCost += same;
    valid += wf;
    if ((!same || !wf) && firstBad.empty())
      firstBad = "fixture " + std::to_string(f) + ": cost " + std::to_string(ca) + " vs " + std::to_string(cb) +
                 (wf ? "" : ", route not a simple source-to-sink path");
    popsOne += p1 - p0;
    popsTwo += p2 - p1;
    if (kind == 1 && margin == 60) {
      ++pocketCases;
      pocketOne += p1 - p0;
      pocketTwo += p2 - p1;
    }
  }
  check(cases == 120 && sameFound == 120 && sameCost == 120,
        "two-ended search: the one-ended search's cost on 120 fixtures (" + std::to_string(sameCost) + "/120; " +
            std::to_string(zeroCases) + " with free edges at cost 0)" + (firstBad.empty() ? "" : " -- " + firstBad));
  check(valid == 120, "two-ended search: every route a simple path from a source tile to its sink (" +
                          std::to_string(valid) + "/120)");
  check(potOk == 120, "two-ended search with potentials (value 2): the same cost and a simple source-to-sink path on " +
                          std::to_string(potOk) + "/120 fixtures (" + std::to_string(popsPot) + " expansions; value 1 " +
                          std::to_string(popsTwo) + ", one-ended " + std::to_string(popsOne) + ")");
  {
    // open ground, the heuristic at full strength (the early rounds): the potentials must take back what the
    // heuristic-free search loses to one-ended A* there
    long long one = 0, two = 0, pot = 0;
    int ok = 0;
    for (int f = 0; f < 30; ++f) {
      Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
      Prices pr(g);
      CostSettings cs;
      cs.model = CostModel::kPresentDemand;
      configureCostModel(pr, u, cs);
      pr.pdIter = 1;
      pr.pdFactor = pdFactorForIter(pr.pdIter);
      std::vector<PathNode> src = {{2 + f % 7, 3 + (f * 5) % 33, 0}};
      std::vector<Point> snk = {{22 + (f * 3) % 16, 2 + (f * 13) % 36}};
      const int amount = 1 + g.minSpacing[0];
      auto costOf = [&](const MSMVResult &r) {
        double c = 0;
        for (size_t k = 0; k + 1 < r.path.size(); ++k) {
          const PathNode &a = r.path[k], &b = r.path[k + 1];
          c += a.y == b.y ? wireCostH(pr, g.hIndex(std::min(a.x, b.x), a.y, 0), amount)
                          : wireCostV(pr, g.vIndex(a.x, std::min(a.y, b.y), 0), amount);
        }
        return c;
      };
      gSearchBidir = false;
      const long long q0 = gSearchPops;
      MSMVResult a = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
      const long long q1 = gSearchPops;
      gSearchBidir = true;
      gSearchBidirPotentials = false;
      MSMVResult b = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
      const long long q2 = gSearchPops;
      gSearchBidirPotentials = true;
      MSMVResult c = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
      const long long q3 = gSearchPops;
      gSearchBidir = saved;
      gSearchBidirPotentials = savedPot;
      ok += a.found && b.found && c.found && std::abs(costOf(a) - costOf(c)) <= 1e-9 * std::max(1.0, costOf(a)) &&
            std::abs(costOf(a) - costOf(b)) <= 1e-9 * std::max(1.0, costOf(a));
      one += q1 - q0;
      two += q2 - q1;
      pot += q3 - q2;
    }
    check(ok == 30 && pot * 2 < two && pot < one * 2,
          "two-ended search with potentials: on open ground under half of value 1's expansions and under twice "
          "one-ended A*'s (" + std::to_string(pot) + " vs " + std::to_string(two) + " vs " + std::to_string(one) + ")");
  }
  check(pocketCases > 20 && pocketTwo * 4 < pocketOne,
        "two-ended search: a sink in a pocket takes under a quarter of the expansions (" + std::to_string(pocketTwo) +
            " vs " + std::to_string(pocketOne) + " on " + std::to_string(pocketCases) + " fixtures; all 120: " +
            std::to_string(popsTwo) + " vs " + std::to_string(popsOne) + ")");
  {
    // the box-limit proof: a wall whose only gap is outside a margin-6 box -> limited; the whole grid -> never
    Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
    Prices pr(g);
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    configureCostModel(pr, u, cs);
    pr.pdIter = 40;
    pr.pdFactor = pdFactorForIter(pr.pdIter);
    for (int y = 0; y < g.ny; ++y)
      for (int x = 18; x < 21; ++x)
        if (y < 30) {
          const size_t e = g.hIndex(x, y, 0);
          u.h[e] = u.capH[e] * 2;
          pr.pdHistH[e] = 8;
        }
    std::vector<PathNode> src = {{10, 10, 0}};
    std::vector<Point> snk = {{30, 10}};
    gSearchBidir = true;
    MSMVResult narrow = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 6);
    MSMVResult whole = multiSourceDijkstra(g, pr, lp, region, src, snk, 1, 60);
    gSearchBidir = saved;
    check(narrow.found && narrow.boxLimited && whole.found && !whole.boxLimited,
          "two-ended search: box-limited when the wall's gap lies outside a margin-6 box, never with the whole grid");
  }
}

// Part 148: the slow tier's plateau stop. Three nets on a one-row, one-track grid: the overflow is forced, so
// the slow tier's best never falls. With the stop (window 3, any gain, floor 1) it ends early with the same
// best and the same routes as the full slow budget; with the floor above the best it runs the full budget.
void testSlowPlateauStop() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd39route_selftest_slowplateau.gr", row);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  auto run = [&](int window, long long floor) {
    const SlowPlateau saved = gSlowPlateau;
    gSlowPlateau = SlowPlateau{window, 0.01, floor};
    RunReport r = route(d, 2, 30, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, true, true, true, 8,
                        BoxGrowth{}, cs, AcceptMode::kKeep, false, -1);
    gSlowPlateau = saved;
    return r;
  };
  RunReport full = run(0, 0), stop = run(3, 1), high = run(3, 1000000);
  bool same = full.nets.size() == stop.nets.size();
  for (size_t i = 0; same && i < full.nets.size(); ++i) {
    same = full.nets[i].segs.size() == stop.nets[i].segs.size();
    for (size_t k = 0; same && k < full.nets[i].segs.size(); ++k)
      same = full.nets[i].segs[k].x1 == stop.nets[i].segs[k].x1 && full.nets[i].segs[k].y1 == stop.nets[i].segs[k].y1 &&
             full.nets[i].segs[k].x2 == stop.nets[i].segs[k].x2 && full.nets[i].segs[k].y2 == stop.nets[i].segs[k].y2;
  }
  check(!full.slowPlateaued && stop.slowPlateaued && !high.slowPlateaued && stop.roundsRun < full.roundsRun &&
            stop.bestOverflow == full.bestOverflow && full.bestOverflow > 0 && same,
        "slow-tier plateau stop: a flat best stops early (" + std::to_string(stop.roundsRun) + " vs " +
            std::to_string(full.roundsRun) + " rounds) with the same best and routes; not below its floor");
}

// Part 173: the slow tier's capacity cut. A one-row grid, 3 nets on 1 track per edge: full cut (1 track, floored at
// half of 2 units -> 1): an edge at capacity beside an overflowed one counts as overflowed, price only it does not.
// Through route(): the reported best and the returned routes are real overflow, cut or not, in both modes.
void testSlowCut() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 4 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 5\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\nnD 3 2 1\n5 5 1\n75 5 1\nnE 4 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd39route_selftest_slowcut.gr", row);
  const RoutingGrid &g = d.grid;
  const SlowCutSettings saved = gSlowCut;
  const size_t hot = g.hIndex(3, 0, 0), side = g.hIndex(4, 0, 0), far = g.hIndex(0, 0, 0);
  auto boundary = [&](bool full, int every, long long below) {
    gSlowCut = SlowCutSettings{1, 1, below, every, full};
    Usage u(g);
    Prices pr(g);
    u.h[hot] = u.capH[hot] + 2; // overflowed
    u.h[side] = u.capH[side];   // at capacity, legal
    u.h[far] = u.capH[far];     // at capacity, outside the zone
    SlowCut c;
    const int realSide = u.capH[side], realFar = u.capH[far];
    c.step(g, u, pr, 0, 2, false);
    bool started = c.on() && c.startRound() == 0;
    bool capsFull = u.capH[side] == virtualCap(realSide, 2) && u.capH[side] < realSide && u.capH[far] == realFar;
    bool capsPrice = u.capH[side] == realSide && pr.vredH != nullptr;
    long long real = c.realOverflow(g, u);
    bool sideOver = u.h[side] > u.capH[side];
    return std::make_tuple(started, capsFull, capsPrice, real, sideOver);
  };
  auto [sf, capsF, unusedF, realF, overF] = boundary(true, 0, 10);
  auto [sp, unusedP, capsP, realP, overP] = boundary(false, 0, 10);
  (void)unusedF;
  (void)unusedP;
  auto [sn, a1, a2, a3, a4] = boundary(true, 0, 1); // 2 > below: not yet
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  check(sf && capsF && realF == 2 && overF, "slow cut, full: a zone edge at capacity counts as overflowed, an edge "
                                            "outside keeps its capacity, and real overflow stays 2");
  check(sp && capsP && realP == 2 && !overP, "slow cut, price only: capacities untouched, the cost seam set");
  check(!sn, "slow cut: does not start above `below`");
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  auto run = [&](const SlowCutSettings &c) {
    gSlowCut = c;
    return route(d, 2, 12, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, true, true, true, 8,
                 BoxGrowth{}, cs, AcceptMode::kKeep, false, -1);
  };
  RunReport none = run(SlowCutSettings{});
  RunReport price = run(SlowCutSettings{1, 1, 1000000, 0, false});
  RunReport full = run(SlowCutSettings{1, 1, 1000000, 2, true});
  gSlowCut = saved;
  check(none.slowCutRound == -1 && price.slowCutRound == 0 && full.slowCutRound == 0, "slow cut: starts at the first boundary");
  check(none.bestOverflow > 0 && price.bestOverflow == totalOverflow(g, usageFromCharges(g, price.nets)) &&
            full.bestOverflow == totalOverflow(g, usageFromCharges(g, full.nets)) && full.bestOverflow == none.bestOverflow,
        "slow cut: the best reported is the returned routes' real overflow (" + std::to_string(full.bestOverflow) + ")");
}

// Part 186: the slow tier's over-capacity cost. The row fixture of testSlowCut (5 nets on 2 tracks: overflow is
// forced): with no gate it prices from round 0; gated below the forced overflow it never starts and the routes are
// the run without it, net for net; on, the best reported is the returned routes' real overflow.
void testSlowOver() {
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 4 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 5\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\nnD 3 2 1\n5 5 1\n75 5 1\nnE 4 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd39route_selftest_slowover.gr", row);
  const RoutingGrid &g = d.grid;
  const SlowOverSettings saved = gSlowOver;
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  auto run = [&](const SlowOverSettings &o) {
    gSlowOver = o;
    return route(d, 2, 12, 0.02, 1.0, HistMode::kAdditive, 1.0, 1, false, true, 1.0, 0.0, true, true, true, 8,
                 BoxGrowth{}, cs, AcceptMode::kKeep, false, -1);
  };
  RunReport none = run(SlowOverSettings{});
  RunReport on = run(SlowOverSettings{0, LLONG_MAX});
  RunReport gated = run(SlowOverSettings{0, 1});
  gSlowOver = saved;
  bool same = none.nets.size() == gated.nets.size();
  for (size_t i = 0; same && i < none.nets.size(); ++i) {
    same = none.nets[i].segs.size() == gated.nets[i].segs.size();
    for (size_t k = 0; same && k < none.nets[i].segs.size(); ++k)
      same = none.nets[i].segs[k].x1 == gated.nets[i].segs[k].x1 && none.nets[i].segs[k].y1 == gated.nets[i].segs[k].y1 &&
             none.nets[i].segs[k].x2 == gated.nets[i].segs[k].x2 && none.nets[i].segs[k].y2 == gated.nets[i].segs[k].y2;
  }
  check(none.slowOverRound == -1 && on.slowOverRound == 0 && gated.slowOverRound == -1,
        "slow over-capacity cost: from round 0 ungated, never above its gate, never when off");
  check(none.bestOverflow > 0 && same && gated.bestOverflow == none.bestOverflow,
        "slow over-capacity cost: gated off, the run is the run without it");
  check(on.bestOverflow == totalOverflow(g, usageFromCharges(g, on.nets)),
        "slow over-capacity cost: the best reported is the returned routes' real overflow (" +
            std::to_string(on.bestOverflow) + ")");
}

// Part 147: annealed refinement. The acceptance rule: off -> a worse reroute is always reverted; on at a
// very high temperature nearly always kept, at a very low one never; a better or equal reroute is never
// reverted; the same seed draws the same decisions. The stage with annealing on the row fixture still ends
// at its best state (no worse than its start), every net connected, and repeats exactly.
void testAnnealedRefinement() {
  const AnnealState saved = gAnneal;
  gAnneal = AnnealState{};
  bool offReverts = revertReroute(AcceptMode::kRevert, 3, 5) && !revertReroute(AcceptMode::kRevert, 5, 5) &&
                    !revertReroute(AcceptMode::kRevert, 5, 3) && !revertReroute(AcceptMode::kKeep, 3, 5);
  gAnneal.on = true;
  gAnneal.rng = 42;
  gAnneal.T = 1e9;
  int keptHot = 0;
  for (int i = 0; i < 1000; ++i)
    keptHot += !revertReroute(AcceptMode::kRevert, 3, 5);
  gAnneal.T = 1e-9;
  int keptCold = 0;
  for (int i = 0; i < 1000; ++i)
    keptCold += !revertReroute(AcceptMode::kRevert, 3, 5);
  gAnneal.T = 1.0; // exp(-2) ~ 0.135
  gAnneal.rng = 7;
  std::vector<bool> d1, d2;
  for (int i = 0; i < 2000; ++i)
    d1.push_back(revertReroute(AcceptMode::kRevert, 3, 5));
  gAnneal.rng = 7;
  for (int i = 0; i < 2000; ++i)
    d2.push_back(revertReroute(AcceptMode::kRevert, 3, 5));
  int keptMid = 0;
  for (bool r : d1)
    keptMid += !r;
  const bool betterKept = !revertReroute(AcceptMode::kRevert, 5, 3);
  gAnneal = saved;
  check(offReverts && keptHot >= 999 && keptCold == 0 && betterKept,
        "annealing acceptance: off reverts worse; very hot keeps worse (" + std::to_string(keptHot) +
            "/1000), very cold keeps none; better is never reverted");
  check(d1 == d2 && keptMid > 200 && keptMid < 340,
        "annealing acceptance: seeded and reproducible; at T 1 a 2-unit worse reroute kept " +
            std::to_string(keptMid) + "/2000 times (exp(-2) ~ 270)");
  auto runRow = [&](std::vector<std::vector<Seg>> &out, RefineReport &rr) {
    Design d = parse("pd39route_selftest_anneal.gr", kThreeOnOneRowGr);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    RefineSettings rs;
    rs.accept = AcceptMode::kRevert;
    rs.pieces = true;
    rs.annealT0 = 2.0;
    rs.annealAlpha = 0.8;
    rr = refine2D(d, nets, rs, false);
    out.clear();
    bool conn = true;
    for (const RoutedNet &rn : nets) {
      out.push_back(rn.segs);
      conn = conn && segsConnected(rn.segs);
    }
    if (!conn)
      out.clear();
  };
  std::vector<std::vector<Seg>> a, b;
  RefineReport ra, rb;
  runRow(a, ra);
  runRow(b, rb);
  bool same = !a.empty() && a.size() == b.size();
  for (size_t i = 0; same && i < a.size(); ++i) {
    same = a[i].size() == b[i].size();
    for (size_t k = 0; same && k < a[i].size(); ++k)
      same = a[i][k].x1 == b[i][k].x1 && a[i][k].y1 == b[i][k].y1 && a[i][k].x2 == b[i][k].x2 &&
             a[i][k].y2 == b[i][k].y2;
  }
  check(same && ra.after <= ra.before && ra.after == rb.after && !gAnneal.on,
        "annealed refinement: ends at its best (" + std::to_string(ra.after) + " from " + std::to_string(ra.before) +
            "), every net connected, the same routes twice, annealing off afterwards");
}

// Part 146: the region stage's early stop. A start above the threshold that is not beaten by the round limit
// stops there with its start back (the round count and the aborted flag say so); below the threshold, or
// with the stop off, the stage runs as before, with the same routes.
void testRegionAbort() {
  // Three nets on a one-row grid with one track: the overflow is forced, so no round can beat the start.
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  Design d = parse("pd39route_selftest_abort.gr", row);
  auto fresh = [&]() {
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    return nets;
  };
  auto segsOf = [](const std::vector<RoutedNet> &ns) {
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : ns)
      out.push_back(rn.segs);
    return out;
  };
  auto same = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size())
        return false;
      for (size_t k = 0; k < a[i].size(); ++k)
        if (a[i][k].x1 != b[i][k].x1 || a[i][k].y1 != b[i][k].y1 || a[i][k].x2 != b[i][k].x2 ||
            a[i][k].y2 != b[i][k].y2)
          return false;
    }
    return true;
  };
  RegionSettings rs;
  rs.enabled = true;
  rs.rounds = 30;
  std::vector<RoutedNet> a = fresh(), b = fresh(), c = fresh();
  RegionReport off = regionNegotiate(d, b, rs, false);
  RegionSettings ab = rs;
  ab.abortAbove = 1;
  ab.abortRounds = 3;
  RegionReport on = regionNegotiate(d, a, ab, false);
  RegionSettings hi = ab;
  hi.abortAbove = 1000000; // the start is below this threshold: no stop
  RegionReport below = regionNegotiate(d, c, hi, false);
  check(off.after == off.before && off.roundsRun > 3, "region abort fixture: forced overflow, no round beats the start");
  check(on.aborted && on.roundsRun == 3 && on.after == on.before && same(segsOf(a), segsOf(b)),
        "region abort: a start above the threshold, not beaten by round 3, stops there with the same routes");
  check(!below.aborted && below.roundsRun == off.roundsRun && same(segsOf(c), segsOf(b)),
        "region abort: below the threshold the stage runs exactly as with the stop off");
}

// Part 144: damping. The route-change discount scales only the congestion part of a stamped edge's cost (the
// WL term, the heuristic's floor, stays whole); unstamped edges are unchanged. With either damping setting the
// region stage still clears the three-nets-on-one-row fixture; the portfolio keys parse.
void testRegionDamping() {
  Design d = parse("pd39route_selftest_damp.gr", kThreeOnOneRowGr);
  const RoutingGrid &g = d.grid;
  Usage u = usageFromCharges(g, std::vector<RoutedNet>{});
  u.h[g.hIndex(2, 1, 0)] = 4; // over capacity 2: a large congestion term
  Prices pr(g);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  const size_t e = g.hIndex(2, 1, 0), f = g.hIndex(3, 1, 0);
  const double ce = wireCostH(pr, e, 2), cf = wireCostH(pr, f, 2), wl = pdWl(pr);
  std::vector<uint32_t> incH(u.h.size(), 0), incV(u.v.size(), 0);
  incH[e] = 7;
  pr.incH = &incH;
  pr.incV = &incV;
  pr.incStamp = 7;
  pr.incBeta = 0.5;
  const double de = wireCostH(pr, e, 2), df = wireCostH(pr, f, 2);
  check(std::abs(de - (wl + 0.5 * (ce - wl))) < 1e-9 * ce && df == cf && de >= heuristicScale(pr) && de < ce,
        "region damping: a stamped edge pays the WL term plus half its congestion term; others unchanged");
  bool ok = true;
  for (int mode = 0; mode < 2; ++mode) {
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    RegionSettings rs;
    rs.enabled = true;
    if (mode == 0)
      rs.damp = 0.5;
    else
      rs.cooldown = 1;
    RegionReport rr = regionNegotiate(d, nets, rs, false);
    bool conn = true;
    for (const RoutedNet &rn : nets)
      conn = conn && segsConnected(rn.segs);
    ok = ok && rr.after == 0 && conn;
  }
  check(ok, "region stage with damping 0.5, and with a 1-round cooldown: zero overflow, every net connected");
  RegionSettings v;
  std::string err;
  check(applyRegionVariant("damp=0.3,cool=2", v, err) && v.damp == 0.3 && v.cooldown == 2 &&
            !applyRegionVariant("damp=1.5", v, err) && !applyRegionVariant("cool=0.5", v, err),
        "portfolio variant parse: damp (0-1) and cool (an integer)");
}

// Part 141: the reclaim path's own region box cap (RegionSettings::boxCapB): on testLowMemFallbackMatches'
// fixture (A at 0 <= the threshold, so B runs), path A's last margin stays within boxCap and B's within
// boxCapB; boxCapB unset (-1) gives B the same cap as A.
void testReclaimPathBoxCap() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](int capB) {
    RegionSettings region;
    region.enabled = true;
    region.rounds = 20;
    region.expand = 0;
    region.boxInit = 5; // round 0's margin is min(5, cap): the cap shows from the first round
    region.boxGrow = 1;
    region.boxCap = 2;
    region.boxCapB = capB;
    Design d = parse("pd39route_selftest_capb.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    return reclaimWithFallback(d, nets, rs, region, refine, false, false, 0);
  };
  FallbackReport same = run(-1), wide = run(7);
  check(same.ranB && wide.ranB && same.regionA.lastMargin == 2 && same.regionB.lastMargin == 2 &&
            wide.regionA.lastMargin == 2 && wide.regionB.roundsRun >= 1 && wide.regionB.lastMargin > 2 &&
            wide.regionB.lastMargin <= 7,
        "reclaim path's box cap: A keeps the region cap; B uses its own (" +
            std::to_string(wide.regionB.lastMargin) + " after " + std::to_string(wide.regionB.roundsRun) +
            " rounds), the region cap when unset");
}

// pd35route (memory): the low-memory fallback keeps exactly the routes the
// sequential fallback keeps, on testFallbackAFirstThreshold's fixtures:
// A at 0 <= threshold (B runs and wins) and A above it (B never runs).
void testLowMemFallbackMatches() {
  const char *gr = "grid 16 5 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                   "num net 4\n"
                   "nA 0 2 1\n5 15 1\n55 15 1\n"
                   "nD 1 2 1\n105 15 1\n155 15 1\n"
                   "nB 2 2 1\n105 35 1\n155 35 1\n"
                   "nC 3 2 1\n105 35 1\n155 35 1\n0\n";
  RegionSettings region;
  region.enabled = true;
  region.rounds = 20;
  region.expand = 0;
  RefineSettings refine;
  refine.iters = 5;
  ReclaimSettings rs;
  rs.ratio = 1.2;
  rs.minExcess = 2;
  auto run = [&](bool lowMem, long long aMax, FallbackReport &fr, std::vector<std::vector<Seg>> &out) {
    bool saved = gLowMem;
    gLowMem = lowMem;
    Design d = parse("pd35route_selftest_lowmem.gr", gr);
    std::vector<RoutedNet> nets(4);
    for (int i = 0; i < 4; ++i)
      nets[i].net = &d.nets[i];
    nets[0].segs = {{0, 1, 0, 0, 0, 0}, {0, 0, 0, 5, 0, 0}, {5, 0, 0, 5, 1, 0}};
    nets[1].segs = {{10, 1, 0, 10, 0, 0}, {10, 0, 0, 15, 0, 0}, {15, 0, 0, 15, 1, 0}};
    nets[2].segs = {{10, 3, 0, 15, 3, 0}};
    nets[3].segs = {{10, 3, 0, 15, 3, 0}};
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, aMax);
    out.clear();
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    gLowMem = saved;
  };
  auto sameSegs = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size())
        return false;
      for (size_t k = 0; k < a[i].size(); ++k)
        if (a[i][k].x1 != b[i][k].x1 || a[i][k].y1 != b[i][k].y1 || a[i][k].x2 != b[i][k].x2 ||
            a[i][k].y2 != b[i][k].y2 || a[i][k].l1 != b[i][k].l1 || a[i][k].l2 != b[i][k].l2)
          return false;
    }
    return true;
  };
  FallbackReport f0, f1;
  std::vector<std::vector<Seg>> s0, s1;
  run(false, 0, f0, s0);
  run(true, 0, f1, s1);
  check(f1.ranB && f1.tookB == f0.tookB && f1.overflowA == f0.overflowA && f1.wireB == f0.wireB && sameSegs(s0, s1),
        "low-memory fallback, B runs: same choice and the same kept routes");
  // A above the threshold: the row fixture (three nets on a one-track row).
  const char *row = "grid 8 1 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                    "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\n"
                    "num net 3\nnA 0 2 1\n5 5 1\n75 5 1\nnB 1 2 1\n5 5 1\n75 5 1\n"
                    "nC 2 2 1\n5 5 1\n75 5 1\n0\n";
  auto runRow = [&](bool lowMem, FallbackReport &fr, std::vector<std::vector<Seg>> &out) {
    bool saved = gLowMem;
    gLowMem = lowMem;
    Design d = parse("pd35route_selftest_lowmem_row.gr", row);
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 0, 0, 7, 0, 0}};
    }
    prepareFor2DRefine(d, nets);
    fr = reclaimWithFallback(d, nets, rs, region, refine, false, false, 10);
    out.clear();
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    gLowMem = saved;
  };
  FallbackReport g0, g1;
  runRow(false, g0, s0);
  runRow(true, g1, s1);
  check(!g1.ranB && !g1.tookB && g1.overflowA == g0.overflowA && g1.overflowA > 10 && sameSegs(s0, s1),
        "low-memory fallback, A above the threshold: B never runs, the same A kept");
  // Part 135: low-memory mode runs path A's portfolio too, with the same pick and routes; the gated
  // portfolio (variants only after path A ends above the threshold) matches the always-on one.
  const std::vector<std::string> savedPf = gPortfolio;
  const bool savedAfter = gPortfolioAfter;
  gPortfolio = {"lagr=0.2", "rounds=3"};
  FallbackReport p0, p1, p2, p3;
  std::vector<std::vector<Seg>> s2, s3;
  gPortfolioAfter = false;
  runRow(false, p0, s0);
  runRow(true, p1, s1);
  gPortfolioAfter = true;
  runRow(false, p2, s2);
  runRow(true, p3, s3);
  check(p1.portfolio.size() == 3 && p0.portfolio.size() == 3 && p1.portfolioPick == p0.portfolioPick &&
            p1.overflowA == p0.overflowA && sameSegs(s0, s1),
        "low-memory fallback with a portfolio: every variant runs, the same pick and routes");
  bool sameRes = p2.portfolio.size() == 3 && p3.portfolio.size() == 3;
  for (size_t i = 0; sameRes && i < 3; ++i)
    sameRes = p2.portfolio[i].overflow == p0.portfolio[i].overflow && p2.portfolio[i].wire == p0.portfolio[i].wire &&
              p3.portfolio[i].overflow == p0.portfolio[i].overflow && p3.portfolio[i].wire == p0.portfolio[i].wire;
  check(sameRes && p2.portfolioPick == p0.portfolioPick && p3.portfolioPick == p0.portfolioPick &&
            !p2.portfolioSkipped && sameSegs(s0, s2) && sameSegs(s0, s3),
        "gated portfolio, A above the threshold: the same variant results, pick and routes as always-on");
  // A at or below the threshold: the gated portfolio runs no variants and keeps the no-portfolio routes.
  FallbackReport q0, q1, q2;
  std::vector<std::vector<Seg>> t0, t1, t2;
  gPortfolio = {};
  run(false, 0, q0, t0);
  gPortfolio = {"lagr=0.2", "rounds=3"};
  run(false, 0, q1, t1);
  run(true, 0, q2, t2);
  // Part 168: the speculative portfolio -- variants started beside path A -- gives the gated portfolio's
  // results exactly: above the threshold the same variant results, pick and routes; at it, the variants are
  // cancelled and the no-portfolio routes kept.
  gPortfolioSpec = true;
  FallbackReport v0, v1, w1, w2;
  std::vector<std::vector<Seg>> sv0, sv1, tw1, tw2;
  runRow(false, v0, sv0);
  runRow(true, v1, sv1);
  run(false, 0, w1, tw1);
  run(true, 0, w2, tw2);
  gPortfolioSpec = false;
  bool sameSpec = v0.portfolio.size() == 3 && v1.portfolio.size() == 3;
  for (size_t i = 0; sameSpec && i < 3; ++i)
    sameSpec = v0.portfolio[i].overflow == p0.portfolio[i].overflow && v0.portfolio[i].wire == p0.portfolio[i].wire &&
               v1.portfolio[i].overflow == p0.portfolio[i].overflow && v1.portfolio[i].wire == p0.portfolio[i].wire;
  check(sameSpec && v0.portfolioPick == p0.portfolioPick && v1.portfolioPick == p0.portfolioPick &&
            !v0.portfolioSkipped && !v0.portfolioCancelled && sameSegs(s0, sv0) && sameSegs(s0, sv1),
        "speculative portfolio, A above the threshold: the same variant results, pick and routes as gated");
  check(w1.portfolioSkipped && w1.portfolioCancelled && w2.portfolioCancelled && w1.portfolio.empty() && w1.ranB &&
            w1.tookB == q0.tookB && sameSegs(t0, tw1) && sameSegs(t0, tw2),
        "speculative portfolio, A at the threshold: the variants cancelled, B runs, the no-portfolio routes kept");
  // Part 170: with PD39ROUTE_PORTFOLIO_DELAY the variants start late (or, above the threshold, after path A) and
  // give the same results; at the threshold they never start and no copies are made.
  bool sameDelay = true;
  for (const double delay : {1e9, 1e-9}) {
    gPortfolioSpec = true;
    gPortfolioDelay = delay;
    FallbackReport x0, x1, y1, y2;
    std::vector<std::vector<Seg>> sx0, sx1, ty1, ty2;
    runRow(false, x0, sx0);
    runRow(true, x1, sx1);
    run(false, 0, y1, ty1);
    run(true, 0, y2, ty2);
    gPortfolioSpec = false;
    gPortfolioDelay = 0.0;
    bool same = x0.portfolio.size() == 3 && x1.portfolio.size() == 3;
    for (size_t i = 0; same && i < 3; ++i)
      same = x0.portfolio[i].overflow == p0.portfolio[i].overflow && x0.portfolio[i].wire == p0.portfolio[i].wire &&
             x1.portfolio[i].overflow == p0.portfolio[i].overflow && x1.portfolio[i].wire == p0.portfolio[i].wire;
    same = same && x0.portfolioPick == p0.portfolioPick && x1.portfolioPick == p0.portfolioPick &&
           sameSegs(s0, sx0) && sameSegs(s0, sx1) && (delay > 1.0 ? x0.portfolioStartedAt < 0.0 : true);
    const bool atThr = delay > 1.0 ? y1.portfolioNotStarted && y2.portfolioNotStarted && !y1.portfolioCancelled
                                   : y1.portfolioSkipped && (y1.portfolioCancelled || y1.portfolioNotStarted);
    same = same && atThr && y1.portfolio.empty() && y1.ranB && y1.tookB == q0.tookB && sameSegs(t0, ty1) &&
           sameSegs(t0, ty2);
    sameDelay = sameDelay && same;
  }
  check(sameDelay, "PD39ROUTE_PORTFOLIO_DELAY: variants started late or after path A give the same results, pick "
                   "and routes; at the threshold within the delay they never start");
  gPortfolio = savedPf;
  gPortfolioAfter = savedAfter;
  check(q1.portfolioSkipped && q2.portfolioSkipped && q1.portfolio.empty() && q1.ranB && q1.tookB == q0.tookB &&
            sameSegs(t0, t1) && sameSegs(t0, t2),
        "gated portfolio, A at the threshold: no variants run, B runs, the no-portfolio routes kept");
  // Part 168: while path A's own variant runs it keeps its full thread share; the variants beside it split the rest.
  {
    ThreadShare share;
    share.running.store(4);
    share.primary.store(1);
    ThreadShare *const savedShare = tShare;
    tShare = &share;
    tPrimary = true;
    const int own = batchThreads();
    tPrimary = false;
    const int other = batchThreads();
    share.primary.store(0);
    share.running.store(3);
    const int after = batchThreads();
    tShare = savedShare;
    const int cores = shareCores();
    check(own == std::max(1, std::min(gThreads, cores)) &&
              other == std::max(1, std::min(gThreads, (cores - own) / 3)) &&
              after == std::max(1, std::min(gThreads, cores / 3)),
          "thread share: path A's own variant keeps its share beside speculative variants, which split the rest");
  }
}

// pd36route: the incremental pattern-stage refresh gives exactly the routes
// of a full refresh before every batch. 40 x 40 grid, one track per edge,
// 300 pseudo-random 2-pin nets: congested enough that later batches in a
// round see earlier ones' usage -- a stale refresh (no update after the
// round's first batch) must route differently, or the fixture proves
// nothing.
void testPatternIncrementalRefreshIsExact() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 300\n";
  unsigned st = 12345;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  for (int i = 0; i < 300; ++i) {
    int x1 = rnd(40), y1 = rnd(40), x2 = rnd(40), y2 = rnd(40);
    gr += "n" + std::to_string(i) + " " + std::to_string(i) + " 2 1\n" + std::to_string(x1 * 10 + 5) + " " +
          std::to_string(y1 * 10 + 5) + " 1\n" + std::to_string(x2 * 10 + 5) + " " + std::to_string(y2 * 10 + 5) +
          " 1\n";
  }
  gr += "0\n";
  auto run = [&](int mode, long long &overflow, int cell = 0, bool dag = false, int threads = 1) {
    Design d = parse("pd36route_selftest_refresh.gr", gr.c_str());
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    PatternStageSettings ps;
    ps.rounds = 5;
    if (cell > 0)
      ps.cell = cell;
    gPatternRefreshTest = mode;
    const bool savedDag = gPatternDag;
    const int savedT = gThreads;
    gPatternDag = dag;
    gThreads = threads;
    RunReport rep = route(d, 60, 0, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true,
                          true, 8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1, &ps);
    gPatternDag = savedDag;
    gThreads = savedT;
    gPatternRefreshTest = 0;
    overflow = rep.bestOverflow;
    std::vector<std::vector<Seg>> segs;
    for (const RoutedNet &rn : rep.nets)
      segs.push_back(rn.segs);
    return segs;
  };
  auto same = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size())
        return false;
      for (size_t j = 0; j < a[i].size(); ++j) {
        const Seg &p = a[i][j], &q = b[i][j];
        if (p.x1 != q.x1 || p.y1 != q.y1 || p.l1 != q.l1 || p.x2 != q.x2 || p.y2 != q.y2 || p.l2 != q.l2)
          return false;
      }
    }
    return true;
  };
  long long ovInc = 0, ovFull = 0, ovStale = 0;
  auto inc = run(0, ovInc), full = run(1, ovFull), stale = run(2, ovStale);
  check(ovFull > 0, "refresh fixture is congested (2D overflow " + std::to_string(ovFull) + " after 5 rounds)");
  check(same(inc, full) && ovInc == ovFull, "incremental refresh: the same routes as a full refresh per batch");
  check(!same(stale, full), "stale costs route differently: the fixture exercises the per-batch refresh");
  // pd38route: PD38ROUTE_PATTERN_DAG -- the batches on the dependency graph,
  // with 4-tile coarse cells so many pieces run at once: exactly the batch
  // loop's routes, at 1 and at 4 threads.
  long long ovB = 0, ovD1 = 0, ovD4 = 0;
  auto bl = run(0, ovB, 4), d1 = run(0, ovD1, 4, true, 1), d4 = run(0, ovD4, 4, true, 4);
  check(same(bl, d1) && ovB == ovD1, "pattern stage on the dependency graph, 1 thread: the batch loop's routes");
  check(same(bl, d4) && ovB == ovD4, "pattern stage on the dependency graph, 4 threads: the batch loop's routes");
}

// pd36route: the local-sum CPU pattern backend returns exactly the
// reference backend's choices and costs, on random grids (costs up to 2^20
// and some at kCostMax) and random batches of straight, L and Z pieces.
void testLocalPatternBackendMatchesReference() {
  unsigned st = 777;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  bool same = true;
  size_t pieces = 0;
  for (int t = 0; t < 40 && same; ++t) {
    gpu::PatternGrid pg;
    pg.nx = 2 + rnd(60);
    pg.ny = 2 + rnd(60);
    pg.h.resize(static_cast<size_t>(pg.nx - 1) * pg.ny);
    pg.v.resize(static_cast<size_t>(pg.nx) * (pg.ny - 1));
    for (auto &c : pg.h)
      c = rnd(50) == 0 ? gpu::kCostMax : rnd(1 << 20);
    for (auto &c : pg.v)
      c = rnd(50) == 0 ? gpu::kCostMax : rnd(1 << 20);
    gpu::PatternBatch pb;
    for (int i = 0; i < 200; ++i) {
      pb.x1.push_back(rnd(pg.nx));
      pb.y1.push_back(rnd(pg.ny));
      pb.x2.push_back(rnd(pg.nx));
      pb.y2.push_back(rnd(pg.ny));
    }
    gpu::CpuPatternBackend ref;
    gpu::CpuLocalPatternBackend loc;
    gpu::PatternResult a, b;
    ref.solve(pg, pb, a);
    loc.solve(pg, pb, b);
    same = a.best == b.best && a.cost == b.cost;
    pieces += pb.size();
  }
  check(same && pieces == 8000, "local pattern backend: the reference's choices and costs on 8000 random pieces");
}

// pd37route: the bitset batch coloring equals pd36route's first-fit scan,
// on 40000 random coarse boxes over 20 x 20 cells (enough to need more than
// 256 batches, so the bitsets grow), and on a few hand cases.
void testPatternColorBatchesMatchesFirstFit() {
  unsigned st = 31337;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  std::vector<PatternCellBox> bx;
  for (int i = 0; i < 40000; ++i) {
    int x = rnd(20), y = rnd(20);
    bx.push_back({x, y, std::min(19, x + rnd(6)), std::min(19, y + rnd(6))});
  }
  auto a = patternColorFirstFit(bx, 20, 20), b = patternColorBatches(bx, 20, 20);
  check(a.size() > 256, "pattern batch coloring fixture: more than 256 batches (" + std::to_string(a.size()) + ")");
  check(a == b, "pattern batch coloring: bitsets = first-fit scan");
  std::vector<PatternCellBox> hand = {{0, 0, 1, 1}, {1, 1, 2, 2}, {3, 3, 3, 3}, {0, 0, 0, 0}, {2, 2, 3, 3}};
  check(patternColorBatches(hand, 4, 4) == patternColorFirstFit(hand, 4, 4) &&
            patternColorBatches(hand, 4, 4) == std::vector<std::vector<int>>{{0, 2}, {1, 3}, {4}},
        "pattern batch coloring: hand case {0,2} {1,3} {4}");
}

// pd37route (Part 117): emitNetSolutionFast writes emitNetSolution's text
// and counts, on 3000 random nets: random pins (1-6, 1-3 access points),
// random straight wires and vias, some zero-length or bent pieces (dropped),
// some unrouted nets, pins left uncovered (fixed by a via).
void testEmitNetSolutionFastMatches() {
  g24::Resources r;
  r.nLayers = 6;
  unsigned st = 2024;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  bool same = true;
  g24::SolutionReport ra, rb;
  for (int t = 0; t < 3000 && same; ++t) {
    g24::Net n;
    n.name = "n" + std::to_string(t);
    int np = 1 + rnd(6);
    for (int k = 0; k < np; ++k) {
      std::vector<g24::Access> p;
      int na = 1 + rnd(3);
      for (int a = 0; a < na; ++a)
        p.push_back({rnd(8), rnd(8), rnd(6)});
      n.pins.push_back(p);
    }
    std::vector<g24::Seg> segs;
    int ns = rnd(10) == 0 ? 0 : rnd(12);
    for (int k = 0; k < ns; ++k) {
      int x = rnd(8), y = rnd(8), z = rnd(6), kind = rnd(5);
      if (kind == 0)
        segs.push_back({x, y, z, std::min(7, x + rnd(5)), y, z});
      else if (kind == 1)
        segs.push_back({x, y, z, x, std::min(7, y + rnd(5)), z});
      else if (kind == 2)
        segs.push_back({x, y, std::min(z, 4), x, y, std::min(5, z + 1 + rnd(3))});
      else if (kind == 3)
        segs.push_back({x, y, z, x, y, z}); // zero-length
      else
        segs.push_back({x, y, 1, std::min(7, x + 1), y, 2}); // not straight
    }
    std::ostringstream oa;
    g24::emitNetSolution(r, n, segs, oa, ra);
    std::string ob;
    g24::emitNetSolutionFast(r, n, segs, ob, rb);
    same = oa.str() == ob;
  }
  same = same && ra.nets == rb.nets && ra.wires == rb.wires && ra.vias == rb.vias && ra.droppedZero == rb.droppedZero &&
         ra.pinFixes == rb.pinFixes && ra.unroutedNets == rb.unroutedNets;
  check(same && ra.pinFixes > 0 && ra.unroutedNets > 0 && ra.droppedZero > 0,
        "fast net emitter: emitNetSolution's text and counts on 3000 random nets");
}

// pd37route (Part 117): the fast readers give the iostream readers' values
// on a .cap and .net with mixed whitespace (tabs, CRLF, runs of blanks),
// decimals in several spellings, signed and padded integers, and a last
// line with no newline.
void testFastReadersMatchIostream() {
  const std::string cap = "pd37route_selftest_fast.cap", net = "pd37route_selftest_fast.net";
  {
    std::ofstream c(cap);
    c << "3 4 3\r\n0.5\t4  5 1e1 +2.25\n1000 2000 1500\n3000 007\n"
         "Metal1 0 0\n1 2 3 4\n0 0 0 0\n9.5 .5 5. 2e-1\n"
         "Metal2   1\t100.0\r\n2 2 2 2\n1.25 1.5 1.75 2\n3 3 3 3\n"
         "Metal3 0 0.25\n6.15 9.78571 10 0\n1E2 0.001 12.5 -0\n7 7 7 7";
  }
  {
    std::ofstream n(net);
    n << "net_alpha\r\n(\n[(0, 1, 2), (1,2,2)]\n[( 0 ,3 ,1 ) ]  \n)\n"
         "b2\n(\n\t[(2, 0, 0), (+1, 1, 1), (0, 3, 2)]\r\n[(0,0,0)]\n)\n"
         "c3\n(\n[(1, 2, 1)]\n[(2, 3, 2), (1, 0, 0)]\n)";
  }
  g24::Resources ra, rb;
  std::string ea, eb;
  std::ifstream ci(cap);
  bool okA = g24::readResources(ci, ra, ea);
  g24::FastText cf;
  bool okB = cf.open(cap) && g24::readResourcesFast(cf, rb, eb);
  bool sameCap = okA && okB && ra.nLayers == rb.nLayers && ra.xSize == rb.xSize && ra.ySize == rb.ySize &&
                 ra.unitLength == rb.unitLength && ra.unitVia == rb.unitVia && ra.overflowCost == rb.overflowCost &&
                 ra.xPitch == rb.xPitch && ra.yPitch == rb.yPitch && ra.cap.size() == rb.cap.size();
  for (size_t i = 0; sameCap && i < ra.cap.size(); ++i)
    sameCap = std::memcmp(&ra.cap[i], &rb.cap[i], sizeof(double)) == 0;
  for (int z = 0; sameCap && z < ra.nLayers; ++z)
    sameCap = ra.layers[z].name == rb.layers[z].name && ra.layers[z].dir == rb.layers[z].dir &&
              ra.layers[z].minLength == rb.layers[z].minLength;
  check(sameCap, "fast .cap reader: the iostream reader's values, bit for bit (" + ea + eb + ")");
  std::ifstream ni(net);
  g24::NetStream sa(ni);
  g24::FastText nf;
  nf.open(net);
  g24::FastNetStream sb(nf);
  g24::Net na, nb;
  bool sameNet = true;
  int count = 0;
  while (true) {
    bool a = sa.next(na, ea), b = sb.next(nb, eb);
    if (a != b) {
      sameNet = false;
      break;
    }
    if (!a)
      break;
    ++count;
    sameNet = sameNet && na.name == nb.name && na.pins.size() == nb.pins.size();
    for (size_t p = 0; sameNet && p < na.pins.size(); ++p) {
      sameNet = na.pins[p].size() == nb.pins[p].size();
      for (size_t q = 0; sameNet && q < na.pins[p].size(); ++q)
        sameNet = na.pins[p][q].x == nb.pins[p][q].x && na.pins[p][q].y == nb.pins[p][q].y &&
                  na.pins[p][q].z == nb.pins[p][q].z;
    }
  }
  check(sameNet && count == 3 && ea == eb, "fast .net reader: the same 3 nets, names and access points");
  std::remove(cap.c_str());
  std::remove(net.c_str());
}

// Part 131: per-net time. A clock inside another on the same thread counts
// nothing; the outer one counts the whole span in its own kind; a net
// outside the table and a null net are ignored; with the table off, clocks
// do nothing at all.
void testNetClock() {
  std::vector<Net> fake(3);
  const Net *savedBase = gNetBase;
  std::vector<std::array<float, 4>> saved;
  saved.swap(gNetTime);
  // Part 133: spin for 2 ms of this thread's CPU time (an elapsed-time spin can get less CPU than that on
  // a loaded machine, now that the clock counts CPU time)
  auto spin = [] { const double t = threadCpuSeconds(); while (threadCpuSeconds() - t < 0.002) {} };
  { NetClock off(&fake[0], kNtMaze); spin(); }
  bool offOk = gNetTime.empty();
  gNetBase = fake.data();
  gNetTime.assign(fake.size(), std::array<float, 4>{0, 0, 0, 0});
  {
    NetClock outer(&fake[1], kNtLayer);
    spin();
    { NetClock inner(&fake[2], kNtMaze); spin(); }
  }
  { NetClock none(nullptr, kNt3D); spin(); }
  addNetTime(&fake[0], kNtPattern, 0.5);
  bool ok = gNetTime[1][kNtLayer] >= 0.004f && gNetTime[2][kNtMaze] == 0.0f && gNetTime[0][kNtPattern] == 0.5f &&
            gNetTime[0][kNtMaze] == 0.0f && tNetClockDepth == 0;
  gNetTime.swap(saved);
  gNetBase = savedBase;
  // Part 133: CPU time, not elapsed -- 50 ms asleep inside a clock records
  // (nearly) nothing; the old elapsed-time clock recorded 0.05 s
  gNetBase = fake.data();
  saved.swap(gNetTime);
  gNetTime.assign(fake.size(), std::array<float, 4>{0, 0, 0, 0});
  { NetClock nap(&fake[0], kNtMaze); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
  bool cpuOnly = gNetTime[0][kNtMaze] < 0.01f;
  gNetTime.swap(saved);
  gNetBase = savedBase;
  check(offOk, "per-net time: with the table off a clock records nothing");
  check(ok, "per-net time: the outer clock counts both spans in its kind, the nested one nothing; explicit shares add");
  check(cpuOnly, "per-net time is thread CPU time: 50 ms asleep records under 10 ms");
}

// Part 127: concurrent work runs on the persistent pool. runOnThreads calls
// every index exactly once; a call made while the pool is busy (from one of
// its own tasks) runs inline rather than deadlocking; tryRun reports busy.
void testWorkerPoolReuse() {
  int saved = gThreads;
  gThreads = 4;
  std::vector<std::atomic<int>> hits(4);
  for (auto &h : hits)
    h.store(0);
  runOnThreads(4, [&](size_t t) { hits[t].fetch_add(1); });
  bool once = true;
  for (auto &h : hits)
    once = once && h.load() == 1;
  std::atomic<int> inner{0}, refused{0};
  workerPool().run(4, [&](size_t) {
    if (!workerPool().tryRun(2, [](size_t) {}, 1))
      refused.fetch_add(1);
    workerPool().run(3, [&](size_t) { inner.fetch_add(1); }); // nested: inline
    runOnThreads(2, [&](size_t) { inner.fetch_add(1); });      // busy pool: fresh threads
  });
  std::atomic<int> after{0};
  runOnThreads(4, [&](size_t) { after.fetch_add(1); });
  gThreads = saved;
  check(once, "worker pool: runOnThreads(4) calls each of the 4 indices exactly once");
  check(refused.load() == 4 && inner.load() == 4 * (3 + 2) && after.load() == 4,
        "worker pool: busy pool refuses tryRun, nested calls run inline or on fresh threads, then it is free again");
}

// Part 125: the ISPD 2025 additions to the 2024 format (g24_io.h). The
// .cap's second line ends with the four PPA weights; a pin line starts
// with the pin's name and a timing value; a 2025 solution is in DEF units
// with layer names and one-layer vias. A 2024 file reads as before.
void testIspd25Format() {
  const std::string body = "4200 4200 4200\n4200 4200\n"
                           "Metal1 0 0\n1 2 3 4\n0 0 0 0\n9.5 .5 5. 2e-1\n"
                           "Metal2 1 100\n2 2 2 2\n1.25 1.5 1.75 2\n3 3 3 3\n"
                           "Metal3 0 0.25\n6.15 9.78571 10 0\n1E2 0.001 12.5 -0\n7 7 7 7\n";
  const std::string c24 = "pd39route_selftest_24.cap", c25 = "pd39route_selftest_25.cap", cBad = "pd39route_selftest_bad.cap";
  {
    std::ofstream(c24) << "3 4 3\n0.5 4 5 1e1 2.25\n" << body;
    std::ofstream(c25) << "3 4 3 \n0.5 4 5 1e1 2.25 -10 -100 300 0.0000003\n" << body;
    std::ofstream(cBad) << "3 4 3\n0.5 4 5 1e1 2.25 -10 -100\n" << body;
  }
  auto both = [](const std::string &f, g24::Resources &a, g24::Resources &b, std::string &ea, std::string &eb) {
    std::ifstream in(f);
    bool okA = g24::readResources(in, a, ea);
    g24::FastText t;
    bool okB = t.open(f) && g24::readResourcesFast(t, b, eb);
    return std::make_pair(okA, okB);
  };
  g24::Resources a24, b24, a25, b25, aBad, bBad;
  std::string e1, e2, e3, e4, e5, e6;
  auto r24 = both(c24, a24, b24, e1, e2), r25 = both(c25, a25, b25, e3, e4), rBad = both(cBad, aBad, bBad, e5, e6);
  bool sameCaps = r24.first && r24.second && r25.first && r25.second && a24.cap == a25.cap && b24.cap == b25.cap &&
                  a25.cap == b25.cap && a24.overflowCost == a25.overflowCost && a25.xPitch == a24.xPitch;
  check(sameCaps && !a24.ppa && !b24.ppa && a24.gcellDbu == 0,
        "2025 .cap: the same capacities as the 2024 file; the 2024 file has no weights");
  check(a25.ppa && b25.ppa && a25.ppaWeight[0] == -10 && a25.ppaWeight[1] == -100 && a25.ppaWeight[2] == 300 &&
            a25.ppaWeight[3] == 0.0000003 && b25.ppaWeight[3] == a25.ppaWeight[3] && a25.gcellDbu == 4200 &&
            b25.gcellDbu == 4200,
        "2025 .cap: the four PPA weights (both readers), GCells of 4200 units");
  check(!rBad.first && !rBad.second && e5.find("4 PPA weights") != std::string::npos && e5 == e6,
        "2025 .cap: two extra values on the cost line rejected by both readers (" + e5 + ")");
  const std::string net = "pd39route_selftest_25.net";
  std::ofstream(net) << "n_alpha\n( \nu1/Z, -2.91964e-10, [(0, 1, 2), (1,2,2)]\n"
                        "bus\\[3\\]/A, 7.26732e-10, [( 0 ,3 ,1 ) ]\n) \nb2\n(\n[(2, 0, 0), (1, 1, 1)]\n)\n";
  std::ifstream ni(net);
  g24::NetStream sa(ni);
  g24::FastText nf;
  nf.open(net);
  g24::FastNetStream sb(nf);
  g24::Net na, nb, ma, mb;
  std::string ea, eb;
  bool okNets = sa.next(na, ea) && sb.next(nb, eb) && sa.next(ma, ea) && sb.next(mb, eb);
  auto pinsAre = [](const g24::Net &n, const std::vector<std::vector<int>> &want) {
    if (n.pins.size() != want.size())
      return false;
    for (size_t p = 0; p < want.size(); ++p) {
      if (n.pins[p].size() * 3 != want[p].size())
        return false;
      for (size_t q = 0; q < n.pins[p].size(); ++q)
        if (n.pins[p][q].z != want[p][3 * q] || n.pins[p][q].x != want[p][3 * q + 1] ||
            n.pins[p][q].y != want[p][3 * q + 2])
          return false;
    }
    return true;
  };
  std::vector<std::vector<int>> alpha = {{0, 1, 2, 1, 2, 2}, {0, 3, 1}}, beta = {{2, 0, 0, 1, 1, 1}};
  check(okNets && na.name == "n_alpha" && nb.name == "n_alpha" && pinsAre(na, alpha) && pinsAre(nb, alpha) &&
            ma.name == "b2" && pinsAre(ma, beta) && pinsAre(mb, beta),
        "2025 .net: pin names and timing values skipped, access points as in 2024 (both readers; escaped brackets)");
  check(na.timing.size() == 2 && nb.timing == na.timing && na.timing[0] == -2.91964e-10f &&
            na.timing[1] == 7.26732e-10f && ma.timing.empty() && mb.timing.empty(),
        "2025 .net: each pin's timing value kept; a 2024 pin line gives none");
  // The writer: a wire on Metal2 from GCell 0 to 2, a via Metal1 -> Metal3 at GCell 1.
  g24::Net w;
  w.name = "w";
  w.pins = {{{0, 0, 1}}, {{1, 0, 0}}};
  std::vector<g24::Seg> segs = {{0, 0, 1, 2, 0, 1}, {1, 0, 0, 1, 0, 2}};
  g24::SolutionReport fr, sr;
  std::string fast;
  g24::emitNetSolutionFast(a25, w, segs, fast, fr);
  std::ostringstream slow;
  g24::emitNetSolution(a25, w, segs, slow, sr);
  const std::string want = "w\n(\n2100 2100 Metal2 10500 2100 Metal2\n6300 2100 Metal1 6300 2100 Metal2\n"
                           "6300 2100 Metal2 6300 2100 Metal3\n)\n";
  check(fast == want && slow.str() == want && fr.wires == 1 && fr.vias == 2 && sr.vias == 2 && fr.layer0Wires == 0,
        "2025 solution: GCell centres in DEF units, layer names, the via split into one-layer steps (both writers)");
  std::string f24;
  g24::SolutionReport r24rep;
  g24::emitNetSolutionFast(a24, w, segs, f24, r24rep);
  check(f24 == "w\n(\n0 0 1 2 0 1\n1 0 0 1 0 2\n)\n", "2024 solution text unchanged (GCell indices, one via line)");
  std::string l0;
  g24::SolutionReport l0rep;
  g24::emitNetSolutionFast(a25, w, {{0, 0, 0, 1, 0, 0}, {0, 0, 0, 0, 0, 1}, {1, 0, 0, 1, 0, 1}}, l0, l0rep);
  check(l0rep.layer0Wires == 1, "2025 solution: a wire on the bottom layer is counted (the evaluator rejects it)");
  for (const std::string &f : {c24, c25, cBad, net})
    std::remove(f.c_str());
}

// pd38route: the slow tier on the dependency graph is the same at 1 and 4
// threads, on a congested 40 x 40 fixture (one track per edge, 250 random
// nets) where 40 slow rounds beat the pattern stage's best, so the routes
// kept are the slow tier's.
void testSlowDagSameAtAnyThreadCount() {
  std::string gr = "grid 40 40 2\nvertical capacity 0 2\nhorizontal capacity 2 0\n"
                   "minimum width 1 1\nminimum spacing 1 1\nvia spacing 0 0\n0 0 10 10\nnum net 250\n";
  unsigned st = 777;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  for (int i = 0; i < 250; ++i) {
    int np = 2 + rnd(3), cx = 0 + rnd(40), cy = 0 + rnd(40);
    gr += "n" + std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(np) + " 1\n";
    for (int k = 0; k < np; ++k) {
      int x = std::min(39, std::max(0, cx + rnd(15) - 7)), y = std::min(39, std::max(0, cy + rnd(15) - 7));
      gr += std::to_string(x * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    }
  }
  gr += "0\n";
  auto run = [&](int threads, long long &overflow, int &escalated, int batch = 0,
                 std::vector<SlowRoundRecord> *history = nullptr) {
    Design d = parse("pd38route_selftest_slow.gr", gr.c_str());
    std::vector<SlowRoundRecord> h;
    gSlowHistory = &h;
    CostSettings cs;
    cs.model = CostModel::kPresentDemand;
    PatternStageSettings ps;
    ps.rounds = 3;
    int savedT = gThreads;
    bool savedD = gSlowDag;
    gThreads = threads;
    gSlowDag = true;
    int savedB = gSlowBatch;
    gSlowBatch = batch;
    gSlowBatchCheck = batch > 0;
    BoxGrowth bx;
    bx.mode = BoxMode::Round;
    bx.init = 4;
    bx.grow = 2;
    bx.cap = 20;
    RunReport rep = route(d, 60, 40, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true,
                          8, bx, cs, AcceptMode::kKeep, true, -1, &ps);
    gSlowHistory = nullptr;
    if (history)
      *history = std::move(h);
    gThreads = savedT;
    gSlowDag = savedD;
    gSlowBatch = savedB;
    gSlowBatchCheck = false;
    overflow = rep.bestOverflow;
    escalated = rep.escalatedCount;
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : rep.nets)
      out.push_back(rn.segs);
    return out;
  };
  long long o1 = 0, o4 = 0;
  int e1 = 0, e4 = 0;
  std::vector<SlowRoundRecord> h1, h4;
  auto a = run(1, o1, e1, 0, &h1), b = run(4, o4, e4, 0, &h4);
  bool same = a.size() == b.size();
  for (size_t i = 0; same && i < a.size(); ++i) {
    same = a[i].size() == b[i].size();
    for (size_t j = 0; same && j < a[i].size(); ++j)
      same = a[i][j].x1 == b[i][j].x1 && a[i][j].y1 == b[i][j].y1 && a[i][j].x2 == b[i][j].x2 &&
             a[i][j].y2 == b[i][j].y2 && a[i][j].l1 == b[i][j].l1 && a[i][j].l2 == b[i][j].l2;
  }
  // Part 122 (option C): the slow tier's own rounds are compared, whichever
  // state the run keeps. Whether the slow tier beats the pattern stage here
  // depends on the platform's floating point (the Mac's slow tier wins by
  // 12 of 1830; Linux's never wins, and the final routes are then the
  // pattern stage's on both thread counts), so it is reported, not required.
  long long slowBest = std::numeric_limits<long long>::max();
  int kept = 0;
  for (const SlowRoundRecord &r : h1) {
    slowBest = std::min(slowBest, r.overflow);
    kept += r.accepted;
  }
  check(h1.size() >= 10 && kept > 20,
        "slow DAG fixture: the slow tier ran (" + std::to_string(h1.size()) + " rounds, " + std::to_string(kept) +
            " reroutes kept, its best 2D overflow " + std::to_string(slowBest) + "; kept state " +
            std::to_string(o1) + ", " + std::to_string(e1) + " nets escalated)");
  check(same && o1 == o4 && e1 == e4 && h1 == h4,
        "slow tier on the dependency graph: 4 threads = 1 thread, round for round and route for route");

  // pd38route: PD38ROUTE_SLOW_BATCH. Batches of 16 at 1 and 4 threads: the
  // same routes, every replica equal to the live state after every batch
  // (checked inside), and the conflict rule exercised (nets carried).
  auto sameRoutes = [](const std::vector<std::vector<Seg>> &x, const std::vector<std::vector<Seg>> &y) {
    bool eq = x.size() == y.size();
    for (size_t i = 0; eq && i < x.size(); ++i) {
      eq = x[i].size() == y[i].size();
      for (size_t j = 0; eq && j < x[i].size(); ++j)
        eq = x[i][j].x1 == y[i][j].x1 && x[i][j].y1 == y[i][j].y1 && x[i][j].x2 == y[i][j].x2 &&
             x[i][j].y2 == y[i][j].y2 && x[i][j].l1 == y[i][j].l1 && x[i][j].l2 == y[i][j].l2;
    }
    return eq;
  };
  long long ob1 = 0, ob4 = 0, os1 = 0;
  int eb1 = 0, eb4 = 0, es1 = 0;
  gSlowBatchCarried = 0;
  std::vector<SlowRoundRecord> hc1, hc4, hs1, hc4n, hp1, hp4, hq1, hq4;
  auto c1 = run(1, ob1, eb1, 16, &hc1);
  long long carried1 = gSlowBatchCarried;
  auto c4 = run(4, ob4, eb4, 16, &hc4);
  auto s1 = run(1, os1, es1, 1, &hs1);
  gSlowBatchLpt = false;
  long long ob4n = 0;
  int eb4n = 0;
  auto c4n = run(4, ob4n, eb4n, 16, &hc4n);
  gSlowBatchLpt = true;
  check(sameRoutes(c4, c4n) && ob4 == ob4n && hc4 == hc4n,
        "batched slow tier: biggest-first work order = plain order, round for round");
  gSlowSpread = 8;
  long long sp1 = 0, sp4 = 0;
  int esp1 = 0, esp4 = 0;
  gSlowBatchCarried = 0;
  auto p1 = run(1, sp1, esp1, 16, &hp1);
  long long carriedSpread = gSlowBatchCarried;
  auto p4 = run(4, sp4, esp4, 16, &hp4);
  gSlowSpread = 0;
  gSlowConflict = false;
  long long nc1 = 0, nc4 = 0;
  int enc1 = 0, enc4 = 0;
  gSlowBatchCarried = 0;
  auto q1 = run(1, nc1, enc1, 16, &hq1);
  long long carriedNone = gSlowBatchCarried;
  auto q4 = run(4, nc4, enc4, 16, &hq4);
  gSlowConflict = true;
  check(carriedNone == 0 && sameRoutes(q1, q4) && nc1 == nc4 && hq1 == hq4,
        "batched slow tier, no conflict rule: nothing carried, 4 threads = 1 thread");
  check(sameRoutes(p1, p4) && sp1 == sp4 && hp1 == hp4,
        "batched slow tier, spread over 8x8 tiles: 4 threads = 1 thread, round for round");
  check(hp1 != hc1, "batched slow tier: spreading changes the slow rounds (control)");
  // Part 197: the two-ended search in the whole flow -- batched slow tier at 1 and 4 threads, round for round.
  {
    long long b1 = 0, b4 = 0, u1 = 0;
    int eb1_ = 0, eb4_ = 0, eu1 = 0;
    std::vector<SlowRoundRecord> hb1, hb4, hu1;
    auto plain = run(1, u1, eu1, 16, &hu1);
    const bool savedBidir = gSearchBidir;
    gSearchBidir = true;
    const long long s0 = gBidirSearches.load();
    auto t1 = run(1, b1, eb1_, 16, &hb1);
    const long long searches = gBidirSearches.load() - s0;
    auto t4 = run(4, b4, eb4_, 16, &hb4);
    gSearchBidir = savedBidir;
    {
      const bool savedPot = gSearchBidirPotentials;
      gSearchBidir = true;
      gSearchBidirPotentials = true; // Part 199
      long long c1p = 0, c4p = 0;
      int ec1 = 0, ec4 = 0;
      std::vector<SlowRoundRecord> hp1_, hp4_;
      auto v1 = run(1, c1p, ec1, 16, &hp1_);
      auto v4 = run(4, c4p, ec4, 16, &hp4_);
      gSearchBidir = savedBidir;
      gSearchBidirPotentials = savedPot;
      check(sameRoutes(v1, v4) && c1p == c4p && hp1_ == hp4_,
            "two-ended search with potentials in the batched slow tier: 4 threads = 1 thread, round for round (2D "
            "overflow " + std::to_string(c1p) + ")");
    }
    check(searches > 1000 && sameRoutes(t1, t4) && b1 == b4 && hb1 == hb4,
          "two-ended search in the batched slow tier: 4 threads = 1 thread, round for round (" +
              std::to_string(searches) + " searches; 2D overflow " + std::to_string(b1) + " vs " + std::to_string(u1) +
              " one-ended)");
    (void)plain;
  }
  // Part 193: PD39ROUTE_SLOW_SEARCHES. Batches of 4 nets that must also hold 6 nets needing a search: fewer
  // batches than batches of 4 alone, different rounds (control), 4 threads = 1 thread, replicas checked inside.
  {
    long long k0 = 0, k1 = 0, k4 = 0;
    int ek0 = 0, ek1 = 0, ek4 = 0;
    std::vector<SlowRoundRecord> hk0, hk1, hk4;
    gSlowBatchesRun = 0;
    auto r0 = run(1, k0, ek0, 4, &hk0);
    const long long batches0 = gSlowBatchesRun;
    gSlowBatchSearches = 6;
    gSlowBatchesRun = 0;
    auto r1 = run(1, k1, ek1, 4, &hk1);
    const long long batches1 = gSlowBatchesRun;
    auto r4 = run(4, k4, ek4, 4, &hk4);
    gSlowBatchSearches = 0;
    check(batches1 < batches0, "batched slow tier, at least 6 searches per batch: fewer batches (" +
                                   std::to_string(batches1) + " vs " + std::to_string(batches0) + ")");
    check(hk1 != hk0, "batched slow tier: a search quota changes the slow rounds (control)");
    check(sameRoutes(r1, r4) && k1 == k4 && hk1 == hk4,
          "batched slow tier, at least 6 searches per batch: 4 threads = 1 thread, round for round (2D overflow " +
              std::to_string(k1) + " vs " + std::to_string(k0) + " without)");
    // Part 195: the overflow gate -- closed throughout (below 0) is the run without a quota, open from the start
    // (below above any overflow) the ungated quota, both round for round.
    long long g0 = 0, g1 = 0;
    int eg0 = 0, eg1 = 0;
    std::vector<SlowRoundRecord> hg0, hg1;
    gSlowBatchSearches = 6;
    gSlowBatchSearchesBelow = 0;
    auto q0 = run(1, g0, eg0, 4, &hg0);
    gSlowBatchSearchesBelow = 1000000000;
    auto q1g = run(1, g1, eg1, 4, &hg1);
    gSlowBatchSearches = 0;
    gSlowBatchSearchesBelow = std::numeric_limits<long long>::max();
    check(sameRoutes(q0, r0) && g0 == k0 && hg0 == hk0,
          "search quota gated below 0 (never open): the slow rounds without a quota");
    check(sameRoutes(q1g, r1) && g1 == k1 && hg1 == hk1,
          "search quota gated above every overflow (open from round 0): the ungated quota");
  }
  check(carriedSpread < carried1, "batched slow tier: spread batches collide less (" + std::to_string(carriedSpread) +
                                      " carried vs " + std::to_string(carried1) + ")");
  check(sp1 <= std::max(os1 * 3 / 2, os1 + 10), "batched slow tier, spread: ends near batch 1 (2D overflow " +
                                                    std::to_string(sp1) + " vs " + std::to_string(os1) + ")");
  check(carried1 > 0, "batched slow tier: the conflict rule carried nets to the next batch (" +
                          std::to_string(carried1) + ")");
  check(sameRoutes(c1, c4) && ob1 == ob4 && eb1 == eb4 && hc1 == hc4,
        "batched slow tier (batch 16): 4 threads = 1 thread, round for round");
  check(hc1 != hs1, "batched slow tier: batch 16 differs from batch 1 (control: batching takes effect)");
  check(ob1 <= std::max(os1 * 3 / 2, os1 + 10), "batched slow tier: batch 16 ends near batch 1 (2D overflow " +
                                                    std::to_string(ob1) + " vs " + std::to_string(os1) + ")");
}

// pd37route: the commuting scheduler. Boxes in order: A (0..3, 0..3), B
// overlapping A, C disjoint from both, D overlapping B only, E overlapping
// A, B and D. Expected batches: A and C in 0, B in 1, D in 2, E in 3 -- each
// after every earlier overlapping box, no two overlapping in a batch.
void testCommutingBatches() {
  std::vector<NetBox> b = {{0, 0, 3, 3}, {2, 2, 6, 6}, {20, 20, 23, 23}, {6, 6, 9, 9}, {0, 0, 9, 7}};
  std::vector<std::vector<size_t>> got = commutingBatches(b, 32, 32, 1);
  std::vector<std::vector<size_t>> want = {{0, 2}, {1}, {3}, {4}};
  check(got == want, "commuting batches: each box after its earlier overlapping ones (A,C | B | D | E)");
  std::vector<std::vector<size_t>> coarse = commutingBatches(b, 32, 32, 8);
  check(coarse.size() >= got.size() && coarse[0].front() == 0,
        "commuting batches, coarse cells: never fewer batches, conservative");
}

// pd37route: the dependency graph runs every position after each earlier
// position whose box overlaps it (4 threads; each start and finish takes a
// ticket from one counter), on 300 random boxes; and runs every position.
void testCommutingDagOrder() {
  unsigned st = 99;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  std::vector<NetBox> b;
  for (int i = 0; i < 300; ++i) {
    int x = rnd(60), y = rnd(60);
    b.push_back({x, y, std::min(63, x + rnd(10)), std::min(63, y + rnd(10))});
  }
  std::atomic<int> ticket{0};
  std::vector<int> start(b.size(), -1), finish(b.size(), -1);
  runCommutingDag(b, 64, 64, 4, 4, [&](size_t k) {
    start[k] = ticket.fetch_add(1);
    volatile double sink = 0;
    for (int r = 0; r < 2000; ++r)
      sink = sink + r;
    finish[k] = ticket.fetch_add(1);
  });
  bool all = true, ordered = true;
  for (size_t i = 0; i < b.size(); ++i) {
    all = all && start[i] >= 0 && finish[i] > start[i];
    for (size_t j = i + 1; j < b.size(); ++j) {
      bool overlap = b[i].x0 <= b[j].x1 && b[j].x0 <= b[i].x1 && b[i].y0 <= b[j].y1 && b[j].y0 <= b[i].y1;
      if (overlap)
        ordered = ordered && finish[i] < start[j];
    }
  }
  check(all, "commuting DAG: every position runs once");
  check(ordered, "commuting DAG: each position starts after every earlier overlapping one finished");
}

// pd39route (Part 133): the timing layer bias. Classes from slack (worst
// -1: -0.8 -> 3, -0.5 -> 2, -0.2 -> 1, -0.05 and positive -> 0, NaN -> 0).
// Then layer assignment under the 2024 score on an 8 x 1 grid, layers H V H V,
// one 2-pin net along the row with both pins on layer 0: unbiased it stays on
// layer 0 (no vias); as a class-3 net with layer 2's resistance 1/100 of
// layer 0's and k = 10, the 7 tiles' delay-weighted length (35 on layer 0)
// outweighs two 2-via stacks (16), so it climbs to layer 2.
void testTimingLayerBiasPicksUpperMetal() {
  TimingLayerModel saved = gTiming;
  ScoreModel savedS = gScore;
  gTiming = TimingLayerModel{};
  gTiming.on = true;
  gTiming.slack = {-1.0f, -0.8f, -0.5f, -0.2f, -0.05f, 0.3f, std::numeric_limits<float>::quiet_NaN()};
  gTiming.relR = {1.0, 1.0, 0.01, 0.01};
  gTiming.k[1] = 1;
  gTiming.k[2] = 5;
  gTiming.k[3] = 10;
  double worst = gTiming.classify();
  check(worst == -1.0 && gTiming.cls == std::vector<uint8_t>({3, 3, 2, 1, 0, 0, 0}),
        "timing classes: slack / worst >= 0.7, 0.4, 0.1 -> 3, 2, 1; near-zero, positive, none -> 0");
  check(gTiming.bias[3][2] == 10 * 0.01 && gTiming.bias[1][0] == 1.0 && gTiming.bias[0][0] == 0.0,
        "timing bias table: k[class] x R_l / R_max");

  Design d = parse("pd39route_selftest_timing.gr", "grid 8 1 4\nvertical capacity 0 10 0 10\n"
                                                    "horizontal capacity 10 0 10 0\nminimum width 1 1 1 1\n"
                                                    "minimum spacing 0 0 0 0\nvia spacing 0 0 0 0\n0 0 10 10\n"
                                                    "num net 1\nn0 0 2 1\n5 5 1\n75 5 1\n0\n");
  const RoutingGrid &g = d.grid;
  gScore = ScoreModel{};
  gScore.on = true;
  gScore.w = {1, 1, 1, 1};
  gScore.unitVia = 4;
  gScore.unitLength = 0.5;
  gScore.capH.assign(g.hCount(), 5.0);
  gScore.capV.assign(g.vCount(), 5.0);
  gScore.xPitch.assign(g.nx - 1, 1);
  gScore.yPitch.assign(std::max(1, g.ny - 1), 1);
  RealLayerPlan lp(g);
  RealUsage ru(g);
  RealViaUsage rvu(g);
  std::vector<int> viaCap = viaCapacityPerLayerPair(g);
  std::vector<Seg> segs = {{0, 0, 0, 7, 0, 0}};
  NetTree legs, t;
  bool built = buildNetTree(segs, legs);
  splitToUnitEdges(legs, t);
  std::vector<std::pair<int, int>> pr(t.nodes.size(), {-1, -1});
  for (size_t u = 0; u < t.nodes.size(); ++u)
    if (t.nodes[u].x == 0 || t.nodes[u].x == 7)
      pr[u] = {0, 0};
  auto assign = [&](const double *bias) {
    tLayerBias = bias;
    std::vector<int> edgeLayer;
    int rootLayer = 0;
    std::vector<std::pair<int, int>> nsr;
    bool ok = layerAssignDP(g, ru, rvu, viaCap, lp, 1, t, edgeLayer, rootLayer, nsr, 1.0, nullptr, nullptr, nullptr,
                            /*nthuCost=*/true, &pr);
    tLayerBias = nullptr;
    std::set<int> layers(edgeLayer.begin(), edgeLayer.end());
    return ok ? layers : std::set<int>{-1};
  };
  check(built && t.edges.size() == 7, "timing fixture: a 7-edge row (sanity)");
  check(assign(nullptr) == std::set<int>{0}, "timing bias off: the net stays on its pins' layer 0");
  check(assign(gTiming.bias[3].data()) == std::set<int>{2},
        "timing bias, class 3: the net climbs to the low-resistance layer 2");
  check(assign(gTiming.bias[1].data()) == std::set<int>{0},
        "timing bias, class 1 (k = 1: 3.5 on layer 0 < 16 in vias): it stays on layer 0");
  // the gate: layer 2 full (10 half tracks used of 2 x 5 on every tile)
  for (int x = 0; x < 7; ++x)
    ru.h[g.hIndex(x, 0, 2)] = 10;
  tLayerBiasMax = gTiming.k[3];
  check(assign(gTiming.bias[3].data()) == std::set<int>{2},
        "timing bias, ungated: class 3 still climbs onto the full layer 2 (overflow)");
  gTiming.gate = true;
  tLayerBiasMax = gTiming.k[3];
  check(assign(gTiming.bias[3].data()) == std::set<int>{0},
        "timing bias, gated: no resistance advantage on a full edge, so it stays on layer 0");
  for (int x = 0; x < 7; ++x)
    ru.h[g.hIndex(x, 0, 2)] = 0;
  check(assign(gTiming.bias[3].data()) == std::set<int>{2}, "timing bias, gated, layer 2 with room: it climbs");
  tLayerBiasMax = 0;
  gTiming.gate = false;
  { // Part 205: the benefit weight -- per classed net, the timing term only, bounded, median net unchanged
    TimingLayerModel tb;
    tb.on = true;
    tb.slack = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 0.5f};
    tb.relR = {1.0, 0.01};
    tb.relC = {1.0, 0.5};
    tb.k[1] = 1, tb.k[2] = 5, tb.k[3] = 10;
    tb.kp = 2;
    tb.classify();
    tb.hpwl = {0, 4, 10, 40, 4000, 1000};
    tb.sinks = {1, 1, 1, 4, 60, 30};
    tb.benefitG = 1, tb.benefitM = 8, tb.benefitP = 6, tb.benefitQ = 0.5;
    tb.setBenefit(); // W = 6, 10, 16, 256, 261600 (the unclassed net has none); median (of five) 16
    check(tb.mult.size() == 6 && tb.benefitRef == 16 && tb.mult[2] == 1.0f && std::fabs(tb.mult[0] - 0.375f) < 1e-6 &&
              tb.mult[3] == 8.0f && tb.mult[4] == 8.0f && tb.mult[5] == 1.0f && tb.benefitHi == 2 && tb.benefitLo == 0,
          "benefit weight: (W / median)^g within [1/M, M] for classed nets, 1 for the rest");
    {
      TimingLayerModel u = tb;
      u.benefitQ = -1;
      u.setBenefit();
      double sum = 0, len = 0;
      for (size_t i = 0; i < 5; ++i) {
        sum += u.mult[i] * (u.hpwl[i] + 1.0);
        len += u.hpwl[i] + 1.0;
      }
      check(std::fabs(sum / len - 1.0) < 1e-4 && u.mult[0] < 1.0f && u.mult[4] > u.mult[0] && u.mult[5] == 1.0f,
            "benefit weight at an unchanged total pull: the wire-weighted mean multiplier is 1 (" +
                std::to_string(sum / len) + "), short nets below 1");
    }
    TimingLayerModel keep = gTiming;
    gTiming = tb;
    Net a, b;
    a.id = 0;
    b.id = 3;
    double ra0, ra1, rb0, rb1;
    {
      LayerBiasScope sa(&a);
      ra0 = tLayerBias[0], ra1 = tLayerBias[1];
    }
    {
      LayerBiasScope sb(&b);
      rb0 = tLayerBias[0], rb1 = tLayerBias[1];
    }
    // class 3: timing 10 x R, power 2 x C. Net 0 (x 0.375): 3.75 + 2, 0.0375 + 1; net 3 (x 8): 80 + 2, 0.8 + 1
    check(std::fabs(ra0 - 5.75) < 1e-9 && std::fabs(ra1 - 1.0375) < 1e-9 && std::fabs(rb0 - 82.0) < 1e-9 &&
              std::fabs(rb1 - 1.8) < 1e-9,
          "benefit weight: a net's row is its multiplier x the timing term plus the unscaled power term");
    gTiming.benefitG = 0;
    gTiming.setBenefit();
    {
      LayerBiasScope sb(&b);
      check(gTiming.mult.empty() && tLayerBias == gTiming.bias[3].data(), "benefit weight off: the class's own row");
    }
    gTiming = keep;
  }
  // robust classes: one -100 outlier among slacks -1 .. -0.1; against the
  // worst every other net is unclassed (ratio <= 0.01); against the 0.1
  // quantile (-1, after the outlier) they spread over the three classes
  // (values kept off the 0.7 / 0.4 / 0.1 boundaries: slacks are floats)
  gTiming.slack = {-100.0f, -1.0f, -0.95f, -0.85f, -0.75f, -0.65f, -0.5f, -0.45f, -0.35f, -0.25f, -0.15f, 0.5f};
  gTiming.refQ = -1;
  gTiming.classify();
  check(gTiming.counts[1] + gTiming.counts[2] == 0 && gTiming.counts[3] == 1,
        "timing classes against the worst: an outlier leaves every other net unclassed");
  gTiming.refQ = 0.1;
  double ref = gTiming.classify();
  check(ref == -1.0 && gTiming.cls[0] == 3 && gTiming.cls[1] == 3 && gTiming.cls[4] == 3 && gTiming.cls[5] == 2 &&
            gTiming.cls[7] == 2 && gTiming.cls[8] == 1 && gTiming.cls[10] == 1 && gTiming.cls[11] == 0,
        "timing classes against the 0.1 quantile (-1): -0.75 and worse 3, -0.65 .. -0.45 2, -0.35 .. -0.15 1");
  // the power term: an unclassed net, layer 2's capacitance 0.3 of the
  // most; kp = 10 makes 7 tiles of layer 0 cost 35 more vs 10.5 more on
  // layer 2, 24.5 > the vias' 16: it climbs; kp = 2 (4.9 < 16) it stays
  gTiming.refQ = -1;
  gTiming.relR.clear();
  gTiming.relC = {1.0, 1.0, 0.3, 0.3};
  gTiming.k[1] = gTiming.k[2] = gTiming.k[3] = 0;
  gTiming.slack = {0.5f};
  gTiming.kp = 10;
  gTiming.classify();
  check(gTiming.rowOn[0] && gTiming.bias[0][2] == 3.0, "power term: every net's row is k_p x C_l / max C");
  check(assign(gTiming.bias[0].data()) == std::set<int>{2}, "power term, kp 10: an unclassed net moves to low-C layer 2");
  gTiming.kp = 2;
  gTiming.classify();
  check(assign(gTiming.bias[0].data()) == std::set<int>{0}, "power term, kp 2: it stays on layer 0");
  // the calibration floor: k3 9.8 -> 50, the classes scaled together; k3
  // already above the floor, or no timing bias, untouched
  gTiming.k[1] = 2.45, gTiming.k[2] = 4.9, gTiming.k[3] = 9.8;
  gTiming.kMin = 50;
  gTiming.applyFloor();
  bool floorOk = std::fabs(gTiming.k[3] - 50) < 1e-9 && std::fabs(gTiming.k[1] - 12.5) < 1e-9;
  gTiming.k[1] = 40, gTiming.k[2] = 80, gTiming.k[3] = 160;
  gTiming.applyFloor();
  floorOk = floorOk && gTiming.k[3] == 160;
  gTiming.k[1] = gTiming.k[2] = gTiming.k[3] = 0;
  gTiming.applyFloor();
  floorOk = floorOk && gTiming.k[3] == 0;
  check(floorOk, "timing floor: k3 below the floor is raised to it (classes together); above it, or zero, unchanged");
  gTiming = saved;
  gScore = savedS;
}

// pd39route (Part 135): the Lagrangian step (over capacity: up by alpha x relative excess; under: down,
// never below 0), its cost term (lambda x demand, nothing without multipliers), the portfolio's variant
// parser and pick (least overflow, then wire, then earliest), and on the three-nets-on-one-row fixture:
// the region stage with multipliers still reaches zero with every net connected, and a portfolio of
// variants keeps the best of what each variant reaches on its own.
void testLagrangeAndPortfolio() {
  std::vector<double> lam = {0.0, 0.5, 0.1};
  std::vector<int> use = {12, 5, 5}, cap = {10, 10, 10};
  advanceLagrange(lam, use, cap, 1.0);
  check(std::fabs(lam[0] - 0.2) < 1e-12 && lam[1] == 0.0 && lam[2] == 0.0,
        "Lagrangian step: +0.2 on an edge 20% over; down to 0 (not below) on edges under capacity");
  RegionSettings rs;
  std::string err;
  check(applyRegionVariant("vcap=0,boxcap=150,lagr=0.5,rounds=40", rs, err) && rs.vcapTracks == 0 &&
            rs.boxCap == 150 && rs.lagrange == 0.5 && rs.rounds == 40,
        "portfolio variant parse: vcap, boxcap, lagr, rounds");
  check(!applyRegionVariant("vcap=-1", rs, err) && !applyRegionVariant("bogus=1", rs, err) &&
            !applyRegionVariant("rounds=0", rs, err) && !applyRegionVariant("vcap", rs, err) &&
            !applyRegionVariant("boxinit=1.5", rs, err),
        "portfolio variant parse: negative, unknown key, zero rounds, missing value, fractional integer refused");
  std::vector<PortfolioResult> pr(3);
  pr[0].overflow = 5, pr[0].wire = 100;
  pr[1].overflow = 3, pr[1].wire = 120;
  pr[2].overflow = 3, pr[2].wire = 110;
  check(pickPortfolio(pr) == 2, "portfolio pick: least overflow, then least wire");
  pr[2].wire = 120;
  check(pickPortfolio(pr) == 1, "portfolio pick: a full tie goes to the earliest");
  {
    // Part 165: a variant's batched rounds take the machine's cores over the variants still running, at most
    // PD37ROUTE_THREADS, and get more as the others finish; outside a portfolio, PD37ROUTE_THREADS.
    const int savedT = gThreads;
    gThreads = 8;
    ThreadShare sh;
    ThreadShare *const savedS = tShare;
    const int none = batchThreads();
    tShare = &sh;
    sh.running.store(3);
    const int three = batchThreads();
    sh.running.store(1);
    const int one = batchThreads();
    tShare = savedS;
    gThreads = savedT;
    const int c = shareCores();
    check(none == 8 && three == std::max(1, std::min(8, c / 3)) && one == std::min(8, c) && one >= three,
          "portfolio thread share: cores / variants running, at most the thread setting, more as variants finish");
  }

  Design d = parse("pd39route_selftest_lagrange.gr", kThreeOnOneRowGr);
  auto fresh = [&]() {
    std::vector<RoutedNet> nets(3);
    for (int i = 0; i < 3; ++i) {
      nets[i].net = &d.nets[i];
      nets[i].segs = {{0, 1, 0, 7, 1, 0}};
    }
    prepareFor2DRefine(d, nets);
    return nets;
  };
  RegionSettings lr;
  lr.enabled = true;
  lr.lagrange = 0.5;
  std::vector<RoutedNet> nl = fresh();
  RegionReport rl = regionNegotiate(d, nl, lr, false);
  bool conn = true;
  for (const RoutedNet &rn : nl)
    conn = conn && segsConnected(rn.segs);
  check(rl.after == 0 && conn, "region stage with multipliers: zero overflow, every net connected");
  // portfolio: variant 0 (one round) cannot finish; the variant with the default rounds can
  RegionSettings one;
  one.enabled = true;
  one.rounds = 1;
  RefineSettings noRefine;
  noRefine.enabled = false;
  std::vector<RoutedNet> alone = fresh();
  RegionReport ra = regionNegotiate(d, alone, one, false);
  std::vector<std::string> saved = gPortfolio;
  gPortfolio = {"rounds=60"};
  std::vector<RoutedNet> np = fresh();
  FallbackReport fr;
  runPathA(d, np, one, noRefine, false, fr);
  gPortfolio = saved;
  check(fr.portfolio.size() == 2 && fr.portfolio[0].overflow == ra.after &&
            fr.portfolioPick == (ra.after > 0 ? 1u : 0u) &&
            totalOverflow(d.grid, usageFromCharges(d.grid, np)) == fr.portfolio[fr.portfolioPick].overflow,
        "portfolio: variant 0 matches the stage run alone, and the kept routes are the best variant's");
}

// pd39route (Part 134): the routed-slack file: listed nets get their value, the rest none; a bad
// index, a non-number or a trailing field is refused.
void testSlackOverrideFile() {
  const std::string path = "pd39route_selftest_slack.txt";
  auto write = [&](const char *text) {
    std::ofstream f(path);
    f << text;
  };
  std::vector<float> v;
  std::string err;
  write("# routed slack\n0 -2.5e-10\n3 1e-11\n");
  bool ok = loadSlackOverride(path, 4, v, err) && v.size() == 4 && v[0] == -2.5e-10f && v[1] != v[1] &&
            v[2] != v[2] && v[3] == 1e-11f;
  write("4 -1e-10\n");
  bool badIdx = !loadSlackOverride(path, 4, v, err);
  write("1 x\n");
  bool badNum = !loadSlackOverride(path, 4, v, err);
  write("1 -1e-10 extra\n");
  bool badExtra = !loadSlackOverride(path, 4, v, err);
  std::remove(path.c_str());
  check(ok, "routed slack file: listed nets get their slack, unlisted none");
  check(badIdx && badNum && badExtra, "routed slack file: an index out of range, a non-number, a trailing field refused");
}

// Part 210: the price-seed file: listed edges get their price, the rest 0; a bad direction, an edge off the grid,
// a negative or non-finite price, or a trailing field is refused; a seeded edge's present-demand cost rises by
// scale x price x amount, an unseeded one is unchanged.
void testPriceSeed() {
  RoutingGrid g;
  g.nx = 4;
  g.ny = 3;
  g.nLayers = 1;
  const std::string path = "pd39route_selftest_seed.txt";
  auto write = [&](const char *text) {
    std::ofstream f(path);
    f << text;
  };
  std::vector<float> h, v;
  std::string err;
  size_t listed = 0;
  write("# seed\nH 2 1 0.5\nV 3 1 2\n");
  bool ok = loadPriceSeed(path, g, h, v, listed, err) && h.size() == 9 && v.size() == 8 && listed == 2 &&
            h[g.hIndex(2, 1, 0)] == 0.5f && v[g.vIndex(3, 1, 0)] == 2.0f && h[0] == 0.0f;
  bool bad = true;
  for (const char *text : {"H 3 0 1\n", "V 0 2 1\n", "D 0 0 1\n", "H 0 0 -1\n", "H 0 0 nan\n", "H 0 0 1 x\n",
                           "H -1 0 1\n", "H 0 0\n"}) {
    write(text);
    bad = bad && !loadPriceSeed(path, g, h, v, listed, err);
  }
  std::remove(path.c_str());
  check(ok, "price seed file: listed edges get their price, the rest 0");
  check(bad, "price seed file: off-grid edges, a bad direction, a negative / NaN price, a trailing or missing field refused");
  // the cost seam: a seeded edge pays scale x price x amount more, an unseeded one nothing
  Design d = parse("pd1route_selftest_basic.gr", kBasicGr);
  const RoutingGrid &gb = d.grid;
  Prices pr(gb);
  Usage u(gb);
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  configureCostModel(pr, u, cs);
  const size_t eh = gb.hIndex(1, 0, 0), eh2 = gb.hIndex(0, 0, 0), ev = gb.vIndex(0, 0, 0);
  const double h0 = wireCostH(pr, eh, 2), h20 = wireCostH(pr, eh2, 2), v0 = wireCostV(pr, ev, 2);
  gSeedH.assign(u.h.size(), 0.0f);
  gSeedV.assign(u.v.size(), 0.0f);
  gSeedH[eh] = 0.25f;
  gSeedV[ev] = 1.0f;
  gSeedScale = 4.0;
  const long long reads = gSeedReads.load();
  const double h1 = wireCostH(pr, eh, 2), h21 = wireCostH(pr, eh2, 2), v1 = wireCostV(pr, ev, 2);
  const bool counted = gSeedReads.load() == reads + 2;
  gSeedH.clear();
  gSeedV.clear();
  gSeedScale = 1.0;
  check(std::fabs(h1 - (h0 + 2.0)) < 1e-12 && std::fabs(v1 - (v0 + 8.0)) < 1e-12 && h21 == h20 && counted,
        "price seed: a seeded edge's cost rises by scale x price x amount (h +2, v +8), an unseeded one is unchanged");
  check(wireCostH(pr, eh, 2) == h0, "price seed off: the cost is the inherited one exactly");
}

// pd39route (Part 212): PD39ROUTE_SKIP_GIANT -- dropGiantNets removes exactly the nets above kGiantNetPins and
// records their ids, and a PD3ROUTE_LA_FROM route that still holds a dropped net has that block skipped (not a
// failure) while every other net is read as before.
void testSkipGiant() {
  std::string gr = "grid 4 2 1\nvertical capacity 10\nhorizontal capacity 1\nminimum width 1\nminimum spacing 0\n"
                   "via spacing 0\n0 0 1 1\nnum net 2\n";
  gr += "giant 1 1006 1\n";
  for (int i = 0; i < 1005; ++i)
    gr += "0 0 1\n";
  gr += "3 1 1\nblocker 2 2 1\n1 0 1\n2 0 1\n0\n";
  Design d = parse("pd39route_selftest_skipgiant.gr", gr.c_str());
  const bool before = d.nets.size() == 2;
  std::set<long long> ids;
  const size_t k = dropGiantNets(d, ids);
  check(before && k == 1 && ids == std::set<long long>{1} && d.nets.size() == 1 && d.nets[0].name == "blocker",
        "skip giant nets: the 1006-pin net is dropped and its id recorded, the 2-pin net kept");
  const std::string path = "pd39route_selftest_skipgiant.route";
  {
    std::ofstream f(path);
    f << "giant 1 2\n(0,0,1)-(3,0,1)\n(3,0,1)-(3,1,1)\n!\nblocker 2 1\n(1,0,1)-(2,0,1)\n!\n";
  }
  gSkippedIds = ids;
  std::vector<RoutedNet> rn = readRouteAs2D(d, path);
  gSkippedIds.clear();
  std::remove(path.c_str());
  check(rn.size() == 1 && rn[0].net == &d.nets[0] && rn[0].segs.size() == 1,
        "skip giant nets: a route file holding the dropped net reads as the other nets alone");
  Design d2 = parse("pd39route_selftest_skipgiant.gr", gr.c_str());
  std::set<long long> none;
  bool small = true;
  for (Net &n : d2.nets)
    if (n.name == "giant")
      n.pins.resize(1000); // exactly the threshold: not exempt
  small = dropGiantNets(d2, none) == 0 && d2.nets.size() == 2;
  check(small, "skip giant nets: a net of exactly 1000 pins is not exempt and stays");
}

// Part 214: the stage report's score sums each layer's own-direction plane only. Both planes carry the layer's
// capacity (as the ISPD 2024 reader fills them); with no demand anywhere the score is the idle floor w x exp(-c/2) of
// the own-direction edges -- the old loop added the other plane's floor too (2x on an idle layer).
void testRouterScoreOnePlane() {
  Design d = parse("pd39route_selftest_onelane.gr", "grid 4 3 3\nvertical capacity 0 0 10\n"
                                                     "horizontal capacity 0 10 0\nminimum width 1 1 1\n"
                                                     "minimum spacing 0 0 0\nvia spacing 0 0 0\n0 0 10 10\n"
                                                     "num net 0\n0\n");
  const RoutingGrid &g = d.grid;
  const ScoreModel savedScore = gScore;
  const std::vector<int> savedDir = gViaDemand.dir;
  gScore = ScoreModel{};
  gScore.on = true;
  gScore.w = {1, 1, 1};
  gScore.capH.assign(g.hCount(), 0.0);
  gScore.capV.assign(g.vCount(), 0.0);
  for (int z = 1; z < 3; ++z) { // both planes of layers 1-2 get capacity 4, as the reader fills them
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        gScore.capH[g.hIndex(x, y, z)] = 4;
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        gScore.capV[g.vIndex(x, y, z)] = 4;
  }
  gViaDemand.dir = {-1, 0, 1}; // layer 1 horizontal, layer 2 vertical
  RealUsage ru(g);
  std::vector<RoutedNet> nets;
  const double got = routerScore(g, ru, nets);
  const double floor1 = 3.0 * 3.0 * std::exp(-2.0); // layer 1: 3 rows x 3 H edges, exp(0.5 (0 - 8) / 2)
  const double floor2 = 2.0 * 4.0 * std::exp(-2.0); // layer 2: 2 rows of V edges x 4 columns
  gScore = savedScore;
  gViaDemand.dir = savedDir;
  check(std::fabs(got - (floor1 + floor2)) < 1e-9,
        "router score: each layer's own-direction plane only (" + std::to_string(got) + " vs " +
            std::to_string(floor1 + floor2) + ")");
}

// pd39route (Part 207): the per-net multiplier file: listed nets get their value, the rest 1; an index out of
// range, a non-number, zero, a negative, a value above 1e6 or a trailing field is refused.
void testTimingMultFile() {
  const std::string path = "pd39route_selftest_mult.txt";
  auto write = [&](const char *text) {
    std::ofstream f(path);
    f << text;
  };
  std::vector<float> v;
  std::string err;
  size_t listed = 0;
  write("# driver strength\n0 0.25\n3 4\n");
  bool ok = loadTimingMult(path, 4, v, listed, err) && v.size() == 4 && v[0] == 0.25f && v[1] == 1.0f && v[2] == 1.0f &&
            v[3] == 4.0f && listed == 2;
  bool bad = true;
  for (const char *text : {"4 2\n", "1 x\n", "1 0\n", "1 -2\n", "1 2e6\n", "1 2 extra\n", "1 nan\n"}) {
    write(text);
    bad = bad && !loadTimingMult(path, 4, v, listed, err);
  }
  std::remove(path.c_str());
  check(ok, "timing multiplier file: listed nets get their multiplier, unlisted 1");
  check(bad, "timing multiplier file: an index out of range, a non-number, 0, a negative, above 1e6, a trailing field, NaN refused");
  check(!loadTimingMult("pd39route_selftest_no_such_file.txt", 4, v, listed, err), "timing multiplier file: a missing file refused");
}

// Part 215: PD39ROUTE_TIMING_ROWS -- the file's rows reach the nets listed, the rest keep their class rows.
void testTimingRowsFile() {
  const std::string path = "pd39route_selftest_rows.txt";
  auto write = [&](const char *text) {
    std::ofstream f(path);
    f << text;
  };
  std::vector<int32_t> rowOf;
  std::vector<double> vals;
  std::string err;
  write("# elmore\n2 1 0.5 0\n0 0 0 3\n");
  bool ok = loadTimingRows(path, 4, 3, rowOf, vals, err) && rowOf == std::vector<int32_t>({1, -1, 0, -1}) &&
            vals == std::vector<double>({1, 0.5, 0, 0, 0, 3});
  bool bad = true;
  for (const char *text : {"4 1 1 1\n", "1 1 1\n", "1 1 1 1 1\n", "1 1 x 1\n", "1 1 -1 1\n", "1 1 2e6 1\n",
                           "1 1 nan 1\n", "1 1 1 1\n1 2 2 2\n"}) {
    write(text);
    bad = bad && !loadTimingRows(path, 4, 3, rowOf, vals, err);
  }
  std::remove(path.c_str());
  check(ok, "timing rows file: listed nets get their row, the rest none");
  check(bad, "timing rows file: index out of range, too few / many values, a non-number, a negative, above 1e6, NaN, "
             "a net listed twice refused");
  check(!loadTimingRows("pd39route_selftest_no_such_file.txt", 4, 3, rowOf, vals, err), "timing rows file: a missing file refused");
  TimingLayerModel keep = gTiming;
  gTiming = TimingLayerModel{};
  gTiming.on = true;
  gTiming.slack = {-1.0f, -1.0f};
  gTiming.relR = {1.0, 0.5, 0.01};
  gTiming.k[3] = 10;
  gTiming.classify();
  gTiming.rowOf = {-1, 0};
  gTiming.rowVals = {2.0, 7.0, 1.0};
  gTiming.rowL = 3;
  Net a, b;
  a.id = 0;
  b.id = 1;
  {
    LayerBiasScope sa(&a);
    check(tLayerBias == gTiming.bias[3].data() && tLayerBiasMax == 10.0, "timing rows: an unlisted net keeps its class row");
  }
  {
    LayerBiasScope sb(&b);
    check(tLayerBias && tLayerBias[0] == 2.0 && tLayerBias[1] == 7.0 && tLayerBias[2] == 1.0 && tLayerBiasMax == 7.0,
          "timing rows: a listed net gets its own row and its row's maximum");
  }
  check(tLayerBias == nullptr, "timing rows: the scope restores the previous row");
  gTiming = keep;
}

// pd39route (Part 133): buildNetTree equals buildNetTreeReference -- nodes
// (order included), edges, adjacency and the return value -- on 3000 random
// nets: rectilinear walks with repeated and overlapping legs (cycles), a few
// layer-change segments, pins on and off the wire, with and
// without dropCycles, from 1 to ~1500 legs.
void testBuildNetTreeMatchesReference() {
  unsigned st = 2026;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  bool same = true, sawCycleFail = false, sawPinNode = false;
  int trials = 0;
  for (int trial = 0; trial < 3000; ++trial) {
    const int span = 8 + rnd(60);
    const int legsN = trial % 100 == 0 ? 500 + rnd(1000) : 1 + rnd(40);
    std::vector<Seg> segs;
    int x = rnd(span), y = rnd(span);
    for (int k = 0; k < legsN; ++k) {
      const int kind = rnd(20);
      if (kind == 0) { // a via (dropped as a leg)
        segs.push_back({x, y, 0, x, y, 1});
        continue;
      }
      if (kind == 1) { // a jump: a new start, maybe disconnected
        x = rnd(span), y = rnd(span);
        continue;
      }
      if (kind == 2 && !segs.empty()) { // repeat an earlier leg (a cycle)
        segs.push_back(segs[static_cast<size_t>(rnd(static_cast<int>(segs.size())))]);
        continue;
      }
      int nx = x, ny = y;
      if (rnd(2))
        nx = rnd(span);
      else
        ny = rnd(span);
      segs.push_back({x, y, 0, nx, ny, 0});
      x = nx, y = ny;
    }
    std::vector<Point> pins;
    for (int k = rnd(12); k-- > 0;) {
      if (!segs.empty() && rnd(2)) { // on a segment's end or interior
        const Seg &sg = segs[static_cast<size_t>(rnd(static_cast<int>(segs.size())))];
        if (sg.y1 == sg.y2)
          pins.push_back({std::min(sg.x1, sg.x2) + rnd(std::abs(sg.x2 - sg.x1) + 1), sg.y1});
        else
          pins.push_back({sg.x1, std::min(sg.y1, sg.y2) + rnd(std::abs(sg.y2 - sg.y1) + 1)});
      } else {
        pins.push_back({rnd(span), rnd(span)});
      }
    }
    for (int mode = 0; mode < 4; ++mode) {
      const bool drop = mode & 1;
      const std::vector<Point> *pp = (mode & 2) ? &pins : nullptr;
      NetTree a, b;
      bool ra = buildNetTree(segs, a, drop, pp), rb = buildNetTreeReference(segs, b, drop, pp);
      bool eq = ra == rb && a.nodes.size() == b.nodes.size() && a.edges.size() == b.edges.size() &&
                a.adj == b.adj;
      for (size_t i = 0; eq && i < a.nodes.size(); ++i)
        eq = a.nodes[i].x == b.nodes[i].x && a.nodes[i].y == b.nodes[i].y;
      for (size_t i = 0; eq && i < a.edges.size(); ++i) {
        const NetTree::Edge &e = a.edges[i], &f = b.edges[i];
        eq = e.a == f.a && e.b == f.b && e.horizontal == f.horizontal && e.cross == f.cross && e.lo == f.lo &&
             e.hi == f.hi;
      }
      same = same && eq;
      sawCycleFail = sawCycleFail || (!drop && !rb);
      if (pp) {
        NetTree c;
        buildNetTreeReference(segs, c, drop, nullptr);
        sawPinNode = sawPinNode || c.nodes.size() < b.nodes.size();
      }
      ++trials;
    }
  }
  check(same, "tree builder (O(n log n)) = the reference on " + std::to_string(trials) +
                  " random nets: nodes in order, edges, adjacency, result");
  check(sawCycleFail && sawPinNode, "tree builder fixtures cover a rejected cycle and a pin that adds a node");
}

// pd39route (Part 132): critical-path-first scheduling. A hand-checked
// graph -- 0 -> 1 -> 3, 0 -> 2, weights 1, 5, 1, 2 -- has bottom levels
// 8, 7, 1, 2; the replay on 1 thread runs every task (finish = total work);
// and the 300-box fixture under priorities still runs each position after
// every earlier overlapping one.
void testCriticalPathFirstDag() {
  CommutingDagPlan p;
  p.n = 4;
  p.succStart = {0, 2, 3, 3, 3};
  p.succ = {1, 2, 3};
  p.indeg = {0, 1, 1, 1};
  const std::vector<double> w = {1, 5, 1, 2};
  setCriticalPathPriority(p, [&](size_t k) { return w[k]; });
  check(p.prio == std::vector<float>({8, 7, 1, 2}), "critical path first: bottom levels 8, 7, 1, 2");
  DagReadyOrder o{p.prio.data()};
  check(o(2, 1) && !o(1, 2), "critical path first: the larger bottom level runs first");
  std::vector<float> t = {1, 5, 1, 2};
  check(simulateDagSchedule(p, t, 1, o) == 9.0f, "schedule replay, 1 thread: finishes at the total work (9)");
  check(simulateDagSchedule(p, t, 2, o) == 8.0f, "schedule replay, 2 threads: finishes at the critical path (8)");
  unsigned st = 77;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  std::vector<NetBox> b;
  for (int i = 0; i < 300; ++i) {
    int x = rnd(60), y = rnd(60);
    b.push_back({x, y, std::min(63, x + rnd(10)), std::min(63, y + rnd(10))});
  }
  CellsOfFn cellsOf = [&](size_t k, std::vector<uint32_t> &out) {
    for (int cy = b[k].y0 / 4; cy <= b[k].y1 / 4; ++cy)
      for (int cx = b[k].x0 / 4; cx <= b[k].x1 / 4; ++cx)
        out.push_back(static_cast<uint32_t>(cy * 16 + cx));
  };
  std::function<double(size_t)> weight = [&](size_t k) { return 1.0 + (k * 7919) % 13; };
  std::atomic<int> ticket{0};
  std::vector<int> start(b.size(), -1), finish(b.size(), -1);
  runCommutingDagFn(b.size(), 256, cellsOf, 4, [&](size_t k) {
    start[k] = ticket.fetch_add(1);
    volatile double sink = 0;
    for (int r = 0; r < 2000; ++r)
      sink = sink + r;
    finish[k] = ticket.fetch_add(1);
  }, &weight);
  bool all = true, ordered = true;
  for (size_t i = 0; i < b.size(); ++i) {
    all = all && start[i] >= 0 && finish[i] > start[i];
    for (size_t j = i + 1; j < b.size(); ++j) {
      bool overlap = b[i].x0 <= b[j].x1 && b[j].x0 <= b[i].x1 && b[i].y0 <= b[j].y1 && b[j].y0 <= b[i].y1;
      if (overlap)
        ordered = ordered && finish[i] < start[j];
    }
  }
  check(all && ordered, "critical path first: every position runs once, after every earlier overlapping one");
}

// pd37route: threaded layer assignment equals the sequential one byte for
// byte -- 2008 cost (NTHU) and the 2024 score with via demand -- on a
// congested 48 x 48 x 4 fixture with 400 random nets, at 1 and 4 threads.
// Control: the same nets taken in reverse, ignoring overlap, must route
// differently, or the fixture would not show that order matters.
void testThreadedLayerAssignmentIsExact() {
  std::string gr = "grid 48 48 4\nvertical capacity 0 2 0 2\nhorizontal capacity 2 0 2 0\n"
                   "minimum width 1 1 1 1\nminimum spacing 1 1 1 1\nvia spacing 0 0 0 0\n0 0 10 10\nnum net 400\n";
  unsigned st = 4242;
  auto rnd = [&](int n) {
    st = st * 1103515245u + 12345u;
    return static_cast<int>((st >> 8) % static_cast<unsigned>(n));
  };
  for (int i = 0; i < 400; ++i) {
    int np = 2 + rnd(3), cx = rnd(48), cy = rnd(48);
    gr += "n" + std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(np) + " 1\n";
    for (int k = 0; k < np; ++k) {
      int x = std::min(47, std::max(0, cx + rnd(13) - 6)), y = std::min(47, std::max(0, cy + rnd(13) - 6));
      gr += std::to_string(x * 10 + 5) + " " + std::to_string(y * 10 + 5) + " 1\n";
    }
  }
  gr += "0\n";
  Design d = parse("pd37route_selftest_la.gr", gr.c_str());
  CostSettings cs;
  cs.model = CostModel::kPresentDemand;
  PatternStageSettings ps;
  ps.rounds = 3;
  RunReport base = route(d, 60, 0, 0.02, 0.05, HistMode::kAdditive, 1.1, 1, false, true, 1.0, 0.0, true, true, true,
                         8, BoxGrowth{}, cs, AcceptMode::kKeep, false, -1, &ps);
  const RoutingGrid &g = d.grid;
  auto run = [&](int threads, bool ignoreOverlap) {
    std::vector<RoutedNet> nets = base.nets;
    int savedT = gThreads;
    gThreads = threads;
    gCommuteTestIgnoreOverlap = ignoreOverlap;
    negotiateViaAssignment(g, nets, 0, 20, 1.0, HistKnobs(), HistKnobs(), LaMode::kNthu, true);
    gThreads = savedT;
    gCommuteTestIgnoreOverlap = false;
    std::vector<std::vector<Seg>> out;
    for (const RoutedNet &rn : nets)
      out.push_back(rn.segs);
    return out;
  };
  auto same = [](const std::vector<std::vector<Seg>> &a, const std::vector<std::vector<Seg>> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].size() != b[i].size())
        return false;
      for (size_t j = 0; j < a[i].size(); ++j) {
        const Seg &p = a[i][j], &q = b[i][j];
        if (p.x1 != q.x1 || p.y1 != q.y1 || p.l1 != q.l1 || p.x2 != q.x2 || p.y2 != q.y2 || p.l2 != q.l2)
          return false;
      }
    }
    return true;
  };
  Pattern3DSettings savedP = gPattern3D;
  gPattern3D.passes = 2; // the 3D passes run after round 0, threaded too
  gPattern3D.mode2008 = true;
  auto seq = run(1, false), par = run(4, false), rev = run(4, true);
  check(same(seq, par), "threaded layer assignment + 3D passes (2008 cost): 4 threads = 1 thread, byte for byte");
  gLaCrit = 1; // Part 132: critical path first changes the order only
  auto crit = run(4, false);
  gLaCrit = 0;
  check(same(seq, crit), "threaded layer assignment, critical path first (2008 cost): 4 threads = 1 thread");
  check(!same(seq, rev), "control: reversed order, overlap ignored, routes differently (the order matters)");
  // the 2024 score with via demand on the same fixture
  ViaDemandModel savedV = gViaDemand;
  ScoreModel savedS = gScore;
  gViaDemand.on = true;
  gViaDemand.dir = {-1, 1, 0, 1};
  gScore.on = true;
  gScore.w = {5, 5, 5, 5};
  gScore.unitVia = 4;
  gScore.capH.assign(g.hCount(), 0.0);
  gScore.capV.assign(g.vCount(), 0.0);
  gScore.capIdxH.clear();
  gScore.capIdxV.clear();
  gScore.lossTable.clear();
  gScore.unitLength = 0.5;
  gScore.xPitch.assign(g.nx - 1, 1);
  gScore.yPitch.assign(g.ny - 1, 1);
  gPattern3D.mode2008 = false;
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x + 1 < g.nx; ++x)
      gScore.capH[g.hIndex(x, y, 2)] = 1;
  for (int y = 0; y + 1 < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      gScore.capV[g.vIndex(x, y, 1)] = 1;
      gScore.capV[g.vIndex(x, y, 3)] = 1;
    }
  auto seqS = run(1, false), parS = run(4, false), revS = run(4, true);
  check(same(seqS, parS), "threaded layer assignment + 3D passes (2024 score, via demand): 4 threads = 1 thread");
  {
    // pd38route: PD38ROUTE_P3D_SKIP -- three passes, with and without skipping
    // unchanged nets, at 1 and 4 threads: the same routes, and nets skipped.
    const int savedPasses = gPattern3D.passes;
    gPattern3D.passes = 3;
    auto ref = run(1, false);
    gP3dSkip = true;
    gP3dSkippedTotal = 0;
    auto sk1 = run(1, false);
    long long skipped1 = gP3dSkippedTotal;
    auto sk4 = run(4, false);
    gP3dReuse = true;
    auto skr4 = run(4, false);
    gP3dSkip = false;
    auto r4 = run(4, false);
    gP3dReuse = false;
    gP3dSkip = true;
    gP3dSkipTestAll = true;
    auto wrong = run(1, false);
    gP3dSkipTestAll = false;
    gP3dSkip = false;
    gPattern3D.passes = savedPasses;
    check(skipped1 > 0, "3D passes, skipping unchanged nets: some nets skipped (" + std::to_string(skipped1) + ")");
    check(same(ref, sk1) && same(ref, sk4), "3D passes, skipping unchanged nets: the same routes at 1 and 4 threads");
    check(!same(ref, wrong), "control: skipping every net after the first pass routes differently (later passes matter)");
    check(same(ref, r4) && same(ref, skr4), "3D passes on the first pass's graph, with and without skipping: the same routes");
  }
  check(!same(seqS, revS), "control (2024 score): reversed order routes differently");
  gViaDemand = savedV;
  gScore = savedS;
  gPattern3D = savedP;
}

// pd36route: the stage report counts wire and vias from segments (a 3-unit
// run, a 2-unit run, a 2-layer via), keeps rows in order, and prints each
// objective's change from the previous one (100 -> 90: -10, -10%).
void testStageReport() {
  StageLog saved = gStages;
  gStages = StageLog{};
  gStages.begin("score");
  std::vector<RoutedNet> nets(1);
  nets[0].segs = {{0, 0, 1, 3, 0, 1}, {3, 0, 0, 3, 0, 2}, {3, 0, 2, 3, 2, 2}};
  gStages.mark("a", &nets, 7, 100.0, "w1");
  gStages.mark("b", nullptr, -1, 90.0);
  std::ostringstream out;
  gStages.print(out);
  const std::string t = out.str();
  check(gStages.rows.size() == 2 && gStages.rows[0].wire == 5 && gStages.rows[0].vias == 2 &&
            gStages.rows[0].overflow2D == 7 && gStages.rows[1].wire == -1,
        "stage report: wire 5 and vias 2 from the segments, rows in order");
  check(t.find("| 100 | - | w1") != std::string::npos && t.find("| 90 | -10 (-10.000%) |") != std::string::npos,
        "stage report: the change column is the objective's difference from the previous row");
  gStages = saved;
}

// pd36route: the loss table returns bit for bit what g24Loss computes,
// through scoreMarginal, on every edge of a 7 x 3 x 5 grid with mixed
// capacity indices, for demands inside the table and past it.
void testScoreLossTableIsExact() {
  const char *gr = "grid 7 3 5\nvertical capacity 0 0 40 0 40\nhorizontal capacity 0 4 0 40 0\n"
                   "minimum width 1 1 1 1 1\nminimum spacing 1 1 1 1 1\nvia spacing 0 0 0 0 0\n"
                   "0 0 10 10\nnum net 0\n0\n";
  Design d = parse("pd36route_selftest_loss.gr", gr);
  const RoutingGrid &g = d.grid;
  ScoreModel saved = gScore;
  gScore.w = {5, 4, 3, 2, 1};
  gScore.capH.clear();
  gScore.capV.clear();
  gScore.capTable = {0.0, 2.0, 6.15, 9.78571, 20.0};
  gScore.capIdxH.assign(g.hCount(), 0);
  gScore.capIdxV.assign(g.vCount(), 0);
  for (size_t i = 0; i < g.hCount(); ++i)
    gScore.capIdxH[i] = static_cast<uint8_t>(i % 5);
  for (size_t i = 0; i < g.vCount(); ++i)
    gScore.capIdxV[i] = static_cast<uint8_t>((i * 3) % 5);
  buildLossTable();
  bool built = gScore.lossTable.size() == 5 * (ScoreModel::kLossMaxDh + 1);
  bool exact = true;
  for (int hv = 0; hv < 2; ++hv) {
    bool h = hv == 0;
    size_t n = h ? g.hCount() : g.vCount();
    for (size_t idx = 0; idx < n; ++idx)
      for (long long cur : {0LL, 1LL, 7LL, 40LL, 509LL, 510LL, 511LL, 700LL})
        for (int amount : {1, 2, 3, 6}) {
          double c = gScore.capOf(h, idx);
          double want = gScore.w[edgeLayerOf(g, h, idx)] * (g24Loss(cur + amount, c) - g24Loss(cur, c));
          exact = exact && scoreMarginal(g, h, idx, cur, amount) == want &&
                  edgeLoss(h, idx, cur) == g24Loss(cur, c);
        }
  }
  check(built, "loss table: one row of kLossMaxDh + 1 values per capacity");
  check(exact, "loss table: scoreMarginal bit for bit equal to the direct exp, in and past the table");
  gScore = saved;
}

// pd35route (memory): the 1-byte capacity table returns exactly the doubles
// a dense copy holds, edge for edge, horizontal and vertical.
void testScoreCapTableIsExact() {
  ScoreModel saved = gScore;
  std::vector<double> dense = {0.0, 9.78571, 6.15, 10.0, 6.15, 0.0};
  gScore.capH = dense;
  gScore.capV = dense;
  gScore.capIdxH.clear();
  gScore.capIdxV.clear();
  std::vector<double> viaDense;
  for (size_t i = 0; i < dense.size(); ++i)
    viaDense.push_back(gScore.capOf(true, i));
  gScore.capH.clear();
  gScore.capV.clear();
  gScore.capTable = {0.0, 6.15, 9.78571, 10.0};
  gScore.capIdxH = {0, 2, 1, 3, 1, 0};
  gScore.capIdxV = {0, 2, 1, 3, 1, 0};
  bool same = true;
  for (size_t i = 0; i < dense.size(); ++i)
    same = same && gScore.capOf(true, i) == viaDense[i] && gScore.capOf(false, i) == dense[i];
  check(same, "score capacities: the 1-byte table returns exactly the dense doubles");
  gScore = saved;
}

// pd35route (Part 111): the streamed 2024 path equals the in-memory one --
// the same Design pins (with access-point choice on) and the same solution
// text, with the netlist read back from its file while writing.
void testG24StreamingMatchesInMemory() {
  const char *cap = "3 4 3\n0.5 2 1 1 1\n100 100 100\n100 100\n"
                    "M1 0 0\n9 9 9 9\n9 9 9 9\n9 9 9 9\n"
                    "M2 0 0\n2 2 2 2\n2 1.5 2 2\n2 2 2 2\n"
                    "M3 1 0\n3 3 0 3\n3 3 3 3\n3 3 3 3\n";
  const char *net = "netA\n(\n[(0, 0, 0), (0, 2, 1)]\n[(0, 3, 2)]\n)\nnetB\n(\n[(0, 2, 2)]\n[(1, 2, 2)]\n)\n"
                    "netC\n(\n[(0, 1, 0)]\n[(0, 1, 2), (0, 3, 0)]\n)\n";
  std::string capPath = "pd35route_selftest_stream.cap", netPath = "pd35route_selftest_stream.net";
  {
    std::ofstream(capPath) << cap;
    std::ofstream(netPath) << net;
  }
  // in memory
  G24Input mem;
  std::string err;
  std::istringstream ci(cap), ni(net);
  g24::readResources(ci, mem.res, err);
  g24::readNets(ni, mem.nets, err);
  mem.nets = g24::chooseAccessPoints(mem.nets);
  Design dm = g24::toDesign(mem.res, mem.nets);
  // streamed
  G24Input st;
  Design ds;
  readG24Streaming(capPath, netPath, /*align=*/true, st, ds);
  bool same = ds.nets.size() == dm.nets.size() && st.nNets == 3 && ds.grid.hcap == dm.grid.hcap &&
              ds.grid.vcap == dm.grid.vcap;
  for (size_t i = 0; same && i < ds.nets.size(); ++i) {
    same = ds.nets[i].id == dm.nets[i].id && ds.nets[i].pins.size() == dm.nets[i].pins.size() &&
           ds.nets[i].name.empty();
    for (size_t k = 0; same && k < ds.nets[i].pins.size(); ++k)
      same = ds.nets[i].pins[k].x == dm.nets[i].pins[k].x && ds.nets[i].pins[k].y == dm.nets[i].pins[k].y &&
             ds.nets[i].pins[k].layer == dm.nets[i].pins[k].layer;
  }
  check(same, "g24 streaming: the same Design (access points chosen, no names kept)");
  // the same routes written both ways
  std::vector<RoutedNet> rm(2), rs(2);
  rm[0].net = &dm.nets[0];
  rs[0].net = &ds.nets[0];
  rm[0].segs = rs[0].segs = {{2, 1, 0, 2, 1, 1}, {2, 1, 1, 3, 1, 1}, {3, 1, 1, 3, 1, 2}, {3, 1, 2, 3, 2, 2}};
  rm[1].net = &dm.nets[2];
  rs[1].net = &ds.nets[2];
  rm[1].segs = rs[1].segs = {{0, 1, 0, 0, 1, 2}, {0, 1, 2, 0, 3, 2}};
  std::string a = "pd35route_selftest_stream_a.sol", b = "pd35route_selftest_stream_b.sol";
  g24::SolutionReport ra = writeG24Solution(a, mem, dm.grid, rm), rb = writeG24Solution(b, st, ds.grid, rs);
  std::stringstream ta, tb;
  ta << std::ifstream(a).rdbuf();
  tb << std::ifstream(b).rdbuf();
  check(ta.str() == tb.str() && !ta.str().empty() && ra.nets == 3 && rb.nets == 3 && ra.pinFixes == rb.pinFixes,
        "g24 streaming: the same solution text, the netlist re-read while writing");
  // pd37route: the threaded writer (slices formatted on the pool) writes
  // the same text and report, streamed and in memory.
  std::string c = "pd37route_selftest_stream_c.sol", e = "pd37route_selftest_stream_e.sol";
  int savedT = gThreads;
  gThreads = 4;
  g24::SolutionReport rc = writeG24Solution(c, st, ds.grid, rs), re = writeG24Solution(e, mem, dm.grid, rm);
  gThreads = savedT;
  std::stringstream tc, te;
  tc << std::ifstream(c).rdbuf();
  te << std::ifstream(e).rdbuf();
  check(tc.str() == ta.str() && te.str() == ta.str() && rc.nets == ra.nets && rc.pinFixes == ra.pinFixes &&
            rc.wires == ra.wires && rc.vias == ra.vias && re.unroutedNets == ra.unroutedNets,
        "threaded writer: the same solution text and report at 4 threads");
  for (const std::string &f : {capPath, netPath, a, b, c, e})
    std::remove(f.c_str());
}

// Part 215 (2026-11-24): the settings table and the checks on it (pd39_settings.h).
void testSettingsRegistry() {
  bool sorted = true;
  for (size_t i = 1; i < settings::kCount; ++i)
    sorted &= std::strcmp(settings::kTable[i - 1].name, settings::kTable[i].name) < 0;
  check(sorted, "settings: the table is sorted by name with no repeats (find() depends on it)");
  bool sameNames = true, filled = true; // 2026-11-26: every setting's default, in kTable's order
  for (size_t i = 0; i < settings::kCount; ++i) {
    sameNames &= std::strcmp(settings::kDefaults[i].name, settings::kTable[i].name) == 0;
    filled &= settings::kDefaults[i].value && *settings::kDefaults[i].value;
  }
  check(sameNames && filled, "settings: kDefaults names every setting of kTable, in order, each with a default");
  bool found = true;
  for (const settings::Info &i : settings::kTable)
    found &= settings::find(i.name) == &i;
  check(found && !settings::find("PD39ROUTE_SKIP_GAINT") && !settings::find("PD39ROUTE_SKIP") &&
            !settings::find("PD39ROUTE_SKIP_GIANTS") && settings::find("PD39ROUTE_SKIP_GIANT=1", 20) &&
            !settings::find(""),
        "settings: find() finds every name and no prefix, extension or misspelling of one");
  long long x = 0;
  double r = 0;
  check(settings::parseInt("12", x) && x == 12 && settings::parseInt(" -3\t", x) && x == -3 &&
            !settings::parseInt("", x) && !settings::parseInt(" ", x) && !settings::parseInt("12x", x) &&
            !settings::parseInt("1.5", x) && !settings::parseInt("abc", x) && !settings::parseInt("0,1", x) &&
            !settings::parseInt("99999999999999999999", x) && !settings::parseInt(nullptr, x),
        "settings: parseInt takes a whole number and nothing else");
  check(settings::parseReal("0.05", r) && r == 0.05 && settings::parseReal("1e-3", r) && r == 1e-3 &&
            settings::parseReal("7", r) && r == 7 && !settings::parseReal("abc", r) && !settings::parseReal("0,1", r) &&
            !settings::parseReal("nan", r) && !settings::parseReal("inf", r) && !settings::parseReal("1e999", r) &&
            !settings::parseReal("", r),
        "settings: parseReal takes a finite number and nothing else");
  // every valid value parses exactly as atof / atoi did (so routes cannot change)
  bool same = true;
  for (const char *v : {"0", "1", "-1", "6", "30", "0.05", "1.5", "1e-3", "2.5e2", "0.1"}) {
    same &= settings::parseReal(v, r) && r == std::strtod(v, nullptr);
    if (settings::parseInt(v, x))
      same &= x == std::strtol(v, nullptr, 10);
  }
  check(same, "settings: valid values parse to exactly what atof / atoi gave");
  char e1[] = "PD39ROUTE_SKIP_GIANT=1", e2[] = "PD39ROUTE_SKIP_GAINT=1", e3[] = "PATH=/bin", e4[] = "PD40ROUTE_X=1",
       e5[] = "GR_SIGNPOST=1", e6[] = "VROUTE_EPS=abc", e7[] = "VROUTE_FAST_ROUNDS=12", e8[] = "VROUTE_EPS=0.1",
       e9[] = "NV2ROUTE_VIA_ROUNDS=1.5", e10[] = "PD39ROUTE_PORTFOLIO=over=0;over=0.075", e11[] = "VROUTE_POWER=";
  char *env[] = {e1, e2, e3, e4, e5, e6, e7, e8, e9, e10, e11, nullptr};
  const std::vector<std::string> p = settings::problems(env);
  const std::vector<std::string> want = {
      "unknown setting PD39ROUTE_SKIP_GAINT (did you mean PD39ROUTE_SKIP_GIANT?)",
      "unknown setting GR_SIGNPOST (did you mean GR_SIGNPOSTS?)", "VROUTE_EPS must be a number (got 'abc')",
      "NV2ROUTE_VIA_ROUNDS must be a whole number (got '1.5')", "VROUTE_POWER must be a number (got '')"};
  check(p == want, "settings: an environment's problems are its misspelled names and its unparsable numbers, "
                   "and nothing else (other prefixes, PATH, valid values and lists pass)");
  if (p != want)
    for (const std::string &s : p)
      std::cout << "    got: " << s << "\n";
  // Part 216: flags take 0 or 1, on-if-set switches take 1, and a Text setting with words takes one of them
  char f1[] = "PD38ROUTE_SLOW_DAG=2", f2[] = "VROUTE_TRACE=0", f3[] = "VROUTE_HIST_MODE=mul", f4[] = "VROUTE_HIST_MODE=ratio",
       f5[] = "VROUTE_MSMV=0", f6[] = "VROUTE_TRACE=1", f7[] = "PD34ROUTE_2D_COST=score";
  char *env2[] = {f1, f2, f3, f4, f5, f6, f7, nullptr};
  const std::vector<std::string> want2 = {"PD38ROUTE_SLOW_DAG must be 0 or 1 (got '2')",
                                          "VROUTE_TRACE is on when set: set it to 1, or leave it unset for off (got '0')",
                                          "VROUTE_HIST_MODE must be one of add/mult/ratio (got 'mul')"};
  const std::vector<std::string> p2 = settings::problems(env2);
  check(p2 == want2, "settings: flags take 0 or 1, switches 1, worded settings their words (the rest pass)");
  if (p2 != want2)
    for (const std::string &s2 : p2)
      std::cout << "    got: " << s2 << "\n";
  char *clean[] = {e1, e3, e7, e8, e10, f4, f5, f6, f7, nullptr};
  check(settings::problems(clean).empty(), "settings: a clean environment has no problems");
  check(settings::nearest("VROUTE_FAST_ROUND") == "VROUTE_FAST_ROUNDS" && settings::nearest("PD99ROUTE_NOTHING_LIKE_IT").empty(),
        "settings: the nearest known name is offered only when it is close");
  check(settings::prefixOf("PD39ROUTE_X", 11) == "PD39ROUTE_" && settings::prefixOf("GR_GPU", 6) == "GR_" &&
            settings::prefixOf("HOME", 4).empty() && settings::knownPrefix("NV2ROUTE_") && !settings::knownPrefix("PD40ROUTE_"),
        "settings: prefixes are grouped and recognised");
}

// Part 216 (2026-11-24): the command-line checks -- every problem found and worded, and a good command line passes.
void testRunCommandLine() {
  const std::string dir = "pd39route_selftest_cmdline";
  mkdir(dir.c_str(), 0755);
  const std::string gr = dir + "/d.gr", cap = dir + "/d.cap", net = dir + "/d.net";
  for (const std::string &f : {gr, cap, net})
    std::ofstream(f) << "x\n";
  auto run = [&](std::vector<std::string> args) {
    args.insert(args.begin(), "pd39route");
    std::vector<char *> av;
    for (std::string &a : args)
      av.push_back(a.data());
    av.push_back(nullptr);
    RouterRun r;
    r.commandLine(static_cast<int>(args.size()), av.data());
    return std::make_pair(r.commandLineOk, r.problems);
  };
  auto has = [](const std::vector<std::string> &ps, const std::string &needle) {
    return std::any_of(ps.begin(), ps.end(), [&](const std::string &p) { return p.find(needle) != std::string::npos; });
  };
  auto a = run({gr, dir + "/out.route"});
  check(a.first && a.second.empty(), "command line: a 2008 run with a readable design and a writable output passes");
  a = run({cap, net, dir + "/out.sol"});
  check(a.first && a.second.empty(), "command line: a 2024 run passes");
  a = run({"--bogus", gr, dir + "/o"});
  check(!a.first && a.second.size() == 1 && has(a.second, "unknown option --bogus"), "command line: an unknown option");
  a = run({gr});
  check(!a.first && has(a.second, "1 argument given"), "command line: too few arguments");
  a = run({dir + "/missing.gr", dir + "/nofolder/out.route"});
  check(!a.first && a.second.size() == 2 && has(a.second, "cannot read the .gr file " + dir + "/missing.gr") &&
            has(a.second, "folder " + dir + "/nofolder does not exist"),
        "command line: a missing input and an unwritable output are both reported");
  a = run({gr, dir});
  check(has(a.second, "cannot write the solution " + dir + ": it is a folder"), "command line: an output that is a folder");
  a = run({cap, net, net});
  check(!a.first && has(a.second, "is the .net file (it would be overwritten)"), "command line: an output that is an input");
  a = run({dir, dir + "/o"});
  check(!a.first && has(a.second, "it is a folder"), "command line: an input that is a folder");
  check(outputProblem("").size() && folderProblem(gr) == "it is not a folder" && folderProblem(dir).empty() &&
            inputProblem(gr).empty(),
        "command line: the path checks name what is wrong");
  for (const std::string &f : {gr, cap, net})
    std::remove(f.c_str());
  rmdir(dir.c_str());
}

int run() {
  std::cout << "self test\n";
  testSettingsRegistry();
  testRunCommandLine();
  testBasicShapesStayOnFastTier();
  testMultiSourceDijkstraUsesCheapestSourceNotFirstInList();
  testMultiSourceDijkstraStopsAtCheapestSink();
  testPriceClampPreventsInfinity();
  testHistoryIgnoresZeroCapacityEdges();
  testDisablingRelaxReproducesPermanentBehavior();
  testRelaxationUndoesExactlyOneStepPerCleanRound();
  testRelaxationFullyRestoresBaselineAfterEqualCleanRounds();
  testHistoryStreakRequiresConsecutiveOvercapacityRounds();
  testHistoryScalesWithOverflowMagnitude();
  testHistoryRatioModeMultipliesByUsageOverCapacityExactly();
  testHistoryRatioExponentSquaresTheRatio();
  testExponentEscalationTracksStackSizeAndRelaxesWithIt();
  testGrid2dCapacityIsExactPostAdjustmentSumAcrossLayers();
  testTwoDRoutingClearsAnOverflowNoSingleLayerChoiceCanClear();
  testFixedTopologySlowTierStillWorksAfterTheCollapse();
  testTwodOnlyFlagIsInertInThisGeneration();
  testMedianSteinerPointIsExact();
  testSteinerizationStrictlyReducesTotalSpan();
  testSteinerizationIsANoOpWhenTheHubIsAlreadyOptimal();
  testSteinerizedOutputIsAlwaysAValidTree();
  testDisablingSteinerizeReproducesPlainMstTopology();
  testEndToEndSteinerizationReducesRealChargedWirelength();
  testExactSteinerBeatsBothPlainMstAndCheapPassOnADegreeThreeHub();
  testExactSteinerMatchesHandVerifiedOptimaOnSmallFixtures();
  testExactSteinerOutputIsAlwaysAValidTree();
  testDegreeCapGatingExercisesFr3sFallbackExactly();
  testDisablingExactSteinerAloneFallsBackToCheapPassNotPlainMst();
  testEndToEndExactSteinerizationReducesRealChargedWirelength();
  testTwoPinAndAlreadyOptimalNetsAreUntouchedByTheExactSolve();
  testCapacityAwareLayerChoiceReactsToRealUsageNotJustPreference();
  testDirectionPreferenceIsNeverViolated();
  testViaStitchingConnectsAThreeWayLayerMismatch();
  testViaStitchingCatchesMidSpanMsmvAttachmentNotJustEndpoints();
  testMultiTileSegmentChargesEveryUnitEdgeNotJustOnce();
  testDegenerateZeroLengthSegmentsAreDropped();
  testTwodOnlyReproducesFr4routesExactSingleLayerOutput();
  testPinAccessLayerIsAlwaysReached();
  testEndToEndRoutingAndLayerAssignmentProducesAConnectedThreeDResult();
  testJointDPAvoidsAnAvoidableViaGreedyWouldCreate();
  testViaWeightKnobHasRealEffect();
  testViaCapacityIsChargedAndInfluencesLaterDecisions();
  testSharedViaStackReplacesOvercountedPairwiseChains();
  testTreeReconstructionSplitsAMidSpanAttachment();
  testTreeReconstructionIsAlwaysAValidTree();
  testPinAccessViaIsChargedIntoRealViaUsage();
  testViaAwareOffReproducesFr5routesExactBehavior();
  testChargeLogRoundTripIsExact();
  testNegotiationReducesViaOverflowASinglePassCannotFix();
  testViaHistoryRelaxDisabledReproducesPermanentBehavior();
  testViaHistoryRelaxationUndoesExactlyOneStepPerCleanRound();
  testSupervisedRevertPrimitivesDetectAndUndoAWorseningMove();
  testWireHistoryFixesRealOverflowASinglePassCannotFix();
  testWireHistoryRelaxDisabledReproducesPermanentBehavior();
  testWireHistoryRelaxationUndoesExactlyOneStepPerCleanRound();
  testWireAwareOffReproducesNv1routesLimitAndIsNotADeadFlag();
  testGiantNetJoinsFastTierAndEscapesAPermanentlyCongestedCorner();
  testRetunedCoefficientsProduceAMateriallyDifferentPricingCurve();
  testWiderBoxFindsDetourFixedMarginCannot();
  testBoxGrowthArithmetic();
  testBoxMarginModesInPhaseBHelpers();
  testParseBoxGrowthRejectsBadInput();
  testPdFactorScheduleMatchesNthu();
  testPresentDemandCostHandComputed();
  testPdCostZeroCapacityIsForbiddenAndFinite();
  testPdHistoryInvariantKeepsShrinkAboveFloor();
  testPdCostFallsTheMomentDemandLeaves();
  testLambdaModeIsTheInheritedCostExactly();
  testAStarStaysExactUnderPdPricing();
  testPdRouteReachesZeroOnADetourOnlyFixture();
  testParseCostSettings();
  testPdCongPowerByMultiplicationMatchesPow();
  testRevertRerouteDecision();
  testKeepModeReachesZeroOnTheDetourOnlyFixture();
  testParseAcceptMode();
  testSplitToUnitEdgesKeepsTheTreeAndItsTiles();
  testNthuLayerAssignmentSwitchesMidLegToAvoidOverflow();
  testLaNetOrderMatchesNthu();
  testReadRouteAs2DProjectsWiresAndDropsVias();
  testNthuLayerAssignmentEndToEndAndRoundTrip();
  testParseLaMode();
  testDropCyclesKeepsARingNetAndItsPin();
  testRefineCostCountsOnlyFullEdges();
  testRefineDetoursAroundFullEdgesRegardlessOfLength();
  testRefineOrderMatchesNthu();
  testPrepareFor2DRefineChargesExactlyTheWires();
  testRefine2DClearsTheThreeOnOneRowFixture();
  testParseRefineSettings();
  testRefineShuffleIsASeededPermutation();
  testRegionNegotiationClearsTheThreeOnOneRowFixture();
  testPieceRerouteMovesOnlyTheOverflowedPiece();
  testBoxHeuristicFindsTheSameCost();
  testBoxLimitSignalAndEvidenceRule();
  testPieceModeStagesClearTheThreeOnOneRowFixture();
  testBendAwareSearchCountsCorners();
  testWlRecoveryShortensADetour();
  testWlRecoveryNeverUsesAFullEdge();
  testWlRecoveryRemovesCornersAtEqualLength();
  testParseWlSettings();
  testPinAwareLayerAssignmentAvoidsPinStacks();
  testShapeCostCountsCornerTiles();
  testRebuildReplacesAWastefulTree();
  testRebuildKeepsAnOptimalTree();
  testSlowPiecesAndStopAt();
  testPatternFirstPieceReroute();
  testPatternStageSeparatesTwoNetsOntoTheTwoLs();
  testPlateauStop();
  testStageMarginCap();
  testRefineLowStop();
  testWorkGatedPlateau();
  testDetMathAccuracy();
  testBuiltWithoutContraction();
  testSlowTierBestStateIsKept();
  testDetourStats();
  testDetourReclaim();
  testReclaimAvoidsOverflow();
  testSlowTierReclaim();
  testReclaimFallback();
  testFallbackBFirst();
  testFallbackAFirstThreshold();
  testVirtualCapArithmetic();
  testOverCapacityCost();
  testVirtualCapCostSeam();
  testVirtualCapZone();
  testVirtualCapSteersSearch();
  testRegionVirtualCapReported();
  testFallbackParallelMatchesSequential();
  testParseVirtualCap();
  testVirtualCapPathAOnly();
  testG24DesignMatchesBridge();
  testViaDemandFollowsTheEvaluatorRule();
  testLayerAssignmentPricesViaDemand();
  testG24LossMatchesTheEvaluator();
  testScoreCostAcceptsCheapOverflow();
  testG24MostlyBlockedLayerStaysRoutable();
  testG24AccessPointChoice();
  testScore2DCostByHand();
  testScore2DPrefersCheapOverflowToDetour();
  testScore2DSeam();
  testPattern3DFlipsOutOfACongestedRow();
  testPattern3DKeepsAnEquallyGoodRoute();
  testPattern3DRemovesADetour();
  testPattern3D2008FlipsToRemoveOverflow();
  testPattern3D2008RemovesADetour();
  testOpStacksIsASparseLifo();
  testNetDeltaMatchesAFullCopy();
  testChargeRecordHasNoUninitialisedBytes();
  testNetSpillRoundTrip();
  testLowMemFallbackMatches();
  testReclaimPathBoxCap();
  testRegionDamping();
  testRegionAbort();
  testAnnealedRefinement();
  testSlowPlateauStop();
  testSlowCut();
  testSlowOver();
  testWallAwareHeuristic();
  testBidirSearch();
  testSearchHeapMatchesPriorityQueue();
  testLateralShifts();
  testEvictChains();
  testScoreCapTableIsExact();
  testScoreLossTableIsExact();
  testStageReport();
  testLocalPatternBackendMatchesReference();
  testCommutingBatches();
  testSlowDagSameAtAnyThreadCount();
  testFastReadersMatchIostream();
  testIspd25Format();
  testWorkerPoolReuse();
  testNetClock();
  testEmitNetSolutionFastMatches();
  testPatternColorBatchesMatchesFirstFit();
  testCommutingDagOrder();
  testCriticalPathFirstDag();
  testBuildNetTreeMatchesReference();
  testSlackOverrideFile();
  testTimingMultFile();
  testTimingRowsFile();
  testPriceSeed();
  testSkipGiant();
  testSplitInteriorCrossings();
  testRefineKeepAbove();
  testRouterScoreOnePlane();
  testLagrangeAndPortfolio();
  testTimingLayerBiasPicksUpperMetal();
  testThreadedLayerAssignmentIsExact();
  testPatternIncrementalRefreshIsExact();
  testG24StreamingMatchesInMemory();
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}

} // namespace selftest
