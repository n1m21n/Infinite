#!/usr/bin/env bash
# B8 Media I/O test clips (docs/plans/perf/benchmark-suite.md §4).
#
# Deterministic synthetic clips: ffmpeg testsrc2 (moving pattern + frame
# counter), H.264, fixed GOP and pixel format so decode cost is comparable
# run to run. Four clips per resolution, 2.0-2.75 s, so even a 300-frame run
# crosses each clip's loop boundary, and with different durations so, when
# several play at once, their loop wraps (reader restarts) do not line up.
#
# Output goes to bench/media/ (gitignored). Never commit these; never use
# website/ or assets/ videos for the bench.
#
#   scripts/bench/b8_make_clips.sh            # 1080 + 2160, H.264
#   B8_HEVC=1 scripts/bench/b8_make_clips.sh  # also b8_<res>p30_hevc_<i>.mp4
#   B8_FORCE=1 scripts/bench/b8_make_clips.sh # regenerate existing clips
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${INFINITE_BENCH_B8MEDIA:-$ROOT/bench/media}"
FFMPEG="${FFMPEG:-$(command -v ffmpeg || true)}"
if [[ -z "$FFMPEG" ]]; then
  echo "b8_make_clips: ffmpeg not found (brew install ffmpeg / apt install ffmpeg)" >&2
  exit 1
fi
mkdir -p "$OUT"

DURATIONS=(2.0 2.25 2.5 2.75)

make_clip() { # <w> <h> <duration> <codec args...> -- <out>
  local w="$1" h="$2" d="$3" out="${@: -1}"
  shift 3
  local args=("${@:1:$#-1}")
  if [[ -s "$out" && "${B8_FORCE:-0}" != 1 ]]; then
    return
  fi
  "$FFMPEG" -nostdin -hide_banner -loglevel error -y \
    -f lavfi -i "testsrc2=size=${w}x${h}:rate=30:duration=${d}" \
    "${args[@]}" -g 30 -keyint_min 30 -sc_threshold 0 -bf 0 \
    -pix_fmt yuv420p -r 30 -an -movflags +faststart "$out"
  echo "  $out"
}

for res in 1080 2160; do
  if [[ "$res" == 1080 ]]; then w=1920; h=1080; else w=3840; h=2160; fi
  for i in 0 1 2 3; do
    make_clip "$w" "$h" "${DURATIONS[$i]}" -c:v libx264 -preset medium -crf 20 -profile:v high \
      "$OUT/b8_${res}p30_${i}.mp4"
    if [[ "${B8_HEVC:-0}" == 1 ]]; then
      make_clip "$w" "$h" "${DURATIONS[$i]}" -c:v libx265 -preset medium -crf 22 -tag:v hvc1 -x265-params log-level=error \
        "$OUT/b8_${res}p30_hevc_${i}.mp4"
    fi
  done
done
echo "b8_make_clips: clips in $OUT"
