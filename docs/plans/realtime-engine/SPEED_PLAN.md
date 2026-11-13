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
| S1.1 | Per-pixel adaptive sampling. Today the stop is one decision for the whole frame (95th percentile of error) | most of the table frame is a flat surface that is clean at 16 to 32 spp; glass and edges need the full count | equal-picture time drops 2x or more, FLIP against full count 0.005 or less |
| S1.2 | Denoiser guides through glass: albedo and normal of the first non-glass hit | glass regions now denoise as one white surface, so they need many samples to look clean | glass at 64 spp matches 256 spp within 0.01 |
| S1.3 | Cheaper Field materials after the first bounce: no bump taps (three program runs a hit) on later bounces | later bounces already see the smooth normal for maps | ms per pass drops 10 % or more, picture unchanged within 0.002 |
| S1.4 | Path length by throughput: roulette from bounce 1 on diffuse, as glass already does from bounce 2 | closed rooms spend most rays on dim deep bounces | ref_interior pass time drops 15 % or more, brightness within 1 % |
| S1.5 | Preview only: settle at 67 % size through the MetalFX scaler live already uses | first clean picture sooner while working | never used on export |

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
