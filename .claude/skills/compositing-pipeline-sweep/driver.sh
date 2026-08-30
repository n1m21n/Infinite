#!/usr/bin/env bash
# Compositing / 2D image pipeline sweep. See SKILL.md.
SWEEP_NAME="compositing / 2D pipeline sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # Every registered node type spawns, gets fed from a real Shape source, and
  # cooks a texture with a non-zero id and non-zero dimensions. This is the one
  # fixture that covers the whole 2D surface rather than one node of it.
  "@IMAGERESYNTH_SELFTEST:3"
  # Bypassing a node mid-chain must pass its input through untouched
  # (invert active -> dark pixel, invert bypassed -> bright pixel again).
  "BYPASSTEST:8"
  # Colour extraction + palette binding + patch round trip of the binding.
  "PALETTETEST:12"
  # Subject-mask backend. Prints "REMOVEBGTEST SKIP: ..." where no backend
  # exists, which is graded as a pass on purpose - a missing OS feature is not
  # a compositing regression.
  "REMOVEBGTEST:1"
)

OBSERVE=(
  # Per-generation pixel drift of the resynth accumulator: read the numbers,
  # they assert nothing. drift=0 on every line means it stopped stepping.
  "RESYNTHTEST:16"
  # Work counter per frame over a populated canvas. Rising idleStreak on a
  # static patch is the point; a permanently 0 streak means something in the
  # 2D chain re-cooks every frame with no input change.
  "CACHETEST,SHOWCASE:24"
)

compositing_static() {
  python3 .claude/skills/compositing-pipeline-sweep/check.py
}
SWEEP_EXTRA=compositing_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
