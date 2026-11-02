#!/bin/bash
# Builds Infinite Remote with your Apple ID's team and installs it on your watch, start to finish. Infinite's
# "Set up watch app" window runs this in its Terminal panel (so a password prompt works); scripts/build_watch_app.sh
# runs the same file. Safe to run again at any point.
#
#   install_watch_app.sh [--team TEAMID] [--device DEVICEID] [--source DIR] [--work DIR] [--health 0|1]
#
# Without --team and --device it finds your free Personal Team and the watch itself.
# Progress goes to the screen as "Step n/5: ..." and to <work>/status.json, which the Apple Watch node reads:
#   {"step":3,"total":5,"state":"running|ok|failed","message":"...","installedAt":<epoch>,"updatedAt":<epoch>}
set -u

total=5
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
      *) echo "Unknown option $1"; exit 2 ;;
   esac
done

mkdir -p "$work" || { echo "Cannot create $work"; exit 1; }
status_file="$work/status.json"
log="$(mktemp -t infinite-watch)"
cur=0

# installedAt survives a failed or running reinstall: the app on the watch is still the old one
installed_at="$(sed -n -E 's/.*"installedAt": *([0-9]+).*/\1/p' "$status_file" 2>/dev/null | head -1)"
installed_at="${installed_at:-0}"

write_status() {   # state step message
   local msg="${3//\\/\\\\}"
   msg="${msg//\"/\\\"}"
   printf '{"step":%s,"total":%s,"state":"%s","message":"%s","installedAt":%s,"updatedAt":%s}\n' \
      "$2" "$total" "$1" "$msg" "$installed_at" "$(date +%s)" >"$status_file.tmp" && mv "$status_file.tmp" "$status_file"
}

step() {
   cur="$1"
   echo "Step $1/$total: $2..."
   write_status running "$1" "$2"
}

fail() {   # reason, then the fix
   echo
   echo "Could not finish: $1"
   [ -n "${2:-}" ] && echo "To fix: $2"
   write_status failed "$cur" "$1${2:+ To fix: $2}"
   if [ -d "$work/InfiniteRemote.xcodeproj" ] && [ -f "$work/Config/Local.xcconfig" ]; then
      echo "Opening the project in Xcode with your team filled in: pick your watch next to Run and press Run."
      open "$work/InfiniteRemote.xcodeproj" 2>/dev/null
   fi
   exit 1
}

lastlog() { grep -v '^[[:space:]]*$' "$log" | tail -1; }

# $1 = team | watch, $2 = a JSON file; prints the id or nothing
pick() {
   osascript -l JavaScript -e '
      function run(a) {
         ObjC.import("Foundation");
         var j = JSON.parse($.NSString.stringWithContentsOfFileEncodingError(a[1], $.NSUTF8StringEncoding, null).js);
         if (a[0] == "team") {
            var found = [];
            (function walk(o) { if (o && typeof o == "object") { if (o.teamID) found.push(o); for (var k in o) walk(o[k]); } })(j);
            found.sort(function (x, y) { return (y.isFreeProvisioningTeam ? 1 : 0) - (x.isFreeProvisioningTeam ? 1 : 0); });
            return found.length ? found[0].teamID : "";
         }
         var ds = (j.result || {}).devices || [];
         for (var i = 0; i < ds.length; i++) {
            var hw = ds[i].hardwareProperties || {}, p = ds[i].deviceProperties || {};
            var kind = ((hw.deviceType || "") + " " + (hw.platform || "") + " " + (hw.productType || "")).toLowerCase();
            if (kind.indexOf("watch") >= 0 && p.developerModeStatus == "enabled") return ds[i].identifier;
         }
         return "";
      }' "$1" "$2" 2>/dev/null
}

# 1: Xcode, its license, and xcode-select -----------------------------------------------------------------------
step 1 "checking Xcode"
app="$(ls -d /Applications/Xcode*.app 2>/dev/null | head -1)"
dev="$(xcode-select -p 2>/dev/null)"
case "$dev" in
   *.app/Contents/Developer) ;;
   *)
      if [ -z "$app" ]; then
         open "https://apps.apple.com/app/xcode/id497799835" 2>/dev/null
         fail "Xcode is not installed." "install Xcode from the App Store, open it once, then run this again."
      fi
      echo "Pointing the command line tools at $app (this asks for your Mac password)."
      sudo xcode-select -s "$app/Contents/Developer" || fail "xcode-select could not be switched to $app." "run: sudo xcode-select -s \"$app/Contents/Developer\""
      ;;
esac
if ! xcodebuild -license check >/dev/null 2>&1; then
   echo "Accepting the Xcode license (this asks for your Mac password)."
   sudo xcodebuild -license accept || fail "The Xcode license was not accepted." "run: sudo xcodebuild -license accept"
fi
if ! xcodebuild -checkFirstLaunchStatus >/dev/null 2>&1; then
   echo "Finishing Xcode's first-launch setup (this may ask for your Mac password)."
   sudo xcodebuild -runFirstLaunch >"$log" 2>&1 || fail "Xcode's first-launch setup failed: $(lastlog)" "open Xcode once and let it install its components."
fi

# 2: team and watch ----------------------------------------------------------------------------------------------
step 2 "finding your Apple ID team and your watch"
if [ -z "$team" ]; then
   defaults export com.apple.dt.Xcode - 2>/dev/null | plutil -convert json -o "$log" - 2>/dev/null
   team="$(pick team "$log")"
fi
if [ -z "$team" ]; then
   open -a Xcode 2>/dev/null
   fail "No Apple ID team found in Xcode." "in Xcode open Settings > Accounts, press +, sign in with your Apple ID, then run this again."
fi
if [ -z "$device" ]; then
   xcrun devicectl list devices --json-output "$log" >/dev/null 2>&1
   device="$(pick watch "$log")"
fi
if [ -z "$device" ]; then
   fail "Your watch is not visible to this Mac." "connect your iPhone with a cable once, unlock it, turn on Developer Mode on the iPhone and the watch (Settings > Privacy & Security), keep the watch unlocked, then run this again."
fi

# 3: project ------------------------------------------------------------------------------------------------------
step 3 "preparing the project"
rsync -a --delete --exclude build --exclude status.json --exclude 'Config/Local.xcconfig' --exclude 'Config/Info.plist' \
   "$source_dir/" "$work/" >"$log" 2>&1 || fail "Could not copy the project: $(lastlog)"
# a bundle id that is unique per team: free teams cannot share one
hash="$(printf '%s' "$team" | shasum | cut -c1-8)"
cat >"$work/Config/Local.xcconfig" <<X
DEVELOPMENT_TEAM = $team
INFINITE_BUNDLE_PREFIX = org.infinitedevices.remote.$hash
INFINITE_WATCH_HEALTH = $health
X
if [ ! -d "$work/InfiniteRemote.xcodeproj" ]; then
   command -v xcodegen >/dev/null 2>&1 || fail "This copy has no Xcode project and xcodegen is not installed." "run: brew install xcodegen"
   (cd "$work" && xcodegen generate --quiet) >"$log" 2>&1 || fail "xcodegen failed: $(lastlog)"
fi

# 4: build --------------------------------------------------------------------------------------------------------
step 4 "building for your watch (the first build takes a few minutes)"
build() {
   xcodebuild -project "$work/InfiniteRemote.xcodeproj" -scheme InfiniteRemote -configuration Release \
      -destination 'generic/platform=watchOS' -derivedDataPath "$work/build" -allowProvisioningUpdates build >"$log" 2>&1
}
if ! build; then
   if grep -qiE "watchOS .*(is not installed|must be installed)|platform .*watchOS.*not installed" "$log"; then
      echo "Downloading the watchOS components for Xcode..."
      xcodebuild -downloadPlatform watchOS || fail "The watchOS components could not be downloaded." "open Xcode > Settings > Components and install watchOS."
      build || fail "The build failed: $(lastlog)" "if it mentions signing, check that your Apple ID is signed in under Xcode > Settings > Accounts."
   else
      fail "The build failed: $(lastlog)" "if it mentions signing, check that your Apple ID is signed in under Xcode > Settings > Accounts."
   fi
fi
built="$work/build/Build/Products/Release-watchos/InfiniteRemote.app"
[ -d "$built" ] || fail "The build finished but the app was not found."

# 5: install ------------------------------------------------------------------------------------------------------
step 5 "installing on your watch"
xcrun devicectl device install app --device "$device" "$built" >"$log" 2>&1 \
   || fail "The install failed: $(lastlog)" "unlock the watch and keep it close to the iPhone, then run this again."
installed_at="$(date +%s)"
write_status ok "$total" "Installed. Open Infinite Remote on the watch."
echo
echo "Done. On the watch: Settings > General > VPN & Device Management > trust your Apple ID, then open Infinite Remote."
