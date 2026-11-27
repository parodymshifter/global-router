#!/bin/bash
# Recreate the ISPD 2025 contest's OpenROAD fork as built on this Mac (2026-10-12): clone, pin, patch, build.
# Usage: openroad/setup.sh [DEST]   (default ~/Code/OpenROAD_ISPD25). See openroad/README.md.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-$HOME/Code/OpenROAD_ISPD25}
COMMIT=1a29dadda2c06e2cb2f5b09b8f7425778dd88b47
[ -e "$DEST" ] && { echo "setup.sh: $DEST exists; give another folder or remove it first" >&2; exit 2; }
git clone --recursive https://github.com/liangrj2014/OpenROAD_ISPD25.git "$DEST"
cd "$DEST"
git checkout "$COMMIT"
git submodule update --init --recursive
git apply "$HERE/ispd25_fork_bsd3.patch"
# OpenSTA (src/sta) is GPL v3, so its one-line change is applied here rather than kept as a patch file in this
# Apache-2.0 repository: newer SWIG wants $action where the 2024 code has $function.
sed -i '' 's/try { \$function }/try { $action }/' src/sta/tcl/Exception.i
grep -q 'try { \$action }' src/sta/tcl/Exception.i || { echo "setup.sh: the OpenSTA change did not apply" >&2; exit 1; }
mkdir -p logs && cp "$HERE/build.sh" "$HERE/PATCHES.txt" logs/
echo "patched; now run: bash $DEST/logs/build.sh   (needs the Homebrew packages in openroad/README.md)"
