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

## Coverage by area (Blender 5.1, in-scope settings present / all)

| Area | Settings | Options | Out of scope | Missing (first 8, from the inventory) |
|---|---|---|---|---|
| Viewport: View tab and header | 18/66 | 8/10 | 28 | tool settings, sidebar, adjust last operation, render region, region minimum x, region minimum y, region maximum x, region maximum y (+40) |
| Viewport: 3D cursor | 2/6 | 0/8 | 0 | quaternion rotation, axis-angle rotation, euler rotation, rotation mode |
| Viewport: shading | 10/38 | 24/41 | 2 | studiolight, world space lighting, backface culling, cavity, cavity type, curvature ridge, curvature valley, cavity ridge (+20) |
| Viewport: overlays | 2/32 | 3/3 | 64 | show overlays, display grid, display grid floor, display x axis, display y axis, display z axis, grid scale, grid lines (+22) |
| Transform, snap, pivot | 7/51 | 36/56 | 82 | lock object modes, projected from view, connected only, proportional size, proportional size, merge threshold, transform pivot point, only locations (+36) |
| Object | 8/76 | 30/80 | 33 | bounding box, parent type, parent vertices, use final indices, camera parent lock, track axis, up axis, quaternion rotation (+60) |
| Camera | 27/87 | 16/31 | 2 | sensor fit, passepartout alpha, horizontal fov, vertical fov, sensor width, orthographic scale, display size, show limits (+52) |
| Light | 13/60 | 6/8 | 3 | use temperature, temperature, temperature color, specular factor, diffuse factor, transmission factor, volume factor, custom attenuation (+39) |
| Light probe | 8/36 | 7/7 | 4 | clipping, influence, influence distance, visibility bias, visibility bleed bias, visibility blur, display data (deprecated), display data (+20) |
| Constraints (aim only) | 3/15 | 0/26 | 0 | owner space, target space, influence, head/tail, follow b-bone, sub-target, track axis, up axis (+4) |
| World | 9/18 | 5/9 | 5 | finite volume, sun threshold, sun angle, use shadow, shadows resolution limit, shadow filter radius, shadow jitter, shadow jitter overblur (+1) |
| Render | 24/138 | 10/67 | 29 | resolution y, pixel aspect x, pixel aspect y, ppm factor, ppm base, fps base, frame map old, frame map new (+106) |
| Output | 11/42 | 16/105 | 2 | media type, file format, color mode, compression, ycc, cinema, cinema (48), compression (+23) |
| Scene | 3/35 | 9/14 | 20 | current frame, current subframe, current subframe, start frame, end frame, frame step, time jump unit, time jump delta (+24) |
| View layer | 16/44 | 0/0 | 16 | alpha threshold, shader aov, shader aov, active aov index, cryptomatte object, cryptomatte levels, cryptomatte accurate, strand (+20) |
| Outliner | 2/23 | 6/12 | 5 | display mode, case sensitive matches only, complete matches only, sort alphabetically, sync outliner selection, show mode column, exclude from view layer, hide in viewport (+13) |
| Preferences: navigation and input | 6/50 | 9/21 | 0 | zoom style, zoom axis, multi-touch gestures, auto depth, zoom to mouse position, auto perspective, orbit method, continuous grab (+36) |
| Preferences: viewport | 2/27 | 4/6 | 20 | tooltips, python tooltips, display object info, display view name, show splash, display playback frame rate (fps), navigation controls, open on mouse over (+17) |
| **All settings** | **171/844** | **189/504** | **315** | |
| **Operators (features)** | **31/241** | | **253** | add camera preset, add safe area preset, unlink data-block, redo, undo history, undo push, undo and redo, add object (+202) |
| **Keymap entries** | 457 in scope | | 49 | listed, not matched; compare with README Controls by hand |

Read the counts as a floor: Infinite often names a thing differently (Render 3D params, node settings), so many "-" rows are done.
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
