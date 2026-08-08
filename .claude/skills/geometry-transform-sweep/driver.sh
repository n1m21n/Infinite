#!/usr/bin/env bash
# Generic transform-propagation sweep for Infinite's 3D geometry nodes.
#
# Runs INFINITE_TRANSFORMSWEEPTEST (src/main.cpp, search
# "TRANSFORMSWEEPTEST") through the real compiled .app binary. That fixture
# wraps a real mesh source in a probe whose GetModelMatrix() is swapped
# between Identity and a distinct-per-axis translation, wires the probe into
# every node type that takes an IGeometrySource input (GeometryOpNode,
# DisplacementNode, MeshResynthNode, MeshToPointsNode, Null3DNode,
# MaterialNode, MappingNode, JoinGeometryNode, ClothNode, both slots of
# InstanceOnPointsNode, and PathNode's mesh-follow), and asserts the node's
# final world-space output moved by exactly that translation. It is not a
# fixed fixture like run-infinite-hygiene's BUGTEST checks - it is meant to
# keep covering every geometry-consuming node type as new ones are added,
# without anyone hand-writing a new check per node.
#
# Usage:
#   .claude/skills/geometry-transform-sweep/driver.sh               # build + run
#   .claude/skills/geometry-transform-sweep/driver.sh --skip-build   # reuse existing build/
#
# Exit code: 0 if the build succeeded and every node passed, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

step() { printf '\n== %s ==\n' "$1"; }

# ---------------------------------------------------------------------------
step "Build"
if [ "$SKIP_BUILD" -eq 1 ]; then
  echo "skipped (--skip-build)"
elif [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
else
  echo "no existing build/, configuring fresh"
  if ! cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tee /tmp/infinite_build.log; then
    echo "CONFIGURE FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee -a /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
fi

if [ ! -x "$BIN" ]; then
  echo "binary not found at $BIN after build"
  exit 1
fi

# ---------------------------------------------------------------------------
step "Transform propagation sweep"
OUT="/tmp/infinite_transform_sweep.log"
INFINITE_TRANSFORMSWEEPTEST=1 INFINITE_EXITAFTER=10 "$BIN" >"$OUT" 2>&1
rc=$?

if [ $rc -ne 0 ]; then
  echo "[CRASH] exited $rc — see $OUT"
  exit 1
fi

if ! grep -qE '^(  \[pass\]|  \[FAIL\]|  \[SKIP\])' "$OUT"; then
  echo "no per-node verdict lines found — the fixture may not have reached its printf block."
  echo "raw output:"
  cat "$OUT"
  exit 1
fi

grep -E '^  \[(pass|FAIL|SKIP)\]' "$OUT"
echo
skips=$(grep -cE '^  \[SKIP\]' "$OUT")
if [ "$skips" -gt 0 ]; then
  echo "$skips node(s) skipped (produced no comparable geometry) — see $OUT for which."
  echo "A skip usually means the fixture's probe mesh/mode doesn't suit that node"
  echo "(e.g. a boundary-follow needs an open mesh); it does not mean that node is fine."
fi

if grep -qE '^TRANSFORM SWEEP FAIL$' "$OUT"; then
  echo "TRANSFORM SWEEP FAILED — see $OUT"
  exit 1
fi

echo "all nodes in the sweep correctly propagate their upstream transform."
exit 0
