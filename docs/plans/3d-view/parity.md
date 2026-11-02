# 3D View: Blender parity tracker

Blender's 3D viewport is the reference for the whole panel, not just navigation. Infinite's one difference: scene objects are the
outputs of canvas nodes. Method: pick one row, use that feature in Blender and note what it does (names, options, keys, behaviour),
describe it in plain words, write Infinite's version, test side by side, tick the row. Blender is GPL: the owner allows reading its
source and UI scripts to understand how a feature works and take inspiration. Nothing is copied: describe the behaviour in your
own words and write the code fresh. (AGENTS.md rule 1 carries this exception. Keep the clone in
`~/blender-ref`, outside the repo, and delete it when finished.)

`blender-inventory.md` lists every setting (with every option), every operator and every default key Blender exposes for these
areas, and whether Infinite's 3D View sources name it. It is made by `tools/3dview_parity_probe.py`, which asks the installed
Blender, headless, for its own settings, operators and keymap. It is a keyword probe: a "-" can be the same thing named
differently, so confirm by testing. Rows the README puts out of scope (modelling, sculpting, painting, rigging, modifiers,
materials, particles, physics, animation data) are still listed, marked "out", and not counted below.

## Coverage by area (Blender 5.1, every row decided in `parity-map.tsv`)

Status counts per area. done = Infinite has it (cited), missing = in scope and not built, deferred = needs renderer work or
owner approval, out = outside the README. The probe prints the undecided count: `python3 tools/3dview_parity_probe.py --unresolved`.

| Area | Done | Missing | Deferred | Out | Mapped |
|---|---|---|---|---|---|
| Viewport: View tab and header | 27 | 4 | 9 | 26 | 66 |
| Viewport: 3D cursor | 5 | 0 | 0 | 1 | 6 |
| Viewport: shading | 10 | 0 | 28 | 0 | 38 |
| Viewport: overlays | 19 | 0 | 3 | 10 | 32 |
| Transform, snap, pivot | 25 | 0 | 0 | 26 | 51 |
| Object | 18 | 0 | 23 | 35 | 76 |
| Camera | 44 | 0 | 20 | 23 | 87 |
| Light | 18 | 0 | 42 | 0 | 60 |
| Light probe | 0 | 0 | 36 | 0 | 36 |
| Constraints (aim only) | 7 | 0 | 0 | 8 | 15 |
| World | 3 | 0 | 14 | 1 | 18 |
| Render | 36 | 1 | 56 | 45 | 138 |
| Output | 1 | 0 | 0 | 41 | 42 |
| Scene | 10 | 1 | 0 | 24 | 35 |
| View layer | 3 | 0 | 15 | 26 | 44 |
| Outliner | 12 | 0 | 0 | 11 | 23 |
| Preferences: navigation and input | 4 | 4 | 3 | 39 | 50 |
| Preferences: viewport | 5 | 8 | 0 | 14 | 27 |
| **All settings** | **247** | **18** | **249** | **330** | **844** |
| **Operators** | **77** | **33** | **14** | **117** | **241** |
| **Keymap entries** | | | | | not mapped: compare with README Controls by hand |

Re-run the probe after each block: `python3 tools/3dview_parity_probe.py --md docs/plans/3d-view/blender-inventory.md`.

## Behaviour the probe can't judge

The inventory lists names, options and keys. Whether a feature behaves like Blender (picking, gestures, editors outside the
viewport) is tracked by hand here.

| Area | Blender | Infinite today | Decision |
|---|---|---|---|
| Undo | Undo, Redo, Undo History | Each gesture, remove, group, duplicate and cursor move pushes one canvas undo checkpoint | Covered; Undo History is the canvas's |
| Repeat and adjust | Repeat Last (Shift+R), Adjust Last Operation (F9), Menu Search (F3) | Repeat Last (Shift+R) for move, rotate, scale: built, to test | Decided: Adjust Last (F9) out, the transform fields and Repeat Last cover it; Menu Search (F3) out, the menus are shallow |
| Toolbar (T) | Select box/circle/lasso, Cursor, Move/Rotate/Scale/Transform, Annotate, Measure | Shift+Space tool pick (W G R S) | Built: the Select chip's right-click menu picks Box, Lasso, Cursor or Measure (gold line with length and X/Y/Z deltas), to test. Decided: Annotate out, no drawing layer |
| Picking | Click again cycles to the object behind, Alt+click lists overlapping objects, active vs selected | Click again on the same spot cycles to the next one back: built, to test | Decided: click-cycle covers it, no list |
| Multi-edit | Alt+drag a field edits all selected, Copy to Selected, Ctrl+L copy transforms | Object tab shows the active item; Alt while editing Location, Rotation or Scale copies the value to all selected: built, to test | Ctrl+L / Object > Copy Transforms from Active: built, to test. Decided: no per-field Copy to Selected button, Alt+edit covers it |
| Aim at object | Track To / Damped Track constraint | Camera and light Data tab > Look at: built, to test | In scope as a "Look at" object field on the camera and light Data tab, following the object live. No constraint stack |
| Other constraints | Follow Path, Child Of, Copy Location, ... | Groups | Out: parenting is groups, paths are node animation |
| Keyframes | Insert keyframe (I), auto-key, timeline, motion paths | Placement stacks on what the node animates | Out: motion comes from nodes and modulation (see "Modulated placements"), never panel keys |
| Camera switching over time | Bind camera to markers | none | Out for v1: this is Shots (README) |
| Render window | F12 result window, render slots, compare slots | Render Image writes to the Output node's path | Built: the Render result card remembers the latest and the previous still, each with a Reveal button, to test. Decided: no in-panel compare view, the file manager opens both |
| Navigation preferences | Turntable vs trackball, zoom to mouse, auto depth, orbit around selection, emulate 3-button mouse, NDOF | Turntable, zoom to pointer, orbit around selection, invert zoom | Decided out: trackball (turntable keeps the horizon up) and auto depth (zoom to pointer covers it); NDOF out |
| File drop | Drop image, glTF or OBJ on the view | none | Decided: the canvas owns file drops and makes the node; the panel then adds it with Add. No second drop target |

## Infinite only

Blender has no answer for these, so each is tested against the vision (live instrument, flow first), not against Blender.

| Area | Question | Today | Decision |
|---|---|---|---|
| Canvas and 3D selection | Picking an object selects its node, and the other way round | Both ways (`FollowCanvasSelection`, select node from the panel) | Covered; add a test row |
| Modulated placements | Can location, rotation, scale take a modulator or MIDI map | No: placements are not params | Decided: deferred as its own feature (placements are not params); until then motion comes from the node |
| Live output | 3D View image to the projector, with fps cost | Only through an Output node | Decided: manual test row in the checklist |
| Scale budget | fps at 50, 128, 256 objects, panel open and closed | Not measured | Decided: manual test row in the checklist |
| Patch round trip | Placements, groups, panel scenes, locks reopen | Saves with the scene | Test open, play, picture (not bit-exact) |

## Done and compared

| Area | Item |
|---|---|
| Navigation | Orbit 0.4 deg/px for drag and trackpad, pan 1:1, wheel notch 1.2x, pivot carried at minimum zoom, orbit over the poles, orbit-around-selection off by default |
| Navigation | Smooth view: numpad views, Frame, orbit steps ease over 0.2 s (smoothstep, shorter for small turns), retargets mid-glide (built, not yet tested) |
| Select | Select tool modes: box, lasso, 3D cursor placement, measure (built, to test) |
| Select | Select menu (All, None, Invert Ctrl+I, Random, Checker Deselect, Linked, Same Type), Ctrl+drag lasso (built, to test) |
| Snap and Object | Snap submenu (grid, cursor, keep offset, active, cursor to active/origin), Mirror X/Y/Z, Hide Unselected Shift+H (built, to test) |
| View menu | Viewpoints, perspective toggle, frame, local view, align camera to view/selected, centre cursor and frame all, zoom (built, to test) |
| Transform | Proportional editing (O, 6 falloffs, size by wheel/PageUp/PageDown, reach ring) for G, R, S (built, to test) |
| Item tab | Per-axis lock for location, rotation and scale (built, to test) |
| Axes | Z up, Y away from the viewer, in gizmo, keys, fields, cursor, grid |

## Status of the plan

Built means compiled and in the Desktop dev build; tick it in `test-checklist.md` once compared with Blender.

| # | Area | Built | Deferred, and why |
|---|---|---|---|
| 1 | Navigation | Invert zoom direction (sidebar View > Navigation), view roll (Shift+Numpad 4/6, View menu; any orbit or numpad view resets it) | Pole horizon blend, emulate numpad: cosmetic / laptop-only |
| 2 | Select | Menu (All, None, Invert, Random, Checker, Linked, Same Type), Ctrl+I, Ctrl+drag lasso | X-ray select-through: objects are picked by centre, so there is nothing hidden to select through; revisit with face-accurate picking |
| 3 | Transform | Snap submenu (grid, cursor, keep offset, active, cursor to active/origin), Mirror X/Y/Z, proportional editing (O, six falloffs, size by wheel), snap to other objects' origins | Snap targets for faces and vertices: items are whole node outputs, not meshes |
| 4 | Item and Object | Per-axis locks for location, rotation, scale; Dimensions field (editable while the item is not rotated) | Delta transform, rotation modes, display-as: need new Item fields and render changes |
| 5 | Camera | Align camera to view / to selected, Zoom Camera 1:1 (View menu) | Border, background image: new camera modes |
| 6 | Shading and overlays | Overlays card (text info, statistics, origins of all), Gizmos card (navigate, move/rotate/scale), Hide Unselected | X-ray, matcap, cavity: need Render 3D shader work, a separate perf-gated change |
| 7 | Add and Object menus | Object submenu (Snap, Mirror, Hide Selected/Unselected), View submenu (viewpoints, frame, local view, align, zoom) | Parent, collections: the scene has groups only |
| 8 | World and Light | already covered by the World and Data tabs | Mist, light radius, spot blend: new Render 3D parameters |
| 9 | Render and Output | already covered by Render 3D and Output node params | Passes and colour management labels: node work, not the 3D View |
| 10 | Outliner | already a list with rename, hide, select | Filters, restrict toggles, drag and drop: new panel behaviour |
| 11 | Viewport extras | none | Quad view, ruler, clip and render borders, pies: new modes, need owner approval |
| 12 | Behaviour the probe can't judge | Undo, tool pick | Built: Repeat Last, select modes, click-cycle, Alt+drag multi-edit, Ctrl+L, Look at, render slots. Decided out: Adjust Last, Menu Search, Alt+click list, trackball, auto depth, file drop |
| 13 | Infinite only | Selection sync, saving | Modulated placements (new behaviour, approval), projector test, scale budget, round-trip test |

Every row is now built or decided (2026-11-02). Rows left in the Deferred column need new modes or renderer work and were
decided out of this pass; reopen one when the owner asks.

## Missing, ranked

Every row the map marks "missing", grouped by area and ordered by how often a composer touches the area (camera, light and
transform first, preferences last). The "where" is the panel tab or node it belongs on. Regenerate with the map, never by hand.

### Viewport: View tab and header (4)

- Tool Settings (`SpaceView3D.show_region_tool_header`): View3D panel header: tool settings strip
- Toolbar (`SpaceView3D.show_region_toolbar`): View3D panel: a tool column beside the view
- Light Probe (`SpaceView3D.show_object_viewport_light_probe`): View tab: per-type visibility filter
- Light Probe (`SpaceView3D.show_object_select_light_probe`): View tab: per-type selectability filter

### Render (1)

- Stamp Render Time (`RenderSettings.use_stamp_render_time`): Render tab > Output: burn-in stamp fields

### Scene (1)

- Start Frame (`Scene.frame_start`): Render tab > Frame Range: start and step

### Preferences: navigation and input (4)

- Auto Perspective (`PreferencesInput.use_auto_perspective`): View tab > Navigation: auto perspective
- Continuous Grab (`PreferencesInput.use_mouse_continuous`): View tab > Navigation: wrap the pointer while dragging
- Orbit Sensitivity (`PreferencesInput.view_rotate_sensitivity_turntable`): View tab > Navigation: orbit sensitivity
- Orbit Sensitivity (`PreferencesInput.view_rotate_sensitivity_trackball`): View tab > Navigation: orbit sensitivity

### Preferences: viewport (8)

- Mini Axes Type (`PreferencesView.mini_axis_type`): View tab: navigation gizmo size and brightness
- Mini Axes Size (`PreferencesView.mini_axis_size`): View tab: navigation gizmo size and brightness
- Mini Axes Brightness (`PreferencesView.mini_axis_brightness`): View tab: navigation gizmo size and brightness
- Smooth View (`PreferencesView.smooth_view`): View tab > Navigation: smooth view time
- Rotation Angle (`PreferencesView.rotation_angle`): View tab > Navigation: numpad step angle
- Gizmo Size (`PreferencesView.gizmo_size`): View tab: gizmo size
- Navigate Gizmo Size (`PreferencesView.gizmo_size_navigate_v3d`): View tab: gizmo size
- Align Object To (`PreferencesEdit.object_align`): Add menu: align new items to world, view or cursor

### Operators (33)

- Add Camera Preset (`CAMERA_OT_preset_add`): Camera Data tab: camera presets
- Align Objects (`OBJECT_OT_align`): Object menu: Align
- Copy Global Transform (`OBJECT_OT_copy_global_transform`): Object menu: copy transform to clipboard
- Clear All Restrict Render (`OBJECT_OT_hide_render_clear_all`): Object menu: show all in renders
- Restrict Render Unselected (`OBJECT_OT_isolate_type_render`): Object menu: render only one type
- Clear Location (`OBJECT_OT_location_clear`): Object menu: Clear Location, Rotation, Scale
- Paste Global Transform (`OBJECT_OT_paste_transform`): Object menu: paste transform from clipboard
- Randomize Transform (`OBJECT_OT_randomize_transform`): Object menu: randomize transform
- Clear Rotation (`OBJECT_OT_rotation_clear`): Object menu: Clear Location, Rotation, Scale
- Clear Scale (`OBJECT_OT_scale_clear`): Object menu: Clear Location, Rotation, Scale
- Select Camera (`OBJECT_OT_select_camera`): Select menu: select the active camera
- Select Pattern (`OBJECT_OT_select_pattern`): Select menu: select by name pattern
- Place Object Under Mouse (`OBJECT_OT_transform_to_mouse`): Object menu: move to mouse
- Drag and Drop (`OUTLINER_OT_item_drag_drop`): Scene tab: drag to reorder or drop into a scene
- Drop Object to Scene (`OUTLINER_OT_scene_drop`): Scene tab: drag to reorder or drop into a scene
- Box Select (`OUTLINER_OT_select_box`): Scene tab: drag a box over rows
- Walk Select (`OUTLINER_OT_select_walk`): Scene tab: arrow keys walk the list
- Show Active (`OUTLINER_OT_show_active`): Scene tab: scroll to the active item
- Viewport Render (`RENDER_OT_opengl`): Render tab: render the viewport as a still
- Cancel Render View (`RENDER_OT_view_cancel`): Render tab: cancel a running render
- Trackball (`TRANSFORM_OT_trackball`): Rotate: trackball mode (R twice)
- Copy Objects (`VIEW3D_OT_copybuffer`): Object menu: copy and paste items between scenes
- Remove from Local View (`VIEW3D_OT_localview_remove_from`): View menu: remove selected from local view
- Paste Objects (`VIEW3D_OT_pastebuffer`): Object menu: copy and paste items between scenes
- Ruler Add (`VIEW3D_OT_ruler_add`): Select chip: persistent ruler beside Measure
- Ruler Remove (`VIEW3D_OT_ruler_remove`): Select chip: persistent ruler beside Measure
- Circle Select (`VIEW3D_OT_select_circle`): Select chip: circle select
- Snap Cursor to Grid (`VIEW3D_OT_snap_cursor_to_grid`): Snap menu: cursor to grid
- Frame Camera Bounds (`VIEW3D_OT_view_center_camera`): View menu: centre on camera or on the pointer
- View Lock Center (`VIEW3D_OT_view_center_lock`): View tab: lock the view to the active item
- Center View to Mouse (`VIEW3D_OT_view_center_pick`): View menu: centre on camera or on the pointer
- View Lock Clear (`VIEW3D_OT_view_lock_clear`): View tab: lock the view to the active item
- View Lock to Active (`VIEW3D_OT_view_lock_to_active`): View tab: lock the view to the active item
