# indexopensource.com scan (2026-10-09)

Source: https://indexopensource.com (data at /data/en.json, 412 projects).
Method: script pulled repo metadata + README head from the GitHub API for every
permissive-licence project (351 of 412). The ~60 GPL/AGPL/LGPL and 3 "needs rechecking"
entries were skipped on purpose (clean-room rule, AGENTS.md invariant 1) and never read.
Raw notes: scratchpad `gh_notes/*.json` + `digest.txt` (not kept in repo).

Facts from the scan: 351 repos read; 6 archived; ~81 untouched for 3+ years;
languages JS 92, Python 48, C++ 35, C# 20, TS 18, C 11, GLSL 9, Go 9, Rust 9.
Most of the list is JS/Go/Unity/Python: useful as an algorithm reference, not as code to link.

## Tier 1: native, permissive, maintained: could be linked or vendored
| Project | Licence | Lang | Last push | Gap in Infinite |
|---|---|---|---|---|
| Manifold | Apache-2.0 | C++ | 2026-10 | robust mesh booleans for geometry nodes |
| meshoptimizer | MIT | C++ | 2026-10 | mesh simplification / LOD |
| Clipper2 | BSL-1.0 | C++ | 2026-04 | 2D polygon boolean / offset / triangulation |
| QuickHull | public domain | C++ | 2024-07 | 3D convex hull node |
| FastNoise Lite | MIT | C/C++/many | 2026-06 | check existing noise set first |
| DaisySP | MIT | C++ | 2026-09 | reference for missing DSP blocks (Infinite has its own DSP) |
| Box2D / Jolt | MIT | C / C++ | 2026-10 | physics; check what the 10 existing "rigid" hits already use |

## Tier 2: algorithms to port or reimplement (MIT/ISC/Unlicense, small)
Delaunator (0 hits in src today), Lenia, WaveFunctionCollapse / MarkovJunior, Lindenmayer,
space-colonization, dlaf (DLA), Spectral.js (Kubelka-Munk mixing), KinoAqua (watercolour),
LineIntegralConvolutions, HatchingShader, Spring-It-On (damped spring smoothing),
optical flow (write our own Horn-Schunck / Lucas-Kanade shader from the papers; do NOT link OpenCV).

## Tier 3: ML models (need a runtime decision first, e.g. ONNX Runtime)
MediaPipe Hand Landmarker (hand joints), Depth Anything V2 (verify licence per model size;
larger checkpoints may be non-commercial), SAM 2, U2-Net, MT3 (JAX/T5X, hard to embed),
Whisper, DDSP. One shared dependency + weights-download story would unlock several.

## Tier 4: skip
Web/GIS/graph viz (d3-*, Leaflet, MapLibre, deck.gl, Cytoscape...), robotics/science sims,
research renderers (pbrt, Mitsuba, appleseed, LuxCore), video/3D generation models,
animation JS libs, Unity-only effects (algorithm reference at most).

## Open checks before building anything
- grep src for what already exists (Voronoi, reaction-diffusion, noise, physics are present).
- Licences that are non-standard in the index: SAM-specific, Geant4, NIST, "BSD-style + grant".
