#!/bin/sh
# H9: regenerate the headless coverage report for a release and attach it.
#   tools/headless-coverage/release_report.sh <release Infinite.app binary> <tag> [--upload]
# Sweep (resumable, 20-min chunks: re-run until it says done), determinism, per-family report.
# With --upload the report goes onto the GitHub release as headless-coverage.md (+ coverage.json);
# the site links to releases/latest/download/headless-coverage.md.
set -e
BIN="$1"; TAG="$2"; UP="$3"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/headless-coverage-$TAG"
python3 "$ROOT/tools/headless-coverage/sweep.py" --bin "$BIN" --out "$OUT" --budget 20 || {
  echo "sweep budget used up: run the same command again to resume"; exit 3; }
python3 "$ROOT/tools/headless-coverage/determinism.py" --bin "$BIN" --sweep "$OUT" --out "$OUT/det"
python3 "$ROOT/tools/headless-coverage/families.py" "$OUT/coverage.json" \
  --determinism "$OUT/det/determinism.json" --out "$OUT/headless-coverage.md"
if [ "$UP" = "--upload" ]; then
  gh release upload "$TAG" "$OUT/headless-coverage.md" "$OUT/coverage.json" --clobber
fi
