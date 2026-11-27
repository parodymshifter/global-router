# Global Router

A research global router for integrated circuits, measured on the ISPD 2008, 2024 and 2025 contest benchmarks. One
program, `gr/pd39route`, routes all three formats. Each net solves a minimum-cost tree under its own shaped costs,
and shared resources carry prices that rise with use (CLAUDE.md, "What this is").

**Results** (all legal, checked by the contests' own evaluators; details in CLAUDE.md and
`HISTORY_COST_SWEEP.md`):

| Contest | Result |
|---|---|
| ISPD 2008 | 12 of 16 designs at zero overflow (Part 212); newblue7 56 against at least 54 proven unavoidable; newblue3, newblue4 and bigblue4 above zero |
| ISPD 2024 | all six hidden designs below the contest's best raw score, 12-38x faster |
| ISPD 2025 | blind six contest score 5.6707, against 8.0604 for 1st place (lower is better) |

| Start here | For |
|---|---|
| [`USAGE.md`](USAGE.md) | Building, benchmarks, the three standard flows with exact commands, input and output formats, checks |
| [`SETTINGS.md`](SETTINGS.md) | Every router setting with its kind, default, meaning and standard value (generated from the router's table) |
| [`CLAUDE.md`](CLAUDE.md) | The project's design decisions and current standards, as a dated record |
| [`HISTORY_COST_SWEEP.md`](HISTORY_COST_SWEEP.md) | Every experiment, numbered by Part, with its measurements |
| [`recaps/`](recaps/) | Illustrated summaries, reviews and research roadmaps |
| [`openroad/`](openroad/README.md) | Rebuilding the ISPD 2025 contest's OpenROAD with this project's fixes |
| [`QUALITY_CHECKS.md`](QUALITY_CHECKS.md), [`PROFILING.md`](PROFILING.md) | CI checks, sanitizers, Instruments profiling |

**Licences:** code is Apache-2.0 (`LICENSE`), documents are CC BY 4.0 (`LICENSE-DOCS`), and `NOTICE` says which is
which and lists the third-party pieces.
