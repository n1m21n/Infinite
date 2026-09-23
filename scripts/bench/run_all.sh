#!/usr/bin/env bash
# Runs the INFINITE_BENCH suite (docs/plans/perf/benchmark-suite.md) and
# collects every BENCH_JSON line into bench/results/<machine>/<date>-<sha>.jsonl.
#
# Usage: scripts/bench/run_all.sh [--soak] [--app <path-to-Infinite.app>]
#
# Each fixture is driven by its own INFINITE_BENCH_* env var (the suite is
# built incrementally - see docs/plans/perf/README.md's status table for
# which benchmarks in the B1-B10 list actually have a fixture behind them
# yet). A fixture id with no implementation is reported as SKIP rather than
# silently omitted, so the run's coverage is always visible in its own log.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
APP="$REPO_ROOT/build/Infinite.app/Contents/MacOS/Infinite"
SOAK=0

while [[ $# -gt 0 ]]; do
   case "$1" in
      --soak) SOAK=1; shift ;;
      --app) APP="$2"; shift 2 ;;
      *) echo "unknown arg: $1" >&2; exit 1 ;;
   esac
done

if [[ ! -x "$APP" ]]; then
   echo "error: $APP not found or not executable - build first (cmake --build build -j8)" >&2
   exit 1
fi

SHA="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
DATE="$(date +%Y%m%d-%H%M%S)"
MACHINE="$(sysctl -n hw.model 2>/dev/null || uname -n)"
MACHINE_SAFE="$(echo "$MACHINE" | tr ' /' '__')"
OUT_DIR="$REPO_ROOT/bench/results/$MACHINE_SAFE"
mkdir -p "$OUT_DIR"
OUT_FILE="$OUT_DIR/$DATE-$SHA.jsonl"
: > "$OUT_FILE"

echo "run_all.sh: machine=$MACHINE sha=$SHA -> $OUT_FILE"

# run_fixture <label> <exitafter> <env-assignments...>
run_fixture() {
   local label="$1" exitafter="$2"; shift 2
   echo "  -> $label"
   local log
   log="$(mktemp)"
   if env "$@" INFINITE_EXITAFTER="$exitafter" "$APP" > "$log" 2>&1; then
      :
   fi
   local n
   n=$(grep -c '^BENCH_JSON ' "$log" || true)
   if [[ "$n" -eq 0 ]]; then
      echo "     FAIL: no BENCH_JSON line (see $log)" >&2
   else
      grep '^BENCH_JSON ' "$log" | sed 's/^BENCH_JSON //' >> "$OUT_FILE"
      echo "     $n result(s)"
   fi
   rm -f "$log"
}

skip() {
   echo "  -> $1: SKIP (no fixture implemented yet - see docs/plans/perf/README.md)"
}

echo "B1 Heavy audio"
# Buffer-size sweep per benchmark-suite.md §4 (64/128/256/512). SECONDS
# defaults to 60s per the doc; a run_all.sh invocation can override it with
# B1_SECONDS for fast iteration - the committed baseline should use the
# default. voices=24 is the midpoint of the doc's 12-32 range.
B1_SECONDS="${B1_SECONDS:-60}"
for buf in 64 128 256 512; do
   run_fixture "B1_heavy_audio voices=24,buffer=$buf" 20000 \
      INFINITE_BENCH_B1VOICES=24 INFINITE_BENCH_B1BUFFER="$buf" INFINITE_BENCH_B1SECONDS="$B1_SECONDS"
done

echo "B2 Heavy visuals"
skip "B2_heavy_visuals"

echo "B3 Live performance"
skip "B3_live"

echo "B4 Complex 3D scenes"
skip "B4_complex_3d"

echo "B5 Fundamentals"
run_fixture "B5_fundamentals_empty" 200 INFINITE_BENCH_B5EMPTY=1
for n in 50 100 200 400; do
   # 152 is the fixture's own sample-window boundary (main.cpp, frameId==152).
   # EXITAFTER must be strictly greater: frameId increments at the bottom of
   # the main loop, after the fixture's own frameId==152 check runs near the
   # top, so EXITAFTER=152 closes the window one iteration before that check
   # ever fires and no BENCH_JSON line is ever printed. Confirmed by direct
   # reproduction: EXITAFTER=152 fails every time, 153+ passes every time.
   run_fixture "B5_fundamentals_nodecount n=$n" 160 INFINITE_BENCH_B5NODES="$n"
done
B5D_SECONDS="${B5D_SECONDS:-30}"
for buf in 64 128 256 512; do
   run_fixture "B5_fundamentals_audioalone buffer=$buf" 20000 \
      INFINITE_BENCH_B5AUDIOALONE="$buf" INFINITE_BENCH_B5AUDIOALONE_SECONDS="$B5D_SECONDS"
done
skip "B5_fundamentals (c/e/f/g sub-benchmarks - per-stage CPU+GPU split, startup time, load/save time, undo-snapshot time)"

echo "B6 Canvas navigation"
skip "B6_canvas_navigation"

if [[ "$SOAK" -eq 1 ]]; then
   echo "B7 Soak and thermal (--soak)"
   skip "B7_soak"
else
   echo "B7 Soak and thermal: skipped (pass --soak to include)"
fi

echo "B8 Media I/O"
skip "B8_media_io"

echo "B9 Memory footprint"
skip "B9_memory_footprint"

echo "B10 Offline render and A/V sync"
skip "B10_offline_av_sync"

echo "Done. Results: $OUT_FILE"
echo "Compare against a baseline with: scripts/bench/compare.py <baseline.jsonl> $OUT_FILE"
