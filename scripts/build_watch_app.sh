#!/bin/bash
# Builds and installs "Infinite Remote" (apps/watch) with the user's own Apple ID.
#   scripts/build_watch_app.sh           generate the Xcode project and open it
#   scripts/build_watch_app.sh --device  build for the paired watch with xcodebuild (needs the team set)
# Set INFINITE_WATCH_HEALTH=1 to build the heart-rate version (needs the HealthKit capability).
set -e
cd "$(dirname "$0")/../apps/watch"

if [ "$(uname)" != "Darwin" ]; then echo "Building the watch app needs a Mac with Xcode."; exit 1; fi
if ! xcodebuild -version >/dev/null 2>&1; then
   echo "Xcode is not installed (or xcode-select points at the command line tools)."
   echo "Install Xcode from the App Store, open it once, then run: sudo xcode-select -s /Applications/Xcode.app"
   exit 1
fi
if ! command -v xcodegen >/dev/null 2>&1; then
   echo "xcodegen is missing. Install it with:  brew install xcodegen"
   exit 1
fi

if [ ! -f Config/Local.xcconfig ]; then
   cp Config/Local.xcconfig.example Config/Local.xcconfig
   echo "Created apps/watch/Config/Local.xcconfig. Put your team id and bundle prefix in it, then run this again."
   open -t Config/Local.xcconfig 2>/dev/null || true
   exit 1
fi
if [ -n "$INFINITE_WATCH_HEALTH" ]; then
   sed -i '' -E "s/^INFINITE_WATCH_HEALTH *=.*/INFINITE_WATCH_HEALTH = $INFINITE_WATCH_HEALTH/" Config/Local.xcconfig
fi
team=$(sed -n -E 's/^DEVELOPMENT_TEAM *= *([A-Za-z0-9]+).*/\1/p' Config/Local.xcconfig | head -1)
prefix=$(sed -n -E 's/^INFINITE_BUNDLE_PREFIX *= *([^ ]+).*/\1/p' Config/Local.xcconfig | head -1)
if [ -z "$team" ] || [ -z "$prefix" ] || [ "$prefix" = "com.yourname" ]; then
   echo "Set DEVELOPMENT_TEAM and INFINITE_BUNDLE_PREFIX in apps/watch/Config/Local.xcconfig first."
   exit 1
fi

xcodegen generate --quiet

if [ "$1" = "--device" ]; then
   xcodebuild -project InfiniteRemote.xcodeproj -scheme InfiniteRemote -configuration Release \
      -destination 'generic/platform=watchOS' -allowProvisioningUpdates build
   echo "Built. To install, use Xcode: pick your watch next to the Run button and press Run."
else
   open InfiniteRemote.xcodeproj
   echo "Opened in Xcode: pick your watch next to the Run button and press Run."
fi
