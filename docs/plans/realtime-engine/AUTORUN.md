# Autorun: finish PLAN.md unattended (owner away, 2026-11-11)

Owner: "assign yourself a goal and finish every block and every step." This file is the goal and the log; read it
first after any context reset, then continue from the first open row.

## Rules (unchanged by being unattended)

- One build at a time (`scripts/ibuild.py build --mode release`), one sweep at a time; always leave a background
  job running so the session is woken when it ends.
- A/B every change against the previous step's sweep (`sweep.py run --tag post`, `diff`); keep only if no case is
  > 0.005 FLIP worse and the step's gate moves the right way. Rotate post -> pre after each kept step.
- Commit per step on the block's branch, no attribution lines, never push, never merge to main.
- **owner** rows and anything that adds a node, mode, panel or UI: write the one-page design in
  `docs/plans/realtime-engine/designs/<step>.md`, mark the row "waiting for owner", move on. Do not build it.
- Budget: a step that fails its gate after three measured attempts is logged with its numbers and skipped.
- Clean room (papers only), GPU only, keep disk low (remove merged worktrees and old sweep images).
- Test tiers (light load on the 8 GB Mac; all runs at background priority via `taskpolicy -b`):

  | tier | when | how |
  |---|---|---|
  | 0 | refactor meant to move no pixel | `sweep.py --spp 16 --tag post run`, `same`: any pixel change fails |
  | 1 | real change | behind a switch: `--tag X --ab X --touch G run`, `--base post --tag X diff` (PLAN §5) |
  | 2 | block close | `--all run` on all 43, `diff` |

  One build carries every candidate (`INFINITE_R3D_AB`, `src/nodes/Render3DAb.h`); sweeps merge, run fastest
  first and stop at `--budget` (20 min).

  The Air (SSH, resolve with `dns-sd` first) is the reserve if a tier-2 sweep must run while building here.

## Order

D (1.1–1.3) -> E (1.4–1.7) -> 1.6 BSDF A/Bs (ggx, falloff cap, Fresnel: `--ab` switches) -> 1.9 -> F (2.1–2.3) -> 2.4–2.9 -> Phase 3 fix/enh rows that do not need
3.2 -> 4.4, 4.5 -> 5.4, 5.5. Owner rows get designs along the way.

## Log

| step | state | numbers | commit |
|---|---|---|---|
| 1.1a sheen + tonemap one source | done | raster/live/settle 0 worse of 43 | 33eddee2 |
| 1.2 engine error on the node | done | self-test caught the point-LOD pipeline (VOutP lacked uv/lp since R5); fixed. raster/live 0 worse of 43 | e6b2e971 |
| 1.3 material record | done | raster identical; live 0 worse of 43; settle 20 touched cases identical to 1.1a (full 43 stopped: over the 20 min cap) | 64bccfea |
| 1.4 speed | done (boat gate); R1 on B4 m/l still open from before | export warm-ups reuse one sun shadow mask: live boat 2.05 -> 1.71 s/frame, FLIP 0.0 vs before; 4 sun cases identical. Refit and half-res many-object rays were already in; B4 untouched (export-only change); its R1 gap (Metal raster 0.87x/1.02x GL at m/l) needs CPU-side raster work, not tried. Mask pass is ~12 ms of the ~41 ms sun cost; the rest is r_fs penumbra fallback (not cacheable pixel-exact) | 6f2b5903 |
| 1.5 settle denoiser | done (time gate); 9/9 <= 0.05 not met | a-trous on albedo-demodulated light (steps 1,2,4,8,16; normal/depth/luminance stops, luminance stop uses both pixels' variance, albedo stop 0.03). settle 16 spp plain vs denoised: 0 worse of 43, 24 better (cornell 0.154->0.110, light_spot 0.081->0.053, interior 0.130->0.067, hero 0.109->0.064; tex_tile 0.030->0.033 within 0.005). Boat 16 spp 1080p 2.06 s/frame (gate 2.5). materials ~0.072, cornell ~0.110 are tracer bias, not noise. Adaptive stop not built (time gate met without it). Knobs: INFINITE_ENGINE_DENOISE=0, _DNSTEPS, _DNSIGL | d9718319 |
| 1.6 test tooling | done | `INFINITE_R3D_AB` switches (one build, any number of variants), `sweep.py --ab --touch --skip --budget`, per-case save; `toggles/` removed | this branch |
| 1.6 exact Fresnel (raster, live) | done, default | raster 0 worse, 2 better (mat_sheen, mat_subsurface −0.011/−0.013); with ms on live 0 worse of 34, sum −0.033. Not wired into settle `ptEval` or the GL tracer (unmeasured there) | this branch |
| 1.6 multiscatter gain (raster, live) | done, default; settle skipped | raster with Fresnel: 0 worse, 4 better of 30 (sheen 0.133->0.120, sss 0.134->0.120, materials 0.089->0.082, tex_maps 0.082->0.075). Settle: Karis-fit E (= 1−0.55·rough) is too low for height-correlated Smith, so it over-brightens: cornell +0.005, mat_anisotropy +0.011, world_grey +0.012. Two polynomial fits of the true E (max error 0.042, 0.032) not good enough; third attempt = 16×16 LUT texture, left for F1 (FINISH_PLAN) | this branch |
| 1.6 height-correlated G2 (`ggx`) | not default; switch kept | raster 2 better (sheen 0.071, sss 0.087) but 2 worse alone (deer +0.012, geom_monkey +0.007) and 4 worse with Fresnel+ms (cornell +0.009, deer +0.016, monkey +0.015, lights_multi +0.005): those cases are already bright (b 1.06–1.09), Schlick-k was hiding it. Unblock: fix the brightness excess first, then re-run `--ab ggx` | — |
| 1.6 falloff cap | dropped | uncapped inverse square: 8 touched cases exactly +0.000; code removed | — |
| 1.6 settle lobe probabilities | no change needed | settle already picks coat on its own (`pC`) and the cosine half covers sheen | — |
| settle glass lobe | done, default | rough dielectric reflect/refract (Walter 2007) ported from the GL tracer: mat_glass 0.101->0.059, lineup 0.057->0.045, 0 worse of 8 touched at 16 spp. mat_glass still above the 0.05 gate | this branch |
| castShadow on Metal | done | was ignored: TLAS instance mask 0xFF/0xFE + shadow rays mask 1; non-casting lights skip shadow map, RT shadow and settle NEE occlusion. raster 0 worse of 37, live worst delta 0.0005 of 43 | this branch |
| remaining rows | plan | method, paper, switch, cases and cost per row in `FINISH_PLAN.md` | — |
| 1.7 preview export | waiting for owner | adds an export choice; measured 8 warm-ups = 0.62 s/frame, FLIP 0.041 vs 96 | `designs/1.7-preview-export.md` |
| owner rows 1.8, 3.2, 3.5, 3.6, 3.8, 3.9, 4.1–4.3, 5.2, 5.3, 5.7 | designs written, waiting for owner | `designs/` | |
