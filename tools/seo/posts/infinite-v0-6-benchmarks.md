---
title: Infinite v0.6 benchmarks: where it is fast and where it is not
seo_title: Infinite v0.6 benchmarks: where it is fast, where it is not
description: Infinite benchmarks on a base MacBook Air M2 against Blender, Vital, Serum 2 and popular DAWs, with the results published, including where Infinite is slower.
date: 2026-10-17
category: product
tags: [benchmarks, performance, open source, audio]
image: assets/og-image.jpg
image_alt: Infinite v0.6 benchmark report summary
keywords: infinite benchmarks, daw performance comparison, vital vs serum cpu, blender video export speed
status: published
---

These are the Infinite v0.6 benchmarks: Infinite measured against the tools you already use, on one ordinary laptop, with the same tests for everyone. We publish the numbers that are good for us and the ones that are not. The full report is on the [benchmarks page](https://infinitedevices.org/benchmarks.html), with a PDF and the raw data as JSON.

## How we tested

Everything ran on a base MacBook Air with an Apple M2 and 8 GB of memory, on macOS, between 12 and 17 October 2026. That is the cheapest model on purpose: if it is fast there, it is fast on yours. Nothing was measured on Windows or Linux.

The tests treat every product as a black box. Notes or a test signal go in, and what comes out is measured. Competitor plugins run inside Infinite's own plugin host, with default settings and no hand-picked presets, and the same code measures them as measures Infinite's own nodes. The tools are in the repository under `tools/re-survey`, so you can repeat any of it.

## The good news

- **Launch:** 0.34 seconds from launch to first frame. Opening a 400-node patch takes 6 ms.
- **Footprint:** 244 MB of memory with the app open and idle, and a 45 MB download. That is less memory than Ableton Live (672 MB), Blender (693 MB), Bitwig (1,392 MB) and TouchDesigner (1,396 MB) in the same idle test.
- **Synth accuracy:** The Wavetable synth has a worst pitch error of 0.2 cents across the keyboard. Vital and Serum 2 measure the same. Off-note energy stays between about -81 and -85 dB, even at the top of the keyboard where cheap synths alias.
- **Synth cost:** The Wavetable synth uses 1.45 ms of CPU per second of audio for each voice. Vital uses 2.82 ms and Serum 2 uses 3.23 ms.
- **Video export:** On matched test scenes, Infinite exported ProRes 4444 at 170 frames a second at 720p and 44 at 4K. Blender managed 1.6 and 0.5. These are different renderers, so this compares throughput, not image style.
- **Plugins:** 97.6 % of the plugins installed on the test machine load and pass audio in Infinite's plugin host.
- **Live use:** zero audio dropouts and zero missed screen refreshes in the standard live-performance test.

## Where Infinite is behind

We list these on the report as below target:

- **Idle CPU:** With nothing loaded, Infinite sits at 12.3 % of one core. Logic, Blender and Vital sit under 4 %.
- **Built-in effects:** The EQ, Limiter, Drive and Chorus cost more CPU than the plugin equivalents.
- **Instancing:** 10,000 instanced objects cost 3.1 ms a frame in Infinite and 1.3 ms in Blender.
- **Heavy projector scenes:** The heaviest test (four 4K videos, three output windows, a camera and Syphon at once) drops frames. Normal and 1080p scenes do not.
- **Default levels:** Reverb, Delay, Chorus and Flanger come out louder than they go in at their default settings.

## What the numbers do not say

One laptop is one data point. The comparisons are about throughput and cost under a fixed test, not about taste: a faster export does not make a better picture, and equal pitch accuracy does not make two synths sound alike. Windows and Linux numbers will come later.

## Check it yourself

Everything is repeatable. Read the [benchmark report](https://infinitedevices.org/benchmarks.html), download the raw JSON, or run the scripts in `tools/re-survey` on your own machine and tell us where your numbers differ. For how the headless renderer works, see [render video and audio from a text file](https://infinitedevices.org/blog/render-video-and-audio-from-a-text-file.html).
