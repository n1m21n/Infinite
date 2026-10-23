---
name: render-hq
description: Rules and procedure for the Render 3D HQ node of Infinite-Turbo 0.71 (Filament-referenced PBR, spherical particle impostors, shadows, GTAO, TAA, bloom, presets) - isolation from the original Render 3D, Apache-2.0 attribution of ported shaders, where the code and shaders live, the BEFORE/AFTER capture of the reference scenes and the checks each session must pass. Use for any session S1-S9 of docs/plans/HQ-071-STRATEGY.md, when touching src/nodes/render_hq/ or shaders/hq_*.glsl, when porting anything from Filament, or when asked to compare Render 3D and Render 3D HQ.
---

# render-hq

Read first, in this order: `docs/plans/HQ-071-STRATEGY.md` (section 6 says which session is next and
what its exit check is), `docs/RENDER-3D-HQ-FILAMENT.md` (map of the current Render 3D, findings,
baseline), `docs/filament-notes.md` (Filament formulas and defaults at a pinned commit; port from
there, not from memory).

## Rules

1. **Render 3D is frozen.** No new feature in `Render3DNode` or its shaders. A refactor of the shared
   kit (`Render3DTemporal`, `Render3DPost`, `Render3DMotion`, `Render3DCull`) keeps the Render 3D
   output identical: `CULLTEST`, `TAATEST`, `R3DFXTEST` and the compat run must not change.
2. **HQ is isolated.** Its code goes in `src/nodes/render_hq/`, its shaders in `shaders/hq_*.glsl`
   (auto-embedded). Everything is built only with `INFINITE_ENABLE_RENDER_HQ=ON`; with OFF the node
   stays registered and its panel says the module is not in this build (RTX stub pattern,
   `nodes/RtxStub.cpp`).
3. **Patches open unchanged.** Old patches never contain the HQ, but HQ params still follow R1: a new
   param goes at the end of `VisitParams`, never renamed or reordered once a session is committed.
4. **Port techniques, not the engine.** No Filament library, no `filamat`, no material compiler.
   Each ported shader starts with a header:
   ```glsl
   // Ported from Filament <path> @ <sha> (Apache License 2.0, Copyright Google LLC).
   // Changes: <what differs: GL 4.6 forward pass, no reversed Z, ...>
   ```
   and `THIRD_PARTY_LICENSES` / `docs/RENDER-3D-HQ-FILAMENT.md` section 14 list it.
5. **No CPU wait per frame.** No `glGet*`, `glGetUniformLocation`, `glFinish`, sync readback or
   `glMapBuffer` with read in the frame. Uniform locations cached at link; GL state saved once and
   restored at the end of the cook (the 2D nodes rely on it).
6. **Measure before and after.** Each session records its numbers in section 6 of the strategy.

## Reference scenes and captures

Scenes A-H, one renderer per patch: `examples/hq/raster/*.inf` (Render 3D) and
`examples/hq/filament/*.inf` (Render 3D HQ) (regenerate with `python scripts/make_hq_scenes.py`; HDRI with
`scripts/make_hq_studio_hdr.py`). Both folders hold the same scenes with the same sources, camera,
lights and HDRI (extend `make_hq_scenes.py`, do not edit the .inf by hand).

```
powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\run-turbo-tests\capture.ps1 -PatchFiles examples\hq\raster
powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\run-turbo-tests\capture.ps1 -PatchFiles examples\hq\filament -Type "Render 3D HQ" -OutDir build\hq\after
```

Each node of `-Type` is saved as `<OutDir>\<scene>_<nodeIndex>.png` (upright, opaque) and its frame
time goes to `<OutDir>\captures.txt`. One run per scene: for timing comparisons use
`perf.ps1 -PatchFiles`. Look at the PNGs (Read tool) before claiming a visual result.

## Checks per session

- Build: `build-windows.bat nopack` with HQ ON, and once with HQ OFF (the stub must link).
- Quick: `driver.ps1 -SkipBuild -Only <the session's test>,CULLTEST,TAATEST,R3DFXTEST`.
- Captures of A-F (BEFORE untouched in `build\hq\before`, AFTER in `build\hq\after`).
- Before any commit (only after S3, with the owner's OK): the full gate,
  `driver.ps1 -Perf -Compat` then `driver.ps1 -Mit -Quick`.

## Cheap delegation

Mechanical work goes to cheaper agents (haiku/sonnet): regenerating scenes, running captures and
summarising `captures.txt`, grepping call sites, filling licence tables. Shader math, the node
architecture and the review stay in the main session.
