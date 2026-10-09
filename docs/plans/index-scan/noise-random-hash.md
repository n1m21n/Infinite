# Cluster: noise / random / hash

NOTE: candidates were NOT cross-checked against the indexopensource.com list (site blocked, 403). Picked from own knowledge plus docs/plans/index-opensource-scan.md:20 (FastNoise Lite, MIT, "check existing noise set first").

## Projects
Read:
- Auburn/FastNoiseLite, branch master, Cpp/FastNoiseLite.h (2586 lines). Licence verified from raw LICENSE: MIT, (c) 2020 Jordan Peck.
Skipped / not fetched (time-boxed, one deep peer was enough):
- PCG hash refs, Squirrel Eiserloh noise, open-simplex ports, blue-noise generators: not fetched. Ideas below from FastNoiseLite only. Licences of those were NOT verified, so nothing read.

## What Infinite does today
- Noise source node: `src/nodes/NoiseNode.cpp:10-11` types Value, fBm, Ridged, Voronoi, Worley Edges, White. GLSL 150 shader at :13-100. Hash is `fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453)` (:33-36), value noise with smoothstep (cubic) interpolation (:41-47), fBm of VALUE noise (:48-60), "ridged" is fold of value noise (:54), Voronoi F1/F2 with animated points (:62-75), domain warp via two fBm lookups (:77-80). Params at `NoiseNode.h:30-45` (seed is just added to p, :34). Note "Perlin-style" in the header comment (NoiseNode.h:8) is inaccurate: no gradient noise exists here.
- Same sin-hash is copy-pasted across many shaders: TextureNode.cpp:72-81, RampNode.cpp:32, FormulaNode.cpp:24,120-198, FeedbackNodes.cpp:192, FieldSynthNode.cpp:587-981; CPU variants GeometryOpNodes.cpp:30,56, SimulationNodes.cpp:46-50 (`Noise3`, hash-only "value noise", comment admits it is not smooth).
- Field: `rand`/`noise`/`sh` are time-domain only, driven by a 64-bit integer Xorwise hash (`src/core/field/FieldRandom.h`, `FieldRandom.cpp:7-30`). They are rejected per-pixel because GLSL 150 lacks int64 (`FieldIR.cpp:3844-3855`). Grep of src/core/field for spatial noise builtins (perlin, simplex, fbm, vnoise, hash) found none; the Field Pixel "Noise Texture" preset (`FieldPixelNode.cpp:216-224`) fakes noise with sin*cos products.
- Searched for simplex, cellular, blue noise, pcg, ping-pong, domain warp in src: only hits were unrelated (platform/audio files); no gradient/simplex/blue-noise code found.

## What the peer does differently (FastNoiseLite.h)
- Integer lattice hash with large primes: `Hash(seed, xPrimed, yPrimed)` :491, `ValCoord` :509, `GradCoord` :529. 32-bit int only, so portable to GLSL 330 `uint` (GLSL 150 has no `uint` hashing issues either: 150 supports uint ops) and removes sin() precision dependence (sin hashes differ across GPUs/llvmpipe and band at large coordinates).
- Real gradient Perlin with quintic fade (`SinglePerlin` :1770, `InterpQuintic` :459), normalised by 1.4247691.
- OpenSimplex2 / 2S (`SingleOpenSimplex2S` :1156), Cellular with distance functions and return types (`SingleCellular` :1483), Value Cubic (:1834).
- Fractals: FBm :845, Ridged :891, PingPong :937 (uses `PingPong` :467 with weighted strength), plus domain warp (gradient / fractal progressive / independent).
- Per-octave seed++ (:942) so octaves decorrelate, instead of sampling the same lattice at scaled coords.

## Improvements
Existing nodes first:
1. Noise node: add true gradient Perlin (quintic) and Simplex types, and switch fBm/Ridged to use the gradient basis. Benefit: removes grid-aligned artefacts of value noise, matches what users expect from "Perlin". Add types at the END of the list (`NoiseNode.cpp:10`) to keep saved `noiseType` ints valid. Skill: new-source-node / new-effect-node (shader string). Effort M. Licence: MIT; reimplement in GLSL from the algorithm; if code is ported near-verbatim, keep the MIT copyright notice.
2. Noise node: add Ping-Pong fractal, per-octave seed offset, Cellular return types (F2-F1, cell value, distance) and distance metric (Euclid/Manhattan/Hybrid). Existing Voronoi at :62-75 only offers F1 and F2-F1. Effort S-M.
3. Replace sin-hash with an integer hash (PCG-style / FNV / the FastNoiseLite prime hash) in one shared GLSL preamble used by NoiseNode, TextureNode, RampNode, FormulaNode, FeedbackNodes. Benefit: cross-GPU/Linux llvmpipe determinism, no banding at large scales or long seeds. Risk: changes every saved patch's look, so gate by a version flag or only apply to new types. Hand-maintained duplicates listed above are the touchpoints. Effort M (regression risk).
4. Domain warp: current warp is value-fBm only (:77-80); add the peer's progressive/independent warp and gradient-based warp. Effort S.
New nodes / Field:
5. Field spatial noise builtins (`noise2(x,y)`, `perlin`, `simplex`, `cellular`, with optional seed) for Pixel/Element domains, using a 32-bit hash so the GLSL 150 int64 limit (`FieldIR.cpp:3853`) does not apply. Also gives a CPU VM/GLSL parity concern: needs identical int32 math on VM and GLSL backends. Skills: field-compiler, field-realtime, field-testing (golden corpus), field-integration. Effort L. Existing `noise` name is already taken by time-domain; choose new names.
6. Geometry noise: SimulationNodes `Noise3` (:46) is hash-only; swap for gradient/simplex 3D (cheap, stateless) and curl noise built from it. Effort S-M.

## Nothing to learn
Time-domain `rand/sh` already uses a solid integer hash with exact period; no change suggested from this cluster.

## Candidates
Gradient Perlin + Simplex types in Noise node | removes value-noise grid artefacts, fills "Perlin-style" gap | M | MIT (FastNoiseLite, notice if near-port)
Field spatial noise builtins (32-bit hash) | real noise in Field pixel/element code, replaces sin() fakes | L | MIT (FastNoiseLite)
Shared integer-hash GLSL preamble | deterministic across GPUs, kills duplicate sin-hash copies | M | MIT/own code
Ping-Pong fractal + cellular return types + warp modes | more looks from existing node | S-M | MIT
