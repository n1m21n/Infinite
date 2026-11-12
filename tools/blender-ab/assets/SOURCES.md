# Hero case assets

Fetched once (2026-11-03, owner-approved) by `fetch.py`, which also packs each model's gltf, bin and
jpg textures into one glb (1k textures). Total about 2.5 MB, so they live in git.

| File | What | Source | Licence |
|---|---|---|---|
| `Ottoman_01.glb` | leather ottoman, albedo + normal + metal-rough maps | https://polyhaven.com/a/Ottoman_01 | CC0 |
| `ClassicNightstand_01.glb` | wooden nightstand, same maps | https://polyhaven.com/a/ClassicNightstand_01 | CC0 |
| `kloofendal_43d_clear_puresky_1k.hdr` | clear-sky HDRI with a sun, 1k | https://polyhaven.com/a/kloofendal_43d_clear_puresky | CC0 |
| `Fox.glb` | skinned Fox, clips Survey / Walk / Run | Khronos glTF-Sample-Assets, `Fox` (mesh by PixelMannen, CC0; rig and animation by @tomkranis, CC-BY 4.0) | CC0 + CC-BY 4.0 |

The Fox is read from `INFINITE_3DBENCH_FOX` when that variable names an existing file (the same
variable the 3D bench uses), otherwise from `assets/Fox.glb`.

## Bed close-up (case `bed`)

`fetch_bed.py` downloads two Poly Haven fabric scans (2k jpg: albedo, normal GL, roughness, height) into
`assets/bed/` (git-ignored, about 22 MB); `bed_build.py` then builds the meshes there headless
(`Blender -b --factory-startup -P bed_build.py`, about 40 s): a cloth-simulated sheet, a plush toy, a
mattress and a lamp, one glb each.

| Files | What | Source | Licence |
|---|---|---|---|
| `bed/sheet_*.jpg` | cotton jersey knit, tinted cool grey on the sheet | https://polyhaven.com/a/cotton_jersey | CC0 |
| `bed/plush_*.jpg` | curly teddy fleece, on the plush toy | https://polyhaven.com/a/curly_teddy_natural | CC0 |

## Shoe (case `ref_shoe`, plan row 10.1)

Not in git. The harness reads it from `SHOE_GLB`, by default `~/Infinite/shoe-dl/MaterialsVariantsShoe.glb`.

| File | What | Source | Licence |
|---|---|---|---|
| `MaterialsVariantsShoe.glb` | scanned sneaker with base colour, normal and occlusion-rough-metal maps, three colour variants | Khronos glTF-Sample-Assets, `MaterialsVariantsShoe` (https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MaterialsVariantsShoe) | CC-BY 4.0 |

Notice: "Materials Variants Shoe", from the Khronos glTF-Sample-Assets, licensed under CC-BY 4.0
(https://creativecommons.org/licenses/by/4.0/); the Khronos model page credits Shopify (2020) as the author
(only `LICENSE.md` is on disk, so check the page's README before publishing). The licence excludes logos and
associated trademarks. Any published picture of this case carries this credit line.
