#!/usr/bin/env bash
# Generic node-type sweeps for Infinite's 3D geometry nodes.
#
# Runs three env-var-gated fixtures (src/main.cpp, search "SWEEPTEST") through
# the real compiled .app binary. Each wraps a real mesh source in a small
# probe IGeometrySource and wires it into every node type that takes a
# geometry input (GeometryOpNode, DisplacementNode, MeshResynthNode,
# MeshToPointsNode, Null3DNode, MaterialNode, MappingNode, JoinGeometryNode,
# ClothNode, both slots of InstanceOnPointsNode, PathNode's mesh-follow, as
# applicable per check), then asserts one invariant across all of them at
# once:
#
#   - TRANSFORMSWEEPTEST — the probe's GetModelMatrix() reaches the node's
#     final world-space output.
#   - MAPPINGSWEEPTEST   — the probe's GetMappingTransform() reaches the
#     node's output, i.e. a Mapping node patched upstream isn't silently
#     dropped. Caught a real bug: ClothNode, MeshResynthNode and
#     MeshToPointsNode forwarded every other side-channel from their input
#     but not this one.
#   - REVISIONSWEEPTEST  — a node's MeshRevision()/generation stamp does not
#     move across two cooks with nothing actually changed (DisplacementNode
#     gets a real, static, connected texture for this one, since that's the
#     case that actually caught the bug). Caught a real bug: DisplacementNode
#     bumped its texture generation on every single cook whenever a texture
#     was connected, even with unchanged pixels, which forced ClothNode
#     downstream to treat every frame as a topology change and reset the
#     simulation back to rest pose continuously instead of ever draping.
#   - RENDER3DCACHESWEEPTEST — the opposite direction from REVISIONSWEEPTEST:
#     a real upstream mesh/cloud/curve change DOES reach Render 3D's rendered
#     pixels, rather than being swallowed by its scene cache. Caught a real
#     bug: Render3DNode::BuildSceneSignature XOR-folded MeshRevision()/
#     PointCloudRevision()/CurveStamp() into one value, which silently
#     cancelled to a constant 0 for every node that returns the same counter
#     from two of those three accessors (MeshToPointsNode, both
#     DistributePoints* nodes, CurveNode) - the render then cached its first
#     frame forever and no upstream edit ever showed up in the viewport.
#
# None of these are a fixed fixture like run-infinite-hygiene's BUGTEST
# checks - they exist to keep covering every geometry-consuming node type as
# new ones are added, without anyone hand-writing a new check per node per
# invariant. See ARCHITECTURE.md's "Node Library" section, "Invariants for
# IGeometrySource-consuming nodes" for the three rules these enforce.
#
# Usage:
#   .claude/skills/geometry-transform-sweep/driver.sh               # build + run
#   .claude/skills/geometry-transform-sweep/driver.sh --skip-build   # reuse existing build/
#
# Exit code: 0 if the build succeeded and every check passed, 1 otherwise.

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
# name : env var : "OK" suffix : "FAIL" suffix : log file
SWEEPS=(
  "Transform propagation:TRANSFORMSWEEPTEST:TRANSFORM SWEEP OK:TRANSFORM SWEEP FAIL:/tmp/infinite_transform_sweep.log"
  "Mapping-transform forwarding:MAPPINGSWEEPTEST:MAPPING SWEEP OK:MAPPING SWEEP FAIL:/tmp/infinite_mapping_sweep.log"
  "Revision stability:REVISIONSWEEPTEST:REVISION SWEEP OK:REVISION SWEEP FAIL:/tmp/infinite_revision_sweep.log"
  "Render 3D cache invalidation:RENDER3DCACHESWEEPTEST:RENDER3D CACHE SWEEP OK:RENDER3D CACHE SWEEP FAIL:/tmp/infinite_render3d_cache_sweep.log"
)

overallOk=1
for spec in "${SWEEPS[@]}"; do
  IFS=':' read -r name envvar okMark failMark out <<< "$spec"
  step "$name sweep"
  env "INFINITE_${envvar}=1" INFINITE_EXITAFTER=10 "$BIN" >"$out" 2>&1
  rc=$?

  if [ $rc -ne 0 ]; then
    echo "[CRASH] exited $rc — see $out"
    overallOk=0
    continue
  fi

  if ! grep -qE '^(  \[pass\]|  \[FAIL\]|  \[SKIP\])' "$out"; then
    echo "no per-node verdict lines found — the fixture may not have reached its printf block."
    echo "raw output:"
    cat "$out"
    overallOk=0
    continue
  fi

  grep -E '^  \[(pass|FAIL|SKIP)\]' "$out"
  skips=$(grep -cE '^  \[SKIP\]' "$out")
  if [ "$skips" -gt 0 ]; then
    echo "$skips node(s) skipped (produced no comparable geometry) — see $out for which."
    echo "A skip usually means the fixture's probe mesh/mode doesn't suit that node"
    echo "(e.g. a boundary-follow needs an open mesh); it does not mean that node is fine."
  fi

  if grep -qE "^${failMark}\$" "$out"; then
    echo "${name^^} SWEEP FAILED — see $out"
    overallOk=0
    continue
  fi

  echo "$name: all nodes correctly propagate the checked invariant."
done

echo
if [ "$overallOk" -eq 0 ]; then
  echo "one or more sweeps failed — see above."
  exit 1
fi
echo "all sweeps passed."
exit 0
