# Prompt: Audio Unit (and later VST3) plugin hosting in Infinite

Paste everything below the line into a fresh Claude Code session.

---

Implement **plugin hosting** in Infinite (`/Users/namansoni/infinte`): a
scanned plugin library in the docked search panel, a `Plugin` audio node that
loads a plugin and exposes its audio out, a **separate native window** for the
plugin's own editor UI, and an Ableton-style "configure" list of mapped
plugin parameters drawn as horizontal sliders on the node body, which grows as
the user maps more.

This is a multi-session feature. Do **Phase 1 (Audio Units)** completely and
stop. Phases 2/3 are scoped at the bottom and must not be started without the
user explicitly asking.

## 0. Format decision — read this before anything else

The request was "VST/VST3/AU". After investigating, only AU is buildable today
without a licensing decision from the user:

- **AU (v2 and v3)** — hostable with zero new third-party code.
  `AudioToolbox`, `AVFoundation`, `CoreAudio` are already linked
  (`CMakeLists.txt:161-179`). `AVAudioUnitComponentManager` enumerates,
  `AUAudioUnit` instantiates and renders, `AUParameterTree` gives the
  parameter list and the "which control did the user just touch" observer.
  This is Phase 1 and covers most macOS plugins.
- **VST3** — needs the Steinberg VST3 SDK, which is dual-licensed **GPLv3 or a
  proprietary Steinberg agreement**. Infinite is MIT and the codebase has an
  explicit clean-room rule about not mixing in GPL code
  (`.claude/skills/new-audio-node/SKILL.md` §0.1). **Do not vendor the VST3
  SDK or JUCE.** Phase 2 below; the user has to make the licensing call first.
- **VST2** — Steinberg stopped issuing VST2 SDK licences in October 2018 and
  the SDK is no longer distributable. Treat "VST2 support" as **out of scope
  permanently** unless the user says otherwise. Do not build it.

Design every layer (scanner entry, node, param mapping, patch format) so a
second plugin format slots in later: put a `format` field on the scanner entry
and on the node's saved identity from day one, even though Phase 1 only ever
writes `"au"`.

## 1. Five invariants that override anything you infer

These are from `.claude/skills/new-audio-node/SKILL.md` §0. Read that skill and
`.claude/skills/audio-node-ui/SKILL.md` in full before writing code; both are
prescriptive.

1. **Clean room.** Do not open, read, grep or reference
   `/Users/namansoni/BespokeSynth`, JUCE, or any GPL host's source.
2. **The two-object rule.** A node is an `INode` on the main thread that
   *owns* an `AudioNode` on the audio thread. They talk only through
   `ParamMailbox` (main→audio) and `MeterRing` (audio→main).
3. **`CookIfNeeded` does no DSP.** Drain meters, push dirty params, < 5 µs.
4. **Audio-thread prohibitions** inside `ProcessBlock` and anything it calls:
   no allocation, no locks, no `dynamic_cast`, no `std::function`/`map`/
   `string`, no GL, no ImGui, no file I/O, no `printf`. **And, new for this
   feature: no Objective-C message sends and no ARC retain/release.** Cache the
   plugin's `AUAudioUnitRenderBlock` into a strong ivar at prepare time on the
   main thread and *call the block* from `ProcessBlock`; never message the
   `AUAudioUnit` from the render thread.
5. **Minimalism.** The node's own controls (the ones that aren't mapped plugin
   params) must stay tiny: plugin name, an open-editor button, a bypass, a mix
   or output trim at most. Everything else on the card is mapped plugin params.

## 2. What the codebase already gives you (verified, don't re-derive)

`src/main.cpp` is 27,435 lines. Line numbers below were checked against the
current working tree.

| Thing | Where |
|---|---|
| Background library scanner, thread + disk-cache pattern | `src/audio/SampleScanner.h` / `.cpp` |
| Scanner's settings dir, JSON persistence, per-test dir override | `src/audio/SampleScanner.cpp:20-59` |
| Search-panel globals, drag state | `src/main.cpp:339-369` |
| `gSearchPanelMode` (0 Modules / 1 Samples / 2 Media) | `src/main.cpp:353` |
| Panel mode tab strip | `src/main.cpp:26543-26551`, dispatch at `26553` / `26611` |
| `DrawLibrarySearchPanel` (folders, refresh, filter, drag rows) | `src/main.cpp:6000-6116` |
| Panel-drag → canvas release handler | `src/main.cpp:26075-26179` |
| OS Finder drop → spawn node | `OnFilesDropped` `src/main.cpp:676`, consumed at `19960-20030` |
| Node registration | `RegisterNodes()`, `src/main.cpp:2142` (Sampler/Drum Sequencer nearby) |
| Audio body dispatch ladder | `DrawAudioNodeBody`, `src/main.cpp:9378` |
| Node help table | `src/main.cpp:~11955` (Sampler's entry at `11959`) |
| Modulatable horizontal slider (this is the row widget you want) | `ModSlider`, `src/main.cpp:936`; audio-styled variant `AudioSliderFloat`, `867` |
| Node body widths | `kAudioNodeWidth=440`, `kAudioNarrowWidth=200`, `kAudioWideWidth=960`, `src/main.cpp:171-189` |
| Patch param serialization (`Text` is one line, backslash/newline escaped, no length cap) | `src/core/Patch.cpp:96-106, 141-150` |
| Variable-count params via indexed names — the precedent to copy | `DrumSequencerNode::VisitParams`, `src/nodes/DrumSequencerNode.cpp:619-659` |
| Post-load hook to rebuild non-serialized state | `ReloadDerivedState`, `src/main.cpp:2978` |
| Audio device / render callback | `Platform::AudioDeviceOpen`, `src/platform/Platform.mm:2146` (AVAudioSourceNode render block → `AudioEngine::Process`) |
| Topology publish + one-generation retire discipline | `AudioEngine::SetTopology`, `src/audio/AudioEngine.h:75` |
| Block/channel ceilings | `kAudioMaxBlockFrames=4096`, `kAudioMaxChannels=8`, `kAudioMaxNodeInputs=8`, `src/audio/AudioEngine.h:19-21` |
| Generic sweeps that will pick the node up automatically | `src/main.cpp:13525-13604`, `16564` |
| Hygiene known-baseline mechanism | `.claude/skills/run-infinite-hygiene/driver.sh:171-210` |

Two facts worth internalizing:

- **There is no `NSWindow` anywhere in `src/` today.** The plugin editor window
  is genuinely new platform surface. The app is GLFW + ImGui + OpenGL;
  `glfwPollEvents` drains the `NSApp` event queue, so a plain `NSWindow` will
  receive events — but **verify this with a throwaway empty-window smoke test
  before building anything on top of it**, and report what you find. If the
  window turns out not to receive events, say so and stop rather than
  inventing a workaround.
- **The UI font is loaded with `AddFontFromFileTTF(path, size)` and no glyph
  range** (`src/main.cpp:17271`), so only Basic Latin is guaranteed. A
  non-ASCII "open in new window" icon glyph will render as `?`. Use an ASCII
  label (`open`, `[ ]`, `>`) or add an explicit glyph range — don't just paste
  a Unicode arrow and assume it renders.

## 3. Phase 1 work items

### 3.1 Platform layer — all Objective-C++ lives here

Add a plugin-hosting section to `src/platform/Platform.h` (pure C++ facade,
opaque handles) implemented in `src/platform/Platform.mm`. This is the existing
architecture's split and it keeps `src/nodes/` and `src/audio/` free of ObjC.
Add `-framework CoreAudioKit` to `CMakeLists.txt:161-179` (needed for the AUv2
generic view fallback); `AVFoundation`/`AudioToolbox` are already there.

Surface, roughly:

- `struct PluginDesc { std::string format, name, manufacturer, identifier; }`
  where `identifier` for AU is a stable string encoding of the
  type/subtype/manufacturer four-char codes plus version — this is what the
  patch file stores, not a filesystem path (an AU's path can move; its
  component description can't).
- `EnumerateAudioUnits(std::vector<PluginDesc>&)` — via
  `[AVAudioUnitComponentManager sharedAudioUnitComponentManager]
  componentsMatchingDescription:` with a wildcard description. Filter to
  effect/instrument/music-effect types (`aufx`, `aumf`, `aumu`) and skip
  Apple's output/converter units.
- `PluginHandle* PluginInstantiate(const PluginDesc&, double sampleRate, int maxBlockFrames, std::string& outError)`.
  `AUAudioUnit` instantiation is **asynchronous** (`instantiateWithComponentDescription:options:completionHandler:`).
  Do not block the UI thread on it — either drive it as a pending state the
  node polls in `CookIfNeeded`, or wait on it off the main thread; whichever
  you pick, the app must stay responsive while a slow plugin loads, and a
  failed load must surface as node status text, not a crash.
- `PluginDestroy(PluginHandle*)` — main thread only, and never while the
  handle is reachable from a published topology (see 3.5).
- `PluginRender(PluginHandle*, const float* const* in, int inChannels, float** out, int outChannels, int numFrames)`
  — **real-time safe**, calls the cached render block with an
  `AURenderPullInputBlock` that copies from `in`. No ObjC messaging, no
  allocation.
- `PluginParameterCount/PluginParameterInfo(index, ...)` returning
  `{ uint64 address, std::string displayName, float min, max, defaultValue, std::string unit }`
  from the `AUParameterTree`'s flattened `allParameters`.
- `PluginSetParameter(handle, address, float)` / `PluginGetParameter`.
  `AUParameter setValue:` is safe to call from the main thread and is
  non-blocking; call it from `CookIfNeeded`, not from `ProcessBlock`.
- **Learn hook:** `PluginBeginLearn(handle)` / `PluginPollLearned(handle, uint64& outAddress)`.
  Implement with `[parameterTree tokenByAddingParameterObserver:]`, which fires
  when the plugin's *own* editor changes a parameter — this is exactly how
  Ableton's Configure mode works. **The observer block is called on an
  arbitrary thread**, so it must do nothing but store the address into a
  `std::atomic<uint64_t>` + flag that the main thread polls. Remove the
  observer token when learn mode ends and in the destructor.
- `PluginOpenEditor(handle)` / `PluginCloseEditor(handle)` / `PluginEditorIsOpen(handle)`.
  Use `[auAudioUnit requestViewControllerWithCompletionHandler:]`; if it yields
  nil (common for older AUv2s), fall back to CoreAudioKit's generic view. Host
  the resulting `NSView` in a new borderless-titled `NSWindow`, sized to the
  view's `fittingSize`, titled with the plugin name. Closing the window must
  route back through `PluginCloseEditor` so the node's open/closed state stays
  truthful, and destroying the plugin must close the window first.
- `PluginSaveState(handle, std::string& outBase64)` / `PluginRestoreState(handle, const std::string& base64)`
  wrapping `AUAudioUnit.fullState` (an `NSDictionary`, archive it with
  `NSKeyedArchiver` then base64 it — `Patch.cpp`'s `Text` is a single escaped
  line with no length cap, so base64 round-trips fine).

### 3.2 `PluginScanner` — `src/audio/PluginScanner.h` / `.cpp`

Model it on `SampleScanner` (same `std::thread` + mutex + `PollResults()`
try_lock + atomic-flags shape, same "load from disk shows the index instantly,
a scan only ever happens from an explicit `StartScan()`" contract — that is
exactly the "no rescan every launch" the user asked for).

Differences from `SampleScanner`, deliberate:

- **No user-managed folder list for AU.** AU discovery is a registry query, not
  a directory walk. The panel gets a single **Rescan** button, not
  Add-folder/per-folder-refresh. Keep the folder machinery out of Phase 1;
  Phase 2 (VST3) is what needs a folder list, and it should be added then.
- Persist to `~/Library/Application Support/Infinite/PluginIndex.json`, reusing
  `SampleScanner.cpp:20-42`'s `SettingsDir()` logic — and **copy its
  test-directory override pattern**: if a plugin-drag hygiene test env var is
  set, redirect to a throwaway subdirectory so a test run can't clobber the
  user's real index.
- Entry: `{ format, name, manufacturer, identifier }`. Store a schema/version
  int in the JSON and discard a cache whose version doesn't match, so a later
  format change doesn't have to hand-migrate.

### 3.3 Search panel — a fourth mode

- Extend `gSearchPanelMode` (`src/main.cpp:353`) with `3 = Plugins` and add a
  fourth tab at `26543-26551`. Four tabs no longer fit at
  `kNodePanelWidth * 0.30f` each — re-split the row width; check it visually,
  don't assume.
- Write `DrawPluginSearchPanel()` next to `DrawLibrarySearchPanel`
  (`main.cpp:6000`). Same shape: Rescan button (disabled + "scanning… (N
  found)" while in flight), filter-as-you-type over the cached index, one
  `Selectable` per result showing `name — manufacturer`, tooltip with the
  format and identifier. Give it its own static search buffer, like the
  Samples/Media modes each have (`main.cpp:6065-6067`).
- Drag: reuse the manual drag mechanism, don't invent a second one. The
  existing globals are a two-valued `gSampleDragIsMedia` bool
  (`main.cpp:348`); **replace that bool with a small enum**
  (`Sample | Media | Plugin`) and update its three read sites
  (`26098`, `26134`, and the drag-start at `6106`) rather than adding a
  parallel `gPluginDragActive` flag.
- Release handling in the `26075` block: a plugin drag dropped **onto an
  existing Plugin node** swaps that node's plugin (preserving nothing —
  different plugin, different state); dropped **on empty canvas** spawns a
  Plugin node there already loading that plugin. Both take a
  `PushUndoCheckpoint()` first and set `gPatchDirty`, matching the Sampler
  branch exactly.

### 3.4 Finder drop

In the `gDroppedFiles` consumer (`main.cpp:19960`), add a branch for `.component`
and `.vst3` paths **before** the existing extension branches. Note these are
*bundle directories*, not files — GLFW hands you the directory path and
`HasExtension` on it works, but anything that stats it as a regular file won't.
For a dropped `.component`, resolve it back to a component description (match
against the scanner index by bundle path, or read the bundle's `Info.plist`
`AudioComponents` array) and spawn a loaded Plugin node. If it can't be
resolved, spawn nothing and log — do not spawn an empty node silently.

### 3.5 The node — `src/nodes/AudioPluginNode.h` / `.cpp`

Shape: **audio source / effect** — `INode` + `IAudioSource`, one audio input at
slot 0 (`AudioInputSlot(0)`, `InputLabel(0) = "in"`), audio out. Add to
`CMakeLists.txt` (~line 120, with the other `src/nodes/*.cpp`), `#include` in
`main.cpp` (~line 92), and register as
`REGISTER_NODE(AudioPluginNode, Plugin, "AudioEffects")` near `main.cpp:2142`.
Check `NodeFactory::DuplicateNames()` doesn't fire on `Plugin`.

Two objects, as always: `AudioPluginNode` (main thread) owns
`AudioPluginAudioNode` (audio thread). The audio half holds a
`std::atomic<Platform::PluginHandle*>`; the main half publishes a prepared
handle into it and **never deletes the previous one immediately** — carry the
same one-generation retire discipline `AudioEngine::SetTopology`
(`AudioEngine.h:75`) uses: retire on swap, destroy on the *next* swap, so no
in-flight callback can be holding it. Getting this wrong is a use-after-free in
the render thread, which is the single highest-risk part of this feature.

`ProcessBlock` is then: read the atomic handle once; if null, copy input to
output (a not-yet-loaded plugin passes audio through rather than muting the
chain); otherwise call `Platform::PluginRender`. Nothing else.

`PrepareToPlay(sampleRate, maxBlockSize)` must reach the plugin's
`maximumFramesToRender` / `allocateRenderResourcesAndReturnError:` — which is
main-thread work, so it can't happen inside `PrepareToPlay` if that's called on
the audio thread; check which thread the topology builder calls it on
(`RebuildAudioTopology` in `main.cpp`) and route accordingly. Cap at
`kAudioMaxBlockFrames`.

### 3.6 Parameter mapping — the Ableton-style configure list

This is the part in the user's sketch: below the header, a grid of horizontal
sliders, two per row, extending as more get mapped.

- Fixed ceiling of **32** mapping slots (`kMaxMappedParams`). `ParamMailbox`
  only has 64 slots total (`ParamMailbox.h:23`) and node bodies stop being
  usable well before 32 sliders anyway.
- Per slot store: `bool assigned`, `uint64 address` (as two `int`s or a
  decimal string — `ParamVisitor` has no 64-bit type, see `INode.h:67-76`),
  `std::string displayName`, `float min`, `max`, `float value`.
- Serialize with `DrumSequencerNode::VisitParams`' indexed-name pattern
  (`DrumSequencerNode.cpp:619-659`): `map0_assigned`, `map0_addr`,
  `map0_name`, `map0_min`, `map0_max`, `map0_value`, … plus the plugin
  identity (`plugin_format`, `plugin_id`, `plugin_name`) and
  `plugin_state` (base64 `fullState`).
- **Draw each assigned slot with `ModSlider(name, &value, min, max, "%.3f", width, /*audioStyle=*/true)`**
  (`main.cpp:936`). That gives you the horizontal slider *and* a modulation
  input pin per row for free — which is what makes these "params I can
  modulate", not just display widgets. Two columns at `kAudioNodeWidth` (440).
- **Trap: modulation bindings are keyed by `(nodeIndex, paramIndex)` and
  `paramIndex` is the per-frame `ModSlider` call order** (`gParamCounter`,
  `main.cpp:711-712`). If unmapping a middle slot compacted the list, every
  later row's pin would silently repoint to a different parameter. So:
  **slots are positional and never compacted.** Unmapping leaves the row in
  place showing "unassigned" with a click-to-learn affordance; only trailing
  unassigned rows are hidden. Note that even hiding trailing rows is safe only
  because nothing follows them — do not add any other `ModSlider` call after
  the mapping grid in this node's body.
- **Learn flow:** a "configure" toggle on the node header. While on, call
  `Platform::PluginBeginLearn`, poll `PluginPollLearned` each `CookIfNeeded`,
  and when an address comes back, fill the first unassigned slot with that
  parameter's info (pull name/min/max via `PluginParameterInfo`) and push an
  undo checkpoint. Also offer a plain dropdown of all plugin parameters as a
  fallback — many plugins' editors don't notify on every control, and a user
  with no working learn has no way to map anything otherwise.
- **Value flow, both directions.** `CookIfNeeded` pushes any slot whose value
  changed this frame to `Platform::PluginSetParameter` (main thread, cheap,
  no mailbox involved — the plugin does its own smoothing). It also reads back
  the current value for slots the user isn't dragging, so turning a knob in the
  plugin's own window moves the node's slider. Rate-limit the read-back
  (every N frames) rather than polling all 32 every frame.
- **This node will fail `AUDIOPARAMSWEEPTEST`**, because the sweep checks that
  every `VisitParams` param reaches the audio thread through the node's
  `ParamMailbox` within a block, and mapped plugin params deliberately don't go
  through the mailbox — they go to `AUParameter`. Do not distort the design to
  satisfy the sweep. Instead add a documented known-baseline line to
  `.claude/skills/run-infinite-hygiene/driver.sh` alongside the existing
  Sampler / Drum Sequencer / Note Router entries (`driver.sh:171-210`),
  explaining *why* it's expected. `AUDIOTEARDOWNSWEEPTEST` must still pass
  cleanly — that one is load-bearing here, since it exercises exactly the
  delete-mid-playback path that 3.5's retire discipline exists for.

### 3.7 Body layout and the open-editor button

Read `.claude/skills/audio-node-ui/SKILL.md` before writing `DrawPluginBody`.
Add its branch to `DrawAudioNodeBody` (`main.cpp:9378`) — that ladder is the
only dispatch site an audio node still needs.

Header row: plugin name (or "no plugin — drag one from the Plugins panel"), an
**open-editor button** (ASCII label, see §2's font note) that toggles the
native window, a bypass toggle, and a configure toggle. Readout strip: plugin
name + format + `N params mapped`, or the load error. Then the two-column
mapping grid.

### 3.8 Docs, help, tests

- One-sentence entry in the node help table (`main.cpp:~11955`), in the
  existing voice — what it does plus the one non-obvious thing (that the
  sliders are mapped by clicking a control in the plugin's own window while
  configure is on).
- Update `ARCHITECTURE.md`'s audio section and `docs/plans/audio/README.md` §3
  to list the node as shipped, and `README.md` where the audio node system is
  described.
- Add an `INFINITE_PLUGINSCANTEST` fixture in the `main.cpp` test style
  (a `getenv` fixture printing a line ending `OK` / containing `FAIL`):
  enumerate AUs headlessly, assert a non-zero count on a normal macOS install
  (Apple ships `AUDelay`/`AUMatrixReverb` etc.), instantiate one Apple AU,
  render a block of known input through it and assert the output isn't
  identical-to-input *and* isn't silence, read its parameter list, set a
  parameter and read it back, save and restore `fullState`, then destroy.
  This is the fixture that proves the whole platform layer works without a UI.
- Add an `INFINITE_PLUGINDRAGTEST` in the shape of `INFINITE_SAMPLERDRAGTEST`
  (`main.cpp:17551-17596` for the setup pattern, `6086-6097` for the row-rect
  capture) if the drag path isn't otherwise covered.

## 4. Exit criteria — report each one explicitly

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` compiles clean.
2. Plugins tab lists real AUs; quitting and relaunching shows the same list
   with **no scan** (this is the user's explicit requirement — verify by
   watching for the scanning indicator on a cold launch, not by assuming).
3. Dragging a plugin from the panel onto empty canvas spawns a loaded Plugin
   node; dropping onto an existing one swaps it; dropping a `.component` from
   Finder does the same.
4. Wired `Oscillator → Plugin → Audio Out`, the plugin audibly processes.
   Unloaded/bypassed, it passes audio through unchanged.
5. The open button opens the plugin's real editor in its own window; closing
   the window updates the node; deleting the node closes the window.
6. With configure on, touching a control in the plugin's window adds a mapped
   row; the row's slider moves the plugin's control and vice versa; a
   modulator cable into the row's pin drives it.
7. Params, mapping list and plugin state survive save → load → undo →
   copy/paste unchanged.
8. Deleting the node mid-playback does not crash and logs zero xruns.
9. `INFINITE_PLUGINSCANTEST` prints `OK`. `/run-infinite-hygiene` passes with
   only the documented new baseline line added.
10. Build copied to `~/Desktop/Infinite.app`.

## 5. Explicitly out of scope for this session

- VST2 — permanently, see §0.
- **Phase 2, VST3** — needs the licensing decision in §0 first. When it
  happens it reuses everything above: `PluginScanner` gains a folder list plus
  the standard `/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/VST3`
  roots, `PluginDesc.format` becomes `"vst3"`, and the `Platform::Plugin*`
  functions gain a second backend. No node, panel, mapping or patch-format
  change should be needed — if you find yourself needing one, the Phase 1
  abstraction was drawn in the wrong place.
- **Phase 3** — MIDI/note input to instrument plugins (`aumu`), multi-out
  plugins, sidechain inputs, plugin latency compensation, and plugin presets
  beyond raw `fullState`. Phase 1's node is an audio-in/audio-out effect only.
- Do not touch the Samples or Media scanner behaviour beyond the
  `gSampleDragIsMedia` → enum change in §3.3.
