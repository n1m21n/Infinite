---
title: Render video and audio from a text file with no window open
description: Infinite's headless mode turns a plain-text patch into stills, video and a loudness report from the command line. Here is the five-minute workflow.
date: 2026-10-17
category: product
tags: [headless, cli, rendering, automation]
image: assets/product_canvas_1400.webp
image_alt: The Infinite node canvas with audio and visual nodes wired together
keywords: headless daw, render audio from command line, scripted video rendering, ci audio testing
status: published
---

A headless DAW is one that renders with no window open. Most DAWs and visual tools only run with a window and a mouse. Infinite's engine does not need either. The executable inside the app is also a command-line tool, so a patch can be validated, drawn and rendered from a terminal, a script or a CI job.

This post walks through the loop from the [Headless User Guide](https://infinitedevices.org/assets/Infinite_Headless_User_Guide.pdf): describe, write, validate, explain, look and listen, render.

## What you need

- The Infinite app. There is no separate package; the executable inside the app is the tool.
- A machine with working OpenGL, because frames are drawn by the real GPU pipeline.
- On Linux, a virtual display. Run each render as `xvfb-run -a Infinite ...`.
- A text editor. Patches are plain text files ending in `.inf`.

Patches that use a camera, microphone or MIDI device are refused in headless mode (exit code 4), since there is no hardware to read. Swap them for a file or a generator.

## A complete patch in a dozen lines

Save this as `hello.inf`:

```
infinite-patch 1
node 1 Source Shape
  id shape
  i shapeType Star
end
node 2 Modulators LFO
  id lfo
end
node 3 Utility Output
  id out
end
cable out 0 shape
mod shape sizeX lfo 0 0 1 0.5
expr shape rotation sin(t) * 90
```

That is one shape, one LFO and an output. The LFO drives the shape's width, and an expression spins it.

## Validate, draw, render

```
Infinite --validate hello.inf
Infinite --frame hello.inf 0,1,2 pics/ --contact-sheet sheet.png
Infinite --render hello.inf hello.mp4 --duration 4 --fps 30
```

`--validate` loads the file strictly and lists every problem with its line number. `--frame` draws stills at 0, 1 and 2 seconds plus one labelled contact sheet. `--render` writes the video.

## Ask the engine, do not guess

Two commands save a lot of trial and error:

- `Infinite --describe "<node type>"` lists a node's controls with their defaults, ranges and dropdown options.
- `Infinite --explain hello.inf` prints the graph that was actually built, so you can check every wire. A patch can validate and still be the wrong patch.

## Measure the sound

`--audio-summary` describes the audio as numbers: loudness, peaks, clipping, silence, a spectrum and a tempo guess.

```
Infinite --audio-summary hello.inf summary.json --wav out.wav --duration 4
```

Add `--stems id,id --stems-dir dir` to write one WAV per named node, or `--output node` to measure a single Audio Out instead of the whole mix.

## Reading the result

The last line printed is always one JSON object with `ok`, `mode`, `frames`, `fps`, `size`, `audio`, `elapsed_ms`, `files`, `warnings` and `errors`. A script can parse that line and branch on `ok` without scraping log text.

## Where this is useful

- **Regression tests for sound and picture.** Render a corpus of patches on every commit and compare the summaries.
- **Batch exports.** Produce a hundred variations of a piece from one patch with `--set node.param=value`.
- **Bots and servers.** Take a patch from a chat message and send back a video.

Want to see what a patch looks like before scripting it? Start with the [first steps tutorial](https://infinitedevices.org/blog/make-your-first-audio-reactive-visual.html). Infinite is free and open source for macOS, Windows and Linux. The full flag list is in the Headless User Guide, and the patch format is documented in the repository.
