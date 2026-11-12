---
title: Euclidean rhythm sequencer algorithms for modular music
description: How to generate Euclidean rhythm sequencer algorithms using Bjorklund integer distribution, modular arithmetic, and audio-reactive triggers in Infinite.
date: 2026-10-20
category: creative
tags: [euclidean rhythms, sequencing, modular synthesis, algorithmic composition]
image: assets/blog_euclidean_rhythm_sequencer.png
image_alt: Euclidean rhythm sequencer algorithm diagram showing geometric hit distributions and modular node patching
keywords: euclidean rhythm sequencer algorithm, euclidean rhythm generator, bjorklund algorithm audio, modular beat generator
status: published
---

A Euclidean rhythm sequencer algorithm distributes a fixed number of active musical pulses as evenly as possible across a discrete timeline of metric steps. First identified in computer science by E. Bjorklund in 2003 for timing neutron accelerators at Los Alamos, and brought into music theory by Godfried Toussaint in 2005, Euclidean rhythms revealed a startling truth: the mathematical distribution of pulses on a circle generates the foundational rhythmic structures of traditional music worldwide.

In modern modular synthesis and creative coding environments like [Infinite](https://infinitedevices.org/), Euclidean sequencers provide an elegant alternative to static step grids. Because the pattern emerges directly from two integer parameters, pulse count and step length, composers can morph complex groove structures live without editing step arrays by hand.

Here is how the mathematics work, how to implement the algorithm in a single line of procedural code, and how to patch Euclidean triggers into synthesizers and 3D geometry simultaneously.

![Euclidean rhythm sequencer algorithm diagram showing geometric hit distributions and modular node patching](https://infinitedevices.org/assets/blog_euclidean_rhythm_sequencer.png)

## From nuclear accelerators to world rhythms

The core challenge of rhythmic distribution can be stated simply: given $n$ available steps in a bar and $k$ active beats, how can we space the beats so the interval between any two consecutive hits differs by at most one step?

Bjorklund originally developed an iterative algorithm based on Euclid's greatest common divisor method. Starting with a set of $k$ ones (active hits) and $n - k$ zeros (rests), the algorithm repeatedly distributes the smaller group into remainder sequences until no division remains. When applied to musical intervals, Toussaint observed that this mechanical reduction produces virtually every major non-Western and folk timeline pattern:

- **E(3, 8)** produces `[X . . X . . X .]`, the Cuban tresillo and Caribbean habanera rhythm.
- **E(5, 8)** produces `[X . X X . X X .]`, the Cuban cinquillo and Spanish tango.
- **E(5, 12)** produces `[X . . X . X . . X . X .]`, the Venda clapping pattern from South Africa.
- **E(7, 12)** produces `[X . X . X X . X . X . X]`, the West African bell pattern (Bembe), which is also geometrically isomorphic to the diatonic major scale on a twelve-tone chromatic circle.
- **E(5, 16)** produces `[X . . X . . X . . . X . . X . .]`, the Brazilian bossa nova necklace.
- **E(7, 16)** produces `[X . . X . X . . X . X . . X . .]`, a fundamental Brazilian samba rhythm.

Because each pattern can be rotated around the circular step circle, a single pair of integers yields multiple distinct syncopations simply by advancing the starting step index.

## The closed-form integer Bresenham formula

While Bjorklund's recursive array partitioning works well for static sequence generation, running recursive string manipulations inside a real-time audio thread or graphics shader is inefficient.

Fortunately, Euclidean distribution can be expressed in closed form as a one-dimensional Bresenham line-drawing algorithm. Imagine drawing a continuous line from coordinate `(0, 0)` to `(n, k)` on a 2D integer grid. Each time the line crosses an integer threshold on the vertical axis, an active hit occurs.

For any discrete step index $s \in [0, n - 1]$, the state of the trigger is calculated directly by comparing two consecutive floor evaluations:

```text
on(s) = floor((s + 1) * k / n) - floor(s * k / n)
```

This mathematical identity possesses several vital properties for modular audio workstations:

1. **Constant time complexity**: The evaluation takes $O(1)$ operations with zero loops, making it suitable for per-sample DSP evaluation at 48,000 Hz.
2. **Deterministic boundary precision**: Because integer multiplication precedes division, no floating-point rounding errors accumulate across long performances.
3. **Instantaneous parameter morphing**: Changing $k$ or $n$ on the fly immediately recalculates the current step without clearing internal buffers or causing audio clicks.

To introduce pattern rotation, we add a phase offset parameter $r$ before taking the modulo of the sequence length:

```text
step_rotated = (s + r) % n
on = floor((step_rotated + 1) * k / n) - floor(step_rotated * k / n)
```

## Implementing Euclidean algorithms in Field Notes

In Infinite, you can write procedural note generation directly using the `Field Notes` node. Unlike fixed piano roll clips, `Field Notes` compiles real-time mathematical expressions into sample-accurate MIDI event streams.

Here is the complete implementation of a parameterised Euclidean rhythm sequencer kernel:

```text
param float hits = 5 [1, 16]
param float steps = 16 [1, 32]
param float rotate = 0 [0, 31]
param float pitch = 60 [24, 96]
state float step = 0

if (tick(1/16) > 0.5) {
   s = (step + floor(rotate + 0.5)) % steps
   k = clamp(floor(hits + 0.5), 1, steps)
   n = floor(steps + 0.5)
   
   on = floor((s + 1) * k / n + 0.001) - floor(s * k / n + 0.001)
   
   if (on > 0.5) {
      note(pitch, 0.85, 1/16)
   }
   
   step = (step + 1) % n
}
```

In this kernel, `tick(1/16)` synchronises with the workstation transport clock, firing a pulse on every sixteenth note. The `state` variable tracks the playback position, while `param` exposes `hits`, `steps`, and `rotate` as live modulatable controls on the node canvas. You can patch an `LFO` or `Random` node directly into `rotate` to create evolving rhythmic variations that stay locked to the metric grid.

If you want to explore pre-built musical patches and procedural devices, check the interactive devices in the [Infinite Library](https://infinitedevices.org/library.html).

## Polyrhythms, phase shifting, and polymetre

The true compositional depth of Euclidean sequencing emerges when combining multiple independent generators running in parallel.

When two sequencers share the same step resolution (for example, sixteenth notes) but use coprime total step counts, they produce polymetric phasing. Consider patching three distinct `Field Notes` instances into separate lanes of a `Drum Sampler` or `Analog` synth:

- Voice 1 (Kick drum): $E(4, 16)$, a steady four-on-the-floor pulse.
- Voice 2 (Snare / Rim): $E(5, 12)$, a five-beat pattern looping every twelve sixteenth notes.
- Voice 3 (Hi-hat / Bell): $E(7, 15)$, a seven-beat pattern looping every fifteen sixteenth notes.

Because 16, 12, and 15 have a lowest common multiple of 240 steps, the combined rhythm will play for fifteen bars of 4/4 time before ever repeating identical downbeat alignments. The music feels continuously evolving and organic, yet remains strictly locked to a shared mathematical grid.

Furthermore, by applying subtle phase modulation to the `rotate` input with a slow triangle `LFO`, the patterns gently slip against one another, creating Steve Reich style phase-shifting textures.

## Simultaneous audio transients and 3D visual geometry

Because Infinite hosts audio synthesis, procedural shaders, and 3D rendering on a single unified execution graph, Euclidean triggers are not restricted to sound design. The same trigger pulse that drives an audio envelope can simultaneously deform procedural geometry in the viewport.

To build an audio-reactive visual installation driven by Euclidean rhythms:

1. Connect the event output of `Field Notes` into an `Analog` synth or `Drum Sampler` to produce the audible transient.
2. Split the trigger velocity into an `Envelope` or `Formula` node to generate a smoothed modulation envelope.
3. Patch the modulation cable into an `Instance on Points` node, scaling concentric rings of geometry in a `Render 3D` scene whenever a Euclidean pulse fires.
4. Route the sound output through `Audio Filter` for warmth, and observe how the 3D particles expand and contract in sample-locked synchronisation with the audio transient.

By evaluating both domains in the same execution cycle, there is zero communication latency, zero MIDI jitter, and zero frame mismatch between the kick drum transient and the on-screen geometric displacement.

You can inspect working interactive visualisers and audio-reactive shaders directly in the [Infinite embeds collection](https://infinitedevices.org/embeds/).

## Creative applications in your music

Euclidean rhythm sequencer algorithms strip away the manual labour of clicking individual grid squares in a conventional timeline. By treating rhythm as a geometric problem of spacing pulses evenly across time, you can discover syncopations and polyrhythms that feel human, balanced, and compelling.

Whether you are designing polyrhythmic techno percussion, West African bell lattices, or live audio-reactive visual performances, Euclidean sequencing turns simple modular numbers into rich musical phrases.
