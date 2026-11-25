#!/usr/bin/env bash
# Quick verification for the saturating-loops study (a few minutes on an M2).
# scripts/kick_check.py reruns the locked phase 1 + phase 3 code on a slice of G (and B) and compares every
# verdict with data/; with an Infinite binary (INFINITE_BIN, else build-dev/ or build/ at the repo root) it
# also re-dumps L with saturators cut and runs the gate's negative test. Never writes data/.
set -euo pipefail
STUDY="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$STUDY/../../.." && pwd)"
PY="${STABMOD_PY:-$HOME/.venvs/stable-modulation/bin/python}"
BIN="${INFINITE_BIN:-}"
for c in "$REPO/build-dev/Infinite.app/Contents/MacOS/Infinite" "$REPO/build/Infinite.app/Contents/MacOS/Infinite"; do
  [[ -z "$BIN" && -x "$c" ]] && BIN="$c"
done
if [[ ! -x "$PY" ]]; then
  echo "No research venv at $PY. Create one (Report 2's venv, reused unchanged):"
  echo "  python3.12 -m venv ~/.venvs/stable-modulation && ~/.venvs/stable-modulation/bin/pip install -r $STUDY/../stable-modulation/scripts/requirements.txt"
  echo "or set STABMOD_PY=/path/to/python."
  exit 1
fi
cd "$STUDY/scripts"
mkdir -p "$STUDY/data-rerun"
"$PY" -W ignore kick_check.py "$STUDY/data-rerun" "$BIN"
