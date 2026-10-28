# Response to MASTER-ARCHITECTURE-BLUEPRINT.md and PRESET-ANCHORS.md

Date: 2026-10-28. Status: evaluation only, nothing built from either doc yet.

## What is good
- Anchors (a few named, trusted settings per engine that the learner is checked against) fit the plan: they act as a sanity set next to the held-out bank.
- A category selector over the sound space is a sensible UI idea.

## Problems found
| # | Issue | Evidence | Fix |
|---|---|---|---|
| 1 | The 40 invented recipes are not real anchors | Hand-written by a model, not by a person who plays the instrument | Use the owner's own patches (`~/Downloads/Patches`) as the anchors. Already extracted for the human benchmark. |
| 2 | Units and keys do not match the engines | attack/decay written as ms 0-4000, fine tune in cents +-50, `analog` is a bool, while sweeps use the engine's own ranges | Generate anchors through `own_sweeps.param_table` (keys, ranges, defaults), not by hand. |
| 3 | The Combes figure is unverified | No source was checked | Do not cite until checked against the paper. |
| 4 | Mel as the intermediate is a hypothesis | Ablation (axes vs mel vs both) in CHAIN-TRACK.md is still pending | Keep the hypothesis labelled until the ablation reports. |
| 5 | Emotional macros are hand-picked linear maps | Same pattern as "learner, not hand-picked" | Learn them from data, then compare with the hand maps as a baseline. |
| 6 | Category selector, emotional knobs and the UI panel are new features | Rule: new features need Naman's approval | Show Naman a mock first. Nothing in Phase 6 starts before that. |

## Proposal
1. Pick anchors per engine by clustering the owner's patches in axis space (one patch per cluster).
2. Split the patches: anchors vs benchmark, disjoint, so the human benchmark stays unbiased.
3. Add "anchor R2 / direction" to the claims scorer as an extra check, not a new bar.

## Needs Naman
- Approval to use the patches folder as the anchor source.
- A look at the category selector and emotion knob mock before Phase 6.
