// g24_io.h
//
// The ISPD 2024 global routing format (.cap resources, .net netlist,
// GCell-coordinate solution): the readers, the mapping into the router's
// gr_io::Design, and the solution writer (roadmap step 3; ISPD24_FORMAT.md,
// HISTORY_COST_SWEEP.md Parts 102-103). Shared by g24_verify (the scorer,
// cross-checked against the contest evaluator), g24_bridge (the .gr
// bridge) and the native routers (pd33route on), so the format has one
// definition.
//
// The porting seam (CLAUDE.md): RoutingGrid stays format-agnostic. What the
// 2008 Design cannot hold -- non-uniform pitches, fractional capacities,
// per-layer overflow weights, layer directions, pins as sets of access
// points -- stays in the Resources / Net types here, for the router
// mechanisms that need it to read alongside the Design.
//
// The mapping (toDesign), identical to the Part 102 bridge's:
//   one tile per GCell; tile 10 x 10 at origin 0 (only so the router's
//   real-coordinate output maps back: GCell x -> 10x);
//   capacity in half tracks, floor(2 x tracks), so a net (minimum width 1 +
//   spacing 1 = 2) uses one track, the evaluator's demand unit; GCell
//   (x, y)'s capacity on a horizontal layer is edge (x,y)-(x+1,y)'s, on a
//   vertical layer edge (x,y)-(x,y+1)'s (the evaluator charges a wire's
//   run [lo, hi) to the GCells it leaves); 0 across the layer's direction
//   and on layer 0 (not routable); each layer's most common nonzero value
//   is its nominal capacity (Part 104: the router reads a layer's direction
//   from its nominal capacity, so a layer blocked in most GCells -- macros
//   on ariane's M2-M4 -- whose most common value is 0 must still get its
//   open value, or it is never a routing layer);
//   each pin at its first access point;
//   optional reservation (Part 102, retired -- every setting was worse):
//   lower a layer-z edge by `reserve` half tracks per pin below z in each of
//   its two GCells, for z in 1..reserveLayers.

#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <istream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gr_io.h"

namespace g24 {

struct Layer {
  std::string name;
  int dir = 0; // 0 horizontal, 1 vertical
  double minLength = 0;
};

struct Resources {
  int nLayers = 0, xSize = 0, ySize = 0;
  double unitLength = 0, unitVia = 0;
  std::vector<double> overflowCost; // per layer
  std::vector<long long> xPitch, yPitch; // xSize-1, ySize-1
  std::vector<Layer> layers;
  std::vector<double> cap; // (z * ySize + y) * xSize + x
  double capacity(int z, int x, int y) const {
    return cap[(static_cast<size_t>(z) * ySize + y) * xSize + x];
  }
};

struct Access {
  int x, y, z;
};
struct Net {
  std::string name;
  std::vector<std::vector<Access>> pins;
};

inline std::string trimEnd(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
    s.pop_back();
  return s;
}

inline bool readResources(std::istream &in, Resources &r, std::string &err) {
  if (!(in >> r.nLayers >> r.xSize >> r.ySize) || r.nLayers < 2 || r.xSize < 1 || r.ySize < 1) {
    err = "bad .cap header (need nLayers >= 2, xSize >= 1, ySize >= 1)";
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
    if (!(in >> p) || p < 0) {
      err = "bad .cap horizontal pitch";
      return false;
    }
  for (long long &p : r.yPitch)
    if (!(in >> p) || p < 0) {
      err = "bad .cap vertical pitch";
      return false;
    }
  r.layers.resize(r.nLayers);
  r.cap.assign(static_cast<size_t>(r.nLayers) * r.ySize * r.xSize, 0.0);
  for (int z = 0; z < r.nLayers; ++z) {
    Layer &L = r.layers[z];
    if (!(in >> L.name >> L.dir >> L.minLength) || (z > 0 && L.dir != 0 && L.dir != 1)) {
      err = "bad .cap layer line for layer " + std::to_string(z);
      return false;
    }
    for (int y = 0; y < r.ySize; ++y)
      for (int x = 0; x < r.xSize; ++x) {
        double c;
        if (!(in >> c)) {
          err = "short capacity table on layer " + std::to_string(z);
          return false;
        }
        r.cap[(static_cast<size_t>(z) * r.ySize + y) * r.xSize + x] = c;
      }
  }
  return true;
}

// The evaluator's rule: a line with neither '(' nor ')' and more than one
// character starts a net; a line with '[' is one pin, "(z, x, y)" access
// tuples. NetStream reads one net at a time (Part 111: the cluster designs'
// netlists are 2 GB, with 120-character names); readNets is a loop over it.
class NetStream {
public:
  explicit NetStream(std::istream &in) : in_(in) {}
  // The next net into `out`; false at the end (err set on a format error).
  bool next(Net &out, std::string &err) {
    out.name.clear();
    out.pins.clear();
    std::string line;
    bool have = false;
    if (pendingName_) {
      out.name = std::move(pending_);
      pendingName_ = false;
      have = true;
    }
    while (std::getline(in_, line)) {
      line = trimEnd(line);
      if (line.find('(') == std::string::npos && line.find(')') == std::string::npos && line.length() > 1) {
        if (have) {
          pending_ = line;
          pendingName_ = true;
          return true;
        }
        out.name = line;
        have = true;
      } else if (line.find('[') != std::string::npos) {
        if (!have) {
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
          err = "net " + out.name + ": pin with no access points";
          return false;
        }
        out.pins.push_back(pin);
      }
    }
    return have;
  }

private:
  std::istream &in_;
  std::string pending_;
  bool pendingName_ = false;
};

inline bool readNets(std::istream &in, std::vector<Net> &nets, std::string &err) {
  NetStream ns(in);
  Net n;
  while (ns.next(n, err))
    nets.push_back(std::move(n));
  return err.empty();
}

// Every access point inside the grid (the evaluator indexes without checks).
inline bool checkAccessPointsNet(const Resources &r, const Net &n, std::string &err) {
  for (const std::vector<Access> &p : n.pins)
    for (const Access &a : p)
      if (a.x < 0 || a.x >= r.xSize || a.y < 0 || a.y >= r.ySize || a.z < 0 || a.z >= r.nLayers) {
        err = "net " + n.name + ": access point outside the grid";
        return false;
      }
  return true;
}
inline bool checkAccessPoints(const Resources &r, const std::vector<Net> &nets, std::string &err) {
  for (const Net &n : nets)
    if (!checkAccessPointsNet(r, n, err))
      return false;
  return true;
}

// Access-point choice (Part 105): returns the nets with each pin's chosen
// access point moved to the front (toDesign and convertSolution route to
// and fix at the first one; any access point still counts as covered).
// The rule is InstantGR's (ISPD 2024 contest winner; cuhk-eda/InstantGR,
// database.hpp net::init), with its box and center over every access point
// of the net:
//   up to 4 pins: every combination; fewest pin pairs that differ in x plus
//     pairs that differ in y (aligned pins -> straight wires, fewer bends),
//     then the smallest summed distance to the center; first found wins ties;
//   more pins: each pin's access point nearest the center (first wins ties).
inline void chooseAccessPointsNet(Net &n) {
  {
    const size_t k = n.pins.size();
    if (k == 0)
      return;
    int x0 = n.pins[0][0].x, x1 = x0, y0 = n.pins[0][0].y, y1 = y0;
    for (const auto &p : n.pins)
      for (const Access &a : p) {
        x0 = std::min(x0, a.x);
        x1 = std::max(x1, a.x);
        y0 = std::min(y0, a.y);
        y1 = std::max(y1, a.y);
      }
    const int cx2 = x0 + x1, cy2 = y0 + y1;
    auto dist = [&](const Access &a) { return std::abs(2 * a.x - cx2) + std::abs(2 * a.y - cy2); };
    std::vector<size_t> pick(k, 0);
    if (k <= 4) {
      std::vector<size_t> idx(k, 0);
      long long bestDiff = -1, bestMetric = 0;
      for (;;) {
        long long diff = 0, metric = 0;
        for (size_t i = 0; i < k; ++i) {
          const Access &ai = n.pins[i][idx[i]];
          metric += dist(ai);
          for (size_t j = 0; j < i; ++j) {
            const Access &aj = n.pins[j][idx[j]];
            diff += (ai.x != aj.x) + (ai.y != aj.y);
          }
        }
        if (bestDiff < 0 || diff < bestDiff || (diff == bestDiff && metric < bestMetric)) {
          bestDiff = diff;
          bestMetric = metric;
          pick = idx;
        }
        // next combination (last pin varies fastest, as a nested loop would)
        size_t i = k;
        while (i > 0) {
          --i;
          if (++idx[i] < n.pins[i].size())
            break;
          idx[i] = 0;
          if (i == 0) {
            i = k + 1; // done
            break;
          }
        }
        if (i == k + 1)
          break;
      }
    } else {
      for (size_t i = 0; i < k; ++i)
        for (size_t j = 1; j < n.pins[i].size(); ++j)
          if (dist(n.pins[i][j]) < dist(n.pins[i][pick[i]]))
            pick[i] = j;
    }
    for (size_t i = 0; i < k; ++i)
      std::swap(n.pins[i][0], n.pins[i][pick[i]]);
  }
}
inline std::vector<Net> chooseAccessPoints(const std::vector<Net> &nets) {
  std::vector<Net> out = nets;
  for (Net &n : out)
    chooseAccessPointsNet(n);
  return out;
}

inline int halfTracks(double tracks) { return tracks <= 0 ? 0 : static_cast<int>(2.0 * tracks + 1e-9); }

// One net into a Design, as net `id`, routed to each pin's first access
// point. keepName false leaves the name empty (the streaming path, which
// writes the solution from the netlist itself, never needs it).
inline void appendDesignNet(gr_io::Design &d, const Net &net, size_t id, bool keepName = true) {
  gr_io::Net n;
  if (keepName)
    n.name = net.name;
  n.id = static_cast<long long>(id);
  n.minWidth = 1;
  n.pins.reserve(net.pins.size());
  for (const std::vector<Access> &p : net.pins)
    n.pins.push_back(gr_io::Pin{p[0].x, p[0].y, p[0].z});
  d.nets.push_back(std::move(n));
}

// The router's Design for a 2024 design (see the header for the mapping).
inline gr_io::Design toDesign(const Resources &r, const std::vector<Net> &nets, int reserve = 0,
                              int reserveLayers = 1) {
  gr_io::Design d;
  gr_io::RoutingGrid &g = d.grid;
  g.nx = r.xSize;
  g.ny = r.ySize;
  g.nLayers = r.nLayers;
  g.llx = g.lly = 0;
  g.tileW = g.tileH = 10;
  g.minWidth.assign(r.nLayers, 1);
  g.minSpacing.assign(r.nLayers, 1);
  g.viaSpacing.assign(r.nLayers, 0);
  std::vector<std::vector<int>> pinsBelow(r.nLayers);
  if (reserve > 0)
    for (int z = 1; z <= reserveLayers && z < r.nLayers; ++z) {
      pinsBelow[z].assign(static_cast<size_t>(r.xSize) * r.ySize, 0);
      for (const Net &n : nets)
        for (const std::vector<Access> &p : n.pins)
          if (p[0].z < z)
            ++pinsBelow[z][static_cast<size_t>(p[0].y) * r.xSize + p[0].x];
    }
  auto edgeCap = [&](int z, int x, int y) {
    int c = halfTracks(r.capacity(z, x, y));
    if (pinsBelow[z].empty())
      return c;
    bool h = r.layers[z].dir == 0;
    int pins = pinsBelow[z][static_cast<size_t>(y) * r.xSize + x] +
               pinsBelow[z][static_cast<size_t>(h ? y : y + 1) * r.xSize + (h ? x + 1 : x)];
    return std::max(0, c - reserve * pins);
  };
  g.hCapLayer.assign(r.nLayers, 0);
  g.vCapLayer.assign(r.nLayers, 0);
  for (int z = 1; z < r.nLayers; ++z) {
    std::map<int, long long> count;
    bool h = r.layers[z].dir == 0;
    for (int y = 0; y < (h ? r.ySize : r.ySize - 1); ++y)
      for (int x = 0; x < (h ? r.xSize - 1 : r.xSize); ++x)
        ++count[edgeCap(z, x, y)];
    long long best = -1;
    int def = 0;
    for (const auto &kv : count)
      if (kv.first > 0 && kv.second > best) {
        best = kv.second;
        def = kv.first;
      }
    (h ? g.hCapLayer : g.vCapLayer)[z] = def;
  }
  g.allocate();
  for (int z = 1; z < r.nLayers; ++z) {
    bool h = r.layers[z].dir == 0;
    for (int y = 0; y < (h ? r.ySize : r.ySize - 1); ++y)
      for (int x = 0; x < (h ? r.xSize - 1 : r.xSize); ++x)
        (h ? g.hcap[g.hIndex(x, y, z)] : g.vcap[g.vIndex(x, y, z)]) = edgeCap(z, x, y);
  }
  d.nets.reserve(nets.size());
  for (size_t i = 0; i < nets.size(); ++i)
    appendDesignNet(d, nets[i], i);
  return d;
}

// The Design's grid alone (no nets), for a caller that streams the nets in
// with appendDesignNet (Part 111). The same as toDesign's grid with no
// reservation.
inline gr_io::Design designGrid(const Resources &r) { return toDesign(r, {}); }

// A Design as .gr text: nominal capacities from hCapLayer / vCapLayer, an
// adjustment for every edge that differs (layer by layer, horizontal edges
// then vertical, row by row), pins at tile centres.
inline void writeGr(const gr_io::Design &d, std::ostream &o) {
  const gr_io::RoutingGrid &g = d.grid;
  auto row = [&](const char *label, const std::vector<int> &v) {
    o << label;
    for (int x : v)
      o << " " << x;
    o << "\n";
  };
  o << "grid " << g.nx << " " << g.ny << " " << g.nLayers << "\n";
  row("vertical capacity", g.vCapLayer);
  row("horizontal capacity", g.hCapLayer);
  row("minimum width", g.minWidth);
  row("minimum spacing", g.minSpacing);
  row("via spacing", g.viaSpacing);
  o << g.llx << " " << g.lly << " " << g.tileW << " " << g.tileH << "\nnum net " << d.nets.size() << "\n";
  for (const gr_io::Net &n : d.nets) {
    o << n.name << " " << n.id << " " << n.pins.size() << " " << n.minWidth << "\n";
    for (const gr_io::Pin &p : n.pins)
      o << g.llx + p.x * g.tileW + g.tileW / 2 << " " << g.lly + p.y * g.tileH + g.tileH / 2 << " " << p.layer + 1
        << "\n";
  }
  std::ostringstream adj;
  long long count = 0;
  for (int l = 0; l < g.nLayers; ++l) {
    for (int y = 0; y < g.ny; ++y)
      for (int x = 0; x + 1 < g.nx; ++x)
        if (g.hcap[g.hIndex(x, y, l)] != g.hCapLayer[l]) {
          adj << x << " " << y << " " << l + 1 << " " << x + 1 << " " << y << " " << l + 1 << " "
              << g.hcap[g.hIndex(x, y, l)] << "\n";
          ++count;
        }
    for (int y = 0; y + 1 < g.ny; ++y)
      for (int x = 0; x < g.nx; ++x)
        if (g.vcap[g.vIndex(x, y, l)] != g.vCapLayer[l]) {
          adj << x << " " << y << " " << l + 1 << " " << x << " " << y + 1 << " " << l + 1 << " "
              << g.vcap[g.vIndex(x, y, l)] << "\n";
          ++count;
        }
  }
  o << count << "\n" << adj.str();
}

struct Seg {
  int xl, yl, zl, xh, yh, zh;
};

struct SolutionReport {
  long long nets = 0, wires = 0, vias = 0, droppedZero = 0, pinFixes = 0, unroutedNets = 0;
};

// A router solution in .gr text (real coordinates on toDesign's 10 x 10
// tiles; net headers "name id", id = the net's index) as ISPD 2024 GCell
// segments: a wire keeps its layer and runs low to high; a via runs z low
// to z high; zero-length pieces are dropped. Then every pin must be covered
// at one of its access points (the evaluator reaches only access points the
// net covers): a pin whose chosen access point is not covered gets a via
// between its layer and the nearest layer the net covers at that GCell; a
// net with no segments (all pins in one GCell) gets a via stack over its
// pins' layers there, at least one layer pair.
// One net's ISPD 2024 segments from its route's pieces (tile coordinates,
// low to high), with the pin-coverage fixes -- see convertSolution.
inline void emitNetSolution(const Resources &r, const Net &n, const std::vector<Seg> &netSegs, std::ostream &o,
                            SolutionReport &rep) {
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
  for (const Seg &g : netSegs) {
    bool via = g.xl == g.xh && g.yl == g.yh;
    if ((via && g.zl == g.zh) || (!via && g.zl != g.zh)) {
      ++rep.droppedZero; // zero-length, or not a straight piece
      continue;
    }
    out.push_back(g);
    cover(g);
  }
  if (out.empty()) {
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

inline SolutionReport convertSolution(const Resources &r, const std::vector<Net> &nets, std::istream &sol,
                                      std::ostream &o) {
  SolutionReport rep;
  std::vector<std::vector<Seg>> segs(nets.size());
  std::string line;
  long long cur = -1;
  auto tile = [](long long v) { return static_cast<int>(v / 10); };
  while (std::getline(sol, line)) {
    line = trimEnd(line);
    size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos)
      continue;
    std::string s = line.substr(b);
    if (s == "!") {
      cur = -1;
      continue;
    }
    if (s[0] == '(') {
      long long x1, y1, l1, x2, y2, l2;
      if (cur < 0 || std::sscanf(s.c_str(), "(%lld,%lld,%lld)-(%lld,%lld,%lld)", &x1, &y1, &l1, &x2, &y2, &l2) != 6)
        continue;
      int ax = tile(x1), ay = tile(y1), az = static_cast<int>(l1) - 1, bx = tile(x2), by = tile(y2),
          bz = static_cast<int>(l2) - 1;
      segs[static_cast<size_t>(cur)].push_back(
          {std::min(ax, bx), std::min(ay, by), std::min(az, bz), std::max(ax, bx), std::max(ay, by), std::max(az, bz)});
      continue;
    }
    std::istringstream hs(s);
    std::string name;
    long long id = -1;
    hs >> name >> id;
    if (id < 0 || id >= static_cast<long long>(nets.size()) || nets[static_cast<size_t>(id)].name != name)
      gr_io::fail("solution net header '" + s + "' does not match the netlist");
    cur = id;
  }
  for (size_t ni = 0; ni < nets.size(); ++ni)
    emitNetSolution(r, nets[ni], segs[ni], o, rep);
  return rep;
}

} // namespace g24
