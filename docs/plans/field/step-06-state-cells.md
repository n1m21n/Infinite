# Field build step 6 — `state` cells (a unit delay wearing an assignment's clothes)

You are implementing **build step 6 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

Line numbers are from `src/` at the commit this was written against. Where a
number has drifted, **re-grep the symbol** — the symbol is authoritative, the
number is a convenience.

**Prerequisite steps that must already be finished and merged:**

| Step | What it delivered | How to confirm |
|---|---|---|
| 1 | `Expression.cpp` split into lexer → AST → typed IR → bytecode under `src/core/field/`, `Expression::Evaluate`'s signature and its three call sites unchanged | the compiler files exist; `INFINITE_FIELDTEST` sections A–C pass |
| 2 | `rand`/`noise`/`sh` pure in `(t, seed)` | random-set baseline recorded |
| 3 | `vec2`/`vec3`/`vec4` + scalar→vector rank polymorphism | `INFINITE_FIELDTEST` D passes |
| 4 | the `element` domain, `attrib`, the SoA `ElementStore`, a Field element node | `INFINITE_FIELDELEMENTTEST` passes; `geometry-transform-sweep` passes |
| 5 | `param` declarations registered through `ParamRef`, with stable param ids | `INFINITE_FIELDPARAMTEST` passes; a binding survives editing the source |

If step 4 is missing, **stop and say so** — there is no Field node to hang a
cell on, and the element row of §5.6's cost table has nothing to measure. If
step 5 is missing, say so too: step 6 reuses step 5's "reconcile only on a
successful compile" trigger and must not build a second one.

---

## 1. Invariants — restated verbatim, they override anything you infer

1. **Clean room.** Infinite is MIT. **Never** open, read, grep, or reference
   GPL sources: Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or
   BespokeSynth (also at `/Users/namansoni/BespokeSynth` on this machine — do
   not open it). The *Kronos paper* (Norilo, "Kronos: A Declarative
   Metaprogramming Language for Digital Signal Processing", Computer Music
   Journal 39:4, 2015) is a published paper and may be cited freely; its code
   may not be read. Safe to read: Faust (LGPL), ChucK (dual MIT/GPL), Houdini
   VEX documentation, TidalCycles docs/papers.

2. **Bare names. No sigils. Ever.** An earlier draft used a VEX-style `@`
   sigil. The owner **removed it**. Plain ASCII, no special characters, in
   every example, every doc, every test fixture, **every error message**.

   | Wrong | Right |
   |---|---|
   | `@P.y += bass * 2` | `P.y += bass * 2` |
   | `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
   | `v@P` / `f@heat` | `P` / `heat` |
   | `state float @z = 0` | `state float z = 0` |

3. **Rate is inferred, never declared.** No `@rate` keyword, no `krate`
   parameter, no domain annotation on a declaration. A `state` cell's domain
   comes from **the kernel it is declared in**, and there is no syntax to say
   otherwise. If you find yourself adding one, you have taken a wrong turn.

4. **`state` is sugar for a unit delay, not "a variable that persists."**
   Every rule in this document follows from the delay reading. If a proposed
   behaviour cannot be derived from "the read is the previous invocation's
   value", it does not go in.

5. **Field is one primitive.** A kernel is a body of code run once per element
   of a domain. A `state` cell is one storage slot per element of that domain.

6. **Only build step 1 touched existing code. Steps 2–10 are additive.** If
   your diff for step 6 edits an existing node, `src/core/Patch.*`,
   `src/core/INode.h` or `src/core/Modulation.h`, something is wrong — with
   the single possible exception in §5.3's OPEN question, which needs the
   owner's explicit go-ahead before you write it.

7. **A failing compile never blanks the graph.** The last working program
   keeps running, its cells keep their values, and the error text is surfaced.
   `FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`) is the reference.

8. **Real-time safety is non-negotiable:** no heap allocation past init, no
   recursion, no unbounded loops, no strings/pointers/dynamic arrays/structs in
   the v1 language surface, every value's size known at compile time. **State
   storage is allocated at compile time, on the main thread, and never again.**

9. **The memory cost of a `state` cell is never silent.** It is surfaced in the
   node UI in the units of §5.6's table. A user who typed `state float` in a
   pixel kernel has just allocated more memory than the rest of their patch.

---

## 2. Goal

Implement `state` declarations: `state <type> <name> = <literal>` lowers to a
**unit-delay node pair** (`StateRead` / `StateWrite`) in the typed IR; a
strongly-connected-components pass refuses any dataflow cycle that does not
pass through one, with a message that lists the cycle node by node with spans;
every cell returns to its declared initial value on a transport reset, driven
from the main thread with no allocation anywhere on the reset path; cells
serialize into the patch keyed by `(name, type)` rather than by index, so
inserting a declaration does not silently reinterpret a saved patch; editing a
running kernel transplants cells that kept both their name and their type and
reinitialises everything else, but only on a **successful** compile; and the
per-domain memory cost is computed by one shared function and displayed on the
node body every frame. The `pixel` domain's ping-pong texture pair is
**specified here and built in step 7** — do not build a GL path in this step.

---

## 3. Files to read first, and why

### Skills — the authoritative contract, in this order

| File | Why |
|---|---|
| `.claude/skills/field-state/SKILL.md` | **the contract this step implements.** §1 the desugaring, §2 the legality rule, §3 the cost table, §4 the ping-pong shape (spec only here), §5 reset, §6 serialization, §7 hot reload, §9 the nine exit criteria |
| `.claude/skills/field-language/SKILL.md` | §4 the no-sigil rule, §5 reserved names per domain (a cell may not shadow one), §8 the one-paragraph `state` summary, §9 types — a `vec3` cell is **3 cells** |
| `.claude/skills/field-compiler/SKILL.md` | §5 step 4: a `state` read is a **back-edge**, so domain inference needs more than one pass; §7 the keep-last-working-program policy; §8 "a dataflow cycle with no `state` on it" is a front-end refusal |
| `.claude/skills/field-realtime/SKILL.md` | §1 rules 1, 5, 8, 11, 12; §3 where allocation hides — the reset path is exactly where a `std::vector::resize` sneaks in |
| `.claude/skills/field-integration/SKILL.md` | §5 the patch contract and the OPEN question on how cells serialize (same question as `field-state` §6 — resolve it **once**, in both places) |
| `.claude/skills/field-testing/SKILL.md` | §1 how testing works here (there is no test binary); §3 sections H and I; §6 the step-6 row |
| `.claude/skills/new-compositing-node/SKILL.md` | read **before** writing anything about pixel state, even as spec — it carries the cook-memoization traps that have already bitten here |
| `.claude/skills/node-ui-pillars/SKILL.md` | **before** you draw the cost readout. A generated readout is still a node row. |

### Real source — the code wins over any skill

| File | What matters |
|---|---|
| `src/core/Transport.h` | `Rewind()` at **:30**, `SetPlaying`/`IsPlaying`/`TogglePlay` at **:26–:28**, the header comment at **:10** ("safe to call from the main thread or the real-time audio thread"). **Read §5.3 before you assume a seek or a loop exists — neither does.** |
| `src/core/Patch.h` | the line grammar at **:16–:25**; "Names may contain spaces, so anything free-form is always last on its line" at **:55** |
| `src/core/Patch.cpp` | `EscapeLine` **:49**, `UnescapeLine` **:64**, `FloatToString` = `%.9g` **:40**, `Writer::Text` **:104–:113**, `Reader` "every setter leaves the field untouched when the key is absent" **:126–:129**, the node-body param tags `f i b c s` parsed at **:379–:387** |
| `src/core/INode.h` | `ParamVisitor` at **:80** — exactly five methods (`Float`, `Int`, `Bool`, `Text`, `Color`). **No `Vec3`, no blob, no typed record.** `VisitParams` at **:159** |
| `src/nodes/FormulaNode.cpp` | `Apply()` **:390–:407** (the keep-last-working-program shape) and `CookIfNeeded` **:409–:418** (the do-not-retry shape) |
| `src/nodes/FeedbackNodes.cpp` | `ReactionDiffusionNode` **:255–:380** — the only real ping-pong state in the tree. Read it now so step 7 inherits a design that matches it. |
| `src/core/Expression.cpp` | `mod` is C `fmod` (**:135**), `round` is `floor(x+0.5)` (**:139**), `^` is right-associative (**:291**). Step 7 needs these; step 6 only needs to not break them. |
| `.claude/skills/run-infinite-hygiene/driver.sh` | `TIER1_CHECKS` begins at **:79**, `FULL_TESTS` at **:173** |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/FieldState.h` / `.cpp` | the cell table: `{ name, type, initialValue[], domain, lane }`; compile-time allocation; `ResetAll()`; `Transplant(const FieldState& old)`; **`CostBytes(domain, elementCount, width, height, voiceCount)` — the single source of §5.6's arithmetic, read by both the allocator and the UI** |
| `src/core/field/FieldCycles.h` / `.cpp` | the SCC legality pass and its error formatting |

(Step 1 placed the compiler in `src/core/field/` with a `Field*` filename
prefix — see `docs/plans/field/step-01-expression-to-ir.md` §4. **Steps 4, 5
and 8 say `src/field/`; that is a drafting error in those files.** Put these
where step 1 actually put its files and keep one location, not two.)

### Modify — Field's own files only

| Path | Change |
|---|---|
| `src/core/field/FieldParse.cpp` | parse `state <type> <name> = <literal>` into the `DeclState` AST node that `field-compiler` §4 already lists |
| `src/core/field/FieldIR.cpp` | emit `StateRead` / `StateWrite` node pairs (§5.1); the back-edge that makes inference iterate (`field-compiler` §5 step 4) |
| `src/core/field/FieldBytecode.cpp` | a cell is a fixed slot index resolved at compile time; the per-element loop reads slot `base + i` |
| `src/nodes/FieldElementNode.cpp` (step 4's node) | own a `FieldState`; reconcile it on a successful compile; reset it on the transport epoch; draw the cost readout; extend `VisitParams` |
| `CMakeLists.txt` | add the new `.cpp` files to `COMMON_SOURCES` — core/compiler files sit near **line 208**, node files near **line 262**. (Steps 4, 5 and 8 say "~line 98" and "~line 54"; those numbers are wrong — `COMMON_SOURCES` starts at **line 206**.) |
| `.claude/skills/run-infinite-hygiene/driver.sh` | add `"FIELDSTATETEST:12"` to `FULL_TESTS` (**line 173**) |

### Must not be modified

`src/core/Patch.h`, `src/core/Patch.cpp`, `src/core/INode.h`,
`src/core/Modulation.h`, `src/core/Mesh.h`, `src/audio/ParamMailbox.h`,
`src/audio/MeterRing.h`, and every existing node.

`src/core/Transport.h` is **conditionally** modifiable — §5.3's OPEN question,
owner sign-off first, and if approved the diff is one `std::atomic<unsigned
long long>` member, one accessor, and two `++` sites. Nothing else.

---

## 5. Step-by-step procedure

### 5.0 Put the three OPEN questions to the owner before writing code

All three are marked OPEN in `field-state`. All three change the shape of the
code. **Do not resolve any of them silently.**

| # | Question | Where | Why it blocks you |
|---|---|---|---|
| 1 | How is a reset *triggered*, given that `Transport` has no seek, no loop and no notification? | §5.3 below | decides whether the diff touches `Transport.h` at all |
| 2 | Does transport **stop** reset, or is stop a pause that resumes? | `field-state` §5's OPEN, restated in §5.3 | decides whether a reverb tail survives a pause |
| 3 | What is actually **serialized** for `element` and `pixel` cells? | `field-state` §6 / `field-integration` §5 — **the same question, resolve it once in both places** | decides whether a patch file can be 11 MB |

### 5.1 The desugaring — a worked before/after, and the subtlety everyone gets wrong

**What the user writes:**

```
param float cutoff = 0.2 [0, 1]
state float z = 0
z += (in - z) * cutoff
out = z
```

**What the compiler builds:**

```
                     ┌──────────── UNIT DELAY ───────────┐
                     │                                   │
                     ▼                                   │
   [StateRead z] ──▶(-)──▶(x cutoff)──▶(+)──▶ z_1 ──┬──▶ [StateWrite z]
        ▲             ▲                  ▲          │
        in ───────────┘                  └──────────┤
                                                    └──▶ out
```

IR, written out:

```
  %0 = StateRead  z            ; the value StateWrite left last invocation
  %1 = Sub        in, %0
  %2 = Mul        %1, cutoff
  %3 = Add        %0, %2       ; this is `z` for the rest of the body
       StateWrite z, %3        ; lands for the NEXT invocation
  %4 = Copy       %3           ; `out = z` reads %3, NOT %0
```

**The subtlety, and it is the whole step:** within one body, a `state` cell
behaves like an ordinary SSA local. Its **entry definition** is `StateRead`; its
**exit definition** is what `StateWrite` stores. A read resolves to the most
recent definition *in the body*, and only a read that precedes the first write
sees the delayed value. The delay lives on the **back-edge from end-of-body to
start-of-body**, and nowhere else.

| Wrong | Right | What breaks otherwise |
|---|---|---|
| every read of `z` lowers to `StateRead` | only reads before the first write do | `out = z` returns last invocation's value — the filter is one sample late, and every textbook difference equation in the docs is wrong |
| the write is immediate, with no back-edge | the write is the exit definition, delayed to the next invocation | there is no delay at all, so `z = z * 0.5 + in` is an algebraic loop and §5.2 should have refused it |
| two writes to `z` in one body produce two delays | the **last** write is the exit definition; there is exactly **one** delay per cell per invocation | a cell written twice decays twice as fast, silently |
| a cell read but never written | still exactly one delay; `StateWrite` stores the unchanged entry value | fine, but the emitter must not elide the slot — it is still serialized and still costs memory |

Rules the parser and IR builder must enforce:

| Rule | Message must say |
|---|---|
| the initial value is a **literal**, not an expression | `field-realtime` §1 rule 5 — name the expression that was not constant |
| a cell may not shadow a reserved attribute (`t` `dt` `frame` `P` `N` `uv` `Cd` `i` `count` `in` `out` `sr` `n` `xy` `col` `res`) | which **domain** the name belongs to. Precedent already in the tree: `ExprGlobals::IsValidName` (`src/core/ExprGlobals.h:45`) already refuses `t`, `pi`, `lo`, `hi` |
| a cell may not share a name with a `param` or an `attrib` | both spans |
| two `state` declarations with the same name | duplicate declaration, both spans |
| `state vec3 v = vec3(0,0,0)` | legal — **3 cells**, three lanes, one delay each |
| `state` with no type | a type is required |

**Why the sugar exists** (put a version of this in the node's help text): the
analyzable form is a `z-1` operator, the readable form is an assignment. Field
ships the readable form and lowers to the analyzable one, so the user writes
what a textbook difference equation looks like and the compiler still gets a
graph it can check. This is the Kronos model (Norilo p.36: cycles are legal "as
long as each cycle includes at least one sample of delay") reached through
familiar syntax. Cite the paper; never claim novelty; never read the code.

### 5.2 The legality check — every cycle must contain a delay

> A cycle in the dataflow graph is legal **if and only if** it passes through
> at least one delay.

**Where it runs.** On the typed IR, **after** domain inference (which needs the
back-edge to iterate — `field-compiler` §5 step 4), **after** inlining, and
**after** constant folding, **before** any backend. That ordering is
`field-state` §2's, and each half of it matters:

- After inlining, because inlining must not dissolve a delay: the delay belongs
  to the **cycle**, not to a name, and a checker that ran pre-inlining could
  approve a cycle that inlining then flattens.
- After constant folding, because folding can legitimately break a cycle
  (`a = b * 0` folds to `a = 0`) and refusing a program that has no cycle left
  is a false positive.

**The algorithm.**

```
1.  Build a directed graph over IR nodes.
       edge u -> v   whenever v consumes u's value
       PLUS one edge  StateWrite(c) -> StateRead(c)  for every cell c,
                      tagged isDelay = true
2.  Tarjan's SCC over the whole graph.            O(V + E), one pass
3.  For each SCC with more than one node, and for each single node
    carrying a self-edge:
       if no edge INSIDE that SCC has isDelay      -> ILLEGAL
       else                                        -> legal, continue
4.  For an illegal SCC, run a DFS inside it to recover one concrete cycle
    in source order, so the message can print it.
5.  Report the first illegal SCC in source order. Stop after 5
    (`field-compiler` §7 — the driver log truncates at 1024 chars and a
    cascade loses the first, most useful message).
```

Termination and cost: Tarjan is linear, runs once per compile, on the main
thread, on a graph whose size is bounded by the source text. No recursion in
the *implementation* either — use an explicit stack (`field-realtime` §1
rule 2).

**The exact error message.** This shape is the contract; a fixture asserts it.

```
error: dataflow cycle with no delay
    a  (line 3, col 1)
 -> b  (line 4, col 1)
 -> a  (line 3, col 1)
  hint: every cycle must pass through a state cell, which is a unit delay.
        Declare one on an edge of this cycle, e.g. `state float b = 0`
```

| The message must | The message must not |
|---|---|
| name every node on the cycle, in source order | say "cyclic dependency" and stop |
| carry `(line, col)` for each, from the token spans step 1 built | report a node with no span |
| close the loop by repeating the entry node | print the SCC as an unordered set |
| contain the word `state` and a concrete, copy-pasteable declaration naming one identifier from the cycle | suggest `@state` or any sigil |

Worked pairs:

| Illegal | Legal |
|---|---|
| `a = b + 1` / `b = a * 2` | `state float b = 0` / `a = b + 1` / `b = a * 2` |
| `out = out * 0.5 + in` | `state float y = 0` / `y = y * 0.5 + in` / `out = y` (`out` is a reserved sample-domain output, not storage) |
| a helper containing a `state` that the inliner flattened onto a delay-free path | keep the delay on the cycle; the checker runs **after** inlining, so this is caught, not missed |

**Do not touch the node-level cycle ban.** Infinite's *graph* already refuses
audio and note cycles (`WouldCreateAudioCycle` / `WouldCreateNoteCycle`,
`src/main.cpp` ~2511) because the topological sort deadlocks. That is a
different rule at a different level. Cycles *inside* one Field kernel are legal
with a delay; cycles *between* audio nodes stay forbidden. Do not "unify" them.

**Forward-looking, v1 has no user functions:** when they arrive, two call sites
of one helper containing a `state` must get **distinct cells**, one per inlined
instance. Write the cell identity as `(inline path, name)` now, even though the
path is always empty in v1, so the day functions land nobody has to re-key
every saved patch.

### 5.3 Reset — and the fact that `Transport` has no seek and no loop

**Read this before writing any reset code.** `field-state` §5 says cells reset
"on **seek**, **loop**, or **transport stop**". Verified against
`src/core/Transport.h`:

| Contract event (`field-state` §5) | Real Infinite API | Exists today? |
|---|---|---|
| transport seek (scrub, jump to marker) | `Transport::Rewind()` (**:30**) — jumps to 0 and **only** to 0 | **partly.** There is no arbitrary seek anywhere in `Transport` |
| loop wrap | — | **does not exist.** There is no loop region in `Transport` |
| transport stop | `SetPlaying(false)` (**:26**) / `TogglePlay()` (**:28**) | yes — but it is a plain setter |
| — | there is **no observer, no callback list, no epoch counter** | a node cannot be told a reset happened; it can only poll |

`Rewind()` is called from 13 sites in `src/main.cpp` (`grep -n 'Rewind()'`),
all of them main-thread UI/shortcut handlers. Nothing in the tree currently
resets simulation state on a rewind — the closest existing behaviour is the
`std::max(0.0, now - mLastSeconds)` dt clamp in `src/nodes/AnalyzeNodes.cpp:1102`
and `src/nodes/MidiNodes.cpp:168`, which *tolerates* backwards time instead of
reacting to it.

> **OPEN 1 — how is a reset triggered? Ask the owner; do not pick silently.**
>
> | Option | What it means | Cost |
> |---|---|---|
> | **(a)** a **reset epoch** on `Transport`: one `std::atomic<unsigned long long> mResetEpoch`, an `unsigned long long ResetEpoch() const` accessor, incremented inside `Rewind()` and on a `SetPlaying`/`TogglePlay` transition to not-playing. Every consumer polls and compares to its own last-seen value. | ~6 lines in a shared header. One publisher, N pollers, no callback list, no allocation, a relaxed load is safe from the audio thread (`Transport.h:10` already guarantees that shape). **Touches existing code — invariant 6 — so it needs the owner's yes.** |
> | **(b)** node-local backwards-time detection: keep `mLastSeconds`, reset when `Seconds() < mLastSeconds - eps`. | zero shared-code change. **Three real failure modes:** a `Rewind()` while already at 0 fires nothing (which is exactly the gesture a user makes to reset a patch); offline render mode moves time arbitrarily via `SetOfflineVideoTime` (`Transport.h:76`); and a stop does not move time at all, so option (b) cannot implement "stop resets" no matter what OPEN 2 decides. |
> | **(c)** a per-node "reset" button and nothing automatic | **rejected** — `field-state` §5's whole point is one rule with no per-node exceptions. |
>
> Recommend (a). If the owner refuses to touch `Transport.h`, ship (b) **and
> put its three failure modes in the node's help text**, because a reset that
> silently does not happen is worse than one that never existed.

> **OPEN 2 — does stop reset, or resume?** Restated verbatim from
> `field-state` §5: the rule as written says stop resets, which means a reverb
> tail does not survive a pause — predictable but musically annoying. Options:
> **(a)** stop resets (the brief's literal reading, one rule); **(b)** stop is
> a pause and only a rewind resets; **(c)** a per-node toggle — rejected, that
> is exactly the per-node exception the rule exists to prevent. Ask the owner.

**The reset table — what each event does, per domain.**

| Event | Cells reset? |
|---|---|
| `Transport::Rewind()` (the only seek that exists) | **yes** |
| loop wrap | n/a — no loop exists; wire it the day one does |
| transport stop | **per OPEN 2** |
| transport pause, if it is ever distinct from stop | per OPEN 2 |
| a `param` changes | no |
| a modulator writes a param | no |
| the node is bypassed and un-bypassed | no |
| the node is collapsed | no |
| patch load | no — the saved values are restored (§5.4) |
| a **successful** recompile | no for transplanted cells, yes for the rest (§5.5) |
| a **failed** compile | **nothing changes at all** |
| a resolution change (pixel) / element-count change (element) | yes for the reallocated storage — there is no meaningful resample of a state field |

**Who performs the reset, on which thread, and what it is allowed to touch.**

| Domain | Where the cells live | Who zeroes them | Thread | Allocates? |
|---|---|---|---|---|
| `frame` | a fixed `float[]` on the node, sized at compile time | the node, at the top of `CookIfNeeded` when the epoch changed | main | **no** — a `std::fill` over storage that already exists |
| `element` | parallel lanes in step 4's SoA `ElementStore` | the node, in `CookIfNeeded`, **before** the per-element loop | main | **no** — the store is already sized by step 4 |
| `pixel` | the ping-pong texture pair (step 7) | an initial-value shader pass issued from `CookIfNeeded` | main — GL is main-thread-only in this codebase (`src/core/GLUtil.cpp:49`) | **no**, unless the resolution actually changed |
| `sample` | fixed register slots allocated at `PrepareToPlay` | the audio thread, at the top of `ProcessBlock`, by writing zeros into slots that already exist | audio | **never** |

> **The rule, stated hard:** a reset writes into storage that **already
> exists**. Allocation happens once, at compile time, on the main thread. A
> reset path that calls `resize()`, `reserve()`, `new`, `make_shared`,
> `glTexImage2D`, or `GLUtil::EnsureFbo` with a changed size is a bug, and on
> the audio thread it is an xrun (`field-realtime` §1 rules 1 and 8).

Sample-domain detail: the audio thread cannot take a lock and cannot be handed
a callback. It reads `Transport::Instance().ResetEpoch()` (a relaxed atomic
load) once at the top of `ProcessBlock`, compares it to its own last-seen
value, and if it differs writes zeros into its own register slots inline. That
is the entire audio-thread half, and it adds **no new cross-thread channel** —
`ParamMailbox` (main→audio) and `MeterRing` (audio→main) remain the only two.

### 5.4 Serialization — keyed by `(name, type)`, never by index

**Why not an index.** Editing a Field body inserts, deletes and reorders
declarations. Infinite has already been bitten by index-keyed persistence:
`node-ui-pillars` P7 documents that saved patches store an *integer index* for
filter mode, so re-ordering a name list silently rewrites every saved patch.
Step 5 fought the same battle for `paramIndex`. State cells must not make it a
third time.

**The real grammar**, verified against `src/core/Patch.h:16–25` and the reader
at `src/core/Patch.cpp:379–387`:

```
node <index> <category> <type name to end of line>
  pos <x> <y>
  flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>
  f <name> <value>          float
  i <name> <value>          int
  b <name> <0|1>            bool
  c <name> <r> <g> <b>      colour
  s <name> <text to end of line>
end
```

Reader facts that decide the design:

| Fact | Line | Consequence |
|---|---|---|
| only the tags `f i b c s` are accepted inside a node | `Patch.cpp:379` | **a new tag is a new patch line kind** — see the OPEN below before inventing one |
| the param **name** is read with `>>` | `Patch.cpp:381` | a cell name must contain no whitespace. Field identifiers cannot, so this is free |
| the **value** is `std::getline` to end of line, one leading space stripped | `Patch.cpp:383–386` | free-form text is fine, but it must be last on its line — it already is |
| `FloatToString` is `snprintf("%.9g")` | `Patch.cpp:40–45` | 9 significant digits round-trips an IEEE-754 `float` **exactly**. A `Float` param is a lossless carrier for a cell |
| "Every setter leaves the field untouched when the key is absent" | `Patch.cpp:126–129` | **a missing cell keeps its declared initial value for free.** Do not write code for that row of the table; the existing reader already does it |
| `Writer::Text` escapes `\` and `\n` only; any other escape is left as written | `Patch.cpp:104–113`, `:154` | multi-line source text already round-trips. **Do not invent a second scheme** |
| `ParamVisitor` has exactly five methods and none carries a type name | `src/core/INode.h:80–89` | `(name, type)` keying needs the type written *somewhere* |

**The recommended encoding — no new patch line kind:**

| What | `VisitParams` call | Patch line |
|---|---|---|
| the Field source text | `v.Text("src", source)` | `s src <escaped body>` |
| a scalar cell's value | `v.Float("st." + name, value)` | `f st.z 0.5` |
| the cell's **type tag** — this is what makes the key `(name, type)` and not just `name` | `v.Text("stt." + name, typeName)` | `s stt.z float` |
| a `vec3` cell | three `Float`s: `st.<name>.x`, `.y`, `.z` | three `f` lines |

Without the `stt.` tag, a cell changed from `float` to `vec3` between save and
load would pass a name-only match and load the old scalar into the new cell's
`.x` lane — the exact "reinterprets bits" failure `field-state` §7's wrong/right
table calls out. The tag costs one short line per cell and makes the key real.

Prefix everything (`st.`, `stt.`) so a cell can never collide with a fixed param
or with step 5's `p.<name>` values. `ParamVisitor` names are **stable keys in
the patch file** (`INode.h:76–78`) — renaming one silently drops that data from
every existing patch, so pick the prefixes now and never change them.

The load/reconcile table, which falls out of the reader's behaviour:

| Saved record | Cell after the first successful compile | Result |
|---|---|---|
| `st.z` present, `stt.z` = `float`, `z` declared `float` | match | value restored |
| `st.z` present, `stt.z` = `float`, `z` declared `vec3` | type mismatch | discarded; cell takes its initial value |
| `st.z` present, `z` no longer declared | orphan | dropped silently |
| `z` declared, nothing saved | new | declared initial value — **the existing reader already does this** |
| `stt.z` missing but `st.z` present (a patch from before the tag existed) | unknowable type | treat as a mismatch and reinitialise. A wrong tail is worse than a fresh one |

**Load ordering.** `VisitParams` replays saved values *before* the program has
necessarily compiled — the same trap step 5 §5.4 documents for params. Restore
into a name-keyed side map, and reconcile it against the declarations on the
**first successful compile**, using exactly the §5.5 transplant rules.

**Size guard, with the arithmetic.**

| Domain | One `state float` | As base64 in the patch file |
|---|---|---|
| `frame` / `sample` | 4 B → 9 characters via `%.9g` | ~9 characters |
| `element`, N = 5000 | 20,000 B | ~26,700 characters |
| `pixel`, 1080p | 8,294,400 B | ~11.1 MB of text |

> **OPEN 3 — what is actually serialized for `element` and `pixel` cells?**
> Restated verbatim from `field-state` §6 and `field-integration` §5 — **the
> same question in two places; answer it once and edit both.**
> **(a)** everything, and accept a multi-megabyte patch file;
> **(b)** frame/sample cells only, with element/pixel cells always
> reinitialised on load (predictable, small patches, and a reloaded
> reaction-diffusion starts from its seed rather than mid-pattern);
> **(c)** a per-declaration `persist` opt-in.
> The brief's stated motivation is "saving mid-reverb and reloading restores
> the tail" — a **sample**-domain case, which (b) satisfies at zero cost, using
> only the existing `f`/`s` tags and adding no new line kind. Ask the owner; if
> the answer has not arrived, ship (b), because a patch that reloads with a
> fresh simulation is recoverable and an 11 MB patch file is not.

### 5.5 Hot reload — transplant by `(name, type)`, reinitialise everything else

| Condition on a cell across the edit | Result |
|---|---|
| same name **and** same type | **transplanted** — its value carries across |
| same name, different type | reinitialised to the new declaration's initial value |
| same name, same type, **different initial value** | **transplanted.** The initial value is not part of the key; it applies at the next reset. Otherwise every keystroke inside `= 0.5` blows away a running filter |
| new name | initial value |
| name disappeared | dropped |

| Wrong | Right | What breaks |
|---|---|---|
| transplant by declaration order | transplant by `(name, type)` | inserting one line at the top shifts every cell's value onto the wrong cell |
| transplant by name only, ignoring type | check both | a `float`→`vec3` change reinterprets bits |
| reset every cell on every edit | transplant matches | a filter that resets on every keystroke is unusable for live editing, which is the entire point of a live-edited language |
| keep the old value when the type changed | reinitialise | as above |
| reconcile on a **failed** compile | reconcile only on success | a typo mid-edit destroys the user's running state — invariant 7 |
| transplant before the new program is proven to compile | compile into a local first, reconcile after | same |

**Reuse step 5's trigger.** Params and cells reconcile in the **same frame, on
the same event: a successful compile.** One function, called once. Two
reconciliation paths will drift, and the frame where they disagree is the frame
a cable points at a param whose cell was already dropped.

**The failed-compile behaviour is `FormulaNode`'s, exactly**
(`src/nodes/FormulaNode.cpp:390–407`): compile into a local, and only on
success delete the old and swap. On failure record the error, return false, and
touch **nothing** — not the program, not the cell table, not the values, not
the param bindings.

### 5.6 The per-domain memory cost table — with the arithmetic worked out

**One `state float` costs:**

| Domain | Cells | Bytes | Arithmetic | Storage |
|---|---|---|---|---|
| `frame` | 1 | **4 B** | `1 x 4` | a slot in the node |
| `sample` | 1 per voice | **4 B x voices** | 8 voices → `8 x 4` = 32 B | a register slot allocated at `PrepareToPlay` |
| `element` | 1 per element | **4 B x N** | N = 5000 → `5000 x 4` = 20,000 B = 19.5 KiB | a parallel lane in step 4's SoA store |
| `pixel` | 1 per pixel, **doubled** for ping-pong | see below | 1080p → **~8 MB per cell** | two GPU textures |

**The pixel arithmetic, written out, because this is the number people get
wrong and because the brief's "8 MB" figure is ambiguous:**

```
1920 x 1080                                   =  2,073,600 pixels

  R32F, ONE texture      2,073,600 x 4 B      =   8,294,400 B  =  7.91 MiB
  R32F, the PAIR         x 2                  =  16,588,800 B  = 15.8  MiB
  R16F, ONE texture      2,073,600 x 2 B      =   4,147,200 B  =  3.96 MiB
  R16F, the PAIR         x 2                  =   8,294,400 B  =  7.91 MiB   <- "8 MB"
  RGBA16F, ONE texture   2,073,600 x 4 x 2 B  =  16,588,800 B  = 15.8  MiB
  RGBA16F, the PAIR      x 2                  =  33,177,600 B  = 31.6  MiB
  RGBA32F, ONE texture   2,073,600 x 4 x 4 B  =  33,177,600 B  = 31.6  MiB
  RGBA32F, the PAIR      x 2                  =  66,355,200 B  = 63.3  MiB
```

Two readings land on the brief's 8 MB, and they agree on the per-cell number:

- an **R16F pair** (one cell per pair): 7.91 MiB per cell;
- an **RGBA16F pair packing 4 cells**: 31.6 MiB / 4 = **7.91 MiB per cell**.

The second is what the codebase already does — `FeedbackNode`, `TrailsNode` and
`ReactionDiffusionNode` all allocate `GL_RGBA16F` ping-pong pairs
(`src/nodes/FeedbackNodes.cpp:50`, `:151`, `:303`). **So "one `state float` in a
1080p pixel kernel is 8 MB" is correct, given RGBA16F ping-pong with up to four
cells packed per pair.** The naive reading — one RGBA32F pair per cell — is
63.3 MiB, **8x more**, and it is what a fresh session will build if nobody
writes this down. Write the number you actually ship, not the slogan.

> **OPEN 4 — what format backs pixel state?** Restated from `field-state` §3:
> **(a)** R32F, one texture per cell, exact; **(b)** RGBA32F, four cells packed,
> fewer binds, wastes up to 3 channels for a single cell; **(c)** RGBA16F,
> half the memory, enough for colour-ish accumulation but **not** for a
> long-running integrator or a feedback delay, where a 10-bit mantissa drifts
> visibly within seconds. The answer differs by use ("trails" is fine on 16F,
> "reaction-diffusion" already runs on 16F here and works, a phase accumulator
> is not). **Step 7 owns the decision; step 6 owns the arithmetic.** Ask the
> owner, and note `GLUtil::EnsureFbo` today supports only `GL_RGBA8` and
> `GL_RGBA16F` (`src/core/GLUtil.cpp:120–121`) — anything else needs a change
> to shared code, which is step 7's problem, not this step's.

**Surfacing it in the UI is a requirement, not a nicety.**

| Rule | |
|---|---|
| Where | a visible readout on the node body, in the same place every frame |
| Not | a tooltip, a log line, an "advanced" section, or a number only shown on hover |
| Shape | `state: 3 cells x 2,073,600 px x 2 (ping-pong) = 31.6 MiB` — cells, the multiplier, and the total, so the user can see *which* factor is the problem |
| Source of the number | **one** function, `FieldState::CostBytes(...)`, read by both the allocator and the readout. Two copies drift, and then the number on the node is a lie (step 8 §5.6 makes the same rule for the transfer cost table) |
| Updates | when the cell count changes **and** when the multiplier changes (resolution, element count, voice count) |
| Layout | read `.claude/skills/node-ui-pillars/SKILL.md` first. A generated readout is still a node row |

### 5.7 Real-time safety — where the allocation is allowed to be

| Moment | Thread | Allocation allowed? |
|---|---|---|
| compile (parse → IR → cycle check → cell table sized) | main | **yes** — this is the only place |
| a resolution / element-count / voice-count change | main | yes, as a reallocation, and it clears |
| `CookIfNeeded` steady state | main | **no** (`field-realtime` §1 rule 8 — the budget is < 5 µs) |
| reset on the transport epoch | main or audio | **no** — fill existing storage |
| `ProcessBlock` | audio | **no** — nothing, ever (`field-realtime` §1 rule 7) |
| a failed compile | main | **no** — nothing is touched at all |

Grep your own diff for `field-realtime` §3's shapes: `std::string` below a
`ProcessBlock`, a `std::vector` returned by value from a per-cook function,
`std::function`, a `std::map` lookup that inserts, `make_shared`, `snprintf`
into a `std::string`. The cost readout is the sneaky one — formatting
`"31.6 MiB"` every frame allocates. Format into a `char[64]` member, or format
only when the number changes.

### 5.8 Fixtures

`INFINITE_FIELDSTATETEST`, in two sections, following `field-testing` §1:

- **Early-exit, headless** — gated before `glfwInit()`, in the shape of
  `INFINITE_DSPTEST` (`src/main.cpp:37619`). Everything that is pure
  computation: desugaring, the cycle checker, transplant, the cost arithmetic.
- **In-frame** — in the shape of `INFINITE_ROUNDTRIPTEST`
  (`src/main.cpp:43318`, fires at `frameId == 4`). Anything needing a real
  spawned node: transport reset, save/load, undo.

One verdict line per section, ending `OK` or containing `FAIL` with the case
name and both values.

| Section | Case | Assert |
|---|---|---|
| desugar | `z += (in - z) * c; out = z` | `out` equals the **post-write** value, not the delayed one — feed a unit step and match the analytic one-pole response |
| desugar | a cell read before any write | reads the previous invocation's value |
| desugar | a cell written twice in one body | exactly **one** delay; the second write is the exit definition |
| desugar | `state vec3 v` | **3** cells reported, and the three lanes do not bleed into each other |
| cycles | `a = b + 1` / `b = a * 2` | refused; the message lists `a`, `b`, `a` **with spans**, contains `state`, and contains no `@` |
| cycles | the same program with a `state` on one edge | compiles and runs |
| cycles | `out = out * 0.5 + in` | refused, naming `out` as a reserved output |
| cycles | a cycle only visible after inlining | refused — proves the pass runs post-inline |
| cycles | a cycle that constant folding removes | **accepted** — proves the pass runs post-fold |
| reset | rewind | every cell back to its declared initial value, **checked by the fixture, not by eye** |
| reset | stop | per OPEN 2, whichever the owner chose |
| reset | a param change | cells **unchanged** |
| reset | bypass / un-bypass | cells **unchanged** |
| reset | allocation counter across 100 resets | **zero** allocations |
| serialize | save → load with a mid-decay value | restored for the domains OPEN 3 says persist; reinitialised for the rest |
| serialize | insert a `state` line **above** an existing one, save, load | every value lands on its own cell — this is the whole reason for `(name, type)` |
| serialize | `stt.` type tag absent | cell reinitialises rather than reinterpreting |
| transplant | same name + same type | value kept |
| transplant | same name, type changed | reset |
| transplant | same name, **initial value** changed | value kept |
| transplant | renamed | old dropped, new initial |
| transplant | compile fails mid-edit | program, cells, values and bindings **all unchanged** — and no recompile on the next frame (`FormulaNode.cpp:415`) |
| cost | frame / element / pixel / sample | matches §5.6's arithmetic exactly, from `CostBytes` — and the UI string is generated from the same call |
| determinism | compile the same source twice | byte-identical cell table and byte-identical bytecode (`field-testing` §3 section I) |

**A test that cannot fail is not a test.** For every row: introduce the bug
deliberately, watch the fixture print `FAIL`, then fix it.

---

## 6. Traps, and the bug each one prevents

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Emitting `@` anywhere — code, docs, error messages, fixtures, the cost readout | the sigil was removed by owner decision, permanently. A grep hit means the surface drifted from the contract |
| 2 | Adding syntax that lets a user declare a cell's rate | rate is inferred; a cell's domain is the kernel's. Any override evaporates every optimisation guarantee |
| 3 | Lowering **every** read of a cell to `StateRead` | `out = z` after `z += ...` returns last invocation's value. The filter is one sample late and every textbook example in the docs becomes wrong |
| 4 | Making the write immediate instead of delayed | there is then no delay, `z = z*0.5 + in` is an algebraic loop, and §5.2 was supposed to refuse it |
| 5 | Emitting one delay per write instead of one per cell | a cell written twice decays twice as fast, silently |
| 6 | Running the cycle check **before** inlining | a helper's delay dissolves on inline and an illegal program compiles |
| 7 | Running it **before** constant folding | `a = b * 0` is refused as a cycle when folding would have removed it — a false positive on legal code |
| 8 | A one-line "cyclic dependency" message | the user wrote three lines and cannot find the cycle. The message must list it node by node with spans |
| 9 | Recursive Tarjan | `field-realtime` §1 rule 2 — a deep IR blows the stack, and unbounded stack is not analyzable |
| 10 | Assuming `Transport` has a seek or a loop | **it has neither** (`Transport.h:26–41`). A reset wired to a seek callback is wired to nothing |
| 11 | Detecting a rewind by watching `Seconds()` go backwards | a rewind at t=0 moves nothing, offline mode moves time arbitrarily (`Transport.h:76`), and a stop moves time not at all. §5.3 OPEN 1(b)'s three failure modes |
| 12 | Allocating on the reset path | `resize`/`new`/`EnsureFbo` in a reset is an xrun on the audio thread and a frame spike on the main one (`field-realtime` §1 rules 1 and 8) |
| 13 | Resetting pixel state from wherever the rewind happened | GL is main-thread-only here (`GLUtil.cpp:49`). The clear pass is issued from `CookIfNeeded` |
| 14 | Adding a callback list or a new atomic so nodes can be *told* about a reset | `ParamMailbox` and `MeterRing` are the only two cross-thread channels (`field-realtime` §1 rule 9). One polled epoch counter is not a channel; a callback list is |
| 15 | Serializing cells by declaration index | the exact failure `node-ui-pillars` P7 documents for saved filter-mode indices: reordering a list silently rewrites every saved patch |
| 16 | Keying on name only, without the type | a `float`→`vec3` change reinterprets bits and the tail comes back as garbage |
| 17 | Inventing a new patch line kind for cells | only `f i b c s` are parsed inside a node (`Patch.cpp:379`). `Float` + `Text` through `VisitParams` already gets you undo, redo, copy/paste and duplication **for free** — needing a per-node entry in any of those paths means the node is built wrong |
| 18 | Putting a free-text field anywhere but last on its line | `Patch.cpp` reads token-separated lines with `>>`; a space silently truncates the record (`Patch.h:55`) |
| 19 | Reconciling cells on a **failed** compile | a typo mid-edit destroys the user's running state — invariant 7 |
| 20 | Resetting a cell because its **initial value** literal changed | every keystroke inside `= 0.5` kills a running filter. The initial value is not part of the key |
| 21 | Two reconciliation paths, one for params and one for cells | they drift; the frame they disagree is the frame a cable points at a dropped cell |
| 22 | Two copies of the cost arithmetic (allocator + UI) | they drift, and then the number on the node is a lie. One `CostBytes`, read twice |
| 23 | Quoting "8 MB" without saying which format it assumes | one RGBA32F pair per cell is **63.3 MiB**, 8x the slogan. §5.6 has the arithmetic; ship the number you allocate |
| 24 | Formatting the cost string every frame | `snprintf` into a `std::string` allocates on the render path (`field-realtime` §3). Format into a `char[]`, or only when it changes |
| 25 | Touching `WouldCreateAudioCycle` / `WouldCreateNoteCycle` (`src/main.cpp` ~2511) | a different rule at a different level. Cycles inside one kernel are legal with a delay; cycles between audio nodes stay forbidden |

---

## 7. Machine-checkable exit criterion

Run all of these. Every one must pass.

```bash
cd /Users/namansoni/infinte

# 1. builds clean
cmake --build build -j"$(sysctl -n hw.ncpu)"

# 2. the state fixture (headless + in-frame sections)
INFINITE_FIELDSTATETEST=1 INFINITE_EXITAFTER=12 \
  ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/fieldstate.log
grep -c FAIL /tmp/fieldstate.log                   # must print 0
grep -c BUG  /tmp/fieldstate.log                   # must print 0
grep -c 'dataflow cycle with no delay' /tmp/fieldstate.log
#   must print >= 1 — proves the illegal-cycle case actually ran and was refused

# 3. the cycle message carries spans and names `state`, and no message has a sigil
grep -A4 'dataflow cycle with no delay' /tmp/fieldstate.log | grep -c 'line [0-9]*, col [0-9]*'
#   must print >= 2 (at least two nodes on the cycle, each with a span)
grep -A4 'dataflow cycle with no delay' /tmp/fieldstate.log | grep -c 'state'
#   must print >= 1
grep -c '@[A-Za-z]' /tmp/fieldstate.log            # must print 0

# 4. no sigils, no declared rates, anywhere in the source or the doc
grep -rn '@[A-Za-z]' src/core/field/ docs/plans/field/step-06-state-cells.md \
  | grep -v '@brief\|@param\|email' || echo "no sigils OK"
grep -rn 'krate\|@rate' src/core/field/ && echo "RATE DECLARED - FAIL" || echo "no rate syntax OK"

# 5. allocation appears ONLY in the compile-time allocator
git diff main -- src/core/field src/nodes \
  | grep -nE '^\+.*(resize\(|reserve\(|push_back\(|\bnew \b|make_shared|glTexImage2D|EnsureFbo)'
#   Every hit must sit inside FieldState::Allocate() or the explicit
#   resolution/element-count-change path. A hit inside ResetAll(), inside
#   CookIfNeeded()'s steady path, or anywhere reachable from ProcessBlock is a FAIL.
#   Name each remaining hit in the commit message.

# 6. no new cross-thread channel
git diff --unified=0 main -- src/core/field src/nodes src/audio src/core/Transport.h \
  | grep -E '^\+.*(std::atomic|std::mutex|std::condition_variable|RingBuffer|std::thread)'
#   Expected: NOTHING, unless the owner approved §5.3 OPEN 1 option (a), in which
#   case exactly one `std::atomic<unsigned long long>` in src/core/Transport.h
#   and nothing else. Any other hit is a FAIL.

# 7. shared code is untouched
git diff main --stat -- src/core/Patch.h src/core/Patch.cpp src/core/INode.h \
                        src/core/Modulation.h src/core/Mesh.h src/audio/ParamMailbox.h
#   expected: no output

# 8. no new patch line kind
git diff main -- src/core/Patch.cpp | grep -E '^\+.*tag =='
#   expected: no output — cells ride the existing `f` and `s` tags

# 9. save/load, undo and copy/paste still round-trip
.claude/skills/data-accuracy-sweep/driver.sh
.claude/skills/run-infinite-hygiene/driver.sh

# 10. project convention
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

`PATCHTEST`, `ROUNDTRIPTEST`, `UNDOTEST`, `UNDOPERFTEST` and `BYPASSTEST` are
the hygiene entries that directly guard step 6; all must pass, and the
`AUDIOPARAMSWEEPTEST` xfail baseline must not grow.

Step 6 is done when all nine of `field-state` §9's criteria hold:

1. It builds clean.
2. A delay-free cycle is refused, with the cycle listed node by node with spans.
3. A cycle through a `state` compiles and runs.
4. Rewind (and stop, per OPEN 2) returns every cell to its declared initial
   value — **verified by a fixture, not by eye**.
5. Save → load restores the cells OPEN 3 says persist and reinitialises the rest.
6. A hot reload that keeps a cell's name **and** type keeps its value; changing
   either resets it; a failing hot reload changes nothing at all.
7. The node UI shows cell count and total bytes, generated by `CostBytes`, and
   the pixel figure matches §5.6's arithmetic.
8. Nothing on any reset path allocates (§7 check 5).
9. `/run-infinite-hygiene` passes.

---

## 8. Out of scope for this step

| Not in step 6 | Where it lands |
|---|---|
| Any GL code, any texture, any shader — including the pixel ping-pong pair | **step 7.** Step 6 specifies its semantics and its cost; step 7 allocates it |
| GLSL transpilation of `StateRead` / `StateWrite` | step 7 |
| `reduce`, `map`, `resample`, `downsample` — including `downsample`'s hold cell, which **is** a state cell | step 8 |
| The `sample` domain's register machine and anything on the audio thread beyond the epoch poll specified in §5.3 | step 9 |
| The `graph` domain | step 10 |
| Adding a `Vec3` method to `ParamVisitor` | never in this step — five methods, and `INode.h:80` is on the must-not-modify list |
| Changing `Expression::Evaluate`'s signature or its three call sites (`src/main.cpp:37507`, `src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179`) | never |
| Converting `Mesh::vertices` to SoA | step 4's owner-approved open question, not reopened here |
| Touching the node-level audio/note cycle ban | never |
| Resolving any question marked **OPEN** without the owner | never. Four of them (§5.0 plus §5.6's format question) go to the owner, not to a test |

If you find a genuine bug in existing code while in here, **report it rather
than fixing it inline.**

---

## 9. Which earlier steps must be finished first

| Step | Why step 6 needs it | Hard or soft |
|---|---|---|
| **1** — lexer / AST / typed IR / bytecode | there is no IR to attach `StateRead`/`StateWrite` to, no dataflow graph to run SCC over, and **no spans**, and this step's error messages are mostly spans | **hard** |
| **2** — pure `rand`/`noise`/`sh` | not required. `state` is orthogonal to randomness | not required |
| **3** — `vec2/3/4` + rank polymorphism | `state vec3 vel = vec3(0,0,0)` is the canonical element example, and "a `vec3` is 3 cells" is a row of the cost table | **hard** |
| **4** — the `element` domain, `attrib`, the SoA `ElementStore` | element cells are lanes in that store, and step 4's `FieldElementNode` is the only Field node that exists to hang cells on | **hard in practice** |
| **5** — `param` → `ParamRef`, stable param ids | step 6 reuses step 5's "reconcile only on a successful compile" trigger and its `p.<name>` patch-key convention. Building a second reconciliation path is trap 21 | **hard by ordering** |

Steps 7, 8, 9 and 10 depend on **this** step, not the other way round: step 7's
ping-pong pair is a pixel-domain `state` cell, step 8's `downsample` hold is a
state cell, and step 9's sample-domain filters are unwritable without one.
