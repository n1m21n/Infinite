---
title: "Flicker-free music-driven visuals: predicting temporal aliasing in Field Pixel kernels"
slug: "flicker-free-pixels"
status: "signed-off"
owner: "namansoni"
date: "2026-11-23"
prior_art: "docs/prior-art/2026-11-23-flicker-free-pixels.md"
commit_sha: "pending-measurement"
---

# Flicker-free music-driven visuals

**Signed off by the owner on 2026-11-23, as written, with D1-D4 as recommended. Sections 1-7 are locked
by this commit.** Sections 8-12 are filled after sign-off. Paired study: `docs/research/stable-modulation`
(branch `feature/stable-modulation`). This is the visual twin of `docs/research/aliasing-aware-field`:
the same question (how much does this code widen the spectrum?) asked about **time** in a pixel kernel.

## 1. Goal

| # | Goal | Output |
|---|---|---|
| G1 | From a Field Pixel kernel's code, and the modulators wired into its knobs, predict how much it will flicker / strobe at a given frame rate | One predicted flicker level per kernel per frame rate |
| G2 | Pick the cheapest number of sub-frame samples (a "shutter") that keeps flicker under a target | A factor F in {1, 2, 4, 8, 16} per kernel |
| G3 | Show the difference on real output: point-sampled vs predicted-shutter clips, side by side | Contact sheet + clips at 60 fps live and 24/30 fps export |
| G4 | Publish the same way whether positive or null | README 1-12, scripts, data, a Web Lab Note |

Product line this earns, if it holds: visuals stay clean on a projector and in exports even when fast
music drives them, with no setting to tune.

## 2. Problem

**What fails.** A pixel kernel is a function of space and time, f(uv, t, knobs). Infinite samples it once
per frame: one `t` per frame goes to the shader (`src/nodes/FieldPixelNode.cpp:1403`) and nothing averages
inside the frame. Anything that changes faster than half the frame rate folds back: wheels turn backwards,
fast stripes crawl the wrong way, audio-rate pulses become slow random flicker. Music makes it worse: a
knob driven by a 45 Hz LFO, a fast envelope or an audio follower is shown at 60 fps as a 15 Hz wobble
that isn't in the music. Exports at 24 or 30 fps fold even more.

**Prior art** (full table: `docs/prior-art/2026-11-23-flicker-free-pixels.md`):

| Source | What it shows | Gap this study fills |
|---|---|---|
| Dorn, Barnes, Lawrence, Weimer, PG 2015 | Band-limited procedural shaders computed analytically, in **space** | No time dimension |
| Yang & Barnes, EG 2018 | A compiler smooths shader programs with a Gaussian in **space** (input σ = half a pixel); "animation" appears only as test shaders rendered per frame | Time is not smoothed; no factor is predicted; no external modulation |
| Yang, Nehab, Sander, "Amortized Supersampling" | Reuses shading across frames for procedural shaders; says it cannot handle arbitrary temporal changes | Reuse, not prediction; breaks exactly on fast change |
| Engine TAA and motion-vector blur (Unity, Unreal docs and forums) | Temporal accumulation along motion vectors; animated textures blur or ghost under TAA | No motion vectors exist for a procedural pattern changing in place |
| Stroboscopic-effect background (Blur Busters; wagon-wheel effect) | Explains the artefact | No method |

Not found: a compiler or tool that predicts temporal aliasing of a procedural kernel from its code,
including inputs modulated by audio, and picks a shutter from it. Absence in a search is not proof of
absence.

## 3. Scope

| In | Out |
|---|---|
| Field Pixel kernels: `t`, `dt`, `frame`, `age`, knobs (params) and how they are modulated | Formula and Shadertoy nodes (GLSL, not Field) |
| Temporal aliasing only: the oracle uses the same pixel centres, so spatial aliasing cancels out | Spatial bandlimiting (covered by Dorn 2015, Yang & Barnes 2018) |
| Modulated knobs: LFO, envelope, audio follower, beat clock, with their bandwidth read from the upstream node | Image / video inputs (already sampled content) |
| Frame rates 60 (live), 30 and 24 (export) | Kernels with pixel `state` (ping-pong feedback): sub-frames would change their meaning |
| A research rig that renders sub-frames and averages them (test code), then report-only prediction | Turning a shutter on in the live app or the exporter: an enhancement that needs owner approval after the results |
| | 3D render path (`Render3D`), which has its own motion blur (realtime-engine G2) |

## 4. Existing constraints

| Constraint | Consequence |
|---|---|
| M2, 8 GB, one GPU shared with the app | Oracle renders at 640x360 for the corpus; 1080p only for the clips and the cost numbers (H2) |
| Pixel kernels compile to GLSL only; no CPU evaluator for the pixel domain was found | The predictor works on Field IR (static), not by running the interpreter as the audio study did. A CPU evaluator would be new code (trajectory choice, section 9) |
| Headless `--frame <patch> <t> <png>` renders any time t | The oracle can render sub-frame times on the real GPU path without new app code; a batched self-test rig is faster and is phase 0 work |
| How modulated knobs are sampled per frame (value at frame time, or something else) is **not yet checked** | Phase 0 confirms it in code before any modulated kernel is scored |
| Pixel `state` is capped (4 declarations) | `state` kernels are out of scope anyway |
| Research standard | Self-written presets are calibration only; "confirmed" needs a held-out set |
| Owner rule: show outputs, not scores | Every result ships with the clips and a contact sheet, plus a blind check (H4) |

**Threats known before data**
- A dB error is a proxy for what an eye sees; H4 is the check that it matches.
- Display-referred (sRGB) vs linear-light averaging gives different "ideal" frames; fixed in D2.
- Modulator bandwidth read from a node (e.g. LFO rate) can be wrong when that node is itself modulated;
  such chains are flagged and scored separately.

## 5. Hypothesis

Time enters a pixel kernel through a few roads: `t` (and `frame`, `age`) and modulated knobs. Each
operation widens the temporal spectrum in a way the code shows: `sin(w*t)` puts energy at w; `uv + v*t`
turns spatial frequency into temporal frequency at speed v; `fract`, `floor`, `step` and comparisons make
jumps whose tails fall about 6 dB per octave; smooth functions fall fast. The aliasing study confirmed this
smoothness-order rule for audio (174 of 174 informative kernels). The same rule applied to time, plus the
bandwidth of each modulator, predicts the flicker and the shutter factor needed.

| ID | Hypothesis | Metric and bar | Kill criterion | Held-out validation |
|---|---|---|---|---|
| H1 | **The analysis predicts** | Predicted factor F̂ equals the oracle's needed F, or is one step above, on ≥ 90% of informative kernels, and below it on ≤ 5%. Spearman ρ ≥ 0.8 between predicted and measured flicker at F = 1 | < 90%, or > 5% under, or ρ < 0.8 | Blind set B, scored once |
| H2 | **Per-kernel choice pays** | With the target met on every kernel, rendering at F̂ costs ≥ 40% less GPU time (geometric mean) than the smallest fixed F that meets it on all kernels | < 40% saving | Sets L, G, B at 1080p |
| H3 | **Reading the modulators matters** | On the modulated set M, the modulation-aware predictor meets H1's bar, and beats a modulation-blind one (knobs assumed constant) by ≥ 20 points of "right or one above" | < 20 points better | M built from B kernels |
| H4 | **People can see it** | Owner, blind, 20 clip pairs (point-sampled vs F̂ shutter, random sides): picks the shutter clip as less flickery ≥ 15 / 20 (binomial p = 0.021) | < 15 / 20 | 20 kernels from B, not seen before |

## 6. Baseline

Recorded once, before the predictor is written, at a pinned commit. Not re-measured later.

**Corpora**

| Set | What | Size | Role |
|---|---|---|---|
| P | Shipped Field Pixel presets that use `t` | all of them | Calibration; also "how much do our own presets flicker today" |
| L | Test shaders described in the Dorn and Yang & Barnes papers (checkerboard with ripples, animated sine wave, animated ripples, fire), rewritten in Field from the papers' descriptions | ~10 | Calibration |
| G | Generated kernels: random compositions of `sin`, `fract`, `floor`, `step`, `smoothstep`, `abs`, `mod` over `uv` and `t`, with speeds from slow to past Nyquist | ≥ 200, fixed seed | Calibration |
| B | Blind: same generator, different seed and settings (D4), generated after the lock | ≥ 200 | Held-out |
| M | B kernels with 1-2 knobs driven by an LFO (0.1-200 Hz), an envelope, an audio follower on real clips, or the beat clock | ≥ 100 | H3 |

**Oracle.** For each kernel and frame rate, render 64 sub-frames per frame on the real GPU path and
average them with the shutter in D1. Flicker at factor F = RMS over pixels and 4 s of frames of
(F-sample render − oracle), in display values 0-1, as dB. "Needed F" = smallest F in {1, 2, 4, 8, 16}
under the target (D3). "Informative" = kernels whose needed F is not 1 at every frame rate.

**Baseline numbers recorded**

| # | Number | Why |
|---|---|---|
| B1 | Flicker at F = 1 (today) for every kernel at 60, 30, 24 fps | The size of the problem, incl. our own presets |
| B2 | GPU ms per frame at F = 1, 2, 4, 8, 16, 1080p, per kernel | Denominator for H2 |
| B3 | The smallest fixed F that meets the target on all of L+G, and on B | H2's competitor |
| B4 | How modulated knobs reach the shader per frame (code reading, `file:line`) | Fixes the model of modulated inputs before M is scored |

## 7. Success metric to be achieved

| Hypothesis | Bar | Verdict rule |
|---|---|---|
| H1 | ≥ 90% right or one above, ≤ 5% under, ρ ≥ 0.8 | Confirmed only if B also passes |
| H2 | ≥ 40% less GPU time than the best fixed F | On B at 1080p |
| H3 | ≥ 20 points better than modulation-blind, and H1's bar on M | Both |
| H4 | ≥ 15 / 20 blind picks | One session, owner only |

The study ships as research when H1 and H4 pass. H2 decides whether per-kernel choice is worth it over one
fixed shutter (it may not be, as in the audio study). H3 decides whether the Infinite-specific part
(reading modulators) is needed.

**Deliverables**
- `data/`: per kernel and frame rate: flicker at each F, needed F, predicted F̂, GPU ms.
- Contact sheet of the 20 worst kernels (point-sampled vs F̂, stills and clips), and the H4 clip pairs.
- `scripts/`: generator, oracle rig, scorer, table builder; `kick-the-tires.sh` under 5 minutes.
- A Web Lab Note with the before/after clips.

## Decisions needed at sign-off

| ID | Decision | Recommendation |
|---|---|---|
| D1 | Oracle shutter | 360° box (the whole frame interval), as live visuals have no film convention; 180° also reported for the exports |
| D2 | Where sub-frames are averaged | In linear light (as light mixes in a camera), then encoded to sRGB; error measured on display values |
| D3 | Flicker target | −40 dB RMS (about 2.5 / 255 of an 8-bit grey level). H4 checks whether that is the visible line |
| D4 | Blind set B | Seed 20261123, up to 6 ops deep (G stops at 4), 30% of kernels with `uv + v*t` motion terms |

## 8. Best and optimised methodology
*(After sign-off.)*

## 9. Proposed solution trajectories
*(After sign-off.)*

## 10. Benchmarking against the baseline
*(After measurement.)*

## 11. Critique
*(After measurement.)*

## 12. Next steps
*(After measurement.)*
