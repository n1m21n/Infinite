#!/usr/bin/env bash
# Runs the INFINITE_BENCH suite (docs/plans/perf/benchmark-suite.md) and
# collects every BENCH_JSON line into bench/results/<machine>/<date>-<sha>.jsonl.
#
# Usage: scripts/bench/run_all.sh [--soak] [--quiet] [--app <path-to-Infinite.app>]
#
# --quiet pauses the semi-brain watch daemon (a launchd agent that runs
# sync_brain.py about once a minute while transcripts change) for the whole
# run and restores it on exit. Use it for any run that becomes a baseline.
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
QUIET=0

while [[ $# -gt 0 ]]; do
   case "$1" in
      --soak) SOAK=1; shift ;;
      --quiet) QUIET=1; shift ;;
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
# Everything this script prints also lands in a run log next to the results.
exec > >(tee -a "${OUT_FILE%.jsonl}.log") 2>&1

# The app labels BENCH_JSON's "commit" from this, so it is right even when
# the app runs outside the repo.
export INFINITE_BENCH_COMMIT="$SHA"

swap_used() { sysctl -n vm.swapusage 2>/dev/null | sed -E 's/.*used = ([0-9.]+M).*/\1/'; }

# --quiet: boot the brain watch daemon out for the run, restore it on exit.
BRAIN_LABEL="com.infinite.semi-brain.watchd"
BRAIN_PLIST="$HOME/Library/LaunchAgents/$BRAIN_LABEL.plist"
BRAIN_DOMAIN="gui/$(id -u)"
BRAIN_BOOTED_OUT=0
wait_for_sync_brain() {
   local waited=0
   while pgrep -f 'sync_brain.py' > /dev/null; do
      if [[ "$waited" -eq 0 ]]; then echo "  waiting for a running sync_brain.py to finish..."; fi
      if [[ "$waited" -ge 600 ]]; then echo "  sync_brain.py still running after 600s - continuing anyway" >&2; return; fi
      sleep 5; waited=$((waited + 5))
   done
}
restore_brain() {
   if [[ "$BRAIN_BOOTED_OUT" -eq 1 ]]; then
      launchctl bootstrap "$BRAIN_DOMAIN" "$BRAIN_PLIST" 2>/dev/null \
         && echo "run_all.sh: restored $BRAIN_LABEL" \
         || echo "run_all.sh: WARNING could not restore $BRAIN_LABEL - run: launchctl bootstrap $BRAIN_DOMAIN $BRAIN_PLIST" >&2
      BRAIN_BOOTED_OUT=0
   fi
   if [[ "$QUIET" -eq 1 ]]; then echo "run_all.sh: quiet=1 swap_end=$(swap_used)"; fi
}
if [[ "$QUIET" -eq 1 ]]; then
   trap restore_brain EXIT
   if [[ -f "$BRAIN_PLIST" ]] && launchctl print "$BRAIN_DOMAIN/$BRAIN_LABEL" > /dev/null 2>&1; then
      # Let an in-flight sync finish before launchd stops its parent, then
      # catch one that started in between.
      wait_for_sync_brain
      launchctl bootout "$BRAIN_DOMAIN/$BRAIN_LABEL" && BRAIN_BOOTED_OUT=1
      wait_for_sync_brain
      echo "run_all.sh: booted out $BRAIN_LABEL for the run"
   else
      echo "run_all.sh: $BRAIN_LABEL not loaded - nothing to pause"
   fi
   echo "run_all.sh: quiet=1 swap_start=$(swap_used)"
fi

echo "run_all.sh: machine=$MACHINE sha=$SHA -> $OUT_FILE"

# run_fixture <label> <exitafter> <env-assignments...>
# FIXTURE_TIMEOUT=<seconds> (set per call, e.g. by B8) kills a run that hangs;
# its label then reports "no BENCH_JSON line".
run_fixture() {
   local label="$1" exitafter="$2"; shift 2
   echo "  -> $label"
   local log
   log="$(mktemp)"
   env "$@" INFINITE_EXITAFTER="$exitafter" "$APP" -ApplePersistenceIgnoreState YES > "$log" 2>&1 &
   local pid=$! wd=""
   if [[ -n "${FIXTURE_TIMEOUT:-}" ]]; then
      # Watchdog's own output goes nowhere so it never holds a pipe open.
      ( sleep "$FIXTURE_TIMEOUT"; kill -9 "$pid" 2>/dev/null && echo "     watchdog: killed after ${FIXTURE_TIMEOUT}s" >&2 ) > /dev/null 2>&1 &
      wd=$!
   fi
   wait "$pid" || true
   if [[ -n "$wd" ]]; then
      { kill "$wd" && wait "$wd"; } 2>/dev/null || true
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
# B2 Heavy visuals sweep per benchmark-suite.md §4 (scales s/m/l, static and animated variants).
for scale in s m l; do
   run_fixture "B2_heavy_visuals scale=$scale,anim=1" 160 \
      INFINITE_BENCH_B2SCALE="$scale" INFINITE_BENCH_B2ANIM=1
   run_fixture "B2_heavy_visuals scale=$scale,anim=0" 160 \
      INFINITE_BENCH_B2SCALE="$scale" INFINITE_BENCH_B2ANIM=0
done
# Per-node GPU split (Render 3D vs each effect type) at the heaviest scale.
run_fixture "B2_heavy_visuals scale=l,anim=1,gpunodes=1" 160 \
   INFINITE_BENCH_B2SCALE=l INFINITE_BENCH_B2ANIM=1 INFINITE_BENCH_B2GPUNODES=1

echo "B3 Live performance"
# B3 Live performance fixture per benchmark-suite.md §4.
# B3_FRAMES defaults to 600 frames; can be overridden via B3_FRAMES for quick runs.
B3_FRAMES="${B3_FRAMES:-600}"
B3_EXIT=$((B3_FRAMES + 50))
run_fixture "B3_live_performance scale=s,buf=256" "$B3_EXIT" \
   INFINITE_BENCH_B3SCALE=s INFINITE_BENCH_B3BUFFER=256 INFINITE_BENCH_B3FRAMES="$B3_FRAMES"

echo "B4 Complex 3D scenes"
# GL timer queries stall the CPU on macOS (glEndQuery flushes and waits), so
# the sweeps run with them off and report honest frame_ms. One timed run at
# l splits Render 3D's GPU time by pass. Object-count sweep at the default
# 2048 shadow map, shadow-quality sweep at m, then the cached (static) cost.
for scale in s m l; do
   run_fixture "B4_complex_3d scale=$scale,shadow=2048,anim=1,gputimers=0" 160 \
      INFINITE_BENCH_B4SCALE="$scale" INFINITE_BENCH_B4SHADOW=2048 INFINITE_BENCH_GPUTIMERS=0
done
for shadow in off 1024 4096; do
   run_fixture "B4_complex_3d scale=m,shadow=$shadow,anim=1,gputimers=0" 160 \
      INFINITE_BENCH_B4SCALE=m INFINITE_BENCH_B4SHADOW="$shadow" INFINITE_BENCH_GPUTIMERS=0
done
run_fixture "B4_complex_3d scale=l,shadow=2048,anim=0,gputimers=0" 160 \
   INFINITE_BENCH_B4SCALE=l INFINITE_BENCH_B4ANIM=0 INFINITE_BENCH_GPUTIMERS=0
run_fixture "B4_complex_3d scale=l,shadow=2048,anim=1,passes=1" 160 \
   INFINITE_BENCH_B4SCALE=l INFINITE_BENCH_B4PASSES=1

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
for n in 50 100 200 400; do
   run_fixture "B5_fundamentals_stages n=$n" 160 INFINITE_BENCH_B5STAGES="$n"
done
run_fixture "B5_fundamentals_startup" 20 INFINITE_BENCH_B5STARTUP=1
for n in 50 100 200 400; do
   run_fixture "B5_fundamentals_loadsave n=$n" 60 INFINITE_BENCH_B5LOADSAVE="$n"
done
for n in 50 100 200 400; do
   run_fixture "B5_fundamentals_undo n=$n" 60 INFINITE_BENCH_B5UNDO="$n"
done

echo "B6 Canvas navigation"
# B6 Canvas navigation per benchmark-suite.md §4: programmatic pan/zoom/drag/
# dropdown over a wired grid, never OS-level UI scripting. Leave Infinite in
# front: `unfocused=1` or `unpaced=1` in the variant means the run is not a
# baseline and its targets report null. B6_FRAMES can be lowered for quick runs.
B6_FRAMES="${B6_FRAMES:-600}"
B6_EXIT=$((B6_FRAMES + 50))
run_fixture "B6_canvas_nav n=300,mode=all" "$B6_EXIT" \
   INFINITE_BENCH_B6NODES=300 INFINITE_BENCH_B6MODE=all INFINITE_BENCH_B6FRAMES="$B6_FRAMES" INFINITE_BENCH_GPUTIMERS=0
run_fixture "B6_canvas_nav n=300,mode=all,collapsed=1" "$B6_EXIT" \
   INFINITE_BENCH_B6NODES=300 INFINITE_BENCH_B6MODE=all INFINITE_BENCH_B6COLLAPSED=1 INFINITE_BENCH_B6FRAMES="$B6_FRAMES" INFINITE_BENCH_GPUTIMERS=0
for n in 200 400; do
   run_fixture "B6_canvas_nav n=$n,mode=pan" "$B6_EXIT" \
      INFINITE_BENCH_B6NODES="$n" INFINITE_BENCH_B6MODE=pan INFINITE_BENCH_B6FRAMES="$B6_FRAMES" INFINITE_BENCH_GPUTIMERS=0
done
# Unpaced ceiling: vsync off, so frame_ms is pure work. Targets report null.
run_fixture "B6_canvas_nav n=300,mode=pan,vsync=0" "$B6_EXIT" \
   INFINITE_BENCH_B6NODES=300 INFINITE_BENCH_B6MODE=pan INFINITE_BENCH_B6VSYNC=0 INFINITE_BENCH_B6FRAMES="$B6_FRAMES" INFINITE_BENCH_GPUTIMERS=0

if [[ "$SOAK" -eq 1 ]]; then
   echo "B7 Soak and thermal (--soak)"
   skip "B7_soak"
else
   echo "B7 Soak and thermal: skipped (pass --soak to include)"
fi

echo "B8 Media I/O"
# B8 Media I/O per benchmark-suite.md §4: looping H.264 clips into Outputs,
# plus projector windows, camera and Syphon/Spout Out. Clips are generated
# into bench/media/ (gitignored) on first use; never website/ or assets/
# videos. The camera is only opened if permission was already granted
# ("camera":"skipped" otherwise - the bench never raises the dialog). Leave
# Infinite in front: unfocused=1 / unpaced=1 / overlap=1 mean "not a
# baseline". B8_FRAMES (default 300) can be raised for longer runs.
B8_FRAMES="${B8_FRAMES:-300}"
B8_EXIT=$((B8_FRAMES + 50))
B8_MEDIA="$REPO_ROOT/bench/media"
if [[ ! -s "$B8_MEDIA/b8_2160p30_3.mp4" ]]; then
   INFINITE_BENCH_B8MEDIA="$B8_MEDIA" "$SCRIPT_DIR/b8_make_clips.sh" || echo "     b8_make_clips.sh failed - B8 runs will report setup FAIL" >&2
fi
b8() { # b8 <clips> <res> <windows> <camera> <syphon>
   FIXTURE_TIMEOUT=240 run_fixture "B8_media_io clips=$1,res=$2,windows=$3,camera=$4,syphon=$5" "$B8_EXIT" \
      INFINITE_BENCH_B8=1 INFINITE_BENCH_B8MEDIA="$B8_MEDIA" INFINITE_BENCH_B8FRAMES="$B8_FRAMES" \
      INFINITE_BENCH_B8CLIPS="$1" INFINITE_BENCH_B8RES="$2" INFINITE_BENCH_B8WINDOWS="$3" \
      INFINITE_BENCH_B8CAMERA="$4" INFINITE_BENCH_B8SYPHON="$5"
}
b8 2 1080 0 0 0
b8 4 1080 0 0 0
b8 2 2160 0 0 0
b8 4 2160 0 0 0
b8 2 1080 2 0 0
b8 2 1080 3 0 0
b8 2 1080 0 1 0
b8 2 1080 0 0 1
b8 4 2160 3 1 1

echo "B9 Memory footprint"
# B9 Memory footprint per benchmark-suite.md §4 (B2 and B4 scenes at scale l, animated).
# B9_FRAMES defaults to 600 frames per the plan; can be overridden via B9_FRAMES for quick runs.
B9_FRAMES="${B9_FRAMES:-600}"
B9_EXIT=$((B9_FRAMES + 50))
run_fixture "B9_memory_footprint scene=b2,scale=l,anim=1" "$B9_EXIT" \
   INFINITE_BENCH_B9SCENE=b2 INFINITE_BENCH_B9FRAMES="$B9_FRAMES"
run_fixture "B9_memory_footprint scene=b4,scale=l,anim=1" "$B9_EXIT" \
   INFINITE_BENCH_B9SCENE=b4 INFINITE_BENCH_B9FRAMES="$B9_FRAMES"

echo "B10 Offline render and A/V sync"
skip "B10_offline_av_sync"

echo "Done. Results: $OUT_FILE"
echo "Compare against a baseline with: scripts/bench/compare.py <baseline.jsonl> $OUT_FILE"
