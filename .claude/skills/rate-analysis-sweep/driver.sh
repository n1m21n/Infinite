#!/usr/bin/env bash
# Frame rate / audio rate analysis sweep. See SKILL.md.
SWEEP_NAME="fps + audio rate analysis sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # Frame limiter: with vsync off, setting a 30fps target must land the frame
  # time in 30-37ms. Catches both "the limiter does nothing" and "the limiter
  # overshoots and costs frames".
  "FPSTEST:62"
)

OBSERVE=(
  # Idle-frame caching over a populated canvas: work= should stop rising and
  # idleStreak= should climb on a static patch. A streak stuck at 0 means the
  # graph re-cooks every frame with no input change - the thing that makes a
  # heavy patch cost full frame time while sitting still.
  "CACHETEST,SHOWCASE:24"
)

# Editor hit-test cost vs node count. ed::End() -> BuildControl walks every
# live node and every live pin once per frame, so it is the one editor cost
# that scales with patch size; a spindump that lands there is
# indistinguishable from a freeze. This measures it rather than guessing.
edperf_scan() {
  local ok=0
  printf '  %-8s %-10s %-10s\n' nodes median_ms max_ms
  for n in 1 50 200 500; do
    local log="/tmp/infinite_edperf_${n}.log"
    env INFINITE_EDPERFTEST="$n" INFINITE_EXITAFTER=40 "$BIN" >"$log" 2>&1
    local rc=$?
    local stats
    stats="$(grep -o 'ed::End=[0-9.]*ms' "$log" | sed 's/[^0-9.]//g' | sort -n | \
      awk '{v[NR]=$1} END {if (NR==0) {print "-", "-"} else {printf "%.2f %.2f", v[int(NR/2)+1], v[NR]}}')"
    printf '  %-8s %s\n' "$n" "$stats"
    if [ $rc -ne 0 ] || [ "${stats%% *}" = "-" ]; then
      echo "  no [edperf] timing lines at n=$n (rc=$rc) - see $log"
      ok=1
    fi
  done
  echo "  read the numbers: cost should grow roughly linearly with node count."
  echo "  A superlinear jump between 200 and 500 is the regression to chase."
  return $ok
}
SWEEP_EXTRA=edperf_scan

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
