# Research landscape: DDSP, parameter estimation, sound matching, embeddings (2026-10-28)

Exploratory. Goal: know the field well enough to place Infinite's sound model in it and pick one area to lead. Sources are the papers named; numbers are only those the papers' pages state.

## Four areas in one picture

```
 audio  ──(inverse: "which knobs made this?")──►  parameters      A. parameter estimation / sound matching
 audio  ◄──(forward: "what does this knob do?")── parameters      B. surrogate / neural proxy (forward model)
 signal path made differentiable (gradients through the synth)    C. DDSP
 audio ──► vector in a space where distance = similarity          D. embeddings (CLAP, EnCodec, DAC, MERT)
```

| Area | Core idea | Representative work | Limits that matter to us |
|---|---|---|---|
| C. DDSP | synth built from differentiable parts (harmonic + filtered noise) so gradients reach the knobs | [Engel et al., ICLR 2020](https://arxiv.org/abs/2001.04643) | needs the synth rewritten in a differentiable framework; one fixed architecture; fits instruments, not arbitrary synths |
| A. Estimation | train a network audio -> parameters on random renders | [InverSynth](https://cs.paperswithcode.com/paper/deep-synthesizer-parameter-estimation); [Sound2Synth, IJCAI 2022](https://www.ijcai.org/proceedings/2022/682) (Dexed FM); [AST on Massive, DAFx 2024](https://arxiv.org/html/2407.16643v1) (16 controls, 1 M renders) | parameter loss is ill-posed (many knob sets, same sound); trained on random presets, weak on real ones |
| A'. Sound matching | search/learn so the *render* matches a target | [InverSynth II, ISMIR 2023](https://ismir2023program.ismir.net/poster_209.html) (proxy + inference-time fine-tune); [SynthRL, IJCAI 2025](https://www.ijcai.org/proceedings/2025/1129) (reward = similarity, no labels); CMA-ES beats gradients on a 28-param subtractive synth (arXiv 2603.15905) | each needs a differentiable or learned stand-in for the synth |
| B. Neural proxy | network preset -> embedding of the render, so black-box synths get a loss | [Combes et al., JAES 2025](https://arxiv.org/abs/2509.07635): 3 synths, feed-forward / recurrent / transformer, 10 M synthetic presets | proxies tested on 3 synths; benefits "nuanced by resource requirements" |
| D. Embeddings | pretrained audio nets as similarity space | CLAP, EnCodec, DAC, MERT; [timbre-rating study, 2025](https://arxiv.org/abs/2507.07764): 18 representations vs 2,614 human ratings, CLAP style embeddings best | opaque axes; not tuned to timbre; no knob link |
| text -> synth | CLAP as the objective, optimise knobs | [CTAG 2024](https://arxiv.org/pdf/2406.00294) (78-param modular synth, evolutionary search); [SynthScribe](https://arxiv.org/pdf/2312.04690) (retrieval) | slow search per prompt; one synth |

Not found in searches (gap, not proof of absence): active learning that chooses *which presets to render next* for synth timbre spaces (nearest: Fasciani's [timbre-space mapping](https://ro.uow.edu.au/articles/conference_contribution/Interactive_computation_of_timbre_spaces_for_sound_synthesis_control/27691926), 2016).

## How each links to Infinite (stage verified in tools/universe)

| Area | Infinite today | Gap |
|---|---|---|
| C. DDSP | none; engines are C++ nodes, not differentiable | not needed: proxy route reaches the same goal |
| B. Proxy | `m1_forward.py` = knobs + note -> characteristic summaries; 3-seed ensemble in `propose.py`; **6 engines in one pipeline** | targets are our 22 interpretable axes, not an opaque embedding |
| A'. Matching | `reach.py`: nearest render -> search knobs on the forward model -> render -> measure (Step 6, C7) | not yet run (queued); no inverse network |
| D. Embeddings | `embed.py`: CLAP + EnCodec on MPS, C11 = how much of the embedding the 22 axes explain | first numbers pending |
| Active learning | `propose.py` + `round_score.py`: model picks renders, predictions stored before render | Analog group split 0.298 -> 0.686 in one round |

## Where to be set apart: **interpretable, active-learned, multi-engine forward model with pre-registered claims**

| Why it is open | Why Infinite can do it |
|---|---|
| Papers train on random presets of one synth; none found that *chooses* renders by model disagreement | `propose.py` is built and shows a measurable jump per round |
| Embedding proxies are opaque: you cannot tell the user "brighter because the filter opened" | our target is 22 named axes the user already reads on the node |
| Proxy papers cover 3 black-box VSTs and need 10 M presets | our engines are deterministic and ours: recipe = lossless record, re-render any time, 4 k renders already move the bar |
| Claims in papers are not frozen before evaluation | `claims.py` freezes test sets and predictions before rendering |

Product tie: the same model that picks training renders answers the user's knob questions (what does this do) and powers "sound like this" (reach test) inside the node, with the axes as the readout.

## Respectable level: what we still owe in each area (small, measurable)

| Area | To reach a defensible level |
|---|---|
| B | run the proxy comparison on our data: MLP vs transformer vs ensemble, and axes-target vs CLAP-target, on the same group split |
| A' | Step 6 numbers (C7) plus a CMA-ES baseline against our search |
| D | C11 numbers and a timbre check: do axis distances track CLAP/EnCodec distances on held-out rows |
| A | an inverse net audio -> knobs trained from our renders, scored by render distance, not parameter loss |
| C | read-only: note that a harmonic-plus-noise engine would be the DDSP-style option if Wavetable/String need it |
