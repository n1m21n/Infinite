# Implement the `Slicer` audio node in Infinite

Repo: `/Users/namansoni/infinte` (note the spelling — no second `i`).
All paths below are relative to that root. All line numbers were verified
against the working tree at the time of writing; `src/main.cpp` is **52,899
lines**, so if a cited line has drifted, grep the quoted symbol rather than
trusting the number.

**Before you start:** the design brief this was written from said the work
was already on `feature/slicer-node`. It is not — the checkout is on `main`.
Create the branch first (`.claude/skills/git-branch-workflow/SKILL.md`):

```bash
git checkout -b feature/slicer-node
```

## Binding skills

Read these three in full before writing any code. They are prescriptive; do
not re-derive them.

- `.claude/skills/new-audio-node/SKILL.md` — the two-object rule, wiring
  sites, bug traps, exit criteria.
- `.claude/skills/audio-node-ui/SKILL.md` — body layout grammar and widgets.
- `.claude/skills/node-ui-pillars/SKILL.md` — P1–P11, the regression contract.

**Correction to those skills, verified:** they reference
`docs/plans/audio/README.md` §3 and `docs/plans/audio/audio-node-ui-system.md`.
**Neither file exists** — `docs/plans/` contains only `ideas.md`,
`test-tiering.md`, `undo-delete-perf-prompt.md` (and now this file). Do not
waste time hunting for them. This document plus the three skills plus the
sibling source files named below are the complete spec.

## Licensing / clean room — non-negotiable

Infinite is MIT. Implement the onset detector **from the cited primary
literature only**. Do not read, open, grep, reference or port:
`/Users/namansoni/BespokeSynth`, aubio, librosa, madmom, Vital, or any other
GPL/AGPL source. The DSP citations below are sufficient to implement from.

---

# 1. Goal

A **slicer**: load or record a sample, chop it into slices (either at
detected transients or on a tempo grid), and play each slice from a MIDI
keyboard, chromatically ascending from C1.

Registry / identity, all decided — do not renegotiate:

| Thing | Value |
|---|---|
| Registered type key | `Slicer` (stable patch-file key) |
| Category | `Synths` (one whitespace-free token — `Patch.cpp` reads `node <index> <category> <typeName>` with `>>`) |
| `DisplayName("Slicer")` | `"slicer"` |
| Body width | `kAudioNodeWidth` (440.0f, `src/main.cpp:243`) |
| Pins | `NoteInputSlot(0)` → `"notes"`, `AudioInputSlot(1)` → `"record in"` |
| Shape | `INode` + `IAudioSource` (audio out, note in, audio in) |

The pin layout is byte-for-byte the same as `SamplerNode.h:63-71`. Audio and
note pins **share one slot index space**; answering both from slot 0 is a
bug that silently zeroes the pin count.

---

# 2. Design spec

## 2.1 Visible controls — 7 params + 1 mode dropdown + a button strip

Draw order is load-bearing. `ModSlider` (`src/main.cpp:2147`, ordinal taken
at `:2152`) and `RegisterDiscreteParam` (`src/main.cpp:1877`) both allocate
`paramIndex = gParamCounter++`, so **the draw order in `DrawSlicerBody` IS
the modulation-pin numbering**. Append new params at the end forever; never
reorder, or existing patches' modulation cables silently rewire.

| # | Control | Widget | Range | Default | Taper |
|---|---|---|---|---|---|
| 1 | `slice by` | `AudioBareDropdown` | `onsets`, `grid` | `onsets` | — |
| 2a | `onsets` | `AudioSliderInt` | 1 .. 64 | 16 | linear |
| 2b | `division` | `AudioBareDropdown` | `1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32` | `1/16` | — |
| 3 | `sensitivity` | `AudioSlider` | 0 .. 100 `%.0f %%` | 65 | linear |
| 4 | `pitch` | `AudioSlider` | −24 .. +24 `%.1f st` | 0 | linear |
| 5 | `finetune` | `AudioSlider` | −100 .. +100 `%.0f c` | 0 | linear |
| 6 | `attack` | `AudioSlider` | 0 .. 500 ms `%.1f ms` | 0 | `SkewAttack100Taper` |
| 7 | `decay` | `AudioSlider` | 5 .. 5000 ms + no-decay detent (`hold`) | hold | `LogTaper` |
| 8 | `speed` | `AudioSlider` | 0.25 .. 4.0 `%.2fx` | 1.0 | exponential (linear in log2) |
| 9 | `volume` | `AudioSlider` | 0 .. 1 `%.2f` | 0.8 | linear |
| 10 | `crossthrough` | `ModCheckbox` | bool | off | — |

The `decay` detent is an **envelope** detent, not a boundary one: at the top of
its throw the slice holds at full level after its attack. Whether it may play
past its own next onset is `crossthrough`'s job and nothing else's.

Cells 2a/2b are the **same grid cell** (pillar P5): the cell index must not
move when `slice by` changes. Both are always drawn; the inactive one is
wrapped in `ImGui::BeginDisabled()`/`EndDisabled()` — **greyed out, never
hidden** (hiding shifts the row).

Signatures you need (verified):

```cpp
// src/main.cpp:7903
bool AudioSlider(const char* label, float* v, float lo, float hi, const char* fmt, float width,
                 FaderPosToValueFn posToValue = nullptr, FaderValueToPosFn valueToPos = nullptr);
// src/main.cpp:7909
bool AudioSliderInt(const char* label, int* v, int lo, int hi, float width);
// src/main.cpp:7901
float AudioHalfWidth();                       // (gAudioContentW - ItemSpacing.x) * 0.5f
// src/main.cpp:9236
void AudioBareDropdown(const char* id, const std::vector<std::string>& options, int current,
                       std::function<void(int)> onSelect, float width,
                       const std::vector<std::string>& categories = {});
// src/main.cpp:1564 — namespace LogTaper { PosToValue, ValueToPos }
```

The two-column grid is literally pairs of `AudioSlider(..., AudioHalfWidth())`
separated by `ImGui::SameLine()`. Copy the rhythm from `DrawSamplerBody`
(`src/main.cpp:10760-10925`, decay's LogTaper call at `:10921`) and
`DrawGranularBody` (`src/main.cpp:11594-11712`).

**Never** use a raw `ImGui::SliderFloat` — it gets no modulation dot. Always
`AudioSlider` / `AudioSliderInt` / `AudioKnobRow::Knob` / `AudioBareDropdown`.

## 2.2 Button strip

Same shape as `DrawSamplerBody`'s top bar: `Load...` (90px, via
`Platform::OpenAudioDialog()`), `Record`/`Stop` (70px), `Audition`/`Stop`
(90px), plus a new **`re-slice`** button that force-relaunches analysis with
the current settings. Each mutating button calls `PushUndoCheckpoint()` first.

## 2.3 Semantics

**Playback rate.**

```
rate = 2^((pitch + finetune/100) / 12) * speed * srRatio
srRatio = (activeSample->sampleRate > 0.0) ? (activeSample->sampleRate / mSampleRate) : 1.0
```

The `srRatio` term is verified at `src/nodes/GranularNode.cpp:311`.
`SamplerNode` omits it; that is a latent bug — **do not copy the omission**.

**Note → slice.** Slice *k* triggers on MIDI note `36 + k` (C1), chromatic
ascending. A note at or above `36 + sliceCount` produces **silence** — do not
wrap, do not clamp, do not fall back to slice 0.

The trigger hook already exists in the sibling:
`SamplerNode.cpp:471`
`void TriggerVoice(int note, float velocity, float overrideStartFrac, int voiceId, float bendSemitones)`.
The whole feature is `TriggerVoice(note, vel, sliceStartFrac[note - 36], voiceId, bend)`.

**Attack.** A per-slice fade-in that **extends** the hidden 2 ms de-click ramp
rather than stacking a second envelope on it — one raised cosine of
`max(2 ms, attack)`. At `attack = 0` the output is bit-identical to no attack
control at all.

**Decay.** Amplitude release measured from note-on, exponential `exp(-t/tau)`
with `tau = decaySeconds / 4.6`. At the top-of-range **no-decay detent** the
slice holds at full level after its attack instead of decaying; that says
nothing about where it stops. Decay runs from note-on independently of attack,
so a long attack against a short decay peaks below unity (standard AD).

**Crossthrough.** The boundary control, latched per voice at note-on. Off (the
default): the slice stops at its own next onset, with a 3 ms raised-cosine fade
that *completes* at the boundary, so no audio past it is ever read. On: the
slice runs to the end of the sample, straight through every later slice.

**Sensitivity vs. onsets.** `sensitivity` sets the onset-detection threshold
(which transients qualify as candidates). `onsets` then caps the kept set to
the **top-N by peak strength**, re-sorted ascending by time. Fewer candidates
than N → fewer slices, and that is fine. The **actual resulting slice count
must appear in the readout strip** (P11: `snprintf` into the strip, never into
a caption; the strip is never empty).

**Grid mode.** Slices at the **global transport BPM**, read from
`Transport::Instance().Tempo()` (`src/core/Transport.h:45`). Do not guess a
file BPM and do not add a per-node bpm param.

```
sliceLen = (60.0 / bpm) * (4.0 / denominator)     // seconds
triplet  => sliceLen *= 2.0/3.0
```

Denominators: `1/4`→4, `1/8`→8, `1/8T`→8×⅔, `1/16`→16, `1/16T`→16×⅔, `1/32`→32.

**Hard cap: 64 slices**, both modes.

## 2.4 Hidden constants — not user-visible params

| Constant | Value |
|---|---|
| Slice-boundary fade-in | 2 ms raised cosine, always on |
| Slice-boundary fade-out | 3 ms raised cosine, always on |
| Min inter-onset interval | 28 ms |
| Silence gate | −70 dB |
| Polyphony | 8 voices, steal oldest |
| Voice-steal crossfade | 2 ms |
| Max slices | 64 |

`speed < 1.0` needs no special case. Confinement is expressed in read-head
**position**, not wall-clock: `pos` advances by `rate` (pitch × speed × sr
ratio), so at speed 0.5 the boundary simply arrives in twice the wall-clock
time and the tail stretches with it. A tail that genuinely wants to run past
the boundary is what `crossthrough = on` is for — an explicit user choice, not
an implicit consequence of moving the decay slider.

---

# 3. Onset detection algorithm — `src/audio/dsp/SlicerDsp.h` / `.cpp`

New files, modelled on `src/audio/dsp/MolderDsp.h` (135 lines) for file
layout, abort discipline and literature-citation style. Built on the existing
`src/audio/dsp/PortableFft.h` — **no new dependency**. Confirmed absent from
the repo: kiss_fft, pffft, any existing `Onset`/`OnsetDetector` class, any
beat-grid code.

## 3.1 `PortableFft` API (verified, `src/audio/dsp/PortableFft.h`)

```cpp
PortableFft::RealFft fft;
fft.Prepare(log2MaxSize);                             // 2..20; call once, main/worker setup
fft.Forward(const float* samples, int log2N,          // N real in
            float* outReal, float* outImag);          // N/2 bins each, x2-scaled (vDSP zrip convention)
// outReal[0] = 2*DC, outImag[0] = 2*Nyquist, outReal[k]/outImag[k] = 2*X_k
PortableFft::HannWindowNorm(float* out, int n);       // Hann, denominator N
```

Because of the x2 scaling, normalise magnitudes consistently — e.g.
`mag[k] = sqrt(re[k]^2 + im[k]^2) * (2.0f / fftSize)`, the same normalisation
`AnalyzeNodes.cpp` applies after its `Forward` call.

## 3.2 STFT

Hann window **1024**, hop **256** at 44.1 kHz (→ ~172 Hz ODF frame rate).
Scale the hop with sample rate so the ODF rate is roughly constant:
`hop = round(256 * sr / 44100)`, `fftSize = 1024` (keep the FFT a power of two;
scale only the hop).

## 3.3 ODF — half-wave-rectified log-magnitude spectral flux (SuperFlux)

```
SF(n) = sum_k max(0, log(1 + c*|X(n,k)|) - log(1 + c*|X_ref(n-1,k)|))
```

with `c` a compression constant (use `c = 1000`, the standard log-magnitude
compression figure) and `X_ref(n-1,k) = max over the 3-bin neighbourhood
{k-1, k, k+1} of |X(n-1, ·)|` — the **maximum-filter trick** that suppresses
vibrato and pitch-glide false positives.

Citation to put in the header comment:

> Dixon, S. "Onset Detection Revisited", Proc. DAFx-06, Montreal, 2006 —
> spectral flux measured at F = 0.964 with 8.8 ms mean absolute error, the
> best-performing and cheapest of the ODF family evaluated there. The 3-bin
> maximum filter over the reference frame is Böck & Widmer's "Maximum Filter
> Vibrato Suppression for Onset Detection" (Proc. DAFx-13).

## 3.4 Peak picking (Dixon, DAFx-06)

1. Normalise the ODF to zero mean / unit standard deviation over the whole
   file.
2. Accept frame `n` iff **both**:
   - `f(n) >= f(k)` for all `n-w <= k <= n+w`, with `w = 3`;
   - `f(n) >= mean(f over [n-3w, n+w]) + delta`.
3. `delta` is what `sensitivity` maps to, exponentially:

```
delta = delta_max * pow(delta_min / delta_max, sensitivity / 100.0)
delta_min = 0.02        // sensitivity = 100 -> most sensitive
delta_max = 1.0         // sensitivity = 0   -> least sensitive
```

## 3.5 Post-processing, in this order

1. Enforce the **28 ms min-IOI** (keep the stronger peak of a colliding pair).
2. Reject onsets whose surrounding-frame RMS is below **−70 dB**.
3. **Backtrack** each accepted peak to the nearest preceding local minimum of
   short-term energy within **15 ms**.
4. **Snap** to the nearest zero crossing within **1 ms**.
   Steps 3–4 are what stop a kick slice starting late and losing its thump.
5. Always force a slice at `t = 0`.
6. Truncate to 64 slices (keep the strongest, then re-sort by time).

## 3.6 Signature

Mirror `MolderDsp::Analyze`'s shape exactly:

```cpp
namespace SlicerDsp
{
   struct Params
   {
      float sensitivity = 65.0f;   // 0..100
      double minIoiSeconds = 0.028;
      float silenceGateDb = -70.0f;
      int fftSize = 1024;
      int maxSlices = 64;
   };

   // Worker-thread only. Allocates freely during setup; allocation-free
   // inside the inner loops. Returns onset positions in FRAMES, ascending,
   // with 0 always present. Honours `abort`: if non-null and it becomes
   // true, returns early with whatever was completed.
   void Detect(const float* mono, int len, double sr, const Params& p,
               std::vector<int>& outOnsets, const std::atomic<bool>* abort = nullptr);
}
```

Return the per-onset peak strengths too (a parallel `std::vector<float>`, or
an out-struct) — the top-N prune in §2.3 needs them and must **not** re-run
`Detect`.

---

# 4. File-by-file implementation plan

## 4.1 `src/audio/dsp/SlicerDsp.h` / `SlicerDsp.cpp` (new)

Pure math. No `INode`, no ImGui, no GL, no threads of its own. Header comment
carries the citations from §3.3. Follow `MolderDsp.h`'s comment style.

## 4.2 `src/nodes/SlicerNode.h` / `SlicerNode.cpp` (new)

**Clone `src/nodes/SamplerNode.h` (178 lines) + `SamplerNode.cpp` (781 lines)**
— it is the only family member with both a notes pin and a record-in pin,
plus polyphony, a voice snapshot and an interactive waveform.

Structural facts verified in the sibling, all of which you must mirror:

- `class AudioSamplerNode : public AudioNode` is declared **entirely inside
  the `.cpp`** (`SamplerNode.cpp:87`); the header only forward-declares it
  (`SamplerNode.h:9`) for the `std::unique_ptr`, which is why the ctor/dtor
  are out-of-line (`SamplerNode.cpp:605-606`). Do exactly this:
  `class AudioSlicerNode : public AudioNode` inside `SlicerNode.cpp`.
- Param ids are file-scope `constexpr int kXParam` in an anonymous namespace
  (`SamplerNode.cpp:22-30`).
- `PrepareToPlay` must `mMailbox.SetImmediate(...)` **every** mailbox param
  (`SamplerNode.cpp:101-108`) or the first block ramps up from zero.
- Smoothed params go through `ParamMailbox::Push` and are read per block via
  `mMailbox.SmoothedValue(kXParam)`. Boundary-ish params (slice start/end
  fractions, reverse-like flags) go through plain `std::atomic` instead —
  `SamplerNode` deliberately sends start/end/reverse as atomics because you
  do not want a smoothed loop boundary. Slice boundaries are the same class:
  **atomics, not smoothed**.
- `CookIfNeeded` (`SamplerNode.cpp:609-627`) does **no DSP**: push params,
  `DrainRetired()`, drain meter rings, copy status flags. Budget < 5 µs.
- Audio-thread prohibitions inside `ProcessBlock` and everything it calls: no
  allocation, no locks, no `dynamic_cast`, no `std::function`/`map`/`string`,
  no GL, no ImGui, no file I/O, no `printf`.

**Buffer ownership.** `Platform::SampleBuffer` is `src/platform/Platform.h:353-359`
(`std::vector<float> channelData` — channel 0 then channel 1, each
`numFrames` long; `channels`, `numFrames`, `sampleRate`). Handoff goes through
`src/audio/SampleSlot.h`: main thread `Push()`, audio thread `SwapIn()` **only
at the top of `ProcessBlock`**, main thread `DrainRetired()` from
`CookIfNeeded`. **Never delete a buffer on the audio thread.**

**Load and record converge on one function.** `SamplerNode::FinishBuffer(...)`
(`SamplerNode.cpp:726-767`) builds the 256-bucket min/max waveform cache,
calls `PushBuffer`, sets status, and resets start/end/position. **This is
where the slicer kicks off onset analysis.** Both `LoadFile`
(`SamplerNode.cpp:709-724`) and `StopRecording` (`SamplerNode.cpp:680-707`)
route through it.

`StartRecording` must call `AudioTopologyRequest::Request()`
(`SamplerNode.cpp:676`) and `RequiresAudioProcessing()` must return
`IsRecording()` (`SamplerNode.h:96`) — otherwise `ProcessBlock` never runs
with no downstream cable and the record input captures nothing.

**Analysis runs on a worker thread.** Not in `CookIfNeeded` (< 5 µs budget)
and not on the audio thread. Copy `src/nodes/GrainMolderNode.h`'s pattern
wholesale (`GrainMolderNode.h:108-175`):

```cpp
enum class Job { None, NewSource, ReProcess };
void LaunchJob(Job job, ...);
void JoinWorkerIfDone();
std::thread mWorkerThread;
std::atomic<bool> mWorking{false}, mAbort{false}, mResultReady{false};
Job mCurrentJob = Job::None;
struct PendingResult { /* onsets + strengths */ };
struct DispatchSnapshot { float sensitivity = -1.0f; bool operator==(...) const; };
DispatchSnapshot mLastDispatched;
int mCooldownFrames = 0;   // debounce
```

Destructor sets `mAbort` and joins.

- `Job::NewSource` — on load / record-stop / `re-slice`.
- `Job::ReProcess` — on a **`sensitivity`** change only.
- `onsets` (the top-N cap), `slice by`, and `division` must **NOT** relaunch
  analysis. They are a cheap main-thread re-prune of the cached candidate
  list / a pure recompute of grid boundaries.

**Persistence.** `VisitParams` (see `SamplerNode.cpp:630-643` for the shape).
Names are stable patch-file keys — renaming one silently drops it from
existing patches.

```cpp
v.Text("path", mFilePath);
v.Int("sliceBy", sliceBy);
v.Int("onsets", onsets);
v.Int("division", division);
v.Float("sensitivity", sensitivity);
v.Float("pitch", pitch);
v.Float("finetune", finetune);
v.Float("speed", speed);
v.Float("decay", decay);
v.Float("volume", volume);
v.Text("slices", mSliceBlob);   // see below
```

`ParamVisitor` supports Float/Int/Bool/Text/Color only.

**Persist the detected slice markers as a Text blob** (`mSliceBlob`, e.g. a
space-separated list of 0..1 fractions) so manual marker edits survive a save.
`ReloadFromPath()` re-runs analysis **only when the blob is absent**.
`ReloadFromPath()` must also save and restore every param that `FinishBuffer`
resets — see `SamplerNode::ReloadFromPath` (`SamplerNode.cpp:769-781`) for
exactly that save/restore dance, including why `position` is restored verbatim
rather than re-clamped (copy/paste dropped the saved value otherwise).

## 4.3 `DrawSlicerWaveform` + `DrawSlicerBody` in `src/main.cpp`

Base the waveform on `DrawSamplerWaveform` (`src/main.cpp:8262-8402`), placed
next to it. Add vertical slice-marker lines and per-slice hit regions on top,
plus a per-slice note label if it fits.

**Correction to a claim in the source brief — the ImGui overlap rule is the
opposite of what you may have been told.** ImGui overlap resolution is **not**
"last submitted wins": a *later* item is blocked from becoming hovered/active
while an *earlier* item already holds `g.HoveredId`, unless that earlier item
opted in via `ImGui::SetNextItemAllowOverlap()` called **before** it. The
verified working pattern (`src/main.cpp:8270-8288`) is:

```cpp
ImGui::SetNextItemAllowOverlap();          // BEFORE the body button, not after
ImGui::SetCursorScreenPos(origin);
ImGui::InvisibleButton("##slicerwavebody", ImVec2(w, h));
// ... draw ...
// then the narrow marker grab-zones, submitted afterwards
```

Marker drags call `PushUndoCheckpoint()` on `ImGui::IsItemActivated()`, and
must write into the persisted slice blob.

Applicable pillars, all of which are acceptance criteria:

- **P1** every control on the row grid — no free-floating widgets between
  `BeginAudioBody`/`EndAudioBody` outside an `AudioKnobRow`, an `AudioSlider`,
  a `BeginAudioSection` panel, or the deliberate full-width button strip.
- **P2** the modulation dot is vertically centred on its control. `AudioKnobRow::Checkbox`
  and `::Dropdown` already offset by `(controlHeight - 12.0f) * 0.5f`; do not
  hand-roll one.
- **P3** selector left, knobs/sliders right — `slice by` is the **leftmost**
  cell of its row.
- **P5** the mode swap in cell 2 must not move the grid.
- **P6** prefer a filled grid; use `row.Skip()` deliberately rather than
  inventing a knob to fill a hole.
- **P10** dark-mode contrast: never hand-roll a checkbox/dropdown colour.
  `PushCheckboxStyle()` / `PushDropdownStyle()` (`src/main.cpp` ~2040 / ~1960)
  own both themes.
- **P11** `snprintf` into the readout strip, never into a caption; the strip
  is never empty. Idle strip should read something like
  `"<file>  -  16 slices  -  onsets"`.
- **P4 (`mix` bottom-right) does not apply** — that is an `AudioEffects`
  convention and this is a `Synths` node with no mix param.
- **P8/P9 do not apply** — no scale/root pair, no pitch strip.

`ImGui::SetTooltip` between `ed::Begin()` and `ed::End()` lands offset from
the cursor and grows with zoom. Use `SetAudioReadout(...)` instead
(`DrawSamplerBody` does exactly this at `src/main.cpp:10822`).

---

# 5. Wiring checklist — hand-maintained sites, verified

Everything **not** listed here is already generic. Adding an entry anywhere
else is a sign the node is built wrong.

1. **`CMakeLists.txt`, two additions.**
   - `src/audio/dsp/SlicerDsp.cpp` in the dsp block — `MolderDsp.cpp` is
     line **316**, `GrainMolderDsp.cpp` line **317**.
   - `src/nodes/SlicerNode.cpp` in the node block — `SamplerNode.cpp` is
     line **321**, `GranularNode.cpp` line **325**.
2. **`src/main.cpp:157-161`** — `#include "nodes/SlicerNode.h"` beside
   `SamplerNode.h` (157), `GrainMolderNode.h` (160), `GranularNode.h` (161).
3. **`DisplayName`, `src/main.cpp:350-384`** — add
   `if (name == "Slicer") return "slicer";` next to the existing `"Sampler"`
   → `"sample player"` (`:365`) and `"Granular"` → `"granular"` (`:369`)
   entries. (Without it the generic lowercase fallback already yields
   `"slicer"`, but add it explicitly so the intent is recorded.)
4. **`RegisterNodes()`, `src/main.cpp:3752`** — add
   `REGISTER_NODE(SlicerNode, Slicer, "Synths");` beside
   `REGISTER_NODE(SamplerNode, Sampler, "Synths");` (`:3913`).
   Check for name collisions: `Noise`, `Curve`, `Curves`, `Shape`, `Pattern`,
   `Transform` are taken by visual nodes; `Slicer` is free.
5. **`ReloadDerivedState`, `src/main.cpp:5001-5034`** — add
   `if (auto* slicer = dynamic_cast<SlicerNode*>(node)) slicer->ReloadFromPath();`
   beside the `SamplerNode` branch (`:5011`).
6. **`DrawAudioNodeBody`, `src/main.cpp:18326`** — the **one remaining
   dispatch ladder**. Add
   `else if (auto* n = dynamic_cast<SlicerNode*>(gn.node.get())) DrawSlicerBody(gn, n);`
   next to the `SamplerNode` branch (`:18342`).
7. **`SpecificNodeHelpText`, `src/main.cpp:24152`** — one entry in the
   existing voice (what it does + the one non-obvious thing). The `Sampler`
   entry is at `:24207`; the surrounding block runs ~24200-24212.
8. **`DrawHelpWindow`, `src/main.cpp:24611`** — one line in the `"Synths"`
   group, which starts at `:24811` and currently ends at `:24818`.
9. **OS file-drop handler, `src/main.cpp:41390-41500`** — add
   `SlicerNode* dropTargetSlicer = FindNodeUnderCanvasPoint<SlicerNode>(canvasPos);`
   beside the existing probes (Sampler at `:41406`, GrainMolder at `:41410`)
   and a consuming branch in the audio-extension arm (Sampler's is `:41448-41452`,
   GrainMolder's `:41480-41484`) so dropping a WAV on the node loads it.
10. **Samples-panel drag-drop, `src/main.cpp:50715-50786`** — inside
    `gSampleDragKind == LibraryDragKind::Sample` (`:50715`), add an
    `else if (SlicerNode* targetSlicer = FindNodeUnderCanvasPoint<SlicerNode>(canvasMouse))`
    branch beside the Sampler one (`:50729`).

### Sites that are generic — do NOT edit (verified)

| Site | Line | Why |
|---|---|---|
| `InputCountFor` | `4109` | counts audio/note pins generically by probing `AudioInputSlot`/`NoteInputSlot` |
| `CableFor` | `4245` | **image** cables only; `AudioCableFor` (`4330`) / `NoteCableFor` (`4337`) forward generically |
| `IsInputSlotCompatible` | `4431` | branches on `AudioInputSlot(slot)` / `NoteInputSlot(slot)` generically (`:4459-4462`) |
| `WireInputSlot` | `4508` | generic |
| `DisconnectLinkById` | `24075` | generic |
| `IsAudioBodyNode` | `7999` | returns true off `IAudioSource` / `AudioInputSlot(0)` / `INoteSource` / `NoteInputSlot(0)` — Slicer qualifies |
| `AudioNodeWidth` | `8049` | default return is `kAudioNodeWidth` (440) — no edit needed |

(The source brief cited 3723 / 3909 / 3985 / 24095 for the cable chain; those
have drifted to the numbers above.)

### Gotchas

- `SpawnNode` returns a `GraphNode*` into the `gNodes` vector that a later
  `SpawnNode` can invalidate. Capture the stable `int index` and re-resolve
  via `FindNodeByIndex`.
- `ed::GetNodePosition` / `ed::GetNodeSize` read stale immediately after a
  spawn.
- `ed::CanvasToScreen` **hangs the app** if called between `ed::Begin`/`ed::End`
  or before `ed::Begin`.
- Cycles: `WouldCreateAudioCycle` / `WouldCreateNoteCycle` exist and must not
  be "fixed".
- One cable per audio input, one per audio output. Never quietly sum at a pin.

---

# 6. Tests

## 6.1 Generic — nothing to edit

`ROUNDTRIPTEST`, `INFINITE_AUDIOPARAMSWEEPTEST` and
`INFINITE_AUDIOTEARDOWNSWEEPTEST` all discover node types off `NodeFactory`
(`src/main.cpp:28111` onward) rather than a hand-maintained list. Registering
the node is enough.

## 6.2 `RunSlicerFixture()` — hand-written, required

Add it next to `RunSamplerFixture()` (body at `src/main.cpp:30259`) and call
it from `RunDspTest()` (`src/main.cpp:34390-34421`) — add both the
`const bool slicerOk = RunSlicerFixture();` line and the `slicerOk &&` term in
the `all` expression.

It must:

1. Synthesise a WAV with **known transient positions** (e.g. short bursts at
   0.0 / 0.25 / 0.5 / 0.75 s over silence), written to `TmpPath(...)` the same
   way `RunSamplerFixture` does (`src/main.cpp:30268-30283`).
2. `LoadFile` it, wait for the worker to finish, assert the **detected slice
   count** and that each detected position is within tolerance (≤ 15 ms) of
   the truth.
3. Assert each slice **triggers**: push a note-on for `36 + k` through a real
   `NoteEventQueue` (the `trigger` lambda at `src/main.cpp:30293` is the
   template) and confirm non-silent output, and that `36 + sliceCount` renders
   **silence**.
4. Assert grid mode at a known `Transport::Instance().SetTempo(120)` produces
   the arithmetically expected boundaries.
5. Print a verdict line ending in `OK` or containing `FAIL` — **the harness
   greps stdout, not exit codes.**

## 6.3 `INFINITE_AUDIOUITEST` spawn block

`src/main.cpp:38031`. The last `SpawnNode` in that block is
`SpawnNode("Equation Synth", "Synths", 5400.0f, 20.0f);` at index **30**
(`:38086`). **Append after it** — the block later indexes `gNodes[24]`
(Sampler), `gNodes[26]` (EQ), `gNodes[1]`, `[3]`, `[4]`, `[6]`, `[7]`, `[8]`
by hard-coded number, so inserting anywhere earlier silently breaks the
fixture. Add:

```cpp
SpawnNode("Slicer", "Synths", 5900.0f, 20.0f);   // 31
```

then pre-load it with a synthetic multi-transient WAV using the same inline
RIFF writer the Sampler pre-load uses (`src/main.cpp:38089-38113`), or the UI
test never draws a populated body.

## 6.4 `audio-param-sweep-expected.txt`

`.claude/skills/run-infinite-hygiene/audio-param-sweep-expected.txt`. Every
sample-based node in this family is blanket-xfailed — Sampler lines 125-135,
PaulStretch 136-148, Molder 149-163, Granular 164-183, Grain Molder 184-197 —
because the sweep's generic rig builds a bare instance with no file loaded and
an unloaded sampler is silent by design. The Slicer will fail on every param
for the same reason. Add **one line per param**, format:

```
<key>|<param>|<reason>
```

**Correction to the source brief:** the first field is the **registered
NodeFactory type name**, i.e. `Slicer` — *not* the `DisplayName` `slicer`.
Verified: the sweep prints `cand.name` (`src/main.cpp:35922`, `:35930`,
`:35945`, `:35948`), which is the registry key — which is why the existing
lines read `Sampler` (not `sample player`) and `Grain Molder`.

Match the existing prose style and cite `RunSlicerFixture` as the real
coverage, e.g.:

```
Slicer|sensitivity|AudioSlicerNode::ProcessBlock returns silent whenever no sample buffer is active - the sweep's generic rig constructs a bare throwaway instance with no file loaded, same "no file loaded" class as Sampler/Granular/Molder. And sensitivity only reaches the slice table via an async worker re-analysis dispatched from CookIfNeeded, never within the probed block. Confirmed correct by loading a real sample and checking detection/playback in RunSlicerFixture/main.cpp
```

The file is checked in **both directions** — a listed param that later passes
is also a failure ("delete its line"). So list exactly the params that
actually fail; run the sweep and read the output rather than guessing.

## 6.5 Harness

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
INFINITE_DSPTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite      # must end DSPTEST OK
.claude/skills/run-infinite-hygiene/driver.sh
```

Baseline is **40/41 pass** with a known `PHASEATEST` failure (a `"Smooth"`
node-name collision). That is pre-existing, not a regression, and must not be
"fixed" here.

Headless UI render, for judging the body by eye:

```bash
INFINITE_AUDIOUITEST=1 IMAGERESYNTH_SCREENSHOT=/tmp/audioui.png \
  ./build/Infinite.app/Contents/MacOS/Infinite
```

---

# 7. Standing user preferences

- **After building, copy the app to the Desktop:**
  `cp -R build/Infinite.app ~/Desktop/Infinite.app`
- **Do not UI-script Infinite's ImGui canvas to verify.** It is slow and
  unreliable. Let the user verify visually, or check the code. Screenshots via
  the headless `IMAGERESYNTH_SCREENSHOT` path above are fine; driving the
  canvas with synthetic mouse input from an automation tool is not.
- Do not end your report telling the user to do obvious implied next steps.

---

# 8. Out of scope

- Do **not** fix `SamplerNode`'s missing `srRatio`. It is a real latent bug,
  worth its own branch; touching it here would make this diff untestable.
- Do not restyle any existing node body, and do not touch `ModSlider`,
  `AudioKnobRow`, `DrawDiscreteParamPin`, `PushCheckboxStyle` or
  `PushDropdownStyle` — a change there changes every node in the app.
- Do not add a per-node BPM param; grid mode reads the global transport.
- Do not add slice reverse, per-slice pitch, or a slice sequencer. Seven
  params plus one mode is the ceiling (`new-audio-node/SKILL.md` §0.5: ~8
  controls max, one processing mode).

---

# 9. Acceptance checklist — report each item explicitly

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` compiles clean, no new
   warnings.
2. Spawned from the palette, the node shows **two** input pins labelled
   `notes` and `record in`, an audio output pin, and a body exactly **440 px**
   wide with a non-empty readout strip.
3. Loading a WAV detects slices; the readout strip shows the **actual** slice
   count; the waveform draws a marker per slice.
4. Playing `C1` upward triggers slices 0..n−1 in order; a note at
   `36 + sliceCount` is silent.
5. Grid mode at a known transport BPM produces arithmetically correct
   boundaries; changing `division` or `onsets` does **not** relaunch the
   worker (verify by instrumenting or by reading `LaunchJob`'s call sites).
6. Params survive save → load → undo → copy/paste → delete unchanged,
   including the slice-marker blob after manual marker edits.
7. Deleting the node mid-playback does not crash and logs zero xruns
   (`INFINITE_AUDIOTEARDOWNSWEEPTEST`).
8. `RunSlicerFixture` prints `OK`; `INFINITE_DSPTEST=1` ends `DSPTEST OK`.
9. `.claude/skills/run-infinite-hygiene/driver.sh` is at the 40/41 baseline.
10. Pillars **P1, P2, P3, P5, P6, P10, P11** all hold. Screenshot the node in
    the light theme, the default dark theme and one high-contrast theme;
    screenshot with a modulation cable attached to the `slice by` dropdown;
    toggle `slice by` and confirm **no cell moves**.
11. `build/Infinite.app` copied to `~/Desktop/Infinite.app`.
