#!/bin/sh
# run_checked24.sh -- run_checked.sh for ISPD 2024 designs (HISTORY_COST_SWEEP.md
# Part 103): one router run under run_tracked.sh (memory + time), then
# g24_verify and the contest evaluator, then a one-line summary. run_checked.sh
# hands a job here when its benchmark ends in .cap (the .net beside it), so
# run_batch.sh job lines take 2024 designs unchanged:
#   <name> bench/ispd24/<design>.cap ./pd33route @standard.env
#
# Usage (from gr/): ./run_checked24.sh <log_prefix> <design.cap> <router_binary>
#
# The router is run as <router> <design.cap> <design.net> <prefix>.g24sol.
# Summary line (<log_prefix>.summary):
#   <name> status=<OK|ILLEGAL|DISAGREE|NO_EVALUATOR|ROUTER_FAILED> cost=<total>
#   wl_cost=<N> via_cost=<N> of_cost=<N> over_gcells=<N> max_over=<tracks>
#   eval_cost=<total> real_s=<N> rss_mb=<N> opens=<N>
# status=OK: g24_verify finds the solution legal AND its total cost equals the
# contest evaluator's (bench/ispd24/official/evaluator) to the printed digit.
# The solution file is gzip-compressed afterwards unless KEEP_ROUTE=1.

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <log_prefix> <design.cap> <router_binary>" >&2
  exit 2
fi

prefix="$1"
cap="$2"
router="$3"
net="${cap%.cap}.net"
name=$(basename "$prefix")
here=$(cd "$(dirname "$0")" && pwd)

"$here/run_tracked.sh" "$prefix" "$router" "$cap" "$net" "${prefix}.g24sol"
router_exit=$?

if [ "$router_exit" -ne 0 ] || [ ! -s "${prefix}.g24sol" ]; then
  echo "$name status=ROUTER_FAILED exit=$router_exit" > "${prefix}.summary"
  exit 1
fi

"$here/g24_verify" "$cap" "$net" "${prefix}.g24sol" > "${prefix}.g24verify" 2>&1
legal=$?
ev="$here/bench/ispd24/official/evaluator"
if [ -x "$ev" ]; then
  "$ev" "$cap" "$net" "${prefix}.g24sol" > "${prefix}.g24eval" 2>&1
fi

cost=$(awk '/^total cost/ {print $3}' "${prefix}.g24verify")
wlc=$(awk '/^wirelength/ {gsub(/\)/,"",$4); print $4}' "${prefix}.g24verify")
viac=$(awk '/^vias/ {gsub(/\)/,"",$4); print $4}' "${prefix}.g24verify")
ofc=$(awk '/^overflow cost/ {print $3}' "${prefix}.g24verify")
overg=$(awk '/^overflow cost/ {gsub(/\(/,"",$4); print $4}' "${prefix}.g24verify")
maxo=$(awk '/^overflow cost/ {print $(NF-1)}' "${prefix}.g24verify")
eval_cost=$(awk '/^total cost/ {print $3}' "${prefix}.g24eval" 2>/dev/null)
opens=$(awk '/^Number of open nets/ {print $NF}' "${prefix}.g24eval" 2>/dev/null)
real_s=$(grep -m1 ' real ' "${prefix}.time" | awk '{print $1}')
rss=$(grep -m1 'maximum resident set size' "${prefix}.time" | awk '{printf "%d", $1 / 1048576}')

status=OK
if [ "$legal" -ne 0 ]; then
  status=ILLEGAL
elif [ ! -x "$ev" ]; then
  status=NO_EVALUATOR
elif [ -z "$cost" ] || [ "$cost" != "$eval_cost" ]; then
  status=DISAGREE
fi

echo "$name status=$status cost=$cost wl_cost=$wlc via_cost=$viac of_cost=$ofc over_gcells=$overg" \
     "max_over=$maxo eval_cost=$eval_cost real_s=$real_s rss_mb=$rss opens=$opens" > "${prefix}.summary"
if [ "${KEEP_ROUTE:-0}" != "1" ]; then
  gzip -f "${prefix}.g24sol"
fi
[ "$status" = OK ]
