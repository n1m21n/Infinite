# 08 — Cloth: PBD → XPBD

Run after 04. This is a **correctness/standards** change, not a speed change:
the goal is that stiffness means one thing regardless of the iteration count,
which is the actual defect PBD has and what XPBD (2016) exists to fix.

`../research-implementation-map.md` §3.1 gated this behind "cloth visibly goes
rubbery". Re-graded 2026-08-14: the defect is real and confirmed by inspection,
and **the code comment admits it outright** — `SimulationNodes.cpp:445-446` says
more iterations means a stiffer, less stretchy cloth, "this is the knob that
actually controls stiffness." So `stiffness` and `iterations` are two knobs for
one physical property, and neither is the property.

One correction to the map: it implies there is no substepping. There is —
`kFixedStep = 1/120` with a catch-up cap (`SimulationNodes.cpp:16, 21, 619-630`).
So effective stiffness is *already* timestep-independent; only the **iteration**
dependence is left, which narrows this job considerably. XPBD's compliance term
alone fixes it; the map's suggested further upgrades (Strain Based Dynamics,
MGPBD multigrid) are not needed and are out of scope.

Also in scope because it is the same bug class: `shapeRetention` is applied
*inside* the constraint loop (`:479-489`), so its pull also scales with
`iterations`.

Paste everything below into a fresh Claude Code session.

---

Convert Infinite's cloth solver from PBD to XPBD (/Users/namansoni/infinte), so
that stiffness is independent of iteration count.

Read `src/nodes/SimulationNodes.h:120-260` (`ClothNode`; defaults at `:155-159`,
state `mPos/mPrev/mRestPos/mPinned` at `:250-252`, serialized params at
`:210-211`) and `src/nodes/SimulationNodes.cpp:435-489` (Verlet integrate, then
the constraint loop) plus `:619-630` (the fixed-step accumulator).

Current solver, for reference — no compliance term, no Lagrange multiplier:

```
447: const int passes = std::max(1, std::min(iterations, 40));
448: const float k = std::max(0.0f, std::min(stiffness, 1.0f));
449: for (int pass = 0; pass < passes; pass++)
451:    for (const Constraint& c : mConstraints) {
460:       const float correction = (len - c.rest) / len * k;
```

Implement XPBD from the primary reference (Macklin, Müller & Chentanez, "XPBD:
Position-Based Simulation of Compliant Constrained Dynamics", 2016): a
per-constraint compliance α = compliance/Δt², a per-constraint Lagrange
multiplier accumulated across iterations within a substep and reset each
substep. Note the integrator is currently Verlet-style position differencing
(`:435-442`), not XPBD's predict/solve/update-velocity form — decide whether to
move to the XPBD form and justify it either way.

## The parameter decision — make it deliberately and write it down

`stiffness` (0..1) and `iterations` (1..40) are both **serialized params**
(`SimulationNodes.h:210-211`) and both on the UI (`main.cpp:8925-8931`:
pinned, stiffness, iterations, damping, mass, hold shape). Under XPBD,
`stiffness` should become compliance and `iterations` should demote to a pure
quality/convergence knob that does not change the look.

That means **saved patches will look different** unless you map the old
parameterisation onto the new one. Choose and justify:

- Reinterpret `stiffness` as compliance, accepting that existing patches change.
- Add a `compliance` param, keep `stiffness` as a legacy input mapped onto it.
- Version the node's serialization and convert old values on load.

Whichever you pick, the existing 15-node-plus patch fixtures must still load.
`.claude/skills/new-audio-node/SKILL.md`'s minimalism rule applies by analogy:
do not end up with three knobs for one physical property. Fewer, meaningful
controls beat a compatibility shim nobody understands.

Fix `shapeRetention`'s (`:479-489`) iteration coupling in the same pass, for the
same reason.

## Rules that override anything you infer

1. **Test the invariant, not the appearance.** The whole point: solve the same
   scene at `iterations` = 2, 6, 20, 40 and the converged rest shape must be
   materially the same. That is the assertion this session lives or dies by.
2. Do not regress stability. XPBD must not make the cloth explode at high
   compliance, low iterations, or under the 31-substep catch-up path.
3. Determinism: same seed and frame sequence → same result. Patches are saved
   and replayed and fixtures assert on rendered output.
4. Respect the revision-stamp invariant (`ARCHITECTURE.md`) — no new revision
   when nothing changed. See `.claude/skills/geometry-transform-sweep/SKILL.md`.
5. Do not implement Strain Based Dynamics or MGPBD multigrid. Out of scope; the
   compliance term alone is the fix.
6. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. A `getenv("INFINITE_CLOTHTEST")` fixture asserting iteration-independence
   (criterion 1 above) with a stated numeric tolerance. Follow the existing
   convention and register it in
   `.claude/skills/run-infinite-hygiene/driver.sh`'s `TESTS`.
2. That fixture **fails against the current PBD solver** — demonstrate it
   before the change. If it passes on unmodified code, the test is not measuring
   iteration dependence and must be fixed before proceeding.
3. Stability verified at the extremes: compliance min/max × iterations 1 and 40,
   plus a forced catch-up (drop a frame) — no explosion, no NaN.
4. The parameter/serialization decision documented in a comment at the class,
   with existing patches confirmed to load and a stated verdict on whether they
   look the same.
5. Cost reported from 04's meter, before and after. XPBD adds a multiplier per
   constraint; confirm it did not become the frame's hot spot.
6. `/run-infinite-hygiene` passes, including the geometry transform sweep.
