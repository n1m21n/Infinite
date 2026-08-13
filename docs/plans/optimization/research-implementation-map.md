# Research → optimisation map

Implementation prep for the reading list compiled 2026-08-12 (see chat / the
`infinite_research_map` artifact). Each entry is graded against the *actual*
code, not the paper's abstract — several of these turned out to already be
implemented, partially implemented, or intentionally not worth doing here.
Written so a fresh Claude Code session can pick up any one section and act on
it without re-deriving this context, following the `docs/plans/*.md`
convention.

**How to use this file:** when the optimisation stage starts, don't work top
to bottom — each section below ends with a priority tag. Do the 🔴 items only
if their trigger condition is true (a real profiler hit, or a real audio
glitch), not speculatively.

---

## 1. Audio engine (P1, once the P0 spike's numbers land)

Current state: `docs/plans/audio/P0-feasibility-prompt.md` is the only audio
engine planning that exists. `Platform.mm`/`Platform.h` currently carry the
throwaway `AudioSpikeStart`/`Stop`/`GetStats` spike (uncommitted as of this
writing) — a single `AVAudioSourceNode` sine generator, not a real graph.
There is no `src/audio/`, no `AudioEngine`, no `AudioNode` base class yet.

### 1.1 Real-time safety rules (Bencina, "Real-time audio programming 101")
**What to do:** before writing `AudioEngine`/`AudioNode`, write down the
constraint list as a comment block at the top of the new header — no
allocation, no locks with unbounded wait, no syscalls, no unbounded loops —
in the render callback. This is exactly the constraint the P0 spike already
follows (`std::atomic`-only fields in `AudioSpikeHandle`,
`Platform.mm:1687-1703`); P1 just needs to keep following it once there's an
actual graph with dynamic add/remove instead of one hardcoded oscillator.
**Priority:** 🔴 mandatory, not optional — this is the constraint the whole
engine design has to satisfy, not an add-on.

### 1.2 Command queue between UI thread and audio thread (Bencina, lock-free/wait-free notes)
**What to do:** the param-change path (turn a knob → audio thread hears the
new value next block) needs a lock-free SPSC ring buffer, not a mutex. Model
it on the existing double-buffering pattern already used for inference
results in comparable JUCE-style architectures — a single
`std::atomic<T>`-guarded index swap, not a queue of messages, if the only
payload is "latest value wins" (true for most knob-driven params). Only reach
for an actual lock-free FIFO if events need to be delivered in order and none
dropped (e.g. MIDI note-on/off, not continuous params).
**Priority:** 🔴 mandatory for P1 — this is the actual hard part of the
engine, not a nice-to-have.

### 1.3 RealtimeSanitizer (RTSan) in the build (ADC 2024)
**What to do:** once `src/audio/` exists, add an `INFINITE_RTSAN` CMake
option that compiles the audio TU with `-fsanitize=realtime` (Clang/LLVM 19+)
and mark the actual render-block entry point(s) `[[clang::realtime]]` (or
LLVM's equivalent attribute — check current syntax at build time, it's been
in flux). Wire it as an opt-in CI-style check alongside
`run-infinite-hygiene`, not a default build flag — it slows compilation and
only exists to catch real-time violations before they cause an audible
glitch.
**Priority:** 🟡 do this once P1's `AudioEngine` exists, before P2 (node
types) — cheaper to catch a violation with zero audio nodes than with twenty.

### 1.4 Engine architecture reference (Bencina & Burk, PortAudio design)
**What to do:** nothing structural — Infinite is committing to
`AVAudioEngine`/`AVAudioSourceNode` per the P0 spike's own scope, not a
cross-platform abstraction. Keep this paper as background only, useful if a
future session revisits "should this go through AUHAL directly instead."
**Priority:** ⚪ reference only, no action.

### 1.5 Pure Data patch-shape study (MSR 2024, "Opening the Valve on Pure-Data")
**What to do:** before finalizing which audio node types ship in P2/P3, skim
this paper's findings on real patch structure (fan-out depth, feedback
prevalence, subpatch nesting) to sanity-check the initial node list against
what patch authors actually build, rather than guessing. This is a
five-minute input to a design decision, not an implementation task.
**Priority:** 🟡 do once, before P2's node list is locked in.

---

## 2. Node graph core (`main.cpp`'s cook-once-per-frame DAG)

### 2.1 Audio/visual cook-rate decoupling (Houdini vs. TouchDesigner split)
**What to do:** this is a design decision, not a code change yet. The visual
graph cooks in lockstep with the render frame (`gLastFrameMs`-driven, per
`ARCHITECTURE.md`). The audio graph will run on a separate real-time thread
at its own block rate (per the P0 spike). Decide explicitly, in the P1 audio
engine doc, whether an audio node reading a *visual* modulator value (an LFO,
say) reads the last-cooked visual value (TouchDesigner-style, simple, one
frame of latency) or triggers its own cook (Houdini-style, more correct,
much harder to make real-time-safe). The P0 spike sidesteps this by having no
audio nodes at all — P1 can't.
**Priority:** 🔴 must be decided before P2 (audio node types) starts, not
during — it changes the shape of the modulation-routing code.

### 2.2 FRP framing (Elliott & Hudak) as a sanity check
**What to do:** no code change. Useful only as a naming/semantics check when
P1's audio param system is designed — confirm "a modulator is a `Time ->
Value` function sampled once per cook" is actually the model in use
(it already is, informally, for the visual graph), so the audio side doesn't
accidentally invent a second, incompatible mental model for the same concept.
**Priority:** ⚪ reference only.

---

## 3. 3D geometry & simulation

### 3.1 Cloth solver — XPBD upgrade (Müller 2007 → Strain Based Dynamics 2014 → MGPBD 2025)
**Current state:** `ClothNode` (`src/nodes/SimulationNodes.h:120`) is
plain PBD — `iterations = 6` (`SimulationNodes.h:157`), distance
constraints solved directly on positions, no strain-based or XPBD
compliance term visible from the field list.
**Trigger condition:** only touch this if cloth visibly goes rubbery/loses
stiffness at low iteration counts, or if `iterations` has to be pushed high
enough to cost real frame time. Don't preemptively rewrite a working solver.
**What to do if triggered:** add an XPBD compliance parameter (`alpha`) to
each constraint instead of solving hard distance constraints — this alone
(no strain-based rewrite needed) makes stiffness independent of iteration
count and substep size, which is the actual bug PBD has. That's the 2007→now
gap; treat Strain Based Dynamics and MGPBD's multigrid solver as further
upgrades only if XPBD alone isn't enough.
**Priority:** 🟢 nice-to-have, gated on an actual visual complaint.

### 3.2 Ocean — single-Gerstner-wave vs. FFT wave spectrum (Tessendorf)
**Current state:** `OceanNode` (`src/nodes/OceanNode.cpp`) takes
`amplitude`/`wavelength`/`steepness` as single scalars into
`MeshOps::Ocean(...)` — one regular Gerstner wave, not a summed spectrum.
**What to do:** this is a genuinely new node/mode, not a bug fix — Tessendorf
FFT ocean layers many frequency bins (inverse FFT) for irregular, realistic
open water instead of one periodic ripple. Non-trivial: needs an FFT
implementation (or a small library) and a spectrum model (Phillips or
JONSWAP). Scope as its own phase (`phase7-fft-ocean.md`-shaped), not a tweak
to the existing node — the current single-wave `OceanNode` should stay as
the cheap/stylized option, same reasoning as Phase 6 kept `ToPoints` alongside
`DistributeOnFaces`.
**Priority:** 🟢 nice-to-have, visual-payoff item — do this if "the ocean
looks too regular" is an actual complaint, not preemptively.

### 3.3 Point distribution — current Poisson disk vs. progressive projection (CGIT 2018)
**Current state:** already implemented, and already close to the paper's
baseline approach. `DistributePointsOnFacesNode` (`PointDistributionNodes.cpp`)
already has a `Poisson Disk` method (line 25) built as: random sample by
triangle-area prefix sum, then reject within `minDistance` using a spatial
hash grid — per `docs/plans/phase6-point-distribution.md:39-43`. This is the
textbook "dart-throwing + spatial hash" method (closer to the *2010*
Parallel Poisson Disk paper's approach than the naive O(n²) version).
**What to do:** the 2018 progressive-sample-projection paper is a real
upgrade only at high point counts (its whole pitch is throughput at massive
density) or if resampling needs to happen live every frame rather than once
per parameter change. Since `DistributeOnFaces` is cached/revision-stamped
(cooks once per parameter change, not per frame — same pattern as every
other geometry node per `ARCHITECTURE.md`'s revision-stamp invariant), the
existing O(1)-per-candidate rejection sampling is very likely fast enough
already.
**Priority:** ⚪ skip unless profiling shows `DistributeOnFaces` rebuild time
is actually a bottleneck at real point counts users hit.

### 3.4 GPU-resident particle state (SIGGRAPH 2007 course notes)
**Current state:** need to check whether `SimulationNodes.h`'s particle
system keeps state CPU-side (likely, given it's a `std::vector`-backed
stateful node like `ClothNode`) vs. GPU-resident.
**What to do:** only relevant if particle count needs to scale past what a
CPU step-then-upload can sustain at frame rate. Given the README's existing
"instancing is one draw call" claim already handles the *draw* side
efficiently, the open question is only the *simulation* step cost. Profile
before touching — this is a rewrite of the whole particle step to live in a
compute shader, not a small change.
**Priority:** ⚪ skip unless profiling shows the particle *step* (not the
draw) is the bottleneck.

---

## 4. Rendering & shading

### 4.1 Cook-Torrance/GGX cross-check (SIGGRAPH 2025 shading course)
**Current state:** `Geometry3DNodes.cpp:484-492` implements
`distributionGGX` (Trowbridge-Reitz NDF) and `geometrySchlickGGX` (Schlick-GGX
geometry term), with a clearcoat layer (`ccD`/`ccG` at lines 658-659) already
present. This is a real, non-trivial Cook-Torrance implementation, not a
placeholder.
**What to do:** the standard gap in a from-scratch implementation like this
is **multi-scatter energy compensation** — at high roughness, single-scatter
GGX loses energy (rough metals look too dark) because it only accounts for
light bouncing off the microfacet once. Read the 2025 course's section on
this (it's revisited most years) and check whether
`Geometry3DNodes.cpp`'s roughness response darkens visibly at
`roughness` near 1.0 on a metallic surface — that's the tell. If so, the fix
is a compensation term (Fdez-Agüera's or Kulla/Conty's multi-scatter
approximation), not a structural rewrite.
**Priority:** 🟡 worth a 30-minute visual check (render a rough metal sphere,
look for darkening) before deciding whether this is worth fixing.

### 4.2 Reaction-Diffusion — GPU kernel throughput (2025 phase-field paper)
**Current state:** `ReactionDiffusionNode` (`FeedbackNodes.h:97`) has
`stepsPerFrame = 8.0f` (line 122) — already runs multiple steps per frame,
presumably as a shader pass loop.
**Trigger condition:** only relevant if `stepsPerFrame` has to be capped
below what looks good because it's costing frame budget. `INFINITE_SHOWCASE4`
already runs this node at `stepsPerFrame = 24` as the heaviest fixture in the
test harness (per the P0 audio doc's own description) — so there's already a
known-heavy baseline to compare against.
**What to do if triggered:** the 2025 paper's ~1000x speedup is against
*multi-threaded CPU*, not a naive GPU shader pass — Infinite's version is
presumably already a fragment-shader ping-pong, i.e. already GPU. The
realistic win here is compiling the reaction-diffusion step to avoid
redundant texture fetches/branches per step, not adopting a fundamentally
different compute model. Profile the actual shader before assuming there's a
1000x sitting on the table — there almost certainly isn't, that number is
against a CPU baseline Infinite never had.
**Priority:** ⚪ skip unless `INFINITE_SHOWCASE4`'s frame cost is a measured
problem.

---

## Summary — what's actually actionable now vs. later

| Item | Priority | Gate |
|---|---|---|
| Real-time safety rules for `AudioEngine` | 🔴 | P1 start |
| Lock-free param path (audio) | 🔴 | P1 start |
| Audio/visual cook-rate decision | 🔴 | before P2 (audio nodes) |
| RTSan build integration | 🟡 | once `src/audio/` exists |
| Pure Data patch-shape skim | 🟡 | before P2 node list is locked |
| GGX multi-scatter visual check | 🟡 | 30 min, any time |
| Cloth → XPBD | 🟢 | visible stiffness complaint |
| Ocean → FFT spectrum | 🟢 | visible "too regular" complaint |
| Point distribution → progressive projection | ⚪ | profiler shows it's slow |
| Particle GPU residency | ⚪ | profiler shows step cost, not draw cost |
| Reaction-diffusion kernel tuning | ⚪ | `SHOWCASE4` frame cost is a measured problem |
| PortAudio-style engine abstraction | ⚪ | reference only |
| FRP semantics | ⚪ | reference only |

Everything ⚪/🟢 is intentionally *not* queued as work — it's here so a
future session doesn't have to re-derive "is this worth doing" from scratch,
and can instead check the trigger condition and move on if it isn't met.
