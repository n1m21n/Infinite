# Revised plan: plug-in data collection, restart point for the next session (2026-10-28)

**Superseded as the entry point by [SOUND-MODEL.md](SOUND-MODEL.md) (goal, milestones M0-M5, order).** This file stays as the record of the plug-in render work; its section 6 steps 1-4 are still the data steps that plan calls on.

## 1. Documents this plan builds on

| Document | What it holds | Status |
|---|---|---|
| `README.md` | Tracker of the whole data universe: stages 1-7, reader row counts, EXS families | Stale on the plug-in work. Its top diagram is Mermaid, which the owner cannot read: replace with ASCII. |
| `plugin-plan.md` | Three coverage levels, per-plug-in library sizes, splicer table, efficiency rule | Current. Its "Order" list is superseded by section 6 below. |
| `HANDOFF.md`, `NEXT.md` | Earlier handoff and next steps | Written 2026-10-27, before the headless-state work. |
| `next-phase.md`, `training-plan.md` | Next phase of data and how the model is trained | Not re-read this session. Check before the Opus design session. |
| `axes-strategy.md`, `dials.md`, `recipes.md`, `groove-2h.md` | Sound axes, dial choices, recipe format, groove work | Background. `recipes.md` defines the recipe rows the join uses. |
| `ANTIGRAVITY_TASKS.md` | Task list handed to another agent | Not re-read this session. |
| `docs/plans/perf/README.md` | Perf tracker | Unrelated to this work. |
| `tools/universe/collect_check.md` | Collection check procedure | Use for the final audit. |

## 2. Overall goal

A generator and a set of nodes whose results beat the benchmarks because every preset, map, groove and recording we own has
been measured and placed in one space: **recipe (knobs) -> measured sound -> instrument -> cluster -> genre -> full mix**.
The plug-in render work supplies the "measured sound" half for synths and effects. Nothing raw leaves the machine; only feature
rows go to `~/Infinite/training-data/`, each with a licence.

Success is **a model that learns the knobs-to-sound map and generalises to presets it never saw**, not a large row count.

## 3. Goal of each task

| Task | Goal | Tool | State today |
|---|---|---|---|
| A. Level 1 | Default sound plus 16 knobs at 0 / 0.5 / 1 for every plug-in (404) | `render_pass.py --sweep-params` | 234 of 404 done as of last plan check |
| B. Plug-in states, no GUI | Template state per plug-in, from the CLI | `plugin_states.py`, `--plugin-state` | Done in code. Air collector had 179 plug-ins, 17 key synths. |
| C. Level 3 (preset through the plug-in's own state) | Exact sound for each preset | `preset_state.py`, `omni_render.py`, `nks_state.py` | Diva running, Omnisphere restarted, Hive/Repro/Vital partly done, others untested |
| D. Level 2 (preset knobs through the host list) | Fallback where state is binary (Serum 2, KORG, effects) | `preset_render.py` | Not built |
| E. Render order | Farthest-first so any prefix covers the library | `coreset_order.py` | Diva only |
| F. Note economy | Render 3 notes, predict 3 | `note_predict.py` | Measured on Diva; applied to Diva and Omnisphere |
| G. Marginal-value gate | Stop each library when more renders stop helping | `marginal_gate.py` | Built; run on Diva only |
| H. Merge + audit | One dataset on the drive, coverage audit | `coverage_audit.py` | Not started |
| I. Handoff | Document for the Opus design session | this file | Written |

## 4. Review and analysis so far

**Worked**
- Headless state capture removes the GUI step (the GUI on the Air crashed).
- Splice formats solved for Diva, Hive, Repro (u-he), Vital and TAL (JUCE), Massive X and Pigments (NKS variants, from probe).
- Cost model: render about 3 s fixed + 0.3 s per audio second. Analysis negligible.

**Measured (Diva, held out)**
- Coverage radius of the farthest-first order falls slowly with no knee: 0.058 of start at 100 presets, 0.035 at 1,000, 0.024 at 3,000, 0.011 at 8,000.
- Predict other notes from C3 alone: spectrum R2 0.85, scalars 0.77, envelope 0.66. Adding C4: +0.03 to +0.04. Copying C3 scores 0.61 / 0.57 / 0.54.
- Knobs -> sound (ridge on random-feature knobs, 2,468 presets): R2 0.15 at 400, 0.21 at 800, 0.25 at 1,600, 0.27 at 2,468. Gain per doubling is falling (0.058 -> 0.042 -> 0.020) but still above the 0.01 stop line.

**Failures found, so the next session does not repeat them**
1. **Omnisphere rows were empty for hours.** `--lead 60` gave 0 of 20 patches with features; `--lead 30` gave 17 of 20. About 34,000 metadata-only rows were produced before anyone opened the file. They are in `~/Infinite/training-data/audio_bad/` on the Air. Lesson: after starting any long job, open the first rows and count fields, not just the row count.
2. **Serum 2**: nine splice variants all rendered the default sound. Level 2 is the route.
3. **Arturia jobs skipped**: the state collector had no VST3 states for them yet.
4. **Collection too slow with AU + VST3 together**: VST3 first, instruments first.
5. **Air hostname does not resolve** from the Pro. Use `namansoni@192.168.29.223` (Namans-MacBook-Air-5.local). The address can change; find it with `arp -a` and an ssh probe.

**Not verified**
- Silent patches: 3 of 9 first Omnisphere rows lacked features. Probably silent patches, not confirmed.
- Hive, Repro, Vital, TAL, Massive X, Pigments row quality (features present? presets sound different?). Run the check in section 7 step 2.
- Whether predicted notes help or hurt the downstream model.
- The proxy that fills unrendered presets: never trained.
- The gate's number is a ridge/random-feature yardstick. A real model will reach a higher score; the stop line should be re-set against the real model.

## 5. Help needed

| From | What |
|---|---|
| Owner | Keep the Air awake, plugged in, Remote Login on, and a Terminal window allowed to read `/Volumes/Wd-Black` (drive access needs the Terminal grant). |
| Owner | Say which plug-ins matter most for sound. Without that, the order follows library size and the gate. |
| Owner | Decide later whether predicted rows are used for training (section 8). |
| Opus session | Design of the knobs-to-sound model and the proxy; review of the gate's stop line. |

## 6. Revised order (cheapest value first, each step gated)

1. **Verify what exists** (30 min, no rendering): feature check on every output file; list per library `presets rendered / presets available / R2 / next-doubling gain`.
2. **Finish Omnisphere on the Air** at lead 30, 3 notes. Run the gate at about 1,500 patches; stop when two doublings gain under 0.01.
3. **Diva on the Pro** to 3,000 farthest-first presets. Run the gate at 3,000. Stop there unless the gain per doubling is still above 0.01.
4. **Hive, Repro, Vital, TAL, Massive X, Pigments**: probe (2 presets sound different and different from default), then render a farthest-first prefix of about 500, run the gate, extend only if it passes.
5. **Gaps with no level 3 route**: level 1 only for Triton Extreme, Zenology, Spire, TAL Sampler, Wurli V3, SubLab, Guitar Rig 7. Serum 2 and KORG via level 2, built only if the gate on a 100-preset pilot shows gain.
6. **Merge, `coverage_audit.py`, update README tracker**, write the data card for the Opus session.

## 7. Session-start checklist

1. Read `docs/NOW.md`, this file, then `plugin-plan.md`.
2. On both machines: `wc -l` and a feature-field count for every `omni_t*.jsonl`, `preset_*.jsonl`. Zero features means stop the job.
3. Find the Air (`arp -a`, ssh probe), check the shards, `air_queue.log`, `plugin_states.log`.
4. Run `marginal_gate.py` per library and write the result into the tracker.
5. Commit tools; never push the chat corpora; keep the disk footprint low (Air about 12 GB free).

## 8. What could be better

| Weakness | Better |
|---|---|
| Rendering is the cost. Each library is rendered preset by preset. | Keep the gate. Add a persistent host (one process, many presets) only if a library passes the gate and the fixed 3 s per run dominates. Unverified. |
| Predicted notes cost 0.03-0.15 R2 on the weaker features. | Train a nonlinear predictor and test velocity. Keep predicted rows tagged `predicted` with their held-out score. |
| One-size recipe join by file path. | Join by `src`, check match counts per library and report unmatched rows. |
| The farthest-first order uses knob values only. | Add sound features once enough rows exist (greedy on both), then compare coverage. |
| Silent or broken patches waste renders. | Pre-screen: skip patches whose template state is a sample-less init. |

## 9. How this raises the model's quality

- **Variety over repetition**: farthest-first puts the most different presets first, so the same budget covers more of the space.
- **Real renders on the information-bearing notes** (C3, C4, C3f); predicted rows only for the rest, labelled, droppable.
- **Held-out presets for every claim**: no number in this plan counts unless it was scored on presets the model never saw.
- **Gate per library**: effort goes where the next doubling still helps; nothing runs on a calendar.
- **Final evaluation** for the model: train with and without predicted rows, compare on real rendered rows, and use the difference to settle section 5's decision.

## 10. How the pieces fit (plain language)

### Flow of work

```
 COLLECT              ORGANISE                  TRAIN                 FINAL MODEL
 read every file  ->  one row per instrument -> note models,        -> generator that picks
 render plug-ins      place on axes, cluster     sound models,          notes + instrument + sound
 measure audio        link recipe to sound       judged on held-out     for (genre, mood, section)
 (this plan)          (atlas, clusters, matrix)  gates B1-B10           and passes the benchmarks
```

| Stage | Where we are (2026-10-28) |
|---|---|
| Collect | Drive files mostly read; audio pass about 74% (75,760 of 102,239 files, 2026-10-27); plug-in renders in progress (this plan) |
| Organise | Atlas of 14,161 instruments and clusters built, on a thin audio base; must be re-run after collection |
| Train | Models train on MIDI only today. **No plug-in render has trained anything yet** (`next-phase.md` P1) |
| Final model | Not built. Needs the linked model of `next-phase.md` |

Share of work that is collection: my estimate, not measured: about half the remaining effort is collection (plug-in renders are the
slow part, hours of machine time), about a quarter organising (join recipe to sound, matrix, clusters), about a quarter training and
evaluation. The plug-in renders feed organise and train, so stopping them early (the gate) is what frees the time.

### Why the data is useful

- Knobs -> sound: how a preset's settings turn into measured sound, so the generator can choose settings for a wanted sound.
- Sound -> words: which sounds people call warm, spacious, glitchy (tags on presets give the words).
- Words -> nodes: which Infinite nodes reproduce that character (the character x node matrix).
- Guardrail: critics and benchmarks judge generator output against real instruments.

### Matrix and axes

- **Axis** = one measured direction that describes a sound, such as brightness (spectral centre), attack time, loudness, roughness,
  or for affect arousal and valence. `axes-strategy.md` proposes four layers: perceptual, musical structure, affect, words.
- **Matrix** = a table that lines two lists up. In this project: character (rows: warm, spacious, glitchy...) by Infinite node
  (columns). Each cell is how much more often presets with that character use that node: log2 of P(node | character) / P(node)
  (`character_matrix.py`, kept only where at least 30 presets support it).
- A second matrix is recipe by sound: each row a preset, columns its knobs and its measured features. That is what the renders build.

### What "learning sound" means here

```
 knobs + note + velocity  --render-->  measured sound (spectrum, envelope, attack, loudness)
        recipe                                   features
           ^                                          |
           +------------- the model learns -----------+
 forward: recipe -> predicted sound      inverse: wanted sound or word -> recipe
```

The model is never given audio samples to imitate. It learns the map between settings and measured sound, so it can propose
settings. That is why held-out presets are the test: a good model scores well on presets it never saw.

### Instrument generation

```
 (genre, mood, section)  ->  sound cluster  ->  pick an existing preset or sample map   [works with data now]
                                            ->  build a playable sample instrument      [from the library; approved]
                                            ->  propose new knob settings               [needs the learned map above]
```

A wavetable generator is approved to improve the model, not to ship as a feature (`next-phase.md`).

### Audio analysis

Each sound is reduced to a row of numbers (`render_pass.py`, `audio_features.py`):

| Group | Fields now | Planned (`next-phase.md` P2) |
|---|---|---|
| Spectrum | 32 band energies, centroid, rolloff, flatness | MFCC, perceptual (ERB) bands |
| Time | attack, decay, envelope (40 points), onsets, crest | harmonic/percussive split |
| Level | rms, peak, low/mid/high share | loudness (LUFS) |
| Pitch/key | none in render rows | pitch, key, tempo grid |

## 11. Depth audit and gaps to fill (steps 7-12, added 2026-10-28)

Nothing in this section is started except where marked. Every step passes the marginal-value gate (`marginal_gate.py`) before it
runs in full: a small pilot first, extend only if held-out scores gain more than 0.01 per doubling.

### Depth by area (checked in `~/Infinite/training-data`)

| Area | Have | Depth |
|---|---|---|
| Number of knobs | 9,599 Diva knob columns; up to about 1,600 per Omnisphere patch; 6,974 params over 342 Infinite node types (`atlas/node_registry.json`) | Deep |
| Value of knobs | every preset stores them; 99,771 presets scanned | Deep |
| Knobs over time | 902 Live-set arcs (`arcs/als.jsonl` 443 + `air_als.jsonl` 459); 11,029 Omnisphere arps; a preset is otherwise one static snapshot | Thin |
| Sounds | plug-in renders in progress | In progress |
| Clusters of sounds | 14,161 instruments, 477 clustered by audio, 9,790 by tags, 3,894 unassigned | Thin |
| Frequency / spectrum | 32 log bands, centroid, rolloff, flatness, 40-point envelope; one vector per note | Shallow (no spectrogram) |
| Nodes | 342 node types; 68 are audio-related; 32 have a measured response (30 effects, 2 synths); 36 do not (18 of them are synth nodes) | Half measured |
| Change of nodes | each knob measured alone at 0 / 0.5 / 1 | Shallow |
| Replaceability | none measured as a pair | Missing |
| Node chains | chain order from Logic strips and Guitar Rig; 23 connection types in `compat` | Order only |

Correction to an earlier message: "32 of 342" mixed in image, 3D and utility nodes that make no sound. The real gap is the 36
audio-related nodes without a response.

### Steps

| # | Step | Why (owner's point) | Pilot and gate | Tool | State |
|---|---|---|---|---|---|
| 7 | **Spectrogram**: 8 time slices per note in every render row | A sound that moves over time needs a spectrum over time, not one average | Re-run 200 Diva presets with slices; compare held-out R2 of the sound model with and without. Keep only if it gains | `render_pass.measure_windows` extension | not started |
| 8 | **Moving knobs**: render a knob sweeping from 0 to 1 over the note, for 5 to 10 knobs that matter, on ~100 presets; add the Live-set arcs as shape priors | Moving a knob changes the sound; static snapshots cannot teach that | Gate on a 100-preset pilot; extend to the knobs whose motion changes the spectrogram most | new `--sweep-time` mode in `preset_state.py` | not started |
| 9 | **Node responses**: only the 36 audio nodes without one, not 310. Sweepable knobs only, 3 settings, measure representatives of similar nodes first | Efficient: skip image / 3D nodes (no sound) and skip a node whose response its nearest measured sibling already predicts | Run the cheap representatives first; extend while the gate gains | `response_harness.py` | **bug found and patched** (uncommitted before this commit): synth nodes with an audio input slot rejected the keyboard note line (`E_KIND_MISMATCH`), so 14 of 16 failed; the other 2 rendered silent. Patch renders them free-running. Rerun not verified. |
| 10 | **Replaceability**: for each node pair in the same family, distance between measured responses at matched knob settings | A node can stand in for another if their responses are close | Compute from step 9 data; no new renders | new script on `response_native*.jsonl` | not started |
| 11 | **Chain value**: render about 200 chains (2 to 4 nodes) from real Logic and Guitar Rig chains; test whether the chain's sound is predicted from its parts' responses; score each chain's coherence; cluster chains | A chain gets a value and a place in a cluster, not just an order | Gate: if parts predict the chain well (held-out R2 high), measure fewer chains and predict the rest | new `chain_render.py` | not started |
| 12 | **Audio analysis upgrade** (below) | The formulas should match what the field uses | Compare old and new features on 200 presets: do clusters or the sound model improve? | `audio_features.py`, `render_pass.py` | not started |

### Audio analysis vs industry practice (read from `audio_features.py`)

| Item | What we do | Industry practice | Verdict |
|---|---|---|---|
| Window and FFT | Hann, 4096 at 48 kHz, hop 1024 | Hann, 2048 to 4096, hop a quarter of the window | Standard |
| Spectrum bands | 32 bands, geometric from 20 Hz to 20 kHz, mean power over the whole window | Mel, Bark or ERB bands (perceptual); frames kept over time | Usable, not perceptual; time averaged away |
| Centroid, 85% rolloff | Power-weighted centroid; 85% rolloff | Same definitions (librosa, Timbre Toolbox, MPEG-7) | Standard |
| Flatness | Geometric / arithmetic mean of magnitude (sqrt of power) | Same ratio on power (MPEG-7) | Small variant, fine as a feature, not comparable with others' numbers |
| Loudness | RMS and peak only | LUFS (ITU-R BS.1770, K-weighted) and psychoacoustic loudness (ISO 532) | Below standard |
| Attack, decay | Time of the 10 ms RMS peak; time to fall to 10% of peak | Log-attack-time (about 20% to 80% of rise), temporal centroid, effective duration (Timbre Toolbox, Peeters 2011) | Crude |
| Onsets | Log-compressed spectral flux | Same family (SuperFlux) | Standard |
| Missing | pitch (f0), MFCC, harmonicity, roughness, spectral flux over time, harmonic/percussive split | Standard in timbre work | Add in step 12; test value before keeping |

### Order after this addition

Finish step 1 to 4 of section 6 first (the data already running), then 9 and 10 (cheap, no new renders for 10), then 7, then 8 and 11,
then 12. Each is dropped if its pilot does not pass the gate.
