---
title: Modulate any VST3 or AU plugin with LFOs, audio and visuals
description: How to modulate VST3 parameters in Infinite DAW using modular LFOs, audio envelopes, webcam feeds and real-time visual movement.
date: 2026-10-17
category: creative
tags: [vst3, au, modulation, plugins, sound design]
image: assets/blog_modulate-vst3-plugins-with-visuals-and-lfos.png
image_alt: A plugin host node inside the Infinite canvas
keywords: modulate vst3 parameters, modular daw plugin hosting, lfo for any plugin, free modular vst host
status: published
---

Most digital audio workstations treat third-party plugins as static black boxes. You insert a synthesiser or effect into an audio track, open its bespoke window, and rely solely on the modulators the plugin developer chose to include. If your favourite compressor lacks sidechain filtering, or your preferred synthesiser lacks an extra complex LFO, you are normally out of options.

Infinite takes a modular approach where every hosted plugin becomes an open node on a shared canvas. When you host a plugin, you can modulate VST3 parameters directly with virtual patch cables, routing numbers from procedural LFOs, envelope followers, sound generators, and interactive graphics straight into plugin knobs.

## What plugins can be hosted

Infinite hosts **Audio Unit** (AU) plugins on macOS and **VST3** plugins across macOS, Windows and Linux. When you add a plugin node to the canvas:

- The plugin opens in its native editor window for tactile preset selection.
- Every automated parameter exposed by the plugin is registered as an addressable control port.
- Input ports receive audio, MIDI note events, and continuous control values (CV).
- Output ports deliver processed audio back into the master bus, a spatialiser, or an audio analysis node.

This architecture works with modern instruments like Vital, Surge XT and Serum, as well as creative effects from Valhalla, FabFilter, and native open-source audio tools.

## Four ways to modulate plugin parameters

Because parameter inputs accept standard continuous signals from any other node, you are not restricted to traditional DAW automation lanes.

### 1. Multi-waveform LFOs
Add an **LFO** node, select a waveform (sine, triangle, ramp, square, or random sample-and-hold), and connect its output to the desired parameter port. You can dial in the modulation depth and baseline offset independently. By combining two LFOs through a **Math** node, you can create evolving, non-repeating modulation curves for filters and chorus rates.

### 2. Audio signals and envelope followers
An **Envelope Follower** converts the instantaneous amplitude of an audio stream into a continuous control signal. If you route the kick drum bus into an Envelope Follower and patch the output to the wet mix or cutoff of a reverb plugin on a synth lead, you get precise, transparent ducking without setting up sidechain routing menus.

### 3. Visuals and video analysis
In Infinite, audio and visuals operate on the same canvas engine. An **Image Analyze** node evaluates visual luminance, edge density, or optical flow motion vectors from a camera or a generative shader. When wired to a plugin parameter, physical movement in your studio or rhythmic pulses on the screen directly modulate synthesiser resonance, delay feedback, or distortion drive.

### 4. Custom Field kernel modulators
When standard LFO shapes are not enough, the **Field Language** node lets you write custom per-sample math. A single expression can compute chaotic attractors, Euclidean step sequences, or stochastic noise formulas running at audio rate.

## A step-by-step modular patch

Here is a practical patch you can build in two minutes:

1. Press `Tab`, type `Plugin`, and load your favourite VST3 synthesiser.
2. Route a **Note Sequencer** node into the plugin's MIDI input port.
3. Add a second **Plugin** node and load an external delay or reverb effect. Cable the audio outputs from the synthesiser into the effect.
4. Add an **LFO** node set to a slow sine wave (around 0.15 Hz) and connect it to the reverb mix parameter.
5. Add an **Envelope Follower** after the synthesiser and connect its modulation output to the cutoff of the reverb.
6. Add an **Audio Analyze** node and pipe its spectral output into a real-time shader, so your sound and visual geometry evolve in lockstep.

## Why modular plugin hosting matters

Traditional automation ties parameter movements to a static horizontal timeline. In contrast, modular parameter modulation makes sound design generative and reactive. For related routing workflows, explore the [modular VST3 host audio routing guide](https://infinitedevices.org/blog/modular-vst3-host-audio-routing-and-modulation.html) and our breakdown of [node-based DAW signal paths](https://infinitedevices.org/blog/node-based-daw-modular-audio-routing.html). If you are crafting basslines and plucks from scratch, see the [electronic music sound design tutorial](https://infinitedevices.org/blog/electronic-music-sound-design-sub-bass-pluck-pad.html).

Infinite is free and open source. The [Node Reference Manual](https://infinitedevices.org/assets/Infinite_Node_Reference_Manual.pdf) documents every available modulator, math operator, and CV routing node.
