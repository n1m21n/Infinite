# Air brief: Blender and Ableton bridges, Phase 1 (no engine build, no Infinite binary)

Work in your own worktree on `air/bridges`. Commit per
step, do not merge or push. No Co-Authored-By or "Generated with" lines.
Python 3 standard library plus Blender's own `bpy` (run as `Blender -b -P`). Load skills `tool-first`,
`infinite-lifecycle`. Never open Blender source or Ableton internals; only their documented APIs and files.

Both apps are installed on this laptop. Do not touch `src/`.

## Blender (docs/plans/blender-bridge/README.md)
1. `tools/blender-bridge/to_infinite.py`: any Blender-readable file or .blend -> modifiers applied, one joined mesh -> GLB -> prints the `Model 3D` patch snippet. Test with 3 inputs; verify by the Pro later (rendering needs the build).
2. `tools/blender-bridge/run_op.py` + classify the 2491 operators in `catalog.json` as `headless_ok` or `needs_ui` by running each on a scratch scene with a timeout; write `catalog.classified.json`. Last stdout line is JSON.
3. `tools/blender-bridge/test_bridge.py`.

## Ableton (docs/plans/ableton-bridge/README.md)
1. Find 3 real `.als` sets on this laptop (copy to a gitignored `fixtures/`, never commit). Write `tools/ableton-bridge/als.py` (gunzip + XML) with `als-read` giving tempo, tracks, clips, MIDI notes. Record the schema in `SCHEMA.md`.
2. `.mid` writer (standard library only) and a test that reads it back.
3. Report in `docs/plans/ableton-bridge/PHASE1-NOTES.md`: what matched what Live shows, what did not.

Done when the tests pass and the notes are written. Stop there; Phase 2 (engine mesh export, Link, M4L) is the Pro's.
