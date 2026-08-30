#!/usr/bin/env bash
# Docked panels sweep: modulation matrix, performance matrix, node browser,
# viewport cards. See SKILL.md.
SWEEP_NAME="panels sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # --- headless (no GL/ImGui): pure-function halves of two panels ---
  # Performance matrix: element model, assignment, persistence.
  "PERFMATRIXTEST:1"
  # Node browser sort/filter strip, run through the exact functions the panel
  # calls. This is the only thing that catches a forgotten LibraryFilterCache
  # key - a missing key does not crash, it just makes a control silently do
  # nothing, which no build-time check can see.
  "BROWSERSORTTEST:1"

  # --- modulation matrix panel, live ---
  # Rows exist for a real link; a collapsed node still registers its param;
  # disabling a row freezes the value; enabled=false survives both the binary
  # and the text patch round trip.
  "MODMATRIXTEST:24"
  # Row fill stays stable while the table scrolls - the geometry bug this
  # fixture was written for. Run in each of the four dock orientations,
  # because the scroll/fill maths differs between a side dock and a
  # top/bottom dock, and the original bug only showed in one of them.
  "MODMATRIXGEOM:28"
  "MODMATRIXGEOM,MODMATRIXDOCK=0:28"
  "MODMATRIXGEOM,MODMATRIXDOCK=2:28"
  "MODMATRIXGEOM,MODMATRIXDOCK=3:28"

  # --- node-local viewport card render ---
  "MINIVIEWPORTTEST:12"
)

OBSERVE=()

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
