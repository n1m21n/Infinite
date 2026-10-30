# Piano Roll: next session (written 2026-10-30)

Spec: `docs/plans/piano-roll/README.md`. Read §9 (UI layout), §10 (blocks), §12.4 (Generate in the panel), §12.6 (save lines)
and "Block A status" at the end before doing anything.

## Start here
1. Read `docs/DECISIONS.md`, follow `infinite-lifecycle`. Run `hostname -s`: only the Pro merges `main` (AGENTS.md names it
   `Namans-MacBook-Pro-366`; this session ran on `Namans-MacBook-Pro-816`, so check which name is current before merging).
2. Decide Block A's merge (step 0 below). The owner was asked and has not answered yet.
3. Then Block B.

## Where things are
- Worktree `/Users/namansoni/Infinite/wt-piano-roll`, branch `feature/piano-roll-a-model`, 7 commits ahead of `main`, clean.
- Builds: `scripts/ibuild.py` only (dev mode, `build-dev/`), one build on the Mac at a time, never a bare `-j`.
- Block A is done and committed (steps 1-7):

| Commit | What |
|---|---|
| abfd1c7e | MIDI lanes, notes and note ops in the arrange model |
| 1f210052 | Save/load of MIDI lanes, clips, notes |
| 3c55e136 | Piano Roll node plays its linked lane |
| b3651764 | .mid import, "Open in Piano Roll" on MIDI File |
| 0c0bd2b4 | Spec: patch tags, Block A status |
| 2a10e490 | NoteGen (types 1-8, form, critic gate, locks, drum map) |
| 5b49d545 | Step 7: sweeps, `determinism-notes` gate row, parity, status |

Passing on the branch: `INFINITE_PIANOROLLTEST` and `INFINITE_NOTEGENTEST` ALL OK (816 takes, 0 empty, melody critic 102/102),
`audio-pipeline-sweep` 19/19, Recipe compile parity 68/68, `gen-patch-skill --check` up to date, `piano_roll.inf` audio is
byte-identical twice.

Not green, not caused by this branch: `audio-node-sweep`'s param half, because `known-blind-spots.txt` lacks 169 rows that
are also missing on main. A separate session card covers it: "Reconcile the audio-node-sweep blind-spot list".
`wasm/parity_dump.js` was not run because emcc is not installed.

## Step 0: land Block A
Only with the owner's yes, and only on the Pro. Follow `git-branch-workflow`: merge `feature/piano-roll-a-model` into `main`,
then run `verify-gate`. Branch Block B off the result.

## Block B: the panel (branch `feature/piano-roll-b-panel`)
Goal: a usable piano roll through the canvas node alone. Load `infinite-design-system`, `node-ui-pillars`, `audio-node-ui`,
`timeline-arrangement-architecture` and `codebase-navigation` first.

Suggested commit order (a commit per step):
1. **Dock and pages.** The panel shell is Sample Studio's (`src/app/panels/SampleStudioPanel.cpp`): dock position, the top-bar
   toggle, View > Piano Roll, one page per MIDI lane, linking each page to its Piano Roll node, and the empty-page text.
   Wire it in the side-panel and menu frame code (`src/app/frame/StageSidePanels.cpp`, `StageMenuBar.cpp`).
   "Open in Piano Roll" (`StagePopupsA.cpp`) should open the page.
2. **Roll and clip strip.** Ruler, loop range, clips, the key column (`DrawMidiKeyboard` in `src/app/bodies/NoteBodies.cpp`),
   and notes drawn in the clip colour with velocity as fill depth. Scale highlight, fold, follow.
3. **Tools.** Select, draw, erase, blade and mute, with zoom and pinch, grid and snap, and audition. Every edit goes through the
   Block A note ops with undo.
4. **Inspector.** Multi-select editing with "mixed" values, and the expression lane: lollipops for velocity, release and
   chance, curves for bend, pressure and timbre.
5. **Quantize, functions menu, keys, help rows.** Keys work only while the panel has focus (the Timeline's rule, §4.7).
   `G` generates; the global Shift+G stays as it is.
6. **Generate.** The chip and type dropdown, the settings card in the inspector column, and takes (the last 6, chips, pin to
   keep). Regenerate a selection, lock rhythm and lock pitch, and drum pages (the drum-row key column).
   Also the save lines from §12.6, which were deferred from Block A: `gen <clipId> ...` and `gentake <clipId> seed=.. pin=1`.
   `NoteGen::Serialize`/`Parse` already produce and read that line; unknown keys are ignored.
7. **The known cost.** Every arrange revision bump rebuilds the audio topology (`ArrangeAudioRebuildIfStale`,
   `src/app/graph/AudioTopology.cpp:969`), so every note drag does too. MIDI-only changes should skip the rebuild, because the
   node reads notes through its lane and the routing does not change. Prove it with a test before relying on it.

Checks: `panels-sweep`, `shortcuts-sweep`, `node-ui-sweep`, screenshots in light and dark at 3 zoom levels, and driving the
panel in the app. Re-run `INFINITE_PIANOROLLTEST` and `INFINITE_NOTEGENTEST` after step 7.

## Useful hooks
- `INFINITE_NOTEGENTEST=1 INFINITE_NOTEGENSPEC="type=pads style=dub_techno seed=2"` prints one take's notes.
- `python3 tools/determinism-check.py --audio-only tests/headless/format/piano_roll.inf`
- `python3 tools/gen-patch-skill.py --check --bin build-dev/Infinite.app/Contents/MacOS/Infinite` (the default path is `build/`).

## After B
Block C (`feature/piano-roll-c-integration`): MIDI tracks in the Timeline and columns in Session, per-note gain and pan,
recording and retro capture, .mid export, ghost notes, select-by, generation types 9-12, Generate all parts, and Sample
Studio's "Notes to Piano Roll".
