# Sound model: handoff plan (2026-10-28)

For the next Claude account. Read this, then `SOUND-MODEL.md` sections 1, 4 and 4b. This file replaces "keep rendering presets"
as the next step.

## 1. The purpose, in one line

Infinite's generator picks a sound that fits a (genre, mood, section) and the owner prefers it in a blind test (M5). Everything
below serves that test. A forward model of Diva is a means, not the goal.

## 2. Think broadly: two roads to M5

```
 ROAD A: PICK  (reaches M5 first)                         ROAD B: MAKE  (research, gated)
 every sound we already have                              knobs ──M1 forward──▶ sound
   ├─ 195k sample/loop features (features.jsonl)          sound ──M2 inverse──▶ knobs
   ├─ Diva 3,873 presets (v1 desc) + 1,631 (v2 mel)        needs many renders per synth,
   ├─ Vital, Omnisphere rows (partial)                     R2 bars 0.50 / 0.40 far away
   ▼                                                       (0.22 / 0.17 at 1,125 presets)
 one sound embedding for all of it
   ▼
 words <-> sound (M4) from the tags already in presets and packs
   ▼
 generator retrieves the best existing sound or preset for (genre, mood, section)  ──▶  M5 blind test
```

Road A uses data that already exists, works for every plug-in and sample at once, and answers the owner's question ("does the model
pick better sounds?") soonest. Road B is only needed to *invent* settings no preset has; it resumes when Road A shows that picking from
existing presets is the limit.

## 3. The plan, with one touch point per phase

| Phase | Work | Bar (done when) | Budget |
|---|---|---|---|
| P1 Inventory | one table of every sound source: rows, features present (desc / mel / tags), plug-in, licence, machine. Read only | the table exists in this file | 1 h |
| P2 Sound embedding | one vector per sound from what is stored (43 descriptors + 64 x 48 mel where present); standardise across sources; nearest-neighbour sanity check by ear on 20 queries | 15 of 20 neighbours judged "same kind of sound" by the owner | 1 session |
| P3 Words <-> sound (M4) | tags from preset names, banks and pack metadata; per word with >= 30 sounds, a classifier on the embedding, scored on held-out banks | AUC >= 0.75 for the majority of words; recall@10 >= 0.5 | 1 session |
| P4 Generator pick (M5 lite) | (genre, mood, section) -> words -> top sounds -> the patch; compare with today's tag lookup | owner prefers the model's pick in >= 6 of 10 blind trials | 1 session + owner's 20 min |
| P5 Road B, only if P4 shows the limit is "no preset sounds right" | the D2 one-knob-variant pilot vs more presets at equal cost (`SOUND-MODEL.md` 4b), then M1 / M2 | M1 bars | per 4b |

Optional, owner's approval needed (a download): a pretrained audio-text model (for example LAION-CLAP, open weights) gives
words <-> sound without our tags, but it needs audio, and only mel is stored. That means re-rendering the chosen sounds locally (raw
audio never leaves the machine). Weigh it after P3: use it only if our tags are too thin.

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
