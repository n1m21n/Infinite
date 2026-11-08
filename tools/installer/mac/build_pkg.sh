#!/usr/bin/env bash
# Builds the buyer installer for macOS: Infinite.pkg, the native Installer.app flow.
#
#   Welcome -> Licence (the terms of sale) -> Installation Type (Infinite app / VST3 / Audio Unit)
#   -> Install -> Summary (next steps: rescan, add the plugin, paste the key)
#
# usage: build_pkg.sh <signed Infinite.app> <out.pkg>
#
# Everything installs for the current user (no admin password): the app to ~/Applications, the
# plugins to ~/Library/Audio/Plug-Ins/{VST3,Components}. The app is the plugin's engine, so it is
# always installed; unticking "Infinite app" keeps it out of sight in
# ~/Library/Application Support/Infinite/Engine. The three ticks are payload-free packages that
# leave a marker; the hidden engine package runs last, reads them and calls
# `Infinite --install-plugins <formats>` (src/core/DawPluginInstall.h), the same copier as the app.
#
# The package is not signed (no paid Apple account): macOS blocks the first open, and the
# download page shows System Settings > Privacy & Security > Open Anyway. Files placed by
# Installer carry no quarantine flag, so DAWs load the plugins without any further step.
set -euo pipefail

APP_IN="$1"; OUT="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
[ -d "$APP_IN/Contents/Resources/Plugins" ] || { echo "build_pkg: $APP_IN carries no plugin bundles"; exit 1; }
VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP_IN/Contents/Info.plist")

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
mkdir -p "$W/res" "$W/pkgs" "$W/engine-root"

# ---- pages ----
cp "$HERE/resources/conclusion.html" "$W/res/"
sed "s/__VERSION__/$VERSION/g" "$HERE/resources/welcome.html" > "$W/res/welcome.html"
python3 "$ROOT/tools/installer/terms_page.py" "$ROOT/website/terms.html" "$W/res/license.html"
# Installer.app does not load linked stylesheets: inline style.css so pages get the system font, not Times.
python3 - "$HERE/resources/style.css" "$W/res"/*.html <<'PY'
import sys
css = open(sys.argv[1], encoding="utf-8").read()
for p in sys.argv[2:]:
    s = open(p, encoding="utf-8").read()
    assert '<link rel="stylesheet" href="style.css">' in s, p
    open(p, "w", encoding="utf-8").write(s.replace('<link rel="stylesheet" href="style.css">', "<style>\n" + css + "</style>"))
PY
python3 "$ROOT/tools/installer/make_art.py" "$W/art" >/dev/null
cp "$W/art/mac/background.png" "$W/res/background.png"

# ---- the three ticks: no payload, one marker each ----
for c in app vst3 au; do
    mkdir -p "$W/scripts-$c"
    cat > "$W/scripts-$c/postinstall" <<SH
#!/bin/bash
mkdir -p "\$HOME/Library/Caches/Infinite/installer" && touch "\$HOME/Library/Caches/Infinite/installer/$c"
exit 0
SH
    chmod +x "$W/scripts-$c/postinstall"
    pkgbuild --nopayload --scripts "$W/scripts-$c" --identifier "org.infinitedevices.installer.$c" \
        --version "$VERSION" "$W/pkgs/choice-$c.pkg" >/dev/null
done

# ---- the engine: the app itself, placed, then the plugins copied by the app ----
ditto "$APP_IN" "$W/engine-root/Infinite.app"
mkdir -p "$W/scripts-engine"
cat > "$W/scripts-engine/preinstall" <<'SH'
#!/bin/bash
# A copy left by an earlier install would be merged with the new payload; start clean.
rm -rf "$2/Infinite.app"
exit 0
SH
cat > "$W/scripts-engine/postinstall" <<'SH'
#!/bin/bash
# $2 is the install location, already inside the user's home (per-user install).
set -u
STAGE="$2/Infinite.app"
MARK="$HOME/Library/Caches/Infinite/installer"
APP="$STAGE"
if [ -f "$MARK/app" ]; then
    mkdir -p "$HOME/Applications"
    rm -rf "$HOME/Applications/Infinite.app"
    mv "$STAGE" "$HOME/Applications/Infinite.app"
    APP="$HOME/Applications/Infinite.app"
fi
xattr -dr com.apple.quarantine "$APP" 2>/dev/null
FORMATS=""
[ -f "$MARK/vst3" ] && FORMATS="vst3"
[ -f "$MARK/au" ] && FORMATS="${FORMATS:+$FORMATS,}au"
rm -rf "$MARK"
if [ -n "$FORMATS" ]; then
    "$APP/Contents/MacOS/Infinite" --install-plugins "$FORMATS" || exit 1
fi
exit 0
SH
chmod +x "$W/scripts-engine/preinstall" "$W/scripts-engine/postinstall"
# Not relocatable: Installer would otherwise "upgrade" any Infinite.app it finds elsewhere on disk.
pkgbuild --analyze --root "$W/engine-root" "$W/engine.plist" >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$W/engine.plist"
plutil -replace 0.BundleIsVersionChecked -bool NO "$W/engine.plist" # a reinstall or an older build still lands
pkgbuild --root "$W/engine-root" --component-plist "$W/engine.plist" --scripts "$W/scripts-engine" \
    --identifier org.infinitedevices.installer.engine --version "$VERSION" \
    --install-location "/Library/Application Support/Infinite/Engine" "$W/pkgs/engine.pkg" >/dev/null

# ---- the product ----
sed "s/__VERSION__/$VERSION/g" "$HERE/distribution.xml.in" > "$W/distribution.xml"
mkdir -p "$(dirname "$OUT")"
productbuild --distribution "$W/distribution.xml" --resources "$W/res" --package-path "$W/pkgs" "$OUT" >/dev/null
echo "built $OUT ($(du -h "$OUT" | cut -f1), version $VERSION)"
