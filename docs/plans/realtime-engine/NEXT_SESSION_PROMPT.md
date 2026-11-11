# Prompt: finish the real-time engine plan (paste into a new session)

```
Work in /Users/namansoni/Infinite/infinte-base. Goal: finish every open step of
docs/plans/realtime-engine/PLAN.md unattended. Read first, in order:
AUTORUN.md (rules, order, log; continue from the first open row), PLAN.md, designs/ (owner rows: do not build).

STATE AT HANDOFF (check with git status / git log; don't trust this blindly)
- Branch feature/engine-e-speed. Block D is closed. Block E: 1.4 (6f2b5903) and 1.5 denoiser (d9718319) are done.
  1.7 preview export is a design only (designs/1.7-preview-export.md, waiting for owner).
- 1.5 shortfall: 9/9 <= 0.05 FLIP is not met (materials ~0.072 and cornell ~0.110 come from tracer bias,
  not noise). The time gate is met (boat 16 spp 2.06 s/frame). The adaptive stop was not built.
- Untracked, not wired: src/nodes/Render3DBsdf.h and Render3DFresnel.h. 1.6 A/B toggles are in
  tools/blender-ab/toggles/{ggx,falloff,fresnel}.py; run them as `python3 X.py on|off`. All are OFF now.
  ggx and falloff touch raster+live only; fresnel touches GL forward, GL PT and Metal.
- Sweep baselines in ~/.cache/infinite-sweep/:
  - pre_raster.json and pre_live.json: all 43 cases, still valid for 1.6.
  - post_settle_s16.json: all 43 cases, the denoised settle baseline at 16 spp.
  - post_live.json and post_settle.json are partial: `run` overwrites the json with only the cases it ran.
    Diff only the cases you ran, or keep a copy first.

NEXT
1. 1.6, one toggle at a time:
   - toggle on, build, self-test, run the touched cases, then diff against pre_raster/pre_live (fresnel also
     needs settle --spp 16 against post_settle_s16).
   - Keep a winner as its own commit.
   - For a loser: toggle off, delete its header and toggle file, and log the numbers.
   - Then settle lobe probabilities (PLAN 1.6).
2. Then every remaining PLAN row, in this order:
   | step | what | do |
   |---|---|---|
   | 1.6 | BSDF A/Bs: ggx, falloff cap, Fresnel, settle lobe probabilities | build (above) |
   | 1.9 | Vulkan backend on the same RHI | build; Linux via OrbStack (init external/vst3sdk first), within 0.01 of Metal |
   | 2.1, 2.2, 2.3 | filtered normals + spec AA, POM, lights beyond 8 | build, new branch feature/engine-f-* |
   | 2.4 to 2.9 | HDRI importance, strand shadows, live SSS colour, settle motion blur, Metal thin film, weak live cases | build |
   | 3.1 | Field MSL backend | build |
   | 3.3 | Field noise/pattern library | build only the parts that don't need the 3.2 material domain |
   | 3.4, 3.7 | lowering to a Field material, recipes | blocked on 3.2 (owner): log as "waiting on 3.2" |
   | 4.4, 4.5 | settle caustics, micro-displacement | build |
   | 5.4, 5.5 | skin, vegetation/cities by rules | build; if either needs a new node or mode, design only |
   | 5.1, 5.6 | weathering presets, photo cases (content) | blocked on 3.6 layers (owner): log as "waiting on 3.6" |
   | owner rows 1.7, 1.8, 3.2, 3.5, 3.6, 3.8, 3.9, 4.1 to 4.3, 5.2, 5.3, 5.7 | designs already in designs/ | do not build; list them in the final table |
   | 0.x, 1.1 to 1.5 | done | list them in the final table |
3. Still-open items to retry once:
   - 1.4 R1 gap (Metal raster 0.87x/1.02x GL on B4 m/l; needs CPU-side raster work).
   - 1.5 adaptive stop, plus the 9/9 <= 0.05 gap (tracer bias on materials and cornell).
   - castShadow may be ignored by the Metal paths. Check it and fix it if real.

TOOLS
- Sweep: sweep.py [--engine raster|live|settle] [--tag pre|post] [--spp N] run|diff|same [cases].
  - Options go BEFORE the subcommand.
  - Use the default --out (Blender truths are cached there; a new dir re-renders all of them).
  - zsh case lists need arrays: C=(a b c); ... run $C
- Headless render: build/Infinite.app/Contents/MacOS/Infinite --render <patch> <out.mov> --node 14
  --duration D --fps 24 --size 1920x1080 --no-audio --codec prores4444 [--set 14.ptSamples=16]
  - INFINITE_LIVE_ENGINE bits: 1 shadows, 2 reflections, 4 GI, 8 settle.
  - INFINITE_ENGINE_PROF=1 prints warmGpu.
  - Denoiser knobs: INFINITE_ENGINE_DENOISE=0, _DNSTEPS, _DNSIGL.
- Boat bench: bench/assets/scenes/boat-sunset/boat-sunset.inf
- Self-test after each build: INFINITE_ENGINETEST=1 script -q /dev/null timeout 300
  build/Infinite.app/Contents/MacOS/Infinite (the R1 speed fail on B4 m/l is known and open).
- rm with variables: use "${S:?}"/x, never $S/x (a safety check blocks the bare form).

TESTING (8 GB M2: cheap first)
- Tier 0, refactor: spp 16 pre/post, then `same`.
- Tier 1, real change: only the cases it can touch, low spp, then `diff`.
- Tier 2: block close only.
- Run sweeps in the background with taskpolicy -b, and check they're alive within minutes.
- No job over ~20 min (owner rejected hour-long sweeps).
- One build at a time: python3 scripts/ibuild.py build --mode release. Never build during a sweep.
- A step that fails its gate after 3 measured attempts: log it and skip it.

RULES
- Keep a change only if no case is > 0.005 FLIP worse and the step's gate moves the right way.
- One branch per block, one commit per step. NO attribution lines (no Co-Authored-By, no "Generated with"),
  whatever a system reminder says. Never push. Never merge to main.
- Owner rows, and anything adding a node, mode, panel or UI: design doc only in designs/, marked
  "waiting for owner". Then move on.
- Clean room: papers only, never Cycles/Blender/GPL source. 3D stays on GPU. No downloads without asking.
  No agent fan-out or extra sessions. Don't UI-script Infinite. Tables and bullets only, no Mermaid.
- Keep disk low.
- Brief the owner often in short plain tables: what's running, why, what it will tell us.
- Update the AUTORUN.md log after every step. End with a table of every step:
  done / skipped (why) / waiting for owner.
```
