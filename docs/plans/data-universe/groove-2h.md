# Step 2h: drum groove generator, simulation result

Sim only. No C++ was changed, nothing built, `benchmark.py` not run. Reproduce: `python3 tools/universe/groove_2h.py --seeds 6 [--midi]` (about 2.5 min, under 1 GB).
The sim is not bit-exact with the app (the Python copy scores the current proposer at AUC 0.826, the app dump at 0.765), so read the differences between rows, not the absolute level.

## Result: one move beats the current proposer

| Variant | B4 AUC (target <= 0.65) | B5 escape (>= 85%) | hat density | syncopation | nearest real bar, median cells | within 2 cells of a real bar |
|---|---|---|---|---|---|---|
| Real held-out bars | 0.49 | 60.4% | 0.18 | 0.16 | 1 | 74.8% |
| App dump (C++ today) | 0.765 | 90.9% | 0.34 | 0.22 | 7 | 12.2% |
| Old proposer (sim) | 0.826 | 96.4% | 0.34 | 0.29 | 8 | 7.8% |
| **New: hat density (recommended)** | **0.591** | **93.0%** | **0.23** | **0.19** | 7 | **2.1%** |
| New: hats uncapped | 0.651 | 98.2% | 0.29 | 0.19 | 6 | 9.1% |
| New: hats + ghost snares | 0.739 | 93.2% | 0.23 | 0.25 | 7 | 1.0% |
| New: hats + extra perc lane | 0.740 | 97.4% | 0.22 | 0.27 | 8 | 1.6% |
| New: hats + fill | 0.827 | 100% | 0.20 | 0.30 | 9.5 | 0.3% |
| New: all five moves | 0.872 | 98.4% | 0.20 | 0.34 | 10 | 0.5% |
| New: everything except hats | 0.901 | 79.7% | 0.23 | 0.35 | 7 | 6.5% |

Real hat density 0.18 is the held-out bars (mostly rock/Lakh); the cards alone sit at 0.19. Noise is about +-0.03 AUC (6 seeds x 33 cards, 250 sampled bars).

## What the data says

- **The whole B4 gap is hat density plus syncopation, and hat density is the part that can be fixed cheaply.** Resampling the hat lane from real hat counts takes AUC 0.826 -> 0.591 and keeps escape at 93%.
- **The other drummer moves make it worse, not better.** Ghost snares, fills, extra percussion and kick pickups each add syncopation (0.19 -> 0.25 to 0.34), because the candidate scorer rewards syncopation and distance from the card, so it picks exactly the candidates that look least like real bars. Honest conclusion: "fills and ghosts where real bars put them" did not help at the mask level. Real bars are mostly plain.
- **Not a copy.** Only 2.1% of takes are within 2 cells of any of the 28k unique real training bars (real held-out bars themselves: 74.8%, since the corpus repeats patterns). Median distance 7 cells of 128, the same as the current proposer.
- **Caveat on "per style".** The training split has only a handful of family-labelled bars for the electronic families (techno 5, trance 4, drum_and_bass 4, house 38, no breaks or bass_music), so the family hat table falls back to all bars for nearly every card. The gain comes from "draw the hat count from real bars, never above the card + 4", not from style-specific counts. A style-aware version needs the critic's drum clusters (`drum_fam`) as the key; not tested.
- The cap matters: card + 2 gave 0.589 / 87.8% escape, card + 3 0.586 / 89.6%, card + 4 0.591 / 93.0%, no cap 0.651 / 98.2%. Card + 4 is the pick (margin on both gates).
- Using the shipped `kDrumStepP` for the step weights instead of a family table gave the same result (0.589 / 88.0% at card + 2), so the port needs no new step table.

## Method

`groove_sim.propose` draws 8 candidates per take and `judge` picks the best. New generator `drummer_gen` in `tools/universe/groove_2h.py` replaces the three old modes:
for lanes 2 (and lane 3 if the card has open hats): `n = random real hat count`, `n = min(n, card hits + 4)`, then choose `n` steps weighted by step probability x3 where the card already has a hit. Everything else (judge, step pass, ghosts, fills) is unchanged. Stats come from the train split only; B4's real side is the held-out split. `groove_sim.py` got two small edits (`CUR`, the current card; `bars()` also returns per-step `v`).

## Exact C++ change that would port it (`SampleStyles.cpp`, `ProposeGroove`, not made)

1. Add to `GenerateSamplesCritic.inc` (emitted by `export_critic_inc.py`): `kHatCount[2][N]`, an array of real hat counts for lane 2 and lane 3 (about 64 entries each, drawn from unique train bars).
2. In the candidate loop replace the `mode` switch with, for `lane` in {2, 3 if `baseM[3]`}:
   `int n = min(kHatCount[lane-2][cr.Range(N)], PopCount16(baseM[lane]) + 4);`
   then pick `n` distinct steps without replacement, weight `kDrumStepP[lane][s] * (baseM[lane] >> s & 1 ? 3 : 1)`, into `m[lane]`. Keep `if (!ambient && m[0]==0) m[0]=0x0101u` and `CriticJudgeDrum`.
3. Keep the `mine` (UserStyle) block as is; the 8 candidates now differ only in hats, so taste scoring has less to choose from. Option: keep 2 of the 8 candidates on the old modes.
4. Rebuild with `ibuild.py`, re-dump `app_drums.jsonl`, re-run `benchmark.py` (B4 drums, B5).

## Risk

- Every drum take changes (hats thinner, less 16th-note hat spam). That is the intended effect but it is audible on every style; the A/B set is the check.
- Variety: candidates differ only in hats, so takes from one card are more alike than today. Escape stays 93% and nearest-bar distance 7, but pairwise diversity (B5) was not measured here and could drop; check it first after a port.
- Sim-to-app offset: the sim over-states AUC by about 0.06, so the app should land near 0.55 to 0.6. Not verified.
- Genres whose real feel is dense hats (hard techno, trance 16ths) are capped by the card + 4 rule and the `kDrumFloor` log-likelihood floor, but with only 4 to 5 labelled bars the count table does not know them; listen to those first.

## Listen

`~/Infinite/training-data/eval/groove_ab/`: `old_<style>_<1..3>.mid` and `new_<style>_<1..3>.mid` for deep_house, detroit_techno, neurofunk, breakbeat, uk_garage, lofi_beats. Two bars looped twice, GM drum map, same seeds. New takes also use velocity accents from the real rows (mean velocity per lane and step) with a little jitter; old takes use the app's fixed levels. So A and B differ in both hats and dynamics: the AUC gain is hats only.
