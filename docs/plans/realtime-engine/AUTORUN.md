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
  | 0 | refactor meant to move no pixel | `sweep.py run --spp 16` pre/post, `sweep.py same`: any pixel change fails |
  | 1 | real change | `run --spp 64 <cases it can touch>` pre/post, `diff` |
  | 2 | block close | full `run` on all 43, `diff` |

  The Air (SSH, resolve with `dns-sd` first) is the reserve if a tier-2 sweep must run while building here.

## Order

D (1.1–1.3) -> E (1.4–1.7) -> 1.6 BSDF A/Bs (ggx, falloff cap, Fresnel: toggles in
`tools/blender-ab/toggles/`) -> 1.9 -> F (2.1–2.3) -> 2.4–2.9 -> Phase 3 fix/enh rows that do not need
3.2 -> 4.4, 4.5 -> 5.4, 5.5. Owner rows get designs along the way.

## Log

| step | state | numbers | commit |
|---|---|---|---|
| 1.1a sheen + tonemap one source | done | raster/live/settle 0 worse of 43 | 33eddee2 |
| 1.2 engine error on the node | done | self-test caught the point-LOD pipeline (VOutP lacked uv/lp since R5); fixed. raster/live 0 worse of 43 | e6b2e971 |
| 1.3 material record | done | raster identical; live 0 worse of 43; settle 20 touched cases identical to 1.1a (full 43 stopped: over the 20 min cap) | 64bccfea |
| 1.4 speed | done (boat gate); R1 on B4 m/l still open from before | export warm-ups reuse one sun shadow mask: live boat 2.05 -> 1.71 s/frame, FLIP 0.0 vs before; 4 sun cases identical. Refit and half-res many-object rays were already in; B4 untouched (export-only change); its R1 gap (Metal raster 0.87x/1.02x GL at m/l) needs CPU-side raster work, not tried. Mask pass is ~12 ms of the ~41 ms sun cost; the rest is r_fs penumbra fallback (not cacheable pixel-exact) | 6f2b5903 |
| 1.5 settle denoiser | done (time gate); 9/9 <= 0.05 not met | a-trous on albedo-demodulated light (steps 1,2,4,8,16; normal/depth/luminance stops, luminance stop uses both pixels' variance, albedo stop 0.03). settle 16 spp plain vs denoised: 0 worse of 43, 24 better (cornell 0.154->0.110, light_spot 0.081->0.053, interior 0.130->0.067, hero 0.109->0.064; tex_tile 0.030->0.033 within 0.005). Boat 16 spp 1080p 2.06 s/frame (gate 2.5). materials ~0.072, cornell ~0.110 are tracer bias, not noise. Adaptive stop not built (time gate met without it). Knobs: INFINITE_ENGINE_DENOISE=0, _DNSTEPS, _DNSIGL | see git log |
| 1.7 preview export | waiting for owner | adds an export choice; measured 8 warm-ups = 0.62 s/frame, FLIP 0.041 vs 96 | `designs/1.7-preview-export.md` |
| owner rows 1.8, 3.2, 3.5, 3.6, 3.8, 3.9, 4.1–4.3, 5.2, 5.3, 5.7 | designs written, waiting for owner | `designs/` | |
