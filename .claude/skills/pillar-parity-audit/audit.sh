#!/usr/bin/env bash
# Pillar-parity audit for Infinite.
#
# Answers one question the hygiene gate does not: for each of the app's
# functional pillars, what evidence exists that it works, ON WHICH PLATFORM,
# and produced by a machine rather than by a claim.
#
# Two lanes:
#   static   - source-tree facts checkable on macOS (static-checks.sh)
#   dynamic  - the INFINITE_* fixtures, grouped by pillar instead of by area
#
# A fixture marked `!` in pillars.tsv is already run by
# run-infinite-hygiene/driver.sh. `--new-only` skips those, so this script
# adds coverage rather than re-running the gate.
#
# Usage:
#   audit.sh                # static lane + every pillar's fixtures
#   audit.sh --new-only     # skip what driver.sh already gates
#   audit.sh --static-only  # source checks only, no build, no GL
#   audit.sh --matrix       # print the pillar/grade matrix and exit
#   audit.sh --pillar P4    # one pillar
#
# Exit 0 if everything that CAN be checked passed.

set -uo pipefail
export INFINITE_NO_UPDATE_CHECK=1

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
cd "$ROOT"
BIN="build/Infinite.app/Contents/MacOS/Infinite"
MANIFEST="$HERE/pillars.tsv"

NEW_ONLY=0; STATIC_ONLY=0; MATRIX_ONLY=0; ONLY_PILLAR=""
while [ $# -gt 0 ]; do
   case "$1" in
      --new-only) NEW_ONLY=1 ;;
      --static-only) STATIC_ONLY=1 ;;
      --matrix) MATRIX_ONLY=1 ;;
      --pillar) ONLY_PILLAR="${2:?--pillar needs an id, e.g. P4}"; shift ;;
      *) echo "unknown arg: $1" >&2; exit 2 ;;
   esac
   shift
done

GRADE_TEXT() {
   case "$1" in
      A) echo "both platforms, by machine" ;;
      B) echo "macOS run + Windows asserted statically" ;;
      C) echo "macOS only; Windows on review + Parallels" ;;
      D) echo "NO EXECUTION ANYWHERE - static guard / manual only" ;;
      *) echo "?" ;;
   esac
}

if [ "$MATRIX_ONLY" = "1" ]; then
   printf '\n%-4s %-46s %-5s %s\n' "ID" "PILLAR" "GRADE" "WHAT THAT MEANS"
   printf '%s\n' "-------------------------------------------------------------------------------------------------"
   while IFS='|' read -r id name mac win grade; do
      case "$id" in ''|\#*) continue;; esac
      printf '%-4s %-46s %-5s %s\n' "$id" "$name" "$grade" "$(GRADE_TEXT "$grade")"
   done < "$MANIFEST"
   echo
   exit 0
fi

# --------------------------------------------------------------------- static
printf '\n########## STATIC LANE ##########\n'
bash "$HERE/static-checks.sh"
STATIC_RC=$?
[ "$STATIC_ONLY" = "1" ] && exit "$STATIC_RC"

# -------------------------------------------------------------------- dynamic
if [ ! -x "$BIN" ]; then
   echo
   echo "No build at $BIN - run 'cmake --build build -j8' first (or use --static-only)."
   exit 1
fi

PASS=0; FAIL=0; SKIP=0
FAILED=()
# Kept in sync with run-infinite-hygiene/driver.sh: the harness names its
# failure branches inconsistently (FAIL, "... - BUG", and 56 places that print
# a bare SUSPECT/MISMATCH), and its pass branches end in OK, PASS or SKIP.
# Matching only FAIL|BUG let a failing fixture read as green in both gates.
FAIL_MARK='FAIL|BUG$|MISMATCH|SUSPECT|DID NOT MOVE|TONE MISSING'
PASS_MARK='OK$|OK[[:space:]]*$|PASS$|SKIP$|CLEAN$'

run_fixture() {
   local name="$1"
   local frames="$2"
   local log="/tmp/infinite_pillar_${name}.log"
   local rc=0
   env "INFINITE_${name}=1" INFINITE_EXITAFTER="$frames" "$BIN" > "$log" 2>&1
   rc=$?
   # Headless, exit-code-gated fixtures (DSPTEST, PERFMATRIXTEST) report through
   # $?; the GL ones always return 0 and speak only in printf. A non-zero exit
   # with no verdict line at all is the crash case.
   if [ "$rc" -ne 0 ] && ! grep -qE "$PASS_MARK" "$log"; then
      printf '    [CRASH]  %-26s (exit %d)  %s\n' "$name" "$rc" "$log"
      tail -3 "$log" | sed 's/^/            /'
      FAIL=$((FAIL+1)); FAILED+=("$name"); return
   fi
   if grep -qE "$FAIL_MARK" "$log"; then
      printf '    [FAIL]  %-26s %s\n' "$name" "$log"
      grep -E "$FAIL_MARK" "$log" | head -3 | sed 's/^/            /'
      FAIL=$((FAIL+1)); FAILED+=("$name")
   elif grep -qE "$PASS_MARK" "$log"; then
      printf '    [pass]  %-26s\n' "$name"
      PASS=$((PASS+1))
   else
      printf '    [no verdict] %-21s %s  (fixture printed no verdict line at %s frames)\n' \
             "$name" "$log" "$frames"
      SKIP=$((SKIP+1))
   fi
}

printf '\n########## DYNAMIC LANE ##########\n'
while IFS='|' read -r id name mac win grade; do
   case "$id" in ''|\#*) continue;; esac
   [ -n "$ONLY_PILLAR" ] && [ "$id" != "$ONLY_PILLAR" ] && continue

   printf '\n== %s  %s  [grade %s: %s]\n' "$id" "$name" "$grade" "$(GRADE_TEXT "$grade")"
   printf '   windows lane: %s\n' "${win:--}"

   if [ "$mac" = "-" ]; then
      printf '    (no macOS fixture - this pillar has no dynamic evidence; see SKILL.md backlog)\n'
      continue
   fi

   IFS=',' read -ra items <<< "$mac"
   for item in "${items[@]}"; do
      fx="${item%%:*}"; fr="${item##*:}"
      gated=0
      case "$fr" in *!) fr="${fr%!}"; gated=1;; esac
      if [ "$NEW_ONLY" = "1" ] && [ "$gated" = "1" ]; then continue; fi
      run_fixture "$fx" "$fr"
   done
done < "$MANIFEST"

printf '\n########## SUMMARY ##########\n'
printf 'static lane: %s\n' "$([ "$STATIC_RC" -eq 0 ] && echo 'all passed' || echo 'FAILURES - see above')"
printf 'dynamic lane: %d passed, %d failed, %d produced no verdict\n' "$PASS" "$FAIL" "$SKIP"
[ ${#FAILED[@]} -gt 0 ] && printf 'failed: %s\n' "${FAILED[*]}"
printf '\nPillars with NO executable evidence on any platform (grade D):\n'
awk -F'|' '/^P/ && $5=="D" {printf "  %-4s %s\n", $1, $2}' "$MANIFEST"
printf '\nWindows has machine evidence for only the pillars graded A or B.\n'
printf 'Everything graded C rests on macOS execution plus a human Parallels pass\n'
printf '(docs/WINDOWS_VERIFICATION.md Part 2).\n\n'

[ "$FAIL" -eq 0 ] && [ "$STATIC_RC" -eq 0 ]
