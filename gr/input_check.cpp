// input_check.cpp -- reads routing inputs with the strict readers the routers and verifiers use (gr_io.h for
// ISPD 2008 .gr, g24_io.h for ISPD 2024/2025 .cap + .net; HISTORY_COST_SWEEP.md Part 155) and reports what it
// found, or the first rule a file breaks, with its line. Nothing is routed.
//
//   g++ -std=c++17 -O2 -Wall -Wextra -o input_check input_check.cpp
//   ./input_check design.gr                    an ISPD 2008 design
//   ./input_check design.cap design.net        an ISPD 2024 or 2025 design
//   ./input_check --selftest                   every rule, fed a file that breaks it
//
// Why: the readers' dominant catch is bad input data, not router bugs (CLAUDE.md, "Verification
// principles"), and the ISPD files are clean, so the rules were never exercised. The self-test feeds one
// broken file per rule and expects that rule's error -- a reader that accepted any of them, or rejected one
// for the wrong reason, fails it -- plus good files that must read with no error and no warning.
#include "g24_io.h"
#include "gr_io.h"

#include <cstdio>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  if (!ok)
    ++failures;
}

// ---- ISPD 2008 ---------------------------------------------------------------------------------------------

// A small good design: 4 x 3 tiles, 2 layers (layer 1 horizontal, layer 2 vertical), tiles 10 x 10 at (0, 0).
const char *kHeader = "grid 4 3 2\n"
                      "vertical capacity 0 20\n"
                      "horizontal capacity 20 0\n"
                      "minimum width 1 1\n"
                      "minimum spacing 1 1\n"
                      "via spacing 1 1\n"
                      "0 0 10 10\n";
const char *kNets = "num net 2\n"
                    "n0 0 2 1\n"
                    "5 5 1\n"
                    "35 25 2\n"
                    "n1 1 2 1\n"
                    "15 5 1\n"
                    "15 25 2\n";
const char *kAdj = "2\n"
                   "0 0 1 1 0 1 10\n"
                   "1 1 2 1 2 2 0\n";

std::string good() { return std::string(kHeader) + kNets + kAdj; }

// Replace the first occurrence of `from` in `text` (which must occur).
std::string with(const std::string &text, const std::string &from, const std::string &to) {
  size_t at = text.find(from);
  if (at == std::string::npos) {
    std::cerr << "selftest fixture: '" << from << "' not found\n";
    std::exit(2);
  }
  return text.substr(0, at) + to + text.substr(at + from.size());
}

// Reads `text`; returns the error message ("" if it read), collecting warnings.
std::string readGrMsg(const std::string &text, std::vector<std::string> *warnings = nullptr) {
  try {
    gr_io::readGrText(text, "t.gr", warnings);
    return "";
  } catch (const gr_io::InputError &e) {
    return e.what();
  }
}

void expectGrError(const std::string &rule, const std::string &text, const std::string &needle) {
  std::string m = readGrMsg(text);
  check(m.find(needle) != std::string::npos, "2008: " + rule + " is rejected" +
                                                 (m.empty() ? std::string(" (it was accepted)") : " -- " + m));
}

void selftest2008() {
  {
    std::vector<std::string> w;
    std::string m = readGrMsg(good(), &w);
    check(m.empty() && w.empty(), "2008: the good design reads with no error and no warning" +
                                      (m.empty() ? std::string() : " -- " + m));
    gr_io::Design d = gr_io::readGrText(good(), "t.gr");
    check(d.nets.size() == 2 && d.nets[0].pins[1].x == 3 && d.nets[0].pins[1].y == 2 &&
              d.nets[0].pins[1].layer == 1 && d.grid.hcap[d.grid.hIndex(0, 0, 0)] == 10 &&
              d.grid.vcap[d.grid.vIndex(1, 1, 1)] == 0 && d.grid.hcap[d.grid.hIndex(1, 0, 0)] == 20,
          "2008: the good design's pins and adjusted capacities are what the file says");
    // without the adjustment block, and with an empty one
    check(readGrMsg(std::string(kHeader) + kNets).empty(), "2008: the adjustment block may be absent");
    check(readGrMsg(std::string(kHeader) + kNets + "0\n").empty(), "2008: an empty adjustment block reads");
  }
  // header
  expectGrError("a zero grid width", with(good(), "grid 4 3 2", "grid 0 3 2"), "grid width 0 is out of range");
  expectGrError("a negative capacity", with(good(), "vertical capacity 0 20", "vertical capacity 0 -20"),
                "vertical capacity of layer 2 -20 is out of range");
  expectGrError("a zero minimum width", with(good(), "minimum width 1 1", "minimum width 1 0"),
                "minimum width of layer 2 0 is out of range");
  expectGrError("a negative minimum spacing", with(good(), "minimum spacing 1 1", "minimum spacing -1 1"),
                "minimum spacing of layer 1 -1 is out of range");
  expectGrError("a zero tile width", with(good(), "0 0 10 10", "0 0 0 10"), "tile width 0 is out of range");
  expectGrError("a non-integer", with(good(), "0 0 10 10", "0 0 10.5 10"), "expected an integer, found '10.5'");
  expectGrError("an integer too large for the format", with(good(), "grid 4 3 2", "grid 4 3 99999999999"),
                "layer count 99999999999 is out of range");
  expectGrError("a missing keyword", with(good(), "minimum spacing", "min spacing"),
                "expected keyword 'minimum', found 'min'");
  expectGrError("a negative net count", with(good(), "num net 2", "num net -2"), "net count -2 is out of range");
  // nets
  expectGrError("a net header missing its width", with(good(), "n0 0 2 1\n", "n0 0 2\n"),
                "expected net 1 of 2: a net header (name id numPins minWidth) (4 values on the line), found 3");
  expectGrError("a pin with an extra value", with(good(), "5 5 1\n", "5 5 1 7\n"),
                "expected net n0: pin 1 of 2 (x y layer) (3 values on the line), found 4");
  expectGrError("a net with fewer pin lines than its count", with(good(), "n0 0 2 1\n5 5 1\n35 25 2\n", "n0 0 2 1\n5 5 1\n"),
                "expected net n0: pin 2 of 2 (x y layer) (3 values on the line), found 4");
  expectGrError("two records on one line", with(good(), "5 5 1\n35 25 2\n", "5 5 1 35 25 2\n"),
                "(3 values on the line), found 6");
  expectGrError("a net with no pins", with(good(), "n1 1 2 1\n15 5 1\n15 25 2\n", "n1 1 0 1\n"),
                "net n1: pin count 0 is out of range");
  expectGrError("a net width of zero", with(good(), "n1 1 2 1", "n1 1 2 0"), "net n1: minimum width 0 is out of range");
  expectGrError("a repeated net name", with(good(), "n1 1 2 1", "n0 1 2 1"), "net name n0 appears twice");
  expectGrError("a repeated net id", with(good(), "n1 1 2 1", "n1 0 2 1"), "net n1: id 0 is used by another net");
  expectGrError("a pin past the grid's right edge", with(good(), "35 25 2", "45 25 2"),
                "net n0: pin (45, 25) is outside the grid");
  // floor, not truncation: x = -5 is tile -1 (it used to land in tile 0)
  expectGrError("a pin just left of the origin", with(good(), "5 5 1\n", "-5 5 1\n"),
                "net n0: pin (-5, 5) is outside the grid (tile -1, 0");
  expectGrError("a pin layer of 0", with(good(), "5 5 1\n", "5 5 0\n"), "net n0: pin layer 0 is not in 1..2");
  expectGrError("a pin layer above the top", with(good(), "5 5 1\n", "5 5 3\n"), "net n0: pin layer 3 is not in 1..2");
  expectGrError("more nets declared than present", with(good(), "num net 2", "num net 3"),
                "expected net 3 of 3: a net header (name id numPins minWidth) (4 values on the line), found 1");
  // capacity adjustments
  expectGrError("an adjustment between layers", with(good(), "0 0 1 1 0 1 10", "0 0 1 0 0 2 10"),
                "capacity adjustment joins layers 1 and 2");
  expectGrError("an adjustment between tiles that are not neighbours", with(good(), "0 0 1 1 0 1 10", "0 0 1 2 0 1 10"),
                "joins tiles that are not neighbours");
  expectGrError("an adjustment outside the grid", with(good(), "1 1 2 1 2 2 0", "1 2 2 1 3 2 0"),
                "refers to a tile outside the grid");
  expectGrError("an adjustment that raises capacity", with(good(), "0 0 1 1 0 1 10", "0 0 1 1 0 1 30"),
                "above the layer's default 20");
  expectGrError("an adjustment in a direction the layer has no capacity in", with(good(), "1 1 2 1 2 2 0", "1 1 2 2 1 2 5"),
                "raises a horizontal edge on layer 2 to 5, above the layer's default 0");
  expectGrError("a negative adjusted capacity", with(good(), "0 0 1 1 0 1 10", "0 0 1 1 0 1 -1"),
                "adjusted capacity -1 is out of range");
  expectGrError("an adjustment with a missing value", with(good(), "0 0 1 1 0 1 10", "0 0 1 1 0 10"),
                "capacity adjustment 1 of 2 (col row layer col row layer capacity) (7 values on the line), found 6");
  expectGrError("fewer adjustments than the count", with(good(), "2\n0 0 1", "3\n0 0 1"), "capacity adjustment 3 of 3");
  expectGrError("an edge adjusted twice to different values",
                with(good(), "2\n0 0 1 1 0 1 10\n", "3\n0 0 1 1 0 1 10\n1 0 1 0 0 1 12\n"),
                "sets an edge already adjusted to 10 to 12");
  {
    std::vector<std::string> w;
    std::string m = readGrMsg(with(good(), "2\n0 0 1 1 0 1 10\n", "3\n0 0 1 1 0 1 10\n1 0 1 0 0 1 10\n"), &w);
    check(m.empty() && w.size() == 1 && w[0].find("appears twice") != std::string::npos,
          "2008: the same adjustment twice reads, with a warning");
  }
  expectGrError("text after the last adjustment", good() + "7\n", "unexpected '7' after the end of the design");
  expectGrError("text after the last net, with no adjustments", std::string(kHeader) + kNets + "0\nextra\n",
                "unexpected 'extra' after the end of the design");
  {
    std::vector<std::string> w;
    std::string m = readGrMsg(with(good(), "horizontal capacity 20 0", "horizontal capacity 20 10"), &w);
    check(m.empty() && w.empty(), "2008: a layer with capacity in both directions reads (the format allows it)");
  }
  // the error names the line
  {
    std::string m = readGrMsg(with(good(), "35 25 2", "45 25 2"));
    check(m.rfind("t.gr:11: ", 0) == 0, "2008: an error names its line -- " + m);
  }
}

int main2008(const std::string &path) {
  std::vector<std::string> warnings;
  gr_io::Design d;
  try {
    d = gr_io::readGrText(gr_io::readWholeFile(path), path, &warnings);
  } catch (const gr_io::InputError &e) {
    std::cout << "REJECTED " << e.what() << "\n";
    return 1;
  }
  long long pins = 0;
  for (const gr_io::Net &n : d.nets)
    pins += static_cast<long long>(n.pins.size());
  std::cout << "OK " << path << ": grid " << d.grid.nx << " x " << d.grid.ny << " x " << d.grid.nLayers << ", "
            << d.nets.size() << " nets, " << pins << " pins, " << warnings.size() << " warnings\n";
  for (size_t i = 0; i < warnings.size() && i < 20; ++i)
    std::cout << "  warning: " << warnings[i] << "\n";
  return 0;
}


// ---- ISPD 2024 / 2025 ----------------------------------------------------------------------------------------

// g24_verify's hand-checked fixture: 3 layers, 4 x 3 GCells.
const char *kCap = "3 4 3\n1 4 10 20 30\n5 6 7\n8 9\n"
                   "M1 0 0\n0 0 0 0\n0 0 0 0\n0 0 0 0\n"
                   "M2 0 1\n1 1 1 1\n2 2 2 2\n0 1 1 1\n"
                   "M3 1 1\n1 1 1 1\n1 1 1 1\n1 1 1 1\n";
const char *kNet = "netA\n(\n[(0, 0, 0)]\n[(0, 3, 0), (0, 3, 1)]\n)\n"
                   "netB\n(\n[(0, 1, 1)]\n[(0, 1, 2)]\n)\n";

struct Read24 {
  std::string err;           // "" if both readers accepted
  bool agree = true;         // the iostream and FastText readers gave the same verdict and message
  std::vector<g24::Net> nets; // from the FastText reader
  g24::Resources res;
};

void writeFile(const std::string &path, const std::string &text) {
  std::FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) {
    std::cerr << "selftest: cannot write " << path << "\n";
    std::exit(2);
  }
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
}

// Both readers, the verifier's (iostream) and the routers' (FastText), on the same text.
Read24 read24(const std::string &cap, const std::string &net) {
  Read24 out;
  std::string e1, e2;
  {
    g24::Resources r;
    std::vector<g24::Net> nets;
    std::istringstream c(cap), n(net);
    if (!g24::readResources(c, r, e1) || !g24::readNets(n, nets, e1) || !g24::checkAccessPoints(r, nets, e1))
      if (e1.empty())
        e1 = "(rejected without a message)";
  }
  {
    const std::string cp = "input_check_selftest.cap", np = "input_check_selftest.net";
    writeFile(cp, cap);
    writeFile(np, net);
    g24::FastText cf, nf;
    cf.open(cp);
    nf.open(np);
    if (!g24::readResourcesFast(cf, out.res, e2)) {
      if (e2.empty())
        e2 = "(rejected without a message)";
    } else {
      g24::FastNetStream ns(nf);
      g24::Net n;
      while (ns.next(n, e2)) {
        if (!g24::checkAccessPointsNet(out.res, n, e2))
          break;
        out.nets.push_back(n);
      }
    }
    std::remove(cp.c_str());
    std::remove(np.c_str());
  }
  out.err = e1;
  out.agree = e1 == e2;
  if (!out.agree)
    out.err = "iostream: '" + e1 + "', FastText: '" + e2 + "'";
  return out;
}

void expect24Error(const std::string &rule, const std::string &cap, const std::string &net, const std::string &needle) {
  Read24 r = read24(cap, net);
  check(r.agree && r.err.find(needle) != std::string::npos,
        "2024: " + rule + " is rejected" + (r.err.empty() ? std::string(" (it was accepted)") : " -- " + r.err));
}

void selftest2024() {
  const std::string cap = kCap, net = kNet;
  {
    Read24 r = read24(cap, net);
    check(r.agree && r.err.empty() && r.nets.size() == 2 && r.nets[0].pins[1].size() == 2 &&
              r.nets[0].pins[1][1].x == 3 && r.nets[0].pins[1][1].y == 1 && r.res.capacity(1, 0, 2) == 0,
          "2024: the good design reads, the same in both readers" + (r.err.empty() ? std::string() : " -- " + r.err));
    Read24 b = read24(cap, "\n" + with(net, ")\nnetB", ")\n\nnetB") + "\n\n");
    check(b.err.empty() && b.nets.size() == 2, "2024: blank lines between records read");
    // ISPD 2025 pin lines: "pin_name, value, [(z, x, y), ...]", names escaping brackets
    Read24 c = read24(with(cap, "1 4 10 20 30\n5 6 7\n8 9\n", "1 4 10 20 30 1 2 3 4\n5 5 5\n5 5\n"),
                      "netA\n(\nu1/A, -0.5, [(0, 0, 0)]\nu2/Z\\[3\\], 1e-3, [(0, 3, 0), (0, 3, 1)]\n)\n");
    check(c.err.empty() && c.nets.size() == 1 && c.nets[0].timing.size() == 2 && c.nets[0].timing[0] == -0.5f &&
              c.nets[0].pins[1].size() == 2,
          "2025: pin lines with names and timing values read" + (c.err.empty() ? std::string() : " -- " + c.err));
  }
  // .cap
  expect24Error("text after the header's three values", with(cap, "3 4 3\n", "3 4 3 7\n"), net,
                "unexpected '7' after nLayers xSize ySize");
  expect24Error("a negative via cost", with(cap, "1 4 10", "1 -4 10"), net, "unit costs must be finite and >= 0");
  expect24Error("a negative overflow cost", with(cap, "1 4 10 20 30", "1 4 10 -20 30"), net,
                "layer 1's must be finite and >= 0");
  expect24Error("a zero pitch", with(cap, "5 6 7\n", "5 0 7\n"), net, "horizontal pitch (0 at GCell 1");
  expect24Error("layer 0 with direction 2", with(cap, "M1 0 0\n", "M1 2 0\n"), net, "layer 0 (direction 2");
  expect24Error("a negative minimum length", with(cap, "M3 1 1\n", "M3 1 -1\n"), net, "minimum length must be");
  expect24Error("a negative capacity", with(cap, "2 2 2 2\n", "2 2 -2 2\n"), net,
                "capacity on layer 1 at GCell (2, 1): capacities must be finite and >= 0");
  expect24Error("a short capacity table", with(cap, "1 1 1 1\n1 1 1 1\n1 1 1 1\n", "1 1 1 1\n1 1 1 1\n1 1 1\n"), net,
                "short capacity table on layer 2");
  expect24Error("text after the last capacity table", cap + "1\n", net,
                "unexpected '1' after the last layer's capacity table");
  // .net
  {
    Read24 r = read24(cap, "[(0, 0, 0)]\n" + net);
    check(r.agree && r.err.find("line 1: pin line before any net name") != std::string::npos,
          "2024: a pin line before any net is rejected -- " + r.err);
  }
  expect24Error("a net without its '(' line", cap, with(net, "netA\n(\n", "netA\n"),
                "line 2: net netA: expected '(' after the net name, found '[(0, 0, 0)]'");
  expect24Error("a net with no pins", cap, with(net, "netB\n(\n[(0, 1, 1)]\n[(0, 1, 2)]\n)\n", "netB\n(\n)\n"),
                "line 8: net netB has no pins");
  expect24Error("an access point with two values", cap, with(net, "[(0, 3, 0), (0, 3, 1)]", "[(0, 3, 0), (0, 3)]"),
                "line 4: net netA: malformed access point (expected '(z, x, y)' with integers)");
  expect24Error("an access point with a non-integer", cap, with(net, "[(0, 0, 0)]", "[(0, 0.5, 0)]"),
                "malformed access point");
  expect24Error("a missing ']'", cap, with(net, "[(0, 0, 0)]", "[(0, 0, 0)"), "expected ',' or ']'");
  expect24Error("text after the access list", cap, with(net, "[(0, 0, 0)]", "[(0, 0, 0)] x"),
                "unexpected text after the access list");
  expect24Error("an empty access list", cap, with(net, "[(0, 0, 0)]", "[]"), "pin with no access points");
  expect24Error("a stray line inside a net", cap, with(net, "[(0, 0, 0)]\n", "[(0, 0, 0)]\nnetZ\n"),
                "line 4: net netA: expected a pin line or ')', found 'netZ'");
  expect24Error("a one-character line where a net name belongs", cap, net + "x\n", "expected a net name, found 'x'");
  expect24Error("a file ending inside a net", cap, net + "netC\n(\n[(0, 2, 2)]\n", "the file ends inside net netC");
  expect24Error("a repeated net name", cap, with(net, "netB\n", "netA\n"),
                "the net names on lines 1 and 6 are the same");
  expect24Error("an access point outside the grid", cap, with(net, "[(0, 1, 2)]", "[(0, 1, 3)]"),
                "net netB: access point outside the grid");
}

int main2024(const std::string &capPath, const std::string &netPath) {
  g24::FastText cf, nf;
  if (!cf.open(capPath) || !nf.open(netPath)) {
    std::cout << "REJECTED cannot open " << capPath << " or " << netPath << "\n";
    return 1;
  }
  g24::Resources r;
  std::string err;
  if (!g24::readResourcesFast(cf, r, err)) {
    std::cout << "REJECTED " << capPath << ": " << err << "\n";
    return 1;
  }
  g24::FastNetStream ns(nf);
  g24::Net n;
  long long nets = 0, pins = 0, access = 0;
  while (ns.next(n, err)) {
    if (!g24::checkAccessPointsNet(r, n, err))
      break;
    ++nets;
    pins += static_cast<long long>(n.pins.size());
    for (const auto &p : n.pins)
      access += static_cast<long long>(p.size());
  }
  if (!err.empty()) {
    std::cout << "REJECTED " << netPath << ": " << err << "\n";
    return 1;
  }
  std::cout << "OK " << capPath << " + " << netPath << ": " << r.nLayers << " layers, " << r.xSize << " x " << r.ySize
            << " GCells" << (r.ppa ? " (ISPD 2025)" : "") << ", " << nets << " nets, " << pins << " pins, " << access
            << " access points\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--selftest") {
    std::cout << "input_check self test\n";
    selftest2008();
    selftest2024();
    std::cout << (failures ? "SELF TEST FAIL" : "SELF TEST PASS") << "\n";
    return failures ? 1 : 0;
  }
  if (argc == 2)
    return main2008(argv[1]);
  if (argc == 3)
    return main2024(argv[1], argv[2]);
  std::cerr << "usage: input_check design.gr | design.cap design.net | --selftest\n";
  return 2;
}
