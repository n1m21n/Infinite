#!/usr/bin/env bash
#
# The subset of src/main.cpp's INFINITE_* self-tests that returns before
# glfwInit() and therefore needs no GPU, no audio device and no window server.
# Used by .github/workflows/build.yml on both platforms.
#
# Two verdict styles live in main.cpp and this script has to respect both
# (see run-infinite-hygiene/SKILL.md, "verdict lines aren't exit codes"):
#
#   INFINITE_DSPTEST         - returns RunDspTest()'s real exit code.
#   INFINITE_AUDIOPDCTEST    - deliberately always returns 0; the printf
#                              verdict line is the only signal.
#
# INFINITE_AUDIOPARAMSWEEPTEST is run but NOT gated: it has a large documented
# baseline of [FAIL] lines for params whose effect the sweep's bare rig cannot
# observe (no sample loaded, note fields outside the outbox signature, ...).
# Gating on it would mean encoding that baseline here and updating it on every
# node change. We only assert it does not crash, which still catches a
# platform layer that dies during node registration.

set -uo pipefail

# Headless/CI: never let UpdateCheck::Start() make a network call.
export INFINITE_NO_UPDATE_CHECK=1

BIN="${1:?usage: headless-tests.sh <path-to-infinite-binary>}"

if [ ! -x "$BIN" ]; then
   echo "FAIL: '$BIN' is not an executable file"
   exit 1
fi

status=0

# --- exit-code gated -------------------------------------------------------

echo "== INFINITE_DSPTEST"
if INFINITE_DSPTEST=1 "$BIN"; then
   echo "   pass"
else
   echo "   FAIL (exit $?)"
   status=1
fi

# Same shape as DSPTEST: returns RunPerfPanelSelfTest()'s exit code. Headless,
# so this is one of the few pieces of the performance matrix that CI can
# actually exercise on Windows.
echo "== INFINITE_PERFMATRIXTEST"
if INFINITE_PERFMATRIXTEST=1 "$BIN"; then
   echo "   pass"
else
   echo "   FAIL (exit $?)"
   status=1
fi

# --- verdict-line gated ----------------------------------------------------
# Each of these always exits 0, so grep the printf line instead.

check_verdict() {
   local var="$1" expect="$2" out
   echo "== $var"
   if ! out="$(env "$var=1" "$BIN" 2>&1)"; then
      echo "   FAIL (crashed, exit $?)"
      printf '%s\n' "$out" | tail -20
      status=1
      return
   fi
   if printf '%s\n' "$out" | grep -q "$expect"; then
      echo "   pass"
   else
      echo "   FAIL (no '$expect' in output)"
      printf '%s\n' "$out" | tail -20
      status=1
   fi
}

check_verdict INFINITE_AUDIOPDCTEST "AUDIOPDCTEST OK"

# Exported-movie A/V sync: pure arithmetic over the same pacing both
# recorders' PTS depend on, so it runs headless here as well as on macOS -
# and the recorders it guards are two separate implementations
# (AVAssetWriter / Media Foundation) with the same frame-counter video clock.
check_verdict INFINITE_RECSYNCTEST "REC SYNC OK"

# The end-to-end half of the same property: writes a real movie through the
# platform recorder with simultaneous tone/flash markers, reopens it with the
# app's own decoders and measures how far apart the two tracks landed. This is
# the only machine evidence anywhere that Media Foundation's muxing keeps
# audio and video together - the macOS AVFoundation path is a separate
# implementation of the same contract.
check_verdict INFINITE_RECEXPORTTEST "REC EXPORT OK"

# RemoveBgNode's background-removal backend (Vision on macOS, ONNX Runtime +
# DirectML on Windows). SKIP is an accepted, non-failing verdict here (see
# RunRemoveBgTest in src/main.cpp) - it just means Platform::SubjectMask
# itself reported no implementation/support on this OS version, not a defect.
# Only a crash or an explicit FAIL verdict fails this job. On the Windows
# runner this is the only automated check that the bundled u2netp model
# actually loads and runs end-to-end (GitHub's Windows runners have no GPU,
# so this also exercises the CPU-EP fallback path, not DirectML).
echo "== INFINITE_REMOVEBGTEST"
if ! out="$(INFINITE_REMOVEBGTEST=1 "$BIN" 2>&1)"; then
   echo "   FAIL (crashed, exit $?)"
   printf '%s\n' "$out" | tail -20
   status=1
elif printf '%s\n' "$out" | grep -qE "REMOVEBGTEST (OK|SKIP)"; then
   printf '%s\n' "$out" | grep "REMOVEBGTEST"
   echo "   pass"
else
   echo "   FAIL (no OK/SKIP verdict)"
   printf '%s\n' "$out" | tail -20
   status=1
fi

# --- crash-only (baseline too large to gate on) ----------------------------

echo "== INFINITE_AUDIOPARAMSWEEPTEST (crash check only)"
if INFINITE_AUDIOPARAMSWEEPTEST=1 "$BIN" > /dev/null 2>&1; then
   echo "   pass (did not crash)"
else
   echo "   FAIL (crashed, exit $?)"
   status=1
fi

echo
if [ "$status" -eq 0 ]; then
   echo "All headless self-tests passed."
else
   echo "Headless self-tests FAILED."
fi
exit "$status"
