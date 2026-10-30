# 3D View

The final stage of a 3D patch: arrange node outputs in one scene, look through a camera, render it.
Infinite's 3D stays abstract, procedural and generative. This is not an editor: no modelling, sculpting,
UVs or rigging. The canvas is the source. The Add menu (Shift+A or +) only spawns real Camera and Light nodes.
Blender is the reference for the panel's grammar, keys and settings names.

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
| Render | Sampling, Shadows, Ambient Occlusion, Bloom, Color Management |
| Output | Format (default 1920x1080), Frame Range, colour depth, Render Animation |
| World | Surface (None, Background, Environment texture), Strength, HDRI, Ambient, Reflections |
| Object | Transform, Lit by |
| Data | Camera: lens, shift, clip, sensor, depth of field. Light: its node's settings |
| Viewport | Snap, Passepartout, Safe Areas, Composition Guides |

## Controls (mouse over the view)

Blender's keymap.

| Key | Does |
|---|---|
| G / R / S | Move, rotate, scale. X Y Z locks an axis, Shift+axis locks a plane, twice for local. Type a number. |
| Alt+G / R / S | Clear location, rotation, scale |
| Ctrl / Shift | Snap / fine, during the gesture |
| Enter or click, Esc or right-click | Confirm, cancel |
| Shift+Space then W G R S | Pick a tool |
| F, period, Home, Shift+C | Frame selection, frame all, reset the view |
| 1 3 7 9 5, 2 4 6 8 | Views, flip, ortho, orbit steps |
| 0, Ctrl+0, Ctrl+Alt+0 | Through the camera, set active camera, camera to view |
| Z, Shift+Z | Shading, toggle wire |
| N | Sidebar |
| H, Alt+H | Hide, show all |
| A, Alt+A, Shift+D, X or Delete | Select all, none, duplicate, remove from scene |
| Shift+A | Add a camera or light |
| F12 | Render |
| Two-finger, Shift+scroll, pinch | Orbit, pan, zoom (middle drag or Alt+left also orbit) |
| Hold right + WASD QE | Fly |
| Shift+3 (anywhere) | Open the panel |

## Known limits

- Spot and area lights move with their placement but still aim at the origin.
- Not built: bokeh blades, motion blur, ray tracing, volumes, light paths, Freestyle, resolution scale,
  frame step, background images. File format and path stay in the render session.
- Render is Infinite's own GL renderer. EEVEE is part of Blender and cannot be embedded. A bridge that
  hands the scene to an installed Blender is the only cheap route to real EEVEE and Cycles, and is not built.
- Alpha reaches the file only in formats that carry it.
- Shots and saved light rigs are not in v1.
