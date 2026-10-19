#!/usr/bin/env python3
"""atlas_spectrum_eval.py -- does the congestion spectrum (routability.cpp's spec_* / xs_*, Part 139) predict
routing outcomes beyond the Atlas's existing pre-routing measures? Standard library only.

  python3 gr/atlas_spectrum_eval.py ATLAS.json FEATURES.txt [PERMUTATIONS]

ATLAS.json is the Routing Difficulty Atlas's data (difficulty_data.py output: measures + outcomes per design);
FEATURES.txt is routability's output with the spectrum. For each contest set and outcome:
  1. Spearman rank correlation of every spectral measure with the outcome, next to the strongest existing measures;
  2. the Atlas's own fit (recaps/difficulty-atlas.template.html, drawFit): ridge least squares on standardized
     measures, penalty 2, outcome transformed as on the page, scored by leave-one-out R^2 (each design predicted by
     a model refitted and re-standardized without it). Baseline: the two existing measures with the largest
     |Spearman| for that outcome in that set; then the baseline plus each spectral measure in turn;
  3. a permutation test of the best gain: every spectral measure shuffled across designs, the best of the ten gains
     recomputed, PERMUTATIONS times (default 500) -- p = the share of shuffles whose best gain is at least the
     observed best gain (this accounts for picking the best of ten).
"""
import json
import math
import random
import sys

SPEC = ['spec_len', 'spec_rel', 'spec_large', 'spec_small', 'spec_slope',
        'xs_len', 'xs_rel', 'xs_large', 'xs_small', 'xs_slope']
LOG_SPEC = {'spec_len', 'xs_len'}  # wavelengths: compared on a log scale
# The Atlas's measures and their transforms (template M list); every measure the page offers.
EXISTING = {'nets_all': 'log', 'pins': 'log', 'tiles': 'log', 'layers': '', 'tracks_h': '', 'tracks_v': '',
            'pin_density': '', 'pin_per_htrack': '', 'pin_per_vtrack': '', 'row_cv': '', 'col_cv': '',
            'pv_mean': '', 'pv_p99': '', 'pv_max': 'log', 'ppn_mean': '', 'ppn_median': '', 'ppn_p90': '',
            'ppn_p99': '', 'ppn_max': 'log', 'ppn_skew': 'log', 'ppn_share2': '', 'ppn_share10': '',
            'big_share': '', 'regions': 'log1p', 'region_top': '', 'reg4_cv': '', 'reg8_cv': '', 'reg16_cv': '',
            'reg16_maxmean': '', 'reg16_gini': '', 'reg4_span2': '', 'reg8_span2': '', 'reg16_span2': '',
            'rsmt_per_net': '', 'rsmt_per_gcell': '', 'rsmt_per_track': '', 'util': '', 'rudy_p99': '',
            'rudy_over': '', 'rudy_excess': 'log1p', 'esc_max': '', 'esc_p999': '', 'cut3_max': '',
            'cut3_over1': 'log1p', 'long_share': ''}
OUTCOMES = {'overflow': 'log1p', 'over_share': 'log1p', 'of_gcell': 'log', 'cpu_mstep': 'log', 'detour': ''}
SETS = [('ISPD 2008', lambda s: s == 'ispd08'), ('ISPD 2024', lambda s: s == 'ispd24'),
        ('ISPD 2025', lambda s: s.startswith('ispd25'))]


def tf(v, kind):
    if v is None or not isinstance(v, (int, float)) or not math.isfinite(v):
        return None
    if kind == 'log':
        return math.log10(v) if v > 0 else None
    if kind == 'log1p':
        return math.log10(1 + max(0.0, v))
    return float(v)


def ranks(a):
    idx = sorted(range(len(a)), key=lambda i: a[i])
    r = [0.0] * len(a)
    i = 0
    while i < len(idx):
        j = i
        while j + 1 < len(idx) and a[idx[j + 1]] == a[idx[i]]:
            j += 1
        for k in range(i, j + 1):
            r[idx[k]] = (i + j) / 2 + 1
        i = j + 1
    return r


def pearson(x, y):
    n = len(x)
    if n < 3:
        return None
    mx, my = sum(x) / n, sum(y) / n
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else None


def spearman(x, y):
    return pearson(ranks(x), ranks(y))


def solve(A, b):
    n = len(A)
    M = [row[:] + [b[i]] for i, row in enumerate(A)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(M[r][c]))
        if abs(M[p][c]) < 1e-12:
            return None
        M[c], M[p] = M[p], M[c]
        for r in range(n):
            if r != c:
                f = M[r][c] / M[c][c]
                M[r] = [a - f * bb for a, bb in zip(M[r], M[c])]
    return [M[i][n] / M[i][i] for i in range(n)]


def ridge(X, y, lam):
    n, p = len(X), len(X[0])
    my = sum(y) / n
    A = [[sum(x[i] * x[j] for x in X) + (lam if i == j else 0) for j in range(p)] for i in range(p)]
    b = [sum(x[i] * (y[r] - my) for r, x in enumerate(X)) for i in range(p)]
    beta = solve(A, b)
    return (my, beta) if beta else None


def loo_q2(cols, y, lam=2.0):
    """The page's leave-one-out R^2: cols are the measures' (transformed) values, one list per measure."""
    n = len(y)
    my = sum(y) / n
    sst = sum((v - my) ** 2 for v in y)
    if sst <= 0:
        return None
    press = 0.0
    for i in range(n):
        keep = [j for j in range(n) if j != i]
        means = [sum(c[j] for j in keep) / len(keep) for c in cols]
        sds = [math.sqrt(sum((c[j] - m) ** 2 for j in keep) / len(keep)) or 1.0 for c, m in zip(cols, means)]
        Xi = [[(c[j] - m) / s for c, m, s in zip(cols, means, sds)] for j in keep]
        f = ridge(Xi, [y[j] for j in keep], lam)
        p = f[0] + sum((c[i] - m) / s * b for c, m, s, b in zip(cols, means, sds, f[1])) if f else my
        press += (y[i] - p) ** 2
    return 1 - press / sst


def main():
    atlas = json.load(open(sys.argv[1]))
    nperm = int(sys.argv[3]) if len(sys.argv) > 3 else 500
    spec = {}
    for line in open(sys.argv[2]):
        if not line.startswith('set='):
            continue
        d = {}
        for tok in line.split():
            if '=' in tok:
                k, v = tok.split('=', 1)
                try:
                    d[k] = float(v)
                except ValueError:
                    d[k] = v
        spec[(d['set'], d['design'])] = d
    rows = []
    for r in atlas:
        s = spec.get((r['set'], r['design']))
        if s:
            rows.append(dict(r, **{k: s[k] for k in SPEC + ['spec_scale'] if k in s}))
    rng = random.Random(139)
    for sname, inset in SETS:
        sub = [r for r in rows if inset(r['set'])]
        for oname, otf in OUTCOMES.items():
            ys = [tf(r['outcome'].get(oname), otf) for r in sub]
            ok = [i for i, v in enumerate(ys) if v is not None]
            if len(ok) < 8 or len(set(ys[i] for i in ok)) < 4:
                continue
            # 1. Spearman, existing and spectral
            def rho_of(key, idx):
                pairs = [(sub[i].get(key), ys[i]) for i in idx]
                pairs = [(a, b) for a, b in pairs if isinstance(a, (int, float)) and math.isfinite(a)]
                return (spearman([a for a, _ in pairs], [b for _, b in pairs]), len(pairs)) if len(pairs) >= 6 else (None, 0)
            ex = [(k, rho_of(k, ok)) for k in EXISTING]
            ex = [(k, r) for k, (r, n) in ex if r is not None]
            ex.sort(key=lambda t: -abs(t[1]))
            sp = [(k,) + rho_of(k, ok) for k in SPEC]
            print(f'\n== {sname}: {oname} ({len(ok)} designs)')
            print('  strongest existing: ' + ', '.join(f'{k} {r:+.2f}' for k, r in ex[:4]))
            print('  spectral:           ' + ', '.join(f'{k} {r:+.2f}' + ('' if n == len(ok) else f' (n {n})')
                                                     for k, r, n in sp if r is not None))
            # 2. LOO R^2: the baseline (top two existing) and + each spectral measure, on designs with all values
            base = [k for k, _ in ex[:2]]
            def col(key, idx, kind):
                return [tf(sub[i].get(key), kind) for i in idx]
            def q2_with(extra, idx, override=None):
                cols = [col(k, idx, EXISTING[k]) for k in base]
                if extra:
                    c = override if override is not None else col(extra, idx, 'log' if extra in LOG_SPEC else '')
                    cols.append(c)
                return loo_q2(cols, [ys[i] for i in idx])
            gains = []
            for k in SPEC:
                idx = [i for i in ok if all(tf(sub[i].get(b), EXISTING[b]) is not None for b in base)
                       and tf(sub[i].get(k), 'log' if k in LOG_SPEC else '') is not None]
                if len(idx) < len(base) + 4:
                    continue
                q0, q1 = q2_with(None, idx), q2_with(k, idx)
                if q0 is None or q1 is None:
                    continue
                gains.append((k, q0, q1, idx))
            if not gains:
                continue
            q0all = gains[0][1]
            line = '  LOO R^2: baseline [' + ', '.join(base) + f'] {q0all:.2f}; + ' + ', '.join(
                f'{k} {q1:.2f}' for k, _, q1, _ in gains)
            print(line)
            best = max(gains, key=lambda g: g[2] - g[1])
            obs = best[2] - best[1]
            # 3. permutation test of the best gain (each spectral measure shuffled within its own designs)
            hit = 0
            for _ in range(nperm):
                mx = -1e9
                for k, q0, _, idx in gains:
                    c = col(k, idx, 'log' if k in LOG_SPEC else '')
                    rng.shuffle(c)
                    q1 = q2_with(k, idx, c)
                    if q1 is not None:
                        mx = max(mx, q1 - q0)
                hit += mx >= obs - 1e-12
            print(f'  best gain: {best[0]} {obs:+.2f} (LOO R^2 {best[1]:.2f} -> {best[2]:.2f}); permutation p = '
                  f'{(hit + 1) / (nperm + 1):.3f} ({nperm} shuffles, best of {len(gains)})')


if __name__ == '__main__':
    main()
