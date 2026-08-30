---
name: render-pipeline-sweep
description: Sweeps Infinite's 3D pipeline as a pipeline rather than node by node - source mesh, operators, instancing, materials and mapping, lights and environment, simulation, and finally whether Render 3D's cached scene actually reflects any of it. Runs the generic geometry sweeps plus the per-stage fixtures. Use when asked "check the rendering", "is the 3D pipeline right", "why did my instances disappear", "why doesn't moving this change the render", "did the viewport freeze", or after touching anything between a geometry source and Render 3D.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/render-pipeline-sweep/driver.sh          # add --skip-build to reuse build/
```

For a single node's transform/mapping/revision invariants in isolation, use
`geometry-transform-sweep` - it is the narrower harness and iterates faster.
This skill is the pipeline-shaped view: the stages, and the places where a
stage silently swallows what the one above it produced.

## The pipeline, and what breaks at each joint

```
GeometryNode / Model / Text3D / Curve / Ocean / Particles
        |  IGeometrySource  (mesh + stamps + side-channels)
   mesh operators (GeometryOp, Displacement, Wrap, Cloth, Resynth, MeshToPoints, ...)
        |
   Instance on Points / Distribute / Join / Merge
        |
   Material / Mapping / Set Color
        |
   Render 3D  (scene cache, camera, lights, HDRI)
        |  texture
   the 2D graph
```

Every joint carries **more than the mesh**: a model matrix, a mapping
transform, an instance chain, an instance group matrix, a surface-texture
revision, and per-kind stamps (mesh / point cloud / curve). The recurring bug
class in this codebase is a node that forwards the mesh and drops one of the
others - a node that looks like a passthrough and isn't:

- `Cloth`, `Resynthesize 3D` and `Mesh to Points` forwarded every side-channel
  except `GetMappingTransform()`, so a Mapping node upstream did nothing.
- `Null 3D`, `Displacement` and `Wrap` overrode neither instancing accessor,
  so instances collapsed to a single stamp behind them; `Material`, `Set
  Color`, `Merge by Distance` and `Switcher 3D` forwarded the chain but not
  the group matrix, so a Transform wrapping the instancer was discarded.
- `Displacement` bumped its texture generation on **every** cook whenever a
  texture was connected, which made `Cloth` downstream treat every frame as a
  topology change and reset the simulation to rest pose forever.
- `Render3DNode::BuildSceneSignature` XOR-folded three revision accessors into
  one value, which cancelled to a constant 0 for every node returning the same
  counter from two of them - the render then cached its first frame forever.

That history is why the first six fixtures in the driver are **generic
sweeps** over every geometry-consuming node type rather than one fixture per
node someone remembered to write. A node added next month is covered without
editing anything.

## Reading the two directions

`REVISIONSWEEPTEST` and `RENDER3DCACHESWEEPTEST` are opposites, and both
matter:

- **REVISION**: a stamp must **not** move when nothing changed. A stamp that
  churns kills every downstream cache and resets simulations.
- **RENDER3DCACHE**: a stamp **must** move when something did. A stamp that
  never moves freezes the viewport on frame one.

A change that fixes one by breaking the other passes half this sweep. Run
both, always.

## Blind spots

- **Pixels are barely checked.** Most fixtures assert on mesh/stamp/count
  state, not rendered output. `run-infinite-hygiene`'s screenshot step is the
  only visual check, and it needs a human or a vision pass to read.
- **One GPU, one driver.** Everything here runs on the local macOS GL stack.
  GLSL 330 strictness, driver-specific defaults and Windows GL behaviour are
  outside it - see `windows-parity` and `pillar-parity-audit`.
- **No performance assertion.** Nothing fails because the pipeline got slower;
  see `rate-analysis-sweep`.
- **Simulation determinism** is checked only for Cloth's rewind reset. A
  simulation that drifts differently at a different frame rate would pass.
