# Fur and hair (approved 2026-11-10: cover everything)

## Gap-closing tracker (NEXT_SESSION.md, 2026-11-11)

| step | what | before | now | gate | state |
|---|---|---|---|---|---|
| A1 | scene tint on `bed_close` | +6/+11/+15 % RGB; live FLIP 0.172, settle 0.168 | live 0.913/0.930/0.932, FLIP 0.127; settle 0.958/0.969/0.939, FLIP 0.099 | < 2 % per channel | tint gone (spread ≤ 3 %); a few % dark left |
| A2 | settle `bed` under the grazing sun | brightness 0.70 | 1.013, FLIP 0.091 | within 5 % | done |
| A3 | raster over-lighting (no GI) | interior 6.6× bright, FLIP 0.79 | interior 1.03×, FLIP 0.103; bed 0.161, bed_close 0.173 | interior FLIP < 0.2 | done; raster sweep inside baseline slack |
| A4 | raster fur two-sided, shadow blotches | `bed_fur_close` 0.402 (1.26× bright) | 0.339 (1.005×); `bed_fur` 0.183 → 0.172; `hair_long` 0.168 → 0.126 | well under 0.40 | done |

A1/A2 cause: sheen. Cycles 4+ sheen is a microfiber LTC lobe; ours was Charlie + Neubelt, up to 13× too bright
at mid roughness and the wrong shape (too little at grazing light). Now `Render3DSheen.h` holds Cycles' measured
furnace albedo plus an LTC transform fitted to Blender renders under ten sun directions, used by all four shader
sites (GL forward, GL path tracer, live, settle). Sheen-only probes match Cycles within 3 % in every engine.
`blender_scene.py` now passes Sheen Roughness to primitives (it was ignored).

A3: raster now has bounce light from a probe grid. The GL path tracer traces it in a probe mode (16 probes on the
longest side, 128 directions × 4 samples, 3 bounces) whenever geometry, lights or the world change; live it waits
for one still frame. Each probe keeps an ambient cube (six faces) plus each face's mean hit distance and its square.
The forward shader blends the eight probes around a point by trilinear share, by facing, and by a Chebyshev test on
that distance (DDGI's leak guard); reflections start from the probes' light and add the world's sharp detail only in
the share of the world the probes see. Probes sitting inside solids (mostly back faces seen) drop out.

A4: fur meshes carry `twoSided` + `ribbonStart` from Fur. Raster draws them unculled, the shadow map takes only the
skin (strands are far thinner than a shadow texel), and a fur mesh skips the normal map: the blotches were the plush
normal map lighting bumps on the skin that the coat hides in Cycles. Raster has no strand self-shadowing, so ribbon
light is scaled by 0.4 (a strand's visibility averaged root to tip) and its ambient is tinted by the fur colour (light
inside the coat has bounced off other strands).

Goal: the plush in the `bed` / `bed_close` A/B reads as fur, not as a bumpy sheen surface, and the same tool makes
styled long hair with a real hair material. Fur is the last gap between Infinite and Blender on that scene.

| block | what | gate |
|---|---|---|
| 1 | Fur mode: strand ribbons in Geometry Op | `bed_fur` beats `bed` FLIP in live and settle |
| 2 | Guides: a curve input combs and styles the strands | `hair_long` A/B reads the same shape as Blender |
| 3 | Hair BSDF (Material `Hair` model) in GL, live, settle | `hair_melanin` sweep vs Cycles Principled Hair |

## Decision: real strands, as a Geometry Op mode

```
 Mesh ──► Geometry Op [Fur] ──► Mesh (base + strands) ──► Material ──► Scene 3D
              │
              └ strands = thin twisted ribbons rooted on the faces
```

| option | raster | live RT | settle | Blender A/B | verdict |
|---|---|---|---|---|---|
| shells + fins (game fur) | shader per layer | needs any-hit alpha per layer | needs it too | no Blender equivalent | rejected: 3 engines of shader work, wrong silhouettes and shadows in RT |
| **strand ribbons (geometry)** | works as is | works as is (TLAS) | works as is | same mesh as glb, plus Cycles hair curves for the look | **chosen** |
| new "Fur" node | - | - | - | - | rejected: Geometry Op already has the mesh in, mesh out slot |

Strands are plain triangles, so every engine shades them, shadows them and gives them AO with no new
shader code. Fur self-shadowing (the dark roots that make fur read) comes from the path tracer for free.

## Block 1: `Fur` in Geometry Op's operation list

| param | default | range | what it does |
|---|---|---|---|
| Density | 40 | 1..400 per cm² | strands per area, spread by face area, capped by Max Strands |
| Max Strands | 150k | 1k..400k | hard cap (memory: ~40 B per vertex, 150k strands ≈ 35 MB) |
| Length | 0.012 | 0..0.2 m | strand length |
| Length Jitter | 0.3 | 0..1 | random length per strand |
| Width | 0.0006 | m | ribbon width at the root, tapers to 0.2× at the tip |
| Segments | 3 | 1..8 | bends per strand (2 triangles each) |
| Gravity | 0.2 | 0..1 | droops strands toward world -Y |
| Clump | 0.3 | 0..1 | pulls tips toward a shared clump centre |
| Curl | 0 | 0..1 | twists strands around their own axis |
| Seed | 0 | int | |

Fixed choices:
- Keep the base mesh (fur grows on it, the skin shows between strands).
- Each strand gets a random twist, so no viewing angle shows it edge-on everywhere.
- UVs: the strand inherits its root's UV, so a texture colours the fur the same way it colours the skin.
- Vertex colour: the root's (white when the mesh has none). No painted root darkening: live and settle trace the
  real occlusion, and a painted ramp would darken it twice.
- Selection-aware: fur grows only on selected faces when a selection arrives (like the other ops).

## Cost (plush at 150k strands, 3 segments)

| | triangles | memory | build |
|---|---|---|---|
| strands | 900k | ~35 MB | one cook, cached until a param changes |
| live (Metal RT) | TLAS rebuild once | | ≤ 1 frame hitch on change |

On the 8 GB M2 this is fine. Max Strands is the guard.

## Blender A/B

1. `bed_fur`: export the same fur mesh to glb, so Blender shades identical geometry. This compares shading.
2. `bed_fur_native`: Blender hair curves (Interpolated children, matching length, density and clump) with the
   same Principled material. This compares the look against native Blender fur.
3. Gate: FLIP on `bed` / `bed_close` must beat today's 0.139 / 0.154 (live) and 0.142 / 0.168 (settle), and the
   plush must read as fur on the board.

## Block 2: guides (styled long hair)

Geometry Op [Fur] gets a second input, `guide`. It takes any curve source (Curve, Curve Ops, Armature).

```
 Curve ──► guide ┐
 Mesh  ──► geo ──┴► Fur ──► strands follow the guide's shape, rooted on the mesh
```

- Every strand takes the guide's shape relative to its first point, turned so the guide's first segment lines up
  with the strand's root normal, then scaled to the strand's length. One curve styles a whole head; Clump, Curl,
  Gravity and Length Jitter break it up.
- Comb (new vector param, default 0): bends the root direction toward a world direction before the guide applies,
  for a parting or a swept fringe without a curve.
- Segments goes up to 32 so long strands bend smoothly.
- Multiple guides (nearest-guide interpolation, like Blender's children) wait until one guide proves short.

## Block 3: Hair BSDF

Material gets a `Hair` model next to the existing one. It follows Blender's Principled Hair (Chiang 2016), written
from the paper (clean room):

| param | default | meaning |
|---|---|---|
| Colour mode | Melanin | Melanin (physical) or Direct colour |
| Melanin | 0.8 | 0 white .. 1 black |
| Redness | 1.0 | pheomelanin share |
| Tint | white | dye on top |
| Roughness | 0.3 | longitudinal |
| Radial Roughness | 0.3 | azimuthal |
| Coat | 0 | fur-like extra R spread |
| IOR | 1.55 | |
| Shift | 2° | cuticle tilt, separates R and TRT highlights |

- Fur gets `Normals: Ribbon | Tangent` (default Ribbon). Tangent writes the strand direction into the vertex normal
  slot, so no vertex format change; the Hair model reads it as the fibre axis. A Hair material on Ribbon normals
  still renders, just with the highlight in the wrong place, and Fur's warning line says so.
- Settle: full Chiang lobes R, TT, TRT, TRT+ with h sampled uniformly across the fibre per hit (that is what the
  paper integrates over).
- Live and raster: the same lobes with the azimuthal term averaged over h, evaluated per light, plus an
  ambient term from the averaged albedo.
- Melanin to absorption uses the published eumelanin/pheomelanin coefficients.

## Out of scope

Grooming brushes and hair dynamics.

## Status (2026-11-10)

| block | state | result |
|---|---|---|
| 1 Fur | built | `bed_fur_close` FLIP settle 0.288 → 0.221, live 0.249 → 0.234; floor (Blender vs Blender, other seed) 0.114 |
| 2 Guides | built, one guide | `hair_long` live FLIP 0.091 (shape and orientation agree) |
| 3 Hair BSDF | not started | |

What was fixed on the way:
- Blender truth: Cycles does not draw curves output by a mesh object's GN modifier. Fur lives on its own hair
  Curves object (`blender_scene.py` `add_fur`).
- Ribbon normal = the ribbon's own face normal (two-sided), width × π/2 (a random-twist ribbon covers 2/π of its
  width on average). A normal rounded across the width measured worse in live and settle.
- Live export ignored Supersample: one sample per pixel on pixel-thin strands gave speckle. Export now averages
  jittered CookEngine passes; GI warm-up runs on the first pass only. Per-case `supersample` in `cases.py`.

Measuring: blur both images (r=4) before FLIP to separate appearance from strand grain (floor 0.057 blurred).

Open gaps:
- Raster fur (FLIP 0.40): shadow map is coarser than 5 mm fur (blotches), and back-face culling drops half the
  ribbons.
- Scene tint shared with no-fur `bed_close` (Infinite +6% R, +11% G, +15% B): not fur, sits in the scene path.
- `hair_long` is measured live only; the arc folds hair back onto the ball. Longer heavy strands (length 0.3,
  gravity 0.6, 16 passes) timed out the live render at 600 s: export cost of many passes on dense strands.
