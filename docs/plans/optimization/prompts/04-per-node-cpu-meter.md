# 04 — Per-node CPU meter

Measurement infrastructure. Do this before 07 and 08.

`../research-implementation-map.md` says "profile before touching" in four
separate places (§3.3, §3.4, §4.2, and the summary table's ⚪ gates) — but the
profiling it assumes is not possible today. `AudioEngine` exposes exactly one
number, `LastBlockLoad()`: `RunTopology` timed as a single unit
(`AudioEngine.cpp:188-199`), smoothed at `kLoadSmoothing = 0.2` (`:32`), stored
in `mLastBlockLoad` (`AudioEngine.h:147`), shown once in the status bar
(`main.cpp:17522`). There is no timing around the per-node `ProcessBlock` call
(`AudioEngine.cpp:141`), no cost field on `AudioTopologyEntry`
(`AudioEngine.h:27-33`) or `AudioNode`, and no equivalent for the visual graph's
per-node cook.

So "which node is expensive" is currently unanswerable, and every optimisation
decision — including 07's and 08's — is a guess.

Paste everything below into a fresh Claude Code session.

---

Add per-node cost measurement to Infinite (/Users/namansoni/infinte), for both
the audio graph and the visual cook.

Read `src/audio/AudioEngine.h/.cpp` (especially `RunTopology` and the
`AudioTopologyEntry` struct), the status-bar block at `src/main.cpp:17521-17548`,
and the cook loop at `src/main.cpp:24741-24742`.

## What to build

1. **Audio: per-node block cost.** Time each `entry.node->ProcessBlock(...)`
   (`AudioEngine.cpp:141`) and publish a smoothed per-node figure the main
   thread can read. This is the part with real constraints — see the rules.
2. **Visual: per-node cook cost.** Time each `gn.node->CookIfNeeded(frameId)`
   (`main.cpp:24741`) and keep a smoothed per-node figure. Cheaper to do
   correctly, since it is already main-thread.
3. **Surfacing.** At minimum: a way to see the worst offenders ranked, and a
   per-node readout. Follow `.claude/skills/audio-node-ui/SKILL.md` for
   anything drawn on an audio node's card; do not invent a new visual language
   for this. A node badge, a debug overlay panel, or both — decide and justify,
   but a number nobody can find is not "surfaced".
4. **Overhead honesty.** Measure and report the cost of the measurement itself.
   If per-node timing measurably raises total load, gate it behind a toggle
   that defaults off, and say what the measured overhead was.

## Rules that override anything you infer

1. The audio-thread constraint list (`src/audio/AudioNode.h:6-12`) is
   absolute. In particular: **no syscalls** in the render callback. Most
   obvious clock calls are syscalls or worse — establish what is actually
   safe here (`mach_absolute_time` / `std::chrono::steady_clock`'s backing on
   macOS) and write down why the one you chose is acceptable. Note that
   `AudioEngine.cpp:172-199` already takes timestamps inside the callback for
   the xrun and load figures, so a precedent exists — follow it rather than
   introducing a second timing mechanism.
2. Publish to the main thread the way everything else does: atomics or
   `MeterRing`, never a lock, never an allocation, never a growing container.
   Per-node storage must be preallocated with the topology (main thread), not
   on first use (audio thread).
3. Timing must not perturb the thing measured more than it reports. One
   timestamp pair per node per block, not per sample.
4. Do not change execution order or topology to make measurement easier.
5. Clean room: do not open, read, grep or reference /Users/namansoni/BespokeSynth.

## Exit criteria — report each explicitly, including any that did not pass

1. With a heavy patch running, the ranked view identifies the expensive nodes,
   and the per-node audio figures **sum to approximately `LastBlockLoad()`** —
   demonstrate this with real numbers. If they do not reconcile, the
   measurement is wrong; say so rather than shipping numbers that do not add up.
2. Validate against a known-heavy case rather than trusting the display: a
   Reverb (8-line FDN) must measure materially more expensive than a Gain, and
   a Particle System at 50k particles must dominate the visual cook.
3. Measured overhead of the instrumentation reported explicitly, with the
   default-on/default-off decision justified by that number.
4. `/run-infinite-hygiene` passes, including both audio sweeps — this touches
   `RunTopology` and the topology entry struct, which
   `AUDIOTEARDOWNSWEEPTEST` exercises directly.
5. `docs/plans/audio/STATUS.md` hardening line updated: per-node CPU meter
   moves from left to shipped.
6. Report the actual worst offenders you found. That list is the input to 07
   and 08 and may reorder them — if the numbers say the particle step or the
   cloth solver is *not* in fact hot, say so, and those sessions get re-gated.
