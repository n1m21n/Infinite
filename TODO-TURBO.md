# Infinite-Turbo: to-do

Next revision first, then the rest in rough order. Keep each item short; move it to
CHANGELOG-TURBO.md when it ships.

## Next revision

- Collect files: "Save with media" (Save As option) copies every file the patch uses (samples, drum kits and MIDI, video / images / 3D models, plugin presets, LUTs, fonts) into a folder next to the .inf (e.g. <name>_media/ by type), rewrites the paths to relative ones, skips duplicates by content, and shows a report of missing files. Needs: one central list of path params per node, relative path resolution on load, and a "missing files" relink dialog.

## Later

- TOP parity, what 0.52 left open (docs/TOP-PARITY.md): Common page on sources, Layer Stack and
  Switcher; pixel units for Crop / Transform; Blend with N inputs; the missing TOPs (Cache / Time
  Machine, Tile, Remap, Corner Pin, Constant, Math, Channel Mix, Luma Blur, Optical Flow...);
  Send / Receive partner lines and wireless links in the MCP graph. Then stage 2: 3D with POPs.

- Timeline: MIDI clips and a piano-roll editor (data model, playback into an instrument's note
  input, editor, .mid import per track). Estimate: 3-4k lines, 1-2 focused weeks.
- Headless engine from upstream: --render / --frame / --frames-dir / --node / --stems / --notes /
  --set / --audio-summary / --contact-sheet / --explain / --describe / --validate, plus the
  console launcher for cmd exit codes.
- Prediction nodes from upstream: Moves (MovementLog / MovementStats), Predictive Modulator,
  Predictive Coloring.

- Geometry: upstream's sprite fill clamp (GPU driver-reset guard) and cloud forwarding through
  Material / Null3D / Mapping / Cloth.

## Out of scope (for now)

- Field language (6 Field nodes).
- macOS / Linux code, upstream's own Windows platform layer (Turbo uses JUCE).
