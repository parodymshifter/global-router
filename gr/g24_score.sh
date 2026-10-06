#!/bin/bash
# g24_score.sh -- scores a router's .gr solution of a bridged ISPD 2024
# design (HISTORY_COST_SWEEP.md Part 102): converts it back to GCell
# segments with g24_bridge and runs g24_verify on the result.
#
#   ./g24_score.sh <design.cap> <design.net> <route or route.gz> <out prefix>
#
# Writes <out prefix>.g24sol and <out prefix>.g24verify; prints the verify
# report. When the contest evaluator is built at bench/ispd24/official/
# evaluator, it also scores the file (<out prefix>.g24eval) and the two
# total costs are compared: "cross-check OK" or "cross-check MISMATCH".
if [ "$#" -ne 4 ]; then
  echo "usage: $0 <design.cap> <design.net> <route[.gz]> <out prefix>" >&2
  exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
case "$3" in
  *.gz) gunzip -c "$3" | "$here/g24_bridge" from-route "$1" "$2" /dev/stdin "$4.g24sol" || exit 1 ;;
  *) "$here/g24_bridge" from-route "$1" "$2" "$3" "$4.g24sol" || exit 1 ;;
esac
"$here/g24_verify" "$1" "$2" "$4.g24sol" | tee "$4.g24verify"
ev="$here/bench/ispd24/official/evaluator"
if [ -x "$ev" ]; then
  "$ev" "$1" "$2" "$4.g24sol" > "$4.g24eval" 2>&1
  a=$(awk '/^total cost/ {print $3}' "$4.g24verify")
  b=$(awk '/^total cost/ {print $3}' "$4.g24eval")
  opens=$(awk '/^Number of open nets/ {print $NF}' "$4.g24eval")
  if [ -n "$a" ] && [ "$a" = "$b" ]; then
    echo "cross-check OK: evaluator total cost $b, open nets $opens"
  else
    echo "cross-check MISMATCH: g24_verify $a, evaluator $b"
  fi
fi
