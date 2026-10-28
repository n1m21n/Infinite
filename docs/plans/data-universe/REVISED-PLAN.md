# Revised plan: plug-in data collection, restart point for the next session (2026-10-28)

Read this first. It replaces the "Order" list in `plugin-plan.md`; the other documents stay as the record.

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
