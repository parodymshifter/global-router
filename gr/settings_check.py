#!/usr/bin/env python3
"""settings_check.py -- the router's settings table against its source (HISTORY_COST_SWEEP.md Part 215, 2026-11-24).

  python3 gr/settings_check.py [ROUTER_STEM]     (default pd39route)

pd39_settings.h lists every setting pd39route reads (kTable); the router stops on an environment variable named like a
setting but not in the table, and on reading a name the table lacks. This check keeps the table honest without a run:
  - every setting name quoted in ROUTER.cpp, and every GR_ name its headers read, is in the table, and every table
    name is one of them (a stale entry would let a removed setting pass the guard silently);
  - the table is sorted in byte order with no repeats (its lookup is a binary search);
  - ROUTER.cpp reads nothing through std::getenv directly (every read goes through settings::get);
  - every name set in gr/*.env is in the table (a settings file the router would refuse is caught here, not mid-flow).
Standard library only; exits 1 on any failure.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    stem = sys.argv[1] if len(sys.argv) > 1 else 'pd39route'
    # the router's source: its .cpp and the parts split out of it (Part 216, R3: <stem without 'route'>_run.h, _selftest.h)
    parts = [stem + '.cpp'] + [f for f in (stem.replace('route', '') + '_run.h', stem.replace('route', '') + '_selftest.h')
                               if os.path.exists(os.path.join(HERE, f))]
    src = '\n'.join(open(os.path.join(HERE, f)).read() for f in parts)
    head = open(os.path.join(HERE, 'pd39_settings.h')).read()
    # the registry's own self-test feeds made-up names (misspellings, other prefixes) on purpose
    i = src.find('void testSettingsRegistry() {')
    if i >= 0:
        src = src[:i] + src[src.index('\n}\n', i) + 3:]
    table = re.findall(r'^\s*\{"([A-Z][A-Z0-9_]*)", Kind::(\w+), "', head, re.M)
    names = [n for n, _ in table]
    used = set(re.findall(r'"([A-Z][A-Z0-9]*ROUTE_[A-Z0-9_]+)"', src))
    for f in [f for f in glob.glob(os.path.join(HERE, '*.h')) + glob.glob(os.path.join(HERE, 'gpu', '*')) if os.path.isfile(f)]:
        if os.path.basename(f) != 'pd39_settings.h':
            used |= {n for n in re.findall(r'getenv\("([A-Z0-9_]+)"\)', open(f, errors='replace').read())
                     if n.startswith('GR_')}
    bad = []
    if names != sorted(names, key=lambda s: s.encode()) or len(set(names)) != len(names):
        bad.append('the table is not sorted in byte order, or repeats a name')
    for n in sorted(used - set(names)):
        bad.append(f'{n} is read by {stem} but not in the table')
    for n in sorted(set(names) - used):
        bad.append(f'{n} is in the table but {stem} does not read it')
    for f in parts:
        direct = [i + 1 for i, line in enumerate(open(os.path.join(HERE, f))) if 'std::getenv(' in line]
        if direct:
            bad.append(f'{f} reads std::getenv directly at lines {direct[:10]} (use settings::get)')
    for f in sorted(glob.glob(os.path.join(HERE, '*.env'))):
        for i, line in enumerate(open(f), 1):
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                n = line.split('=', 1)[0]
                if n not in names:
                    bad.append(f'{os.path.basename(f)}:{i}: {n} is not a setting {stem} knows')
    # 2026-11-26: every setting's default (kDefaults, same names in the same order) and SETTINGS.md generated from both
    dnames = re.findall(r'^\s*\{"([A-Z][A-Z0-9_]*)", "(?:[^"\\]|\\.)*"\},\s*$', head, re.M)
    if dnames != names:
        bad.append('kDefaults does not list the same names as kTable in the same order')
    sys.path.insert(0, HERE)
    import settings_doc
    cur = open(settings_doc.OUT).read() if os.path.exists(settings_doc.OUT) else ''
    if cur != settings_doc.render():
        bad.append('SETTINGS.md is out of date (python3 gr/settings_doc.py rewrites it)')
    for b in bad:
        print('FAIL', b)
    kinds = {}
    for _, k in table:
        kinds[k] = kinds.get(k, 0) + 1
    print(f'settings_check: {len(names)} settings in the table ({", ".join(f"{v} {k}" for k, v in sorted(kinds.items()))}), '
          f'{len(used)} read by {stem}: ' + ('FAIL' if bad else 'ok'))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
