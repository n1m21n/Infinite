# Plugin hosting

Status: **Phase 1 (Audio Units) shipped.** Phase 2 (VST3) is blocked on a
licensing decision, not on engineering effort. VST2 is permanently out of scope.

Related: `src/platform/Platform.h`'s plugin section (the whole API surface),
`src/nodes/AudioPluginNode.h` (the node and its two load-bearing invariants),
`src/audio/PluginScanner.h`, and `docs/plans/audio/README.md` P3f.

---

## 0. Why only AU today

The original ask was "VST / VST3 / AU". Only AU is buildable without a decision
that isn't an engineer's to make.

| Format | Status | Why |
|---|---|---|
| **AU** (v2 and v3) | shipped | Hostable with zero new third-party code. `AudioToolbox`/`AVFoundation`/`CoreAudio` were already linked; only `CoreAudioKit` was added, for the editor view controller. `AVAudioUnitComponentManager` enumerates, `AUAudioUnit` instantiates and renders, `AUParameterTree` supplies the parameter list and the "which control did the user just touch" observer that drives configure mode. Covers most macOS plugins. |
| **VST3** | phase 2, **blocked** | Needs the Steinberg VST3 SDK, which is dual-licensed **GPLv3 or a proprietary Steinberg agreement**. Infinite is MIT, and this codebase has an explicit clean-room rule about not mixing in GPL code (`.claude/skills/new-audio-node/SKILL.md` §0.1). The SDK must not be vendored — and neither must JUCE, for the same reason — until the licensing route is chosen. |
| **VST2** | **permanently out of scope** | Steinberg stopped issuing VST2 SDK licences in October 2018 and the SDK is no longer distributable. Do not build it. |

Everything below the node — the scanner entry, the node's saved identity, the
patch format — carries a `format` string from day one so a second backend needs
no format change. Today it is always `"au"`.

## 1. What phase 2 (VST3) would touch

If and when the licensing question is answered, VST3 should reuse all of
phase 1. In rough order:

- `PluginScanner` gains a folder list (the standard
  `/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/VST3` roots plus
  user-added ones) alongside the registry query. This is the one place the
  "AU needs no folders" simplification has to be undone; the machinery to copy
  is `SampleScanner`'s.
- `PluginDesc.format` becomes `"vst3"`, and its `identifier` becomes whatever
  is stable for a VST3 (the class UID, not a path — same reasoning as AU's
  four-char-code triple).
- The `Platform::Plugin*` functions gain a second backend behind the same
  signatures.
- `PluginScanner::kIndexSchemaVersion` bumps, which discards the old cache
  rather than migrating it.

**No node, panel, mapping or patch-format change should be needed.** If phase 2
finds itself needing one, the phase 1 abstraction was drawn in the wrong place
and that is the thing to fix, not to work around.

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
