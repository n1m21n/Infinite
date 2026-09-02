#!/usr/bin/env bash
# Pre-commit hygiene driver for Infinite.
#
# Builds the app, then drives its built-in env-var-gated self-test harness
# (src/main.cpp, search `getenv("INFINITE_`) through the real compiled
# .app binary — real ImGui frames, real GL draws, real node graph, not a
# mock. Each test spawns a small fixture graph, runs it for N frames, and
# printf's a verdict line ending in "OK"/"PASS"/"SKIP", containing "FAIL", or
# naming a failure ("... - BUG", "SUSPECT", "MISMATCH"). This script greps for
# the failure markers, requires a positive verdict to be present at all, and
# reports pass/fail per check.
#
# Usage:
#   .claude/skills/run-infinite-hygiene/driver.sh --fast              # Tier 1: pre-commit smoke (~8s)
#   .claude/skills/run-infinite-hygiene/driver.sh --auto              # Tier 1 + auto-detected groups from git diff
#   .claude/skills/run-infinite-hygiene/driver.sh --group audio,3d    # Tier 2: specific subsystem groups
#   .claude/skills/run-infinite-hygiene/driver.sh --full              # Tier 3: full suite + screenshot smoke (default)
#   .claude/skills/run-infinite-hygiene/driver.sh --skip-build        # reuse existing build/
#   .claude/skills/run-infinite-hygiene/driver.sh --shot-only         # just render + screenshot, no test suite
#
# Exit code: 0 if the build succeeded and every check passed, 1 otherwise.

set -uo pipefail

# Runs headless and repeatedly; never let UpdateCheck::Start() make a
# network call from here.
export INFINITE_NO_UPDATE_CHECK=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
SHOT_ONLY=0
TIER="full"
EXPLICIT_GROUPS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-build) SKIP_BUILD=1; shift ;;
    --shot-only) SHOT_ONLY=1; shift ;;
    --fast) TIER="fast"; shift ;;
    --auto) TIER="auto"; shift ;;
    --full) TIER="full"; shift ;;
    --group)
      TIER="group"
      EXPLICIT_GROUPS="$2"
      shift 2
      ;;
    --group=*)
      TIER="group"
      EXPLICIT_GROUPS="${1#*=}"
      shift
      ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# Fixture verdicts are not uniform: the failure branch of a ternary is
# sometimes "FAIL", sometimes "... - BUG", and in 56 places in main.cpp it is
# a bare word like "SUSPECT" or "MISMATCH". Grepping only for FAIL|BUG meant
# every one of those failed silently and the gate stayed green - MACROTEST
# printed MISMATCH for as long as its expectation had been stale and this
# script called it a pass. Match every negative form the harness actually
# emits, and separately require a positive verdict (below), so a fixture that
# dies before printing anything can no longer masquerade as a pass either.
FAIL_MARK='FAIL|BUG$|MISMATCH|SUSPECT|DID NOT MOVE|TONE MISSING'
PASS_MARK=' OK$|OK$|PASS$|SKIP$|CLEAN$'
PASS=0
FAIL=0
FAILED_NAMES=()
EXPECTED_FILE="$(dirname "${BASH_SOURCE[0]}")/audio-param-sweep-expected.txt"

# ---------------------------------------------------------------------------
# Tier definitions per docs/plans/test-tiering.md
# ---------------------------------------------------------------------------

TIER1_CHECKS=(
  "UNDOTEST:10"
  "PATCHTEST:30"
  "ROUNDTRIPTEST:35"
  "PINDUPTEST:10"
  "BYPASSTEST:30"
  "DELETECRASHTEST:8"
  "DSPTEST:1"
  "FIELDTEST:1"
  "PERFMATRIXTEST:1"
  "AUDIOPDCTEST:1"
  "RECSYNCTEST:1"
  "AUTOSAVEMARKERTEST:1"
)

GROUP_AUDIO=(
  "AUDIOPARAMSWEEPTEST:1"
  "AUDIOTEARDOWNSWEEPTEST:10"
  "AUDIOGRAPHTEST:8"
  "AUDIOLIFECYCLETEST:8"
  "AUDIORECOVERYTEST:8"
  "AUDIOPDCTEST:1"
  "RECSYNCTEST:1"
  "RECEXPORTTEST:1"
  "METALLICDECAYTEST:1"
)

GROUP_3D=(
  "GEOTEST:30"
  "MESHOPTEST:30"
  "TEXT3DTEST:30"
  "PATHOCEANTEST:35"
  "SHADOWTEST:35"
  "MATFRAMETEST:35"
  "MAPTEST:35"
  "PADPATHTEST:35"
  "3DTEST:35"
  "TRANSFORMSWEEPTEST:10"
  "MAPPINGSWEEPTEST:10"
  "REVISIONSWEEPTEST:10"
  "ENVTEST:14"
  "WRAPTEST:35"
)

GROUP_UI=(
  "GROUPTEST:30"
  "COMMENTTEST:15"
  "HIDETEST:35"
  "SELECTTEST:35"
  "DISTRIBUTETEST:10"
  "INSTANCESELECTTEST:10"
  "MINIVIEWPORTTEST:12"
  "COLORTEST:13"
  "PALETTETEST:30"
  "DRAGTEST:35"
  "WTDRAGTEST:80"
)

GROUP_MODULATION=(
  "MACROTEST:30"
  "MODBOUNDSTEST:30"
  "MODMATRIXTEST:25"
  "PERFMATRIXTEST:1"
)

GROUP_VIDEO=(
  "VIDEOAUDIOTEST:70"
  "VIDEOSPEEDTEST:186"
  "RECEXPORTTEST:1"
  "RECSYNCTEST:1"
)

GROUP_MEDIA=(
  "SAMPLERDRAGTEST:600"
  "MEDIADRAGTEST:600"
  "PLUGINDRAGTEST:600"
  "PLUGINSCANTEST:1"
  "BROWSERSORTTEST:1"
)

GROUP_COMPOSITING=(
  "PHASEATEST:35"
  "PHASECTEST:35"
  "PHASEDTEST:35"
  "PHASEETEST:35"
  "PHASEFTEST:35"
  "PHASE1TEST:35"
  "PHASE4TEST:10"
  "BUGTEST:35"
  "FIXTEST:35"
  "LIVETEST:35"
  "REMOVEBGTEST:1"
  "CURVESLUTTEST:10"
)

FULL_TESTS=(
  "UNDOTEST:10"
  "UNDOPERFTEST:10"
  "PATCHTEST:30"
  "ROUNDTRIPTEST:35"
  "GROUPTEST:30"
  "COMMENTTEST:15"
  "HIDETEST:35"
  "SELECTTEST:35"
  "DISTRIBUTETEST:10"
  "PHASE4TEST:10"
  "INSTANCESELECTTEST:10"
  "MINIVIEWPORTTEST:12"
  "COLORTEST:13"
  "MACROTEST:30"
  "MODBOUNDSTEST:30"
  "MODMATRIXTEST:25"
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
  "AUTOSAVETEST:8"
  "VIDEOAUDIOTEST:70"
  "VIDEOSPEEDTEST:186"
  "DRAGTEST:35"
  "WTDRAGTEST:80"
  "PINDUPTEST:10"
  "PERFMATRIXTEST:1"
  "AUDIOPARAMSWEEPTEST:1"
  "AUDIOTEARDOWNSWEEPTEST:10"
  "AUDIOPDCTEST:1"
  "RECSYNCTEST:1"
  "RECEXPORTTEST:1"
  "AUDIOLIFECYCLETEST:8"
  "AUDIORECOVERYTEST:8"
  "SAMPLERDRAGTEST:600"
  "MEDIADRAGTEST:600"
  "PLUGINDRAGTEST:600"
  "PLUGINSCANTEST:1"
  "AUTOSAVEMARKERTEST:1"
  "REMOVEBGTEST:1"
  "CURVESLUTTEST:10"
  "METALLICDECAYTEST:1"
)

# Helper to add checks uniquely to an array
declare -a SELECTED_TESTS=()
add_checks() {
  for item in "$@"; do
    local exists=0
    for existing in "${SELECTED_TESTS[@]:-}"; do
      if [[ "$existing" == "$item" ]]; then
        exists=1; break
      fi
    done
    if [[ $exists -eq 0 ]]; then
      SELECTED_TESTS+=("$item")
    fi
  done
}

add_group() {
  local grp="$1"
  case "$grp" in
    audio) add_checks "${GROUP_AUDIO[@]}" ;;
    3d|geometry) add_checks "${GROUP_3D[@]}" ;;
    ui|editor) add_checks "${GROUP_UI[@]}" ;;
    modulation) add_checks "${GROUP_MODULATION[@]}" ;;
    video|export) add_checks "${GROUP_VIDEO[@]}" ;;
    media|browser) add_checks "${GROUP_MEDIA[@]}" ;;
    compositing|misc) add_checks "${GROUP_COMPOSITING[@]}" ;;
    *) echo "Warning: unknown group '$grp' ignored" >&2 ;;
  esac
}

RUN_SHOT=0

if [[ "$TIER" == "fast" ]]; then
  echo "Tier 1: Pre-commit smoke (${#TIER1_CHECKS[@]} checks)"
  add_checks "${TIER1_CHECKS[@]}"
elif [[ "$TIER" == "group" ]]; then
  echo "Tier 2: Explicit groups [$EXPLICIT_GROUPS]"
  IFS=',' read -ra GRP_ARRAY <<< "$EXPLICIT_GROUPS"
  for g in "${GRP_ARRAY[@]}"; do
    add_group "$g"
  done
elif [[ "$TIER" == "auto" ]]; then
  DIFF_FILES=$(git diff --name-only HEAD 2>/dev/null || true)
  STAGED_FILES=$(git diff --cached --name-only 2>/dev/null || true)
  UNTRACKED_FILES=$(git status --porcelain 2>/dev/null | awk '{print $2}' || true)
  ALL_DIFF=$(printf "%s\n%s\n%s" "$DIFF_FILES" "$STAGED_FILES" "$UNTRACKED_FILES" | sort -u | grep -v '^$' || true)

  if echo "$ALL_DIFF" | grep -qE 'src/core/Patch\.cpp|NodeFactory|INode\.h'; then
    echo "Tier auto: Cross-cutting change detected in diff -> escalating to Tier 3 (Full suite)"
    TIER="full"
    SELECTED_TESTS=("${FULL_TESTS[@]}")
    RUN_SHOT=1
  elif [[ -z "$ALL_DIFF" ]]; then
    echo "Tier auto: No local diff detected -> running Tier 1 smoke"
    add_checks "${TIER1_CHECKS[@]}"
  else
    add_checks "${TIER1_CHECKS[@]}"
    SELECTED_GROUPS=()
    if echo "$ALL_DIFF" | grep -qE 'src/audio/|src/nodes/Audio|Sequencer|Resonator'; then
      SELECTED_GROUPS+=("audio"); add_group "audio"
    fi
    if echo "$ALL_DIFF" | grep -qE 'src/nodes/Geometry|Mesh|src/core/Mesh\.|3D|Ocean|Path'; then
      SELECTED_GROUPS+=("3d"); add_group "3d"
    fi
    if echo "$ALL_DIFF" | grep -qE 'src/main\.cpp|src/core/NodeViewport\.|imgui'; then
      SELECTED_GROUPS+=("ui"); add_group "ui"
    fi
    if echo "$ALL_DIFF" | grep -qE 'src/core/Modulation\.|Macro|PerfMatrix'; then
      SELECTED_GROUPS+=("modulation"); add_group "modulation"
    fi
    if echo "$ALL_DIFF" | grep -qE 'OutputNode|Recorder|Muxer|Decoder|Platform.*Video'; then
      SELECTED_GROUPS+=("video"); add_group "video"
    fi
    if echo "$ALL_DIFF" | grep -qE 'Scanner|Browser|SampleFolders|MediaFolders'; then
      SELECTED_GROUPS+=("media"); add_group "media"
    fi
    if echo "$ALL_DIFF" | grep -qE 'src/nodes/Blend|Curves|Feedback|Filter'; then
      SELECTED_GROUPS+=("compositing"); add_group "compositing"
    fi

    if [[ ${#SELECTED_GROUPS[@]} -gt 0 ]]; then
      echo "Tier auto: Selected Tier 1 + groups [${SELECTED_GROUPS[*]}] (${#SELECTED_TESTS[@]} checks total)"
    else
      echo "Tier auto: No specific subsystem diff detected -> running Tier 1 smoke (${#SELECTED_TESTS[@]} checks)"
    fi
  fi
fi

if [[ "$TIER" == "full" ]]; then
  echo "Tier 3: Full gate (${#FULL_TESTS[@]} checks + screenshot smoke)"
  SELECTED_TESTS=("${FULL_TESTS[@]}")
  RUN_SHOT=1
fi

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
if [ "$RUN_SHOT" -eq 1 ] || [ "$SHOT_ONLY" -eq 1 ]; then
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
fi

# ---------------------------------------------------------------------------
step "Self-test suite (${#SELECTED_TESTS[@]} checks)"
for spec in "${SELECTED_TESTS[@]}"; do
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
  if [ "$name" = "AUDIOPARAMSWEEPTEST" ]; then
    # Per-line verdict, not one summary line — the final "AUDIO PARAM SWEEP
    # FAIL"/"OK" src/main.cpp:25391 prints is intentionally ignored here;
    # each param's own [FAIL]/[pass] line is checked against
    # audio-param-sweep-expected.txt in both directions. See that file's
    # header for what counts as a baselined blind spot vs a real failure.
    xfail=0; newfail=0; nowpass=0
    xfail_out="/tmp/infinite_test_AUDIOPARAMSWEEPTEST_xfail.log"
    : > "$xfail_out"
    detail_lines=()
    while IFS= read -r line; do
      if [[ "$line" =~ \[FAIL\][[:space:]]+(.+[^[:space:]])[[:space:]]+param\ \'([^\']+)\' ]]; then
        node="${BASH_REMATCH[1]}"; param="${BASH_REMATCH[2]}"
        if grep -qF "${node}|${param}|" "$EXPECTED_FILE"; then
          xfail=$((xfail+1))
          echo "  [xfail] ${node}  param '${param}' — baselined, see audio-param-sweep-expected.txt" >> "$xfail_out"
        else
          newfail=$((newfail+1))
          detail_lines+=("new failure: '${node}|${param}' — add a line to audio-param-sweep-expected.txt if this is another rig blind spot, otherwise fix the dropped mailbox push")
        fi
      elif [[ "$line" =~ \[pass\][[:space:]]+(.+[^[:space:]])[[:space:]]+param\ \'([^\']+)\'\ reaches\ audio\ thread ]]; then
        node="${BASH_REMATCH[1]}"; param="${BASH_REMATCH[2]}"
        if grep -qF "${node}|${param}|" "$EXPECTED_FILE"; then
          nowpass=$((nowpass+1))
          detail_lines+=("now passes: '${node}|${param}' — delete its line from audio-param-sweep-expected.txt")
        fi
      elif [[ "$line" =~ ^[[:space:]]*\[FAIL\] ]]; then
        # A [FAIL] line that isn't the per-param "reaches audio thread"
        # shape (e.g. a save/load round-trip failure) — the expectations
        # file only covers that one shape, so this is always a real failure.
        newfail=$((newfail+1))
        detail_lines+=("new failure (non-param-reach shape): $line")
      fi
    done < "$out"

    cat "$xfail_out"
    if [ $newfail -gt 0 ] || [ $nowpass -gt 0 ]; then
      echo "  [FAIL]  $name — $xfail xfail, $newfail new failure(s), $nowpass now-passing baselined param(s) — see $out"
      for l in "${detail_lines[@]}"; do echo "          $l"; done
      FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
    else
      echo "  [pass]  $name  — $xfail xfail (baselined blind spots, see $xfail_out), 0 new"
      PASS=$((PASS+1))
    fi
    continue
  fi

  if grep -qE "$FAIL_MARK" "$out"; then
    echo "  [FAIL]  $name — see $out"
    grep -E "$FAIL_MARK" "$out" | sed 's/^/          /'
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
  elif ! grep -qE "$PASS_MARK" "$out"; then
    # No failure marker AND no verdict at all: the fixture never reached its
    # assertion. Silence is not a pass.
    echo "  [FAIL]  $name — no verdict printed, see $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name (no verdict)")
  else
    verdict=$(grep -E "$PASS_MARK" "$out" | tail -1)
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
  echo "known baseline (not a regression unless it changes): PHASE1TEST is"
  echo "occasionally flaky on particle-system timing; rerun once before treating it"
  echo "as a regression. PHASEATEST currently fails on a pre-existing \"Smooth\" node-"
  echo "name collision unrelated to audio work — see the spawned task to fix it."
  exit 1
fi
echo "all checks green."
exit 0
