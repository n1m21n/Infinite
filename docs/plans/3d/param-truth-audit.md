# Param-truth audit: Material, Light, World (HDRI)

Question per control: does it do what its label and range claim, in the raster renderer and in Render 3D's path tracer (PT, `engine 1`)?

Method: params and ranges from `Infinite --describe`; consumers traced in `src/nodes/Geometry3DNodes.cpp` (raster shader, `RigLight`), `Render3DPathTrace.{h,cpp}` (`PtMaterial`, `PtLight`) and `Render3DPathTraceCook.cpp`. Suspicious ones proven with headless frames from the existing release build: sphere + one Light, 160x160, PT 48 samples / 4 bounces, `--frame patch.inf 0`. "d" = mean abs pixel difference of the whole frame against the first value of the sweep (0.000 = identical image). "ctr" = mean RGB of the centre 40x40 px. Scripts, patches and renders: `$TMPDIR/param-truth/` (`sweep.py`, `maps2.py`, `s1..s6.json`, `*.out.json`, `r/N/`).

"World" = the HDRI node (`EnvironmentNode`, registered as `HDRI`). Render 3D's own sky/ambient controls are not covered.

Verdicts: OK / IGNORED-RASTER / IGNORED-PT / RANGE / LABEL / DISAGREE. Test caveat for all PT numbers: 48 spp noise is about +-0.003 in d, so d under 0.003 is "no visible change".

## Material (26 controls + 5 colours)

| control | label / range | raster | PT | verdict | evidence | suggested fix |
|---|---|---|---|---|---|---|
| color | colour | uColor | baseColor (sRGB to linear) | OK | red vs dark: ctr R 0.589 vs 0.141 raster, 0.647 vs 0.145 PT | none |
| metallic | 0..1 | used | used | OK | d 0.006 raster, 0.016 PT (0 to 1) | none |
| roughness | 0.02..1 | used | used, clamped to 0.045 | OK (minor) | metal ctr 0.444/0.502/0.552 raster, 0.441/0.494/0.405 PT for 0.05/0.3/1; PT is darker at 1.0 than raster | PT floor 0.045 vs UI floor 0.02: raise slider min to 0.045 or lower PT clamp |
| opacity | 0..1 | blends | NOT READ (`PtMaterial` has no opacity) | IGNORED-PT | green world: raster ctr G 0.632 / 0.773 / 0.874 for 1 / 0.5 / 0.1; PT 0.747 for all three, d 0.000 | add an alpha pass-through in PT (stochastic skip with p = opacity), or show "raster only" tag |
| shading (Lit / Normals / UV / Flat) | enum | all four modes | NOT READ, always Lit | IGNORED-PT | raster d 0.018 / 0.035 / 0.025 for modes 1..3; PT d 0.000 for all | PT: disable the non-Lit modes (or override output colour per mode); at least grey the control when engine = PT |
| wrap (clamp / repeat / mirror) | enum | used | used (`m.wrap`) | OK (not proven) | needs UVs outside 0..1 via Mapping; plain sphere shows no difference (d 0.000 raster, 0.000 PT). Code reads it in both | prove later with a Mapping scale 3 |
| emissionColor | colour | used | used | OK | red vs blue at emission 2: ctr R 0.919 vs 0.613 raster, 0.915 vs 0.648 PT | none |
| emission | 0..8 | used | used | OK / RANGE (mild) | ctr R 0.835 / 0.972 / 1.0 for 1 / 4 / 8 raster, 0.838 / 0.968 / 1.0 PT: clips white by about 5 | none needed; optional: soft-clip tonemap note |
| ior | 1..3 | F0 (dielectric) and refraction bend | F0 + refraction | OK (weak without glass) | opaque sphere: d 0.002 (1 to 3) both. Glass over backdrop: d 0.018 raster, 0.013 PT; raster is monotone, PT jumps 1.0 to 1.5 then flat (0.0129, 0.013, 0.0128) | PT flat above 1.5 on a sphere is plausible physics; raster bend (`(ior-1)*4*trans`) is a fake, fine |
| transmission | 0..1 | screen-space refraction | real glass | DISAGREE | green world: raster G 0.632 / 0.737 / 0.821; PT 0.747 / 0.691 / 0.675 for 0 / 0.5 / 1. Backdrop: raster sees the cubes unflipped, PT shows them refracted and flipped (see `back.png`) | accept (different physics), but document: raster is a see-through approximation |
| trans rough (transmissionRoughness) | 0..1 | blur via mip lod | used | RANGE (raster) | backdrop: raster d 0.0016 at 0.5, 0.0126 at 1.0 (all effect in the last quarter); PT d 0.008 / 0.018 | raster: map roughness to lod nonlinearly (e.g. lod = r^0.5 * maxLod) so mid values show |
| specular | 0..1 | dielectric F0 scale | used | OK | dark sphere, ctr 0.020 / 0.100 / 0.154 raster, 0.026 / 0.103 / 0.155 PT | none |
| clearcoat | 0..1 | used | used | OK | blue rough sphere ctr R 0.197 / 0.211 / 0.224 raster, 0.209 / 0.215 / 0.225 PT | none |
| coat roughness | 0.02..1 | used | used | OK | d 0.0032 raster, 0.003 PT (0.02 to 1) | none |
| subsurface | 0..1 | wrap-lighting tint | NOT READ | IGNORED-PT | red tint: raster ctr R 0.559 / 0.734 / 0.816; PT 0.612 for all values, d 0.000 | PT: approximate as diffuse tint (mix baseColor toward subsurfaceColor by amount) or hide in PT |
| subsurfaceColor | colour | used | NOT READ | IGNORED-PT | same sweep | as above |
| sss radius (subsurfaceRadius) | 0..1 | wrap width | NOT READ | IGNORED-PT; RANGE (raster) | raster d 0.0025 (0 to 0.5), 0.0006 (0.5 to 1): saturates by 0.5; PT d 0.000 | map radius to a wider wrap range or cap slider at 0.5 |
| sheen | 0..1 | Charlie sheen | used | OK | dark sphere, sheenColor red: ctr R 0.142 / 0.228 / 0.286 raster, 0.122 / 0.265 / 0.349 PT | none |
| sheenColor | colour | used | used | OK | same | none |
| sheen roughness | 0.01..1 | used | used | OK | d 0.0124 raster, 0.0141 PT (0.01 to 1) | none |
| iridescence | 0..1 | thin-film tint | NOT READ | IGNORED-PT | metal 0.5: raster ctr G 0.494 / 0.515 / 0.532 (small d 0.0035); PT 0.527 for all, d 0.000 | PT: either implement or grey out; raster effect is weak (d 0.0035 at max) |
| irid IOR | 1..3 | used | NOT READ | IGNORED-PT | raster ctr 0.436 / 0.485 / 0.493 R at 1 / 1.33 / 3; PT d 0.000 | as above |
| irid thickness | 100..1000 nm | used | NOT READ | IGNORED-PT | raster ctr G 0.499 / 0.532 / 0.466 for 100 / 400 / 1000; PT d 0.000 | as above |
| anisotropy | -1..1 | GGX aniso | NOT READ | IGNORED-PT | metal rough 0.3: raster ctr R 0.457 / 0.489 / 0.482 / 0.470 / 0.454 for -1 / -0.5 / 0 / 0.5 / 1 (d at most 0.0036); PT 0.503 for all five, d 0.000 | PT: implement anisotropic GGX, or hide in PT. Raster effect on a sphere is small; ok |
| aniso rotation | 0..1 | rotation x 2 pi | NOT READ | IGNORED-PT; RANGE (raster) | raster 0 = 0.5 = 1.0 exactly (ctr 0.4565 / 0.4565 / 0.4565), only 0.25 differs (0.4545): anisotropy repeats every 180 degrees, so 0.5..1 duplicates 0..0.5 | map slider 0..1 to 0..pi instead of 0..2 pi (`anisotropyRotation * 3.1415927`) |
| dispersion | 0..1 | RGB offset `bendDir * disp * 0.04` | NOT READ | RANGE (raster) + IGNORED-PT | glass over red/blue cubes, ior 1.8: raster ctr 0.3634 / 0.3633 / 0.3632 at 0 / 0.5 / 1 (d 0.0000): offset is sub-pixel (`bendDir` is about 0.02 uv, times 0.04); PT d 0.000 | raster: multiply offset by about 30 so max is a few pixels; PT: spectral/3-channel ior split or hide |
| alpha cutoff | 0..1 | discards alpha < cutoff | NOT READ | IGNORED-PT | opacity 0.6, backdrop: raster d 0.0 at cutoff 0 / 0.5, d 0.032 at 0.7 and 1 (switches at 0.6 as expected); PT d 0.000 for all | PT: skip hit if alpha < cutoff (needs the same alpha as opacity) |
| texture alpha (textureAlpha) | bool | alpha from albedo | NOT READ | IGNORED-PT | with a Texture on the opacity slot: raster d 0.000 (no alpha channel in that Texture), PT d 0.000. Code: PT has no alpha path at all | tie to the opacity fix |
| bump (normalBump) | bool | height to normal | used | DISAGREE (weak) | normal slot fed by a checker: raster ctr 0.606 off / 0.539 on (d 0.0114); PT 0.587 / 0.591 (d 0.0057) | check the bump scale: raster bump lowers the mean 0.07, PT does not |
| normal strength | 0..4 | `sampled.xy *= strength` | used | DISAGREE (raster bug) | raster with the map at strength 0 = ctr R 0.572, but the same patch with NO normal map = 0.539 (d 0.0063 gap): strength 0 should equal no map. PT strength 0 = 0.5924 = no map exactly. At 4 raster 0.589, PT 0.504 | raster: after scaling xy, rebuild z (`z = sqrt(1 - xy.xy)`) and renormalize, or `mix(N, mapN, strength)` |

Input slots (not params, but controls in practice):

| slot | raster | PT | verdict | evidence |
|---|---|---|---|---|
| albedo | used | used | OK | checker: ctr R 0.352 raster, 0.390 PT vs 0.539 / 0.592 without |
| roughness | used | used | OK | 0.479 vs 0.540 raster, 0.467 vs 0.524 PT |
| metallic | used | used | OK | d small both (0.542 vs 0.539, 0.599 vs 0.592) |
| normal | used | used | OK (see strength) | |
| ao | used | NOT READ (no ao slot in `PtMaterial`) | IGNORED-PT | raster 0.520 vs 0.539; PT 0.5924 with and without, d 0.000 |
| emission | used | used | OK | 0.754 vs 0.922 raster, 0.783 vs 0.924 PT |
| clearcoat / sheen | used | used | OK | |
| opacity (map) | used | NOT READ | IGNORED-PT | raster 0.300 vs 0.539; PT 0.5924 with and without |
| transmission (map) | used | used | OK (not exercised: transmission 0 multiplies the map to 0) | |

## Light (22 controls, 5 hidden)

Dark world, shadows on, one Light node wired to `light_1`. Types: 0 Directional, 1 Point, 2 Sun, 3 Ambient, 4 Spot, 5 Area.

| control | label / range | raster | PT | verdict | evidence | suggested fix |
|---|---|---|---|---|---|---|
| type | 6 types | all | Ambient (3) dropped by design; Sun = Directional | DISAGREE (ambient) | ctr R: dir 0.490, point 0.240, sun 0.507, ambient 0.743, spot 0.240, area 0.250 raster; PT 0.493, 0.284, 0.493, ambient 0.023, 0.283, 0.280. Ambient: raster lights the sphere to 0.743, PT leaves it black (0.023) because `Render3DPathTraceCook.cpp` skips type 3 ("the traced world lights the scene instead") | PT: turn an Ambient light into uniform sky radiance (or mark "raster only" in the type menu) |
| orbit (azimuth) | -180..180 | used | used | OK | ctr R 0.070 / 0.470 / 0.300 at -120 / 0 / 120 raster, 0.084 / 0.472 / 0.306 PT | label "orbit" vs key azimuth: rename "azimuth" if there is also "orbit / beat" |
| elevation | -85.9..85.9 | used | used | OK | 0.183 / 0.497 / 0.470 raster, 0.193 / 0.499 / 0.472 PT | none |
| distance | 0.2..20 | position for point/spot/area | same | LABEL (directional / sun / ambient) | directional: d 0.000 for 0.2 / 4 / 20 in both engines. Point: ctr 0.001 / 0.331 / 0.240 / 0.026 raster, 0.016 / 0.665 / 0.281 / 0.035 PT | hide for types 0, 2, 3 |
| color | colour | used | used | OK | red: ctr R 0.532 vs 0.490, G 0.075 vs 0.495 raster; PT 0.533 / 0.082 | none |
| intensity | 0..5 | used | used | OK | dir ctr 0.001 / 0.490 / 0.652 / 0.735 raster, 0.016 / 0.492 / 0.653 / 0.736 PT (0 / 1.2 / 3 / 5) | PT floor 0.016 at 0 is the dark world, not the light |
| falloff | 0..4 | `1/(1+d^2 k)`, k = 0.25 x falloff | k mis-mapped | DISAGREE | point at d 6: ctr 0.479 / 0.157 / 0.100 / 0.060 raster for 0 / 1 / 2 / 4; PT 0.069 / 0.175 / 0.112 / 0.069. PT at falloff 0 applies full inverse-square (dark) and 0 = 4 exactly; `Render3DPathTraceCook.cpp:239` scales by `1/k` then the tracer adds `1/d^2` | PT: soft falloff `1/(1+d^2 k)` must be evaluated per hit (add k to `PtLight`), not folded into colour |
| cone angle | 5..85 | used | used | OK / RANGE (scene dependent) | on a plane: raster d 0.048 / 0.080 / 0.080 / 0.080 for 12 / 30 / 60 / 85 relative to 5; PT 0.050 / 0.083 / 0.084 / 0.084. Beyond about 30 deg the lit area is bigger than the frame | none |
| penumbra | 0..1 | used | used | OK | cone 12: d 0.010 / 0.022 raster, 0.011 / 0.024 PT | none |
| orbit / beat | -1..1 | `orbitPerBeat x Beats` (`SceneNodes.cpp:46`) | marks the view "animated" | OK (by code; not run, needs a running transport) | | none |
| width, height (area) | 0.05..10 | quad area light | quad area light | DISAGREE / RANGE | normalized, plane scene: raster ctr 0.277 / 0.281 / 0.293 / 0.282 for width 0.05 / 0.5 / 2 / 10 (d 0.025 at most); PT 0.362 / 0.361 / 0.350 / 0.213 (d 0.086). Raster barely reacts to size (soft shadows are not widened) | raster: widen the shadow filter by area size so the control matters |
| normalize | bool (hidden) | used | used | OK | 4x4 area: off ctr 0.739 vs on 0.285 raster, 0.697 vs 0.241 PT | none |
| diffuse | 0..2 | used | NOT READ | IGNORED-PT | raster ctr R 0.118 / 0.490 / 0.606 for 0 / 1 / 2; PT 0.492 for all, d 0.000 | PT: multiply colour for diffuse bounce (needs separate diffuse/spec light terms) or hide in PT |
| specular | 0..2 | used | NOT READ | IGNORED-PT | raster ctr R 0.017 / 0.030 / 0.036; PT 0.030 for all, d 0.000 | as above |
| temperature (checkbox) | bool | used | used | OK | on: kelvin 800 / 6500 / 20000 gives ctr R/B 0.535 / 0.060 (orange), 0.490 / 0.484, 0.343 / 0.489 (blue) in raster; PT matches within 0.01 | none |
| kelvin | 800..20000 | used when temperature on | same | LABEL (ignored when the checkbox is off) | off: d 0.000 for all three kelvins in both engines | grey the slider while off |
| exposure | -10..10 stops | used | used | OK / RANGE | ctr R 0.005 / 0.490 / 0.827 / 0.998 for -10 / 0 / 3 / 10 raster; PT 0.018 / 0.492 / 0.828 / 1.0. White-clips by +3 or so; -10 is black | narrow to about -6..+6 |
| cutoff (checkbox) + cutoff distance | 0.01..100 | used | used | OK | point at d 4: ctr 0.001 at cutoff 0.01 and 3, 0.240 at 100, same in PT | none |
| castShadow | bool (hidden) | used | NOT READ | IGNORED-PT | sphere on a plane: raster d 0.0038 (shadow region is small), PT d 0.000; PT shadows are always on | PT: honour as "light does not occlude" (skip shadow ray) |
| shadowRadius | float (hidden) | softens | NOT READ | IGNORED-PT; RANGE | plane: raster d 0.0001 (0.25), 0.0013 (3.0); PT d 0.000 | raster effect is nearly invisible even at 3.0: scale it up |
| shadowFilter | float (hidden) | softens | NOT READ | IGNORED-PT; RANGE | raster d 0.0001 (1), 0.0004 (4); PT 0.000 | as above |
| softFalloff | bool (hidden) | chooses falloff form | NOT READ | DISAGREE | raster off vs on d 0.004; PT 0.000 (see falloff) | fixed by the falloff fix |
| shadowRes, shadowClip, showCone, transmission | hidden | shadow map size / near / debug cone / glass term | not read | not tested | | |

## World (HDRI node; 3 UI controls, 4 hidden)

Sample: `tools/blender-ab/assets/kloofendal_43d_clear_puresky_1k.hdr`, `envAsBackground` on.

| control | label / range | raster | PT | verdict | evidence | suggested fix |
|---|---|---|---|---|---|---|
| (file) | Choose HDRI | used | used | OK | | |
| intensity | 0..8 | `envIntensity x hdri.intensity` | same | OK / RANGE (mild) | ctr R 0.492 / 0.595 / 0.732 / 0.813 raster, 0.491 / 0.704 / 0.861 / 0.921 PT for 0 / 1 / 4 / 8 (tonemap compresses above 4) | none |
| rotation | -180..180 | used | used | OK | -180 and 180 identical (ctr 0.3562 raster, 0.3524 PT) as they should be; -90 / 0 / 90 gives 0.448 / 0.564 / 0.458 raster, 0.445 / 0.562 / 0.456 PT | none |
| sun threshold | 0..100 | cuts HDR peaks into a sun light | same | RANGE | 0 / 1 / 5 / 50 / 100: ctr R 0.446 / 0.803 / 0.793 / 0.792 / 0.792 raster, 0.646 / 0.841 / 0.840 / 0.839 / 0.839 PT: everything above 1 is identical for this sky (its sun is above 100), so 1..100 is dead travel | log-scale slider, or cap at about 20; show the extracted sun's peak value |
| sunAngle | float (hidden) | softens sun shadow | NOT READ | IGNORED-PT; RANGE | raster d 0.0006 (5 deg), 0.0012 (20 deg); PT 0.000 | |
| sunShadow | bool (hidden) | on/off | NOT READ | IGNORED-PT (not proven in raster) | d 0.000 both in the test scene (sun shadow falls where the plane view does not show it) | retest with a lower sun |
| sunShadowRes / Filter | hidden | used | NOT READ | IGNORED-PT | filter d 0.0001 / 0.0002 raster, 0.000 PT | |

## Ranked fixes worth doing

Ranked by how often an owner will hit it x how wrong the picture is.

| rank | fix | why | cost |
|---|---|---|---|
| 1 | PT: honour `opacity`, `alphaCutoff` and the opacity map | a glassy or cut-out material vanishes in PT: most visible disagreement in a final render | small to medium: alpha test at hit |
| 2 | PT falloff: evaluate `1/(1+d^2 k)` per hit | point, spot and area lights render 0.07 vs 0.48 at falloff 0; PT 0 equals 4 | small |
| 3 | PT Ambient light type (3): feed as uniform radiance, or mark raster-only | an Ambient light lights the sphere 0.743 raster vs 0.023 PT | small |
| 4 | Raster `normalStrength` 0 is not "no map" | shading changes when strength is 0 (0.572 vs 0.539) | small: renormalize |
| 5 | PT `diffuse` / `specular` light terms | two sliders do nothing in PT (d 0.000) | medium |
| 6 | Raster `dispersion` offset is sub-pixel | slider does nothing at all in raster (d 0.0000) | trivial: scale factor |
| 7 | `anisotropyRotation`: 0..1 maps to 2 pi but the effect repeats at pi | half the slider is a duplicate (0.5 = 1 = 0) | trivial |
| 8 | PT subsurface / iridescence / anisotropy: implement or grey out when engine = PT | 8 controls with no effect in PT; raster effects are small | large to implement, trivial to grey out |
| 9 | PT `shading` (Normals / UV / Flat): hide or grey out in PT | 3 modes ignored | trivial |
| 10 | Light: hide `distance` for directional / sun / ambient; grey `kelvin` while temperature is off; narrow `exposure` | dead controls | trivial |
| 11 | Raster `transmissionRoughness` and `subsurfaceRadius` range curve | all the effect is in the last quarter / first half | trivial |
| 12 | HDRI `sunThreshold` range | 1..100 is dead travel | trivial |
| 13 | Raster area-light size barely softens | width/height 0.05..10 gives d 0.025 raster vs 0.086 PT | medium |
| 14 | PT `ao` slot and shadow controls (`castShadow`, `shadowRadius`, `shadowFilter`, `sunShadow*`) | no effect in PT | small each |

Not proven (need a different test): `wrapMode` (needs UVs past 0..1), `textureAlpha` (needs an RGBA source), `orbitPerBeat` (needs a running transport), `sunShadow` in raster (needs a lower sun), hidden Light `shadowRes` / `shadowClip` / `showCone`.
