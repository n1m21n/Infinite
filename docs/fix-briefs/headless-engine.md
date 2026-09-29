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
- **do NOT simply skip the canvas draw.** Modulation, expressions, palette, performance-panel
  values and gesture playback are only applied to params that **registered this frame while
  their node body drew** (`main.cpp` ~95787: "the parameter registry is rebuilt every frame while
  nodes draw"; `Modulation::RegisterParam`, `FrameParams()`). Skip the draw and every `mod`,
  `expr` and `gesture` line silently stops working, so the render comes out static. The app
  already has a no-draw mechanism: `gParamRegisterOnly` (`main.cpp` ~2691, used by collapsed
  nodes and the hidden-band pattern at ~21643), which runs the body's widget calls to register
  params without drawing. In headless mode, run every node's body in register-only mode each
  frame. Don't cull by `IsParamDriven()`, because a headless run has no viewport to cull
  against and the cost is small next to cooking. Skip only the ImGui render/submit and
  `DrawOfflineRenderProgressWindow`. Keep `glfwPollEvents`. Test: a patch whose only motion is
  one LFO → param renders frames that differ from each other; the same patch with the draw
  fully skipped must fail that test (proving the test catches the bug);
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

### 3.1b Every UI control, one row per control (found after step 2)

Measured on the step-2 `--describe` output (297 types): 5,402 saved params (3,820 float,
724 int, 399 bool, 365 colour, 94 text) and 1,789 modulatable controls. **The two lists aren't
joined**, so an author can't get from what's on screen to what goes in the file:
- **1,063 of 1,789 modulatable labels aren't a saved key.** The UI says `size x` / `pos x` / `shape`,
  but the file needs `sizeX` / `posX` / `shapeType`. `ParamRef` has no key field, so a `mod` or
  `expr` line can't be written by key, and `--explain` can't name what a binding drives.
- **Dropdowns carry no option names.** Shape's `shape` is `isEnum` with `enum: []`; the file is
  `i shapeType 0..19`. Nobody can tell which number is "star". The same goes for the 724 int
  params (Oscillator `waveform`, `filterType`, `fmMode`, …).
- **Ranges exist only for modulatable controls.** About 3,600 saved params have no min/max, so
  `W_OUT_OF_RANGE` can't fire for them. Some of those are internal state (not a UI control at
  all) that an author shouldn't touch, and nothing marks which.
- **Buttons are actions, not state** (open editor, load file, randomize, reset, record, learn),
  so they have no file form. That's fine, but nothing tells an author which state a button
  produces or whether that state is saved at all.

Fix, without touching the ~158 `Draw*Params` functions one by one:
1. **Join by address.** During the register-only pass, run a `ParamVisitor` that records
   `&value` for every `VisitParams` key, then match each registered `ParamRef.value` pointer
   (valid within that frame) to its key. Add `std::string key` to `ParamRef` (filled by this
   pass, and in `Modulation::mKnownParams`). A `ParamRef` whose pointer matches no key is an
   **unsaved control**, a real save/load bug (list them; `ROUNDTRIPTEST` should have caught it).
   A key that no widget ever registers is either a plain control or internal state.
2. **Dropdown options at registration.** The combo helpers that set `ref.isEnum`
   (`main.cpp` ~3306) already have the item strings. Copy them into `enumOptions` there, once
   per helper, not per node. Then allow `i shapeType Star` in the file (the reader maps the name to
   the index via `--describe`; the writer still writes numbers), and have `--explain` print names.
3. **Plain controls get ranges too.** The ~18 non-modulatable widgets (`docs/node_param_audit.md`)
   register a range-only record in the same pass (a flag on `ParamRef`, never shown as a pin).
4. **`--describe` becomes one list per node**, with one row per saved key:
   `key, kind, default, label, min, max, step, options[], modulatable_index|null, ui:true|false`.
   `ui:false` = internal state: the validator warns `W_INTERNAL_PARAM` if a hand-written patch
   sets one, and the skill says not to.
5. **Actions list.** `--describe` adds `actions: [{label, effect}]` for each button, taken from
   the node-help text where it exists (`project_node_help_coverage`). The effect is one of:
   "sets `path`" (write the key instead), "randomizes params" (use `--set` or a seed), or
   "**UI-only**". UI-only covers learned/trained state (Prediction family), live recording
   into a sampler, and plugin editor state. For each of these, check whether the result is
   saved in the patch; if it's saved, a patch made in the UI carries it, and if not, it's a
   save/load gap to report.
6. Test: for 10 node types spanning categories (Shape, Oscillator, Wavetable, Reverb, FieldPixel,
   LFO, Render 3D, Sampler, Mixer, Predictive LFO), every on-screen control appears in
   `--describe` with its key and label, every dropdown shows its option names, and
   `i <key> <OptionName>` round-trips to the same index.

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

**Strict by default outside the UI.** Every CLI mode (`--render`, `--frame`, `--frames-dir`,
`--stems`, `--audio-summary`, `--validate`) and the RPC's `load_patch_text` run in strict mode:
any warning is promoted to an error, nothing renders, exit code `3`, and the status JSON lists
every problem at once (don't stop at the first one, so one fix round clears them all).
`--lenient` opts back out for old or hand-merged patches. Double-clicking a file and the
Finder/Explorer open path stay lenient, so no existing user patch stops opening. Test: a patch
with one misspelled param renders under `--lenient`, refuses under the default, and the error
names the line and the nearest valid key.

### 3.3b `--explain`: read back what actually loaded
`Infinite --explain my.inf [--json]` loads the patch (strict), then prints the **live** graph,
not the file. So it reflects what `ApplyPatchData` actually built after remapping, clamping
and defaults, generated from the same state `PatchJson::ToJson` reads:

```
Output (3) <- image: Field Pixel "hero" (2) out 0
Field Pixel "hero" (2)   size 1920x1080   kernel: 14 lines, compiled OK
  radius 0.40   <- LFO "lfo1" (1) out 0, bipolar, depth 0.50, range 0.20..0.60
  hue    0.10   <- expr: sin(t*2)*0.5+0.5
LFO "lfo1" (1)   rate 1/4 (synced, 120 BPM)   shape sine
Audio master <- Reverb (5) <- Oscillator (4) <- notes: Random Note Generator (6)
Unconnected: none   Unused outputs: none
```

Params are shown by key with their resolved value. Anything left at its default is marked
`(default)` in `--json` and hidden in text unless you pass `--all`. Modulation shows the resolved
range (`Modulation::ResolvedSourceFor`), not just the raw record. The skill's loop requires
comparing this against the intent before rendering. It's the check that catches a patch that's
valid but wrong, like modulating the wrong param or wiring the wrong input. It's also exposed as
an `explain` RPC method against the running app.

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
- The loop, as a checklist the AI may not skip: describe -> write -> validate (strict) ->
  **explain and compare to intent** -> frame + summary -> fix -> render. Exact commands, exit
  codes, JSON fields.
- How to get node facts: **always** `Infinite --describe <type> --json`, never memory.

**Generated, never hand-written.** Everything in the skill that states a fact about nodes
(type list by task, one-line descriptions, common params and slots, example snippets) is
produced by `tools/gen-patch-skill.py` from `Infinite --describe --json`, between
`<!-- generated:begin -->` / `<!-- generated:end -->` markers. Only the loop, the rules and
the troubleshooting prose are hand-written. The same script regenerates the
`docs/ai-skills/` copy and the `AISkillContent.h` constant, which gives that header the single
source of truth its comment says is missing (extend the script to cover the Field and
expression skills too if their content can be derived the same way; otherwise leave them). CI
job in block 5: run the generator and fail if the output differs from what's committed
("skill is stale, run tools/gen-patch-skill.py"). Also render every example patch with
`--strict` and fail on any error. A node change that isn't reflected in the skill can't merge.
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

## Block 8 — Node census: find edge cases by machine, not by memory

Hand-listing edge cases does not scale to ~300 types (80 of them 3D, 36 Compositing, plus the
integrated audio↔visual nodes). `scripts/node_census.py` spawns **every** registered type alone,
feeds each input slot a canonical source of its kind (image: Formula, geometry: Cube, audio:
Oscillator, notes: Random Note Generator), routes the first output somewhere observable
(Output / Render 3D → Output / the Output's audio pin / a Shape's `pos x`), and checks
validate, render, black, NaN, size changes, determinism, render-vs-frame clock gap, and audio
silence/clipping. The full run takes 4.5 min on 4 jobs.

**First run (2026-09-29, 297 types): 259 clean. No crashes, hangs or NaNs, and every type is
deterministic.** What it found:

| Finding | Types | St | Action |
|---|---|---|---|
| **`--render` says `ok`, exit 0, "rendered 60 frames", but the `.mov` is unreadable (`moov atom not found`)**: 50 of 297 rows at `--jobs 4`; always with 3+ parallel renders | any | C | E14 below (finalize wait returns before `finishWriting` completes). Serious: the AI and the film engine both trust `ok`. The baseline is recorded with this bug present, so fixing it shows as 50 improved rows. |
| Render 3D's `env` slot is reported as kind `image`, but only an HDRI plugs in (`E_KIND_MISMATCH` for Formula) | Render 3D | C | `--describe` must report the real accepted source (`kind: environment`, `accepts: [HDRI]`). Generalise: the schema's kind comes from `CapsAcceptedBy`, not from `CableFor`. |
| Default params clip a 0.85-peak input | Formant Filter 5.4, Resonator Bank 2.4, Wavetable Shaper 1.9, Drive 1.6, EQ 1.6, Flanger 1.4, Analog 1.4, Frequency Shifter 1.3, Blend Audio 1.2, Stereo 1.2, Wave Terrain 1.1, Phaser 1.1, Metallic 1.0 (Mixer 6.8 is 8 summed inputs, expected) | C | Hand to `param-truth-audit` (default gain staging). The skill tells the AI to follow these with a limiter or lower gain. |
| Silent with no asset | Audio File, Sampler, Slicer, PaulStretch, Molder, Grain Molder, Granular | C (expected) | Census gains `tests/headless/fixtures/` (a 2 s WAV, a PNG, a 2 s MP4, an OBJ) and sets `path` for asset nodes. Then silence is a real failure. |
| Silent without a pattern or performer | Keyboard, Drum Sequencer, Predictive Notes | C (expected) | Per-type seed params in a small `census_overrides.json`. |
| Hardware sources refuse with rc 4 | Video In, Syphon In, Audio In, MIDI CC/Trigger/Notes | C (correct) | Keep as an expected row. |
| Harness gaps | Output, Audio Out, Comment, Group, Field Graph, OSC Send, Draw | — | Special-case in the census; not engine bugs. |
| Render-vs-frame clock gap | HDRI 3.7, addnoise 3.4 (addnoise reads `Transport::Seconds`, so this is H.264 noise on grain) | L | The clock check is only trustworthy on lossless frames. Switch it to the PNG-sequence path (7.2) once that lands. It missed Audio Displacement (D8) for this reason. |

**How it stays useful:**
- `--baseline build/census/census.json` prints only rows that changed and exits 1, so a new
  node or a regression shows up in one line. Commit a baseline under `tests/headless/census_baseline.json`.
- Run it in `verify-gate` whenever a diff touches `src/nodes/` or node registration, and in CI on Linux.
- Next layers, in this order (each is a loop over the schema, not a hand list):
  1. **Param sweep**: every modulatable at min/mid/max → crash, NaN, black, silent, or
     "no visible or audible change" (a dead control; feeds `param-truth-audit`).
  2. **Enum sweep**: every dropdown option once (needs 3.1b's option names).
  3. **Pair sweep**: for each output kind × input slot the validator accepts, one real render;
     validator-says-ok but engine-refuses = a schema bug (like the env slot).
  4. **Bypass sweep**: every bypassable type bypassed → output equals its input (G3/G4).
  5. **Pacing sweep**: lossless render-vs-frame, catches every wall-clock node (D8 class).
- Every finding becomes a row in the generated skill (block 6), so the AI learns it once.

## Edge cases (every one needs a test or an explicit "won't fix" note)

**Status after steps 1–2 (commits `5c286db`, `35f8306`; checked against `build/` on 2026-09-29):**
- ✅ E1: `HeadlessTick` keeps the hidden window drawing, and the 3 `--frame` times of
  `tests/headless/patch_video.inf` (21 `mod` lines) differ from each other and match single-time runs.
- ✅ E2: `--describe` gives 297 types, identical across two runs. Per-type runs match the full dump
  (Reverb 7, Oscillator 22 modulatable). The 19 types without modulatables are I/O and utility nodes.
- ❌ E9 for `--render`: 3+ parallel renders all report `ok` and all write broken files (E14). `--frame` is fine.
- ✅ E9: `--frame` leaves `~/Library/Application Support/Infinite` untouched. 3 parallel runs
  give byte-identical PNGs.
- ✅ E10 for the chosen Output (`sOut->recordVideoPath = job.out`). Still open: a second
  Output, or `exportImagePath`, in the patch.
- ⚠️ A1/A3: CRLF and BOM files are now **refused** by strict mode (loud, good), but the message
  reads `unknown node type 'Random Note Generator\r'`, and the GUI still loads CRLF silently
  wrong. Fix the reader (strip `\r`, skip the BOM), which fixes both.
- ⚠️ Type names are internal (`FieldPixel`), while the UI shows "Field Pixel". The hint catches it;
  accepting display names as aliases (and saying which one `--describe` prints) removes the trap.
- ⚠️ Controls not joined to saved keys, dropdowns have no option names: see 3.1b.
- ❌ Graph topology: the validator accepts every broken case tested (G1–G15 below).
- Not yet checked: A2, A5–A7, A9, E3–E8, E11–E13, G8, L*, P*.

Status: **C** = confirmed in code on 2026-09-29, **L** = likely, verify first.

### The engine itself
| # | Case | St | Required behaviour |
|---|---|---|---|
| E1 | Param registry only fills while node bodies draw (see 1.3) | C | Register-only pass for every node, every headless frame |
| E2 | `--describe` ranges/indices come from the same registry | C | `--describe` runs one register-only pass per spawned node before reading `KnownParam` |
| E3 | `mod`/`expr` `dstParam` is the **order** widgets register in. A node whose visible widgets change with a mode or dropdown could shift indices | L | `--describe` reports indices per mode where they differ. The validator warns when a binding's index resolves to a different key than the one `--explain` shows. Nodes that register hidden widgets (the band pattern at `main.cpp` ~21643) are the model fix. List the offenders you find instead of fixing them all. |
| E4 | Assets load asynchronously (video decode, `AudioDecodeCache`, model import, `RemoveBgNode`/`SlicerNode`/`MolderNode` threads), so early frames render black or silent | L | A "graph ready" barrier before frame 0: poll every node for pending loads (add `INode::IsLoading()`, default false) with a timeout. The 2-frame preroll is not enough. |
| E5 | Field kernel fails to compile: keep-last-working means the first compile leaves nothing, so the frame is black | C (by design) | Strict mode: compile error = `E_FIELD_COMPILE` with the compiler's line and column. Also compile with the target GLSL version on macOS (GLSL 330 on Windows/Linux per `windows-parity`), so "works on Mac, black on Linux" is caught on the Mac. |
| E6 | Output size odd (H.264 needs even), zero, or bigger than `GL_MAX_TEXTURE_SIZE` | L | Validate up front with a clear error. Round odd sizes only if `--allow-round`. |
| E7 | Mac locked or on battery overnight: App Nap and occlusion throttling stall a hidden window (the "B8 locked-screen trap" in `run-infinite-hygiene`) | L | Headless mode holds an activity assertion (`NSProcessInfo beginActivityWithOptions` on macOS, `SetThreadExecutionState` on Windows). Test a render with the screen locked. |
| E8 | Disk full or process killed mid-render leaves a partial file that looks valid | L | Write to `out.partial.<ext>` and rename on success. On failure, delete it and exit `5`. The same goes for PNG sequences (write to `dir.partial/`, then rename). |
| E9 | Two renders at once, or a render while the GUI app runs, collide on shared state: `Infinite.autosave.inf`, recents, prefs, `control_token`, the plugin cache, fixed `TmpPath` names | C (shared files exist) | Headless writes none of these. Any temp file it needs gets the PID in its name. Test: 4 renders in parallel plus the GUI open, and every output is correct. |
| E10 | A patch carries `s recordVideoPath` (`OutputNode.h:252`) or other output-path params | C | Headless ignores every output path stored in the patch and writes only where the CLI says. A downloaded patch must never choose where files get written. |
| E11 | Plugins: licence/registration dialogs, editors that open windows, AU needing the main run loop, very slow scans | L | Headless never opens a plugin editor. It uses the existing plugin cache and never rescans. A plugin that shows a modal dialog hits `--timeout` with a message naming it. `--no-plugins` bypasses them (with a warning). |
| E12 | Patch uses arrangement clips/timeline: does `--render` from Transport 0 play them? | L | Decide explicitly (recommended: yes, the same as pressing play). `--explain` lists clips. |
| E13 | Sample-rate-dependent nodes prepared at the default rate before `--sample-rate` is applied | L | Set the offline rate before `RebuildAudioTopology` and before any node prepares |
| E14 | **`--render` reports `ok`, exit 0, but the `.mov` has no index (`moov atom not found`)**. Always with 3+ renders in parallel (2 are fine, serial is fine), and also once serially with the Output size modulated mid-render | C | Root cause (very likely): `Platform::RecorderStop` waits for `finishWritingWithCompletionHandler` by spinning `NSRunLoop runUntilDate` (`Platform.mm:3087-3091`) on the background finalize thread (`OutputNode.cpp` ~517). That thread's run loop has no sources, so `runUntilDate` returns immediately, the 2000 × 5 ms wait takes about 0 ms, and `done == false` is never checked (only `Failed` is). The GUI survives because the process lives on; headless exits first. Fix: wait on a `dispatch_semaphore` signalled by the completion handler (60 s timeout, same as the worker wait above it), and return an error when it times out or when `writer.status != Completed`. Then, separately, lock the encoder size at frame 0 for a size-changing Output (`W_SIZE_CHANGED`). Headless verifies the finished file opens before it reports `ok` (exit 5 otherwise). Test: census with `--jobs 4` shows zero `render-ok-but-file-unreadable`. This also affects GUI exports on quit and the film engine's parallel shots. |

### Authoring the file
| # | Case | St | Required behaviour |
|---|---|---|---|
| A1 | **CRLF line endings** (a Windows editor, some AI tools): `std::getline` keeps `\r`, so the type name becomes `"Random Note Generator\r"` and the node is **silently skipped**. `s` values get a stray `\r`. `end\r` doesn't close the block. | C | Reader strips a trailing `\r` from every line. Test with a CRLF copy of `patch_1.inf`. |
| A2 | Trailing spaces after a type name or value | C | Trim trailing whitespace on type names and non-`s` values |
| A3 | UTF-8 BOM at file start makes the magic check fail ("not an Infinite patch", which is at least loud) | C | Skip a leading BOM |
| A4 | Tabs, blank lines, `#` comments | partly C (leading whitespace is already stripped) | Comments are explicit (block 3.2) |
| A5 | Patch from a newer Infinite: `version > kVersion` refuses, but a *same-version* patch with params added by a newer build drops them silently | C | Write a `built-with <app version>` line (unknown tags are already ignored by older builds). Strict mode on an older build reports "patch uses params this build doesn't have (written by vX)". |
| A6 | Media paths (`s path`, 12 nodes; `filePath`, `exportImagePath`) are absolute, so a patch moved to another machine or CI renders black or silent | C (params exist) | Resolve a relative path against the patch file's folder first. The validator errors `E_MISSING_MEDIA` naming the node and path. `--canonicalize --relative-media` rewrites paths relative to the patch. |
| A7 | Numbers: `1e-3`, `.5`, `-0`, `nan`, `inf`, commas | L | Accept what `strtod` accepts (C locale only). Reject nan/inf. The validator flags an unparsable number instead of letting `atof` turn it into 0. No `setlocale` call exists today; keep it that way and add a test that forces `LC_NUMERIC=de_DE` and checks the parse. |
| A8 | Duplicate node index, duplicate `id` word, a cable to itself, two cables into one single-input slot | L | Validator errors for each (the last-wins behaviour today is silent) |
| A9 | Huge files (an AI loops and writes 50k nodes), very long `s` lines | L | Caps on the validator (node count, line length) with clear errors. RPC `load_patch_text` has a size cap. |

### The live and AI loop
| # | Case | St | Required behaviour |
|---|---|---|---|
| L1 | **Live reload reads a half-written file** while the AI is saving | L | Reload only after the file's size and mtime have been stable for 300 ms **and** it parses. A failed parse keeps the current graph and shows the error in the banner, never an empty canvas. The skill tells the AI to write to a temp file and rename it (an atomic save). |
| L2 | The user edits in the UI while the AI edits the file | L | Unsaved-edit guard (block 4). Add a diff banner, "file changed: 3 nodes, 2 cables", with Reload / Keep mine. |
| L3 | Reload floods undo history | L | One checkpoint per reload, with reloads within 2 s merged into one |
| L4 | Reload resets stateful nodes (feedback, sims) and the transport position while the user is performing | L | Keep the transport position. Stateful nodes reset only if their own params changed, and the reset is shown in the banner. |
| L5 | The AI judges a PNG wrong: tiny contact sheet, colour-managed viewer, 16-bit PNG shown as 8-bit | L | Contact sheet cells ≥ 480 px wide, sRGB tagged. Stats in JSON (2.2) are the ground truth for black, clipped and empty. |
| L6 | The AI can't hear, so "sounds good" is unverifiable | by design | The skill says so, and ends music tasks by asking the user to listen. Never claim it sounds good. |
| L7 | The AI uses a node or param that exists only on one OS (per-OS `#if` nodes, `project_node_help_coverage` memory) | L | `--describe` marks platform availability. Validator warning `W_PLATFORM_ONLY`. |

### Platform
| # | Case | St | Required behaviour |
|---|---|---|---|
| P1 | **Windows: `Infinite.exe` is built `WIN32_EXECUTABLE TRUE`** (`CMakeLists.txt:874`), so stdout and stderr are not attached to the terminal and the JSON status line and exit code are invisible or unreliable in cmd/PowerShell | C | Ship a tiny console-subsystem `infinite.exe` (or `Infinite-cli.exe`) that launches the GUI binary with the args and pipes its output back, or call `AttachConsole(ATTACH_PARENT_PROCESS)` early in headless mode and document its limitations. Recommended: the separate console shim (the same pattern as the console-subsystem helper at `CMakeLists.txt:933`). |
| P2 | macOS: running the binary through a symlink (the "command line tool" button) must still find the bundle's Resources (fonts, shaders) | L | Resolve `_NSGetExecutablePath` + `realpath`, never `argv[0]`. Test from a symlink in `/usr/local/bin`. |
| P3 | macOS Gatekeeper/quarantine on a freshly downloaded app run from the terminal first | L | The shim prints a hint if the binary is quarantined. The app must be opened once from Finder (document it). |
| P4 | Linux: no `DISPLAY`/`WAYLAND_DISPLAY`, Wayland-only desktops, AppImage FUSE missing | C (GLFW needs a display) | `E_NO_DISPLAY` with the `xvfb-run` hint (1.6). Document `--appimage-extract-and-run`. |
| P5 | Unicode or space-containing paths, and Windows long paths > 260 characters | L | UTF-8 everywhere and wide APIs on Windows (`windows-parity`). Test with `~/Desktop/My Shots/ünïcode.inf`. |

### Graph topology: chains, splits, merges, loops, bypass
Tested on `build/` on 2026-09-29 with 11 small patches (Shape, Formula, Blend, Fit, Feedback,
Oscillator, Mixer, LFO). **`--validate` returned `ok` with no warnings for every broken one.**

| # | Case | St | What happens today | Required behaviour |
|---|---|---|---|---|
| G1 | **Blend reports 64 input slots** in `--describe` (every other type is right). `SchemaFor` (`main.cpp` ~7904) probes slots 0–63 with `SlotKindOf`, and `CableFor` maps *any* Blend slot > 0 to input B (`main.cpp` ~7287) | C | `cable 3 5 2` passes the validator and silently **replaces** input B (the frame equals a Formula-only render) | Bound the probe by `InputCountFor(gn)` for image slots. Make `CableFor` return null for Blend slot ≥ 2. Test: `cable <blend> 2 …` → `E_BAD_SLOT`. |
| G2 | Two cables into one slot (A8) | C | The last line wins with no message (Output showed only the second source) | `E_SLOT_TAKEN` naming both lines. Fan-*out* from one output stays legal. |
| G3 | `flags … bypassed=1` on a node that can't be bypassed (Blend, Mixer, 2+ inputs; `CanBypass`, `main.cpp` 7268) | C | The frame loop clears it silently (`main.cpp` ~95801), so the author's "off" node is on | `W_BYPASS_IGNORED` (strict: error) using the same `CanBypass` probe. `--explain` shows the effective bypass state. |
| G4 | Bypassed 1-input node | C (works) | Passes its input through (a bypassed Fit equals no Fit) | Keep. `--explain` marks it `bypassed → passes <src>`. Add to goldens. |
| G5 | Bypassed synth | C (by design) | Silence (`BypassSource()` null) | `--explain` says "bypassed: silent" so the AI doesn't think audio vanished for another reason. |
| G6 | **Image loop without a Feedback node** (Blend → Fit → Blend) | C | No hang, no error: the cook memo hands back last frame's texture, so it acts as a hidden one-frame delay whose result depends on cook order | `W_IMAGE_CYCLE` naming the nodes and suggesting a Feedback node. A loop that passes through a Feedback node is legal and must stay silent. Extend `findCycle` (`PatchSchema.cpp` ~274) to image and geometry cables, with Feedback as the one breaker. |
| G7 | Image loop *through* Feedback | C (works) | Legal by design (`FeedbackNodes.h`); a 50 % Blend with Feedback renders | Keep silent. Add a golden, because trails/echo patches depend on it. |
| G8 | Geometry loops | L | Not checked by the validator | Same as G6 for `geo` cables. Verify whether the geometry cook recurses or memoizes before choosing error vs warning. |
| G9 | `mod` whose source is not a modulator (`mod 3 0 3`, source = Shape) and `dstParam` 999 | C | Accepted with no message | `E_NOT_A_MODULATOR` and `E_BAD_PARAM` (index past the node's `KnownParam` count). |
| G10 | Modulator loops (LFO A rate ← LFO B, LFO B rate ← LFO A) | C (renders) | Accepted and renders | Keep legal (it's a real technique) but warn `W_MOD_CYCLE` once and state the one-frame lag in `--explain`, so determinism tests know about it. |
| G11 | Merge with an empty input (Blend with only A, mix 0.5) | C | The missing input counts as transparent black, so the picture dims (alpha 189 instead of 255) | `W_OPEN_INPUT` on merge nodes (Blend, Layer Stack, Mixer, Blend Audio) when a slot the mix depends on is empty. |
| G12 | Audio fan-out (one Oscillator into two Mixer channels) | C (correct) | Same pitch (225 Hz); it's summed, not pulled twice. Peak 1.71, so it **clips** | Keep. `--audio-summary` must report clipping (peak > 1.0) as a warning, and the skill tells the AI to lower gain after merges. |
| G13 | Nodes reachable only through `mod`/`pal` (an LFO) count as used; a whole chain that never reaches Output/Audio Out is `W_UNUSED_NODE` | C (code read) | Correct already | Keep. Also warn when an Output's image input is empty (`W_OUTPUT_EMPTY`), which today renders a blank frame silently. |
| G14 | Nodes with both image and audio pins (Output slot 1 = audio, Oscillator slot 0 = notes, 1 = FM) | C | `E_KIND_MISMATCH` already catches wrong kinds | Once 3.2 lands, name-based slots (`cable out.audio …`) remove numeric guessing. |
| G15 | Usage text says `--render … <out.mp4\|mov\|wav>`, but `.wav` is refused with `E_UNSUPPORTED_CONTAINER` | C | Contradicts itself | Either support `.wav` (audio-only render, useful for sound design) or drop it from the usage text. Recommended: support it, since 7.1 stems need it anyway. |

### 3D category (80 types): what makes it harder
Test prompt: "a cube emitting particles, colour-gradient it". The patch (Cube → Set Vertex Color →
Render 3D geo A, Particle System → geo B) validates clean and renders at 0.1/1/2 s. It only looks
right because the author knew the rules below, and today no tool tells the AI any of them.

| # | Case | St | What happens today | Required behaviour |
|---|---|---|---|---|
| D1 | **Particle System has no geometry input**: it emits from Point/Sphere/Box/Disc only (`SimulationNodes.h:29`) | C | "A cube emitting particles" = a Box emitter with `emitRadius 0.5` sized to the unit cube (half-extent 0.5, `Mesh.cpp:117`) and hidden inside it. It can't emit from the surface, and when the cube moves or spins the emitter does not follow | The skill has a recipes table ("emit from a mesh" → Box/Sphere matched to the primitive's size; moving emitter → modulate the emitter's position params from the same source as the mesh). If surface emission matters, it's a node feature, not a headless one. `--describe` says "emitter: built-in shapes only". |
| D2 | **"Gradient" means 4 different things** in 3D: particle age (`startColor`→`endColor`), Set Vertex Color `Index` (rampA→rampB by vertex order, so on a cube it shows **per-face banding**, not a smooth sweep), `Position` (bbox → RGB rainbow that **ignores rampA/rampB**), and `Texture` fed by a Ramp image (same gradient on every face, via UV) | C | The help text says Set Vertex Color has "a two-colour Ramp" source, but no source by that name exists (`GeometryOpNodes.cpp:1098`) | Fix the help text (`main.cpp` 40136). The skill's recipes table maps "gradient across the object" → the real options with a rendered thumbnail each. Optional node fix: a `Position` axis + rampA/rampB mode. `--explain` prints which gradient each colour param actually drives. |
| D3 | Colour stacking: Set Vertex Color vs each modifier's own material block (`color`, `inherit`) | L | `inherit 0` on a downstream Transform resets colour (test gf.inf: the red copy) | `--explain` shows the effective material per Render 3D input. The validator warns when a vertex colour is set upstream of an `inherit 0` modifier that replaces it. |
| D4 | **Framing**: Render 3D's camera doesn't auto-frame | C | A Transform with `offsetX 1.2` pushed a cube out of the frame. Nothing warns | `--explain` prints each geometry input's world bbox and whether it is inside the camera frustum. `W_OUT_OF_FRAME`, plus image stats (2.2) "object covers N % of frame". |
| D5 | Slot layout: Render 3D geo A–D are slots 0–3, env is slot **8**, and 4–7 are unused | C | Numeric guessing fails | Name-based slots (3.2): `geo render.env hdri`. |
| D6 | Geometry fan-out (one Cube into Render 3D and into a Transform) | C (correct) | The original is not changed | Keep. Add a golden. |
| D7 | Simulations (Particle System, Cloth, Ocean) run on Transport beats with a 0.25 s catch-up cap and reset on rewind | C | `--frame` steps every frame from 0 (`main.cpp` ~66545), so frame 60 has its full history. It's deterministic (xorshift seeded from `seed`) | Keep. Document: `--frame 30` costs 30 steps. A tempo change in the patch changes the particle timing (dt is derived from beats and BPM). |
| D8 | **Audio Displacement uses the wall clock** for its attack/decay (`AudioDisplacementNode.cpp:182`, `steady_clock`) | C | Offline `--frame` cooks many frames in one tick, so dt clamps to 1 ms and the displacement barely reacts. It's non-deterministic across machines | Same fix as the gesture clock (7.3): dt from `Transport::Seconds()`. Also audit `WaveTerrainNode.cpp:746` (a wall-clock rebuild throttle can leave stale meshes offline). Add both to 7.3's param-trace test. |
| D9 | Model 3D / HDRI / Image textures load asynchronously | L | = E4 | Graph-ready barrier (E4) covers the geometry importers too. |
| D10 | Heavy 3D (maxParticles, subdivide levels, Array count × Instance on Points) explodes the vertex count | L | An AI can write `levels 6` on a 100k-vert mesh | `--explain` prints the vert/point count per node. The validator warns above a budget (e.g. 5 M verts); `--timeout` is the hard stop. |
| D11 | Units: rotations in degrees, sizes relative to the unit primitive, camera in orbit terms (`camDistance`, `camAzimuth`, `camElevation`) | C | Easy to mix up | `--describe` carries a `unit` per param (deg, px, ms, Hz, world). The generated skill (block 6) prints it. |

Tests for G1–G15 and D1–D11 go in `tests/headless/topology/` as one `.inf` per case plus the expected
validator codes. They are cheap (under 200 ms each) and run with the step 2 gate.

## Order and commits

| Step | Delivers | Gate before moving on |
|---|---|---|
| 1 | Block 1 (`--render`, `--frame`, status, exit codes) macOS + E1, E4, E8, E9, E10 | the LFO-only patch renders moving frames; `--render` of `assets/examples/patch_1.inf` produces a playable file with audio; `--frame` PNG matches what the Output shows |
| 2 | Block 3.1 + 3.3 + 3.3b (`--describe`, strict validator, `--explain`) | every registered type spawns headless without crash; a typo'd param refuses under strict with the nearest key; `--explain` of `assets/examples/patch_1.inf` lists every cable and binding the UI shows; A1–A3, A6–A8 pass on a CRLF + BOM + relative-media copy; G1–G3, G6, G9, G11 give their codes |
| 2b | Block 8 census (script + baseline) + E14 | full census runs; E14 fixed and the census shows no `render-ok-but-file-unreadable` |
| 3 | Block 2 | sine test passes; black patch reports 100 % black |
| 4 | Block 3.1b (controls joined, dropdown names, actions) + 3.2 + 3.4 | an authored patch using names loads identically to its canonicalized numeric form (compare `PatchJson::ToJson`) |
| 5 | Block 1.6 Linux/Windows + Block 5 + P1–P5 | Linux CI renders goldens; Windows builds |
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
