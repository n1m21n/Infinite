# Infinite-Turbo: to-do

Next revision first, then the rest in rough order. Keep each item short; move it to
CHANGELOG-TURBO.md when it ships.

## Next revision

- Collect files: "Save with media" (Save As option) copies every file the patch uses (samples, drum kits and MIDI, video / images / 3D models, plugin presets, LUTs, fonts) into a folder next to the .inf (e.g. <name>_media/ by type), rewrites the paths to relative ones, skips duplicates by content, and shows a report of missing files. Needs: one central list of path params per node, relative path resolution on load, and a "missing files" relink dialog.

## Later

- Formula: colour (vec3) and toggle uniforms as knobs; Shadertoy keyboard channel from MIDI notes;
  cubemap passes (render to six faces) and a samplerCube from an HDRI.

- Full build, next GPL modules: DUCK sidechain; Surge XT effects (spring reverb, Nimbus); frei0r video
  filters. A live SLOOP drum synth node (the kits are already in as rendered samples).

- Video In: format dropdown with the device's real mode list (resolution x fps x subtype), as the
  log already prints it.
- Zero-copy GPU video: D3D11VA frames shared with GL (WGL_NV_DX_interop2) instead of the
  download/upload round trip; DXV decoded to BC blocks on the GPU like HAP.
- main.cpp split, step two: turn the src/app fragments into separate .cpp files (shared state in
  headers) for faster incremental builds; then the frame steps into functions.
- Packaging in plain cmd: package-windows.bat still calls scripts/package-windows.ps1.

- TOP parity, what 0.52 left open (docs/TOP-PARITY.md): Common page on sources, Layer Stack and
  Switcher; pixel units for Crop / Transform; Blend with N inputs; the missing TOPs (Cache / Time
  Machine and Optical Flow arrived in 0.60; Tile, Remap, Corner Pin, Constant, Math, Channel Mix,
  Luma Blur);
  Send / Receive partner lines and wireless links in the MCP graph. Then stage 2: 3D with POPs.

- Timeline: MIDI clips and a piano-roll editor (data model, playback into an instrument's note
  input, editor, .mid import per track). Estimate: 3-4k lines, 1-2 focused weeks.
- Headless engine from upstream: --render / --frame / --frames-dir / --node / --stems / --notes /
  --set / --audio-summary / --contact-sheet / --explain / --describe / --validate, plus the
  console launcher for cmd exit codes.
- Upstream gesture learning: Moves and Predictive Modulator need upstream's MovementLog (binding
  callbacks and per-param tapers in Modulation, the CookFrame hooks) and its IPredictor Drift.
- Upstream audio-engine resync: per-voice modulation (VoiceMod) in Analog / Wavetable, NoteEvent
  MPE fields (pressure / slide per note), the newer synth cores (warp modes, voice lanes). Until
  then Voice Spread and Note Expression act as one shared value, and LFO / Random / MSEG "per voice"
  is not offered.
- Upstream UI parity for modulators: FixedParamOrdinal / RegisterHiddenParam (stable slider
  ordinals per mode), skewed knob tapers (Turbo draws them linear), the "show more" folds.

## Out of scope (for now)

- Field language (6 Field nodes).
- macOS / Linux code, upstream's own Windows platform layer (Turbo uses JUCE).
