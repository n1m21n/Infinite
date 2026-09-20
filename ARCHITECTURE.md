# Architecture Map

Documentation-only index of where things live in this codebase. No code was
moved to produce this — `main.cpp` is a single file that has grown to roughly
71,000 lines (almost entirely one `int main()` plus the free functions it
calls), so instead of a risky physical split, this doc tells you which named
symbol to grep for a given kind of task.

**Anchors below are symbols (function names, global variables, or — where
neither exists because the code is inline in `main()` — a distinctive
`grep`-able call/string right at that spot), never line numbers.** A 71k-line
file that gets edited constantly makes any line number stale within days;
`grep -n '\bSymbolName\b' src/main.cpp` finds the current location in
milliseconds and was used to verify every row in this doc. When a row's
anchor is a call/string rather than a real symbol, that's called out — it
means the code lives inline in `main()` with no function of its own.

## Categories

The taxonomy is derived from how this app is actually built, not a generic
split:

1. **Node Library** — what each node *does*: its params, math, per-type UI
2. **Engine / Runtime Core** — the machinery nodes and UI both sit on: graph
   model, patch format, factory, clock, modulation routing, GL/mesh utils
3. **Editor UI** — the interactive canvas surface: menus, widgets, minimap,
   groups, docked panels, keyboard shortcuts
4. **App Features** — cross-cutting, document-level behavior: undo/redo,
   save/load, export, clipboard, preferences, the cook/eval tick
5. **Platform Layer** — macOS and Windows native shims (file dialogs, audio
   device I/O, MIDI, video/image decode, plugin hosting)
6. **Dev/Test Harness** — env-var-gated self-tests, not product code

When asked to review/enhance/build a feature in one of these, grep the
anchors listed under it rather than scrolling.

---

## 1. Node Library

What each node type does: math, state, per-node parameter UI.

**Files:** `src/nodes/*.h` / `src/nodes/*.cpp` (one pair per node type)

| File | Node |
|---|---|
| ImageSourceNode | File-backed image source (stb_image) |
| SlideshowNode | Folder-backed, transport-synchronised image sequence with GPU transitions |
| ShapeNode | Procedural SDF shapes |
| FormulaNode | User GLSL expression source |
| FilterNode | Generic single-in/out shader-pass (covers every filter table entry) |
| BlendNode | Two-input compositing |
| LayerStackNode | Four-input stacked compositing |
| FitNode | Resolution adaptor (TouchDesigner "Fit TOP"-style) |
| VideoSourceNode | Video file source, synced to `Transport`; also an `IAudioSource` — second output plays the clip's own audio track on the same clock |
| ModulatorNodes | LFO/Random/Pattern/Math — control-value emitters |
| NoiseNode | Value/Perlin/Voronoi/ridged noise |
| ResynthNode | Iterative image resynthesizer |
| MacroNodes | Hand-driven knob/XY modulator |
| CurvesNode | Photoshop-style per-channel curves |
| RemoveBgNode | OS on-device background segmentation |
| DrawNode | Paintable canvas (persistent FBO) |
| RampNode | Gradient generator |
| PaletteNode | Palette extracted from a reference image (Oklab k-means); drives colour params graph-wide |
| AnalyzeNodes | Image analyze |
| Geometry3DNodes | Surface properties struct for 3D renderer |
| ModelSourceNode | Disk mesh import |
| Text3DNode | Extruded text geometry |
| UtilityNodes | Comment, Group, Null, Viewport, Null3D, Material, JoinGeometry, MetaBall, MeshToPoints |
| PathNode | Move-along-path over time |
| OceanNode | Gerstner-wave water surface |
| CurveNode | 3D curve through space |
| SimulationNodes | Stateful sims (cloth, particles, etc.) carried frame-to-frame |
| GenerativeNodes | Iterative 3D generative mutator |
| GeometryOpNodes | Mesh→mesh operators (dropdown-selected, one class) |
| SceneNodes | Camera & lights |
| FeedbackNodes | Feedback/trails/reaction-diffusion |
| SwitcherNode | Cycles between inputs on a timer |
| TextNode | Typography via CoreText/CoreGraphics (macOS) or GDI+ (Windows) |
| SyphonInNode | Syphon video client (zero-copy GPU texture receiver, macOS) / Spout (Windows) |
| SyphonOutNode | Syphon video server (zero-copy GPU texture publisher, macOS) / Spout (Windows) |
| ProjectionNode | Projection mapping, 4-corner homography warp, mesh warping, and test patterns |
| OutputNode | Terminal node — identity-pass FBO, drives recording |

### Field node system

`Field` is a small embedded expression language with its own compiler
(lexer → typed IR → three backends), hosted by six node types rather than
one — see the `field-language`, `field-compiler`, `field-domains`,
`field-integration` and `field-state` skills for the language itself; this
row is only the code-location map.

**Files:** `src/nodes/FieldElementNode.h`/`.cpp`, `FieldPrimitiveNode.h`/`.cpp`,
`FieldSampleNode.h`/`.cpp`, `FieldSynthNode.h`/`.cpp`, `FieldGraphNode.h`/`.cpp`,
`FieldPixelNode.h`/`.cpp` (the shared compiler lives behind
`src/core/Expression.cpp`, see `field-compiler`).

**File:** `src/main.cpp` — anchors: `DrawFieldElementParams`,
`DrawFieldPrimitiveParams`, `DrawFieldSampleParams`, `DrawFieldSynthParams`,
`DrawFieldGraphParams`, `DrawFieldPixelParams` (one per node type's param
panel), `DrawFieldDeviceControls` (shared preset-device chrome templated
across the Field node types).

### Audio / note node system

A second cable type and DAG. Most audio *effects* share one C++ class rather than
getting a file each:

- **`src/nodes/AudioEffectNode.h`/`.cpp`** — the one class every entry in the
  `EffectDef` table (`src/audio/EffectDefs.h`/`.cpp`) is instantiated as; adding
  effect N+1 is a table row plus an `IEffectKernel` (`src/audio/dsp/*Kernel.h`/
  `.cpp`, one per effect: Audio Filter, Dynamics, Delay, Reverb, Drive, Stereo,
  Pitch Shifter, Frequency Shifter, Chorus, Flanger, Phaser, Bitcrush,
  Transient Shaper, Stutter, Ring Mod, Tremolo, Formant Filter, Wavetable Shaper,
  EQ), not a new node class. EQ is a deliberate second, separate node from Audio
  Filter rather than a re-expansion of it — Audio Filter stays one band/one type
  on purpose.
- **`src/audio/Wavetable.h`/`.cpp`** — the 12-table/8-frame/10-mip-level bank
  shared by the **Wavetable** synth and the **Wavetable Shaper** effect
  (the latter reads a frame as a transfer curve rather than an oscillator).
- **`src/audio/SampleSlot.h`** — the main-thread-hands-a-buffer-to-the-audio-
  thread lifetime pattern (pending/active/retire-ring), shared by **Sampler**
  (one instance), **PaulStretch** (one instance), **Drum Sequencer** (eight, one per lane),
  **Wave Terrain**, and **Equation Synth**. Lifted out of
  `SamplerNode.cpp` so a new sample-playing node never has to reimplement its
  own use-after-free trap.
- **`src/nodes/AnalogNode.h`/`.cpp` & `src/nodes/AnalogSynthCore.h`** — virtual-analog
  polyphonic synthesizer node. Features dual PolyBLEP anti-aliased oscillators
  (osc 1 unison stack, osc 2 tune/detune/sync, sub-oscillator, noise generator,
  pre-filter drive), non-linear Zero-Delay-Feedback (ZDF) 4-pole Moog ladder
  and 2-pole SVF filters with keytracking, analog pitch/cutoff drift, and up to
  8-voice polyphony with ADSR amplitude envelopes.
- **`src/nodes/EquationNode.h`/`.cpp` & `src/audio/dsp/EquationDsp.h`** — Desmos-style
  mathematical equation oscillator synth node. Evaluates user math expressions
  \(y = f(x, a, b, c, d)\) or presets in real-time, generates an exact 10-level
  anti-aliased Fourier mip pyramid wavetable via 1024-point Radix-2 FFT, and renders
  as a polyphonic synthesizer with ADSR envelopes, SVF filter, unison, and glide.
- **`src/nodes/AudioPluginNode.h`/`.cpp`** — hosts a third-party plugin (Audio
  Units on macOS, VST3 on Windows — see `PluginHostWin.cpp`/`PluginVST3Win.cpp`)
  as an ordinary audio effect node. Unusually for this codebase it
  is a *three*-object node: the `INode` main-thread half, its `AudioNode` audio
  half, and the plugin instance itself, an opaque `Platform::PluginHandle` the
  main half owns and the audio half only ever reads through a
  `std::atomic<PluginHandle*>`. Two consequences are load-bearing and written
  up on the class: mapped plugin parameters deliberately bypass `ParamMailbox`
  (they go straight to `AUParameter`, which does its own smoothing, and 32
  mapped params would not fit the mailbox's 64 slots anyway — this is why the
  node carries a documented `AUDIOPARAMSWEEPTEST` baseline), and swapping or
  unloading a plugin retires the old handle for one generation before
  destroying it, mirroring `AudioEngine::SetTopology`.
- **`src/nodes/MolderNode.h`/`.cpp` & `src/audio/dsp/MolderDsp.h`/`.cpp`** —
  analysis/genome/additive-resynthesis sample-mangling synth: a source
  sample is decomposed once (YIN pitch, per-frame STFT partial tracking
  with a voiced/unvoiced gate, sinusoids-plus-residual split) into an
  `Analysis`, and each "roll" mutates a small deterministic `Genome` (a
  seeded xorshift32 replayed `generation` times, so a patch persists just
  two integers rather than the genome itself) and re-renders a playable
  sample from `Analysis x Genome`. Unlike every other audio node here, this
  is a **three**-thread node, not two: both `Analyze()` and `Render()` are
  full-buffer, tens-to-hundreds-of-milliseconds operations that would blow
  `CookIfNeeded`'s <5us budget, so they run on a dedicated worker
  `std::thread` (one job in flight at a time, joined before the next
  starts — `SampleScanner`'s thread shape) and hand a finished result to
  the main thread through a single `mResultReady` atomic flip, never a
  mutex. The rendered buffer then reaches the audio thread through the
  ordinary `SampleSlot`. The destructor sets an abort flag and joins the
  worker before any member is freed, so a delete mid-analysis can't
  use-after-free a worker still writing into the node.
- **`src/nodes/RemoveBgNode.h`/`.cpp`** — background removal via
  `Platform::SubjectMask` (Vision on macOS, ONNX Runtime on Windows). Another
  `SampleScanner`-shaped worker `std::thread`, but with a **latest-only**
  request slot rather than a run-to-completion job queue: a `FrameRequest`
  arriving while one is already queued replaces it outright, so a slow
  segmentation pass on a video input never backs up. The completed
  `FrameResult` carries the mask *and* a copy of the source pixels it was
  computed from (matched by a monotonic `serial`), and `CookIfNeeded`
  composites the mask exclusively against that paired source texture, never
  the live current frame — otherwise a moving subject's mask would visibly
  lag the frame it's applied to. The worker never touches a GL object; the
  main thread does the GPU readback before handing off and the two texture
  uploads after picking up a result.
- **`src/audio/PluginScanner.h`/`.cpp`** — `SampleScanner`'s thread +
  `PollResults()` + disk-cache shape, over a component-registry query instead
  of a directory walk, so it has no user-managed folder list. Backs the docked
  panel's fourth mode (anchor: `gPluginFilter`, `DrawBrowserFilterStrip`).
- **Every Objective-C object involved in plugin hosting lives behind
  `Platform.h`'s plugin section** — `src/nodes/` and `src/audio/` stay pure
  C++, and the audio thread never sends a message or touches ARC: the render
  path calls an `AURenderBlock` cached on the main thread at prepare time, with
  an `__unsafe_unretained` stack pull-input block. The plugin's editor is a
  plain `NSWindow` (the only one in the app), which works because
  `glfwPollEvents` drains and dispatches `NSApp`'s queue. AU is always
  supported; VST3 is a second backend behind the same surface on both
  platforms, gated behind the `INFINITE_ENABLE_VST3` build option (off by
  default — the VST3 SDK is GPLv3-or-commercial and this codebase is MIT, see
  `LICENSE` and `docs/plans/audio/plugin-hosting.md`).
- Per-effect body/visualizer UI lives in `src/main.cpp` as `DrawXxxBody`/
  `DrawXxxVisualizer` pairs next to the `EffectVisualizerId` switch inside
  `DrawAudioNodeBody` — see `.claude/skills/new-audio-node/SKILL.md` for the
  exact wiring sites and `.claude/skills/audio-node-ui/SKILL.md` for the
  layout grammar.
- **`src/nodes/VideoSourceNode.h`/`.cpp`** — the first node with both an image
  output *and* an audio output on the same `INode` (`OutputCount() == 2`;
  output 0 is the picture, output 1 is `IAudioSource`). Its `VideoAudioNode`
  audio half doesn't run an independent playback clock: it reads from the
  exact `mPosition` the picture side already integrates on the main thread
  (which already bakes in `speed`, including negative/reverse), published
  once per frame via a single `std::atomic<double>`, so the two outputs can't
  drift onto different clocks even though sub-frame lip-sync is not
  guaranteed. `IAudioSource::IsAudioOutputIndex(int)` exists because of this
  node specifically — every other `IAudioSource` has exactly one output and
  it's the audio one, but the four `srcIsAudioNode` dispatch sites in
  `main.cpp` need to know *which* of this node's two outputs a dragged cable
  came from before treating it as an audio source.

### Invariants for `IGeometrySource`-consuming nodes

Four rules every node with a single `IGeometrySource*` input (or `sourceInput`/
`instanceShape`/etc.) is expected to follow, each backed by an automated
sweep in `geometry-transform-sweep` (also run as part of
`run-infinite-hygiene`) rather than left to manual review:

1. **Forward every side-channel you don't explicitly change.** `GetMesh()`,
   `GetModelMatrix()`, `GetMaterial()`, `GetSurfaceTexture()`,
   `GetMaterialTexture()`, `GetMappingTransform()` — a node that bakes its
   input's mesh but forgets to forward one of these silently drops it
   somewhere downstream with no error. This has happened three times
   (`ClothNode`, `MeshResynthNode`, `MeshToPointsNode` all forwarded material
   and textures but not `GetMappingTransform()`) because each accessor is a
   separate manual override with nothing enforcing "forward all or none."
   `MAPPINGSWEEPTEST` checks this for `GetMappingTransform()` specifically;
   there is currently no sweep for the others, so a new one dropping
   `GetMaterialTexture()` wouldn't be caught automatically yet.
2. **Only bump a revision/generation stamp when your actual output changed.**
   `MeshRevision()` (or any node-local generation counter that feeds into it)
   must not move just because `CookIfNeeded` ran again — it has to reflect a
   real change in what `GetMesh()`/`GetPoints()`/etc. would return.
   `DisplacementNode` violated this by bumping `mTexGeneration` on every cook
   while a texture was connected, whether or not its pixels changed; any
   stateful node downstream (`ClothNode`) that keys a full state reset off
   "did the input's revision move" then resets every frame instead of ever
   settling. `REVISIONSWEEPTEST` checks this generically.
3. **Render 3D's scene cache must see every component stamp separately.**
   `Render3DNode::BuildSceneSignature` tracks `MeshRevision()`,
   `PointCloudRevision()` and `CurveStamp()` as distinct fields, not folded
   together. They used to be XOR-folded into one value, which silently produced
   a constant `0` for every node that returns the same counter from two of them
   (`MeshToPointsNode`, both `DistributePoints*` nodes, `CurveNode`) — the
   render then cached its first frame forever and no upstream edit ever showed
   up in the viewport. `RENDER3DCACHESWEEPTEST` checks this generically.
4. **If you pass geometry through, pass the instancing through with it.**
   `InstanceOnPointsNode::GetMesh()` returns the single stamp mesh and carries
   its N placements separately, so consumers walk `PassthroughSource()` to find
   the instancer (`Render3DNode::FindInstancer`, `NodeViewport`'s copy) and read
   `GetInstanceGroupMatrix()` to pick up a wrapping Transform's rigid move.
   A forwarding node must override **both or neither** — `Null3DNode`,
   `DisplacementNode` and `WrapNode` had neither, so instances vanished behind
   them; `MaterialNode`, `SetColorNode`, `MergeByDistanceNode` and
   `Switcher3DNode` had only the first, so a wrapping Transform's move was
   silently discarded. A node with several geometry inputs must forward the
   group matrix from the same input its `PassthroughSource()` returns.
   `INSTANCESWEEPTEST` checks this generically.

If you add a new node type in this category, wire it into all four sweeps
rather than hand-writing a one-off check — see `geometry-transform-sweep`'s
SKILL.md, "Adding a new node type to a sweep."

**File:** `src/main.cpp` — registration, per-node UI, and node-graph wiring
(as opposed to the *rendering* of pins/links, which is Editor UI)

| Anchor (grep this symbol) | What |
|---|---|
| `DisplayName` | Display-name formatting for registered types |
| `RegisterNodes` | Registers every type with `NodeFactory` |
| `ModulatorForOutput`, `FindNodeByIndex` | Node/output lookups |
| `InputCountFor` | Per-type input pin counts |
| `CableFor` | Node/slot → `ImageCable` mapping |
| `ConnectGeometrySlot` | Geometry/camera/light connection mapping |
| `ReloadDerivedState`, `CopyParams` | Post-load/paste state rebuild and param copy |
| `DrawXxxParams` (grep the pattern) | **All per-node parameter panel functions** — one per node type, the bulk of the file; grep the specific node's name, e.g. `DrawShapeParams`, `DrawFormulaParams` |
| `DisconnectLinkById`, `DisconnectAllTo`, `RemoveNodeByIndex` | Node/link lifecycle |
| `OnFilesDropped` (GLFW drop callback) / `gDroppedFiles` | Drag-and-drop file → auto-spawn matching source node |
| `dynamic_cast<...Node*>(gn.node.get())` chain inside `main()` (not its own function — grep any `DrawXxxParams(n)` call inside it, e.g. `DrawFormulaParams(n);`) | Per-frame dispatch: routes each node to its `DrawXxxParams` (chrome around this dispatch is Editor UI, see below) |
| `ed::QueryNewLink` | New-connection validation — type-checks proposed links per node kind |
| `ed::QueryDeletedNode`, `ed::QueryDeletedLink` | Editor-initiated delete → `RemoveNodeByIndex`/`DisconnectLinkById` |

---

## 2. Engine / Runtime Core

The graph data model and low-level machinery underneath both nodes and UI.

**Files:**
- `src/core/GraphNode.h` — editor-side wrapper owning an `INode` + stable index
- `src/core/INode.h` — mix-in interface for graph nodes
- `src/core/ImageCable.h` — typed patch cable (texture handle between nodes)
- `src/core/NodeFactory.h/.cpp` — module registry
- `src/core/Modulation.h/.cpp` — control-value node base
- `src/core/Palette.h/.cpp` — colour bindings (palette node + swatch -> a colour param), the colour counterpart of Modulation
- `src/core/Transport.h/.cpp` — global clock (drives modulators, video playback); accessed as the `Transport::Instance()` singleton
- `src/core/BlendModes.h/.cpp` — shared blend-mode vocabulary + GLSL
- `src/core/FilterDefs.h/.cpp` — declarative filter-type table
- `src/core/GLUtil.h/.cpp` — FBO/shader-pass helpers
- `src/core/Mesh.h/.cpp` — mesh + matrix math for 3D nodes
- `src/core/Patch.h/.cpp` — patch file *format* (struct + read/write primitives, including `Patch::StreamRecord` for the arrangement timeline; the save/load/undo *flows* that use it are App Features, below)

**File:** `src/main.cpp`

| Anchor (grep this symbol) | What |
|---|---|
| `#include` block at the top of the file | Includes |
| `gNodes`, `gGroupMembers`, `gNextIndex`, `gEditor`, `struct LinkInfo` / `gLinks` / `FindLink` | Core node/link registries |
| `glfwInit()` (the real call, not a comment referencing it) | App bootstrap: GLFW/GL/ImGui init, HiDPI fonts (`AddFontFromFileTTF`), backend init, App Support dir, `ed::Config` |
| `while (!glfwWindowShouldClose(window))` in `main()` | Main-loop top: poll events, `ImGui_ImplOpenGL3_NewFrame`/`NewFrame()` |
| `gLinks.clear()` inside the per-frame block in `main()` (not its own function) | Per-frame link-table rebuild (image/geometry/modulator link data — rendering the resulting `ed::Link()` calls is Editor UI) |
| `ApplyModulationAndPalette` | **Modulation/cook pipeline** — applies bound modulator values into params; paired with `CookIfNeeded(frameId)` per Output node in `main()`'s frame loop (the core generative-evaluation tick; listed again under App Features since it's also the thing "play" triggers) |
| `ImGui::Render()` / `glfwSwapBuffers(window)` at the bottom of `main()` | Render/present: GL clear, swap buffers, frame limiter, `main()` exit |

---

## 3. Editor UI

The interactive canvas: rendering, layout, menus, widgets, theming, minimap,
groups, docked panels, popups, keyboard shortcuts for interaction.

**File:** `src/main.cpp`

| Anchor (grep this symbol) | What |
|---|---|
| `kPreviewSize`, `kViewportSize`, `kParamWidth`, `kPinRadius`, `kPinHit` | Layout constants |
| `gGridSnap`, `gTargetFps`, `gVsync` | Canvas/UI globals: grid snap, target FPS/vsync, (frame-timing and minimap globals live alongside these) |
| `struct DropdownRequest` / `gDropdown` | Deferred dropdown & color-picker popup infra (works around node-editor canvas-transform bug) |
| `OnFilesDropped` | Drag-and-drop file globals + GLFW drop callback |
| `DropdownButton`, `ColorSwatch` | Reusable param widget chrome |
| `ModSlider`, `BeginNodeParams`, `ModSliderInt` | The modulatable-slider widget used by nearly every node's param panel |
| `NodeSeparator` | Param-panel section separator |
| `AlignOptions` | Alignment dropdown option list |
| `EyeToggle`, `BypassToggle` | Hand-drawn param-visibility/bypass icons |
| `DrawPin` | Generic pin-drawing helper |
| `DrawFxPad` | XY control-surface widget (Resynth) |
| `DrawCurveEditor` | Interactive tone-curve widget |
| `DrawPaintablePreview`, `DrawCommentPreview`/`DrawCommentParams` | Paintable-canvas and comment-node chrome |
| `GroupOwning`, `PruneDeadGroups`, `AutoFitGroupToMembers`, `DrawGroupNode` | **Node group UI system** |
| `DrawPreview`, `DrawModulatorMeter` | Image/3D thumbnail; scope-style modulator meter |
| `DrawModMatrixDocked` (content: `DrawModMatrixTable`) | **Modulation matrix docked panel** — dockable to any of 4 sides (`gModMatrixDock`), lists every bound modulator × destination |
| `DrawPerfPanelDocked` (content: `DrawPerfPanelContent`) | **Performance (macro) matrix docked panel** — dockable to any of 4 sides (`gPerfPanelDock`); `gPerfElements`/`gPerfLayout` are its records, `gArrangeStreams` (see App Features) reuses the same `Patch::PerfRecord` shape for the arrangement timeline |
| `DrawViewportPanelDocked` | Docked 3D viewport panel |
| `DrawBrowserFilterStrip` (call sites: `gModulesFilter`, `gPluginFilter`, `gFieldFilter`) | Shared search/sort/filter strip used by the docked node-browser side panel (module/plugin/Field/media tabs — `gSearchPanelMode`) |
| `DrawShortcutsWindow` / `gShortcutsOpen` | **Keyboard shortcuts reference window** (floating, opened from Help) |
| `DrawHelpWindow` | Module reference modal |
| `DrawMinimap` | Rendering, click/drag-to-navigate |
| `AddFontFromFileTTF` | ImGui context/HiDPI font setup, base style |
| `ImGui::BeginMenu("File")` inside `main()`'s `ImGui::BeginMenuBar()` block (not its own function) | Main menu bar: File/Edit/View menus, minimap settings, performance (FPS/vsync), transport buttons + BPM slider, trackpad wheel damping, canvas rect capture |
| same `dynamic_cast<...Node*>(gn.node.get())` chain as Node Library, above | Per-frame node chrome: pin layout, node body, preview dispatch, Eye/Bypass row, "mod" badge (the dispatch-*to*-param-panel logic itself is Node Library) |
| `gLinks.clear()` block (see Engine Core above) | Rendering the rebuilt link table (`ed::Link()` calls, orange-tinted for modulation) |
| `gRequestCopy`, `gRequestPaste`, `gRequestDuplicate`, `gRequestGroup`, `gShortcutsOpen = true` | Keyboard shortcuts: Undo/Redo, Delete, Shift+D duplicate, Cmd+G group/ungroup, Cmd+C/V, drag-checkpoint capture, snap-on-release |
| `DrawMinimap` call site / right-click `ImGui::OpenPopup` for the node spawner / `DrawBrowserFilterStrip` docked panel | Popup layer: minimap draw site, right-click node-spawner popup, deferred popups, docked node-browser side panel |
| `gFormulaEditorOpen`, `DrawHelpWindow` | Floating windows: GLSL Formula editor, Help window |
| `ImGuiKey_S`/`ImGuiKey_O`/`ImGuiKey_N` checks near the bottom of `main()` | Cmd+S/O/N global shortcuts (when no text field focused) |

---

## 4. App Features

Cross-cutting, document-level behavior: undo/redo, save/load, export,
clipboard, preferences, transport wiring, the arrangement timeline.

**File:** `src/main.cpp`

| Anchor (grep this symbol) | What |
|---|---|
| `BuildPatchData` | Serializes live graph to `Patch::Data` (shared by save + undo/redo) |
| `SavePatchTo`, `NewPatch` | Save-to-path, new/blank patch |
| `ApplyPatchData`, `LoadPatchFrom` | Restore graph from snapshot/disk |
| `PushUndoCheckpoint`, `PushUndoSnapshot`, `Undo`, `Redo` | **Undo/redo** |
| `SavePatchInteractive` | Save/Save-As dialog flow |
| `iniPath`/`graphPath` next to the settings-dir resolution near the top of `main()` | Preferences persistence: `imgui.ini`, `Infinite.json` (node-editor layout) under the per-user settings dir (`AppPaths.h`) |
| `Patch::LoadRecents`/`Patch::NoteRecent`/`Patch::Recents` (declared in `src/core/Patch.h`) | Recent-files list |
| `ExportPng` | PNG export |
| `OutputNode::StartRecording`/`OutputNode::StopRecording` (declared in `src/nodes/OutputNode.h`; UI call sites are the `n->StartRecording()`/`n->StopRecording()` calls in the transport/menu-bar and per-node body code in `main.cpp`) | Video recording controls (path, fps, include-audio, start/stop) |
| `ClusterClipboard`, `CaptureClusterLinks`, `ApplyClusterLinks` | Copy/paste and Shift+D duplicate clipboard (Cmd+C/Cmd+V; the request flags live under Editor UI above) |
| `Transport::Instance().Tick(...)` inside `main()`'s frame loop | Transport wiring: per-frame `Tick`, Play/Pause/Rewind, BPM slider (buttons themselves are Editor UI; the `Tick()` call and what it drives is the feature) |
| `ApplyModulationAndPalette` | Modulation/cook pipeline (see also Engine Core — this is the same code, listed here because "press Play" is the user-facing feature it powers) |
| `glfwSwapInterval`, `gTargetFps`, `kFpsValues` | Vsync toggle, target-FPS frame limiter |
| `Patch::StreamRecord` (`src/core/Patch.h`) / `gArrangeStreams` in `main.cpp` | **Arrangement timeline** — data model and serialization only so far (`docs/plans/arrangement/README.md`); no docked timeline panel exists in `main.cpp` yet. Reuses `Patch::PerfRecord`, the same record type the performance matrix stores — see `gArrangeStreams`'s doc comment for the node-index-rewrite rules on `ApplyPatchData`/`RemoveNodeByIndex`/`NewPatch` |

---

## 5. Platform Layer

Native shims kept out of the main C++ translation units — one abstraction
(`Platform.h`) with two implementations, picked by `CMakeLists.txt`. See
`.claude/skills/windows-parity` for the full contract: `src/nodes/` must
never branch on `_WIN32` (0 occurrences is load-bearing), every
`Platform::` declaration needs a macOS implementation, a Windows
implementation, and (for a new `.cpp`) a `CMakeLists.txt` `WIN32_SOURCES`
entry — a missing Windows definition is not a legitimate landing, only a
stub that fills `outError` and returns `false` is.

**Files (macOS):** `src/platform/Platform.h` (shared declaration surface,
829 lines, pure C++ — no Objective-C/COM types leak through it),
`src/platform/Platform.mm`
- Native open-file dialog (image-filtered)
- Image decode via ImageIO (any OS-supported format → RGBA8)
- 3D model loading via ModelIO (OBJ/PLY/STL/USD/USDZ)
- Audio Unit plugin hosting, Syphon GPU texture share
- Vision-based subject segmentation (`RemoveBgNode`'s `Platform::SubjectMask`)

**Files (Windows):** `src/platform/win/`
- `PlatformWin.cpp` — the `Platform.h` surface's Windows implementation: file
  dialogs, image decode, model loading, `WinCommon.h`'s `Utf8ToWide`/
  `WideToUtf8`/`HrToString` helpers (deliberately on no include path — only
  Windows translation units may include it)
- `AudioDeviceWin.cpp` — WASAPI audio device I/O
- `MidiWin.cpp` — WinMM MIDI I/O
- `MediaWin.cpp`, `MediaDecodeWin.cpp` — Media Foundation video/image decode
- `PluginHostWin.cpp`, `PluginVST3Win.cpp` — VST3 plugin hosting (the
  Windows counterpart of macOS's Audio Unit hosting; gated behind
  `INFINITE_ENABLE_VST3` on both platforms)
- `SpoutGLBridge.h`/`.cpp`, `PlatformWinSyphon.cpp` — Spout GPU texture
  share (the Windows counterpart of macOS Syphon)
- `CrashHandlerWin.cpp` — Windows crash/minidump handling

---

## 6. Dev/Test Harness (not product code)

**File:** `src/main.cpp` — no single symbol anchors this; it is interleaved
throughout the file rather than confined to a block. Grep the specific
`INFINITE_*`/`IMAGERESYNTH_*` env var name (e.g. `AUDIOPARAMSWEEPTEST`,
`MAPPINGSWEEPTEST`, `UNDOTEST`, `PATCHTEST`) or the `*TEST` printf verdict
string to jump straight to one test.

Automated regression/visual-verification tests gated behind
`getenv("INFINITE_*")` / `getenv("IMAGERESYNTH_*")` env vars (e.g.
`COLORTEST`, `UNDOTEST`, `PATCHTEST`, `MACROTEST`, `SHOWCASE`, ...). They drive
synthetic frames and `printf` pass/fail verdicts. Ignore these unless the task
is specifically about the self-test harness itself.

---

## Quick lookup by task type

- **"Add a new node type" / "fix a node's parameters or math"** → Node Library
- **"Fix how connections/pins/the graph model work"** → Engine / Runtime Core
- **"Change how X looks/behaves in the editor UI" / "fix a docked panel"** → Editor UI
- **"Undo isn't working" / "add export format" / "fix save/load" / "arrangement timeline"** → App Features
- **"Fix file dialogs / audio device / MIDI / video decode / plugin hosting on macOS or Windows"** → Platform Layer
- **"Something in the self-test harness"** → Dev/Test Harness
