# R571 - Keyboard-only use

> **Status (2026-10-06): the owner's revised model replaced the Tab-cursor design below. Slices 1-3 of the old
> design are superseded; the old text is kept underneath as history only. Slice 4 (popup/panel nav) is still open.**

## Current model (shipped on `feature/keyboard-v2`, test: `INFINITE_KBCURSORTEST`)

Click a node to make it the **active node** (exactly one node selected). Then:

| Key | Does |
|---|---|
| Tab / Shift+Tab | Walk **that node's params only**, in a loop. Stops when another node is clicked. |
| Left / Right (param focused) | Nudge the value. Alt = x10. Digits start typed entry. |
| Up / Down / Left / Right (no param focused) | Move the selected nodes one grid step (one undo entry per burst) |
| Shift + arrow | Select the neighbouring node in that direction |
| Shift+Enter / Enter | Zoom into the active node / back out to the saved view |
| H | Node help |
| B | Bypass (pre-existing) |
| Shift+U | Ungroup the selected group |
| W A S D | Pan the canvas |
| F | Centre and frame everything |
| Esc | Leave param focus |

Mechanics worth knowing:
- Tab is claimed with `SetKeyOwner(ImGuiKey_Tab, kKbTabOwner, LockUntilRelease)` before `NewFrame` while a node is
  active. Without it ImGui's own Tab nav puts a slider into text edit, sets `WantTextInput`, and every plain-key
  shortcut switches off (found by `KBCURSORTEST`).
- Params register in draw order through `KbParamHook` at the end of `ModSlider`/`ModKnob`, keyed `(nodeIndex, paramIndex)`.
- View changes (zoom, pan) are queued and applied just before `ed::Begin`; doing it mid-frame crashed (SIGBUS).
- The rows live in `kShortcuts[]` (in-app shortcuts window); `shortcuts-sweep/check.py` keeps them honest.
- Not covered yet: checkboxes and dropdowns are not Tab-focusable; Shift+U, H and F have no test.

---

## History: the original Tab-cursor design (superseded)


Status: plan, nothing built. Found by the R560 UX audit: `NavEnableKeyboard` has zero hits in `src/`
(`grep -rn "ConfigFlags\|NavEnable" src` is empty; `io.ConfigFlags` is never touched after
`ImGui::CreateContext()` at `src/main.cpp:73335`). Without a mouse you cannot select, connect or move a
node, or edit a knob.

All anchors below are symbols; line numbers are as of 2026-10-06 and `main.cpp` is ~104.8k lines
(the numbers in `shortcuts-sweep`/`node-ui-pillars` are stale by ~50k lines - re-grep before citing).
Dear ImGui is 1.90.9 (`external/imgui/imgui.h:30`), non-docking build assumptions unchanged.

## Lens map

| Lens | Level | Sub-lenses | Why |
|---|---|---|---|
| 1 Structure | Touched | 1b | No new node. But one new hand-maintained wiring: a keyboard connect path must share the connect-validity chain with the drag handler (see slice 3). |
| 2 Execution | Touched | 2b | New per-frame keyboard-cursor update; must run at a fixed point in the frame relative to `ed::Begin/End`. No audio-thread work. |
| 3 Data/State | Touched | 3c, 3e | Cursor is runtime-only (never saved). Move/connect/param-edit must push undo exactly once (`PushUndoCheckpoint`). |
| 4 UI/UX | Primary | 4b, 4c, 4d, 4e, 4f | Canvas interaction, panels/popups nav, key map, focus ring theme (light+dark), shortcut window rows. |
| 5 Platform | Touched | 5a(no), 4d->5d | No `Platform::` call. Only Cmd vs Ctrl rule (`cmdOrCtrl`) and GLFW key mapping on Win/Linux. Clear for 5a/5b/5c. |
| 6 Performance | Clear | - | Cursor ring is one rect per frame; the node/pin list reuses data already built while drawing. Revisit only if slice 3 enumerates params every frame (cache it). |
| 7 Correctness | Touched | 7b, 7d | Invariants (see below); verification via synthetic-key fixture + `shortcuts-sweep`. |

Coupling rules applied: 4d -> 5d (Ctrl/Cmd), 3a not touched (no new param), 1b -> 3b/4b/5a covered above.

## 1. What a new keymap must not collide with (all verified in code)

| Existing handling | Where | Rule for the new keymap |
|---|---|---|
| `typing = io.WantTextInput`, gate on almost every canvas key | `main.cpp` ~98493 (editor block) | Every new key is `!typing`. Same gate as siblings. |
| `gArrangeFocused` / `gArrangeClaimedKeys` | declared ~1528; set ~34676 (panel click claim), ~78284-78298 (click-anywhere focus tracker), ~98799-98806 | Timeline owns Arrow/Home/End/Alt+Arrow/M/B/0/Cmd+E/R/Shift+J/K when focused. New keys: `!gArrangeFocused`. |
| `gPerfMatrixFocused` | ~98786-98794, ~41408 | Edit mode owns Delete/Cmd+C/V/A/Esc. New keys `!gPerfMatrixFocused`. |
| Hover-to-type: `HandleParamTypeHotkeys` | ~2869; 6 call sites (~4438, 4510, 4605, 5539, 5577, 30045) | Digits `0-9`, `-`, `.`, `=` over a hovered param open typed entry. They read `io.InputQueueCharacters` BEFORE `WantTextInput` flips. Cursor mode must NOT claim these chars. A keyboard-focused param should reuse this same seeding path (slice 3), so `3` on the cursor-focused knob behaves like hovering it. |
| `gTypedParam` (open typed field) | ~2757 | While non-empty, all keys belong to it. Cursor stands down (`!gTypedParam.empty()`). |
| Comment inline edit: hovered comment + Enter/KeypadEnter/any printable char | ~28699-28712 | Enter on a hovered comment opens edit. A cursor on a comment must route Enter to the same thing, not double-fire. |
| Audio keyboard node `computerKeyboardEnabled`: hovered + typing keys Z S X D C V G B H N J M , L . ; / Q 2 W 3 ... | `kTypingKeys` ~20190-20227, gated `isHovered && !WantTextInput && !gArrangeFocused` | Plain letters are NOT safe while a keyboard node is hovered. Cursor-mode keys must be non-letters, or require cursor mode ON (mode swallows hover-letter play, see below). |
| Plain canvas keys | `B` bypass, `X` delete cable, `/` comment, `Space` play (~98549), Delete/Backspace | Reserved. Space especially: ImGui nav treats Space as "activate". |
| Shift family | Shift+A,V,M,P,T,Y,N,H,K,X,D (~98507-98750) and Shift+J/K/A/N in timeline | Reserved. Shift+Tab is free. |
| Cmd/Ctrl family | Cmd+Z/Y, C, V, D, G, E, R, S, O, N, 0 | Reserved; gate new Cmd combos on `cmdOrCtrl`, never `KeySuper` alone (shortcuts-sweep check 3). |
| Esc consumers (not global): popups, blade, scrub cancel, perf MIDI-learn, param MIDI-learn (`UpdateParamMidiLearn` ~41050), comment edit, drift-follow picker | many | Esc priority order: open popup/typed field/learn > pending connect > cursor mode off. Never consume Esc when another owner exists. |
| Node editor library's own shortcuts | `external/imgui-node-editor/imgui_node_editor.cpp` ~4509-4517 (Ctrl+X/C/V/D, Space = CreateNode) and ~5071 (Delete); `ed::EnableShortcuts` is never called in `src` so they are on | Library Space hook exists; harmless today, but keep Space out of the keymap. |
| `kShortcuts[]` help table | `DrawShortcutsWindow` ~42212 | Contract. Every new key gets a row in the same commit. |
| `Tab` and arrows outside the timeline | no plain `Tab` handler anywhere in `src/main.cpp`; arrows only at ~34834-34840 (timeline) | Tab and arrows are free on the canvas. |

Not found (confirmed): no `NavEnableKeyboard`/`NavEnableGamepad`, no `WantCaptureKeyboard` use in `src`, no
cursor/focus model for nodes. Three windows already use `ImGuiWindowFlags_NoNav` (~44131, ~44218, ~102356)
- overlays that must stay that way.

## 2. Proposed model: modal "keyboard cursor" on the canvas

Entry: `Tab` on the canvas (not `typing`, not `gArrangeFocused`, not `gPerfMatrixFocused`, no popup open).
Exit: `Esc` (when nothing higher in the Esc priority list), any mouse click on canvas, or opening a text field.
While ON: a visible focus ring; plain-letter hover handlers (`kTypingKeys` etc.) stay off only for the keys the
mode actually uses (the mode uses no letters in slice 1-2, so no collision).

Cursor state (runtime only, struct `KbCursor`, never serialised):
`level {Off, Node, Pin, Param}`, `nodeIndex` (int, NOT `GraphNode*`), `pinId` (int, uses the existing
`GraphNode::InputPinId/OutputPinId/ParamPinId` grammar in `src/core/GraphNode.h:117-121`),
`connectFrom` (pin id or -1).
Re-resolve through `FindNodeByIndex` every frame (SpawnNode/gNodes reallocation hazard from
codebase-navigation) and drop the cursor if the node is gone (delete, NewPatch, load, undo/redo).

| Key (cursor ON) | Level Node | Level Pin | Level Param |
|---|---|---|---|
| `Tab` / `Shift+Tab` | next / previous node in spatial reading order (top-left to bottom-right; cheap, from `ed::GetNodePosition`) | next / prev pin of this node (inputs then outputs) | next / prev param |
| `Arrow keys` | nearest node in that direction (cone search on node centres) | `Up/Down` pin list, `Left/Right` input<->output side | `Up/Down` param list; `Left/Right` nudge the value (see below) |
| `Enter` | select node (`ed::SelectNode`, `Shift+Enter` adds to selection) and descend | on output pin: start connect (`connectFrom`); on input pin with `connectFrom` set: complete connect | open typed entry (reuse `gTypedParam` insert path) |
| `Esc` | cursor off | back to Node | back to Pin / Node |
| `Alt+Arrows` (`Shift` = x10) | move selected nodes by grid step (`ed::SetNodePosition`, one `PushUndoCheckpoint` per press burst) | - | - |
| `Delete`/`Backspace`/`B`/`Shift+D` ... | untouched: act on `ed::GetSelectedNodes`, so they already work on the cursor's selection | | |
| `Space` | untouched (transport) | | |
| `F` (cursor ON only) | `gRequestFitViewNodeIndex = cursor node` (frame it) | | |

Param nudge: `Left/Right` = one step, `Shift` = x10, `Alt` = x0.1, through the same range/step the knob uses
(param registry, see slice 3). Digit/`-`/`.`/`=` while a param is the cursor target go to the existing
`HandleParamTypeHotkeys` seeding path (call it with the cursor target's `editKey`, bypassing hover).

Focus ring: draw with the app's accent via a theme helper next to `PushCheckboxStyle`/`PushDropdownStyle`
(node-ui-pillars P10: both themes defined in exactly those two places, ring must read in light, dark and
one high-contrast theme, ring never louder than the checked-state accent). Ring is drawn on the node border
(node level), around the pin hit box `kPinHit` (pin level, `DrawPin` ~6047), around the control rect (param
level). Pin ring must not move the modulation dot (P2) - it is an overlay, not a layout change.
Connect preview: draw the pending cable from `connectFrom` to the cursor pin using the existing link colour
helper (`CategoryColors::CableColorFor`), and show the reject reason in the existing tooltip path.

## 3. Is `ImGuiConfigFlags_NavEnableKeyboard` safe? Not globally. Opt in per window.

What it would do if flipped on at `CreateContext`: Tab/arrows/Space/Enter/Esc drive ImGui's own focus nav in
every window. Consequences found in the code:

1. Space = nav "activate" -> would click the focused widget AND fire `Transport::TogglePlay` (~98549).
2. Enter = activate -> collides with comment Enter-to-edit (~28699) and `pickFirst` in the node picker (~100615).
3. Arrow keys would move nav focus through widgets drawn inside node bodies, which live in the node editor's
   zoomed/panned canvas; the nav rect and `ImGuiCol_NavHighlight` render under the canvas transform, and nav
   scrolling will fight `ed::` pan. Node bodies are custom-drawn (`AudioKnobRow`, knobs) with `InvisibleButton`s
   - most are focusable by nav, producing an unpredictable Tab order that ignores the pin model.
4. Hover-to-type and `kTypingKeys` rely on `io.InputQueueCharacters`; nav does not consume characters, so OK,
   but `io.WantCaptureKeyboard` becomes true more often - nothing in `src` reads it (confirmed), fine.
5. Esc in nav "unfocuses" and would race the many Esc consumers listed above.

Recommendation:
- Slice 4 sets `io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard` once after `CreateContext()` (~73335) BUT
  the node-editor canvas window and its node bodies are excluded: the canvas host window is created with
  `ImGuiWindowFlags_NoNav` (spike first: `ed::Begin` creates an inner child window; if its flags cannot be set,
  fall back to toggling the flag per frame - OFF while the canvas window is hovered/focused or `KbCursor` is
  on, ON when a popup/modal/side panel/menu bar owns focus). Spike result decides; both are ~10 lines.
- Opt-in targets (get nav for free): main menu bar, context menus, the "search" node picker, Settings modal,
  Shortcuts window, Browser panel, Modulation Matrix and Performance Matrix windows, Arrangement Timeline inspector
  fields, file dialogs (tinyfiledialogs are native and already keyboard-accessible).
- Keep the existing three `NoNav` overlays untouched.
- Guard Space/Enter/Esc canvas handlers with `!ImGui::IsAnyItemFocused()`-style check only if the spike shows a
  double fire; otherwise nav stays out of the canvas and the guard is unneeded.
- `ConfigNavMoveSetMousePos` stays false (moving the OS pointer would trigger hover-to-type and hover handlers).

## 4. Slices (each independently shippable, smallest first)

### Slice 1 - Node-level cursor: Tab cycle, select, frame (smallest, no connect/move)
Files: `src/main.cpp` only (editor keyboard block just after `ed::EndCreate()`, ~98490-98560, plus draw site for
the ring after node draw). New: `struct KbCursor` + `KbCursorUpdate(bool typing, bool cmdOrCtrl)` near the
`gRequestSelectAll` family; ring drawn in the node draw pass (`DrawNodes`) using `ed::GetNodePosition/Size`.
Behaviour: Tab/Shift+Tab/arrows move cursor at Node level, Enter selects (`ed::SelectNode`), `F` frames, Esc exits.
Reuses: `ed::SelectNode/ClearSelection` (as `doSelectAll` ~98510), `gRequestFitViewNodeIndex`, existing
delete/duplicate/bypass keys which already act on the editor selection.
`kShortcuts` rows: "Keyboard Cursor | Tab / Shift+Tab", "Move Cursor | Arrow keys", "Select Under Cursor | Enter",
"Exit Cursor | Esc". Note shortcuts-sweep `KEY_ALIASES` already maps tab/up/down/left/right/enter/escape,
but `Escape`/`Enter` are in `LOCAL_KEYS` (rows for them need no handler-side change; Tab will be matched by a new
`ImGui::IsKeyPressed(ImGuiKey_Tab` handler). Re-run `python3 .claude/skills/shortcuts-sweep/check.py`; expect clean
without touching `LOCAL_KEYS`/`SUPER_ALLOW`. Arrow rows already exist for the timeline ("Left / Right"), so word the
canvas row as "Up / Down / Left / Right (cursor on)" or add a `KEY_ALIASES` entry deliberately with a comment.
Tests: new fixture `INFINITE_KBCURSORTEST` following the `INFINITE_INPUTTEST` pattern (~76625: `AddKeyEvent` at
chosen `frameId`s): spawn 3 nodes, press Tab twice, Enter, assert `ed::GetSelectedObjectCount()==1` and the
selected node equals the expected spatial order; then Esc and assert cursor off; assert gating: with
`gArrangeFocused=true`, `typing` (set `WantTextInput` via an open `gTypedParam`) and a hovered audio keyboard node
nothing moves. Register it in `.claude/skills/run-infinite-hygiene/driver.sh` (`TIER1_CHECKS`/`GROUP_ui`) - a fixture
not in driver.sh is not part of the gate (codebase-navigation living-map note).
Why safe: no new action semantics; only selection, which the whole app already trusts.

### Slice 2 - Move nodes from the keyboard
Files: `src/main.cpp` same block. `Alt+Arrows` / `Alt+Shift+Arrows` call `ed::SetNodePosition` for every selected node
(group members follow; reuse `gGroupMembers` lookup exactly as the delete/duplicate blocks ~98826-98838), one
`PushUndoCheckpoint()` at burst start (track "burst" by key-held/frame gap) and `gSuppressUndoCheckpoints` for the batch.
Careful: `ed::SetNodePosition` on a just-spawned node reads stale state (codebase-navigation living map) - only move nodes that
have been drawn at least once; also a spawn-time sync at ~47870 uses `spawnX/spawnY`.
Reuses: `ClusterOffset`, the grid snap used by mouse drag if any (verify), `FindFreeSpawnPosition` is NOT used.
`kShortcuts`: "Move Nodes | Alt+Arrows (Shift = x10)". Note Alt+Left/Right already documented for timeline markers - the
canvas row must say "(canvas)" and the handler is `!gArrangeFocused`.
Tests: extend `INFINITE_KBCURSORTEST`: select, Alt+Right x3, read `ed::GetNodePosition` one frame later, assert dx; Undo once
returns all three steps (burst) or one step (decide: see open question 3); assert saved position round-trips (patch
save/load of node pos - run `INFINITE_ROUNDTRIPTEST`).

### Slice 3 - Pins, keyboard connect, keyboard param edit
Files:
- `src/main.cpp`: extract the connect-acceptance chain out of the inline `ed::QueryNewLink` handler (~98208-98330) into one
  function `TryConnectPins(int pinA, int pinB, std::string& reason)` that returns valid + reason and performs the wiring.
  Both the drag handler and the keyboard path call it (otherwise the keyboard path is a fifth hand-maintained wiring chain; see
  `cable-logic-sweep` and the existing `ConnectNodes` ~7938 which only covers plain input slots, NOT param pins or colour pins).
  Cheapest correct shape: make `ConnectNodes` (or a sibling) accept pin ids and cover `IsParamPin`/`IsColorPin`, then have the
  handler call it. This also removes the divergence risk the comment at ~7677 already warns about.
- Pin cursor level: enumerate pins per node via `GraphNode::InputPinId/ParamPinId/ColorPinId/OutputPinId` and the node's declared
  slots; record each pin's screen rect at draw time inside `DrawPin` (~6047) and the discrete-param-pin helper (`DrawDiscreteParamPin`)
  into a per-frame `gKbPinRects` map (pinId -> rect). That is the only place that knows where pins are.
- Param cursor level: a per-frame registry of param widgets keyed `(nodeIndex, paramIndex)` filled where `HandleParamTypeHotkeys` is
  already called (6 sites) - they hold `editKey` and `float* value`. Left/Right nudge writes `*value` with the knob's own range;
  that range must come from the same place the knob clamps (param-truth-audit: do not invent a second range). Params not routed through
  those 6 sites (non-float: dropdowns, checkboxes via `AudioKnobRow::Checkbox/Dropdown`) are a second pass: Enter toggles / opens the
  dropdown by `ImGui::OpenPopup` equivalent. Decide scope in open question 4.
- Enter on param opens typed entry through the `gTypedParam.insert(editKey)` block ~2858-2860 (extract that into
  `OpenTypedParam(editKey, value)` so hover and keyboard share it).
Reuses: `PushUndoCheckpoint`, `IsKernelDrivenParam` and `PredictorBindRefusal` (already inside the handler), `RebuildAudioTopology`.
`kShortcuts`: "Pin Level | Enter on node, then Tab / arrows", "Start / Finish Connect | Enter", "Cancel Connect | Esc",
"Nudge Value | Left / Right (param focused)".
Tests: `INFINITE_KBCONNECTTEST`: spawn Noise->Blur (image), drive Tab/Enter/Down/Enter/Tab/Enter via `AddKeyEvent`, assert the link exists
in `gLinks`; drive an invalid pair (output->output, audio cycle) and assert refused with the same reason string as the drag handler;
drive a modulator -> param pin and assert binding exists and `INFINITE_MODSWEEP`-style check passes; Undo restores. Also a static
parity test: both callers reach `TryConnectPins` (grep gate in the fixture or in `cable-logic-sweep`'s static cross-check).
Run `cable-logic-sweep` and `modulation-sweep`.

### Slice 4 - ImGui keyboard nav for popups, menus and panels
Files: `src/main.cpp`: after `ImGui::CreateContext()` (~73335) set `io.ConfigFlags |= NavEnableKeyboard`; canvas window `NoNav`
(or per-frame toggle, see section 3). Audit each popup/panel for focus entry (`ImGui::SetKeyboardFocusHere` on first open of the node
picker already exists via text field), tab order (Settings, Shortcuts window, Browser, Mod/Perf matrices, context menus), and
`ImGuiCol_NavHighlight` in both themes (theme code that owns `PushCheckboxStyle`/`PushDropdownStyle`; light-theme contrast check).
Guards: Space/Enter/Esc canvas handlers gain a `!NavFocusInPanel` check only if the spike shows double-fire.
Tests: `INFINITE_NAVTEST`: open Shortcuts window, `AddKeyEvent(Tab)` x2, `Enter`, assert the filter field has `ActiveId`/focus
(`ImGui::IsItemActive` captured in-window) and that `Transport::IsPlaying()` did NOT change when Space is sent while a nav item is focused
and the canvas is not; screenshot via `run-infinite-hygiene --shot-only` in light and dark to check the highlight ring (node-ui-pillars P10 acceptance).
Last because it changes behaviour of every window; slices 1-3 do not depend on it.

## Invariants established (run `invariant-interaction-audit` after implementing)

1. Cursor never outlives its node: dropped on `RemoveNodeByIndex`, `NewPatch`, load, undo/redo (all rebuild `gNodes`). Stores indices, never `GraphNode*`.
   Sibling that could undo it: `SpawnNode` reallocation, group delete (members), `Undo()`.
2. Keyboard connect and drag connect are the same decision (single `TryConnectPins`). Sibling that could undo it: anyone adding a fifth chain; the
   `ConnectNodes` RPC path.
3. Cursor mode swallows no key that another owner has: every new key is `!typing && !gArrangeFocused && !gPerfMatrixFocused && gTypedParam.empty()`.
   Sibling: hover-to-type digits, `kTypingKeys` letters, comment Enter-to-edit, Esc consumers.
4. One undo entry per user gesture (move burst, connect, typed edit). Sibling: `gSuppressUndoCheckpoints` leakage.
5. Param nudge is clamped to the same range/step as the knob. Sibling: modulation (`ParamRef` bound param) - nudging a modulated param must edit
   the base value, not the modulated value (check how the knob does it).
6. Cmd-family gated on `cmdOrCtrl` (shortcuts-sweep check 3).

## Cross-platform notes (windows-parity / linux-parity)
No `Platform::` function is added. GLFW delivers `Tab`/arrows identically on all three; macOS Alt = Option, so `Alt+Arrow` may emit a dead
character on some Windows/Linux layouts (AltGr) - check `io.KeyAlt` vs `io.KeyCtrl && io.KeyAlt` (AltGr reports both on Windows). `Cmd` is not used
anywhere in the new keymap except via `cmdOrCtrl`. Linux: run the fixture in the container+Xvfb rig (synthetic `AddKeyEvent` needs no real X input).

## shortcuts-sweep plan
Each slice adds `kShortcuts` rows in the same commit as its handlers (category "Keyboard Cursor" new, or fold into "Edit & Canvas"). Run
`python3 .claude/skills/shortcuts-sweep/check.py` per slice. Expected friction: (a) arrows and Tab need `KEY_ALIASES` rows (Tab exists, arrows exist);
(b) `Enter`/`Esc` stay in `LOCAL_KEYS`, so they pass either way but still document them in the table; (c) because arrows are already a timeline row, make sure
the checker finds at least one canvas `IsKeyPressed(ImGuiKey_LeftArrow` - it matches by key, not by gate, so a canvas handler is only "documented" by the
existing timeline row; word the new row distinctly so the help window is truthful. The sweep cannot prove gate correctness; the fixtures above do.

## Open questions for the owner
1. Entry key: `Tab` (this plan) vs a dedicated key (`K`/`Shift+C`)? Tab makes the feature discoverable but means Tab on canvas stops being "unused".
2. Should cursor mode be sticky (stays ON until Esc) or momentary (Tab ring auto-hides after N seconds idle / on mouse move)? Mouse move vs key-driven hover-to-type conflicts depend on this.
3. Undo granularity for keyboard moves: one entry per key press (precise, noisy) or per burst (one per held run)? Mouse drag is one entry per drag.
4. Scope of param edit in slice 3: only float knobs/sliders (the 6 `HandleParamTypeHotkeys` sites), or also checkboxes, dropdowns, buttons and Field-graph editors (much bigger)?
5. Should nav-enabled panels (slice 4) flip on for everyone, or behind a Settings toggle until it has soaked for a release? (Lower risk of surprising current mouse users.)
6. Node move step: fixed grid size or zoom-aware (screen pixels)? And should `Alt+Arrows` snap to other nodes' edges?
7. Do Arrangement Timeline clips get a keyboard cursor too (a separate quest: it has its own selection model `gArrangeSel`), or is that out of R571?
8. Screen-reader / announce support (R560 asked about accessibility): out of scope here, or should the cursor expose a text description line in the status bar?
