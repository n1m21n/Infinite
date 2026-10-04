# Infinite-Turbo: to-do

Next revision first, then the rest in rough order. Keep each item short; move it to
CHANGELOG-TURBO.md when it ships.

## Next revision

- (empty: the side panel folders shipped in 0.49)

## Later

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
