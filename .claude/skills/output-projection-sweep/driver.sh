#!/usr/bin/env bash
# Output / projection sweep: Syphon, Spout, projector windows, terminal nodes.
# See SKILL.md.
SWEEP_NAME="output + projection sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # Spout loopback: publish a synthetic texture and receive it back in the same
  # process, checking pixels AND orientation. Windows-only by design - it
  # prints SKIP on macOS, and SKIP on a Windows box with no
  # WGL_NV_DX_interop2, which is graded as a pass on purpose.
  "SPOUTLOOPTEST:1"
  # Every registered node type - Syphon In/Out, Projection, Output, Viewport
  # included - spawns, is fed, and cooks a real texture.
  "@IMAGERESYNTH_SELFTEST:3"
)

OBSERVE=()

output_static() {
  python3 .claude/skills/output-projection-sweep/check.py
}
SWEEP_EXTRA=output_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
