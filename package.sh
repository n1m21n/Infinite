#!/usr/bin/env bash
# Builds a universal Release .app and wraps it in a distributable DMG.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-dist"
STAGE="$ROOT/dist/stage"
DMG="$ROOT/dist/Infinite.dmg"

echo "==> configuring"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null

echo "==> building"
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

APP="$BUILD/Infinite.app"
[ -d "$APP" ] || { echo "build produced no .app"; exit 1; }

# Runtime state the app writes next to itself must not ship inside the bundle.
rm -f "$APP/Contents/Resources/Infinite.json" "$APP/Contents/Resources/imgui.ini"

echo "==> verifying the bundle is self-contained"
if otool -L "$APP/Contents/MacOS/Infinite" | grep -qE '/opt/homebrew|/usr/local/(lib|opt)'; then
    echo "ERROR: bundle links a non-system library:"
    otool -L "$APP/Contents/MacOS/Infinite" | grep -E '/opt/homebrew|/usr/local/(lib|opt)'
    exit 1
fi
lipo -info "$APP/Contents/MacOS/Infinite"

# Ad-hoc signature. Not notarized (that needs a paid Developer account), so
# first launch still needs right-click -> Open. Signing at least keeps the
# bundle from being reported as damaged.
echo "==> ad-hoc signing"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP" && echo "signature ok"

echo "==> staging"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

echo "==> building dmg"
hdiutil create -volname "Infinite" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

echo
echo "built $DMG ($(du -h "$DMG" | cut -f1))"
