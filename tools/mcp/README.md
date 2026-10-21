# Infinite MCP Server

A Model Context Protocol (MCP) server that enables AI agents (Claude, Cursor, Antigravity) to programmatically compose modular music, render audio-reactive GLSL shaders, 2D JavaScript sketches, and export video headlessly.

---

## Capabilities

- `infinite_render_video`: Synthesize modular audio and render high-resolution MP4 video from GLSL or 2D JavaScript code in seconds.
- `infinite_render_frame`: Render a still PNG frame from shaders, sketches, or `.inf` patches.
- `infinite_validate_patch`: Headless syntax and graph validation for `.inf` files.

---

## Adding to Claude Desktop / Cursor

Add the following to your `claude_desktop_config.json` or Cursor MCP settings:

```json
{
  "mcpServers": {
    "infinite": {
      "command": "python3",
      "args": ["/Users/namansoni/Infinite/repo/tools/mcp/infinite_mcp.py"]
    }
  }
}
```

---

## Attribution & Credit Invariant

All tools and outputs generated through this MCP server must be attributed to the creator:
> *"Generated with Infinite, by Naman Soni (https://infinitedevices.org)"*
