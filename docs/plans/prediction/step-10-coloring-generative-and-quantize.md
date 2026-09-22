# Prediction step 10: make Predictive Coloring generative, then build Predictive Quantize

Status: **prompt for a future session — nothing in this doc is built.** Written 2026-09-22 as the
handoff after a classification sweep of all 5 Prediction (green cable) nodes found one that
doesn't belong in the category yet, plus one design that's ready to build but never got built.

Read `docs/plans/prediction/README.md` §8.2 and the 10-axis taxonomy card in memory
(`project_prediction_module_taxonomy.md`) before touching either task. Every other Prediction node
(Drift, Moves, Predictive Modulator, Predictive Notes) was scored against those 10 axes during this
sweep; both tasks below exist to bring the last two members of the category up to that same bar.
When you believe a task is done, write out the 10-axis scorecard for the node and check it against
the other four's scores — don't ship on a partial pass.

Branch per `git-branch-workflow`: these are two unrelated features touching disjoint files: cut
`feature/prediction-coloring-generative` for Task A and `feature/prediction-quantize` for Task B
(don't do both on one branch).

---

## Task A — Predictive Coloring: closed-form solver → genuinely generative

### A0. Why this node fails the bar today

`ColorStats::Fit()` (`src/core/ColorStats.cpp`, declared `src/core/ColorStats.h:187`) is a
**deterministic closed-form regulator**: given the live frame's `HistogramSet` and the learned
`Profile`, it *solves* for the 13 `ColorGradeParams` that would nudge this frame's moments toward
the target's moments, every frame, from scratch. There is no state that evolves on its own.
Unplug the video input and the node produces nothing; freeze the input and `Fit()` returns the same
answer forever.

Compare the other four:
- **Drift** — an Ornstein-Uhlenbeck SDE with its own position/velocity state; keeps moving even if
  you stop looking at it, wanders around a learned mean, never exactly repeats.
- **Moves** — PCA'd Δpos covariance driving an OU gesture fader; same "let go and it keeps going"
  property.
- **Predictive Modulator** — DMD-fit linear operator with a spectral-radius clamp; free-runs the
  fitted dynamics forward.
- **Predictive Notes** — variable-order Markov model; *samples* a next note from a learned
  distribution, doesn't solve for "the" note.

The common thread: all four have **autonomous state that evolves via a stochastic or dynamical
process fit to learned data**, and *sample or integrate forward in time* rather than algebraically
solving the current instant. Coloring must gain the same property to belong in the same category
as a node, not just visually (green cable, `Confidence01()`, Learn button).

### A1. What "generative" means here, concretely

Don't reinvent Coloring's math — its closed-form grade-fit (§3.2 of
`docs/plans/prediction/step-08-predictive-coloring.md`) is good and should stay as the
**equilibrium point**, exactly the role the learned mean plays in Drift's OU process. What's
missing is the wander around it.

Add a second, small OU process **in `ColorGradeParams` space** (16 scalar dims), anchored at
`Fit()`'s output as its equilibrium, with:

- **Mean-reversion target**: `Fit(live, target, selfNormalize)`, recomputed each rate-limited
  analysis tick (already gated by `sampleRate`, §2 of step-08) — this is the "where it's pulling
  toward," analogous to Drift's learned mean.
- **Noise amplitude per dimension**: derived from the learned distribution's own spread — reuse
  `Profile::TargetStdY()` / the histogram spread already computed for each channel (§3 of
  `ColorStats.h`) rather than inventing a new stat. A profile that has only ever seen one flat grey
  card should wander less than one that's seen a huge variety of footage; that's a Confidence-like
  signal already present (`Confidence01()`), so scale wander amplitude by `(1 - Confidence01())`
  as well as by the learned spread — more confident profiles produce tighter, more purposeful
  wander; brand-new profiles shouldn't wander wildly with no data to justify it.
- **One user-facing knob**: `wander` (0..1, default small, e.g. 0.15), following
  `feedback_audio_node_minimalism` — this node already has Learn + Mix; adding a `wander` knob is
  the *one* new control, not a menu of OU parameters (theta/sigma stay internal constants tuned by
  eye, same as Drift's internal timescales aren't exposed either — check `src/nodes/DriftNode.*`,
  or whatever the source file for Drift now is, for the precedent on what's exposed vs. internal).
- **Time-step integration**: sample this like Drift already does — the wander state advances once
  per cook/frame using `Transport`-derived dt, mean-reverting toward the `Fit()` equilibrium,
  clamped to each param's valid range (`ColorGradeParams` has no explicit bounds struct today —
  add sane clamps, e.g. `contrast` > 0, `saturation` >= 0, hue wraps mod 1 rather than clamping).

Net effect: instead of "always the exact same grade for the exact same frame," two runs of the
same footage produce visibly different (but plausible, learned-distribution-bounded) grades, and
holding on one frame doesn't freeze the output — it keeps gently drifting among looks the profile
has actually seen. That's the generative property the other four have and this one currently
lacks.

### A2. Where the state lives (two-object pattern)

Follow the same split every other Prediction node uses:
- **`PredictiveColoringNode` (INode, main thread)**: owns Learn state, the `ColorStats::Profile`
  accumulation, and rebuilding whatever the audio/render-thread-visible baked equilibrium is.
- **The render-thread side** (`AudioPredictiveColoringNode` if it doesn't already exist as a
  distinct class — check `src/nodes/PredictiveColoringNode.*` first; it may currently be a single
  INode doing everything on the cook thread since this is a video not audio path) owns the OU
  wander integration per frame and must not block on the Profile's histogram math.
- Cross the OU **equilibrium target** (16 floats) from main thread to the per-frame integrator the
  same way Drift/Moves cross their tables: atomic pointer swap of a small POD struct, freed only
  after the consuming thread has provably moved past the previous one. Do not take a lock on the
  per-frame path.

### A3. Confidence, cold start, determinism (axes 8–10 of the taxonomy)

- **Cold start**: with `TotalSamples() == 0`, `Fit()` already has defined defaults (`ColorGradeParams`
  identity values). Wander must default to *zero amplitude* until there's at least some learned
  spread — a freshly-loaded node with no Learn pass yet should output the identity grade exactly,
  not a random wander around identity. Gate wander amplitude on `HasLearnedData()`.
- **Confidence**: `Profile::Confidence01()` already exists — reuse it for the wander-amplitude
  scaling in A1, and surface it in the UI badge the same way Predictive Notes does, gated the same
  defensive way this session just added to `PredictiveNotesNode::Confidence01()` (don't show a
  confident-looking number for a state that hasn't actually learned anything).
- **Determinism**: the wander process needs a `seed` for reproducible test runs (see A4) — follow
  `PredictiveNotesNode::seed`'s precedent (`src/nodes/PredictiveNotesNode.h:52`), a saved int param
  seeding a local RNG, not `rand()`/global state.

### A4. Test plan

Add `INFINITE_PREDCOLORTEST` coverage (the env var already exists per this session's test run —
confirm what it currently covers, then extend it) for:
- Same seed + same input footage → identical wander trajectory (determinism).
- Zero learned data → zero wander, exact `Fit()` output (cold start).
- Wander stays within each param's valid range over a long synthetic run (clamp correctness).
- `wander = 0` reproduces the old (pre-this-change) deterministic behavior exactly, so existing
  patches that never touch the new knob don't change appearance.

### A5. Docs/help

Update whichever help table(s) describe Predictive Coloring (see `project_node_help_coverage`
memory — check both the full descriptive table and the shorter search/spawn-menu table in
`src/main.cpp`, the same two tables this session already touched for Moves) to mention the new
`wander` control in one line. Keep it short — this is a help string, not documentation.

---

## Task B — Predictive Quantize: build it from the existing design

Full design already exists at `docs/plans/prediction/step-09-predictive-quantize.md` (read it in
full before starting — 153 lines, nothing summarized here should substitute for reading it).
Nothing is built yet: `ls src/nodes/ | grep -i quant` finds only the existing fixed-grid
`QuantizerNode`/`AudioQuantizerNode` (`src/nodes/NoteNodes.h:453`, `NoteNodes.cpp:1429`); there is
no `PredictiveQuantiz*` anywhere in `src/`.

Key settled decisions from step-09 (do not re-derive or re-litigate these — they're settled):
- **Reuses `NoteModel::kIoi`** (`audio/NoteModel.h:29`) and `SnapTicks` rather than building a new
  onset-spacing representation — groove/template quantizing is a marginal histogram over the
  existing IOI viewpoint, not a new model class.
- **`mix` param for partial correction**, not `div` (a fixed grid divisor) — this node nudges notes
  toward the learned groove, it does not hard-snap to a grid; that distinction is also what
  separates it from `QuantizerNode` in the UI/help text (§ in step-09 on distinguishing the two —
  make sure whichever help table entry you add makes this distinction explicit, since a user
  searching "quantize" will see both).
- **Per-patch persistence**, not global/cross-session like `MovementStats`/`ColorStats` — re-check
  step-09's rationale for this before assuming otherwise; it's a deliberate axis-6 (Storage)
  decision, not an oversight.
- **Two-object audio-node pattern**, atomic swap of a small (~4-entry) mode-set array — same shape
  as `PredictiveNotesNode`'s table swap, just a much smaller payload.
- **5 traps** listed in step-09 §5 — read and avoid all 5 explicitly; don't rediscover them by
  shipping and then debugging.
- **Proposed test plan** in step-09 §6 (`INFINITE_PREDQUANTIZETEST`) and **exit criterion** in §7 —
  both already specified; implement against them rather than inventing new ones.

### B1. Build it to the 10-axis standard from day one

This is the point of doing Task B alongside Task A: don't repeat the 8 gaps this session just
fixed on the other four nodes (see `git log` on this branch's parent commits, or ask for the list
if not visible, before starting — they cluster around: raw-memcpy serialization instead of
versioned pack/unpack, dead/unwired flags, save-only-on-stop instead of periodic autosave, ungated
confidence badges showing stale numbers, and silent no-ops on too-short Learn passes with no UI
feedback). Concretely, before calling this node done:
- Serialization: versioned magic+version+explicit-field pack/unpack from the start (the pattern
  now established in `ColorStats::Profile::Serialize/Deserialize` and
  `MovementStats::Engine::Serialize/Deserialize` — copy that shape, don't write a new raw memcpy).
- Every saved flag/param that changes behavior must actually be read somewhere (grep for it after
  adding it — an unwired flag is exactly last session's `MovementLog::kCorrection` bug).
- Autosave on the same periodic cadence pattern as `PollColorStatsAutosave`/`PollAutosave`
  (`src/main.cpp`), not save-only-on-explicit-stop.
- `Confidence01()`-equivalent must gate on "has this actually learned something in the mode it's
  currently in," per the `PredictiveNotesNode::Confidence01()` `sourceMode` gate added this
  session — don't show a confident number for an untrained state.
- A too-short/insufficient Learn pass must set a visible flag/status (like
  `PredictiveNotesNode::LastLearnTooShort()`), not silently keep old state with no UI signal.

### B2. UI minimalism

Per `feedback_audio_node_minimalism` and `node-ui-pillars` (load that skill before touching any
node UI): this should land as Learn + `mix` + maybe one more control at most, not a knob per
internal parameter. Check step-09's own proposed control surface before adding anything beyond it.

### B3. Help coverage

New node = new entries in all the tables `project_node_help_coverage` tracks (right-click help is
a 5-level fallback across 4 hand-kept tables — check both directions: that the node's help text
exists, and that generic fallback text doesn't accidentally already half-match it in a confusing
way against `QuantizerNode`'s existing entries).

---

## Order of operations

Do Task A first (smaller, single node, no new files) — it's a good rehearsal of the "closed-form
+ OU wander" pattern before Task B's from-scratch build. Run `invariant-interaction-audit` after
each task (both touch save/load and Learn-state invariants). Run the full sweep of self-tests
(`INFINITE_DRIFTTEST`, `INFINITE_PREDFEEDBACKTEST`, `INFINITE_PREDV2TEST`, `INFINITE_PREDMIDITEST`,
`INFINITE_PREDCOLORTEST`, `INFINITE_MOVESTATSTEST`, plus the new `INFINITE_PREDQUANTIZETEST`) before
merging either branch — a regression in one Prediction node from touching shared code
(`ColorStats.h`/`.cpp`, `NoteModel.h`) is exactly the kind of thing a full sweep catches and a
single test doesn't.
