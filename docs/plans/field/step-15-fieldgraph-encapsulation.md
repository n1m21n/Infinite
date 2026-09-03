# Field build step 15 — FieldGraphNode encapsulation ("Instrument Mode")

You are implementing **build step 15 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against
(`feature/field-step-14-dynamic-pins-graph-node` tip, commit `18ba50e`) —
re-grep the symbol if a number has drifted; the symbol wins.

**Prerequisite:** none of steps 11-14 need to be built first — see §0 below,
this is not a continuation of that sequence, it replaces it. Branch from
`feature/field-step-14-dynamic-pins-graph-node`
(`git checkout -b feature/field-step-15-fieldgraph-encapsulation`) only to
keep the branch-chain convention; no code from that branch is reused.

**Companion step:** `step-16-fieldgraph-unpack-to-canvas.md` builds the
"[Unpack to Canvas]" escape hatch on top of what this step ships. Build this
step first — unpacking only makes sense once there is something encapsulated
to unpack.

---

## 0. Why this step exists, and what it does to steps 11-14

### 0.1 The product problem (not a step 11-14 problem — a today problem)

`FieldGraphNode`'s *shipped* behavior, unrelated to anything steps 11-14
planned: writing a graph script and clicking "Regenerate" spawns every
`emit()`-ed node directly onto the visible canvas, at whatever position the
kernel's own call-site-based auto-placement computes (`FieldGraphNode.cpp`
§4.2 below), stacking new clusters as the user iterates. There is no live
parameter path either — a `param float amount = 0.5 [0,2]` declared on
`FieldGraphNode` already renders as an ordinary modulatable knob
(`DrawFieldGraphParams`, `main.cpp:5444-5484`, uses `ModSlider` exactly like
any other node's param), and a wired modulation cable already writes into
`p.value` every frame — but that new value only ever reaches a mounted child
node's actual parameter at the next full `Regenerate()`, which re-interprets
the whole program and re-diffs the whole mounted-node set
(`FieldGraphNode::Regenerate`, `FieldGraphNode.cpp:63-178`). There is no
per-frame bridge from "declared param's live value changed" to "the mounted
child's parameter actually moved." This makes `FieldGraphNode` useful for
one-shot generation and nothing else: no live-patchable instrument, no
clutter-free canvas.

### 0.2 Steps 11-14 are superseded outright — verified, not merely asserted

**Checked against the actual code at this branch tip, not just the docs:**
zero pin-related code exists in `FieldGraphNode.h`/`.cpp` today. No
`PinTable` type exists anywhere under `src/core/field/` (`find` and `grep`
both zero hits). No `ModulatorInputSlot` override, no `addTriggerInput`
field, nothing. Steps 11-14 (`step-11-dynamic-pins-phase1.md` through
`step-14-dynamic-pins-graph-node.md`) describe a `PinTable`-backed dynamic
pin mechanism for all four Field node types, but as of this branch tip **it
was never implemented for any of the four** — those four documents are
planning-doc-only, not a partially-built system this step needs to work
around or migrate off of.

**Verdict: step 14 is superseded outright by this doc. There is no code to
preserve, and this doc does not build step 14's design.** Specifically:

| Step 14 piece | Status under this doc |
|---|---|
| `trigger <name>` / `modparam <domain> <name>` declared outside the kernel body, user-typed keywords | **replaced.** This doc derives `FieldGraphNode`'s boundary pins automatically from the script's terminal `emit()`-ed nodes' own pin shapes (§2) — the user writes no pin-declaration syntax at all. Decision 1-3 from the original brief (referenced by step 14 §2) is answered differently here: pins are inferred, not declared. |
| §1.2's structural distinction — `FieldGraphNode`'s kernel runs once at edit time via `Regenerate()`, never `CookIfNeeded`; `emit`/`connect`/`set`/`place` are edit-time side effects on the graph, not per-cook values | **survives, load-bearing.** This doc's encapsulation design does not change when or how the graph-domain kernel runs. It changes *where the mounted result lives* (virtual, inside the node's own runtime, §2) and *how a param's live value reaches a mounted child* (§4), not the kernel's execution model. |
| `GraphIRProgram` must never grow `declaredOutputs`/`declaredInputs`; `output`/`input` refused inside a graph-domain kernel body | **survives unchanged.** This step touches `GraphIRProgram` and `IRStmtKind` not at all — boundary pins are derived from `GraphPlan` data (`EmitSpec`/`ConnectSpec`, already-existing structs, §2.2) after interpretation, never from new IR grammar. |
| `PinTable`, cable-orphaning refusal on `FieldGraphNode`'s own pins | **not reused — nothing to reuse; superseded by §3's boundary-pin bookkeeping**, which is deliberately simpler because boundary pins are derived, not user-declared, so there is no user-edited declaration list to refuse a removal from. Orphaning is handled differently here (§3.3). |
| The four-step sequence's closing claim ("all four Field node types now support dynamic pins") | **no longer true for `FieldGraphNode` in the sense step 14 meant.** `FieldElementNode`/`FieldSampleNode`/`FieldPixelNode` dynamic pins (steps 12-13) are untouched by this doc and remain whatever future work builds them — this doc is `FieldGraphNode`-only, exactly as step 14 was. |

**A future session must treat steps 11-14 as historical record for
`FieldElementNode`/`FieldSampleNode`/`FieldPixelNode` only (if that work is
ever resumed) and must not implement step 14's `trigger`/`modparam` design
for `FieldGraphNode` — this doc is what ships for that node type.**

---

## 1. Goal

Two changes to `FieldGraphNode`, both required for this step's exit
criterion:

1. **Default mode becomes encapsulated ("Instrument Mode").** Regenerating
   no longer spawns child nodes onto the visible canvas. They exist as real
   `INode` instances, wired exactly as `Regenerate()` already wires them
   today, but are not drawn as separate boxes in the node editor. The outer
   canvas shows exactly one `FieldGraphNode` box, with boundary pins (§2)
   and an inline preview (§5) standing in for whatever the encapsulated
   graph produces.
2. **Live parameter forwarding.** A `param` declared on `FieldGraphNode`
   that maps directly onto a mounted child's parameter (a `set(handle,
   "paramName", declaredParamName)` call whose value expression is exactly
   that param, §4.1) forwards every frame without a `Regenerate()`. A
   modulation cable plugged into that `FieldGraphNode` param pin therefore
   modulates the internal node smoothly, same as plugging directly into the
   child would if it were visible.

---

## 2. Files to read first

### Docs

| File | Why |
|---|---|
| `docs/plans/field/step-10-graph-domain.md` | the whole `FieldGraphNode`/`FieldGraphReconciler`/`IFieldGraphHost` design this step builds on top of — read in full |
| `.claude/skills/field-integration/SKILL.md` | `ParamRef`/`ParamMailbox` contract (§3-4), the `gParamRegisterOnly` mechanism this step's virtual-hosting design depends on is *not* documented there by name but the "register every frame even when hidden" rule is — cross-reference with `main.cpp:1374` below |
| `.claude/skills/codebase-navigation/SKILL.md` | general method; this step adds one new living-map entry (§9) |

### Real source — verified current, this branch tip

| File | Lines | Why |
|---|---|---|
| `src/nodes/FieldGraphNode.h` | whole file, 88 lines | the class this step modifies; `Apply()` (`:33`, compile-only) and `Regenerate(IFieldGraphHost&)` (`:40`, the only path that mounts/unmounts/wires) are unchanged in *contract*, only in what host they're driven against (§3) |
| `src/nodes/FieldGraphNode.cpp` | whole file, 197 lines | `Regenerate()` body (`:63-178`); the auto-placement block (`:86-147`) with its literal constants `kAutoPlaceOriginX/Y = 60`, `kAutoPlaceDX = 580` (`kAudioNodeWidth 440 + margin`), `kAutoPlaceDXWide = 1080`, `kAutoPlaceDY = 260`, `kAutoPlaceDYWide = 520` — **this is the only auto-layout code that exists anywhere in the codebase today** (confirmed: `grep -rn "topological\|TopoSort\|AutoLayout" src/` outside comments has zero hits for real layout code); it lays out by call-site left-to-right and clone-index top-to-bottom, not by topological depth, and step 16 needs to decide whether to extend it or replace it for the unpack case |
| `src/core/field/FieldGraphHost.h` | whole file, 57 lines | `IFieldGraphHost`: `Mount`, `Unmount`, `SetParam`, `Connect`, `Place`, `Alive`, `TypeNameOf`, `Spawnable`, `Remount` — the seam this step's virtual host implements a second way (§3) |
| `src/core/field/FieldGraphReconciler.h` / `.cpp` | 45 / 145 lines | `ReconcileGraphPlan` — untouched by this step; the virtual host still gets driven through exactly this diff, same as today |
| `src/core/field/FieldGraphKernel.h` | whole file, 78 lines | `EmitSpec`, `ConnectSpec`, `SetSpec`, `PlaceSpec`, `GraphPlan` — §2.2 derives boundary pins from `plan.connects`/`plan.emits` after `InterpretGraphProgram` runs, no new IR needed |
| `src/core/field/FieldIR.h` | `IRStmt::setValue` (`IRNodePtr`, the value expression of a `set()` call) around `:150-160` (re-grep, exact line drifts) | §4.1's provenance test — "is this `set()`'s value expression exactly a bare `Variable` node naming a declared param" — reads this field directly, no IR change |
| `src/main.cpp` | `MainGraphHost` (`:26143-26330`, re-verify range) implementing `IFieldGraphHost` against real `gNodes`/`SpawnNode`/`ConnectNodes`/`RemoveNodeByIndex` | the host this step's `VirtualGraphHost` (§3) is modeled on, and the one still used by step 16's unpack |
| `src/main.cpp` | `RunFieldGraphRegenerate` (`:27774-27781`, wraps `Regenerate()` as one undo step) and its call sites: the deferred flag drain after `ed::End()` (`:56650`-ish, re-grep `gFieldGraphPendingRegenerate`) and the standalone Field-editor window's own button (`:57230`-ish, re-grep `RunFieldGraphRegenerate(gFieldGraphEditor)`) | trap T14 — `Regenerate()` must never run nested inside `ed::Begin()`/`ed::End()`; this step's virtual-mode `Regenerate()` still mounts/unmounts real `INode` objects (§3.1) so the same deferral applies |
| `src/main.cpp` | `DrawFieldGraphParams` (`:5444-5484`) | shows `FieldGraphNode`'s own declared params as `ModSlider`s today (`p.id` as the modulation-matrix key) — confirms these are already ordinary modulatable knobs; §4 adds the missing forward-to-child step, nothing about the knob itself changes |
| `src/main.cpp` | the node-body draw dispatch, `dynamic_cast<FieldGraphNode*>(gn.node.get()) != nullptr` branch (`:53678-53682`, comment: "Meta-node, no picture to show") | exactly where §5's inline viewport call goes; today this branch is empty on purpose because there is nothing to show — the underlying reason (`FieldGraphNode::GetOutputTexture()` returns 0, `FieldGraphNode.h:23`) still holds, so §5's viewport reads a *child's* texture, not the node's own |
| `src/main.cpp` | `DrawPreview(INode* node)` (`:21151`-ish) | the existing generic inline-texture-preview function every image-producing node's body already uses (letterboxed, checkerboard backdrop, `GetOutputTexture()`/`GetOutputWidth()`/`GetOutputHeight()`) — §5 reuses this verbatim for a texture/render terminal, no new preview widget invented |
| `src/nodes/GranularNode.h` | `kWaveformCacheSize = 256`, `waveformMin`/`waveformMax` decimated-min/max arrays, `RebuildWaveformCache` (declared `:89-120`, defined in `.cpp`) | the closest existing pattern for an inline *audio* preview (a static node has no live scope elsewhere in this codebase — confirmed no `ScopeNode`/`WaveformPreview` type exists, `grep` zero hits outside Granular/GrainMolder/DrumSequencer's own per-lane caches) — §5.3 adapts this shape for an audio-domain terminal, it does not invent a new caching scheme |
| `src/main.cpp` | `gParamRegisterOnly` (`bool` at `:1374`; set `true`/`false` around collapsed-node and modulation-matrix drawing passes, e.g. `:36426-36430`, `:36455-36461`) | the mechanism §3.2 reuses to keep a virtual/encapsulated child's `VisitParams`-driven `ParamRef` registration and `ParamMailbox` push alive every frame without drawing it on the visible canvas — this already exists for collapsed nodes today, doing the same "run the node's normal per-frame logic, skip the pixels" job this step needs for hidden children |
| `src/nodes/UtilityNodes.h` | `GroupNode` (`:53-81`) | referenced only for contrast in §3 — this step does **not** use `GroupNode` for encapsulation (that is step 16's un-encapsulation target, not this step's container) |

---

## 3. What "encapsulated" means, concretely

### 3.1 Mounted nodes are real `INode` instances; only their canvas presence changes

Per §0.2's point-2 table entry, `Regenerate()`'s contract is unchanged: it
still calls `Apply()`, still calls `InterpretGraphProgram`, still calls
`ReconcileGraphPlan` against a host implementing `IFieldGraphHost`, still
produces real `Mount`/`Unmount`/`SetParam`/`Connect`/`Place` calls. What
changes is the host `Regenerate()` is called against, and one new piece of
per-`FieldGraphNode` bookkeeping:

```cpp
// FieldGraphNode.h additions
public:
   // True (default) = encapsulated / Instrument Mode: mounted children are
   // real nodes in gNodes but flagged hidden from the visible canvas and
   // node-editor picking (§3.2). False after step 16's "Unpack to Canvas"
   // runs (that step flips this permanently for this instance and moves
   // membership into a GroupNode instead - see step 16 §3).
   bool encapsulated = true;

   // The node indices this FieldGraphNode currently owns, mirroring
   // mOwnership's values (Field::GraphOwnershipMap key -> gNodes index) but
   // as a flat set for O(1) "is this index one of mine" checks from the
   // node-editor draw loop (§3.2) and the inline-viewport terminal lookup
   // (§5.2). Rebuilt from mOwnership at the end of every Regenerate() call,
   // not persisted separately - VisitParams does not need a new line for
   // it, ownershipText already round-trips the source of truth.
private:
   std::set<int> mMountedIndices;
```

`mOwnership` (`FieldGraphNode.h:82`, already a
`Field::GraphOwnershipMap key -> gNodes index`) is exactly the data
`mMountedIndices` mirrors — the new field is a derived cache for fast
membership tests, not a second source of truth.

### 3.2 Hiding a mounted node from the canvas — reuse `GraphNode`'s existing hide mechanism, do not invent a new one

Grep before assuming this needs new plumbing: check whether `GraphNode`
(the `gNodes` element wrapping every `INode*`) already has any per-node
"exists in the graph but not drawn/pickable" flag (comment/hidden viewport
nodes, disabled nodes, etc. — this codebase already hides retired viewports,
`gRetiredViewports`, and already skips drawing for other reasons in the main
node-editor loop). **If such a flag exists, add one more boolean alongside
it and gate this step's mounted-child hiding on it** (skip `ed::BeginNode`/
`ed::EndNode` for the frame, skip drawing the box, but do NOT skip:

- `CookIfNeeded` — the node still needs to run every cook, same as a
  visible node; hiding it from the canvas must not become a second
  bypass/mute mechanism.
- `VisitParams`-driven `ParamRef` registration — run it every frame with
  `gParamRegisterOnly = true` around the call (§2's file-read entry), the
  exact mechanism already used for collapsed nodes (`main.cpp:1374` and its
  call sites). This is what keeps a hidden child's own params modulatable
  by a cable plugged directly into the child (still possible — encapsulation
  hides the box, it does not forbid cables terminating on a hidden node;
  see the trap in §8 about not conflating "hidden" with "uncable-able").
- Audio-thread topology inclusion (`RebuildAudioTopology`) — a hidden audio
  node must still cook and still be part of the audio graph, or Instrument
  Mode silently mutes every internal audio node the moment it's regenerated.

**If no such flag exists** (verify by reading the node-editor draw loop in
full around where `DrawGroupNode`/regular node drawing happens, `main.cpp`
~`53100`-`53700`), add the minimal one: a `bool hiddenFromCanvas` on
`GraphNode` (not `INode` — this is canvas presentation state, the same
category as `spawnX`/`spawnY`/`liveX`/`liveY` already living on `GraphNode`,
not on the node itself), checked at the very top of the per-node draw loop
to skip `ed::BeginNode` through `ed::EndNode` for that node this frame,
while every other per-frame system (cook, param registration, audio
topology) still walks `gNodes` unconditionally exactly as it does today.
This is deliberately the smallest change that satisfies "invisible, not
absent."

### 3.3 The virtual host: same `IFieldGraphHost`, `Mount` sets the hidden flag

```cpp
// main.cpp, alongside MainGraphHost
struct VirtualGraphHost final : public Field::IFieldGraphHost
{
   FieldGraphNode* owner; // whose mMountedIndices this Mount/Unmount updates

   int Mount(const std::string& typeName) override
   {
      // Identical to MainGraphHost::Mount (SpawnNode at 0,0, category
      // lookup) - copy that body, do not refactor MainGraphHost into a
      // shared base yet; see the trap in §8 about premature sharing before
      // step 16 shows what actually needs to be common.
      GraphNode* gn = /* ...same as MainGraphHost::Mount... */;
      if (gn != nullptr)
      {
         gn->hiddenFromCanvas = true; // or the equivalent flag from §3.2
         owner->mMountedIndices.insert(gn->index);
      }
      return gn != nullptr ? gn->index : -1;
   }

   void Unmount(int id) override
   {
      owner->mMountedIndices.erase(id);
      // ...identical cable/mod-rescue accounting as MainGraphHost::Unmount...
   }

   // SetParam, Connect, Alive, TypeNameOf, Spawnable, Remount: identical to
   // MainGraphHost - copy, don't share yet (§8).

   void Place(int id, float x, float y) override
   {
      // A no-op is correct here, not a bug: an encapsulated child's
      // position is meaningless - nothing ever draws it at (x,y). Do NOT
      // silently drop place() calls with an error/notice; they are still
      // syntactically valid (the kernel can still call place(), e.g. code
      // shared between step-16-unpacked and still-encapsulated instances)
      // and simply have no effect while encapsulated - Place() being a
      // no-op is what already happens for cases where the plan places a
      // key that's never emitted, so this is consistent with existing
      // graph-plan tolerance, not a new special case.
   }
};
```

`RunFieldGraphRegenerate` (`main.cpp:27774`) gains one line: construct
`VirtualGraphHost` instead of `MainGraphHost` when
`target->encapsulated == true`, `MainGraphHost` otherwise. Trap T14's
deferral (never call `Regenerate()` nested inside `ed::Begin()`/`ed::End()`)
applies identically to both hosts — `VirtualGraphHost::Mount` still calls
`SpawnNode`, which still mutates `gNodes`.

---

## 4. Boundary pins — derived, not declared

### 4.1 Input pins: `FieldGraphNode`'s own `param` declarations, unchanged in kind

Nothing new here. `param float amount = 0.5 [0,2]` declared inside the
graph-domain kernel's source (parsed by the existing
`LowerGraphProgramToIR`, producing `GraphIRProgram::declaredParams`,
`FieldGraphNode.cpp:55`/`FieldIR.h`) already becomes a `ParamEntry` in
`mParamTable` and already renders as a `ModSlider` knob
(`DrawFieldGraphParams`, `main.cpp:5478-5483`). This step does not add
declaration syntax for input pins — the existing `param` mechanism already
*is* the input-pin mechanism (a modulatable float knob on the node's own
header/body is what an "input pin" means for a scalar in this codebase;
compare to how any ordinary node's `param` is simultaneously a UI knob and a
modulation-cable target, no separate "pin" concept exists for scalars
anywhere else in Infinite either). §4.2 is what's actually new: making a
`param`'s live value reach the node it targets.

### 4.2 The live-forwarding mechanism — feasibility finding

**Verified, not assumed: today, a `param`'s current value only reaches a
mounted child at `Regenerate()` time.** `InterpretGraphProgram`
(`FieldGraphKernel.h:73-77`) takes a `params` snapshot
(`mParamTable.ValueMap()`, `FieldGraphNode.cpp:68`) and bakes it into
`SetSpec::value` (a plain `float`, `FieldGraphKernel.h:39-45`) once, at
interpretation time. `ReconcileGraphPlan` then calls `host.SetParam(id,
paramName, value)` for each `SetSpec` whose target actually mounted/updated.
There is no per-frame path from "the ParamTable entry backing this param
changed" to "the already-mounted child's field changed" — confirmed by
reading `Regenerate()`'s full body (`FieldGraphNode.cpp:63-178`): the only
call to `host.SetParam` happens inside `ReconcileGraphPlan`
(`FieldGraphReconciler.cpp`), which only runs as part of a full
`Regenerate()`.

**Is a full teardown required to forward a value live? No — but a new fast
path is required, not a trivial one.** `IFieldGraphHost::SetParam(id,
paramName, value)` (`FieldGraphHost.h:26`) is already a direct, cheap call —
`MainGraphHost::SetParam` (`main.cpp` ~`26280`-ish) just runs a
`ParamVisitor` against the target node's `VisitParams`. It does not go
through `ReconcileGraphPlan` and has no dependency on re-interpreting the
whole program. **The missing piece is provenance**: `SetSpec` (and the
`GraphPlan` that holds a `std::vector<SetSpec>`) has no record of *which*
`SetSpec`s are a direct, unconditional pass-through of a single declared
param (`set(osc1, "amount", childAmount)` where `childAmount` names a
`param`) versus a computed expression (`set(osc1, "amount", childAmount *
2 + offset)`) or a plain literal. Add that provenance at interpretation
time:

```cpp
// FieldGraphKernel.h — SetSpec gains one field
struct SetSpec
{
   std::string targetKey;
   std::string paramName;
   float value = 0.0f;
   std::string sourceParamName; // NEW: non-empty iff setValue's expression
                                 // (FieldIR.h IRStmt::setValue) is exactly a
                                 // bare Variable node naming a declared
                                 // param - i.e. this SetSpec is a live-
                                 // forwardable direct pass, not a computed
                                 // expression. Empty for a literal or any
                                 // expression more complex than a bare name.
   SourceSpan span;
};
```

`InterpretGraphProgram` (`FieldGraphKernel.cpp`, not read in full above —
read it before implementing) sets `sourceParamName` while walking each
`IRStmtKind::SetParam` statement's `setValue` node: if `setValue->kind ==
IRKind::Variable && !setValue->isHandle` and the name matches an entry in
`program.declaredParams`, record it; otherwise leave it empty (the existing
literal-baking behavior is unchanged for every other case — this is
additive, not a behavior change to `SetSpec::value`, which is still computed
and still baked exactly as today for the full-`Regenerate()` path).

`FieldGraphNode` then keeps, alongside `mLastPlan`, a small derived index
built once per successful `Regenerate()`:

```cpp
// FieldGraphNode.h
private:
   // (declared param name) -> list of (mounted node index, paramName) this
   // param forwards to directly, per §4.2's sourceParamName provenance.
   // Rebuilt at the end of every successful Regenerate() from mLastPlan.sets
   // and mOwnership - never mutated incrementally, never saved (VisitParams
   // does not need a new line - it is fully re-derivable from state that
   // already round-trips: code, mOwnership, and mParamTable's declared
   // params).
   std::map<std::string, std::vector<std::pair<int, std::string>>> mLiveForward;
```

And a new per-frame call, **not gated on `Regenerate()`**:

```cpp
// FieldGraphNode.h
public:
   // Pushes any param whose value has changed since the last call directly
   // to its forwarded target(s) via host.SetParam - no re-interpretation,
   // no ReconcileGraphPlan, no Mount/Unmount. Called once per frame per
   // FieldGraphNode with at least one non-empty mLiveForward entry, from
   // the same per-frame tick that already drains
   // gFieldGraphPendingRegenerate (main.cpp, after ed::End() - trap T14
   // does NOT apply here: this never spawns/removes/reconnects a node, only
   // ever calls SetParam on ones that already exist, so it is safe to call
   // every frame including from inside a still-open ed:: pass if ever
   // needed - but for consistency with the rest of this doc's flow, drive
   // it from the same post-ed::End() tick anyway).
   void PushLiveParams(Field::IFieldGraphHost& host);
```

`PushLiveParams` walks `mParamTable.Params()`, and for any entry whose
`value` differs from the value last pushed (track a small
`std::map<std::string, float> mLastPushedValue` alongside `mLiveForward`,
same lifetime), looks it up in `mLiveForward` and calls
`host.SetParam(index, paramName, newValue)` for every `(index, paramName)`
pair, using `mOwnership`/`mMountedIndices` to resolve. **This is real,
previously-nonexistent work, not a reuse of an existing mechanism** — say so
plainly rather than implying it falls out of what's already there.

**One more gap worth stating plainly, not glossed over:** `SetParam`'s
current implementations (`MainGraphHost::SetParam`,
`main.cpp`) write straight into the target's field via `ParamVisitor` —
correct for a value the target reads directly on its own next cook (the
pattern every Field element/sample/pixel node's own params already use, per
`field-integration` §3's `ParamRef` contract: the modulation system owns
smoothing, not the writer). If the mounted child is itself an ordinary
audio node whose parameter is meant to be smoothed via `ParamMailbox`
(`field-integration` §4), a raw field write bypasses that node's own
`ParamRef`/mailbox path entirely, producing a stepped/zippered value instead
of a smoothed one. **This is not a new bug this step introduces** — it is
already true of every `SetParam` call today, live-forwarded or not, since
`Regenerate()`'s existing `host.SetParam` calls go through the exact same
`ParamVisitor` write. Fixing it (routing a live-forwarded audio param
through the target's own registered `ParamRef`/`ParamMailbox::Push` instead
of a raw field write) is out of scope for this step — flag it, do not fix
it silently, and do not claim the "smoothly at audio rate" requirement in
§0.1's product ask is fully met for audio-domain targets until this is
addressed. It is met today for anything read once per frame on the main
thread (the overwhelming majority of Field-mountable node params), which is
most of the value.

---

## 5. Output pins and the inline viewport

### 5.1 Deriving the output pin(s) from terminal emitted nodes

A "terminal" emitted node, for this step's purposes: an entry in
`plan.emits` (`GraphPlan::emits`, `FieldGraphKernel.h:60`) whose key never
appears as a `srcKey` in `plan.connects` — i.e. nothing inside the graph
consumes its output, so it is a leaf of the internal wiring, a candidate
boundary output. (A node that is only ever a `dstKey` and never a `srcKey`
is an internal sink with no output pin of interest here; a node that is
never mentioned in `connects` at all, source or destination, is still a
terminal by this same rule — nothing consumes it.)

For each terminal, resolve its mounted `INode*` (via `mOwnership`) and ask
it the same three questions any other node in this codebase already answers
generically:

| Terminal's `GetOutputTexture()`/`RequiresAudioProcessing()` | Boundary pin kind |
|---|---|
| non-zero texture, `GetOutputWidth() > 0` | image/texture output pin |
| implements `IGeometrySource` | geometry output pin |
| `RequiresAudioProcessing()` true (has an `IAudioSource` half) | audio output pin |

**Do not build a new classification enum for this** — `INode`'s own
virtuals (`field-integration` §2/§6) already answer exactly this, generi-
cally, for every node type in the codebase; a terminal `emit()`-ed node is
just an ordinary `INode*` by the time it's mounted, nothing graph-domain-
specific about answering these three questions.

**Multiple terminals** (a script that emits two independent unconnected
chains) produce multiple boundary output pins, one per terminal, in
`plan.emits` order. This is deliberately simple over clever — no attempt to
infer "the important one"; every leaf gets a pin.

Boundary output pins are recomputed at the end of every successful
`Regenerate()`, from `mLastPlan` (already stored, `FieldGraphNode.h:81`) —
no new persisted state, no save-format change. A pin that disappears
because a `Regenerate()` un-terminal'd it (the user wired a new `connect()`
consuming what used to be a leaf) simply stops appearing next frame; there
is nothing to "orphan-refuse" here in the step-13/14 sense, because there is
no user-authored declaration to protect — the pin set is a pure function of
the current program, recomputed every regenerate, same as `mLastPlan`
itself already is.

### 5.2 Boundary input pins beyond `param`

Per §4.1, scalar params already cover the "float value flows in" case. This
step does not add image/audio/geometry-typed *input* pins on
`FieldGraphNode` deriving from the internal graph's own unconnected input
slots (e.g. an internal node with a dangling image input slot that nothing
inside the script feeds) — that is a real future extension but is out of
scope here; note it as such in §7, do not silently build a partial version.

### 5.3 The inline viewport — reuse `DrawPreview`, extend for audio

For an image/texture or geometry-rendering terminal, the node-body dispatch
(`main.cpp:53678-53682`, currently an empty branch with the "Meta-node, no
picture to show" comment) changes to:

```cpp
else if (auto* fgn = dynamic_cast<FieldGraphNode*>(gn.node.get()))
{
   if (INode* terminal = fgn->PrimaryTerminalForPreview()) // §5.1's list, first entry with a texture
      DrawPreview(terminal);
   else if (INode* audioTerminal = fgn->PrimaryAudioTerminalForPreview())
      DrawFieldGraphWaveform(fgn, audioTerminal); // §5.3.1, new
   // else: no terminal yet (empty/uncompiled program) - draw nothing,
   // exactly as today's empty branch does.
}
```

`DrawPreview(INode*)` (`main.cpp:21151`) is called **exactly as every other
image-producing node's body already calls it** — no new preview widget,
this is the literal existing function, called with the resolved terminal's
`INode*` instead of `gn.node.get()`. This is the concrete "point to the real
existing pattern" the brief asked for: there is no second preview mechanism
to invent for the texture/geometry case.

#### 5.3.1 Audio terminal — no existing generic scope, adapt the Granular pattern

Confirmed by grep: no generic "inline audio waveform preview for any node"
function exists in this codebase today. The closest precedent is
`GranularNode`'s own decimated min/max cache
(`src/nodes/GranularNode.h:89-92`, `kWaveformCacheSize = 256`,
`waveformMin`/`waveformMax` float arrays, filled by
`RebuildWaveformCache(const std::vector<float>& mono)`), which is per-node,
not generic. `DrawFieldGraphWaveform` (new, `main.cpp`, next to
`DrawPreview`) adapts this shape rather than inventing a third: maintain the
same `kWaveformCacheSize = 256` decimated min/max pair on `FieldGraphNode`
itself (not on the terminal — the terminal doesn't know it's being
previewed), fed by reading the terminal's `IAudioSource` output via the same
mechanism `MeterRing`/analysis nodes already use to pull audio-thread data
to the main thread (`field-integration` §4 — audio → main only ever goes
through `MeterRing`, no new channel here either), and draw it as a filled
min/max strip the same visual language `GranularNode`'s own body already
uses. Do not build a spectrum/FFT view — out of scope, min/max waveform
only, matching the existing precedent exactly.

### 5.4 Boundary pin identity for cables

An outer cable plugged into `FieldGraphNode`'s derived output pin needs a
stable target to resolve against every frame (the terminal's `INode*`
resolved via `mOwnership`, re-resolved each `CookIfNeeded`/draw pass rather
than cached as a raw pointer — same "never hold a stale `INode*`/`GraphNode*`
across a `SpawnNode` call" discipline the codebase-navigation living map
already documents for `SpawnNode`). If the terminal's identity key
disappears on the next `Regenerate()` (the emit that used to produce it is
gone), any outer cable that was plugged into that pin is detached exactly
as `MainGraphHost::Unmount`'s existing `mDetachedCableCount` accounting
already surfaces for internal cables — extend that same counter/notice path
to boundary cables rather than inventing a second detached-cable report.

---

## 6. Save / load / undo

No new `VisitParams` entries. `code`, `uid`, `ownershipText`, and
`mParamTable`'s existing `VisitParams` call (`FieldGraphNode.cpp:180-197`)
already carry everything this step needs to reconstruct on load:
`encapsulated` (new field, §3.1) **does** need one new line — add
`v.Bool("encapsulated", encapsulated)` to `VisitParams`, defaulting to
`true` for a patch saved before this step existed (an old patch has no
`encapsulated` line at all; `ParamVisitor`'s existing "missing key keeps the
struct's default" behavior — confirmed by how every other optional bool
param in this codebase already loads — means an old save simply gets the
new default, which is the intended behavior: an old patch's children were
already unpacked onto the canvas, and defaulting to encapsulated would
silently swallow them from view on next load. **Trap, worth its own line in
§8**: default must be `false` (unpacked) for state loaded before this field
existed, not `true` — the opposite of the field's own default for a
*brand-new* node. Distinguish "field absent from the save" from "field
present and false" the same way any other backward-compatible bool
addition in this codebase already has to (check an existing precedent, e.g.
how a recently-added node bool defaults on old-patch load, before deciding
the exact mechanism — do not assume `ParamVisitor::Bool` alone gives you
this distinction, verify it).

`mMountedIndices` and `mLiveForward`/`mLastPushedValue` (§3.1, §4.2) are
derived-only, never serialized, rebuilt on the first `Regenerate()`/
`PushLiveParams` after load — same discipline as `mLastPlan` today.

Undo/redo: a hidden child node is still an ordinary `gNodes` entry, so
undo/redo's existing "rebuild `gNodes` wholesale" path (referenced by
`PruneDeadGroups`'s comment, `main.cpp:20807-20817`, about undo/redo not
calling `RemoveNodeByIndex`) already carries it correctly — the
`hiddenFromCanvas` flag (§3.2) lives on `GraphNode`, which undo/redo already
restores wholesale.

---

## 7. Out of scope for this step

| Not in this step | Where it lands |
|---|---|
| The "[Unpack to Canvas]" button and everything it does | `step-16-fieldgraph-unpack-to-canvas.md` |
| Image/audio/geometry-typed *input* pins derived from the internal graph's own dangling input slots (§5.2) | future extension, not designed here |
| Routing a live-forwarded param through the target's own `ParamRef`/`ParamMailbox` instead of a raw `VisitParams` field write, for audio-thread-consumed params (§4.2's stated gap) | future work; flagged, not fixed |
| Dashed/distinct-style cable rendering for a param-modulation cable vs. a data cable | open question, see step-16 §6 — this step's boundary pins render with whatever the existing `ed::Link` styling already does, no change |
| A spectrum/FFT inline preview for an audio terminal | out of scope, §5.3.1 — min/max waveform only |
| `FieldElementNode`/`FieldSampleNode`/`FieldPixelNode` dynamic pins (steps 11-13's remaining scope) | untouched, unresolved, this doc does not revive that work |

---

## 8. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Treating steps 11-14 as partially-built and trying to migrate existing `PinTable` code | there is no existing code — verified zero grep hits (§0.2). Building "migration" logic for code that doesn't exist wastes the whole step |
| 2 | Making `hiddenFromCanvas` also skip `CookIfNeeded`, param registration, or audio topology inclusion | turns "hidden" into a second mute/bypass mechanism; a hidden node must behave exactly like a visible one in every respect except being drawn (§3.2) |
| 3 | Assuming a cable cannot terminate on a hidden node | encapsulation is a canvas-presentation choice, not a graph-topology one — an outer boundary cable (§5.4) and, if the codebase's editor allows it, a direct cable to a hidden child's own pin, must both keep working |
| 4 | Refactoring `MainGraphHost` and `VirtualGraphHost` into a shared base class before step 16 exists | step 16 needs its own variant (mount directly onto canvas *and* into a fresh `GroupNode`, §16's own design) — premature sharing across three not-yet-fully-understood shapes produces the wrong abstraction; copy the ~150 lines twice, unify later if a fourth shape ever needs it |
| 5 | Defaulting `encapsulated` to `true` for a patch saved before this field existed | silently hides every existing user's already-unpacked graph children on next load (§6) — the trap is specifically about the *load-time* default for absent state, not the *new-node* default, and they are opposite values |
| 6 | Building a second detached-cable/dropped-mod counter for boundary pins | `MainGraphHost`'s existing `mDetachedCableCount`/`mDroppedModCount` accounting (already surfaced through `Regenerate()`'s notice string) is the pattern to extend, not duplicate (§5.4) |
| 7 | Reading a live-forwarded param value directly from `ProcessBlock` or writing it there | per `field-integration` §4, `ParamMailbox::Push` is main-thread-only and `SmoothedValue`/`SetImmediate` are audio-thread-only — `PushLiveParams` (§4.2) runs on the main thread, same as every other per-frame main.cpp tick |
| 8 | Assuming `DrawPreview` needs modification to support a terminal that isn't `gn.node.get()` | it doesn't — `DrawPreview(INode* node)` already takes any `INode*`, not specifically the currently-drawing `GraphNode`'s own node (§5.3); no signature change needed |

---

## 9. Living-map addition (`codebase-navigation` skill)

Add this entry when this step lands (per that skill's own "add to the map"
instruction):

> **`gParamRegisterOnly` is the existing "run a node's per-frame logic
> without drawing it" mechanism** (`main.cpp:1374`, used today for collapsed
> nodes and the modulation matrix). Any future feature needing a node to
> stay live (params, cook, audio topology) while absent from the visible
> canvas — `FieldGraphNode` encapsulation (step 15) is the first consumer —
> should drive this flag rather than inventing a second "logically present,
> visually absent" mechanism.

---

## 10. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

for v in FIELDTEST FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST \
         FIELDGRAPHBLASTTEST FIELDGRAPHENCAPTEST FIELDGRAPHLIVEPARAMTEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done

.claude/skills/cable-logic-sweep/driver.sh
.claude/skills/modulation-sweep/driver.sh
.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDGRAPHENCAPTEST` (new) must assert:

1. spawning a `Field Graph` node, writing a script that emits N nodes, and
   regenerating leaves `gNodes.size()` at exactly `N + 1` (the graph node
   plus its N children) but the node-editor's visible/pickable node count
   (however that's queryable — via the `hiddenFromCanvas`/equivalent flag
   from §3.2) at exactly `1`;
2. every mounted child still cooks every frame (a child with a
   frame-counter side effect, or an audio child with `RequiresAudioProcessing()`,
   still advances) despite being hidden;
3. a terminal emitting an image producer shows a non-empty texture via the
   node body's inline preview (§5.3), pixel-identical to what `DrawPreview`
   would show if the child were visible and previewed directly;
4. toggling `encapsulated` to `false` on an existing regenerated node makes
   its children appear on the canvas at their last `Place()`d (or
   auto-placed) position on the very next frame, with no re-`Regenerate()`
   and no node re-created (same `gNodes` indices before and after the
   toggle);
5. loading a patch saved before this step (no `encapsulated` line) restores
   with children visible on canvas (§6's default-for-absent-state rule),
   not hidden.

`INFINITE_FIELDGRAPHLIVEPARAMTEST` (new) must assert:

1. a script `param float amount = 0 [0,1] \n emit("LFO", 0) \n set(osc, "rate", amount)`
   (adjust to a real spawnable node/param pair) regenerated once, then
   driving `amount`'s `ParamTable` entry directly (simulating a modulation
   cable write) changes the mounted `LFO`'s actual `rate` field within the
   same frame, with zero calls to `ReconcileGraphPlan`/`Mount`/`Unmount`
   (instrument the test host to assert this, e.g. a call counter);
2. a `set()` call whose value expression is not a bare param reference
   (a literal, or `amount * 2`) is **not** present in `mLiveForward` — only
   full `Regenerate()` ever updates its target, confirmed by driving the
   backing param and asserting the target's field does *not* change until
   an explicit `Regenerate()` runs;
3. `PushLiveParams` called with no changed param values makes zero
   `host.SetParam` calls (instrument the test host with a call counter) —
   confirms it is a delta-only push, not a re-push-everything-every-frame
   loop.

---

## 11. This step, and step 16, replace the dynamic-pins sequence's remaining `FieldGraphNode` scope

Steps 11-14's stated closing claim ("all four Field node types now support
dynamic pins") is retroactively **not** how `FieldGraphNode` ships — see
§0.2. If `FieldElementNode`/`FieldSampleNode`/`FieldPixelNode` dynamic pins
are ever resumed, that work stands on its own and does not need anything
from this step; this step and step 16 are `FieldGraphNode`-only and do not
touch `PinTable`, `GraphIRProgram`, or `IRStmtKind` at all.
