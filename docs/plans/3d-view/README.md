# 3D View

The final stage of a 3D patch: arrange node outputs in one scene, look through a camera, render it.
Infinite's 3D stays abstract, procedural and generative. This is not an editor: no modelling, sculpting,
UVs or rigging. The canvas is the source. The Add menu (Shift+A or +) only spawns real Camera and Light nodes.
Blender is the reference for the panel's grammar, keys and settings names.

It composes, it does not modify: geometry, materials, particles and physics stay in the node chain, and the
panel has no Modifiers, Material, Particles, Physics or Collection tab, and no node stack, not even read-only.
It only improves what comes after adding: composing, camera, lights, render and output.

## What it is

```
 canvas nodes                 3D View node                    export
 Sphere ----+  (by uid,       +---------------+
 Fractal ---+-- no cable) --> | placements    | --image--> Output node --> file
 Camera ----+                 | world, lens   |
 Light -----+                 +---------------+
                                     ^
                              3D View panel edits placements only
```

| Rule | Decision |
|---|---|
| Source of truth | The node. A placement stacks on top of whatever the node animates. |
| Write back | Never for placements. Lens and light cards edit that node's own settings, so both views agree. |
| Same node twice | Allowed, each is its own item. |
| Deleted node | Stays listed as "source missing", placement kept. |
| Bypassed node | Hidden, placement kept. |
| Background | Transparent until a world is set: colour, Environment node or any image node. |
| Per-object lighting | "Lit by": each item picks which scene lights reach it. Up to 8 lights. |
| Add a Render 3D | Brings its geometry, lights, camera and environment. Its lights light only what it brought. |
| Panel closed | The editor render costs nothing. The node still renders if an Output is wired. |
| Limit | 256 objects; past that the panel says how many were left out. |

## Panel

| Tab | Holds |
|---|---|
| Render | Sampling, Shadows, Ambient Occlusion, Bloom, Film, Color Management |
| Output | Format (default 1920x1080, resolution %), Frame Range, colour depth, pass, projection, Render Image, Render Animation |
| World | Surface (None, Background, Environment texture), Strength, HDRI, Ambient, Reflections |
| Object | Transform, Visibility, Animation (keys), Lit by |
| Data | Camera: lens, shift, clip, sensor, depth of field (real f-stop). Light: its node's settings, shadow caster. Both: point at 3D cursor |
| Viewport | Transform, Snap, 3D Cursor, Navigation, Passepartout, Safe Areas, Composition Guides |

The outliner renames (double-click or F2), searches from six items, and takes Ctrl and Shift clicks.
Everything on these tabs saves with the scene.

## Controls (mouse over the view)

Blender's keymap.

| Key | Does |
|---|---|
| G / R / S | Move, rotate, scale. X Y Z locks an axis, Shift+axis locks a plane, twice for the orientation. Type a number. |
| Alt+G / R / S | Clear location, rotation, scale |
| Ctrl / Shift | Snap / fine, during the gesture |
| Enter or click, Esc or right-click | Confirm, cancel |
| Shift+Space then W G R S | Pick a tool |
| F or numpad period, Home, Shift+C | Frame selection, frame all, frame all and zero the cursor |
| Period, comma | Pivot, orientation |
| Shift+B, / | Zoom into a box, local view |
| Shift+Tab, Ctrl+` | Snap, overlays |
| Shift+right-click | Place the 3D cursor |
| I, Alt+I | Insert key, remove key at the current time |
| F2 | Rename |
| 1 3 7 9 5, 2 4 6 8 | Views, flip, ortho, orbit steps |
| 0, Ctrl+0, Ctrl+Alt+0 | Through the camera, set active camera, camera to view |
| Z, Shift+Z | Shading, toggle wire |
| N | Sidebar |
| H, Shift+H, Alt+H | Hide, hide the rest, show all |
| A, Alt+A, Ctrl+I, Shift+D, X or Delete | Select all, none, invert, duplicate, remove from scene |
| Shift+A | Add a camera or light |
| F12, Ctrl+F12 | Render image, render animation |
| Two-finger, Shift+scroll, pinch | Orbit, pan, zoom (middle drag or Alt+left also orbit) |
| Hold right + WASD QE | Fly |
| Shift+3 (anywhere) | Open the panel |

## Known limits

- Not built: circle and lasso select, x-ray, bokeh blades, motion blur, ray tracing, volumes, light paths,
  Freestyle, frame start and step, render region (Ctrl+B), background images. Movie format and path stay in
  the render session; a still goes to the Output node's image path, with no save dialog.
- Light falloff is not shown: the renderer ignores it.
- Render is Infinite's own GL renderer. EEVEE is part of Blender and cannot be embedded. A bridge that
  hands the scene to an installed Blender is the only cheap route to real EEVEE and Cycles, and is not built.
- Alpha reaches the file only in formats that carry it.
- Shots and saved light rigs are not in v1.
