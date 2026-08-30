#!/usr/bin/env bash
# Modulation sweep: sources, destinations, the binding model, and the matrix.
# Usage: driver.sh [--skip-build]
SWEEP_NAME="modulation-sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  "MODMATRIXTEST:30"    # bind/unbind/enable/disable through the matrix, and what the destination holds
  "MODMATRIXGEOM:30"    # the matrix's own row fill must be scroll-invariant
  "MODBOUNDSTEST:30"    # a binding's lo/hi range, clamping and integer snapping at the destination
  "MACROTEST:30"        # macro controls as modulation sources, across save/load
  "PERFMATRIXTEST:1"    # performance matrix patch round trip (headless, exit-code gated)
  "ROUNDTRIPTEST:35"    # every node type's params, incl. modulators, survive save/load
)
# No verdict line - these print a value trace to read, not an assertion.
OBSERVE=(
  "MODTEST:46"          # LFO -> Shape size: beats, lfo value and the destination each frame
)

sweep_static() {
  local root; root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
  python3 "$root/scripts/audit_node_params.py" --out "$root/docs/node_param_audit.md"
}
SWEEP_EXTRA=sweep_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
