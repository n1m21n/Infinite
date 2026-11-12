---
title: Hardware MIDI and controller mapping in Infinite
description: Connect hardware synthesizers, MIDI controllers, and DMX lighting to Infinite with low latency routing and parameter modulation.
date: 2026-10-19
category: product
tags: [hardware, midi, controller mapping, live performance, dsp]
image: assets/blog_hardware_midi.png
image_alt: hardware midi routing and modulation matrix in infinite
keywords: midi controller daw, hardware synth routing, low latency midi, live electronic music setup, modular daw
status: published
---

performing electronic music live with hardware should feel direct and immediate. yet most traditional setups require juggling multiple applications: one daw for sequencing, another utility for midi cc routing, a visual package like touchdesigner for projections, and external software for dmx stage lighting.

infinite unifies this entire live performance rig into a single real-time engine. with sub-millisecond midi scheduling and direct parameter binding, any hardware knob, fader, or pad can modulate synths, audio filters, 3d geometry, and stage lights simultaneously.

## 1. low-latency midi device discovery

infinite natively enumerates all connected midi input and output interfaces without requiring driver restarts or app relaunches.

whether you plug in a usb class-compliant controller (like an akai apc40, novation launchpad, or arturia keystep) or a multi-port midi interface connected to vintage hardware synthesizers via 5-pin din cables, infinite binds them immediately.

in the node graph, you interact with hardware via dedicated hardware nodes:
- **midi in**: captures note-on, note-off, velocity, pitch bend, aftertouch, and control change (cc) streams.
- **midi out**: sends sample-accurate clock, transport start/stop, and polyphonic note sequences back to your analog synths and drum machines.
- **midi cc in**: isolates a specific cc number (0 to 127) and converts it into a normalized float pin (`0.0` to `1.0`) with optional slew smoothing.

## 2. one-click midi learn across audio and visuals

mapping a physical fader to any parameter in infinite is instantaneous:
1. right-click any parameter knob or slider on any node (e.g., the cutoff of a **moog filter** or the zoom of a **field pixel** shader).
2. select **midi learn**.
3. move the physical knob or fader on your hardware controller.

the parameter is immediately bound. because infinite uses a normalized float bus for all modulation signals, that single hardware fader can be split and routed to multiple destinations at once:
- fader 1 pulls down the audio master lowpass filter cutoff.
- fader 1 simultaneously collapses the 3d wireframe field into a tight geometric core.
- fader 1 dims the ambient stage light intensity.

you achieve total audiovisual cohesion with zero latency mismatch between what the audience hears and what they see.

## 3. bidirectional feedback and led motorized faders

for live performers using controllers with led rings or motorized faders, infinite provides bidirectional state synchronization. when an automated lfo or ai agent moves a parameter in the software, infinite transmits the updated cc value back out through **midi out**, keeping your physical hardware controls in perfect physical alignment with the software state.

## 4. stage lighting: dmx and art-net integration

infinite does not stop at audio and visuals; it also outputs dmx lighting frames over open protocols:
- map the kick drum audio transient from **audio analyze** to dmx channel 1 (strobe intensity).
- map the root note pitch from **midi in** to dmx rgb fixtures across your stage.

with infinite, your entire live performance (audio synthesis, visual projection, hardware sequencing and lighting) runs inside a single memory space, running rock solid at 60 fps without dropping a single audio buffer.
