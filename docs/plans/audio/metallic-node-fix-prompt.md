# Fix prompt — Metallic node screams / produces high-frequency artefacts on knob movement

Paste everything below into a fresh Claude Code session.

---

## Context

`Metallic` is a physical-modelling synth node added recently (untracked files:
`src/nodes/MetallicNode.h`, `src/nodes/MetallicNode.cpp`,
`src/audio/dsp/MetallicResonator.h`; UI in `src/main.cpp` `DrawMetallicBody`,
registered at `src/main.cpp:2350`).

**Symptom:** moving almost any knob on the node — glide, width, transient,
stiffness, decay — makes it emit loud, unusually high-frequency screaming /
buzzing. Held notes drift to wrong pitches when glide is up. Note-off doesn't
silence anything cleanly.

All of the root causes below have been confirmed by reading the code. Line
numbers are from the current working tree.

---

## 1. The acoustics params are read once per *block*, not once per *sample* — this is the main bug

`ParamMailbox::SmoothedValue()` (`src/audio/ParamMailbox.cpp:17-23`) advances a
one-pole smoother **by exactly one sample** per call. Its time constant is 5 ms
(`ParamMailbox::PrepareToPlay`). The house pattern is to call it once per sample
inside the render loop — see `src/nodes/WavetableSynthCore.h:302-335`, where every
`SmoothedValue` call sits inside `for (int i = 0; i < buffer.numFrames; i++)`.

`AudioMetallicNode::ProcessBlock` breaks this. At
`src/nodes/MetallicNode.cpp:134-139` it reads six params **outside** the render loop:

```cpp
const float transientVal  = mMailbox.SmoothedValue(kParamTransient);
const float decayVal      = mMailbox.SmoothedValue(kParamDecay);
const float stiffnessVal  = mMailbox.SmoothedValue(kParamStiffness);
const float widthVal      = mMailbox.SmoothedValue(kParamWidth);
const float fineVal       = mMailbox.SmoothedValue(kParamFine);
const float glideSec      = mMailbox.SmoothedValue(kParamGlide);
```

(The four filter/drive/volume params at lines 251-254 *are* read per sample and
are fine.)

Consequences:

- The 5 ms smoothing becomes 5 ms × blockSize. At a 512-frame block that is a
  ~2.5 second ramp on every one of those knobs. A knob move stays "in motion"
  for seconds after the user let go.
- Worse: it feeds the retrigger detector at
  `src/nodes/MetallicNode.cpp:147-150`:

  ```cpp
  if (mLastTransient >= 0.0f && std::fabs(transientVal - mLastTransient) > 0.01f)
     transientChanged = true;
  ```

  Because the smoothed value now crawls toward the target over hundreds of
  blocks, `transientChanged` stays **true for every block for several seconds**.
  And `transientChanged` fully re-`Trigger()`s every active voice
  (`MetallicNode.cpp:190-194`) — re-firing `MalletExciter::Trigger` and its whole
  noise/pulse burst. A mallet strike every ~10 ms is a buzz at block rate with a
  very bright spectrum. In free-run mode (`MetallicNode.cpp:207-217`) it's worse:
  it **allocates and triggers a brand new voice every block** until the smoother
  settles, so all 8 voices restrike continuously.

**Fix:**

- Move the six `SmoothedValue` calls into the per-sample render loop, matching
  `WavetableSynthCore.h`. The values used for note allocation / `UpdateAcoustics`
  should be taken from that same per-sample smoother state.
- Restructure so that per-voice coefficient recomputation (`UpdateAcoustics`)
  happens on a **fixed control-rate subdivision** (e.g. every 32 samples, or once
  per block using values that have actually been advanced `numFrames` times) —
  not once per block using a smoother that only moved one sample.
- Replace the `transientChanged` re-trigger heuristic entirely. A synth must not
  restrike a voice because a knob moved. Transient should only be read at
  `Trigger()` time (note-on / Strike button / free-run timer). Delete
  `mLastTransient`, `transientChanged`, and the re-`Trigger()` block at lines
  190-194 and the `transientChanged` term at line 207.

---

## 2. Resonator coefficients are recomputed every block with no crossfade, and modes are allowed to sit at Nyquist

`MetallicVoice::UpdateAcoustics` (`src/audio/dsp/MetallicResonator.h:484-519`) is
called unconditionally every block for every active voice
(`MetallicNode.cpp:189` and `:224`). It calls `ResonantMode::Setup` for all 12
modes, which rewrites `a1`, `a2`, `gain` while `s1`/`s2` keep the state from the
previous frequency.

With `t60` up to 20 s the pole radius `r` is ~0.99995 — an extremely high-Q
resonator. Jumping such a filter's centre frequency once per block, with state
preserved and no gain crossfade, produces a discontinuity at every block boundary.
At ~94–187 blocks/sec that discontinuity train *is* the buzzing artefact, and it
is broadband, so it reads as "high frequency."

Two amplifiers of this:

- `ResonantMode::Setup` (`MetallicResonator.h:312-313`) clamps mode frequency to
  `0.495 * sampleRate`. Several materials put most of their 12 modes above
  Nyquist even at low fundamentals — `kVibraphone` has `modeRatios[11] = 150.0`,
  `kTitanium` has `74.0`. At 220 Hz that is 33 kHz. So 5-8 of the 12 modes all
  pile up clamped at ~21.8 kHz, each with `r ≈ 1`, all being re-kicked every
  block. That's a shrieking near-Nyquist chorus by construction.
- The `stiffness` knob multiplies this: `inharm = sqrtf(1.0f + effectiveStiffness
  * (m*m))` (`MetallicResonator.h:494`) with `m` up to 11 and
  `effectiveStiffness` up to 2.5 gives `inharm` up to ~17.4. Sweeping the
  stiffness knob therefore sweeps every mode across the whole spectrum and into
  the clamp — which, per the block-rate recompute above, is a screaming chirp.

**Fix:**

- In `ResonantMode::Setup`, if the requested frequency is above ~0.45 × sampleRate,
  **mute the mode** (`gain = 0`, and zero its state) rather than clamping it to
  Nyquist. Out-of-band partials should disappear, not stack up at the top.
- Only recompute mode coefficients when the inputs actually changed. Cache the
  last `(freqHz, decaySec, stiffness, material, stereoSpread)` tuple on the voice
  and early-out of `UpdateAcoustics` when it's unchanged within a small epsilon.
  With fix #1 in place this makes a settled patch completely static.
- When coefficients *do* change, ramp `gain` (and ideally `a1`) toward the new
  value over the control block instead of stepping, so a deliberate stiffness
  sweep glides rather than clicks.

---

## 3. `glide` is broken three separate ways

`mPitchSmoother` is a single `DspMath::OnePole` shared across all 8 voices
(`MetallicNode.cpp:361`).

- **It's stepped once per voice per block** (`MetallicNode.cpp:188`, inside the
  `for (int v...)` loop). So the glide rate depends on both block size and how
  many voices are sounding. With `SetTimeConstant(glideSec, mSampleRate)`, glide
  = 0.1 s gives coeff ≈ 0.99979 applied ~100× per second → the actual glide takes
  tens of seconds, and speeds up as you add notes.
- **It's shared, so it has one output for all voices.** Every held note is
  dragged toward the same smoothed pitch — a held chord collapses toward a single
  frequency. Glide must be per-voice state (add a `float glidedFreq` /
  `DspMath::OnePole pitchSmoother` to `MetallicVoice`, seeded with the note's
  target pitch on `Trigger`).
- **`mPitchSmoother.current` is never initialised** — `SetImmediate` is never
  called on it, so it starts at `0.0f`. The first time glide is turned up, every
  voice is retuned from ~0 Hz and slowly crawls up through the spectrum
  (mode frequencies bottom-clamped at 10 Hz, delay length clamped at 20 Hz),
  which is exactly the "moved the glide knob and it made a weird sweep" report.
  Also, freshly triggered notes are immediately re-tuned to this stale smoothed
  value on the very same block, because `Trigger` at `MetallicNode.cpp:165` is
  followed straight away by the `UpdateAcoustics` loop at `:182-196`.

**Fix:** move glide into `MetallicVoice` as per-voice state, seed it to the
note's own target frequency at `Trigger()` (so a new note starts *at* pitch, not
at 0), and advance it once per sample (or once per control block with a coeff
derived for that block length), not once per voice per block.

---

## 4. `voiceLevel` is computed but never applied to the output — voices never release and never free

In `MetallicVoice::Process` (`MetallicResonator.h:561-566`):

```cpp
voiceLevel *= ampDecay;
if (voiceLevel < 0.00005f && !exciter.active) { active = false; midiNote = -1; }
```

`voiceLevel` is only used as a shutoff test — the output at lines 558-559 is
never multiplied by it. So:

- `Release()` (`MetallicResonator.h:521-524`) sets `ampDecay = 0.995f`, which
  changes nothing audible; the voice keeps ringing at full level for ~2000 samples
  and is then **hard-cut**. That's a click on every note-off.
- With a long `decay`, `ampDecay = expf(-1/(decay*1.5*SR))` is so close to 1 that
  reaching 5e-5 takes minutes. Voices effectively never go inactive.

**Fix:** multiply the voice's contribution by `voiceLevel` (apply it to both
`modalL/modalR` and `wgSample` before summing into `outL/outR`), and give
`Release()` a proper release time constant instead of a magic `0.995f`.

---

## 5. Voice stealing reuses a voice without clearing it — this is the polyphonic screech

`AllocateVoice` (`MetallicNode.cpp:327-345`) returns the oldest voice when all 8
are busy, and `MetallicVoice::Trigger` (`MetallicResonator.h:463-482`) **does not
call `Reset()`**. So the stolen voice keeps:

- its 4096-sample `delayBuffer` full of the previous note's energy, and
- its 12 resonator states,

while `delayLength` is rewritten to the new note's period. A loud waveguide buffer
suddenly re-read at a much shorter delay, with `loopGain` up to 0.999
(`MetallicResonator.h:510`), is a bright pitched-up feedback squeal. Combined with
#4 (voices never free, so *every* new note steals), this fires constantly.

**Fix:** either call `Reset()` at the top of `Trigger()`, or — better, to avoid a
click — add a short "steal fade" (a few ms of forced `voiceLevel` ramp to zero
before reassigning). Clearing the delay buffer and mode states is the minimum.

---

## 6. Smaller things worth fixing in the same pass

- **`loopGain` has a 0.5 floor** (`MetallicResonator.h:510`:
  `std::clamp(loopLoss, 0.5f, 0.999f)`). Short decays can't actually damp the
  waveguide. Drop the floor to ~0.0 and let `effectiveDecay` control it.
- **`delayLength` bottom clamp is 2.0 samples** (`MetallicResonator.h:507`),
  allowing the waveguide to be tuned to ~SR/2. The mode path already clamps
  frequency to 20 Hz min via `std::max(20.0f, freqHz)` but there is no top guard.
  Clamp `delayLength` to a musically sane minimum (e.g. 8 samples).
- **No NaN / denormal guard anywhere.** Two nested feedback structures with
  runtime-modulated coefficients will eventually produce a non-finite sample.
  Add a finite check + reset per voice, and clamp the final `outL[i]`/`outR[i]`.
- **Free-run timer is only decremented in the `else` branch**
  (`MetallicNode.cpp:220`), so the branch that fires the strike never advances it
  — with #1 fixed this is mostly moot, but the logic should still be
  unconditional.
- **`MetallicNode::SetMaterialPreset` overwrites `transient`/`decay`/`stiffness`
  without a `PushUndoCheckpoint` inside itself** — the UI does it at
  `src/main.cpp:6606` so this is fine, but note that changing material silently
  discards the user's tweaks to three knobs. Consider only applying preset
  defaults on first selection, or leaving as-is if that's intentional. **This one
  is a judgment call, not a confirmed bug — ask before changing it.**

---

## Order of work

Do 1, 2, and 5 first — they are the ones producing the actual screaming. 3 and 4
are correctness/musicality bugs that will still be audible afterwards. 6 is
hardening.

## Out of scope

Do not restyle `DrawMetallicBody` or change the node's control set — the UI
layout and knob ranges (`src/main.cpp:6581-6693`) are fine and match the
`audio-node-ui` conventions. Do not touch `ParamMailbox` itself; its
one-sample-per-call contract is correct and other nodes depend on it — the bug is
that `MetallicNode` calls it at the wrong rate.

## Done criteria

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then run the audio node sweep, which covers this node's
param round-trip and teardown safety:

```bash
.claude/skills/audio-node-sweep/driver.sh
```

Manually verify in the app: sweep each of glide, width, stiffness, transient, and
decay across its full range, with 1 note held and with 8 notes held. No screaming,
no buzz at block rate, no clicks on note-off, and pitch stays where the notes were
played.
