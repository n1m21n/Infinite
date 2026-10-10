#!/usr/bin/env bash
# One command for Block C's visual pass: every node family, dark + light, params closed + open, as contact sheets.
#   tools/design/gallery/run_sheets.sh [OUT_DIR] [Category ...]
# Default OUT_DIR ~/infinite-node-gallery/after-$(date +%F)-sheets. Sheets land in OUT_DIR/<theme>/sheets/.
set -euo pipefail
cd "$(dirname "$0")/../../.."
OUT_BASE="${1:-$HOME/infinite-node-gallery/after-$(date +%F)-sheets}"; shift || true
G=tools/design/gallery
INFINITE_NODELIST=1 INFINITE_EXITAFTER=5 ./build/Infinite.app/Contents/MacOS/Infinite > /tmp/infinite-nodelist.txt 2>/dev/null || true
if [ "$#" -gt 0 ]; then CATS=("$@"); else CATS=($(grep '^NODELIST ' /tmp/infinite-nodelist.txt | sed 's/^NODELIST //; s/|.*//' | sort -u)); fi
for THEME in dark light; do
  export OUT="$OUT_BASE/$THEME" THEME
  mkdir -p "$OUT"; cp /tmp/infinite-nodelist.txt "$OUT/list.txt"
  python3 $G/shoot.py "${CATS[@]}" > "$OUT/shoot.log" 2>&1
  for cat in "${CATS[@]}"; do for st in c o; do python3 $G/sheet.py "$cat" "$st" >> "$OUT/sheet.log" 2>&1 || true; done; done
  echo "$THEME: $(ls "$OUT/sheets" 2>/dev/null | wc -l | tr -d ' ') sheets in $OUT/sheets"
done
