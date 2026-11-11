# Prompt: finish the real-time engine plan (paste into a new session)

```
Work in /Users/namansoni/Infinite/infinte-base. Goal: finish every open step of
docs/plans/realtime-engine/PLAN.md unattended. Read these first, in this order:
docs/plans/realtime-engine/AUTORUN.md (goal, rules, order, log; continue from its first open row),
PLAN.md, then designs/ (owner rows, do not build them).

STATE AT HANDOFF (verify with git status / git log, do not trust this blindly)
- Branch feature/engine-d-shading. Done: 1.1a (33eddee2), autorun docs + sweep tiers (2d607ce8).
- Staged in the index = step 1.2 (engine error on the node + point-LOD VOutP uv/lp fix).
- Unstaged = step 1.3: rest of src/engine/rhi/MetalDevice.mm (MatRec) + untracked src/nodes/Render3DMaterial.h.
- Untracked, not wired: Render3DDenoise.h (1.5), Render3DFresnel.h + Render3DBsdf.h (1.6 A/Bs, used only
  when tools/blender-ab/toggles/{fresnel,ggx}.py are switched on; all toggles are OFF).
- Sweep of 1.2+1.3 (build already has both): raster 0 worse, live 0 worse of 43. Settle was stopped before it
  finished; post_settle.json (15:06) is the stale 1.1a file. First job: run settle only,
  `sweep.py run --engine settle --tag post` (background), then `diff --engine settle`. 0 worse -> commit 1.2
  (`git commit`), then `git add` the 1.3 files and commit, log both in AUTORUN.md, rotate post -> pre.

ORDER
D close (trackers) -> E on feature/engine-e-speed: 1.4 profile boat with INFINITE_ENGINE_PROF (upload is
already revision-gated; find the ~160 ms fixed cost), refit/upload gating, many-object rays; 1.5 wire the
denoiser (pt_fs MRT acc/aux/gd -> dn_prep -> a-trous steps 1,2,4,8,16 -> ptres remodulates) + adaptive stop,
gate settle 9/9 <= 0.05 FLIP at <= 2.5 s/frame; 1.7 preview export -> 1.6 A/Bs one at a time (ggx, falloff
cap, Fresnel, settle lobe probabilities; keep only winners, delete losers' headers and toggles) -> 1.9 ->
F (2.1-2.3) -> 2.4-2.9 -> Phase 3 fix/enh rows not needing 3.2 -> 4.4, 4.5 -> 5.4, 5.5.

TESTING (cheap first, the Mac is an 8 GB M2)
| tier | when | command |
| 0 | refactor that must move no pixel | sweep.py run --spp 16 --tag pre/post, then sweep.py same --engine E --spp 16 |
| 1 | real change | sweep.py run --spp 64 <only the cases it can touch> pre/post, then diff --spp 64 |
| 2 | block close only | full sweep.py run on all 43 cases, raster/live/settle, then diff |
- Pick the tier before building; write the case list for tier 1 in the AUTORUN log row.
- Raster-only change: sweep raster only. Settle-only change: settle only.
- Truth renders are cached by the harness; never re-render Blender unless a case file changed.
- Engine self-test after each build: INFINITE_ENGINETEST=1 script -q /dev/null timeout 300
  build/Infinite.app/Contents/MacOS/Infinite (R1 speed gate fail is the known open 1.4 item).
- One build at a time via python3 scripts/ibuild.py build --mode release; never build while a sweep runs.
- Run sweeps in the background (run_in_background) and check they are alive within a few minutes.
- Budget: no single job over ~20 min; a step failing its gate after 3 measured attempts is logged and skipped.

RULES
- Keep a change only if no case is > 0.005 FLIP worse and the step's gate moves the right way.
- One branch per block, commit per step, no attribution lines (no Co-Authored-By, no "Generated with").
  Never push, never merge to main.
- Owner rows and anything adding a node, mode, panel or UI: design doc only in designs/, mark "waiting for
  owner", move on.
- Clean room: papers only, never Cycles/Blender/GPL source. 3D stays on GPU. No downloads without asking.
  No agent fan-out or extra sessions. Don't UI-script Infinite. Tables and bullets, no Mermaid.
- Keep disk low: delete old sweep img/ folders and merged worktrees.
- Update the AUTORUN.md log after every step (state, numbers, commit). End with a table of every step:
  done / skipped (why) / waiting for owner.
```
