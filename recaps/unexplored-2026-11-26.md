# Unexplored improvements, ranked (2026-11-26)

What recent published global-routing work suggests this project has not yet tried, ranked by the chance that a first
test pays off, weighed against what it would gain and cost. Written after Parts 215-217.

## How the ranking was made

- **Sources.** Web searches on 2026-11-26 (listed at the end): the ISPD 2024 and 2025 contest pages and winners; the
  GPU routers InstantGR, InstantGR 2.0, HeLEM-GR, GANGR, FastGR and GAMER; the differentiable router DGR; GAP-LA; EDGE
  (CUGR 2.0); SPRoute 2.0; SALT and OASALT; GR-Evolve; BonnRoute (ISPD 2026 invited talk); an obstacle-avoiding
  sparse-maze flow. Several were read only through abstracts and search summaries, not in full. Those figures are
  marked "(abstract)" below.
- **Was it tried here?** Each idea was searched in `HISTORY_COST_SWEEP.md`, `BEST_RESULTS_PLAN.md` and `recaps/`
  (term counts on 2026-11-26). An idea marked "not in the record" had no matching entry. That supports "not tried"
  only as far as those terms reach: work recorded under other words would be missed.
- **P** is my estimate, not a measurement, that the first test listed passes the project's bar:
  - **quality:** better by the contest's own scorer on most designs tried, and no design worse by more than its
    measured spread;
  - **speed:** at least 25% less wall time with the same results, or results inside the spread.

  The basis for each P is given in its row.
- **Rank** orders the ideas by P times the size of the gain, divided by the cost. Two ideas with equal P are ordered
  by their gain.
- **Kind:**
  - **Q / S:** quality or speed.
  - **linear:** an algorithmic or sequential change.
  - **parallel:** a gain that comes from running work concurrently, on CPU threads or a GPU.

## What this project's own experiments already establish

- **2025 is limited by timing and by OpenROAD time.**
  - The best routes come from the Elmore stage: per-net rows from `elmore_rows.py`, priced by the driver's resistance
    times each layer's capacitance (Part 215).
  - Each refinement round is about 22 minutes, and about 90% of it is the OpenROAD session (Part 216).
  - Only 0.3-1.9% of nets change route from one round to the next (Part 216, `churn.py`).
- **Refinement's stop rule mattered more than its weighting.**
  - Patience 3 with a cap of 48 gained on two designs (blind mempool_group 0.8855 -> 0.8467, Part 217).
  - Fading the classes (decay) and changing the strength ratio between classes lost on every design tried
    (Parts 216-217).
  - Visible mempool_group reaches a fixed point at round 2, so the cap never acts there.
- **The Elmore rows are a per-net constant.** One row per net uses a single downstream capacitance (`C_down`) for the
  whole net. The delay of a segment does not depend on where it sits in the tree, or on the resistance upstream of it
  (`gr/elmore_rows.py` header).
- **A narrower critical set alone lost.** Routed-slack classes, a few hundred to a few thousand nets, lost to
  input-slack classes. That held even after the 11-20 slack-script fix, although the fix made them 1.0-1.4 better on
  visible mempool_group (Part 217).
- **2008:** eleven of sixteen designs are at zero overflow. The residues sit close to proven bounds: newblue7 56
  against at least 54 forced; newblue4 is limited by pin placement. Only newblue3 (31434; at least 26264 forced; best
  published 31024) leaves room.

## Ranked list

| # | Idea (source) | Kind | P | Gain if it works | Cost | Status here | First test |
|---|---|---|---|---|---|---|---|
| 1 | **Path-aware Elmore look-ahead in the layer-assignment dynamic program**: each segment's delay uses the resistance upstream of it and the capacitance below it, with criticality per sink, inside the tree DP that assigns layers (GAP-LA; its v2 abstract reports 8.5-18.3% better worst slack on the ISPD 2025 winners' own 2D routes) | Q, linear (GPU optional) | 0.55 | Large: worst and total slack carry most of the 2025 score on ariane, mempool_tile_wrap and mempool_group | Days (C++ in the layer-assignment DP) | Our version is per-net rows (Part 215). GAP-LA was read for its critical set (Part 215), and its look-ahead is not built. | Blind ariane and blind mempool_tile_wrap, best K, against the Elmore stage's routes |
| 2 | **Incremental timing between refinement rounds**: keep one OpenROAD process alive, re-estimate parasitics and re-time only the nets whose routes changed | S, linear | 0.40 | Very large: 3-10x on the 2025 loop if only 1-2% of nets need work. That would make both mempool_cluster designs affordable. | Days. Feasibility unverified: OpenROAD's documentation does not describe incremental `estimate_parasitics -global_routing`. | Not in the record ("incremental timing / STA / parasitic": 0 hits) | Read OpenROAD's `est` source for an incremental path; time one round of visible mempool_tile_wrap both ways and compare slack files byte for byte |
| 3 | **Screen candidates with our own Elmore delay model before OpenROAD**: the ladder's 6 routes and the Elmore stage's 3-5 strengths are all scored today; run OpenROAD only on the predicted top few | S, linear | 0.50 | Medium: 30-50% fewer sessions outside refinement | Hours to a day (the model exists in `elmore_rows.py`) | Not in the record ("screen / predict score": 0 hits) | On the twelve designs' saved loop tables: does a model rank put the measured best in its top 2? |
| 4 | **Timing-driven tree shapes for critical nets**: a shallow-light Steiner tree (SALT) with each net's trade-off set from its slack, the Prim-Dijkstra slider CLAUDE.md plans (OASALT reports 15% better worst delay at 10% less wire) | Q, linear | 0.40 | Medium. Topology fixes path length, which layer choice cannot. The congestion cost is a risk. | Days (SALT is open source, `github.com/chengengjie/salt`; licence to check) | Proposed in `recaps/steiner-research-2026-10-10.html`; not in HISTORY | The 300 worst nets of blind ariane, SALT trees at two settings, at the best Elmore K |
| 5 | **Cost-distance trees as the per-net solver**: congestion prices plus each sink's criticality times its path delay. This is the project's founding Lagrangian idea with delay added (Held and Perner; BonnRoute's ISPD 2026 slides list a resource sharing model with timing constraints). | Q, linear | 0.30 | Large if it works; it subsumes #1 and #4 for the nets it routes | Weeks | Proposed in the 10-10 recap; not in HISTORY ("cost-distance": 0 hits) | After #1 and #4, only if both show timing headroom |
| 6 | **Make the router's own score match the official evaluator** (it reads 0.31% low on mempool_tile_rank) | Q, linear | 0.30 | Small to medium. The pattern stage optimizes this score (`PD34ROUTE_2D_COST=score`), so an error in it steers routes. | Hours (a diagnostic first) | Open since Part 216; cause unverified | Split the gap by term (overflow, wire, vias) on one design |
| 7 | **Randomized rounding of newblue3's fractional hotspot solution, many samples in parallel** (BonnRoute: fractional resource sharing, randomized rounding, local repair; review method 6) | Q, parallel | 0.30 | Medium, newblue3 only: 31434 against 31024 published | A day (`hotspot_cg.py`) | Listed in the 11-24 review; not run | 64 rounded samples of Part 209's fractional solution, each repaired; the best kept |
| 8 | **A stabilized lower bound for newblue3** (review method 5) | diagnostic | 0.60 | No route change. It says whether the 31434 -> 31024 gap is reachable before anyone tunes for it. | Days | Listed in the 11-24 review; not run | Stabilized column generation on Part 213's hotspot program |
| 9 | **Parallel region stage, speculative with conflict repair**, for the 2008 designs that keep overflow (track A) | S, parallel | 0.35 | Medium: the region stage is 88% of newblue7's run | Days | An exact split is dead (Part 157, A1). Batching is adopted for stages entering at 1000 units or fewer (Parts 163-167). | newblue1: speculative batches with exact replay of conflicts; residue and time against the standard |
| 10 | **Layer assignment load balance** (5.6 of 8 threads busy, Part 126) | S, parallel | 0.50 | Small: layer assignment is a minority of run time | Hours | Known since Part 126, not fixed | Work stealing over nets sorted by size; byte-identical check |
| 11 | **Detours over a routing DAG as a tier between pattern and maze** (EDGE / CUGR 2.0: create detours only where needed) | S, linear | 0.25 | Small to medium: fewer maze escalations | Days | Not in the record by name | Count the escalated nets whose final route a one-detour DAG could reach |
| 12 | **Per-design automated heuristic search** (GR-Evolve, abstract: an LLM agent rewrites the router per design, 8.72% less post-detailed-routing wire) | Q, linear | 0.20 | Medium, per design | High: 12-24 h per design by one review, plus model tokens | Not in the record (0 hits). An earlier recap set language-model tuning aside. | Not before #1-#4 |
| 13 | **GPU maze routing (GAMER-style wavefronts) on Metal** for the slow tier and region stage (one survey reports 2.25x overall on CUGR) | S, parallel | 0.20 | Medium | Weeks, and same-routes-at-any-thread-count (CLAUDE.md) is hard to keep with ties in a wavefront | Not in the record ("Bellman / wavefront": 0 hits) | Only after #9; profile first |
| 14 | **Learned or GPU-built batching** (GANGR, InstantGR 2.0) | S, parallel | 0.15 | Small | Days | Set aside in the 10-10 recap: Part 119's tile spreading removed the waste it targets | No test until the slow tier's share grows again |
| 15 | **Differentiable concurrent routing** (DGR, DAC 2024: candidate trees with probabilities, gradient descent, then rounding) | Q, parallel | 0.15 | Large if it beat our pattern and negotiation stages; no published comparison on these benchmarks found | Weeks to months, GPU | Not in the record (0 hits) | Not recommended now |
| 16 | **Soft capacity reserved for detailed routing** (SPRoute 2.0: reserves space by pin density and RUDY, the Rectangular Uniform wire Density estimate) | Q, linear | 0.10 | The contest scores here are global-routing scores, so little would count | Days | Not in the record by name | Not recommended for the contest scores |

## Notes

- **#1 and #2 are the two worth starting on.** #1 is the one quality mechanism the 2025 winners have and this router
  does not: delay that depends on a segment's place in the tree. #2 attacks the cost that kept mempool_cluster out of
  today's standard. The 2025 loop's own measurements make #2 plausible: few nets change per round, and sessions are
  90% of a round. Its feasibility depends on OpenROAD internals that have not been checked.
- **#3 is cheap and has a yes/no test on data already saved** (the twelve designs' loop tables).
- **Not counted:** running mempool_cluster on a machine with more memory than this 24 GB Mac (its sessions peak at
  80-113 GB) is an infrastructure step, not an algorithm.

## Sources

- [ISPD25 contest: performance-driven large-scale global routing (results)](https://www.ispd.cc/contests/25/index.html)
  and [contest repository index](https://github.com/liangrj2014/ISPD25_contest/blob/main/index.md)
- [Invited: ISPD 2025 performance-driven large-scale global routing contest](https://www.researchgate.net/publication/389889622_Invited_ISPD_2025_Performance-Driven_Large_Scale_Global_Routing_Contest)
  · [NVIDIA listing](https://research.nvidia.com/labs/electronic-design-automation/publication/liang2025ispd)
- [ISPD24 contest: GPU/ML-enhanced large-scale global routing](https://liangrj2014.github.io/ISPD24_contest/)
  · [contest paper](https://dl.acm.org/doi/10.1145/3626184.3639693)
  · [CUHK team announcement](https://www.cse.cuhk.edu.hk/news/achievements/shiju-lin-liang-xiao-jinwei-liu-and-prof-evangeline-f-y-young-achieved-remarkable-success-in-the-2024-international-symposium-on-physical-design-ispd-contest/)
- [GAP-LA: GPU-accelerated performance-driven layer assignment (arXiv 2507.13375)](https://arxiv.org/abs/2507.13375v1)
  · [v2](https://arxiv.org/html/2507.13375v2)
- [InstantGR (ICCAD 2024)](https://dl.acm.org/doi/10.1145/3676536.3676787)
  · [code](https://github.com/cuhk-eda/InstantGR)
  · [InstantGR, TCAD version](https://www.researchgate.net/publication/392116745_InstantGR_Scalable_GPU_Parallelization_for_3-D_Global_Routing)
  · [InstantGR 2.0](https://qqq1357962.github.io/pdf/instantgr2.0.pdf)
- [HeLEM-GR (ICCAD 2024)](https://dl.acm.org/doi/10.1145/3676536.3676650)
  · [PDF](https://yibolin.com/publications/papers/ROUTE_ICCAD2024_Zhao.pdf)
- [GANGR (arXiv 2511.17665)](https://arxiv.org/pdf/2511.17665)
- [DGR, differentiable global router (DAC 2024)](https://wadmes.github.io/cv/raw/DAC24.pdf)
- [EDGE / CUGR 2.0 (DAC 2023)](https://scholars.hkbu.edu.hk/en/publications/edge-efficient-dag-based-global-routing-engine/)
  · [CUGR code](https://github.com/cuhk-eda/cu-gr)
- [FastGR (DATE 2022)](https://www.cse.cuhk.edu.hk/~byu/papers/C138-DATE2022-FastGR.pdf)
  · [Challenges and approaches in VLSI routing (survey citing GAMER)](https://dl.acm.org/doi/pdf/10.1145/3505170.3511477)
- [SPRoute 2.0 (ASP-DAC 2022)](https://csl.yale.edu/~rajit/ps/ASPDAC_2022.pdf)
- [SALT (TCAD 2020)](https://chengengjie.github.io/papers/J2-TCAD20-SALT.pdf)
  · [code](https://github.com/chengengjie/salt)
  · [OASALT](https://tomqingo.github.io/files/OASALT_On_the_Construction_of_Obstacle-Avoiding_Steiner_shAllow-Light_Tree.pdf)
- [TILA, timing-driven incremental layer assignment (ICCAD 2015)](https://www.cse.cuhk.edu.hk/~byu/papers/C40-ICCAD2015-LPlan.pdf)
- [GR-Evolve (arXiv 2604.22234)](https://arxiv.org/pdf/2604.22234)
- [BonnRoute: classic routing algorithms with recent advances (ISPD 2026)](https://researchr.org/publication/Vygen26/bibtex)
  · [session slides](https://ispd.cc/slides/2026/protected/slides_4_3.pdf)
- [Obstacle-avoiding global routing flow with sparse maze routing (arXiv 2503.07268)](https://arxiv.org/pdf/2503.07268)
- [ISPD 2026 program (contest topic: post-placement buffering and sizing)](https://ispd.cc/ispd2027/slides/ispd2026.html)
- [OpenROAD parasitics estimation docs](https://openroad.readthedocs.io/en/latest/main/src/est/README.html)
- Project record: `HISTORY_COST_SWEEP.md` Parts 126, 157, 163-167, 207, 209, 213, 215-217;
  `recaps/steiner-research-2026-10-10.html`; `recaps/review-2026-11-24.html`.
