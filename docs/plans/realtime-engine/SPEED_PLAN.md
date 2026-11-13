# Speed plan after the judge table (2026-11-13)

Goal: settle at least 5x faster than GL Path trace at the same picture, and a real frame rate for live, measured
on scenes that stress different things. Export quality is never traded for speed.

## 1. What the numbers say today

| Fact | Number | Meaning |
|---|---|---|
| Settle, 640x480, 64 spp | 5.5 s wall | about 2 s is launch and scene build |
| Settle, 1920x1440, 256 spp | 120 s wall | about 6 million path samples a second at 8 bounces |
| GL Path trace, same frame | 315 s | settle is 2.6x faster, gate is 5x |
| Live, one frame, 1920x1440 | 39 s wall | a cold launch; says nothing about frame rate |
| Live orbit, 960x720 | 10.6 s a frame | 30 separate launches; same problem |
| Cycles, 1024 spp | 194 s wall | Blender's adaptive sampling is on by default in the harness, so 1024 is a cap, and the time includes start-up |

So the judge table measures the picture well and speed badly: every time is a cold launch of the app.

## 2. Do we need another scene

No new scene. Three existing ones cover what one scene cannot, and the timing method changes.

| Scene | Stresses | Exists |
|---|---|---|
| judge_table | glass, layered Field materials, depth of field | yes |
| ref_interior | closed room, light only by bounces | yes |
| carpet_pile (200 k copies) | geometry and ray structure size | yes |

## 3. Blocks

### Block S0: a ruler for speed (first, small)

| Step | What | Done when |
|---|---|---|
| S0.1 | Warm timing in the harness: one launch, then report scene build ms, ms per settle sample pass, ms per live frame after warm-up (frames 2..31) | three numbers per scene, repeatable within 5 % |
| S0.2 | Speed set = the three scenes above at 1920x1080 | one table in AUTORUN |
| S0.3 | Cycles timed fairly: adaptive sampling off, render time from Blender's own log | Cycles column comparable sample for sample |
| S0.4 | Equal-picture timing for settle: seconds until FLIP against its own 1024 spp frame is 0.01 or less | one number per scene; this is the number the 5x gate uses |

### Block S1: settle speed (ordered by expected gain; each is a guess until S0 measures it)

| Step | What | Why it should pay | Keep only if |
|---|---|---|---|
| S1.0 | Owen-scrambled Sobol samples (Burley 2020) in place of white-noise PCG, 16 dimensions per path event; added after S0 showed settle and Cycles cost the same per sample on ref_interior (586 vs 578 ms at 1080p) while Cycles is cleaner per sample (FLIP 0.0185 vs 0.0278 at 16 spp) | evenly spread samples converge faster at the same cost; direct light from area lights and depth of field gain most | equal-picture time drops 1.3x or more on judge_table and ref_interior, picture at 1024 spp unchanged within 0.003 |
| S1.1 | Per-pixel adaptive sampling. Today the stop is one decision for the whole frame (95th percentile of error) | most of the table frame is a flat surface that is clean at 16 to 32 spp; glass and edges need the full count | equal-picture time drops 2x or more, FLIP against full count 0.005 or less |
| S1.2 | Denoiser guides through glass: albedo and normal of the first non-glass hit | glass regions now denoise as one white surface, so they need many samples to look clean | glass at 64 spp matches 256 spp within 0.01 |
| S1.3 | Cheaper Field materials after the first bounce: no bump taps (three program runs a hit) on later bounces. Found already in place (2026-11-13: bump taps only when b == 0); what is left is the one program run a hit | later bounces already see the smooth normal for maps | ms per pass drops 10 % or more, picture unchanged within 0.002 |
| S1.4 | Path length by throughput: roulette from bounce 1 on diffuse. Today every branch (diffuse, glass, fog, subsurface) starts it at bounce 2, so this is one bounce earlier, a small gain | closed rooms spend most rays on dim deep bounces | ref_interior pass time drops 15 % or more, brightness within 1 % |
| S1.5 | Preview only: settle at 67 % size through the MetalFX scaler live already uses | first clean picture sooner while working | never used on export |

**S1.1 result (2026-11-13): dropped.** Built and measured at 960x540, 256 spp, against the same build tracing every pixel
(commit 6d638de0, reverted in 6789116c; bring it back by reverting that revert):

| Variant | judge_table GPU s | FLIP vs full | ref_interior GPU s | FLIP vs full |
|---|---|---|---|---|
| every pixel | 27.9 | 0 | 47.8 | 0 |
| per-pixel stop 0.05, from 16 spp | 21.0 (1.33x) | 0.0155 (picture darker) | | |
| stop 0.05 from 128 spp | 21.7 (1.29x) | 0.0091 | | |
| stop 0.05 from 64 spp, 3x3 neighbours must agree | 23.7 (1.18x) | 0.0068 | 43.5 (1.10x) | 0.0034 |
| stop 0.025 from 64 spp, 3x3 neighbours | 27.2 (1.03x) | 0.0007 | | |

Why it does not pay: the time goes into glass, edges and dim corners, which do not converge before the count runs out;
the flat table is cheap per sample already. A pixel judged on its own stops early when its samples happened to miss a
bright path, so the picture darkens (the 3x3 rule fixes most of that and takes the gain with it). It also needs two more
full-size RGBA32F targets (66 MB at 1080p). Better routes to the same goal are S1.2 (denoise through glass) and better
sampling of the dim corners (S1.5), not stopping the easy pixels.

### Block S2: live speed and the glass path

| Step | What | Done when |
|---|---|---|
| S2.1 | Read the S0 live numbers per pass (shadow mask, reflections, GI, denoise) | the slowest pass named per scene |
| S2.2 | Glass with something inside: refract through nested objects, not one | live glass on judge_table is clear in all 30 orbit frames |
| S2.3 | Cut the slowest pass (half-size, fewer rays, reuse across frames), one change at a time | 30 fps at 1080p warm on the M2 for judge_table, picture within 0.01 of before |

## 4. Gates

| Gate | Target |
|---|---|
| Settle vs GL Path trace, equal picture | 5x faster on all three scenes |
| Settle vs Cycles, equal picture | faster on all three scenes (Cycles s to 0.01 / settle s to 0.01 above 1), both on the M2 GPU, Cycles adaptive off, 8 bounces each |
| Settle export | FLIP against the full sample count 0.005 or less with every speed-up on |
| Live | 30 fps at 1080p warm on judge_table; ref_interior and carpet_pile reported, no gate yet |

Equal-picture runs use 960x540 so a 1024 spp reference stays under a few minutes. When a ladder ends above
FLIP 0.01, the time is carried on along the last two points' log-log line and marked "~" in the table.

## 5. Order with the picture work

S0 first (it also fixes the Cycles column). Then transmission in settle (absorption, touching media, the dark
water surface) before S1, because S1.2 changes how glass is sampled and should start from correct glass.

## 6. Test queue (run when the laptop is free, in this order, from tools/blender-ab)

| # | Command | Time | Answers |
|---|---|---|---|
| 1 | `python3 speed.py equal carpet_pile --engines settle,pt --budget 15` | ~25 min (two 1024 spp references) | the last S0 cell: settle vs GL Path trace on the ray-structure scene |
| 2 | `judge_render.py probe --size 960 720 --tag off` with `INFINITE_BIN=~/.cache/infinite-sweep/bin/before-ulp.app/Contents/MacOS/Infinite` (kept copy, delete after), then `--tag ulp --ref off` with `INFINITE_BIN=$PWD/../../build/Infinite.app/Contents/MacOS/Infinite` | ~3 min | the offset change keeps the picture (FLIP small, glass and water same or clearer) |
| 3 | The same pair with `--distance 3` (tags `off3`, `ulp3`) | ~4 min | the 0.2 mm glass-water gap is no longer stepped over in wide shots |
| 4 | `judge_render.py probe --size 960 720 --lift 0.0005 --tag lift --ref ulp` | ~3 min (Blender writes the moved glbs once) | the light glass base: dark when lifted means the exact glass-table contact is the cause |
| 5 | S1.0 Sobol: `speed.py equal judge_table ref_interior --engines settle --budget 15` (built in with the ulp offset; compare against the S0 rows 42.5 s and 45.2 s) | ~12 min | keep S1.0 only if both drop 1.3x or more |
| 6 | `speed.py equal judge_table ref_interior --engines settle` after each later S1 step | ~5 min each | the keep-or-drop number for that step |

Row 5 also repairs a stale cell: the judge_table settle ladder and its 1024 spp reference in `results.json` were
re-rendered at 15:20 on 2026-11-13 by the S1.1 build with the per-pixel stop on (the ladder flattens at FLIP 0.019 and
reads "never"). Row 5 overwrites both; the ref_interior settle rows were not touched.

Note for picture checks: Cycles' bounce cap of 8 (speed.py) turns half the glass black, because Cycles counts every
glass interface as a bounce; picture truth keeps Cycles' default bounces (blender_scene.py only caps when asked).
