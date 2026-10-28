# Second-opinion brief: six open problems in Infinite's sound-learning work

Date: 2026-10-28. Audience: someone outside the project (no prior context assumed). Status: exploratory, not for publishing.
Numbers are measured on our own data and are recorded in `HANDOFF.md` and `SOUND-MODEL.md`.

## 1. What Infinite is and what we are building

Infinite is a node-based instrument: sources, effects and modulators are wired into chains, and every node has knobs. We are building a **learned model of that instrument** so it can (a) predict what a patch will sound like without rendering it, (b) know what each knob does, and (c) find knob settings that sound like a target.

```
 knobs (0-1 each) + note ──► [ forward model ] ──► sound description (22 characteristic axes, 11 "mean" axes shared by all engines)
        ▲                                                  │
        │            "find knobs that give this" ◄─────────┘   (reach)
        └─ "what does this knob do?"  = f(knobs + move) - f(knobs)   (claim C2)
```

- **Engine** = one sound generator node (Analog, Oscillator, Wavetable, FM, Metallic, String).
- **Bank** = about 20 sounds from one engine: a base patch plus variants with 1-3 knobs changed.
- **Held-out bank score** = about 1/5 of banks, chosen by a hash of the folder name, are never trained on; we score the model on them (R2 over the 11 mean axes, "group R2"). Bar for a pass: mean R2 >= 0.6.
- **The 11 mean axes**: loudness, brightness, sharpness, warmth, weight, fullness, noisiness, movement, attack, decay, density.
- **Claims** C1-C11 are written down and frozen before scoring (`claims.json`), each with a bar. Misses are recorded as misses.
- **Active learning loop (Step 4)**: three models trained with different seeds; where they disagree most, we render next; repeat until two rounds each gain under 0.02.

## 2. Where we stand (measured)

| Item | Result | Bar | Verdict |
|---|---|---|---|
| Held-out bank group R2, Oscillator | 0.192 -> 0.551 after one model-chosen round | 0.6 | close, not yet |
| Held-out bank group R2, Analog | 0.298 -> 0.686 after one model-chosen round | 0.6 | pass, but the equal-size random control is not run yet |
| C2 direction right, Analog / Oscillator | 63.5% / 61.8% | 90% | MISS |
| C2 size correlation, Analog / Oscillator | 0.635 / 0.549 | 0.7 | MISS |
| C2 on moves >= 0.5 sd (chosen after the fact) | Analog 75.5%, r 0.707; Oscillator 84.5%, r 0.614 | not the frozen bar | exploratory only |
| Reach to sample-library targets, Analog | measured distance 4.20 vs 3.43 for the nearest existing render; closer for 13.7% of 300 targets | n/a | model is optimistic by 0.55 sd |
| Genre from the 11 axes of samples | chance level | 0.7 top-3 | MISS |
| Genre from drum-groove structure | top-3 0.394 (chance 0.250) | 0.7 | above chance, below bar |
| Pitch from audio vs file name (single notes) | 283/289 = 97.9% | 90% | pass |
| Model-vs-measured knob activity rank correlation | Analog 0.68, Oscillator 0.84 | none set | encouraging |

Caveats we already know: every held-out score is **same generator** (unseen banks, same engine code); the held-out set grows as the library grows unless scored on the frozen file; model-chosen vs equal-size random data is unproven.

## 3. The six problems

### P1. Learning what a knob does, not only what a patch sounds like (claim C2)

- **Statement.** Given a patch and a 1-3 knob change, predict the sign and size of the change on each axis.
- **Evidence.** Direction right 62-64% over all non-zero changes. Per axis on clearly-changed axes (> 0.3 sd): sharpness 88-97%, decay 85-87%, loudness 69-83%, but **fullness 62-69%** (r 0.26-0.48), noisiness 74-84% (r 0.38-0.52), movement 71%.
- **Why it may be hard.** (1) The scorer counts changes of a few thousandths of an sd, whose sign no model can predict, so 90% over all of them is close to unreachable by construction. (2) The model is trained on values; a good value fit does not guarantee good differences between near-identical inputs.
- **Being tried.** `c2_delta.py`: same model plus a loss on f(a) - f(b) for every pair of training renders that differ in at most 3 knobs (derivative-informed / gradient-matching training).
- **Questions.** Is an effect-size-gated metric a legitimate bar, or does changing it after seeing results undermine the claim? Better ways to learn sensitivities from base-plus-variant pairs (paired training, contrastive, Jacobian penalties)? Are fullness/noisiness/movement even single-direction concepts?

### P2. Steering toward a target sound (reach)

- **Statement.** Given a target description (from a library sample), choose knobs so a render lands close to it.
- **Evidence.** Reach2 (Analog): 300 targets, 150 candidates per target. Predicted distance 3.65, measured 4.20, nearest existing render 3.43. The chosen point beat the nearest existing render for 13.7%. The optimiser picks points where the model errs in its own favour (optimiser's curse).
- **Being tried.** Hill-climb on model predictions (`--refine`), penalty by seed disagreement (`--beta`, lower-confidence bound), planned: render the top few per target, retrain, repeat (inference-time refinement as in Inversynth II).
- **Questions.** Which of ensemble penalty, closed-loop render-and-retrain, or CMA-ES is most efficient? How to state honestly that a target is out of an engine's reach, and how much of the failure is the engine rather than the search?

### P3. Are the 11 axes the right description of a sound?

- **Statement.** Everything above is measured through 11 hand-built axes.
- **Evidence.** Genre from these axes is at chance on 11k library samples; the worst-predicted axes (fullness, noisiness, movement) may mix several effects.
- **Candidate.** Learned embeddings (CLAP pooled + EnCodec latent mean/std, 768-d, computed on a GPU-less Mac via MPS) as a second description, with the axes as the readable layer.
- **Questions.** How to test an embedding without circular evidence (the embedding and the target both come from the same audio)? When is a hand-built axis better than a learned one for a musician-facing tool?

### P4. Proving a chain is learned, not just single nodes

- **Statement.** Chains of nodes behave like a neural net: each node an area, knobs as weights, some nodes much more active than others. A tweak anywhere changes the output. We need a model that knows each part and a proof that the **chain** is learned.
- **Plan** (`CHAIN-TRACK.md`): per-node forward models composed. Levels: L1 serial (source -> effect), L2 mix/split, L3 modulation, L4 graphs, L5 feedback. 45 node types take audio in and out; 25 one-input effects (~190 knobs) are ready for L1.
- **Proposed proofs.** Composition R2 on held-out chain shapes; no loss with depth; the model's effect of bypassing a node (lesion) rank-correlates >= 0.7 with the measured effect; interaction terms beat an additive model. Neuroscience-derived tools already built: activity map (which knobs matter), connectivity (pairwise interactions), tuning curves (response shape per knob).
- **Questions.** What is the right held-out unit: unseen chain shapes, unseen source/effect pairs, or unseen nodes? Is lesion-vs-model rank correlation a sufficient proof of "learned the chain"? How to design feedback loops (L5) so the test is not trivially stable?

### P5. Choosing data efficiently

- **Evidence.** One model-chosen round took Oscillator R2 from 0.192 to 0.551 and Analog from 0.298 to 0.686. The control "same number of random renders" has not been run, so the gain may be mostly "more data".
- **Cost.** About 1.6 s per preset (about 0.5 s analysis, about 1.1 s render and start). Options in `EFFICIENCY.md` (analyse only the C3 note; overlap render and analysis).
- **Questions.** Best disagreement measure beyond a 3-seed ensemble? How to keep the loop from exploring only where the model is odd while rare-but-real regions stay unvisited (we added a farthest-point `--diverse` option, untested)?

### P6. Evidence beyond the generator

- **Statement.** All held-out scores are unseen banks of the same code. They show the model learned the engine; they do not show it matches what people hear.
- **Available.** A critic with AUC 0.924 on a held-out half, owner agreement 30/40 still open; the sample library (out of domain) for reach tests.
- **Questions.** What cheap external evidence is convincing: listening test design (how many raters, what comparisons), other synths, recordings? What should a claim say while only same-generator evidence exists?

## 4. What a second opinion would help with most

1. P1: whether to keep the frozen strict bar, add an effect-size-gated bar, or both, and how to word the claim.
2. P4: the held-out design and what proof of "chain learned" would convince a sceptic.
3. P6: the cheapest external evidence.
4. P2: choosing between ensemble penalty, closed-loop, and CMA-ES.

## 5. What we can supply to the reviewer

- Frozen claims file with bars, per-engine prediction files with hashes and times, and the scoring code (`tools/universe/claims.py`).
- Full tables per round in `HANDOFF.md`; design docs `RESEARCH-LANDSCAPE.md`, `CHAIN-TRACK.md`, `EFFICIENCY.md`.
- Our own request to the owner: 30-40 listening judgements for the critic, and any real patches or recordings to match.

## 6. Response to the review (SECOND-OPINION-REVIEW.md, 2026-10-28)

| Review item | Decision | Why / status |
|---|---|---|
| P0 equal-size random control | **Adopt, running** | `control.py`: base = the 1,965 renders before r01; arm A adds the 2,000 model-chosen rows, arm B adds 2,000 random rows (same one/pair/fresh mix, same edge-biased draw); `p3_characteristics.py --extra` scores both on the same test banks. Driver6 runs it after the Oscillator reach2 render. |
| Two-tier C2 (strict + audible-sensitivity C2.1) | **Adopt, tau = 0.5 sd, not 0.3** | The review ties 0.3 sd to one JND, but we have not measured a JND for these axes. 0.5 sd is the cut already shown as exploratory, so there is no further tuning. C2.1 is pre-registered for the four engines not yet scored (Wavetable, FM, Metallic, String); for Analog and Oscillator it stays post hoc and is labelled so. 0.3 sd is reported as a sensitivity check. Strict C2 stays a MISS. |
| Sobolev / difference loss (`c2_delta.py`) | **Done: no gain** | Analog lam 1: 63.9% / r 0.626; lam 10: 61.8% / r 0.549; plain model 63.5% / r 0.635. |
| Pessimistic LCB + top-K real renders, CMA-ES stage | **Adopt** | `--beta` and `--refine` are in `reach.py`; reach3 plan runs with them. Top-K closed-loop rendering is the next change. CMA-ES is not added until the beta/refine result is in. |
| Out-of-gamut advisory | **Adopt** | Needs a distance of the target to the engine's own renders (Mahalanobis on the 11 axes); build after reach3. |
| Dual representation (axes for UI, embeddings for loss) | **Adopt as direction** | The embeddings job (CLAP + EnCodec) is queued; the claim that genre failure "proves the axes are free of genre bias" is not adopted: it shows the axes do not carry genre, not that they are unbiased. |
| Held-out unit = unseen node combinations; depth curve; lesion with null controls; additive baseline | **Adopt** | Added to the CHAIN-TRACK proofs. Null lesion (mix = 0 or unity gain must predict delta = 0) is a good test we did not have. |
| 2AFC listening test (15-20 producers, 30 triplets) | **Adopt as the external-evidence plan; needs the owner** | The "70% win rate gives p < 0.001" figure needs checking: with 30 trials 70% is p ~ 0.02 one-sided; p < 0.001 needs about 80% at 30 trials or more trials. |
| Literature list | **Partly unverified** | InverSynth II is real (Barkan et al., ISMIR 2023) but the review's title differs from the paper's; 'Combes et al. JAES 2025' and 'CMA-ES vs gradient search (2026)' have no citation and are not used until found; micro-tcn is a compressor-modelling repo, not a general surrogate-synth reference. |

## 7. Reply to SECTION-6-RESPONSE.md (2026-10-28)

| Reviewer statement | Checked | Result |
|---|---|---|
| 2AFC: 70% of 30 trials is p ~ 0.02; 80% needed for p < 0.001 | exact binomial | **Agreed**: P(>=21/30) = 0.0214; P(>=24/30) = 0.0007. |
| 15 raters x 30 trials, 70% pooled: p ~ 1e-18 | exact normal tail | pooled p = 1.1e-17 (reviewer's figure is close), **but the pooled test is the wrong test**: raters share the same 30 triplets, so the trials are not independent. The model needs a random effect for the item as well as for the rater (crossed); the effective sample for 'is it the triplets' is closer to 30 than 450. We will pre-register the crossed mixed model and report the item-level result. |
| Difference-loss failure 'proves the derivative representation is fine' and gating at 0.5 sd 'solves the root problem' | own data | **Not accepted.** The noise explanation is a hypothesis we have not tested (e.g. drop pairs with |change| < 0.3 sd from the loss). Gating does not fix the model: on clearly-changed axes (> 0.3 sd) direction is still only 62-69% for fullness and 71-74% for movement/noisiness (Oscillator). Gating makes the claim measurable, not the model correct. |
| 'Convex hull / Mahalanobis' out-of-gamut | reach2 | We use the target's distance to the nearest existing render in sd units; it correlates 0.89 (Analog) and 0.77 (Oscillator) with the measured outcome. A convex hull in 11 dimensions would contain almost everything; the nearest-render distance is the measure that works. |
| InverSynth II title 'Fine-Tuning a Neural Synthesizer Proxy for Target-Matching', authors 'Barkan, Tsiris, et al.' | ISMIR 2023 listing | **Wrong again.** Title: 'InverSynth II: Sound Matching via Self-Supervised Synthesizer-Proxy and Inference-Time Finetuning'; Barkan, Shvartzman, Uzrad, Laufer, Elharar, Koenigstein. |
| Combes et al. 'Surrogate Models for Virtual Analog and Digital Synthesizers' (JAES 2025), arXiv:2509.07635 | arXiv | **Real paper, wrong title**: 'Neural Proxies for Sound Synthesizers: Learning Perceptually Informed Preset Representations' (Combes, Weinzierl, Obermayer; code pcmbs/synth-proxy). Journal venue not checked. |

### What the Combes paper adds (read 2026-10-28, abstract and results via arXiv HTML)

- Same shape as ours: a preset encoder maps synth parameters into a frozen audio model's embedding space (L1 loss), trained on ~10M random presets; Dexed (FM, 100 params), Diva (analog, 173), TAL-NoiseMaker.
- **A transformer encoder beat MLP and highway networks on every synth**, and attention helped on interdependent parameters (Dexed algorithm, Diva mode-dependent). Our forward model is a plain MLP; the 'MLP vs transformer' comparison already on our list is now justified, particularly for Analog (mode switches) and chains.
- **Same-generator gap quantified**: MRR 0.87 / 0.81 on synthetic presets fell to 0.12 / 0.46 on hand-crafted ones. This is the evidence for P6 in someone else's data: random-preset scores do not carry to what people actually build. Our 'hand-built' check set would be the owner's real patches.
- **An embedding loss improved sound matching** over parameter loss alone, in-domain and on NSynth: supports using learned embeddings as the reach objective (P3).
- **A non-circular test for embeddings** (P3): rank embedding distances against monotonic sweeps of a known attribute (envelope, brightness, pitch). Our own knob sweeps (tuning curves) are exactly such sweeps; scoring CLAP / EnCodec by monotonicity along them tests the embedding without using genre or the axes as the ruler.
- Caveats they state: LFO parameters (time and frequency at once) are the least stable; synthetic sampling covers many unmusical regions; one note and velocity only (as ours: C3).
