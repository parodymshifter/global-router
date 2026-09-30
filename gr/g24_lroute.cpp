// g24_lroute.cpp -- throwaway L-router for the ISPD 2024 format (roadmap
// step 3). Exists only to produce legal solutions so gr/g24_verify.cpp can
// be cross-checked against the contest evaluator, as gr/lroute.cpp did for
// ISPD 2008 (CLAUDE.md, build order 2). No congestion awareness.
//
//   g24_lroute <design.cap> <design.net> <solution>
//   g24_lroute --selftest
//
// Per net: each pin's first access point; a Manhattan minimum spanning tree
// over those (Prim); each tree edge an L -- horizontal on the lowest
// horizontal routing layer, then vertical on the lowest vertical one -- with
// a via stack at each end and at the corner joining whatever layers meet
// there. Zero-length wires and vias are never written (the evaluator reads
// a same-layer segment as a wire and asserts it has length).

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace g24l {

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

// Only what routing needs from the .cap: sizes and each layer's direction.
bool readLayers(std::istream &in, int &nLayers, int &xSize, int &ySize, std::vector<int> &dir, std::string &err) {
  if (!(in >> nLayers >> xSize >> ySize) || nLayers < 3) {
    err = "bad .cap header (need at least layer 0 plus one layer per direction)";
    return false;
  }
  double skip;
  for (int i = 0; i < 2 + nLayers; ++i)
    in >> skip;
  for (int i = 0; i < xSize - 1 + ySize - 1; ++i)
    in >> skip;
  dir.assign(nLayers, 0);
  for (int z = 0; z < nLayers; ++z) {
    std::string name;
    double minLen;
    if (!(in >> name >> dir[z] >> minLen)) {
      err = "bad layer line " + std::to_string(z);
      return false;
    }
    for (long long i = 0; i < static_cast<long long>(xSize) * ySize; ++i)
      in >> skip;
  }
  if (!in) {
    err = "truncated .cap";
    return false;
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
        err = "pin before net name";
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
      if (!pin.empty())
        nets.back().pins.push_back(pin);
    }
  }
  return true;
}

void addVia(std::vector<Seg> &out, int x, int y, int za, int zb) {
  if (za == zb)
    return;
  out.push_back({x, y, std::min(za, zb), x, y, std::max(za, zb)});
}

// The segments of one net: MST over each pin's first access point, each
// edge an L (hLayer then vLayer).
std::vector<Seg> routeNet(const Net &n, int hLayer, int vLayer) {
  std::vector<Seg> out;
  std::vector<Access> p;
  for (const auto &pin : n.pins)
    p.push_back(pin[0]);
  size_t k = p.size();
  if (k < 2)
    return out;
  std::vector<char> in(k, 0);
  std::vector<int> best(k, INT_MAX), parent(k, -1);
  best[0] = 0;
  for (size_t it = 0; it < k; ++it) {
    size_t u = k;
    for (size_t i = 0; i < k; ++i)
      if (!in[i] && (u == k || best[i] < best[u]))
        u = i;
    in[u] = 1;
    if (parent[u] >= 0) {
      const Access &a = p[parent[u]], &b = p[u];
      bool dx = a.x != b.x, dy = a.y != b.y;
      if (!dx && !dy) {
        // Same GCell. The evaluator only reaches access points the net
        // covers, so even two pins on one access point need a via there.
        if (a.z == b.z)
          addVia(out, a.x, a.y, a.z, a.z > 0 ? a.z - 1 : a.z + 1);
        else
          addVia(out, a.x, a.y, a.z, b.z);
      } else if (dx && !dy) {
        addVia(out, a.x, a.y, a.z, hLayer);
        out.push_back({std::min(a.x, b.x), a.y, hLayer, std::max(a.x, b.x), a.y, hLayer});
        addVia(out, b.x, b.y, b.z, hLayer);
      } else if (!dx && dy) {
        addVia(out, a.x, a.y, a.z, vLayer);
        out.push_back({a.x, std::min(a.y, b.y), vLayer, a.x, std::max(a.y, b.y), vLayer});
        addVia(out, b.x, b.y, b.z, vLayer);
      } else {
        addVia(out, a.x, a.y, a.z, hLayer);
        out.push_back({std::min(a.x, b.x), a.y, hLayer, std::max(a.x, b.x), a.y, hLayer});
        addVia(out, b.x, a.y, hLayer, vLayer); // corner
        out.push_back({b.x, std::min(a.y, b.y), vLayer, b.x, std::max(a.y, b.y), vLayer});
        addVia(out, b.x, b.y, b.z, vLayer);
      }
    }
    for (size_t i = 0; i < k; ++i)
      if (!in[i]) {
        int d = std::abs(p[i].x - p[u].x) + std::abs(p[i].y - p[u].y);
        if (d < best[i]) {
          best[i] = d;
          parent[i] = static_cast<int>(u);
        }
      }
  }
  return out;
}

void write(std::ostream &os, const Net &n, const std::vector<Seg> &segs) {
  os << n.name << "\n(\n";
  for (const Seg &s : segs)
    os << s.xl << ' ' << s.yl << ' ' << s.zl << ' ' << s.xh << ' ' << s.yh << ' ' << s.zh << '\n';
  os << ")\n";
}

} // namespace g24l

namespace selftest {
static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}

int run() {
  using namespace g24l;
  std::cout << "g24_lroute self test\n";
  // Three pins: (0,0) on layer 0, (3,2) on layer 0, (3,0) on layer 2.
  Net n{"net3", {{{0, 0, 0}}, {{3, 2, 0}}, {{3, 0, 2}}}};
  std::vector<Seg> s = routeNet(n, 2, 1);
  bool shapesOk = true, noZero = true;
  for (const Seg &g : s) {
    bool via = g.zl != g.zh, wire = !via;
    if (via && (g.xl != g.xh || g.yl != g.yh || g.zl > g.zh))
      shapesOk = false;
    if (wire && !((g.zl == 2 && g.yl == g.yh && g.xl < g.xh) || (g.zl == 1 && g.xl == g.xh && g.yl < g.yh)))
      shapesOk = false;
    if (wire && g.xl == g.xh && g.yl == g.yh)
      noZero = false;
  }
  check(shapesOk, "every via vertical with zl < zh; every wire on its layer's direction, low end first");
  check(noZero, "no zero-length wire is written");
  // MST: (0,0)-(3,0) is 3, (3,0)-(3,2) is 2: a straight run on layer 2 then
  // one on layer 1 -- two wires.
  int wires = 0;
  for (const Seg &g : s)
    wires += g.zl == g.zh;
  check(wires == 2, "the three-pin net takes the two straight MST edges, no L corner (" + std::to_string(wires) +
                        " wires)");
  Net one{"single", {{{5, 5, 0}}}};
  check(routeNet(one, 2, 1).empty(), "a one-pin net writes no segments");
  Net same{"stack", {{{4, 4, 0}}, {{4, 4, 4}}}};
  std::vector<Seg> st = routeNet(same, 2, 1);
  check(st.size() == 1 && st[0].zl == 0 && st[0].zh == 4, "two pins in one GCell on layers 0 and 4: one via 0-4");
  Net twin{"twin", {{{7, 7, 0}}, {{7, 7, 0}}}};
  std::vector<Seg> tw = routeNet(twin, 2, 1);
  check(tw.size() == 1 && tw[0].zl == 0 && tw[0].zh == 1 && tw[0].xl == 7,
        "two pins on the same access point: a via 0-1 there (the evaluator only reaches covered access points)");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}
} // namespace selftest

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest")
    return selftest::run();
  if (argc != 4) {
    std::cerr << "usage: " << argv[0] << " <design.cap> <design.net> <solution>\n";
    return 2;
  }
  std::ifstream cf(argv[1]), nf(argv[2]);
  int nLayers, xSize, ySize;
  std::vector<int> dir;
  std::vector<g24l::Net> nets;
  std::string err;
  if (!cf || !nf || !g24l::readLayers(cf, nLayers, xSize, ySize, dir, err) || !g24l::readNets(nf, nets, err)) {
    std::cerr << "input error: " << err << "\n";
    return 2;
  }
  int hLayer = -1, vLayer = -1;
  for (int z = 1; z < nLayers; ++z) {
    if (dir[z] == 0 && hLayer < 0)
      hLayer = z;
    if (dir[z] == 1 && vLayer < 0)
      vLayer = z;
  }
  if (hLayer < 0 || vLayer < 0) {
    std::cerr << "need a horizontal and a vertical routing layer\n";
    return 2;
  }
  // G24_LROUTE_SPREAD=1: net i uses the (i mod count)-th horizontal and
  // vertical routing layers instead of the lowest pair, so every layer
  // carries wire -- a cross-check solution where every layer's overflow
  // term shows in the total, not only one saturated pair's.
  std::vector<int> hs, vs;
  for (int z = 1; z < nLayers; ++z)
    (dir[z] == 0 ? hs : vs).push_back(z);
  const char *spread = std::getenv("G24_LROUTE_SPREAD");
  bool spreadOn = spread && std::string(spread) == "1";
  std::ofstream os(argv[3]);
  for (size_t i = 0; i < nets.size(); ++i) {
    int h = spreadOn ? hs[i % hs.size()] : hLayer, v = spreadOn ? vs[i % vs.size()] : vLayer;
    g24l::write(os, nets[i], g24l::routeNet(nets[i], h, v));
  }
  if (spreadOn)
    std::cerr << "routed " << nets.size() << " nets over " << hs.size() << " H and " << vs.size() << " V layers\n";
  else
    std::cerr << "routed " << nets.size() << " nets on layers " << hLayer << " (H) and " << vLayer << " (V)\n";
  return 0;
}
