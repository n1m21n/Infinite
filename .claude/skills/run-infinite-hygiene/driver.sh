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
  "AUDIOLIFECYCLETEST:8"
  "AUDIORECOVERYTEST:8"
  "SAMPLERDRAGTEST:600"
  "MEDIADRAGTEST:600"
  "PLUGINDRAGTEST:600"
  # Headless (exits before glfwInit), so the frame budget is irrelevant to it -
  # it polls a real asynchronous AU instantiation on its own bounded timer.
  "PLUGINSCANTEST:1"
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
  echo "known baseline (not a regression unless it changes): PHASE1TEST is"
  echo "occasionally flaky on particle-system timing; rerun once before treating it"
  echo "as a regression. PHASEATEST currently fails on a pre-existing \"Smooth\" node-"
  echo "name collision unrelated to audio work — see the spawned task to fix it."
  echo "known baseline: AUDIOPARAMSWEEPTEST currently reports [FAIL] on one Dynamics"
  echo "param (sidechainExternal — every audio input slot a candidate has gets the"
  echo "identical drive-tone buffer aliased in, main and sidechain alike, so"
  echo "switching which pin the detector reads from can never change the signature),"
  echo "two Delay params (sync, rateDiv — both gate the base delay time itself, the"
  echo "very knob that would need to differ to observe a change, so unlike"
  echo "feedback/tone/pan/ducking they can't be gated behind a short-path"
  echo "prerequisite), the same sync/rateDiv pair on Chorus, Flanger, Phaser and"
  echo "Stutter (identical structural reason, same MusicTime table), and three Reverb"
  echo "params (decay, damping, predelay — all only change what the FDN's 8 lines"
  echo "*write*, and a line's own read trails its write by ~600-1100 samples at the"
  echo "default size, longer than the sweep's post-alteration measurement window)."
  echo "All hand-confirmed correct — see EffectDefs.cpp's comments on each param and"
  echo ".claude/skills/audio-node-sweep/SKILL.md's blind-spots section."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all nine Sample"
  echo "Player params (pitch, finetune, speed, start, end, volume, loop, reverse,"
  echo "pingpong - registered type name is still \"Sampler\"). The sweep's generic rig"
  echo "constructs a bare throwaway instance with no file loaded, and an unloaded"
  echo "Sample Player is silent by design (matching Audio File's \"no file loaded\""
  echo "behaviour) - with nothing playing, no param can ever produce a measurable"
  echo "difference in the rendered signature. Confirmed correct by loading a real"
  echo "sample and checking each param (plus reverse/pingpong/end behaviour and the"
  echo "record input) audibly/visibly changes playback; see SAMPLERTEST inside"
  echo "RunSamplerFixture in main.cpp, and SamplerNode.cpp's ProcessBlock."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Note Router's"
  echo "probability (redistributes events across its multiple output ports, but the"
  echo "sweep's generic rig only ever reads port 0 via NoteOutbox()) and on Note"
  echo "Capturer's loop/quantizeDiv (both only take effect via an explicit"
  echo "record/play transport command the generic rig never issues, so the node"
  echo "never leaves its idle state during the probe). Confirmed correct by reading"
  echo "NoteNodes.cpp's AudioNoteRouterNode::NoteOutbox(int) and"
  echo "AudioNoteCapturerNode::ProcessBlock's mCommand switch."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on every Drum"
  echo "Sequencer param (all 8 lanes' volume/pan/pitch/decay/transient/start/"
  echo "end/swing/mute/solo/choke/steps, plus rate/steps/swing/volume/run and"
  echo "the four global offset knobs: transient/decay/pitch/pan)."
  echo "Same root cause as Sampler above: the sweep's generic rig builds a bare"
  echo "throwaway instance with no lane sample loaded, and a lane with nothing"
  echo "loaded is silent by design (docs/plans/audio/drum-sequencer-v2-prompt.md"
  echo "§2) - with nothing playing, no param can produce a measurable difference"
  echo "in the rendered signature. Confirmed correct with real loaded samples in"
  echo "RunDrumSequencerFixture (DRUMSEQTEST) in main.cpp, which exercises every"
  echo "one of these params (timing, swing per-lane and global, velocity, choke,"
  echo "solo/mute, decay, start/end range, global offset composition, voice-"
  echo "steal, transport agreement, run toggle) against real audio and passes."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Plugin's bypass"
  echo "and map_slots. This one is by design, not a rig limitation: a hosted"
  echo "plugin's parameters deliberately do NOT go through ParamMailbox - they are"
  echo "pushed straight to the plugin with Platform::PluginSetParameter, because the"
  echo "plugin owns its own parameter smoothing and one mailbox slot per mapped"
  echo "param would blow past ParamMailbox's 64-slot ceiling (see AudioPluginNode.h)."
  echo "bypass does reach the audio thread, through its own atomic rather than the"
  echo "mailbox the sweep watches, and map_slots is a serialization-only count of"
  echo "how many mapping rows to write - it has no audio meaning at all. The sweep"
  echo "also builds a bare instance with no plugin loaded, which renders pass-through"
  echo "by design, so nothing it can set could change the signature either way."
  echo "Confirmed by INFINITE_PLUGINSCANTEST (real AU: instantiate, render, set and"
  echo "read a parameter, save/restore fullState) and INFINITE_PLUGINDRAGTEST."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Limiter's"
  echo "release. Confirmed by direct simulation of the kernel's own math against"
  echo "the sweep's exact probe/block timing: the probe tone is two channels in"
  echo "phase quadrature (L=sin, R=cos), so the stereo-linked per-sample level"
  echo "ripples with a ~40-sample period, but this kernel's peak detector takes"
  echo "the max over its full ~72-sample lookahead window (wider than that"
  echo "ripple), which lands on a fixed value once warmed up - nothing left for"
  echo "release to chase. threshold has explicit testCandidates and passes; see"
  echo "EffectDefs.cpp's comment on Limiter's release param."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all eight of Note"
  echo "Stack's semiN params (semi0..semi7 - enabledN itself passes for all eight)."
  echo "Same root cause as Note Router/Sampler above: each semitone only affects the"
  echo "voice it belongs to, and the sweep's generic rig builds a bare throwaway"
  echo "instance with every enabledN false by default, so no voice is sounding for"
  echo "any semiN to be observed on. Confirmed correct by RunNoteStackFixture"
  echo "(DSPTEST notestack ...) in main.cpp, which enables real voices and checks"
  echo "the exact transposed pitches, the dedupe rule, and the out-of-range drop -"
  echo "all against the live semitone values."
  exit 1
fi
echo "all checks green."
exit 0
