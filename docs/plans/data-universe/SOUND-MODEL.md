# Sound model: the one plan (2026-10-28)

**Start here.** This file sets the goal, the milestones, the benchmark of every milestone and the order of work for the sound side
of the data universe. It supersedes the "Order" lists in `REVISED-PLAN.md` (section 6 and 11), `NEXT.md` and `next-phase.md` section 8+.
Those files stay as the record of what was measured and decided. The note-model track (MIDI, gates B1-B10 in `training-plan.md`)
is unchanged and is tracked there.

## 1. What we are building

**An intelligent sound model**: a model that has learned how a synth's settings turn into sound, so it can (a) predict the sound of
settings it never heard, (b) find settings for a sound or a word it is given, and (c) let Infinite's generator pick sounds for a
(genre, mood, section). "Real learning" means one thing here: **every claim is scored on presets the model never saw**.

```
                        FORWARD  (M1)
   recipe ------------------------------------------>  sound
   knobs + note + velocity                              log-mel frames + timbre descriptors
          <------------------------------------------
                        INVERSE  (M2)
                              |
             shared sound space across synths (M3)
                              |
             words <-> sound  (M4: warm, glitchy, wide ...)
                              |
             generator in Infinite (M5): (genre, mood, section) -> sound -> recipe
```

Two tracks feed the generator and stay separate:

| Track | Learns | Data | Benchmark | Where |
|---|---|---|---|---|
| Note model | which notes, grooves, chords | MIDI | B1-B10 (8 / 10) | `training-plan.md` |
| **Sound model** | which settings make which sound | plug-in renders + presets | M0-M5 below | this file |

## 2. Honest state (measured 2026-10-28)

| Item | Number |
|---|---|
| Diva presets rendered (3 notes each) | 3,613, still rendering (3 shards on the Pro) |
| Knobs -> sound, linear ridge, held out | R2 0.263 |
| Knobs -> sound, 2-layer MLP (numpy), held out | **R2 0.349** (bands 0.350, scalars 0.345), first real model |
| MLP learning curve | 0.117 at 400, 0.202 at 800, 0.252 at 1,600, 0.349 at 2,891: **+0.10 per doubling, still data-limited** |
| Plug-in renders that trained any model before today | none |
| Sound target stored per render | 43 numbers (32 averaged bands + 11 scalars); from today also a 64 x 48 log-mel spectrogram |
| Tools in `tools/universe/` | 107 scripts, no index |
| Plan documents in this folder | 13 |

Reading: the ridge gate said "barely worth rendering more"; a real model says the opposite. The gate's yardstick, not the data,
was the limit. Rendering Diva further is justified.

**Frozen split (M0, `sound_eval.py`, 3,694 presets):**

| Model | Random split R2 | Bank split R2 (whole folders held out) |
|---|---|---|
| ridge | 0.282 | 0.156 |
| k-NN (k=5) | 0.261 | 0.031 |
| MLP | 0.384 | **0.211** |

The bank split is the honest number. Presets inside one bank are near-copies, so a random split rewards memory (k-NN collapses from
0.26 to 0.03 once banks are held out). Every bar in section 4 is judged on the bank split first.

## 3. Why it felt cluttered, and the fix

| Cause | Fix |
|---|---|
| Collection ran ahead of learning: 100k+ rows, zero sound models | Every new data step must serve a milestone below and show held-out gain |
| Many workstreams open at once (mood, wavetables, chains, node responses, replaceability, moving knobs, audio v2, atlas) | One milestone at a time; the rest is in the parked list (section 6) |
| Weak yardstick (ridge on averaged features) decided what to render | Gate re-set against the MLP; later against the M1 model |
| Target averaged time away (one spectrum per 3 s note) | Log-mel frames stored with every render from now on |
| No fixed evaluation: each probe made its own split | M0: one frozen split file, one metric module, one command |

## 4. Milestones, metrics, bars

Metrics follow common practice in synth sound matching and neural audio work: log-mel distance and multi-scale spectral distance
(DDSP, Engel et al. 2020), timbre descriptors (Timbre Toolbox, Peeters et al. 2011), parameter error plus re-render round trip for the
inverse task (Yee-King et al. 2018, Barkan et al. 2019 InverSynth, Esling et al. 2020 Flow Synthesizer, which also used Diva),
Fréchet Audio Distance on embeddings for whole-set comparisons (Kilgour et al. 2019), and blind listening in the MUSHRA format
(ITU-R BS.1534) for the final judgement.

| # | Milestone | Done when (bar) | Baseline today |
|---|---|---|---|
| M0 | **Evaluation harness** | `sound_eval.py` scores any model on a frozen split (`eval/splits/diva.json`): random-preset split **and** bank split (whole preset folders held out, no near-duplicate leakage). Prints R2 per descriptor group, log-mel L1 in dB, spectral convergence. Same model twice gives the same numbers within 0.005 | probes use ad-hoc splits |
| M1 | **Forward model, Diva** | descriptor R2 >= 0.50 random split, >= 0.40 bank split; log-mel L1 at least 20% below the nearest-preset baseline (k-NN in knob space) | MLP R2 0.384 random, 0.211 bank; mel rows start 2026-10-28 |
| M2 | **Inverse model, Diva** | for 200 held-out sounds: re-render the predicted knobs; log-mel L1 to the target at least 10% below the retrieval baseline (knobs of the closest rendered preset) | not built |
| M3 | **Second and third synth** (Vital, open format; Omnisphere, largest library) in one shared sound space | M1 bars per synth; cross-synth match ("this Diva sound in Vital") beats retrieval by 10% on re-render | Vital partly rendered; Omnisphere rows being redone at lead 30 |
| M4 | **Words <-> sound** | per word with >= 30 tagged presets: AUC >= 0.75 from the sound embedding on held-out presets; recall@10 >= 0.5 for word -> preset | tags exist, no sound embedding |
| M5 | **Generator uses the sound model** | owner's blind listening, 10 trials, MUSHRA-style: model's pick preferred over tag lookup in >= 60%; B1-B10 no regression (G5) | not built |

Model choices, settled:

| Choice | Decision | Why |
|---|---|---|
| Probe models | numpy on the Pro (ridge, k-NN, MLP) | no install, seconds to run, fine at 4k rows |
| Training models beyond the probe (M1 mel decoder, M2, M3) | PyTorch on Colab through the existing parity setup (`results/colab_2026-10-27.txt`) | 8 GB Macs; rows are features, allowed to travel |
| Knob encoding | standardised continuous knobs now; one-hot for selector knobs (oscillator model, filter type) in M1 | selectors are categories, not amounts |
| Note and velocity | inputs to the model (C3, C4, C3 forte rendered; others predicted only if `note_predict.py` holds) | sound depends on both |
| Gate stop line | rerun `marginal_gate.py` with the MLP curve (`sound_model_probe.py`); stop a library when two doublings gain < 0.01 **on the MLP** | ridge undersells the data |

## 5. Order of work

```
 M0 harness ──> M1 forward Diva ──> M2 inverse Diva ──> M3 Vital + Omnisphere ──> M4 words ──> M5 in Infinite
     │               │                   │
     │               └─ keep Diva rendering (mel rows) until the MLP gate says stop
     └─ data check of every render file (REVISED-PLAN 6.1) feeds the split file
```

| Step | Work | Cost | State |
|---|---|---|---|
| 1 | Re-run the gate on Diva; first real model (`sound_model_probe.py`) | 1 min | **done 2026-10-28** (table in section 2) |
| 2 | Store log-mel (64 x 48, uint8 0.5 dB) with every render (`render_pass.logmel`) | none extra | **done 2026-10-28**, picked up by each new render chunk |
| 3 | M0: frozen splits + `sound_eval.py` | 1 session | **split frozen and scoring works 2026-10-28** (`eval/splits/diva.json`, 22 banks, 768 presets). Left: log-mel L1 and spectral convergence once mel rows exist; repeat-run check |
| 4 | Verify every render file (features present, presets differ, silent share) on both machines | 30 min | REVISED-PLAN 6.1 |
| 5 | M1 on Colab: MLP with one-hot selectors, then mel decoder | 1-2 sessions | after 3 |
| 6 | Omnisphere on the Air at lead 30; Vital prefix of 500 | machine hours | REVISED-PLAN 6.2, 6.4 |
| 7 | M2, then M3, M4, M5 | | after M1 passes |

## 6. Parked until a milestone needs it

Each item returns only when a milestone's bar cannot be reached without it. Nothing here is deleted.

| Item | Returns at |
|---|---|
| Moving knobs (sweep over a note), REVISED-PLAN step 8 | after M2, if time-varying sounds are the error |
| Node responses, replaceability, chain value (steps 9-11) | M5 (mapping the model's sound onto Infinite nodes) |
| Audio analysis upgrade (step 12: LUFS, log-attack, MFCC, f0) | M1, only if descriptors limit the score; the mel target already covers most |
| Mood layer W4, axes S1-S5 | M4 |
| Wavetable generator W3, sample-instrument builder W2 | after M3 |
| Level 2 renders (Serum 2, KORG) | M3, only if Vital and Omnisphere are not enough |
| Atlas and cluster rebuild | M4 |

## 7. Rules carried over

One heavy job per machine; 20-minute resumable chunks; nothing raw leaves a machine (mel rows are features); held-out numbers only;
commit per step, no attribution lines; new nodes, panels and downloads need the owner's yes; python for these tools is
`~/music videos/n1m21n-room/.venv/bin/python3 -I`.
