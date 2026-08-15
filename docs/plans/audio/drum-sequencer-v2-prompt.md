# Drum Sequencer v2 — layout redesign, per-lane sample cards, per-lane outs

Implementation prompt for a fresh Claude Code session on Infinite.
Supersedes the layout half of `docs/plans/audio/drum-sequencer-prompt.md`
(§1a's "one shared strip for the selected lane" decision is explicitly
reversed here). The DSP, Transport-derived step firing, choke groups,
`SampleSlot` lifetime handling, and the dirty-tracking push discipline in
the existing implementation are **correct and stay** — this is a rework of
the data model's shape and the entire node body, not a rewrite of the
engine-side node.

---

## 0. What exists today (verified, read this before changing anything)

- `src/nodes/DrumSequencerNode.h` / `.cpp` (159 / 699 lines). Untracked —
  never committed, so **there is no save-format compatibility burden**.
  You may change `kMaxSteps`, drop params, and rename freely.
- Registered at `src/main.cpp:2122` (`REGISTER_NODE(DrumSequencerNode, Drum
  Sequencer, "Synths")`), built via `CMakeLists.txt:121`.
- Body drawn by `DrawDrumSequencerBody` — `src/main.cpp:5596-5800`,
  dispatched at `src/main.cpp:9191`. Grid drag state is the file-scope
  `gDrumGridDrag` at `src/main.cpp:5587-5594`.
- Sample-panel drag-drop and OS file-drop resolve a lane via
  `DrumSequencerLaneForScreenY` (`src/main.cpp:647`), which reads the
  `gridScreenTopY` / `gridScreenRowH` fields the body caches each frame
  (`DrumSequencerNode.h:106-107`). Call sites: `src/main.cpp:19742-19751`
  and `src/main.cpp:25869-25873`.
- Test fixture `RunDrumSequencerFixture` at `src/main.cpp:15606-16100`ish,
  invoked at `src/main.cpp:16319`; the node-round-trip fixture pokes
  `gNodes[27]` at `src/main.cpp:17237`.
- `src/audio/SampleSlot.h` — per-lane buffer handoff, already 8 instances.
  Do not touch.
- `SamplerNode` already has exactly the waveform widget this redesign wants:
  `waveformMin/Max/waveformCacheCount` (`SamplerNode.h:92-98`), `start` /
  `end` normalized 0..1 (`SamplerNode.h:107-108`), and the draggable
  start/end handle drawing in `DrawSamplerWaveform`
  (`src/main.cpp:4697-4790`ish). **Reuse this pattern; do not invent a
  second one.**

---

## 1. Target layout

The node body, top to bottom:

**A. Lane cards — 4 rows × 2 columns, 8 cards total.** Each card is one
lane, in lane order (left column lanes 1-4, right column lanes 5-8), and
contains:
  - a waveform view (drag-&-drop target, click-empty-to-open-file-dialog)
    with two draggable play heads marking sample start and end;
  - an `x` button that clears the sample from that lane;
  - five small knobs in a row: **transient, decay, pitch, volume, pan**.

**B. An 8 × 8 step grid** (8 lanes × 8 steps). A cell's fill height is its
velocity; click toggles, vertical drag on a lit cell sets velocity —
already implemented, keep the gesture exactly as-is.
  - Left of each row: **R** (randomise that row's fill + velocity), then
    **M** and **S** (that lane's mute / solo).
  - Right of each row: a **small swing knob**, per row.
  - Far right of each row: that lane's **individual audio out pin**.

**C. A global control row at the bottom:** rate, steps, swing, transient,
decay, pitch, volume, pan — plus the node's global out pin (the sum of all
lanes), which is what already exists.

**D. No note input.** The node triggers only from its own
Transport-derived sequence at the chosen rate.

---

## 2. Decisions and assumptions — read before starting

1. **"remove note triggering" is scoped to this node only.** The standalone
   `SamplerNode` keeps its note input; only `DrumSequencerNode`'s
   `noteInput` / `baseNote` / note-event path is removed. If that reading
   is wrong, stop and ask before touching `SamplerNode`.
2. **`kMaxSteps` drops 16 → 8**, `numSteps` defaults to 8. The mockup is
   explicit about an 8×8 grid, and nothing shipped depends on 16.
3. **Global transient/decay/pitch/volume/pan are offsets, not replacements.**
   Each is applied on top of the per-lane value (additive for pitch in
   semitones, transient, and pan — clamped to the lane range; multiplicative
   for volume; additive for decay then clamped 0..1). This is the only
   reading under which a global row and a per-lane row can coexist. The
   knobs' neutral position is 0 (×1 for volume).
4. **Per-lane out pins are a cross-cutting engine change, not a node-local
   one.** Flagging this up front because it is the single most expensive
   item here and the rest of the redesign does not depend on it: see §6.
   Everything in §3-§5 must land and pass hygiene *before* §6 begins, so
   the redesign is shippable even if §6 gets deferred.

---

## 3. Model changes — `DrumSequencerNode.h/.cpp`

**Remove:**
- `NoteCable noteInput;`, `NoteInputSlot()`, `InputLabel()`, `baseNote`,
  `mLastBaseNote`, the `v.Int("baseNote", ...)` line (`.cpp:593`).
- Audio-side: `SetNoteInbox` / `mNoteInbox` / `mBaseNote` (`.cpp:81, 122,
  154, 205-258, 389, 415`) and the `baseNote` argument to `PushGlobals`
  (`.cpp:116`). The per-sample loop keeps its step-event merge and simply
  loses the note-event merge; `noteIdx` and `noteEvts` go away.
- `selectedLane` and the whole "selected lane strip" section
  (`main.cpp:5753-5768`) — every lane now shows its own controls.

**Add to the main-thread node:**
```cpp
static constexpr int kMaxSteps = 8;          // was 16
static constexpr int kWaveCache = 128;       // per-lane, smaller than SamplerNode's 256

float laneStart[kNumLanes];                  // 0..1, defaults 0
float laneEnd[kNumLanes];                    // 0..1, defaults 1
float laneSwing[kNumLanes];                  // 0..1, defaults 0
float laneWaveMin[kNumLanes][kWaveCache];
float laneWaveMax[kNumLanes][kWaveCache];
int   laneWaveCount[kNumLanes] = {};

float globalTransient = 0.0f;                // -1..1
float globalDecay     = 0.0f;                // -1..1
float globalPitch     = 0.0f;                // -24..24 semitones
float globalVolume    = 1.0f;                //  0..2
float globalPan       = 0.0f;                // -1..1

void RandomizeLane(int lane);                // fill + velocity, one row
void ClearLane(int lane);                    // the card's `x`: buffer, path,
                                             // name, status, waveform cache
```
Give every new field a shadow copy in the `mLast*` block and fold it into
`PushDirtyParams` — the whole point of that block (see the comment at
`DrumSequencerNode.h:130-135`) is that a no-change cook writes nothing
across the thread boundary. A new param that pushes unconditionally
silently defeats it.

Declare every new field in `VisitParams` (`.cpp:~580-600`). The waveform
cache is **not** a param — it is rebuilt by `ReloadFromPaths()`, the same
way `FinishLaneBuffer` must now populate it at load time (mirror
`SamplerNode`'s `BuildWaveformCache` tail, `SamplerNode.h:118`).

`AUDIOPARAMSWEEPTEST` enforces the VisitParams↔mailbox correspondence and
will fail loudly on anything you forget — run it, don't eyeball it.

---

## 4. Audio-side changes — `AudioDrumSequencerNode` in `DrumSequencerNode.cpp`

1. **Start/end honoring.** A lane voice currently plays from frame 0 to
   buffer end. It must now start at `laneStart * numFrames` and stop at
   `laneEnd * numFrames`. Clamp `end > start` by at least one frame at the
   push site (main thread), not on the audio thread. Note the existing
   decay-coefficient comment (`DrumSequencerNode.h:122-128`): the
   coefficient scales to `laneSampleLenSec`, which should now scale to the
   *selected range* length, not the whole file.
2. **Per-lane swing.** Global `swing` currently offsets odd steps. Make the
   effective swing for lane L `clamp(swing + laneSwing[L], 0, 1)` and apply
   it per lane when computing that lane's step time. The step-time
   derivation must stay a pure function of `Transport::Beats()` — do not
   introduce any per-lane accumulated counter (see the class comment at
   `DrumSequencerNode.h:22-30`; that property is what makes rewind and
   tempo change work without special cases).
3. **Global offsets** fold into the existing `laneVolNow / lanePanNow /
   lanePitchNow` snapshot arrays at the top of `ProcessBlock` — one place,
   applied once per block, so per-sample cost is unchanged.

Everything in `ProcessBlock` stays under the real-time constraints listed
in `src/audio/AudioNode.h:6-21`.

---

## 5. UI rebuild — `DrawDrumSequencerBody`, `src/main.cpp:5596-5800`

Replace the body wholesale. Consult
`.claude/skills/audio-node-ui/SKILL.md` for the layout grammar, section
widgets, and knob helpers before writing widget code — in particular its
warning that `ed::CanvasToScreen` hangs outside the node draw.

- **Width.** `kAudioNodeWidth` is 440 (`src/main.cpp:171`). Two waveform
  cards side by side plus five knobs each does not fit. Introduce a wider
  constant for this node (≈880-960) and pass it to `BeginAudioBody`, which
  already takes an explicit width. Do not change `kAudioNodeWidth` itself —
  every other audio node is tuned to it.
- **Lane cards.** Factor a `DrawDrumLaneCard(DrumSequencerNode* n, int
  lane, float w)` helper. Waveform height ≈52px. The start/end handles use
  the `SetNextItemAllowOverlap()` + body-catcher-first ordering from
  `DrawSamplerWaveform` (`src/main.cpp:4705-4714`) — that comment documents
  a real ImGui overlap trap; reproducing the widget without reproducing the
  ordering gives you dead handles.
- Clicking an empty card's waveform opens `Platform::OpenAudioDialog()` and
  loads via the existing `LoadFileToLane`. The `x` calls `ClearLane`.
- Every mutation of node state from the UI is preceded by
  `PushUndoCheckpoint()` — match the existing body, which does this
  correctly at every site.
- **Grid.** Keep the cell gesture code at `src/main.cpp:5691-5741` almost
  verbatim; only the surrounding geometry changes. Left gutter is now
  `R | M | S` (three ~15px buttons, `R` calling `n->RandomizeLane(lane)`);
  right side gains a compact swing knob and, after §6, the lane's out pin.
- **`gridScreenTopY` / `gridScreenRowH` must still be cached** from the new
  grid's geometry. Three call sites outside this function depend on them
  (§0); dropping them silently breaks sample drag-drop onto lanes with no
  compile error.
- **Global row.** `rate` (dropdown), `steps`, `swing`, then `transient`,
  `decay`, `pitch`, `volume`, `pan`. Use two `AudioKnobRow`s rather than
  one row of 8 cramped knobs.
- Keep the existing Run / Randomise / Clear buttons below the global row.

---

## 6. Per-lane audio outputs (do this last, as its own commit)

**This is an engine-level change**, because today an `AudioNode` writes
exactly one output buffer and an `AudioCable` records only *which node*
feeds a pin, with no output index:
- `AudioCable` (`src/core/AudioCable.h:16-26`) stores a bare `INode*`.
- `AudioNode::ProcessBlock` (`src/audio/AudioNode.h:27`) takes a single
  `AudioBuffer& output`.
- `AudioTopologyEntry` (`src/audio/AudioEngine.h:27-33`) has a scalar
  `outputBufferIndex`.

The note side already solved the identical problem for Note Router, and
that is the pattern to mirror exactly:
- `NoteCable::Connect(INode*, int outputSlot = 0)` + `GetOutputSlot()`
  (`src/core/NoteCable.h:11-18`);
- editor-side pin count via `INode::OutputCount()` / `OutputLabel()`
  (`src/core/INode.h:124-125`), as `NoteRouterNode` does
  (`src/nodes/NoteNodes.h:582-583`);
- audio-thread-side fan-out via `AudioNode::NoteOutbox(int outputSlot)`
  (`src/audio/AudioNode.h:51`).

Steps:
1. `AudioCable`: add `int mOutputSlot`, defaulted `Connect(INode*, int
   outputSlot = 0)`, `GetOutputSlot()`. Every existing call site keeps
   compiling.
2. `AudioNode`: add `virtual int AudioOutputCount() const { return 1; }`
   and `virtual void ProcessBlockMulti(const AudioBuffer* const* inputs,
   int numInputs, AudioBuffer* const* outputs, int numOutputs)` whose
   default body calls `ProcessBlock(inputs, numInputs, *outputs[0])`. No
   existing node changes.
3. `AudioTopologyEntry`: `outputBufferIndex` → `int
   outputBufferIndices[kAudioMaxNodeOutputs]` + `int numOutputs`, with a
   new `constexpr int kAudioMaxNodeOutputs = 9;` beside the existing caps
   at `src/audio/AudioEngine.h:19-21`. Update `AudioEngine::RunTopology`
   (declared `AudioEngine.h:154`) to allocate and hand over N buffers and
   call `ProcessBlockMulti`.
4. `RebuildAudioTopology` in `main.cpp`: when resolving a consumer's audio
   input, index the source entry's `outputBufferIndices[cable
   ->GetOutputSlot()]` instead of its scalar. Guard an out-of-range slot to
   0 rather than trusting a stale saved patch.
5. **Cable save/load must persist the slot.** Find how note-cable output
   slots are serialized and mirror it for audio cables; an audio cable that
   round-trips back to slot 0 turns 8 lane outs into 8 copies of the mix
   after one save/load, and no compiler will tell you.
6. `DrumSequencerNode`: `OutputCount() → 9`, `OutputLabel(i)` = `"1".."8"`
   for lanes and `"out"` for index 8 (keep the mix at the **last** index so
   that a patch made before this change, which resolves to slot 0, does not
   silently become "lane 1 only" — or, if you prefer the mix at 0, verify
   there are no such patches and say so). The audio node writes each lane's
   voice sum into its own output buffer and the full mix into the mix
   buffer, in one pass — do not run the voice loop nine times.
7. Pin placement: the lane out pins must line up with their grid rows.
   Check `.claude/skills/audio-node-ui/SKILL.md` for whether the node
   drawing code supports per-output pin Y placement; if it does not, put
   the 8 lane pins in a vertical stack along the node's right edge and
   accept approximate alignment rather than reworking the pin layout
   engine.

---

## 7. Tests and exit criteria

Update `RunDrumSequencerFixture` (`src/main.cpp:15606+`): its note-trigger
case (`nodeNote`, ~`:15829`) is deleted, its 16-step cases become 8-step,
and it gains coverage for (a) `laneStart`/`laneEnd` actually bounding
playback, (b) per-lane swing shifting one lane's onsets and not another's,
(c) global offsets composing with per-lane values, (d) after §6, lane out
buffer *k* containing only lane *k*'s voices while the mix buffer contains
all of them. Also fix `src/main.cpp:17237`'s `gNodes[27]` poke if the
registration order shifts.

Done means all of:
```bash
INFINITE_AUDIOPARAMSWEEPTEST=1 ./build/Infinite && INFINITE_AUDIOTEARDOWNSWEEPTEST=1 ./build/Infinite
```
pass, and the full `run-infinite-hygiene` skill passes end to end (it
covers the 167-node round trip, save/load, and undo/redo — all three are
load-bearing here, since this change touches VisitParams, cable
serialization, and node width).

Per the standing project rule: after building, copy `build/Infinite.app` to
`~/Desktop/Infinite.app`.

---

## 8. Sequencing

| Commit | Contents | Gate |
|---|---|---|
| 1 | §3 model + §4 audio, note input removed, old UI minimally patched to compile | param sweep passes |
| 2 | §5 full UI rebuild | hygiene passes, node looks right in the app |
| 3 | §6 per-lane outs (engine + node + serialization) | hygiene + new fixture cases pass |

Stop and report after each commit rather than landing all three blind.
