# Implementation prompt — Gaussian Splat support in Infinite

Copy everything below the line into a fresh Claude Code session on this repo.
Full research and rationale: `docs/plans/gaussian-splat-node.md`.

---

Implement 3D Gaussian Splatting rendering in Infinite. Read
`docs/plans/gaussian-splat-node.md` first — it has the full design, the verified
file/line references, and the trap list. Do not re-derive the design; it has
already been investigated against this tree.

Load these skills before writing code: `codebase-navigation`, `new-geometry-node`,
`windows-parity`, `node-ui-pillars`, `git-branch-workflow`.

## Hard constraints — verified, do not work around

1. **OpenGL 3.3 core, all shaders `#version 150`** (`src/main.cpp:47889-47897`).
   No compute shaders. No SSBOs. No GPU sort. The port target is the *WebGL2*
   class of implementation (`antimatter15/splat`), not the CUDA reference.
2. **No `_WIN32` in the node or core layer.** Anything platform-specific goes
   behind `Platform::` in `src/platform/Platform.h` with both sides implemented.
3. Shaders must be strict-GLSL clean: no implicit `int`→`float`. Windows drivers
   reject what macOS accepts; this repo has shipped that bug before.
4. Follow the existing point-cloud precedent (`GetPointCloud()` /
   `PointCloudRevision()`, `Geometry3DNodes.h:196-200`), do not invent a new
   cable type.

## Scope of THIS session: phases 1 and 2 only

Stop after phase 2. Do not build the node UI — that is a separate branch.

### Phase 1 — `feature/splat-loader`

Create `src/core/SplatIO.{h,cpp}`:

```cpp
struct Splat {
   float px, py, pz;
   float cov[6];          // 3D covariance upper triangle, PRE-COMPUTED on load
   float r, g, b, a;       // linear RGB from SH DC, opacity already sigmoid'd
   // Kept on the CPU only. NEVER uploaded - the GPU texture stays 64 B/splat
   // and the render path never reads these. They exist because a future Field
   // element kernel that deforms splats needs scale/rot as separate values;
   // recovering them from `cov` afterwards means eigendecomposing a 3x3 per
   // splat, which is lossy and unpleasant. Costs 28 B/splat of RAM (1M splats
   // = 92 MB instead of 64 MB) and nothing else. See the "Field" section of
   // docs/plans/gaussian-splat-node.md.
   float sx, sy, sz;       // ellipsoid radii, already exp()'d
   float qw, qx, qy, qz;   // rotation, already normalised, wxyz order
};
struct SplatCloud {
   std::vector<Splat> splats;
   float boundsMin[3], boundsMax[3];
   bool Empty() const { return splats.empty(); }
};
```

Implement:
- `bool LoadSplatPly(const std::string& path, SplatCloud& out, std::string& err)`
  — binary_little_endian only. **Parse the header and build a property→offset
  map**; do not assume field order, it differs between Postshot / Nerfstudio /
  INRIA exports.
- `bool LoadSplatFile(...)` for antimatter15's 32-byte `.splat`.

Conversions that MUST happen on load — getting any of these wrong is the
standard first-attempt failure:

| Stored | Convert |
|---|---|
| `opacity` (logit) | `1/(1+exp(-x))` |
| `scale_0..2` (log) | `exp(x)` |
| `rot_0..3` | quaternion in **wxyz** order — normalise it |
| `f_dc_0..2` | `rgb = 0.5 + 0.2820948 * f_dc` |

Then build `Σ = R S Sᵀ Rᵀ` once on the CPU and store the 6 upper-triangle
values. The shader must never redo this per frame — `cov` is what the GPU
texture is built from. Keep the de-log'd `scale` and the normalised quaternion
alongside it on the CPU struct (see the comment above); they are not uploaded
and the render path must not read them.

Route file access through the existing `Platform::` path helpers, not raw
`fopen(std::string)`.

**Exit criterion for phase 1:** a headless test that loads a small generated
`.ply` fixture (write the fixture generator too), and asserts splat count,
position bounds, and that a splat with stored `opacity = 0` comes back as
`a ≈ 0.5` and stored `scale = 0` comes back as radius `1.0`. Also assert that
rebuilding `cov` from the retained `scale`/`rot` reproduces the stored `cov`
bit-for-bit — that is what proves the retained values are actually usable by a
later Field pass and did not silently diverge. No GL, no window.
Commit phase 1 on its own branch before starting phase 2.

### Phase 2 — `feature/splat-render`

Add to `IGeometrySource` (`src/nodes/Geometry3DNodes.h`), defaulting to
`nullptr`/`0` so nothing existing changes:

```cpp
virtual const SplatCloud* GetSplatCloud() { return nullptr; }
virtual unsigned long long SplatCloudRevision() { return 0; }
```

**Forward both down `PassthroughSource()`** in `GeometryOpNode` and every other
passthrough wrapper. Skipping this makes splats silently vanish behind a
Transform node — the same class of bug that already hit env-light cache
invalidation in this repo.

In `Render3DNode::CookIfNeeded` (`Geometry3DNodes.cpp:1374`):

- **Split slot iteration into two passes.** Opaque meshes and point sprites
  first (depth test on, depth write on), then splats (depth test on, depth
  **write off**, `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`, back-to-front).
  This is the least mechanical part of the job — do it deliberately.
- Add `drawSplatSlot`, modelled on `drawCloudSlot` (`:1732`). Precedence becomes
  `splat cloud > point cloud > mesh triangles`, extending the existing rule at
  `:1964`.

GPU layout — no SSBO available, so:

- **Static, re-uploaded only when `SplatCloudRevision()` changes:** a
  `GL_RGBA32F` texture, 2048 wide, 4 texels per splat —
  `[pos.xyz, opacity]`, `[cov 0-2, —]`, `[cov 3-5, —]`, `[rgb, —]`.
  64 bytes/splat; 1M splats = 64 MB.
- **Per frame:** only a `GL_R32UI` index VBO with `glVertexAttribDivisor(…, 1)`,
  N entries, 4 MB for 1M. This being the only per-frame upload is the entire
  point of the design.
- Draw with `glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, N)`; the vertex
  shader does `texelFetch(splatTex, …)` using the instanced index.
  `texelFetch` is core in GLSL 1.30, so it is legal at `#version 150`.

Sorting — CPU, on one persistent worker thread:

- 16-bit key from quantised view-space depth, counting sort, 65536 buckets, O(N).
- Double-buffer the index array. The render thread uploads the newest *complete*
  order and **never blocks**; if a sort is in flight it reuses the previous order.
- Skip the sort when the camera barely moved
  (`dot(fwd, lastSortFwd) > 0.9995` and small position delta). A static camera
  must cost zero.

Vertex shader (EWA splatting):

```
Σ' = J W Σ Wᵀ Jᵀ            J = Jacobian of the affine approx of the projection
eigen-decompose the 2×2 Σ'  → major/minor screen axes
quad radius = 3·sqrt(eigenvalue) along each axis
cull behind the near plane by emitting gl_Position = vec4(0,0,2,1)
pass the conic (inverse Σ') and premultiplied colour to the fragment stage
```

Fragment shader:

```
power = -0.5*(conic.x*d.x*d.x + conic.z*d.y*d.y) - conic.y*d.x*d.y;
alpha = min(0.99, opacity * exp(power));
if (alpha < 0.00392) discard;     // 1/255
```

Add a VRAM/fill budget warning mirroring the existing sprite fill-rate guard at
`Geometry3DNodes.cpp:1826`.

**Exit criteria for phase 2, in order:**

1. A hardcoded 3-splat cloud (one wide+flat, one tall+thin, one rotated 45°)
   renders as three correctly oriented, correctly sized ellipses. Verify this
   before touching a real file — it isolates the covariance maths from the loader.
2. A real `.ply` scene renders recognisably, with correct back-to-front blending
   and no popping as the camera orbits.
3. `run-infinite-hygiene` is green.
4. Frame time with a static camera is unchanged from an empty scene
   (proves the sort skip works).

## Reporting

Report honestly at each exit criterion, with the actual test output. If the
covariance maths does not land, say so and stop rather than shipping something
that renders blobs. Do not proceed to phase 2 until phase 1's test passes and
is committed.
