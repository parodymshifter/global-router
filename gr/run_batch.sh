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
# exported to that job only. Plain bash 3.2 (macOS /bin/bash): no wait -n,
# so the throttle polls the running-job count every 20s.

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

run_one() { # name bench router [VAR=value ...]
  local name="$1" bench="$2" router="$3"
  shift 3
  (
    for kv in "$@"; do export "$kv"; done
    "$here/run_checked.sh" "$outdir/$name" "$bench" "$router"
  )
  cat "$outdir/$name.summary" >> "$summary"
}

while read -r line; do
  case "$line" in ''|'#'*) continue ;; esac
  while [ "$(jobs -rp | wc -l)" -ge "$max_jobs" ]; do sleep 20; done
  # shellcheck disable=SC2086
  run_one $line < /dev/null &
done < "$jobfile"
wait

echo "batch done: $(grep -c . "$summary") jobs, $(grep -vc 'status=OK' "$summary") not OK" >> "$summary"
cat "$summary"
