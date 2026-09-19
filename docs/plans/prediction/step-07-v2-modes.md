# Prediction step 7: v2 modes (outline, split before building)

**This file is a scope outline, not a single-session brief.** Each item becomes its own
`step-07x-*.md` brief, written after steps 1–5 have produced real logs. It must pass the README §4.3
gate before any UI is built.

Line numbers are from commit `35221c1`; re-grep the symbol if one has drifted.

| Item | What | Where it lives | Key correctness rule | Gate |
|---|---|---|---|---|
| 7a **Follow** | param B follows A with learned lag: `x_B(t) = Σ β_j x_j(t − τ_j) + c` | worker fit in `MovementStats`; a new Drift input "Follow" choosing a leader key | correlate **velocities**, not levels; τ ≥ 0; ridge λ by holdout; **candidates start from same-role groups in the patch (README §6), sign unknown until the first co-moves**, then any pair that co-moves | beats Drift alone on coupled pairs |
| 7b **Recall** | jump back to a similar past moment and continue from it | per-bar summary index (mean + slope per key, masked) on the worker | search the index, never raw history; compare only keys present in both windows | beats B0 on 4-bar-ahead NLL |
| 7c **Session map** | sections + returns (A B A′ C), click to jump | worker: 1 column per bar, cosine similarity, ≤ 2048 columns, Foote novelty | cosine, not raw `MᵀM`; block-average beyond the cap | owner can name the sections of their own session |
| 7d **Your moves faders** | few faders drive many params: `p = p_now + W·δh` | worker PCA on **deltas** ΔM; faders as a modulator with learned weights | PCA on signed deltas; NMF only on levels; before enough data, start with one fader per same-role group, with signs learned from the first co-moves | top 3 components explain ≥ 60% of hand-move variance |
| 7e **Section-conditioned Drift** | a knob gets a different home per section | `p̂` per (key, section id) from 7c | fall back to the unconditioned `p̂` by `nEff` | beats unconditioned Drift |
| 7f **Predictive MIDI source B** | movement → notes (README §8.1 mapping) | a mode of the step-6 node | deterministic mapping; no learning | owner listening test |
| 7g **Play like me** (v3, research) | PCA-space dynamics / ProMP | worker | DMD `A` scaled to spectral radius ≤ 1 | research only |

## Code references to start from

| Area | Where |
|---|---|
| per-key stats, profiles, weights | `src/core/MovementStats.*` (steps 2 and 5) |
| log reading for offline fits | `MovementLog::ReadFile` (step 1) |
| worker thread | the step-1 writer thread; add a job queue rather than a second thread |
| Drift inputs | `DriftNode` in `src/nodes/PredictionNodes.*` (step 4) |
| note output for 7f | `PredictiveNotesNode` (step 6), `src/audio/NoteEvent.h` |
| a panel for the session map | the docked panel pattern; load the `panels-sweep` skill before building |
