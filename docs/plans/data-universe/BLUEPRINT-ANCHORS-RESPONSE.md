# Response to MASTER-ARCHITECTURE-BLUEPRINT.md and PRESET-ANCHORS.md

Date: 2026-10-28. Status: evaluation only, nothing built from either doc yet.

## What is good
- Anchors (a few named, trusted settings per engine that the learner is checked against) fit the plan: they act as a sanity set next to the held-out bank.
- A category selector over the sound space is a sensible UI idea.

## Problems found
| # | Issue | Evidence | Fix |
|---|---|---|---|
| 1 | The 40 recipes name no source | Human-made presets by other musicians are the right kind of anchor, but the doc gives no pack, author or file for these 40, so they cannot be checked as real presets | Name the source of each (pack/author), or replace with presets whose origin is known. The owner's patches (`~/Downloads/Patches`) are one confirmed human source. |
| 2 | Units and keys do not match the engines | attack/decay written as ms 0-4000, fine tune in cents +-50, `analog` is a bool, while sweeps use the engine's own ranges | Generate anchors through `own_sweeps.param_table` (keys, ranges, defaults), not by hand. |
| 3 | The Combes figure is unverified | No source was checked | Do not cite until checked against the paper. |
| 4 | Mel as the intermediate is a hypothesis | Ablation (axes vs mel vs both) in CHAIN-TRACK.md is still pending | Keep the hypothesis labelled until the ablation reports. |
| 5 | Emotional macros are hand-picked linear maps | Same pattern as "learner, not hand-picked" | Learn them from data, then compare with the hand maps as a baseline. |
| 6 | Category selector, emotional knobs and the UI panel are new features | Rule: new features need Naman's approval | Show Naman a mock first. Nothing in Phase 6 starts before that. |

## Proposal
1. Pick anchors per engine from presets with a known human source (the owner's patches, plus other musicians' presets once their source is named), by clustering in axis space (one patch per cluster).
2. Split the patches: anchors vs benchmark, disjoint, so the human benchmark stays unbiased.
3. Add "anchor R2 / direction" to the claims scorer as an extra check, not a new bar.

## Needs Naman
- Owner approved (2026-10-28): `~/Downloads/Patches` is an anchor source.
- Source (pack/author) for the 40 recipes in PRESET-ANCHORS.md, if they come from other musicians' presets.
- A look at the category selector and emotion knob mock before Phase 6.
