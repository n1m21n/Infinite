# P3a — Notes: infrastructure + first nodes

Re-derived against the finished P2.8 routing layer (per-node pooled output
buffers, the `AudioTopology` DAG, `ProcessBlock`'s "read inputs, write your
output" contract, Mixer/Splitter). Every line number below was verified
against the working tree on the date this was written; the tree at that point
built clean, passed hygiene 39/40, and printed `DSPTEST OK` at 11 checks.

## Scope warning — read before planning your session

`docs/plans/audio/README.md` §4 lists P3a as "7 nodes + Envelope". **Do not
attempt all eight in one session.** The note *transport layer does not exist
yet* — the note cable is pure scaffolding, no node has ever had a note pin,
and `AudioNode` has no channel through which a note event can reach a synth on
the audio thread. Building that plumbing is most of the work; the nodes are
comparatively easy once it exists.

**This prompt covers Part 1: the note event infrastructure, plus the three
nodes needed to prove it end to end** (Note Sequencer, Envelope, and a note
input on the existing Oscillator). Note Filter / Note Modify / Arpeggiator /
Note Echo / Note Router / Note Display are **Part 2**, a separate session
against the finished infrastructure. Say in your summary that Part 2 is
outstanding.

## Confirmed current state — verified this session, do not re-derive

### The note cable is scaffolding that nothing uses

- `src/core/NoteCable.h` exists and mirrors `AudioCable` exactly. Its own
  comment says "no P2 node has a note pin yet, this exists so P3a's note
  nodes have the scaffolding already in place."
- `INode::NoteInputSlot(int)` (`src/core/INode.h:136`) returns `nullptr` and
  **is never overridden by any node in the codebase.** Grep confirms zero
  implementations — every other `NoteInputSlot` hit is a *caller* in
  `main.cpp`.
- **There is no `INoteSource` marker interface.** `IAudioSource`
  (`src/core/INode.h:21-26`) exists and is the pattern to mirror, but its note
  equivalent was never written. Grep for `INoteSource` returns nothing.
- Consequently `srcIsNoteSource` is **hardcoded `false` at three separate call
  sites**: `main.cpp:1852`, `:14445`, `:15500`, each with the comment "no P2
  node produces notes yet". All three must become a real
  `dynamic_cast<INoteSource*>` check. They are, in order: node-type
  recommendation for drag-to-empty-canvas (`RecommendedNodeTypesForOutput`),
  live link-drag validation and its refusal messages, and link-drag-to-spawn.
  Missing one means note links silently refuse in that one code path —
  exactly the class of bug that cost the user time when audio pins shipped
  unlabelled.
- Connection *rules* are already correct and generic once the flag is real:
  `IsInputSlotCompatible` (`main.cpp:1609`, note branch at `:1639-1641`)
  returns `srcIsNoteSource` for any pin whose `NoteInputSlot(slot)` is
  non-null. Note-cable link tinting already exists (green,
  `ImColor(90,220,130)`).
- Editor-side note wiring is *already threaded through* the non-audio-thread
  sites: `ConnectSlot`-style connect (`main.cpp:1751`), disconnect
  (`main.cpp:5789`), `DisconnectAllTo`'s sweep (`main.cpp:6349`), save/load
  (`main.cpp:6574`, `:6816`) and link drawing (`main.cpp:14250`) all already
  call `NoteInputSlot`. **You are not starting from zero on the editor side** —
  the gap is the audio thread and the `INoteSource` marker.

### P2.8 landed — the topology now carries real edges

This is the biggest change from the previous draft of this prompt, which was
written when the engine ran a flat list over one shared buffer. That is no
longer true; do not plan against it.

- `AudioNode::ProcessBlock` is now
  `ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output)`
  (`src/audio/AudioNode.h:26`). Nodes read their input buffers and write their
  own output buffer. `inputs[i]` is null when pin `i` is unconnected — treat
  as silence, still write the output.
- `AudioTopologyEntry` (`src/audio/AudioEngine.h:27-33`) carries
  `inputBufferIndices[kAudioMaxNodeInputs]`, `numInputs`, and
  `outputBufferIndex` — indices into the topology's pooled buffer set.
  `AudioTopology` (`:41-46`) is `order` + `terminalBufferIndices` +
  `numBuffers`. Ceilings: `kAudioMaxNodeInputs = 8`,
  `kAudioMaxBlockFrames = 4096`, `kAudioMaxChannels = 8` (`:19-21`).
- `AudioEngine::RunTopology` (`src/audio/AudioEngine.cpp:90-134`) walks `order`
  front-to-back — **it does not sort**, the builder's ordering is load-bearing
  — hands each node its input views and its own output view, then sums
  `terminalBufferIndices` into the device buffer.
- `CollectAudioChain` (`main.cpp:6381-6411`) is the DFS that builds it: it
  recurses through `AudioInputSlot(slot)` before pushing the node's own entry,
  so sources land before consumers. `RebuildAudioTopology`
  (`main.cpp:6426-6461`) seeds it from every `AudioOutputNode`'s connected
  input.
- Mixer and Splitter exist: `MixerNode` (`src/nodes/AudioNodes.h:123`,
  `kSlots = 8`, per-slot `gainDb`) and `SplitterNode` (`:170`,
  `kMaxFanout = 4`). Rule 1 fan-out refusal is `AudioSourceHasFreeOutput`
  (`main.cpp:1822`), which special-cases Splitter.

### The note routing gap — read this before designing anything

Two concrete consequences of the above that you must solve. Both are real; I
confirmed each by reading the code, not by inference.

**1. A note-only node never gets a topology entry at all.**
`CollectAudioChain` pushes an entry only inside
`if (auto* src = dynamic_cast<IAudioSource*>(node))` (`main.cpp:6403`). A Note
Sequencer produces notes, not audio — it is not an `IAudioSource` — so as
things stand it is invisible to the audio thread and can never run there.
But it *must* run there: sample-accurate step timing is the entire reason P2.5
made `Transport` sample-accurate before this phase.

**2. `RunTopology` will index out of bounds on a node with no audio output.**
`AudioEngine.cpp:123` does `list->buffers[entry.outputBufferIndex]`
unconditionally. `AudioTopologyEntry::outputBufferIndex` already defaults to
`-1`, so the struct can already *express* "no audio output" — but the engine
cannot yet execute it.

**My recommendation, which you own the final call on:** let note-producing
nodes take an ordinary entry in `order` with `outputBufferIndex = -1`, and
guard `RunTopology` to pass a zero-frame/null output for those. This keeps one
ordering mechanism, one retire discipline, and one place where nodes get
ticked, rather than a second parallel "note graph" execution path that would
have to be kept in sync with this one forever. It also makes the ordering
guarantee fall out for free: a sequencer collected before its synth is
*already* ordered correctly by the existing DFS post-order. State what you
chose and why.

**Also note the seeding limitation:** `RebuildAudioTopology` only walks
backward from `AudioOutputNode`s. A note chain that terminates at a synth is
reachable (Audio Out → synth → its note pin → sequencer), *provided you extend
`CollectAudioChain` to recurse through `NoteInputSlot` as well as
`AudioInputSlot`*. A pure note terminal with no audio path — Note Display — is
**not** reachable and will need a second seed loop. That node is Part 2, so
you do not have to solve it now, but leave a comment saying so rather than
letting Part 2 rediscover it.

### `ParamMailbox` is the wrong tool for notes, deliberately

`src/audio/ParamMailbox.h` is a "latest value wins" flat
`std::atomic<float>[kMaxParams=64]` array — it *drops* intermediate values by
design, which is correct for a continuously-swept knob and catastrophic for
note-on/note-off pairs (a dropped note-off is a stuck note). **Write a
separate SPSC ring for notes; do not extend `ParamMailbox`.** Use
`src/audio/MeterRing.cpp` as the reference for a correct SPSC ring in this
codebase (producer writes only `mTail`, consumer writes only `mHead` — and
note that `ParamMailbox`'s original ring was replaced precisely because it
violated that invariant).

### The voice machinery already exists and is unused

`src/audio/AudioVoice.h` provides a complete `Envelope` (ADSR, linear
segments, `NoteOn`/`NoteOff`/`Process`/`IsActive`) and `VoiceAllocator`
(round-robin with oldest-steal, `std::vector<Voice>` sized once at
construction, never resized). Both were written RT-safe for exactly this
phase. **Use them; do not write a second envelope.**

### Transport is sample-accurate and safe to read from the audio thread

`Transport::Beats()` / `Seconds()` compute from the audio sample counter
whenever the engine is running, and `AudioEngine::Process` calls
`AdvanceAudioClock(numFrames)` once per block before any node runs. A
sequencer inside `ProcessBlock` can therefore compute the exact sample offset
of a step boundary. **This is what makes block-offset scheduling possible at
all.**

### The Oscillator is free-running

`OscillatorNode` (`src/nodes/AudioNodes.h:22`) has a `frequency` param and no
note input — it plays a continuous tone. It has `glide`, `velToVolume` and
`velToFilter` declared and deliberately unread, with a comment in
`src/nodes/AudioNodes.cpp` saying they wait on P3a's note cable. **This phase
is what makes them live.**

### Category colours are already in place

`"Notes"` exists in all five presets in `src/core/CategoryColors.cpp`. No
colour work needed.

## The live-MIDI constraint — a real limitation, decide explicitly

The existing MIDI layer is **polled and value-based, not a timestamped event
stream.** `src/platform/Platform.h` offers `MidiRead` (current value for a
binding), `MidiNoteHitCount` (a monotonic counter per note), and
`MidiChannelLastNote` (most recent note on a channel, with a `hitSeq`). There
is no queue of `(note, velocity, timestamp)` events, and `MidiCCNode` /
`MidiTriggerNode` (`src/nodes/MidiNodes.h`) consume it by polling once per
frame in `CookIfNeeded`.

Consequence, stated plainly: **an internally generated sequence can be
sample-accurate; live MIDI input cannot be, without new CoreMIDI work.**
Polling at ~60 fps quantises live input to ~16 ms, and fast passages can drop
notes entirely between polls (the counter tells you *that* hits happened, not
when or how many distinct ones in order).

**Recommendation: scope this phase to internally generated notes
(sequencer → synth), and leave live-MIDI-to-note-cable for a follow-up** that
adds a proper timestamped SPSC event queue filled from the CoreMIDI read
callback. The user does want MIDI In as a note source eventually — it is named
explicitly in `audio-graph-semantics.md` §1's "sequencer + keyboard + MIDI In"
example — so design the event struct so that source can be added without
changing it. Attempting both here roughly doubles the phase and mixes a
Platform-layer rewrite into what is otherwise node work. If you disagree, say
so explicitly rather than quietly doing one or the other.

## What to build

### 1. The note event transport (design this first, write it down)

Decide and document these in a short block comment at the top of the new
header, since P3a-Part-2 and P3b both depend on it:

- **The event struct.** At minimum `note` (0-127), `velocity` (0..1),
  `isNoteOn`, and `frameOffset` (sample offset within the current block — the
  "block-offset scheduling" the plan asks for). **It must also carry a source
  identifier.** This is non-negotiable and comes from
  `audio-graph-semantics.md` §4: note pins accept multiple cables and merge
  implicitly, so **note-off must match on `(source, pitch)`, not pitch
  alone** — otherwise two sources playing the same pitch produce a stuck note.
  Retrofitting that field later means touching every producer and consumer.
- **The queue.** A fixed-capacity SPSC ring, RT-safe on both ends, following
  `MeterRing`'s index discipline. Choose a capacity and justify it in a
  comment. **Decide and document the overflow policy** — dropping a note-off
  causes a stuck note, so an overflow that drops must at minimum be counted
  and surfaced, the way `AudioEngine::XrunCount()` already surfaces dropouts.
- **How events reach the consuming synth.** Give each note-consuming
  `AudioNode` its own inbound queue that its upstream writes into directly.
  This is now clearly the right shape rather than a toss-up: P2.8's topology
  already models producer→consumer edges explicitly, so the note graph should
  use the same model rather than introducing a global bus that Note Router
  (Part 2) would have to work around. It also makes multi-cable fan-in fall
  out naturally — N producers writing into one consumer's queue *is* the
  implicit merge Rule 2 specifies. State this in the header comment.
- **RT-safety is non-negotiable** on the audio-thread side: no allocation, no
  locks, no `std::function`, per `src/audio/AudioNode.h`'s constraint list.

Extend `CollectAudioChain` (`main.cpp:6381`) to recurse through
`NoteInputSlot` the same generic way it already walks `AudioInputSlot`, so a
node added next month can't be forgotten. Note fan-in means a note pin may
have **more than one** source — the existing `AudioCable` one-source model
does not cover this, so `NoteCable` (or the pin's storage) needs to hold a
small fixed-capacity set of sources, and `main.cpp`'s connect/disconnect/
save-load/link-draw sites listed above all need to handle N rather than 1.
Budget real time for this; it is more editor work than the audio side.

Also handle the merge cases `audio-graph-semantics.md` §4 flags:
- Same source, same pitch, two note-ons without an intervening off. Retrigger
  the existing voice or steal a new one? Pick and document.
- Voice budget across merged sources — the existing oldest-steal policy
  handles it, but say so explicitly rather than discovering it as a bug.
- **A source deleted mid-playback while holding notes down.** Those voices
  must be released or they stick forever. This is the note-graph analogue of
  the use-after-free that `DisconnectAllTo` and `AUDIOGRAPHTEST` exist to
  prevent, and it needs equivalent test coverage.

### 2. Editor plumbing for note pins

- Add `INoteSource` to `src/core/INode.h`, mirroring `IAudioSource` (`:21-26`)
  including the explanatory comment about why the marker exists.
- Replace all three hardcoded `srcIsNoteSource = false` sites
  (`main.cpp:1852`, `:14445`, `:15500`) with a real `dynamic_cast`.
- **Rule 1 applies to note outputs too**, per `audio-graph-semantics.md` §1: a
  note output feeds exactly one destination, and Note Router (Part 2) is the
  fan-out point. `AudioSourceHasFreeOutput` (`main.cpp:1822`) is the pattern —
  write the note equivalent now, with the Note Router exemption stubbed, or
  Part 2 will have to retrofit refusal into three call sites again. Its
  refusal message per §7: "One connection per output — use a Note Router".
- Every note pin must carry an `InputLabel` (e.g. `"notes"`). Non-optional:
  unlabelled audio pins are precisely why the user could not work out how to
  connect Oscillator → Gain. `MixerNode::InputLabel`
  (`src/nodes/AudioNodes.h:143`) is the multi-slot pattern.
- `InputCountFor` (`main.cpp:1330`) needs entries for the new nodes.
- **Note-only nodes will currently fall through to the generic *visual* node
  path — this is a confirmed gap, not a maybe.** Both
  `CanShowInViewportPanel` (`main.cpp:5299`, gate at `:5323`) and
  `IsAudioBodyNode` (`main.cpp:3230-3234`) test
  `dynamic_cast<IAudioSource*>(n) != nullptr || n->AudioInputSlot(0) != nullptr`.
  A Note Sequencer satisfies neither, so it would get a `DrawPreview` body and
  a viewport-panel card showing a blank box. Fix **both** — they carry a
  comment saying they must never disagree — by widening the test to include
  note pins and `INoteSource`. Widen it generically, not by node type.
- Follow `docs/plans/audio/audio-node-ui-system.md` for node body layout, the
  compact-grid/expand grammar, and the visualizer catalogue (§3 already
  assigns Envelope an ADSR-shape visualizer and note nodes a stat line).

### 3. The three proving nodes

- **Note Sequencer** (category `"Notes"`). Per README §3: `pattern` = Grid /
  Euclidean / Polyrhythm, one node, differing only in the step-selection rule.
  Steps derive their timing from `Transport::Beats()` so they land on exact
  sample offsets. The record-arm button described in §3 is **Part 2** — do not
  build it here.
- **Envelope** (category `"Modulators"`, per README §3's modulator table).
  Note-triggered ADSR built on the existing `Envelope` class. It is also an
  `IModulator` so it can drive *visual* params — that dual role is called out
  in the plan as a selling point ("the flash the visuals on a note" node), so
  make sure `Value01()` works.
- **A note input on `OscillatorNode`.** Give it a note input slot and make it
  voice-allocated via the existing `VoiceAllocator`. This is where `glide` and
  `velToVolume` become live. **`velToFilter` has no filter to drive** (the
  Oscillator deliberately has no filter — see `audio-node-ui-system.md` §8);
  either leave it inert with its existing comment updated, or remove it, but
  do not invent an oscillator filter to justify it.

Watch the voice-count multiplier: `kMaxUnison = 8` per voice already, times
the allocator's voice count. Keep everything preallocated and say in your
summary what the worst-case oscillator count per block is.

## Explicitly out of scope

- Note Filter, Note Modify, Arpeggiator, Note Echo, Note Router, Note Display
  — Part 2. (Leave the Note Router fan-out exemption stubbed, as above.)
- Live MIDI into the note cable, and any CoreMIDI/Platform rewrite — see the
  constraint section above.
- The Note Sequencer's record-arm/recording mode.
- Any Synth node beyond the Oscillator note input (P3b), any Effect (P3c).
- Touching `MidiCCNode`/`MidiTriggerNode`, which are existing working
  modulator nodes unrelated to this cable.
- Modulator → audio param. Still deferred per `cook-rate-decision.md`; the
  Envelope's `IModulator` role is modulator→**visual**, which already works.

## Clean-room constraint — still binding

**Do not read, open, grep, or reference `/Users/namansoni/BespokeSynth`** or
any other GPLv3 source. Node taxonomy and param naming are freely reusable;
DSP and scheduling logic must be written from primary literature or first
principles, as every prior phase has done.

## Build & verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

Baseline is **39/40 passing** (verified on the tree this prompt was written
against). The only expected failure is `PHASEATEST` (a pre-existing, unrelated
`"Smooth"` node-name collision the user fixed elsewhere — **do not fix it, do
not report it as a regression**). `DRAGTEST` printing "... : BUG" in its
canvas-pan sub-check is also a known baseline, not yours. Watch
`ROUNDTRIPTEST` and `PATCHTEST` (new node types must round-trip, and a note
pin holding N sources must save/load correctly) and `AUDIOGRAPHTEST` (you are
changing the audio graph's lifetime surface — deleting a sequencer
mid-playback must not leave a synth holding a dangling pointer; extend that
fixture to cover a note connection).

```bash
INFINITE_DSPTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite
```

Currently **11 checks**, must still end `DSPTEST OK`. **Add note-scheduling
coverage here** — this is the phase where the plan's §4 test list becomes
concrete. At minimum:

- a note-on scheduled at a known sample offset takes effect at exactly that
  sample;
- ADSR segment durations match their param values within tolerance;
- a note-off during release does not restart the envelope;
- voice-steal picks the oldest voice when all are busy;
- **two sources sending the same pitch, one sending note-off, leaves the other
  still sounding** — this is the `(source, pitch)` matching rule from §4 and
  is the single most important new test in this phase.

Note that P2.8 added Mixer and Splitter without DSPTEST coverage of their own
(the 11 checks are all oscillator/gain/filter). Not your job to backfill, but
don't assume summing is proven by the suite.

Finally, launch the app and confirm by ear and eye: Note Sequencer →
Oscillator → Gain → Audio Out plays an actual rhythmic sequence, two
sequencers into one Oscillator's note pin merge correctly, the Envelope node's
visualizer draws its ADSR shape, note links tint green, and every note pin is
labelled.
