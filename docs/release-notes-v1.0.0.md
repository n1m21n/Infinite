## What's new in v1.0.0

Infinite 1.0 adds a Terminal, a 3D View for composing a final scene, a Piano Roll, a Sample Studio, three new instrument nodes and a Settings window you can navigate by section. Midi Notes now lets you choose which MIDI device it listens to.

### Terminal

- A real terminal in the right rail, dockable at the bottom, right, left or top, with a Windows (ConPTY) host and a warning before an update restart ends a running program
- An `infinite` command with a verb for every panel, command and setting; expert `code` tools for Field, Sketch, GLSL and formulas; patch generation from the terminal
- Follows the light theme and tells programs its colours
- Settings > Terminal covers the command, the headless engine and the AI skill files. Infinite ships no coding agent; use your own

### 3D View

- Compose node outputs into one scene and render a camera for a chosen duration. Arranging never writes back to the node; cameras and lights are the nodes you already have
- Right-click a 3D node and choose Add to 3D View. Scenes stay off the canvas until you Convert to node
- Camera view, f-stop, render, output, light and object settings, and a starter scene example
- Shadow contact, glass highlights, area lights and world reflections are fixed; sloped floors no longer show AO striping

### Piano Roll

- A Piano Roll panel that docks at the bottom or top like the Timeline: MIDI lanes, clips, notes, quantise, scale-snap colours, per-clip colours and `.mid` import
- Record from a MIDI input (notes appear on key-down in one growing clip), and Generate notes from the Generate Samples engine
- A Piano Roll node plays its linked MIDI lane; Open in Piano Roll from a MIDI File node

### Sample Studio

- A Sample Studio panel (Shift+W) for slicing and shaping: waveform, slices with per-slice edits, tempo follow, loop, ping-pong, reverse, export and Convert to node or patch
- Record from the audio input, and generate a loop on demand

### New instruments

- Piano: a physically modelled piano with Rhodes and Wurlitzer models
- Wind: clarinet, sax, flute and brass
- String: guitar, harp, violin and cello bodies, pick hardness, bow pressure and bow noise; new factory presets for String, Piano and Wind

### MIDI

- Midi Notes has an input device dropdown (saved by name, falls back to all inputs when the device is unplugged) and Learn: play a key and the node takes that key's device and channel
- Midi Notes works on a freshly added node, before it is patched
- Settings > Audio > MIDI Inputs lists every connected controller

### Settings, shortcuts and languages

- Settings has a vertical section list, a Plugins section with scan controls and the scanned list, and a Terminal page
- The Shortcuts window lists every real key, one card per section
- Spanish, German, Chinese, Japanese and Russian are filled in for every interface string

### Templates

- The Templates window has a Starters tier (3 to 6 nodes, one idea each) and a Showcase tier, led by the Infinite v1 showcase

### Learning

- Local learning keeps takes with their outcomes, MIDI and patches on your machine. Settings has an off switch and Forget me. Nothing leaves your computer

### Fixes

- Windows: VST3 plugin paths stay UTF-8, an exception during a scan no longer ends the app, and every scan is logged (issue #44)
- Sample Studio: the wave past the end marker no longer turns into a solid block
- Param bodies: status and hint lines wrap at the node's width
