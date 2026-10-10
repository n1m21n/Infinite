# Prompt: mine indexopensource.com projects for ideas that improve Infinite

You are working in the Infinite repo (/Users/namansoni/infinte). Read AGENTS.md first and follow its skill rules.
Load `codebase-navigation`, `codebase-lenses`, `semi-brain` before starting. Create branch per `git-branch-workflow`.

## Goal
Go through the open-source projects catalogued at https://indexopensource.com and extract techniques that would
improve Infinite across ALL categories (new nodes AND improvements to existing nodes, UI, modulation, audio,
geometry, effects, Field). This is for inspiration and learning, not for linking libraries.
Language does not matter: JS, Go, C#, Unity, Python code is all fair game as a source of ideas.

## Inputs already prepared
- `docs/plans/index-opensource-scan.md`: the first-pass scan and tiers (summary only).
- Project list: `curl -sL https://indexopensource.com/data/en.json` (412 projects; fields: values[1]=name,
  values[8]=repo, values[9]=licence, plus description/keywords). Re-fetch GitHub README/meta with `gh api`
  into a scratch dir, not the repo.

## Hard rules
1. Clean room (AGENTS.md invariant 1): skip every GPL / AGPL / LGPL project and "Needs rechecking" licences.
   Never open their source, READMEs excepted only if the licence is permissive. Permissive (MIT, BSD, ISC,
   Apache-2.0, Unlicense, 0BSD, Zlib, BSL-1.0, CC0, public domain) may be read in full. Note attribution
   need when an idea is a near-port.
2. Do not edit src/. Output is notes only, in `docs/plans/index-scan/<cluster>.md`.
3. Ground every claim: before saying "Infinite lacks X" grep `src/` and name the file(s) that exist.
   Never say "we have nothing" from memory.
4. No commits to main, no pushes, no release talk.

## Method
1. Group the permissive projects into technique clusters (not one by one). Suggested clusters:
   noise/random/hash; triangulation, Voronoi, hulls, spatial indexes; curves/splines/strokes/path offsets;
   mesh ops (simplify, smooth, boolean, voxel, marching); generative rules (WFC, L-systems, growth,
   cellular automata, DLA, reaction-diffusion); physics/particles/fluids; image filters (blur, bloom, dither,
   halftone, glitch, watercolour, LUT, blend); colour spaces and mixing; easing/springs/IK/procedural
   animation; audio DSP and synthesis; computer vision/ML (optical flow, tracking, depth, masks);
   layout/packing; renderers (path tracing, sky, ocean); tool-design ideas (Orca, Motion Canvas, Penrose).
   Drop clusters irrelevant to Infinite (maps/GIS, graph viz, robotics, video generators) and say so.
2. For each cluster pick the 1-3 best projects and read the actual implementation code (core files, not just README).
3. Find Infinite's equivalent (node, shader, Field builtin, DSP) and read it.
4. Write `docs/plans/index-scan/<cluster>.md` with:
   - What Infinite does today (file:line).
   - What the peer does differently, citing their file/function.
   - Concrete improvements: existing-node upgrades first, then new nodes. For each: user-visible benefit,
     which skill applies (new-*-node, field-*), rough effort S/M/L, licence/attribution note.
   - "Nothing to learn" is a valid answer; say it plainly.
5. Finish with `docs/plans/index-scan/SUMMARY.md`: ranked list of at most 10 candidates across all clusters,
   each with a one-line propose-a-decision for the owner (approve / change / abandon). One line per candidate.

## Execution
Use parallel subagents, one per cluster (cartographer-style deep reads). Keep each subagent's report to the notes file;
return only a 5-line digest to the main session. Report any cluster you could not finish and why.

## Done when
Every cluster has a notes file or an explicit skip reason, SUMMARY.md exists, and nothing outside docs/plans/index-scan
was changed.
