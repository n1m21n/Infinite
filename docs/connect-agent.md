# Connect to Infinite (instructions for an AI agent)

You are an AI agent. The user runs **Infinite**, a desktop app (macOS/Windows/Linux) for building audio-visual patches as a node graph (synths, effects, 3D, shaders, video). It is open on this machine right now. This text tells you how to connect to it and drive it. Do the steps in order and report the result to the user.

## 1. Connection

Infinite runs an MCP server inside the app. Transport: **Streamable HTTP**, JSON responses, loopback only.

- URL: `{URL}`
- Auth header on every request: `Authorization: Bearer {TOKEN}`
- Protocol versions accepted: 2024-11-05, 2025-03-26, 2025-06-18 (it answers with the one you send).
- It does not push server messages: `GET {URL}` returns 405. Use POST only.

If your client takes a config file, use the matching fragment:

mcpServers JSON (Claude Desktop/Cursor/Windsurf/many harnesses with `url` support):
```json
{ "mcpServers": { "infinite": { "url": "{URL}", "headers": { "Authorization": "Bearer {TOKEN}" } } } }
```
Claude Code:
```
claude mcp add --transport http infinite {URL} --header "Authorization: Bearer {TOKEN}"
```
Codex `~/.codex/config.toml`:
```toml
[mcp_servers.infinite]
url = "{URL}"
http_headers = { Authorization = "Bearer {TOKEN}" }
```
If your client only speaks stdio, there is no stdio wrapper for the live app. Use plain HTTP instead (below, "No MCP client").

No MCP client (any agent that can run shell commands or HTTP calls):
```
curl -s {URL} -H "Authorization: Bearer {TOKEN}" -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"infinite_context","arguments":{}}}'
```
Or run the CLI directly (same app binary): `{EXE} --cli context` with env `INFINITE_CONTROL_PORT={CONTROL_PORT}` and `INFINITE_CONTROL_TOKEN` (the app's control token; for HTTP use the token above instead).

## 2. Check the connection

1. `initialize`, then `tools/list`: you must see `infinite_context`, `infinite_cli`, `infinite_rpc`, `infinite_rpc_methods`.
2. Call `infinite_context` with `{}`. A text answer describing the open patch means it works.
3. HTTP 401 = wrong or missing token. Connection refused = Infinite is closed, or the server is switched off (Settings → Agents & Terminal → Connect Infinite to an outside agent). Tell the user; do not retry in a loop.

## 3. Tools

| Tool | Arguments | Use |
|---|---|---|
| `infinite_context` | none | State of the open patch: nodes, cables, what is selected, hints. Call first and after each change. |
| `infinite_cli` | `args`: array of strings, e.g. `["add","Oscillator"]` | Runs one `infinite` command. The commands are the whole editing language: `add <type> [x y]`, `remove <node>`, `connect <src[:out]> <dst[:slot]>`, `disconnect`, `set <node.param> <value>`, `undo`, `docs [topic]`, `find <words>`, `guide`. Run `["help"]` for the full list. |
| `infinite_rpc` | `method`: string, `params`: object | Low-level JSON-RPC of the app (same as the control socket). Use only when `infinite_cli` has no command. |
| `infinite_rpc_methods` | none | The method names `infinite_rpc` accepts. |

## 4. Recommended workflow

1. `infinite_context` to see the patch.
2. For unknown node types or fields: `infinite_cli ["find","<words>"]` and `["docs","nodes"]`. Pick names from those answers; do not guess them.
3. Change the patch with a few `infinite_cli` calls (`add`, `connect`, `set`). Changes appear live in the app and each one can be undone with `["undo"]`.
4. `infinite_context` again to verify what changed. Report to the user what you did.

## 5. Limits

- The app must be open, with its window not minimised for long: commands run on the app's main thread and time out after about 30 s.
- Loopback only. Do not try to reach it from another machine.
- The token is a secret of the user's. Do not write it to files, logs, commits or messages other than their own client config.
- A "show lock" in the app blocks edits that move the graph; the error says so. Ask the user to unlock it.
- Not verified: Windows behaviour of `infinite_cli`.
