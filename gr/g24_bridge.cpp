// g24_bridge.cpp -- a format bridge between the ISPD 2024 contest files and
// the ISPD 2008 .gr world the routers read (roadmap step 3, first step;
// HISTORY_COST_SWEEP.md Part 102). Lets the existing standard router
// (pd23route) route an ISPD 2024 design unchanged, so it can be scored by
// gr/g24_verify.cpp (which matches the official evaluator) before a native
// reader exists.
//
//   g24_bridge to-gr <design.cap> <design.net> <out.gr>
//   g24_bridge from-route <design.cap> <design.net> <route.sol> <out.g24sol>
//   g24_bridge --selftest
//
// to-gr: one .gr tile per GCell (tile 10 x 10, origin 0, so a GCell's
// centre is (10x + 5, 10y + 5)); one .gr layer per .cap layer (z -> z + 1).
// Capacity in half tracks -- floor(2 x tracks), so a net (minimum width 1 +
// spacing 1 = 2) uses one track, the evaluator's demand unit: on a layer
// whose direction is horizontal, horizontal capacity is the layer's most
// common value and vertical capacity 0 (and vice versa); every GCell edge
// that differs gets a .gr capacity adjustment. Layer 0 (metal 1, not
// routable) gets 0 both ways. Each pin keeps its first access point.
// Optional via reservation (Part 102): G24_BRIDGE_RESERVE=k (half tracks
// per pin, default 0 = off) lowers each layer-z edge's capacity, for z in
// 1..G24_BRIDGE_RESERVE_LAYERS (default 1, M2 only), by k for every pin
// below layer z in each of the edge's two GCells -- the evaluator charges a
// via passing through layer z at a GCell half a track on the two layer-z
// GCells beside it, unless the net has a layer-z wire there.
// What the bridge cannot carry, and the router therefore does not see:
// non-uniform GCell pitches (wirelength is scored in DBU by the evaluator),
// via demand on wire capacity, the per-layer overflow weights, and the
// choice among a pin's access points.
//
// from-route: reads the router's .gr solution (tile coordinates via the
// same 10 x 10 grid) and writes the ISPD 2024 GCell segments: a wire keeps
// its layer and runs low to high; a via runs z low to z high; zero-length
// pieces are dropped. Then every pin must be covered at one of its access
// points (the evaluator only reaches access points the net covers): a pin
// whose chosen access point is not covered gets a via between its layer
// and the nearest layer the net covers at that GCell; a net the router left
// unrouted (all pins in one GCell) gets a via stack over its pins' layers
// there, at least one layer pair. The number of each fix is printed.
//
// The mapping and the conversion live in g24_io.h (toDesign, writeGr,
// convertSolution), shared with the native routers (pd33route on), so the
// bridge and the native path build the same Design.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "g24_io.h"

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "g24_bridge self test\n";
  using namespace g24;
  // 4 x 3 GCells, 3 layers: M1 (not routable), M2 horizontal, M3 vertical.
  // M2 capacities 2 tracks everywhere but 1.5 at (1,1); M3 3 tracks
  // everywhere but 0 at (2,0).
  const char *cap = "3 4 3\n0.5 2 1 1 1\n100 100 100\n100 100\n"
                    "M1 0 0\n9 9 9 9\n9 9 9 9\n9 9 9 9\n"
                    "M2 0 0\n2 2 2 2\n2 1.5 2 2\n2 2 2 2\n"
                    "M3 1 0\n3 3 0 3\n3 3 3 3\n3 3 3 3\n";
  const char *net = "netA\n(\n[(0, 0, 0), (0, 0, 1)]\n[(0, 3, 2)]\n)\nnetB\n(\n[(0, 2, 2)]\n[(1, 2, 2)]\n)\n";
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream ci(cap), ni(net);
  check(readResources(ci, r, err) && readNets(ni, nets, err) && nets.size() == 2, "fixture reads");
  std::ostringstream gr;
  writeGr(toDesign(r, nets), gr);
  std::string g = gr.str();
  check(g.find("vertical capacity 0 0 6\nhorizontal capacity 0 4 0\n") != std::string::npos,
        "defaults in half tracks along each layer's direction, 0 across and on M1");
  check(g.find("1 1 2 2 1 2 3\n") != std::string::npos, "M2 edge (1,1)-(2,1) at 1.5 tracks -> adjustment to 3");
  check(g.find("2 0 3 2 1 3 0\n") != std::string::npos, "M3 edge (2,0)-(2,1) at 0 tracks -> adjustment to 0");
  check(g.find("netA 0 2 1\n5 5 1\n35 25 1\n") != std::string::npos, "pins: first access point, GCell centres, z + 1");
  // Reservation 1 on M2: pins below M2 (first access point on M1) are
  // netA's at (0,0) and (3,2) and netB's first at (2,2); netB's second is on
  // M2 itself and does not count. M2 is horizontal: edge (0,0)-(1,0) loses 1,
  // (1,2)-(2,2) loses 1, (2,2)-(3,2) loses 2 (pins in both its GCells). M3
  // is untouched (only M2 reserved).
  std::ostringstream gr2;
  writeGr(toDesign(r, nets, 1, 1), gr2);
  std::string g2 = gr2.str();
  check(g2.find("0 0 2 1 0 2 3\n") != std::string::npos && g2.find("1 2 2 2 2 2 3\n") != std::string::npos &&
            g2.find("2 2 2 3 2 2 2\n") != std::string::npos && g2.find("1 1 2 2 1 2 3\n") != std::string::npos &&
            g2.find("horizontal capacity 0 4 0\n") != std::string::npos,
        "reservation: M2 edges beside pins below it lose 1 half track per pin; default and M3 unchanged");
  // A router solution for netA: via M1-M2 at (0,0), M2 wire to (3,0), via
  // up to M3, M3 wire to (3,2) -- ending on M3 while netA's second pin is on
  // M1 at (3,2): the bridge must add a via down to M1 there. netB is not in
  // the solution (both pins in GCell (2,2)): it gets a via stack M1-M2.
  std::istringstream sol("netA 0 4\n(5,5,1)-(5,5,2)\n(5,5,2)-(35,5,2)\n(35,5,2)-(35,5,3)\n(35,5,3)-(35,25,3)\n!\n");
  std::ostringstream out;
  SolutionReport fr = convertSolution(r, nets, sol, out);
  std::string s = out.str();
  check(s.find("netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 1 3 0 2\n3 0 2 3 2 2\n3 2 0 3 2 2\n)\n") != std::string::npos,
        "netA: segments converted, and a via M1-M3 added at (3,2) for the uncovered pin");
  check(s.find("netB\n(\n2 2 0 2 2 1\n)\n") != std::string::npos && fr.unroutedNets == 1 && fr.pinFixes == 1,
        "netB, unrouted: one via stack over its pins' layers (M1-M2); 1 pin fix, 1 unrouted net");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  std::string mode = argc > 1 ? argv[1] : "";
  if (!((mode == "to-gr" && argc == 5) || (mode == "from-route" && argc == 6))) {
    std::cerr << "usage: " << argv[0] << " to-gr <design.cap> <design.net> <out.gr>\n       " << argv[0]
              << " from-route <design.cap> <design.net> <route.sol> <out.g24sol>\n";
    return 2;
  }
  g24::Resources r;
  std::vector<g24::Net> nets;
  std::string err;
  std::ifstream ci(argv[2]), ni(argv[3]);
  if (!ci || !ni || !g24::readResources(ci, r, err) || !g24::readNets(ni, nets, err) ||
      !g24::checkAccessPoints(r, nets, err)) {
    std::cerr << "g24_bridge: " << (err.empty() ? "cannot open input" : err) << "\n";
    return 1;
  }
  if (mode == "to-gr") {
    std::ofstream o(argv[4]);
    const char *rv = std::getenv("G24_BRIDGE_RESERVE"), *rl = std::getenv("G24_BRIDGE_RESERVE_LAYERS");
    int reserve = rv ? std::atoi(rv) : 0, reserveLayers = rl ? std::atoi(rl) : 1;
    if (reserve < 0 || reserveLayers < 1) {
      std::cerr << "g24_bridge: G24_BRIDGE_RESERVE must be >= 0 and G24_BRIDGE_RESERVE_LAYERS >= 1\n";
      return 2;
    }
    g24::writeGr(g24::toDesign(r, nets, reserve, reserveLayers), o);
    std::cerr << "wrote " << argv[4] << ": " << r.xSize << " x " << r.ySize << " x " << r.nLayers << ", "
              << nets.size() << " nets\n";
    return 0;
  }
  std::ifstream si(argv[4]);
  std::ofstream o(argv[5]);
  if (!si || !o) {
    std::cerr << "g24_bridge: cannot open the solution files\n";
    return 1;
  }
  g24::SolutionReport fr = g24::convertSolution(r, nets, si, o);
  std::cerr << "converted " << fr.nets << " nets: " << fr.wires << " wires, " << fr.vias << " vias, "
            << fr.droppedZero << " zero-length pieces dropped, " << fr.pinFixes << " pins given a via, "
            << fr.unroutedNets << " unrouted nets given a via stack\n";
  return 0;
}
