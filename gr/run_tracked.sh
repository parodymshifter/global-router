#!/bin/sh
# run_tracked.sh -- wraps a router invocation to capture both physical and
# virtual memory alongside the existing overflow/wirelength/time numbers
# every run in this project already records.
#
# /usr/bin/time -l gives an authoritative *physical* peak (the kernel's own
# "maximum resident set size" / "peak memory footprint", tracked over the
# process's whole lifetime) but has no virtual-memory equivalent. Virtual
# size (VSZ) on macOS is dominated by the dyld shared-cache mapping and
# reserved-but-uncommitted address space, not actual usage -- there is no
# kernel-tracked "peak VSZ" the way there is for RSS -- so the only way to
# see it at all is periodic polling while the process runs, which this
# script does at a coarse 5s interval alongside the authoritative physical
# number, purely for visibility (a flat, unchanging VSZ across the whole
# run number is itself the informative result, not a search for a peak that
# doesn't mean the same thing).
#
# Usage:
#   ./run_tracked.sh <log_prefix> <router_binary> <args...>
#
# Writes:
#   <log_prefix>.time    -- /usr/bin/time -l output (real/user/sys time,
#                            maximum resident set size, peak memory
#                            footprint, page faults, etc.)
#   <log_prefix>.vsz      -- timestamped VSZ/RSS samples every 5s while the
#                            process runs, plus a final "max observed"
#                            line -- a lower bound on the true peak, since
#                            it can only catch what a 5s poll happens to see
#   <log_prefix>.stdout, <log_prefix>.stderr -- the router's own output,
#                            unchanged from every prior run's convention

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <log_prefix> <router_binary> <args...>" >&2
  exit 2
fi

prefix="$1"
shift

: > "${prefix}.vsz"

/usr/bin/time -l "$@" > "${prefix}.stdout" 2> "${prefix}.stderr.tmp" &
router_pid=$!

# Poll while the router runs. ps exits nonzero once the pid is gone, which
# ends the loop without needing to track completion separately.
while ps -o vsz=,rss= -p "$router_pid" > /tmp/run_tracked_sample.$$ 2>/dev/null; do
  sample=$(cat /tmp/run_tracked_sample.$$)
  if [ -n "$sample" ]; then
    echo "$(date +%H:%M:%S) $sample" >> "${prefix}.vsz"
  fi
  rm -f /tmp/run_tracked_sample.$$
  sleep 5
done
rm -f /tmp/run_tracked_sample.$$

wait "$router_pid"
router_exit=$?

# /usr/bin/time -l writes its report to stderr, interleaved with whatever
# the router itself wrote there -- split them apart so <log_prefix>.stderr
# matches every prior run's plain-router-stderr convention, and the time
# report gets its own file.
grep -E "real |maximum resident|peak memory|page (reclaims|faults)|instructions retired|cycles elapsed" "${prefix}.stderr.tmp" > "${prefix}.time"
grep -vE "real |maximum resident|peak memory|page (reclaims|faults)|instructions retired|cycles elapsed|average (shared|unshared)|swaps|block (input|output)|messages (sent|received)|signals received|voluntary|involuntary" "${prefix}.stderr.tmp" > "${prefix}.stderr"
rm -f "${prefix}.stderr.tmp"

if [ -s "${prefix}.vsz" ]; then
  maxVsz=$(awk '{print $2}' "${prefix}.vsz" | sort -n | tail -1)
  maxRss=$(awk '{print $3}' "${prefix}.vsz" | sort -n | tail -1)
  echo "max observed VSZ=${maxVsz}KB RSS=${maxRss}KB (5s-poll lower bound, not the kernel-tracked peak)" >> "${prefix}.vsz"
fi

exit "$router_exit"
