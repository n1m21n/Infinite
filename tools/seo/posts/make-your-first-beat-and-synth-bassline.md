---
title: Make your first beat and synth bassline
description: Build a drum groove, patch a warm synth bassline, add space with Reverb and Delay, and mix everything to your speakers in Infinite.
date: 2026-11-23
category: creative
tags: [beginners, beatmaking, sound-design, modular]
image: assets/blog_make-your-first-beat-and-synth-bassline.png
image_alt: Node patch diagram showing Drum Sequencer and Note Sequencer into Analog, combined in Mixer through Delay and Reverb to Audio Out
keywords: make your first beat, beginner synth bassline, node-based beatmaking, Infinite tutorial
status: published
---

If you want to make your first beat in Infinite without getting lost in menus, you only need a handful of nodes. In about ten minutes, you can build a drum groove, patch a warm synth bassline that locks to the same tempo, add depth with echo and room space, and balance the levels to your speakers.

Infinite works like a tabletop of instruments and effects connected by cables. Notes flow from sequencers into synths, and audio flows from synths through effects into your speakers. Nothing is hidden behind nested mixer windows unless you want it to be.

## 1. Hear sound first with Audio Out

Every audio patch ends at **Audio Out**. Add an **Audio Out** node to the canvas and place it on the right side so you have room to build from left to right. **Audio Out** has a single audio input pin and no output pin, because it sends sound straight to your audio interface or laptop speakers.

One rule helps keep patches tidy: **Audio Out** accepts only one cable. Whenever you want to hear drums and a synth at the same time, you combine them in a **Mixer** node first and run a single cable from **Mixer** into **Audio Out**.

## 2. Build a drum groove with Drum Sequencer

Add a **Drum Sequencer** node on the left of the canvas and a **Mixer** node between it and **Audio Out**. Connect the output of **Drum Sequencer** to the first input of **Mixer**, then connect the output of **Mixer** to **Audio Out**.

Press play on the transport bar at the top of the window. **Drum Sequencer** locks to the project tempo right away and gives you eight drum lanes above a 16-step grid. You have three quick ways to start a pattern:

- **Click steps on the grid.** Click any square to turn a hit on or off, drag vertically on a lit step to change how hard it hits, or drag across a row to paint a quick hi-hat run.
- **Press Randomise.** If you want an instant kick, snare, and hat pattern to react to, click the randomise button and tweak from there.
- **Pick a built-in groove.** Open the groove picker between the lane cards and the grid to browse 141 rhythms across ten styles. Choosing a groove loads the pattern, swing, and bundled kit samples in one step, and you can switch between its A, B, and C parts for verses, bridges, and choruses.

Each of the eight lanes has its own volume, pan, pitch, and decay controls. Try shortening the decay on your hi-hats for a tighter pocket, or nudging the swing slider up slightly to give the beat a relaxed bounce.

## 3. Patch a synth bassline with Note Sequencer and Analog

Now that the drums are moving, add a bassline underneath them. Drop a **Note Sequencer** node and an **Analog** synth node below your drums on the canvas.

Connect the note output of **Note Sequencer** to the note input of **Analog**, then connect the audio output of **Analog** to the second input of **Mixer**.

In **Note Sequencer**, each vertical bar is one step. Drag the tall upper section of a bar up or down to pick the note pitch, and click the thin strip underneath to turn that step on or off. For a simple, driving bassline, start with 8 or 16 steps and leave a few rests so the kick drum has room to breathe. You can adjust **gate** to make the notes short and plucky or long and sustained.

Next, shape the sound on **Analog**:

- Turn down the **cutoff** control on the filter until the sound feels round and warm rather than buzzy.
- Bring up the **sub** oscillator to add weight an octave below the main pitch.
- Keep **attack** fast so each bass note speaks cleanly on the beat, and adjust **decay** to control how quickly the note settles.

If you want to explore more synth textures later, the [musicians guide](https://infinitedevices.org/musicians.html) walks through the other instruments and note tools built into Infinite.

## 4. Add space with Delay and Reverb

Dry drums and dry bass can sound flat on their own. To give your loop width and depth, insert **Delay** and **Reverb** between **Mixer** and **Audio Out**, or patch **Reverb** after **Analog** if you want the bass and synth chords to sit in a room while the kick stays dry and centred.

In **Delay**, tempo sync is on by default, so your echoes land in time with the beat. Set **rate** to a dotted eighth or quarter note, keep **feedback** moderate so the repeats fade cleanly, and turn on **bounce** if you want echoes to hop from left to right. Turning on the **analog** switch adds gentle tape warmth so each repeat softens slightly as it fades.

In **Reverb**, start with a short **size** and **decay**, and keep **mix** low so the groove stays punchy. A little **predelay** leaves a tiny gap before the room tail blooms, which keeps drum hits crisp.

## 5. Balance your mix and keep building

With both instruments playing through **Mixer**, use the per-slot **gain**, **pan**, **mute**, and **solo** controls to balance the drums against the bass. Press `Shift+L` at any time to open the Mixer panel for a full console view of your strips and master loudness meter.

When you are happy with the loop, you can record a take straight from the Mixer panel or pair your beat with visuals on the same canvas. Our beginner guide on how to [make your first audio-reactive visual](https://infinitedevices.org/blog/make-your-first-audio-reactive-visual.html) shows how to drive a shape from the music you just patched.

Ready to try it yourself? [Download Infinite](https://infinitedevices.org/download.html) for macOS, Windows, or Linux, or browse starter patches in the [preset library](https://infinitedevices.org/library.html) to open a beat and take it apart at your own pace.
