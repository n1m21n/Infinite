---
title: Sound design in Infinite: sub bass, plucks, and pads
description: Guide to modular sound design in Infinite DAW: subtractive sub bass, Karplus-Strong plucks, and ambient pads with Field kernels.
date: 2026-10-19
category: creative
tags: [sound design, synth, electronic music, sub bass, dsp]
image: assets/blog_sound_design_synth.png
image_alt: 3d audio spectrum and modular sound design synthesis in infinite
keywords: sound design daw, sub bass patch, synth pluck tutorial, ambient pad sound design, modular synth linux mac windows
status: published
---

Most modern digital audio workstations treat sound design as an external plugin problem. You buy a third-party VST, open a floating GUI, turn virtual skeuomorphic knobs, and send audio back into a linear timeline. If you want a dedicated sound design DAW, Infinite takes a fundamentally different path: sound design and synthesis live directly on the same canvas as your sequencers, modulation matrix, and visual engine.

With Field language nodes, every oscillator, envelope, filter, and wavefolder can be written as clean, per-sample DSP code running at 48 kHz with zero allocation overhead. You can also [modulate VST3 plugins with LFOs and visuals](https://infinitedevices.org/blog/modulate-vst3-plugins-with-visuals-and-lfos.html) whenever you wish to incorporate external instruments.

Here is how to construct three foundational electronic music sounds from first principles in Infinite.

## 1. Clean, punchy sub bass with harmonic saturation

A clean sub bass needs two things: rock-solid pitch stability in the 30 Hz to 80 Hz region and subtle odd harmonics so it remains audible on laptop speakers and mobile devices.

### Signal flow
1. **MIDI In** -> pitch to frequency conversion: `f = 440.0 * pow(2.0, (note - 69.0) / 12.0)`.
2. **Field Synth** -> phase accumulator with soft-clipped sine wave and dynamic saturation.
3. **Envelope** -> fast exponential attack (2 ms), medium decay (350 ms), sustained floor at -6 dB.

### The Field kernel
```field
// registers: r0 = phase, r1 = pitch_smooth
let target_phase_inc = f * (6.28318530718 / 48000.0)
r1 = r1 * 0.95 + target_phase_inc * 0.05
r0 = r0 + r1
if r0 > 6.28318530718 { r0 = r0 - 6.28318530718 }

// fundamental sine wave
let fundamental = sin(r0)

// third harmonic saturation for low-end translation
let sat = fundamental + 0.15 * sin(r0 * 3.0)

// soft tanh-like saturation curve
out = sat / (1.0 + abs(sat) * 0.3)
```

Because the phase accumulator maintains exact 64-bit continuous phase, there is zero phase click even during rapid bassline glides or legato 808 note changes. You can open an interactive version of this patch in the [Infinite Library](https://infinitedevices.org/library.html).

## 2. Snappy acoustic-style plucks via Karplus-Strong

Subtractive synths approximate plucks by sweeping a lowpass filter with a fast envelope. Physical modelling plucks, however, sound organic and tactile because they simulate the energy dissipation of a real vibrating string. You can test a ready-to-run [Karplus-Strong synthesiser patch](https://infinitedevices.org/library/karplus-strong-synth.html) directly in the library.

In Infinite, you can build a physical modelling string resonator using a feedback delay line with a lowpass damping filter in an audio effect node:

```field
// r0..r2048: circular delay buffer, r2049: write head index, r2050: previous sample
let buffer_len = int(48000.0 / f)
let read_idx = (r2049 - buffer_len + 2048) % 2048
let delayed_sample = read_reg(read_idx)

// lowpass damping filter (averaging adjacent samples)
let filtered = (delayed_sample + r2050) * 0.498
r2050 = delayed_sample

// mix incoming excitation noise burst with feedback
let excitation = in
let new_sample = excitation + filtered
write_reg(r2049, new_sample)
r2049 = (r2049 + 1) % 2048

out = new_sample
```

When you trigger this node with a 5-millisecond white noise burst from a **Noise** node, you get an acoustic harp or guitar timbre that responds naturally to note velocity.

## 3. Shimmering ambient pads with detuned supersaws and stereo diffusion

For deep atmospheric soundscapes, Infinite lets you run multi-voice unison oscillators with randomised phase spreading and integrated stereo diffusion, similar to the [pad wash library patch](https://infinitedevices.org/library/pad-wash.html).

### Recipe
1. Generate 7 saw oscillators per voice, detuned exponentially across +/- 18 cents.
2. Feed the summed output into a **Field Effect** stereo chorus and all-pass diffuser network.
3. Connect the audio spectrum directly into a **Field Pixel** visualiser node so your pad's frequency harmonics modulate a real-time raymarched crystal or volumetric cloud on screen.

### Modulating sound from visuals and vice-versa
Because Infinite does not separate the audio thread from the modulation graph, you can route the brightness of your visual scene directly into the filter cutoff of your pad:

- **Audio Analyze** -> measures pad high-frequency resonance.
- **Field Pixel** -> visual bloom expands in response to pad brightness.
- **LFO / Motion Tracker** -> webcam hand position controls pad stereo spread.

Every parameter in Infinite is accessible over the Python SDK and headless automation, allowing AI agents and artists alike to generate whole albums of sound design with verifiable precision.
