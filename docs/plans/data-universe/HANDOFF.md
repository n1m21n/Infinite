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

**Status 2026-10-28:** P1 approved by the owner (`CHARACTERISTICS.md`, 18 axes + space added). P2 done: every
held source measured, coverage and cross-form agreement in `CHARACTERISTICS.md`; brightness / sharpness agree 0.96 / 0.92 between mel
and band forms of the same render. P3 first pass (`p3_characteristics.py`, Diva v2 rows, C3, 1 seed): 0 of 11 characteristic
means at the 0.6 bar; median head R2 0.153 random, 0.082 group (best: attack 0.44, density 0.23 on group). Not the renders: repeat
r >= 0.97 on every axis, so the ceiling is about 1. Not one knob either: the strongest single knob explains |r| 0.28 of brightness
(VCF frequency) and 0.15 of warmth, so the sound comes from knob interactions. Presets alone add 0.04-0.07 per doubling (7+ doublings
to the bar). Running now: D2 one-knob variants vs D1 more presets at 1,000 renders each, plus knob trims (`SOUND-MODEL.md` 4b).
P4 dry run (`p4_shared.py`, band form, 3,000 rows per source, 6 spectral axes): samples and Air cover each other (0.44 / 0.37 of rows
have a neighbour as close as their own median, self = 0.50); Diva and samples 0.26 / 0.39, up from 0.16 / 0.27 once loudness (a
per-source offset) is left out, the rest is content (samples hold drums and loops, much lighter low end). Plug-in renders form their
own island (coverage 0): effects measured on one broadband test signal, so they stay out of the instrument space.

Data rule for every phase: collect only what a learning curve or an error says is missing (section 4).

## 4. Rules that keep the project honest

- No data collection without a learning curve or error diagnosis that names the data, its budget and its proof (`SOUND-MODEL.md` 4b).
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
