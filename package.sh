#!/usr/bin/env bash
# Builds a universal Release .app and wraps it in a distributable DMG.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build-dist"
STAGE="$ROOT/dist/stage"
DMG="$ROOT/dist/Infinite.dmg"

echo "==> configuring"
# Ninja when it is available, otherwise whatever CMake defaults to. This is the
# bigger of the two builds - it compiles every source twice, once per
# architecture - so it gains the most from a faster generator, but the script
# still has to run on a machine that only has make.
GENERATOR=()
WANTED="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
    WANTED="Ninja"
fi
echo "    using $WANTED"

# CMake refuses to reconfigure an existing build directory with a different
# generator, so a tree built before Ninja was installed would fail here rather
# than switch. Wiping is safe: this directory is only ever a build artifact.
if [ -f "$BUILD/CMakeCache.txt" ] &&
   ! grep -q "CMAKE_GENERATOR:INTERNAL=$WANTED" "$BUILD/CMakeCache.txt"; then
    echo "    generator changed, reconfiguring from scratch"
    rm -rf "$BUILD"
fi

cmake -S "$ROOT" -B "$BUILD" ${GENERATOR[@]+"${GENERATOR[@]}"} \
    -DCMAKE_BUILD_TYPE=Release >/dev/null

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
# The entitlements must be repeated here: this re-sign replaces the signature
# CMake's POST_BUILD step applied, and dropping them would leave a shipped app
# that the kernel SIGKILLs (no error, no stderr) the first time a third-party
# plugin maps its code pages. See cmake/Infinite.entitlements.
codesign --force --deep --sign - \
    --entitlements "$ROOT/cmake/Infinite.entitlements" "$APP"
codesign --verify --deep --strict "$APP" && echo "signature ok"

echo "==> staging"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
if [ -f "$ROOT/Infinite_Node_Reference_Manual.pdf" ]; then
    cp "$ROOT/Infinite_Node_Reference_Manual.pdf" "$STAGE/"
fi
ln -s /Applications "$STAGE/Applications"

echo "==> building dmg"
hdiutil create -volname "Infinite" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

if [ -d "$ROOT/website/assets" ]; then
    cp "$DMG" "$ROOT/website/assets/Infinite.dmg"
    echo "copied $DMG -> website/assets/Infinite.dmg"
fi

echo
echo "built $DMG ($(du -h "$DMG" | cut -f1))"

