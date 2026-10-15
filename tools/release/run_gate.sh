#!/usr/bin/env bash
# Release gate runner: P1 (static, no app) then P2 (serial app-launching battery), one pass/fail table per category.
#
# Wraps existing tools only; it builds nothing and compiles nothing. Protocol: docs/release/PROTOCOL.md (section 6, 7).
# Steps are the STEPS table below: id | category | phase | gating | slow | description | command.
#
# Usage:
#   tools/release/run_gate.sh --list                      show every step (id, phase, category, cost, command); runs nothing
#   tools/release/run_gate.sh --phase P1                  static checks (seconds to minutes), the default
#   tools/release/run_gate.sh --phase P2 [--bin PATH]     the serial app battery (slow; the candidate binary must be built)
#   tools/release/run_gate.sh --phase all                 P1 then P2
#   tools/release/run_gate.sh --cat D,E --only X1,X2      filters (combine with --phase and --list)
#   tools/release/run_gate.sh --out DIR                   run folder (default docs/release/runs/<timestamp>, ignored by git)
#   tools/release/run_gate.sh --no-slow                   skip steps marked slow (the full driver and the census)
#   tools/release/run_gate.sh --force                     run P2 even if a build or an Infinite process is running
#   tools/release/run_gate.sh --self-test                 check the table parses and the filters work (runs no step)
#
# Output: <run>/results.tsv (id, category, phase, status, seconds, log), <run>/logs/<id>.log, and a table per category.
# Status: PASS, FAIL (non-zero exit), ERROR (exit 2 or tool missing: the tool could not judge), SKIP (filtered by --no-slow),
# DRY (--list). Steps with gating=info are shown but never fail the run.
# Exit: 0 every gating step passed, 1 a gating step failed or errored, 2 usage or machine-state error.
#
# P2 rules (also in the protocol): one build at a time, app-launching tests serial. This runner refuses to start P2 while
# `cmake --build|make|clang` or another Infinite app is running (override: --force). The sweep drivers in
# .claude/skills/*-sweep use the hard-coded binary build/Infinite.app (scripts/sweep_runner.sh); the hygiene driver,
# census, render goldens, determinism, smoke and self-update use --bin. Keep the candidate binary in build/ or pass --bin
# and know that the sweeps still look at build/.

set -uo pipefail
export INFINITE_NO_UPDATE_CHECK=1

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

BIN="$ROOT/build/Infinite.app/Contents/MacOS/Infinite"
PHASE="P1"; LIST=0; ONLY=""; CATS=""; OUT=""; NOSLOW=0; FORCE=0; SELFTEST=0

SW=".claude/skills"
# id|category|phase|gating|slow|description|command   ($BIN and $OUT are expanded when the step runs)
STEPS=(
  # ---- P1: static, no build, no app launch ----
  "ratchet|I|P1|yes|no|design ratchet: no literal-colour or icon count went up|python3 tools/design/ratchet.py"
  "lint-icons|I|P1|yes|no|icon lint|python3 tools/design/lint_icons.py"
  "motion-lint|I|P1|yes|no|motion lint|python3 tools/design/motion_lint.py"
  "contrast|I|P1|yes|no|text contrast pairs in both themes|python3 tools/design/contrast.py"
  "i18n-lint|I|P1|yes|no|unwrapped UI strings|python3 tools/i18n/lint.py"
  "shortcuts-static|I|P1|yes|no|shortcuts match the help window|python3 $SW/shortcuts-sweep/check.py --quiet"
  "cable-static|I|P1|yes|no|cable logic static check|python3 $SW/cable-logic-sweep/check.py"
  "compositing-static|E|P1|yes|no|compositing pipeline static check|python3 $SW/compositing-pipeline-sweep/check.py"
  "output-static|E|P1|yes|no|output/projection static check|python3 $SW/output-projection-sweep/check.py"
  "param-truth|D|P1|info|no|control range vs DSP agreement (needs reading: report only)|python3 scripts/audit_param_truth.py"
  "parity-static|J|P1|yes|no|every Platform:: function has three sides|bash $SW/pillar-parity-audit/static-checks.sh"
  "data-ledger|G|P1|yes|no|no dataset unused or stale|python3 tools/re-survey/data_ledger.py lint"
  "brand-lint|L|P1|yes|no|brand colours on brand surfaces|python3 tools/brand/brand_lint.py"
  "version-gate|L|P1|yes|no|version above every published one (needs gh/network; ERROR if it cannot read)|python3 scripts/release_version_gate.py"
  "findings-selftest|L|P1|yes|no|the findings tracker's own self-test|python3 tools/release/findings.py --self-test"
  # ---- P2: serial, launches the app ----
  "driver-full|*|P2|yes|yes|hygiene driver, every group (reuses the build; known failures from the baselines)|INFINITE_BIN=\"\$BIN\" bash $SW/run-infinite-hygiene/driver.sh --full --skip-build"
  "census|A|P2|yes|yes|every node alone: validate, render, black, NaN, size, determinism vs baseline|python3 scripts/node_census.py --bin \"\$BIN\" --out \"\$OUT/census\" --baseline tests/headless/census_baseline.json"
  "sweep-audio-node|D|P2|yes|no|audio node sweep|bash $SW/audio-node-sweep/driver.sh --skip-build"
  "sweep-audio-pipeline|D|P2|yes|no|audio pipeline sweep|bash $SW/audio-pipeline-sweep/driver.sh --skip-build"
  "sweep-modulation|D|P2|yes|no|modulation sweep|bash $SW/modulation-sweep/driver.sh --skip-build"
  "sweep-data-accuracy|B|P2|yes|no|data corruption along chains|bash $SW/data-accuracy-sweep/driver.sh --skip-build"
  "sweep-compositing|E|P2|yes|no|compositing pipeline sweep|bash $SW/compositing-pipeline-sweep/driver.sh --skip-build"
  "sweep-output|E|P2|yes|no|output and projection sweep|bash $SW/output-projection-sweep/driver.sh --skip-build"
  "sweep-av-sync|E|P2|yes|no|audio/video sync in exported movies|bash $SW/av-sync-sweep/driver.sh --skip-build"
  "sweep-geometry|F|P2|yes|no|geometry transform sweep|bash $SW/geometry-transform-sweep/driver.sh --skip-build"
  "sweep-render|F|P2|yes|no|render pipeline sweep|bash $SW/render-pipeline-sweep/driver.sh --skip-build"
  "sweep-rate|H|P2|yes|no|sample/frame rate analysis|bash $SW/rate-analysis-sweep/driver.sh --skip-build"
  "sweep-node-ui|I|P2|yes|no|node UI sweep|bash $SW/node-ui-sweep/driver.sh --skip-build"
  "sweep-panels|I|P2|yes|no|panels sweep|bash $SW/panels-sweep/driver.sh --skip-build"
  "render-goldens|E|P2|yes|no|image and audio goldens in tests/render|python3 tools/render-check.py --binary \"\$BIN\""
  "determinism|E|P2|yes|no|two runs identical on the video patch (by-design list excepted)|python3 tools/determinism-check.py tests/headless/patch_video.inf --binary \"\$BIN\""
  "headless-smoke|C|P2|yes|no|headless CLI contract: exit codes, JSON, files|bash scripts/headless_smoke.sh \"\$BIN\""
  "selfupdate|K|P2|yes|no|in-place update on a throwaway copy|bash scripts/selfupdate_swap_test.sh \"\$APP\""
  "scoreboard|G|P2|info|no|scoreboard gate over the QUALITY rows of this run's logs (missing metrics are normal in a partial run)|python3 tools/quality/quality.py check \"\$OUT\"/logs/*.log"
)

usage() { sed -n '2,/^set -uo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --list|--dry-run) LIST=1; shift ;;
    --phase) PHASE="$2"; shift 2 ;;
    --only) ONLY=",$2,"; shift 2 ;;
    --cat) CATS=",$2,"; shift 2 ;;
    --out) OUT="$2"; shift 2 ;;
    --bin) BIN="$2"; shift 2 ;;
    --no-slow) NOSLOW=1; shift ;;
    --force) FORCE=1; shift ;;
    --self-test) SELFTEST=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done
case "$PHASE" in P1|P2|all) ;; *) echo "bad --phase $PHASE (P1, P2, all)" >&2; exit 2 ;; esac

selected() {  # selected <id> <cat> <phase> -> 0 when the filters keep the step
  local id="$1" cat="$2" ph="$3"
  [[ "$PHASE" != "all" && "$ph" != "$PHASE" ]] && return 1
  [[ -n "$ONLY" && "$ONLY" != *",$id,"* ]] && return 1
  [[ -n "$CATS" && "$CATS" != *",$cat,"* ]] && return 1
  return 0
}

APP="$(cd "$(dirname "$BIN")/../.." 2>/dev/null && pwd)"

if [[ $SELFTEST -eq 1 ]]; then
  bad=0; ids=" "
  for row in "${STEPS[@]}"; do
    IFS='|' read -r id cat ph gating slow desc cmd <<< "$row"
    [[ -z "$cmd" || -z "$desc" ]] && { echo "bad row: $row"; bad=1; }
    [[ "$ids" == *" $id "* ]] && { echo "duplicate id $id"; bad=1; }
    ids+="$id "
    [[ "$ph" == P1 || "$ph" == P2 ]] || { echo "bad phase in $id"; bad=1; }
    [[ "$gating" == yes || "$gating" == info ]] || { echo "bad gating in $id"; bad=1; }
    # the script a step names must exist (first path-like token after the interpreter)
    for tok in $cmd; do
      case "$tok" in */*.py|*/*.sh) [[ -e "${tok//\$SW/$SW}" ]] || { echo "$id: missing $tok"; bad=1; } ; break ;; esac
    done
  done
  PHASE=P1; n1=0; for row in "${STEPS[@]}"; do IFS='|' read -r id cat ph _ <<< "$row"; selected "$id" "$cat" "$ph" && n1=$((n1+1)); done
  [[ $n1 -gt 0 ]] || { echo "P1 filter selects nothing"; bad=1; }
  [[ $bad -eq 0 ]] && echo "run_gate self-test OK (${#STEPS[@]} steps)" || echo "run_gate self-test FAIL"
  exit $bad
fi

if [[ $LIST -eq 1 ]]; then
  printf '%-20s %-3s %-5s %-6s %-5s %s\n' ID CAT PHASE GATING SLOW "DESCRIPTION  ->  COMMAND"
  for row in "${STEPS[@]}"; do
    IFS='|' read -r id cat ph gating slow desc cmd <<< "$row"
    selected "$id" "$cat" "$ph" || continue
    [[ $NOSLOW -eq 1 && "$slow" == yes ]] && continue
    printf '%-20s %-3s %-5s %-6s %-5s %s\n%-47s-> %s\n' "$id" "$cat" "$ph" "$gating" "$slow" "$desc" "" "$cmd"
  done
  echo "(list only: nothing was run)"
  exit 0
fi

# ---- machine-state guards (P2 launches the app and needs the binary) ----
needs_p2=0
for row in "${STEPS[@]}"; do IFS='|' read -r id cat ph _ <<< "$row"; [[ "$ph" == P2 ]] && selected "$id" "$cat" "$ph" && needs_p2=1; done
if [[ $needs_p2 -eq 1 ]]; then
  if [[ ! -x "$BIN" ]]; then echo "binary not found or not executable: $BIN (this runner never builds; build first, one build at a time)" >&2; exit 2; fi
  if [[ $FORCE -eq 0 ]]; then
    busy="$(pgrep -fl 'cmake --build|make |clang' | grep -v "$$" || true)"
    if [[ -n "$busy" ]]; then echo "a build looks to be running; P2 would race it. Wait, or use --force:" >&2; echo "$busy" >&2; exit 2; fi
    running="$(pgrep -fl 'Infinite.app/Contents/MacOS/Infinite' || true)"
    if [[ -n "$running" ]]; then echo "an Infinite app is already running; P2 tests are serial. Quit it, or use --force:" >&2; echo "$running" >&2; exit 2; fi
  fi
fi

[[ -z "$OUT" ]] && OUT="$ROOT/docs/release/runs/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUT/logs"
RESULTS="$OUT/results.tsv"
printf 'id\tcategory\tphase\tgating\tstatus\tseconds\tlog\n' > "$RESULTS"

{
  echo "Release gate run $(date '+%Y-%m-%d %H:%M:%S')"
  echo "commit: $(git rev-parse --short HEAD 2>/dev/null) on $(git branch --show-current 2>/dev/null); dirty files: $(git status --short 2>/dev/null | wc -l | tr -d ' ')"
  echo "phase: $PHASE   binary: $BIN"
  echo "run folder: $OUT"
} | tee "$OUT/header.txt"

if command -v perl >/dev/null 2>&1; then
  run_limited() { local secs="$1"; shift; perl -e 'alarm shift; exec @ARGV' "$secs" "$@"; }
else
  run_limited() { shift; "$@"; }
fi

for row in "${STEPS[@]}"; do
  IFS='|' read -r id cat ph gating slow desc cmd <<< "$row"
  selected "$id" "$cat" "$ph" || continue
  log="$OUT/logs/$id.log"
  if [[ $NOSLOW -eq 1 && "$slow" == yes ]]; then
    printf '%s\t%s\t%s\t%s\tSKIP\t0\t-\n' "$id" "$cat" "$ph" "$gating" >> "$RESULTS"
    printf '  %-4s %-20s (--no-slow)\n' SKIP "$id"
    continue
  fi
  limit=900; [[ "$slow" == yes ]] && limit=7200; [[ "$ph" == P2 && "$slow" == no ]] && limit=3600
  start=$SECONDS
  export BIN OUT APP
  ( run_limited "$limit" bash -c "$cmd" ) > "$log" 2>&1
  rc=$?
  secs=$((SECONDS - start))
  if [[ $rc -eq 0 ]]; then st=PASS
  elif [[ $rc -eq 2 || $rc -eq 126 || $rc -eq 127 || $rc -eq 142 ]]; then st=ERROR
  else st=FAIL; fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$cat" "$ph" "$gating" "$st" "$secs" "logs/$id.log" >> "$RESULTS"
  printf '  %-5s %-20s %4ss  %s\n' "$st" "$id" "$secs" "$desc"
done

# ---- one table per category ----
echo
python3 - "$RESULTS" <<'PY'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1]), delimiter="\t"))
cats = sorted({r["category"] for r in rows}, key=lambda c: ("Z" if c == "*" else c))
print("%-4s %5s %5s %6s %5s  %s" % ("Cat", "pass", "fail", "error", "skip", "not passing (gating marked *)"))
for c in cats:
    rs = [r for r in rows if r["category"] == c]
    n = lambda s: sum(1 for r in rs if r["status"] == s)
    bad = ["%s%s(%s)" % (r["id"], "*" if r["gating"] == "yes" else "", r["status"]) for r in rs if r["status"] in ("FAIL", "ERROR")]
    print("%-4s %5d %5d %6d %5d  %s" % (c, n("PASS"), n("FAIL"), n("ERROR"), n("SKIP"), ", ".join(bad)))
gating_bad = [r for r in rows if r["gating"] == "yes" and r["status"] in ("FAIL", "ERROR")]
print()
print("gating steps not passing: %d   (logs under logs/<id>.log; categories without a row here were not run)" % len(gating_bad))
sys.exit(1 if gating_bad else 0)
PY
exit $?
