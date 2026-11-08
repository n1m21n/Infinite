#!/usr/bin/env bash
# Install-route check (docs/plans/vst-au/README.md 3.9): runs the app's own copier the ways users
# reach it, in a throwaway HOME, and checks what lands where.
#   tools/plugin/install_check.sh [APP_EXE]
# Routes: direct exe, a symlink to it (Homebrew's `infinite`), a re-install over an existing copy
# (an update), then uninstall. Settings > DAW plugin calls the same DawPlugin::Install.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
case "$(uname -s)" in
  Darwin) EXE="${1:-$ROOT/build-c1/Infinite.app/Contents/MacOS/Infinite}" ;;
  MINGW*|MSYS*|CYGWIN*) EXE="${1:-$ROOT/build/Release/Infinite.exe}" ;;
  *) EXE="${1:-$ROOT/build/Infinite}" ;;
esac
[ -x "$EXE" ] || { echo "no app at $EXE"; exit 2; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/home" "$T/lic" "$T/bin"
export HOME="$T/home" USERPROFILE="$T/home" LOCALAPPDATA="$T/home/AppData/Local" INFINITE_LICENSE_DIR="$T/lic"
case "$(uname -s)" in
  Darwin) DIRS=("$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"); N=4 ;;
  MINGW*|MSYS*|CYGWIN*) DIRS=("$LOCALAPPDATA/Programs/Common/VST3"); N=2 ;;
  *) DIRS=("$HOME/.vst3"); N=2 ;;
esac
fail=0
pass() { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
count() { local c=0 d; for d in "${DIRS[@]}"; do c=$((c + $(ls -d "$d"/Infinite* 2>/dev/null | wc -l))); done; echo "$c"; }

"$EXE" --install-plugins >"$T/out" 2>&1 && [ "$(count)" = "$N" ] && pass "direct: $N bundles" || { bad "direct"; cat "$T/out"; }
[ -s "$INFINITE_LICENSE_DIR/plugin-install.txt" ] && pass "record: $(head -1 "$INFINITE_LICENSE_DIR/plugin-install.txt")" || bad "record missing"
engine="$(sed -n 2p "$INFINITE_LICENSE_DIR/plugin-install.txt")"
[ -x "$engine" ] && pass "engine path runs" || bad "engine path '$engine'"
touch "${DIRS[0]}/Infinite FX.vst3/stale-marker"
"$EXE" --install-plugins >"$T/out" 2>&1 && [ ! -e "${DIRS[0]}/Infinite FX.vst3/stale-marker" ] && [ "$(count)" = "$N" ] \
  && pass "update replaces the old copy" || { bad "update"; cat "$T/out"; }
"$EXE" --uninstall-plugins >"$T/out" 2>&1 && [ "$(count)" = 0 ] && [ ! -e "$INFINITE_LICENSE_DIR/plugin-install.txt" ] \
  && pass "uninstall" || { bad "uninstall"; cat "$T/out"; }
if ! uname -s | grep -qE "MINGW|MSYS|CYGWIN"; then
  ln -s "$EXE" "$T/bin/infinite"
  "$T/bin/infinite" --install-plugins >"$T/out" 2>&1 && [ "$(count)" = "$N" ] && pass "symlinked exe (Homebrew)" || { bad "symlinked exe"; cat "$T/out"; }
  engine="$(sed -n 2p "$INFINITE_LICENSE_DIR/plugin-install.txt" 2>/dev/null)"
  [ "$engine" != "$T/bin/infinite" ] && pass "engine is the real exe" || bad "engine recorded as the link"
fi
exit $fail
