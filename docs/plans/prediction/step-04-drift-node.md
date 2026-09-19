# Prediction step 4: the Drift node

The first user-visible piece: a green node implementing README §4.1 (Continue + Home + Wander in one
SDE), with 6 controls, Freeze, a confidence dot and a ghost line.

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

## Start

Prereq: step 3 merged.

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-04-drift-node
```

Skills: `new-modulator-node` (the whole thing; §4 wiring checklist and §7 exit criterion),
`node-ui-pillars`, `node-param-audit`, `run-infinite-hygiene`.

## Files to read first

| File / symbol | Why |
|---|---|
| `src/nodes/ModulatorNodes.h` `LFONode` | shape of a modulator node: `INode + IModulator`, empty `CookIfNeeded`, zero-size output |
| `src/main.cpp:5439` `REGISTER_NODE(LFONode, LFO, "Modulators")` in `RegisterNodes()` | registration line to copy |
| `CMakeLists.txt:456` `src/nodes/ModulatorNodes.cpp` | where to add the new `.cpp` |
| `src/core/INode.h` `ParamVisitor` (`Text(name, std::string&)` at `:87`) | saving the frozen profile as text |
| `src/core/CategoryColors.cpp:33/52/78` `"Modulators"` colours per theme | the default theme already uses lime `#A3E635`; see README §12 Q7 |
| `src/main.cpp:37310` and `38096` (LFO help strings), `75275` (browser table) | the hand-kept help/browser tables: all must list the new node (memory: node help has 4 tables; check both directions) |
| `src/core/MovementStats.h` (step 2) | θ, σ, `p̂`, range, `releaseVel`, `nEff` |
| `src/core/Modulation.h` `IPredictor` (step 3) | the interface to implement |

## What to build

### 4.1 `src/nodes/PredictionNodes.h/.cpp` → `class DriftNode : public INode, public IModulator, public IPredictor`

**Params (`VisitParams`, stable names):** `speed` (0.1–4, default 1), `stray` (0.25–4, log, default 1),
`momentum` (0–4 s, default 1), `link` (0–2, default 0), `seed` (int), `frozen` (bool),
`frozenProfile` (text), `anchors` (text: `uid:paramIndex=pos` per binding; README §6 rung 4).
Depth is the binding's own lo/hi and needs no param.

**Per-slot live state** (not saved): `x`, `v`, RNG state, last seen frame. Slots live in a
`std::unordered_map<ParamKey, Slot>`. Drop a slot after 300 frames unseen.

**`Tick(frameId, dt)`**, for each slot, with stats `S = MovementStats::For(key)` (or the frozen copy):

```
τm  = momentum
v   = v · exp(−dt / τm)
E   = MovementStats::HandEnergy(key)       // [0,1] = 0.7·role + 0.3·all, live, Hand+Perf only (step 5)
T   = stray · (1 + link · E);  η = speed · θ(S) · k        // k calibrated so relaxation ≈ 1/θ; document the value
σ   = sqrt(2 · η · T)                      // Stray is an exact temperature: stationary ∝ p̂^(1/T)
drift = clamp(−η · U′(x) · dt, −0.05, 0.05)    // U = −log(p̂ + ε), U′ by central difference on bins
x   = x + v·dt + drift + σ·sqrt(dt)·N(0,1)
reflect x into [lo(S), hi(S)]  (fallback [0,1])
```

With too little data (`nEff` below the step-5 threshold), `p̂` is flat, so this is a gentle
reflected random walk: README §6 rung 4, until step 5 adds the blending.

**`OnRelease(k, pos, vel)`:** `x = pos`, `v = vel`. **`OnGrab`:** mark the slot paused.
**`Value01()`** (the matrix meter): the first slot's `x`, or 0.5.

### 4.2 Freeze

Turning `frozen` on serialises, per bound key, `p̂[64] (8-bit quantised), θ, σ, lo, hi` into
`frozenProfile` (≈ 150 B per binding, base64). While frozen, `Tick` reads only that. A shared patch
then behaves the same on another machine; **with the same `seed` it is deterministic**.

### 4.3 UI (`DrawDriftParams` in `src/main.cpp`, dispatched beside `DrawLFOParams`)

- One `AudioKnobRow` / `ModKnob` row: Speed · Stray · Momentum · Link; Seed as `ModSliderInt`; Freeze as
  `ModCheckbox`. Every control is a `Mod*` widget (`node-param-audit`: no raw ImGui sliders).
- A **confidence dot** per bound param (colour by `nEff` rung), in the pillar-approved slot.
- **Ghost line:** in the modulated branch of `ModSlider`/`ModKnob`, when the source is a `DriftNode`,
  draw the next 2 s (32 points) from a *copy* of the slot state (x, v) **and the RNG state**.
  Copy the state, do not reseed from `seed`: reseeding would draw the path from the start of the take,
  not from now. Never advance the real slot. The preview steps at a fixed dt (1/16 s) while the real
  slot steps at frame dt, so the ghost is the likely path, not an exact one; label it that way. Compute
  it at most once per slot per frame (cache on `frameId`).
- Run the `node-ui-pillars` checklist, in light and dark themes.

### 4.4 Wiring (the `new-modulator-node` §4 checklist)

`REGISTER_NODE(DriftNode, Drift, "<category>")`, where the category string is one word (README §12 Q7);
CMake; the help strings at both help tables; the browser entry; the Node Reference Manual flag for the
`ship-infinite` skill. Deletion needs nothing: `UnbindAllFor` already handles it.

## Traps

| Trap | Why |
|---|---|
| Spiky `U′` | A raw 64-bin histogram makes the drift jump. Smooth it (step 2) and cap the drift step. |
| σ from "dynamic range" | Wrong statistic. σ follows from η and Stray (above), and θ from AR(1). |
| Ghost line mutating state | Simulate a copy of x, v **and the RNG state**; test 4 below proves it. |
| Colour clash | Lime is already the Modulators colour in the default theme. |
| Saving live state | x/v/RNG are not params; only Freeze data and `anchors` are. |
| Anchor creep | Update an anchor only from a `Hand`/`Perf` write to that key, never from Drift's own output. |

## Test: `INFINITE_DRIFTTEST`

1. Seed stats with a two-peak `p̂`, T = 1, and run 10 simulated minutes: the value histogram matches
   `p̂` (KL < 0.1). At T = 0.25 it is sharper; at T = 4 it is flatter.
2. `OnRelease` with v = 0.5/s and τm = 1 s: the value travels ≈ 0.5 before settling.
3. The same seed and frozen profile give identical 600-frame traces across two runs; save/load
   keeps them identical.
4. **Ghost isolation:** two identical seeded nodes, one with its ghost line drawn every frame and one
   without, produce bit-identical 600-frame traces.
5. The node passes the `new-modulator-node` §7 exit criterion (spawn, bind, save/load, delete
   mid-drive: no dangling binding).

## Exit criterion

`DRIFTTEST` + `PREDBINDTEST` + the modulation group pass. The `node-ui-pillars` checklist is clean in
both themes. The owner has tried it on a real patch; per memory, the owner verifies the UI by hand,
so it is not UI-scripted.
