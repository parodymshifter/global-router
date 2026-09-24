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
time_pid=$!

# `time_pid` is /usr/bin/time itself, not the router it execs as a child --
# polling that PID directly measures the wrapper (a few hundred KB, forever)
# and silently reports nothing about the router at all. Found this the hard
# way: the first run under this script logged RSS readings under 1.2MB for
# a process /usr/bin/time's own -l report said peaked at 635MB. Wait for
# the real child to appear under time_pid, then poll *that* PID instead.
router_pid=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
  router_pid=$(pgrep -P "$time_pid" | head -1)
  [ -n "$router_pid" ] && break
  sleep 0.2
done

# Poll while the router runs. ps exits nonzero once the pid is gone, which
# ends the loop without needing to track completion separately. If the
# child was never found (router exited faster than the 2s search window,
# or pgrep isn't available), fall back to waiting on time_pid directly with
# no samples rather than silently polling the wrong process.
if [ -n "$router_pid" ]; then
  while ps -o vsz=,rss= -p "$router_pid" > /tmp/run_tracked_sample.$$ 2>/dev/null; do
    sample=$(cat /tmp/run_tracked_sample.$$)
    if [ -n "$sample" ]; then
      echo "$(date +%H:%M:%S) $sample" >> "${prefix}.vsz"
    fi
    rm -f /tmp/run_tracked_sample.$$
    sleep 5
  done
  rm -f /tmp/run_tracked_sample.$$
fi

wait "$time_pid"
router_exit=$?

# /usr/bin/time -l writes its report to stderr, interleaved with whatever
# the router itself wrote there -- split them apart so <log_prefix>.stderr
# matches every prior run's plain-router-stderr convention, and the time
# report gets its own file.
#
# Every /usr/bin/time -l line starts with whitespace and a number
# ("  5723.67 real ...", "  1013088256  maximum resident set size"); router
# output starts with words. The patterns are anchored to that shape: an
# earlier unanchored "real " also matched the router's own
# "real (wire+via) overflow: ..." summary and moved it into <prefix>.time.
time_line='^[[:space:]]*[0-9.]+[[:space:]]+'
grep -E "${time_line}(real|maximum resident|peak memory|page (reclaims|faults)|instructions retired|cycles elapsed)" "${prefix}.stderr.tmp" > "${prefix}.time"
grep -vE "${time_line}(real|maximum resident|peak memory|page (reclaims|faults)|instructions retired|cycles elapsed|average (shared|unshared)|swaps|block (input|output)|messages (sent|received)|signals received|voluntary|involuntary)" "${prefix}.stderr.tmp" > "${prefix}.stderr"
rm -f "${prefix}.stderr.tmp"

if [ -s "${prefix}.vsz" ]; then
  maxVsz=$(awk '{print $2}' "${prefix}.vsz" | sort -n | tail -1)
  maxRss=$(awk '{print $3}' "${prefix}.vsz" | sort -n | tail -1)
  echo "max observed VSZ=${maxVsz}KB RSS=${maxRss}KB (5s-poll lower bound, not the kernel-tracked peak)" >> "${prefix}.vsz"
fi

exit "$router_exit"
