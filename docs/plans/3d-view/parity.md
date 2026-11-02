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
| Viewport: View tab and header | 15 | 16 | 9 | 26 | 66 |
| Viewport: 3D cursor | 1 | 4 | 0 | 1 | 6 |
| Viewport: shading | 6 | 4 | 28 | 0 | 38 |
| Viewport: overlays | 7 | 12 | 3 | 10 | 32 |
| Transform, snap, pivot | 10 | 15 | 0 | 26 | 51 |
| Object | 11 | 7 | 23 | 35 | 76 |
| Camera | 44 | 0 | 20 | 23 | 87 |
| Light | 18 | 0 | 42 | 0 | 60 |
| Light probe | 0 | 0 | 36 | 0 | 36 |
| Constraints (aim only) | 2 | 5 | 0 | 8 | 15 |
| World | 3 | 0 | 14 | 1 | 18 |
| Render | 14 | 23 | 56 | 45 | 138 |
| Output | 1 | 0 | 0 | 41 | 42 |
| Scene | 4 | 7 | 0 | 24 | 35 |
| View layer | 3 | 0 | 15 | 26 | 44 |
| Outliner | 2 | 10 | 0 | 11 | 23 |
| Preferences: navigation and input | 4 | 4 | 3 | 39 | 50 |
| Preferences: viewport | 4 | 9 | 0 | 14 | 27 |
| **All settings** | **149** | **116** | **249** | **330** | **844** |
| **Operators** | **75** | **35** | **14** | **117** | **241** |
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

### Viewport: View tab and header (16)

- Tool Settings (`SpaceView3D.show_region_tool_header`): View3D panel header: tool settings strip
- Toolbar (`SpaceView3D.show_region_toolbar`): View3D panel: a tool column beside the view
- Lock to Cursor (`SpaceView3D.lock_cursor`): View tab > Navigation
- Lens (`SpaceView3D.lens`): View tab: viewport lens (the view has a fixed lens)
- Clip Start (`SpaceView3D.clip_start`): View tab: viewport clip (the camera has its own)
- Clip End (`SpaceView3D.clip_end`): View tab: viewport clip (the camera has its own)
- Show Light Size (`SpaceView3D.show_gizmo_light_size`): light gizmo handles in the viewport
- Show Light Look-At (`SpaceView3D.show_gizmo_light_look_at`): light gizmo handles in the viewport
- Show Camera Lens (`SpaceView3D.show_gizmo_camera_lens`): camera gizmo handles in the viewport
- Show Camera Focus Distance (`SpaceView3D.show_gizmo_camera_dof_distance`): camera gizmo handles in the viewport
- Camera (`SpaceView3D.show_object_viewport_camera`): View tab: per-type visibility filter
- Light (`SpaceView3D.show_object_viewport_light`): View tab: per-type visibility filter
- Light Probe (`SpaceView3D.show_object_viewport_light_probe`): View tab: per-type visibility filter
- Camera (`SpaceView3D.show_object_select_camera`): View tab: per-type selectability filter
- Light (`SpaceView3D.show_object_select_light`): View tab: per-type selectability filter
- Light Probe (`SpaceView3D.show_object_select_light_probe`): View tab: per-type selectability filter

### Viewport: 3D cursor (4)

- Quaternion Rotation (`View3DCursor.rotation_quaternion`): 3D Cursor card: cursor has a location only
- Axis-Angle Rotation (`View3DCursor.rotation_axis_angle`): 3D Cursor card: cursor has a location only
- Euler Rotation (`View3DCursor.rotation_euler`): 3D Cursor card: cursor has a location only
- Rotation Mode (`View3DCursor.rotation_mode`): 3D Cursor card: cursor has a location only

### Viewport: shading (4)

- Outline (`View3DShading.show_object_outline`): Overlays card: selection outline in Solid
- Background (`View3DShading.background_type`): View tab: Solid view background
- Background Color (`View3DShading.background_color`): View tab: Solid view background
- Outline Color (`View3DShading.object_outline_color`): Overlays card: outline colour

### Viewport: overlays (12)

- Display Grid (`View3DOverlay.show_ortho_grid`): Overlays card: grid and axis options
- Display X Axis (`View3DOverlay.show_axis_x`): Overlays card: grid and axis options
- Display Y Axis (`View3DOverlay.show_axis_y`): Overlays card: grid and axis options
- Display Z Axis (`View3DOverlay.show_axis_z`): Overlays card: grid and axis options
- Grid Scale (`View3DOverlay.grid_scale`): Overlays card: grid and axis options
- Grid Lines (`View3DOverlay.grid_lines`): Overlays card: grid and axis options
- Grid Scale Unit (`View3DOverlay.grid_scale_unit`): Overlays card: grid and axis options
- Outline Selected (`View3DOverlay.show_outline_selected`): Overlays card: selection outline
- Show 3D Cursor (`View3DOverlay.show_cursor`): Overlays card: hide the 3D cursor
- Show Performance (`View3DOverlay.show_performance`): Overlays card: frame time line
- Extras (`View3DOverlay.show_extras`): Overlays card: hide camera and light markers
- Light Colors (`View3DOverlay.show_light_colors`): Overlays card: tint light markers

### Transform, snap, pivot (15)

- Projected from View (`ToolSettings.use_proportional_projected`): Proportional Editing card: projected from view
- Only Locations (`ToolSettings.use_transform_pivot_point_align`): Transform card: only locations toggle
- Align Rotation to Target (`ToolSettings.use_snap_align_rotation`): Snap card: align rotation to target
- Absolute Increment Snap (`ToolSettings.use_snap_grid_absolute`): Snap card: absolute increment mode
- Rotation Precision Increment (`ToolSettings.snap_angle_increment_3d_precision`): Snap card: precision step with Shift
- Snap Target (`ToolSettings.snap_target`): Snap card: closest, centre, median, active
- Snap onto Selectable Only (`ToolSettings.use_snap_selectable`): Snap card: only onto selectable items
- Use Snap for Translation (`ToolSettings.use_snap_translate`): Snap card: snap per tool toggles
- Use Snap for Rotation (`ToolSettings.use_snap_rotate`): Snap card: snap per tool toggles
- Use Snap for Scale (`ToolSettings.use_snap_scale`): Snap card: snap per tool toggles
- Plane Axis (`ToolSettings.plane_axis`): Add menu: placement plane for new items
- Auto Axis (`ToolSettings.plane_axis_auto`): Add menu: placement plane for new items
- Position (`ToolSettings.plane_depth`): Add menu: placement plane for new items
- Orientation (`ToolSettings.plane_orientation`): Add menu: placement plane for new items
- Current Transform Orientation (`TransformOrientationSlot.custom_orientation`): Transform card: custom orientations

### Object (7)

- Track Axis (`Object.track_axis`): Camera and light Data tab: Look at axes
- Up Axis (`Object.up_axis`): Camera and light Data tab: Look at axes
- Disable Selection (`Object.hide_select`): Item tab: disable selection toggle
- Display Bounds (`Object.show_bounds`): Item tab: show bounds
- Display Bounds Type (`Object.display_bounds_type`): Item tab: show bounds
- Display Name (`Object.show_name`): Item tab: show name and axes in the view
- Display Axes (`Object.show_axis`): Item tab: show name and axes in the view

### Constraints (aim only) (5)

- Influence (`Constraint.influence`): Camera and light Data tab: Look at influence
- Track Axis (`TrackToConstraint.track_axis`): Camera and light Data tab: Look at axes
- Up Axis (`TrackToConstraint.up_axis`): Camera and light Data tab: Look at axes
- Target Z (`TrackToConstraint.use_target_z`): Camera and light Data tab: Look at target Z up
- Track Axis (`DampedTrackConstraint.track_axis`): Camera and light Data tab: Look at axes

### Render (23)

- Pixel Aspect X (`RenderSettings.pixel_aspect_x`): Render tab > Format: pixel aspect
- Pixel Aspect Y (`RenderSettings.pixel_aspect_y`): Render tab > Format: pixel aspect
- FPS Base (`RenderSettings.fps_base`): Render tab > Format: 23.976 / 29.97 rates
- Dither Intensity (`RenderSettings.dither_intensity`): Render tab > Output: dither
- Filter Size (`RenderSettings.filter_size`): Render tab > Sampling: pixel filter width
- Stamp Time (`RenderSettings.use_stamp_time`): Render tab > Output: burn-in stamp fields
- Stamp Date (`RenderSettings.use_stamp_date`): Render tab > Output: burn-in stamp fields
- Stamp Frame (`RenderSettings.use_stamp_frame`): Render tab > Output: burn-in stamp fields
- Stamp Frame (`RenderSettings.use_stamp_frame_range`): Render tab > Output: burn-in stamp fields
- Stamp Camera (`RenderSettings.use_stamp_camera`): Render tab > Output: burn-in stamp fields
- Stamp Lens (`RenderSettings.use_stamp_lens`): Render tab > Output: burn-in stamp fields
- Stamp Scene (`RenderSettings.use_stamp_scene`): Render tab > Output: burn-in stamp fields
- Stamp Note (`RenderSettings.use_stamp_note`): Render tab > Output: burn-in stamp fields
- Stamp Render Time (`RenderSettings.use_stamp_render_time`): Render tab > Output: burn-in stamp fields
- Stamp Note Text (`RenderSettings.stamp_note_text`): Render tab > Output: burn-in stamp fields
- Stamp Output (`RenderSettings.use_stamp`): Render tab > Output: burn-in stamp
- Stamp Labels (`RenderSettings.use_stamp_labels`): Render tab > Output: burn-in stamp fields
- Font Size (`RenderSettings.stamp_font_size`): Render tab > Output: burn-in stamp fields
- Text Color (`RenderSettings.stamp_foreground`): Render tab > Output: burn-in stamp fields
- Background (`RenderSettings.stamp_background`): Render tab > Output: burn-in stamp fields
- Use Simplify (`RenderSettings.use_simplify`): Render tab > Sampling: simplify for preview
- Look (`ColorManagedViewSettings.look`): Render tab > Color Management
- Gamma (`ColorManagedViewSettings.gamma`): Render tab > Color Management

### Scene (7)

- Start Frame (`Scene.frame_start`): Render tab > Frame Range: start and step
- Frame Step (`Scene.frame_step`): Render tab > Frame Range: start and step
- Stamp Note (`Scene.use_stamp_note`): Render tab > Output: stamp note
- Unit System (`UnitSettings.system`): View tab: scene units
- Rotation Units (`UnitSettings.system_rotation`): View tab: scene units
- Unit Scale (`UnitSettings.scale_length`): View tab: scene units
- Length Unit (`UnitSettings.length_unit`): View tab: scene units

### Outliner (10)

- Case Sensitive Matches Only (`SpaceOutliner.use_filter_case_sensitive`): Scene tab: search and sort options
- Complete Matches Only (`SpaceOutliner.use_filter_complete`): Scene tab: search and sort options
- Sort Alphabetically (`SpaceOutliner.use_sort_alpha`): Scene tab: search and sort options
- Selectable (`SpaceOutliner.show_restrict_column_select`): Scene tab: selectable column
- Disable in Renders (`SpaceOutliner.show_restrict_column_render`): Scene tab: render toggle column
- Filter Objects (`SpaceOutliner.use_filter_object`): Scene tab: filter by object
- Object State Filter (`SpaceOutliner.filter_state`): Scene tab: state filter
- Invert (`SpaceOutliner.filter_invert`): Scene tab: state filter
- Show Lights (`SpaceOutliner.use_filter_object_light`): Scene tab: type filter
- Show Cameras (`SpaceOutliner.use_filter_object_camera`): Scene tab: type filter

### Preferences: navigation and input (4)

- Auto Perspective (`PreferencesInput.use_auto_perspective`): View tab > Navigation: auto perspective
- Continuous Grab (`PreferencesInput.use_mouse_continuous`): View tab > Navigation: wrap the pointer while dragging
- Orbit Sensitivity (`PreferencesInput.view_rotate_sensitivity_turntable`): View tab > Navigation: orbit sensitivity
- Orbit Sensitivity (`PreferencesInput.view_rotate_sensitivity_trackball`): View tab > Navigation: orbit sensitivity

### Preferences: viewport (9)

- Display Playback Frame Rate (FPS) (`PreferencesView.show_playback_fps`): Overlays card: frame rate
- Mini Axes Type (`PreferencesView.mini_axis_type`): View tab: navigation gizmo size and brightness
- Mini Axes Size (`PreferencesView.mini_axis_size`): View tab: navigation gizmo size and brightness
- Mini Axes Brightness (`PreferencesView.mini_axis_brightness`): View tab: navigation gizmo size and brightness
- Smooth View (`PreferencesView.smooth_view`): View tab > Navigation: smooth view time
- Rotation Angle (`PreferencesView.rotation_angle`): View tab > Navigation: numpad step angle
- Gizmo Size (`PreferencesView.gizmo_size`): View tab: gizmo size
- Navigate Gizmo Size (`PreferencesView.gizmo_size_navigate_v3d`): View tab: gizmo size
- Align Object To (`PreferencesEdit.object_align`): Add menu: align new items to world, view or cursor

### Operators (35)

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
- Create Orientation (`TRANSFORM_OT_create_orientation`): Transform card: save a custom orientation
- Delete Orientation (`TRANSFORM_OT_delete_orientation`): Transform card: delete a custom orientation
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
