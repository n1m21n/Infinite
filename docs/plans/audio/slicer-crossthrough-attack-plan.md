# Slicer: crossthrough toggle + per-slice attack

Branch: `feature/slicer-node`. All line numbers verified at HEAD `29666d7`.

## 0. Findings

### `decay` IS correctly per-slice — confirmed, no fix needed
Path: `CookIfNeeded` (SlicerNode.cpp:547) -> `PushParams` (:101-113) -> **per voice at note-on**
`StartVoice` (:359-397) snapshots `v.elapsed/v.tau/v.infinite` -> **per voice per sample**
`ProcessBlock` (:236-237) `if (!vo.infinite) g *= std::exp(-vo.elapsed / vo.tau);`
`elapsed/tau/infinite` are members of `struct Voice` (:293-311), 9 independent slots.
So decay is measured from each slice's own note-on. Correct as built.

### CORRECTION to the premise: the default ALREADY stops at the boundary
`decay = 5000.0f` and `kDecayInfinite = 4999.0f`, so out of the box a note-triggered
slice stops at its own next onset. The crossthrough the user observed comes from either
(a) moving the decay slider off the very top -- any value < 4999 flips `stopPos` to
`numFrames-1`, i.e. run to end of sample -- or (b) the `Audition` button, which is
`TriggerSlicePreview(-1)` -> `sliceIndex < 0` -> `startFrac=0, endFrac=1`, whole buffer
**by design**. DO NOT "fix" the audition path.

### LIVE BUG to fix as part of this work
`StartVoice` reads the **smoothed** mailbox value to make a **discrete** decision:
`v.infinite = decayMs >= kDecayInfinite;` where `decayMs = mMailbox.SmoothedValue(kDecayParam)`.
`SmoothedValue` both advances the one-pole and returns a mid-ramp value, so a note-on
landing a few ms after the user releases the decay slider at the top of its throw sees
e.g. 4931.0 and silently starts a NON-infinite voice. It also advances decay's smoother
one extra sample per note-on, out of phase with the per-sample loop.
**A detent must be read from the plain atomic `mDecay.load()`, never from the mailbox.**

## 1. Resolved semantics (Q A)

Today `Voice::infinite` controls THREE things at once: the envelope (:236), the boundary
(:254-266), and note-off eligibility (:433-444). Split it.

| control | owns | default |
|---|---|---|
| `crossthrough` (bool) | **the boundary** -- does playback stop at the next onset | **false** (user decision) |
| `decay` (float ms) | **only the envelope** -- how fast amplitude falls after attack | inf detent = hold at full level; says NOTHING about boundaries |

Rename `Voice::infinite` -> `Voice::noDecay` (envelope only), add `Voice::confine`
(boundary only, latched at note-on from `!crossthrough`).

The four combinations:
1. **cross OFF + decay inf** (new shipped default): attack ramp, full level, 3 ms raised-cosine
   fade completing exactly at the next onset. The classic tight chop.
2. **cross OFF + decay finite**: ends at whichever comes first -- envelope < 1e-4, or next onset.
3. **cross ON + decay inf**: plays full level through every later slice to end of sample.
   The only case a note-off can shorten (NoteOff stays keyed to `noDecay`).
4. **cross ON + decay finite**: one-shot with a tail over the rest of the break.

### speed < 1.0 tail vs confinement -- the resolution
Confinement governs when the ENVELOPE closes, and it is expressed in **read-head position,
not wall-clock**, so it already stretches correctly with speed: `vo.pos` advances by `rate`
(pitch * speed * srRatio), so at speed 0.5 the boundary arrives in twice the wall-clock time.
The 3 ms fade completes AT the boundary and no audio past `endPos` is ever read -- that is
what the existing `if (vo.pos + fadeOutSamples * rate >= stopPos) BeginFadeOut(vo)` does,
starting the fade early in POSITION. **Keep that exactly as-is** -- it is what makes "the
playhead never travels past the next slice marker" literally rather than approximately true.
The brief's "must read past the boundary" clause is now served by `crossthrough = ON`, which
is the explicit escape hatch. Amend the brief rather than leave it contradicting the code.

## 2. Widget choice (Q C): grid-aligned `ModCheckbox`, NOT a toggle button

`AudioToggleButton` (the Sampler `loop/rev/p-p` idiom) is rejected on two counts:
it never calls `RegisterDiscreteParam`, so those Sampler bools are **not modulatable and not
performance-matrix assignable**; and the strip is already ~364 px of 440.
`ModCheckbox` (main.cpp:2092-2143) calls `RegisterDiscreteParam(..., isBool=true, nullptr)`
then `DrawDiscreteParamPin(h, label, kParamWidth, ImGui::GetFrameHeight())` -- the
`controlHeight` arg places the dot at `(controlHeight - 12.0f) * 0.5f`, so **P2 is satisfied
with no per-node offset code**, and theming goes through the shared `PushCheckboxStyle` (P10).
Precedent for a positioned ModCheckbox inside an audio body: `DrawWavetableBody`
main.cpp:9539-9556 (explicit `SetCursorScreenPos` anchored to `gAudioContentX`).
P1's forbidden pattern is a checkbox trailing `row.End()` with no cell -- this is not that.

**Discrete params do not share `gParamCounter`**: allocated from `kDiscreteParamBase = 400`
and keyed by label hash (`gDiscreteSlotByLabel`, main.cpp:1172-1186, 2477), specifically so
adding one cannot shift any float ordinal. The checkbox costs nothing in renumbering.

## 3. Layout (Q D) -- reordering floats is FREE right now, confirmed

No hard-coded Slicer param index anywhere; `Patch::SaveParams/LoadParams`
(src/core/Patch.cpp:172,181) is a name->value pair list, not positional; fixture and sweep
set/enumerate by name. The node was introduced on this unmerged branch, so no saved patch
can reference it. Reorder now; never again after merge.

```
[ Load... | Record | Audition | re-slice ]      (unchanged)
[ waveform 140px full width ]                    (unchanged)
row 1:  slice by (dropdown)  |  onsets
row 2:  division (dropdown)  |  sensitivity
row 3:  pitch                |  finetune
row 4:  attack               |  decay        <-- envelope pair, adjacent
row 5:  speed                |  volume
row 6:  crossthrough (cbox)  |  (deliberately empty)
```
P3: left column of rows 1/2/6 is the non-slider control column. P5: rows 1/2 keep the
existing BeginDisabled grey-out-never-hide verbatim. P6: the ragged full-width `volume` row
is gone. 11 controls is odd so exactly one cell must be spare; far-right of the last row
beside a checkbox is P6's sanctioned position. P4 does not apply (Synths node, no `mix`).

Resulting float pin order: `0 onsets, 1 sensitivity, 2 pitch, 3 finetune, 4 attack,
5 decay, 6 speed, 7 volume`. Discrete (label-keyed, order-independent): slice by, division,
crossthrough.

## 4. `attack` spec (Q E)

```cpp
float attack = 0.0f;   // ms, 0..500; per-slice attack ramp
```
```cpp
AudioSlider("attack", &n->attack, 0.0f, 500.0f, "%.1f ms", halfW,
            SkewAttack100Taper::PosToValue, SkewAttack100Taper::ValueToPos);
```
Range 0..500 ms, default **0.0** (punchy drums, and bit-identical to today at the default).
Taper `SkewAttack100Taper` (main.cpp:1591-1594): centre 100 on 0..500 gives k=2.322,
12 o'clock = 100 ms, pos 0.1 = 2.4 ms. **Do not pair it with a 0..200 range** -- centre 100
there degenerates to linear.

**One ramp, never two.** The hidden 2 ms de-click fade exists: `kFadeInMs = 2.0f`
(SlicerNode.cpp:35), raised cosine over `fadeInTotal/fadeInLeft` (:231-235), set at :392-393.
Attack EXTENDS that same ramp:
```cpp
const float attackMs = mAttack.load(std::memory_order_relaxed);
const float rampMs   = std::max(kFadeInMs, attackMs);   // never two ramps
v.fadeInTotal = std::max(1, (int)(rampMs * 0.001f * (float)mSampleRate));
v.fadeInLeft  = v.fadeInTotal;
```
Keep `RaisedCosine` for the whole ramp. At `attack = 0` this is byte-for-byte today's
behaviour -- the property that guarantees no regression. No new Voice fields needed.
Note in comments: decay runs from note-on independently of attack, so a long attack against
a short decay peaks below unity (standard AD, correct); with crossthrough OFF an attack
longer than the slice is cut by the boundary fade (correct and expected).

## 5. Playhead (Q F): audio-side fix alone. **DO NOT add a draw-side clamp.**

`PublishSnapshot` (:446-464) writes `position = vo.pos / frames` for **active voices only**.
`DrawSlicerWaveform` (main.cpp:8493-8503) `continue`s on `amp < 0.002f`. The boundary fade
drives `vo.active = false` (:242-246), so the voice drops out of the snapshot and the
playhead disappears at the marker. A draw-side clamp would hide a still-advancing voice and
misreport DSP state -- forbidden.
Caveats (comment only, no code): the snapshot publishes at end of ProcessBlock so a position
can be one block stale, but by then the fade is done and amp is under the draw threshold;
`endPos` is latched at StartVoice, so dragging a marker mid-voice leaves that voice on its
old boundary until retrigger (pre-existing, out of scope).

## 6. Changes

### src/nodes/SlicerNode.h
- :137-150 reorder params to the new draw order; add
  `float attack = 0.0f;` and `bool crossthrough = false;`
- :148-150 rewrite the `kDecayInfinite` comment: above this the slider is at its no-decay
  detent, the slice holds at full level after its attack instead of decaying; it says nothing
  about where the slice stops -- that is `crossthrough`'s job.
- :44-64 class header: add a paragraph on the two-control split and the four combinations.

### src/nodes/SlicerNode.cpp
- :27-31 add `constexpr int kAttackParam = 5;`
- :81-85 `PrepareToPlay`: `mMailbox.SetImmediate(kAttackParam, mAttack.load(...));`
- :101-113 widen to `PushParams(pitch, finetune, speed, volume, attackMs, decayMs, crossthrough)`;
  store `mAttack`, `mCrossthrough`; `mMailbox.Push(kAttackParam, attackMs)`.
  **`crossthrough` is a plain `std::atomic<bool>`, not a mailbox param** -- a bool has no ramp.
- :293-311 `struct Voice`: `bool infinite = true;` -> `bool noDecay = true;` PLUS `bool confine = true;`
- :236-237 `if (!vo.noDecay) g *= std::exp(...)`
- :254-266 the boundary fix:
  `const double stopPos = vo.confine ? vo.endPos : (double)(numFrames - 1);`
  `else if (!vo.noDecay && std::exp(-(float)vo.elapsed / vo.tau) < 1.0e-4f) BeginFadeOut(vo);`
  Rewrite the comment to the confinement / speed<1.0 reconciliation above.
- :380, :390-393 `StartVoice`: read detent + attack from the **atomics**, latch confine:
  `const float decayMs = mDecay.load(...); const float attackMs = mAttack.load(...);`
  `v.noDecay = decayMs >= SlicerNode::kDecayInfinite;`
  `v.confine = !mCrossthrough.load(std::memory_order_relaxed);`
  `v.tau = std::max(1.0e-4f, (decayMs * 0.001f) / kDecayTauDivisor);`
  plus the rampMs fade-in from section 4.
- :435-443 `NoteOff`: `mVoices[v].infinite` -> `mVoices[v].noDecay`, update comment.
- :481-486 add `std::atomic<float> mAttack { 0.0f };` and `std::atomic<bool> mCrossthrough { false };`
- :517-533 `VisitParams`: add `v.Float("attack", attack);` and `v.Bool("crossthrough", crossthrough);`
- :543-547 `CookIfNeeded`: `attack = std::clamp(attack, 0.0f, 500.0f);` and call the widened PushParams.
- :898-919 **`ReloadFromPath` -- DO NOT MISS.** Save/restore `attack` and `crossthrough`
  around `LoadFile`, or copy/paste silently drops them.

### src/main.cpp
- :11079-11092 update `DrawSlicerBody`'s pillar comment (the full-width `volume` P6
  justification is gone; document the new row order, the deliberate empty far-right cell,
  and that discrete params are label-keyed so the checkbox costs no renumbering).
- :11208-11224 replace the four slider statements with the six-row layout. Checkbox row:
```cpp
{
   const float y = ImGui::GetCursorScreenPos().y;
   ImGui::SetCursorScreenPos(ImVec2(gAudioContentX, y));
   bool cross = n->crossthrough;
   if (ModCheckbox("crossthrough##slicerCrossthrough", &cross))
   {
      PushUndoCheckpoint();
      n->crossthrough = cross;
   }
   if (ImGui::IsItemHovered())
      SetAudioReadout("crossthrough",
                      n->crossthrough ? "slices run past their own boundary"
                                      : "each slice stops at the next onset");
}
```
  The `bool tmp` dance is required -- ModCheckbox's comment at :2107-2110 explains that
  modulator-driven flips are only reported through the return value.
- :11217-11221 decay comment + `decayFmt`: keep or change `"inf"` to `"hold"` (now more
  truthful); update the comment to the envelope-only meaning.
- :11224 delete `(void)gap;` if `gap` becomes unused.
- :24514 in-app node manual -- the sentence "decay at the top of its throw reads inf: the
  slice plays through to its next boundary and stops there instead of decaying" is **now
  false**. Replace with both controls and the four combinations in brief.
- :25122 short catalogue blurb -- mention crossthrough and the attack/decay pair.

### docs/plans/audio/slicer-node-prompt.md
- :85 param table row 7 -- retarget the inf detent; add `attack` and `crossthrough` rows.
- :147-149 the "Decay." paragraph -- the inf sentence must change.
- :183-184 the speed<1.0 clause -- amend per section 1's resolution.

## 7. Tests

### RunSlicerFixture (main.cpp:34702, header comment :34698-34701)
Refactor: section 2's `renderNote` lambda (:34794-34845) returns only a peak. Extract
`renderNoteBuf(note, frames, outL, attackMs, decayMs, crossthrough)`, keep `renderNote` as a
peak-only wrapper so section 2 is untouched. **Set node.attack/decay/crossthrough BEFORE the
`CookIfNeeded` that follows `PrepareToPlay`** -- they only reach audio via PushParams.

**Section 5 -- PRIMARY: crossthrough OFF confines.** note 36, crossthrough=false, decay=5000,
one ProcessBlock of 16384 frames. Boundary = slice 1 at 0.25 s = frame 11025.
- `peak([0, 11025-256))` > 0.05 -- it sounded.
- `peak([11025+256, 16384))` < 1.0e-4 -- **silent after its own next onset.**
  This is the assertion the whole change exists for.
- The +/-256-frame guard covers the 3 ms (132-frame) fade plus interpolation slop.

**Section 6 -- VARIANT: crossthrough ON keeps going.** Identical but crossthrough=true.
- `peak([11025+256, 16384))` > 0.05. The exact complement of section 5; proves the toggle works.

**Section 7 -- attack ramps. NEEDS ITS OWN SIGNAL.**
**Trap: do NOT reuse the existing fixture WAV** -- its bursts are `exp(-t*200)` enveloped
(:34718-34724), so at t=50 ms the source is already ~4.5e-5 and a 50 ms attack measures as a
false FAIL. Synthesize **1.0 s of constant-amplitude 220 Hz sine** to
`TmpPath("infinite_slicer_attack.wav")`, reusing the RIFF writer at :34726-34744. One slice.
- Render A: attack=50.0 (2205 frames), decay=5000, crossthrough=false, 16384 frames.
- Render B control: attack=0.0, else identical.
- Assert `rms(A,[0,441))` (first 10 ms) is at least **10x** smaller than `rms(A,[1764,2646))`
  (~40-60 ms) -- still climbing.
- Assert `rms(B,[0,441))` is within ~2x of `rms(B,[1764,2646))` -- control does not ramp.
- **Use RMS, not peak** -- a 220 Hz carrier's zero crossings make short-window peak noisy.
Update the fixture header comment to list the new coverage.

### .claude/skills/run-infinite-hygiene/audio-param-sweep-expected.txt
Grouped by node, checked in BOTH directions. `AudioParamSweep::Collector` does visit `Bool`
(main.cpp:35893-35896), so `crossthrough` WILL appear as a [FAIL] without its line.
**Append after line 438** (`Slicer|volume|...`), reusing that exact reason string, two lines:
`Slicer|attack|...` and `Slicer|crossthrough|...`.

The round-trip printed count (main.cpp:36474) goes 9 -> 11. Informational only -- driver.sh
only diffs [FAIL] lines and no baseline records "9". Noted so nobody chases it.

## 8. Traps
1. `SmoothedValue` for a detent -- live bug, must read atomics.
2. The default is ALREADY boundary-confined; don't chase a phantom.
3. `Audition` / `TriggerSlicePreview(-1)` must keep playing the whole buffer -- it does so
   naturally via endFrac=1.0, `confine` is a harmless no-op there. Don't special-case it away.
4. `ReloadFromPath` must save/restore both new params.
5. Reordering sliders shifts float pins -- free today, never again after merge.
6. `AudioToggleButton` is not modulatable.
7. Fixture section 7 must not reuse the burst WAV.
8. Do not clamp the playhead in DrawSlicerWaveform.
9. Cosmetic: `kDecayTauDivisor` comment (:46-49) describes the -60 dB constant (6.9) then
   uses 4.6. Optional tidy.
