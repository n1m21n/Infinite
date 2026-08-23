# Molder node: drop the note pin, add a playback range + loop transport

Four changes to the Molder node — two requested features, one confirmed bug
with three distinct root causes, and two smaller bugs found while
investigating it (Synths category, currently on
branch `feature/molder-node`, files still untracked). Everything below was
verified against the code at the line numbers given — no exploration needed
before you start.

Files in play:
- `src/nodes/MolderNode.h` (196 lines)
- `src/nodes/MolderNode.cpp` (633 lines — `AudioMolderNode`, the audio-thread
  class, is lines 44–226; the main-thread `MolderNode` follows)
- `src/main.cpp` (~36.7k lines — `DrawMolderWaveform` at 8581,
  `DrawMolderBody` at 8661, node-manual text at 16879)

The close sibling for **both** changes is `SamplerNode` — it already has
exactly the start/end/loop/rev/ping-pong feature set you're adding, with a
draggable-handle waveform. Mirror it rather than inventing anything.
`src/nodes/SamplerNode.h:130-136`, `src/nodes/SamplerNode.cpp:52-80` and
`290-370`, and `DrawSamplerWaveform` at `src/main.cpp:5968`.

---

## 1. Remove the "notes" input pin

The pin is real and wired (it drives an 8-voice `VoiceAllocator` lane), but
it's unwanted — Molder is a roll-and-audition sound designer, not a playable
sampler. Rip out the whole note lane, don't just hide the pin.

**`MolderNode.h`:**
- Delete the `NoteInputSlot` override and the `NoteCable noteInput;` member.
- Change `AudioInputSlot` to answer **slot 0** instead of slot 1
  (`return slot == 0 ? &audioInput : nullptr;`).
- Change `InputLabel` to `return slot == 0 ? "record in" : nullptr;`.
- Drop `#include "core/NoteCable.h"`.

**`MolderNode.cpp`, inside `AudioMolderNode`:**
- Delete `SetNoteInbox()` override, `mNoteInbox`, `mNoteCursor`.
- Delete `VoiceAllocator mVoices`, `mVoicePos`, `mVoiceId`, the `mVoices`
  setup in the constructor and in `PrepareToPlay` (the `mVoices.SetSampleRate`
  / `SetADSR` pair), the `NoteEvent evts[64]` pop block, the per-sample
  `evtIdx` dispatch loop, and the whole `for (int v = 0; v < mVoices...)`
  voice-mixing loop. Only the `mSelfEnv` / `mSelfPos` self lane survives.
- **Line ~102, the record-input index:** the comment says "Slot 1 is this
  node's own audio input (record in) — slot 0 is the note pin's slot". After
  this change the audio input is slot 0, so it becomes
  `(numInputs > 0) ? inputs[0] : nullptr`. Update the comment too. This is
  load-bearing: `src/main.cpp:17500-17511` builds `inputBufferIndices[slot]`
  keyed directly on the `AudioInputSlot(slot)` index, so leaving it at
  `inputs[1]` silently breaks recording.
- **Line ~140:** the transport free-run gate is
  `if (transportPlaying && !mTransportWasPlaying && mNoteInbox == nullptr)`.
  Drop the `mNoteInbox == nullptr` clause — free-run is now unconditional.
- **Line ~199:** `const float active = (mSelfEnv.IsActive() || mVoices.IsVoiceActive(0)) ...`
  becomes just `mSelfEnv.IsActive()`.
- `#include "audio/AudioVoice.h"` is still needed (`Envelope` lives there),
  so keep it — only `VoiceAllocator` usage goes.

**No `main.cpp` edit is required for the pin count.** `InputCountFor`
(`src/main.cpp:2800-2805`) probes `AudioInputSlot`/`NoteInputSlot` generically
in a loop, so the node drops from two pins to one on its own.

**Known, accepted breakage:** links serialize as `(srcIndex, srcOutputIndex,
dstIndex, dstSlot)` — any already-saved patch with a cable into Molder's
slot-1 record input will fail to reconnect after this. The node is unreleased
and untracked, so that's fine; don't write a migration.

---

## 2. Add start / end / loop / reverse / ping-pong

### 2a. Params on `MolderNode` (`MolderNode.h`)

Add next to the existing eight public knobs, matching `SamplerNode.h:130-136`
verbatim in naming and defaults:

```cpp
float start = 0.0f;    // 0..1, left edge of the playback/loop range
float end = 1.0f;      // 0..1, right edge of the playback/loop range
bool loop = false;
bool reverse = false;  // plays start<-end instead of start->end
bool pingpong = false; // with loop on, bounces direction at each edge instead of wrapping
```

Add them to `VisitParams` (`MolderNode.cpp:432`) — `v.Float("start", start)`,
`v.Float("end", end)`, `v.Bool("loop", loop)`, `v.Bool("reverse", reverse)`,
`v.Bool("pingpong", pingpong)`.

**Critical:** do **not** add these to `DispatchSnapshot` (`MolderNode.h`, the
struct near the bottom). That snapshot exists to trigger a worker re-render
when a genome-affecting knob changes. These five are pure playback params —
they change nothing about the rendered buffer, and adding them there would
kick off a full STFT-and-additive-render every time someone drags a marker.

### 2b. Audio thread (`AudioMolderNode`)

- Add `std::atomic<float> mStart{0.0f}, mEnd{1.0f};` and
  `std::atomic<bool> mLoop{false}, mReverse{false}, mPingpong{false};`
  Plain atomics, no `ParamMailbox` — this matches how `mLevel` is already
  handled here, and `SamplerNode` also uses plain atomics for start/end/
  reverse (`SamplerNode.cpp:518-522`).
- Copy `AdvanceVoicePosition` from `src/nodes/SamplerNode.cpp:52-80` into
  Molder's anonymous namespace and use it for the self lane. It already
  handles all three modes plus the "clamp unconditionally, a handle drag can
  move the range under an in-flight voice" case. There is only one lane now,
  so no `speedSign` variation — pass `1.0f`.
- Add `int mSelfDir = 1;` and, per block, re-sync it from the live reverse
  toggle when ping-pong is off:
  `if (!pingpong) mSelfDir = reverseOn ? -1 : 1;`
  This exact line exists in `SamplerNode.cpp:336-338` and its comment
  explains why it's needed: without it, flipping `rev` mid-playback does
  nothing until retrigger, and turning `p-p` off after a bounce leaves the
  voice stuck playing backward forever.
- Replace the current self-lane advance (`mSelfPos += 1.0; if (mSelfPos >= endFrame - 1) mSelfEnv.NoteOff();`)
  with the `AdvanceVoicePosition` call; release only when it returns true.
- `TriggerPreview(frac)` currently sets `mSelfPos = frac * numFrames`.
  Clamp that into `[startPos, endPos]`.
- **Transport free-run start point:** `mSelfPos = 0.0` becomes
  `mSelfPos = startFrac * numFrames` (or `endPos` when reverse is on and
  ping-pong is off), so pressing space starts at the marker, not at zero.
- Playhead publishing already exists at the end of `ProcessBlock`. When
  nothing is sounding, park the published value on `mStart` rather than
  freezing at the last position — `SamplerNode.cpp:363-366` does exactly
  this and explains why.

Push the five values from `CookIfNeeded` (`MolderNode.cpp:384`) right next to
the existing `mAudioNode->mLevel.store(level, ...)` line. That's the
one-block-latency requirement the audio param sweep checks.

### 2c. UI (`src/main.cpp`)

`DrawMolderWaveform` (line 8581) already draws the waveform, the orange
playhead line, and a click-to-audition body button. Extend it to match
`DrawSamplerWaveform` (line 5968), which you should read in full first:

- Dim the regions outside `[start, end]` with the same
  `IM_COL32(0,0,0,130)` / light-theme `IM_COL32(255,255,255,140)` overlay.
- Draw green start / red end vertical marker lines plus the triangle grips
  at top and bottom (the grips are clamped a few px inboard so they stay
  grabbable at start=0 / end=1).
- Add the two 10px-wide `InvisibleButton` grab zones **after** the body
  button, with `PushUndoCheckpoint()` on `IsItemActivated()` and
  `ImGuiMouseCursor_ResizeEW` on hover/active.
- **The ordering trap, already documented at `src/main.cpp:5977-5982`:**
  ImGui overlap is *not* "last submitted wins". The body button must call
  `ImGui::SetNextItemAllowOverlap()` **before** it is submitted, or the
  handle buttons never receive mouse input at all. Molder's body button at
  8590 already does this — keep it.
- Change the body click-to-audition (line 8593) to Sampler's rule: a click
  outside `[start, end]` triggers from `n->start` rather than from the click
  point.

In `DrawMolderBody` (line 8661):
- Add the three `AudioToggleButton` toggles — `loop`, `rev`, `p-p` — mirroring
  `src/main.cpp:8402-8424`. Each wraps its write in `PushUndoCheckpoint()`.
  Right-justify the row off `gAudioContentX + gAudioContentW`, **not**
  `GetContentRegionAvail()` — the comment at 8390-8400 explains that
  anchoring off live avail overlaps the node-editor's resize hit-zone and
  makes the buttons read as resize drags. Put the row on the button line
  next to Load/Record/Roll/Iterate/Reset, or on its own line under it if
  that row is already full at `kAudioNodeWidth`; your call visually.
- Add numeric `start` / `end` sliders as a half-width pair, with the same
  cross-clamping as `src/main.cpp:8467-8471`. Put them after the existing
  `time`/`level` pair.

### 2d. Node manual text

Update the Molder entry at `src/main.cpp:16879` with one sentence covering
the new range + loop controls, and remove any implication that it accepts
notes. Also fix the now-stale class comment at the top of `AudioMolderNode`
(`MolderNode.cpp:41-43`), which currently reads "One-shot note playback …
No loop, no reverse" — both halves become false.

---

---

## 3. Reset doesn't reliably return to the original sample

Reported symptom: "in some cases the Reset button doesn't revert back to the
original sample." This is real, and it's **three separate defects** stacked
on the same button. `MolderNode::Reset()` is four lines
(`src/nodes/MolderNode.cpp`, just after `Roll()`):

```cpp
void MolderNode::Reset()
{
   mGeneration = 0;
   if (mAnalysis.valid && !mWorking.load(std::memory_order_relaxed))
      LaunchJob(Job::RenderOnly);
}
```

### 3a. Reset leaves the eight knobs where they are — confirmed, main cause

`BuildDesiredGenome()` (`MolderNode.cpp:246`) replays `mGeneration` mutations
from `mSeed` and then applies the knobs **on top**:

```cpp
g.tonalAmount *= 2.0f * tone;
g.noiseAmount *= 2.0f * air;
g.transientAmount *= 2.0f * snap;
g.harmonicStretch *= 0.8f + stretch * 0.4f;
g.inharmonicity += std::max(0.0f, (stretch - 0.5f)) * 3.0f;
g.attackScale *= powf(2.0f, -(time - 0.5f) * 2.0f);
g.decayScale  *= powf(2.0f,  (time - 0.5f) * 2.0f);
g.pitchShiftSemitones += pitch;
```

Each of those is neutral only at the spawn default (`tone/air/snap/stretch/
time = 0.5`, `pitch = 0`). So `generation == 0` is the unmutated genome, but
it is **not** the original sound unless every knob also happens to be neutral.
In the reported case the knobs read tone 0.28 / air 0.68 / snap 0.42 /
time 0.65 / pitch 1.8 st — Reset dutifully went to gen 0 and still sounded
nothing like the load.

**Fix:** `Reset()` should restore the knobs to their neutral defaults
alongside `mGeneration = 0` — `tone = air = snap = stretch = time = 0.5f;
pitch = 0.0f;`. Leave `chaos` and `level` alone: `chaos` only affects the
*next* roll and never the current render, and `level` is output gain, not
part of the genome. Say so in a comment so the asymmetry doesn't read as an
oversight.

### 3b. Reset is silently dropped when nothing else changed — confirmed

The `!mWorking` guard means a Reset pressed during an in-flight job does
nothing immediately. That usually self-heals, because `CookIfNeeded`'s
dirty-check (`MolderNode.cpp:412-430`) compares live values against
`mLastDispatched` and re-dispatches once the worker goes idle. But there is a
hole it cannot heal: **if `mGeneration` is already 0 and only the knobs are
off-neutral, the snapshot after 3a's knob restore does differ — good — but
without 3a it matches exactly and Reset is a total no-op.** Pressing a button
and having literally nothing happen is the sharpest form of the reported
symptom.

**Fix:** with 3a in place this mostly resolves itself, but make it explicit —
drop the `!mWorking` early-out from `Reset()` entirely and let the
`CookIfNeeded` dirty check own the dispatch. `LaunchJob` already no-ops on a
busy worker via its own `mWorking.exchange(true)` guard at the top, so calling
it unconditionally is safe; the state change (generation + knobs) must happen
regardless.

### 3c. `mSourceMono` is clobbered by *every* render, not just Iterate — confirmed

In `JoinWorkerIfDone()` (`MolderNode.cpp:~355`):

```cpp
mSourceMono = mPendingResult.rendered; // Iterate() re-analyzes whatever's currently rendered
mSourceSR = mPendingResult.renderedSR;
```

That runs on every completed job, including a plain `Job::RenderOnly` from a
Roll or a knob drag. The comment says it's for `Iterate()`, but nothing
restricts it to the analyze path. Consequences:

- The originally loaded audio is gone from the node after the first Roll.
- `Iterate()` therefore re-analyzes the *rolled* render even when the user
  never pressed Iterate before — the "eating the sound" behaviour starts one
  step earlier than documented.
- Once any `AnalyzeThenRender` lands, `mAnalysis` is replaced too
  (`JoinWorkerIfDone`'s `if (mPendingResult.didAnalyze)` branch), so
  generation 0 is permanently redefined as the mutated sound. After that,
  **no** amount of fixing 3a/3b can make Reset reach the original — the
  original analysis no longer exists anywhere in the node.

**Fix:** keep the original separate from the iteration feedback path.

1. Restrict the clobber to the case it was written for: only assign
   `mSourceMono`/`mSourceSR` from a render when the job that produced it was
   `Job::AnalyzeThenRender`, or better, have `Iterate()` pass the current
   render explicitly and stop `JoinWorkerIfDone` from writing `mSourceMono`
   at all. The second is cleaner — `Iterate()` already knows what it wants to
   re-analyze.
2. Add `MolderDsp::Analysis mOriginalAnalysis;` plus `std::vector<float>
   mOriginalMono; double mOriginalSR;`, captured once at the end of
   `LoadFile()` and `StopRecording()` (the two places that establish a genuine
   source). Have `Reset()` restore `mAnalysis`/`mSourceMono`/`mSourceSR` from
   them before dispatching. That makes Reset mean "back to the file as
   loaded", which is what the button is understood to promise and what the
   node manual at `src/main.cpp:16879` currently claims.

Note the memory cost is real but bounded — `Analysis` is a plain copyable
struct of vectors and `mAnalysis` is already copied by value into every
worker job launch, so one more retained copy is consistent with how the node
already handles it.

**Judgment call left to you, but here's my recommendation:** an alternative
reading is that Reset should mean only "generation 0, keep my knobs and keep
my iterations". I don't think that's right — Roll/RollBack already cover
walking the generation axis, so a Reset that doesn't undo Iterate has no
distinct job. Implement the full "back to the loaded file" semantics above.
If you disagree after reading the code, say so rather than silently picking
the other one.

### 3d. While you're in there

Update the node manual text at `src/main.cpp:16879` — it currently says
"Reset returns to generation 0, the unmutated analysis", which is a precise
description of the buggy behaviour. It should describe the fixed behaviour.

---

## 4. Two more bugs the same screenshot proves

Both were found while investigating #3. They are small, they are in the files
you're already editing, and #4a is very likely part of what the user
experienced as "Reset did nothing".

### 4a. `DrawMolderPartialBars` never advances the ImGui cursor — confirmed

`src/main.cpp:8630`. The function takes `ImGui::GetCursorScreenPos()`, draws
into the window draw list, and returns — it submits no ImGui item at all. No
`Dummy`, no `InvisibleButton`, no `ItemSize`. Compare `DrawMolderWaveform`
(8581), which submits `ImGui::InvisibleButton("##molderwavebody", ImVec2(w, h))`
and therefore does advance the cursor.

Result: everything drawn after it in `DrawMolderBody` — the
`ImGui::Dummy(0, 6)` and then the `seed / gen / f0 / harm` readout — is laid
out at the strip's own origin and renders **on top of the partial bars**.
That is exactly what the screenshot shows: "seed 3378985248 gen 0 f0 40.0 Hz
harm 100%" printed through the purple bar strip, unreadable.

**Fix:** add `ImGui::Dummy(ImVec2(w, h));` at the end of
`DrawMolderPartialBars` (after `AddRect`), with `ImGui::SetCursorScreenPos(origin)`
first if needed so the dummy occupies the strip's actual rect.

### 4b. Unpitched sources analyze to nothing and render silent — confirmed by evidence, needs a repro

Same screenshot: the loaded file is `Hi-Hat 02 - Jazz Vanguard.aif`, the
waveform pane is **completely blank**, and the readout says `f0 40.0 Hz`,
`harm 100%`, gen 0.

Reading those three together:
- 40.0 Hz on a hi-hat is YIN failing to find any periodicity and bottoming
  out at the low end of its search range.
- `harm 100%` is the `if (n == 0) return 100.0f;` early-out in
  `MolderNode::HarmonicityPercent()` — it means **every** partial had
  `envelopeAmp[h] < 1e-5f`, i.e. the analysis found no partial with any
  energy at all. It is reporting "perfectly harmonic" for "nothing to
  measure", which is its own small bug in that function.
- A blank waveform means `RebuildWaveformCache` ran over an all-zero buffer —
  the render produced silence.
- The partial-bar strip still shows full-height bars only because
  `DrawMolderPartialBars` normalises by `maxAmp` (8644-8646), so a row of
  near-zero amplitudes renders as a flat full-scale block. It is drawing
  noise as if it were signal.

So on an unpitched percussive source the whole chain degenerates: no f0 → no
partials → silent render → and then **every** button including Reset appears
to do nothing, because everything renders to silence.

**This one I have not reproduced**, only inferred from the screenshot and the
code. Do that first: load a hi-hat or other unpitched one-shot, and confirm
the rendered buffer is silent. Then decide the fix. Two candidates, in order
of preference:

1. The residual path should carry the sound when the tonal path finds
   nothing. `MolderDsp`'s sinusoids-plus-residual split means an unpitched
   source ought to end up almost entirely residual and still render
   correctly — if it doesn't, the bug is in the split or in how `tone`/`air`/
   `snap` scale a zero-partial analysis, and that's the real fix.
2. Failing that, detect the degenerate case (`numPartials == 0` or all
   `envelopeAmp` below threshold) and surface it honestly in `mStatus`
   ("unpitched - residual only" or "analysis found no partials") instead of
   silently rendering nothing.

Also fix `HarmonicityPercent()`'s `n == 0` case to return 0, not 100, and
have the UI show `--` rather than a fabricated `100%`.

**If reproduction shows the render is *not* silent** — i.e. the blank
waveform has some other cause — stop and report what you actually found
rather than implementing either fix above. This item is the one part of this
prompt that rests on inference rather than a confirmed read of the code.

## Verify before you call it done

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Must compile clean. Then run the audio sweeps, which cover exactly the two
things this change can break — a param that doesn't reach the audio thread,
and a pin/teardown regression from removing a cable slot:

```bash
.claude/skills/audio-node-sweep/driver.sh
```

The five new params should pass the param sweep cleanly since they take
effect within one block (unlike `chaos`/`pitch`/`tone`/etc., which route
through the worker re-render — if those already report as skipped or failing
in the sweep, that's pre-existing and out of scope here).

Finally, copy the built `Infinite.app` to `~/Desktop`.

## Out of scope

- Do not touch `MolderDsp.h` / `MolderDsp.cpp`. The analysis/genome/render
  engine is unaffected — this is purely playback and pin surface.
- Do not add a per-voice or polyphonic lane back in some other form. The
  point of change 1 is that Molder has exactly one self lane.
- Do not refactor `AdvanceVoicePosition` into a shared header used by both
  Sampler and Molder. Copying the ~30 lines is the right call for now;
  a shared DSP helper is a separate, larger change.
- On #4b: do not "fix" the silent render by rescaling or normalising the
  output buffer. If an unpitched source renders silent, find why the residual
  path is dropping it — a normalise would turn silence into amplified noise
  and hide the actual defect.
