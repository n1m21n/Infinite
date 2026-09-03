# Field Build Step 9 — Sample Domain Design Notes

This document records the design decisions and scope boundaries for Field
Step 9: the `sample` domain, a register-machine kernel that runs once per
audio sample per voice on the real-time audio thread.

---

## 1. Architecture

- **Compiler** (`BackendRegister.cpp`, main thread only): a dedicated
  AST-to-bytecode lowering pass, not a reuse of `FieldIR.cpp`'s typed IR that
  the Element/Pixel backends share — the sample domain's target (branchless
  straight-line register bytecode, no runtime jumps) is fundamentally
  different from the GLSL-shaped IR those backends emit.
- **Bytecode**: fixed `SampleInstr{op, dst, a, b, c, imm}` — a flat register
  file (`kSampleMaxRegs = 128`), no operand stack, no runtime branches.
- **Interpreter** (`SampleRuntime.h`, header-only, audio thread): a single
  `switch` over `SampleOp`, real-time-safe (no allocation, no locks).
- **Two-object pair**: `FieldSampleNode` (main-thread `INode`, editor/compile)
  + `AudioFieldSampleNode` (audio-thread `AudioNode`, private to
  `FieldSampleNode.cpp` — never reachable from the compiler).

## 2. Branching (`if`/`else`)

No runtime jump instructions exist, so `if`/`else` compiles to an SSA-style
"phi via Select": each branch is compiled against an independent copy of the
pre-`if` scope, and for every name that existed before the `if` and diverges
between branches, a `Select(dst, condReg, thenReg, elseReg)` instruction
merges the two bindings. Names declared inside only one branch stay
branch-local (ordinary lexical scoping) — they are not merged.

## 3. Loops (`for`)

Compile-time-only: strict pattern match on `var = <literal>`,
`var < <literal>` / `var <= <literal>`, `var += 1` / `var = var + 1`, fully
unrolled at compile time up to `kSampleMaxUnroll = 64` (matches step 8's
`map` unroll-cap convention). A non-constant bound (e.g. `for (i=0; i<n; ...)`
where `n` is the reserved runtime sample-index value) is a compile error:
`"for-loop bounds must be compile-time integer constants in the sample
domain"`. This keeps the interpreter free of any unbounded loop.

## 4. State transplant (hot reload)

Per-voice `state` cells live inside `AudioFieldSampleNode`
(`mStateCur`/`mStateNext`, ping-ponged), not inside `SampleProgram` itself —
otherwise a retired program's free (via `SampleSlotT::DrainRetired`) would
use-after-free live state. Transplant by `(name, type)` match is resolved on
the **main thread** at compile time (`CompileSampleProgram`'s `previous`
argument), against `FieldSampleNode::mLastCompiled` — the main thread's own
retained copy of the last successfully compiled program. `Apply()` never
reads the live audio-thread program back across threads to do this.

## 5. Voice allocation & state reset

Every `NoteOn` — whether it lands on an idle voice or steals an active one —
resets that voice's `state` cells to their declared initial values. Unlike
`Envelope::ResetLevel()`'s deliberate non-reset-on-legato behavior, Field's
`state` cells have no such exception: a new note is always a fresh instance
of the kernel's own per-voice memory. No separate idle-vs-steal detection was
needed.

## 6. `reduce.rms(in, loHz, hiHz)`

`ReduceOps.h`'s `ReduceRmsBandLimited` is a whole-buffer batch function, not
a per-sample streaming filter — so the register machine does not attempt a
per-sample implementation at all. `reduce.rms` is restricted to the exact
statement form `reduce.rms(in, loHz, hiHz)`, with `in` literally the bare
identifier and `loHz`/`hiHz` compile-time constants. Execution happens once
per audio block, directly on the raw input `AudioBuffer`, in
`AudioFieldSampleNode::ProcessBlock`, published via the existing `MeterRing`
(no new cross-thread channel — same convention as step-08-notes.md §4).

## 7. Safety

- **NaN/Inf**: a once-per-block sweep (not per-sample — an unbounded-looking
  per-sample cost is real-time-hostile) over live voice state. On a hit: zero
  the block just rendered, reset all voice state to initial values, bump an
  atomic fault counter (`FieldSampleNode::FaultCount()`).
- **Output clamp**: ±4.0 per sample, matching the rest of the audio graph's
  headroom convention.
- **Denormals**: flushed on every `StoreState` write-back
  (`DspMath::FlushDenormal`).
- **Params**: a 128-param hard cap (`kMaxParams`, `ParamMailbox.h`) enforced
  as a compile error, not a runtime clamp.

## 8. Two param-index spaces (a documented bug class, avoided here)

`ParamTable`'s stable, persisted `id` and the sample domain's dense
`mailboxId` (0..127, recomputed fresh every successful compile, never
persisted) are different, unrelated numbers. `FieldSampleNode` keeps a
`mCompiledParams` copy of the just-compiled program's `SampleParamSlot` list
(preserving declaration-order `mailboxId`) and pushes each frame by looking
up the current value by **name** through `ParamTable::Find` — never by
assuming `ParamTable::Params()`'s iteration order lines up with mailbox
order (`ParamTable::Reconcile` is free to reorder/append relative to
declaration order).

## 9. Fixture coverage (`INFINITE_FIELDSAMPLETEST`)

Covered: basic compile + execution, state hot-reload transplant, param
mailbox smoothing, note-on voice state reset, 129-param compile-time
refusal, non-constant for-loop bound refusal, NaN poisoning
sweep/recovery/fault-count, `reduce.rms` → `MeterRing` publish, zero
allocation across 200 steady-state blocks (via `malloc_zone_statistics`,
macOS-only — no global `operator new`/`delete` override, since that would
change allocation behavior for the entire process, not just the test).

Deliberately deferred: true voice-stealing across more concurrent notes than
`kMaxVoices`, hot-reload across a state type change (no second scalar state
type exists yet to change to/from), and the full param-reordering scenario
(covered indirectly by `AUDIOPARAMSWEEPTEST`'s generic sweep instead).

`AUDIOPARAMSWEEPTEST` baselines two new blind spots for the "Field Sample"
node in `audio-param-sweep-expected.txt`: `maxVoices` (a structural
polyphony cap, not a per-sample DSP knob — same class as Drum Sequencer's
`steps`) and `field_nextParamId` (`ParamTable`'s internal persisted-id
bookkeeping counter, not a real param).
