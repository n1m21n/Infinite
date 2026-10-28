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
