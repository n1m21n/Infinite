#!/bin/bash
# Launch the Desktop Infinite.app as a brand-new user would see it: an empty home, so no settings,
# no Terminal welcome seen, no agent set up. Your real profile is untouched.
# Usage: scripts/first_install.sh [app] [fresh-home-dir]   (defaults: ~/Desktop/Infinite.app, ~/infinite-first-install)
APP="${1:-$HOME/Desktop/Infinite.app}"
FRESH="${2:-$HOME/infinite-first-install}"
[ -d "$APP" ] || { echo "no app at $APP"; exit 2; }
rm -rf "$FRESH" && mkdir -p "$FRESH"
echo "fresh home: $FRESH"
HOME="$FRESH" exec "$APP/Contents/MacOS/Infinite"
