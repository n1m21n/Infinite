# Terminal

Status: approved by the owner 2026-10-30. Block A built, Blocks B and C built in part on `feature/terminal-b`.

Checked on macOS (2026-10-30): `scripts/terminal_cli_smoke.py` passes; live verbs, `term run` with exit codes, `infinite` inside the panel, and two apps on one machine (second takes the next port) were run against a dev app.

Open:
- `patch apply --merge` is not built. Parity is held by `tools/terminal/parity.py` (run in `scripts/headless_smoke.sh`): 457 clickable actions, each with a verb or a stated reason in `tools/terminal/parity.json`. Verbs were each run against the app on macOS, except render output and `node rename`.
- Shell integration startup files are not written; `term run` sends its own exit marker, and on PowerShell it does not work yet.
- C5/C6 agent bench not run.
- Start-up of the shim is 130-200 ms, over the 50 ms target: the command is the whole app binary.
- Unverified: show lock refusal, audio during a flood, vttest, Claude Code/vim/htop/tmux by hand, signed Finder launch, Windows and Linux builds.
Idea: a real terminal in the right rail, for people and coding agents, with everything Infinite can do
reachable from one `infinite` command, and expert help for every place people write code in Infinite.

## Goal and done check

- Goal: an agent such as Claude Code runs inside Infinite exactly as it runs in Ghostty, can see and
  change the open patch without anyone copying paths or screenshots by hand, writes correct Field, Sketch
  and GLSL first time, and can generate a whole working patch from the terminal.
- Done when: all of the "Proof" rows at the end of each block pass on macOS, Windows and Linux, and the
  audio bench shows no change with the terminal open and busy.

## The picture

```
 ┌─ Infinite process ─────────────────────────────────────────────────────┐
 │                                                                        │
 │  Terminal panel (ImGui, main thread)                                   │
 │    tabs, cell grid, selection, search        reads latest snapshot     │
 │         │ keys, paste, resize                        ▲ never blocks    │
 │         ▼                                            │                 │
 │  TermSession (one per tab, own thread)                                 │
 │    PtyHost  ──bytes──►  Sync filter  ──►  VtEngine  ──►  snapshot      │
 │       │                                                                │
 │  RemoteControl 127.0.0.1:7777  ◄────┐      existing, gets more verbs   │
 │       │ one drain per frame         │                                  │
 │  HandleRpcCommand (Rpc.cpp)         │                                  │
 └───────┼─────────────────────────────┼──────────────────────────────────┘
         │ PTY                         │ JSON-RPC + token
         ▼                             │
 ┌─ child processes ─────────────────────────────────┐
 │  zsh / bash / PowerShell                          │
 │    ├─ claude, codex, vim, git, tmux ...           │
 │    └─ infinite <verb>  ───────────────────────────┘
 │         app running?  yes: live route (RPC)
 │                        no: headless route (--frame, --render, --validate ...)
 └───────────────────────────────────────────────────┘
```

## What exists today

| Piece | Fact | Effect on this plan |
|---|---|---|
| Rail | `PanelRail::Item` calls in `src/app/frame/StageMenuBar.cpp:26-49`, 8 tiles on top, 2 at the bottom | Add a ninth tile at `top + 8 * step` |
| Panel pattern | `gPianoRollOpen` is the newest full example: `AppState.cpp`, `AppShared.h`, `StageLayout.cpp`, `StageSidePanels.cpp`, `StageKeyboard.cpp`, `StageMenuBar.cpp`, `PatchIO.cpp` | Copy it, except the patch save (D9) |
| Key claim | `gPianoRollClaimedKeys` turns canvas shortcuts off (`StageKeyboard.cpp:341`, `StageLayout.cpp:134,146`) | Terminal gets the same claim |
| Tab trap | ImGui Tab nav must be taken with `SetKeyOwner` before `NewFrame` (`docs/plans/keyboard-nav/README.md`) | Same trick, or Tab never reaches the shell |
| Live control | `RemoteControl`: loopback only, port 7777, `INFINITE_CONTROL_PORT`, token file, requests run on the main thread between frames | The live route is already there |
| Live verbs | About 30 in `src/app/graph/Rpc.cpp`: graph, params, patch load/save, undo, `render_frame`, `screenshot_node`, `batch`. None for modulation, expressions, bypass, transport, timeline, session or selection | Block B fills these |
| OSC | Loopback UDP 9100, `src/app/graph/OscApi.cpp`, includes show next/prev/go | Stays as it is; show verbs reach parity through RPC |
| Headless | `--describe --validate --explain --frame --frames-dir --render --audio-summary --canonicalize --version`, typed errors, exit codes, `--json` with `attribution` | Wrapped, never rewritten |
| Windows | `src/cli_main_win.cpp` builds `Infinite.com`, the console front door | The `infinite` command uses it on Windows |
| PTY and VT | None in the codebase. No mono font in `external/fonts`, no terminal glyph | New: engine, font, icon |
| ImGui | 1.92.9, which has dynamic font loading | Terminal glyphs should not need a huge baked atlas (checked in A1) |
| macOS signing | Hardened runtime with four entitlements in `cmake/Infinite.entitlements` | Child shells must be tested in a signed build, not only a dev build |
| Code nodes | Field family (`FieldPixel`, `Field Synth`, `Field Effect`, `Field Notes`, `Field Modifier`, `Field Primitive`, `Field Graph` and others), `Sketch`, `Sketch 3D`, `Shadertoy`, `Formula`, `Equation Synth` | One `code` command for all of them |
| Code checking | `--validate` builds each node holding `s code` or `s formula` and reads `INode::CodeError()`. `SketchNode` and `Sketch3DNode` keep a `LastError()` but do not override `CodeError()`, so Sketch errors are not reported through this path | C1 closes the gap, including GLSL in `Shadertoy`, which needs a GL context |
| Language knowledge | `src/core/AISkillContent.h` ships patch authoring, Field language and expression guides inside the app. Sketch, Sketch 3D and Shadertoy have none | C2 adds them, so a shipped app is the expert, not only a checkout |
| Authoring loop | `infinite-patch-authoring`: describe, write, lay out, validate, explain, look, fix, render | `patch check` runs it in one call |
| Layout | `tools/patch-layout.py` is checkout only and not in the shipped app | Layout moves into the app (D23) |
| Starter patches | `assets/templates` with `index.txt`, `assets/examples/authoring` | Become the `patch new` starting points |

## Decisions

| # | Decision | Why |
|---|---|---|
| D1 | A real PTY and a full VT engine. No fake console, no line-input box | "Works like a real terminal" is the whole point; agents and vim need raw keys and the alternate screen |
| D2 | Engine: `libvterm` vendored in `external/`, behind a small `VtEngine` header | Plain C, fits our build. `libghostty-vt` has more features but needs the Zig toolchain on every platform and warns that its API will change |
| D3 | Handle synchronized output (DEC 2026) ourselves in a small byte filter in front of the engine: hold the published snapshot between begin and end | Agent TUIs repaint constantly and rely on it to avoid flicker. I found no confirmation that libvterm supports it, so we do not depend on it |
| D4 | One thread per session reads the PTY and feeds the engine, then publishes a snapshot. The main thread only ever takes the latest snapshot and never waits | Audio > projector > canvas. A `cat` of a huge file must not touch a frame |
| D5 | Sessions live in the app process in this version, behind a `PtyHost` header | Smallest thing that works. Cost: a crash of Infinite ends the shell and any agent in it. D6 softens that, and a helper process can slot in behind the same header if it proves needed |
| D6 | Quit and self-update warn when a session is running something. Each agent run is a normal terminal session, so tmux inside the panel gives survival to anyone who wants it | A lost agent run is a flow break; tmux already solves it without us owning a daemon |
| D7 | Spawn on the session thread, close all inherited file descriptors in the child, start a login shell | A fork from the main thread would stall a frame. The child must not inherit audio, socket or plugin handles. A login shell gives the user's real PATH, which a GUI app otherwise lacks, so `claude` is found |
| D8 | Windows uses ConPTY, macOS and Linux use a POSIX PTY. Same `PtyHost` header | The only platform split. Shell on Windows is PowerShell (`pwsh` if present) |
| D9 | Open state, dock and height are app preferences, not saved in the patch | A shared patch must never open a terminal or start a shell |
| D10 | Patches never touch the terminal: no node, expression or script can start a session or type into one | A patch from a stranger must stay harmless |
| D11 | One command, `infinite`, auto-routes: live when an app answers, headless when none does. `--live` and `--headless` force a route. `Infinite --frame ...` keeps working | People and agents should not have to know which route applies |
| D12 | Every `infinite` verb has `--json`, keeps the `attribution` object, returns typed errors and the documented exit codes | Same contract as the headless CLI, so agents parse one shape |
| D13 | Agent edits go through `batch` and land as one undo step per command | The human can take back an agent's change with one undo |
| D14 | Live verbs that change the patch are refused while a show is running, unless `--force` is passed | Infinite is a live instrument; an agent must not rewrite a running show by accident. Reads stay allowed |
| D15 | We ship no coding agent and no API key handling. The user's own `claude`, `codex` and the like run in the shell | It is a terminal, not an AI feature; no accounts, no cost, no lock-in |
| D16 | Font: Geist Mono bundled in `external/fonts`, with Noto as fallback for CJK. Box drawing and block characters are drawn as shapes, not glyphs | It is the mono cut of the house type, and shapes leave no seams between cells. If A1 finds coverage gaps, JetBrains Mono replaces it |
| D17 | Theme: terminal colours come from design tokens, one dark palette. No user theme files in this version | One design language, and less surface |
| D18 | Shortcut Ctrl+` toggles the panel and moves focus between terminal and canvas, on all platforms | The standard terminal toggle in editors, and it never collides with a shell key. `shortcuts-sweep` confirms it is free |
| D19 | Parity is full: every panel, command and setting gets a verb | Owner's call, 2026-10-30. Held by the parity script |
| D20 | One `code` command across all code nodes. The node type picks the language, so nobody learns five tools | Field, JS and GLSL differ, but check, push, watch and docs are the same job |
| D21 | Checking uses the real compilers the app runs. No separate linter or parser | The check and the render can never disagree, the same rule `CheckFieldCode` follows |
| D22 | Code lives in plain files while being worked on, and goes into the node with `code push` or `code watch` | Agents and editors work on files. Keep-last-working means a broken save never blanks the output |
| D23 | Layout moves into the app, and `patch-layout.py` becomes a thin caller or is retired | A shipped app has no checkout, and a generated patch must not open as one pile |
| D24 | Generation stays with the user's agent. Infinite gives starters, facts and a one-call check, and has no prompt-to-patch model of its own | Follows D15. The agent is the writer; `patch check` keeps it honest |
| D25 | A generated patch counts as done only when `patch check` passes on both picture and sound | A patch can validate and still be blank or silent |

## What the human sees

```
 ┌────────────────────────────────────────────────────┬────┐
 │ canvas                                             │ ◧  │
 │                                                    │ ▣  │
 │                                                    │ ⠿  │ rail
 ├─ zsh ─┬─ claude ● ─┬─ + ──────────────── ⌕  ⌃  × ──┤ …  │
 │ ~/shows/set1 $ infinite graph                      │ ▶_ │ new tile
 │ 12 nodes, 14 wires, Output: out                    │    │
 │ ~/shows/set1 $ █                                   │    │
 └────────────────────────────────────────────────────┴────┘
```

| Element | Behaviour |
|---|---|
| Rail tile | New `Terminal` glyph (`>_` in a square), built with `tools/design/build_glyphs.py`. Small status dot: running, finished, failed, needs attention |
| Dock | Bottom by default, moves to all four sides; View > Terminal picks the side |
| Tabs | One per session, named after the running program. `+` opens a new one. Split panes are out of this version; tmux covers them |
| Start folder | The open patch's folder, or home when the patch is unsaved |
| Keys | With focus, all keys go to the shell, including Tab, Esc and arrows. Cmd+C copies a selection (Ctrl+Shift+C on Windows and Linux), Cmd+V pastes, Cmd+Q still quits |
| Mouse | Select, double-click word, triple-click line, wheel scrollback. Programs that ask for the mouse get it; Shift overrides |
| Scrollback | 10,000 lines per session, Cmd+F search |
| Links | URLs open the browser. `.inf` paths open the patch. Rendered images and videos reveal in the Library |
| Canvas to shell | Drag a node, file or sample in and its id or path is typed at the cursor, with bracketed paste |
| Attention | When a program rings the bell or sends a notification while the panel is closed, the tile dot shows it. Nothing pops up |

Settings: font size, scrollback length, shell path. Nothing else.

## The `infinite` command

Every session starts with these set, so the command targets the right app even with two running:
`PATH` (the shim comes first), `INFINITE_CONTROL_PORT`, `INFINITE_CONTROL_TOKEN`, `TERM=xterm-256color`,
`COLORTERM=truecolor`, `TERM_PROGRAM=Infinite`.

| Group | Verbs | Route |
|---|---|---|
| Look | `graph`, `explain`, `params`, `selection`, `view`, `status` | live, or headless given a patch file |
| Edit | `add`, `remove`, `connect`, `disconnect`, `set`, `mod`, `expr`, `bypass`, `group`, `batch`, `undo`, `redo` | live; a patch file argument edits the text |
| Patch | `new`, `open`, `save`, `validate`, `canonicalize`, `apply <file.inf>` | both |
| Play | `play`, `stop`, `seek`, `tempo`, `session launch`, `timeline ...`, `show next/prev/go` | live |
| See and hear | `shot <node>`, `frame`, `render`, `audio`, `hash` | both |
| Code | `code new`, `code check`, `code push`, `code pull`, `code watch`, `code errors`, `code docs`, `code examples` | both, see below |
| Generate | `patch new`, `patch check`, `patch layout`, `patch apply`, `patch recipes` | both, see below |
| Learn | `nodes`, `describe <type>`, `guide`, `context` | both |
| Watch | `watch` streams changes as JSON lines | live |
| Terminal | `term list`, `term run`, `term read`, `term send` | live |

Three of these matter most to agents:

- `selection` tells the agent what the human is pointing at, so "make this brighter" has a subject.
- `shot` and `frame` write a PNG and print its path, which is how an agent checks a picture.
- `context` prints a short brief: open patch, selection, verbs, and where the full guide lives. It builds on
  `src/core/AISkillContent.h`. An agent reads this one output and is ready.

`term run` lets a process outside the panel, such as an MCP client, run a command in a visible session and get
the output and exit code back. Exit codes come from shell integration marks (OSC 133), which we install for
zsh, bash and PowerShell through the startup files of our own sessions only. The user's dotfiles are not edited.

The shim is the same binary and exits before any window or GPU work starts. The MCP servers stay as they are.

## Writing code from the terminal

```
 infinite code new sketch flow.js        starter file, written to disk
        │
        ▼
 you or your agent edit flow.js ◄────────────────┐
        │                                        │
        ▼                                        │
 infinite code check flow.js   real compiler     │ line:col + hint
        │ ok                                     │
        ▼                                        │
 infinite code watch flow.js --node sketch1 ─────┘
        │ every save lands in the node, a broken save keeps the last good picture
        ▼
 infinite shot sketch1         PNG path back, so the agent sees the result
```

| Language | Nodes | `code new` kind | Check runs | Result to look at |
|---|---|---|---|---|
| Field | `FieldPixel`, `Field Graph`, `Field Modifier`, `Field Primitive`, `Field Synth`, `Field Effect`, `Field Notes`, `Field Sample` | `field-pixel`, `field-synth` ... | Field compiler | Frame, or audio summary |
| JavaScript 2D | `Sketch` | `sketch` | quickjs compile, then one `draw(0)` | Frame |
| JavaScript 3D | `Sketch 3D` | `sketch3d` | quickjs compile, one `draw(0)`, mesh caps | Frame through a small Render 3D patch, plus vertex count |
| GLSL | `Shadertoy` (all passes) | `shadertoy` | Driver compile with the Shadertoy preamble | Frame |
| Formula and equations | `Formula`, `Equation Synth` | `formula`, `equation` | Formula compiler | Frame, or audio summary |
| Expressions | `expr` and `glob` lines | none | Expression compiler | `explain` |

File extensions pick the language: `.field`, `.sketch.js`, `.sketch3d.js`, `.glsl`. `--type` overrides.

What makes it expert, not just a wrapper:

- `code docs <language> [topic]` prints the language reference from inside the app, so it matches this version
  exactly. Grammar, API, reserved words, the wrong/right table.
- `code examples <language>` lists working code from the node's own presets, and `code examples <name>` prints one.
- `code errors` gives the same error shape for every language: `{code, message, line, col, hint}`, with new
  codes `E_SKETCH_COMPILE`, `E_SKETCH_RUNTIME`, `E_GLSL_COMPILE`. Hints name the fix, not only the fault, as
  the Field errors do today.
- `code check --look` also renders one frame and reports `frame_stats`, so "compiles but black" is caught.
- `code pull <node>` writes a node's code to a file, so existing patches can be edited from the terminal.
- `param()` knobs declared in Sketch and Field appear in `infinite params`, and can be set and modulated by name.

## Generating patches from the terminal

| Step | Command | What it does |
|---|---|---|
| Start | `patch new --recipe <name> out.inf` | Writes a working starter from `assets/templates`; `patch recipes` lists them |
| Facts | `describe <type>` | Real params, ranges and pins, never from memory |
| Check | `patch check out.inf` | Validate, compile all code, explain, frames, contact sheet, audio summary. One JSON answer, ends at the first stage that fails |
| Tidy | `patch layout out.inf` | Bands, wiring depth and comments, as the authoring skill requires |
| Open | `patch apply out.inf` | Loads into the running app as one undo step, or `--merge` adds it beside the open patch |
| Render | `render out.inf out.mp4` | Final output |

`patch check` reports these, and `--want` lets the caller state what it expects:

| Check | Fails when |
|---|---|
| Loads | Any error or strict warning |
| Code | Any node's code does not compile |
| Reachable | A node feeds neither an Output nor an Audio Out |
| Picture | Frames are black, flat or unchanged over time, when `--want moving-picture` is set |
| Sound | Silent or clipping, when `--want sound` is set |

An agent's loop is then: `context`, `patch new`, edit, `patch check`, fix, `patch layout`, `patch apply`.
A human can use the same commands with no agent at all.

## Parity rule

"Everything from the terminal" is kept honest by a check, not by memory: a script lists app commands and panel
actions and reports any without a verb. New features add their verb in the same change, and that rule goes into
the `new-*` skills once Block B is done.

## Blocks

Three blocks, one branch each, a commit per step. This file is the tracker.

### Block A: the terminal (`feature/terminal-a`)

| Step | Work |
|---|---|
| A1 | Spike: vendor libvterm, render a static screen in a test window, confirm font coverage and dynamic glyph loading |
| A2 | `PtyHost` for macOS and Linux, `TermSession` thread, snapshot, sync filter |
| A3 | Panel: rail tile, glyph, dock, keys, mouse, selection, scrollback, search, tabs |
| A4 | ConPTY on Windows |
| A5 | Quit and update warning, links, drag-in, status dot |

Proof:

| Check | Pass means |
|---|---|
| `INFINITE_TERMINALTEST` self-test | Scripted bytes give the expected grid, including wide characters, alternate screen and resize |
| `vttest` core menus | No wrong result on cursor, screen and character tests |
| By hand, all three platforms | Claude Code, vim, htop, tmux and `git log` run correctly, with no flicker during Claude Code streaming |
| `yes` running for a minute, panel open | Audio bench unchanged. Frame cost of the panel at most 1 ms |
| Panel closed, session running | No main-thread cost |
| Signed macOS build | Shell starts and finds `claude` from a Finder launch |

### Block B: the command (`feature/terminal-b`)

| Step | Work |
|---|---|
| B1 | `infinite` shim and auto-route over today's RPC and headless flags; env injection; multi-instance token |
| B2 | New RPC verbs: modulation, expressions, bypass, groups, selection, view, transport |
| B3 | New RPC verbs: timeline, session, show, Piano Roll, Sample Studio, settings |
| B4 | `watch` stream, `term` verbs, shell integration |
| B5 | Parity script, show refusal (D14), `docs/headless/GUIDE.md`, `AGENT.md`, `AGENTS.md`, help tables |

Proof:

| Check | Pass means |
|---|---|
| Round trip | Every Edit verb, then `save`, then `--validate` passes |
| Same answer | A read verb gives the same result on the live and headless routes for the same patch |
| Undo | One `batch` undoes in one step |
| Show | Writes refused during a show, reads work |
| Two apps | Each terminal drives its own app |
| Shim speed | `infinite --version` under 50 ms |
| Parity script | Zero missing verbs, or each gap listed with a reason |
| `scripts/headless_smoke.sh`, `scripts/live_rpc_smoke.py` | Still pass |

### Block C: code, patches and the agent flow (`feature/terminal-c`)

| Step | Work |
|---|---|
| C1 | One code check for all languages: Sketch and Sketch 3D report through `CodeError()`, GLSL compile in the headless path, the shared error shape |
| C2 | `code` verbs and language references for Sketch, Sketch 3D and Shadertoy in `AISkillContent.h`, generated from the real APIs so they cannot drift |
| C3 | `patch` verbs, layout moved into the app, `patch check` with `--want` |
| C4 | `context`, `guide` and the agent brief; check what an agent wastes tokens on |
| C5 | Agent-run bench: 25 fixed tasks in a fresh Claude Code session, below, against the same tasks through MCP |
| C6 | Fix what the bench shows; every failed task becomes a clearer error, a better doc or a better verb |

Bench tasks:

| Group | Tasks | Example |
|---|---|---|
| Live edits | 5 | Add an LFO to the selected shape |
| Field | 5 | Write a FieldPixel reaction pattern and a Field Synth pluck |
| Sketch 2D and 3D | 5 | Kinetic type in Sketch, a tube sculpture in Sketch 3D |
| GLSL | 3 | Port a two-pass Shadertoy shader |
| Whole patches | 7 | Build and render a 10 s audio-reactive patch from one sentence |

Proof:

| Check | Pass means |
|---|---|
| Broken code | A broken sample per language gives the right code, line and hint |
| Same verdict | `code check` and the app agree on every shipped preset |
| Watch | A broken save keeps the last picture, the next good save lands |
| Docs | Every API name in the references exists, checked by script |
| Generated patches | Open with no pile, pass `patch check`, and show a picture or make sound |
| Bench | Every task finishes with no human help, in fewer tokens and tool calls than the MCP baseline |

## Risks

| Risk | Handling |
|---|---|
| libvterm misses something an agent TUI needs | The `VtEngine` header keeps the swap to one file. A1 and the Claude Code check find it early |
| Windows ConPTY behaves differently | Own step, own check; `windows-parity` skill applies |
| RPC speed: one drain per frame | `batch` covers bulk edits. Behaviour with the window hidden is measured in B1, since an agent may run while the app is in the background |
| Large RPC surface grows untested | Every verb is added to the round-trip check in the same commit |
| An agent can delete files or hurt a patch | Same risk as any terminal. Undo covers the patch, and the shell has only the user's permissions |

## Not in this version

Split panes, user colour themes, image display in the terminal (Kitty or Sixel), Kitty keyboard protocol,
a helper process that outlives the app, a built-in agent, remote SSH helpers, recording sessions.

## Approved surface

Approved by the owner on 2026-10-30. Anything beyond this list goes back for approval first.

| Item | Params | UI/UX | Intuitive? | Works on | Overlap |
|---|---|---|---|---|---|
| Terminal panel and rail tile | Font size, scrollback, shell | As drawn above, docks like every other panel | Behaves like any terminal | macOS, Windows, Linux | None; MCP and headless stay, and this drives them |
| `infinite` command | None | Text only | One name, auto-routes | macOS, Windows, Linux | Wraps the `--` flags, replaces none |
| `watch` and `term` verbs | None | Text only | Agent-facing | macOS, Windows, Linux | None |
| `code` and `patch` verbs | None | Text only | Same steps for every language | macOS, Windows, Linux | Wraps the compilers and the authoring loop |
| Show refusal for agents | `--force` | Error text | Clear message names the flag | macOS, Windows, Linux | Extends `ShowSafety.cpp` |

## Sources

- [Ghostling and libghostty-vt](https://github.com/mthli/ghostling)
- [libghostty-vt Rust bindings, API stability note](https://docs.rs/libghostty-vt/0.2.0/src/libghostty_vt/lib.rs.html)
- [Claude Code flicker and DEC 2026 issue](https://github.com/anthropics/claude-code/issues/37283)
