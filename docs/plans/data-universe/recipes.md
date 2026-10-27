# Recipes: modifiable node-chain fingerprints with a surface of dials

Status 2026-10-27: design decided from the owner's brief (the industry song structure is a reference, not a rule; the user bends it with dials and creativity; the structure underneath stays robust and editable through a mapping). Nothing built. No new node, panel or UI: this is a file format plus headless tools. Any in-app panel that shows it needs the owner's approval first.

## Principle

| Layer | What it is | Who touches it |
|---|---|---|
| Surface | dials: the owner's words (spacious, glitchy...), creativity c, energy, plus a time curve per dial | the user |
| Mapping | `dial -> knob moves`, solved through measured knob responses, never hand-written per recipe | the system |
| Deep | the chain: slots, branches, nodes, every param with its distribution and limits | anyone who wants full control |

Edits flow both ways: a dial move rewrites deep params; a deep edit is read back into the dials (the readout), so the surface always shows the true state.

## Recipe schema (JSON, versioned)

```
recipe: id, version, role (kick, bass, pad, lead, vox, drums bus, fx bus...), parents (derived from)
slots[]: name (source, tone, drive, space, motion, width, dynamics...), order, accepts{node: weight}   <- swappable by role
branches[]: from, to, kind (series | parallel | send), mix{mean, spread}
params[]: slot, node, param, norm 0..1, dist{mean, spread}, usage u (share of real presets that move it),
          limits{lo, hi} (critic-safe), response{percept axis -> d(percept)/d(param), sign, curve}
reference: arc prior per section role (build / drop / breakdown...) as curves over normalised section time
sources: preset ids and render ids it was fitted from (audit trail)
```

- Slots are role-based, not fixed nodes: the `space` slot accepts Reverb, Convolution Reverb or Delay with weights from `character_matrix.json`, so a recipe survives a node being swapped or a Field-written effect being added.
- The reference arc is the industry shape (intro, verse, build, drop, breakdown, outro) as a default curve per dial, not a constraint: the user's time curve replaces it.

## The mapping, concretely

1. **Responses (the Jacobian).** Render each node at a grid of knob settings on a fixed test set and measure the L1-L2 percepts (axes-strategy.md). Per knob: sign, size, curve, and where it saturates.
2. **Word targets.** Each word is a vector in percept space (mined from tags, axes-strategy.md section 8).
3. **Solve.** Dial value d gives target change Dp = d x word vector. Knob moves Dtheta = J^+ Dp, regularised, sparse, pulled toward the recipe's usual knobs (high u) and clipped to limits. Result: a small set of knobs moves, not all of them.
4. **Creativity.** c widens the solve: it adds weight to dormant knobs (low u) that also move the target, raises the sampling spread, and may swap the node inside a slot. Same critic floor as before. c = 0 uses only the usual knobs at their usual spread.
5. **Time.** Each dial is a curve over the song; the recipe evaluates it per section, so a build is "space up, brightness up, density up" and a drop is the release, bent however the user draws it.
6. **Readout.** After any edit, re-estimate each dial from the current params through the same J (forward model), so deep edits show on the surface.

## Fitting recipes from data

| Part | Source |
|---|---|
| Chain topology and slot order | preset chains (Logic strips, Guitar Rig, Ableton racks, Bitwig, Alchemy): order frequencies per role |
| Param distributions and usage u | preset module values; arcs from DAW projects (`arcs/als.jsonl`) for per-section values |
| Responses | headless renders of Infinite's own nodes (28 effects, 20 synths, Field effects measured the same way) |
| Reference arcs | arc tables per section tenth, plus the owner's albums as a personal arc prior |
| Personal centre | owner's tracks (`albums/n1m21n_tracks.jsonl`): a recipe set can start from "my" centre |

## Checks (the bars for this layer)

| # | Check | Bar |
|---|---|---|
| R1 | Realisation: asked change in percept space vs measured change after rendering the solved knobs | median cosine >= 0.8 |
| R2 | Round trip: set dial, solve, re-read dial | within 0.1 of the asked value |
| R3 | Reversible: a deep edit then dial readout then the same dial value returns to a sound within the same cluster | pass on 50 recipes |
| R4 | Safe: no critic failure at c <= 0.5; at c = 1 failures are redrawn, never emitted | 0 emitted failures |
| R5 | Coverage: recipes cover the chains seen in the data | >= 80% of Logic-strip chains map to a recipe whose slots fit |
| R6 | Heard: owner A/B on 10 dial moves, the described change is the one heard | >= 8 of 10 |

## Build order

1. Response harness (the heavy slot): headless render, percept delta per knob, on the Pro; resumable.
2. Recipe schema and a fitter for topology and param distributions from preset chains.
3. The solver (J^+ with sparsity and limits) and R1, R2.
4. Time curves with the industry arc as default; owner listening for R6.
5. Only then: ask the owner about any in-app surface for it.
