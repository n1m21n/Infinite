#!/usr/bin/env bash
# Pre-commit hygiene driver for Infinite.
#
# Builds the app, then drives its built-in env-var-gated self-test harness
# (src/main.cpp, search `getenv("INFINITE_`) through the real compiled
# .app binary — real ImGui frames, real GL draws, real node graph, not a
# mock. Each test spawns a small fixture graph, runs it for N frames, and
# printf's a verdict line ending in "OK", containing "FAIL", or ending in
# "BUG". This script greps for the failure markers and reports pass/fail
# per check.
#
# Usage:
#   .claude/skills/run-infinite-hygiene/driver.sh              # full suite
#   .claude/skills/run-infinite-hygiene/driver.sh --skip-build  # reuse existing build/
#   .claude/skills/run-infinite-hygiene/driver.sh --shot-only   # just render + screenshot, no test suite
#
# Exit code: 0 if the build succeeded and every check passed, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
SHOT_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    --shot-only) SHOT_ONLY=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

FAIL_MARK='FAIL|BUG$'
PASS=0
FAIL=0
FAILED_NAMES=()

# name:exitAfter — chosen empirically (each verified in-session to print its
# full verdict by that frame; padded a few frames for safety). Grouped by
# what they exercise; see SKILL.md for the map.
TESTS=(
  "UNDOTEST:10"
  "PATCHTEST:30"
  "ROUNDTRIPTEST:35"
  "GROUPTEST:30"
  "COMMENTTEST:15"
  "HIDETEST:35"
  "SELECTTEST:35"
  "DISTRIBUTETEST:10"
  "PHASE4TEST:10"
  "MINIVIEWPORTTEST:12"
  "COLORTEST:13"
  "MACROTEST:30"
  "PALETTETEST:30"
  "BYPASSTEST:30"
  "GEOTEST:30"
  "MESHOPTEST:30"
  "TEXT3DTEST:30"
  "PATHOCEANTEST:35"
  "SHADOWTEST:35"
  "MATFRAMETEST:35"
  "MAPTEST:35"
  "PADPATHTEST:35"
  "BUGTEST:35"
  "FIXTEST:35"
  "3DTEST:35"
  "TRANSFORMSWEEPTEST:10"
  "MAPPINGSWEEPTEST:10"
  "REVISIONSWEEPTEST:10"
  "ENVTEST:14"
  "PHASEATEST:35"
  "PHASECTEST:35"
  "PHASEDTEST:35"
  "PHASEETEST:35"
  "PHASEFTEST:35"
  "WRAPTEST:35"
  "LIVETEST:35"
  "PHASE1TEST:35"
  "DELETECRASHTEST:8"
  "AUDIOGRAPHTEST:8"
  "DRAGTEST:35"
  "WTDRAGTEST:35"
  "AUDIOPARAMSWEEPTEST:1"
  "AUDIOTEARDOWNSWEEPTEST:10"
)

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

# ---------------------------------------------------------------------------
step "Visual smoke (screenshot)"
SHOT="/tmp/infinite_hygiene_shot.png"
rm -f "$SHOT"
IMAGERESYNTH_SCREENSHOT="$SHOT" INFINITE_SHOWCASE=1 "$BIN" >/tmp/infinite_shot.log 2>&1
if [ -f "$SHOT" ]; then
  echo "wrote $SHOT — Read it to eyeball rendering (nodes, previews, chrome all draw correctly)"
else
  echo "SCREENSHOT FAILED — see /tmp/infinite_shot.log"
  FAIL=$((FAIL+1))
  FAILED_NAMES+=("SCREENSHOT")
fi

if [ "$SHOT_ONLY" -eq 1 ]; then
  echo
  echo "shot-only run — skipping test suite"
  exit 0
fi

# ---------------------------------------------------------------------------
step "Self-test suite (${#TESTS[@]} checks)"
for spec in "${TESTS[@]}"; do
  name="${spec%%:*}"
  frames="${spec##*:}"
  out="/tmp/infinite_test_${name}.log"
  env "INFINITE_${name}=1" INFINITE_EXITAFTER="$frames" "$BIN" >"$out" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    echo "  [CRASH] $name — exited $rc, see $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name (crash)")
    continue
  fi
  if grep -qE "$FAIL_MARK" "$out"; then
    echo "  [FAIL]  $name — see $out"
    grep -E "$FAIL_MARK" "$out" | sed 's/^/          /'
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
  else
    verdict=$(grep -E ' OK$' "$out" | tail -1)
    echo "  [pass]  $name  ${verdict:+— $verdict}"
    PASS=$((PASS+1))
  fi
done

# ---------------------------------------------------------------------------
step "Summary"
echo "passed: $PASS   failed: $FAIL"
if [ $FAIL -gt 0 ]; then
  echo "failing checks: ${FAILED_NAMES[*]}"
  echo
  echo "known baseline (not a regression unless it changes): DRAGTEST's canvas-pan"
  echo "sub-check prints \"... : BUG\" on a clean tree — see SKILL.md Gotchas. PHASE1TEST"
  echo "is occasionally flaky on particle-system timing; rerun once before treating it"
  echo "as a regression. PHASEATEST currently fails on a pre-existing \"Smooth\" node-"
  echo "name collision unrelated to audio work — see the spawned task to fix it."
  echo "known baseline: AUDIOPARAMSWEEPTEST currently reports [FAIL] on one Dynamics"
  echo "param (sidechainExternal — both audio input slots get the identical drive tone"
  echo "in the sweep's generic rig, so switching which one the detector reads from"
  echo "can never change the signature), two Delay params (sync, rateDiv — both"
  echo "gate the base delay time itself, whose musical default (250ms) is longer than"
  echo "the sweep's ~70ms warmup window), and three Reverb params (decay, damping,"
  echo "predelay — all only change what the FDN's 8 lines *write*, and a line's own"
  echo "read trails its write by ~600-1100 samples at the default size, longer than"
  echo "the sweep's post-alteration measurement window). All hand-confirmed correct —"
  echo "see EffectDefs.cpp's comments on each param and .claude/skills/audio-node-sweep/"
  echo "SKILL.md's blind-spots section. Dynamics and Delay were cut down from a much"
  echo "larger control surface to match KHS Audio's reference plugins per"
  echo ".claude/skills/new-audio-node/SKILL.md's minimalism rule, which is also why"
  echo "this list is now much shorter than it used to be."
  exit 1
fi
echo "all checks green."
exit 0
