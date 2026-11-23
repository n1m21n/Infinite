#!/usr/bin/env bash
# Full reproduction of every dataset in data/ (about 15 minutes on an M2, plus one Infinite binary for
# phases 2-4: INFINITE_BIN, else build-dev/ or build/ at the repo root). Runs on a copy of scripts/ and corpus/
# in a temp folder (the scripts write next to themselves) and copies the results to data-rerun/. Never
# writes data/. Compare with: diff of the JSON verdict fields, or scripts/kick_check.py for the quick slice.
set -euo pipefail
STUDY="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$STUDY/../../.." && pwd)"
PY="${STABMOD_PY:-$HOME/.venvs/stable-modulation/bin/python}"
BIN="${INFINITE_BIN:-}"
for c in "$REPO/build-dev/Infinite.app/Contents/MacOS/Infinite" "$REPO/build/Infinite.app/Contents/MacOS/Infinite"; do
  [[ -z "$BIN" && -x "$c" ]] && BIN="$c"
done
[[ -x "$PY" ]] || { echo "no venv at $PY (see scripts/kick-the-tires.sh)"; exit 1; }
[[ -x "$BIN" ]] || { echo "no Infinite binary; build with python3 scripts/ibuild.py build --mode dev or set INFINITE_BIN"; exit 1; }
export INFINITE_BIN="$BIN"
W="$(mktemp -d)/study"; mkdir -p "$W/data"; cp -R "$STUDY/scripts" "$STUDY/corpus" "$W/"
cd "$W/scripts"
run() { echo "== $*"; "$PY" -W ignore "$@"; }
run phase0_b3.py > ../data/phase0-b3.log            # B3
run phase1_h2.py > ../data/phase1-h2.log            # H2
run phase1_spotcheck.py                             # H2 spot check
run phase1_b12.py > ../data/phase1-b12.log          # B1, B2 (phase 1 oracle, kept as the baseline record)
run emit_field.py ../corpus/field                   # deterministic: same files as committed
echo "== interpreter dump"; INFINITE_FIELDSTABDUMP=../corpus/field INFINITE_FIELDSTABDUMP_OUT="$W/amat" "$BIN" >/dev/null 2>&1 || true
run phase2_compare.py "$W/amat" ../data/phase2-compare.json   # phase 2 (now with B too) and phase 4 dump check
cp ../data/phase2-compare.json ../data/phase4-B-compare.json
run phase3_score.py L,G --jobs 6 > ../data/phase3-L-G.log      # H1, H3, Float32 on L + G
run phase3_h4.py                                                 # H4
run phase3_score.py B --jobs 6 > ../data/phase4-B.log            # phase 4: B with the locked scorer
mv ../data/phase3-B.json ../data/phase4-B.json
run phase6_exact.py                                              # phase 6: exact proof of H2
mkdir -p "$STUDY/data-rerun"; cp -R "$W/data/." "$STUDY/data-rerun/"
echo "wrote $STUDY/data-rerun"
