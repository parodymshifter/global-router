#!/bin/bash
# run_batch.sh -- runs a list of router jobs through run_checked.sh, at most
# MAX_JOBS (default 3, CLAUDE.md's concurrency cap) at a time, and collects
# every job's one-line summary into <jobfile>.summary as each finishes. One
# background launch of this script replaces one launch (and one check-in)
# per run.
#
# Usage (from gr/):  ./run_batch.sh <jobfile> <out_dir>
#
# Job file: one job per line, blank lines and # comments ignored:
#   <name> <benchmark.gr> <router_binary> [VAR=value ...]
# e.g.
#   pd1_S1_adaptec1 bench/adaptec1.capo70.3d.35.50.90.gr ./pd1route VROUTE_TRACE=1 NV11ROUTE_BOX_MODE=round
#
# Each job's files land under <out_dir>/<name>.*; the VAR=value pairs are
# exported to that job only. A token "@file" exports every VAR=value line of
# that file (blank lines and # comments skipped; a relative path is tried
# as given, then next to this script) -- e.g. "@standard.env", the standard
# configuration. Pairs are exported left to right, so a VAR=value after
# @file overrides the file.
#
# The cap is machine-wide, not per batch: the throttle counts every router
# running under run_tracked.sh (each is a `/usr/bin/time -l <router>`
# process), including runs started outside this batch, so a batch launched
# while other tracked runs are still going never pushes the total past
# MAX_JOBS. Plain bash 3.2 (macOS /bin/bash): no wait -n, so it polls every
# 20s, and waits 5s after each launch so the new job's process is visible
# before the next count.

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <jobfile> <out_dir>" >&2
  exit 2
fi

jobfile="$1"
outdir="$2"
max_jobs="${MAX_JOBS:-3}"
here=$(cd "$(dirname "$0")" && pwd)
summary="${jobfile}.summary"
mkdir -p "$outdir"
: > "$summary"
# Part 133: hold off idle sleep while the batch runs (a sleeping Mac pauses
# every run and spoils real_s; a closed lid still sleeps). macOS only.
# In a subshell, so the final `wait` does not wait on it.
command -v caffeinate > /dev/null && (caffeinate -i -w $$ > /dev/null 2>&1 &)

run_one() { # name bench router [VAR=value ...]
  local name="$1" bench="$2" router="$3"
  shift 3
  (
    for kv in "$@"; do
      case "$kv" in
        @*)
          f="${kv#@}"
          [ -f "$f" ] || f="$here/$f"
          if [ ! -f "$f" ]; then
            echo "run_batch: settings file ${kv#@} not found" >&2
            exit 2
          fi
          while IFS= read -r l || [ -n "$l" ]; do
            case "$l" in ''|'#'*) ;; *) export "$l" ;; esac
          done < "$f"
          ;;
        *) export "$kv" ;;
      esac
    done
    "$here/run_checked.sh" "$outdir/$name" "$bench" "$router"
  )
  cat "$outdir/$name.summary" >> "$summary"
}

while read -r line; do
  case "$line" in ''|'#'*) continue ;; esac
  while [ "$(pgrep -f '^/usr/bin/time -l ' | wc -l)" -ge "$max_jobs" ]; do sleep 20; done
  # shellcheck disable=SC2086
  run_one $line < /dev/null &
  sleep 5
done < "$jobfile"
wait

echo "batch done: $(grep -c . "$summary") jobs, $(grep -vc 'status=OK' "$summary") not OK" >> "$summary"
cat "$summary"
