# Prediction step 5: cold start, source weights and collapse monitors

Makes Drift behave well on day one and keeps it from teaching itself (README §5, §6).

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

## Start

Prereq: step 4 merged.

```bash
git switch main && git pull --ff-only
git switch -c feature/prediction-step-05-cold-start
```

Skills: `invariant-interaction-audit` (the weights are an invariant: "the model never learns mostly
from itself").

## Files to read first

| File / symbol | Why |
|---|---|
| [README.md](README.md) §5 (weights table, monitors), §6 (ladder, readiness table) | the spec |
| `src/core/MovementStats.h/.cpp` (step 2) | per-key and per-profile stats, `nEff` |
| `src/nodes/PredictionNodes.cpp` `DriftNode::Tick` (step 4) | where the blended stats are read |

## What to build

### 5.1 Profile stats (rung 2)

`MovementStats` already keeps a profile per `(nodeType, paramIndex, name)` (step 2). Make sure every
`Observe` also updates the profile with the same weight. Profiles span patches, and that is what lets
a new patch start warm.

### 5.1b Role stats (rung 2b: "cutoff is cutoff")

Add a third stats level keyed by **role** (README §6 role table), in `src/core/ParamRoles.h`: one
`constexpr` table of `{category, alias, role, family, unit}` rows, plus
`RoleFor(category, name) -> {role, family, unit}`. Build the alias column from the real labels:
`grep -rhoiE '(ModSlider|ModKnob|ModSliderInt|AudioKnob)[A-Za-z]*\("[^"]+"' src/main.cpp src/nodes`,
and review the top 60. The category comes from `REGISTER_NODE`. A fourth level, keyed by **family**,
is fed by the same `Observe`. Every `Observe` for a key with a role also updates
the role stats, converted to the role's physical unit:

- `filter.cutoff`: `log2(hz)`, where `hz = posToValue(pos, min, max)` of the source param;
- `level.gain`: dB; `pitch.*`: cents (`pitch.freq`: 1200·log2 Hz); `size.*` and `xf.pos`: a fraction
  of the canvas, taken from the node's canvas/output size, else fader pos; `colour.hue`: circular
  (wrap the histogram); roles without a unit: fader pos.

To read a role histogram for a target key, rebin it into the target's fader space through the target's
own `valueToPos(min..max)`, and drop and renormalise any mass outside the target's range.

### 5.1c "You" stats (rung 2c) and hand energy

- **You stats:** one global record fed by every `Hand`/`Perf` observation of every key, in fader space:
  log θ (weighted mean + variance), release τ, pause-length histogram. **Never** a p̂. Persist it in
  `stats.bin` beside the keys.
- **`MovementStats::HandEnergy(key)`:** `0.7·E_role + 0.3·E_all`. Each is an EMA (τ ≈ 1 s) of `|Δpos|/dt`
  summed over `Hand`/`Perf` writes this frame (all keys, or keys with this key's role in the current
  patch), divided by the you-stats typical move speed, clamped to [0,1]. Keep one EMA per role
  present in the patch (a few dozen floats). Updated in `Observe` on the
  main thread, so a read is O(1). Prediction, modulator, gesture and `Other` writes do not count.
- **Anchors:** on every `Hand`/`Perf` write to a key bound to a Drift node, that node sets
  `anchors[key] = pos`. At bind time with no entry, use the current pos.

### 5.2 Blending, not switching

```
w1 = nEff(key)     / (nEff(key) + N0)          N0 ≈ 30 independent samples (tunable)
w2 = nEff(profile) / (nEff(profile) + N0) · (1 − w1)
w2b= nEff(role)    / (nEff(role) + 3·N0)  · (1 − w1 − w2)   // weaker prior: larger N0
w2d= nEff(family)  / (nEff(family) + 6·N0) · (1 − w1 − w2 − w2b)   // speed + range; p̂ only for one-unit families
w4 = 1 − w1 − w2 − w2b − w2d
p̂  = w1·p̂_key + w2·p̂_profile + w2b·p̂_role + w2d·p̂_family + w4·p̂_anchor    // p̂_anchor: N(anchor, 0.05); flat if none
                                                                // anchor weight inside w4 is N0/3-scaled
θ, momentum: the same chain, with rung 2c inserted before the fallback:
   wYou = nEff(you)/(nEff(you)+N0) · (1 − w1 − w2 − w2b − w2d)
   θ    = exp( w1·lnθ_key + w2·lnθ_profile + w2b·lnθ_role + w2d·lnθ_family + wYou·lnθ_you + rest·lnθ_default )
range: declared min/max until w1 > 0.2
```

Rung 3 (community prior) is **out of scope** until the sharing endpoint exists. Leave a `w3 = 0` slot.

### 5.3 Source weights

Encode README §5's table in one `constexpr` array in `MovementStats.cpp`, indexed by `Source` + flags:
hand 1.0, perf 1.0, correction 3.0, gesture 0.2, modulator 0.1, expression 0.1, prediction 0.1,
untouched prediction 0, other (preset/randomize) 0. It is a single edit point, and the raw log is unaffected.

### 5.4 Monitors and auto-cut

Per key, over rolling windows of active time: the entropy of `p̂`, the fitted σ, and the share of
`nEff` from prediction rows. **Auto-cut:** if entropy falls > 25% over 2 h of active time with no new
hand samples, set that key's prediction weight to 0 until the next hand move. Log a `MARK` when it
fires.

### 5.5 Confidence dot

Map `w1`: < 0.2 grey (defaults), < 0.6 dim green (your style from other patches), else full green (this knob).

## Traps

| Trap | Why |
|---|---|
| Hard rung switching | Behaviour jumps when a threshold is crossed. Blend. |
| Weighting acceptance | "Left it alone" is mostly inattention; weighting it closes the collapse loop in README §5. |
| Role by name only | `freq`/`amount` mean different things per category. Key roles by (category, name). |
| Size pooled in pixels | Canvases differ; pool as a canvas fraction. |
| Profile key by name only | Names repeat; include `paramIndex`. |

## Test: `INFINITE_PREDFEEDBACKTEST`

1. **Cold:** a fresh key with empty profile/role/family stats → a narrow landscape at its anchor (flat
   only with no anchor); the value stays inside the declared range.
2. **Warm profile:** train a profile on node type A in "patch 1"; spawn a fresh A node → `w2 > 0.5`
   and `p̂` has the profile's peaks.
3. **Role pooling:** train `filter.cutoff` on a Filter node whose cutoff dwells at 1 kHz; spawn a
   Wavetable with a different cutoff range → its `p̂` peaks at the fader position of 1 kHz *in its own
   range*, not at the Filter's fader position. A param with an unknown name gets `w2b = 0`.
4. **Your style:** train only one busy knob for 1 simulated hour; bind a never-touched knob →
   its θ is within 30% of the busy knob's, its p̂ peaks at its anchor, and it stays within ±0.1 of the
   anchor 95% of the time at Stray 1.
5. **Energy link:** Link = 1, a quiet knob; its variance over 10 s of simulated hand movement on the
   busy knob is ≥ 2× its variance over 10 s of silence. With Drift driving 5 knobs and no hand input,
   `HandEnergy()` stays 0 (no self-excitation).
6. **Role energy:** moving one opacity by hand raises `HandEnergy` of another opacity to ≥ 2× that of
   an unrelated gain.
7. **Role keying:** `freq` on a filter node → `filter.cutoff`; on an oscillator → `pitch.freq`;
   `amount` on a 2D node → no role. Fine tune trained on synth A (dwelling at +3 cents) gives a fresh
   synth B with a different range a p̂ peak at +3 cents.
8. **Anchor creep:** save and reload after 10 minutes of Drift; the anchors are unchanged.
9. **Collapse:** run Drift feeding only itself for 20 simulated hours at prediction weight 0.1 with
   no hand input. Entropy must not drop below 75% of its start, **or** the auto-cut must fire. Repeat
   at weight 1.0 to show the auto-cut catches the collapse.

## Exit criterion

`PREDFEEDBACKTEST` passes; readiness on a fresh install matches README §6's table within a factor of 2
on the owner's first real sessions (report the numbers in the merge commit).
