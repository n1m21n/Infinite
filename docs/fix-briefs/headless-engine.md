# Brief: Infinite as a headless engine for Claude Code

You are a fresh Claude Code session in `/Users/namansoni/infinte` (Infinite: C++/OpenGL/ImGui
AV workstation, MIT). Nothing from the conversation that produced this brief is available to
you; everything you need is below. Every file/line reference was checked on 2026-09-29 against
`main` at `ba96ab1`. Line numbers in `src/main.cpp` (96,896 lines) drift, so grep the quoted
symbol before trusting a number.

## Goal

Claude writes a patch as text, Infinite renders or opens it with one command, and Claude
checks the result without anyone touching the UI:

```
write scene.inf  ->  Infinite --frame 2.0 scene.inf out.png   (ms-scale check)
                 ->  Infinite --render scene.inf out.mp4      (full take)
                 ->  read PNG / JSON summary -> edit -> repeat
                 ->  a running Infinite reloads scene.inf live when it changes
```

**Exit criterion:** from a cold start, a session using only the skill from block 5 writes a
10-node patch (image + audio + one modulation), renders it, spots one visual problem in the
PNG, fixes it and re-renders in under 60 s total, with no window shown. The same command
passes on macOS and in the Linux Xvfb rig. Windows must compile and have its side written; a
CI run on Windows is enough proof there.

**Film exit criterion (block 7):** one Infinite node, animated only by its own modulation,
expressions and a recorded gesture, renders as a 4 s alpha PNG sequence. A synth node renders
a WAV stem from the film's events file. Both drop into a skia act frame-locked, and a second
render of the same shot is byte-identical.

## What already exists (do not rebuild it)

| Piece | Where | State |
|---|---|---|
| Text patch format | `src/core/Patch.h:1-100` (grammar comment), `Patch::Write` `src/core/Patch.cpp:203`, example `assets/examples/patch_1.inf` | Already line-based text (`infinite-patch 1`, `node … end`, `cable/geo/aud/note/mod/expr/transport`). Claude can write it directly **today**. |
| Deterministic offline render | `StartOfflineRenderSession` (`main.cpp` ~41966), `RunOfflineRenderStep`, `OutputNode::StartOfflineRender / CaptureOfflineFrame / RequestFinishOfflineRender` (`src/nodes/OutputNode.h:131-232`), `Transport::SetOfflineMode` | Fixed-step video+audio take with preroll; refuses hardware-driven sources (`FindHardwareDrivenNode`). Only reachable from the UI and the `INFINITE_OFFLINERENDER*` test fixture (`main.cpp` ~67690). |
| Hidden window | `gHeadlessTestWindow` + `Platform::SuppressAppUIForHeadlessProcess()` (`main.cpp` ~66484-66515) | macOS: no window, no Dock icon, keyed on `INFINITE_EXITAFTER` / `IMAGERESYNTH_SCREENSHOT`. Linux: `tools/linux/xvfb-harness.sh` + CI step "Virtual display Xvfb render groups" (`.github/workflows/build.yml:310`). |
| Open patch from argv | `main.cpp` ~69382: `argv[1]` ending `.inf`/`.infinite` -> `LoadPatchFrom` | Works. `LoadPatchFrom` at ~46248, `ApplyPatchData` at ~45947. |
| JSON-RPC control server | `src/core/RemoteControl.{h,cpp}`, handler `HandleRpcCommand` (`main.cpp` ~46981-47420), started ~66881 on 127.0.0.1:7777 (`INFINITE_CONTROL_PORT`) | 20 methods: `list_node_types, create_node, delete_node, connect, disconnect, get_graph, get_params, set_param, set/get_node_position, fit_view(_node), expand_node, auto_wire_inputs, save/load/new_patch, undo, redo, screenshot_node`. One op per request, drained once per frame. Disabled under `INFINITE_EXITAFTER`. Client: `scripts/node_screenshot.py` (its `TOKEN_PATH` is hard-coded to the macOS path). |
| Live graph -> JSON | `src/core/PatchJson.{h,cpp}` (`PatchJson::ToJson`) | Data -> JSON only. |
| AI skills | `src/core/AISkillContent.h`, `docs/ai-skills/{field-language,expression-globals}` | Field and expressions covered. No node/patch skill. |
| Pixel stats | `src/core/ColorStats.{h,cpp}` | Reuse for image summary. |

So the concept's "hidden window is the costly part" and "the format may be text already" are
both mostly answered: the window problem is solved on macOS and Linux, and the format is text.
**The real gaps are:**

1. No CLI entry to the offline render, no machine-readable status, no exit code.
2. No single-frame or audio-summary fast path.
3. The format is writable but **silently forgiving**: an unknown `f name` key is ignored
   (`Reader` in `Patch.cpp:~140` keeps the default), an unknown node type is skipped with a
   stderr line (`main.cpp` ~45964), a wrong slot number loads wrongly (`CableRecord` carries no
   type). An author gets no signal a typo happened. Slots and `mod`'s `dstParam` are **numeric
   indices** with no name, so an author cannot write them without a schema.
4. No machine-generated schema of node types, params, ranges and slots, so any hand-written
   skill will drift from the code.
5. No live reload of a file a running Infinite has open.
6. Determinism has never been audited outside the one fixture. Wall-clock or unseeded
   randomness appears in at least `MolderNode.cpp, FieldGraphNode.cpp, SimulationNodes.cpp,
   AudioMeterNode.cpp, AudioDisplacementNode.cpp, DrumSequencerNode.cpp, WaveTerrainNode.cpp`
   (grep `glfwGetTime|steady_clock::now|random_device|rand\(`).

## Before you start

- Branch: `feature/headless-engine` off `main` (`git-branch-workflow` skill). One commit per step,
  explicit `git add <paths>`. **No** `Co-Authored-By` / "Generated with Claude Code" lines anywhere.
- Load these skills first: `codebase-navigation`, `run-infinite-hygiene` (build + self-test
  routes), `windows-parity`, `linux-parity`, `av-sync-sweep` (you are touching offline render),
  `data-accuracy-sweep` (you are touching load), `field-integration` (patch grammar section).
- Run `python3 tools/semi-brain/4_engine/semi_brain_cli.py` (see the `semi-brain` skill) on
  "CLI headless render mode reusing StartOfflineRenderSession vs new render loop" and fold its
  answer in before block 1.
- Do **not** UI-script the ImGui canvas to verify anything. Verify with the CLI you are building
  and the self-test harness.

## Block 1 — `--render` and `--frame` (together they give CI renders)

### 1.1 Argument parsing
In `main()` (`main.cpp` ~66098), next to `--dump-movement-log` (~66428) and
`--vst3-scan-bundle` (~66443), add a small parsed struct `HeadlessJob`:

```
Infinite --render <patch.inf> <out.(mp4|mov|wav)> [--start S] [--duration S] [--fps N]
         [--size WxH] [--output <node index|name>] [--sample-rate HZ] [--no-audio] [--json <status.json>]
Infinite --frame  <patch.inf> <T seconds | T1,T2,...> <out.png | out_dir/> [--size WxH] [--output …] [--json …]
Infinite --audio-summary <patch.inf> <out.json> [--start S] [--duration S] [--wav <out.wav>]
Infinite --validate <patch.inf> [--json …]
Infinite --describe [<node type>] [--json <out.json>]     (see block 3)
Infinite --version --json
```

Rules:
- Any of these modes sets the same state `gHeadlessTestWindow` uses (hidden window, no Dock
  icon), skips `CheckAutosaveRecovery`, skips `RemoteControl::Start`, the update check
  (`INFINITE_NO_UPDATE_CHECK`), the audio device, recents, and autosave writes. Refactor the
  existing `gHeadlessTestWindow` condition into one `IsHeadlessProcess()` so there is one
  place that decides it.
- Headless mode must never open an audio device. Offline audio already runs through
  `AudioEngine::ProcessOffline`; drive it at `--sample-rate` (default 48000) instead of
  whatever `AudioEngine::SampleRate()` reports. Today `StartOfflineRenderSession` takes the
  take's rate from the running device and falls back to video-only when there is none;
  add a path that sets the offline rate explicitly with no device. This is the most delicate
  change here: read commit `9b8ab23` ("Fix offline render deadlocking against the writer's
  audio gate") before touching the gate.
- Paths are UTF-8; on Windows they go through the existing wide-path helpers (`windows-parity`).

### 1.2 Which Output
Pick the render target in this order: `--output` (index from the file or the node's name),
else the only `Output` node, else fail with `E_NO_OUTPUT` / `E_AMBIGUOUS_OUTPUT` listing the
candidates. Do not invent an Output. Size defaults to that Output's size; `--size` overrides
through the same `width/height` arguments `StartOfflineRenderSession` already takes.

### 1.3 Drive the existing session, don't write a new loop
After `LoadPatchFrom`, set `recordVideoPath`, `offlineFps`, `offlineDurationSeconds`,
`offlinePrerollFrames` on the chosen Output and call `StartOfflineRenderSession`. The existing
frame loop already calls `RunOfflineRenderStep` each frame. In headless mode:
- skip the canvas/ImGui draw and `DrawOfflineRenderProgressWindow` (nothing sees it; it costs
  frames); keep `glfwPollEvents` so the process stays responsive on macOS;
- when `PollOfflineFinalize()` clears `IsOfflineRendering()`, write the status and exit.

`--start` needs a seek before the session starts. Check what `Transport` seek does to
`state`-holding nodes (the `field-state` skill documents reset on seek) and render from 0 with
the preroll when the patch has stateful nodes and `--start` > 0, unless you find an existing
fast-forward. Say which you did in the commit message.

Output container: the recorder writes what it writes today (check `Platform::` recorder for
mp4 vs mov per OS). If the extension asks for something the platform recorder can't write,
fail with `E_UNSUPPORTED_CONTAINER` rather than silently writing another format. `.wav`
means audio only: reuse the `gArrangeWavRender` path (`main.cpp` ~1231) or `AudioRecordings::WriteWav`.

### 1.4 `--frame`
Fast path, no encoder. Run the same offline Transport (fixed step, no device) to T with the
preroll, cook the Output once, read `OutputNode::GetOutputTexture()` (an FBO texture,
`OutputNode.h:46`) with `glGetTexImage`/FBO readback, flip rows, `stbi_write_png`. Never read
the window's front buffer (that is what `screenshot_node` does and it would capture the hidden
window, not the Output). Several `T`s in one call reuse one process and one patch load: step
forward through them in ascending order. Target: < 1.5 s for a 10-node patch at 512x512 on
this Mac, including launch; record the real number.

### 1.5 Status JSON and exit codes
Always print one JSON object as the **last line on stdout** and, with `--json`, to that file:

```json
{"ok":true,"mode":"render","patch":"scene.inf","out":"out.mp4","frames":120,"fps":30,
 "size":[1280,720],"audio":{"sample_rate":48000,"frames":192000},"elapsed_ms":2310,
 "warnings":[{"code":"W_UNKNOWN_PARAM","node":3,"type":"Blur","key":"f radius2","line":41}],
 "errors":[]}
```

Exit codes: `0` ok, `2` usage, `3` patch load/validation error, `4` refused (hardware source,
same text `StartOfflineRenderSession` puts in `SetRecordStatus`), `5` render/encode failure,
`6` timeout (`--timeout S`, default 600). Warnings from load (block 3) go into `warnings`
rather than stderr only.

### 1.6 Platform sides
Every new `Platform::` function gets macOS, Windows and Linux bodies (`AGENTS.md` rule 3).
Expected: none new on macOS (reuse `SuppressAppUIForHeadlessProcess`); Windows needs the window
created hidden (`GLFW_VISIBLE false`) with no taskbar flash; Linux keeps requiring a display
(Xvfb) because GLFW needs one. Document `xvfb-run -a Infinite --render …` in the skill and make
the Linux binary print `E_NO_DISPLAY` with that hint instead of crashing when `DISPLAY` and
`WAYLAND_DISPLAY` are both unset. EGL surfaceless is out of scope (see bottom).

## Block 2 — fast audio and image checks

### 2.1 `--audio-summary`
Offline audio only (no video cook if the patch has no Output video need), write JSON:
integrated loudness (LUFS, BS.1770 K-weighting, gated), true-peak dBTP (4x oversampled),
sample peak, RMS per channel, DC offset, % clipped samples, silence ratio, stereo correlation,
onset count / estimated BPM (simple spectral-flux; label it an estimate), and a 32-band
log-spaced spectrum (mean dB per band over the range), plus per-second loudness so a fade or
drop-out is visible. Put the DSP in a new `src/core/AudioAnalysis.{h,cpp}` with no main.cpp
dependency so a unit test can call it on a synthesized sine (a 1 kHz sine at -20 dBFS must read
-20 ± 0.1 LUFS-ish and 1 kHz must be the loudest band). `--wav` also writes the samples.

### 2.2 Image summary alongside PNGs
`--frame` also emits, per frame in the status JSON: mean/min/max luma, mean RGB, % pure black,
% clipped white, alpha coverage, and a 16-bin luma histogram, via `src/core/ColorStats`. This is
what lets Claude catch "black frame", "blown out" and "nothing rendered" without reading the
image. Add `--contact-sheet <out.png>` to `--frame` with several times: one PNG grid with
timestamps, so one image read covers a whole animation.

## Block 3 — the patch as an authored language

### 3.1 `--describe`: schema generated from the code
Walk every registered type (`RegisterNodes()` / whatever `list_node_types` RPC uses), spawn
each once headless, and dump: `category`, `typeName`, display name, each param from
`VisitParams` (kind `f/i/b/c/s`, key, default), range/label/enum names where a `ParamRef`
registers them (`Modulation::RegisterParam`, `src/core/Modulation.h:60` `struct ParamRef`), the
modulatable param index each `mod`/`expr` line would use, and every input slot with its cable
kind (`cable/geo/aud/note`, from `CableFor`, `ConnectGeometrySlot`, `AudioInputSlot`, note slots)
plus output count per kind. `--describe <type>` prints one. This JSON is the source of truth for
the skill (block 5) and for the validator; nothing is hand-listed. Spawning every node headless
is also a free smoke test: any type that crashes or leaks GL state here is a real bug, file it.

### 3.2 Names instead of numbers (backward compatible)
Keep format version `1` readable forever. Extend the reader, not the meaning:
- `cable`/`geo`/`aud`/`note` accept a slot **name** in place of the number
  (`cable 3 input 1` and `cable 3 1 1` both work). Names come from 3.1's slot table.
- `mod` and `expr` accept a param **key** in place of `dstParam` (`mod 5 radius 2 0 1 1 0.5`).
- Node references by word: support an `id <word>` line inside a node block and let
  cable/mod/expr lines use that bare word wherever an index goes (no sigils, per `AGENTS.md`
  rule 2; a word may not parse as an integer). Indices are already arbitrary
  (`ApplyPatchData` remaps them), so this changes nothing for saved files.
- The writer keeps writing numbers (so every existing test, diff and undo snapshot is
  unchanged). Add `Infinite --canonicalize in.inf out.inf` that loads and rewrites, so an
  authored file can be normalised.
- Allow `#` comment lines and blank lines (check the reader already skips unknown tags; make
  comments explicit).
- Params not written keep their defaults (already true), so an authored patch only lists what
  it changes. Document that; it is what makes patches short.

### 3.3 Validator
`--validate` and every headless load run a strict pass over `Patch::Data` before
`ApplyPatchData`, producing `{code, line, node, message, hint}`:
- `E_UNKNOWN_TYPE` with the 3 nearest type names (edit distance);
- `W_UNKNOWN_PARAM` with the nearest key for that type; `W_TYPE_MISMATCH` (`f` key the node
  reads as `i`);
- `W_OUT_OF_RANGE` using 3.1's ranges (clamp is the node's business, warn only);
- `E_BAD_SLOT` / `E_KIND_MISMATCH` (an `aud` line into an image slot) using the same
  connection rules as `QueryNewLink` / `docs/reference/connection-rules.md` - call the real rule
  function, do not reimplement it; `E_CYCLE` where the graph forbids one;
- `E_DANGLING` index, `W_DUPLICATE_UID`, `W_UNUSED_NODE` (not reaching any Output or the
  audio master), `E_NO_OUTPUT` for render modes;
- `E_HARDWARE_SOURCE` for render modes, reusing `FindHardwareDrivenNode`'s list;
- a Field/Expression compile error in an `s` param surfaces with the compiler's own message and
  line (the compilers already keep last-working; surface the error text).
The interactive app keeps loading leniently; it only additionally logs the warnings.
Record the reader line number on each record while parsing (Patch.cpp reader) so messages can
cite `scene.inf:41`.

### 3.4 Format doc
Write `docs/reference/patch-format.md`: the grammar (lift and complete the comment at the top of
`Patch.h`), the name-based forms, defaults rule, one minimal image patch, one minimal audio patch,
one with modulation, and the stability promise (version 1 lines never change meaning; new fields
only trail). Point `Patch.h`'s comment at it instead of duplicating.

## Block 4 — live integration with a running Infinite

This is what makes "write a file, open it in Infinite" instant while the owner watches.
- **Watch the open patch file.** When the file currently open changes on disk and the canvas
  has no unsaved edits since the last load/save, reload it (`LoadPatchFrom`) preserving the view
  (pan/zoom) and transport position; if there are unsaved edits, show a small "file changed on
  disk - Reload" banner instead of clobbering. Poll mtime+size once per second on the main
  thread (no watcher thread, no new `Platform::` call needed; if you do add one, three sides).
  Treat reload as one undo checkpoint so Cmd+Z returns to the previous graph.
- **`infinite open <patch>` from a shell while the app runs** already goes through the OS open
  handler; confirm it reuses the running instance on all three platforms and note the result.
- **RPC:** add `batch` (array of `{method, params}` executed in one frame, all-or-nothing using
  one undo checkpoint), `load_patch_text` (patch source in the request, no temp file),
  `validate_patch_text`, `render_frame` (same code as `--frame`, against the live graph, into a
  path), and `describe`. Make `scripts/node_screenshot.py`'s token path cross-platform via the
  same directory `AppPaths::AppSupportDir()` returns. Keep loopback-only and the token check.
- Do this block after 1-3. If time is short, file watch alone gives most of the value.

## Block 5 — determinism audit and CI renders

- Add `--determinism-check <patch.inf>`: render N frames twice in one process (fresh load each
  time) and once in a second process, compare RGBA bytes and audio samples exactly; report the
  first differing frame and which node's output first differs (cook each node's texture/audio and
  hash in topological order). Offline Transport time must be the only clock. Walk the 7 files
  above plus any hit for `glfwGetTime|ImGui::GetTime|steady_clock|system_clock|random_device|rand(`
  in `src/nodes`, `src/audio`, `src/core`: replace wall-clock with `Transport` time and seed
  randomness from (node uid, `seed` param). A node that genuinely can't be deterministic (live
  input, plugins with internal clocks, GPU float differences across vendors) goes on a documented
  list and makes the check report `nondeterministic_by_design` instead of failing.
- Third-party VST3/AU plugins: allowed in `--render`, but mark the status `deterministic:false`.
- Reference patches: `tests/render/*.inf` (start with 5: 2D compositing, 3D render, Field
  Pixel, audio synth + effect, modulation). A script `tools/render-check.sh` renders each with
  `--frame` at fixed times and `--audio-summary`, and compares to `tests/render/golden/` with a
  tolerance per platform (exact on the same machine; perceptual tolerance across GPUs; llvmpipe
  goldens separate). Wire into the existing Linux Xvfb CI job next to the render groups and a
  macOS job; Windows compile + one render.

## Block 6 — the Claude skill

Create `.claude/skills/infinite-patch-authoring/SKILL.md` (and the `.agents/skills` symlink),
add it to the `AGENTS.md` catalog under Process, and a copy under `docs/ai-skills/` with an
"Install AI Skill" constant in `AISkillContent.h` next to the Field one (keep them in sync the
way that header's comment says). Contents:
- The loop (validate -> frame -> summary -> render), exact commands, exit codes, JSON fields.
- How to get node facts: **always** `Infinite --describe <type> --json`, never memory. The skill
  does not list params; it may list the ~20 most useful types by task (image, 3D, audio, mod) with
  one line each, generated from `--describe` by a script so it can be regenerated.
- The format essentials, defaults rule, name-based slots, and the rules that bite: bypass rule
  (2+ input nodes never bypass), no sigils, the Field rules (link to `field-language`).
- 3 verified example patches committed under `assets/examples/authoring/`, each rendered by
  CI in block 5.
- Troubleshooting table: black frame, silent audio, `W_UNUSED_NODE`, refused hardware source,
  `E_NO_DISPLAY` on Linux.

## Block 7 — Infinite as the film engine's visual and sound backend

Context: the owner's code-rendered films (global `motion-film` skill; current project
`~/infinite-launch-video`) are built in skia-python. That engine already consumes external
media: `lib/fx.py:274` is a sequential ffmpeg reader that returns a `skia.Image` at clip time t
for any video or still, and `assemble.py` mixes WAV stems with `soundfile`. `sfx.py` turns an
events file (`[{ "t", "type", "dur", "intensity", "x" }, …]`) into a synthesized SFX stem.
The goal of this block: one Infinite node's picture or sound becomes a layer or stem in a film,
frame-locked, with no UI.

**Animation stays inside Infinite.** The film does not push per-frame parameter curves. A shot
animates with Infinite's own modulation (modulator nodes and `mod` bindings), expressions
(`expr` lines, `glob` globals) and gesture recordings (`gesture` lines), all saved in the
patch. The film's only jobs are choosing the shot's time range and fps, and optionally
per-shot static overrides. So everything that animates must run on offline Transport time.

### 7.1 `--node <id>`: tap any node, no Output needed
`--render/--frame --node <index|id>` renders that node's image output (`INode`'s output
texture, or output N with `--node 5:1` for multi-output nodes like Field Pixel's aux) or, for
`.wav`, that node's audio output (its `AudioCable` source, output slot N), instead of an Output
node's. Implement it with a hidden, internal Output-equivalent that isn't added to `gNodes`, isn't
saved, and doesn't show up in the graph: reuse `OutputNode`'s offline capture path by
constructing a detached `OutputNode` whose input cable points at the tapped node, if its lifetime
and GL context allow it; otherwise factor `CaptureOfflineFrame`'s readback into a helper both can
call. Audio tap: render that node's output alone (its own upstream chain, not the master mix), so
a synth node gives a clean stem. `--stems <id>,<id>,…` writes one WAV per node in the same pass,
all the same length and sample-aligned (e.g. `hero.wav`, `bass.wav`), for `assemble.py` to mix.
Size for a tapped image node defaults to that node's own resolution; `--size` rescales with the
same filter the Output uses.

### 7.2 Alpha PNG sequence (priority in this block)
`--frames-dir <dir> [--alpha] [--start S --duration S --fps N]` writes `dir/000000.png …` plus
`dir/frames.json` (`fps`, `size`, `count`, `start`, `alpha`, `premultiplied:false`, `color_space`).
Requirements, each verified, not assumed:
- **Keep the alpha channel end to end.** Check what `OutputNode` does with alpha today: the
  readback code near `OutputNode.h:388` mentions a BGRA fast path. Check whether Output composites
  over black or a background colour before capture, and whether the FBO is RGBA8 or RGB. With
  `--alpha`, capture before any background fill. With `--node`, capture the node's own texture.
- **Write straight (unpremultiplied) alpha.** Infinite blends source-over (see
  `compositing-pipeline-sweep` for the pipeline's alpha convention). Determine whether textures
  hold premultiplied colour. If they do, unpremultiply on write (guarding alpha 0), because
  PNG and skia's default `kUnpremul` decode expect straight alpha. Test with a 50%-alpha red
  shape: the PNG must read (255,0,0,128), not (128,0,0,128). A dark fringe on edges is the
  symptom of getting this wrong.
- Colour: sRGB 8-bit by default. `--depth 16` writes 16-bit PNG if the source FBO is float,
  otherwise it refuses with a message instead of padding.
- PNG encoding is the bottleneck at 1080p. Encode on a small worker pool (bounded queue, the
  same back-pressure idea as the offline encoder's queue-bytes budget) so readback never waits
  on zlib. Use a fast compression level by default and `--png-level` to override it.
- Also offer `--render out.mov --codec prores4444` (alpha-carrying) where the platform
  recorder supports it (macOS AVFoundation). Elsewhere, refuse with `E_UNSUPPORTED_CODEC`
  and point to `--frames-dir`.
- Film side: `fx.py`'s reader already handles image sequences through ffmpeg
  (`-framerate N -i dir/%06d.png`). Confirm it keeps alpha (it needs `-pix_fmt rgba`, which
  depends on its `-vf`/`-pix_fmt` args at `fx.py:297`) and fix the reader in that repo if it
  doesn't. That's a one-line change there, noted rather than done from this repo.

### 7.3 Offline-clock correctness for Infinite's own animation
Every time-based animation source must advance on offline Transport time during a headless
render, exactly once per rendered frame:
- **Expressions: already correct.** `ExprGlobals::EvaluateAll(t)` is fed
  `Transport::Instance().Seconds()` (`main.cpp` ~65063-65071). Add a test anyway.
- **Gesture recordings: confirmed bug.** `GestureRecorder::AdvanceClock` is called with
  `ImGui::GetIO().DeltaTime` (`main.cpp` ~71641). That's wall-clock frame time, so during an
  offline render (where one frame can take 50 ms or 500 ms of real time) a recorded knob move
  plays at the wrong speed and differs run to run. Fix: while `gOfflineRender.active` (and in
  every headless mode), advance by exactly `1.0 / offlineFps`, or better, derive the playback
  clock from Transport seconds so a `--start` offset lands in the right place in the loop. Keep
  the record clock on wall time (you can't record in headless mode anyway). This affects the
  interactive offline render in the UI too, so it's a fix for existing users, and gets its own commit.
- **Modulator nodes** (LFO, Random, Pattern, Envelope, Prediction family — `new-modulator-node`
  skill, `ModulatorNodes.h`): audit each one's time source. Any that reads `glfwGetTime`,
  `ImGui::GetTime` or frame delta instead of Transport is the same bug. Random-type modulators
  must be seeded (block 5), so a re-render of the same shot is identical.
- **Tempo sync**: `transport <bpm> …` in the patch sets BPM. Add `--bpm` to override it per
  shot, so beat-synced modulation lines up with the film's cut grid (the film quantises SFX to
  its own grid in `sfx.py`'s `quantise`).
- Test: a patch with one LFO, one expression, and one recorded gesture on three params, rendered
  twice at 30 fps and once at 60 fps. At matching timestamps the param values (dump them
  with `--trace-params <file.json>`: node, param, value per frame) must match to 1e-6, and the
  PNGs must be byte-identical between the two 30 fps runs.

### 7.4 Per-shot overrides (static, optional)
`--set <node>.<param>=<value>` (repeatable; node by index or `id` word, param by key from
`--describe`) is applied after load, before preroll. It's for static per-shot choices like a
palette or a seed, not animation. It goes through the same `Reader`/`VisitParams` path as a
file load, so it's validated by the block 3 validator. `--set` on a param that has a `mod`,
`expr` or `gesture` binding warns `W_OVERRIDDEN_BY_MODULATION`, because the binding wins every frame.

### 7.5 Film events → notes (Infinite sound design locked to picture)
`--notes <events.json>` with `--note-map <map.json>` turns the film's events into note-ons for
a note-input node, so an Infinite synth designs a hit that lands exactly on the picture event.
- Map file: `{ "<event type>": { "node": "<id>", "pitch": 60, "velocity_from": "intensity",
  "length_from": "dur", "pan_from": "x" } }`. Unmapped types are ignored and listed in the status.
  `pan_from` only applies if the target has a pan param, set per note through the existing
  note-expression path if one exists (check `NoteModel`, `src/audio/NoteModel.h`). Otherwise
  warn instead of faking it.
- Inject through the same note path live MIDI uses, scheduled at sample-accurate offsets
  inside the offline audio block (`AudioEngine::ProcessOffline`), not quantised to video
  frames. Check `audio-pipeline-sweep` for how note fan-out is timestamped and use that.
  `--notes` must not trip `FindHardwareDrivenNode`'s MIDI refusal: the notes come from a file,
  so the take is deterministic.
- Output: one WAV per target node via 7.1's `--stems`, sample-aligned with t=0 = film t=0.
  `assemble.py` then mixes Infinite stems next to (or instead of) `sfx.py`'s synthesized ones.
- Test: three events at t = 0.5, 1.0, 1.5 into a short-envelope synth. The stem's onsets
  (from 2.1's onset detector) land within ±1 ms of those times.

### 7.6 Film engine hook (in the film repo, noted only)
Add to the `motion-film` skill's docs, not this repo: a scene can declare
`{"infinite": {"patch": "shots/hero.inf", "node": "hero", "range": [2.0, 6.0], "alpha": true}}`,
and `render.py` calls `Infinite --frames-dir …` once, caches by the patch file's hash plus its
args, and feeds the directory to `fx.py`'s reader. The same goes for `--stems` into
`assemble.py`. Write this up as a short section in this brief's final report for the owner to
apply, and don't edit `~/infinite-launch-video` from this session.

## Order and commits

| Step | Delivers | Gate before moving on |
|---|---|---|
| 1 | Block 1 (`--render`, `--frame`, status, exit codes) macOS | `--render` of `assets/examples/patch_1.inf` produces a playable file with audio; `--frame` PNG matches what the Output shows |
| 2 | Block 3.1 + 3.3 (`--describe`, validator) | every registered type spawns headless without crash; a typo'd param yields `W_UNKNOWN_PARAM` with a suggestion |
| 3 | Block 2 | sine test passes; black patch reports 100 % black |
| 4 | Block 3.2 + 3.4 | an authored patch using names loads identically to its canonicalized numeric form (compare `PatchJson::ToJson`) |
| 5 | Block 1.6 Linux/Windows + Block 5 | Linux CI renders goldens; Windows builds |
| 6 | Block 6 skill + exit-criterion run | the 60 s run, timed, written into the commit message |
| 7 | Block 7.3 gesture-clock fix (own commit, can land any time after step 1) | the 30/30/60 fps param-trace test passes |
| 8 | Block 7.1 + 7.2 (`--node`, alpha PNG sequence) | a 50%-alpha red shape reads (255,0,0,128); a tapped node renders without an Output in the patch |
| 9 | Block 7.4 + 7.5 (`--set`, `--notes` → stems) | onset test within ±1 ms; stems sample-aligned |
| 10 | Block 4 | edit file on disk -> running app reloads, undo restores |

## Verification each step

```
cmake --build build -j"$(sysctl -n hw.ncpu)"
```
Must compile clean. Then run the `run-infinite-hygiene` fast tier plus `ROUNDTRIPTEST`,
`INFINITE_PATCHTEST` and `INFINITE_OFFLINERENDER` / `INFINITE_OFFLINERENDERREFUSETEST` fixtures
(they cover the code you are reusing), and `av-sync-sweep` after step 1 and 5. Add a self-test
that runs the CLI itself end to end (spawn `build/Infinite.app/Contents/MacOS/Infinite --frame …`
from a script under `scripts/`, assert JSON and PNG). After the final build copy
`build/Infinite.app` to `~/Desktop/Infinite.app`.

## Out of scope (deliberately)

- True GPU-less rendering (EGL surfaceless / OSMesa / Metal headless without a window). The
  hidden-window route already works on macOS and Linux-with-Xvfb; revisit only if CI cost demands it.
- A new patch format (JSON/YAML). The text format is fine; making it strict and name-aware is the
  work. `PatchJson` stays read-only.
- Rendering arrangement/timeline ranges from the CLI beyond what `--start/--duration` gives;
  the arrange render queue (`main.cpp` ~43336) can be exposed later with `--arrange`.
- An MCP server in this repo. The RPC is enough; an MCP wrapper can sit on top later.
- Any change to what the interactive app does on load, other than extra logged warnings and the
  file-changed banner.

## Open decisions (recommendation given, flag if you disagree)

1. **Node references in authored files**: `id <word>` line (recommended) vs positional indices only.
2. **`--start` with stateful nodes**: render from 0 and discard (recommended, correct) vs seek (fast, wrong for feedback/sims).
3. **Golden tolerance across GPUs**: per-platform goldens (recommended) vs one golden + perceptual diff.
