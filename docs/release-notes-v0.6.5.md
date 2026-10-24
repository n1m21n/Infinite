## What's new in v0.6.5

v0.6.5 adds a Session view for launching clips live, GPU points that scale to a million particles, new 3D outputs, and a long list of fixes that make bad files and bad values safe.

### Session view

- Session is its own panel (rail icon, Shift+S) with a Timeline-style toolbar, a monitor and a launch inspector
- A grid of clips and scenes: per-clip loop, loop length, launch quantize and launch mode; per-scene quantize and keep-tracks-playing
- Assign a canvas node to a slot, Add to Session from the canvas, Send Scene to Timeline, drag clips between cells, drop files from Finder onto cells
- Record takes, click to stop, follow scenes, Stop All on `.`
- The Timeline monitor plays only the Timeline's own clips; the Session monitor shows only in Session mode
- Groups are blocks: every group gets a power button, and a group can be one section of a live Show Set with one GO, one quantize and one fade

### GPU points

- Image to Points, Mesh to Points, Transform, Twist, Displacement and Field Modifier work on points on the GPU; Instance on Points builds its copies in the vertex shader
- Particle System simulates on the GPU (up to 1M particles) with a new emit pin and parameters
- Point Trails: a ring of past positions per point, drawn as tapered, fading prisms
- Feedback 3D: a one-frame geometry delay with a loop pin; it reseeds when the transport rewinds, holds while paused, and recovers from non-finite geometry
- Model 3D reads .ply point clouds; Displacement gets a Noise source; Field Modifier gets point presets (Limit to Box, Project onto Shape) and a life attribute
- Opaque mesh draws use a program with discard compiled out, which is faster on tile GPUs

### 3D and visuals

- Render 3D: 360 / dome output (Equirect 360, Cubemap 3x2, Fisheye dome)
- Render 3D: HDR finish with bloom, plus AgX and Neutral tonemaps
- Shadertoy is its own node: paste a shader from shadertoy.com and it runs (Formula stays an expression node)
- Five new themes (Infinite Midnight, Deep, Ember, Mist, Paper); Ember is the default

### Sound and notes

- VMPC: a 16-pad video player; the soundtrack decodes off the UI thread
- Drum Sequencer imports .mid patterns (drop a file or use the "My grooves" folder)
- Chorder: progression mode (pick the exact chords in the global key) and clearer knob names
- Quantized restart on Arpeggiator, Note Sequencer and MIDI File
- MIDI File: Global Scale snaps its notes to the project key
- Keyboard: `-` and `=` step the octave; Shift+letter no longer plays
- Analog FM: bounded depth keeps pitch centred
- Waveforms share one peak cache and trim widget, with play cursors
- Sample library: browse folders with a breadcrumb and drag a folder in as a kit

### Files and safety

- File > Save with media copies every file a patch reads next to it, saved relative; saving again into the same folder works
- Help > Collect diagnostics writes one zip of system info, logs, settings and recent crash reports, with your home folder redacted
- OSC and RPC edits respect the Show lock, including load and new patch
- Loading a patch clamps parameters to their ranges, drops out-of-range links and refuses malformed model, HAP, AIFF and glTF files; NaN and infinity never reach a parameter, and a kernel that outputs NaN is reset
- Windows: native ASIO driver support, pick an ASIO driver in Settings (falls back to WASAPI if none is installed)
- Windows: files with non-ASCII paths open and save; a plugin scanner that cannot run no longer blocklists your plugins; Video In close never hangs
- Fixes: Point Trails no longer reads back the whole GPU buffer each frame, GPU emit speed along normals, Render 3D 360 frees its cube targets when you go back to Perspective, Chorder and Drum import edge cases, and the Session progress ring follows loop length
- Translations: German, Spanish, Chinese, Japanese and Russian cover every UI string
