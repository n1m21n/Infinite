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

## Beyond a line: graphs (multiple sources, effects, interlinked chains)

Real patches are graphs, so the model must be one too. Same idea, one more rule: a node with several inputs gets a combine step.

```
 serial      S ─► E ─► E                      f_E(knobs, x)
 mix (fan-in)  S1 ─┐
               S2 ─┴► Mixer ─► E              x = combine(x1, x2)   (sum of signals, not of axes)
 split (fan-out) S ─► E1 ─┐
                  └─► E2 ─┴► Mixer            same x feeds two nodes
 modulation   LFO ──► knob of E               the cable moves a knob over time, not the signal
 feedback     E ─► Delay ─► E                 needs state; last stage
 interlinked  chain A ⇄ chain B               A's output modulates or feeds B
```

| Level | Adds | New data (about) | Held-out test |
|---|---|---|---|
| L1 serial (above) | composition | 7,000 | unseen node combinations |
| L2 mix / split | combine rule for 2+ inputs | 5,000 | unseen source pairs |
| L3 modulation | cable drives a knob | 3,000 | unseen modulator-target pairs |
| L4 graphs | any wiring, 3-6 nodes | 6,000, chosen by model disagreement | unseen topologies (whole graph shapes) |
| L5 feedback | state across time | later | only after L1-L4 pass |

- Combine step is learned from rendered mixes: input = the axes of each source plus their level ratio and masking, output = axes of the mix. The mix model is its own node model (the Mixer), so it gets its own held-out score.
- Modulation is a different kind of link: it changes the node's knob per time step, so the target's tuning curve (axis vs knob) is what gets composed.
- Each level has a pass bar before the next level is rendered; the active-learning loop picks L4 graphs because random wiring would waste most renders.
- Bypass rule (2+ input nodes never bypass) means lesion tests on mixers remove an input instead.
- Brain parallel: this is the move from a single pathway to networks with convergence (fan-in), divergence (fan-out), neuromodulation (L3) and recurrence (L5).

## Effect-node census (2026-10-28, from tools/universe/describe_all.json, 342 node types)

59 types output audio: 14 pure sources and **45 with an audio input as well**. By category: AudioEffects 30, Synths 10 (Field Synth, Grain Molder, Granular, Looper, Molder, Oscillator, PaulStretch, Sampler, Slicer, Wavetable: these take audio in too, e.g. FM/sidechain/material), Utility 5 (Audio Meter, Blend Audio, Gain, Mixer, Splitter).

Render-first list for L1 (single audio-in effect, 1 input, 3-20 knobs): Audio Filter 8, Bitcrush 5, Chorus 9, Convolution Reverb 7, Cycle Shaper 5, Delay 14, Drive 6, EQ 20, Flanger 8, Formant Filter 3, Frequency Shifter 6, Key-Snap 6, Limiter 4, Phaser 8, Pitch Shifter 5, Resonator Bank 9, Reverb 7, Ring Mod 4, Saturator 10, Spec Blur 6, Stereo 3, Stutter 3, Transient Shaper 3, Tremolo 6, Wavetable Shaper 8 = 25 effects, ~190 knobs. Held back for L2+: two-input nodes (Dynamics sidechain, Shape Resonator, Spectrum Slide, Blend Audio, Mixer, Splitter). Left out: Plugin (0 knobs, third-party), Field Effect (user code), Audio Meter (no output change). Caveat: 'audio in' counts any audio-kind input pin, so a Synths-category node may use it only as a modulator; confirm per node with a render before it is in a held-out chain shape.

## Additions from the external review (2026-10-28)

- **Held-out unit = unseen node combinations**: train on A->B and B->C, test on A->C and A->B->C (in addition to unseen chain shapes).
- **Depth curve**: report held-out R2 at depth 1, 2, 3, 4 and the error against the sum of per-node errors; the claim 'no depth cost' needs the curve, not one number.
- **Null lesions**: bypassing a node whose effect is nil (mix = 0, unity gain, a filter wide open) must be predicted as zero change; the model's lesion-vs-measured rank correlation (>= 0.7) is only reported together with its null-lesion error.
- **Additive baseline**: the composed model must beat 'sum of single-node changes' on the interaction pairs; report where the two differ (saturation after filter, delay into reverb).


## Intermediate ablation (from WEAKNESSES.md, vulnerability 1)
Passing 22 axes between nodes may lose what a nonlinear effect needs (a saturator sees a square and an FM cluster differently even if their axes match). Test, on held-out chain shapes and unseen combinations, an effect proxy fed with: (a) the true 22 axes of its input, (b) the predicted mel from the source model, (c) both. If (a) is clearly worse than (b) on the effect's output, chains carry mel between nodes and the axes stay a read-out only. Needs the L1 renders.
