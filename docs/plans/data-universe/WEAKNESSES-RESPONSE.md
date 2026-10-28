# Response to WEAKNESSES.md

Date: 2026-10-28. Each of the six points is sorted into: already measured, new and valid, or not supported. Actions are listed at the end.

## Verdicts

| # | Weakness | Verdict | What we already know | Action |
|---|---|---|---|---|
| 1 | Chain: 22 axes between nodes lose the waveform | **Valid, the most important.** A saturator on a square vs an FM cluster with identical axes is a real counter-example | Nothing measured yet (chain renders L1 not done). The forward model already has a mel decoder, so a temporal intermediate is available without new data | Add an **intermediate ablation** to CHAIN-TRACK: effect proxy fed (a) true 22 axes of its input, (b) predicted mel, (c) both. If (a) loses clearly to (b) on held-out combos, the bottleneck is proven and chains pass mel. Cheap once L1 renders exist |
| 2 | Same-generator illusion | **Valid, but the cited number is unverified.** The "0.87 -> 0.12" figure is not in the Combes abstract (it says "encouraging results" for both synthetic and hand-crafted presets); I could not confirm it and do not build on it | Our banks are same-engine held-out folders; the C2 moves are single-knob steps around base sounds, and reach targets are real sample-library sounds. So part of our evaluation is already off the sweep distribution | The repo holds **no human-authored patches** for these six engines (searched all `.inf` files). A human-preset benchmark needs the owner. See request below |
| 3 | 11 mean axes collapse time | **Partly agreed, already adopted.** Section 6 settled axes for UI, embeddings for loss. The 22-axis space already includes spread (std) axes, not only means | Genre at chance shows the axes drop macro-musical content; agreed, not a surprise | Keep. No change; reach loss on embeddings stays on the list |
| 4 | Composite axes (fullness, movement, noisiness) | **Valid, matches our data.** Those are exactly the weak C2 axes. Neither the difference loss nor pair filtering moved them (63.5 / 63.9 / 63.0 %) | At >= 0.5 sd moves direction is 73.9 % | Do **not** change frozen axes or claims. Add an exploratory diagnostic: for each weak axis, which knobs flip its sign across contexts (non-monotonic = composite). Report only, no new claim |
| 5 | Optimizer's curse | **Already adopted** (LCB `--beta`, `--refine`, top-K real renders). The new point is real: 3 seeds give a poor std | reach2 shows the curse (predicted 3.65, measured 4.20) | Add a **calibration check** to reach score: correlation of seed std with actual prediction error. If low, `--beta` is ineffective and top-K re-render is the only protection |
| 6 | Disagreement sampling picks chaos | **Plausible, testable now.** Control run decides efficiency; pathology is a separate, cheap check | Random control is rendering (about 1,200 of 2,000) | When it finishes, compare chosen vs random on: failed or silent render rate, share at the extreme of each knob range, and group head medians. If chosen is worse, restrict candidates to the musical prior |

## What I am not taking

- "Transition reach entirely to embeddings": too strong. Axes remain the only target the user can read; embeddings go where the review of section 6 already put them (loss), after the monotonicity test.
- Calling the 11-axis score a "perceptual non-equivalence" proof: true in principle, but our C3 owner listening test is the direct measure and is pending.

## Request to the owner (improves the model)

Weakness 2 can only be settled with real musician settings. Smallest useful set: **20 patches you would actually play per engine for Analog and Oscillator** (40 total), made in the Infinite UI and saved as `.inf`. They become a locked human benchmark: score the held-out-bank model on them and report the gap. Until then the claim "learned" stays labelled "synthetic-bank only".

## Order of work

1. Seed-std calibration in `reach.py score` (runs on reach3 when it renders).
2. Control A/B plus pathology check (when the render ends).
3. Intermediate ablation written into CHAIN-TRACK.md, run when L1 renders exist.
4. Weak-axis sign-flip diagnostic from existing C2 data.
