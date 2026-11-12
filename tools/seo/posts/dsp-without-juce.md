---
title: writing audio dsp without juce: notes from infinite
description: why infinite's audio engine is written in modern c++ without a framework, and what that changes for real-time safety.
date: 2026-10-19
category: product
tags: [c++, dsp, audio, architecture]
image: assets/nodegraph.webp
image_alt: infinite modular audio graph canvas and node dsp architecture
keywords: c++ audio dsp without juce, real-time audio thread allocations, spsc lock-free audio queue, vst3 hosting architecture, field dsp language
status: published
---

juce is the de facto standard for audio software development, but it brings substantial architectural gravity: a massive monolithic codebase, custom string classes, complex licensing, and heavy abstractions.

when building [infinite](https://infinitedevices.org), an open-source modular workstation, we chose to write the entire pull-based audio engine in modern standard c++ (c++20) from scratch.

here is what we learned and how the architecture is designed.

---

## 1. the golden rule: zero allocations on the audio thread

any audio callback deadline at 48khz with a 128-sample buffer is strict: you have exactly 2.66 milliseconds to finish processing before the dac underflows and creates an audible click.

to guarantee hard real-time safety:
- **zero `malloc` and `free`**: all audio buffers, voice allocations, and delay memory are pre-allocated during `preparetoplay()`.
- **zero mutexes**: cross-thread communication between the ui thread and audio thread uses atomic pointers, lock-free single-producer single-consumer (spsc) ring buffers, and double-buffered modulation arrays.
- **cache-line aligned processing**: audio frames are laid out contiguously in memory so simd instructions can vectorize filter stages without cache stalls.

---

## 2. native au and vst3 plugin hosting

instead of relying on framework wrappers, infinite speaks directly to plugin apis:
- **vst3**: loads the component bundle, queries `IComponent` and `IAudioProcessor`, maps parameters to continuous modulation arrays, and schedules sample-accurate note events.
- **audio unit (au)**: interacts directly with the coreaudio audio unit v2 and v3 c-api on macos.

parameters on third-party plugins can be modulated in real time by any signal on the canvas, whether an audio lfo, an envelope follower, or a generative javascript sketch.

![infinite modular canvas](../assets/product_canvas_1400.webp)

---

## 3. embedded dsp kernel language

to let users write custom filters and physical modeling synths without recompiling c++, we created **field**, an embedded dsp language. a 1-pole filter in field is 4 lines:

```
param float cutoff = 0.25 [0.01, 0.99]
state float z = 0
z += (in - z) * cutoff
out = z
```

the compiler translates this directly into optimised vector loops evaluated per-sample on the audio thread.

- try infinite: [infinitedevices.org](https://infinitedevices.org)
- read the codebase: [github.com/n1m21n/infinite](https://github.com/n1m21n/infinite)
