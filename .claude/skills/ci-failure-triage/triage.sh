#!/usr/bin/env bash
# triage.sh <run-id|branch>  - one-shot read of every failed job in a CI run.
# Prints per failed job: failed step, first real error lines, and for a CL.exe crash the
# file it was compiling (the LAST name CL printed - it prints a name when it STARTS a file).
set -u
repo=n1m21n/Infinite
arg="${1:?run id or branch}"
if [[ "$arg" =~ ^[0-9]+$ ]]; then run=$arg
else run=$(gh run list --branch "$arg" -L 1 --json databaseId -q '.[0].databaseId'); fi
echo "run $run  https://github.com/$repo/actions/runs/$run"
gh run view "$run" --json status,jobs -q '.status, (.jobs[]|"  \(.name)\t\(.status)\t\(.conclusion)")'
for j in $(gh run view "$run" --json jobs -q '.jobs[]|select(.conclusion=="failure")|.databaseId'); do
  name=$(gh api repos/$repo/actions/jobs/$j -q .name)
  echo; echo "=== FAILED: $name (job $j)"
  gh api repos/$repo/actions/jobs/$j -q '.steps[]|select(.conclusion=="failure")|"failed step: "+.name'
  log=$(mktemp); gh api repos/$repo/actions/jobs/$j/logs | tr -d '\r' > "$log" 2>&1
  # a compiler crash: show the file it was on, and whether that file is /Od-scoped already
  if grep -q "CL!RaiseException\|exited with code -5297" "$log"; then
    last=$(grep -n "CL!RaiseException" "$log" | head -1 | cut -d: -f1)
    f=$(head -n "$last" "$log" | grep -E "^[0-9T:.Z-]+ +[A-Za-z0-9_+.-]+\.(cpp|c|cc)$" | tail -1 | awk '{print $2}')
    echo "COMPILER CRASH (CL.exe). File it was compiling: $f"
    grep -n "$f" /Users/namansoni/infinte/CMakeLists.txt | head -3
    grep -n "COMPILE_OPTIONS.*/Od" /Users/namansoni/infinte/CMakeLists.txt
  fi
  echo "--- first errors:"
  grep -n -i "Performing Test\|HAVE_" -v "$log" | grep -i -m8 ": error \|fatal error\|error LNK\|FAILED\|##\[error\]\|AddressSanitizer\|runtime error:\|Assertion\|undefined reference" | cut -c1-240
  echo "--- tail:"; tail -n 6 "$log" | cut -c1-200
  rm -f "$log"
done
