# Sample Studio: waveform and slice editor (spec for sign-off, 2026-10-29, revision 2)

Status: **spec only. No code until Naman signs off** (new UI and a new saved section are the owner's call).
Lane 3 (Major): a new saved section, a new offline render chain, a new drop target.
Builds on the Sample Studio from `920c5ac0`, `4cd641d5`, `436da494` and `b0cc4c33`, all on `feature/sample-dots` and none on `main` yet
(`docs/plans/data-universe/SAMPLE-STUDIO-DESIGN.md`).

Revision 2 follows Naman's call on 2026-10-29: the Studio is a self-contained sample editor, a "Sampler pro" editor. You go deep in it,
and nothing on the canvas changes while you edit. When you are happy, you add the result to the patch as a node (Sampler, Slicer and so
on, as Convert to node does today), and play with it there.

## 1. What Naman asked for

In his words: zoom in and out of the waveform; cut it into slices, each slice with its own sample identity; per-slice gain, pitch and
fine tune; per-slice time-stretch and compress; per-slice colours; per-slice fade in and out; per-slice transient and decay shaping;
"add settings according to professional standards", including anything he missed; and drag and drop of any sample into the studio.

The bar: Ableton Simpler/Sampler slicing, and Bitwig's Sampler and audio editor.

## 2. Competitor data we already have

What I found (all in the repo or `~/re-output/`):

| Source | What it says | Use here |
|---|---|---|
| `docs/plans/re-survey/ABLETON-ALL-MODELS-MAPPING.md` §3.1, `~/re-output/ableton/ableton_all_models.json` | Simpler (`OriginalSimpler`) and Sampler (`MultiSampler`) top-level structure from Live 12's schema: Player, Pitch, Filter, Shaper, VolumeAndPan, AuxEnv, LFOs, Key/Vel mod destinations. | Confirms the per-slice chain order: player, pitch, filter, shaper, volume and pan. |
| `docs/plans/re-survey/ABLETON-GAPS-AND-OUTPACE.md` Gap 2, `capability-diff.md` row P5/C, dossier C | Live's Beats warp cuts audio at onsets and loops each decay: 0 flams, 0 cents. Infinite's stretch keeps pitch, but smears drums: 10-14 extra onsets and 3 dB of crest factor lost. | Stretching each slice on its own, starting at its onset, is the Beats-mode answer. The stretch bench (`tools/re-survey/stretch_kit.py`) measures it. |
| `tools/re-survey/ableton_map.py:38` | Maps "Simpler / Sampler" onto our Sampler and Slicer nodes. | The Studio's results land in those two nodes. |
| `tools/re-survey/stretch_kit.py:156` | Bitwig's stretch modes include Elastique Pro and Slice. | Reference for the stretch-mode list (§4.4). |

What I did not find:
- No per-slice field list for Simpler, because the extractor stops at the top-level containers.
- No Bitwig Sampler field data.
- No measured UI or workflow numbers for slicing in either app.

Where the extras below claim a professional standard, the source is the vendors' public manuals and shipped UI. No competitor source code
was read (AGENTS invariant 1).

## 3. Shape: edit in the Studio, then send the result out

```
 drop a file / Generate a take
            │
            ▼
   ┌─────────────────── Sample Studio (all editing here) ───────────────────┐
   │  waveform + zoom · slices · per-slice settings · audition · Studio undo│
   │  plays through StudioLoopPlayer (Studio audio mode, canvas paused)     │
   └──────────────┬─────────────────────────────────────────────────────────┘
                  │  when happy: Convert to node / Export
                  ▼
   Sampler · Slicer (with the slices) · one Sampler per slice · note clip · files
                  │
                  ▼
   the patch: modulation, MIDI, effects, as for any node
```

- **Nothing on the canvas changes while you edit.** No node is spawned or linked. The Studio's audio stays in Studio mode, as it does
  today.
- **The result goes out as audio "as heard".** Every per-slice setting is rendered in, and the slice points travel with it. A Slicer made
  from the Studio plays slice *n* on note 36 + *n*, with each slice sounding exactly as it did in the Studio.
- **Modulation starts once the result is a node.** The Sampler's and Slicer's existing params (pitch, speed, attack, decay, volume and
  more) are modulatable there as they are today. The Studio itself has no mod cables: it is an editor, not an instrument.
- **The Studio keeps its work.** What you have open in the Studio (the source file, the slices and every setting) is saved with the patch,
  so reopening the patch reopens your edit (§6).
- **Undo inside the Studio** is the Studio's own: Cmd+Z while the mouse is over it. It never touches the patch's undo.

Because everything is rendered offline, no setting has to be cheap enough for a live voice. Stretch, compression and shaping all run on a
worker when an edit ends, so nothing has to be baked-but-not-modulatable as revision 1 needed.

## 4. Features

Columns: **Asked** means Naman asked for it. **Pro** means it is a professional-standard extra. **Phase** is the build phase in §10.

### 4.1 Waveform and zoom

| Feature | Detail | Asked / Pro | Phase |
|---|---|---|---|
| Zoom in / out | Cmd/Ctrl+wheel or pinch zooms around the mouse. `+` / `-` keys. From the whole file down to single samples. | Asked | 1 |
| Scroll | Wheel or two-finger swipe scrolls sideways. Drag the overview strip. | Pro | 1 |
| Overview strip | A thin whole-file waveform above the main view, with the visible range as a box you can drag and resize. As in Bitwig's audio editor. | Pro | 1 |
| Zoom to slice / fit | `Z` zooms to the selected slice(s). `Shift+Z` fits the whole sample. | Pro | 1 |
| Deep zoom | Below about 2 samples per pixel, it draws the sample line with dots. Peaks come from `WavePeaks` (up to 16384 buckets), then from raw frames. | Pro | 1 |
| Stereo view | One combined lane by default. A toggle shows L and R. | Pro | 1 |
| Ruler | Bars.beats at the transport tempo, or seconds, or samples. Click the unit to switch. | Pro | 1 |
| Level scale | A vertical zoom of the waveform drawing only (it never changes the sound), for quiet material. | Pro | 1 |

### 4.2 Bringing audio in

| Feature | Detail | Asked / Pro | Phase |
|---|---|---|---|
| Drop any audio file | wav, aiff, flac, mp3, m4a, caf and ogg: the canvas's `kAudioExt` list, through `AudioDecodeCache::DecodeCached`. A drop on the Studio is claimed by the Studio, not the canvas. | Asked | 2 |
| Drop from the browser | The Samples browser drags into the Studio too, on the same path. | Pro | 2 |
| Drop several files | The first file opens. A toast says the rest were ignored. | Pro | 2 |
| Generated takes | Generate works as today. A take is just another source: slice it, shape it, convert it. | existing | 2 |
| Long files | Up to 10 minutes. Longer files open with a toast offering to keep the first 10 minutes (memory: an 8 GB Mac). | Pro | 2 |
| Normalise | Normalise the whole sample (peak to -0.3 dBFS) or each slice. Each slice's gain is set so its peak lands there. | Pro | 4 |

### 4.3 Slicing

| Feature | Detail | Asked / Pro | Phase |
|---|---|---|---|
| Manual slices | Double-click on the waveform, or `Cmd+E` at the mouse or playhead, splits a slice. Drag a marker to move it. Delete, or double-click a marker, joins the two slices. | Asked | 2 |
| Auto-slice: transients | `SlicerDsp::Detect` (SuperFlux with Dixon peak picking, already shipping in the Slicer), with a **sensitivity** control and a **max slices** limit. | Pro | 2 |
| Auto-slice: beat grid | 1/4, 1/8, 1/16, 1/32 and triplets, from the transport tempo or the sample's own tempo (§4.5). | Pro | 2 |
| Auto-slice: equal | N equal parts, from 2 to 64. | Pro | 2 |
| Auto-slice keeps hand edits | Re-running auto-slice asks before it replaces slices you have edited. | Pro | 2 |
| Snap | Grid, zero crossing (within ±2 ms, so markers never click), or off. Hold Cmd/Ctrl to place freely. | Pro | 2 |
| Start and end of the sample | The existing start and end handles stay. Slices live between them. | existing | 2 |
| Each slice has its own identity | Name (double-click to rename), colour, note, and its own settings (§4.4). A slice keeps a stable id, so moving markers never re-points its settings. | Asked | 2 |
| Select | Click selects one slice. Shift-click selects a range, Cmd-click adds or removes one. Inspector edits apply to every selected slice (relative for knobs). | Pro | 3 |
| Audition | Click a slice's header strip, or press `Q`, to hear it alone. Arrow keys move the selection and audition. Play plays the whole sample as heard. | Pro | 2 |
| Crossfade between slices | One global **xfade** (0-20 ms). Each slice reads that far past its end and fades it out, so cuts never click. | Pro | 3 |

### 4.4 Per-slice settings (the inspector)

Every slice carries all of these. All are rendered offline when an edit ends, and you hear them at once in the Studio.

| Group | Setting | Range | Asked / Pro | Phase |
|---|---|---|---|---|
| Level | gain | -inf..+24 dB | Asked | 3 |
| | pan | L100..R100 | Pro | 3 |
| Pitch | pitch | ±48 st | Asked | 3 |
| | fine | ±100 ct | Asked | 3 |
| Time | stretch | off / repitch / beats / tonal | Asked | 5 |
| | length | ×0.25..×4, or "fit to grid" (stretches the slice to a whole number of grid steps) | Asked | 5 |
| Dynamics | compress | amount 0..1 (one knob), with threshold, ratio, attack, release and makeup under "more" | Asked | 4 |
| Shape | transient | -100..+100 % (cuts or boosts the attack) | Asked | 4 |
| | decay | -100..+100 % (shortens or lengthens the tail) | Asked | 4 |
| | fade in / out | 0..2000 ms, curve -1..+1 | Asked | 3 |
| Tone | filter | off, lp 12/24, hp 12/24, bp 12 (pillar P7 names), cutoff, res | Pro | 4 |
| Playback | reverse | on/off | Pro | 3 |
| | loop | off / forward / ping-pong, with loop start/end inside the slice (carried to the Sampler on convert) | Pro | 4 |
| Mix | mute, solo | on/off, in the slice header strip and the inspector | Pro | 3 |
| | choke | group 0 (none) to 8. Carried to the Slicer on convert, where a slice in a group stops the others in its group (open/closed hats). | Pro | 6 |
| Identity | name, colour, note | colour from the design palette (`ColourChip`), auto-coloured by index; note defaults to 36 + index | Asked (colour) | 2 |

Pitch with stretch "off" changes the slice's length, as on a tape. With stretch on, pitch keeps the length.

### 4.5 Tempo and warp

| Feature | Detail | Asked / Pro | Phase |
|---|---|---|---|
| Sample tempo | Guessed from the slice grid (onset spacing), shown and editable. Takes made in the Studio carry their tempo. | Pro | 5 |
| Fit to tempo | One click sets every slice's length so the sample plays at the transport tempo, slice by slice (Beats-mode behaviour, §2). | Pro | 5 |
| Warp markers | Free markers that pin a point in the sample to a beat, independent of slices. **Proposed for a later round**: slices plus fit-to-grid cover most loop use, and full warp is its own project. | Pro | later |

### 4.6 Sending the result out (the end of the work)

| Feature | Detail | Asked / Pro | Phase |
|---|---|---|---|
| Convert to Sampler | The whole sample as heard, as today. | existing | 2 |
| Convert to Slicer | The sample as heard plus its slice points, so slice *n* plays on note 36 + *n*. | Pro | 2 |
| Convert to Molder, Grain Molder, PaulStretch, Granular | As today, with the sample as heard. | existing | 2 |
| Slices to Samplers | One Sampler per slice (the slice as heard), wrapped in a Group, with loop and reverse set from the slice. | Pro | 6 |
| Slices to a note clip | A Slicer, plus a Timeline note clip that replays the sample in slice order at its slice positions (Live's "Slice to new MIDI track"). | Pro | 6 |
| Export | The whole sample as heard (as today: WAV 24/16, FLAC, MP3), or each slice as its own file (`<sample> 01 <slice name>`). | Pro | 6 |
| Save / load a slice set | Save the slices and their settings as a `.slices` file; load it onto another sample in the Studio. | Pro (asked as "per-slice save/load") | 6 |

Every Convert is one patch undo step, as today.

### 4.7 Keyboard (only while the mouse is over the Studio)

| Key | Action |
|---|---|
| `Cmd+E` | split at the mouse (or at the playhead if the mouse is outside the waveform) |
| `Delete` | join the selected slice to the one before it |
| `Z` / `Shift+Z` | zoom to selection / fit all |
| `+` / `-` | zoom in / out |
| `←` / `→` | previous / next slice (auditions it) |
| `Q` | audition the selected slice |
| `M` / `S` / `R` | mute / solo / reverse the selected slices |
| `Cmd+Z` / `Cmd+Shift+Z` | Studio undo / redo |

`shortcuts-sweep` will check each key against the global map and the help window. The Studio claims a key only while hovered, so canvas
W/A/S/D movement and the global shortcuts are untouched.

## 5. Data model

A new `src/audio/SliceSet.h/.cpp`, with plain data and no ImGui, so the headless tests can drive it.

```cpp
struct SliceParams {            // defaults = the slice plays untouched
   float gainDb = 0, pan = 0, pitchSt = 0, fineCt = 0;
   int   stretchMode = 0;       // 0 off, 1 repitch, 2 beats, 3 tonal
   float lengthRatio = 1; bool fitToGrid = false;
   float compAmount = 0, compThreshDb = -18, compRatio = 4, compAttackMs = 10, compReleaseMs = 120, compMakeupDb = 0;
   float transient = 0, decay = 0;               // -1..+1
   float fadeInMs = 0, fadeOutMs = 0, fadeCurve = 0;
   int   filterMode = 0; float cutoffHz = 20000, res = 0;
   bool  reverse = false; int loopMode = 0; float loopStart = 0, loopEnd = 1;
   bool  mute = false, solo = false; int choke = 0;
};
struct Slice {
   uint16_t id;                 // stable, never reused while the slice exists
   int64_t  start;              // frame in the source; a slice ends where the next one starts (or at the end marker)
   std::string name; uint8_t colour; int note;
   SliceParams p;
};
struct SliceSet {
   std::vector<Slice> slices;   // sorted by start, at most 64 (the Slicer's kMaxSlices)
   int64_t endFrame; float sampleBpm = 0; float xfadeMs = 2;
   std::string Serialize() const;                    // versioned text: "sliceset 1 ..."
   static bool Parse(const std::string&, SliceSet&);
   void SplitAt(int64_t frame); void Join(int index); void Move(int index, int64_t frame);
   void FromOnsets(const std::vector<int>& onsets, const std::vector<float>& strengths, int maxSlices);
   void FromGrid(double framesPerStep, int64_t from, int64_t to);
   void FromEqual(int n, int64_t from, int64_t to);
};
int64_t SnapToZeroCrossing(const Platform::SampleBuffer&, int64_t frame, int windowFrames);
```

The Studio document is `{ source, SliceSet, sample-wide settings (today's pitch, speed, loop, reverse, ping-pong, fades) }`. Studio
undo is a capped stack of snapshots of that document (the document is small; the audio is never copied).

## 6. Save and load

- **The Studio's work, in the patch.** One new patch section, `studio`, holds the source path and `SliceSet::Serialize()`, next to the
  existing `panelStudio*` layout keys in `PatchIO.cpp`.
  - A generated take is written to a WAV in Recordings when the patch is saved, so the path always points at a real file.
  - A patch without the section opens the Studio empty, as today.
  - The slice line writes fields by name (`g=-3 p=12 ...`) and leaves defaults out, so a field added later loads as its default.
- **The result, in its node.** A converted Sampler or Slicer saves as any node does, through its file path and params.
  - The Slicer needs one small public setter, `SetSlices(fractions)`: it writes the given points as its hand-edited onsets and sets the
    count to match, so the slices survive save, load, copy/paste and undo through its existing `slices` blob.
  - Nothing about how old Slicer patches load changes.
- **Checks:** a save → load → save round trip of the Studio section must give the same bytes (new headless test). For the converted
  Slicer, `audio-node-sweep` and `data-accuracy-sweep` cover the param round trip, copy/paste and duplicate.

## 7. Modulation

- Inside the Studio: none. It is an editor, so no ParamRefs and no mod dots.
- After convert: the Sampler's and Slicer's existing ParamRefs, unchanged. `modulation-sweep` and `node-param-audit` confirm the converted
  nodes behave like any other Sampler or Slicer.
- **Later round, if wanted:** a "keep slices live" convert, where the Slicer carries per-slice gain, pitch, pan and filter as modulatable
  params. Not in this build.

## 8. DSP path (offline, one chain)

```
source frames ─► per slice: stretch (if on) ─► read (pitch, reverse, loop, xfade tail)
             ─► fades ─► transient/decay shaper ─► compressor ─► filter ─► gain ─► pan
             ─► slices laid end to end (mute/solo applied) ─► the sample "as heard"
```

- **`src/audio/SliceRender.h/.cpp`**: renders one slice, and the whole set, offline, on a worker.
  - **Cancel:** a new edit cancels the running render and starts again.
  - **When it runs:** after a drag or a knob edit ends. It does not run on every drag frame.
  - **Cache:** only changed slices re-render, each cached by a hash of its settings.
- **Stretch:** Signalsmith Stretch (MIT, already vendored and used by `ClipTimeStretch`).
  - **Beats** stretches each slice on its own from its onset.
  - **Tonal** stretches with transient protection.
  - **Repitch** is plain resampling.
- **Pitch:** this replaces today's grain `PitchShift` in the panel with a cleaner path. Stretch off means resampling. Stretch on means
  Signalsmith's pitch shift.
- **Shaper and compressor:** their maths come from `TransientShaperKernel` and `DynamicsKernel`. The DSP cores move into small headers
  that both the kernels and the renderer call, so the Studio's compressor is the same compressor as the Dynamics node.
- **Filter:** a TPT state-variable filter.
- **Playback:** the rendered "as heard" buffer goes to `StudioLoopPlayer` as today. Audition plays one slice's render through the same
  player. Export and Convert write the same buffer, so what you heard is what goes out.
- **Determinism:** the same document gives the same bytes. A `determinism-check` row is added. Profile: Offline (quality first), with the
  Studio's audition on the existing player.

## 9. UI layout (bottom dock, the existing panel)

```
+-------------------------------------------------------------------------------------------------------------+
| (Generate) [instrument v] (scale) [bars v]  (>) Loop Ping-pong Reverse  |  [transients v] sens ==o== (Slice) snap [grid v]  ... Export  Convert to  (gear) |
+--------------------------------------------------------------------------------------------+----------------+
| overview  [========visible========]                                                         | SLICE 3 "snare"|
| 1.1        1.2        1.3        1.4        2.1   ruler                                     | colour ● note D1|
| |▓▓01 kick▓▓|░░02 hat░░|▓▓03 snare▓▓|░░04 hat░░|...   slice header strip: colour, name, M S  | Level  gain pan |
| |~~~~/\/\~~~|~~~~/\~~~~|~~~/\/\/\~~~|~~~/\~~~~~|      waveform, slice regions tinted       | Pitch  st  ct   |
| |           |          |            |          |      markers with grab tabs                | Time   mode len |
|                                                                                             | Shape  trn dec  |
|                                                                                             |   fade in / out |
|                                                                                             | Dyn    comp     |
|                                                                                             | Tone   filter   |
|                                                                                             | Play   rev loop |
+--------------------------------------------------------------------------------------------+----------------+
```

- **Toolbar:** today's row stays. A slice group adds the slice mode dropdown (transients / grid / equal / manual) with its one control
  (sensitivity, division or N), the Slice action, and snap. If the row is too narrow, the slice group wraps to a second row at the same
  tile height.
- **Drop:** an empty Studio says "Drop an audio file, or Generate". While a file is dragged over it, the waveform well lights in the
  accent.
- **Slice header strip:** each slice's colour, number and name, plus mute and solo glyphs on hover. Clicking it selects and auditions.
- **Waveform:** each slice's region is tinted with its colour at low alpha (P10 budget: the waveform stays the loudest thing). Selected
  slices are outlined in the accent. A marker is a 1 px line with a grab tab, and the cursor changes over it.
- **Inspector:** the existing settings card on the right becomes the slice inspector. It uses the same `SectionCard` and `FieldWell`
  grammar, one section per group in §4.4. With no slice selected it shows the sample (today's settings). With several slices selected,
  fields show "mixed" and edit relatively.
- **Convert to:** the existing menu, with Slicer moved up and the new "Slices to Samplers" and "Slices to note clip" entries.
- **Design system:**
  - Every colour comes from tokens. The slice palette is a new role-token list (12 colours, checked in both themes). It is not literal
    colours.
  - Icons for split, join and snap come from the glyph set, or are drawn with `lint_icons.py` if missing.
  - Motion stays at 200 ms or less.
  - `inventory.py`'s ratchet must not grow.

## 10. Build phases (one branch, a commit per step)

Branch: `feature/sample-studio-slices` in its own worktree. Its base is `feature/sample-dots`, because that branch holds the Studio and
`main` does not have it yet; it merges after `feature/sample-dots`. I won't touch the generator files (`SampleStyles.cpp`,
`GenerateSamplesNode.*`). All builds go through `scripts/ibuild.py` at -j4 or less with nice, one heavy job at a time, after checking that
no render is running.

| Phase | What lands | Checked by |
|---|---|---|
| 1 | Zoom, scroll, overview, ruler, deep zoom, stereo view, level scale (on today's take) | screenshots at 4 zoom levels in light and dark; `panels-sweep` |
| 2 | `SliceSet` + tests; drop into the Studio; manual and auto slicing; snap; names, colours, notes; audition; Studio undo; the `studio` patch section; Convert to Slicer with slices (`SetSlices`) | headless SliceSet tests; save→load→save bytes; a drop driven in the app; `audio-node-sweep`, `data-accuracy-sweep` on the converted Slicer |
| 3 | `SliceRender` with gain, pan, pitch, fine, fades, reverse, mute, solo, xfade; multi-select inspector | render tests (silence in → silence out, unity settings → identical audio); click test at slice boundaries |
| 4 | Compressor, transient and decay shaper, filter, loop modes, normalise | `param-truth-audit` (each control does what its label says); `audio-pipeline-sweep` |
| 5 | Stretch (repitch, beats, tonal), length, fit to grid, sample tempo, fit to tempo | `stretch_kit.py` on the drum stimulus: flams and crest against today's 10-14 / -3 dB |
| 6 | Slices to Samplers, slices to note clip, choke on the Slicer, export slices, `.slices` files, shortcuts | `shortcuts-sweep`; `node-ui-sweep`; `verify-gate` on the whole branch |

Each phase is verified in the running app (driven, with screenshots), not only built. Help text for every new control goes into the four
help tables (stage 7). Windows and Linux: the only new platform-facing piece is the drop target, which uses the existing GLFW callback.

## 11. What sign-off decides

1. **Scope:** phases 1-6 as listed, with free warp markers and live per-slice modulation left for later. Or cut it to phases 1-4 first
   (zoom, drop, slices, every per-slice setting except stretch) and decide the rest after using it.
2. **Keys (§4.7):** `Cmd+E` split, `Q` audition, `M`/`S`/`R`, and the Studio's own undo, all only while hovering the Studio.

## 12. What the build does differently (2026-10-29)

Built: phases 1-6 in one pass, with these gaps against §4-§8.

| Spec | Built | Why / next |
|---|---|---|
| `.slices` sidecar files (§4.6) | Not built. The slice set saves only inside the patch's `studio` section. | The patch section covers "reopen my edit". A sidecar is a small follow-up on `SliceSet::Serialize()`. |
| Slices to a note clip (§4.6) | Not built. | Needs the Timeline clip API; its own step. |
| Choke carried to the Slicer (§4.4) | The choke field is edited and saved in the Studio, but the Slicer has no choke groups, so Convert drops it. | Choke groups in the Slicer are a Slicer change (new param), so they wait for the owner's yes. |
| Drop from the in-app Samples browser (§4.2) | Not built. Drops from Finder work. | The browser's drag payload is not wired to the Studio rect yet. |
| Shaper and compressor share the Dynamics node's DSP core (§8) | They live in `SliceRender.cpp`. | Moving the cores out of `TransientShaperKernel` / `DynamicsKernel` touches shipping nodes; kept for a separate refactor. |
| "Replace the slices?" (§4.3) | An in-panel `DialogParts` modal, not a platform dialog. | There is no platform confirm call; the modal matches the Arrange overwrite prompt. |

Owner test pass (2026-10-29), changes against the spec:

| Spec | Now | Why |
|---|---|---|
| Per-slice pitch with Length off repitches (§4.4) | Pitch never changes a slice's length; only Length = repitch moves time with pitch. | Owner: pitch up must not speed up. |
| Studio plays at the sample's own tempo | The edit is stretched (Signalsmith) to the transport tempo, so a faster transport plays faster at the same pitch. | Owner: tempo up raised the pitch. |
| Per-slice compress and filter (§4.4) | Hidden from the inspector; the fields still save and render. | Owner: too much per slice. |
| Fades as numbers | Fades are drawn as curves with corner handles; drag sets length, Shift-drag the curve. The wave shows gain and fades live. | Owner request. |
| Delete / copy / paste | Delete removes the slice's audio from the source; Cmd+C / Cmd+V pastes the slices right after the selection; Cmd+X cuts. | Owner request. |
| Split button | Scissors is a tool (C, Esc back); Shift+click splits with any tool; double-click opens settings; click auditions. | Owner request. |
| Generate by style | Style dropdown removed; Wild stays. | Owner request. |
| Convert to patch | Pastes only the chain that feeds the take's part (e.g. one Wavetable for a bass), not the whole arrangement. | Owner: drums showed up for a bass take. |
