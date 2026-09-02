---
name: field-state
description: Field's `state` cells — the delay-sugar semantics, the "every dataflow cycle must contain at least one delay" legality rule, reset on seek/loop/stop, serialization into the patch by (name, type), hot-reload transplant rules when a kernel body is edited while running, the per-domain memory cost table (1 float per cell in frame/sample, per element in element, per pixel in pixel — an 8 MB ping-pong texture pair at 1080p), and how pixel-domain state becomes a ping-pong texture pair. Use whenever writing, reviewing or lowering a `state` declaration, when a Field kernel needs memory between invocations, when a filter/feedback/integrator/smoother is involved, when a patch reloads with the wrong tail or a reset that should not have happened, or when a pixel kernel's memory cost needs to be surfaced in the UI.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

Read [`field-language`](../field-language/SKILL.md) §8 first for the syntax and
[`field-compiler`](../field-compiler/SKILL.md) §5 for how a `state` back-edge
forces domain inference to iterate. Costs interact with
[`field-realtime`](../field-realtime/SKILL.md);
[`field-integration`](../field-integration/SKILL.md) covers where the saved
bytes actually go in the patch file.

`state` is build step 6. Nothing here is implemented yet.

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable.
2. **`state` is sugar for a unit delay.** It is not "a variable that persists".
   Every rule below follows from the delay reading.
3. **The memory cost of a `state` cell is never silent.** It is surfaced in the
   node UI, in the units of §3's table. A `state float` a user typed without
   understanding is an 8 MB allocation in the pixel domain.

---

## 1. Syntax the user writes, semantics the compiler implements

What the user writes — readable and familiar:

```
state float z = 0
z += (in - z) * cutoff
out = z
```

What the compiler builds:

```
        in ──▶(-)──▶(×cutoff)──▶(+)──▶ z ──▶ out
               ▲                  ▲     │
               └──────────────────┴─────┘
                        through a UNIT DELAY
```

The read of `z` on the right-hand side is the value from the **previous
invocation of the kernel**; the write lands for the next one. That is the whole
semantics.

**Why the sugar exists.** The analyzable form is a `z-1` operator; the readable
form is an assignment. Field ships the readable form and lowers to the
analyzable one, so the user writes what a textbook difference equation looks
like and the compiler still gets a graph it can check.

## 2. The legality rule — every cycle must contain a delay

> A cycle in the dataflow graph is legal **if and only if** it passes through
> at least one delay.

This is the Kronos model, reached through familiar syntax rather than a `z-1`
operator (Norilo p.36: "cycles in the signal flow, as long as each cycle
includes at least one sample of delay").

| Wrong | Right | Why |
|---|---|---|
| `a = b + 1` / `b = a * 2` | introduce a `state` on one edge | a delay-free cycle has no defined value |
| `out = out * 0.5 + in` | `state float y = 0` / `y = y * 0.5 + in` / `out = y` | `out` is a reserved sample-domain output, not storage |
| `state float z = 0` / `z = z` in a delay-free helper the compiler inlined | keep the delay on the *cycle*, not on a name | inlining must not dissolve the delay — the checker runs on the IR **after** inlining |

**Where the check runs.** On the typed IR, after inlining and after constant
folding, before any backend. Report the cycle **node by node with spans** —
"`a` (line 3) → `b` (line 4) → `a`" — not "cyclic dependency". A user who
wrote three lines cannot find a cycle from a one-line message.

**Relationship to the audio graph's existing cycle ban.** Infinite's *node*
graph already refuses audio and note cycles (`WouldCreateAudioCycle` /
`WouldCreateNoteCycle`, `src/main.cpp` ~2511) because the topological sort
deadlocks. That is a different rule at a different level and Field does not
change it: cycles *inside* one Field kernel are legal with a delay; cycles
*between* audio nodes remain forbidden. Do not "fix" the node-level check.

## 3. The per-domain memory cost table

**One `state float` costs:**

| Domain | Cells | At typical size | Storage |
|---|---|---|---|
| `frame` | 1 float | 4 B | a slot in the node |
| `sample` | 1 float per cell **per voice** | 4 B × voices | a register slot allocated at `PrepareToPlay` |
| `element` | 1 float **per element** | 5 000 elements → 20 KB | a parallel array alongside the attribute store |
| `pixel` | 1 float **per pixel**, doubled for ping-pong | 1920×1080 → **8 MB** as an RGBA32F pair | two GPU textures |

Worked pixel arithmetic, because this is the number people get wrong:

```
1920 × 1080 pixels          = 2,073,600
× 4 channels (RGBA32F)      = 8,294,400 floats
× 4 bytes                   = 33.2 MB per texture
```

A single `state float` does not need four channels — but a texture is allocated
per-format, so the practical unit is: **one RGBA32F ping-pong pair = ~66 MB at
1080p, and packs up to four `state float` cells.** The design brief's "8 MB
ping-pong texture pair" figure corresponds to a single-channel R32F pair
(2,073,600 × 4 B × 2 ≈ 16.6 MB) or an R16F pair (≈8.3 MB).

> **OPEN — what format backs pixel state?** **(a)** R32F, one texture per cell,
> exact; **(b)** RGBA32F, four cells packed per texture, fewer binds and fewer
> allocations, but wastes up to 3 channels for a single cell; **(c)** RGBA16F,
> half the memory, and enough precision for colour-ish accumulation but **not**
> for a long-running integrator or a feedback delay, where 16-bit mantissa
> drift is audible/visible within seconds. The brief's 8 MB figure implies
> 16-bit. Ask the owner; the answer differs for "trails" (16F is fine) and
> "reaction-diffusion" (needs 32F).

**The cost must be surfaced in the UI.** Not a tooltip, not a log line — a
visible readout on the node showing cell count × per-element cost = total, in
the same place every frame. A user who types `state float` in a pixel kernel
has just allocated more memory than most of their patch.

## 4. Pixel-domain state is a ping-pong texture pair

A fragment shader cannot read the texture it is writing. So a pixel `state`
cell becomes two textures swapped each frame:

```
   frame N          read ── texA ──▶ [kernel] ──▶ write ── texB
   frame N+1        read ── texB ──▶ [kernel] ──▶ write ── texA
```

| Concern | Rule |
|---|---|
| Declaration | one sampler uniform per cell (or per packed group — §3's open question) |
| Write | the kernel's `fragColor` write goes to the *other* texture of the pair |
| Swap | once per cook, after the pass, never mid-pass |
| Resize | on a resolution change **the pair is reallocated and both textures are cleared to the declared initial value** — there is no meaningful resample of a state field, and silently stretching it produces artifacts users read as a bug |
| Reset | clear both textures to the initial value (§5) |
| Precision | see §3's open question |

The pattern already exists in this codebase — Feedback, Trails and Reaction
Diffusion are frame-persistent image nodes, and
`.claude/skills/new-compositing-node/SKILL.md` covers ping-pong/persistent-state
cooking for that node class. **Read it before implementing pixel state**; it
carries the cook-memoization traps that have already bitten here (a persistent
node cooked twice in one frame double-steps; a node cooked zero times freezes).

## 5. Reset — one rule, no per-node exceptions

> On **seek**, **loop**, or **transport stop**, every state cell returns to its
> declared initial value.

| Event | Cells reset? |
|---|---|
| transport seek (scrub, jump to marker) | **yes** |
| loop wrap | **yes** |
| transport stop | **yes** |
| transport pause (if distinct from stop) | see open question |
| a `param` changes | no |
| a modulator writes a param | no |
| the node is bypassed and un-bypassed | no |
| patch load | no — the saved values are restored (§6) |

One rule, no exceptions, because the alternative is a per-node reset policy and
then nobody can predict what a scrub does. The transport is
`Transport::Instance()` (`src/core/Transport.h`); a Field node observes it the
same way every other time-dependent node does.

> **OPEN — does a stop-then-play reset, or resume?** The rule above says stop
> resets, which means a reverb tail does not survive a pause. That is
> predictable but musically annoying. Options: **(a)** stop resets (the brief's
> literal reading, one rule); **(b)** stop is a pause and only an explicit seek
> or loop resets; **(c)** a per-node `reset on stop` toggle — rejected, that is
> exactly the per-node exception the rule exists to prevent. Ask the owner
> before implementing (a) or (b).

## 6. Serialization — saved by `(name, type)`

State cells are written into the patch keyed by `(name, type)`, so saving
mid-reverb and reloading restores the tail.

| Rule | |
|---|---|
| Key | `(name, type)` — not declaration order, not an index |
| Restored on load | yes, before the first cook |
| Missing on load | the cell takes its declared initial value |
| Present but wrong type | discarded; the cell takes its initial value |
| Present but the cell no longer exists | dropped silently |

**Order-independence is the point.** Infinite has already been bitten by
index-keyed persistence: `node-ui-pillars` P7 documents that saved patches store
an *integer index* for filter mode, so re-ordering a name list silently rewrites
every saved patch. State cells must not repeat that — name-and-type keying makes
adding, removing or reordering a declaration safe.

**Size guard.** A `frame`- or `sample`-domain cell is a handful of floats and
belongs in the patch. An `element`-domain cell at N = 5000 is 20 KB, and a
`pixel`-domain cell is megabytes.

> **OPEN — what is actually serialized for element and pixel state?**
> **(a)** everything, and accept a multi-megabyte patch file;
> **(b)** frame/sample cells only, with element/pixel cells always reinitialised
> on load (predictable, small patches, and a reloaded reaction-diffusion starts
> from its seed rather than mid-pattern);
> **(c)** a per-declaration `persist` opt-in.
> The brief's stated motivation is "saving mid-reverb and reloading restores the
> tail" — a *sample*-domain case, which (b) satisfies. Ask the owner.

Patch format context: `src/core/Patch.h` documents the line grammar
(`mod …`, `expr <dstIndex> <dstParam> <text>`, `glob <name> <text>`). A state
record needs its own line kind; see
[`field-integration`](../field-integration/SKILL.md) §5.

## 7. Hot reload — transplant by name and type, zero everything else

When a kernel body is edited while running:

| Condition on a cell | Result |
|---|---|
| same name **and** same type as before the edit | **transplanted** — its value carries across |
| same name, different type | zeroed to the new declaration's initial value |
| new name | initial value |
| name disappeared | dropped |

| Wrong | Right |
|---|---|
| transplant by declaration order | transplant by `(name, type)` |
| transplant by name only, ignoring type | a `float`→`vec3` rename reinterprets bits |
| reset every cell on every edit | a filter that resets on every keystroke is unusable for live editing |
| keep the old value when the type changed | see above |

**Reuse the existing "don't blank on a typo" policy.** A hot reload whose
compile fails must leave the *previous* program and *its* cells running,
untouched — the same behaviour as `FormulaNode::Apply()`
(`src/nodes/FormulaNode.cpp:390`), which keeps the last working program on a
failed compile. Transplant happens only on a **successful** compile. See
[`field-compiler`](../field-compiler/SKILL.md) §7.

## 8. Worked examples

**Sample — one-pole lowpass.** 1 cell, 4 bytes.

```
param float cutoff = 0.2 [0, 1]
state float z = 0
z += (in - z) * cutoff
out = z
```

**Frame — a smoothed value.** 1 cell, 4 bytes. Resets on seek, which is what
you want: scrubbing should not carry a stale ramp.

```
param float target = 0.0 [0, 1]
state float smooth = 0
smooth += (target - smooth) * 0.1
```

**Element — per-point velocity.** 3 cells (a `vec3` is 3 cells), 5000 elements
→ 60 KB. Legal, and worth showing in the UI.

```
state vec3 vel = vec3(0, 0, 0)
vel.y -= 9.8 * dt
P += vel * dt
```

Note `dt` is frame-domain and `P`/`vel` are element-domain: `9.8 * dt` is
hoisted out of the per-element loop by rate inference
([`field-compiler`](../field-compiler/SKILL.md) §5).

**Pixel — trails.** 1 cell **per pixel**. At 1080p this is the 8 MB line item
that must appear on the node.

```
state float prev = 0
prev = max(prev * 0.95, col.r)
col = vec3(prev, col.g, col.b)
```

**Illegal — a delay-free cycle.**

```
a = b + 1        # wrong
b = a * 2
```

```
state float b = 0    # right
a = b + 1
b = a * 2
```

## 9. Exit criterion for a `state` change

1. It builds clean.
2. A delay-free cycle is refused, with the cycle listed node by node with spans.
3. A cycle through a `state` compiles and runs.
4. Seek, loop and stop each return every cell to its declared initial value —
   verified by a fixture, not by eye.
5. Save → load restores the cells that §6 says are persisted, and reinitialises
   the ones it says are not.
6. A hot reload that keeps a cell's name and type keeps its value; changing
   either resets it; a failing hot reload changes nothing at all.
7. The node UI shows cell count and total bytes, and the pixel-domain figure
   matches §3's arithmetic.
8. A pixel-domain resolution change reallocates and clears the pair — no
   stretched-state artifacts.
9. `/run-infinite-hygiene` passes, and the save/load round trip
   (`INFINITE_ROUNDTRIPTEST`) covers a node carrying state cells.
