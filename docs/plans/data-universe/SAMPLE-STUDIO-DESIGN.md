# Sample studio in the app: design for sign-off (draft 2026-10-29)

Status: **design only. Building waits for Naman's yes** (new panels need owner approval). This follows the plan that was already settled
(IMPROVEMENT-PLAN.md §11.1): blob and dots on the left, waveform on the right, export WAV, convert to patch, Shift+W, dock right/top/bottom.

## What it is
The website's Sample studio (`website/samples/studio.html`), as a docked panel in Infinite. Pick a style and a part, Generate, shape the take with
the dots, trim it, then drop it into the patch or export it. It drives the same engine as the Generate Samples node (`SampleStyles` compiled
into an `OfflineGraph`, rendered on a worker), so a take made in the panel and one made in the node are the same sound for the same seed.

## Layout (docked right; top and bottom docks use the same parts side by side)
```
+--------------------------------------------------------------+
| Style [deep house v]  Part [bass v]  Bars [2 v]   (Generate) |
+-------------------------+------------------------------------+
|   feel field (blob)     |  waveform, trim handles (16ths)    |
|   energy across,        |  play / loop / reverse / fade      |
|   mood up               |                                    |
|   tone . space . wild   |  takes: 1  2  3  (click to recall) |
|   (pills)               |                                    |
+-------------------------+------------------------------------+
| (Export WAV)  (Convert to patch)  (Drag to canvas)           |
+--------------------------------------------------------------+
```
- Feel field and pills: the same five numbers as the website (`tone, energy, space, mood, variation`, `gen_set_shape`). A dot move re-renders
  the same seed (pinned), so the instrument never swaps under the hand.
- Waveform: trim snaps to 16ths; reverse and fade as on the website. Takes keep the last few renders.
- Convert to patch: opens the take's `.inf` (the graph that made it) on the canvas, as the website's patch download does.
- Drag to canvas: makes a Generate Samples node holding that take (style, part, seed, dots), so it saves with the patch.
- Shift+W shows and hides it. To check during planning: plain W is used for canvas movement in `StageLinks.cpp:801`; Shift+W is free in the menu bar.

## The models behind it
- **Role critic (shipped with this branch):** after every first render (not pinned dot moves), the 132-number frame measurement of the take goes
  through the small network in `RoleCritic.cpp`; a part that does not sound like the part asked for is redrawn with the next seed, up to the
  existing 3 redraws, the last kept. The same code runs on the website (wasm) and in the node. Nothing on screen; a redraw is just a slower take.
- **Later (IMPROVEMENT-PLAN §10):** the dots stop being hand rules and ask the learned knob and sound models for the move, with the critic guarding
  the result. Only after those bars pass and a blind test.

## Website
The website studio stays the reference and keeps working as is. When the panel ships, both use the same engine build, so a share link's
(style, part, seed, dots) opens the same take in either.

## What sign-off decides
1. Panel (this design) or a bigger Generate Samples node body. The plan says panel.
2. Shift+W as the toggle.
3. Default dock: right.

## Build order once approved (each step its own commit; skills in brackets)
1. Panel shell, dock, Shift+W (`infinite-design-system`, `panels-sweep`, `shortcuts-sweep`).
2. Style / part / bars row and Generate on the node's worker path; takes list.
3. Feel field and pills (pinned re-render), waveform with trim, reverse, fade (`node-ui-pillars` for widget grammar).
4. Export WAV (credit tags as on the website), Convert to patch, drag to canvas (`data-accuracy-sweep` for save/load of the take).
5. Windows and Linux checks (`windows-parity`, `linux-parity`); the wasm parity test (`wasm/parity_dump.js`) stays green.

## Built 2026-10-29 (first cut, for Naman's review)
`src/app/panels/SampleStudioPanel.cpp`, Shift+W, View menu "Sample Studio", rail item (Synths family glyph until a studio icon is drawn).
- Engine: a private Generate Samples node (never in the graph). The node gained the dots (tone, energy, space, mood: saved, no control on the
  node), a pinned render (one attempt, lenient gate, as the website's shaped renders) and `keepSeed` for takes brought to the canvas.
- Done: style / part / length, Generate with progress and cancel, feel field (energy across, mood up), tone / space / wild, waveform with 16th
  trim, play / loop / reverse / fade, takes (last 6), Export WAV as heard, Save patch (.inf with an Audio Out), To canvas (a Generate Samples
  node with the take's recipe and trim). Review shots: `INFINITE_OPENPANELS=studio,studiodemo`.
- Not yet: top and bottom docks (right only), true drag to canvas (a button for now), credit tags in the WAV, opening the patch straight onto
  the canvas (saved to a file instead, so the open patch is never replaced), Windows / Linux runs, the panels-sweep.
