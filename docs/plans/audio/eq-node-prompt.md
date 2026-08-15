# Implementation prompt — EQ node (interactive response curve)

Hand this whole file to a fresh Claude Code session in `/Users/namansoni/infinte`.
Everything below was verified against the tree at commit `f50cef7`.

---

Implement the **EQ** node in Infinite (`/Users/namansoni/infinte`).

Category `AudioEffects`. Shape: audio effect. It is a **new `EffectDef` table
row plus a new kernel plus a new `Draw*Body`** — *not* a new `INode` subclass.
`docs/plans/audio/README.md:180` folds EQ into Audio Filter ("an EQ *is*
filters in series"), but the shipped Audio Filter was deliberately cut down to
a single band (`src/audio/EffectDefs.cpp:29-54` says so explicitly), so the
multi-band EQ that line describes does not exist. Build it as its own node
rather than re-expanding Audio Filter — Audio Filter's one-band-one-type
surface is the thing a sweep, a fixture and a shipped patch all depend on.

Read `.claude/skills/new-audio-node/SKILL.md` for the procedure and
`.claude/skills/audio-node-ui/SKILL.md` for the body layout. Both are
prescriptive — do not re-derive either.

Five rules that override anything you infer:
1. **Clean room.** Do not read/grep `/Users/namansoni/BespokeSynth` or any
   GPLv3 source. The DSP here is Robert Bristow-Johnson's Audio EQ Cookbook,
   which `DspMath::Biquad` (`src/audio/DspMath.h:306`) already implements.
2. **Two objects.** `INode` (main thread) owns `AudioNode` (audio thread);
   they talk only through `ParamMailbox` and `MeterRing`. `AudioEffectNode` +
   `IEffectKernel` already give you this for free — you write a kernel, not a
   node class.
3. **`CookIfNeeded` does no DSP.** Coefficients are computed in
   `PushParams` (main thread) and pushed as coefficients, exactly like
   `AudioFilterKernel::PushParams` (`src/audio/dsp/AudioFilterKernel.cpp:8`).
   `ProcessBlock` must never call `tan()`/`cos()`.
4. **Audio thread:** no allocation, locks, `dynamic_cast`, `std::function`/
   `map`/`string`, GL, ImGui, file I/O, or `printf`.
5. **Minimalism.** ~8 controls max excluding the visualizer. The design
   below is 6 knobs + a dropdown; do not add band-count selectors, analyzer
   modes, mid/side, dynamic EQ, or auto-gain. If it feels short, that is the
   bar being met, not missed.

---

## 1. The design (all decisions resolved — implement as written)

**Five fixed bands, always present, each individually enable-able.** No
variable band count: a band-count param means the UI reflows when it changes,
which `audio-node-ui` forbids ("Node layout must not resize when a mode
changes"). Bands, with defaults:

| # | default type | freq | Q | gain | on |
|---|---|---|---|---|---|
| 1 | low shelf | 80 Hz | 0.707 | 0 dB | on |
| 2 | peak | 250 Hz | 1.0 | 0 dB | on |
| 3 | peak | 1000 Hz | 1.0 | 0 dB | on |
| 4 | peak | 4000 Hz | 1.0 | 0 dB | on |
| 5 | high shelf | 10000 Hz | 0.707 | 0 dB | on |

Every band can be set to any of **five types**: `low shelf`, `peak`,
`high shelf`, `hp 12`, `lp 12`. All five are single-stage RBJ biquads —
`DspMath::Biquad::SetLowShelf/SetPeaking/SetHighShelf/SetHighpass/SetLowpass`
all exist already. **Do not use `TptSvf` here**, and do not offer 24/36 dB
slopes: the whole visualizer design below depends on every band being one
biquad with a closed-form magnitude (§3), and cascaded SVF stages would break
that for no audible gain that Audio Filter doesn't already provide.

Control surface (`DrawEqBody`):
- The response curve, full body width — this *is* the Tier 1 surface (§3g of
  `audio-node-ui-system.md`: "if the picture is a parameter, the picture is
  the control").
- One `AudioKnobRow(4)`: `type` dropdown, `freq`, `Q`, `gain` — all four
  bound to **the currently selected band**. `AudioEffectNode.h`'s class
  comment already names this exact pattern ("Audio Filter's Tier 1 follows
  whichever band is selected") as the reason layout is not table-driven.
- One `BeginAudioSection("output")` with `output gain` and `mix`.

That is 6 knobs + 1 dropdown. Nothing else.

Readout stat line (never empty): `"5 bands - 3 active - +2.5 dB peak"` or
similar; band 1..5 selection shown as `"band 3 - 1.0 kHz"` while a handle is
being dragged.

## 2. `EffectDef` table row (`src/audio/EffectDefs.cpp`)

Add after the Audio Filter entry. Param naming — 5 bands × 5 params, flat, no
nesting (the table has no nesting and `VisitParams` keys are flat strings):

```
band1Type, band1Freq, band1Q, band1Gain, band1On
... through band5On
outputGainDb
selectedBand          // uiOnly
```

`selectedBand` (0..4) is UI state that must survive save/load, so it goes in
the table with **`uiOnly = true`** — that flag exists precisely so
`AUDIOPARAMSWEEPTEST` reports it `pass` with a note instead of failing it for
having no audio-thread effect (`src/main.cpp:15284`).

**The prerequisite trap — this will fail the sweep if you skip it.** The
sweep varies one param at a time and asserts the audio output changed. With
every band's `gain` defaulting to 0 dB, a peak band is an exact no-op, so
`band2Freq` and `band2Q` are *unobservable* at their defaults and the sweep
reports FAIL. Declare prerequisites on every band's `freq` and `q`:

```cpp
def.params.push_back({ "band2Freq", 20.0f, 20000.0f, 250.0f, false,
                       { { "band2Gain", 12.0f }, { "band2On", 1.0f } } });
```

and on every `gain`, `{ { "bandNOn", 1.0f } }`. `hp 12`/`lp 12` bands need no
gain prereq but still need the `On` one. This is the same mechanism Audio
Filter's `gain` uses (`EffectDefs.cpp:45`); read `EffectParamPrereq`'s comment
in `src/audio/EffectDefs.h:20-35` before writing them.

Set `def.bodyWidth = 440.0f`, `def.defaultMix = 1.0f`,
`def.visualizerId = EffectVisualizerId::kEqCurve` (add that enumerator to
`EffectDefs.h`), `def.makeKernel = []{ return std::make_unique<EqKernel>(); }`.

## 3. The kernel (`src/audio/dsp/EqKernel.h` / `.cpp`)

Model it on `AudioFilterKernel` — same file shape, same `PushParams` →
coefficients discipline.

Mailbox layout: 5 bands × 5 coefficients (b0,b1,b2,a1,a2) = 25 slots, plus 5
enable slots, plus one output-gain slot = 31. `ParamMailbox::kMaxParams` is
**64** (`src/audio/ParamMailbox.h:23`), so this fits with room to spare —
but it is close enough that adding a sixth band later would not, which is a
second reason the band count is fixed at 5.

`ProcessBlock`: `DspMath::Biquad mBiquad[5][kMaxChannels]`, 5 in series per
channel, then output gain. Copy the per-sample coefficient assignment from
`AudioFilterKernel::ProcessBlock`'s non-SVF branch, including the
`+ 1.0e-20f` denormal bias before the recursive stages. A disabled band is
implemented by pushing **bypass coefficients** (`b0=1, b1=b2=a1=a2=0`), not
by branching on a flag per sample — that keeps the mailbox's smoothing
crossfading the band out instead of clicking it out. `LatencySamples()` is 0.

### The closed-form magnitude — the one genuinely new piece of DSP

`AudioFilterDsp::MagnitudeDb` (`AudioFilterKernel.h`) computes a band's
response by **rendering a settled sine through a scratch filter** — up to
8000 samples per evaluation point. That is affordable for one band × 160
points; at 5 bands it is ~6.4 M sample operations per curve recompute, and a
curve recompute happens on **every frame of a handle drag**. Do not reuse it
for the curve.

Instead add to a new `EqDsp` namespace:

```cpp
// |H(e^-jw)| of one RBJ biquad, in dB, evaluated at evalHz.
inline float BiquadMagnitudeDb(const DspMath::Biquad& bq, float evalHz, double sampleRate)
{
   const double w = 2.0 * M_PI * evalHz / sampleRate;
   const double c1 = cos(w),  s1 = sin(w);
   const double c2 = cos(2*w), s2 = sin(2*w);
   const double nRe = bq.b0 + bq.b1 * c1 + bq.b2 * c2;
   const double nIm = -(bq.b1 * s1 + bq.b2 * s2);
   const double dRe = 1.0 + bq.a1 * c1 + bq.a2 * c2;
   const double dIm = -(bq.a1 * s1 + bq.a2 * s2);
   const double mag2 = (nRe*nRe + nIm*nIm) / std::max(1e-20, dRe*dRe + dIm*dIm);
   return (float)(10.0 * log10(std::max(1e-20, mag2)));
}
```

Plus a `ConfigureBiquad(bq, type, freq, q, gainDb, sampleRate)` shared by
`PushParams` and the visualizer, so the picture and the running filter can
never disagree — the same reason `AudioFilterDsp::ConfigureBiquad` exists.
The composite response is the **sum of the enabled bands' dB values** (a
series cascade multiplies magnitudes, which adds in dB).

I derived the formula above but did **not** run it. Verify it in the fixture
(§6) against `AudioFilterDsp::MagnitudeDb` before trusting it — if the sign
convention on `nIm`/`dIm` is wrong the magnitude is unaffected (both terms
are squared), but check the shelf and peak cases numerically anyway.

## 4. The interactive curve (`DrawEqVisualizer` in `src/main.cpp`)

Start from `DrawAudioFilterVisualizer` (`src/main.cpp:6817-6947`) — it is
already a draggable response curve and it carries three hard-won structural
decisions in its comments. Keep all three:

- **Exactly one `ImGui::InvisibleButton` over the whole graph.** Its comment
  at `main.cpp:6885` records that a second real ImGui item drawn on top (to
  make a second handle separately clickable) disturbed the layout cursor for
  the knob row drawn afterward and broke drag on *both* handles. With five
  bands the temptation to add per-handle buttons is much stronger — don't.
  Decide by proximity on `IsItemActivated()` which band and which handle the
  gesture grabbed, latch it for the whole drag (`audio-node-ui-system.md`
  §3g: "latch the handle on press or a fast drag jumps between handles").
- **Q is a second handle, not the scroll wheel.** Same comment: scroll is how
  the canvas zooms, so scrolling over the graph silently dragged Q. Draw a
  small diamond offset beside each band's dot, dragged vertically, log-mapped
  over 0.1..18 — reuse Audio Filter's mapping exactly.
- **No trailing `ImGui::Dummy`** after the graph: the `InvisibleButton`
  already reserved the layout space.

Reuse `FilterVizFreqToX` / `FilterVizXToFreq` / `FilterVizDbToY` /
`FilterVizYToDb` (`main.cpp:6788-6813`) and the `kFilterViz*Hz/Db` constants
verbatim — same 20 Hz–20 kHz log X, ±24 dB Y. Do not define a second set.

Interaction, resolved:
- **Drag a band dot** → X sets that band's `freq`, Y sets its `gain`. For
  `hp 12`/`lp 12` bands, gain is inert: pin the dot to the 0 dB line and let
  only X move (mirror `AudioFilterDsp::UsesGain`'s role).
- **Drag a band's diamond** → that band's `Q`.
- **Click empty graph area** → select the nearest band (sets `selectedBand`,
  which the knob row follows). A press that lands on a handle both selects
  that band and starts its drag.
- **Double-click a band's dot** → toggle `bandNOn`. Detect with
  `ImGui::IsMouseDoubleClicked(0)` while the button is hovered and the press
  is within the handle's radius; cancel the latched drag for that gesture so
  the toggle doesn't also nudge freq/gain.
- **`PushUndoCheckpoint()` once per gesture on `IsItemActivated()`** —
  Audio Filter already does exactly this at `main.cpp:6824`.

Drawing, in order: panel fill → clip → freq/dB graticule (same ticks as Audio
Filter) → each enabled band's own curve at low alpha → the composite curve
bright (`IM_COL32(150,214,255,245)`, 1.8 px) with a soft fill to the 0 dB
line → handles. **A disabled band draws a hollow dot and no curve**, so "off"
is visible rather than absent (§3f, "a visualizer is never blank"). The
selected band's dot is drawn larger and last, so it is never occluded.

Caching: add a `gEqCurveCache` keyed on `gCurrentNodeIndex`, exactly like
`gFilterCurveCache` (`main.cpp:6776-6782`), with a signature vector holding
sample rate + all 25 band params. Recompute only on signature change. Even
with the closed form this matters: 5 bands × 160 points every frame for
20 nodes is the drawing cost `README.md` §1 warns is the real budget, not the
DSP.

Height: use `190.0f`, matching Audio Filter. If it reads cramped once you see
it, **raise the height, never the node width** — width is the invariant the
whole grammar hangs off (`audio-node-ui` "an audio node's width is a scope").
440 stays.

Readout strip on hover: `"band 3  1.0 kHz  +4.5 dB  Q 1.20"` via
`SetAudioReadout`. **Never `ImGui::SetTooltip`** between `ed::Begin`/`ed::End`.

## 5. Wiring sites

Everything else is generic. Concretely:
1. `src/audio/dsp/EqKernel.h` / `.cpp` — new files.
2. `CMakeLists.txt` — add `EqKernel.cpp` to the `src/audio/` source list
   (near the other `dsp/*Kernel.cpp` entries, ~line 54).
3. `src/audio/EffectDefs.h` — add `kEqCurve` to `EffectVisualizerId`.
4. `src/audio/EffectDefs.cpp` — the table row (§2), plus the
   `#include "dsp/EqKernel.h"`.
5. `src/main.cpp:8642` — a `case EffectVisualizerId::kEqCurve: DrawEqBody(...)`
   in the `DrawAudioNodeBody` switch. This is the **only** dispatch ladder
   that needs an entry; `RegisterNodes()` already loops `GetEffectDefs()` at
   `main.cpp:2132`, and `InputCountFor` is generic.
6. `src/main.cpp:11164`-ish — one line in the node help table, in the existing
   voice: what it does plus the one non-obvious thing (drag the curve; double-
   click a band to bypass it).
7. `src/main.cpp:15661` — add `SpawnNode("EQ", "AudioEffects", ...)` to the
   `INFINITE_AUDIOUITEST` fixture's canvas, next to Wavetable Shaper (26).
   Give it a non-flat default in the fixture (a couple of bands moved off
   0 dB) so the screenshot shows a real curve, the same reason the Sampler
   entry there synthesizes a fixture WAV.
8. `docs/plans/audio/STATUS.md` — add EQ to the Shipped list and bump the
   count; `ARCHITECTURE.md`'s audio section likewise.

## 6. Tests — write them with the node

Add `INFINITE_EQTEST`, modelled on `DelayTest` (`src/main.cpp:14005`) — a
real `AudioEffectNode` → `AudioEffectRuntime` → `EqKernel` chain, headless, no
device. Assert:
- **Closed form vs measurement.** For each of the five types at a few
  freq/Q/gain points, `EqDsp::BiquadMagnitudeDb` agrees with
  `AudioFilterDsp::MagnitudeDb` (the settled-sine measurement) within ~0.5 dB.
  This is the check that makes the whole visualizer trustworthy.
- **Rendered response.** A single peak band at 1 kHz, +12 dB, Q 1, all other
  bands off → a 1 kHz sine through the node comes out ~+12 dB, and a 100 Hz
  sine comes out ~unchanged.
- **Bypass is exact.** All five bands off, `mix = 1` → output equals input
  sample-for-sample (within float epsilon), i.e. the bypass coefficients are
  a true identity.
- **Series composition.** Two overlapping peaks at the same freq, +6 dB each
  → ~+12 dB there.

Print one verdict line ending `OK` or containing `FAIL`, per
`new-audio-node` §5.

**Interactivity cannot be verified from a screenshot**, and drag is the part
of this node most likely to ship broken. Extend the `INFINITE_WTDRAGTEST`
harness (`main.cpp:17097` drives synthetic mouse events; `main.cpp:22428`
converts canvas-space item rects to screen via `ed::CanvasToScreen`) or add an
`INFINITE_EQDRAGTEST` in the same shape, asserting: dragging band 3's dot
right raises `band3Freq` and leaves every other band's params bit-identical;
dragging its diamond changes only `band3Q`; double-clicking band 2's dot
flips `band2On` without moving `band2Freq`. Note the recorded trap:
**`ed::CanvasToScreen` hangs the app** if called inside the node draw or
before `ed::Begin` — the only safe place is the post-editor block.

Then run the two generic sweeps and the hygiene pass:

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

then `/run-infinite-hygiene`, and confirm `AUDIOPARAMSWEEPTEST` reports every
`bandN*` param green (this is where a missing prerequisite from §2 shows up)
and `AUDIOTEARDOWNSWEEPTEST` survives spawning/deleting an EQ mid-playback.

Deploy step, per this project's convention: copy `build/Infinite.app` to
`~/Desktop/Infinite.app` after a successful build.

## 7. Explicitly out of scope

- **No spectrum analyzer behind the curve.** It needs an FFT of the input
  published audio→main; `MeterRing` moves plain decimated floats
  (`src/audio/MeterRing.h`) and has no framing for spectra. That belongs to
  the Scope node (P3d), which `audio-node-ui-system.md` §3 already designates
  as "the one node that gets a bigger visualizer".
- **Do not modify Audio Filter**, its kernel, its visualizer, or its table
  row. Reuse its helper functions; change none of them.
- No dynamic EQ, mid/side, band solo, analyzer, auto-gain, linear phase,
  variable band count, or 24/36 dB band slopes.
- Do not restyle the visual/image/geometry node library; `ModSlider`'s
  `audioStyle` default stays `false`.

## 8. Done when

1. It builds clean.
2. Spawned from the palette it shows one `audio` input pin, renders at
   exactly 440 wide, and has a non-empty readout strip.
3. It audibly EQs a real chain ending at `Audio Out`.
4. Params survive save → load → undo → copy/paste → delete unchanged.
5. Deleting it mid-playback does not crash and logs zero xruns.
6. `INFINITE_EQTEST` prints `OK`, the drag fixture prints `OK`, and
   `/run-infinite-hygiene` passes.
7. `STATUS.md` and `ARCHITECTURE.md` are updated.

Report each of the seven explicitly.
