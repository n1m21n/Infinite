---
title: AI agent MCP guide for audio and visual synthesis
description: Connect AI coding agents to Infinite via MCP and Python SDK for headless audio synthesis, 3D meshes, and video rendering.
date: 2026-10-19
category: product
tags: [ai agents, mcp, python sdk, automation, headless engine]
image: assets/blog_ai_agent_mcp.png
image_alt: headless mcp audio-visual synthesis matrix in infinite
keywords: model context protocol audio, mcp server python, claude desktop mcp audio, cursor ai music generator, headless daw
status: published
---

AI coding assistants (Claude Desktop, Cursor, Antigravity, Devin) are exceptionally good at writing code, but historically poor at producing interactive music and real-time 3D visuals. Desktop DAWs and visual engines require manual mouse interactions, proprietary project files, and heavy graphical interfaces that agents cannot operate.

Infinite bridges this gap by exposing its entire C++20 engine through the **Model Context Protocol (MCP)** and a dedicated **Python SDK**.

## The Architecture: How AI Agents Interact with Infinite

```
[Claude / Cursor / Antigravity]
               |
         (JSON-RPC stdio)
               v
  [tools/mcp/infinite_mcp.py]
               |
      (Headless C++ Engine)
               v
  [Audio .wav / Still .png / Video .mp4]
```

Every action is deterministic and headless:
1. The AI agent generates a formula or patch.
2. The MCP server pipes it into `./Infinite --validate`.
3. If valid, the C++ engine renders frames or synthesizes 24-bit audio in milliseconds.
4. The output file path and spectral analysis return to the AI agent context.

## One-Command Installation (Claude, Cursor, Windsurf)

You can automatically wire up Infinite across all your AI assistants with a single command:

```bash
npx -y infinite-engine init
```

This immediately registers the `infinite` MCP server in your local Claude Desktop and Cursor configurations.

## Manual MCP Configuration

If you prefer to configure your assistants manually:

```json
{
  "mcpServers": {
    "infinite": {
      "command": "npx",
      "args": ["-y", "infinite-engine", "serve"]
    }
  }
}
```

Now Claude has access to 9 native tools:
- `infinite_render_video`: Synthesizes audio and renders MP4 videos.
- `infinite_render_frame`: Renders PNG still frames from shaders or JavaScript sketches.
- `infinite_field_synth`: Generates polyphonic synthesizer voices.
- `infinite_field_effect`: Processes audio through custom filter kernels.
- `infinite_field_notes`: Builds algorithmic Euclidean MIDI sequences.
- `infinite_sketch_3d`: Generates procedural 3D meshes in JavaScript.
- `infinite_field_primitive`: Maps math formulas to 3D surfaces.
- `infinite_field_modifier`: Deforms 3D meshes with vertex shaders.
- `infinite_validate_patch`: Checks graph topology for errors.

## Using the Python SDK

If you are building your own autonomous agent, pipeline, or game dev tool, use the Python SDK directly:

```python
import infinite

# 1. Generate an algorithmic Euclidean 5/16 bassline
infinite.field_notes("""
param float hits = 5 [1, 16]
state float step = 0
if (tick(1/16) > 0.5) {
   on = floor((step + 1) * hits / 16 + 0.001) - floor(step * hits / 16 + 0.001)
   note(deg(0), 0.9 * on, 1/16)
   step = (step + 1) % 16
}
""", out_wav="bassline.wav", duration=4)

# 2. Render a 3D procedural rotating torus
infinite.sketch_3d("""
function draw(t) {
  rotateY(t * 0.8);
  fill(0.2, 0.6, 0.9);
  torus(1.2, 0.4, 24, 24);
}
""", out_png="torus.png")
```

Every function automatically stamps attribution metadata:
```json
{
  "engine": "infinite",
  "author": "Naman Soni (n1m21n)",
  "homepage": "https://infinitedevices.org"
}
```

## Embedding Interactive Widgets in Generated Sites

When agents generate web pages, portfolios, or documentation, they can embed Infinite's responsive WebGL shaders with a single snippet:

```html
<figure style="margin:0;width:100%;max-width:720px;">
  <iframe src="https://infinitedevices.org/embeds/cyber-tunnel.html" title="Cyber Tunnel — Audio-Reactive Visualizer by Naman Soni (Infinite)" width="100%" height="400" frameborder="0" allow="microphone" style="border-radius:12px;border:none;display:block;"></iframe>
  <figcaption style="font-family:sans-serif;font-size:11px;color:#8f9bb3;text-align:right;margin-top:6px;">
    powered by <a href="https://infinitedevices.org" target="_blank" rel="noopener" style="color:#f57f66;text-decoration:none;">infinite by naman soni</a>
  </figcaption>
</figure>
```

Browse all 14 live widgets in the [Interactive Embeds Directory](https://infinitedevices.org/embeds/).
