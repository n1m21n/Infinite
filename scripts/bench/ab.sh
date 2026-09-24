#!/usr/bin/env bash
# Interleaved A/B benchmark: runs a base build and a branch build of Infinite
# alternately (base, branch, base, branch, ...) on the same fixture, then
# prints a median table with change %. Interleaving spreads machine drift
# (thermals, swap, background load) over both sides instead of one.
#
# Usage: scripts/bench/ab.sh <baseApp> <branchApp> <rounds> -- ENV=VAL [ENV=VAL ...]
#   <baseApp>/<branchApp>: path to Infinite.app (or its Contents/MacOS/Infinite)
#   env: the fixture's INFINITE_BENCH_* variables, e.g.
#     scripts/bench/ab.sh ../infinte-base/build/Infinite.app build/Infinite.app 2 -- \
#        INFINITE_BENCH_B6NODES=300 INFINITE_BENCH_B6MODE=all INFINITE_BENCH_GPUTIMERS=0
#
# Knobs: EXITAFTER (default 650, frames), AB_TIMEOUT (default 240 s watchdog),
# AB_OUT (directory for the raw BENCH_JSON lines, default a mktemp dir).
# Every launch passes -ApplePersistenceIgnoreState YES so a crashed run can't
# leave macOS's reopen-windows dialog hanging the next one.
set -uo pipefail

if [[ $# -lt 4 || "$4" != "--" ]]; then
   sed -n '2,16p' "$0" >&2
   exit 1
fi

resolve() {
   local p="$1"
   if [[ -d "$p" && -x "$p/Contents/MacOS/Infinite" ]]; then p="$p/Contents/MacOS/Infinite"; fi
   if [[ ! -x "$p" ]]; then echo "error: $1 is not an Infinite binary or .app" >&2; exit 1; fi
   echo "$p"
}
BASE="$(resolve "$1")"; BRANCH="$(resolve "$2")"; ROUNDS="$3"; shift 4
ENVS=("$@")
EXITAFTER="${EXITAFTER:-650}"
AB_TIMEOUT="${AB_TIMEOUT:-240}"
OUT="${AB_OUT:-$(mktemp -d)}"
mkdir -p "$OUT"

if pgrep -f sync_brain > /dev/null; then
   echo "warning: sync_brain is running - numbers will be noisy" >&2
fi
echo "ab.sh: $ROUNDS rounds, env: ${ENVS[*]}"
echo "swap: $(sysctl -n vm.swapusage 2>/dev/null)"

run_one() { # run_one <label> <binary> <round>
   local label="$1" bin="$2" round="$3" log="$OUT/$1-$3.log"
   env "${ENVS[@]}" INFINITE_EXITAFTER="$EXITAFTER" "$bin" -ApplePersistenceIgnoreState YES > "$log" 2>&1 &
   local pid=$!
   ( sleep "$AB_TIMEOUT"; kill -9 "$pid" 2>/dev/null && echo "watchdog: killed $label round $round" >&2 ) > /dev/null 2>&1 &
   local wd=$!
   wait "$pid" 2>/dev/null
   { kill "$wd" && wait "$wd"; } 2>/dev/null
   if grep -q '^BENCH_JSON ' "$log"; then
      grep '^BENCH_JSON ' "$log" | sed 's/^BENCH_JSON //' > "$OUT/$label-$round.json"
      echo "  $label round $round: ok"
   else
      echo "  $label round $round: FAIL, no BENCH_JSON (see $log)" >&2
   fi
}

for ((r = 1; r <= ROUNDS; r++)); do
   run_one base "$BASE" "$r"
   run_one branch "$BRANCH" "$r"
done

python3 - "$OUT" <<'EOF'
import json, sys, glob, os, statistics
out = sys.argv[1]
def load(side):
    runs = []
    for f in sorted(glob.glob(os.path.join(out, side + "-*.json"))):
        for line in open(f):
            line = line.strip()
            if line:
                runs.append(json.loads(line))
    return runs
def flat(d, prefix=""):
    r = {}
    for k, v in d.items():
        key = prefix + k
        if isinstance(v, dict):
            r.update(flat(v, key + "."))
        elif isinstance(v, (int, float)) and not isinstance(v, bool):
            r[key] = float(v)
    return r
base, branch = load("base"), load("branch")
if not base or not branch:
    print("not enough results to compare"); sys.exit(1)
print("variants: base=%s | branch=%s" % (sorted({r.get("variant") for r in base}), sorted({r.get("variant") for r in branch})))
fb = [flat(r) for r in base]; fr = [flat(r) for r in branch]
keys = [k for k in fb[0] if all(k in x for x in fb + fr)]
skip = ("frames", "nodes")
print("%-44s %12s %12s %9s" % ("metric (median)", "base", "branch", "change"))
for k in keys:
    if k in skip or k.endswith("_start_mb"):
        continue
    b = statistics.median(x[k] for x in fb); a = statistics.median(x[k] for x in fr)
    ch = "" if b == 0 else "%+.0f%%" % ((a - b) / abs(b) * 100)
    print("%-44s %12.2f %12.2f %9s" % (k, b, a, ch))
for side, runs in (("base", base), ("branch", branch)):
    tps = [r.get("targets_pass") for r in runs if r.get("targets_pass")]
    if tps:
        print("%s targets_pass: %s" % (side, tps))
EOF
echo "raw results: $OUT"
