## What's new in v0.5.0

### A cleaner, more consistent interface

- Every node body follows one layout: parameter names sit inside the control, sliders and button rows span the full body width, values carry their units (Hz, ms, dB), and each node has one status line in the same style
- Empty nodes say what they need: Image, Video, Video In, Syphon, NDI, Slideshow and sequencers show a centred hint or caption instead of a blank preview
- Camera, light and particle previews draw a gizmo; scopes, curve editors, colour ramp handles and delay echoes stay inside their frames
- Analyse nodes (Hand Track, Image Analyze, Audio Analyze, Geometry Table) show slider-style output meters with live values in the body
- Axis controls are a segmented X/Y/Z switch; short choices select with one click
- Dropdowns, the modulation menu and every popup stay the same size on screen at any canvas zoom, with a capped width and shorter rows
- Zoomed far out, nodes collapse their parameters to keep the canvas readable; pins and bindings stay where they are
- A compact top bar: File, Edit and Menu fold into one Menu tile, the logo opens About, transport and Start Audio sit either side of BPM, and an available update shows as an icon. Settings moves to a gear on the right rail
- The cable delete button sits at the middle of the cable, scales with zoom, is undoable, and has a short grace period so it does not vanish as you reach for it
- Settings, the Field and Formula editors, and the Sketch editors share one window style; dialogs are tighter
- Colour swatches look the same everywhere (Settings, the theme preset strip, markers)
- Light theme contrast fixes, one tooltip delay and width, and a Reduce motion setting
- Add an Undo History panel (Shift+U) with labelled steps and click to jump
- Add Cmd/Ctrl+F find over the canvas (title, type, comment text, parameter names), with matches ringed on the canvas and minimap
- Add 15 starter templates under File > New from template
- Copy and paste nodes between patches as text, in one undo step
- Errors and warnings show on the node with a relink action; patch load and save, audio device and autosave problems use one notice style; Help gains Copy system info and Reveal logs
- Add a Cook times overlay (Menu > Cook times, off by default) and a Settings switch for the launch update check; the canvas grid is now off by default
- Add a two-step first-run text and an About build line with links
- The macOS DMG has a background image and a laid-out Finder window

### New nodes and sounds

- Add Sketch (Sources): write a JavaScript `draw(t)` that paints a canvas, with 17 presets, an editor window and SVG import (drop or paste an SVG; `svgDraw`, `svgSet`, `svgText`, `svgBox`)
- Add Sketch 3D (3D): a JavaScript script builds a mesh (box, sphere, cylinder, cone, torus, plane, tube, `beginShape`) with 6 presets
- Add Field Notes (Notes): a note node whose body is a Field script, with a note-out pin and presets
- Add MIDI Out (Utility): send notes and CC to a hardware or virtual port on macOS, Windows and Linux, with a Panic button; hanging notes are released on delete, stop and device change
- Add Spatial Mixer (Utility): a binaural 3D mixer for headphones. Each input has azimuth, elevation, distance and width, with a measured HRTF, room, bass mono, limiter and LUFS meter, and 24-bit export; head tracking through supported headphones on macOS
- Add Hand Track, Face Track and Pose Track: camera or image to modulators (landmarks, gestures), with a live landmark overlay and mirror, smoothing and hold. They need the Tracking pack from Settings > Extensions
- Add Delaunay Mesh and Voronoi Cells (3D), Curve Ops (resample, simplify, smooth, offset) and a quadric Decimate op
- Add Dither (Compositing): Bayer 2/4/8 and noise patterns with levels and mix
- Add gradient Perlin and Simplex (single and fBm) to Noise
- Color Ramp interpolates in Oklab and OKLCh; Palette swatches are gamut-mapped by chroma reduction
- Smooth has Per frame, Time and Spring modes; new nodes default to Per frame
- Add Game of Life and Smooth Life to the Field Pixel presets

### Fixes

- Fix Sketch scripts crashing the app when drawing at top level, and block SVGs from reading local files
- Fix a Smooth spring turning permanently NaN after a frame hitch
- Fix Spatial Mixer losing cables on load when a patch was saved with an empty input slot
- Fix quitting during an extension download hanging, and tracking nodes keeping a stale pose after the input is unplugged
- Fix Linux MIDI Out leaving notes held when a node is closed, and Windows loading the tracking runtime from a non-ASCII path
- Fix Reduce motion and Cook times not being remembered between launches

### Changes to existing patches

- Palette colours that fall outside the display range are now reduced in chroma instead of clipped, so some palettes look slightly different
- `beat` and `noteNum` are now reserved names in Field programs; a program that used either as its own variable name must rename it
- Ocean and Audio Ribbon orientation changed; re-check patches that use them
- Spatial Mixer head tracking works on macOS only
