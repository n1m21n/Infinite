# index-scan SUMMARY

Ranked candidates across all clusters (max 10). Each line ends with a decision for the owner: approve / change / abandon.
Detail, file:line evidence and licence notes are in the cluster file named in brackets.

## Scope caveat (read first)
`indexopensource.com`, github.com and api.github.com were blocked by this session's egress policy (403); only
raw.githubusercontent.com worked. The 412-project list was therefore NOT fetched, and candidates were NOT cross-checked
against it. Peers were chosen from well-known repos and the first-pass tiers in `docs/plans/index-opensource-scan.md`,
and every peer's licence was verified from its raw LICENSE file before any source was read (GPL/AGPL/LGPL, 404 and
unclear licences skipped). Treat this as a partial scan; re-run with the index reachable to catch projects not named here.

## Ranked candidates
1. **Delaunay Mesh + Voronoi Cells geometry nodes** (ISC Delaunator; no Delaunay/hull/Voronoi geometry exists) [triangulation-voronoi-hulls-spatial] - M. Decision: approve / change / abandon?
2. **Simplify/Decimate node, QEM edge collapse** (MIT meshoptimizer idea; grep found no simplify/decimate) [mesh-ops] - M. Decision: approve / change / abandon?
3. **Time-based Smooth + Spring mode, plus Penner easing table** (fixes frame-rate-dependent `SmoothNode`; MIT/Unlicense) [easing-springs-ik] - S-M. Decision: approve / change / abandon?
4. **Gradient Perlin + Simplex in NoiseNode** (today value noise + sin hash; MIT FastNoiseLite) [noise-random-hash] - M. Decision: approve / change / abandon?
5. **Optical Flow node, Horn-Schunck GLSL first** (no flow code today; own implementation from papers) [cv-ml] - M. Decision: approve / change / abandon?
6. **Curve Offset/Stroke node + Curve Ops (resample/simplify/smooth)** (BSL-1.0 Clipper2 model; curves can only be transformed today) [curves-splines-strokes] - M. Decision: approve / change / abandon?
7. **Color Ramp Oklab/OKLCh interpolation + palette gamut mapping** (ColorRamp lerps raw RGB; MIT Color.js idea) [colour] - S-M. Decision: approve / change / abandon?
8. **Dither node (Bayer + blue noise) and linear-tap blur/bloom fix** (no dither node; blur bands at wide radius) [image-filters] - S. Decision: approve / change / abandon?
9. **Cloth XPBD compliance/substeps + particle curl noise/collision** (MIT Jolt idea; fix stale help text at HelpWindows.cpp:824) [physics-particles-fluids] - S-M. Decision: approve / change / abandon?
10. **Cellular Automaton node + Life/Lenia Field Pixel presets** (no CA exists; from papers) [generative-rules] - S-M. Decision: approve / change / abandon?

## Honourable mentions (not ranked)
Field error-span highlighting (S, data already exists) and marker ripple move [tool-design]; analog drum voices and
envelope-follower auto-wah [audio-dsp]; procedural sky bake and ocean spectrum waves [layout-renderers];
OrtModel refactor as prerequisite for any ML node [cv-ml].

## Clusters skipped or only partly covered
- Skipped as irrelevant to Infinite (per prompt): maps/GIS, graph viz, robotics, video generators, path-tracer research renderers, force/text layout.
- IK/boids: no peer read, licences unchecked [easing-springs-ik].
- Lenia, DLA, L-system, space colonization peers: LICENSE fetch 404, so ideas come from papers only [generative-rules].
- quickhull has no LICENSE file (README says public domain): not read; hull node would be a textbook rewrite.
- Penrose, noflo, Bezier.js, culori: licence unverifiable or nothing to learn.
