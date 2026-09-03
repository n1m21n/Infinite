# Field — step 10: the `graph` domain (kernels that emit nodes, at edit time)

> This is an **implementation prompt**, not implementation notes. It is written to be
> pasted into a fresh Claude Code session that has never seen this repository. Every
> function name, signature and line number below was read out of the tree at the time
> of writing. Redundancy with the other `docs/plans/field/step-*.md` files is
> deliberate and must not be "cleaned up".
>
> **Step 10 is the last step on purpose. Read §2.1 before writing a line of code.**

---

## 0. Regression gate and branch discipline — added 2026-09-03, read this first

This section postdates the rest of this file. Where anything below conflicts with it, **this section
wins**. It exists because of what actually happened when steps 3, 4, 5 and 6 were implemented as one
batch on one branch:

- Step 6 added a pre-scan in `LowerElementProgramToIR` (`src/core/field/FieldIR.cpp`) that pinned every
  bare local to `(float, Domain::Element)`. That silently defeated **step 4's rate inference** — nothing
  was ever hoisted, `ir.prologue` was empty for every program, and a frame variable left in the element
  loop read `0.0` instead of erroring. It also killed **step 3's** local type inference:
  `c = vec3(1,0,0)` was rejected as "cannot assign vec3 to float".
- Step 6's cycle checker used one graph node per assign-target *name*, so `P.y += sin(P.x) * 2.0` was
  rejected as a delay-free cycle. That is the canonical element idiom — the Field Element node's own
  default program and 2 of its 5 presets did not compile.
- Step 5's harness dereferenced a null `Find()` and **segfaulted**, so 4 of its 6 sections had never
  executed even once.
- Step 4's element backend shipped **two near-duplicate opcode interpreters** (a prologue switch and an
  element-loop switch). Unhandled opcodes fell through `default:` and left the register at `0.0` with no
  error. Frame-hoisted `-t`, `t > 1.0`, `if(t > 1.0, a, b)`, `&&`, `||`, `!` and `for` loops all
  evaluated to zero, silently.

None of this was caught by the step that caused it, and step 4's harness was registered in **no tier** of
the hygiene driver, so nothing ever ran it. Four rules follow.

### 0.1 One step, one branch, one commit

```bash
git checkout -b feature/field-step-NN-slug   # off the previous step's branch, before writing any code
```

Commit on that branch when the step's harness passes. **Never** leave two steps uncommitted in one tree —
that is what made the failures above unattributable. The chain today is
`main → feature/field-step-01-expression-ir → …-02-pure-randomness → …-03-vectors-and-rank → …`.

### 0.2 Run every Field harness, not only your own

Your step is not done when your fixture passes. It is done when **all** of them pass:

```bash
cmake --build build -j8
for v in FIELDTEST FIELDELEMENTTEST FIELDPARAMTEST FIELDSTATETEST NEWFIXTURES; do
  echo "== $v"; env INFINITE_$v=1 ./build/Infinite.app/Contents/MacOS/Infinite | tail -20
done
```

(replace `NEWFIXTURES` with every `INFINITE_FIELD*` fixture that exists by the time you run this —
`grep -o 'INFINITE_FIELD[A-Z0-9]*' src/main.cpp | sort -u`.)

A FAIL in an **earlier** step's harness is a regression in **your** change, not a pre-existing problem to
report and move past. `INFINITE_FIELDTEST` section A must stay at 170/170; if a corpus golden value
changes, that is a bug in your change — **do not re-baseline the corpus.**

### 0.3 Register your fixture in the hygiene driver

Add every fixture you create to `TIER1_CHECKS` in
`.claude/skills/run-infinite-hygiene/driver.sh` (the Field fixtures are headless early-exit runs costing
~1s each, so tier 1 is correct). A fixture that is not in the driver does not exist — that is exactly how
step 4's regression survived.

### 0.4 Never write an opcode switch with a silent `default:`

If this step adds a backend or an interpreter: **one** implementation per opcode, shared across every
register bank or execution context, and an unhandled opcode must be **loud** (an `outError` or an assert),
never a zero-valued fallthrough. Two switches over the same opcode enum will drift, and the drift is
invisible because the wrong answer is a plausible number. Same rule for a silent value fallback: an
unresolved name is a compile error with a source span, not a runtime `0.0`.

---

## 1. Invariants — restated verbatim, do not paraphrase

These are copied from `docs/plans/field/README.md` §0 and from the owner's standing
instructions. They are restated in full in every step file so that a session that has
read only this file is still bound by them.

| # | Invariant |
|---|---|
| I1 | **Clean room.** NEVER open, read, `grep`, `find`, download or quote **Kronos**, **Cmajor**, **SuperCollider**, or `/Users/namansoni/BespokeSynth`. All of those are GPL; Infinite is MIT. Cite the Kronos *papers* freely (Norilo, *Kronos: A Declarative Metaprogramming Language for Digital Signal Processing*, Computer Music Journal 39:4, 2015). Never its code. If you find yourself about to read a file under a GPL project to "check how they did it", stop. |
| I2 | **No sigils.** Field uses **bare names**: `P`, `N`, `uv`, `t`, `in`, `out`. Never `@P`, never `$P`, never `%P`. Anything in a skill or an older draft that shows a sigil is stale and the bare form wins. |
| I3 | **Rate is inferred, never declared.** There is no `@graph` annotation, no `rate graph` keyword, no `#pragma`. A kernel is in the `graph` domain because *nothing it reads has a finer domain*. If you find yourself adding syntax so the user can say which rate they want, you have taken a wrong turn. |
| I4 | **The code wins over any skill.** Where a `.claude/skills/field-*` document contradicts the real source tree, the source tree is correct and the skill is stale. Record every such conflict in §1.5 of this file — do not silently follow either one. |
| I5 | **One primitive.** "A kernel is a body of code run once per element of a domain." The `graph` domain has exactly one element per graph edit. Nothing about step 10 is allowed to introduce a second organising concept. |
| I6 | **Branch first.** `git checkout -b feature/field-step-10-graph-domain` before the first edit. Do not commit to `main`. |
| I7 | **Typed IR is the durable asset.** No backend-specific IR node types. The `graph` domain gets an *interpreter* over the existing IR, not a fourth IR dialect. |
| I8 | **Do not weaken an existing guarantee to make step 10 easier.** In particular the `ExprGlobals` list-order property (§5.5) and the audio/note cycle refusals (§5.7) are load-bearing and must survive intact. |

### 1.1 Reserved names in the `graph` domain

`graph` has **no reserved names of its own**. That is the point: `t` is `frame`, `P`/`N`
are `element`, `uv` is `pixel`, `in`/`out` are `sample`. A kernel that mentions none of
them, and calls nothing that transitively reads one, infers `graph` and is therefore a
graph kernel. Nothing is declared.

---

## 1.5 Discrepancies found between the skills, the sibling step files, and the code

The code wins in every row. Fix nothing in the skills as part of step 10 — record and
route around.

| # | Skill / doc says | The code actually says | What step 10 must do |
|---|---|---|---|
| D1 | Steps 04, 05, 08 and 09 write Field sources under `src/field/`. | Step 01 actually put them under **`src/core/field/`** with a `Field` filename prefix (`FieldLexer.cpp`, `FieldParser.cpp`, …). `docs/plans/field/step-06-state-cells.md` §4 already calls `src/field/` "a drafting error". | Use **`src/core/field/Field*.{h,cpp}`**. If a path in this file disagrees with what is on disk, `ls src/core/field/` and believe the disk. |
| D2 | `field-globals` (and older drafts) cite `ExprGlobals::IsValidName` at `src/core/ExprGlobals.h:44`. | It is at **`src/core/ExprGlobals.h:45`**. | Cite `:45`. The signature `bool IsValidName(const std::string& name, std::string& outError);` is unchanged and **must stay unchanged** — it is an exit-criterion item. |
| D3 | The design brief says `ExprGlobals` is absorbed as **graph-domain constants**. | `ExprGlobals::EvaluateAll(double t)` is called **once per frame** from `src/main.cpp:37421`, and `src/core/ExprGlobals.h:22-27` states each global "sees `t` plus every global *above* it". A global is therefore **frame-domain by default**, not graph-domain. | Split by inference, not by fiat: a global whose expression is constant-foldable (mentions no `t`, no `rand`/`noise`/`sh`) is `graph`; every other global is `frame`. See §5.5. This is the single largest correction in step 10. |
| D4 | `docs/plans/undo-delete-perf-prompt.md` (the format model) cites `BuildPatchData` at 23492, `RemoveNodeByIndex` at 23421, `Undo` at 24145, `kMaxUndoDepth` at 23861. | All four have drifted; the file's own Part A items 1 and 5 and Part B `AssetCache` have landed since. Current: `BuildPatchData` **`src/main.cpp:25749`**, `RemoveNodeByIndex` **`:25666`**, `Undo` **`:27058`**, `kMaxUndoDepth` **`:26243`**. | Copy that document's *format*, never its line numbers. Re-derive every number with `grep -n` before you rely on it. |
| D5 | Older notes cite `COMMON_SOURCES` in `CMakeLists.txt` around line ~98 / ~54. | It is at **`CMakeLists.txt:206`**, with core files from 207 and node `.cpp` files around 258-268. | Add new `src/core/field/*.cpp` files after the existing core entries at ~207-257. |
| D6 | README §0 invariant: "Only step 1 touches existing code. Steps 2-10 are additive." | Step 10 **cannot** be purely additive. `SpawnNode` (`src/main.cpp:4967`) and `RemoveNodeByIndex` (`:25666`) live in `src/main.cpp`'s anonymous namespace and are not reachable from `src/core/`. Regeneration must also be remapped across undo/redo/load, in the same place `RemapViewportPanelNodes` (`src/main.cpp:27045`) already is. | Confine the change to a **narrow host interface** (§4.2): `src/core/field/FieldGraphHost.h` declares a pure-virtual `IFieldGraphHost`; `src/main.cpp` implements it. `src/core/field/` never includes `main.cpp`'s internals and never learns about `GraphNode`. Declare this scope expansion in the PR description; it is expected, not a surprise. |
| D7 | `field-realtime` rule 4: "no strings in the language surface." | A graph kernel must name node types ("Wavetable") and param names ("cutoff"). | Carve-out, stated precisely so the invariant survives: **a string literal is legal only in a literal argument position of `emit` and `set`. It is interned at compile time into an integer id and never becomes a value.** There is still no `string` type: you cannot bind one to a name, pass one through a `param`, return one, or concatenate. Enforce this in the parser, not by convention. |
| D8 | `field-domains` §181 table: "`graph` → anything \| implicit \| 0" (a graph value broadcasts into any finer domain, free). | Correct and unchanged. But the *reverse* row at `field-domains` §95 — "reduce crossing two levels at once (`sample` → `graph`) \| error" — undersells it. | **No** transfer into `graph` exists at all, not from `frame`, not one level, not with `reduce`. `graph` is a source, never a sink. See §5.2. |
| D9 | `node-ui-pillars` P7 (line 107): "Saved patches store an integer index, so re-ordering or inserting an entry in any of these lists silently rewrites every saved patch." | Confirmed, and it is worse for step 10: **undo/redo reassigns every node index.** `NewPatch()` sets `gNextIndex = 1` (`src/main.cpp:26273`) and `ApplyPatchData` (`:26787`) remaps saved→new. | The generated-node key scheme must be a **persisted, index-free, non-ordinal string** (§5.3). Any design that keys on `GraphNode::index`, or on position in a vector, is wrong on the first Cmd+Z. |
| D10 | Step 09 §9 dependency table: "**10** — the `graph` domain \| **no**, and it depends on **this** step, not the other way round". | Consistent. Step 10 depends on 1-9; nothing depends on step 10. | Keep §9 of this file consistent with that row. |
| D11 | `field-integration` implies a new node type needs only `REGISTER_NODE`. | A new node type also needs an include, a body-draw dispatch entry, and a help-text entry (`SpecificNodeHelpText` `src/main.cpp:24153`, `NodeHelpText` `:24472`). | Wire all four for the new `Field Graph` node, and add the help text — an unhelped node is a review finding. |

---

## 2. Goal

Add the `graph` domain: Field kernels that **run at edit time and emit Infinite nodes**.
A graph kernel has rate zero — it runs when the user changes something, never per frame,
never per sample, never per pixel — and its output is a set of real nodes in `gNodes`
that are indistinguishable from hand-placed nodes once created, except that the kernel
that made them will reconcile them again on the next edit. The whole of step 10 is three
problems: proving at compile time that a kernel is genuinely rate zero (§5.2), giving
every emitted node a stable identity so regeneration is a **diff** and not a
delete-everything-and-respawn (§5.3), and making one regeneration read as exactly one
undo step against Infinite's real snapshot-based undo (§5.4).

### 2.1 Why this ships last — the actual reason, not a scheduling accident

Norilo evaluated Kronos in teaching over two years and reported the split plainly
(Computer Music Journal 39:4, 2015, p. 45): students grasped filters immediately because
the patches **corresponded very closely to textbook diagrams**, and they struggled to
apply the algorithmic-routing layer — the metaprogramming — on their own.

That is the whole risk of this step in one sentence. Graph metaprogramming is the
feature that impresses experts in a demo and loses everybody else in practice. So:

| Consequence for step 10 | Concretely |
|---|---|
| Steps 1-9 must be **complete and unregressed** before step 10 lands | The exit criterion (§7) runs the *entire* harness, not just step 10's fixtures. A graph kernel that emits nodes is worthless if the sample kernel it emits is broken. |
| The graph domain must be **skippable** | A user must be able to use every other domain, ship real patches, and never open a graph kernel. Nothing in steps 1-9 may start requiring a graph kernel to work. Verified by the exit criterion: the step 1-9 fixtures must pass on a build where no `Field Graph` node is ever spawned. |
| Generated nodes must look like **ordinary nodes** | A user who inherits a patch containing a graph kernel must be able to read the emitted subgraph as a normal patch — the textbook-diagram correspondence Norilo's students got — without understanding the kernel. This is why §5.6 picks serialisation over regeneration, and why generated nodes are drawn with the standard node body, not a special one. |
| The error messages carry the teaching load | Every refusal in §5.2, §5.5 and §5.7 must say **what to do instead**, not just what went wrong. A rate error that says "graph kernel cannot read `t`" and stops has failed. |

Do not add graph-domain sugar, graph-domain-only operators, or a second way to write a
kernel because "the metaprogramming case is different". It is not different. It is the
same primitive (I5) with one element.

---

## 3. Files to read first, and why

Read all of these before writing anything. The **why** column is why the file changes
what you write; skipping one produces a specific, predictable bug.

### 3.1 Design and plan

| File | Why |
|---|---|
| `docs/plans/field/README.md` | §0 the seven invariants; §2 the step table (step 10 = XL, depends on 1-9, exit criterion "graph conformance table; list-order evaluation preserved; `IsValidName` unchanged; and nothing in steps 1-9 regressed"); §4 the known discrepancy list; §5 "these are prompts, not notes — redundancy is correct". |
| `docs/plans/field/step-09-sample-domain.md` | The sibling written against the same spec. Its §1.5 (D1-D10) is the model shape for §1.5 here; its §7 is the rigour bar for §7 here; its §9 closing row names step 10 explicitly. **This file must not contradict it.** |
| `docs/plans/field/step-08-transfer-operators.md` | The house style: numbered sections, verbatim invariants, files-to-read table, phased procedure, a traps table whose second column is "the bug it prevents", a runnable exit criterion. |
| `docs/plans/field/step-05-param-declarations.md` | The `param` machinery a graph kernel reads to decide *how many* nodes to emit, and step 5's own ordinal-identity problem (`paramIndex`), which is the same bug class as §5.3 here. Whatever step 5 chose for param identity, step 10 reuses — do not invent a second identity scheme. |
| `docs/plans/field/step-06-state-cells.md` | `state` is keyed by **name**, not by position. §5.3's key scheme is deliberately the same rule so the language has one identity story. Also confirms `src/core/field/` (D1). |
| `docs/plans/field/step-01-*.md`, `step-04-*.md` | Where the lexer/parser/IR actually live and what the domain lattice type is called. |
| `docs/plans/undo-delete-perf-prompt.md` | **The format to copy.** Also the reason `gDeferAudioRebuild` exists (`src/main.cpp:25087`), which §5.4 depends on. Its line numbers are stale (D4). |

### 3.2 Skills

| Skill | Why |
|---|---|
| `.claude/skills/field-domains/SKILL.md` | The lattice (lines 46-48), the transfer table (line 181: `graph` → anything, implicit, cost 0), and line 95 (`sample` → `graph` reduce is an error). §5.2's check is a direct consequence. |
| `.claude/skills/field-compiler/SKILL.md` | Where the domain fixpoint lives. §5.2 adds **no new pass** — it adds one assertion after the existing fixpoint. |
| `.claude/skills/field-integration/SKILL.md` | The node-registration checklist. Incomplete — see D11. |
| `.claude/skills/field-testing/SKILL.md` | The fixture conventions (`getenv("INFINITE_*")`, verdict lines, `INFINITE_EXITAFTER`) that §7 must match. Its §5 conformance table has the `graph` row. |
| `.claude/skills/field-realtime/SKILL.md` | Rule 4, no strings — and the exact shape of the carve-out in D7. |
| `.claude/skills/field-globals/SKILL.md` | The `ExprGlobals` absorption. Stale on the line number (D2) and on the domain (D3). |
| `.claude/skills/node-ui-pillars/SKILL.md` | **Mandatory** before any node UI edit (owner standing instruction). P7 at line 107 is the ordinal-identity bug this step must not repeat. Symmetry and dark-mode contrast are non-negotiable for the `Field Graph` node body. |
| `.claude/skills/run-infinite-hygiene/driver.sh` | `TIER1_CHECKS=(` at line **79**, `FULL_TESTS=(` at line **173**. New fixtures are registered here. Note it auto-escalates to the full tier when the diff touches `src/core/Patch.cpp`, `NodeFactory` or `INode.h` — step 10 touches none of those, so **register the new fixtures explicitly** or they will not run. |

### 3.3 Real source — read these, they decide the design

| File / symbol | Line | Why it matters to step 10 |
|---|---|---|
| `src/main.cpp` — `GraphNode* SpawnNode(const std::string& typeName, const std::string& category, float x = 0.0f, float y = 0.0f)` | **4967** | The only node-creation path. Calls `MakeNode`, then `PushUndoCheckpoint()`, then `gn.index = gNextIndex++`, then `gNodes.push_back`, and returns `&gNodes.back()`. **Two consequences:** it pushes its own undo checkpoint (§5.4 must suppress it), and the returned pointer is invalidated by the *next* spawn (§6 T3). |
| `src/main.cpp` — `void RemoveNodeByIndex(int index)` | **25666** | The only node-destruction path. Generically handles `DisconnectAllTo`, `Modulation::UnbindAllFor`, `PaletteBinding::UnbindAllFor`, group-membership erase, viewport-panel erase, and a `RebuildAudioTopology()` after the erase. **Unmount must route through this**, never through a bespoke teardown, or you will leak exactly the bindings this function was written to clean up. |
| `src/main.cpp` — `void DisconnectAllTo(INode* dying)` | **24862** | Why a user-drawn cable *out of* a generated node silently vanishes on unmount (§5.7 case 5). |
| `src/main.cpp` — `GraphNode* FindNodeByIndex(int index)` | **4092** | The only legal way to turn a stored index into a node. Returns `nullptr` for a dead index. Every resolution in §5.3 goes through it. |
| `src/main.cpp` — `Patch::Data BuildPatchData()` | **25749** | Builds the undo snapshot from live `gNodes`. **Generated nodes are in `gNodes`, so they are already in every undo snapshot whether you want them or not.** This single fact decides §5.6. |
| `src/main.cpp` — `void ApplyPatchData(const Patch::Data& data, std::map<int,int>* outRemap = nullptr)` | **26787** | Undo, redo and load all funnel through here. Sets `gSuppressUndoCheckpoints = true` at `:26790`, respawns every node with a **fresh index**, fills `remap[rec.index] = spawned->index`, restores globals at `:26908-26910`, and does exactly one `RebuildAudioTopology()` at the end. |
| `src/main.cpp` — `void NewPatch()` | **26245** | Called by `ApplyPatchData`. Retires nodes into `gRetiredNodes` (`:26263`), `ExprGlobals::Clear()` (`:26272`), **`gNextIndex = 1` (`:26273`)**. This is why node indices are not stable identity (D9). |
| `src/main.cpp` — `bool gSuppressUndoCheckpoints` | **26236** | The mechanism that makes a batch of spawns one undo step. |
| `src/main.cpp` — `std::deque<Patch::Data> gUndoStack; std::deque<Patch::Data> gRedoStack;` | **26241-26242** | The real undo types: whole-graph `Patch::Data` snapshots in a deque. Not a command list, not deltas. |
| `src/main.cpp` — `const size_t kMaxUndoDepth = 200;` | **26243** | Why the "one checkpoint per emitted node" alternative is a data-loss bug and not just noise (§5.4). |
| `src/main.cpp` — `void PushUndoSnapshot(Patch::Data snapshot)` / `void PushUndoCheckpoint()` | **27007** / **27022** | `PushUndoCheckpoint()` is the "capture the current graph before I change it" call. It early-returns when `gSuppressUndoCheckpoints` is set. |
| `src/main.cpp` — `void Undo()` / `void Redo()` | **27058** / **27072** | Both call `ApplyPatchData(..., &remap)` then `RemapViewportPanelNodes(remap)` (`:27067`, `:27081`). |
| `src/main.cpp` — `void RemapViewportPanelNodes(const std::map<int,int>& remap)` | **27045** | **The precedent.** Session state keyed by node index, carried across a respawn by the remap. The kernel's ownership map is the second instance of exactly this pattern; copy it, do not invent something else. The comment at `:27039` explains why it is needed. |
| `src/main.cpp` — `bool gDeferAudioRebuild` | **25087** | Set around a loop that would otherwise rebuild audio topology once per node. Used at `:42784/42787` and `:49730/49747`. Regeneration is exactly such a loop. |
| `src/main.cpp` — `void RebuildAudioTopology()` | ~**25118** | Early-outs on `gDeferAudioRebuild` at `:25120`. Calling it 32 times in one regeneration resets every reverb tail 32 times. |
| `src/main.cpp` — `CaptureClusterLinks` / `ApplyClusterLinks` | **4743** / **4785** | The existing, tested "capture the cables around a set of nodes, respawn, reattach" pair, including modulation and palette links. §5.7 reuses these rather than writing a third copy. `ApplyClusterLinks`'s comment at `:4779-4784` says the caller is expected to be inside a `gSuppressUndoCheckpoints` region — that is exactly §5.4's shape. |
| `src/main.cpp` — copy/paste | `doCopy` ~**50045**, `doPaste` ~**50073**, paste body **50100-50135** | The model implementation of a batched multi-node edit: one `PushUndoCheckpoint()` at `:50108`, `gSuppressUndoCheckpoints = true` at `:50109`, spawn loop, `RandomNode::NextSeed()` re-seed at `:50117-50118` (**the precedent for re-identifying a copy**, §5.7 case 3), group re-membership at `:50122-50133`, `ApplyClusterLinks` at `:50134`, flag cleared at `:50135`. |
| `src/main.cpp` — `ed::BeginDelete()` block | ~**50137** | Calls `RemoveNodeByIndex((int)nodeId.Get() / GraphNode::kStride)`. This is how a user hand-deletes a generated node (§5.7 case 1). |
| `src/main.cpp` — `bool IsUserSpawnable(const std::string& name)` | **398** | Returns false for `"Delete Selected"`, `"Transform Selected"`, `"Extrude Selected"`, `"Group"`. `emit` must refuse exactly this set (§5.7 case 2). |
| `src/main.cpp` — `bool WouldCreateAudioCycle(INode*, INode*)` / `WouldCreateNoteCycle` | **4576** / **4613** | Used at `:4684`/`:4689` and `:49183`/`:49189`. A kernel's `connect` must go through the same refusals (§5.7 / §6 T7). |
| `src/main.cpp` — `std::map<GroupNode*, std::set<int>> gGroupMembers` | ~**455-460** | Keyed by *pointer*, membership only grows, and a stale index "is simply skipped wherever this is read, never treated as an error". Model the ownership map on this tolerance. |
| `src/main.cpp` — `GroupOwning` / `IndexOfGroupNode` / `DrawGroupNode` | **20411** / **20424** / **20605** | Needed to carry group membership across an unmount/remount (§5.7 case 2). |
| `src/main.cpp` — `ExprGlobals::EvaluateAll(t)` call site | **37421** | Once per frame. **This is the evidence for D3.** |
| `src/main.cpp` — the globals UI window | **51655+** | Document-level, deliberately not a node; every edit calls `PushUndoCheckpoint()`. §5.5 must not change this. |
| `src/main.cpp` — `INFINITE_UNDOTEST` fixture | **42571** | The model fixture shape for §7: `NewPatch()`, spawn, undo, redo, param edit, delete-with-connection, redo-stack invalidation, each printing `... OK` / `... FAIL`. Copy its structure exactly. |
| `src/main.cpp` — `INFINITE_ROUNDTRIPTEST` | **43318** | The save/load round-trip fixture §7 extends. |
| `src/main.cpp` — `INFINITE_GROUPTEST` / `INFINITE_DELETECRASHTEST` | **39307**, **42924** / **38940**, **46032** | The two fixtures most likely to regress from step 10. |
| `src/core/ExprGlobals.h` | **69 lines**; comment **22-27**; `IsValidName` **45** | The list-order / no-cycle guarantee, verbatim in §5.5. `struct Global { std::string name; std::string expr; float value = 0.0f; std::string error; };` |
| `src/core/ExprGlobals.cpp` | `EvaluateAll` ~**30-60** | Builds `sValues` incrementally so a forward reference fails "unknown identifier" rather than reading a stale value; refuses the names `t`, `pi`, `lo`, `hi`. |
| `src/core/Patch.h` | **16-58** grammar; `struct Data` | The line grammar. Step 10 adds **no new line kind** (§5.6). "Names may contain spaces, so anything free-form is always last on its line." |
| `src/core/Patch.cpp` | `kMagic` **17**; `EscapeLine` **49**; `UnescapeLine` **64**; `s` write **113**; node tag reader **379-386** | The reader accepts only the tags `f i b c s` inside a node record; the name is read with `>>` and the value with `getline` after stripping one leading space. A `Text` param carrying a key must therefore survive escaping — use `EscapeLine`/`UnescapeLine`, do not hand-roll. |
| `src/core/Modulation.h` | `ParamRef` **28**; `using Key = std::pair<int,int>` **53**; `RestoreLink` **115**; `Unbind` **124**; `UnbindAllFor` **125**; `Links()` **139**; `SetExpression` **149**; `Expressions()` **153**; `ClearFrameParams` **167**; `RegisterParam` **168**; `KnownParam` **182** | Bindings are keyed `(nodeIndex, paramIndex)` — **both halves move when a node is remounted.** `RestoreLink` is the patch-load-only entry that does not derive its range from the current frame, which is exactly right for a node that has not drawn yet. |
| `src/core/INode.h` | `ParamVisitor` **80**; `CookIfNeeded` **123**; `VisitParams` **159**; `RequiresAudioProcessing` **200** | `ParamVisitor` supports Float/Int/Bool/Text/Color only. `set` in a kernel can drive Float/Int/Bool params; Text and Color are out of scope (§8). |
| `src/core/NodeFactory.h` | `MakeNode`, `DuplicateNames()`, `REGISTER_NODE` | `emit` resolves a type-name literal through the same factory the spawn menu uses. `DuplicateNames()` must stay empty. |
| `CMakeLists.txt` | `COMMON_SOURCES` **206** | Where new `src/core/field/*.cpp` files are added. |

---

## 4. Files to create and modify

### 4.1 Create

| Path | Contents |
|---|---|
| `src/core/field/FieldGraphKernel.h` | The graph-domain intrinsics' signatures, the `EmitSpec` / `ConnectSpec` / `SetSpec` value types the interpreter produces, and `struct GraphPlan { std::vector<EmitSpec> emits; std::vector<ConnectSpec> connects; std::vector<SetSpec> sets; std::vector<PlaceSpec> places; };`. A plan is **pure data** — it names types and keys, never `INode*`, never a node index. |
| `src/core/field/FieldGraphKernel.cpp` | The interpreter: walks the typed IR of a rate-zero kernel and produces a `GraphPlan`. No Infinite headers beyond `NodeFactory.h` for type-name validation. Must be deterministic: same source + same `param` values ⇒ byte-identical plan. |
| `src/core/field/FieldGraphHost.h` | `struct IFieldGraphHost` — the **only** seam between Field and `src/main.cpp`. Pure virtual: `int Mount(const std::string& typeName, const std::string& category, float x, float y)` → new node index or `-1`; `void Unmount(int nodeIndex)`; `bool SetParam(int nodeIndex, const std::string& paramName, float value, std::string& outError)`; `bool Connect(int srcIndex, int srcSlot, int dstIndex, int dstSlot, std::string& outError)`; `void Place(int nodeIndex, float x, float y)`; `bool Alive(int nodeIndex) const`; `bool Spawnable(const std::string& typeName) const`. Nothing in this header mentions `GraphNode`, `gNodes`, ImGui or ImNodes. |
| `src/core/field/FieldGraphReconciler.h` / `.cpp` | The diff (§5.3): takes a `GraphPlan` plus the current `key → nodeIndex` ownership map plus an `IFieldGraphHost&`, and produces mount/update/remount/unmount actions. **This is the file the tests hammer** — it must be unit-testable with a fake host and no ImGui, no OpenGL, no audio. |
| `src/core/field/FieldGraphOwnership.h` / `.cpp` | `class FieldGraphOwnership` — the persisted `key → nodeIndex` map plus its serialise/deserialise (`ToText()` / `FromText()`) and `Remap(const std::map<int,int>&)`. Storage format is one `key=index` pair per space-separated token, keys escaped; it lives in a single `Text` param on the kernel node (§5.6). |
| `src/nodes/FieldGraphNode.h` / `.cpp` | The `Field Graph` node type: a source text box, a compile-error strip, a "Regenerate" button, the read `param` knobs (from step 5), a status line ("16 nodes, 2 missing"), and its `VisitParams` exposing `source` (Text), `uid` (Text), `ownership` (Text) plus the kernel's declared `param`s. Follow `.claude/skills/node-ui-pillars` — symmetry and dark-mode contrast are non-negotiable. |

### 4.2 Modify — the whole permitted diff to existing code

Step 10 is not purely additive (D6). This is the complete list; anything outside it is a
scope violation.

| File | Change | Why it cannot be avoided |
|---|---|---|
| `CMakeLists.txt` | Add the new `src/core/field/*.cpp` and `src/nodes/FieldGraphNode.cpp` to `COMMON_SOURCES` (line **206**). | Build. |
| `src/main.cpp` | `#include "nodes/FieldGraphNode.h"`, one `REGISTER_NODE(FieldGraphNode, Field Graph, Field)`, one body-draw dispatch entry, one `SpecificNodeHelpText` entry (**24153**) and one `NodeHelpText` entry (**24472**). | D11 — the standard four-point node wiring. |
| `src/main.cpp` | Add `struct MainGraphHost final : IFieldGraphHost` in the anonymous namespace, below `RemoveNodeByIndex` (**25666**) so both `SpawnNode` and `RemoveNodeByIndex` are in scope. It is a **thin forwarder**: `Mount` → `SpawnNode`, `Unmount` → `RemoveNodeByIndex`, `Alive` → `FindNodeByIndex(i) != nullptr`, `Spawnable` → `IsUserSpawnable`, `Connect` → the same `WouldCreateAudioCycle`/`WouldCreateNoteCycle` gate used at `:4684`/`:4689`. No policy lives here. | `SpawnNode`/`RemoveNodeByIndex` are file-local to `main.cpp`. |
| `src/main.cpp` | Add `void RemapFieldGraphOwnership(const std::map<int,int>& remap)` immediately after `RemapViewportPanelNodes` (**27045**), and call it from `Undo()` (**27058**) and `Redo()` (**27072**) on the line after each existing `RemapViewportPanelNodes(remap)` call (**27067**, **27081**). | D9 — undo/redo reassign every index; without this the ownership map points at strangers after one Cmd+Z. |
| `src/main.cpp` | `LoadPatchFrom` (currently calls `ApplyPatchData(data)` with no remap) passes a `std::map<int,int>` and calls `RemapFieldGraphOwnership` with it. | Saved indices are remapped on load exactly as on undo; the kernel's saved map is in *saved* index space. |
| `src/main.cpp` | In the paste body (**50100-50135**), after the group re-membership loop and before `ApplyClusterLinks` (**50134**): give every pasted `FieldGraphNode` a **fresh uid** and rewrite the pasted ownership entries through `newByOrig`. Mirror the `RandomNode::NextSeed()` re-seed at **50117-50118**. | §5.7 case 3 — two kernels with the same uid fight over the same nodes forever. |
| `src/main.cpp` | Add the four fixtures of §7 (`INFINITE_FIELDGRAPHTEST`, `INFINITE_FIELDGRAPHRATETEST`, `INFINITE_FIELDGRAPHUNDOTEST`, `INFINITE_FIELDGRAPHBLASTTEST`), each guarded by `getenv`, following `INFINITE_UNDOTEST` (**42571**). | §7. |
| `.claude/skills/run-infinite-hygiene/driver.sh` | Register the four fixtures in `FULL_TESTS=(` (line **173**); put only `FIELDGRAPHRATETEST` in `TIER1_CHECKS=(` (line **79**) — it is the fast one. | The auto-tier escalation triggers on `Patch.cpp`/`NodeFactory`/`INode.h`, none of which step 10 touches, so unregistered fixtures never run. |
| `src/core/field/FieldDomain*.{h,cpp}` (step 04's files, exact names per D1) | Add the single post-fixpoint assertion of §5.2 and its error message. **No new pass.** | I3, I7. |
| `src/core/field/FieldGlobals*.{h,cpp}` (step 04/05's globals bridge) | Classify each `ExprGlobals::Global` as `graph` or `frame` by constant-foldability (§5.5). | D3. |

### 4.3 Must **not** be modified

Touching any of these is a review failure, not a judgement call.

| File | Why |
|---|---|
| `src/core/ExprGlobals.h` / `.cpp` | The list-order / structurally-impossible-cycle guarantee (**h:22-27**) and `IsValidName` (**h:45**) are both exit-criterion items. Field *reads* globals; it does not change how they are evaluated, ordered, named or stored. |
| `src/core/Patch.h` / `src/core/Patch.cpp` | Step 10 adds **no new line kind** and no new node tag. Generated nodes serialise as ordinary `node` records; the ownership map rides in an existing `s` (Text) param on the kernel node. Also: touching `Patch.cpp` silently escalates the hygiene driver to the full tier, masking the fact that you forgot to register your fixtures. |
| `src/core/INode.h` | Adding a field to `INode` for "generated-ness" is the tempting wrong answer. It would make every node type pay for a feature almost none of them use, and it is unnecessary — ownership lives on the kernel, not on the children (§5.6). |
| `src/core/Modulation.h` / `.cpp` | `RestoreLink`, `UnbindAllFor` and the `(nodeIndex, paramIndex)` key already do everything §5.7 needs. |
| `src/core/NodeFactory.h` / `.cpp` | `emit` uses `MakeNode` as it stands. `DuplicateNames()` must still return empty. |
| Every existing node type under `src/nodes/` | A generated node is an ordinary node. If a node type needs changing to be emittable, it is not emittable in v1. |
| Anything under `/Users/namansoni/BespokeSynth`, or any Kronos / Cmajor / SuperCollider source | I1. |

---

## 5. Procedure

Nine phases. Each ends in a state where the build is green and the full harness passes.
Do not start a phase before the previous one's check passes.

### Phase 0 — branch and baseline

```bash
cd /Users/namansoni/infinte
git checkout -b feature/field-step-10-graph-domain
bash .claude/skills/run-infinite-hygiene/driver.sh --full
```

Record the baseline. If anything is already failing on `main`, **stop and report it** —
step 10's exit criterion is "nothing in steps 1-9 regressed", which you cannot assert
against a red baseline.

### Phase 1 — the surface: four intrinsics, no new syntax

The `graph` domain gets exactly four intrinsics and **no new keywords, no new operators,
no annotations** (I3, I5).

| Intrinsic | Signature | Notes |
|---|---|---|
| `emit` | `emit("Type Name", k0, k1, ...) -> handle` | `"Type Name"` is a literal-only interned string (D7). The trailing `int` arguments are the **key path** (§5.3). Returns an opaque handle valid only within this kernel run. |
| `connect` | `connect(srcHandle, srcSlot, dstHandle, dstSlot)` | Slots are `int` literals or graph-domain ints. Both handles must come from `emit` in this run. |
| `set` | `set(handle, "paramName", value)` | `value` is a graph-domain `float`/`int`/`bool`. `"paramName"` is a literal-only interned string. |
| `place` | `place(handle, x, y)` | Canvas position. Optional — see the hand-edit table in §5.3.4. |

Worked example — a four-voice detuned stack whose voice count is a `param`:

```
param int voices = 4 [1, 8]

for (i = 0; i < 8; i++) {
   if (i < voices) {
      osc  = emit("Wavetable", i)
      filt = emit("Audio Filter", i)
      connect(osc, 0, filt, 0)
      set(osc, "detune", (i - 3.5) * 4.0)
      set(filt, "cutoff", 400.0 + i * 200.0)
      place(osc,  0.0,   i * 160.0)
      place(filt, 320.0, i * 160.0)
   }
}
```

Nothing in that kernel mentions `t`, `P`, `uv` or `in`, so the fixpoint infers `graph`
and it is a graph kernel. **That is the entire declaration mechanism.**

Wrong / right on the surface:

| Wrong | Right | Why |
|---|---|---|
| `@graph kernel voices { ... }` | (nothing) | I3 — rate is inferred. |
| `name = "osc" + i` then `emit(name, i)` | `osc = emit("Wavetable", i)` | D7 — a type-name literal is a compile-time token, never a value. Refuse at parse time with `a node type name must be a literal here`. |
| `emit("Wavetable")` inside a `for` | `emit("Wavetable", i)` | Every iteration would produce the same key and the diff would collapse N nodes into one. Refuse — see §5.3.2. |
| `for (i = 0; i < voices; i++)` where `voices` is a `param` | `for (i = 0; i < 8; i++) { if (i < voices) { ... } }` | Loop bounds must be compile-time constants so the plan is statically bounded (§6 T5). The `if` gives the same result and keeps the bound literal. |
| `set(h, "cutoff", 400 + t)` | `set(h, "cutoff", 400)`, and let the emitted node be modulated | `t` is frame-domain — §5.2 refuses it. |

### Phase 2 — rate-zero enforcement

**Rule: a graph kernel's every value must be in the `graph` domain. There is no
transfer *into* `graph` (D8) — `graph` is a source, never a sink.**

This needs **no new analysis**. Step 04 already computes a domain for every IR value as
a dataflow fixpoint over the lattice `graph ⊑ frame ⊑ {element | pixel | sample}`
(`field-domains` lines 46-48), with `element`, `pixel` and `sample` mutually
incomparable. After that fixpoint converges, add one walk:

```
for each IR value v reachable from an emit/connect/set/place argument:
    if domain(v) != Domain::Graph:
        error at v.span
```

Because domains are seeded at the *leaves* (`t` → frame, `P`/`N` → element, `uv` →
pixel, `in`/`out` → sample) and joined upward, this catches transitive dependencies for
free. In particular it catches the three that people miss:

| Sneaky case | Caught because |
|---|---|
| `rand()`, `noise(x)`, `sh(x, rate)` | They are seeded from `t`, so their result joins to `frame`. **Non-determinism in a graph kernel is refused as a side effect of the rate rule** — which is exactly what §5.6 needs to be able to serialise instead of regenerate. |
| A global that mentions `t` | §5.5 classifies it `frame`; the join propagates. |
| A `reduce` from any finer domain | There is no transfer into `graph`; the reduce itself is refused at the operator level (`field-domains` line 95) before the domain walk even runs. |

**The exact error message.** One shape, used everywhere, three lines: what, why, what to
do instead. It must name the *leaf* that introduced the finer domain, not just the
expression that used it.

```
FieldGraph.field:4:24: error: `t` is a frame-domain value; this kernel emits nodes and
   runs at edit time (rate 0), so it cannot read anything that changes per frame.
   `t` reaches this expression through: t -> phase -> cutoff
   Fix: move the time-dependent part into the node this kernel emits (set a constant
   here and modulate that param), or replace `t` with a graph-domain constant.
```

Rules for the message, all testable:

| Requirement | Reason |
|---|---|
| Names the offending leaf (`t`, `P`, `N`, `uv`, `in`, `out`, or the global's name) | "Rate error" alone is unactionable. |
| Prints the provenance chain leaf → … → use | §2.1: the metaprogramming layer is the part users cannot self-serve; the chain is the teaching. |
| Says the domain by name (`frame`, `element`, `pixel`, `sample`) | Ties the error back to the one concept the language has. |
| Always ends in a `Fix:` line naming a concrete alternative | A refusal without an alternative fails §2.1. |
| Reports **all** rate violations in one compile, not just the first | Otherwise fixing an 8-line kernel is 8 compiles. |
| Never says the word "rate zero" without also saying "edit time" | "Rate zero" means nothing to a first-time reader. |

And the negative test that must exist: a graph kernel is **not** allowed to be silently
promoted. If a value in a graph kernel infers `frame`, the kernel does not become a
frame kernel that runs 60 times a second spawning nodes — it is a **compile error**. Say
so in a comment at the check site, because "just let it run at frame rate" is the
obvious-looking fix a future contributor will reach for, and it spawns 60 nodes a second.

### Phase 3 — identity and reconciliation

This is React's reconciler, and it fails in exactly React's way if you get keys wrong.

#### 5.3.1 The key

**`key = "<lvalueName>#<k0>.<k1>..."`**, and the node's owner is the kernel's persisted
`uid`.

| Component | Source | Why not the alternative |
|---|---|---|
| `uid` | A random 16-hex-char string minted when a `FieldGraphNode` is first constructed, stored in its `uid` Text param, re-minted on paste. | Not `GraphNode::index`: `NewPatch()` sets `gNextIndex = 1` (`src/main.cpp:26273`) and `ApplyPatchData` (`:26787`) reassigns every index, so an index-based owner id is wrong after the first undo. Not the node's title: titles are user-editable and duplicable. |
| `lvalueName` | The identifier the `emit` result is assigned to (`osc`, `filt` above). Assignment to a named local is **required**; a bare `emit(...)` statement is a parse error. | Not the emit's source ordinal: inserting an `emit` above shifts every ordinal below it and remounts the whole subgraph — P7's bug class exactly (`node-ui-pillars` line 107). Not the source span: whitespace edits would remount. The name is also how `state` cells and `param`s are keyed (steps 5 and 6), so the language has **one** identity rule. |
| `k0.k1...` | The `int` key-path arguments to `emit`, rendered in order. | Not the iteration ordinal implicitly: it must be *written*, so that the author has said which loop variable is the identity. |

Renaming `osc` to `sawA` unmounts every `osc#*` and mounts a fresh `sawA#*`. That is the
same rule as renaming a `state` cell, and the node body must say so explicitly:
`renaming an emit target replaces its nodes`.

#### 5.3.2 Keys that are refused at compile time

| Refused | Message |
|---|---|
| `emit` inside a `for` with no key path | ``emit("Wavetable") is inside a loop and has no key, so every iteration would name the same node. Add the loop variable: emit("Wavetable", i)`` |
| Two `emit`s with the same `lvalueName` | ``two emit targets are both named `osc`; each emit target must have its own name`` |
| A key path whose values are not provably distinct across iterations (e.g. `emit("X", 0)` inside a loop) | ``the key for `osc` is the constant 0, so all 8 iterations name the same node. Use the loop variable.`` |
| An `emit` result that is never assigned | ``emit(...) must be assigned to a name; that name is the identity of the nodes it creates`` |

The first and third are the React `key` warning, promoted to an error. A warning is not
enough here: the failure mode is silent node loss, not a console message.

#### 5.3.3 The diff

The ownership map is rebuilt as a **`std::map<std::string,int>` of key → node index**,
persisted (§5.6) and remapped across undo/redo/load (§4.2). Every read of it resolves
through `FindNodeByIndex` (`src/main.cpp:4092`) and treats `nullptr` as "absent, not an
error" — the same tolerance `gGroupMembers` documents at `src/main.cpp:455-460`.

```
reconcile(plan, ownership, host):
  live   = { key: idx  for (key, idx) in ownership  if host.Alive(idx) }
  wanted = { spec.key: spec  for spec in plan.emits }        # duplicate keys already refused at compile time

  actions = []
  for key, spec in wanted:                    # deterministic order: plan order
      if key not in live:
          actions += MOUNT(key, spec)
      elif typeOf(live[key]) != spec.typeName:
          actions += REMOUNT(key, spec)       # unmount + mount, carrying §5.7's rescue payload
      else:
          actions += UPDATE(key, spec)        # set only the params this plan drives
  for key, idx in live:
      if key not in wanted:
          actions += UNMOUNT(key, idx)

  # cables are asserted after every node exists, never during the mount loop
  actions += RECONNECT(plan.connects)
```

Non-negotiable properties of this algorithm, each with the bug it prevents:

| Property | Bug prevented |
|---|---|
| `UPDATE` is strictly preferred over `REMOUNT` — a node is only remounted if its **type** changed | A remount changes the node index, which destroys every modulation binding keyed `(nodeIndex, paramIndex)` (`src/core/Modulation.h:53`) and every inbound cable via `DisconnectAllTo` (`src/main.cpp:24862`). Turning a knob must not silently detach the patch. |
| `UNMOUNT` runs **after** every mount and update | Unmounting first frees indices that `gNextIndex` will hand back out, so a mount can land on the index a just-unmounted node had, and any half-updated map now aliases. |
| Cables are asserted in one pass at the end | A `connect` whose destination has not been mounted yet is otherwise dropped, and the kernel's emitted topology depends on statement order. |
| The whole thing is idempotent | `reconcile(plan) ; reconcile(plan)` must produce zero actions the second time. This is a fixture assertion in §7, not a hope. |
| The action list is computed **before** any host call | `SpawnNode` invalidates `GraphNode*` (`src/main.cpp:27337-27341`) and mutates `gNodes`; computing while mutating is how you read a dangling pointer. Plan fully, then execute. |

#### 5.3.4 Which hand-edits survive a regeneration

This table is the user-visible contract. It belongs in the node's help text
(`SpecificNodeHelpText`, `src/main.cpp:24153`) as well as here.

| Hand edit to a generated node | Survives regeneration? | Why |
|---|---|---|
| Dragging it to a new canvas position | **Yes**, unless the kernel calls `place()` for that key | `place` is optional precisely so a kernel can lay out once and then leave the user alone. A kernel that calls `place` owns position; one that does not, does not. |
| Collapsing / expanding the body, resizing it | **Yes** | Not in the plan; the diff never touches it. |
| Bypass toggle | **Yes** | Same. |
| Editing a param the kernel does **not** `set` | **Yes** | The `UPDATE` action writes only the params in `plan.sets` for that key. |
| Editing a param the kernel **does** `set` | **No — clobbered every regeneration** | The kernel is the source of truth for those. The UI must **mark kernel-driven params read-only**, drawn the same way a param with a modulation cable is drawn. Silently reverting a knob the user can still turn is the worst option available. |
| Attaching a modulation cable to a kernel-driven param | **Refused at bind time**, with the reason | Two writers, one param. |
| Attaching a modulation cable to a free param | **Yes** — and it is rescued across a remount (§5.7 case 4) | |
| A cable the kernel drew, rerouted by hand | **No** — reasserted on the next regeneration | `plan.connects` is authoritative for cables between generated nodes. |
| A cable the user drew from a generated node to a **non**-generated node | **Yes** — rescued across a remount (§5.7 case 5) | The rest of the patch must not silently detach. |
| Deleting the node by hand | Honoured until the next regeneration, then re-mounted (§5.7 case 1) | |
| Putting it in a group | **Yes** — group membership is carried across a remount (§5.7 case 2) | |
| Renaming the node | **Yes** | Title is not part of the plan. |

### Phase 4 — undo/redo

#### 5.4.1 The policy

**One regeneration is exactly one undo entry, and it is the *same* entry as the user
edit that caused it.**

Implementation — this is the paste path (`src/main.cpp:50100-50135`) verbatim in shape:

```cpp
// in the FieldGraphNode's regeneration entry point, main-thread, after the canvas draw
PushUndoCheckpoint();                    // src/main.cpp:27022 - captures the graph BEFORE the edit
gSuppressUndoCheckpoints = true;         // :26236 - SpawnNode/RemoveNodeByIndex each push their own otherwise
gDeferAudioRebuild       = true;         // :25087 - one rebuild, not one per node
   ... execute the action list ...
gDeferAudioRebuild       = false;
RebuildAudioTopology();                  // exactly once
gSuppressUndoCheckpoints = false;
```

Note the ordering: the checkpoint is pushed **before** the flag is set, because
`PushUndoCheckpoint()` early-returns while the flag is set.

#### 5.4.2 The rejected alternatives, each named by the bug it causes

| Rejected | The bug |
|---|---|
| **No suppression** — let `SpawnNode` (`:4974`) and `RemoveNodeByIndex` (`:25671`) each push their own checkpoint | A 16-voice kernel costs 32 checkpoints per regeneration. Cmd+Z appears to do nothing (it undoes one internal spawn out of 32), and with `kMaxUndoDepth = 200` (`:26243`) seven regenerations evict the user's entire real history. This is **data loss**, not noise. |
| **No checkpoint at all** — suppress the outer one too, "regeneration isn't a user edit" | Undo rewinds the source text without rewinding the nodes it produced. The graph now contains nodes whose owner key points at a kernel that no longer asks for them, and the next regeneration unmounts them — so a single Cmd+Z destroys work two steps later, at a moment unconnected to the keypress. |
| **One checkpoint per emitted node but coalesce in `PushUndoSnapshot`** (`:27007`) by comparing snapshots | `Patch::Data` comparison is a whole-graph string compare per push; 32 of them per keystroke on a large patch is a visible stall, and coalescing-by-equality merges *genuinely distinct* consecutive edits elsewhere in the app. |
| **Regenerate on undo** instead of restoring the generated nodes from the snapshot | The snapshot already contains them — `BuildPatchData()` (`:25749`) reads live `gNodes` and generated nodes are ordinary members of it. You would restore N nodes and then emit N more. Duplication on every Cmd+Z. |
| **A separate undo stack for graph regenerations** | Two stacks means an interleaved history the user cannot reason about: Cmd+Z would sometimes step the graph stack and sometimes the main one, depending on what was focused. |

#### 5.4.3 Undoing *past* a regeneration

Nothing special happens, and making something special happen is the bug.

| Step | What actually runs |
|---|---|
| User presses Cmd+Z | `Undo()` (`src/main.cpp:27058`) pops `gUndoStack` (a `std::deque<Patch::Data>`, `:26241`) and calls `ApplyPatchData(snapshot, &remap)` (`:26787`). |
| `ApplyPatchData` | Calls `NewPatch()` (`:26245`), which retires every node and sets `gNextIndex = 1` (`:26273`); then respawns each record with a **fresh** index and fills `remap[rec.index] = spawned->index`. Generated nodes are ordinary records and come back like everything else. Globals are restored at `:26908-26910`. One `RebuildAudioTopology()` at the end. |
| `Undo()` then | `RemapViewportPanelNodes(remap)` (`:27067`) — **and, added by step 10, `RemapFieldGraphOwnership(remap)` on the next line.** |
| The kernel | Its `source`, `uid` and `ownership` Text params were in the snapshot, so it comes back with its map intact-in-saved-space, and the remap moves it into live-index space. |

**The one rule that makes this work: a compile is not a regeneration.** After a load or
an undo the kernel *compiles* (to populate the editor and its error strip) but
**reconciles nothing**. Regeneration is triggered only by:

1. the source text changing and compiling clean, **debounced** (same debounce step 09 uses — do not recompile per keystroke);
2. a `param` the kernel reads changing value;
3. the explicit "Regenerate" button.

Implement with an explicit `mRealised` flag that the param-load path sets true, and
assert it: if a regeneration ever fires from inside `ApplyPatchData`, that is a bug, and
it is worth a `#ifndef NDEBUG` re-entrancy guard that aborts, because the symptom
otherwise is silent node duplication on every undo.

### Phase 5 — absorbing `ExprGlobals`

#### 5.5.1 The guarantee that must survive, verbatim

`src/core/ExprGlobals.h:22-27`:

> Evaluation order is the order the list is in, and each global sees `t` plus every
> global *above* it - so they can build on each other, and a cycle is structurally
> impossible rather than something to detect at runtime. A global that fails to evaluate
> keeps its last good value and records the error for the editor to show.

Nothing in step 10 changes the list, its order, its evaluation, its storage, or
`IsValidName` (`:45`, which refuses `t`, `pi`, `lo`, `hi`). Field is a **reader**.

#### 5.5.2 The correction to the brief (D3)

Globals are evaluated once per frame from `src/main.cpp:37421` with the current `t`, and
each global may mention `t`. So "globals are graph-domain constants" is **false as
written**. The correct rule, and it falls straight out of the same inference machinery:

| Global's expression | Inferred domain | Readable from a graph kernel? |
|---|---|---|
| Constant-foldable — no `t`, no `rand`, no `noise`, no `sh`, and every global it references is itself `graph` | `graph` | **Yes** |
| Mentions `t`, or `rand`/`noise`/`sh`, or references a `frame` global | `frame` | **No** — same rate-zero error as §5.2, naming the global and the offending name inside it |

Classification is a fixpoint over the same list, in the same order, and because a global
can only reference globals *above* it, that fixpoint converges in one pass with no cycle
handling. That is the list-order guarantee paying for itself twice.

Error shape for the second row:

```
FieldGraph.field:2:14: error: the global `wobble` is frame-domain (its expression reads
   `t`), and this kernel emits nodes at edit time (rate 0).
   Fix: read a global that does not depend on `t`, or move the dependency into the node
   this kernel emits.
```

#### 5.5.3 Writing globals: refused

**A graph kernel may read globals. It may never write, create, reorder, or delete one.**

If a kernel could write a global, the list-order property dies: a kernel that runs at
edit time and mutates the list could produce a list whose entry *k* references entry
*k+1*, and "a cycle is structurally impossible" degrades into "a cycle must be detected
at runtime" — the exact thing `ExprGlobals` was written to avoid. Refuse at parse time:

```
FieldGraph.field:6:4: error: a graph kernel cannot assign to the global `depth`.
   Globals are evaluated top-down so that a cycle is structurally impossible; letting a
   kernel rewrite the list would break that.
   Fix: emit a node and set its param instead, or edit the global in the Globals window.
```

#### 5.5.4 If a graph kernel would introduce a cycle the globals could not

Three distinct cycle kinds, three distinct answers:

| Cycle kind | Answer |
|---|---|
| **Value cycle among globals** | Cannot arise — §5.5.3 forbids writing. Unchanged from today. |
| **Cycle in the emitted node graph** (`connect(a,0,b,0)` and `connect(b,0,a,0)`) | `IFieldGraphHost::Connect` routes through the *same* `WouldCreateAudioCycle` (`src/main.cpp:4576`) / `WouldCreateNoteCycle` (`:4613`) gates the UI uses at `:4684`/`:4689`. A refused connect is **not** a silently dropped cable: the reconciler collects it and the kernel shows `2 cables refused: would create an audio feedback loop (osc#3 -> filt#3 -> osc#3)`. The nodes stay; only the cable is refused, which matches what dragging that cable by hand does. |
| **A kernel that emits a kernel** | `emit` refuses any `Field Graph` type outright — no metaprogramming the metaprogrammer in v1 — and independently a re-entrancy guard makes a regeneration that triggers a regeneration an assert. Without both, a kernel emitting itself is unbounded node creation at edit time, i.e. the app never returns to the event loop. |

### Phase 6 — save / load

#### 5.6.1 The two options, costed

| Dimension | **(a) Serialise generated nodes as ordinary node records** | **(b) Regenerate from the kernel on load** |
|---|---|---|
| File size | +1 `node` record per generated node (a 16-voice kernel ≈ 16 records ≈ a few KB) | 1 record for the kernel |
| Version skew — an emitted type is renamed or removed in a later build | Patch still opens; the unknown type is skipped with a warning by the existing loader path and the rest of the graph survives | The kernel re-emits a type this build does not have; the **whole generated subgraph is missing**, with no record in the file of what it was |
| A patch opened on a build without Field compiled in | The generated nodes load and work; only the kernel node is skipped | The entire subgraph vanishes |
| The kernel's source changed between save and load (e.g. the user edits, saves, reverts the text by hand) | The file loads exactly as saved; the diff runs only when the user next edits, and they see it happen | The file silently opens as a **different patch**. The saved work is gone with no diff and no undo entry |
| Determinism | Absolute — the file *is* the graph | Depends on the kernel being pure. §5.2 already refuses `rand`/`noise`/`sh` (they are `t`-seeded), but a changed `param` default or a compiler change still shifts the output |
| Hand-edits on generated nodes (position, free params, groups, outbound cables) | Preserved by the file | Lost on every load |
| Undo / redo | **Free.** Undo snapshots are `Patch::Data` from `BuildPatchData()` (`src/main.cpp:25749`), which reads live `gNodes`, and generated nodes are in `gNodes` | Broken. Every undo would have to re-run the kernel, and `ApplyPatchData` (`:26787`) has no compiler and no business having one |
| Load cost | One respawn per node — identical to any other patch of the same size | A compile plus N spawns, on the main thread, during load |
| New `Patch` line kind needed | **No** | No |
| Failure mode when it goes wrong | A stale-looking subgraph the user can see and fix | Silent absence |

#### 5.6.2 Decision: **(a) — serialise generated nodes as ordinary node records**

Three reasons, in order of force:

1. **Undo already forces it.** `BuildPatchData()` reads live `gNodes`; a generated node is in `gNodes`; therefore it is already in every undo snapshot whether option (b) likes it or not. Choosing (b) for *load* would give the same `Patch::Data` two different meanings depending on whether it arrived from a file or from `gUndoStack` — and both go through the same `ApplyPatchData`. That is not a design, it is a latent bug.
2. **Every failure mode of (a) is visible; every failure mode of (b) is silent.** §2.1: the users who lose here are the ones who did not write the kernel.
3. **(a) needs no change to `Patch.h`/`Patch.cpp` at all.** No new line kind, no new node tag, nothing added to the grammar at `src/core/Patch.h:16-58`, nothing added to the tag reader at `src/core/Patch.cpp:379-386`.

#### 5.6.3 How ownership persists without touching `INode`

The tempting design — a `gen.owner` / `gen.key` param on each generated node — is
**wrong**, because it would require modifying `VisitParams` on every existing node type
(or `INode` itself, which §4.3 forbids). Instead:

**Ownership lives on the kernel, as one `Text` param.**

| Concern | Mechanism |
|---|---|
| Storage | `FieldGraphNode` exposes `ownership` via `ParamVisitor` as Text. It serialises through the existing `s <name> <escaped>` path (`src/core/Patch.cpp:113` write, `:379-386` read) using `EscapeLine` / `UnescapeLine` (`:49`, `:64`) — do not hand-roll escaping; keys can contain characters the line grammar cares about. |
| Format | Space-separated `escapedKey=index` pairs. Parse defensively: an unparseable pair is skipped, not fatal. |
| Index space | The saved map is in **saved** index space. `ApplyPatchData` remaps saved→live; `RemapFieldGraphOwnership(remap)` (new, next to `RemapViewportPanelNodes` at `src/main.cpp:27045`) moves the map with it, from `Undo()`, `Redo()` and `LoadPatchFrom`. |
| Stale entries | Never fatal. Every read resolves through `FindNodeByIndex` (`:4092`); `nullptr` means "absent", exactly as `gGroupMembers` documents (`:455-460`). |
| Size | An entry is ~24 bytes. A 200-node kernel is under 5 KB in one line — well within the line grammar, which puts free-form content last on the line. |

### Phase 7 — blast radius

Five cases, each with a defined behaviour and the alternative it rejects.

#### Case 1 — a generated node is deleted by hand

The user selects it and presses Delete; `ed::BeginDelete()` (~`src/main.cpp:50137`)
calls `RemoveNodeByIndex(...)`, which tears it down generically. The kernel's map now
names a dead index.

| | |
|---|---|
| **Immediately** | Nothing. The delete stands; the kernel does **not** regenerate, because nothing the kernel reads changed. The kernel body shows `1 of 16 emitted nodes missing`. |
| **On the next regeneration** | The key resolves to no live node (`Alive(idx)` false), so it is treated as unmounted, and it is **re-mounted**. A hand-delete is undone by the next regeneration. |
| **The user's escape hatch** | Reduce the `param` that produced it, edit the kernel, or delete the kernel. |
| **Rejected: tombstone the key** so a hand-deleted node stays deleted | A hidden per-key suppression list the user cannot see or edit, which makes the kernel's output no longer a function of its source — you could not explain the graph by reading the kernel. Directly against §2.1. |
| **Rejected: regenerate immediately on the delete** | Deleting a node and watching it reappear in the same frame is indistinguishable from a broken app. |

#### Case 2 — generated nodes inside a group

`gGroupMembers` is `std::map<GroupNode*, std::set<int>>` (`src/main.cpp:455-460`), keyed
by pointer, membership-only-grows, stale indices skipped.
`RemoveNodeByIndex` erases the index from every group. A remount produces a **new** index
that is not in the group, so the node would silently leave the group it was in.

| | |
|---|---|
| **Behaviour** | The reconciler carries group membership across a `REMOUNT`: before unmounting, record the owning group via `GroupOwning` (`:20411`) / `IndexOfGroupNode` (`:20424`); after mounting the replacement, re-insert into that group. This is the same three lines paste already runs at `:50122-50133`. |
| **`emit` of a Group** | Refused. `IsUserSpawnable` (`:398`) returns false for `"Group"`, `"Delete Selected"`, `"Transform Selected"`, `"Extrude Selected"`; `emit` refuses exactly that set, naming the type: ``emit cannot create "Group": it is not a spawnable node type``. |
| **Group deleted while the kernel still owns members** | Group deletion does not delete members; the members are simply ungrouped. No kernel involvement. |
| **Rejected: emit into a group by having the kernel name one** | Needs a way to reference a non-generated node from a kernel, which needs stable identity for hand-placed nodes, which does not exist. §8. |

#### Case 3 — copy/paste of a generated subgraph

Three sub-cases, all decided by the `RandomNode::NextSeed()` precedent at
`src/main.cpp:50117-50118` — paste re-identifies anything whose identity must be unique.

| Copied | Behaviour |
|---|---|
| **Kernel + all its generated nodes** | The pasted kernel gets a **fresh `uid`**, and its ownership entries are rewritten through `newByOrig` (`:50111`) to point at the pasted copies. Two kernels now exist, each owning its own subgraph. **Rejected: keep the uid** — both kernels then claim the same nodes and each regeneration steals them from the other, forever. |
| **Generated nodes without their kernel** | The copies are **orphaned into ordinary nodes**: they appear in no ownership map and no kernel will ever touch them. **Rejected: attach them to the original kernel** — it would immediately unmount them as unknown keys, so paste would appear to do nothing. |
| **Kernel without its generated nodes** | The pasted kernel owns nothing; its first regeneration mounts a complete set. Note `doCopy` (~`:50045`) already drags group *members* along with a copied group — do **not** add an equivalent for generated children. Regeneration is cheaper, deterministic, and avoids a second identity rewrite. |

In all three, the existing paste checkpoint at `:50108` covers the whole operation, so a
paste is still one undo step.

#### Case 4 — a modulation cable on a generated node's param, when that node is unmounted

`RemoveNodeByIndex` calls `Modulation::UnbindAllFor(index)` (`src/core/Modulation.h:125`)
and the palette equivalent. The binding is **destroyed**, not stashed, and the remounted
node has a different index, so `(nodeIndex, paramIndex)` (`Modulation.h:53`) no longer
matches on either half.

| | |
|---|---|
| **`UPDATE`** (params changed, type did not) | Nothing happens — the index is unchanged, the binding is untouched. This is the main argument for §5.3.3's "prefer UPDATE over REMOUNT". |
| **`REMOUNT`** (type changed) | The reconciler **rescues** the bindings: before unmounting, capture via `CaptureClusterLinks` (`src/main.cpp:4743`), which already collects `Modulation::Instance().Links()` and `PaletteBinding::Instance().Links()` for a set of indices; after mounting, reinstall via `ApplyClusterLinks` (`:4785`). Use `Modulation::RestoreLink` (`Modulation.h:115`) semantics, not a live bind: the new node has not drawn a frame, so a bind that derives its range from the current frame would produce a wrong range. Also carry `Expressions()` (`Modulation.h:153`) via `SetExpression` (`:149`). |
| **True `UNMOUNT`** (the key is genuinely gone) | The unbind is correct and final. The kernel body reports `2 modulation bindings dropped with unmounted nodes` — the user must be told, because the binding was theirs, not the kernel's. |
| **Binding onto a kernel-driven param** | Refused at bind time (§5.3.4): two writers, one param. |
| **Rejected: silently rebind by param *name* after the fact** | Param indices are ordinal (`node-ui-pillars` P7); a rebind-by-name that guesses the index reattaches the cable to the wrong knob when a param list differs even slightly. Capture-and-restore the actual `Source` instead. |

#### Case 5 — a generated node feeding a non-generated one

The user drew a cable from a generated node's output into a hand-placed node's input.
That cable lives in the *consumer's* input field, and `DisconnectAllTo(dying)`
(`src/main.cpp:24862`) nulls every such pointer generically on unmount.

| | |
|---|---|
| **`REMOUNT`** | Rescued, by the same `CaptureClusterLinks` / `ApplyClusterLinks` pair as case 4 — note `ApplyClusterLinks`'s documented behaviour (`:4779-4784`): a source outside the copied set is rewired to the *original* external node and re-validated through `FindNodeByIndex`, which is exactly the semantics needed here. |
| **True `UNMOUNT`** | The cable is genuinely gone, and the consumer loses its input. **This must be reported before it happens**: the kernel body shows `unmounting osc#5 will detach 1 cable to "Reverb"`, and it proceeds. It proceeds because refusing would make the kernel's output depend on what the user happened to wire up; it is reported because a knob-turn that silently detaches half a patch is the single worst outcome in this document. |
| **Recovery** | One Cmd+Z, because §5.4 made the whole regeneration one undo entry. This is *why* §5.4's policy matters — it is the recovery path for case 5. |
| **A generated node feeding another generated node** | Owned by `plan.connects`; reasserted every regeneration; a hand reroute does not survive (§5.3.4). |
| **Rejected: refuse to unmount a node with user-drawn outbound cables** | The kernel could then never shrink, and the user would have to hunt for which cable is blocking it. |

### Phase 8 — the node UI

Load `.claude/skills/node-ui-pillars` first — this is a standing instruction, not a
suggestion. Symmetry and dark-mode contrast are non-negotiable. Beyond the pillars,
step 10 specifically requires:

| Element | Requirement |
|---|---|
| Source box | Monospace, error line highlighted, all rate errors shown at once (§5.2). |
| Status line | `16 nodes · 2 missing · 1 cable refused` — one line, always present, never a modal. |
| Kernel-driven params on generated nodes | Drawn read-only, in the same treatment as a modulation-bound param. Never editable-but-reverted. |
| "Regenerate" button | Explicit, always available, and the only way to force a run when nothing changed. |
| Warnings before destructive regeneration | Case 5's detach notice appears in the status line *after* the fact (the regeneration is one undoable step); it must not be a blocking dialog. |
| Node count | Keep it under ~7 visible controls (owner standing instruction: keep audio/utility nodes KHS-plugin-simple). Source box, error strip, status line, Regenerate, plus the kernel's own `param` knobs. |

---

## 6. Traps, and the bug each one prevents

| # | Trap | The bug it prevents |
|---|---|---|
| T1 | **Do not key generated nodes on `GraphNode::index`.** | `NewPatch()` sets `gNextIndex = 1` (`src/main.cpp:26273`) and `ApplyPatchData` (`:26787`) reassigns every index. One Cmd+Z and the kernel owns whichever nodes happen to land on those numbers — including hand-placed ones, which it will then unmount. |
| T2 | **Do not key generated nodes on the emit's source ordinal or its position in the plan vector.** | `node-ui-pillars` P7 (line 107) and step 05's `paramIndex`: inserting an `emit` above shifts every ordinal below it, silently remounting the entire subgraph and destroying every modulation binding and outbound cable on it. |
| T3 | **Do not hold a `GraphNode*` across a `SpawnNode` call.** | `SpawnNode` (`:4967`) does `gNodes.push_back` and returns `&gNodes.back()`; the next push can reallocate. The comment at `:27337-27341` spells this out and shows the fix used elsewhere in the file: store the `index` and re-resolve through `FindNodeByIndex` (`:4092`). A dangling `GraphNode*` in the mount loop is a use-after-free that only shows up on the 17th node. |
| T4 | **Do not run regeneration from inside the node-editor draw pass.** | The mount/unmount loop mutates `gNodes` while ImNodes is iterating it. Queue the request and run it at a safe point after the canvas draw, where paste and delete already run (~`:50073`, `:50137`). |
| T5 | **Do not allow a `param`-dependent loop bound.** | `for (i = 0; i < voices; i++)` with `voices` a runtime `param` makes the plan unbounded; a slider drag from 4 to 512 spawns 512 nodes per intermediate value on the way. Require literal bounds plus an `if` guard (§5.1), so the maximum node count is knowable at compile time and can be shown in the UI. |
| T6 | **Do not let a rate violation degrade into "just run it at frame rate".** | A graph kernel promoted to `frame` spawns nodes 60 times a second. This is the obvious-looking fix; put a comment at the check site saying it is forbidden and why. |
| T7 | **Do not bypass `WouldCreateAudioCycle` / `WouldCreateNoteCycle` (`:4576`, `:4613`) when the kernel connects.** | A kernel-built audio feedback loop is a hard lock or a runaway in the audio thread, with no UI path to undo it because the app is unresponsive. Route `Connect` through the same gate the UI uses at `:4684`/`:4689`. |
| T8 | **Do not call `RebuildAudioTopology()` per node.** | It runs `PrepareToPlay` on every audio node; 32 calls in one regeneration reset every reverb tail 32 times and stall the frame. Wrap the loop in `gDeferAudioRebuild` (`:25087`) and rebuild once, as `:42784-42787` and `:49730-49747` already do. |
| T9 | **Do not push an undo checkpoint per emitted node.** | `kMaxUndoDepth = 200` (`:26243`); a 16-node kernel is 32 checkpoints per regeneration, so seven regenerations wipe the user's real history. Suppress with `gSuppressUndoCheckpoints` (`:26236`) exactly as paste does at `:50108-50109`. |
| T10 | **Do not push the checkpoint *after* setting `gSuppressUndoCheckpoints`.** | `PushUndoCheckpoint()` (`:27022`) early-returns while the flag is set, so the whole regeneration becomes un-undoable and the user loses the graph with no recovery. Checkpoint first, then set the flag. |
| T11 | **Do not regenerate as a side effect of a compile.** | Load and undo both compile the kernel to populate its editor. If that compile reconciles, every `ApplyPatchData` both restores the generated nodes *and* emits them again — duplication on every Cmd+Z. Gate on an explicit `mRealised` flag and add a debug re-entrancy assert. |
| T12 | **Do not unmount before mounting.** | Unmounting first returns indices that `gNextIndex` will reissue, so a mount can land on an index a just-unmounted node held, aliasing a half-updated ownership map. Mount and update first, unmount last, connect last of all. |
| T13 | **Do not remount when only params changed.** | A remount changes the node index, and modulation is keyed `(nodeIndex, paramIndex)` (`Modulation.h:53`) while inbound cables are cleared by `DisconnectAllTo` (`:24862`). Turning a knob would silently detach the patch. Remount only on a **type** change. |
| T14 | **Do not add a `gen.owner` param to existing node types or a field to `INode`.** | Every node type would pay for a feature almost none use, `INode.h` is on the must-not-modify list, and it is unnecessary: ownership lives on the kernel (§5.6.3). |
| T15 | **Do not add a new `Patch` line kind or node tag.** | The reader accepts only `f i b c s` inside a node record (`src/core/Patch.cpp:379-386`); a new tag makes every patch written by this build unreadable by every previous build. It is also unnecessary (§5.6.2 reason 3). And touching `Patch.cpp` auto-escalates the hygiene driver, hiding the fact that you forgot to register your fixtures. |
| T16 | **Do not hand-roll escaping for the ownership Text param.** | `EscapeLine` (`:49`) / `UnescapeLine` (`:64`) exist because free-form values collide with the line grammar; a key containing a newline or a leading space silently truncates the map on reload, losing ownership of half the subgraph. |
| T17 | **Do not treat a stale index in the ownership map as an error.** | The map legitimately goes stale on hand-delete (§5.7 case 1). Resolve through `FindNodeByIndex` and skip `nullptr`, matching `gGroupMembers`' documented tolerance at `:455-460`. An assert here fires on ordinary user behaviour. |
| T18 | **Do not let `emit` create a non-spawnable type.** | `IsUserSpawnable` (`:398`) excludes `"Group"`, `"Delete Selected"`, `"Transform Selected"`, `"Extrude Selected"` for reasons the UI already learned. An emitted bare `Group` has nothing to auto-fit to. |
| T19 | **Do not let `emit` create a `Field Graph` node.** | Unbounded recursion at edit time; the app never returns to the event loop. Refuse the type outright *and* keep the re-entrancy guard — either alone is defeatable. |
| T20 | **Do not let a graph kernel write a global.** | It destroys the "cycle is structurally impossible" property at `src/core/ExprGlobals.h:22-27`, converting a compile-time impossibility into a runtime detection problem nobody wrote the detector for. |
| T21 | **Do not modify `ExprGlobals::IsValidName` (`h:45`).** | It is a literal exit-criterion item (README §2). It refuses `t`, `pi`, `lo`, `hi`; Field adding its own reserved names here would rename users' existing globals on load. |
| T22 | **Do not forget to register the new fixtures in `driver.sh`.** | The auto-tier escalation at driver line 290 triggers on `src/core/Patch.cpp`, `NodeFactory` and `INode.h`. Step 10 touches none of them, so an unregistered fixture never runs and the exit criterion is vacuously green. `FULL_TESTS=(` is line **173**; `TIER1_CHECKS=(` is line **79**. |
| T23 | **Do not skip `RemapFieldGraphOwnership` in `LoadPatchFrom`.** | `Undo`/`Redo` remap and load does not, so the bug appears only after quitting and reopening — the hardest kind to attribute. |
| T24 | **Do not reuse the kernel's `uid` on paste.** | Two kernels with one uid each steal the subgraph back from the other on every regeneration. Mirror the `RandomNode::NextSeed()` re-seed at `:50117-50118`. |
| T25 | **Do not read a GPL source tree to check how graph metaprogramming "should" work.** | I1. Infinite is MIT; Kronos, Cmajor, SuperCollider and BespokeSynth are GPL. Cite the CMJ 39:4 paper, never any code. |

---

## 7. Machine-checkable exit criterion

Every command below is real and runnable from the repo root. Run them **in order**. A
step that prints anything other than what its comment says is a failure; do not proceed.

### 7.1 Build

```bash
cd /Users/namansoni/infinte
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8 2>&1 | tee /tmp/field_step10_build.log
# must print 0
grep -cE '\berror\b|\bError\b' /tmp/field_step10_build.log
```

### 7.2 The four new fixtures

Each follows the `INFINITE_UNDOTEST` shape at `src/main.cpp:42571`: run headless with a
frame budget, print one `... OK` or `... FAIL` line per assertion.

```bash
BIN=build/Infinite.app/Contents/MacOS/Infinite

# --- 1. reconciliation: keys, mount/update/unmount, idempotence -----------------
env INFINITE_FIELDGRAPHTEST=1 INFINITE_EXITAFTER=240 "$BIN" > /tmp/fg_recon.txt 2>&1
# must print 0
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/fg_recon.txt
# must print a non-zero count of OK lines
grep -cE ' OK$' /tmp/fg_recon.txt

# --- 2. rate zero: every violation is a compile error, none silently promoted ---
env INFINITE_FIELDGRAPHRATETEST=1 INFINITE_EXITAFTER=120 "$BIN" > /tmp/fg_rate.txt 2>&1
# must print 0
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/fg_rate.txt

# --- 3. undo/redo: one regeneration is one entry; undo past it is clean ---------
env INFINITE_FIELDGRAPHUNDOTEST=1 INFINITE_EXITAFTER=360 "$BIN" > /tmp/fg_undo.txt 2>&1
# must print 0
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/fg_undo.txt

# --- 4. blast radius: the five cases of Phase 7 --------------------------------
env INFINITE_FIELDGRAPHBLASTTEST=1 INFINITE_EXITAFTER=480 "$BIN" > /tmp/fg_blast.txt 2>&1
# must print 0
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT' /tmp/fg_blast.txt
```

#### What each fixture must assert

| Fixture | Assertions |
|---|---|
| `INFINITE_FIELDGRAPHTEST` | `NewPatch()`; spawn a `Field Graph`; set a kernel emitting 4 nodes; regenerate → `gNodes` grew by exactly 4 · regenerate again → **zero** actions and `gNodes` unchanged (idempotence, §5.3.3) · raise `voices` 4→6 → exactly 2 mounts, 0 unmounts, 0 remounts · lower 6→3 → exactly 3 unmounts, 0 mounts · change one `set` value → exactly 4 updates, 0 mounts, 0 unmounts, and every node index is **unchanged** (T13) · rename the emit target → 3 unmounts + 3 mounts · insert a second `emit` **above** the first in the source → the first emit's nodes keep their indices (T2). |
| `INFINITE_FIELDGRAPHRATETEST` | Each of these compiles to an **error**, and the message contains the named leaf and a `Fix:` line: `t` in a `set` value · `P` in a loop bound · `uv` anywhere · `in` anywhere · `rand()` in a `set` value · a `t`-dependent global · `reduce` from `sample` · assignment to a global (§5.5.3) · `emit("Group", i)` · `emit("Field Graph", i)` · `emit` in a loop with no key · two emits with the same name · a non-literal type name. And: a clean kernel compiles with **zero** errors and infers domain `graph`. Also assert that **all** violations in a multi-error kernel are reported in one compile. |
| `INFINITE_FIELDGRAPHUNDOTEST` | Regenerate 8 nodes → `gUndoStack.size()` grew by exactly **1** (not 8, not 16) · Cmd+Z equivalent → all 8 gone and the kernel's source is back · redo → all 8 back, ownership map resolves to 8 live nodes · undo *past* the regeneration to before the kernel existed → no orphan nodes remain and no regeneration fired (T11) · redo forward → identical graph · after `ApplyPatchData`, the ownership map's indices all resolve via `FindNodeByIndex` (T1, §4.2 remap) · 30 consecutive regenerations leave `gUndoStack.size() <= 30` and do not evict a checkpoint pushed before them (T9). |
| `INFINITE_FIELDGRAPHBLASTTEST` | **Case 1**: hand-delete a generated node → kernel reports 1 missing, no crash; regenerate → it returns; index differs; nothing else moved. **Case 2**: put 2 generated nodes in a group, force a type-change remount → both are still in the group afterwards; `emit("Group", i)` is refused. **Case 3**: copy kernel+children, paste → the pasted kernel's `uid` differs, both kernels regenerate without stealing each other's nodes, and node count doubles exactly once; copy children alone → the copies are owned by nobody and survive a regeneration of the original. **Case 4**: bind a modulation cable to a free param, force a remount → the binding is still present and points at the new index; bind to a kernel-driven param → refused; true unmount → the binding is gone and the kernel reported it. **Case 5**: cable a generated node into a hand-placed node, force a remount → the cable survives; true unmount → the cable is gone, the kernel reported the detach, and **one** undo restores both the node and the cable. |

### 7.3 Nothing in steps 1-9 regressed

This is half of step 10's exit criterion and it is not optional.

```bash
cd /Users/namansoni/infinte
bash .claude/skills/run-infinite-hygiene/driver.sh --full 2>&1 | tee /tmp/fg_hygiene.txt
# must print 0
grep -cE 'FAIL|BUG$|MISMATCH|SUSPECT|DID NOT MOVE|TONE MISSING' /tmp/fg_hygiene.txt
```

Then confirm the four new fixtures are actually registered, or §7.2 proved nothing about
CI (T22):

```bash
# must print 4
grep -cE 'FIELDGRAPH(TEST|RATETEST|UNDOTEST|BLASTTEST)' \
  .claude/skills/run-infinite-hygiene/driver.sh
```

### 7.4 The graph conformance table

`field-testing` §5's conformance table must gain a `graph` row that is generated by the
build, not hand-written:

```bash
env INFINITE_FIELDCONFORMANCE=1 INFINITE_EXITAFTER=120 \
  build/Infinite.app/Contents/MacOS/Infinite > /tmp/fg_conf.txt 2>&1
# must print 1 - the graph row is present
grep -cE '^\s*graph\s' /tmp/fg_conf.txt
# must print 0
grep -cE 'FAIL' /tmp/fg_conf.txt
```

### 7.5 The globals guarantees are untouched

```bash
cd /Users/namansoni/infinte
# must print 0 - ExprGlobals is not modified by this branch
git diff --name-only main -- src/core/ExprGlobals.h src/core/ExprGlobals.cpp | wc -l | tr -d ' '
# must print 0 - Patch, INode, Modulation and NodeFactory are not modified either
git diff --name-only main -- src/core/Patch.h src/core/Patch.cpp src/core/INode.h \
  src/core/Modulation.h src/core/Modulation.cpp src/core/NodeFactory.h src/core/NodeFactory.cpp \
  | wc -l | tr -d ' '
# must print 1 - IsValidName still exists with its exact signature
grep -c 'bool IsValidName(const std::string& name, std::string& outError);' src/core/ExprGlobals.h
```

And the list-order property, asserted at runtime by the existing globals fixture plus
one new assertion inside `INFINITE_FIELDGRAPHTEST`: define globals `a = 2`, `b = a * 3`,
in that order, and check `Values()["b"] == 6`; then define `c = d + 1` before `d = 5` and
check `c` records an "unknown identifier" error rather than reading a stale value.

### 7.6 Save / load round trip

```bash
env INFINITE_ROUNDTRIPTEST=1 INFINITE_EXITAFTER=240 \
  build/Infinite.app/Contents/MacOS/Infinite > /tmp/fg_rt.txt 2>&1
# must print 0
grep -cE 'FAIL|MISMATCH' /tmp/fg_rt.txt
```

Extend that fixture (do not write a second one) with: save a patch containing a kernel
and 8 generated nodes; reload; assert **9** nodes present, the ownership map resolves to
all 8, **no regeneration fired on load** (T11), and a regeneration immediately after
load produces **zero** actions (§5.3.3 idempotence, end to end through the file).

### 7.7 Deploy

Owner standing instruction: after building Infinite, always copy the app to the Desktop.

```bash
cp -R /Users/namansoni/infinte/build/Infinite.app ~/Desktop/Infinite.app
```

---

## 8. Out of scope for step 10

Each of these is a real, reasonable feature. None of them ships in step 10. If you find
yourself needing one to finish, you have mis-scoped something — stop and say so.

| Out of scope | Why |
|---|---|
| A kernel referencing or modifying a **hand-placed** node | Requires stable identity for hand-placed nodes, which does not exist (indices are reassigned on every undo, D9). A whole design of its own. |
| `emit`ing a `Group`, or a kernel creating groups | `IsUserSpawnable` (`src/main.cpp:398`) excludes `Group`; an emitted bare group has nothing to auto-fit to. |
| A kernel emitting another `Field Graph` kernel | T19. Metaprogramming the metaprogrammer. |
| Setting **Text** or **Color** params from a kernel | `ParamVisitor` (`src/core/INode.h:80`) supports them, but there is no string or colour *type* in Field (D7 keeps the string carve-out to literal argument positions only). |
| Arrays, lists, or any collection in the language | Not needed: the only iteration is a counted `for` with literal bounds (T5). Adding collections also reintroduces the ordinal-reordering key problem the whole of §5.3 exists to avoid. |
| Reading the current graph from inside a kernel (`nodeCount()`, `isConnected()`, …) | Makes the plan a function of the graph the plan mutates. Non-terminating in the general case, and non-deterministic in the easy case. |
| Incremental / partial regeneration below whole-kernel granularity | The diff is already the incrementality. A sub-kernel granularity needs a second identity scheme. |
| A visual "what will change" preview before regenerating | Worth doing later; needs the reconciler to be able to run against a fake host, which §4.1 already ensures. Not step 10. |
| Running a graph kernel on a background thread | `SpawnNode`/`RemoveNodeByIndex` are main-thread-only and mutate `gNodes` under the UI. |
| Changing `ExprGlobals`' evaluation, ordering, storage, UI, or `IsValidName` | §4.3, T20, T21. |
| Any new `Patch` line kind, node tag, or file-format version bump | T15, §5.6.2. |
| Templating / sharing kernels between patches, a kernel library | Needs a stable cross-patch identity story. Later. |

---

## 9. Which earlier steps must be done first

Step 10 depends on **all** of steps 1-9, and nothing depends on step 10 — consistent with
`docs/plans/field/step-09-sample-domain.md` §9's closing row. One reason per step, so a
partial ordering can be checked rather than assumed.

| Step | Required because |
|---|---|
| **01** — lexer, parser, project skeleton | Every intrinsic in §5.1 is parsed by step 01's parser, and the literal-only string carve-out (D7) is a **parser** rule, not a convention. Also fixes where the files live (`src/core/field/`, D1). |
| **02** — typed IR | §5.2's check walks IR values; `GraphPlan` is produced by interpreting IR. Without a typed IR there is nothing to prove rate-zero *about*. |
| **03** — the bytecode VM / evaluator | The graph kernel is **interpreted** at edit time. It reuses step 03's evaluator rather than adding a fourth backend (I7). |
| **04** — domain inference (the fixpoint over the lattice) | §5.2 adds one assertion **after** step 04's fixpoint. Without the fixpoint, rate-zero enforcement would need its own analysis, which is exactly the duplication I3 and I7 forbid. |
| **05** — `param` declarations | A kernel's node count is driven by `param`s (`voices` in §5.1); §5.4's trigger 2 is "a `param` the kernel reads changed". Step 05 also owns the param-identity decision that §5.3 deliberately mirrors rather than re-litigates. |
| **06** — `state` cells | Not used by graph kernels (there is no "previous edit"), but step 06 set the **name-keyed identity rule** that §5.3.1 reuses so the language has one identity story. Also confirms `src/core/field/`. |
| **07** — the `pixel` domain | §5.2 must refuse `uv`, which means the `pixel` seed must exist to be refused. A rate check that cannot see a domain cannot reject it, and §7.2's rate fixture asserts on `uv` explicitly. |
| **08** — transfer operators | D8's rule — "there is **no** transfer into `graph`" — is a statement about step 08's operator set. It can only be enforced where `reduce`/`resample`/`downsample` are defined. |
| **09** — the `sample` domain | The most valuable thing a graph kernel emits is a voice stack of audio nodes, and §5.2 must refuse `in`/`out`. Step 09 also sets the debounce-the-recompile pattern §5.4.3 reuses, and its §9 already names step 10 as downstream. |
| **all of 1-9, green** | The exit criterion (§7.3) is "nothing in steps 1-9 regressed", run as the full hygiene tier. Per §2.1, a graph kernel that emits broken kernels is worse than no graph kernel. Do not start step 10 against a red baseline (Phase 0). |
