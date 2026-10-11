#!/bin/sh
# fetch_eval2008.sh -- download the ISPD 2008 Global Routing Contest evaluator
# (eval2008.pl, by Cliff Sze of IBM Austin, mostly from code by Philip Chong of
# Cadence Berkeley Labs) from the contest site into gr/eval2008/, where
# run_checked.sh and the CI cross-check expect it.
#
# It is not kept in this repository: it carries no license, so it is fetched
# from its source instead of redistributed. The archive is checked against the
# SHA-256 of the copy this project has always used (5650 bytes, dated
# 2008-03-24); a different file stops the script rather than replacing the
# scorer every result in HISTORY_COST_SWEEP.md was cross-checked against.
#
#   sh gr/fetch_eval2008.sh          (from anywhere; writes next to this script)
set -eu
url="https://www.ispd.cc/contests/08/eval2008.zip"
want="a33fb66740ca89c53feef208e16a05f6f228d52ca31e4e5ea3c74a991d6cf58c"
here=$(cd "$(dirname "$0")" && pwd)
dest="$here/eval2008"
if [ -f "$dest/eval2008.pl" ]; then
  echo "eval2008: already present at $dest/eval2008.pl"
  exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "eval2008: downloading $url"
curl -fsSL --max-time 60 -o "$tmp/eval2008.zip" "$url"
if command -v shasum >/dev/null 2>&1; then
  got=$(shasum -a 256 "$tmp/eval2008.zip" | awk '{print $1}')
else
  got=$(sha256sum "$tmp/eval2008.zip" | awk '{print $1}')
fi
if [ "$got" != "$want" ]; then
  echo "eval2008: checksum mismatch (got $got, want $want); not installed" >&2
  exit 1
fi
mkdir -p "$dest"
unzip -q -o "$tmp/eval2008.zip" -d "$tmp/x"
cp "$tmp/x/eval2008/"* "$dest/"
echo "eval2008: installed $dest/eval2008.pl"
