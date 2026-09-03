# Field build step 5 — `param` declarations become real knobs

You are implementing **build step 5 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

**Prerequisite steps that must already be finished and merged before you start:**

| Step | What it delivered | How to confirm |
|---|---|---|
| 1 | `Expression.cpp` split into lexer → AST → typed IR → bytecode, `Expression::Evaluate`'s signature and its three call sites unchanged | the compiler files exist; `INFINITE_FIELDTEST` A–C pass |
| 2 | `rand`/`noise`/`sh` pure in `(t, seed)` | random-set baseline recorded |
| 3 | `vec2/3/4` + rank polymorphism | `INFINITE_FIELDTEST` D passes |
| 4 | the `element` domain, `attrib`, the SoA store, a Field element node | `INFINITE_FIELDELEMENTTEST` passes; `geometry-transform-sweep` passes |

Step 5 is where Field's declarations first meet Infinite's shared modulation
machinery, so step 4's node is what you hang the knobs on. If step 4 is
missing, stop and say so.

---

## 1. Invariants — restated verbatim, they override anything you infer

1. **Clean room.** Infinite is MIT. **Never** open, read, grep, or reference
   GPL sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or
   BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine — do
   not open it). The *Kronos paper* (Norilo, CMJ 39:4, 2015) is a published
   paper and may be cited freely; its code may not be read. Safe to read:
   Faust (LGPL), ChucK (dual MIT/GPL), Houdini VEX docs, TidalCycles docs.

2. **Bare names. No sigils. Ever.** `param float amount = 0.5 [0, 2]`, never
   `@amount`. Plain ASCII, in code, docs, error messages and fixtures.

   | Wrong | Right |
   |---|---|
   | `@amount` | `amount` |
   | `chf("amount")` | `param float amount = 0.5 [0, 2]` then `amount` |

3. **Rate is inferred, never declared.** A `param` seeds the **`graph`** domain
   in inference (`field-compiler` §5 step 1) — it is a constant for the
   duration of a cook. There is no syntax to say otherwise.

4. **Field is one primitive, not a pile of features.** A `param` is a
   declaration that binds a name to a host-supplied constant. It is not a new
   evaluation mode.

5. **Only build step 1 touches existing code. Steps 2–10 are additive.** In
   particular: **do not add a field to `ParamRef`** without §5.1's open
   question being answered by the owner first — `ParamRef` is registered by
   every node in the app, every frame.

6. **Two cross-thread channels, and no third.** `ParamMailbox` (main → audio)
   and `MeterRing` (audio → main). Step 5 adds neither a new atomic nor a new
   queue. (Step 5 is main-thread only; the audio path is step 9.)

7. **A failing compile never blanks the graph.** Last working program keeps
   running, error text surfaced. `FormulaNode::Apply()` is the reference.

8. **Real-time safety:** no heap allocation in per-cook code past init, no
   recursion, no unbounded loops, no strings/pointers/dynamic arrays/structs in
   the v1 language surface, every size known at compile time.

---

## 2. Goal

Make a `param` line in a Field program **auto-create a knob in the node body**,
registered through the existing `ParamRef` machinery in
`src/core/Modulation.h`, so it is modulatable, expression-drivable, and
survives save/load/undo/copy-paste exactly like every hand-written knob in the
app. The hard part is not drawing the knob — it is **identity**: a Field body
is edited live, so `param` lines get inserted, deleted, reordered and renamed
while modulation cables are attached to them. Infinite keys every modulation
binding on `(nodeIndex, paramIndex)`. If `paramIndex` is the declaration
ordinal, inserting one line at the top of a Field body silently re-points every
modulation cable on that node. Step 5 is done when that cannot happen.

---

## 3. Files to read first, and why

### Skills

| File | Why |
|---|---|
| `.claude/skills/field-language/SKILL.md` | §7 the `param` syntax and its `ParamRef` field mapping, **including the OPEN question about where the default value lives**; §4 no sigils; §14 row 12 (the 128 ceiling) |
| `.claude/skills/field-integration/SKILL.md` | **§3 is the core of this step** — the `ParamRef` mapping table, the five re-registration rules, and the OPEN question about `paramIndex`; §5 save/load/undo; §4 `ParamMailbox` (context only, step 9 does the work) |
| `.claude/skills/field-compiler/SKILL.md` | §5 domain inference (a `param` seeds `graph`); §8 what the front end must refuse |
| `.claude/skills/field-realtime/SKILL.md` | §1 rule 10 — `param` count ≤ 128 per sample-domain kernel |
| `.claude/skills/field-testing/SKILL.md` | §6 the step-5 row: "a binding survives **editing the Field source**" is the acceptance criterion |
| `.claude/skills/node-param-audit/SKILL.md` | run it after; it confirms the param is modulatable and appears in the performance matrix |
| `.claude/skills/modulation-sweep/SKILL.md` | the end-to-end modulation sweep this step must not regress |
| `.claude/skills/node-ui-pillars/SKILL.md` | **before** writing any knob-row layout. Auto-generated knob rows must sit on the row grid like every other node's. |

### Real source — the code wins over any skill

| File | Lines that matter | Why |
|---|---|---|
| `src/core/Modulation.h` | `struct ParamRef` at **line 28** (an older doc says 29 — the code wins); `Modulation::Source` at `:57`; `Bind`/`RestoreLink`/`SetRange`/`SetEnabled`/`Unbind`/`UnbindAllFor` at `:110`–`:124`; `ClearFrameParams()` `:167`; `RegisterParam()` `:168`; `KnownParam()` `:182`; `SetExpression`/`ClearExpression` `:147`–`:149` | the whole contract |
| `src/core/INode.h` | `ParamVisitor` at `:80` — exactly five methods: `Float`, `Int`, `Bool`, `Text`, `Color`. **No `Vec3`, no blob.** `VisitParams` at `:159` | how params save |
| `src/main.cpp` | `gParamRegisterOnly` at `:1328`, its uses at `:1922`, `:2165`, `:3085`, `:3351`, `:48355`, `:48826`; the modulation apply loop near `:37506` | why registration must happen even when nothing draws |
| `src/core/Patch.h` | the `mod` and `expr` line grammar, and the note that "Names may contain spaces, so anything free-form is always last on its line" | save format |
| `src/nodes/FormulaNode.h` | `VisitParams` at `:39` — the model for saving user-typed source text plus fixed knobs | |
| `src/audio/ParamMailbox.h` | `kMaxParams = 128` at **line 23** (an older doc says 24 — the code wins) | the ceiling a sample kernel must respect |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/ParamTable.h` / `.cpp` | the per-node param table: stable id allocation, name→id map, values, ranges, defaults, and the persisted id counter |

### Modify

| Path | Change |
|---|---|
| `src/core/field/*` (step 1/4's compiler) | parse `param <type> <name> = <literal> [ <min>, <max> ]`; seed it as `graph` domain; expose the declaration list on the compiled program |
| `src/nodes/FieldElementNode.h` / `.cpp` (step 4's node) | own a `ParamTable`; register every param through `Modulation::RegisterParam` each frame; draw the auto-generated knob rows; extend `VisitParams` |
| `CMakeLists.txt` | add `src/core/field/ParamTable.cpp` |
| `.claude/skills/run-infinite-hygiene/driver.sh` | add `"FIELDPARAMTEST:10"` to `FULL_TESTS` (**line 173**; an older doc says 49 — the code wins) |

**Do not modify** `src/core/Modulation.h`, `src/core/INode.h`, or
`src/core/Patch.h` unless §5.1's open question is answered in a direction that
requires it — and then only with the owner's explicit go-ahead.

---

## 5. Step-by-step procedure

### 5.1 Answer the two OPEN questions FIRST. Do not resolve them silently.

Both are marked OPEN in the skills. Both change the shape of the code you are
about to write. **Put them to the owner before writing anything.**

**OPEN 1 — what is a Field `param`'s `paramIndex`?**

`Modulation` keys every binding on `(nodeIndex, paramIndex)` (`Modulation.h`,
`using Key = std::pair<int,int>`). Editing a Field body adds, removes and
reorders `param` declarations.

| Option | What it means | Verdict |
|---|---|---|
| **(a)** a stable hash of the param name, with collision detection at compile time | no extra persisted state; a rename still breaks the binding | viable |
| **(b)** a monotonically increasing per-node counter, allocated on first sight of a name, never reused, **persisted in the patch** | survives insert/delete/reorder; needs one more saved field | viable, and the more robust of the two |
| **(c)** declaration order | inserting a `param` line at the top re-points every modulation cable on that node | **not acceptable** |

This is the same class of bug `node-ui-pillars` P7 documents for filter-mode
indices — saved patches store an *integer index*, so re-ordering a name list
silently rewrites every saved patch. **Ask the owner between (a) and (b).**
Whichever is chosen, `paramIndex` must be **stable across frames** and across
edits that do not touch that param's own line.

**OPEN 2 — where does a `param`'s default value live?**

`ParamRef` (`src/core/Modulation.h:28`) carries exactly:

```cpp
struct ParamRef {
   int nodeIndex, paramIndex;
   float* value;
   float minValue, maxValue, step;
   std::string name;
   bool isEnum, isBool;
   std::vector<std::string> enumOptions;
   FaderPosToValueFn posToValue;
   FaderValueToPosFn valueToPos;
};
```

There is **no `defaultValue`**.

| Option | Trade |
|---|---|
| **(a)** store defaults in the Field node's own side table keyed by name | zero change to shared code; "reset to default" stays Field-only |
| **(b)** add `defaultValue` to `ParamRef` | every node in the app gets reset-to-default for free — but it touches a struct every node registers into **every frame** |
| **(c)** treat the literal as the initial value only, with no reset concept | simplest; no reset gesture at all |

Invariant 5 points at (a). **Ask; do not pick silently.**

### 5.2 Parse the declaration

```
param float amount = 0.5 [0, 2]
```

| Piece | Meaning | `ParamRef` field |
|---|---|---|
| `param` | keyword | — |
| `float` | type — **float only in v1** | `step = 0`, `isEnum = false`, `isBool = false` |
| `amount` | identifier **and** knob caption | `name` |
| `= 0.5` | initial value | see OPEN 2 |
| `[0, 2]` | range | `minValue`, `maxValue` |

`[ ]` are lexed as `PUNCT` and are legal **only inside a `param` range**
(`field-compiler` §2) — they are not an array syntax, and v1 has no arrays.

| Wrong | Right | Error must say |
|---|---|---|
| `param amount = 0.5 [0, 2]` | `param float amount = 0.5 [0, 2]` | a type is required |
| `param float P = 0.5 [0, 1]` | pick another name | `P` is a reserved word of the `element` domain |
| `param float a = 0.5` | `param float a = 0.5 [0, 1]` | decide with the owner whether a missing range defaults to `[0,1]` or errors; do not leave it undefined |
| `param float a = 0.5 [2, 0]` | `[0, 2]` | min must be ≤ max |
| `param vec3 c = ...` | not in v1 | float params only in v1 |
| two `param float a` lines | one | duplicate declaration, both spans |
| a `param` whose range endpoints are expressions | literals | `field-realtime` §1 rule 5 — sizes and ranges are compile-time constants |

Prior art, cite it, do not claim novelty: Houdini `chf()`, Cabbage markup.

### 5.3 Register through `ParamRef` — the five rules a fresh session gets wrong

Each is documented in `Modulation.h` and each is a real failure mode.

| # | Rule | Source | What breaks otherwise |
|---|---|---|---|
| 1 | **Re-register every frame, while the node draws.** `ClearFrameParams()` runs each frame | `Modulation.h:167` | the param vanishes from the matrix on any frame it did not register |
| 2 | **Register even when collapsed or hidden.** `gParamRegisterOnly` (`src/main.cpp:1328`) exists precisely so a modulator keeps writing into a param that is not being drawn | `main.cpp:2165`, `:3085`, `:3351` | a collapsed node's modulation silently freezes |
| 3 | **Never store the `float*`.** `KnownParam`'s stored copy deliberately nulls it — "the raw float* is only valid within the frame that registered it" | `Modulation.h:179`–`:186` | a dangling pointer read after the node's storage moved |
| 4 | **`paramIndex` must be stable across frames** — it is half the binding key | `Modulation.h`, `using Key` | OPEN 1; option (c) silently re-points every cable |
| 5 | **A wired modulator beats a typed expression** on the same param | `Modulation.h:139`–`:146` | double-driving a param, or an expression that a cable should have overridden |

`value` must point at the `ParamTable`'s own float for this param, **valid only
within the frame that registered it**.

### 5.4 Save / load / undo — all generic, if you do it right

| Thing | How it saves |
|---|---|
| Field source text | a `Text` param via `VisitParams`, exactly as `FormulaNode::formula` does (`src/nodes/FormulaNode.h:41`). `Patch.cpp`'s `EscapeLine`/`UnescapeLine` already turn embedded newlines into `\n` and back — **do not invent a second scheme** |
| each `param`'s current value | a `Float` param via `VisitParams`, named so it cannot collide with a fixed param — e.g. `p.<name>` |
| the persisted id counter and name→id map, if OPEN 1 resolves to (b) | `Int` / `Text` params via `VisitParams` |
| modulation on a Field param | the **existing** `mod <dstIndex> <dstParam> ...` line, keyed `(nodeIndex, paramIndex)` — hence OPEN 1 |
| a typed `=` expression on a Field param | the **existing** `expr <dstIndex> <dstParam> <text>` line |

**Undo/redo and copy/paste are generic.** They go through `VisitParams`. A
Field node that saves correctly through `VisitParams` gets undo, redo,
copy/paste and duplication **for free**. Adding a per-node entry to any of those
paths is a sign the node is built wrong.

Two traps in the line grammar, both already burned into this codebase:

- A **space breaks a token-separated line** — anything free-form must be
  **last on its line**. `Patch.h`'s own header comment says so.
- The reader's documented forgiveness (`src/core/Patch.cpp:154`): any escape
  other than `\n` is left exactly as written. This is one more reason v1 has no
  strings.

**Load ordering matters.** `VisitParams` replays saved values *before* the
program has necessarily been compiled. Restore into the `ParamTable` keyed by
name, and reconcile against the declarations on the first successful compile:

| Saved param | After compile | Result |
|---|---|---|
| name still declared, still float | — | value restored |
| name no longer declared | — | value dropped |
| name declared but never saved | — | takes its declared initial value |

### 5.5 Rename and delete while a modulator is attached — the heart of this step

This is what the exit criterion actually tests. Decide each row explicitly and
implement it; do not let the behaviour fall out of whatever the code happens to
do.

| Edit to the source text | What must happen to a modulation cable already bound to it |
|---|---|
| a `param` line is **added above** an existing one | **nothing** — every existing binding stays on its own param. This is the whole reason OPEN 1 exists. |
| a `param` line is **deleted** | the binding is removed via `Modulation::Unbind(nodeIndex, paramIndex)` (`Modulation.h:123`), and any typed expression via `ClearExpression` (`:148`). The cable disappears from the UI in the same frame; it must not linger pointing at a param that no longer exists. |
| a `param` is **renamed** | under OPEN 1 option (a) (name hash) the id changes, so this is a delete + add: the binding is dropped. Under option (b) (counter keyed on first sight of a name) it is also a delete + add. **Either way a rename loses the binding** — so the UI must say so, not silently drop the cable. Surface it: a one-line notice on the node naming the param that lost its modulation. |
| a `param`'s **range** changes (`[0,2]` → `[0,4]`) | the binding survives. `Modulation::Source::lo/hi` are in **destination units** (`Modulation.h:81`–`:83`), so the bound range does not auto-rescale — decide with the owner whether to clamp `lo`/`hi` into the new range via `SetRange` (`Modulation.h:118`) or leave them. Clamping is the safer default; either way it must be deliberate. |
| a `param`'s **type** changes | float-only in v1, so this cannot happen yet. Refuse anything else at parse time. |
| the whole program fails to compile mid-edit | **nothing changes.** The last working program, its param table, its ids and its bindings all keep running (invariant 7). Reconciliation happens **only on a successful compile.** |
| the node is deleted | `Modulation::UnbindAllFor(nodeIndex)` (`Modulation.h:124`) already handles this generically — do not add a per-node path |

**Undo of a source edit must restore the bindings too.** Because the source text
is a `Text` param and the id map (option b) is saved alongside it, an undo that
restores the text also restores the ids, and the `mod` lines in the snapshot
still key correctly. **Verify this rather than assuming it** — it is the single
most likely place for step 5 to be subtly wrong.

### 5.6 Draw the knobs

Read `.claude/skills/node-ui-pillars/SKILL.md` **before** writing this.
Auto-generated rows are still node rows: they sit on the same row grid, obey
the same symmetry rules, and honour the light/dark contrast budget. A
Field node whose knobs are laid out "however many params there happen to be"
is a pillar violation even though nothing crashed.

### 5.7 The 128 ceiling

`ParamMailbox::kMaxParams = 128` (`src/audio/ParamMailbox.h:23`) bounds how
many params a **sample-domain** kernel can have. The sample domain is step 9,
but implement the check now, in the front end, where the declarations are
counted: exceeding it is **a compile error naming `kMaxParams` and its source
file**, never a silent truncation (`field-compiler` §8,
`field-realtime` §1 rule 10). For the element/frame/pixel domains, pick a
defensible ceiling of your own and surface it the same way — an unbounded knob
count is a UI failure even where no mailbox is involved.

### 5.8 Fixtures

`INFINITE_FIELDPARAMTEST`. The parse/refusal cases are pure computation and
belong in an **early-exit headless** section modelled on `INFINITE_DSPTEST`
(gated at `src/main.cpp:37619`, before `glfwInit()`). The
registration/binding/save-load cases need a real spawned node and belong in an
**in-frame** section modelled on `INFINITE_ROUNDTRIPTEST`
(`src/main.cpp:43318`, `frameId == 4`). One verdict line per section.

| Section | Assert |
|---|---|
| parse | every wrong/right row in §5.2, each error naming its span |
| registration | after one frame, the param appears in `Modulation::AllKnownParams()` with the right `name`, `minValue`, `maxValue` |
| collapsed | with `gParamRegisterOnly` set, the param still registers |
| binding survives insert | bind a modulator to `b`, insert `param float a` **above** it, recompile, assert the binding still points at `b` |
| binding drops on delete | delete `b`'s line, recompile, assert `IsModulated` is false and no stale `Key` remains in `Links()` |
| rename | rename `b`, recompile, assert the binding is gone **and** the node surfaced a notice |
| failed compile | introduce a syntax error, assert every binding, every param value and the running program are **unchanged** |
| save/load | save → load → assert source text, every param value, and every `mod` line round-trip |
| undo | edit the source, undo, assert text **and** bindings are back |
| ceiling | 129 params errors, and the message contains `kMaxParams` |

**A test that cannot fail is not a test.** Break each case deliberately, watch
it print `FAIL`, then fix it.

---

## 6. Traps

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Using declaration order as `paramIndex` | inserting one `param` line at the top silently re-points **every** modulation cable on the node. This is the same failure `node-ui-pillars` P7 documents for saved filter-mode indices. |
| 2 | Storing the `float*` from a `ParamRef` beyond the frame that registered it | `Modulation.h:179`–`:186` nulls it in `KnownParam` on purpose; a stored copy dangles the moment the table reallocates |
| 3 | Registering only when the node body is expanded | a collapsed node's modulation freezes. `gParamRegisterOnly` (`src/main.cpp:1328`) exists exactly for this. |
| 4 | Registering once at spawn instead of every frame | `ClearFrameParams()` (`Modulation.h:167`) wipes the list each frame; the param disappears from the matrix |
| 5 | Reconciling params on a **failed** compile | a typo mid-edit destroys the user's bindings. Reconcile only on success (invariant 7). |
| 6 | Adding `defaultValue` to `ParamRef` without asking | that struct is registered by every node in the app every frame; growing it is a whole-app change, not a Field change (OPEN 2) |
| 7 | Inventing a new patch line kind for param values | `Float` through `VisitParams` already works and already gets undo/copy-paste for free |
| 8 | Putting a free-text field anywhere but last on its line | `Patch.cpp` reads token-separated lines with `>>`; a space silently truncates the record |
| 9 | Adding a per-node entry to the undo, copy/paste or duplicate paths | those are generic. Needing one means the node is built wrong (`field-integration` §5). |
| 10 | Letting a `param` name shadow a reserved attribute (`t`, `dt`, `frame`, `P`, `N`, `uv`, `Cd`, `i`, `count`) | compile error, naming the domain the name belongs to. Precedent: `ExprGlobals::IsValidName` (`src/core/ExprGlobals.h:45`) already refuses `t`, `pi`, `lo`, `hi`. |
| 11 | Silently truncating at 128 params | `field-realtime` §1 rule 10 — a truncated param is a knob that does nothing, with no message |
| 12 | Auto-rescaling a binding's `lo`/`hi` when a range changes, without deciding to | `Modulation::Source::lo/hi` are in **destination units** (`Modulation.h:81`). Silent rescaling changes what a saved patch sounds/looks like. |
| 13 | Dropping a binding on rename without telling the user | the cable just vanishes; the user assumes a bug. Surface a notice naming the param. |
| 14 | Writing the param values into the patch under names that can collide with fixed params | prefix them (`p.<name>`); `ParamVisitor` names are stable keys and a collision silently overwrites |
| 15 | Laying out auto-generated knobs off the row grid | `node-ui-pillars` — symmetry and dark-mode contrast are non-negotiable regardless of whether the row was hand-written or generated |
| 16 | Pushing a Field param straight into the audio thread here | that is step 9, and it goes through `ParamMailbox::Push` and nothing else (invariant 6) |
| 17 | Treating a `param` as anything but `graph` domain in inference | `field-compiler` §5 step 1 — a `param` is a constant for the cook; making it frame-domain defeats the hoist |

---

## 7. Machine-checkable exit criterion

```bash
cd /Users/namansoni/infinte

# 1. builds clean
cmake --build build -j"$(sysctl -n hw.ncpu)"

# 2. the param fixture
INFINITE_FIELDPARAMTEST=1 INFINITE_EXITAFTER=10 ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/fieldparam.log
grep -c FAIL /tmp/fieldparam.log     # must print 0
grep -c BUG  /tmp/fieldparam.log     # must print 0

# 3. shared modulation code is untouched (unless OPEN 2 was answered otherwise)
git diff main --stat -- src/core/Modulation.h src/core/Modulation.cpp src/core/INode.h src/core/Patch.h src/core/Patch.cpp
#   expected: no output

# 4. no sigils, no declared rates
grep -rn '@[A-Za-z]' src/core/field/ docs/plans/field/ | grep -v '@brief\|@param\|email' || echo "no sigils OK"

# 5. no new cross-thread channel
git diff main -- src/core/field/ src/nodes/FieldElementNode.* | grep -n 'std::atomic\|std::mutex\|std::thread' || echo "no new channel OK"

# 6. modulation still works end to end
.claude/skills/modulation-sweep/driver.sh 2>/dev/null || echo "(run the modulation-sweep skill's procedure by hand if it has no driver)"

# 7. the full gate
.claude/skills/run-infinite-hygiene/driver.sh

# 8. project convention
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Then run `.claude/skills/node-param-audit/SKILL.md`'s procedure and confirm
every Field `param` appears in the modulation matrix and in the performance
matrix.

`MODMATRIXTEST`, `MODBOUNDSTEST`, `MACROTEST`, `PERFMATRIXTEST`, `UNDOTEST`,
`PATCHTEST` and `ROUNDTRIPTEST` are the hygiene entries that directly guard
step 5; all must pass, and the `AUDIOPARAMSWEEPTEST` xfail baseline must not
grow.

---

## 8. Out of scope for this step

| Not in step 5 | Where it lands |
|---|---|
| `state` cells, and the question of how they serialize | step 6 |
| The `pixel` domain and GLSL uniforms for params | step 7 |
| `reduce`/`map`/`resample`/`downsample` | step 8 |
| `ParamMailbox::Push` and anything reaching the audio thread | step 9 — implement only the **compile-time 128 check** now |
| `graph` domain | step 10 |
| Non-float params (`int`, `bool`, enum, colour) — `isEnum`, `isBool`, `enumOptions`, `posToValue`, `valueToPos` all stay at their defaults | a later, separately-scoped change |
| Adding `defaultValue` to `ParamRef` | only if OPEN 2 resolves to (b), and then as its own owner-approved commit |
| Changing `Expression::Evaluate`'s signature or its three call sites (`src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179`) | never |
| Changing undo *semantics* — which actions checkpoint, how many undos an action costs | never in this step |

If you find a genuine bug in existing modulation code while in here, **report
it rather than fixing it inline**.
