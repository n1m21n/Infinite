# 07 — Particle system performance

Run after 04 (per-node CPU meter). 04's measurements may re-gate or cancel this
session — if the particle step is not actually hot at counts you use, the
correct outcome is to close this and say so.

`../research-implementation-map.md` §3.4 asked "is particle state GPU-resident?"
and parked the answer behind a profiler gate. Re-graded 2026-08-14: the map's
characterisation is **accurate** — CPU-resident — but it missed a straightforward
algorithmic defect that needs no GPU rewrite at all, and understated the step
cost.

**The algorithmic defect:** `Emit` linear-scans the entire array looking for a
dead slot, once per spawned particle (`src/nodes/SimulationNodes.cpp:140-148`).
That is O(rate × capacity) per substep. At the 50,000 cap it is quadratic-ish
work to spawn.

**The step cost:** `SimulationNodes.cpp:158-200`, single-threaded, no SIMD, and
run on the same fixed-step accumulator as Cloth (`:227-243`,
`kFixedStep = 1/120`, `kMaxCatchUp = 0.25` ⇒ up to **31 substeps in one frame**).
UI cap is 50,000 (`main.cpp:8960`, default 4,000). Worst case in a single frame:
50k × 31 × 3 `Noise3` calls with turbulence on.

Paste everything below into a fresh Claude Code session.

---

Optimise Infinite's particle system (/Users/namansoni/infinte) without changing
what it looks like.

Read `src/nodes/SimulationNodes.h:26-98` (`ParticleSystemNode`, state is
`std::vector<Particle> mParticles` at `:98`), `src/nodes/SimulationNodes.cpp:140-243`
(emit, step, accumulator), and the consumers that read the CPU vector via
`GetPointCloud()` (`SimulationNodes.h:48`): `src/core/NodeViewport.cpp:515`,
`src/nodes/GeometryOpNodes.cpp:528, 921`, `src/nodes/UtilityNodes.cpp:481`.

## Order of work — cheapest and safest first

1. **Measure first.** Use 04's per-node meter. Record baseline frame cost at
   4k (default), 20k, and 50k particles, with turbulence off and on. Every
   later claim in this session is measured against these numbers.
2. **Fix `Emit`'s linear scan** (`:140-148`) — a free-list or a compacted-live-prefix
   layout. Pure algorithmic win, no behaviour change, no threading, no SIMD.
   Re-measure.
3. **Bound the substep explosion.** 31 substeps of a 50k-particle step in one
   frame is a frame-time cliff, and it is *reached by recovering from a hitch* —
   so a hitch causes a bigger hitch. Consider a substep budget that degrades
   gracefully (fewer substeps at high counts) rather than a hard catch-up cap.
   Whatever you choose, the simulation must stay deterministic for a given
   frame sequence and must not visibly change at default settings. Note Cloth
   shares this accumulator pattern — do not change shared behaviour without
   saying so.
4. **Only if 1–3 leave it hot:** multithread or SIMD the step loop. The loop is
   embarrassingly parallel except for emission. Report the measured gain.
5. **Do not** move state to the GPU. That is the map's ⚪ item, gated on the
   *step* being the bottleneck after the above, and it would require rewriting
   every CPU consumer listed above. If you conclude it is genuinely warranted,
   write it up as a proposal — do not start it.

## Rules that override anything you infer

1. **Visual output must not change** at default settings. Same seed, same
   frame sequence, same particle positions. If a change is unavoidable
   (e.g. step 3's substep budget under extreme load), state exactly when it
   diverges and why that is acceptable.
2. Determinism is a feature: patches are saved and replayed, and the self-test
   fixtures assert on rendered output. A "faster" system that renders
   differently frame-to-frame is a regression.
3. Respect the revision-stamp invariant in `ARCHITECTURE.md` — a node must not
   report a new revision when nothing changed, or downstream caches thrash.
   `.claude/skills/geometry-transform-sweep/SKILL.md` covers the sweep that
   checks this.
4. If threading: no data races on `mParticles`, and no thread-per-frame
   creation. The audio thread is not involved here, but the frame thread has a
   budget too.
5. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. Before/after numbers at 4k / 20k / 50k, turbulence off and on, from 04's
   meter. No unmeasured optimisation claims.
2. Emission at 50k is no longer O(capacity) per spawned particle —
   demonstrated by measurement, not by inspection.
3. Rendered output at default settings is unchanged: verify with the existing
   fixtures (`INFINITE_DELETECRASHTEST` wires a Particle System into Render 3D
   and every Instance-on-Points pin, `main.cpp:16012`) plus a visual check.
4. `/run-infinite-hygiene` passes, including the geometry transform sweep.
5. State explicitly whether the step is *still* the bottleneck after your work,
   and therefore whether the map's §3.4 GPU-residency gate is now met. That
   answer, either way, is the input to any future session.
