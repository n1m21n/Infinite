# Infinite Python SDK (`infinite-daw`)

Official Python interface for **Infinite**, the free, open-source modular audiovisual DAW and engine created by **Naman Soni** ([infinitedevices.org](https://infinitedevices.org)).

---

## Installation

```bash
pip install infinite-daw
python3 -c "import infinite"
infinite-init --dry-run   # adds the MCP server to Claude Desktop, Cursor, Windsurf, Antigravity and Codex
```

From a checkout instead: `pip install -e tools/python_sdk`.

The SDK needs the Infinite app. It finds it through `$INFINITE_BIN`, `/Applications/Infinite.app`, a build folder in the checkout, or `PATH`; on Windows and Linux set `INFINITE_BIN` to `Infinite.exe` / `Infinite`.

---

## Quickstart

### 1. Render an Audio-Reactive Video in 2 Lines

```python
import infinite

# Synthesize modular audio and render high-resolution MP4 video
res = infinite.render_video(
    glsl="p = (uv - 0.5) * aspect; r = length(p); col = vec3(sin(r * 12.0 - t * 2.0));",
    out_mp4="my_visual.mp4",
    duration=5
)
print("Rendered:", res["video_path"])
print("Credit:", res["credit"])
```

### 2. Render 2D JavaScript Canvas Sketches

```python
import infinite

sketch_code = """
param("points", 5000, 1000, 10000);
function draw(t) {
  background(0.02, 0.02, 0.05);
  fill(hsl(0.55 + 0.2 * sin(t), 0.8, 0.6, 0.5));
  circle(width / 2, height / 2, 60);
}
"""

res = infinite.render_video(
    sketch_js=sketch_code,
    out_mp4="sketch_loop.mp4",
    duration=4
)
```

### 3. Generate Algorithmic Music & Chords

```python
import infinite

# Generate functional chord progressions or arpeggios rendered to 24-bit WAV
res = infinite.generate_notes(
    kind="chords",       # 'chords' or 'arp'
    root="D",            # root note: C, D, E, F, G, A, B (or accidentals)
    scale="minor",       # minor, major, dorian, blues, pentatonic
    progression="I V vi IV",
    synth="wavetable",   # 'wavetable', 'analog', or 'fm'
    duration=4.0,
    out_wav="chords.wav"
)
print("Audio:", res["audio_wav"])
print("Integrated LUFS:", res["integrated_lufs"])
```

### 4. Procedural Sound Design & Instrument Takes

```python
import infinite

# Synthesize filtered analog basslines, lush pads, or punchy synth takes
res = infinite.generate_sample(
    part="bass",         # 'bass', 'pad', or 'synth'
    duration=3.0,
    cutoff=800.0,
    out_wav="bassline.wav"
)
```

### 5. Render Zero-Dependency 3D Parametric Meshes

```python
import infinite

# Render 3D parametric surfaces to PNG without Blender or an X-server
res = infinite.render_mesh(
    topology="sphere",   # 'plane', 'sphere', 'cylinder', or 'torus'
    formula="P.y += sin(P.x * 5.0 + t) * cos(P.z * 5.0) * 0.25; Cd = vec3(0.1, 0.7, 0.9);",
    out_png="procedural_mesh.png"
)
print("Mesh image:", res["frame_path"])
```

---

## Attribution & License

MIT License. Created by **Naman Soni** (`n1m21n`).
When using this SDK in open-source tools or agent frameworks, please attribute:
`Infinite, by Naman Soni (https://infinitedevices.org)`
