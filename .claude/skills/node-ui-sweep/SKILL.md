---
name: node-ui-sweep
description: Sweep the UI/UX inside and around Infinite's nodes for bugs on both macOS and Windows - pin identity, cable dragging and drawing, node dragging vs canvas panning, collapsing a node's params, node box sizing, node-local viewports, in-node editors (wavetable, EQ curve), groups, undo/redo, copy/paste/delete, drag-and-drop from the browser onto a node, and whether every control is reachable by a modulation cable. Use after changing any node body, widget wrapper, pin layout, the node editor canvas, the group box, undo, or the browser drop targets; when a cable lands on the wrong control or will not attach, when collapsing a node loses its modulation, when a node will not drag or the canvas pans instead, when a node box jitters or grows, or before a release as a UI regression gate.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
.claude/skills/node-ui-sweep/driver.sh
```

`--skip-build` to reuse the existing binary. Exit 0 means every asserted
fixture passed and the modulatable-control ratchet held.

This sweep is the slowest of the family: the three drag-and-drop fixtures wait
on an asynchronous media/plugin scan and are budgeted 600 frames each (they
exit the moment their verdict prints, so a healthy run is far shorter). To skip
them while iterating on something else, comment them out of `ASSERT` in
`driver.sh` rather than lowering their budget.

## What "node UI" covers here

| surface | fixture | the bug it catches |
| --- | --- | --- |
| pin identity | `PINDUPTEST` | two controls emitting the same pin id in one frame - cables land on the wrong control or disappear |
| canvas vs node drag | `DRAGTEST` | dragging empty canvas moves a node, or dragging a node pans the view |
| collapsed nodes | `HIDETEST` | collapsing a node's params silently drops its modulation bindings, stops drawing the cable, or freezes the driven value |
| in-node editors | `WTDRAGTEST`, `EQDRAGTEST` | direct-manipulation surfaces (wavetable scrub, EQ dots/diamonds, double-click band bypass) that stop responding, or move the wrong slot |
| node-local viewport | `MINIVIEWPORTTEST` | the in-node 3D preview renders nothing, or does not follow a transform change |
| groups | `GROUPTEST` | group box does not resize to its members, an overlapping group steals members, ungroup frees the members' nodes |
| undo/redo | `UNDOTEST` | undo of a delete does not restore the wire, a param edit is not undoable, the redo stack is not cleared by a new action |
| drop targets | `SAMPLERDRAGTEST`, `MEDIADRAGTEST`, `PLUGINDRAGTEST` | dropping a sample/image/plugin onto a node does not load it, or loads the wrong one |
| control reachability | `audit_node_params.py` ratchet | a new control drawn with a raw ImGui widget, so it has no pin, takes no cable and never appears in the performance matrix picker |

Read but not graded: `SIZETEST` (node box size per frame - look for oscillation
or unbounded growth) and `INPUTTEST` (select/copy/paste/delete counts - check
that paste produced a real duplicate carrying the source's param values).

## The ratchet is the part that catches new work

Fixtures catch regressions in code that already exists. The static check
catches the far more common case: someone adds a control to a node body with
`ImGui::SliderFloat` instead of `ModSlider`. That control looks right, and is
simply unreachable - no pin, no cable, absent from *Assign Parameter*.

`driver.sh` runs `scripts/audit_node_params.py` and fails if the count of
non-modulatable controls rises above the baseline recorded in the driver (16 at
the time of writing, out of 1658 modulatable across 135 nodes). When you fix
some, lower the baseline in `driver.sh` so it cannot come back. The full
per-node report it writes to `docs/node_param_audit.md` is what tells you
*which* control; `node-param-audit` is the skill for reading it.

## Windows parity

Every fixture here drives the real ImGui/node-editor canvas through synthetic
input, so running the driver on Windows is a genuine second-platform check
rather than a re-run of the same code path - the input, DPI, and window paths
underneath differ.

Two things to watch when a fixture passes on macOS and fails on Windows:

- **Modifier keys.** Anything that reads a modifier must use
  `cmdOrCtrl = io.KeyCtrl || io.KeySuper` (or the `MODKEY` macro), never
  `io.KeySuper` alone. A macOS-only Cmd check is the single most common cause
  of "this shortcut/drag does nothing on Windows". `shortcuts-sweep` gates this
  statically; check it too when a UI interaction is dead on one OS.
- **Drag-and-drop payloads.** The three drop fixtures reach into the platform
  file/plugin browser. A failure on one OS only usually means the browser row
  data, not the drop target, differs.

## Reading a failure

- `PINDUPTEST FAIL: N node(s) emitted a pin id twice` - names the offending
  nodes. Almost always a body drawing a control twice (both branches of a mode
  switch running), or a hand-written pin id colliding with a generated one.
- `DID NOT MOVE - BUG` / `PAN OK` / `BUG` in `DRAGTEST` - the two halves are
  independent: "node still, view panned" for a canvas drag, "node moved" for a
  node drag. Read which half printed the failure.
- `LOST - BUG`, `MISSING - BUG`, `FROZEN - BUG` in `HIDETEST` - respectively:
  bindings dropped on collapse, cable not drawn on the collapsed body,
  modulation no longer applied. Three different bugs, three different lines.
- `[NO VERDICT]` on a drag fixture - it never got past its scan phase. Read the
  log: if it never printed a phase line at all, the browser found no candidate
  file/plugin on this machine, which is an environment problem, not a
  regression.

## What this sweep does not cover

- **No visual/pixel check of node chrome.** Nothing asserts that a node looks
  right, only that it behaves. Layout judgement is still by eye - that is what
  `INFINITE_AUDIOUITEST` (a fixture that deliberately does *not* exit) is for.
- **Dropdowns, text fields and colour pickers are only partly covered.**
  `INFINITE_PICKERTEST`, `INFINITE_COLORTEST` and `INFINITE_COMMENTTEST` open
  those surfaces but print no verdict, so they are not in the graded list.
  Adding an assertion to them is the cheapest way to widen this sweep.
- **Node resizing has no assertion**, only `SIZETEST`'s trace.
- **Cable *legality*** - what may connect to what - is a separate sweep; see
  `cable-logic-sweep`.
