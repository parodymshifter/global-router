#!/bin/sh
# run_checked.sh -- one router run plus both independent checks, with no
# model in the loop: run_tracked.sh (memory + time), then gr_verify, then
# eval2008.pl, then a one-line summary. Written so a batch of multi-hour runs
# can be verified and recorded on disk as each finishes, and whoever reads
# the results later reads one file instead of re-running the checks.
#
# Usage (from gr/, router env vars set by the caller):
#   ./run_checked.sh <log_prefix> <benchmark.gr> <router_binary>
#
# A benchmark ending in .cap is an ISPD 2024 design: handed to
# run_checked24.sh (same job lines, its own checks and summary).
#
# Writes <log_prefix>.{stdout,stderr,time,vsz} (run_tracked.sh), .route,
# .verify, .eval, and <log_prefix>.summary -- one line:
#   <name> status=<OK|ILLEGAL|DISAGREE|ROUTER_FAILED> of=<N> max=<N> wl=<N>
#   eval_of=<N> eval_max=<N> eval_wl=<N> 2d=<N> real_s=<N> rss_mb=<N>
#   legal=<m/o/d/i/u counts>
# status=OK means legal (no malformed, out-of-bounds, disconnected,
# incomplete or unrouted nets) AND gr_verify and eval2008.pl agree on total
# overflow, max overflow and scored wirelength. Anything else is flagged, not
# hidden: the summary is written either way.
#
# Disk: once both checks and the summary are done, the route file is
# gzip-compressed to <log_prefix>.route.gz (route files run 100-170 MB each
# and filled ~33 GB of disk before this). Reuse one with
#   gunzip -k <log_prefix>.route.gz
# Set KEEP_ROUTE=1 for a job whose route file is a planned input to later
# runs (e.g. PD3ROUTE_LA_FROM); it is then left uncompressed.

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <log_prefix> <benchmark.gr> <router_binary>" >&2
  exit 2
fi

prefix="$1"
bench="$2"
router="$3"
name=$(basename "$prefix")
here=$(cd "$(dirname "$0")" && pwd)

# An ISPD 2024 design (.cap, its .net beside it): run_checked24.sh.
case "$bench" in
  *.cap) exec "$here/run_checked24.sh" "$prefix" "$bench" "$router" ;;
esac

"$here/run_tracked.sh" "$prefix" "$router" "$bench" "${prefix}.route"
router_exit=$?

field() { # last field of the first line matching $2 in file $1
  grep -m1 "$2" "$1" 2>/dev/null | awk '{print $NF}'
}

if [ "$router_exit" -ne 0 ] || [ ! -s "${prefix}.route" ]; then
  echo "$name status=ROUTER_FAILED exit=$router_exit" > "${prefix}.summary"
  exit 1
fi

"$here/gr_verify" "$bench" "${prefix}.route" > "${prefix}.verify" 2>&1
perl "$here/eval2008/eval2008.pl" "$bench" "${prefix}.route" > "${prefix}.eval" 2>&1

of=$(field "${prefix}.verify" 'total overflow')
max=$(field "${prefix}.verify" 'max overflow')
wl=$(grep -m1 'scored wirelength' "${prefix}.verify" | awk '{print $3}')
m=$(field "${prefix}.verify" 'malformed segments')
o=$(field "${prefix}.verify" 'out of bounds')
d=$(field "${prefix}.verify" 'disconnected nets')
i=$(field "${prefix}.verify" 'incomplete nets')
u=$(field "${prefix}.verify" 'unrouted nets')
eval_line=$(tail -n 1 "${prefix}.eval")
eval_of=$(echo "$eval_line" | awk '{print $(NF-2)}')
eval_max=$(echo "$eval_line" | awk '{print $(NF-1)}')
eval_wl=$(echo "$eval_line" | awk '{print $NF}')
twod=$(grep -m1 '^2D overflow' "${prefix}.stderr" | sed -E 's/^2D overflow[^:]*: ([0-9]+).*/\1/')
real_s=$(grep -m1 ' real ' "${prefix}.time" | awk '{print $1}')
rss=$(grep -m1 'maximum resident set size' "${prefix}.time" | awk '{printf "%d", $1 / 1048576}')

status=OK
if [ "$m$o$d$i$u" != "00000" ]; then
  status=ILLEGAL
elif [ -z "$of" ] || [ "$of" != "$eval_of" ] || [ "$max" != "$eval_max" ] || [ "$wl" != "$eval_wl" ]; then
  status=DISAGREE
fi

echo "$name status=$status of=$of max=$max wl=$wl eval_of=$eval_of eval_max=$eval_max" \
     "eval_wl=$eval_wl 2d=$twod real_s=$real_s rss_mb=$rss legal=$m/$o/$d/$i/$u" \
     > "${prefix}.summary"
if [ "${KEEP_ROUTE:-0}" != "1" ]; then
  gzip -f "${prefix}.route"
fi
[ "$status" = OK ]
