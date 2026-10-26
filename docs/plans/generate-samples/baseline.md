# Generate Samples diversity baseline (recorded once, 2026-10-26)

Release build of main at the time, 200 takes per part (random seed draws the card, 2 bars, generic card only), 1196 of 1200 rendered (4 failed all redraws). Measured by `tools/gensample_diversity.py`; the ruler (feature std) and near-duplicate threshold are frozen in `baseline.json`. Not re-measured.

Tool correction made before any generator change: the first run defined a near-duplicate against the 10th percentile of pairwise distances, which a nearest neighbour passes almost always (drums 100%); it is now a quarter of the mean pairwise distance. Pairwise distances and means are unchanged.

Reading it:
- Drums are the least diverse (mean distance 3.09, 53% near-duplicates), then percussion (29%). Pads, plucks and synths are separated but cluster in a narrow sound family (see means below).
- "Distinct graphs" is not informative: every take's patch text differs by its seeds, so it counts 200 of 200. Distinct voice sets is the real structural number: 193-199 of 200 differ only by small node-type combinations drawn from the same pools.
- The `all` row is a part the node does not offer; kept for completeness.
- Plucks: decay 1.19 s and onset spacing 0.20 s are not pluck-like (spec: decay 80-600 ms); crest 15 dB.

Targets from section 7 against this: mean pairwise distance at least 1.5x per part (drums 4.6, percussion 6.8, pads 7.6, plucks 6.6, synths 7.8); near-duplicate share under half (drums under 27%, percussion under 15%).

| part | takes | mean pairwise distance | mean nearest-neighbour | near-duplicate share | distinct graphs | distinct voice sets | distinct (voices, root, scale) |
|---|---|---|---|---|---|---|---|
| all | 200 | 3.74 | 1.67 | 17% | 200 | 199 | 200 |
| drums | 200 | 3.09 | 1.14 | 53% | 200 | 196 | 200 |
| pads | 199 | 5.08 | 2.08 | 0% | 199 | 195 | 199 |
| percussion | 200 | 4.54 | 1.51 | 29% | 200 | 193 | 199 |
| plucks | 197 | 4.38 | 2.05 | 1% | 197 | 194 | 197 |
| synths | 200 | 5.17 | 2.18 | 3% | 200 | 199 | 200 |

near-duplicate threshold (a quarter of the baseline's mean pairwise distance): 1.083

| part | centroid | flatness | crest | onsets | spacing | decay | width | low |
|---|---|---|---|---|---|---|---|---|
| all | 8.75 | -2.65 | 14.26 | 33.38 | 0.21 | 1.16 | -14.15 | 0.61 |
| drums | 8.39 | -2.43 | 15.43 | 38.09 | 0.12 | 0.43 | -18.81 | 0.74 |
| pads | 8.52 | -5.23 | 13.15 | 23.52 | 0.47 | 1.80 | -13.58 | 0.21 |
| percussion | 9.59 | -2.73 | 18.28 | 22.80 | 0.26 | 0.90 | -17.51 | 0.37 |
| plucks | 9.14 | -4.29 | 15.26 | 27.95 | 0.20 | 1.19 | -15.16 | 0.19 |
| synths | 7.63 | -5.54 | 13.25 | 35.08 | 0.22 | 1.06 | -18.34 | 0.65 |
