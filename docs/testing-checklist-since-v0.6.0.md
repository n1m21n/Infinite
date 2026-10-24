# User testing checklist: everything new on main since v0.6.0

Build under test: `~/Desktop/Infinite.app` (arm64 Release of main at f8955ad7).
Tick each box; write what you saw next to any that fail. Items marked **(Win)** need a Windows machine.
Every item comes from the commit list in `docs/commits-since-v0.6.0.md`.

## 0. Just fixed today
- [ ] **Analog FM:** add Analog, set Wave 2 to sine or saw, sweep FM 0 -> 1. Pitch stays on the note, no dives or bends, no stalling at high FM.
- [ ] Analog FM with oct2 / semi2 changed, hard sync on, and unison voices: still no pitch drift.
- [ ] Play chords (several voices) with FM up: every voice stays in tune.
- [ ] Old patches that use Analog FM still open and sound close (FM 0.3 was re-gated, so listen for a change in colour).

## 1. Session view (new panel, Live Loops style)
- [ ] Open Session: toolbar rail icon (grid), Shift+S. The panel docks and undocks.
- [ ] Add a clip with the plus in an empty cell; right-click clip menu leads with slot settings.
- [ ] Launch a clip: it waits for the grid (default 1 bar). Click a playing or queued clip: it stops or is taken back on the grid.
- [ ] Launch modes: **Trigger** (second click stops) and **Gate** (plays while held). Toggle is gone.
- [ ] Per-clip loop, loop length, launch quantize; per-scene quantize and keep-tracks-playing.
- [ ] Scenes: launch a scene, "Then Play Next Scene" runs in order, up-next highlight shows.
- [ ] **Stop All** (toolbar and `.`) stops everything.
- [ ] Session Audio mode: only launched clips sound; the Timeline's clips stay silent.
- [ ] Drag clips between cells, copy/paste a clip, duplicate a slot, delete a row while a clip plays.
- [ ] Drop files from Finder onto cells; Speed / Reverse appear only for file clips.
- [ ] Assign a canvas node to a clip; Add to Session from the canvas; Send Scene to Timeline.
- [ ] **Record** writes the performance to the Timeline; **Render Take** renders audio and video to a folder you choose.
- [ ] Save, quit, reopen: the Session grid comes back exactly.

## 2. Blocks and Show Set
- [ ] Every group has a power button in its frame; there is no "Make block" any more.
- [ ] Power off a group: its nodes stop cooking and go silent with a fade; power on brings them back.
- [ ] Show Set: patches only (blocks are groups). GO switches with one quantize and fade.

## 3. P8 GPU points (biggest visual change)
- [ ] Image to Points -> Transform -> Render 3D: looks the same as before, noticeably smoother at high counts.
- [ ] Video to points at ~262k points holds a high frame rate.
- [ ] Mesh to Points outputs points; Twist and Displacement act on them.
- [ ] **Displacement Noise source** (new): animate it, check it respects its params.
- [ ] **Feedback 3D** (new node): one-frame geometry delay with a loop pin. A geometry cycle elsewhere is refused with a message.
- [ ] **Point Trails** (new node): tapered fading trails behind moving points.
- [ ] Particle System on the GPU (up to 1M particles): emit pin and the new params work; save/load keeps them.
- [ ] Instance on Points: copies follow the points; opaque cubes at 100k stay fast.
- [ ] Field Modifier on points: the new presets (including Limit to Box and Project onto Shape) and the `life` attribute.
- [ ] Model 3D opens a `.ply` point cloud as points.
- [ ] **Bypass** Point Trails and Particle System: geometry passes straight through.
- [ ] Sketch3D no longer corrupts its mesh.

## 4. Render 3D
- [ ] HDR finish: bloom, AgX and Neutral tonemaps.
- [ ] 360 / dome output: Equirect 360, Cubemap 3x2, Fisheye dome.
- [ ] Text 3D refuses text over 2000 characters (no hang or crash).
- [ ] Image size params are capped at 8192 per side.

## 5. New and changed nodes
- [ ] **Shadertoy** is its own node: paste a shader from shadertoy.com and it runs. Formula is back to expressions only.
- [ ] **VMPC** 16-pad video player: pads trigger clips; cables to a switched-away pad draw dotted.
- [ ] **Chorder:** progression mode (pick the exact chords in the global key); the knobs are renamed (notes, length, oct up) and have tooltips.
- [ ] **MIDI File:** Global Scale snaps notes to the project key; layout is tidy (gap under piano roll, four knobs a row, loop bottom-left).
- [ ] **Drum Sequencer:** import `.mid` patterns by drop or the "My grooves" folder; accent pitch and accent vol pair up on the lane card.
- [ ] Quantized restart works on Arpeggiator, Note Sequencer and MIDI File.
- [ ] Waveform views: shared trim widget with play cursors on sample players.
- [ ] Blend mode menus no longer offer Overlay, Soft Light and Hard Light, but an old patch using one still shows it.
- [ ] Dotted cables appear for rate params hidden by sync/Hz mode (note nodes, effects, LFO/Random).
- [ ] Note Switcher synced mode has no stray gap above the manual checkbox.
- [ ] Material node: two balanced columns, unused rows grey out.
- [ ] PaulStretch unison and Grain Molder seed sliders: dragging or modulating them is stable, and the Molder keeps its seed on reload.

## 6. App-wide
- [ ] Keyboard: `-` and `=` step the octave; Shift+letter no longer plays a note.
- [ ] **Five new themes:** Infinite Midnight, Deep, Ember, Mist, Paper. Switch each; check contrast on nodes, panels and timeline.
- [ ] Sample library: browse folders with a breadcrumb; drag a folder in to make a kit.
- [ ] FILE > **Save with media** copies every file the patch reads next to it, saved relative. Move the folder and reopen.
- [ ] Help > **Collect diagnostics** produces one zip with system info, logs, settings and crash reports.
- [ ] Undo labels read clearly, e.g. "Edit 24 settings on Blur".
- [ ] Timeline video clips honour Fade In / Fade Out.
- [ ] Node search finds the shown name (type "spout") **(Win)**.
- [ ] Menus and dialogs in German, Spanish, Chinese, Japanese and Russian (251 strings added): no blank labels.
- [ ] Exported audio, images and video carry the "Made with Infinite" note (Settings > Audio > Credit Infinite in exported files). Turn it off and check it goes away.
- [ ] OSC and RPC edits respect Show lock.
- [ ] Quit is clean: learned profiles save before audio stops; no hang.

## 7. Robustness (try to break it)
- [ ] Open a patch with extreme or garbage values in effect, filter, Analog, Wavetable, sample player or Gain params: it opens, no NaN screech, values sit within the knob range.
- [ ] A damaged WAV, AIFF, HAP movie or glTF file is refused cleanly and does not hang.
- [ ] A very long formula in Formula or an equation node is refused, not frozen.
- [ ] Set tempo to 0 or a huge number (via RPC or a patch file): it clamps to 1..999. The top-bar field itself limits to 20..300.
- [ ] Molder with a huge saved generation value does not freeze the UI.
- [ ] A loaded patch's perf panel layout fits the panel.

## 8. Windows only
- [ ] Open, read and save files with non-ASCII paths (names with accents or CJK) **(Win)**.
- [ ] Video In closes without hanging on a stalled device and shows the mode list in the log **(Win)**.
- [ ] VST3 scan: if the scanner cannot run, no plugin gets blocklisted; the scan exits without a crash **(Win)**.
- [ ] Provenance FLAC stamp works with a non-ASCII path **(Win)**.

## Not in this build (do not test yet)
- ASIO on Windows (`feature/asio-driver`) is unmerged.
- The turbo / Windows-only line (0.61 - 0.71: path tracing, RTX, Filament Render 3D HQ) is a separate fork, not in main.
