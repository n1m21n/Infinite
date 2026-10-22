#!/usr/bin/env bash
# Runs the live-engine selftests (GUI + real audio device) under a TSan build,
# one at a time, and prints one line per test with its TSan warning count.
# Headless --audio-summary renders audio on the main thread, so only these
# reach the audio-thread vs main-thread races TSan is for.
#
#   cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DINFINITE_SANITIZE=thread
#   cmake --build build-tsan -j8
#   tools/bughunt/tsan_selftests.sh build-tsan/Infinite.app/Contents/MacOS/Infinite OUT_DIR [TEST ...]
#
# Reports land in OUT_DIR/<TEST>.<pid>. Frame tests need INFINITE_EXITAFTER
# to quit; 150 frames covers every one listed. CONVOLUTIONTEST's CPU-cost
# check fails under TSan's ~10x slowdown: read its other lines, not its rc.
set -u
BIN="$1"; OUT="$2"; shift 2
TESTS=("$@")
[ ${#TESTS[@]} -eq 0 ] && TESTS=(AUDIOGRAPHTEST NEWPATCHAUDIOTEST AUDIOLIFECYCLETEST AUDIORECOVERYTEST
   DELETECRASHTEST BYPASSTEST AUDIORINGTEST AUDIOUITEST MODTEST CONVOLUTIONTEST MOLDERTEST
   AUDIOPARAMSWEEPTEST MIXEDSTRESSTEST RECTEST)
mkdir -p "$OUT"
for t in "${TESTS[@]}"; do
   start=$(date +%s)
   env "INFINITE_$t=1" INFINITE_EXITAFTER=150 INFINITE_NO_UPDATE_CHECK=1 \
      TSAN_OPTIONS="log_path=$OUT/$t halt_on_error=0 second_deadlock_stack=1 report_signal_unsafe=0" \
      perl -e 'alarm 900; exec @ARGV' "$BIN" > "$OUT/$t.out" 2>&1
   rc=$?
   n=$(cat "$OUT/$t".[0-9]* 2>/dev/null | grep -c "WARNING: ThreadSanitizer")
   echo "$t rc=$rc secs=$(( $(date +%s) - start )) tsan_warnings=$n"
done
