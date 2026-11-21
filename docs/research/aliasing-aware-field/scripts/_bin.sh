# Sourced by the other scripts: finds the repo root and the Infinite binary.
# scripts/ -> aliasing-aware-field/ -> research/ -> docs/ -> repo root
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
BIN="${INFINITE_BIN:-}"
if [[ -z "$BIN" ]]; then
  for c in "$ROOT/build-dev/Infinite.app/Contents/MacOS/Infinite" "$ROOT/build/Infinite.app/Contents/MacOS/Infinite"; do
    if [[ -x "$c" ]]; then BIN="$c"; break; fi
  done
fi
if [[ -z "$BIN" || ! -x "$BIN" ]]; then
  echo "Infinite binary not found. Build it (python3 scripts/ibuild.py build --mode dev) or set INFINITE_BIN." >&2
  exit 1
fi
OUT="${OUT:-$(dirname "$0")/../data-rerun}"
mkdir -p "$OUT"
