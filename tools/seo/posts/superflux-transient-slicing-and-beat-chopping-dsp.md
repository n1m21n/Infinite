---
title: SuperFlux transient slicing and beat chopping in real-time DSP
seo_title: SuperFlux Transient Slicing DSP | Infinite
description: How Infinite's Slicer node uses SuperFlux onset detection, centred STFT framing, and position-domain confinement for tight real-time beat chopping.
date: 2026-11-23
category: creative
tags: [transient detection, superflux, beat slicing, audio dsp, modular synthesis]
keywords: superflux onset detection, transient slicing dsp, beat chopping audio, centred stft onset, modular sample slicer
image: assets/blog_superflux-transient-slicing-and-beat-chopping-dsp.png
image_alt: Technical DSP diagram of Infinite's Slicer node showing centred 1024-point Hann STFT, 3-bin SuperFlux reference max-filter, Dixon adaptive peak picker, and position-domain slice confinement
status: published
---

Open-source sample engines and transient tools are getting renewed attention across audio engineering. Recent releases such as Floe for CLAP, VST3, and AU sample libraries, Demucs stem separation compiled to Rust and WebAssembly for browser workflows, and transient-driven resynthesisers all point to the same practical demand: musicians want fast, inspectable ways to split a recording into playable rhythmic hits without wrestling with a wall of manual markers. Yet automatic transient slicing is deceptively fragile on real musical material, because vibrato, pitch glides, and un-centred FFT windows quietly ruin slice placement.

In [Infinite](https://infinitedevices.org/), the **Slicer** node (`SlicerNode`) pairs a clean-room SuperFlux onset detector (`SlicerDsp::Detect`) with a lock-free, position-domain playback engine. You can drop in a drum break or melodic phrase, slice it by detected transients or by the global transport tempo grid, audition and drag individual markers on the waveform, and drive up to `64` slices chromatically from MIDI note `36` (`C1`) upwards alongside [wind and piano physical models](https://infinitedevices.org/blog/digital-waveguide-wind-and-piano-physical-modelling.html) or visual nodes on the same canvas.

![Technical DSP diagram of Infinite's Slicer node showing centred 1024-point Hann STFT, 3-bin SuperFlux reference max-filter, Dixon adaptive peak picker, and position-domain slice confinement](https://infinitedevices.org/assets/blog_superflux-transient-slicing-and-beat-chopping-dsp.png)

## Why naive spectral flux misfires on vibrato and early frames

Basic onset detectors measure frame-to-frame increases in short-time Fourier transform (STFT) magnitude. That works on isolated dry snares, but breaks down on sustained basslines, strings, or vocals: when a pitch glides or vibrato shifts energy from one FFT bin to its neighbour, a naive differencer sees a sudden jump in the destination bin and fires a false-positive slice.

Infinite's `SlicerDsp::Detect` solves this by implementing two primary-reference techniques from the DAFx literature:

1. **Log-compressed spectral flux (Dixon, DAFx-06)**: Magnitude bins are compressed via $\log(1 + 1000 |X|)$ before half-wave-rectified differencing, keeping quieter percussive transients visible without letting broadband noise dominate the onset detection function (ODF).
2. **SuperFlux 3-bin reference maximum filter (Boeck & Widmer, DAFx-13)**: Before subtracting the previous frame from the current frame, `Slicer` applies a 3-bin maximum filter across the *reference* frame. Because vibrato shifts energy only by a bin or two, the widened reference absorbs the pitch wobble while genuine broadband attacks still stand out cleanly.

Equally important is **centred STFT framing**. `Slicer` uses a `1024`-sample Hann window with a sample-rate-scaled hop (`round(256 * sr / 44100)`), keeping the ODF frame rate near `172 Hz` across `44.1 kHz`, `48 kHz`, and `192 kHz` files. Each frame `n` is centred on sample `n * hop` rather than starting there. An un-centred window reports an onset up to `fftSize / 2` samples early, which lands the slice marker in the silence ahead of the attack where a `-70 dB` silence gate (`silenceGateDb = -70.0f`) would reject it outright.

## Background worker analysis and instant repruning

Running an STFT and adaptive peak picker over a long recording does not belong on the audio thread. `SlicerNode` spawns a dedicated background worker with an `abort` flag so loading a new file or handing over hand-placed markers from the Sample Studio (`SetSlices`) cancels any obsolete scan cleanly.

Once the worker finishes, `SlicerDsp::Detect` returns strictly ascending onset frames gated by a `28 ms` minimum inter-onset interval (`minIoiSeconds = 0.028`) and an asymmetric adaptive threshold (`kPeakW = 3` frames, `kPeakMultiplier = 3`). Crucially, frame `0` is always stamped with a sentinel strength (`kForcedOnsetStrength = 1e9f`) so pruning down to the strongest `N` slices via the `onsets` parameter happens instantaneously on the main thread without re-running the FFT and without ever dropping the start of the sample.

Switching `sliceBy` to `grid` bypasses spectral analysis altogether and derives slice boundaries arithmetically from the global transport tempo and the selected division (`kSlicerNumDivisions = 6`). Hand-dragged markers in `onsets` mode are serialised into the patch (`slices`) so reopening or copying a node preserves manual edits without re-analysing the file.

## Separating boundary confinement from amplitude decay

A common sampler bug is conflating *where a slice stops* with *how fast its amplitude decays*. `SlicerNode` keeps those two concerns strictly orthogonal:

- **`crossthrough` owns the boundary**: When off (the default), each voice stops cleanly at the next slice's onset with a `3 ms` raised-cosine fade so adjacent hits never bleed into one another. When on, playback runs past the boundary through the rest of the sample.
- **`decay` owns the envelope**: It shapes the amplitude falloff after `attack`, with a hold detent at the top of its throw (`kDecayInfinite`) for full-level sustained playback.

Even more importantly, boundary confinement is evaluated in **read-head position** rather than wall-clock milliseconds. If you halve `speed` to `0.5` (clamped between `0.25` and `4.0`, alongside `pitch` in `-24..+24` semitones and `finetune` in `-100..+100` cents), the voice takes twice as long in wall-clock time to reach the next onset and never reads past it.

## Patching Slicer into generative audio and visual chains

Because `AudioSlicerNode` publishes up to `8` active playheads (`kMaxVisualVoices = 8`) into a lock-free triple buffer (`SlicerVoiceSnapshot`), the UI waveform draws live per-slice playheads alongside a `256`-bucket cached envelope (`WavePeaks`) without touching the audio callback. You can sequence slices from `Euclidean` or `Arpeggiator`, modulate `pitch`, `speed`, or `decay` from an LFO, and route the audio out to `Spatial Mixer` and `Audio Texture` in one graph. Example patches and starter templates are available in the [Infinite library](https://infinitedevices.org/library.html).

There are clear limits to keep in mind. Infinite is a young, MIT-licensed open-source project: `Slicer` caps each instance at `64` slices (`kMaxSlices = 64`), and MIDI notes at or above `36 + sliceCount` stay silent rather than wrapping back to slice `0`. On desktop Infinite hosts VST3 and AU plugins, though AAX is not supported, and live hardware audio capture into `Slicer`'s `record in` pin is refused in headless mode. For sound designers and DSP engineers who want transparent, sample-accurate beat chopping without opaque black-box behaviour, the full C++ implementation is ready to inspect, patch, and render.
