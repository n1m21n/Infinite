# Fix: Render 3D's frame cache goes stale — the render only updates when you orbit the viewport

## Symptom

Patch: `Particle System` → (cloud) `Instance on Points` ← (shape) `Cube`, then
`Instance on Points` → `geo A` of `Render 3D`.

The Particle System advances every frame, and the `Instance on Points` node's own
inline node preview shows the particles moving. But the `Render 3D` node's output
image is frozen. It only refreshes when the user drags/orbits inside the Render 3D
viewport. This same "3D render isn't live" symptom has shown up in other patch
shapes too — this prompt fixes the shared root cause, not just this patch.

## Root cause (confirmed by reading the code, not inferred)

`Render3DNode::CookIfNeeded` early-returns on an unchanged scene signature:

- `src/nodes/Geometry3DNodes.cpp:1383-1385`
  ```cpp
  SceneSignature sceneSig = BuildSceneSignature();
  if (mHasSceneBuilt && sceneSig == mSceneBuilt)
     return;
  ```

`Render3DNode::BuildSceneSignature` (`src/nodes/Geometry3DNodes.cpp:1265-1332`)
records, per geometry slot, only:

- `src/nodes/Geometry3DNodes.cpp:1306`
  ```cpp
  sig.geomRev[i] = source->MeshRevision() ^ source->PointCloudRevision() ^ source->CurveStamp();
  ```
- plus `SurfaceTextureRevision()` and `GetMaterial()`.

For an `InstanceOnPointsNode` fed by a particle cloud, **all three of those stamps
are permanently constant**:

- `InstanceOnPointsNode::MeshRevision()` returns `instanceShape->MeshRevision()`
  — the Cube's stamp, which never moves (`src/nodes/GeometryOpNodes.cpp:469-473`).
- `InstanceOnPointsNode` does not override `PointCloudRevision()`, so it inherits
  the base `{ return 0; }` (`src/nodes/Geometry3DNodes.h:152`).
- It does not override `CurveStamp()` either → constant 0.

The live-changing data is `InstanceTransforms()` / `InstanceColors()`, stamped by
`InstanceRevision()` (`src/nodes/GeometryOpNodes.h:507`, bumped in
`InstanceOnPointsNode::CookIfNeeded` → `Rebuild()`, `src/nodes/GeometryOpNodes.cpp:653`).
`InstanceOnPointsNode::CookIfNeeded` **does** correctly detect the cloud's revision
change (`src/nodes/GeometryOpNodes.cpp:631, 641`) and rebuilds the transforms every
frame. So the instancer is fine — Render 3D simply never looks at that stamp when
deciding whether to redraw.

`InstanceRevision()` *is* read in Render 3D, but only inside the GPU-upload block at
`src/nodes/Geometry3DNodes.cpp:2023-2038`, which sits **after** the early return at
line 1384 and is therefore unreachable on a stale-signature frame.

Corroborating evidence that this is exactly the split: the per-node inline preview
(`src/core/NodeViewport.cpp:540-570, 745-769`) *does* fold `InstanceRevision()` and
`GetInstanceGroupMatrix()` into its own dirty check — which is precisely why the
`Instance on Points` node preview is live while `Render 3D` is frozen.

Orbiting unfreezes it because `camAzimuth`/`camElevation` are Render 3D's own params,
which go through `VisitParams` into `sig.own` (`src/nodes/Geometry3DNodes.cpp:1269-1271`),
so the signature changes and the draw runs.

### Second instance of the same bug class (fix in the same pass)

`SceneSignature` has **no field for any geometry source's model matrix**
(see the struct at `src/nodes/Geometry3DNodes.h:503-521`). But
`GeometryNode::GetModelMatrix()` is evaluated live off the transport clock:

- `src/nodes/Geometry3DNodes.cpp:796-805`
  ```cpp
  const float spin = spinY * (float)Transport::Instance().Beats();
  ```

So a Cube with `spinY != 0` patched straight into Render 3D changes its world
transform every frame with zero revision bump and zero signature contribution →
the render is frozen in exactly the same way. The comment at
`src/nodes/Geometry3DNodes.h:463-468` already anticipates a wrapping Transform
node's params changing without an `InstanceRevision()` bump, and handles it in the
upload path — again unreachable behind the early return.

## What to change

All edits are in `src/nodes/Geometry3DNodes.h` and `src/nodes/Geometry3DNodes.cpp`.
Do **not** change `InstanceOnPointsNode` — its cooking and stamping are correct.

1. **Add the instancing stamp to `SceneSignature`.**
   In the `SceneSignature` struct (`src/nodes/Geometry3DNodes.h`, around line 503),
   add per-slot fields:
   ```cpp
   unsigned long long instanceRev[kSlots] = { 0, 0, 0, 0 };
   size_t instanceCount[kSlots] = { 0, 0, 0, 0 };
   ```
   and compare them in `operator==` alongside the existing `geomRev` check.
   (`instanceCount` is cheap insurance for a rebuild that lands on the same
   revision value — include it, or justify in a comment why you dropped it.)

   Populate them in `BuildSceneSignature` (`Geometry3DNodes.cpp:1295-1314`) using
   the **same `FindInstancer(source)` chain walk** the draw path already uses
   (`Geometry3DNodes.cpp:56-63`), not a direct `dynamic_cast` — a Transform node
   patched between the instancer and Render 3D must still resolve. The header
   already declares `FindInstancer` in the anonymous namespace at the top of the
   .cpp, so it is in scope from `BuildSceneSignature`.

2. **Add each slot's world transform to `SceneSignature`.**
   Add `Mat4 modelMatrix[kSlots];` and `Mat4 instanceGroupMatrix[kSlots];` and
   compare both in `operator==` (`Mat4` already has `operator==` — it is used at
   `Geometry3DNodes.cpp:2037` and `NodeViewport.cpp:570`). Fill them from
   `source->GetModelMatrix()` and `source->GetInstanceGroupMatrix()` in the same
   per-slot loop. This fixes the `spinY`/animated-transform case above and the
   wrapping-Transform-node case in one shot.

3. **Sanity-check the remaining live-but-unstamped inputs.** While you are in
   `BuildSceneSignature`, confirm nothing else feeding the draw changes per-frame
   without contributing to the signature. Two known-safe ones already handled:
   camera/light `orbitPerBeat` set `sig.animated = true` (lines 1279-1280,
   1291-1292), and bound material maps set `sig.texturedMaterial = true`
   (lines 1309-1313); both force an unconditional redraw. If you find a third
   live-but-unstamped input, either stamp it or fold it into `animated` — and say
   which you did and why.

Update the block comment above `struct SceneSignature`
(`src/nodes/Geometry3DNodes.h:481-501`) to describe the new fields; it is currently
an accurate, detailed explanation of what the signature covers and will be wrong
after this change.

## Verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then:

```bash
.claude/skills/geometry-transform-sweep/driver.sh
```

Note that this sweep did **not** catch the bug — it exercises geometry-source
outputs, not `Render3DNode`'s frame cache. After the fix, consider adding a check
that drives a `Particle System → Instance on Points → Render 3D` chain for two
frames and asserts `Render3DNode`'s output revision (`mRevision`) advances between
them, and a second case with a `spinY != 0` Geometry node straight into Render 3D.
If that check is awkward to express in the existing sweep harness, say so rather
than forcing it — but do at least confirm manually in the running app that both
patches are live without touching the viewport.

## Out of scope

- Do not touch `InstanceOnPointsNode`, `NodeViewport`, or the GPU-upload block at
  `Geometry3DNodes.cpp:2023-2060` — they are all correct today.
- Do not replace the signature cache with an unconditional per-frame redraw. The
  cache is deliberate and load-bearing for static scenes; the fix is to make the
  signature complete, not to remove it.
