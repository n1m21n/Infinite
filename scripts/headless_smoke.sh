#!/usr/bin/env bash
# End-to-end check of the headless CLI (docs/fix-briefs/headless-engine.md, blocks 1, 2 and 3).
# Spawns Infinite --frame / --render / --audio-summary / bad input and asserts the status JSON,
# the exit code and the files. Usage: scripts/headless_smoke.sh [path/to/Infinite]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-$ROOT/build/Infinite.app/Contents/MacOS/Infinite}"
PATCH="$ROOT/tests/headless/patch_video.inf"
OUT="${SMOKE_OUT:-$(mktemp -d)}"
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

# v0.6 1.1: an offline bounce through a latent effect starts on time. PDC delays the master by the graph's
# output latency (Drive 73, Limiter 80 samples at 48 kHz); the render drops that head, so the click lands
# where it lands dry, in the WAV and in the exported movie.
python3 - "$OUT" <<'PY'
import wave, struct, sys
out, r = sys.argv[1], 48000
f = [0] * r
for i in range(48):
    f[int(0.25 * r) + i] = 12000 if i < 24 else -12000
w = wave.open(out + "/lat_click.wav", "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(r)
w.writeframes(struct.pack("<%dh" % len(f), *f)); w.close()
src = ["infinite-patch 1", "node 1 Modulators Audio File", "  id src", "  s path " + out + "/lat_click.wav", "  b loop 0", "end",
       "node 4 Source Shape", "  id picture", "end"]
for name, fx in (("dry", None), ("drive", "Drive"), ("limiter", "Limiter")):
    L = list(src)
    if fx:
        L += ["node 2 AudioEffects " + fx, "  id fx", "end"]
    L += ["node 3 Utility Audio Out", "  id speakers", "end", "node 5 Utility Output", "  id out", "  b includeAudio 1", "end",
          "cable out 0 picture", "aud speakers audio " + ("fx" if fx else "src"), "aud out 1 " + ("fx" if fx else "src")]
    if fx:
        L += ["aud fx 0 src"]
    open(out + "/lat_%s.inf" % name, "w").write("\n".join(L) + "\n")
PY
for n in dry drive limiter; do
  "$BIN" --audio-summary "$OUT/lat_$n.inf" "$OUT/lat_$n.json" --duration 1 --output 3 --wav "$OUT/lat_$n.wav" >/dev/null 2>&1
done
LATPY='
import array, subprocess, sys, wave
def load(p):
    if p.endswith(".wav"):
        w = wave.open(p); a = array.array("h"); a.frombytes(w.readframes(w.getnframes())); return list(a[0::w.getnchannels()])
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", p, "-ac", "1", "-ar", "48000", "-f", "s16le", "-"], capture_output=True).stdout
    a = array.array("h"); a.frombytes(raw[:len(raw) // 2 * 2]); return list(a)
def lag(dry, wet):  # cross-correlation peak over +-200 samples around the click
    c = int(0.25 * 48000); d = dry[c - 400:c + 800]
    best = max(range(-200, 201), key=lambda k: sum(x * y for x, y in zip(d, wet[c - 400 + k:c + 800 + k])))
    return best
dry = load(sys.argv[1]); tol = int(sys.argv[2])
sys.exit(0 if all(abs(lag(dry, load(p))) <= tol for p in sys.argv[3:]) else 1)
'
python3 -c "$LATPY" "$OUT/lat_dry.wav" 1 "$OUT/lat_drive.wav" "$OUT/lat_limiter.wav"; check "bounce: Drive and Limiter land on the dry click (within 1 sample, --audio-summary)" $?
if command -v ffmpeg >/dev/null; then
  for n in dry drive limiter; do "$BIN" --render "$OUT/lat_$n.inf" "$OUT/lat_$n.mp4" --duration 1 --fps 30 >/dev/null 2>&1; done
  python3 -c "$LATPY" "$OUT/lat_dry.mp4" 2 "$OUT/lat_drive.mp4" "$OUT/lat_limiter.mp4"; check "export: Drive and Limiter land on the dry click in the movie (within 2 samples)" $?
fi

# Spatial Mixer is a terminal like Audio Out: no W_UNUSED_NODE / E_NO_AUDIO for an Oscillator patched only into it
SP="$ROOT/tests/headless/summary/spatial_only.inf"
"$BIN" --validate "$SP" >/dev/null 2>&1; check "spatial-only patch passes strict validate" $?
r=$("$BIN" --audio-summary "$SP" "$OUT/sp.json" --duration 0.5 2>/dev/null)
[ "$(echo "$r" | json "d['ok']")" = "True" ]; check "spatial-only patch measures audio (sink = Spatial Mixer)" $?

# --timeout is checked between ticks; a tick that never returns must still end the job (watchdog, B-A-001)
INFINITE_HEADLESS_TEST_HANG_TICK=1 "$BIN" --audio-summary "$SP" "$OUT/hang.json" --duration 2 --timeout 1 > "$OUT/hang.out" 2>/dev/null &
hpid=$!; hs=$SECONDS
while kill -0 $hpid 2>/dev/null && [ $((SECONDS - hs)) -lt 30 ]; do sleep 1; done
if kill -0 $hpid 2>/dev/null; then kill -9 $hpid; hrc=99; else wait $hpid; hrc=$?; fi
[ "$hrc" = "6" ] && grep -q '"E_TIMEOUT"' "$OUT/hang.out"; check "a hung tick is stopped by the watchdog (exit 6, E_TIMEOUT, ${hrc}/$((SECONDS - hs))s)" $?

# Audio File -> Output direct used to mux the raw file, ignoring volume/gain: volume 0.2 must render ~12 dB under 0.8
if command -v ffmpeg >/dev/null; then
  FV="$ROOT/tests/headless/filevol/file_volume.inf"
  sed "s#@ROOT@#$ROOT#" "$FV" > "$OUT/file_volume_lo.inf"
  sed 's#^  f volume 0.2#  f volume 0.8#' "$OUT/file_volume_lo.inf" > "$OUT/file_volume_hi.inf"
  "$BIN" --render "$OUT/file_volume_lo.inf" "$OUT/fv_lo.mp4" --duration 1 --fps 30 >/dev/null
  "$BIN" --render "$OUT/file_volume_hi.inf" "$OUT/fv_hi.mp4" --duration 1 --fps 30 >/dev/null
  vlo=$(ffmpeg -i "$OUT/fv_lo.mp4" -af volumedetect -vn -f null - 2>&1 | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')
  vhi=$(ffmpeg -i "$OUT/fv_hi.mp4" -af volumedetect -vn -f null - 2>&1 | sed -n 's/.*max_volume: \(-*[0-9.]*\) dB.*/\1/p')
  python3 -c "import sys; sys.exit(0 if 10 < float('${vhi:--999}') - float('${vlo:--999}') < 14 else 1)"; check "render: Audio File volume reaches the movie (0.2 vs 0.8)" $?
fi

# Audio File export: a file far longer than the take used to stall the macOS writer for good
# (audio was appended only up to the picture, the writer waited for audio ahead of it), and the
# AAC settings took only 32-48 kHz (an 8 kHz file failed, 96 kHz threw and killed the app).
python3 - "$OUT" <<'PY'
import wave, sys
for name, rate, ch, secs in (("long", 48000, 1, 600), ("r8k", 8000, 1, 3), ("r96k", 96000, 2, 3)):
    w = wave.open(sys.argv[1] + "/%s.wav" % name, "wb"); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(b"\0\0" * ch * rate * secs); w.close()
PY
for name in long r8k r96k; do
  cat > "$OUT/af_$name.inf" <<INF
infinite-patch 1
node 1 Source Shape
  f width 320
  f height 180
end
node 2 Modulators Audio File
  s path $OUT/$name.wav
  b loop 0
end
node 3 Utility Output
  b includeAudio 1
end
cable 3 0 1
aud 3 1 2
INF
done
r=$("$BIN" --render "$OUT/af_long.inf" "$OUT/af_long.mp4" --duration 120 --fps 30 --timeout 90); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "d['frames']")" = "3600" ]; check "render: a 600 s audio file under a 120 s take finishes" $?
if command -v afinfo >/dev/null; then
  ad=$(afinfo "$OUT/af_long.mp4" 2>/dev/null | sed -n 's/estimated duration: \([0-9.]*\).*/\1/p')
  python3 -c "import sys; sys.exit(0 if abs(float('${ad:-0}') - 120) < 0.05 else 1)"; check "render: that take's audio track is as long as its picture" $?
fi
for name in r8k r96k; do
  "$BIN" --render "$OUT/af_$name.inf" "$OUT/af_$name.mp4" --duration 2 --fps 30 --timeout 30 >/dev/null 2>&1
  check "render: a $name audio file exports" $?
done
rm -f "$OUT/long.wav"

# Saving: a write that fails partway (here a 4 KB file-size limit standing in for a full disk) must
# leave the previous file whole - Save used to truncate the user's only copy in place. And a folder
# with a non-ASCII name saves and opens (Windows read std::string paths in the ANSI code page).
python3 - "$OUT/big.inf" <<'EOF'
import sys
L = ["infinite-patch 1", "node 1 Source Shape", "end"]
for i in range(2, 600):
    L += ["node %d Compositing %s" % (i, ("invert", "opacity", "posterize")[i % 3]), "end", "cable %d 0 %d" % (i, i - 1)]
L += ["node 600 Utility Output", "end", "cable 600 0 599"]
open(sys.argv[1], "w").write("\n".join(L) + "\n")
EOF
"$BIN" --canonicalize "$OUT/big.inf" "$OUT/saved.inf" >/dev/null 2>&1 && cp "$OUT/saved.inf" "$OUT/saved.before"
(trap '' XFSZ; ulimit -f 4; "$BIN" --canonicalize "$OUT/big.inf" "$OUT/saved.inf" >/dev/null 2>&1)
check "save: a write that fails partway reports it" $([ $? -ne 0 ] && echo 0 || echo 1)
cmp -s "$OUT/saved.inf" "$OUT/saved.before" && [ ! -e "$OUT/saved.inf.tmp" ]
check "save: ...and leaves the previous file whole, no temp behind" $?
mkdir -p "$OUT/Müller ñ"
"$BIN" --canonicalize "$OUT/big.inf" "$OUT/Müller ñ/p.inf" >/dev/null 2>&1 && "$BIN" --validate "$OUT/Müller ñ/p.inf" >/dev/null 2>&1
check "save: a patch in a non-ASCII folder saves and opens" $?
rm -rf "${OUT:?}/big.inf" "${OUT:?}/saved.inf" "${OUT:?}/saved.before" "${OUT:?}/Müller ñ"

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
r=$("$BIN" --describe); rc=$?; [ "$rc" = 0 ] && [ "$(echo "$r" | json "d['count'] > 250")" = "True" ]; check "describe lists every type, exit 0" $?
# Bare --describe aborted (exit 134): probing Spatial Mixer's head-track dropdown started CoreMotion, and
# macOS kills a terminal-started process on that call (no motion usage string of its own). HeadTracker
# is off in headless runs (bde3a909); this is the row that would have caught it.
"$BIN" --describe "Spatial Mixer" >/dev/null 2>&1; check "describe Spatial Mixer exit 0 (no CoreMotion in headless)" $?

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

# Field compile errors (re-survey D.md P4): every Field node type with a kernel that calls an
# unknown function. --validate, --explain and the render modes must all name the kernel
# (E_FIELD_COMPILE, the `s code` line, the function name), never blame the cable. The same file
# with bogusfn -> sin must validate clean, so the kernel is the only thing wrong.
FLD="$ROOT/tests/headless/field"
for f in "$FLD"/*.inf; do
  name=$(basename "$f" .inf)
  codeline=$(grep -n '^  s code ' "$f" | cut -d: -f1)
  r=$("$BIN" --validate "$f" 2>/dev/null); rc=$?
  [ "$rc" = 3 ] && [ "$(echo "$r" | json "[(e['code'], e['line'], 'bogusfn' in e['message']) for e in d['errors']]")" = "[('E_FIELD_COMPILE', $codeline, True)]" ]
  check "field $name: validate exit 3, E_FIELD_COMPILE at line $codeline names bogusfn" $?
  sed 's/bogusfn(/sin(/' "$f" > "$OUT/field_ok_$name.inf"
  "$BIN" --validate "$OUT/field_ok_$name.inf" >/dev/null 2>&1; check "field $name: same patch with sin( validates" $?
  r=$("$BIN" --explain "$f" 2>/dev/null); rc=$?
  [ "$rc" = 3 ] && [ "$(echo "$r" | json "[e['code'] for e in d['errors']]")" = "['E_FIELD_COMPILE']" ]
  check "field $name: explain exit 3 with E_FIELD_COMPILE" $?
  case "$name" in
    pixel|primitive|modifier|graph) r=$("$BIN" --frame "$f" 0 "$OUT/field_$name.png" 2>/dev/null); rc=$?; what=frame ;;
    *) r=$("$BIN" --audio-summary "$f" "$OUT/field_$name.json" --duration 0.2 2>/dev/null); rc=$?; what=audio-summary ;;
  esac
  [ "$rc" = 3 ] && [ "$(echo "$r" | json "[e['code'] for e in d['errors']]")" = "['E_FIELD_COMPILE']" ]
  check "field $name: $what exit 3 with E_FIELD_COMPILE, not a cable error" $?
done

# Formula / expr compile errors: --validate never compiled a Formula or Equation Synth
# `s formula`, an `expr` or a `glob`, so a typo passed validation and only failed (or silently
# misbehaved) in the app. Each file calls bogusfn once; the error names the line that holds it.
FML="$ROOT/tests/headless/formula"
for f in "$FML"/*.inf; do
  name=$(basename "$f" .inf)
  case "$name" in
    formula|equation) code=E_FORMULA_COMPILE; line=$(grep -n '^  s formula ' "$f" | cut -d: -f1) ;;
    *) code=E_EXPR_COMPILE; line=$(grep -n 'bogusfn' "$f" | grep -v ':#' | head -1 | cut -d: -f1) ;;
  esac
  r=$("$BIN" --validate "$f" 2>/dev/null); rc=$?
  [ "$rc" = 3 ] && [ "$(echo "$r" | json "[(e['code'], e['line'], 'bogusfn' in e['message']) for e in d['errors']]")" = "[('$code', $line, True)]" ]
  check "formula $name: validate exit 3, $code at line $line names bogusfn" $?
  sed 's/bogusfn(/sin(/' "$f" > "$OUT/formula_ok_$name.inf"
  "$BIN" --validate "$OUT/formula_ok_$name.inf" >/dev/null 2>&1; check "formula $name: same patch with sin( validates" $?
  case "$name" in
    equation) r=$("$BIN" --audio-summary "$f" "$OUT/formula_$name.json" --duration 0.2 2>/dev/null); rc=$?; what=audio-summary ;;
    *) r=$("$BIN" --frame "$f" 0 "$OUT/formula_$name.png" 2>/dev/null); rc=$?; what=frame ;;
  esac
  [ "$rc" = 3 ] && [ "$(echo "$r" | json "[e['code'] for e in d['errors']]")" = "['$code']" ]
  check "formula $name: $what exit 3 with $code" $?
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
# nodes out of index order keep their own numbers and ids; a source output may be named
O="$N/out_of_order.inf"
"$BIN" --validate "$O" >/dev/null 2>&1; check "names: out-of-order indices + named output validate" $?
r=$("$BIN" --explain "$O" 2>/dev/null)
echo "$r" | grep -q '^  bloom "glow" (5)$' && echo "$r" | grep -q '^  Audio Analyze "ears" (6)$'; check "names: out-of-order ids bind to their declared index" $?
echo "$r" | grep -q '^  mod bloom "glow" (5) uIntensity <- Audio Analyze "ears" (6) out 1, absolute, depth 1, range 0.8..2.2$'; check "names: named mod output resolves, lo/hi kept" $?
sed 's/ears low 0/ears 1 0/' "$O" > "$OUT/oo_num.inf"
"$BIN" --canonicalize "$O" "$OUT/oo_a.inf" >/dev/null 2>&1; "$BIN" --canonicalize "$OUT/oo_num.inf" "$OUT/oo_b.inf" >/dev/null 2>&1
cmp -s "$OUT/oo_a.inf" "$OUT/oo_b.inf"; check "names: named output == numeric output (byte-identical)" $?
sed 's/ears low 0/ears lo 0/' "$O" > "$OUT/oo_bad.inf"
r=$("$BIN" --validate "$OUT/oo_bad.inf" 2>/dev/null); rc=$?
[ "$rc" = 3 ] && [ "$(echo "$r" | json "(d['errors'][0]['code'], d['errors'][0]['hint'].split(',')[0])")" = "('E_BAD_REF', 'outputs: level')" ]; check "names: unknown output name exit 3 with the output list" $?
printf 'infinite-patch 1\nnode 0 Source Shape\nend\n' > "$OUT/idx0.inf"
r=$("$BIN" --validate "$OUT/idx0.inf" 2>/dev/null); [ "$(echo "$r" | json "d['errors'][0]['code']")" = "E_BAD_INDEX" ]; check "names: node index 0 rejected" $?

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

# --- per-shot overrides (R471 7.4): --set <node>.<param>=<value> ---
TAP="$ROOT/tests/headless/format/tap.inf"
cov() { "$BIN" --frames-dir "$TAP" "$OUT/fs" --duration 0.1 --fps 10 --node 1 --alpha "$@" 2>/dev/null | json "d['frame_stats'][0]['alpha_coverage']"; }
c0=$(cov); c1=$(cov --set shape.sizeX=0.2); c2=$(cov --set 1.sizeX=0.2)
[ "$c0" != "$c1" ]; check "set: a changed size changes the picture" $?
[ "$c1" = "$c2" ]; check "set: node by id word and by index give the same picture" $?
[ "$(cov --set shape.shapeType=Circle)" != "$c0" ]; check "set: a dropdown option name is accepted" $?
"$BIN" --frame "$TAP" 0 "$OUT/s.png" --node 1 --set shape.nope=1 >/dev/null 2>&1; [ $? = 3 ]; check "set: unknown param exit 3" $?
"$BIN" --frame "$TAP" 0 "$OUT/s.png" --node 1 --set shape.sizeX=big >/dev/null 2>&1; [ $? = 3 ]; check "set: non-number for a float exit 3" $?
"$BIN" --frame "$TAP" 0 "$OUT/s.png" --node 1 --set shape.sizeX >/dev/null 2>&1; [ $? = 2 ]; check "set: malformed spec exit 2" $?
rf=$("$BIN" --frame "$ROOT/tests/headless/format/modulation.inf" 0 "$OUT/s.png" --set shape.sizeX=0.3 2>/dev/null)
[ "$(echo "$rf" | json "[w['code'] for w in d['warnings']]")" = "['W_OVERRIDDEN_BY_MODULATION']" ]; check "set: a bound control warns W_OVERRIDDEN_BY_MODULATION" $?

# --- film events -> notes, per-node stems (R471 7.5) ---
NF="$ROOT/tests/headless/format"
rm -rf "$OUT/stems"
rn=$("$BIN" --audio-summary "$NF/notes.inf" "$OUT/n.json" --duration 2.2 --notes "$NF/notes_events.json" --note-map "$NF/notes_map.json" --stems hit --stems-dir "$OUT/stems" 2>/dev/null)
[ "$(echo "$rn" | json "d['notes']['injected']")" = "3" ]; check "notes: three mapped events injected" $?
[ "$(echo "$rn" | json "d['notes']['unmapped_types']")" = "['whoosh']" ]; check "notes: an unmapped event type is listed, not played" $?
[ "$(echo "$rn" | json "'W_UNMAPPED_EVENT' in [w['code'] for w in d['warnings']]")" = "True" ]; check "notes: W_UNMAPPED_EVENT is raised" $?
# Field Synth voice pool: 16 short notes must all sound (voices past the rendered 8 once went silent).
python3 - "$NF" "$OUT" <<'PY'
import json, sys
nf, out = sys.argv[1:3]
json.dump([{"t": 0.3 + 0.3 * i, "type": "impact", "dur": 0.1, "intensity": 1.0} for i in range(16)], open(out + "/fs_events.json", "w"))
json.dump({"impact": {"node": "fsyn", "pitch": 60, "velocity_from": "intensity", "length_from": "dur"}}, open(out + "/fs_map.json", "w"))
PY
cat > "$OUT/fieldsynth16.inf" <<'INF'
infinite-patch 1
node 1 Notes Keyboard
  id keys
end
node 2 Synths Field Synth
  id fsyn
end
node 3 Utility Audio Out
  id speakers
end
note fsyn 0 keys
aud speakers 0 fsyn
INF
"$BIN" --audio-summary "$OUT/fieldsynth16.inf" "$OUT/fs.json" --duration 5.6 --wav "$OUT/fs.wav" --notes "$OUT/fs_events.json" --note-map "$OUT/fs_map.json" >/dev/null 2>&1
python3 - "$OUT/fs.wav" <<'PY'
import wave, array, sys
w = wave.open(sys.argv[1]); r = w.getframerate(); n = w.getnchannels()
a = array.array('h'); a.frombytes(w.readframes(w.getnframes())); L = a[0::n]
def peak(i):  # window after note i's onset (0.3 + 0.3 i), 0.12 s
    s = int((0.3 + 0.3 * i) * r); return max((abs(x) for x in L[s:s + int(0.12 * r)]), default=0)
sys.exit(0 if all(peak(i) > 300 for i in range(16)) else 1)
PY
check "field synth: all 16 short notes are audible (voice pool recycles)" $?
# Field Synth release: the kernel owns the tail. A fixed 30 ms voice release once cut every preset dead
# at note-off; the default pad must still sound 100 ms after its note ends, then free its voice (silent).
python3 - "$OUT" <<'PY'
import json, sys
out = sys.argv[1]
json.dump([{"t": 0.3, "type": "impact", "dur": 0.1, "intensity": 1.0}], open(out + "/fsr_events.json", "w"))
PY
"$BIN" --audio-summary "$OUT/fieldsynth16.inf" "$OUT/fsr.json" --duration 2.0 --wav "$OUT/fsr.wav" --notes "$OUT/fsr_events.json" --note-map "$OUT/fs_map.json" >/dev/null 2>&1
python3 - "$OUT/fsr.wav" <<'PY'
import wave, array, sys
w = wave.open(sys.argv[1]); r = w.getframerate(); n = w.getnchannels()
a = array.array('h'); a.frombytes(w.readframes(w.getnframes())); L = a[0::n]
def peak(t0, t1): return max((abs(x) for x in L[int(t0 * r):int(t1 * r)]), default=0)
sys.exit(0 if peak(0.5, 0.52) > 300 and peak(1.5, 2.0) == 0 else 1)
PY
check "field synth: a note's tail rings past note-off, then the voice frees" $?
# Note fan-out: one Keyboard into 12 synths, every Mixer input muted but the 12th. The 9th consumer
# and later once got no notes (NoteEventQueue capped at 8 cursors).
python3 - "$OUT" <<'PY'
import json, sys
out = sys.argv[1]
L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end"]
W = []
for k in range(12):
    L += ["node %d Synths Analog" % (10 + k), "  id a%d" % k, "end"]
    W += ["note a%d 0 keys" % k, "aud mx in_%d a%d" % (k + 1, k)]
L += ["node 30 Utility Mixer", "  id mx", "  i channels 12"] + ["  f gain%d -120" % i for i in range(11)] + ["end", "node 31 Utility Audio Out", "end"]
open(out + "/fanout12.inf", "w").write("\n".join(L + W + ["aud 31 audio mx"]) + "\n")
json.dump([{"t": 0.2, "type": "hit", "dur": 0.5, "v": 1.0}], open(out + "/fo_events.json", "w"))
json.dump({"hit": {"node": "keys", "pitch": 60, "velocity_from": "v", "length_from": "dur"}}, open(out + "/fo_map.json", "w"))
F = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Utility Audio Out", "end"]
for k in range(65):
    F += ["node %d Synths Analog" % (10 + k), "end"]
open(out + "/fanout65.inf", "w").write("\n".join(F + ["note %d 0 keys" % (10 + k) for k in range(65)] + ["aud 2 audio 10"]) + "\n")
PY
"$BIN" --audio-summary "$OUT/fanout12.inf" "$OUT/fo.json" --duration 1 --wav "$OUT/fo.wav" --notes "$OUT/fo_events.json" --note-map "$OUT/fo_map.json" >/dev/null 2>&1
python3 - "$OUT/fo.wav" <<'PY'
import wave, array, sys
w = wave.open(sys.argv[1]); n = w.getnchannels()
a = array.array('h'); a.frombytes(w.readframes(w.getnframes()))
sys.exit(0 if max(abs(x) for x in a[0::n]) > 300 else 1)
PY
check "notes: the 12th synth on one Keyboard gets notes (fan-out past 8)" $?
fo=$("$BIN" --validate "$OUT/fanout65.inf" 2>/dev/null); [ "$(echo "$fo" | json "'W_NOTE_FANOUT' in [i['code'] for i in d['errors'] + d['warnings']]")" = "True" ]; check "notes: 65 consumers on one note output raise W_NOTE_FANOUT" $?
# Arrangement audio: --timeline plays a 120 BPM click clip (one click every 0.5 s from 0.25 s) placed at beat 2;
# at 60 BPM it must start at 2.0 s and stretch to one click per second. Without --timeline headless played the canvas only.
python3 - "$OUT" <<'PY'
import wave, struct, sys
out, r = sys.argv[1], 48000
f = [0] * (2 * r)
for k in range(4):
    for i in range(48):
        f[int((0.25 + 0.5 * k) * r) + i] = 20000 if i < 24 else -20000
w = wave.open(out + "/clicks.wav", "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(r)
w.writeframes(struct.pack("<%dh" % len(f), *f)); w.close()
open(out + "/clip.inf", "w").write("\n".join(["infinite-patch 1", "node 1 Modulators Audio File", "  uid 9001",
    "  s path " + out + "/clicks.wav", "  b loop 0", "end", "stream 1 0 1 0 0 1 0 A",
    "cliptick 0 1 1920 3840 9001 0 0 0 0 1 0 0 0 0 clicks", "clipsample 0 1 1", "clipbpm 0 1 120 2"]) + "\n")
PY
"$BIN" --audio-summary "$OUT/clip.inf" "$OUT/clip.json" --duration 6.5 --bpm 60 --timeline --wav "$OUT/clip.wav" >/dev/null 2>&1
python3 - "$OUT/clip.wav" <<'PY'
import wave, array, sys
w = wave.open(sys.argv[1]); r = w.getframerate()
a = array.array('h'); a.frombytes(w.readframes(w.getnframes()))
L = a[0::w.getnchannels()]; on, last = [], -r
for i, x in enumerate(L):
    if abs(x) > 6000 and i - last > r // 10:
        on.append(i / r); last = i
sys.exit(0 if len(on) == 4 and all(abs(o - t) <= 0.005 for o, t in zip(on, (2.5, 3.5, 4.5, 5.5))) else 1)
PY
check "arrangement: --timeline plays a sample clip at its beat, tempo-synced (120 BPM clip at 60 BPM)" $?
"$BIN" --audio-summary "$OUT/clip.inf" "$OUT/clip.json" --timeline --output 1 >/dev/null 2>&1; [ $? = 2 ]; check "arrangement: --timeline with --output exits 2" $?
[ -f "$OUT/stems/hit.wav" ]; check "stems: one WAV per node" $?
python3 - "$OUT/stems/hit.wav" <<'PY'
import wave, array, sys
w = wave.open(sys.argv[1]); r = w.getframerate()
a = array.array('h'); a.frombytes(w.readframes(w.getnframes()))
L = a[0::w.getnchannels()]; thr = max(abs(x) for x in L) * 0.02
on, quiet = [], 10**9
for i, x in enumerate(L):
    if abs(x) > thr and quiet > int(0.05 * r): on.append(i / r)
    quiet = 0 if abs(x) > thr else quiet + 1
ok = w.getnframes() == round(2.2 * r) and len(on) == 3 and all(abs(o - t) <= 0.001 for o, t in zip(on, (0.5, 1.0, 1.5)))
sys.exit(0 if ok else 1)
PY
check "stems: onsets land within 1 ms of the events, length = duration" $?
"$BIN" --audio-summary "$NF/notes.inf" "$OUT/n.json" --duration 1 --notes "$NF/notes_events.json" --note-map "$NF/notes_map.json" --stems keys --stems-dir "$OUT/stems" >/dev/null 2>&1; [ $? = 3 ]; check "stems: a node with no audio output exits 3" $?
"$BIN" --audio-summary "$NF/notes.inf" "$OUT/n.json" --notes "$NF/notes_events.json" >/dev/null 2>&1; [ $? = 2 ]; check "notes: --notes without --note-map exits 2" $?
"$BIN" --audio-summary "$NF/notes.inf" "$OUT/n.json" --stems hit >/dev/null 2>&1; [ $? = 2 ]; check "stems: --stems without --stems-dir exits 2" $?

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

# --- release: a new version must be above every published one (else old copies keep opening patches) ---
python3 "$ROOT/scripts/release_version_gate.py" --self-test | grep -q '^RELEASEGATE OK'; check "release gate: version ordering cases" $?

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
r=$("$BIN" --audio-summary "$M/nan_param.inf" "$OUT/nan.json" --duration 1 2>/dev/null); rc=$?
[ "$rc" = 0 ] && [ "$(echo "$r" | json "d['warnings']==[] and 900 < d['audio_summary']['loudest_band_hz'] < 1200")" = "True" ]; check "summary: a nan/inf param in a patch is ignored, the sine still sounds" $?
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
