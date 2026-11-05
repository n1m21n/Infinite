## What's new in v1.1.0

Infinite 1.1 turns the 3D View into a full workspace: Edit Mode, Pose mode, sculpting, baking, rigging, materials and a path-trace engine, with five new nodes. It also adds a Chat panel that works with your own Claude, ChatGPT or Gemini, and native ASIO audio on Windows.

### 3D View

- **Edit Mode (Tab):** select, move, rotate, scale, extrude, inset and delete on a mesh, kept by an Edit Mesh node so the edit replays on the input. G, R and S take X, Y and Z (Z is vertical, as in object mode) and Shift+axis locks to a plane, with a readout in the displayed axis order. Edits keep their meaning when the input mesh is refined
- **Pose mode** with weight paint, and a Material tab that edits the selected object's Material and its maps, with a live preview ball
- **Shading and overlays:** Solid view with object colour, X-ray (Alt+Z) and a wireframe overlay, per-item shadow casting, flat meshes drawn on both sides, and a grid that depth-tests correctly: it no longer bleeds through meshes, and the mesh wins where it meets the floor
- **Render passes:** mist (with a range display on the camera), object index and cavity; motion blur; a camera background image; a render region; a render-time stamp and a Finish for renders
- **Cameras and lenses:** cylindrical 360 and custom fisheye lenses, camera sensor presets, and leaving camera view returns you to the view you had before
- **Selecting and moving:** circle select, trackball rotate, persistent rulers, continuous grab, centre view to mouse, Alt-drag box select in the outliner, outliner reorder, filters and keyboard walk, and Shift+double-click on empty space to glide the pivot back to the 3D cursor
- **Placing things:** drop a model file into the view, turn any 2D output into a textured picture plane, motion-modulated placements and Follow, and a right-click menu that changes with what you click
- **Units, preferences and operators** for transform, snap, placement and the object, select and view operators, with the parity checked row by row against Blender's 3D View. The nav and edit axes are self-tested
- **Trackpad:** Shift pans, Ctrl or Cmd zooms and plain scroll orbits, undoing macOS's Shift scroll swap
- Edit Mesh, Sculpt and Armature cards have an Open in 3D View button; the scene list uses item keys instead of action buttons

### New nodes

- **Sculpt:** brushes with strengths and spacing fitted to Blender's own (draw, inflate, flatten, pinch, crease), symmetry that follows the displayed axes, any 2D node as the brush image, and strokes whose dabs land on the surface where the stroke began
- **Bake:** a normal map from a sculpt
- **UV Project:** projects UVs onto any mesh
- **Edit Mesh:** the Edit Mode operations as a node; it passes its input through when bypassed
- **Armature:** bends any geometry with a bone chain, with weight paint

### Rigging and models

- Skinned glTF clips and morphs in Model 3D, fitted to the rest pose, with IK driven by 3D View items
- glTF import: one part per material, file textures right side up, and spec-gloss materials read (diffuse map, roughness from glossiness, specular from F0)
- Text 3D: even caps and banded walls, so sculpts and modifiers stop tearing letters

### Materials and Render 3D

- **Path-trace engine:** a progressive path tracer with a BVH, with clearcoat, sheen, glass (IOR, rough transmission), specular level, depth of field matching the raster's blur, material texture maps (albedo, roughness, metallic, normal or bump, emission, clear coat, sheen, transmission), opacity with an alpha cutoff and opacity map, and refinement in frame-budget strips. Choose it from the Engine dropdown on the Render tab; raster-only rows go grey under it. Use it for scenes that rely on global illumination
- **Raster:** soft area-light shadows that follow the light's size, a floor bounce, Blender's spot-light cone falloff, light falloff, Ambient, diffuse and specular, the exact sRGB curve instead of gamma 2.2, and a sheen visibility term
- **Materials:** 12 measured Material presets, opacity and transmission maps, a Load map field that spawns a cabled Image Source, and images that can be dropped onto a Material. Controls the path tracer ignores are greyed
- Render 3D has balanced columns and a node-wide show more or show less

### Physics and modelling

- **Cloth:** bending, a collider mesh input with Coulomb friction (0 to 10), real-world gravity that pulls every mass the same, and a draped-over-a-ball check. Cloth no longer bypasses: it has two inputs, so an old patch with a bypassed Cloth behaves differently
- **Rigid Bodies:** a box uses the piece's own axes, with warm-started contacts
- **Subdivide** is Catmull-Clark on recovered quads, to the limit surface, and faster. **Twist** on Y turns the same way as X and Z
- Sculpting is incremental, and a 3D bench measures the slow paths

### Chat

- A Chat panel where you talk to your own Claude, ChatGPT or Gemini. Infinite ships no agent: Settings > Agents installs and signs in the ones you choose, and a first-run flow guides someone who has never used a terminal. A missing Node is said once, with its real reason
- Type `@` to name a node, `/` for commands, `@gemini` or `@chatgpt` to switch agent; one model picker for all agents; queue messages while an agent works
- Pictures, files, audio, video and animated gifs in the thread; paste pictures and files; code opens in a side pane
- A change card shows what the agent changed, with live knobs and the agent's value as a dot (double-click to put it back)
- Agents can find nodes by plain words, import files, generate samples and MIDI, and link every settings page
- Pop the chat out into a floating window; token use per turn and in Settings
- Node menu: Reference in chat
- Every chat string is translated into Spanish, German, Chinese, Japanese and Russian

### Windows audio

- Native ASIO: choose ASIO as the output mode in Settings > Audio, pick your interface's driver and play at buffer sizes down to 32 frames, with a round-trip latency readout. If the driver is missing or will not start, Infinite keeps playing on WASAPI and says why. Thanks to Ricardo Palmieri, whose fork set the behaviour

### Interface

- Inactive params read as inactive in every node; the output window is opaque with a checkerboard behind a transparent background
- New nodes land on a free spot, chained left to right, never on top of each other
- Snap to grid applies only after a node drag, and only to the dragged nodes

### Fixes

- Sketch text falls back to CJK fonts
- The Piano Roll plays notes on the loop start after a wrap
- Dropdowns close on the first Esc; wide knob captions and dropdown values end in an ellipsis
- Linux: the 3D View grid keeps a depth margin of at least four depth steps, so software GL no longer lets it through; the video decoder is drained at the end of a file so the last frames arrive

Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org.
