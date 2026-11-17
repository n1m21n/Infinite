# Chat panel

The chat is the front face of the Terminal panel. You say what you want in plain words, name nodes with `@`, and a coding agent (Claude Code, Codex or Antigravity) changes the open patch. Every change is checked before it lands and is listed under the reply with Undo. Infinite never asks for or stores an API key: each agent signs in with its own account.

The same page is in the Node Field Guide (Using the App). The agent side is in `docs/plans/agent-collab/README.md`.

## Set up an agent

1. Open the Terminal panel. The chat is the default face; the Terminal button in the chat header switches to the shell (Cmd+` on macOS, Ctrl+` elsewhere).
2. The empty chat says **Connect an agent**. Pick one and press **Set up**. Infinite installs the agent's command line if it is missing, puts the `infinite` command on your PATH, registers Infinite's MCP server with the agent and installs the live-session skill. Each step checks first and is skipped when done, so Set up is also the repair.
3. When the agent needs a login, **Open sign-in page** opens your browser. If the agent asks for a code, paste it under **Paste the code from the browser** and press Submit.
4. The row reads **Ready**. Type a line and send.

Status words (chat and Settings > Agents):

| Status | Meaning | Button |
|---|---|---|
| Checking… | Looking at what is installed and signed in | none |
| Not installed | The agent's command is missing | Set up |
| Needs setup | Installed, but the Infinite link is missing | Set up |
| Setting up… / Signing out… | A step is running | none |
| Waiting for sign-in… | Browser sign-in is open | Cancel |
| Not signed in | Installed, never signed in | Sign in |
| Signed out | Was signed in; the login ran out | Sign in again |
| Setup did not finish | A step failed | Try again |
| Ready | Can take turns | Sign out |

### When it fails

| What you see | What to do |
|---|---|
| Setup did not finish | Try again. The failing step is named; steps that passed are skipped. Password prompts (Homebrew, sudo) appear in a Terminal tab. |
| Not installed after Set up | Install the agent from its vendor and press Set up again. No network or no Node.js are the usual causes; Infinite sets Node.js up when it can and says what is missing when it cannot. |
| Sign-in timed out / did not finish | Try again. Keep the browser tab open until it says you are done. |
| Signed out | The login expired. Sign in again; your chats and settings stay. |
| It stopped responding, so the chat ended the turn | The agent went quiet. Try again; a lost agent conversation restarts once by itself. |
| Connection row in Settings > Agents | A piece of the plumbing broke (the `infinite` command, the MCP link, the skill). Repair reruns only that piece. |
| Antigravity or Codex will not set up | Use the Terminal button, install and start the agent in the shell yourself; `infinite` is on its PATH. |

Fallback: the chat is optional. The Terminal still starts any agent in the patch's folder, and `infinite` works from any shell.

## Talking to it

- **Enter** sends, **Shift+Enter** breaks the line. While an agent works, a new message joins the queue shown above the box; click a row to edit it, × to drop it.
- The agent name on the right of the tool row picks who answers. `@claude`, `@codex` or `@antigravity` in the line sends the turn to that agent; name two and they answer side by side.
- A line the patch grammar reads fully (for example setting a value on a node) runs at once, with no agent, as one undo step.

### `@` picks a node or an agent

Type `@` for the picker: nodes in the open patch first, then agents. Keep typing to filter by title, type or number. Picking one writes it into the line as a chip. The agent sees which node you mean, so "make @Noise#2 slower" edits that node. Right-click a node on the canvas and choose **Reference in chat** to do the same.

### `/` commands

Type `/` for the list. A command that is not on it says so.

| Command | What it does |
|---|---|
| `/help` | What the chat can do |
| `/shortcuts` | Every keyboard shortcut |
| `/manuals` | The manuals and where to read them |
| `/languages` | The code languages built in |
| `/about` | What Infinite is |
| `/website`, `/github` | Open the website or the GitHub page |
| `/agent claude` | Switch agent (claude, codex or antigravity) |
| `/model opus` | Switch model; `/model default` goes back |
| `/new` | Start a new chat |
| `/undo` | Undo the last change |
| `/explain` | Explain this patch (an agent answers) |
| `/check` | Validate the patch and its code |
| `/docs <question>` | Ask the manuals: `/docs how does Field work` |

Paths such as `/Users/me` are not read as commands.

### Model and agent pickers

The pills on the right of the tool row pick the agent and its model for the next turns. Claude Code offers Sonnet, Opus and Haiku; Codex and Antigravity list the models their own command reports. A pick lasts for the chat; Settings > Agents keeps the model each agent starts with.

### Attach files

The **+** button, or dropping files on the chat, attaches them as chips above the box (× removes one; extra chips collapse into a count with every path in the tooltip). Pictures, sound, video, 3D models, MIDI, SVG, patches, Field programs and text or code files attach; anything else is ignored. The agent receives the paths. Files an agent makes come back in its reply: pictures and sound play inline, other files show a **Show in folder** button.

## What an agent did

A turn shows one folded row, **Working…** with the time, then **Made N changes** (or **Looked at the patch**). Unfold it for one plain-words row per step. A **Changed** card lists what moved in the patch, diffed before and after, with **Undo** to roll that turn back. Failed steps are counted on the row; **Did not run** marks a card whose change the check rejected; the last good state keeps playing.

Nodes and parameters named in a reply are links. A node link is a chip in the node's colour: click it to frame that node on the canvas, hover for its type and number. A parameter link goes to that parameter and shows its current value on hover (a dot marks one that is modulated or an expression). A name that is no longer in the patch is struck through and reads **Not in the patch**. An agent may also offer buttons in its reply (add a node, set a value, undo, open a panel).

Code in a reply shows in a block with a Copy button; **Open code** / **Close code** shows or hides it beside the reply.

### Tokens per turn

The agent's name above each reply is followed by that turn's token count. Hover it to see how many of them were read from the cache (cached input costs about a tenth of fresh input). The same totals add up per agent in Settings.

## Settings > Agents

| Row | What it does |
|---|---|
| Claude Code, Codex, Antigravity | Status of each agent, and its one button: Set up, Sign in, Sign in again, Sign out, Cancel, Try again. Sign-in codes are pasted on the row. |
| `<Agent>` model | The model that agent starts with. Default leaves the choice to the agent. |
| `<Agent>` usage | Turns, tokens, share read from the cache and cost, per agent, since the last reset. **Reset** clears all of them. Only agents with turns appear. |
| Connection | Shows only when the plumbing is broken; **Repair** fixes it. |
| Agent folder | Where agents run. Default: the open patch's folder. Choose… picks another. |
| More folders | Extra folders agents may open. Add folder… / Remove. |
| Edit files in them | Off: agents read the extra folders only. On: they may write there too. |
| Allowed commands | Commands agents may run without asking, added from a **Not allowed** note under a reply (**Allow**) and removable here. |
| Saved chats (N) | **Clear all** deletes every saved chat after a confirmation. |

When an agent is stopped by a permission, the reply carries a **Not allowed: …** note with one button: **Allow folder** or **Allow `<command>`**. It is added to the lists above and applies from the next turn.

Chats are saved on this machine and come back from the history button in the chat header, with their agent sessions. Search chats filters them.
