#!/usr/bin/env bash
# End-to-end check of the headless CLI (docs/fix-briefs/headless-engine.md, block 1).
# Spawns Infinite --frame / --render / bad input and asserts the status JSON,
# the exit code and the files. Usage: scripts/headless_smoke.sh [path/to/Infinite]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/Infinite.app/Contents/MacOS/Infinite}"
PATCH="$ROOT/tests/headless/patch_video.inf"
OUT="$(mktemp -d)"
fail=0
check() { if [ "$2" = "0" ]; then echo "[pass] $1"; else echo "[FAIL] $1"; fail=1; fi; }
json() { python3 -c "import sys,json; d=json.loads(sys.stdin.read().strip().splitlines()[-1]); print($1)"; }

r=$("$BIN" --frame "$PATCH" 1.0 "$OUT/f.png"); rc=$?
check "frame exit 0" $rc
[ "$(echo "$r" | json "d['ok']")" = "True" ]; check "frame json ok" $?
[ -s "$OUT/f.png" ]; check "frame png written" $?

r=$("$BIN" --frame "$PATCH" 0,0.5 "$OUT/frames/"); rc=$?
[ "$rc" = 0 ] && [ "$(ls "$OUT/frames" | wc -l | tr -d ' ')" = 2 ]; check "frame dir two pngs" $?

r=$("$BIN" --render "$PATCH" "$OUT/r.mp4" --duration 2 --fps 30); rc=$?
check "render exit 0" $rc
[ "$(echo "$r" | json "d['frames']")" = "60" ]; check "render 60 frames" $?
[ "$(echo "$r" | json "d['audio']['sample_rate']")" = "48000" ]; check "render has audio" $?
if command -v ffprobe >/dev/null; then
  ffprobe -v error -show_entries stream=codec_type -of csv=p=0 "$OUT/r.mp4" | grep -q audio; check "mp4 has audio track" $?
fi

"$BIN" --render "$OUT/missing.inf" "$OUT/x.mp4" >/dev/null; [ $? = 3 ]; check "missing patch exit 3" $?
"$BIN" --frame "$PATCH" abc "$OUT/x.png" >/dev/null; [ $? = 2 ]; check "bad time exit 2" $?
"$BIN" --render "$PATCH" "$OUT/x.avi" >/dev/null; [ $? = 2 ]; check "bad container exit 2" $?
"$BIN" --render "$ROOT/assets/examples/patch_1.inf" "$OUT/x.mp4" >/dev/null; [ $? = 3 ]; check "no Output exit 3" $?

rm -rf "$OUT"
exit $fail
