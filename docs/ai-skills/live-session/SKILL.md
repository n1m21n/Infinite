---
name: infinite-live-session
description: Edit the Infinite window the user has open, live. Use whenever INFINITE_SESSION is set or you run inside Infinite's Terminal, and the user asks to build, change, wire, modulate or fix anything in their patch. Read `infinite context` first; never write a new .inf unless asked.
---

# Edit the open Infinite window

You are running inside Infinite's Terminal panel. The user is looking at a live patch in that window,
and every request is about that patch unless they name a file.

1. Run `infinite context` first. It returns the window's status, the selected nodes, every node with
   its index, and any code errors. Use `infinite params <node>` to see a node's keys and ranges.
2. Change the patch with live verbs. Each one is applied to the window at once, can be undone, and
   the user sees it happen:

   | Want | Verb |
   |---|---|
   | add a node | `infinite add <Type>` |
   | wire two nodes | `infinite connect <src[:out]> <dst[:slot]>` |
   | set a value | `infinite set <node> <key> <value>` |
   | modulate a value | `infinite mod <src[:out]> <dst> <key> [lo hi]` |
   | expression on a value | `infinite expr <node> <key> "=sin(t)"` |
   | many changes, one undo | `infinite batch -` (JSON array of {method, params} on stdin) |
   | take it back | `infinite undo` |

   A node can be its index or its name, as `infinite context` lists them.
3. Do not write a new .inf file, and do not run `infinite render` or the headless engine on a copy,
   unless the user asks for a file. Headless tools never touch the window.
4. After a change, run `infinite context` again and read `codeErrors` before you say it is done.

## How to write replies

The user reads your reply in Infinite's chat, where every node, param and value you name becomes a
link to it. Name things the way the app shows them, in these exact forms:

| Thing | Write | Example |
|---|---|---|
| node | `@<title>#<index>`, title and index as `infinite context` lists them | `@rings#7`, `@Field Pixel#3` |
| param | node, then `.<key>` from `infinite params` | `@rings#7.width` |
| value set | param `=` value, with the unit the node shows (Hz, s, dB, beats, %) | `@osc#2.frequency = 440 Hz` |
| value changed | old `→` new | `@rings#7.width 0.010 → 0.003` |
| modulation | source `→` param, then the range | `@lfo#4 → @cube#1.detail 0.5–0.8` |
| expression | param `=` the quoted formula | `@cube#1.scale = "=sin(t)*0.5+0.5"` |
| cable | source `→` destination, with pins when not 0 | `@noise#5 → @blur#6:1` |
| new node | `added` + the node | `added @blur#6` |
| picture | the file path on its own line | `/tmp/look.png` |

Rules:
- Use the node type's name as the app shows it (`Field Pixel`, `LFO`), never an internal class name.
- Say what changed, not how: no RPC JSON, no CLI output, no code unless the user asked for code.
- After a change, end with a short `Changed:` list, one line per change in the forms above.
- Before you edit, check names against `infinite context`. A node you name that does not exist shows as a broken link.

If `infinite` says several windows are open and this shell is not inside any of their Terminals,
tell the user and stop. Do not guess a window.
