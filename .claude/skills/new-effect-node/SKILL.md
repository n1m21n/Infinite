---
name: new-effect-node
description: The standard procedure for adding a new Effects/Color/Compositing node to Infinite — the FilterDef table pattern, when it's a table entry vs a real C++ class, the shared GLSL preamble, the caching bug trap that has already happened here, and the machine-checkable exit criterion. Use when implementing a new image effect, color-grading module, or compositing operator; when writing the prompt for a fresh session that will implement one; or when a filter node produces a black/frozen/wrong-looking image, doesn't animate, or its params don't reach the shader.
---

Paths are relative to the repo root (`/Users/namansoni/infinite`).

This is the 2D-effects sibling of `new-audio-node` and `new-geometry-node`.
Read whichever of those you haven't if this is your first node in Infinite —
the shape of "how nodes wire in" is shared; what's node-family-specific is
below.

---

## 0. The one thing that makes this category different

**Almost every Effects/Color/Compositing node is a data table entry, not a
new C++ class.** `FilterNode` (`src/nodes/FilterNode.cpp`, 134 lines) is
instantiated once per `FilterDef` — adding an effect is a `FilterDef` entry
in `src/core/FilterDefs.cpp` plus a GLSL fragment body, not a header/source
pair, not a `CMakeLists.txt` change, not a `main.cpp` include. Registration
is a single generic loop (`main.cpp` ~2019):

```cpp
for (const FilterDef& def : GetFilterDefs())
   NodeFactory::Instance().Register(def.name,
      [defPtr]() -> INode* { return FilterNode::CreateFor(*defPtr); },
      def.category);
```

**Only reach for a hand-written class** (like `Blend`, `LayerStack`,
`Switcher`, `RemoveBackground`, `Feedback`, `Draw`) when the effect
genuinely cannot be one fragment-shader pass over one or two input textures
— e.g. it needs multiple render targets, ping-pong feedback across frames,
CPU-side work (Vision segmentation), or persistent state beyond "the current
param values." Check `src/nodes/FilterDefs.cpp`'s ~90 entries first; the
overwhelming majority of effect ideas fit the table. If you're not sure
which this is, say so in the prompt rather than guessing — picking the
class path for something that's really a table entry means duplicating
FBO/caching/dispatch machinery that already exists.

---

## 1. Read these before writing code

| File | Why |
|---|---|
| `src/core/FilterDefs.h` | the `FilterDef`/`FilterParamDef` schema — every field you'll fill in |
| `src/core/FilterDefs.cpp` | ~90 existing entries; find one with a similar shape (single-pass color op, blur-family multi-pass, second-input like displace/LUT) and copy its structure |
| `src/nodes/FilterNode.cpp` | the shared preamble, the caching signature, how params reach the shader — read this once, it explains everything table entries rely on |
| `docs/plans/optimization/research-implementation-map.md` | shader technique references if the effect needs one you haven't implemented before |

---

## 2. The shared GLSL contract

Every `fragmentBody` is appended to this preamble (`FilterNode.cpp`
`kPreamble`) — do not redeclare any of these, and do not assume anything
not listed here exists:

```glsl
#version 150
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;       // your one required input
uniform vec2 uTexelSize;      // 1/width, 1/height — for neighbor sampling
uniform float uTime;          // transport seconds; only bound if your body references it
uniform sampler2D uSrc2;      // second input, only meaningful if inputs = 2
uniform int uHasSrc2;         // 1 if a second cable is actually connected, else 0 (uSrc2 aliases uSrc)
```

Your `fragmentBody` is just extra uniform declarations (one per
`FilterParamDef::uniformName`, matching `Type`) plus a `main()` that reads
`vUv`/`uSrc` and writes `fragColor`.

- **`Type::Float` → GLSL `uniform float`**, `Type::Int`/`Bool`/`Enum` →
  `uniform int`, `Type::Color` → `uniform vec3`. A mismatch here compiles
  fine (GLSL doesn't know your intent) but silently reads garbage — the enum
  in `FilterParamDef::Type` must match the GLSL type you declared, by hand,
  every time.
- **Second input.** Set `inputs = 2` on the `FilterDef`; `FilterNode`
  handles pulling `uSrc2`, guarding the unbound-texture-unit case, and
  setting `uHasSrc2`. Displacement maps, LUTs, and any "blend two things"
  effect use this.
- **`uTime`.** Only reference it if the effect genuinely animates on its
  own (a procedural noise field, a scanline sweep). `FilterNode` detects
  `"uTime"` in your fragment body text at construction and skips the
  per-frame cache short-circuit when present — see the bug trap below.

---

## 3. The caching bug trap — read this before anything else

`FilterNode::CookIfNeeded` builds a `Signature` (upstream texture revision,
width/height, all param values) and **skips re-rendering entirely** when
the signature is unchanged from the last cook — a Blur feeding into a Twirl
feeding into a Kaleidoscope must not re-run all three every frame if
nothing changed.

This means: **anything your shader reads that isn't `uSrc`/`uSrc2`'s
revision or a declared `FilterParamDef` will silently freeze.** This
already happened on the 3D side (`Render3DNode`'s scene cache didn't
invalidate when an Environment node's intensity/rotation changed, because
those were read as raw uniforms outside the revision-tracked path — see
`08dd3ec`). The Effects-node equivalent trap is the same shape: if you ever
add a fragment-body uniform driven by something other than a declared param
or `uSrc`/`uSrc2` (a global, a second node's field read directly), it must
be folded into `Signature` or the effect will appear to work once and then
stop updating. The `uTime` special-case in `FilterNode` is the one
sanctioned exception, and it exists specifically because "always time" is a
declared, detectable escape hatch — don't invent a second one.

---

## 4. The wiring checklist (table-entry path — the common case)

1. **Pick a name and category.** Category is `"Effects"`, `"Color"`, or
   `"Compositing"` (string, must match `FilterDef::category` exactly — these
   three already have palette colors in `src/core/CategoryColors.cpp`,
   nothing to add there).
2. **Name collision check.** `NodeFactory::DuplicateNames()` catches it at
   runtime, but grep `FilterDefs.cpp` and the hand-written-class
   `REGISTER_NODE` calls in `main.cpp` first — `Noise`, `Curve`, `Curves`,
   `Shape`, `Pattern`, `Transform` are already taken by other node families
   (see `new-audio-node`'s collision table for the audio side of this same
   list).
3. **Write the `FilterDef`** in `FilterDefs.cpp`: `name`, `category`,
   `params` (one `FilterParamDef` per control, in the order you want them
   drawn — use `sectionLabel` if the list is long enough to need grouping,
   see the Color Adjustments entry), `fragmentBody`, and `inputs = 2` only
   if you need a second texture.
4. **Write the GLSL.** Match every `uniformName`/`Type` pair to a `uniform`
   declaration in `fragmentBody`. Sample `uSrc` at `vUv` (or an offset
   derived from `uTexelSize` for blur/distort-family effects) and write
   `fragColor`.
5. **That's it for wiring** — no `CMakeLists.txt`, no `main.cpp` include, no
   dispatch-ladder entry. The generic registration loop and `FilterNode`
   handle spawn, pins (always one image in, `inputs=2` adds a second),
   params panel (auto-generated from `FilterParamDef::type`), save/load,
   undo, copy/paste, bypass.
6. **Node help table** — `FilterHelpText` (`main.cpp` ~10189) is a per-name
   lookup; add one sentence in the existing voice: what it does and the one
   non-obvious thing about it (see `Geometry3DHelpText`/
   `SpecificNodeHelpText` for the pattern other categories use).

## 4b. The hand-written-class path (rare)

If the effect needs multiple passes, feedback across frames, or non-shader
work, follow the shape of an existing one closest to what you're building
(`FeedbackNode` for cross-frame, `RemoveBackground` for CPU/Vision work,
`LayerStack`/`Blend` for multi-input compositing) rather than `FilterNode`.
This *is* the audio-node-style wiring: new header/source pair, add to
`CMakeLists.txt` (`src/nodes/` list, ~line 98), include in `main.cpp`,
`REGISTER_NODE(XxxNode, Display Name, "Category")`, a `Draw*Body`/dispatch
entry, and a help-table entry. State explicitly in the prompt which path
you're taking and why — don't let a fresh session default to the harder one
for something that's really a table entry, or the easier one for something
that genuinely needs per-frame state.

---

## 5. Bug traps, each of which has already happened in this codebase

- **Cache signature omits a value the shader actually reads** (§3). The
  single most likely way a new effect "looks right once, then freezes."
- **`FilterParamDef::type` doesn't match the GLSL uniform type.** Compiles,
  reads garbage silently — no error anywhere.
- **Space in the category string.** Same trap as audio nodes: `Patch.cpp`
  parses `node <index> <category> <typeName>` with `>>`, so a space eats
  the type name on load. Categories here are single words already
  (`Effects`, `Color`, `Compositing`) — don't add a multi-word one.
- **Second input left unbound.** If `inputs = 2` but nothing is patched into
  the second slot, sampling an unbound texture unit is undefined and spams
  the GL driver log — `FilterNode` already guards this by aliasing `uSrc2`
  to `uSrc` when disconnected (`uHasSrc2 = 0`); branch on `uHasSrc2` in your
  shader rather than assuming `uSrc2` holds real content.
- **Cheap-looking shader, expensive redraw.** A cache-miss re-render happens
  on every param drag; an effect with an expensive loop (e.g. a large blur
  kernel written as nested samples instead of separable passes) will feel
  laggy under interactive dragging even though it's "just a filter."

---

## 6. Tests

Infinite has no test binary; tests are `getenv("INFINITE_…")` fixtures in
`main.cpp`. There's no per-effect DSP-fixture equivalent yet (unlike audio's
`INFINITE_DSPTEST`) — the coverage that exists is the full node
round-trip:

- `INFINITE_ROUNDTRIPTEST` (registry-driven, `main.cpp` ~17083) picks up
  every registered `FilterDef` automatically — no per-node work needed —
  and checks params survive copy/paste and save/load.
- Manually verify the caching bug trap yourself: drag a param, confirm the
  image updates; if the effect depends on anything beyond declared params
  and `uSrc`/`uSrc2`, toggle that thing and confirm the image updates too.
- Then run `/run-infinite-hygiene` before committing.

If you're adding several effects in one session and this keeps mattering,
that's a signal a golden-image visual-regression sweep (render each
`FilterDef`, diff against a stored reference) is worth building as its own
skill — flag it rather than hand-rolling one-off checks each time.

---

## 7. Exit criterion — state it machine-checkably in every prompt

A node is done when all of these hold:

1. It builds clean (or, for the table path, no build step is needed beyond
   the existing `FilterDefs.cpp`/`FilterNode.cpp` compilation).
2. Spawned from the palette it shows the right pin count (1, or 2 if
   `inputs = 2`) and the params panel matches the `FilterDef` param list.
3. It renders correctly on a real image, and animates only if it's supposed
   to (only references `uTime` when the effect is meant to self-animate).
4. Its params survive save → load → undo → copy/paste → delete unchanged.
5. Dragging any param that affects the shader's output visibly updates the
   image every time — no stale/frozen frame.
6. `/run-infinite-hygiene` passes.
7. `README.md`'s node table is updated to list it under the right category.

---

## 8. Prompt template for a fresh session

```
Implement the <NAME> node in Infinite (/Users/namansoni/infinite).

Category: <Effects|Color|Compositing>. Inputs: <1 | 2>.
What it does: <...>. Params: <name, type, range for each>.

Follow .claude/skills/new-effect-node/SKILL.md for the procedure. It's
prescriptive — do not re-derive it.

This is almost certainly a FilterDef table entry in src/core/FilterDefs.cpp,
not a new C++ class — only use a hand-written class if the effect needs
multiple render targets, cross-frame state, or CPU-side work, and say so
explicitly if you think it does.

Two rules that override anything you infer:
1. Every uniform your fragment body reads must be either uSrc/uSrc2 (whose
   revisions are already in FilterNode's cache signature) or a declared
   FilterParamDef — nothing else, or the effect will freeze after the first
   render instead of updating live.
2. FilterParamDef::type must match the GLSL uniform type you declared by
   hand — Float/vec, Int|Bool|Enum/int, Color/vec3. A mismatch compiles and
   silently reads garbage.

Reference entries: pick the closest existing FilterDef in FilterDefs.cpp by
shape (single-pass color op, blur-family, or a second-input effect like
displace/LUT) and match its structure.

Done when SKILL.md §7's seven criteria all hold. Report each one.
```
