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

# --- names (C2): a hand-written patch canonicalizes to the numeric twin ---
N="$ROOT/tests/headless/names"
"$BIN" --canonicalize "$N/numeric.inf" "$OUT/n_num.inf" >/dev/null 2>&1; check "names: numeric canonicalize" $?
"$BIN" --canonicalize "$N/named.inf" "$OUT/n_named.inf" >/dev/null 2>&1; check "names: named canonicalize" $?
cmp -s "$OUT/n_num.inf" "$OUT/n_named.inf"; check "names: named == numeric (byte-identical)" $?
# CRLF, BOM and trailing spaces on every line must not change the result
sed 's/$/\r/' "$N/named.inf" > "$OUT/crlf.inf"
printf '\xef\xbb\xbf' > "$OUT/bom.inf"; cat "$N/named.inf" >> "$OUT/bom.inf"
sed 's/$/   /' "$N/named.inf" > "$OUT/trail.inf"
for v in crlf bom trail; do
  "$BIN" --canonicalize "$OUT/$v.inf" "$OUT/n_$v.inf" >/dev/null 2>&1
  cmp -s "$OUT/n_num.inf" "$OUT/n_$v.inf"; check "names: $v variant canonicalizes identically" $?
done
# the same picture from both spellings
"$BIN" --frame "$N/numeric.inf" 0 "$OUT/fn.png" >/dev/null 2>&1; "$BIN" --frame "$N/named.inf" 0 "$OUT/fa.png" >/dev/null 2>&1
cmp -s "$OUT/fn.png" "$OUT/fa.png"; check "names: identical --frame PNGs" $?
# a GUI-written file is untouched by the new reader/writer
"$BIN" --canonicalize "$ROOT/assets/examples/patch_1.inf" "$OUT/p1.inf" >/dev/null 2>&1
cmp -s "$OUT/p1.inf" "$N/patch_1.canonical.inf"; check "names: patch_1 load-write byte-identical to baseline" $?
# errors: unknown id, duplicate id, bad id
printf 'infinite-patch 1\nnode 1 Source Shape\n  id a\nend\nnode 2 Utility Output\n  id a\nend\n' > "$OUT/dup.inf"
r=$("$BIN" --validate "$OUT/dup.inf" 2>/dev/null); [ "$(echo "$r" | json "d['errors'][0]['code']")" = "E_DUPLICATE_ID" ]; check "names: duplicate id reported" $?
printf 'infinite-patch 1\nnode 1 Source Shape\n  id 7up\nend\n' > "$OUT/bad.inf"
r=$("$BIN" --validate "$OUT/bad.inf" 2>/dev/null); [ "$(echo "$r" | json "d['errors'][0]['code']")" = "E_BAD_ID" ]; check "names: bad id reported" $?
printf 'infinite-patch 1\nnode 1 Source Shape\n  id shape\nend\nnode 2 Utility Output\nend\ncable out 0 shpae\n' > "$OUT/ref.inf"
r=$("$BIN" --validate "$OUT/ref.inf" 2>/dev/null); rc=$?
[ "$rc" = 3 ] && [ "$(echo "$r" | json "d['errors'][0]['code']")" = "E_BAD_REF" ]; check "names: unknown reference exit 3" $?

# --- key join (C3): every labelled control should know its saved key; the count may only go up ---
BASE="$ROOT/tests/headless/describe_join_baseline.json"
r=$("$BIN" --describe 2>/dev/null)
[ "$(echo "$r" | json "d['join_stats']['keyed'] >= json.load(open('$BASE'))['keyed']")" = "True" ]; check "describe: keyed controls did not drop below the baseline" $?
[ "$(echo "$r" | json "[m['key'] for t in d['types'] if t['type']=='Shape' for m in t['modulatable'] if m['label']=='size x'][0]")" = "sizeX" ]; check "describe: Shape 'size x' is key sizeX" $?
[ "$(echo "$r" | json "[m['key'] for t in d['types'] if t['type']=='Shape' for m in t['modulatable'] if m['label']=='shape'][0]")" = "shapeType" ]; check "describe: Shape dropdown 'shape' is key shapeType (perturbation tier)" $?

# --- describe rows (C4): one row per saved key, dropdown options present ---
[ "$(echo "$r" | json "sum(1 for t in d['types'] for m in t['modulatable'] if 'enum' in m and len(m['enum'])==0)")" = "0" ]; check "describe: no dropdown has an empty option list" $?
for ty in Shape Oscillator Wavetable Reverb FieldPixel LFO "Render 3D" Sampler Mixer "Predictive Modulator"; do
  [ "$(echo "$r" | json "(lambda t: len(t['rows'])==len(t['params']) and all(rw['ui'] and rw['label'] for rw in t['rows'] if rw['modulatable_index'] is not None) and all(len(rw['options'])>0 for rw in t['rows'] if rw['options'] is not None and rw['key'] in [m.get('key') for m in t['modulatable'] if 'enum' in m]))([t for t in d['types'] if t['type']=='$ty'][0])")" = "True" ]; check "describe: $ty rows complete" $?
done
[ "$(echo "$r" | json "[rw['options'][:3] for t in d['types'] if t['type']=='Shape' for rw in t['rows'] if rw['key']=='shapeType'][0]")" = "['Circle', 'Ellipse', 'Rectangle']" ]; check "describe: shapeType lists option names" $?
r2=$("$BIN" --validate "$N/out_of_range.inf" 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r2" | json "sorted(w['code'] for w in d['warnings'])")" = "['W_OUT_OF_RANGE', 'W_OUT_OF_RANGE']" ]; check "validate: W_OUT_OF_RANGE reported, advisory (exit 0)" $?
r2=$("$BIN" --validate "$ROOT/assets/examples/patch_1.inf" 2>/dev/null)
[ "$(echo "$r2" | json "sum(1 for w in d['warnings'] if w['code'] in ('W_OUT_OF_RANGE','W_INTERNAL_PARAM'))")" = "0" ]; check "validate: GUI-saved patch_1 has zero range/internal hits" $?

# --- keyed (C5): controls by key, dropdowns by option name == the numeric twin ---
K="$ROOT/tests/headless/keyed"
"$BIN" --canonicalize "$K/keyed.inf" "$OUT/ck.inf" >/dev/null 2>&1; "$BIN" --canonicalize "$K/numeric.inf" "$OUT/cn.inf" >/dev/null 2>&1
cmp -s "$OUT/ck.inf" "$OUT/cn.inf"; check "keyed: canonicalize identical to numeric twin" $?
"$BIN" --frame "$K/numeric.inf" 0 "$OUT/kn.png" >/dev/null 2>&1; "$BIN" --frame "$K/keyed.inf" 0 "$OUT/kk.png" >/dev/null 2>&1
cmp -s "$OUT/kn.png" "$OUT/kk.png"; check "keyed: identical --frame PNGs" $?
r=$("$BIN" --frame "$K/misspelled.inf" 0 "$OUT/km.png" 2>/dev/null); rc=$?
[ "$rc" = 3 ] && [ ! -e "$OUT/km.png" ]; check "keyed: misspelled key exit 3, nothing rendered" $?
[ "$(echo "$r" | json "[(e['code'],e['line'],e['hint'].split(',')[0]) for e in d['errors']]")" = "[('E_BAD_KEY', 14, 'did you mean: sizeX')]" ]; check "keyed: error names line and nearest key sizeX" $?

rm -rf "$OUT"
exit $fail
