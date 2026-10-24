# PointZ reference

Generated from the operator table (src/pop). Settings keys are what `set_param`, `describe` and saved patches use; vector settings are saved as key + X / Y / Z.

## Box PointZ

A box of points and faces: size, centre and divisions per side. Outputs P, N and Tex.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `size` | size | vector | 1 1 1 |
| `center` | center | vector | 0 0 0 |
| `div` | divisions | integer | 1 |

## Sphere PointZ

A sphere: UV (rows and columns, poles at +-Y) or geodesic (icosphere, even triangles).

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `type` | type | menu: 0 UV, 1 geodesic | 0 UV |
| `radius` | radius | number | 0.5 |
| `center` | center | vector | 0 0 0 |
| `rows` | rows | integer | 24 |
| `cols` | columns | integer | 48 |
| `freq` | frequency | integer | 3 |

## Torus PointZ

A torus around the Y axis: major and minor radius, rows around the ring, columns around the tube.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `major` | major radius | number | 0.5 |
| `minor` | minor radius | number | 0.15 |
| `rows` | rows | integer | 48 |
| `cols` | columns | integer | 24 |
| `center` | center | vector | 0 0 0 |

## Tube PointZ

A tube along Y: bottom and top radius (a cone when one is 0), height, rows and columns, optional caps.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `rbottom` | bottom radius | number | 0.5 |
| `rtop` | top radius | number | 0.5 |
| `height` | height | number | 1 |
| `rows` | rows | integer | 2 |
| `cols` | columns | integer | 32 |
| `caps` | end caps | on/off | 1 |
| `center` | center | vector | 0 0 0 |

## Grid PointZ

A grid of rows x columns points (its dimension is kept for grid-aware PointZ nodes), as polygons, row lines or points.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `size` | size | vector | 2 2 0 |
| `rows` | rows | integer | 32 |
| `cols` | columns | integer | 32 |
| `orient` | orientation | menu: 0 XY, 1 XZ (ground), 2 YZ | 1 XZ (ground) |
| `kind` | output | menu: 0 polygons, 1 lines, 2 points | 0 polygons |
| `center` | center | vector | 0 0 0 |

## Plane PointZ

A flat rectangle split into rows x columns, as polygons, row lines or points.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `size` | size | vector | 1 1 0 |
| `rows` | rows | integer | 1 |
| `cols` | columns | integer | 1 |
| `orient` | orientation | menu: 0 XY, 1 XZ (ground), 2 YZ | 0 XY |
| `kind` | output | menu: 0 polygons, 1 lines, 2 points | 0 polygons |
| `center` | center | vector | 0 0 0 |

## Circle PointZ

A circle or arc: a closed line, a filled polygon (triangle fan) or points.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `radius` | radius | number | 0.5 |
| `div` | divisions | integer | 64 |
| `arcStart` | arc start | number | 0 |
| `arcEnd` | arc end | number | 360 |
| `kind` | output | menu: 0 polygons, 1 lines, 2 points | 1 lines |
| `orient` | orientation | menu: 0 XY, 1 XZ (ground), 2 YZ | 0 XY |
| `center` | center | vector | 0 0 0 |

## Rectangle PointZ

A rectangle: a closed line of four corners, a filled quad or its corner points.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `size` | size | vector | 1 1 0 |
| `kind` | output | menu: 0 polygons, 1 lines, 2 points | 1 lines |
| `orient` | orientation | menu: 0 XY, 1 XZ (ground), 2 YZ | 0 XY |
| `center` | center | vector | 0 0 0 |

## Line PointZ

A straight line from point A to point B with evenly spaced points.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `a` | point A | vector | -0.5 0 0 |
| `b` | point B | vector | 0.5 0 0 |
| `points` | points | integer | 32 |

## Point Generator PointZ

Scatters a number of points in a shape (box, sphere, sphere surface, disc, line), uniformly at random or as an even golden-ratio spiral. Outputs point primitives with P and an Id.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `count` | points | integer | 1000 |
| `shape` | shape | menu: 0 box, 1 sphere, 2 sphere surface, 3 disc, 4 line | 0 box |
| `size` | size | vector | 1 1 1 |
| `center` | center | vector | 0 0 0 |
| `dist` | distribution | menu: 0 random, 1 even spiral | 0 random |
| `seed` | seed | number | 1 |

## Pattern PointZ

N points along X carrying a waveform in an attribute (sine, triangle, square, ramp, gaussian, random) - a channel of values to look up, displace or instance with. The value also lifts Y when 'plot' is on.

Tier A. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `count` | points | integer | 128 |
| `wave` | waveform | menu: 0 sine, 1 triangle, 2 square, 3 ramp, 4 gaussian, 5 random | 0 sine |
| `cycles` | cycles | number | 1 |
| `phase` | phase | number | 0 |
| `amp` | amplitude | number | 1 |
| `offset` | offset | number | 0 |
| `width` | width | number | 2 |
| `name` | attribute | text | Pattern |
| `plot` | plot in Y | on/off | 1 |
| `seed` | seed | number | 1 |

## Null PointZ

Passes its input through unchanged: a stable junction to branch a PointZ chain, and the end of a Particle or Feedback loop - give it a 'tag' and name that tag in the loop's 'target'.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `tag` | tag (loop target) | text |  |

## In PointZ

Marks where a PointZ enters a group or macro; passes it through.

Tier A. Inputs: 1 geometry.

No settings.

## Out PointZ

Marks where a PointZ leaves a group or macro; passes it through.

Tier A. Inputs: 1 geometry.

No settings.

## Geometry to PointZ

Turns any Infinite 3D source (Geometry, Model 3D, Text 3D, Particle System, Curve...) into a PointZ, with its placement baked in. Every PointZ input already does this; use it to make the step explicit.

Tier A. Inputs: 1 geometry.

No settings.

## Switch PointZ

Outputs one of up to four inputs, picked by 'index' (modulate it to cut between shapes).

Tier A. Inputs: 4 geometry.

| key | label | type | default |
|---|---|---|---|
| `index` | index | integer | 0 |

## Transform PointZ

Moves, rotates and scales points (and their normals) about a pivot, in a chosen order. 'also vectors' lists more vector attributes to rotate with them (e.g. PartVel).

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `t` | translate | vector | 0 0 0 |
| `r` | rotate | vector | 0 0 0 |
| `s` | scale | vector | 1 1 1 |
| `uniform` | uniform scale | number | 1 |
| `pivot` | pivot | vector | 0 0 0 |
| `order` | order | menu: 0 scale rotate translate, 1 scale translate rotate, 2 rotate scale translate, 3 rotate translate scale, 4 translate scale rotate, 5 translate rotate scale | 0 scale rotate translate |
| `vectors` | also vectors | text |  |
| `mask` | mask attribute | text |  |

## Twist PointZ

Deforms along an axis: twist (rotate around it), bend (curl it into an arc), taper (scale the cross-section) or shear (slide it sideways), growing from 'origin' over 'length'.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `mode` | operation | menu: 0 twist, 1 bend, 2 taper, 3 shear | 0 twist |
| `axis` | axis | menu: 0 X, 1 Y, 2 Z | 1 Y |
| `amount` | amount | number | 90 |
| `origin` | origin | number | 0 |
| `length` | length | number | 1 |
| `clamp` | clamp to length | on/off | 0 |
| `mask` | mask attribute | text |  |

## Noise PointZ

Adds (or sets, or multiplies by) smooth noise on any attribute. Types: gradient, fractal (fbm), curl (swirling, divergence-free, good for particles), ridged, turbulence, cells (Worley), value and random. With 'translate 4D' on, time and the W offset move through a fourth dimension, so the noise evolves in place instead of sliding. Sampled at 'coordinates' (P by default).

Tier A. Inputs: 1 geometry, cooks every frame while 'speed' is on.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `type` | type | menu: 0 gradient, 1 fractal, 2 curl, 3 ridged, 4 turbulence, 5 cells, 6 value, 7 random | 1 fractal |
| `mode` | combine | menu: 0 add, 1 set, 2 multiply | 0 add |
| `amp` | amplitude | number | 0.2 |
| `axisAmp` | per axis | vector | 1 1 1 |
| `bias` | offset (added) | number | 0 |
| `freq` | frequency | number | 1.5 |
| `scale` | frequency per axis | vector | 1 1 1 |
| `offset` | translate | vector | 0 0 0 |
| `use4d` | translate 4D | on/off | 1 |
| `t4d` | translate 4D (W) | number | 0 |
| `speed` | speed | number | 0 |
| `oct` | harmonics | integer | 4 |
| `spread` | harmonic spread | number | 2 |
| `rough` | roughness (gain) | number | 0.5 |
| `expo` | exponent | number | 1 |
| `coord` | coordinates | text | P |
| `seed` | seed | number | 0 |
| `mask` | mask attribute | text |  |

## Random PointZ

Writes a random value per point into an attribute, between 'min' and 'max' (per component), uniform or gaussian, optionally whole numbers. Same seed, same values.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | Color |
| `mode` | combine | menu: 0 set, 1 add, 2 multiply | 0 set |
| `lo` | min | vector | 0 0 0 |
| `hi` | max | vector | 1 1 1 |
| `lo4` | min (4th) | number | 1 |
| `hi4` | max (4th) | number | 1 |
| `dist` | distribution | menu: 0 uniform, 1 gaussian | 0 uniform |
| `integer` | whole numbers | on/off | 0 |
| `seed` | seed | number | 1 |
| `mask` | mask attribute | text |  |

## Math PointZ

Arithmetic on one attribute: (value + pre add) x multiply, through a function, + post add.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `dest` | output (empty = same) | text |  |
| `pre` | pre add | number | 0 |
| `mul` | multiply | number | 1 |
| `fn` | function | menu: 0 none, 1 abs, 2 negate, 3 sqrt, 4 square, 5 power, 6 exp, 7 log, 8 sign, 9 floor, 10 ceil, 11 fract, 12 round, 13 1 / x, 14 1 - x, 15 clamp 0..1, 16 length, 17 normalize, 18 sin, 19 cos | 0 none |
| `exp` | exponent | number | 2 |
| `post` | post add | number | 0 |
| `mask` | mask attribute | text |  |

## ReRange PointZ

Maps an attribute from one range onto another (every component), optionally clamped.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `inLo` | from low | number | 0 |
| `inHi` | from high | number | 1 |
| `outLo` | to low | number | 0 |
| `outHi` | to high | number | 1 |
| `clamp` | clamp | on/off | 0 |
| `mask` | mask attribute | text |  |

## Quantize PointZ

Snaps an attribute to a grid of 'step' (per axis for vectors), rounding, flooring or ceiling.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `step` | step | vector | 0.1 0.1 0.1 |
| `step4` | step (4th) | number | 0.1 |
| `offset` | offset | vector | 0 0 0 |
| `how` | rounding | menu: 0 round, 1 floor, 2 ceil | 0 round |
| `mask` | mask attribute | text |  |

## Trig PointZ

Trigonometric function on an attribute: amplitude x f(value x frequency + phase). 'cycles' units make 1.0 one full turn.

Tier A. Inputs: 1 geometry, cooks every frame while 'speed' is on.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `dest` | output (empty = same) | text |  |
| `fn` | function | menu: 0 sin, 1 cos, 2 tan, 3 asin, 4 acos, 5 atan, 6 sinh, 7 cosh, 8 tanh | 0 sin |
| `units` | units | menu: 0 radians, 1 cycles, 2 degrees | 1 cycles |
| `freq` | frequency | number | 1 |
| `phase` | phase | number | 0 |
| `amp` | amplitude | number | 1 |
| `speed` | speed | number | 0 |
| `mask` | mask attribute | text |  |

## Limit PointZ

Keeps an attribute inside a box: clamp to its walls, wrap around (exit one side, enter the other) or mirror back.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `mode` | mode | menu: 0 clamp, 1 wrap, 2 mirror | 0 clamp |
| `lo` | min | vector | -1 -1 -1 |
| `hi` | max | vector | 1 1 1 |
| `mask` | mask attribute | text |  |

## Normalize PointZ

Vectors: scales each to 'length' (unit by default). Single values: stretches the attribute's own range across all points to 0..1.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | N |
| `length` | length | number | 1 |
| `mask` | mask attribute | text |  |

## Projection PointZ

Pushes points onto a shape - a plane, a sphere or a cylinder - by 'amount' (0 leaves them, 1 lands them on it). Normals follow when 'set normals' is on.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `shape` | shape | menu: 0 plane, 1 sphere, 2 cylinder | 1 sphere |
| `axis` | axis | menu: 0 X, 1 Y, 2 Z | 1 Y |
| `center` | center | vector | 0 0 0 |
| `radius` | radius / offset | number | 0.5 |
| `amount` | amount | number | 1 |
| `setN` | set normals | on/off | 1 |
| `mask` | mask attribute | text |  |

## Texture Map PointZ

Writes texture coordinates (Tex) by projecting the points: planar on XY / XZ / YZ (fitted to the input's bounds), spherical, cylindrical or from the normal. 'scale' and 'offset' tile and slide them.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `proj` | projection | menu: 0 planar XY, 1 planar XZ, 2 planar YZ, 3 spherical, 4 cylindrical, 5 from normal | 0 planar XY |
| `scale` | scale | vector | 1 1 1 |
| `offset` | offset | vector | 0 0 0 |
| `dest` | attribute | text | Tex |
| `mask` | mask attribute | text |  |

## Field PointZ

A region of space that weights points: 1 inside the shape, 0 outside, with a soft 'transition' between. Writes Weight (and signed distance Dist), combined with what is already there. Use the weight as another PointZ's mask to limit its effect to the region.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `shape` | shape | menu: 0 sphere, 1 box, 2 torus, 3 tube, 4 capsule, 5 plane | 0 sphere |
| `center` | center | vector | 0 0 0 |
| `size` | size / radius | vector | 0.5 0.5 0.5 |
| `minor` | minor radius | number | 0.1 |
| `axis` | axis | menu: 0 X, 1 Y, 2 Z | 1 Y |
| `range` | transition | number | 0.2 |
| `curve` | transition curve | menu: 0 linear, 1 smooth, 2 ease in-out | 1 smooth |
| `invert` | invert | on/off | 0 |
| `combine` | combine | menu: 0 replace, 1 add, 2 multiply, 3 max, 4 min | 0 replace |
| `weight` | weight attribute | text | Weight |
| `dist` | write Dist | on/off | 0 |

## Force Radial PointZ

Adds a force to PartForce pulling points toward a centre (or pushing them away), fading out over 'radius', with an optional swirl around an axis. Put it inside a Particle PointZ loop.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `center` | center | vector | 0 0 0 |
| `strength` | strength | number | 1 |
| `radius` | radius | number | 2 |
| `falloff` | falloff | menu: 0 none, 1 linear, 2 inverse square | 1 linear |
| `swirl` | swirl | number | 0 |
| `axis` | swirl axis | vector | 0 1 0 |
| `mask` | mask attribute | text |  |

## Force Target PointZ

(0.71.1) Adds a spring force to PartForce pulling each particle towards its own point of the second input (a text, a grid with noise, a mesh): point PartId (or index, or a random one), wrapped around the target's count, so a million particles can share a smaller shape. 'damping' takes out velocity so they settle on it; 'max force' keeps far ones from shooting in. Put it inside a Particle PointZ loop, next to Force Radial (attract / repel) and Collide.

Tier A. Inputs: 2 geometry (particles, target).

| key | label | type | default |
|---|---|---|---|
| `strength` | strength | number | 4 |
| `damping` | damping | number | 1.5 |
| `match` | match by | menu: 0 PartId (else index), 1 index, 2 random (by PartId) | 0 PartId (else index) |
| `limit` | max force (0 = none) | number | 0 |
| `mask` | mask attribute | text |  |

## Collide PointZ

(0.71.1) A plane particles cannot cross (a floor by default): a point behind it is put back on it, the part of PartVel going into the plane bounces back times 'bounce', and the sliding part slows by 'friction'. Put it inside a Particle PointZ loop, after the forces.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `point` | plane point | vector | 0 0 0 |
| `normal` | plane normal | vector | 0 1 0 |
| `bounce` | bounce | number | 0.2 |
| `friction` | friction | number | 0.3 |
| `mask` | mask attribute | text |  |

## Lookup Attribute PointZ

Copies attributes from the second input onto the first, point by point: point i reads the point given by its 'index attribute' (or the same number when empty), clamped to the range. Components and renames as in TouchDesigner's POPs (0.71.1): 'P.y' copies only P's y, 'P.y=Lum' writes the source's Lum into P's y, 'Height=P.y' the source's P.y into a new Height (x y z w or r g b a); 'combine' adds, subtracts, multiplies, or keeps the min / max instead of replacing.

Tier A. Inputs: 2 geometry.

| key | label | type | default |
|---|---|---|---|
| `attribs` | attributes | text | Color |
| `index` | index attribute | text |  |
| `offset` | index offset | integer | 0 |
| `combine` | combine | menu: 0 replace, 1 add, 2 subtract, 3 multiply, 4 min, 5 max | 0 replace |

## Lookup Channel PointZ

Runs an attribute through a curve: its value (0..1 after the input range) picks a point on a five-key curve, smoothly interpolated, and the result is written out. A shaping table you can modulate key by key.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | input attribute | text | Tex |
| `comp` | component | integer | 0 |
| `inLo` | from low | number | 0 |
| `inHi` | from high | number | 1 |
| `k0` | key 0 | number | 0 |
| `k1` | key 1 | number | 0.25 |
| `k2` | key 2 | number | 0.5 |
| `k3` | key 3 | number | 0.75 |
| `k4` | key 4 | number | 1 |
| `interp` | interpolation | menu: 0 linear, 1 smooth | 1 smooth |
| `dest` | output attribute | text | Value |

## Lookup Texture PointZ

Samples an image at each point's texture coordinate (or its position fitted to the bounds) and writes the colour - replacing, multiplying or adding - or pushes the points along their normals by the image's brightness.

Tier A. Inputs: 1 geometry + 1 image.

| key | label | type | default |
|---|---|---|---|
| `uvFrom` | coordinates | menu: 0 Tex, 1 position XY (bounds), 2 position XZ (bounds) | 0 Tex |
| `mode` | result | menu: 0 colour replace, 1 colour multiply, 2 colour add, 3 displace along N, 4 write attribute | 0 colour replace |
| `amount` | amount | number | 1 |
| `dest` | attribute | text | Lum |
| `mask` | mask attribute | text |  |

## Group PointZ

Marks a set of points in a float attribute (1 in, 0 out): inside a box or sphere, an index range, a random fraction, facing a direction, or a GLSL condition. Combine with what is there to build up a selection; other PointZ nodes take it as their mask.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `name` | group attribute | text | Group |
| `by` | select by | menu: 0 box, 1 sphere, 2 index range, 3 random fraction, 4 normal direction, 5 GLSL condition | 0 box |
| `center` | center | vector | 0 0 0 |
| `size` | size | vector | 1 1 1 |
| `radius` | radius | number | 0.5 |
| `start` | first | integer | 0 |
| `end` | last | integer | 99 |
| `step` | every | integer | 1 |
| `fraction` | fraction | number | 0.5 |
| `seed` | seed | number | 1 |
| `dir` | direction | vector | 0 1 0 |
| `angle` | spread (deg) | number | 45 |
| `expr` | condition | text | P.y > 0.0 |
| `combine` | combine | menu: 0 replace, 1 union, 2 intersect, 3 subtract | 0 replace |
| `invert` | invert | on/off | 0 |

## Blend PointZ

Blends attributes of the first input toward the second (same point count): P, N and Color by default. 'weight' 0 is the first input, 1 the second; a weight attribute blends per point.

Tier A. Inputs: 2 geometry.

| key | label | type | default |
|---|---|---|---|
| `weight` | weight | number | 0.5 |
| `attribs` | attributes | text | P,N,Color |
| `wattr` | weight attribute | text |  |
| `mask` | mask attribute | text |  |

## Dimension PointZ

Gives the points a grid shape (rows x columns; 'from input' keeps a generator's own) and writes each point's normalised column and row (0..1) into an attribute, for grid-aware effects.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `auto` | from input | on/off | 1 |
| `rows` | rows | integer | 16 |
| `cols` | columns | integer | 16 |
| `dest` | attribute | text | Tex |

## Phaser PointZ

Staggers an animation across points: as 'phase' runs 0 to 1, each point's own 0..1 (from an attribute, or its index) decides when its 'Phase' ramps from 0 to 1, over 'edge'. Drive a Blend, a mask or a scale with it.

Tier A. Inputs: 1 geometry, cooks every frame while 'speed' is on.

| key | label | type | default |
|---|---|---|---|
| `phase` | phase | number | 0.5 |
| `edge` | edge | number | 0.2 |
| `speed` | speed | number | 0 |
| `from` | offset attribute (empty = index) | text |  |
| `comp` | component | integer | 0 |
| `curve` | curve | menu: 0 linear, 1 smooth | 1 smooth |
| `dest` | output attribute | text | Phase |

## Point PointZ

Sets point attributes from GLSL expressions: position, normal, colour and one custom attribute. Empty keeps the value. In scope: P, N, Color, Tex, id, uCount, uTime, uBeat, noise and hash helpers.

Tier A. Inputs: 1 geometry, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `eP` | position | text |  |
| `eN` | normal | text |  |
| `eC` | colour (vec4) | text |  |
| `custom` | custom attribute | text |  |
| `eX` | custom value | text |  |
| `comps` | custom components | integer | 1 |
| `animate` | animate (uses uTime) | on/off | 0 |
| `mask` | mask attribute | text |  |

## Attribute PointZ

Creates or overwrites an attribute: a constant, a copy of another attribute, the point index (raw or 0..1), or a GLSL expression.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `name` | attribute | text | Value |
| `comps` | components | integer | 1 |
| `from` | value | menu: 0 constant, 1 copy attribute, 2 index, 3 index 0..1, 4 expression | 0 constant |
| `value` | value | vector | 0 0 0 |
| `value4` | value (4th) | number | 1 |
| `src` | source attribute | text | P |
| `expr` | expression | text | P.y |

## Attribute Combine PointZ

Combines two attributes of the same points into a third: add, subtract, multiply, divide, min, max, dot, cross, distance or average.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `a` | A | text | P |
| `b` | B | text | N |
| `op` | operation | menu: 0 add, 1 subtract, 2 multiply, 3 divide, 4 min, 5 max, 6 dot, 7 cross, 8 distance, 9 average | 0 add |
| `dest` | result | text | Result |

## Attribute Convert PointZ

Copies, renames or deletes an attribute, or changes its component count (a single value can be spread to every component).

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `op` | operation | menu: 0 copy / resize, 1 rename, 2 delete | 0 copy / resize |
| `src` | attribute | text | P |
| `dest` | new name | text | Rest |
| `comps` | components | integer | 3 |
| `spread` | spread a single value | on/off | 1 |

## Math Combine PointZ

Combines one attribute of the first input with the same-numbered points of the second: add, subtract, multiply, divide, min, max, or replace.

Tier A. Inputs: 2 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `battrib` | B attribute (empty = same) | text |  |
| `op` | operation | menu: 0 add, 1 subtract, 2 multiply, 3 divide, 4 min, 5 max, 6 replace | 0 add |
| `scale` | B scale | number | 1 |
| `mask` | mask attribute | text |  |

## Math Mix PointZ

Mixes two attributes of the same points by a weight (a number, or a weight attribute per point) into a result attribute.

Tier A. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `a` | A | text | P |
| `b` | B | text | Rest |
| `weight` | weight | number | 0.5 |
| `wattr` | weight attribute | text |  |
| `dest` | result | text | P |

## GLSL PointZ

Runs your own GLSL once per point. Every input attribute is a variable of its name (P, N, Color, Tex ...); list the ones you change in 'outputs' (name or name:components, up to four). Also: id, uCount, uTime, uBeat, uDelta, knobs uA..uD, fetch_P(i) for any point's position, sampleImage(uv) from the image pin, and the noise / hash / rotation helpers.

Tier A. Inputs: 1 geometry + 1 image, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `outputs` | outputs | text | P |
| `code` | code | text | P += N * 0.1 * sin(uTime * 2.0 + P.y * 6.0); |
| `a` | uA | number | 0.5 |
| `b` | uB | number | 0.5 |
| `c` | uC | number | 0.5 |
| `d` | uD | number | 0.5 |
| `animate` | animate (cook every frame) | on/off | 1 |
| `mask` | mask attribute | text |  |

## GLSL Select PointZ

Marks points with a GLSL condition (true = 1) into a group attribute, like Group PointZ's GLSL mode but with the knobs uA..uD and every attribute in scope. Follow with Delete PointZ to keep only them.

Tier A. Inputs: 1 geometry, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `name` | group attribute | text | Group |
| `cond` | condition | text | P.y > uA - 0.5 |
| `a` | uA | number | 0.5 |
| `b` | uB | number | 0.5 |
| `animate` | animate | on/off | 0 |

## Image to PointZ

Turns an image into points: a grid of columns x rows over the picture (keeping its aspect), each point coloured by the pixel under it and lifted by its brightness. Polygons, row lines or points.

Tier A. Inputs: 0 geometry + 1 image.

| key | label | type | default |
|---|---|---|---|
| `cols` | columns | integer | 160 |
| `aspect` | rows from aspect | on/off | 1 |
| `rows` | rows | integer | 90 |
| `width` | width | number | 2 |
| `height` | height from brightness | number | 0 |
| `kind` | output | menu: 0 polygons, 1 lines, 2 points | 2 points |
| `alpha` | drop transparent pixels | on/off | 0 |

## Signal to PointZ

Turns a row of an image into a line of points - one per pixel across - lifted by a channel's value. Feed it an Audio Texture for a live waveform or spectrum as geometry.

Tier A. Inputs: 0 geometry + 1 image.

| key | label | type | default |
|---|---|---|---|
| `row` | row (0..1) | number | 0 |
| `chan` | channel | menu: 0 red, 1 green, 2 blue, 3 alpha, 4 brightness | 0 red |
| `points` | points | integer | 256 |
| `width` | width | number | 2 |
| `amp` | amplitude | number | 1 |
| `bias` | centre (value at rest) | number | 0.5 |
| `dest` | value attribute | text | Value |

## Merge PointZ

Joins up to four PointZ nodes into one: points, faces and lines appended; an attribute any of them has is given to all (defaults where missing).

Tier B. Inputs: 4 geometry.

No settings.

## Delete PointZ

Removes points (and the faces and line pieces that used them): those whose attribute is over 'threshold' (a Group or Weight), an index range, every Nth, a random fraction, or the points no face or line uses. 'invert' keeps them instead.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `by` | delete by | menu: 0 attribute, 1 index range, 2 every Nth, 3 random fraction, 4 unused points | 0 attribute |
| `attrib` | attribute | text | Group |
| `threshold` | threshold | number | 0.5 |
| `start` | first | integer | 0 |
| `end` | last | integer | 99 |
| `nth` | every | integer | 2 |
| `fraction` | fraction | number | 0.5 |
| `seed` | seed | number | 1 |
| `invert` | invert (keep them) | on/off | 0 |

## Convert PointZ

Changes what the points make: faces to their edges as lines, anything to bare points, lines to filled faces (fan from the first point), or points to one line through them in order.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `to` | convert to | menu: 0 points, 1 edges (lines), 2 faces from closed lines, 3 one line through the points | 0 points |
| `close` | close the line | on/off | 0 |

## Normal PointZ

Computes normals: smooth (averaged over the faces around each point), or each line's tangent direction. 'flip' turns them around.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `mode` | mode | menu: 0 smooth from faces, 1 line tangent | 0 smooth from faces |
| `flip` | flip | on/off | 0 |

## Facet PointZ

Unique points: every face gets its own corners (hard, faceted shading with flat normals). Consolidate: merges points closer than 'distance' (welds seams), averaging their attributes, with smooth normals after.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `mode` | mode | menu: 0 unique points (flat), 1 consolidate (weld) | 0 unique points (flat) |
| `dist` | distance | number | 0.0001 |
| `normals` | compute normals | on/off | 1 |

## Primitive PointZ

Works on whole primitives: reverses faces (flips winding) and line direction, and writes each point's primitive number into an attribute (PrimId) for per-face effects.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `reverse` | reverse | on/off | 0 |
| `primId` | write PrimId | on/off | 1 |
| `name` | attribute | text | PrimId |

## Select PointZ

Keeps only the listed attributes (P always stays) and renames with 'old:new' pairs - to slim a PointZ down before caching, instancing or writing a file.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `keep` | keep attributes (empty = all) | text |  |
| `rename` | rename (old:new, ...) | text |  |

## Sort PointZ

Reorders the points (faces and lines follow): by an attribute's component, by distance from a point, randomly, or reversed. Order matters to Trail, Line and index-based PointZ nodes.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `by` | sort by | menu: 0 attribute, 1 distance from point, 2 random, 3 reverse | 0 attribute |
| `attrib` | attribute | text | P |
| `comp` | component | integer | 0 |
| `from` | point | vector | 0 0 0 |
| `seed` | seed | number | 1 |
| `desc` | descending | on/off | 0 |

## Topology PointZ

Writes how each point sits in the mesh: Valence (how many points it connects to), Boundary (1 on an open edge) and PrimCount (faces using it). Optionally outputs the boundary edges alone as lines.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `edges` | output boundary lines only | on/off | 0 |

## Copy PointZ

Real copies of the geometry (unlike Instance PointZ, which draws one mesh many times): 'copies' stacked with a cumulative step transform, and with a template on the second input, a set at every template point (moved to its P, turned to its N and Rotate, scaled by PScale, coloured by its Color). Writes CopyId and TemplateId.

Tier B. Inputs: 2 geometry.

| key | label | type | default |
|---|---|---|---|
| `copies` | copies | integer | 1 |
| `t` | step translate | vector | 0 0 0 |
| `r` | step rotate | vector | 0 0 0 |
| `s` | step scale | vector | 1 1 1 |
| `align` | align to template N | menu: 0 off, 1 +Z to N, 2 +Y to N | 0 off |
| `scale` | template scale | number | 1 |
| `color` | stamp template Color | on/off | 1 |
| `maxPoints` | point limit | integer | 4e+06 |

## Extrude PointZ

Faces: pushes them out along their normals by 'distance' with an 'inset', building the side walls. Lines: sweeps each line along a direction into a ribbon of faces.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `dist` | distance | number | 0.2 |
| `inset` | inset | number | 0 |
| `dir` | line direction | vector | 0 0 1 |
| `steps` | line steps | integer | 1 |

## Subdivide PointZ

Faces: splits each face into four per level, optionally smoothing toward a rounded surface. Lines: inserts points between, corner-cutting (Chaikin) when 'smooth' is on.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `levels` | levels | integer | 1 |
| `smooth` | smooth | number | 1 |

## Sprinkle PointZ

Scatters points over the input's surface, evenly by area (or one count per face), each carrying the attributes interpolated from where it landed. Output: point primitives.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `count` | points | integer | 2000 |
| `method` | method | menu: 0 by area, 1 per face | 0 by area |
| `seed` | seed | number | 1 |

## Revolve PointZ

Spins a profile line around an axis into a surface (a lathe): 'divisions' around, 'angle' for a partial sweep. Every attribute of the profile carries round.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `axis` | axis | menu: 0 X, 1 Y, 2 Z | 1 Y |
| `origin` | axis origin | vector | 0 0 0 |
| `angle` | angle | number | 360 |
| `div` | divisions | integer | 32 |

## Curve PointZ

Turns lines (or points in order) into smooth curves through them: Catmull-Rom through every point, or B-spline near them, with 'divisions' points per segment.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `type` | type | menu: 0 Catmull-Rom (through points), 1 B-spline (smoother) | 0 Catmull-Rom (through points) |
| `div` | divisions | integer | 8 |
| `close` | close open lines | on/off | 0 |

## Line Resample PointZ

Re-spaces the points of every line evenly along its length: a fixed number per line, or one every 'length'. Every attribute is interpolated.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `by` | by | menu: 0 count, 1 segment length | 0 count |
| `count` | points | integer | 64 |
| `length` | segment length | number | 0.05 |

## Line Divide PointZ

Adds points inside every segment of every line: straight (linear) or along a smooth curve through the points (Catmull-Rom). Unlike Line Resample the original points stay.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `div` | divisions per segment | integer | 4 |
| `interp` | interpolation | menu: 0 linear, 1 smooth | 1 smooth |

## Line Break PointZ

Cuts lines into pieces: every N points, at corners sharper than 'angle', or where an attribute is over 'threshold'.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `by` | break | menu: 0 every N points, 1 at sharp corners, 2 where attribute > threshold | 0 every N points |
| `every` | every | integer | 8 |
| `angle` | angle | number | 45 |
| `attrib` | attribute | text | Group |
| `threshold` | threshold | number | 0.5 |

## Line Metrics PointZ

Measures every line and writes per point: Arc (distance along it), ArcU (0..1 along it), LineId, LineLen and the tangent T. Use ArcU to colour, grow or animate along lines.

Tier B. Inputs: 1 geometry.

No settings.

## Line Smooth PointZ

Relaxes lines: each pass pulls every point toward the middle of its neighbours by 'strength'. Ends stay put unless 'move ends' is on.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `iter` | passes | integer | 4 |
| `strength` | strength | number | 0.5 |
| `ends` | move ends | on/off | 0 |

## Line Thick PointZ

Gives lines a body: a flat ribbon (facing 'up', or each point's N) or a round tube with 'sides'. Width can follow an attribute (e.g. PScale or ArcU).

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `shape` | shape | menu: 0 ribbon, 1 tube | 1 tube |
| `width` | width | number | 0.05 |
| `sides` | sides | integer | 8 |
| `up` | ribbon up | vector | 0 1 0 |
| `useN` | ribbon faces N | on/off | 0 |
| `wattr` | width attribute | text |  |

## Accumulate PointZ

Running total of an attribute in point order - over all points, or restarting on every line - written to a new attribute. 'normalize' divides by the final total (a 0..1 ramp).

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | Value |
| `comp` | component | integer | 0 |
| `scope` | scope | menu: 0 all points, 1 per line | 0 all points |
| `norm` | normalize | on/off | 0 |
| `dest` | result | text | Accum |

## Analyze PointZ

Summarises an attribute over all points into one point: Min, Max, Avg, Sum (per component) and Count, with P at the centroid. Read it with Geometry Table, or Lookup Attribute.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |

## Histogram PointZ

Counts an attribute's values into bins and outputs one point per bin along X (height = count, normalised to the fullest bin) as a line, with Count and BinValue attributes.

Tier B. Inputs: 1 geometry.

| key | label | type | default |
|---|---|---|---|
| `attrib` | attribute | text | P |
| `comp` | component | integer | 1 |
| `bins` | bins | integer | 32 |
| `auto` | range from data | on/off | 1 |
| `lo` | low | number | 0 |
| `hi` | high | number | 1 |
| `width` | width | number | 2 |
| `height` | height | number | 1 |

## Particle PointZ

A particle system born from the input's points: 'birth rate' per second (or 'per point' every frame), each with a life, starting velocity and the source point's attributes. Writes PartId, PartVel, PartAge, PartLifeSpan, PartForce, PartMass, PartDrag. Forces: gravity here, or any PointZ nodes after it that add to PartForce (Force Radial, Noise on PartForce...) ending in a Null PointZ whose 'tag' you put in 'target' - next frame the particles carry on from there.

Tier B. Inputs: 1 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `birth` | birth | menu: 0 rate per second, 1 per point each frame | 0 rate per second |
| `rate` | birth rate | number | 200 |
| `life` | life (s) | number | 3 |
| `lifeVar` | life variance | number | 0.3 |
| `vel` | velocity | vector | 0 0.5 0 |
| `speedN` | speed along N | number | 0 |
| `velRand` | random velocity | number | 0.2 |
| `jitter` | birth jitter | number | 0 |
| `gravity` | gravity | vector | 0 -1 0 |
| `drag` | drag | number | 0 |
| `damp` | damping | number | 0 |
| `mass` | mass | number | 1 |
| `max` | max particles | integer | 100000 |
| `timeScale` | time scale | number | 1 |
| `inherit` | copy source attributes | on/off | 1 |
| `target` | target (Null PointZ tag) | text |  |
| `play` | play | on/off | 1 |
| `seed` | seed | number | 1 |

## Feedback PointZ

Starts a loop: outputs its input once (and on reset), then every frame the output of the Null PointZ tagged 'target' from the frame before - so the PointZ nodes in between act again on their own result (growth, erosion, trails of change).

Tier B. Inputs: 1 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `target` | target (Null PointZ tag) | text | loop |
| `play` | play | on/off | 1 |

## Trail PointZ

Draws where each point has been: keeps the last 'length' frames and outputs one line per point (matched by PartId, or by index), newest first, with TrailAge 0..1 along it.

Tier B. Inputs: 1 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `length` | length (frames) | integer | 30 |
| `every` | sample every | integer | 1 |
| `match` | match points by | menu: 0 PartId (else index), 1 index | 0 PartId (else index) |
| `maxPoints` | point limit | integer | 2e+06 |

## Cache PointZ

Remembers the last 'frames' cooks of its input (on the GPU, no copies) and outputs the one 'delay' frames back. Cache Select and Cache Blend read its frames by its 'tag'.

Tier B. Inputs: 1 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `frames` | frames | integer | 30 |
| `delay` | output delay | integer | 0 |
| `active` | record | on/off | 1 |
| `tag` | tag | text | cache |

## Cache Select PointZ

Outputs one frame of the Cache PointZ tagged 'target': 'index' frames back from the newest.

Tier B. Inputs: 0 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `target` | Cache PointZ tag | text | cache |
| `index` | index | integer | 0 |

## Cache Blend PointZ

Blends between two frames of the Cache PointZ tagged 'target' at a fractional 'position' (frames back): smooth slow motion and time-smear of P and Color when point counts match.

Tier B. Inputs: 0 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `target` | Cache PointZ tag | text | cache |
| `pos` | position | number | 0 |
| `attribs` | attributes | text | P,Color |

## GLSL Create PointZ

Makes points from nothing with your GLSL: 'points' of them (or a rows x columns grid), as points, one line, or grid faces. Set P, Color and any listed outputs per id.

Tier B. Inputs: 0 geometry, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `layout` | layout | menu: 0 points, 1 one line, 2 grid faces | 0 points |
| `count` | points | integer | 1000 |
| `rows` | rows | integer | 64 |
| `cols` | columns | integer | 64 |
| `outputs` | outputs | text | P,Color |
| `code` | code | text | float u = float(id) / float(uCount); P = vec3(cos(u * TAU... |
| `a` | uA | number | 0.5 |
| `b` | uB | number | 0.5 |
| `animate` | animate | on/off | 1 |

## GLSL Copy PointZ

Copies the input 'copies' times and lets your GLSL place each copy: copyId (0..uCopies-1) and the point's own values are in scope; change P, Color and any listed outputs.

Tier B. Inputs: 1 geometry, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `copies` | copies | integer | 16 |
| `outputs` | outputs | text | P |
| `code` | code | text | float u = float(copyId) / float(uCopies); P = rotY(u * TA... |
| `a` | uA | number | 0.5 |
| `b` | uB | number | 0.5 |
| `animate` | animate | on/off | 0 |

## GLSL Advanced PointZ

GLSL over the first input with random access to every attribute of the second: fetch_<name>(i) reads point i of input B (uCountB points), plus the image pin and knobs uA..uD. For nearest-point tricks, morphs and custom simulations.

Tier B. Inputs: 2 geometry + 1 image, cooks every frame while 'animate' is on.

| key | label | type | default |
|---|---|---|---|
| `outputs` | outputs | text | P |
| `code` | code | text | P = mix(P, fetch_P(id % uCountB), uA); |
| `a` | uA | number | 0.5 |
| `b` | uB | number | 0.5 |
| `c` | uC | number | 0.5 |
| `d` | uD | number | 0.5 |
| `animate` | animate | on/off | 1 |

## Point File In PointZ

Reads a point cloud: PLY (ascii or binary, with colours, normals and any extra per-point values), or a text table (.xyz, .pts, .csv, .txt: x y z [r g b]).

Tier B. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `path` | file | text |  |
| `scale` | scale | number | 1 |
| `every` | keep every | integer | 1 |
| `yUp` | Z-up file (swap to Y-up) | on/off | 0 |

## File In PointZ

Reads geometry with its faces and lines: OBJ (v / vt / vn / f / l) or PLY with faces.

Tier B. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `path` | file | text |  |
| `scale` | scale | number | 1 |

## Table to PointZ

Points from a table of numbers typed in (or pasted): one point per line, columns mapped by 'columns' (name:components, e.g. P:3,Color:4,PScale:1). Optionally joined into a line.

Tier B. Inputs: 0 geometry.

| key | label | type | default |
|---|---|---|---|
| `table` | table | text | 0 0 0 1 0 0 1 1 0 0 1 0 |
| `columns` | columns | text | P:3 |
| `as` | as | menu: 0 points, 1 line, 2 closed line | 0 points |
| `path` | or read file | text |  |

## File Out PointZ

Writes its input to a file - PLY (binary or ascii), OBJ or CSV of every attribute - when 'write' is switched on (once per switch), or on every change with 'auto'. Passes the input on.

Tier B. Inputs: 1 geometry, cooks every frame.

| key | label | type | default |
|---|---|---|---|
| `path` | file | text |  |
| `format` | format | menu: 0 PLY binary, 1 PLY ascii, 2 OBJ, 3 CSV | 0 PLY binary |
| `write` | write | on/off | 0 |
| `auto` | write on every change | on/off | 0 |

## Instance PointZ

Geometry instancing (TouchDesigner's Geometry COMP idea): draws the shape on the second input once at every point of the first, on the GPU, in one draw call - millions are fine. Each instance reads the point's P (translate), N (aim), PScale (uniform scale), Scale (per-axis scale), Rotate (degrees), Color and Active (0 hides it); rename any of them below. Instance textures: plug up to four images and pick one per instance by its TexIndex, by instance number or at random, or cut one image (or the material's texture) into an atlas grid of cells.

Tier B. Inputs: 2 geometry + 1 image.

| key | label | type | default |
|---|---|---|---|
| `scale` | scale | number | 0.05 |
| `scaleXYZ` | scale X / Y / Z | vector | 1 1 1 |
| `rotOffset` | rotate (added, degrees) | vector | 0 0 0 |
| `offset` | translate (added) | vector | 0 0 0 |
| `align` | aim along N | menu: 0 off, 1 +Z to N, 2 +Y to N | 0 off |
| `posAttr` | translate attribute | text | P |
| `scaleAttr` | scale attribute | text | PScale |
| `scale3Attr` | scale XYZ attribute | text | Scale |
| `rotAttr` | rotate attribute | text | Rotate |
| `colorAttr` | colour attribute | text | Color |
| `activeAttr` | active attribute | text | Active |
| `normalAttr` | aim attribute | text | N |
| `useColor` | colour instances | on/off | 1 |
| `texMode` | instance texture | menu: 0 material texture, 1 pick image by index, 2 atlas grid | 0 material texture |
| `texFrom` | texture index from | menu: 0 TexIndex attribute, 1 instance number, 2 random | 0 TexIndex attribute |
| `texAttr` | texture index attribute | text | TexIndex |
| `atlasCols` | atlas columns | integer | 4 |
| `atlasRows` | atlas rows | integer | 4 |

