# Data-type modernization — prompt sequence

Five self-contained implementation prompts for modernizing the core data
types that flow through Infinite's node graph (Note, Audio, Modulator,
Texture, Mesh). Each file is ready to paste as-is into a fresh Claude Code
session. Run them in order — later phases assume earlier ones are merged
and validated.

Written after a full verification pass against the actual code (exact
struct fields, call sites, and line numbers confirmed, not assumed) on
2026-08-15. If the code has moved on significantly since then, re-verify
line numbers before pasting.

## Order and rationale

1. **[01-note-event-voice-id.md](01-note-event-voice-id.md)** — cheap,
   real correctness bug (note-off matching by pitch alone, not voice
   identity), no existing objection. Do this first.
2. **[02-texture-internal-format.md](02-texture-internal-format.md)** —
   cheap, low blast radius (default-parameter change, 25 existing call
   sites untouched), no existing objection. Do this second.
3. **[03-mesh-vertex-tangents.md](03-mesh-vertex-tangents.md)** —
   **conditional**. Normal mapping already works today via a
   screen-space-derivative method chosen specifically to avoid this field.
   The prompt requires reproducing a concrete visible artifact before
   writing any code — don't skip that gate.
4. **[04-modulator-bipolar-contract.md](04-modulator-bipolar-contract.md)**
   — **highest risk**. Touches ~18 modulator implementers plus the
   apply-loop and three test assertions. Run last, after 1–2 are merged.
   The prompt itself lays out a narrower, lower-risk option (Option A)
   alongside the original full-rename ask (Option B) and recommends
   starting with A.
5. **[05-mesh-named-attributes.md](05-mesh-named-attributes.md)** —
   **rescoped from the original ask**. A generic Blender-Geometry-Nodes-
   style attribute system was already proposed and explicitly rejected in
   `docs/plans/phase3-per-element-color.md` (eager revision-stamp cook
   model, no lazy field evaluator). This prompt asks for one fixed named
   field at a time instead, following the `Mesh::vertexColor` convention
   — and requires picking a concrete property with the user first, since
   none was confirmed.

## Do not build

Two things came up during scoping that should NOT be implemented as
originally framed:

- A **generic string-keyed attribute system** on `Mesh` — rejected in
  writing, four times, across `docs/plans/README.md`,
  `phase2-one-geometry-interface.md`, `phase3-per-element-color.md`,
  `phase4-selection-as-input.md`, plus a code comment on `SetColorNode`
  (`src/nodes/GeometryOpNodes.h:601-602`).
- **Vertex tangents added speculatively** — only add them if a concrete
  seam/artifact from the current screen-space-derivative normal mapping
  (`src/nodes/Geometry3DNodes.cpp:501-523`) is actually reproduced first.
