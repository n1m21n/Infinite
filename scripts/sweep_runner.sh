#!/usr/bin/env bash
# Shared runner for the .claude/skills/*-sweep drivers.
#
# Every sweep driver is the same shape: build (unless --skip-build), run a
# list of env-var-gated fixtures out of the real compiled binary, and grade
# them. This holds the part that is identical between them so a driver is
# just its fixture list plus `source scripts/sweep_runner.sh`.
#
# A driver sets, before sourcing:
#   SWEEP_NAME    - shown in the header
#   ASSERT        - array of "FIXTURE:frames" that print an OK/FAIL verdict
#                   and are graded pass/fail
#   OBSERVE       - array of "FIXTURE:frames" that print numbers with no
#                   verdict; their logs are pointed at for reading, never
#                   graded (grading them would be inventing a threshold the
#                   fixture itself does not assert)
#   SWEEP_EXTRA   - optional: name of a function run after the fixtures,
#                   for static checks. It must return non-zero on failure.
#
# Frame budgets: a fixture that has never been run through this runner has an
# unverified budget. A too-small budget shows up as "no verdict printed",
# which is reported as a failure rather than a pass - so an unverified budget
# is loud, not silent. Raise it and re-run rather than lowering the bar.
#
# Exit code: 0 if the build succeeded and every ASSERT fixture passed.

set -uo pipefail
export INFINITE_NO_UPDATE_CHECK=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
for arg in "${SWEEP_ARGS[@]:-}"; do
  case "$arg" in
    "") ;;
    --skip-build) SKIP_BUILD=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

# Same marker set as run-infinite-hygiene's driver: the harness's failure
# branch is "FAIL" in most places and a bare word ("SUSPECT", "MISMATCH") in
# others, and a fixture that dies before printing anything must not pass by
# printing nothing.
FAIL_MARK='FAIL|BUG$|MISMATCH|SUSPECT|UNEXPECTED|DID NOT MOVE|TONE MISSING'
PASS_MARK=' OK$|OK$|PASS$|SKIP$|CLEAN$'

PASS=0
FAIL=0
FAILED_NAMES=()

step() { printf '\n== %s ==\n' "$1"; }

printf '=== %s ===\n' "${SWEEP_NAME:-sweep}"

step "Build"
if [ "$SKIP_BUILD" -eq 1 ]; then
  echo "skipped (--skip-build)"
elif [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee /tmp/infinite_build.log | tail -5; then
    echo "BUILD FAILED - see /tmp/infinite_build.log"; exit 1
  fi
else
  echo "no existing build/, configuring fresh"
  cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tee /tmp/infinite_build.log | tail -5
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee -a /tmp/infinite_build.log | tail -5; then
    echo "BUILD FAILED - see /tmp/infinite_build.log"; exit 1
  fi
fi
[ -x "$BIN" ] || { echo "binary not found at $BIN"; exit 1; }

run_one() {
  local spec="$1" graded="$2"
  # "NAME:frames" gates on INFINITE_NAME; "@FULL_VAR:frames" gates on FULL_VAR
  # verbatim, for the handful of fixtures that predate the INFINITE_ prefix
  # (IMAGERESYNTH_SELFTEST).
  local key="${spec%:*}" frames="${spec##*:}"
  local name="${key//@/}"; name="${name//,/+}"
  # A key may name more than one gate, comma-separated, for fixtures that only
  # mean anything on top of another fixture's graph ("CACHETEST,SHOWCASE" -
  # the cache observer over a populated canvas rather than an empty one).
  local envs=() part
  local IFS=,
  for part in $key; do
    # a gate may carry a value ("RECTEARDOWNTEST=quit"); bare gates get =1
    case "$part" in
      @*=*) envs+=("${part#@}") ;;
      @*)   envs+=("${part#@}=1") ;;
      *=*)  envs+=("INFINITE_${part}") ;;
      *)    envs+=("INFINITE_${part}=1") ;;
    esac
  done
  unset IFS
  local out="/tmp/infinite_test_${name//[=+]/_}.log"
  env "${envs[@]}" INFINITE_EXITAFTER="$frames" "$BIN" >"$out" 2>&1
  local rc=$?
  # Some fixtures return their verdict as the exit code as well as a printed
  # line (DSPTEST, RESONATORTEST, ...). A non-zero exit that also printed a
  # failure line is an honest [FAIL], not a crash - only a non-zero exit with
  # no verdict in the log is a crash.
  if [ $rc -ne 0 ] && ! grep -qE "$FAIL_MARK" "$out"; then
    echo "  [CRASH]  $name - exited $rc, see $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name (crash)"); return
  fi
  if [ "$graded" = "observe" ]; then
    echo "  [read]   $name - $out"
    return
  fi
  if grep -qE "$FAIL_MARK" "$out"; then
    echo "  [FAIL]   $name - see $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
  elif grep -qE "$PASS_MARK" "$out"; then
    echo "  [pass]   $name"
    PASS=$((PASS+1))
  else
    echo "  [NO VERDICT] $name - printed no OK/FAIL line; frame budget too low, or it never ran. See $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name (no verdict)")
  fi
}

if [ "${#ASSERT[@]}" -gt 0 ]; then
  step "Asserted fixtures (${#ASSERT[@]})"
  for spec in "${ASSERT[@]}"; do run_one "$spec" assert; done
fi

if [ "${#OBSERVE[@]}" -gt 0 ]; then
  step "Observation fixtures (${#OBSERVE[@]}) - read these logs, they assert nothing"
  for spec in "${OBSERVE[@]}"; do run_one "$spec" observe; done
fi

if [ -n "${SWEEP_EXTRA:-}" ]; then
  step "Static checks"
  if "$SWEEP_EXTRA"; then
    echo "  [pass]   static checks"
    PASS=$((PASS+1))
  else
    echo "  [FAIL]   static checks"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("static checks")
  fi
fi

step "Result"
echo "passed: $PASS   failed: $FAIL"
if [ "$FAIL" -gt 0 ]; then
  printf 'failing: %s\n' "${FAILED_NAMES[*]}"
  exit 1
fi
exit 0
