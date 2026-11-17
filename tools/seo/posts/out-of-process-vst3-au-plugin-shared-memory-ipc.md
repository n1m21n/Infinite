---
title: Out-of-process VST3 and AU plugin architecture with shared memory
seo_title: Out-of-process VST3 and AU plugin IPC | Infinite
description: How Infinite runs a modular synth and GPU visual engine as a VST3 and AU plugin using lock-free shared-memory IPC, PBO readback, and host isolation.
date: 2026-11-15
category: product
tags: [vst3, au, ipc, dsp, architecture]
keywords: out-of-process vst3 plugin, shared memory audio ipc, modular vst3 au plugin, lock-free ring buffer audio, gpu plugin ui readback
image: assets/blog_out-of-process-vst3-au-plugin-shared-memory-ipc.png
image_alt: Diagram of the Infinite out-of-process VST3 and AU plugin architecture showing the host DAW shell, shared-memory audio ring buffer, triple-buffered pixel transport, and out-of-process engine
status: published
---

An out-of-process VST3 plugin keeps a modular audio-visual engine from crashing or stalling the host DAW. Recent open-source audio releases, from cross-platform CLAP/VST3/AU sample libraries like Floe to Rust-based workstations like Transit and browser AudioWorklet experiments, show how much engineering effort goes into keeping plugin hosts stable while running rich DSP and custom interfaces.

When a plugin only runs a few biquad filters and draws sliders with a CPU canvas, living inside the DAW's address space is straightforward. When the plugin is an entire modular workstation with 377 nodes, 23 synthesisers, 29 audio effects, live GLSL shaders, and a 3D renderer, running in-process creates two immediate hazards: GPU context collisions with the host windowing system, and audio thread stalls if a dynamic graph or shader compilation hiccups.

The [Infinite VST3 and AU plugin](https://infinitedevices.org/plugin.html) solves both problems by splitting the plugin into two OS processes: a minimal native shell loaded by the DAW, and a separate engine process that runs the node graph, DSP, and GPU rendering.

## Why modular audiovisual engines clash with DAW hosts

Inside a host such as Ableton Live, Logic Pro, or Reaper, every loaded plugin shares the host process's OpenGL or Metal state, heap allocator, and UI event loop. A single uncaught driver quirk in a 3D node or a heavy `GLSL` fragment pass can bring down the entire session, taking unsaved tracks with it.

Isolation also protects real-time audio. In a standalone app, Infinite already enforces a strict lock-free audio thread (described in our notes on [writing audio DSP without JUCE](https://infinitedevices.org/blog/dsp-without-juce.html)). Moving the engine into its own process extends that boundary to the DAW itself: the shell inside the host contains no node graph, no shader compiler, and no 3D scene state.

## Shared-memory audio and MIDI transport

Each plugin instance allocates one shared-memory region (`PluginIpc`) created by the shell in the host process and opened by the engine helper. Every sample, MIDI event, and parameter change crosses process boundaries through pre-allocated ring buffers with zero heap allocation or mutexes on either audio thread.

The shared header (`kMagic = 0x494E4650`) lays out five regions:

- **Block slots (`kSlots = 64`)**: each `BlockIn` entry carries the frame count, offline flag, DAW transport state (tempo, PPQ, time signature, loop bounds), up to 512 sample-accurate MIDI events (`kMaxEvents`), and up to 256 parameter automation points (`kMaxParamPoints`).
- **Input audio ring (`inRing`)**: four channels (`kInChannels = 4`) carrying main stereo L/R plus sidechain stereo L/R, indexed by absolute frame count.
- **Output audio ring (`outRing`)**: two channels (`kOutChannels = 2`) written by the engine and read by the shell.
- **Message rings (`toEngine` and `toShell`)**: dual 1 MiB (`1u << 20` bytes) framed byte rings used strictly on non-realtime message threads for patch state, editor input, and automation edits.

To avoid blocking the DAW's audio callback, the shell uses a fixed one-block lookahead (`L = maxBlock`). On block `[P, P + n)`, the shell posts the incoming audio, MIDI, and automation to `inRing` and `Slots`, then reads cooked output from `outRing` at `[P - L, P - L + n)`.

In real-time mode, the shell performs a bounded wait for the engine to reach `P - L + n`. If the engine misses the deadline, the shell outputs silence, increments an `xruns` counter visible in the UI, and moves on without stalling the host. If the engine stalls long enough that slots would overflow, the shell skips posting and the engine zero-fills the gap (`gaps` counter) on resync so stale audio never replays. During offline bounce (`kOffline`), the shell switches to an unbounded wait so every frame renders sample-accurately regardless of GPU or CPU load.

## Avoiding Ableton's 64-parameter clutter

Modular hosts often expose thousands of internal parameters to the DAW, flooding automation lanes and device panels. Infinite defines `kHostSlots = 128` automatable host slots, intentionally above Ableton Live's 64-parameter auto-populate threshold. As a result, the device panel opens clean and only populates the specific knobs a musician maps from nodes like `Wavetable`, `Granular`, `Modal`, or `Field Patcher`.

## Triple-buffered GPU pixel transport for the UI

Rendering a node canvas, `Spectrum` analysers, and `Render 3D` viewports inside a DAW plugin window without sharing a graphics context requires a second lock-free channel (`PluginFrames`, `kMagic = 0x46464E49`).

The engine renders its full interface into a hidden GPU window, then reads the pixels back asynchronously using two Pixel Buffer Objects (PBOs). By mapping the PBO at the start of the following frame after buffer swap, the readback avoids stalling the GPU pipeline. The 8-bit BGRA rows are copied top-down into a shared-memory triple buffer (`kBuffers = 3`) using an atomic `middle` index exchange with a `kDirty` flag:

1. **Engine (writer)** fills `buffer[back]`, then executes `back = exchange(middle, back | kDirty)`.
2. **Host view (reader)** checks `middle` on a 120 Hz display timer; if `kDirty` is set, it swaps `front = exchange(middle, front)` and blits to CoreGraphics, Windows `BI_RGB`, or X11 `ZPixmap`.

Neither side waits on the other. When the plugin editor window is closed (`ViewState::open == 0`), the engine stops PBO readback altogether, leaving zero GPU copy overhead while audio continues running. Users can also pop the editor out into the engine's own native window (`kMsgPopout`), which suspends readback and gives direct hardware presentation. Mouse, scroll, and keyboard events travel in reverse from the host view to the engine as `InputEvent` structs over the message ring.

## Honest limits of the current architecture

This split design brings real trade-offs that are worth stating plainly:

- **One-block latency**: the lookahead buffer adds `maxBlock` samples of latency (reported to the DAW for plugin delay compensation). At 48 kHz and 256 samples, that is 5.33 ms.
- **Format coverage**: the plugin ships as VST3 and AU on macOS, and VST3 on Windows and Linux. AAX (Pro Tools) is not supported.
- **Readback bandwidth**: when the embedded plugin window is open at high DPI, copying BGRA frames over shared memory uses extra memory bandwidth compared to native single-process rendering, though closing the editor or popping out the window removes it.

All 377 nodes, 142 factory presets, and full standalone parity are available in the [Infinite VST3 and AU plugin](https://infinitedevices.org/plugin.html), and you can inspect our real-time DSP and frame-rate measurements on the [Infinite benchmarks page](https://infinitedevices.org/benchmarks.html).
