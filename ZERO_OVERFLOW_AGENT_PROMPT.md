# Zero-overflow fresh-start prompt

Saved 2026-09-05. This is the exact prompt handed to the background agent
tasked with restarting the global router project with total overflow -> 0
as the sole success metric, superseding CLAUDE.md's original "results in
the same range as published routers" target. Kept here as a record of the
mandate in case the agent's own CLAUDE.md addendum needs cross-referencing
or the effort needs to be re-launched.

---

You're picking up a VLSI global router research project at `~/Code/Global Router`. Read `CLAUDE.md` in the repo root first for the full technical background (cost model, `.gr` format details, verification principles) — treat it as background knowledge, but the mandate below **supersedes its stated "Target" section**. Eleven routers already exist in `gr/` (`lroute` through `sroute`), each a frozen, separately-committed artifact building on the last. Do not modify or delete any of them — they're historical record. Read their header comments (`grep -A20 "^// Result" gr/<name>.cpp`) for what was tried and measured; that history is below in condensed form so you don't have to re-derive it.

**The mandate, which is new and overrides everything else:** the single success metric is **total overflow on the three real ISPD benchmarks, and the goal is zero.** Not "in the range of published routers," not a wirelength/overflow trade-off, not a fairness reallocation — zero. Every change you make must be justified by evidence that it moves total overflow strictly downward (or is a necessary enabler toward that, argued and then verified). A mechanism that trades overflow for something else — wirelength, fairness across net classes, priority typing — is out of scope until overflow is at or near zero; it can be revisited afterward as a secondary refinement, never before.

**Benchmarks** (already downloaded locally, gitignored, do not re-fetch unless missing): `gr/bench/adaptec1.capo70.3d.35.50.90.gr`, `gr/bench/newblue1.ntup50.3d.30.50.90.gr`, `gr/bench/bigblue1.capo60.3d.50.10.100.gr`. Tools already built and validated: `gr/gr_verify.cpp` (legality + overflow/wirelength scoring) and `gr/eval2008/eval2008.pl` (the official independent scorer — cross-check every settled result against it exactly, per `CLAUDE.md`'s standing rule). A GitHub Actions workflow at `.github/workflows/build-and-validate.yml` already builds and self-tests every router and runs the full benchmark/legality/cross-check pipeline on Linux x86_64 — extend it for whatever you build rather than duplicating it, and expect it to need a policy update once overflow approaches zero (e.g. asserting overflow doesn't regress, eventually asserting `== 0`).

**Best overflow numbers ever recorded in this lineage** (from committed headers — verify these yourself via `gr_verify`/`eval2008.pl` before trusting them as the bar, don't just take my word for it):

| benchmark | best committed overflow | router |
|---|---|---|
| adaptec1 | 173790 | eroute |
| newblue1 | 42642 | hroute |
| bigblue1 | 302770 | hroute |

Every router in this lineage is still hundreds of thousands of tracks short of zero. **None of them have ever converged.**

**The critical open question you should resolve before designing anything new:** every router in this lineage prints `"hit the round cap"` on all three real benchmarks — never once `"converged to zero overflow"` — using a fixed default round cap (typically 30-52 rounds depending on router). Nobody has ever checked whether overflow keeps monotonically decreasing if you let it run far longer with retuned pricing (epsilon, threshold, power), or whether it plateaus well above zero. This is your first experiment, before any architecture change: take the current best router (`hroute` or `eroute`), crank the round cap and re-tune epsilon, and plot overflow vs. round count out to 5-10x the current cap. If it's still dropping, the problem may just be patience/tuning. If it plateaus, the negotiated-congestion mechanism as currently built (multiplicative-weight Lagrangian pricing, no historical-cost accumulation) is structurally insufficient and you need a different core mechanism — the published literature's answer for this (FastRoute, NCTU-GR, and the original Pathfinder FPGA router) is negotiated congestion with a **history cost term that accumulates permanently across rounds** (not just current-round usage/capacity), which is notably absent from every router built so far. That's the most likely single missing piece, but confirm it empirically rather than assuming it.

**What to keep from this project's hard-won discipline** (all validated repeatedly across 11 routers, not optional):
- Cross-check every settled result against `eval2008.pl`, not just `gr_verify` — they're independent, and `CLAUDE.md` mandates checking every round on all three benchmarks.
- Legality (0 disconnected/incomplete/unrouted/malformed) is non-negotiable at every step.
- Self-tests must prove a *decisive behavioral change* (e.g., "this net took path X instead of Y because of mechanism Z"), never just "it ran" or "it computed a score."
- Tune empirically — every fixed threshold in this project that was "reasoned into place" rather than measured against a real skewed distribution eventually needed retuning (see `mroute` step 3's documented failure with a fixed absolute threshold, fixed by switching to population-percentile ranking).
- Bound maze search with a fixed absolute margin, not one scaled to net span (a real, measured 100x runtime blowup was found and fixed this way).
- Escalation/relaxation must be supervised per-net (check the local overflow delta, revert if worse) — an unconditional batch acceptance spiked overflow 25x in this project's own history and never recovered.
- Report regressions and negative results as honestly as wins. This project's own committed history includes at least one full stage (layer-range pre-assignment) that was tried and reverted after honest measurement.
- Build new mechanisms as separate, comparable, frozen artifacts (new file, new commit) rather than mutating a working router in place — that's how this project has stayed comparable across 11 iterations.

**What to explicitly deprioritize:** stage 6's net-typing (`troute`) and nested-capacity (`sroute`) mechanisms are validated *reallocation* mechanisms — they redistribute which nets absorb congestion but, by this project's own measurement, make aggregate overflow slightly *worse* on every real benchmark tested. They are not wrong, but they are off the critical path for a zero-overflow mandate until the core congestion-reduction mechanism actually gets close to zero first.

**Deliverable:** update `CLAUDE.md` (or add a clearly-dated addendum) recording this mandate change for future sessions, then start from the convergence experiment above and let the evidence drive the next architectural decision — don't commit to a specific technique (history-cost negotiation, full ripup-reroute, monotonic routing, etc.) until you've measured that the current mechanism actually plateaus short of zero.
