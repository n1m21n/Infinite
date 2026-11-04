#!/bin/bash
# Builds and installs "Infinite Remote" (apps/watch) with your own Apple ID. A thin wrapper: it runs the same
# apps/watch/install_watch_app.sh that Infinite's "Set up watch app" button runs in its Terminal panel.
#   scripts/build_watch_app.sh                       find your team and watch, build, install
#   scripts/build_watch_app.sh --team ID --device ID override either (xcrun devicectl list devices)
set -e
if [ "$(uname)" != "Darwin" ]; then echo "Building the watch app needs a Mac with Xcode."; exit 1; fi
exec "$(cd "$(dirname "$0")/.." && pwd)/apps/watch/install_watch_app.sh" "$@"
