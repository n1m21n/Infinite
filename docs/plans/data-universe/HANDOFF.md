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
