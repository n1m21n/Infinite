#!/bin/bash
# Install the 5-minute laptop sync job on THIS laptop (launchd). Safe to re-run.
#   scripts/install_laptop_sync.sh           install / update
#   scripts/install_laptop_sync.sh remove    uninstall
set -e
LABEL="org.infinitedevices.laptopsync"
DIR="$HOME/Library/Application Support/infinite-sync"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/laptopsync.log"
DOMAIN="gui/$(id -u)"

launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
[ "${1:-}" = "remove" ] && { rm -f "$PLIST"; echo "removed"; exit 0; }

mkdir -p "$DIR" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
cp "$(dirname "$0")/sync_laptops.sh" "$DIR/sync_laptops.sh"
chmod +x "$DIR/sync_laptops.sh"

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array>
    <string>/bin/bash</string><string>$DIR/sync_laptops.sh</string><string>tick</string>
  </array>
  <key>StartInterval</key><integer>300</integer>
  <key>RunAtLoad</key><true/>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
EOF

launchctl bootstrap "$DOMAIN" "$PLIST"
echo "installed: every 5 min, log at $LOG"
