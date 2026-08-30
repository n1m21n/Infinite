# Fix: undo/redo and node deletion stall on large patches (Windows + macOS)

Users report that on a large patch, pressing Undo/Redo or deleting a node
freezes Infinite for a fraction of a second to several seconds. It is worse on
Windows. This is not a mystery hang — it is four measurable costs on paths that
were written for correctness and never for scale. All four are confirmed in the
current source; line numbers below are from `src/main.cpp` at the commit this
brief was written against, so re-grep the function name if a number has drifted.

Do the work as **two commits**: Part A (items 1–5) is low-risk and fixes the
per-click stutter and the delete stall. Part B (item 6) is the larger change
that fixes the multi-second undo hang. Land and verify A before starting B.

---

## Confirmed root causes

**(a) `BuildPatchData()` is O(N²) with `dynamic_cast` in the inner loop.**
`src/main.cpp:23492`. For every node it linearly scans all of `gNodes` to
resolve each image cable, each of `kMaxAudioSlots` (8) audio cables, each of
`kMaxNoteSlots` (4) note cables, each of `kMaxGeometrySlots` (4) geometry pins,
plus camera/light/palette/modulator pins. The geometry `record` lambda
(`src/main.cpp:23568`) runs `dynamic_cast<IGeometrySource*>(src.node.get())`
inside that inner scan, and `ModulatorForOutput` (`src/main.cpp:3487`) does
another `dynamic_cast` per candidate output. On a 300-node patch that is on the
order of 10^5–10^6 `dynamic_cast` calls per snapshot. MSVC's RTTI path is
substantially slower than the Itanium ABI's, which is why Windows feels worse.

**(b) A full snapshot is built on *every left mouse click*, anywhere.**
`src/main.cpp:44808`: `if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
gDragStartSnapshot = BuildPatchData(); ... }`. This is not gated on the click
landing on a node, or on anything being selected. Every click on a knob, a
button, a menu, or empty canvas pays cost (a) in full. This is the single
largest contributor to "the whole app feels sticky on a big patch".

**(c) Batch delete calls `RebuildAudioTopology()` once per deleted node.**
`RemoveNodeByIndex` (`src/main.cpp:23421`) ends with `RebuildAudioTopology()`
(`src/main.cpp:23466`), and the batch-delete loop at `src/main.cpp:44425-44429`
calls `RemoveNodeByIndex` per node. `RebuildAudioTopology`
(`src/main.cpp:23065`) walks the whole graph, calls `PrepareToPlay` on every
audio node in the order (`src/main.cpp:23197`), recomputes PDC, and allocates a
fresh `ProcessList` with N `PooledBuffer::Allocate()` calls. Deleting a 20-node
group does all of that 20 times. `PrepareToPlay` on `AudioEffectNode`
(`src/nodes/AudioEffectNode.cpp:36`) resizes buffers and re-preps the kernel, so
this is also an audible bug: deleting an unrelated node resets every reverb tail
and delay line in the patch.

**(d) Undo/redo destroys and rebuilds the entire graph from disk.**
`Undo()`/`Redo()` (`src/main.cpp:24145`, `24157`) call `ApplyPatchData`
(`src/main.cpp:23903`), which calls `NewPatch()` — retiring every node — and
then respawns all of them. For each respawned node it calls `ReloadDerivedState`
(`src/main.cpp:4397`), which unconditionally re-runs `ReloadFromPath()` on
`ImageSourceNode`, `EnvironmentNode`, `ModelSourceNode`, `AudioFileNode`,
`SamplerNode`, `PaulStretchNode`, `MolderNode`, `GrainMolderNode`,
`GranularNode`, `DrumSequencerNode` (all paths), `VideoSourceNode`, and
`PaletteNode`. None of those has an early-out — e.g.
`ImageSourceNode::ReloadFromPath` (`src/nodes/ImageSourceNode.h:41`) just calls
`Load(p)` again. **One undo therefore re-decodes every image from disk,
re-parses every model, re-opens every video file, and re-reads every audio
sample.** Opening a video on Windows goes through Media Foundation source-reader
creation, which is hundreds of milliseconds each. That is the multi-second hang.
Every VST3/AU plugin instance is also destroyed and re-instantiated (the reload
is async via `ReloadFromIdentity`, but the *teardown* is synchronous and some
plugins block for a long time in it).

Two smaller items found alongside:

- `Undo()`/`Redo()` copy the snapshot instead of moving it:
  `Patch::Data prev = gUndoStack.back();` immediately followed by `pop_back()`
  (`src/main.cpp:24148-24150` and the mirror in `Redo`).
- `kMaxUndoDepth = 200` (`src/main.cpp:23861`) over a `std::vector<Patch::Data>`
  with `gUndoStack.erase(gUndoStack.begin())` at the cap
  (`src/main.cpp:24120`). Each `Patch::Data` holds two heap-allocated
  `std::string`s per parameter per node, so 200 snapshots of a 300-node patch is
  a large, fragmenting resident set — a second reason Windows suffers more.

---

## Part A — low-risk (do this first, one commit)

### 1. Stop snapshotting on every click
`src/main.cpp:44805-44810`. Only capture `gDragStartSnapshot` when the click
could actually start a node drag. Gate it on there being a node under the cursor
or a non-empty node selection — `ed::GetHoveredNode()` is the natural test and is
already used elsewhere in this file; check how the surrounding code queries it
and mirror that. Keep the existing "only push once real movement is confirmed"
logic at `44812-44830` exactly as it is; this change is purely about not
*capturing* on clicks that can never become a node drag.

Verify by hand afterwards: click-drag a node, confirm one undo restores its
original position; click a knob and drag it, confirm the knob's own
`IsItemDeactivatedAfterEdit()` checkpoint still gives one undo step and that the
node position is not disturbed.

### 2. Make `BuildPatchData()` linear
`src/main.cpp:23492`. Before the node loops, build the reverse lookup tables once
with a single pass over `gNodes` (N `dynamic_cast`s total, not N² ):

- `std::unordered_map<const void*, int>` mapping `INode*` → `gn.index`
- the same map extended with each node's `dynamic_cast<IGeometrySource*>` address
  (note the existing comment at `src/main.cpp:23574`: with multiple inheritance
  the `IGeometrySource*` and `INode*` addresses differ, so both must be inserted)
- the same for `IPaletteSource*` and for `IModulator*` per output index, mirroring
  `ModulatorForOutput`'s two cases (`node->ModulatorOutput(o)`, else
  `dynamic_cast<IModulator*>(node)` for output 0 only)

Then replace every inner `for (GraphNode& src : gNodes)` scan in this function
with a map lookup. Preserve the exact existing semantics, including that the
modulator pass records the *output index* `o` (see the comment at
`src/main.cpp:23629` — dropping it re-attached every restored cable to output 0)
and that a `nullptr` `wanted` records nothing.

This is a pure refactor: `BuildPatchData`'s output must be byte-identical.
`ROUNDTRIPTEST` and `PATCHTEST` in the hygiene suite cover this — they must still
pass unchanged.

### 3. Batch the audio-topology rebuild on delete
Add a defer flag next to `gSuppressUndoCheckpoints`, e.g. `gDeferAudioRebuild`,
and have `RebuildAudioTopology()` (`src/main.cpp:23065`) return immediately when
it is set. Set it around the batch-delete loop at `src/main.cpp:44420-44430`
(alongside the existing `gSuppressUndoCheckpoints = true`), clear it, then call
`RebuildAudioTopology()` once. Use the same pattern as
`ApplyPatchData`, which already deliberately does a single rebuild at the end
(`src/main.cpp:24069-24075`) for exactly this reason.

Be careful with the ordering rationale documented at `src/main.cpp:23461-23465`:
the rebuild must happen *after* the victim is erased from `gNodes`, never before.
Deferring to after the whole loop preserves that. `DELETECRASHTEST` and the audio
teardown sweep (`.claude/skills/audio-node-sweep/`) must still pass — they exist
precisely to catch a topology holding a pointer to a freed node.

### 4. Move, don't copy, the snapshots
`src/main.cpp:24145-24168`: in both `Undo()` and `Redo()`, take the snapshot with
`std::move(stack.back())` before `pop_back()`. Also move at
`src/main.cpp:44828` (`PushUndoSnapshot(std::move(gDragStartSnapshot))`) —
`gDragSnapshotPushed` is set true immediately after, so nothing reads
`gDragStartSnapshot` again before the next mouse-down reassigns it. Confirm that
reasoning holds against the code as you find it before making the change.

### 5. Change the undo stacks to `std::deque`
`src/main.cpp:23859-23861`. `std::vector` + `erase(begin())` at a 200-deep cap
shifts the whole stack on every checkpoint past the cap. `std::deque<Patch::Data>`
with `pop_front()` removes that. Everything else about the stacks stays the same.

Judgment call left to you: whether 200 is still the right depth once each entry
is this large. I would keep 200 rather than change user-visible behaviour in a
performance commit, but say so in the commit message if you disagree.

---

## Part B — the undo/redo hang (second commit)

The goal is that undoing a node move in a patch containing videos, models and
large images does **not** touch the disk at all.

**Recommended approach: make `ReloadDerivedState` idempotent via a path-keyed
asset cache, not by making `ApplyPatchData` diff-aware.**

I considered the diff-aware option — having `ApplyPatchData` reuse live nodes
whose `(typeName, category, saved index)` still match instead of calling
`NewPatch()`. It is the theoretically better fix, but `ApplyPatchData` is the
single shared restore path for undo, redo, file open, autosave recovery *and* the
round-trip self-test, and it currently gets its correctness from the fact that it
always starts from an empty graph and remaps every index. Making it partially
incremental is a large, high-risk change to the one function that must never be
wrong. **Do not attempt it in this commit.** If Part B's cache does not bring
undo under ~100 ms on a heavy patch, raise that as a separate, separately-scoped
piece of work rather than expanding this one.

Instead:

6. Add a process-wide, path-keyed cache for the assets whose reload dominates:
   - `ImageSourceNode` / `EnvironmentNode`: cache the decoded pixels or the GL
     texture keyed by absolute path + file mtime + size. Sharing a GL texture
     name across nodes changes ownership semantics, so the safer first version is
     to cache the *decoded CPU buffer* and skip only the stb_image decode and the
     disk read, still uploading a per-node texture. Measure whether that alone is
     enough before going further.
   - `ModelSourceNode`: cache the parsed `Mesh` keyed the same way. The mesh is
     already copied into the node, so this one is straightforward.
   - `VideoSourceNode`: do **not** cache the decoder handle — it is stateful and
     per-node. Instead check whether `Open()` can early-out when
     `mLoadedPath` already equals the requested path and `mVideo != nullptr`.
     On the undo path the node is freshly spawned so that early-out will not
     fire; if so, say that plainly in the commit message and leave video as a
     known remaining cost rather than papering over it.
   - Audio sample loads (`AudioFileNode`, `SamplerNode`, `GranularNode`,
     `PaulStretchNode`, `MolderNode`, `GrainMolderNode`, `DrumSequencerNode`):
     cache the decoded sample buffer keyed by path + mtime + size. These share a
     decode entry point — find it and put the cache there, once, rather than in
     each node.

   Invalidate on mtime/size change so "edit the file on disk, it updates" keeps
   working. Bound the cache (a total-bytes budget with LRU eviction) so a session
   that has touched a hundred 4K images does not grow without limit.

7. While you are in `ReloadDerivedState` (`src/main.cpp:4397`), note that
   `CopyParams` (`src/main.cpp:4439`) calls it too, so copy/paste of a heavy node
   gets the same benefit. Do not change `CopyParams`'s behaviour otherwise.

---

## Measurement — do this first and last

Before changing anything, add a temporary timing print (or a permanent one behind
an env var, matching how the other `getenv("INFINITE_...")` harnesses in
`src/main.cpp` are gated) around `BuildPatchData()`, `ApplyPatchData()`, and
`RebuildAudioTopology()`, then:

- Build a stress patch of ~200–300 nodes including at least a few
  `ImageSourceNode`s pointing at real files, a `ModelSourceNode`, and a
  `VideoSourceNode`.
- Record the millisecond cost of: one left click, one node delete, one group
  delete, one undo, one redo.
- Re-record all five after Part A and again after Part B, and put the
  before/after numbers in the commit messages.

Do not claim an improvement you have not measured. If a change turns out not to
matter, say so and keep or drop it on its own merits.

Consider adding a `INFINITE_UNDOPERFTEST` self-test to
`src/main.cpp` mirroring the existing `INFINITE_UNDOTEST` at
`src/main.cpp:37901` — spawn N nodes, time a `BuildPatchData()`, and fail if it
exceeds a generous ceiling. Register it in
`.claude/skills/run-infinite-hygiene/driver.sh`'s `TESTS=(...)` list (line 49) so
this cannot silently regress. This is optional; if you skip it, say why.

---

## Out of scope

- Do **not** make `ApplyPatchData` incremental/diff-aware (see Part B rationale).
- Do **not** change the patch file format, `Patch::NodeRecord`'s
  `vector<pair<string,string>>` param representation, or `Patch::Write`/`Read`.
  Switching undo snapshots off the string format onto a binary/variant one is a
  real further optimisation but is a separate piece of work with its own
  round-trip risk.
- Do **not** change undo *semantics* — which actions create a checkpoint, how
  many undos an action costs, or what `gSuppressUndoCheckpoints` covers. This is
  a performance change only. If you find a genuine semantic bug while in here,
  report it rather than fixing it inline.
- Do not touch `FindNodeByIndex`'s linear scan (`src/main.cpp:3496`). It is O(N)
  and called from 93 sites; converting it to a map is a broader change and is not
  on the hot paths identified above once item 2 lands.

---

## Verification

After each commit:

```
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean — do not rely on the edit "looking right".

Then run the full hygiene suite:

```
.claude/skills/run-infinite-hygiene/driver.sh
```

`UNDOTEST`, `PATCHTEST`, `ROUNDTRIPTEST`, `DELETECRASHTEST` and the audio
teardown sweep are the ones that directly guard what is being changed here; all
of them must pass, and the AUDIOPARAMSWEEPTEST xfail baseline must not grow.

Item 3 touches the audio graph, so also run:

```
.claude/skills/audio-node-sweep/driver.sh
```

Part B touches geometry-consuming nodes' reload path, so also run:

```
.claude/skills/geometry-transform-sweep/driver.sh
```

Finally, copy the built app to the Desktop as this project's convention requires:

```
cp -R build/Infinite.app ~/Desktop/Infinite.app
```
