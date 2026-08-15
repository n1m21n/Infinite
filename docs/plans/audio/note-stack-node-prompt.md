# Implement the Note Stack node in Infinite

Repo: `/Users/namansoni/infinte`. This prompt is self-contained — every file
path, line number, and function name below was verified against the current
tree, not copied from a doc. Where a skill file's stated line numbers are
stale, the real ones are given here; trust these.

---

## 1. What the node is

**Note Stack** — a note processor. One note input, one note output. It has
**eight independent transpose voices**, each a semitone knob with its own
on/off enable button. Every incoming note-on is emitted unchanged (the dry
note always passes through), plus one transposed copy per *enabled* voice.

So with three voices enabled at `+12`, `-7`, `+6`, a single incoming C4
produces four simultaneous note-ons: C4, C5, F3, F#4. Releasing that C4
releases all four.

Category: `Notes`. Shape: **note source / processor** — `INode` +
`INoteSource`, note-in at slot 0, implicit note output slot 0. Display name
`Note Stack`. (`Layer Stack` exists at `main.cpp:2252` in the `Compositing`
category — different name, no `NodeFactory::DuplicateNames()` collision, but
don't shorten this one to just `Stack`.)

**This node does not already exist.** `ChorderNode` (`NoteNodes.h:738`,
DSP at `NoteNodes.cpp:2758`) is the nearest-sounding sibling but is a
*generator*: it has no `NoteInputSlot`, runs on its own clock, and invents
scale-degree chords. Note Stack is input-driven and key-agnostic. Do not
extend Chorder; do not add scale/root quantization to Note Stack.

---

## 2. Read these first

| File | Why |
|---|---|
| `.claude/skills/new-audio-node/SKILL.md` | the procedure — prescriptive, don't re-derive |
| `.claude/skills/audio-node-ui/SKILL.md` | body layout, read before writing `DrawNoteStackBody` |
| `src/nodes/NoteNodes.cpp:947` (`AudioSemitoneShiftNode`) | **the direct template** — ~60 lines, transpose + note-off remap |
| `src/nodes/NoteNodes.cpp:1632` (`AudioNoteEchoNode`) | the reference for one-note-in / many-notes-out |
| `src/audio/NoteEventQueue.h` | `kCapacity = 256`; `Push` force-admits note-offs when full |

Note the skill's `ARCHITECTURE.md` size claim is stale: `src/main.cpp` is
**28,710 lines**, not ~12k. Re-grep before trusting any line number in a doc.

---

## 3. The `INode` half — `src/nodes/NoteNodes.h`

Declare `class AudioNoteStackNode;` with the other forward declarations at the
top of the file (lines 14–33), and add `NoteStackNode` after `ChorderNode`
(~line 771). Mirror `NoteEchoNode` (`NoteNodes.h:523`) exactly for the boilerplate:

```cpp
class NoteStackNode : public INode, public INoteSource
{
public:
   static constexpr int kVoices = 8;

   static INode* Create() { return new NoteStackNode(); }
   NoteStackNode();
   ~NoteStackNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;

   int  semitones[kVoices] = { 12, 7, 5, 3, -3, -5, -7, -12 };
   bool enabled[kVoices]   = { false, false, false, false, false, false, false, false };
   NoteCable noteInput;

   int LastStackSize() const; // main-thread readout: notes emitted for the last note-on

private:
   std::unique_ptr<AudioNoteStackNode> mAudioNode;
   int mLastCookFrame = -1;
};
```

Out-of-line ctor/dtor in the `.cpp` (`= default`) so `main.cpp` never sees the
audio-thread class — same as `NoteTransposeNode` at `NoteNodes.cpp:1010`.

**`VisitParams` — the save/load keys.** Arrays need one stable key per element;
`ParamVisitor` only has `Float`/`Int`/`Bool` scalars (`src/core/INode.h:71-73`).
Build the names with `snprintf` into a local buffer:

```cpp
void NoteStackNode::VisitParams(ParamVisitor& v)
{
   char key[24];
   for (int i = 0; i < kVoices; i++)
   {
      snprintf(key, sizeof(key), "semi%d", i);    v.Int(key, semitones[i]);
      snprintf(key, sizeof(key), "enabled%d", i); v.Bool(key, enabled[i]);
   }
}
```

These 16 names are permanent patch-file keys — don't rename them later.

`CookIfNeeded` pushes both arrays to the audio node and does nothing else
(no DSP, < 5 µs). Copy the `NoteTransposeNode::CookIfNeeded` shape at
`NoteNodes.cpp:1013`, including the lazy `make_unique` guard that also appears
in `GetAudioNode()`.

---

## 4. The `AudioNode` half — `src/nodes/NoteNodes.cpp`

Add `class AudioNoteStackNode : public AudioNode` just above the
`NoteStackNode` method definitions. Start from `AudioSemitoneShiftNode`
(`NoteNodes.cpp:947`) — it is the same node with `kVoices == 0`.

### Params cross the thread boundary as atomics

`std::atomic<int> mSemitones[8]` and `std::atomic<bool> mEnabled[8]`, written
by `CookIfNeeded` via setters, read `memory_order_relaxed` in `ProcessBlock`.
That is what `AudioSemitoneShiftNode` does for its single `mSemitones`; keep
it. No allocation, locks, `dynamic_cast`, `std::function`/`map`/`string`, GL,
ImGui, file I/O, or `printf` anywhere in `ProcessBlock`.

### The note-off bookkeeping is the whole correctness problem

`AudioSemitoneShiftNode` keeps `int mOutNote[128]` — one output note per held
input note — so the note-off can be replayed at the pitch that actually
sounded. Note Stack emits up to **9** notes per input note, so it needs:

```cpp
int8_t mOutNotes[128][kVoices + 1]; // -1 = unused slot
int8_t mOutCount[128];
```

Rules, each of which prevents a real stuck-note bug:

1. **Capture the enabled set at note-on, replay it verbatim at note-off.**
   Read `mEnabled`/`mSemitones` once when the note-on arrives, write the
   resulting pitches into `mOutNotes[in.note]`, and on note-off send offs for
   exactly those stored pitches — never re-read the knobs. Toggling a voice
   off while a note is held must not strand that voice's note on forever.
2. **Dedupe within one input note.** Two voices set to the same value, or a
   voice at `0` colliding with the dry note, would otherwise emit two note-ons
   and two note-offs for one pitch — downstream voice allocators
   (`src/audio/AudioVoice.h`) treat that as two voices and one leaks. Before
   pushing, skip any pitch already in `mOutNotes[in.note]`.
3. **Clamp, don't wrap.** `std::clamp(in.note + semi, 0, 127)`. A transposed
   copy that lands out of range should be dropped, not folded — but the dry
   note always goes out regardless.
4. **`PrepareToPlay` clears the tables** (`mOutCount[i] = 0`, `mOutNotes` to
   -1), same as `AudioSemitoneShiftNode:950`.
5. Every emitted event carries `out.frameOffset = in.frameOffset`,
   `out.source = this`, and the input's velocity unchanged.

The existing `NoteEvent evts[64]` pop-per-block in
`AudioSemitoneShiftNode:959` can produce 64 × 9 = 576 pushes into a
256-entry `NoteEventQueue`. That's a real but bounded overflow risk;
`NoteEventQueue::Push` already force-admits note-offs when full
(`NoteEventQueue.h:46-59`), so no note can hang — leave the queue alone and
don't raise `kCapacity`. Just be aware of it if the sweep reports overflow.

Expose `int LastStackSize() const` from an `std::atomic<int>` set on each
note-on, for the UI readout.

---

## 5. Wiring — `src/main.cpp`

Verified sites (line numbers are current; re-grep to confirm before editing):

1. **`#include "nodes/NoteNodes.h"`** — already included, nothing to add.
2. **`RegisterNodes()`**, after line 2316 (`REGISTER_NODE(ChorderNode, Chorder, "Notes");`):
   `REGISTER_NODE(NoteStackNode, Note Stack, "Notes");`
   The category must stay a single whitespace-free token — `Patch.cpp` reads
   `node <index> <category> <typeName>` with `>>` and a space silently eats
   the type name on load.
3. **`DrawNoteStackBody`** — add next to `DrawChorderBody` (line 7240).
4. **Dispatch ladder** in `DrawAudioNodeBody` — add a branch beside line 10037:
   ```cpp
   else if (auto* n = dynamic_cast<NoteStackNode*>(gn.node.get()))
      DrawNoteStackBody(gn, n);
   ```
5. **Node help table**, line ~12618 (the `"Note Echo"` / `"Chorder"` entries
   are at 12618 and 12623 — the skill's "~7780" is stale). One sentence in the
   existing voice, e.g.:
   `{ "Note Stack", "Layers transposed copies of every incoming note on top of the original - eight independent semitone voices, each switched on or off on its own. The dry note always sounds; the enabled voices are added to it, not instead of it. The set of voices is captured when a note starts, so switching one off mid-note never leaves it hanging." }`

`InputCountFor`, `CableFor`, connect/disconnect, cycle detection, patch
save/load, undo and copy/paste are **already generic** — they work off
`NoteInputSlot()` and `VisitParams()`. Adding an entry to any of them means the
node was built wrong.

---

## 6. The body UI — `DrawNoteStackBody`

Read `.claude/skills/audio-node-ui/SKILL.md` before writing this. The layout
the design asks for: **four voices above a centre readout, four below.**

```
BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);   // 440.0f, main.cpp:174
   AudioKnobRow(4)  -> voices 0..3, KnobInt(-24..24)
   a row of 4 AudioToggleButton pills, one under each knob    // main.cpp:4772
   the centre "in" strip — the dry note, always on
   a row of 4 AudioToggleButton pills
   AudioKnobRow(4)  -> voices 4..7
EndAudioBody();
```

Useful pieces, all already in `main.cpp`:

- `AudioKnobRow` (line 4609) — `KnobInt(label, &v, lo, hi)` at line 4645,
  `cellW = gAudioContentW / count` for aligning the toggles to the knob cells.
- `AudioToggleButton(label, bool*, width)` at line 4772 — the pill toggle.
  Use `##` suffixes for unique ImGui IDs (`"1##stackEn0"`, …).
- `DrawNoteEchoBody` (line 6948) is the minimal reference for the
  `BeginAudioBody` / knob row / `EndAudioBody` skeleton.
- `DrawNoteRouterBody` (line 6968) shows drawing a row of lit dots with
  `ImDrawList` — the same technique works for a "which voices are sounding"
  indicator if you want one, but it is optional.

Grey each knob (`ImGui::BeginDisabled()`) when its voice is off, so the card
reads at a glance. Status strip: something like
`"4 notes"` from `LastStackSize()`, or `"3 voices on"` when idle.

**On the minimalism rule** (`new-audio-node/SKILL.md` §0.5, "~8 controls
max"): a bank of eight identical voice slots counts as *one* control type, the
same way Note Sequencer's step grid does — 8 knobs + 8 pills is fine here.
What the rule does forbid is adding *more kinds* of control. Do not add:
per-voice velocity, per-voice delay/strum, spread, scale/root quantization, a
mode dropdown, a voice-count knob, or a mix/dry knob. Eight transposes and
eight switches is the entire node.

---

## 7. Open decision — resolve it this way unless you find a reason not to

**Do the top four knobs have to be positive and the bottom four negative?**
The layout is described as "4 above, 4 below" the incoming-note row, which
suggests it, but the motivating example (`+12`, `-7`, `+6` all enabled
together) doesn't require it.

**Recommendation: give all eight knobs the full `-24..+24` range; position is
layout only, not a constraint.** Constraining the banks would make the example
unreachable when a user happens to grab the wrong four knobs, and there's no
DSP reason for the asymmetry. The defaults above (`+12 +7 +5 +3` on top,
`-3 -5 -7 -12` below) give the layout its intuitive reading without enforcing
it. If you decide otherwise, say so explicitly in the commit message.

---

## 8. Tests

- **A DSP fixture.** Follow `INFINITE_DSPTEST` (`main.cpp:14263`, dispatched at
  `main.cpp:18231`). Assert, headlessly: one note-on in with voices at
  `+12/-7/+6` enabled yields exactly 4 note-ons at the expected pitches with
  the input's velocity; the matching note-off yields exactly 4 note-offs at
  those same pitches; disabling a voice *between* the on and the off still
  yields 4 offs (rule 4.1); two voices set to the same value yield 1 extra
  note, not 2 (rule 4.2); a voice transposing past 127 is dropped while the
  dry note still sounds (rule 4.3). Print a line ending `OK` / containing
  `FAIL`.
- **Confirm the generic sweeps pick it up** rather than writing per-node
  versions: `INFINITE_AUDIOPARAMSWEEPTEST` (`main.cpp:17290`, dispatched
  `18211`) must round-trip all 16 params, and `INFINITE_AUDIOTEARDOWNSWEEPTEST`
  (`main.cpp:24211`) must spawn/wire/delete it mid-playback with no crash and
  zero xruns. If the param sweep doesn't see the array params, fix
  `VisitParams`, not the sweep.

Then:

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean, then run `/run-infinite-hygiene`.

---

## 9. Out of scope

- Don't touch `ChorderNode`, `NoteEchoNode`, or `AudioSemitoneShiftNode` —
  read them as templates, leave them alone.
- Don't change `NoteEventQueue::kCapacity` or its overflow policy.
- Don't add entries to `InputCountFor` / `CableFor` / `ConnectGeometrySlot`.
- No geometry pins, so no `geometry-transform-sweep` run is needed.

## 10. Done when

All seven of `.claude/skills/new-audio-node/SKILL.md` §6's criteria hold.
Report each one explicitly, including updating `ARCHITECTURE.md`'s audio
section and adding a **Note Stack** row to `docs/plans/audio/README.md` §3
(the node table around line 131) — it is not specced there today, so add it
rather than assuming it exists.
