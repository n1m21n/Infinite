#!/usr/bin/env bash
# Generic node-type sweeps for Infinite's audio/note nodes.
#
# Runs two env-var-gated fixtures (src/main.cpp, search "AUDIOPARAMSWEEPTEST"
# and "AUDIOTEARDOWNSWEEPTEST") through the real compiled .app binary. Both
# discover candidate node types from NodeFactory - the same registry
# RegisterNodes() populates - rather than a hand-maintained list, so a node
# added later (docs/plans/audio/README.md §3) is covered by both without
# anyone editing this file. See SKILL.md for what each one actually checks
# and its documented blind spots.
#
#   - AUDIOPARAMSWEEPTEST   - headless, no window: every param VisitParams
#     declares survives a save/load round trip, and (where a node can be
#     synthetically driven) reaches the audio thread within one block of its
#     own CookIfNeeded call.
#   - AUDIOTEARDOWNSWEEPTEST - runs inside the normal windowed app loop
#     (needs a frame to reach frameId==4): spawns each candidate wired into a
#     running graph, renders a few blocks, deletes it via the real
#     RemoveNodeByIndex, renders a few more, and asserts no crash + any
#     downstream cable pointing at it got cleared.
#
# Usage:
#   .claude/skills/audio-node-sweep/driver.sh               # build + run
#   .claude/skills/audio-node-sweep/driver.sh --skip-build   # reuse existing build/
#
# Exit code: 0 if the build succeeded and both sweeps passed, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

step() { printf '\n== %s ==\n' "$1"; }

# ---------------------------------------------------------------------------
step "Build"
if [ "$SKIP_BUILD" -eq 1 ]; then
  echo "skipped (--skip-build)"
elif [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
else
  echo "no existing build/, configuring fresh"
  if ! cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tee /tmp/infinite_build.log; then
    echo "CONFIGURE FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee -a /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
fi

if [ ! -x "$BIN" ]; then
  echo "binary not found at $BIN after build"
  exit 1
fi

overallOk=1

# ---------------------------------------------------------------------------
step "Param sweep (headless)"
OUT=/tmp/infinite_audio_param_sweep.log
env INFINITE_AUDIOPARAMSWEEPTEST=1 "$BIN" >"$OUT" 2>&1
rc=$?
if [ $rc -ne 0 ] && [ $rc -ne 1 ]; then
  echo "[CRASH] exited $rc — see $OUT"
  overallOk=0
else
  grep -E '^  \[(pass|FAIL|SKIP)\]' "$OUT" || { echo "no per-node verdict lines found — see $OUT"; overallOk=0; }
  skips=$(grep -cE '^  \[SKIP\]' "$OUT")
  if [ "$skips" -gt 0 ]; then
    echo "$skips node(s)/param(s) skipped — see SKILL.md's blind-spots section for why a SKIP or a"
    echo "residual FAIL on a param gated by another param's value is not necessarily a mailbox bug."
  fi
  if grep -qE '^AUDIO PARAM SWEEP FAIL$' "$OUT"; then
    echo "AUDIO PARAM SWEEP FAILED — see $OUT"
    overallOk=0
  elif grep -qE '^AUDIO PARAM SWEEP OK$' "$OUT"; then
    echo "every testable param round-trips through save/load and reaches the audio thread."
  else
    echo "no verdict line found — see $OUT"
    overallOk=0
  fi
fi

# ---------------------------------------------------------------------------
step "Teardown sweep (windowed, frameId==4)"
OUT=/tmp/infinite_audio_teardown_sweep.log
env INFINITE_AUDIOTEARDOWNSWEEPTEST=1 INFINITE_EXITAFTER=10 "$BIN" >"$OUT" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
  echo "[CRASH] exited $rc — see $OUT"
  overallOk=0
else
  grep -E '^  \[(pass|FAIL)\]' "$OUT" || { echo "no per-node verdict lines found — see $OUT"; overallOk=0; }
  grep -E '^xruns=' "$OUT"
  if grep -qE '^AUDIO TEARDOWN SWEEP FAIL$' "$OUT"; then
    echo "AUDIO TEARDOWN SWEEP FAILED — see $OUT"
    overallOk=0
  elif grep -qE '^AUDIO TEARDOWN SWEEP OK$' "$OUT"; then
    echo "every audio/note-graph node type survives a mid-playback delete with cables cleared."
  else
    echo "no verdict line found — see $OUT"
    overallOk=0
  fi
fi

echo
if [ "$overallOk" -eq 0 ]; then
  echo "one or more sweeps failed — see above."
  exit 1
fi
echo "both sweeps passed."
exit 0
