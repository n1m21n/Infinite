# Brain Light: every pixel from brain data

Status: **§1–7 signed off by the owner 2026-11-23; §9 option A chosen (a mode on Brain Source's `image` pin).** Branch `feature/bci-brain-light`.
Parent plan: `docs/plans/bci/STRATEGY.md` (Blocks A–C done there).

## 1. Goal

Turn all of the incoming brain data into a picture from scratch: each pixel's colour and brightness comes
from the signal, with no authored shape, shader or preset in between.
The 24 brain pins and Brain Sculpt then work as the **control layer** on top of it.

## 2. Problem

| Today | Why it misses the goal |
|---|---|
| The visuals in the demo patch come from a Field Pixel bell someone wrote. The brain only turns its knobs. | The look is authored. The brain is a hand on the knobs, not the source of the picture. |
| The `image` pin is a 4-row 1–40 Hz spectrogram plus a state strip. | It is raw data, but it reads as a lab readout, not a picture. The owner could only read it through the strip. |
| The picture uses band **power** only (4 sensors × 40 bins). | Phase, the waveform itself, blinks and jaw (EMG), heart pulse and sensor-to-sensor timing are thrown away. |
| The 24 features (alpha, blink, jaw, gaze…) never reach the pixels. | They exist only as knob signals. |

## 3. Scope

| In | Out |
|---|---|
| One fixed, explainable **data → light law**: what each part of the signal becomes on screen | Decoding emotion, attention or images from thought (ruled out on a 4-sensor headset, STRATEGY §11) |
| Python mirror on real recordings first (HEROIC Muse 2, PhysioNet, EEG-Eyeblinks; all already on disk) | A headset session (none on hand, as before) |
| GPU drawing: the worker thread sends small arrays, a shader draws the pixels | Any CPU per-pixel rendering |
| The 24 pins + Brain Sculpt as the control layer (trail, zoom, mix of layers) | New brain features or new pins beyond what the law needs |
| Where it lives in Infinite (see §9: a mode on Brain Source's `image` pin vs a new node) | Building before sign-off |

## 4. Existing constraints

- M2 8 GB: one build at the end of the block, through `scripts/ibuild.py`.
- Pixels on the GPU; the worker thread stays at 20/64 Hz off the audio and render threads.
- Muse is weak: 4 dry sensors, low SNR (Muse vs gel r = 0.52–0.61). The law must still look alive on a flat signal and not just show noise.
- Data available: 4 channels at 256 Hz (AF7, AF8, TP9, TP10), the 4×40 spectrum, Hilbert phase (already in the audio pin), the calibrated eyes-closed score, and 24 features.
- A new node or mode needs owner approval (this doc is the request).
- Brain Source pin indices are saved in patches: never reorder; only append.

## 5. Hypothesis

> A picture where **position = where on the head**, **colour = which rhythm**, **brightness = how strong**
> and **motion = the wave's own phase** is readable by eye and feels like *your* brain, without any authored art.

The law (each row is pure data, nothing drawn by hand):

| Picture property | Comes from | Why |
|---|---|---|
| **Where** | The 4 sensors placed at their real head positions (forehead left/right, behind left/right ear); every pixel blends the sensors near it | Region matters: TP10 alpha was ×4.63 vs ×2.78 averaged |
| **Colour (hue)** | The spectrum at that point: 1–40 Hz mapped linearly onto the colour wheel (delta = red, theta = orange, alpha = gold, beta = green, gamma = blue-violet); mixed by band power | One rhythm, one colour, so eyes-closed alpha turns the picture gold |
| **Saturation** | How peaked the spectrum is (low spectral entropy = one clear rhythm = vivid; noise = grey) | Separates a real rhythm from noise without labels |
| **Brightness** | Calibrated power (z vs the person's own first 30 s) | Same normalisation the image pin uses |
| **Motion** | Each sensor's instantaneous phase sends rings outward from its spot at the wave's own frequency (slowed for the eye) | The waveform itself becomes visible; two sensors in sync make standing interference patterns |
| **Flashes / tears** | Raw broadband bursts: blinks (AF7/AF8 big slow swing) and jaw clench (TP9/TP10 high-frequency EMG) show up on their own, at their own sensors | They are in the raw signal, so the law doesn't need special cases |
| **Pulse** | Heart pulse feature, a faint global breathing of brightness | The only feature not in the raw 4 channels |

Control layer (the 24 pins, Brain Sculpt and normal knobs) changes *how* the law is shown (trail length,
slow-down factor, zoom, symmetry, layer mix), never *what* the data is.

## 6. Baseline (recorded once, 2026-11-23; not re-measured later)

| Measure | Value |
|---|---|
| Data reaching pixels without an authored shader | 4 ch × 40 bins power only (image pin) |
| Phase, raw waveform, EMG, pulse in the picture | none |
| Raw image readable by proxy (open vs closed) on HEROIC | 0.74 (STRATEGY Block B) |
| Owner blind test on the picture | 10/10, but only by reading the calibrated strip first |
| Demo patch picture | authored Field Pixel bell modulated by `alpha` (`bci_alpha_bell.inf`) |
| Flat-line input (all zeros) | spectrogram goes dark; demo bell still draws a bell |

## 7. Success metrics

| # | Metric | Bar |
|---|---|---|
| L1 | **No authored art.** Zero input gives a black frame; swapping two sensors' data swaps their regions; same data gives the same frame | all three hold (automated test) |
| L2 | **Readable without the strip.** Blind 2AFC eyes open vs closed on the Brain Light picture alone, 10 HEROIC pairs | owner ≥ 9/10; proxy ≥ 0.80 over all HEROIC sessions |
| L3 | **States look different.** Blind 3-way pick: eyes closed / blink / jaw clench (EEG-Eyeblinks + HEROIC) | owner ≥ 8/10 |
| L4 | **Alive at rest.** Eyes-open, sitting still: the picture still moves | mean frame-to-frame change > 1% of pixels, never a frozen frame for 1 s |
| L5 | **Cheap.** GPU draw at 1920×1080 on the M2 | < 0.5 ms per frame; audio thread untouched |
| L6 | **Art, not a readout.** Owner looks at the calm / focus / blink sheet | owner says yes |

## 8. Methodology

1. **Python mirror first** (`tools/bci/brain_light.py`): same law, drawn with numpy from the real CSVs. No build.
2. Render proof sheets: eyes open vs closed, blink, jaw, flat line, swapped sensors (L1, L4, L6).
3. Blind set like M3 (seeded pairs + key kept apart), owner picks (L2, L3); proxy observer for the 0.80 bar.
4. Keep only if it beats the baseline; stop after 2 rounds of law changes with nothing new (owner rule).
5. Then C++: worker thread packs a small per-frame buffer (4 × 40 spectrum, 4 phases, 4 amplitudes, burst flags);
   one fragment shader draws it. One build at the end. Selftest for L1, timing for L5.

## 9. Solution trajectories

Where does it live in Infinite? **Needs your approval.**

| Option | Pros | Cons |
|---|---|---|
| **A. A mode on Brain Source's `image` pin** (Spectrogram / Light) — *recommended* | No new node; extends what exists (owner prefers extending); patch wiring unchanged | Brain Source body gets one more control |
| B. New node "Brain Light" fed by Brain Source | Clean separation; its own knobs for the control layer | New node on the canvas; duplicates data plumbing |
| C. Field Pixel preset reading `brain.*` | No C++ | Authored shader again: misses the goal; no phase or raw access |

What-ifs:

| If | Then |
|---|---|
| L2 fails on Muse (signal too weak) | Raise slow-down and smoothing; if still < 0.80, ship as "works on stronger headsets" and say so |
| It looks like noise (L6 no) | Give the law more inertia (longer trails) before adding anything authored |
| Phase rings are too fast to read | Fixed slow-down ×(1/20): alpha 10 Hz → 0.5 Hz rings |

## 10. Benchmark vs baseline

Python mirror, 2 rounds of law changes plus one exposure fix (2026-11-23).
Tool: `tools/bci/brain_light.py` (`sheet`, `checks`). Data: HEROIC Muse 2, 63 sessions, 126 pairs.
Sheets: `proof/brain_light/brain_light_open_closed.png`, `proof/brain_light/brain_light_bursts.png`.
Clips (not in the repo, large): `~/Infinite/media/bci-brain-light/*.mp4`.

| Metric | Bar | Result | vs baseline (§6) |
|---|---|---|---|
| L1 zero input → black | yes | **pass** (max pixel 0) | demo bell drew a bell on zero input |
| L1 same data → same frame | yes | **pass** | — |
| L1 swap left/right sensors → mirrored picture | yes | **pass** (99th pct pixel diff 10/255, grain only) | — |
| L2 proxy: more gold light with eyes closed | ≥ 0.80 | **fail: 0.675** (85/126) | raw spectrogram proxy was 0.74 |
| L2 owner blind | ≥ 9/10 | not run yet | 10/10 only via the strip |
| L3 eyes closed / blink / jaw 3-way | ≥ 8/10 | not run; no labelled jaw data | — |
| L4 alive at rest | > 1% pixels change, never frozen 1 s | **mixed**: 47% mean (min session 16.5%), but one 1.36 s near-frozen run | — |
| L5 GPU < 0.5 ms | — | not measured (no C++ yet) | — |
| L6 owner: art, not a readout | yes | waiting for the owner | — |

Rounds:
1. Per-bin excess clipped at 1, EMG grain at full strength: the grain covered everything; clipping flattened
   the spectrum, so blinks were green-white instead of red.
2. Unclipped excess, colour weighted by excess², grain only well above baseline: blinks turn the forehead red,
   noise gone; too dark. Exposure gain raised (`tanh(mean/0.6)`), not a law change.

Reading the sheets: eyes closed is brighter than eyes open in most sessions, but the colour is not reliably
gold. On Muse the alpha excess is often smaller than other bins' excess, so the colour wanders (teal, red, blue).

## 11. Critique

- Hue from frequency is a choice, not a law of nature; it is fixed and stated, so it is honest, but someone chose it.
- 4 sensors give a very coarse map; most pixels are blends. The picture will be soft by nature.
- EMG and blinks may dominate the picture over real brain rhythms. That is true to the data but may look "wrong".
- L6 is taste; the other bars keep it from being only taste.
- **Results (§10): the raw law alone does not carry eyes-closed on Muse** (0.675 < 0.80, below the raw
  spectrogram's 0.74). This matches STRATEGY §11: without calibration Muse alpha is near a coin flip for half
  of people. The calibrated eyes-closed score (Block A) is what made M3 readable.
- The "EMG" burst found by searching HEROIC is the biggest fast excess on TP9/TP10, not a labelled jaw clench.
- Rings dominate the look in every state; the geometry may read as one template.

## 12. Next steps

1. ~~Owner signs off §1–7 and picks A/B/C in §9.~~ Done 2026-11-23: option A.
2. Python mirror + proof sheets (no build).
3. Blind sets, owner picks.
4. C++ + shader, one build, selftests, then the deck slide.
