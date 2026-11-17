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
| Delete | Delete (or Delete + noun: Delete Track) | `Backspace` (3D view keeps Blender's Remove with `X`) |
| Rename | Rename | `MODKEY+R` where the panel has it |
| Cut / Copy / Paste | Cut / Copy / Paste | `MODKEY+X` / `MODKEY+C` / `MODKEY+V` |
| Duplicate | Duplicate | `MODKEY+D` (3D view and Piano Roll: `Shift+D`) |
| Group / Ungroup | Group / Ungroup | `MODKEY+G` / `MODKEY+Shift+G` |

Hints use the `MODKEY` macro, never a literal "Cmd+" or "Ctrl+". Order rows inside a lambda helper (defined ahead of its call site) are not scanned. The sibling lints
(menu_lint, menu_alive_coverage, menu_grounding, keys_gate) are separate gates and stay as they are. Labels are
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
| Node (group / note / node variants) | `src/app/frame/StagePopupsA.cpp:95` | Rename / Cut `MODKEY+X` / Copy `MODKEY+C` / Duplicate `MODKEY+D` / Turn Group Off / Ungroup / Delete Group `Backspace` / Edit Note / Cut `MODKEY+X` / Copy `MODKEY+C` / Duplicate `MODKEY+D` / Font Size > / Change Colour... / Ungroup / Delete Note `Backspace` / Cut `MODKEY+X` / Copy `MODKEY+C` / Duplicate `MODKEY+D` / (computed) `B` / Presets > / Open in Piano Roll / Hide params / Show params / Hide viewport / Show viewport / Open in viewport panel / Open in Drum panel `Shift+J` / Open in Mixer panel `Shift+L` / Show modulation matrix / Add to Timeline > / Add to Session > / Add to Timeline / Add to Session / Output window > / Open in new window / Reference in chat / Help / Ungroup / Delete Node `Backspace` |
| Side panel module (search) | `src/app/frame/StageSidePanels.cpp:560` | Remove from favourites / (computed) |
| Side panel module (category) | `src/app/frame/StageSidePanels.cpp:622` | Remove from favourites / (computed) |
| Arrange clip (timeline + session slot) | `src/app/panels/ArrangePanel.cpp:77` | Rename `MODKEY+R` / Colour Tint > / Assign Node... / Rename `MODKEY+R` / Active / Clip Settings / Blend Mode > / Colour Tint > / Assign Node... / Output > / Split at Playhead `MODKEY+E` / Group `MODKEY+G` / Ungroup `MODKEY+Shift+G` / Delete `Backspace` |
| Arrange viewport dock | `src/app/panels/ArrangePanel.cpp:1550` | Viewport Position > |
| Arrange marker | `src/app/panels/ArrangePanel.cpp:2128` | Rename / Colour > / Delete Marker |
| Arrange track header | `src/app/panels/ArrangePanel.cpp:3922` | Rename Track `MODKEY+R` / Duplicate Track `MODKEY+D` / Add Video Track / Add Audio Track / Remove from Group / Convert to Group / Group Selected / Render Track / Delete Track |
| Arrange track-group header | `src/app/panels/ArrangePanel.cpp:4026` | Rename Group `MODKEY+R` / Duplicate Group / Toggle Enabled / Add Video Track / Add Audio Track / Render Group / Ungroup (Keep Tracks) / Delete Group + Tracks |
| Arrange empty track area | `src/app/panels/ArrangePanel.cpp:4284` | Add Video Track / Add Audio Track / Add Track Group / Paste at Playhead `MODKEY+V` |
| Session track | `src/app/panels/ArrangeSessionGrid.cpp:88` | Add Video Track / Add Audio Track / Group This Track / Ungroup Track / Stop Track / Rename Track / Duplicate Track / Delete Track |
| Session empty slot | `src/app/panels/ArrangeSessionGrid.cpp:348` | Add Clip / Paste `MODKEY+V` / Launch Row in Group / Paste Selected Timeline Clip / Stop Track |
| Session scene | `src/app/panels/ArrangeSessionGrid.cpp:497` | Launch Quantize > / Keep Other Tracks Playing [x] / Then Play Next Scene [x] / Next Scene After > / Stop Scene / Send Scene to Timeline / Insert Scene Above / Duplicate Scene `MODKEY+D` / Delete Scene `Backspace` |
| Session dock | `src/app/panels/ArrangeSessionGrid.cpp:1952` | Dock Position > / Close Session |
| Curve editor | `src/app/panels/CurveEditorPanel.cpp:1360` | (computed) / Handles > / Quantise to Grid `Q` / (computed) / Copy `MODKEY+C` / Paste `MODKEY+V` / Delete `Backspace` |
| Library sample / media entry | `src/app/panels/LibraryPanels.cpp:650` | Remove from favourites / (computed) |
| Library plugin entry | `src/app/panels/LibraryPanels.cpp:1126` | Remove from favourites / (computed) |
| Library field preset | `src/app/panels/LibraryPanels.cpp:1332` | Remove from favourites / (computed) |
| Mixer strip | `src/app/panels/MixerPanel.cpp:509` | Rename / Reset name / Colour > / Solo safe / Pre-fader sends / Reset strip / Move Left / Move Right |
| Mixer insert | `src/app/panels/MixerPanel.cpp:688` | (items drawn by helper) |
| Mixer panel | `src/app/panels/MixerPanel.cpp:1464` | Dock Position > / Close Mixer |
| Mod matrix curve | `src/app/panels/ModMatrixPanel.cpp:152` | Linear (Reset) / Ease In (+0.50) / Ease Out (-0.50) / Steep Exp (+0.85) / Steep Log (-0.85) / Invert Curve |
| Mod matrix dock | `src/app/panels/ModMatrixPanel.cpp:986` | Dock Position > / Close Mod Matrix |
| Performance control | `src/app/panels/PerfPanel.cpp:501` | Rename / Type > / Assign X Axis... / Assign Y Axis... / Clear Destinations / Listening X... (Move MIDI CC) / (computed) / Listening Y... (Move MIDI CC) / (computed) / Clear MIDI Bindings / Assign Parameter... / Clear Destination / (computed) / (computed) / Clear MIDI Binding / Colour Tint > |
| Performance canvas | `src/app/panels/PerfPanel.cpp:2357` | Dock Position > / + Add Control > / Clear Page Controls / Close Performance |
| Piano roll note / clip / empty stretch | `src/app/panels/PianoRollPanel.cpp:1174` | Add Note / Add Clip / Settings / Duplicate `Shift+D` / Mute `0` / Quantise > / Fx > / Delete `Backspace` |
| Sample studio waveform / slice | `src/app/panels/SampleStudioPanel.cpp:2219` | Settings `Double-click` / Copy `MODKEY+C` / Paste at playhead `MODKEY+V` / Mute `M` / Solo `S` / Reverse `V` / Colour > / Split here `Double-click top` / Join `J` / Play slice `Q` / Convert to node / Zoom to selection `Z` / Zoom out `Shift+Z` / Delete `Backspace` |
| Terminal dock | `src/app/panels/TerminalPanel.cpp:688` | Dock Position > / Close Chat |
| 3D view select tool | `src/app/panels/View3DPanel.cpp:5748` | (computed) / Clear Rulers |
| 3D view Add (+ scene) items | `src/app/panels/View3DPanel.cpp:7979` | From canvas > / Camera / (computed) / HDRI... / New scene / Copy scene `MODKEY+C` / Paste scene `MODKEY+V` / Duplicate scene `MODKEY+D` / Delete scene |
| 3D view (object / empty space) | `src/app/panels/View3DPanel.cpp:8068` | Rename `F2` / Copy / Paste / Duplicate `Shift+D` / Hide `H` / Frame `F` / Group `MODKEY+G` / Ungroup `MODKEY+Shift+G` / Source > / Object > / Go to node / Remove `X` / Add > / Select > / Frame all `Home` / View > |
| Viewport panel dock | `src/app/panels/ViewportPanel.cpp:491` | Dock Position > / Clear Cards / Close Viewport |
