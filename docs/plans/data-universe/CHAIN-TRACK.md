# Chain track (G6 / claim C9): the chain is the model

Owner direction 2026-10-28: a node chain is shaped like a neural net (nodes = regions, cables = connections, knobs = weights, any tweak changes the result). So understand each node, and prove the chain is learned. Frozen before any render.

## Model

```
 note ─► [Source model] ─► [Node 2 model] ─► [Node 3 model] ─► output
            f1(knobs1, note)    f2(knobs2, x1)     f3(knobs3, x2)
 x_k = the 22-axis characteristics after node k  (the shared language between nodes)
```

One small model per node type; the chain prediction is the composition. No chain-level training in the main arm.

## Data to render (per chain: x_k measured after every node, so each link is scored alone)

| Set | What | Why |
|---|---|---|
| single | each node on its own (source engines done; effects fed fixed reference sounds) | per-node models |
| pairs | source + 1 effect, 20 effects x 6 engines, random knobs | link models f2 |
| triples | source + 2 effects, effect pairs held out by combination | does composition generalise |
| moves | one chain, one knob moved at one node (C9) | tweak direction and size |

## Held-out (frozen, hash by chain shape, like banks)

- **Held-out chain shapes**: whole node combinations never rendered in training (e.g. a given source + effect A + effect B).
- **Held-out moves**: 200 single-knob moves in held-out chains (claim C9, C2 bars: direction >= 90%, size r >= 0.7).

## Proofs (all reported, none tuned on the test set)

| Proof | Measure | Pass |
|---|---|---|
| Composition works | R2 of axes at the chain output, composed model on held-out shapes | >= 0.6 per axis group (P3 bar) |
| No hidden depth cost | error at depth d <= sum of per-node errors | holds at depth 2 and 3 |
| Chain-level tweaks | C9 on held-out moves | C2 bars |
| Activity matches reality | per-node sensitivity predicted by the model vs measured by bypass ("lesion") | rank correlation >= 0.7 |
| Interactions | for node pairs: effect of moving both vs sum of moving each ("connectivity"); model predicts the gap | beats the additive guess |

## Neuroscience ideas used

- sparse activity (activity.py): few knobs carry most change
- lesion: bypass a node, measure loss (the bypass rule: 2+ input nodes never bypass)
- functional connectivity: pairs that change the result together
- tuning curves: output axis vs one knob swept (per node)
- context dependence: a knob's activity depends on others (e.g. Analog detune is dead while voices = 1); measure activity per context

## Open before rendering

1. Which effect nodes render headless with a fixed input. `docs/node_param_audit.md` lists only 2 under AudioEffects (Field Effect, Plugin), so it is not a census; use `Infinite --describe <node> --json` per candidate name (as own_sweeps.describe does), never `--help` (it launches the app and hangs).
2. Reference input sounds: pick ~50 from held-out source renders, spanning the axes.
3. Cost: one render per chain, so pairs 2,400 + triples 4,000 + moves 600 ~ 7,000 renders, about the size of one engine's first batch.
