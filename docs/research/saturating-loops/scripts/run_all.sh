#!/usr/bin/env bash
# Full reproduction of every dataset in data/ for the saturating-loops study (several hours on an M2 8 GB:
# phase 1 on G is the long step; one Infinite dev build for phase 2: INFINITE_BIN, else build-dev/ or build/
# at the repo root). Runs on a copy of scripts/ and corpus/ in a temp folder and copies the results to
# data-rerun/. Never writes data/.
set -euo pipefail
STUDY="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$STUDY/../../.." && pwd)"
PY="${STABMOD_PY:-$HOME/.venvs/stable-modulation/bin/python}"
BIN="${INFINITE_BIN:-}"
JOBS="${JOBS:-4}"
for c in "$REPO/build-dev/Infinite.app/Contents/MacOS/Infinite" "$REPO/build/Infinite.app/Contents/MacOS/Infinite"; do
  [[ -z "$BIN" && -x "$c" ]] && BIN="$c"
done
[[ -x "$PY" ]] || { echo "no venv at $PY (see scripts/kick-the-tires.sh)"; exit 1; }
[[ -x "$BIN" ]] || { echo "no Infinite binary; build with python3 scripts/ibuild.py build --mode dev or set INFINITE_BIN"; exit 1; }
W="$(mktemp -d)/study"; mkdir -p "$W/data"; cp -R "$STUDY/scripts" "$STUDY/corpus" "$W/"
cp -R "$STUDY/../stable-modulation" "$W/../stable-modulation"   # Report 2's certifier, imported by path
cd "$W/scripts"
run() { echo "== $*"; "$PY" -W ignore "$@"; }
run phase0_t3.py > ../data/phase0-t3.log
run phase0_b3.py > ../data/phase0-b3.log                          # B3 (reads phase0-t3)
run phase0_L.py > ../data/phase0-L.log                            # B2, T1/T3/B4 on L
run phase0_L_ceil.py > ../data/phase0-L-ceil.log                  # ceilings vs first witness
run phase0_moog.py > ../data/phase0-moog.log                      # H2 (T3 on the tanh Moog)
run gen_sat.py G                                                  # deterministic: same corpus/G.json
run phase1_G.py "$JOBS" > ../data/phase1-G.log                    # B1, B4, T1, T3, G1, oracle on G
run emit_sat.py ../corpus/field                                   # phase 2: L and G as Field kernels
echo "== interpreter dump (saturators cut)"
INFINITE_FIELDSTABDUMP=../corpus/field INFINITE_FIELDSTABDUMP_OUT="$W/lmat" INFINITE_FIELDSTABDUMP_SAT=1 "$BIN" >/dev/null 2>&1 || true
run phase2_compare.py "$W/lmat" ../data/phase2-compare.json
run phase3_score.py G "$JOBS" > ../data/phase3-G.log              # H1, H3, H4, Float32 on G
run gen_sat.py B                                                  # phase 4: B, after the lock
run emit_sat.py ../corpus/field
INFINITE_FIELDSTABDUMP=../corpus/field INFINITE_FIELDSTABDUMP_OUT="$W/lmat" INFINITE_FIELDSTABDUMP_SAT=1 "$BIN" >/dev/null 2>&1 || true
run phase2_compare.py "$W/lmat" ../data/phase4-B-compare.json
run phase3_score.py B "$JOBS" > ../data/phase4-B.log
mkdir -p "$STUDY/data-rerun"; cp -R "$W/data/." "$STUDY/data-rerun/"
echo "wrote $STUDY/data-rerun"
