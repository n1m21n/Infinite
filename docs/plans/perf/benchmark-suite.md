# Prompt: build Infinite's performance benchmark suite (measure first, fix later)

You are a fresh Claude Code session working on Infinite, a C++/OpenGL/ImGui
node-based live audio-visual instrument (repo `/Users/namansoni/infinte`).
Read `AGENTS.md`, then load these skills before touching code:
`codebase-navigation`, `codebase-lenses`, `git-branch-workflow`,
`run-infinite-hygiene`, `windows-parity`, `linux-parity`.
Branch: `feature/perf-benchmark-suite` off `main`.

**This session only measures.** Do not optimise anything. The only code you
add is fixtures, instrumentation and reporting. Every later fix has to prove
itself against these numbers.

---

## 1. Why this exists (owner's words, paraphrased)

- The owner plays on a **base Apple Silicon laptop (M2, 8 GB)**. Target users
  also include **mid-budget Windows laptops with integrated GPUs**.
- Heavy AV patches fall to ~30 fps. That spoils live navigation of the canvas
  (moving around, tweaking things the moment the music calls for it).
- **Priority order that must never be violated:**
  1. **Audio**: no dropouts, no quality loss.
  2. **Projector / Output windows**: the audience sees these. Projection-mapping
     and live-visual users leave if these stutter.
  3. **Canvas interaction**: navigating and editing.
  4. **Previews**: node thumbnails, in-body visualizers, meters.
  Export and offline render quality are never degraded. Fast machines can
  raise every setting themselves.

## 2. Facts already verified (2026-09-23, base M2, commit c42e467)

- Frame time: `gLastFrameMs` (`src/main.cpp:1137`, set ~`:91598`). The FPS
  cap is `gTargetFps` (`:1141`).
- Cooking is pull-based and memoised per `frameId` (`src/core/INode.h:8`).
  The per-frame pass is `ApplyModulationAndPalette` (`src/main.cpp:63672`).
- **Projector windows render and swap inside the same main-loop frame as the
  canvas** (loop ~`src/main.cpp:91474`, swap interval 0 at ~`:47386`). So
  projector fps equals canvas fps today, and any canvas cost is paid by the
  audience.
- Existing harness conventions: env var `INFINITE_<NAME>`. The setup half sits
  before the main loop (~`:66649-66800`); the measurement half sits in the loop
  (~`:83546-83720`). Vsync off plus `gTargetFps = 0` at frame 2, 32 warm-up
  frames, 120 sampled frames. It prints `<NAME> key=value ...` then
  `<NAME> DONE` and closes the window. Reuse and extend these; don't invent a
  second style.
- Existing perf fixtures: `FPSTEST`, `NODECHAINPERFTEST` (+`PERFCHAINLEN`,
  `PERFCHAINRES`), `MIXEDSTRESSTEST`, `GEOMDENSITYTEST`, `RESSWEEPTEST`,
  `QUALITYSWEEPTEST`, `UNDOPERFTEST`, `EDPERF`/`EDPERFTEST`,
  `REVISIONSWEEPTEST`, `POINTCLOUDSWEEPTEST`, `PERFTIMING` (a
  `ScopedPerfTimer` at ~`:40309`).
- Audio: `AudioEngine::XrunCount()` exists, but it is a wall-clock-gap
  **heuristic** (`src/audio/AudioEngine.cpp:22-28`). There is **no audio
  callback load meter**.
- There are **no GPU timer queries** anywhere in `src/`.
- Known fixture flaws:
  - `GEOMDENSITYTEST` and `RESSWEEPTEST` render a **static** scene.
    Render 3D reuses its last image when nothing changed, so they read a flat
    ~2 ms from 540p to 4K and from 50k to 2M triangles. They measure the cache,
    not rendering.
  - Stress fixtures spawn every node at (0,0), so all nodes are on screen.
    Real patches are spread out.
  - Reports give avg/min/max only. For live work, **p95/p99 and frame pacing**
    matter more than the average.

Baseline numbers on the owner's M2 (use them to sanity-check your harness):

| Fixture | Result |
|---|---|
| NODECHAIN len=50 @480² / 960² / 1920² | 5.8 / 10.7 / 38.8 ms |
| NODECHAIN len=20 / 100 @1920² | 17.6 / 70.6 ms |
| MIXEDSTRESS scale 1 (121 nodes) / scale 3 (289 nodes) | 8.7 ms / 54.7 ms (18 fps) |

A CPU profile of MIXEDSTRESS=3 (macOS `sample`) showed 71% of main-thread
time in three UI hot spots. These are listed in §7; don't fix them here.

## 3. Metrics every benchmark must report

Emit one JSON line per run, prefixed `BENCH_JSON ` so a script can grep it:

```
BENCH_JSON {"bench":"B3_live","variant":"m","machine":"<hw.model>","gpu":"<GL_RENDERER>",
 "commit":"<git sha>","frames":600,
 "frame_ms":{"p50":..,"p95":..,"p99":..,"max":..},
 "projector_ms":{"p50":..,"p99":..,"missed_vsync":..},
 "stages_cpu_ms":{"modulation":..,"cook":..,"node_bodies":..,"editor_end":..,"imgui_render":..,"projectors":..,"swap":..},
 "stages_gpu_ms":{...},
 "audio":{"buffer":256,"sr":48000,"cb_load_p50":..,"cb_load_p99":..,"cb_load_max":..,"xruns":..},
 "mem":{"rss_mb":..,"gl_tex_mb_est":..},
 "nodes":..,"tris":..,"output_hash":"..."}
```

- **Frame pacing**: store every frame time in a ring buffer. Report
  percentiles plus a count of frames over 1.5× the target period.
- **Stage timings**: CPU timing with `steady_clock` around each main-loop
  stage. GPU timing with `GL_TIME_ELAPSED` queries, read back **N frames
  later** so nothing stalls. macOS GL 4.1 supports these; guard them on other
  platforms.
- **Audio callback load**: `(time inside the render callback) / (buffer period)`.
  Must be real-time safe: atomics only, no locks, no allocation, no printf on
  the audio thread. The main thread reads and aggregates.
- **`output_hash`**: a hash of the final Output texture (read back once, at
  the end of the run). This is the **quality guard**. Later optimisations must
  keep the hash identical at "Full" quality, or explain why not
  (e.g. float-summation order).
- **Memory**: process RSS (`task_info` on macOS; equivalents elsewhere) at
  start and end.

## 4. The suite

Run all benchmarks through one entry point, `INFINITE_BENCH=<id>[:<variant>]`,
with variants `s`/`m`/`l` for scale. Keep the existing fixture env vars working
as aliases. Lay nodes out on a realistic grid, not stacked at the origin.
Every "animated" variant binds LFOs, so caches are legitimately invalidated
every frame: that is the worst case. Every "static" variant is the best case.
Report both.

| ID | Benchmark | Fixture shape | Primary metrics |
|---|---|---|---|
| **B1** | Heavy audio | 12-32 voices mixing Sampler/Wavetable/Oscillator; per-voice chains Filter→Drive→Delay→Reverb→Dynamics; Mixer; modulated params. Sweep buffer 64/128/256/512 | cb_load p99, xruns over 60 s, main fps alongside |
| **B2** | Heavy visuals | Geometry (high-detail primitives, point clouds, deform/displace driven per frame) → Render 3D 1080p → 10-30 compositing/effect nodes (blur, blend, colour, feedback) → Output. Static and animated variants | frame p50/p99, GPU stage ms, cook vs body split |
| **B3** | Live performance | B1-lite + B2-lite + one open **projector window** + simulated MIDI notes/CC injected into the note queue + macro/gesture playback + Prediction modulators running | **projector p99**, missed vsyncs, audio xruns, and input-to-photon: frames from an injected param change until the Output texture revision changes |
| **B4** | Complex 3D scenes | Many objects, instancing, several lights, shadows, materials, HDRI, ocean; animated camera. Sweep object count and shadow quality | frame p99, GPU ms, tris, draw calls |
| **B5** | Fundamentals | (a) empty patch; (b) node-count scaling 50/100/200/400 of a mixed set, to catch O(N²); (c) per-stage CPU+GPU split; (d) audio thread alone at each buffer size; (e) startup time; (f) patch load/save time; (g) undo snapshot time vs patch size | Growth curve per stage; flag any stage growing faster than linear |
| **B6** | Canvas navigation *(missing today)* | Large patch (200-400 nodes spread out). **Programmatic** pan/zoom through the node-editor API, programmatic node drag, dropdown open. Never OS-level UI scripting | Canvas frame p50/p95 while moving; time spent on off-screen node bodies |
| **B7** | Soak and thermal *(missing)* | B3 for 30 min (a `long` variant, not part of the default run) | fps vs time curve (fanless Air throttling), RSS growth, xrun total |
| **B8** | Media I/O *(missing)* | 2-4 video clips 1080p/4K playing, camera in, 2-3 projector windows, Syphon/Spout out | Decode ms, upload ms, per-window present ms |
| **B9** | Memory footprint *(missing)* | B2/B4 at l scale | RSS, estimated texture/VBO bytes (sum of allocated sizes you can see) against the 8 GB unified memory reality |
| **B10** | Offline render and A/V sync *(missing)* | Arrangement render of B3 for 30 s | Realtime factor, A/V drift (reuse `av-sync-sweep`'s checks) |

## 5. Deliverables

1. The `INFINITE_BENCH` fixtures and instrumentation (stage timers, GPU query
   ring, audio load meter, output hash, RSS), compiled on macOS, Windows and
   Linux. Any platform-specific piece goes behind `Platform::`, per the
   parity skills.
2. `scripts/bench/run_all.sh`: runs B1-B6 and B8-B10 (not B7 unless `--soak`),
   collects `BENCH_JSON` lines into
   `bench/results/<machine>/<date>-<sha>.jsonl`.
3. `scripts/bench/compare.py <baseline.jsonl> <new.jsonl>`: prints a table and
   flags any p99 regression over 10%, any xrun increase, and any `output_hash`
   change.
4. A first baseline recorded on this machine and committed under
   `bench/baselines/m2-8gb.jsonl`.
5. `docs/plans/perf/README.md`: how to run it, what each benchmark proves, the
   baseline table, and the top five costs per benchmark (by stage and by node
   type) as found by the numbers.
6. Windows/Linux: the fixtures must build in CI. CI runners have no real GPU,
   so mark GPU-bound numbers there as *not meaningful* rather than recording
   them as baselines.

## 6. Targets to measure against (projector rule confirmed by the owner 2026-09-23; the rest proposed)

| Scope | Target on base M2 |
|---|---|
| Audio, B1/B3 | 0 xruns in 10 min at 256 frames; cb_load p99 ≤ 50% |
| Projector, B3 | Refresh-aware. Read each Output window's monitor refresh R (60/120/144/165…). Must hit: a locked 60 fps (p99 ≤ 16.7 ms), or, on 120/144 Hz, a whole divisor of R. Aim: native R when there is headroom. Never an uneven rate such as 90 on a 120 Hz panel, because it judders. Missed vsync < 0.5%. Report R, the chosen rate and the frame-interval jitter (stddev) in `BENCH_JSON`. Test at R = 60 and at R = 120 (ProMotion, and a Windows 120/144 Hz monitor via CI or a tester). |
| Canvas, B6 | p50 ≥ 60 fps, p95 ≥ 45 fps while panning a 300-node patch |
| Input-to-photon, B3 | ≤ 2 frames |
| Soak, B7 | RSS growth < 2% over 30 min; fps drop from thermals reported, not hidden |
| Quality | `output_hash` unchanged at Full quality |

## 7. Known fix targets (do NOT fix in this session; confirm each with the numbers)

Ranked by evidence from the MIXEDSTRESS=3 profile, main thread, 2,075 samples:

1. **Modulated dropdowns push undo snapshots.** About 25% of main-thread
   time. Already being fixed on `bugfix/modulated-dropdown-undo-spam`
   (`docs/fix-briefs/modulated-dropdown-undo-spam.md`). Benchmark before and
   after.
2. **Filter response curve recomputed every frame while modulated.** About
   31%. The per-node cache (`src/main.cpp` ~19966-20245) invalidates on every
   modulation step. Check the EQ (`BandMagnitudeDb` ~20566) and other in-body
   visualizers for the same shape.
3. **`GetNodeInstanceIndex` is O(N²).** About 15%. `src/main.cpp:681`: every
   node's header scans all nodes and builds title strings. This is why
   MIXEDSTRESS grows 6.3× for 2.4× the nodes.
4. **Projector coupled to the canvas frame** (~`:91474`). Needs a design:
   projector first, and when over budget, skip or throttle canvas redraw
   rather than the projector.
   **Pacing gap (found 2026-09-23):** projector contexts use
   `glfwSwapInterval(0)` (~`:47436`); only the main window waits for vsync.
   If the projector's display refresh differs from the laptop panel's
   (ProMotion 120 Hz with a 60 Hz projector, or a Windows 144 Hz laptop with a
   60 Hz projector), projector frames are timed by the wrong clock. That
   gives tearing and uneven frame intervals. B3 must record per-window
   refresh R and the frame-interval jitter for the projector. The design
   should pace the main loop to the projector's display when an Output window
   is open.
5. **Off-screen and collapsed node bodies**: are they still drawn every
   frame? B6 answers this.
6. **Revision-cache coverage**: about 25 of 84 node headers override
   `TextureRevision`, and 17 override `MeshRevision`. `REVISIONSWEEPTEST` can
   list the gaps.
7. **Performance governor**: projector ≥ canvas ≥ previews, audio and export
   untouched, "Max" turns it off. Design only after B3/B6 exist.
8. **Geometry LOD / GPU deformation**: no evidence yet. Decide after B2/B4.
9. **Audio xrun detection is heuristic.** Replace it with the callback load
   meter from §3.

## 8. Rules

- Measure only. If you find a bug, write it into `docs/plans/perf/README.md`
  under "Found while measuring"; don't fix it.
- Never UI-script the app (no AppleScript, no clicking). Drive everything from
  env-var fixtures and in-code calls.
- Audio-thread code must stay real-time safe (see `audio-pipeline-sweep` and
  `field-realtime` for the rules).
- After building, copy `build/Infinite.app` to `~/Desktop/Infinite.app`.
- Commit on the feature branch with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`. Do not merge or push; the owner decides.
- Finish by reporting the baseline table and the top costs per benchmark,
  and state plainly anything you could not measure.
