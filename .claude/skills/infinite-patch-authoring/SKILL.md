---
name: infinite-patch-authoring
description: Write, check and render Infinite patches (.inf text files) headless, without the UI. Use whenever the user asks you to build, change or debug an Infinite patch, wire nodes, add modulation, or render a frame, video or audio measurement from a patch file.
---

# Authoring Infinite patches headless

A patch is a line-based text file. You write it, Infinite checks it and renders it, and you read
the result back as JSON. Never guess a node's name, keys, slots or ranges: ask the binary.

`Infinite` below is the executable: `build/Infinite.app/Contents/MacOS/Infinite` on macOS,
`Infinite.exe` on Windows, `Infinite` on Linux. On Linux there is no display, so put
`xvfb-run -a` in front of every render (`E_NO_DISPLAY` means you forgot).

## The loop (do every step, in order)

1. **Describe** - `Infinite --describe "<type>" --json out.json`. Read the type's `params`
   (key, default, min, max, dropdown options), `inputs` (slot names) and `outputs`. Do this for
   every node type you use. Not from memory.
2. **Write** - the smallest patch that could work. Leave every parameter you do not change at its
   default; a node needs only the keys you set.
3. **Validate** - `Infinite --validate patch.inf`. Strict by default: warnings are errors too.
   Fix until the exit code is 0. Every error carries a line number and a hint.
4. **Explain and compare to intent** - `Infinite --explain patch.inf`. It prints the graph that
   was actually built. Read it against what the user asked for: is every wire you wrote there,
   is every node you expect reachable from an Output or Audio Out, is any parameter still at a
   default you meant to change? A patch can validate and still be the wrong patch.
5. **Look and listen** - `Infinite --frame patch.inf 0,1,2 dir/ --contact-sheet sheet.png` for
   pictures, `Infinite --audio-summary patch.inf out.json` for sound. Read `frame_stats` and
   `audio_summary` from the status line before you open any file.
6. **Fix** - change the patch, go back to step 3. Change one thing at a time.
7. **Render** - `Infinite --render patch.inf out.mp4 [--start S] [--duration S] [--fps N]`.

## Reading the result

The last line of stdout is one JSON object: `ok`, `mode`, `frames`, `fps`, `size`, `audio`,
`elapsed_ms`, `files`, `warnings[]`, `errors[]`, plus `frame_stats[]` (`--frame`) or
`audio_summary` (`--audio-summary`). Each issue is `{code, message, line, node, hint}`.

| Exit | Means |
|---|---|
| 0 | ok |
| 2 | bad command line (`E_USAGE`) |
| 3 | the patch does not load or validate; read `errors[0]` |
| 4 | refused: a hardware source (camera, microphone, MIDI) cannot render headless |
| 5 | rendering failed (`E_RENDER`, `E_NO_DISPLAY`) |
| 6 | timeout (raise `--timeout`) |

## The format in one screen

```
infinite-patch 1
node <index> <category> <type name>
  id <word>                 optional name you can use instead of the index
  f <key> <float>   i <key> <int or option name>   b <key> <0|1>
  c <key> <r> <g> <b>       s <key> <text to end of line>
end
cable <dst> <slot> <src>    image          geo <dst> <slot> <src>   geometry, camera, light
aud   <dst> <slot> <src>    audio          note <dst> <slot> <src>  notes
mod <dst> <param|key> <src> <srcOutput> <polarity> <depth> <centre>
expr <dst> <param|key> <expression>        glob <name> <expression>
```

- Wires read destination first: `cable out 0 shape` = "Output slot 0 is fed by shape".
- Names work everywhere an index does: node `id`s, slot names (`input`, `input_2`, ...), and a
  parameter's saved key in `mod`/`expr` (`mod shape sizeX lfo 0 0 1 0.5`).
- Free text (type names, strings, expressions) is always last on its line.
- Full reference: `docs/reference/patch-format.md`. Three verified starting points live in
  `assets/examples/authoring/` (image, audio, modulation).

## Rules that bite

- **Defaults rule.** Omit what you do not change. Never copy every key of a node into a patch.
- **No sigils.** Field and expression code uses bare names (`P.y += bass * 2`), never `@P.y`.
  Field kernels: see the `infinite-field-language` skill.
- **Bypass.** A node with two or more inputs never bypasses; a bypassed node does not cook.
  `flags 0 1 0 0` on such a node raises `W_BYPASS_IGNORED`.
- **Something must be reachable.** An image needs an `Utility Output`; sound needs an
  `Utility Audio Out` or an Output with audio wired in.
- **Animate inside the patch.** Use modulator nodes, `mod`, `expr` and `glob`. Do not render a
  frame at a time with edited parameters.
- **One-input merge nodes** (Blend, Layer Stack, Mixer) with an empty input dim rather than fail:
  `W_OPEN_INPUT`. Wire every input.

## Troubleshooting

| Symptom | Look at |
|---|---|
| Black frame, `W_BLACK_FRAME` | `--explain`: is the Output's image slot wired (`W_OUTPUT_EMPTY`)? Is the source's size or colour zero? Is a node upstream bypassed? |
| Silent audio, `W_SILENT` | is an `Audio Out` reachable from a synth? Do the notes reach the synth (`note` wire)? Is a volume or gain at 0? |
| `W_UNUSED_NODE` | the node reaches no Output or Audio Out: wire it in or delete it |
| `E_KIND_MISMATCH` | the source and destination slot kinds differ (image into an audio slot); `--describe` lists each slot's kind |
| `E_BAD_KEY` / `E_BAD_VALUE` | misspelled control key or dropdown option; the hint names the nearest valid one |
| `E_HARDWARE_SOURCE` (exit 4) | the patch has a camera, microphone or MIDI source; replace it with a file or generator |
| `E_NO_DISPLAY` (Linux) | run under `xvfb-run -a` |

## Node facts

Everything below is generated from `Infinite --describe --json` by `tools/gen-patch-skill.py`.
Do not edit between the markers; run the script. Use the `node` line exactly as shown (category
first, then the type name), then `--describe "<type>"` for parameters.

<!-- generated:begin -->
299 node types. `node <index> <category> <type>`; inputs are `slot name:kind`, outputs `label:kind`; `p` is the number of saved parameters; `bypass` marks single-input nodes that can be bypassed.


### 3D

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Array` | geo:geometry | out:geometry | 65 | bypass |
| `Arrow 3D` | texture:image | out:geometry | 50 | bypass |
| `Audio Displacement` | geo:geometry, audio:audio | out:geometry | 32 |  |
| `Audio Ribbon` | input:audio | out:geometry | 23 | bypass |
| `Camera` | none | out:camera | 13 | bypass |
| `Capsule` | texture:image | out:geometry | 50 | bypass |
| `Cloth` | geo:geometry | out:geometry | 38 | bypass |
| `Cone` | texture:image | out:geometry | 50 | bypass |
| `Cube` | texture:image | out:geometry | 50 | bypass |
| `Curve` | none | out:geometry | 32 | bypass |
| `Cylinder` | texture:image | out:geometry | 50 | bypass |
| `Delete` | geo:geometry | out:geometry | 65 | bypass |
| `Delete Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Depth Projection` | depth:image, color:image | out:geometry | 35 |  |
| `Difference` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Disc` | texture:image | out:geometry | 50 | bypass |
| `Displacement` | geo:geometry, texture:image | out:geometry | 23 |  |
| `Distribute Points in Grid` | none | out:geometry | 8 | bypass |
| `Distribute Points on Faces` | geo:geometry | out:geometry | 22 | bypass |
| `Dodecahedron` | texture:image | out:geometry | 50 | bypass |
| `Explode` | geo:geometry | out:geometry | 65 | bypass |
| `Extrude` | geo:geometry | out:geometry | 65 | bypass |
| `Extrude Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Field Modifier` | geo:geometry | geo:geometry | 11 | bypass |
| `Field Primitive` | none | geo:geometry | 13 | bypass |
| `Gear 3D` | texture:image | out:geometry | 50 | bypass |
| `Geometry` | texture:image | out:geometry | 50 | bypass |
| `HDRI` | none | out:environment | 3 | bypass |
| `Helix` | texture:image | out:geometry | 50 | bypass |
| `Icosphere` | texture:image | out:geometry | 50 | bypass |
| `Image to Points` | input:image | out:geometry | 10 | bypass |
| `Instance on Points` | points:geometry, shape:geometry, cloud:geometry | out:geometry | 25 |  |
| `Intersect` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Join Geometry` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Klein Bottle` | texture:image | out:geometry | 50 | bypass |
| `Light` | none | out:light | 10 | bypass |
| `Mapping` | geo:geometry | out:geometry | 11 | bypass |
| `Material` | geo:geometry, albedo:image, roughness:image, metallic:image, normal:image, ao:image, emission:image, clearcoat:image, sheen:image | out:geometry | 28 |  |
| `Merge by Distance` | geo:geometry | out:geometry | 1 | bypass |
| `Mesh to Edges` | geo:geometry | out:geometry | 22 | bypass |
| `Mesh to Faces` | geo:geometry | out:geometry | 22 | bypass |
| `Mesh to Points` | geo:geometry | out:geometry | 22 | bypass |
| `Metaballs` | cloud:geometry | out:geometry | 28 | bypass |
| `Mirror` | geo:geometry | out:geometry | 65 | bypass |
| `Mobius Strip` | texture:image | out:geometry | 50 | bypass |
| `Model 3D` | texture:image | out:geometry | 27 | bypass |
| `Normals` | geo:geometry | out:geometry | 65 | bypass |
| `Null 3D` | geo:geometry | out:geometry | 0 | bypass |
| `Ocean` | texture:image | out:geometry | 29 | bypass |
| `Octahedron` | texture:image | out:geometry | 50 | bypass |
| `Particle System` | none | out:geometry | 23 | bypass |
| `Plane` | texture:image | out:geometry | 50 | bypass |
| `Points to Vertices` | points:geometry | out:geometry | 18 | bypass |
| `Prism` | texture:image | out:geometry | 50 | bypass |
| `Pyramid` | texture:image | out:geometry | 50 | bypass |
| `Render 3D` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry, camera:camera, light_1:light, light_2:light, light_3:light, env:environment | out:image | 39 |  |
| `Resynthesize 3D` | geo:geometry | out:geometry | 13 | bypass |
| `Rounded Cube` | texture:image | out:geometry | 50 | bypass |
| `Screw` | geo:geometry | out:geometry | 65 | bypass |
| `Select` | geo:geometry | out:geometry | 65 | bypass |
| `Set Vertex Color` | geo:geometry, texture:image, palette:palette | out:geometry | 6 |  |
| `Smooth` | geo:geometry | out:geometry | 65 | bypass |
| `Solidify` | geo:geometry | out:geometry | 65 | bypass |
| `Sphere` | texture:image | out:geometry | 50 | bypass |
| `Star 3D` | texture:image | out:geometry | 50 | bypass |
| `Subdivide` | geo:geometry | out:geometry | 65 | bypass |
| `Supershape` | texture:image | out:geometry | 50 | bypass |
| `Switcher 3D` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 5 |  |
| `Tetrahedron` | texture:image | out:geometry | 50 | bypass |
| `Text 3D` | texture:image | out:geometry | 29 | bypass |
| `Torus` | texture:image | out:geometry | 50 | bypass |
| `Torus Knot` | texture:image | out:geometry | 50 | bypass |
| `Transform` | geo:geometry | out:geometry | 65 | bypass |
| `Transform Selected` | geo:geometry | out:geometry | 65 | bypass |
| `Triangulate` | geo:geometry | out:geometry | 65 | bypass |
| `Tube` | texture:image | out:geometry | 50 | bypass |
| `Twist` | geo:geometry | out:geometry | 65 | bypass |
| `Union` | geo_a:geometry, geo_b:geometry, geo_c:geometry, geo_d:geometry | out:geometry | 23 |  |
| `Wireframe` | geo:geometry | out:geometry | 65 | bypass |
| `Wrap` | source:geometry, target:geometry | out:geometry | 26 |  |

### AudioEffects

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Filter` | audio:audio | out:audio | 10 | bypass |
| `Bitcrush` | audio:audio | out:audio | 4 | bypass |
| `Chorus` | audio:audio | out:audio | 10 | bypass |
| `Cycle Shaper` | audio:audio | out:audio | 5 | bypass |
| `Delay` | audio:audio | out:audio | 10 | bypass |
| `Drive` | audio:audio | out:audio | 6 | bypass |
| `Dynamics` | audio:audio, sidechain:audio | out:audio | 9 |  |
| `EQ` | audio:audio | out:audio | 28 | bypass |
| `Field Effect` | in:audio | out:audio | 10 | bypass |
| `Flanger` | audio:audio | out:audio | 9 | bypass |
| `Formant Filter` | audio:audio | out:audio | 3 | bypass |
| `Frequency Shifter` | audio:audio | out:audio | 6 | bypass |
| `Limiter` | audio:audio | out:audio | 5 | bypass |
| `Phaser` | audio:audio | out:audio | 9 | bypass |
| `Pitch Shifter` | audio:audio | out:audio | 4 | bypass |
| `Plugin` | in:audio | out:audio | 7 | bypass |
| `Resonator Bank` | audio:audio | out:audio | 9 | bypass |
| `Reverb` | audio:audio | out:audio | 7 | bypass |
| `Ring Mod` | audio:audio | out:audio | 4 | bypass |
| `Spec Blur` | audio:audio | out:audio | 6 | bypass |
| `Stereo` | audio:audio | out:audio | 4 | bypass |
| `Stutter` | audio:audio | out:audio | 6 | bypass |
| `Transient Shaper` | audio:audio | out:audio | 3 | bypass |
| `Tremolo` | audio:audio | out:audio | 7 | bypass |
| `Wavetable Shaper` | audio:audio | out:audio | 8 | bypass |

### Compositing

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Color Ramp` | img:image, audio:audio | out:image | 28 |  |
| `Blend` | input:image, input_2:image | out:image | 2 |  |
| `Color Ramp` | input:image | out:image | 67 | bypass |
| `Comment` | none | out:image | 5 |  |
| `Curves` | input:image | out:image | 6 | bypass |
| `Feedback` | input:image | out:image | 1 | bypass |
| `Fit` | input:image | out:image | 6 | bypass |
| `Group` | none | out:image | 4 | bypass |
| `Layer Stack` | input:image, input_2:image, input_3:image, input_4:image | out:image | 8 |  |
| `Null` | in:image | out:image | 0 | bypass |
| `Reaction Diffusion` | input:image | out:image | 11 | bypass |
| `Remove Background` | input:image | out:image | 9 | bypass |
| `Switcher` | input:image, input_2:image, input_3:image, input_4:image | out:image | 5 |  |
| `Trails` | input:image | out:image | 7 | bypass |
| `Viewport` | in:image | out:image | 0 | bypass |
| `alpha from luma` | input:image | out:image | 1 | bypass |
| `alpha invert` | input:image | out:image | 0 | bypass |
| `alpha levels` | input:image | out:image | 3 | bypass |
| `chroma key` | input:image | out:image | 5 | bypass |
| `color adjustments` | input:image | out:image | 23 | bypass |
| `coloroverlay` | input:image | out:image | 2 | bypass |
| `dropshadow` | input:image | out:image | 4 | bypass |
| `exposure` | input:image | out:image | 1 | bypass |
| `gradientmap` | input:image | out:image | 3 | bypass |
| `invert` | input:image | out:image | 0 | bypass |
| `lookup` | input:image, input_2:image | out:image | 3 |  |
| `luma key` | input:image | out:image | 5 | bypass |
| `lut` | input:image, input_2:image | out:image | 2 |  |
| `opacity` | input:image | out:image | 1 | bypass |
| `outerglow` | input:image | out:image | 2 | bypass |
| `posterize` | input:image | out:image | 1 | bypass |
| `premultiply` | input:image | out:image | 1 | bypass |
| `set alpha` | input:image, input_2:image | out:image | 0 |  |
| `show alpha` | input:image | out:image | 0 | bypass |
| `threshold` | input:image | out:image | 1 | bypass |
| `transform` | input:image | out:image | 10 | bypass |

### Effects

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Resynthesize` | input:image | out:image | 20 | bypass |
| `addnoise` | input:image | out:image | 1 | bypass |
| `bloom` | input:image | out:image | 3 | bypass |
| `boxblur` | input:image | out:image | 1 | bypass |
| `convolve` | input:image | out:image | 13 | bypass |
| `crop` | input:image | out:image | 6 | bypass |
| `diffuseglow` | input:image | out:image | 2 | bypass |
| `displace` | input:image, input_2:image | out:image | 2 |  |
| `edge outline` | input:image | out:image | 3 | bypass |
| `edge sobel` | input:image | out:image | 2 | bypass |
| `emboss` | input:image | out:image | 4 | bypass |
| `gaussianblur` | input:image | out:image | 1 | bypass |
| `glitch` | input:image | out:image | 5 | bypass |
| `halftone` | input:image | out:image | 3 | bypass |
| `kaleidoscope` | input:image | out:image | 5 | bypass |
| `lensdistortion` | input:image | out:image | 3 | bypass |
| `liquify` | input:image | out:image | 3 | bypass |
| `mirror tile` | input:image | out:image | 1 | bypass |
| `motionblur` | input:image | out:image | 2 | bypass |
| `normal map` | input:image | out:image | 3 | bypass |
| `pinchpunch` | input:image | out:image | 4 | bypass |
| `pixelate` | input:image | out:image | 1 | bypass |
| `radialblur` | input:image | out:image | 3 | bypass |
| `ripple` | input:image | out:image | 5 | bypass |
| `symmetry` | input:image | out:image | 4 | bypass |
| `twirl` | input:image | out:image | 4 | bypass |
| `unsharpmask` | input:image | out:image | 2 | bypass |
| `vignette` | input:image | out:image | 5 | bypass |

### Macros

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Macro Bipolar Knob` | none | out:modulator | 2 | bypass |
| `Macro Knob` | none | out:modulator | 2 | bypass |
| `Macro NumBox` | none | out:modulator | 5 | bypass |
| `Macro Radio Selector` | none | out:modulator | 3 | bypass |
| `Macro Slider` | none | out:modulator | 2 | bypass |
| `Macro Step Gate` | none | out:modulator | 3 | bypass |
| `Macro Toggle` | none | out:modulator | 2 | bypass |
| `Macro Trigger` | none | out:modulator | 2 | bypass |
| `Macro XY` | none | x:modulator, y:modulator | 5 | bypass |

### Modulators

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio Analyze` | audio:audio | level:modulator, low:modulator, mid:modulator, high:modulator, onset:modulator, b1:modulator, b2:modulator, b3:modulator, b4:modulator, b5:modulator, b6:modulator, b7:modulator, b8:modulator | 4 | bypass |
| `Audio File` | none | out:audio | 9 | bypass |
| `Audio to CV` | audio:audio | out:modulator | 6 | bypass |
| `CV Recorder` | in:modulator | out:modulator | 6 | bypass |
| `CV to Pitch` | in:modulator | out:modulator | 6 | bypass |
| `Compare` | input:modulator, input_2:modulator | out:modulator | 4 |  |
| `Constant` | none | out:modulator | 1 | bypass |
| `Envelope` | in:modulator | out:modulator | 6 | bypass |
| `Geometry Table` | geo:geometry | cx:modulator, cy:modulator, cz:modulator, spread:modulator, x1:modulator, y1:modulator, z1:modulator, x2:modulator, y2:modulator, z2:modulator, x3:modulator, y3:modulator, z3:modulator, x4:modulator, y4:modulator, z4:modulator | 10 | bypass |
| `Image Analyze` | input:image | result:modulator, bright:modulator, contrast:modulator, red:modulator, green:modulator, blue:modulator, sat:modulator, hue:modulator, motion:modulator, cx:modulator, cy:modulator | 14 | bypass |
| `Invert` | in:modulator | out:modulator | 3 | bypass |
| `LFO` | none | out:modulator | 5 | bypass |
| `MIDI CC` | none | out:modulator | 7 | hardware: refused headless, bypass |
| `MIDI Trigger` | none | out:modulator | 6 | hardware: refused headless, bypass |
| `Math` | input:modulator, input_2:modulator | out:modulator | 6 |  |
| `Mod Curve` | in:modulator | out:modulator | 3 | bypass |
| `Mod Depth` | in:modulator | out:modulator | 2 | bypass |
| `Note to CV` | notes:note | out:modulator | 3 | bypass |
| `Null Modulator` | in:modulator | out:modulator | 1 | bypass |
| `Palette` | ref:image | out:palette | 14 | bypass |
| `Path` | curve:geometry, geo:geometry | x:modulator, y:modulator, z:modulator, t:modulator | 14 |  |
| `Pattern` | none | out:modulator | 24 | bypass |
| `Random` | none | out:modulator | 5 | bypass |
| `Range to Range` | in:modulator | out:modulator | 6 | bypass |
| `Smoothing` | in:modulator | out:modulator | 2 | bypass |
| `Velocity to CV` | notes:note | out:modulator | 2 | bypass |
| `Vibrato` | none | out:modulator | 1 | bypass |

### Notes

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Arpeggiator` | notes:note | out:note | 8 | bypass |
| `Bouncing Balls` | none | out:note | 9 | bypass |
| `Chorder` | none | out:note | 9 | bypass |
| `Gate` | notes:note | out:note | 1 | bypass |
| `Glide` | notes:note | out:note | 1 | bypass |
| `Humanizer` | notes:note | out:note | 2 | bypass |
| `Keyboard` | none | out:note | 5 | bypass |
| `MIDI Notes` | none | out:note | 4 | hardware: refused headless, bypass |
| `Note Capturer` | notes:note | out:note | 2 | bypass |
| `Note Echo` | notes:note | out:note | 7 | bypass |
| `Note Filter` | notes:note | out:note | 6 | bypass |
| `Note Merge` | in_1:note, in_2:note, in_3:note, in_4:note | out:note | 0 |  |
| `Note Router` | notes:note | 1:note, 2:note, 3:note, 4:note | 2 | bypass |
| `Note Sequencer` | none | out:note | 54 | bypass |
| `Note Stack` | notes:note | out:note | 17 | bypass |
| `Note Strum` | notes:note | out:note | 1 | bypass |
| `Note Switcher` | in_1:note, in_2:note, in_3:note, in_4:note | out:note | 5 |  |
| `Note Transpose` | notes:note | out:note | 2 | bypass |
| `Pitch Bend` | notes:note | out:note | 1 | bypass |
| `Quantizer` | notes:note | out:note | 1 | bypass |
| `Random Note Generator` | none | out:note | 10 | bypass |
| `Velocity Curve` | notes:note | out:note | 1 | bypass |

### Prediction

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Drift` | none | out:predictor | 20 | bypass |
| `Moves` | none | out:predictor | 6 | bypass |
| `Predictive Coloring` | input:image | out:image | 7 | bypass |
| `Predictive Modulator` | in:modulator | out:modulator | 6 | bypass |
| `Predictive Notes` | notes:note | out:note | 10 | bypass |
| `Predictive Quantize` | notes:note | out:note | 1 | bypass |
| `Predictive Rhythm` | notes:note | out:note | 5 | bypass |
| `Predictive Velocity` | notes:note | out:note | 1 | bypass |

### Source

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Arrow` | none | out:image | 20 | bypass |
| `Audio Texture` | input:audio | out:image | 4 | bypass |
| `Blob` | none | out:image | 20 | bypass |
| `Chevron` | none | out:image | 20 | bypass |
| `Circle` | none | out:image | 20 | bypass |
| `Crescent` | none | out:image | 20 | bypass |
| `Cross` | none | out:image | 20 | bypass |
| `Draw` | input:image | out:image | 13 | bypass |
| `Ellipse` | none | out:image | 20 | bypass |
| `FieldPixel` | none | out:image | 9 | bypass |
| `Formula` | none | out:image | 8 | bypass |
| `Gear` | none | out:image | 20 | bypass |
| `Heart` | none | out:image | 20 | bypass |
| `Hexagon` | none | out:image | 20 | bypass |
| `Image Source` | none | out:image | 1 | bypass |
| `Line` | none | out:image | 20 | bypass |
| `Noise` | none | out:image | 15 | bypass |
| `Pie` | none | out:image | 20 | bypass |
| `Polygon` | none | out:image | 20 | bypass |
| `Ramp` | none | out:image | 22 | bypass |
| `Rectangle` | none | out:image | 20 | bypass |
| `Ring` | none | out:image | 20 | bypass |
| `Rounded Rect` | none | out:image | 20 | bypass |
| `Shape` | none | out:image | 20 | bypass |
| `Slideshow` | none | out:image | 7 | bypass |
| `Star` | none | out:image | 20 | bypass |
| `Superellipse` | none | out:image | 20 | bypass |
| `Teardrop` | none | out:image | 20 | bypass |
| `Text` | none | out:image | 20 | bypass |
| `Texture` | none | out:image | 47 | bypass |
| `Triangle` | none | out:image | 20 | bypass |
| `Video` | none | video:image, audio:audio | 7 | bypass |
| `Video In` | none | out:image | 4 | hardware: refused headless, bypass |

### Synths

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Analog` | notes:note | out:audio | 33 | bypass |
| `Drum Sequencer` | none | out:audio, 1:audio, 2:audio, 3:audio, 4:audio, 5:audio, 6:audio, 7:audio, 8:audio | 169 | bypass |
| `Equation Synth` | notes:note | out:audio | 30 | bypass |
| `Field Synth` | notes:note, in:audio | out:audio | 13 | bypass |
| `Grain Molder` | notes:note, record_in:audio | out:audio | 14 | bypass |
| `Granular` | record_in:audio | out:audio | 21 | bypass |
| `Looper` | audio:audio | out:audio | 9 | bypass |
| `MPC` | notes:note | out:audio | 177 | bypass |
| `Metallic` | notes:note | out:audio | 15 | bypass |
| `Molder` | record_in:audio | out:audio | 16 | bypass |
| `Oscillator` | notes:note, fm_in:audio | out:audio | 22 | bypass |
| `PaulStretch` | record_in:audio | out:audio | 14 | bypass |
| `Sampler` | notes:note, record_in:audio | out:audio | 14 | bypass |
| `Slicer` | notes:note, record_in:audio | out:audio | 13 | bypass |
| `Spectral Synth` | image:image, note:note | out:audio | 33 | bypass |
| `Wave Terrain` | texture:image, note:note | out:audio | 35 | bypass |
| `Wavetable` | notes:note, fm_in:audio | out:audio | 73 | bypass |

### Utility

| Type | Inputs | Outputs | p | Notes |
|---|---|---|---|---|
| `Audio In` | none | out:audio | 4 | hardware: refused headless, bypass |
| `Audio Meter` | audio:audio | out:audio | 0 | bypass |
| `Audio Out` | audio:audio | out:image | 2 | bypass |
| `Blend Audio` | a:audio, b:audio | out:audio | 1 |  |
| `Field Graph` | none | out:image | 7 | bypass |
| `Gain` | audio:audio | out:audio | 1 | bypass |
| `Mixer` | in_1:audio, in_2:audio, in_3:audio, in_4:audio, in_5:audio, in_6:audio, in_7:audio, in_8:audio | out:audio | 49 |  |
| `OSC Receive` | none | out:modulator | 4 | bypass |
| `OSC Send` | in:modulator | out:image | 5 | bypass |
| `Output` | in:image, audio:audio | out:image | 9 |  |
| `Projection` | in:image | out:image | 135 | bypass |
| `Splitter` | audio:audio | out:audio | 0 | bypass |
| `Syphon In` | none | out:image | 3 | hardware: refused headless, bypass |
| `Syphon Out` | in:image | out:image | 1 | bypass |
<!-- generated:end -->
