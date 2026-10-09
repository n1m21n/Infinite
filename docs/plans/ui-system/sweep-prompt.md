# UI + node consistency sweep: prompt and tracker

Paste the prompt below into a fresh session on `feature/ui-canvas`. The tracker underneath is the single place
status lives; update it in the same commit as each fix.

## Why this exists

The design pass closed every plan row, yet review still found surfaces nobody had looked at: the Viewport panel
frame, the Shortcuts and Help windows, the filter field overlapping a card. Tests and the ratchet cannot see
"looks different". Only a screenshot of every surface, compared against the reference surface, can. Nodes are the
biggest surface and have had the least of this pass.

## Prompt

```
Sweep Infinite's whole UI for visual consistency with the new design language, one surface at a time, with a
screenshot as proof for every one. Branch: feature/ui-canvas. Tracker: docs/plans/ui-system/sweep-prompt.md.

Load first: infinite-design-system, node-ui-pillars, audio-node-ui, run-infinite-hygiene (headless screenshots).

References (the look everything is compared against):
  - Windows/dialogs: Settings (16 pt window pad, SectionCard groups, FormParts controls, one title row).
  - Docked panels: Modulations / Perf / Arrange (PanelFrame card, inset gap, grip strip, DrawPanelSeam).
  - Nodes: a finished Wavetable node (audio-node-ui spec, section 8).

Part A: chrome sweep. For every row in the A table of the tracker:
  1. Screenshot dark, then light (INFINITE_FORCELIGHT=1), headless:
     INFINITE_AUDIOUITEST=1 INFINITE_OPENPANELS=<tokens> IMAGERESYNTH_SCREENSHOT=/tmp/x.png INFINITE_SCREENSHOT_FRAME=40
     Add an OPENPANELS token in src/app/Startup.cpp when a surface has none. For panels also take every dock side.
  2. Read the PNG. Compare against the reference on: card/frame, window padding, title row, header style, scrollbar,
     empty state, hover/press, text sizes, overlap or clipping, anything still using the old look.
  3. Fix by editing the component, never the call site. Then re-shoot. Nothing is "done" without the after shot.
  4. Record the verdict in the tracker (ok / fixed in <sha> / needs decision) and note anything not visually checked.

Part B: node redesign sweep. Every node type on the canvas, thought through, not skimmed:
  - Group by family (Source, 3D, Compositing, Effects, Modulators, Prediction, Macros, Utility, Notes, Synths,
    AudioEffects). Do one family per commit, in the order of the B table.
  - Audio effects first: all 16 AudioEffects share ONE visualizer grammar. For each, check: visualizer is full body
    width, never blank at rest, same frame/graticule/colour roles, readout strip never empty, knob rows on the grid,
    selector left / knobs right, mix bottom-right (P1 to P6), same caption and value formatting, same
    hover/modulated state, same idle state. Where two effects show the same kind of thing (a response curve, a
    meter, a time display), they must be drawn by one shared component, not two look-alikes.
  - Every node: screenshot expanded, collapsed and modulated, dark and light, plus one high-contrast theme once per
    family. Run the node-ui-pillars acceptance checklist on each.
  - A visualizer or widget that differs from its siblings is fixed in the shared component; if no shared component
    exists, create one in src/app/ui/design/components and move the siblings onto it.
  - Never add a control to fill a hole. Do not restyle the visual/image/geometry node library beyond what the
    pillars require.

Rules: components only; ratchet must not rise (python3 tools/design/inventory.py, then ratchet.py); colours stay 0;
three platforms; debug-only tools under #ifndef NDEBUG; one commit per surface or per node family, staged by explicit
path (the repo has unrelated uncommitted files); no Claude attribution lines in commits; after each build copy
build/Infinite.app to ~/Desktop/Infinite.app; run driver.sh --group ui before each commit and --auto before the last.

Keep the screenshots: /tmp/sweep/<surface>-<dark|light>-before|after.png, and list them in the tracker row.
Finish with a short summary: rows fixed, rows ok, decisions taken, what was not visually verified, what remains.
```

## Tracker

Status: `todo`, `ok` (checked, matches), `fixed <sha>`, `decision` (needs owner), `n/a`.

### A. Chrome

| Surface | Dark | Light | Notes |
|---|---|---|---|
| Settings window (all 6 tabs) | todo | todo | reference |
| All Shortcuts | fixed | todo | cards + pad, filter gap |
| Help / module reference | fixed | todo | cards, header fill off |
| Colour picker | todo | todo | |
| Node help popups | todo | todo | |
| Unsaved changes / Recover dialogs | ok | todo | |
| Export / render dialogs | todo | todo | |
| Field editor | todo | todo | |
| Library panel | todo | todo | |
| Viewport panel: bottom, right, left, top | fixed (bottom, right) | todo | left/top not seen |
| Modulations panel: 4 docks | ok (bottom, right) | todo | reference |
| Perf panel: 4 docks, edit mode | todo | todo | |
| Arrange panel: 4 docks, inspector | todo | todo | |
| Top bar, menus (File/Edit/Menu) | fixed | todo | flash fix not seen on screen |
| Context menus (canvas, node, cable) | todo | todo | |
| Toasts, tooltips, empty states | todo | todo | |
| Minimap, canvas, cables, grid | todo | todo | |
| Right rail (panel toggles) | todo | todo | |

### B. Nodes

| Family | Nodes | Dark | Light | Notes |
|---|---|---|---|---|
| AudioEffects (16) | one shared visualizer grammar | todo | todo | first |
| Synths | | todo | todo | |
| Notes | | todo | todo | |
| Modulators | | todo | todo | |
| Prediction | green family, 10-axis card | todo | todo | |
| Macros | | todo | todo | |
| Effects (image) | FilterDef pattern | todo | todo | |
| Compositing | | todo | todo | |
| Source | | todo | todo | |
| 3D / geometry | Render 3D worst case | todo | todo | |
| Utility | | todo | todo | |

### Audio effects visualizer matrix

Fill one row per effect while sweeping. A column that differs from the majority is a defect, or a documented decision.

| Effect | Visualizer kind | Full width | Idle state | Frame / graticule | Readout idle text | mix last | Shared component |
|---|---|---|---|---|---|---|---|
| (todo) | | | | | | | |
