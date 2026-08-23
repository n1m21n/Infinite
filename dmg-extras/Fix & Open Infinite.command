#!/usr/bin/env bash
# Fix & Open Infinite.command
#
# What this does, in plain terms:
#   1. Finds Infinite.app (in /Applications, or right next to this script).
#   2. Clears the macOS "quarantine" flag on it — the flag that, combined
#      with Infinite not being notarized by Apple, makes macOS report it
#      as "damaged and can't be opened."
#   3. Opens Infinite.
#
# This only touches Infinite.app. It does not change any system security
# setting, does not affect any other app, and does not download or send
# anything anywhere. Read every line above (or open this file in a text
# editor) before running it if you want to check.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP="/Applications/Infinite.app"
if [ ! -d "$APP" ]; then
    APP="$SCRIPT_DIR/Infinite.app"
fi

if [ ! -d "$APP" ]; then
    echo "Couldn't find Infinite.app in /Applications or next to this script."
    echo "Drag Infinite.app into Applications first, then run this again."
    read -n 1 -s -r -p "Press any key to close..."
    echo
    exit 1
fi

echo "This will clear macOS's quarantine flag on:"
echo "  $APP"
echo "so it opens without the \"damaged\" warning, then launch it."
echo "See \"Read Me First.txt\" in this DMG for why that warning shows up."
echo
read -r -p "Continue? [y/N] " REPLY
case "$REPLY" in
    [yY]*) ;;
    *) echo "Cancelled."; exit 0 ;;
esac

xattr -cr "$APP"
echo "Done. Opening Infinite..."
open "$APP"
