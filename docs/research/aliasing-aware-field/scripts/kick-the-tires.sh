#!/usr/bin/env bash
# Under a minute: phase 0 + 1 (47 kernels rendered at 1x-32x, with predictions).
# Pass/fail lines print as FIELDALIAS ...; compare $OUT/phase1-predictions.csv
# with data/phase1-predictions.csv (tolerance in ARTIFACT.md).
set -euo pipefail
source "$(dirname "$0")/_bin.sh"
INFINITE_FIELDALIASTEST=1 INFINITE_FIELDALIAS_CSV="$OUT/phase1-predictions.csv" "$BIN" 2>&1 | grep "^FIELDALIAS" | tail -5
echo "wrote $OUT/phase1-predictions.csv"
