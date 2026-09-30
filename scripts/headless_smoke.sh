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

cat > "$OUT/bad.inf" <<'INF'
infinite-patch 1
node 1 Source Shpe
end
node 2 Source Shape
  f sizee 0.5
end
node 3 Utility Output
end
cable 3 0 2
aud 3 0 2
cable 3 5 2
cable 9 0 2
INF
r=$("$BIN" --validate "$OUT/bad.inf"); rc=$?
[ "$rc" = 3 ]; check "validate bad patch exit 3" $?
for code in E_UNKNOWN_TYPE W_UNKNOWN_PARAM E_KIND_MISMATCH E_BAD_SLOT E_DANGLING; do
  echo "$r" | grep -q "\"$code\""; check "validate reports $code" $?
done
echo "$r" | grep -q '"line":2'; check "validate cites line numbers" $?
r=$("$BIN" --validate "$PATCH" --for-render); [ $? = 0 ]; check "validate good patch exit 0" $?
r=$("$BIN" --describe Blend); [ $? = 0 ] && [ "$(echo "$r" | json "d['types'][0]['type']")" = "Blend" ]; check "describe one type" $?
"$BIN" --describe Blnd >/dev/null; [ $? = 3 ]; check "describe unknown type exit 3" $?
r=$("$BIN" --describe); [ "$(echo "$r" | json "d['count'] > 250")" = "True" ]; check "describe lists every type" $?

# A1/A3: a CRLF copy with a UTF-8 BOM reads the same as the original
(printf '\xef\xbb\xbf'; sed 's/$/\r/' "$PATCH") > "$OUT/crlf.inf"
"$BIN" --validate "$OUT/crlf.inf" --for-render >/dev/null; check "CRLF + BOM patch validates" $?

# G1: Blend has two inputs; Render 3D camera/light pins and the env kind are real slots
r=$("$BIN" --describe Blend); [ "$(echo "$r" | json "len(d['types'][0]['inputs'])")" = "2" ]; check "Blend describes 2 inputs" $?
r=$("$BIN" --describe "Render 3D"); [ "$(echo "$r" | json "[i['kind'] for i in d['types'][0]['inputs'] if i['slot'] in (4,8)]")" = "['camera', 'environment']" ]; check "Render 3D camera and env slot kinds" $?

# Topology cases (G1-G13): each .inf must give exactly the codes in its .expect
# ('-' = a clean validate). Errors and warnings are compared together.
for f in "$ROOT"/tests/headless/topology/*.inf; do
  name=$(basename "$f" .inf)
  got=$("$BIN" --validate "$f" --for-render 2>/dev/null | json "' '.join(sorted(set(i['code'] for i in d['errors'] + d['warnings']))) or '-'")
  want=$(tr ' ' '\n' < "${f%.inf}.expect" | grep -v '^$' | sort | tr '\n' ' ' | sed 's/ $//')
  [ "$got" = "$want" ]; check "topology $name ($want)" $?
done

# C0: the probe waits for every node to draw, not a fixed 8 ticks. patch_1 has a
# Wavetable that used to give a false E_BAD_PARAM; run it several times.
ok=0
for i in 1 2 3 4 5 6; do
  r=$("$BIN" --validate "$ROOT/assets/examples/patch_1.inf" 2>/dev/null); [ "$(echo "$r" | json "d['ok']")" = "True" ] && ok=$((ok+1))
done
[ "$ok" = 6 ]; check "patch_1 validates 6/6 (probe barrier)" $?
r=$("$BIN" --describe 2>/dev/null); [ "$(echo "$r" | json "sum(len(t['modulatable']) for t in d['types']) > 1500")" = "True" ]; check "describe registers off-screen nodes too" $?

# C1: strict by default, --lenient opts out. A misspelled param refuses (exit 3),
# names its line and the nearest key; --lenient renders it.
S="$ROOT/tests/headless/strict"
r=$("$BIN" --frame "$S/misspelled.inf" 0 "$OUT/s.png" 2>/dev/null); rc=$?
[ "$rc" = 3 ]; check "strict: misspelled param exit 3" $?
[ ! -e "$OUT/s.png" ]; check "strict: nothing rendered" $?
[ "$(echo "$r" | json "[(e['code'],e['line'],e.get('promoted'),e['hint']) for e in d['errors']]")" = "[('W_UNKNOWN_PARAM', 119, True, \"did you mean 'damping'?\")]" ]; check "strict: error names line and nearest key" $?
r=$("$BIN" --frame "$S/misspelled.inf" 0 "$OUT/s.png" --lenient 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ -s "$OUT/s.png" ]; check "lenient: misspelled param renders, exit 0" $?
[ "$(echo "$r" | json "'W_UNKNOWN_PARAM' in [w['code'] for w in d['warnings']] and not d['errors']")" = "True" ]; check "lenient: still reported as a warning" $?
"$BIN" --validate "$S/misspelled.inf" >/dev/null 2>&1; [ $? = 3 ]; check "validate is strict by default" $?
"$BIN" --validate "$S/misspelled.inf" --lenient >/dev/null 2>&1; [ $? = 0 ]; check "validate --lenient exit 0" $?
# the topology warnings are errors by default, warnings under --lenient
for g in g3_bypass_blend g6_image_cycle g11_open_blend g13_output_empty; do
  "$BIN" --validate "$ROOT/tests/headless/topology/$g.inf" --for-render >/dev/null 2>&1; [ $? = 3 ]; check "strict: $g exit 3" $?
  "$BIN" --validate "$ROOT/tests/headless/topology/$g.inf" --for-render --lenient >/dev/null 2>&1; [ $? = 0 ]; check "lenient: $g exit 0" $?
done
# advisory warnings never block: unused nodes in the real fixtures
"$BIN" --validate "$ROOT/assets/examples/patch_1.inf" >/dev/null 2>&1; [ $? = 0 ]; check "strict: unused nodes stay advisory (patch_1)" $?
# every problem at once: warnings promoted before load AND E_BAD_PARAM found after
r=$("$BIN" --frame "$S/all_at_once.inf" 0 "$OUT/a.png" 2>/dev/null); rc=$?
[ "$rc" = 3 ]; check "strict: all-at-once exit 3" $?
[ "$(echo "$r" | json "sorted(e['code'] for e in d['errors'])")" = "['E_BAD_PARAM', 'W_BYPASS_IGNORED', 'W_UNKNOWN_PARAM']" ]; check "strict: all-at-once lists every problem" $?

rm -rf "$OUT"
exit $fail
