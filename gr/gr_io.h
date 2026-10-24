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
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gr_io {

[[noreturn]] inline void fail(const std::string &msg) {
  std::cerr << "error: " << msg << "\n";
  std::exit(1);
}

// An input file that breaks the format (HISTORY_COST_SWEEP.md Part 155): thrown by the readers with
// "<file>:<line>: <what is wrong>"; readGr turns it into the usual error exit, before any routing starts.
struct InputError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Whitespace tokenizer over a whole file, keeping each token's line. The .gr header is line oriented in
// practice but not reliably so across benchmark generations, so it is read as a token stream with keywords
// consumed explicitly; the net, pin and capacity-adjustment records are checked to sit one per line
// (Part 155), so a record with a missing or extra field is reported where it is instead of shifting every
// value after it.
class Tokens {
public:
  Tokens(const std::string &text, std::string name) : name_(std::move(name)) {
    int line = 1;
    size_t i = 0, n = text.size();
    while (i < n) {
      char c = text[i];
      if (c == '\n') {
        ++line;
        ++i;
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
        ++i;
      } else {
        size_t j = i;
        while (j < n && text[j] != '\n' && text[j] != ' ' && text[j] != '\t' && text[j] != '\r' &&
               text[j] != '\v' && text[j] != '\f')
          ++j;
        toks_.emplace_back(text, i, j - i);
        lines_.push_back(line);
        i = j;
      }
    }
    lastLine_ = line;
  }

  bool done() const { return pos_ >= toks_.size(); }
  // The line of the next token (the file's last line at the end).
  int line() const { return done() ? lastLine_ : lines_[pos_]; }

  [[noreturn]] void error(int line, const std::string &msg) const {
    throw InputError(name_ + ":" + std::to_string(line) + ": " + msg);
  }

  const std::string &peek() const {
    if (done())
      error(lastLine_, "unexpected end of file");
    return toks_[pos_];
  }

  std::string next() {
    if (done())
      error(lastLine_, "unexpected end of file");
    return toks_[pos_++];
  }

  // A whole decimal integer (optional sign, digits, nothing else) in the range of long long.
  long long nextInt() {
    const int ln = line();
    std::string t = next();
    size_t k = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    bool digits = k < t.size();
    for (size_t j = k; j < t.size(); ++j)
      digits = digits && t[j] >= '0' && t[j] <= '9';
    if (!digits)
      error(ln, "expected an integer, found '" + t + "'");
    try {
      return std::stoll(t);
    } catch (...) {
      error(ln, "integer '" + t + "' is out of range");
    }
  }
  // An integer in [lo, hi], `what` naming it in the error.
  int nextIntIn(long long lo, long long hi, const std::string &what) {
    const int ln = line();
    long long v = nextInt();
    if (v < lo || v > hi)
      error(ln, what + " " + std::to_string(v) + " is out of range (" + std::to_string(lo) + " to " +
                    std::to_string(hi) + ")");
    return static_cast<int>(v);
  }

  // Consume an expected keyword, case insensitive. False (nothing consumed) if the next token is another word.
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
      error(line(), "expected keyword '" + kw + "', found '" + peek() + "'");
  }

  // The next record must be exactly `fields` tokens forming a line of their own (Part 155); `what` names the
  // record and its fields for the error. Returns the record's line.
  int expectRecord(int fields, const std::string &what) {
    if (done())
      error(lastLine_, "unexpected end of file: expected " + what);
    const int ln = lines_[pos_];
    if (pos_ > 0 && lines_[pos_ - 1] == ln)
      error(ln, "expected " + what + " to start a new line, found '" + toks_[pos_] + "' after other values");
    size_t k = pos_;
    while (k < toks_.size() && lines_[k] == ln)
      ++k;
    if (static_cast<int>(k - pos_) != fields)
      error(ln, "expected " + what + " (" + std::to_string(fields) + " values on the line), found " +
                    std::to_string(k - pos_));
    return ln;
  }

private:
  std::string name_;
  std::vector<std::string> toks_;
  std::vector<int> lines_;
  size_t pos_ = 0;
  int lastLine_ = 1;
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

  // Map a real coordinate to a tile column/row: floor((x - llx) / tileW), as the format defines it (Part 155:
  // C++ division truncates toward zero, which put a coordinate just below the origin in tile 0 instead of
  // outside the grid).
  static long long floorDiv(long long a, long long b) {
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
  }
  int colOf(long long x) const {
    long long c = floorDiv(x - llx, tileW);
    return static_cast<int>(std::clamp<long long>(c, -1, std::numeric_limits<int>::max()));
  }
  int rowOf(long long y) const {
    long long r = floorDiv(y - lly, tileH);
    return static_cast<int>(std::clamp<long long>(r, -1, std::numeric_limits<int>::max()));
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

// The reader (Part 155: strict). Everything the format does not allow stops the run, with the file and line;
// nothing out of spec is silently repaired or skipped:
//   header: grid dimensions >= 1; capacities, minimum spacings and via spacings >= 0; minimum widths >= 1;
//     tile width and height >= 1; net count >= 0; every count, coordinate and capacity fits in an int;
//   nets: each header "name id numPins minWidth" on a line of its own, each pin "x y layer" too, so a record
//     with a missing or extra value is caught where it is; names and ids unique; numPins >= 1; minWidth >= 1;
//     every pin inside the grid, its tile floor((x - llx) / tileW) as the format defines it, its layer in
//     1..nLayers;
//   capacity adjustments (optional as a block): the count >= 0 on a line of its own, each adjustment's seven
//     values on a line of their own, both tiles inside the grid, on one layer (vias have no capacity in this
//     format), neighbours; the new capacity in 0..the layer's default for that direction (the format reduces
//     capacity); an edge adjusted twice to different values is an error (twice to the same value, a warning);
//   nothing may follow the last adjustment (or the last net, without them).
// A warning (printed, the run continues) for what the format allows but suggests a mistake: an identical
// adjustment repeated. `warnings`, if given, collects warnings instead of printing them. (A layer with capacity
// in both directions is allowed and not reported: the benchmarks have none, but many self-test grids do.)
inline Design readGrText(const std::string &text, const std::string &name,
                         std::vector<std::string> *warnings = nullptr) {
  Tokens tk(text, name);
  Design d;
  RoutingGrid &g = d.grid;
  auto warn = [&](int line, const std::string &msg) {
    std::string w = name + ":" + std::to_string(line) + ": " + msg;
    if (warnings)
      warnings->push_back(w);
    else
      std::cerr << "warning: " << w << "\n";
  };
  const long long kIntMax = std::numeric_limits<int>::max();

  tk.expectKeyword("grid");
  g.nx = tk.nextIntIn(1, kIntMax, "grid width");
  g.ny = tk.nextIntIn(1, kIntMax, "grid height");
  g.nLayers = tk.nextIntIn(1, kIntMax, "layer count");

  auto readVector = [&](const char *w1, const char *w2, long long lo, const std::string &what) {
    tk.expectKeyword(w1);
    if (w2)
      tk.expectKeyword(w2);
    std::vector<int> v(g.nLayers);
    for (int i = 0; i < g.nLayers; ++i)
      v[i] = tk.nextIntIn(lo, kIntMax, what + " of layer " + std::to_string(i + 1));
    return v;
  };

  g.vCapLayer = readVector("vertical", "capacity", 0, "vertical capacity");
  g.hCapLayer = readVector("horizontal", "capacity", 0, "horizontal capacity");
  g.minWidth = readVector("minimum", "width", 1, "minimum width");
  g.minSpacing = readVector("minimum", "spacing", 0, "minimum spacing");
  g.viaSpacing = readVector("via", "spacing", 0, "via spacing");

  g.llx = tk.nextInt();
  g.lly = tk.nextInt();
  g.tileW = tk.nextIntIn(1, kIntMax, "tile width");
  g.tileH = tk.nextIntIn(1, kIntMax, "tile height");

  g.allocate();

  tk.expectKeyword("num");
  tk.expectKeyword("net");
  const int numNets = tk.nextIntIn(0, kIntMax, "net count");
  d.nets.reserve(static_cast<size_t>(numNets));
  std::unordered_set<std::string> names;
  std::unordered_set<long long> ids;
  names.reserve(static_cast<size_t>(numNets));
  ids.reserve(static_cast<size_t>(numNets));

  for (int i = 0; i < numNets; ++i) {
    Net n;
    const int hl = tk.expectRecord(4, "net " + std::to_string(i + 1) + " of " + std::to_string(numNets) +
                                          ": a net header (name id numPins minWidth)");
    n.name = tk.next();
    n.id = tk.nextInt();
    const int numPins = tk.nextIntIn(1, kIntMax, "net " + n.name + ": pin count");
    n.minWidth = tk.nextIntIn(1, kIntMax, "net " + n.name + ": minimum width");
    if (!names.insert(n.name).second)
      tk.error(hl, "net name " + n.name + " appears twice");
    if (!ids.insert(n.id).second)
      tk.error(hl, "net " + n.name + ": id " + std::to_string(n.id) + " is used by another net");
    n.pins.reserve(static_cast<size_t>(numPins));
    for (int p = 0; p < numPins; ++p) {
      const int pl = tk.expectRecord(3, "net " + n.name + ": pin " + std::to_string(p + 1) + " of " +
                                            std::to_string(numPins) + " (x y layer)");
      long long px = tk.nextInt();
      long long py = tk.nextInt();
      long long layer = tk.nextInt();
      Pin pin;
      pin.x = g.colOf(px);
      pin.y = g.rowOf(py);
      if (layer < 1 || layer > g.nLayers)
        tk.error(pl, "net " + n.name + ": pin layer " + std::to_string(layer) + " is not in 1.." +
                         std::to_string(g.nLayers));
      pin.layer = static_cast<int>(layer) - 1; // file layers are one based
      if (!g.inBounds(pin.x, pin.y, pin.layer))
        tk.error(pl, "net " + n.name + ": pin (" + std::to_string(px) + ", " + std::to_string(py) +
                         ") is outside the grid (tile " + std::to_string(pin.x) + ", " + std::to_string(pin.y) +
                         " of " + std::to_string(g.nx) + " x " + std::to_string(g.ny) + ")");
      n.pins.push_back(pin);
    }
    d.nets.push_back(std::move(n));
  }

  // Capacity adjustments: optional as a block.
  if (!tk.done()) {
    tk.expectRecord(1, "the capacity adjustment count");
    const int numAdj = tk.nextIntIn(0, kIntMax, "capacity adjustment count");
    std::unordered_map<long long, int> seenH, seenV; // edge index -> adjusted capacity
    for (int a = 0; a < numAdj; ++a) {
      const int al = tk.expectRecord(7, "capacity adjustment " + std::to_string(a + 1) + " of " +
                                            std::to_string(numAdj) + " (col row layer col row layer capacity)");
      int c1 = tk.nextIntIn(0, kIntMax, "adjustment column");
      int r1 = tk.nextIntIn(0, kIntMax, "adjustment row");
      int l1 = tk.nextIntIn(1, kIntMax, "adjustment layer") - 1;
      int c2 = tk.nextIntIn(0, kIntMax, "adjustment column");
      int r2 = tk.nextIntIn(0, kIntMax, "adjustment row");
      int l2 = tk.nextIntIn(1, kIntMax, "adjustment layer") - 1;
      int cap = tk.nextIntIn(0, kIntMax, "adjusted capacity");
      if (!g.inBounds(c1, r1, l1) || !g.inBounds(c2, r2, l2))
        tk.error(al, "capacity adjustment refers to a tile outside the grid (tile indices, not real coordinates, "
                     "are expected here)");
      if (l1 != l2)
        tk.error(al, "capacity adjustment joins layers " + std::to_string(l1 + 1) + " and " +
                         std::to_string(l2 + 1) + " (vias have no capacity in this format)");
      const bool h = r1 == r2 && std::abs(c1 - c2) == 1, v = c1 == c2 && std::abs(r1 - r2) == 1;
      if (!h && !v)
        tk.error(al, "capacity adjustment joins tiles that are not neighbours");
      const int def = h ? g.hCapLayer[l1] : g.vCapLayer[l1];
      if (cap > def)
        tk.error(al, "capacity adjustment raises a " + std::string(h ? "horizontal" : "vertical") + " edge on layer " +
                         std::to_string(l1 + 1) + " to " + std::to_string(cap) + ", above the layer's default " +
                         std::to_string(def) + " (the format reduces capacity)");
      const size_t e = h ? g.hIndex(std::min(c1, c2), r1, l1) : g.vIndex(c1, std::min(r1, r2), l1);
      auto &seen = h ? seenH : seenV;
      auto [it, fresh] = seen.emplace(static_cast<long long>(e), cap);
      if (!fresh) {
        if (it->second != cap)
          tk.error(al, "capacity adjustment sets an edge already adjusted to " + std::to_string(it->second) +
                           " to " + std::to_string(cap));
        warn(al, "the same capacity adjustment appears twice");
      }
      (h ? g.hcap : g.vcap)[e] = cap;
    }
  }
  if (!tk.done())
    tk.error(tk.line(), "unexpected '" + tk.peek() + "' after the end of the design");
  return d;
}

inline Design readGr(const std::string &path) {
  try {
    return readGrText(readWholeFile(path), path);
  } catch (const InputError &e) {
    fail(e.what());
  }
}

} // namespace gr_io
