# Command line & agents: one-click setup

Status: planned and built on `feature/cli-setup` (2026-11-02). macOS verified by script run with a fake HOME and stub tools; Windows and Linux scripts are uncompiled/unrun until CI and a manual pass. Parent plan: `docs/plans/terminal/README.md`. Same pattern as the Apple Watch installer (`docs/plans/apple-watch/README.md`): a bundled script runs visibly in Infinite's Terminal and writes a status JSON; the page reads only that JSON. No dependency on that branch.
Skills before building: `windows-parity`, `linux-parity`, `build-discipline`.

## Goal

Settings has a "Command line & agents" section. One button, "Set up everything", opens a new Terminal tab and runs a setup script there (so a `sudo` or `brew` prompt works). When it ends, a green tick per step shows. A "Coding agent" row installs Claude Code if missing and starts it in the open patch's folder with Infinite's MCP server already registered.

## What exists today

| Piece | Where | Gap |
|---|---|---|
| Terminal panel, tabs, `NewTab` (cwd = open patch's folder) | `src/app/panels/TerminalPanel.cpp:139` | No way for other code to open a tab and type a command |
| `infinite` shim | `ShimDir()` in `TerminalPanel.cpp:92`, `<AppSupport>/term-bin/<hash>/infinite` | Only on PATH **inside** Infinite's terminal (`AddCommandEnv`) |
| Typing into a tab | `TermSession::SendBytes` (`src/core/term/TermSession.h:74`); `term_send` RPC | Used by the RPC only |
| `--cli` verbs, headless flags | `src/core/InfiniteCli.cpp`, `src/core/HeadlessJob.cpp` (`--version`, `--frame`, `--render` ...) | Linux needs a virtual display (`xvfb-run`); nothing checks it |
| MCP server | npm `infinite-engine` (`tools/npm/infinite-engine`), run as `npx -y infinite-engine serve`; `INFINITE_BIN` points it at the app | Needs Node 18+; user must run `claude mcp add` by hand |
| Settings Terminal page | `SettingsWindow.cpp`, tab 8: Terminal panel, The infinite command, Headless engine, AI skills (text only) | No action, no state |
| Persistence of small flags | sidecar files in `AppPaths::AppSupportDir()` (`UpdateCheck` `update-check-off`) | Reuse the idea |

## The picture

```
 Settings > Terminal > "Command line & agents"
   [Set up everything]  [Agent: Claude Code v]  [Install]  [Start agent]  [x] start on open
        |                                                         |
        | writes <AppSupport>/cli-setup/setup.sh|.ps1             | types ONE line into a new tab
        v                                                         v
   TerminalRunInNewTab("Infinite setup", "sh '<...>/setup.sh'")   (Claude Code runs in patch folder)
        |
        v   visible in the tab, sudo/brew prompts work
   Step 1/5 ... Step 5/5   --writes-->  <AppSupport>/cli-setup/status.json
                                                |
   Settings page polls the file (1 s, only while the section is on screen) -> green ticks
   (page never reads terminal text, never spawns a shell)
```

## Steps (what the script does)

| # | Step id | Checks first | Does if needed | Failure shows |
|---|---|---|---|---|
| 1 | `path` | `~/.local/bin/infinite` holds the current exe path; `~/.local/bin` already on PATH or its rc block present | writes the shim; appends a marked PATH block to the shell rc | detail with the file it could not write |
| 2 | `headless` | `"$EXE" --version` runs; Linux: `xvfb-run` or `$DISPLAY` | Linux only: installs `xvfb` through apt/dnf/pacman with `sudo` | "install xvfb" with the exact command |
| 3 | `node` | `node` 18+ and `npx` | macOS: `brew install node`; Linux: apt/dnf/pacman with `sudo`; Windows: `winget install OpenJS.NodeJS.LTS` | link to nodejs.org when no package manager exists |
| 4 | `mcp` | `INFINITE_BIN=$EXE npx -y infinite-engine status` finds the app | the same call downloads the package into the npx cache | npx error line |
| 5 | `agent` | agent installed (`command -v`); MCP entry `infinite` points at `$EXE` | `claude mcp remove` + `claude mcp add` when missing or stale | "waiting": agent not installed yet (not a failure) |

Each step ends in one of `ok`, `skipped` (nothing to do), `failed`, `waiting`. Ok and skipped both draw a green tick. The script prints `Step N/5: <what>` and `   ok|skipped|FAILED: <detail>` per step and never stops at the first failure (later steps that do not depend on it still run; 4 needs 3).

## Status JSON

```
{ "version": 1, "os": "macos", "agent": "claude", "exe": "...",
  "running": false, "startedAt": 1762080000, "finishedAt": 1762080042,
  "steps": [ {"id":"path","state":"ok","detail":"~/.local/bin/infinite"}, ... ] }
```
The page treats `running:true` older than 15 minutes as dead (script killed with the tab). A missing or unparsable file means "not set up yet": all steps grey.

## Agents (a table, not code)

`CliSetup::Agents()` rows: `id, name, binary, installUnix, installWin, launch, mcpCheck, mcpAdd, mcpRemove, loginHint`.

| id | Claude Code (first row) |
|---|---|
| install | macOS/Linux `curl -fsSL https://claude.ai/install.sh \| bash`; Windows `irm https://claude.ai/install.ps1 \| iex` |
| installed? | file probe on PATH + `~/.local/bin`, `/opt/homebrew/bin`, `/usr/local/bin`, `~/.claude/local` (the page cannot spawn a shell, and a Finder-launched app has a short PATH) |
| launch | `claude` |
| MCP | check `claude mcp get infinite`; add `claude mcp add --scope user -e INFINITE_BIN=<exe> infinite -- npx -y infinite-engine serve` |
| login | none stored by Infinite: Claude Code asks for its own login in the terminal on first start |

Adding Codex or Gemini later is one more row.

## Decisions (all made)

| # | Question | Decision | Why |
|---|---|---|---|
| D1 | Where does the UI live? | New first section "Command line & agents" on the Settings Terminal page | The page already holds the terminal/CLI/headless text; section is above it |
| D2 | Where does the script live? | Embedded in the binary as a string; written to `<AppSupport>/cli-setup/` when a button is pressed | No change to the three packagers; the exact script that runs is the one the page shows |
| D3 | How do agent fields reach the script? | C++ prepends shell-quoted variable assignments to the script body | One file, nothing to parse, nothing passed through the environment |
| D4 | How does the page learn state? | Reads `status.json` only; agent "installed" by file probe | Never parses terminal text, never spawns |
| D5 | Show commands first? | The section prints the exact line that will be typed and a collapsible "Show the script" with the full text. Nothing runs until a button is pressed | Requirement 5 |
| D6 | Installing prerequisites | Script installs Node (brew/apt/dnf/pacman/winget) and Linux `xvfb`; the preview lists them as "only if missing". No installer of Homebrew itself | Needs the visible terminal for passwords; stops at a link when no package manager exists |
| D7 | PATH, permanently | Shim in `~/.local/bin` plus a marked block in the shell rc (zsh `.zshrc`, bash `.bash_profile` on macOS / `.bashrc` on Linux, else `.profile`). Windows: `%LOCALAPPDATA%\Infinite\bin` + user PATH | Same shape as other CLIs; marked block makes reruns and removal clean |
| D8 | Rerun | Every step checks first; the shim is rewritten only if its text differs (app moved) | "Set up everything" doubles as repair |
| D9 | Terminal tab API | `TerminalRunInNewTab(title, line)` in `TerminalPanel.cpp`: opens the panel, adds a tab, types the line plus Enter | The terminal already types via `SendBytes`; no new RPC |
| D10 | Agent start line | `export PATH=~/.local/bin:$PATH; <mcpCheck> >/dev/null 2>&1 \|\| <mcpAdd>; <launch>` so MCP is registered even when setup was never run | "MCP already registered" holds |
| D11 | Auto-start | Toggle persisted in `<AppSupport>/cli-setup/config.json` with the chosen agent. When the panel creates its first tab and the toggle is on and the agent is installed, the line is typed into that tab. Off by default | Never surprises a user; one tab, not on every tab |
| D12 | Keys | Infinite never asks for, stores or logs an API key | The agent's own login flow runs in the tab |
| D13 | Windows | `setup.ps1`, run as `powershell -NoProfile -ExecutionPolicy Bypass -File "<path>"`. Same five steps with PowerShell checks. Written but not run by me; marked "unverified on Windows" in the status line of this doc | `windows-parity`: honest about what is unrun |
| D14 | Linux | Same `setup.sh` (branches by `uname`); apt/dnf/pacman only | `linux-parity` |
| D15 | Debug tools | None. The selftest hook is an early exit like every other `INFINITE_*TEST`; no Settings debug UI | Release rule |
| D16 | Legacy | No compat flags: status `version` 1 is the only version; unknown versions read as "not set up" | Scale-to-users rule |
| D17 | Testability | `CliSetup` is a core module with no ImGui and no process spawn. `Describe()` turns (status, agent probe, config) into the view the page draws. The runner is a `std::function` the test replaces | Requirement 6 |
| D18 | One-time setup that survives quit, restart and update | Everything the script does lives outside the running app. The `infinite` command is a small launcher at `~/.local/bin/infinite` (and `infinite-app`, used as the MCP `INFINITE_BIN`), never a path inside one bundle. The launcher reads `<AppSupport>/cli-setup/exe`, which Infinite rewrites at every start, then falls back to `/Applications/Infinite.app`, `~/Applications/Infinite.app` (Linux `/opt/Infinite`, Windows `Program Files\Infinite`). The PATH line sits once in the shell profile between markers (checked before writing, never duplicated). The MCP registration lives in the agent's own config and names the launcher. Status JSON stays in Application Support. The auto-start toggle and agent choice are saved in `cli-setup/config.json` beside it, read at launch | Owner request 2026-11-02 |
| D19 | Silent re-check at launch | A worker thread, file stats and one small file read only (no shell, nothing on the main thread): launchers present and the recorded app exists, PATH line or folder on PATH, node/npx found, agent binary found, agent config still names `infinite-app`. Runs only when a status file shows setup was done before. All fine: nothing shows. Else that step turns amber with the reason and a Repair button | Owner request 2026-11-02 |
| D20 | Repair reruns one step | Repair writes the same script with `ONLY=<step>` and seeds the other steps from the last status.json, so their ticks stay. Same visible Terminal tab, same exact-text rule. The check runs again when the run finishes | D19 |

## Files

| File | Change |
|---|---|
| `src/core/CliSetup.h/.cpp` | agent table, status parse, script builders (sh, ps1), command lines, config, `Describe()` |
| `src/app/panels/CliSetupSection.cpp` | draws the section from `Describe()`; polls status |
| `src/app/panels/TerminalPanel.cpp` | `TerminalRunInNewTab`, auto-start in the first-tab branch |
| `src/app/AppShared.h` | declarations |
| `src/app/panels/SettingsWindow.cpp` | calls the section at the top of tab 8 |
| `src/app/selftest/CliSetupTests.cpp`, `Startup.cpp` | `INFINITE_CLISETUPTEST` |
| `CMakeLists.txt` | three sources |
| `src/app/panels/HelpWindows.cpp`, `tools/manual/ui_chapter.py` | help text and manual entry |

## Tests

- `INFINITE_CLISETUPTEST` (early exit, no window, no shell, no installer): fake `status.json` files walk the section through none, running (2 of 5), all ok, one failed, agent missing, agent present, stale running, garbage JSON, unknown version; asserts ticks, button labels/enabled flags, and that clicking Set up / Install / Start hands the exact expected line to a fake runner; checks generated scripts contain each step and every quoting case (spaces and a single quote in the exe path).
- Script run on macOS with `HOME` set to a temp folder and stub `claude`/`npx`/`node` on PATH: first run, second run (all skipped), moved exe (shim rewritten).
- Manual on the owner's Mac: see the final report.

Added for D18 to D20: launcher text identical on every run (rewritten only when different); "app moved": the record is stale and the old file gone, so the step is amber until the next start rewrites the record, and after it the launcher resolves; profile line removed, agent uninstalled, registration gone, node removed each turn exactly their own step amber; Repair types one sh line and seeds the others from the last status. Covered by the CLISETUPTEST cases 11.
