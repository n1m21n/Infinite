# Chat panel

How to use Infinite's Chat panel. Source: `tools/manual/ui_chapter.py` (the manual's Using the App chapter); `infinite docs ui chat` prints the same text. Edit the source, not this copy.

## Settings: Agents
The Terminal page of Settings has one section, **Agents**: a row per coding agent (Claude, ChatGPT, Gemini) with its status and the button that moves it on, the model each answers with, what each has used, the folders and commands agents may use, and the saved chats. The Chat chapter below explains every row.
Setting up an agent opens a Terminal tab and runs a short script. It puts the `infinite` command on your PATH for every terminal, checks the headless engine, makes sure Node.js is present, fetches the Infinite MCP server and registers it with the agent. Each step checks first and is skipped when it is already done, so the button doubles as a repair. Password prompts (Homebrew, sudo) appear in the tab. Infinite never asks for or stores an API key: the agent's own login runs in the browser. Nothing is installed until you press a button.
Setup is one time. It survives quitting, restarting and updating Infinite: the `infinite` command is a small launcher that finds the current Infinite, and the agent's registration points at that launcher. Each time Infinite starts it quietly checks the steps. If everything holds, nothing shows. If something broke (Infinite moved, the agent was uninstalled, the line was removed from your shell profile), a **Connection** row appears naming the step, with a **Repair** button that reruns only that step.

## Chat
The Chat panel is how you talk to Infinite. Say what you want and the patch changes while you watch. It is the Terminal panel's chat face: the Terminal opens to it, and the Terminal button in its toolbar switches to a plain shell and back. Short commands (add, connect, remove, bypass, undo, modulate, set) run at once inside Infinite with no agent. Anything else goes to the agent you picked, which changes the open patch through the `infinite` command, so every change is listed on a card and can be undone.

### Set up an agent
Infinite talks to three coding agents you install yourself. The chat names them by what people call them: **Claude** (Claude Code), **ChatGPT** (Codex) and **Gemini** (Antigravity). It never asks for or stores an API key; each agent signs in with its own account. The first time the Terminal opens, a welcome asks which agent to connect and has one **Connect** button; **Skip, just a terminal** leaves the chat for later. Settings, Terminal, Agents has one row per agent with its status and the one button that moves it on:

- **Not installed**: Set upThe agent's program is missing. Set up opens a Terminal tab and types the vendor's own installer (Claude and Gemini: the vendor's install script; ChatGPT: `npm install -g @openai/codex`).
- **Needs setup**: Set upInstalled, but the `infinite` command, the MCP link or the skills are not connected yet. Set up runs the one-click connection in a Terminal tab.
- **Setting up… 2 of 5**: The setup script is running; passwords (Homebrew, sudo) are asked for in its tab.
- **Setup did not finish**: Try againA setup step failed; the tab shows which. Fix it there, or run the step by hand, then try again.
- **Not signed in**: Sign inOpens the agent's own sign-in. The page opens in your browser; the row reads Waiting for sign-in… with a Cancel button. A sign-in that is not finished in 3 minutes ends with Sign-in timed out.
- **Signed out**: Sign in againThe agent said its sign-in expired in the middle of a chat. Sign in again and ask again.
- **Ready**: Sign outInstalled, connected and signed in. Sign out is shown when the agent has a sign-out command.

### When something fails

- **<i>Finish in your browser…</i> and no page opened**: The sign-in address is shown in the row; open it by hand. If the page ends with a code (Gemini), paste it into the field that appears and press Submit.
- **<i>Sign in to Claude to continue.</i>**: Press Sign in in Settings, Terminal, Agents (or the chat's agent row), then ask again.
- **<i>hit its usage limit</i>**: The agent's own plan is used up. Wait, or pick another connected agent in the model picker.
- **<i>Could not reach the agent</i>**: No internet, or a proxy blocks it. Check the connection and press Try again.
- **<i>needs its terms accepted once</i>**: Open the Terminal, start the agent and answer its first screen.
- **<i>is not installed. Set it up again.</i>**: The program was removed. Press Set up.
- **<i>It stopped responding, so the chat ended the turn</i>**: The agent went quiet for too long. Ask again; the earlier changes stay on their cards.
- **Setup keeps failing**: Fallback: open the Terminal, install the agent by hand (the commands above), then press Connect again. Each setup step checks first, so rerunning only repairs what is broken.

### The message box

- **@**: Opens the node picker: type to filter the nodes in the open patch, Up/Down to move, Return or Tab to take one, Esc to close. `@cube` names a node (`@cube#2` when there are two), `@cube.detail` one of its controls. The agent receives its exact type, index and values.
- **@claude, @chatgpt, @gemini**: Name an agent to have that agent answer this one message.
- **/**: Opens the command list: type to filter, Return to run.
- **Return / Shift+Return**: Return sends; Shift+Return starts a new line (the box grows to six lines, then scrolls). While an agent is working, Return adds the line to the queue; the Stop button ends the turn.

- **/help**: What the chat can do, with a button per command.
- **/shortcuts, /manuals, /languages, /about**: Answered by Infinite at once: the shortcut list, the three manuals, the built-in code languages, what Infinite is.
- **/website, /github**: Open the website or the repository.
- **/agent <name>**: Switch to a connected agent by name: `/agent claude`. The model picker does the same by choosing one of its models.
- **/model <name>**: Switch the model, for example `/model opus`.
- **/new**: Start a new chat.
- **/undo**: Undo the last change.
- **/explain [topic]**: The agent explains the open patch in plain words.
- **/check**: Validates the patch and its code, one line per problem, with a button to fix what it can.
- **/docs <question>**: Answers from the manuals, found with `infinite docs`, not from memory.

### One model picker
There is one picker, not an agent picker and a model picker. It lists every connected agent's models together, each under its agent's name. Choose a model and that agent answers from then on; the avatar in front of each reply shows who spoke. An agent that is not ready does not appear until you set it up. `/model` and Settings, Terminal, Claude model / ChatGPT model / Gemini model set the same thing. Each agent's list comes from the agent itself, so a new model shows up when the agent offers it; its default model leaves the choice to the agent.

### Files, pictures and paste
Press **+** (Attach files) or drop files on the panel: audio, video, images, GIFs, 3D models, MIDI, patches and code. Cmd/Ctrl+V pastes copied files or a copied picture; text on the clipboard still pastes as text. A pasted picture is saved in the chats folder so the agent can read it. A file still being written is retried until it is complete.
Pictures show as thumbnails in your message and in replies; click one to open it **full size** over the app, fitted to the window. A click or Esc closes it. Videos play in a card and stop when scrolled off screen. GIFs animate; a GIF over 16 MB shows its first frame only. Audio shows a player with a waveform, a folder button (Show in folder) and a waveform button.

### Change cards
After each turn the chat lists what the agent did: **Looked at the patch** for a read-only turn, or **Made 3 changes** with one line per change (old → new). The card shows only this turn's changes, never the whole history. Node names and parameter names on it are links: click a node to select it and bring it into view, click a parameter to jump to that control. A name whose node is gone shows as plain text.
Parameters the agent changed appear as **live knobs** on the card, only the ones that matter for the change. Turn a knob and the patch follows as you play; the agent's value stays as a dot on the track, and a double-click puts the knob back on it. **Undo** on the card reverts the whole turn; a changed line that has been undone reads Undone.
An audio file the agent made shows as an audio card. **Open in a Sampler** (the waveform button) puts that file into a new Sampler node on the canvas, ready to play. **Show in folder** reveals the file.

### Permissions
By default agents may only run `infinite` commands and read the files you attach. When one tries something else, a note Not allowed: … appears with an **Allow** button: Allow folder adds a folder, Allow <command> adds a command, for good. Settings, Terminal, Agents lists them:

- **Agent folder**: Where agents start, and the folder they may read. The open patch's folder by default; Choose… picks another.
- **More folders**: Extra folders agents may read. Add folder… adds one, Remove takes it back.
- **Edit files in them**: Off (default): agents run `infinite` and read attached files only. On: they may also read and change files in the folders above.
- **Allowed commands**: The commands agents may run besides `infinite`, added by Allow in the chat. Remove takes one back.
- **Claude / ChatGPT / Gemini model**: The model that agent answers with; the chat's picker sets the same value.
- **Claude / ChatGPT / Gemini usage**: Turns, tokens, the share of cached input and the cost when the agent reports one. Cached input is re-read on every step of a turn and costs about a tenth of fresh input. **Reset** clears the counts for every agent.
- **Connection**: Only when a step of the setup broke (the command, the MCP link, the skills): names the step; Repair reruns it.
- **Saved chats**: How many chats are kept on this computer. Clear all deletes them after a confirmation.

### Usage and tokens
Under an agent's name on each reply, a faint **count of tokens** shows what that turn used (input, cached input and output together); hover it for the cached share. Chats are kept locally; the **Chats** button in the toolbar lists, searches and deletes them, and **New chat** starts a fresh one.

### Floating windows
Chat, the Viewport, the Modulation Matrix and the Performance panel can leave the dock and float. Right-click the panel's toolbar (or use its dock menu) and choose **Floating Window** after Bottom, Right, Left and Top. The panel opens in its own window over the stage that you move and resize like Settings or Shortcuts; its X closes it. The chat's toolbar also has **Pop out**, which becomes **Dock** while it floats. Choosing any dock position puts the panel back.
