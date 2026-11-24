# Quality checks

These checks look for bugs and slow spots in the router as it grows.
They run on GitHub, and you can run the same checks on your own Mac.

## What runs on GitHub

The file `.github/workflows/quality-checks.yml` checks the current router, `pd39route` (the `ROUTER`
setting at its top; since 2026-11-24 -- from 2026-10-14 to then it still checked `pd38route` by mistake).

| Job | What it does | When it runs |
|---|---|---|
| Static analysis | clang-tidy and cppcheck read the code and list likely mistakes | every push, every pull request, nightly |
| Sanitizers | Builds the router so it stops on memory errors and bad math, then runs the self-tests and a routed test design | every push, every pull request, nightly |
| Same routes on every platform | Builds `PORTABLE_ROUTER` on Linux and macOS, routes a test design on each, and compares the routes | every push, every pull request, nightly |
| Settings take effect and match the table | `gr/settings_check.py`: the settings table `gr/pd39_settings.h` lists exactly the settings the router reads, sorted, and covers every `gr/*.env` name; then `gr/e2e_settings.py`: runs the router with each recent setting given through the environment and looks for evidence the mechanism acted, and checks that a misspelled name or a non-numeric value stops the run | every push, every pull request, nightly |
| Valgrind | A slower and deeper memory check | nightly and on request |
| Timing | Routes a fixed test design three times and compares time, memory, overflow and wirelength with the last good run on main | every push, every pull request, nightly |

The file `.github/workflows/codeql.yml` adds GitHub's CodeQL scanner (the repository is public, so it runs).

Every build of `pd39route` uses `-ffp-contract=off -pthread`. Without the first, newer g++ versions fuse
`a*b + c` into one instruction, which changes routes, and the router's self-test fails on purpose.

Each job writes a short summary on the run page.
Longer reports are saved as downloads on the same page.

## Two switches to flip later

At the top of `quality-checks.yml` you will find two settings.

- `STATIC_GATE`: set it to `"true"` when you have cleaned up the findings. Then cppcheck errors will fail the job.
- `PERF_GATE`: set it to `"true"` after you have watched a few runs. Then a big jump in time or memory will fail the job. GitHub's machines vary in speed, so give it a few runs first.

## Moving to a newer router

Change `ROUTER: pd39route` at the top of `quality-checks.yml` (and `PORTABLE_ROUTER` if the new one
must also give the same routes on every platform).
Also change the two file names in `codeql.yml`, and the `--router` default in `perf/run_perf.py`.
Keep `-ffp-contract=off -pthread` on every build line.

## About "[skip ci]"

A commit message with `[skip ci]` stops the checks for that commit.
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
    rm -f pd39route.san
    g++ -std=c++17 -O1 -g -ffp-contract=off -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -o pd39route.san pd39route.cpp
    ./pd39route.san --selftest

Static analysis (installed on the Mac since 2026-11-24 with `brew install llvm cppcheck shellcheck ruff`):

    cd gr
    $(brew --prefix llvm)/bin/clang-tidy pd39route.cpp -quiet -checks='-*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,clang-analyzer-*,performance-*,-performance-avoid-endl' -- -std=c++17
    cppcheck --enable=warning,performance,portability --std=c++17 --max-configs=1 --inline-suppr --suppress=missingIncludeSystem pd39route.cpp
    shellcheck *.sh
    ruff check *.py

Valgrind does not work well on current Macs.
Use the GitHub job for that one.

## The test design

`perf/gen_synth.py` makes a fixed test design with 3500 nets on a 90 by 90 grid.
It gives the same file every time, on every machine.
So if the overflow or wirelength changes between runs, the router changed.
If only the time changes, the router got faster or slower, or the machine did.
