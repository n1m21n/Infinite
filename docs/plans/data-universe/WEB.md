# The web: every model linked to every other, through the characteristics (owner 2026-10-28)

One page to start the next session from. It joins what every other doc in this folder says; it replaces none of them.
Rule of the web: **a model is a point; a link is "given A, what is B"; all points share one coordinate system, the 22 characteristic axes.**

## 1. The picture

```
                              GENRE / SUBGENRE clusters (G5, C8)
                          mood (C5) . words (C4) . taste (C10)  <- steer every point
                                           |
   NOTE model  ---------how it is played-----------> SOUND / SYNTH model (per engine, C1 C2 C3 C7)
   MIDI, drums, groove,                               recipe = every knob of one engine
   velocity, expression,                                    |                    ^
   quantize, CCs, arcs                                      v                    |
        |                                           INSTRUMENT model        SAMPLE model
        |                                           (sampler maps, EXS,     (whole sample =
        |                                            Kontakt, acoustic,       notes + sound +
        |                                            Step 7)                 effects + mix)
        |                                                   |                    ^
        +------------------------> EFFECTS / CHAIN model (C9, CHAIN-TRACK) -------+
                                                            |
                                                      MIX model (G8)
                                                            |
        every point reads and writes the SAME 22 axes  <----+----> render -> measure -> claims
```

Real signal path (order of work): **note -> sound / instrument -> effects -> mix**. Genre, mood, words and taste are not stages, they are dials on all of them.

## 2. The shared coordinate: characteristics (CHARACTERISTICS.md)

| Group | Axes |
|---|---|
| Timbre (audio) | loudness, brightness, sharpness, warmth, weight, fullness, noisiness, roughness, attack, decay, movement, width, plus 20 space, 21 vowel, 22 glide |
| Music (MIDI) | pitch height, register spread, roughness (chord), tonal tension, harmonic rhythm, density, contour |
| Shared | loudness (velocity), roughness, density: audio and MIDI measured by the same rule |

Every row of every model is stored as these curves (mean, spread, slope). That is what lets a note model talk to a sound model with no custom bridge: both are points in the same space.

## 3. The points (what each model is, its data, its claims, its state)

| Point | Data it learns from | Learner and claims | State (2026-10-28) |
|---|---|---|---|
| **Note** | MIDI (lakh, groove, bach, vgmidi, EZkeys, Ableton clips): melody pitches over time, chords, drum grooves, velocity, quantization, note expression, CCs, meters, arcs | critic `critic.py` (hard + feel + judge), `feel_analysis.py` bands, benchmark B1-B10, groove 2h | gates mostly pass. Weak: B3 sloppy_timing 83% (real cause: the 25% margin puts the live jitter limit at 0.229 not 0.183; tightening to the plain p99 0.15 catches 97.6% at 1% false reject, measured today, not yet applied), inverted_accents 93%, B8 thin 68/80, B10 arousal AUC 0.549 |
| **Sound / synth** (6 engines: Analog, Oscillator, Wavetable, FM, Metallic, String) | own renders (`own_<engine>.jsonl`), Diva banks, human presets (Patches, Vital/Serum/Hive), 195k measured samples | forward model recipe -> axes, active learning (Step 4). Claims C1, C2, C3, C7 | C3 critic AUC 0.924 PASS but owner blind listen 24/40 MISS (bar 30). C2 MISS on all six. Reach MISS (Analog 9.3% closer, String 17.3%). Determinism 20/20 |
| **Instrument** | EXS maps (2,114 playable), Kontakt maps 229, Omnisphere soundsources 11,519, LABS, sampler atlas | `instrument_atlas.py`; Step 7 String vs library acoustic sounds | atlas built (14,161 rows); Step 7 not started; in-app sampler node = new feature, owner approval first |
| **Effects / chain** | node census (342 types), `describe_all.json`, Logic channel strips (26,216 `.cst`), preset chains, Ableton device chains | CHAIN-TRACK: x_k after every node, each link scored alone. Claim C9 | designed, not rendered. Starts after the embeddings; first renders = single effects, then source + 1 effect |
| **Mix** | Ableton sets (`arcs/als*.jsonl`): levels, pans, sends, chains; album and loop audio: balance, masking, crest, LUFS | G8 | waits on the Ableton sets |
| **Sample** (whole) | patch-universe library `~/Infinite/patch-universe-library/samples/` (what the live site generator makes) | `compose` places notes + recipe + chain + mix in one patch; final blind test vs site generator | `listen_model.py` has events + recipe + Reverb v1; `compose.py` not yet split out |
| **Genre / subgenre clusters** | tracks and loops named by genre, genre graph, MIDI arcs | G5, C8 (top-3 >= 0.7) | `genre_graph.py`, `arc_analysis.py` partial; each genre = region of axes + structure (tempo, groove, chords, form) |
| **Sound clusters** | atlas rows by timbre per family | `sound_clusters.py` | thin audio base; rerun after audio pass |
| **Mood / emotion** | owner's emotion rules on the axes (arousal, valence), mood reference sets as a check only | C5, C5r | rules drafted, not signed off |
| **Words** | preset names, tags, folders; `character_matrix.json` | C4 | not run |
| **Taste** | local Prediction data (keeps, undoes, tweaks), never uploaded | C10 | not started |

## 4. The links (pairs) and what each one learns

The link layer is W5 in `next-phase.md`: count baseline first, then a low-rank matrix, and it only counts if it beats the baseline by >= 0.10 bits held out. Never a hand-made grid.

| Link A -> B | Learns | Data that feeds it | Status |
|---|---|---|---|
| Note -> Sound | which timbres suit how a part is played (a pluck vs a pad vs a bass) | sample rows with a measurable note pattern + the sound's axes; EZkeys/MIDI by instrument family | not built |
| Sound -> Note | which phrases a sound wants (range, density, legato) | the same pairs reversed | not built |
| Note -> Instrument | playing style per instrument (keyswitches, velocity layers, range) | EXS/Kontakt maps zones + MIDI by family | not built |
| Sound -> Effects | which chain changes this sound toward a target axis | CHAIN-TRACK renders (x_0 -> x_k) | designed |
| Effects -> Mix | level / pan / send after the chain | G8 sets | waits on data |
| Genre -> every point | the region and structure each point lives in | G5 regions | partial |
| Mood / words -> every point | a steer along arousal / valence / named axes | C4, C5 | rules drafted |
| Taste -> every point | per-user offset on the shared critic | G7 | later |
| Any pair -> Sample | consistency of the combined render (does it still sound like what was asked) | whole-sample blind test | final step |

Not every one of the (n x n) pairs gets its own model: each link is a conditional on the shared axes, so adding a point adds one embedding, not n new models. That is the saving the shared coordinate buys.

## 5. One learning recipe for all points (what today taught)

| Step | Sound model (done) | Same step for Note / Effects / Mix |
|---|---|---|
| Measure every data point on the axes | `own_sweeps.py`, `characteristics.py` | MIDI rows -> music axes; chain renders -> x_k |
| Forward model: knob -> axes | recipe -> axes, per engine | events/params -> axes |
| Claims frozen before testing | `claims.json`, held-out by group | gates B1-B10 already frozen; C9 for chains |
| Active learning picks next renders | Step 4 | apply to chains and notes |
| Critic: good vs random | C3 (22-axis logistic) | note critic (hard, feel, judge) |
| Human check | owner blind listen, loudness-matched | same page builder `listen_*.py` |
| Corruption test | not yet | `critic.py corrupt()` (B3) |
| Creativity dial | `dials.md` | same dial c |

## 6. Every doc, and what to do with it

| Doc (`docs/plans/data-universe/`) | Role | Read when |
|---|---|---|
| README.md | collection tracker, what each reader gave, Air and WD-Black inventory | data questions |
| NEXT.md | older session-start note (2026-10-27) | history only; this file supersedes |
| **WEB.md** (this) | the map | every session start |
| INTEGRATION.md | patch + 22 axes as the two shared surfaces, layer contracts | wiring layers |
| EXECUTION.md | the running plan: Steps 0-7, G1-G10, claims C1-C11 | always; follow in order |
| SOUND-MODEL.md | the sound plan, honest state, 4b data contract numbers | sound work |
| HANDOFF.md | every number recorded, owner answers, test results | before claiming anything |
| CHARACTERISTICS.md | the 22 axes and their rules | adding / changing a measurement |
| CHAIN-TRACK.md | effects model design | effects work |
| MASTER-ARCHITECTURE-BLUEPRINT.md | twin-brain note + synth, emotional knobs, anchors | vision |
| PRESET-ANCHORS.md, BLUEPRINT-ANCHORS-RESPONSE.md | 40 AI-written anchors (owner: the plugin presets and `~/Downloads/Patches` are the human ones) | anchors |
| REVISED-PLAN.md, next-phase.md, training-plan.md | older plans: W1-W5 link layer, baselines, layout | link layer, baselines |
| RESEARCH-LANDSCAPE.md | DDSP, sound matching, embeddings; activity / connectivity / tuning curves | research choices |
| SECOND-OPINION*.md, REVIEW-CONCLUSION.md, WEAKNESSES*.md, SECTION-6-RESPONSE.md | external reviews and our answers | when a claim is challenged |
| axes-strategy.md, dials.md, recipes.md | axis choice, creativity dial, recipe schema | dials and recipes |
| plugin-plan.md, EFFICIENCY.md, groove-2h.md, ANTIGRAVITY_TASKS.md | plug-in coverage, speed, drum port, delegated tasks | those tasks |

Outside this folder: `docs/plans/generate-samples/` (site generator brief, baseline), `docs/plans/web-generator/`, `docs/plans/patch-corpus/`, `docs/plans/prediction/` (taste data, G7), `docs/plans/ableton-bridge/` (Ableton sets, G8), `docs/plans/arrangement/`, `docs/NOW.md`.

## 7. Tools (`tools/universe/`)

| Layer | Tools |
|---|---|
| Collect / read | `universe.py`, `*_reader.py`, `midi_ingest.py`, `audio_features*.py`, `collect_check.py` |
| Measure | `characteristics.py`, `feel_analysis.py`, `embed.py`, `embed_monotone.py` |
| Sound | `own_sweeps.py`, `sound_space.py`, `learn.py`, `claims.py`, `c2_*.py`, `human_bench.py`, `determinism.py` |
| Note | `critic.py`, `benchmark.py`, `groove_sim.py`, `genre_graph.py`, `arc_analysis.py` |
| Atlas / clusters | `instrument_atlas.py`, `sound_clusters.py`, `character_matrix.py` |
| Listen / compose | `listen_c3.py`, `listen_model.py` (compose v1) |

## 8. Start of the next session (in this order)

| # | Do | Why |
|---|---|---|
| 1 | Read this file, then `EXECUTION.md` from the current step, then the last 80 lines of `HANDOFF.md` | state |
| 2 | Check jobs: embeddings, monotonicity (`/tmp/own/embed_mono_analog.log`), dev build (`/tmp/own/ibuild_dev.log`) | long jobs lie |
| 3 | Note layer: apply the jitter fix (gate at plain p99 for jitter, keep B2 false reject <= 3%), re-run B3 and B2, record in HANDOFF | first measured win on a second point |
| 4 | Sound layer: rest of Steps 3-7 on the six engines, preset anchors from `~/Downloads/Patches` with a disjoint benchmark split | C2 / reach still MISS |
| 5 | Effects layer: first chain renders after embeddings | opens point 4 of the web |
| 6 | Split `compose.py` out of `listen_model.py` | one wiring place |
| 7 | Link layer W5: count baseline for Note -> Sound | first edge of the web |
| 8 | Mix when Ableton sets arrive; instrument Step 7; owner approval before Phase 6 | later |
| 9 | When all is done: tell the owner; whole samples, shuffled and blind, new generator vs live site generator | the end goal |

## 9. Standing rules and open items

| Item | State |
|---|---|
| Raw MIDI delete | only per folder confirmed in the cloud by the Drive API; owner said delete the synced ones; most folders unconfirmed |
| WD-Black | all 195,081 rows measured and stored; needed again for re-listening, re-measuring, 22 unfinished folders |
| Air | reached by Bonjour name (changes); see memory `air-has-more-presets` |
| No push of chat corpora; no Claude attribution in commits; new nodes / modes / panels need owner approval; builds only via `scripts/ibuild.py`, one at a time | rules |
| Owner help | blind listening pages (`listen_*.py`); in-context model test `model_m1ctx` awaiting answers |
