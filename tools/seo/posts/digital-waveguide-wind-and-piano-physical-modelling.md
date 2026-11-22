---
title: Digital waveguide physical modelling for wind and piano
seo_title: Digital Waveguide Wind and Piano DSP | Infinite
description: How Infinite's Wind and Piano nodes model clarinet reeds, brass lips, Chaigne felt hammers, dispersion allpasses, and a 224-mode soundboard in real-time C++ DSP.
date: 2026-11-22
category: product
tags: [physical modelling, digital waveguide, audio dsp, wind synthesis, piano synthesis]
keywords: digital waveguide physical modelling, wind synthesis dsp, piano physical modelling, nonlinear reed bore, stiff string dispersion allpass
image: assets/blog_digital-waveguide-wind-and-piano-physical-modelling.png
image_alt: Technical DSP diagram of Infinite's Wind and Piano physical modelling nodes showing reed, jet, and lip bore waveguides alongside felt hammer, dispersion allpass, and 224-mode soundboard stages
status: published
---

Physical modelling synthesis is having a visible moment across open-source audio engineering. Recent repositories such as `pmlib` for Python and SuperCollider and `mesh2audio` for striking 3D meshes as resonant bodies sit alongside a wave of browser-based WebAudio and WebAssembly synthesisers. Yet acoustic wind bores and struck piano strings remain awkward to implement cleanly in general-purpose block graphs, because their core equations depend on tight, single-sample feedback loops whose phase delay and stability cannot be split across arbitrary buffer boundaries without breaking pitch or blowing up.

In [Infinite](https://infinitedevices.org/), two dedicated note-driven source nodes, **Wind** (`WindNode`) and **Piano** (`PianoNode`), run those single-sample waveguide loops natively on the audio thread alongside the [String](https://infinitedevices.org/blog/physical-modelling-string-synthesis-in-real-time.html) and [Metallic](https://infinitedevices.org/blog/modal-synthesis-and-physical-modelling-of-metallic-percussion.html) physical models. Instead of sampling a recording or stacking static oscillators, each voice solves a closed acoustic loop per sample while keeping parameter updates lock-free through a main-to-audio mailbox.

![Technical DSP diagram of Infinite's Wind and Piano physical modelling nodes showing reed, jet, and lip bore waveguides alongside felt hammer, dispersion allpass, and 224-mode soundboard stages](https://infinitedevices.org/assets/blog_digital-waveguide-wind-and-piano-physical-modelling.png)

## Why single-sample waveguide loops matter for acoustic instruments

In a digital waveguide, a cylindrical tube or tensioned string is represented as a pair of delay lines carrying travelling pressure or velocity waves, terminated by a frequency-dependent loss filter and a nonlinear excitation. Because the excitation reads the reflected wave from the bore or string on the very next sample, inserting a 128-sample AudioWorklet quantum or a graph block delay inside that loop lowers the effective pitch ceiling and distorts the coupling between exciter and resonator.

Infinite avoids that boundary problem by keeping the entire waveguide loop inside each voice's `Tick()` function, while re-solving slowly varying coefficients, such as fractional delay lengths, dispersion allpass poles, and lip resonator tuning, every `kChunk = 32` samples (roughly `0.7 ms` at `48 kHz`). That split keeps per-sample arithmetic lean enough for 8-voice polyphony on `Wind` and 16-voice polyphony on `Piano`, even at `192 kHz` sample rates (`kBufSize = 32768` samples per wind delay line and `kStrBuf = 8192` samples per piano string loop).

## Inside the Wind node: clarinet, sax, flute, and brass

`Wind` models four distinct bore and exciter geometries inside a single 8-voice node (`WindNode::kClarinet`, `kSax`, `kFlute`, and `kBrass`). Every voice maintains two `32768`-sample ring buffers (`bore` and `aux`), a one-pole bell loss filter, and a breath envelope with a `40 ms` tongue-onset overshoot (`kTongueAmount = 0.4`) so note attacks speak like a real articulator rather than a slow fade-in:

- **Clarinet (`loop = 0.5`)**: Uses a cylindrical bore with an inverting bell (`-0.95 * lp`) and Julius Smith's nonlinear reed reflection table (`clamp(0.7 - 0.3 * diff, -1.0, 1.0)`). Because the bell inverts and the reed closes under pressure, the loop sounds at twice its round-trip length and emphasises odd harmonics.
- **Sax (`loop = 1.0`)**: Splits a conical bore into two delay sections on either side of the reed (`kSaxReedPos = 0.4`), with both ends inverting (`Scavone 1997`) so every harmonic, both odd and even, speaks across the register.
- **Flute (`loop = 1.9965`)**: Couples a breath jet delay (`kJetRatio = 0.32` of the bore delay) into a cubic jet-splitting table, followed by a `20 Hz` loop DC blocker so sustained air pressure never drifts the pipe offset.
- **Brass (`loop = 2.0`)**: Replaces the reed table with a two-pole resonant lip valve (`kLipQ = 25.0`, `kLipDcGain = 3.0`) tuned slightly flat of the note (`kLipTune = 0.975`) so the lips pull into lock with the second bore mode.

On every `Retune()` call, `Wind` subtracts the phase delay of the one-pole bell filter (`OnePoleDelay(pole, w)`) from the target period before writing `d0` and `d1`. Without that phase compensation, darkening the `tone` knob would flatten the pitch of high notes. After summing active voices, a `60 Hz` highpass removes low breath rumble before a DC blocker and headroom trim (`kHeadroom = 0.35`).

## Inside the Piano node: felt hammers, stiff strings, and 224 modes

Struck strings behave differently from blown pipes because piano wire is stiff. Its upper partials do not sit at integer multiples $k f_0$; instead, they stretch sharp according to $k f_0 \sqrt{1 + B k^2}$, where the inharmonicity coefficient $B$ climbs roughly `1.2x` per octave up the keyboard (`Inharmonicity(note, stiffness)`).

`Piano` models that physics across 16 voices (`PianoNode::kMaxVoices = 16`), with up to four coupled waveguides per note (`kStrings = 4`: up to three detuned unison strings plus a slower second polarisation):

1. **Chaigne and Askenfelt felt hammer**: At note-on, a virtual felt hammer strikes at one-eighth of the string length (`kStrikePos = 0.125`). Its contact force grows nonlinearly with felt compression (`kHammerK = 8e9`, `kHammerP = 2.5` at A0 rising `0.012` per key, `kHammerMass = 0.005 kg` at C4), and a `1024`-sample force history buffer handles near-end reflections before the hammer rebounds.
2. **Closed-form loss and golden-section dispersion**: A two-point closed-form one-pole loss filter (`LossFilter`) sets the fundamental `SustainT60` (`25 s` at A0, halving every 18 keys) and high-frequency rolloff from the `tone` and `decay` parameters. Four first-order dispersion allpass filters per string (`kDispersion = 4`) are then fitted on the fly via a 24-iteration golden-section search (`FitDispersion`) across partials 2 to 8 so the loop's phase delay matches stiff-string stretching.
3. **Thiran fractional delay and damper behaviour**: A Thiran allpass (`eta`) resolves sub-sample loop lengths so treble notes keep their sustain rather than dulling under linear interpolation. Releasing a key engages the dampers over `release` milliseconds, except from MIDI note `89` (F6) upwards (`kUndamped = 89`), which rings freely like an acoustic grand.
4. **224-mode soundboard (`DenseBody`)**: The string sum feeds a dense modal bank spanning `45 Hz` to `6000 Hz` across `224` modes with a constant loss factor (`eta = 0.02`, so `T60` falls as `1/f`), blended against a `0.65` direct admittance share and `0.7` stereo width.
5. **Electric models**: Switching `model` to `Rhodes` or `Wurlitzer` routes the voice through `ElectricPiano.h` (struck tine or reed, pickup voicing on `tone`, overtone bell on `stiffness`, speaker cabinet on `body`, and `5.6 Hz` vibrato or stereo Suitcase pan on `tremolo`).

## Patching acoustic models into a unified audiovisual graph

Because `Wind` and `Piano` are standard `INode` sources on Infinite's canvas, you can drive them from `Piano Roll`, `Euclidean`, or external MIDI, modulate `breath`, `vibrato`, `hardness`, or `stiffness` from any LFO or audio-rate envelope, and route their outputs into `Spatial Mixer`, `Audio Effect`, or visual nodes like `Audio Texture` and `Displacement` in the same file. Ready-to-run patches and templates live in the [Infinite library](https://infinitedevices.org/library.html).

There are clear limits to keep in mind. Infinite is a young, MIT-licensed open-source project: `Wind` outputs a mono signal across its 8 voices (leaving stereo width to downstream effects or `Spatial Mixer`), and while Infinite hosts VST3 and AU plugins on desktop, AAX is not supported and live hardware audio inputs are refused in headless mode. For sound designers and DSP engineers who want inspectable, zero-allocation physical models rather than black-box sample libraries, the C++ source for both nodes is available to read, patch, and render offline or live.
