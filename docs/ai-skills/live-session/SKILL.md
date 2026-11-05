---
name: infinite-live-session
description: Edit the Infinite window the user has open, live. Use whenever INFINITE_SESSION is set or you run inside Infinite's Terminal, and the user asks to build, change, wire, modulate or fix anything in their patch. Read `infinite context` first; never write a new .inf unless asked.
---

# Edit the open Infinite window

You are running inside Infinite's Terminal panel. The user is looking at a live patch in that window,
and every request is about that patch unless they name a file.

1. Run `infinite context` first. It returns the window's status, the selected nodes, what the user
   is pointing at and last touched (`attention`), every node with its index, and any code errors.
   The infinite-map skill says which command answers which question, and the craft skills
   (infinite-visual-craft, infinite-sound-craft, infinite-film-craft) turn a vague ask into nodes. Use `infinite params <node> --full` to see a node's keys, labels, ranges, options and what is modulated.
2. Change the patch with live verbs. Each one is applied to the window at once, can be undone, and
   the user sees it happen:

   | Want | Verb |
   |---|---|
   | pick the right node | `infinite find <plain words>` (ranks every node card; pick from it, not from memory) |
   | add a node | `infinite add <Type>` |
   | wire two nodes | `infinite connect <src[:out]> <dst[:slot]>` |
   | set a value | `infinite set <node> <key> <value>` |
   | modulate a value | `infinite mod <src[:out]> <dst> <key> [lo hi]` |
   | expression on a value | `infinite expr <node> <key> "=sin(t)"` |
   | many changes, one undo | `infinite batch -` (JSON array of {method, params} on stdin) |
   | take it back | `infinite undo` |

   A node can be its index or its name, as `infinite context` lists them.
   Everyday asks that are not about one node:

   | Ask | Do |
   |---|---|
   | make me a sample, loop, drum hit, pad sound | `infinite studio generate <part> [bars=2]`, then `infinite studio status` until ready; offer `[Sample studio](open:samplestudio)` |
   | send me the sample, as a wav | `infinite studio export`, give the path and `[Show in Finder](reveal:<path>)` |
   | put the sample in a node | `infinite studio node Sampler` (or Slicer, Molder, Grain Molder, PaulStretch, Granular, Generate Samples) |
   | write me a melody, chords, bassline, drum beat, MIDI | `infinite notes generate <chords\|comp\|pads\|strum\|arp\|bass\|melody\|drums> [bars=4 style=... energy=...]`; it lands in the Piano Roll (offer `[Piano roll](open:pianoroll)`) |
   | send me the MIDI file | `infinite notes export <clip>`, give the path and `[Show in Finder](reveal:<path>)` |
   | play that MIDI in a node | the take already plays through its Piano Roll node; for a MIDI File node, `infinite import <file.mid>` |
   | use a file from my computer (sample, image, video, model, .mid) | `infinite files <words> [audio\|image\|video\|model\|midi]` finds it; then `infinite import <file>...`, `into <node>` loads it into that node |
   | where is that file | `[Show in Finder](reveal:<path>)` |
   | is my MIDI controller connected | `infinite settings midi`, then offer `[MIDI settings](open:settings/midi)` |
   | map my knob or fader | add `MIDI CC` (Learn on the node), then `infinite mod <cc> <node> <key>` |
   | control it from my phone | add `OSC Receive`, then `mod` it like an LFO |
   | I can't hear anything | `infinite status` (is audio running), `infinite settings devices`, check the patch ends in `Output`; offer `[Audio settings](open:settings/devices)` |
   | use my webcam / mic | `Video In` / `Audio In` nodes (`infinite find webcam`) |
   | put it on my projector or second screen | `infinite node <Output> window display <n>`; for warping, `Projection` |
   | play, stop, tempo | `infinite play`, `stop`, `tempo <bpm>` |
   | save, open, start a new patch, recent ones | `infinite save [path]`, `open <p.inf>`, `new`, `recent` |
   | start from an example | `[Templates](open:templates)` |
   | undo that | `infinite undo`, or a `[Undo](undo:)` button |
   | render or export a video | `infinite timeline render`; `infinite render <p.inf> <out.mp4>` only when asked for a file |
   | show me what it looks like | `infinite shot <node>` |
   | add my sample folder | `infinite library add samples <folder>` |
   | theme, font, language, size | `infinite settings preset\|font\|language\|uiScale <value>`; `[Appearance](open:settings/appearance)` |
   | it is slow | `[Performance](open:perf)`, `infinite settings gpu`; offer `[Performance settings](open:settings/performance)` |
   | it crashed, send logs, update | `infinite settings logs`, `infinite settings updates`; `[Diagnostics](open:settings/diagnostics)` |
   | what does this node do, shortcuts | `infinite docs nodes <name>`, `infinite docs shortcuts` |
   | where is a panel or setting | a button: `open:<panel>` or `open:settings/<place>` (`infinite docs panels`) |
   | upload or post somewhere | you cannot reach other sites from here: save the file, give its path, say where to upload it |
   | anything outside Infinite | answer briefly; say plainly what you cannot do from here |

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

When the ask is vague, or a change is the user's call, offer buttons instead of doing it. Write
each as a markdown link on its own line; the chat draws a button:

| Button | Write |
|---|---|
| add a node | `[Add Bloom](add:bloom)` |
| set a value | `[Slower](set:@lfo#4.rateHz=0.25)` |
| delete a node | `[Remove](delete:@blur#6)` |
| undo | `[Undo](undo:)` |
| go to a node or param | `[Show](go:@rings#7)` |
| open a panel | `[Open timeline](open:timeline)` |
| open a setting | `[MIDI settings](open:settings/midi)` |
| show a file in Finder | `[Show in Finder](reveal:/path/to/file.wav)` |

Rules:
- Use the node type's name as the app shows it (`Field Pixel`, `LFO`), never an internal class name.
- Say what changed, not how: no RPC JSON, no CLI output, no code unless the user asked for code.
- After a change, end with a short `Changed:` list, one line per change in the forms above.
- Before you edit, check names against `infinite context`. A node you name that does not exist shows as a broken link.

If `infinite` says several windows are open and this shell is not inside any of their Terminals,
tell the user and stop. Do not guess a window.
