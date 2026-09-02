# Field build step 9 — the `sample` domain (a register machine on the audio thread)

You are implementing **build step 9 of the Field language** in Infinite
(`/Users/namansoni/infinte`, C++17, ImGui + OpenGL, MIT licensed). This is a
self-contained brief; you have no prior context on Field and do not need any
beyond what is listed under "Files to read first".

Line numbers are from `src/` at the commit this was written against — re-grep the
symbol if a number has drifted. **The symbol is authoritative, not the number,
and the code is authoritative over any skill.** Every discrepancy already found
between the skills and the code is recorded inline below; if you find another,
record it in the same shape rather than silently following the skill.

**Prerequisite steps that must already be finished and merged before you start:**

| Step | What it delivered | How to confirm |
|---|---|---|
| 1 | `Expression.cpp` split into lexer → AST → typed IR → bytecode; `Expression::Evaluate` signature and its three call sites unchanged | the compiler files exist; `INFINITE_FIELDTEST` sections A–C pass |
| 2 | `rand`/`noise`/`sh` pure in `(t, seed)` | random-set baseline recorded in the fixture's comment |
| 3 | `vec2/3/4` + rank polymorphism | `INFINITE_FIELDTEST` section D passes |
| 5 | `param` → `ParamRef`, with a **stable-by-name** param identity | `INFINITE_FIELDPARAMTEST` passes; a binding survives inserting a `param` line above it |
| 6 | `state` cells, delay lowering, the cycle check, the `(name, type)` transplant rule | `INFINITE_FIELDTEST` section H passes |

Step 8 (transfer operators) **specified but deliberately did not build** the
sample half of `reduce`. Step 9 lands it. If step 8 is missing, `reduce.rms(in,
20, 200)` has no IR node to lower and you should stop and say so.

Step 4 (`element`) and step 7 (`pixel`) are **not** prerequisites — nothing in
the sample backend touches a mesh or a shader. See §9.

---

## 1. Invariants — restated verbatim, they override anything you infer

### 1.1 Clean room (hard rule, non-negotiable)

Infinite is **MIT**. **Never** open, read, grep, or reference GPL sources:
Kronos (`bitbucket.org/vnorilo/k3`), Cmajor, SuperCollider, or BespokeSynth
(also at `/Users/namansoni/BespokeSynth` on this machine — **do not open it**).
The *Kronos paper* (Norilo, "Kronos: A Declarative Metaprogramming Language for
Digital Signal Processing", Computer Music Journal 39:4, 2015) is a published
paper and may be cited freely; its code may not be read. Safe to read: Faust
(LGPL), ChucK (dual MIT/GPL), Houdini VEX documentation, TidalCycles docs and
papers.

This step is the one most likely to tempt a lookup at "how another DSP language
allocates its registers". Do not. Every design decision below is derivable from
Infinite's own code plus the cited papers.

### 1.2 No sigils. Bare names. Ever.

An earlier draft used a VEX-style `@`. The owner **removed it**. Plain ASCII,
no special characters, in code, docs, error messages and fixtures.

| Wrong | Right |
|---|---|
| `@P.y += bass * 2` | `P.y += bass * 2` |
| `@Cd = vec3(1,0,0)` | `Cd = vec3(1,0,0)` |
| `v@P` / `f@heat` | `P` / `heat` |
| `@in`, `@out`, `@z` | `in`, `out`, `z` |

### 1.3 Rate is inferred, never declared

There is **no** `@rate` keyword, **no** `krate` parameter, **no** domain
annotation on a binding. A kernel is a sample kernel because it mentions `in`,
`out`, `sr` or `n` — not because anything says so. There is no syntax to force
it and no syntax to prevent it.

### 1.4 The other invariants

1. **One primitive.** A kernel is a body of code run once per element of a
   domain. The sample domain has one element per sample per voice. Nothing else
   changes. A feature that cannot be explained that way does not go in.
2. **Steps 2–10 are additive.** Only step 1 touched existing code. If this diff
   edits `src/core/Expression.cpp`'s public API, any of its three call sites, or
   any existing node, something is wrong.
3. **The two-object rule** (`.claude/skills/new-audio-node/SKILL.md` §0.2). The
   `INode` on the main thread owns an `AudioNode` on the audio thread. **The
   compiler lives entirely in the `INode` half and is never reachable from
   `ProcessBlock`.**
4. **`CookIfNeeded` does no DSP and does not compile.** It drains meters, pushes
   dirty params, and drains retired programs. Budget < 5 µs.
5. **A failing compile changes nothing that is running.** The last working
   program keeps producing audio, its state cells intact, and the error text is
   surfaced. `FormulaNode::Apply()` (`src/nodes/FormulaNode.cpp:390`) is the
   reference shape, and `FormulaNode::CookIfNeeded`'s retry guard
   (`src/nodes/FormulaNode.cpp:415`) is the reference for not recompiling a
   broken program every frame.
6. **Two cross-thread channels for *values*, and no third.** `ParamMailbox`
   (main → audio, `src/audio/ParamMailbox.h`) and `MeterRing` (audio → main,
   `src/audio/MeterRing.h`). This step adds neither a new atomic nor a new
   queue. See the discrepancy note in §1.5 about `SampleSlotT`, which is a
   *lifetime* channel and already exists.
7. **Real-time safety, applied to generated code as much as to hand-written
   code.** No heap allocation after `PrepareToPlay`, no recursion, no unbounded
   loops, no strings/pointers/dynamic arrays/structs in the language surface,
   every value's size known at compile time, voice counts bounded and declared
   up front.

### 1.5 Discrepancies between the skills and the real code — the code wins

Record these in your commit message. They are already verified against `src/`.

| # | Skill says | Code says | Consequence for this step |
|---|---|---|---|
| D1 | `field-realtime` §1 row 10 and the brief cite `ParamMailbox.h:24` for `kMaxParams` | it is **`src/audio/ParamMailbox.h:23`** | cite `:23` |
| D2 | `field-compiler` §6.3 cites `ParamMailbox.h:39` for `SmoothedValue` | it is **`:40`**; `Push` is `:36`, `SetImmediate` is `:44`, `PrepareToPlay` is `:25` | cite the real lines |
| D3 | `field-realtime` §1 row 10: exceeding 128 params causes "silent truncation" | **`ParamMailbox::Push` bounds-checks and returns** (`src/audio/ParamMailbox.cpp:12–13`), and `SmoothedValue` returns `0.0f` out of range (`:19–20`) | the real failure is **worse than truncation**: the 129th knob silently does nothing *and* the kernel reads `0.0`, not the param's declared value. §5.5 |
| D4 | `field-integration` §3 cites `Modulation.h:29` for `ParamRef`, `:174` `ClearFrameParams`, `:180` `gParamRegisterOnly`, `:186` `KnownParam` | `struct ParamRef` is **`:28`**, `ClearFrameParams()` **`:167`**, `RegisterParam()` **`:168`**, `KnownParam()` **`:182`**, `Unbind` **`:124`**, `UnbindAllFor` **`:125`**, `using Key` **`:53`** | step-05's numbers are the correct ones |
| D5 | `field-language` §5 cites `ExprGlobals.h:44` for `IsValidName` | it is **`:45`** | cite `:45` |
| D6 | The brief §14 and `field-integration` §1 call `EquationNode` "the model for `sample`-domain compilation" | `EquationNode` **never runs its language on the audio thread**. Its AST is walked 1024 times on the *main* thread, FFT'd, and baked into a 10-level mip pyramid (`src/audio/dsp/EquationDsp.h:710` `BuildBankFromAst`); `ProcessBlock` does a table lookup (`EquationDsp.h:71` `SampleBank`) | EquationNode models **keeping the compiler off the audio thread** and **the swap protocol** — not how a per-sample kernel executes. It cannot express `in`, `state`, or feedback, so it is not the model for the register machine. §5.1 |
| D7 | The skills list only `ParamMailbox` and `MeterRing` as cross-thread channels | **`SampleSlotT<T>` (`src/audio/SampleSlot.h:77`) is a third, already in the tree**, used by Sampler, Granular, PaulStretch, DrumSequencer, WaveTerrain, ImageSpectralSynth and Equation | **reusing it is not adding a channel; hand-rolling a fourth is.** §5.8 |
| D8 | The skills do not mention it | `ParamMailbox`'s smoother time constant is **hard-coded to 5 ms** (`src/audio/ParamMailbox.cpp:7`, `SetTimeConstant(0.005f, sampleRate)`), not per-param | §5.5's mode-selector trap |
| D9 | The skills do not mention it | **three incompatible `x / 0` semantics already ship**: `Expression.cpp`'s `ParseTerm` *fails the whole evaluation* (`s.Fail("division by zero")`); `EquationDsp.h:185` returns **±1000.0**; GLSL yields a driver-defined `inf` | §5.10's open question |
| D10 | The skills do not mention it | FTZ/DAZ is already set **per audio callback** at `src/audio/AudioEngine.cpp:322` (x86_64 `_mm_setcsr(… \| 0x8040)`) and `:323–327` (aarch64 FPCR bit 24) | Field must **not** set it again. §5.10 |

---

## 2. Goal

Give Field a `sample` domain: a kernel that runs once per audio sample, per
voice, on the real-time audio thread, compiled on the **main** thread into a
fixed-size register-machine program and handed across without a lock, an
allocation, or an xrun. A one-pole lowpass written as four lines of Field must
produce the same response as a hand-written `DspMath` one-pole, survive being
edited while it is making sound, keep its filter state across that edit when the
cell kept its name and type, and reset it when it did not. When the user types a
129th `param`, the compile fails with a message naming `kMaxParams` and its file
— it never ships a knob that does nothing. Done means: zero allocations after
`PrepareToPlay`, zero xruns under `AUDIOTEARDOWNSWEEPTEST`, and a hot recompile
that is inaudible except where the program genuinely changed.

---

## 3. Files to read first, and why

### Skills — the authoritative contract, read in this order

| File | Why |
|---|---|
| `.claude/skills/field-realtime/SKILL.md` | **the primary source for this step.** §1's 12-row checklist is the diff gate; §3 is where allocation hides; §4 is the branching cost model; §5 is the budget table |
| `.claude/skills/field-language/SKILL.md` | §2 the domain table, §3 rate inference, §4 no sigils, §5 the sample domain's reserved words (`in` `out` `sr` `n`), §8 `state`, §13's worked one-pole, §14 rows 7/8/12 |
| `.claude/skills/field-compiler/SKILL.md` | §6.3 the register-machine backend; §3 the precision rule (`float` for sample, `double` for frame); §6.4 the retargetability rules the IR must keep; §7 the error/recovery model; §8 what the front end refuses |
| `.claude/skills/field-state/SKILL.md` | §3 the per-domain cost table (1 float per cell **per voice** in `sample`); §5 the reset rule; §7 the `(name, type)` transplant table — §5.9 below turns that into an audio-thread-safe form |
| `.claude/skills/field-domains/SKILL.md` | §3 `reduce`; the `frame → sample` crossing must arrive through the mailbox smoother, never around it |
| `.claude/skills/field-integration/SKILL.md` | §4 `ParamMailbox` as the only main→audio value path; §2 the `INode` contract; §5 save/load; §9 the wiring checklist |
| `.claude/skills/field-testing/SKILL.md` | §1 how fixtures work here; §5's `sample` conformance table; §6 row 9 is this step's exit row |
| `.claude/skills/new-audio-node/SKILL.md` | §0.2 the two-object rule, §0.3 the `CookIfNeeded` budget, §0.4 the audio-thread prohibitions, §0.5 minimalism, §3 the wiring sites |
| `.claude/skills/audio-node-ui/SKILL.md` and `.claude/skills/node-ui-pillars/SKILL.md` | **before** writing any `Draw*Body`. Auto-generated knob rows sit on the same row grid as every hand-written one |
| `docs/plans/field/step-08-transfer-operators.md` | §5.2's "where a sample→frame reduce actually runs" is the design you are landing; §5.6's cost table row for `sample → frame` |
| `docs/plans/field/step-05-param-declarations.md` | §5.3's five `ParamRef` rules and §5.7's 128 ceiling — step 9 adds the *second* index space on top |

### Real source — the code wins over any skill

| File | Lines that matter | Why |
|---|---|---|
| `src/audio/ParamMailbox.h` | `kMaxParams = 128` **`:23`**; `PrepareToPlay` `:25`; `Push` `:36` (**main only**); `SmoothedValue` `:40` (**audio only**, advances the smoother by one sample); `SetImmediate` `:44`; the header comment `:11–20` | the only main→audio value path, and its comment records the ring-buffer bug that is why it is a flat atomic array |
| `src/audio/ParamMailbox.cpp` | `:7` the hard-coded 5 ms time constant; `:12–13` `Push`'s bounds check; `:19–20` `SmoothedValue`'s out-of-range `0.0f` | D3 and D8 — the real 129th-param failure mode |
| **`src/audio/SampleSlot.h`** | `SampleSlotT<T>` `:77`; `Push` `:85` (main); `SwapIn` `:96` (audio, top of block, returns `true` on a swap); `Active` `:108`; `DrainRetired` `:111` (main, once per `CookIfNeeded`); `BufferRetireRing::kCapacity = 8` `:44`; the drop-rather-than-overwrite comment `:36–39`; the whole class comment `:8–30` | **the compile-swap protocol, already written, already generic, already MIT.** Do not hand-roll a second one |
| `src/nodes/EquationNode.h` | `kMaxVoices = 8` `:20`, `kMaxUnison = 8` `:21`; `CompileEquation()` `:72`; `LastError()` `:73`; `mAst` `:129`; the `mLast*` dirty-tracking block `:133–139`; `RebuildBank()` `:141` | the precedent for a compiled-from-text audio node — and read it to see what it does **not** do (D6) |
| `src/audio/dsp/EquationDsp.h` | **this is where EquationNode's language actually lives**: `TokenType` `:86`, `Token` `:111`, `AstNodePtr = shared_ptr<AstNode>` `:119`, `AstType` `:121`, `VarId` `:130`, `AstNode::Evaluate(x,a,b,c,d,t)` `:144`, `class Parser` `:278`; `BuildBankFromAst` `:710`; the NaN/Inf guard `:743`; the ±100 clamp `:745`; the `/`-by-zero sentinel `:185` | the fifth mini-language Field replaces. Its `shared_ptr` AST is fine **because it lives on the main thread only** (`field-realtime` §3) |
| `src/nodes/EquationNode.cpp` | `class AudioEquationNode` `:95`; ctor pushes an initial bank and `SwapIn`s it `:104–105`; the main-thread push `:134`; `DrainRetired` `:139`; the audio-thread `SwapIn` + `Active()` `:208–209`; `SampleSlotT<EquationBank> mBankSlot` `:530`; `AllocateVoice` `:177` | the working end-to-end example of the swap protocol on a compiled-from-text node |
| `src/audio/AudioNode.h` | the class comment `:6–21` (the audio-thread prohibition list, and the no-aliasing buffer contract); `PrepareToPlay` `:26`; `ProcessBlock` `:28`; `Reset()` `:43`; `LatencySamples()` `:53` | the base class the sample backend derives from |
| `src/audio/AudioVoice.h` | `Envelope` `:13`; **`Envelope::ResetLevel()` and its comment `:31–39`** — why a *stolen* voice resets and a retrigger does not; `VoiceAllocator` `:123`; `NoteOn` `:131–133` ("steals the oldest active voice if every voice is already busy") | the voice-overflow policy, already written and already documented. §5.7 |
| `src/audio/AudioVoice.cpp` | `NoteOn` `:20–58`: round-robin first-free, then steal by lowest `age` | the exact algorithm to reuse, not reinvent |
| `src/audio/DspMath.h` | `OnePole` `:18` and its comment `:13–17` ("the exact formula `ParamMailbox::SmoothedValue` used inline before this header existed"); **`FlushDenormal` `:64`** (`fabs(x) < 1e-20f ? 0 : x`) and its comment `:61–63`; `FastTanh` `:75` | the primitives to reach for before writing any DSP; the denormal guard for `state` write-back |
| `src/audio/AudioEngine.cpp` | **`:321–328`** — FTZ/DAZ set per callback on both architectures; `:337–338` the xrun counter | D10: do not set FTZ again; and this is the counter the exit criterion reads |
| `src/audio/AudioEngine.h` | `kAudioMaxBlockFrames = 4096` `:24` | the register file and any per-block scratch are sized against this |
| `src/audio/AudioBuffer.h` | the whole file (11 lines) — planar, non-owning, `channels[ch][0..numFrames)` | the output shape |
| `src/audio/MeterRing.h` | `kCapacity = 4096` `:13`; `Write` `:16` (audio); `ReadLatest` `:25` (main) | how a `sample → frame` `reduce` publishes. **`ReadLatest`, never `Read`** — step-08 §6 trap 4 |
| `src/audio/dsp/ReverbKernel.h` | `:139` `buf[pos] = FlushDenormal(x + g * y)` | the shipped pattern for flushing a recursive feedback cell |
| `src/audio/dsp/MetallicResonator.h` | `:732` `if (!std::isfinite(vL) \|\| !std::isfinite(vR))` | the shipped pattern for a NaN trap on the audio thread |
| `src/core/Modulation.h` | `using Key = std::pair<int,int>` `:53`; `struct ParamRef` `:28`; `ClearFrameParams()` `:167`; `RegisterParam()` `:168`; `KnownParam()` `:182`; `Unbind` `:124`; `UnbindAllFor` `:125` | step 5 built on this; step 9 must not conflate its `paramIndex` with a mailbox `paramId` (§5.6) |
| `src/main.cpp` | `RebuildAudioTopology()` `:25118`; the `RequiresAudioProcessing()` walk `:25193`; the `PrepareToPlay(sampleRate, kAudioMaxBlockFrames)` loop `:25270–25275`; `SpawnNode` `:4967` | when your `AudioNode` is prepared, and with what |
| `src/core/INode.h` | `ParamVisitor` `:80` (exactly `Float`/`Int`/`Bool`/`Text`/`Color` — no `Vec3`, no blob); `CookIfNeeded` `:123`; `VisitParams` `:159`; `RequiresAudioProcessing()` `:200` | save/load and audio-participation |
| `src/nodes/FormulaNode.cpp` | `Apply()` `:390`; the retry guard `:415` | invariant 1.4.5's reference shape |
| `src/core/Expression.cpp` | `ParseTerm`'s `/` and `%` branches (`s.Fail("division by zero")`, `fmod`) | D9 — the frame domain's division semantics, which the sample domain cannot copy |

---

## 4. Files to create / modify

### Create

| Path | Contents |
|---|---|
| `src/core/field/SampleProgram.h` | the POD program object the audio thread reads: fixed instruction array, register counts, state-cell count and initial values, the transplant map, the param-id map, `maxVoices`. **No `std::string`, no `std::vector`, no virtual, no owning pointer.** See §5.3 |
| `src/core/field/BackendRegister.h` / `.cpp` | lowering from the typed IR to a `SampleProgram`: register allocation, instruction selection, the compile-time refusals of §5.5/§5.7. Main thread only |
| `src/core/field/SampleRuntime.h` | the interpreter loop over a `SampleProgram` — a header-only, allocation-free `Run(...)`, callable from `ProcessBlock`. **No compiler symbols reachable from here** |
| `src/nodes/FieldSampleNode.h` / `.cpp` | the node: the `INode` half (editor, params, save/load, compile, `SampleSlotT` push, `DrainRetired`) and the `AudioNode` half (`SwapIn`, the block loop, voices, NaN trap) — the two-object rule |
| `docs/plans/field/step-09-notes.md` | *(optional)* measurements, the allocation-counter run, and the owner's answers to the open questions below |

### Modify — Field's own files only

| Path | Change |
|---|---|
| `src/core/field/Ir.h` | nothing new **unless** a genuinely missing fact is needed. No `SampleXxx` node types (`field-compiler` §6.4) |
| `src/core/field/Infer.cpp` | seed `in`, `out`, `sr`, `n` as `sample`-domain reserved names; land step 8's `reduce(sample) → frame` |
| `src/core/field/ParamTable.*` (step 5) | add the **mailbox `paramId`** column alongside the existing stable `paramIndex`. Two index spaces, one table (§5.6) |
| `CMakeLists.txt` | `src/core/field/BackendRegister.cpp` and `src/nodes/FieldSampleNode.cpp` into `COMMON_SOURCES`. Field/core sources sit around `:208–227`, audio sources around `:228`, node sources around `:309` |
| `src/main.cpp` | **only** the four generic wiring sites: the include (~`:92`), `REGISTER_NODE` in `RegisterNodes()` (~`:3847` neighbourhood), the node-help table (~`:7780`), the body-draw dispatch. Nothing else |
| `.claude/skills/run-infinite-hygiene/driver.sh` | add `"FIELDSAMPLETEST:10"` to `FULL_TESTS` (the array begins at **`:173`**) and to the audio group array (begins at `:94`) |

### Must not be modified

- `src/core/Expression.h` / `.cpp` public API, and its three `Evaluate` call
  sites: `src/main.cpp:37506`, `src/core/ExprGlobals.cpp:72`,
  `src/nodes/AnalyzeNodes.cpp:179`.
- `src/audio/ParamMailbox.h` / `.cpp`. If 128 is genuinely not enough, that is
  an owner conversation, not a diff.
- `src/audio/SampleSlot.h`, `src/audio/MeterRing.h`, `src/audio/AudioNode.h`,
  `src/audio/AudioVoice.h`, `src/audio/DspMath.h`, `src/audio/AudioEngine.*`.
- `src/core/Modulation.h`, `src/core/INode.h`, `src/core/Patch.h` / `.cpp`.
- Any existing node, including `EquationNode` and `FormulaNode`. Read them;
  do not touch them.

---

## 5. Procedure, phased

### 5.1 Phase 0 — read `EquationNode` for what it *is not*

Do this before writing a line, because the brief will otherwise mislead you.

`EquationNode` is described everywhere as "the model for `sample`-domain
compilation". What it actually does:

```
   main thread                                          audio thread
   ───────────                                          ────────────
   formula text
      │  EquationDsp::Parser::Parse          (:281)
      ▼
   shared_ptr<AstNode>                        (:119)
      │  BuildBankFromAst                     (:710)
      │    1024 × AstNode::Evaluate           (:144)   ← the language runs HERE
      │    forward FFT, 10 harmonic ceilings, inverse
      ▼
   EquationBank: 10 × 1024 floats             (:26)
      │  mBankSlot.Push(new EquationBank(…))  (EquationNode.cpp:134)
      └──────────────────────────────────────▶ mBankSlot.SwapIn()  (:208)
                                               SampleBank(bank, φ, dφ)  (:71)
                                                 ← a table lookup, no AST
```

| What EquationNode models, and Field should copy | What it does **not** model |
|---|---|
| the compiler runs on the main thread, once, on a dirty check | a per-sample kernel — the AST is never walked at 48 kHz |
| the result is a fixed-size POD handed over by `SampleSlotT` | `in` — the bank is generated, not a filter of an input |
| `DrainRetired()` on the main thread, in the cook | `state` — a wavetable has no feedback and no memory between samples |
| a NaN/Inf guard and a hard clamp at bake time (`EquationDsp.h:743`, `:745`) | anything with a cycle, which is exactly what `state` exists for |
| voice allocation and stealing (`EquationNode.cpp:177`) | `param`s reaching the kernel — the bank is baked with `a,b,c,d` frozen in |

**Write that finding into your commit message.** The register machine is a new
execution model, not a refactor of an existing one. Its *lifetime* and *swap*
machinery is a straight reuse; its *execution* is not.

### 5.2 Phase 1 — the no-allocation, no-locks checklist, before any code

`field-realtime` §1 is 12 rows and §3 lists where allocation hides. Both apply
in full and are not restated here. What follows is the **audio-thread-specific
expansion** you must run against your own diff, because C++ allocates
invisibly and the sample backend is the only place in Field where it is fatal.

| # | Shape | Why it allocates or blocks | Safe replacement in this step |
|---|---|---|---|
| 1 | `std::function<…>` anywhere in `SampleRuntime.h` or below `ProcessBlock` | type erasure heap-allocates for any capture larger than the small-buffer optimisation, and the size is not guaranteed | a plain `switch` on `Instr::op`, or a `constexpr` function-pointer table with **no** capture |
| 2 | `std::string` — including a temporary built for an error path | SSO covers ~15 chars on libc++ and nothing longer; a "this can't happen" log line allocates on the one run where it does | keep every name in the `INode` half. The `SampleProgram` carries **no names at all** (§5.3) |
| 3 | `std::vector::push_back` / `resize` / `reserve` in the block loop | any growth reallocates | fixed C arrays sized from compile-time constants; `PrepareToPlay` is the only place a size is chosen |
| 4 | copying a `std::shared_ptr` | an atomic refcount increment — not an allocation, but a contended atomic RMW on the audio thread, and the *destructor* can free | `SampleSlotT` hands over a raw `T*` precisely to avoid this (`SampleSlot.h:8–30`). `EquationDsp`'s `shared_ptr` AST is fine **only because it never crosses to the audio thread** |
| 5 | `std::map` / `std::unordered_map` lookup with `operator[]` | inserts on a miss, which allocates a node | a flat `int` array indexed by a small dense id — which is exactly what the `paramId` space in §5.6 is for |
| 6 | a lambda capturing a container **by value** | copies the container, which allocates | capture by reference into a stack frame that outlives the call, or do not use a lambda |
| 7 | `std::to_string`, `snprintf` into a `std::string`, `operator+` on strings | allocates, in the log line you added while debugging and forgot | there is no logging on the audio thread. Publish a `std::atomic<int>` error code and let the main thread render the text |
| 8 | virtual dispatch through a type-erased handle | not an allocation, but an indirect branch per call, which is precisely the mispredict cost the register machine exists to avoid (Norilo p.31) | `SampleProgram` is a POD read through a raw pointer. `AudioNode::ProcessBlock` is the **only** virtual call in the path, once per block |
| 9 | `dynamic_cast` | RTTI walk, and unbounded | forbidden by `AudioNode.h:6–21`. Resolve types on the main thread |
| 10 | `new` / `delete` — including the `delete` inside a retire | `SampleSlotT::SwapIn` retires into a ring **instead of** deleting, for exactly this reason (`SampleSlot.h:92–95`) | `DrainRetired()` on the main thread. Never `delete` a program from `ProcessBlock` |
| 11 | any lock, including a `try_lock` "that never contends" | priority inversion against a lower-priority main thread | the atomic exchange in `SampleSlotT::Push`/`SwapIn` |
| 12 | `printf` / `fprintf` / file I/O / GL / ImGui | syscalls with unbounded latency | `AudioNode.h:6–21` lists all of these. Field gets no exemption |

**How to check it rather than assert it.** §7's exit criterion runs the diff
through a grep for every left-column shape and runs the fixture under an
allocation counter. Assertion is not evidence.

### 5.3 Phase 2 — the register machine

#### 5.3.1 Why a register machine and not the frame-domain bytecode VM

Step 1 built a bytecode VM for `frame`. It is the wrong shape here, and the
reason is a rate argument, not a taste argument.

| | `frame` bytecode VM (step 1) | `sample` register machine (this step) |
|---|---|---|
| Invocations/sec | 60 | 48 000 × voices — up to 384 000 for 8 voices, per node |
| Dispatch cost | invisible | **the dominant cost.** Ertl & Gregg report 50–98 % branch misprediction for typical interpreters; on Sandy Bridge a miss is ~18 cycles, in which the chip could have retired **144 floating-point ops** (Norilo p.31) |
| Accumulator width | `double` — the step-1 saved-patch corpus depends on it bit-for-bit (`field-compiler` §3) | `float`. No corpus depends on it, and `float` halves the register file and doubles the SIMD width available later |
| Operand access | a stack machine is acceptable | **a flat register file.** Every operand index is resolved at compile time; there is no push/pop traffic and no stack-depth bookkeeping per instruction |
| Allocation | the VM may allocate at build time on the main thread | none, ever, after `PrepareToPlay` |
| Loop structure | one invocation, no loop | block → sample → voice, with the program pointer, param fetch and all `graph`-domain constants hoisted out of all three |

Both backends lower **the same typed IR**. This is not a second compiler and it
must not become one — `field-compiler` §6.4's retargetability rules still hold:
no `SampleXxx` node type enters the IR, and every intrinsic keeps one semantic
definition across backends.

#### 5.3.2 Instruction encoding and the register file

Fixed-width instructions, no operand stack, all indices resolved at compile
time:

```cpp
struct Instr {
   uint8_t  op;    // an enum; one switch case each
   uint8_t  dst;   // register index
   uint8_t  a;     // register index, or an immediate slot index
   uint8_t  b;     // register index, or unused
   float    imm;   // folded constant, or a bounded loop's trip count
};
```

| Decision | Value | Why, and what breaks if you exceed it |
|---|---|---|
| register index width | 8 bits → **256 registers max** | keeps `Instr` at 8 bytes so a whole program stays in L1. Exceeding it is a **compile error** naming the program's register count and the limit — `field-realtime` §1 rule 5 (every size known at compile time) with a different face. Never widen silently |
| instruction array | a fixed `Instr code[kMaxInstr]` inside the program | no `std::vector` on a struct the audio thread reads. Pick `kMaxInstr` (4096 is generous for a kernel a human types) and make overflow a compile error |
| register file storage | `float mRegs[kMaxRegs]` **in the `AudioNode`**, not in the program | the program is shared, immutable code; registers are per-invocation scratch |
| state cells | `float mState[2][kMaxStateCells * kMaxVoices]` in the `AudioNode`, ping-ponged on a swap | §5.9 — the transplant needs the *old* values to survive the swap, so they cannot live in the retired program |
| param scratch | `float mParamNow[ParamMailbox::kMaxParams]` in the `AudioNode` | 512 B, refreshed once per sample (§5.5) |

The `SampleProgram` itself is a POD with **no names, no strings, no owning
pointers, and no virtuals**:

| Field | Type | Note |
|---|---|---|
| `codeLen`, `code[kMaxInstr]` | `int`, `Instr[]` | the program |
| `numRegs` | `int` | ≤ 256, checked at compile time |
| `numStateCells` | `int` | per voice |
| `stateInit[kMaxStateCells]` | `float[]` | the declared initial values, for reset and for a non-transplanted cell |
| `transplantFrom[kMaxStateCells]` | `int16_t[]` | index into the *previous* program's cell array, or `-1`. Computed on the main thread by name+type; **no name reaches the audio thread** (§5.9) |
| `paramReg[ParamMailbox::kMaxParams]` | `int16_t[]` | mailbox `paramId` → register index, or `-1` |
| `numParams` | `int` | ≤ 128, checked at compile time (§5.5) |
| `maxVoices` | `int` | a compile-time constant (§5.7) |
| `inReg`, `outReg`, `srReg`, `nReg` | `int` | the reserved sample-domain names |

#### 5.3.3 The block loop, and the one ordering decision that matters

```
ProcessBlock(inputs, numInputs, output):

  1.  swapped = mSlot.SwapIn()                     // top of block, never mid-block
  2.  p = mSlot.Active()
      if (p == nullptr) { fill output with silence; return; }
  3.  if (swapped) { transplant state (§5.9); start a 64-sample output ramp }
  4.  drain the note inbox → VoiceAllocator (§5.7)
  5.  for (n = 0; n < numFrames; n++)              // SAMPLE-MAJOR
        5a. for each declared param i:
              mParamNow[i] = mMailbox.SmoothedValue(i)      // EXACTLY ONCE per sample
        5b. acc = 0
            for (v = 0; v < p->maxVoices; v++)     // VOICE-INNER
              if (!allocator.IsVoiceActive(v)) continue
              load p->paramReg[…] ← mParamNow[…]
              regs[p->inReg] = input ? input[n] : 0.0f
              regs[p->srReg] = (float)mSampleRate
              regs[p->nReg]  = (float)mSampleCounter
              load this voice's state cells into their registers
              run p->code[0 .. codeLen)            // straight-line, one switch
              store this voice's state cells back, each through FlushDenormal
              acc += regs[p->outReg]
        5c. output[ch][n] = clamp(acc * rampGain, -kOutClamp, +kOutClamp)
  6.  NaN sweep over the block (§5.10)
  7.  publish any `reduce` results through MeterRing (§5.11)
```

**Why sample-major and voice-inner, and not the other way round.**
`ParamMailbox::SmoothedValue(paramId)` **advances the one-pole by one sample**
(`src/audio/ParamMailbox.h:38–40`, `src/audio/ParamMailbox.cpp:22`). Calling it
inside the voice loop advances it once per voice per sample:

| Layout | `SmoothedValue` calls per sample | Effect on a 5 ms ramp (`ParamMailbox.cpp:7`) |
|---|---|---|
| voice-major, param fetch inside | V | the ramp finishes in 5/V ms — **and its length changes with how many notes are held.** A knob move sounds different for one note than for eight |
| **sample-major, param fetch once, hoisted above the voice loop** | 1 | correct, and voice-count-independent |

The cost of sample-major is that a voice's state cells are re-touched every
sample rather than staying hot for a whole block. At V ≤ 16 and a few dozen
cells that is a few kilobytes cycling in L1 — measure it, but do not trade
correctness for it. If it ever does matter, the fix is to precompute the block's
smoothed param values into a bounded `float[numParams][numFrames]` scratch
allocated at `PrepareToPlay` — **not** to move the fetch into the voice loop.

| Wrong | Right |
|---|---|
| `mMailbox.SmoothedValue(i)` inside the voice loop | fetch once per sample into `mParamNow[]`, above the voice loop |
| `mMailbox.SmoothedValue(i)` once per **block** | zipper noise — a 60–190 Hz staircase on every knob move |
| reading the `INode`'s `float` param field directly from `ProcessBlock` | `SmoothedValue(paramId)` (`field-integration` §4) |
| `mSlot.SwapIn()` in the middle of the sample loop | top of the block only (`SampleSlot.h:92–95`) |

#### 5.3.4 Branching, loops, and what the front end refuses here

`field-realtime` §4's cost model applies unchanged: in `sample` a branch is a
**real** branch, and a mispredict is ~18 cycles / 144 foregone FLOPs. The
backend does not predicate — that is the pixel backend's lowering, and doing it
here would double the cost of every `if` for no benefit.

| Refuse at compile time | Message must say |
|---|---|
| a loop whose bound is not a compile-time constant | which expression was not constant |
| a loop bound above a stated unroll/iteration ceiling | the bound and the ceiling |
| recursion, direct or mutual | the cycle of names |
| a dataflow cycle with no `state` on it | the cycle, node by node with spans (`field-state` §2) |
| more than 128 `param`s | §5.5 |
| more than 256 registers after allocation | the count and the limit |
| a local shadowing `in`, `out`, `sr` or `n` | that the name is reserved in the **`sample`** domain |

### 5.4 Phase 3 — where the compile happens, and where it does not

```
   trace every caller of the compile entry point. If any path reaches
   AudioNode::ProcessBlock, the design is wrong, not the call.
```

| Runs on | What |
|---|---|
| **main**, on a debounced source change | lex, parse, infer, allocate registers, emit `SampleProgram`, compute the transplant map, `mSlot.Push(...)` |
| **main**, every `CookIfNeeded` | `mSlot.DrainRetired()`, drain `MeterRing` with `ReadLatest`, `mMailbox.Push(paramId, value)` for dirty params, read the atomic error code and render its text. **< 5 µs** |
| **audio**, top of `ProcessBlock` | `mSlot.SwapIn()`, transplant on `true` |
| **audio**, per sample | `SmoothedValue`, the register loop, `FlushDenormal` on state write-back |
| **audio**, end of block | the NaN sweep, `MeterRing::Write` for any `reduce` |

**Debouncing is not a nicety.** §5.8's retire ring holds 8 entries and drops
(leaks) beyond that. Recompiling on every keystroke can push faster than one
adoption per block plus one drain per frame. Recompile on a settle timer or on
editor blur, and follow `FormulaNode::CookIfNeeded`'s retry guard
(`src/nodes/FormulaNode.cpp:415`): **a program that failed to compile is not
recompiled until the source changes again.**

### 5.5 Phase 4 — `param`, `ParamMailbox`, and the 128 hard cap

`ParamMailbox` is the **only** main→audio value path. Not a second atomic, not
a second queue. The header's own comment (`src/audio/ParamMailbox.h:11–20`)
records why: an earlier ring-buffer version had the producer's overrun-drop path
writing the consumer-owned head index, which broke the single-consumer
invariant under real concurrent load.

`kMaxParams = 128` (**`src/audio/ParamMailbox.h:23`** — the skills say `:24`;
D1) is a **hard cap on the number of `param` declarations in a sample-domain
Field body.**

#### The real failure mode of the 129th param (correcting `field-realtime` §1 row 10)

`field-realtime` calls it "silent truncation". The code is more specific and
worse:

```cpp
// src/audio/ParamMailbox.cpp:10-15
void ParamMailbox::Push(int paramId, float value)
{
   if (paramId < 0 || paramId >= kMaxParams)
      return;                                   // <-- the write vanishes
   mTarget[paramId].store(value, std::memory_order_release);
}

// src/audio/ParamMailbox.cpp:17-23
float ParamMailbox::SmoothedValue(int paramId)
{
   if (paramId < 0 || paramId >= kMaxParams)
      return 0.0f;                              // <-- not the declared default
   …
}
```

So with a 129th param and no compile-time check:

| | What the user sees |
|---|---|
| the knob | draws, moves, saves, loads, accepts a modulation cable |
| `Push` | discards every write |
| the kernel | reads **`0.0f`**, not the declared initial value, not the last value, not the min |
| the diagnosis | a `param float mix = 1.0 [0, 1]` that behaves as if it were `0` — silently, forever, with a knob showing `1.0` |

**Therefore:** exceeding 128 is a **compile error** raised in the front end,
where the declarations are counted, before any backend runs. The message must
name the count, the limit, `kMaxParams`, and its file:

> `132 params declared; the sample domain allows at most 128 (ParamMailbox::kMaxParams, src/audio/ParamMailbox.h:23)`

Never truncate. Never clamp. Never "just use the first 128".

#### Two more mailbox facts the skills do not carry

| Fact | Consequence |
|---|---|
| **The smoother time constant is hard-coded to 5 ms** (`src/audio/ParamMailbox.cpp:7`) and is not per-param (D8) | a Field `param` used as a discrete mode selector glides through every intermediate value for 5 ms on each change. v1 params are float-only (step 5), so if a kernel branches on one, it must quantize **after** the smoother — or the branch flickers through every mode for 5 ms. Document this in the node's help text |
| `SetImmediate` is **audio-thread only** (`ParamMailbox.h:42–44`) | use it once, from `PrepareToPlay` or the first block, to seed each slot at its declared initial value. Otherwise the first 5 ms of audio ramps from 0 to the param's real value on every device start |

### 5.6 Phase 5 — the `paramIndex` ordinal trap, and the second index space step 9 adds

Step 5 already settled that a Field `param`'s `paramIndex` — the one that is
half of `Modulation::Key` (`src/core/Modulation.h:53`, `using Key =
std::pair<int,int>`) — **must be stable by name**, not the declaration ordinal.
Restated because this step must not undo it:

| Option for `paramIndex` | Effect of inserting `param float a` at the **top** of a body |
|---|---|
| (a) a stable hash of the param's name | every existing binding stays on its own param |
| (b) a monotonically increasing per-node counter, allocated on first sight of a name, never reused, persisted in the patch | same |
| **(c) declaration order** | **every modulation cable on the node silently re-points one param to the left.** Not acceptable |

This is the same bug class `node-ui-pillars` documents as **P7** for filter-mode
indices: saved patches store an *integer index* into a name list, so reordering
the list silently rewrites the meaning of every saved patch. Field `param`s must
not repeat it.

**What step 9 adds, and gets wrong if you are not careful.** The mailbox has its
*own* index space:

| Index | Range | Owner | Stable across an edit? | Used for |
|---|---|---|---|---|
| `paramIndex` | arbitrary (a hash, or a never-reused counter) | `Modulation`, `src/core/Modulation.h:53` | **yes, by name** | the modulation binding key, the `mod` and `expr` patch lines |
| `paramId` | **dense `0 .. 127`** | `ParamMailbox`, `src/audio/ParamMailbox.h:23` | **no** — it is a slot number | the mailbox array index and `SampleProgram::paramReg[]` |

| Wrong | Right | The bug |
|---|---|---|
| passing `paramIndex` to `mMailbox.Push()` | pass the `paramId` from the ParamTable's mapping | under option (a), a name hash is far outside `0..127`, so `Push`'s bounds check discards **every** param write and the whole kernel reads zeros |
| deriving `paramId` from declaration order and treating it as an identity | derive `paramId` fresh on every successful compile; carry identity in `paramIndex` alone | a reordering edit re-points the *mailbox slots*, and every held value lands on the wrong param for one block |
| persisting `paramId` in the patch | persist only the name-keyed values and the `paramIndex` scheme step 5 chose | a `paramId` is a runtime slot, not a saved fact |

**The rule.** One table, two columns. `ParamTable` owns
`name → { paramIndex (stable), paramId (dense, recomputed each compile) }`.
`SampleProgram::paramReg[paramId]` is what codegen writes. The mapping is
rebuilt on every **successful** compile and never on a failed one.

### 5.7 Phase 6 — voice counts bounded and declared, and a stated overflow policy

`field-realtime` §1 rule 6: element and voice counts are bounded and declared up
front. Kronos accepts the same constraint for polyphony (Norilo p.46).

| Question | Answer |
|---|---|
| Where does the bound live? | a **compile-time constant on the node**, mirrored into `SampleProgram::maxVoices`. Not a param, not a runtime value, not "however many notes arrive" |
| What value? | match the shipped precedent: `EquationNode::kMaxVoices = 8` (`src/nodes/EquationNode.h:20`). 8 unless the owner says otherwise |
| Why must it be compile-time? | `field-realtime` §1 rule 5. The state array is `numStateCells × maxVoices` floats allocated once at `PrepareToPlay`; a runtime voice count means a runtime allocation |
| What if a `param` tries to set it? | compile error naming the declaration. A voice count read from a param is rule 6 with a different face |

**The overflow policy, stated rather than emergent.** Reuse `VoiceAllocator`
(`src/audio/AudioVoice.h:123`) — do not write a ninth voice allocator. Its
`NoteOn` (`src/audio/AudioVoice.cpp:20–58`) is:

1. round-robin from a cursor, preferring a voice whose envelope is **inactive**;
2. if every voice is busy, **steal the one with the lowest `age`** — the oldest
   active note;
3. advance the cursor past the chosen voice.

`AudioVoice.h`'s own comment (`:131–133`) documents step 2 as the contract, so
this is a documented policy, not an accident of the loop.

**Field's one addition, and it matters:**

> **On a steal, every `state` cell belonging to that voice is reset to its
> declared initial value.**

`Envelope::ResetLevel()`'s comment (`src/audio/AudioVoice.h:31–39`) is the
precedent and the reason: a retrigger that keeps its level is what makes legato
click-free, but a *stolen* voice is "a voice whose oscillator phase and filter
state are about to be reset out from under a still-loud level". A Field `state`
cell is exactly that filter state. Carry it over and the new note inherits the
stolen note's integrator — at best a click, at worst a self-oscillating filter
that never settles.

| Event | Voice's `state` cells |
|---|---|
| note on, into an **idle** voice | reset to initial values |
| note on, **stealing** an active voice | reset to initial values, and `ResetLevel()` on the envelope |
| note off / release | unchanged — the tail needs them |
| same-note retrigger into the same voice | **unchanged** — this is what makes legato click-free |
| transport seek / loop / stop | reset (`field-state` §5, one rule, no per-node exceptions) |
| a compile swap | **transplanted, not reset** — §5.9 |

### 5.8 Phase 7 — the compile swap, with no glitch and no xrun

**Do not hand-roll this.** `SampleSlotT<T>` (`src/audio/SampleSlot.h:77`) is
already in the tree, already generic over the payload type, already MIT, and
already used by Sampler, Granular, PaulStretch, DrumSequencer, WaveTerrain,
ImageSpectralSynth and Equation. Its header comment (`:8–30`) explains that it
exists precisely so the same use-after-free trap is not reimplemented per node.

**Discrepancy D7:** the Field skills name only `ParamMailbox` and `MeterRing` as
cross-thread channels. `SampleSlotT` is a third, and it is a *lifetime* channel
rather than a value channel. **Reusing it satisfies invariant 1.4.6; writing a
fourth would violate it.** Say so in the commit message so the next reader does
not "fix" it.

The protocol, exactly:

| # | Thread | Call | Contract |
|---|---|---|---|
| 1 | main | build a `SampleProgram*` with `new` | compilation, register allocation, the transplant map — all here |
| 2 | main | `mSlot.Push(program)` | `SampleSlot.h:85–90`. If a *previous pending* program was never adopted, `Push` **deletes it here, on the main thread** — the audio thread never saw it, so nothing can be racing it |
| 3 | audio | `mSlot.SwapIn()` at the **top of `ProcessBlock`, never mid-block** | `SampleSlot.h:96–105`. Adopts the pending program; retires the previously active one into the SPSC ring **instead of deleting it**; returns `true` when a swap happened |
| 4 | audio | `mSlot.Active()` | `:108`. Valid for the whole block |
| 5 | main | `mSlot.DrainRetired()` once per `CookIfNeeded` | `:111–115`. **This is the only place a program is freed** |

```
   main thread                      audio thread
   ───────────                      ────────────
   compile ──▶ new SampleProgram
                     │
                     │  Push()           (atomic exchange, SampleSlot.h:87)
                     ▼
              [ mPendingBuffer ] ──────▶ SwapIn()   top of ProcessBlock
                                            │
                                     old ───┘──▶ [ BufferRetireRing, cap 8 ]
                                            │
                                        mActiveBuffer  (read all block)
                                            │
   DrainRetired() ◀─────────────────────────┘
        │
        └──▶ delete   ← the ONLY free, and it is on the MAIN thread
```

| Question the task asks | Answer |
|---|---|
| Who owns the old program? | the audio thread holds the pointer for the duration of one block; ownership transfers to the retire ring inside `SwapIn`; the main thread takes it back in `DrainRetired` |
| When is it freed? | **on the main thread, in `CookIfNeeded`, via `DrainRetired()`.** Never on the audio thread — `SampleSlot.h:17–20` names deleting on the audio thread as "one of the standing audio-thread prohibitions" |
| How is a glitch avoided? | the swap happens only at a block boundary, so no block runs half the old program and half the new one. A swap that genuinely changes the output still steps — so on `SwapIn() == true`, ramp the node's output gain 0→1 over ~64 samples. Precedent: `src/nodes/SamplerNode.cpp:113` uses a short fade for exactly this ("enough to avoid a click on trigger/steal, not a musical parameter") |
| How is an xrun avoided? | nothing on the audio thread allocates, locks, or compiles. `SwapIn` is one atomic exchange plus a ring write; the transplant (§5.9) is one bounded copy |

**The retire-ring trap.** `BufferRetireRing::kCapacity = 8`
(`src/audio/SampleSlot.h:44`), and when full it **drops the retire — the program
leaks** — rather than overwriting an unread slot (`:36–39`: "leaking one buffer
beats a double free"). Pushing faster than the audio thread adopts and the main
thread drains therefore leaks programs. Two obligations:

1. **Debounce the recompile** (§5.4). Never compile per keystroke.
2. **Call `DrainRetired()` unconditionally every `CookIfNeeded`**, not only when
   something changed.

### 5.9 Phase 8 — state at the swap boundary

Step 6 settled the semantics (`field-state` §7): transplant a cell whose **name
and type both survived** the edit; reset every other cell to its new
declaration's initial value; on a *failed* compile change nothing at all.

Those rules are stated in terms of **names**, and names cannot be compared on
the audio thread (checklist row 2). The resolution is the substance of this
phase.

**Split the rule into a main-thread half and an audio-thread half.**

| Half | Thread | What it does |
|---|---|---|
| decide | **main**, at compile time | for each cell `c` in the *new* program, find the cell in the *previous* program with the same name **and** the same type. Write its index into `newProgram->transplantFrom[c]`, or `-1` if there is none. This is where all the string comparison happens |
| apply | **audio**, on `SwapIn() == true` | one bounded pass, no strings, no allocation |

```cpp
// audio thread, immediately after SwapIn() returned true
// src and dst are the AudioNode's own two fixed state arrays (§5.3.2)
for (int v = 0; v < p->maxVoices; ++v)
   for (int c = 0; c < p->numStateCells; ++c) {
      const int from = p->transplantFrom[c];
      dst[v * p->numStateCells + c] =
         (from >= 0) ? src[v * prevNumStateCells + from]
                     : p->stateInit[c];
   }
mStateCur ^= 1;   // ping-pong; no allocation, no copy of the program
```

**Why the state arrays live in the `AudioNode` and not in the program.** If
state lived inside the `SampleProgram`, `SwapIn` would have already retired the
old program before the transplant could read it, and reading a retired pointer
is a use-after-free the moment `DrainRetired` runs. Two fixed arrays on the
node, ping-ponged, sized to the compile-time maximum, allocated once in
`PrepareToPlay`: the program is immutable code plus metadata, the state is the
node's.

| Wrong | Right | The bug |
|---|---|---|
| comparing cell names inside `ProcessBlock` | precompute `transplantFrom[]` on the main thread | `std::string::operator==` on the audio thread; and the names would have to be in the program, which reintroduces strings there |
| transplanting by **declaration order** | by `(name, type)`, resolved to indices at compile time | inserting a `state` line at the top shifts every cell's value one place — a filter's integrator lands in a delay's read index |
| transplanting by **name only**, ignoring type | require both | a `float`→`vec3` change reinterprets one float as one lane of three and the other two are garbage |
| resetting every cell on every edit | transplant the matches | a filter that resets on every keystroke is unusable for live editing, which is the entire point of a hot reload |
| transplanting on a **failed** compile | reconcile only on success | a typo mid-edit destroys the user's running state (invariant 1.4.5) |
| reading the retired program's state after `SwapIn` | keep state on the node, ping-ponged | use-after-free the next time `DrainRetired` runs |
| treating a compile swap as a reset | it is a transplant | `field-state` §5's reset list is seek / loop / stop. A swap is not on it |

**Serialization is out of scope here and stays out.** `field-state` §6 and
`field-integration` §5 both mark "what is actually serialized for element and
pixel state" **OPEN**, with option (b) — frame/sample cells only, as `Float`
params named `state.<name>` — satisfying the brief's stated motivation ("saving
mid-reverb and reloading restores the tail", a *sample*-domain case). If step 6
already resolved it, follow that resolution. If it did not, ship option (b) for
the sample domain only and say so; do not invent a new patch line kind here.

### 5.10 Phase 9 — denormals, NaN poisoning, and output clamping

#### Denormals

| Fact | Consequence |
|---|---|
| FTZ/DAZ is already set **per audio callback** for both architectures at `src/audio/AudioEngine.cpp:321–328` | **do not set it again.** Field gets it for free (D10) |
| FTZ/DAZ still does not cover every recursive path — a feedback cell decaying toward a tiny DC offset can stall on some paths | apply `DspMath::FlushDenormal(x)` (`src/audio/DspMath.h:64`, `fabs(x) < 1e-20f ? 0 : x`) **on every `state` cell write-back**, exactly as `src/audio/dsp/ReverbKernel.h:139` and `src/audio/dsp/SpecBlurKernel.cpp:98` already do |

One line, in one place: the state store-back in §5.3.3 step 5b. Not scattered
through the instruction switch.

#### NaN poisoning

A `state` cell that becomes NaN **stays** NaN — `NaN * 0` is `NaN`, `NaN + x` is
`NaN` — and it propagates out through the shared output buffer into every
downstream audio node, silencing the whole patch until the node is deleted. The
user's diagnosis is "the app broke", not "line 4 divides by zero".

| Where | Check | Precedent |
|---|---|---|
| **once per block**, not per sample | sweep the block's output for `!std::isfinite` | `src/audio/dsp/MetallicResonator.h:732` does exactly this shape |
| on a hit | zero the block, reset **every** state cell to its `stateInit` value, bump a `std::atomic<int>` fault counter | the counter is the only thing crossing back to main; the main thread renders the message |
| main thread, in `CookIfNeeded` | read the counter; if it grew, surface a visible non-fatal notice on the node naming the kernel | never a `printf` from the audio thread |

A per-sample check is the wrong trade: it costs a compare per sample per voice
to catch something that, once it happens, persists until the next block anyway.

#### Output clamping

`EquationDsp` already sets the house precedent at bake time: NaN/Inf → 0
(`src/audio/dsp/EquationDsp.h:743`), then a hard clamp to ±100
(`:745`), then peak normalization. Field's kernel output needs the same
belt-and-braces, at run time:

```
out = clamp(out, -kOutClamp, +kOutClamp)     // kOutClamp = 4.0f
```

Rationale for ±4.0 rather than ±1.0: it is ~12 dB of headroom above full scale,
so a legitimately hot signal is not silently distorted, while a runaway feedback
loop is loud-but-bounded rather than a full-scale DC blast into someone's
monitors. State the number in the node's help text; do not leave it implicit.

> **OPEN — what is `x / 0` in the sample domain? Ask the owner; do not decide in
> code.** Three incompatible answers already ship (D9):
>
> | Site | `x / 0` |
> |---|---|
> | `src/core/Expression.cpp` `ParseTerm` | **fails the whole evaluation** — `s.Fail("division by zero")`; `Evaluate` returns `false` and the param keeps its last good value |
> | `src/audio/dsp/EquationDsp.h:185` | returns **±1000.0** |
> | GLSL (the pixel backend) | driver-defined, typically `inf` |
>
> The frame domain **must** keep `Expression.cpp`'s behaviour — it is in the
> step-1 regression corpus. The sample domain **cannot**: "return false and keep
> the last good value" is not expressible per-sample, because there is no last
> good sample. So the sample backend needs a **total, non-trapping** semantics.
> Options: **(a)** the `EquationDsp` guarded form (a finite sentinel), which is
> total and already shipped here; **(b)** produce `inf` and rely on the §5.10
> NaN/Inf sweep to catch it one block later; **(c)** make division by a
> possibly-zero expression a compile-time warning and clamp the divisor.
> Recommend **(a)**, and **document the frame/sample divergence explicitly** —
> `field-compiler` §6.4 requires every intrinsic to have one semantic
> definition, so a deliberate per-domain difference must be written down, not
> discovered. Bring it to the owner with this table.

Note the same class of question for `%`: `Expression.cpp` uses C `fmod`, GLSL's
`mod` is the floored variant, and they disagree in sign for negative operands.
Step 1 already recorded this; the sample backend must match whatever the IR
settled on, not whatever `fmodf` happens to do.

### 5.11 Phase 10 — land step 8's `sample → frame` `reduce`

Step 8 specified this and deliberately did not build it. The design is fixed;
implement it as written.

| Rule | |
|---|---|
| Where it runs | on the **audio thread**, inside the block loop |
| What it publishes | **one float per block**, not per sample |
| Through what | the **existing** `MeterRing` (`src/audio/MeterRing.h`) — `Write` on the audio side (`:16`), `ReadLatest` on the main side (`:25`) |
| Drained by | `CookIfNeeded`, with **`ReadLatest`, never `Read`** |
| Why `ReadLatest` | a per-block producer against a per-frame consumer using `Read` accumulates **unbounded lag** — the value reads further behind the longer the patch runs. `ReadLatest`'s own comment says exactly this |
| `reduce.rms(in, lo, hi)` | band-filter `in` to `[lo, hi]`, then RMS over the block. The band filter is per-sample state and lives in the register machine like any other `state` |

**No new cross-thread channel.** §7's grep proves it.

### 5.12 Phase 11 — fixtures

`INFINITE_FIELDSAMPLETEST`. The register machine, register allocation, and every
compile-time refusal are pure computation and belong in an **early-exit,
headless** section gated before `glfwInit()`, modelled on `INFINITE_DSPTEST`
(`field-testing` §1). The node-level cases need a spawned node and the real
audio engine, so they belong in an **in-frame** section modelled on
`INFINITE_ROUNDTRIPTEST` (`src/main.cpp:43317`, fires at `frameId == 4`).
One verdict line per section, ending `OK` or containing `FAIL`.

| Section | Case | Assert |
|---|---|---|
| refusals | 129 params | compile error; the message contains `kMaxParams` **and** `ParamMailbox.h` |
| refusals | a `while`, or a `for` with a param-derived bound | refused, naming the non-constant expression |
| refusals | a program needing > 256 registers | refused, naming the count and the limit |
| refusals | `float in = 0` (shadowing a reserved name) | refused, and the message says **`sample` domain** |
| refusals | a delay-free cycle | refused, listing the cycle node by node with spans |
| correctness | the `field-language` §13 one-pole | matches `DspMath::OnePole`'s analytic response at three cutoffs, within a stated epsilon |
| correctness | `sr` and `n` | `sr` equals the engine's negotiated rate; `n` advances by exactly `numFrames` per block |
| params | push a value, run one block | the kernel sees it, smoothed, **within one block** — the generic guarantee `AUDIOPARAMSWEEPTEST` already checks |
| params | one param, eight held voices | the smoothing ramp length is **identical** to the one-voice case (this is the §5.3.3 sample-major bug, and it is invisible any other way) |
| params | `paramIndex` vs `paramId` | pushing by `paramIndex` under a hash scheme reaches **nothing**; pushing by `paramId` reaches the kernel |
| swap | recompile mid-playback | no discontinuity beyond the ramp; **zero xruns**; `AudioEngine`'s xrun counter unchanged |
| swap | 20 recompiles in 20 frames | every program is eventually freed — instrument `DrainRetired`'s delete count and assert it equals the push count. This is the `kCapacity = 8` leak (`SampleSlot.h:44`) |
| swap | a **failing** recompile | the previous program still runs, its state cells are untouched, the error text is set, and it does **not** recompile on the next frame (a compile counter over 10 frames) |
| state | rename one cell, keep another | the kept one transplants; the renamed one takes its initial value |
| state | change a cell's type, keep its name | reset, not reinterpreted |
| state | seek / loop / stop | every cell back to its initial value |
| voices | 9 notes into an 8-voice kernel | the oldest is stolen; the stolen voice's state cells are at their initial values; no click beyond the ramp |
| numerics | a long decay tail | flushes to **exact zero**, no denormal stall (the pattern `src/main.cpp:30018–30019` already tests for other nodes) |
| numerics | a kernel forced to NaN | the block is zeroed, cells reset, the fault counter incremented, and the **next** block is clean |
| numerics | a runaway feedback loop | output bounded by `kOutClamp`, never full-scale DC |
| allocation | 1000 blocks after `PrepareToPlay` | **zero** allocations, under a counter |
| teardown | delete the node mid-playback | no crash, no dangling cable, zero xruns (`AUDIOTEARDOWNSWEEPTEST`) |
| reduce | `reduce.rms(in, 20, 200)` on a known tone | the frame-side value matches the analytic RMS within a stated epsilon, and reads through `ReadLatest` |

**A test that cannot fail is not a test** (`field-testing` §0.2). Break each case
deliberately, watch it print `FAIL`, then fix it.

---

## 6. Traps, and the bug each one prevents

| # | Trap | The bug it prevents |
|---|---|---|
| 1 | Treating `EquationNode` as the model for per-sample execution | it bakes a wavetable on the main thread and does a table lookup on the audio thread (D6). Copying it gives you a synth with no `in`, no `state` and no feedback — i.e. not the sample domain |
| 2 | Calling `SmoothedValue` inside the voice loop | it advances the one-pole once per call (`ParamMailbox.cpp:22`), so a 5 ms ramp becomes 5/V ms **and its length changes with how many notes are held**. Silent, and it sounds like a broken knob |
| 3 | Calling `SmoothedValue` once per block | zipper noise: an audible staircase at block rate on every knob move |
| 4 | Silently truncating at 128 params | worse than truncation (D3): the knob draws, saves, modulates, and the kernel reads **`0.0f`**. Make it a compile error naming `kMaxParams` and `src/audio/ParamMailbox.h:23` |
| 5 | Passing `paramIndex` to `ParamMailbox::Push` | under a name-hash scheme the id is far outside `0..127`, `Push`'s bounds check discards it, and every param in the kernel reads zero |
| 6 | Using declaration order as `paramIndex` | inserting one `param` line at the top re-points **every** modulation cable on the node. Same failure `node-ui-pillars` P7 documents for saved filter-mode indices |
| 7 | Hand-rolling a program swap | `SampleSlotT` (`src/audio/SampleSlot.h:77`) exists so this trap is not reimplemented per node; its comment says so. A hand-rolled one is a fourth cross-thread channel and a use-after-free waiting for a reload under load |
| 8 | `delete`-ing the retired program on the audio thread | `SampleSlot.h:17–20` names it a standing prohibition. The free is `DrainRetired()`, on the **main** thread |
| 9 | Recompiling per keystroke | the retire ring holds 8 and then **leaks** rather than overwriting (`SampleSlot.h:36–39`, `:44`). Debounce, and drain unconditionally every cook |
| 10 | Recompiling a program that already failed | `FormulaNode::CookIfNeeded`'s guard (`src/nodes/FormulaNode.cpp:415`) exists for exactly this. A retry loop burns a compile per frame for a program that will keep failing |
| 11 | `SwapIn()` anywhere but the top of `ProcessBlock` | half the block runs the old program and half the new — an audible discontinuity that is not reproducible, because it depends on where in the block the main thread happened to push |
| 12 | Keeping `state` inside the `SampleProgram` | `SwapIn` retires the old program before the transplant can read it; the next `DrainRetired` frees it under you |
| 13 | Comparing cell names on the audio thread | `std::string` comparison below `ProcessBlock`, and it forces names into the program object. Precompute `transplantFrom[]` on the main thread |
| 14 | Transplanting state by declaration order | inserting one `state` line shifts every cell's value by one — a filter integrator lands in a delay's read index and the node self-oscillates |
| 15 | Transplanting by name while the type changed | a `float`→`vec3` edit reinterprets one float as one lane of three |
| 16 | Resetting every cell on a successful recompile | a filter that resets on every keystroke makes live editing useless, which is the whole point of a hot reload |
| 17 | Reconciling anything on a **failed** compile | a typo mid-edit destroys the running program, its params and its state (invariant 1.4.5) |
| 18 | Carrying `state` across a voice **steal** | the new note inherits the stolen note's integrator — a click, or a filter that never settles. `Envelope::ResetLevel()`'s comment (`AudioVoice.h:31–39`) is the precedent for treating a steal differently from a retrigger |
| 19 | Resetting `state` on a same-note **retrigger** | the opposite error: legato and repeated notes click. Same comment, other direction |
| 20 | A voice count read from a param or from note traffic | `field-realtime` §1 rules 5 and 6; the state array cannot be sized at compile time and the allocation moves to the audio thread |
| 21 | Writing a ninth voice allocator | `VoiceAllocator` (`src/audio/AudioVoice.h:123`) already implements round-robin-then-steal-oldest and documents it |
| 22 | Setting FTZ/DAZ in the Field node | already done per callback at `src/audio/AudioEngine.cpp:321–328` (D10). A second `_mm_setcsr` is a redundant write on every block and a portability hazard |
| 23 | Omitting `FlushDenormal` on `state` write-back | FTZ/DAZ does not cover every recursive path; a decaying feedback cell can stall. `ReverbKernel.h:139` is the shipped pattern |
| 24 | Checking for NaN per sample | a compare per sample per voice to catch something that persists for the rest of the block anyway. Sweep once per block |
| 25 | Letting a NaN escape into the output buffer | it poisons every downstream node through the shared buffer and silences the whole patch. `MetallicResonator.h:732` is the shipped guard |
| 26 | No output clamp | a runaway feedback loop sends full-scale DC to the user's monitors. `EquationDsp.h:745` clamps at bake time for the same reason |
| 27 | Copying `Expression.cpp`'s division semantics into the sample backend | "fail the evaluation and keep the last good value" has no meaning per-sample. §5.10's open question |
| 28 | Predicating branches in the sample backend | predication is the **pixel** lowering. In `sample` a branch is a real branch; predicating doubles the cost of every `if` for nothing (`field-realtime` §4) |
| 29 | Publishing a `reduce` through a new atomic or queue | `ParamMailbox.h:11–20` records the real bug a hand-rolled ring caused here. Use `MeterRing` |
| 30 | Draining a `reduce` with `MeterRing::Read` once per frame | a per-block producer against a per-frame consumer accumulates **unbounded lag**. `ReadLatest` |
| 31 | Putting a `SampleXxx` node type into the IR | `field-compiler` §6.4 — the moment one exists, the C++ and WASM backends must special-case it and retargetability is gone |
| 32 | Compiling anywhere reachable from `ProcessBlock` | trace every caller of the compile entry point. `field-realtime` §1 rule 11 |
| 33 | Any DSP in `CookIfNeeded` | `< 5 µs` budget (`new-audio-node` §0.3). It drains, pushes and frees; it does not generate samples and it does not compile |
| 34 | Auto-generated knob rows laid out "however many params there are" | `node-ui-pillars` — symmetry and dark-mode contrast are non-negotiable whether the row was hand-written or generated |

---

## 7. Machine-checkable exit criterion

Run all of these. Every one must pass.

```bash
cd /Users/namansoni/infinte

# ─── 1. builds clean ──────────────────────────────────────────────────────
cmake --build build -j"$(sysctl -n hw.ncpu)"

# ─── 2. the sample-domain fixture ─────────────────────────────────────────
INFINITE_FIELDSAMPLETEST=1 INFINITE_EXITAFTER=10 \
  ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/fieldsample.log
grep -c FAIL /tmp/fieldsample.log     # must print 0
grep -c BUG  /tmp/fieldsample.log     # must print 0
grep -c 'OK$' /tmp/fieldsample.log    # must print >= 1 (a silent pass is not a pass)

# ─── 3. the generic audio sweeps, with ZERO xruns ─────────────────────────
INFINITE_AUDIOPARAMSWEEPTEST=1 INFINITE_EXITAFTER=1 \
  ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/audioparam.log
grep -c FAIL /tmp/audioparam.log      # must print 0

INFINITE_AUDIOTEARDOWNSWEEPTEST=1 INFINITE_EXITAFTER=10 \
  ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | tee /tmp/audioteardown.log
grep -c FAIL /tmp/audioteardown.log   # must print 0
grep -iE 'xrun' /tmp/audioteardown.log | grep -v ' 0 ' && echo "XRUNS - FAIL" \
                                                       || echo "zero xruns OK"

INFINITE_AUDIOLIFECYCLETEST=1 INFINITE_EXITAFTER=8 \
  ./build/Infinite.app/Contents/MacOS/Infinite 2>&1 | grep -c FAIL   # must print 0

# ─── 4. no new cross-thread channel; SampleSlotT reuse only ───────────────
#     Must print nothing. SampleSlotT<...> in FieldSampleNode is a REUSE of
#     src/audio/SampleSlot.h and is expected; a new atomic/queue/mutex is not.
git diff main -- src/core/field/ src/nodes/FieldSampleNode.h src/nodes/FieldSampleNode.cpp \
  | grep -E '^\+.*(std::atomic<[^>]*\*>|RingBuffer|std::mutex|std::condition_variable|std::thread)' \
  | grep -v 'SampleSlotT' \
  || echo "no new cross-thread channel OK"

# ─── 5. allocation shapes in anything the audio thread can reach ──────────
#     Every hit must be justified in the commit message or removed.
git diff main -- src/core/field/SampleRuntime.h src/core/field/SampleProgram.h src/nodes/FieldSampleNode.cpp \
  | grep -nE '^\+.*(std::function|std::string|std::vector|std::map|std::shared_ptr|make_shared|push_back|resize|reserve|dynamic_cast|printf|snprintf|new |malloc)' \
  || echo "no allocation shapes OK"

# ─── 6. the compiler is not reachable from the audio thread ───────────────
#     ProcessBlock must not mention any compile entry point.
awk '/::ProcessBlock/,/^}/' src/nodes/FieldSampleNode.cpp \
  | grep -nE 'Compile|Lex|Parse|Infer|Lower|Emit' \
  && echo "COMPILER REACHABLE FROM AUDIO THREAD - FAIL" \
  || echo "compiler off the audio thread OK"

# ─── 7. the shared audio/modulation code is untouched ─────────────────────
git diff main --stat -- src/audio/ParamMailbox.h src/audio/ParamMailbox.cpp \
                        src/audio/SampleSlot.h src/audio/MeterRing.h \
                        src/audio/AudioNode.h src/audio/AudioVoice.h \
                        src/audio/DspMath.h src/audio/AudioEngine.cpp \
                        src/core/Modulation.h src/core/INode.h src/core/Patch.h
#   expected: no output

# ─── 8. step 1's public API and its three call sites are untouched ────────
grep -n 'Expression::Evaluate' src/main.cpp src/core/ExprGlobals.cpp src/nodes/AnalyzeNodes.cpp
#   expected: exactly 3 lines, at main.cpp:37506, ExprGlobals.cpp:72, AnalyzeNodes.cpp:179
git diff main -- src/core/Expression.h | grep '^[-+].*Evaluate' || echo "signature unchanged OK"

# ─── 9. the 128 ceiling is a real error with a real message ───────────────
grep -rn 'kMaxParams' src/core/field/ src/nodes/FieldSampleNode.cpp \
  || echo "NO 128 CHECK - FAIL"
grep -rn 'ParamMailbox.h' src/core/field/ | grep -i 'error\|message\|msg' \
  || echo "error message does not cite the header - review"

# ─── 10. no sigils, no declared rates ─────────────────────────────────────
grep -rn '@[A-Za-z]' src/core/field/ src/nodes/FieldSampleNode.* docs/plans/field/step-09-*.md \
  | grep -v '@brief\|@param\|email' || echo "no sigils OK"
grep -rn 'krate\|@rate' src/core/field/ src/nodes/FieldSampleNode.* \
  && echo "RATE DECLARED - FAIL" || echo "no rate syntax OK"

# ─── 11. the full gate ────────────────────────────────────────────────────
.claude/skills/run-infinite-hygiene/driver.sh

# ─── 12. project convention ───────────────────────────────────────────────
cp -R build/Infinite.app ~/Desktop/Infinite.app
```

Then run `.claude/skills/audio-node-sweep/SKILL.md` and
`.claude/skills/node-param-audit/SKILL.md`'s procedures against the new node.

`AUDIOPARAMSWEEPTEST`, `AUDIOTEARDOWNSWEEPTEST`, `AUDIOLIFECYCLETEST`,
`ROUNDTRIPTEST`, `PATCHTEST`, `UNDOTEST`, `MODMATRIXTEST` and `MODBOUNDSTEST`
are the hygiene entries that directly guard step 9; all must pass, and the
`AUDIOPARAMSWEEPTEST` xfail baseline must not grow.

**Step 9 is done when all of the following hold** (`field-testing` §6 row 9,
`field-realtime` §7):

1. It builds clean and the full hygiene gate passes.
2. Zero allocations after `PrepareToPlay`, verified under an allocation counter,
   not by inspection alone.
3. Zero xruns across a recompile-under-playback and across node teardown.
4. A 129th `param` is a **compile error** whose message names `kMaxParams` and
   `src/audio/ParamMailbox.h` — never a silent no-op knob.
5. `paramIndex` (modulation identity, stable by name) and `paramId` (mailbox
   slot, dense `0..127`) are distinct, and a modulation binding survives
   inserting a `param` line above it.
6. The voice bound is a compile-time constant; a 9th note into an 8-voice kernel
   steals the oldest and **resets that voice's state cells**.
7. A compile swap happens only at a block boundary, the old program is freed on
   the **main** thread via `DrainRetired()`, and pushes and frees balance over a
   20-recompile burst.
8. State transplants by `(name, type)` — resolved to indices on the main thread —
   and a failed compile changes nothing at all.
9. A one-pole written in Field matches `DspMath::OnePole`'s analytic response.
10. A forced NaN is caught within one block, the cells reset, and the fault is
    surfaced on the node; a runaway loop is bounded by the output clamp.
11. `reduce.rms(in, 20, 200)` publishes one float per block through the existing
    `MeterRing`, drained with `ReadLatest`; the §7 grep confirms no new channel.
12. Every question marked **OPEN** that this step touches (division semantics;
    sample-domain state serialization if step 6 left it open) has been **put to
    the owner and answered** — not silently resolved in code.

---

## 8. Out of scope for this step

| Not in step 9 | Where it lands |
|---|---|
| The `graph` domain | step 10 |
| Anything touching `Mesh`, geometry, or the AoS/SoA question | step 4, and `field-compiler` §9's open question |
| Anything emitting GLSL | step 7 |
| A `pixel → frame` reduce and its readback latency | step 8's open question; not reopened here |
| Widening `ParamMailbox::kMaxParams` | an owner conversation, not a diff |
| Making the mailbox smoothing time per-param | same. Note the 5 ms constant (D8) in the node's help text and move on |
| Non-float params (`int`, `bool`, enum, colour) | a later, separately-scoped change. `isEnum`/`isBool`/`enumOptions`/`posToValue`/`valueToPos` stay at their defaults |
| SIMD / vectorizing the register machine across voices | a follow-up, once the scalar version is correct and measured. Do not start here |
| A JIT, LLVM, or native codegen | not in v1 at all. The register machine's justification (Norilo p.31) is about avoiding *interpreter dispatch*, not about needing machine code |
| A `parallel`, `scan`, `fold`, `zip` or `filter` operator | five transfer operators, full stop |
| Changing `Expression::Evaluate`'s signature or its three call sites | never |
| Changing undo *semantics* | never in this step |
| Rewriting `EquationNode` to use Field | tempting and out of scope. Read it; do not touch it. Migrating it is its own owner-approved change once Field's sample domain has shipped and settled |

If you find a genuine bug in existing audio code while in here, **report it
rather than fixing it inline.**

---

## 9. Which earlier steps must be done first

Worked out from the brief's §15 build order plus what this step actually reads.

| Step | Required? | Why |
|---|---|---|
| **1** — lexer / AST / typed IR / bytecode | **yes** | there is no IR to lower and no spans for the error messages this step is largely made of |
| **2** — pure `rand`/`noise`/`sh` | **yes** | a sample kernel calling `rand` at 48 kHz with today's three-sine sum is strongly autocorrelated and takes no seed; it must be the `(t, seed)` form before it runs at audio rate |
| **3** — `vec2/3/4` + rank polymorphism | **yes** | stereo. A `vec2` `out`, and `P *= 2.0`-style scalar broadcast, are load-bearing in any real audio kernel |
| **5** — `param` → `ParamRef` | **yes** | §5.5 and §5.6 are entirely about the two index spaces, and there is no `paramIndex` to be distinct from without step 5. Step 5 also already implements the compile-time 128 check that step 9 wires to the mailbox |
| **6** — `state` cells | **yes** | a sample kernel with no `state` cannot express a filter, a delay, or any feedback — i.e. it cannot express the reason the sample domain exists. §5.9 is the audio-thread form of step 6's transplant rule |
| **8** — transfer operators | **coordinates with** | step 8 built `reduce` in the IR and in the error messages, and explicitly deferred the audio-thread half to here (§5.11). If step 8 is done, land its spec unchanged. If step 8 is somehow not done, step 9 can still ship a sample kernel with no `reduce`, and `reduce`'s audio half lands with step 8 instead — but say which you did |
| **4** — the `element` domain | **no** | nothing here touches a mesh. Step 8 needs it; step 9 does not |
| **7** — the `pixel` domain | **no** | nothing here emits GLSL |
| **10** — the `graph` domain | **no**, and it depends on **this** step, not the other way round | |
