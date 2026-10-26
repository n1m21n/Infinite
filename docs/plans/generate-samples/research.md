# Generate Samples: sound research (2026-10-26)

Owner ask: research each sound properly, online sources allowed, downloads allowed. This file holds what the sources
say, what Infinite does today, and what changed. No audio was downloaded: every number below came from text sources,
and the models are built from the synths already in the app. Where a source gave no number, that is marked.

## Pluck

| Source says | Infinite |
|---|---|
| Bright, harmonically rich start (saw or bright wavetable); square gives a hollow retro pluck ([ask.audio](https://ask.audio/articles/how-to-create-big-room-synth-plucks-in-logic-pro-x), [Violet Recording](https://violetrecording.com/how-to-make-a-pluck-sound/)) | 9 kits across saw, string, mallet, kalimba, tine, FM, glass tables |
| Near-instant amp attack, short-to-medium decay, zero or very low sustain | attack 1–5 ms, decay 120–350 ms, sustain 0 |
| Low-pass with its own envelope: fast attack, short decay; filter attack a touch faster than amp ([Avid](https://www.avid.com/resource-center/how-to-make-trance-plucks)) | filter decay 30–80% of amp decay |
| Pitch envelope: start an octave or two high for a few ms, then drop ([KVR](https://www.kvraudio.com/forum/viewtopic.php?p=8802313)) | chirp on some kits |
| Marimba second partial ~4.0×, third ~9.2×; vibraphone decays far longer than marimba ([CCRMA](https://ccrma.stanford.edu/courses/150-2001/percussion.html)) | marimba and vibraphone tables used; no per-instrument decay split yet |

## Pad

| Source says | Infinite |
|---|---|
| Medium-to-long attack and release on the amp ([Ableton](https://www.ableton.com/blog/pad-it-out-10-ways-make-distinctive-pad-sounds/), [MusicRadar](https://musicradar.com/how-to/how-to-create-an-ambient-synth-pad-from-scratch)) | swell filter, long release |
| Unison with small detune, stereo spread; check phase | unison/detune/width on the voice |
| Chorus for subtle motion ([Attack](https://www.attackmagazine.com/technique/tutorials/cold-dark-pads/)) | **added**: every pad runs through a Chorus (rate 0.15–0.6 Hz, 3–8 ms depth) |
| Slow free-rate LFO on cutoff or wavetable position, not tempo-synced ([Syntorial](https://www.syntorial.com/tutorials/synth-quickie-evolving-pad/)) | **added**: wavetable position drifts over 16/24/32 beats on every pad |
| Long warm reverb, plus delay (feedback delay can be cleaner than reverb) | reverb always, delay 60% (dotted beats) |

## Kick

| Source says | Infinite |
|---|---|
| Sine body, attack 0, sustain 0; decay 200–300 ms house, 400+ ms techno, a beat or longer for 808/trap ([Beat Kitchen](https://beatkitchen.io/guides/electronic-music/02-drum-design/)) | punch 130–260 ms, thump 200–420, 808 450–900 |
| Pitch sweep: decay 20–80 ms, a few semitones to an octave+ | 8–36 semitones, 18–110 ms by kind |
| Noise click: attack 0, decay 5–15 ms, small amount | **added**: 70% of synth kicks get a 5–15 ms noise click at −13 dB |
| 808 pitch falls on an exponential curve and drifts as it decays ([Perfect Circuit](https://www.perfectcircuit.com/signal/kick-drum-synthesis)) | pitch envelope |
| Sub is sine, so harmonics (saturation) are what small speakers hear ([MusicTech](https://musictech.com/tutorials/subs-for-small-speakers/)) | bass has an optional Drive |
| Not found | the 30–60 Hz fundamental range of an 808: no source gave it |

## Snare

| Source says | Infinite |
|---|---|
| Body sine/triangle 150–250 Hz, decay 80–150 ms ([Beat Kitchen](https://beatkitchen.io/guides/electronic-music/02-drum-design/)); the 909 uses two partials at 180 and 330 Hz ([Sound On Sound](https://www.soundonsound.com/techniques/practical-snare-drum-synthesis)) | **fixed a bug**: the synth snare body sat at 73–110 Hz (MIDI 38–47); now MIDI 52–58 (~175–260 Hz), decay 60–140 ms |
| Noise through band/high-pass, decay 100–250 ms; house leans noise, dubstep leans tone | band-pass or LP24 noise 1.8–6 kHz, 70–220 ms |

## Clap

| Source says | Infinite |
|---|---|
| Noise through band-pass at 1–3 kHz, 3–4 retriggers 10–20 ms apart, then a 100–200 ms tail (Beat Kitchen; [KVR 909 thread](https://www.kvraudio.com/forum/viewtopic.php?p=8606994): four repeats, ~11 ms, BP 1140 Hz Q 1.95, poster unsure) | **fixed**: was one burst; now a 9–16 ms feedback Delay (0.5–0.7) after BP noise gives the repeated bursts |

## Hi-hat

| Source says | Infinite |
|---|---|
| Closed: high-passed noise 5–8 kHz, decay 10–40 ms. Open: 200–500 ms, choked by the closed hat (Beat Kitchen) | **fixed**: synth closed hat decay was 50–220 ms, now 20–90 ms (Metallic units) |
| 808 uses six square oscillators through band-pass filters; the frequencies are unit-dependent ([TipTop HATS808](https://www.Andertons.co.uk/tiptop-audio/tiptop-audio-hats808-tr808-hi-hats-drum-generator)); forum list 205.3, 304.4, 369.6, 522.7, ~540, ~800 Hz ([Elektronauts](https://www.elektronauts.com/t/hh-lab-tips/207960)) is community-collected, unverified | Metallic node with 8 materials |

## Toms and percussion
Beat Kitchen gives no tom or percussion numbers. Toms are built as a dropped sine (8–22 semitones over 40–110 ms,
180–460 ms decay), an engineering choice, not a sourced one.

## Drum patterns and tempo

| Genre | Source says |
|---|---|
| House | 115–130 BPM; kick on every beat, closed hat on the off-beats, clap on 2 and 4 ([Ableton](https://learningmusic.ableton.com/make-beats/tempo-and-genre.html), [Beat Kitchen](https://beatkitchen.io/guides/electronic-music/01-genre-landscape/)) |
| Techno | 120–140 BPM; same kick, 16th-note hats, quieter and mechanical |
| Boom bap | 85–100 BPM |
| Trap | 130–150 BPM written, felt half-time; fast hat rolls (detail not found) |
| Drum and bass | 160–180 BPM, cluster at 174–176; two-step snares on 2 and 4, lightly swung 8th hats |
| Breakbeat | no source found |

## Bass

| Source says | Infinite |
|---|---|
| Reese: two or more detuned saws; the beating is the sound; ±0.3 semitone (NI), wide unison past ~0.4 splits into separate pitches; filter ~650 Hz, res ~14%; sine sub an octave down, high-pass the Reese above ~100 Hz ([NI](https://blog.native-instruments.com/reese-bass/), [Future Proof](https://futureproofmusicschool.com/blog/reese-bass-sound-design-everything-you-need-to-know)) | reese kind exists; detune and sub layering not yet checked against these numbers (open) |
| Acid: low cutoff, high resonance, filter envelope opened over time, accent raises volume + resonance + envelope together, slides ([Roland](https://articles.roland.com/beyond-acid-pushing-the-tb-303-into-new-sonic-territory/)) | acid kind; no accent or slide yet (open) |
| Sub/808 is sine-based and below ~100 Hz | sub, 808 kinds |

## FX

| Source says | Infinite |
|---|---|
| Riser: noise through band/high-pass with cutoff automated up, slow volume swell; pitch riser from detuned saws; downsweep at the drop; impact ends a riser; whoosh is a short pass-by with stereo movement ([Violet Recording](https://violetrecording.com/how-to-make-risers-and-sweeps/), [NI](https://blog.native-instruments.com/sound-in-film/)) | riser, downlifter, impact, texture, zap, sparkle. Missing: whoosh with a stereo sweep, Shepard-style endless riser (open) |
| Reverse reverb: no source found | not built |

## Foley

| Source says | Infinite |
|---|---|
| Three groups: feet (footsteps, runs, landings), moves (cloth, bedsheets), specifics (props such as keys, phones, tools). Surface and footwear drive footsteps: wood, concrete, dirt, gravel, tile; leather creaks sharper than denim ([Krotos](https://krotos.studio/blog/foley-sound-design)) | feet: footsteps **now by surface** (dirt, wood, concrete, gravel). moves: rustle only. specifics: knocks, clocks, glass, typing, bubbles, drips, heartbeat, crackle |
| Not found | no source for exact spectra or timings of foley sounds |

## Gaps this research could not fill
- Numeric ranges for toms, percussion, 303 envelope times, 808 fundamentals, whoosh duration, foley spectra.
- Genre pattern detail beyond the basic skeletons (trap rolls, breakbeat).
- Anything audible: these are text specs. Whether a pluck sounds like a pluck is the owner's listening test.

## Second pass: gaps looked up (2026-10-26)

| Gap | Found |
|---|---|
| 808 fundamental | Tune to the key's root; C1 32.7, E1 41.2, F1 43.65 Hz (equal temperament) ([Music Production Wiki](https://musicproductionwiki.com/tools/808-sub-bass-tuner)). Below ~100 Hz; sine, so add harmonics for small speakers ([MusicTech](https://musictech.com/tutorials/subs-for-small-speakers/)) |
| 303 acid | Open-source model defaults: decay 30–3000 ms (300 default), slide 60 ms, cutoff 1 kHz, resonance 50%, env mod 25%; accent shortens the filter envelope and boosts it, MIDI velocity ≥100 = accent ([nt_303](https://github.com/thorinside/nt_303)). Hardware 200/30 ms figures not confirmed |
| Whoosh | Noise through band/low-pass, cutoff swept up and down over ~0.5–1 s with a matching swell; needs movement; rising = approaching, falling = leaving; weight from the low end ([Violet Recording](https://violetrecording.com/how-to-make-whoosh-sounds/)). Vendor examples 2 s fast transition, 4–5 s long |
| Tom | A tom starts higher and falls as the head's vibration gives way to the body's resonance; one unvetted script uses 120→72 Hz over 0.35 s ([Syntom article](https://siliconchip.com.au/Issue/1989/May/Syntom+-+A+Synthesised+Tom-Tom)) |
| Shaker, conga, bongo, rim, cowbell | No usable numbers found (Bitwig's cowbell is two oscillators with an offset, no values) |
| Categories | Sample packs and presets sort into bass, keys, leads, pads, plucks, synth (arps, stabs, chords), percussion, drums, FX (builds, sweeps, reverses, hits), textures/atmospheres/drones, foley ([Elektronauts](https://elektronauts.com/t/wtf-do-sample-pack-producers-mean-by-keys-and-synth/228064), [Evolution pack](https://mataio.gumroad.com/l/evolutionsamplepack)). No single standard exists |

## Third pass: built from the gaps (2026-10-26)

| Gap | What changed |
|---|---|
| Drum sub-parts | Parts Kicks, Snares, Hats, Toms render one lane family each. Genre feel (house, techno, boom bap, trap, dnb, breakbeat) sets the kick/snare/hat/clap pattern and a tempo band from the skeletons in "Drum patterns and tempo" |
| 808 root | The 808 bass kind plays the key's root (degree 0) instead of a free scale degree, per the tuning source in the second pass |
| 303 acid | Acid bass accents: velocity 1.0 on accents (0.45-0.65 otherwise), glide 50-70 ms, gate 85-110% (nt_303 defaults: slide 60 ms, accent = loud + short envelope) |
| Whoosh | FX kind: noise through a band-pass whose cutoff sweeps, plus a Stereo pan sweep, ~0.5-1 s (Violet Recording) |
| Shepard riser | FX kind 6: three octave-spaced voices whose volumes cross-fade while pitch rises, so the riser never ends |
| Keys, Leads, Textures | New parts (appended to the enum, so saved patches keep their value). Keys: tine, organ, felt piano, vibes, clav, pipes. Leads: brass, reed, saw, pulse, vowel, FM. Textures: drone, vowel bed, shimmer, rumble, wind, grain. Categories follow the pack/preset split in the second pass |
| Root prior | With no key set, the root is drawn from a prior: G major 10.7%, C major 10.2% are the Spotify catalogue figures (Kenny Ning 2015); the other weights are estimates |
| Website | The generator runs in the browser as wasm (wasm/): same Compile + OfflineGraph + Master, three nodes that start threads (Partials load, Convolution IR build, Shape Resonator solve) run inline there |

Still not measured by ear: every number here is a text spec. Whether a part sounds right is the owner's listening gate.
