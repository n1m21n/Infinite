# Field Build Step 8 — Transfer Operators Design Notes

This document records the design decisions, syntax rules, desugaring strategies, and scope boundaries for Field Step 8 (`reduce`, `map`, `broadcast`, `resample`, `downsample`).

---

## 1. Syntax & Operator Recognition

### 1.1 `broadcast`
- **Rule:** Implicit only. Coarse-to-fine domain transitions (`Graph -> Frame -> Element/Pixel/Sample`) happen automatically via rate inference and prologue hoisting / uniforms.
- **Refusal:** If written explicitly as `broadcast(...)`, the compiler refuses with:
  `"broadcast is implicit; write 'P.y += amount'"` (or `"broadcast is implicit; remove this call"`).
- **Cost:** 0 (hoist / uniform).

### 1.2 `reduce`
- **Forms:**
  - `reduce.sum(x)`
  - `reduce.rms(x)`
  - `reduce.rms(in, lo, hi)` (3-arg band-limited RMS: **Sample domain only**)
  - `reduce.min(x)`
  - `reduce.max(x)`
  - `reduce.mean(x)`
- **Parsing:** Dotted method syntax `reduce.<op>(...)` is parsed in `FieldParse.cpp` (`ParsePostfix`) by detecting an `AstIdent` followed by `.` and an operation identifier and an immediate `(`.
- **Arguments:** In Element domain, the argument must be a bare variable or attribute name (`P`, `N`, `uv`, `Cd`, or user-declared attribute like `heat`). Arbitrary expressions must be bound to a variable/attrib first:
  `"reduction argument must be a bare variable or attribute name (got expression; hint: bind the expression to an attribute or variable first, e.g. 'heat = length(P) * 0.1; avg = reduce.mean(heat)')"`
- **Domain Transitions:**
  - `Element -> Frame`: Evaluated once per frame in the Element bytecode prologue via `OpReduceElementAttrib` reading the full attribute array from `ElementStore`.
  - `Sample -> Frame`: Produces Frame-domain result in typed IR. (Routes via `MeterRing::ReadLatest` when audio domain kernel lands in Step 9; verified via direct IR unit tests here).
  - `Pixel -> Frame`: Refused in v1 per Open Question (c):
    `"pixel->frame reductions are not supported in v1 (open design question on GPU->CPU readback latency; hint: render elements to a texture and sample it, or reduce to frame before passing to pixel)"`.
  - Reductions on values already coarse (`Frame` or `Graph`): Refused with `"cannot reduce 'x': value is already frame-domain"`.
  - Reductions crossing two levels (`Sample -> Graph`): Refused with `"reduce one level; graph is edit-time and has no per-frame value"`.

### 1.3 `resample`
- **Form:** `resample(x, D)` where `D` is a domain identifier (`frame`, `element`, `pixel`, `sample`).
- **Legality:**
  - `sample -> frame` (fine -> coarse): Takes most recent value.
  - `graph/frame -> element/pixel/sample` (coarse -> fine): Holds/broadcasts.
  - `D == domain(x)`: Identity.
  - `element/pixel -> frame`: Refused: `"resample is not defined from <domain> to frame; use reduce instead (e.g. reduce.mean(x) or reduce.rms(x))"`.
  - Incomparable crossings (`element <-> pixel <-> sample`): Refused: `"cannot resample between incomparable domains <d1> and <d2>; route through frame instead"`.

### 1.4 `downsample`
- **Form:** `downsample(x, k)`
- **Factor `k`:** Must be a compile-time constant integer literal >= 1. Non-constant expressions or params are refused:
  `"downsample factor k must be a compile-time constant integer literal >= 1 (got non-constant '<expr>')"`
- **Domain:** Retains `domain(x)`, records `divisor = k`.
- **Desugaring in Bytecode / Element & Pixel IR:**
  - For top-level statements `y = downsample(x, k)`:
    - Generates a synthetic hidden state cell `__ds_hold_<id>` with domain = `xIR->domain`, type = `xIR->type`.
    - Generates conditional gate `if (mod(timeVar, k) == 0)` where `timeVar` is `frame` (for Frame domain) or `i` (for Element domain).
    - Writes `x` to `__ds_hold_<id>` on gate match.
    - Yields `y = __ds_hold_<id>`.

### 1.5 `map`
- **Form:** `map { ... }` or `map(N) { ... }`
- **Legality:**
  - Count must be bounded and known at compile time.
  - Mapped body's domain must be finer than or equal to surrounding domain:
    `"mapped body domain must be finer than or equal to surrounding domain; use reduce for many-to-one aggregation"`.
  - `state` inside a `map` allocates one cell per element (N cells).

---

## 2. Incomparable Domain Error Reporting

`FieldError` contains a single `SourceSpan span`. To meet the requirement that incomparable domain errors name **both** spans and domains:
- The primary `span` is set to the first operand's span.
- The error message and hint explicitly format the line and column of both operands:
  `"incomparable domains: element (line X, col Y) and sample (line A, col B)"`
  `"hint: cannot mix element and sample domains directly; wrap in a transfer operator, e.g. reduce.rms(in, 20, 200) or route through frame"`

---

## 3. Cost Table Representation

The transfer cost table (§5.6 of the plan) is encoded as static data in `Transfer.h` / `Transfer.cpp`. Codegen and UI read from this single source of truth.

---

## 4. Real-time & Clean-room Safety

- No heap allocation in runtime loops.
- No new cross-thread channels (sample reduce uses existing `MeterRing`, parameter transport uses `ParamMailbox::SmoothedValue`).
- No GPL sources referenced.
