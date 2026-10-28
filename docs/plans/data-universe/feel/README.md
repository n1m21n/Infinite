# Feel model, merged (2026-10-28): A node map + B mix/structure + C music cues

| File | What |
|---|---|
| A_node_map.md | every synth/effect/note node and knob -> axes -> V/E/T; what is measured vs inferred; mood and MIDI data on disk; 13 ranked gaps |
| B_mix_structure.md | spectrum bands, good-mix numbers, silence, build-ups, groove numbers, instrument roles |
| C_music_cues.md | numeric cue effects (tempo, mode, register, dynamics, chords, rhythm), labelled datasets, models |
| ../FEEL-MAP.md | first pass (directions only); superseded by these where numbers differ |

## Decisions
| # | Decision | Why |
|---|---|---|
| 1 | Mood = V, E (energy arousal), T (tension arousal) | Eerola 2012 splits arousal; C: audio R2 V .72, activity .85, tension .79 |
| 2 | Rule form = linear sum of cue terms + 2 penalties (mode/tempo conflict, extreme pitch) | Eerola 2013: linear 77-89% of variance, no surviving interactions; Hunter 2010 conflict |
| 3 | Do not hard-couple mode to tempo | Poon and Schutz 2015: only tendencies in real music |
| 4 | Add axes 23-31 (below) so the biggest cues exist as numbers | A: mode and tempo are not among the 22; they carry the most variance |
| 5 | Fit V/E/T weights on our own data (EMOPIA 1,078 clips, VGMIDI 204, 11,700 chord rows); formula mood rows (203k) are reference only | A: formula rows disagree with genre pillars (r about 0) |
| 6 | Genre mix targets (LUFS, crest, tilt) computed from our renders + Harmonix Set, not from guesses | B: no genre-resolved study exists |
| 7 | Measure the unmeasured nodes before using their mapping (all 24 note nodes, modulators, 13 synths, effect pairs) | A: only 156 single-knob effect tests exist |

## New axes to add
| # | Axis | Measured how |
|---|---|---|
| 23 | mode (major-ness) | key and mode estimate on MIDI; on audio chroma profile |
| 24 | tempo (BPM) | onset autocorrelation |
| 25 | swing ratio | long:short of paired onsets (3.5:1 slow to 1:1 fast) |
| 26 | micro-timing spread (ms) | onset offset from grid, per part |
| 27 | silence ratio and longest rest | fraction of time below -50 dB; rest lengths |
| 28 | spectral band energies (sub, bass, low-mid, mid, presence, air) | band RMS |
| 29 | loudness LUFS and crest factor / loudness range | pyloudnorm |
| 30 | stereo width and low-band mono-ness | mid/side, below 250 Hz |
| 31 | section energy arc (build slope, drop height) | per-bar loudness and density curve |

Already unmeasured and still to fix: space (20), vowel (21), glide (22), roughness on audio, width, movement.

## Order of work
1 add axes 23-31 to the measurer (MIDI side first, no renders needed) -> 2 measure effect pairs and note nodes (renders) -> 3 fit V/E/T weights on labelled data -> 4 held-out check (r >= 0.8, owner 70%) -> 5 compute genre mix targets from our renders and Harmonix -> 6 wire the same V/E/T target into note model, sound model and chain choice.
