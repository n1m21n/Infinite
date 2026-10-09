# Cluster: easing, springs, IK, procedural animation

NOTE: indexopensource.com and github.com are blocked; candidates below were picked from my own knowledge plus
docs/plans/index-opensource-scan.md:27 (which lists "Spring-It-On" as Tier 2). They were NOT cross-checked against the index list.

## Projects read
- orangeduck/Spring-It-On (branch master): LICENSE fetched, "MIT License, Copyright (c) 2021 Daniel Holden". Read `common.h` (damper_exact, spring_damper_exact, critical_spring_damper_exact, simple/decay variants, halflife_to_lag) and listed `smoothing.c`/`springdamper.c` (not studied).
- warrenm/AHEasing (branch master): `UNLICENSE` fetched (Unlicense, public domain); `easing.c` header repeats it. (No LICENSE file; README grep for licence found nothing, the UNLICENSE file is the evidence.) Read `AHEasing/easing.c` function list: Linear + Quad/Cubic/Quartic/Quintic/Sine/Circular/Exponential/Elastic/Back/Bounce, each In/Out/InOut (31 functions, `AHFloat f(AHFloat p)`, p in 0..1).

## Skipped / not read
- IK (FABRIK / CCD) and boids/steering peers: not read. I did not verify the licence of any IK or boids repo, so none was opened. Infinite has no use for them yet (see below), so this is a deliberate low-priority skip, not a verified negative on the peers.
- Any GPL/AGPL project: none opened.

## What Infinite does today
- Modulators (all `INode + IModulator`, `src/nodes/ModulatorNodes.h`): LFO (shapes only Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold, `ModulatorNodes.cpp:17-19`), Constant, Random, Pattern, Math, Compare, RangeToRange, Smooth (h:267), CVRecorder, ModDepth, Envelope (h:413), CVToPitch, Null, Invert, ModCurve (h:576), AudioToCV.
- Smoothing today: `SmoothNode::Value01` (`ModulatorNodes.cpp:226-239`) is a one-pole `mLast += (target-mLast)*(1-amount)` advanced once per change of `Transport::Beats()`. The step is a fixed fraction per tick, not scaled by elapsed time, so response depends on frame rate (and on BPM since the memo key is beats). No velocity state, no overshoot. `CVToPitchNode` glide (`ModulatorNodes.cpp:510-530`) does it correctly: exp(-dt/tau) using `Transport::Seconds()` dt, with the same-tick memo.
- Easing: only cubic smoothstep `t*t*(3-2t)` inside Pattern (`ModulatorNodes.cpp:100,130`, `smoothSteps`). Envelope is purely linear segments (`ModulatorNodes.cpp:455-478`; attack/decay/release). `ModCurveNode` (h:576) applies a user-drawn Catmull-Rom `CurveShape` (`src/core/CurveShape.h:10`) as a transfer function; this is the only generic shaper, and it has no preset easing library.
- Field has `smoothstep`, `mix/lerp`, `clamp` (`src/core/field/ElementBackend.cpp:986-992`, `FieldGraphKernel.cpp:375-398`, `BackendRegister.cpp:143-171`). Searched Field/src for `easeIn|easing|elastic|bounce|overshoot` and found no easing library (only the Pattern smoothstep and particle `bounce` restitution in `SimulationNodes.h:199`).
- Physics: `ClothNode` is position-based dynamics (`SimulationNodes.h:119-125`, comment explicitly avoids spring forces); Particle System has gravity/turbulence/collision (`SimulationNodes.h:~95-110`).
- IK / boids / steering: grepped src and tests for `boid|flocking|inverse kin|fabrik|ccd|skeleton|bone`: only hit is an unrelated "skeleton entries" comment at `FieldElementNode.h:135`. No IK, no skeleton type (`Mesh` has no joints), no flocking node exists.

## What the peers do differently
- Spring-It-On `damper_exact` (`common.h:72-93`): halflife-parametrised exponential smoothing `lerp(x,g,1-2^(-dt/halflife))`, frame-rate independent, with `fast_negexp` polynomial for exp. Infinite's Smooth uses an `amount` with no time unit.
- `spring_damper_exact` (`common.h:243-305`): closed-form, unconditionally stable damped spring solving x,v for any dt, with critical / under-damped / over-damped branches; params are `frequency` and `halflife` (or damping ratio via `spring_damper_exact_ratio`). `critical_spring_damper_exact` (`common.h:366-385`) is the 3-line critically damped case, with `x_goal`, `v_goal`. This gives overshoot/wobble, which no Infinite modulator can make.
- AHEasing: 31 stateless p->p functions; trivial to table-drive.

## Concrete improvements

### Existing nodes
1. **Smooth: make it time-based, add Spring mode** (S-M). Fix frame-rate dependence by switching to halflife in seconds with dt from `Transport::Seconds()` (copy the CVToPitch dt/memo pattern, `ModulatorNodes.cpp:510-530`). Add a `mode` param (Slew / Critical / Spring) and `frequency`, `halflife` knobs; Spring mode stores x and v, uses the same-tick memo (new-modulator-node section 2) and clamps the final output to 0..1 (an under-damped spring overshoots; the clamp is required by the 0..1 contract, or expose raw overshoot only through ModDepth). Benefit: knobs and image params that follow with inertia and bounce; stable at any frame rate. Skill: new-modulator-node. Licence: MIT, near-port of the formulas needs the Daniel Holden MIT notice; the maths is also in his article, so reimplementing from the equations needs no attribution.
   - Risk: changing `amount` semantics breaks saved patches; keep `amount` as the legacy mode and add new keys to `VisitParams`. Float params are addressed by draw order (new-modulator-node section 5), so draw new floats after the existing ones, unconditionally.
2. **ModCurve / Pattern / LFO: add an easing preset table** (S). Add a "Shape" dropdown of Penner easings (Quad..Bounce, In/Out/InOut) to ModCurve (applied before/after the CurveShape, or as `mix` target), to Pattern's `smoothSteps` (currently only smoothstep, `ModulatorNodes.cpp:100,130`) and to Envelope segments (currently linear). Shapes are pure p->p functions, so idempotency is free. Benefit: snappier/bouncier motion without hand-drawing curves. Skill: new-modulator-node (+ node-param-audit for the new dropdown). Licence: AHEasing is Unlicense (no attribution needed); the formulae are also standard Penner equations.
3. **Field built-ins** (S): add `ease_*`/`spring` helper functions to Field (`ElementBackend.cpp:986`, `FieldGraphKernel.cpp:375`, the GLSL backend and VM). Skill: field-compiler, field-language, field-testing. Wait on this until the Modulator table exists, to share one implementation. Not verified: whether GLSL and sample backends need parity entries per function (read `field-compiler` first).

### New nodes
4. **Spring modulator** (M): a dedicated `Spring` modulator is arguably better than overloading Smooth if the class must stay simple: inputs `in` + optional `velocity`, params frequency/halflife, outputs position and velocity (multi-output, new-modulator-node section 3). Benefit: velocity output is a new signal usable for squash/stretch. Skill: new-modulator-node. Licence: MIT/near-port.
5. **Easing / Tween modulator** (S-M): triggered one-shot tween (start/end, duration in beats, easing select, retrigger on gate input), a Pattern/Envelope relative. Skill: new-modulator-node.
6. **Spring geometry node** (L): apply spring-damper per-vertex or per-instance (follow-through, jiggle) on a Transform/Instance stream. Would need `IGeometrySource` plumbing (new-geometry-node) and a persistent state store; the Field `state` route (field-state) may be cheaper. Not scoped.
7. **IK / FABRIK / CCD chain node and boids/steering node** (L, low priority): no skeleton, no joint type, no existing use; would be a Field Modifier or a geometry operator. I did not read an IK or boids peer, so there is no code evidence here. A boids node is expressible as a Field Modifier with neighbour reads, so check field-domains first. Recommend deferring.

## Nothing-to-learn items
- Cloth/soft body: Infinite already uses PBD for stability (`SimulationNodes.h:119-125`); spring-damper exact solutions are for scalar/vec smoothing, not a replacement.

## Candidates
Spring/halflife mode for Smooth (time-based, fixes frame-rate dependence) | stable inertial/bouncy modulation; fixes a real fps-dependence | S-M | MIT (Spring-It-On, Daniel Holden; notice if near-port)
Easing preset table for ModCurve/Pattern/Envelope | 30 named easings on existing modulators | S | Unlicense (AHEasing)
Spring modulator with velocity output | new signal (position + velocity) | M | MIT
Tween/Easing trigger modulator | one-shot gated tweens | S-M | Unlicense
IK / boids nodes | no peer read, no existing infra | L | unverified, defer
