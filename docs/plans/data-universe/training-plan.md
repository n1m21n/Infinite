# Training data plan: collect cleanly, organise once, train against fixed goals

Companion to [README.md](README.md) (the step tracker). This file holds the rules that do not change from
session to session: where every kind of data lives, when collecting counts as finished, how disk is kept
free, and the goals and benchmarks every training run is judged by. Numbers marked *(2026-10-27)* are
today's; update them when a run changes them.

## 1. Rules

| Rule | Why |
|---|---|
| Nothing raw leaves the machine it sits on. Only rows (numbers, names, tags) go to `~/Infinite/training-data/` | disk, licences |
| Every row has a `license` field; `--scope shippable` trains shipped models (owner data, public datasets, our renders). Library and plug-in rows guide critics and benchmarks only | data is a guardrail, not a template |
| Nothing musical is skipped silently: a file is *decoded*, *metadata-only* (header, strings, size) or listed in the unknown-format triage | the owner may have forgotten parameters that still help |
| One heavy job per machine, 20-minute resumable chunks, WD-Black is read only, builds only through `scripts/ibuild.py`, no push without asking | 8 GB machines |
| A plug-in that cannot render on one machine is retried on the machine that has its library (`render_pass.py --retry-from`) | libraries live on the drive attached to the Pro |
| Retrain only after the collection criteria in section 4 hold, and keep a model only if it beats the old one on the gates | no churn |

## 2. Layout (one place per kind of data)

All under `~/Infinite/training-data/`; each machine writes its own files and only rows are copied.

| Folder | Content | Writer | Key fields |
|---|---|---|---|
| `audio/features.jsonl`, `audio/air_features.jsonl` | 32-band spectrum, envelope, loudness, groove per loop and one-shot (`dup_of` rows point at identical files) | `audio_features.py` | `src, bands, centroid, attack_ms, part, bpm, key, groove` |
| `audio/render_features*.jsonl`, `render_failures.jsonl` | the same row for a plug-in's sound at a setting and note (or test signal) | `render_pass.py` | `plugin_id, setting, note/stimulus` |
| `presets/` | knob values: Omnisphere, Diva, Hive, Ableton, Logic strips, NI, Bitwig, Valhalla, u-he, ... | `preset_reader`, `plain_preset_reader`, `binary_preset_reader`, `misc_format_reader` | params, tags, chain order |
| `samplers/exs.jsonl`, `spitfire/` | key and velocity maps, Kontakt maps, LABS audio | `exs_reader`, `spitfire_reader` | zones, files |
| `steam/` | Omnisphere arps, mod matrices, soundsource labels, wavetables | `steam_reader` | tags, steps |
| `wavetables/` | harmonic shapes of every wavetable | `wt_reader`, `binary_preset_reader` | harmonics per frame |
| `arcs/`, `grooves/` | automation arcs, Live-set clips, groove templates | `als_reader`, `arc_analysis`, `binary_preset_reader` | arc shape, swing, velocity |
| `inbox/`, `store/` | MIDI by source, ingested rows | `midi_ingest`, `link_*` | notes, expression |
| `atlas/` | `instruments.jsonl` (one row per instrument), `clusters.json`, `instruments_clustered.jsonl`, later `genres.json` | `instrument_atlas.py`, `sound_clusters.py`, `genres.py` | family, cluster, assigned (audio or tags) |
| `manifest/` | per machine: every file read, reader, rows, status; `unknown_<host>.jsonl` triage | `data_universe.py` | path, size, mtime |
| `models/`, `eval/` | trained models, held-out sets, A/B takes | `train_all.py`, `benchmark.py` | |

## 3. Collection ledger *(2026-10-27)*

| Source | Done | Target for "collected" |
|---|---|---|
| WD-Black audio | 75,760 of 102,239 files (2026-10-27: 52 folders left) | every file measured or marked duplicate |
| WD-Black presets / maps / wavetables / Live sets / MIDI | all readers run (see README) | `collect_check` shows 0 missed |
| Air presets and audio | done | unchanged |
| Air binary formats | 9,810 rows (`.agr` 219, Bitwig 2,781 headers, NI 6,684, Vital 126); Serum 2 knobs, 3,103 `.ffp` unread | decoded or metadata-only, none silent |
| Unread musical types (`.fxp`, `.sfz`, `.amxd`, `.asd`, `.cst`, `.bwproject`, `.nksn`, `.nka`) | 22,194 rows in `store/formats_meta.jsonl` (2026-10-27) | `.sfz`/`.fxp` parsed, 6 formats permanently metadata-only |
| Render pass | Air running AU + VST3 (hundreds of rows); WD-Black-library plug-ins queued for the Pro | one default row for 90% of installed plug-ins; every failure classified |
| Omnisphere / LABS / Kontakt / Keyscape / Trilian sounds | Air has LABS, Omnisphere, Kontakt 8; Pro has Omnisphere with no library | per-patch rows, see section 7 |
| Atlas | 14,161 instruments; 2,154 clustered by audio, 9,751 by tags, 2,256 none | see section 4 |

## 4. Collection is finished when

| # | Criterion | Check |
|---|---|---|
| C1 | Audio pass: every WD-Black audio file has a row or a `dup_of` row (measured 75,760 of 102,239 rows, 52 folders remaining as of 2026-10-27) | `collect_check` shows 0 audio misses |
| C2 | No silent skips: every musical extension is decoded or metadata-only (permanently metadata-only: `.asd` Ableton warp markers, `.amxd` Max device patcher meta, `.cst` Logic channel strip, `.bwproject` Bitwig project, `.nksn` Kontakt snapshot, `.nka` Kontakt array); unknown formats triaged | `data_universe.py --status` lists 0 untriaged |
| C3 | EXS instruments with audio features >= 90% | `instrument_atlas.py` summary |
| C4 | Audio-clustered share of EXS + Kontakt >= 60%; every family with >= 50 audio rows has silhouette >= 0.40 | `sound_clusters.py` table |
| C5 | Render pass: >= 90% of installed AU/VST3 plug-ins have a default row on some machine; failures classified (library missing, hang, crash, not ready) | `render_failures` per host |
| C6 | Disk: both machines have >= 15 GB free on the data volume | `df` |

## 5. Storage

*(2026-10-27)* The Pro's data volume was at 100% (553 MB free), later 10 GB; the Air has 11 GB. WD-Black has 274 GB free and is mounted read-write.

| Where | Size | Action |
|---|---|---|
| `~/Library` (Application Support 15 GB, Group 7 GB, Mobile 7 GB, Caches 4.5 GB) | 40 GB | `ccache` (3.4 GB) is regenerable; clear when a build is not about to start |
| `~/Infinite/datasets` | 10.9 GB | keep until the raw-MIDI items (arcs, CCs, non-4/4, orchestration, EMOPIA) are extracted |
| `~/Infinite/media`, `films_work`, `behance` | 7.2, 2.5, 0.9 GB | candidates to move to an external drive (tools reference these paths: leave a symlink) |
| `~/Infinite/worktrees` | 4.0 GB | remove merged worktrees |
| `training-data` | 5.1 GB | rows only; compress finished big files (`presets.jsonl` 195 MB, `exs.jsonl` 141 MB) to `.gz` and read through gzip |

Completed `store/*.jsonl` tables over 20 MB are mirrored to Drive as `.gz` (`sync_drive.py` already does this).

Moves to an external drive need the owner's yes per folder; nothing is moved or deleted without it.

## 6. Training goals and benchmarks

The benchmark is `tools/universe/benchmark.py` (10 gates). Goal: **10 / 10 gates on `--scope shippable`, held for two consecutive retrains.** *(2026-10-27: 8 / 10, B4 drums and B5 failing.)*

| Gate | Measures | Threshold | Now |
|---|---|---|---|
| B1 | held-out compression (bits saved per cell on unseen files) | positive for every part | chord +0.057, drums +0.204 |
| B2 | real-music false rejects | FRR <= 3.0% | drums 2.4% |
| B3 | mishap catch (11 corruption types) | >= 96%, 100% on critical | 97.2% overall; `sloppy_timing` 83%, `inverted_accents` 93% |
| B4 | structural realism, melody and drums vs real | classifier AUC <= 0.65 | drums 0.794 (fail); sim: hat-density move 0.591 vs 0.826 |
| B5 | novelty and diversity | card escape >= 85%, diversity >= real, cluster entropy >= 0.75 | entropy 0.664 (fail) |
| B6 | taste adaptation speed | >= 75% keep by click 20 | pass |
| B7 | genre graph regression | well-formed, neighbour signal | pass |
| B8 | spectral gate on real renders | held-out FRR <= 2%, catch >= 96% | 0.81%, 97.7% |
| B9 | 5-pillar + graph learner | Keep@20 >= 82% | pass |
| B10 | emotion layer | valence AUC >= 0.70, arousal >= 0.65 | pass; vgmidi arousal AUC 0.549 is a weak spot |

New goals this data adds (tracked in the README step rows, measured by the listed tool):

| Goal | Measure | Target |
|---|---|---|
| G1 Timbre coverage | instruments per family with an audio-based cluster | C3, C4 above |
| G2 Mix realism (step 6) | B8 on full-mix renders: FRR <= 2%, catch >= 96% with FX chains from Live sets and strips | pass |
| G3 Genre evidence (step 5) | genres whose clusters and grooves come from counted rows, not MIDI alone | >= 80% of the graph's genres |
| G4 Drum groove | B4 drums <= 0.65 and B5 escape >= 85% (port of 2h approved by the owner 2026-10-27) | both |
| G5 No regression | a retrained model replaces the old one only if no gate gets worse by more than its noise and at least one improves | per retrain |

Retrain procedure: criteria C1 to C6 hold, then `train_all.py --scope shippable`, `benchmark.py`, compare with `models/retrain_history.jsonl`, keep only if G5 holds, and ship the critic header (step 2i) only at 10 / 10.

## 7. Per-patch rendering for Omnisphere, LABS and Kontakt

The render pass measures a plug-in at its default patch and a parameter sweep. The soundsource library (11,519 Omnisphere sounds, LABS and Kontakt instruments) needs one row per patch. The headless CLI has no flag to load a plug-in preset, so the open question is whether the plug-in node in a `.inf` patch can carry a saved state or program index. Next step: test that on the Air with one LABS instrument; if it works, `render_pass.py` iterates the patches; if not, only default and sweep rows exist and these libraries stay tag-clustered.

## 8. Order of work

1. Keep the audio loop (Pro) and the render loop (Air) running in chunks; pull Air rows back after each chunk.
2. When the audio loop ends: Pro retry of the Air's library failures; rebuild atlas and clusters.
3. Finish no-skip readers (2n), genres (5), then full mix (6) and node proposals (7, shown to the owner first).
4. Weak spots (2j) and the `grid: none` leftovers (2k).
5. Check C1 to C6, then retrain once.
