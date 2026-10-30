# Sound model: handoff plan (2026-10-28, rewritten after the owner's survey, section 6)

For the next Claude account. Read this whole file first (section 6 holds the owner's own answers and wins over anything else), then
`SOUND-MODEL.md` sections 4 and 4b for the measured state. This replaces "keep rendering presets" and the earlier pick-first plan.

## 1. The purpose

**A model that understands sound at an intelligent level and can therefore make new sounds.** Understanding means one shared,
mathematical space where timbre (synth knobs, samples) and music (pitches, chords, changes over time) are described by measurable
rules, not by human labels. "Built" means: it understands the **characteristics** of a sound (brightness, dullness, darkness, and a
defined set like them), for any sound or phrase, including ones it never heard. Emotion, genre and clusters are maps on top of
that space, by rule. No UI/UX now.

```
 inputs                          the shared space (core)                         maps on top (later)
 synth knobs + note ─┐     ┌─ characteristic set, each a math rule,      ┐     ┌─ instrument / sound clusters
 samples, loops      ├──▶  │  each measured as a curve over time:        ├──▶  ├─ emotion by rule (owner's rules)
 MIDI phrases        ┘     │  brightness, darkness, roughness, attack,   │     └─ make new sounds: set knobs/notes
                           │  loudness, pitch, chord tension, density ... ┘        to reach a target point
                           └─ datasets = reference points for structure, never emotion truth
```

## 2. The plan, one touch point per phase

| Phase | Work | Done when | Budget |
|---|---|---|---|
| P1 Characteristic set | Write the set (about 12-16), each with: name, the math rule, unit, how it is measured over time, and the literature it rests on (Timbre Toolbox, Peeters 2011; sharpness, Zwicker and Fastl; roughness, Sethares / Vassilakis; consonance and tension, Plomp-Levelt, Lerdahl). Timbre side (brightness = spectral centroid, darkness, dullness, warmth, roughness, noisiness, attack, decay, width, movement) and music side (pitch height, register spread, chord consonance / tension, harmonic rhythm, note density, contour). Opposites (bright / dark) are one axis, not two | the owner signs off the list | 1 session |
| P2 Measure everything | One tool: characteristics of any audio / mel / MIDI, as curves over time. Run it on what is held: Diva v2 mel rows (1,631), Diva v1 descriptors (3,873), sample features (195k), MIDI corpus. Table of which characteristic each source can give | coverage table in this file; repeat measurement identical | 1 session |
| P3 Understands | Forward model predicts the characteristic curves from knobs + notes, scored on held-out presets and phrases (`m1_forward.py` with characteristic targets instead of the 43 raw descriptors) | each characteristic R2 >= 0.6 on the group split, or a learning curve that says which data closes the gap (data contract, `SOUND-MODEL.md` 4b) | 1-2 sessions |
| P4 Shared across sources | A sound from Diva, Vital or a sample with similar characteristics lands in the same place; timbre and phrases share the axes | nearest neighbours across sources agree on the characteristic values within the repeat noise | 1 session |
| P5 Makes | Set knobs (and notes) to reach a target characteristic point; render; measure | rendered point within a set distance of the target, better than the closest existing preset | after P3 |
| P6 Maps | Clusters (instrument families) and the owner's emotion rules over the characteristic curves | owner's rules written; clusters checked against reference datasets | after P4 |

**The owner's ladder (2026-10-28).** Each level has its own structure and its own characteristics, and is built from the one under it:

```
data > interpretation > sound > cluster of sounds > instrument > cluster of instruments > mix > full song
 P2       P1 axes        P3/P5      P4 map            P6 families     P6 + arrangement      later    later
```

Owner's view of sound: every sound is mouldable into another cluster by knobs (pluck -> pad: longer attack and decay, more space;
pluck -> bass: lower pitch; chord -> melody: pick pitches; percussion = short high-transient sounds, high or low). So the model's job
is the knob combination that moves a sound between clusters; synth presets give the anchors (a kick, a bass, a pad), and every
knob is to be mapped onto Infinite's own synths and effects so each sound can be made in Infinite. The owner holds many more presets
on the drive: ask only when a curve says they make the model more robust.

**Map and learner (owner, 2026-10-28).** Presets of other synths (Diva, Omnisphere, ...) and the sample library are the **map**:
measured points of where good and real sounds live. Infinite's own nodes are the **learner**: rendered without limit with one-knob
sweeps, knob pairs and active learning, and for its own DSP the known math is a prior. Moves between clusters are generative (target
point minus current point; the model finds the knobs), never a fixed list. Real instruments come from engines Infinite already has,
on one line from synth to real:

```
 Analog / FM / Wavetable   ->   String (waveguide + modal body)   ->   Granular        ->   Sampler
 invented sounds                pluck = guitar, harp; bow = violin,    textures from        exact recordings,
                                cello; strike = piano-like, dulcimer   real audio           picked from the map
```

The model chooses the engine and its knobs for a target point: the most synth-like engine that reaches it within the repeat noise,
else the next one; a hybrid patch (a sampled attack from the library on a String or Analog body) covers the gap, as Omnisphere does
with sample layers on a synth engine. Labels for "piano", "guitar" come from sample and preset names and folders (D4). Order:
Analog sweeps, then String sweeps (target: the library's acoustic points), then hybrids.
Owner 2026-10-28: a piano model, a wind model and a better String model are wanted for this (design shown before building). No
preloaded sample libraries: an instrument ships as a compact learned parameter set (partial frequencies, levels and decays at a few
keys and velocities, plus a noise envelope; kilobytes, not gigabytes), measured from the library and played by Infinite's engines.

**Status 2026-10-28:** P1 approved by the owner (`CHARACTERISTICS.md`, 18 axes + space added). P2 done: every
held source measured, coverage and cross-form agreement in `CHARACTERISTICS.md`; brightness / sharpness agree 0.96 / 0.92 between mel
and band forms of the same render. P3 first pass (`p3_characteristics.py`, Diva v2 rows, C3, 1 seed): 0 of 11 characteristic
means at the 0.6 bar; median head R2 0.153 random, 0.082 group (best: attack 0.44, density 0.23 on group). Not the renders: repeat
r >= 0.97 on every axis, so the ceiling is about 1. Not one knob either: the strongest single knob explains |r| 0.28 of brightness
(VCF frequency) and 0.15 of warmth, so the sound comes from knob interactions. Presets alone add 0.04-0.07 per doubling (7+ doublings
to the bar). Diva pilot closed (EXECUTION Step 1): more presets reach 0.200 group and are flattening (+0.04 for the last doubling), one-knob moves on 1,000 bases 0.113; Diva stops for P3 and stays the map; the learner moves to Infinite's engines (`SOUND-MODEL.md` 4b).
P4 dry run (`p4_shared.py`, band form, 3,000 rows per source, 6 spectral axes): samples and Air cover each other (0.44 / 0.37 of rows
have a neighbour as close as their own median, self = 0.50); Diva and samples 0.26 / 0.39, up from 0.16 / 0.27 once loudness (a
per-source offset) is left out, the rest is content (samples hold drums and loops, much lighter low end). Plug-in renders form their
own island (coverage 0): effects measured on one broadband test signal, so they stay out of the instrument space.

**Step 1 closed 2026-10-28** (`SOUND-MODEL.md` 4b, pilot close-out): group median head R2 0.200 with D1 + D1b (~3,500 presets), 0.113
with D2w; 0 of 11 at the bar. Decision: the curve holds, Diva rendering stops for P3 and continues only as map rows for P4; the Step 2
sweep is 30% one-knob moves, 70% fresh base points.

G1 + G2 + Step 2 first batch closed 2026-10-28: `tools/universe/detail.py` measures 15 more axes (13 synthetic checks pass); `tools/universe/own_sweeps.py` renders Infinite's own engines to schema-3 rows (recipe, knobs, 8 windows C3/C4/C3f/C1/C5/C6/C3pp/C3mf, 128-band mel, top-64 partials, noise envelope, release tail, velocity response; designs base 30% / one 60% / pair 10%, bank = group). Notes go to input slot 0 (EXECUTION.md says 1; wrong). Release build, 4 shards, 0.4 s per preset (a debug build is 10x slower). Oscillator: 1,984 presets (13 swept, 9 fixed) in 773 s; `check` 20 recipes bit-identical, `repeat` r 1.00000. P3 (`ml-venv`, 1 seed): random split 8 / 11 means at 0.6, median head R2 0.624; **group split 0 / 11, median 0.192** (head: loudness.std 0.79, loudness.slope 0.80, attack 0.58, decay 0.56 pass or near; brightness, warmth, weight 0.1-0.36). Reading: sparse 13-knob space, 101 banks; Step 3 learning curve decides whether data or model.

G10 filled 2026-10-28: `tools/universe/wavetables.py` measured 21,558 tables (1,432 wt, 20,000 synthetic, 126 Vital) at 220 Hz reference: centroid, rolloff, odd/even, fundamental share, flatness, Sethares roughness, two spectral peaks; 5 synthetic checks pass -> `characteristics/wavetables.jsonl`. Open: the check against the node's 64 built-in tables (needs the node's table source).

Step 2 Analog + Step 3 first curves, 2026-10-28: Analog 1,965 presets (check 20 recipes bit-identical); C1, C2 frozen for Analog (`claims.py check` hashes match; C3 map set kept). P3 median head R2 by data fraction (1 seed):

| Engine | split | 0.25 | 0.5 | 1.0 | means at 0.6 (1.0) |
|---|---|---|---|---|---|
| Oscillator | random | 0.319 | 0.457 | 0.624 | 8 / 11 |
| Oscillator | group | 0.104 | 0.123 | 0.192 | 0 / 11 |
| Analog | random | 0.280 | 0.406 | 0.725 | 8 / 11 |
| Analog | group | 0.198 | 0.254 | 0.298 | 1 / 11 |

All four curves still rise with data and the k-NN baseline stays far below the head, so per the plan Step 4 chooses the next 2,000 (not more uniform draws). Group bar not met on either engine; recorded as a miss with its curve, bar unchanged. Analog renders cost ~0.65 s/preset (Oscillator 0.4).

Step-by-step execution from here: `EXECUTION.md` (start at Step 0). What the model claims and the numbers that prove it
(C1 axes, C2 ingredients, C3 good, C4 words, C5 moods and emotions, C6 creative, C7 makes): the claims registry in `EXECUTION.md`.

Data rule for every phase: measure everything held, fully and losslessly (section 3); learning curves decide only how many new renders to make.

## 3. Data map: every data kind, its job, and the gap that is filled (owner 2026-10-28)

Owner: the learning algorithm is only as good as its data; no lossy, missing or thin data. So every kind held is measured fully and
placed, and every gap below is filled (`EXECUTION.md` Step G). The learning-curve rule decides only how many *new renders* to make,
never whether to measure what is held.

Five jobs in the one shared space (the 22 axes, plus the audio embedding as the full fingerprint, below):

| Job | What it gives the model | Claims |
|---|---|---|
| teacher | ingredients -> sound, rendered without limit from Infinite's engines | C1, C2 |
| map | where real, good sounds live | C3, C7 |
| words | warm, glitchy, dark ... from names, tags, folders | C4 |
| structure | notes, chords, grooves, song form, genres | C6, C8 |
| mood | the owner's rules, with references beside them | C5 |

| Data | Held | Job | Gap filled by Step G |
|---|---|---|---|
| node params of our engines | `atlas/node_registry.json`, `--describe` | teacher | ranges per param (G1) |
| renders of our engines | made in Step 2 | teacher | lossless record: recipe + partials + velocities + release (G1) |
| presets (Diva, Omnisphere, Ableton, Logic, Bitwig ...) | `presets/`, `steam/`, `audio/diva_*` | map, words | - |
| audio samples | `features.jsonl` 182k, Air 12k | map | curves, partials, pitch from the drive (G3) |
| pitch, frequency, spectrum | measured (P2) | axes 2-7, 13 | finer: partials (G2) |
| wavetables | `wavetables/` 1,432 tables, 20k spectra | map (oscillator level) | measured on the axes (G10) |
| instruments (EXS, Spitfire, Omnisphere) | `atlas/instruments*.jsonl` 14k | map, cluster names | - |
| clusters, character matrix | `atlas/clusters.json`, `character_matrix.json` | checks, words | - |
| MIDI (pitch, velocity, chords) | `store/inbox.jsonl` 3.16M | structure | CCs, meters, tempo changes, orchestration, EMOPIA (G4) |
| chords, grooves, song arcs | `store/chords`, `grooves/`, `arcs/` | structure | mix data from Ableton sets (G8) |
| mood | `mood/song_mood.jsonl` 203k, owner's albums | mood reference | - |
| genres, subgenres | `atlas/genres.json` | structure | placed as regions + structure, claim C8 (G5) |
| node expressions, patch pieces | `store/expression_pieces` 1,271, `patch_pieces` 892 | teacher, level 2: chains | claim C9 (G6) |
| user behaviour (keep / undo / tweak) | local Prediction data | personal map | claim C10, local only (G7) |
| audio embedding | not made | full fingerprint beside the axes | claim C11 (G9) |

Musical data per level, all to be measured (G2, G4, G8):

| Level | Data |
|---|---|
| one sound | fundamental, partials (frequency, level, decay each), inharmonicity, odd / even balance, formants, spectral envelope, noise part, transient, ADSR, pitch envelope and glide, vibrato, tremolo, modulation, stereo width and phase, room tail, loudness, velocity response, tuning in cents |
| notes and music | velocity, microtiming, swing, duration and articulation, register, intervals, chords and voicing, key and mode, harmonic rhythm, contour, rhythm pattern, meter, tempo, CC automation, instrumentation, song form |
| mix | level balance, panning, EQ masking, compression and crest factor, loudness (LUFS), effect chain order |

**Audio embedding: where it sits.** The 22 axes are what the model can explain; they are not the whole sound. An embedding (a
pretrained audio network's fingerprint) holds the rest. Jobs: (1) gap finder: predict the embedding from the 22 axes; what the axes
cannot explain points to the next missing characteristic (C11); (2) nearest neighbours across sources for P4 and start points for
P5; (3) Frechet Audio Distance (Kilgour 2019) for whole-set checks (C3, C6); (4) a text-audio embedding (CLAP) as a second view of
words (C4). It never replaces the axes: the axes stay the understanding, the embedding is the check that nothing is lost. A neural
codec (EnCodec / DAC) can also store any audio near-losslessly in little space. Model download: owner's yes first.

## 4. Rules that keep the project honest

- Everything held is measured fully (section 3). New renders need a learning curve or error diagnosis that names them, their budget and their proof (`SOUND-MODEL.md` 4b).
- Each phase ends at its bar or its budget, whichever comes first. Write the number into this file, then stop.
- One heavy job on one machine (the Pro). The Air is parked; its Diva shards stalled for an unknown reason (2026-10-28).
- Builds only via `scripts/ibuild.py`. New nodes, panels or downloads need the owner's yes. No Claude attribution in commits.
  Chat corpora are never pushed. ASCII diagrams and tables, no Mermaid.

## 5. Where things are

| What | Where |
|---|---|
| Plan, milestones, bars, data contract | `docs/plans/data-universe/SOUND-MODEL.md` (sections 4, 4b) |
| Diva rows v2 (knobs + mel + desc, 3 notes) | `~/Infinite/training-data/audio/diva_m1.jsonl` (Pro + Air merged, 1,631 presets) |
| Diva rows v1 (desc only) | `~/Infinite/training-data/audio/preset_diva.jsonl` (3,873 presets) |
| Sample and loop features | `~/Infinite/training-data/audio/features.jsonl` (195k rows) |
| Loader, eval, M1 | `tools/universe/sound_data.py`, `sound_eval.py`, `m1_forward.py` (`--frac` for learning curves) |
| M1 learning-curve results | `~/Infinite/training-data/models/m1_diva_lc/` |
| Renderer (fast, batched) | `tools/universe/render_pass.py` (`render_states`), `preset_state.py`; Pro script `~/Infinite/scratch_omni/diva_v3.sh` |
| Torch venv | `~/Infinite/ml-venv` (CPU) |

## 6. Owner's answers (survey, 2026-10-28)

1. What the sound model is for: "understand sound at an intelligent level", meaning the context of mood, genre, frequency, instrument,
   knob data and every other data point together, "and hence have the ability to make new sounds". So **making new sounds is the
   goal and understanding is how**: one shared space for all those data kinds. Road A (pick) is a step on the way, not the end.
2. First proof: none of the user-facing ones (edit by words, text to sound, recreate, blend). Words are the model's *internal*
   understanding, not a feature. If the model understands words and sounds together, it can cluster sounds and instruments, give a
   robust algorithm, and map everything to emotions. No UI/UX work now; the owner designs that once the model exists. So the proofs
   are internal: held-out scores on the shared space (clusters, instruments, emotions), not demos.
3. Emotion ground truth: not human labels. Public datasets and industry songs carry human *perception* (memory, nostalgia, culture),
   which shifts as cultures and sub-cultures change, so it is not trustable as truth. Music is a mathematical entity: describe it by
   scientific rules (loudness, brightness, frequency, pitches, chords, and how each changes over time). Datasets are reference points
   for how structure looks (a melody, a chord, a strong form), not emotion labels. So the model's core is a measurable description
   space; emotion is a rule-based map on top of it, never a learned label.
4. Timbre and notes: **one shared space**. A sound's timbre and a phrase's pitches, chords and changes over time are one vector,
   so a pad and a chord progression can be compared and matched. The note-model track (MIDI, B1-B10) and the sound track join.
5. Built when: it understands the characteristics of a sound (brightness, dullness, darkness, ...), with a defined set of
   characteristics for this purpose. Not the other offered tests (reach a target, predict held-out, cross-synth, clusters) as the
   definition; those stay as the checks of each phase.

## Step 5 — sound_space.py (critic, novelty, clusters), 2026-10-28
`tools/universe/sound_space.py` (numpy only; no sklearn on this Mac). Features = the 11 axes shared by Diva rows and samples (loudness … density), standardised on the map.
- **C3 critic** (logistic, map vs random, frozen C3 sets, Oscillator renders as the random side): train map 5,252 / random 483, test map 1,370 (393 Diva + 977 samples) vs random 118 → **AUC 0.924 (bar 0.8) — PASS**; loudness alone 0.802, so the critic is not only a loudness gate. Owner-agreement half of C3 (30/40) still open. Only 118 test randoms until more engines are frozen.
- **Novelty N** (distance to nearest map row / median map nearest-neighbour distance, capped, 0-1): Oscillator base 0.883, one 0.874, pair 0.864; **R** (axes outside the map 5–95% band) 0.16 / 0.16 / 0.14. Random Oscillator patches sit far from the map on these axes; the N<0.1 at c=0 half of C6 needs the generator, not this file.
- **Clusters**: `clusters --k 30` names regions from preset/sample words (drum loops, pads, one-shots …); table printed on demand, not stored.
Still open in Step 5: C4 words, wavetable reach of the 64 built-in tables (needs the node's table source).

### G10 check — the node's 64 built-in tables vs the 21,558-table library region (2026-10-28)
Dumped the built-in tables' first 64 harmonics per frame with a 20-line C++ program linked to `src/audio/Wavetable.cpp` (harmonics by DFT of frame mip 0), measured with `wavetables.table()`. On five shared measures (centroid, odd/even, flatness, roughness, fundamental share; z-scored on the library): a library table lies within 1x the library's own median nearest-neighbour distance of a built-in table for **0.3%**, within 3x for **10.0%**, within 5x for **35.3%**. The built-ins span the extremes (centroid 222–8,486 Hz, odd/even up to 118 dB) but the library's bulk (centroid 264–1,612 Hz, 5–95%) is thinly covered: the 64 are wide and sparse, the library is dense in the middle. Read: a map-to-Wavetable reach test (Step 6) needs more than the 64 tables, or the node needs a user-loadable bank.

## Step 4 round r01 — Analog (2026-10-28)
Model-chosen 2,000 renders (3-seed disagreement over 20,000 candidates; one / pair / fresh thirds + 200 uniform) rendered in 953 s, 0 failed. `round_score.py` (stored prediction vs measured, median R2 of the means): propose/one **0.768**, propose/pair **0.633**, propose/fresh 0.325, uniform/fresh 0.356. The model predicts moves from known points well and fresh points no better than before: the new data goes where it was uncertain. P3 after the round (3,963 presets): random 8/11 at the bar, head median **0.751** (was 0.725); **group 7/11 at the bar, head median 0.686 (was 0.298, 1/11)**, via mel 0.409, k-NN 0.317. The group split went from a miss to near the bar in one round, so the Step 4 stop rule (two rounds < 0.02 gain) is far off: more rounds are justified.

## Step 4 round r01 — Oscillator (2026-10-28)
2,000 model-chosen renders, 0 failed, 1,122 s. Stored prediction vs measured (median R2 of means): one 0.584, pair 0.505, fresh 0.280, uniform 0.420. P3 after (3,982 presets): random 6/11 at the bar, head 0.616 (was 0.457); **group 4/11, head 0.551 (was 0/11, 0.192)**. Same pattern as Analog: one round moves the group split most; stop rule far off, round 2 (with 4–12 knob moves) queued.

## Claims frozen for the four new engines (2026-10-28, after the first 2,000 renders each)

Wavetable, FM, Metallic, String: first batch 2,000 presets each, 0 failed (854, 880, 717, 728 s). C1 group sets, C2 moves and C3 random sets frozen (C3 keys `random:own_<engine>.jsonl`); `claims.py check`: all hashes match. Frozen before any model was trained on them.

## Step 7 first look and a reach bug (2026-10-28)

**String vs acoustic families** (`string_families.py --n 1900`, equal render count, ratio = distance to nearest render / family's own inner distance, 11 axes, random knobs):

| engine | guitar | harp | pluck | violin | cello | piano |
|---|---|---|---|---|---|---|
| String | 7.02 | 5.46 | 5.53 | 7.11 | 4.30 | 6.90 |
| Analog | 4.78 | 3.48 | 3.27 | 4.98 | 2.79 | 4.48 |
| Oscillator | 3.88 | 2.90 | 2.72 | 3.99 | 2.84 | 3.63 |
| Wavetable (1,525) | 5.18 | 3.75 | 3.67 | 5.15 | 3.45 | 4.88 |
| FM | 5.99 | 4.89 | 4.48 | 6.21 | 3.43 | 6.08 |
| Metallic | 5.56 | 4.76 | 4.26 | 7.58 | 4.36 | 5.03 |

Reading: no engine lands inside any family at random knobs (all ratios > 1, 0% within inner distance), and the dedicated String engine is the *farthest* from real strings, Oscillator the nearest. Caveat: random knobs cover the engine's whole space uniformly, mostly not string-like; this does not yet say String cannot reach them. The reach test (steering to a target) is the fair one.

**Reach reach1 was invalid:** silent axes are NaN in the characteristic vectors, so every start distance was NaN and "closer than nearest" printed 0.0% (Analog and Oscillator). Fixed in `reach.py` (NaN filled with the engine's column median, stored in the queue). The reach1 renders (287 Analog, 294 Oscillator) stay as training rows. Rerun as `reach2` in driver5 after the library measuring finishes, then Step 4 rounds and embeddings.

## G5 / C8 first pass: genre from the sample library's axes is at chance (2026-10-28)

`genre_regions.py`: rows labelled by genre words in pack / file name (reference, not truth), 11 shared axes + tempo, softmax regression, leave-packs-out (every pack held out once). 16,978 rows, 11 genres, 38 packs: per-genre mean top-3 **0.274** against chance 0.273 (bar 0.7); top-1 0.107 vs 0.091. **C8 as measured here: MISS (no signal).** First attempt with one held-out fifth had only 5 test packs, mostly house, and was not usable.

Why (reading, not yet tested): a pack's single loops and one-shots (kicks, hats, pads) carry the pack's character in the *combination* and the *structure* (tempo, groove, chords, form), not in each file's tone; names are a noisy label. Next tests: (a) genre from MIDI drum grooves + chords + tempo (`midi_drum_groove`, `midi_chord_prog` rows with a genre word in the path, atlas evidence lists counts per genre); (b) full tracks with a genre label (needs data from the owner, below).

## Asks for the owner (things that would improve the model)

| Ask | Why it helps | Claim |
|---|---|---|
| Listen to 40 pairs (critic picks vs not), say agree / disagree | C3 owner-agreement bar 30/40 is the one open half of C3 | C3 |
| A folder of full tracks you consider good examples per genre (even 10 per genre, any length) | genre needs track-level audio; sample packs gave chance | C8 |
| Ableton sets / projects you actually like (G8 reads track levels, pans, sends, device chains) | mix and chain data from real use | G8, C9 |
| Patches (.inf) you made and kept vs discarded | the best signal of taste: what a user keeps, undoes, tweaks stays local | C10 |

## C8 on structure: drum grooves carry some genre (2026-10-28)

`genre_groove.py`: Groove MIDI (store/public_drums.jsonl), 8 lanes x 16 steps + lane densities + log tempo + swing, softmax, leave-drummer-out (only 4 drummers). 3,790 grooves, 12 families: per-family mean **top-1 0.184, top-3 0.394** (chance 0.083 / 0.250; bar 0.7) -> above chance, below the bar. Best: rock 0.78, dance 0.73, funk 0.69, hiphop 0.58; worst: punk 0.00, latin 0.04, afrobeat 0.10. Limits: four drummers (the test is mostly 'a new drummer'), family from file names, grooves alone. Next: add chord progression and bass rows of the same genre words (midi_chord_prog, midi_bass_seq) and the 22-axis description of full tracks; both are in the owner asks above.

## C2 (knows what each ingredient does) on Analog and Oscillator: MISS (2026-10-28)

`c2_predict.py`: 3 forward models trained only on rows outside the held-out banks; predicted f(variant) - f(base) for every frozen C2 move, stored with `claims.py predict` before scoring.

| engine | moves | direction right (bar 90%) | size r (bar 0.7) | verdict | exploratory: moves >= 0.5 sd (post hoc, NOT the frozen bar) |
|---|---|---|---|---|---|
| Analog, all | 1,412 | 63.5% | 0.635 | MISS | 188 moves: 75.5%, r 0.707 |
| Oscillator, all | 1,276 | 61.8% | 0.549 | MISS | 103 moves: 84.5%, r 0.614 |

Reading: most axis changes from a 1-3 knob move are near zero, and the sign of noise is a coin flip, so 'direction right' is dragged to ~60%. Big moves are predicted with the right sign 75-85% of the time. The claim as frozen stays MISS; no wording about 'knows what each knob does' is allowed yet. Next: more rounds (data), ensemble size, score only the axes the activity map says the knob drives.

## G3 sample library, features_v3 (2026-10-28)

`samples_v3.py run --budget 30`: 11,316 rows in ~29 min. Repeat check: 50/50 identical. Pitch class from the audio vs the pitch in the file name: **462/691 = 66.9% (bar 90) -> below the bar.** The pitch feature stays out of any claim until the mismatches are split into 'file name wrong' and 'estimator wrong' (octave errors, percussive and noisy sounds have no pitch). Next: list the 229 mismatches by instrument family.

Pitch mismatches split (691 named files): bass 81%, guitar 94% (18 files), synth 68% (432), other 56% (158), piano 33% (6 files). Mismatch intervals mod 12 are spread (5 st: 35, 7 st: 31, 3 st: 28, 2 st: 23, 9 st: 22), with fifths/fourths the largest, which is the signature of the estimator locking onto the 3rd/4th harmonic, not of wrong file names alone. Decision: pitch from audio is NOT used where the file name gives the note; the audio estimator needs a harmonic-product fix before it is trusted for synths. Not fixed yet.

**Correction to the two paragraphs above (same day):** the 66.9% mixed two kinds of file. Files named with note AND octave (single notes): **283/289 = 97.9% agree (bar 90 -> PASS)**; the 6 misses are 8 octave errors (-12 st) in an f0 spread of 289. Files named with a pitch class only (402; 179 agree = 44.5%) are loop keys and arpeggio/chord labels ('Arp E G2', '..._G_125BPM'), not single pitches, so file name is not ground truth there. The harmonic-lock explanation and the 'estimator needs a fix' decision above are withdrawn. Change to the check: `cmd_check` must score only `name_midi` rows. 

### C2 diagnosis: where the misses are (2026-10-28, per-axis, moves with |change| > 0.3 sd)

Direction right on clearly-changed axes vs all non-zero changes. Analog: sharpness 88%, decay 87%, attack 95%, noisiness 84%, brightness 82%, loudness 83% (r 0.86 for loudness, attack, decay), fullness only 69% (r 0.48), movement 71%. Oscillator: sharpness 97%, decay 85%, brightness 78%, loudness 69%; fullness 62% (r 0.26), noisiness 74% (r 0.38), attack 64%. So (1) the weak axes are fullness, noisiness and movement, in both engines: the 11-axis description may not hold a clean single direction for them; (2) the frozen scorer counts every non-zero change, including ones of a few thousandths of an sd, where the sign is not predictable by any model. The 90% bar over all such changes is close to unreachable by construction; I keep the frozen bar and the MISS, and report the 0.3-0.5 sd view as exploratory only. Next: more training data from Step 4 rounds (every round adds 1-3 knob moves) and re-score C2 after r02, same frozen moves.

## G7/reach2 Analog (first valid reach result, NaN fix applied)

300 sample-library targets (from the 11 axes + distribution columns), one chosen point per target from 150 model-ranked candidates (3 seeds). Median distance in sd units: nearest existing Analog render **3.43**, predicted for the chosen point 3.65, **measured 4.20**; chosen point closer than the nearest existing render for **13.7%** of targets. Reading: the planner picked from too few candidates (150 per target) to beat the 4,250 renders already there, and the model is optimistic by 0.55 sd. reach.py now has `--refine G` (model-only hill-climb: best 8, 120 children per generation). Next: reach3 on Analog with `--refine 6`; if the measured distance still sits above the nearest-render baseline the model cannot steer toward samples and that stays a stated limit, not a claim.

### C2 with a derivative-informed loss (Analog, lam 1): no gain

`c2_delta.py` (descriptor-only model, extra loss on f(a)-f(b) over 6,629 training pairs differing in <= 3 knobs): direction right **63.9%** (was 63.5%), size r **0.626** (was 0.635), 1,412 moves -> MISS; moves >= 0.5 sd: 78.2%, r 0.716 (was 75.5%, 0.707). Within noise of the plain model. Reading: with this much data the difference loss adds nothing at weight 1; testing lam 10 next. If that is also flat, the limit is data on the weak axes (fullness, noisiness, movement) or the axes themselves (P1/P3 in SECOND-OPINION.md), not the training loss.

C2 delta-loss lam 10 (Analog): direction right **61.8%**, size r **0.549** (worse than plain 63.5% / 0.635 and lam 1 63.9% / 0.626); moves >= 0.5 sd 70.7%, r 0.640. Conclusion: the difference loss does not help here at either weight. The limit is not the training objective.

### reach2 Analog by target difficulty (out-of-gamut view)

| target tertile (distance to nearest existing Analog render) | start (sd) | measured pick (sd) | closer than nearest |
|---|---|---|---|
| near | 2.08 | 2.56 | 11.0% |
| middle | 3.43 | 4.15 | 14.0% |
| far | 6.17 | 7.14 | 16.0% |

Correlation of start distance with measured distance 0.89. Two findings: (1) the target's distance to the engine's own renders is an excellent out-of-gamut warning: far targets (> ~5 sd) end ~7 sd away whatever the search does; `reach.py score` now prints this table. (2) In every tertile the model-picked point is ~0.5-1.0 sd WORSE than simply returning the nearest render that already exists. Plain nearest-neighbour retrieval is the baseline reach has not yet beaten; reach3 (pessimistic hill-climb) is the test, and a gain must be reported against this baseline, not against zero.

### reach2 Oscillator (same method, 150 candidates per target)

300 targets: nearest existing render **2.03** sd, predicted 2.01, **measured 2.57**; closer than nearest for **7.0%**. By tertile (near / middle / far): start 1.47 / 2.03 / 3.16, measured 1.80 / 2.45 / 4.05, closer 5% / 9% / 7%. Correlation of start distance with measured distance 0.77. Same picture as Analog: the model-picked point is worse than the nearest existing render, and the target's distance to the engine's own renders predicts the outcome. Oscillator's renders cover the target space better (median nearest 2.03 vs 3.43).

### C2 delta-loss with near-zero pairs filtered (reviewer hypothesis) - Analog
`c2_delta.py --lam 1 --min-change 0.3` (drop training pairs whose largest axis change is < 0.3 sd): direction 63.0%, size r 0.609 (bar 90 / 0.7) -> MISS. Plain model 63.5% / 0.635; lam1 63.9% / 0.626. So neither the difference loss nor filtering the noisy pairs helps; the gap is not a derivative-fit problem. At >= 0.5 sd (188 moves) direction is 73.9%, r 0.694, consistent with the C2.1 effect-size-gated reading.

### Out-of-gamut advisory (reach.py score, on reach2)
Fit: expected landing distance = a * (distance of target to nearest existing render) + b. Oscillator 1.12x + 0.40 (resid sd 1.07); Analog 1.16x + 0.27 (resid sd 1.40).
Targets the fit advises "reachable within 2.5 sd" land within 2.5 sd: Oscillator 84% (vs 24% of the rest), Analog 86% (vs 6% of the rest). At 1.5 sd: Oscillator 80% vs 6%, Analog 67% vs 1%. So the pre-render advice separates reachable from unreachable targets; it can be quoted in the app before any render. (Fit and check use the same targets: in-sample; a held-out target split is still to do.)
Held-out half (fit on one half of the targets, judged on the other): "within 2.5 sd" advice holds - Oscillator 88% landed within vs 27% of the rest; Analog 94% vs 7%. The 1.5 sd advice has too few advised targets (1-3%) to judge. Use the 2.5 sd line.

### Weak-axis sign-flip diagnostic (c2_signflip.py, Analog, 1,612 one-knob bank pairs, report only)
Mean over knobs of "share of pairs where the knob pushes the axis the same way" (1.0 = always, 0.5 = coin flip), moves >= 0.5 sd: fullness 0.62, warmth 0.66, decay 0.68, movement 0.69, attack 0.70, noisiness 0.71, density 0.71, weight 0.72, sharpness 0.78, loudness 0.81, brightness 0.81.
Reading: fullness is the least consistent axis, which fits the "composite" idea, but decay (0.68) is just as context-dependent and is predicted well (88-97% direction), so context-dependence alone does not explain the weak C2 axes; the model learns context. Movement and noisiness are mid-table. The composite hypothesis is only supported for fullness and warmth. No change to axes or claims.

### G4 gate: MIDI fully extracted (counts and hand check, 2026-10-28)
`store/midi_full.jsonl`: 294,571 rows over 21 corpora; 293,573 parsed, 998 unparseable (error rows, nothing to extract).
Per field (of the parsed): with CCs 207,879 | more than one tempo 71,576 | first meter not 4/4 39,459 | meter changes 45,233 | more than one instrument family 179,391 | EMOPIA emotion label 1,078 of 1,078.
Hand check: 20 random files re-parsed by an independent byte-level reader: note counts and CC counts equal in 20 of 20, meter messages consistent with the stored meters.
G4 closed. Raw MIDI `datasets/midi/data` (8.6 GB, the extracted copy) may be removed once `archives/` (2.3 GB, the original downloads) is on Drive and the owner says yes.

### C2 on Wavetable (untouched engine, pre-registered; first of the four)
| set | moves | strict C2 (bar 90 % / r 0.7) | C2.1: moves >= 0.5 sd |
|---|---|---|---|
| single (100) | 346 axis moves | 55.8 %, r -0.470 -> MISS | 22 moves: 45.5 %, r -0.638 -> MISS |
| combined (60) | 355 axis moves | 53.8 %, r 0.549 -> MISS | 42 moves: 85.7 %, r 0.673 -> MISS (close; n small) |
Reading: Wavetable has 65 swept knobs, so a single-knob step rarely changes anything audible (only 22 of 346 single moves reach 0.5 sd), and those few are predicted no better than chance. Combined moves reach 85.7 % when audible. Reported as a MISS on both tiers; no rescoring.

### Human-patch benchmark: Oscillator (owner's 37 distinct settings from ~/Downloads/Patches vs 836 held-out bank rows; same ensemble, same metric)
| axis | bank R2 | human R2 |
|---|---|---|
| loudness | 0.68 | 0.78 |
| brightness | 0.73 | 0.22 |
| sharpness | 0.75 | -0.46 |
| warmth | 0.57 | 0.16 |
| weight | 0.60 | 0.19 |
| fullness | 0.38 | 0.31 |
| noisiness | 0.40 | 0.30 |
| movement | 0.46 | 0.57 |
| attack | 0.70 | 0.38 |
| decay | 0.60 | 0.33 |
| density | 0.35 | -0.11 |
| **mean** | **0.565** | **0.243** |
Error in bank spreads: bank 0.67, human 0.53 (the human patches' absolute error is not larger). The human patches vary much less than the bank on most axes (spread vs bank: sharpness 0.41, warmth 0.29, weight 0.37, movement 0.38, brightness 0.60), so R2 against their own mean is a harsh yardstick there; fullness 1.02, decay 1.14, density 0.95 vary as much as the bank.
Reading: a same-generator gap exists in R2 terms (0.565 -> 0.243: P3 bar 0.6 would be missed on human settings) but it is mostly resolution, not blindness: the model does not separate the owner's settings from each other on brightness, sharpness, warmth, weight. Loudness and movement hold up. Caveats: static knobs only (no cables), a few non-swept knobs differ, 37 points, Oscillator only so far.

### C2 FM result (2026-10-28)
| tier | axis moves | direction right (bar 90) | size r (bar 0.7) | verdict |
|---|---|---|---|---|
| single moves | 627 | 65.7% | 0.666 | MISS |
| combined moves | 627 | 71.0% | 0.688 | MISS |
Exploratory (|change| >= 0.5 sd): single 60 moves, 85.0% / r 0.704; combined 104 moves, 87.5% / r 0.724. Closest of the engines to the bar; the pre-registered C2.1 effect-size tier is still under 90% direction.

### C2 Metallic result (2026-10-28)
| tier | axis moves | direction right (bar 90) | size r (bar 0.7) | verdict |
|---|---|---|---|---|
| single moves | 760 | 62.8% | 0.178 | MISS |
| combined moves | 313 | 77.6% | 0.965 | MISS (direction) |
Exploratory (|change| >= 0.5 sd): single 48 moves, 83.3% / r 0.194; combined 25 moves, 96.0% / r 0.969 (would clear the effect-size tier on the combined moves, but only 25 moves). Pre-registered C2.1 tier is scored by claims.py; check its line before quoting.

### Human-patch benchmark: Analog (2026-10-28)
Held-out bank vs the owner's Analog patches (same ensemble, trained outside the held-out banks).
| axis | bank R2 | human R2 |
|---|---|---|
| loudness | 0.75 | 0.31 |
| brightness | 0.77 | -0.03 |
| sharpness | 0.74 | -0.86 |
| warmth | 0.70 | -3.78 |
| weight | 0.75 | -1.08 |
| fullness | 0.53 | -0.05 |
| noisiness | 0.49 | 0.39 |
| movement | 0.67 | 0.50 |
| attack | 0.84 | 0.64 |
| decay | 0.74 | 0.57 |
| density | 0.67 | 0.01 |
Mean R2: bank 0.696, human -0.306. Error in bank spreads: bank 0.54, human 0.52, i.e. the absolute error on the owner's patches equals the held-out bank's.
Why R2 collapses: the owner's Analog patches span only a sliver of the bank on warmth (0.11 of the bank sd) and weight (0.08), so R2 divides by a tiny variance. Read the error-in-spreads column, not R2, for this engine. The model cannot rank the owner's patches against each other on warmth/weight (resolution), but it is no less accurate in absolute terms.

### C2 String result (2026-10-28)
| tier | axis moves | direction right (bar 90) | size r (bar 0.7) | verdict |
|---|---|---|---|---|
| single moves | 893 | 73.6% | 0.604 | MISS |
| combined moves | 414 | 81.9% | 0.453 | MISS |
Exploratory (|change| >= 0.5 sd): single 114 moves, 81.6% / r 0.631; combined 33 moves, 81.8% / r 0.643.

### C2 across all six engines (single / combined; direction %, size r)
| engine | single | combined | verdict |
|---|---|---|---|
| Analog, Oscillator | see earlier entries | | MISS |
| Wavetable | 55.8 / -0.47 | 53.8 / 0.55 | MISS |
| FM | 65.7 / 0.67 | 71.0 / 0.69 | MISS |
| Metallic | 62.8 / 0.18 | 77.6 / 0.97 | MISS (direction) |
| String | 73.6 / 0.60 | 81.9 / 0.45 | MISS |
No engine reaches 90% direction. C2 stays a MISS in the claims freeze.

### Preset anchors from existing Diva measurements (2026-10-28)
`tools/universe/preset_anchors.py`: k-means in standardized axis space over `characteristics/preset_diva_v1.jsonl` (18,907 note-renders, 10 axes; `movement` dropped, <85% coverage), one medoid per cluster. k=12 gives anchors from distinct families (drones/FX, chord leads, keys, basses, percussion, hi-hats), each a real human preset with its source path; output `training-data/own/anchors_diva.json`. These are audio-space anchors (route A): use as reach targets / coverage checks, no knob mapping. Same recipe applies to the other engines once rendered (Vital next: open JSON).

### Random-control A/B for active learning, Analog (2026-10-28)
Same base data plus the same number of extra renders, either the model-chosen points or random points.
| arm | group means at the bar | median R2 head | median R2 via mel | k-NN |
|---|---|---|---|---|
| chosen | 7 / 11 | 0.731 | 0.493 | -0.119 |
| random | 10 / 11 | 0.763 | 0.516 | -0.115 |
Random beat chosen on every column. The active-learning pick did not help on this engine; do not claim the chosen-points gain (Step 4 rounds). Remaining to check from the render files: failed/silent rate and extreme-knob share of the chosen set (a pick that crowds the edges can hurt held-out folders). Single run, one seed per arm: treat the 0.03 head gap as a direction, not a size.
Control follow-up: each arm is ~2,000 distinct settings (about 8 note rows each). Extreme-knob share (inside 5% of the range edge): chosen 0.257, random 0.241. Silent renders (peak < 1e-3): chosen 0.1%, random 0.2%. So the chosen set is not crowding the edges or producing silence; the arms differ only in where the points sit, and random was at least as good.

## reach3 Analog (refine + LCB, 2026-10-28)
300 targets, same bar as reach2 (P5: closer than the nearest existing render). Median distance in sd units: nearest existing render **3.02**, predicted **3.27**, measured **3.96**; closer than nearest **9.3%** (reach2: 3.43 / 4.20). **MISS.**
| targets | start dist | measured | closer than nearest |
|---|---|---|---|
| near (n=100) | 1.88 | 2.49 | 6.0% |
| middle | 3.02 | 3.83 | 10.0% |
| far | 5.75 | 7.09 | 12.0% |
Start distance vs measured distance r 0.85. The model-guided pick lands about 0.9 sd farther than simply taking the nearest render in 90.7% of targets: the predicted gain is not real (optimizer's curse again, now with LCB and model-only refine). Seed-spread calibration line did not print (no stored `pred_spread` in this plan file); the plan ran on the pre-spread code path, so that check is still open.
Out-of-gamut advisory fit again: expected landing = 1.22 x gamut distance + 0.36 (resid sd 1.67). Advised within 2.5 sd: 12% of targets, 76% of those land within 2.5 sd vs 9% of the rest (held-out half: 89% vs 13%). Advisory works; steering does not.
Reading for the claims: do not claim "reach beats retrieval" on Analog. What is supported: knowing before rendering which targets are reachable.

## reach1 String (Step 6/7, 2026-10-28)
300 Diva map targets, plain plan (no `--refine`; so no seed-spread calibration), 295 new String renders, 0 failed. Median distance in sd units: nearest existing String render **4.52**, predicted **4.48**, measured **5.02**; closer than nearest **17.3%**. **MISS** (bar: closer than the nearest existing render).
| targets | start dist | measured | closer than nearest |
|---|---|---|---|
| near (n=100) | 3.28 | 3.65 | 20.0% |
| middle | 4.52 | 4.97 | 16.0% |
| far | 6.50 | 7.03 | 16.0% |
Start distance vs measured r 0.94. Advisory fit: expected landing = 1.11 x gamut distance + 0.12 (resid sd 1.11). Only 2% of targets are advised reachable within 2.5 sd (all of those do land within it; 3% of the rest do): String's renders sit far from almost all map sounds, so the advisory has nothing to select. Compared with Analog (nearest 3.02, 12% advised) String is the least reachable engine for map sounds, consistent with the family table (String farthest from real strings).
Step 7 reading: steering does not close the gap. The dedicated String engine reaches map sounds worse than Analog or Oscillator; the open design question (better String, piano or wind model, hybrid attack) stands and goes to the owner as a decision, not a build.
Scorer fix: `reach.py score` crashed when a plan has no `pred_spread` (None in an object array); now treated as missing.

## Determinism of the six own engines (2026-10-28)
`tools/universe/det_engines.py`: 20 random stored recipes per engine rendered twice in separate batches, compared sample by sample on the rendered window. **Wavetable, FM, Metallic, String, Analog, Oscillator: 20/20 bit-identical, worst relative difference 0.** Renders are deterministic, so repeat-noise is zero and any C2 miss is model error, not render noise. (Build: Desktop Infinite.app 0.6.6.)

## Vital / plug-in presets: blocked on the build (2026-10-28)
Plug-ins installed here: Vital, Serum 2, Diva, Omnisphere, Phase Plant (AU and VST3 for most). `~/Music/Vital` holds 75 `.vital` files (the 800 Vital records in `presets/` came from the Air). `plugin_states.py` needs the headless `--plugin-state` flag, which is in source on main but in no release (latest is v0.6.6); the only binary on this Mac is the Desktop copy at 0.6.6 (no `build/` or `build-dev/` exists), so the Vital state template comes back `ready: false`. To render Vital / Serum 2 / Hive / Repro presets, a build of current main is needed (`scripts/ibuild.py build`, release or dev; heavy on this 8 GB Mac, run when nothing else is). Diva presets are already measured (18,907 rows), so the Diva anchors stand.

### C3 owner listening test built, 2026-10-28 (`tools/universe/listen_c3.py`)

`pick` scores the frozen C3 held-out sample files with the critic and takes the top 20 and bottom 20; `build` cuts 6 s clips, loudness-matches them (the critic's strongest single axis is loudness, 0.80 AUC alone, so the owner judges the sound, not the volume), shuffles with a fixed seed and writes `eval/listen/c3/index.html` (blind, Good / Not good per clip) plus the key `eval/listen/c3_key.json`; `score FILE` compares the saved answers with the critic's side. Bar: agree on >= 30 of 40.

Decision made before any listening: the picks are real sample files only (top 20 score 9.6..14.3, bottom 20 score -9.0..-4.0). Taking them from the whole test set put 20 samples on top and 11 synth renders in the bottom, so the owner could agree just by hearing sample versus synth. Synth renders are not in this test.

Owner answers: Anchors and recipes: the owner says the 40 anchors were AI generated, while the presets inside each plug-in and the Patches folder are human made (the 40 recipes' source is now answered).

**C3 owner listening result, 2026-10-28: 24 of 40 agree (bar 30) - MISS.** Owner's answers kept at `eval/listen/c3_answers_owner_2026-10-28.json`. Top 20 picks: owner called 18 good (so the critic's praise holds). Bottom 20 picks: owner called only 6 not good; 14 of the critic's worst real samples sounded fine to the owner. The 14 are mostly hi-hats, percussion hits and short one-shots (hihats, ride, crash, stick hit) plus a few tonal ones. Reading: the critic ranks short, quiet, sparse or high-band sounds as bad because its 22 axes and the "good" training set lean to long, full sounds; the owner judges them as good sounds of their kind. The next step is not a threshold change: the critic needs a per-part view (compare a hi-hat with hi-hats), which is a model change to be tested on a fresh frozen set, and the miss stays on record.

**Raw MIDI vs Drive, 2026-10-28:** `datasets/midi/data` 313,963 files, 8.55 GB. 19 of 22 folders matched Drive by file count and bytes in `Infinite-Cloud-Training/training-data/inbox`. The other three were missing or partial: emopia_piano_emotions (Drive had 1,090 of 2,780), vgmidi_game_tracks (204 of 247), music_fundamentals (0 of 4). Added only the missing files (no overwrite) under `_datasets_extra/data`; the mount now matches the local counts and bytes for all three. The Drive API did not yet list a new file, so Drive for desktop was still uploading when checked. Nothing deleted.

**Model-sound listening test built, 2026-10-28 (`tools/universe/listen_model.py`):** 10 Analog sounds the model proposed (reach3 plan items) against 10 uniform-random Analog sounds, rendered with the 0.6.6 binary, loudness-matched, shuffled, blind; page at `eval/listen/model_m1/index.html`, key `model_m1_key.json`. `score FILE --tag m1` prints the share called Good per side and their gap. Repeat on each later model (`--tag m2`). It is a small, exploratory taste check (n=10 per side), not a frozen claim.

**Raw MIDI delete, 2026-10-28: NOT done, owner said delete the synced ones.** The Mac's Drive mount mirrors the local counts for all folders, but the Drive web API shows only part of it in the cloud: Rock-Piano-04414.mid is in the cloud, while `R_214_ASAP.mid`, `CHEN04.mid`, `Early Bird Satellite.mid` and a `lakh_midi_full` file were not found, and the cloud folder `lakh_midi_full/9` lists no children. Mount presence is not cloud proof, so no folder was deleted. Drive for desktop is running; re-check once its upload finishes.

**Owner verdict on the first model-sound test, 2026-10-28: the test was wrong.** All answers were "not good": the clips were a bare engine playing one held note with no note model and no effects, which sounds like noise or a plain oscillator. A synth sounds good or human when note data (a real phrase) and audio effects are involved, and the live website sample generator is far better. Lesson recorded: the unit of "good" is the patch in context (notes + effects), not the engine settings alone; the model that scores engine settings is only the sound-design part of that.

Fix, `listen_model.py build --context --tag m1ctx`: the same engine settings are played by a real phrase (Bach chorale 068, first 12 s, as note events) through a Reverb, 10 model-proposed + 10 uniform-random Analog settings, plus 4 of the site generator's own samples as the reference bar, all 24 shuffled, loudness-matched. Page: `eval/listen/model_m1ctx/index.html`. Wiring found while building: the MIDI File node plays nothing in a headless render (silence), so notes go in as `--notes` events instead.

**C11 Analog, 2026-10-28 (G9 embeddings, 2,000 rows, test 429):** R2 of the embedding (PCA-32) from the 11 axes: variance-weighted **0.523**, median component 0.042. C11 has no pass bar; it is reported per round and each new axis must raise it. Reading: the 11 axes explain about half of the embedding's variance (the large, shared components), and almost nothing of most small components. The unexplained half goes to listening and naming, as the claim says. Baseline for the next round: 0.523.

## Note layer 2j: sloppy_timing fix (2026-10-28)
Cause: the feel gate widens every band by 25% of its width; for jitter that put the live limit at 0.229 (p99 is 0.1835), so random-offset bars passed.
Change: `feel_analysis.MARGINS = {"jitter": 0.0}` (other features keep 0.25). Measured on a fresh seed (777), 1,000 real test bars and 300 corrupted:

| | before | after |
|---|---:|---:|
| feel_check false reject on real bars | 0.7% | 1.1% (bar: B2 total <= 3%) |
| sloppy_timing caught | 83.3% | 93.3% |

Not tuned further: a 0.15 limit would catch 97.6% at 1.0% false reject on the first sample, but that is picking on the data; kept the plain p99. Still to do: re-run full `benchmark.py` B2/B3 with the daemon paused.

## G9 monotonicity, Analog (2026-10-28)
Does the embedding move steadily when one knob is swept? Spearman rho per knob sweep: CLAP mean 0.91 (85% of sweeps >= 0.8, 92% >= 0.5, none negative); EnCodec mean 0.89 (81%, 89%, 0%). Weakest knobs (CLAP): pw1 0.62, fine1 0.64, fine2 0.82, fm 0.82, drive 0.83. Strongest: attack, release, resonance, noise, spread (1.00). The embeddings carry knob direction well; pulse width and fine tune are the thin ones.
Dev build (`ibuild.py build --mode dev`, -j2) running; Vital/Serum state re-run waits on it.

## Status 2026-10-28 (late): Block 8 first version built
`tools/universe/compose.py` is the one place that writes a whole patch: chord-file note layer (bass / pad / lead, mood in the source name) + sound recipe per part (Analog / Wavetable / FM, role-shaped) + dry and send Mixers (gain, pan, reverb send) + Reverb + Limiter, rendered headless. `compose.py batch --n 6`: 6 of 6 rendered, peak -3 dB (limiter ceiling), no silence. `compose.py blind` built 13 whole clips (8 composed, 5 site generator), loudness-matched and shuffled, at `training-data/eval/listen/compose_blind/index.html`, key in `compose_blind_key.json`. Open: the owner's rating, then the sound layer replaces ROLE shaping with a target point, and the mix layer replaces MIX once Ableton sets are in.

## Block 1 and 2 (2026-10-28, evening)
**Block 1** `tools/universe/midi_axes.py` writes axes 23-31 per MIDI file (`store/midi_axes.jsonl`). Swing and micro-timing report a grid-confidence and leave swing empty when the file is not on a grid (first version gave 1.67 / 31 ms on every performed file: an artefact). 12 of 12 spot-checked against mido. Local files first (~80k); the other ~214k were only on Google Drive and are being pulled to `datasets/midi/data`, then run.
**Block 2** `tools/universe/fit_ve.py`: ridge and gradient-boosted fits of V and E from 45 cues (axes 23-31 + register, density, articulation, scale-degree histogram) on EMOPIA 1,078 clips / 381 songs, held out by song (10 splits). **E passes: r 0.816 (ridge) / 0.844 (GBR), accuracy 0.91.** **V misses the 0.8 bar: r 0.508 / 0.547, accuracy 0.72 / 0.74** (was 0.43 for the rule). Top V cues: major-ness, key strength, high register; top E cues: velocity, note rate. Matches the literature ceiling (EMOPIA valence-only accuracy .56-.83 in the paper). Not re-scored with an easier bar. Chord-file words not yet added; owner sign-off of the C5 rules (70% of 40 clips) still open. Weights: `feel/ve_weights.json`.

## Block 3 (2026-10-28, evening)
The unassigned count is 2,252 (not 3,894: earlier tag passes placed more). They are not timbre-ambiguous instruments but Omnisphere phrases (948), one-shots (194), psychoacoustic FX (414), retro textures (408), Spitfire loop libraries (209) and 79 generic: Omnisphere sound sources cannot be rendered on their own (only patches that use them), so `tools/universe/assign_rest.py` assigns them a content-kind cluster from the library path (`assigned: "path"`, `kind_cluster: true`) -> `atlas/instruments_assigned.jsonl`. Unassigned 0 of 14,161 (bar < 5%). A kind is not a timbre cluster; a timbre model should skip `kind_cluster` rows. Rendering them stays open.

## 2026-10-28 (later) — Blocks 2 and 4 follow-up
- Block 4: no Harmonix Set and the Ableton mixer data is unusable (faders at 0 dB, sends off; `mix_als.py`, 89 sets, 1,787 tracks, kept for devices/pan only).
  Replacement: `mix_audio.py` measured 1,680 finished tracks on the WD drive (LUFS, crest, side/mid, 6 band shares, section arc).
  `mix_to_csv.py` wrote 33 measured rows into `feel/dataset/4_Mix.csv` (evidence M). The 496 'role x band energy share' rows are still priors: no stems exist, only masters.
- Block 2 robustness (`v_boost.py`, 53 features, 10 song-grouped splits, 4 model families):
  E r = 0.82-0.87 (sd 0.02-0.03), acc 91-92%: passes, stable across families.
  V r = 0.51-0.58, acc 73-76%: plateaued across all families and added harmony features. Feature-limited; the bar (0.8) stays unmet.
- Sign-off (decided by the agent, owner delegated 2026-10-28): E rules approved for use. V rules are ADVISORY only: compose may nudge mode/brightness from V but no
  decision may depend on V alone. Owner's 40-clip check is still the real V test.

## 2026-10-28 (goal item 3) — knob direction, held-out banks (`knob_direction.py`)
Measured-slope model, 5 folds split by bank (no leakage). Two readings, both reported:
| Engine | all moves, dominant axis | gated (>= 0.5 sd) | RELIABLE cells only (train consistency >= 0.8): acc, share of knob-axis cells |
|---|---|---|---|
| Analog | 65.4% +- 4.4 | 64.8% | **89.5%** (400 moves; 23 of 49 cells) |
| Oscillator | 67.6% +- 4.2 | 70.4% | **91.6%** (793 moves; 25 of 79 cells) |
| Sampler | 69.8% +- 8.4 | 76.2% | **90.9%** (99 moves; 8 of 12 cells) |
| Equation Synth | 58.1% +- 9.5 | 57.0% | no reliable cells (formula text drives the sound; knobs are weak) |
Reading: the all-moves bar (90%) is still MISSED (about 65%): a knob's effect on the dominant axis depends on context. What does reach 90% is a selective
predictor that abstains on non-reliable (knob, axis) cells; it covers about half the cells. This is a different claim from the original bar (abstaining),
so the original bar stays open until the owner decides whether the selective reading counts. compose should use only reliable cells either way.
Also measured this session: 6 more engines (Equation, Partials, Sampler, Slicer, Grain Molder, Field Synth; 240 presets each). Partials, Grain Molder and
Field Synth produced no one-knob pairs in 240 presets (bank design needs more presets); Wave Terrain and Spectral Synth need an image input, Granular,
PaulStretch, Molder and Looper need an audio input: not yet measured.

## Block 1 progress (2026-10-28)
Axes 23-31 measured on all locally pulled MIDI (a31 223,824 rows, 293 errors); `midi_axes.py check --n 20` = 20/20 consistent. Remaining corpora (tegridy onward) still pulling; re-run `midi_axes.py run --local` after the pull ends.

## Goal item 4: cluster assignment from audio (2026-10-28)
`tools/universe/cluster_eval.py` -> `feel/cluster_eval.json`. Per family (>=24 audio instruments), reference clusters from the whole family; 5 random 80/20 splits per seed, re-cluster on 80%, assign held-out 20% by nearest train centroid, score against the reference label.
- Instrument-weighted held-out accuracy: seed 0 93.8%, seed 1 93.9% (2,130 held-out each); seeds differ by 0.1 pt. Bar >= 85% and stable: MET on this reading.
- Caveat, stated plainly: the weighted number is carried by large k=2 families (drums 789, synth 491, bass 174). Family-macro accuracy is ~85% (seed 0) and the many-cluster families are weak: strings 66-70% (k=12), mallets 67-77%, other 29-40%. Those are misses and the reference labels are themselves k-means on the same features (circular), so this measures stability of assignment, not agreement with human-named timbres.
- Not yet done: per-part critic evaluation; the 2,252 `none` (no audio, no usable tags) rows in instruments_clustered stay unassigned there.

## C5 mood test, round 1: MISS, and the test was invalid (2026-10-28)
Owner's answers: real 6/20, composed 6/20, total 12/40 (chance is 10). Every answer was "tense" or "sad": no clip read as happy or calm. Owner: the clips in MY TEST sounded like noisy pads. Correction (owner, 2026-10-29): the live Generate Samples node and the website generator are fine; the complaint was only about the test I built.
Cause (mine, not the mood model): clips used default-0.5 / randomly drawn Analog recipes, so noise, drive, fm and detune sat in every voice and real EMOPIA piano was rendered through the same noisy pad. The test measured the timbre, not the mood. Same cause explains compose.py sounding like noisy pads. This says nothing about the live node, which the owner confirms is fine.
Fix: compose.CLEAN forces noise 0, drive 0, fm 0, low resonance, small detune on every part; C5 real clips use a fixed clean pluck voice. Round 2 rebuilt at training-data/eval/listen/c5_mood. Round 1 stays recorded as a miss.
Determinism (item 8): `own_sweeps.py check` worst mel r = 1.00000 on Analog, FM, Oscillator, Metallic, Equation Synth, Sampler, Field Synth (bar 0.97): MET for those 7.

## Dot test on live-style Analog voices (2026-10-29)
tools/universe/dot_test.py: 12 Analog voices from the 34 live style cards, one trusted non-banned knob per axis moved by one spread, base/up/down rendered (100 renders), 80 trials.
- Direction correct: 91.2% (bar 85%). Median gain 0.94 of the requested move (bar 0.5). Per axis: loudness 100%, brightness 93%, sharpness 92%, fullness 92%, warmth 82%, movement 82%.
- Noisiness stays put (|shift| < 0.3 sd): 83.8% (bar 90%) -> MISS on this bar, so verdict INCONCLUSIVE.
- Loudness drift: 29.5% stay put. Not a defect for export (-14 LUFS normalises) but the dot must compensate gain.
- Reading: a single dot works on a live voice. Next: pick the knob with least noise coupling per axis and re-test; this is tuned on the same voices, so confirm on fresh voices before it counts.

### Dot test round 2: fresh voices, least-noise-coupling knob (2026-10-29)
12 other live-style Analog voices (--skip 12 --lowcouple), 63 trials. Noisiness holds 93.7% (bar 90%, MET). Direction 79.4% overall (bar 85%, MISS), but by axis: brightness 100%, sharpness 100%, warmth 91%, fullness 91%, loudness 75% (normalised at export), movement 11% (FAIL: the low-coupling knob for movement is wrong).
Decision: ship dots only for brightness, sharpness, warmth, fullness (>= 91% on fresh voices, noise held). Movement is not a dot until a knob passes. Whole-test verdict stays INCONCLUSIVE, recorded as a miss on the all-axes reading.

### Dots: how many independent dots does the Analog model really have? (2026-10-29)
tools/universe/dots.py. Finding, not a pass: brightness, sharpness, warmth and fullness are NOT four controls. With reliable cells only, all four pick the same knob (cutoff), and a ridge mixing matrix reaches only ~0.2-0.3 of each target with off-diagonals as large as the diagonal. SVD of the reliable knob x axis matrix (11 knobs): singular values 3.84, 2.47, 1.63, 1.07, 0.71, 0.31, ~0 -> about 5 independent directions: (1) attack/punch vs decay, (2) cutoff vs loudness, (3) cutoff vs release (tone vs tail), (4) sustain -> movement, (5) resonance -> decay/warmth.
Decision: the dots are defined from these model directions (named after what they do), not from the 11 descriptive axis names. Must be re-tested on fresh voices (target/decoupling) before shipping. Only Analog so far; other 12 engines untested.

### Block 1 axes pass complete (2026-10-29)
midi_axes.jsonl: 293,573 of 293,573 indexed files have a row; 292,452 measured (a24-a30), a23 287,614, a31 271,588; 1,121 error rows (unparseable or >4 MB or >20 s). 20-file spot check 20/20 consistent. Goal count is 294,582: 1,009 files are not in the midi_full index (not yet extracted) -> bar "all 294,582" = 99.3% measured, a MISS until those are indexed.

### Dot test round 3: the five model directions on 12 fresh voices (2026-10-29)
One dominant knob per direction, 57 trials: Tone (cutoff -> brightness) 92% (12), Tail (release -> decay) 89% (19), Punch (attack -> attack) 62% (16, FAIL), Motion (sustain -> movement) 50% (10, chance, FAIL). Resonance->warmth produced no usable trial. Overall sign 75.4%, noisiness held 91%, median gain 0.49 -> INCONCLUSIVE, recorded as a miss.
What the data supports on Analog today: TWO real dots, Tone (cutoff; 92-100% over rounds 2-3) and Tail (release; 89%). The 3rd and 4th dot are not supported yet; they need either better knobs (an envelope-shape macro, tested) or more sweeps. The product claim must be "2 proven dots on Analog", not 3-4 on all instruments, until measured otherwise.

### Goal item 3 status, all engines (knob_direction.py, 5 folds by bank, 2026-10-29)
| engine | reliable-cell acc | all moves (dominant) | note |
|---|---|---|---|
| Analog | 89.5% | 65.4 +- 4.4 | reliable just under 90 |
| Oscillator | 91.6% | 67.6 +- 4.2 | |
| FM | 91.6% | 76.6 +- 2.9 | |
| Metallic | 95.6% | 78.2 +- 1.8 | |
| String | 92.8% | 76.7 +- 1.8 | |
| Sampler | 90.9% | 69.8 +- 8.4 | only 12 banks |
| Wavetable | none (0 reliable cells) | 52.8 +- 4.4 | MISS, chance |
| Equation Synth | none | 58.1 +- 9.5 | MISS, 12 banks |
| Partials, Slicer, Grain Molder, Field Synth | too few one-knob pairs | - | NOT MEASURABLE yet |
Bar (>= 90% on all 13 engines, all moves): MISS on every engine. Only on the "reliable cells only" reading do 5 of 13 engines pass (Oscillator, FM, Metallic, String, Sampler), Analog is 0.5 pt short. Closing it needs new one-knob sweeps on Wavetable, Equation Synth, Partials, Slicer, Grain Molder, Field Synth (+ Spectral Synth, Granular for the 13) and a better model than a per-knob slope for the all-moves reading.

## 2026-10-29 (goal item 2) — note model, held-out benchmark run (`benchmark.py --scope shippable`, log kept at /tmp/bench_note.log)
Run with sweeps running alongside; B2/B3 here are NOT the daemon-paused runs the goal asks for (item 8 still open).

| Gate | Result | Bar | Verdict |
|---|---|---|---|
| B1 held-out compression, bits saved on unseen files | drum +0.204, drum_mix +0.166, melody +0.813, bass +0.111, chord +0.058, params +1.997 | positive for every part | MET |
| B2 real-music false rejects | overall 1.38% (drums 2.90%, melody 2.40%, chords 1.28%) | <= 3.0% | MET (not daemon-paused) |
| B3 mishap catch | 98.0%; critical types 100%; sloppy_timing 91%, inverted_accents 93% | >= 96%, 100% critical | MET (not daemon-paused) |
| B4 structural realism AUC | melody 0.535, drums 0.632 | <= 0.65 | MET (linear test on few features) |
| B5 novelty/diversity | card escape 88.4%, diversity 0.137 vs real 0.116, cluster entropy 0.672 vs real 0.936 | entropy >= 0.90 x real | **MISS** (0.72 x real) |
| B10 emotion | valence AUC 0.763, arousal 0.717 mean over held-out listeners; vgmidi arousal 0.549 weak | 0.70 / 0.65 | MET |

Not measured yet for item 2: held-out key accuracy and chord-prediction accuracy by name, and generated-note spread against the corpus on tempo, register, density, rest, swing. B1 chord +0.058 bits is the nearest evidence for chords. These two are the next tool (`note_spread.py`).

### Note spread, live generator vs corpus (`tools/universe/note_spread.py` -> `feel/note_spread.json`)
Test fixed before the run: share of the 32 style-card patches (render-runs/styled/*_1.inf, Note Sequencer layers only) inside the corpus 5th-95th percentile (19,827 files); pass = >= 90% on every axis.

| Axis | Corpus p5 / p50 / p95 | Generated p5 / p50 / p95 | Inside |
|---|---|---|---|
| tempo (bpm) | 66 / 120 / 171 | 69 / 126 / 175 | 88% (just under) |
| register (range midpoint, MIDI) | 52.5 / 60.5 / 70 | 26 / 36 / 44 | **0%** |
| density (notes/s) | 2.2 / 12.3 / 36 | 0.3 / 6.5 / 11 | 69% |
| rest | 0 / 0.04 / 1.0 | 0.16 / 0.34 / 0.94 | 100% |
| swing | 0 / 0 / 0.5 | 0 / 0 / 0.25 | 100% |

**Verdict: MISS on tempo (88%), register (0%) and density (69%).** Cause for register: `styled.py` places sequencer lines at octave 2 (bass/low layers); Chorder and Arpeggiator voices are not counted, so register may read low partly because of what is measured. Rest and swing agree only loosely (rest compares step silence with time silence). Fix route: let the learned note model set register and density per role from corpus percentiles, then re-run this same test. No bar moved.

### Note model: held-out key and chord accuracy (item 2, first half)
Objective/baseline/test written in each tool header. No bar was set for these in the goal text beyond "held-out accuracy and loss", so these are reported, not graded.
- **Key** (`key_eval.py`, labels = keys in the chord-progression file names, detector = midi_axes fixed profiles, 1,500 unseen files): exact tonic+mode **51.1%** (major 59.4%, minor 44.1%); counting the relative key **69.9%**. Short 4-8 chord files are hard for a pitch-class profile; this is the detector the axes use, so axis 23 inherits this error on short files.
- **Next chord** (`chord_eval.py`, back-off trigram, 5 random 80/20 splits by progression): top-1 **23.7% +- 2.2**, top-3 **52.5% +- 1.9**, cross-entropy **4.99 +- 0.26 bits** (uniform 6.46). Baseline "most frequent chord" top-1 18.1%. Beats baseline, but only **194 distinct progressions** exist once the 12 keys are merged, so the estimate is thin and the model is a small table, not a learned song model.
Not done: chord prediction on the 294k full-song corpus (needs a chord recogniser run over audio-free MIDI), so item 2 stays open.

### Register miss, diagnosed (not a restated pass)
Corpus register by instrument family (first 120k files, range midpoint, p5/p50/p95): piano 47/60/75, guitar 47.5/59.5/72.5, ensemble 50.5/66/79.5, **bass 31.5/38.5/56**, strings 39/63/77.5. The generated Note Sequencer lines (26/36/44) are bass-role lines compared above against an all-family corpus. Against the bass family alone, 71.9% (23 of 32) sit inside the corpus 5th-95th range. That is still under the 90% bar. **The original item-2 verdict stays MISS.** A per-role version of the test (each generated layer against its own family) would be the fair test; it changes the test, so it waits for Naman's sign-off. The sequencer layers carry only the bass/low roles; Chorder/Arpeggiator layers are not yet measured.

### Item 1 figures from the feel workbook (~/Downloads/feel_dataset.xlsx, sheet 1_Instruments, 14,161 rows)
| Column | Count | Share |
|---|---|---|
| audio_measured = yes | 2,158 | 15.2% |
| audio_measured = no (evidence "R, prior") | 12,003 | 84.8% |
| cluster_assignment from audio | 2,158 | 15.2% |
| cluster_assignment from tags only | 9,751 | 68.9% |
| cluster_assignment none (unassigned) | 2,252 | **15.9%** |
| Best knobs "engine unmeasured" | 2,156 | 15.2% |

**Item 1 verdict: MISS.** Bar is unassigned < 5% (now 15.9%) and the measured column covering every audio node. Sources: steam 11,519, exs 2,413, nkc 229. Next step is a render-and-measure pass over the 12,003 unmeasured instruments (many are Steam library entries with no sample audio on this Mac, which has to be checked first, since an instrument with no audio cannot be measured here).

### Item 4, per-part critic: status corrected
`tools/universe/critic.py` is a per-part critic for the note side: parts drum / melody / bass / chord, each with hard validity rules, a plausibility floor set at the 1.5th percentile of held-out real music (B2 false rejects 1.38%, drums 2.90%), and mishap catch 98.0% over 11 corruption types (B3). It exists and is benchmarked. **What is still missing for item 4:** a critic for the *sound* side, per audio part (is this bass voice / pad voice / lead voice a good sound for its role). `feel_check` and `spectral_gates.py` (B8: held-out FRR 0.81%, catch 97.7%) check renders for damage, not for fit to a role. Verdict for item 4: cluster assignment bar met (93.8% / 93.9% over two seeds); per-part critic **partly in place (note side), sound-side role critic not built**.

### Item 1, MIDI axes accounting (2026-10-29)
292,452 files carry axes 23-30 (axis 31 on 271,588: files without enough bars skip it); 1,121 error rows, split: **747 "no notes"** (empty files, correctly unmeasurable), **365 over the 20 s parse limit** (large files; retryable with a higher limit), 5 dtype errors and 2 empty-list errors (code bugs, fixable), 2 "not midi". So 747 + 2 are legitimately unmeasurable; **about 372 are fixable** (365 timeouts + 7 bugs). Not yet re-run. Bar "all 294,582" cannot be met literally: at most 294,582 - 749 = 293,833 have notes.

### Item 2, per-role register test (signed off 2026-10-29)
Each Note Sequencer layer (55 across the 32 style patches) against its own family's 5th-95th range, corpus first 150k files: layers with midpoint < 48 count as bass (corpus bass 31.5-56), the rest as melodic (corpus 46.5-79).
- Bass layers inside the bass range: **26 of 48**. Melodic layers inside: **7 of 7**. Overall **60.0%** (bar 90%). **MISS.**
- The misses are all on the low side: bass layers whose range midpoint sits under 31.5 (sub-bass, below the corpus bass 5th percentile). Cause: `styled.py` / SampleStyles place sequencer bass at octave 2 with a narrow span. Fix candidate: lift the bass octave in the style cards where the card allows it; it ships only if it wins the blind test against the unchanged live node (goal item 7).

## 2026-10-29 Claims ledger C1-C11 (goal item 8). Verdict = what the evidence in this file shows today; "not run" counts as FAIL until measured.
| Claim | Bar | Evidence | Verdict |
|---|---|---|---|
| C1 axes understood | R2 per axis mean >= 0.6, group split | median head R2 0.082-0.200 (group), best axes attack 0.44, density 0.23 | **FAIL** |
| C2 ingredients | direction >= 90%, size r >= 0.7 | Analog 63.5% / r 0.635; Oscillator 61.8%; reliable cells 89.5-95.6% on six engines, Wavetable 75.0% | **FAIL** |
| C3 good sound | AUC >= 0.8; owner >= 30/40 | AUC 0.924 PASS; owner 24/40 | **FAIL** (owner half) |
| C4 words | AUC >= 0.75 per word; >= 70% of 50 | words_eval2: 5 of 28 testable words >= 0.75 (sub, distorted, mellow, punchy, digital); made-sounds half not run | **FAIL** |
| C5 moods | r >= 0.8 per dim; >= 80% in region | E r 0.816-0.844 PASS; V r 0.508-0.547; clips unscored (round 1 invalid) | **FAIL** (V) |
| C6 creative | N < 0.1 at c = 0; N, R rise with c | Oscillator base N 0.883 (needs generator) | **FAIL (not run)** |
| C7 can make | nearer than nearest render on 300 targets | not run | **FAIL (not run)** |
| C8 genres | top-3 >= 0.7 | sample axes at chance 0.274 vs 0.273; drum grooves carry some genre | **FAIL** |
| C9 chains | direction >= 90%, size r >= 0.7 | chain track not measured | **FAIL (not run)** |
| C10 taste | keep >= 75% by click 20 | simulated personas only (B6 75.2%); no real user data | **FAIL (not run on people)** |
| C11 nothing lost | R2 rises each round | 0.523 (Analog, 2,000 rows) | reported, no bar |

### Item 4, sound-side role critic first measurement (`tools/universe/role_critic.py` -> `feel/role_critic.json`)
Objective: softmax regression P(role | audio features), 2,158 audio-measured instruments, 14 roles (families); accept a voice in a role when P(role) is above the 3rd percentile of train P(true role). Test: 2 seeds x 5 random 80/20 splits, mean +- sd; "catch" = share of held-out voices rejected when declared as every wrong role.
- False reject **5.6% +- 1.1** (bar <= 3%) — **MISS**
- Catch of swapped roles **73.7% +- 1.2** (bar >= 96%) — **MISS**
- Top-1 role accuracy 70.0% +- 1.3 (baseline: biggest role share).
Reading: roles that overlap in sound (piano / keys / mallets, synth / texture / other) cannot be told apart from 43 spectral features; swapping within them is not detectable. Next: add temporal/envelope features and merge indistinguishable roles into role groups, then re-run on the same splits.
Second run, same splits: key range added as 3 features and piano/keys/mallets and synth/texture/other merged into two role groups (10 roles). FRR **4.6% +- 0.8** (bar 3%), catch **75.2% +- 1.5** (bar 96%), top-1 **73.2% +- 1.2**. Still a **MISS** on both. The audio features hold no more role information than this; the remaining lever is more audio per instrument (only 2,158 of 14,161 are measured), tied to item 1.

### C4 words, first measurement (`tools/universe/words_eval.py` -> `feel/words_eval.json`, 2026-10-29)
Test: does a sample's audio (32 log bands + 8 scalars) tell whether its file name carries a sound word? 182,276 sample rows, pack-held-out (crc32(pack) % 5 == 0), logistic per word, 3 negative-subsample seeds, AUC mean +- sd. Bar: AUC >= 0.75 per word.
Of 30 candidate words (warm, bright, dark, soft, ...), **only 5 have >= 40 positives on both sides**; the other 25 are too rare in sample names to test. Results: dark **0.749** +- 0.015 (60 test positives), punchy 0.726 +- 0.018 (40), hard 0.524, tight 0.507, clean 0.500 (294 train positives: one pack, no audio signal). Mean 0.601; **0 of 5 reach 0.75. C4 words half: MISS.** The "made sounds" half (>= 70% of 50) was not run. Reading: file names are a thin, noisy word source; word labels need the preset names and tags of the 14k instruments and Diva presets, which have richer words.

## Session wrap (2026-10-29): silent-sweep fix and where to resume
- Silent sweeps, cause found: patch_text did not escape newlines in string params (Field Synth `code`); Slicer left on sliceBy 0 (manual markers, none set); Grain Molder never saved its source path (VisitParams lacked `path`; one line added, dev build rebuilt, still renders silent: `path` is not yet in its param table, so FIXED_ENGINE's SRC is never written).
- Verified peaks after fix: Field Synth 0.29-0.41, Partials 0.43-0.64, Slicer 0-0.09, Wavetable 0/1.0/0.68 (some settings silent; per-knob scan was unfinished).
- Owner direction: focus sweeps on the main synths only: Analog, Wavetable, Oscillator, Metallic, String, FM, Sampler. Field Synth is for custom-code sounds, later. Grain Molder/Slicer/Partials dropped for now.
- Knob-direction on reliable pairs (old run): Analog 89.5, Oscillator 91.6, FM 91.6, Metallic 95.6, String 92.8, Sampler 90.9; Wavetable none (silence). Rerun knob_direction.py after Wavetable is fixed.
- C4 word AUC (pack-held-out): dark 0.749, punchy 0.726, hard 0.524, clean 0.500, tight 0.507. Plan: label from own render knobs, add word-specific measured features, drop words the sound cannot carry only with owner sign-off.
- Next: (1) find which Wavetable knob silences it, fix its fixed/swept ranges, rerun its sweep; (2) rerun knob_direction.py, commit knob_direction.json; (3) item 1 MIDI retries; then remaining GOAL.md items.

## Goal item 4, role critic retrained on part-labelled samples, packs held out (2026-10-29): MISS
`tools/universe/role_critic_samples.py` (result `feel/role_critic_samples.json`). 124,830 samples from audio/features.jsonl in the generator's 13 parts
(clap with snare, cymbal with hat, `unknown` left out), 331 packs, 5-fold with whole packs held out, per-role 3% thresholds set on a validation split of training packs.
- Softmax regression: FRR 4.3% +- 1.6, catch 40.4% +- 5.4, top-1 50.8%.
- Two-layer MLP (256-128): FRR 4.6% +- 1.4, catch 43.9% +- 5.2, top-1 54.2%.
- Bars FRR <= 3%, catch >= 96%: MISS on both, far off. More rows did not help: the instrument-level critic (2,158 voices) had catch 75%.
Why: the stored features are whole-file summaries (32 band means, peak, rms, onsets, crest, centroid). They hold no envelope over time,
which is what separates pluck / lead / keys / pad and kick / tom. Labels come from folder names and are noisy (fx, synth, drums are catch-alls).
What would move it: per-frame features (attack, decay, spectral change over time) from the audio itself. The source files are on the WD Black
drive, which is not connected, so that re-analysis waits on the drive.

## Goal item 4 on the generator's own audio, frame-by-frame features (2026-10-29): catch MET, FRR MISS
Idea (Naman): render the parts with Infinite itself, so labels are exact and the audio can be measured frame by frame without the drive.
`tools/universe/wasm_render_parts.js` (live generator via website wasm, `_gen_pcm`) -> `frame_features.py` (per-hit envelope, attack, decay,
sustain, timbre change over time, pitchedness, hit regularity, spectral shape mean and spread) -> `role_critic_render.py`. Data:
`training-data/audio/gen_parts.jsonl`, 5,097 renders = 34 styles x 15 parts x 10 seeds (features only, no audio kept). Result `feel/role_critic_render.json`.
Test: 5-fold with whole styles held out; per-role 3% thresholds from a validation split of training styles.
- MLP (128-64): catch 98.4% +- 1.0 (bar >= 96%: MET), FRR 6.1% +- 1.4 (bar <= 3%: MISS), top-1 93.5%.
- Linear: catch 98.0%, FRR 6.1%, top-1 93.0%. Nested pairs (drums holds kicks etc.) change catch by < 0.2 points.
- Biggest confusions: plucks->pads 5.3%, pads->textures 5.0%, plucks->leads 4.4%, synths->leads 4.4%.
- Same task on whole-file summaries of real packs: top-1 54%, catch 44%. The frame features are what moved it.
FRR miss: the thresholds come from ~5 validation styles (~55 renders per role), so the 3rd percentile is noisy and does not carry to new styles.
Next: set thresholds on out-of-fold probabilities over all training styles (about 6x more rows per role), then retest. The bar is unchanged.
Limit: this checks the generator's parts against each other, not against real instruments. The real-sample check needs the same frame
features on real audio (WD Black, or any local library run through frame_features.measure).

### Role critic cut-offs from out-of-fold probabilities (2026-10-29): MET
Same data, features, model and 3rd-percentile target; only where the cut-offs come from changed. Each outer fold now trains on all its
training styles, and the cut-offs come from 4 inner style-held-out models that score every training style once (about 6x more rows per role).
`role_critic_render.py train --calib oof` (default now; `--calib val` reproduces the earlier run). Result `feel/role_critic_render_oof.json`.
- MLP: FRR 2.6% +- 0.6 (bar <= 3%: MET), catch 97.9% +- 0.2 (bar >= 96%: MET), top-1 93.9%. Worst fold FRR 3.7% (fold 1); the bar is read on the mean.
- Linear: FRR 2.9%, catch 96.9%, also inside both bars.
Scope: parts of the generator against each other, on styles never seen in training. Not yet checked against real instruments (needs frame features on real audio).

## 2026-10-29 (night): improvement-plan pass, results per bar
Bars unchanged throughout; misses recorded as misses.

### Goal item 3, Wavetable knob direction (4,080 own renders, 5 folds by bank): MISS
`knob_direction.py` on the finished Wavetable render: reliable cells **75.0%** on 220 scored moves (bar 90%), dominant-axis 55.9% +- 1.1, 2,492 pairs over 204 banks.
The other six engines stay at 89.5-95.6%. Reading: most Wavetable pairs move several knobs at once, so few (knob, axis) cells are reliable. Next: renders that move one knob
at a time (`own_sweeps.py` with the "one" share raised), then rerun; one heavy job at a time, after the critic renders.

### C4 words, second measurement (`tools/universe/words_eval2.py` -> `feel/words_eval2.json`): MISS
Labels now come from Diva preset names, descriptions, usage, type tags and folders (4,326 rendered presets joined by name), Ableton device previews (12,119) and sample names (182,276);
features add attack/decay, stereo width, onsets, length and the 40-point envelope. Logistic per word, 5 folds by group (bank / device folder / pack), AUC mean over folds.
- **28 of 33 words testable** (was 5). On all sources together, **5 reach 0.75**: sub 0.945, distorted 0.833, mellow 0.784, punchy 0.783, digital 0.772.
- The rest fall below; clean 0.389 and soft 0.48 are under chance (the word follows the pack, not the sound). Verdict read on the all-sources run, not on the best source per word.
Next: dropping words the audio cannot carry needs Naman's sign-off; the "made sounds" half of C4 (>= 70% of 50) still not run.

### Item 1, MIDI axes bug fix (`tools/universe/midi_axes.py`)
Re-strikes of a held note stored fields in the wrong order ((t0, v0, t, pitch, ch) instead of (t0, t, pitch, v0, ch)); about 40% of files have re-strikes (158 of a 400-file sample),
so every axis built on note lengths, pitch or velocity was off for them. Also fixed: the dtype-object error, the empty min() error, and the parse limit (20 s -> 60 s, `MIDI_AXES_LIMIT`).
The old output is kept as `store/midi_axes.pre-restrike.jsonl`; the full rerun is recorded below when it finishes. `key_eval.py` is unchanged by the fix (exact 0.511, lenient 0.699):
the chord-progression files have no re-strikes.

### Item 2, key accuracy, detector variants (`tools/universe/key_variants.py` -> `feel/key_variants.json`): lenient MET
Grid of profile (Krumhansl-Kessler, Temperley, Albrecht) x bass weight x first/last-chord weight, chosen on 1,500 tuning files that are disjoint from key_eval.py's 1,500 test files
(`key_eval_files.py`). Chosen: Temperley, no extra weights. Test: **lenient 76.5%** (bar 70%: MET), exact 44.1% (KK: 51.1%).
The trade: Temperley confuses relative major/minor more often. midi_axes keeps the KK mode axis (it feeds major-ness for mood); key reporting can use Temperley. Both numbers recorded.

### Item 2, next chord (`tools/universe/chord_eval2.py` -> `feel/chord_eval2.json`): MET
Same data, splits and baseline as chord_eval.py. Model: numeral n-gram mixed with a chord-class back-off (root semitone from the tonic + quality), mode context,
the chord one bar back, and the first chord. Weights chosen on split seeds 10-19, reported on seeds 0-4. Top-1 32.3% vs baseline 18.1%: **gain +14.3 +- 2.5 points**
(bar +10: MET; the old n-gram gave +5.6). Caveat: 194 distinct progressions; selection and report splits are different shuffles of the same pool.

### Item 2, per-role register and density, corrected count (`tools/universe/role_spread.py` -> `feel/role_spread.json`)
The old count took every Note Sequencer step as a note and counted synth drum lanes (a Wavetable with a pitch drop and no sustain, or a noise-only Analog, fed one pitch on every step)
as bass or melodic lines. Now only audible steps (enabled, velocity > 0) count and drum lanes are left out. `--legacy` reproduces the old numbers (`feel/role_spread_legacy.json`).
Patches: the live generator's style patches dumped with `wasm_patches.js` (all seeds), 508 pitched layers (888 under the old count).

| Role | Axis | Old count inside | Corrected inside | Generator median | Corpus 5-95% | Verdict |
|---|---|---|---|---|---|---|
| Bass | register | 89.1% | **97.5%** | 36 | 31.5-56 | MET |
| Bass | density | 62.8% | **53.5%** | 3.7 notes/s | 0.38-3.94 | MISS (206 above) |
| Melodic | register | 61.9% | **100%** | 53 | 46.5-79 | MET |
| Melodic | density | 94.5% | **78.5%** | 5.6 notes/s | 0.07-6.91 | MISS |

The 104 "too high" melodic layers in the old count were hat and cymbal lanes. The bass-octave lift in SampleStyles.cpp is no longer needed for register; it is set aside in
`git stash` ("bass-lift octave") and would ship only after a blind test.
**Question for Naman (density):** the misses sit in styles built on dense 16th lines (industrial techno, acid, neurofunk, gabber). The corpus is mostly Lakh pop/rock MIDI with no genre
field, so its density range is a pop range. Options: (a) keep the bar against this corpus and thin those styles, (b) compare against a genre-matched corpus subset (needs genre labels),
(c) read density per style family. No change to the music made.

### Goal item 4, role critic v2: note features and moving dots (`role_critic_render.py`, `feel/role_critic_v2_*.json`)
7,645 renders of the live generator: 5,097 with dots at rest, 2,548 with random dots and variation (`--shaped`); 5 parts rendered silent and were skipped.
Bug found and fixed on the way: `frame_features.measure` also returns a field called `shape`, which overwrote the dots record, so every row looked dot-shaped.
Rows now keep the flag as `dots`; the existing rows were relabelled exactly from their seed (seed = first + style*1009 + part*101 + k; first 20000 = rest, 40000 = dots).
5-fold, whole styles held out, MLP (Linear in the json):

| Trained on | Cut-offs from | Rest FRR / catch | Dots FRR / catch | Verdict |
|---|---|---|---|---|
| rest | all training rows | 2.7% / 98.2% | 6.4% / 97.5% | MISS (dots FRR) |
| rest + dots | all training rows | 1.7% / 98.5% | 4.4% / 98.0% | MISS (dots FRR) |
| rest + dots | dot-shaped training rows | 1.7% / 98.2% | **3.3%** / 97.6% | MISS by 0.3 (dots FRR) |

Rest renders meet both bars (note features: FRR 2.6 -> 1.7 when trained on both). With dots moving, catch holds but false rejects rise; the dots move
sounds out of the at-rest region. The cut-off percentile was not tuned on test results. Next (running): 2,550 more dot-shaped renders (seeds from 60000),
so the shaped cut-offs rest on ~2x more rows; then the same third test.

### Goal item 3, knob direction across the keyboard (`tools/universe/knob_notes.py` -> `feel/knob_notes.json`)
own_sweeps stores mel and bands only for C3, C4 and C3f; the other windows (C1, C5, C6, C3pp, C3mf) keep rms, partials and the release tail. So this test
uses three axes every window has: brightness (partial centroid over f0), loudness (rms dB), tail length. Reliable (knob, axis) cells are learned at C3 on
training banks and scored on held-out banks in every window. Bar 90% per window.
- Analog 92.0-94.5%, Oscillator 94.7-97.0%, FM 92.0-95.7%: **MET in all 8 windows.**
- Metallic: C1 89.8%, C5 87.5%, C6 83.5% (MISS at the extremes; 90-93.5% elsewhere). String: C1 87.5%, C5 89.6%, C6 84.9% (MISS at the extremes).
- Wavetable 82.5-86.9% everywhere (MISS; same data gap as above). Sampler: only C3/C4/C3f stored, 84.8% on 33 moves (too few).
Reading: on these three axes a direction learned at C3 carries across notes and velocities for the subtractive/FM engines; the physical models
(Metallic, String) change behaviour at the keyboard ends, so their knob model needs per-register slopes.

### Smaller checks (2026-10-29, night)
- Valence after the MIDI re-strike fix: `fit_ve.py` (ml-venv) gives the same numbers to the third decimal (V r 0.508 / 0.547, E 0.816 / 0.844): EMOPIA's piano
  files are not affected. C5 V still MISS. Outside V/A data: MERP's repo states no dataset licence, so nothing was downloaded; DEAM / PMEmo / EMOPIA audio
  licences still to check on their own pages.
- Real audio on this Mac: `/Library/Audio/Apple Loops` holds only Final Cut Pro sound effects (1,337), no music loops; the real-instrument and genre checks still
  wait on the WD Black drive.
- Pitch from audio (IMPROVEMENT-PLAN 9.3) is already answered above: single named notes agree 97.9% (bar 90); the 66.9% figure mixed loop-key names in. No fix needed.

### Genre step 1: style from the generator's own audio (`tools/universe/style_critic.py` -> `feel/style_critic*.json`)
34 styles, 7,645 part renders (one take = the 15 parts of one style at one seed). MLP per part on frame features; a take's style = sum of its parts' log P.
5 folds by seed offset, so whole takes are held out. Chance: top-1 2.9%, top-3 8.8%.

| Features | Rest takes top-1 / top-3 | Dot-shaped takes top-1 / top-3 | One part alone, top-3 |
|---|---|---|---|
| frame features | 24.1% / 45.3% | 20.6% / 43.5% | 17.5% |
| + tempo (log2 bpm) | 52.6% / **79.4%** | 39.4% / **74.1%** | 39.7% |

Confusions are between near neighbours (ambient / dark ambient, solo piano / neo-classical, liquid dnb / neurofunk, acid house / tech house).
Scope: this is the "does this take sound like its style" critic (IMPROVEMENT-PLAN 7.2) on generator audio, on isolated parts, not on a mix. It is not C8:
C8 asks for top-3 >= 0.7 on real music, which needs frame features and a tempo estimate on real loops (WD Black drive). C8 stays FAIL.

### Decisions taken 2026-10-29 (Naman delegated both: "take your decision")
**Density (item 2).** Per-style reading of the corrected count (same patches, same corpus): the bass density miss is not confined to four styles. Inside share by style:
industrial techno 0%, acid house 6%, neurofunk 12%, footwork 18%, psytrance 20%, gabber 21%, IDM 33%, and also tech house 38%, deep house 39%, classic trance 40%, minimal techno 44%.
Pop bass in the corpus tops out at 3.9 notes/s (p95); a house bass on 8ths at 124 bpm is 4.1 notes/s and a 16th line is 8.3. So the corpus is a pop yardstick and dance bass sits
above it by design. **Decision: do not thin the music** (thinning dance bass to pop density would make the styles wrong, which the blind test would punish). The item-2 density verdict
**stays MISS** against this corpus; it is not re-scored. The fair test is option (b), a genre-matched corpus (electronic MIDI with genre labels), and it is logged as the next step
for this bar. Register (MET for both roles) is unaffected.
**Words (C4).** **Decision: only the five words that reach AUC 0.75 drive generation** (sub, distorted, mellow, punchy, digital). The other 23 testable words stay in the vocabulary
for search and names but are not linked to any sound change, because the audio cannot be shown to carry them. The C4 verdict stays FAIL (5 of 28, bar 70%); not re-scored.
Reason for both: neither choice moves a bar; each keeps the product honest about what the models can do.

### Goal item 4, role critic v3, and the critic inside Generate Samples (`RoleCritic.cpp`, `feel/role_critic_v3_calshaped.json`)
v3: 10,191 renders (5,097 at rest, 5,094 dot-shaped). The model is trained on all of them, and its cut-offs come from out-of-fold scores on the dot-shaped renders. Whole styles held out, 5 folds:

| Model | Rest FRR / catch | Dots FRR / catch | Verdict |
|---|---|---|---|
| MLP (shipped) | **1.3% / 98.5%** | **2.9% / 98.0%** | **MET** (bars FRR <= 3%, catch >= 96%) |
| Linear | 1.8% / 97.1% | 2.9% / 96.7% | MET |

Doubling the dot renders closed the 0.3-point miss from v2; no bar or percentile moved.
**Shipped into the engine:**
- `tools/universe/export_role_critic.py` writes `src/app/graph/RoleCriticWeights.inc`: the MLP (132 -> 128 -> 64 -> 15) and one cut-off per role (3rd percentile, out of fold, dot-shaped renders).
- `src/app/graph/RoleCritic.cpp` ports frame_features.measure + vector to C++.
- `GenerateSamplesNode::Finish` and `wasm/gen_main.cpp` redraw a part that fails its role's cut-off, inside the existing 3-redraw budget. The last attempt is kept. Pinned dot moves are never judged.

**Checks:**
- **Parity:** `critic_parity.py` (60 fresh parts) gives a feature difference of median 5e-10 and max 4e-5 training spreads. P(role) differs by at most 4e-7, and the accept calls agree 60/60.
- **Redraws:** `critic_reroll.js` (1,020 first takes, dots at rest, same cards). Redrawn takes go from 8.9% to **9.4%**, 0 failed, +5% time per take (767 -> 808 ms in node).
  Most critic rejections fall on takes the old gate already redraws. The new redraws are in Leads (4.4% -> 5.9%), Textures (2.9% -> 7.4%) and Pads (14.7% -> 16.2%).
- **Not done:**
  - The app build has only had a syntax check against build-dev flags; no full build or run yet.
  - `website/gen` is not updated: deploying the new wasm needs Naman's yes.
  - The blind test (does a redrawn take sound better?) needs Naman.

## 2026-10-29 (late) — MIDI axes rerun after the re-strike fix
- `midi_axes.py run --jobs 3` then `counts`: 293,573 rows, 292,825 measured, 748 errors, 0 not found (4,939 s).
- `note_spread.py` (32 style-card patches vs 19,845 corpus files) -> `feel/note_spread.json`: bpm 88% inside, register 0%, density 75%,
  rest **34% (was 100%)**, swing 100%. MISS as before. Rest moved because the fix measures rests properly: the corpus p95 of rest share fell
  from 1.00 to 0.275, and the generator's patches (median 0.34) now sit above it. A new miss, recorded as one; the generator rests far more than
  the corpus (loops built from short motifs). Register 0% is the all-voice median and was already known (per-role register is the bar, MET).
- `v_boost.py` with its feature cache rebuilt (old cache kept as `mix/ve_feats.pre-restrike.npz`): same numbers to the third decimal
  (V r 0.51-0.58, E r 0.82-0.87). Valence stays MISS (advisory only).

### Goal item 3, Wavetable after one-knob sweeps (2026-10-29): MISS, improved
`own_sweeps.py --design sweep --notes C3`: 1,332 presets in 338 s (one base + 19 one-knob moves per bank, knobs rotating; C3 only, 8x less audio).
`knob_direction.py`: reliable cells **79.3%** on 512 scored moves (was 75.0% on 220; bar 90%), 35 reliable cells of 206 with data, 3,842 pairs over 276 banks.
Reading: more pairs made more cells measurable but most Wavetable (knob, axis) cells stay inconsistent (the effect of a knob depends on the others),
so a per-knob slope cannot reach 90% here. Next: a slope conditioned on the base (k-NN or small net over the base knobs), not more of the same renders.
`knob_direction.py` now keeps other engines' results when run on a subset.

## Wavetable knob model that uses the other knobs (2026-10-29): MISS, no model beats the plain slope
Same data (3,842 one-knob pairs, 276 banks), same reliable-cell test (tau 0.8, eff 0.3, folds by bank):
| Model | Held-out acc | Scored moves |
|---|---|---|
| One slope per knob (current) | 79.3% | 512 |
| Nearest 24 / 48 / 96 bases (the other knobs) decide the sign | 77.5 / 78.2 / 79.3% | 276 / 496 / 512 |
| Slope conditioned linearly on every knob (ridge 0.1-100) | 54-64% | 3,237-1,261 |
| Curve per knob (sign of f(end) - f(start), 4 or 8 segments) | 67 / 64% | 982 / 1,480 |
Bigger moves are no easier (eff 0.5: 80.8%, 0.8: 76.3%, 1.2: 68.4%), while Oscillator holds 90-92% at every size. So the
misses are not interactions a model can learn from these pairs; the likeliest cause is measurement noise on Wavetable renders
(a characteristic that differs between two renders of the same preset). Next: render 100 Wavetable presets twice (different
note-on phase) and measure the repeatability ceiling; if the same preset disagrees with itself ~20% of the time, 90% is not
reachable with these characteristics and the finding goes to Naman (the bar does not move without sign-off).
`knob_direction.py` now also reports the neighbour model (`local`) per engine.

## Goal item 3, Analog after one-knob sweeps (2026-10-29): slope 89.5% (MISS by 0.5), neighbour model 91.2% (MET)
+1,350 one-knob presets (C3 only, 645 s). Reliable-cell slope unchanged at 89.5% (1,062 moves). The neighbour model (sign from the
24 training moves of that knob whose other knobs are nearest) reaches 91.2% on 704 held-out moves, folds by bank, same tau/eff.
It covers fewer moves (it abstains where neighbours disagree); compose should use it for Analog and the slope elsewhere.

## C5 mood test, round 2: MISS, timbre held fixed (2026-10-29)
Owner's answers (`eval/listen/c5_answers_owner_2026-10-29.json`): real 12/20, composed 4/20, **total 16/40 (bar 28) - MISS** (chance 10).
Answers lean tense (20 of 40) and only one clip was called calm. Owner: every clip is the same synth with a different melody, a dark synth.
Cause (the test, again): the 20 real EMOPIA clips all play one fixed Analog voice (saw + square, cutoff 5 kHz) and the 20 composed clips
come from compose.py (retired), also Analog with compose.CLEAN. Mood was asked to come from notes alone through one buzzy voice, so the
voice pulled every clip towards tense / sad. The machine check before handing over (noise / flatness per clip) passed and did not catch this.
Real clips (notes from real music) beat composed ones 12 vs 4: the composed note side is the weaker half on this reading.
Next (round 3): real clips through a piano-like voice (EMOPIA is piano), composed clips from the live Generate Samples node with its own
voice choice per mood instead of compose.py. Round 2 stays recorded as a miss.

### Finding while building C5 round 3: the full mix is drum-led (2026-10-29)
Owner, on the live generator's full mix (downtempo, seed 500, four moods): "all just drums". Measured by projecting the full mix on each
part rendered alone: the Drums layer explains 10-41% of the mix, Synths (bass + motif) 0-13%, Pads / Keys / Plucks / Leads ~0% (their
voices differ when rendered alone, so this undercounts them, but the ear agrees). Mood moves scale (major / minor pentatonic) and tempo
(89.7 -> 74.3 bpm), so the notes do change, but they sit under the drums. Round 3 composed clips therefore use melodic parts, no drums.
Open (product, not this test): the Full part's melodic layers are buried; balance check per style before any change ships.

### C5 round 3 built: machine check before listening (2026-10-29)
40 clips at eval/listen/c5_mood (key c5_mood_key.json, with each clip's source). None noisy (flatness <= 0.001), none silent.
Real (EMOPIA on the FM electric piano) separate by measurement: mode (Krumhansl major - minor) Q1 +0.27, Q4 +0.12, Q2 -0.05, Q3 -0.10;
onsets/s Q1 4.1, Q2 4.5 vs Q3 2.6, Q4 3.1. Composed (live generator, Keys part, mood / energy dots at +-0.8 / +-0.7) do NOT separate:
mode -0.03..+0.03 in every quadrant, onsets/s 0.6-0.7 in every quadrant (about 7 chords in 12 s). Reading: the energy dot does not reach
the Keys part's rhythm or density, and mood only swaps major / minor pentatonic, which barely moves the chord colour. Prediction before the
owner listens: real half well above chance, composed half near chance. If that holds, the fix is in SampleStyles (energy -> chord rate /
rhythm on every melodic part; mood -> full major / minor triads, not pentatonic), not in the test.

## Paused 2026-10-29: mood across sound, plus polish (SampleStyles)
- Mood and energy now also move voice choice (keys by quadrant), register, brightness, attack and hold, reverb and delay, tension (resonance, detune), chord progressions and rhythm, lead contour, and tempo. Zero dots stay byte-identical except for the new master high-pass.
- `tools/universe/mood_check.py`: 14/30 cues right on the old generator, 24/30 on the new one (2 seeds). Still missing: tense keys brightness, leads mode and maj_minus_min, the full mix's maj_minus_min, and pads/leads tail.
- Polish: mixes put 19% of their power under 40 Hz (DEAM songs: 1.2%) and are hollow from 250 Hz to 2 kHz. Just added a master 24 dB/oct high-pass at 30 Hz before the Limiter. **Not built or measured yet.**
- Next: rebuild the wasm (`nice -n 10 cmake --build build-wasm -j2`), re-run bands.py and mood_check.py, fix the octave −1 voices under low notes at the source, then work on the low-mid/mid balance. Don't copy anything into website/gen without asking.

## 2026-10-30 Plan item 2: next chord, small neural nets vs the n-gram (`chord_nn.py` -> `feel/chord_nn.json`)
Same held-out splits as chord_eval2 (test 20% never seen), 5 seeds.

| Model | Next-chord top-1 | Gain over most-frequent (points) | Cross-entropy (bits, lower is better) |
|---|---|---|---|
| n-gram (shipped) | 32.3% | 14.25 | 4.64 |
| LSTM | 30.6% ± 2.0 | 12.5 ± 1.6 | 4.09 |
| **Transformer** | **33.9% ± 1.6** | **15.8 ± 1.1** | **3.83** |

- The transformer is the first neural model to beat the n-gram on held-out data. Top-1 is ahead by about one sd, a small
  lead; cross-entropy is clearly lower, so its probabilities are better calibrated, which matters for item 3 (sampling with temperature for variety).
- Only next chord is scored here. Key, register, density per role and B5 variety need the generator to sample from the
  transformer (item 3) before it can replace the n-gram.

## 2026-10-30 Plan item 1: whole-mix balance at rest (`mix_bands.py`)
The dots are gone, so every take renders at mood and energy 0; the mood quadrant check (`mood_check.py`) no longer applies.
What was left of item 1 is the balance of the whole mix. `tools/universe/mix_bands.py` renders all 34 styles x 2 seeds
(Full part) and measures power share per band against two references: 150 DEAM songs (mostly rock, pop, classical) and
212 electronic full mixes from the sample library (pack song starters, construction-kit demos, music-kit full loops,
measured from the audio on the WD Black drive; cached at `~/Infinite/training-data/eval/lib_fullmix_bands.json`).

Each part alone matched its library loops before the change (`part_vs_library.py`: kicks low share 0.993 vs 0.986, bass
0.927 vs 0.935). The fault was the sum: kick and bass together held 42% of the full mix at 40-80 Hz, and the loudness match
pushed the mids down. Fix (`SampleStyles.cpp`, `fullLowDb`): in the Full part only, the kick (sample lane and synth kick)
and the bass sit 6 dB lower. No draw moves.

| Band | Before | -4 dB | **-6 dB (shipped)** | Library full mixes | DEAM |
|---|---|---|---|---|---|
| sub < 40 | 0.6% | 0.5% | 0.4% | 0.2% | 0.2% |
| 40-80 | 41.9% | 31.8% | **26.4%** | 21.6% | 7.8% |
| 80-250 | 26.7% | 26.2% | 25.0% | 30.9% | 33.0% |
| 250-500 | 6.4% | 7.4% | 7.9% | 10.2% | 15.4% |
| 500-2k | 4.3% | 5.8% | 6.7% | 9.5% | 20.4% |
| 2k-6k | 2.5% | 4.4% | 5.5% | 0.8% | 3.3% |
| 6k+ | 1.9% | 3.4% | 4.7% | 0.0% | 0.4% |

- 40-250 Hz together: 51.4% vs the library's 52.5%. The low end now matches; the low mids are still at 0.7-0.8x.
- New gap, now visible: the top end (2 kHz and up) is too loud, 10% of the power against ~1% in the library mixes and 3.7%
  in DEAM. Not the hats: the hat lane and synth hats 4 dB lower in the full mix moved 6k+ only 4.7% -> 4.2% and 2-6 kHz
  not at all, so that try was reverted. The source is still to find (per-channel mute renders of the full patch).
- The 30 Hz master high-pass measures as intended: sub-40 Hz is 0.4%.
- Gates: pads 102/102 identical, and the bass, kicks and synths parts are 102/102 identical too (`wasm_pad_dump.js`, new
  in `tools/universe/`, with `--cmp`). Full takes: 102 render, 0 clipped, 0 silent with redraws on. Pinned (no redraw),
  style 28 seed 9002 fails Master (silent or non-finite on the first draw); it failed the same way before this change and
  is still open.
- The wasm build needs `EMSDK_PYTHON=/opt/homebrew/bin/python3.12` (emscripten refuses the system Python 3.9).

## 2026-10-30 Plan item 3: sampling the chord transformer with a temperature (`chord_sample.py` -> `feel/chord_sample.json`)
Correction to the plan: B5 in `benchmark.py` scores drum grooves (cluster entropy of drum families), not chords, so chord
sampling cannot move it. Item 3 is measured in the bank's own terms instead: four chord roots as scale degrees. Variety =
entropy of the progressions at the real set's size (bar 0.90 x real, the B5 rule). Realism = Jensen-Shannon distance of
degree-bigram shares to the held-out 20%. Transformer retrained per split, 5 seeds, 2,000 samples per temperature.

| Source | Variety (bits) | x real | JS to held-out (lower = more real) | New progressions |
|---|---|---|---|---|
| Real (194 progressions) | 7.03 | 1.00 | floor: train vs test 0.296 | |
| Shipped bank (297, uniform draws) | 6.82 | **0.97** | 0.329 | |
| Transformer T=0.6 | 6.66 | 0.95 | 0.359 ± 0.033 | 87% |
| Transformer T=0.8 | 7.05 | 1.00 | 0.328 ± 0.022 | 92% |
| Transformer T=1.0 | 7.25 | 1.03 | 0.318 ± 0.016 | 96% |
| Transformer T=1.2 | 7.34 | 1.04 | 0.320 ± 0.015 | 98% |

- Chord variety already passes with the shipped bank (0.97 x real). The variety MISS on record (0.72 x) is the drum B5.
- The transformer at T=1.0 is as real as the bank (0.318 vs 0.329, inside one sd) and makes 96% new progressions, but it is
  not clearly better, so nothing ships (keep only if better).
- The limit is data: 194 labelled progressions. The next real step for the note model is more chord data: derive
  progressions from POP909 / Lakh MIDI (chord estimation per beat), then retrain and rerun chord_nn.py and chord_sample.py.

## 2026-10-30 Plan item 4: valence from DEAM audio (`deam_valence.py` -> `feel/deam_valence.json`): MISS, r 0.749
All 1,802 DEAM songs measured: timbre, envelope, onsets and notes from `frame_features`, plus key mode, triad colour,
chord change, tempo, beat and loudness or brightness over thirds. Held out by song, 10 splits.

| Target | Ridge | Gradient boosting | Extra trees | Harmony and tempo only (best) |
|---|---|---|---|---|
| Valence | 0.704 | **0.749 ± 0.027** | 0.737 | 0.705 |
| Arousal | 0.730 | **0.783 ± 0.024** | 0.769 | 0.737 |

- Valence misses the 0.8 bar (0.749), well above the notes-only EMOPIA fit (0.547) and at the top of the literature range
  for audio valence (about 0.6-0.7). Not re-scored with an easier bar.
- Audio + notes is this fit: the note features (pitch content, mode, chord colour) are inside the 0.749. Notes alone (harmony
  columns) give 0.705, so timbre and dynamics add about 0.04.
- Use: the valence model can score the generator's own renders (machine mood check), since it reads only audio.

## 2026-10-30 Plan items 7 and 8: String body tables, pick and bow controls: MISS (shipped as options, defaults unchanged)
Item 7: `string_body_fit.py` fits a body table (10 modes: frequency, Q, gain) from one instrument's recorded single notes
(dB average of the first second of every note, smoothed to 1/6 octave, tilt removed, peaks taken greedily). Fits against each
body curve: guitar r 0.68 (GarageBand classical guitar, 101 notes; box table 0.42), harp 0.80 (43 notes), violin 0.86 and
cello 0.75 (section pizzicato, 21 and 19 notes; box about 0). New String dropdown `body type`: box (default), guitar, harp,
violin, cello; each table loudness-matched to the box. Item 8: `hardness` (pick pulse 0.3 ms at 1, default, up to 3 ms),
`pressure` (bow friction slope 3.5 at 0.5, default; x2.8 steeper at 0, /2.8 at 1) and `noise` (rosin noise in the bow speed, 0
default). Old presets render unchanged: `own_sweeps.py check --node String`, worst mel r 0.99986 (gate 0.999).

Family test (`string_body_test.py`, `string_families.py`'s ratio: median distance from each library sample to the nearest
render over the family's own spacing; lower is closer; 400 base presets, C3):

| Family | box (today) | guitar | harp | violin | cello | pluck: box | pluck: harp |
|---|---|---|---|---|---|---|---|
| guitar | 7.15 | 7.09 | 6.77 | **6.72** | 6.69 | 7.91 | 7.55 |
| harp | 5.56 | 5.59 | 5.28 | **5.22** | 5.38 | 6.17 | 5.88 |
| violin | 7.12 | 7.04 | 6.73 | **6.68** | 6.84 | 8.49 | 8.38 |
| cello | 4.47 | 4.59 | 4.12 | **4.05** | 4.29 | 7.11 | 6.68 |
| piano | 7.17 | 7.19 | 6.64 | **6.57** | 6.74 | 7.86 | 7.26 |

Bowed (300 presets, every one bowed): box 7.02 / 4.12 (violin / cello), violin body 6.77 / 3.98, light pressure 6.85 / 4.04,
heavy 6.88 / 4.03, noise 0.4 6.86 / 4.25, cello body with heavy pressure and noise 7.25 / 4.73.

- The bar (guitar well under 4) is missed by far. A fitted body lowers every family by 0.1-0.5 (5-8%), and the guitar table does
  not even help the guitar most, so the body is not what keeps String from the guitar family. Bow pressure and noise move the
  ratio by under 0.15. Kept as options because each is a little better and the defaults are today's sound exactly.
- What limits the ratio is outside these knobs: renders are at C3 only while library samples are at every pitch, and the
  string's own spectrum and decay (loop loss, excitation) put it away from the recordings. Next: rerun on all STIM notes, and
  fit the loop loss (tone vs frequency) from the same recordings, before more body work.

## 2026-10-30 Plan item 9: Wind node (clarinet, sax, flute, brass): shipped, family bar MISS
New node `Wind` (Synths; `src/nodes/WindNode.*`): bore delay line closed by a reed (clarinet), a reed at 0.4 of a conical bore
(sax), an air jet with a cubic jet table (flute) or a lip resonator (brass, constant Q 25, DC gain 3, tuned 2.5-3.5% under the
note); bell loss, breath noise, 5.5 Hz vibrato, a 40 ms tongue overshoot of the pressure at note-on; 8 voices; controls
instrument, breath, tone, noise, vibrato, attack, release, volume. Constants trimmed with `wind_proto.py` (numba twin of the
C++ tick, no rebuild per try) and checked in the real node with `wind_tune.py` (held note every 5 semitones, YIN):

| Instrument | Range | Speaks | Pitch (median, breath 0.5 / 1) | RMS | Peak |
|---|---|---|---|---|---|
| clarinet | 50-86 | every note from breath 0.1 | +1 / +1 c | -9 / -8 dB | 0.77 |
| sax | 49-81 | every note from breath 0 (one 2-octave jump at 54, breath 1) | +2 / 0 c | -10 / -9 dB | 0.84 |
| flute | 60-96 | every note from breath 0.1 | 0 / 0 c | -8.5 / -8 dB | 0.63 |
| brass | 40-82 | every note from breath 0 | +12 / +8 c (low notes up to +50 c at breath 0: lip pull) | -15 / -13 dB | 0.9 |

Fixes on the way: the flute's jet table was clipped flat (pressure 1.3-1.8 into x(x^2-1)) and it overblew to the octave until
the loop was two periods; the sax needs pressure just above its threshold (0.6) and the reed further down the bore; the brass lip
resonator's DC gain grew as 1/w^2, so low notes saturated open and stayed silent. Param sweep: all 8 params pass (release
through `SweepPostAlter`: the sweep's first held note arrives before the inbox consumer registers, so the release waits for a
sounding voice). Teardown sweep: Wind passes (the run then stalls after Convolution Reverb, unrelated).

Family test (`string_families.py --set wind`, 400 presets at C3 and C4, ratio as for String; lower is closer):

| Family | samples | Wind | Wind before tongue + gains | Wavetable | Analog | String |
|---|---|---|---|---|---|---|
| clarinet | 122 | 5.95 | 5.97 | **5.08** | 5.97 | 7.32 |
| sax | 253 | 5.69 | 6.13 | **5.23** | 6.36 | 7.34 |
| flute | 565 | 5.02 | 5.22 | **4.13** | 4.39 | 6.60 |
| brass | 2271 | 6.61 | 7.12 | **5.94** | 6.84 | 8.96 |

- MISS: no family passes, and Wavetable lands closer on all four. Per-axis gap from each library sample to its nearest Wind
  render (median z): attack -0.6 to -1.2 (the recordings peak sooner), loudness +0.6 to +1.2, density +0.2 to +0.9,
  brightness +0.3 to +0.5. The tongue overshoot and +3 dB moved every family by 0.0-0.5.
- Same limit as String: renders at two pitches against libraries at every pitch, and 400 random presets over a narrow engine
  against Wavetable's huge space. Next: render the families' own pitches (STIM notes), a brighter breath (a reed-table slope
  per breath), and an onset noise burst; then rerun.

## 2026-10-30 Plan item 11: pitch from audio, harmonic sum (`detail.hsum`, `pitch_check.py` -> `eval/pitch_check.json`): MET except mallets
`detail.hsum`: subharmonic summation (Hermes 1988). The averaged amplitude spectrum goes onto a 10-cent log grid, taking the max over
±30 cents so stretched piano partials still count. Score(f) = sum over 12 harmonics of 0.84^(h-1) A(h f), for f from 27 Hz to 2.1 kHz.
`detail.measure` now also returns `f0_hs`; `samples_v3.py` takes its pitch class from `f0_hs` first and falls back to YIN.
Test set: drive files named with exactly one note+octave (no loop/bpm/chord/arp in the name), grouped into 10 families by path,
160 per family at seed 7 (1,600 files, 1,580 measurable). Pitch class only, because libraries disagree on which octave is C3.
The rule (YIN, harmonic sum, or harmonic sum where the two disagree above a salience threshold) was chosen on split A; the
report is on split B:

| Family | n (B) | YIN | harmonic sum (shipped) | bar 90 |
|---|---|---|---|---|
| bass | 84 | 89.3 | 92.9 | PASS |
| brass | 80 | 98.8 | 100.0 | PASS |
| guitar | 76 | 96.1 | 96.1 | PASS |
| mallet | 79 | 72.2 | 78.5 | MISS |
| organ | 83 | 98.8 | 100.0 | PASS |
| piano | 77 | 90.9 | **96.1** (33% in the old 6-file count) | PASS |
| strings | 70 | 98.6 | 97.1 | PASS |
| synth | 72 | 88.9 | 95.8 | PASS |
| vocal | 80 | 100.0 | 100.0 | PASS |
| wind | 79 | 98.7 | 100.0 | PASS |

- Split A chose the plain harmonic sum (family mean 96.0% against YIN's 93.1%). A YIN fallback only lost accuracy.
- Mallets miss. 14 of the 26 misses are one synthetic "Error Bell" pack named down to G0, and the rest are tubular bells,
  glass xylophone and a vibraslap. These are inharmonic sounds with no single harmonic pitch. Without bells and slaps it is
  84.2%, but on only 19 files, so this is a recorded miss and not a claim.
- `frame_features` (loop/take features) has no f0, and one f0 means nothing for a loop, so the estimator lives in the
  single-note path (`detail.py`).

## 2026-10-30 Plan item 20: local learning log (`src/app/graph/LearningLog.*`, `INFINITE_LEARNINGTEST`): MET, two deviations
Close doc §5.1 and §5.2 are now app code. Everything goes under `AppSupportDir()/learning/`:

| File | Written when |
|---|---|
| `sessions.jsonl` | app start and end, and when the switch changes: session id, time, app version, learning on/off |
| `takes/YYYYMMDD.jsonl` | every Generate Samples take, once it has an outcome. Fields: part, style, landed seed, attempt, dots, bpm, key, follow flags |
| `audio/YYYYMMDD.jsonl` | a take is kept: its RoleCritic frame features (132 numbers), never the audio |
| `midi/YYYYMMDD.jsonl` | notes played in from any MIDI device: on/off, velocity, beat, tempo, key, scale, playing; flushed every 10 s |
| `patches/<hash>.inf` + `index.jsonl` | a user save: patch text stored once per FNV-1a hash, one index line per save |
| `profile/user_profile.json` | rebuilt on a background thread when older than 20 h, checked every 10 min |

Outcomes:
- **kept**: the node's "keep as file" export, or a take leaving the Sample studio (export, slices, to patch, to node, to Samplers). Kept after a take settled still counts, as its own line.
- **rerolled**: a new take from the same node.
- **edited**: the node's settings moved off the take within 30 s.
- **deleted**: the node was deleted.
- **ignored**: nothing happened for 45 s.

Not counted as new takes: a reopened patch re-rendering its saved take, and a take carried from the studio to the canvas.

The profile has these blocks:
- taste: keep rate per part, style and part|style, trust n/(n+20)
- sound: per-part median of the kept features
- harmony: pitch-class, key, scale and chord-size shares
- melody: step/leap/repeat shares, range, velocity, 16th onset grid

Settings > Learning has:
- "Learn from my playing" (saved as `learnFromPlaying`). Off drops pending takes, stops every writer and the movement log, and leaves existing files.
- "Open learning folder", with the folder size.
- A two-click "Forget me" that deletes the folder.

Raw logs share a 500 MB cap, oldest first; `profile/` is never dropped. Logging is off in headless jobs and whenever any
`INFINITE_*` variable is set. `INFINITE_LEARNING_DIR` points it at a test folder and turns it on.

`INFINITE_LEARNINGTEST` PASS, 11 checks:
- five takes with one of each outcome
- nothing written while off
- a patch saved twice stored once
- profile counts takes, kept synths and features
- a C major triad read as one 3-note chord, C D E G as steps
- Forget me removes the folder

Deviations and limits:
- MIDI goes to plain JSONL, not the movement log's compressed writer: no MovementLog record type was added. A day of playing is a few MB.
- Audio features are the role critic's 132 frame features, not the "32 bands + centroid, crest, onsets" in the close doc, because they are already in the app and are what the critic refit (item 21) reads.
- The movement log stays in its own folder; it was not moved under `learning/`.
- The voice source (Vital / designed / bank line) is not known where a take lands, so taste has no voice-source block yet.
- The MIDI reader itself is not exercised by the test (no device); its profile side is.
- Nothing reads the profile yet (item 21).

## 2026-10-30 Plan item 22: starter profile and a Forget me that holds (`tools/universe/starter_profile.py`, `resources/starter-profile/`): MET, one caveat

- **What ships.** `starter_profile.py` copies the eight derived prediction files (note style, rhythm style, velocity, quantize, tempo bins, colour stats, Generate taste and taste vector) from the owner's `prediction` folder into `resources/starter-profile/` with a `manifest.json` (bytes, sha1). The tempo profile keeps only its `bin` lines: the `file` lines named the owner's files and are stripped. No audio, no MIDI, no patch, no path. CMake bundles the folder on all three platforms next to `lang/`.
- **Seeding.** `LearningLog::Start(BundledResourcePath("starter-profile"))` runs before the profiles load. `SeedStarter` copies the starter only when the prediction folder has none of the profile files and the marker `AppSupportDir()/Infinite.starter-profile` is absent, then writes the marker. The marker sits outside `learning/`, so Forget me does not bring the starter back, and a machine that has learned is never overwritten.
- **Forget me now holds.** It deletes the learning folder, the nine prediction files (the eight above plus `generate_seen.txt`), the movement-log folder's contents, and resets the in-memory Generate taste (`UserStyle::ResetTaste`). The prediction saves at quit (`main.cpp`) and the periodic autosave (`PatchIO.cpp`) are gated by `LearningLog::MaySaveProfiles()`: false with learning off or after Forget me, so the profiles the session still holds in memory are not written back.
- **Test.** `INFINITE_LEARNINGTEST` now has 15 checks, all pass: the 11 from item 20 plus seeding an empty folder, no reseed once the marker exists, no seed over a folder that has learned, and no profile saves after Forget me. A test folder (`INFINITE_LEARNING_DIR`) makes Forget me delete only that folder; the real prediction folder was checked intact after the run.
- **Caveat.** The source files were last written 05:28 and 03:31 today, and self-tests ran on this machine around then; headless runs never save the profiles, but GUI self-tests may have. The starter may carry a little test noise. It is a prior, not a model: the first real sessions outweigh it.
- **Not done.** The movement log's open session file may survive Forget me on Windows (open handle) and stays until the next Forget me; its retention pass only prunes above 1 GB. With learning still on, the session keeps logging into fresh files after Forget me, by design.

## 2026-10-30 Plan item 21: models learn from the logs (`tools/universe/learn_from_logs.py`): BLOCKED on data, learner built and tested

- **What the log now carries.** The compiler lists what each take drew in `Compiled::picks`: `prog:<i>` and `line:<i>` (note bank rows), `voice:vital:<role>:<row>` and `voice:designed:<voice>`. Recording draws nothing from the random streams, so takes are unchanged. `TakeMade` writes them as `picks` on the take line. `INFINITE_LEARNINGTEST` checks the compiler lists voice and bank picks over eight Synths takes (16 checks, all pass).
- **The learner.** Per part and pick, a keep rate shrunk toward the base rate (20 pseudo-takes), as a weight clipped to 0.5..2 and normalised inside its pool (no pick ruled out). Voice shares: kept rate of Vital vs designed voices per part. Gate (close doc §5.3, "keep rate up on held-out sessions"): last quarter of sessions held out; the self-normalised importance estimate of the keep rate under the new weights must beat the held-out keep rate, and so must its bootstrap 5th percentile. `learned_weights.json` is written to `learning/profile/` only when the gate passes.
- **Fixture (`selftest`).** 24 sessions of 20 takes. With a hidden preference (line 3 kept 55%, others 20%) the gate passes (held keep 0.208, estimate 0.261, p5 lift 0.019) and line 3 tops the weights. With no preference it refuses (p5 lift -0.009). With 40 takes it says "not enough data".
- **The real log.** 0 takes so far: the log started today and only self-tests have run. Verdict "not enough data" (needs 200 training takes and 60 held out). This item stays open until the owner has generated a few hundred takes.
- **Not done.** Nothing in the app reads `learned_weights.json` yet; the draws in `BankLine`/`BankProg`/voice choice stay uniform. The spectral gate refit and the role critic refit (≤ 3% false rejects on kept takes) need about 200 kept takes with features; the learner only counts them. Wiring the weights in is a small change once a real log passes the gate.

## 2026-10-30 Plan items 5-6: what each effect knob does (`fx_sweeps.py`, `note_fx.py` -> `feel/fx_map.*`, `feel/note_fx_map.*`): 20 of 27 MET

Per effect: 12 banks, each one base (every knob drawn in its range) and one move of each knob; the source is a fixed set of parts
(audio effects) or six phrases through Keyboard -> node -> sine (note effects), read back from the audio. A (knob, measure) cell is
reliable when >= 80% of its training moves agree; held out by part / phrase. Bar 90%.

| Kind | MET | MISS |
|---|---|---|
| Audio (14) | Chorus .957, Delay .937, Drive .920, Dynamics .975, Flanger .948, Limiter .993, Phaser .974, Saturator .964, Stereo .914 (35 moves) | Audio Filter .693, Bitcrush .897, EQ .873, Reverb .861, Transient Shaper .851 |
| Note (13) | Arpeggiator .949, Chorder .934, Gate .947, Humanizer .955 (67), Note Echo .934, Note Filter .998, Note Stack .942, Note Strum .966 (59), Note Transpose .958, Pitch Bend 1.0 (48), Velocity Curve 1.0 (52) | Glide .893, Quantizer .878 |

- **Note Stack had 0 test pairs**: its `semiN` knobs only reach the output when `enabledN` is on (the node says so in
  `SweepPrerequisitesFor`), and the sweep left all eight voices at their default, off. Every move changed nothing. `note_fx.py` now
  fixes `enabled0-7` on; rerun 648 rows, **.942 on 447 moves**. The voices-off rows are kept as `notefx_note_stack.voices-off.jsonl`.
- **Chorder** rendered 0 before (it has no note input, so the phrase only sets the tempo); the `GENERATORS` rule fixed it: 720 rows, **.934**.
- **Arpeggiator** printed no per-bank lines because its 432 rows (12 banks x 6 phrases x 6 designs) were already on disk: the rerun skipped them all. Nothing missing.
- The misses are near the bar except Audio Filter (.693 on only 75 moves): its type knob changes what cutoff and resonance do, so one direction per knob is the wrong model, the same reading as Wavetable (item 14).

## 2026-10-30 Plan items 10 and 12: Piano node family test MISS; nothing added to Generate Samples

The Piano node (49a9deba) shipped as a node. Family test (`own_sweeps.py run --node Piano --count 400 --notes C3,C4`, then
`string_families.py --node Piano`; ratio as for String, lower is closer, bar well under 4):

| Family | samples | nearest render (median z) | inner distance | ratio |
|---|---|---|---|---|
| piano | 3173 | 1.95 | 0.31 | 6.26 |
| guitar | 3180 | 2.40 | 0.35 | 6.93 |
| harp | 876 | 2.31 | 0.42 | 5.54 |
| pluck | 759 | 2.51 | 0.47 | 5.30 |
| cello | 363 | 2.29 | 0.42 | 5.50 |
| violin | 678 | 2.94 | 0.34 | 8.61 |

- MISS. Piano is its own nearest family (1.95, the lowest of the six), so the node sounds like a piano to the measure, but the
  ratio is 6.26, no better than String or Wind against their families. Same limit as items 8-9: two render pitches against
  libraries at every pitch, and 400 random presets. Not chased further (owner: stop work that is not leading anywhere).
- Item 12: no instrument passed its family test (String, Wind, Piano), so none was added to Generate Samples. Pads unchanged.

## 2026-10-30 Plan item 13: overnight knob -> sound rounds (run on the MacBook Air): String MET, Wavetable MISS, Sampler not run; stopped by the owner

`overnight_rounds.py` on the Air (summary `~/Infinite/training-data/models/overnight/20261030.jsonl` there). Median held-out R² of the
characteristic means, head; bar 0.6:

| Engine | base | round a | round b | round c | result |
|---|---|---|---|---|---|
| String | 0.846 (2,266 presets, 9 of 11 at bar) | 0.905 (+0.059) | 0.859 (-0.046) | 0.863 (+0.004) | MET at base; stop rule fired, rounds added nothing lasting |
| Wavetable | 0.002 (5,490, 0 of 11) | 0.230 (+0.228, 7,490) | not scored | - | MISS |
| Sampler | - | - | - | - | not run (240 presets) |

- The first run at 04:57 scored nothing (head null for every round) and was rerun; only the second run is in the table.
- Wavetable: the round-b sweep was under way when the overnight process ended at 09:26. The owner had chosen to stop after the
  Wavetable round and before Sampler, as the rounds no longer pay off and nothing that ships reads this model. A watcher
  (`~/stop_after_wt.sh` on the Air) stopped any Sampler step; the round-b rows that landed stay in `own_wavetable.jsonl` there.
- Why Wavetable stays low: one knob move does not have one direction on Wavetable (item 14 measures how often a move disagrees
  with itself), so more presets of the same kind do not lift R².
- No cross-engine model was built.

## 2026-10-30 Plan item 17: 909 patterns and MPC / SP-1200 swing in the Drums groove (`tools/universe/groove_909.py` -> `GenerateSamplesGrooves909.inc`): MET

- **Bank.** `groove_909.py` reads the Drumazon 2 pattern library (`.dr2seq`, straight 1/16 only; triplets skipped). A 32-step
  pattern gives both halves. A figure needs a kick and a hat (fills and intros are left out). The result is 274 unique 16-step
  figures, 157 of them four-on-the-floor. Lanes: kick, snare, closed hat (ride when the pattern has no closed hat), open hat, clap.
  The 909's accent is not kept.
- **Swing.** The ten 16th swings named in Ableton's MPC 3000 and SP 1200 swing grooves, converted to DrumSequencer units
  (odd 16ths late by `swing * 0.5` steps, so p% is `4p/100 - 2`): 0.16 to 0.96.
- **In the generator** (`SampleStyles.cpp`, Drums groove): on a four-on-the-floor card, half the takes take their closed and
  open hat lanes, and the clap when the figure has one, from a 909 figure. Kick and snare stay with the card's own groove. A
  swung card (swing > 0.02) plays a machine swing within 0.12 of its own, else the nearest; straight cards stay straight. Both
  draws use their own random streams, so nothing else in a take moves.
- **Gate holds.** Shipped spectral gate on 40 clean takes per part, same seeds, before vs after: drums 0/40 -> 0/40, kicks,
  snares, hats, toms, percussion 0 -> 0, full 1/40 -> 1/40. 23 of 40 drum takes, 8 hats takes and 15 full mixes changed.
  Seven synths / pads / plucks takes and two snares takes moved by at most 0.6% in one band with RMS unchanged to 4 digits:
  render noise, not this change.
- `groove_bank.py`'s inc is unchanged; the 909 bank sits beside it.

## 2026-10-30 Plan item 14: Wavetable knob direction, repeat test (`tools/universe/wavetable_repeat.py` -> `feel/wavetable_repeat.json`): MISS stands (79.3%), and render noise is ruled out

The question: is 90% out of reach because a Wavetable move disagrees with itself from render to render? 100 one-knob pairs from
`own_wavetable.jsonl` (seed 0, phase knobs left out) rendered again at C3 with both oscillators' start phase turned by 0.37 of
a cycle, a change no listener hears. Every characteristic the stored move changes clearly (>= 0.3 spread) must move the same way.

| | |
|---|---|
| clear axis moves | 143 |
| flip direction | 8 (**5.6%**) |
| still clear after the turn | 90.2% |
| a take vs its own re-render, median change | 0.003 spread (3.4% of axes reach 0.3) |

- Renders agree with themselves: the ceiling from repeat noise is about 94%, above the 90% bar. The guess in NEXT-SESSION
  (about 20% disagreement, so 90% unreachable) is wrong.
- The flips sit on knobs whose effect depends on other settings: a.table 1/3, a.warpMode 1/4, b.ampDecay 1/5, b.filterType 1/7,
  b.unison 1/10, b.resonance 1/13, b.table 1/20, a.pan 1/1.
- So the 79.3% is the model's limit: one direction per knob, while on Wavetable a move's direction depends on the table, warp and
  filter type. The conditioned models already tried lost (HANDOFF, "Wavetable knob model that uses the other knobs"), and the
  item 13 sound model reached only 0.23 R². The bar stays a MISS. Reaching it needs a model per table / warp mode, or more
  data per region, not cleaner renders.
