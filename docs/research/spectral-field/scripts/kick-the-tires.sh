#!/usr/bin/env bash
set -euo pipefail

# Quick verification (< 5 min) for Spectral Field headless self-tests.
# Requires a built Infinite binary (pass INFINITE_BIN=/path/to/Infinite if non-default).
# scripts/ -> spectral-field/ -> research/ -> docs/ -> repo root
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
BIN="${INFINITE_BIN:-}"
if [[ -z "$BIN" ]]; then
  for c in "$ROOT/build-dev/Infinite.app/Contents/MacOS/Infinite" "$ROOT/build/Infinite.app/Contents/MacOS/Infinite" "$ROOT/build/Infinite"; do
    if [[ -x "$c" ]]; then BIN="$c"; break; fi
  done
fi

if [[ ! -x "$BIN" ]]; then
  echo "Infinite binary not found at: $BIN"
  echo "Build Infinite or set INFINITE_BIN=/path/to/Infinite and re-run."
  exit 1
fi

INFINITE_SPECTRALFIELDTEST=1 "$BIN"
