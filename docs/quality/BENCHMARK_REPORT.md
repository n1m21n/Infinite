# Infinite v0.6.0 — Benchmark Report

Source: `docs/quality/SCOREBOARD.md` (rows dated 2026-10-12 to 2026-10-14) and `bench/baselines/m2-8gb.jsonl`.
Machine: base MacBook Air M2, 8 GB (Mac14,7). No data from Windows or Linux.
Scope: this is what was measured. Competitor numbers exist only where listed in §2–§4; everything else is Infinite against its own targets.

## 1. Summary

| Pillar | Metrics | Met | Behind | Info only | Unmeasured |
|---|---|---|---|---|---|
| P1 Live AV workstation | 10 | 9 | 0 | 1 | 0 |
| P5 Audio quality and hosting | 36 | 28 | 0 | 8 | 0 |
| P6 Timeline and arrangement | 2 | 1 | 0 | 1 | 0 |
| P7 3D, compositing, output | 10 | 9 | 1 | 0 | 0 |
| P8 Performance | 39 | 18 | 2 | 7 | 12 |

Known misses: 10,000-instance scene cost (§4), the heaviest projector scene (§5), and three recorded exceptions (effect insert gain, stretch crest, geometry winding).

## 2. Audio: Infinite vs other synths (black-box MIDI → audio, same harness)

| Metric | Analog | Wavetable | Vital | Serum 2 | Diva |
|---|---|---|---|---|---|
| Worst pitch error, MIDI 24–108 (cents, lower better) | 0.173 | 0.174 | 0.174 | 0.174 | 7.73 |
| Non-harmonic energy, MIDI 60 (dB, lower better) | -84.51 | -84.16 | -84.34 | -82.96 | -40.80 |
| Non-harmonic energy, MIDI 96 (aliasing) | -83.26 | -84.94 | -84.31 | -82.59 | -27.00 |
| Non-harmonic energy, MIDI 108 (aliasing) | -81.20 | -83.92 | -83.63 | -81.86 | -10.35 |

Reading: pitch accuracy is tied with Vital and Serum 2; aliasing is in the same band as Vital and Serum 2 (Wavetable ahead of Serum 2 at 96/108, Analog slightly behind Vital). Diva's figures reflect its analog-modelled design, not a defect. Attack, release and velocity response are design choices and are reported, not judged.

Infinite CPU per voice (offline, ms of render per second of audio): Wavetable 3.03, Analog 29.94. No competitor CPU figures measured. Analog + EQ track: 68.4 ms/s, about 10 tracks at 70% of one core; Wavetable + Reverb: 18.6 ms/s, about 34 tracks.

Internal audio correctness (all met, release-gated): pitch ≤1 cent across 44.1/48/96 kHz, note on/off clicks ≤12 dB, no stuck voices (tail ≤-100 dBFS), bit-identical repeat renders, mixer sum error ≤1e-6, gain knob error 0 dB.

Plugin hosting: 97.6% of installed VST3 and 97.6% of installed AU plugins load and pass audio headless; 0 unexplained host crashes.

## 3. Audio latency and real-time

| Metric | Result | Target |
|---|---|---|
| Audio callback load p99, 24 voices, 256-frame buffer | 44.75% | ≤50% |
| Xruns at 256 frames | 0 | 0 |
| Effect latency after compensation | 0 ms (AudioFilter 0.19 ms) | informational |

Not a target: 64-frame buffers (the baseline recorded 3 xruns at 64 frames).

## 4. 3D and offline render: Infinite vs Blender

| Metric | Infinite | Blender | Note |
|---|---|---|---|
| 10,000 instances, per frame | 3.08 ms | 1.26 ms | Infinite behind (target ≤1.30) |
| Subdivide ×2 + animated displacement, per frame | 7.19 ms | 8.14 ms | Infinite ahead |
| Cloth 32×32, per frame | 0.42 ms | 18.4 ms | solvers differ (Infinite 6 iterations, Blender 5 quality steps) |
| Movie render fps (blur scene, ProRes 4444) | 170 fps at 720p, 81 at 1080p, 44.5 at 2160p | 1.58 fps at 720p | same scene family, Infinite far faster |
| H264 luma PSNR, blur scenes | 50.6–51.8 dB | 44.3–46.1 dB | Infinite ahead |
| H264 luma PSNR, instanced scenes | 35.5–37.1 dB | 28.8–32.2 dB | Infinite ahead |
| Movie render fps, instanced scenes (ProRes 4444) | 128 (720p), 80 (1080p), 57 (2160p) | 0.88 (720p), 0.66 (1080p), 0.21 (2160p) | Infinite far faster |
| A/V sync offset, 10 min render | 0.042 ms | 0.021 ms | both negligible |
| A/V sync drift, 10 min render | 0 ms/min | 0 ms/min | tied |

Arrangement render of the reference scene: 2.25× realtime; worst A/V drift 0 frames.
Offline movie fps (Infinite): blur 720p H264 263, instanced 720p H264 201, instanced 1080p ~80.

## 5. Live visuals, UI and projector (Infinite only)

| Metric | Result | Target |
|---|---|---|
| Launch to first frame | 342 ms | ≤1000 ms |
| Patch load, 400 nodes | 6.04 ms | ≤16.7 ms |
| Patch save | ≤2.86 ms | ≤16.7 ms |
| Canvas pan p50, 300 nodes | 16.67 ms | ≤17.2 ms |
| Canvas pan p95, 200 nodes | 16.69 ms | ≤22.8 ms |
| Project load, 4000 nodes (headless) | 0.874 s | informational |
| Projector p99 interval under load | 17.64 ms | ≤18.3 ms |
| Missed vsync, standard scenes | 0% | ≤0.5% |
| Input-to-photon | 1 frame | ≤2 frames |
| Peak memory footprint | 760 MB | ≤912 MB |
| Audio-reactive flash latency | 16.6 ms (frame quantisation ~9.6) | ≤45 ms |
| Beat lock / clock lock | 100% / 100% | 100% |
| Sync drift over take | 0 ms/min | ≤1 ms/min |
| Steady-state render-target reallocations | 0 | 0 |
| Stress (fuzz, random patches, kill-during-save) | 0 failures | 0 |
| Media decode (4×2160 clips) | 1.01× clip fps, 0 dropped | ≥1.0×, 0 |

Misses:
- Heaviest projector scene (4×2160 video + 3 windows + camera + Syphon): p99 33 ms and 9.4% missed vsync (targets 18.3 ms / 0.5%). Scenes with 2×1080 and 2–3 windows are at 17.7 ms p99 and 0% missed.

## 6. Not measured yet

- 4000-node canvas pan/select/delete (R5), edit-storm under live load (B3 edit), long soak (B7: memory growth, xruns, thermal), real-time audio-reactive 10,000-instance scene (live.frame_p95). These are defined and gated but have no rows.
- Competitor comparisons for DAW/VJ tools (Ableton, Bitwig, TouchDesigner, Resolume, VCV, Max): none. Do not claim parity or superiority against them from this data.

## 7. Safe claims from this data

- Pitch accuracy on par with Vital and Serum 2; aliasing in the same range.
- Offline movie render far faster than Blender on the measured scenes (note: different renderers, so this is a throughput comparison, not an image-quality one) and tight A/V sync (<0.05 ms offset, 0 drift over 10 minutes).
- Cloth simulation and subdivision faster than Blender on the measured cases; instancing slower.
- Launches in about a third of a second on a base M2; 400-node patches load in 6 ms.
- 97.6% plugin compatibility on the test machine, zero unexplained host crashes.

---

# Appendix A — Raw B-suite baseline (`bench/baselines/m2-8gb.jsonl`)

One row per recorded run. Frame times in ms, callback load as % of the buffer period, RSS in MB. B2/B4 rows are the visual and 3D scenes at increasing scale; B5 rows are node-count scaling, stage costs, startup, load/save and undo; B6/B8 are canvas and media I/O runs. Some bench rows were recorded at earlier commits than the final release.

| Bench | Commit | Nodes | Frames | Frame p50 | p95 | p99 | CB load p99 | Xruns | Projector p99 | RSS |
|---|---|---|---|---|---|---|---|---|---|---|
| B1_heavy_audio | a8d8c46 | 149 | 3095 | 16.68 | 29.59 | 47.47 | 40.8% | 3 | - | 208 |
| B1_heavy_audio | a8d8c46 | 149 | 3130 | 16.68 | 28.15 | 47.84 | 41.1% | 0 | - | 407 |
| B1_heavy_audio | a8d8c46 | 149 | 3150 | 16.69 | 26.08 | 40.95 | 44.7% | 0 | - | 347 |
| B1_heavy_audio | a8d8c46 | 149 | 1725 | 16.69 | 68.2 | 109.98 | 40.3% | 0 | - | 280 |
| B2_heavy_visuals | c602a0d | 18 | 152 | 8.63 | 11.05 | 11.98 | - | - | - | 270 |
| B2_heavy_visuals | c602a0d | 17 | 152 | 5.09 | 7.02 | 7.57 | - | - | - | 297 |
| B2_heavy_visuals | c602a0d | 32 | 152 | 16.76 | 22.65 | 24.96 | - | - | - | 311 |
| B2_heavy_visuals | c602a0d | 31 | 152 | 5.01 | 6.61 | 7.35 | - | - | - | 305 |
| B2_heavy_visuals | c602a0d | 42 | 152 | 19.86 | 23.75 | 26.03 | - | - | - | 324 |
| B2_heavy_visuals | c602a0d | 41 | 152 | 4.27 | 7.02 | 9.24 | - | - | - | 314 |
| B2_heavy_visuals | c602a0d | 42 | 152 | 22.19 | 25.16 | 26.57 | - | - | - | 248 |
| B4_complex_3d | c602a0d | 17 | 152 | 4.88 | 7.51 | 8.17 | - | - | - | 340 |
| B4_complex_3d | c602a0d | 17 | 152 | 6.97 | 10.19 | 10.82 | - | - | - | 352 |
| B4_complex_3d | c602a0d | 17 | 152 | 13.48 | 16.98 | 17.64 | - | - | - | 375 |
| B4_complex_3d | c602a0d | 17 | 152 | 6.44 | 8.95 | 9.81 | - | - | - | 344 |
| B4_complex_3d | c602a0d | 17 | 152 | 7.01 | 10.14 | 11.03 | - | - | - | 352 |
| B4_complex_3d | c602a0d | 17 | 152 | 7.27 | 9.75 | 11.54 | - | - | - | 351 |
| B4_complex_3d | c602a0d | 16 | 152 | 1.16 | 4.8 | 6.1 | - | - | - | 356 |
| B4_complex_3d | c602a0d | 17 | 152 | 21.54 | 25.96 | 30.58 | - | - | - | 387 |
| B5_fundamentals_empty | c602a0d | 0 | 152 | 1.01 | 3.29 | 4.17 | - | - | - | 289 |
| B5_fundamentals_nodecount | c602a0d | 50 | 152 | 4.25 | 7.21 | 7.87 | - | - | - | 331 |
| B5_fundamentals_nodecount | c602a0d | 100 | 152 | 7.48 | 10.56 | 11.06 | - | - | - | 377 |
| B5_fundamentals_nodecount | c602a0d | 200 | 152 | 13.86 | 16.44 | 16.63 | - | - | - | 419 |
| B5_fundamentals_nodecount | c602a0d | 400 | 152 | 25.82 | 28.85 | 29.63 | - | - | - | 370 |
| B5_fundamentals_audioalone | c602a0d | 2 | 0 | 0.0 | 0.0 | 0.0 | 1.7% | 0 | - | - |
| B5_fundamentals_audioalone | c602a0d | 2 | 0 | 0.0 | 0.0 | 0.0 | 1.5% | 0 | - | - |
| B5_fundamentals_audioalone | c602a0d | 2 | 0 | 0.0 | 0.0 | 0.0 | 1.1% | 0 | - | - |
| B5_fundamentals_audioalone | c602a0d | 2 | 0 | 0.0 | 0.0 | 0.0 | 2.7% | 0 | - | - |
| B5_fundamentals_stages | c602a0d | 50 | 152 | 8.58 | 11.55 | 17.38 | - | - | - | 331 |
| B5_fundamentals_stages | c602a0d | 100 | 152 | 12.09 | 19.46 | 24.34 | - | - | - | 377 |
| B5_fundamentals_stages | c602a0d | 200 | 152 | 21.16 | 24.16 | 25.2 | - | - | - | 420 |
| B5_fundamentals_stages | c602a0d | 400 | 152 | 34.26 | 36.71 | 37.66 | - | - | - | 324 |
| B5_fundamentals_startup | c602a0d | 0 | 1 | 342.16 | 342.16 | 342.16 | - | - | - | 287 |
| B5_fundamentals_loadsave | c602a0d | 50 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 307 |
| B5_fundamentals_loadsave | c602a0d | 100 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 336 |
| B5_fundamentals_loadsave | c602a0d | 200 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 402 |
| B5_fundamentals_loadsave | c602a0d | 400 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 466 |
| B5_fundamentals_undo | c602a0d | 50 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 309 |
| B5_fundamentals_undo | c602a0d | 100 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 341 |
| B5_fundamentals_undo | c602a0d | 200 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 420 |
| B5_fundamentals_undo | c602a0d | 400 | 1 | 0.0 | 0.0 | 0.0 | - | - | - | 489 |
| B9_memory_footprint | c602a0d | 42 | 600 | 14.38 | 17.15 | 17.51 | - | - | - | 328 |
| B9_memory_footprint | c602a0d | 17 | 600 | 13.67 | 16.72 | 18.69 | - | - | - | 376 |
| B3_live_performance | 19de37d | 72 | 600 | 16.67 | 17.38 | 17.64 | 22.5% | 0 | 17.64 | 247 |
| B6_canvas_nav | beb4d87 | 300 | 600 | 3.15 | 7.12 | 7.84 | - | - | - | 576 |
| B8_media_io | beb4d87 | 9 | 300 | 16.72 | 32.8 | 33.04 | - | - | - | 1807 |
| B6_canvas_nav | d12582d | 300 | 600 | 16.67 | 16.69 | 33.33 | - | - | - | 601 |
| B6_canvas_nav | d12582d | 300 | 600 | 16.67 | 16.68 | 17.17 | - | - | - | 574 |
| B6_canvas_nav | d12582d | 200 | 600 | 16.67 | 16.69 | 17.06 | - | - | - | 508 |
| B6_canvas_nav | d12582d | 400 | 600 | 16.67 | 16.69 | 17.65 | - | - | - | 656 |
| B8_media_io | d12582d | 4 | 300 | 16.67 | 17.06 | 17.64 | - | - | - | 557 |
| B8_media_io | d12582d | 8 | 300 | 16.67 | 17.64 | 17.65 | - | - | - | 783 |
| B8_media_io | d12582d | 4 | 300 | 16.67 | 17.62 | 17.66 | - | - | - | 1045 |
| B8_media_io | d12582d | 8 | 300 | 16.67 | 17.62 | 17.65 | - | - | - | 1868 |
| B8_media_io | d12582d | 4 | 300 | 16.68 | 17.65 | 17.72 | - | - | - | 559 |
| B8_media_io | d12582d | 5 | 300 | 16.67 | 17.7 | 17.77 | - | - | - | 560 |
| B8_media_io | d12582d | 4 | 300 | 16.67 | 17.01 | 17.61 | - | - | - | 554 |
| B8_media_io | d12582d | 5 | 300 | 16.67 | 16.69 | 17.62 | - | - | - | 555 |
| B10_offline_av_sync | d12582d | 72 | 900 | 0.0 | 0.0 | 0.0 | - | - | - | - |
| B10_offline_av_sync | d12582d | 71 | 900 | 0.0 | 0.0 | 0.0 | - | - | - | - |
| B7_soak | 9ce52a5 | 72 | 106837 | 16.67 | 18.08 | 26.57 | 22.9% | 0 | 26.57 | 105 |

Notes on the raw rows:
- **B1 (heavy audio, 149 nodes):** four runs at different buffer sizes. The 64-frame run had 3 xruns (not a target); 256 frames had 0. Callback load p99 40.3–44.7%.
- **B5 startup:** 342 ms to first frame. **B5 audio alone:** callback load 1.1–2.7%, 0 xruns.
- **B5 node-count scaling:** frame p50 1.0 ms (empty) → 4.3 / 7.5 / 13.9 / 25.8 ms at 50 / 100 / 200 / 400 nodes; these runs render every node every frame, so they are a worst-case scaling probe, not the canvas-pan test (B6).
- **B3 live performance (72 nodes):** 0 xruns, callback load p99 22.5%, projector p99 17.64 ms.
- **B7 soak (72 nodes, 106,837 frames, about 30 minutes at 60 fps):** 0 xruns, callback load p99 22.9%, frame p99 26.6 ms. The scoreboard still lists the soak metrics as unmeasured because the RSS-growth and thermal rows were not ingested; the raw run is above.
- **B10 offline A/V sync:** two 900-frame renders, no live frame timing (offline).

# Appendix B — Latest run (`bench/results/Mac14,7/20261016-085847-d1de24dd`)

B8 media I/O only: decode rate 1.01× clip fps on every clip, 0 dropped frames except 2 on the `2x1080+2w` scene; window p99 17.2 ms, missed vsync 0–0.37% on the 2×1080 scenes.

# Appendix C — Complete scoreboard (generated 2026-10-14)

Every metric, target, gate, status and best competitor, verbatim from `docs/quality/SCOREBOARD.md`.


Generated by `tools/quality/quality.py board` from `registry.tsv` and `results.jsonl`; do not edit by hand. How the system works: `README.md`.

Status: **met** every Infinite subject meets the target · **MISS** a release-gated miss · **behind** a report-only miss · **exc** met apart from recorded exceptions · **info** no direction · **unmeasured** no rows yet.

| Pillar | Metrics | Met | Behind / miss | Info | Unmeasured |
|---|---|---|---|---|---|
| P1 Live AV workstation | 10 | 9 | 0 | 1 | 0 |
| P5 Audio quality and hosting | 36 | 28 | 0 | 8 | 0 |
| P6 Timeline and arrangement | 2 | 1 | 0 | 1 | 0 |
| P7 3D, compositing, output | 10 | 9 | 1 | 0 | 0 |
| P8 Performance | 39 | 18 | 2 | 7 | 12 |

### P1 Live AV workstation: AV integration

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `av.react_latency_ms` | Click in the movie audio to the audio-reactive flash in its picture, 30 fps offline render; 45 ms = picture-late sync bound | <= 45.00 ms | report | met | 16.63 (Infinite) | - |
| `av.react_jitter_ms` | Std dev of that latency over 31 beats at 128 BPM (frame quantisation alone gives ~9.6 ms at 30 fps) |  | report | info | 9.33 (Infinite) | - |
| `av.beat_lock_pct` | Beats whose reactive flash lands 0-1 frames after the first possible frame, never early | >= 100 % | report | met | 100 (Infinite) | - |
| `av.drift_ms_min` | Absolute trend of reactive frame error over the take | <= 1.00 ms/min | report | met | 0 (Infinite) | - |
| `av.clock_lock_pct` | Beat-clock flash (no analysis) within 0-1 frames of the click in the same movie | >= 100 % | report | met | 100 (Infinite) | - |

### P1 Live AV workstation: Note nodes

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `note.pair_unpaired` | Stuck, orphan-off, duplicate-on and off-before-on events | <= 0 count | release | met | 0 (node:Arpeggiator) | - |
| `note.grid_samples` | Worst note-on error against the grid across block sizes and tempi | <= 1.00 samples | release | met | 0.793 (node:Arpeggiator) | - |
| `note.jitter_samples` | Note-on error through the node, plus missing notes | <= 0 samples | release | met | 0 (node:Note Filter) | - |

### P1 Live AV workstation: Project files

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `patch.community_fail` | Published community patches that fail the smoke (tools/website/community_patches.py --smoke): each must open, play and show a picture or sound. Pixels are never compared; patches may change as Infinite evolves | <= 0 count | report | met | 0 (Infinite: community patches) | - |

### P1 Live AV workstation: Stability

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `stress.failures` | Crashes, hangs or unstable saves over every node type, 300 fuzzed patches, random patches and 40 kill-during-save rounds | <= 0 count | report | met | 0 (Infinite: fuzz) | - |

### P5 Audio quality and hosting: Audio I/O

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `io.gain_knob_db` | Gain knob error over -48..+24 dB | <= 0.01 dB | release | met | 0 (node:Gain) | - |
| `io.sum_error` | Mixer sum error against a double-precision reference | <= 1e-06 abs | release | met | 0 (node:Mixer) | - |

### P5 Audio quality and hosting: Built-in instruments

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `synth.pitch_cents` | Pitch error per note at 44.1/48/96 kHz (spectrum peak, sustained render) | <= 1.00 cents | release | met | 0.0345 (node:Analog) | - |
| `synth.note_cents` | Pitch error of note 69 played through the note cable | <= 1.00 cents | release | met | 0.009 (node:Analog) | - |
| `synth.alias_db` | Non-harmonic energy re fundamental at 1.75 and 5 kHz, 48 kHz | <= -80.00 dB | release | met | -82.63 (node:Oscillator) | - |
| `synth.rate_spread_db` | Level spread of the same 3 s render over 44.1/48/96 kHz | <= 1.00 dB | release | met | 0.824 (node:PaulStretch) | - |
| `synth.click_on_db` | Note-on second difference re the larger of steady state and the 5-50 ms body | <= 12.00 dB | release | met | 5.70 (node:Wavetable) | - |
| `synth.click_off_db` | Note-off second difference re steady state | <= 12.00 dB | release | met | -0.0027 (node:Equation Synth) | - |
| `synth.poly_tail_dbfs` | Level 9.5 s after all notes off (stuck voices); free-running nodes are not measured | <= -100 dBFS | release | met | -240 (node:Analog) | - |
| `synth.chord4_peak_dbfs` | Peak of a 4-note chord at velocity 0.8, defaults | <= 0 dBFS | release | met | -2.33 (node:Field Synth) | - |
| `synth.peak_dbfs` | Peak of a single sustained note, defaults | <= 0 dBFS | release | met | -3.11 (node:Analog) | - |
| `synth.sample_pitch_spread_c` | Fixture WAV (48 kHz) pitch spread over 44.1/48/96 kHz device rates (F11) | <= 20.00 cents | release | met | 6.85 (node:PaulStretch) | - |
| `synth.sample_length_ratio` | Fixture WAV playback length, longest over shortest across device rates (F11) | <= 1.02 ratio | release | met | 1.00 (node:Granular) | - |
| `synth.det_differs` | 1 when the same render twice is not bit-identical | <= 0 bool | release | met | 0 (node:Analog) | - |
| `probe.pitch_worst_c` | Worst pitch error over MIDI 24-108, black box through the host | <= 1.00 cents | release | met | 0.174 (node:Wavetable) | 0.174 (Vital Audio: Vital) |
| `probe.nonharm60_db` | Non-harmonic energy at MIDI 60 (clean reference); best competitor about -84 | <= -80.00 dB | release | met | -84.16 (node:Wavetable) | -84.34 (Vital Audio: Vital) |
| `probe.nonharm96_db` | Non-harmonic energy at MIDI 96 (aliasing) | <= -80.00 dB | release | met | -83.26 (node:Analog) | -84.31 (Vital Audio: Vital) |
| `probe.nonharm108_db` | Non-harmonic energy at MIDI 108 (aliasing) | <= -80.00 dB | release | met | -81.20 (node:Analog) | -83.63 (Vital Audio: Vital) |
| `probe.attack_ms` | Default-patch attack 10-90 % (a design choice, compared not judged) |  | report | info | - | - |
| `probe.release_s` | Default-patch release to -60 dB |  | report | info | - | - |
| `probe.vel_span_db` | Level change velocity 0.25 -> 1.0 |  | report | info | - | - |
| `builtin.pitch_c` | Worst pitch error of a built-in instrument at its default patch, same MIDI through each app's own bounce (compared, not judged: defaults differ) |  | report | info | 41.45 (Infinite: Metallic) | - |
| `builtin.nonharm108_db` | Non-harmonic energy at MIDI 108 of a built-in instrument at its default patch (aliasing), per app |  | report | info | -3.66 (Infinite: FieldSynth) | - |

### P5 Audio quality and hosting: Effects

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `fx.dc_dbfs` | Mean DC left by an effect under noise, at defaults | <= -50.00 dBFS | release | met | -55.11 (node:Bitcrush) | - |
| `fx.tail_dbfs` | Level 10 s after the input goes silent | <= -100 dBFS | release | met | -240 (node:Audio Filter) | - |
| `fx.click_db` | Worst step transient when a parameter jumps | <= 12.00 dB | release | met | 9.00 (node:Dynamics) | - |
| `fx.rate_spread_db` | Tone level difference across 44.1/48/96 kHz | <= 1.00 dB | release | met | 0.996 (node:Key-Snap) | - |
| `fx.insert_gain_db` | Level change when the node opens: default mix, pink noise at -20 dBFS (v0.6 1.9) | <= 1.00 dB | release | exc | 10.82 (node:Shape Resonator) | - |
| `fx.hot_null_db` | Chorus and Flanger: residual between a -1 dBFS and a scaled -12 dBFS 1 kHz tone (level-dependent distortion; v0.6 1.10) | <= -60.00 dB | release | met | -143 (node:Flanger) | - |
| `builtin.fx_latency_ms` | Latency of a built-in effect after the app's own compensation (a bounce should read 0) |  | report | info | 0.188 (Infinite: AudioFilter) | - |
| `builtin.fx_thd12_db` | THD of a built-in effect at defaults, 1 kHz at -12 dBFS; effects with a tail over 0.5 s are not measured |  | report | info | -35.01 (Infinite: Drive) | - |

### P5 Audio quality and hosting: Plugin hosting

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `host.unexplained_crashes` | Installed plugins that crash or hang Infinite's host with no outside evidence the plugin is at fault (bad signature, auval crashes too) | <= 0 count | report | met | 0 (Infinite: AU) | - |
| `host.ok_pct` | Installed plugins that load and pass audio in Infinite's host, headless |  | report | info | 97.59 (Infinite: VST3) | - |

### P5 Audio quality and hosting: Timeline audio

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `stretch.extra_onsets` | Worst count of drum hits heard twice (flams, pre-echo) over 0.5/0.8/1.25/2x Timeline Sample stretch | <= 0 onsets | report | met | 0 (Infinite) | - |
| `stretch.timing_ms` | Worst drum hit against the median hit over the four stretch ratios | <= 5.00 ms | report | met | 3.25 (Infinite) | - |
| `stretch.crest_err_db` | Worst drum crest against the ideal stretch (hits at their own length, clipped to full scale) over the four ratios | <= 0.5 dB | report | exc | 2.50 (Infinite) | - |

### P6 Timeline and arrangement: Project files

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `project.load_s` | Headless load (parse, build every node, check) of the largest generated patch, process start included |  | report | info | 0.874 (Infinite: 4000 nodes) | - |
| `project.load_fail` | 1 when a generated patch of that size fails to load or to write back | <= 0 bool | report | met | 0 (Infinite: 10 nodes) | - |

### P7 3D, compositing, output: 3D engine

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `geo.instance_ms` | Per-frame cost of 10,000 instances on a 100x100 grid, scale animated (Infinite: render slope net of baseline; Blender: depsgraph eval, 2026-10-13) | <= 1.30 ms | report | behind | 3.08 (Infinite) | 1.26 (Blender) |
| `geo.subdiv_ms` | 32x32 grid, subdivide x2 smooth, animated noise displacement, per frame (Blender 2026-10-13 sets the target) | <= 8.10 ms | report | met | 7.19 (Infinite) | 8.14 (Blender) |
| `sim.cloth_ms` | 32x32 cloth sheet pinned on one edge, one simulation step per frame (solvers differ: Blender 5 quality steps, Infinite 6 iterations) | <= 18.40 ms | report | met | 0.422 (Infinite) | 18.40 (Blender) |

### P7 3D, compositing, output: Compositing

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `comp.blend_x255` | Blend modes against the W3C formulas, worst over modes | <= 1.00 x/255 | release | met | 0.125 (node:Blend) | - |

### P7 3D, compositing, output: Geometry

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `geo.winding_mismatch` | Triangles whose winding disagrees with their vertex normals | <= 0 triangles | release | exc | 14608 (node:Screw on sphere) | - |
| `geo.nonmanifold` | Non-manifold or one-direction edges on shapes that promise a closed mesh | <= 0 edges | release | met | 0 (node:Capsule) | - |
| `geo.cloth_stretch_pct` | Worst edge stretch of a pinned cloth after 12 s | <= 50.00 % | release | met | 1.39 (node:Cloth) | - |
| `geo.inst_mirrored` | Instance transforms with a negative determinant | <= 0 count | release | met | 0 (node:Instance on Points) | - |
| `geo.decimate_bbox_pct` | Decimate: worst vertex distance to the original surface | <= 5.00 % bbox | release | met | 0.851 (node:Decimate) | - |
| `geo.disp_offset` | Displacement: worst vertex off the formula (run with INFINITE_GEOAUDIT_ONLY=DISP) | <= 0.02 units | report | met | 0 (node:Displacement) | - |

### P8 Performance: Bench: audio

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b1_cb_load_p99` | B1 audio callback load p99, 24 voices, per buffer size (suite §6) | <= 50.00 % | release | met | 44.75 (bench:B1:buf256) | - |
| `bench.b1_xruns` | B1 xruns at 256 frames, the §6 buffer (64 is not a target: Found in Block 1) | <= 0 count | release | met | 0 (bench:B1:buf256) | - |
| `load.ms_per_track_s` | Audio-thread ms per second of audio per track (synth + effect, held 4-note chords, offline), cheapest track count |  | report | info | 68.36 (node:Analog+node:EQ) | - |
| `load.tracks_at_70pct` | Tracks of that synth + effect at 70 % of one core |  | report | info | 10.00 (node:Analog+node:EQ) | - |

### P8 Performance: Bench: canvas

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b6_pan_p50_ms` | B6 canvas frame p50 while panning a 200-400 node patch (>= 60 fps) | <= 17.20 ms | release | met | 16.67 (bench:B6:n300-all) | - |
| `bench.b6_pan_p95_ms` | B6 canvas frame p95 while panning a 200-400 node patch (>= 45 fps) | <= 22.80 ms | release | met | 16.69 (bench:B6:n200-pan) | - |
| `bench.r5_pan_p95_ms` | R5 canvas frame p95 while panning a 4000-node patch (>= 30 fps; proposed, ROBUSTNESS.md R5) | <= 33.30 ms | report | unmeasured | - | - |
| `bench.r5_select_p95_ms` | R5 canvas frame p95 while select-all and clear alternate on 4000 nodes (proposed) | <= 33.30 ms | report | unmeasured | - | - |
| `bench.r5_delete_all_ms` | R5 worst frame around deleting all 4000 nodes through the Delete-key path (proposed) | <= 500 ms | report | unmeasured | - | - |
| `bench.r5_nodes_after_delete` | R5 nodes left after the delete-all (0 = every node went) | <= 0 count | report | unmeasured | - | - |

### P8 Performance: Bench: fundamentals

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b5_startup_ms` | B5 launch to first frame swapped (proposed: under one second) | <= 1000 ms | release | met | 342 (bench:B5:startup) | - |
| `bench.b5_load_ms` | B5 patch load (real LoadPatchFrom), 50-400 nodes; proposed: inside one 60 Hz frame | <= 16.70 ms | release | met | 6.04 (bench:B5:loadsave:400) | - |
| `bench.b5_save_ms` | B5 patch save (real SavePatchTo), 50-400 nodes; proposed: inside one 60 Hz frame | <= 16.70 ms | release | met | 2.86 (bench:B5:loadsave:50) | - |

### P8 Performance: Bench: live edit

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b3_edit_late_frames` | B3 edit storm: edits (add, connect, delete, Field preset load) whose frame did over one refresh period of work or ran over 1.5 periods | <= 0 frames | release | unmeasured | - | - |
| `bench.b3_edit_work_ms_max` | B3 edit storm: most frame work any single edit cost (one 60 Hz period) | <= 16.70 ms | release | unmeasured | - | - |
| `bench.b3_edit_xruns` | B3 edit storm: audio dropouts while the graph is edited under live load | <= 0 count | release | unmeasured | - | - |

### P8 Performance: Bench: media I/O

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b8_decode_rate_ratio` | B8 decoded fps over the clip's own fps, per clip | >= 1.00 x clip fps | release | met | 1.01 (bench:B8:4x2160:clip3) | - |
| `bench.b8_dropped_frames` | B8 decoded frames dropped, per clip | <= 0 frames | release | met | 0 (bench:B8:2x1080+2w:clip0) | - |
| `bench.b8_window_p99_ms` | B8 projector window interval p99 (locked 60 fps), per window | <= 18.30 ms | report | behind | 33.21 (bench:B8:4x2160+3w+cam+syphon:w2) | - |
| `bench.b8_window_missed_vsync_pct` | B8 projector window intervals over 1.5 refresh periods, per window | <= 0.5 % | report | behind | 9.36 (bench:B8:4x2160+3w+cam+syphon:w0) | - |

### P8 Performance: Bench: memory

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.fbo_allocs_steady` | Render targets reallocated in steady state (B2/B4 scenes, also the B9 scenes) | <= 0 count | release | met | 0 (bench:B2:scale=l,anim=0) | - |
| `bench.b9_footprint_peak_mb` | B9 peak OS footprint, B2/B4 at scale l; budget = b2 baseline 760 MB + 20 % (compare.py change gate) | <= 912 MB | release | met | 760 (bench:B9:b2) | - |

### P8 Performance: Bench: offline render

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b10_realtime_factor` | B10 arrangement render of the B3 scene: seconds of take per wall second | >= 1.00 x realtime | release | met | 2.25 (bench:B10:30s-anim0) | - |
| `bench.b10_av_drift_worst_frames` | B10 worst A/V marker drift in the rendered movie (av-sync-sweep: 2 frames anywhere) | <= 2.00 frames | release | met | 0 (bench:B10:30s-anim0) | - |
| `bench.b10_av_drift_end_frames` | B10 A/V drift at the end of the take (av-sync-sweep: 1 frame at the end) | <= 1.00 frames | release | met | 0 (bench:B10:30s-anim0) | - |
| `render.movie_fps` | Movie render speed (render + encode) per scene and codec, from the slope between a 1 s and a 3 s render (render_bench) |  | report | info | 44.46 (Infinite: blur_2160p/prores4444) | 1.58 (Blender: blur_720p/prores4444) |
| `render.h264_psnr_db` | Luma PSNR of the H264 movie against the same frames written lossless (render_bench) |  | report | info | 35.47 (Infinite: instanced_720p) | 46.13 (Blender: blur_720p) |
| `render.sync_offset_ms` | Sound minus picture in a 10 minute rendered movie of flashes and clicks, absolute (render_bench) |  | report | info | 0.0417 (Infinite) | 0.0208 (Blender) |
| `render.sync_drift_ms_min` | Sync drift over the same 10 minute movie, absolute (render_bench) |  | report | info | 0 (Infinite) | 0 (Blender) |

### P8 Performance: Bench: projector

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b3_projector_p99_ms` | B3 projector present interval p99 under live load; locked 60 fps = 1.1 x 16.7 ms | <= 18.30 ms | release | met | 17.64 (bench:B3:s) | - |
| `bench.b3_missed_vsync_pct` | B3 projector intervals over 1.5 refresh periods (§6: under 0.5 %) | <= 0.5 % | release | met | 0 (bench:B3:s) | - |
| `bench.b3_input_to_photon_frames` | B3 injected param change to Output revision, worst sample | <= 2.00 frames | release | met | 1.00 (bench:B3:s) | - |

### P8 Performance: Bench: soak

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `bench.b7_rss_growth_pct` | B7 soak RSS growth, last 5 min vs first 5 min after warm-up | <= 2.00 % | report | unmeasured | - | - |
| `bench.b7_xruns` | B7 soak xruns over the whole run | <= 0 count | report | unmeasured | - | - |
| `bench.b7_thermal_fps_drop_pct` | B7 soak fps drop first to last sample (reported, not hidden: no target) |  | report | unmeasured | - | - |

### P8 Performance: Built-in instruments

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `synth.cpu_ns_frame` | Render cost per frame, one voice (noisy: report only) | <= 4000 ns/frame | report | met | 347 (node:Spectral Synth) | - |
| `probe.cpu_voice_ms_s` | Render ms per second of audio per voice, offline |  | report | info | 29.94 (node:Analog) | - |

### P8 Performance: Live scene

| Metric | What | Target | Gate | Status | Infinite worst | Best other |
|---|---|---|---|---|---|---|
| `live.frame_p95_ms` | p95 frame time of a real-time audio-reactive 10,000-instance scene at 1280x720 (TD logs its own; Infinite needs the GUI) | <= 16.70 ms | report | unmeasured | - | - |
| `live.drop_pct` | Frames over 1.5x the 60 fps budget in that scene | <= 1.00 % | report | unmeasured | - | - |

### Per subject

Every subject for metrics that miss, carry exceptions, have no direction, or compare against other apps.

- `av.react_jitter_ms` (ms): Infinite 9.33
- `probe.pitch_worst_c` (cents): node:Analog 0.173 · node:Wavetable 0.174 · Vital Audio: Vital 0.174 · Xfer Records: Serum 2 0.174 · u-he: Diva 7.73
- `probe.nonharm60_db` (dB): node:Analog -84.51 · node:Wavetable -84.16 · Vital Audio: Vital -84.34 · Xfer Records: Serum 2 -82.96 · u-he: Diva -40.80
- `probe.nonharm96_db` (dB): node:Wavetable -84.94 · node:Analog -83.26 · Vital Audio: Vital -84.31 · Xfer Records: Serum 2 -82.59 · u-he: Diva -27.00
- `probe.nonharm108_db` (dB): node:Wavetable -83.92 · node:Analog -81.20 · Vital Audio: Vital -83.63 · Xfer Records: Serum 2 -81.86 · u-he: Diva -10.35
- `probe.attack_ms` (ms): node:Analog 3.00 · node:Wavetable 4.00 · Vital Audio: Vital 1.00 · Xfer Records: Serum 2 2.00 · u-he: Diva 44.00
- `probe.release_s` (s): node:Analog 0.252 · node:Wavetable 0.263 · Xfer Records: Serum 2 0.018 · Vital Audio: Vital 0.084 · u-he: Diva 1.61
- `probe.vel_span_db` (dB): node:Analog 24.08 · node:Wavetable 24.08 · Vital Audio: Vital -0.0023 · Xfer Records: Serum 2 -0.0021 · u-he: Diva 12.92
- `builtin.pitch_c` (cents): Infinite: Analog 0.174 · Infinite: Wavetable 0.174 · Infinite: EquationSynth 0.174 · Infinite: Oscillator 0.174 · Infinite: FieldSynth 6.90 · Infinite: Metallic 41.45
- `builtin.nonharm108_db` (dB): Infinite: Wavetable -83.92 · Infinite: EquationSynth -83.89 · Infinite: Oscillator -82.14 · Infinite: Analog -81.20 · Infinite: Metallic -6.28 · Infinite: FieldSynth -3.66
- `fx.insert_gain_db` (dB): node:Dynamics 0 · node:EQ 0 · node:Stereo 0 · node:Transient Shaper 0 · node:Pitch Shifter 0.0001 · node:Limiter 0.0019 · node:Bitcrush 0.0518 · node:Delay 0.171 · node:Frequency Shifter 0.251 · node:Chorus 0.333 · node:Spectrum Slide 0.394 · node:Reverb 0.404 · node:Stutter 0.521 · node:Phaser 0.667 · node:Flanger 0.878 · node:Key-Snap 1.55 (exc F21) · node:Audio Filter 1.95 (exc F21) · node:Cycle Shaper 2.17 (exc F21) · node:Tremolo 2.72 (exc F21) · node:Ring Mod 3.02 (exc F21) · node:Resonator Bank 3.34 (exc F21) · node:Formant Filter 3.88 (exc F21) · node:Spec Blur 4.66 (exc F21) · node:Drive 5.31 (exc F21) · node:Wavetable Shaper 8.96 (exc F21) · node:Shape Resonator 10.82 (exc F21)
- `builtin.fx_latency_ms` (ms): Infinite: Chorus 0 · Infinite: Delay 0 · Infinite: Drive 0 · Infinite: Dry 0 · Infinite: Dynamics 0 · Infinite: EQ 0 · Infinite: Flanger 0 · Infinite: Limiter 0 · Infinite: Phaser 0 · Infinite: Reverb 0 · Infinite: AudioFilter 0.188
- `builtin.fx_thd12_db` (dB): Infinite: Phaser -110 · Infinite: Chorus -102 · Infinite: EQ -97.30 · Infinite: Dry -92.21 · Infinite: Dynamics -92.21 · Infinite: Limiter -92.21 · Infinite: Flanger -91.97 · Infinite: AudioFilter -90.76 · Infinite: Drive -35.01
- `host.ok_pct` (%): Infinite: AU 97.62 · Infinite: VST3 97.59
- `stretch.crest_err_db` (dB): Infinite 2.50 (exc F20)
- `project.load_s` (s): Infinite: 4000 nodes 0.874
- `geo.instance_ms` (ms): Infinite 3.08 (miss) · Blender 1.26
- `geo.subdiv_ms` (ms): Infinite 7.19 · Blender 8.14
- `sim.cloth_ms` (ms): Infinite 0.422 · Blender 18.40
- `geo.winding_mismatch` (triangles): node:8 balls r96 0 · node:Array on cube 0 · node:Array on sphere 0 · node:Arrow 3D 0 · node:Capsule 0 · node:Cone 0 · node:Cube 0 · node:Curve Ops hostile 0 · node:Curve Ops no input 0 · node:Curve source tube 0 · node:Cylinder 0 · node:Decimate Icosphere 0 · node:Decimate Torus 0 · node:Decimate on cube 0 · node:Decimate on sphere 0 · node:Delaunay 0 · node:Delete on cube 0 · node:Delete on sphere 0 · node:Disc 0 · node:Dodecahedron 0 · node:Explode on cube 0 · node:Explode on sphere 0 · node:Extrude on cube 0 · node:Extrude on sphere 0 · node:Gear 3D 0 · node:Helix 0 · node:Icosphere 0 · node:Join merge 0 · node:Merge by Distance 0 · node:Metaballs 0 · node:Metaballs extreme 0 · node:Mirror on cube 0 · node:Mirror on sphere 0 · node:Normals on cube 0 · node:Normals on sphere 0 · node:Ocean 0 · node:Octahedron 0 · node:Plane 0 · node:Prism 0 · node:Pyramid 0 · node:Rounded Cube 0 · node:Select on cube 0 · node:Select on sphere 0 · node:Smooth on cube 0 · node:Smooth on sphere 0 · node:Solidify on cube 0 · node:Solidify on sphere 0 · node:Sphere 0 · node:Star 3D 0 · node:Subdivide on cube 0 · node:Subdivide on sphere 0 · node:Supershape 0 · node:Tetrahedron 0 · node:Torus 0 · node:Torus Knot 0 · node:Transform on cube 0 · node:Transform on sphere 0 · node:Triangulate on cube 0 · node:Triangulate on sphere 0 · node:Tube 0 · node:Twist on cube 0 · node:Twist on sphere 0 · node:Voronoi 0 · node:Wireframe on cube 0 · node:Wireframe on sphere 0 · node:default 0 · node:node Offset 0 · node:node Resample 0 · node:node Simplify 0 · node:node Smooth 0 · node:one ball r24 0 · node:one ball r40 0 · node:one ball r96 0 · node:pinned 0 · node:realised 0 · node:Solidify on a plane 0 · node:Mobius Strip 2.00 (exc F16) · node:Klein Bottle 7.00 (exc F16) · node:Screw on cube 1456 (exc F18) · node:Screw on sphere 14608 (exc F18)
- `load.ms_per_track_s` (ms/s): node:Wavetable+node:Reverb 18.57 · node:Analog+node:EQ 68.36
- `load.tracks_at_70pct` (tracks): node:Wavetable+node:Reverb 34.00 · node:Analog+node:EQ 10.00
- `bench.b8_window_p99_ms` (ms): bench:B8:2x1080+2w:w1 17.69 · bench:B8:2x1080+3w:w2 17.69 · bench:B8:2x1080+3w:w1 17.70 · bench:B8:2x1080+2w:w0 17.72 · bench:B8:2x1080+3w:w0 17.78 · bench:B8:4x2160+3w+cam+syphon:w0 33.04 (miss) · bench:B8:4x2160+3w+cam+syphon:w1 33.09 (miss) · bench:B8:4x2160+3w+cam+syphon:w2 33.21 (miss)
- `bench.b8_window_missed_vsync_pct` (%): bench:B8:2x1080+2w:w0 0 · bench:B8:2x1080+2w:w1 0 · bench:B8:2x1080+3w:w0 0 · bench:B8:2x1080+3w:w1 0 · bench:B8:2x1080+3w:w2 0 · bench:B8:4x2160+3w+cam+syphon:w0 9.36 (miss) · bench:B8:4x2160+3w+cam+syphon:w1 9.36 (miss) · bench:B8:4x2160+3w+cam+syphon:w2 9.36 (miss)
- `render.movie_fps` (fps): Infinite: blur_720p/h264 263 · Infinite: instanced_720p/h264 201 · Infinite: blur_720p/prores4444 170 · Infinite: instanced_720p/prores4444 128 · Infinite: blur_1080p/prores4444 81.43 · Infinite: blur_1080p/h264 81.00 · Infinite: instanced_1080p/prores4444 80.23 · Infinite: instanced_1080p/h264 73.23 · Infinite: instanced_2160p/h264 60.15 · Infinite: instanced_2160p/prores4444 56.76 · Infinite: blur_2160p/h264 49.97 · Infinite: blur_2160p/prores4444 44.46 · Blender: blur_720p/prores4444 1.58 · Blender: blur_720p/h264 1.56 · Blender: blur_1080p/prores4444 1.17 · Blender: blur_1080p/h264 1.16 · Blender: instanced_720p/prores4444 0.883 · Blender: instanced_720p/h264 0.87 · Blender: instanced_1080p/prores4444 0.664 · Blender: instanced_1080p/h264 0.587 · Blender: blur_2160p/prores4444 0.47 · Blender: blur_2160p/h264 0.434 · Blender: instanced_2160p/prores4444 0.207 · Blender: instanced_2160p/h264 0.194
- `render.h264_psnr_db` (dB): Infinite: blur_1080p 51.81 · Infinite: blur_2160p 51.54 · Infinite: blur_720p 50.63 · Infinite: instanced_2160p 37.10 · Infinite: instanced_1080p 36.39 · Infinite: instanced_720p 35.47 · Blender: blur_720p 46.13 · Blender: blur_2160p 44.87 · Blender: blur_1080p 44.34 · Blender: instanced_2160p 32.16 · Blender: instanced_1080p 29.69 · Blender: instanced_720p 28.83
- `render.sync_offset_ms` (ms): Infinite 0.0417 · Blender 0.0208
- `render.sync_drift_ms_min` (ms/min): Infinite 0 · Blender 0
- `probe.cpu_voice_ms_s` (ms/s): node:Wavetable 3.03 · node:Analog 29.94

Rows from (UTC date, commit): 2026-10-12 49f240d7; 2026-10-13 0531853e; 2026-10-13 38e754be; 2026-10-13 4466dd20; 2026-10-13 6136354a; 2026-10-13 71643a34; 2026-10-13 c2f7d0d1; 2026-10-13 c64eb6fe; 2026-10-13 d6020b77; 2026-10-13 db820427; 2026-10-13 e182230c; 2026-10-14 7f002bee.

---

# Appendix D — Additional competitor runs (2026-10-17, M2 8 GB)

Run today with the repo's own harnesses (`tools/re-survey/`). All are black-box, outside-in measurements on the same machine. Caveats first:
- Plugins run at their **default state**. A default that does nothing (Pro-Q 4 flat EQ, Pro-L 2 under threshold, Infinite Limiter) measures as identical to the dry rig, so only its CPU column means something. THD/alias columns on reverbs and delays measure the tail (marked *), not distortion.
- The effects and synth probes ran at the same time, so the CPU columns carry extra noise; the harness says to treat differences under about 1 ms/s as noise.
- Phase Plant and Twin 3 produced silence or unusable output at their default state with no patch loaded; they are not comparable and should be ignored.
- The app bench ran while a second Infinite instance (the Desktop test build) was open, so Infinite's idle CPU may be overstated.
- No Windows/Linux competitors; macOS versions only.

## D1 Effects: Infinite nodes vs commercial plugins


| Target | Latency smp (ms) | Gain 1k dB | THD -12 | THD+N -12 | THD -1 | Alias HF dB | Flatness ±dB 63-16k | Tail -60 s | RT60 s | CPU ms/s (net) | Params |
|---|---|---|---|---|---|---|---|---|---|---|---|
| dry (rig floor) | 0 (0.00 ms) | -0.00 | -92.2 | -87.1 | -104.5 | -87.6 | 0.00 | 0.01 | - | 0.00 | - |
| node:Reverb | 0 (0.00 ms) | +4.29 | -114.4* | -43.2* | -17.6* | -30.3* | 0.66 | 0.60 | 1.48 | 8.77 | - |
| Valhalla DSP, LLC: ValhallaRoom | 547 (11.40 ms) | +0.33 | -93.9* | -32.3* | -23.6* | 5.2* | 20.70 | 1.68 | 1.86 | 20.47 | 22 |
| FabFilter: Pro-R 2 | 0 (0.00 ms) | -1.32 | -109.0 | -42.5 | -120.5 | -38.0 | 0.40 | 0.46 | 2.19 | 25.60 | 136 |
| node:EQ | 0 (0.00 ms) | -0.00 | -97.3 | -86.1 | -114.2 | -87.6 | 0.00 | 0.01 | - | 12.77 | - |
| FabFilter: Pro-Q 4 | 0 (0.00 ms) | -0.00 | -92.2 | -87.1 | -104.5 | -87.6 | 0.00 | 0.01 | - | 2.10 | 600 |
| node:Dynamics | 0 (0.00 ms) | -0.00 | -92.2 | -87.1 | -92.0 | -87.6 | 0.00 | 0.01 | - | 2.80 | - |
| FabFilter: Pro-C 2 | 0 (0.00 ms) | +0.69 | -70.6 | -70.5 | -64.7 | -80.7 | 0.00 | 0.01 | - | 3.23 | 46 |
| node:Limiter | 0 (0.00 ms) | -0.00 | -92.2 | -87.1 | -104.5 | -87.6 | 0.00 | 0.01 | - | 10.40 | - |
| FabFilter: Pro-L 2 | 0 (0.00 ms) | -0.00 | -92.2 | -87.1 | -104.5 | -87.6 | 0.00 | 0.01 | - | 9.90 | 32 |
| node:Delay | 0 (0.00 ms) | +3.01 | -63.4* | -35.4* | -20.8* | -25.7* | 0.26 | 1.51 | 1.87 | 2.20 | - |
| Valhalla DSP, LLC: ValhallaDelay | 0 (0.00 ms) | -0.19 | -45.9* | -12.0* | -25.6* | -17.1* | 4.41 | 2.72 | 3.85 | 7.43 | 38 |
| node:Drive | 0 (0.00 ms) | +5.54 | -35.0 | -35.0 | -16.6 | -89.1 | 0.97 | 0.02 | - | 19.93 | - |
| FabFilter: Saturn 2 | 0 (0.00 ms) | +0.72 | -40.7 | -40.7 | -18.4 | -32.3 | 1.25 | 0.02 | - | 5.87 | 951 |
| node:Chorus | 0 (0.00 ms) | -0.18 | -102.4 | -11.9 | -24.4 | -7.2 | 4.27 | 0.02 | 0.10 | 3.57 | - |
| Kilohearts: kHs Chorus | 0 (0.00 ms) | -1.77 | -107.9 | -55.6 | -116.0 | -4.1 | 4.00 | 0.01 | 0.09 | 0.03 | 7 |
| node:Flanger | 0 (0.00 ms) | -6.00 | -92.0 | -21.1 | -37.8 | -8.2 | 4.08 | 0.05 | 0.05 | 3.23 | - |
| Kilohearts: kHs Flanger | 0 (0.00 ms) | -1.69 | -107.5 | -86.5 | -26.2 | -5.6 | 17.73 | 0.01 | - | 0.10 | 10 |
| node:Phaser | 0 (0.00 ms) | +1.69 | -110.5 | -88.7 | -92.6 | -88.6 | 9.51 | 0.01 | - | 2.17 | - |
| Kilohearts: kHs Phaser | 0 (0.00 ms) | -6.76 | -102.7 | -34.6 | -116.1 | -78.0 | 12.25 | 0.01 | - | 0.97 | 7 |


Readings:
- **Latency:** Infinite effects report 0 ms after compensation (see the scoreboard). ValhallaRoom reports 11.4 ms.
- **Reverb:** Infinite Reverb 8.8 ms/s CPU against Pro-R 2 25.6 and ValhallaRoom 20.5, with RT60 1.48 s against 2.19 and 1.86. The Infinite reverb adds +4.3 dB at 1 kHz and clips at defaults (peak +5.9 dBFS); Pro-R 2 is -1.3 dB and ValhallaRoom +0.3 dB. That level jump is the known `fx.insert_gain_db` exception on the scoreboard.
- **Delay:** Infinite Delay 2.2 ms/s vs ValhallaDelay 7.4; Infinite adds +3 dB at defaults (peak +1.7 dBFS), ValhallaDelay -0.2 dB.
- **Drive/saturation:** Infinite Drive THD -35 dB vs Saturn 2 -40.7 dB at -12 dBFS (both are deliberate distortion); Infinite Drive's high-frequency alias floor is cleaner (-89.1 vs -32.3 dB) but costs 19.9 ms/s against 5.9.
- **Modulation:** Infinite Chorus, Flanger and Phaser use more CPU than the Kilohearts equivalents (3.6/3.2/2.2 vs 0.03/0.1/1.0 ms/s). Infinite Chorus and Flanger are louder-than-input at defaults and clip; Kilohearts Flanger has a deep notch around 250 Hz in the sweep. These are not like-for-like patches.
- **Dynamics:** Infinite Dynamics 2.8 ms/s vs Pro-C 2 3.2; at default Pro-C 2 distorts more (THD -70.6 dB vs the rig floor) while Infinite Dynamics matches the dry floor.

## D2 Instruments: black-box synth probe


| Target | Octave | Pitch err ¢ (worst) | Non-harm 60 | Non-harm 96 | Non-harm 108 | Onset ms | Attack ms | Release -60 s | Vel span dB | L/R corr | Same twice | CPU ms/s idle / voice |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Kilohearts: Phase Plant | - | - | - | - | - | - | - | - | 0.0 | - | yes | 10.00 / -0.12 |
| FabFilter: Twin 3 | +0 | 4.6 | -13.8 | -12.6 | -7.4 | 0.0 | 289 | 2.58 | 7.5 | 0.87 | no (-9 dB) | 25.30 / 6.05 |
| Vital Audio: Vital | +0 | 0.2 | -84.3 | -84.3 | -83.6 | 0.0 | 1 | 0.08 | -0.0 | 1.00 | yes | 11.50 / 2.36 |
| Xfer Records: Serum 2 | +0 | 0.2 | -83.0 | -82.6 | -81.9 | 0.0 | 2 | 0.02 | -0.0 | 1.00 | yes | 8.90 / 4.03 |
| u-he: Diva | -1 | 7.8 | -40.8 | -27.0 | -10.4 | 0.0 | 44 | 1.61 | 12.9 | 0.99 | yes | 10.53 / 22.64 |
| node:Wavetable | +0 | 0.2 | -84.2 | -84.9 | -83.9 | 0.0 | 4 | 0.26 | 24.1 | 1.00 | yes | 8.30 / 1.31 |
| node:Analog | +0 | 0.2 | -84.0 | -82.9 | -81.1 | 0.0 | 3 | 0.25 | 24.1 | 1.00 | yes | 8.13 / 8.57 |
| node:Oscillator | +0 | 0.2 | -74.0 | -82.1 | -82.1 | 0.0 | 3 | 0.26 | 24.1 | 1.00 | yes | 8.40 / 3.35 |


Reading: Wavetable and Analog match Vital and Serum 2 on pitch (0.2 cents worst case) and aliasing (about -81 to -85 dB non-harmonic). Infinite's per-voice CPU: Wavetable 1.3, Oscillator 3.4, Analog 8.6 ms/s; Vital 2.4, Serum 2 4.0, Diva 22.6. Infinite's instruments respond to velocity (24 dB span), while Vital and Serum 2 ignore it at their default patches.

## D3 Whole-app footprint: launch, idle CPU, memory


| App | Window s (1st) | Window s (warm) | Idle CPU % mean / max | Memory MB (footprint) | Threads | Processes | Bundle MB |
|---|---|---|---|---|---|---|---|
| Ableton Live 12 Suite | 4.7 | 3.1 | 41.9 / 49 | 672 | 76 | 5 | 8021 |
| Bitwig Studio | 7.8 | 6.2 | 17.6 / 110 | 1392 | 127 | 4 | 874 |
| Logic Pro Creator Studio | 2.5 | 1.3 | 3.9 / 8 | 122 | 27 | 1 | 2194 |
| TouchDesigner | 4.2 | 2.5 | 49.8 / 57 | 1396 | 30 | 1 | 1707 |
| VCV Rack 2 Free | 1.9 | 0.8 | 521.7 / 609 | 264 | 15 | 1 | 42 |
| Blender | 3.5 | 2.7 | 2.8 / 4 | 696 | 33 | 1 | 879 |
| Vital | 3.2 | 1.4 | 0.4 / 1 | 248 | 11 | 1 | 24 |
| BespokeSynth | 6.8 | 2.1 | 26.2 / 31 | 1528 | 13 | 1 | 268 |
| Infinite | 0.8 | 0.8 | 10.7 / 13 | 588 | 17 | 1 | 74 |


Reading (idle, nothing loaded; Infinite row re-measured 2026-11-21, 3 runs, 20 s idle window, dev build): Infinite opens a window in 0.8 s cold / 0.8 s warm, uses 588 MB and has the smallest bundle of the full workstations (74 MB, against 874 MB to 8 GB for DAWs and TouchDesigner). Memory is below Ableton (672 MB), Blender (696 MB), Bitwig (1.4 GB), TouchDesigner (1.4 GB) and BespokeSynth (1.5 GB); only Logic (122 MB) and the Vital plugin (248 MB, a single synth) are comparable or lower. **Infinite's idle CPU is 10.7% of a core, which is higher than Logic (3.9%), Blender (2.8%) and Vital (0.4%) and below Ableton (41.9%), TouchDesigner (49.8%) and VCV Rack (521.7%, it renders its audio engine continuously).** The Infinite row was re-measured on a quiet machine with no second instance running.

## D4 Not run

- Audio-callback load at matched track counts in Ableton, Bitwig or Logic; real round-trip latency (needs BlackHole loopback); movie render speed and live visual frame times against TouchDesigner (needs a manual Textport step); stretch and bounce comparisons (need hand-bounced files from each DAW). These tools exist (`bounce_kit.py`, `stretch_kit.py`, `latency_probe.py`, `render_bench.py td`, `av_bench.py td`) but need manual steps in each app.
