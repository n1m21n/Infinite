# index-scan: execution plan for Infinite

Companion to `SUMMARY.md`. Turns the ten ranked candidates into waves of work, each mapped to the code area, owning skills and
the gate that proves it. Nothing here is started; each item waits for the owner's approve / change / abandon on `SUMMARY.md`.

Evidence level: file:line cites come from the cluster notes, written by agents that grepped `src/` at scan time. Re-grep
each symbol (codebase-navigation: anchors are symbols, not line numbers) before editing. Cluster notes were not
cross-checked against the indexopensource.com list (blocked), so a re-run with the index reachable may add candidates.

## Rules that apply to every item
- Branch per item (`git-branch-workflow`): `feature/<slug>` off `main`, commit per step, explicit `git add`. No work on `main`.
- Start each item with `triage`, then `infinite-planner`; finish with `verify-gate` (advisory) before merge.
- Clean room: ideas only. Near-ports of MIT/ISC/Apache code need the attribution noted in the cluster file; BSL/Apache-2.0
  items vendored as libraries follow the `external/<name>/` pattern (codebase-navigation "Vendoring pattern").
- Three platforms: any `Platform::` touch needs macOS, Windows and Linux sides (`windows-parity`, `linux-parity`).
- Field work uses bare names, no sigils (`field-language`).
- Every new param: `ParamRef` + `VisitParams` save/load + undo + modulation question (`node-param-audit`, `node-ui-pillars`).
- Save-compat: changing an existing node's output (Noise, Smooth, Blur, ColorRamp, Cloth) must not move saved patches.
  Add new modes behind a new enum value, defaulting old patches to the old behaviour, or version the param.

## Waves

### Wave 0: decisions and tiny fixes (no new nodes)
| # | Item | Area | Skill | Effort | Gate |
|---|---|---|---|---|---|
| 0a | Fix stale help text claiming particle collision/spring-mass | `src/app/panels/HelpWindows.cpp` (~:824) | `release-notes-audit` mindset, `node-param-audit` | S | read the text against `SimulationNodes.cpp` |
| 0b | Field error-span highlighting (span data exists, UI shows flat string) | `StageSidePanels.cpp` (~:808-835), `FieldError.span` | `field-integration`, `node-ui-pillars` | S | Field editor test; light + dark contrast |
| 0c | Owner decision on the ten candidates | `SUMMARY.md` | n/a | n/a | approve / change / abandon recorded |

### Wave 1: self-contained, high value, low risk (parallel, independent branches)
**1. Time-based Smooth + Spring mode + easing table** [easing-springs-ik] - S-M
- Area: `src/nodes/ModulatorNodes.cpp` (`SmoothNode` ~:226), `ModulatorNodes.h`, `src/core/CurveShape.h` (easing table for ModCurve/Pattern/Envelope).
- Skills: `new-modulator-node`, `modulation-sweep`, `param-truth-audit`, `rhythmic-quantization-standard` (if tempo-synced).
- Steps: (a) convert one-pole to a time-constant form while keeping a "legacy per-tick" mode for old patches; (b) add Spring mode (halflife/damping), velocity as a second output only if multi-output rules allow (`Value01()` idempotency); (c) add the named easing set as a shared table, not per-node copies; (d) check Field backends for an entry per easing function (left open in the note).
- Risks: frame-rate dependence is the reason to fix it, so the legacy mode must be explicit. Fan-out jitter if state advances in a getter.
- Gate: `modulation-sweep`, ROUNDTRIPTEST of Smooth, fixture at 30 vs 144 fps giving the same response time.

**2. Dither node + blur tap fix** [image-filters] - S
- Area: `src/core/FilterDefs.cpp` (blur ~:72-125, ~:412) and `FilterDefs.h`; Dither is a FilterDef table row.
- Skills: `new-effect-node`, `compositing-pipeline-sweep`, `data-accuracy-sweep`, `node-ui-pillars` (mix slot bottom-right).
- Steps: add Dither (Bayer 2/4/8 + blue-noise texture, levels, mix) as a FilterDef; separately switch blur to linear-sample taps behind a quality param so old looks stay.
- Risks: blue-noise needs a small baked texture or an integer-hash generator; bypass must pass through; idle-frame caching trap (new-effect-node "caching trap").
- Gate: compositing sweep, a golden image per Bayer size, bypass leak check.

**3. Oklab ColorRamp interpolation + palette gamut mapping** [colour] - S-M
- Area: `src/nodes/ColorRampNode.cpp` (~:125, raw RGB lerp), `PaletteNode.cpp` (`LabToDisplay` ~:65-71 hard clip), optional shared `ColorSpace.h`.
- Skills: `new-effect-node`, `data-accuracy-sweep`, `param-truth-audit`; sRGB convention note in `codebase-navigation` (plain `GL_RGBA8`, in-shader `toLinear()`).
- Steps: add interpolation-space enum (RGB default for old patches, Oklab, OKLCh with hue-arc); replace per-channel clip with chroma reduction at fixed L/h.
- Risks: three sRGB conventions already coexist (exact piecewise, gamma 2.2, none); do not "fix" them silently, since it shifts saved looks. Unify in a separate, reviewed step.
- Gate: ramp endpoints identical in RGB mode; gamut-map unit fixture on out-of-gamut swatches.

### Wave 2: core capability nodes (geometry and noise)
**4. Gradient Perlin + Simplex in NoiseNode** [noise-random-hash] - M
- Area: `src/nodes/NoiseNode.cpp:10-100`, `NoiseNode.h:30-45`; later Field `FieldRandom.cpp`, `FieldIR.cpp:3844-3855`.
- Skills: `new-source-node`, `field-language`, `field-testing` (corpus), `data-accuracy-sweep`.
- Steps: new noise `type` enum values (existing value-noise stays default); shared integer-hash GLSL preamble is a separate follow-up (touches ~8 shader copies, saved-look regression risk, so do it last and behind golden images).
- Gate: deterministic across GPUs (integer hash), TextureRevision stays honest, old patches render identically.

**5. Delaunay Mesh + Voronoi Cells nodes** [triangulation-voronoi-hulls-spatial] - M each, Delaunay first
- Area: new `src/core/` Delaunay routine (Delaunator-style sweep, halfedge output), nodes beside `PointDistributionNodes.h` / `GeometryOpNodes.h`, registered in `src/app/graph/NodeRegistry.cpp` (~:95).
- Skills: `new-geometry-node`, `geometry-transform-sweep`, `cable-logic-sweep`, `render-pipeline-sweep`, `codebase-navigation` (four cable chains in main.cpp).
- Steps: Delaunay Mesh (point cloud in, mesh out; consume `GetPointCloud()`); Voronoi Cells from circumcentres (shatter pieces / cell wireframes). Forward Material and mapping per the passthrough-field trap.
- Risks: degenerate/collinear input; point-cloud/curve passthrough gap in wrappers (`codebase-navigation` map entry); revision stamps for cached meshes.
- Attribution: ISC; add notice only if structure is near-ported.
- Later S-M: Earcut z-order hashing for Text3D `EarClip`/`MergeHoles` (`Mesh.cpp:1201`, `:1298`).

**6. Simplify/Decimate (QEM edge collapse)** [mesh-ops] - M
- Area: `src/core/Mesh.cpp` (MeshOps), `GeometryOpNodes.h`, node registration.
- Skills: `new-geometry-node`, `geometry-transform-sweep`, `render-pipeline-sweep`.
- Steps: border-locking, flip rejection, target ratio and error params; preserve UVs/vertex colour/material. Own implementation (meshoptimizer is idea only).
- Gate: watertight in = watertight out; Instances/group matrices preserved; no invented vertex colour.
- Defer: robust winding-based Boolean (L) and SDF level-set mesher (L) until a decision on replacing BSP CSG (`Mesh.cpp:4532`, `:4822`) and the metaballs 96^3 cap (`:4036`).

**7. Curve Offset/Stroke + Curve Ops** [curves-splines-strokes] - M
- Area: curve nodes in `GeometryOpNodes.cpp` (~:567, Transform only today), `Mesh.cpp` polyline code, `SamplePolyline` (cached cumulative length is a separate S fix).
- Skills: `new-geometry-node`, `geometry-transform-sweep`, `cable-logic-sweep`.
- Steps: Curve Ops first (resample, simplify, smooth: textbook, no dependency); Curve Offset using Clipper2's join/arc-tolerance model reimplemented, or vendor Clipper2 under `external/clipper2/` (BSL-1.0, header-friendly) if 2D boolean is also wanted.
- Risks: `Polyline` is a single strip, so branching/multi-contour output is a data-structure decision (see also L-system).

### Wave 3: simulation and generative (needs design calls first)
**8. Cloth XPBD compliance + substeps; particle curl/collision** [physics-particles-fluids] - S-M
- Area: `src/nodes/SimulationNodes.h/.cpp` (ClothNode is PBD with ground plane only; ParticleSystemNode noise is not curl).
- Skills: `new-geometry-node`, `render-pipeline-sweep`, `rate-analysis-sweep`, `param-truth-audit`.
- Steps: compliance independent of tempo/framerate, substeps param, then bend/pressure/tether constraints (M), curl noise and plane collision in particles. 2D stable-fluids node (M-L) is a separate new node.
- Gate: stiffness identical at 30/60/144 fps; no per-frame alloc; no regression in saved cloth patches (new params default to old values).

**9. Optical Flow node** [cv-ml] - M
- Area: new source/effect node using GLSL Horn-Schunck; flow texture + motion modulators. Prerequisite for ML nodes: refactor `SubjectMaskOnnx.cpp` (single-model singleton) to an `OrtModel` (M, no user-visible change). ORT already vendored (`CMakeLists.txt:123-195`, `:946-955`).
- Skills: `new-effect-node` or `new-compositing-node` (frame-persistent), `compositing-pipeline-sweep`, `rate-analysis-sweep`.
- Risks: needs previous-frame texture (ping-pong cooking rule); depth/hand models have licence limits (Depth-Anything-V2 Base/Large are CC-BY-NC, only Small is Apache-2.0); abandoned `origin/motion-track-node` has Shi-Tomasi/LK/NCC code if revived.

**10. Cellular Automaton node + Life/Lenia presets** [generative-rules] - S-M
- Area: Field Pixel presets first (`FieldPixelNode.cpp::Presets()`, S, no new node), then a CA node beside `ReactionDiffusionNode` (`FeedbackNodes.cpp:179`).
- Skills: `field-pixel-presets`, `field-state` (state cells, ping-pong), `field-realtime`, `new-compositing-node`.
- Risks: Lenia kernel radius vs Field's offset-read cap is unverified; WFC/MarkovJunior need a worker thread and Infinite has no non-audio async precedent, so they stay out of this wave.

## Suggested order and dependencies
```
0 -> 1.1, 1.2, 1.3 (parallel)
      -> 2.4 Noise, 2.5 Delaunay -> Voronoi, 2.6 Decimate, 2.7 Curve Ops -> Curve Offset
           -> 3.8 Cloth/Particles, 3.9 OrtModel -> Optical Flow, 3.10 CA presets -> CA node
```
- 1.3 before any colour-space unification; 4's shared hash preamble last.
- 2.5 Voronoi depends on 2.5 Delaunay; 2.7 Offset after Curve Ops.
- 3.9 OrtModel refactor precedes any depth/hand/segment node.

## Per-item definition of done
1. Lens map filled (`codebase-lenses` Mode A, seven rows) and handed to `infinite-planner`.
2. New node registered in the node-type table and drop/extension lists if file-based; cable chains updated.
3. Save/load round-trip, undo, copy/paste, bypass, delete-mid-playback pass; ROUNDTRIPTEST covers it.
4. Node body follows `node-ui-pillars` (light + dark).
5. `verify-gate` run; sweeps named above pass; `run-infinite-hygiene` before commit.
6. Windows/Linux sides exist for anything touching `Platform::`.
7. `codebase-navigation` living map updated if a new wiring hotspot was found.

## Not planned (deferred or dropped)
IK/boids (no peer read), WFC / MarkovJunior (worker thread needed), analog drum voices and auto-wah [audio-dsp] (candidates for an audio wave once Wave 1 lands), sky bake and ocean spectrum [layout-renderers], marker ripple [tool-design], robust Boolean and SDF mesher (L).
