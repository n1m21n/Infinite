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
