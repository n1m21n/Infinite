# Research brief: professional 3D in Infinite

Paste everything below the line into the research model. Attach the repo's node list
(`Infinite --describe --json`) and `docs/reference/patch-format.md` if it accepts files.

---

## Who you are writing for

A coding agent will implement your findings straight away. It needs the maths, the exact
algorithm and the reason for each choice, not a tutorial. Where you give a formula, give it
fully (symbols defined, units, ranges). Where you name an algorithm, name the paper and
the file/function in Blender's source that implements it.

## The product

Infinite is a live, node-based audio + visual app (C++). Users and AI agents build patches
by wiring nodes; everything can be driven by sound, time or a knob, live at 60 fps.

- Graphics: OpenGL 4.1 core on macOS (no compute shaders, no GL ray tracing), the same
  code on Windows and Linux. A Metal backend for macOS is allowed if it pays for itself.
- Target machine: Apple M2, 8 GB unified memory. It must also run on weak Windows/Linux
  GPUs.
- Audio shares the machine. The audio thread is realtime; per-pixel work goes on the GPU;
  branchy per-frame geometry may use the CPU within a ms budget; edit-time jobs (bakes,
  imports, unwraps, remesh) run in the background on efficiency cores.
- 3D today (about 100 nodes): primitives, mesh ops (bevel, extrude, inset, boolean
  union/difference/intersect, subdivide, smooth, decimate, solidify, screw, mirror,
  array, twist, wireframe, triangulate, merge by distance), selection ops, curves, sweep,
  revolve, L-system, metaballs, Voronoi, Delaunay, sculpt, Material (PBR: albedo,
  roughness, metallic, normal, AO, emission, clearcoat, sheen), Mapping, Bake, Displacement,
  HDRI, Light (3 per render), Camera, Render 3D (shadow maps, SSAO, AgX tonemap), cloth,
  rigid bodies, particles, ocean, Model 3D import, point clouds, Depth Projection.
- Missing: global illumination, path tracing, rigging/skinning, UV unwrap and most of
  texture painting, volumes, hair, physical camera (DOF, motion blur, exposure in EV),
  render passes/AOVs.

## The goal

Let a user or an agent make renders as realistic as Blender Cycles, with the tools a
professional uses, but with a different approach: a raster image while editing that
converges to a path-traced image when idle, and every control playable live. Simpler than
Blender: good defaults and few controls up front, depth when asked for.

Where Blender is not state of the art, say so and give the better method (papers from
2018 to 2026, production renderers, game engines).

## Areas to research

Cover every area below. For each one, deliver the same seven parts:

1. **What professionals use, ranked.** The features artists actually use most for
   realistic work, most-used first, with a source for the ranking (studio pipelines,
   Blender/Houdini/Maya docs, surveys, course syllabi, ArtStation breakdowns).
2. **Maths.** The formulas behind each top feature, complete.
3. **Blender's implementation.** File paths and function names in the Blender source
   (`intern/cycles/...`, `source/blender/...`), what it does, its known weaknesses.
4. **Better than Blender.** The newer or better method, if one exists, with the paper.
5. **Fit for Infinite.** GPU (GL 4.1 fragment shader, or Metal) or CPU class (per-frame
   within budget, or background job); data layout; expected cost in ms or seconds on
   an M2 for a typical scene; what works live and what only offline.
6. **User side.** How to show it simply: defaults, presets, one-click looks, which three
   controls come first. Show how KeyShot, Spline, Womp, Unreal, Houdini, Substance or
   ZBrush hide the complexity where relevant.
7. **Proof.** How to prove our version matches: a Blender A/B scene (same inputs in
   headless Blender and Infinite), the metric (FLIP, PSNR, curvature error, volume
   error, deformation error) and the pass threshold.

### A. Layout and scene assembly
Transforms and pivots, snapping, instancing at scale, collections/hierarchies, scene
units and real-world scale, reference images, cameras and shot framing, asset libraries
and drag-in scenes, level of detail.

### B. Modelling
Polygon modelling (bevel with profiles, loop cuts, edge slide, knife, bridge, fill,
boolean robustness: exact vs fast, Blender's mesh arrangement solver), subdivision
surfaces (Catmull-Clark, creases, OpenSubdiv on GPU, adaptive), remeshing (voxel,
quad remesh / Instant Meshes / QuadriFlow), curves and NURBS, procedural geometry
nodes (what Geometry Nodes users reach for most), hard-surface workflows (weighted
normals, chamfers, shading smoothness).

### C. Sculpting
Brush maths (draw, clay strips, crease, grab, snake hook, smooth, inflate, pinch, layer,
pose, cloth brush), falloff curves, stroke spacing and lazy mouse, dyntopo vs multires
vs voxel remesh, mesh filters, masks and face sets, symmetry, the data structures that
keep sculpting fast at millions of polygons (Blender's PBVH, ZBrush's approach), and
GPU-side sculpting where it beats the CPU.

### D. UVs and textures
Unwrapping (LSCM, ABF++, SLIM, seams, packing), texel density, triplanar and box
mapping, procedural textures (noise, Voronoi, musgrave/fractal, wave, gradients: exact
formulas as Blender implements them), texture painting and projection painting, baking
(normal, AO, curvature, thickness, lighting) and the maths of tangent space (MikkTSpace),
UDIMs, texture memory budgets on 8 GB.

### E. Materials (PBR)
The Principled BSDF in full: diffuse model, GGX/Trowbridge-Reitz with multiple-scattering
energy compensation, Fresnel (Schlick vs conductor with complex IOR, F82 tint), metallic
workflow, specular tint, anisotropy, clearcoat, sheen (Zeltner LTC sheen), subsurface
scattering (random walk vs Christensen-Burley), transmission and thin-film iridescence,
emission. OpenPBR and MaterialX as the reference standard. Split-sum / prefiltered
environment lighting for the raster path, so raster and path tracer match.
Layered materials and how Substance artists build them.

### F. Lighting
Light types and units (watts, lumens, lux, nits, candela; physical sky like Nishita/
Hosek-Wilkie), soft area lights in raster (LTC area lights), shadows (cascaded, virtual
shadow maps, PCSS, ray-traced), IES profiles, HDRI handling (importance sampling, sun
extraction), global illumination options for raster (SSGI, irradiance probes / DDGI,
voxel cone tracing, Lumen's approach, Eevee Next's ray tracing), light linking,
three-point and product lighting setups professionals start from.

### G. Ray tracing and path tracing (the deciding question; go deepest here)
A progressive path tracer that converges while the user is idle and stays interactive
while they edit. Should it be:

- A) GL 4.1 fragment-shader passes (BVH packed in textures), or
- B) a Metal backend (compute plus Metal ray tracing, hardware-accelerated on newer Apple
  Silicon, software on M2), sharing textures with the GL app?

For each: BVH layout and build/refit cost (SAH, binned SAH, LBVH, CWBVH, TLAS/BLAS for
instancing and animated meshes); expected ms per sample at 1080p and 540p on M2;
sampling (MIS, next-event estimation, Cycles' light tree, ReSTIR DI/GI, path guiding,
blue-noise / Sobol); temporal reuse while the camera or a knob moves; denoisers (SVGF,
A-SVGF, OIDN on GPU or Neural Engine, Apple MetalFX denoiser) and their cost; GL-Metal
interop through IOSurface (cost, sync, pitfalls); what Windows and Linux need for each
option (Vulkan RT, GL fallback). Cite working open source: three-gpu-pathtracer,
Falcor, Mitsuba 3, pbrt-v4, Apple's Metal ray tracing samples, Cycles' Metal backend.
End this area with one recommendation and a build order.

### H. Camera and finishing
Physical camera (focal length, sensor, f-stop, shutter, ISO; exposure value maths),
depth of field (bokeh shape, near/far field in raster vs path-traced), motion blur,
lens effects that read as real (vignette, chromatic aberration, distortion, glare and
bloom from a physical kernel), tonemapping (AgX, Filmic, ACES, Khronos PBR Neutral),
colour management (OCIO, scene-linear, display transforms), film grain, render passes
and AOVs, compositing professionals do after the render.

### I. Rigging and animation
Armatures and bone maths, linear blend skinning vs dual-quaternion skinning (and newer:
optimised centres of rotation, delta mush), automatic weights (Blender's heat diffusion,
Baran and Popovic; newer: bounded biharmonic weights, robust inpainting), IK solvers
(CCD, FABRIK, Jacobian / damped least squares, Blender's iTaSC), constraints, shape keys
and drivers, Rigify-style auto rigs, retargeting mocap, GPU skinning in a vertex shader.
How to make rigging playable by sound and knobs.

### J. Simulation and effects (brief)
Cloth (XPBD vs Blender's mass-spring), rigid bodies, particles, hair/fur (strand
rendering and shading: Marschner, Chiang), volumes (smoke, fog, clouds; ray marching,
OpenVDB/NanoVDB), fluids. Only the parts that matter for still and animated realism.

## Output format

1. One section per area A to J, each with the seven parts above. Use tables for rankings
   and comparisons.
2. A master table across all areas: feature, area, pro usage rank, realism impact
   (1-5), cost to build (days), GPU/CPU class, depends on.
3. A recommended build order for the whole programme, in blocks of about two weeks, with
   path tracing and GI placed where they unlock the most.
4. A source list: papers (with year), Blender source paths, official docs, open-source
   repos. Mark anything you are not sure of as unverified.

Rules: give formulas and algorithms, not long code. Blender is GPL; describe what its
code does, do not paste it. Prefer numbers to adjectives.
