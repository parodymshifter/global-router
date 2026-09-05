// gr_verify.cpp
//
// Reader for the ISPD global routing benchmark format (.gr) and a verifier
// for a routing solution against that benchmark.
//
// Build:
//     clang++ -std=c++17 -O2 -Wall -Wextra -o gr_verify gr_verify.cpp
//
// Use:
//     ./gr_verify --selftest
//     ./gr_verify benchmark.gr solution.route
//     ./gr_verify benchmark.gr            (reports the parsed design only)
//
// The verifier answers four questions independently of any router:
//     1. Is every net's route a connected subgraph containing all its pins?
//     2. Does any edge carry more than its capacity, and by how much?
//     3. What is the total wirelength and via count?
//     4. Is every segment well formed (axis aligned, on the grid)?
//
// Design note: RoutingGrid owns the capacity model and edge indexing and knows
// nothing about file formats. That separation is deliberate. Swapping the .gr
// reader for a LEF/DEF front end later should not touch anything below it.

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../gr/gr_io.h"

using gr_io::Design;
using gr_io::fail;
using gr_io::Net;
using gr_io::Pin;
using gr_io::readGr;
using gr_io::RoutingGrid;

// ---------------------------------------------------------------------------
// Solution reader
// ---------------------------------------------------------------------------

// Format:
//   <netName> <netId>
//   (x1,y1,l1)-(x2,y2,l2)
//   ...
//   !
//
// Coordinates are real, layers one based. A segment is axis aligned in x, in y,
// or is a via with x and y equal and layers differing.

struct Segment {
  int x1, y1, l1;
  int x2, y2, l2; // tile coordinates, zero based layers
};

struct RoutedNet {
  std::string name;
  long long id = 0;
  std::vector<Segment> segs;
};

std::vector<RoutedNet> readSolution(const std::string &path,
                                    const RoutingGrid &g) {
  std::ifstream in(path);
  if (!in)
    fail("cannot open " + path);

  std::vector<RoutedNet> out;
  std::string line;
  RoutedNet cur;
  bool open = false;

  auto flush = [&]() {
    if (open) {
      out.push_back(std::move(cur));
      cur = RoutedNet();
      open = false;
    }
  };

  size_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    // Trim.
    size_t b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
      continue;
    size_t e = line.find_last_not_of(" \t\r\n");
    std::string s = line.substr(b, e - b + 1);

    if (s == "!") {
      flush();
      continue;
    }

    if (s[0] == '(') {
      if (!open)
        fail("segment before any net header at line " +
             std::to_string(lineNo));
      long long x1, y1, l1, x2, y2, l2;
      if (std::sscanf(s.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1,
                      &l1, &x2, &y2, &l2) != 6)
        fail("malformed segment at line " + std::to_string(lineNo) + ": " + s);
      Segment sg;
      sg.x1 = g.colOf(x1);
      sg.y1 = g.rowOf(y1);
      sg.l1 = static_cast<int>(l1) - 1;
      sg.x2 = g.colOf(x2);
      sg.y2 = g.rowOf(y2);
      sg.l2 = static_cast<int>(l2) - 1;
      cur.segs.push_back(sg);
      continue;
    }

    // Net header.
    flush();
    std::istringstream hs(s);
    hs >> cur.name >> cur.id;
    open = true;
  }
  flush();
  return out;
}

// ---------------------------------------------------------------------------
// Verifier
// ---------------------------------------------------------------------------

struct Report {
  // Geometry problems, found before anything else is trusted.
  long long malformedSegments = 0;
  long long outOfBoundsSegments = 0;

  // Connectivity.
  long long disconnectedNets = 0;
  long long netsWithMissingPins = 0;
  long long unroutedNets = 0;

  // Congestion.
  long long totalOverflow = 0;
  int maxOverflow = 0;
  long long overflowedEdges = 0;

  // Size.
  long long wirelength = 0; // unit grid edges, width weighted
  long long viaCount = 0;

  std::vector<std::string> firstProblems; // capped sample for the user

  void note(const std::string &s) {
    if (firstProblems.size() < 20)
      firstProblems.push_back(s);
  }

  bool clean() const {
    return malformedSegments == 0 && outOfBoundsSegments == 0 &&
           disconnectedNets == 0 && netsWithMissingPins == 0 &&
           unroutedNets == 0 && totalOverflow == 0;
  }
};

// Union find over the tiles a single net touches.
class DisjointSet {
public:
  int add() {
    parent_.push_back(static_cast<int>(parent_.size()));
    return static_cast<int>(parent_.size()) - 1;
  }
  int find(int a) {
    while (parent_[a] != a) {
      parent_[a] = parent_[parent_[a]];
      a = parent_[a];
    }
    return a;
  }
  void unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a != b)
      parent_[a] = b;
  }
  void clear() { parent_.clear(); }

private:
  std::vector<int> parent_;
};

Report verify(const Design &d, const std::vector<RoutedNet> &sol) {
  const RoutingGrid &g = d.grid;
  Report rep;

  std::vector<int> husage(g.hCount(), 0);
  std::vector<int> vusage(g.vCount(), 0);

  // Index the solution by net name so missing nets are detectable.
  std::unordered_set<std::string> routed;
  routed.reserve(sol.size() * 2);

  // Map a tile triple to a dense id, for union find inside one net.
  auto tileKey = [&](int x, int y, int l) -> long long {
    return (static_cast<long long>(l) * g.ny + y) * g.nx + x;
  };

  for (const RoutedNet &rn : sol) {
    routed.insert(rn.name);

    // Per net edge lists, charged once per occurrence rather than deduplicated:
    // the official ISPD scorer (eval2008.pl) charges every segment as written,
    // so a net that emits overlapping segments pays for the overlap. Verified
    // against eval2008.pl's total-overflow and wirelength output directly --
    // deduplicating here silently undercounted both.
    std::vector<size_t> hEdges, vEdges;
    std::unordered_set<long long> nodes;
    std::vector<std::pair<long long, long long>> links;
    long long netVias = 0;

    for (const Segment &s : rn.segs) {
      bool sameLayer = (s.l1 == s.l2);
      bool sameXY = (s.x1 == s.x2 && s.y1 == s.y2);

      if (!g.inBounds(s.x1, s.y1, s.l1) || !g.inBounds(s.x2, s.y2, s.l2)) {
        ++rep.outOfBoundsSegments;
        rep.note("net " + rn.name + ": segment leaves the grid");
        continue;
      }

      if (!sameLayer) {
        if (!sameXY) {
          ++rep.malformedSegments;
          rep.note("net " + rn.name + ": layer change with x or y movement");
          continue;
        }
        // Via stack. Count one via per layer crossed.
        int lo = std::min(s.l1, s.l2), hi = std::max(s.l1, s.l2);
        netVias += (hi - lo);
        for (int l = lo; l <= hi; ++l)
          nodes.insert(tileKey(s.x1, s.y1, l));
        for (int l = lo; l + 1 <= hi; ++l)
          links.emplace_back(tileKey(s.x1, s.y1, l),
                             tileKey(s.x1, s.y1, l + 1));
        continue;
      }

      if (s.x1 != s.x2 && s.y1 != s.y2) {
        ++rep.malformedSegments;
        rep.note("net " + rn.name + ": diagonal segment");
        continue;
      }

      if (s.y1 == s.y2) { // horizontal
        int a = std::min(s.x1, s.x2), b = std::max(s.x1, s.x2);
        for (int x = a; x < b; ++x)
          hEdges.push_back(g.hIndex(x, s.y1, s.l1));
        for (int x = a; x <= b; ++x)
          nodes.insert(tileKey(x, s.y1, s.l1));
        for (int x = a; x < b; ++x)
          links.emplace_back(tileKey(x, s.y1, s.l1),
                             tileKey(x + 1, s.y1, s.l1));
      } else { // vertical
        int a = std::min(s.y1, s.y2), b = std::max(s.y1, s.y2);
        for (int y = a; y < b; ++y)
          vEdges.push_back(g.vIndex(s.x1, y, s.l1));
        for (int y = a; y <= b; ++y)
          nodes.insert(tileKey(s.x1, y, s.l1));
        for (int y = a; y < b; ++y)
          links.emplace_back(tileKey(s.x1, y, s.l1),
                             tileKey(s.x1, y + 1, s.l1));
      }
    }

    rep.viaCount += netVias;

    // Find this net in the design so we can check its pins and width.
    const Net *net = nullptr;
    for (const Net &n : d.nets)
      if (n.name == rn.name) {
        net = &n;
        break;
      }
    int width = net ? net->minWidth : 1;

    // Charge usage. The ISPD capacity model measures available space, not
    // track count: an edge's utilization is the sum of the widths of the nets
    // crossing it plus the layer minimum spacing once per net. So a net on
    // layer l consumes (minWidth + minSpacing[l]) units, not 1.
    for (size_t e : hEdges) {
      int l = static_cast<int>(e / (static_cast<size_t>(g.ny) * (g.nx - 1)));
      husage[e] += width + g.minSpacing[l];
    }
    for (size_t e : vEdges) {
      int l = static_cast<int>(e / (static_cast<size_t>(g.ny - 1) * g.nx));
      vusage[e] += width + g.minSpacing[l];
    }
    // Wirelength is reported in tile-to-tile steps, unweighted, as the contest
    // scoring defines it.
    rep.wirelength += static_cast<long long>(hEdges.size() + vEdges.size());

    if (!net)
      continue;

    // Connectivity: all pins in one component of the routed subgraph.
    if (net->pins.size() <= 1)
      continue;
    if (nodes.empty()) {
      ++rep.unroutedNets;
      rep.note("net " + rn.name + ": multi pin net with no route");
      continue;
    }

    std::vector<long long> nodeList(nodes.begin(), nodes.end());
    std::sort(nodeList.begin(), nodeList.end());
    auto idOf = [&](long long k) -> int {
      auto it = std::lower_bound(nodeList.begin(), nodeList.end(), k);
      if (it == nodeList.end() || *it != k)
        return -1;
      return static_cast<int>(it - nodeList.begin());
    };

    DisjointSet ds;
    for (size_t i = 0; i < nodeList.size(); ++i)
      ds.add();
    for (auto &lk : links) {
      int a = idOf(lk.first), b = idOf(lk.second);
      if (a >= 0 && b >= 0)
        ds.unite(a, b);
    }

    int root = -1;
    bool missing = false, split = false;
    for (const Pin &p : net->pins) {
      // The route must reach the pin's exact declared layer, not merely its
      // tile on some other layer. The official ISPD scorer checks reachability
      // this way (eval2008.pl does the BFS keyed on the pin's own layer with
      // no fallback), so a looser check here would pass solutions the real
      // scorer rejects.
      int pid = idOf(tileKey(p.x, p.y, p.layer));
      if (pid < 0) {
        missing = true;
        continue;
      }
      int r = ds.find(pid);
      if (root < 0)
        root = r;
      else if (r != root)
        split = true;
    }
    if (missing) {
      ++rep.netsWithMissingPins;
      rep.note("net " + rn.name + ": route does not reach every pin");
    }
    if (split) {
      ++rep.disconnectedNets;
      rep.note("net " + rn.name + ": route is in more than one piece");
    }
  }

  // Nets present in the design but absent from the solution.
  //
  // The contest rules exempt two categories: nets with more than 1000 pins,
  // and nets whose pins all land in a single tile. Flagging those as errors
  // would produce thousands of false positives on a real benchmark, which is
  // the fastest way to make a verifier useless.
  for (const Net &n : d.nets) {
    if (n.pins.size() <= 1)
      continue;
    if (n.pins.size() > 1000)
      continue;
    bool oneTile = true;
    for (size_t i = 1; i < n.pins.size() && oneTile; ++i)
      if (n.pins[i].x != n.pins[0].x || n.pins[i].y != n.pins[0].y)
        oneTile = false;
    if (oneTile)
      continue;
    if (routed.find(n.name) == routed.end()) {
      ++rep.unroutedNets;
      rep.note("net " + n.name + ": missing from the solution file");
    }
  }

  // Congestion.
  auto scan = [&](const std::vector<int> &use, const std::vector<int> &cap) {
    for (size_t i = 0; i < use.size(); ++i) {
      int ov = use[i] - cap[i];
      if (ov > 0) {
        rep.totalOverflow += ov;
        rep.maxOverflow = std::max(rep.maxOverflow, ov);
        ++rep.overflowedEdges;
      }
    }
  };
  scan(husage, g.hcap);
  scan(vusage, g.vcap);

  return rep;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

void printDesign(const Design &d) {
  const RoutingGrid &g = d.grid;
  std::cout << "design\n";
  std::cout << "  grid            " << g.nx << " x " << g.ny << " x "
            << g.nLayers << "\n";
  std::cout << "  tile            " << g.tileW << " x " << g.tileH
            << " at (" << g.llx << "," << g.lly << ")\n";
  std::cout << "  h edges         " << g.hCount() << "\n";
  std::cout << "  v edges         " << g.vCount() << "\n";
  std::cout << "  total capacity  " << g.totalCapacity() << "\n";
  std::cout << "  nets            " << d.nets.size() << "\n";

  size_t pins = 0, multi = 0, maxPins = 0;
  for (const Net &n : d.nets) {
    pins += n.pins.size();
    if (n.pins.size() > 1)
      ++multi;
    maxPins = std::max(maxPins, n.pins.size());
  }
  std::cout << "  pins            " << pins << "\n";
  std::cout << "  multi pin nets  " << multi << "\n";
  std::cout << "  largest net     " << maxPins << " pins\n";
}

void printReport(const Report &r) {
  std::cout << "\nverification\n";
  std::cout << "  wirelength         " << r.wirelength << "\n";
  std::cout << "  vias               " << r.viaCount << "\n";
  // Contest scoring charges k=1 tile of wirelength per via crossing.
  std::cout << "  scored wirelength  " << (r.wirelength + r.viaCount)
            << "   (wirelength + 1 per via)\n";
  std::cout << "  total overflow     " << r.totalOverflow << "\n";
  std::cout << "  max overflow       " << r.maxOverflow << "\n";
  std::cout << "  overflowed edges   " << r.overflowedEdges << "\n";
  std::cout << "  malformed segments " << r.malformedSegments << "\n";
  std::cout << "  out of bounds      " << r.outOfBoundsSegments << "\n";
  std::cout << "  disconnected nets  " << r.disconnectedNets << "\n";
  std::cout << "  incomplete nets    " << r.netsWithMissingPins << "\n";
  std::cout << "  unrouted nets      " << r.unroutedNets << "\n";

  if (!r.firstProblems.empty()) {
    std::cout << "\nfirst problems\n";
    for (const std::string &s : r.firstProblems)
      std::cout << "  " << s << "\n";
  }
  std::cout << "\n" << (r.clean() ? "PASS" : "FAIL") << "\n";
}

// ---------------------------------------------------------------------------
// Self test
// ---------------------------------------------------------------------------

// The point of these is not to show the verifier working on good input. It is
// to show it failing on input that is broken in a specific way. A verifier that
// only ever passes is indistinguishable from one that always passes.

namespace selftest {

const char *kGr = R"(grid 4 4 2
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
netB 1 2 1
5 15 1
35 15 1
netC 2 2 1
5 25 1
5 35 1
0
)";

// Clean: three nets, each within capacity, each connected. netC's pins are
// declared on layer 1 but the run is on layer 2, so it needs a via at each
// end down to the pin's actual layer.
const char *kGood = R"(netA 0
(5,5,1)-(35,5,1)
!
netB 1
(5,15,1)-(35,15,1)
!
netC 2
(5,25,1)-(5,25,2)
(5,25,2)-(5,35,2)
(5,35,2)-(5,35,1)
!
)";

// netA is drawn in two pieces that do not meet.
const char *kSplit = R"(netA 0
(5,5,1)-(15,5,1)
(25,5,1)-(35,5,1)
!
netB 1
(5,15,1)-(35,15,1)
!
netC 2
(5,25,2)-(5,35,2)
!
)";

// netC omitted entirely.
const char *kMissing = R"(netA 0
(5,5,1)-(35,5,1)
!
netB 1
(5,15,1)-(35,15,1)
!
)";

// A diagonal segment, which is not representable on the grid.
const char *kDiagonal = R"(netA 0
(5,5,1)-(35,15,1)
!
netB 1
(5,15,1)-(35,15,1)
!
netC 2
(5,25,2)-(5,35,2)
!
)";

void write(const std::string &path, const char *text) {
  std::ofstream out(path);
  if (!out)
    fail("cannot write " + path);
  out << text;
}

int failures = 0;

void check(bool cond, const std::string &what) {
  std::cout << (cond ? "  ok   " : "  FAIL ") << what << "\n";
  if (!cond)
    ++failures;
}

int run() {
  const std::string gr = "selftest.gr";
  write(gr, kGr);
  Design d = readGr(gr);

  std::cout << "self test\n";
  check(d.grid.nx == 4 && d.grid.ny == 4 && d.grid.nLayers == 2,
        "grid dimensions parsed");
  check(d.nets.size() == 3, "three nets parsed");
  check(d.nets[0].pins.size() == 2, "netA has two pins");
  check(d.nets[0].pins[0].x == 0 && d.nets[0].pins[0].y == 0,
        "pin coordinates map to tile 0,0");
  check(d.nets[0].pins[1].x == 3, "second pin maps to column 3");
  check(d.grid.hcap[d.grid.hIndex(0, 0, 0)] == 2, "layer 1 h capacity is 2");
  check(d.grid.vcap[d.grid.vIndex(0, 0, 1)] == 2, "layer 2 v capacity is 2");

  auto runCase = [&](const char *sol, const char *name) {
    const std::string p = std::string("selftest_") + name + ".route";
    write(p, sol);
    return verify(d, readSolution(p, d.grid));
  };

  Report good = runCase(kGood, "good");
  check(good.clean(), "clean solution passes");
  check(good.wirelength == 7, "wirelength is 7 unit edges");
  check(good.totalOverflow == 0, "no overflow on the clean solution");

  Report split = runCase(kSplit, "split");
  check(split.disconnectedNets == 1, "split route is detected");
  check(!split.clean(), "split solution fails");

  Report missing = runCase(kMissing, "missing");
  check(missing.unroutedNets == 1, "missing net is detected");
  check(!missing.clean(), "missing solution fails");

  Report diag = runCase(kDiagonal, "diagonal");
  check(diag.malformedSegments == 1, "diagonal segment is detected");
  check(!diag.clean(), "diagonal solution fails");

  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL")
            << "\n";
  return failures == 0 ? 0 : 1;
}

} // namespace selftest

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
  std::ios::sync_with_stdio(false);

  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();

  if (argc < 2 || argc > 3) {
    std::cerr << "usage: " << argv[0] << " <benchmark.gr> [solution.route]\n";
    std::cerr << "       " << argv[0] << " --selftest\n";
    return 2;
  }

  Design d = readGr(argv[1]);
  printDesign(d);

  if (argc == 3) {
    std::vector<RoutedNet> sol = readSolution(argv[2], d.grid);
    std::cout << "  routed nets     " << sol.size() << "\n";
    Report r = verify(d, sol);
    printReport(r);
    return r.clean() ? 0 : 1;
  }
  return 0;
}
