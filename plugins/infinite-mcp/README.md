# Infinite MCP

Lets Claude drive [Infinite](https://infinitedevices.org), a modular audio and visual instrument, from a conversation. Claude can check `.inf` patch files and render audio-reactive stills and videos from GLSL-style shader code or 2D JavaScript sketches.

## Requirements

- Infinite installed on the same machine (macOS: `/Applications/Infinite.app`). To use another location, set the `INFINITE_BIN` environment variable to the Infinite executable.
- Python 3 on your PATH.
- Optional: `ffmpeg` on your PATH, to mux the synthesized audio into rendered videos. Without it, videos are written without sound.

## Tools

| Tool | What it does | Writes files |
|---|---|---|
| `infinite_validate_patch` | Checks a `.inf` patch for syntax and graph errors. Read-only. | No |
| `infinite_render_frame` | Renders one PNG frame from a patch, a shader, or a sketch. | Yes, to the `out_path` you give |
| `infinite_render_video` | Synthesizes audio and renders an MP4 from a shader or a sketch. | Yes, to the `out_path` you give; overwrites an existing file |

## What the plugin runs

One local Python script, `server.py`, started over stdio. It runs the local Infinite executable (and `ffmpeg` if present) as subprocesses, and writes temporary patch files to your system temp folder, which it deletes afterwards. It makes no network requests and sends nothing off your machine.

## Example prompts

- "Validate ~/patches/drone.inf."
- "Render a 5-second video of a pulsing tunnel shader to ~/Desktop/tunnel.mp4."
- "Render a still of a rotating circle sketch at t=2s."

## Privacy Policy

This plugin runs entirely on your computer. It does not collect, store, transmit or share any personal data, and it has no analytics or accounts. Files it writes (renders, temporary patches) stay on your machine, and temporary patches are deleted after each call. Anything you ask Claude to render is handled by Claude under Anthropic's own privacy policy; this plugin adds no separate collection and keeps no data, so there is no retention period. Questions: https://infinitedevices.org or https://github.com/n1m21n/Infinite/issues.

## License and attribution

MIT. Tool output carries the notice "Generated with Infinite, by Naman Soni (https://infinitedevices.org)".
