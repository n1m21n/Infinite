# Proposals A–C (awaiting owner yes)

New UI or new behaviour, so each needs a yes before any code. Decisions are settled
below. Each line is what I'd build, not an open question.

## A. Load-image button on each Material map slot

What it does: each map row on the Material node (albedo, roughness, metallic, normal,
ao, emission, clearcoat, sheen, opacity, transmission) gets a small image button.
Click → file picker → a new Image Source node lands left of the Material, loaded
with the file and cabled into that pin. A row whose pin is already cabled shows no
button: the cable is the truth.

```
 [Image Source: wood.png] ──────────┐
                                    ▼
 ┌ Material ──────────────────────────┐
 │ albedo     ● [img]   <- button    │   click: pick file -> spawn + cable
 │ roughness  ● [img]                │
 │ normal     ●═══ (cabled: no btn)  │
 └────────────────────────────────────┘
```

| Decision | Choice |
|---|---|
| Where the button lives | In the node body row of each map (DrawMaterialParams, GeometryParams.cpp) |
| What gets made | Image Source node (`SpawnNode("Image Source","Source",...)`, `Load(path)`), then `ConnectNodes(img, 0, material, 1+map)` |
| Placement | Left of the Material, stacked by map index so several loads don't overlap |
| Undo | One checkpoint "Load map image" covers spawn + cable |
| Non-colour maps (roughness, metallic, normal, ao, opacity, transmission) | Image Source loaded as-is; the Material already reads these as linear data |
| Drag a file from Finder onto a row | Same path as the button (free once the button exists) |

Build order: button + spawn + cable → stacking placement → drop-on-row → UI audit
covers the new button (one popup, Esc closes, value unchanged).
Size: small, one step.

## B. Edit Mode for meshes

What it does: Tab in the 3D View on a mesh enters Edit Mode. You select vertices,
edges or faces (1/2/3), then G/R/S move them, E extrudes, I insets, X deletes. It
is a node like Sculpt: an **Edit Mesh** node sits after the mesh and holds the edits.

### Data model: replayable op list (chosen)

| | Replayable op list | Baked delta |
|---|---|---|
| Stored | `[select faces {ids}, extrude 0.2, move (0,0.1,0) ...]` | final positions + topology |
| Upstream mesh changes | ops replay on the new mesh: edits follow | delta no longer fits: edits lost |
| Patch size | tiny text | the whole mesh |
| Undo | pop an op | snapshot |
| Fits Infinite | yes: everything upstream stays live | no: breaks "design as you go" |

Chosen: **op list**. Each op names elements by stable selectors, not raw indices:
- faces by **position + normal** (nearest face centre within a tolerance)
- vertices by the welded point id, the same id Sculpt uses
- if a selector finds nothing after an upstream change, that op is skipped and the
  node shows a warning, as Sculpt does when its offsets stop fitting

Ops reuse the core ops that already exist: ExtrudeSelected, InsetFaces,
BevelEdges and Decimate (Mesh.cpp, BevelInset.cpp). Move, rotate and scale are a
matrix on the selected points.

```
 Geometry ──> Edit Mesh ──> Sculpt ──> Material ──> 3D View
              ops: [sel, extrude, move, ...]   replayed each cook (cached by input revision)
```

| Upstream change | What happens |
|---|---|
| Param tweak (size, detail) with the same topology | ops replay, edits follow |
| Topology change (detail 32 → 48) | selectors re-match by position; unmatched ops skip and warn |
| Input swapped for another mesh | same as above; most ops will warn |

### Build order (each its own commit and A/B)
1. Edit Mesh node: op list in the patch, replay, and a cache keyed by input revision.
2. Selection in the 3D View: vertex, edge and face; click, shift-click, box; highlight.
3. G/R/S on the selection, reusing the transform gizmo math.
4. E, I and X through the existing core ops.
5. Selector re-match when upstream changes, plus the warning.
6. Blender A/B for each op (same op in headless Blender → compare meshes), per the
   3D-process rule.

Size: large, about 3 blocks. Steps 1–3 already give a usable "move these faces".

## C. Any 2D output as a textured plane in the 3D View

What it does: dropping any node with a picture output (image, video, shader,
generator, Render 3D) into the 3D View makes a plane showing its live output,
sized to the picture's aspect. It is a normal item: it moves, rotates, scales and
lights like geometry, and it renders in raster and the path tracer.

| Decision | Choice |
|---|---|
| Kind | New `Scene3DNode::Kind::Picture` (today only Geometry, Camera and Light; 2D nodes are `Missing`) |
| Mesh | A unit quad scaled to the output's aspect (height 1 m), built in the scene, not a node |
| Material | Unlit by default (the picture shows as it is); an item toggle makes it lit (it becomes the albedo map) |
| Live | It samples the node's output texture each frame, so a playing video or shader animates |
| Alpha | Respected: a cut-out PNG reads as a cut-out (alpha-tested in raster, transparency in PT) |
| Two-sided | Yes |
| Feedback | A Render 3D that shows its own output on a plane: the plane shows the previous frame, never recurses |
| Path tracer | The texture is uploaded as an emissive (unlit) or albedo (lit) map |

```
 [Shader] ──(drag into 3D View)──> Item{kind Picture, uid shader}
                                   └─ quad (aspect from texture) + unlit material sampling GetOutputTexture()
```

Build order:
1. Kind::Picture, the quad item and raster draw.
2. Lit toggle, alpha and two-sided.
3. Path tracer upload.
4. Feedback guard.
5. Blender A/B: an image on a plane, emissive vs lit.

Size: medium, one block.

## Order if all three get a yes
A (small, a daily win) → C (medium, reuses the existing texture plumbing) → B (large).
