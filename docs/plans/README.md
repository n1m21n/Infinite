# Geometry roadmap — implementation prompts

Each file is a self-contained prompt for a fresh Claude Code session. Paste the
whole file; it assumes no memory of the conversation that produced it.

Written after an evaluation of Infinite's geometry data model against Blender's
geometry nodes (368 node docs). The headline finding: Blender's ~250-node count
is roughly 87 field/attribute infrastructure nodes, ~45 pure math, and only
**19 actual conversion nodes**. Chasing the node list is chasing the wrong
number — the substrate is what makes conversions compose.

## Status

| Phase | File | Status |
|---|---|---|
| 0 — `ToPoints` correctness (weld, edge dedup, stable ids) | — | **done**, `e3ae030` |
| 1 — points render as camera-facing sprites | — | **done**, `f47a942` |
| 2 — one geometry interface, no per-class ladders | `phase2-one-geometry-interface.md` | not started |
| 3 — per-element colour attributes | `phase3-per-element-color.md` | not started |
| 4 — selection as an input, not four operators | `phase4-selection-as-input.md` | not started |
| 5 — curves first-class (3 conversion nodes) | `phase5-curves-first-class.md` | not started |
| 6 — real point distribution (4 nodes) | `phase6-point-distribution.md` | not started |

## Ordering

**Phase 2 first.** It is the only one that reduces future cost rather than
adding surface: it collapses four hand-written `dynamic_cast` ladders into
generic loops, including `DisconnectAllTo`, whose own comment warns that a
missing entry "would leave a pointer to a freed node and crash on the next
cook." Every phase after it is cheaper for having it, and every new node type
added before it is one more class to migrate.

After that, order by what you want:

- **Visual payoff soonest** → 6 (Distribute Points on Faces), then 3, then 5.
- **Least future cost** → 4 (stops the `XSelected` combinatorial growth before
  more operators exist).

Phases 3, 4, 5 and 6 are mutually independent. Each names its own dependencies.

## Two design decisions that were deliberately reversed

Both files carry a "do not improve it" section at the top, because a fresh
session will otherwise reach for the Blender-shaped answer and it is wrong here.

1. **Phase 2 does not introduce a `Geometry` value type with `shared_ptr`
   components.** That would require all 19 implementors to change how they own
   their mesh, for a purity win. Folding the three interfaces into one achieves
   the same external property — one socket, one validator — without touching
   ownership.

2. **Phase 3 does not introduce generic string-keyed named attributes.**
   Blender's named attributes only work because a field evaluator can read an
   arbitrary name and compute with it. Infinite's cook model is eager with
   revision-stamp caching, which is structurally incompatible with lazy fields,
   and fields are cut permanently. Without an evaluator, a `Store Named
   Attribute` node would write into a void.

## Cut permanently, not deferred

- **Lazy fields** (~87 Blender nodes) — incompatible with eager cooking.
- **A "Grid" data type** — it is a mesh primitive or a lattice of points.
- **Volumes / SDF** (30 nodes) — voxel grids plus a raymarcher; different product.
- **Topology query nodes** (`edge_neighbors`, `corners_of_face`, dual mesh) —
  only meaningful with fields.
- **Implicit conversion on connect** — offer to *insert* the conversion node
  instead.
- **N-gon face tracking** — a static-modelling concern. If per-face attributes
  need face identity later, reuse the `Mesh::selectionGroup` trick rather than
  restructuring `Mesh`.

## Why this shape and not Blender's

Infinite is a real-time, modulated GPU compositor — Transport, LFOs, audio
analyze, macro knobs, palettes, feedback — that grew a geometry arm. Blender's
geometry nodes are a static modelling system with no live performance model.

So the phases that earn their place are the ones making geometry **animatable,
modulated and performable**, not the ones replicating a static-topology
toolkit. The end state is not Blender-with-fewer-nodes; it is a geometry graph
you can perform live, where a palette drives point colour and geometry responds
while the transport runs.
