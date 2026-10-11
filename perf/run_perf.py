#!/usr/bin/env python3
"""Build a router, route the synthetic design, verify it, write results.json.

usage: perf/run_perf.py [--router pd38route] [--out perf/out] [--repeat 3]
                        [--cxx g++] [--env-file gr/standard.env]

Records, per run: wall seconds, CPU seconds (user + system, all threads),
peak memory, and the verifier's legality counts, total overflow and scored
wirelength. The summary keeps the fastest run by CPU time, because noise on a
shared machine only ever makes a run slower.

The synthetic design (perf/gen_synth.py) is generated, not downloaded, and is
byte-identical everywhere, so the quality numbers must repeat exactly on the
same compiler and platform. A change in them means the router's behaviour
changed, not just its speed.
"""
import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sh(cmd, **kw):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def load_env(path):
    env = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip()
    return env


def timed_run(cmd, env, log_path):
    """Run cmd; return (exit code, wall s, CPU s, peak RSS MB) for that process alone."""
    t0 = time.perf_counter()
    with open(log_path, "w") as log:
        p = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
        _, status, ru = os.wait4(p.pid, 0)
    wall = time.perf_counter() - t0
    p.returncode = os.waitstatus_to_exitcode(status)
    cpu = ru.ru_utime + ru.ru_stime
    # ru_maxrss is KiB on Linux and bytes on macOS.
    rss = ru.ru_maxrss / (1024.0 * 1024.0 if sys.platform == "darwin" else 1024.0)
    return p.returncode, wall, cpu, rss


def cpu_model():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.lower().startswith(("model name", "hardware", "cpu model")):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def parse_verify(text):
    def num(label):
        m = re.search(r"^\s*" + re.escape(label) + r"\s+(-?\d+)", text, re.M)
        return int(m.group(1)) if m else None

    return {
        "malformed": num("malformed segments"),
        "out_of_bounds": num("out of bounds"),
        "disconnected": num("disconnected nets"),
        "incomplete": num("incomplete nets"),
        "unrouted": num("unrouted nets"),
        "overflow": num("total overflow"),
        "wirelength": num("scored wirelength"),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--router", default="pd38route")
    ap.add_argument("--out", default=os.path.join(ROOT, "perf", "out"))
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    ap.add_argument("--env-file", default=os.path.join(ROOT, "gr", "standard.env"))
    ap.add_argument("--skip-build", action="store_true")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    router = os.path.join(a.out, a.router)
    verify = os.path.join(a.out, "gr_verify")
    design = os.path.join(a.out, "synth.gr")

    if not a.skip_build:
        flags = ["-std=c++17", "-O2", "-Wall", "-Wextra"]
        sh([a.cxx, *flags, "-o", router, os.path.join(ROOT, "gr", a.router + ".cpp")])
        sh([a.cxx, *flags, "-o", verify, os.path.join(ROOT, "gr", "gr_verify.cpp")])
    with open(design, "w") as f:
        sh([sys.executable, os.path.join(ROOT, "perf", "gen_synth.py")], stdout=f)

    env = dict(os.environ)
    env.update(load_env(a.env_file))
    # Bound the work so the job finishes in about a minute and stays comparable.
    env.update({"VROUTE_FAST_ROUNDS": "12", "VROUTE_SLOW_ROUNDS": "12"})

    runs = []
    for i in range(a.repeat):
        route = os.path.join(a.out, "synth.%d.route" % i)
        rc, wall, cpu, rss = timed_run([router, design, route], env, os.path.join(a.out, "run%d.log" % i))
        if rc != 0:
            print("router exited with status", rc, file=sys.stderr)
        v = subprocess.run([verify, design, route], capture_output=True, text=True).stdout
        with open(os.path.join(a.out, "verify%d.txt" % i), "w") as f:
            f.write(v)
        r = {"exit": rc, "wall_s": round(wall, 3), "cpu_s": round(cpu, 3), "rss_mb": round(rss, 1)}
        r.update(parse_verify(v))
        runs.append(r)
        print("run %d: %s" % (i, json.dumps(r)), flush=True)

    best = min(runs, key=lambda r: r["cpu_s"])
    legal = all(
        r["exit"] == 0 and all(r[k] == 0 for k in ("malformed", "out_of_bounds", "disconnected", "incomplete", "unrouted"))
        for r in runs
    )
    repeatable = len({(r["overflow"], r["wirelength"]) for r in runs}) == 1
    result = {
        "router": a.router,
        "platform": platform.platform(),
        "cpu": cpu_model(),
        "compiler": subprocess.run([a.cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[0],
        "design": "synthetic 90x90x4, 3500 nets (perf/gen_synth.py defaults)",
        "legal": legal,
        "repeatable": repeatable,
        "best": best,
        "runs": runs,
    }
    with open(os.path.join(a.out, "results.json"), "w") as f:
        json.dump(result, f, indent=2)
    print(json.dumps(result["best"]))
    return 0 if legal else 1


if __name__ == "__main__":
    sys.exit(main())
