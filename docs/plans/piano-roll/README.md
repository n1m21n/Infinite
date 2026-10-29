# Piano Roll: docked MIDI clip editor (spec for sign-off, 2026-10-30)

Status: **spec only. No code until Naman signs off.** It adds a new panel, a new node and a new saved record type, so the owner decides.
Lane 3 (Major): a new clip type in the Arrange model, a new note-source node, and a new docked panel.
Builds on the Arrange model (`src/arrange/ArrangeModel.h`), Session slots (`ArrangeSession*.cpp`), the Sample Studio panel
(`SampleStudioPanel.cpp`, design language and dock), and the note path (`src/audio/NoteEvent.h`, `MidiFile.h`).

## 1. What Naman asked for

| Ask | Where in this spec |
|---|---|
| A docked panel (top or bottom), in the same design language as Sample Studio, Timeline and Session | §3, §9 |
| Professional, DAW-standard MIDI clips | §4 (with the extras he didn't list marked **Pro**) |
| Live link to a MIDI node on the canvas; that node drives synths and instruments | §3, §6 |
| Many pages, like the performance panel's pages, each linked to its own MIDI node | §3.2 |
| No separate "Enable audio" chip, because it is only MIDI | §3.3 |
| Clips handled the same way as in Timeline and Session | §3.1, §4.1 |
| Lay out, cut, slice, copy, paste, rearrange clips | §4.1 |
| Per-note velocity, gain, pan, pitch and other note expression | §4.4 |
| Toolbar icons for draw, delete, zoom; shortcuts; two-finger pinch | §4.2, §4.7 |
| Snap to the scale; highlight the global scale | §4.5 |
| Clip colours | §4.1 |
| Select several clips (or notes) and change their settings together | §4.1, §4.6 |
| Generate notes with the Generate Samples note model: chord progressions, arps and other types, with generation settings (2026-10-30) | §12 |

The bar: Ableton Live 12's MIDI editor, Bitwig's note expressions and per-note operators, Logic's piano roll, FL Studio's tools,
Cubase's key editor. Sources are the vendors' public manuals and shipped UI. No competitor source code is read (AGENTS invariant 1).
`docs/plans/re-survey/capability-diff.md` row P6/F already lists "note entry in a clip" as a gap for all three DAWs we measured.

## 2. What exists today (checked in code)

| Piece | State | Use here |
|---|---|---|
| `Arrange::Clip`, `Lane`, `SessionSlot` | Ticks at kPPQ 960, with ops `PlaceOverwrite`, `MoveClips`, `TrimEdge`, `Split`, `DuplicateBlock`, `Delete`, `SetEnabled`, `Group`, all headless-tested. Lanes are only `kLaneVideo` or `kLaneAudio`. | MIDI clips become a third lane type on the **same** model and ops, so cutting, moving and pasting behave the same way by construction. |
| Session slots | `Lane::slots`, launch, loop, follow actions | A MIDI lane gets slots for free. |
| `NoteEvent` | note, velocity, on/off, `frameOffset`, `voiceId`, `bendSemitones`, `pressure`, `slide` | Carries velocity, release, pitch bend, pressure and timbre today. Gain and pan per note need two new fields (§4.4). |
| `MidiFileNode` / `MidiFile::Song` | Plays a .mid on the beat clock, sample-offset scheduling | It is the playback template. Its parser is the .mid import. There is no MIDI file writer yet. |
| `Transport::Key()` / `Scale()` | Global key and scale; `GlobalScaleToggle` glyph (NoteSnap) | Scale highlighting and snapping. |
| `DrawMidiKeyboard` (`NoteBodies.cpp:347`) | The shared piano strip | The roll's key column. |
| Perf pages (`PerfPanel.cpp`) | Page tabs, reorder, rename | The page tab grammar. |
| Sample Studio panel | Dock bottom/top, `IconChip` toolbar, pinch zoom (`TrackpadPinchThisFrame`), right-click Dock Position / Close | The same chrome, copied. |
| A piano roll anywhere | **None.** Timeline and Session have no MIDI tracks. | — |
| Generate Samples note engine (`SampleStyles::Compile`, `GenerateSamplesNotes.inc`) | It writes notes as a private patch of note nodes (Chorder, Arpeggiator, Note Sequencer, Drum Sequencer) that an `OfflineGraph` renders. It draws on 34 style cards, the note bank, bass roles, keys play modes, genre grooves and the melody critic. The notes never leave the graph as notes. | The note generator for §12: the same engine, with its notes captured instead of only heard. |

## 3. Shape

### 3.1 One source of truth: a page is a MIDI track

```
  Piano Roll panel                  Arrange model (in the patch)              canvas
 ┌──────────────────────┐        ┌───────────────────────────────┐       ┌────────────────┐
 │ [Bass][Lead][Chords]+│ ─────► │ Lane kLaneMidi "Bass"         │       │  Piano Roll    │
 │  page = one MIDI lane│ edits  │   noteNodeUid ───────────────────────► │  "Bass"   (o)──┼─► notes ─► Wavetable
 │  clips + notes       │        │   clips[] (timeline)          │ plays │  notes in (o)  │
 └──────────────────────┘        │   slots[] (session)           │       └────────────────┘
           ▲                     └───────────────────────────────┘
           │ same lane, same clips
  Timeline MIDI track · Session MIDI column
```

- **A page is a MIDI lane.** Its clips live in `Arrange::Model`, next to the Timeline's video and audio lanes. Save, load, patch undo,
  autosave and index remapping come with the model.
- **The linked canvas node plays it.** A new **Piano Roll** node (a note source) is the lane's output. Its notes cable goes to any synth,
  sampler, instrument plug-in or MIDI Out, like every other note source.
- **The same clip everywhere.** A MIDI clip is an `Arrange::Clip` with notes. The piano roll page, the Timeline MIDI track and the Session
  MIDI column all show the same lane, so an edit in one shows in the others. There is one data model and one set of ops.

### 3.2 Pages and links

| Action | Result |
|---|---|
| `+` tab | Adds a MIDI lane, and spawns a Piano Roll node next to the canvas view's centre, linked and named after the page. One patch undo step. |
| Link chip on a page | A menu of unlinked Piano Roll nodes, plus "Pick on canvas" (click-to-assign, as Session already does). |
| One node, one page | A node already linked elsewhere asks: "Move the link here?" Two lanes never drive one node. |
| Delete the node | The page goes **offline**. Clips stay, drawn hatched, with "Link a Piano Roll node". This is the `ClearSource` rule the Timeline already follows. |
| Delete the page | Removes the lane and its clips. The node stays on the canvas, unlinked and silent. |
| Select the node on the canvas | The panel jumps to its page (if open). Double-click the node, or its body's Open chip, opens the panel on that page. |
| Tabs | Rename (double-click), reorder (drag), colour, close. The tab order is the MIDI lanes' order, so the Timeline shows them in the same order. |
| `[` / `]` | Previous / next page. |
| MIDI File node | Its menu gains "Open in Piano Roll": it imports the song into a new page and swaps the node for a linked Piano Roll node, keeping the cables. One undo step. |

### 3.3 Playback

- MIDI lanes play whenever the transport plays. **There is no "Enable MIDI" chip**: notes are not audio, so nothing needs to take the
  output. The Timeline's and Session's audio modes do not gate MIDI lanes.
- A Session slot launched on a MIDI lane plays instead of that lane's timeline clips. This is the existing Session rule, unchanged.
- The Timeline's loop range (`Settings::loop`) is the piano roll's loop too. There is one loop, as there is one playhead.
- Stop, seek, loop wrap and clip edits under the playhead send note-offs for every voice the node started (matched by `voiceId`), so
  nothing is left stuck on.

## 4. Features

Columns: **Asked** means Naman asked for it. **Pro** means it is a professional-standard extra. **Block** is the build block in §10.

### 4.1 Clips (the clip strip at the top of a page)

| Feature | Detail | Asked / Pro | Block |
|---|---|---|---|
| Draw a clip | Draw tool on the clip strip, or double-click empty space: a clip one bar long (or to the grid). | Asked | B |
| Move, trim, split | The Timeline's gestures and ops: drag body, drag edges, blade (`B`) or `Cmd+E` at the playhead, snap to the grid, clip edges and playhead. | Asked | B |
| Copy, cut, paste, duplicate | `Cmd+C/X/V` paste at the playhead; `Cmd+D` duplicates the block after itself; Alt-drag copies. | Asked | B |
| Rearrange | Drag clips; moved clips overwrite what they land on (`MoveClips`). Drag a clip to another page's tab to move it to that lane. | Asked | B |
| Loop inside a clip | Loop on/off, loop start and length. A looped clip repeats its notes to fill its box; repeats draw as ghosts. Start offset. | Pro | A |
| Colour | 12-colour role-token palette (shared with Sample Studio slices), auto by page. Notes take their clip's colour. | Asked | B |
| Name, mute, group | `Cmd+R`, `0`, `Cmd+G` / `Cmd+Shift+G`, as in the Timeline. | Pro | B |
| Clip settings | gain (velocity offset in dB terms), transpose, velocity scale, swing, chance scale, loop, colour, name. | Asked | B |
| Several clips together | Shift/Cmd-click or marquee. The inspector edits all of them; knobs move relatively and show "mixed" values. | Asked | B |
| Consolidate | Merge the selected clips into one clip. | Pro | B |
| Session slots | Clicking a MIDI slot in Session opens it on its page, with a "Session · Scene 3" chip in place of the clip strip. | Pro | C |

### 4.2 Tools and view (toolbar icons, each with a key)

| Tool / control | Key | Detail | Asked / Pro | Block |
|---|---|---|---|---|
| Select | `Esc` | Click selects; marquee; Shift adds; Cmd toggles; double-click a note deletes it. | Asked | B |
| Draw | `D` | Click adds a note at the last used length; drag after the click sets its length; drag across keys paints a run. | Asked | B |
| Erase | `E` | Click or drag to delete. | Asked | B |
| Blade | `B` | Cuts notes (and clips on the strip) at the mouse. Same key as the Timeline. | Asked | B |
| Mute | `0` | Mutes the selected notes or clips; they draw hatched. | Pro | B |
| Zoom in / out / fit | `=` `-` `F`, `Z` to the selection | Icons in the toolbar. | Asked | B |
| Horizontal zoom | pinch, `Cmd+wheel` | Around the mouse, as in the Timeline and Sample Studio. | Asked | B |
| Vertical zoom | `Alt+pinch`, `Alt+Cmd+wheel` | Changes the key height. A rail on the key column drags it too. | Pro | B |
| Scroll | two-finger swipe, wheel | Sideways and up/down. | Asked | B |
| Follow playhead | toggle icon | The view pages along while playing. | Pro | B |
| Grid | dropdown: off, bar, 1/2 .. 1/64, triplets; "adaptive" follows zoom | Same `SnapGridTicks` as the Timeline. Cmd while dragging places freely. | Pro | B |
| Audition | toggle icon | Notes sound when drawn, clicked or moved, through the linked node. The key column plays notes on click. | Pro | B |
| Fold | toggle icon | Show only keys that hold notes, or only scale keys (§4.5). | Pro | B |
| Generate | chip + type dropdown, `G` | Generates notes into the selection or a new clip (§12). Settings icon opens the card. | Asked | B |

### 4.3 Note editing

| Feature | Detail | Asked / Pro | Block |
|---|---|---|---|
| Move, resize, copy | Drag; edges resize; Alt-drag copies; Shift locks the axis. | Asked | B |
| Transpose | Up/Down a semitone (a scale degree when scale snap is on); Shift+Up/Down an octave. | Pro | B |
| Nudge | Left/Right one grid step, as the Timeline nudges clips. | Pro | B |
| Split, join | Blade or `Cmd+E` splits at the mouse; `Cmd+J` joins touching notes of one pitch. | Asked | B |
| Quantize | `Q` to the grid; `Shift+Q` opens strength, swing, start / end / both. Uses the `rhythmic-quantization-standard` grid. | Pro | B |
| Functions menu | Legato, humanize (timing, velocity), reverse, invert, double/half speed, strum, chop to grid, velocity ramp, fit to scale. | Pro | B |
| Select by | All of a pitch; every Nth; by velocity range; invert the selection. | Pro | C |
| Ghost notes | Faintly show another page's notes for writing parts against each other. | Pro | C |
| Record | Arm on the page: the node's **notes in** records into a new clip at the playhead while playing. Overdub or replace. Retro capture keeps the last 8 bars played while not armed. | Pro | C |
| Import / export .mid | Drop a .mid on a page or the clip strip; drag a clip out to Finder as .mid. | Pro | A (import) / C (export) |

### 4.4 Per-note data and expression

Every note carries all of these. The expression lane under the roll edits one at a time (tabs), and the note inspector edits all of them.

| Field | Range | Reaches the synth as | Asked / Pro | Block |
|---|---|---|---|---|
| pitch | 0..127 | `NoteEvent::note` | Asked | A |
| start, length | ticks | timing | Asked | A |
| velocity | 1..127 | `velocity` | Asked | A |
| release velocity | 0..127 | `velocity` on the note-off | Pro | A |
| chance | 0..100 % | decided at play, seeded by (note id, loop pass), so renders repeat | Pro | A |
| mute | on/off | the note is skipped | Pro | A |
| pitch bend | a curve, ±48 st | `bendSemitones` updates (`bendUpdate`) | Asked ("pitch") | A |
| pressure | a curve, 0..1 | `pressure`, `hasExpression` | Asked ("other expression") | A |
| timbre (slide) | a curve, 0..1 | `slide` (CC74 in MPE terms) | Asked ("other expression") | A |
| gain | -inf..+12 dB | **new** `NoteEvent::gainDb` | Asked | C |
| pan | L100..R100 | **new** `NoteEvent::pan` | Asked | C |

- **Gain and pan wait for block C** because no voice reads them yet. They land together with support in the three voices that already
  read MPE expression (Oscillator, Wavetable, Sampler), so no control ships that the synth ignores (`param-truth-audit`). The node
  info says which voices hear them.
- **Curves:** pitch, pressure and timbre are breakpoint curves inside the note. Each point is (tick offset, value), and the curve is
  drawn over the note when its tab is open.
- **Voices that ignore expression** still play the note, as MPE input already does today.

### 4.5 Scale

| Feature | Detail | Asked / Pro | Block |
|---|---|---|---|
| Scale highlight | Rows in the global key (`Transport::Key()`, `Scale()`) are tinted; the root's rows are stronger; out-of-scale rows are darker. It updates live when the key changes. | Asked | B |
| Scale snap | The NoteSnap glyph (`GlobalScaleToggle`). While on, drawing, dragging and transposing land only on scale degrees. Off by default, per page. | Asked | B |
| Out-of-scale notes | With snap on, existing notes outside the scale get a small mark, and "Fit to scale" moves them. | Pro | B |
| Fold to scale | Hides the rows that are not in the scale (§4.2). | Pro | B |
| Node output snap | The Piano Roll node's own `useGlobalScale`, like the other note nodes, snaps at play time without changing the notes. | Pro | A |

### 4.6 Inspector (right side, Sample Studio's grammar)

- **Nothing selected:** the page (node link, colour, scale snap, grid, audition).
- **Clip(s) selected:** §4.1's clip settings.
- **Note(s) selected:** pitch, start, length, velocity, release, chance, mute. Below them, the expression values at the note's start.
  With several notes, fields show "mixed" and drag relatively; typed values set every note.

It uses the same `SectionCard` and `FieldWell` parts as the Sample Studio inspector. Dropdown faces have no chevrons.

### 4.7 Keyboard (only while the panel has focus, the Timeline's rule)

| Key | Action |
|---|---|
| `Shift+R` | toggle the panel (global; free today) |
| `Esc` / `D` / `E` / `B` | select / draw / erase / blade |
| `0` | mute or unmute the selection |
| `Cmd+C` / `X` / `V`, `Cmd+D`, Delete | copy, cut, paste, duplicate, delete (notes, or clips on the strip) |
| `Cmd+E` / `Cmd+J` | split / join |
| `Cmd+A` | select all in the clip |
| `Cmd+R` / `Cmd+G` | rename / group clips |
| `G` | Generate (§12); `Shift+G` stays the global panel key |
| `Q` / `Shift+Q` | quantize / quantize settings |
| Up/Down, `Shift+Up/Down` | transpose a semitone (scale degree) / an octave |
| Left/Right | nudge one grid step; with nothing selected, step the playhead |
| `=` `-` `F` `Z` | zoom in / out / fit / to the selection |
| `[` / `]` | previous / next page |
| `Cmd+L`, `I` / `O` | loop on/off, loop in / out (shared with the Timeline) |
| `Cmd+Z` | patch undo: every edit is one patch undo step per gesture, like the Timeline |

`shortcuts-sweep` checks each key against the global map and the help table. Keys a canvas also uses (`D`, `E`, `B`, `0`) only act while
the panel has focus, so canvas keys are untouched.

## 5. Data model (`src/arrange/ArrangeModel.h`, pure data, headless-testable)

```cpp
enum LaneType { kLaneVideo = 0, kLaneAudio = 1, kLaneMidi = 2 };

struct ExprPoint { Tick at; float v; };      // at = ticks from the note's start

struct Note {
   uint32_t id;                              // stable within its clip, never reused
   Tick  start, length;                      // start relative to the clip's content start; length > 0
   uint8_t pitch;                            // 0..127
   uint8_t vel = 100, relVel = 64;
   float chance = 1.0f;                      // 0..1
   bool  mute = false;
   float gainDb = 0.0f, pan = 0.0f;          // block C
   std::vector<ExprPoint> bend, pressure, timbre;   // empty = flat
};

// Added to Clip (MIDI lanes only; defaults are no-ops on audio and video clips):
std::vector<Note> notes;                     // sorted by (start, pitch)
uint32_t nextNoteId = 1;
bool midiLoop = false; Tick loopStart = 0, loopLength = 0, contentOffset = 0;
int  transpose = 0; float velScale = 1.0f, swing = 0.0f;

// Added to Lane:
uint64_t noteNodeUid = 0;                    // the Piano Roll node that plays it; 0 = offline
bool scaleSnap = false;
```

New ops, each returning true when the model changed and bumping `revision` once:

```cpp
bool AddNotes(Model&, uint64_t clipId, std::vector<Note>, std::vector<uint32_t>* outIds = nullptr);
bool EditNotes(Model&, uint64_t clipId, const std::vector<Note>&);   // by id
bool DeleteNotes(Model&, uint64_t clipId, const std::vector<uint32_t>&);
bool MoveNotes(Model&, uint64_t clipId, const std::vector<uint32_t>&, Tick dt, int dPitch, bool scaleSnap, int key, int scale);
bool SplitNotes(Model&, uint64_t clipId, const std::vector<uint32_t>&, Tick at);
bool JoinNotes(Model&, uint64_t clipId, const std::vector<uint32_t>&);
bool QuantizeNotes(Model&, uint64_t clipId, const std::vector<uint32_t>&, const QuantizeSpec&);
bool LinkLane(Model&, uint64_t laneId, uint64_t nodeUid);            // unlinks any other lane holding nodeUid
```

- **Existing clip ops learn about notes:** `Split` divides a MIDI clip's notes at the cut, and a note crossing the cut is shortened
  (the Ableton and Bitwig behaviour). `DuplicateBlock`, `DuplicateLane` and paste copy notes and keep their ids within the new clip.
- **`Validate`** adds: MIDI clips only on MIDI lanes, notes sorted, every length > 0, pitch in 0..127, and at most one lane per
  `noteNodeUid`.
- **The cap is 16,384 notes per lane**, which fits the audio snapshot's fixed buffer (§7). Adding past the cap is refused with a toast.

## 6. The Piano Roll node

| | |
|---|---|
| Category | Notes, in the note-source colour, beside MIDI File and Keyboard |
| Inputs | `notes` (optional): recording source, and passed through to the output (thru on/off) |
| Output | `notes` |
| Params | transpose, velocity scale, `useGlobalScale` (output snap), thru, channel stamp (for MIDI Out) |
| Body | A mini roll of the clip under the playhead (clip colour, playhead line), the page name, and an Open chip. It uses the viewer frame from `element-notes.md`. |
| Bypass | It is a source with one optional input, so bypass passes the notes input through (the bypass rule allows that for one input). |
| Copy/paste or duplicate | The copy comes out **unlinked**. A toast offers "Duplicate the page too", which copies the lane linked to the new node. |
| Help | A row in each of the four help tables, plus the node reference. |

## 7. Engine (audio thread, no allocation, no locks)

```
main thread: lane edit ─► build NoteSnapshot (flat, sorted events in ticks, loops unrolled per clip box)
            ─► atomic<NoteSnapshot*>::exchange ─► the old one is retired a generation later (AudioEngine::SetTopology's precedent)
audio thread, per block: Transport beat range [b0, b1) ─► binary search the snapshot ─► NoteEvents with frameOffset
            ─► chance(note id, pass) ─► expression updates as bendUpdate events at most every 64 frames ─► notes out
```

- **Which clips play:** Timeline clips by default, or the launched Session slot. The main thread publishes the active source per lane as
  an atomic, the same way Session already does for audio.
- **Edits while playing:** the new snapshot is diffed against the notes held. A held note that was deleted, or shortened past the
  playhead, gets its note-off at once.
- **Offline render:** the same code path runs on `ProcessOffline`'s beat clock, so a render gives the same bytes each time. A
  `determinism-check` row is added.
- **Timing** is sample-accurate from the beat clock, as `MidiFileNode` schedules today.

## 8. Save and load

- **On disk:** the MIDI lane is a normal `stream` record with type 2. Its clips are normal `clip` lines. A `streamnote` line holds the
  lane's link and scale snap; a `clipmidi` line per clip holds its content window and clip settings. Notes are child lines
  `clipnote <stream> <clipId> id=1 s=0 l=480 p=60 ...` on the `perf`/`perftarget` parent+child template, with fields written by name
  and defaults left out. Expression curves go on the same line (`bend=0:0,240:2.0`). The tag is `clipnote`, not `note`: `note` is
  already the note-cable line, and an older reader would load every note as a cable.
- **Older builds** ignore the unknown tags (`Patch::Read` has no final `else`), so a patch with MIDI lanes still opens there. Those
  builds read stream type 2 as a video lane, so the lane shows up empty (its clips have no source).
- **PatchJson** mirrors it for `get_graph`.
- **Checks:** save → load → save gives the same bytes (headless). `data-accuracy-sweep` covers copy/paste and duplicate of the node.

## 9. UI layout (bottom or top dock, as the Timeline and Sample Studio)

```
+---------------------------------------------------------------------------------------------------------------------+
| [Bass ●][Lead ●][Chords ●][+]                                                       link: Piano Roll "Bass"  (×)    |
| (sel)(draw)(erase)(blade)(mute) | grid [1/16] (snap) (scale) (fold) (audition) (follow) | (Q) fx | [Generate][Chords v] (-)(+)(fit) |
+----+----------------------------------------------------------------------------------------------+-----------------+
|    | 1        2        3        4        5        6          ruler, loop range                    | NOTE (3 sel.)   |
|    | [■■ Verse A ■■■■■■■■■][■■ Verse B ■■■■■■■■]      [■■ Hook ■■■]   clip strip               | pitch   mixed   |
|    |  takes: (1)(2)(3★)(4)                                                                        |                 |
|----+----------------------------------------------------------------------------------------------| start   2.1.3   |
| C4 | ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  rows: in-scale tinted, root stronger, out-of-scale dark | length  1/8     |
| B3 |     ▆▆▆▆▆       ▆▆▆                                                                         | vel     96      |
| A3 | ▆▆▆▆      ▆▆▆▆▆▆▆      notes in clip colour, velocity as fill depth, selected outlined         | release 64      |
| G3 |                           ▆▆▆▆▆▆▆▆                                                          | chance  100 %   |
|keys|                       │ playhead                                                            | mute    off     |
+----+----------------------------------------------------------------------------------------------+ EXPRESSION      |
| [Velocity][Release][Chance][Pitch][Pressure][Timbre]     ▌ ▌▌ ▌  ▌▌▌  lollipops / curves          | bend    0 st    |
+----+----------------------------------------------------------------------------------------------+-----------------+
```

- **Chrome:** Sample Studio's. The `IconChip` toolbar, the same tile heights, a right-click menu with Dock Position and Close Piano Roll,
  a top-bar toggle, and View > Piano Roll. There is no audio-mode chip.
- **Key column:** `DrawMidiKeyboard`, anchored on C, with octave labels. Clicking a key plays it when audition is on.
- **Notes:** the clip colour, with velocity as fill depth. A muted note is hatched. Clips that are not being edited draw at low alpha.
- **Expression lane:** resizable. Velocity, release and chance draw as lollipops; pitch, pressure and timbre draw as curves. The draw
  tool paints and draws lines (ramps).
- **Empty page:** "Draw a clip, drop a .mid, or record".
- **Design system:** colours come from tokens, and the 12-colour clip palette is a role-token list checked in both themes. New icons
  (draw, erase, fold, follow, audition) come from the glyph set or are drawn, then checked with `lint_icons.py`. Motion stays at
  200 ms or less. `inventory.py`'s ratchet must not grow.

## 10. Build blocks (three branches, a commit per step)

Every build goes through `scripts/ibuild.py`, one heavy job at a time.

| Block | Branch | What lands | Checked by |
|---|---|---|---|
| **A: model, node, engine** | `feature/piano-roll-a-model` | `kLaneMidi`, `Note`, the note ops, `Split`/`Duplicate` with notes, `Validate`; save/load and PatchJson; the Piano Roll node with playback, loops, chance, bend/pressure/timbre; .mid import (`MidiFile::Song`); "Open in Piano Roll" on MIDI File; **`NoteGen` core (§12.5): note capture from `OfflineGraph`, the `Recipe` note fields, types 1-8 of §12.2, form, the drum map** | headless model tests; save → load → save bytes; `INFINITE_PIANOROLLTEST` (a known clip renders the expected NoteEvents at the expected frames, at two tempos); `audio-node-sweep`, `audio-pipeline-sweep`, `determinism-check`; **§12.6's checks** |
| **B: the panel** | `feature/piano-roll-b-panel` | The dock, pages and links, the clip strip, the roll, tools, zoom and pinch, grid, scale highlight and snap, fold, audition, follow, the inspector with multi-select, the expression lane, quantize and the functions menu, keys, help rows; **the Generate chip, the settings card, takes, keep/lock, regenerate a selection, drum pages** | `panels-sweep`; `shortcuts-sweep`; `node-ui-sweep`; screenshots in light and dark at 3 zoom levels; driven in the app |
| **C: everywhere else** | `feature/piano-roll-c-integration` | MIDI tracks in the Timeline (note thumbnails) and MIDI columns in Session; per-note gain and pan (`NoteEvent` + the three voices); recording and retro capture; .mid export (a new small writer); ghost notes; select-by; **generation types 9-12 (follow another page's harmony, counter-melody, continue, vary), Generate all parts, Sample Studio "Notes to Piano Roll"** | `param-truth-audit` on gain and pan; `av-sync-sweep` with a MIDI-driven synth in a render; `verify-gate` on the branch |

Block A is first because everything reads the model. Block B gives a usable piano roll on its own, through the canvas node. Block C
makes the lanes visible in Timeline and Session.

## 11. Decided here

1. **Clips live in the Arrange model, not in the node.** This keeps them consistent with Timeline and Session, gives one undo and one
   save path, and means Session launching works with no extra code. The cost: a copied node comes out unlinked (§6).
2. **Only Piano Roll nodes link.** Arpeggiator, MIDI File and the other note nodes transform or generate notes, but they don't hold
   clips. MIDI File converts in one step instead.
3. **Patch undo, not a private undo.** The notes are patch data, as the Timeline's clips are. The Sample Studio's private undo stays its
   own, because the Studio's edit is not patch data until you convert it.
4. **`B` is the blade, as in the Timeline.** The Sample Studio uses `C` for its scissors, and moving it to `B` is a one-line follow-up for
   consistency.
5. **4/4 only**, as the Arrange model is today (`kMaxTick`). A meter map is its own project.
6. **CC automation lanes** (mod wheel, any CC, as modulator outputs on the node) are left for a later round. Note expression covers the
   per-note case Naman asked for.
7. **Generation uses the Generate Samples engine, not a second note model.** A page's Generate and a Sample Studio take with the same
   recipe and seed play the same notes. One engine, one critic, one set of model gaps to close (§12.7).

## 12. Generate notes (the Sample Studio's note engine, as MIDI)

### 12.1 How it works
```
 settings card ──► Recipe (style, part, seed, bars, key, scale, dots + note fields §12.3)
                        │
                        ▼
            SampleStyles::Compile(recipe)        same call the Generate Samples node makes
                        │  private patch: Chorder / Arpeggiator / Note Sequencer /
                        │  Drum Sequencer / Humanizer, bass role, keys play mode
                        ▼
            OfflineGraph::RenderNotes(...)       NEW: runs the note nodes only, no synths,
                        │                        records each NoteEvent at the voice input
                        ▼                        (tick = frame -> beats at the recipe bpm)
            critic gate (CriticJudgeMelody, the role critic's note checks; up to 3 redraws)
                        │
                        ▼
            notes -> a new clip at the playhead (or the selection), one undo step, a take
```
- **Notes only, no audio render.** `RenderNotes` steps the private graph's note sources over `bars` and stops; a 4-bar take is a few ms,
  so Generate is instant and runs on the main thread. No worker, no progress bar.
- **Same seed, same notes.** The recipe and seed are saved on the clip (`gen` line, §12.6), so regenerating "this one" is exact and
  "next" is `seed + 1`.
- **The sound stays the page's.** Generation writes notes; whatever the linked Piano Roll node drives keeps playing them. "Generate all
  parts" (§12.2, #12) is the one action that also adds instruments.

### 12.2 Types (the Generate dropdown; each one maps to a part and note nodes the engine already has)

| # | Type | What comes out | Engine path | Block |
|---|---|---|---|---|
| 1 | Chord progression | Block chords, one per bar or per `rate` | Chorder progression mode, bank `NoteProg`, card `chords.size/beats` | A |
| 2 | Chord rhythm (comp) | The progression re-struck on a genre rhythm (house stabs, offbeat skank, piano comp) | Keys `kComp` + the card's groove skeleton | A |
| 3 | Pads | Long held chords, voice-led, ties across bars | Keys `kHeld`, Pads part | A |
| 4 | Broken chords / strum | Chord tones spread over time (Alberti, strums, rolled) | Chorder `strumMs` + Arpeggiator `kAsPlayed`/`kJoin` | A |
| 5 | Arpeggio | The progression through an arp (15 modes: up, down, up-down, converge, stairs, random ...) | Arpeggiator over Chorder, Keys `kArp` | A |
| 6 | Bass | A bass line under the progression; role picks the feel: long, rolling, melodic, funk, wobble, trap 808, acid | Bass part, `BankLine(bass)`, bass roles | A |
| 7 | Melody / hook | A lead line in the key, contour and motif repeat, judged by the melody critic | Motif (`motifDensity/Octave/Gate/Range`), `BankLine`, `CriticJudgeMelody` | A |
| 8 | Drums | GM drum notes (kick 36, snare 38, clap 39, closed hat 42, open hat 46, toms 45/50, bell 56) on the style's grooves, ghosts and fills | Drum Sequencer, `kGrooveSkeletons/Hats/Ghosts` | A |
| 9 | Ostinato / riff | A short repeating figure (1 or 2 beats) in the key, the Plucks part's line | Note Sequencer (16 steps), Plucks | C |
| 10 | Counter-melody | A second line that moves when the melody holds, in thirds/sixths or contrary motion against a chosen page | Motif seeded from the other page's notes | C |
| 11 | Continue / vary | Extends or varies the selected notes in their own style | `NoteModel` (the PPM model behind Predictive Notes): `Build(selection)`, `Player::Next` | C |
| 12 | Generate all parts | One page per part (drums, bass, chords, lead) plus the style's instruments on the canvas, linked | `Part::Full`, one compile, split by voice | C |

Drum pages: a page whose clip holds type-8 notes (or any page, from its menu) can show **drum rows** instead of keys: named rows
(Kick, Snare, Hat ...) in GM order, only the used rows, notes as fixed-length hits. Same data, a different key column.

### 12.3 Settings (the Generate card, Sample Studio's inspector grammar)

Shared (every type):

| Setting | Range / values | Default | Maps to |
|---|---|---|---|
| Type | §12.2 | Chord progression | `Recipe.part` + the note fields |
| Style | the 34 style cards, grouped by family | the last used | `Recipe.style` |
| Bars | 1, 2, 4, 8 (16 and 32 with Form) | 4 | `Recipe.bars` |
| Form | none, AABA, ABAB, AAAB (last-bar fill) | none | repeats the take's bars, varying the last |
| Key and scale | follows the global key and scale (the NoteSnap toggle), or set here | global | `Recipe.key/scale` |
| Seed | number, dice icon (`seed + 1`), lock | random | `Recipe.seed` |
| Creativity | 0 .. 1 | 0.3 | `variation` and the sampling temperature `T = 1 + 2c` (`dials.md`); the critic floor stays on at any value |
| Energy | -1 .. 1 | 0 | `Recipe.energy`: velocity, density, hat density |
| Mood | -1 .. 1 | 0 | `Recipe.mood`: minor or major family when the scale is not set |
| Density | sparse .. busy | style | motif density, hat and ghost density, comp hits |
| Range | low and high note (drag on the key column) | style | motif range, bass octave, voicing register |
| Velocity | dynamics 0 .. 100 %, accent on downbeats | style | velocity spread around the energy value |
| Humanize | timing ms, velocity | style card `chords.human` | Humanizer node |
| Swing | 0 .. 75 % | style card `swing` | written into note starts |
| Gate | 10 .. 100 % | style card `motif.gate` | note lengths |
| Personal | on / off | off | `Recipe.personal`: leans on this machine's learned profile |

Per type (shown only for that type):

| Type | Settings |
|---|---|
| Chords, comp, pads, strum | chord size 2-6, voicing (close, open, drop-2, spread), inversion, voice leading on/off, rate (beats per chord), typed numerals (`i VI III VII`, overrides the bank), strum ms |
| Arpeggio | the 15 modes, rate 1/4 .. 1/32 and triplets, octaves 1-4, step gates (8) |
| Bass | role (7), octave, follow kick (land notes on the drum page's kicks) |
| Melody, counter-melody | contour (rise, fall, arch, wave, free), motif repeat 0-100 %, rest amount, harmony page |
| Drums | which rows, fills every 4 / 8 bars, ghost amount |
| Continue / vary | length to add, how close to the source (creativity) |

Harmony source (chords, arp, bass, melody): **own progression** or **another page's chords**, so a bass and a melody can be generated over
the chords already on the Chords page.

### 12.4 In the panel
- **Toolbar:** a Generate chip (text, Primary, as the Studio's) and a type dropdown beside it; the Settings icon opens the card in the
  inspector column. `G` with the panel focused generates (the global Shift+G is untouched).
- **Where it lands:** a selection on the ruler sets the range; otherwise a new clip at the playhead for `bars`. Over an existing clip it
  asks nothing: it writes a new take into that clip (below).
- **Takes:** each Generate is a take on the clip (last 6, chips under the clip strip, Studio's grammar). Click to recall, keep with a pin,
  the rest drop on save. Takes are recipes (seed + settings), not note copies, so they cost a line each.
- **Regenerate a selection:** select notes or a bar range, Generate replaces just that part, the rest fixed.
- **Locks:** lock rhythm (new pitches on the same starts and lengths) or lock pitch (new rhythm on the same pitch order). Both are
  post-passes on the captured notes.
- **Sample Studio:** a take there gets "Notes to Piano Roll" in Convert to node: it runs `RenderNotes` on the take's recipe and opens a
  page with the notes (Block C).

### 12.5 Code (Block A unless marked)

| Piece | Where | Note |
|---|---|---|
| `OfflineGraph::RenderNotes(bars, std::vector<CapturedNote>&)` | `src/app/graph/OfflineGraph.*` | Cooks note sources only; pairs note-on/off by `voiceId`; frame -> tick at the graph's bpm |
| `NoteGen::Generate(const GenSpec&) -> Take` | new `src/app/graph/NoteGen.*` | Builds the Recipe, compiles, captures, runs the critic gate, applies form, swing, gate, locks, scale snap |
| `Recipe` note fields | `SampleStyles.h` | `chordSize`, `voicing`, `inversion`, `arpMode`, `arpRate`, `arpOctaves`, `bassRole`, `contour`, `motifRepeat`, `density`, `rangeLo/Hi`, `numerals`. **Each defaults to "the card picks"**, so every existing sample compiles unchanged |
| Part split | `NoteGen` | Every captured note carries the voice that received it; a type keeps only its part's voice |
| Drum map | `NoteGen` | Drum Sequencer lanes -> GM notes (§12.2 #8) |
| Drum rows | panel, Block B | Key column variant |
| Types 9-12, Studio button | Block C | |

### 12.6 Save, checks
- **Save:** `gen <clipId> type=chords style=deep_house seed=42 bars=4 c=0.30 ...` after the clip's `clipnote` lines; takes as
  `gentake <clipId> seed=43 pin=1`. Unknown keys are ignored, so new settings load in old builds as plain notes.
- **Checks:**
  - `INFINITE_NOTEGENTEST`: every type x every style family, 3 seeds: notes in range, in key (or the chosen scale), no overlaps on one
    pitch, the bar count right, same seed -> same notes twice.
  - Parity: `RenderNotes` notes equal the notes the audio render's voices receive for the same recipe (the `determinism-check` route).
  - The wasm parity test (`wasm/parity_dump.js`) stays green: the new Recipe fields at their defaults change no sample.
  - Musical, judged on unseen seeds with the data-universe tools: the critic pass rate per type, B5 variety, density per style family
    (the 2026-10-29 decision).

### 12.7 What the model can and can't do today (from `STATUS-2026-10-29.md`)

| Measure | Today | Bar | What that means in the piano roll |
|---|---|---|---|
| Next chord | +14.3 over baseline | met | Progressions are the strongest type |
| Register (bass, melodic) | 97.5 %, 100 % | met | Lines sit where they should |
| Key | 51 % exact, 70 % lenient | miss | Generation is always snapped to the page's key, so out-of-key notes never land; the miss is in *guessing* a key, which the global scale makes unnecessary |
| Density | bass 53.5 %, melodic 78.5 % | miss | The Density setting overrides; judged per style family from now on |
| Variety (B5) | 0.72 x real | miss | Takes can feel alike; Creativity's temperature is the fix being tested, and the dice is always one click |

The piano roll ships on the engine as it is. Each model gain (the chord_nn.py comparison, temperature for variety) lands in
`SampleStyles`/`NoteGen` and improves the Studio and the roll together, with no UI change.

## Block A status (2026-10-30, branch `feature/piano-roll-a-model`, worktree `wt-piano-roll`)

| Step | State | Commit |
|---|---|---|
| 1-3 Model, note ops, NoteSchedule, Piano Roll node (loops, chance, bend/pressure/timbre, thru) | done, `pianoroll-check` + `INFINITE_PIANOROLLTEST` (120/87 bpm) ALL OK | 3c55e136 |
| 4 Save/load (`streamnote`/`clipmidi`/`clipnote`), PatchJson, byte-stable round trip | done | 1f210052 |
| 5 `.mid` import (`Arrange::ImportSong`) + "Open in Piano Roll" on MIDI File | done; the swap itself is UI-only (not headless-tested) | b3651764 |
| 6 NoteGen | **not started** | |
| 7 Sweeps, determinism row, wasm parity, `gen-patch-skill --check` | **not run** | |

### Left for step 6 (NoteGen), with the findings so far
- `OfflineGraph::RenderNotes(bars, std::vector<CapturedNote>&)`: after `Prepare`, process only entries whose owner is an
  `INoteSource`; register one extra consumer (`NoteEventQueue::RegisterConsumer`, no device running) on every outbox that feeds a
  non-note-source (a voice), pop it each block, pair on/off by `voiceId`, frame -> beats with `mClock.BlockStartBeats()` + offset.
  Tag each note with the consumer's patch index, so the part split keeps one voice's notes.
- Drums do not go through note cables: the Drum Sequencer is an audio source. Read its compiled params directly: `steps` (32),
  `swing`, and `stepVel[lane][step]` (`lane<i>_...` keys). Lane order is `kKitFiles` (SampleStyles.cpp:74): kick, snare, closed hat,
  open hat, clap, low tom, high tom, bell -> GM 36, 38, 42, 46, 39, 45, 50, 56. Synth-replaced lanes (`synthDrums`, volume 0) still
  hold the full pattern in the sequencer, so the sequencer alone is the truth.
- `src/app/graph/NoteGen.*`: `Generate(GenSpec) -> Take`: Recipe (part from type), `SampleStyles::Compile`, `Patch` parse,
  `OfflineGraph::Build/Prepare`, `RenderNotes`, keep the type's voice, critic gate (up to 3 redraws), form, swing, gate, scale snap.
- Recipe note fields (`chordSize`, `voicing`, `inversion`, `arpMode`, `arpRate`, `arpOctaves`, `bassRole`, `contour`,
  `motifRepeat`, `density`, `rangeLo/Hi`, `numerals`) all default to "the card picks" so wasm parity stays green.
- `INFINITE_NOTEGENTEST` as §12.6.

### Left for step 7
audio-node-sweep, audio-pipeline-sweep, determinism-check (add a Piano Roll row), `wasm/parity_dump.js`,
`tools/gen-patch-skill.py --check`. Known cost for Block B: every arrange revision bump rebuilds the audio topology
(`ArrangeAudioRebuildIfStale`), so note edits do too; skip MIDI-only changes there.
