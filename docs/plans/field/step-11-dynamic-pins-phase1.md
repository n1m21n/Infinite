# Field build step 11 — dynamic pins, Phase 1: a fixed per-type menu

You are implementing **build step 11 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

Line numbers are from `src/` at the commit this was written against — re-grep
the symbol if a number has drifted. **The symbol is authoritative, not the
number, and the code is authoritative over any doc, including this one.**

**Prerequisite steps that must already be finished and merged before you
start:** steps 1–10 (`docs/plans/field/step-01-*.md` through
`step-10-graph-domain.md`). All ten have shipped in this tree — `src/core/field/`
contains the full lexer/AST/IR/backend pipeline for all five domains,
`FieldElementNode`, `FieldSampleNode`, `FieldPixelNode` and `FieldGraphNode`
all exist and compile real kernels, and `FieldGraphNode` already has a working
`emit`/`connect`/`set`/`place` reconciler (`src/core/field/FieldGraphReconciler.*`).
Confirm with `INFINITE_FIELDGRAPHTEST` before starting.

This step also depends on
`docs/plans/field/design-brief-dynamic-pins.md` (read it in full — it is the
research brief this whole step-11/12/13/14 sequence executes) and on the
following **settled product decisions**, made by the app's owner and binding
on every step in this sequence, not just this one:

1. **Both phases ship.** The end goal (steps 12–14) is universal,
   kernel-declared, arbitrary in/out pins of any domain on all four Field node
   types — e.g. a `FieldElementNode` with two geometry inputs and one audio
   input, whose kernel makes geometry react to the audio. This step (Phase 1)
   is the cheaper, C++-hardcoded proof of the plumbing that step 12 builds on;
   it is not a smaller version of the real feature, it is a different,
   shippable milestone that happens to exercise the same infrastructure.
2. **All four node types get this**, not just one. `FieldGraphNode` is the
   type most likely to be confused with its existing `emit()` node-spawning
   machinery — §5.5 below states the distinction in writing for Phase 1, and
   step 14 restates and extends it for Phase 2.
3. **Default pins are the floor.** Every node type's current native pin shape
   (one geo in/out for `FieldElementNode`, note+audio in / audio out for
   `FieldSampleNode`, one image in/out for `FieldPixelNode`, no typed pins for
   `FieldGraphNode`) stays exactly as-is when the Phase-1 menu entry is
   switched off. The menu entry is additive, never a replacement.
4. **Cable-orphaning policy: refuse, don't drop.** If toggling the menu entry
   off would orphan a live cable on the pin it removes, the toggle is
   **refused** and the node's pin shape and program stay exactly as they were
   before the attempted toggle — the same "keep the last working program"
   convention `FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`) already
   uses for a failed compile. This is a hard acceptance-criterion item below,
   not a nice-to-have.

---

## 1. Invariants — restated verbatim, they override anything you infer

### 1.1 Clean room (non-negotiable)

Infinite is **MIT**. **Never** open, read, grep, or reference GPL sources:
Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or BespokeSynth
(also at `/Users/namansoni/BespokeSynth` on this machine — do not open it).
The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable freely; its code is not.
Safe to read: Faust (LGPL), ChucK (dual MIT/GPL), Houdini VEX docs,
TidalCycles docs.

### 1.2 No sigils, bare names — unaffected by this step, restated for completeness

Nothing in this step touches Field syntax. If a menu entry's activation ever
grows a syntax surface (it does not, in Phase 1 — see §5), it inherits this
rule without exception.

### 1.3 One step, one branch, one commit

```bash
git checkout -b feature/field-step-11-dynamic-pins-phase1   # off main
```

Commit when the fixture in §7 passes. Do not leave this step and step 12
uncommitted in the same tree — `step-09-sample-domain.md` §0.1 documents in
detail what happened the last time two steps were batched on one branch.

### 1.4 Run every Field harness, not only your own

```bash
cmake --build build -j8
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST; do
  echo "== $v"; env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tail -20
done
```

(Re-derive this list with `grep -o 'INFINITE_FIELD[A-Z0-9]*' src/main.cpp | sort -u`
before you rely on it — steps ship fixtures this doc cannot know about.) A
FAIL in an earlier step's harness is a regression in **this** change.

### 1.5 The four hand-maintained image-pin chains

Per `.claude/skills/cable-logic-sweep/SKILL.md`: audio/note/geometry/modulator
inputs are discovered generically through `INode`'s virtual accessors; image
inputs are **not** — they go through four hand-maintained `dynamic_cast`
chains in `src/main.cpp`. Verified against the current tree (the brief's line
numbers have drifted — this is the corrected set, re-grep before trusting it
further):

| Chain | Current line | Answers |
|---|---|---|
| `InputCountFor` | `src/main.cpp:4160` | how many *image* input pins a node draws |
| `CableFor` | `src/main.cpp:4300` | which `ImageCable*` a given image slot is |
| `IsInputSlotCompatible` | `src/main.cpp:4488` | what a slot accepts |
| `WireInputSlot` | `src/main.cpp:4565` | performs the connection |

Both `FieldPixelNode` (its `src` texture input, `InputCountFor` at
`main.cpp:4197`, `CableFor` at `:4338`) and `FieldElementNode` (its optional
`geo` input, `main.cpp:4199`) already have entries in `InputCountFor`/`CableFor`
— **do not assume symmetry across all four chains**: `grep -n "FieldElementNode\|FieldPixelNode\|FieldSampleNode\|FieldGraphNode" src/main.cpp`
before editing any of the four, and add exactly one line per chain per new
image-typed pin this step introduces. Run `python3
.claude/skills/cable-logic-sweep/check.py` after every edit to these chains.

### 1.6 A failing Apply() changes nothing that is running

Same rule as every prior Field step: `FieldElementNode::Apply()`,
`FieldSampleNode::Apply()`, `FieldPixelNode::Apply()` and
`FieldGraphNode::Apply()` (`src/nodes/FieldElementNode.cpp:48`,
`FieldSampleNode.cpp:321`, `FieldPixelNode.cpp:146`, `FieldGraphNode.cpp:27`)
already keep the last successfully compiled program on a failed recompile.
Phase 1 adds a **second** failure mode to the same `Apply()`s — toggling the
menu entry off while a cable is attached to the pin it would remove — and it
must be refused with the identical discipline: nothing about the node's live
pin shape, program, or state changes when the toggle is refused.

---

## 2. Goal

Prove out, with a small C++-hardcoded menu (no kernel-declared syntax, no IR
change, no domain-inference change), the four pieces of plumbing that Phase 2
(steps 12–14) will need for real:

1. `OutputCount()` (or an equivalent input-count accessor) returning something
   other than a hardcoded literal for a Field node type, driven by ordinary
   node state.
2. `Patch::CableRecord::srcOutput`/slot addressing more than one pin on a
   Field node, round-tripping through save/load.
3. Undo/redo and copy/paste correctness for a Field node whose pin count
   changes at runtime.
4. The cable-orphaning refusal policy (decision 4 above), enforced at
   `Apply()`, with a user-visible reason.

Each of the four Field node types gets exactly **one** optional extra pin,
picked from a menu of exactly one entry per type, toggled by an ordinary
`bool` param the user flips in the params panel — the same shape as
`GeometryTableNode::rows` (`src/nodes/GeometryTableNode.h:45`,
`OutputCount() { return 4 + 3 * RowCount(); }`) already being an ordinary
saved param driving `OutputCount()`. Nothing here is discovered from kernel
source text; that is Phase 2's job.

---

## 3. Files to read first, and why

### Docs and skills

| File | Why |
|---|---|
| `docs/plans/field/design-brief-dynamic-pins.md` | the full research brief this step executes — read it in full, not summarized |
| `.claude/skills/cable-logic-sweep/SKILL.md` | the four image-pin chains (§1.5 above) and the connection-matrix rules in `IsInputSlotCompatible` |
| `.claude/skills/field-integration/SKILL.md` | §6 pins and slots (one shared index space per kind); §5 save/load/undo generic-through-`VisitParams` rule |
| `.claude/skills/field-compiler/SKILL.md` | §7 the `FormulaNode`-modelled error/recovery discipline — this step's Apply()-refusal reuses it |
| `.claude/skills/node-ui-pillars/SKILL.md` | **before** touching any `DrawField*Params` function |
| `docs/plans/field/step-09-sample-domain.md` §0 | the branch-discipline rules this doc's §1.3–1.4 restate |

### Real source — the code wins over any doc

| File | Lines that matter | Why |
|---|---|---|
| `src/core/INode.h` | `OutputCount()` `:137` (default `1`), `OutputLabel(int)` `:138`, `ModulatorOutput(int)` `:143`, `IsAudioOutputIndex(int)` `:47` (default `true`), `AudioOutputSlotForPin(int)` `:48`, `GeometryInputSlot` `:166`, `AudioInputSlot` `:176`, `NoteInputSlot` `:177`, `ParamVisitor` `:80`, `VisitParams` `:159` | the whole pin contract |
| `src/nodes/GeometryTableNode.h` | `OutputCount()` `:45`, `RowCount()` `:51`, the comment at `:42-45` ("growing rows must only ever append new pins, never renumber existing ones") | the existing precedent for a runtime-variable `OutputCount()`, and its append-only rule, which this step must also honour |
| `src/nodes/MacroNodes.h` | `OutputCount()` `:49` (returns `2`), `ModulatorOutput(int)` override `:54` | **the exact precedent for "one node, second output is an `IModulator`"** — reuse this shape, do not invent a new one |
| `src/nodes/VideoSourceNode.h` | `OutputCount()` `:32` (`2`), `IsAudioOutputIndex(int)` `:40` (`index == 1`) | the precedent for "one node, two differently-*kinded* outputs, disambiguated by `IsAudioOutputIndex`" |
| `src/core/Patch.h` | `CableRecord` `:72-83`, `srcOutput` comment `:78-82` ("unused (always 0) for plain image cable records — only Note Router has more than one note output, and only a node like VideoSourceNode … has more than one audio output") | the save-format addressing this step must extend correctly |
| `src/main.cpp` | generic output-pin draw site: `54290` (`const int outputs = geoTable != nullptr ? 4 : (drumSeq != nullptr ? 1 : std::max(1, gn.node->OutputCount()));` — **note `GeometryTableNode` and `DrumSequencerNode` are special-cased here for their own inline-row UI reasons; a Field node must not need a third special case** — verify your node draws correctly through the plain `std::max(1, gn.node->OutputCount())` fallback); `ModulatorForOutput` `:4134` (`IModulator* specific = node->ModulatorOutput(outputIndex)`); the four image chains (§1.5) |
| `src/nodes/FieldSampleNode.h` | `code` default `:74`; `ReadRmsLatest(float&)` `:64-68`; `MeterRing& ScopeRing()` (FieldSampleNode.cpp:78) | **§5.2's menu entry already has 90% of its data plumbing built** — read this before writing anything for the sample node |
| `src/nodes/FieldSampleNode.cpp` | `mMeter.Write(&v, 1)` `:278` inside the `hasReduceRms` block `:272-278`; `mMeter` field `:298` | where the RMS value is already published to a `MeterRing`, today read only for the node's own on-screen readout, never exposed as an outbound pin |
| `src/core/field/SampleProgram.h` | `struct SampleProgram` `:85`; `hasReduceRms` `:95`; `reduceLoHz`/`reduceHiHz` `:96-97` | the compiled-program fields that already carry whether the kernel calls `reduce.rms` |
| `src/nodes/FormulaNode.cpp` | `Apply()` `:390`, the retry guard in `CookIfNeeded` (~`:409-420`) | the keep-last-working-program shape this step's refusal-on-toggle reuses |
| `src/core/Modulation.h` | `struct ParamRef` `:28`, `using Key = std::pair<int,int>` `:53`, `UnbindAllFor` `:125` | modulation bindings this step must not orphan when a pin is added/removed |
| `.claude/skills/run-infinite-hygiene/driver.sh` | `TIER1_CHECKS=(` `:79`, `FULL_TESTS=(` `:180` (drifted again from step 9's `:173` — **re-grep every time**, do not trust any previously-recorded line for this array) | where new fixtures register |

---

## 4. The menu, one entry per node type

| Node type | Default (unchanged) shape | Phase-1 optional extra pin | Toggle param |
|---|---|---|---|
| `FieldElementNode` | 1 geo in (`GeometryInputSlot(0)`), 1 geo out (`IGeometrySource`) | one **Frame**-domain scalar output, exposed as `ModulatorOutput(1)` — the value of the reserved `publish` attribute the kernel is expected to assign in element/frame scope (already legal Field syntax: an ordinary `attrib float publish = 0` plus an assignment; nothing new is added to the language for Phase 1) | `bool publishScalarOutput = false` |
| `FieldSampleNode` | note in, audio in (`AudioInputSlot(0)`), 1 audio out | one **Frame**-domain scalar output, exposed as `ModulatorOutput(1)`, sourced from the **already-existing** `reduce.rms` publish path (`FieldSampleNode.cpp:272-278`, `mMeter`) | `bool exposeRmsOutput = false` |
| `FieldPixelNode` | 1 image in (`src`, via the image chains), 1 image out | one auxiliary **image** output — a second render target the pixel kernel's existing `state` ping-pong texture (see `field-state` §4) already computes internally; exposed as a second image output pin instead of staying internal | `bool exposeAuxTexture = false` |
| `FieldGraphNode` | no typed pins (topology-only, via `emit`/`connect`/`set`/`place`) | one optional **modulator input** pin — a trigger the graph kernel can read as a `param`-like value at regeneration time, distinct from `emit()`'s node-spawning (see §5.5) | `bool addTriggerInput = false` |

None of these four entries requires a domain-inference change: `publish` is
already representable as an ordinary `attrib` at Frame domain (hoisted the
same way `amount` is in the `field-compiler` §5 worked example); `reduce.rms`
already exists (step 8/9); the pixel node's `state` ping-pong texture already
exists (step 7); and a `FieldGraphNode` modulator input is just one more
`INode::ModulatorInputSlot` override, which is already a fully generic
mechanism (`field-integration` §6) that no existing Field node happens to use
yet.

---

## 5. Procedure

### 5.1 `FieldElementNode` — the `publish` output

1. In `src/nodes/FieldElementNode.h`, add `bool publishScalarOutput = false;`
   to `VisitParams` (a `Bool`, per `ParamVisitor`'s five methods,
   `INode.h:80`).
2. Override `OutputCount()`: `publishScalarOutput ? 2 : 1`.
3. Override `ModulatorOutput(int index)`: for `index == 1` when
   `publishScalarOutput` is true, return a small owned `IModulator`
   implementation (`Field::ScalarPublishModulator` or similar, header-only,
   living next to the node) whose `Value()` reads the compiled program's last
   frame-domain `publish` value. Model the shape on `MacroNodes.h:54`.
4. In `Apply()` (`FieldElementNode.cpp:48`), after a successful compile,
   check whether the new program's `declaredAttribs`
   (`ElementIRProgram::declaredAttribs`, `FieldIR.h:186`) contains an entry
   named `publish` of type `float`. **Do not require it** — `publish` unused
   while the toggle is on is a `0.0` output, not an error. The toggle governs
   pin *existence*; the kernel governs pin *value*.
5. **The refusal (decision 4).** Before compiling with
   `publishScalarOutput` flipped from `true` to `false`, check whether output
   index 1 has a live cable (walk `gNodes` for any `GraphNode` whose slot maps
   to `(thisNodeIndex, 1)` via the existing cable-discovery helpers used by
   `RemoveNodeByIndex`/`DisconnectAllTo`, `src/main.cpp:24862`). If one exists,
   refuse the toggle: revert the `bool` in the UI, leave `OutputCount()`,
   the program, and every cable exactly as they were, and surface a one-line
   reason ("disconnect the publish output first") the same way a failed
   compile surfaces `LastError()`. This is a **UI-level** refusal (the toggle
   is a checkbox, not a compile), not a compiler-level one — but it must feel
   identical to the user: nothing observable changes on a refusal.

### 5.2 `FieldSampleNode` — the RMS output

1. Add `bool exposeRmsOutput = false;` to `VisitParams`.
2. Override `OutputCount()`: `exposeRmsOutput ? 2 : 1`.
3. Override `IsAudioOutputIndex(int index)`: **must** become
   `index == 0` (not the default `return true`) once `OutputCount() > 1`,
   exactly as `VideoSourceNode::IsAudioOutputIndex` does at
   `VideoSourceNode.h:40` — otherwise the generic audio-cable dispatch treats
   the RMS pin as a second audio buffer and every downstream consumer breaks.
4. Override `ModulatorOutput(1)` to return an `IModulator` whose `Value()`
   calls the **already-existing** `ReadRmsLatest(float&)`
   (`FieldSampleNode.h:64-68`) — this is the one menu entry in this step that
   needs no new data path at all, only a new pin exposing data that already
   flows.
5. `hasReduceRms` (`SampleProgram.h:95`) is read at `Apply()` time the same
   way as §5.1 step 4: the toggle governs whether the pin exists; whether the
   kernel actually calls `reduce.rms` governs whether the pin ever reads
   anything other than the modulator's last value (which defaults to `0`).
6. Refusal-on-toggle-off: identical shape to §5.1 step 5, checked against
   `(thisNodeIndex, 1)`.

### 5.3 `FieldPixelNode` — the auxiliary texture output

1. Add `bool exposeAuxTexture = false;` to `VisitParams`.
2. `FieldPixelNode` has **no** `OutputCount()` override today (default `1`,
   image-only). Add one, `exposeAuxTexture ? 2 : 1`.
3. **This is the entry that actually exercises the four hand-maintained image
   chains (§1.5) from the output side**, not just the input side they already
   handle. Read `field-state` §4 (the pixel domain's `state` ping-pong texture
   pair) to find the second texture already being computed internally; expose
   its read-side texture as `GetOutputTexture()`'s counterpart for output
   index 1. Since none of the four `main.cpp` image chains address *output*
   texture selection today (`CableFor`/`WireInputSlot` are input-side; the
   generic output-pin draw at `main.cpp:54290` already handles output *count*
   generically) — grep for any node with more than one **image** output
   before assuming there is no fifth site to update, and record what you find
   in your commit message rather than assuming symmetry with the input side.
4. Refusal-on-toggle-off: identical shape to §5.1 step 5.

### 5.4 `FieldGraphNode` — the trigger input

1. Add `bool addTriggerInput = false;` to `VisitParams`.
2. Override `ModulatorInputSlot(int slot)`: for `slot == 0` when
   `addTriggerInput` is true, return `&mTriggerInput` (an owned
   `IModulator*` field). `ModulatorInputCount()` returns `addTriggerInput ? 1
   : 0`.
3. **This pin does not feed the kernel as a `param`.** It is read once, by
   the node's own `Regenerate()` path (`RunFieldGraphRegenerate`,
   `src/main.cpp:27774`), as an edge-trigger: a rising crossing of `0.5`
   re-runs `emit`/`connect`/`set`/`place` the same way the existing
   "Regenerate" button already does. It is **not** wired into the kernel's IR
   at all in Phase 1 — no `Field::Domain` change, no new reserved name. This
   keeps the whole entry inside "no IR change" (§2).
4. Refusal-on-toggle-off: identical shape to §5.1 step 5, checked against the
   modulator *input* slot instead of an output.

### 5.5 The `emit()` distinction, stated explicitly

`emit()`/`connect()`/`set()`/`place()` (`FieldGraphKernel.cpp`,
`FieldGraphReconciler.*`) let a **graph**-domain kernel spawn **other
`INode` instances** on the canvas — node-graph topology, resolved once per
regeneration by the reconciler, entirely separate `INode`s from the
`FieldGraphNode` itself. The trigger pin added in §5.4 is the opposite
direction: it is data flowing **into** the `FieldGraphNode`'s own pin
boundary, exactly like a knob or a modulator input on any other node — it
does not spawn anything, name anything, or touch the reconciler's
`key → nodeIndex` ownership map (`FieldGraphOwnership.*`). A future
implementer must not let these two mechanisms share code: `emit()` stays
entirely inside `FieldGraphKernel.cpp`/`FieldGraphReconciler.*`; the trigger
pin is ordinary `INode` pin plumbing in `FieldGraphNode.h`/`.cpp`, wired the
same way any other node's `ModulatorInputSlot` is. Step 14 restates and
extends this distinction for Phase 2, where the graph node's *dynamic* pins
(kernel-declared, not menu-toggled) raise the same question again at higher
stakes.

### 5.6 Save/load, undo, copy/paste

All four toggles and the pin they gate are ordinary saved `Bool` params
through `VisitParams` — **no new `Patch` line kind**. `Patch::CableRecord`'s
existing `srcOutput`/slot fields already address "more than output/input 0"
(`Patch.h:78-82`); the only new fact this step introduces is that a Field
node's second pin can now legitimately appear there. Verify explicitly, do
not assume:

| Case | Must hold |
|---|---|
| save → load, toggle on, pin 1 wired | the cable's `srcOutput`/`dstSlot` round-trips and reconnects to the right pin, not pin 0 |
| save → load, toggle on, pin 1 **not** wired | loads with the pin present and empty |
| an **old** patch, saved before this step existed | loads with the toggle at its default (`false`) and exactly the pre-step-11 pin shape — `ROUNDTRIPTEST` must still pass unmodified for every pre-existing Field patch in the corpus |
| undo across a toggle flip | the pin count, any cable that existed before the flip, and the toggle's own `Bool` value all revert together, in the same undo step — this is a single `VisitParams`-backed param change and needs no special-casing (`field-integration` §5's "adding a per-node entry to undo/copy-paste is a sign the node is built wrong" applies here too) |
| copy/paste a node with the toggle on | the pasted node has the same pin shape; a cable into/out of the extra pin is rescued or dropped exactly as `main.cpp`'s existing paste path (`~50100-50135`) already handles any other multi-output node's cables |

### 5.7 Append-only, per `GeometryTableNode`'s own rule

`GeometryTableNode.h:42-45`'s comment states the rule this step must also
honour: growing pin count must only ever **append**, never renumber. Every
menu entry above adds pin **index 1** on top of the existing index-0 pin —
never inserts before it, never reorders. This is why the toggle is a single
bool rather than, say, a dropdown of several extra-pin *kinds*: a dropdown
that could change which pin is index 1 would violate append-only the moment
the user picks a different entry while a cable is attached. If Phase 2 (step
12) ever needs more than one optional pin per node, each new pin gets the
next unused index, never reuses one a still-live cable might reference from
an older saved patch.

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Letting the toggle's `Apply()` drop a live cable silently | decision 4 — this is the entire point of this step; a silently dropped cable is treated as a failed acceptance criterion, not a minor issue |
| 2 | Forgetting `IsAudioOutputIndex` on `FieldSampleNode` once it has 2 outputs | the default (`INode.h:47`, `return true`) makes every downstream node treat the RMS pin as a second audio buffer |
| 3 | Special-casing a Field node in the `main.cpp:54290` output-pin draw site the way `GeometryTableNode`/`DrumSequencerNode` are | none of the four Phase-1 entries need inline per-pin UI; if you find yourself adding a third special case, the design has drifted from §2's scope |
| 4 | Wiring the `FieldGraphNode` trigger pin into the kernel's IR | §5.5 — that is Phase 2/step 14 territory, and doing it now blurs the emit()-vs-pins distinction the owner explicitly asked to be kept explicit |
| 5 | Treating the toggle as if it were a kernel-declared pin | it is not — Phase 1 has zero IR/compiler changes; if implementing an entry seems to require touching `FieldIR.h`/`FieldParse.cpp`, stop and read §2 again |
| 6 | Adding a new `Patch` line kind for the toggle or the extra pin | unnecessary — `Bool` through `VisitParams` plus the existing `CableRecord.srcOutput` addressing already cover every case in §5.6 |
| 7 | Reordering or inserting before pin index 0 | `GeometryTableNode.h:42-45`'s append-only rule — see §5.7 |
| 8 | Assuming the four image chains (§1.5) are symmetric between input and output | they are input-only today; §5.3 explicitly requires you to verify rather than assume when adding the pixel node's aux **output** |

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte

# 1. builds clean
cmake --build build -j"$(sysctl -n hw.ncpu)"

# 2. every Field harness, not just this step's
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST \
         FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log   # must print 0 for every one
done

# 3. this step's own fixture (add it — model it on INFINITE_FIELDPARAMTEST's
#    early-exit shape for the pure parts, and INFINITE_ROUNDTRIPTEST's
#    in-frame shape for the save/load/undo parts)
env INFINITE_FIELDPINSTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/fieldpins.log
grep -c FAIL /tmp/fieldpins.log   # must print 0

# 4. the static wiring check for the four image chains
python3 .claude/skills/cable-logic-sweep/check.py

# 5. the full gate
.claude/skills/run-infinite-hygiene/driver.sh

# 6. project convention
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDPINSTEST` must assert, for **each of the four node types**:

1. toggle off → on: `OutputCount()`/`ModulatorInputCount()` grows by exactly
   1, pin 0's identity is unchanged, and the new pin appears with a correct
   label.
2. wire a cable to the new pin, save, load: the cable reconnects to the same
   pin, not pin 0.
3. wire a cable to the new pin, attempt to toggle off: the toggle is
   **refused**, the `bool` param reverts, the cable is still present, and
   `OutputCount()`/`ModulatorInputCount()` is unchanged — assert all four in
   the same check, not just the toggle's own value.
4. with the cable removed, toggle off succeeds and the pin disappears.
5. undo across step 1 restores the pre-toggle pin count and, if a cable was
   attached before the toggle, restores that cable too, in one undo step.
6. copy/paste a node mid-toggle-on preserves its pin count.
7. an old (pre-step-11) saved patch containing a `FieldElementNode` or
   `FieldSampleNode` still loads with exactly its old pin shape and the new
   `bool` defaulted false — regression-test this against a fixture patch
   file checked into the corpus, not just asserted in code.

Register `FIELDPINSTEST` in `TIER1_CHECKS` (`driver.sh:79`, re-grep first —
this number drifts every step) since it is a headless/early-exit-capable
fixture like its siblings.

---

## 8. Out of scope for this step

| Not in step 11 | Where it lands |
|---|---|
| Kernel-declared pin syntax (`output <domain> <type> <name> = <expr>` or similar) | step 12 |
| Domain-inference extension for multiple declared outputs on one kernel | step 12 |
| Arbitrary kernel-driven combination of geometry/audio/image pins on one node (the "two geo inputs and one audio input" example) | steps 12–13 |
| `FieldGraphNode` gaining kernel-declared typed pins beyond the one hardcoded trigger input | step 14 |
| Any change to `Expression::Evaluate`'s signature or its three call sites (`src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179`) | never |
| A second cross-thread channel | never — this step reuses `MeterRing`/`ModulatorOutput`, adding neither an atomic nor a queue |

If you find a genuine bug in the existing `reduce.rms`/`MeterRing`/
`ModulatorOutput` machinery while implementing this step, **report it rather
than fixing it inline** — it predates this step and is out of scope to fix
here.
