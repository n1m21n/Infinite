#!/usr/bin/env bash
# Runs every plugin validator on the built bundles and prints one summary table
# (docs/plans/vst-au/README.md, M-1 / M-2 / M-3). Builds nothing.
#
#   tools/plugin/validate.sh [--build DIR] [--strictness N] [--skip-pluginval] [--skip-auval]
#
#   --build        plugin build dir (default build-plugin)
#   --strictness   pluginval strictness (default 5; 10 for a release candidate)
#   PLUGINVAL      pluginval binary (default ~/infinite-plugin-tools/pluginval/pluginval.app/Contents/MacOS/pluginval,
#                  else `pluginval` on PATH)
#   INFINITE_PLUGIN_ENGINE  engine binary (default build-c1's Infinite.app on macOS)
#
# auval needs the AUs installed: they are copied into ~/Library/Audio/Plug-Ins/Components for the
# run and removed again on exit, also on failure. Logs go to $LOGDIR (default /tmp/infinite-validate).
set -u
cd "$(dirname "$0")/../.."

BUILD=build-plugin
STRICT=5
DO_PV=1
DO_AUVAL=1
while [ $# -gt 0 ]; do
   case "$1" in
      --build) BUILD="$2"; shift 2 ;;
      --strictness) STRICT="$2"; shift 2 ;;
      --skip-pluginval) DO_PV=0; shift ;;
      --skip-auval) DO_AUVAL=0; shift ;;
      *) echo "unknown option $1" >&2; exit 2 ;;
   esac
done

LOGDIR="${LOGDIR:-/tmp/infinite-validate}"
mkdir -p "$LOGDIR"
OS="$(uname -s)"

if [ -z "${INFINITE_PLUGIN_ENGINE:-}" ]; then
   case "$OS" in
      Darwin) export INFINITE_PLUGIN_ENGINE="$PWD/build-c1/Infinite.app/Contents/MacOS/Infinite" ;;
      *) export INFINITE_PLUGIN_ENGINE="$PWD/build-c1/Infinite" ;;
   esac
fi
[ -x "$INFINITE_PLUGIN_ENGINE" ] || { echo "engine not found: $INFINITE_PLUGIN_ENGINE" >&2; exit 2; }

PV="${PLUGINVAL:-$HOME/infinite-plugin-tools/pluginval/pluginval.app/Contents/MacOS/pluginval}"
[ -x "$PV" ] || PV="$(command -v pluginval || true)"

VALIDATOR="$BUILD/bin/Release/validator"
[ -x "$VALIDATOR" ] || VALIDATOR="$BUILD/bin/validator"

ROWS=()
FAIL=0
row() { ROWS+=("$(printf '| %-22s | %-20s | %-6s | %s |' "$1" "$2" "$3" "$4")"); [ "$3" = "PASS" ] || [ "$3" = "SKIP" ] || FAIL=1; }

engines_left() { pgrep -f -- '--plugin-engine' | wc -l | tr -d ' '; }

for B in "Infinite Instrument" "Infinite FX"; do
   VST3="$BUILD/VST3/Release/$B.vst3"
   [ -d "$VST3" ] || VST3="$BUILD/VST3/$B.vst3"
   TAG="${B// /_}"
   if [ ! -d "$VST3" ]; then
      row "M-1 validator" "$B" FAIL "bundle missing"
      continue
   fi

   # M-1: Steinberg validator
   LOG="$LOGDIR/validator_$TAG.txt"
   "$VALIDATOR" "$VST3" > "$LOG" 2>&1
   RES="$(grep -E 'Result: [0-9]+ tests passed, [0-9]+ tests failed' "$LOG" | tail -1)"
   if [ -n "$RES" ] && echo "$RES" | grep -q ' 0 tests failed'; then
      row "M-1 validator" "$B" PASS "${RES#*Result: }"
   else
      row "M-1 validator" "$B" FAIL "${RES:-no result} ($LOG)"
   fi

   # M-2: pluginval
   if [ $DO_PV = 1 ]; then
      if [ -z "$PV" ]; then
         row "M-2 pluginval $STRICT" "$B" FAIL "pluginval not found (set PLUGINVAL)"
      else
         LOG="$LOGDIR/pluginval_$TAG.txt"
         "$PV" --strictness-level "$STRICT" --skip-gui-tests --timeout-ms 120000 --validate "$VST3" > "$LOG" 2>&1
         RC=$?
         if [ $RC = 0 ] && grep -q "SUCCESS" "$LOG"; then
            row "M-2 pluginval $STRICT" "$B" PASS "exit 0"
         else
            row "M-2 pluginval $STRICT" "$B" FAIL "exit $RC ($LOG)"
         fi
      fi
   fi
done

# M-3: auval (macOS)
if [ "$OS" = Darwin ] && [ $DO_AUVAL = 1 ]; then
   COMP="$HOME/Library/Audio/Plug-Ins/Components"
   mkdir -p "$COMP"
   INSTALLED=()
   cleanup() {
      for c in "${INSTALLED[@]:-}"; do [ -n "$c" ] && rm -rf "$COMP/$c"; done
      killall -9 AudioComponentRegistrar 2>/dev/null || true
   }
   trap cleanup EXIT
   for c in "Infinite Instrument.component" "Infinite FX.component"; do
      if [ -d "$BUILD/AU/$c" ]; then
         rm -rf "$COMP/$c"
         cp -R "$BUILD/AU/$c" "$COMP/"
         INSTALLED+=("$c")
      fi
   done
   killall -9 AudioComponentRegistrar 2>/dev/null || true
   sleep 2
   for spec in "aumu InIn Infinite Instrument" "aumf InFx Infinite FX"; do
      set -- $spec
      TYPE=$1; SUB=$2; shift 2; B="$*"
      LOG="$LOGDIR/auval_${B// /_}.txt"
      auval -strict -v "$TYPE" "$SUB" Infn > "$LOG" 2>&1
      RC=$?
      WARN=$(grep -c '^WARNING' "$LOG")
      if [ $RC = 0 ] && grep -q "AU VALIDATION SUCCEEDED" "$LOG"; then
         row "M-3 auval -strict" "$B" PASS "$WARN warning(s)"
      else
         row "M-3 auval -strict" "$B" FAIL "exit $RC ($LOG)"
      fi
   done
fi

LEFT=0
for _ in $(seq 1 50); do LEFT=$(engines_left); [ "$LEFT" = 0 ] && break; sleep 0.1; done
if [ "$LEFT" = 0 ]; then row "no engine left" "-" PASS "0 running"; else row "no engine left" "-" FAIL "$LEFT running"; fi

echo
printf '| %-22s | %-20s | %-6s | %s |\n' "check" "build" "result" "detail"
printf '|%s|%s|%s|%s|\n' "------------------------" "----------------------" "--------" "--------"
for r in "${ROWS[@]}"; do echo "$r"; done
echo
if [ $FAIL = 0 ]; then echo "VALIDATE: PASS"; else echo "VALIDATE: FAIL (logs in $LOGDIR)"; fi
exit $FAIL
