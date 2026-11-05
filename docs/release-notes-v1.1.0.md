## What's new in v1.1.0

Infinite 1.1 adds a Chat panel that works with your own Claude, ChatGPT or Gemini, a much deeper 3D View, five new 3D nodes and native ASIO audio on Windows.

### Chat

- A Chat panel where you talk to your own Claude, ChatGPT or Gemini. Infinite ships no agent: Settings > Agents installs and signs in the ones you choose, and a first-run flow guides someone who has never used a terminal. A missing Node is said once, with its real reason
- Type `@` to name a node, `/` for commands, `@gemini` or `@chatgpt` to switch agent; one model picker for all agents; queue messages while an agent works
- Pictures, files, audio, video and animated gifs in the thread; paste pictures and files; code opens in a side pane
- A change card shows what the agent changed, with live knobs and the agent's value as a dot (double-click to put it back)
- Agents can find nodes by plain words, import files, generate samples and MIDI, and link every settings page
- Pop the chat out into a floating window; token use per turn and in Settings
- Node menu: Reference in chat

### 3D View

- Edit Mode (Tab), Pose mode, X-ray and wireframe overlays, circle select, render passes, mist, cavity, motion blur, camera backgrounds, cylindrical and fisheye lenses, and a Material tab with a live preview ball
- Drop a model or any 2D output into the view
- Rigging: skinned glTF clips and morphs, IK, and an Armature node that bends any geometry with a bone chain, with weight paint

### New nodes

- Sculpt (brushes fitted to Blender's), Bake (normal map from a sculpt), UV Project, Edit Mesh and Armature

### Render 3D and physics

- Render 3D gets a path-trace engine: clearcoat, sheen, glass, depth of field, material maps and opacity. Raster gets area-light soft shadows and a floor bounce. Use the path tracer for scenes that rely on global illumination
- Cloth has bending, a collider input and friction; Rigid Bodies use the piece's own axes
- Subdivide is Catmull-Clark; Twist on Y turns the same way as X and Z
- Cloth no longer bypasses (it has two inputs), so an old patch with a bypassed Cloth behaves differently

### Windows audio

- Native ASIO: choose ASIO as the output mode in Settings > Audio, pick your interface's driver and play at buffer sizes down to 32 frames, with a round-trip latency readout. If the driver is missing or will not start, Infinite keeps playing on WASAPI and says why. Thanks to Ricardo Palmieri, whose fork set the behaviour

### Interface and languages

- Inactive params read as inactive in every node; output window is opaque with a checkerboard behind a transparent background
- New nodes land on a free spot, chained left to right, never on top of each other
- Every chat string is translated into Spanish, German, Chinese, Japanese and Russian

### Fixes

- Sketch text falls back to CJK fonts
- The Piano Roll plays notes on the loop start after a wrap
- Dropdowns close on the first Esc; wide knob captions and dropdown values end in an ellipsis; snap-to-grid applies only after a node drag

Made with Infinite, by Naman Soni (n1m21n), https://infinitedevices.org.
