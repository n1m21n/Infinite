#!/usr/bin/env bash
# Infinite installer for Linux. Run it from the unpacked folder:   ./install.sh
#
#   terms -> Infinite app? -> VST3? -> install -> next steps
#
#   --yes          accept the terms and install everything without asking
#   --no-app       skip the app's menu entry and `infinite` command
#   --no-vst3      skip the VST3 plugins
#   --uninstall    remove everything this script installed
#
# Everything goes into your home folder, no sudo:
#   ~/.local/share/infinite/Infinite.AppImage   the app, also the plugin's engine (always installed)
#   ~/.vst3/Infinite Instrument.vst3, Infinite FX.vst3
#   ~/.local/share/applications/infinite.desktop and ~/.local/bin/infinite   (Infinite app)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
DEST="$DATA/infinite"
ENGINE="$DEST/Infinite.AppImage"
BIN="$HOME/.local/bin/infinite"
DESKTOP="$DATA/applications/infinite.desktop"
ICON="$DATA/icons/hicolor/512x512/apps/infinite.png"

YES=0; APP=1; VST3=1; UNINSTALL=0
for a in "$@"; do
    case "$a" in
        --yes|-y) YES=1 ;;
        --no-app) APP=0 ;;
        --no-vst3) VST3=0 ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown option: $a (see --help)"; exit 2 ;;
    esac
done

if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; E=$'\e[38;2;255;107;53m'; R=$'\e[0m'; else B=; D=; E=; R=; fi
say() { printf '%s\n' "$*"; }
ask() { # ask "question" default(1|0) -> 0 yes / 1 no
    local q="$1" def="$2" hint reply
    [ "$def" = 1 ] && hint="[Y/n]" || hint="[y/N]"
    if [ "$YES" = 1 ] || [ ! -t 0 ]; then [ "$def" = 1 ]; return; fi
    read -r -p "$q $hint " reply
    case "$reply" in [yY]*) return 0 ;; [nN]*) return 1 ;; *) [ "$def" = 1 ] ;; esac
}

# The AppImage runtime needs FUSE; without it, run the same file extracted.
run_engine() {
    "$ENGINE" "$@" 2>/dev/null || APPIMAGE_EXTRACT_AND_RUN=1 "$ENGINE" "$@"
}

if [ "$UNINSTALL" = 1 ]; then
    [ -x "$ENGINE" ] && run_engine --uninstall-plugins || true
    rm -f "$BIN" "$DESKTOP" "$ICON"
    rm -rf "$DEST"
    say "Infinite is removed. Your patches, settings and licence key stay in ~/.config and ~/.local/share/Infinite."
    exit 0
fi

SRC="$(ls "$HERE"/Infinite-*.AppImage 2>/dev/null | head -1 || true)"
[ -n "$SRC" ] || { say "No Infinite AppImage next to this script."; exit 1; }

say ""
say "${B}Infinite${R}  ${D}a visual instrument and effect for your DAW, and the standalone app${R}"
say ""
if [ "$YES" != 1 ]; then
    say "Terms of sale and use: ${E}TERMS.txt${R} in this folder, also https://infinitedevices.org/terms.html"
    ask "Do you accept the terms?" 0 || { say "Nothing was installed."; exit 1; }
    say ""
    ask "Install the Infinite app (menu entry and the 'infinite' command)?" "$APP" && APP=1 || APP=0
    ask "Install the VST3 plugins (Infinite Instrument, Infinite FX) to ~/.vst3?" "$VST3" && VST3=1 || VST3=0
    say ""
fi

mkdir -p "$DEST"
cp -f "$SRC" "$ENGINE.new" && chmod +x "$ENGINE.new" && mv -f "$ENGINE.new" "$ENGINE"
say "  installed  $ENGINE"

if [ "$APP" = 1 ]; then
    mkdir -p "$(dirname "$BIN")" "$(dirname "$DESKTOP")" "$(dirname "$ICON")"
    ln -sf "$ENGINE" "$BIN"
    [ -f "$HERE/infinite.png" ] && cp -f "$HERE/infinite.png" "$ICON"
    cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Name=Infinite
Comment=Visual instrument and audiovisual workstation
Exec="$ENGINE" %F
Icon=infinite
Terminal=false
Categories=AudioVideo;Audio;Video;Graphics;
MimeType=application/x-infinite-patch;
DESK
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(dirname "$DESKTOP")" >/dev/null 2>&1 || true
    say "  installed  the Infinite app (menu entry, 'infinite' command)"
fi

if [ "$VST3" = 1 ]; then
    run_engine --install-plugins vst3 | sed 's/^/  /'
fi

say ""
say "${B}Infinite is installed.${R} Next:"
# steps:begin (tools/installer/sync_steps.py)
say "  1. Open your DAW and rescan plugins (~/.vst3)."
say "  2. Add Infinite Instrument to an instrument track, or Infinite FX to an audio track, and open it."
say "  3. The licence screen opens. Copy the key from your email and click Paste key."
say "  4. Play a few notes."
# steps:end
say "${D}  Remove everything later with: ./install.sh --uninstall${R}"
