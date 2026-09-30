# Testing the pending ideas: which, if any, do good

Written 2026-09-29 after the full ISPD 2008 suite (`HISTORY_COST_SWEEP.md`
Part 75): 11 of 16 at zero overflow; residues newblue1 54, newblue4 148,
newblue7 106, bigblue4 378, newblue3 38704; scored wirelength +0.3% to
+9.2% vs NTHU-Route 2.0; runtime up to 27 h. Six ideas are pending. This
plan says how each is tested, what counts as "did good", and in what
order.

## The ideas

1. **Plateau stop for the slow tier** (runtime). newblue3 spent all 150
   slow rounds at ~41000, gaining ~1% per 25 rounds.
2. **Several hotspot boxes instead of one** (runtime, maybe quality).
   The region stage reroutes every net touching one box around *all*
   remaining overflow: newblue7's box was 137 x 115 tiles, 265927 nets.
3. **newblue1's last 54** (quality). LMgr reaches 0, so it is routable.
4. **Wirelength on the outliers** (quality). newblue1 +9.2%, bigblue4
   +9.0%, bigblue2 +4.9%, newblue6 +3.6% vs NTHU-Route 2.0.
5. **Via-aware layer assignment** (quality; required for ISPD 2024,
   where vias consume wire capacity).
6. **The CLAUDE.md designs never built**: nested long-net capacity
   budgets, a fair-share objective, timing-driven priorities, giant-net
   decomposition, provenance tracking, demand-seeded initial prices, the
   return-to-placement exit.

## Can the order be predicted?

**Partly.**
- **Runtime ideas (1, 2, the margin cap): yes, mostly.** The recorded stage
  traces can be replayed offline to show where a stop would fire and what
  best it would keep. Part 74 did this and caught, before any run, that a
  window alone would have cost bigblue2 its zero.
- **Quality ideas (3, 4, 5, 6): only the ceiling.** A diagnostic can bound
  the upside, since a mechanism cannot recover more wirelength than the
  gap it targets. It cannot predict whether the mechanism will realize
  that upside. This project's record is blunt about reasoned predictions:
  - The revert rule that was load-bearing under lambda pricing was the
    obstacle under present-demand pricing (Parts 59-60).
  - The 6-tile box everyone "knew" was fine was the biggest hidden limiter
    (Part 51).
  - Pricing-coefficient tweaks moved results 0.6-2.7%, while structural
    changes to what the search can do (MSMV, growing boxes, pieces) moved
    them by 20-99%.

The one heuristic the record supports: **structural changes beat knob
changes**. The order below uses bounded upside x cheapness, runs the
diagnostics before building anything, and fixes each decision rule in
advance.

## Ground rules for every test

- **Off = byte-identical.** Each mechanism lands in a new router, off by
  default. Parity against its parent is a byte-identical route file at a
  small budget with the mechanism off, as Parts 74 and 76 did.
- **Noise floor first.** The router is deterministic but chaotic: a
  harmless change moves the residues. newblue1's region stage wandered
  between 70 and 88 over rounds 399-499 (Part 68), and newblue4's best
  region state was 168-220 depending on when it stopped (Part 74). Before
  judging any quality idea, run the Part 75 baseline twice more on each
  residue benchmark with a quality-neutral perturbation (pattern-stage
  color cell 8 -> 7 and 9). A result counts only if it lies outside the
  spread of those three runs.
- **Guard set.** Every candidate that will be adopted also runs on four
  fast zero-overflow benchmarks: newblue2 (2 min), adaptec4 (9 min),
  bigblue3 (27 min), adaptec2 (32 min). Losing a zero anywhere is an
  automatic rejection.
- **Decision rules, set before the runs:**
  - *Runtime ideas* pass if runtime falls by at least 20% on the benchmarks
    they target, with overflow no worse and wirelength within +0.2% on
    every benchmark run.
  - *Quality ideas* pass if total overflow falls outside the noise spread
    on at least one residue benchmark, or scored wirelength falls by more
    than 0.5%, with no regression on the other metric, the guard set or
    any residue benchmark.
- **Everything is verified:** `status=OK`, `gr_verify` = `eval2008.pl`,
  raw numbers into `HISTORY_COST_SWEEP.md` as they arrive, at most 3
  concurrent routers, and larger benchmarks only on the fastest proven
  code.

## Order

### Phase 0 -- in flight or free (now)

- **Margin cap** (`pd13route`, Part 76): the screens are running.
  Predicted big runtime win on bigblue4. The risk is bigblue2's late
  zero. Judge by the runtime rule.
- **Noise-floor runs**: newblue1 and newblue4, two perturbed baselines
  each (~16 h of slot time, background).

### Phase 1 -- diagnostics, no new router (about a day of scripts over existing route files and traces)

- **D1, residue anatomy** (ideas 2 and 3). For newblue1, newblue4,
  newblue7 and bigblue4:
  - Split the residue into 2D overflow and layer-assignment overflow.
  - Cluster the overflowed edges into connected hotspots: how many, how
    big, how far apart, how many nets touch each.
  - Rerun `gr/lpbound.cpp` on each cluster.

  If most of a residue is in several small, separated clusters, idea 2's
  upside is real and it moves up. If it is one cluster, idea 2 is a
  runtime-only idea.
- **D2, wirelength anatomy** (ideas 4 and 5). Per benchmark:
  - Split scored wirelength into wire and vias. This is done above: vias
    are 27-47% of it.
  - Break the vias down into corners, pins and straight (the Part 69
    script).
  - Measure detour: per-net wire vs its pins' rectilinear MST length.

  The four worst wirelength gaps are among the most via-heavy designs, so
  the prediction is that the gap is mostly vias. This diagnostic confirms
  or refutes that before anything is built. NTHU-Route 2.0's own routes
  are only available for adaptec1, where our vias are within 3.5% of its
  (1.836M vs 1.775M). For the outliers, compare against the MST bound
  instead.
- **D3, slow-tier replay** (idea 1). Replay a floor-gated plateau rule on
  every recorded slow tier, as Part 74 did for the region stage.

### Phase 2 -- build in the order the diagnostics point to

Expected order, and why:

1. **Idea 1, slow-tier plateau.** Cheapest, and predictable from D3. The
   likely outcome is a modest runtime win on newblue3 only, since the
   floor keeps it off the small residues. About 1 hour to build; test on
   newblue3 plus the guard set.
2. **Idea 5 as a wirelength tool, plus idea 4: via reduction in layer
   assignment.** This comes early if D2 confirms the gap is vias, because
   the upside is large (the four worst gaps are the via-heavy designs)
   and the same code is needed for ISPD 2024. Candidates, one at a time:
   - a via-count tie-break that also weighs the stacks to pins;
   - net ordering in layer assignment by via savings;
   - a post-assignment pass that re-assigns single nets to cut vias
     without adding overflow, like the WL recovery pass but in 3D.

   Test on newblue1, bigblue4, bigblue2, newblue6 plus the guard set.
3. **Idea 2, multi-region hotspots,** if D1 finds several separated
   clusters. Each cluster gets its own box, routed one after another (or
   in parallel later -- disjoint boxes are exactly what step 4's
   multithreading wants). Test on newblue7, newblue4, bigblue4, newblue1.
4. **Idea 3, newblue1.** Mostly served by 2 and by 6's nested budgets.
   What remains specific to it is a mechanism no router here has tried:
   FastRoute 3.0's virtual capacity (pricing against a lowered capacity
   in the hotspot so negotiation over-corrects). Screen on newblue1 only.
5. **Idea 6, the parts that can move a score here:**
   - *Nested long-net budgets*: the second price that pushes long nets out
     of a contested edge before local nets feel it. Structural and
     untried; screen on the residue benchmarks.
   - *Demand-seeded initial prices*: probably little upside, because the
     pattern stage already prices by demand before any maze search; one
     cheap screen on adaptec1 at the screen budget.
   - The rest cannot move an ISPD 2008 or 2024 score and are deferred to
     where they can:
     - timing-driven priorities: ISPD 2025 / OpenROAD;
     - fair-share objective: re-examine with timing;
     - giant-net decomposition: none of the scored nets is giant; revisit
       at ISPD 2024 scale;
     - provenance tracking: a debugging tool;
     - return-to-placement exit: needs placement.

## Prior art: which ideas the contest winners already use

| idea | in prior art? | where |
|---|---|---|
| 1 slow-tier plateau | yes, common | NTHU-Route 2.0 stops its main stage on an overflow threshold and iteration limits; FastRoute bounds its rip-up iterations. Our floor gate (stop only far from zero) is a local detail. |
| 2 region-focused rip-up | yes, in spirit | NTHU-Route 2.0's congested-region identification orders rip-up by congested regions; BoxRouter grows boxes out from congestion. Per-cluster boxes are a variation, not a new idea. |
| 3 newblue1 residue | not an idea but a target | LMgr reaches 0 with dynamic topology updates; FastRoute 3.0's virtual capacity is the untried candidate. |
| 4 wirelength | yes | FastRoute's Steiner-tree edge shifting and topology optimization; NTHU's post-routing refinement; our WL recovery pass is the same family. |
| 5 via-aware layer assignment | yes | NTHU-Route 2.0's layer assignment minimizes vias (ours copies it); FastRoute 4.x is via-aware; the ISPD 2024 routers (CUGR 2 lineage, InstantGR) price via demand in 3D pattern routing. |
| 6a nested long-net budgets | as formulated here, apparently not | Contest routers steer long nets with layer rules, not a second Lagrangian price on a nested capacity constraint. This is the most plausibly novel of the six -- CLAUDE.md's "at least one novel contribution" candidate. |
| 6b fair-share objective | not in the ISPD 2008 / 2024 routers | Fairness appears in network-flow literature, not in these contest routers. |
| 6c timing-driven priorities | yes | ISPD 2025 entries, OpenROAD's FastRoute critical-net handling. |
| 6d giant-net decomposition, 6f demand-seeded prices | yes, standard | FLUTE-based decomposition; probabilistic congestion estimates before the first route (FastRoute, NTHU). |
| 6e provenance, 6g placement exit | tooling / flow, not router scoring | -- |

These attributions come from the published descriptions of those routers
and the papers cited in Part 75. They were not checked against source
code, so treat "yes" as "described in their papers".
