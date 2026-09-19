# Prediction modulator: design draft v2

Status: **design only, nothing built.** v1 written 2026-09-18 for outside opinions. v2 (same day)
folds in a code-grounded review, corrected formulas and a narrower v1 scope, and adds execution
briefs (step-01 … step-07) with code references checked against commit `35221c1`. Section 0 scores
both drafts so reviewers can see what changed and why.

## 0. Review scorecard (v1 draft → this draft)

Scores out of 10. "Lift" is what would raise the v2 score further.

| Axis | v1 | Why v1 scored that | What v2 changed | v2 | Lift |
|---|---|---|---|---|---|
| **Feasibility** | 4 | `Value01()` has no destination, so one green node could not hold a model per param; bindings map linearly in param units, not fader space; `ParamMailbox` cannot carry tables; modulated params are read-only, so the "grab to correct" loop had no input path | §2 specifies the side interface, fader-space mapping, override path, stable keys and threads, each checked against the code | 7 | Prototype §2.1 + §2.3 first; they change established modulation semantics and need the `modulation-sweep` skill run over them |
| **Innovation** | 7 | Learned modulation of arbitrary params is rare in node tools, but the pieces have prior art (Continuator, Wekinator) | One signature behaviour (§4.1: release → continue → settle → wander) plus personal priors that travel across patches (§6) | 8 | Condition the landscape on the session section (§7), so the same knob has different "homes" in verse and drop |
| **Design** | 5 | Six modes on one cable is mode soup, which contradicts the ~7-controls/one-mode rule for nodes; override and ghost line undefined | One node, one behaviour, five knobs + Freeze; grab-to-override specified; other modes moved to v2/v3 | 8 | Paper-prototype the ghost line and confidence readout against `node-ui-pillars` before code |
| **Experimentality** | 9 | Many bold ideas (session map, moves faders, predictive MIDI) | Keeps all of them, but gates each behind a measured baseline | 8 | Trade-off accepted: a little less wild in v1 for a v1 that ships |
| **Performance** | 3 | `MᵀM` at 10 Hz is ~5 GB/hour; brute-force Recall grows with the whole history; retraining cost unspecified; log estimate ignored keys and timestamps (~170 MB/h real) | O(1)-per-sample online statistics (§3.3), ~6 B events and a driven-param policy (§3.1: ~10–30 MB/h), a capped and downsampled session map on a worker thread | 8 | Measure the logger's frame cost with 400 params before shipping; budget ≤ 0.1 ms/frame |
| **Predictive / algorithmic** | 5 | OU σ taken from the wrong statistic; Langevin stationary law misstated; Follow correlates levels (spurious); NMF on signed deltas; DMD unstabilised; T→0 described as "replay"; no evaluation for param modes | All corrected in §4.2; held-out NLL gate against two baselines (§4.3); sources weighted explicitly (§5) | 7 | Run §4.3 on real owner logs; until then the numbers are designs, not results |
| **Overall** | **5.5** | | | **7.7** | |

## 1. The idea

Infinite already has three ways a knob can be driven by something other than the hand. Add a
fourth: a **Prediction** cable (green), dragged onto any param like a modulator, driving it with
values **generated from a model of that param's own history**. The history comes from an
always-on log of every param movement.

| Class | Colour | Behaviour authored by | Generalises to new situations? |
|---|---|---|---|
| Modulator | yellow | a fixed waveform | no |
| Expression | purple | a formula the user typed | no |
| Gesture recording | red | the user's performance, replayed exactly | no |
| **Prediction** | **green** | **a model learned from the user's log** | **yes** |

**The signature behaviour (v1):** let go of a knob and it *keeps going* the way your hand was
moving, *settles* into the places you usually leave it, then *wanders* among them at your usual
speed. One behaviour, learned per param, explained in one sentence.

## 2. How it fits the existing code (verified 2026-09-18)

### 2.1 One model per destination: a side interface

`IModulator::Value01()` takes no argument ([Modulation.h](../../../src/core/Modulation.h)), so every
destination of one modulator gets the same value. A prediction is a model of one param.

**Proposal:** a side interface, checked in the apply loop the way `MacroNumBoxNode` already is
(`ApplyModulationAndPalette`, `src/main.cpp`):

    class IPredictor {                        // implemented by the green node only
    public:
       virtual float ValuePos01For(const ParamKey& dst) = 0;   // fader position, 0..1
    };
    struct ParamKey { uint64_t nodeUid; int paramIndex; };   // name kept as metadata only

- One green node can drive many params; it holds one model slot per bound `ParamKey`.
- No other modulator changes. `Value01()` still exists (returns the first slot) so the matrix
  meter and existing plumbing keep working.
- **Idempotency** (`new-modulator-node` skill §2): `ValuePos01For` is a pure read. Models advance
  once per frame in the node's own tick, never inside the read, so driving 1 or 10 params gives
  the same motion.

### 2.2 Fader space, not value space

The apply loop writes `lo + (hi - lo) * v` in the param's own units, then `ShapeToParam` (snap +
clamp). Models learn in fader position (`valueToPos`), so on a log-scaled knob a linear write
would crowd predictions toward the top. For `IPredictor` bindings the loop maps in position space:

    posLo = valueToPos(lo); posHi = valueToPos(hi)
    value = posToValue(posLo + (posHi - posLo) * p)      then ShapeToParam

Params with no `valueToPos` (linear) reduce to today's formula.

### 2.3 Shift-grab to override (the correction path)

Today a patched param is drawn read-only (`ModSlider`'s `modulated` branch, `src/main.cpp`), and a
finished gesture loop is **locked against a plain grab**. Only Shift, or an explicit re-record arm,
drives it (`const bool locked = hasPlayback && !KeyShift ...`, `src/main.cpp` ~3777). Green bindings follow the
gesture convention, so an accidental grab never fights the model:

1. **Shift + drag** on a green-bound param draws the editable widget, and the apply loop skips that
   binding for as long as the item is active.
2. Every frame of the grab is logged `source = hand`, `flag = correction` (§5).
3. On release, the model reseeds at the new value with the hand's release velocity (§4.1, Continue).
4. A plain grab stays read-only, like every other cable.

This is a new rule in the modulation system; run the `modulation-sweep` skill after building it.

### 2.4 Stable keys

Key = **`GraphNode::uid` + `paramIndex`**. The name is stored as metadata, never as the key:
param names are **not unique within a node** (Wavetable has three `release` params; see the
`++found == 3` fixture in `ApplyModulationAndPalette`). `paramIndex` is a patch-file key already
("Parameter indices are patch-file keys", [GraphNode.h](../../../src/core/GraphNode.h)). `uid` exists,
is saved, and is restored by `ApplyPatchData`. `nodeIndex` is **not** stable: `ApplyPatchData`
respawns every node with fresh indices on undo (commit `5699b6c`). Consequences:

- The log and the statistics store use `(uid, paramIndex)` only. Cross-patch priors (§6 rung 2) use
  `(nodeType, paramIndex, name)`.
- Because the learned statistics are keyed by uid, **undo needs no remap**: the green node holds only
  live simulation state (x, v, RNG), rebuilt lazily. Duplicate/paste mints a new uid, so the copy
  starts at rung 2 (its node type's profile), which is already warm.
- Collapsed nodes register their params **only if something drives them**
  (`registerOnlyParams`, `src/main.cpp` ~81929), and params hidden behind a mode switch never register.
  The log is change-based, so a param nobody can see or drive has nothing to log: no holes.
  A green binding sets `hasModulatedParams`, so a collapsed green-driven node keeps registering.

### 2.5 Threads

| Thread | Does | Never does |
|---|---|---|
| Main | capture log rows into a preallocated ring; O(1) online-statistics updates (§3.3); tick v1 models; the UI | disk I/O, heavy fits |
| Worker (new, one) | drain the ring to disk; heavy fits (Follow, Recall index, session map); build MIDI tables | touch `ParamRef::value` pointers |
| Audio | read the predictive MIDI node's tables | allocate, lock, free |

- Ring → worker: bounded queue that **drops under backpressure** rather than blocking the frame.
  Precedent: the recorder's worker queue (commit `436bd62`).
- Worker → audio (MIDI tables only): double-buffered, published with an atomic pointer swap; the
  retired buffer is freed on the main thread. `ParamMailbox` is 128 smoothed float slots and cannot
  carry a table.

## 3. The data

### 3.1 The log

One row per param change, captured at the end of `ApplyModulationAndPalette`, where every writer
has already landed:

    (t, key, pos01, source, flags)

- `pos01` = `valueToPos` of the effective value. Enum/bool params are logged but excluded from the
  continuous maths.
- `source` ∈ {hand, modulator, expression, gesture, perf-matrix, prediction, other}; `flags` ∈ {correction}.
- Structure events (add/delete node, cables) are not logged, except **binding events** (bind,
  unbind, lo/hi change) for driven params; see the policy below.

**What is tracked (complete list):**

| Record | Content | When |
|---|---|---|
| `KEY` | id, uid, paramIndex, node type, param name, min/max, isEnum/isBool, has fader curve | first time a key is seen in a file |
| `VAL` | id, Δt, pos01 (uint16), source, flags | on change; driven params at most 10 Hz |
| `BIND` | id, bind / unbind / range change, driver kind, lo, hi | when it happens |
| `TRANSPORT` | playing, bpm, beats, beats-per-bar | on change, and once per bar while playing |
| `MARK` | session start/end, patch loaded / new / undo / redo | when it happens: values that jump because of these are **re-baselined, not logged as moves** |

`source` is decided by elimination in the apply loop: a param that changed with no binding, expression,
gesture or perf-matrix writer this frame is `hand`. Not logged: node/cable edits, audio, file paths,
anything during offline render or a self-test run. A Settings checkbox pauses logging (default on) and
"Open log folder" sits next to it (owner to confirm; see §12).

**Encoding:** a per-file key table (uid+name → varint id), then events as
`varint id · varint Δt (ms) · uint16 pos` ≈ **5–6 bytes**. Hand and perf-matrix moves are logged
on change at full rate. The worker flushes every few seconds. Closed session files are deflated with miniz (already vendored), and the folder has a size cap (default 1 GB) that deletes the oldest raw files only after their statistics are in `stats.bin`: see step-01 §1.3b.

**Driven-param policy.** A param driven by a modulator or expression changes every frame but carries
no information about the user. Its motion is determined by the binding. Log it **decimated to 10 Hz**
plus its binding events. It trains at low weight (§5).

**Size (estimate, not measured):** 5 hand-moved params × 60 Hz × 6 B ≈ 1.8 KB/s while playing; 100
driven params × 10 Hz × 6 B ≈ 6 KB/s. Worst case ≈ **8 KB/s ≈ 30 MB/hour**, far less when idle. v1's
12 KB/s figure omitted keys and timestamps (real cost of v1's scheme ≈ 170 MB/hour).

### 3.2 The matrix

    M ∈ R^(P × T)   rows = params (keys), columns = analysis steps, with a mask for "param absent"

P changes whenever nodes are added or removed, even within a session. Rows are aligned by key; a
missing param is masked (not zero) and every analysis below uses the mask. The dense grid is 10 Hz
for fits and **one column per bar** (1 s when the transport is stopped) for the session map.

### 3.3 Online statistics (the performance core)

v1 needs **no retraining pass**. Each param keeps running sums with exponential forgetting
(half-life H, default 2 weeks of *playing* time). Each update is O(1):

| Statistic | Kept as | Gives |
|---|---|---|
| AR(1) sums over the 10 Hz grid | `W, Σx, Σy, Σx², Σxy, Σy²` (x = x_t, y = x_{t+1}) | φ, c, residual variance → θ, μ, σ (§4.2) |
| Dwell landscape | 64-bin histogram of pos01 | `p(x)`, sweet spots |
| Observed range | the same histogram | 2nd–98th percentile |
| Release velocity | last 100 ms of hand motion before release | Continue seed |
| Confidence | `n_eff = Σ weights` | cold-start rung (§6), UI readout |

Forgetting also bounds how far any feedback loop can drift (§5). Heavier features (coupling,
rhythm, order) are v2 and are computed on the worker.

## 4. Models

### 4.1 v1: the one behaviour, "Drift"

Per bound param, per frame (dt = frame time):

    v ← v · e^(−dt/τ_m)                                          (Continue: release momentum)
    x ← x + v·dt − η·U′(x)·dt + σ·√dt·N(0,1)                     (Home + Wander)
    reflect x at the observed range (fallback: [0, 1])
    U(x) = −log(p̂(x) + ε),  p̂ = Gaussian-smoothed dwell histogram

- **Stationary law.** With reflecting walls the long-run density is ∝ `p̂(x)^(2η/σ²)`. Define
  **Stray** `T = σ²/(2η)`: T = 1 reproduces your dwell habits; T < 1 sticks harder to sweet spots;
  T > 1 roams wider. That makes Stray an exact temperature, not just a label.
- **Time scale.** η is set so the relaxation time matches the param's own AR(1) `1/θ` (§4.2), and the
  **Speed** knob scales it. Your twitchy knobs drift fast; your slow ones drift slow.
- **Stability.** Cap the drift step `|η·U′·dt| ≤ 0.05` per frame; smooth p̂ enough that U′ is not
  spiky. Euler–Maruyama is sufficient at frame rate with those caps.
- **Cold start.** With no data on this knob, speed and momentum come from **your style** (rung 2c,
  learned on the knobs you do move), and p̂ is a narrow peak at the knob's **anchor**, the value you
  last set by hand (rung 4, §6). A quiet knob makes small moves in your rhythm around a place you
  chose, not a random walk over its whole range.
- **Energy link.** `T_eff = Stray · (1 + Link · E)`, where `E ∈ [0,1]` is how actively *you* are moving
  right now (§6). Quiet knobs wake up while you play and settle when you stop.

**Controls (6 + Freeze, one mode):**

| Control | Does |
|---|---|
| **Speed** | time-scale multiplier on the learned θ |
| **Stray** | temperature T (0.25 … 4, default 1) |
| **Momentum** | τ_m: how far a release carries on (0 = off) |
| **Depth** | lo/hi of the binding (already exists per binding) |
| **Link** | 0…2, default 0: how much your live hand activity raises Stray (energy link, §6) |
| **Seed** | fixed random seed, so a take is reproducible |
| **Freeze** | snapshot the learned profile into the patch (§9) |

Plus a read-only **confidence** dot (n_eff), per `node-ui-pillars`.

### 4.2 All modes, corrected formulas and release stage

Notation: `x_t ∈ [0,1]` is fader position; `dt` the step.

| # | Mode | Model | Corrected formula / rule | Stage |
|---|---|---|---|---|
| 1 | **Continue** | damped momentum | `v ← v·e^(−dt/τ)`; per-*second* rate, not per-step λ, so 30 and 60 fps agree; total travel = v·τ | v1 (inside Drift) |
| 2 | **Wander** | Ornstein–Uhlenbeck | exact step `x′ = μ + (x−μ)e^(−θdt) + σ√((1−e^(−2θdt))/(2θ))·N(0,1)`. Fit AR(1) `x_{t+1} = c + φx_t + e`: `φ = e^(−θdt)`, `μ = c/(1−φ)`, **`σ² = var(e)·2θ/(1−φ²)`** (σ from residuals, not dynamic range). Clamp φ ∈ [0.5, 0.999]: dwell-heavy knob data pushes φ → 1, where μ is ill-conditioned. If used standalone, run it in logit space so bounds need no clamping | v1 (supplies θ to Drift) |
| 3 | **Home** | Langevin on the dwell landscape | `dx = −η U′(x) dt + σ dW`, stationary ∝ `p^(2η/σ²)`. Only T = 1 reproduces p. Uniform-grid samples are already dwell-weighted: **do not** weight again | v1 (inside Drift) |
| 4 | **Follow** | lagged ridge regression | `x_B(t) = Σ_j β_j x_j(t − τ_j) + c`. Choose τ_j by cross-correlating **velocities** Δx (levels give spurious coupling between any two drifting knobs); require τ_j ≥ 0 | v2 |
| 5 | **Recall** | nearest-neighbour segment / variable-order Markov | Index per-bar summary vectors (mean + slope per param, masked); search the index, not raw history; compare only params present in both windows | v2 |
| 6 | **Play like me** | PCA dynamics / ProMP | `A = H₂·pinv(H₁)` (exact DMD) then **scale A so its spectral radius ≤ 1** or it blows up. ProMP conditioning unchanged | v3 (research) |

### 4.3 Evaluation: a mode ships only if it beats the baselines

Per param, hold out the last 20% of the log. Score the negative log-likelihood of the next 1 s
against two baselines:

- **B0 hold:** the value stays where it is.
- **B1 range:** uniform over the observed range.

A mode is enabled for a param only if it beats both; otherwise the green cable falls back one
cold-start rung. The same number drives the confidence dot. This mirrors the MIDI node's
information-content meter (§8.3), so both halves share one evaluation idea.

## 5. The feedback loop (training on its own output)

Predicted values are written into params and logged. Training on them teaches the model its own
output. The mechanism of concern is the one in Shumailov et al. (Nature 2024): recursive training
loses the tails. That paper is about large generative models, so this is an analogy by mechanism.

**The loop most likely to collapse here is Home, not Wander:**

```
Drift sits near a sweet spot ──► logged as dwell there
        ▲                               │
        └── landscape gets sharper ◄────┘      (each pass, T effectively < 1)
```

**Training weights per source** (training-time settings; the raw log keeps every tag, so any weight
can change later without losing data):

| Source | Weight | Why |
|---|---|---|
| hand, perf-matrix | 1.0 | the user, directly |
| hand + `correction` (grabbed a green param) | 3.0 | DAgger-style: the expert corrected the learner |
| gesture playback | 0.2 | a replay of a hand take already logged at 1.0 |
| modulator, expression | 0.1 | determined by the binding, not the user |
| prediction | 0.1 (owner's call: keep the data rich) | own output |
| other (preset / randomize / reset button: a jump with no active widget) | **0** | not a gesture; logged so the jump is explained, never learned |
| prediction left untouched ("acceptance") | **0** | mostly absent attention, not approval, and weighting it closes the loop above |

**Monitors** (per param, over playing time): dwell-histogram entropy, Wander σ, and the share of
`n_eff` coming from prediction rows. **Auto-cut:** if entropy falls by more than a set fraction over
a window with no new hand data, the prediction weight for that param drops to 0. Forgetting (§3.3)
bounds drift regardless.

## 6. Cold start: a fallback ladder

Logs exist only from the day the logger ships. Patches saved earlier are not used (owner's decision).

| Rung | Data | Gives |
|---|---|---|
| 1 | this param's own log (this uid) | everything |
| 2 | the same `(nodeType, paramName)` in the user's other logged patches | range, dwell, speed: **your style travels between patches** |
| 2b | the same **param role** on *any* node type: e.g. `filter.cutoff` pools an Oscillator's cutoff, a Filter's, and a plugin's "Filter Freq" | range, dwell, speed: **"cutoff is cutoff"** |
| 2d | the same **role family**: e.g. `pitch` pools freq, coarse, fine tune, detune; `size` pools size, scale, radius, width, height | speed (θ) and range; landscape only where the family shares a unit |
| 2c | **you**: pooled over *all* your hand-moved knobs, in fader space | speed (θ), momentum, pause lengths: **how you move**, never where |
| 3 | opt-in community prior, same key or role | range, dwell, typical speed |
| 4 | the knob's **anchor** (last hand-set value, else its value at bind time) inside declared min/max | a narrow landscape around the anchor; flat only if there is no anchor |

Order of strength: 1 → 2 → 2b → 2d → 2c → 4. Each level is weaker than the one before it.

**Param roles (rungs 2b, 2d).** A hand-kept alias table in `src/core/ParamRoles.h`, case- and
space-insensitive, keyed by **(node category, normalised name)**. The category key stops generic names
from pooling nonsense: `amount` on a 2D node is not `amount` on a reverb, and `freq` on a filter is a
cutoff while `freq` on an oscillator is a pitch. It is built from the labels Infinite actually uses
(a grep of the `Mod*` widgets finds `width`/`height` ×16, `gain` ×9, `radius` ×8, `opacity` ×6,
`scale` ×4, `mix` ×3 …), plus plugin aliases. Unknown names get no role.

| Family | Roles (examples of names) | Pooled in |
|---|---|---|
| pitch | `pitch.freq` (freq on oscillators), `pitch.coarse`, `pitch.fine` (fine, fine tune), `pitch.detune` | semitones / cents |
| filter | `filter.cutoff` (cutoff, freq on filters, filter freq), `filter.resonance` (res, q) | log₂ Hz, 0–1 |
| level | `level.gain` (gain, volume, level, output) | dB |
| mix | `mix` (mix, dry/wet, wet; `amount` on audio FX only) | % |
| env | `env.attack/decay/sustain/release` | log seconds |
| opacity | `opacity` (opacity, alpha, bg opacity) | 0–1 |
| size | `size.size`, `size.scale` (scale, scale x/y/z), `size.radius`, `size.extent` (width, height), `size.point` | fraction of the canvas / range, never pixels |
| transform | `xf.rotation` (rotation, angle, spin), `xf.pos` (x, y, z, pos x/y) | degrees; fraction of the canvas |
| colour | `colour.hue`, `colour.brightness`, `colour.saturation` | native units (hue is circular) |
| time | `time.rate` (rate, speed, rate (beats)) | log; per beat when tempo-synced |

**Rung 2d (family)** pools across the roles of one family with an even larger `N0` (6·N0). It mainly
shares **speed and range**; the landscape is shared only where the family has one unit (pitch in
cents, size as a canvas fraction).

- **Pool in physical units, not fader position.** Fader position 0.5 means different Hz on a 20 Hz–20 kHz
  knob and a 50 Hz–5 kHz one. A role with a known unit (cutoff → log₂ Hz, gain → dB) stores its histogram in
  that unit; to use it for a target knob, map it through the target's own min/max and `valueToPos`, and
  drop and renormalise any mass outside the target's range. Roles without a unit pool in fader space.
- **A weak prior, on purpose.** Same name can mean different things (a high-pass vs a low-pass cutoff;
  `mix` on a reverb vs a delay). The role level gets a larger `N0` than rung 2, so a few minutes of the
  knob's own data overrides it. Speed (θ) pools more safely across roles than the landscape does.
- **Not the same as Follow.** Pooling answers "how do I usually set cutoffs". "When I move this cutoff I
  also move that one" is coupling, i.e. Follow (§4.2 mode 4, step 7a).

**Rung 2c: your style (how, not where).** Timing traits belong to your hand, not to a knob: how fast
you move, how long you pause, whether you flick or sweep, how soon a value settles. They are pooled
across every hand-moved key in fader space and used **only** for θ, Momentum and the release shape,
never for p̂. One busy knob is enough: a few hundred moves in an hour makes this prior warm after about
**one session**, and every quiet knob gets it at once.

**Rung 4: the anchor.** A knob you never move is one you are happy with, so where you left it is data.
The **anchor** is the last hand-set value for this key (from the log), else its value when bound. It is
never updated by predictions, and is saved in the node (`anchors`) so reloading does not creep. p̂ is a
Gaussian of width 0.05 (fader space) at the anchor, blended in with weight `N0/3`. The first real
touches outweigh it quickly, since corrections count ×3.

**Energy link (live, not learned).** Per knob, `E = 0.7·E_role + 0.3·E_all`. `E_all` = the exponential
moving average (τ ≈ 1 s) of your hand speed over all keys, divided by your typical move speed from rung
2c, clamped to [0,1]; `E_role` is the same over keys **of this knob's role in this patch**. Moving one
opacity wakes the other opacities strongly and everything else a little. Only `Hand`
and `Perf` sources count; predictions never do, so Drift cannot excite itself. The knob being
Shift-grabbed is excluded. Speed has no sign, so Link needs no data about whether knobs move together.
That is Follow (step 7a), which needs co-movement history.

**Role groups: a head start for coupling (per patch).** Params in the **same patch** with the **same
role** form a group (all opacities, all fine tunes, all FX mixes). A group is a set of *candidate*
pairs for Follow (7a) and a starting basis for the "Your moves" faders (7d). The **sign starts
unknown**, not positive, because a crossfade moves two opacities in opposite directions. It is
learned from the first few co-moves; a candidate that never co-moves within ~30 min of active time is
dropped. Habits (kind A: where a role sits) still pool across patches; coupling (kind B: what moves
together) never does, since two patches share no knobs.

**How long until a knob "feels like you" (estimates, not measured; the §4.3 gate decides).** Count
*moves*, not rows: at φ ≈ 0.99 on the 10 Hz grid, `n_eff ≈ n(1−φ)/(1+φ)` is about one independent
sample every 20 s.

| Stage | Needs | Active time on that knob |
|---|---|---|
| your range | ~20–30 grabs | ~5 min |
| your speed on a knob you rarely touch | borrowed from rung 2c (your busy knobs) | ~1 session on *any* knob |
| your speed | relative error of θ ≈ √(2/(θT)); ±20% at T ≈ 60/θ | ~10 min (for a 10 s settle time) |
| release momentum | ~10–20 releases | ~5–10 min |
| your sweet spots | ~10–20 rests per spot | ~15–30 min |
| passes the gate | a meaningful 20% holdout | ~30 min |

Rung chosen by `n_eff` and by the §4.3 gate. Blend adjacent rungs by `n_eff` rather than switching,
so behaviour does not jump when a threshold is crossed.

## 7. Session map and "your moves" (v2)

- **Session map:** columns = one per bar (1 s when stopped), each column L2-normalised over its
  unmasked params; similarity = **cosine**, not raw `MᵀM` (a raw dot product makes loud frames
  similar to everything). Cap at 2048 columns (16 MB as float32); beyond that, block-average. Computed
  on the worker. Sections from Foote's checkerboard novelty. Shown as **labelled sections with
  returns** (A B A′ C), not a raw heatmap, for non-technical users.
- **Your moves faders:** PCA on **deltas** ΔM (signed), so `p = p_now + W·δh` is well defined.
  NMF needs non-negative input: use it only on levels, for "which params belong together" views.
- **Lift (innovation):** condition Drift's landscape on the current section, so a knob can have one
  home in the verse and another in the drop.

## 8. Predictive MIDI: a note-family node

A note node (`INoteSource`, emits `NoteEvent`s) that predicts notes instead of values. Its own node,
because its output is notes. Build it with the `new-audio-node` skill (two-object rule).

### 8.1 Two sources

| Source | How | Result |
|---|---|---|
| **A. Learn from notes (the hero)** | wire a note chain in, press **Learn** | captures, models, then plays standalone in that style |
| **B. Follow my movement** | reads the param log / Drift output (no cable) | movement → notes, deterministic mapping below |

| Note attribute | From the log |
|---|---|
| onset | reversals and speed peaks of a tracked param; session-map section changes are accents |
| pitch | scale-quantised value of a chosen track, or of the first PCA axis h₁ |
| velocity | movement speed, normalised |
| duration | dwell before the next movement |
| pitch bend | fine residual of the track |
| chord size | how many tracks move at once |

### 8.2 What is learned (source A)

Multiple-viewpoint variable-order Markov models (Conklin & Witten 1995; the basis of IDyOM), orders
blended PPM-style (Cleary & Witten 1984). `NoteEvent` today carries `note`, `velocity`, `isNoteOn`,
`frameOffset`, `source`, `voiceId`, `bendSemitones`/`bendUpdate`
([NoteEvent.h](../../../src/audio/NoteEvent.h)). `frameOffset` is only within the block: onsets are
`block start + frameOffset`, converted to transport beats.

| Viewpoint | Derived from | Alphabet |
|---|---|---|
| pitch interval + register | `note` | interval −12..+12, octave band |
| onset spacing | absolute onset → beats | 1/16 grid + micro-timing residual |
| duration | off − on | ratio to spacing, 4–8 bins |
| velocity | `velocity` | 6–8 bins, conditioned on beat position |
| bend contour | `bendSemitones` | few contour shapes |
| chord | simultaneous notes | interval stack |
| metric position | beat in bar | 16 values (context) |

**Timbre per note (decided direction):** pair each note-on with the param values active at its onset,
taken from the log. It needs no change to `NoteEvent`.

### 8.3 How much to learn: measure, do not guess

**Learn** shows a live meter: predict the held-out last 20% and report cross-entropy against an
order-0 baseline. It stops when the curve plateaus. Starting points (counting arguments, not
measured):

| Learns | Notes (rough) |
|---|---|
| pitch set + rhythm feel (order 0) | 16–24 |
| melodic tendencies (order 1–2) | 60–120 |
| phrase memory (order 3–4) | 200+, or 2–3 loop repeats |
| a deterministic loop | exactly 2 periods |

The window is counted in bars. Default: 8 bars or 64 notes, whichever is later, ending early on
plateau. **Offline Learn:** also accept a sequencer's pattern read directly (no playback): it is
cheaper and gives cleaner data.

### 8.4 Controls (6, one mode)

| Control | Does |
|---|---|
| **Learn** | capture, then run standalone |
| **Stray** | temperature on the blended distribution: `p_i ∝ p_i^(1/T)`, renormalised. T = 1 faithful; high → uniform within Range. **Low T is not "replay"**: greedy decoding loops on the likeliest short cycle. At the bottom of the knob, switch to longest-exact-context replay instead |
| **Memory** | maximum context order (and what "replay" means at Stray = 0) |
| **Length spread** | duration variability |
| **Velocity spread** | velocity variability |
| **Range** | lowest to highest note |

Threads: learning on the worker; tables to audio by atomic pointer swap (§2.5); events scheduled by
`frameOffset`.

## 9. Trust and control

- **See:** a ghost line of the next ~2 s on the knob (Drift is cheap to roll forward with the fixed Seed).
- **Override:** grab the knob (§2.3). The grab is logged as a correction.
- **Freeze:** stores the learned profile in the patch (64-bin histogram + θ, σ, τ_m, T, seed ≈ 150
  bytes per binding), so a shared patch behaves identically elsewhere. Unfrozen state is derived data
  in the log folder, not in the patch.

## 10. Logs and sharing

- Stored in the app-data folder (macOS Application Support, Windows AppData, Linux XDG data dir).
  Every path is a three-way `Platform::` obligation (`windows-parity`, `linux-parity` skills).
- **Upload is opt-in, default off.** "Share my log" sends a **pseudonymised** log (uids remapped, no
  file paths, no audio) to a private endpoint. Node types, param names and timing together can still
  identify a person, so do not call it "anonymous". No token-in-client GitHub writes.

## 11. Build order

Each step is its own branch (`git-branch-workflow`) with a self-test (`run-infinite-hygiene`).
Each has a self-contained execution brief with code references: [step-01](step-01-movement-log.md),
[step-02](step-02-online-stats-and-eval.md), [step-03](step-03-predictor-binding.md),
[step-04](step-04-drift-node.md), [step-05](step-05-cold-start-and-feedback.md),
[step-06](step-06-predictive-midi.md), [step-07](step-07-v2-modes.md).

| Step | Scope | Exit criterion |
|---|---|---|
| 1 | Logger: ring buffer, uid keys, encoding, worker writer, driven-param policy | ≤ 0.1 ms/frame with 400 params; the log survives undo/duplicate/paste with correct keys |
| 2 | Online statistics (§3.3) + an offline §4.3 evaluator script run on the owner's logs | Drift beats B0 and B1 on most hand-moved params, or the plan is revisited |
| 3 | `IPredictor`, fader-space apply, grab-to-override, undo remap of model slots | `modulation-sweep` passes; no existing modulator changes behaviour |
| 4 | Green node: Drift, 5 controls + Freeze, confidence dot, ghost line | `new-modulator-node` exit criterion; `node-ui-pillars` checklist |
| 5 | Cold-start rungs 2 and 4, feedback weights, monitors | Rung blending shows no jumps; the auto-cut triggers in a synthetic test |
| 6 | Predictive MIDI node (source A) | Held-out meter works; no allocation on the audio thread |
| 7 | v2: Follow, Recall, session map, moves faders, section-conditioned Drift | each passes the §4.3 gate |

## 12. Questions for reviewers

1. **Owner decision:** `IPredictor` side interface (recommended, §2.1) vs. one green node per param?
2. Shift-grab to override (§2.3) matches gesture loops. Is that enough, or should a plain grab work too?
6. **Owner decision:** logging pause checkbox + "Open log folder" in Settings (§3.1), given "always on"?
7. **Owner decision:** the default theme already colours the Modulators category lime (`#A3E635`,
   `CategoryColors.cpp`). Pick a Prediction green that reads as different (e.g. teal), or give it a new
   category colour.
3. Drift folds Continue, Home and Wander into one SDE. Is one behaviour enough for v1, or does Follow
   belong in v1 too?
4. Forgetting half-life: 2 weeks of playing time is a guess. What should it be tied to?
5. Prior art we have missed? (Wekinator is now listed; is there learned-automation work in DAWs?)

## 13. References

- Conklin and Witten, *Multiple Viewpoint Systems for Music Prediction*, 1995: <https://www.ehu.eus/cs-ikerbasque/conklin/papers/jnmr95.pdf>
- IDyOM (Pearce): <http://mtpearce.github.io/idyom/>
- Cleary and Witten, *Data compression using adaptive coding and partial string matching* (PPM), IEEE Trans. Communications, 1984 (not fetched)
- Pachet, *The Continuator*, 2003: <https://www.francoispachet.fr/the-continuator-musical-interaction-with-style-pachet-2003/>
- Fiebrink, *Real-time Human Interaction with Supervised Learning Algorithms for Music Composition and Performance* (Wekinator), PhD thesis, Princeton, 2011 (not fetched): interactive ML with the user correcting the model, the closest prior art to §2.3/§5
- Paraschos et al., *Probabilistic Movement Primitives*, 2013: <https://www.ias.tu-darmstadt.de/uploads/Publications/Paraschos_Humanoids_2013.pdf>
- Foote, novelty via self-similarity, 2000: <https://www.audiolabs-erlangen.de/resources/MIR/FMP/C4/C4S4_NoveltySegmentation.html>
- Schmid, *Dynamic mode decomposition*, 2010: <https://www.cambridge.org/core/journals/journal-of-fluid-mechanics/article/dynamic-mode-decomposition-for-analysis-of-timeseries-data/0C805EA6FDDE25CB823AFB8CC69B64AF>
- Gillespie, *Exact numerical simulation of the Ornstein–Uhlenbeck process and its integral*, Phys. Rev. E 54, 1996 (not fetched)
- Shumailov et al., *AI models collapse when trained on recursively generated data*, Nature 2024: <https://www.nature.com/articles/s41586-024-07566-y>
- Ross et al., DAgger (overview): <https://arxiv.org/pdf/1811.06711>
- OU parameter estimation from discrete data: <https://arxiv.org/pdf/1608.04507>
