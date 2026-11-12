---
title: One graph for sound and pictures: how Infinite keeps them in step
seo_title: One graph for sound and pictures: how Infinite syncs them
description: Infinite runs a pull-based audio graph and a GPU texture pipeline off one BPM transport, so audio can drive visuals and visuals can drive audio.
date: 2026-10-17
category: product
tags: [architecture, audio, opengl, modulation]
image: assets/nodegraph.webp
image_alt: A node graph in Infinite with audio and image nodes connected by cables
keywords: audio reactive visuals engine, node based audiovisual workstation, audio and video sync
status: published
---

Video tools and audio tools usually live in separate programs. You wire them together with OSC, MIDI clock or a texture-sharing protocol, and you accept a little latency and jitter on the way. Infinite takes a different route: audio and visuals are two parts of one engine that share a transport.

## Two graphs, one clock

The engine has two sides, and each one follows the rules of its own medium.

- **Audio** is a pull-based, sample-accurate graph. If a buffer is late, you hear a click, so it is built around a deadline.
- **Visuals** are a GPU texture pipeline on GLSL and OpenGL 3.2 Core. A dropped frame is far less noticeable, but throughput matters.

Both sides are tied to a global BPM transport, so a beat means the same moment in the sound and in the picture.

## Modulation crosses the boundary

The useful part is what happens between the two. Any signal can modulate any control:

- an **Audio Analyze** node gives an 8-band FFT and onset detection;
- an **LFO**, **Envelope Follower**, **Random** or **Pattern CV** gives a moving number;
- an **Image Analyze** node reads luminance or motion from a picture.

Wire the output of one of those to a control and it moves. That control can be a shader parameter, a 3D transform, a synth knob or the slider of a hosted plugin. Sound drives pictures, and pictures drive sound.

## Why one canvas helps

When both live on the same canvas, the connection is a cable you can see. There is no second application to launch, no port to configure and no project that has to be kept in sync by hand. A patch is also a plain-text `.inf` file, so it can be versioned, diffed and rendered [without a window](https://infinitedevices.org/blog/render-video-and-audio-from-a-text-file.html).

## Try it

Open the **Moves with the music** template from the [Library](https://infinitedevices.org/library.html). It is a shape that grows with the sound, and it is the smallest example of the idea above.

Infinite is free and open source for macOS, Windows and Linux.
