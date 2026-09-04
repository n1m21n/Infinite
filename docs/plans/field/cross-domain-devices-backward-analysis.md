# Cross-domain Field devices, and answering OPEN-A/B/C/D backward

## Status

Not started — a design exercise, no code. Companion to
`design-brief-language-opens.md`. Method: instead of arguing the four OPEN
questions abstractly, design concrete devices that span more than one of
Field's domains in one instrument, and let each device's *actual* needs
tell us which OPEN question is load-bearing and how urgently. Every
cross-domain mechanism cited below (`reduce`, implicit `broadcast`,
`FieldGraphNode` bundling) is real and already merged into `main` — see
Sourcing note at the end.

## The real mechanism, in one paragraph — corrected

**Correction to this doc's first version:** I originally wrote that no
single Field node can have pins in more than one domain and that a
cross-domain device always needs a `FieldGraphNode` bundle. That's wrong —
checked against the actual merged source (steps 11-14, "Dynamic Pins," are
real, not just planned: `src/core/field/FieldIR.h`'s `declaredOutputs`/
`declaredInputs`, wired into `FieldElementNode`/`FieldPixelNode`/
`FieldSampleNode` in `src/nodes/*.h/.cpp`, `git log` confirms all four steps
merged). A single node's kernel **can** declare extra `output <domain>
<type> <name>` pins in a *different* domain from its own native shape —
e.g. an ordinary geometry (`FieldElementNode`) can also declare a `sample`-
domain `audio` output pin on itself, no second node required.

The catch, which matters for device design: `FieldIR.cpp`'s domain-join
check (`CheckPinDomainOk`) treats `element`/`pixel`/`sample` as **mutually
incomparable peers** — a declared pin can only be fed by an expression in
the *same* domain, or one that's `frame` or `graph` (which broadcast down
to anything). So this doesn't bypass the reduce→frame→broadcast rule from
the paragraph below — it just means the reduce can now land on a **second
pin of the same node** instead of requiring a separate bundled node. A
device that needs true peer-to-peer domain crossing (one node's per-element
value feeding another node's per-sample value *directly*, with no frame
layover) still doesn't exist — see Finding E, which is unaffected by this
correction.

**So there are two real mechanisms for a cross-domain device, not one:**
(1) **single-node dynamic pins** — one node, e.g. `FieldElementNode`,
declares extra pins in other domains fed by its own `reduce`d-to-`frame`
values; cheapest, no bundling, no `FieldGraphNode` needed. (2)
**`FieldGraphNode` bundling** — for when the device genuinely needs more
than one *native* node body (e.g. two independent sample voices, or a
geometry sim plus a separately-parameterized pixel sim), `emit`+`connect`
several nodes and encapsulate as one box (step 15/16/17 — "Instrument
Mode", `.infdev` export). Data crosses domains, in either mechanism, only
via `reduce` (many→one, explicit) and implicit `broadcast` (one→many,
never written), both always through `frame`. **There is still no direct
element↔pixel, element↔sample, or pixel↔sample operator at the per-value
level** — every cross-domain value goes through `frame` as a layover,
whether that lands on a second pin of the same node or on a second node
entirely.

## 18 devices

| # | Device | Domains | The idea | Cross-domain mechanism | OPEN dependency |
|---|---|---|---|---|---|
| 1 | **Cloth Bell** | element + sample | A simulated cloth hits a floor plane; the impact rings a bell voice | `reduce.max` on impact velocity → `frame` → broadcast into a sample voice's gate/amplitude | **B** (cloth needs neighbour-constraint solving) |
| 2 | **Gradient-Synth Sphere** | pixel + element + sample | A sphere's position samples a pixel gradient underneath it; that sampled color drives an oscillator's pitch | `resample` — reading the pixel domain *at the sphere's own position*, which is exactly what `resample` already means ("read domain A while standing in domain B") | **none** — already legal today |
| 3 | **Reaction-Diffusion Drone** | pixel + sample | A Gray-Scott sim's total chemical concentration drives a drone synth's filter cutoff | `reduce.mean` on the pixel state → `frame` → broadcast into `cutoff` | **C** (the RD sim itself needs neighbour pixel reads — the crossing out to audio is free) |
| 4 | **Rope Harp** | element + sample | A hanging Verlet rope's per-segment tension, mapped one segment per string voice, tunes 8 plucked strings | tension per element (needs OPEN-B for the rope), reduced/selected per voice, driving 8 Karplus-Strong-style resonators (needs OPEN-A ring per voice) | **A + B** |
| 5 | **Flock Choir** | element + sample | A boid flock's local density (bounded-radius neighbours, not full n-body) modulates a choir of oscillator voices' detune | flocking bounded via the fixed-grid spatial-hash escape hatch (`algorithms.md` §10.5), not true n-body — `reduce` the local count → `frame` → broadcast | **B** (only if flocking is written as ordered-neighbour reads rather than the spatial-hash escape hatch — worth deciding per-device, not assumed) |
| 6 | **Vortex Wind Chime** | pixel + element + sample | A fluid sim's curl field pushes individual floating particles, which collide and ring | the fluid sim needs OPEN-C; pushing *each particle individually* from the *pixel field at that particle's own position* is **not** expressible today — there is no per-element sample of a pixel field, only an aggregate `reduce` | **C, and a genuinely new gap (see Finding E)** |
| 7 | **Bass Ribbon Mirror** | sample + element + pixel | Bass frequencies both undulate a geometry ribbon and pulse a pixel glow, in sync | `reduce.rms(in, 20, 160)` → `frame` → broadcast twice (once into `element`, once into `pixel`) | **none** — already legal, cheapest 3-domain device on this list |
| 8 | **Granular Skin** | element + sample | A mesh's oldest/most-stressed vertices (5000 elements) each seed a grain in an 8-voice granular player | picking 8 out of 5000 is neither `map` nor `reduce` — it's the bounded **k-argmax** pattern (`algorithms.md` §10.7), already a known allowed technique, not a new primitive; each grain itself needs a frozen buffer | **A** (the grain buffer), plus confirms k-argmax (not a new OPEN) solves the many→few step |
| 9 | **Comb-Filter Terrain** | pixel + sample | An audio comb delay's tap spacing visualized as a rippling terrain heightmap | comb needs OPEN-A; the ripple pattern is pure-formula pixel reading the same delay-time param via broadcast | **A** |
| 10 | **Voice-Reactive Growth** | sample + element | A differential-growth curve grows faster the louder you sing into it | `reduce.rms` → `frame` → broadcast into the growth sim's spring constant; the growth sim itself needs neighbour reads | **B** |
| 11 | **Chladni Sand Synth** | pixel + element + sample | A standing-wave formula simultaneously renders a Chladni pattern, positions "sand" particles at its nodal lines, and tunes an oscillator to the same frequency param | one shared `param freq`, read as a pure formula independently in all three domains — no state, no crossing operator needed at all | **none** — proves some 3-domain devices need zero OPEN answers |
| 12 | **Feedback Skin Drum** | element + pixel + sample | A drum membrane's impact location and depth both trigger a sample voice and paint a heatmap of where it's been hit | the sample trigger is `reduce.max` → `frame`, fine; painting *per-impact-location* onto a pixel texture from element data has no direct operator — today this is done by rendering geometry to a texture (an existing render-pipeline feature), not a Field-language crossing | **B, and Finding E again** |
| 13 | **Strange-Attractor Sculpture** | element + sample | A chaotic attractor's trail of past positions is drawn as a string of points in space, and also drives an FM synth's mod index | drawing the *trail* means each element `i` reads the attractor's state from `i` steps ago — a table/history read, not a single delay | **A**, specifically option (c)'s indexed table, not just a ring |
| 14 | **Pixel Skin Vocoder** | pixel + sample | Vocal formant bands light up as colored stripes across a gradient | formant tracking needs delay history (LPC); each stripe just reads its own fixed frame-domain formant-band value via broadcast | **A** |
| 15 | **Collision Bell Garden** | element + sample | Bouncing particles ring a note on impact, velocity sets the note's brightness | collision detection is element-domain math already legal; whether the trigger is a plain broadcast float or a real `noteOn`/velocity pair depends on whether note-shaped data is wanted | **D, weakly** — a plain gate float already works; D only matters if this needs to look like a *note* to downstream note-aware nodes |
| 16 | **Liquid Metal Lens** | pixel + element | A fluid sim visually refracts geometry rendered on top of it | this is a rendering/compositing trick (distort the render using the fluid's velocity field as a lookup), not a Field-language domain crossing at all | **C** for the fluid itself; **not** a Field-language gap otherwise |
| 17 | **Resonant Body Double** | element + sample | Any mesh becomes a strikeable resonant object — a handful of modal filters tuned to the mesh's own vibration modes | mesh vibration needs neighbour-coupled mass-spring math; each mode is a small resonator needing its own delay state | **A + B together** — the richest physically-modeled instrument on this list |
| 18 | **Living Palette** | pixel + sample | A color palette that drifts toward the timbral "mood" of what's been played, across sessions, not just within one patch | `reduce` on spectral features → `frame` → a *learned* palette bias that must outlive a single patch | **A** (a table) **+ the per-user store** — this is a rung-4 device, gated by the same store gap flagged in `algorithms.md` §9.7 |

## Backward-derived findings

Tallying which OPEN question is actually load-bearing across these 18,
rather than reasoning about the language in the abstract:

| OPEN | Appears in | Read |
|---|---|---|
| **A** (state arrays/ring/table) | 9 of 18 devices (4, 8, 9, 13, 14, 17, 18, plus 2 more implicitly) | **The single most-needed primitive by a wide margin.** Almost every device that's actually interesting — anything physically modeled, anything with grain/echo/history — needs it. This alone argues for answering OPEN-A first, of the four |
| **B** (element neighbour reads) | 6 of 18 (1, 4, 5, 10, 12, 17) | Second most needed. Every device involving a simulated *material* (cloth, rope, growth, mesh) needs it — this is the primitive that makes geometry feel physical rather than decorative |
| **C** (pixel offset reads) | 4 of 18 (3, 6, 12, 16) | Needed specifically for the *generative-pixel-as-its-own-system* category (fluid, RD) — but notably, once a pixel sim exists, getting its result *out* to sound or geometry needs no new primitive (`reduce` already does it). C's scope is narrower than A/B: it matters for building the pixel system, not for connecting it to anything else |
| **D** (2nd sample input / notes) | 1 of 18, weakly (15) | **Barely came up.** For *this* specific ambition — one instrument spanning all three domains — D is the least urgent of the four. It matters a great deal elsewhere (rung-4 learning, note-transition prediction, echo cancellation) but almost none of the richest cross-domain devices above actually need it |

**Suggested priority order, derived from this exercise rather than asserted
up front: A, then B, then C, then D.** This is the same order I'd have
guessed, but now it's backed by 18 concrete devices rather than intuition —
worth citing that reasoning if this goes to the language owner.

### A genuinely new finding: call it OPEN-E

Three devices (6, 12, and implicitly 16) hit something none of OPEN-A
through D covers: **there is no way for one element (or one pixel) to read
a value from the *other* domain at its own specific position** — only the
aggregate `reduce`-through-`frame` path exists. A particle cannot ask "what
is the fluid field doing right where I am"; a pixel cannot ask "what color
is the nearest mesh point to me." Today, the only way to do this for real
is to fall back on the ordinary render pipeline (rasterize geometry to a
texture, sample a texture in a shader) — which works, but is a rendering
trick sitting outside Field's own domain-crossing vocabulary, not a Field
answer to the question. This should be written up as a fifth open
question and added to `design-brief-language-opens.md` rather than left
implicit in this device list — it's the one gap this exercise surfaced
that the original four didn't anticipate.

## Sourcing

Domain-crossing rules (`reduce`, implicit `broadcast`, the "no direct
element↔pixel/element↔sample/pixel↔sample" finding) are from
`.claude/skills/field-domains/SKILL.md` and its worked audio→geometry and
geometry→pixel examples. The `FieldGraphNode` bundling mechanism
(`emit`/`connect`, encapsulation, `.infdev` export) is real, merged code —
`src/core/field/FieldGraphKernel.h/.cpp`, `FieldGraphReconciler.h/.cpp`,
`src/nodes/FieldGraphNode.h` — confirmed by reading the source, not just the
step-10/15/16/17 plan docs. No worked example anywhere in the docs
currently chains all three domains in one script; devices 2, 6, 7, 11, 12,
18 above are original to this exercise, not transcribed from an existing
example.
