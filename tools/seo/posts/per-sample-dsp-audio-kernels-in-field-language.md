---
title: Writing 48kHz audio DSP kernels in Field language
description: Breakdown of Infinite register machine for Field Synth and Effect nodes, compiling per-sample audio DSP kernels at 48kHz.
date: 2026-10-19
category: product
tags: [dsp, c++, field language, synthesis, audio engineering]
image: assets/blog_audio_dsp_kernel.png
image_alt: real-time 48khz audio dsp register waveform visualization in infinite
keywords: audio dsp c++, per-sample dsp kernel, audio register machine, moog ladder filter formula, modular synth coding
status: published
---

Most modern digital audio workstations treat audio synthesis as a black box: you write a C++ plugin, compile a VST3 or AU bundle with JUCE or Dplug, restart your host, and attach a debugger.

In Infinite, you can write audio DSP at the sample level directly inside the canvas using **Field language**. Your code runs on the audio thread once per sample (48,000 times per second), compiled to a register machine that executes with zero memory allocations.

## How the Sample Register Machine Works

When you place a **Field Synth** (`Synths Field Synth`) or **Field Effect** (`AudioEffects Field Effect`) node, Infinite compiles the kernel expression to a compact bytecode representation that evaluates on register cells.

### Built-in Variables Available on the Audio Thread:
- `sr`: Audio sample rate in Hz (typically `48000.0`).
- `in`: Incoming audio sample value (`[-1.0, 1.0]`) in Field Effect.
- `out`: Output audio sample to write.
- `freq`: Note frequency in Hz (voice pitch in Field Synth).
- `gate`: `1.0` while a MIDI note is held down, `0.0` when released.
- `noteVel`: Key velocity (`[0.0, 1.0]`).
- `state`: Declares per-voice persistent memory preserved across samples.
- `param`: Exposes a modulatable float knob on the node body with min/max bounds.

## A 1-Pole Lowpass Filter Kernel

Here is a classic 1-pole lowpass filter written in 4 lines:

```field
param float cutoff = 0.25 [0.01, 0.99]
state float z = 0
z += (in - z) * cutoff
out = z
```

Because `state float z = 0` allocates a persistent cell in the voice register, `z` retains its previous sample value, creating an analog RC filter response.

## Shipped Moog 4-Pole Ladder Filter

For aggressive saturation and self-oscillation, Infinite ships with a 4-pole cascaded non-linear filter:

```field
param float cutoff = 2200.0 [80.0, 14000.0]
param float res = 0.70 [0.0, 0.98]
param float drive = 1.50 [1.0, 4.0]
param float mix = 1.0 [0.0, 1.0]
state float s1 = 0
state float s2 = 0
state float s3 = 0
state float s4 = 0

f = clamp((cutoff / sr) * 2.2, 0.005, 0.42)
feedback = (s4 * res * 3.6) / (1.0 + abs(s4) * 0.8)
u = (in * drive) - feedback
sat = u / (1.0 + abs(u) * 0.3)

s1 = s1 + f * (sat - s1)
s2 = s2 + f * (s1 - s2)
s3 = s3 + f * (s2 - s3)
s4 = s4 + f * (s3 - s4)

wet = s4
out = in * (1.0 - mix) + wet * mix
```

Notice the dry/wet `mix` pattern at the end: every Field audio effect ends with `out = in * (1.0 - mix) + wet * mix`, giving you instant parallel processing.

## Real-Time Mod Matrix Integration

Unlike fixed DSP plugins, a Field Effect node can publish meter readings back to the visual and modulation canvas using `reduce.rms`:

```field
param float boost = 1.5 [0.5, 4.0]
output frame float bass = reduce.rms(in, 20.0, 200.0)
out = in * boost
```

The `bass` scalar automatically becomes a second output pin on the node. You can run a cable from that pin directly into a 3D geometry deformer, a light intensity slider, or a GLSL shader.

## Testing and Verifying Headlessly

You can test Field kernels without opening the GUI using Infinite's CLI or Python SDK:

```python
import infinite

# Synthesize a custom per-sample subtractive saw voice
infinite.field_synth("""
state float phase = 0
phase = (phase + freq / sr) % 1.0
out = (phase * 2.0 - 1.0) * 0.3 * gate
""", out_wav="saw.wav", duration=3)
```

The CLI synthesizes the audio, measures integrated LUFS, checks for clipped frames, and writes the output file with zero GUI dependencies.
