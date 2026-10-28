# Making the pipeline faster without losing quality (2026-10-28)

Rule: a speed-up is kept only if the frozen test scores and the stored rows of the windows we keep are unchanged (bit-identical audio features, or a stated tolerance).

## Where the time goes (per preset, one shard; 2,000 presets = 720-880 s on 4 shards, so ~1.6 s each)

| Piece | Cost | How known |
|---|---|---|
| Python analysis of the 8 windows | ~0.5 s (3 full windows 0.09 s each, 5 light 0.05 s each) | measured on synthetic audio today |
| Render + process start (release build: one Infinite process per patch, 24.5 s of audio) | ~1.1 s | by subtraction; split between start-up and audio not yet measured |
| Model training per round (3 seeds x 300 epochs, CPU) | minutes per round | from round logs |

## Options

| # | Idea | Expected gain | Quality risk | State |
|---|---|---|---|---|
| 1 | Render only the windows training reads. `sound_data.build` reads C3, C4, C3f only; p3 and the forward model use C3 only. The other 5 of the 8 windows (C1, C5, C6, C3pp, C3mf) are rendered and analysed but never read until G1 uses them | audio per patch 24.5 s -> 3.5 s (C3 only); analysis 0.5 s -> 0.1 s | none for the windows kept, if the first window is identical (a note's start does not depend on later notes); **to verify** | built: `own_sweeps.py run --notes C3` (default unchanged) |
| 2 | Overlap analysis with the next render (pipeline inside a shard) | up to ~30% | none, same functions | not built |
| 3 | Full 8 windows only for a sample (e.g. 1 in 10, plus final set) | keeps G1 pitch/velocity data at 10% of the cost | none for G1 stats if sample is random and recorded | plan |
| 4 | Stop rounds when the rule says so (two rounds < 0.02 gain) | avoids wasted rounds | none, rule is already pre-registered | in step4_loop |
| 5 | Candidate scoring and training on the GPU (MPS) | seconds per round | fp differences; check score within 0.01 | not tried (MLP is small; CPU may win) |
| 6 | Reuse features: never re-measure an unchanged row (cached .npz, resumable by id) | already true | none | done |
| 7 | Cap embeddings (`--limit`), CLAP+EnCodec on MPS at 0.17 s each | already applied | stated in the claim | done |

## Check before using #1 in rounds (needs one quiet slot)

1. Render 20 existing presets with `--notes C3` into a scratch file; compare each C3 row (mel, bands, detail) to the stored one: expect identical.
2. Measure seconds per preset, all 8 vs C3 only, same shard count. Record in HANDOFF.md.
3. If identical: `step4_loop.py` passes `--notes C3` for rounds r02+; the rows stay valid for P3 because those use C3 only. If any difference: stop and keep the 8-window path.

## Brain parallel (for the chain track)

Efficient coding: spend resources where surprise is high. Active learning is that for rendering (render where the seeds disagree); #1 is the same idea for what we measure (measure only the windows that carry information for the claim being tested).
