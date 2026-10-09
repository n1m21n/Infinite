# Cluster: generative rules (WFC, MarkovJunior, L-systems, space colonization, CA/Lenia, DLA, RD, growth)

Candidates were chosen from my own knowledge plus docs/plans/index-opensource-scan.md Tier 2 (line 25-26).
They were NOT cross-checked against the indexopensource.com list (the site is blocked, 403).

## Projects read
| repo | licence (verified from raw LICENSE) | branch | files read |
|---|---|---|---|
| mxgmn/WaveFunctionCollapse | MIT, (c) 2016 Maxim Gumin | master | Model.cs (Run, NextUnobservedNode, Observe, Propagate, Ban, Clear), OverlappingModel.cs (pattern extraction, symmetry, agrees/propagator build) |
| mxgmn/MarkovJunior | MIT, (c) 2022 Maxim Gumin | main | source/OneNode.cs (Go, RandomMatch), source/RuleNode.cs (Load, rule symmetry expansion); Rule/AllNode/ParallelNode/Field/Interpreter fetched, skimmed only |

## Skipped
- Chakazul/Lenia, fogleman/dlaf, jgillick/lsystem: LICENSE fetch returned 404 on main and master, so licence is unverifiable. Not read.
  Lenia, DLA and L-systems are all described in open papers and textbooks (Chan 2019 Lenia; Witten-Sander 1981; Prusinkiewicz & Lindenmayer, The Algorithmic Beauty of Plants; Runions et al. 2007 space colonization), so they can be implemented from the maths without any peer source.
- No GPL project was opened.

## What Infinite does today (grep of src/ for wavefunction|markov|l-system|colonization|lenia|cellular|conway|life|dla|diffusion-limited|reaction-diffusion|gray-scott|physarum|boids|differential growth|alive|neighbor)
- Reaction-diffusion EXISTS twice:
  - `ReactionDiffusionNode` (Gray-Scott, ping-pong RGBA16F, 6 presets, optional image modulates feed): src/nodes/FeedbackNodes.cpp:179-330, registered src/app/graph/NodeRegistry.cpp:123.
  - Field Pixel preset "Reaction Diffusion (Gray-Scott)" using `state` cells with `[wrap]`, `age` one-shot seeding: src/nodes/FieldPixelNode.cpp:300-330. High-precision bank for neighbour-reading kernels: src/core/field/PixelState.h:17-24.
- "Physarum Slime Mold" (src/nodes/FieldPixelNode.cpp:733) is a static sin/cos vein pattern, NOT an agent/trail simulation. No agent-based slime found.
- Markov exists only for notes: PredictiveNotes/Rhythm (src/audio/NoteModel.h:9, src/nodes/PredictiveNotesNode.h:17). Nothing spatial.
- Point scatter: Poisson disk and random on faces (src/nodes/PointDistributionNodes.cpp:28, src/core/Mesh.h:529-533), Distribute Points in Grid (NodeRegistry.cpp:97).
- Curves: `CurveNode` exposes a `Polyline` via `GetCurve()` (src/nodes/CurveNode.h:36, struct at src/core/Mesh.h:281). Particles: SimulationNodes.cpp. Iterative mesh mutation: `MeshResynthNode` (src/nodes/GenerativeNodes.h, deterministic by seed+generation).
- Searched and found NOTHING (zero hits in src/ and in the Field presets lists in FieldPixelNode.cpp, FieldElementNode.cpp, FieldPrimitiveNode.cpp): wave function collapse, tiled/constraint synthesis, rewriting grammars (L-system/Markov Junior), Game of Life or any cellular automaton, Lenia, DLA, space colonization, differential growth, boids. Only "Cellular" is a ResynthNode mode name (src/nodes/ResynthNode.cpp:14), which is an image-resynth effect, not a CA. "Pine Tree" (src/nodes/FieldPrimitiveNode.cpp:131) is a hand-built SDF, not a grammar.

## What the peers do differently
- WFC (Model.cs): per-cell boolean wave over T patterns; observe = lowest-entropy cell (`entropies[i]`, noise 1e-6 tiebreak), weighted random collapse; `Propagate` is a stack-based arc-consistency pass over `propagator[d][t]` with `compatible[i][t][d]` counters (a pattern is banned when a direction's support count reaches 0). Overlapping model extracts NxN patterns from a sample image with up to 8 symmetries, weights = frequency, adjacency by `agrees`. Heuristics: Entropy, MRV, Scanline. Failure returns false and caller re-seeds. This is CPU, inherently sequential, deterministic per seed.
- MarkovJunior (OneNode.cs/RuleNode.cs): rewrite rules (input pattern -> output pattern, wildcard 0xff) expanded over symmetry group; OneNode applies one random match per step from an incrementally maintained match list (`matches`, `matchMask`, swap-remove); AllNode applies all non-overlapping; ParallelNode applies simultaneously. Optional `fields` (distance potentials) and `temperature` bias the choice (Field.DeltaPointwise), and `observations` do goal-directed search. A program is a tree of sequence/markov/loop nodes in XML.

## Concrete improvements

### Existing-node upgrades
1. Reaction Diffusion node: add the diffusion stability fix and richer control that the Field preset already documents (explicit-step bound, FieldPixelNode.cpp:322-326). The node's sim shader (FeedbackNodes.cpp:205-245) uses full-step Laplacian weights 0.2/0.05 with no sub-step; add "steps per frame" (2-8 sim passes) and a kill/feed brush from the input image (not just feed modulation). Benefit: faster, controllable growth; visibly better at 60fps. Skill: new-compositing-node (ping-pong cooking). Effort S. Licence: none needed, own code.
2. Replace the fake "Physarum Slime Mold" Field preset with a real texture-space agent model is NOT possible in a Field Pixel kernel (no scatter writes, see field-language); the honest fix is rename it ("Slime Veins") or build the agent version as a node (below). Effort S for rename. Licence: none.
3. Field Pixel presets (S each, no C++ beyond the preset table, use `state` + `[wrap]` exactly like the Gray-Scott preset, skill field-pixel-presets): 
   - "Game of Life / Life-like (B/S rule params)": state cell, 8 offset reads, birth/survive thresholds as params, seeded via `age`. 
   - "Lenia": state cell, ring-kernel convolution via bounded `for` loop of offset reads (field-realtime restricts to bounded loops; radius capped ~8 for cost), growth function (gaussian bump) with mu/sigma params. Reference: Chan 2019 paper, not code. 
   - "Cyclic CA / Greenberg-Hastings": cheap, striking spirals.
   Benefit: three new living-pattern generators that react to audio via params, with zero new node plumbing. Verify bank precision (PixelState.h:17-24).

### New nodes
1. Cellular Automaton (Compositing, Source-like, optional image seed): dedicated ping-pong node with rule set (Life, Seeds, Brian's Brain, Lenia presets), steps/frame, seed from input luminance, injection by input. More efficient than the Field route and exposes ParamRefs. Skill: new-compositing-node (or new-source-node). Effort M. Licence: none (algorithms are public domain concepts).
2. WFC Tiles / Pattern Synth (Source or Effects): input image = sample, output = same-style image of chosen size (overlapping model, N=2 or 3, symmetry, periodic). Realistic design: CPU solve in a worker on parameter change, upload R8/RGBA texture, `seed` param modulatable between solves; re-solve on contradiction with bounded retries. Distinct from ResynthNode (src/nodes/ResynthNode.cpp) which resynthesises via other means. Algorithm is the Model.cs loop above (entropy pick + stack propagation); implement from scratch from the published description in the README; if any structure is closely mirrored (propagator/compatible counters), retain "Copyright (C) 2016 Maxim Gumin, MIT" notice in a comment. Skill: new-source-node (cook memo, TextureRevision) plus a worker-thread pattern (no precedent outside audio, see codebase-navigation note). Effort L. 
   A cheaper 3D variant (WFC onto a tile grid of Geometry instances feeding Instance on Points, NodeRegistry.cpp:90) is a follow-on. Skill: new-geometry-node.
3. L-System (3D, Curve output): axiom + rules text, iterations, angle, step, stochastic option; turtle interpreter emitting a `Polyline` through `GetCurve()` (CurveNode.h:36) and optionally branch radius for tube/instances. Fits existing curve consumers (Path, Instance on Points). Maths from Prusinkiewicz & Lindenmayer; no peer code needed. Skill: new-geometry-node. Effort M. Licence: none.
4. Space Colonization / Growth (3D): attractor points (from Distribute Points, PointDistributionNodes.cpp) grow a branching Polyline set with Runions-style rules; animate by iteration count modulated by a signal. Skill: new-geometry-node. Effort M. Note the curve type is a single `Polyline`; branching needs a multi-polyline structure (check Mesh.h:281 before committing) - risk.
5. Rewrite Grid / "Markov Junior" Sequencer (Compositing/Source, or note output): simplified MarkovJunior: a small rule list (e.g. `WB -> WW`, `RBB -> GGR`) over a coloured grid, OneNode (random match) and AllNode modes, symmetries, steps per frame, palette mapping; grid can drive texture and also a note/gate output (cells -> rhythm). The core of OneNode.RandomMatch (maintained match list, swap-remove, re-validate with `grid.Matches` before apply) is cheap to reimplement. Skip fields/observations/search in v1 (large). Skill: new-source-node. Effort L. Attribution: near-port of structure => keep MIT notice.
6. DLA (Diffusion-limited aggregation) / Slime agents: two GPU/CPU growth sims. DLA is CPU random walkers on a grid into a texture or point set (Witten-Sander; trivial); agent Physarum (Jones 2010) needs scatter writes so a node with point-sprite or compute-style trail pass. Skill: new-source-node. Effort M each. Licence: none.

## Risks / open questions
- Whether the Field pixel `for` loop and offset-read count limits allow a Lenia kernel radius >= 6 in real time: check field-realtime offsetReadCount cap before promising; prototype only.
- WFC needs a worker thread; Infinite has no non-audio async precedent (codebase-navigation note on synchronous decode).
- Branching curve output needs a data-structure decision (Polyline is single-strip).

## Candidates
Life/Lenia/cyclic CA Field Pixel presets | three new living generators for free, audio-reactive via params | S | none (from papers)
Cellular Automaton node (Life/Lenia/Brian's Brain) | fast, modulatable CA with image seeding | M | none
L-System node (Curve output) | procedural plants/fractals feeding Path and instancing | M | none
Reaction Diffusion node: substeps + image brush | faster, steerable growth in existing node | S | none
WFC Pattern Synth node (overlapping model) | infinite same-style texture/tile output from a sample | L | MIT, keep Gumin notice if structure mirrored
Rewrite Grid (MarkovJunior-lite) | rule-based growth, also drives rhythm | L | MIT, keep Gumin notice
Space colonization + DLA nodes | organic branching/aggregate shapes | M | none
