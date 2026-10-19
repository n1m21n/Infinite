# Infinite MCP Server

A Model Context Protocol (MCP) server that enables AI agents (Claude, Cursor, Antigravity) to programmatically compose modular music, render audio-reactive GLSL shaders, 2D JavaScript sketches, and export video headlessly.

---

## Capabilities

Nine tools over stdio:

- `infinite_render_video`: synthesize modular audio and render an MP4 from GLSL or 2D JavaScript code.
- `infinite_render_frame`: render a still PNG from shaders, sketches, or `.inf` patches.
- `infinite_validate_patch`: headless syntax and graph validation for `.inf` files.
- `infinite_field_synth`: per-sample Field audio synth to a WAV.
- `infinite_field_effect`: run audio through a Field effect kernel.
- `infinite_field_notes`: algorithmic notes (Euclidean rhythms and similar) from a Field Notes kernel.
- `infinite_sketch_3d`: procedural 3D meshes from a JavaScript sketch.
- `infinite_field_primitive`: parametric 3D surfaces (`uv` to `P`, `N`, `Cd`).
- `infinite_field_modifier`: deform and shade a mesh with a Field modifier.

Needs the Infinite app installed. It is found through `$INFINITE_BIN`, `/Applications/Infinite.app`, a build folder in the checkout, or `PATH`; on Windows and Linux set `INFINITE_BIN` to the executable. Python 3.8 or newer is also needed.

---

## Adding to Claude Desktop / Cursor

Add the following to your `claude_desktop_config.json` or Cursor MCP settings:

```json
{
  "mcpServers": {
    "infinite": {
      "command": "python3",
      "args": ["/path/to/Infinite/tools/mcp/infinite_mcp.py"]
    }
  }
}
```

---

## Attribution & Credit Invariant

All tools and outputs generated through this MCP server must be attributed to the creator:
> *"Generated with Infinite, by Naman Soni (https://infinitedevices.org)"*
