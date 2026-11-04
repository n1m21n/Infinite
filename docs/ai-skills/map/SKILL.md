---
name: infinite-map
description: The map of Infinite for any agent working with it - what exists (node families, panels, languages, manuals, validators), which command answers which question, how to read what the user is pointing at, and how to turn a vague ask into concrete node suggestions with buttons. Use at the start of every Infinite conversation, and whenever unsure where a fact lives.
---

# Infinite map

Infinite is a live audiovisual instrument: one canvas of nodes for picture, 3D, sound, notes and
modulation, played live, rendered or projected. Every fact below comes from a command. Ask the
command, never your memory.

## Where each answer lives

| Question | Command |
|---|---|
| What is the user looking at and touching? | `infinite context` (its `attention` part) |
| Every node, its index and title | `infinite context`, `infinite nodes` (types by family) |
| A node type's params, pins, ranges, options | `infinite describe <Type>` |
| A node in the patch: keys, values, what is modulated | `infinite params <node> --full` |
| What a node is for, a thing to try, a chain it fits | `infinite docs nodes <Type>` |
| Keyboard shortcuts | `infinite docs shortcuts` |
| Panels and how to open them | `infinite docs panels` |
| What can be cabled into what | `infinite docs connections` |
| Field language, full manual | `infinite docs field`; quick reference `infinite code docs field` |
| Sketch (JS 2D), Sketch 3D (three.js style), GLSL, expressions | `infinite code docs sketch\|sketch3d\|glsl\|expressions` |
| Working examples per language | `infinite code examples <kind> [name]` |
| Render, check, drive without the window | `infinite docs headless` |
| .inf file format, geometry domains | `infinite docs patch-format`, `infinite docs geometry` |
| Craft: pictures, sound, film | the infinite-visual-craft, infinite-sound-craft and infinite-film-craft skills |

## Attention: what the user means by "this"

`infinite context` returns `attention`:

| Field | Meaning |
|---|---|
| `selected` | the nodes selected on the canvas |
| `touched` | the control the user last pressed or dragged: node, key, label, value, range |
| `hovered` | the control under the mouse now |
| `keyboard` | the control Tab focus is on |
| `panels` | the panels open now |

"Make this slower", "this knob", "it": resolve to `touched`, then `selected`, then `hovered`. Say
which one you took in a few words ("the speed you just moved on @lfo#4").

## Validators: check before you say it works

| Language | Check |
|---|---|
| Field Pixel | `infinite code check k.field` (any file: `--type <kind>`, kinds from `infinite code kinds`) |
| Field Synth | `infinite code check k.synth.field` |
| Field Modifier, Field Primitive | `infinite code check k.modifier.field`, `k.primitive.field` |
| Sketch (JavaScript, 2D) | `infinite code check s.sketch.js` |
| Sketch 3D (JavaScript, three.js style) | `infinite code check s.sketch3d.js` |
| GLSL (Shadertoy node) | `infinite code check s.glsl` |
| Formula, Equation Synth | `infinite code check f.formula`, `e.equation` |
| A patch file | `infinite patch check p.inf` |
| The open window | `infinite code errors` |

Never hand the user code that has not passed its check.

## Node families

| Family | For | Reach for |
|---|---|---|
| Source | first picture | Shape, Noise, Ramp, Text, Image Source, Video, Video In, FieldPixel, Shadertoy, Sketch, Draw |
| Effects | change a picture | gaussianblur, bloom, glitch, kaleidoscope, displace, halftone, pixelate, ripple, edge outline |
| Compositing | colour, layers, time | Blend, Layer Stack, Curves, Color Ramp, Feedback, Trails, Fluid, Reaction Diffusion, chroma key |
| 3D | geometry to a render | primitives, Model 3D, Instance on Points, Particle System, Field Modifier, Sketch 3D, Material, Camera, Light, Render 3D |
| Synths | make sound | Analog, FM, Wavetable, Piano, String, Sampler, Granular, Drum Sequencer, Field Synth |
| AudioEffects | change sound | Audio Filter, EQ, Dynamics, Delay, Reverb, Drive, Chorus, Stutter, Convolution Reverb |
| Notes | notes and MIDI | MIDI Notes, Note Sequencer, Arpeggiator, Chorder, Piano Roll, Quantizer, Humanizer |
| Modulators | move a value over time | LFO, Random, MSEG, Envelope, Audio Analyze, Audio to CV, Smoothing, Image Analyze, Hand Track |
| Macros | knobs for playing live | Macro Knob, Macro XY, Macro Trigger |
| Prediction | learned modulators | Drift, Moves, Predictive Modulator, Predictive Notes |
| Utility | in and out | Output, Audio Out, Mixer, Syphon/NDI In and Out, OSC, MIDI Out, Projection |

`infinite nodes` is the full list; this table is where to start.

## Vague asks: suggest, then offer buttons

People start vague ("make it dreamy", "something for a techno set"). Do this:

1. Read `infinite context`. Build on what is there.
2. Name the look or sound in craft terms (one line), using the craft skills.
3. Offer two or three concrete moves, each a node or a value, each with a button.
4. Do it yourself only when the ask is clear enough; otherwise let the buttons do it.

## Buttons

Write a button as a markdown link to `verb:arg`. The chat draws it as a button the user presses.

| Button | Write |
|---|---|
| add a node | `[Add Bloom](add:bloom)` (the type name exactly as `infinite nodes` lists it) |
| set a value | `[Slower](set:@lfo#4.rateHz=0.25)` |
| delete a node | `[Remove blur](delete:@gaussianblur#6)` |
| undo | `[Undo](undo:)` |
| go to a node or param | `[Show](go:@rings#7)`, `[Show speed](go:@rings#7.speed)` |
| open a panel | `[Open timeline](open:timeline)` (names from `infinite docs panels`) |

Labels: two or three words, a verb first. Put buttons on their own line after the suggestion
they belong to. Never offer a button for a node or key you have not checked exists.
