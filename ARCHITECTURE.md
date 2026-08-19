# Architecture Map

Documentation-only index of where things live in this codebase. No code was
moved to produce this — `main.cpp` is a single ~9,146-line file (almost
entirely one `int main()`), so instead of a risky physical split, this doc
tells you which line ranges to open for a given kind of task.

## Categories

The taxonomy is derived from how this app is actually built, not a generic
split:

1. **Node Library** — what each node *does*: its params, math, per-type UI
2. **Engine / Runtime Core** — the machinery nodes and UI both sit on: graph
   model, patch format, factory, clock, modulation routing, GL/mesh utils
3. **Editor UI** — the interactive canvas surface: menus, widgets, minimap,
   groups, keyboard shortcuts
4. **App Features** — cross-cutting, document-level behavior: undo/redo,
   save/load, export, clipboard, preferences, the cook/eval tick
5. **Platform Layer** — macOS-native shims (file dialogs, image/model decode)
6. **Dev/Test Harness** — env-var-gated self-tests, not product code

When asked to review/enhance/build a feature in one of these, open only the
files/line-ranges listed under it.

---

## 1. Node Library

What each node type does: math, state, per-node parameter UI.

**Files:** `src/nodes/*.h` / `src/nodes/*.cpp` (one pair per node type)

| File | Node |
|---|---|
| ImageSourceNode | File-backed image source (stb_image) |
| ShapeNode | Procedural SDF shapes |
| FormulaNode | User GLSL expression source |
| FilterNode | Generic single-in/out shader-pass (covers every filter table entry) |
| BlendNode | Two-input compositing |
| LayerStackNode | Four-input stacked compositing |
| FitNode | Resolution adaptor (TouchDesigner "Fit TOP"-style) |
| VideoSourceNode | Video file source, synced to `Transport` |
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
| TextNode | Typography via CoreText/CoreGraphics |
| SyphonInNode | Syphon video client (zero-copy GPU texture receiver) |
| SyphonOutNode | Syphon video server (zero-copy GPU texture publisher) |
| ProjectionNode | Projection mapping, 4-corner homography warp, mesh warping, and test patterns |
| OutputNode | Terminal node — identity-pass FBO, drives recording |

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
  (one instance), **PaulStretch** (one instance), and **Drum Sequencer** (eight, one per lane). Lifted out of
  `SamplerNode.cpp` so a new sample-playing node never has to reimplement its
  own use-after-free trap.
- **`src/nodes/AudioPluginNode.h`/`.cpp`** — hosts a third-party plugin (Audio
  Units today) as an ordinary audio effect node. Unusually for this codebase it
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
- **`src/audio/PluginScanner.h`/`.cpp`** — `SampleScanner`'s thread +
  `PollResults()` + disk-cache shape, over a component-registry query instead
  of a directory walk, so it has no user-managed folder list. Backs the docked
  panel's fourth mode.
- **Every Objective-C object involved in plugin hosting lives behind
  `Platform.h`'s plugin section** — `src/nodes/` and `src/audio/` stay pure
  C++, and the audio thread never sends a message or touches ARC: the render
  path calls an `AURenderBlock` cached on the main thread at prepare time, with
  an `__unsafe_unretained` stack pull-input block. The plugin's editor is a
  plain `NSWindow` (the only one in the app), which works because
  `glfwPollEvents` drains and dispatches `NSApp`'s queue. AU is always
  supported; VST3 is a second backend behind the same surface, gated behind
  the `INFINITE_ENABLE_VST3` build option (off by default — the VST3 SDK is
  GPLv3-or-commercial and this codebase is MIT, see `LICENSE` and
  `docs/plans/audio/plugin-hosting.md`).
- Per-effect body/visualizer UI lives in `src/main.cpp` as `DrawXxxBody`/
  `DrawXxxVisualizer` pairs next to the `EffectVisualizerId` switch inside
  `DrawAudioNodeBody` — see `.claude/skills/new-audio-node/SKILL.md` for the
  exact wiring sites and `.claude/skills/audio-node-ui/SKILL.md` for the
  layout grammar.

### Invariants for `IGeometrySource`-consuming nodes

Three rules every node with a single `IGeometrySource*` input (or `sourceInput`/
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

If you add a new node type in this category, wire it into all three sweeps
rather than hand-writing a one-off check — see `geometry-transform-sweep`'s
SKILL.md, "Adding a new node type to a sweep."

**File:** `src/main.cpp` — registration, per-node UI, and node-graph wiring
(as opposed to the *rendering* of pins/links, which is Editor UI)

| Lines | What |
|---|---|
| 84-93 | `DisplayName` — display-name formatting for registered types |
| 534-635 | `RegisterNodes()` — registers every type with `NodeFactory` |
| 637-654 | `ModulatorForOutput`, `FindNodeByIndex` |
| 656-728 | `InputCountFor` — per-type input pin counts |
| 730-781 | `CableFor` — node/slot → `ImageCable` mapping |
| 783-878 | `ConnectGeometrySlot` — geometry/camera/light connection mapping |
| 879-913 | `ReloadDerivedState`, `CopyParams` |
| 914-2769 | **All `DrawXParams` functions** — one per node type's parameter panel (the bulk of the file; grep `DrawXxxParams` to jump to a specific node's UI) |
| 3262-3635 | `DisconnectLinkById`, `DisconnectAllTo`, `RemoveNodeByIndex` — node/link lifecycle |
| 5226-5275 | Drag-and-drop file → auto-spawn matching source node |
| 7500-7823 | Per-frame dispatch: routes each node to its `DrawXParams` (chrome around this dispatch is Editor UI, see below) |
| 7957-8148 | New-connection validation (`QueryNewLink`) — type-checks proposed links per node kind |
| 8396-8416 | Editor-initiated delete → `DisconnectLinkById`/`RemoveNodeByIndex` |

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
- `src/core/Transport.h/.cpp` — global clock (drives modulators, video playback)
- `src/core/BlendModes.h/.cpp` — shared blend-mode vocabulary + GLSL
- `src/core/FilterDefs.h/.cpp` — declarative filter-type table
- `src/core/GLUtil.h/.cpp` — FBO/shader-pass helpers
- `src/core/Mesh.h/.cpp` — mesh + matrix math for 3D nodes
- `src/core/Patch.h/.cpp` — patch file *format* (struct + read/write primitives; the save/load/undo *flows* that use it are App Features, below)

**File:** `src/main.cpp`

| Lines | What |
|---|---|
| 1-70 | Includes |
| 95-138 | `gNodes`/`gGroupMembers`/`gNextIndex`/`gEditor`, `LinkInfo`/`gLinks`/`FindLink` — core node/link registries |
| 4130-4213 | App bootstrap: GLFW/GL/ImGui init, HiDPI fonts, backend init, App Support dir, `ed::Config` |
| 4890-4964 | Main-loop top: poll events, `NewFrame()` |
| 7823-7941 | Per-frame link-table rebuild (image/geometry/modulator link data — rendering the resulting `ed::Link()` calls is Editor UI) |
| 8791-8818 | **Modulation/cook pipeline** — applies bound modulator values into params, then `CookIfNeeded(frameId)` per Output node (the core generative-evaluation tick; listed again under App Features since it's also the thing "play" triggers) |
| 9071-9146 | Render/present: `ImGui::Render()`, GL clear, swap buffers, frame limiter, `main()` exit |

---

## 3. Editor UI

The interactive canvas: rendering, layout, menus, widgets, theming, minimap,
groups, popups, keyboard shortcuts for interaction.

**File:** `src/main.cpp`

| Lines | What |
|---|---|
| 74-83 | Layout constants (`kPreviewSize`, `kViewportSize`, `kParamWidth`, `kPinRadius`, `kPinHit`) |
| 139-175 | Canvas/UI globals: grid snap, frame-timing display, target FPS/vsync, minimap globals, zoom sensitivity, pan/hover state |
| 176-210 | Deferred dropdown & color-picker popup infra (works around node-editor canvas-transform bug) |
| 211-232 | Drag-and-drop file globals + `OnFilesDropped` GLFW callback |
| 233-274 | `DropdownButton`, `ColorSwatch` — reusable param widget chrome |
| 276-403 | `ModSlider` / `BeginNodeParams` / `ModSliderInt` — the modulatable-slider widget used by nearly every node's param panel |
| 405-432 | `NodeSeparator` |
| 434-438 | `AlignOptions()` |
| 440-503 | `EyeToggle`, `BypassToggle` — hand-drawn param-visibility/bypass icons |
| 505-532 | `DrawPin` — generic pin-drawing helper |
| 1176-1236 | `DrawFxPad` — XY control-surface widget (Resynth) |
| 1388-1485 | `DrawCurveEditor` — interactive tone-curve widget |
| 2792-2884 | `DrawPaintablePreview`, `DrawCommentPreview`/`DrawCommentParams` |
| 2885-3092 | **Node group UI system**: `GroupOwning`, `PruneDeadGroups`, `AutoFitGroupToMembers`, `DrawGroupNode` |
| 3148-3261 | `DrawPreview` (image/3D thumbnail), `DrawModulatorMeter` (scope-style meter) |
| 3315-3509 | `DrawHelpWindow` — module reference modal |
| 3956-4069 | `DrawMinimap` — rendering, click/drag-to-navigate |
| 4154-4184 | ImGui context/HiDPI font setup, base style |
| 4980-5225 | Main menu bar: File/Edit/View menus, minimap settings, performance (FPS/vsync), transport buttons + BPM slider, trackpad wheel damping, canvas rect capture |
| 7500-7823 | Per-frame node chrome: pin layout, node body, preview dispatch, Eye/Bypass row, "mod" badge (the dispatch-*to*-param-panel logic itself is Node Library) |
| 7823-7941 | Rendering the rebuilt link table (`ed::Link()` calls, orange-tinted for modulation) |
| 8151-8449 | Keyboard shortcuts: Undo/Redo, Delete, Shift+D duplicate, Cmd+G group/ungroup, Cmd+C/V, drag-checkpoint capture, snap-on-release |
| 8461-8730 | Popup layer: minimap draw site, right-click node-spawner popup, deferred popups, docked node-browser side panel |
| 8734-8790 | Floating windows: GLSL Formula editor, Help window |
| 9095-9110 | Cmd+S/O/N global shortcuts (when no text field focused) |

---

## 4. App Features

Cross-cutting, document-level behavior: undo/redo, save/load, export,
clipboard, preferences, transport wiring.

**File:** `src/main.cpp`

| Lines | What |
|---|---|
| 3636-3762 | `BuildPatchData()` — serializes live graph to `Patch::Data` (shared by save + undo/redo) |
| 3763-3807 | `SavePatchTo`, `NewPatch()` |
| 3808-3895 | `ApplyPatchData`, `LoadPatchFrom` — restore graph from snapshot/disk |
| 3896-3955 | **Undo/redo**: `PushUndoCheckpoint`/`PushUndoSnapshot`, `Undo()`, `Redo()` |
| 4106-4126 | `SavePatchInteractive` — Save/Save-As dialog flow |
| 4206-4211 | Preferences persistence: `imgui.ini`, `Infinite.json` (node-editor layout) under `~/Library/Application Support/Infinite` |
| 4211, 5002-5018 | Recent-files list (`Patch::LoadRecents`/`NoteRecent`/`Recents()`) |
| 2770-2791 | `ExportPng` |
| 7776-7813 | Video recording controls (path, fps, include-audio, start/stop) — backs `OutputNode::StartRecording/StopRecording` |
| 8189-8222 | Shift+D duplicate |
| 8350-8394 | Copy/paste clipboard (Cmd+C/Cmd+V) |
| 4956-4966, 5103-5122 | Transport wiring: per-frame `Tick`, Play/Pause/Rewind, BPM slider (buttons themselves are Editor UI; the `Tick()` call and what it drives is the feature) |
| 8791-8818 | Modulation/cook pipeline (see also Engine Core — this is the same code, listed here because "press Play" is the user-facing feature it powers) |
| 5075-5077, 9115-9124 | Vsync toggle, target-FPS frame limiter |

---

## 5. Platform Layer

macOS-native shims kept out of the main C++ translation units.

**Files:** `src/platform/Platform.h`, `src/platform/Platform.mm`
- Native open-file dialog (image-filtered)
- Image decode via ImageIO (any OS-supported format → RGBA8)
- 3D model loading via ModelIO (OBJ/PLY/STL/USD/USDZ)

---

## 6. Dev/Test Harness (not product code)

**File:** `src/main.cpp`, lines **4225-4834** and **5277-7500** (~2,000-2,500
lines interleaved with real UI/node code)

Automated regression/visual-verification tests gated behind
`getenv("INFINITE_*")` / `getenv("IMAGERESYNTH_*")` env vars (e.g.
`COLORTEST`, `UNDOTEST`, `PATCHTEST`, `MACROTEST`, `SHOWCASE`, ...). They drive
synthetic frames and `printf` pass/fail verdicts. Ignore these unless the task
is specifically about the self-test harness itself.

---

## Quick lookup by task type

- **"Add a new node type" / "fix a node's parameters or math"** → Node Library
- **"Fix how connections/pins/the graph model work"** → Engine / Runtime Core
- **"Change how X looks/behaves in the editor UI"** → Editor UI
- **"Undo isn't working" / "add export format" / "fix save/load"** → App Features
- **"Fix file dialogs / model import on macOS"** → Platform Layer
- **"Something in the self-test harness"** → Dev/Test Harness
