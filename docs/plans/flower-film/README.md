# Flower film: hyper-realism spec

One flower, made and rendered in Infinite only, moving to sound.
Status: **signed off** 2026-11-20. Model built: `art/lily/gen_lily.py` writes `art/lily/lily.inf`.

Refs (owner's, kept outside git): `~/Infinite/media/films/flower-film/refs/1-5`.
Work and outputs: `~/Infinite/media/films/flower-film/`.

## 1. The flower

| Pick | Why |
|---|---|
| **Hero: blue-and-white spotted lily** (ref 1, an Oriental "stargazer" shape) | One big flower fills the frame. Its large thin tepals show every realism cue: light through the petal, parallel veins, speckles, white rim, ruffled edge. Six tepals and six stamens give six clear parts for sound to move. |
| Later (film only): blue daisy spray (ref 2) | The Fibonacci disc and many ray florets suit octave and chord motion. Not part of this session. |

Real lilies are not blue; ref 1 is dyed or graded. We keep the owner's colour: deep violet-blue throat fading to a white rim.

### What a real lily looks like (study notes)

| Part | Real detail |
|---|---|
| Tepals | 6: three outer (narrow, pointed), three inner (wider, ruffled edge). 9-10 cm long, 0.3-0.5 mm thick. Tips recurve back past flat. A keel ridge outside, a groove (nectar furrow) inside along the midrib. |
| Colour | Blue strongest along the midrib and centre. White band 4-8 mm wide at the rim. Throat goes pale green-white (a green star). |
| Speckles | Dark dots, 0.3-1.5 mm, densest near the base, sparse toward the tip. Near the throat many are raised bumps (papillae), like small fleshy hairs. |
| Veins | Fine parallel veins, lengthwise. Hidden in front light, clear in backlight. |
| Surface | Waxy cuticle: a soft sheen, not gloss. Light velvet near the throat. |
| Stamens | 6 filaments, 7-9 cm, curving out and up. Anthers 1-1.5 cm, pivot on the filament tip (T-shape). Dust-dry pollen, rust in nature (dark blue in ref 1). |
| Pistil | One longer style, glossy, ending in a three-lobed wet stigma. Green ovary at the base. |
| Backlight | Tepals glow; the white rim goes bright, blue goes deep cyan-violet; veins and dots show as silhouettes. |
| Ageing | Pollen falls on the tepals as smudges. Rim browns and goes see-through first. Tepals soften, curl further back, crease. Anthers shrivel. |

## 2. Set

- **Black velvet void**, as ref 1: background under 1 % grey, so backlit tepals glow against it.
- No floor in frame. One dark sweep far behind, well out of focus, to stop pure digital black.
- Flower on a 25 cm stem, tilted 20° toward camera. One leaf low in frame.

## 3. Light

| Light | What | Settings (first guess) |
|---|---|---|
| Key / backlight | Low sun behind and above left, through the tepals | Directional, 25° elevation, warm-neutral 5200 K |
| Fill | Large soft box, front right | Area 1 x 1 m, low power, cool 6500 K |
| Rim on stamens | Small hard kicker, behind right | Spot or small area, picks out anthers and pollen |
| World | Near black | so only the lights shape the flower |

## 4. Camera and lens

- 100 mm macro on full frame (Camera sensorWidth 36, fov about 13.7°).
- About 0.65 m away, 3/4 front, 20° above the flower's face.
- f/5.6: stamens and throat sharp, tepal tips soft. Six blades.
- Settle path tracer, AgX view, 1920 x 1440 for stills.

## 5. Modelling: from scratch, the Blender way

The owner's rule: model as in Blender, making full use of Infinite's nodes, the 3D View and its shaders (Material + 2D maps). Sketch 3D only for a part where it is clearly the more realistic route (none planned yet); no Field Primitive meshes.

| Part | Route in Infinite (Blender equivalent) |
|---|---|
| Tepal cage | Plane, low grid -> **Edit Mesh** ops: select points, move / scale / rotate into the petal outline, midrib keel and groove (edit mode on a cage) |
| Smooth tepal | **Subdivide** smooth, 2-3 levels (subdivision surface) |
| Recurve and cup | **Armature** along the tepal: curl, twist, wave (bone chain bend) |
| Ruffled rim, creases, papillae | **Sculpt**: Draw, Crease, Pinch, Inflate brushes, saved as stroke data |
| Thickness | **Solidify** 0.4 mm (so the light passes a real slab) |
| Six tepals | **Transform** each copy, 60° steps, outer three lower and narrower |
| Filaments, style, stem | **Curve** + **Sweep** with taper |
| Anthers, ovary, stigma | **Revolve** profiles, Edit Mesh for the stigma lobes |
| Leaf | Plane cage -> Edit Mesh -> Subdivide -> Armature, as a tepal |
| Dew drops | **Sphere**, flattened, scattered by **Distribute Points on Faces** + **Instance on Points** |
| Pollen, dust | tiny Spheres, same scatter, on anthers and fallen on tepals |

Every process gets the Blender A/B (owner rule): the tepal cage + subdivide + armature against Blender's subdivision and bend through `tools/blender-ab/modeling.py`, and the hero scene against Cycles through the harness.

## 6. Materials

| Part | Material |
|---|---|
| Tepal | base colour from a painted texture (blue to white rim, dots, veins); translucency 0.5-0.7; roughness 0.45; specular 0.4; light sheen 0.2 |
| Tepal colour map | made in Infinite's 2D nodes (Ramp, Noise, Shape, Blend) by UV, baked to an image, fed to Material albedo |
| Veins / dots in backlight | darker lines in the same map: translucency passes the base colour, so they show as silhouettes |
| Filament | pale blue-white, translucency 0.4 |
| Anther / pollen | dark blue, rough 0.9, sheen 0.6 (dust) |
| Stigma | coat 1, roughness 0.05 (wet) |
| Dew | transmission 1, IOR 1.33, roughness 0 (caustic glints on in settle) |
| Stem, leaf | green, subsurface 0.3, waxy coat 0.2 |

## 7. Detail list

| Detail | Plan |
|---|---|
| Morning dew | 30-60 drops, 0.5-3 mm, beading on the tepals, a few on the leaf; each a small lens |
| Dust | a few specks on the dark tepals; a faint haze in the backlight (settle fog, density very low) |
| Pollen | packed on the anthers; 10-20 grains and two smudges fallen on the lower tepals |
| Ageing (hero: just open) | rim faintly translucent; one tepal tip slightly browned. Full ageing is a film beat (section 9). |

## 8. Sound drives motion

Sound and picture come from **one score**: one MIDI file plays the synths and drives the motion. All of it with standard nodes and `mod` wires, so it is frame-exact headless.

| Sound | Drives | How |
|---|---|---|
| **Sine drone** (A1, 55 Hz) | whole stem sway | the same note, 8 octaves down, is 0.21 Hz: an LFO at that rate on the stem Armature `curl`. Sound and sway share one pitch. |
| **Octave bands** (Audio Analyze b1-b8) | one part per octave | low b1-b2 -> stem and leaf; b3 -> outer tepals breathe (Armature curl +-3°); b4 -> inner tepals; b5 -> filaments tremble; b6+ -> anthers |
| **MIDI notes** | single tepals | pitch class -> tepal (12 classes onto 6 tepals, pairs); note-on -> Envelope (fast attack, slow damped release) -> that tepal's Armature `curl`; velocity -> depth |
| **High accents** (top octave, velocity > 100) | pollen fall | Envelope on the pollen scatter's offset: grains drop from the anthers |
| **Long low note release** | dew drop | one drop slides and falls |
| **Whole piece** | bloom and age | Sculpt `amount` (works like a shape key) blends open -> fully open -> wilted across the piece |

## 9. Film shape (plan only; no render until asked)

About 30 s, one flower, camera language only:
1. Dark. The drone starts; backlight rises through one tepal (slow push in).
2. Notes arrive; tepals answer one by one (static macro, rack focus throat -> anther).
3. Octave climb; stamens tremble, pollen falls (slow orbit 15°).
4. Last long note; the flower ages and lets one dew drop fall (hold, then fade).

Review by still sheets at each beat.

## 10. What Infinite can and cannot do today

| Item | Today | Note |
|---|---|---|
| Box modelling a cage (select, move, scale, rotate, extrude, inset) | yes | Edit Mesh ops |
| Proportional editing | **no** | work on a coarse cage, then Subdivide; Sculpt Grab for soft moves |
| Subdivision surface | yes | Subdivide, smooth |
| Bend / curl rig | yes | Armature: curl, twist, wave, poses, IK |
| Sculpt brushes | yes | Draw, Smooth, Grab, Inflate, Pinch, Flatten, Crease; `amount` blends like a shape key |
| Thin translucent petal in settle | yes | Material translucency (settle passes a diffuse share through) |
| Painted colour map by UV | yes | 2D nodes -> Material albedo |
| Per-part translucency or roughness map | **partly** | one translucency value per material; veins come from the colour map |
| Caustic glints from dew | yes | settle caustics (row 4.4) |
| Depth of field, bokeh blades | yes | Render 3D dofAperture, dofBlades |
| Haze in the backlight | yes | settle fog with falloff (row 4.1) |
| Motion blur in settle | check | row 2.7; test on the first moving shot |
| Sound -> motion | yes | MIDI File, Audio Analyze, Envelope, Note to CV, LFO -> `mod` |
| Settle from a patch file | headless flag | settle is switched on by `INFINITE_LIVE_ENGINE=15`, not a patch key |
| Settle speed for a film | slow | stills first; frame cost measured on the hero before any film talk |

## 11. Done for this session

1. This spec signed off.
2. A hero still of the lily in settle that reads as a photograph on a contact sheet beside the refs.
3. A short animation and sound-sync plan (section 8 + 9, refined after the still).

## 12. Animation and sound-sync plan (after the first still)

How the built lily moves. Every row is a standard node plus a `mod` wire; none needs a new node.

| Part in the patch | Moves by | Driven by |
|---|---|---|
| `edit0..5 -> sub0..5` (tepals) | an **Armature** between Subdivide and Transform, chain along the tepal (+Z), `curl` +-4 deg, `wave` for a ripple | note Envelope per tepal (pitch class mod 6), plus Audio Analyze b3 (outer) / b4 (inner) as a slow breath |
| `fil0..5` (filaments) | Armature, `waveAmount` small, `waveSpeed` high | b5 band: a tremble on the high octave |
| `anth0..5` (anthers) | Transform `rotX` about the filament tip (they pivot, T shape) | b6+ band, one-pole smoothed |
| whole flower | sway: Transform `rotX`/`rotZ` about the stem base | LFO at 55 Hz / 2^8 = 0.215 Hz, the drone's own pitch 8 octaves down |
| pollen | Distribute Points on Faces on the anthers -> Instance on Points; Envelope moves `offsetY` down | top-octave notes, velocity > 100 |
| ageing | Sculpt `amount` 0 -> 1 on the tepals (shape key: curl back, rim crease) | a slow ramp over the whole piece |

Score: one MIDI File plays a Sine synth drone (A1) and a soft bell/pluck line, and feeds Envelope and Note to CV. Audio Analyze reads the mixed audio for the bands. Picture and sound are the same file, so frames line up exactly.

Roadblocks found while building (to fix in the engine, in this order):

| # | Roadblock | Fix |
|---|---|---|
| 1 | Transform on a Group 3D bakes the group into one mesh and drops the per-part materials, so the whole flower cannot sway as one | Group 3D gets a whole-group matrix (posX/Y/Z, rotX/Y/Z, pivot), applied to every child as Render 3D draws it; `mod`-able |
| 2 | Curve has presets only, no control points, so filaments and style are Cylinder cages edited ring by ring (works, but slow to write by hand) | Curve keeps a point list (`s points x y z ...`), so Curve + Sweep is the Blender route |
| 3 | `dofAperture` tops out at 4; a 100 mm lens at 0.62 m and f/5.6 needs about 10.7 | raise the control range to match real macro blur |
| 4 | Settle cost: 1280 x 960 at 64 samples takes 54 s on the M2 (GPU bound, CPU idle). A frame started while another settle holds the GPU comes back black in 1 s, with no error | film frames at 1280 x 960, 64 samples + denoise (about 1 min a frame: 30 s of film is 15 h, so beats are rendered on ask only); `--frame` should fail loudly on a black settle frame |
| 5 | Transform defaults `offsetX` to 0.6 m: a Transform written for rotation only throws the part out of frame | write `offsetX 0` (done in the generator); worth a lint hint |
| 6 | Plane `rotX/Y/Z` are radians, Transform's are degrees: `rotX -90` on a Plane tilts it 26.6 deg | one unit (degrees) on every node, or the validator names the unit |
| 7 | Distribute Points on Faces gives a point cloud that Transform drops and Instance's `cloud` slot draws nothing from | insert Points to Vertices, wire into `points` with `pointMode 0` (done); Transform/Instance should take clouds |
| 8 | Macro scale is below the slider ranges: Solidify min 1 mm, Distribute density max 500/m2, instanceScale min 5 mm | tepals built 3x and scaled 1/3 (done); widen the ranges in GeometryParams.cpp |
| 9 | Mapping between a Plane and its Material hides a FieldPixel albedo in settle (grey floor) | tile inside the kernel (`u = fract(uv * 4)`, done); fix Mapping's UV pass-through |

Shots (section 9) stay as planned. First moving test: 4 s of beat 2 (one note -> one tepal) at 640 x 480, 64 samples, checked as a still sheet, not a film.
