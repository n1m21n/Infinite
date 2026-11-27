---
title: Chop and remix a drum loop with Slicer
description: Load or record a drum loop in Infinite, chop it into playable slices with Slicer, sequence new grooves with Note Sequencer, and add Delay and Reverb.
date: 2026-11-27
category: creative
tags: [beginners, beatmaking, sampling, sound design]
image: assets/blog_chop-and-remix-a-drum-loop-with-slicer.png
image_alt: Node patch diagram showing Slicer chopping a drum loop into slices triggered by Note Sequencer and MIDI Input, routed through Audio Filter, Delay, Reverb, and Audio Out
keywords: chop a drum loop, slice samples beatmaking, drum slicer modular, Infinite Slicer
status: published
---

When you chop a drum loop, you turn an existing recording into a beat of your own. Instead of playing a sample from start to finish at one fixed rhythm, you split it into individual hits and trigger those pieces in any order you like.

In [Infinite](https://infinitedevices.org/), the **Slicer** node handles the entire workflow on the canvas. You can drop in a drum loop, record a snippet from another node, let **Slicer** find every hit automatically, and rearrange the groove with a sequencer or MIDI controller in a few minutes.

![Node patch diagram showing Slicer chopping a drum loop into slices triggered by Note Sequencer and MIDI Input, routed through Audio Filter, Delay, Reverb, and Audio Out](https://infinitedevices.org/assets/blog_chop-and-remix-a-drum-loop-with-slicer.png)

## 1. Patch Slicer to your speakers first

Start by giving **Slicer** a clean path to your speakers so you can audition every change right away:

1. Drop a **Slicer** node, a **Mixer** node, and an **Audio Out** node onto the canvas, arranged from left to right.
2. Connect the audio output of **Slicer** to the first input of **Mixer**, then connect **Mixer** to **Audio Out**.

Click **Load...** at the top of **Slicer** to pick a WAV, AIFF, FLAC, or MP3 drum loop from your computer. If you want to hear the whole loop before chopping it, click **Audition**. You can also patch any synth or drum node into the **record in** pin on **Slicer** and press **Record** to capture a fresh loop straight off the canvas.

## 2. Choose between onset hits and a tempo grid

Once a loop is loaded, **Slicer** draws the waveform across the middle of the node with vertical markers showing where each slice begins. Use the **slice by** dropdown to pick how those markers are placed:

- **onsets**: **Slicer** listens for the sharp attack of each kick, snare, and hi-hat and places a marker at every hit. Adjust **count** to set the maximum number of slices (up to `64`), or turn **sensitivity** up or down and click **re-slice** if quiet ghost notes are being missed or extra markers appear.
- **grid**: **Slicer** divides the loop into even rhythmic slices locked to your project tempo. Use the **division** dropdown to pick eighth notes, sixteenth notes, or triplets. Grid mode is ideal when you want tidy, equal-length chops that stay tight regardless of how busy the original drum pattern is.

## 3. Trigger slices with Note Sequencer or a MIDI keyboard

**Slicer** maps each slice to a musical note starting at `C1` for the first slice, `C#1` for the second, `D1` for the third, and so on up the keyboard. That means any note node or controller can play your chopped hits:

1. Drop a **Note Sequencer** node to the left of **Slicer** and connect its note output to the **notes** input on **Slicer**.
2. Press play on the transport bar at the top of the window.
3. Drag the vertical bars in **Note Sequencer** around `C1` and the octave above to rearrange the kicks, snares, and hats into a brand new pattern.

If you prefer to tap rhythms out by hand, connect a **MIDI Input** node to **Slicer** instead, or draw your pattern in **Piano Roll** for longer multi-bar variations. Any note above the last slice stays silent, so you never trigger stray audio by accident. If you want to layer a synth bassline underneath your chopped break, our guide to [making your first beat and synth bassline](https://infinitedevices.org/blog/make-your-first-beat-and-synth-bassline.html) walks through adding **Analog** into the second slot of **Mixer**.

## 4. Shape the chop with pitch, speed, and crossthrough

Once your new groove is looping, the lower controls on **Slicer** let you change the character of the kit without touching the original file:

- **pitch and finetune**: Tune the whole drum kit down a few semitones for heavier, dusty breaks, or pitch it up for snappy percussion.
- **speed**: Stretch or compress playback from `0.25x` to `4.00x` independently of **pitch** to tighten sluggish hits or give short chops extra body.
- **attack and decay**: Raise **attack** slightly to soften sharp clicks at the start of a slice, or pull **decay** down for short, gated hits. Turning **decay** all the way up switches it to **hold**, keeping each slice at full level until it ends.
- **crossthrough**: Leave **crossthrough** off for classic tight chops where every hit stops cleanly before the next slice begins. Turn **crossthrough** on when you want a triggered hit to ring out through the rest of the loop.

## 5. Add movement with filters, Delay, and Reverb

Chopped loops come alive when you treat them like an instrument rather than a static recording. Insert an **Audio Filter** between **Slicer** and **Mixer**, then patch an **LFO** onto the filter **cutoff** knob so the brightness breathes gently over four bars. Adding **Delay** and **Reverb** between **Mixer** and **Audio Out** puts the dry break into a shared room; keep **mix** modest on **Reverb** so the kick and snare stay punchy.

You can read more about Infinite's built-in instruments and sampling tools on the [musicians page](https://infinitedevices.org/musicians.html). Keep in mind that **Slicer** caps each loop at `64` slices, and Infinite is a young open-source project without AAX plugin support.

Ready to chop your own breaks? [Download Infinite](https://infinitedevices.org/download.html) for macOS, Windows, or Linux, or open a starter beat from the [preset library](https://infinitedevices.org/library.html) and drop **Slicer** straight into the mix.
