#!/bin/sh
# Shoot the v1.0 (beta) README screenshots from docs/showcase/infinite-v1-showcase.inf.
# Usage: docs/showcase/shoot.sh [shot ...]   (no arguments = all nine shots)
#   APP=/path/to/Infinite.app/Contents/MacOS/Infinite   the dev app to drive (default below)
# Writes docs/screenshots/v1/<shot>.png, each window-only, 2x Retina, under 1.5 MB.
# Runs one Infinite instance on port 7790 and quits it at the end; never captures the full screen.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SHOW=$REPO/docs/showcase
PATCH=$SHOW/infinite-v1-showcase.inf
OUT=$REPO/docs/screenshots/v1
APP=${APP:-$HOME/Infinite/infinte-base/build-dev/Infinite.app/Contents/MacOS/Infinite}
TMP=$(mktemp -d)
TOKFILE="$HOME/Library/Application Support/Infinite/control_token"
SHOTS=${*:-"node-graph 3d-view mod-matrix session-clips timeline piano-roll hero-panels sample-studio terminal"}
mkdir -p "$OUT"

# 1. Never launch while a build is running (8 GB Mac).
while ! python3 "$REPO/scripts/ibuild.py" status 2>/dev/null | grep -q 'build lock: free'; do
   echo "a build is running; waiting"; sleep 30
done

# 2. Window-id helper: largest layer-0 window of a pid (CoreGraphics).
cat > "$TMP/winid.swift" <<'EOF'
import CoreGraphics
let pid = Int32(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
var best: (Int, Double) = (0, 0)
for w in list {
  guard let p = w[kCGWindowOwnerPID as String] as? Int32, p == pid else { continue }
  guard let layer = w[kCGWindowLayer as String] as? Int, layer == 0 else { continue }
  let b = w[kCGWindowBounds as String] as! [String: Any]
  let area = (b["Width"] as! Double) * (b["Height"] as! Double)
  if area > best.1 { best = (w[kCGWindowNumber as String] as! Int, area) }
}
print(best.0)
EOF
swiftc -O "$TMP/winid.swift" -o "$TMP/winid"

# 3. Launch. Keep a copy of the shared control token: other sessions may overwrite it.
[ -f "$TOKFILE" ] && cp "$TOKFILE" "$TMP/token.bak"
INFINITE_NO_SINGLE_INSTANCE=1 INFINITE_CONTROL_PORT=7790 INFINITE_NOSPLASH=1 INFINITE_NO_UPDATE_CHECK=1 \
   "$APP" "$PATCH" > "$TMP/app.log" 2>&1 &
PID=$!
sleep 15
cp "$TOKFILE" "$TMP/tok"
c() { INFINITE_CONTROL_PORT=7790 INFINITE_CONTROL_TOKEN=$(cat "$TMP/tok") "$APP" --cli "$@"; }
cleanup() {
   c quit >/dev/null 2>&1 || true; sleep 2; kill "$PID" 2>/dev/null || true
   [ -f "$TMP/token.bak" ] && cp "$TMP/token.bak" "$TOKFILE"
   rm -rf "$TMP"
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
c studio set "text=$(cat "$TMP/studio.txt")" >/dev/null
c session scene 1 >/dev/null   # starts the audio engine and plays scene "Groove"

closeall() { for p in samplestudio pianoroll modmatrix perf view3d timeline session terminal; do c panel $p close >/dev/null 2>&1 || true; done; c view params off >/dev/null; }

# keep <name>: window-only capture, shrunk with sips in 160 px steps until under 1.5 MB.
keep() {
   screencapture -o -x -l "$("$TMP/winid" "$PID")" "$TMP/$1.png"
   cp "$TMP/$1.png" "$OUT/$1.png"; w=2880
   while [ "$(stat -f %z "$OUT/$1.png")" -gt 1500000 ] && [ $w -gt 1600 ]; do
      w=$((w-160)); sips -Z $w "$TMP/$1.png" --out "$OUT/$1.png" >/dev/null
   done
   echo "$1: $(sips -g pixelWidth "$OUT/$1.png" | tail -1 | awk '{print $2}') px, $(stat -f %z "$OUT/$1.png") bytes"
}

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
   hero-panels)   c session scene 2 >/dev/null
                  c panel pianoroll open top >/dev/null; c panel modmatrix open left >/dev/null
                  c panel perf open right >/dev/null; c panel view3d open bottom >/dev/null
                  c view fit >/dev/null; sleep 4 ;;
   sample-studio) c panel samplestudio open top >/dev/null; c view fit >/dev/null; sleep 4 ;;
   terminal)      c panel terminal open right >/dev/null; c view fit >/dev/null; sleep 3
                  c term send "PROMPT='%1~ %% '; cd '$SHOW'; clear
" >/dev/null; sleep 1
                  c term send "infinite explain | grep ' mod ' | sed -E 's/^ +mod //; s/ out 0, absolute, depth 1, range/ /'
" >/dev/null; sleep 2
                  c term send "infinite session scene 2
" >/dev/null; sleep 2
                  c term send "infinite perf | jq -r '.controls[] | \"\\(.label) -> node \\(.node)\"'
" >/dev/null; sleep 3 ;;
   *) echo "unknown shot $s"; continue ;;
   esac
   keep "$s"
done
