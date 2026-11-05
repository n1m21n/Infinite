#!/bin/bash
# Run Infinite as a brand-new user: no settings, no recents, no chats, no agent sign-ins, no setup done.
#
# Your real profile is never touched. The app runs with HOME set to a throw-away folder, so everything it
# writes (Application Support, ~/.local/bin, ~/.claude, ~/.codex, ~/.gemini) lands there and is deleted when
# you quit, along with its own empty keychain. Your shell startup files are linked in, so a login shell
# still finds node, npm and the agents' programs the way it does for you
# (that is what Infinite's setup and the agents use).
#
# Usage: scripts/fresh_profile.sh [path/to/Infinite.app]   (default ~/Desktop/Infinite.app)
set -u
APP="${1:-$HOME/Desktop/Infinite.app}"
BIN="$APP/Contents/MacOS/Infinite"
[ -x "$BIN" ] || { echo "no app at $APP"; exit 2; }
REAL_HOME="$HOME"
FRESH="$(mktemp -d /tmp/infinite-fresh.XXXXXX)"
for f in .zshenv .zprofile .zshrc .bash_profile .bashrc .profile; do
  [ -e "$REAL_HOME/$f" ] && ln -s "$REAL_HOME/$f" "$FRESH/$f"
done
# An empty login keychain of its own: agents that keep their sign-in in the Keychain (Gemini, Claude) would
# otherwise hit macOS's "Keychain Not Found" dialog. The keychain list is read from $HOME, so yours is untouched.
KC="$FRESH/Library/Keychains/login.keychain-db"
mkdir -p "$FRESH/Library/Keychains"
HOME="$FRESH" security create-keychain -p "" "$KC" \
  && HOME="$FRESH" security list-keychains -d user -s "$KC" \
  && HOME="$FRESH" security default-keychain -s "$KC" \
  && HOME="$FRESH" security set-keychain-settings "$KC"
echo "fresh profile: $FRESH (deleted on quit)"
HOME="$FRESH" "$BIN"
rm -rf "$FRESH"
