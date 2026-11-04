# Agent collaboration layer

Status: rewritten 2026-11-04 with the owner's additions: chat front end, both panels, @ nodes,
slash commands, session awareness, clean-machine setup. Sections 1-7 await sign-off. T1 built; Block C
refined with setup-as-chat, links, the patch grammar and the agent reply rule (C1-C7); approved 2026-11-04.
Comes before Field Coder (`docs/plans/field-coder/`), which later plugs in as one more executor.

## 1. Goal

A user and any coding agent (Claude Code, Codex, Antigravity, later Field Coder) work on the same
open patch. The user installs nothing by hand, points at a node with `@`, says what they want, and
carries on. The agent edits that node in **this** window. Infinite checks every edit before it lands.

- Front end: a guiding chat panel.
- Back end: the agent running in Infinite's terminal.
- Both panels can be visible at once.

## 2. Problem

What the owner hit, in the built-in terminal: "make the patch". The agent built a different patch
file and said it was in the open one. Then it said it could not see the session.

Root causes, from the code:

| # | Cause | Where | Status |
|---|---|---|---|
| R1 | The MCP server never touches the open window. Its tools (`render_field_pixel`, `validate_patch`, …) are headless only, so an agent using MCP writes and renders new files. | `tools/npm/infinite-engine/lib/server.js` | confirmed |
| R2 | The skill installed into agents teaches "write, check and render .inf patches headless". Nothing says "the open session is your target". | `infinite-patch-authoring` skill | confirmed |
| R3 | The terminal passes `INFINITE_CONTROL_PORT/TOKEN` to its shell. When an agent's tool drops that env, `infinite` falls back to port 7777 + the shared `control_token` file. A second app (old copy, test run) holding 7777 then answers with its own empty patch. | `InfiniteCli.cpp:241`, `RemoteControl.cpp` BindListener | likely: the screenshot showed "untitled, 0 nodes" |
| R4 | No reply says which session it came from, so the agent can't notice it is talking to the wrong one. | all RPC replies | confirmed |

Other gaps:

| Gap | What the user feels |
|---|---|
| Codex install runs before Node is set up | Connect Codex fails on every clean machine |
| Node missing (macOS without brew, unknown Linux, Win10 without winget) | generic "install from nodejs.org" |
| Code applies before it is checked | the picture or sound breaks mid-session |
| Errors come back on a later call | 4-6 turns per change |
| Terminal-only | artists don't know what to type or which node is which |

## 3. Scope

Three blocks, in build order:

| Block | Contains | New UI? |
|---|---|---|
| A It just works | session binding (R1-R4); live MCP tools; clean-machine setup for all three agents | no |
| B Gate | in-app validators on every agent write; typed errors in the same reply; last good state keeps playing | no |
| C Chat front end | chat panel + terminal side by side on one agent session; `@` node picker; slash commands; change cards with Keep / Revert; presence ring; labelled undo; soft hold | yes, needs approval |

Out: Infinite's own model or API keys (the user's agent brings its own); multi-user networking;
Field Coder itself.

## 4. Constraints

- Agent-agnostic. Claude Code, Codex and Antigravity are first class. Others still work as a plain terminal.
- The live picture and sound are the product. A bad agent write never causes a black frame, silence or a dropout.
- Validation runs off the audio thread.
- Existing pieces:
  - `validate_patch_text`, `code_errors` / `CodeIssue`, headless `--validate`, Field compile, SketchCheck
  - `batch`, `PushUndoCheckpoint(label)`, `selection`
  - `INFINITE_TERM_TAB`, the CliSetup 5-step script (`src/core/CliSetup.cpp`)
- No fragile screen-scraping of agent TUIs.
- New UI is shown to the owner and approved before it ships. Short UI text, no hint lines.
- Few users: the smallest change that closes each gap; no compat flags.

## 5. Hypothesis

Make the agent bound to the open session, and check every write in-app with typed errors. Then:
- every agent edit lands in the window the user is looking at
- turns per change drop from 4-6 to ≤ 2
- user interruptions drop to 0

Add a guiding chat front end with `@` and `/`, and a first-time user gets from install to a first
agent edit without opening docs.

## 6. Baseline

Recorded once, before any build, on the M2. Two instances run, the user's window on port 7778 and a
stray one on 7777. Same 30 tasks (section 8), current build:

| Arm | What runs |
|---|---|
| B0 today | each agent in the built-in terminal, current skills and MCP |
| B1 self-check | same, with the agent told to run `infinite --validate` before applying (prompt only) |
| B2 clean machine | Connect each agent on a fresh macOS user account (no brew, no Node), Ubuntu VM, Windows 10 VM without winget |

B1 shows how far a smart agent gets alone. Block B has to beat it.

## 7. Gates

| Gate | Pass |
|---|---|
| Right session | 100 % of agent edits land in the window whose terminal or chat sent them (two-instance test) |
| Clean-machine connect | 3 agents × 3 OS: all connect, or end on one clear per-OS next step |
| Install to first edit | ≤ 3 min on macOS with the agent already signed in |
| Turns per change | median ≤ 2 |
| User interruptions | 0 across the 30 tasks |
| Broken output | 0 black / NaN frames, 0 silent blocks, 0 dropouts from agent writes |
| Gate latency | Field / Sketch check ≤ 50 ms p95; graph check ≤ 10 ms p95 |
| User edit lost | 0 (soft hold test) |
| Frame time | within 1 ms of a no-agent run |

## 8. Methodology

### Block A: it just works

**Session binding.** Every agent route resolves to exactly one window, or fails loudly.

```
chat / terminal tab N in window W
   │ env: INFINITE_CONTROL_PORT, _TOKEN, INFINITE_TERM_TAB=N, INFINITE_SESSION=<W id>
   ▼
agent ──► infinite CLI ─┐
      └─► MCP (live) ───┼─► finds W by INFINITE_SESSION
                        │     env lost? walk parent pids → the app that owns this PTY
                        │     still unknown? error E_NO_SESSION listing open windows; never guess 7777
                        ▼
                    window W ──► every reply carries session { id, patch, title, nodes, selection }
```

| Decision | Choice |
|---|---|
| Where windows register | `AppSupport/instances/<pid>.json` = {pid, port, token}; the pid is the session id; removed on quit; dead pids deleted by the CLI |
| Fallback to 7777 | removed for the terminal and chat. Kept only for a bare outside shell with exactly one window open. |
| MCP tools | MCP gets live tools (`get_session`, `get_graph`, `propose`, `set_param`, `screenshot_node`, `errors`), proxied to the bound window. The headless render tools stay and are labelled "file only". |
| Agent instructions | A 4th shipped skill, `infinite-live-session`, installed with the others: "the open window is your target; run `infinite context`; edit with live verbs; write a .inf only when asked". The patch-authoring skill points to it inside the Terminal. A skill, not edits to the user's CLAUDE.md / AGENTS.md / GEMINI.md. |
| First message context | The chat sends `get_session` output (patch, selection, `@` nodes) with the user's first message, so the agent starts knowing the session. |

**Clean-machine setup.** Fixes to the CliSetup script:

| Fix | Detail |
|---|---|
| Order | setup steps 1-4 run first, then the agent install (step 5). This fixes Codex, which needs npm. |
| No Node needed for MCP | the MCP server moves into the Infinite binary (`infinite mcp`, stdio). Claude Code and Antigravity then need no Node at all. Codex still needs Node to install. |
| Node for Codex | macOS without brew: official `.pkg` link + one line. Linux unknown distro: the distro's command, or the nodejs.org tarball path. Win10 without winget: MSI link. One line per OS, no generic message. |
| Windows execution policy | already ran with `-ExecutionPolicy Bypass -File` (process scope only); checked, no change |
| End state | a per-agent check (`infinite doctor`) that the agent starts, sees the MCP server and reaches this window |

### Block B: the gate

Every agent write goes through one path.

```
agent ──► propose(calls[], label)                          RPC, MCP tool, `infinite apply`
            ├─ 1 shape   ids, keys, enum values, ranges         < 1 ms
            ├─ 2 graph   would-be patch ─► validate_patch_text  ≤ 10 ms
            ├─ 3 code    Field / Sketch / GLSL / Formula compile on a worker   ≤ 50 ms
            ├─ 4 smoke   1 frame / 1 audio block of changed nodes: not black,  ≤ 150 ms
            │            no NaN, peak < 0 dBFS (on for code and new nodes)
            ├─ fail ─► { ok:false, errors:[typed, with near + hint] }   live graph untouched
            └─ pass ─► one batch, one undo step "Agent: <label>" ─► { ok:true, undo_id, session }
```

| Question | Decision |
|---|---|
| New verb or change `batch`? | new `propose`; `batch` stays raw for scripts |
| Error shape | `CodeIssue` + `line`, `col`, `hint`, `near` (closest key / enum list / node) |
| Code fails in a running node | old code keeps running |
| Partial pass | no, all or nothing |

```json
{ "ok": false, "errors": [
  { "code": "E_FIELD_COMPILE", "node": 7, "line": 4, "col": 12, "message": "unknown name 'radious'",
    "near": ["radius"], "hint": "names are bare: radius, not p.radius" },
  { "code": "E_PARAM_ENUM", "node": 4, "key": "type", "message": "'Bricks' is not a type",
    "near": ["Brick", "Voronoi", "Magic", "Wave"] } ] }
```

### Block C: chat front end

Refined 2026-11-04 from the owner's notes: setup happens in the chat, the chat is quiet, every node and
param it shows is a link, and a short patch grammar runs exact edits without the agent.

The chat is a front end on the same agent session the terminal shows. It never builds its own agent.
It lives inside the app, so a direct command calls the same RPC handler as the matching `infinite`
verb. The chat and the CLI cannot drift.

**C1. One panel, two faces.** A glyph toggle in the panel header (chat ⇄ terminal) flips the same
session between the clean chat and the full TUI. Both can also be open at once as two docked panels.

```
┌ Agent ─────────────────────────────── [chat|term] ┐
│ you   @cube modulate detail with @lfo             │   user line: quiet well, chips inline
│       from 0.5 to 0.8                             │
│ ┌ ⚡ ran directly ───────────────────────────────┐ │   change card
│ │ @lfo → @cube.detail   0.5 – 0.8                │ │
│ │ = infinite mod lfo cube detail 0.5 0.8   [Undo]│ │   the CLI line it equals
│ └────────────────────────────────────────────────┘ │
│ ✦ make the rings feel like breathing               │   no grammar match: goes to the agent
│ ▸ 3 steps                                          │   tool calls folded to one row
│ Slowed @rings.speed to 0.2 and added an @lfo on    │   agent text: plain, no bubble
│ @rings.width.                                      │   every node/param is a link
│ ┌──────────────┐                                   │
│ │  [image]     │ out.png                           │   agent's .png/.jpg shown inline
│ └──────────────┘                                   │
│ @_ ______________________________________ ⚡ [↵]   │   badge: ⚡ direct  ✦ agent
└───────────────────────────────────────────────────┘
```

**C2. Setup is the first conversation.** The old Terminal welcome screen goes. A first-run chat
opens with Infinite's own short lines, the answer chips, and the 5 setup steps as one live checklist.
The setup script still runs in a terminal tab behind it.

```
Infinite   Which agent should work with you?
           [Claude Code] [Codex] [Antigravity] [Just a terminal]
you        Codex
Infinite   Setting up Codex
           ✓ 1 Infinite on PATH
           ✓ 2 headless engine
           ● 3 Node.js 22 …            failure: the step's one-line reason + [Retry] [Show terminal]
           ○ 4 MCP server
           ○ 5 Codex + skills
Infinite   Codex is connected to this window. Try:  @cube set size to 2
```

**C3. Every node, param and value is a link.** The chat keeps a catalog of the window: node titles,
types and indices, and for each node its keys, labels, ranges, enum values and whether a key can be
modulated. It refreshes on every graph change, because it is in-process.

| Chip | Looks | Click |
|---|---|---|
| node `@cube` | node's category colour dot + title | select + frame it on the canvas |
| param `@cube.detail` | neutral chip; amber when modulated, violet with an expression, green when predicted (role colours) | select node, flash that control |
| value `0.5` in a card | mono readout | none |
| file `out.png` | inline image, max panel width | open full size |

How agent text is matched. The live-session skill gives the agent one reply rule, "How to write
replies": nodes as `@<title>#<index>`, params as `@<title>#<index>.<key>`, changes as `old → new`,
modulation as `@src#i → @dst#j.key lo–hi`, pictures as a path on their own line, and a closing
`Changed:` list. The `#index` ends the title, so titles with spaces or repeated titles still link
exactly. In the terminal face the same text reads plainly. The chip shows the param's label from
the catalog, not its key. A fallback links a backticked node title. A bare param word links only
when its node is named in the same sentence, so prose like "more detail" stays plain. A node that
no longer exists renders struck through.

**C4. The patch grammar.** Short words people already say. A line that parses fully runs directly
(⚡: one batch, one undo step, milliseconds). Anything else goes to the agent (✦). `@agent` forces
the agent route.

| Say | Runs |
|---|---|
| `@a modulate <key> with @b [from x to y]` | `mod b a key x y` |
| `@a set <key> to v` · `@a <key> to v` | `set a key v` |
| `@a <key> = sin(t)` | `expr a key "=sin(t)"` |
| `connect @a to @b` · `@a to @b` | `connect a b` |
| `add <Type> [after @a]` | `add` + `connect` |
| `remove @a` · `bypass @a` · `undo` | `remove` · `bypass` · `undo` |
| `… and …` | clauses run as one batch, one undo |
| `… or …` | never direct: the agent picks, or tries both |
| `@agent …` | always the agent, with the chips as context |

Example: `@cube modulate detail with @lfo from 0.5 to 0.8 and @oscillator modulate frequency with @random`
is two `bind_mod` calls in one batch with one undo step. No agent turn.

**C5. Dropdowns follow the slot.** The composer always knows which slot is next.

| After | Dropdown shows | Keys |
|---|---|---|
| `@` | nodes: selected first, then recently touched, then by name; canvas click picks too | ↑↓ move, Tab/↵ pick, Esc close |
| `@node ` | that node's params: label, value, range; ◆ marks modulatable | type to filter |
| `with` | nodes with an output that fits: modulators first | |
| `from` / `to` / `set … to` | current value and range; enums list their values | ↑↓ nudge one step, Shift fine |
| `/` | commands | |

A word that does not fit its slot gets a red underline, and the dropdown shows the nearest valid
choices. A value out of range snaps to the range edge in the dropdown, never silently.

**C6. Slash commands.** `/` opens actions that are not patch edits. The verbs also exist as `/`
templates that insert the sentence with tab stops (`/modulate` → `@_ modulate _ with @_`).

| Command | Does |
|---|---|
| `/fix [@node]` | the agent reads errors on the node (or all) and fixes them through the gate |
| `/explain @node` | what the node does and what each param is set to |
| `/look @node` | screenshot the node's output into the chat and to the agent |
| `/listen @node` | audio summary (pitch, level, spectrum) |
| `/render` | render the current output to a file |
| `/session` | what the agent sees: window, patch, selection |
| `/terminal` | flip to the terminal face |

**C7. Quiet by default.** The chat shows the user's lines, the agent's prose, change cards,
images, permission requests (Allow / Deny) and errors. Tool calls fold into one `▸ N steps` row that
opens to the raw lines. Everything else stays in the terminal face.

| Piece | Decision |
|---|---|
| Back end | Agent's structured mode (Claude Code `stream-json` first; Codex and Antigravity adapters checked before building). No structured mode → the chat shows the terminal face only. |
| Change cards | One per change, direct or agent: chips of what changed, the equal CLI line, Undo. They auto-keep. |
| Presence | Thin ring on nodes inside an agent change; fades 1.5 s after. Never moves camera, selection or focus. |
| Soft hold | A node the user is editing rejects agent writes with `E_USER_EDITING` + `retry_after_ms` |
| History | Undo menu shows "Chat: <line>" or "Agent: <label>" |
| Look | Existing components only: FieldWell (composer, user lines), ChipButton (chips), NoticeCard (change card), CodeBlock (CLI line, folded steps), BusyLine (agent working), MenuParts (dropdowns, no chevrons), GlyphToggle (chat ⇄ terminal). New glyphs: chat, send, direct (bolt), agent (spark), via `build_glyphs.py`. No hint text under controls. |

Gaps this needs filled first:

| Gap | Fix |
|---|---|
| `get_params` returns key → value strings only | `get_params` gains `full:true`: label, range, step, enum values, modulatable, mod/expr state (the `--describe` data, per node) |
| No "show me this node" call | `focus {index, key?}`: select, frame, flash the control. Also an `infinite focus` verb. |
| Agent does not write links | done in T1: the live-session skill's reply rule (see C3) |

### Test set: 30 tasks

| Group | Count | Examples |
|---|---|---|
| Param | 6 | brick texture on cube; 440 Hz sine; rings width 0.003 |
| Graph | 6 | cube → sphere; blur after Pixel; LFO to cutoff |
| Code | 8 | concentric rings; pulse at 0.5 Hz; Sketch 3D spiral of boxes; Field Synth FM bell |
| Fix | 4 | seeded typo, wrong enum, missing wire, NaN divide |
| Session | 3 | two windows open; agent in window B edits only B; env stripped; second app on 7777 |
| Collab | 3 | user drags a slider on the node the agent edits; user deletes a referenced node; edit while audio plays |

The driver (`tools/agent-collab/`) runs each task per agent and logs turns, interruptions, broken
frames, wrong-session hits and wall time. Correctness is scored with the field-coder oracles.

## 9. Trajectories

| Path | Covers | When |
|---|---|---|
| T1 | A: session binding + setup order + Node messages | first; fixes the owner's bug and Codex |
| T2 | A: `infinite mcp` native + live MCP tools | next; removes Node for 2 of 3 agents |
| T3 | B: gate | then |
| T4 | C: chat + terminal faces, setup-as-chat, links, grammar, dropdowns, `/`, cards | approved by the owner 2026-11-04 |
| T5 | Field Coder behind `propose` | only if it passes its own speed gate |

## 10. Results

**T1 (2026-11-04, branch `agent-collab/t1`).**

| Piece | State | Proof |
|---|---|---|
| Instance registry `instances/<pid>.json` | built | written on bind, removed at exit |
| CLI resolve: env → INFINITE_SESSION → parent-pid walk → one window → loud fail | built | two live fake windows: exit 7 with both pids; stale file deleted; INFINITE_SESSION picks one and never falls back |
| Terminal sets INFINITE_SESSION; `status` and `context` name the window | built | |
| Agent install moved into setup step 5, after Node | built | CLISETUPTEST 66/66, incl. "installs a missing agent at step 5, after Node" |
| Per-OS Node lines (too old / macOS / Linux / Windows) | built | script text; Linux and Windows end-to-end runs still to do |
| `infinite-live-session` skill, with the reply rule | built | ships as the 4th skill |
| Parent-pid walk inside a real agent shell | not yet run | needs the owner's two-window repro |

**T4 (2026-11-04, branch `agent-collab/t4`).**

| Piece | State | Proof |
|---|---|---|
| `get_params --full`, `focus`, batch `$N` refs | built | CHATTEST |
| Patch grammar: modulate / set / = / connect / add / remove / bypass / undo, `and` batches | built | CHATTEST: the owner's two-clause example runs as one batch with its CLI lines |
| Reply links `@title#index[.key]`, backticked titles, bare keys, images, code | built | CHATTEST |
| Claude and Codex stream parsers, resume args | built | CHATTEST |
| Agent runner (login shell, stop, last words on a crash) | built | CHATTEST with a stand-in agent: events in order, crash text, quiet stop |
| Chat face: messages, chips that frame the node, change cards with Undo, folded steps, images | built | builds; not yet looked at in the app |
| Composer: span colours, route badge, per-slot dropdown, ↑↓ nudge, history | built | builds; not yet looked at in the app |
| Terminal face toggle resumes the same session | built | resume line in CHATTEST |
| Setup as the chat's opening message | built | CLISETUPTEST still passes |
| A real `claude -p` turn (the chat's exact args, via the login shell) | run | 2026-11-04, Claude Code 2.1.318: session id kept, text and result read; CHATTEST with INFINITE_CHATTEST_CLAUDE_LOG |
| `--resume` turn, `codex exec` turn | not yet run | |
| `/` commands | not built | |

## 11. Critique

- **Agent modes differ.** Structured modes and resume commands differ by agent, and some may lack one. Then the chat degrades to the terminal. Each adapter is checked against the agent's own docs before building.
- **Native MCP is the largest single piece.** It is a stdio JSON-RPC server over verbs that already exist. If it slips, T1 still fixes the bug through the CLI and the instruction note.
- **Removing the 7777 fallback can break a user's outside script.** That only happens when two windows are open; one window keeps working.
- **The smoke check catches breakage, not taste.**
- **Change cards could nag.** They auto-keep, and there is one per change.

## 12. Next steps

1. Owner signs off sections 1-7.
2. Driver + 30 tasks in `tools/agent-collab/`; record B0, B1, B2.
3. T1: session binding, setup reorder, per-OS Node lines, instruction note; rerun B0.
4. T2: `infinite mcp` with live tools.
5. T3: the gate; keep only if it beats B1.
6. T4 built; owner looks at the chat in the app and runs one real agent turn.
