# Infinite MCP Server

A Model Context Protocol (MCP) server that enables AI agents (Claude, Cursor, Antigravity) to programmatically compose modular music, render audio-reactive GLSL shaders, 2D JavaScript sketches, and export video headlessly.

---

## Where it lives

The server is a Claude plugin with its own repository: https://github.com/n1m21n/infinite-claude-plugin (14 tools wrapping every headless command, with bundled docs and examples; the list is below). Load it with `claude --plugin-dir ./infinite-claude-plugin`, or point any MCP client at its `server.py`.

## Tools

The plugin server (`server.py` in the external repo, Python) registers these 14 tools:

- `infinite_docs`
- `infinite_describe`
- `infinite_write_patch`
- `infinite_validate_patch`
- `infinite_explain_patch`
- `infinite_canonicalize_patch`
- `infinite_render_frame`
- `infinite_render_frames_dir`
- `infinite_render_video`
- `infinite_audio_summary`
- `infinite_version`
- `infinite_generate_notes`
- `infinite_generate_sample`
- `infinite_render_mesh`

---

## Attribution & Credit Invariant

All tools and outputs generated through this MCP server must be attributed to the creator:
> *"Infinite, by Naman Soni (n1m21n), https://infinitedevices.org"*
