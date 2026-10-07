#!/bin/sh
# Downloads the ISPD 2024 contest benchmarks (Simple_inputs, Google Drive
# folder 1bon65UEAx8cjSvVhYJ-lgC8QMDX0fvUm, linked from
# liangrj2014.github.io/ISPD24_contest) listed in download.list: "<id> <name>".
# Skips a file already present; rejects an HTML page (a Drive warning) in
# place of data. One line per file to stdout.
cd "$(dirname "$0")"
while read -r id name; do
  [ -z "$id" ] && continue
  if [ -s "$name" ]; then echo "have $name"; continue; fi
  curl -sSL --retry 3 -o "$name.part" "https://drive.usercontent.google.com/download?id=$id&export=download&confirm=t"
  if [ $? -ne 0 ] || head -c 200 "$name.part" | grep -qi "<html"; then
    echo "FAILED $name"; rm -f "$name.part"; continue
  fi
  mv "$name.part" "$name"
  echo "got $name $(ls -l "$name" | awk '{print $5}') bytes"
done < download.list
echo "downloads done"
