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
