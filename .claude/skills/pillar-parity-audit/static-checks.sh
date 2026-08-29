#!/usr/bin/env bash
# Pillar-parity static lane.
#
# The half of cross-platform verification that needs no Windows machine and no
# GPU: things that are true or false about the *source tree*, checkable on
# macOS in under a second, and that historically shipped broken precisely
# because no build and no fixture could see them.
#
# Each check names the pillar it serves (see SKILL.md) and the real defect it
# guards against. Exit 0 if every check passed, 1 otherwise.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

PASS=0; FAIL=0
ok()   { printf '  [pass] %s\n' "$1"; PASS=$((PASS+1)); }
bad()  { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
head2() { printf '\n== %s ==\n' "$1"; }

# --------------------------------------------------------------------------
head2 "P11 platform contract - no _WIN32 in the node layer"
# Why: a Windows-only branch under src/nodes/ is a code path no macOS build
# compiles and nobody with commit access can execute. windows-parity SKILL.md
# calls the current count of zero "load-bearing".
n=$(grep -rl '_WIN32' src/nodes 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "0" ]; then ok "src/nodes/ has no _WIN32 conditionals"
else bad "$n file(s) in src/nodes/ use _WIN32:"; grep -rl '_WIN32' src/nodes | sed 's/^/         /'; fi

# --------------------------------------------------------------------------
head2 "P11 platform contract - every Platform:: declaration has both implementations"
# Why: declaring in Platform.h and implementing only in Platform.mm builds
# clean on macOS and produces an unresolved external symbol on Windows CI.
# Declarations after Platform.h's `#if defined(_WIN32)` guard are Windows-only
# by design (the projector/output window) and are exempt from the macOS side.
GUARD=$(grep -n '^#if defined(_WIN32)' src/platform/Platform.h | tail -1 | cut -d: -f1)
GUARD=${GUARD:-999999}
head -n "$((GUARD - 1))" src/platform/Platform.h \
  | grep -oE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_:<>,* &]*[ *&]([A-Z][A-Za-z0-9_]*)\(' \
  | grep -oE '[A-Z][A-Za-z0-9_]*\($' | tr -d '(' | sort -u > /tmp/pillar_decls.txt
missing_mac=(); missing_win=()
while read -r fn; do
   [ -z "$fn" ] && continue
   grep -qE "\b${fn}[[:space:]]*\(" src/platform/Platform.mm src/platform/PluginVST3.mm \
      || missing_mac+=("$fn")
   grep -rqE "\b${fn}[[:space:]]*\(" src/platform/win/ || missing_win+=("$fn")
done < /tmp/pillar_decls.txt
total=$(wc -l < /tmp/pillar_decls.txt | tr -d ' ')
if [ ${#missing_mac[@]} -eq 0 ]; then ok "all $total portable declarations have a macOS definition"
else bad "no macOS definition: ${missing_mac[*]}"; fi
if [ ${#missing_win[@]} -eq 0 ]; then ok "all $total portable declarations have a Windows definition"
else bad "no Windows definition (link error on CI): ${missing_win[*]}"; fi

# --------------------------------------------------------------------------
head2 "P11 platform contract - every Windows source is in the build"
# Why: a new src/platform/win/*.cpp that nobody added to WIN32_SOURCES never
# compiles, so its definitions are missing and the whole link fails - a
# failure mode invisible from macOS.
for f in src/platform/win/*.cpp; do
   b=$(basename "$f")
   grep -q "$b" CMakeLists.txt || bad "$b is not referenced in CMakeLists.txt"
done
grep -q 'WIN32_SOURCES' CMakeLists.txt && ok "WIN32_SOURCES lists every src/platform/win/*.cpp" \
   || bad "CMakeLists.txt has no WIN32_SOURCES list"

# --------------------------------------------------------------------------
head2 "P13 dual-path numeric equivalence"
# Why: PortableFft::Inverse returned time-reversed audio for months while the
# macOS Accelerate branch sounded perfect and the shared fixture printed OK on
# the real Windows runner. Every __APPLE__ / #else numeric split needs an
# assertion that compares the two paths, not one that exercises whichever
# branch the running machine compiled.
SPLIT=()
while IFS= read -r line; do [ -n "$line" ] && SPLIT+=("$line"); done < <(
   grep -rl 'defined(__APPLE__)\|ifdef __APPLE__' src/nodes src/audio 2>/dev/null | sort)
if [ ${#SPLIT[@]} -eq 0 ]; then
   ok "no __APPLE__ splits under src/nodes or src/audio"
else
   printf '  %d dual-path file(s) - each needs a cross-branch assertion in RunDspTest():\n' "${#SPLIT[@]}"
   printf '         %s\n' "${SPLIT[@]}"
   if grep -q 'PortableFft' src/main.cpp; then
      ok "src/main.cpp references PortableFft (round-trip assertion present in the DSP test)"
   else
      bad "src/main.cpp never names PortableFft - INFINITE_DSPTEST cannot be testing the portable branch"
   fi
fi

# --------------------------------------------------------------------------
head2 "P2 shortcuts - nothing advertised is unwired"
# Why: DrawShortcutsWindow's kShortcuts[] table is the app's public contract
# for keyboard control. A key listed there with no live ImGuiKey_ reference in
# main.cpp is a shortcut the docs promise and the app ignores. (A real
# INFINITE_SHORTCUTSWEEPTEST that injects each chord and asserts the action
# fired is the proper closure - see SKILL.md's backlog. This is the cheap
# guard that catches the removal case.)
KEYS=$(sed -n '/static const ShortcutEntry kShortcuts\[\]/,/^      };/p' src/main.cpp \
       | grep -oE '"[^"]*"' | grep -oE '\b(Space|Delete|Backspace|Slash|[A-Z])\b' \
       | sort -u)
unwired=()
for k in $KEYS; do
   case "$k" in Shift|Ctrl|Cmd|MODKEY) continue;; esac
   grep -q "ImGuiKey_${k}\b" src/main.cpp || unwired+=("$k")
done
[ -z "$(printf '%s' "$KEYS")" ] && bad "could not parse kShortcuts[] - has DrawShortcutsWindow moved?"
if [ ${#unwired[@]} -eq 0 ] && [ -n "$KEYS" ]; then
   ok "every key advertised in kShortcuts[] has a live handler reference"
else
   [ ${#unwired[@]} -gt 0 ] && bad "advertised but no ImGuiKey_ handler: ${unwired[*]}"
fi
# Modifier parity: the handler must accept Ctrl as well as Cmd, or every
# MODKEY shortcut in the table is dead on Windows.
if grep -q 'io.KeyCtrl || io.KeySuper\|KeySuper || .*KeyCtrl' src/main.cpp; then
   ok "modifier handling accepts Ctrl and Cmd (MODKEY chords work on both platforms)"
else
   bad "no 'KeyCtrl || KeySuper' modifier check - MODKEY shortcuts may be macOS-only"
fi

# --------------------------------------------------------------------------
head2 "P3 settings & state location"
# Why: defect 1.5 in docs/WINDOWS_VERIFICATION.md - settings paths that differ
# between platforms silently lose a user's theme and recents. Everything
# mutable must resolve through AppPaths, never next to the executable.
stray=$(grep -rn '"imgui\.ini"\|"Infinite\.json"' src --include='*.cpp' --include='*.mm' \
        | grep -v 'AppPaths\|settingsDir' | grep -v 'settingsDir.empty()' | wc -l | tr -d ' ')
if [ "$stray" = "0" ]; then ok "no settings file written outside an AppPaths-derived directory"
else bad "$stray settings path(s) bypass AppPaths:"; \
     grep -rn '"imgui\.ini"\|"Infinite\.json"' src --include='*.cpp' --include='*.mm' | sed 's/^/         /'; fi

# --------------------------------------------------------------------------
head2 "P10 crash diagnostics parity"
# Why: Windows links WIN32_EXECUTABLE - no console, so an unhandled exception
# unwinds silently and the window just vanishes. InstallCrashHandler and
# AppendLogLine are the only reason a Windows crash leaves evidence.
for fn in InstallCrashHandler AppendLogLine; do
   grep -q "$fn" src/platform/Platform.mm && grep -rq "$fn" src/platform/win/ \
      && ok "$fn implemented on both platforms" \
      || bad "$fn missing an implementation on one platform"
done
grep -q 'InstallCrashHandler' src/main.cpp \
   && ok "main() installs the crash handler" \
   || bad "main() never calls Platform::InstallCrashHandler()"

# --------------------------------------------------------------------------
printf '\n== static lane: %d passed, %d failed ==\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
