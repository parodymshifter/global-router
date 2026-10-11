# Quality checks

These checks look for bugs and slow spots in the router as it grows.
They run on GitHub, and you can run the same checks on your own Mac.

## What runs on GitHub

The file `.github/workflows/quality-checks.yml` runs four jobs.

| Job | What it does | When it runs |
|---|---|---|
| Static analysis | clang-tidy and cppcheck read the code and list likely mistakes | every push, every pull request, nightly |
| Sanitizers | Builds the router so it stops on memory errors and bad math, then runs the self-tests and a routed test design | every push, every pull request, nightly |
| Valgrind | A slower and deeper memory check | nightly and on request |
| Timing | Routes a fixed test design three times and compares time, memory, overflow and wirelength with the last good run on main | every push, every pull request, nightly |

The file `.github/workflows/codeql.yml` adds GitHub's CodeQL scanner.
CodeQL is free for public repositories, so it skips itself until you make this one public.

Each job writes a short summary on the run page.
Longer reports are saved as downloads on the same page.

## Two switches to flip later

At the top of `quality-checks.yml` you will find two settings.

- `STATIC_GATE`: set it to `"true"` when you have cleaned up the findings. Then cppcheck errors will fail the job.
- `PERF_GATE`: set it to `"true"` after you have watched a few runs. Then a big jump in time or memory will fail the job. GitHub's machines vary in speed, so give it a few runs first.

## Moving to a newer router

Change `ROUTER: pd38route` at the top of `quality-checks.yml`.
Also change the two file names in `codeql.yml`.

## About "[skip ci]"

A commit message with `[skip ci]` stops the checks for that commit.
Many recent commits use it.
The nightly run ignores the tag, so those commits still get checked within a day.
For changes to routing code, it is better to leave the tag off.

## Run the checks on your Mac

Timing and quality canary (about two minutes):

    python3 perf/run_perf.py --out /tmp/perf --repeat 3
    python3 perf/compare.py /tmp/perf/results.json

Run it again after a change, then compare the two results:

    cp /tmp/perf/results.json /tmp/perf/before.json
    python3 perf/run_perf.py --out /tmp/perf --repeat 3
    python3 perf/compare.py /tmp/perf/results.json /tmp/perf/before.json

Sanitizers:

    cd gr
    g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o pd38route.san pd38route.cpp
    ./pd38route.san --selftest

Static analysis (first run `brew install llvm cppcheck`):

    cd gr
    $(brew --prefix llvm)/bin/clang-tidy pd38route.cpp -quiet -checks='-*,bugprone-*,clang-analyzer-*,performance-*' -- -std=c++17
    cppcheck --enable=warning,performance,portability --std=c++17 --max-configs=1 pd38route.cpp

Valgrind does not work well on current Macs.
Use the GitHub job for that one.

## The test design

`perf/gen_synth.py` makes a fixed test design with 3500 nets on a 90 by 90 grid.
It gives the same file every time, on every machine.
So if the overflow or wirelength changes between runs, the router changed.
If only the time changes, the router got faster or slower, or the machine did.
