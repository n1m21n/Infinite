# Field build step 13 — dynamic pins, Phase 2b: node, UI and save-format wiring

You are implementing **build step 13 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against —
re-grep the symbol if a number has drifted; the symbol wins, not the number.

**Prerequisite:** `feature/field-step-12-dynamic-pins-ir` merged, and its
exit criterion (`docs/plans/field/step-12-dynamic-pins-ir.md` §7) green.
Step 12 built `output`/`input` declarations, IR collection, and `PinTable` —
none of it wired into a node yet. This step wires it into
`FieldElementNode`, `FieldSampleNode`, `FieldPixelNode`. **`FieldGraphNode`
is step 14**, not this step — its pin story is different enough (§5.7 of
step 12) to need its own doc and its own restatement of the emit()
distinction.

---

## 1. Invariants

### 1.1 Clean room, no sigils

Same as every prior step (`step-12` §1.1). Not restated in full here.

### 1.2 One step, one branch, one commit

```
git checkout feature/field-step-12-dynamic-pins-ir
git checkout -b feature/field-step-13-dynamic-pins-node-wiring
```

Commit only when §7's exit criterion is green. Do not batch this with
step 14 — `step-09-sample-domain.md` §0.1 records the concrete failures
(silent rate-inference defeat, cycle-checker false positives, a null-deref
segfault, duplicate opcode interpreters) that came from batching steps on
one branch in this exact project.

### 1.3 Run every Field harness, not only this step's own

```bash
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST \
         FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST \
         FIELDPINSTEST FIELDPINDECLTEST FIELDPINNODETEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done
```

`FIELDPINNODETEST` is this step's own new fixture (§7). Register it in
`TIER1_CHECKS` in `.claude/skills/run-infinite-hygiene/driver.sh` — re-grep
the array's current line, it moves every step (step-09 cited `:173`, step-11
found it at `:180` — check again, do not trust either number).

### 1.4 The four hand-maintained image-pin chains — verified current lines

```bash
grep -n "int InputCountFor\|ImageCable\* CableFor\|bool IsInputSlotCompatible\|void WireInputSlot" src/main.cpp
```

confirms, at the time of writing:

| Chain | Line | Role |
|---|---|---|
| `InputCountFor` | `main.cpp:4160` | how many pins to draw |
| `CableFor` | `main.cpp:4300` | which `ImageCable*` a slot is |
| `IsInputSlotCompatible` | `main.cpp:4488` | what a slot accepts |
| `WireInputSlot` | `main.cpp:4565` | performs the connection |

This step adds entries to all four for a **kernel-declared image-typed pin**
(a `FieldPixelNode`'s `input pixel image <name>` aux input, or a `FieldElementNode`/
`FieldSampleNode` that somehow declares an `image`-typed pin — refused per
step 12 §5.6's table, so in practice only `FieldPixelNode` needs entries
here). Audio/note/geometry/modulator declared pins do **not** need entries
here — `INode`'s generic virtual accessors already dispatch those (§5.2).
Run `.claude/skills/cable-logic-sweep/check.py` after touching any of the
four; it proves every class declaring an `ImageCable` is named in `CableFor`
and that pin counts and cable-kind overrides line up.

### 1.5 Failing Apply() changes nothing — extended to a real diff, not a toggle

Step 11 §1.6 extended `FormulaNode::Apply()`'s keep-last-working-program
discipline to a single hardcoded toggle. This step extends it to the actual
kernel-driven case: **any** edit to a Field node's source text that would
retire (§5.4 of step 12) a `PinTable` entry with a live cable must leave the
node running its previous compiled program, previous `PinTable`, and
previous pin count — completely unchanged — with the new source text's
compile error (in this case, a synthesized "would disconnect pin `X`, still
wired" message, not a syntax error) surfaced via the node's existing
`LastError()`/equivalent path.

---

## 2. Goal

Make `FieldElementNode::OutputCount()`/input-slot accessors,
`FieldSampleNode::OutputCount()`/input-slot accessors, and
`FieldPixelNode`'s image-pin entries in the four `main.cpp` chains all
**driven by the last successfully compiled program's `PinTable`**, instead
of the fixed native shape (Element: geo in/out; Sample: note+audio in, audio
out; Pixel: image in/out) or step 11's single hardcoded toggle. Wire
`Patch::CableRecord` addressing through `PinTable` ids so cables survive a
kernel edit that reorders or adds declarations without touching unrelated
ones (brief's open question 4, save-format migration — resolved here, not
deferred). Implement the accept/refuse decision step 12 §5.5 stopped short
of (the node layer is where cables are visible). Prove undo/redo/copy-paste
correctness for a pin *count that changes on an ordinary kernel edit*, not
just a toggle flip.

---

## 3. Files to read first

### Docs

| File | Why |
|---|---|
| `docs/plans/field/step-12-dynamic-pins-ir.md` | everything this step consumes: `PinTable`, `DeclaredOutput`/`DeclaredInput`, the diff contract of §5.5 |
| `docs/plans/field/step-11-dynamic-pins-phase1.md` §5.1-§5.4, §5.6 | the per-node-type menu entries this step's kernel-declared mechanism sits **alongside** — native default pins stay the floor (decision 3), declared pins are additive/subtractive from there, and step 11's own hardcoded toggle-pin should now be re-expressible as a one-line kernel declaration (§5.6 below) rather than removed |
| `docs/plans/field/design-brief-dynamic-pins.md` | open questions 4 (save-format) and 5 (cable-orphaning) — this step is where both get implemented, not just decided |

### Skills

| File | Why |
|---|---|
| `.claude/skills/cable-logic-sweep/SKILL.md` | the full connection matrix and the four chains — read before touching any of them |
| `.claude/skills/field-integration/SKILL.md` §5, §6 | the `Patch` line grammar, `ParamVisitor`'s five methods, the one-index-space rule for input slots |
| `.claude/skills/node-ui-pillars/SKILL.md` | load before drawing any pin row — symmetry and dark-mode contrast are non-negotiable |

### Real source — verified current

| File | Lines | Why |
|---|---|---|
| `src/core/INode.h` | `OutputCount()` `:137`, `OutputLabel(int)` `:138`, `ModulatorOutput(int)` `:143`, `IsAudioOutputIndex` `:47`, `GeometryInputSlot`/`ModulatorInputSlot`/`AudioInputSlot`/`NoteInputSlot` `:166-177` | the virtuals this step overrides with `PinTable`-driven answers |
| `src/core/Patch.h` | `struct CableRecord` `:72-83` — `dstIndex`, `dstSlot`, `srcIndex`, `srcOutput` (comment at `:77-81` already documents `srcOutput`'s only two prior users: Note Router and `VideoSourceNode`) | this step becomes the third user of `srcOutput` addressing >1 output, and the first to need **stable identity** behind that bare int — see §5.4 |
| `src/main.cpp` | `InputCountFor:4160`, `CableFor:4300`, `IsInputSlotCompatible:4488`, `WireInputSlot:4565` (re-verified current, §1.4) | the four chains |
| `src/nodes/FieldElementNode.h` / `.cpp` | class header `:13`, `Apply()` `:61` | where `PinTable`-driven `OutputCount()`/geometry-input-slot accessors are added |
| `src/nodes/FieldSampleNode.h` / `.cpp` | class header `:22`, `Apply()` `:49` | same, for audio in/out |
| `src/nodes/FieldPixelNode.h` / `.cpp` | class header `:13`, `Apply()` `:45` | same, for image in/out, plus the four `main.cpp` chains |
| `src/core/field/PinTable.h` | the whole file (new in step 12) | `Reconcile`, `Pins()`, `Find`, `SerializePinMap`/`DeserializePinMap` |
| `src/nodes/GeometryTableNode.h` | `OutputCount() { return 4 + 3 * RowCount(); }` `:45`, its append-only save-format comment `:51` | the existing precedent for a genuinely variable `OutputCount()` driven by node state — re-read it, this step is the second node type to do this for real (step 11 only faked it with a bool) |
| `src/nodes/MacroNodes.h` | `OutputCount()==2` override `:49`, `ModulatorOutput(1)` `:54` | the precedent for "declared output N maps to `ModulatorOutput(N)`" |

---

## 4. Files to create / modify

### Create

None — this step wires existing pieces together; no new class.

### Modify

| Path | Change |
|---|---|
| `src/nodes/FieldElementNode.h` / `.cpp` | own a `Field::PinTable mOutputPins, mInputPins;` pair; `Apply()` reconciles both after a successful compile, checks the refuse condition (§5.3) before committing; `OutputCount()`, `OutputLabel`, `ModulatorOutput`, `GeometryInputSlot`/`AudioInputSlot` overridden to read `PinTable` |
| `src/nodes/FieldSampleNode.h` / `.cpp` | same shape, for the sample domain's separately-implemented declared-pin lists (step 12 §1.6) |
| `src/nodes/FieldPixelNode.h` / `.cpp` | same shape, for image in/out; additionally touches the four `main.cpp` chains (§5.6) |
| `src/main.cpp` | `InputCountFor:4160`, `CableFor:4300`, `IsInputSlotCompatible:4488`, `WireInputSlot:4565` — one new branch each, for a `FieldPixelNode` with a declared `image`-typed aux input |
| `src/core/Patch.h` / `.cpp` | no struct change (`CableRecord.srcSlot`/`dstSlot`/`srcOutput` already generic ints) — but `Patch::Save`/`Load`'s Field-node section gains the `PinTable`'s `SerializePinMap`/`DeserializePinMap` payload, via the existing `Text` param path (§5.4) |

### Must not be modified

- `FieldGraphNode.*` — step 14.
- `ParamTable.h`/`.cpp` — read-only precedent, not touched.
- `Expression::Evaluate`'s signature and its three call sites — never, per
  every prior step in this sequence.

---

## 5. Procedure

### 5.1 The shape every one of the three node types repeats

For `FieldElementNode` (do `FieldSampleNode` and `FieldPixelNode` identically
after this one is proven, per node type's own compiled-program type):

1. `Apply()` compiles the new source into a **local** `ElementIRProgram`
   (already the existing shape — `field-compiler` §7's "compile into a
   local, swap only on success").
2. On success, build the `DeclaredPin` list from the compiled program's
   `declaredOutputs`/`declaredInputs` (step 12 §5.2), **prefixed by the
   node's native default pins** (decision 3 — the default shape is the
   floor, never removed by a kernel; a `FieldElementNode`'s geo-in/geo-out
   are always `PinTable` entries whether or not the kernel declares
   anything, indices 0 in each direction, exactly matching step 11 §5.7's
   append-only rule).
3. Call `mOutputPins.Reconcile(...)` and `mInputPins.Reconcile(...)` **into
   local copies**, not the live member tables yet — `PinTable::Reconcile`
   mutates in place (step 12 §5.4), so reconciling into a scratch copy of
   the live table is required to inspect the diff before committing.
4. Diff the scratch copy against the live table: any entry whose
   `isDeclared` just flipped `true→false` is a **candidate refusal**. For
   each candidate, ask the node's own cable state (this is the one thing
   the compiler layer could not do, per step 12 §5.5) — is there a live
   `CableRecord` in the current `Patch` addressed to this node's pin id at
   this slot? (Concretely: walk `gPatch.cables`/`gPatch.mods` for a record
   whose `(dstIndex, dstSlot)` or `(srcIndex, srcOutput)` matches this
   node's index and this pin's **current slot** — not its `PinTable` id;
   the live cable is still addressed by slot until this Apply() commits,
   per §5.4.)
5. If any candidate is wired: **refuse the whole Apply()**. Discard the
   local `ElementIRProgram`, discard both scratch `PinTable` copies, keep
   the previous program/table/pin-count/cables exactly as they were, and
   set the node's error text to name every refused pin and its still-live
   cable (e.g. "kernel edit would remove output `bass`, which is wired to
   Mixer 2's input 3 — disconnect it first"). Return `false`. **Do not
   commit any part of the compile** — this is a whole-Apply() rollback, not
   a partial one; the same all-or-nothing rule `FormulaNode::Apply()`
   already follows for a syntax error.
6. If nothing candidate is wired: commit. Swap the live program, commit
   both scratch `PinTable`s into the live members, and — critically — **for
   every retired-but-previously-live pin id that is not wired**, its slot is
   now free; do not attempt to reuse the slot number for a different pin in
   the same Apply() (the append-only rule, step 11 §5.7 — a slot number,
   once assigned to a `PinTable` id, is never reassigned to a different id
   within the node's lifetime, even after that id retires. `OutputCount()`
   in step (7) below is the count of pins **still present** — retired ones
   are dropped from the visible count, not padded with a gap, exactly like
   the precedent this creates for `main.cpp`'s pin-drawing loop, which
   already iterates `0..OutputCount()` with no gap-handling anywhere).
7. `OutputCount()` returns `mOutputPins.Pins().size()` counting only
   entries where `isDeclared` is currently true, in `PinTable`'s own vector
   order (which is insertion order — append-only, per (6)). `OutputLabel(i)`
   returns that entry's `name`. `ModulatorOutput(i)` for `i > 0` returns a
   pointer to the compiled program's evaluated value for that declared
   output — same mechanism `MacroNodes.h:54` already uses for its second
   output, just generalized to N.

### 5.2 Input slots — geometry/audio declared pins use the existing generic accessors, not a new mechanism

Per `field-integration` §6: `INode`'s input slots share **one index space**
across kinds, and audio/note/geometry/modulator inputs are already
generically dispatched via `AudioInputSlot`, `NoteInputSlot`,
`GeometryInputSlot`, `ModulatorInputSlot` — **only image inputs need the
four `main.cpp` chains** (`cable-logic-sweep` §0's whole point). A
`FieldElementNode` with a declared `input element geometry other` (step 12
§5.6) needs `GeometryInputSlot(N)` to return non-null for the slot `PinTable`
assigned to `other` — **no `main.cpp` change**, because `GeometryInputSlot`
is already probed generically everywhere (`cable-logic-sweep`'s whole
premise). Likewise a declared `input sample audio sidechain` just needs
`AudioInputSlot(N)` answered.

**This is the one pleasant surprise in this step**: only `FieldPixelNode`'s
declared `image`-typed pins touch `main.cpp` at all. Confirm this by reading
`cable-logic-sweep`'s own table before writing code — do not add
`main.cpp` entries for geometry/audio declared pins; if you find yourself
about to, stop and re-read `IsInputSlotCompatible`'s evaluation order
(`cable-logic-sweep` §"connection matrix"), because a generic accessor
answering non-null is already sufficient at every one of its ten branches.

### 5.3 The refuse condition, precisely

Refuse the Apply() when, and only when: a `PinTable` entry (output or
input, either table) that was `isDeclared=true` before this compile is
`isDeclared=false` after it, **and** at least one `CableRecord` (image) or
`ModRecord`/audio-cable/note-cable (for the generically-dispatched kinds)
currently addresses that entry's pre-compile slot on this node. A pin whose
declaration is simply **unchanged** — same name, same type, same domain —
is never a refusal candidate, no matter how much of the rest of the kernel
changed around it. A newly-**added** declaration is never a refusal
candidate either (nothing was there to orphan). This mirrors
`ParamTable::Reconcile`'s existing three-way split (new / unchanged /
retired) exactly — reread `step-05-param-declarations.md` §5.5's worked
before/after table and use the identical shape for the worked example in
this step's own test fixture (§7).

### 5.4 Save-format — `PinTable`'s ids replace bare-slot addressing

The brief's sharpest risk: `CableRecord.dstSlot`/`srcOutput` are bare ints
with no name or type tag, so a kernel-declared output set changing between
saves could silently reconnect a cable to the wrong domain (design brief,
"the sharpest risk in the whole idea"). This step closes it, using exactly
`ParamTable`'s already-shipped answer to the identical problem for `param`s.

**On save:** unchanged — `CableRecord` still stores `dstSlot`/`srcOutput` as
plain ints, because `Patch.h`'s line grammar and every existing consumer
(the UI, `ConnectNodes`, undo/redo) already work in slot-space and changing
that format is a much larger migration than this step needs. What changes:
alongside the node's existing `Text` param carrying its Field source
(`field-integration` §5, the same path `FormulaNode::formula` already
uses), the node's `VisitParams` **also** emits two more `Text` params:
`__outputPins` and `__inputPins`, each holding
`mOutputPins.SerializePinMap()` / `mInputPins.SerializePinMap()` (step 12
§5.4's format — name, id, type, domain, isDeclared per entry, one line
each, following the same `EscapeLine`/`UnescapeLine` discipline
`field-integration` §5 already documents for multi-line text).

**On load:** the node's constructor path calls `Apply()` once (already the
existing behaviour for every Field node type — a fresh compile from saved
source), which naturally reconciles `PinTable` from empty against the
freshly-compiled declarations, in the exact same insertion order the file
was saved in **only if** `DeserializePinMap` runs first and seeds
`mOutputPins`/`mInputPins` with their saved id-to-name mapping **before**
that first `Apply()`'s `Reconcile()` call — otherwise a freshly-constructed
`PinTable` has no memory of prior ids and would (harmlessly, but
pointlessly) mint new ones matching the same names anyway, since
`Reconcile`'s id-minting only triggers for names it has never seen (step 12
§5.4). **Do `DeserializePinMap` first regardless** — it is what makes a
*retired-but-still-referenced-by-an-old-cable* id survive a save/load round
trip even though the current kernel no longer declares it, which is exactly
the case a stale cable's `CableRecord` still points at by slot number: the
slot layout on load must reconstruct identically to how it was at save
time, gaps included, or an old cable's bare `dstSlot` reconnects to the
wrong pin. This is the concrete mechanism that makes bare-slot addressing
safe again — the slot numbers are stable because `PinTable`'s insertion
order is stable across a save/load round trip, not because the addressing
scheme itself changed.

**Old, pre-step-13 patches** (the brief's open question 4): such a patch has
no `__outputPins`/`__inputPreset` params at all. `DeserializePinMap` on an
absent/empty string must produce an empty table, and the first `Apply()`
after load reconciles it fresh — since every existing saved Field kernel
(pre-step-11, pre-step-12) has zero declared pins by construction (nobody
could write `output`/`input` before step 12 existed), this reconciles to
exactly the native default pins at their native slots, identical to today's
behaviour. **No migration code is needed beyond "absent string means empty
table"** — verify this with a corpus fixture (§7).

### 5.5 Undo/redo/copy/paste

Per `field-integration` §5: these are generic and go entirely through
`VisitParams`. Since `PinTable`'s two `Text` params are now part of
`VisitParams`, a `PinTable` state (including retired entries) rides along
with every undo/redo/copy/paste snapshot automatically — **do not** add a
per-node entry to any of those paths; doing so is a sign of a design
mistake per `field-integration` §5's own explicit warning. The one thing to
verify by hand (not by construction) is that undo restores the **compiled
program**, not just the saved text and pin table — i.e. after an undo that
reverts a kernel edit, `OutputCount()` must immediately reflect the reverted
kernel's pin shape, which requires undo's restore path to trigger the same
`Apply()`/`CookIfNeeded` recompile every other param-restore already
triggers. Confirm this against whichever generic "params changed, mark
dirty" mechanism the undo restore path already uses for e.g. `FormulaNode`'s
`formula` text param — Field nodes should need zero special-casing here.

### 5.6 Re-expressing step 11's hardcoded toggles as ordinary declarations

Step 11 shipped one hardcoded bool-gated extra pin per node type
(`publishScalarOutput`, `exposeRmsOutput`, `exposeAuxTexture`,
`addTriggerInput`) precisely to prove the plumbing before the compiler
existed. Now that it does: **leave step 11's toggles in place, unchanged** —
they are a separate, still-legal path (a node can have both a toggle-driven
pin and kernel-declared ones; §5.1 step 2's "prefixed by native default
pins" list should also include any step-11 toggle pins that are currently
on, ahead of declared ones, keeping the append order native → toggle →
declared). Do not remove or migrate them in this step; that is a UX
decision (whether the toggle becomes redundant now that a user can just
write `output frame float bass = reduce.rms(in, 20, 200)` themselves) for
the owner to make later, explicitly out of scope here (§8).

### 5.7 `FieldPixelNode`'s image-typed declared pins — the four `main.cpp` chains

Only this node type needs entries here (§5.2). Add, in each of the four
chains, a branch recognizing a `FieldPixelNode*` with a declared `image`-
typed input pin (found via `dynamic_cast<FieldPixelNode*>(&gn)` then a
`PinTable` lookup for an input entry beyond the native `src` slot 0):

| Chain | New branch does |
|---|---|
| `InputCountFor:4160` | returns `1 + node->DeclaredImageInputCount()` instead of the hardcoded `1` |
| `CableFor:4300` | for `slot > 0`, returns the `ImageCable*` backing that declared input (a new `std::vector<ImageCable>` member on `FieldPixelNode`, parallel to its existing single-cable member — read how `VideoSourceNode` or another multi-cable node already shapes this before inventing a new pattern) |
| `IsInputSlotCompatible:4488` | falls through to the existing image catch-all (rule 10, `cable-logic-sweep`'s table) — **no new branch needed** here, since an image-into-image connection is already legal by the generic rule; only add a branch if a declared image input needs to *refuse* something the generic rule would accept (it does not, per step 12 §5.6's table being deliberately narrow) |
| `WireInputSlot:4565` | performs the connection into the right `ImageCable` from the new vector, mirroring the existing single-slot case |

Run `.claude/skills/cable-logic-sweep/check.py` immediately after — it
mechanically confirms `CableFor`'s branch and the pin count agree for every
class declaring an `ImageCable`, catching exactly the kind of drift this
step risks introducing.

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Reconciling `PinTable` directly on the live member before checking for orphaned cables | makes the refusal (§5.1 step 5) impossible to implement — by the time you know a cable is orphaned, the table has already forgotten the old id existed. Always reconcile into a scratch copy first |
| 2 | Checking a candidate-retired pin's cable by `PinTable` id instead of its pre-compile **slot** | a live `CableRecord` is still addressed by slot until this Apply() commits (§5.4) — comparing against the new id (which does not exist in any saved cable yet) always finds nothing, silently defeating the refusal |
| 3 | Reusing a freed slot number for a newly-added declared pin in the same Apply() | breaks the append-only invariant (§5.1 step 6) the moment a save/load round trip happens between two edits — an old cable pointing at the reused slot would silently reconnect to the wrong pin |
| 4 | Adding `main.cpp` chain entries for declared **audio** or **geometry** pins | unnecessary — those kinds are already generically dispatched (§5.2); adding hand-written entries anyway creates a second, redundant, driftable code path for something that already works |
| 5 | Calling `Apply()` on load before `DeserializePinMap` has seeded the live tables | produces a table with fresh ids that happen to match by name but does not guarantee slot-order stability across the round trip — do `DeserializePinMap` first, always (§5.4) |
| 6 | Treating "no `__outputPins` param present" as an error on load | it is the expected shape for every pre-step-13 patch (§5.4) — treat as an empty table, not a migration failure |
| 7 | Letting undo restore the saved `PinTable`/text without triggering a recompile | leaves `OutputCount()` stale relative to the just-restored kernel until the next unrelated cook — verify the recompile actually fires (§5.5) |
| 8 | Removing step 11's hardcoded toggles as part of "cleaning up" during this step | explicitly out of scope (§5.6, §8) — that is a UX call for the owner, not an implementation detail |

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST \
         FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST \
         FIELDPINSTEST FIELDPINDECLTEST FIELDPINNODETEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done

python3 .claude/skills/cable-logic-sweep/check.py
.claude/skills/cable-logic-sweep/driver.sh
.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDPINNODETEST` (new, headless, spawns real nodes — no GL
context needed for Element/Sample; Pixel needs the existing headless GL
context this project's other pixel-domain fixtures already use) must
assert, per node type (Element, Sample, Pixel — three passes of the same
list):

1. spawning with no `output`/`input` declarations gives exactly the native
   default pin shape (step 11 §4's baseline column);
2. adding `output frame float x = ...` grows `OutputCount()` by one, at the
   next free slot, without disturbing slot 0;
3. wiring a cable to that new pin, then editing the kernel to remove the
   `output` line: Apply() is **refused**, `LastError()` names the pin and
   the still-live cable, `OutputCount()`/the cable/the previous compiled
   program are all byte-for-byte unchanged from before the edit;
4. removing the cable, then re-applying the same edit: Apply() **succeeds**,
   the pin is gone, `OutputCount()` drops by one;
5. renaming a declared output with no cable attached: succeeds, old name's
   slot is dropped, new name gets a fresh slot (not the old one) — per
   §5.4's retire-and-mint rule;
6. save → load round-trips: pin names, slots, and any live cable's
   `dstSlot`/`srcOutput` addressing are identical after load;
7. an **old, pre-step-13 corpus fixture patch** (a `.infinite` file saved by
   a build before this step existed, checked into the fixture corpus) still
   loads with its native pin shape and zero declared pins — proving §5.4's
   "absent string means empty table" path;
8. undo after a kernel edit that added a pin restores the previous
   `OutputCount()` immediately (no extra cook needed to notice);
9. copy/paste a node with at least one declared output and one retired
   (renamed-away) entry: the pasted node's `PinTable` state matches the
   original's, including the retired entry;
10. (`FieldPixelNode` pass only) a declared `image`-typed input pin appears
    in `InputCountFor`'s count, accepts a wired image cable, and
    `cable-logic-sweep/check.py` passes with it present.

---

## 8. Out of scope for this step

| Not in step 13 | Where it lands |
|---|---|
| `FieldGraphNode` gaining any pins at all | step 14 |
| Removing or migrating step 11's hardcoded per-type toggles | an explicit later UX decision, owner's call — not implied by this step existing |
| Unifying the sample backend with the shared typed IR | never planned in this sequence (step 12 §8) |
| A `resample`-based crossing for `geometry`↔`pixel` or `image`↔`element` declared pins | future work beyond step 12 §5.6's deliberately narrow table |
| Any change to `Expression::Evaluate`'s signature or its three call sites | never |
| A second cross-thread channel | never — `ParamMailbox` and `MeterRing` remain the only two |
