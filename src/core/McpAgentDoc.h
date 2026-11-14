#pragma once
// Generated from docs/connect-agent.md by tools/gen_agent_doc.py - edit the .md, not this file.
namespace McpHttp
{
   constexpr const char* kAgentDoc = R"RAWDOC(# Connect to Infinite (instructions for an AI agent)

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

1. `initialize`, then `tools/list`: you must see `infinite_context`, `infinite_cli`, `infinite_rpc`, `infinite_rpc_methods`, `infinite_rpc_help`.
2. Call `infinite_context` with `{}`. A text answer describing the open patch means it works.
3. HTTP 401 = wrong or missing token. Connection refused = Infinite is closed, or the server is switched off (Settings → Agents & Terminal → Connect Infinite to an outside agent). Tell the user; do not retry in a loop.

## 3. Tools

| Tool | Arguments | Use |
|---|---|---|
| `infinite_context` | none | State of the open patch: nodes, cables, what is selected, hints. Call first and after each change. |
| `infinite_cli` | `args`: array of strings, e.g. `["add","Oscillator"]` | Runs one `infinite` command. The commands are the whole editing language: `add <type> [x y]`, `remove <node>`, `connect <src[:out]> <dst[:slot]>`, `disconnect`, `set <node.param> <value>`, `undo`, `docs [topic]`, `find <words>`, `guide`. Run `["help"]` for the full list. |
| `infinite_rpc` | `method`: string, `params`: object | Low-level JSON-RPC of the app (same as the control socket). Wrong or missing fields return `isError` with the method's example, never a crash. |
| `infinite_rpc_methods` | none | **Every method with its parameters and a working example.** Read it before the first `infinite_rpc` call. |
| `infinite_rpc_help` | `method`: string | The same for one method. |

## 3b. RPC cheat sheet (details: `infinite_rpc_methods`)

- Node indexes are integers from `get_graph` (`nodes[].index`). A plain integer is best (`"dst":5`); a numeric string (`"dst":"5"`) or `{"index":5}` is also read. `bind_mod` takes the node as `dst`, `unbind_mod` as `index`, and either verb accepts either name.
- Control keys have a type-letter prefix: `f rotation` (float), `i`, `b`, `c`, `s`. `get_params` / `describe` print them. `set_param` accepts the key with or without the letter (`rotation`); `bind_mod`/`set_expr` too.
- Type names for `create_node`/`describe` are exact (`describe` with no `type` lists them).
- Set a value: `infinite_rpc {"method":"set_param","params":{"index":5,"name":"rotation","value":0.5}}`.
- Set an expression: same call with a string value that starts with `=`: `"value":"=sin(t)*0.3"`, or `set_expr` with `text`.
- Modulate a control from another node: `bind_mod {"dst":5,"key":"f rotation","src":4,"output":0,"lo":0,"hi":1}`; remove with `unbind_mod {"dst":5,"key":"f rotation"}` (`index` works for both). `src` also takes `source`/`from`/`modulator`. The answer names what was bound: `{"index","param","key","src","output"}`.
- Cables: `connect {"srcIndex":4,"srcOutput":0,"dstIndex":5,"dstSlot":0}`; `disconnect {"linkId":N}` (ids in `get_graph`).
- Save the patch: `save_patch {"path":"/abs/path/name.inf"}`. With no `path` it writes to the open patch's own file; if the patch has no file yet it returns an error. In that case do not invent a path: ask the user where to save. The path must be absolute; missing folders are created; the answer is `{"saved":"<real absolute path>"}` or an error that names the reason (no folder rights, not absolute, is a folder). Open with `load_patch {"path":...}`.
- Undo your own change: `undo`.

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
- `infinite_context` works without the CLI (it reads the app's RPC in-process), so it also works on Windows.
- `infinite_cli` runs on Windows too: it starts `<this binary> --cli <args>` with the control port and token in its environment (`src/core/McpHttp.cpp`, `RunCli`), the same command the POSIX build runs. Verified by code review only — not yet exercised against a live Windows instance.
)RAWDOC";
}
