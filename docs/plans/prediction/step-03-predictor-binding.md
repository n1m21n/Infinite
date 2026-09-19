# Prediction step 3: the `IPredictor` binding path

Teaches the modulation system to host a per-destination predictor (README §2.1–§2.3): the side
interface, fader-space mapping and Shift-grab override. It uses a **test-only stub predictor**; the
real node is step 4.

**This step changes established modulation semantics.** Run the `modulation-sweep` skill before merging.

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

## Start

Prereq: steps 1–2 merged; the step-2 gate passed.

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-03-predictor-binding
```

Skills: `codebase-navigation`, `new-modulator-node` (§2 idempotency), `modulation-sweep`,
`invariant-interaction-audit`, `node-ui-pillars` (the Shift-grab widget state).

## Files to read first

| File / symbol | Why |
|---|---|
| `src/core/Modulation.h:17` `IModulator`; `:30` `ParamRef`; `Source` (`lo`, `hi`, `curve`, `enabled`) | where `IPredictor` sits and what a binding carries |
| `src/main.cpp:61435–61490` modulator branch of `ApplyModulationAndPalette`: `ResolvedSourceFor`, bypass check, `ModulatorForOutput`, `MacroNumBoxNode` special case (`61460`), generic write (`61488`) | the new branch goes beside `MacroNumBoxNode` |
| `src/main.cpp:61063` `ShapeToParam` | final snap + clamp, unchanged |
| `src/main.cpp:3419` `ModSlider`: `modulated` at `3442`, read-only draw ~`3600–3620`, gesture lock `3770–3777`, `StopPlayback` on activate `3750` | the Shift-grab path for sliders |
| `src/main.cpp:4525` `ModKnob`: `modulated` at `4632` | the same for knobs |
| `src/main.cpp:3361` `ModCheckbox`, `2925` `RegisterDiscreteParam` | discrete params: green does **not** bind to them (see below) |
| `src/main.cpp:3940` `VFaderFloat`, `4324` `BipolarKnobFloat` | drawing primitives under `ModSlider`/`ModKnob`; take the `readOnly` flag |
| `src/main.cpp:66607` `ClearFrameParams()` in the frame loop | clear the per-frame grab set here too |
| `src/main.cpp:81929` `registerOnlyParams` | a collapsed green-driven node must keep registering (`hasModulatedParams`) |

## What to build

### 3.1 Interface (`src/core/Modulation.h`, beside `IModulator`)

```cpp
struct ParamKey { uint64_t uid; int paramIndex; };

class IPredictor {
public:
   virtual ~IPredictor() {}
   virtual void  Tick(int frameId, double dt) = 0;                 // advance every slot once
   virtual float ValuePos01For(const ParamKey& k, float curPos) = 0; // pure read, 0..1 fader pos
   virtual void  OnGrab(const ParamKey& k) = 0;                     // Shift-grab began
   virtual void  OnRelease(const ParamKey& k, float pos, float velPerSec) = 0;
};
```

`curPos` lets a new slot start where the param is, not at 0.5.

### 3.2 Apply loop (`ApplyModulationAndPalette`)

- **Before** the `FrameParams` loop, tick every predictor once:
  `for gn in gNodes: if (!gn.node->bypassed) if (auto* p = dynamic_cast<IPredictor*>(gn.node.get())) p->Tick(frameId, dt)`.
  This is the idempotency rule: never advance inside `ValuePos01For`. **Skip bypassed predictors**
  (bypassed nodes do not cook), so a bypassed Drift freezes rather than advancing unseen and jumping
  on un-bypass.
- **Order inside the loop is load-bearing:** the new branch goes *after* the existing
  `bypassed && BypassSource() == nullptr` check (`src/main.cpp` ~61440) and the `ModulatorForOutput`
  null check, and *before* the `MacroNumBoxNode` check (~61460). Never hoist it above the bypass check.
- In the modulator branch, **before** the `MacroNumBoxNode` check:

```cpp
if (auto* pred = dynamic_cast<IPredictor*>(modNode->node.get())) {
   if (ref.isEnum || ref.isBool) continue;                   // green never drives discrete params
   const ParamKey k{ UidForIndex(ref.nodeIndex), ref.paramIndex };
   if (gPredictorGrabs.count(k)) { MovementLog::NoteWriter(..., Hand /*+kCorrection*/); continue; }
   const float cur = ToPos(ref, *ref.value);
   float p = ApplyModulationCurve(std::clamp(pred->ValuePos01For(k, cur), 0.f, 1.f), src.curve);
   const float posLo = ToPos(ref, src.lo), posHi = ToPos(ref, src.hi);
   *ref.value = ShapeToParam(ref, FromPos(ref, posLo + (posHi - posLo) * p));
   MovementLog::NoteWriter(ref.nodeIndex, ref.paramIndex, Source::Prediction);
   continue;
}
```

`ToPos`/`FromPos` use `ref.valueToPos`/`posToValue` when set and are linear otherwise. The generic
modulator path (`61488`) is untouched.

### 3.3 Shift-grab override (widgets)

In `ModSlider` and `ModKnob`, when `modulated` and the binding's source node is an `IPredictor`:

- Shift held (or the item already active): draw the **editable** path (`readOnly=false`), not the
  read-only branch.
- `IsItemActivated()` → `PushUndoCheckpoint()` + `pred->OnGrab(k)`.
- `IsItemActive()` → insert `k` into `gPredictorGrabs` (a per-frame set, cleared beside
  `ClearFrameParams()` at `66607`).
- `IsItemDeactivated()` → `pred->OnRelease(k, pos, vel)`; take the velocity from `MovementStats`
  `releaseVel` (step 2).
- The pin/track colour for a green binding is the Prediction colour (owner decision, README §12 Q7).
  Keep it inside the dark/light contrast budget of `node-ui-pillars`.

Discrete widgets (`ModCheckbox`, `RegisterDiscreteParam`) are unchanged. Green-to-discrete is blocked
at **every** path that creates a binding, not only the cable drop:

| Path | Site | Guard |
|---|---|---|
| cable drag onto a param pin | `src/main.cpp` ~82937 (`Modulation::Instance().Bind(dstNode->index, …)`) | refuse when the source is an `IPredictor` and `KnownParam(dst)` is enum/bool; show the "can't connect" cursor |
| patch load / undo / redo | `ApplyPatchData` → `RestoreLink` ~43365 | cannot check yet (the destination has not registered), so allow it and let the apply-loop guard make it inert |
| paste / duplicate | `RestoreLink` ~7263 | same as load |
| apply loop (defence in depth) | the `isEnum \|\| isBool → continue` line in §3.2 | always on; the modulation matrix shows such a row as disabled with a tooltip |

Re-grep `Bind(` and `RestoreLink(` before merging: any new user-facing bind path needs the first guard.

### 3.4 Stub for tests

`StubPredictorNode` (inside the `#ifndef NDEBUG` test code, never registered in release; see the
no-debug-tools-in-release rule): it returns a fixed per-key ramp and counts `Tick` calls.

## Traps

| Trap | Why |
|---|---|
| Advancing state in the read | Driving 3 params would move 3× as fast (`new-modulator-node` §2). |
| Linear lo/hi on log knobs | Crowds predictions at the top. Map in fader space. |
| Plain grab overrides | Breaks the lock convention every other cable and gesture loop follows. Shift only. |
| Keying slots by `nodeIndex` | Undo renumbers. Use `(uid, paramIndex)`. |
| Forgetting bypass | The existing bypass check (`modNode->node->bypassed && BypassSource()==nullptr`) must run before the predictor branch, as it does for every modulator. |
| Grab set not cleared | A stale entry freezes a param forever. Clear every frame. |

## Test: `INFINITE_PREDBINDTEST`

With the stub predictor:

1. One stub drives 3 params: `Tick` runs once per frame (idempotency).
2. A log-scaled param given `p = 0.5` lands at `posToValue(0.5)`, not at the linear midpoint.
3. Simulated Shift-grab: the binding is skipped while active; `OnRelease` gets the new pos; a plain
   grab changes nothing.
4. Undo after binding: the slot key (uid) is unchanged and the binding still drives.
5. Binding a green cable to a bool pin by cable drag is refused; a hand-edited patch file carrying
   that binding loads, the param is never written, and the matrix row shows disabled.
6. Bypass the stub: `Tick` count stops and the param holds; un-bypass resumes from the held value.
7. Every existing test in `GROUP_MODULATION` still passes.

## Exit criterion

`PREDBINDTEST` passes; the `modulation-sweep` skill reports no new failures; `MODTEST`, `MODBOUNDSTEST`,
`MODMATRIXTEST` and `GESTUREUNDOTEST` pass unchanged.
