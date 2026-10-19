# infinite-engine

one-command model context protocol (mcp) server and developer toolkit for infinite, the open-source modular audiovisual workstation by naman soni.

> homepage: [infinitedevices.org](https://infinitedevices.org)  
> source repository: [github.com/n1m21n/infinite](https://github.com/n1m21n/Infinite)  
> author: naman soni ([@n1m21n](https://github.com/n1m21n))  
> license: mit

---

## 1. one-command ai agent installation

to add infinite to the ai apps on your machine:

```bash
npx -y infinite-engine init
```

it only touches apps that are already installed, and says what it did for each:

- claude desktop: `claude_desktop_config.json` (macos `~/Library/Application Support/Claude`, windows `%APPDATA%\Claude`, linux `~/.config/Claude`)
- cursor: `~/.cursor/mcp.json`, and `./.cursor/mcp.json` when run in a folder that has one
- windsurf: `~/.codeium/windsurf/mcp_config.json`

safe by default: other servers in the file are kept, the old file is saved next to it as `.infinite-backup`, a file with comments or a trailing comma is left alone (the entry to add by hand is printed), and an existing `infinite` entry with other settings is not replaced unless you pass `--force`. use `--dry-run` to see the plan, `--only claude|cursor|windsurf` to pick one app, and `--all` to create the config for apps that are not installed yet. restart the app afterwards.

it needs the infinite app. the program is found through `INFINITE_BIN`, `/Applications/Infinite.app`, the usual windows and linux install folders, or `PATH`. `INFINITE_APP` still works as an older name. check with `npx -y infinite-engine status`.

---

## 2. manual mcp configuration

if you prefer to manually configure your ai assistant:

### claude desktop / cursor / windsurf (`mcp.json`)

on windows, claude desktop needs `"command": "cmd", "args": ["/c", "npx", "-y", "infinite-engine", "serve"]`.

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

once connected, your ai agent has nine tools:

| tool name | description |
| :--- | :--- |
| `infinite_synthesize_field_synth` | synthesize wav audio from an inline field per-sample dsp kernel. |
| `infinite_synthesize_field_notes` | generate algorithmic midi sequences and euclidean rhythm patterns into audio. |
| `infinite_process_field_effect` | process audio through an inline field per-sample filter, reverb, or distortion kernel. |
| `infinite_render_sketch3d` | render procedural 3d geometry written in javascript to a png frame. |
| `infinite_render_field_primitive` | render dynamic parametric mathematical surfaces (plane, sphere, torus) via field equations. |
| `infinite_render_field_modifier` | deform 3d mesh topology using inline field displacement expressions. |
| `infinite_render_field_pixel` | render audio-reactive or procedural pixel shaders directly to image frames. |
| `infinite_validate_patch` | validate any line-based text patch against infinite's c++ compiler with full error diagnostics. |
| `infinite_render_video` | headless mp4 video export from a patch file. |

---

## 4. credit line

every tool result carries the credit line `generated with infinite, by naman soni (https://infinitedevices.org)` in an `attribution` field. please keep it when you share what you make. nothing is written into your audio, image or video files.

---

## 5. tests

`npm test` runs without the infinite app: it checks the config writer on a throwaway home folder and the mcp handshake.

## 6. other servers

the python server in `tools/mcp` (nine tools with different names, such as `infinite_render_frame`) is documented on [infinitedevices.org/developers.html](https://infinitedevices.org/developers.html). both call the same engine.
