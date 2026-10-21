---
title: Free motion tracking for music and visuals in Infinite
description: Hand Track, Face Track and Pose Track turn a camera or picture into modulators, so a pinch, a smile or a raised arm can drive any parameter in your patch.
date: 2026-10-17
category: creative
tags: [motion tracking, hand tracking, face tracking, interactive art]
image: assets/og-image.jpg
image_alt: Infinite showing a live hand landmark overlay beside a node graph
keywords: motion tracking software free, hand tracking visuals, webcam body tracking music, interactive visuals open source
status: published
---

Free motion tracking software is built into Infinite as an optional pack: it turns a camera image into numbers you can wire to anything: a shader's size, a filter cutoff, a synth note. There are three tracking nodes, Hand Track, Face Track and Pose Track, and each one outputs modulators the same way an LFO does. This post explains what they give you, how to set them up, and where they work well.

## What you need

- Infinite 0.5.0 or later.
- The free Tracking pack (26 MB). Open Settings, then Extensions, and press Install next to Tracking. It installs while the app runs, with no restart. If you are offline, download the pack for your platform from the [Extensions page](https://infinitedevices.org/extensions.html) and use Install from file.
- A video source: a camera, a video file or a still image.

The pack is optional on purpose. If you never track anything, you never download it.

## The three nodes

All three live in the Modulators category. Each has a `present` output that says whether a subject was found, followed by the values below.

**Hand Track** gives you `palm x`, `palm y`, `index x`, `index y`, `thumb x`, `thumb y`, then `pinch`, `open`, `roll` and `size`. It also has six gesture flags: `fist`, `point`, `peace`, `thumbs up`, `ok` and `rock`.

**Face Track** gives you `head x`, `head y`, `yaw`, `pitch` and `roll`, then `eye L`, `eye R`, `brows`, `mouth`, `smile`, `gaze x` and `gaze y`.

**Pose Track** gives you `body x`, `body y`, `head x`, `head y`, both wrists as `wrist L x`, `wrist L y`, `wrist R x`, `wrist R y`, then `arm L`, `arm R`, `spread`, `lean` and `size`.

Because these are ordinary modulator outputs, you connect them the same way as any other: drag a modulation cable to a parameter. Nothing about the destination needs to know it is being driven by a body.

## Smooth, hold and mirror

Raw tracking jitters, and a hand leaving the frame for a moment should not make a visual snap to zero. Each tracking node has three settings for this:

- `mirror`, on by default, so that moving your right hand moves things to the right on screen, like a mirror.
- `smoothing`, 0.5 by default. At 0 the values are raw. Towards 1 they are very smooth. The filter is speed adaptive (One-Euro), so slow movements are smoothed hard while fast movements stay responsive.
- `holdMs`, 150 by default. After the subject is lost, the node keeps the last values for that many milliseconds before reporting that nothing is there.

The gesture flags and `present` skip the smoothing, since a gesture is either on or off. The node body shows a live landmark overlay and slider-style meters, so you can see what the tracker sees before you wire anything.

## Three patches to try

**A pinch that controls a filter.** Add a Hand Track node, then wire `pinch` to a filter cutoff on a synth or a hosted plugin. Closing your fingers closes the filter. If you want a different range, put a Range to Range node between them.

**A head that steers the picture.** Wire Face Track `yaw` and `pitch` to a shape's rotation or to a shader's offset, and `smile` to colour or brightness. For a gallery piece this is enough: a screen that leans towards the person looking at it.

**Arms as a conductor.** Wire Pose Track `arm L` and `arm R` to two different parameters, for example the level of two instruments, and `spread` to the amount of an effect. Stand back from the camera so the whole upper body is in frame.

## Tips from how the trackers work

- Light the subject from the front. A bright window behind you makes the face and hands harder to find.
- Keep the whole hand in view. `size` tells you how close it is to the camera, which is a useful modulator on its own.
- Use the gesture flags as switches and the continuous outputs as knobs. A `fist` that mutes and a `pinch` that sweeps feels natural.
- If you want motion without identifying a person or body part, the Image Analyze node can measure luminance and motion in a region of any image, with no pack needed.
- Tracking nodes report `present` as off when the input is unplugged, instead of freezing the last pose.

## Combine it with sound

Tracking is most fun when it is one part of a larger patch. Because the same graph holds audio and visuals, a hand can drive a plugin while the plugin's output drives the picture. See [modulating VST3 and AU plugins](https://infinitedevices.org/blog/modulate-vst3-plugins-with-visuals-and-lfos.html) for the plugin side, and [one graph for sound and pictures](https://infinitedevices.org/blog/one-graph-for-sound-and-pictures.html) for why the two share a clock.

## Try it

Download Infinite free from [infinitedevices.org](https://infinitedevices.org), install the Tracking pack from Settings, add a Hand Track node and watch the landmarks appear. The full parameter list is in the [Node Reference Manual](https://infinitedevices.org/assets/Infinite_Node_Reference_Manual.pdf).
