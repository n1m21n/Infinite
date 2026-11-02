# 3D View keymap vs Blender (Object Mode, default keymap)

Source: Blender's default keymap as documented in the 4.x manual. The 3D View
handles keys only while the pointer is over it. `Ctrl` reads `Cmd` on macOS.

| Blender key | Blender action | Infinite | Note |
|---|---|---|---|
| G / R / S | Move / rotate / scale | Same | X/Y/Z lock, Shift+axis plane, typed values, Enter/Esc |
| Alt+G / R / S | Clear location / rotation / scale | Same | |
| Shift+Space | Tool picker | Same | then W / G / R / S |
| A / Alt+A | Select all / none | Same | |
| Ctrl+I | Invert selection | Same | fixed in block L (inverted twice) |
| Shift+D | Duplicate | Same | |
| Alt+D | Linked duplicate | No | items already point at nodes; a duplicate is linked by nature |
| X / Delete | Delete | Same | removes from the view; nodes stay on the canvas |
| H / Shift+H / Alt+H | Hide / hide others / show all | Same | |
| Ctrl+G | Group | Same | Ctrl+Alt+G ungroups |
| Ctrl+P / Alt+P | Parent / clear parent | Follow / Clear Follow | added in block L |
| Ctrl+L | Link/Transfer data menu | Copy transforms from active | differs on purpose: there is no data to link |
| Shift+R | Repeat last | Same | |
| F2 | Rename | Same | |
| F / Numpad . | Frame selected | Same | Blender: Numpad . only; F added for laptops |
| Home | Frame all | Same | in camera view resets zoom/pan |
| Numpad 1 / 3 / 7 (+Ctrl) | Axis views | Same | top-row digits also work |
| Numpad 5 | Persp / ortho | Same | |
| Numpad 2 4 6 8 / 9 | Orbit steps / flip | Same | |
| Shift+Numpad 4 / 6 | Roll | Same | fixed in block L (never fired) |
| Numpad 0 / Ctrl+0 / Ctrl+Alt+0 | Camera view / set active / camera to view | Same | |
| Numpad / | Local view | Same | also / |
| Numpad + / - | Zoom | Same | also = / - |
| Shift+B | Zoom border | Same | |
| Shift+C | Centre cursor, frame all | Same | |
| Z | Shading pie | Cycle | no pie menus in Infinite; chips in the header |
| Shift+Z | Wireframe toggle | Same | |
| Alt+Z | X-ray | Same | block J |
| Shift+Alt+Z | Overlays | Same | added in block L; Ctrl+` also toggles |
| Ctrl+` | Gizmos | Overlays | differs: gizmos have their own toggles in the View tab |
| . / , | Pivot / orientation pie | Menu | |
| O | Proportional editing | Same | wheel / PageUp/Down resizes during a move |
| Shift+O | Proportional falloff | No | set in the sidebar |
| Shift+Tab | Snap toggle | Same | |
| N | Sidebar | Same | |
| Shift+A | Add menu | Same | |
| Shift+S | Snap pie | Right-click > Object > Snap | no pie |
| F12 / Ctrl+F12 | Render image / animation | Same | |
| Shift+` | Fly / walk | Right-drag + WASDQE | |
| M | Move to collection | No | scenes and groups replace collections |
| Ctrl+A | Apply transform | No | transforms belong to the item, the node stays clean |
| I / Alt+I | Insert / clear keyframe | No | motion comes from modulation, Motion card and the timeline |
| Tab | Edit mode | No | geometry is edited in nodes, not in the view |
| F3 | Search | No | the canvas search covers nodes |
| F9 | Adjust last operation | No | |
| Ctrl+B / Alt+B | Render region / clipping region | No | candidates for a later block |
| Shift+G / Shift+L | Select grouped / linked | Menu | right-click > Select |
