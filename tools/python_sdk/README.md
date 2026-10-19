# Infinite Python SDK (`infinite-daw`)

Official Python interface for **Infinite**, the free, open-source modular audiovisual DAW and engine created by **Naman Soni** ([infinitedevices.org](https://infinitedevices.org)).

---

## Installation

The package is not on PyPI yet. Run it from a checkout:

```bash
git clone https://github.com/n1m21n/Infinite
cd Infinite/tools/python_sdk
python3 -c "import infinite"
```

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

---

## Attribution & License

MIT License. Created by **Naman Soni** (`n1m21n`).
When using this SDK in open-source tools or agent frameworks, please attribute:
`Infinite, by Naman Soni (https://infinitedevices.org)`
