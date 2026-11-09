#!/usr/bin/env bash
# Builds a DAW test kit for step 2.8 (docs/plans/vst-au/README.md 8.8): the four plugin bundles in
# the packaged layout (engine inside each bundle), a test licence, the R patches, the checklists
# and install / uninstall scripts. Builds nothing; run after the plugin and engine builds.
#
#   tools/plugin/daw_kit.sh [--out DIR] [--build build-plugin] [--engine build-c1/Infinite.app]
#
# On the test Mac:  ./install.sh   (copies into ~/Library/Audio/Plug-Ins, writes the test licence)
#                   ./uninstall.sh (removes both again; leaves a real licence alone)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build-plugin/daw-kit"
BUILD="$ROOT/build-plugin"
ENGINE="$ROOT/build-c1/Infinite.app"
while [ $# -gt 0 ]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --build) BUILD="$2"; shift 2 ;;
    --engine) ENGINE="$2"; shift 2 ;;
    *) echo "unknown: $1"; exit 2 ;;
  esac
done
[ -x "$ENGINE/Contents/MacOS/Infinite" ] || { echo "missing engine: $ENGINE"; exit 2; }

rm -rf "${OUT:?}"
mkdir -p "$OUT/VST3" "$OUT/Components" "$OUT/licence" "$OUT/patches" "$OUT/checklists"

put_engine() { # $1 = folder that holds Helpers
  mkdir -p "$1/Helpers"
  cp -R "$ENGINE" "$1/Helpers/Infinite Engine.app"
}
for n in "Infinite Instrument" "Infinite FX"; do
  cp -R "$BUILD/VST3/Release/$n.vst3" "$OUT/VST3/"
  put_engine "$OUT/VST3/$n.vst3/Contents"
  codesign --force --deep -s - "$OUT/VST3/$n.vst3" 2>/dev/null
  cp -R "$BUILD/AU/$n.component" "$OUT/Components/"
  put_engine "$OUT/Components/$n.component/Contents/Resources/plugin.vst3/Contents"
  codesign --force --deep -s - "$OUT/Components/$n.component" 2>/dev/null
done

# A key signed by the check tool's test key; only an INFINITE_LICENSE_TEST_KEY build accepts it.
"$BUILD/bin/Release/plugin_shell_check" --write-license "$OUT/licence"
cp "$ROOT"/tests/plugin/patches/*.inf "$OUT/patches/"
cp "$ROOT"/tests/plugin/daw/*.md "$OUT/checklists/" 2>/dev/null || true
cp "$ROOT/tools/plugin/crash_drill.py" "$OUT/"
mkdir -p "$OUT/bin"
cp "$BUILD/bin/Release/au_check" "$BUILD/bin/Release/validator" "$OUT/bin/"

cat > "$OUT/install.sh" <<'EOS'
#!/usr/bin/env bash
set -euo pipefail
K="$(cd "$(dirname "$0")" && pwd)"
P="$HOME/Library/Audio/Plug-Ins"
L="$HOME/Library/Application Support/Infinite"
mkdir -p "$P/VST3" "$P/Components" "$L"
for n in "Infinite Instrument" "Infinite FX"; do
  rm -rf "${P:?}/VST3/$n.vst3" "${P:?}/Components/$n.component"
  cp -R "$K/VST3/$n.vst3" "$P/VST3/"
  cp -R "$K/Components/$n.component" "$P/Components/"
done
xattr -dr com.apple.quarantine "$P/VST3/Infinite "*.vst3 "$P/Components/Infinite "*.component 2>/dev/null || true
if [ -f "$L/license.inflic" ] && [ ! -f "$L/license-test-key.hex" ]; then
  echo "a real licence is installed; leaving it (the test build may refuse it)"
else
  cp "$K/licence/"* "$L/"
fi
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -a 2>/dev/null | grep -i infinite || echo "AUs not listed yet: open Logic's Plug-in Manager and rescan"
echo "installed"
EOS
cat > "$OUT/uninstall.sh" <<'EOS'
#!/usr/bin/env bash
set -euo pipefail
P="$HOME/Library/Audio/Plug-Ins"
L="$HOME/Library/Application Support/Infinite"
for n in "Infinite Instrument" "Infinite FX"; do
  rm -rf "${P:?}/VST3/$n.vst3" "${P:?}/Components/$n.component"
done
if [ -f "$L/license-test-key.hex" ]; then
  rm -f "$L/license.inflic" "$L/license-test-key.hex" "$L/license-revoked.json"
fi
pkill -f -- "--plugin-engine" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "removed"
EOS
chmod +x "$OUT/install.sh" "$OUT/uninstall.sh"
( cd "$(dirname "$OUT")" && rm -f "$(basename "$OUT").tar.gz" && tar -czf "$(basename "$OUT").tar.gz" "$(basename "$OUT")" )
du -sh "$OUT" "$OUT.tar.gz"
