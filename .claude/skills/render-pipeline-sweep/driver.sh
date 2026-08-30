#!/usr/bin/env bash
# Rendering / geometry pipeline sweep: source -> operator -> Render 3D -> pixels.
# Usage: driver.sh [--skip-build]
SWEEP_NAME="render-pipeline-sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # --- generic node-type sweeps (cover every geometry-consuming type at once)
  "TRANSFORMSWEEPTEST:10"      # an upstream transform reaches the final output
  "MAPPINGSWEEPTEST:10"        # an upstream Mapping node isn't silently dropped
  "REVISIONSWEEPTEST:10"       # a stamp doesn't move when nothing changed
  "INSTANCESWEEPTEST:10"       # instancing side-channels survive a passthrough node
  "RENDER3DCACHESWEEPTEST:10"  # a real upstream change DOES reach rendered pixels
  "RENDER3DLIVETEST:10"        # live/animated sources render, not just static ones
  # --- specific pipeline stages
  "GEOTEST:30"                 # geometry sources
  "MESHOPTEST:30"              # mesh operators
  "TEXT3DTEST:30"              # text -> mesh
  "PATHOCEANTEST:35"           # path and ocean surfaces
  "PADPATHTEST:35"
  "WRAPTEST:35"
  "DISTRIBUTETEST:10"          # point distribution
  "PHASE4TEST:10"
  "INSTANCESELECTTEST:10"
  # Mesh face selection: select-by-normal, delete/keep the selection, move it,
  # extrude it, and reproduce a random selection from its seed.
  "SELECTTEST:6"
  "MATFRAMETEST:35"            # materials across frames
  "MAPTEST:35"                 # UV mapping
  "SHADOWTEST:35"              # shadowing
  "ENVTEST:14"                 # HDRI / environment lighting
  "3DTEST:35"                  # the end-to-end 3D pass
  "CLOTHTEST:70"               # simulation stability + rewind reset
  "PARTICLETEST:60"            # particle sim -> instancing -> triangles rendered
)
OBSERVE=()

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
