# Step 19: `delay(x, samples)` sample-domain intrinsic

> **Handoff note:** this doc is written to be executable by an AI agent with
> no other context than this repository. It cites real file:line locations
> confirmed by direct investigation as of commit `58acad2` on branch
> `bugfix/field-device-ui-and-cable-fixes`. If line numbers have since
> drifted, re-locate by the named function/symbol, not the raw number.
>
> This is workstream 1 of 3 in a related set — see also
> [`step-20-field-primitive-node.md`](step-20-field-primitive-node.md) and
> [`step-21-field-synth-node.md`](step-21-field-synth-node.md). They are
> independently implementable; suggested order is 20 → 19 → 21 (see each
> doc's own rationale), but nothing here blocks on the other two except that
> a Karplus-Strong-style Field Synth preset (step 21) would want this to
> land first.

## Context

Field Effect (`FieldSampleNode`) currently fakes delay-line effects
(Reverb, Delay) entirely out of `state float` cells hand-indexed as a shift
register — functional, but capped at `Field::kSampleMaxStateCells = 64`
(`src/core/field/SampleProgram.h:21`), i.e. ~1.45ms of total delay at
44.1kHz if used naively, and every new effect preset has to hand-roll its own
shift-register logic in kernel text. This step adds a first-class
`delay(x, samples)` intrinsic so presets can request a real delay line in one
call, unlocking genuine Reverb/Delay/Chorus/Flanger/Phaser presets.

The user explicitly chose `delay(x, samples)` — a function-call intrinsic —
over any new declaration syntax.

## Architecture ground truth

Field is not one compiler. The **sample domain is a fully separate,
self-contained compiler** in `src/core/field/BackendRegister.cpp` — it does
**not** go through the typed IR in `src/core/field/FieldIR.cpp` at all (that
IR is used only by the element domain / `ElementBackend.cpp` and pixel domain
/ `GlslBackend.cpp`). This is explicitly documented in a header comment at
`BackendRegister.cpp:11-21`, explaining the sample domain needs straight-
line, no-branch bytecode for the audio thread.

**Consequence: this step touches only sample-domain files** —
`BackendRegister.cpp`, `SampleProgram.h`, `SampleRuntime.h`,
`FieldSampleNode.cpp/.h`. Do not touch `FieldIR.cpp`, `ElementBackend.cpp`,
or `GlslBackend.cpp` for this step.

## Why no grammar/parser changes are needed

Plain `name(args)` calls already parse as an ordinary `AstCall{"delay",
[x, N]}` via `ParsePrimary` (`src/core/field/FieldParse.cpp:158-186`) — the
same mechanism that already parses `reduce.max(x)`-style dotted calls (glued
into a single callee string at parse time,
`src/core/field/FieldParse.cpp:194-249`, `ParsePostfix`). No new AST node, no
new token, no grammar rule. This is purely a `BackendRegister.cpp`-side
compiler feature — `delay(x, N)` already parses correctly today, it just
isn't recognized during compilation.

## Where intrinsic dispatch happens today

`BackendRegister.cpp::CompileCall` (lines 87-160) is a plain if/else chain on
the callee string — not a table (tables are an element/pixel-domain-only
pattern living in `FieldIR.cpp`). `reduce.rms` is hand-rolled inline there;
every other `reduce.*`/`map` form is explicitly refused with `"...not
supported inside a sample-domain kernel in v1"`. `delay` becomes a new
branch in this if/else chain.

## Design: model `delay(x, N)` as an implicit ring-buffer state cell

Precedent to follow — `state float` declarations:

- Compile-time: `AstDeclState` → `BackendRegister.cpp:287-332` (reserved-name
  check, float-only type enforcement, literal-only initializer requirement,
  the `kSampleMaxStateCells` cap check at lines 320-326) →
  `SampleProgram::state` (`std::vector<SampleStateInit>`, built once on the
  main thread).
- Runtime: `AudioFieldSampleNode` holds fixed-size raw C arrays
  `mStateCur[64]`/`mStateNext[64]` (`src/nodes/FieldSampleNode.cpp:193-194`,
  no heap allocation), crossed to the audio thread via
  `SampleSlotT<Field::SampleProgram>` compile-swap — main thread `Push()`s,
  audio thread `SwapIn()`s once at the top of `ProcessBlock`, never
  mid-block — with `(name,type)`-matched state transplant on every
  successful hot-swap (`SampleStateInit::transplantFromIndex`, resolved once
  on the main thread by exact string match against the previous compiled
  program).

`delay(x, N)` differs from every other sample-domain intrinsic in one
important way: it has a **side effect** (it advances a ring write pointer)
rather than being a pure expression. Concretely:

1. **`N` must be a compile-time integer literal.** Same enforcement pattern
   already used for `for`-loop bounds and `state` initializers (both already
   require `AstKind::Literal` in `BackendRegister.cpp`). Reject a non-literal
   `N` with a clear compile error naming the call site.
2. Each `delay(x, N)` call site implicitly declares its own ring-buffer state
   allocation of `N` cells (distinct from a user-declared `state float`, but
   reusing the same underlying state-cell storage/transplant machinery) plus
   a scalar write-cursor.
3. **Add a separate cap constant from `kSampleMaxStateCells`.**
   `SampleProgram.h:21` caps ordinary `state` cells at 64 — too small for a
   useful delay. Add e.g. `Field::kSampleMaxDelayCells` sized for at least
   one reverb-length delay (roughly 2200–4400 cells covers ~50-100ms at
   44.1kHz). **This exact size is an open decision — pick one, document why
   in a comment at the declaration site, don't leave it unstated.** Track it
   as a **cumulative budget across all `delay()` calls in one kernel** (a
   preset may call `delay()` multiple times for a multi-tap effect),
   enforced the same way as the existing cap check at
   `BackendRegister.cpp:320-326`.
4. **Runtime opcode(s).** Add new `SampleOp` case(s) to the flat `switch`
   interpreter in `SampleRuntime.h` for: reading the ring at the current read
   position (`N` samples behind the write cursor), and writing `x` into the
   ring at the write cursor then advancing it mod `N` — or one fused opcode
   doing both per call, whichever is simpler to encode correctly. No
   allocation, no branch-on-data-shape — matches the existing interpreter's
   documented audio-thread constraints (see `SampleRuntime.h`'s header
   comment).

## Two scope decisions to make explicitly, not inherit silently

- **Persistence.** Sample-domain `state` (and by extension delay buffers) is
  **not currently persisted to the patch** — `VisitParams` in
  `FieldSampleNode.cpp` never touches `mStateCur`. Decide whether `delay()`
  buffers should be saved/restored on patch load (probably not needed for
  v1, since audio state naturally resets on transport start) — but state
  that decision explicitly in code comments and/or the PR description rather
  than leaving it implicit.
- **Fault reset.** The existing NaN/Inf sweep that resets `state` cells
  should almost certainly also sweep delay-buffer cells. Verify this is
  wired in when the new storage is added — don't assume it happens
  automatically if delay buffers end up in a separate array from
  `mStateCur`.

## Files to touch (only these four)

- `src/core/field/BackendRegister.cpp` — `CompileCall` new `delay` branch;
  new cap-check mirroring the `kSampleMaxStateCells` enforcement pattern.
- `src/core/field/SampleProgram.h` — new `kSampleMaxDelayCells` (or similar)
  constant; extend `SampleProgram`'s data shape to carry delay-buffer layout
  info alongside the existing `state` vector.
- `src/core/field/SampleRuntime.h` — new opcode case(s) in the interpreter
  switch.
- `src/nodes/FieldSampleNode.cpp/.h` — extend `AudioFieldSampleNode`'s
  fixed-size state arrays (or add a sibling fixed-size delay-buffer array)
  and wire it through the same compile-swap/transplant path as
  `mStateCur`/`mStateNext`.

## Verification

1. Build (`cmake --build build -j 8`).
2. Write a small test preset using `delay(in, 4410)` (100ms at 44.1kHz) as a
   single tap; load it in the app and confirm audibly it produces a clean
   discrete echo, distinct from the current diffuse/short character of the
   existing state-cell-based Reverb/Delay presets.
3. Confirm a `delay()` call with a non-literal `N` argument produces a clear
   compile error, not a crash or silently wrong result.
4. Confirm the cumulative-budget cap check rejects a preset requesting more
   total delay cells than the budget allows, with a message naming the
   limit.
5. A successful C++ build only checks the preset strings compile as string
   literals — it does not validate the Field kernel text itself, which is
   checked at preset-load time inside the running app. Say this explicitly
   when reporting status; don't claim the kernel change is "verified" until
   it has actually been loaded and heard in the app.
6. Deploy to `~/Desktop/Infinite.app` after a successful build and manual
   check, per this project's existing convention.
