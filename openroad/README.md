# The ISPD 2025 contest's OpenROAD, as built here

The ISPD 2025 flow (`gr/timing_loop.py`) scores timing and power with the contest's own OpenROAD fork,
[liangrj2014/OpenROAD_ISPD25](https://github.com/liangrj2014/OpenROAD_ISPD25). Upstream OpenROAD rejects both our
solutions and the contest's sample (`GRT-0263`; `HISTORY_COST_SWEEP.md`, the evaluation-environment note before
Part 133). The fork is from January 2025 and does not build against 2026 Homebrew without small fixes. Until
2026-11-26 those fixes existed only as uncommitted edits in `~/Code/OpenROAD_ISPD25`. This folder keeps them.

| File | What it is |
|---|---|
| `ispd25_fork_bsd3.patch` | `git diff` of the fork's own files at commit `1a29dad` (16 files). Checked: it applies cleanly to a fresh checkout of that commit and reproduces the built tree. |
| `LICENSE.OpenROAD` | OpenROAD's BSD 3-Clause licence, which covers the patched files |
| `setup.sh` | Clones the fork, pins the commit, applies the patch, makes the one OpenSTA edit, copies the build script |
| `build.sh` | The cmake + ninja build used on 2026-10-12 (no GUI, no Python, no tclreadline) |
| `PATCHES.txt` | The original note on each fix and why |

**OpenSTA** (`src/sta`) is GPL v3, which this Apache-2.0 repository cannot include. Its one change is a single line in
`tcl/Exception.i` (`try { $function }` becomes `try { $action }`, for newer SWIG). `setup.sh` applies it with `sed`
and checks that it took. The same edit applied to a clean copy reproduces the built file exactly.

## Rebuilding

```bash
openroad/setup.sh                       # clones to ~/Code/OpenROAD_ISPD25 (refuses if it exists)
bash ~/Code/OpenROAD_ISPD25/logs/build.sh
```

**Prerequisites:**
- Homebrew packages: `bison flex tcl-tk@8 or-tools qt@5 boost swig fmt eigen spdlog yaml-cpp cmake ninja`. All
  were installed here on 2026-11-26.
- CUDD in `~/.local` (`libcudd.a`).
- `timing_loop.py` expects the binary at `~/Code/OpenROAD_ISPD25/build/src/openroad`.

**Not checked:** a rebuild from these files on another machine. The patch, the OpenSTA edit and the build script
are each checked against this machine's tree, as described above.

## The other 2025 evaluation pieces (not in this repository)

- **The contest's congestion evaluator:** `evaluation/evaluator.cpp` from
  [liangrj2014/ISPD25_contest](https://github.com/liangrj2014/ISPD25_contest), built as the contest builds it
  (assertions on).
- **The NanGate45 libraries and `setRC.tcl`:** from the same contest repository.
- **Where they live here:** both in `~/Code/GlobalRouterRuns/ispd25eval`, which `timing_loop.py` reads (`EVAL_DIR`).
- **The benchmarks:** from the contest's release, unzipped to
  `gr/bench/ispd25/ISPD2025_benchmarks/{visible,blind}/<design>/`.
