---
title: Build a generative melody and chord progression
description: Patch Chorder, Arpeggiator, Random Note Generator, Analog, Delay, and Reverb in Infinite to create evolving chords and melodies without music theory.
date: 2026-11-25
category: creative
tags: [beginners, generative music, sound design, modular]
image: assets/blog_generative-melody-and-chords-in-infinite.png
image_alt: Node patch diagram showing Chorder and Arpeggiator feeding Analog, paired with Random Note Generator, Delay, Reverb, Mixer, and Audio Out
keywords: generative melody, chord progression without music theory, Infinite arpeggiator chorder, ambient synth patch
status: published
---

You do not need to read sheet music or memorise chord voicings to write an evolving song. In Infinite, you can connect a few note nodes together so the patch chooses tasteful chords, turns them into movement, and keeps every note locked to the same key.

In this guide, you will patch a self-playing chord progression and generative melody from scratch, send it through a warm synth, and wash it in echo and room space. Every node sits in plain view on the canvas, so you can change the mood in seconds just by turning a knob.

## 1. Set up your synth and speakers first

Start on the right side of the canvas so you have room to patch from left to right:

1. Drop an **Analog** synth node, a **Mixer** node, and an **Audio Out** node onto the canvas.
2. Connect the audio output of **Analog** to the first input of **Mixer**, and connect **Mixer** to **Audio Out**.

By routing through **Mixer** right away, it is easy to add a second melody voice or a drum groove later without rewiring **Audio Out**.

## 2. Generate a chord progression with Chorder

Next, add a **Chorder** node to the left of **Analog** and connect its note output to the note input of **Analog**. Press play on the transport bar at the top of the window. You will immediately hear chords cycling in time with the project tempo.

**Chorder** keeps you in key automatically. On the top row of the node body, pick a **scale** (such as Minor or Dorian) and a **root** note. Then try two ways of working:

- **Pick a progression preset.** Open the **chords** dropdown to choose a built-in progression, or click **random** to let **Chorder** walk through natural harmonic changes on its own.
- **Shape the voicing.** Turn **notes** up from `3` to `4` for lush seventh chords, or raise **oct up** slightly so an extra note occasionally shimmers an octave higher.
- **Add human feel.** Turn **strum** up just a touch so the notes in each chord roll slightly rather than landing all at once, and nudge **hu.time** and **hu.vel** so each hit breathes.

Set **rate** to `1 bar` or `1/2` if you want slow, ambient harmonic shifts.

## 3. Turn static chords into motion with Arpeggiator

Sustained chords sound warm, but breaking them into individual notes creates instant rhythmic momentum.

1. Drop an **Arpeggiator** node between **Chorder** and **Analog**.
2. Disconnect **Chorder** from **Analog**, connect **Chorder** into **Arpeggiator**, and connect **Arpeggiator** into **Analog**.

Now every chord **Chorder** plays is broken into a moving pattern. Pick a **mode** such as **Up/Down**, **Converge**, or **Random**, and set **octaves** to `2` so the pattern climbs into a wider range. Use the **gate** knob to switch between short, plucky notes and overlapping, dreamy phrases. You can also click steps in the gate grid at the top of **Arpeggiator** to add rests and syncopation.

## 4. Layer a generative melody with Random Note Generator

Once your chords are rippling along, add an independent lead line that wanders around them without ever hitting a wrong note:

1. Add a **Random Note Generator** node and a second **Analog** node below your chord chain.
2. Connect **Random Note Generator** to the second **Analog**, and patch that **Analog** into the second input of **Mixer**.

Set **Random Note Generator** to the same **scale** and **root** as **Chorder** (or enable global scale on the transport bar so both follow one key). Use **lo** and **hi** to keep the melody sitting cleanly above the chords, then adjust **wander** to control how far the melody leaps between steps. Turning up **groove** adds subtle rhythmic swing so the line feels played rather than mechanical.

If you want to pair this melodic loop with percussion later, our step-by-step guide to [making your first beat and synth bassline](https://infinitedevices.org/blog/make-your-first-beat-and-synth-bassline.html) shows how to lock **Drum Sequencer** to the same tempo.

## 5. Shape the synth and add Delay and Reverb

To make both **Analog** voices blend like a finished record, shape their filters and give them space:

- On the chord **Analog**, turn **freq** down to soften the brightness, bring up **spread** and **detune** for width, and switch **analog** on for gentle warmth.
- Insert **Delay** and **Reverb** between **Mixer** and **Audio Out**. In **Delay**, tempo sync is active by default, so a dotted-eighth repeat weaves naturally around your arpeggio. Turn on **bounce** for stereo movement across your headphones.
- In **Reverb**, raise **size** and **decay** for an ambient tail, keeping **mix** balanced so the notes stay clear.

For more ideas on building instruments and melodic chains, visit the [musicians guide](https://infinitedevices.org/musicians.html).

## 6. Make the patch evolve over time

The real joy of a generative patch is letting it shift slowly while you listen. Drop an **LFO** node onto the canvas, set it to a slow rate such as `4 bars`, and drag its output onto the **freq** filter knob on **Analog** or the **gate** knob on **Arpeggiator**. An orange modulation dot appears on the knob, opening and closing the tone gradually over several bars.

Ready to hear it for yourself? [Download Infinite](https://infinitedevices.org/download.html) for macOS, Windows, or Linux, or open a starter patch from the [preset library](https://infinitedevices.org/library.html) to explore how the nodes fit together.
