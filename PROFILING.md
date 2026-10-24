# Profiling the router with Apple Instruments

How to see where a run's time goes, stage by stage, on this Mac (HISTORY_COST_SWEEP.md Part 126).
The router's own `stage report:` table gives each stage's wall time and objective; Instruments adds
what the table cannot: which functions the time is in, how many threads were actually busy, whether
they ran on performance or efficiency cores, and -- with CPU counters -- whether the cycles were
useful work or stalls.

## Pieces

- `gr/signpost.h` -- stage intervals and round events (os_signpost, Points of Interest track). On
  when `GR_SIGNPOSTS=1` (set in `standard.env`), no-ops otherwise and on other platforms. Routes are
  byte-identical with markers on.
- `gr/instruments_profile.sh` -- records a run with `xcrun xctrace` (launch it, or attach to one
  already running for a window) and writes the report.
- `gr/instruments_report.py` -- reads the trace through `xctrace export` and writes Markdown:
  per stage wall s, CPU s, busy threads (CPU / wall), performance-core share, threads seen, rounds;
  with CPU Counters, the top-down split of cycles; and the functions with the most self time and the
  router functions with the most inclusive time, for the run and for each stage.

## Use

Launch a run under Instruments (settings from the environment, as for any run):

```bash
cd gr && env $(grep -hv '^#' standard.env standard24.env fast24.env | xargs) PD37ROUTE_THREADS=8 ./instruments_profile.sh ~/Code/GlobalRouterRuns/instruments/nvdla -- ./pd39route bench/ispd24/nvdla_rank.cap bench/ispd24/nvdla_rank.net /tmp/nvdla.sol
```

Add `-t "CPU Counters"` for the cycle breakdown ("processing" is back-end stalls, where waiting on
memory shows up; "delivery" front-end; "discarded" mispredicted work). Attach to a long run for a
window instead of recording hours (the router process, not its `run_tracked.sh` wrapper):

```bash
cd gr && ./instruments_profile.sh -a "$(pgrep -f '^\./pd39route' | head -1)" -l 60 ~/Code/GlobalRouterRuns/instruments/window
```

Open `OUT.trace` in Instruments for the timeline. Traces are large -- a 90 s CPU Counters window of an
8-thread run saves 1.2-1.7 GB -- so keep windows short, keep traces in the run archive (not the repository),
and delete them once their reports are written. While recording, xctrace also writes a raw kernel trace
(`$TMPDIR/instruments*.ktrace`, 5-13 GB per 90 s window) that it does not remove; `instruments_profile.sh`
deletes the ones its recording made. Calling `xctrace record` directly leaves them behind: ten of them filled
the disk on 2026-10-24 (Part 154).

## Source lines

`instruments_report.py --lines NAME` adds self time by source line inside functions whose name
contains NAME. It needs line tables: build in two steps so `dsymutil` can read the object file, and
profile that binary:

```bash
cd gr && g++ -std=c++17 -O2 -g -ffp-contract=off -c -o pd39route.dbg.o pd39route.cpp && g++ -pthread -o pd39route.dbg pd39route.dbg.o && dsymutil pd39route.dbg
```

Caution from Part 128: in code that stalls on instruction fetch, samples pile up on call sites and
loop heads, so a hot line is where time is charged, not necessarily what costs it. Check a fix by
timing it on a quiet machine (pause the background runs), not by the profile alone.

## Reading it

- Busy threads well under the thread count in a parallel stage: the stage is waiting (batch
  barriers, a serial step, too few tasks), not computing.
- "threads seen" far above the thread count: threads are being created per batch instead of reused.
- Measure on a quiet machine before comparing two runs' times; the report says nothing about load.
- No privileges are needed: developer mode can stay off.
