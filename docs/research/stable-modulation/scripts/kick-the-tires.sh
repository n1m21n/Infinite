#!/usr/bin/env bash
# Quick verification for the stable-modulation study (under 2 minutes on an M2).
# Re-runs B3 (phase 0) into data-rerun/, then scripts/kick_check.py: H2 at the published bounds, the locked
# scorer on 16 L + 12 G + 12 B kernels (must match data/ exactly), and the Float32 re-check of L when an
# Infinite binary is found (INFINITE_BIN, else build-dev/ or build/ at the repo root). Never writes data/.
set -euo pipefail
STUDY="$(cd "$(dirname "$0")/.." && pwd)"
PY="${STABMOD_PY:-$HOME/.venvs/stable-modulation/bin/python}"
if [[ ! -x "$PY" ]]; then
  echo "No research venv at $PY. Create one:"
  echo "  python3.12 -m venv ~/.venvs/stable-modulation && ~/.venvs/stable-modulation/bin/pip install -r $STUDY/scripts/requirements.txt"
  echo "or set STABMOD_PY=/path/to/python."
  exit 1
fi
cd "$STUDY/scripts"
mkdir -p "$STUDY/data-rerun"
echo "== B3: oracle against the paper's counterexamples (about 60 s)"
"$PY" -W ignore phase0_b3.py data-rerun | tail -3
echo "== checks"
"$PY" -W ignore kick_check.py "$STUDY/data-rerun"
