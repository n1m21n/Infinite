# Plugin hosting

Status: **Phase 1 (Audio Units) and Phase 2 (VST3) shipped.** Phase 2 is built via the optional `INFINITE_ENABLE_VST3=ON` CMake flag (GPLv3). VST2 is permanently out of scope.

Related: `src/platform/Platform.h`'s plugin section (the whole API surface),
`src/nodes/AudioPluginNode.h` (the node and its two load-bearing invariants),
`src/audio/PluginScanner.h`, and `docs/plans/audio/README.md` P3f.

---

## 0. Supported Formats

| Format | Status | Why / Notes |
|---|---|---|
| **AU** (v2 and v3) | shipped | Hostable with zero third-party dependencies. `AudioToolbox`/`AVFoundation`/`CoreAudio`/`CoreAudioKit` are system frameworks. `AVAudioUnitComponentManager` enumerates, `AUAudioUnit` instantiates and renders, `AUParameterTree` supplies the parameter list and the learn observer. Pure MIT build. |
| **VST3** | shipped (optional) | Built when `-DINFINITE_ENABLE_VST3=ON`. The Steinberg VST3 SDK is dual-licensed **GPLv3 or proprietary Steinberg agreement**. Infinite's source remains MIT; distributing a binary compiled with VST3 enabled links the GPLv3 VST3 SDK and must be distributed under GPLv3. The SDK is fetched via submodule / external directory. |
| **VST2** | **permanently out of scope** | Steinberg stopped issuing VST2 SDK licences in October 2018 and the SDK is no longer distributable. Do not build it. |

Everything below the node — the scanner entry, the node's saved identity, the
patch format — carries a `format` string ("au" or "vst3").

## 1. Phase 2 (VST3) architecture

VST3 reuses all of phase 1's node and UI architecture:

- `PluginScanner` has a folder list (the standard
  `/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/VST3` roots plus
  user-added ones) alongside the AU registry query, with persistence in `PluginFolders.json`.
- `PluginDesc.format` carries `"vst3"`, and its `identifier` is the stable class UID (`vst3:<hex>`).
- The `Platform::Plugin*` functions dispatch to `src/platform/PluginVST3.mm` behind the exact same signatures.
- `PluginScanner::kIndexSchemaVersion` is bumped to 3, cleanly refreshing stale caches.

**No node, panel, mapping or patch-format change was needed.** The phase 1 abstraction cleanly accommodates both backends.

## 2. Phase 3 — what phase 1 deliberately does not do

The node is an audio-in / audio-out effect. Not implemented:

- **Instrument plugins (`aumu`) and MIDI/note input.** `EnumerateAudioUnits`
  filters to `aufx` (effect) and `aumf` (music effect) and deliberately
  excludes `aumu`: listing a synth that can only ever render silence, because
  nothing can play it, is worse than not listing it. Phase 3 adds a note input
  pin and `scheduleMIDIEventBlock`, and drops the filter at the same time.
- **Multi-out plugins and sidechain inputs.** One input bus, one output bus.
- **Plugin latency compensation.** `AUAudioUnit.latency` is not read and
  nothing downstream compensates for it.
- **Presets beyond raw `fullState`.** A patch stores the plugin's whole state
  blob; there is no preset browser, and `currentPreset` / factory presets are
  not surfaced.

## 3. Two things worth knowing before touching this code

**The editor window is the app's only `NSWindow`.** Infinite is GLFW + ImGui +
OpenGL and had no `NSWindow` anywhere in `src/` before this. It works because
`glfwPollEvents` drains and dispatches `NSApp`'s event queue, which was
verified with a throwaway empty-window test — an `NSWindow` created alongside
the GLFW window received `drawRect:`, a posted mouse event, and
`windowWillClose:` with `glfwPollEvents` as the only pump — before anything was
built on top of it. If that ever stops holding, the symptom will be a plugin
editor that draws once and then ignores the mouse.

**The UI font is loaded with no glyph range** (`AddFontFromFileTTF(path, size)`
in `main.cpp`), so only Basic Latin is guaranteed. The open-editor button says
`open` / `close` rather than carrying an icon glyph for that reason; a Unicode
arrow renders as a literal `?`.
