# Using the Global Router

How to build the router, get the benchmarks, run the three standard flows, and check the results. Every setting is
listed with its default in [`SETTINGS.md`](SETTINGS.md), which is generated from the router's own settings table. Why
each mechanism exists, and what it was measured to do, is in `HISTORY_COST_SWEEP.md`, cited by Part number throughout.

## What is current

`gr/` holds about 108 C++ files. Nearly all are retired router generations (`aroute` ... `pd38route`), kept so that
every result in `HISTORY_COST_SWEEP.md` can be rebuilt. The current programs:

| Program | What it does |
|---|---|
| `gr/pd39route` | **The router**, for all three contests: ISPD 2008 (`.gr`) and ISPD 2024 / 2025 (`.cap` + `.net`). Source: `pd39route.cpp`, `pd39_run.h`, `pd39_selftest.h`, `pd39_settings.h`. |
| `gr/timing_loop.py` | The ISPD 2025 flow: routes several timing strengths, scores each with the contest's OpenROAD and evaluator, refines, keeps the best |
| `gr/elmore_rows.py` | Per-net Elmore delay rows for the 2025 flow's last stage (called by `timing_loop.py`) |
| `gr/reorder_portfolio.py` | The ISPD 2008 flow for a design that does not reach zero overflow: several reorderings, the best kept |
| `gr/gr_verify` | ISPD 2008 verifier: legality, overflow, wirelength (cross-checked against `eval2008.pl`) |
| `gr/g24_verify` | ISPD 2024 / 2025 verifier |
| `gr/input_check` | Reads a design without routing it, and reports every problem in it |
| `gr/run_batch.sh`, `run_checked.sh`, `run_checked24.sh`, `run_tracked.sh` | Batch runner: each job is routed under `/usr/bin/time`, verified and scored, and leaves a one-line summary |
| `gr/cutbound`, `gr/lpbound` | Lower bounds on overflow in a region (diagnostics, Parts 65 and 91) |
| `gr/settings_check.py`, `gr/settings_doc.py`, `gr/e2e_settings.py` | The settings table against the source, `SETTINGS.md` regenerated, every setting run end to end (CI) |
| `openroad/setup.sh` | Rebuilds the ISPD 2025 contest's OpenROAD fork with this project's fixes (`openroad/README.md`) |
| `tools/export_conversations.py` | Writes the Claude Code session transcripts as readable Markdown into `conversations/` (local only) |

## Build

Everything builds with a C++17 compiler and nothing else. These are the same lines CI uses
(`.github/workflows/build-and-validate.yml`):

```bash
cd gr
g++ -std=c++17 -O2 -Wall -Wextra -ffp-contract=off -pthread -o pd39route pd39route.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o gr_verify gr_verify.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o g24_verify g24_verify.cpp
g++ -std=c++17 -O2 -Wall -Wextra -o input_check input_check.cpp
./pd39route --selftest
```

- **`-ffp-contract=off`** keeps routes identical between macOS and Linux (Part 123).
- **Fresh binaries:** on macOS, write a new binary to a fresh file (`rm` the old one first). Copying over a binary in
  place leaves a stale code signature, and later launches are killed.
- **The Metal (GPU) pattern stage** is optional, with a separate build (`GPU_BACKEND_PLAN.md`). The plain `g++` build
  is the CPU reference, and any GPU result must match it bit for bit.

## Benchmarks

None of the benchmark files are in the repository: they are third-party files with their own terms.

| Contest | Where from | Put in |
|---|---|---|
| ISPD 2008 | ispd.cc's benchmark pages, `<design>.gr.gz` (CI's URLs: adaptec1 from `https://www.ispd.cc/contests/07/rcontest/benchmark/`, bigblue1 from `https://www.ispd.cc/contests/08/benchmark/`; which page serves each of the other designs was not recorded) | `gr/bench/<design>.gr` (gunzip) |
| ISPD 2008 evaluator | `sh gr/fetch_eval2008.sh` (downloads `eval2008.pl` from ispd.cc and checks its SHA-256) | `gr/eval2008/` |
| ISPD 2024 | `gr/bench/ispd24/download.sh` (the contest's Google Drive folder, `download.list`) | `gr/bench/ispd24/<design>.cap / .net` |
| ISPD 2024 evaluator | `evaluator.cpp` from [cuhk-eda/InstantGR](https://github.com/cuhk-eda/InstantGR) `run/`, built locally, BSD-3 (Part 103) | `gr/bench/ispd24/official/evaluator` |
| ISPD 2025 | The contest's benchmark release (`ISPD2025_benchmarks.zip`; [contest page](https://www.ispd.cc/contests/25/index.html)). The exact download link was not recorded. | `gr/bench/ispd25/ISPD2025_benchmarks/{visible,blind}/<design>/` |
| ISPD 2025 scoring | The contest's OpenROAD fork (`openroad/README.md`); the congestion evaluator and NanGate45 from [liangrj2014/ISPD25_contest](https://github.com/liangrj2014/ISPD25_contest) | `~/Code/OpenROAD_ISPD25`, `~/Code/GlobalRouterRuns/ispd25eval` |

**Downloads:** the large 2024 / 2025 designs are several GB unpacked. On this project, downloads and installs are
made only with the user's approval (CLAUDE.md).

## Input and output formats

- **ISPD 2008 `.gr`:** grid, per-direction capacities, minimum widths and spacings, nets with pin coordinates, and
  capacity adjustments. CLAUDE.md's "ISPD `.gr` specification details" lists the easy-to-miss rules:
  - capacity is space, not tracks;
  - nets over 1000 pins need not be routed;
  - pins are given in real coordinates, but adjustments use tile indices.

  The readers (`gr/gr_io.h`) stop on anything out of spec and name the file and line. `input_check` reports every
  problem before stopping.
- **ISPD 2008 solution (`.route`):**
  - each net starts with a header line, `<name> <id>` (the two fields `pd39route` writes, which `eval2008.pl`
    accepts on every checked run);
  - then one segment per line, `(x1,y1,l1)-(x2,y2,l2)`, in real coordinates, with 1-based layers;
  - then `!`.

  A via is a segment between two layers at one point, and costs one unit of wirelength per layer it crosses.
- **ISPD 2024 / 2025 `.cap` + `.net`:** the GCell grid with per-layer capacities, and nets with access points.
  `ISPD24_FORMAT.md` documents the format, taking the contest evaluator as the source of truth. 2025 adds per-pin
  slack in the `.net` file, plus a `.def` and `.sdc` for OpenROAD.
- **ISPD 2024 / 2025 solution:** GCell segments per net, the format `g24_verify` and the contest evaluator read.

## Settings

All of the router's behavior beyond its file arguments is set by environment variables: 188 in all, listed with
type, default and meaning in [`SETTINGS.md`](SETTINGS.md), and printed by `./pd39route --settings`. The standard
flows are sets of them, kept in settings files:

| File | Adds |
|---|---|
| `standard.env` | The base pipeline: negotiation loop, layer assignment, refinement, region stage, reclaim (2008 Parts 71-90) |
| `fast08.env` | 2008 speed (8 threads, batched slow tier, pattern stage on the dependency graph) and the late-stage portfolio. It also leaves the exempt giant nets unrouted (Part 212). |
| `speed08.env` | 2008 speed layer: region-stage box cap, plateau stops, batched region stage, two-ended search (Parts 140-202) |
| `standard24.env` | ISPD 2024 / 2025 cost model: the contest score in 2D routing and layer assignment, access points, via demand |
| `fast24.env` | ISPD 2024 / 2025 runtime: batched, spread slow tier; pattern stage on the dependency graph; shared 3D-pass graph |
| `standard25.env` | ISPD 2025 timing and capacitance bias (layer R and C from NanGate45's `setRC.tcl`) |

**Checked before anything runs** (`gr/pd39_run.h`, Part 216). Each of these stops the run with every problem listed,
before any routing:
- an unknown option;
- the wrong number of arguments;
- an input that cannot be read, or an output that cannot be written;
- a variable named like a setting but not in the table (it names the nearest real one);
- a value of the wrong type;
- a Text word outside its set;
- a problem in the input files.

**Loading a settings file.** `run_batch.sh` job lines take `@file.env`. From a shell, load the files line by line:

```bash
env $(grep -h -v -e '^#' -e '^$' standard.env fast08.env) ./pd39route in.gr out.route
```

Never source a settings file with `.`: `fast08.env`'s `PD39ROUTE_PORTFOLIO` holds `;`, which the shell would treat
as a command separator. No value in the standard files contains a space, so the word-splitting above is safe for
them (checked 2026-11-26).

## The three standard flows

### ISPD 2008

One run, then both checks:

```bash
cd gr
env $(grep -h -v -e '^#' -e '^$' standard.env fast08.env speed08.env) \
    ./pd39route bench/adaptec1.capo70.3d.35.50.90.gr adaptec1.route
./gr_verify bench/adaptec1.capo70.3d.35.50.90.gr adaptec1.route
perl eval2008/eval2008.pl bench/adaptec1.capo70.3d.35.50.90.gr adaptec1.route
```

**Batch form** (verification and the summary are done by the scripts; at most 3 jobs at once):

```bash
cd gr
cat > jobs.txt <<'EOF'
std_adaptec1 bench/adaptec1.capo70.3d.35.50.90.gr ./pd39route @standard.env @fast08.env @speed08.env
EOF
./run_batch.sh jobs.txt out/
```

Each job leaves `out/<name>.summary`, giving the status (`OK` means legal, and `gr_verify` agrees with
`eval2008.pl`), the overflow, the maximum overflow and the wirelength. Each job also leaves the route and its time
and memory logs.

**A design that does not reach zero overflow** (newblue3, newblue4, newblue7, bigblue4 since Part 212):

```bash
python3 gr/reorder_portfolio.py gr/bench/newblue4.mpl50.3d.40.10.95.gr ~/work/nb4 --out ~/work/nb4/best.route
```

It routes once. Only if overflow remains does it run five reorderings of the slow tier, and it keeps the best legal
one by the contest's order (`--help` lists the options).

**Results and runtimes:** all sixteen designs are in CLAUDE.md and `HISTORY_COST_SWEEP.md` Part 75 onward. Twelve
reach zero overflow, mostly in minutes. The rest take hours.

### ISPD 2024

```bash
cd gr
env $(grep -h -v -e '^#' -e '^$' standard.env standard24.env fast24.env) PD37ROUTE_THREADS=8 \
    ./pd39route bench/ispd24/mempool_tile_rank.cap bench/ispd24/mempool_tile_rank.net mempool_tile_rank.sol
./g24_verify bench/ispd24/mempool_tile_rank.cap bench/ispd24/mempool_tile_rank.net mempool_tile_rank.sol
bench/ispd24/official/evaluator bench/ispd24/mempool_tile_rank.cap bench/ispd24/mempool_tile_rank.net mempool_tile_rank.sol
```

- **Judge results by the official evaluator's total cost.** The router's own printed score reads 0.31% low on
  mempool_tile_rank, for a reason not yet found (Part 216).
- **Batch form:** `run_batch.sh` job lines take a `.cap` path; the `.net` must sit beside it.

### ISPD 2025

The standard flow puts the contest's timing engine in the loop:

```bash
python3 gr/timing_loop.py gr/bench/ispd25/ISPD2025_benchmarks/blind/ariane ~/work/ariane --out ~/work/ariane/best.route
```

**What it needs:**
- `DESIGN_DIR` holds `<name>.cap`, `.net`, `.def` and `.sdc`, and the folder is named after the design.
- `WORK_DIR` must not contain a space, because OpenROAD reads its scripts unquoted.
- The contest's OpenROAD at `~/Code/OpenROAD_ISPD25/build/src/openroad`.
- The evaluator and NanGate45 in `~/Code/GlobalRouterRuns/ispd25eval`.

`timing_loop.py` checks all of these, and its own options, before it starts.

**What it does** (stage by stage, with the measurements, in the script's header and in Parts 133-217):
1. Routes the design unbiased.
2. Routes the ladder: three timing strengths, with two class sources (routed slack and input slack).
3. Tries the power term at the best strength.
4. Refines: OpenROAD's measured slack re-classes the nets, and the design is rerouted, round after round.
5. Reroutes the best with per-net Elmore delay rows.

Every candidate is scored by the contest evaluator and OpenROAD, and the best by the contest score is kept.

| Option | Default | Effect |
|---|---|---|
| `--out FILE` | none | write the best route here |
| `--classes both\|routed\|input` | `both` | which slack sources the ladder classes nets from |
| `--k3 a,b,c` | `20,180,540` | the ladder's top-class timing strengths (classes get k3/4, k3/2, k3) |
| `--refine N` | `48` | most refinement rounds; it stops sooner when rounds stop gaining (`0` = off) |
| `--patience P` | `3` | refinement stops after P + 1 rounds in a row without a new best (`0` = the old first-miss rule) |
| `--elmore K1,K2,...\|0` | `35,70,140` (and `280,560` when the best's strength is 540) | the Elmore stage's strengths (`0` = off) |
| `--endpoints N\|measured` | the contest's published count for blind designs, OpenROAD's count otherwise | the endpoint count the TNS term is divided by when candidates are ranked |
| `--reuse DIR` | none | refine an earlier loop's best instead of rerunning stages 1-3 (`--refine 0`: only the Elmore stage) |
| `--resume` | off | restart a stopped loop in the same `WORK_DIR`. It reruns nothing already measured (`scores.jsonl`). |
| `--lean` | off | compress every route that can no longer win. Needed for the mempool_cluster designs (routes about 11 GB each). |
| `--two-sessions` | off | score and measure slack in two OpenROAD sessions per round (the pre-2026-11-24 way) |
| `--router PATH` | `gr/pd39route` | another router build |

**Costs:**
- **Runtime:** from about 15 minutes (ariane) to about 19 hours (blind mempool_group), and 20-30 hours for
  mempool_cluster.
- **Memory:** OpenROAD sessions for the mempool designs peak at about 30 GB (mempool_group) and 80-113 GB
  (mempool_cluster) of memory footprint. On a 24 GB Mac most of that is swap, so keep disk space free.
- **Snapshots:** Time Machine local snapshots taken during a `--lean` run keep the routes it compresses (Part 217).
  Pause automatic backups for long runs, or watch `tmutil listlocalsnapshots /`.

**Without OpenROAD**, the one-route fallback is the router with `@standard.env @standard24.env @fast24.env
@standard25.env`.

## Checks before committing a router change

- `./pd39route --selftest`: the self-test, including deliberately broken inputs.
- `python3 gr/e2e_settings.py pd39route` (about a minute): every setting run end to end, looking for evidence that
  its mechanism acted.
- `python3 gr/settings_check.py`:
  - the settings table matches the source;
  - every settings file uses known names;
  - `kDefaults` matches `kTable`;
  - `SETTINGS.md` is current.

  After changing the table, run `python3 gr/settings_doc.py` to rewrite `SETTINGS.md`.
- A change that should not move routes is checked byte for byte: route a design with the old and new builds and
  compare the files.
- CI runs the builds, self-tests, analyzers, sanitizers and checks on every push (`QUALITY_CHECKS.md`).

## Where results live

- **`HISTORY_COST_SWEEP.md`:** every experiment, numbered by Part. CLAUDE.md summarizes the standards and best
  results.
- **`recaps/`:** illustrated summaries (Recaps 1-9, reviews, research roadmaps).
- **`~/Code/GlobalRouterRuns`** (local, not on GitHub): every run's routes, logs and score tables, and the frozen
  copies of the flow used for each Part's runs.
