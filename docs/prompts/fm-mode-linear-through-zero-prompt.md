# Fix: the Oscillator/Wavetable "fm" mode is inaudible next to "pm"

## Context (already verified — do not re-derive)

Both `OscillatorNode` and `WavetableNode` share one DSP object,
`AudioWavetableNode` in `src/nodes/WavetableSynthCore.h`. They expose a
paired control in `main.cpp` — a mode dropdown `{ "pm", "fm" }` plus an
"fm depth" knob (0..2):

- `src/main.cpp:7125-7128` — Wavetable (`wtFmMode`)
- `src/main.cpp:7348-7351` — Oscillator (`oscFmMode`)

These are the only two nodes in the repo with an "fm in" pin
(`WavetableNode.h:144`, `OscillatorNode.h:43`); no other module has an FM
depth control, so the scope of this fix is exactly those two nodes and the
one shared core.

The two modes are implemented at `src/nodes/WavetableSynthCore.h:367-368`:

```cpp
const float fmPhaseOffset  = (fmMode == 0) ? (extFm * sm.fmDepth) : 0.0f;
const float fmExpSemitones = (fmMode == 1) ? (extFm * sm.fmDepth * 12.0f) : 0.0f;
```

`fmPhaseOffset` is added to the read phase in `RenderEngine`
(`WavetableSynthCore.h:717`). `fmExpSemitones` is folded into the cents sum
in both the free-running path (line 390) and the note-driven path (line 447),
then exponentiated at lines 393 / 450.

**Nothing is missing and no earlier fix was lost.** `fmMode` is pushed from
`CookIfNeeded` every frame (`OscillatorNode.cpp:53`, `WavetableNode.cpp:40`),
reaches the audio thread (`RunFmModeDebugCheck`, `src/main.cpp:24433`), is
saved (`v.Int("fmMode", ...)`), and the exponential path measurably works. I
compiled `WavetableSynthCore.h` standalone against a synthetic FM input and
measured it. With a 2 Hz modulator at full scale, carrier 220 Hz, depth 2.0,
the instantaneous pitch sweeps **50 Hz → 860 Hz** — exactly the ±24 semitones
the code asks for, in both the free-running and note-driven paths.

**The real defect is that the two modes are not gain-matched, and the FM one
is the wrong algorithm.** Measured spectral centroid of the output, carrier
220 Hz, modulator a 220 Hz sine (1:1 ratio), same knob positions:

| depth | modulator at 1.0 (pm / fm) | modulator at 0.3 (pm / fm) |
|-------|---------------------------|----------------------------|
| 0.00  | 239 / 239 Hz              | 239 / 239 Hz               |
| 0.25  | 505 / 287 Hz              | 296 / 251 Hz               |
| 0.50  | 549 / 443 Hz              | 367 / 263 Hz               |
| 1.00  | 934 / 779 Hz              | 579 / 320 Hz               |
| 2.00  | 1723 / 1586 Hz            | 614 / 499 Hz               |

At a realistic modulator level (0.3 — a modulator oscillator with its volume
knob down, or one riding its own amp envelope) PM at depth 0.25 already
brightens the tone more than FM does at depth 2.0. That is the "fm does
nothing" the user hears, and it is inherent to the algorithm: PM's depth is
in *cycles* (depth 2 = ±2 cycles = ±12.6 rad of phase, a huge modulation
index), while exponential FM's index falls as `modulator_amplitude / f_mod`,
so it collapses toward nothing as soon as the modulator isn't full-scale or
isn't low-pitched. Exponential FM also detunes the carrier — the average
pitch drifts sharp with depth, because `2^x` is convex — so the note goes out
of tune before it gets bright.

Exponential FM is also not what a synth labelled "fm" is expected to do.
Every reference implementation (DX7, Serum, Massive X, Operator) means
**linear, through-zero FM** by "FM": the modulator adds a *frequency offset in
Hz*, proportional to the carrier frequency, not a pitch offset in semitones.
That is the mode this control should be selecting.

## What to change

### 1. Replace exponential FM with linear through-zero FM

In `src/nodes/WavetableSynthCore.h`, replace the `fmExpSemitones` term
(line 368) with a linear frequency offset expressed as a **multiplier on the
carrier frequency**, so it stays gain-matched to PM across the whole range of
carrier pitches:

- Keep `fmMode == 0` (PM) exactly as it is. It is correct and users rely on it.
- For `fmMode == 1`, compute a per-sample ratio `fmLinRatio = 1.0f + extFm *
  sm.fmDepth * kFmLinScale` and multiply the final carrier frequency by it,
  instead of routing anything through the `semitones` / `powf(2, …)` sum.
- Pick `kFmLinScale` so the two modes are roughly perceptually matched at the
  same knob position with a full-scale modulator — start at `4.0f` (depth 2
  → ±8× the carrier frequency, index comparable to PM's ±12.6 rad at a 1:1
  ratio) and check it against the centroid table above; the goal is that
  switching pm→fm at a fixed depth changes the *character* of the sound, not
  its brightness by an order of magnitude. Say in your final message what
  scale you settled on and what you measured.
- Delete `fmExpSemitones` from the cents sums at lines 390 and 447. Those
  lines should go back to `(sm.pitchBend) * 100.0f` (plus `v.bend * 100.0f`
  in the note-driven case) and the FM term should be applied *after* the
  `powf`, at lines 393 and 450, as `base * powf(...) * fmLinRatio`.

**Through-zero matters and is the reason for the ordering above.** The
existing clamp at lines 393/450 is `std::clamp(freq, 20.0f, sampleRate*0.45f)`.
A linear FM term will legitimately drive the instantaneous frequency negative
— that is what "through-zero" means, and clamping it to +20 Hz is exactly
what makes cheap FM implementations sound dull and asymmetric. Change the
clamp so the *magnitude* is bounded but the sign survives: clamp the
unmodulated carrier to `[20, sr*0.45]` as today, then apply `fmLinRatio` and
clamp the result to `[-sr*0.45, sr*0.45]`.

`RenderEngine` must then tolerate a negative `freq`. Today it early-returns
silence on `freq <= 0.0f` (`WavetableSynthCore.h:672-676`). Change that guard
to `if (freq == 0.0f)`, or drop it and rely on the phase wrap. Check the two
places downstream that assume a positive frequency:

- `const double inc = (double)voiceFreq / mSampleRate;` (line 710) — a
  negative `inc` runs the table backwards, which is correct through-zero
  behavior. `st.phase[u] -= floor(st.phase[u])` (line 725) already wraps
  correctly for negative values, but confirm it.
- `Wavetable::MipForPhaseInc(inc)` (line 712) — **this one almost certainly
  needs `std::abs(inc)`**. Read `src/audio/Wavetable.cpp` and check; a
  negative increment must select the same mip level as its positive twin, or
  a through-zero sweep will pick a nonsense mip and either alias or go
  silent every time the modulator crosses zero.
- The internal cross-mod operator's `opInc` at line 685 uses
  `freq * warpRatio`. Leave the operator tracking the *unmodulated* carrier
  frequency rather than the through-zero one — pass the pre-FM frequency in
  for that purpose. A sync/cross-mod operator whose own rate goes negative
  mid-cycle is a different (and much wilder) feature than what the "sync"
  knob currently promises, and the Oscillator hardwires
  `warpMode = kWarpSync, warpAmount = 1.0` (`OscillatorNode.cpp:36-37`), so
  this path is live on every single Oscillator node — do not change its
  behavior as a side effect of this fix.

### 2. Relabel so the control reads honestly

In `src/main.cpp:7125` and `src/main.cpp:7348`, the dropdown entries stay
`{ "pm", "fm" }` — those are the right names once mode 1 is actually linear
FM. Update the two explanatory comments directly above them (7122-7124 and
7346-7347), which currently describe mode 1 as "exponential FM
(pitch-tracking, can self-modulate harder)". That description will be wrong
after this change. Say instead that mode 1 is linear through-zero FM, that
its depth is a multiple of the carrier frequency, and that PM's depth is in
cycles of phase.

Also update the field comments that call mode 1 exponential:
`src/nodes/OscillatorNode.h:69`, `src/nodes/WavetableNode.h:177`,
`src/nodes/WavetableNode.h:104`, and the two "Exponential FM has no ceiling
of its own" comments at `WavetableSynthCore.h:391-392` and `448-449` (which
justify a clamp you are about to change).

### 3. Add a regression check that would have caught this

`RunFmModeDebugCheck` (`src/main.cpp:24433`) only proves `fmMode` reaches the
audio thread — it never renders a sample, which is why a mode that was
technically wired but musically inert passed everything. Extend it (or add a
sibling check called from the same site, `src/main.cpp:24940`) that actually
renders: build an `AudioWavetableNode`, push a full-scale sine into the
"fm in" slot, render one block in each mode at `fmDepth = 0` and
`fmDepth = 2`, and assert that **both** modes move the output measurably
versus their own depth-0 baseline — and by comparable amounts. A zero-crossing
count over the block is enough of a brightness proxy and needs no FFT; the
depth-2 crossing count should be at least several times the depth-0 count in
*both* modes. Print pass/fail in the same style as the surrounding checks and
fold the result into the existing return value.

My standalone harness (free-running and note-driven, both modes, depths
0/0.25/0.5/1/2) is the shape to copy; it linked against only
`src/audio/ParamMailbox.cpp`, `MeterRing.cpp`, `Wavetable.cpp`, and
`AudioVoice.cpp`, so this check adds no new dependencies to the test binary.

## Out of scope

- Don't touch the PM path. It works and is correctly scaled.
- Don't add an FM ratio/offset control, an FM source selector, or per-engine
  FM depth. Those are real features but they're a separate design pass.
- Don't change the "fm in" pin's mono-summing behavior
  (`WavetableSynthCore.h:355-364`) — summing L+R to mono at full strength is
  deliberate and documented.

## Definition of done

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` compiles clean.
2. The new render-based FM check passes, and the existing
   `RunFmModeDebugCheck` still passes.
3. Run `.claude/skills/audio-node-sweep/driver.sh` — this touches two audio
   nodes' param paths and the sweep is the cheap guard against a param that
   silently stops reaching the audio thread.
4. Report the `kFmLinScale` you chose and the before/after brightness numbers
   at modulator amplitude 0.3, depth 1.0 — the case that currently reads as
   "fm does nothing" (centroid 320 Hz today, versus 579 Hz for PM at the same
   settings). That gap is what this fix has to close.
5. Copy the built `Infinite.app` to `~/Desktop`.
