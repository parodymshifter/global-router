// g24_verify.cpp -- verifier and scorer for the ISPD 2024 global routing
// format (.cap resources, .net netlist, GCell-coordinate solution), roadmap
// step 3. Built before any router reads the format, as gr_verify.cpp was for
// ISPD 2008 (CLAUDE.md, build order 1).
//
//   g24_verify <design.cap> <design.net> <solution>   score + legality
//   g24_verify --selftest
//
// The scoring rules replicate the contest evaluator (ISPD24 contest,
// evaluator.cpp as distributed with cuhk-eda/InstantGR, run/evaluator.cpp),
// read line by line; ISPD24_FORMAT.md at the repo root records them. In
// short:
//   .cap  line 1: nLayers xSize ySize
//         line 2: unitLengthCost unitViaCost overflowCost[0..nLayers-1]
//         xSize-1 horizontal GCell pitches, then ySize-1 vertical ones
//         per layer: "name direction minLength", then ySize rows of xSize
//         capacities (tracks, may be fractional). Layer 0 is not routable.
//   .net  a net name line, "(", one "[(z, x, y), (z, x, y), ...]" line per
//         pin listing its access points, ")".
//   solution: a net name line, "(", "xl yl zl xh yh zh" segments (a wire
//         on one layer along its direction with xl < xh or yl < yh, or a
//         via with equal x, y and zl < zh), ")".
//   Scoring: wire demand 1 track on GCells [lo, hi) of the run; a via
//   between z and z+1 at (x, y) that the net does not already cover on
//   layer z adds half a track to each of the two layer-z GCells beside it
//   along that layer's direction (a full track to the one inside at the
//   grid edge). Per routing-layer GCell: exp(0.5 * (demand - capacity)) when
//   capacity > 0.001, exp(0.75 * 2 * demand) when capacity is 0 and demand
//   > 0 -- every GCell counts, used or not. Costs: unitLength * sum of wire
//   pitch lengths + unitVia * vias (each layer crossing) + sum over layers
//   of overflowCost[z] * that layer's GCell sum.
//
// Beyond the official evaluator (which asserts or exits on them), every
// structural problem is reported, not crashed on: out-of-range or reversed
// coordinates, wires on layer 0 or against a layer's direction, diagonal
// segments, unknown or repeated nets, unconnected pins, nets missing from
// the solution. A solution with any of these is ILLEGAL; its costs are
// still printed for diagnosis.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "g24_io.h"

namespace g24 {

// The format types and readers (Layer, Resources, Access, Net,
// readResources, readNets) live in g24_io.h, shared with the bridge and
// the native routers.

struct Report {
  double wlCost = 0, viaCost = 0, overflowCost = 0, total = 0;
  long long wireLength = 0, vias = 0;
  long long opens = 0, missing = 0, structural = 0;
  double maxOverflowTracks = 0;  // max over GCells of demand - capacity (tracks)
  long long overflowedGcells = 0; // GCells with demand > capacity
  // Per layer (diagnostic, not part of the score): the weighted overflow
  // cost, the part of it on GCells over capacity, the part a zero-via-demand
  // solution would pay (wire demand only), and the floor (no demand at all).
  struct LayerDetail {
    double loss = 0, lossOver = 0, lossWireOnly = 0, floor = 0;
    long long over = 0;
  };
  std::vector<LayerDetail> layers;
  std::vector<std::string> problems; // first few structural messages
  bool legal() const { return opens == 0 && missing == 0 && structural == 0; }
};

class Scorer {
public:
  explicit Scorer(const Resources &r) : r_(r) {
    size_t n = static_cast<size_t>(r.nLayers) * r.xSize * r.ySize;
    flag_.assign(n, -1);
    wire_.assign(n, 0);
    viaDemand_.assign(n, 0);
  }

  Report score(const std::vector<Net> &nets, std::istream &sol) {
    Report rep;
    std::unordered_map<std::string, int> byName;
    for (size_t i = 0; i < nets.size(); ++i)
      byName[nets[i].name] = static_cast<int>(i);
    std::vector<char> seen(nets.size(), 0);
    std::vector<long long> viasPerLayer(r_.nLayers, 0);
    auto problem = [&](const std::string &m) {
      ++rep.structural;
      if (rep.problems.size() < 20)
        rep.problems.push_back(m);
    };
    std::string line;
    int cur = -1;       // net index, -1 outside a net
    bool inBody = false, skipping = false;
    std::vector<std::array<int, 3>> viaLoc; // x, y, z (lower layer)
    bool bad = false;
    while (std::getline(sol, line)) {
      line = trimEnd(line);
      if (line.empty())
        continue;
      if (cur < 0 && !skipping) {
        auto it = byName.find(line);
        if (it == byName.end()) {
          problem("unknown net '" + line + "'");
          skipping = true;
          inBody = false;
          continue;
        }
        cur = it->second;
        if (seen[cur])
          problem("net " + line + " appears twice");
        seen[cur] = 1;
        inBody = false;
        bad = false;
        viaLoc.clear();
        continue;
      }
      if (line[0] == '(') {
        inBody = true;
        continue;
      }
      if (line[0] == ')') {
        if (!skipping) {
          finishNet(cur, viaLoc);
          if (bad || !connected(nets[cur], cur, static_cast<int>(nets.size())))
            ++rep.opens;
        }
        cur = -1;
        skipping = false;
        inBody = false;
        continue;
      }
      if (skipping)
        continue;
      if (!inBody) {
        problem("net " + nets[cur].name + ": segment before '('");
        continue;
      }
      std::istringstream ss(line);
      int xl, yl, zl, xh, yh, zh;
      if (!(ss >> xl >> yl >> zl >> xh >> yh >> zh)) {
        problem("net " + nets[cur].name + ": unreadable segment '" + line + "'");
        bad = true;
        continue;
      }
      auto inGrid = [&](int x, int y, int z) {
        return x >= 0 && x < r_.xSize && y >= 0 && y < r_.ySize && z >= 0 && z < r_.nLayers;
      };
      if (!inGrid(xl, yl, zl) || !inGrid(xh, yh, zh)) {
        problem("net " + nets[cur].name + ": segment outside the grid '" + line + "'");
        bad = true;
        continue;
      }
      if (zl != zh) {
        if (xl != xh || yl != yh || zl > zh) {
          problem("net " + nets[cur].name + ": malformed via '" + line + "'");
          bad = true;
          continue;
        }
        for (int z = zl; z < zh; ++z) {
          ++viasPerLayer[z];
          viaLoc.push_back({xl, yl, z});
        }
        continue;
      }
      if (zl == 0) {
        problem("net " + nets[cur].name + ": wire on layer 0 '" + line + "'");
        bad = true;
        continue;
      }
      if (r_.layers[zl].dir == 0) {
        if (!(xh > xl && yh == yl)) {
          problem("net " + nets[cur].name + ": not a left-to-right wire on horizontal layer " +
                  std::to_string(zl) + " '" + line + "'");
          bad = true;
          continue;
        }
        for (int x = xl; x < xh; ++x) {
          flag_[idx(zl, x, yl)] = cur;
          ++wire_[idx(zl, x, yl)];
        }
        flag_[idx(zl, xh, yl)] = cur;
      } else {
        if (!(yh > yl && xh == xl)) {
          problem("net " + nets[cur].name + ": not a bottom-to-top wire on vertical layer " +
                  std::to_string(zl) + " '" + line + "'");
          bad = true;
          continue;
        }
        for (int y = yl; y < yh; ++y) {
          flag_[idx(zl, xl, y)] = cur;
          ++wire_[idx(zl, xl, y)];
        }
        flag_[idx(zl, xl, yh)] = cur;
      }
    }
    if (cur >= 0 || skipping)
      problem("solution ends inside a net");
    for (size_t i = 0; i < nets.size(); ++i)
      if (!seen[i])
        ++rep.missing;

    // Costs, in the evaluator's order (z, then x, then y).
    for (int z = 0; z < r_.nLayers; ++z) {
      rep.vias += viasPerLayer[z];
      if (z == 0) {
        rep.viaCost += static_cast<double>(viasPerLayer[z]) * r_.unitVia;
        continue;
      }
      unsigned long long wl = 0;
      double layerLoss = 0;
      Report::LayerDetail det;
      auto loss = [](double demand, double c) {
        if (c > 0.001)
          return std::exp(0.5 * ((demand - 2.0 * c) / 2.0));
        if (c >= 0 && demand > 0)
          return std::exp(0.5 * (1.5 * demand));
        return 0.0;
      };
      for (int x = 0; x < r_.xSize; ++x)
        for (int y = 0; y < r_.ySize; ++y) {
          size_t i = idx(z, x, y);
          double demand = 2.0 * wire_[i] + viaDemand_[i]; // half tracks
          double c = r_.capacity(z, x, y);
          double l = loss(demand, c);
          layerLoss += l;
          det.lossWireOnly += loss(2.0 * wire_[i], c);
          det.floor += loss(0, c);
          double over = demand / 2.0 - c;
          if (over > 1e-9) {
            ++rep.overflowedGcells;
            ++det.over;
            det.lossOver += l;
            rep.maxOverflowTracks = std::max(rep.maxOverflowTracks, over);
          }
          if (r_.layers[z].dir == 0 && x + 1 < r_.xSize)
            wl += static_cast<unsigned long long>(wire_[i]) * r_.xPitch[x];
          else if (r_.layers[z].dir == 1 && y + 1 < r_.ySize)
            wl += static_cast<unsigned long long>(wire_[i]) * r_.yPitch[y];
        }
      rep.wireLength += static_cast<long long>(wl);
      rep.overflowCost += layerLoss * r_.overflowCost[z];
      det.loss = layerLoss * r_.overflowCost[z];
      det.lossOver *= r_.overflowCost[z];
      det.lossWireOnly *= r_.overflowCost[z];
      det.floor *= r_.overflowCost[z];
      rep.layers.resize(z + 1);
      rep.layers[z] = det;
      rep.viaCost += static_cast<double>(viasPerLayer[z]) * r_.unitVia;
      rep.wlCost += static_cast<double>(wl) * r_.unitLength;
    }
    rep.total = rep.overflowCost + rep.viaCost + rep.wlCost;
    return rep;
  }

private:
  const Resources &r_;
  std::vector<int> flag_, wire_, viaDemand_;

  size_t idx(int z, int x, int y) const {
    return (static_cast<size_t>(z) * r_.xSize + x) * r_.ySize + y;
  }

  // The evaluator's update_stacked_via_counter: a via at (x, y) from layer z
  // adds demand on layer z unless the net already covers (z, x, y); then
  // every via's two ends are marked as covered.
  void finishNet(int net, const std::vector<std::array<int, 3>> &viaLoc) {
    for (const auto &p : viaLoc) {
      int x = p[0], y = p[1], z = p[2];
      if (flag_[idx(z, x, y)] == net)
        continue;
      flag_[idx(z, x, y)] = net;
      if (z == 0)
        continue; // layer 0 is not routable; its demand is never scored
      if (r_.layers[z].dir == 0) {
        if (x > 0 && x < r_.xSize - 1) {
          ++viaDemand_[idx(z, x - 1, y)];
          ++viaDemand_[idx(z, x, y)];
        } else if (x > 0) {
          viaDemand_[idx(z, x - 1, y)] += 2;
        } else if (x < r_.xSize - 1) {
          viaDemand_[idx(z, x, y)] += 2;
        }
      } else {
        if (y > 0 && y < r_.ySize - 1) {
          ++viaDemand_[idx(z, x, y - 1)];
          ++viaDemand_[idx(z, x, y)];
        } else if (y > 0) {
          viaDemand_[idx(z, x, y - 1)] += 2;
        } else if (y < r_.ySize - 1) {
          viaDemand_[idx(z, x, y)] += 2;
        }
      }
    }
    for (const auto &p : viaLoc) {
      flag_[idx(p[2], p[0], p[1])] = net;
      flag_[idx(p[2] + 1, p[0], p[1])] = net;
    }
  }

  // Flood fill over this net's covered GCells from pin 0's access points;
  // every other pin must have a reached access point. Marks with net +
  // nNets as the evaluator does, so the next net's flags are unaffected.
  bool connected(const Net &n, int net, int nNets) {
    if (n.pins.empty())
      return true;
    int traced = net + nNets;
    std::vector<std::array<int, 3>> stack;
    auto visit = [&](int x, int y, int z) {
      size_t i = idx(z, x, y);
      if (flag_[i] == net) {
        flag_[i] = traced;
        stack.push_back({x, y, z});
      }
    };
    for (const Access &a : n.pins[0])
      if (a.x >= 0 && a.x < r_.xSize && a.y >= 0 && a.y < r_.ySize && a.z >= 0 && a.z < r_.nLayers)
        visit(a.x, a.y, a.z);
    while (!stack.empty()) {
      auto [x, y, z] = stack.back();
      stack.pop_back();
      if (z > 0 && r_.layers[z].dir == 0) {
        if (x > 0)
          visit(x - 1, y, z);
        if (x < r_.xSize - 1)
          visit(x + 1, y, z);
      } else if (z > 0) {
        if (y > 0)
          visit(x, y - 1, z);
        if (y < r_.ySize - 1)
          visit(x, y + 1, z);
      }
      if (z > 0)
        visit(x, y, z - 1);
      if (z < r_.nLayers - 1)
        visit(x, y, z + 1);
    }
    for (size_t p = 1; p < n.pins.size(); ++p) {
      bool ok = false;
      for (const Access &a : n.pins[p])
        if (a.x >= 0 && a.x < r_.xSize && a.y >= 0 && a.y < r_.ySize && a.z >= 0 && a.z < r_.nLayers &&
            flag_[idx(a.z, a.x, a.y)] == traced) {
          ok = true;
          break;
        }
      if (!ok)
        return false;
    }
    return true;
  }
};

void print(const Report &rep) {
  std::printf("wirelength %lld (cost %.4f)\n", rep.wireLength, rep.wlCost);
  std::printf("vias %lld (cost %.4f)\n", rep.vias, rep.viaCost);
  std::printf("overflow cost %.4f (%lld GCells over capacity, max %.2f tracks)\n", rep.overflowCost,
              rep.overflowedGcells, rep.maxOverflowTracks);
  for (size_t z = 1; z < rep.layers.size(); ++z) {
    const Report::LayerDetail &d = rep.layers[z];
    std::printf("  layer %zu overflow cost %.1f: %lld GCells over (%.1f of it), wire demand alone %.1f, floor %.1f\n",
                z, d.loss, d.over, d.lossOver, d.lossWireOnly, d.floor);
  }
  std::printf("total cost %.4f\n", rep.total);
  std::printf("open nets %lld, missing nets %lld, structural problems %lld\n", rep.opens, rep.missing,
              rep.structural);
  for (const std::string &p : rep.problems)
    std::printf("  %s\n", p.c_str());
  std::printf("%s\n", rep.legal() ? "LEGAL" : "ILLEGAL");
}

} // namespace g24

// ---------------------------------------------------------------------------
// Self test: a hand-computed fixture, then broken solutions that must each
// be caught.
// ---------------------------------------------------------------------------
namespace selftest {

using namespace g24;
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}

// 3 layers (M1 unroutable, M2 horizontal, M3 vertical), 4 x 3 GCells,
// pitches x 5 6 7, y 8 9; unit length cost 1, via cost 4, overflow costs
// 10 20 30. M2 capacities: row 0 all 1, row 1 all 2, row 2 "0 1 1 1"; M3
// all 1.
const char *kCap = "3 4 3\n1 4 10 20 30\n5 6 7\n8 9\n"
                   "M1 0 0\n0 0 0 0\n0 0 0 0\n0 0 0 0\n"
                   "M2 0 1\n1 1 1 1\n2 2 2 2\n0 1 1 1\n"
                   "M3 1 1\n1 1 1 1\n1 1 1 1\n1 1 1 1\n";
// netA: pins at M1 (0,0) and M1 (3,0) or (3,1). netB: M1 (1,1) and M1 (1,2).
const char *kNet = "netA\n(\n[(0, 0, 0)]\n[(0, 3, 0), (0, 3, 1)]\n)\n"
                   "netB\n(\n[(0, 1, 1)]\n[(0, 1, 2)]\n)\n";
// netA: up to M2 at (0,0), along row 0 to x 3, down to M1. netB: up to M3
// at (1,1) (two via layers), up column 1 to y 2, down to M1.
const char *kSol = "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 0 3 0 1\n)\n"
                   "netB\n(\n1 1 0 1 1 2\n1 1 2 1 2 2\n1 2 0 1 2 2\n)\n";

Report run(const char *cap, const char *net, const std::string &sol) {
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream c(cap), n(net), s(sol);
  if (!readResources(c, r, err) || !readNets(n, nets, err)) {
    std::cout << "  fixture parse error: " << err << "\n";
    ++failures;
    return Report{};
  }
  Scorer sc(r);
  return sc.score(nets, s);
}

void testHandComputedFixture() {
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream c(kCap), n(kNet);
  check(readResources(c, r, err) && r.nLayers == 3 && r.xSize == 4 && r.ySize == 3 && r.unitVia == 4 &&
            r.overflowCost[2] == 30 && r.xPitch[2] == 7 && r.yPitch[1] == 9 && r.layers[2].dir == 1 &&
            r.capacity(1, 0, 2) == 0 && r.capacity(1, 3, 1) == 2,
        "resources parse exactly (header, costs, pitches, per-layer capacity rows)");
  check(readNets(n, nets, err) && nets.size() == 2 && nets[0].pins.size() == 2 && nets[0].pins[1].size() == 2 &&
            nets[0].pins[1][1].x == 3 && nets[0].pins[1][1].y == 1 && nets[0].pins[1][1].z == 0,
        "nets parse exactly (z, x, y order; a pin can list several access points)");
  Report rep = run(kCap, kNet, kSol);
  // Hand computation (see ISPD24_FORMAT.md): vias 2 (netA, layer 0) + 2 + 2
  // (netB, layers 0 and 1) = 6, cost 24. Wire: M2 row 0 x 0-2 = 5+6+7 = 18;
  // M3 column 1, y 1 = 9; cost 27. netB's layer-1 vias at (1,1) and (1,2)
  // are not covered by its own M2 wire, so each adds half a track to M2
  // GCells x 0 and x 1 of its row. Layer sums: M2 = 3 + 3e^-.5 + 2e^-.75 +
  // 2e^-1 + e^.75 + e^-.25 = 9.395884766646896 (the e^.75 is GCell (0,2):
  // capacity 0, demand half a track); M3 = 1 + 11e^-.5 = 7.671837256838968.
  // Overflow cost 20 * 9.3958... + 30 * 7.6718... = 418.0728130381069.
  check(rep.vias == 6 && std::fabs(rep.viaCost - 24) < 1e-12, "vias 6, cost 24 (" + std::to_string(rep.viaCost) + ")");
  check(rep.wireLength == 27 && std::fabs(rep.wlCost - 27) < 1e-12,
        "wirelength 27 from the pitches of the GCells crossed (" + std::to_string(rep.wireLength) + ")");
  check(std::fabs(rep.overflowCost - 418.0728130381069) < 1e-9,
        "overflow cost 418.0728130381069, hand computed (got " + std::to_string(rep.overflowCost) + ")");
  check(std::fabs(rep.total - 469.0728130381069) < 1e-9 && rep.legal(), "total 469.0728..., legal");
  check(rep.overflowedGcells == 1 && std::fabs(rep.maxOverflowTracks - 0.5) < 1e-12,
        "one GCell over capacity -- the zero-capacity one -- by half a track");
}

void testBrokenSolutionsAreCaught() {
  // Missing the via down to netA's second pin: an open.
  Report a = run(kCap, kNet, "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n)\nnetB\n(\n1 1 0 1 1 2\n1 1 2 1 2 2\n1 2 0 1 2 2\n)\n");
  check(a.opens == 1 && !a.legal(), "a net missing its last via is open");
  // The second pin's other access point, (3,1), reached instead: legal.
  Report b = run(kCap, kNet,
                 "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 1 3 0 2\n3 0 2 3 1 2\n3 1 0 3 1 2\n)\nnetB\n(\n1 1 0 1 1 "
                 "2\n1 1 2 1 2 2\n1 2 0 1 2 2\n)\n");
  check(b.opens == 0 && b.legal(), "reaching any one access point of a pin connects it");
  Report c = run(kCap, kNet, "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 0 3 0 1\n)\n");
  check(c.missing == 1 && !c.legal(), "a net left out of the solution is reported missing");
  Report d = run(kCap, kNet, std::string(kSol) + "netA\n(\n0 0 0 0 0 1\n)\n");
  check(d.structural >= 1 && !d.legal(), "a net routed twice is a structural problem");
  Report e = run(kCap, kNet, "netA\n(\n0 0 0 0 0 1\n0 0 1 3 1 1\n3 0 0 3 0 1\n)\nnetB\n(\n1 1 0 1 1 2\n1 1 2 1 2 "
                             "2\n1 2 0 1 2 2\n)\n");
  check(e.structural == 1 && e.opens == 1, "a diagonal wire is caught and its net counted open");
  Report f = run(kCap, kNet, "netA\n(\n0 0 0 0 0 1\n0 0 1 3 0 1\n3 0 0 3 0 1\n)\nnetB\n(\n1 1 0 1 1 1\n1 1 1 1 2 "
                             "1\n1 2 0 1 2 1\n)\n");
  check(f.structural == 1 && !f.legal(), "a vertical wire on the horizontal layer M2 is caught");
  Report g = run(kCap, kNet, "netA\n(\n0 0 0 3 0 0\n)\nnetB\n(\n1 1 0 1 1 2\n1 1 2 1 2 2\n1 2 0 1 2 2\n)\n");
  check(g.structural == 1 && !g.legal(), "a wire on layer 0 is caught");
  Report h = run(kCap, kNet, "netA\n(\n0 0 1 3 0 1\n9 0 0 9 0 1\n)\nnetB\n(\n1 1 0 1 1 2\n1 1 2 1 2 2\n1 2 0 1 2 "
                             "2\n)\n");
  check(h.structural >= 1 && !h.legal(), "a segment outside the grid is caught");
  Report i = run(kCap, kNet, "ghost\n(\n0 0 1 3 0 1\n)\n" + std::string(kSol));
  check(i.structural == 1 && i.missing == 0 && i.opens == 0, "an unknown net is reported and skipped");
  Report j = run(kCap, kNet, "netA\n(\n3 0 1 0 0 1\n)\nnetB\n(\n)\n");
  check(j.structural >= 1 && !j.legal(), "a right-to-left wire is caught (the format requires xl < xh)");
}

void testZeroCapacityAndViaEdgeRules() {
  // One net, a via 1 -> 2 at x = 0 (the grid edge) on horizontal M2 with no
  // M2 wire there: the evaluator puts a full track on GCell (0, y) of M2.
  const char *cap = "3 3 1\n1 1 1 1 1\n10 10\n\nM1 0 0\n0 0 0\nM2 0 1\n1 1 1\nM3 1 1\n0 0 0\n";
  const char *net = "nv\n(\n[(0, 0, 0)]\n[(0, 0, 0)]\n)\n";
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream c(cap), n(net);
  bool ok = readResources(c, r, err) && readNets(n, nets, err);
  check(ok && r.ySize == 1 && r.yPitch.empty(), "a one-row grid has no vertical pitches");
  Scorer s(r);
  std::istringstream sol("nv\n(\n0 0 0 0 0 2\n)\n");
  Report rep = s.score(nets, sol);
  // M2: GCell 0 demand 2 half-tracks = 1 track = capacity -> e^0; GCells 1, 2
  // e^-.5. M3: capacity 0 everywhere, demand 0 -> contributes nothing. Vias:
  // 2 layers crossed -> cost 2.
  double want = 1 * (1 + 2 * std::exp(-0.5));
  check(std::fabs(rep.overflowCost - want) < 1e-12 && rep.vias == 2 && rep.legal(),
        "via at the grid edge: a full track on the one GCell inside; zero-capacity idle GCells cost nothing");
  // A wire through zero capacity: M3 has capacity 0, a vertical wire is not
  // possible on a 1-row grid, so use M2 with capacity 0 instead.
  const char *cap0 = "2 3 1\n1 1 1 1\n10 10\n\nM1 0 0\n0 0 0\nM2 0 1\n0 0 0\n";
  const char *net0 = "nw\n(\n[(0, 0, 0)]\n[(0, 2, 0)]\n)\n";
  Resources r0;
  std::vector<Net> n0;
  std::istringstream c0(cap0), nn0(net0);
  ok = readResources(c0, r0, err) && readNets(nn0, n0, err);
  Scorer s0(r0);
  std::istringstream sol0("nw\n(\n0 0 0 0 0 1\n0 0 1 2 0 1\n2 0 0 2 0 1\n)\n");
  Report rep0 = s0.score(n0, sol0);
  // Wire on GCells 0, 1: demand 2 half-tracks each, capacity 0 -> e^(0.75*2)
  // each. GCell 2: vias at x = 0 and x = 2 are covered by the wire (flags at
  // 0..2), so no via demand; demand 0, capacity 0 -> nothing.
  check(ok && std::fabs(rep0.overflowCost - 2 * std::exp(1.5)) < 1e-9 && rep0.legal(),
        "wire through zero capacity: e^(0.75 * demand) per GCell; vias from layer 0 add no scored demand");
  check(rep0.wireLength == 20, "wirelength 10 + 10");
}

void testParserRejectsBadInput() {
  Resources r;
  std::vector<Net> nets;
  std::string err;
  std::istringstream a("1 4 3\n1 1 1\n");
  check(!readResources(a, r, err), "a one-layer .cap (no routing layer) is rejected");
  std::istringstream b("2 2 2\n1 1 1 1\n5\n5\nM1 0 0\n0 0\n0 0\nM2 2 1\n1 1\n1 1\n");
  check(!readResources(b, r, err), "a routing layer with direction 2 is rejected");
  std::istringstream c("2 2 2\n1 1 1 1\n5\n5\nM1 0 0\n0 0\n0 0\nM2 0 1\n1 1\n");
  check(!readResources(c, r, err), "a short capacity table is rejected");
  std::istringstream d("[(0, 1, 1)]\n");
  check(!readNets(d, nets, err), "a pin before any net name is rejected");
  std::istringstream e("n\n(\n[(0, 1, 1)]\n)\n");
  check(!readNets(e, nets, err), "a one-character name line is not a net (the evaluator's length > 1 rule)");
}

int run() {
  std::cout << "g24_verify self test\n";
  testHandComputedFixture();
  testBrokenSolutionsAreCaught();
  testZeroCapacityAndViaEdgeRules();
  testParserRejectsBadInput();
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}

} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <design.cap> <design.net> <solution>\n       " << argv[0]
              << " --selftest\n";
    return 2;
  }
  g24::Resources r;
  std::vector<g24::Net> nets;
  std::string err;
  std::ifstream cf(argv[1]), nf(argv[2]), sf(argv[3]);
  if (!cf || !nf || !sf) {
    std::cerr << "cannot open an input file\n";
    return 2;
  }
  if (!g24::readResources(cf, r, err) || !g24::readNets(nf, nets, err)) {
    std::cerr << "input error: " << err << "\n";
    return 2;
  }
  std::printf("%zu nets, grid %d x %d x %d\n", nets.size(), r.xSize, r.ySize, r.nLayers);
  g24::Scorer sc(r);
  g24::Report rep = sc.score(nets, sf);
  g24::print(rep);
  return rep.legal() ? 0 : 1;
}
