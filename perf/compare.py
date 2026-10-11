#!/usr/bin/env python3
"""Compare two perf results.json files and say what changed.

usage: perf/compare.py NEW.json [OLD.json] [--cpu-limit 25] [--rss-limit 15]
                       [--fail-on-regression]

Prints a markdown table (fit for the GitHub step summary). Exit status is 1 when
the new run is illegal, or, with --fail-on-regression, when CPU time or memory
grew by more than the limits. A change in overflow or wirelength is reported as
a behaviour change but never fails the job: it is often intended.
"""
import argparse
import json
import sys


def pct(new, old):
    return 100.0 * (new - old) / old if old else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("new")
    ap.add_argument("old", nargs="?")
    ap.add_argument("--cpu-limit", type=float, default=25.0, help="percent CPU growth that counts as a regression")
    ap.add_argument("--rss-limit", type=float, default=15.0, help="percent peak-memory growth that counts as a regression")
    ap.add_argument("--fail-on-regression", action="store_true")
    a = ap.parse_args()

    new = json.load(open(a.new))
    nb = new["best"]
    print("### Router timing: %s" % new["router"])
    print("")
    print("%s, %s, %s" % (new["compiler"], new["platform"], new.get("cpu", "unknown CPU")))
    print("")
    problems = []
    if not new["legal"]:
        problems.append("the routed output is not legal (see verify*.txt)")
    if not new["repeatable"]:
        problems.append("repeat runs gave different overflow or wirelength: the router is not deterministic here")

    old = None
    if a.old:
        try:
            old = json.load(open(a.old))
        except (OSError, ValueError):
            old = None

    if old is None:
        print("No earlier result to compare with; this run becomes the first data point.")
        print("")
        print("| metric | this run |")
        print("|---|---|")
        for k, label in (("cpu_s", "CPU seconds"), ("wall_s", "wall seconds"), ("rss_mb", "peak memory MB"),
                         ("overflow", "total overflow"), ("wirelength", "scored wirelength")):
            print("| %s | %s |" % (label, nb[k]))
    else:
        ob = old["best"]
        same_platform = all(old.get(k) == new.get(k) for k in ("platform", "compiler", "cpu"))
        print("| metric | before | now | change |")
        print("|---|---|---|---|")
        for k, label in (("cpu_s", "CPU seconds"), ("wall_s", "wall seconds"), ("rss_mb", "peak memory MB")):
            print("| %s | %s | %s | %+.1f%% |" % (label, ob[k], nb[k], pct(nb[k], ob[k])))
        for k, label in (("overflow", "total overflow"), ("wirelength", "scored wirelength")):
            print("| %s | %s | %s | %s |" % (label, ob[k], nb[k], "same" if ob[k] == nb[k] else "CHANGED"))
        print("")
        if not same_platform:
            print("The earlier run used a different compiler or platform, or CPU, so timings are not compared strictly.")
        else:
            if pct(nb["cpu_s"], ob["cpu_s"]) > a.cpu_limit:
                problems.append("CPU time grew %.1f%% (limit %.0f%%)" % (pct(nb["cpu_s"], ob["cpu_s"]), a.cpu_limit))
            if pct(nb["rss_mb"], ob["rss_mb"]) > a.rss_limit:
                problems.append("peak memory grew %.1f%% (limit %.0f%%)" % (pct(nb["rss_mb"], ob["rss_mb"]), a.rss_limit))
            if (nb["overflow"], nb["wirelength"]) != (ob["overflow"], ob["wirelength"]):
                print("Behaviour change: the routes differ from the earlier run (overflow or wirelength moved).")
                print("")

    if problems:
        print("**Problems:**")
        for p in problems:
            print("- " + p)
    else:
        print("No problems found.")

    hard = not new["legal"] or not new["repeatable"]
    soft = any(("grew" in p) for p in problems)
    return 1 if hard or (soft and a.fail_on_regression) else 0


if __name__ == "__main__":
    sys.exit(main())
