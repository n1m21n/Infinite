# Field Language Decisions, Analysis & Presets Plan

**Status:** Decision Document & Architectural Reference  
**Scope:** Resolves `OPEN-A` through `OPEN-D` in `docs/plans/field/design-brief-language-opens.md`, provides trade-off analysis, capabilities mapping, and a production catalog of 18 presets across all Field nodes.

---

## 1. Trade-Off Analysis: Open Questions & Decisions

| Open Question | Recommended Solution | Devil's Advocate | Features for a User | Opportunities |
|---|---|---|---|---|
| **OPEN-A: Delay lines & state arrays**<br>*(Sample history vs. no-arrays rule)* | **Fixed-size `ring` cell:**<br>`state ring float buf[2048] = 0`<br>Access via `buf.read(delay)` (clamped) and `buf.write(x)`.<br>Defer tables to explicit `table` declarations. | Introduces dot-method syntax (`.read`/`.write`) and hidden cursor state into what was a purely imperative language. Users accustomed to C/JS will ask for raw indexing (`buf[k]`). | **Custom audio delays:** Flangers, chorus, tape echoes, pitch-period freeze/smears, comb filters, and high-order LPC resonators written directly in text. | Compiles directly to circular pointer increments with zero bounds-check overhead; makes Infinite's entire delay/reverb DSP stack user-hackable without crash or allocation risks. |
| **OPEN-B: Element neighbour reads**<br>*(Accessing `P.at(i-1)` without breaking SIMD)* | **Double-buffered previous-cook read:**<br>`P.at(i-1)` reads element $i-1$ from the *input/previous cook buffer* (clamped `[0, count-1]`).<br>Multi-pass relaxation runs as sub-step cooks. | Neighbour reads are 1 frame (~16 ms) stale. Stiff springs or tight cloth can feel slightly compliant unless sub-stepped, and inner loops cannot do Gauss-Seidel relaxation across particles in a single pass. | **Physical mesh & curve dynamics:** Differential growth (brain coral / ammonite folding), Verlet particle ropes, chains, cloth constraints, and curve smoothing. | Keeps element kernels 100% pure and data-parallel. Enables vectorization across AVX2/NEON and future offload to GPU compute shaders with zero thread barriers or race conditions. |
| **OPEN-C: Pixel offset reads & boundaries**<br>*(Sampling spatial neighbours + wrap/clamp)* | **Field call syntax + declaration boundary mode:**<br>`state float A = 1 [wrap]`<br>`A(uv + d)` performs texture fetch.<br>Bare `A` remains sugar for `A(uv)`. | Syntax blurs the line between function calls, variables, and texture samplers. High-tap stencils (e.g., 8 fetches/px at 1080p = 1B fetches/s) can easily saturate GPU memory bandwidth. | **Cellular automata & fluid dynamics:** Scriptable Reaction-Diffusion, Navier-Stokes fluid advection, motion trails, directional blurs, and Sobel/Laplacian edge filters. | Replaces monolithic C++ nodes (Feedback, Reaction Diffusion, Trails) with live-editable Field shaders. Surfaces memory & texture-fetch budgets directly on the node UI. |
| **OPEN-D (Audio): Second sample input**<br>*(LMS echo cancellation & vocoders)* | **Kernel-declared dynamic input pins:**<br>Declare `input sample float ref` (via Step 12/13 system) to spawn an additional audio input pin on the node. | Dynamic pins alter node topologies from text, meaning editing or commenting out a line can orphan or disconnect live canvas patch cables if not protected by refuse-on-break checks. | **Two-stream adaptive DSP:** Real two-input LMS echo cancellation, noise cancelers, sidechain duckers/compressors, and audio cross-synthesis vocoders. | Avoids awkward hacks like packing stereo into `vec2 in` or hardcoding magic keywords (`in2`); unifies audio multi-input with geometry and pixel multi-stream pin architectures. |
| **OPEN-D (Notes): Note/event input**<br>*(Pitch/gate awareness vs. 6th domain)* | **Reject 6th domain; expose events in `frame` & `sample`:**<br>Sample domain uses per-voice `freq`/`gate`.<br>Frame domain uses event streams (`noteOn`, `notePitch`, `noteVel`). | Frame-rate note processing is quantised to 60 Hz (~16.6 ms), lacking sub-millisecond, sample-accurate timestamping for rapid polyphonic chord bursts. | **Intelligent musical sequencers:** Generative Euclidean sequencers, scale/chord quantisers, Markov melody engines, and audio synths with full pitch/gate control. | Saves months of architectural overhead by avoiding a 6th domain backend, event priority queue, and scheduler; keeps the runtime strictly aligned to the transport clock. |
| **Rung-4 State Lifetime: Reset vs. Learning**<br>*(Seek/stop reset wiping learned user models)* | **Dedicated `persistent` storage class:**<br>`persistent float mu = 0.5`<br>Regular `state` resets on transport seek/stop; `persistent` survives timeline scrubs and loops. | Adds a second state concept. If a user accidentally marks an audio filter cell as `persistent`, scrubbing the timeline will result in dirty, un-cleared audio tails instead of deterministic playback. | **Living instruments & ergonomic knobs:** Knobs that adapt to preferred sweet spots, groove trackers that lock to live human swing, and adaptive gain stages that don't reset on scrub. | Infinite becomes the first creative tool with self-adapting DSP/visual ergonomics that learn from human gestures while preserving timeline determinism for regular effects. |
| **Rung-4 Scope: Patch vs. User storage**<br>*(Cross-patch learning vs. patch encapsulation)* | **Two-tier storage hierarchy:**<br>Patch-specific habits (groove, curve bias) serialize into `.inf`. Global habits (node co-occurrence) serialize into `~/.infinite/user_profile.json`. | Patch portability becomes subjective: sharing a project file with a collaborator will not carry the author's global profile habits, potentially causing subtle "runs differently for me" discrepancies. | **Workflow acceleration:** Predictive node browser recommendations (e.g., placing a Wavetable automatically suggests your favourite Filter, not alphabetical order). | Delivers native, privacy-preserving workflow intelligence running 100% locally with zero cloud dependencies or LLM overhead. |

---

## 2. Capability & Systems Matrix

| Capability | Domain | Rung | Primary OPEN Primitive | State Footprint | Real-Time Bound |
|---|---|---|---|---|---|
| **Markov Melody Engines** | `frame` | 4 | `table` + `noteOn` (`A`/`D`) | $580\text{ B}$ | 24 iterations/frame |
| **Intelligent Sequencers** | `frame` | 0–4 | `reduce.rms` + `persistent` | $12\text{ B}$ | Zero iteration / $O(1)$ |
| **Algorithmic Reverb & Comb** | `sample` | 1 | `ring` buffer (`A`) | $128\text{ KB}$ (8 voices) | $O(1)$ circular read/write |
| **Noise Cancellation (NLMS)** | `sample` | 3 | Dynamic pin (`D`) | $260\text{ B}$ (8 voices) | $\mu < 2.0$ (NLMS bound) |
| **Vocoders & Adaptive LPC** | `sample` | 2–3 | `ring` + Dynamic pin (`A`/`D`) | $1.03\text{ KB}$ (8 voices) | Stable inverse IIR poles |
| **Fluid Dynamics (Navier-Stokes)**| `pixel` | 2–3 | Offset call `prev(uv+d)` (`C`) | $16.8\text{ MB}$ (512²) | CFL condition: $v \cdot \Delta t < 1$ |
| **Geometry Dynamics (Verlet/PBD)**| `element` | 1–3 | `P.at(i-1)` previous cook (`B`) | $60\text{ KB}$ (5000 pts) | Stiffness $\le 1.0$ |
| **Sample Resynthesis & Freeze** | `sample` | 2–3 | `ring` buffer (`A`) | $64\text{ KB}$ (8 voices) | Clamped buffer read head |
| **Pixel Resynthesis & Advection** | `pixel` | 2–3 | `prev(uv+d)` + `[wrap]` (`C`) | $66.4\text{ MB}$ (1080p) | 8 texture fetches/pixel |
| **Patch Intelligence & Co-occur** | `graph` | 4 | Global profile + `table` | $111.6\text{ KB}$ | Runs only on cable drag |
| **Evolving Modulations & Chaos** | `frame` | 1 | Standard `state` | $8\text{ B}$ | Attractor basin param bounds |

---

## 3. Production Presets Catalog (18 Presets)

Every preset below strictly obeys Field's core syntax rules:
* **Bare names only, no `@` sigils**
* **Inferred rates, never declared**
* **Pure ASCII operators and standard clamps**
* **Bounded memory footprint and real-time safe execution**

```
Presets Catalog Overview:
├── Audio & Synthesizers (FieldSampleNode)
│   ├── 01. NLMS Adaptive Feedback Killer & De-Hummer
│   ├── 02. Chamberlin Multi-Tap SVF Filter
│   ├── 03. Tape Comb Reverb & Chorus
│   ├── 04. Infinite Pitch-Freeze & Formant Smear
│   ├── 05. Adaptive Formant Vocoder (LPC Cross-Synthesis)
│   └── 06. Polyphonic FM Bell Generator
├── Visuals, Textures & Shaders (FieldPixelNode)
│   ├── 07. Gray-Scott Reaction-Diffusion (Turing Coral)
│   ├── 08. Realtime Navier-Stokes Smoke & Vortex Curl
│   ├── 09. Optical Flow Video Slit-Scan & Smear
│   ├── 10. Phosphor CRT Persistence & Motion Trails
│   └── 11. Kinetic Moire Ripple Generator
├── Geometry & Physics (FieldElementNode)
│   ├── 12. Verlet Particle Rope & Cable Physics
│   ├── 13. Ammonite Differential Growth Surface
│   ├── 14. Audio-Reactive Bass Ribbon
│   └── 15. Predictive Particle Collision Bounce
└── Sequencers & Modulators (FieldGraphNode / Frame)
    ├── 16. Bresenham Euclidean Generative Polyrhythm
    ├── 17. Hénon Strange Attractor Modulation Pair
    └── 18. Phase-Locked Loop Live Tempo Tracker
```

---

### Group A: Audio & Synth Presets (`FieldSampleNode`)

#### Preset 01: `NLMS Adaptive Feedback Killer & De-Hummer`
* **Domain:** `sample` | **Rung:** 3 (Self-Correction) | **Category:** `Audio FX / Utility`
* **Description:** Tracks drifting 50/60 Hz ground hum or howling PA feedback and cancels it in under 15 ms using normalized least mean squares.
```
param float mu   = 0.3 [0.001, 1.9]
param float mode = 0 [0, 1]           # 0 = De-hummer (error), 1 = Tone isolator (filtered)
state float x1 = 0
state float x2 = 0
state float x3 = 0
state float x4 = 0
state float w1 = 0
state float w2 = 0
state float w3 = 0
state float w4 = 0
state float pw = 1

y   = w1 * x1 + w2 * x2 + w3 * x3 + w4 * x4
e   = in - y
pw  = lerp(pw, x1 * x1 + x2 * x2 + x3 * x3 + x4 * x4, 0.001)
g   = mu * e / (0.000001 + pw)
w1 += g * x1
w2 += g * x2
w3 += g * x3
w4 += g * x4
x4 = x3; x3 = x2; x2 = x1; x1 = in

out = lerp(e, y, mode)
```

#### Preset 02: `Chamberlin Multi-Tap SVF Filter`
* **Domain:** `sample` | **Rung:** 1 (Memory) | **Category:** `Audio Filter`
* **Description:** High-resonance analog synth state-variable filter providing lowpass, bandpass, and highpass outputs simultaneously.
```
param float cutoff = 1200 [40, 8000]
param float q      = 0.707 [0.05, 1.0]
param float mixLp  = 1.0 [0, 1]
param float mixBp  = 0.0 [0, 1]
param float mixHp  = 0.0 [0, 1]

state float lp = 0
state float bp = 0

f   = 2.0 * sin(3.141592 * cutoff / sr)
hp  = in - lp - q * bp
bp += f * hp
lp += f * bp

out = lp * mixLp + bp * mixBp + hp * mixHp
```

#### Preset 03: `Tape Comb Reverb & Flanger`
* **Domain:** `sample` | **Rung:** 1 (Memory) | **Category:** `Audio FX / Spatial`
* **Description:** Vintage metallic comb delay with variable high-frequency absorption in the feedback loop.
```
param float delay = 1200 [4, 4095]
param float fb    = 0.7 [0, 0.98]
param float damp  = 0.25 [0, 1]
state ring float buf[4096] = 0
state float lp = 0

r = buf.read(delay)
lp += (r - lp) * (1.0 - damp)
buf.write(in + lp * fb)
out = in * 0.5 + r * 0.5
```

#### Preset 04: `Infinite Pitch-Freeze & Formant Smear`
* **Domain:** `sample` | **Rung:** 2 (Prediction) | **Category:** `Audio FX / Resynth`
* **Description:** Single-pitch-period repetition that freezes incoming audio into an infinite, crystal-clear organ pad with zero grain jitter.
```
param float freeze = 0 [0, 1]
param float pitch  = 220 [40, 1200]
state ring float buf[2048] = 0

period = clamp(sr / pitch, 40, 2047)
src = in
if (freeze > 0.5) {
  src = buf.read(period)
}
buf.write(src)
out = src
```

#### Preset 05: `Adaptive Formant Vocoder (LPC Cross-Synthesis)`
* **Domain:** `sample` | **Rung:** 3 (Self-Correction) | **Category:** `Audio FX / Vocoder`
* **Description:** Adaptive Linear Predictive Coding analysis tracks incoming speech formants live and imposes them onto a carrier synth stream.
```
input sample float carrier
param float mu = 0.25 [0.01, 1.5]
state float x1 = 0; state float x2 = 0; state float x3 = 0; state float x4 = 0
state float w1 = 0; state float w2 = 0; state float w3 = 0; state float w4 = 0
state float y1 = 0; state float y2 = 0; state float y3 = 0; state float y4 = 0
state float pw = 1

# Analyze vocal tract:
predIn = w1 * x1 + w2 * x2 + w3 * x3 + w4 * x4
e = in - predIn
pw = lerp(pw, x1 * x1 + x2 * x2 + x3 * x3 + x4 * x4, 0.002)
g = mu * e / (0.00001 + pw)
w1 += g * x1; w2 += g * x2; w3 += g * x3; w4 += g * x4
x4 = x3; x3 = x2; x2 = x1; x1 = in

# Re-synthesize carrier:
synthPred = w1 * y1 + w2 * y2 + w3 * y3 + w4 * y4
out = carrier + synthPred
y4 = y3; y3 = y2; y2 = y1; y1 = out
```

#### Preset 06: `Polyphonic FM Bell Generator`
* **Domain:** `sample` | **Rung:** 0 (Pure Formula) | **Category:** `Audio Synth`
* **Description:** Standalone 2-operator FM synth voice utilizing sample domain generator mode (`freq`, `gate`).
```
param float modRatio = 2.76 [0.5, 8.0]
param float modIndex = 3.5 [0, 10.0]
state float pCarrier = 0
state float pMod = 0

pMod = mod(pMod + (freq * modRatio) / sr, 1.0)
modSig = sin(6.283185 * pMod) * modIndex

pCarrier = mod(pCarrier + (freq / sr) * (1.0 + modSig), 1.0)
out = sin(6.283185 * pCarrier) * gate
```

---

### Group B: Visuals & Shaders (`FieldPixelNode`)

#### Preset 07: `Gray-Scott Reaction-Diffusion (Turing Coral)`
* **Domain:** `pixel` | **Rung:** 3 (Self-Correction) | **Category:** `Pixel Sim`
* **Description:** Spatially-coupled reaction-diffusion equations generating mitotic organic spots, coral patterns, and labyrinthine textures.
```
param float feed = 0.054 [0.01, 0.09]
param float kill = 0.062 [0.03, 0.07]
state float A = 1.0 [wrap]
state float B = 0.0 [wrap]

d = 1.0 / res
lapA = A(uv + vec2(d.x, 0)) + A(uv - vec2(d.x, 0)) + A(uv + vec2(0, d.y)) + A(uv - vec2(0, d.y)) - 4.0 * A
lapB = B(uv + vec2(d.x, 0)) + B(uv - vec2(d.x, 0)) + B(uv + vec2(0, d.y)) + B(uv - vec2(0, d.y)) - 4.0 * B

r = A * B * B
A += (1.0 * lapA - r + feed * (1.0 - A))
B += (0.5 * lapB + r - (kill + feed) * B)

col = vec3(B * 2.5, B * 0.8, 1.0 - A)
```

#### Preset 08: `Realtime Navier-Stokes Smoke & Vortex Curl`
* **Domain:** `pixel` | **Rung:** 3 (Self-Correction) | **Category:** `Pixel Sim / Fluid`
* **Description:** Incompressible fluid mechanics with semi-Lagrangian advection and divergence-free pressure projection.
```
param float visc  = 0.0001 [0, 0.005]
param float decay = 0.995 [0.95, 1.0]
state vec2 vel = vec2(0, 0) [clamp]
state float prs = 0 [clamp]
state vec3 dye = vec3(0, 0, 0) [clamp]

d = 1.0 / res
v = vel(uv - vel * d)
div = (vel(uv + vec2(d.x, 0)).x - vel(uv - vec2(d.x, 0)).x + vel(uv + vec2(0, d.y)).y - vel(uv - vec2(0, d.y)).y) * 0.5
prs = (prs(uv + vec2(d.x, 0)) + prs(uv - vec2(d.x, 0)) + prs(uv + vec2(0, d.y)) + prs(uv - vec2(0, d.y)) - div) * 0.25

vel = (v - vec2(prs(uv + vec2(d.x, 0)) - prs(uv - vec2(d.x, 0)), prs(uv + vec2(0, d.y)) - prs(uv - vec2(0, d.y))) * 0.5) * decay
dye = mix(col, dye(uv - vel * d) * decay, 0.95)
col = dye
```

#### Preset 09: `Optical Flow Video Slit-Scan & Smear`
* **Domain:** `pixel` | **Rung:** 2 (Prediction) | **Category:** `Pixel FX / Motion`
* **Description:** Computes spatial and temporal image gradients to advect live video along its own motion trajectories.
```
param float flowScale = 1.5 [0, 5.0]
param float fade      = 0.97 [0.8, 1.0]
state vec3 prev = vec3(0, 0, 0) [clamp]
state vec2 flow = vec2(0, 0) [clamp]

d = 1.0 / res
gx = (prev(uv + vec2(d.x, 0)).r - prev(uv - vec2(d.x, 0)).r) * 0.5
gy = (prev(uv + vec2(0, d.y)).r - prev(uv - vec2(d.y)).r) * 0.5
gt = col.r - prev.r
den = gx * gx + gy * gy + 0.0002

flow = lerp(flow, vec2(-gt * gx / den, -gt * gy / den) * flowScale, 0.3)
prev = lerp(col, prev(uv - flow * d), fade)
col  = prev
```

#### Preset 10: `Phosphor CRT Persistence & Motion Trails`
* **Domain:** `pixel` | **Rung:** 1 (Memory) | **Category:** `Pixel FX / Retro`
* **Description:** Emulates authentic multi-decay RGB phosphor persistence of classic studio cathode-ray displays.
```
param float decayR = 0.92 [0.5, 0.99]
param float decayG = 0.88 [0.5, 0.99]
param float decayB = 0.70 [0.5, 0.99]
state vec3 phosphor = vec3(0, 0, 0) [clamp]

phosphor.r = max(col.r, phosphor.r * decayR)
phosphor.g = max(col.g, phosphor.g * decayG)
phosphor.b = max(col.b, phosphor.b * decayB)
col = phosphor
```

#### Preset 11: `Kinetic Moire Ripple Generator`
* **Domain:** `pixel` | **Rung:** 0 (Pure Formula) | **Category:** `Pixel Generator`
* **Description:** Multi-source radial interference wave generator with transport sync and zero memory overhead.
```
param float freq  = 18.0 [2, 60]
param float speed = 2.5 [0.1, 10]
param float rings = 8.0 [1, 20]

p1 = length(uv - vec2(0.35, 0.5))
p2 = length(uv - vec2(0.65, 0.5))
w1 = sin(p1 * freq * rings - t * speed)
w2 = sin(p2 * freq * rings + t * speed)
val = 0.5 + 0.25 * (w1 + w2)

col = vec3(val, val * 0.7, 1.0 - val)
```

---

### Group C: Geometry & Physics (`FieldElementNode`)

#### Preset 12: `Verlet Particle Rope & Cable Physics`
* **Domain:** `element` | **Rung:** 3 (Self-Correction) | **Category:** `Geometry / Physics`
* **Description:** Point-cloud rope and hanging cable simulation using Verlet integration and distance-constraint relaxation.
```
param float restDist = 0.08 [0.01, 0.5]
param float stiff    = 0.6 [0.1, 1.0]
param float gravity  = 9.8 [0, 30.0]
state vec3 prevP = vec3(0, 0, 0)

# Verlet Momentum:
cur = P
vel = (cur - prevP) * 0.98
P = cur + vel
P.y -= gravity * dt * dt
prevP = cur

# Anchor root vertex:
if (i == 0) {
  P = vec3(0, 1.0, 0)
} else {
  # Distance constraint to upstream segment:
  nbr = P.at(i - 1)
  delta = P - nbr
  len = max(length(delta), 0.0001)
  err = len - restDist
  P -= (delta / len) * err * stiff * 0.5
}
```

#### Preset 13: `Ammonite Differential Growth Surface`
* **Domain:** `element` | **Rung:** 1 (Memory) | **Category:** `Geometry / Organic`
* **Description:** Simulates morphogenetic curve expansion that folds into brain-coral and suture-line structures.
```
param float springK = 0.35 [0.05, 1.0]
param float repel   = 0.04 [0, 0.2]
attrib float age = 0

nbrA = P.at(i - 1)
nbrB = P.at(i + 1)
mid  = (nbrA + nbrB) * 0.5

# Chain cohesion:
P += (mid - P) * springK

# Perturbative outward buckle:
norm = vec3(-(nbrB.y - nbrA.y), nbrB.x - nbrA.x, 0)
P += normalize(norm + vec3(1e-5)) * repel * sin(t * 2.0 + float(i) * 0.1)

age += dt * 0.1
Cd = vec3(clamp(age, 0.0, 1.0), 0.3, 0.8)
```

#### Preset 14: `Audio-Reactive Bass Ribbon`
* **Domain:** `element` | **Rung:** 0 (Pure Formula) | **Category:** `Geometry / Audio-Reactive`
* **Description:** Low-frequency audio reduction from sample domain broadcast to mesh vertices to drive dynamic traveling wave displacements.
```
param float amp  = 0.8 [0, 3.0]
param float wave = 4.0 [0.5, 16.0]

# Audio crossing: sample -> frame (computed 1x/frame):
bass = reduce.rms(in, 20, 160)

# Broadcast to element mesh:
disp = sin(P.x * wave + t * 4.0) * bass * amp
P.y += disp
Cd = vec3(bass * 2.0, 0.4, 1.0 - bass)
```

#### Preset 15: `Predictive Particle Collision Bounce`
* **Domain:** `element` | **Rung:** 2 (Prediction) | **Category:** `Geometry / Simulation`
* **Description:** Forward trajectory prediction preventing particles from tunneling through collision planes at high velocities.
```
param float floorY = -1.0 [-3.0, 1.0]
param float bounce = 0.65 [0, 0.95]
state vec3 prevP = vec3(0, 0, 0)

cur = P
vel = cur - prevP
nextY = cur.y + vel.y

if (nextY < floorY) {
  vel.y = -vel.y * bounce
}
P = cur + vel
prevP = cur
```

---

### Group D: Sequencers & Modulators (`FieldGraphNode` / Frame)

#### Preset 16: `Bresenham Euclidean Generative Polyrhythm`
* **Domain:** `frame` | **Rung:** 0 (Pure Formula) | **Category:** `Sequencer / Rhythm`
* **Description:** Pure closed-form Bresenham Euclidean rhythm generator producing clave, tresillo, and custom polyrhythmic trigger streams.
```
param float bpm    = 124 [40, 220]
param float steps  = 16 [2, 32]
param float pulses = 5 [1, 32]
param float rotate = 0 [0, 16]

stepIndex = floor(mod(t * (bpm / 60.0) * 4.0 + rotate, steps))
gate = 0
if (mod(stepIndex * pulses, steps) < pulses) {
  gate = 1
}
pulseOut = gate
```

#### Preset 17: `Hénon Strange Attractor Modulation Pair`
* **Domain:** `frame` | **Rung:** 1 (Memory) | **Category:** `Modulation / Chaos`
* **Description:** Non-linear discrete chaotic attractor yielding dual organically correlated, non-repeating modulation signals.
```
param float a = 1.40 [1.1, 1.42]
param float b = 0.30 [0.1, 0.35]
param float speed = 1.0 [0.1, 5.0]
state float x = 0.1
state float y = 0.1

nx = 1.0 - a * x * x + y
y  = b * x
x  = nx

# Correlated outputs for modulating filter + pan:
modA = clamp(x * 0.7 + 0.5, 0.0, 1.0)
modB = clamp(y * 1.5 + 0.5, 0.0, 1.0)
```

#### Preset 18: `Phase-Locked Loop Live Drum Follower`
* **Domain:** `frame` | **Rung:** 3 (Self-Correction) | **Category:** `Modulation / Tempo`
* **Description:** 2nd-order critically damped PLL tempo tracker locking directly to live acoustic drum transients or audio feeds.
```
param float kp = 0.08 [0.01, 0.3]
param float ki = 0.0016 [0.0001, 0.02]    # Critically damped: ki ≈ kp^2 / 4
state float phase = 0
state float period = 0.5
state float prevFlux = 0

flux = reduce.rms(in, 40, 250)
onset = clamp((flux - prevFlux) * 8.0, 0.0, 1.0)
prevFlux = lerp(prevFlux, flux, 0.25)

phase += dt / period
if (phase >= 1.0) { phase -= 1.0 }

err = phase
if (err > 0.5) { err -= 1.0 }

if (onset > 0.5) {
  phase  -= kp * err
  period += ki * err * period
}

syncClock = 1.0 - phase
currentBpm = 60.0 / period
```
