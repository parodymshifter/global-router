# Convergence experiment: does more rounds get to zero overflow?

Recorded 2026-09-05. This answers the open question posed in
`ZERO_OVERFLOW_AGENT_PROMPT.md`: every router in the lineage prints "hit
the round cap" on all three real ISPD benchmarks, never "converged to zero
overflow," using a fixed default round cap (~30-52 rounds depending on
router). Nobody had checked whether overflow keeps dropping with more
rounds, or plateaus. This experiment checked, using `hroute` (one of the
two best-performing routers in the pre-stage-6 lineage) with the round cap
raised roughly 3.3x (52 -> 172) and epsilon re-tuned.

## Results

| benchmark | default-cap baseline | extended (172 rounds) | improvement | final-round behavior |
|---|---|---|---|---|
| adaptec1 | 174186 (52 rounds, hroute's own baseline rerun) | 168644 (172 rounds, finished) | -3.1% | flatlined (168684 -> 168644 over the last 5 rounds, ~8/round) |
| newblue1 | 42642 | 41504 | -2.7% | flatlined (41504 held for 2 rounds before hitting the extended cap) |
| bigblue1 | 302770 | 298682 | -1.35% | flatlined (298710 -> 298682 over the last 5 rounds, ~5-6/round) |

One side experiment (adaptec1, aggressive epsilon retune on the fast tier)
also surfaced a real negative finding: cranking epsilon up to accelerate
convergence instead **destabilized the fast tier** -- overflow climbed
from 338908 to 380018 across rounds 7-21 before a cleanup pass rescued it
back down to 225806. This is exactly the stampede behavior CLAUDE.md's
parallelism section already warns about ("Parallel runs want a smaller
epsilon than sequential ones"); naively increasing epsilon to converge
faster backfires rather than helping.

## Conclusion

**This is a genuine plateau, not a patience/tuning problem.** 3.3x more
rounds buys only 1-3% additional overflow reduction on every benchmark,
with a clearly decelerating (asymptotic) curve on all three, two of which
had already gone essentially flat well before hitting the extended round
cap. The current mechanism -- identical across every router in this
lineage -- is pure current-round multiplicative-weight Lagrangian pricing:

    lambda_e <- lambda_e * exp(epsilon * usage_e / capacity_e)

This formula only reacts to *this round's* usage. It has no memory: if an
edge's usage drops below capacity for even one round (because a net
detoured away from it), lambda stops climbing and can even effectively
lose ground relative to other edges' prices, regardless of how many prior
rounds that edge spent overflowed. That's consistent with the plateau
shape observed -- the mechanism can reach a local equilibrium where
residual overflow persists indefinitely because no edge's price ever gets
"stuck" high enough to force the last remaining contested nets off it.

**Per the mandate, the next concrete step is a structurally different
mechanism**, not further tuning of this one: a permanent, monotonically
non-decreasing historical-cost term per edge, in the style of the
Pathfinder FPGA router (and the negotiated-congestion lineage FastRoute
and NCTU-GR both descend from) -- an edge that has been over capacity for
many rounds should get permanently more expensive regardless of what its
usage looks like in the current round, so it can no longer look
attractive again just because one net happened to move off it briefly.
This is the mechanism this project has never implemented; every router
here uses reactive-only pricing.
