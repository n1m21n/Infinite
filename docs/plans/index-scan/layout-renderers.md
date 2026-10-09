# Index scan: layout/packing and renderers

NOTE: indexopensource.com is blocked in this environment, so these candidates were NOT cross-checked against the index list. They come from my own knowledge of well-known repos. Only raw.githubusercontent.com was reachable.

## Projects read (licence verified from LICENSE via raw)
- mapbox/potpack @ main: ISC. Read index.js in full (117 lines).
- d3/d3-hierarchy @ main: ISC (Mike Bostock). Read src/pack/siblings.js (front-chain circle packing). Also fetched src/treemap/squarify.js but did not study it.
- d3/d3-force @ main: ISC. Fetched src/manyBody.js (quadtree Barnes-Hut). Only its header was inspected, so it is not a source of claims below.
- sebh/UnrealEngineSkyAtmosphere @ master: MIT (Epic Games, 2020). Read Resources/RenderSkyRayMarching.hlsl: function list (IntegrateScatteredLuminance, NewMultiScattCS, RenderTransmittanceLutPS, SkyViewLutPS, AerialPerspective slices) and structure, not every line.
- 2Retr0/GodotOceanWaves @ main: MIT. Read assets/shaders/compute/spectrum_compute.glsl in full (JONSWAP/TMA spectrum, Hasselmann spread).
- ebruneton/precomputed_atmospheric_scattering @ master: BSD-3-Clause (verified from the LICENSE text). Licence OK, source not read (UE sky covers the same ground in a simpler form).

## Skipped
- Path tracers (Cycles, Mitsuba 3 [BSD but offline], smallpt, LuxCore): not relevant to a realtime GL 3.3 compositor. Render 3D is a raster PBR pipeline, so a path tracer would be a separate engine (L effort, no user value over the existing raster path). Skipped as irrelevant. smallpt LICENSE fetch also failed (404) so it is unverifiable.
- Volumetrics (Blender EEVEE, Unreal volumetric fog): the Blender/UE engine sources are GPL or custom licences. Nothing permissive and small was read. The UE sky repo's aerial-perspective froxel volume is the relevant permissive reference and is covered below.
- Force-directed layout (d3-force): Infinite has no graph-data node to lay out, so there is no consumer. Not pursued. The PBD solver in src/nodes/SimulationNodes.h:120-123 already covers spring-like constraints for particles and cloth.
- Text layout (HarfBuzz is MIT-ish, Pango is LGPL): TextNode already has wordWrap (src/nodes/TextNode.h:42). I did not investigate shaping, so there is nothing grounded to propose. Skipped.
- GPL/AGPL: nothing opened.

## What Infinite does today
- Packing/layout: grep of src/ (*.h, *.cpp, excluding audio) for "bin pack|shelf|skyline|squarif|force.directed|barnes|quadtree|rect pack" found nothing relevant (hits were EQ shelf filters only). Registered 3D layout-ish nodes are only Distribute Points in Grid / on Faces, Merge by Distance (src/app/graph/NodeRegistry.cpp:95-98). A grep for Poisson/Lloyd/Voronoi/relax in src/nodes and src/core headers found nothing. So no circle-packing, rect-packing, treemap or blue-noise node exists.
- Sky: Render 3D has a built-in 3-colour gradient sky/horizon/ground (uEnvSky/uEnvHorizon/uEnvGround, src/nodes/Geometry3DNodes.cpp:225-226, 275-276, 497-499) and an HDRI node (src/nodes/EnvironmentNode.h:27-91, registered NodeRegistry.cpp:103) that uses averaged mips as a fake irradiance and glossy reflection (EnvironmentNode.h:20-26, which admits it is not split-sum IBL). A grep for "rayleigh|scatter|Hosek|Preetham" in src found no physical sky. A Field Pixel preset draws a stylised sunset (src/nodes/FieldPixelNode.cpp:494-527), 2D only.
- Water: OceanNode (src/nodes/OceanNode.h:1-50) builds a mesh on the CPU from summed Gerstner waves, rebuilt in MeshOps::Ocean (src/core/Mesh.cpp:1517-1600). It is closed-form per vertex, with no state and a 400 resolution cap (Mesh.cpp:1520). Deep-water dispersion is already used (Mesh.cpp:1544). Octaves are golden-angle rotated (Mesh.cpp:1535). There is no FFT or spectrum: grep for "fft" in nodes/core hit only audio and synth files, none for water.
- Fog/volumetrics: the only fog is the Field Pixel preset (FieldPixelNode.cpp:494). Render 3D has shadow maps (Geometry3DNodes.cpp:31-69, 444-469) but no fog, god-ray or volumetric term (grep "fog|volumetric|godray" in src/nodes found only that preset).

## What the peers do differently
- potpack/index.js: skyline-less "space list" shelf pack. Sorts by height desc, starts with a width of sqrt(area/0.95), keeps a list of free spaces, and splits each on placement. Returns w, h, and fill. About 70 lines, deterministic, near-square result, ~85-95% fill.
- d3-hierarchy siblings.js: front-chain circle packing. Each circle is tangent to two previous ones (place()), the closest-to-centroid pair is rescored (score()), and an enclosing circle is computed (packEncloseRandom). Needs only radii as input and yields a tidy "bubble" layout.
- UnrealEngineSkyAtmosphere: physically based sky from Rayleigh, Mie and ozone, using a transmittance LUT (RenderTransmittanceLutPS), a multiple-scattering LUT (NewMultiScattCS, "Equation 10 Psi_ms"), a sky-view LUT (SkyViewLutPS) and a camera froxel volume for aerial perspective (RenderCameraVolumePS). The LUTs make a full sky cost a few texture reads per pixel and recomputed only when the sun or atmosphere changes.
- GodotOceanWaves spectrum_compute.glsl: Tessendorf-style FFT ocean. It generates a TMA/JONSWAP spectrum with finite-depth dispersion (dispersion_relation, tanh) and Hasselmann directional spread (hasselmann_directional_spread), with swell/spread/detail knobs, then (separate shaders, not read) an inverse FFT per cascade. Several cascades with different tile_length avoid visible tiling.

## Improvements: existing nodes first
1. Ocean: add wind-driven look controls without full FFT (S). Replace the pure Gerstner amplitudes with wave amplitudes sampled from the JONSWAP/TMA spectrum (peak frequency, wind speed, swell, spread) and a Hasselmann-style directional spread instead of golden-angle spacing. This gives the user a "wind speed / swell / fetch" mental model and more natural sea states. Still CPU, still closed-form, so Mesh.cpp:1517 changes only. Skill: new-geometry-node (revision rules). Licence: formula from the cited academic papers (Tessendorf, Horvath), MIT file only inspired. No attribution needed if written from the papers.
2. Ocean: true FFT cascade (L). 256x256 FFT displacement with 2-3 cascades and foam from the Jacobian would give a convincing open sea. It needs GPU compute or a CPU FFT; the repo has GL 3.3 shaders only, so it would be render-to-texture ping-pong, and Ocean currently outputs a CPU mesh (MeshRevision at OceanNode.h). That is a redesign; flag as large. Gate on the Windows GLSL 330 constraint (windows-parity).
3. Render 3D background: procedural physical sky (M). Add a "sky" mode to the Environment/HDRI side (or to Render 3D's gradient) that bakes a transmittance + multiple-scatter + sky-view LUT from sun elevation, turbidity and ground albedo, and writes the equirect that Render 3D already samples as environment (EnvironmentNode.h:61 GetEnvironmentTexture). Because Render 3D already treats the environment as an equirect texture with mips, a CPU or GL-baked equirect sky would plug straight in with no changes to Render 3D, and also light the scene (the averaged-mip IBL approximation). Sun elevation then becomes modulatable, which gives animated sunrise/sunset. Skill: new-source-node style for a texture source plus Render 3D env pin. Licence: UE repo is MIT, so near-port needs the MIT notice kept; or implement from Hillaire's paper (EGSR 2020) to avoid attribution.
4. Render 3D: aerial-perspective fog (S-M). One extra term in the lit fragment shader, exponential height fog tinted by the sky colour, with density and falloff controls and a sun in-scatter lobe. The UE froxel volume is overkill, but the per-distance transmittance blend from the same sky model is the cheap subset. Skill: render-pipeline-sweep for scene-cache signature (any new uniform must enter SceneSignature, see codebase-navigation note on Render 3D mDraw). Licence: pure maths.

## Improvements: new nodes
5. Pack Rects / Atlas (S). Input: N image or text cells, output: tile positions via potpack's shelf algorithm, as a point cloud or image atlas for Distribute / Instance on Points. Gives artists contact-sheet, tiled-typography and sprite-atlas layouts. potpack is 70 lines, so it can be re-implemented outright. Skill: new-geometry-node (a point-cloud producer; remember the passthrough note that cloud/curve forwarding is easy to miss). Licence: ISC, a rewrite from the idea needs no notice; a direct copy needs the Mapbox ISC notice.
6. Pack Circles (S-M). Takes a list of radii (from a Field or audio-bands) and produces tangent non-overlapping circle centres via the front-chain algorithm; "audio bubble chart" visuals. Could also be a Field reduce/map primitive. Skill: new-geometry-node. Licence: ISC (Bostock), same remark. The algorithm derives from the Wang et al. paper, so citing the paper is enough for a reimplementation.
7. Blue-noise / Poisson-disk point distribution (S). Infinite has grid and on-faces distribution (NodeRegistry.cpp:95-97) but I found no Poisson option. kchapelier/fast-2d-poisson-disk-sampling (MIT) was licence-checked only, source not read. Bridson's algorithm is a public paper. Benefit: even, non-clumpy scatter for instancing. Skill: new-geometry-node.
8. Treemap layout (S). d3 squarify is 66 lines. Maps a list of weights to rectangles (e.g. audio spectrum bands to a treemap). Niche; low priority.

## Nothing to learn
- Path tracing: nothing worth taking (see Skipped).
- Force layout: no consumer in Infinite.
- Text layout: not assessed beyond TextNode wordWrap; no claim made.

## Open risks
- I did not read the d3 treemap or d3-force algorithms in depth, and did not read the rest of the Godot ocean pipeline (FFT / displacement shaders). Candidate 2 is based on the spectrum shader plus general knowledge of the Tessendorf method.
- OceanNode.cpp internals past line 20 were not read; revision handling for a changed spectrum should be checked against new-geometry-node.

## Candidates
Procedural sky bake (UE atmosphere LUTs -> equirect env) | animated physically-based sunrise/sunset lighting and background, modulatable sun | M | MIT (UE sky) or paper-derived
Ocean spectrum-driven waves (JONSWAP/TMA + directional spread) | natural sea states with wind/swell controls, no new infra | S | paper formulas, MIT GodotOceanWaves as inspiration
Pack Rects / Pack Circles nodes (potpack, d3 pack) | atlas, contact-sheet and bubble layouts from lists | S | ISC
Render 3D height/aerial fog | depth cue and haze in 3D scenes | S-M | maths only
Poisson-disk distribution | blue-noise instancing | S | MIT (not read) / Bridson paper
