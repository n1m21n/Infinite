# Generate Samples: sound-type rebuild

Status: sections 1-7 drafted for sign-off. Nothing below is built yet. Numbers in the spec cards are
**hypotheses from sound-design practice**; each one is checked against reference audio (section 8, step 2)
before it goes into the generator.

## 1. Goal
Every part the node offers (synths, pads, plucks, percussion, drums, plus bass and FX/foley if approved)
sounds like what its name says, and two presses of generate sound like two different instruments.

## 2. Problem
| Symptom (owner, 2026-10-26) | Likely cause (to confirm in the baseline) |
|---|---|
| "Current plucks don't sound like plucks" | Pluck = Analog or Wavetable voice, envelope 1-5 ms attack / 120-350 ms decay, arpeggiated chords. No filter-envelope snap, no damping, no body or pick transient; the voice pool is the pad pool |
| Too much repeatability and similarity | A part draws from ~3 voice types per card and about 34 cards; the same 4 envelope numbers; fixed drum grids per card; one reverb/delay chain family |
| Pads are not "pads" | Envelope is fine, but no slow movement (drift, detune, filter or wavetable motion), no wide stereo, no big reverb/delay tail |
| Percussion and drums share one engine | One sample kit (8 kit files) plus synth voices; no per-instrument model for kick / snare / hat |
| Bass, FX, foley absent | Not offered |
| "No data" users get a generic result | The baseline is the style card only |

## 3. Scope
- In: voice design and envelope rules per part; per-instrument rules for kick, snare, hat, clap, tom, rim, shaker; groove and pattern generation; FX and foley as new parts; a researched default profile used when the player has no data; the diversity measurement tool.
- In: the UI already changed (bars dropdown, follow tempo, follow scale).
- Out: new node types (the rebuild uses the nodes that exist; a missing capability is listed in section 11, not built); changing the personalisation layer; export/render settings.
- Needs owner approval first (new features): bass part, FX part, foley part.

## 4. Existing constraints
- Offline render budget: a 2-bar generate must stay within the 0.5 s budget in optimised builds.
- Patch is a text graph of existing nodes (SampleStyles::Compile); only node params, wiring and the random stream change.
- Same recipe must re-render the same loop (reopen), so every new random draw is seed-keyed on its own stream.
- Personal profile layers on top; a researched default replaces "card only" underneath.
- Debug-build timing is not representative; measure in release.
- No audio data downloaded without a yes (file name, source, size). Licences must allow analysis and must not ship inside the app.

## 5. Hypothesis
| # | Claim | How it is tested |
|---|---|---|
| H1 | Replacing one-size envelopes with per-type models (spec cards below) makes parts identifiable by ear and by spectrum | Spectral/envelope features fall inside each type's reference range |
| H2 | Drawing the voice, the modulation (filter env, LFO, drift), the effect chain and the pattern from independent seed streams raises diversity more than adding cards | Pairwise feature distance across 1 000 takes |
| H3 | Parts built from a role-based voice recipe (e.g. pluck = short exciter + resonant body + filter env + damping) beat parts picked from a named node pool | A/B listening on 20 takes per part, owner blind |
| H4 | A researched default profile per type removes the "no data" weakness without hurting players with data | Same metrics on an empty profile |

## 6. Baseline (recorded once, before any change)
Method, fixed now, run once on the current release build and never re-measured:
- Render the existing pool: every card x part at seeds 1-3, 2 bars (`INFINITE_GENSAMPLEBATCH`), plus 200 random-card takes per part.
- Per take compute: spectral centroid, spectral flatness, crest factor, onset count, median onset spacing, envelope decay time (-20 dB), stereo width, low-band (<150 Hz) share, pitch-class histogram.
- Per part report: mean pairwise standardized distance, number of distinct patch graphs, number of distinct (voice types, drum pattern, root, scale) tuples, share of takes within a small distance of another take.
- Recorded in `docs/plans/generate-samples/baseline.md` by `tools/gensample_diversity.py` (to write).

## 7. Success metric
| Metric | Target vs baseline |
|---|---|
| Mean pairwise distance per part | at least 1.5x the baseline, no part below it |
| Near-duplicate share (nearest neighbour closer than a quarter of the baseline's mean pairwise distance) | under half the baseline |
| Type fit (features inside the spec card's range) | at least 90% of takes per part |
| Blind A/B vs old generator, owner, 20 takes per part | owner prefers the new one for at least 16 of 20 per part |
| Plucks, owner blind "is this a pluck" | at least 18 of 20 |
| 2-bar generate time (release) | within the 0.5 s budget |
| Reopen determinism | same recipe renders the same loop (existing test) |

Stop condition: two rounds with a part not improving on the distance and A/B gates, then park that part and report.

---

## Building blocks to expand (owner, 2026-10-26: "expand the building blocks")
Repetition comes from how few things a take can be made of. Each take draws from these axes; the plan widens every one.
| Axis | Today | To add |
|---|---|---|
| Voices per part | ~3 synth types per card, 34 cards | a role recipe per part with 6-10 voice builds each (e.g. pluck: string, mallet, harp, FM, saw-stab, kalimba, koto, pizzicato) |
| Oscillator and tone | one oscillator type per voice | layered oscillators, detune spread, sub layer, noise layer, wavetable position |
| Modulation | seeded param jitter | filter envelope, LFO to cutoff/pitch/pan, drift, velocity to brightness, slow random movement |
| Envelopes | 4 numbers per part | shape families per type (snap, bloom, swell, gate, ring-out) |
| Effects | 2-3 from a card's list | chains per type (space, saturation, filter, modulation, delay), order and amount drawn |
| Rhythm | fixed grid string per card | a groove library per genre family plus generated euclidean/polyrhythm patterns, swing, fills, density |
| Harmony | root, scale, a few chord sizes | progression library, voicings and inversions, modal mixture, octave choice |
| Drums | 8 kit files + a few synth voices | synthesised kick/snare/hat/clap/tom/rim/shaker models, 6+ kit types |
| Sources | built-in nodes only | optional CC0 sample sets for drums, percussion, foley (needs a yes before any download) |
| Processing | none | per-take pitch, filter, layering, reverse, stutter, bit-reduction drawn from the seed |
Rule: no new axis is added unless the diversity tool shows it raising pairwise distance; each is switched on one at a time (keep-only-if-better).

## Appendix: sound spec cards (the 14 questions)
Format per type: what it is, what makes it that, how to build it from Infinite's nodes, what varies between takes.
All times are starting points to be checked against reference audio.

### Pad
- **What:** a sustained, slowly evolving bed. Its job is space and harmony, not rhythm.
- **Defining traits:** attack 0.3-3 s; release 1.5-8 s; sustain high; little transient; wide stereo; slow motion (LFO 0.05-0.5 Hz, detune drift, wavetable or filter sweep over 4-32 s); low-pass 1-6 kHz, never bright and hard.
- **Types:** warm analog (detuned saws or pulses + low-pass), glass/FM bell pad, string ensemble (many detuned saws + chorus), choir/vowel (formant filters), evolving wavetable, granular/texture, drone.
- **Space:** long reverb (decay 4-12 s, pre-delay 20-80 ms, high damping), a synced delay at low feedback; both give "drift".
- **Varies:** type, chord voicing and inversion, detune amount, motion rate and depth, filter shape, reverb size, stereo width, register (octave).

### Percussion (non-kit)
- **What:** pitched or unpitched hits that add rhythm colour: shakers, congas, bongos, claves, woodblocks, tambourine, cowbell, triangle, metallic bells, hand drums, taiko.
- **Traits:** a transient under 5 ms, then a short body; families differ by decay (wood 40-150 ms, skin 100-400 ms, metal 300 ms-3 s), by pitch (tuned or noisy), and by spectrum (inharmonic partials for metal).
- **Build:** short noise burst + band-pass for shakers; sine/triangle with fast pitch drop + noise for skins; inharmonic partials (FM at non-integer ratios, Metallic) for bells.
- **Varies:** instrument family, tuning, decay, velocity pattern with accents and ghosts, sparse or busy density, swing.

### Pluck
- **What:** a struck or plucked string-like note: instant attack, bright start, quick decay to a darker, shorter sustain.
- **Traits:** attack under 3 ms; decay 80-600 ms; sustain 0; a low-pass filter envelope that opens at the hit and closes within 100-400 ms (this is the "pluck" sound); resonance 0.1-0.4; light stereo chorus or short delay; often a tiny noise or click.
- **Types:** Karplus-Strong string (Analog-like body with damping), synth pluck (saw + filter env), kalimba/mallet (sine + inharmonic partial, fast decay), harp, koto, pizzicato, FM pluck, pitched pluck chord stab.
- **Why the current ones fail:** the amplitude shape is right, but there is no filter-envelope movement and no per-note brightness drop; a lowpass with a fixed cutoff makes them sound like short organs.
- **Varies:** type, filter-env depth and speed, decay, note length, arp pattern/rate/direction, octave, velocity-to-brightness, echo.

### Bass
- **What:** the lowest pitched line: sets root and groove.
- **Traits:** fundamental 30-120 Hz; mono below 120 Hz; clean sub plus a mid layer for small speakers; usually monophonic, with a tight envelope and a gate on the groove.
- **Types:** sub sine, 808 (long decay, pitch glide, clipped), reese (two detuned saws + low-pass movement), acid (saw, resonant filter with envelope, slides), FM bass, pluck/finger bass, wobble (LFO on the filter), slap/funk.
- **Build:** oscillator layer(s) + low-pass; saturator for harmonics; sidechain from the kick.
- **Varies:** type, pattern (root, fifths, octave jumps), slides, filter-env, saturation, length.

### Drum set
- **What:** a kit (kick, snare, hats, toms, cymbals, clap) playing a groove.
- **Kit types:** acoustic rock, jazz brushes, 808, 909, lo-fi sampled, trap, breakbeat, electronic glitch.
- **Grooves:** four-on-the-floor (house/techno), backbeat (rock/pop), breakbeat (kick on 1, ghost snares, syncopation), half-time (trap/dubstep), shuffle/swing (hip hop, garage), Afro-Latin clave patterns, DnB two-step.
- **Per-groove rules:** kick placement, snare on 2 and 4 (or 3 for half-time), hats on 8ths/16ths with open hat on the offbeat, ghost notes at low velocity, fills at the end of the 4th or 8th bar, tempo bands per genre.
- **Varies:** kit, groove, density, swing 50-68%, velocity humanisation, fills, kit tuning, room reverb.

### Kick
- **What:** the low-frequency pulse of the beat.
- **Traits:** a fast pitch drop (150-60 Hz to 40-60 Hz in 20-60 ms), body 80-400 ms, a click at 2-5 kHz for attack, sub under 80 Hz.
- **Types:** 808 (long, tonal), 909 (punchy), acoustic (shell and beater), techno (tuned, distorted), hardstyle (long clipped), lo-fi (short, filtered).
- **Varies:** start/end pitch, drop time, decay, click level, distortion, tuning to the key.

### Hi-hat
- **What:** high-frequency noise-like metal.
- **Traits:** spectrum 5-16 kHz; closed decay 20-80 ms; open 200-800 ms; closed hat chokes open hat; velocity changes tone, not only volume.
- **Build:** six detuned square oscillators (808/909 method) or white noise, high-pass at 6-8 kHz, band-pass resonance around 8-10 kHz, short exponential envelope.
- **Varies:** closed, open, pedal, decay, pitch/brightness, pattern (8ths, 16ths, triplets, rolls).

### Snare
- **What:** the backbeat hit: tonal body plus noise.
- **Traits:** body 150-250 Hz (decay 80-200 ms); noise 2-10 kHz (decay 100-300 ms); sharp attack; optional reverb.
- **Types:** acoustic, 808, 909, trap (tight, bright), rimshot, clap (several noise bursts 10-30 ms apart), brush.
- **Varies:** tune, noise balance, decay, snap, room, layered clap.

### FX
- **What:** non-musical effects that move a track: risers, downlifters, impacts, sweeps, glitches, reverse tails, stingers, laser/zaps, transitions.
- **Traits:** defined by a controlled change over time (filter, pitch, volume, density) across 1-8 bars, usually into a downbeat; impacts are a sub boom + noise burst + long tail.
- **Varies:** type, direction (up/down), length, texture (noise, tonal, metallic), reverb size.

### Foley
- **What:** everyday sounds that are not music: footsteps, cloth, doors, paper, keys, water, wood and metal taps.
- **Traits:** short, noisy, irregular; defined by materials (spectrum and decay) and by a human-like timing.
- **Build:** filtered noise bursts + resonators for body; random timing with small jitter and velocity variation. Synthesis gets only part of the way; sampled sources (own recordings, a CC0 library, with a yes before any download) are the strong option.
- **Varies:** material, action, rhythm of the action, room.

### What may be missing
Chord stabs and keys (piano, organ, e-piano, Rhodes), leads (solo melodies), arps as their own part, vocal textures (choir, formant, vocoder), noise beds and ambiences, sub-only bass, risers as a separate part from FX. Proposed for later review, not in this scope.

---

## 8. Best and optimised methodology (draft)
1. Build the diversity tool and record the baseline (section 6). One chunk.
2. Gather references: for each type, 20-50 reference sounds from sources the owner approves (own library, CC0 packs). Measure the spec-card features and replace each hypothesis number with a measured range. Analysis only; nothing shipped.
3. Rebuild per type in order of audible gap: pluck, pad, drums/kick/snare/hat, percussion, bass, then FX and foley. Per type: voice recipe, modulation, effect chain, independent seed streams, `Probe` + type-fit test, A/B vs the old generator.
4. Default profile per type (what a player with no data gets) from the same references.
5. Keep-only-if-better gate on the section 7 metrics after each type.

## 9. Proposed solution trajectories
| Trajectory | Pros | Cons |
|---|---|---|
| A. Per-type voice recipes from existing nodes (recommended) | No new engine code; fits the offline graph; reopen stays deterministic | Limited by what the nodes can do; foley and acoustic realism capped |
| B. Add sampled sources (CC0 recordings) for kicks, snares, foley, percussion | Most realistic; fastest to good drums | Package size, licence work, less variation unless processed |
| C. Hybrid: A for tonal parts, B for drums/foley, with seeded processing (pitch, filter, layering) | Best result per part | Two systems to maintain |
| D. More cards only | Cheap | Does not fix "does not sound like a pluck" and does not cut similarity much |

What-if: if A cannot reach the fit/A-B gate for kicks, snares or foley, move those parts to B or C. If bass or FX is not approved, the scope drops to five parts and the metrics apply to those.

## 10. Benchmarking against the baseline
Filled after the work: results tables linked to section 6 and section 7.

## 11. Critique (initial)
- Reference numbers are my own priors, so the quality of this plan depends on step 2 and not on this text.
- Pairwise feature distance can be raised by noise; the type-fit and the owner A/B are the checks against that.
- Plucks and foley may expose missing node capabilities (a damped string resonator, a transient designer); these are listed as findings, not built without approval.
- The default profile for no-data users is only as good as the references behind it.

## 12. Next steps
1. Owner signs off sections 1-7 (and says yes or no to bass, FX, foley as new parts).
2. Build the diversity tool and record the baseline.
3. Reference gathering plan with exact file names and sources for approval.
4. Rebuild per type in the order above, each with the keep-only-if-better gate.
5. Ship / iterate / abandon decision per part.


## Build log: sound models (2026-10-26)

- Plucks: nine kits (string, harp, mallet, kalimba, tine, saw stab, squelch, FM pluck, glass) on the Wavetable voice with its own filter envelope for the snap.
- Pads: eight kits (warm saw, strings, choir, glass, shimmer, dark drone, organ, soft keys), slow swell, long release, per-take reverb size and optional dotted delay.
- Drums: per-take kit character (tight, boomy, crunchy, soft, snappy), per-lane pitch/decay/snap/pan, synth kick (punch, 808, thump) and metallic hats two takes in five.
- New parts: bass (sub, 808, reese, pluck, FM, acid), fx (riser, downlifter, impact, texture, zaps, sparkle), foley (steps, knocks, drips, ticks, rustle, crackle).
- Gotchas: Wavetable `mix 1` renders silent; the seeded draw leaves `a.on` off and scatters the pitch envelope, so built voices set them (`CleanWavetable`).
- Measured on 30 takes per part (release): near-duplicate share drums 53% -> 23%, percussion 29% -> 7%, pads and plucks 0%; mean pairwise distance drums 3.09 -> 2.83, percussion 4.54 -> 3.51 (open: widen drum/percussion/foley families).

## Build log: parts, genre feel and the web generator (2026-10-26)

- Parts now: full, synths, pads, plucks, percussion, drums, bass, fx, foley, keys, leads, textures, kicks, snares, hats, toms (16; new ones appended to the enum so saved patches keep their part). Sources: [research.md](research.md), "Third pass".
- Drum sub-parts and genre feel: six feels (house, techno, boom bap, trap, dnb, breakbeat) set lane patterns and the tempo band; kicks/snares/hats/toms keep only their lanes and lean on synth drums.
- Bass: acid accents and slides, 808 on the key root. FX: whoosh with a stereo sweep, endless Shepard-style riser. Root prior when no key is set.
- Fallback: with no learned profile the generator uses the cards alone (personal = false).
- Web generator (`wasm/`, target `infinite_gen_wasm`, `docs/plans/web-generator/`): the same Compile + OfflineGraph + Master in Emscripten, run in a Web Worker (`website/gen/`), loaded on the first press. Three thread-starting nodes (Partials load, Convolution IR, Shape Resonator) run inline there.
  - Build: `source ~/emsdk/emsdk_env.sh && emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm -j2`.
  - Parity: `tools/web_gen_parity.py` compares 5 styles x 6 parts at seed 1 against the desktop render (length, LUFS within 0.5 dB, correlation > 0.99). Desktop side: `INFINITE_GENSAMPLEPARITY=1 INFINITE_GENSAMPLEBATCH=<dir>`.
