# 3D View

The final stage of a 3D patch: arrange node outputs in one scene, look through a camera, render it.
Infinite's 3D stays abstract, procedural and generative. This is not an editor: no modelling, sculpting,
UVs or rigging, and no add menu, because the canvas is the source.

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
| Write back | Never. Moving something in the panel does not touch its node. |
| Same node twice | Allowed, each is its own item. |
| Deleted node | Stays listed as "source missing", placement kept. |
| Bypassed node | Hidden, placement kept. |
| Background | Transparent until a world is set: colour, Environment node or any image node. |
| Per-object lighting | "Lit by": each item picks which scene lights reach it. Up to 8 lights. |
| Add a Render 3D | Brings its geometry, lights, camera and environment. Its lights light only what it brought. |
| Panel closed | The editor render costs nothing. The node still renders if an Output is wired. |
| Limit | 256 objects; past that the panel says how many were left out. |

## Controls (mouse over the view)

| Key | Does |
|---|---|
| G / R / S | Move, rotate, scale. X Y Z locks an axis, twice for local. Type a number. |
| Ctrl / Shift | Snap / fine, during the gesture |
| Enter or click, Esc or right-click | Confirm, cancel |
| Q W E T | Select, move, rotate, scale tool |
| F, Home | Frame selection, frame all |
| 1 3 7, 0 | Front, side, top, through the camera |
| Z | Wire, Solid, Final |
| H, Alt+H | Hide, show all |
| A, Shift+D, Delete | Select all, duplicate, remove from scene |
| Two-finger, Shift+scroll, pinch | Orbit, pan, zoom (middle drag or Alt+left also orbit) |
| Hold right + WASD QE | Fly |
| Shift+3 (anywhere) | Open the panel |

## Known limits

- Spot and area lights move with their placement but still aim at the origin.
- Alpha reaches the file only in formats that carry it.
- Shots and saved light rigs are not in v1.
