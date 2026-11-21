#!/usr/bin/env bash
set -euo pipefail

# Quick verification (< 5 min) for Spectral Field headless self-tests.
# Requires a built Infinite binary (pass INFINITE_BIN=/path/to/Infinite if non-default).
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="${INFINITE_BIN:-$ROOT/build/Infinite}"

if [[ ! -x "$BIN" ]]; then
  echo "Infinite binary not found at: $BIN"
  echo "Build Infinite or set INFINITE_BIN=/path/to/Infinite and re-run."
  exit 1
fi

INFINITE_SPECTRALFIELDTEST=1 "$BIN"
