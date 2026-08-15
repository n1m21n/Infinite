# Optimisation & hardening prompts — one file, one session

Each file is self-contained: paste it into a fresh Claude Code session.

## Why this directory exists separately from `../research-implementation-map.md`

That map was written **2026-08-12, before `src/audio/` existed** — it says so
itself ("There is no `src/audio/`, no `AudioEngine`, no `AudioNode` base class
yet"). Since then the whole audio arm shipped: engine, 16 effects, 7 note
nodes, Sampler. Its three 🔴 "mandatory before P1/P2" items were re-graded
against the code on 2026-08-14 and **all three are already satisfied**:

| Map item | Re-graded verdict |
|---|---|
| §1.1 RT-safety constraint list written down | **satisfied** — `src/audio/AudioNode.h:6-12` carries it and cites the map by name |
| §1.2 lock-free param path | **satisfied** — `ParamMailbox` is atomic latest-value-wins, no lock/alloc either side. `ParamMailbox.h:16-19` records that an earlier ring version was deliberately removed for breaking the single-consumer invariant |
| §2.1 audio/visual cook-rate decision | **satisfied and implemented** — `cook-rate-decision.md` chose TouchDesigner-style last-cooked-value; `main.cpp:24516-24534` (modulators) runs before `:24741` (cook), audio thread only reads atomics |
| §1.3 RTSan | **open, but blocked** — see 06 |
| §1.5 Pure Data patch-shape skim | **moot** — the node list is locked and shipped |
| §3.3 point distribution, §4.2 reaction-diffusion | **not work**, by the map's own ⚪ gate |

So the map is now mostly a *record of decisions already taken*. Its remaining
value is §3/§4's trigger-gated items, re-graded below.

**The re-grade also turned up seven defects the map never mentions.** Those,
not the map's leftovers, are what 01–03 fix. The map was looking for
speculative optimisations; the code had real bugs.

## Priority

Ordered for: *make what is shipped robust and professional; new nodes later.*
Not ordered for visual payoff — that was explicitly deprioritised.

| # | Session | Why here | Gate |
|---|---|---|---|
| 01 | Audio lifecycle correctness | Three real bugs. The worst is user-hittable in one gesture: load a patch, press Start Audio, and every node's mailbox is left unprepared because `RebuildAudioTopology` is never called after `Start()`. | none — do first |
| 02 | Device-change & sleep/wake recovery | **Zero** notification observers exist. Unplug headphones or close the lid and the engine goes silent with no recovery. This is the largest gap between "works on my desk" and "professional". | after 01 (shares the restart path) |
| 03 | `NoteEventQueue` SPSC violation | Latent, not yet live: the note-off overflow path has the *producer* writing the consumer-owned index — structurally the same bug `ParamMailbox` already removed for being wrong. Benign only while both ends share a thread, and the header says it exists in SPSC form for a future CoreMIDI producer. | none — independent, small |
| 04 | Per-node CPU meter | Measurement before optimisation. The map preaches "profile before touching" in four separate places but the engine only exposes one whole-graph number, so no such profiling is currently possible. Unblocks 07/08 and every future perf claim. | none |
| 05 | Audio architecture docs + staleness sweep | `ARCHITECTURE.md` has 21 lines on the node *library* and **zero** on `AudioEngine`, the audio thread, the topology swap, `ParamMailbox`/`MeterRing`, or the two-object rule. Two status tables are also wrong. Standards work. | any time |
| 06 | RTSan build mode | The one tool that would *prove* the RT-safety claims in `AudioNode.h` rather than asserting them. Value has grown 40× since the map filed it (0 audio nodes then, ~40 now). | **blocked on installing Homebrew LLVM** — read 06 before starting |
| 07 | Particle system performance | Real, measurable: `Emit` linear-scans the whole array per spawn, and the step can run 50k particles × 31 substeps × 3 noise calls single-threaded in one frame. | after 04 |
| 08 | Cloth → XPBD | Modern-standard correctness: stiffness currently depends on iteration count, which is the actual bug PBD has. `shapeRetention` has the same coupling. | after 04 |

01–05 are mutually independent and can run in parallel or any order, except
02 after 01.

## Deliberately parked, with reasons

Not laziness — each is parked on a stated condition, so a future session
checks the condition and moves on rather than re-deriving the judgement.

- **FFT ocean** (map §3.2). Parked as a *new node/mode*, not a fix. The map's
  evidence is out of date: `MeshOps::Ocean` (`src/core/Mesh.cpp:1410-1457`)
  already sums **1–8 Gerstner components** with 0.62 falloff, golden-angle
  rotation and per-component `sqrt(g/k)` deep-water dispersion — not the "one
  regular Gerstner wave" the map claims. Its *conclusion* survives (this is
  not a Phillips/JONSWAP spectrum), but the gap is much smaller than written,
  so the payoff is smaller too. Accelerate/vDSP is already linked and already
  runs a 1024-point real FFT (`Platform.mm:1461-1538`), so no new dependency
  when it is wanted. Adding a node conflicts with "new nodes can be added
  later".
- **GGX multi-scatter** (map §4.1). Parked as visual payoff. Verified real,
  and *worse* than the map says: the IBL path has no split-sum environment
  BRDF at all — no LUT, no analytic scale/bias, specular IBL is just
  `prefiltered * F` (`Geometry3DNodes.cpp:674-682`). So analytic and IBL need
  two different fixes, not one. Also note the map's "30-minute visual check"
  is not possible as-is: `INFINITE_3DTEST` sweeps metallic and roughness in
  *opposite* directions (`main.cpp:21954,21960`) and `INFINITE_ENVTEST`'s
  metal sphere is at roughness 0.03 (`:16106`), so no fixture can currently
  see the darkening. Budget a harness edit alongside.
- **Geometry Phase 5 — curves first-class** (`../../phase5-curves-first-class.md`).
  The only outstanding phase on that roadmap, and now cheaper than its doc
  assumes because Phase 2 landed. But it is three new nodes → "later".
  If picked up, two line references in that doc are stale: `DisconnectAllTo`
  is at `main.cpp:11571`, not `:5255`, and its "Plumbing" section tells you to
  add a `QueryNewLink` assignment block that Phase 2b deleted on purpose.
- **Remaining audio nodes** — Note Display (prompt already written at
  `../../audio/prompts/13-note-display.md`, never executed), Oscillator, Drum
  Sequencer, Resonator, Scope, Recorder, Shaper, Mod Recorder, unified Macro.
- **Missing per-effect DSP fixtures** — 11 of 16 effects have none (Drive,
  Stereo, Pitch Shifter, Chorus, Flanger, Phaser, Bitcrush, Transient Shaper,
  Stutter, Ring Mod, Formant Filter). Arguably belongs in the robustness tier;
  parked only because the generic sweeps already cover the param/teardown
  invariants and these would cover DSP *correctness*. Promote if a DSP
  regression ever ships.

## Convention

Same as `../../audio/prompts/`: after each session, `STATUS.md` records what
shipped; plan docs and this README track the plan and do not change. Opt-in
test modes are `getenv("INFINITE_…")` gates in `main.cpp` plus an entry in
`.claude/skills/run-infinite-hygiene/driver.sh`'s `TESTS` array — **not** CMake
flags. RTSan (06) is the one exception and cannot follow that pattern.
