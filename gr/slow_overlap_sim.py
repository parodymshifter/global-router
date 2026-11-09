#!/usr/bin/env python3
# Part 192 (2026-11-09): the ceiling of overlapping the slow tier's batches, from a PD39ROUTE_SLOW_TASKS dump.
#
# The batched slow tier (PD38ROUTE_SLOW_BATCH) searches a batch's nets on T workers against the batch-start usage,
# then commits them in net order; the next batch starts only when every search of this one is done. This replays
# each dumped round's batches -- each task's measured duration, in the order the workers took them -- under:
#   barrier      the current rule (a batch's searches start after the previous batch committed); its total is
#                checked against the measured search wall time of each batch
#   lookahead L  a batch's searches may start once the batch L+1 before it has committed (commits stay in net
#                order, after the batch's own searches and the previous commit); L = 1 lets one batch overlap the
#                previous one's stragglers. Its searches then see usage without the commits of the L batches
#                before it -- the "stale" share below, a proxy for the extra conflicts (carried nets) it would cause
#   no gates     any task as soon as a worker is free (list scheduling over the whole round)
#   bound        max(task time / T, longest task) per round
# Commit time is not in the dump (it is ~1% of the stage) and is taken as zero in every model. Durations were
# measured with the machine's other threads idle part of the time; fuller overlap means more concurrent searches
# and, at ~30% memory stalls (Part 192's Instruments), somewhat slower ones -- so these are ceilings.
#
# usage: slow_overlap_sim.py DUMP [THREADS=8] [LOOKAHEADS=1,2,4]
import heapq
import sys
from collections import defaultdict


def read(path):
    rounds = defaultdict(list)  # round -> [(wall, [durations s])]
    stats = defaultdict(lambda: {'c': 0, 'u': 0, 'x': 0, 'xs': 0.0})
    for line in open(path):
        if not line.startswith('B '):
            continue
        f = line.split()
        r, n, wall = int(f[1]), int(f[3]), float(f[4])
        durs = []
        for t in f[5:5 + n]:
            d, _, o = t.split(':')
            d = float(d) * 1e-6
            durs.append(d)
            stats[r][o] += 1
            if o == 'x':
                stats[r]['xs'] += d
        rounds[r].append((wall, durs))
    return rounds, stats


def simulate(batches, T, look):
    """look = None: no gates; look = 0: barrier; look = L: batch j starts after batch j-L-1 committed.
    Returns (makespan, stale task time, total task time)."""
    free = [0.0] * T
    heapq.heapify(free)
    commit = []  # commit time of each batch
    stale = total = 0.0
    for j, (_, durs) in enumerate(batches):
        gate = 0.0
        if look is not None and j - look - 1 >= 0:
            gate = commit[j - look - 1]
        prev = commit[j - 1] if j > 0 else 0.0
        fin = 0.0
        for d in durs:
            t0 = max(heapq.heappop(free), gate)
            heapq.heappush(free, t0 + d)
            fin = max(fin, t0 + d)
            total += d
            if t0 < prev:  # searched before the previous batch committed: misses its commits
                stale += d
        commit.append(max(fin, prev))
    return (commit[-1] if commit else 0.0), stale, total


def main():
    path = sys.argv[1]
    T = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    looks = [int(x) for x in (sys.argv[3] if len(sys.argv) > 3 else '1,2,4').split(',')]
    rounds, stats = read(path)
    hdr = ['round', 'batches', 'tasks', 'measured', 'barrier'] + [f'L{l}' for l in looks] + ['no gates', 'bound',
           'longest task', 'carried share of task time'] + [f'stale L{l}' for l in looks]
    print('\t'.join(hdr))
    tot = defaultdict(float)
    for r in sorted(rounds):
        b = rounds[r]
        meas = sum(w for w, _ in b)
        bar, _, task = simulate(b, T, 0)
        row = [r, len(b), sum(len(d) for _, d in b), meas, bar]
        tot['measured'] += meas
        tot['barrier'] += bar
        stales = []
        for l in looks:
            m, st, _ = simulate(b, T, l)
            row.append(m)
            tot[f'L{l}'] += m
            stales.append(st / task if task else 0.0)
        ng, _, _ = simulate(b, T, None)
        longest = max((max(d) for _, d in b if d), default=0.0)
        bound = max(task / T, longest)
        row += [ng, bound, longest, stats[r]['xs'] / task if task else 0.0] + stales
        tot['no gates'] += ng
        tot['bound'] += bound
        tot['task'] += task
        print('\t'.join(f'{x:.3f}' if isinstance(x, float) else str(x) for x in row))
    print()
    print(f'all dumped rounds, {T} threads: measured {tot["measured"]:.1f} s, barrier model {tot["barrier"]:.1f} s '
          f'({100 * (tot["barrier"] / tot["measured"] - 1):+.1f}% vs measured), task time {tot["task"]:.1f} s')
    for k in [f'L{l}' for l in looks] + ['no gates', 'bound']:
        print(f'  {k:9s} {tot[k]:9.1f} s  {tot["barrier"] / tot[k]:.2f}x vs barrier  '
              f'({100 * (1 - tot[k] / tot["barrier"]):.1f}% less)')


if __name__ == '__main__':
    main()
