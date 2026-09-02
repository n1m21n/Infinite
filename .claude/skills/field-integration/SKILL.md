---
name: field-integration
description: How a Field node joins Infinite without breaking what already works — the INode contract, IAudioSource and IGeometrySource mix-ins, ParamRef registration through src/core/Modulation.h, ParamMailbox as the only main-to-audio path, GLUtil::CompileProgram as the pixel backend's target, the patch save/load line grammar in src/core/Patch.h, undo/redo, and the four incompatible mini-languages Field absorbs. Use when wiring a Field node into main.cpp, when a Field `param` must become a modulatable knob or reach the audio thread, when Field output must render or produce geometry, when a Field program must survive save/load/undo/copy-paste, or when reviewing any Field diff that touches existing Infinite source.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

This is the skill that keeps Field from breaking what already exists. Read
[`field-language`](../field-language/SKILL.md) and
[`field-compiler`](../field-compiler/SKILL.md) first;
[`field-realtime`](../field-realtime/SKILL.md) is the diff checklist that
enforces the thread rules stated here.

A Field node is still a node. **Read
`.claude/skills/new-source-node/SKILL.md` (or `new-audio-node`,
`new-geometry-node`, `new-effect-node`, as applicable) before writing one** —
Field does not exempt a node from any of the existing procedures, and this
skill only covers what is *additional*.

---

## 0. Invariants

1. **Clean room.** Never read Kronos, Cmajor, SuperCollider or BespokeSynth
   source. The Kronos *paper* (Norilo, CMJ 39:4, 2015) is citable.
2. **Only build step 1 touches existing code.** Steps 2–10 are additive: new
   files, new node types. If a diff for step 4 or later edits an existing node,
   something is wrong.
3. **Two cross-thread channels, and no third.** `ParamMailbox` (main → audio)
   and `MeterRing` (audio → main). Field adds neither a new atomic nor a new
   queue.
4. **A failing Field program never blanks the graph.** The last working program
   keeps running and the error text is surfaced — the existing behaviour of
   both `FormulaNode::Apply()` and the expression apply loop.

---

## 1. What Field absorbs — verified against the code

The brief calls these "four incompatible mini-languages". **There are five**,
and a sixth consumer with its own variable set. All line counts below were
checked.

| File | Lines | What it is | Relationship to Field |
|---|---|---|---|
| `src/core/Expression.h` / `.cpp` | 48 / 428 | recursive-descent **double** evaluator for inline `=` parameter expressions, re-parsed on every evaluation. Binds `t`, `lo`, `hi`, siblings, globals | **the seed.** Step 1 restructures this behind an identical API |
| `src/core/ExprGlobals.h` | 69 | patch-wide named expressions, evaluated in list order so cycles are structurally impossible | absorbed as `graph`-domain constants |
| `src/nodes/FormulaNode.h` / `.cpp` | 67 / — | user-typed GLSL body wrapped in a preamble and compiled at runtime; a failed compile keeps the last working program | the model for the `pixel` domain's error UX |
| `src/nodes/EquationNode.h` | 142 | Desmos-style `y = f(x,a,b,c,d)` → Fourier mip pyramid wavetables → 8-voice poly synth | the model for `sample`-domain compilation |
| **`src/audio/dsp/EquationDsp.h`** | — | **a fifth mini-language the brief does not list**: its own `TokenType`, `Token`, `AstType`, `VarId`, `AstNode` with a `double Evaluate(x,a,b,c,d,t)` walker, and a full `Parser` class with `ParseOr`/`ParseAnd`/`ParseComparison`/… | this is where `EquationNode`'s language actually lives; Field replaces **this**, not the node |
| `src/nodes/AnalyzeNodes.cpp:179` | — | a **third** `Expression::Evaluate` consumer, binding a completely different variable set: `lum`, `sat`, `s`, `hue`, `h`, `max`, `min`, `delta`, `motion`, `u`, `v` | a de-facto extra domain; see §7's caching trap |

`Expression::Evaluate`'s three call sites — `src/main.cpp:37506`,
`src/core/ExprGlobals.cpp:72`, `src/nodes/AnalyzeNodes.cpp:179` — **must not
change in step 1.**

## 2. `INode` — the contract a Field node satisfies

`src/core/INode.h` (229 lines). The pure virtuals every node must answer:

| Member | Line | Field node's answer |
|---|---|---|
| `unsigned int GetOutputTexture()` | 117 | the pixel FBO's texture; `0` for a non-image node |
| `int GetOutputWidth() / GetOutputHeight() const` | 118–119 | the pixel kernel's resolution; `0` when there is none |
| `void CookIfNeeded(int frameId)` | 123 | run the frame kernel, dispatch the element/pixel kernel — **never compile here** |

The optional virtuals that matter to Field:

| Member | Line | Note |
|---|---|---|
| `TextureRevision()` | 132 | must change when the pixel kernel produced new pixels, and **must not** when it did not; a node that always returns `NextTextureRevision()` forces everything downstream to re-render every frame |
| `VisitParams(ParamVisitor&)` | 159 | the save/load contract — see §5 |
| `InputLabel(int slot)` | 154 | without it a pin is a bare unlabelled dot |
| `ModulatorOutput(int)` | 143 | if a Field node emits a control value |
| `BypassSource()` | 151 | what a bypassed Field node passes through |
| `GeometryInputSlot` / `ModulatorInputSlot` / `AudioInputSlot` / `NoteInputSlot` | 166–177 | one shared slot index space — see §6 |
| `RequiresAudioProcessing()` | 200 | true for a node with a sample-domain kernel |

`ParamVisitor` (`src/core/INode.h:80`) offers exactly five methods: `Float`,
`Int`, `Bool`, `Text`, `Color`. **There is no `Vec3` and no blob.** A Field
node's source text is a `Text` param; its `param` values are `Float`s; its
`state` cells need a representation that fits — see §5's open question.

## 3. `param` → `ParamRef` — how a declaration becomes a knob

`ParamRef` (`src/core/Modulation.h:29`):

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

Mapping from `param float amount = 0.5 [0, 2]`:

| Declaration piece | `ParamRef` field |
|---|---|
| `amount` | `name` — also the knob caption |
| `[0, 2]` | `minValue`, `maxValue` |
| `float` | `step = 0`, `isEnum = false`, `isBool = false` |
| `0.5` | **no field** — see `field-language` §7's open question |
| — | `value` points at the node's own float; **valid only within the frame that registered it** |

Rules, each of which is already documented in `Modulation.h` and each of which
a fresh session gets wrong:

| Rule | Source |
|---|---|
| **Re-register every frame, while the node draws.** `ClearFrameParams()` runs each frame | `Modulation.h:174` |
| **Register even when collapsed or hidden.** `gParamRegisterOnly` exists because registering is what lets a modulator keep writing into a param | `Modulation.h:180` |
| **Never store the `float*`.** `KnownParam`'s stored copy deliberately nulls it | `Modulation.h:186` |
| **`paramIndex` must be stable across frames** | it is half the binding key; an index that shifts when a `param` line is added silently re-points every modulation cable |
| **A wired modulator beats a typed expression** on the same param | `Modulation.h:139` |

> **OPEN — what is a Field `param`'s `paramIndex`?** Editing the source text
> adds, removes and reorders `param` declarations. If `paramIndex` is the
> declaration ordinal, inserting a `param` line at the top re-points every
> modulation cable on that node — the same class of bug `node-ui-pillars` P7
> documents for filter-mode indices. Options: **(a)** a stable hash of the param
> name, with collision detection at compile time; **(b)** a monotonically
> increasing per-node counter, allocated on first sight of a name and never
> reused, persisted in the patch; **(c)** declaration order, and accept that
> editing a Field body scrambles its modulation. **(c)** is not acceptable.
> Ask the owner between (a) and (b).

**After adding a `param` path, run
`.claude/skills/node-param-audit/SKILL.md`** to confirm the param is
modulatable and appears in the performance matrix.

## 4. Reaching the audio thread — `ParamMailbox`, and nothing else

`src/audio/ParamMailbox.h` (50 lines):

| Member | Thread | Note |
|---|---|---|
| `PrepareToPlay(double sampleRate)` | main | configures the per-slot one-pole smoothers |
| `Push(int paramId, float value)` | **main only** | latest write wins; not a FIFO, by design |
| `SmoothedValue(int paramId)` | **audio only** | advances smoothing by one sample and returns it |
| `SetImmediate(int paramId, float value)` | **audio only** | no ramp; startup use |
| `kMaxParams` | — | **128** |

The header carries its own history lesson (`ParamMailbox.h:11–20`): an earlier
ring-buffer version had the producer's overrun-drop path writing the
consumer-owned head index, which broke the single-consumer invariant under real
concurrent load. **Do not reintroduce a queue for Field.** One atomic slot per
param is the design.

| Wrong | Right |
|---|---|
| a new lock-free queue for Field params | `ParamMailbox::Push` |
| reading a Field node's `float` field directly from `ProcessBlock` | `SmoothedValue(paramId)` |
| more than 128 params in a sample kernel | a compile error naming `kMaxParams` |
| stepping a frame-rate value into the sample domain raw | let it arrive through the smoother — `field-domains` §5 |

**Audio → main goes through `MeterRing`**, which is how a `sample` → `frame`
`reduce` publishes (`field-domains` §3). No new channel.

**The two-object rule applies unchanged**
(`.claude/skills/new-audio-node/SKILL.md` §0.2): the `INode` on the main thread
owns an `AudioNode` on the audio thread; they talk only through `ParamMailbox`
and `MeterRing`. Field's sample backend lives in the `AudioNode` half. The
compiler lives in the `INode` half. **The compiler is never reachable from
`ProcessBlock`.**

## 5. Save / load / undo — the patch contract

`src/core/Patch.h` documents the line grammar. The relevant kinds:

```
mod  <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre> [<lo> <hi> [<enabled>]]
expr <dstIndex> <dstParam> <expression text to end of line>
glob <name> <expression text to end of line>
```

| Thing | How it saves |
|---|---|
| Field source text | a `Text` param via `VisitParams` — the same way `FormulaNode` saves `formula` (`src/nodes/FormulaNode.h:41`) and `EquationNode` saves its formula |
| `param` values | `Float` params via `VisitParams` |
| modulation on a Field param | the existing `mod` line, keyed `(nodeIndex, paramIndex)` — hence §3's open question |
| `graph`-domain constants | the existing `glob` lines |
| `state` cells | **needs a new line kind** — see below |

**Two traps in the line grammar, both already burned into this codebase:**

- **A space breaks a token-separated line.** `Patch.cpp` reads
  `node <index> <category> <typeName>` with `>>`, which is why the category
  string must be one whitespace-free token
  (`.claude/skills/new-audio-node/SKILL.md` §3.4). A Field record with a
  free-text field must be **last on its line** — that is exactly why `expr` and
  `glob` put the expression text at the end.
- **Multi-line source text is already solved — do not invent a second scheme.**
  `VisitParams::Text` writes an `s <name> <escaped>` line, and
  `Patch.cpp`'s `EscapeLine` / `UnescapeLine` (`src/core/Patch.cpp:49` and `:64`)
  turn an embedded newline into a literal `\n` and back. That is how
  `FormulaNode::formula` — also multi-line GLSL — already round-trips. A Field
  program's source text uses the same path, unchanged. Note the reader's
  documented forgiveness at `Patch.cpp:154`: any escape other than `\n` is left
  exactly as written, so a Field program containing a literal backslash sequence
  survives, but **`\n` inside a Field string literal would not** — which is one
  more reason v1 has no strings (`field-realtime` §1 rule 4).

> **OPEN — how do `state` cells serialize?** `ParamVisitor` has only
> `Float/Int/Bool/Text/Color`; an element- or pixel-domain state field is
> thousands to millions of floats. Options: **(a)** persist frame/sample cells
> only, as individual `Float` params named `state.<name>` — small, fits the
> existing visitor, and satisfies the brief's stated motivation ("saving
> mid-reverb and reloading restores the tail", a sample-domain case);
> **(b)** a new `state` patch line with base64 payload — arbitrary size, but a
> new line kind and multi-megabyte patch files; **(c)** a sidecar file next to
> the patch. This is the same open question as
> [`field-state`](../field-state/SKILL.md) §6 — resolve it once, in both places.

**Undo/redo and copy/paste are generic.** They go through `VisitParams`. A
Field node that saves correctly through `VisitParams` gets undo, redo,
copy/paste and duplication for free. **Adding a per-node entry to any of those
paths is a sign the node is built wrong** — the same rule as
`.claude/skills/new-audio-node/SKILL.md` §3.

## 6. Pins and slots

`INode`'s input slots share **one index space** across kinds
(`.claude/skills/new-audio-node/SKILL.md` §4). A Field node with a geometry
input at slot 0 and an audio input at slot 1 answers `GeometryInputSlot(0)` and
`AudioInputSlot(1)` — **not both from 0**. Non-contiguous slot indices make the
generic pin count stop at the first gap, which shows up as a node with missing
pins.

| Field node shape | Implements | Typical pins |
|---|---|---|
| pixel kernel (image source/effect) | `INode` | image in, image out |
| element kernel (geometry op) | `INode`, `IGeometrySource` | geometry in, geometry out |
| sample kernel (audio effect/synth) | `INode`, `IAudioSource` | audio in, audio out |
| frame kernel emitting a control value | `INode`, `IModulator` | modulator out |

## 7. `IGeometrySource` — what an element-domain node must answer

`IGeometrySource` lives in `src/nodes/Geometry3DNodes.h:118`, not in a header
of its own. The pure virtuals:

| Member | Note |
|---|---|
| `const Mesh& GetMesh()` | object space |
| `unsigned long long MeshRevision()` | **pure virtual on purpose** — "a source that silently returned a constant would freeze its geometry at the first frame" |
| `Mat4 GetModelMatrix() const` | |
| `Material GetMaterial() const` | |

**The passthrough-forwarding trap**, which has already caused a real bug here
(env light cache invalidation): a node that derives its mesh from an upstream
source must forward `PassthroughSource()`, `SurfaceTextureRevision()`,
`GetMappingTransform()`, `GetInstanceGroupMatrix()`, `InstanceSelection()` and
`InstanceTransformOverride()`. An element-domain Field node **is** such a node.
Read `.claude/skills/new-geometry-node/SKILL.md` before writing one, and run
`.claude/skills/geometry-transform-sweep/SKILL.md` after.

## 8. `GLUtil::CompileProgram` — the pixel backend's target

`unsigned int CompileProgram(const char* fragSrc, std::string* outError = nullptr)`
(`src/core/GLUtil.h:34`).

| Fact | Consequence for the generated shader |
|---|---|
| It supplies its own vertex shader, binding `aPos` to 0 and `aUv` to 1 | emit a fragment shader only |
| The fragment stage receives `in vec2 vUv` | that is the pixel domain's `uv` |
| **Every `#version` in `src/` is `150`** — all 57 of them | emit `#version 150`, never 330 |
| The driver log is captured into `char log[1024]` | a long error cascade loses the first, most useful message — cap reported errors (`field-compiler` §7) |
| Returns `0` on failure, fills `outError` | keep the previous program; see below |

Copy `FormulaNode::Apply()`'s shape exactly
(`src/nodes/FormulaNode.cpp:390–406`): compile into a local, return early
leaving the old program untouched on failure, delete the old one and swap only
on success. And copy its **retry policy**: `CookIfNeeded` recompiles only when
`mProgram == 0 && mLastError.empty()` (`src/nodes/FormulaNode.cpp:415`), so a
broken program costs nothing per frame. A Field pixel node that recompiles a
failing shader 60 times a second is a visible bug.

## 9. Wiring checklist for a Field node

Everything not listed here is already generic. Adding an entry to a generic
path is a sign the node is built wrong.

1. `src/nodes/FieldXxxNode.h` / `.cpp` — the `INode` half; for a sample-domain
   node, an `AudioNode` half held by `std::unique_ptr` with an out-of-line
   ctor/dtor so `main.cpp` never sees the audio-thread class.
2. `CMakeLists.txt` — add the `.cpp` (node files under `src/nodes/` ~line 98;
   audio-thread files under `src/audio/` ~line 54). The compiler itself belongs
   under `src/core/` or a new `src/field/`.
3. `src/main.cpp` include, with the others (~line 92).
4. `RegisterNodes()` — `REGISTER_NODE(FieldXxxNode, Display Name, "Category")`
   (`src/core/NodeFactory.h:50`). **The category must be one whitespace-free
   token.**
5. Name-collision check — `NodeFactory::DuplicateNames()` catches it at runtime.
   `Formula`, `Equation`, `Expression`, `Noise`, `Curve`, `Shape`, `Pattern`
   and `Transform` are taken.
6. `VisitParams` — source text, every `param`, and whatever §5 settles for
   `state`.
7. `InputLabel(slot)` for every pin.
8. The body draw function and its dispatch entry — and
   `.claude/skills/node-ui-pillars/SKILL.md` **before** writing it. Symmetry and
   dark-mode contrast are non-negotiable, and a Field node's auto-generated
   knob rows must sit on the row grid like any other node's.
9. The node help table (~line 7780) — one sentence.

## 10. Exit criterion for a Field integration change

1. It builds clean on macOS, and the generated GLSL is `#version 150`
   (`.claude/skills/windows-parity/SKILL.md` — a shader that compiles on one
   driver and not the other is the classic failure here).
2. `Expression::Evaluate`'s signature and its three call sites are unchanged.
3. Spawned from the palette, the node shows the right pin count and labels and
   its body renders with a non-empty readout strip.
4. Every `param` appears in the modulation matrix, is bindable, and its binding
   survives editing the Field source (§3's open question must be **answered**,
   not deferred, before this can be checked).
5. Source text, params and (per §5) state survive save → load → undo →
   copy/paste → delete unchanged.
6. A deliberately broken program leaves the last working program running,
   surfaces its error, and does not recompile every frame.
7. Deleting the node mid-playback does not crash and logs zero xruns.
8. No new cross-thread channel exists — `grep` the diff for new atomics and
   queues.
9. `TextureRevision()` changes when pixels changed and not when they did not
   (`.claude/skills/data-accuracy-sweep/SKILL.md`).
10. `/run-infinite-hygiene` passes; plus `node-param-audit` for params,
    `geometry-transform-sweep` for an element node, and the audio sweeps for a
    sample node.
