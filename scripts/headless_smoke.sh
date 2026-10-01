#!/usr/bin/env bash
# End-to-end check of the headless CLI (docs/fix-briefs/headless-engine.md, blocks 1, 2 and 3).
# Spawns Infinite --frame / --render / --audio-summary / bad input and asserts the status JSON,
# the exit code and the files. Usage: scripts/headless_smoke.sh [path/to/Infinite]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/Infinite.app/Contents/MacOS/Infinite}"
PATCH="$ROOT/tests/headless/patch_video.inf"
OUT="$(mktemp -d)"
fail=0
check() { if [ "$2" = "0" ]; then echo "[pass] $1"; else echo "[FAIL] $1"; fail=1; fi; }
json_file() { python3 -c "import json; d=json.load(open('$1')); print($2)"; }
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

# R476: --render --start S carries the audio clock to S, not just the picture (tone begins at 2 s)
if command -v ffmpeg >/dev/null; then
  LT="$ROOT/tests/headless/summary/late_tone.inf"
  "$BIN" --render "$LT" "$OUT/lt0.mp4" --start 0 --duration 1 --fps 30 >/dev/null
  "$BIN" --render "$LT" "$OUT/lt2.mp4" --start 2 --duration 1 --fps 30 >/dev/null
  mv0=$(ffmpeg -i "$OUT/lt0.mp4" -af volumedetect -vn -f null - 2>&1 | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')
  mv2=$(ffmpeg -i "$OUT/lt2.mp4" -af volumedetect -vn -f null - 2>&1 | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')
  python3 -c "import sys; sys.exit(0 if float('${mv0:--999}') < -60 else 1)"; check "render --start 0: late tone not yet audible" $?
  python3 -c "import sys; sys.exit(0 if float('${mv2:--999}') > -20 else 1)"; check "render --start 2: audio starts at 2 s, not 0" $?
fi

# R275: overlapping translucent meshes composite back-to-front whatever their slot order
R3="$ROOT/tests/headless/render3d"
"$BIN" --frame "$R3/translucent_ab.inf" 0 "$OUT/tr_ab.png" >/dev/null
"$BIN" --frame "$R3/translucent_ba.inf" 0 "$OUT/tr_ba.png" >/dev/null
cmp -s "$OUT/tr_ab.png" "$OUT/tr_ba.png"; check "render3d: translucent slot order does not change the picture" $?

# R18: on Linux a run with no display server says so, with the fix, instead of a dialog
if [ "$(uname)" = Linux ]; then
  r=$(env -u DISPLAY -u WAYLAND_DISPLAY "$BIN" --frame "$PATCH" 1.0 "$OUT/nd.png" 2>/dev/null); rc=$?
  [ "$rc" = 5 ] && echo "$r" | grep -q '"E_NO_DISPLAY"' && echo "$r" | grep -q xvfb-run; check "linux: no display -> E_NO_DISPLAY with xvfb-run hint, exit 5" $?
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

# --- film backend (R471): --frames-dir PNG sequence, --node tap with no Output ---
FD="$OUT/fd"; rm -rf "$FD"
rf=$("$BIN" --frames-dir "$ROOT/tests/headless/format/tap.inf" "$FD" --duration 0.3 --fps 10 --node shape --alpha 2>/dev/null)
[ "$(echo "$rf" | json "d['ok'] and d['mode']=='frames' and d['frames']==3")" = "True" ]; check "frames-dir: --node taps a patch with no Output, 3 frames" $?
[ -s "$FD/000000.png" ] && [ -s "$FD/000002.png" ] && [ ! -e "$FD/000003.png" ]; check "frames-dir: numbered PNGs 000000..000002" $?
[ "$(json_file "$FD/frames.json" "d['fps']==10 and d['count']==3 and d['alpha'] and d['premultiplied']==False and d['color_space']=='srgb'")" = "True" ]; check "frames-dir: frames.json describes the sequence" $?
[ "$(echo "$rf" | json "0 < d['frame_stats'][0]['alpha_coverage'] < 100")" = "True" ]; check "frames-dir --alpha: the picture keeps its transparency" $?
rf=$("$BIN" --frames-dir "$ROOT/tests/headless/format/tap.inf" "$FD" --duration 0.1 --fps 10 --node 1 2>/dev/null)
[ "$(echo "$rf" | json "d['frame_stats'][0]['alpha_coverage']")" = "100.0" ]; check "frames-dir without --alpha: opaque PNGs" $?
"$BIN" --frames-dir "$ROOT/tests/headless/format/tap.inf" "$FD" --duration 0.1 --node 9 >/dev/null 2>&1; [ $? = 3 ]; check "frames-dir: --node with no such node exit 3" $?
"$BIN" --frames-dir "$ROOT/tests/headless/format/tap.inf" "$FD" --node 1 >/dev/null 2>&1; [ $? = 2 ]; check "frames-dir: no --duration exit 2" $?
"$BIN" --frame "$ROOT/tests/headless/format/tap.inf" 0 "$OUT/tap.png" >/dev/null 2>&1; [ $? = 3 ]; check "frame without --node still needs an Output" $?
"$BIN" --frame "$ROOT/tests/headless/format/tap.inf" 0 "$OUT/tap.png" --node 1 >/dev/null 2>&1; [ -s "$OUT/tap.png" ]; check "frame --node writes the tapped image" $?

# --- describe actions (R474): the buttons a node draws, with what each one does ---
[ "$(echo "$r" | json "[a['effect'] for t in d['types'] if t['type']=='Sampler' for a in t['actions'] if a['label']=='Load...'][0]")" = "sets a file path (write the key instead)" ]; check "describe: Sampler 'Load...' is a file-path action" $?
[ "$(echo "$r" | json "[a['label'] for t in d['types'] if t['type']=='Shape' for a in t['actions']][:2]")" = "['Circle', 'Ellipse']" ]; check "describe: Shape lists its buttons in draw order" $?
[ "$(echo "$r" | json "sum(1 for t in d['types'] if t['actions'])  > 30")" = "True" ]; check "describe: many node types report actions" $?

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

# --- format doc fixtures (3.4): the three patches docs/reference/patch-format.md shows ---
F="$ROOT/tests/headless/format"
for f in image audio modulation; do
  "$BIN" --validate "$F/$f.inf" >/dev/null 2>&1; check "format: $f.inf validates strictly" $?
done

# --- R41: the authoring skill's example patches, and the skill itself ---
E="$ROOT/assets/examples/authoring"
for f in image audio modulation; do
  "$BIN" --validate "$E/$f.inf" >/dev/null 2>&1; check "authoring example: $f.inf validates strictly" $?
done
"$BIN" --frame "$E/image.inf" 0 "$OUT/ex_image.png" >/dev/null 2>&1; [ -s "$OUT/ex_image.png" ]; check "authoring example: image.inf renders a frame" $?
"$BIN" --frame "$E/modulation.inf" 0.5 "$OUT/ex_mod.png" >/dev/null 2>&1; [ -s "$OUT/ex_mod.png" ]; check "authoring example: modulation.inf renders a frame" $?
"$BIN" --audio-summary "$E/audio.inf" "$OUT/ex_audio.json" --duration 2 >/dev/null 2>&1; [ -s "$OUT/ex_audio.json" ]; check "authoring example: audio.inf measures" $?
# macOS only: the committed skill is generated from the macOS node list (Linux/Windows lack some nodes)
if [ "$(uname)" = Darwin ]; then
  python3 "$ROOT/tools/gen-patch-skill.py" --check --bin "$BIN" >/dev/null 2>&1; check "authoring skill is not stale (tools/gen-patch-skill.py --check)" $?
fi

# --- explain (3.3b): the live graph read back after the load ---
rel() { sed -n '/^Relations/,/^Unconnected/p' | grep -c "^  $1 "; }
P1="$ROOT/assets/examples/patch_1.inf"
r=$("$BIN" --explain "$P1" 2>/dev/null); rc=$?
check "explain: patch_1 exit 0" $rc
for t in cable geo aud note mod pal expr; do
  [ "$(echo "$r" | rel $t)" = "$(grep -c "^$t " "$P1")" ]; check "explain: patch_1 $t lines == $t records in the file" $?
done
r=$("$BIN" --explain "$F/image.inf" 2>/dev/null)
echo "$r" | grep -q '^    shapeType Star (6)$' && echo "$r" | grep -q '^    sizeX 0.6$'; check "explain: changed params by key, dropdown by option name" $?
echo "$r" | grep -q '(default)'; [ $? = 1 ]; check "explain: defaults hidden in text" $?
echo "$r" | grep -q '^  cable Output "out" (2) slot 0 \[in\] <- Shape "shape" (1)$'; check "explain: cable names both ends and the slot" $?
"$BIN" --explain "$F/image.inf" --all 2>/dev/null | grep -q '^    posX 0.5   (default)$'; check "explain --all: defaults listed and marked" $?
r=$("$BIN" --explain "$F/audio.inf" 2>/dev/null)
echo "$r" | grep -q '^  note Wavetable "synth" (2) slot 0 \[notes\] <- Random Note Generator "notes" (1)$' &&
  echo "$r" | grep -q '^  aud Audio Out "speakers" (3) slot 0 \[audio\] <- Wavetable "synth" (2)$'; check "explain: audio and note wires carry slot names" $?
r=$("$BIN" --explain "$F/modulation.inf" 2>/dev/null)
echo "$r" | grep -q '^  mod Shape "shape" (1) sizeX <- LFO "lfo" (2) out 0, absolute, depth 1, range 0.01..1$'; check "explain: mod shows key, source and resolved range" $?
echo "$r" | grep -q '^  expr Shape "shape" (1) rotation = sin(t) \* 90$'; check "explain: expr shows key and text" $?
echo "$r" | grep -q '^    sizeX .*(live value, driven by mod)$'; check "explain: a driven param is marked, not shown as authored" $?
r=$("$BIN" --explain "$F/modulation.inf" --json 2>/dev/null)
[ "$(echo "$r" | python3 -c "
import sys,json
g=json.loads(sys.stdin.readline()); st=json.loads(sys.stdin.readline())
sh=[n for n in g['nodes'] if n['id']=='shape'][0]; p={q['key']:q for q in sh['params']}
print(st['ok'] and st['mode']=='explain' and len(g['nodes'])==3 and sorted(x['kind'] for x in g['relations'])==['cable','expr','mod']
      and p['f sizeX'].get('driven')=='mod' and p['f rotation'].get('driven')=='expr' and p['f posX']['default'] and g['unconnected']==[])")" = "True" ]; check "explain --json: graph line then status line" $?
# R473: depth beyond the file - effective bypass, mod-loop lag, geometry stats, material, clips
r=$("$BIN" --explain "$ROOT/tests/headless/explain/depth.inf" --lenient 2>/dev/null)
echo "$r" | grep -q '^    > bypassed: silent (nothing passes through)$'; check "explain depth: bypassed source says silent" $?
echo "$r" | grep -q '^    > bypass ignored: 2 inputs, nothing to pass through$'; check "explain depth: ignored bypass is reported" $?
[ "$(echo "$r" | grep -c 'in a modulation loop: one-frame lag$')" = 2 ]; check "explain depth: both mod-loop edges flagged" $?
echo "$r" | grep -q '^    > geometry: 96 vertices, 108 faces, world bbox (1.5 -0.5 -0.5) to (2.5 0.5 0.5)$'; check "explain depth: geometry counts and world bbox" $?
echo "$r" | grep -q '^    > material: lit, colour 1 0 0, '; check "explain depth: effective material" $?
echo "$r" | grep -q '^  clip video lane 1 "intro" at beat 3, 4 beats, source Cube "cube" (1)$'; check "explain depth: arrangement clip listed" $?
r=$("$BIN" --explain "$S/misspelled.inf" 2>/dev/null); rc=$?
[ "$rc" = 3 ] && [ "$(echo "$r" | wc -l | tr -d ' ')" = 1 ]; check "explain: strict, a misspelled param exits 3 with no graph" $?
"$BIN" --explain "$S/misspelled.inf" --lenient 2>/dev/null | grep -q '^Relations ('; check "explain --lenient: explains it anyway" $?
"$BIN" --explain "$OUT/missing.inf" >/dev/null 2>&1; [ $? = 3 ]; check "explain: missing patch exit 3" $?
"$BIN" --explain >/dev/null 2>&1; [ $? = 2 ]; check "explain: no patch exit 2" $?

# --- summaries (block 2): audio numbers and per-frame image stats ---
M="$ROOT/tests/headless/summary"
INFINITE_AUDIOSUMMARYTEST=1 "$BIN" 2>/dev/null | tail -1 | grep -q '^AUDIOSUMMARYTEST: PASS$'; check "summary: analyzer self-test (1 kHz sine at -20 dBFS reads -20 LUFS)" $?
r=$("$BIN" --audio-summary "$M/sine.inf" "$OUT/sine.json" --duration 2 --wav "$OUT/sine.wav" 2>/dev/null); rc=$?
check "summary: sine exit 0" $rc
[ "$(echo "$r" | json "d['mode']=='audio-summary' and 900 < d['audio_summary']['loudest_band_hz'] < 1200 and d['audio']['frames']==96000 and d['warnings']==[]")" = "True" ]; check "summary: sine loudest band is 1 kHz, 2 s at 48 kHz, no warnings" $?
[ "$(python3 -c "
import json; d=json.load(open('$OUT/sine.json'))
print(len(d['spectrum']['mean_db'])==32 and len(d['loudness_per_second'])==2 and d['source']=='mix' and abs(d['integrated_lufs']-d['sample_peak_dbfs'])<0.2)")" = "True" ]; check "summary: out.json has 32 bands, per-second loudness, LUFS == sine peak" $?
[ "$(wc -c < "$OUT/sine.wav" | tr -d ' ')" -gt 380000 ]; check "summary: --wav written" $?
"$BIN" --audio-summary "$M/sine.inf" "$OUT/sine2.json" --duration 2 >/dev/null 2>&1; cmp -s "$OUT/sine.json" "$OUT/sine2.json"; check "summary: two runs give the same file" $?
r=$("$BIN" --audio-summary "$M/clipping.inf" "$OUT/clip.json" --duration 1 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "[w['code'] for w in d['warnings']]==['W_CLIPPING'] and d['audio_summary']['clipped_percent'] > 10")" = "True" ]; check "summary: over full scale warns W_CLIPPING, still exit 0" $?
r=$("$BIN" --audio-summary "$M/silent.inf" "$OUT/silent.json" --duration 1 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "[w['code'] for w in d['warnings']]==['W_SILENT'] and d['audio_summary']['integrated_lufs'] is None")" = "True" ]; check "summary: silence warns W_SILENT, loudness null" $?
r=$("$BIN" --audio-summary "$M/no_audio.inf" "$OUT/na.json" 2>/dev/null); rc=$?
[ "$rc" = 3 ] && [ ! -e "$OUT/na.json" ] && echo "$r" | grep -q '"E_NO_AUDIO"'; check "summary: no audio terminal exit 3 E_NO_AUDIO, nothing written" $?
r=$("$BIN" --audio-summary "$PATCH" "$OUT/one.json" --duration 1 --output 15 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "d['source']=='node' and d['source_node']==15 and len(d['sinks'])==2")" = "True" ]; check "summary: --output picks one of two audio terminals" $?
"$BIN" --audio-summary "$PATCH" "$OUT/x.json" --output 7 >/dev/null 2>&1; [ $? = 3 ]; check "summary: --output on a non-terminal exit 3" $?
"$BIN" --audio-summary "$M/sine.inf" >/dev/null 2>&1; [ $? = 2 ]; check "summary: no out.json exit 2" $?
"$BIN" --frame "$M/black.inf" 0 "$OUT/x.png" --wav "$OUT/x.wav" >/dev/null 2>&1; [ $? = 2 ]; check "summary: --wav outside --audio-summary exit 2" $?

r=$("$BIN" --frame "$M/black.inf" 0,1 "$OUT/black/" --contact-sheet "$OUT/black/sheet.png" 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "[f['black_percent'] for f in d['frame_stats']]==[100.0,100.0] and [w['code'] for w in d['warnings']]==['W_BLACK_FRAME','W_BLACK_FRAME']")" = "True" ]; check "summary: black patch reports 100 % black and W_BLACK_FRAME per frame" $?
r=$("$BIN" --frame "$PATCH" 0,1,2 "$OUT/cs/" --contact-sheet "$OUT/cs/sheet.png" 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "len(d['frame_stats'])==3 and all(len(f['luma_histogram'])==16 and abs(sum(f['luma_histogram'])-1)<0.01 and f['black_percent']<100 for f in d['frame_stats']) and d['contact_sheet']['cells']==3 and d['contact_sheet']['cell_width']>=480 and not any(w['code']=='W_BLACK_FRAME' for w in d['warnings'])")" = "True" ]; check "summary: frame_stats per frame, 16-bin histogram sums to 1, sheet cells >= 480 px" $?
[ "$(python3 -c "
import struct; b=open('$OUT/cs/sheet.png','rb').read()
print(b[12:16]==b'IHDR' and b[37:41]==b'sRGB' and struct.unpack('>I',b[16:20])[0] >= 960)")" = "True" ]; check "summary: contact sheet is an sRGB-tagged PNG" $?
"$BIN" --render "$PATCH" "$OUT/x.mp4" --contact-sheet "$OUT/x.png" >/dev/null 2>&1; [ $? = 2 ]; check "summary: --contact-sheet outside --frame exit 2" $?

# R40: two processes render a patch with a random note source to the same bytes (the note pump that
# runs without a device used to leave the generator mid-sequence, so the first note varied run to run)
python3 "$ROOT/tools/determinism-check.py" "$ROOT/tests/headless/format/audio.inf" --binary "$BIN" --times 0,1 --audio-seconds 1 >/dev/null; check "determinism: random-note synth patch renders identically twice" $?
python3 "$ROOT/tools/determinism-check.py" "$PATCH" --binary "$BIN" --times 0,1 --audio-seconds 1 >/dev/null; check "determinism: the 40-node demo patch (modulators, Wave Terrain, effects) renders identically twice" $?

rm -rf "$OUT"
exit $fail
