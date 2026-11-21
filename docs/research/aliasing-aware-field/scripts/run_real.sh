#!/usr/bin/env bash
# Phases 6 and 7 (README sections 17, 19): run the kernel list on 12 clips, in shards.
#   scripts/run_real.sh measure 0 4 8   -> shards 0..3 of 8, in parallel
#   scripts/run_real.sh p1 0 4 8
#   PHASE=phase7 CLIPS=corpus/clips7 scripts/run_real.sh measure 0 4 8   (phase 7 confirmation clips)
set -euo pipefail
source "$(dirname "$0")/_bin.sh"
MODE="$1"; FIRST="$2"; COUNT="$3"; TOTAL="$4"
R="$(cd "$(dirname "$0")/.." && pwd)"
PHASE="${PHASE:-phase6}"
CLIPS="${CLIPS:-corpus/clips}"
[[ "$CLIPS" = /* ]] || CLIPS="$R/$CLIPS"
O="$OUT/$PHASE"; mkdir -p "$O"
[[ -f "$O/kernels.txt" ]] || python3 "$R/scripts/real_kernels.py" --subsample > "$O/kernels.txt"
pids=()
for ((i = FIRST; i < FIRST + COUNT && i < TOTAL; i++)); do
  INFINITE_FIELDREAL="$R/corpus/heldout" INFINITE_FIELDREAL_CLIPS="$CLIPS" INFINITE_FIELDREAL_MODE="$MODE" \
    INFINITE_FIELDREAL_LIST="$O/kernels.txt" INFINITE_FIELDREAL_SHARD="$i/$TOTAL" \
    INFINITE_FIELDREAL_CSV="$O/$MODE-$i.csv" "$BIN" > "$O/$MODE-$i.log" 2>&1 &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "done $MODE shards $FIRST..$((FIRST + COUNT - 1)) of $TOTAL"
