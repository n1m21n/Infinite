# Perf areas (16 slots, ordered by what the user feels: audio > projector > canvas > previews)

Each area slot works the same 6 steps (plus a web research pass from TECHNIQUES.md 6 before step 6):
1. **Contract**: write the metric and its target (from internal/plans/perf/benchmark-suite.md section 6 and the README scoreboard). No target = say so and propose one in the issue.
2. **Today**: read the area's rows in README (Scoreboard, Top costs, Found while measuring, open items). Start at the biggest slice, not the first file.
3. **Measure** if the machine is quiet (bench id below); else **trace** the hot path (TECHNIQUES.md 2) and label numbers `estimated`/`traced`.
4. **Sweep the code**: at least 8-12 files of the area (find them with skill `codebase-navigation`; source roots are src/audio, src/app/frame, src/app/graph, src/app/bodies, src/nodes, src/core, src/engine, src/platform). Log every audited-and-clean file.
5. **Siblings**: for each candidate, grep for the same idiom elsewhere (TECHNIQUES.md 4).
6. **Scope and file** per COMMON.md issue template; check the proposal against the hard constraints.

| N | Area | Metric and target | Bench / tool | Files to start from | Skills to read |
|---|---|---|---|---|---|
| 0 | Audio callback: DSP cost | cb_load p99 <= 50% at 256, 0 xruns | B1, B3, B7 | src/audio (AudioEngine.cpp, AudioVoice.cpp, AudioWorkerPool.cpp, DspMath.h, EffectDefs.cpp), audio nodes in src/nodes | audio-pipeline-sweep, new-audio-node, audio-node-sweep, param-truth-audit |
| 1 | Audio latency | input-to-sound ms at 64/128/256, xruns 0 | tools/re-survey/latency_probe.py, loopback_rec.py | AudioEngine.cpp, NoteEventQueue.h, CompensationDelay.h (PDC), MIDI in/out in src/platform | audio-pipeline-sweep (PDC), av-sync-sweep |
| 2 | Audio memory + allocation | zero alloc/lock/IO in callback, RSS growth < 2%/30 min | B7 soak, `leaks`, grep patterns | AudioBuffer.h, AudioVoice.cpp, nodes with per-voice state (reverb, delay, sampler, wavetable) | audio-node-sweep, infinite-code-review (real-time) |
| 3 | Projector / output pacing | interval p99 <= 18.3 ms, missed vsync < 0.5%, input-to-photon <= 2 frames | B3, B8 | src/app/frame (StagePresent.cpp, StageFramePump.cpp), Output nodes, Syphon/Spout in src/platform | output-projection-sweep, render-pipeline-sweep |
| 4 | Canvas frame (UI + node bodies) | B6 pan p50 <= 17.2 ms, p95 <= 22.8 ms; node_bodies ms | B5, B6 | src/app/frame (StageNodeBodies.cpp, StageLayout.cpp, StageLinks.cpp), src/app/bodies, src/app/ui | node-ui-pillars, infinite-design-system, node-ui-sweep |
| 5 | Cook graph | cook_all ms, redundant re-cooks = 0 | B2, B4, B6, `CookProbe` | src/app/frame/CookFrame.cpp, src/core/CookProbe.*, node cache/revision code | compositing-pipeline-sweep, data-accuracy-sweep |
| 6 | Render / GPU pipeline | frame ms, `fbo_allocs_steady` = 0, passes per effect | B2, B4 | src/engine, Effects/Color filters (FilterDef pattern), blur, compositing, shaders | render-pipeline-sweep, new-effect-node, compositing-pipeline-sweep |
| 7 | Media decode + upload | decode real time, 0 dropped, upload CPU ms | B8, B10 | src/platform (Platform.mm, MediaLinux.cpp, Windows media), Video/VideoIn nodes, AudioDecodeCache | output-projection-sweep, av-sync-sweep, linux-parity, windows-parity |
| 8 | 3D / geometry / realtime engine | scene frame ms, cull rate, draw calls | Ocean scene, 3D View, docs/plans/realtime-engine | src/engine, geometry nodes, Bvh.*, EditMesh.*, BrushEngine.* (sculpt) | geometry-transform-sweep, new-geometry-node |
| 9 | Field compiler and kernels | kernel ms per pixel/sample, compile ms | tests/field/corpus.txt, Field nodes | Field compiler/backends, Field Synth/Effect/Modifier/Pixel | field-compiler, field-realtime, field-state, field-testing |
| 10 | Startup, load, open patch | cold start s, open-patch s, RSS at idle | app_bench.py, load_bench.py, `scripts/fresh_profile.sh` | src/app/Startup.cpp, AppState.cpp, preset/help/font/shader init, patch load/save | infinite-lifecycle, run-infinite-hygiene |
| 11 | Export / offline render | export fps, wall time, output_hash unchanged | B10, tools/render-check.py, determinism-check.py | recorder/export path, AudioFileWriter.*, ExportImage.cpp, render-take | av-sync-sweep, output-projection-sweep |
| 12 | 2D View + paint | M3 composite 20 layers <= 4 ms (README: still failing), edit path | `INFINITE_VIEW2DTEST` M3/M3b | 2D View compositor, BrushEngine, layer cache (plan internal/plans/2d-view) | compositing-pipeline-sweep |
| 13 | Platform twins | Windows/Linux paths do not copy/upload/poll more than macOS | static + `gh run list` CI times | every `Platform::` function with 3 sides, src/platform/linux, windows | windows-parity, linux-parity, pillar-parity-audit |
| 14 | Previews + thumbnails | preview ms per node, cost when off-screen | B6 offscreen_bodies, B2 | node preview/thumbnail code, 3D thumb, gallery cache | node-ui-pillars |
| 15 | Knowledge + digest (weekly) | n/a | skill prior-art-scout | see below | prior-art-scout |

## Slot 15: knowledge work and digest
1. Pick the area with the most open `perf-candidate` issues or the largest unproven target in README. Do the wider research pass (every area slot already does a short one): peers' public issues, PRs, release notes, docs, papers, talks: techniques for lock-free audio queues, SIMD layout, dirty-region UI redraw, display-link pacing, GPU timer queries, texture streaming, denormal handling, block-size adaptive DSP. Never GPL source (AGENTS.md rule 1).
2. For each technique that could apply: measure the matching Infinite cost on the quiet machine (or trace it), and write ONE `perf-candidate` issue with the link, the technique in our terms, and our numbers. No link-only issues.
3. Re-rank candidates.md and open ONE `perf-digest` issue (label `perf-digest`, `auto-perfhunt`): table of every open `perf-candidate` with area, metric, gain, evidence tier, effort, risk, suggested release bundle (ordered into at most 3 blocks, one branch per block, per the owner's chunking rule). Close nothing; the owner decides.
4. Record in perfhunt-state/lessons.md which techniques were rejected and why (so they are not re-proposed).
