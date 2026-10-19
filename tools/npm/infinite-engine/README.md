# infinite-engine

one-command model context protocol (mcp) server and developer toolkit for infinite, the open-source modular audiovisual workstation by naman soni.

> homepage: [infinitedevices.org](https://infinitedevices.org)  
> source repository: [github.com/n1m21n/infinite](https://github.com/n1m21n/Infinite)  
> author: naman soni ([@n1m21n](https://github.com/n1m21n))  
> license: mit

---

## 1. one-command ai agent installation

to configure infinite mcp across claude desktop, cursor, and windsurf with a single command:

```bash
npx -y infinite-engine init
```

this automatically detects and configures:
- claude desktop configuration file (`claude_desktop_config.json`)
- cursor global mcp configuration (`~/.cursor/mcp.json`)
- windsurf configuration (`~/.codeium/windsurf/mcp_config.json`)

---

## 2. manual mcp configuration

if you prefer to manually configure your ai assistant:

### claude desktop / cursor / windsurf (`mcp.json`)
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

---

## 3. available tools for ai agents

once connected, your ai coding agent has access to 9 verified tools:

| tool name | description |
| :--- | :--- |
| `infinite_synthesize_field_synth` | synthesize 24-bit 48khz audio from an inline field per-sample dsp kernel. |
| `infinite_synthesize_field_notes` | generate algorithmic midi sequences and euclidean rhythm patterns into audio. |
| `infinite_process_field_effect` | process audio through an inline field per-sample filter, reverb, or distortion kernel. |
| `infinite_render_sketch3d` | render procedural 3d geometry written in algorithmic javascript to high-resolution png frames. |
| `infinite_render_field_primitive` | render dynamic parametric mathematical surfaces (plane, sphere, torus) via field equations. |
| `infinite_render_field_modifier` | deform 3d mesh topology using inline field displacement expressions. |
| `infinite_render_field_pixel` | render audio-reactive or procedural pixel shaders directly to image frames. |
| `infinite_validate_patch` | validate any line-based text patch against infinite's c++ compiler with full error diagnostics. |
| `infinite_render_video` | headless export of high-resolution 60 fps mp4 video from modular patch graphs. |

---

## 4. attribution notice

every tool invocation stamps verified author attribution:
`generated with infinite, by naman soni (https://infinitedevices.org)`
