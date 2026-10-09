# Done 2026-10-09: "Global Router Speed and Theory", second look

Published at the same link (version 4); local copy
`recaps/speed-theory-2026-10-09.html`. The brief below is kept for the record.


Requested by the user 2026-10-08. **Trigger:** when the runtime work is
complete -- the lean 2024 pipeline and pd36route's exact cuts (Part 115),
any further step-2 work, and the threads + GPU work (step 3).

## Baseline

- Published v1: https://claude.ai/artifact/4vLqP5qoUvbHf3CCTh5o2Y (later
  versions only corrected the contest runtime line and added a link).
- Frozen copy: `recaps/speed-theory-v1-2026-10-08.html`.

## What the new page must contain

1. A fresh look at where the time goes and what remains, measured on the
   then-current router (stage times, cluster designs included).
2. A comparison with v1: which of its claims and recommendations clearly
   pointed to where things landed, which missed, and what it did not see.
   Known so far:
   - v1 named the two plateaued loops (LA rounds, WL recovery) as the
     time; correct -- but it proposed stop rules, and the real answer was
     dropping both stages (WL recovery *worsened* the 2024 score; LA
     rounds gained ~0.01%).
   - v1 missed the pattern stage's per-batch full-grid cost refresh (found
     only by reading the code during Part 115) and the 2008 zero-overflow
     pipeline being the wrong objective for 2024 (found by reading
     InstantGR's flow).
   - v1's estimate "stop rules alone: 6-8 h on the cluster" vs what the
     lean pipeline + pd36route actually measure.
3. What approach would have surfaced these earlier, and what analysis
   discipline should be kept for future algorithm work (see the memory
   note `performance-analysis-discipline` and Part 115).

Publish as a new version of the same artifact or a v2 page (ask the user),
with the v1-vs-now comparison as its own section.
