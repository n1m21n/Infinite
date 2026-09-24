#!/usr/bin/env bash
# Persistent base build for A/B benchmarks: one worktree of main at
# ../infinte-base, kept for good and rebuilt incrementally, so no session
# stashes, rebuilds main in its own checkout, or builds a base from scratch.
#
# Usage: scripts/bench/base.sh [ref]    (default: main)
# Prints the base Infinite.app path on the last line, e.g.
#   BASE=$(scripts/bench/base.sh | tail -1)
#   scripts/bench/ab.sh "$BASE" build/Infinite.app auto -- INFINITE_BENCH_B6NODES=300 ...
# Never remove ../infinte-base when a branch is done; it is shared.
set -euo pipefail
REF="${1:-main}"
ROOT="$(git rev-parse --show-toplevel)"
COMMON="$(cd "$(git rev-parse --git-common-dir)" && pwd)"
# Sibling of the main checkout: <parent of .git's checkout>/infinte-base
BASE_DIR="$(dirname "$(dirname "$COMMON")")/infinte-base"

if [[ ! -d "$BASE_DIR" ]]; then
   echo "base.sh: creating $BASE_DIR at $REF (first build is a full build, later ones are incremental)" >&2
   git -C "$ROOT" worktree add --detach "$BASE_DIR" "$REF" >&2
fi
want="$(git -C "$ROOT" rev-parse "$REF^{commit}")"
have="$(git -C "$BASE_DIR" rev-parse HEAD)"
if [[ "$want" != "$have" ]]; then
   if [[ -n "$(git -C "$BASE_DIR" status --porcelain --ignore-submodules=all | grep -v 'tools/semi-brain/')" ]]; then
      echo "base.sh: $BASE_DIR has local changes outside tools/semi-brain; not touching it" >&2; exit 1
   fi
   echo "base.sh: moving base ${have:0:7} -> ${want:0:7} ($REF)" >&2
   # Detach first if main is checked out there, so no branch is ever moved by force.
   git -C "$BASE_DIR" checkout -q --detach "$want" -- 2>/dev/null || git -C "$BASE_DIR" checkout -q --force --detach "$want"
fi
if [[ ! -f "$BASE_DIR/build/CMakeCache.txt" ]]; then
   cmake -S "$BASE_DIR" -B "$BASE_DIR/build" -DCMAKE_BUILD_TYPE=Release >&2
fi
cmake --build "$BASE_DIR/build" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >&2
echo "$BASE_DIR/build/Infinite.app"
