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

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace g24b {

struct Layer {
  std::string name;
  int dir = 0;
  double minLength = 0;
};
struct Resources {
  int nLayers = 0, xSize = 0, ySize = 0;
  double unitLength = 0, unitVia = 0;
  std::vector<double> overflowCost;
  std::vector<long long> xPitch, yPitch;
  std::vector<Layer> layers;
  std::vector<double> cap;
  double capacity(int z, int x, int y) const { return cap[(static_cast<size_t>(z) * ySize + y) * xSize + x]; }
};
struct Access {
  int x, y, z;
};
struct Net {
  std::string name;
  std::vector<std::vector<Access>> pins;
};
struct Seg {
  int xl, yl, zl, xh, yh, zh;
};

static std::string trimEnd(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  return s;
}

// The .cap and .net readers are g24_verify.cpp's (the evaluator's rules).
bool readResources(std::istream &in, Resources &r, std::string &err) {
  if (!(in >> r.nLayers >> r.xSize >> r.ySize) || r.nLayers < 2 || r.xSize < 1 || r.ySize < 1) {
    err = "bad .cap header";
    return false;
  }
  if (!(in >> r.unitLength >> r.unitVia)) {
    err = "bad .cap cost line";
    return false;
  }
  r.overflowCost.resize(r.nLayers);
  for (double &c : r.overflowCost)
    if (!(in >> c)) {
      err = "bad .cap overflow costs";
      return false;
    }
  r.xPitch.resize(r.xSize - 1);
  r.yPitch.resize(r.ySize - 1);
  for (long long &p : r.xPitch)
    if (!(in >> p)) {
      err = "bad .cap pitch";
      return false;
    }
  for (long long &p : r.yPitch)
    if (!(in >> p)) {
      err = "bad .cap pitch";
      return false;
    }
  r.layers.resize(r.nLayers);
  r.cap.assign(static_cast<size_t>(r.nLayers) * r.ySize * r.xSize, 0.0);
  for (int z = 0; z < r.nLayers; ++z) {
    Layer &L = r.layers[z];
    if (!(in >> L.name >> L.dir >> L.minLength)) {
      err = "bad .cap layer line " + std::to_string(z);
      return false;
    }
    for (int y = 0; y < r.ySize; ++y)
      for (int x = 0; x < r.xSize; ++x)
        if (!(in >> r.cap[(static_cast<size_t>(z) * r.ySize + y) * r.xSize + x])) {
          err = "short capacity table on layer " + std::to_string(z);
          return false;
        }
  }
  return true;
}

bool readNets(std::istream &in, std::vector<Net> &nets, std::string &err) {
  std::string line;
  while (std::getline(in, line)) {
    line = trimEnd(line);
    if (line.find('(') == std::string::npos && line.find(')') == std::string::npos && line.length() > 1) {
      nets.push_back(Net{line, {}});
    } else if (line.find('[') != std::string::npos) {
      if (nets.empty()) {
        err = "pin line before any net name";
        return false;
      }
      for (char &c : line)
        if (c == '(' || c == ')' || c == ',' || c == '[' || c == ']')
          c = ' ';
      std::istringstream ss(line);
      std::vector<Access> pin;
      int x, y, z;
      while (ss >> z >> x >> y)
        pin.push_back({x, y, z});
      if (pin.empty()) {
        err = "net " + nets.back().name + ": pin with no access points";
        return false;
      }
      nets.back().pins.push_back(pin);
    }
  }
  return true;
}

int halfTracks(double tracks) { return tracks <= 0 ? 0 : static_cast<int>(2.0 * tracks + 1e-9); }

// The .gr text for a design.
void writeGr(const Resources &r, const std::vector<Net> &nets, std::ostream &o, int reserve = 0,
             int reserveLayers = 1) {
  // pinsBelow[z][y * xSize + x]: pins whose first access point is at (x, y)
  // on a layer below z (only for the reserved layers).
  std::vector<std::vector<int>> pinsBelow(r.nLayers);
  if (reserve > 0)
    for (int z = 1; z <= reserveLayers && z < r.nLayers; ++z) {
      pinsBelow[z].assign(static_cast<size_t>(r.xSize) * r.ySize, 0);
      for (const Net &n : nets)
        for (const std::vector<Access> &p : n.pins)
          if (p[0].z < z)
            ++pinsBelow[z][static_cast<size_t>(p[0].y) * r.xSize + p[0].x];
    }
  // An edge's capacity: its GCell's half tracks, less the reservation for
  // the pins in its two GCells.
  auto edgeCap = [&](int z, int x, int y) {
    int c = halfTracks(r.capacity(z, x, y));
    if (pinsBelow[z].empty())
      return c;
    bool h = r.layers[z].dir == 0;
    int pins = pinsBelow[z][static_cast<size_t>(y) * r.xSize + x] +
               pinsBelow[z][static_cast<size_t>(h ? y : y + 1) * r.xSize + (h ? x + 1 : x)];
    return std::max(0, c - reserve * pins);
  };
  // Per layer: the default (most common) half-track capacity along its direction.
  std::vector<int> def(r.nLayers, 0);
  for (int z = 1; z < r.nLayers; ++z) {
    std::map<int, long long> count;
    bool h = r.layers[z].dir == 0;
    for (int y = 0; y < (h ? r.ySize : r.ySize - 1); ++y)
      for (int x = 0; x < (h ? r.xSize - 1 : r.xSize); ++x)
        ++count[edgeCap(z, x, y)];
    long long best = -1;
    for (const auto &kv : count)
      if (kv.second > best) {
        best = kv.second;
        def[z] = kv.first;
      }
  }
  o << "grid " << r.xSize << " " << r.ySize << " " << r.nLayers << "\n";
  o << "vertical capacity";
  for (int z = 0; z < r.nLayers; ++z)
    o << " " << (z > 0 && r.layers[z].dir == 1 ? def[z] : 0);
  o << "\nhorizontal capacity";
  for (int z = 0; z < r.nLayers; ++z)
    o << " " << (z > 0 && r.layers[z].dir == 0 ? def[z] : 0);
  o << "\nminimum width";
  for (int z = 0; z < r.nLayers; ++z)
    o << " 1";
  o << "\nminimum spacing";
  for (int z = 0; z < r.nLayers; ++z)
    o << " 1";
  o << "\nvia spacing";
  for (int z = 0; z < r.nLayers; ++z)
    o << " 0";
  o << "\n0 0 10 10\nnum net " << nets.size() << "\n";
  for (size_t i = 0; i < nets.size(); ++i) {
    o << nets[i].name << " " << i << " " << nets[i].pins.size() << " 1\n";
    for (const std::vector<Access> &p : nets[i].pins)
      o << p[0].x * 10 + 5 << " " << p[0].y * 10 + 5 << " " << p[0].z + 1 << "\n";
  }
  std::ostringstream adj;
  long long n = 0;
  for (int z = 1; z < r.nLayers; ++z) {
    bool h = r.layers[z].dir == 0;
    for (int y = 0; y < (h ? r.ySize : r.ySize - 1); ++y)
      for (int x = 0; x < (h ? r.xSize - 1 : r.xSize); ++x) {
        int c = edgeCap(z, x, y);
        if (c == def[z])
          continue;
        adj << x << " " << y << " " << z + 1 << " " << (h ? x + 1 : x) << " " << (h ? y : y + 1) << " " << z + 1 << " "
            << c << "\n";
        ++n;
      }
  }
  o << n << "\n" << adj.str();
}

struct FromReport {
  long long nets = 0, wires = 0, vias = 0, droppedZero = 0, pinFixes = 0, unroutedNets = 0;
};

// Converts the router's solution into ISPD 2024 segments, fixing pin
// coverage (see the file header).
FromReport convertSolution(const Resources &r, const std::vector<Net> &nets, std::istream &sol, std::ostream &o) {
  FromReport rep;
  std::unordered_map<std::string, std::vector<Seg>> segs;
  std::string line, cur;
  auto tile = [](long long v) { return static_cast<int>(v / 10); };
  while (std::getline(sol, line)) {
    line = trimEnd(line);
    size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos)
      continue;
    std::string s = line.substr(b);
    if (s == "!") {
      cur.clear();
      continue;
    }
    if (s[0] == '(') {
      long long x1, y1, l1, x2, y2, l2;
      if (cur.empty() || std::sscanf(s.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        continue;
      int ax = tile(x1), ay = tile(y1), az = static_cast<int>(l1) - 1, bx = tile(x2), by = tile(y2),
          bz = static_cast<int>(l2) - 1;
      segs[cur].push_back({std::min(ax, bx), std::min(ay, by), std::min(az, bz), std::max(ax, bx), std::max(ay, by),
                           std::max(az, bz)});
      continue;
    }
    std::istringstream hs(s);
    hs >> cur;
  }
  for (const Net &n : nets) {
    ++rep.nets;
    std::vector<Seg> out;
    std::set<std::tuple<int, int, int>> covered; // (x, y, z)
    auto cover = [&](const Seg &g) {
      if (g.zl == g.zh) {
        for (int x = g.xl; x <= g.xh; ++x)
          for (int y = g.yl; y <= g.yh; ++y)
            covered.insert({x, y, g.zl});
      } else {
        for (int z = g.zl; z <= g.zh; ++z)
          covered.insert({g.xl, g.yl, z});
      }
    };
    auto it = segs.find(n.name);
    if (it != segs.end())
      for (const Seg &g : it->second) {
        bool via = g.xl == g.xh && g.yl == g.yh;
        if ((via && g.zl == g.zh) || (!via && g.zl != g.zh)) {
          ++rep.droppedZero; // zero-length, or not a straight piece
          continue;
        }
        out.push_back(g);
        cover(g);
      }
    if (out.empty()) {
      // Unrouted (all pins in one GCell, or a one-pin net): a via stack over
      // the pins' layers at the first pin's GCell, at least one layer pair.
      ++rep.unroutedNets;
      if (!n.pins.empty()) {
        const Access &a0 = n.pins[0][0];
        int zl = a0.z, zh = a0.z;
        for (const std::vector<Access> &p : n.pins)
          if (p[0].x == a0.x && p[0].y == a0.y) {
            zl = std::min(zl, p[0].z);
            zh = std::max(zh, p[0].z);
          }
        if (zh == zl) {
          if (zh + 1 < r.nLayers)
            ++zh;
          else
            --zl;
        }
        Seg v{a0.x, a0.y, zl, a0.x, a0.y, zh};
        out.push_back(v);
        cover(v);
      }
    }
    for (const std::vector<Access> &p : n.pins) {
      bool ok = false;
      for (const Access &a : p)
        ok = ok || covered.count({a.x, a.y, a.z});
      if (ok)
        continue;
      const Access &a = p[0];
      int best = -1;
      for (int z = 0; z < r.nLayers; ++z)
        if (covered.count({a.x, a.y, z}) && (best < 0 || std::abs(z - a.z) < std::abs(best - a.z)))
          best = z;
      if (best < 0)
        best = a.z + 1 < r.nLayers ? a.z + 1 : a.z - 1; // nothing here at all: a lone via (still open)
      Seg v{a.x, a.y, std::min(a.z, best), a.x, a.y, std::max(a.z, best)};
      out.push_back(v);
      cover(v);
      ++rep.pinFixes;
    }
    o << n.name << "\n(\n";
    for (const Seg &g : out) {
      o << g.xl << " " << g.yl << " " << g.zl << " " << g.xh << " " << g.yh << " " << g.zh << "\n";
      if (g.zl == g.zh)
        ++rep.wires;
      else
        ++rep.vias;
    }
    o << ")\n";
  }
  return rep;
}

} // namespace g24b

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}
int run() {
  std::cout << "g24_bridge self test\n";
  using namespace g24b;
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
  writeGr(r, nets, gr);
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
  writeGr(r, nets, gr2, 1, 1);
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
  FromReport fr = convertSolution(r, nets, sol, out);
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
  g24b::Resources r;
  std::vector<g24b::Net> nets;
  std::string err;
  std::ifstream ci(argv[2]), ni(argv[3]);
  if (!ci || !ni || !g24b::readResources(ci, r, err) || !g24b::readNets(ni, nets, err)) {
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
    g24b::writeGr(r, nets, o, reserve, reserveLayers);
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
  g24b::FromReport fr = g24b::convertSolution(r, nets, si, o);
  std::cerr << "converted " << fr.nets << " nets: " << fr.wires << " wires, " << fr.vias << " vias, "
            << fr.droppedZero << " zero-length pieces dropped, " << fr.pinFixes << " pins given a via, "
            << fr.unroutedNets << " unrouted nets given a via stack\n";
  return 0;
}
