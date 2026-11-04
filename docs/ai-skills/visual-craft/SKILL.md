---
name: infinite-visual-craft
description: Visual art craft mapped onto Infinite's nodes - art movements and styles, composition, colour, compositing, typography and fonts, and motion design - so a vague visual ask ("dreamy", "brutalist poster", "VHS", "Bauhaus", "liquid chrome") becomes a short chain of real nodes and values. Use whenever the user describes a look, style, mood, layout, font or movement for a picture or 3D render.
---

# Visual craft in Infinite

Turn a look into a chain: source -> shape it -> colour it -> finish it -> Output. Check every
type name with `infinite nodes` and every key with `infinite describe <Type>` before you use it.

## Styles and movements

| Ask | What makes it | Chain to start from |
|---|---|---|
| dreamy, ethereal | soft edges, glow, low contrast, slow drift | Noise -> gaussianblur -> bloom -> Curves (lift blacks); LFO on Noise speed, slow |
| VHS, lo-fi video | chroma bleed, scan lines, wobble, noise | Video -> glitch -> addnoise -> motionblur (small) -> vignette |
| glitch, datamosh | blocks, channel shifts, frame smear | glitch, pixelate, Time Machine, Optical Flow, Feedback |
| psychedelic, op art | symmetry, saturated cycling hue | kaleidoscope, mirror tile, twirl, Color Ramp; LFO on hue or ramp offset |
| Bauhaus, Swiss | primary colours, circles, squares, grid, flat fills | Circle, Rectangle, Triangle -> Layer Stack; Text in Helvetica Neue or Futura |
| brutalist | heavy type, raw black and white, hard edges | Text (Impact, Druk Wide Bold) -> threshold -> Layer Stack |
| risograph, print | limited inks, grain, misregistration | halftone or dither -> posterize -> Color Ramp (2 to 3 inks) -> addnoise |
| generative, algorithmic | rules, repetition, noise fields | FieldPixel, Sketch, Reaction Diffusion, L-System, Voronoi Cells |
| liquid, organic | flowing distortion, metaballs | Fluid, liquify, displace with Noise, Metaballs, Blob |
| cyberpunk, neon | dark base, magenta and cyan glow | edge outline -> bloom -> Color Ramp (magenta to cyan) -> Blend add |
| minimal | one shape, space, one accent colour | one Shape or Text, plenty of empty frame |
| chrome, Y2K | reflective metal, gradients, blobs | 3D: Supershape or Metaballs -> Material (metallic, low roughness) -> HDRI -> Render 3D |
| ink, watercolour | bleed, paper texture, uneven edges | Draw or Image Source -> diffuseglow -> displace (Noise) -> Texture blend |
| audio-reactive | picture follows the music | Audio Analyze or Audio to CV -> modulate scale, bloom, displace amount |

## Composition

- Rule of thirds: subject at 1/3 or 2/3 (Shape posX/posY 0.333 or 0.667). Centre only for
  symmetry or iconic frames.
- One hero, one support, the rest quiet. Sizes step down by about 0.6 each tier.
- Negative space is part of the picture; leave at least a third of the frame empty in minimal work.
- Lead the eye with lines (Line, Arrow, Chevron), light (vignette, Light) or the brightest colour.
- Safe areas: keep text inside 90% of the frame; for 9:16 keep it out of the top 14% and bottom 22%.
- Symmetry: mirror tile or kaleidoscope for formal, iconic or ritual moods; break it slightly to feel alive.

## Colour

| Scheme | Use | In Infinite |
|---|---|---|
| monochrome | calm, graphic | Color Ramp with two tones of one hue |
| complementary | energy, contrast | two inks opposite on the wheel, Color Ramp or Palette |
| analogous | harmony, mood | neighbouring hues, gradientmap |
| triadic | playful, Bauhaus | three primaries as flat fills |
| duotone | posters, album art | gradientmap or Color Ramp with two stops |

- Warm reads near, cool reads far. Saturated accents on a muted base.
- Grade last: Curves (contrast S), exposure, color adjustments, lut.
- Palette and Predictive Coloring pull colour from images or learn it; Image Analyze modulates from a picture.

## Compositing

| Need | Node |
|---|---|
| stack layers with blend modes | Layer Stack, Blend (add, screen, multiply, overlay) |
| cut out | chroma key, luma key, Remove Background, alpha from luma |
| glow, shadow | bloom, outerglow, dropshadow, diffuseglow |
| motion over time | Feedback, Trails, Time Machine, motionblur |
| depth in 2D | gaussianblur on the back layer, vignette, smaller and dimmer far layers |
| match two sources | Curves and color adjustments on one until blacks and whites agree |

Order matters: key -> colour -> blur -> glow -> grade -> Output.

## Typography and fonts

The Text node uses the system's fonts (`fontName`; `infinite describe Text` lists them on this
machine). Text 3D makes geometry from type.

| Voice | Fonts commonly on macOS |
|---|---|
| neutral, Swiss | Helvetica Neue, Avenir Next, Futura |
| editorial, elegant | Didot, Bodoni 72, Baskerville, Big Caslon |
| heavy, poster | Impact, Druk Wide Bold, DIN Condensed |
| technical, code | Menlo, Source Code Pro, PT Mono |
| hand, warm | Bradley Hand, Marker Felt, SignPainter |

- Two typefaces at most: one display, one text.
- Size ladder: headline 1, sub 0.5, caption 0.25 of the headline.
- Tight tracking for large display type (`tracking` negative), open tracking for small caps.
- Contrast: light type on dark or dark on light; outline (`outlineWidth`) only for type over busy pictures.
- Kinetic type: modulate `tracking`, `scaleX`/`scaleY`, `posY` with an Envelope or MSEG on the beat.

## Motion design

| Feel | Curve | In Infinite |
|---|---|---|
| snappy UI | ease out, 150 to 300 ms | MSEG with a fast attack, short decay |
| natural | ease in-out | LFO sine, Smoothing on a stepped source |
| bouncy | overshoot then settle | MSEG with an overshoot point, Envelope |
| mechanical | linear, stepped | LFO saw or square, Pattern, Random stepped |
| organic drift | slow noise | Random smooth, Drift |

- Sync to music: LFO and Pattern rates in beats (`rateBeats`); Envelope from MIDI Trigger or Audio to CV on the kick.
- Stagger: the same move on several layers, each offset by a fraction of a beat (LFO `phase`).
- Rest: hold a frame still between moves; constant motion reads as noise.
- Loops: rates that divide the loop length (1, 2, 4 bars) so the end meets the start.
