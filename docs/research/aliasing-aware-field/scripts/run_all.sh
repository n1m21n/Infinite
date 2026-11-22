#!/usr/bin/env bash
# Every dataset in data/, rewritten under $OUT (default data-rerun/), never over data/.
#   phase 0/1: alias measured at 1x-32x plus predictions (one CSV holds both)
#   phase 2:  H2 table, tanh x3 vs 8x, corner sweep, held-out presets
# The phase-2 cost line is the -O2 bench's output; re-measure it with
#   clang++ -std=c++20 -O2 -Isrc -Isrc/core -o /tmp/rate_cost_bench tools/field_rate_cost_bench.cpp && /tmp/rate_cost_bench
set -euo pipefail
source "$(dirname "$0")/_bin.sh"
COST="${INFINITE_FIELDRATE_COST:-0,3.5,11,23,47,10,23,49}"
INFINITE_FIELDALIASTEST=1 INFINITE_FIELDALIAS_CSV="$OUT/phase1-predictions.csv" "$BIN" > "$OUT/phase1.log" 2>&1
INFINITE_FIELDRATEPLAN=1 INFINITE_FIELDRATE_COST="$COST" INFINITE_FIELDRATEPLAN_CSV="$OUT/phase2-h2.csv" \
  INFINITE_FIELDRATE_DUMP="tanh x3 chain drive 4" "$BIN" > "$OUT/phase2-h2-run.log" 2>&1
grep "H2 aggregate\|tanh x3 vs\|held-out:" "$OUT/phase2-h2-run.log"
