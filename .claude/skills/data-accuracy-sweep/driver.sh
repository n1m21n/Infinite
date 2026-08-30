#!/usr/bin/env bash
# Data accuracy sweep: does what a node holds survive being copied, saved,
# passed through, cached and bypassed? See SKILL.md.
SWEEP_NAME="data accuracy sweep"
SWEEP_ARGS=("$@")

ASSERT=(
  # Every node type's params survive copy/paste AND save/load, checked per
  # type and per value. The single widest data-integrity fixture in the app.
  "ROUNDTRIPTEST:10"
  # Binary patch round trip of a real 3D graph: params, image wiring, geometry
  # links and modulation links all present after save -> NewPatch -> load.
  "PATCHTEST:8"
  # Text patch (autosave) round trip: params, wiring, modulation, palette
  # bindings, expressions and globals. A separate serializer from PATCHTEST -
  # one passing says nothing about the other.
  "AUTOSAVETEST:8"
  # Autosave marker handling, headless.
  "AUTOSAVEMARKERTEST:1"

  # Pass-through nodes must not alter the data. Null forwards a mesh with the
  # SAME stamp and a texture with the same id and size, and re-uploads nothing
  # on a steady frame.
  "UTILTEST:12"
  # TextureRevision() honesty: a node claiming its texture is unchanged must
  # actually be unchanged, and one that changed must say so.
  "REVISIONSWEEPTEST:10"
  # Bypass passes the input through untouched rather than substituting
  # something of its own.
  "BYPASSTEST:8"
  # Modulation lands inside the destination's own units and bounds rather than
  # writing a raw 0..1 into a parameter that is not 0..1.
  "MODBOUNDSTEST:30"

  # Everything in the registry cooks a real texture of a real size when fed.
  "@IMAGERESYNTH_SELFTEST:3"
)

OBSERVE=(
  # Work counter over a populated canvas: a static patch should go idle. A
  # patch that never goes idle is re-cooking data that did not change.
  "CACHETEST,SHOWCASE:24"
)

data_static() {
  # Same gate as compositing-pipeline-sweep, for the same reason: a cook that
  # is not memoized on the frame id runs once per downstream cable, so an
  # accumulating node's data depends on how many things read it.
  python3 .claude/skills/compositing-pipeline-sweep/check.py
}
SWEEP_EXTRA=data_static

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/scripts/sweep_runner.sh"
