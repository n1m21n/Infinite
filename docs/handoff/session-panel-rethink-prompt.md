# Prompt: rethink Session as its own panel (clip launcher)

Paste everything below the line into a fresh session. Branch `feature/session-view`, worktree
`/Users/namansoni/Infinite/worktrees/main`. Read `docs/handoff/session-view.md`, `docs/NOW.md`,
`docs/DECISIONS.md`, and the skills `timeline-arrangement-architecture`, `infinite-design-system`,
`new-audio-node`, `git-branch-workflow`, `infinite-lifecycle` first. Use the semi-brain before
architecture calls.

---

## Goal

Session is currently a mode of the Timeline panel (a Timeline | Session pill in the Timeline toolbar). The owner
wants it rethought **from zero** as a **separate panel**, like every other panel we have (own dock slot,
own toolbar, own shortcuts, own help), while still using the Timeline's engine and data model
(`Arrange::Model`, `SessionSlot`, `Scene`, `ArrangePlaybackModel()`, the shared clip menu and inspector).
Every detail must be right: nothing shown that does not apply, nothing that crashes, nothing that
silently does nothing. Do not patch the current mode: audit it, write the design, get sign-off, then build.

## Defects reported by the owner (reproduce each first, then fix the cause, not the symptom)

1. **Clear Slot crashes the app.** Reproduce via right-click clip -> Clear Slot. Suspected cause:
   `DrawArrangeClipContextMenu` (ArrangePanel.cpp ~71) holds `Arrange::Clip* cp` and calls
   `DrawArrangeSessionSlotMenuItems(cid)` first; Clear Slot resets the slot mid-menu, so the code after it
   (`cp->srcUid`, `LaneOfClip(...)->type`, selection loops, the inspector) dereferences a cleared clip or a
   null lane. Also check `slots[row]` indexing for rows past `slots.size()`. Fix structurally: destructive
   actions are queued and applied after the menu closes, or the menu re-resolves ids every frame. Audit
   **every** menu item and shortcut that deletes/moves/replaces a clip for the same hazard, and add tests
   that run each action with the menu open and the inspector showing the same clip.
2. **Shortcuts on clips.** Decide and implement the set (below). Today only the Timeline's shortcuts exist and
   they do not understand slots (`ArrangeSelectionIds()` is timeline-only; slots use `ArrangeSelectedSlotId()`).
3. **Cannot assign a node to a slot clip.** Reproduce with the "Unassigned clip" cell. The click-to-assign flow
   (`gArrangeAssigningClipId`, ArrangeRender.cpp ~854/872, ArrangePanel.cpp ~171/476/873, ArrangeSupport1.cpp
   ~998/1040) must work from the slot clip's right-click menu, the inspector, and by clicking a node on the canvas.
4. **Assign highlight.** While assigning, the Timeline shows a subtle highlight animation on the clip (and on
   candidate nodes). Verify the slot cell shows the same, honours `UiAnim::ReduceMotion()`, and clears on cancel
   (Esc, click empty, delete the clip).
5. **Timeline-only icons visible in Session.** The toolbar still shows Timeline controls (Bars | Time pill,
   Render, snap, zoom, loop region, etc.). Inventory every control and decide: belongs to Session, belongs to
   both, or is removed. A separate panel solves this by construction; do not reintroduce a shared toolbar
   with hidden bits.
6. **Viewport and Render in Session** (partly fixed in the last commit, uncommitted: monitor drawn beside the grid,
   Render chip hidden in Session). Re-decide in the new panel: monitor placement, whether Render exists
   (render uses plain `gArrange`, so it ignores launched slots; the honest answer is Record to Timeline then render).
7. **Drop, Record, convert to Timeline.** Users must be able to: drop files onto any cell (including a cell holding
   an unassigned clip), record the launch performance, and turn a session into a timeline. Document and make
   discoverable: Record -> Timeline, "Place on Timeline at Playhead", "Send scene to Timeline", and how to get
   back. Confirm each works end to end with a test.

## Questions to answer in the design (with evidence from code and the Ableton/Resolume/Bitwig benchmarks in
`~/re-output/capability-map/cache/` and the Live Object Model pages)

- Which shortcuts, exactly? Propose and justify a table. Candidates: Cmd/Ctrl+C / V / X (clip copy/paste/cut into
  the selected empty cell), Cmd/Ctrl+D (duplicate slot to the next free cell), Shift+D? (owner asked; check what
  Timeline uses for duplicate and keep one meaning), Delete/Backspace (clear slot), Enter or Space on a cell
  (launch), arrows (move cell selection), Shift+arrows (extend), Cmd/Ctrl+A, Esc (cancel assign / stop), Cmd/Ctrl+N
  style add clip, Cmd/Ctrl+Shift+N add scene, scene launch on a key, Stop All, Cmd/Ctrl+Z / Shift+Z undo/redo for
  every slot edit. Check conflicts with global shortcuts and the Timeline's, and with text fields.
- Multi-select of cells: copy/paste a block of cells, drag to move or copy (Alt/Option), drag a clip between
  tracks of the same type, drag scenes to reorder.
- Panel identity: name, dock slot, default size, how it opens (menu, shortcut, toolbar), how its state persists in
  the patch, what happens to the existing Timeline | Session pill (remove it), and migration of `gArrangeSessionView`.
- What the Timeline panel still shows about slots (nothing, or a small "N clips launched" indicator).
- Group tracks and group scenes (the owner asked for both): model, UI, launch semantics.
- Remaining semantics from the previous design not built: legato, quantize values 1/4 beat and 4 bars (transport
  enum change), scene follow actions, scene colour, scene time signature, same-track video transition on launch,
  a clip-inspector Launch section (settings are only in right-click menus today).
- Overlap rules, stated and tested: two scenes at once (additive), video layering order on the same video track
  versus across tracks, audio overlap on one track (last launch wins vs mix), what the viewport shows.

## Method (follow the project rules)

- Fewer, bigger chunks: at most 3 blocks, one branch per block, a commit per step, one tracker.
- Step 1 audit: list every control in the current Session view and every Timeline control it inherits; every
  shortcut; every menu item; every state transition (idle, queued, playing, assigning, recording). Output a table.
- Step 2 design doc in `docs/plans/session-panel/README.md` following the 12-section research template; settle
  decisions yourself (propose, do not ask), then show the owner **mockups/screens** for sign-off before building.
  New panels need the owner's approval (rule: new features need approval).
- Step 3 build: tokens only (`python3 tools/design/ratchet.py` colours 0, raw widgets must not exceed the stored
  433; the last uncommitted change took it to 435 by wrapping the grid in a child, now reverted by passing a width),
  i18n for every string (`python3 tools/i18n/extract.py report` clean, `INFINITE_I18NTEST`), help text coverage
  for the new panel, no debug tools in release builds, ReduceMotion honoured.
- Tests: extend `INFINITE_SESSIONVIEWTEST` with the crash regressions (every destructive action with the menu open),
  shortcut behaviour, assign flow, drop onto filled and unassigned cells, record -> timeline, save/load of every new
  field, one-shot / Toggle / Gate / scene quantize / additive launch / upNext highlight (currently code-only).
  Run the hygiene driver (`.claude/skills/run-infinite-hygiene/driver.sh --auto --skip-build`), expect 54/54 or better.
- Do not UI-script the ImGui canvas (Control your Mac). Verify through tests and `IMAGERESYNTH_SCREENSHOT` shots in
  light and dark themes.
- Commits: no Claude attribution lines. Never push chat corpora. Deploy with `scripts/deploy_desktop.sh` (refuses if
  Infinite is running). Merge to `main` (`--no-ff`, push) only on the owner's yes, Pro only. No release unless asked.
- Update `docs/handoff/session-view.md` at the end so it describes the final design, not the first one.

## Done means

Reproduced-then-fixed crash with a regression test; shortcut table implemented and listed in help; assign works and
animates; Session is its own panel with only Session controls; viewport visible; no Render button that cannot render;
drop, record, and convert-to-timeline all work and are discoverable; builds clean; ratchet unchanged; i18n and
session tests green; hygiene matches baseline; owner has seen light and dark screenshots.
