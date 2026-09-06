# Gaussian Splatting in Infinite — research, decision, and build plan

Status: research complete, not yet implemented.
Investigated against this tree on 2026-09-06.

---

## 0. The two facts that decide everything

Both were read out of this repo, not assumed.

| Fact | Where | Consequence |
|---|---|---|
| Infinite runs an **OpenGL 3.3 core** context, and all 56 shaders are `#version 150` | `src/main.cpp:47889-47897`, `src/main.cpp:32022-32033` | **No compute shaders, no SSBOs, no GPU radix sort.** The reference CUDA rasteriser is off the table. The *WebGL2* class of implementation is the only one that ports. |
| Render3D already has a per-slot **instanced billboard path** for point clouds, with camera-facing quads built in the vertex shader | `src/nodes/Geometry3DNodes.cpp:1732-1830` (`drawCloudSlot`), shader at `:240-305` (`uIsSprite`, `uCamRight/uCamUp`) | A Gaussian splat is *an oriented, anisotropic version of a billboard you already draw*. The integration is an extension of an existing pass, not a new renderer. |

> GL 3.3 is not a blocker. `antimatter15/splat` renders 1M+ splats at 60fps on **WebGL2**, which is a strictly weaker feature set than GL 3.3. The technique it uses — static data texture + CPU sort + tiny per-frame index buffer — is exactly what this codebase can do.

---

## 1. What Gaussian splatting actually is

A scene is stored as N anisotropic 3D Gaussians. No mesh, no triangles, no texture atlas.

Each splat carries:

| Field | Size | Meaning |
|---|---|---|
| position | 3 float | centre in world space |
| scale | 3 float | ellipsoid radii (stored as log, `exp()` on load) |
| rotation | 4 float | quaternion (stored unnormalised, normalise on load) |
| opacity | 1 float | stored as logit, `sigmoid()` on load |
| SH coeffs | 3 to 48 float | colour. Degree 0 = flat RGB. Degree 3 = view-dependent |

Rendering is **not** raytracing. It is a sort + a blend:

```
 for each splat:                                     ← vertex shader, 1 instance
   Σ  = R S Sᵀ Rᵀ                    3D covariance from rot/scale
   Σ' = J W Σ Wᵀ Jᵀ                  project to 2D  (EWA splatting, J = Jacobian
                                     of the affine approx of the projection)
   eigen-decompose Σ' (2×2)     →    major/minor screen axes
   emit a quad of radius 3σ along those axes

 for each fragment:                                  ← fragment shader
   α = opacity · exp(-½ dᵀ Σ'⁻¹ d)   d = offset from splat centre in pixels
   blend  ONE, ONE_MINUS_SRC_ALPHA   back-to-front, depth-write OFF
```

The whole algorithm is ~120 lines of GLSL. **The hard part is the sort**, not the maths.

### Why the sort is the hard part

Gaussians are transparent, so they must be drawn back-to-front, and the order changes every time the camera moves.

| Approach | Viable on GL 3.3? |
|---|---|
| GPU radix sort (compute shader) | ❌ needs GL 4.3 |
| Bitonic sort in fragment shader | ❌ ~200 passes for 1M — too slow (the `antimatter15` README says exactly this) |
| **CPU 16-bit counting/radix sort on a worker thread** | ✅ **this is the answer** — ~2-4ms for 1M splats, off the render thread, reuse last frame's order while in flight |

---

## 2. Industry standards, as of 2026

### Formats

| Format | Backer | Size (1M splats) | Use |
|---|---|---|---|
| **`.ply`** (binary_little_endian) | INRIA original, de-facto | ~240 MB | **Universal interchange. Every tool reads and writes it. Start here.** No formal spec exists — it is a community convention. |
| `.splat` | antimatter15 | ~32 MB | 32 bytes/splat, DC colour only, trivially parseable. Great *secondary* target. |
| `.spz` | Niantic | ~16 MB | Being standardised as the compression inside the Khronos glTF extension |
| `.sog` / SOGS | PlayCanvas | ~10 MB | Best compression + fastest load; image-based |
| **`KHR_gaussian_splatting`** in `.glb` | Khronos | — | **Ratification expected during 2026.** Once shipped, splats travel through normal glTF pipelines. This repo already vendors `external/cgltf` — future-proofed cheaply. |

### Tooling the industry actually uses

| Tool | Platform | CUDA? | Note |
|---|---|---|---|
| **Brush** (Arthur Brussee) | **macOS + Windows + Linux + web** | **No** — WebGPU | The one recommendation for a Mac user. Trains locally on Apple Silicon. |
| Postshot (Jawset) | Windows only | Yes | The commercial standard; fastest, best quality |
| Nerfstudio `splatfacto` / `gsplat` | Linux/Windows | Yes | The research standard; needs COLMAP for poses |
| LichtFeld Studio | Win/Linux | Yes | Open-source, actively developed |
| Luma AI / Polycam | Cloud + phone | — | Capture-side; phone → splat, no GPU needed |

### Where it has landed in production (2026)

Nuke 17 ships native splat support. Houdini 21 has a technical preview. OpenUSD 26.03 added a first-class schema. V-Ray 7 and OctaneRender 2026 raytrace splats. Unity's de-facto plugin is `aras-p/UnityGaussianSplatting`; Unreal's is Luma AI's official plugin. Framestore delivered ~40 final-pixel shots on *Superman* with 4D splatting.

**Read: this is no longer experimental. A splat node puts Infinite in the same room as Nuke and Houdini, and no other node-based *audiovisual* tool has one.**

---

## 3. Your question about "image → 3D quickly"

Two different things get conflated. Be precise about which one you saw:

```
┌─ single image → object splat ───────────────────────────────┐
│  Feed-forward nets: TriplaneGaussian, LGM, Splatter-Image,  │
│  VolSplat (ECCV'26), AnySplat, FreeSplatter                 │
│  ~1-5 seconds.  Object-level, hallucinated backsides.       │
│  ALL are PyTorch. None are embeddable in a C++ GL app.      │
└─────────────────────────────────────────────────────────────┘
┌─ photo/video set → scene splat ─────────────────────────────┐
│  COLMAP poses → 3DGS optimisation.  10-60 min, 8-24GB VRAM. │
│  Photoreal, full scene.  This is what production uses.      │
└─────────────────────────────────────────────────────────────┘
```

Neither belongs inside Infinite's process.

### So: is offline conversion the right call? **Yes, unambiguously.**

| | Embed a trainer | **Offline conversion (recommended)** |
|---|---|---|
| Dependency | PyTorch/CUDA, or a WebGPU trainer port | **none** |
| Apple Silicon | CUDA is unavailable — needs a separate MPS/WebGPU path | works, no special case |
| Binary size | +2 GB | +0 |
| Time in-app | minutes, blocking, uninterruptible | milliseconds (file load) |
| Fits Infinite's model | ✗ Infinite is a real-time instrument | ✓ same as `ModelSourceNode` loading a `.obj` |

Infinite becomes a **consumer and a performer** of splats, not a producer — exactly the relationship it already has with meshes (`ModelSourceNode`), images, and audio files. The user trains in Brush or Postshot, drops the `.ply` on the canvas, and then does the thing no other tool does: **patches it into a modulation graph.**

---

## 4. macOS / Windows parity

Clean, because the design deliberately uses nothing platform-specific.

| Component | Risk | Mitigation |
|---|---|---|
| Renderer | GL 3.3 + `#version 150`, identical both sides | Keep shaders at `#version 150`; no implicit int→float conversions (Windows/Mesa GLSL is stricter — this repo has already been bitten, see `src/main.cpp:54091`) |
| Sort | `std::thread` + `std::vector` | none |
| PLY parse | endianness, wide file paths | binary_little_endian only (universal for 3DGS); route paths through the existing `Platform::` shim, never `fopen` a `std::string` directly on Windows |
| Memory | 1M splats × 64 B texture = 64 MB VRAM | Cap + user-facing budget warning, mirroring the existing sprite fill-rate budget at `Geometry3DNodes.cpp:1826` |

No `_WIN32` should appear anywhere in the new code. That is the bar set by the `windows-parity` skill.

---

## 5. GitHub repos worth reading

**Read these two first — they are the actual blueprint:**

| Repo | Why |
|---|---|
| `antimatter15/splat` | **The single most important one.** WebGL2, no libraries, ~500 lines. CPU sort in a worker + data texture + instanced quads. This is the architecture to port, near-literally. |
| `aras-p/UnityGaussianSplatting` | Aras's blog series is the best written explanation of the *engineering* (packing, sorting, quality-vs-size tradeoffs) anywhere. |

| Repo | Purpose |
|---|---|
| `graphdeco-inria/gaussian-splatting` | The original paper's reference implementation — the ground truth for the maths |
| `MrNeRF/awesome-3D-gaussian-splatting` | The canonical curated index |
| `ArthurBrussee/brush` | Cross-platform trainer, Rust+WebGPU, no CUDA — the macOS answer |
| `nerfstudio-project/gsplat` | The clean, well-documented CUDA rasteriser; good for verifying maths |
| `playcanvas/supersplat` | Browser editor — cropping/cleanup before import |
| `mkkellogg/GaussianSplats3D` | three.js viewer; good reference for LOD and progressive loading |
| `kishimisu/Gaussian-Splatting-WebGL` | Alternative WebGL reference, heavily commented |
| `nianticlabs/spz` | The SPZ format, for phase 4 |

---

## 6. Proposed design

### Node shape

Two nodes, following the existing precedent exactly.

```
 ┌──────────────────┐
 │  Gaussian Splat  │  new: SplatSourceNode : INode, IGeometrySource
 │   [file: .ply]   │  category "3D"
 │  ◦ texture in    │
 └────────┬─────────┘
          │ geometry cable (existing)
          ▼
 ┌──────────────────┐
 │    Render 3D     │  extended: new drawSplatSlot() branch
 └────────┬─────────┘
          ▼  image cable → the whole existing 2D graph
```

**Why `IGeometrySource` and not a new cable type:** because the point-cloud path set the precedent. `GetPointCloud()` was added as an optional virtual returning `nullptr` by default (`Geometry3DNodes.h:196-200`), and `Render3D` branches on it. A splat cloud is the same move:

```cpp
// Geometry3DNodes.h, alongside GetPointCloud()
virtual const SplatCloud* GetSplatCloud() { return nullptr; }
virtual unsigned long long SplatCloudRevision() { return 0; }
```

Precedence inside `Render3D::drawSlot`, extending the existing "cloud wins over triangles" rule at `Geometry3DNodes.cpp:1964`:

```
  splat cloud  >  point cloud  >  mesh triangles
```

### The one real ordering constraint

Splats are transparent and must not write depth. So the splat pass **cannot** be interleaved with the opaque mesh pass:

```
 pass 1  opaque meshes        depth test ON,  depth write ON
 pass 2  splats, back-to-front depth test ON,  depth write OFF,
                               blend(ONE, ONE_MINUS_SRC_ALPHA)
 pass 3  existing transmission pass
```

This is a real change to `Render3DNode::CookIfNeeded` (`Geometry3DNodes.cpp:1374`) — slot iteration has to be split into two loops. Flag it early; it is the least mechanical part of the job.

### GPU data layout (GL 3.3, no SSBO)

```
 STATIC, uploaded once per file load
 ───────────────────────────────────
 splatTex : GL_RGBA32F, 2048 wide, ceil(N*4/2048) tall
   texel 4i+0   position.xyz            , opacity
   texel 4i+1   cov3d[0..2]  (xx,xy,xz) , —
   texel 4i+2   cov3d[3..5]  (yy,yz,zz) , —
   texel 4i+3   colour.rgb (SH DC)      , —
   → 64 bytes/splat. 1M splats = 64 MB VRAM.

 PER FRAME (only when the camera moved)
 ──────────────────────────────────────
 indexVbo : GL_R32UI, divisor 1, N entries = 4 MB for 1M
   ← the ONLY thing re-uploaded per frame. This is the whole trick.

 draw: glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, N)
       vertex shader does texelFetch(splatTex, index) — texelFetch is
       core in GLSL 1.30, so it is legal at #version 150.
```

### Sort

```
 camera moves ─► post job to worker thread (one, persistent)
                   │  16-bit key = view-space depth, quantised
                   │  counting sort, 65536 buckets, O(N)
                   ▼
                 double-buffered index array
                   │
 render thread ────┴─► uploads newest COMPLETE order; if a sort is
                        still in flight, reuses the previous order.
                        Never blocks. Never tears.
```

Skip the sort entirely when `dot(camForward, lastSortForward) > 0.9995` and the position delta is small — a static camera costs nothing.

### Node controls — 7, per the house limit

| Control | Type |
|---|---|
| file | file picker + drop target |
| point size | float knob, modulatable |
| opacity | float knob, modulatable |
| SH mode | dropdown: flat / view-dependent |
| tint | colour |
| crop | float knob (radius from centre — cheap way to kill floaters) |
| max splats | int (budget/LOD; sorts by opacity·scale, keeps the top K) |

Everything modulatable through `ParamRef` (`src/core/Modulation.h`) — **this is the part that makes it an Infinite node and not a viewer.** Audio-reactive splat size, LFO on crop radius, macro on opacity.

---

## 7. Build plan

Four branches, one per phase, per the repo's git workflow.

| Phase | Branch | Deliverable | Exit criterion |
|---|---|---|---|
| **1. Format** | `feature/splat-loader` | `SplatCloud` struct + `.ply` reader + `.splat` reader in `src/core/SplatIO.{h,cpp}` | Headless test loads a known file, asserts splat count, position bounds, and that opacity/scale were de-logit/de-log'd. **No GL, no node — fully unit-testable.** |
| **2. Render** | `feature/splat-render` | Data texture, EWA shader, CPU sort, `drawSplatSlot` in Render3D, two-pass split | A hardcoded 3-splat cloud renders as 3 correctly-oriented ellipses; a real scene renders recognisably; `run-infinite-hygiene` green |
| **3. Node** | `feature/splat-node` | `SplatSourceNode`, UI body, `VisitParams`, save/load, undo, `REGISTER_NODE` | Node round-trips through save/load; every param reachable by modulation; the 167-node-type sweep passes |
| **4. Polish** | `feature/splat-polish` | `.spz` read, budget/LOD, crop, image-cable tint, `KHR_gaussian_splatting` if ratified | — |
| **5. Field** | `feature/splat-field` | Optional — Field element kernel as a one-shot conditioning pass. **Gated on the two open items in §8.** | — |

**Phase 1 is completely independent of phases 2-4** and can be verified with no window open. Start there.

### Effort

| Phase | Rough |
|---|---|
| 1 | half a day |
| 2 | 2-3 days (the shader maths and the two-pass split are the real work) |
| 3 | 1 day |
| 4 | open-ended |

### The traps, named up front

1. **PLY property order is not fixed.** Parse the header and build an offset map. Do not assume `x,y,z,nx,ny,nz,f_dc_0,...`. Files from Postshot, Nerfstudio and INRIA differ.
2. **Opacity is a logit, scale is a log.** Forget the `sigmoid`/`exp` and you get an invisible or a universe-sized cloud. This is the #1 first-attempt bug.
3. **SH DC is not RGB.** `colour = 0.5 + 0.2820948 * f_dc`. (`0.2820948` = `SH_C0`.)
4. **Quaternion order.** INRIA PLY stores `rot_0..3` as **wxyz**, not xyzw.
5. **`#version 150` strictness on Windows.** No implicit `int`→`float`. Write `1.0` not `1`. This repo has shipped that bug before.
6. **MSAA + splats** is wasted fill rate — splats are already analytically antialiased by the Gaussian falloff. Consider skipping the resolve cost when a slot is splat-only.
7. **Keep `scale` and `rot` on the CPU struct** even though the renderer only
   ever reads `cov`. It costs 28 B/splat of RAM and nothing else, and it is the
   difference between a future Field pass being straightforward and it needing a
   per-splat 3×3 eigendecomposition. See §8.
8. **The `PassthroughSource()` forwarding trap** (`Geometry3DNodes.h:169`) — if a Transform node sits between the splat source and Render3D, `GetSplatCloud()` must be forwarded down the passthrough chain or the splats silently vanish. This exact class of bug has already happened in this repo with env-light cache invalidation.

---

## 8. Does Field belong anywhere in this?

Checked against the real implementation, not the design docs.

**Not in the hot path. It would make it dramatically slower.**

| Placement | Verdict | Reason |
|---|---|---|
| The sort | ❌ not expressible | A sort is a *permutation*. Field has no arrays and no scatter-write; `.at()` is a read only. |
| EWA projection | ❌ **de**optimisation | It has to run in the vertex shader. Field's element backend is a **CPU bytecode interpreter** — a scalar loop, one pass per element per cook (`src/core/field/ElementBackend.cpp:1685`). Routing 1M projections/frame through it moves them off the GPU entirely. |
| Per-frame element kernel over splats | ❌ | `FieldElementNode::CookIfNeeded` is guarded only by `frameId` (`src/nodes/FieldElementNode.cpp:347`) and re-gathers AoS→SoA every cook (`:457`). Element domain's design point is N≈5000; a splat cloud is ~1M. Two orders of magnitude past it. |
| Frame domain driving splat params | ✅ already free | 60 invocations/sec. This is just the modulation matrix — no new code, it falls out of registering the params through `ParamRef`. |
| **One-shot conditioning pass** | ✅ **the one worth building** | below |
| Pixel domain over the splat data texture | ⚠️ needs a language change | below |

### The placement that works

Every expensive splat operation a user actually wants is **edit-time, not
frame-time**: crop floaters, recolour, decimate to top-K, bend or warp a
capture. Those run once when a param changes, never 60×/sec.

```
 .ply ──▶ SplatCloud ──▶ [Field element kernel] ──▶ bake ──▶ static RGBA32F texture
                          cooks on change only                   (unchanged)
                          order 10-30 ms for 1M splats     only the 4 MB index
                          — fine once, fatal at 60 fps       buffer moves per frame
```

This preserves the entire performance design. The static texture stays static;
the per-frame upload stays the index buffer alone.

`P` and `Cd` map onto splat position and colour **exactly** — both are already
element-domain reserved names (`field-language` §5). That part needs no
language change at all.

### Two things it needs that do not exist today

> **OPEN — a cook-on-change mode for the element domain.** `FieldElementNode`
> re-cooks unconditionally every frame. A splat variant needs revision-stamp
> gating plus a budget warning modelled on the sprite fill-rate guard at
> `Geometry3DNodes.cpp:1826` — because patching an LFO into a Field-splat param
> would otherwise silently re-cook 1M elements per frame and kill the app with
> no visible cause.

> **OPEN — reserved names for splat scale / rotation / opacity.** The element
> domain has only `P N uv Cd i count`. A kernel that reshapes splats needs
> access to the ellipsoid. Options: **(a)** host-populated `attrib`
> declarations, no language change; **(b)** new reserved names in the element
> domain, which is a language-surface change and therefore falls under
> `field-language` §15 — owner's call, do not resolve it in code.

### The speculative one

The splat data texture is 2048 × ceil(4N/2048) RGBA32F — **it is already an
image**. A pixel-domain kernel over it compiles to GLSL, runs on the GPU in
~1 ms for 4M texels, and never round-trips to the CPU. That would give per-splat
manipulation at *frame* rate rather than edit rate.

The blocker is semantic, not technical: the pixel domain's reserved names are
`uv xy col res aspect alpha age`, so a kernel would be writing `col` to mean
"position". That is a new domain or a domain variant — squarely an owner
decision, and explicitly not something a session should invent.
