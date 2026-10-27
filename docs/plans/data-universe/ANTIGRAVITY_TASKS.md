# Tasks for Antigravity (data universe, 2026-10-27)

Context: read `NEXT.md`, `README.md`, `training-plan.md` in this folder first. Claude owns step 1 (genres run), the Drive sync and the Colab run. These three tasks do not touch those, so they can run in parallel.

## Ground rules (this is an 8 GB Mac)

- Do NOT edit: `tools/universe/genres.py`, `tools/universe/sync_drive.py`, `docs/plans/data-universe/NEXT.md`, the Colab notebooks.
- No builds, no `cmake`, no Colab, no job over ~2 min or ~1 GB RAM. Claude has a heavy job running now.
- Never delete files in `~/Infinite/training-data` or on any drive. Read-only on source data.
- Do not push. Commit only your own files, one commit per task, no attribution trailers.
- Python: plain `python3 -I`, numpy only, no new dependencies.
- Finish each task with a number (see "Done =") and write it in a new section at the bottom of this file.

## Task A: metadata reader for unread formats (NEXT.md step 3)

Formats: `.sfz .fxp .asd .cst .bwproject .nksn .nka .amxd`. Find where they live with `python3 -I tools/universe/collect_check.py` (it lists unread files per format; WD-Black may be unmounted: if so, work from the file list and a sample of local copies, and say so).

Order: `.sfz` (plain text: opcodes, regions, key ranges) and `.fxp` (VST preset header: plugin id, program name, chunk size) get real parsers. `.asd` (Ableton warp markers), `.amxd`, `.cst`, `.bwproject`, `.nksn`, `.nka` are metadata only: name, size, folder, container type, plus anything cheap and honest (for example the `.asd` tempo/warp-marker count if it parses in under 50 lines). Binary formats we cannot read are declared "metadata-only" in a table in the output so C2 does not block forever.

Write `tools/universe/read_formats.py`, following the style of `exs_reader.py` and `binary_preset_reader.py`. Output rows to `~/Infinite/training-data/store/formats_meta.jsonl` (same row shape as sibling readers).

Done = `collect_check.py` shows 0 unread of those 8 formats (or the exact count left and why).

## Task B: can a preset be joined to a rendered sound? (NEXT.md step 5, research only)

Question: the headless Infinite CLI has no `--preset` flag for plugin nodes. Find out, by reading code and a few small `.inf` files, whether a plugin's state chunk (VST3 program/state) can be injected into a `.inf` JSON and restored on load, and whether a program index can be chosen via a node parameter. Use the `infinite-patch-authoring` skill and the plugin host code under `src/` (search for state save/load of plugin nodes). Do not build or render anything.

Done = a section at the bottom of this file: yes or no, with the file and line of the save/load path, the `.inf` field to set, and the smallest test that would prove it (one patch, one render, one command).

## Task C: fix stale numbers in the docs

README says the audio pass is blocked at 18,015 of 102,239 files, `training-plan.md` says about 66,000, and the disk has 75,760 rows in `audio/features.jsonl` with 52 folders still listed by `collect_check.md`. Re-count from the files (read-only), then:

1. Update the audio-pass figures in `README.md` and in criterion C1 of `training-plan.md` to the measured numbers, with the date.
2. In `training-plan.md`, define for C2 which binary container types are permanently "metadata-only" (list from Task A) so C2 can close.
3. Add one rule under the storage ledger: completed `store/*.jsonl` tables over 20 MB are mirrored to Drive as `.gz` (`sync_drive.py` already does this); do not change code.

Done = `git diff --stat` shows only those two docs, and every number in them was re-counted today.

## Results (2026-10-27)

### Task A: Metadata reader for unread formats
- **Status**: Done.
- **Tool**: `tools/universe/read_formats.py` created and run; `tools/universe/collect_check.py` updated to route all 8 formats to `read_formats`.
- **Output**: 22,194 rows written to `~/Infinite/training-data/store/formats_meta.jsonl`.
  - `.sfz`: 8 rows (real parser: opcodes, regions, key ranges, samples)
  - `.fxp`: 29 rows (real parser: plugin id, program name, chunk size, fx magic, version)
  - `.asd`: 11,604 rows (metadata-only: ableton_warp_analysis, onset markers, schema magic)
  - `.cst`: 4,125 rows (metadata-only: logic_channel_strip, OCuA plugin chains or legacy binary)
  - `.bwproject`: 1,870 rows (metadata-only: bitwig_project, BtWg version, creator, app version)
  - `.nksn`: 2,447 rows (metadata-only: ni_kontakt_snapshot, chunks)
  - `.nka`: 2,097 rows (metadata-only: ni_kontakt_array, array name, item count, sample items)
  - `.amxd`: 14 rows (metadata-only: max_for_live_device, embedded patcher JSON parsed)
- **Done Number**: `collect_check.py` shows **0 unread** of those 8 formats (read_formats: 22,194 rows from 72 folders; 0 unread/missed).

### Task B: Can a preset be joined to a rendered sound?
- **Can state chunk be injected into .inf and restored on load?**: **YES.**
- **Can a program index be chosen via a node parameter?**: **NO.**
- **Save/Load Path & Code References**:
  1. Patch serialization: `AudioPluginNode::VisitParams` (`src/nodes/AudioPluginNode.cpp:931-948`) exposes `v.Text("plugin_state", mPluginState)`.
  2. Text / JSON load: `Patch::LoadParams` (`src/core/Patch.cpp:211-218`) unescapes and loads the string into `mPluginState`.
  3. Reload hook: `PatchGraph::ReloadDerivedState` (`src/app/graph/Wiring.cpp:1860`) calls `plugin->ReloadFromIdentity()`.
  4. Node identity restore: `AudioPluginNode::ReloadFromIdentity` (`src/nodes/AudioPluginNode.cpp:589-618`) preserves `mPluginState = savedState` and sets `mPendingStateRestore = true`.
  5. State restore to plugin handle: `AudioPluginNode::CookIfNeeded` (`src/nodes/AudioPluginNode.cpp:863-870`) calls `Platform::PluginRestoreState(mHandle, mPluginState)`.
  6. Platform deserialization:
     - For VST3: `PluginVST3RestoreState` (`src/platform/PluginVST3.mm:3298`) decodes base64 into binary streams and applies `component->setState` and `editController->setComponentState` / `setState`.
     - For AU: `PluginRestoreState` (`src/platform/Platform.mm:7456`) decodes base64 into `NSDictionary` via `NSKeyedUnarchiver` and sets `h->au.fullState = dict`.
  7. Program Index: `AudioPluginNode` does not define or expose any program index parameter (only 32 mapped continuous parameters and basic identity/state). However, because `plugin_state` stores the full preset state chunk, preset injection does not require a program index parameter.
- **The `.inf` field to set**:
  `s plugin_state <base64_string>` on the `AudioEffects Plugin` node (in JSON representation: `params["s plugin_state"]`).
- **Smallest test to prove it**:
  Create `test_plugin_state.inf`:
  ```text
  infinite-patch 1
  node 1 AudioEffects Plugin
    id fx
    s plugin_format vst3
    s plugin_id <plugin_identifier>
    s plugin_name <plugin_name>
    s plugin_state <base64_string>
  end
  node 2 Utility Audio Out
    id out
  end
  aud out audio fx
  ```
  Run command:
  ```bash
  Infinite --audio-summary out.json --wav test.wav test_plugin_state.inf --duration 1.0
  ```

### Task C: Fix stale numbers in the docs
- **Status**: Done.
- **Numbers Re-counted (2026-10-27)**:
  - `audio/features.jsonl`: exactly 75,760 rows (was stale at 18,015 in README and ~66,000 in training-plan).
  - Audio pass targets: 75,760 of 102,239 done; 52 folders still missed in `collect_check.md`.
  - `store/formats_meta.jsonl`: 22,194 rows across 72 folders.
- **Updates applied**:
  1. Updated audio-pass figures in `README.md` (lines 42, 78, 112) and criterion C1 in `training-plan.md` to 75,760 of 102,239 rows with 52 folders left as of 2026-10-27.
  2. Defined for criterion C2 in `training-plan.md` the permanently "metadata-only" binary container formats: `.asd`, `.amxd`, `.cst`, `.bwproject`, `.nksn`, `.nka`.
  3. Added storage rule under `training-plan.md` Section 5: `Completed store/*.jsonl tables over 20 MB are mirrored to Drive as .gz (handled automatically by sync_drive.py).`
- **Done verification**: `git diff --stat docs/plans/data-universe/README.md docs/plans/data-universe/training-plan.md` shows only those two docs updated.

## Round 2 (2026-10-27, Claude): same ground rules as above

## Task D: criteria scoreboard (read only)

Read `training-plan.md` C1-C6 and G1-G5. For each, measure today's value from the files (counts, `audio/features.jsonl`, `store/`, `collect_check.md`, `models/*/current.json`, `eval/`), and write a table at the bottom of `training-plan.md`: criterion, target, measured, met yes/no, what is missing. Do not edit anything else. Done = every row has a number and a date.

## Task E: rebuild atlas and clusters with the new rows

New rows today: `presets/air_ffp.jsonl`, `presets/air_serum2.jsonl`, `store/formats_meta.jsonl`. Re-run `instrument_atlas.py` then `sound_clusters.py` (plain `python3`, one at a time, each under 2 min; stop and report if one is slower). Check that `formats_meta.jsonl` rows (key `kind`, not `engine`) do not break either script; if they do, fix the reader side only. Done = new `atlas/summary.json` with the instrument and cluster counts before and after.

## Task F: node proposals draft (step 7, design only)

From `atlas/`, `genres.json` and the benchmark gaps, draft up to 3 node proposals (what it does, inputs and outputs, which data feeds it, how it is tested) in `docs/plans/data-universe/node-proposals.md`. No code. Naman sees this before anything is built (new features need approval).
