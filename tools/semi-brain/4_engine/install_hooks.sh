#!/bin/bash
# install_hooks.sh
# Keeps the Semi-Brain in sync without blocking git.
#
#   daemon  (default) a launchd agent running l1/brain_watchd.py: FSEvents on git refs, src/,
#           .claude/skills, docs/ and the session transcripts, 2 s debounce, then one incremental
#           sync at background QoS / nice 19. Replaces the batch sync after every commit.
#           The daemon itself runs at normal priority: it idles, but its brief server answers
#           the prompt hook and must not be throttled; the syncs it starts are taskpolicy -b.
#   hooks   post-commit / post-merge -> .git/hooks/sync-brain-bg, the fallback runner. It steps
#           aside while the daemon is alive (pidfile), so a commit never starts a second sync.
#   claude  a UserPromptSubmit hook in .claude/settings.json (gitignored, so per machine):
#           4_engine/hooks/prompt_brief.py adds the daemon's brief to every prompt.
#   sleep   a launchd agent running l1/sleep.py daily at 04:30 (or at wake if the Mac slept
#           through it) under taskpolicy -b / nice 19: retune weights, credit L0 notes, write
#           proposals. It skips itself while Infinite runs or the daemon is paused.
#
#   install_hooks.sh               hooks + daemon + sleep
#   install_hooks.sh --no-daemon   hooks only
#   install_hooks.sh --sleep       the sleep agent only (leaves the daemon as it is)
#   install_hooks.sh --uninstall-daemon

set -e
REPO_ROOT="$(git rev-parse --show-toplevel)"
GITDIR="$(cd "$REPO_ROOT" && cd "$(git rev-parse --git-common-dir)" && pwd)"
HOOKS_DIR="$GITDIR/hooks"
LABEL="com.infinite.semi-brain.watchd"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
SLEEP_LABEL="com.infinite.semi-brain.sleep"
SLEEP_PLIST="$HOME/Library/LaunchAgents/$SLEEP_LABEL.plist"
STATE_DIR="$REPO_ROOT/tools/semi-brain/l1/state"
PYTHON="${PYTHON:-/usr/bin/python3}"

uninstall_daemon() {
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  launchctl bootout "gui/$(id -u)/$SLEEP_LABEL" 2>/dev/null || true
  rm -f "$PLIST" "$SLEEP_PLIST"
}

install_sleep() {
  mkdir -p "$STATE_DIR" "$(dirname "$SLEEP_PLIST")"
  cat << PLIST_END > "$SLEEP_PLIST"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$SLEEP_LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/sbin/taskpolicy</string><string>-b</string>
    <string>/usr/bin/nice</string><string>-n</string><string>19</string>
    <string>$PYTHON</string><string>-W</string><string>ignore</string>
    <string>$REPO_ROOT/tools/semi-brain/l1/sleep.py</string>
  </array>
  <key>WorkingDirectory</key><string>$REPO_ROOT</string>
  <key>StartCalendarInterval</key>
  <dict><key>Hour</key><integer>4</integer><key>Minute</key><integer>30</integer></dict>
  <key>ProcessType</key><string>Background</string>
  <key>EnvironmentVariables</key>
  <dict><key>PATH</key><string>/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/opt/homebrew/bin</string></dict>
  <key>StandardOutPath</key><string>$STATE_DIR/sleep.log</string>
  <key>StandardErrorPath</key><string>$STATE_DIR/sleep.log</string>
</dict>
</plist>
PLIST_END
  launchctl bootout "gui/$(id -u)/$SLEEP_LABEL" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$SLEEP_PLIST"
  echo "sleep: $SLEEP_LABEL daily 04:30 ($SLEEP_PLIST); log $STATE_DIR/sleep.log"
}

if [ "$1" = "--uninstall-daemon" ]; then
  uninstall_daemon
  echo "Semi-Brain daemon removed; the post-commit/post-merge hooks sync on their own again."
  exit 0
fi
if [ "$1" = "--sleep" ]; then
  [ "$(uname)" = "Darwin" ] || { echo "sleep: macOS only (launchd)"; exit 1; }
  install_sleep
  exit 0
fi

cat << 'EOF' > "$HOOKS_DIR/sync-brain-bg"
#!/bin/bash
# Semi-brain sync, shared by post-commit and post-merge. One runner at a time (a trigger while
# one runs just marks "run again"), background QoS, lowest CPU priority, so builds and
# benchmarks keep the machine. scripts/bench/ab.sh waits for it to finish.
# While the watch daemon (brain_watchd) is alive it has already seen this change: step aside.
# The brain lives in the main checkout: a commit in another worktree syncs that one, and a
# checkout without the incremental sync (l1/sync.py; the pre-v2 brain rebuilt everything, over
# an hour of CPU) is never synced from a hook.
GITDIR="$(cd "$(git rev-parse --git-common-dir)" && pwd)"
PID="$(cat "$GITDIR/brain_watchd.pid" 2>/dev/null)"
[ -n "$PID" ] && kill -0 "$PID" 2>/dev/null && exit 0
ROOT="$(dirname "$GITDIR")"
[ -f "$ROOT/tools/semi-brain/l1/sync.py" ] || exit 0
cd "$ROOT" || exit 0
LOCK="$GITDIR/sync_brain.lock"; PENDING="$GITDIR/sync_brain.pending"
touch "$PENDING"
mkdir "$LOCK" 2>/dev/null || exit 0      # a runner is active; it will pick up PENDING
(
  trap 'rmdir "$LOCK"' EXIT
  while [ -e "$PENDING" ]; do
    rm -f "$PENDING"
    taskpolicy -b nice -n 19 python3 tools/semi-brain/4_engine/sync_brain.py --sync >/dev/null 2>&1
  done
) </dev/null >/dev/null 2>&1 &
EOF

for h in post-commit post-merge; do
  printf '#!/bin/bash\n"$(git rev-parse --git-common-dir)/hooks/sync-brain-bg"\n' > "$HOOKS_DIR/$h"
done
chmod +x "$HOOKS_DIR/sync-brain-bg" "$HOOKS_DIR/post-commit" "$HOOKS_DIR/post-merge"
echo "hooks: post-commit, post-merge -> sync-brain-bg"

# Claude Code hooks, merged into the project's .claude/settings.json (other keys kept).
"$PYTHON" - "$REPO_ROOT/.claude/settings.json" << 'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
cfg = json.loads(path.read_text()) if path.exists() else {}
hooks = cfg.setdefault("hooks", {})
cmd = 'python3 "$CLAUDE_PROJECT_DIR/tools/semi-brain/4_engine/hooks/prompt_brief.py"'
entries = [e for e in hooks.get("UserPromptSubmit", [])
           if not any("prompt_brief.py" in h.get("command", "") for h in e.get("hooks", []))]
entries.append({"hooks": [{"type": "command", "command": cmd, "timeout": 5}]})
hooks["UserPromptSubmit"] = entries
path.parent.mkdir(exist_ok=True)
path.write_text(json.dumps(cfg, indent=2) + "\n")
print(f"claude: UserPromptSubmit -> prompt_brief.py ({path})")
PY

[ "$1" = "--no-daemon" ] && exit 0
[ "$(uname)" = "Darwin" ] || { echo "daemon: macOS only (launchd + FSEvents); hooks only here"; exit 0; }

mkdir -p "$STATE_DIR" "$(dirname "$PLIST")"
cat << EOF > "$PLIST"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$PYTHON</string>
    <string>$REPO_ROOT/tools/semi-brain/l1/brain_watchd.py</string>
  </array>
  <key>WorkingDirectory</key><string>$REPO_ROOT</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>30</integer>
  <key>ProcessType</key><string>Standard</string>
  <key>EnvironmentVariables</key>
  <dict><key>PATH</key><string>/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/opt/homebrew/bin</string></dict>
  <key>StandardOutPath</key><string>$STATE_DIR/watchd.stdout.log</string>
  <key>StandardErrorPath</key><string>$STATE_DIR/watchd.stdout.log</string>
</dict>
</plist>
EOF
uninstall_daemon_keep_plist() { launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true; }
uninstall_daemon_keep_plist
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "daemon: $LABEL loaded ($PLIST); log $STATE_DIR/watchd.log"
install_sleep
