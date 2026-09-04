# Field build step 22 — offset reads of a pixel state cell (OPEN-C)

**Status:** built and verified on `feature/field-step-22-pixel-offset-reads`.

Answers `OPEN-C` from `algorithms.md` §4 with the spelling decided in
`language-decisions-and-presets.md` §1, and is the first step that makes the
"evolving texture" half of the algorithm catalogue writable.

## What the language gained

| | |
|---|---|
| offset read | `A(uv + d)` where `A` is a **pixel** state cell. Bare `A` stays sugar for `A(uv)` |
| what it reads | the previous cook's cell, always — never a value written earlier in the same body, which is what keeps the pass order-independent |
| boundary | per cell: `state float A = 1 [wrap]`, `[clamp]` (default), `[border]` |
| `age` | new pixel reserved name: cooks since this node's state was cleared, `0` on the first cook |
| precision | a kernel with any offset read gets an RGBA32F state bank; one without keeps RGBA16F |

`age` was not in the decision doc. It turned out to be required: Gray-Scott
starting from a uniform `B = 0` sits on a fixed point forever, and `frame` is
the **global** cook counter — already in the thousands when a node is spawned —
so it cannot express "the first cook". A permanent injection strong enough to
start the reaction also saturates the frame; only a one-shot seed works.

## Two pre-existing bugs this exposed

Neither was introduced by this step; both were found because a diffusion kernel
is the first thing that actually reads these values.

1. **`res` and `aspect` were frame-domain.** Rate inference therefore hoisted
   any expression mentioning them — `d = 1.0 / res` — into the CPU-side
   prologue, and that evaluator only knows `t`/`dt`/`frame`/params. `res`
   evaluated to **0**, so every kernel that measured a texel got a texel size
   of zero. Both are now pixel-domain and bound from `fld_res` inside `main()`.
2. **A hoisted vec2/3/4 was uploaded with `glUniform1f`.** Lane 0 was set and
   every other lane stayed zero, so `e = vec2(0.22 * cos(t), 0.22 * sin(t))`
   sat at the origin instead of orbiting. The pixel partition now hoists
   **scalars only**; multi-lane frame-domain values stay in the body at a cost
   of a few ALU per pixel.

## Files

| File | Change |
|---|---|
| `src/core/field/FieldAst.h` | `AstDeclState::boundary` |
| `src/core/field/FieldParse.cpp` | parses `[clamp\|wrap\|border]`; `age` added to the pixel reserved-shadow check |
| `src/core/field/FieldIR.h` | `BoundaryMode`; `IRNode::isOffsetRead`; `DeclaredState::boundary` |
| `src/core/field/FieldIR.cpp` | a call whose callee names a state cell lowers to an offset read; non-pixel cells refused; `res`/`aspect`/`age` seeded as pixel-domain; scalar-only hoist |
| `src/core/field/Transfer.{h,cpp}` | `BoundaryModeFromString` / `ToString` |
| `src/core/field/GlslBackend.{h,cpp}` | offset read → one `texture()` fetch with its boundary rule; `fld_age`; `offsetReadCount` / `usesOffsetReads` |
| `src/core/field/PixelState.{h,cpp}` | `Resize(w, h, highPrecision)`, `BytesInUse()` |
| `src/core/GLUtil.cpp` | `GL_RGBA32F` gets `GL_FLOAT` pixel type |
| `src/nodes/FieldPixelNode.{h,cpp}` | `mStateAge`; 32F for simulations; two new presets |
| `src/main.cpp` | `INFINITE_FIELDPIXELTEST` assertions 19-27 |

## Evidence

`INFINITE_FIELDPIXELTEST` — all 27 assertions pass. The load-bearing ones:

| # | Asserts |
|---|---|
| 19 | Gray-Scott compiles, and emits exactly 8 fetches |
| 20 | `[wrap]` lowers to `fract`, the default to `clamp` — per cell |
| 21 | an offset read of a non-pixel cell is refused, with a message naming why |
| 22 | 32F for a neighbour-reading kernel, 16F for a plain trails kernel |
| 23 | **a seeded spike propagates to a texel 6 away** — 0 under the old current-pixel-only rule, so this fails if the fetch coordinate is ever dropped |
| 24 | the shipped RD preset evolves, stays finite, and carries real spatial variance |
| 26 | `age` fires exactly once — a cell seeded on the first cook holds the seed, not a multiple of it |
| 27 | the advection preset transports density away from its emitter |

`/run-infinite-hygiene`: 67 passed, 3 known xfail, 1 failure (`GROUPTEST`)
that reproduces identically on clean `main`.

## What is still open

`OPEN-A` (ring buffers / delay lines), `OPEN-B` (element neighbour reads —
the "evolving mesh" half), and `OPEN-D` (second sample input, note events).
All three have decided spellings in `language-decisions-and-presets.md` §1 and
none is built. `OPEN-B` is the natural next step.
