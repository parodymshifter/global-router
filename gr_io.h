// gr_io.h
//
// Shared reader for the ISPD global routing benchmark format (.gr): the grid,
// capacity model, and netlist. No verifier or router logic lives here — just
// the format. gr_verify and lroute both include this so the parser has one
// definition instead of two that quietly drift apart.
//
// Design note: RoutingGrid owns the capacity model and edge indexing and knows
// nothing about file formats. That separation is deliberate. Swapping the .gr
// reader for a LEF/DEF front end later should not touch anything below it.

#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace gr_io {

[[noreturn]] inline void fail(const std::string &msg) {
  std::cerr << "error: " << msg << "\n";
  std::exit(1);
}

// Whitespace tokenizer over a whole file. The .gr header is line oriented in
// practice but not reliably so across benchmark generations, so we treat it as
// a token stream and consume keywords explicitly.
class Tokens {
public:
  explicit Tokens(const std::string &text) {
    std::istringstream in(text);
    std::string t;
    while (in >> t)
      toks_.push_back(t);
  }

  bool done() const { return pos_ >= toks_.size(); }

  const std::string &peek() const {
    if (done())
      fail("unexpected end of file");
    return toks_[pos_];
  }

  std::string next() {
    if (done())
      fail("unexpected end of file");
    return toks_[pos_++];
  }

  long long nextInt() {
    std::string t = next();
    try {
      return std::stoll(t);
    } catch (...) {
      fail("expected an integer, found '" + t + "'");
    }
  }

  // Consume an expected keyword. Case insensitive, tolerant of absence so the
  // reader survives minor header variations between benchmark releases.
  bool tryKeyword(const std::string &kw) {
    if (done())
      return false;
    std::string t = toks_[pos_];
    std::string l;
    for (char c : t)
      l.push_back(static_cast<char>(std::tolower(c)));
    if (l == kw) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expectKeyword(const std::string &kw) {
    if (!tryKeyword(kw))
      fail("expected keyword '" + kw + "', found '" + peek() + "'");
  }

private:
  std::vector<std::string> toks_;
  size_t pos_ = 0;
};

inline std::string readWholeFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    fail("cannot open " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// ---------------------------------------------------------------------------
// RoutingGrid: geometry, edge indexing, capacity.
// ---------------------------------------------------------------------------

// Tile coordinates are zero based: x in [0,nx), y in [0,ny), layer in
// [0,nLayers). Benchmark files use one based layers; the reader converts.
//
// Two edge families:
//   H edge (x,y,l) joins tile (x,y,l) to (x+1,y,l), valid for x in [0,nx-1)
//   V edge (x,y,l) joins tile (x,y,l) to (x,y+1,l), valid for y in [0,ny-1)
// Layer changes are vias. ISPD .gr places no capacity on vias, so they are
// counted but not checked. The via counter is kept separate so a via capacity
// model can be added without disturbing the wire accounting.

struct RoutingGrid {
  int nx = 0, ny = 0, nLayers = 0;

  // Origin and tile pitch, used only to map real coordinates to tiles.
  long long llx = 0, lly = 0, tileW = 1, tileH = 1;

  // Per layer nominal capacities, before adjustments.
  std::vector<int> hCapLayer, vCapLayer;
  std::vector<int> minWidth, minSpacing, viaSpacing;

  // Dense per edge capacity.
  std::vector<int> hcap, vcap;

  size_t hCount() const {
    return static_cast<size_t>(nLayers) * ny * std::max(nx - 1, 0);
  }
  size_t vCount() const {
    return static_cast<size_t>(nLayers) * std::max(ny - 1, 0) * nx;
  }

  size_t hIndex(int x, int y, int l) const {
    return (static_cast<size_t>(l) * ny + y) * static_cast<size_t>(nx - 1) + x;
  }
  size_t vIndex(int x, int y, int l) const {
    return (static_cast<size_t>(l) * (ny - 1) + y) * static_cast<size_t>(nx) + x;
  }

  bool inBounds(int x, int y, int l) const {
    return x >= 0 && x < nx && y >= 0 && y < ny && l >= 0 && l < nLayers;
  }

  void allocate() {
    hcap.assign(hCount(), 0);
    vcap.assign(vCount(), 0);
    for (int l = 0; l < nLayers; ++l) {
      for (int y = 0; y < ny; ++y)
        for (int x = 0; x + 1 < nx; ++x)
          hcap[hIndex(x, y, l)] = hCapLayer[l];
      for (int y = 0; y + 1 < ny; ++y)
        for (int x = 0; x < nx; ++x)
          vcap[vIndex(x, y, l)] = vCapLayer[l];
    }
  }

  // Map a real coordinate to a tile column/row.
  int colOf(long long x) const {
    return static_cast<int>((x - llx) / tileW);
  }
  int rowOf(long long y) const {
    return static_cast<int>((y - lly) / tileH);
  }

  long long totalCapacity() const {
    long long t = 0;
    for (int c : hcap)
      t += c;
    for (int c : vcap)
      t += c;
    return t;
  }
};

// ---------------------------------------------------------------------------
// Netlist
// ---------------------------------------------------------------------------

struct Pin {
  int x = 0, y = 0, layer = 0; // tile coordinates, zero based layer
};

struct Net {
  std::string name;
  long long id = 0;
  int minWidth = 1;
  std::vector<Pin> pins;
};

struct Design {
  RoutingGrid grid;
  std::vector<Net> nets;
};

// ---------------------------------------------------------------------------
// .gr reader
// ---------------------------------------------------------------------------

// Format, as emitted by the ISPD global routing benchmarks:
//
//   grid <nx> <ny> <nLayers>
//   vertical capacity <c_1> ... <c_nLayers>
//   horizontal capacity <c_1> ... <c_nLayers>
//   minimum width <w_1> ... <w_nLayers>
//   minimum spacing <s_1> ... <s_nLayers>
//   via spacing <v_1> ... <v_nLayers>
//   <llx> <lly> <tileWidth> <tileHeight>
//   num net <numNets>
//   <netName> <netId> <numPins> <minWidth>
//     <x> <y> <layer>          (real coordinates, one based layer)
//     ...
//   <numAdjustments>
//   <c1> <r1> <l1> <c2> <r2> <l2> <reducedCapacity>
//
// Adjustment tile coordinates are grid indices in every file I have seen, but
// that is worth confirming against your own copy.

inline Design readGr(const std::string &path) {
  Tokens tk(readWholeFile(path));
  Design d;
  RoutingGrid &g = d.grid;

  tk.expectKeyword("grid");
  g.nx = static_cast<int>(tk.nextInt());
  g.ny = static_cast<int>(tk.nextInt());
  g.nLayers = static_cast<int>(tk.nextInt());
  if (g.nx <= 0 || g.ny <= 0 || g.nLayers <= 0)
    fail("grid dimensions must be positive");

  auto readVector = [&](const char *w1, const char *w2) {
    tk.expectKeyword(w1);
    if (w2)
      tk.expectKeyword(w2);
    std::vector<int> v(g.nLayers);
    for (int i = 0; i < g.nLayers; ++i)
      v[i] = static_cast<int>(tk.nextInt());
    return v;
  };

  g.vCapLayer = readVector("vertical", "capacity");
  g.hCapLayer = readVector("horizontal", "capacity");
  g.minWidth = readVector("minimum", "width");
  g.minSpacing = readVector("minimum", "spacing");
  g.viaSpacing = readVector("via", "spacing");

  g.llx = tk.nextInt();
  g.lly = tk.nextInt();
  g.tileW = tk.nextInt();
  g.tileH = tk.nextInt();
  if (g.tileW <= 0 || g.tileH <= 0)
    fail("tile dimensions must be positive");

  g.allocate();

  tk.expectKeyword("num");
  tk.expectKeyword("net");
  long long numNets = tk.nextInt();
  d.nets.reserve(static_cast<size_t>(numNets));

  for (long long i = 0; i < numNets; ++i) {
    Net n;
    n.name = tk.next();
    n.id = tk.nextInt();
    long long numPins = tk.nextInt();
    n.minWidth = static_cast<int>(tk.nextInt());
    if (n.minWidth <= 0)
      n.minWidth = 1;
    n.pins.reserve(static_cast<size_t>(numPins));
    for (long long p = 0; p < numPins; ++p) {
      long long px = tk.nextInt();
      long long py = tk.nextInt();
      long long pl = tk.nextInt();
      Pin pin;
      pin.x = g.colOf(px);
      pin.y = g.rowOf(py);
      pin.layer = static_cast<int>(pl) - 1; // file layers are one based
      if (!g.inBounds(pin.x, pin.y, pin.layer))
        fail("net " + n.name + " has a pin outside the grid");
      n.pins.push_back(pin);
    }
    d.nets.push_back(std::move(n));
  }

  // Capacity adjustments. Absent in some files, so treat as optional.
  if (!tk.done()) {
    long long numAdj = tk.nextInt();
    for (long long a = 0; a < numAdj; ++a) {
      int c1 = static_cast<int>(tk.nextInt());
      int r1 = static_cast<int>(tk.nextInt());
      int l1 = static_cast<int>(tk.nextInt()) - 1;
      int c2 = static_cast<int>(tk.nextInt());
      int r2 = static_cast<int>(tk.nextInt());
      int l2 = static_cast<int>(tk.nextInt()) - 1;
      int cap = static_cast<int>(tk.nextInt());

      if (l1 != l2)
        continue; // via capacity is not modeled
      if (!g.inBounds(c1, r1, l1) || !g.inBounds(c2, r2, l2))
        fail("capacity adjustment refers to a tile outside the grid; if your "
             "file uses real coordinates here rather than tile indices, "
             "convert with colOf/rowOf before this check");

      if (r1 == r2 && std::abs(c1 - c2) == 1) {
        g.hcap[g.hIndex(std::min(c1, c2), r1, l1)] = cap;
      } else if (c1 == c2 && std::abs(r1 - r2) == 1) {
        g.vcap[g.vIndex(c1, std::min(r1, r2), l1)] = cap;
      }
      // Non adjacent pairs are ignored rather than treated as an error: some
      // files include degenerate entries.
    }
  }

  return d;
}

} // namespace gr_io
