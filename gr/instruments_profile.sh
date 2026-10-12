#!/bin/bash
# instruments_profile.sh -- record a router run with Apple Instruments and write a per-stage report
# (HISTORY_COST_SWEEP.md Part 126; PROFILING.md).
#
# Launch a run (router settings come from the environment, as for any run; GR_SIGNPOSTS=1 is set so
# the stage markers are on):
#   ./instruments_profile.sh [-t TEMPLATE] [-l SECONDS] OUT_PREFIX -- ./pd39route in.cap in.net out.sol
# Attach to a router already running (e.g. a long 2008 run in its region stage; markers appear only
# if it was started with GR_SIGNPOSTS=1 -- standard.env sets it):
#   ./instruments_profile.sh -a PID [-t TEMPLATE] -l SECONDS OUT_PREFIX
#
# TEMPLATE: "Time Profiler" (default; 1 ms samples, call stacks) or "CPU Counters" (the same samples
# plus Apple's top-down split of cycles: useful / back-end / front-end / bad speculation). Writes
# OUT_PREFIX.trace (open it in Instruments for the timeline), OUT_PREFIX.stdout (the run's own
# output, stderr included) and OUT_PREFIX.report.md.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
template="Time Profiler"
limit=""
pid=""
while getopts "t:l:a:" o; do
  case $o in
    t) template=$OPTARG ;;
    l) limit=$OPTARG ;;
    a) pid=$OPTARG ;;
    *) sed -n 2,18p "$0"; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { sed -n 2,18p "$0"; exit 2; }
out=$1
shift
[ "${1:-}" = "--" ] && shift
rm -rf "$out.trace"
args=(record --template "$template" --output "$out.trace")
[ -n "$limit" ] && args+=(--time-limit "${limit}s")
if [ -n "$pid" ]; then
  [ -n "$limit" ] || { echo "attaching needs -l SECONDS"; exit 2; }
  xcrun xctrace "${args[@]}" --attach "$pid"
else
  [ $# -ge 1 ] || { echo "nothing to launch: give the router command after --"; exit 2; }
  GR_SIGNPOSTS=1 xcrun xctrace "${args[@]}" --target-stdout "$out.stdout" --launch -- "$@"
fi
python3 "$here/instruments_report.py" "$out.trace" > "$out.report.md"
echo "report: $out.report.md"
