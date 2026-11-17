# Manual coverage audit, Infinite 2.0 (MANUALS lane)

Branch `release/2.0-manual`, worktree `rel-manual`. Source audit only: no app was launched for this file.

## Status

The audit below was written from source before any capture. Current state:

- Node shots: 744 of 744 raw PNGs (372 spawnable types, closed and open), minimum width 262 px.
  Verified by `tools/manual/verify_nodes.py`; output in `docs/release/manual-shots-2.0.json`.
- Chapter 29 UI shots: regenerated at 3200x2000 (`docs/ui_screenshots/`, commit ab5e2cf4).
- PDFs rebuilt (Node Reference, Field Language, Headless User Guide); `pdf_check.py` passes with blank-area warnings only.
- Known exceptions: 12 node shots with a red boolean/instance outline (Intersect, Union, Difference, Instance on Points),
  recorded in `docs/shots/manifest.json` and `docs/release/known-issues-2.0.md`.
- Dropdown (d) states: NOT COVERED. This is a stated gap, not a skipped step. The 45-minute pass was started and
  stopped without app runs, for these reasons:
  - The existing dropdown hook (`INFINITE_NODEGALLERY` plus `INFINITE_GALLERYOPENDD=<param index>`,
    `src/app/frame/StagePopupsB.cpp`) costs one app launch per dropdown, about 15 to 20 s each.
  - It needs the param index of each node's enum control. `infinite --describe` takes about 15 s and printed no usable
    enum lists, so the indexes would have to be guessed.
  - The build copy at `infinte-base/build/Infinite.app` was being recompiled by the build lane, so the pass used the
    release copy at `/private/tmp/claude-501/shotrun/Infinite.app` (2.0 release, built 22:02) instead.
  - The 2.0 shot plan has zero "d" entries.
  Covered: none of the dropdown or context-menu IDs in section E below.
- Dropdown option plan (source, not `--describe`): `tools/manual/dropdown_plan.json`, written by
  `tools/manual/dropdown_plan.py` from `docs/node_param_audit.md` and the `DropdownButton` calls in
  `src/app/bodies` (options read from literal lists, `Class::Names()` functions and namespace-scope constants in
  `src/nodes` and `src/app`). 184 dropdowns on 204 audited nodes: 140 have their options resolved, 44 do not.
  The 44 unresolved are lists built at runtime (e.g. `BlendModes::NamesFor()`, font, device, MIDI and file lists)
  and are listed in the plan with `status: "unresolved"`. Caveats for the later capture run:
  - `param_index` is the control's position among the node's audited controls. It has not been checked against the
    runtime index that `INFINITE_GALLERYOPENDD` uses. Check one node before a batch.
  - 24 registered node types have no `Draw*` function in the audit (3D Group/Null, Compositing Group/Null/Viewport,
    some modulators and sources, Output). They have no plan entries.
- Capture of the dropdowns themselves is a docs-only follow-up after the 2.0 release; manuals are not tied to the
  binary.
- Video In caption: kept in the manifest, but Video In has no card in the chapter list, so the caption does not appear
  in the PDF text yet (stated gap).

## Chapter numbering

The brief says chapters 1-20. In this tree the UI manual is chapter 29 ("Using the App", `tools/manual/ui_chapter.py`),
and the Node Reference Manual is chapters 21+ (`tools/manual/build.py`, `tools/manual/cards*.py`). Chapters 1-20 are a
frozen PDF with no source. The audit below uses chapter 29 for UI surfaces and the node cards for nodes.

## Source inventory (counted from src/)

| Surface | Count | Where |
|---|---|---|
| Menu bar groups | File, Edit, Menu, Help | `src/app/frame/StageMenuBar.cpp` |
| Menu items (call sites) | 43 in StageMenuBar, 71 in StagePopupsA, 3 in StagePopupsB | same |
| Menu > Panels submenus | Viewport, Modulation matrix, Mixer, Performance Matrix, Arrangement Timeline, Session, Sample Studio, 3D View, 2D View, Piano Roll, Drums, Terminal, Curve Editor, Field Patcher, Show Set | `StageMenuBar.cpp` ~L373-668 |
| Right-side panel rail tiles | 17: library, viewport, modmatrix, perf, mixer, arrange, session, studio, piano, drums, view3d, view2d, curve, field, history, settings, terminal | `StageMenuBar.cpp` `DrawPanelRail` |
| Settings tabs | 12: Appearance, Canvas & Workspace, Audio, General & Performance, Field Language, Expression Globals, Extensions, Plugins, Agents & Terminal, Infinite as VST3/AU, 3D View, Keys | `src/app/panels/SettingsWindow.cpp` L94 |
| Settings section cards | 27 `FormParts::Section` headings | `SettingsWindow.cpp` |
| Popup / context / dropdown IDs | 90 distinct literal `OpenPopup` / `BeginPopup` IDs (195 incl. menus and combos) | `src/app` |
| Combo boxes | 75 distinct (scan of `BeginCombo` / `BareCombo` / `Combo` with literal or `T()` / `L()`-wrapped ids; an earlier count of 38 used a narrower regex and missed the wrapped ids) | `src/app` |
| Dialogs and floating windows | About, Collect diagnostics, Check for updates, Unsaved changes, Autosave recovery, Share Patch, Template library, Pre-show check, Terminal quit, Shortcuts, Help, UI Style Editor, UI Debugger | `StageFloating.cpp`, `StageMenuBar.cpp`, `HelpWindows.cpp` |
| Node spawnable types | 372 (377 registered), per the brief | `NODELIST` of the release binary (not re-counted: the app is busy) |
| MCP tools | npm 12, Python plugin 14 | per the brief |

## Documented before (chapter 29 figures, current source)

`tools/manual/ui_chapter.py` references 51 unique figure ids. Mapped to the surfaces above:

- Menus: 4 of 4 groups (file, edit, menu, panels). Help menu: 0 figures (only the About dialog).
- Rail tiles: 11 of 17 (library, viewport, modmatrix, perf, arrange/timeline, session, studio, piano, view3d, history, settings).
- Settings tabs: 9 of 12 (appearance, canvas, audio, general, field, globals, extensions, plugins, terminal).
- Settings section cards: 0 of 27 individually (only the whole tab).
- Popups and context menus: rc_node, rc_canvas_palette, rc_palette_submenu, perf_control_menu2, tl_* (timeline tool/grid/lane/clip/track/render/add-track) and perf_edit/add_control. About 12 of the 90 literal popup IDs.
- Dialogs and windows: about, shortcuts, help, templates (4). Missing: unsaved, recover, collect diagnostics, check for updates, share patch, pre-show check, terminal quit.
- Nodes: the node reference (chapters 21+) has one card per node from the 1.2 build; these are old images and must be recaptured.

## Gap list (what 2.0 must add)

### A. Menus (add zoomed shots)
- Help menu: All shortcuts, Help / module reference, UI Debugger / Item Picker, UI Style Editor, Collect diagnostics, About, Check for updates.
- File: Open Recent submenu, Template library, Save with media, Share Patch.
- Menu: Settings, Show Set submenu, Show lock, Next patch / Previous patch, Panic / Resume after panic, Pre-show check.
- Panels submenus with no figure: Mixer, Drums, 2D View, Curve Editor, Field Patcher, Terminal.
- Proposed ids: `menu_help`, `menu_file_recent`, `menu_file_sharepatch`, `menu_menu_showlock`, `menu_menu_panic`,
  `menu_menu_preshow`, `menu_show_set` (a file exists on disk but is not wired into the chapter), `menu_panels_mixer`,
  `menu_panels_drums`, `menu_panels_view2d`, `menu_panels_curve`, `menu_panels_fieldpatcher`, `menu_panels_terminal`.

### B. Panel rail and panels (add whole-panel shots)
- Missing rail tiles: `panel_mixer`, `panel_drums`, `panel_view2d`, `panel_curve`, `panel_fieldpatcher`, `panel_terminal`.
- Text-only today (no figure): Mixer, Drums, Field Patcher, Curve Editor, 2D View, Canvas Link.

### C. Settings (add a zoomed shot per section card)
- Missing tabs: `settings_vst3` (Infinite as VST3/AU), `settings_3dview`, `settings_keys`.
- Section cards with no figure (27 total): Theme Preset & Palette, Node Module Category Colors, Cable Category Colors,
  Node Card Styling, Display, Transparency Backdrop, Grid & Canvas, Minimap, Cable Visibility, Audio Devices & Format,
  MIDI Inputs, Autosave, Performance & Rendering, Movement Log, Learning, Diagnostics, Application & Updates,
  CLI setup, DAW plugin section, plus the Field Language topic dropdown (`##fieldtopic`) and its sections.

### D. Dialogs and floating windows
- Unsaved changes, Autosave recovery, Collect diagnostics, Check for updates, Share Patch, Pre-show check, Terminal quit.

### E. Dropdowns and context menus (per panel, zoomed)
Popup IDs with no figure, grouped by panel:
- Node: `##nodecontext`, `##nodehelp`, `##newinsertfx`, `##newsendfx`, `##moreinsfx`.
- Viewport: `##viewportpanelctx`, `##v3dcontext`, `##v3ddockmenu`, `##v3dpivot`, `##v3dorient`, `##v3daddmenu`.
- 2D View: `##v2ddockmenu`, `##v2dvstylepop`, `##v2dvfillpop`, `##v2dvcornpop`, `##v2dtoolpick`, `##v2dsympop`,
  `##v2drendermenu`, `##v2dnodepop`, `##v2dfilldlg`, `##v2deffpop`, `##v2ddocpop`, `##v2dbarmore`, `##v2dadjpop`.
- Sample Studio: `##studioDockMenu`, `##studioWaveMenu`, `##studioToolMenu`, `##studioSliceMenu`, `##studioNodeMenu`, `##studioExportMenu`.
- Piano Roll: `##pianoDockMenu`, `##pianoToolMenu`, `##pianoRecDeviceMenu`, `##pianoQuantMenu`, `##pianoGridMenu`, `##pianoFxMenu`, `##pianoFx`.
- Drums: `##drumDockMenu`, `##drumGroove`.
- Curve Editor: `##curveMenu`, `##curveDockMenu`.
- Field Patcher: `##fpPick`, `##fpNodeMenu`, `##fpLinkMenu`, `##fpDockMenu`, `##fpNew`, `##fpFamily`.
- Session: `##sessiontrackctx`, `##sessiondockctx`, `##sessionslotctx`, `##sessionscenectx`, `##sessionquantpop`.
- Arrange: `##arrangeviewportctx`, `##arrangeemptyspacectx`, `##arrangeaddtrackpopup`, `##arrangeRenderPopup`, `##arrangeDockMenu`.
- Modulation matrix: `##modmatrixctx`, `##modbind`, `##rowmenu`.
- Mixer: `##mixerpanelctx`. Terminal: `##terminalDockMenu`, `##terminalquit`.
- Transport and canvas: `##metronomePopup`, `##sharepatch`, `##slotsmap`, `##fielddevicesave`, `##perfaddcontrolmenu`.
- Also the 75 combo boxes in src/app (see the counts above; corrected from 38) and the search overlay (6 `search` popups).

## Shot budget (estimate)

| Group | Shots |
|---|---|
| A menus | 13 |
| B panels | 6 |
| C settings tabs and section cards | 3 + 27 + 1 |
| D dialogs | 7 |
| E dropdowns and context menus | about 110 |
| Node cards, 372 types x (closed, params open, first dropdown) | about 1,100 |

At roughly 15-20 s per app launch on this Mac, the node cards alone are several hours of serial capture. This must run
when the gate is idle and one capture process is allowed at a time.

## Open items

- Node count 377 registered / 372 spawnable is taken from the brief. It is not re-counted here.
- The off-screen tool hardcodes `build-dev/` and the control port; it needs a `--app` path and a free port before a
  release run, or the gate and capture collide.
- The Output node export field shows the owner's Desktop path. Blank it before the release run (docs/shots/README).
