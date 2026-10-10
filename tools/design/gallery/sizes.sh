#!/bin/bash
# Prints Cat|params(0/1)|Name|w|h for every node, params closed and open.
OUT=${OUT:-/tmp/node-gallery}; mkdir -p "$OUT"
APP=./build/Infinite.app/Contents/MacOS/Infinite
[ -f "$OUT/list.txt" ] || INFINITE_NODELIST=1 INFINITE_EXITAFTER=5 $APP > "$OUT/list.txt" 2>/dev/null
for cat in $(grep '^NODELIST ' "$OUT/list.txt" | cut -d' ' -f2- | cut -d'|' -f1 | sort -u); do
  names=$(grep "^NODELIST $cat|" "$OUT/list.txt" | cut -d'|' -f2 | paste -sd, -)
  for p in 0 1; do
    extra=(); [ $p = 1 ] && extra=(INFINITE_GALLERYPARAMS=1)
    env INFINITE_NODEGALLERY="$cat|$names" INFINITE_GALLERYGRID="8,1100,1700" INFINITE_GALLERYSIZES=1 INFINITE_EXITAFTER=32 "${extra[@]}" $APP > "$OUT/run-$cat-$p.log" 2>&1
    grep "^NODESIZE" "$OUT/run-$cat-$p.log" | sed "s/^NODESIZE /$cat|$p|/"
  done
done
