# Equation Oscillator — should-fix prompt

Branch: `feature/equation-oscillator`. This is the fix list from the
`infinite-code-review` pass on this branch (2026-08-21) — the "should fix"
items only, skipping the "consider" judgment calls. Fix these before merging
to `main`.

Read `.claude/skills/new-audio-node/SKILL.md` first — its four invariants
(clean room, two-object rule, `CookIfNeeded` does no DSP, audio-thread
prohibitions) apply to every fix below.

---

## 1. Audio-thread allocation in `ProcessBlock`

`src/nodes/EquationNode.cpp`, inside `AudioEquationNode::ProcessBlock`:

```cpp
if (mMonoScope.size() < (size_t)numFrames)
   mMonoScope.resize(numFrames, 0.0f);
```

`std::vector::resize` can allocate, which is a flat audio-thread
prohibition. `PrepareToPlay` already sizes `mMonoScope` to
`max(maxBlockSize, 4096)`, so this branch shouldn't fire in practice — but
its presence means the code doesn't trust that contract.

**Fix:** delete the resize call from `ProcessBlock`. Trust `PrepareToPlay`'s
sizing (that's the whole point of the call). If you want a safety net, clamp
`numFrames` to `mMonoScope.size()` for the scope write only, don't grow the
buffer mid-block.

---

## 2. `RebuildBank()` running full-cost FFT synchronously on knob drag

`src/nodes/EquationNode.cpp`, `EquationNode::CookIfNeeded` → `RebuildBank()`
→ `EquationDsp::BuildBankFromAst`: 1024 AST evaluations plus 1 forward + 10
inverse 1024-point FFTs, run synchronously on the main thread every time
`formula`, `domainMode`, or any of knobA–D changes.

`CookIfNeeded`'s budget is <5µs. This is doing on the order of 10 FFTs of
N=1024 — realistically hundreds of µs to low-ms. ImGui knob widgets fire a
changed-value event on effectively every frame during a drag, so dragging
knob A/B/C/D continuously re-triggers the full rebuild every single frame
for the duration of the drag. Expect visible UI stutter while turning any
of the four function knobs.

**Fix:** pick one —
- Debounce: only call `RebuildBank()` after the drag ends (ImGui gives you
  `IsItemDeactivatedAfterEdit()`) rather than on every `IsItemEdited()`, or
  after a short idle timer once the value stops changing.
- Move the rebuild off the main thread onto a worker thread, handing the
  finished `EquationBank` back through the same `SampleSlotT` mechanism
  already used for the main→audio handoff (build a new bank on the worker,
  `Push()` it when done — the audio thread already knows how to receive
  this).

Prefer the debounce — it's a smaller change and the audio thread already
gets a stable table between edits either way.

---

## 3. `Radix2FFT` duplicated from `WaveTerrainDsp.h`

`src/audio/dsp/EquationDsp.h:622-705` (`struct Radix2FFT`) is a byte-for-byte
duplicate of `src/audio/dsp/WaveTerrainDsp.h:56-139` — same struct name,
same twiddle-table precompute, same bit-reversal loop, same comment. Two
copies of the same primitive that can silently drift apart is exactly what
`DspMath.h` exists to prevent.

**Fix:** factor `Radix2FFT` out into a shared location (either add it to
`DspMath.h` alongside the other shared primitives, or a new
`src/audio/dsp/Fft.h`) and have both `WaveTerrainDsp.h` and `EquationDsp.h`
include it instead of each defining their own copy.

---

## 4. Missing node help-table entry

`.claude/skills/new-audio-node/SKILL.md` §3.9 requires one sentence in the
node help table (`main.cpp` ~line 7780, inside the `DrawHelpWindow` module
reference data) — what the node does and the one non-obvious thing about
it. This branch's diff doesn't touch that table at all; it was skipped.

**Fix:** add an entry for Equation Synth. Suggested copy, adjust to match
the existing entries' voice:

> **Equation Synth** — graph a math expression `y = f(x, a, b, c, d)` and
> play it as a polyphonic oscillator. The equation is baked into a
> 10-level antialiased wavetable on edit, not evaluated per-sample — so
> turning the A–D knobs recomputes the table rather than modulating live.

---

## Also worth doing while in this code (not blocking, from the same review)

- Filter envelope (`filterAmount`, `filterAttack/Decay/Sustain/Release`) is
  wired into `VisitParams`, the mailbox, and `RenderVoice`'s cutoff
  modulation, but `DrawEquationBody` never draws a control for any of
  them — the whole filter-envelope path is currently unreachable from the
  UI and stuck at its `filterAmount = 0` default. Either add the controls
  (a filter envelope panel like the amp one, plus a `filterAmount` knob) or
  cut the dead params.
- Control count (~16-17 across function/voice/filter/envelope sections) is
  well past the Tier-1 ~8-control bar the `new-audio-node` skill sets,
  though `WavetableNode` is this codebase's accepted "big synth" precedent
  — worth a deliberate look rather than assuming size is fine by
  association, especially once the dead filter-envelope controls above are
  resolved one way or the other.
- `main.cpp`'s dev-harness smoke-test section replaced
  `gNodes.back()` with a hardcoded `gNodes[24]` to keep referencing the
  Sampler fixture after the new node's spawn call shifted indices. Fragile,
  but this is test-harness code, not product code — low priority.

---

Run `/run-infinite-hygiene` and the `audio-node-sweep` skill after these
land, since neither ran during the original review (branch wasn't
buildable from this session — fetched read-only via the GitHub API).
