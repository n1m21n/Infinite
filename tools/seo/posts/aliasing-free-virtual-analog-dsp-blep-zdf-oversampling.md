---
title: Aliasing-free virtual analog DSP: BLEP, 4x oversampling, and ZDF
seo_title: Aliasing-Free Virtual Analog DSP | Infinite
description: How Infinite's Analog synth combines 24-tap BLEP/BLAMP oscillators, 4x half-band oversampling, ADAA drive, and 4-lane SIMD ZDF filters below -80 dB aliasing.
date: 2026-11-24
category: product
tags: [virtual analog, audio dsp, blep oscillator, zdf filter, oversampling]
keywords: virtual analog dsp, blep blamp oscillator, zero delay feedback filter, oversampling adaa, modular synth c++
image: assets/blog_aliasing-free-virtual-analog-dsp-blep-zdf-oversampling.png
image_alt: Technical DSP pipeline diagram of Infinite's Analog synth showing 24-tap narrow-band BLEP/BLAMP oscillators, ADAA tanh drive, 4x half-band oversampling, and 4-lane SIMD ZDF filter
status: published
---

Open-source audio engines and header-only C++20 synthesis libraries are drawing renewed focus to virtual analog DSP. Projects such as `DSPark` and modular DAW proposals like `suhina-daw` highlight a recurring engineering trade-off: building oscillators, saturators, and resonant filters from clean mathematical blocks is straightforward at low frequencies, yet high MIDI notes and driven filters expose harsh non-harmonic foldover that ruins mix clarity.

In [Infinite](https://infinitedevices.org/), the **Analog** node (`AnalogNode` backed by `AnalogSynthCore`) solves that chain end to end on the audio thread. Rather than relying on a single anti-aliasing trick, it pairs narrow-band 24-tap windowed-sinc BLEP/BLAMP oscillators (`BlepOsc`), a `4x` linear-phase half-band oversampler (`Oversampler`), antiderivative-antialiased (`FastTanhAdaa`) pre-filter drive, and 4-lane SIMD zero-delay feedback (ZDF) filters (`FilterModels`), keeping non-harmonic aliasing below `-80 dB` across `C4`, `C7`, and `C8` saw waves.

![Technical DSP pipeline diagram of Infinite's Analog synth showing 24-tap narrow-band BLEP/BLAMP oscillators, ADAA tanh drive, 4x half-band oversampling, and 4-lane SIMD ZDF filter](https://infinitedevices.org/assets/blog_aliasing-free-virtual-analog-dsp-blep-zdf-oversampling.png)

## Why 2-point PolyBLEP and 2x oversampling still alias

A 2-point polynomial BLEP (`PolyBlepOsc`) is cheap, yet on a `1.75 kHz` sawtooth wave it still leaves `-34 dB` of aliasing, and a naive triangle wave aliases on every slope reversal. Even when an oscillator is replaced with a longer sinc table, two subtler traps remain:

- **Table resolution vs kernel length**: In `BlepOsc`, each discontinuity correction is stored in a `24`-tap (`kKernelHalf = 12`) Kaiser-windowed (`beta = 9.0`) sinc residual table for both steps (BLEP) and slope kinks (BLAMP). Crucially, the table uses `kTableRes = 2048` rows per sample of fractional position. At `128` rows per sample, linear-interpolation error alone left a `-67 dB` line at every folded harmonic (`-30 dB` total on a `1.75 kHz` saw).
- **20 to 24 kHz intermodulation before drive**: `BlepOsc` offers two cutoffs: `kBandWide` (`0.90` of Nyquist, flat to roughly `18 kHz` at `48 kHz`) for direct output like `Wavetable`, and `kBandNarrow` (`0.80` of Nyquist, `-0.4 dB` at `16 kHz`, `-2.7 dB` at `18 kHz`, and `-52 dB` at `24 kHz`) for `Analog`. Because `20-24 kHz` energy from a wide kernel intermodulates inside the `tanh` drive and filter stages, no post-oscillator oversampling can remove the resulting foldover. Switching to `kBandNarrow` plus `4x` oversampling dropped `Analog`'s non-harmonic floor at `4.2 kHz` from `-64 dB` to `-86 dB`.

Hard sync in `BlepOsc` is also solved at the exact sub-sample instant the master oscillator wraps: the slave is stepped to the master's wrap point, reset, and stepped for the remainder of the sample with the jump band-limited, rather than snapping slave phase to `0` on sample boundaries.

## Antiderivative drive and 4x oversampling

Inside `AnalogSynthCore`, each voice's mixed oscillators (`osc1` with up to `kMaxUnison = 7` unison voices, `osc2`, `sub`, and `noise`) pass through an AC blocker before entering a `4x` linear-phase half-band cascade (`kFilterOversample = 4`, `kFilterOversampleGroupDelay = 32` for the first stage).

Running the drive and filter at `2x` is not enough: even with `FastTanhAdaa` (first-order antiderivative antialiasing on the `tanh` curve), the per-stage `tanh` nonlinearities inside the ladder filter still fold back to `-73 dB` non-harmonic on a clean `19 kHz` input, whereas `4x` drops that floor to `-89 dB`. Combined with `kBandNarrow`, a `C4`, `C7`, or `C8` saw moves from `-68 / -63 / -61 dB` down below `-80 dB`. Using `firstStageGroupDelay = 32` instead of the `64`-sample effect default keeps CPU cost low enough for up to `16` oversampled filter paths across `8` polyphonic voices (`kMaxVoices = 8`).

## Zero-delay feedback filters in 4-lane SIMD

Both `Analog` and `Audio Filter` share `FilterModels.h`, which implements three zero-delay feedback trapezoidal (TPT) prototypes alongside standard SVF modes:

1. **Moog 4-pole transistor ladder (`kModelLadder`)**: Critical loop gain `kCriticalK = 4.0f` and `kOscRatio = 1.0f`.
2. **MS-20 2-pole Sallen-Key (`kModelMs20`)**: Positive feedback through a high-pass path, `kCriticalK = 2.0f`, keeping DC gain at `1.0` across resonance.
3. **TB-303 4-pole diode ladder (`kModelDiode`)**: Coupled stages with `kCriticalK = 22.1049383f` and `kOscRatio = 0.726483157f`. Because a raw diode ladder droops by `-27 dB` at full resonance, `MakeupGain` normalises its passband level to match the transistor ladder.

Each OTA-style stage saturates on the difference it integrates and linearises `tanh(v) ~= v * T(v')` around the previous sample (`Slope(v) = FastTanh(v) / v`), turning the coupled stages into a linear system solved outright, while the loop's feedback limiter (`fb = L * tanh(fb_in / L)`) is solved via Newton's method (`SolveLimiter`). `CutoffToG` scales `tan(pi * fc / fs) / m` so every model self-oscillates on the exact `cutoff` pitch.

All `FilterModels` code is templated on `float` or `VoiceSimd::F4`, allowing `AnalogSynthCore::DriveAndFilter4` to process four voices in lockstep per stereo side (`kLaneGroups = kMaxVoices / 4 = 2`) with identical floating-point operation ordering to the scalar path.

## Patching Analog inside the node graph

On Infinite's canvas, `Analog` connects directly to note generators like `Euclidean` and `Piano Roll`, per-voice modulators, and downstream DSP or visual nodes such as `Audio Effect`, `Spatial Mixer`, and `Audio Texture`, or sits alongside physical models like [Wind and Piano](https://infinitedevices.org/blog/digital-waveguide-wind-and-piano-physical-modelling.html). Ready-to-run synth patches and starter templates are available in the [Infinite library](https://infinitedevices.org/library.html), or you can [download Infinite](https://infinitedevices.org/) to inspect the node graph directly.

There are clear limits to keep in mind. Infinite is a young, MIT-licensed open-source project: `Analog` caps polyphony at `8` voices (`kMaxVoices = 8`) and unison at `7` (`kMaxUnison = 7`), desktop plugin hosting supports VST3 and AU without AAX, and live hardware audio inputs are refused in headless mode. For DSP engineers and sound designers who want inspectable, zero-allocation virtual analog synthesis, the full C++ implementation is open to read, benchmark, and patch.
