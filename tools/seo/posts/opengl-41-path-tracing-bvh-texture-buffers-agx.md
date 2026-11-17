---
title: Real-time OpenGL 4.1 path tracing with BVH texture buffers and AgX
seo_title: OpenGL 4.1 Path Tracing & AgX | Infinite
description: How Infinite's Render 3D node runs a progressive OpenGL 4.1 path tracer using RGBA32F BVH texture buffers, 12 ms strip budgets, and shared AgX tonemapping.
date: 2026-11-17
category: creative
tags: [path tracing, opengl, agx, 3d rendering, shaders]
keywords: opengl 4.1 path tracing, bvh samplerbuffer glsl, agx tonemapping glsl, progressive strip path tracer, real-time 3d rendering
image: assets/blog_opengl-41-path-tracing-bvh-texture-buffers-agx.png
image_alt: Technical architecture diagram of Infinite Render 3D showing scene inputs, PtSameView raster-to-path-trace gate, seven RGBA32F samplerBuffer BVH textures, 12 ms StepFrame strip budget, and shared AgX display tonemapping
status: published
---

Real-time OpenGL 4.1 path tracing lets a modular 3D scene refine into physically based light transport without requiring compute shaders, hardware ray-tracing cores, or an external offline renderer. Recent graphics engineering threads on Hacker News and open-source rendering repositories, from pushing Three.js lighting pipelines toward studio quality to multi-target shader toolkits like LYGIA and TypeGPU and experimental audiovisual film adapters, highlight how much effort goes into closing the gap between fast interactive viewports and physically plausible shading.

In [Infinite](https://infinitedevices.org/), the **Render 3D** node (`Render3DNode`) bridges both workflows inside a single patch graph. While you orbit a `Camera`, modulate a `Light`, or animate geometry, forward rasterisation keeps the viewport responsive. The moment the view holds still, `Render3DPathTrace` takes over and progressively accumulates Monte Carlo samples using `GL_TEXTURE_BUFFER` arrays and the same display colour pipeline as the raster pass.

![Technical architecture diagram of Infinite Render 3D showing scene inputs, PtSameView raster-to-path-trace gate, seven RGBA32F samplerBuffer BVH textures, 12 ms StepFrame strip budget, and shared AgX display tonemapping](https://infinitedevices.org/assets/blog_opengl-41-path-tracing-bvh-texture-buffers-agx.png)

## Why OpenGL 4.1 still constrains portable engines

Cross-platform creative tools that target macOS alongside Windows and Linux hit a well-known graphics API ceiling: Apple froze desktop OpenGL at version 4.1 core, which omits compute shaders and Shader Storage Buffer Objects (`SSBO`). Many path-tracing tutorials assume Vulkan, OptiX, or OpenGL 4.3 compute kernels, leaving OpenGL 4.1 engines with either a separate Metal backend or raster-only approximations.

`Render3DPathTrace` sidesteps that split by running its entire ray-traversal and shading loop inside a standard OpenGL 4.1 / 3.3 core fragment shader. Every acceleration structure, triangle buffer, and material table lives in `RGBA32F` buffer textures (`GL_TEXTURE_BUFFER` bound to `samplerBuffer` uniforms) and is read via `texelFetch`. The same C++ and GLSL code path compiles and runs across macOS, Windows, and Linux without maintaining duplicate compute backends.

## Flattening a two-level BVH into samplerBuffer textures

When geometry changes, `Render3DPathTrace::UploadScene()` builds bottom-level acceleration structures (`Bvh::Blas`) for each unique mesh and a top-level acceleration structure (`Bvh::Tlas`) over all placed instances, then packs the scene into seven `RGBA32F` buffer textures:

1. **`uTlasNodes` and `uTlasInstances`**: Store the top-level bounding-volume hierarchy and per-instance inverse transforms plus material IDs.
2. **`uBlasNodes`, `uBlasTris`, `uBlasNormals`, and `uBlasUvs`**: Hold the per-mesh BVH tree, triangle vertex positions, smooth normals, and two texels per triangle for UV coordinates.
3. **`uMaterials`**: Encodes linear-light `PtMaterial` records covering base colour, metallic, roughness, emission, index of refraction (`ior`), dielectric specular level, transmission, clearcoat, sheen, opacity, and alpha cutoff.

To stay well within fragment-shader texture-unit limits, material maps are not bound as dozens of separate samplers. Instead, `BuildAtlas()` packs every active `Material` texture across up to nine map slots (`albedo`, `roughness`, `metallic`, `normal`, `emission`, `clearcoat`, `sheen`, `transmission`, and `opacity`) into a single 2D atlas (`mAtlas`), passing normalised UV rectangles inside `uMaterials`.

## Keeping a 12 ms frame budget with adaptive strip tracing

A full-screen Monte Carlo sample on a complex mesh can easily exceed a 16.6 ms display interval on integrated GPUs or an 8 GB Apple Silicon laptop. Blocking the main thread would freeze node dragging, timeline playback, and audio-reactive modulation.

`Render3DNode` prevents stalls with two mechanisms in `Render3DPathTraceCook.cpp` and `Render3DPathTrace::StepFrame()`:

- **Motion gating (`PtSameGeometry` and `PtSameView`)**: While the camera, lights, environment, rig, or animated geometry change, `Render 3D` stays on forward rasterisation. Once `PtSameView` confirms the scene is still, accumulation resets and progressive tracing begins.
- **Adaptive horizontal strips**: `StepFrame()` targets `frameBudgetMs = 12.0f` and caps live refinement at `sampleBudgetPerFrame = 4`. It traces horizontal strips of `mStripRows` height, calls `glFinish()`, measures `rowsPerMs`, and scales the next strip height to `rowsPerMs * frameBudgetMs * 0.8` (clamped between `8` rows and full height). Until the first full sample completes, `Render 3D` blits only `CoveredRows()` over the existing raster frame, so the viewport never flashes black.

Offline renders bypass the 12 ms cap via `RenderToSpp()`, tracing straight up to `ptSamples` (clamped between `1` and `4096`) with `maxBounces` clamped between `1` and `16` (default `4`).

## Physically based shading, NEE, and thin-lens bokeh

Inside the fragment shader, each bounce evaluates the full `PtMaterial` surface model:

- **Opaque and glass lobes**: GGX microfacet specular and cosine-weighted hemisphere diffuse sit alongside Walter et al. dielectric transmission (`ior`, `transmission`, `transmissionRoughness`) with exact Fresnel reflection/refraction and clearcoat support.
- **Next-Event Estimation (NEE) and MIS**: At each hit, up to eight scene lights (directional, point, sun, ambient, spot, and rectangular area lights) are sampled directly, with Multiple Importance Sampling (`wMis`) weighting area-light samples against BSDF direction sampling.
- **See-through alpha and Russian roulette**: Alpha-blended or alpha-cutout surfaces pass through up to 16 layers without consuming a bounce, and Russian roulette terminates low-throughput paths from bounce 2 onward. Any `NaN` or `Inf` radiance sample is zeroed before hitting the ping-pong `RGBA32F` accumulation buffers (`mAccum[2]`).
- **Real camera optics**: `SetLens()` converts `dofAperture`, `focusDistance`, `dofBlades`, `dofRotation`, and `dofRatio` into physical thin-lens sampling for polygonal bokeh, matching the focus plane of the raster depth-of-field pass.

You can wire procedural meshes from [Infinite's 3D nodes](https://infinitedevices.org/library.html) such as `L-System` or `Field Primitive` directly into `Render 3D`, light them with an `Environment` HDRI or sky gradient, and drive any light or material parameter from audio analysis before exporting stills or video.

## Shared AgX and Khronos PBR Neutral display resolve

Switching from raster motion to path-traced stillness breaks visual continuity if the two renderers use different tone curves. In Infinite, both the forward raster shaders and `Render3DPathTrace::ResolveDisplay()` include `R3D_TONEMAP_GLSL` (`Render3DTonemap.h`).

That header implements Troy Sobotka's `AgX` (`agxTone`, using inset/outset 3x3 matrices, log2 encoding over `-12.47393` to `+4.02607` stops, and a 6th-order polynomial contrast curve) alongside `Khronos PBR Neutral` (`neutralTone`), `ACES`, `Reinhard`, and `None`, followed by IEC 61966-2-1 sRGB transfer encoding. Saturated emissive lights and bright specular highlights roll off identically whether you are scrubbing a parameter live or letting a frame converge.

## Honest limits of the current architecture

- **Raster-only point clouds**: GPU point clouds and particle sprites (`GetGpuPoints()`, `GetPointCloud()`) are skipped by `UploadScene()` and remain raster-only.
- **Animated scenes stay raster**: Because `PtSameView` resets accumulation whenever geometry, lights, or cameras move, fast audio-reactive animations render with the forward raster pipeline rather than accumulating multi-bounce path-traced frames per tick.
- **No hardware RT cores**: OpenGL 4.1 fragment-shader BVH traversal is portable across macOS, Windows, and Linux, but high-polygon meshes trace more slowly than dedicated hardware ray-tracing pipelines.

If you want to inspect how Infinite keeps 3D graphics and audio synthesis decoupled across threads and processes, read our notes on [out-of-process VST3 and AU plugin IPC](https://infinitedevices.org/blog/out-of-process-vst3-au-plugin-shared-memory-ipc.html), or explore the full architecture on the [Infinite developers page](https://infinitedevices.org/developers.html).
