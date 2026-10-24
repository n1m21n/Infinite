#!/bin/bash
# Copy a built Infinite.app to ~/Desktop and make it the copy that opens .inf files.
#
# Why: every build on this Mac (each worktree's build/, bench baselines, /Applications) has the same bundle id, and
# macOS keeps the .inf default per bundle id, not per path, so double-click opened whichever copy Launch Services
# liked best - an old v0.5.0 Codex build - and fixed bugs looked unfixed. Unregistering the others is the only lever;
# no files are touched. Launching any other build registers it again, so this runs on every deploy.
#
# Usage: scripts/deploy_desktop.sh [path/to/Infinite.app]   (default build/Infinite.app)
# Exit: 0 deployed and claimed, 1 Infinite is running (quit it first), 2 no build, 3 another copy still opens .inf
set -u
SRC="${1:-build/Infinite.app}"
DST="$HOME/Desktop/Infinite.app"
LS=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
[ -d "$SRC" ] || { echo "no build at $SRC"; exit 2; }
# Never put a dev/debug build on the Desktop: it shows debug-only menu items and is slower.
python3 "$(dirname "$0")/check_release_binary.py" "$SRC" || exit 2
if pgrep -x Infinite >/dev/null; then echo "Infinite is running - quit it, then rerun"; exit 1; fi
rm -rf "$DST" && cp -R "$SRC" "$DST"
"$LS" -dump 2>/dev/null | grep -E '^path: .*/Infinite\.app ' | sed -E 's/^path: +//; s/ \(0x[0-9a-f]+\)$//' | sort -u |
while read -r p; do [ "$p" = "$DST" ] || "$LS" -u "$p" 2>/dev/null; done
"$LS" -f "$DST"
probe="$(mktemp -d)/probe.inf"; : > "$probe"
opener=$(osascript -l JavaScript -e 'ObjC.import("AppKit"); var u=$.NSWorkspace.sharedWorkspace.URLForApplicationToOpenURL($.NSURL.fileURLWithPath("'"$probe"'")); u.isNil() ? "none" : u.path.js' 2>/dev/null)
rm -f "$probe"
echo "deployed $DST; .inf opens: $opener"
[ "$opener" = "$DST" ] || [ "$opener" = "none" ] || exit 3
