#!/bin/sh
# Shoot the v1.0 (beta) README screenshots from docs/showcase/infinite-v1-showcase.inf.
# Usage: docs/showcase/shoot.sh [shot ...]   (no arguments = all nine shots)
#   APP=/path/to/Infinite.app/Contents/MacOS/Infinite   the dev app to drive (default below)
# Writes docs/screenshots/v2/<shot>.png, each window-only, 2x Retina, under 1.5 MB.
# Runs one off-screen Infinite instance on port 7790 and quits it at the end; the app saves its own frames.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SHOW=$REPO/docs/showcase
PATCH=${PATCH:-$SHOW/infinite-v1-showcase.inf}
OUT=${OUT:-$REPO/docs/screenshots/v1}   # OUT=docs/screenshots/v2 for 2.0 captures
PORT=${PORT:-7790}
APP=${APP:-$HOME/Infinite/infinte-base/build-dev/Infinite.app/Contents/MacOS/Infinite}
TMP=$(mktemp -d)
TOKFILE="${SHOT_HOME:-$HOME}/Library/Application Support/Infinite/control_token"
SHOTS=${*:-"node-graph 3d-view mod-matrix session-clips timeline piano-roll hero-panels sample-studio terminal"}
mkdir -p "$OUT"

# 1. Never launch while a build is running (8 GB Mac).
while ! python3 "$REPO/scripts/ibuild.py" status 2>/dev/null | grep -q 'build lock: free'; do
   echo "a build is running; waiting"; sleep 30
done

# 3. Launch. Keep a copy of the shared control token: other sessions may overwrite it.
[ -f "$TOKFILE" ] && cp "$TOKFILE" "$TMP/token.bak"
INFINITE_NO_SINGLE_INSTANCE=1 INFINITE_CONTROL_PORT=$PORT HOME="${SHOT_HOME:-$HOME}" INFINITE_NOSPLASH=1 INFINITE_NO_UPDATE_CHECK=1 INFINITE_HIDE_TELEMETRY=1 \
   IMAGERESYNTH_SCREENSHOT="$TMP/unused.png" INFINITE_SCREENSHOT_FRAME=999999999 INFINITE_SHOT_REQUEST="$TMP/req" \
   "$APP" "$PATCH" > "$TMP/app.log" 2>&1 &
PID=$!
sleep 15
cp "$TOKFILE" "$TMP/tok"
c() { INFINITE_CONTROL_PORT=$PORT INFINITE_CONTROL_TOKEN=$(cat "$TMP/tok") "$APP" --cli "$@"; }
cleanup() {
   c quit >/dev/null 2>&1 || true; sleep 2; kill "$PID" 2>/dev/null || true
   [ -f "$TMP/token.bak" ] && cp "$TMP/token.bak" "$TOKFILE"
   mkdir -p "$REPO/.proof" && cp "$TMP/app.log" "$REPO/.proof/shoot-app.log" 2>/dev/null; rm -rf "$TMP"
}
trap cleanup EXIT

# The Studio does not resolve a relative take path on load, so set it absolute.
sed "s#path=media/#path=$SHOW/media/#" > "$TMP/studio.txt" <<'EOF'
studio 1
src path=media/studio-take.wav
sliceset 1 end=195254 next=9
slice id=1 s=0 col=3 n=60
slice id=2 s=24406 col=6 n=61
slice id=3 s=48812 col=9 n=62
slice id=4 s=73218 col=2 n=63
slice id=5 s=97624 col=5 n=64
slice id=6 s=122030 col=8 n=65
slice id=7 s=146436 col=1 n=66
slice id=8 s=170842 col=4 n=67
EOF
c studio set "text=$(cat "$TMP/studio.txt")" >/dev/null || true
c session scene 1 >/dev/null || true  # starts the audio engine and plays scene "Groove"

closeall() { for p in samplestudio pianoroll modmatrix perf view3d view2d curveeditor mixer drums fieldpatcher timeline session terminal; do c panel $p close >/dev/null 2>&1 || true; done; c view params off >/dev/null; }


# Content checks: a shot is kept only when the thing it shows is really there (no empty states).
# status and graph are JSON; explain is text with one "  Type (index)" header per node.
loaded() { c status 2>/dev/null | python3 -c "import sys,json; n=json.load(sys.stdin)['nodes']; print('nodes:', n); sys.exit(0 if n>=35 else 1)"; }
has_types() { c explain 2>/dev/null | python3 -c "import sys,re; t={m.group(1) for m in re.finditer(r'^  (.+?) \\(\\d+\\)$', sys.stdin.read(), re.M)}; m=[x for x in sys.argv[1:] if x not in t]; print('missing types:', m or 'none'); sys.exit(1 if m else 0)" "$@"; }
has_mods() { c graph 2>/dev/null | python3 -c "import sys,json; n=len(json.load(sys.stdin)['modulation']); print('modulations:', n); sys.exit(0 if n>0 else 1)"; }
has_clips() { c graph 2>/dev/null | python3 -c "import sys,json; n=sum(len(s.get('clips',[])) for s in json.load(sys.stdin)['streams']); print('clips:', n); sys.exit(0 if n>0 else 1)"; }
has_perf() { c perf 2>/dev/null | python3 -c "import sys,json; n=len(json.load(sys.stdin).get('controls',[])); print('bound controls:', n); sys.exit(0 if n>0 else 1)"; }
verify() {
   case $1 in
   node-graph|3d-view|session-clips|timeline|piano-roll|sample-studio|drums|mixer|2d-view|field-patcher|terminal|hero-panels|curve-editor|field-editor)
      loaded || return 1 ;;
   esac
   case $1 in
   mod-matrix)            loaded && has_mods ;;
   performance)           has_perf ;;
   hero-panels)           loaded && has_perf && has_mods ;;
   session-clips|timeline) has_clips ;;
   piano-roll)            has_types "Piano Roll" ;;
   sample-studio)         has_types "Sampler" ;;
   mixer)                 has_types "Mixer" ;;
   drums)                 has_types "Drum Sequencer" ;;
   3d-view)               has_types "3D View" ;;
   2d-view)               has_types "2D View" ;;
   field-patcher|field-editor|curve-editor) has_types "FieldPixel" ;;
   *)                     true ;;
   esac
}
# keep <name> is only called after verify <name> passed; a failed check prints FAIL and writes nothing.

# keep <name>: the app saves its own backbuffer (INFINITE_SHOT_REQUEST), so this works off-screen and with the
# screen locked; then shrink with sips in 160 px steps until under 1.5 MB.
keep() {
   rm -f "$TMP/$1.png"; echo "$TMP/$1.png" > "$TMP/req"
   n=0; while [ -f "$TMP/req" ] && [ $n -lt 60 ]; do sleep 0.5; n=$((n+1)); done
   [ -f "$TMP/$1.png" ] || { echo "$1: no frame saved"; return; }
   w=$(sips -g pixelWidth "$TMP/$1.png" | tail -1 | awk '{print $2}')
   cp "$TMP/$1.png" "$OUT/$1.png"
   while [ "$(stat -f %z "$OUT/$1.png")" -gt 1500000 ] && [ $w -gt 1600 ]; do
      w=$((w-160)); sips -Z $w "$TMP/$1.png" --out "$OUT/$1.png" >/dev/null
   done
   echo "$1: $(sips -g pixelWidth "$OUT/$1.png" | tail -1 | awk '{print $2}') px, $(stat -f %z "$OUT/$1.png") bytes"
}

loaded || { echo "FAIL: patch did not load in the app"; exit 1; }

for s in $SHOTS; do
   closeall
   case $s in
   node-graph)    c view params on >/dev/null; c view fit >/dev/null; sleep 3 ;;
   3d-view)       c panel view3d open top >/dev/null; sleep 7 ;;
   mod-matrix)    c panel modmatrix open bottom >/dev/null; c view fit >/dev/null; sleep 3 ;;
   session-clips) c session scene 1 >/dev/null; c panel session open >/dev/null; sleep 4 ;;
   timeline)      c session stop >/dev/null; c play >/dev/null; c seek 2 >/dev/null
                  c panel timeline open top >/dev/null; sleep 3 ;;
   piano-roll)    c panel pianoroll open top >/dev/null; sleep 3 ;;
   performance)   c session scene 2 >/dev/null; c panel perf open bottom >/dev/null; c view fit >/dev/null; sleep 4 ;;
   hero-panels)   c session scene 2 >/dev/null
                  c panel pianoroll open top >/dev/null; c panel modmatrix open left >/dev/null
                  c panel perf open right >/dev/null; c panel view3d open bottom >/dev/null
                  c view fit >/dev/null; sleep 4 ;;
   sample-studio) c panel samplestudio open top >/dev/null; c view fit >/dev/null; sleep 4 ;;
   terminal)      c panel terminal open right >/dev/null; c term open >/dev/null 2>&1 || true; c view fit >/dev/null; sleep 8
                  n=0; until c term send " " >/dev/null 2>&1 || [ $n -ge 20 ]; do sleep 2; n=$((n+1)); done
                  c term send "PROMPT='%1~ %% '; cd '$SHOW'; clear
" >/dev/null; sleep 1
                  c term send "infinite explain | grep ' mod ' | sed -E 's/^ +mod //; s/ out 0, absolute, depth 1, range/ /'
" >/dev/null; sleep 2
                  c term send "infinite session scene 2
" >/dev/null; sleep 2
                  c term send "infinite perf | jq -r '.controls[] | \"\\(.label) -> node \\(.node)\"'
" >/dev/null; sleep 3 ;;
   mixer)         c session scene 2 >/dev/null; c panel mixer open bottom >/dev/null; c view fit >/dev/null; sleep 3 ;;
   drums)         c session scene 1 >/dev/null; c panel drums open top >/dev/null; sleep 3 ;;
   2d-view)       c select ${SEL2D:-3} >/dev/null; c panel view2d open top >/dev/null; sleep 5 ;;
   field-patcher) c select FieldPixel >/dev/null; c view fit >/dev/null; c panel fieldpatcher open fill >/dev/null; sleep 4 ;;
   curve-editor)  c panel curveeditor open top >/dev/null; sleep 4 ;;
   field-editor)  c select FieldPixel >/dev/null; c panel fieldpatcher open top >/dev/null; sleep 4 ;;
   *) echo "unknown shot $s"; continue ;;
   esac
   if verify "$s"; then keep "$s"; else sleep 3; if verify "$s"; then keep "$s"; else echo "FAIL $s: content missing, not kept"; fi; fi
done
