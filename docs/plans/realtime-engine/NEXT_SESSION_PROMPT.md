# Prompt: finish the real-time engine plan (paste into a new session)

```
Work in /Users/namansoni/Infinite/infinte-base. Goal: finish every open step of
docs/plans/realtime-engine/PLAN.md unattended. Read first, in order:
AUTORUN.md (rules, order, log; continue from the first open row), PLAN.md, designs/ (owner rows: do not build).

STATE AT HANDOFF (check with git status / git log; don't trust this blindly)
- Branch feature/engine-e-speed. Blocks D and E are closed except the leftovers below.
- 1.6 is done: exact Fresnel + multiscatter gain are default on raster/live, the settle glass lobe is
  default, castShadow works on Metal. `ggx` (height-correlated G2) is still an --ab switch, not default.
- Sweep baselines in ~/.cache/infinite-sweep/: pre_raster/post_raster (current code), post_live (before
  Fresnel+ms; re-run before a live diff), post_settle_s16 (current code for the 8 touched cases).

NEXT
1. Read FINISH_PLAN.md: it has the method, paper, switch, cases and cost for every open row.
2. Do Tier A top to bottom (1.6 leftovers, 2.1, 2.9, 2.8, 1.5 adaptive stop, 2.4), then Tier B.
   Owner rows (Tier C) stay designs. 3.4, 3.7 wait on 3.2; 5.1, 5.6 wait on 3.6.
3. Still-open items to retry once:
   - 1.4 R1 gap (Metal raster 0.87x/1.02x GL on B4 m/l; needs CPU-side raster work).
   - 1.5 adaptive stop, plus the 9/9 <= 0.05 gap (tracer bias on materials and cornell).

TOOLS
- Sweep: sweep.py [--engine raster|live|settle] [--tag T] [--base T] [--spp N] [--ab a,b] [--touch G,G]
  [--skip G] [--budget MIN] run|diff|same|pick|times [cases].
  - New candidate = `#ifdef R3D_AB_<NAME>` in the shader, then `--ab name`: one build, many variants.
  - Screen on raster with `--touch G --skip hair`; confirm the winning set once on live and settle --spp 16.
  - Results merge and save per case; a killed run keeps what it finished.
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
