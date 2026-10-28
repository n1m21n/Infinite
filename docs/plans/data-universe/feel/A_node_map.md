# A. Node map: every sound node and knob -> 22 axes -> mood V / E / T (factual base, 2026-10-28)

Read-only research. Nothing invented: a cell says MEASURED (from our renders), INFERRED (from node function or FEEL-MAP rule) or UNMEASURED.
V = valence, E = energy arousal, T = tension arousal. Evidence letters are FEEL-MAP.md's: A study, B literature (unverified), C my inference.
Sources read: README, WEB, CHARACTERISTICS, CHAIN-TRACK, INTEGRATION, FEEL-MAP, next-phase, dials, axes-strategy, recipes (all in `docs/plans/data-universe/`),
RESEARCH-LANDSCAPE.md (activity/tuning results), `tools/universe/describe_all.json`, `activity.py`, `tuning.py`, `own_sweeps.py`, `sound_clusters.py`, `response_harness.py`.
Data root `D = ~/Infinite/training-data`.

## 1. The axes and the mood rule they feed

### 1.1 The 22 axes: what is measured, and where

| # | Axis | Measured today? | Where |
|---|---|---|---|
| 1 | loudness | yes, all audio + MIDI velocity | `D/characteristics/*.jsonl` |
| 2 | brightness | yes (audio) | same |
| 3 | sharpness | yes (audio) | same |
| 4 | warmth | yes (audio) | same |
| 5 | weight | yes (audio) | same |
| 6 | fullness | yes (audio) | same |
| 7 | noisiness | yes (audio) | same |
| 8 | roughness | MIDI only (906 phrases); audio NO (needs partials) | `characteristics/midi_sample.jsonl` |
| 9 | attack | yes (one number per note) | same |
| 10 | decay | yes, 58-95% of rows (censored at window end) | same |
| 11 | movement | only Diva v2 mel rows (4,935) and own-synth renders (curve) | `diva_m1.jsonl`, `audio/own_*.jsonl` |
| 12 | width | yes but weak: 1 (5%) on Diva v2, 97% on plug-in renders, sample rows have corr | same |
| 13 | pitch height | MIDI curve; synth rows take their note; samples only if named | `midi_sample.jsonl` |
| 14 | register spread | MIDI only | same |
| 15 | roughness (music side) | MIDI only | same |
| 16 | tonal tension | MIDI only | same |
| 17 | harmonic rhythm | MIDI only | same |
| 18 | density | yes both | same |
| 19 | contour | MIDI only | same |
| 20 | space | NOT measured | needs renders that record the release tail |
| 21 | vowel | NOT measured | needs formant tracker |
| 22 | glide | NOT measured | needs pitch tracker |

The sound forward models, `activity.py` and `tuning.py` use only **11 axes** (`p3_characteristics.COLS` means): loudness, brightness, sharpness, warmth, weight, fullness, noisiness, movement, attack, decay, density.
Roughness (8), width (12), space (20), vowel (21), glide (22) and every music axis are NOT inside any knob->axis model yet.

### 1.2 Axis -> V / E / T (FEEL-MAP rows; direction of the axis going UP; `?` = no source)

| Axis up | V | E | T | Evidence | FEEL-MAP row |
|---|---|---|---|---|---|
| loudness | 0/- | ++ | ++ | A/B | Loudness up |
| brightness | + | ++ | + | A | Bright (centroid up) |
| sharpness | + (as brightness) | + | + | C (Eerola: "sharp" with bright/angry profile) | Angry: sharp; Eerola 2012 |
| warmth (up = more 120-500 Hz) | 0 / - | - | - | C (read from Dull row; warmth is not "dull") | Dull / Tender: soft |
| weight (up = more < 120 Hz) | - | - | 0 | C (Register low) | Register low |
| fullness (spread) | ? | ? | ? | gap (no row; "strong partials" lifts V, A, Table 5) | none |
| noisiness | - | + | + | C (borrowed from Roughness row) | Roughness / dissonance is B |
| roughness | - | + | ++ | B (Plomp-Levelt) | Roughness / dissonance |
| attack: faster | + | + | - | A | Fast attack |
| attack: slower | - | - | + | A | Slow attack |
| decay: longer | 0 | - | - | A | Legato (long decay) |
| decay: shorter | + | + | + | A | Staccato |
| movement (flux / variation) | - | + | + | A (fear profile; Eerola: more spectral variation raises T) | Vibrato irregular |
| width | ? | ? | ? | gap | none |
| space | ? | ? | ? | gap (only "pauses, space" at note level: V-, E-, T+, B/C) | none |
| vowel (formants) | ? | ? | ? | gap | none |
| glide up/down | ? | ? | ? | gap (Sievers step/direction B/C) | Wide pitch steps |
| pitch height | + | + | + | A (3rd cue) | Register high |
| register spread (wider) | + | + | + | B | Pitch range wide |
| tonal tension (up) | - | 0 | ++ | B/C (minor mode + , chord ending) | Mode / chord tables |
| harmonic rhythm (faster) | ? | + | ? | B/C | Chord rate |
| density (onsets/s up) | + small | ++ | + | A | Tempo up |
| contour rising | + | + | + | B/C | Wide steps up |
| (not an axis) mode major | ++ | + | - | A | Table 3 (largest single cue) |

Cue-importance order (Eerola 2013): mode, tempo, register, dynamics, articulation, timbre. 77-89% of rating variance is linear, so a signed weight per axis is the right first model. The weights themselves are NOT fitted yet (FEEL-MAP: "all sources give direction").

## 2. Synths and sources

Counts in describe_all.json: Synths 20, Source with audio out 2 (Video, VMPC), Audio File (Modulators) 1, Audio In 1.
"Measured" = stored one-knob pairs from `D/audio/own_<engine>.jsonl`, regression of the 11-axis change on the knob change (signed, in spreads per full knob range; I re-ran this read-only from `activity.py`'s pair logic). Selector knobs (filterType, wave, algorithm, exciter, material) are level indices: the sign is meaningless, only the size. Context mixed: effect of a knob depends on the others (RESEARCH-LANDSCAPE: Analog detune is dead while voices = 1).

### 2.1 Six engines with sweep data

| Engine | Rows / presets in `own_<e>.jsonl` | Swept knobs of total | Busiest knobs and what they move (MEASURED; sd per range) |
|---|---|---|---|
| Analog | 39,072 / 4,834 | 24 of 33 | filterType 0.58 (warmth +.5, loud -.4); cutoff 0.38 (fullness +1.1, loud +1.1, sharp +.9); attack 0.25 (attack +2.9, density +1.8); resonance 0.21 (loud +.7, full -.5, bright +.3); noise 0.19 (noisy +1.1, bright +.6); release 0.13 (decay +1.5, loud +1.4); sustain 0.13 (movement -.9, loud +.8); pw1 0.13 (density -.5); sub 0.12 (bright -.4); keyTrack (loud -.3). Dead: detune 0.00, spread 0.02 (voices fixed 1), fm 0.05, drive 0.05, oscMix 0.07 |
| Oscillator | 36,680 / 4,568 | 13 of 22 | filterType 0.55; cutoff 0.46 (bright +1.7, weight -1.2, loud +1.2); sync 0.36 (warmth -.8, bright +.8, full -.7); unison 0.22 (density +.9, decay -.8, movement +.7); waveform 0.21 (full +1.0, noisy +.8); resonance 0.19 (loud +.6); ampAttack 0.18 (attack +2.2, decay -1.0); detune 0.17; ampRelease 0.13 (decay +1.3, loud +1.1); ampSustain 0.12 (loud +.8). fmDepth/fmMode fixed 0 in sweep |
| FM | 16,000 / 1,937 | 30 of 35 | algorithm 0.69 (movement -2.1, sharp -1.5, bright -1.3); fixed1 0.42 (weight +2.2, bright -1.7); fixed2 0.37; attack1 0.25 (attack +1.8, decay -1.2); release1 0.18 (loud +1.7, decay +1.6); level3 0.16 (sharp +1.0, bright +.8, noisy +.8); level4 0.15 (movement +.6, noisy +.5); ratio1..4 0.09-0.12 (bright/sharp +.3 to +.5; ratio2 warmth -.85) |
| Metallic | 16,000 / 1,995 | 9 of 15 | filterCutoff 0.60 (sharp +2.0, warmth +1.7, full +1.7); decay 0.33 (decay +2.7, loud +2.4, movement -1.4); material 0.28 (full -.7, bright -.5); transient 0.17 (full +1.0, sharp +.9); filterResonance 0.16 (attack +.7); stiffness 0.12; drive 0.07 (noisy +.4); width 0.01 (dead) |
| String | 18,360 / 2,266 | 7 of 8 | exciter 1.06 (selector pluck/bow/strike: bright -2.2, full -2.0, warmth +1.6); position 0.38 (weight +.8, full +.5); stiffness 0.29 (bright -.7, warmth +.6, noisy -.6); decay 0.22 (decay +1.2, loud +1.1); body 0.22 (full -1.3, weight -1.1, warmth +.9); release 0.19; tone 0.13 (bright +.6, sharp +.6, full +.6) |
| Wavetable | 16,000 / 1,967 | 65 of 73 | only 6-19 one-knob pairs per knob (thin): a.on/b.on, a.cutoff (decay +1.6, attack -1.4, bright +1.4), b.pitchAmount (attack +1.5), a.filterAmount (density -1.0, bright +.5), mix (density +1.3). Treat as UNRELIABLE; direction signs not stable |

Model view (RESEARCH-LANDSCAPE "Tuning curves v1", Analog, forward-model sweep 0->1, in spreads): cutoff -> brightness up 1.86; attack -> attack up 2.39; release -> decay up 1.79; noise -> noisiness up 0.51; resonance -> loudness up 0.64; sustain -> loudness up 0.59; pw1 nearly inert 0.09. Model vs measured activity rank correlation: Analog 0.68, Oscillator 0.84. **C2 (direction of knob moves) is MISS**: direction right 63.5% Analog, 61.8% Oscillator (bar 90%); on moves > 0.5 sd 75-85%. Per-axis direction on clear moves: sharpness 88-97%, decay 85-87%, attack 64-95%, loudness 69-83%; weak: fullness 62-69%, noisiness 74-84%, movement 71%.

Mood effect of the busiest synth knobs (INFERRED from the axis table 1.2 applied to the MEASURED axis moves):

| Knob raised | Measured axes | V | E | T | Note |
|---|---|---|---|---|---|
| filter cutoff (Analog, Osc, Metallic) | bright / sharp / loud up | + | ++ | + | A rule x measured size |
| resonance | loud up, fullness down | - | + | + | resonance = narrow peak; "roughness" not measured |
| attack time up | attack axis up (slower) | - | - | + | A (slow attack) |
| release / decay time up | decay up, loud up | 0 | - | - | A (legato) |
| noise / noise knob up | noisiness up | - | + | + | C |
| unison / detune up | density, movement up | - | + | + | C; detune dead at voices=1 |
| sub / weight up | brightness down, weight up | - | - | 0 | C |
| FM level3/4, ratio up | sharp, noisy, bright up | + / - mixed | + | + | C; FM roughness unmeasured |
| String exciter pluck -> bow | bright down, warmth up | - | - | - | C; selector |
| String / Metallic decay up | decay up | 0 | - | - | A |

### 2.2 Synth / source nodes with NO knob->axis data

| Node | Knobs (swept ranges from describe_all) | Status | Axes it plausibly moves (INFERRED) |
|---|---|---|---|
| Partials | pitch -24..24, formant -12..12, partials 1..48, morph 0..1, speed 0.1..4, noise 0..2, release 1..5000, loop | UNMEASURED | brightness, fullness, noisiness, vowel (formant), decay |
| Wave Terrain | 27 numeric: orbitType, channel, centerX/Y, radiusX/Y, ratioA/B, rotation, scanSpeed, position, unison, detune, filter, drive, amp ADSR | response harness: ALL renders silent (147/147, needs an image input pin) | needs image; unmeasurable headless as built |
| Equation Synth | preset, domainMode, knobA-D 0..1, unison, detune, filter, drive, ampADSR | UNMEASURED | brightness, noisiness, roughness (Chebyshev presets) |
| Spectral Synth | scanMode, scanSpeed 0.05..10, minFreq/maxFreq, partialsChoice, contrast, brightness, threshold, unison, filter | harness: 16 of 27 knobs, 3 stimuli (C2/C3/C4) | see harness table 2.3 |
| Sampler | pitch -24..24, finetune, speed -2..2, start/end/position, decay 0.05..10, fadeIn/Out, loop, reverse | UNMEASURED (content-dependent) | pitch height, decay, attack, brightness (pitch/speed) |
| Slicer | sliceBy, onsets 1..64, division, sensitivity, pitch, attack 0..500, decay 5..5000, speed | UNMEASURED | density, attack, decay |
| PaulStretch | stretch 1..1000, window_size, phase_rand, pitch_shift, freq_shift, unison, detune | UNMEASURED | movement down, decay up, noisiness, space (ambient wash) |
| Molder / Grain Molder | chaos, tone, air, snap, stretch, grain 10..500, amount, pitch | UNMEASURED | noisiness, brightness, movement |
| Granular | position, scan, grainLength 5..1000, density 1..100, randomPos/Length/Pitch/Pan, reverseProb, pitchShift, window, width | UNMEASURED | density, movement, noisiness, width |
| Field Synth | detune 1..1.03, cutoff 200..10000, res 0..0.92, sub, warmth, attack 0.005..0.5, maxVoices | UNMEASURED (user-code node) | brightness, warmth, attack |
| Drum Sequencer | 380 rows (steps, lanes, velocities) | UNMEASURED as knobs; grooves have MIDI-side data (section 6) | density, loudness, attack |
| MPC / VMPC | 177 / 149 rows | UNMEASURED | pad sample player |
| Looper | take, thru, finetune, pitch, speed, volume, fades | UNMEASURED | density/decay (loop) |
| Oscillator via harness | frequency 0->1: brightness +7.0 oct, weight -0.93 (pitch artifact); pitchBend: weight -0.68; glide inert | MEASURED but only 3 of 20 knobs | pitch height (frequency) |

### 2.3 Response harness for synth nodes beyond the six (`D/audio/response_native_synths.jsonl`, 324 rows)
Spectral Synth, 3 stimuli, 0->1 sweep, delta vs default:

| Knob | brightness | loudness | other | Reads as |
|---|---|---|---|---|
| maxFreq | +3.2 oct | 0 | weight -0.43 | brighter -> V+, E+ (rule A) |
| minFreq | +2.8 oct | 0 | weight -0.57 | brighter, thinner |
| scanSpeed | +2.7 oct | 0 | width -0.45 | brighter, more movement |
| scanMode | -1.9 oct | -2 dB | width -1.05 | darker, narrower |
| contrast | +0.3 oct | -28 dB | noisiness +0.11 | quieter |
| threshold | +0.3 oct | -17 dB | width +0.16 | quieter |
| brightness | 0 | +6 dB | - | loudness only |
| colorMode, glide, fine, rootFreq, freqScale | ~0 | ~0 | - | inert in this test |

## 3. Audio effects (30 types in AudioEffects + 5 in Utility)

Columns: knobs from describe_all (non-mix; `mix` 0..1 exists on nearly all). MEASURED = `D/audio/response_native.jsonl` (1,179 rows): `response_harness.py` renders Audio File -> node -> out with 3 stimuli (drum loop, pink burst, sine sweep), mix/wet pinned to 1, one knob alone 0 -> 1 (fractions 0, 0.5, 1) vs defaults. Numbers = mean change over stimuli, 0 -> 1: br = brightness in octaves of centroid, ld = loudness dB (rms), nz = noisiness (flatness), wt = weight (share < 120 Hz), wd = width (1 - L/R correlation).
Caveats of the harness: single knob from default, one baseline, no repeats or noise floor; stimulus mono so width moves only for nodes that create stereo; selector knobs (type, sync, shape, model) were not swept; "0" means no change in those 8 features, not proven inert (EQ band freq/Q are 0 because band gains default to 0 dB; Delay timeMs 0 because the features don't see delay time). Loudness deltas on gain-like knobs (output, makeup, inGain) are pure gain, not timbre.

VET column = INFERRED: the axis table 1.2 applied to the measured br / ld / nz / wt / wd moves (+ for raising the knob). Space (axis 20) and roughness (8) are not measured, so tails and modulation depth get "?".

### 3.1 Tone and dynamics

| Node | Knobs [range] | MEASURED (0 -> 1) | Axes also plausible (INFERRED) | V / E / T raising |
|---|---|---|---|---|
| Audio Filter | type, freq 20..20000, q 0.1..18, gain -24..24, envAmount -1..1, rate 0.02..5, drive, sync | freq: br +7.6, ld +24, nz +0.33, wt -0.56; q: br +2.7, ld +5, wt -0.53; envAmount: br +0.75; gain/rate: 0 | movement (env), roughness (high q) | freq: V+ E++ T+ ; q: V0 E+ T+ |
| EQ | 5 bands x (type, freq, Q 0.1..18, gain -24..24, on), outputGainDb | band1Gain: br -4.7, wt +0.76, ld +14; band2Gain: br -2.9; band3Gain: br -1.0, wt -0.42; band4Gain: br +1.6, wt -0.58; band5Gain: br +4.3, nz +0.35; freq/Q: 0 | warmth (low-mid bands), sharpness | low bands up: V- E- T0 ; high bands up: V+ E+ T+ |
| Dynamics | threshold -60..0, ratio 1..20, attack 0.05..200, release 5..2000, makeup -12..24, detectorRms, sidechainExternal, scHpf, style | makeup: ld +28.7 (gain); threshold: ld +25, br -0.32; attack/ratio/release: 0 | decay (pump), attack (transient squash), density | threshold lower: E-, T- (levels flatten); no feel row for compression |
| Limiter | threshold -30..0, release 5..1000, inGain -12..24, outGain -24..12 | inGain: ld +22; threshold: ld +19, br +0.29; outGain: ld +36 | loudness only | gain: E+ T+ (loudness rule) |
| Transient Shaper | attack -24..24, sustain -24..24 | attack: ld +16.6 (dB gain on transient); sustain: ld +22, br +0.44, nz +0.21 | attack, decay | attack up: V+ E+ T- (fast attack, A); sustain up: E- T- (legato, A) |
| Formant Filter | vowel 0..4, q 2..30 | vowel: br -0.69, wt +0.13; q: ld -3 | vowel (axis 21, UNMEASURED) | ? (no rule) |
| Key-Snap | snap 0..1, glide 0..500, scale, root, globalKey | glide/snap: ~0 in these features | pitch height / tonal tension (retunes to scale) | tonal tension down when snapped to key: T- (B/C) |
| Spectrum Slide | slide 0..1 | 0 (inert in harness: 1 of 1) | movement | ? |

### 3.2 Distortion and colour

| Node | Knobs [range] | MEASURED | Axes also plausible | V / E / T raising |
|---|---|---|---|---|
| Drive | drive 0..40, bias -1..1, tone -1..1, color 0..1, output -24..12 | drive: ld +17.5, nz +0.18; tone: br +3.2, wt -0.51, nz +0.20; bias/color ~0 | roughness, sharpness | drive: V- E+ T+ (noisy/rough C/B); tone: V+ E++ T+ |
| Saturator | model1-3, amount1-3 0..1, routing, feedback 0..0.95, output -24..12 | amount1: ld +17, nz +0.14; amount2/3/feedback: 0 (models not active in default routing); output: gain | roughness | V- E+ T+ (C) |
| Bitcrush | rate 200..44100, bits 1..16, dither, analog | bits: br -1.4, ld +12, wt +0.57; rate: br +3.7, nz +0.43, wt -0.40 | noisiness, roughness | lower rate/bits: V- E0 T+ (noisy, C) |
| Wavetable Shaper | table, position 0..1, drive 0..24, bias, smooth, output, stereo | drive: br +1.1, nz +0.26; position: br +0.37, nz +0.16; output: gain; smooth/bias: 0 | roughness | V- E+ T+ (C) |
| Cycle Shaper | waveform, threshold -60..-12, smooth 0..32 | threshold: br -0.13, nz +0.08; smooth 0 | noisiness | ? |
| Ring Mod | freq 1..5000, waveform | freq: br +1.1, wt -0.42 | roughness (inharmonic sidebands), noisiness | V- E+ T++ (roughness B) |
| Frequency Shifter | shift -100..100, feedback 0..0.95, spread 0..100, range | feedback: ld +5, wt -0.38; shift: ~0; spread: wd +1.0 | roughness, width | V- E+ T+ (C) |
| Pitch Shifter | pitch -24..24, grain 10..250, analog, spectral | pitch: br +2.4, nz +0.75, ld -1.8 | pitch height (register), glide | pitch up: V+ E+ T+ (register A) |
| Field Effect | p.drive 1..10, p.tone 400..12000, p.bias, p.mix | drive: br +2.0, ld +9.6; tone: br +4.6, nz +0.56; mix: br -3.1 | user code | as Drive |

### 3.3 Space, modulation, time

| Node | Knobs [range] | MEASURED | Axes also plausible | V / E / T raising |
|---|---|---|---|---|
| Reverb | size 0..1, decay 0.1..20, damping 0..1, predelay 0..500, width 0..1, analog | decay: br -2.9, ld +17, wt +0.30; size: br +2.2, ld -10.5, nz +0.12; damping: br -1.3, wt +0.22; width: wd +0.08; predelay: 0 | **space (20, UNMEASURED)**, decay | decay up: darker (V- E- T- by brightness) + more space (no rule) |
| Convolution Reverb | size, shape, material 0..1, distance 0..1, decay 0.1..10, spread 0..1 | decay: br -2.3, nz -0.16, wd +0.91; distance: br -1.05, wd +0.52; size: br +1.0, wd -0.45; spread: wd +0.76; material: br +0.5 | space, width | as Reverb; width has no rule |
| Delay | timeMs 1..2000, feedback 0..110, tone -1..1, pan, ducking 0..1, wow, flutter, saturation, diffusion, sync, rateDiv | feedback: br -1.25, ld +16, nz +0.18, wt +0.41; ducking: ld -10.8, br +0.74; tone: br +0.58; timeMs, pan, wow, flutter, saturation, diffusion: 0 (features blind or mono) | space, density (echoes: onsets +4.7), decay | feedback: density up (E+), darker; space no rule |
| Chorus | delay 1..30, depth 0..15, spread 0..1, taps, feedback 0..0.9, rate 0.02..5, sync | depth: wd +0.85, ld -3; delay: wd +0.56; feedback: br -1.2, wd -0.34; rate: ~0; spread: 0 | width (11 movement/roughness not measured) | width has no rule; movement up: V- E+ T+ (A, weak) |
| Flanger | delay 0.2..15, depth 0..10, feedback -0.95..0.95, rate, spread 0..1 | spread: wd +1.0; depth: wd +0.78, br -0.89; feedback: br -1.9; delay: br +0.68, wd +0.33; rate: br -0.57 | movement, roughness (comb) | movement up: V- E+ T+ ; feedback: roughness up T+ (B) |
| Phaser | cutoff 100..4000, depth 0..1, order 2..8, spread, rate | cutoff: br -1.8, wt +0.40; order: wd +0.31, ld -2.5; spread: wd +0.31; depth/rate: small | movement | movement up: as Flanger |
| Tremolo | depth 0..1, shape, stereoPhase 0..180, rate 0.05..20, sync | depth: ld -4.6; stereoPhase: wd +0.16; rate: ~0 | movement (amplitude), decay | tremolo depth up: movement/density up: V- E+ T+ (A fear profile, weak) |
| Stutter | steps 2..16, timeMs, gateMask, sync, rateDiv | steps: onsets -3.3, ld +1.8 | density (strong), attack | steps up: density up: V+ E++ T+ (A tempo) |
| Stereo | width 0..2, pan -1..1, bassMono 0..500 | bassMono: br +0.2; width/pan: 0 (mono stimulus) | width (12) | width has no rule |
| Spec Blur | blurTime 10..5000, tilt -1..1, diffusion 0..1, freeze | tilt: br -5.1, wt +0.69; diffusion: wd +0.92; blurTime: ld -18, nz +0.24 | space, movement down, decay | tilt up: darker V- E- T- |
| Resonator Bank | rootFreq 20..2000, structure, poles 1..16, decay 0.05..10, scatter, spread, damp | rootFreq: br +6.2, ld -21, wt -0.98; poles: br +1.6, wt -0.46; decay: br -0.53; spread: wd +0.59 | roughness, pitch height, decay | rootFreq up: V+ E+ T+ (register); spread: width |
| Shape Resonator | tune 20..2000, decay 0.02..20, damping, pos, modes 1..32 (+ geometry in) | tune: br +6.5, ld -15, wt -0.98; modes: br +1.6, wt -0.47; decay: br +0.76 | pitch height, decay | tune up: register up |

### 3.4 Utility audio nodes

| Node | Knobs | Status |
|---|---|---|
| Gain | 1 | UNMEASURED; loudness only (axis 1) |
| Mixer | 49 rows (levels, pans, sends) | UNMEASURED; mix rules need Ableton sets (G8) |
| Blend Audio | 1 | UNMEASURED |
| Splitter | 4 | UNMEASURED |
| Audio Meter, Audio In, Audio Out, Spatial Mixer (106 rows), Plugin (third-party, 0 knob map) | | UNMEASURED / not a node model |

Harness coverage: 30 effect types, 156 of 194 numeric knobs with a numeric range swept (the other rows are enum/int selectors with no range in `describe_all`). Not covered at all: Plugin, Shape Resonator second input, all two-input behaviours (sidechain, Spectrum Slide partner), all selector knobs, any knob PAIR (interaction).

## 4. Note / MIDI effects and note sources (24 Notes + 4 Prediction note nodes)

No render or knob->axis data exists for any of them (they act on the music axes 13-19, which are measured on MIDI files, not on node outputs). Mapping below is INFERRED from what the node does; VET follows 1.2.

| Node | Knobs [range] | Music axis it moves (INFERRED) | V / E / T raising | Data about the same thing (MEASURED elsewhere) |
|---|---|---|---|---|
| Note Transpose | semitones -48..48 | pitch height (13) | V+ E+ T+ (register A) | register stats in melody_contour rows |
| Pitch Bend | bendSemitones -2..2 | glide (22), pitch height | ? | `note_expression` bend lists |
| Velocity Curve | curve 0.25..4 | loudness (1, velocity) | curve < 1 louder: E+ T+ | velocity stats |
| Humanizer | timingMs 0..100, velocityPct 0..100 | timing offset (feel), loudness variance | small: V+ (looser); large: V-? | drum `off`/`vel` lanes, feel bands (feel_analysis.py) |
| Quantizer | div (Off, 4 bars ... 1/16 ...) | timing offset to grid; density quantised | strict grid: V- (stiff, B/C) | micro-timing bands |
| Gate | holdMs 0..3000 | decay / articulation (staccato vs legato, 10) | short: V+ E+ T+ ; long: E- T- (A) | none for notes |
| Glide | glideMs 0..2000 | glide (22), legato | legato: E- T- | none |
| Note Echo | rateBeats/Seconds, repeats 1..8, decay 0..100, transposePerRepeat -12..12, muteDry | density (18), space-like | density up: V+ E++ T+ | none |
| Arpeggiator | mode (Up/Down/...), octaves 1..4, rate, gatePercent 1..100, stepGates, stepTiming | density, contour (19), register spread (14), articulation | rate up E++ ; octaves up spread V+ E+ T+ ; gate short staccato | Omnisphere arps: 11,029 step sequences in `steam` rows |
| Chorder | scale, root, chordSize 2..6, strumMs, humanize, upperHarmonics 0..100, mode/progSteps/progDeg0-7/progType0-7 | tonal tension (16), roughness (15), harmonic rhythm (17), register spread (14); major vs minor (mode cue) | scale major: V++ E+ T- ; minor: V-- E- T+ ; upperHarmonics up: roughness up T+ | 11,700 chord rows (store/chords.jsonl); 861k chord_prog rows |
| Note Stack | 8 slots semi -24..24 + enabled | register spread, roughness (interval choice), chord quality | stacking m2/tritone: roughness up V- T++ ; octaves: spread | none |
| Note Filter | scale, root, rangeLow/High 0..127, chance 0..100 | tonal tension (out-of-key removed), density (chance), pitch range | in-scale: T- ; chance down: density down E- | none |
| Note Router / Switcher / Merge | mode, probability, rate | structure only | ? | none |
| Random Note Generator | rangeLow/High, scale, root, rate, maxStep 1..12, groove 0..1, swingMode | density, register, contour step size (Sievers), groove/swing | maxStep up: wide steps V+ E+ T+ ; groove up: medium syncopation bump (Witek B, disputed) | groove rows (swing, `groove`) |
| Note Sequencer | 55 rows (steps) | density, pitch, pattern | via step data | bass_seq (486k) and drum_groove (403k) rows |
| MIDI File / MIDI Notes / Keyboard | transpose, velocity 0..2, speed 0.25..4, loop | pitch height, loudness, tempo/density (speed) | speed up: V+ E++ T+ (tempo A) | the whole MIDI corpus |
| Note Capturer, Note Strum (strumMs 0..100), Bouncing Balls | | density, strum articulation, random density | | none |
| Field Notes | root, scale, p.hits 1..16 | density, key | | none |
| Predictive Notes | stray 0..1, memory 0..8, lengthSpread, velocitySpread, rangeLow/High | surprise, velocity variance, register | stray up: tonal tension / roughness up (surprise raises T, B Huron) | learned from keeps/undos (taste data, not on disk here) |
| Predictive Quantize / Velocity / Rhythm | mix 0..1, stray, memory | timing, velocity, rhythm surprise | | |

Global tempo / mode (the two biggest cues) are NOT node knobs of the sound chain: BPM sits on the transport and mode/scale sits on Note Filter / Chorder / global scale (`useGlobalScale`).

## 5. Modulators, macros, prediction (sound relevant)

Modulators (33): LFO, Random, Pattern, Math, Compare, Range to Range, Smoothing, Invert, Mod Depth, Mod Curve, MSEG, Voice Spread, Note Expression, CV to Pitch, MIDI CC, MIDI Trigger, Path, Geometry Table, Constant, Null Modulator, Image Analyze, Hand/Face/Pose Track, Palette, Audio File, Audio Analyze, Vibrato, Envelope, Note to CV, Velocity to CV, CV Recorder, Audio to CV. Macros (9): Knob, Slider, Bipolar Knob, XY, Toggle, Trigger, NumBox, Radio Selector, Step Gate. Prediction (non-note): Drift (speed 0.1..4, stray, momentum, smoothness 0..0.95, depth 0..1), Moves, Predictive Modulator, Predictive Coloring.

| Item | Axis effect | Status |
|---|---|---|
| A modulator moves a target knob over time | movement (11) up, plus the target knob's own axes along the cable | NO measurement. CHAIN-TRACK L3 (3,000 renders) not run |
| LFO / Vibrato / Envelope / MSEG / Random depth and rate | movement; Vibrato irregular: A row (V- E+ T+) | UNMEASURED; real LFO shape+rate usage exists in 44 patch rows (LFO) and 56 (Random) in `store/patch_pieces.jsonl` |
| Drift / Predictive Modulator depth | slow movement | UNMEASURED |

## 6. Chain-level data that exists (what a "mood of a chain" model could use)

| Data | Path | Size | What it holds |
|---|---|---|---|
| Single-node responses | `D/audio/response_native.jsonl`, `response_native_synths.jsonl` | 1,179 + 324 rows | knob 0/0.5/1 vs default, 8 harness features; NOT the 22-axis rows (no roughness, no curves) |
| Synth sweeps | `D/audio/own_{analog,oscillator,fm,metallic,string,wavetable}.jsonl` | 39,072 / 36,680 / 16,000 / 16,000 / 18,360 / 16,000 rows | full recipes + axes, 8 note windows each |
| Human synth presets rendered | `own_*_human.jsonl`: analog 128, oscillator 296, metallic 96, wavetable 56 rows | | taste anchors from `~/Downloads/Patches` |
| Character x node matrix | `D/atlas/character_matrix.json` | 26,915 presets (Logic strips + Guitar Rig) | log2 lift of each Infinite effect for 11 words. Top lifts n >= 30: spacious Chorus +2.5, Convolution Reverb +2.4, Pitch Shifter +2.2, Reverb +1.7; dirty Pitch Shifter +3.2, Drive +2.6; lofi Saturator +1.4, Bitcrush +0.7; glitchy Pitch Shifter +3.6, Audio Filter +2.9, Ring Mod +1.9; moving Audio Filter +3.2, Flanger +2.9, Phaser +2.1; clean Reverb +1.9; bright Chorus +1.9; warm/dark/aggressive: no strong lift (lift ~0 for every node, weak) |
| Words counts | same file `characters` | spacious 2,466; lofi 1,783; warm 1,476; glitchy 1,393; moving 1,299; dark 1,187; dirty 1,035; aggressive 972; clean 833; phat 449; bright 239 | tags from names/folders |
| Chains | `D/presets/air_logic_strips.jsonl` (25,486 strips, order of plug-ins), `arcs/als.jsonl` + `arcs/air_als.jsonl` | | slot order and device chains; NOT mapped to mood |
| Chain renders (CHAIN-TRACK) | none | 0 | pairs 2,400 / triples 4,000 / moves 600 planned, NOT rendered |

## 7. Mood and emotion data on disk

| Dataset | Path | Rows | Labels | Notes |
|---|---|---|---|---|
| EMOPIA (pop piano, 4 quadrants) | `~/Infinite/datasets/midi/data/emopia_piano_emotions/EMOPIA_1.0/` (`label.csv` 1,078 rows, `midis/`, 1,090 .mid) | 1,078 clips | 4Q: Q1 V+A+, Q2 V-A+, Q3 V-A-, Q4 V+A-; annotator col | features + arcs in `D/eval/affect_midi.jsonl` (253 V-A-, 265 V-A+, 310 V+A-, 250 V+A+). Also `emotion` field in `D/store/midi_full.jsonl` from file name Q1-Q4 |
| VGMIDI (game music) | `~/Infinite/datasets/midi/data/vgmidi_game_tracks/vgmidi_labelled.csv` (204 data rows) + `vgmidi_unlabelled.csv` (3,850) | 204 labelled | valence, arousal in {-1,+1} | `affect_midi.jsonl`: 27 V-A-, 39 V-A+, 62 V+A-, 76 V+A+. Arousal AUC for B10 only 0.549 |
| Combined affect eval rows | `D/eval/affect_midi.jsonl` | 1,282 | set, id, valence, arousal, features{mode, bpm, notes_per_s, vel, vel_sd, reg, span, poly, minor_chords, seconds} + 2-s arc | already a V/A (not T) table; E vs T split not available |
| Mood-named chord files | `D/store/chords.jsonl` (source: `datasets/midi/data/chord_progressions`, `modal_chord_progressions`, `chords_and_scales`) | 11,700 chord_prog rows from 965 mood-named files (`mood` list on each row) | mode Major 3,000 / Minor 3,600 / Modal 5,100; 23 mood words: Mysterious 3,600, Nostalgic 3,540, Hopeful 3,240, Triumphant 2,040, Romantic 1,920, Joyful 1,020, Rebellious 1,080, Dark 900, Surprised 900, Sad 780, Tender 600, Playful 420, Peaceful 360, Spiritual 360, Relaxed 300, Empowered 240, Lonely 180, Excited 120, Fearful 120, Dramatic 60, Anguished 60 (counts are word hits across rows, 12 rows per file) | owner-cleared; FEEL-MAP says use these to measure diminished/aug/sus/7th/9th chords vs mood: not done |
| Formula mood per song | `D/mood/song_mood.jsonl` | 203,115 songs | valence 199,979, energy 198,766, brightness 181,593, contrast 134,558, danceability 95,169 (raw + `_pct`) | source split: inbox 201,594, chords 965, groove 486, egmd 66, midi 4. **A formula, not ground truth**: vs declared genre pillars r = 0.14 / -0.01 / 0.32 / -0.43 / -0.01 (bar 0.5 missed). Valence formula: major-chord share .55, major bass .15, tempo .15, upward steps .15. Energy: note rate .35, velocity .30, drum density .20, tempo .15 |
| Link rows per song | `D/link/songs.jsonl` | 127,640 songs | family, `act` keys, `mood` (valence, energy, danceability) | same formula mood joined to song activity |
| Genre pillars (declared) | `D/atlas/genres.json` | 69 genres; pillars (danceability, energy, valence, brightness, contrast) declared on 34 catalog cards ("hand-written, not measured"); the other 35 are inferred/family clusters with measured counts | | `genre_regions.json`: 11-axis audio mean/std per genre; label-from-name top1 0.107 top3 0.274 (weak) |
| Instrument mood tags | `D/atlas/instruments.jsonl` | 14,161 rows; `moods` on 1,136, `timbre` on 3,118 (Omnisphere tags) | | text mood words on sounds; usable as audio->mood word link |
| Preset tags | `D/presets/air_plain.jsonl` (NKS 5,979 types/characters, Serum 2 tags, etc.), `D/presets/presets.jsonl` | | `tags` dict (genre, character, complexity) | word source for C4, not V/E/T |
| Album profile | `D/albums/n1m21n_tracks.jsonl` | 162 owner tracks | none (audio profile only) | personal centre; no mood labels |
| Owner blind listening | `D/eval/listen/c3_answers_owner_2026-10-28.json` | 40 pairs | critic agree/disagree (24/40) | not mood |
| Audio mood ground truth (DEAM, MTG-Jamendo, etc.) | not downloaded | 0 | | needs owner's yes (axes-strategy.md 3) |

## 8. MIDI features already extracted

Store: `D/store/inbox.jsonl` (3,160,654 rows: chord_prog 861,277; melody_contour 705,139; note_expression 705,139; bass_seq 486,338; drum_groove 402,761), built by `midi_ingest.py` from 236,386 source files (README: 2,600 rows from drive, plus corpora under `D/inbox/*` symlinks).

| Feature | Where | Count / format | Notes |
|---|---|---|---|
| Pitch (melody) | `inbox.jsonl` type melody_contour: `pitches`, `steps`, `ioi`, `rate`, role | 705,139 16-note windows | |
| Velocity | type note_expression: `vel` list, plus bend/pressure/slide lists; drum `vel` lanes | 705,139 windows | per-note velocity curve |
| Timing offset / micro-timing | drum_groove `off` lane ('a'..'q' = -8..+8 sixteenth-steps, 'i' = on grid) | with `vel` on rows from ingest v2 | `feel_analysis.py` bands: vel_sd, accent, hat_alt, backbeat, swing, layback, jitter, drag |
| Grooves | drum_groove `lanes` x 8 (kick snare hat open_hat clap tom_lo tom_hi bell) x 16 steps, `swing`, `groove`, `bpm`; `D/store/public_drums.jsonl` 3,899; `D/grooves/air_agr.jsonl` 219 Ableton templates (per-note time, velocity, quantise/timing/random/velocity amounts); `D/store/pieces.jsonl` 67 style cards | | Groove MIDI top-1 genre 0.184 |
| Quantization | `.agr` quantise amounts; B3 critic `sloppy_timing` (83% caught), `critic.py corrupt()` | | |
| Drum | drum_groove (402,761 bars; 103 rows in `mix.py` 8-64 latent styles) | | B4 AUC 0.632 PASS, B5 entropy 0.672 FAIL (9/10 gates) |
| Bass | bass_seq: `steps` (16-step degrees), `root`, `mode`, `bpm` | 486,338 | |
| Chords | chord_prog: `numerals`, `qual` (quality), `inv`, `mode`, `key`, `chords` {deg, acc, minor, ext}, `mood` | 861,277 (11,700 from the mood-named files) | qualities in qual: min, sus2, m7 ... |
| Whole-file MIDI facts | `D/store/midi_full.jsonl` | 294,571 files: channels, GM families, CCs per controller (events, distinct, rate, movement), bend, program changes, tempo changes, meters, key signatures, track names, EMOPIA emotion | |
| Arcs over time | `D/arcs/detail.jsonl.gz` (305 MB), `arcs/als.jsonl`, `air_als.jsonl`, `arc_analysis.py` | per 2 s / per bar: note rate, drum hits, velocity, register, parts; sections; tempo changes; CC motion shapes; mix set-once values | |
| Music axes 13-19 | `D/characteristics/midi_sample.jsonl` | 906 phrases only (50 per dataset x 21) | full corpus about 2.5 h, "only if P4 needs it" |
| Per-song mood | `mood/song_mood.jsonl` (section 7) | 203,115 | |
| Expression pieces / patch pieces | `store/expression_pieces.jsonl` 1,271 (glide, bend params from owner patches), `store/patch_pieces.jsonl` 892 (node param values from owner patches: Oscillator 87, Reverb 85, Random 56, Sampler 51, Analog 47, Delay 46, LFO 44 ...) | | human knob settings: usage (u_k) source |

## 9. Gaps, ranked by importance

Importance = how much the feel model is blocked.

| # | Gap | Why it blocks | What closes it (cheapest first) |
|---|---|---|---|
| 1 | **No rule or data from sound axes to V/E/T** beyond FEEL-MAP direction signs. No weights fitted. The audio mood labels do not exist (EMOPIA/VGMIDI are MIDI, 1,282 labelled clips, V/A only, no T) | Everything in sections 2-4 stops at "sign" without a size | Fit signed weights on EMOPIA+VGMIDI music axes first (MIDI axes 13-19 + mode + tempo already computable); a rendered-EMOPIA set (clip -> synth -> 22 axes) joins sound axes to V/A. Needs a T label: derive from tension arousal proxies (roughness, tonal tension, dissonance) or ask owner. |
| 2 | **Mode (the largest cue) and tempo are not axes among the 22.** Mode = major/minor share sits only in the formula mood, not as a characteristic; tempo only via density | The two biggest cues have no home in the axis list | Add mode and tempo as named features next to the 22 (they exist in `midi_full`, `chord_prog.mode`, `affect_midi.features.mode,bpm`) |
| 3 | **22 axes vs 11 in the sound models.** roughness, width, space, vowel, glide, and all music axes are not in activity/tuning/forward models | Roughness is the second-largest T cue (FEEL-MAP B); width and space are what Reverb/Delay/Chorus/Stereo change most | Store peak lists per render for roughness; record release tails (space); add axes to `p3_characteristics.COLS` before the next sweep |
| 4 | **Effects have no 22-axis responses.** Harness gave 8 features at 3 values, 1 knob at a time, 1 baseline, no noise floor; no pair interactions; no selectors (filter type, models, shapes, sync) | The knob->axis map of 30 effects is rough and can mislead (zero != inert) | Re-run `response_harness` with the 22 axis extraction, 5+ fractions, 2 baselines, then CHAIN-TRACK L1 pairs (2,400 renders) |
| 5 | **Space, silence, structure, mix measures are absent.** Space (axis 20) not measured; no silence/rest-ratio feature; no section/form feature on audio; mix (balance, masking, pan, send) waits on Ableton sets (G8); `Mixer` 49 knobs unmeasured | Reverb/Delay/Mixer are the biggest "feel" nodes in practice and have no V/E/T rule either | Space from release tail in renders; rest-ratio and section count from `arcs/detail`; Mix from `als` sets |
| 6 | **Note-effect nodes have zero node-level data** (24 Notes + 4 Prediction nodes). Their effect on music axes is inferred only | Humanizer, Quantizer, Gate, Glide, Arpeggiator, Chorder, Note Stack, Note Echo directly set tempo/density/legato/mode/chord, which are the top FEEL-MAP cues | Run them on fixed MIDI, measure the 7 music axes before/after (cheap, no audio) |
| 7 | **Synth engines not swept:** Partials, Wave Terrain (silent headless), Equation Synth, Sampler, Slicer, PaulStretch, Molder, Grain Molder, Granular, Field Synth, Drum Sequencer, MPC/VMPC, Looper | Whole families (granular, sampler, drums) have no knob->axis row | Same `own_sweeps.py` pattern per engine; Wave Terrain needs an image pin |
| 8 | **Knob->axis models are weak:** C2 MISS (direction 63.5% / 61.8%), Wavetable has only 6-19 pairs per knob, fullness/noisiness/movement unreliable; activity is context-dependent (detune dead at voices 1) | Sign errors would flip a mood push | More rounds (data), score only the axes the activity map says the knob drives, conditional activity per context |
| 9 | **Modulator/macro links unmeasured** (LFO rate/depth, envelope) | movement (axis 11) and tension via Vibrato are in FEEL-MAP; nothing links a cable to V/E/T | CHAIN-TRACK L3 renders |
| 10 | **FEEL-MAP gaps still open:** diminished/aug/sus/7th/9th chords, progressions, swing, micro-timing, late snare, width, space, vowel, glide, fullness (no source). Cultural limit (Western only; owner has Hindi/raga content, S7) | These rows have no direction at all | Measure chord-type vs mood on the 965 mood-named files (11,700 rows) and EMOPIA; swing/micro-timing vs EMOPIA/VGMIDI labels |
| 11 | **Mood formula unvalidated** (W4 r = 0.14/-0.01/0.32/-0.43/-0.01 vs declared pillars; weak labels) | The 203k mood rows are not ground truth | Compare formula to EMOPIA/VGMIDI labels directly (1,282 rows, quick) |
| 12 | **Music axes on 906 phrases only; samples have no curves or pitch** (sample rows are averages; pitch estimator verified only for named single notes 97.9%) | Few MIDI rows to fit a V/E/T rule on; no audio-side pitch height | Run the music-axis tool on all labelled MIDI (EMOPIA+VGMIDI = 1,282 files, minutes) |
| 13 | **Taste / owner anchor not in the model:** C3 critic owner agreement 24/40 (bar 30); no owner mood labels on any sound | The 70%-agreement listening check (FEEL-MAP step 3) has no baseline | owner listening pages (`listen_*.py`) |

## 10. Ten-line summary

1. 342 node types: sound relevant = 20 Synths, 30 AudioEffects, 5 audio Utility, 24 Notes, 8 Prediction, 33 Modulators, 9 Macros, 3 audio sources.
2. 22 axes: 15 measured on some data, 7 not or partly (space 20, vowel 21, glide 22 not at all; roughness only on MIDI; width and movement thin).
3. Knob->axis data exists for 6 synth engines (Analog 24, Oscillator 13, FM 30, Metallic 9, String 7, Wavetable 65 swept knobs, 142k rows) but on 11 axes only; Wavetable per-knob pairs are too few.
4. 30 audio effects have a first single-knob harness (156 knobs, 3 settings, 8 features, 1,179 rows); no pair, selector or 22-axis data. Space tools (Reverb, Delay) are unmeasured on space/roughness.
5. 24 note nodes and 33 modulators have no measurement at all; only INFERRED from function.
6. FEEL-MAP gives signed V/E/T for 17 axes (A/B/C evidence); width, space, vowel, glide, fullness have no row; mode and tempo (biggest cues) are not among the 22 axes.
7. Mood labels on disk: EMOPIA 1,078 clips and VGMIDI 204 (V/A only, MIDI), 965 mood-worded chord files (23 words, 11,700 rows), 203,115 formula mood rows (unvalidated), 69 genre pillars (34 hand-declared), 11 character words over 26,915 presets.
8. MIDI features extracted at scale: 3.16 M rows (chord 861k, melody 705k, expression 705k, bass 486k, drum 403k with velocity and micro-timing offset), 294,571 whole-file rows, arcs, 219 Ableton grooves; music axes 13-19 only on 906 phrases.
9. Weak links: C2 knob-direction MISS (63%), mood formula r about 0, no audio-side mood ground truth, no chain renders (0 of ~7,000).
10. Top gaps: (1) sound-axes -> V/E/T weights unfitted, (2) mode and tempo missing from the axis set, (3) roughness/space/width absent from sweeps, (4) effect responses too coarse, (5) note nodes unmeasured.
