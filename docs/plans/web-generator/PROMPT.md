# Prompt: live sample generation on the website

Paste this into a fresh session in ~/Infinite/worktrees/main.

---

Goal: on infinitedevices.org/samples, a visitor picks a category (all, synths,
pads, plucks, percussion, drums) and a key, presses generate, hears a new loop
made right then by Infinite's real Generate Samples engine, and downloads it as
a .wav. Each press is a new seed, not a pick from a fixed set. It must be the
same sound the Desktop node makes for that style + seed + part + key.

What exists (read first):
- src/app/graph/SampleStyles.{h,cpp}: style cards + Recipe (style, seed, part,
  bars, bpm, key, scale) -> Compile() -> a patch text of real nodes.
- src/app/graph/OfflineGraph.{h,cpp}: builds that patch and renders it offline.
- src/app/graph/GenerateSamplesNode.cpp: pre-roll, up to 3 redraws
  (seed + attempt*7919), Master() to -14 LUFS under -1 dBTP.
- src/app/selftest/GenSampleTests.cpp: RunGenSampleBatch shows the whole
  headless path in ~50 lines (recipe -> compile -> build -> render -> master).
- website/samples.js + samples.css: the generate button, the generated row and
  the client-side wav writer (wavOf) already exist; today they read a
  pre-rendered pool.json that was never published. Replace the pool source,
  keep the UI.
- The site is static GitHub Pages (deploy-pages.yml); no server.

Step 1, measure before building (report, then stop for my sign-off):
- List every node type a style card can place (SampleStyles::PoolTypes) and,
  for each, what it links against: platform APIs, JUCE/VST3, threads, SIMD,
  file I/O, GL. Mark which compile with Emscripten as-is, which need a shim,
  which must be dropped.
- Cost of the smallest headless target: SampleStyles + OfflineGraph + those
  nodes + core/Patch, no UI, no GL, no audio device. Estimate wasm size
  (gzipped) and render time for a 2-bar loop in Chrome and Safari on a
  mid-range phone. Budget: under 3 MB gzipped, under 2 s per loop.
- Name the alternatives you rejected with their numbers (render server on a
  paid host; a bigger pre-rendered pool) so the choice is grounded.

Step 2, after sign-off:
- A CMake target `infinite_gen_wasm` (Emscripten) exposing one call:
  generate(style, part, seed, bars, key, scale) -> interleaved float32 +
  label + bpm. Same Master() and redraw rule as the node.
- Run it in a Web Worker so the page never blocks; show a busy state on the
  button; progress if cheap.
- Parity test: for 5 styles x 6 parts at seed 1, the wasm output matches the
  Desktop render (same LUFS within 0.5 dB, same length, correlation > 0.99).
  Add it as a hygiene check or a CI job, whichever is cheaper.
- Website: the generate button calls the worker; the style is random within
  the chosen category; the key filter is respected (ignored for drums and
  percussion); the download is named infinite-<label>-<bpm>bpm.wav.
- Lazy-load the wasm only on the first generate press.

Rules: no new features beyond this; one build at a time via scripts/ibuild.py;
short lowercase commits, one per step, no attribution lines; nothing pushed
without my yes; check the page in the browser at phone and desktop width.
