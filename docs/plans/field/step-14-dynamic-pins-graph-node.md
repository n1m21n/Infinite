# Field build step 14 — dynamic pins, Phase 2c: FieldGraphNode

You are implementing **build step 14 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). Self-
contained brief; no prior context assumed beyond "Files to read first".
Line numbers are from `src/` at the commit this was written against —
re-grep the symbol if a number has drifted; the symbol wins.

**Prerequisite:** `feature/field-step-13-dynamic-pins-node-wiring` merged,
and its exit criterion green. This is the last step in the dynamic-pins
sequence (steps 11-14) and closes out decision 2 (all four Field node
types get dynamic pins, not just three).

---

## 1. Invariants

### 1.1 Clean room, no sigils, one-branch-one-step

Same as every prior step. `git checkout feature/field-step-13-dynamic-pins-node-wiring && git checkout -b feature/field-step-14-dynamic-pins-graph-node`. Run
every Field harness (step 13 §1.3's loop, plus this step's own
`FIELDPINGRAPHTEST`), not only this step's own fixture.

### 1.2 `FieldGraphNode` is structurally unlike the other three — restated from step 11 §5.5, now load-bearing

Step 11 §5.5 first drew this line, for a single hardcoded trigger pin. This
step is where the distinction actually matters, because it governs
**everything** about how this node type's dynamic pins work, and getting it
wrong would silently reintroduce `emit()` as a second, competing pin
mechanism. State it in full, once, here:

| | The other three node types (Element/Sample/Pixel) | `FieldGraphNode` |
|---|---|---|
| When the kernel runs | every cook (`CookIfNeeded`, every frame or every audio block) | **once**, only when `Regenerate()` is explicitly called — `main.cpp:5089`'s own comment says it outright: `fgn->Apply(); // compile-only (T11) - never Regenerate() from here` |
| What the kernel produces | a typed value per declared output pin, read by downstream nodes over an ordinary cable | **side effects on the node graph itself** — `Mount`/`Unmount`/`SetParam`/`Connect`/`Place` calls against `IFieldGraphHost` (`FieldGraphHost.h:19-33`), diffed and reconciled by `FieldGraphReconciler` |
| What a "pin" would mean | a value crossing a domain boundary at compile/cook time (step 12 §1.3) | there is no per-cook value to expose — `graph` is a pure source, never a sink (step 10 §5.2, restated in step 12 §5.7) |
| The `output`/`input` declarations from step 12 | legal, collected into `declaredOutputs`/`declaredInputs` | **refused at parse time** inside a `graph`-domain kernel body (step 12 §5.7) — a graph kernel's `emit`/`connect`/`set`/`place` calls are not expressible as `output`/`input` statements and must not be conflated with them |
| This step's actual deliverable | N/A (done in step 13) | ordinary `INode` pins **on the `FieldGraphNode` itself**, declared **outside** the graph-domain kernel body, that do not touch `GraphIRProgram::declaredParams` or `IRStmtKind::Emit/Connect/SetParam/Place` at all |

**The one-sentence version, worth keeping verbatim in any future summary of
this sequence:** *dynamic pins on `FieldGraphNode` are ordinary node-level
input/output plumbing that happens to live on a graph-domain node; `emit()`
is a completely different mechanism, at a different time (edit time vs.
compile/cook time), producing a completely different kind of artifact
(other `INode`s vs. typed values) — the two must never share a declaration
syntax, an IR statement kind, or a reconciliation pass.*

### 1.3 Cable-orphaning refusal applies here too

Same discipline as step 13 §5.3, applied to `FieldGraphNode`'s own pins
(§5.1 below). A `Regenerate()` (not `Apply()` — see §5.2) that would retire
a wired pin is refused, keeping the previous mounted-node state.

---

## 2. Goal

Give `FieldGraphNode` its own dynamic pins — declared, per decision 1-3, in
a way that (a) does not touch the graph-domain kernel's own grammar or IR
(`GraphIRProgram`, `IRStmtKind::Emit/Connect/SetParam/Place`, all untouched
by this step), (b) follows the same append-only `PinTable`-backed identity
and cable-orphaning-refusal shape steps 12-13 already built for the other
three node types, and (c) closes decision 2's requirement that all four
Field node types support dynamic pins, not three. Concretely: extend step
11's single hardcoded `addTriggerInput` bool-gated pin into a small,
kernel-adjacent (not kernel-body) declaration mechanism for
**modulator-typed input pins only** — the one pin kind that makes sense on
a node whose only "cook" is an edit-time, side-effecting regeneration.

---

## 3. Files to read first

### Docs

| File | Why |
|---|---|
| `docs/plans/field/step-10-graph-domain.md` | the whole `FieldGraphNode`/`FieldGraphReconciler`/`IFieldGraphHost` design — read in full, this step builds directly on top of it |
| `docs/plans/field/step-11-dynamic-pins-phase1.md` §5.4, §5.5 | the hardcoded `addTriggerInput` pin and the first statement of the emit()-vs-pins distinction this step extends |
| `docs/plans/field/step-12-dynamic-pins-ir.md` §5.7 | why `output`/`input` are refused inside a graph-domain kernel body — the reasoning this step's design must stay consistent with |
| `docs/plans/field/step-13-dynamic-pins-node-wiring.md` §5.1, §5.3, §5.4 | the `PinTable` reconcile/refuse/save-format shape this step reuses, adapted for a node whose "compile" (`Apply()`) and "cook" (`Regenerate()`) are two separate calls instead of one |

### Real source — verified current

| File | Lines | Why |
|---|---|---|
| `src/nodes/FieldGraphNode.h` | class header `:18`; `Apply()` `:33` (compile-only, `main.cpp:5089`'s comment: "never Regenerate() from here"); `Regenerate(Field::IFieldGraphHost&)` `:44` (the only path that mounts/unmounts/wires — main-thread-only, wraps its own undo checkpoint) | the two-call split this step's pin mechanism must respect — see §5.2 |
| `src/core/field/FieldGraphHost.h` | `IFieldGraphHost` interface, `:12-40+`: `Mount`, `Unmount`, `SetParam`, `Connect`, `Place`, `Alive`, `TypeNameOf` | the seam `Regenerate()` drives — this step's pins never call through this interface, only ordinary `INode` pin accessors do |
| `src/core/field/FieldIR.h` | `enum class IRStmtKind` `:69-84` (`Emit`, `Connect`, `SetParam`, `Place` at `:80-83`, explicitly commented "kept as IRStmt variants rather than a fifth IR node type"); `struct GraphIRProgram` `:175-179` (`statements`, `declaredParams` only — **no** `declaredOutputs`/`declaredInputs`, confirming step 12 §5.7's refusal is already reflected in the type shape) | confirms this step must not add fields here |
| `src/main.cpp` | `RunFieldGraphRegenerate` `:27774-27781` (wraps `Regenerate()` as exactly one undo step); the `Regenerate()` call sites at `:47990`/`:48003`/`:48014`/`:48023` | where this step's new trigger-pin-driven regenerate call is added, alongside the existing UI-button-driven ones — same undo wrapping, no new undo path |
| `src/core/field/ParamTable.h` | whole file (57 lines) | the `PinTable`-adjacent identity scheme this step reuses again, for the small pin set defined below |

---

## 4. Files to create / modify

### Create

None.

### Modify

| Path | Change |
|---|---|
| `src/nodes/FieldGraphNode.h` / `.cpp` | own a `Field::PinTable mInputPins;` (no `mOutputPins` — see §5.1, `FieldGraphNode` has no output pins in this step); override `ModulatorInputSlot`; add a declaration mechanism for named modulator-input pins, parsed **outside** the graph-domain kernel body (§5.3) |
| `src/main.cpp` | the `Regenerate()` call sites (`:47990` neighbourhood) gain a check: an edge-triggered declared input pin whose value just rose is treated as an additional, equivalent trigger to the existing UI button — no new chain, no new dynamic_cast (§5.2 explains why: modulator inputs are already generically dispatched) |

### Must not be modified

- `src/core/field/FieldIR.h`'s `GraphIRProgram`, `IRStmtKind` — this step
  adds zero graph-domain-kernel grammar.
- `src/core/field/FieldGraphReconciler.*`, `FieldGraphKernel.*`,
  `FieldGraphHost.*` — the emit/connect/set/place machinery is completely
  untouched by this step, by design (§1.2).
- The four `main.cpp` image-pin chains — `FieldGraphNode` has no image
  pins, declared or native.

---

## 5. Procedure

### 5.1 What kind of pin makes sense here, and why it's a small, closed set — not the general step-12 grammar

Per §1.2, `FieldGraphNode` has no per-cook typed value to publish, so
**declared `output` pins make no sense on this node type at all** — there
is nothing to compute and no domain to compute it in. That leaves only
**inputs**, and only ones whose semantics fit an edit-time, side-effecting
regeneration rather than a per-cook value read:

| Declared pin kind | Meaning | Backing mechanism |
|---|---|---|
| `trigger <name>` | an edge-triggered modulator input; a rising edge calls `RunFieldGraphRegenerate` exactly as the existing UI "Regenerate" button does | generalizes step 11 §5.4's single hardcoded `addTriggerInput` bool to N named triggers |
| `modparam <domain> <name>` | a modulator input whose **current value**, not just its edge, is readable from *inside* `set()` calls in the kernel body at regenerate time — e.g. `set(child, "amount", modparam.level)` | new in this step; the one place a value legitimately flows *from* an ordinary pin *into* the graph-domain kernel, because `set()` already accepts a runtime value today (step 10) and this just gives it a named external source instead of only a literal/param |

Both are **modulator-typed** pins — `IModulator`/`ModulatorInputSlot`, the
same generic mechanism every other Field node type's modulator inputs
already use (`field-integration` §6). **No image, audio, or geometry pin
kind is legal on `FieldGraphNode` in this step** — refuse any declaration
using one of those, at parse time, with a message pointing at the fact that
`FieldGraphNode` has no per-cook value of any of those kinds to attach to.
This is a deliberately narrow set, matching the brief's own suggestion that
`FieldGraphNode` "possibly comes last / excluded" — it is not excluded per
decision 2, but its dynamic-pin surface is legitimately smaller than the
other three node types', because most pin *kinds* genuinely do not apply
here.

### 5.2 Declaration syntax lives outside the kernel body, not inside it

Per §1.2's table, `output`/`input` are refused *inside* a graph-domain
kernel body. `trigger`/`modparam` declarations are **not** graph-domain
statements at all — they are node-level metadata, parsed from a **separate
declaration block** that precedes the kernel body, using the same lexer but
a different top-level parse entry point (mirroring how `param`/`attrib`
declarations already sit alongside ordinary statements in the other three
domains, except here they are lifted fully outside the `graph` kernel
rather than interleaved with it, since nothing about them is graph-domain
code):

```
# declared pins - parsed before the graph-domain body, not part of it
trigger regenerateNow
modparam frame float childAmount

# ordinary graph-domain kernel body starts here
emit("Oscillator", key: "osc1")
set(osc1, "amount", modparam.childAmount)
```

`FieldGraphNode::Apply()` (`FieldGraphNode.h:33`, compile-only, never calls
`Regenerate()`) is extended to also parse this declaration block and
reconcile `mInputPins` from it — **this still never touches the live
graph**, exactly as its existing doc comment promises ("never mutates the
graph - safe to call from any path that just needs the node's compiled
state refreshed"). Reconciling a `PinTable` is node-local bookkeeping, not
a graph mutation, so this is consistent with `Apply()`'s existing contract
without needing to change it.

### 5.3 The refuse condition — adapted for the two-call split

Step 13 §5.3's refuse condition assumed `Apply()` both compiles and commits
in one call. Here, `Apply()` only compiles/reconciles `mInputPins`;
`Regenerate()` is the one that would actually disturb anything live (mounted
nodes, not cables — this node type's "orphaning" risk is different in kind).
Adapt as follows:

- **`Apply()`'s refusal** (step 13 §5.1 shape, reused as-is): if a
  `trigger`/`modparam` declaration that has a live modulator cable wired to
  it is removed or retyped, refuse — keep the previous `mInputPins` and the
  previous compiled declaration set, surface the error via `LastError()`,
  exactly like every other Field node's `Apply()`.
- **`Regenerate()` needs no additional refusal of its own for pins** — it
  already has its own, separate reconciliation problem (mounted child nodes,
  handled by `FieldGraphReconciler`, untouched by this step) and does not
  interact with `mInputPins` at all beyond reading a trigger's edge state or
  a `modparam`'s current value at the moment it runs. Do not conflate the
  two refusal mechanisms — a `FieldGraphNode` mid-refusal on its *pins* still
  runs its *last successfully compiled* trigger/modparam declarations against
  `Regenerate()`, exactly as a Element/Sample/Pixel node mid-refusal keeps
  running its last successfully compiled program.

### 5.4 Wiring the trigger into the existing `RunFieldGraphRegenerate` path

`RunFieldGraphRegenerate` (`main.cpp:27774-27781`) already wraps
`Regenerate()` as exactly one undo step and is already called from multiple
UI sites (`:47990`, `:48003`, `:48014`, `:48023` — re-verify these on read,
they are UI-button-adjacent call sites and likely to drift). Add one more
call site: once per frame, for each `FieldGraphNode` with at least one
`trigger`-kind declared pin, check whether that pin's `ModulatorInputSlot`'s
current value just crossed the existing edge-detection threshold this
codebase already uses elsewhere for other edge-triggered modulator inputs
(grep for the pattern rather than inventing a new one — trigger/gate
edge-detection already exists for other node types, e.g. drum sequencer
gate handling) — and if so, call `RunFieldGraphRegenerate(node)` exactly as
the button click does. **No new dynamic_cast chain**: this is possible
without touching `main.cpp`'s four image chains at all, because
`ModulatorInputSlot` is already a generic `INode` virtual (§1.2's table,
`field-integration` §6) — the same pleasant-surprise shape step 13 §5.2
found for geometry/audio declared pins on the other three node types.

### 5.5 Save/load/undo — same shape as step 13 §5.4/§5.5, smaller surface

`mInputPins.SerializePinMap()` rides along via `VisitParams`, same as step
13. Undo/redo/copy-paste are generic through `VisitParams`, same as step
13 — no new per-node entry to any of those paths. The one `FieldGraphNode`-
specific wrinkle: its `Uid()` (`FieldGraphNode.h`, "16 lowercase hex chars,
regenerated only when this node's identity must diverge from a source
node's — paste") is unrelated to `PinTable`'s ids and must not be conflated
with them — a paste that regenerates `Uid()` for graph-identity purposes
does **not** need to regenerate `PinTable` ids; the pasted node's pin
identity is copied verbatim, exactly like step 13's exit criterion 9 for
the other three node types.

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Adding `output`/`declaredOutputs` support to `GraphIRProgram` "for consistency" with the other three node types | `graph` has no per-cook value — this would resurrect exactly the confusion §1.2 exists to prevent. `FieldGraphNode` has no output pins in this step, period |
| 2 | Implementing `trigger`/`modparam` as new `IRStmtKind` variants inside the graph-domain grammar | they are node-level metadata parsed *outside* the kernel body (§5.2) — adding them to `IRStmtKind` would make them indistinguishable from `emit`/`connect`/`set`/`place` at the type level, which is precisely the conflation §1.2 forbids |
| 3 | Calling `Regenerate()` from `Apply()` to "make pins take effect immediately" | `Apply()`'s contract is explicitly compile-only and graph-mutation-free (`FieldGraphNode.h:33`'s own comment, and `main.cpp:5089`) — breaking that contract breaks every other path that calls `Apply()` expecting no side effects (paste, patch load, undo/redo) |
| 4 | Building a new edge-detection helper for the trigger pin instead of reusing the existing pattern elsewhere in the codebase | duplicated, possibly inconsistent threshold/hysteresis behavior — grep first (§5.4) |
| 5 | Adding entries to the four `main.cpp` image-pin chains for `FieldGraphNode` | it has no image pins, declared or native — nothing to add |
| 6 | Conflating `PinTable` pin identity with `FieldGraphNode::Uid()` | they solve different problems (cable addressing vs. graph-node-instance identity across paste) and regenerating one must never imply regenerating the other (§5.5) |

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte
cmake --build build -j"$(sysctl -n hw.ncpu)"

for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST FIELDSAMPLETEST \
         FIELDGRAPHTEST FIELDGRAPHRATETEST FIELDGRAPHUNDOTEST FIELDGRAPHBLASTTEST \
         FIELDPINSTEST FIELDPINDECLTEST FIELDPINNODETEST FIELDPINGRAPHTEST; do
  env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tee /tmp/$v.log | tail -5
  grep -c FAIL /tmp/$v.log
done

python3 .claude/skills/cable-logic-sweep/check.py
.claude/skills/cable-logic-sweep/driver.sh
.claude/skills/run-infinite-hygiene/driver.sh
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`INFINITE_FIELDPINGRAPHTEST` (new) must assert:

1. a `FieldGraphNode` with no `trigger`/`modparam` declarations behaves
   identically to today — `Regenerate()` only ever runs from the existing
   UI-button call sites;
2. `output`/`input` written inside the graph-domain kernel body is refused
   at parse time, citing `emit`/`connect`/`set`/`place` (step 12 §5.7's
   message, re-verified still fires for this node type specifically);
3. declaring `trigger regenerateNow` adds exactly one modulator input pin;
   driving its wired modulator source across the edge threshold calls
   `Regenerate()` exactly once per rising edge, wrapped as exactly one undo
   step (`RunFieldGraphRegenerate`'s existing contract, unmodified);
4. declaring `modparam frame float childAmount` and referencing
   `modparam.childAmount` inside a `set(...)` call in the kernel body
   resolves to that pin's current value at the moment `Regenerate()` runs,
   not at `Apply()` time;
5. removing a `trigger`/`modparam` declaration that has a live wired cable:
   `Apply()` is refused, previous declaration set and `mInputPins` are
   unchanged, error names the pin;
6. an `image`/`audio`/`geometry`-typed declaration on `FieldGraphNode` is
   refused at parse time with a message naming why (§5.1);
7. save → load → undo → copy/paste round-trip `mInputPins` exactly, and
   `Uid()` regeneration on paste (existing behavior) does not disturb pin
   identity (§5.5).

---

## 8. Out of scope for this step

| Not in step 14 | Where it lands / status |
|---|---|
| Output pins of any kind on `FieldGraphNode` | not planned — §1.2/§5.1, there is no per-cook value to publish |
| Image/audio/geometry-typed pins on `FieldGraphNode` | not planned — §5.1's set is deliberately closed |
| Changing `emit`/`connect`/`set`/`place`'s own grammar or `FieldGraphReconciler`'s diff algorithm | untouched, by design, through this entire four-step sequence |
| A `resample`-style crossing that lets a graph-domain kernel read a `frame`-domain value without going through a declared `modparam` pin | not proposed; `modparam` is the only sanctioned entry point for an external value into a graph-domain kernel |
| Any change to `Expression::Evaluate`'s signature or its three call sites | never |

---

## 9. This closes the dynamic-pins sequence (steps 11-14)

All four Field node types (`FieldElementNode`, `FieldSampleNode`,
`FieldPixelNode`, `FieldGraphNode`) now support dynamic pins, kernel-
declared where a per-cook value exists (Element/Sample/Pixel, steps 12-13)
and declaration-adjacent where it does not (Graph, this step) — matching
decision 2 in full. The cable-orphaning refusal (decision 4) is implemented
end to end, not just decided. Save-format addressing (open question 4) is
resolved via `PinTable`, reusing `ParamTable`'s already-shipped identity
scheme rather than inventing a second one. The `emit()`-vs-dynamic-pins
distinction (open question raised in the original brief) is stated three
times with increasing stakes across steps 11, 12, and this step, and is
structurally enforced, not just documented: `GraphIRProgram` has no
`declaredOutputs`/`declaredInputs` fields, and `output`/`input` cannot
parse inside a graph-domain kernel body at all.
