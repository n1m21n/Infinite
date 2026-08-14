# Audio phases — what is shipped, what is left

Companion to `README.md` (the plan). This file tracks state; the plan does not.
Update it as part of each node's exit criteria (`new-audio-node` SKILL §6.7).

Last verified against the registry in `src/main.cpp` on 2026-08-13 (§0
infrastructure + Audio Filter + Dynamics + Delay + Reverb sessions).

Design specs: `P3c-P3a2-design.md` covers the 7 effects and the 7 note nodes —
params, DSP references, layouts, and a prompt generator (§5) for executing
sessions. Its §0 (shared infrastructure) must land before any node in either
part.

## Infrastructure

| Phase | State |
|---|---|
| P0 feasibility | done — numbers kept, code discarded |
| P1a engine (`AudioEngine`, `AudioBuffer`, `AudioNode`, `ParamMailbox`, `MeterRing`) | done |
| P1b `DspMath`, `AudioVoice`, `INFINITE_DSPTEST` | done |
| P2 cable plumbing (audio + note cables through every wiring site) | done |
| P2.5 transport (audio callback drives the clock) | done |
| P2.6 audio settings | done |
| P2.7 node UI system (`audio-node-ui-system.md` v3, `BeginAudioBody` et al) | done |
| P2.8 routing (Mixer, Splitter) | done |
| P3c-P3a2-design.md §0.1 `RateDivision`/`BeatsFor`/`BarsToBeats` (`src/audio/MusicTime.h`) | done |
| §0.2 time signature on `Transport` (`SetTimeSignature`/`Bars`/`BeatsPerBar`), surfaced next to BPM | done |
| §0.3 global key/scale on `Transport` + scale table/`SnapToScale`/`DegreeToNote` (`MusicTime.h`) | done |
| §0.4 `AudioEffectNode` + `EffectDef`/`EffectParamDef` table (`src/audio/EffectDefs.h`, `src/nodes/AudioEffectNode.h/.cpp`) + `IEffectKernel` | done |
| §0.5 effect conventions (mix crossfade + denormal guard in `AudioEffectRuntime`; latency passthrough; mailbox smoothing) | done |

Generic since 2026-08-13: `InputCountFor` counts audio/note pins by probing
`AudioInputSlot`/`NoteInputSlot`, so no per-node entry is needed. The only
per-node ladder left is `DrawAudioNodeBody` (and, for `AudioEffectNode`, a
second dispatch on `EffectDef::name` inside that same branch).

## Nodes — 22 of 30-plus-8 shipped (effects list expanded past the original 30, see P3c below)

**Shipped:** Wavetable · Gain · Audio Out · Mixer · Splitter · MIDI Notes ·
Envelope · Audio Filter · Dynamics · Delay · Reverb · Drive · Stereo ·
Pitch Shifter · Chorus · Flanger · Phaser · Bitcrush · Transient Shaper ·
Stutter · Ring Mod · Formant Filter

### P3a Notes — 1 of 7 (+ Envelope)

| Node | State |
|---|---|
| MIDI Notes | shipped (replaced the plan's Note Sequencer as the note source) |
| Note Sequencer | **left** — grid / euclidean / polyrhythm + record-arm |
| Arpeggiator | **left** |
| Note Filter | **left** — scale snap, range gate, chance |
| Note Modify | **left** — pitch/octave/velocity/gate/pan/humanise |
| Note Echo | **left** |
| Note Router | **left** — round-robin / random / probability / chain, 1→4 |
| Note Display | **left** — piano roll / keyboard |

### P3b Synths — 1 of 5

| Node | State |
|---|---|
| Wavetable | shipped |
| Oscillator | **left** — waveform + 2-op FM + unison/detune/glide |
| Drum Sequencer | **left** — per-step sample/vol/decay/pitch/repeat |
| Resonator | **left** — metallic / modal / string / plate |
| Sampler | **left** — classic/slice/repitch/granular/spectral, one session of its own |

### P3c Effects — 15 of 7 (original consolidation exceeded per 2026-08-14 user request)

The user asked to expand past the README §3 7-effect consolidation and add 8
more, specifying the exact list and pointing at a screenshot of the KHS Audio
plugin suite as the params/knobs reference for all of them (the same
reference the minimalism rule already treats as the control-surface bar).
All 15 are `AudioEffectNode` table entries — no new node class, per §0.4.

| Node | State |
|---|---|
| Audio Filter | shipped — `AudioFilterKernel` (`src/audio/dsp/`), RBJ biquads + cascaded `DspMath::TptSvf`, draggable log-frequency response curve |
| Dynamics | shipped — `DynamicsKernel` (`src/audio/dsp/`), Giannoulis/Massberg/Reiss soft-knee gain computer + branching detector, sidechain input (slot 1), transfer-curve visualizer with live operating point + GR bar. Compressor only (threshold/ratio/attack/release/makeup + peak-RMS switch + sidechain switch, 8 params) — cut down from an earlier 4-mode/17-param version to match KHS Audio Compressor's control surface, see `.claude/skills/new-audio-node/SKILL.md`'s minimalism rule |
| Delay | shipped — `DelayKernel` (`src/audio/dsp/`), Hermite-interpolated fractional delay line, one mode plus a Bounce (ping-pong) on/off switch, single-knob tone tilt, tempo sync via `MusicTime`, decaying-tap-bars visualizer with a wet/dry level pair. 9 params — cut down from an earlier 4-mode/39-param version (multiband, multitap, saturation, HP/LP, mod rate/depth, L/R offset all removed, not hidden) to match KHS Audio Delay's control surface (time/tone/feedback/pan/duck/bounce/mix) |
| Reverb | shipped — `ReverbKernel` (`src/audio/dsp/`), 8-line FDN with a Hadamard mixing matrix (Jot & Chaigne), Schroeder allpass diffusion, per-line one-pole damping, RT60-envelope visualizer with a wet/dry level pair. Algorithmic only (no convolution engine) — size/decay/damping/predelay + universal mix, 5 params, one mode. `decay`/`damping`/`predelay` are confirmed AUDIOPARAMSWEEPTEST blind spots (structural: they only affect what an FDN line *writes*, and a line's read trails its write by ~600-1100 samples at the default `size`, longer than the sweep's post-alteration window — see the comment on `EffectDefs.cpp`'s Reverb entry), hand-verified instead via `RunReverbFixture` (RT60 accuracy, predelay landing, damping-shortens-decay, no denormal/NaN spike on a long tail) |
| Drive | shipped — `DriveKernel` (`src/audio/dsp/`), tanh saturator (drive/bias/tone/color/output) with an always-on DC blocker. `color` (2026-08-14) blends tanh toward a harder arctan saturator for a warm-to-aggressive character axis distinct from `tone`'s spectral tilt — see `DriveDsp::RawShape`. One curve family, not the design doc's 6-mode dropdown, per the minimalism rule — see `DriveKernel.h`'s class comment |
| Stereo | shipped — `StereoKernel`, M/S width/pan/bass-mono, polar stereo-field imager visualizer (2026-08-14, apex-centered dB-ring arcs + dB baseline scale + a filled width/pan wedge, matching the reference hardware-plugin goniometer layout — replaces the earlier rotated-45deg vectorscope). `width` is a confirmed AUDIOPARAMSWEEPTEST blind spot (the sweep's rig feeds identical L/R, so side is already zero) — see `EffectDefs.cpp`'s comment |
| Pitch Shifter | shipped — `PitchShiftKernel`, classic two-tap crossfaded delay-line shifter (pitch, grain) |
| Chorus | shipped — `ChorusKernel`, 2-3 tap modulated delay voices (delay/spread/taps/depth/feedback), sync-to-tempo rate (sync/rateDiv, mirrors Delay/Stutter) or free Hz. Knob rows are now 3-over-3 (delay/spread/depth, then feedback/mix/rate) so the rate control (dropdown or knob) sits in the row with the others instead of on its own. `rateDiv` is a confirmed blind spot, same category as Delay's — see `EffectDefs.cpp`'s comment |
| Flanger | shipped — `FlangerKernel`, one modulated delay line with feedback (delay/depth/feedback), same sync-to-tempo rate mode as Chorus. Knob rows are 3-over-2 (delay/depth/feedback, then mix/rate). `rateDiv` confirmed blind spot, same category |
| Phaser | shipped — `PhaserKernel`, cascaded first-order allpass stages (cutoff/depth/order/spread), same sync-to-tempo rate mode as Chorus. Knob rows are 3-over-3 (cutoff/depth/order, then spread/mix/rate). `rateDiv` confirmed blind spot, same category |
| Bitcrush | shipped — `BitcrushKernel`, sample-rate reduction + bit quantization (rate, labelled "downsample" in the UI/bits). Visualizer redrawn (2026-08-14) as a filled, sample-and-held quantized arch (single hump, solid fill to baseline) matching the reference bitcrusher's staircase-fill look, replacing the earlier centered oscillating line |
| Transient Shaper | shipped — `TransientShaperKernel`, dual-envelope difference detector (attack/sustain) |
| Stutter | shipped — `StutterKernel`, tempo-synced beat-repeat/glitch loop (sync/rateDiv/timeMs/chunk). No visualizer (removed 2026-08-14 per explicit request — the card is knobs + sync toggle only now). `rateDiv` is a confirmed blind spot, same category as Delay's — see `EffectDefs.cpp`'s comment |
| Ring Mod | shipped — `RingModKernel`, multiplies by a `DspMath::PolyBlepOsc` (freq/waveform/mix, mix is now a knob alongside freq/wave rather than a separate slider) |
| Formant Filter | shipped — `FormantFilterKernel`, three parallel bandpass resonators morphed A-E-I-O-U (vowel/Q) |

All 15 are green under `AUDIOTEARDOWNSWEEPTEST`. `AUDIOPARAMSWEEPTEST` is
green except the pre-existing Dynamics/Delay/Reverb blind spots, Stereo's
`width`, and Chorus/Flanger/Phaser/Stutter's `rateDiv` above — all
confirmed-by-hand structural, not dropped mailbox pushes (Chorus/Flanger/
Phaser's sync-to-tempo mode, added 2026-08-14, hits the exact same blind
spot Delay/Stutter's own `rateDiv` already does, for the same reason). No
per-node `RunXxxFixture` DSP fixture yet for Drive/Stereo/Pitch Shifter/
Chorus/Flanger/Phaser/Bitcrush/Transient Shaper/Stutter/Ring Mod/Formant
Filter — **left**, same as most of P3b/P3a below.

`AudioEffectNode` (§0.4) is built and proven by Audio Filter's use, and now by
Dynamics reusing the same table for a second, unrelated kernel: one
`EffectDef` table entry declares a param list once (name/range/default),
walked generically for `VisitParams` and the mailbox push; each kernel lives
in its own file under `src/audio/dsp/` behind `IEffectKernel`. What stays
per-effect, deliberately not table-driven (same carve-out the design doc
gives visualizers, for the same heterogeneity reason): the body layout
(`DrawAudioFilterBody`/`DrawDynamicsBody` in `src/main.cpp`) and the
visualizer. Adding effect three is a new `EffectDef` row, a new kernel file,
and a new `Draw*Body`.

Two pieces of shared infrastructure Dynamics needed that Audio Filter didn't,
now generic rather than Dynamics-specific:
- **Sidechain input.** `EffectDef::hasSidechain` makes `AudioEffectNode`
  answer `AudioInputSlot(1)`; `IEffectKernel::ProcessBlock` takes an optional
  `const AudioBuffer* sidechain` (null unless both `hasSidechain` and the pin
  is actually cabled). Every other kernel ignores the parameter.
- **A second kernel-published meter.** `IEffectKernel::ExtraMeter()` (default
  `nullptr`) lets a kernel publish values beyond the runtime's own generic
  post-mix peak - Dynamics publishes `{instantaneous input dB, gain-reduction
  dB}`, read via `AudioEffectNode::ExtraMeterValue(i)` for the transfer
  curve's live dot and GR bar.
- **Visualizer dispatch by id.** `EffectDef::visualizerId`
  (`EffectVisualizerId` enum) replaced the `Def().name == "Audio Filter"`
  string branch in `DrawAudioNodeBody` before a second effect could turn it
  into a growing string ladder.
- **Gated-param prerequisites for the sweep.** `EffectParamDef::prerequisites`
  (other params that must be set first) and `::uiOnly` let
  `AudioParamSweep` (`src/main.cpp`, `INFINITE_AUDIOPARAMSWEEPTEST`) arrange a
  gated param's dependencies before testing it in isolation, instead of
  reporting a real-but-uninformative FAIL - see the P4 note below.

### P3d Utility — 3 of 7 (Gain, Audio Out, Mixer, Splitter shipped = 4)

| Node | State |
|---|---|
| Gain, Audio Out, Mixer, Splitter | shipped |
| Audio In | **left** |
| Scope | **left** — waveform / spectrum / meter; spectrum mode emits a texture (the audio→visual bridge) |
| Recorder | **left** |

### Modulators — 1 of 4 new, 0 of 2 extended

| Node | State |
|---|---|
| Envelope | shipped |
| Shaper | **left** — reuse `DrawCurveEditor` |
| Mod Recorder | **left** |
| Macro | **left** — knobs/bars/XY, count 1–8; deletes the existing Macro Knob + Macro XY |
| Pattern (extend) | **left** — 8 → 16 steps, curve interpolation |
| Audio Analyze (extend) | **left** — add pitch-track and envelope-follower outputs |

### P3e Sample browser — left

Samples mode on the docked node browser: background indexing thread,
filter-as-you-type, disk-persisted index, per-location rescan, drag-to-spawn.

## P4 / P5

- `AUDIOPARAMSWEEPTEST` — done (2026-08-13), extended for gated params in the
  Dynamics session (2026-08-13). Headless, registry-driven (see
  `.claude/skills/audio-node-sweep/SKILL.md`): every param survives save/load
  and reaches the audio thread within one block of its own `CookIfNeeded`.
  Audio Filter's real inter-param-dependency blind spots
  (`bandCount`/`selectedBand`/`band0Gain`/`band0Solo`/`band1-3`, previously
  documented here as expected FAILs) are now resolved rather than merely
  documented: `EffectParamDef::prerequisites` lets the sweep set a gated
  param's dependencies (e.g. `bandCount>=2` + `band1Enabled` before testing
  `band1Freq`) before probing it in isolation, and `EffectParamDef::uiOnly`
  reports a param with no DSP meaning at all (`selectedBand`) as a `pass` with
  a note instead of trying every alternate value and reporting FAIL. **Audio
  Filter is now fully green.**
- **Audio Filter simplified to a single filter (2026-08-13).** The 4-band UI
  (bands dropdown, band strip, `bandCount`/`selectedBand`/`band0-3*` params)
  was cut per UI feedback that it was overcomplicated for what the node needs
  - the `bandCount`/`selectedBand`/`band0Gain`/`band0Solo`/`band1-3` blind
  spots noted above no longer exist because those params don't exist. Audio
  Filter is now `type`/`freq`/`q`/`gain`/`outputGainDb` + the universal `mix`;
  `gain`'s only remaining prerequisite is `type=peak` (shelf/peak are the only
  gain-using types). Still fully green under `AUDIOPARAMSWEEPTEST`.
- `AUDIOTEARDOWNSWEEPTEST` — done (2026-08-13). Registry-driven, runs inside
  the normal app loop: spawn, wire into a running graph, delete mid-playback
  via the real `RemoveNodeByIndex`, keep rendering, cables cleared. Green for
  every shipped node including Audio Filter, Dynamics and Delay.
- **Dynamics and Delay cut down to KHS Audio's reference control surfaces
  (2026-08-13)**, per user feedback that both had grown well past what a real
  plugin needs and `.claude/skills/new-audio-node/SKILL.md`'s new minimalism
  rule (§0.5), which cites KHS Audio's Delay and Compressor by name as the
  bar. **Dynamics**: dropped limit/gate/expand modes (compressor only now),
  knee (fixed internal 6 dB constant), lookahead (delay line removed
  entirely), stereo link (always fully linked via per-sample max across
  channels), auto release, and sidechain HP/audition — 17 params down to 8
  (threshold/ratio/attack/release/makeup/detectorRms/sidechainExternal +
  mix). This *also* resolved the five previously-documented blind spots
  (`knee`/`rmsWindow`/`lookahead`/`stereoLink`/`autoRelease`) by deleting the
  controls, not by gating them - they don't exist to fail any more. One new,
  different blind spot took their place: `sidechainExternal` gates *which
  pin* the detector reads, but `AUDIOPARAMSWEEPTEST`'s generic rig wires the
  identical drive tone to every audio input slot a candidate has (main and
  sidechain alike), so toggling which one is read can never move the
  signature - correct by inspection of the `useSidechain ? sidechain : in`
  ternary in `ProcessBlock`, documented on the param's own `EffectParamDef`
  entry. **Delay**: dropped multiband and multitap modes entirely (`mode`
  replaced by a plain `bounce` bool - ping-pong on/off, not a mode selector),
  separate feedback HP/LP/saturation (collapsed into one bipolar `tone` tilt
  knob), chorus modulation (mod rate/depth), and the ping-pong-only L/R
  offset spread — 39 params down to 9
  (bounce/sync/rateDiv/timeMs/feedback/tone/pan/ducking + mix; `pan` is new,
  balance-law gain on the wet output, replacing what L/R offset used to
  approximate less directly). `sync`/`rateDiv` remain the same confirmed
  blind spots as before (gate the base delay time itself, past the sweep's
  ~70ms warmup window) - `feedbackLp` is gone along with the param.
- Both registered with `run-infinite-hygiene`'s curated check list.
- Per-node DSP fixtures (written alongside each node, not batched at the end)
  — done for Audio Filter, Dynamics, Delay, Reverb (`RunAudioFilterFixture`/
  `RunDynamicsFixture`/`RunDelayFixture`/`RunReverbFixture`, `src/main.cpp`,
  run under `INFINITE_DSPTEST`); left for the rest.
- Hardening: xrun counter in the UI, device/sample-rate change, sleep-wake
  recovery, per-node CPU meter, `ARCHITECTURE.md` audio section — left
