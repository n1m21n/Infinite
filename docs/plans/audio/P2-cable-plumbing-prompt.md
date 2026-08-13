# P2 — Audio/Note cable types through the node editor

Goal: introduce two new cable types — **Audio** and **Note** — through
every wiring site the visual/geometry/modulator cable types already go
through, proven with exactly three minimal real `INode` types
(`Oscillator`, `Gain`, `Audio Out`) wired to the `src/audio/` engine P1
already built and verified. Per the plan, keep the test surface to these
three nodes deliberately — this is "the mechanical, error-prone phase," and
a small node count makes a missed touch-point obvious instead of buried in
noise.

**Clean-room rule, verbatim:** do not open, read, grep, or reference
`/Users/namansoni/BespokeSynth`.

**Everything below was verified against the actual code just before this
session, not assumed from the plan doc.** Every file/line reference is
current as of `git log` HEAD `d661657` plus the uncommitted P0/P1 audio work
already in the tree.

## What already exists — P1's audio engine, confirmed working

`src/audio/` has `AudioEngine`, `AudioNode`/`AudioBuffer`, `ParamMailbox`
(flat atomic-array design, not a ring — see its header comment for why),
`MeterRing`, `DspMath` (PolyBLEP osc, TPT SVF, RBJ biquads, one-pole,
dB↔lin, equal-power pan, fast tanh), `AudioVoice` (ADSR + allocator).
`INFINITE_DSPTEST` passes clean, including a real filter sweep:
```
DSPTEST peak amplitude: expected 0.500  got 0.5000  OK
DSPTEST zero-crossing freq: expected 440.0 Hz  got 435.94 Hz  OK
DSPTEST gain smoothing: |delta| = 0.0253 (instant jump would be ~0.4)  OK
DSPTEST meter ring: expected 12 entries  got 12  OK
DSPTEST filter passband: expected >0.700  got 1.0000  OK
DSPTEST filter sweep attenuation: expected <0.300  got 0.0741  OK
DSPTEST filter meter ring: expected 40 entries  got 40  OK
```
The DSPTEST fixture (`main.cpp:6616-` region, `namespace DspTest`) has
hardcoded, non-`INode` `SineOscNode`/`GainNode`/`FilterNode` classes used
only by that headless test. **This session builds separate, real `INode`
subclasses** — don't try to reuse or relocate the `DspTest` classes, they
stay exactly as they are (a different, headless test path this phase
doesn't touch). The docs already written and worth reading before you
start: `docs/plans/audio/README.md` (the overall plan, §3 for node
categories and §4 P2's own bullet list), `docs/plans/audio/P1a-engine-prompt.md`
and `P1b-dspmath-voice-prompt.md` (what P1 actually built),
`docs/plans/audio/cook-rate-decision.md` (the audio/visual cook-rate
decision — not yet relevant to this session since there's no visual
modulator wired to an audio param here, but read it so you don't
accidentally reinvent a different answer to the same question).

## The two-object rule applies for real now

Each of the three new node types is **two objects**: an `INode` (main
thread — UI, params, save/load, pins) that owns an `AudioNode` (audio
thread — `ProcessBlock` only), communicating only through `ParamMailbox`
and `MeterRing`, per the rule stated in both P1 prompts. `CookIfNeeded` on
each of these three `INode`s does **no DSP** — it drains the node's
`MeterRing` (if it has one) and that's it; the actual sample generation
happens in `AudioEngine`'s real-time callback via `ProcessBlock`, called
from the topology `AudioEngine::SetTopology` was given, not from
`CookIfNeeded`.

## 1. Three new `INode` types — `src/nodes/AudioNodes.h`/`.cpp`

New files (follow the existing `src/nodes/*.h`+`.cpp` pair convention, e.g.
`ShapeNode.h`/`.cpp`).

```cpp
class OscillatorNode : public INode
{
public:
   // main-thread state: waveform choice, base frequency
   // owns a DspMath::PolyBlepOsc-backed AudioNode (a private nested class
   // or a separate AudioOscillatorNode in the same file - your call, but
   // keep the audio-thread class out of INode's own header so nothing on
   // the main-thread side accidentally reaches for AudioNode's private
   // real-time state directly)
   unsigned int GetOutputTexture() override { return 0; } // no image output
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override; // no DSP - see above
   void VisitParams(ParamVisitor& v) override; // waveform, frequency
   AudioNode* GetAudioNode(); // for SetTopology to find - see step 6
};

class GainNode : public INode { /* same shape, one audio input, gain param */ };

class AudioOutputNode : public INode { /* one audio input, terminal - see step 6 for what "terminal" means for topology */ };
```

`GetOutputTexture()`/`GetOutputWidth()`/`GetOutputHeight()` returning
`0`/`0`/`0` matches the existing convention for non-image nodes — grep
`AudioFileNode` (`src/nodes/AnalyzeNodes.h:98-100`) or `MathNode`
(a modulator, same shape) for a node that already does this; don't invent
a different "no image" convention.

**Judgment call, resolved:** name these `Oscillator`/`Gain`/`Audio Out`
exactly as the plan's P2 section says — check
`NodeFactory::Instance()`'s registry for collisions first
(`grep -n 'REGISTER_NODE' src/main.cpp` and check the display names used)
since the plan's own §3 name-collision table doesn't cover these three and
a fresh check is cheap.

## 2. `AudioCable`/`NoteCable` — `src/core/AudioCable.h`/`NoteCable.h`

Mirror `src/core/ImageCable.h` exactly (`Connect`/`Disconnect`/
`GetSource`/`IsConnected`), but drop the texture-specific parts
(`Resolved()`'s bypass-walk, `Pull()`, `Width()`/`Height()`,
`TextureRevision()` reads) — an audio/note cable in the *editor* is just a
typed "who feeds this pin" record for save/load and UI, matching what
`ImageCable` is at its core. **It is not the real-time signal path** — the
real-time path is `AudioEngine`'s flattened `ProcessList` built by
`SetTopology` (step 6). Keep bypass-walking out of `AudioCable`/`NoteCable`
for this session: none of the three new node types has a `BypassSource()`
override yet, and adding bypass semantics for a signal type P2 is still
proving out is scope creep — leave `INode::bypassed`/`BypassSource()`
at their existing defaults for these three nodes.

## 3. `InputCountFor`/`CableFor`/`AudioCableFor`/`NoteCableFor` (`main.cpp:1304-1467`)

Input pins for every node type share **one flat numbering** — `GraphNode`'s
`InputPinId(slot)` covers image, geometry, and modulator inputs alike in
the same `slot` range (`GraphNode.h:44`, "+1..+99 image input pins" — the
comment is stale in calling them "image" pins specifically, since geometry
and modulator inputs already share the same range via `GeometryInputSlot`/
`ModulatorInputSlot`; audio/note pins are one more accessor tried against
that same `slot` number, not a new numeric range). Concretely:

- `InputCountFor` (`main.cpp:1304-1397`): add `Oscillator` (0 inputs — it's
  a source), `Gain` (1), `Audio Out` (1) as more `dynamic_cast` branches,
  matching the existing style exactly (see e.g. `FeedbackNode` at
  `:1388-1389` for the one-input shape).
- `AudioCableFor(GraphNode&, int slot)` — new function beside `CableFor`
  (`:1399-1467`), same dispatch style: `dynamic_cast` to `GainNode`/
  `AudioOutputNode`, return the node's `AudioCable&` member for the
  matching slot.
- `NoteCableFor(GraphNode&, int slot)` — same shape, unused by any of the
  three P2 nodes (none of them has a note pin), but must exist per the
  plan's own list ("an AudioCableFor/NoteCableFor beside CableFor") since
  P3a's note nodes land on this scaffolding directly. An empty-but-correct
  function (returns `nullptr` for everything, since nothing has a note pin
  yet) is the right amount of work here — don't invent a note-consuming
  node to justify it.

## 4. `ConnectGeometrySlot`/`IsInputSlotCompatible`/`WireInputSlot` (`main.cpp:1479-1664`)

These three already dispatch on `slot` through a chain of `dynamic_cast`s
in a fixed order (geometry pins first via the generic
`GeometryInputSlot()`, then a handful of special-cased node types, then
image cables last as the default). Add the audio/note branches to the
**same chain**, in the same relative position other typed-pin checks sit
(image is deliberately last as the fallback in all three — mirror that,
don't reorder it):

- `IsInputSlotCompatible` (`:1533-1596`): needs two more source-side facts
  passed in — `bool srcIsAudioNode` (`dynamic_cast<AudioNode-owning
  marker...>` — see the note on a marker interface below) and
  `bool srcIsNoteSource` — alongside the existing `srcIsModulator`/
  `srcPalette`/`srcGeometry`/`srcCamera`/`srcLight`/`srcAudioFile`/
  `srcIsEnvironment` parameter list. **This function has four call sites**
  (`main.cpp:1706`, `:13454`, `:14437`, and the `RecommendedNodeTypesForOutput`
  probe loop at `:1676-1718` which also needs the new source facts computed
  for whatever probe/source it's checking) — update the signature and every
  call site together, in one pass, so it doesn't compile with a stale
  4-of-6 call site.
- **Reject rules to encode here** (per `docs/plans/audio/README.md`'s P2
  section, "Connection rules to enforce"): audio source → non-audio,
  non-param pin is rejected (no coercion to an Envelope Follower — that
  node doesn't exist yet, plain rejection is correct for this phase);
  audio ↔ image rejected; note ↔ audio rejected; geometry → audio
  rejected; a *visual* modulator → an audio input pin rejected (the
  cook-rate decision doc explains why: a visual modulator's value only
  ever reaches an audio node through the published-atomic mechanism it
  describes, never through a direct cable — that mechanism doesn't exist
  yet, so for now the pin-compatibility check simply says no). None of
  these need new machinery beyond what `IsInputSlotCompatible` already
  does — they're `return false` branches keyed on the same source-fact
  booleans every other rejection already uses.
- **Marker for "this INode owns an AudioNode":** rather than
  `dynamic_cast<OscillatorNode*>(src) || dynamic_cast<GainNode*>(src)`
  (which grows one more `||` per audio node type forever), add a small
  mix-in interface next to `IModulator`/`IGeometrySource` in `INode.h`:
  ```cpp
  class IAudioSource { public: virtual ~IAudioSource() {} virtual AudioNode* GetAudioNode() = 0; };
  ```
  and have `OscillatorNode`/`GainNode` (not `AudioOutputNode` — it's a
  sink, not a source; see step 6) inherit it alongside `INode`, matching
  exactly how `IGeometrySource` already sits beside `INode` on geometry
  nodes. Then `srcIsAudioNode` is one `dynamic_cast<IAudioSource*>(src) !=
  nullptr` check, and step 6's topology-builder can find every audio node
  in the graph the same generic way `DisconnectAllTo` already finds every
  geometry-consuming node via `GeometryInputSlot` (step 7 mirrors this for
  audio/note).
- **Cycle rejection** (README: "any cycle in the audio graph... visual
  graph allows cycles via FeedbackNode; audio must not, or the topological
  sort deadlocks"): before accepting a new audio→audio connection in the
  `QueryNewLink` handler (`main.cpp:13416-13488`, specifically inside the
  `if (valid && ed::AcceptNewItem())` branch, before calling
  `WireInputSlot`), walk forward from the candidate destination node
  through its own audio output consumers (a small DFS/BFS over
  `IAudioSource`-owning nodes using `AudioCableFor` to find who each node
  feeds) and reject if it reaches back to the candidate source. Three
  nodes in a line can't actually form a cycle, so this won't fire in this
  session's own test graph — write it anyway, since P3 (real synths with
  feedback-shaped effects like Delay) is exactly where a missing check
  here would silently corrupt `AudioEngine::SetTopology`'s ordering
  instead of failing loudly at connect time.

## 5. `QueryNewLink` accept/reject + pin colors (`main.cpp:13416-13488` region, and link tinting at `:13407-13414`)

- Wire the new `srcIsAudioNode`/`srcIsNoteSource` facts into the existing
  `valid = IsInputSlotCompatible(...)` call at `:13454` (this is the same
  edit as updating the call site in step 4 — do it once).
- **Pin/link tinting**, per the plan's colour scheme (Audio = blue, Note =
  green): the existing link-drawing loop (`:13407-13414`) only
  special-cases param-pin links (orange, via `GraphNode::IsParamPin`).
  Add: for a link whose destination slot resolves through `AudioCableFor`
  (not `CableFor`) on the destination node, draw it in the plan's blue;
  through `NoteCableFor`, green. This needs the destination `GraphNode*`
  and slot resolved from `link.dstPin` the same way the existing param-pin
  check does (`GraphNode::NodeIndexFromPin`/`InputSlotFromPin`) — you'll
  need to look up the destination node from `gNodes` inside this loop,
  which the current param-pin branch doesn't need to do (it only inspects
  the pin id, not the node); do that lookup once per link, not per frame
  redundantly if you can hoist it, but don't over-optimize this before
  confirming it's even measurable.
- imgui-node-editor pin colors (the little dot at each unconnected pin, as
  opposed to the link line) are a separate call from link tinting —
  `grep -n "PinColor\|ed::PinRect\|BeginPin" main.cpp` before assuming a
  specific API name; if the exact pin-coloring call isn't obviously
  present already (this codebase may only tint links, not pin dots — treat
  this as unconfirmed, verify at implementation time), it's fine to ship
  audio/note support with link tinting only and pin dots left at their
  current default color; note that explicitly rather than silently
  matching only half the plan's ask.

## 6. `AudioEngine::SetTopology` integration — building the real-time graph from the editor graph

Nothing built this yet — P1's `AudioEngine` takes a `std::vector<AudioNode*>`
directly; nothing translates the node-editor's cable graph into that list.
This session needs the minimum version: whenever the audio-relevant part of
the graph changes (a connect, disconnect, or delete touching an
`IAudioSource`/audio-input node), rebuild the topology and call
`AudioEngine::Instance().SetTopology(...)`.

**Design decision, made now so you don't have to invent one:** flatten via
a simple DFS from every `AudioOutputNode` in the graph, walking backward
through each node's audio input cable to its source, collecting
`AudioNode*` pointers in **reverse-of-discovery** order (so a source is
processed before whatever consumes it — `AudioEngine::Process` runs the
list front-to-back over one shared scratch buffer, per P1's design, so
order matters). `AudioOutputNode` itself needs an `AudioNode`-side
counterpart too (something that, in `ProcessBlock`, is the terminal —
either a no-op passthrough the engine's real device callback reads from
directly, or literally nothing since the engine's scratch buffer already
holds the final samples once every node up the chain has run in order).
Decide which and say so in a comment at the topology-builder's call site —
this is exactly the kind of thing a P3 session extending this to a real
mixing graph (multiple sources into one output) will need to know was a
deliberate choice, not an oversight.

Trigger points to rebuild from: the `WireInputSlot`/disconnect paths
touched in steps 4/7, and node spawn/delete. Simplest correct approach for
this session: rebuild the whole topology from scratch every time any of
these happen (three-node graphs are cheap to walk fully; don't build
incremental diffing yet — that's premature for a graph this small, and
`SetTopology`'s one-generation-retire design already makes a full rebuild
cheap on the main thread).

## 7. `DisconnectLinkById`/`DisconnectAllTo`/`RemoveNodeByIndex` — do this one generically

`DisconnectAllTo` (`main.cpp:5916-5977`) already has the exact hazard this
step exists to avoid, documented in its own comment
(`:5940-5942`, "whatever it calls its field(s) internally... found
generically... so a new node type can't forget to register here and leave
a pointer to a freed node that crashes on the next cook"). Follow that
same generic pattern instead of adding one more hand-maintained
`dynamic_cast` ladder:

- Add `virtual AudioCable* AudioInputSlot(int slot) { return nullptr; }`
  and `virtual NoteCable* NoteInputSlot(int slot) { return nullptr; }` to
  `INode.h`, mirroring `GeometryInputSlot`/`ModulatorInputSlot`
  (`INode.h:109,112`) exactly in shape and placement.
- `GainNode`/`AudioOutputNode` override `AudioInputSlot` to return the
  address of their `AudioCable` member (matching how `GeometryInputSlot`
  hands back `IGeometrySource**` — here it's a pointer to the cable object
  itself, since `AudioCable` isn't a raw pointer type the way geometry
  slots are; pick whichever of "return `AudioCable*`" vs "return
  `AudioCable**`" is more consistent with the rest of `INode.h`'s existing
  slot accessors and say which you picked).
- `DisconnectAllTo` (`:5916-5977`): add a generic loop over
  `kMaxGeometrySlots`-shaped bound (reuse or add a `kMaxAudioSlots`/
  `kMaxNoteSlots` constant near `:1473`) calling `AudioInputSlot`/
  `NoteInputSlot` and clearing/disconnecting if the slot's source matches
  `dying`, the same shape as the existing geometry loop at `:5943-5951`.
- `RemoveNodeByIndex` (`:5979-`) already calls `DisconnectAllTo` — no
  separate change needed there once the above lands, same as it already
  covers geometry generically.
- `DisconnectLinkById` (`main.cpp:5372`, read it before assuming its shape
  — it's a different code path than `DisconnectAllTo`, keyed by link id
  rather than by dying node) needs its own audio/note branch if it
  currently only handles image cables and modulator/palette bindings by
  type-switching on the link's pin kind; read it and match whatever
  pattern it already uses for geometry/modulator disconnection there.
- **After this lands, also rebuild the audio topology (step 6)** on every
  path that calls `DisconnectAllTo`/`RemoveNodeByIndex`/`DisconnectLinkById`
  for an audio-relevant node/link — a stale `AudioEngine::ProcessList`
  holding a pointer to a just-deleted node's `AudioNode` is exactly the
  "pointer to a freed node... crashes on the next cook" bug this generic
  pattern exists to prevent, just on the audio thread instead of the
  render thread, which makes it worse (a crash *there* takes the whole
  process with it, not just a black texture).

## 8. `CategoryColors` — new categories, and one naming collision to avoid

`CategoryColors.cpp` already has an `"Effects"` category (line 39, for
visual effects — blur, sharpen, kaleidoscope, per `FilterDefs.h`). The
plan's audio §3 also wants an "Effects" category (Audio Filter, Dynamics,
Delay, Reverb, Drive, Pitch Time, Stereo). **Use `"Audio Effects"` for the
new category, not `"Effects"`** — reusing the visual category name would
mix audio and visual nodes in the same search-panel group and give them
identical tinting, exactly the kind of ambiguity the plan's own §3
name-collision table already resolved once for node *names* (`Filter` →
`Audio Filter`) — the same reasoning applies to this category *string*.
This session only needs categories for the three nodes it actually adds
(`Oscillator`/`Gain` → `"Synths"`; `Audio Out` → `"Audio Utility"`, per
§3's Utility list), but add all four of the plan's named new categories
now (`"Notes"`, `"Synths"`, `"Audio Effects"`, `"Audio Utility"`) since
`CategoryColors` entries are cheap, colour-only, and every later phase
needs them present — better to add all four in one pass across all five
presets than touch this file five more times across P3a–P3e.

**Mechanical but real work:** `CategoryColors.cpp` has 5 presets
(`"Infinite"`, `"Nord"`, `"Dracula"`, `"Catppuccin Mocha"`,
`"Tokyo Night"` — confirmed via `grep -n '{ "[A-Z][a-zA-Z ]*", {$'`), each
with its own colour table (`:30-`, `:50-`, `:70-`, `:90-`, `:110-`). Add
all 4 new categories to **all 5** tables, not just `"Infinite"` — a
category missing from one preset falls back to `ColorFor`'s "neutral grey"
default (per the header comment at `CategoryColors.h:45-47`), which is a
real but silent visual regression for anyone using a non-default theme,
not a crash, so it's easy to miss if you only check the default preset
while testing.

## 9. `Patch.h`/`.cpp` and `BuildPatchData`/`ApplyPatchData`

- `Patch.h`: add `std::vector<CableRecord> audio;` and
  `std::vector<CableRecord> note;` to `Data` (`Patch.h:81-89`), reusing the
  existing `CableRecord` shape exactly like `geometry` already does
  (`:84-85`) — don't invent a new record type, audio/note connections are
  the same `(dstIndex, dstSlot, srcIndex)` shape as every other typed
  cable.
- `Patch.cpp`: the `geo` tag's read path is at `:308-315` — mirror it for
  two new line tags, `audio` and `note`, each pushing into the
  corresponding new vector. Also find and mirror the **write** side (the
  function that emits `geo <dst> <slot> <src>` lines — not shown in what's
  been read yet this session, grep `Patch.cpp` for where it writes the
  `"geo "` prefix and add the two new prefixes right next to it).
- `BuildPatchData` (`main.cpp:6026-6150`): the geometry-pointer-comparison
  loop at `:6066-6139` is the pattern to extend, but audio/note cables
  don't need that pattern — they're typed `AudioCable`/`NoteCable` members
  with a plain `IsConnected()`/`GetSource()`, exactly like the *image*
  cable loop at `:6046-6061` (which uses `CableFor` + `IsConnected` +
  a direct `GetSource()` scan, simpler than the pointer-comparison
  approach the untyped geometry/camera/light/modulator pins need). Add the
  audio/note equivalents of that simpler loop, using `AudioCableFor`/
  `NoteCableFor` and pushing into `data.audio`/`data.note`.
- `ApplyPatchData` (`main.cpp:6216-`): find where it replays
  `data.geometry` (via `ConnectGeometrySlot`, per the header comment
  chain traced above) and `data.cables` (via `CableFor`+`Connect`, matching
  the save-side symmetry) and add the audio/note replay using
  `AudioCableFor`/`NoteCableFor`+`Connect`, matching whichever of those two
  existing replay shapes actually applies (audio/note cables are typed
  like image cables, not raw-pointer like geometry, so the `data.cables`
  replay path is almost certainly the shape to mirror — confirm by reading
  it, don't assume).
- **After a full-graph load, rebuild the audio topology once** (step 6) —
  loading 3 nodes and 2 audio cables one at a time would otherwise attempt
  3 separate topology rebuilds mid-load, which is wasteful and, more
  importantly, could call `SetTopology` with a half-wired graph if load
  order isn't guaranteed source-before-destination.

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```
Confirm clean, then run manually: spawn Oscillator → Gain → Audio Out,
connect them, confirm audio plays through the real device (no
`INFINITE_*` env var needed for this manual check — it's the actual app).
Then exercise the exit criterion literally: save the patch, reload it,
undo/redo the connections, copy/paste the three-node group, delete a node
mid-chain and confirm no crash (this is exactly what step 7's generic
`AudioInputSlot`/`DisconnectAllTo` wiring exists to guarantee) and confirm
audio stops cleanly rather than continuing to reference a freed node.

Run the full hygiene suite — **this is the first audio-phase session where
it applies**, since this phase is the one that first touches `INode`,
`Patch`, and the connect/disconnect paths 167 existing node types share:
```bash
.claude/skills/run-infinite-hygiene/driver.sh
```
Pay special attention to `ROUNDTRIPTEST` (register the three new node
types so they're included — check whether it enumerates
`NodeFactory::Instance()`'s registry automatically or needs an explicit
list; if automatic, the three new `REGISTER_NODE` calls are enough),
`PATCHTEST`, and `DELETECRASHTEST` — all three are named in
`docs/plans/audio/README.md` §7 as the checks this phase is most likely to
break. If any fail, fix before considering this session done — don't leave
a red hygiene suite for P3 to inherit.

Also re-run `INFINITE_DSPTEST` to confirm this session's `main.cpp`
changes (the new `dynamic_cast` branches threaded through
`InputCountFor`/`IsInputSlotCompatible`/etc.) didn't disturb the
`DspTest` namespace's separate hardcoded fixture:
```bash
INFINITE_DSPTEST=1 build/Infinite.app/Contents/MacOS/Infinite
```

## Out of scope (explicitly deferred)

- Any real synth/effect/note node beyond the three minimal proof nodes —
  P3a onward.
- Incremental topology diffing (only full-rebuild-on-change this session,
  see step 6) — revisit only if profiling ever shows patch-editing latency
  from this, which a 3-node graph won't.
- Pin-dot coloring if the imgui-node-editor API for it isn't already used
  elsewhere in this codebase (see step 5's caveat) — link tinting is the
  requirement this session must hit; pin dots are a nice-to-have.
- Envelope-Follower auto-insertion when an audio cable is dropped on a
  param pin (the README mentions this as a future coercion) — this
  session rejects that connection outright, no coercion node exists yet.
- Sidechain inputs, multi-output audio nodes, anything from P3c/P3d's
  effect/utility node lists — not touched by this phase's three nodes.
