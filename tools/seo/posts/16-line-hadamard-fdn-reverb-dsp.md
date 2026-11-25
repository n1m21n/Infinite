---
title: 16-line Hadamard FDN reverb: diffusion, modulation, and SIMD DSP
seo_title: 16-Line Hadamard FDN Reverb DSP | Infinite
description: How Infinite's Reverb node pairs dual 4-stage Schroeder diffusers, a 16-line Hadamard FDN, Hermite modulation, and damping-compensated RT60 in SIMD.
date: 2026-11-26
category: product
tags: [audio dsp, fdn reverb, hadamard matrix, simd, sound design]
keywords: fdn reverb dsp, 16-line hadamard feedback delay network, schroeder allpass diffusion, hermite delay modulation, damping compensated rt60
image: assets/blog_16-line-hadamard-fdn-reverb-dsp.png
image_alt: DSP block diagram of Infinite's Reverb node showing stereo predelay, dual 4-stage Schroeder allpass diffusers, 16-line Hadamard FDN with Hermite modulation, and SIMD damping
status: published
---

Algorithmic reverb remains one of the most active topics in open-source audio engineering, from differentiable Feedback Delay Networks (`differentiable-fdn`) and embedded `esp32-fdnreverb` ports to modular channel-strip projects like `mas-reverb`. Achieving dense, non-metallic tails without exploding CPU budgets requires solving four coupled problems in FDN reverb DSP: input transient density, tank eigenmode spacing, pitch-smooth delay modulation, and accurate high-frequency decay times.

In [Infinite](https://infinitedevices.org/), the **Reverb** node (`ReverbKernel`) addresses all four in a zero-allocation C++ kernel with NEON and SSE2 SIMD paths. Rather than gating modulation or diffusion behind a "vintage" toggle, it runs two independent 4-stage Schroeder allpass diffusers into a 16-line Fast Walsh-Hadamard FDN with always-on prime-detuned Hermite modulation and damping-compensated loop gain.

![DSP block diagram of Infinite's Reverb node showing stereo predelay, dual 4-stage Schroeder allpass diffusers, 16-line Hadamard FDN with Hermite modulation, and SIMD damping](https://infinitedevices.org/assets/blog_16-line-hadamard-fdn-reverb-dsp.png)

## Why static delay networks ring metallically

A bare Feedback Delay Network with unmodulated delay lines behaves like a bank of coupled comb filters: impulsive inputs such as drums or plucked strings excite discrete modal peaks that ring as metallic resonances. Two design choices in `ReverbDsp` prevent that ringing before it starts:

- **Hall-scale mutually coprime delay lines**: The tank uses `16` delay lines (`kNumLines = 16`) with base lengths at `44.1 kHz` spanning `907` to `3407` samples (`kBaseLengths44k`, roughly `20.6 ms` to `77.3 ms`), scaled by sample rate and `size` (`0.15 + 0.85 * size`). Because every base length is prime and mutually coprime, no two lines share a resonant comb family.
- **Always-on prime-detuned LFO modulation**: Every line's read tap is continuously modulated by its own LFO (`kLfoRates` from `0.29 Hz` to `0.83 Hz`), each drifted by a `0.08 Hz` secondary sinusoid (`25%` rate depth) so no two lines ever phase-lock. Modulation depth is `4.0` samples in clean mode and `10.0` samples in `analog` mode, read through a 4-point Hermite cubic interpolator (`Hermite4`).

Computing `32` `sinf()` calls per sample for 16 drifted LFOs used to dominate the kernel's CPU cost. `AdvanceLfoBank` replaces them with `FastSin2Pi`, a branch-free degree-9 minimax polynomial over `[-pi/2, pi/2]` with a maximum error of `1.9e-7` that vectorises cleanly.

## Dual-channel Schroeder diffusion and stereo decorrelation

Before audio enters the 16-line tank, it passes through a stereo predelay (`0` to `500 ms`) with an equal-power `20 ms` tap crossfade (`kPredelayFadeMs = 20.0f`) so automating `predelay` never clicks. Next come two independent 4-stage Schroeder allpass diffusion chains (`mDiffuserL` and `mDiffuserR`):

- **Left channel lengths**: `{142, 107, 379, 277}` samples at `44.1 kHz`, with allpass gains `{0.75, 0.75, 0.625, 0.625}`.
- **Right channel lengths**: The same four primes offset by `+23` samples (`kDiffusionROffset44k = 23`), so stereo channels decorrelate genuinely instead of relying on a mono sum with a phase flip.

After diffusion, fixed input low-pass filters (`mInputLpfL` and `mInputLpfR`) soften extreme transients. When `analog` (`0.0` to `1.0`) is enabled, asymmetric `tanh` saturation (`AsymTanh` at `0.12f`) and an envelope-following dynamic-air filter (`mInputEnv`) modulate the tank's damping corner downward during quiet tails.

## Damping-compensated RT60 and parameter slewing

A classic pitfall in FDN design is placing a one-pole low-pass filter (`y += a * (x - y)`) inside each feedback line without accounting for its passband attenuation below the cutoff frequency. At a `4 s` decay setting, even mild attenuation per pass shortens the actual `1 kHz` RT60 by roughly `10%`.

`ReverbDsp::DampCompensation` computes the exact inverse magnitude `1 / |H|` of the one-pole damping filter at `min(1000 Hz, 0.25 * cutoffHz)` and scales the per-line decay gain `exp(-3 * ln(10) * activeLen / (fs * decaySeconds))` by that factor. `CompensatedDecayGain` then clamps the compensated gain below `g^0.8`, ensuring DC RT60 never exceeds the `decay` knob by more than `25%` and keeping the loop strictly under unity for any `decay` from `0.1 s` to `20.0 s`.

To prevent pitch zippering when `size` (`0.0` to `1.0`) moves in real time, each line's active length slews toward its target at `0.03` samples per sample (`kLineSlew = 0.03f`), bounding pitch glide to at most half a semitone. Per-block caches key `cachedBaseCutoff`, `cachedDampCoeff`, `cachedPredelaySamples`, and `cachedDecayGain` on their exact inputs so steady-state blocks skip redundant `pow` and `exp` calls with bit-identical results.

## 16-line Hadamard mixing in NEON and SSE2

Because `16` is a power of two, the tank's feedback matrix is an orthogonal Fast Walsh-Hadamard Transform (`Hadamard16`) normalised by `1 / sqrt(16) = 0.25f`. Energy in an orthogonal matrix is strictly conserved, so the tank can never build up runaway gain regardless of how many lines feed it.

`ReverbKernel::ProcessBlockSimd` processes the 16 lines as four 4-lane SIMD groups (`float32x4_t` on ARM NEON, `__m128` on x86 SSE2):

1. Gather four adjacent samples (`wp - iDelay` through `wp - iDelay - 3`) per line and run a `4x4` matrix transpose so `xm1`, `x0`, `x1`, and `x2` sit in parallel lanes for `Hermite4`.
2. Execute `Hadamard16` across the four vectors via four 4-lane butterfly stages (`Hadamard4`) followed by two cross-vector butterfly passes and the `0.25f` normalisation.
3. Apply `decayGain`, one-pole `dampCoeff`, and denormal flushing (`1.0e-15f` threshold), inject the stereo-alternating diffused input (`+0.5` on lines `0..7`, `-0.5` on lines `8..15`), and sum even and odd lines into `wetL` and `wetR` scaled by `width` (`crossGain = 1.0 - width * 0.4`).

## Patching Reverb inside the node graph

On Infinite's node canvas, `Reverb` pairs naturally after [Analog](https://infinitedevices.org/blog/aliasing-free-virtual-analog-dsp-blep-zdf-oversampling.html), `Wavetable`, or `Sampler`, and accepts modulation cables on `size`, `decay`, `damping`, `predelay`, `width`, `analog`, and `mix` (`0.25` default). You can explore ready-made synth and ambient patches in the [Infinite library](https://infinitedevices.org/library.html) or [download Infinite](https://infinitedevices.org/) to inspect and route the node graph directly.

There are honest limits to keep in mind. Infinite is a young, MIT-licensed open-source project: `Reverb` operates on stereo 2-channel audio (zeroing channels above 2), desktop plugin hosting supports VST3 and AU without AAX, and live hardware audio inputs are refused in headless mode.
