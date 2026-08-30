#!/usr/bin/env bash
# Cable-connection sanity for Infinite: the static four-chain consistency
# check, plus the runtime fixtures that exercise connect/disconnect/delete.
#
# Usage: driver.sh [--skip-build]
SWEEP_NAME="cable-logic-sweep"
SWEEP_ARGS=("$@")

# Verdict-printing fixtures. Budgets copied from run-infinite-hygiene's
# driver, where each was verified to print its verdict by that frame.
ASSERT=(
  "ROUNDTRIPTEST:35"        # every registered type: spawn, save, load, compare
  "PINDUPTEST:10"           # no node hands two controls the same pin id
  "DELETECRASHTEST:8"       # deleting a wired node leaves no dangling image cable
  "AUDIOTEARDOWNSWEEPTEST:10" # ...the same for every audio/note node type
  "NOTEFANOUTTEST:4"        # one note source into several consumers
)
OBSERVE=()

sweep_static() {
  python3 "$(dirname "${BASH_SOURCE[0]}")/check.py"
}
SWEEP_EXTRA=sweep_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
