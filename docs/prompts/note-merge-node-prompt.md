# Add a Note Merge node

## Context you need before starting

Infinite's note system was built single-producer-per-consumer throughout.
Every synth/oscillator/VST note input, and every note-processing node, has
exactly **one** note input pin today — `NoteCable` is a single-connection
type by design (`src/core/NoteCable.h`), and the audio-thread wiring
(`AudioNode::SetNoteInbox`) mirrors that with no slot concept at all. This
task adds the first many-inputs-into-one note consumer, which means doing a
small, targeted generalization of the wiring infrastructure before the node
itself is straightforward.

**Do not model this on `MixerNode`'s "just add N cable slots" shape and stop
there** — `MixerNode` works because `AudioCable`/`AudioInputSlot` were
already multi-slot-capable everywhere. The note-side inbox plumbing is not,
and that's the real work here.

## What's already proven to be correct, and why (don't second-guess these)

- **Same-pitch note-on collisions from different sources are already a
  non-issue.** `NoteEvent` (`src/audio/NoteEvent.h`) carries `voiceId`
  (assigned once per note-on via `NextVoiceId()`, globally unique — see the
  atomic counter at the bottom of that file) and `VoiceAllocator::NoteOff`
  (`src/audio/AudioVoice.cpp:61-65`) matches note-off to note-on strictly by
  `voiceId`, never by MIDI note number. Two merged inputs firing the same
  pitch simultaneously become two independent, correctly-tracked voices with
  zero new logic required. Do not add any dedup/collision-avoidance code to
  Merge.
- **`source` is rewritten on every hop, not preserved.** Every existing
  single-input processing node in `src/nodes/NoteNodes.cpp` (see
  `AudioNoteFilterNode::ProcessBlock`, `AudioSemitoneShiftNode::ProcessBlock`,
  `AudioPitchBendNode::ProcessBlock`) sets `out.source = this;` on every
  event it forwards. Nothing in the codebase currently matches on `source`
  (`VoiceIdMap` keys purely by `voiceId`), so follow the established
  convention: Merge's `AudioNoteMergeNode::ProcessBlock` should also set
  `out.source = this;` on each forwarded event, for consistency with every
  other node in the file. The one field that must never be altered or
  regenerated is `voiceId` — pass it through byte-for-byte.
- **No note-priority (highest/lowest/last) logic belongs on Merge.** That's
  a separate, smaller follow-up scoped for `NoteStackNode`
  (`src/nodes/NoteNodes.h`/`.cpp`) later, since NoteStack already owns
  "what's currently held." Merge only needs correct timestamp ordering;
  last-note behavior for things like `NoteToCVNode::LastNote()` already
  falls out of that for free. **This prompt is only the Merge node — do not
  touch `NoteStackNode`.**

## Step 1 — generalize the inbox side to mirror the outbox side

The outbox side already supports N slots per producer:
`virtual NoteEventQueue* NoteOutbox(int outputSlot)` in `src/audio/AudioNode.h:61`
(used by `AudioNoteRouterNode`'s 4 outputs, `src/nodes/NoteNodes.cpp:1661`).
The inbox side has no equivalent. Add one, without breaking any of the ~20
existing single-input consumers:

1. In `src/audio/AudioNode.h`, add a new virtual alongside the existing one
   at line 76:
   ```cpp
   // Slot-aware inbox setter for a note consumer with more than one note
   // input (currently only Note Merge). Default forwards slot 0 to the
   // existing single-slot overload so every pre-existing consumer needs no
   // change.
   virtual void SetNoteInbox(int inputSlot, NoteEventQueue* inbox, int cursor)
   {
      if (inputSlot == 0)
         SetNoteInbox(inbox, cursor);
   }
   ```
   Leave the existing `virtual void SetNoteInbox(NoteEventQueue* inbox, int cursor)`
   (line 76) exactly as-is — every current consumer keeps overriding that one
   only.

2. In `src/main.cpp`, bump `const int kMaxNoteSlots = 2;` (line 2921) to `4`.
   This constant already drives every generic note-pin loop in the file
   (pin drawing, connection validation, teardown, etc. — grep
   `kMaxNoteSlots` to see all 8 use sites at lines 2921, 3185, 17203, 17290,
   17358, 17398, 17415, 17772, 33470). Most of those loops already iterate
   every slot and will pick up 4-input nodes for free. **Verify each site
   still does the right thing after the bump** — in particular the two
   described in step 3 below need actual code changes, not just the constant
   bump.

   4 slots mirrors `NoteRouterNode`'s 4-way fan-out for visual/behavioral
   symmetry and keeps the node a reasonable width. This is a judgment call,
   not a hard requirement — if you think a different count reads better once
   you see the node's body drawn, say so, but 4 is the recommended default.

3. In `src/main.cpp`'s `RebuildAudioTopology()`, two loops currently stop at
   the *first* connected note input per node rather than visiting all of
   them — both need to become full loops:

   - The outbox-reset pre-pass (`main.cpp:17393-17411`): the inner loop
     `for (int slot = 0; slot < kMaxNoteSlots && cable == nullptr; slot++)`
     stops at the first slot the node type exposes at all, so for a 4-input
     Merge node it only ever looks at slot 0. Change this to iterate every
     slot and reset every connected slot's producer outbox, not just the
     first.
   - The actual wiring loop (`main.cpp:17412-17434`) has the identical
     "stop at first" pattern and ends with `consumer->SetNoteInbox(inbox, cursor);`
     (the single-slot call). Change this to loop over every slot
     `0..kMaxNoteSlots`, and for each **connected** slot, look up its
     producer/outbox/cursor the same way the existing code does, then call
     the new per-slot `consumer->SetNoteInbox(slot, inbox, cursor);` from
     step 1. For a slot with no connection, either skip it or call
     `SetNoteInbox(slot, nullptr, -1)` — check what `AudioNoteMergeNode`'s
     `ProcessBlock` expects (see step 4) and match it; a null inbox pointer
     must be handled the same way every existing node already handles it
     (`(mInbox != nullptr) ? mInbox->Pop(...) : 0`).

   Every existing single-input node still only has slot 0 ever connected, so
   this change is behaviorally a no-op for all of them — confirm that by
   re-running the existing note-node sweep after this change (see the build
   step at the end).

## Step 2 — `NoteMergeNode` (main thread, `src/nodes/NoteNodes.h`)

Add a new class next to `NoteRouterNode` (`NoteNodes.h:471-501` is the
closest existing multi-slot note node, even though Router is fan-out and
Merge is fan-in — copy its shape, not its routing logic):

```cpp
class NoteMergeNode : public INode, public INoteSource
{
public:
   static constexpr int kSlots = 4;

   static INode* Create() { return new NoteMergeNode(); }
   NoteMergeNode();
   ~NoteMergeNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &noteInputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override;
   AudioNode* GetAudioNode() override;

   NoteCable noteInputs[kSlots];

private:
   std::unique_ptr<AudioNoteMergeNode> mAudioNode;
   int mLastCookFrame = -1;
};
```

`VisitParams` has nothing to persist — this node has no editable params, only
cables. `InputLabel` can return "1".."4" the same way `NoteRouterNode::OutputLabel`
does (`NoteNodes.cpp:1820-1824`). No `OutputCount()`/`OutputLabel()` override
needed — the default single-output behavior every other `INoteSource` gets is
correct here.

Add the forward declaration to the `class ...;` block near the top of
`NoteNodes.h` (alongside `AudioNoteRouterNode` etc., line ~25) for the audio
counterpart, `AudioNoteMergeNode`.

## Step 3 — `AudioNoteMergeNode` (audio thread, `src/nodes/NoteNodes.cpp`)

Mirror `AudioNoteFilterNode`'s shape (`NoteNodes.cpp:374-482`) but with 4
inboxes instead of 1:

```cpp
class AudioNoteMergeNode : public AudioNode
{
public:
   static constexpr int kSlots = NoteMergeNode::kSlots;

   void PrepareToPlay(double, int) override {}

   void ProcessBlock(const AudioBuffer* const*, int, AudioBuffer&) override
   {
      struct Tagged { NoteEvent evt; };
      Tagged all[kSlots * 64];
      int total = 0;

      for (int s = 0; s < kSlots; s++)
      {
         if (mInbox[s] == nullptr)
            continue;
         NoteEvent evts[64];
         const int n = mInbox[s]->Pop(mCursor[s], evts, 64);
         for (int i = 0; i < n && total < kSlots * 64; i++)
            all[total++].evt = evts[i];
      }

      std::stable_sort(all, all + total, [](const Tagged& a, const Tagged& b)
      {
         return a.evt.frameOffset < b.evt.frameOffset;
      });

      for (int i = 0; i < total; i++)
      {
         NoteEvent out = all[i].evt;
         out.source = this;
         mOutbox.Push(out);
      }
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   void SetNoteInbox(int inputSlot, NoteEventQueue* inbox, int cursor) override
   {
      if (inputSlot >= 0 && inputSlot < kSlots)
      {
         mInbox[inputSlot] = inbox;
         mCursor[inputSlot] = cursor;
      }
   }

private:
   NoteEventQueue mOutbox;
   NoteEventQueue* mInbox[kSlots] = {};
   int mCursor[kSlots] = { -1, -1, -1, -1 };
};
```

`kSlots * 64 = 256` matches `NoteEventQueue::kCapacity` exactly
(`src/audio/NoteEventQueue.h:49`) — worst case (all 4 inputs fully saturated
in one block) fits without overflow. Note this class does **not** override
the single-arg `SetNoteInbox(NoteEventQueue*, int)` — only the new per-slot
one from Step 1.

Add the usual `NoteMergeNode::NoteMergeNode()/~NoteMergeNode()/CookIfNeeded/
VisitParams/GetAudioNode/InputLabel` definitions after the class, following
`NoteFilterNode`'s definitions immediately below its class (`NoteNodes.cpp:484-521`)
as the template — `CookIfNeeded` just lazily constructs `mAudioNode` (there
are no params to push, so no `PushParams` call is needed, unlike
`NoteFilterNode`).

## Step 4 — wire it into `main.cpp`

1. **Registration** — add near the other `"Notes"` category registrations,
   e.g. right after `REGISTER_NODE(NoteRouterNode, Note Router, "Notes");`
   (`main.cpp:2526`):
   ```cpp
   REGISTER_NODE(NoteMergeNode, Note Merge, "Notes");
   ```

2. **Body/UI dispatch** — add a `DrawNoteMergeBody` function near
   `DrawNoteRouterBody` (`main.cpp:10125-10157`). There's nothing to edit
   (no params), so keep it minimal: `BeginAudioBody`/`EndAudioBody` with a
   status string showing how many inputs are currently connected, e.g.
   `"N active"` counting `n->noteInputs[i].IsConnected()`. Optionally reuse
   Router's four-dot indicator pattern (lit = connected) for visual
   consistency, but that's not required — a text status line is enough.
   Register the dispatch with the other `dynamic_cast` checks near
   `main.cpp:13686`:
   ```cpp
   else if (auto* n = dynamic_cast<NoteMergeNode*>(gn.node.get()))
      DrawNoteMergeBody(gn, n);
   ```

3. **Tooltip/help text** — add an entry to the node-help array near
   `main.cpp:16688` (same array as the `"Note Router"` entry), describing
   it plainly: up to 4 note inputs are merged into one output stream in
   timestamp order; each input's notes remain independent voices (matched by
   `voiceId`, not pitch), so two inputs playing the same note at once sound
   as two overlapping voices, not a collision.

4. **`CMakeLists.txt`** — no change needed. `NoteNodes.cpp`/`.h` are already
   in the build; this is a new class in an existing translation unit.

## Out of scope for this prompt

- `NoteStackNode` priority modes (highest/lowest/last) — separate follow-up,
  don't touch it here.
- Per-input transpose/velocity trim on Merge (Mixer's per-channel gain/pan
  equivalent) — not required for a correct first version; flag it as a
  possible future enhancement in a comment if you want, but don't build it
  now.
- Any change to `NoteCable` itself (it stays single-connection; Merge's
  multiple *slots* are what provide the fan-in, not a change to the cable
  type).

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean, not just that the edit looks right. Then:

- Manually wire 2-3 note sources (e.g. two `Note Sequencer` nodes with
  different patterns) into a Note Merge node, and Merge's single output into
  an Oscillator or Wavetable — confirm notes from both sources sound, that
  overlapping identical pitches from different sources both sustain and
  release correctly, and that disconnecting one input mid-playback doesn't
  affect the other's notes or leave anything stuck on.
- Run the existing note/audio node sweep (`audio-node-sweep` skill) to
  confirm the `kMaxNoteSlots` and `RebuildAudioTopology` changes didn't
  regress any of the ~20 existing single-input note nodes — this is exactly
  what `AUDIOPARAMSWEEPTEST`/`AUDIOTEARDOWNSWEEPTEST` check (param
  round-trip and mid-playback add/wire/delete safety).
