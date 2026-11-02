#!/bin/bash
# Builds Infinite Remote with your Apple ID's team and installs it on your watch. Used by Infinite's
# "Set up watch app" window and by scripts/build_watch_app.sh, so both do the same steps.
#
#   install.sh --team TEAMID [--device DEVICEID] [--source DIR] [--work DIR] [--health 0|1]
#
# Prints one "STEP <text>" line per stage and, on failure, "ERROR <last line>" then "FALLBACK" after it has
# opened the project in Xcode with the team filled in (press Run there). Exit code 0 only when installed.
set -u

team=""; device=""; health=0
source_dir="$(cd "$(dirname "$0")" && pwd)"
work="$HOME/Library/Application Support/Infinite/WatchRemote"
while [ $# -gt 0 ]; do
   case "$1" in
      --team) team="$2"; shift 2 ;;
      --device) device="$2"; shift 2 ;;
      --source) source_dir="$2"; shift 2 ;;
      --work) work="$2"; shift 2 ;;
      --health) health="$2"; shift 2 ;;
      *) echo "ERROR unknown option $1"; exit 2 ;;
   esac
done
[ -n "$team" ] || { echo "ERROR no team id given"; exit 2; }

log="$(mktemp -t infinite-watch)"
fail() {
   local last
   last="$(grep -v '^[[:space:]]*$' "$log" | tail -1)"
   echo "ERROR ${1:-$last}"
   if [ -d "$work/InfiniteRemote.xcodeproj" ]; then
      open "$work/InfiniteRemote.xcodeproj" 2>/dev/null
      echo "FALLBACK"
   fi
   exit 1
}

echo "STEP Preparing the project"
mkdir -p "$work" || fail "cannot create $work"
rsync -a --delete --exclude build --exclude 'Config/Local.xcconfig' --exclude 'Config/Info.plist' "$source_dir/" "$work/" >"$log" 2>&1 || fail

# a bundle id that is unique per team: free teams cannot share one
hash="$(printf '%s' "$team" | shasum | cut -c1-8)"
cat >"$work/Config/Local.xcconfig" <<X
DEVELOPMENT_TEAM = $team
INFINITE_BUNDLE_PREFIX = org.infinitedevices.remote.$hash
INFINITE_WATCH_HEALTH = $health
X

if [ ! -d "$work/InfiniteRemote.xcodeproj" ]; then
   command -v xcodegen >/dev/null 2>&1 || fail "this copy has no Xcode project and xcodegen is not installed (brew install xcodegen)"
   echo "STEP Generating the Xcode project"
   (cd "$work" && xcodegen generate --quiet) >"$log" 2>&1 || fail
fi

echo "STEP Building (the first build takes a few minutes)"
xcodebuild -project "$work/InfiniteRemote.xcodeproj" -scheme InfiniteRemote -configuration Release \
   -destination 'generic/platform=watchOS' -derivedDataPath "$work/build" \
   -allowProvisioningUpdates build >"$log" 2>&1 || fail
app="$work/build/Build/Products/Release-watchos/InfiniteRemote.app"
[ -d "$app" ] || fail "build finished but the app was not found"

if [ -z "$device" ]; then
   echo "STEP Built; no watch given, opening Xcode"
   open "$work/InfiniteRemote.xcodeproj"
   echo "FALLBACK"
   exit 0
fi
echo "STEP Installing on your watch"
xcrun devicectl device install app --device "$device" "$app" >"$log" 2>&1 || fail
echo "DONE"
