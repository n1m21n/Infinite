#!/usr/bin/env bash
# Builds the shipping plugin bundles (docs/plans/vst-au/README.md 3.6) and copies them into OUT:
# macOS: universal arm64 + x86_64, the two .vst3 and the two .component; Windows / Linux: the two
# .vst3. Always INFINITE_LICENSE_TEST_KEY=OFF: a shipped plugin never accepts test keys.
#
#   tools/plugin/build_bundles.sh OUT [--build DIR] [--jobs N]
#
# package.sh puts OUT = Infinite.app/Contents/Resources/Plugins; CI puts the Windows zip's Plugins\
# and the AppImage's usr/share/infinite/Plugins. The engine is the app itself (DawPluginInstall.h).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:?usage: build_bundles.sh OUT [--build DIR] [--jobs N]}"
shift
BUILD="$ROOT/build-dist-plugin"
JOBS=3
while [ $# -gt 0 ]; do
  case "$1" in
    --build) BUILD="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    *) echo "unknown: $1"; exit 2 ;;
  esac
done

ARGS=(-DCMAKE_BUILD_TYPE=Release -DINFINITE_LICENSE_TEST_KEY=OFF)
TARGETS=(InfiniteInstrument_vst3 InfiniteFX_vst3)
case "$(uname -s)" in
  Darwin)
    ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
    TARGETS+=(InfiniteInstrument_au_bundle InfiniteFX_au_bundle)
    ;;
esac
cmake -S "$ROOT/src/plugin/shell" -B "$BUILD" "${ARGS[@]}" >/dev/null
grep -q "INFINITE_LICENSE_TEST_KEY:BOOL=OFF" "$BUILD/CMakeCache.txt" || { echo "test key is on in $BUILD"; exit 1; }
for t in "${TARGETS[@]}"; do
  cmake --build "$BUILD" --config Release --target "$t" -j "$JOBS"
done

mkdir -p "$OUT"
for n in "Infinite Instrument" "Infinite FX"; do
  V="$BUILD/VST3/Release/$n.vst3"
  [ -d "$V" ] || V="$BUILD/VST3/$n.vst3"
  [ -d "$V" ] || { echo "missing $n.vst3 in $BUILD"; exit 1; }
  rm -rf "${OUT:?}/$n.vst3"
  cp -R "$V" "$OUT/"
  if [ "$(uname -s)" = Darwin ]; then
    rm -rf "${OUT:?}/$n.component"
    cp -R "$BUILD/AU/$n.component" "$OUT/"
    lipo -archs "$OUT/$n.vst3/Contents/MacOS/$n" | grep -q "x86_64 arm64\|arm64 x86_64" || { echo "$n.vst3 is not universal"; exit 1; }
  fi
done
ls "$OUT"
