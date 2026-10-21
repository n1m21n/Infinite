---
title: Make your first audio-reactive visual in Infinite
description: A beginner walkthrough from a free template to a shape that moves with your music, using the Audio Analyze node and a single cable.
date: 2026-10-17
category: creative
tags: [tutorial, audio reactive, beginners, templates]
image: assets/howto_daw.webp
image_alt: Infinite open on a patch that makes music and pictures together
keywords: how to make audio reactive visuals, audio visualizer tutorial, music visualiser free, node based visuals beginner
status: published
---

This is the shortest path from nothing to a picture that follows your music. You need Infinite and about ten minutes. No plugins or extra software.

## 1. Start from a template

Open the [Library](https://infinitedevices.org/library.html) and download **Moves with the music**. It is a small patch where a shape grows with the sound. Open the file from **File, Open** in Infinite and press play.

A template is a normal patch, so everything in it can be changed.

## 2. Follow the cable

The patch has two halves, and the template labels them with comments on the canvas.

- **The sound.** A **Random Note Generator** plays an **Analog** synth into **Audio Out**.
- **The picture.** A **Shape** goes through a **Bloom** effect to the **Output** node.

What joins them is an **Audio Analyze** node. It listens to the sound and turns it into moving numbers: level, low, mid and high. Those numbers drive the shape's Size X and Size Y. Controls that are driven by the music show an orange dot.

That is the whole trick. Any number can move any control.

## 3. Change what moves

Try these one at a time:

1. Switch the source of Size X from level to **low** or **high**. Low reacts to bass, high to the bright parts of the sound.
2. Change the **Depth** of a driven control to make it move more or less.
3. Raise the **Intensity** on Bloom for a stronger glow.
4. Add an **LFO** and connect it to a colour control, so the picture drifts even when the music is quiet.

## 4. Bring your own sound

Replace the template's synth with a **Sampler** or an audio file, or load a **VST3 or AU plugin** and play it from your MIDI keyboard. The Audio Analyze node listens to whatever you connect to it.

## 5. Save and share

Patches are plain-text `.inf` files. Send the file to a friend and they get exactly your setup. If you make something you like, the [Discord](https://discord.gg/7cpQfCxnx) is a good place to post it.

## Where to go next

- Read [how sound and pictures share one graph](https://infinitedevices.org/blog/one-graph-for-sound-and-pictures.html).
- Learn to write your own effect with [Field](https://infinitedevices.org/blog/field-language-write-what-a-node-does.html).
- Browse the other templates in the Library: Modulators, Synths, 3D and Compositing are good second stops.
