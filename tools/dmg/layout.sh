#!/bin/bash
# Lay out the DMG window (background, icon positions) with Finder, then write the result back to a compressed DMG.
# usage: layout.sh <stage-dir> <out.dmg>   Falls back to a plain DMG (exit 0) if Finder scripting is unavailable.
set -u
STAGE="$1"; OUT="$2"; VOL="Infinite"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; RW="$TMP/rw.dmg"

plain() { hdiutil create -volname "$VOL" -srcfolder "$STAGE" -ov -format UDZO "$OUT" >/dev/null; rm -rf "$TMP"; exit 0; }

mkdir -p "$STAGE/.background"
python3 "$HERE/make_background.py" "$STAGE/.background/background.png" 2>/dev/null || plain
SIZE=$(( $(du -sm "$STAGE" | cut -f1) + 40 ))
hdiutil create -volname "$VOL" -srcfolder "$STAGE" -ov -format UDRW -size ${SIZE}m "$RW" >/dev/null || plain
hdiutil detach "/Volumes/$VOL" >/dev/null 2>&1
DEV=$(hdiutil attach "$RW" -readwrite -noverify -noautoopen | grep -E '^/dev/' | head -1 | awk '{print $1}')
[ -n "$DEV" ] || plain

cat > "$TMP/layout.applescript" <<OSA
tell application "Finder"
  tell disk "$VOL"
    open
    set current view of container window to icon view
    set toolbar visible of container window to false
    set statusbar visible of container window to false
    set the bounds of container window to {200, 120, 800, 520}
    set opts to the icon view options of container window
    set arrangement of opts to not arranged
    set icon size of opts to 96
    set background picture of opts to file ".background:background.png"
    set position of item "Infinite.app" of container window to {150, 190}
    set position of item "Applications" of container window to {450, 190}
    try
      set position of item "Fix & Open Infinite.command" of container window to {150, 320}
    end try
    try
      set position of item "Read Me First.txt" of container window to {450, 320}
    end try
    close
    open
    update without registering applications
    delay 2
    close
  end tell
end tell
OSA
# Finder may sit on a permission prompt (Automation); give it 40 s, then fall back.
osascript "$TMP/layout.applescript" >/dev/null 2>&1 &
OPID=$!
for _ in $(seq 1 40); do kill -0 $OPID 2>/dev/null || break; sleep 1; done
if kill -0 $OPID 2>/dev/null; then kill $OPID 2>/dev/null; RC=1; else wait $OPID; RC=$?; fi
sync; sleep 1
hdiutil detach "$DEV" >/dev/null 2>&1 || hdiutil detach "$DEV" -force >/dev/null 2>&1
[ $RC -eq 0 ] || { echo "layout: Finder scripting unavailable, building a plain DMG"; plain; }
rm -f "$OUT"
hdiutil convert "$RW" -format UDZO -o "$OUT" >/dev/null || plain
rm -rf "$TMP"
