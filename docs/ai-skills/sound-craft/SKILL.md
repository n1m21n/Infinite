---
name: infinite-sound-craft
description: Sound design and music craft mapped onto Infinite's nodes - genres and subgenres (tempo, groove, palette, structure), sound design recipes (bass, pads, leads, drums, textures, FX), mixing order and arrangement - so a vague musical ask ("dark techno groove", "lo-fi chords", "cinematic riser") becomes real nodes and values. Use whenever the user names a genre, mood, instrument, sound, groove, mix problem or song section.
---

# Sound craft in Infinite

Chain: notes -> synth -> effects -> Mixer -> Audio Out. Check every type with `infinite nodes`
and every key with `infinite describe <Type>` before you use it. Tempo is the transport's `bpm`
(`infinite status`); set it with `infinite tempo <bpm>`.

## Genres and subgenres

| Genre | Subgenres | BPM | Groove | Palette in Infinite |
|---|---|---|---|---|
| House | deep, tech, Chicago, French, afro | 118-128 | four on the floor, offbeat open hat, swung 16ths | Drum Sequencer, Analog bass, Chorder stabs, Piano chords, Reverb |
| Techno | minimal, Detroit, industrial, hypnotic, dub | 125-140 | relentless kick, rumble, ride, little melody | Drum Sequencer, Analog or FM rumble -> Reverb -> Audio Filter, Stutter |
| Drum and bass | liquid, neuro, jungle, jump-up | 170-176 | two-step breaks, half-time feel | Slicer on a break, Wavetable reese bass, Drive, Chorus |
| Dubstep, bass | brostep, riddim, deep dubstep | 140 (half time 70) | kick 1, snare 3, wobble bass | Wavetable or FM bass, LFO on Audio Filter cutoff in beats, Drive, Saturator |
| Hip-hop | boom bap, trap, lo-fi, drill | 70-100 (trap 130-150 felt half) | swung drums, sampled chords | Sampler, Slicer, MPC, Drum Sequencer, Humanizer, Bitcrush |
| Lo-fi | lo-fi hip-hop, chillhop | 70-90 | lazy swing, soft kick | Piano or Sampler chords -> Bitcrush (light) -> Tremolo -> Audio Filter low-pass |
| Ambient | drone, dark ambient, new age | free or 60-90 | no grid, slow swells | Granular, PaulStretch, Spectral Synth, Partials, Reverb huge, Convolution Reverb |
| Synthwave | outrun, darksynth | 80-118 | gated snare, driving 8th bass | Analog with detune, Chorus, Delay, Arpeggiator |
| Trance | uplifting, psy, progressive | 132-145 (psy 138-148) | rolling 16th bass, long builds | Arpeggiator -> Analog supersaw (voices, spread), Delay, Reverb |
| Pop | synth-pop, indie, hyper-pop | 90-130 | clear backbeat, hook first | Piano, Analog, Chorder, Dynamics, Limiter |
| Cinematic | trailer, underscore, horror | free | swells, hits, risers | String, Piano, Resonator Bank, Shape Resonator, Reverb, Pitch Shifter |
| Experimental | IDM, glitch, noise, generative | any | broken, probabilistic | Field Synth, Random Note Generator, Bouncing Balls, Stutter, Spec Blur |
| World-rooted | Indian classical, afrobeat, latin | per style | tala, clave, polyrhythm | Note Sequencer with odd lengths, Drum Sequencer lanes at different lengths, Key-Snap |

Structure for dance music: intro 16 -> build 8 -> drop 16 -> break 16 -> build 8 -> drop 16 ->
outro 16 (bars). Changes land on 8 or 16 bar boundaries. Arrange in the Timeline panel
(`[Open timeline](open:timeline)`); launch live from the Session panel.

## Sound design recipes

| Sound | Recipe |
|---|---|
| sub bass | Analog or Oscillator sine, one octave below the bass line, no effects, mono |
| reese bass | Analog two saws, `detune` up, Chorus, Audio Filter low-pass 200-800 Hz |
| acid | Analog saw, high `resonance`, Envelope on `cutoff`, Glide on overlapping notes |
| wobble | Wavetable bass, LFO in beats (1/4, 1/8) on Audio Filter cutoff |
| pad | Analog or Wavetable, slow `attack` (0.5-2 s), long release, Chorus, Reverb |
| pluck | short decay envelope, no sustain, Delay 1/8 dotted |
| lead | one saw, light Drive, Delay 1/4, Reverb small |
| supersaw | Analog `voices` up, `spread` and `detune` up, Stereo width |
| kick | Drum Sequencer lane with a kick sample, or Oscillator with a fast pitch Envelope |
| bell, metal | FM ratios not whole numbers, Metallic, Resonator Bank |
| texture | Granular or Grain Molder on a field recording, PaulStretch, Spec Blur |
| riser | noise -> Audio Filter with MSEG opening over 8 bars, Pitch Shifter up, Reverb growing |
| impact | low Oscillator drop + noise burst -> Convolution Reverb |

## Mixing

- Order per channel: EQ (cut first) -> Dynamics -> Drive or Saturator -> modulation FX -> Delay -> Reverb.
- Low end: one sub source, high-pass everything else around 100-150 Hz.
- Space: Reverb and Delay on fewer sounds than you think; predelay keeps the dry hit clear.
- Loudness: Limiter last on the master, never on every channel. Watch Audio Meter.
- Width: low end mono, pads and FX wide (Stereo, Chorus), lead centred.
- Sidechain feel: Envelope or Audio to CV from the kick ducking the pad's volume.

## Notes and harmony

| Ask | Do |
|---|---|
| chords | Chorder or Note Stack on a single note line; Piano Roll to edit |
| stay in key | Key-Snap on audio pitch, Quantizer on notes |
| groove | Humanizer for timing and velocity, Velocity Curve, swing in the sequencer |
| melody ideas | Arpeggiator, Random Note Generator in a scale, Predictive Notes |
| moods | minor and Dorian dark, Lydian dreamy, Phrygian tense, major pentatonic bright |

Picture and sound together: Audio Analyze or Audio to CV turns the music into modulation for any
visual param (see infinite-visual-craft).
