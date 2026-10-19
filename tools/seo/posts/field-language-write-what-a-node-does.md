---
title: Field: write a short kernel and let the node decide where it runs
description: Field is the language built into Infinite. One kernel can become a shader, a geometry deformer or an audio effect, depending on the node it lives in.
date: 2026-10-17
category: product
tags: [field, shaders, dsp, creative coding]
image: assets/field_pixel_editor.webp
image_alt: The Field Pixel code editor open inside Infinite
keywords: kernel language for shaders and audio, glsl shader editor, audio dsp scripting, creative coding language
status: published
---

Most nodes do one fixed thing. A Blur node blurs and a Delay node delays. Creative work often needs something in between, so Infinite includes **Field**, a kernel language for shaders, geometry and audio: a small language for writing what a node does.

## One idea: a kernel run once per element

A Field node runs your kernel once for every element of its domain, forever. The domain is chosen by the node, not by you:

```
col = vec3(uv.x, uv.y, 0.5)              // once per pixel, on the GPU: a shader
P.y += sin(P.x * 4.0 + t) * 0.2          // once per point in a mesh: a geometry deformer
out = in * gain                          // once per audio sample: a DSP effect
```

You never declare a backend. The same way of thinking covers pictures, shapes and sound.

## The Field nodes

| Node | Domain | What it does |
|---|---|---|
| Field Pixel | pixel | Per-pixel kernel compiled to GLSL, with `state` cells for feedback |
| Field Modifier | element | Per-vertex kernel over existing geometry |
| Field Primitive | element | Generates point geometry such as spirals, lattices and torus knots |
| Field Effect | sample | Per-sample audio effect with `in`, `out`, `state` and `param` |
| Field Synth | sample | A polyphonic synth voice driven by `freq` and `gate` |
| Field Notes | sample | Sends notes with `note(pitch, vel, len)` and reads the transport |
| Field Graph | graph | Runs at edit time and builds real nodes and cables |

## Knobs that become pins

A kernel can declare parameters in its own code, and each one becomes a real input on the node:

```
param float cutoff = 0.25 [0.01, 0.99]
state float z = 0
z += (in - z) * cutoff
out = z
```

That is a one-pole low-pass filter. The `cutoff` pin can be wired to an LFO, a MIDI knob or the output of an Audio Analyze node like any other control, and it survives save and load.

## Why a language, not another plugin format

A kernel is short enough to read in one go and to share as a text file. The Library has a growing set of free Field devices to start from, and the language reference is available as a [PDF manual](https://infinitedevices.org/assets/Field_Language_Manual.pdf).

To see where a kernel's numbers can come from, read [how sound and pictures share one graph](https://infinitedevices.org/blog/one-graph-for-sound-and-pictures.html). Infinite is free and open source for macOS, Windows and Linux.
