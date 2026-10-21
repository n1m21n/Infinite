---
title: Modulate any VST3 or AU plugin with LFOs, audio and visuals
description: Load a third-party plugin into Infinite and drive its parameters from an LFO, an envelope follower or something moving on screen.
date: 2026-10-17
category: creative
tags: [vst3, au, modulation, plugins, sound design]
image: assets/product_canvas_1400.webp
image_alt: A plugin host node inside the Infinite canvas
keywords: modulate vst3 parameters, modular daw plugin hosting, lfo for any plugin, free modular vst host
status: published
---

Plugins usually come with a fixed set of modulation: the LFOs and envelopes their authors built in. In Infinite, a hosted plugin is just another node, and its parameters are controls like any other. Anything that makes a number can move them.

## What can be hosted

Infinite hosts **Audio Unit** plugins on macOS and **VST3** plugins on macOS, Windows and Linux. Each plugin opens in its own native window, and its parameters appear as controls you can automate and modulate.

## Three ways to move a knob

1. **An LFO.** Add an LFO node, connect it to the parameter, and set the range. It is the quickest way to give any filter or reverb some life.
2. **The sound itself.** An **Envelope Follower** turns the loudness of a signal into a number. Send it from a drum bus to a plugin's filter cutoff for simple ducking and pumping.
3. **A picture.** An **Image Analyze** node reads luminance or motion from a video or a shader. Wire it to a plugin knob and the music reacts to what you see.

## A patch to try

1. Add a synth, then your favourite delay or reverb plugin after it.
2. Add an LFO and connect it to the plugin's mix or feedback control.
3. Add an Envelope Follower on the synth and connect it to the plugin's filter.
4. Draw a shape and let the same LFO drive its size, so what you hear and what you see move together.

## Why it matters

Most hosts can automate a plugin, but only from inside that host's own lanes. Here the source can be a synth, a camera, a physics simulation or a few lines of [Field code](https://infinitedevices.org/blog/field-language-write-what-a-node-does.html). Your existing plugins gain a modulation system they never had.

Infinite is free and open source. The [Node Reference Manual](https://infinitedevices.org/assets/Infinite_Node_Reference_Manual.pdf) lists every modulator and what it outputs.
