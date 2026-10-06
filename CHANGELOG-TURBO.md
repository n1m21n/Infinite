# Infinite-Turbo (for Windows) - changelog

## 0.50.0-turbo (2026-10-06)

### Quantized restart on sequencers
- **Chord Progression, Note Sequencer, Arpeggiator and MIDI File** get a mappable **restart**
  button and a **restart q** setting (immediate, next beat, next bar, 2 bars, 4 bars; default next
  bar). The sequence starts again from step 1 / chord 1 on the grid line. A blinking dot shows a
  restart waiting. Map it to MIDI, CV or a Performance button.
- Chord Progression now lands chord changes on the real block start (they were one audio block late).

### Chord names spelled out
- Qualities read **MAJ / MIN** in the all-caps UI: C MAJ, A MIN, A MIN7, C MAJ7, B DIM, C AUG,
  C SUS4. Plain numbers stay (C7, C9). Long names wrap root over quality.

### Note Switcher
- **8 inputs** (the **inputs** setting shows 2-8 pins; old patches keep 4).
- **switch q**: in manual mode a slot change waits for the next beat / bar / 2 / 4 bars and lands on
  the exact sample. Pending slot shows in amber.
- **slot 1..8** buttons (mappable) pick a slot and turn manual on.

### Scenes node (Macros): one button, several changes
- A radio-button scene launcher: up to 8 scenes x 8 outputs, cabled to any params. Entering a
  scene sets every output to that row, so what is ON in another scene and OFF here switches off.
- Output modes: **on/off** (default: mutes, switches), **choice** (switcher slot, drum part:
  follows the cabled dropdown), **level** (0..1), **pulse** (a trigger on entry: restarts).
- **Off row** on top of the grid: pressing the playing scene again goes there (option), plus a
  mappable **all off**. **q** quantizes every change; the queued scene blinks.
- **scene 1..8**, prev / next and the selector are mappable; a Performance button bound to a
  scene lights while that scene plays.
- Performance Mode: right-click a control, **+ Add Another Parameter...** to drive several params
  with one control. MCP: new `scenes` tool, `perf_add` with `element`.

### MIDI channels
- **MIDI Notes**: channel is a dropdown (omni, ch 1-16), a **learn** button takes the channel of the
  next note, and "last note in: ch N" shows what arrives (marked when filtered out). Example:
  MiniLab keys on ch 1, pads on ch 7, one MIDI Notes per channel.
- MIDI CC and MIDI Trigger get a channel dropdown; MIDI Trigger no longer fires when edited.

### Drum Sequencer
- **Import .mid**: button, drag and drop, or MCP. GM drum notes go to the 8 lanes, quantized to 16ths
  (swing or triplets detected), bars from the file, velocity 100+ as accent, into the live part.
- **Rate and swing stay** when switching A / B / C.
- **Edits are kept** per part and per groove; **revert** reloads the library part.
- **Your MIDI folder**: .mid files in `%LOCALAPPDATA%\Infinite\DrumPatterns` (subfolders, or an
  index.json with title, style, bpm) join the matching library group by style (electro-funk in
  Electronic, r-b in Funk & Breaks), tagged MIDI; unmatched styles get a "MIDI: <style>" group. A / B / C are
  the file's first distinct 8-bar chunks; the tempo is not changed. Rescan / open folder in the menu.
- **User presets**: save / load by name (all parts, rate, swing) in `%LOCALAPPDATA%\Infinite\DrumPresets`.
- Lane trims survive patch load, undo and paste.

### Super Mixer master
- Stereo **VU** (peak, RMS, peak hold, clip LEDs), **master pan** and **MUTE** (click-free).
- **Mastering chain** with a bypass each (all off by default): 3-band EQ, glue compressor,
  stereo width, saturation and a mastering limiter, with gain-reduction meters.
- **Limiter**: true-peak (4x oversampled) ceiling in dBTP, lookahead 0.5-5 ms with a smooth
  attack, auto release (fast for transients, slow for dense material), stereo link, styles
  (transparent, punchy, loud), GR history. Its lookahead is reported to delay compensation;
  with the limiter off there is no delay.

### Visuals
- **Slideshow**: mappable restart / prev / next.
- **VMPC**: transition styles between clips (the Slideshow set) and transition time; default Cut.

### Other
- New nodes always appear in front of existing ones.
- **Equation Synth**: 30 new presets (pads, plucks, basses, bells, leads, organs, percussion,
  drones), grouped by category, with envelope, filter, unison and drive.
- Grain Molder saves its sample file in the patch (it reopened empty before).

## 0.49.0-turbo (2026-10-03)

### Waveforms you can trim by eye
- Sampler, Drum Sequencer lanes, MPC pads, Slicer, Molder, Grain Molder, Granular and Paul
  Stretch draw a real waveform: min / max with the RMS body inside, one column per screen pixel,
  finer as you zoom the canvas in (a multi-resolution peak cache built once per sample).
- **Play cursors**: a line with a small head for every sounding voice (Sampler note voices and
  audition, each Drum lane voice, MPC pads, Slicer, Molder, Granular...), placed on the whole
  sample so it lines up with the trim handles.
- **Zoom to trim**: the magnifier chip in a waveform's corner shows only start..end (plus a
  margin), with a bar locating it in the whole sample.
- **Trim handles**: the time shows while dragging (1.234 s / m:ss.mmm), Shift drags 10x finer,
  grabbing no longer jumps, and the range can go down to 0.1% of the sample.

### Side panel: Samples and Media by folder
- **Library folders** you manage: Add folder, Rescan, rename or remove a root (files stay on
  disk); the bundled Turbo kits are added once as "Turbo kits". Saved in
  `%LOCALAPPDATA%\Infinite\SampleFolders.json` / `MediaFolders.json`.
- **Browse by folder** with a breadcrumb (Library / root / kit / sub): subfolders first with
  their file counts, then the files. Typing in the search box searches every folder and shows
  where each hit lives.
- **No duplicates**: a file reached through two roots, a root inside another root, different
  spellings of the same path or a link loop is listed once.
- **Drag a folder** onto a Drum Sequencer (lanes 1-8) or an MPC (pads 1-16) to load its audio
  files in name order; onto empty canvas it creates a Drum Sequencer with them. Single samples
  can now be dropped onto an MPC pad too.
- Scans run in the background and only the new folder is scanned when one is added.

### Crash fix: plugins
- **Crash writing MIDI into a plugin that was being reloaded or re-prepared** (seen in a crash
  dump: the pitch-bend range message going into a freed MIDI buffer). The plugin node now holds
  the plugin for its whole audio block, MIDI included, and reload / prepare / remove wait for it;
  a plugin still busy after 2 s is left in memory instead of freed under the audio thread.
- Release builds now write a PDB next to the exe, so future crash dumps can be read with names.

### Mappable controls
More node buttons go through the param machinery, so they can be MIDI-learned (Ctrl+M or
right-click), driven by a CV cable and put on the Performance panel:
- **Drum Sequencer** pattern picker: a `part` selector (A / B / C; a CV, MIDI knob or
  Performance selector switches the part once when it changes), the **A verse / B bridge /
  C chorus** buttons as triggers (a MIDI pad fires "B"), **< / >** groove as triggers
  ("prev groove" / "next groove") and `groove` (index in the shown category). A driven change
  from MIDI or the Performance panel is one undo step; a CV cable adds none.
- **Slicer**: Record, Audition (state buttons like the Sampler's), re-slice (trigger).
- **Molder**: Record (state), Roll, Iterate, Reset (triggers).
- **Grain Molder**: Record, Audition (state).
- **CV Recorder**: record / stop (state: high records, low stops).
- **Predictive Rhythm / Predictive Notes**: Learn / Stop (state).
- **Clip Matrix**: REC > ARR (state).

### From upstream Infinite
- **Sampler**: 16 voices (was 8); a file shared by several Samplers or reloaded is decoded once
  (decode cache).
- **Timeline clip pitch** for clips of a Sampler or an Analog (a "pitch (st)" slider in the
  clip settings), added on top of the node's own pitch; it lasts only while the clip plays.
- **Random Note**: style "melodic" (upstream's melody picker with bar / beat accent velocities)
  next to the original "walk" (default, so old patches play the same).
- **Geometry point size**: Image to Points and Depth Projection get "size relative to cell"
  (upstream's point base size: point size 1 matches the cell, carried through Geometry Op, Set
  Color and Switcher 3D into Render 3D). Off by default, so old patches look the same.

## 0.48.0-turbo (2026-10-03)

### New: MIDI File node (Notes)
- Plays a Standard MIDI File (.mid / .midi, formats 0 and 1) in time with the transport: positions
  are in beats, so the file follows the app tempo ("use file tempo" sets the transport to the
  file's). Playback starts on the quantize grid (default 1 bar) when the transport starts, a file
  loads, play is turned on, after a seek and at each loop.
- Track (all or one) and channel filter, transpose, velocity scale, loop with length in bars, a
  piano-roll preview with the playhead. Note-offs are sent on stop, loop, seek and changes.
- Drop a .mid on the canvas to create one, or on a MIDI File node to load it there. Own parser,
  robust against damaged files.

### From upstream Infinite
- **Looper**: one simple **take length** menu (free, 1/16 to 1/2 bar, 1 to 32 bars) replaces the
  mode / bars / sub-bar controls; speed, pitch and fine tune (varispeed, like upstream; away from
  1.0 overdub pauses), fade in / fade out per pass, output volume.
- **Looper PLAY in time**: with "in time" on (the old "sync to bar") and the transport playing,
  PLAY and DUB now wait for the next bar line like REC already did (PLAY blinks, "play in 1.2 s");
  pressed just after a line they start at once, already in phase. Pressing PLAY again cancels.
  REC and PLAY also lost a one-audio-block late start (the grid was read at the block's end).
- **MPC**: per pad speed (negative plays the trim range backwards), fine tune, fade in / out and
  sync to a musical division (a hit waits for the next grid line).
- **Macro Trigger and Performance bangs** fire Looper buttons and MPC / VMPC pads on every
  trigger (before, toggles reacted to every second one).
- **Audio In**: pick a stereo pair, a single input or a mix of all inputs, listed with the
  device's channel names; warns when the patch was set up on another input device.
- **Timeline**: live waveforms of clips from node audio are measured at 1/16 beat with min / max,
  like upstream.
- **Random Note**: groove (swing of the odd steps, also longer than an audio block).
- **Sampler**: position (start point inside start..end) and decay (0 = held, as before).
- **Metronome**: the top bar CLICK is now a drawn metronome whose rod swings with the beat.
- Patches without node positions wait for the measured node sizes before the automatic layout.

### Notes
- New saved keys are appended, so older patches load and sound as before. A CV / MIDI mapping on
  the old Looper mode, sub-bar or "sync to bar" controls needs to be redone on the new menu.
- MCP: `looper` takes `length` ("2 bars", "1/4 bar", "free") and `in_time`, and reports
  `length`, `in_time` and `waiting_s`; `looper` and `pads` accept the new params (`pads` also has a `state` action), and invalid
  values are rejected without changing anything.

## 0.47.0-turbo (2026-10-02)

### Claude chat inside the app
- **CLAUDE** in the top bar (or Shift+C, or VIEW > Claude chat) opens a chat window. It runs the
  Claude Code CLI you already have, logged in with your Claude account (Pro / Max), so it costs
  nothing beyond your plan and needs no API key. Its only tools are Infinite-Turbo's own MCP
  tools, so Claude reads and edits the open patch, looks at the picture and loads grooves, the
  same as from Claude Desktop. Model menu (default, sonnet, opus, haiku), New chat, Stop; the
  conversation continues between messages. Every change is one undo step.
- Not installed yet? The window shows the one-line cmd install
  (`curl -fsSL https://claude.ai/install.cmd -o install.cmd && install.cmd && del install.cmd`)
  and asks you to run `claude` once to log in. `INFINITE_CLAUDE_PATH` points it at another exe.

### Drum Sequencer: pattern library
- **141 grooves in 10 groups, each with three parts**: A verse (main groove), B bridge (lighter
  or breakdown) and C chorus (fuller, often two bars ending in a fill). Rock & Pop, Funk & Breaks
  (Funky Drummer, Amen, Impeach, Levee feels), Hip Hop (boom bap, Dilla, trap, drill, Baltimore /
  Jersey club, footwork), Electronic (house, techno, garage, DnB, jungle, dubstep, trip-hop
  Massive Attack / Portishead feels, Björk feels, big beat Prodigy / Chemical Brothers feels,
  IDM), Latin & Caribbean (son and rumba clave, cascara, tumbao, salsa, mambo, cha-cha-chá,
  songo, mozambique, bembé, guaguancó, reggaeton, cumbia, merengue, bolero, reggae, ska, soca),
  Brazil (ijexá, samba batucada, partido alto, bossa, baião, xote, arrasta-pé, xaxado, maracatu,
  samba-reggae, frevo, coco, ciranda, funk carioca tamborzão and Volt Mix, axé), Middle East &
  North Africa (maqsum, baladi, saidi, malfuf, ayoub, chiftetelli, samai 10/8, karsilama 9/8,
  aksak, fallahi, gnawa), India (teentaal, keherwa, dadra, rupak, jhaptaal, ektaal with bayan /
  dayan and doubled-bol fills, bhangra), Africa & World and Jazz.
- The Brazilian, Afro-Cuban, Caribbean and Middle Eastern grooves were rewritten part by part
  from the traditional ensembles (agogô, atabaques and xequerê in ijexá, surdos 1/2/3, caixa,
  tamborim and agogô in the batucada, zabumba and triângulo in baião, dum / tek / ka in the
  doumbek rhythms...). Song-named entries are feel sketches, not transcriptions.
- **Picker**: group, groove (only that group's, so the list is short), < > to step through
  grooves, and A / B / C buttons that switch the part in one click while it plays. The groove
  and part are saved with the patch. A pattern sets steps, rate (triplet grids for 6/8, 12/8
  and swing) and swing; the groove shows a suggested tempo.
- **Accent pitch** per lane (lane card): accented steps play that many semitones away, so one
  lane plays a two-tone bell (agogô, campana, cencerro: X = low bell, x = high). Grooves set it.
- Lanes show their role when empty (1 kick, 2 snare, 3 closed hat, 4 open hat, 5 clap / rim,
  6 low tom / conga, 7 high tom / conga, 8 bell / ride). Empty lanes get the bundled **Turbo kit**
  (eight synthesized one-shots in `assets/drumkits/turbo-basic`, the bell an agogô), hats in one
  choke group.
- MCP: **`drum_pattern`** lists the library (optionally one category) or loads a groove and part.

### Chord Progression: more chords
- 26 new builder qualities: 11 (9sus4), m11, maj7#11, maj9#11, 13, m13, maj13, 13sus4, 9#11,
  m(maj9), 7b9, 7#9, 7#11, 7b13, 7alt, m7b9, 7#5, 7b5, maj7#5, dim(maj7), 6/9, m6/9, madd9, add11,
  quartal and the "so what" voicing. The quality menu is grouped (triads, 6ths & adds, 7ths,
  9ths / 11ths / 13ths, altered, voicings). Saved patches keep their chords.

### Fixes
- **File > New crashed while audio was playing** (e.g. right after startup, with the example
  running): New dropped the nodes but never told the audio thread, which kept processing freed
  nodes. New now republishes the (empty) audio graph, and removed nodes are destroyed only after
  the audio thread has finished two blocks without them. New also clears the Performance panel.

- **Dropping a WAV onto a Drum Sequencer lane that was sounding crashed**: the lane's voices kept
  reading the replaced sample after it was freed. Its voices now stop the moment the new sample
  takes over (also on the lane's x and when the kit loads).

### Small
- Long dropdowns get a filter box; dropdown items with the same name no longer collide.

## 0.46.0-turbo (2026-10-02)

The rest of upstream's Windows-relevant features: Performance Mode, MCP phases 3 and 4, and the
small items. Left out on purpose: the Field language and everything macOS / Linux / headless only.

### Opens with an example
- Infinite-Turbo starts on **superSynthMCP**, a bundled example (two Analog synths through delay,
  mixer and reverb, note sequencers switched from a Performance page, audio-reactive kaleidoscope /
  glitch / bloom visuals), already playing. It opens untitled, so Save never overwrites it.
- Settings > Startup: "Open the example at startup" (on by default; off = start empty) and
  "Open the example now". A `.inf` given on the command line or an autosave to recover wins.
- `release-windows.bat`: clean build + package, versioned ZIP and release notes in `dist\release`,
  a local `v<version>` tag, and the GitHub "new release" page opened for the upload.

### Performance Mode (from upstream)
- **PERF** in the top bar, **Shift+P** or VIEW > Performance mode: a dockable panel (bottom, top, left
  or right) with pages of controls for playing live: knob, fader, slider, toggle, XY pad, trigger,
  number box, radio selector, bipolar knob, step gate.
- Each control is bound to any node parameter: right-click it > Assign Parameter..., then click the
  parameter on the canvas (the nearest one in the node under the mouse lights up). Or right-click a
  parameter's pin > "add to Performance".
- Edit mode (move, resize by type, rename, colour, copy / paste / duplicate, pages: add, rename,
  duplicate, reorder, delete) and Perform mode. MIDI learn per control (two axes on the XY pad).
- Saved with the patch in upstream's format (`perfui` / `perfname` / `perf` / `perftarget` /
  `perfmidi`), so the panels open in both.
- Discrete parameters (dropdowns, checkboxes, integers) follow the panel too.

### Claude / MCP
- **`screenshot_node`** and **`render_frame`**: Claude can look at any node's image or the live Output
  (JPEG / PNG, downscaled); `render_frame path` also saves the Output at full size.
- **`authoring_guide`** tool and **`build_patch`** prompt: the patch-building guide for AI assistants.
- **Turbo-only MCP tools**: `clip_matrix` (launch / release clips, scenes, stop rows, read state), `pads`
  (hit MPC / VMPC pads), `looper` (record, play, overdub, undo, state) and `perf_list` / `perf_add` /
  `perf_remove` / `perf_show` (build and open the Performance Mode panel).
- The authoring guide (and the AI skill) gained a **Turbo-only features** section: Clip Matrix, MPC /
  VMPC, Looper, Super Mixer, Layout, Projection, Spout, Transport Control and Performance Mode keys, so
  Claude can build patches with the nodes that only exist in Turbo.
- Settings menu, "AI assistants": Connect to Claude Desktop (same as `setup-mcp.bat`), Install AI skill
  for Claude Code (`%USERPROFILE%\.claude\skills\infinite-turbo-patching`), Save AI skill to a folder.

### Small ports (from upstream)
- **Single instance**: double-clicking a `.inf` with Infinite-Turbo already open opens it there (with
  unsaved edits it asks first: save then open / discard / cancel). Paths with accents work.
- **Interface scale**: the UI follows Windows' display scale per monitor (and changes when the window
  moves to another monitor). Settings > Interface scale: Auto or a fixed 80-200%.
- **Fit**: offset X / Y in output pixels (+ right, + up), modulatable.
- **CLICK** in the top bar: a metronome on the audio output while the transport plays, no node needed;
  the button flashes on the beat.
- **Update check**: one request at startup to this fork's GitHub releases; a newer one shows an
  UPDATE badge (opens the release page). Settings > Updates turns it off.

## 0.45.0-turbo (2026-10-02)

Upstream bug fixes, an MCP server for Claude, and three upstream patching aids.

### Build patches with Claude (MCP)
- **`Infinite-Turbo.exe --mcp`** is an MCP server (stdio) that edits the patch open in the running app
  over the local control port. If the app is closed, the first request starts it.
- **`setup-mcp.bat`** registers it in Claude Desktop's config (other servers kept, `.bak` of the old
  file). Claude Code: `claude mcp add infinite-turbo -- "<path>\Infinite-Turbo.exe" --mcp`.
- New RPC tools: `describe` (node types; per type its inputs, outputs, settings and help), `explain`
  (live graph with connections, param names and ranges, modulation, warnings), `batch` (one undo
  step, all or nothing, `$N` references), `get_patch_text` / `validate_patch_text` /
  `load_patch_text`, `modulate` / `set_expression` / `unmodulate` by param name (waits for a new node
  to draw), `auto_layout`, `transport`, `patch_format`.
- `create_node` takes `settings` and places the node by itself when no x/y is given; `connect` takes
  slots and outputs by label ("B", "audio"); `set_param` ignores case, spaces and underscores.
- The control server serves each client on its own thread and sends large replies whole.

### Patching aids (from upstream)
- **Live reload**: the open patch file is watched. A change made outside (an editor, git, an AI)
  reloads it as one undo step; with unsaved edits a banner asks Reload / Keep mine.
- **Auto layout**: nodes without a `pos` line are laid out by signal flow once they have drawn.
- **Live hints**: an amber border and a tooltip on a half-wired Blend or two-input filter, an Output
  with nothing connected, and an image loop without a Feedback node (300 ms after the last edit).

### Fixes (ported from upstream)
- Transform moves curves and point clouds too; Switcher 3D and Set Color pass curves and clouds on.
- Depth Projection and Image to Points no longer apply their tint twice (it was squared), and
  Depth Projection no longer applies its colour image twice.
- Distribute on Faces re-bakes the point colours when the inherited material or its colour changes.
- Render 3D draws translucent meshes back to front without depth write, so one does not hide the
  other behind it.
- A source wired to both an Audio Out and an Output is mixed into the device once (it was doubled).
- Gesture loops follow the render's video time during a timeline render (they played at the speed of
  the UI frame rate).

### Fixes (Turbo)
- Undo no longer forgets which file is open (the title lost its name and Ctrl+S asked for a new one).
- Undo, redo and reloads keep the node numbers of the saved patch when they can.

### Docs
- README rewritten: quick start, what Turbo adds, MCP, shortcuts, build, troubleshooting.

## 0.44.1-turbo (2026-10-02)

### Fixes
- **Crash during the timeline WAV / MP4 render**: while the main thread renders offline the device callback
  outputs silence, but it skipped its liveness timestamp, so after 0.75 s the audio recovery thought the
  device had died and reopened the ASIO driver in the middle of the render (crash inside the driver).
  The engine now stays "alive" during a render and device recovery waits until it ends.
- Time-stretch buffers (Audio File Player clips, Clip Matrix voices) are no longer reallocated on every
  audio topology rebuild when nothing changed (avoids freeing memory the audio thread may still be using).

### Timeline
- **Rename tracks**: double-click the track name or any free spot of the track header, press F2 on the
  selected track, or right-click > Rename. Enter or clicking away confirms, Esc cancels, an empty name
  returns to the default (A1, V1...).

## 0.44.0-turbo (2026-10-02)

### Clip Matrix (session view / clip launcher)
- **New node Clip Matrix** (Source): rows are tracks, columns are scenes (4 x 8 by default, up to 16 x 16).
  Audio, video and image clips, dropped on the cells or loaded from the cell menu.
- **Quantized launching on the audio thread**: clips, scenes and stops start on the next grid line
  (None / free, 1/16 to 4 bars; global or per clip), sample-accurate inside the block.
- **Modes**: loop (default), once, gate (plays while held). Launching on an empty cell stops the row.
- **Audio clips** are tempo-aware: detected BPM, sync to tempo (stretched, pitch kept), pitch per clip.
  Video clips follow the same clock, with their soundtrack.
- **Follow actions** per clip (after N bars or the clip length: stop, again, next, previous, first, last,
  any, other, with a chance for action B) and **scenes with tempo / time signature**.
- Every cell, scene and stop has a CV pin; MIDI notes launch cells (base note + row x scenes + column).
- **REC > ARR** records the performance into the Arrangement Timeline (one audio / video track per row,
  each launch and loop pass a clip, one undo step).
- **Clip Matrix Out** (AudioUtility): splits the matrix into one video + one audio output per row, like
  MPC Out. Cables now remember which output of a multi-output node they come from (saved as an optional
  4th token on `cable` / `aud` lines; older files load unchanged).

## 0.43.2-turbo (2026-10-02)

- **Spout In / Spout Out**: the Syphon nodes are named after what they are on Windows (Spout2), in the
  node browser, help and docs. Patches with the old `Syphon In` / `Syphon Out` names still open (renamed on
  load, like Set Color).

## 0.43.1-turbo (2026-10-01)

### Arrangement Timeline, rest of upstream's feature set
- **Video follows the playhead**: every Video clip is locked to the timeline (jumps, loop, scrub), with
  a movie offset; "lock video to timeline" off lets it run free.
- **Render / export queue** (RENDER...): MP4 / MOV with audio, or 32-bit float WAV, of the whole
  arrangement, the loop, the selected clips or a bar range; offline, one frame at a time, so picture and
  sound stay frame-accurate. Several jobs run one after another; cancel / show file.
- **Track groups (folders)**: + GROUP, track menu > Group, collapsible group rows with on/off, colour,
  rename, nesting, duplicate, ungroup / delete; clicking a group row selects its clips.
- **Waveforms and thumbnails**: audio clips show the file's waveform (analysed in the background) or,
  for live sources, what they played; video clips keep a film strip of frames captured while playing.
- **3D nodes** can be video clips (rendered like their mini viewport).
- **Audio Samples**: tempo detected on import, "sync to tempo" stretches them to the project tempo
  without changing pitch (Signalsmith Stretch, MIT, vendored), per-clip pitch +/-24 st, /2 x2 reset.
- **Per-clip modulation bypass**: a clip can hold any of its source's modulated knobs still while it
  plays.

## 0.43.0-turbo (2026-10-01)

### Arrangement Timeline (port of upstream's)
- **Docked timeline panel** (VIEW > Arrangement timeline, Shift+T): video and audio tracks, clips in
  bars/beats (upstream's model and patch lines verbatim, so arrangements open in both), ruler with
  scrub, markers, loop range, snap grid (bar to 1/16, triplets; Alt = free), zoom, follow.
- **Clips** point at canvas nodes: right-click a node > Add to Timeline, double-click an empty spot
  on a track, or drop audio / video / image files on the panel (each gets its own source node as a
  "Sample" clip, locked to the timeline). Move (across tracks), trim, fades, split, blade, duplicate,
  copy/paste, groups, enable/disable, colours, rename, clip settings window.
- **Timeline mode** (TIMELINE button): the timeline owns the audio output. Audio clips are gated,
  faded and panned per sample with track gain/pan/mute/solo and meters; canvas Audio Outs go quiet
  (live ones stay).
- **Video**: the video tracks are composited at the playhead (32 blend modes, opacity, fades as
  crossfades, brightness/contrast/saturation), shown in the panel monitor and available as the new
  **Timeline** source node, so the arrangement can go to an Output / projector.
- Transport gains seek and a loop range; nodes get a stable `uid` saved in the patch.

## 0.42.2-turbo (2026-10-01)

### Looper
- **No more late first take**: every buffer (two 120 s banks and a 1 s pre-roll) is allocated and
  committed on the first frame after the node is inserted, and the audio thread keeps the pages
  resident, so the first take never stalls on page faults.
- **Late REC presses land on the grid**: with sync on, a REC pressed up to 200 ms (or 1/4 of the
  grid) after a bar / sub-bar line starts the take on that line, back-filled from the pre-roll,
  instead of waiting a whole bar. While armed, the strip shows how long until the take starts.
- **The loop is saved with the project**: the current loop is written as a 32-bit float WAV in the
  Recordings folder (`looper_<id>.wav`) and reloaded when the patch opens. A whole-patch undo no
  longer wipes the loop either (the respawned node re-adopts it).
- **UNDO / REDO of takes and overdub layers** (with CV pins). UNDO during an overdub closes the
  layer and removes it; a new layer after an undo drops the redo branch. Up to 32 steps.
- **EXPORT WAV**: writes the loop, all layers summed, to a 32-bit float stereo WAV.
- Paste / duplicate of a Looper copies its loop into an independent looper.

## 0.42.1-turbo (2026-10-01)

### Metronome
- **Transport Control has a metronome**: `click` on/off (a pin, so a MIDI pad or Macro Toggle can
  switch it), `volume`, `accent` (higher click on the first beat of the bar). Plays while the
  transport runs, locked to the same beat clock as the sequencers.
- **Two routes**: `direct` mixes it straight into the audio device output, with no cable and
  without touching any audio connection (and it stays out of Audio Out recordings); the new
  `click` output pin sends it into the graph, for a mixer, an effect or an Audio Out.

## 0.42.0-turbo (2026-09-30)

Tempo, key and scale stay with the project and can be driven without the mouse; wave B of the
upstream catch-up.

### Transport
- **Patches save BPM, time signature, key and scale** (upstream `transport` line, so patches stay
  compatible both ways). A new document starts at 120 / 4/4 / C major. Undo does not move them.
- **Tempo changes no longer jump the playhead**: while audio runs, a new tempo lands at the next
  block after re-basing the clock (upstream fix). Before, every tempo edit during playback
  re-measured the whole elapsed time.
- **Transport Control** (Control, Turbo original): PLAY / STOP / REWIND / TAP buttons, each also a
  trigger pin; bpm with glide (accelerando/ritardando), follow external MIDI clock, key and scale
  (optionally waiting for the next bar), time signature. Every control is a modulation pin, so a
  MIDI controller (MIDI learn), LFO, Macro or sequence can drive them. A section only takes over
  while its `drive` switch is on; off, it mirrors the live transport. `T` taps tempo while the
  pointer is over the node. AUDIO starts/stops the audio engine; `audio on open` starts it
  whenever the patch is opened. Outputs (modulators): `beat` and `bar` (0..1 ramps locked to the
  transport), `bpm` (20..300 as 0..1) and `play` (1 while playing).
- **Settings > Audio > "Start audio when Infinite opens"** (off by default, machine setting).
- **Chord Progression `sets key`**: each chord sets the global key and a matching scale, so
  Quantizer, Random Note and other key-aware nodes follow the progression.

### From upstream
- **Gesture recording**: hold Shift while dragging any knob or slider to record the movement;
  releasing Shift loops it (while the transport plays). A green dot on the pin marks a looping
  param; grabbing it without Shift takes it back. Saved with the patch.
- **glTF / GLB import**: dropping a .gltf/.glb builds Model 3D + Material + one Image Source per
  texture map (albedo, roughness, metallic, normal, AO, emission), already wired, in one undo
  step. Dropped onto an existing Model 3D it just reloads. FBX, DAE and 3DS drops also open.
- **Sampler fade in / fade out** per pass (replaces the loop crossfade; the old key is ignored).
- **Patch reader** tolerates CRLF line ends and a UTF-8 BOM (files edited in Notepad or by tools).
- **Crash reports**: an unhandled crash leaves a minidump and a log line in
  `%LOCALAPPDATA%\Infinite\crash`. `diagnose-windows.bat` now includes a system report
  (GL, audio driver and devices, MIDI, VST3 blocklist) and the crash log.

### Looper
- A Macro Trigger or MIDI pad on REC / PLAY / DUB presses the button once per hit (footswitch
  style) instead of holding it only while high.

### Not ported
- Instance selection (needs upstream's instance-realize geometry refactor).
- Upstream's browser sort/filter strip and 9-category taxonomy: our browsers already sort and
  filter, and our menu organisation differs.
- Upstream's right-click MIDI learn: we keep our own MIDI learn mode.

## 0.41.0-turbo (2026-09-30)

Wave 1 of the upstream catch-up, plus a chord progression node.

### New nodes
- **Chord Progression** (Notes, Turbo original): a looped chord progression locked to the transport.
  Up to 16 chords, each set by clicking keys on a two-octave keyboard or filled from a root +
  quality builder (maj, min, 7, maj7, m7, dim, aug, sus2/4, 6, m6, 9, maj9, m9, m7b5, dim7, add9,
  7sus4, m(maj7), 5), with `inv` for the next inversion. Each chord lasts its own number of bars
  (half-bar steps). Chord names are recognised from the keys (including slash chords). Knobs:
  chords, octave, transpose, velocity, gate (1 = legato); `bass` doubles the lowest note an octave
  down. Play modes: block, strum up/down (`strum` ms between notes), arp up/down/up-down/random
  (one note per `rate` step over 1-3 `octaves`), pulse (chord re-struck every step), alberti
  (low-high-middle-high) and bass + chord (oom-pah). Chord changes and steps are sample-accurate.
  Plays while the transport runs, releases on stop.
- **Analog** (Synths, from upstream): polyphonic virtual-analog synth, two oscillators with sync,
  unison, sub, noise, ZDF ladder / SVF filters, amp envelope.
- **Slicer**, **Molder**, **Grain Molder** (Synths, from upstream): onset/grid sample slicer played
  from notes; partial-spectrum resynthesis; granular molding. Samples load by dragging from the
  Samples panel onto the node.
- **Geometry Table** (Modulators, from upstream): samples a mesh / point cloud / slice contour into
  rows of x/y/z modulator outputs plus centroid and spread. Instanced sources read as their base
  mesh for now.
- **Depth Projection** (3D, from upstream): turns a depth map (plus optional colour) into a point
  cloud or torn triangle mesh (perspective, planar, radial, cylindrical).

### Upstream features
- Compressor transfer curve shows live gain reduction.
- PaulStretch: modulatable `position` and waveform scrub.
- Wave Terrain: shows the baked wavetable stack.

### Fixed
- Nodes with 10+ outputs: the output pin block overflowed into the next node (Image Analyze `cy`,
  Audio Analyze `b6`-`b8`). Pin stride widened from 1000 to 1050, as upstream.

### Comb filter (from upstream)
- `comb +` / `comb -` filter types (feedback delay line; cutoff sets the tooth spacing, resonance
  the depth) in Wavetable, Oscillator, Equation, Wave Terrain, Image Spectral Synth, Metallic,
  Audio Filter and EQ. The response curves draw the comb teeth.
- Equation, Wave Terrain and Image Spectral Synth now offer the whole shared filter list (lp/hp
  12/24/36, bp 12/24, notch 12/24, comb). Unlike upstream, their saved filter index keeps its old
  meaning: the original five come first and the rest are appended, so old patches do not change
  filter. Metallic keeps its storage order as upstream does.
- Audio Filter's comb follows the env and mod sweep like the other types (upstream uses an LFO).
- Metallic modes are normalised for their strike peak (upstream fix in the same change): louder,
  and long decays no longer fade into silence early.

### Renamed
- **Set Color** is now **Set Vertex Color**, as upstream. Old patches load it under the new name.

## 0.40.0-turbo (2026-09-26)

Performance pass, then more upstream features.

### Performance
- **Off-screen nodes skip their body** (ported from upstream): a node well outside the view keeps its
  last box and pins (cables still land) and is not drawn. Always drawn: selected, dragged, nodes with
  modulation / MIDI / expression / palette bindings, anything while a popup or MIDI learn is open,
  the Keyboard node, and every node once per 30 frames. `INFINITE_NO_CULL=1` turns it off.
- **Image chains settle**: Noise, Shape, Blend, Fit, Layer Stack, Ramp, Color Ramp and Curves only
  re-render when their params, inputs or size change, so a still chain stops recooking everything
  downstream every frame. Filters cache while the transport is stopped even if they read time.
- **Separable blurs**: Gaussian Blur, Box Blur, Bloom, Diffuse Glow, Unsharp Mask and Outer Glow run
  as two passes (2N texture reads per pixel instead of N squared), through one shared scratch
  render target per size. Filter uniform locations are looked up once, not every cook.
- **Video / camera upload**: decoder and capture threads hand over BGRA (the GPU's native upload
  format); the render thread no longer pays a 3-byte BGR swizzle per frame.
- **Undo / delete / autosave**: the patch snapshot is O(N) (was O(N squared) with dynamic_cast in the
  inner loop), the undo stack drops old entries in O(1), deleting a selection rebuilds the audio graph
  once instead of once per node, and the autosave writes on a worker thread (no periodic hitch).

### Added
- **Audio In**: choose the input pair (1+2, 3+4...) or a single channel (mono) of a multichannel
  interface; each Audio In node reads independently (two Audio In nodes used to steal each
  other's samples).
- **Slideshow** (Source): the images of a folder with fade / slide / wipe / zoom transitions.
- **CV Recorder** (CV Tools): record any modulator on the beat clock, loops on stop, speed, low / high.
- **Resonator Bank**, **Cycle Shaper**, **Spec Blur** (AudioEffects) with their visualizers.
- **Reverb**: upstream's 16-line FDN redesign (Schroeder diffusion, delay modulation, SSE path) and
  an analog toggle. Existing params and patches unchanged.
- **Audio Filter / EQ**: live spectrum of the signal behind the response curve; EQ: Shift-drag a
  band dot to change its Q.
- **Modulation matrix** (VIEW, Shift+M): every modulation cable in one table, with amount,
  bipolar, range mapper, live value, jump to node and delete; expressions listed below.
- **Explode**: "by" Faces or Loose Parts (each connected part moves as a rigid piece).

### Live audio latency
- **ASIO**: the build downloads Steinberg's ASIO SDK 2.3.4 (GPLv3 since Oct 2025) and enables
  JUCE's ASIO driver; `INFINITE_ENABLE_ASIO=OFF` to leave it out. If the download fails the build
  goes on without ASIO.
- **Settings > Audio > Driver**: WASAPI shared (the old default), ASIO, WASAPI exclusive, WASAPI
  low latency, DirectSound. Buffer sizes down to 32 frames. The panel shows the driver-reported
  round trip in ms.
- **Audio Out "live"**: this output skips the delay compensation that lines several Audio Outs up
  with the slowest branch, so a mic -> plugin -> headphones path is never held back.
- The device list is scanned only for the chosen driver and cached (it used to rescan every driver
  type every frame while Settings was open, which can glitch live audio).
- build-windows.bat clears a stray CMAKE_GENERATOR_PLATFORM from the environment (the
  "will be ignored" warning on every build).

### Fixed
- **Plugins with a mono output** (Auto-Tune and other vocal tools, some synths): only the left
  channel carried the plugin, so panning right in a mixer gave silence. The host now asks for a
  stereo layout when loading and, for a plugin that stays mono, copies its output to both sides.
- **Plugin pitch smear at small buffers** (32/48 frames): the plugin processed a full 64-frame
  buffer on every smaller block and ran ahead of real time. It now processes exactly the block size.
  A mono source feeding a stereo plugin input goes to both input channels.
- **Plugin editors that draw with OpenGL** (Antares Auto-Tune and others) froze the Infinite window
  while audio kept running: the plugin left its own GL context current. The host now saves and
  restores the GL context around every piece of plugin UI code and reclaims it after each event pump.

### Not ported
- Mod Mixer (reverted upstream), Drum Sequencer lane outs (Turbo's MPC + MPC Out covers it),
  Geometry Table, asset decode cache, Performance Mode, update checker, glTF: next rounds.

## 0.39.0-turbo (2026-09-26)

Upstream features ported (items 1-13 of the easy list), adapted to Turbo.

### Added
- **Alpha filters** (Compositing): show alpha, opacity, set alpha (second input's luminance becomes
  the alpha), alpha invert, alpha from luma, alpha levels, premultiply / unpremultiply.
- **Theme**: Forest Green (light) preset; the Theme picker shows panel / text / accent swatches per
  preset. Nord's dim text is readable now.
- **Instance numbering**: nodes that share a title show "#1", "#2"... in the header, MIDI map, output
  window titles and background-preview labels. Display only, patches are unchanged.
- **Comment node**: hover it and start typing (or Enter / double-click) to edit, drag the bottom-right
  corner to resize, font size Small / Normal / Large / Extra Large.
- **Macros**: Macro Slider, Macro Bipolar Knob, Macro Toggle, Macro Trigger, Macro NumBox (min / max /
  step), Macro Radio Selector (2-8), Macro Step Gate (8 steps on the transport). All controls live in the
  node body and can be MIDI-learned there. New "Macros" category (Macro Knob and Macro XY moved in).
- **Keyboard** (Notes): on-screen piano plus laptop typing while hovered (Z row / Q row), octave,
  transpose, velocity, snap to the global scale.
- **Velocity to CV** (Analysis) and **Note Switcher** (Notes, 4 note inputs on a beat / seconds clock
  or manual).
- **Drift** (Modulators): Ornstein-Uhlenbeck random walk with speed, stray, momentum, home, range,
  depth, tempo quantize and smoothing. Upstream's version learns a landscape from your hand moves
  (its MovementStats engine, not in Turbo); this one keeps the same physics around a fixed home.
- **Audio Meter** (AudioUtility): stereo RMS + peak bars, peak hold, max-peak readout, clip latch
  (click to reset); measures even with its output unconnected.
- **Blend / Layer Stack**: Anti-Erase mode (keep A only where B is opaque), index 31, old patches
  unchanged.
- **Transform (geometry)**: pivot x / y / z for rotate and scale.
- **Material**: UV wrap mode (clamp / repeat / mirror) applied to every map.
- **Phaser**: feedback knob (-0.9..0.9, default 0.5) around the allpass cascade.
- **Flanger**: ~7 kHz damping on the feedback path (repeats darken like a BBD).
- **Sampler**: loop xfade (0-250 ms, default 8 ms, equal power) removes the wrap click.
- **Browser**: star favourites in all four modes (Modules gets a FAVOURITES section on top), filter
  (All / Favourites / type) and sort (name / type / folder / favourites first, asc / desc) in Samples,
  Media and Plugins; right-click a row: favourite, Show in Explorer, copy path. Saved in
  `%LOCALAPPDATA%\Infinite\Infinite.browserfavorites`.
- **3D viewports**: numpad-style keys while hovering (1 front, 3 right, 7 top, Ctrl for the opposite
  side, 0 three-quarter view).

### Not ported
- **Moves**: it projects a gesture onto the principal components of your recorded hand movements,
  which needs upstream's MovementLog / MovementStats engine. Left for the prediction-engine port.

## 0.38.1-turbo (2026-09-26)

### Fixed
- **MIDI faders jumping** (e.g. nanoKONTROL on the Super Mixer): the mapping was linear in dB while
  the on-screen fader uses the console taper (unity at 75% of throw). MIDI now follows the widget's
  taper (dB faders and knobs, log frequency knobs), so hardware and screen move together.
- 7-bit controller jitter: a one-step move that reverses direction is ignored, and each mapping glides
  to its target ("smooth" in the mapping editor, default 25 ms, 0 = off). Saved with the patch
  (`midismooth` line, older patches load with the default).

## 0.38.0-turbo (2026-09-26)

### Added
- **MIDI learn for every parameter** that has a CV pin (sliders, knobs, buttons, toggles,
  dropdowns, pads):
  - Ctrl+M or VIEW > MIDI learn mode: the node widgets lock, click a parameter (the one nearest
    the click in that node), move a control - mapped. Stays in learn mode for the next one; Esc
    cancels / exits. Pins show the state: orange ring = learnable, pulsing = waiting, green = mapped
    (bright when MIDI arrives).
  - Right-click a parameter's pin: MIDI learn / learn again / clear, and the mapping editor.
  - A mapping is device + channel + CC or note, so sources never mix. Identical controllers get
    distinct keys ("name", "name #2", ...). "any device" / "any channel" make a patch portable to
    another machine or controller.
  - Per mapping: continuous (knob/fader), momentary (hold) or toggle (press on/off), invert, soft
    takeover (no jumps), and min/max in the parameter's own units - editable any time.
  - VIEW > MIDI map...: connected devices, rescan (controllers plugged in later), every mapping with
    live activity, edit, learn again, show node, delete.
  - Values are written only when new MIDI arrives, so the mouse still works on mapped sliders; a
    wired modulation cable wins over MIDI. Saved with the patch (`midimap` lines).
- **VMPC plays the clip's soundtrack** on a new "audio" output (wire it to Audio Out or a mixer),
  in sync with the picture (the audio is the clock while it plays), with trim, speed/reverse and
  loop modes; "play audio" and "audio volume" controls. Clips without an audio track stay silent.

### Changed
- MIDI inputs with the same name get separate ids ("#2", "#3"); the first keeps the old id, so
  MIDI CC / Trigger nodes saved before keep working.

## 0.37.0-turbo (2026-09-25)

### Menus
- **Categories reorganised** (node browser, search popup, spawn menu), from what each node does:
  Source (Text joined it), **Video** (Video, VMPC, Video In, Syphon/Spout In), Effects (Resynthesize
  joined), **Utility** (Comment, Group, Null, Viewport), **Audio In / Out** (Audio In, Audio Out,
  Audio File), **Audio to Visual** (Audio Texture, Audio Color Ramp, Audio Displacement, Audio
  Ribbon - the old 2-node "Audio"), Audio Mix & Routing, Audio Effects & Plugins, Synths & Samplers,
  Notes & Sequencers, **Prediction**, Modulators (generators), **CV Tools** (Math, Compare, Invert,
  Range to Range, Smoothing, Mod Depth, Null Modulator, CV to Pitch), **Analysis** (Audio/Image
  Analyze, Audio to CV, Note to CV, Palette), **MIDI & OSC** (MIDI CC/Trigger, OSC Receive/Send,
  OSC to CV). Node type names are unchanged; old patches load and re-file their nodes.

### Added
- **Range mapper** per modulation binding (right-click a modulated parameter): in min/max with
  "capture" from the live signal, out min/max in the parameter's units, invert, reset. Saved in the
  patch (trailing tokens of the `mod` line; older builds ignore them).
- **Color markers** for nodes (right-click > Color marker): coloured band + frame, 8 colours,
  applies to the whole selection, saved in the patch (6th token of `flags`).
- **OSC to CV** (MIDI & OSC): 8 channels, each an address + argument index, input range, invert,
  smoothing, and learn (arm, move the control, the next address is assigned). 8 CV outputs.
- **Predictive Notes / Quantize / Velocity / Rhythm**, ported from upstream Infinite
  (NoteModel, NoteTheory): learn from a note chain and play or correct in that style. Their shared
  cross-session learning pools are saved in `%LOCALAPPDATA%\Infinite\prediction`.

### Changed
- **Noise**: five 4D types in the style of TouchDesigner's Noise TOP - Simplex 4D (new default),
  Perlin 4D, Ridged 4D, Turbulence 4D, Billow 4D. The image is a slice of 4D noise and time moves
  along the 4th axis (speed), so it morphs in place instead of scrolling sideways. New controls:
  translate x/y, drift x/y (optional scroll), z, rotate, exponent; octaves, lacunarity, gain, warp,
  contrast, brightness, seed and rgb noise apply too. The original 2D types are unchanged.

### Fixes
- **VST3 plugins now see the host transport** (play/stop, tempo, time signature, bar and beat
  position). Tempo-synced and sequenced effects (Glitch 2, Effectrix, synced delays/LFOs) used to
  read "stopped, no tempo" and pass the audio through dry.
- **OSC**: the listener only bound 127.0.0.1, so controllers on the LAN (phone, tablet, another
  PC) never reached it; it now binds all interfaces. Two OSC nodes on the same port fought over the
  socket (the second got nothing); one shared listener per port now. Bundles and every numeric
  argument (f, i, d, h, T/F) are decoded. OSC Receive's low/high accept any range (0..127, -1..1),
  it can pick an argument, and it shows the raw value and the last address received.

## 0.36.0-turbo (2026-09-25)

### Fixes
- **Fullscreen editor flicker** with a fullscreen output window: the editor window is now one
  pixel taller than the monitor (so it is never promoted to exclusive-style presentation), and
  output windows only re-assert "topmost" when they actually lost it (it was every 0.5 s).
- **Looper recorded late**: takes are shifted by the interface round-trip latency (input +
  output latency reported by the driver + one buffer), overdub too, and a manual offset
  (-100..+300 ms) sits on top. The node shows the compensation in use.
- **Audio Analyze** only accepted an Audio File node. Its single input is now an "audio" pin
  that takes any audio cable (synth, VST, Audio In, mixer, Audio File) and wins over the live
  input; old patches with an Audio File load unchanged.

### New node
- **VMPC** (Source): the MPC for video clips, a VJ clip launcher. 16 pads with one video each, hit
  with the mouse, a CV pin per pad or the note input (base note 36). Per pad: one shot, gate
  (loops while held) or loop toggle, trim in/out, speed (negative = reverse). Output: the clip of
  the last pad hit (transparent when nothing plays, or hold the last frame). Decoders stay open
  per pad, so a hit only seeks. Silent (use a Video node for a soundtrack). Load video / load
  folder go through the non-blocking dialogs.

### Changed
- **Layout**: scale is anchored at the layer centre (x/y place the layer at scale 1); fit / fill
  also work with a custom size.
- **Super Mixer**: input gain knob per channel (+/-24 dB, pre-EQ, CV pin). It is the first knob
  row, so CV mappings made on the Super Mixer's knobs in 0.33-0.35 move by one row.
- **Projection**: transparent outside the warped image (option, on by default) and per-edge edge
  blend with width (0..1 of the image, default 0.30), S-curve and gamma, for overlapping
  projectors. Blend mode **alpha** (default: the edge fades out, for real blending in video
  mapping) or **black** (darkens RGB). **Antialiased outline** (on by default, 0.5-8 px): the warped
  border fades over a few pixels instead of a jagged triangle edge.
- Output windows composite the image's alpha over black, so transparent and alpha-blended edges
  show as a fade on the projector.

## 0.35.0-turbo (2026-09-25)

### Added
- **Canvas preview** (TouchDesigner-style): one node's output is drawn behind the node canvas,
  under nodes and cables. Ctrl+Shift+B toggles it for the selected node; also in the node's
  right-click menu (*Show behind the canvas*) and in the OUTPUT WINDOW section of Output /
  Projection / Layout params. **VIEW > Output behind the canvas**: scaling (fit / real pixels 1:1 /
  fill / stretch), darken veil, node card opacity, grid on/off. Geometry nodes go through Render 3D.
- **Fullscreen UI**: F11 in the editor switches the main window to borderless fullscreen on its
  current monitor (no display mode change) and back, restoring size and maximized state. Output
  windows keep their own F11.
- **VIEW menu**: side panel, fullscreen UI, canvas preview settings.

### Fixes
- **File dialogs no longer pause Infinite.** Open/save dialogs (samples, images, video, models,
  folders, patches, recording destination) ran on the render thread and froze the transport,
  sequencers, video and output windows until closed. They now run on their own thread; the result
  is applied at the start of a later frame (node-bound results are dropped if the node was deleted).
- **Dialogs open in front of the editor**, also in fullscreen: every dialog is owned by the main
  window (it used to open behind a fullscreen editor, reachable only with Alt+Tab). The editor
  ignores input while a dialog is up; playback and outputs keep running.
- Save in the "Unsaved Changes" prompt waits for the Save As dialog before continuing.

### Changed
- The top-bar SEARCH button is now **PANEL**, highlighted while the side panel is open; Ctrl+B
  toggles the panel.

## 0.34.0-turbo (2026-09-25)

### Fixes
- **Buffer underruns.** The render thread was registered with MMCSS "Games" at HIGH priority and
  competed with the JUCE audio thread; the call was removed (the audio thread keeps its own
  priority, FTZ/DAZ stay on).
- **Kontakt / Native Instruments not found.** The VST3 folder scan only accepted `.vst3` bundle
  folders; single-file `.vst3` plugins (how NI installs Kontakt and others) are now found too.
  Run `Rescan plugins` once.
- **MPC**: pad volume, pitch, pan and trim are drag fields, so double-click (or Ctrl+click) lets
  you type the value.

### Added
- **Build version in the app**: window title and FILE menu show the version and build date
  (`INFINITE_TURBO_VERSION` comes from `project(... VERSION)` in CMakeLists.txt).
- **MPC trim in/out** per pad: drag the handles on the waveform or type the start/end; saved in the
  patch; loading a new sample resets it.
- **OUTPUT WINDOW panel** in the parameters of Output, Projection and Layout: open, size in
  pixels (W/H + presets + source size), scaling mode, fullscreen, close. The right-click menu
  still has the same options.
- **Layout** node (Compositing): canvas of exact pixel size with 8 image inputs, each placed at its
  source's real pixel size (or a custom width/height), x/y in canvas pixels, scale, opacity,
  visibility; miniature editor (click to select, drag to move); 1:1 / fit / fill / centre;
  background colour + alpha; CV pins on x, y, scale and opacity of every layer.
- `GLUtil::DrawFullscreenQuad()` for nodes that composite into sub-rectangles.

### Build
- `build-windows.bat` now only compiles the test build (`build\windows-vs2022\Release`);
  `build-windows.bat pack` creates `dist\` and the ZIP.

## 0.33.0-turbo (2026-09-25)

### Fixes
- **VST instruments never received notes.** The topology builder calls the slot-aware
  `AudioNode::SetNoteInbox(slot, ...)` with the node's real note-pin slot, and the default only
  forwarded slot 0. The Plugin node keeps audio at slot 0 and notes at slot 1, so DecentSampler,
  Kontakt & co. got nothing (same for Spectral Synth and Wave Terrain). The default now forwards
  any slot (single-note-pin nodes only get one call); Note Merge still overrides it.
- **VST3 scan and Native Instruments plugins**: a plugin that describes itself and then crashes
  while unloading at exit is now accepted (the scanner also exits without running teardown);
  the scan timeout went from 30 s to 180 s and a timeout no longer blocklists the plugin. The
  blocklist file moved to `PluginVST3Blocklist-v4.txt`, so plugins wrongly blocked by R31a are
  retried. VST2-only plugins (`.dll`) are still not supported.
- Save dialog: default extension `.inf` (same format/extension as upstream Infinite),
  case-insensitive check.

### Output windows
- Background and letterbox bars are black (were grey).
- Node menu > Output window > **Size (pixels)**: exact drawable size (W/H + Apply, presets, source
  size), correct under Windows display scaling.
- Output window > **Scaling**: Fit (letterbox), **Real pixels 1:1** (centred), Fill (crop), Stretch.

### New nodes
- **MPC** (Synths): 16 sample pads, modes one shot / gate (hold) / loop (toggle), note input
  (base note 36), a CV pin per pad, load sample / load folder, per-pad volume/pitch/pan, master.
- **MPC Out** (AudioUtility): one pad's own stereo output from an MPC.
- **Looper** (AudioUtility): REC / PLAY / DUB / CLEAR (all with CV pins), length in bars,
  sub-bar or free, sync to the bar grid, forward / reverse / ping-pong, loop on/off, thru and level.
- **Super Mixer** (AudioUtility): 16 channels, fader, pan, mute, solo, 3-band EQ per channel,
  master fader; every control has a CV pin.
- Engine: `kAudioMaxNodeInputs` 8 -> 16; the PDC scratch buffer is heap-allocated once per audio
  thread instead of a 1-2 MB `thread_local` array reserved for every thread in the process.
- `INode::ResolveAudioTaps()`: called before each topology publish (used by MPC Out).

## 0.32.1-turbo (2026-09-24)

### Performance
- **Link table rebuild** (every frame): owner lookups go through maps built once per frame
  instead of scanning all nodes with a `dynamic_cast` for every connected input (was O(N^2)
  casts per frame; now O(N)).
- **Shader binary cache**: linked programs are stored in
  `%LOCALAPPDATA%\Infinite\shadercache\<driver hash>\` and reloaded with `glProgramBinary`,
  so opening a patch no longer recompiles every node shader. A driver update uses a new folder;
  a rejected binary falls back to compiling. Disable with `INFINITE_SHADER_CACHE=0`.
- **MMCSS**: the render/UI thread registers as a "Games" task (`avrt`), for steadier frame
  pacing under background load.

### Not changed
- Spout receiver: the first `ReceiveTexture()` call only connects/checks the sender and does not
  copy; removing it could break new-frame detection. Left as is.

### Tests on Windows
- New skill `.claude/skills/run-turbo-tests` (PowerShell driver) replaces the macOS hygiene
  driver: build + screenshot + the self-test suite, results in `build\test-results\`
  (`summary.txt`, one log per check, `screenshot.png`). Detects `[CRASH]` (with hex exit code),
  `[HANG]` (timeout kill) and `[EMPTY]`.
- `test-windows.bat` at the repo root: `-Quick`, `-Only A,B`, `-SkipBuild`, `-Config Debug`,
  `-ShotOnly`.
- `known-baseline.md` lists the checks that fail by design.
- The other skills now point to `test-windows.bat` / `/run-turbo-tests`; the macOS `.sh` drivers
  were removed.

## 0.32.0-turbo (2026-09-24)

First Infinite-Turbo cut, on top of the Windows R31a port. Branch `turbo/windows-only`.
Nothing here has been compiled with MSVC yet: the changed sources were syntax-checked with a
MinGW cross compiler; the first `build-windows.bat` run is the real test.

### Windows-only
- Removed every macOS path: `Platform.mm`, `PluginVST3.mm/.h`, `PluginHandleInternal.h`,
  `scanner_main.mm`, `AudioFileWriter.mm`, CoreText `TextNode.cpp`, `external/syphon`,
  the Steinberg `external/vst3sdk` submodule (JUCE hosts VST3 on Windows), plist /
  entitlements / .icns / iconsets, `package.sh` (DMG), `website/`, the GitHub Pages workflow,
  macOS crash reports and the macOS-only skills `ship-infinite` and `plugin-host-hardening`.
- Resolved all `#if defined(__APPLE__)` / `#if defined(_WIN32)` blocks to their Windows branch
  (14 files).
- `CMakeLists.txt` rewritten for MSVC only: `/MP`, Release `/Oi /Ot /Gy /GS-`, `/OPT:REF /OPT:ICF`,
  `/Zc:__cplusplus`, option `INFINITE_ENABLE_AVX2` (ON), option `INFINITE_ENABLE_LTCG` (OFF).

### Name
- Executable `Infinite-Turbo.exe`, window title `... - Infinite-Turbo`, version resource 0.32.0.
- Package `dist\Infinite-Turbo-Windows-x64(.zip)`.
- User data stays in `%LOCALAPPDATA%\Infinite` so settings, plugin index, blocklist and models
  from R31a keep working.

### Performance and correctness fixes
- **Audio: denormal protection was OFF on Windows.** `AudioEngine::Process` guarded FTZ/DAZ with
  `__x86_64__`, which MSVC never defines. Reverb/filter/delay tails decaying into denormals could
  cost 10-100x CPU. Now always set.
- **Video decoder** (`PlatformWindows.cpp`): read-ahead cache in the playback direction, `grab()`
  for small forward jumps instead of keyframe seeks, reverse playback decoded in batches (one seek
  per batch instead of one per frame), D3D11VA/DXVA hardware decode requested (disable with
  env `INFINITE_VIDEO_HWACCEL=0`), frame buffers swapped/recycled instead of copied, BGR->RGBA
  CPU conversion removed.
- **Video / camera upload**: BGR uploaded directly (GPU swizzle), texture storage allocated only on
  size change (`glTexSubImage2D` otherwise). Video node now reports a real `TextureRevision`, so a
  paused clip no longer forces every downstream filter to re-render.
- **Camera**: requests MJPG (30 fps at 720p/1080p on most USB webcams), 1-frame capture buffer,
  mirror + flip in a single pass.
- **Remove Background**: no more synchronous full-resolution `glReadPixels`. Source is copied on the
  GPU (paired with its mask), a downscaled copy (max 512 px) is read back through PBO + fence,
  and the worker always processes the newest frame (the old queue could process stale frames).
  GPU copies are recycled, bounded even when inference fails.
- **Uniform-location cache**: all ~340 `glGetUniformLocation` call sites are transparently served
  from a per-program cache (`OpenGLHeaders.h` / `GLUtil.cpp`); re-link and delete invalidate it.
- **Frame limiter**: high-resolution waitable timer (`Platform::PreciseSleep`) instead of
  `sleep_for` + spin; frees most of a CPU core at capped FPS.
- **VSync with output windows**: exactly one VSync wait per frame. With projector windows open the
  projector owns VSync (tear-free projection paced by its display); otherwise the editor does.
- **Node/link lookup**: `FindNodeByIndex` / `FindLink` use validated position hints (O(1) hits).
- **OpenGL context**: requests 4.6 core (falls back 4.5 > 4.3 > 3.3 > 3.2); logs renderer and
  version to `Infinite.log`. Shaders unchanged (`#version 150`).
- File names in titles/menus: `find_last_of('/')` -> `find_last_of("/\\")` (12 sites) so Windows
  paths show just the file name.

### Setup scripts
- `build-windows.bat`: args `debug`, `fresh`, `nopack`; configures only when needed (no forced
  `--fresh` every build), all cores (`/m` + `/MP`), refuses to build while the app is running,
  no submodule step.
- `install-dependencies.bat`: vcpkg binary cache in `%LOCALAPPDATA%\InfiniteBuild\vcpkg-binary-cache`
  (reinstall/second checkout reuses compiled OpenCV etc.), no submodule step.
- `run-windows.bat`: new exe name, Debug fallback, above-normal priority, pauses only on error.
- `install-runtime.bat`: reuses models bundled in the ZIP, one checksum routine.
- `diagnose-windows.bat`: new names, reads any blocklist version.
- `scripts/package-*.ps1` and CI workflow renamed and cleaned of macOS files.
