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

## Redesign 2026-10-29 (Naman's notes: no text, dock top/bottom, timeline grammar)
- Dock: bottom (default) or top, like the Timeline and Scene docks; grip on the inner edge; right-click the toolbar to switch. Height saved in the panel layout (`panelStudioH`, `panelStudioDock`). The right dock is gone.
- Toolbar, icons only (tooltips carry the words): Generate (Refresh; green while making, click cancels), part dropdown (fades and settles in over `motion_on`, scrolls past nine rows), snap to scale (NoteSnap, the note nodes' icon; follows the project key and scale), Play (on the global transport), Loop. Pinned right: Studio Audio chip, Export, Convert to patch, Convert to node, Settings.
- Studio audio: `AudioMode::Studio`. Play switches to it; the canvas Audio Outs are bypassed exactly as Timeline audio does, and the top bar shows a Studio badge that hands audio back. The take plays through `StudioLoopPlayer` (in `AudioEngine`, after the topology, locked to the transport's beat position).
- Left: the website's blob shader (ported, straight alpha) reacting to 16 bands of the take at the playhead, and four glass dots (tone, energy, space, mood). Drag a dot: the blob leans and tints toward it, letting go re-renders the take's own seed pinned. Double-click sends a dot home.
- Right: the waveform on a 16th grid, start and end handles snapping to 16ths, the transport playhead (mapped through reverse and ping-pong).
- Settings (the clip inspector's grammar): pitch (semitones), fine (cents), speed (0.25-4x), loop, reverse, ping-pong, fade in/out, and for the next take style, length, wild, key and scale.
- Export: WAV 24, WAV 16, FLAC, MP3 to the Recordings folder (revealed), or WAV to a chosen path. The take as heard.
- Convert to patch: the graph that made the take plus an Audio Out, pasted as one undo step and wrapped in a Group. Convert to node: Generate Samples (the recipe, keepSeed, trim) or Sampler, Slicer, Molder, Grain Molder, PaulStretch, Granular loaded with the take as heard.
- Removed: takes list, all labels and status text.
- Not yet: Windows / Linux runs, panels-sweep, credit tags in exports, pitch shift is a simple grain shifter (not formant-aware).

## Round two 2026-10-29 (Naman's notes: more intuitive, symmetric toolbar)
- Toolbar, one row at tile height: Generate (text, Primary; a click while making cancels and makes again), instrument dropdown, snap to scale (icon), bars dropdown (a change makes a new take), Play, then Loop / Ping-pong / Reverse as text toggles with the Sampler's grammar. Pinned right: Export, Convert to patch, Convert to node (text), Settings (icon).
- Playback as the Sampler: all toggles off plays the take once from where play started, then the studio stops the transport; Loop repeats it locked to the grid; Ping-pong needs Loop; Reverse plays backwards either way.
- The four dots are gone (another idea later); the blob stays and still reacts to the take.
- Convert to patch: the headless graph has no positions, so the paste lays it out sources-left, sinks-right, then once the nodes have drawn it lays them out again by their measured sizes around the paste point (the patch-load auto-layout tick, without fitting the view). No stacked nodes.
