# Field device catalog v1 — ready to start building

## Status

Introduction document, not a build step. Purpose: hand a concrete set of
22 cross-domain devices to whoever builds next, plus a final answer to
`OPEN-A` through `OPEN-E` so nobody has to re-litigate the language while
building a device. Supersedes nothing — `algorithms.md` remains the
canonical single-domain catalogue; this is the cross-domain layer on top
of it. Companions: `design-brief-language-opens.md` (the four original
questions), `cross-domain-devices-backward-analysis.md` (the 18-device
analysis these answers were derived from).

## The two ways to build a device (mechanism, corrected)

| Mechanism | When to use it | How |
|---|---|---|
| **Single-node dynamic pins** | The device is really one native node's behavior, with a second (or third) domain's worth of output/input bolted on | `reduce` the node's own domain data down to `frame`, then declare an extra `output <domain> <type> <name>` pin fed by that `frame` value — real, shipped (steps 11-14). Cheapest option, no bundling |
| **`FieldGraphNode` bundle** | The device genuinely needs more than one native node body (independent voices, a sim plus a separately-tuned second sim) | `emit` + `connect` several nodes, encapsulate as one box ("Instrument Mode", steps 15-17). Heavier, but the only option when one kernel body isn't enough |

Both mechanisms move data across domains **only** via `reduce` (many→one)
and implicit `broadcast` (one→many, never written), and both always
transit through `frame` — `element`/`pixel`/`sample` are mutually
incomparable peers with no direct link between them.

## Final answers: OPEN-A through OPEN-E

Derived from the 18-device backward analysis (tally: A in 9/18, B in 6/18,
C in 4/18, D in 1/18), not asserted cold. These are my recommendation for
sign-off, stated as final so building can start — not yet implemented.

| | Answer | Spelling |
|---|---|---|
| **OPEN-A** | Ring/delay state gets its own syntax now; a separate `table` declaration is added later, only when a real device needs indexed lookup (don't build it speculatively) | `state float buf[2048] = 0` with `buf.write(x)` / `buf.read(delay)`, offset clamped to size — no raw `buf[k]` indexing in v1 |
| **OPEN-B** | Ordered neighbour read, previous-cook semantics (keeps element kernels parallelizable) | `P.at(i-1)` / `attrib.at(offset)`, clamped to `[0, count-1]` |
| **OPEN-C** | Reuse `resample`'s concept rather than invent a fifth spelling; boundary mode is a keyword, not an overload of `param`'s range brackets (that overload was flagged as a real defect in an earlier draft) | `state float A = 1.0 boundary(wrap)`, offset read via the existing domain-crossing form, not a new `[wrap]` bracket |
| **OPEN-D** (2nd input) | Accepted as a narrow, node-shaped exception — rare enough (two-input adaptive filters only) that it doesn't warrant changing `in`'s type everywhere | `in2` as a second reserved name, present only on a node declaring it |
| **OPEN-D** (notes) | Folded into `frame`, not a sixth domain | `noteOn`, `notePitch`, `noteVel` reserved in `frame` |
| **OPEN-E** (new — per-point cross-domain sampling) | **Not solved as a new Field primitive in v1.** The existing render-pipeline pattern (rasterize geometry to a texture, sample that texture in a pixel/geometry node) is the sanctioned answer for now. Revisit a true per-element/per-pixel resample primitive only if a real device proves the render-pipeline route insufficient | render-to-texture + texture sample (existing node-graph feature, not new Field syntax) |

## Where each device actually lives — node-type mapping, corrected

Every device in this catalog is hosted in one of the four *existing* Field
node classes — checked directly against `src/nodes/`:

| Domain the kernel body runs in | Node class | File |
|---|---|---|
| `sample` | `FieldSampleNode` | `src/nodes/FieldSampleNode.h` / `.cpp` |
| `pixel` | `FieldPixelNode` | `src/nodes/FieldPixelNode.h` / `.cpp` |
| `element` | `FieldElementNode` | `src/nodes/FieldElementNode.h` / `.cpp` |
| `graph` (edit-time topology only) | `FieldGraphNode` | `src/nodes/FieldGraphNode.h` / `.cpp` |

**A real gap, worth flagging before anyone starts building a pure
modulator-style device:** `FieldGraphNode` is **not** a place to put a
live, continuously-running frame-domain kernel (a Bresenham sequencer, a
Hénon/Lorenz attractor, a PLL tempo tracker). Checked directly against
`src/nodes/FieldGraphNode.h` and `step-14-dynamic-pins-graph-node.md` §5.1:
`FieldGraphNode`'s kernel runs once, at *edit time*, to `emit`/`connect`/
`set`/`place` other nodes onto the canvas — it has **no output pins at
all**, and the parser rejects a `graph`-domain pin outright ("graph domain
has no per-frame dataflow to expose"). It structurally cannot stream out a
continuous modulation value. There is also **no existing frame-only Field
node** — `ModulatorNodes.h/.cpp` (Infinite's current LFO/Random/Pattern
modulators) are hand-written C++, not Field, so there's nothing to
piggyback on that's already Field-native either.

Two real options, for any device in this catalog whose logic is
essentially "a standalone modulator" (device #13's attractor trail, or any
future Bresenham/PLL-style device):

| Option | What it means | Cost |
|---|---|---|
| **(a) Piggyback** — declare a `frame`-domain `output` pin on whichever `FieldSampleNode`/`FieldElementNode`/`FieldPixelNode` the device already needs | Works **today**, no new node type — the modulator logic lives inside the device's existing node body and exposes itself as one more pin | Free |
| **(b) A fifth node type**, a real standalone `FieldFrameNode`/`FieldModulatorNode` | Doesn't exist, isn't in any of the 17 build steps — genuinely new scoping work (registration, save-format lines, UI), comparable in size to one of the earlier numbered steps | A real, unscheduled build step |

**Recommendation: (a) for now.** Every frame-only need in this catalog
(device #13's attractor history, or a future pure-sequencer device) can
piggyback on a node it's already touching for another reason. Only invest
in (b) once enough standalone-modulator devices exist that piggybacking
starts feeling cramped — don't build a fifth node type speculatively.

## The catalog: 22 devices

Devices 1-18 are from the backward-analysis exercise; 19-22 are your four
new ideas, added the same way.

| # | Device | Domains | Mechanism | The idea | OPEN dependency |
|---|---|---|---|---|---|
| 1 | Cloth Bell | element + sample | single-node pins | Simulated cloth hits a floor plane; impact rings a bell voice | B |
| 2 | Gradient-Synth Sphere | pixel + element + sample | single-node pins | A sphere samples a pixel gradient under it; that color tunes an oscillator | none (already legal via `resample`) |
| 3 | Reaction-Diffusion Drone | pixel + sample | single-node pins | RD sim's total concentration drives a drone's filter cutoff | C |
| 4 | Rope Harp | element + sample | single-node pins or bundle (8 voices) | A hanging rope's per-segment tension tunes 8 plucked strings | A + B |
| 5 | Flock Choir | element + sample | single-node pins | Bounded-radius flock density detunes a choir of voices | B (if written as ordered-neighbour reads, not the spatial-hash escape hatch) |
| 6 | Vortex Wind Chime | pixel + element + sample | bundle (needs E's render-pipeline workaround) | Fluid curl pushes floating particles that collide and ring | C, E |
| 7 | Bass Ribbon Mirror | sample + element + pixel | single-node pins (2 declared outputs) | Bass undulates a geometry ribbon and pulses a pixel glow in sync | none — cheapest 3-domain device here |
| 8 | Granular Skin | element + sample | bundle | Stressed vertices seed grains in an 8-voice granular player | A, plus k-argmax (§10.7, not a new OPEN) for picking 8 of 5000 |
| 9 | Comb-Filter Terrain | pixel + sample | single-node pins | A comb delay's tap spacing ripples a pixel heightmap | A |
| 10 | Voice-Reactive Growth | sample + element | single-node pins | Growth sim speeds up the louder you sing | B |
| 11 | Chladni Sand Synth | pixel + element + sample | bundle | One shared frequency param renders a Chladni pattern, positions sand, and tunes a synth | none — proves some 3-domain devices are free today |
| 12 | Feedback Skin Drum | element + pixel + sample | bundle + render-pipeline (E) | Drum impacts trigger a voice and paint a heatmap of hit locations | B, E |
| 13 | Strange-Attractor Sculpture | element + sample | bundle | An attractor's trail of past positions is drawn as points, drives an FM synth's mod index | A (a table, once built) |
| 14 | Pixel Skin Vocoder | pixel + sample | single-node pins | Vocal formant bands light up as colored stripes | A |
| 15 | Collision Bell Garden | element + sample | single-node pins | Bouncing particles ring a note on impact, velocity sets brightness | D, weakly (plain gate float works without it) |
| 16 | Liquid Metal Lens | pixel + element | render-pipeline trick, not Field crossing | Fluid sim visually refracts geometry rendered on top of it | C (for the fluid only) |
| 17 | Resonant Body Double | element + sample | bundle | Any mesh becomes a strikeable resonant object via modal filters | A + B |
| 18 | Living Palette | pixel + sample | single-node pins + per-user store | A palette that drifts toward the "mood" of what's been played, across sessions | A + the per-user store gap (`algorithms.md` §9.7) |
| **19** | **Boundary Chime** | element + sample | single-node pins | Displaced geometry rings a note the instant it crosses a line/box boundary — your "displacement touches a line/box → sound" idea | none new — plain boundary test (`if crossing-condition`) reduced to a trigger, same shape as #15 |
| **20** | **Reverb Bloom** | sample + element | bundle | A simple pad+reverb chain's tail shapes a growing/pulsing mesh at the end of the signal — your "sound → reverb → pad, generates geometry" idea | A (the reverb needs a ring buffer); the sample→geometry step is plain `reduce`→`frame`→`broadcast`, already legal |
| **21** | **Sound-Colored Field** | sample + pixel | single-node pins | Incoming audio's spectral balance continuously repaints a color field — your "colors that change due to incoming sound" idea | none new — `reduce.rms` at a few bands → `frame` → broadcast into `col`, the same shape as the existing Bass Ribbon pattern |
| **22** | **Color-Driven Terrain** | pixel + element | single-node pins or bundle | Incoming color (from video/image input) displaces or reshapes geometry — your "geometry that changes due to incoming color" idea | E, if each mesh point needs its *own* sampled color at its own position (per-point pixel→element); **none new** if it's an aggregate (average hue of the frame) reduced through `frame` instead |

Devices 21 and 22 are worth reading side by side: they're mirror images of
each other (sound→color vs. color→geometry) and both come essentially free
*if* the crossing is an aggregate (`reduce`), and both hit **OPEN-E** the
moment you want per-point resolution instead (each pixel individually
reacting to a different part of the audio spectrum's spatial layout, or
each mesh vertex sampling a different pixel under it). That's the cleanest
illustration in the whole catalog of exactly what OPEN-E is and isn't
blocking — worth pointing whoever builds #21/#22 first at this note.

## Where to start

Cheapest, most illustrative first builds, in order: **#7 (Bass Ribbon
Mirror)** and **#11 (Chladni Sand Synth)** need zero OPEN answers and prove
the single-node dynamic-pins mechanism end to end. **#19 (Boundary Chime)**
and **#21 (Sound-Colored Field)** are your new ideas that also need
nothing new — good next targets. Everything needing **OPEN-A** (9 of 22
devices) becomes buildable the moment the ring-buffer `state` syntax
above ships — that's the single highest-leverage piece of language work
in this whole catalog.
