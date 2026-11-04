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

If `infinite` says several windows are open and this shell is not inside any of their Terminals,
tell the user and stop. Do not guess a window.
