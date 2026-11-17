# Context-menu order

One order for every right-click menu in the app, kept by `tools/design/context_menu_order.py`
(a static scan, no app build; run by the "Design ratchet" CI job).

## Canonical order

Top to bottom, groups separated where the menu is long enough to need it:

| # | Group | Examples |
|---|---|---|
| 1 | Settings / Open / Rename | Settings, Rename, Edit Note |
| 2 | Edit | Cut, Copy, Paste, Duplicate |
| 3 | State toggles | Mute, Solo, Bypass, Enable, Hide, Lock, Active |
| 4 | Actions and submenus | Quantise, Fx, Colour, Group, Ungroup, Render, Go to node |
| 5 | Separator | |
| 6 | Delete, always last | Delete, Delete Track, Delete Note |

Rules the lint enforces (ranks 10 / 20-23 / 30 / 90):

- Delete is the last item of its menu (submenus are skipped).
- Duplicate comes before Delete and before the state toggles.
- Settings and Rename come first.
- Items in exclusive if/else branches are not compared with each other.

## Wording and hints

| Action | Label | Hint |
|---|---|---|
| Delete | Delete (or Delete + noun: Delete Track) | `Backspace` (never Del, X or Remove) |
| Rename | Rename | `MODKEY+R` where the panel has it |
| Cut / Copy / Paste | Cut / Copy / Paste | `MODKEY+X` / `MODKEY+C` / `MODKEY+V` |
| Duplicate | Duplicate | `MODKEY+D` |
| Group / Ungroup | Group / Ungroup | `MODKEY+G` / `MODKEY+Shift+G` |

Hints use the `MODKEY` macro, never a literal "Cmd+" or "Ctrl+". Labels are
wrapped in `L()` and need no new translation entries when only reordered.

## Adding or changing a menu

1. Put items in the order above.
2. Register a new popup id in the `MENUS` table of `tools/design/context_menu_order.py`
   so the scan checks it. A popup whose id looks like a context menu but is not registered fails the lint.
3. Run `python3 tools/design/context_menu_order.py`. A violation that cannot be fixed
   goes in `tools/design/context_menu_baseline.json` via `--update`; the baseline only goes down.

## Inventory (generated: `python3 tools/design/context_menu_order.py --table`)

| Menu | Where | Items, top to bottom |
|---|---|---|
| Node (group / note / node variants) | `src/app/frame/StagePopupsA.cpp:89` | Rename / Duplicate `MODKEY+D` / Turn Group Off / Ungroup / Delete Group `Backspace` / Edit Note / Duplicate `MODKEY+D` / Font Size > / Change Colour... / Ungroup / Delete Note `Backspace` / Duplicate `MODKEY+D` / (computed) `B` / Presets > / Open in Piano Roll / Hide params / Show params / Hide viewport / Show viewport / Open in viewport panel / Show modulation matrix / Add to Timeline > / Add to Session > / Add to Timeline / Add to Session / Output window > / Open in new window / Reference in chat / Help / Ungroup / Delete Node `Backspace` |
| Side panel module (search) | `src/app/frame/StageSidePanels.cpp:564` | Remove from favourites / Add to canvas |
| Side panel module (category) | `src/app/frame/StageSidePanels.cpp:626` | Remove from favourites / Add to canvas |
| Arrange clip (timeline + session slot) | `src/app/panels/ArrangePanel.cpp:76` | Rename `MODKEY+R` / Color Tint > / Assign Node... / Rename `MODKEY+R` / Active / Sync to Tempo [x] / Reset Sample BPM to Detected / Compositing > / Color Grade > / Color Tint > / Assign Node... / Output > / Group `MODKEY+G` / Ungroup `MODKEY+Shift+G` / Delete `Backspace` |
| Arrange panel dock | `src/app/panels/ArrangePanel.cpp:1731` | Dock Position > / Close Arrangement Timeline |
| Arrange viewport dock | `src/app/panels/ArrangePanel.cpp:1757` | Viewport Position > |
| Arrange marker | `src/app/panels/ArrangePanel.cpp:2334` | Rename / Delete Marker |
| Arrange track header | `src/app/panels/ArrangePanel.cpp:4110` | Rename Track `MODKEY+R` / Add Video Track / Add Audio Track / Remove from Group / Convert to Group / Group Selected / Render Track / Delete Track |
| Arrange track-group header | `src/app/panels/ArrangePanel.cpp:4206` | Rename Group `MODKEY+R` / Duplicate Group / Toggle Enabled / Add Video Track / Add Audio Track / Render Group / Ungroup (Keep Tracks) / Delete Group + Tracks |
| Arrange empty track area | `src/app/panels/ArrangePanel.cpp:4463` | Add Video Track / Add Audio Track / Add Track Group |
| Session track | `src/app/panels/ArrangeSessionGrid.cpp:81` | Add Video Track / Add Audio Track / Group This Track / Ungroup Track / Stop Track / Delete Track |
| Session empty slot | `src/app/panels/ArrangeSessionGrid.cpp:316` | Add Clip / Paste `MODKEY+V` / Launch Row in Group / Paste Selected Timeline Clip / Stop Track |
| Session scene | `src/app/panels/ArrangeSessionGrid.cpp:455` | Launch Quantize > / Keep Other Tracks Playing [x] / Then Play Next Scene [x] / Next Scene After > / Stop Scene / Send Scene to Timeline / Insert Scene Above / Duplicate Scene `MODKEY+D` / Delete Scene `Backspace` |
| Session dock | `src/app/panels/ArrangeSessionGrid.cpp:1871` | Dock Position > / Close Session |
| Curve editor | `src/app/panels/CurveEditorPanel.cpp:1310` | (computed) / Handles > / Quantise to Grid `Q` / (computed) / Delete `Backspace` |
| Curve editor page tab | `src/app/panels/CurveEditorPanel.cpp:1583` | Close Page / Delete Curve |
| Library sample / media entry | `src/app/panels/LibraryPanels.cpp:649` | Remove from favourites / Add to canvas |
| Library plugin entry | `src/app/panels/LibraryPanels.cpp:1125` | Remove from favourites / Add to canvas |
| Library field preset | `src/app/panels/LibraryPanels.cpp:1329` | Remove from favourites / Add to canvas |
| Mod matrix curve | `src/app/panels/ModMatrixPanel.cpp:152` | Linear (Reset) / Ease In (+0.50) / Ease Out (-0.50) / Steep Exp (+0.85) / Steep Log (-0.85) / Invert Curve |
| Mod matrix dock | `src/app/panels/ModMatrixPanel.cpp:982` | (computed) / Close panel |
| Performance control | `src/app/panels/PerfPanel.cpp:494` | Rename / Type > / Assign X Axis... / Assign Y Axis... / Clear Destinations / Listening X... (Move MIDI CC) / (computed) / Listening Y... (Move MIDI CC) / (computed) / Clear MIDI Bindings / Assign Parameter... / Clear Destination / (computed) / (computed) / Clear MIDI Binding / Color Tint > |
| Performance page tab | `src/app/panels/PerfPanel.cpp:2274` | Rename Page / Duplicate Page / Delete Page |
| Performance canvas | `src/app/panels/PerfPanel.cpp:2433` | Dock Position > / + Add Control > / Clear Page Controls / Close Performance Matrix |
| Piano roll page | `src/app/panels/PianoRollPanel.cpp:902` | Rename `MODKEY+R` / New Page / Delete Page |
| Piano roll note / clip / empty stretch | `src/app/panels/PianoRollPanel.cpp:1178` | Add Note / Add Clip / Settings / Duplicate `MODKEY+D` / Mute `0` / Quantise > / Fx > / Delete `Backspace` |
| Sample studio waveform / slice | `src/app/panels/SampleStudioPanel.cpp:2097` | Settings `Double-click` / Copy `MODKEY+C` / Paste after `MODKEY+V` / Mute `M` / Solo `S` / Reverse `V` / Colour > / Split here `Double-click top` / Join `J` / Play slice `Q` / Convert to node / Zoom to selection `Z` / Zoom out `Shift+Z` / Delete `Backspace` |
| Terminal dock | `src/app/panels/TerminalPanel.cpp:679` | Dock Position > / Close Terminal |
| 3D view select tool | `src/app/panels/View3DPanel.cpp:5632` | (computed) / Clear Rulers |
| 3D view Add (+ scene) items | `src/app/panels/View3DPanel.cpp:7805` | From canvas > / Camera / (computed) / HDRI... / New scene / Copy scene `MODKEY+C` / Paste scene `MODKEY+V` / Duplicate scene `MODKEY+D` / Delete scene |
| 3D view (object / empty space) | `src/app/panels/View3DPanel.cpp:7892` | Rename `F2` / Copy `MODKEY+C` / Paste `MODKEY+V` / Duplicate `MODKEY+D` / Hide `H` / Frame `F` / Group `MODKEY+G` / Ungroup `MODKEY+Alt+G` / Source > / Object > / Go to node / Delete `Backspace` / Add > / Select > / Frame all `Home` / View > |
| Viewport panel dock | `src/app/panels/ViewportPanel.cpp:484` | (computed) / Close panel / Clear all cards |
