---
name: audio-node-ui
description: Design and build the UI for Infinite's audio and note nodes (Wavetable, Filter, Delay, Reverb, Dynamics, Mixer, MIDI Notes, Envelope, Scope, ...) so they look and behave like a real instrument rather than a generic node — layout grammar, knob/slider/section widgets, visualizer catalogue, and the hardware/plugin conventions each rule comes from. Use when adding or restyling any audio or note node's body, when a node "looks bad / cramped / unfinished / not like a real plugin", when adding a knob, meter, scope, step grid, frequency-response curve or any inline audio visualizer, when a node is the wrong width or its params don't line up, or when asked how the audio nodes should look.
---

Paths are relative to the repo root (`/Users/namansoni/infinte`).

## Read first, in this order

1. `docs/plans/audio/audio-node-ui-system.md` — **the spec (v3).** Prescriptive.
   §1 layout grammar, §2 widget set, §3 visualizer catalogue, §5 param density,
   §7 the one-param case, §8 the reference Wavetable node, §9 the by-eye acceptance test.
2. `docs/plans/audio/audio-node-ui-review.md` — the critique that produced v3.
   Every rule in the spec has a defect behind it; this is where the defect and
   the plugin benchmark are written down. Read it before *changing* a rule.
3. `src/main.cpp`, the `v3 audio node layout` block — the implementation.
   `BeginAudioBody`, `BeginAudioSection`, `AudioKnobRow`, `AudioSlider`,
   `KnobFloat`, `ModKnob`, and the per-node `Draw*Body` functions below them.

Do **not** re-derive the grammar from scratch. It has been through three
revisions and two rounds of user feedback; the reasons are written down.

## The one rule everything else follows from

**An audio node's width is a scope, not a constant.** `BeginAudioBody` sets it;
every helper derives from `gAudioBodyW` / `gAudioContentW`. Rows are laid out
*to fit* the column and can never set it.

The v2 failure this replaced: `kAudioNodeWidth` was declared but not enforced,
so the visualizer drew at 440, `NodeSeparator` at 190, and the knob row at
whatever N knobs added up to — three widths in one body, the node taking the
widest, nothing sharing an edge. That single mechanical fact was most of why
the result read as unfinished.

Two widths exist. There is no third:

| Constant | Value | Use |
|---|---|---|
| `kAudioNodeWidth` | 440 | a node with a real param set |
| `kAudioNarrowWidth` | 200 | ≤2 params — Gain, Splitter, Audio Out |
| `kAudioWideWidth` | 960 | two parallel instruments side by side (Wavetable) — use `BeginAudioColumns`, read spec §1k first |

## Skeleton for a new audio node body

```cpp
void DrawFooBody(GraphNode& gn, FooNode* n)
{
   char stat[64];
   snprintf(stat, sizeof(stat), "...");   // never empty - see "Readout strip"
   BeginAudioBody(gn.index, gn.category, kAudioNodeWidth, stat);

   DrawFooVisualizer(n);                  // exactly one, full body width
   ImGui::Dummy(ImVec2(0.0f, 4.0f));

   {                                      // Tier 1: prefer 2 rows of 4
      AudioKnobRow row(4);
      row.Dropdown("mode", kModes, n->mode, [n](int i){ PushUndoCheckpoint(); n->mode = i; });
      row.Knob("cutoff", &n->cutoff, 20.0f, 20000.0f, "%.0f Hz", kKnobLarge);
      row.Knob("res",    &n->res,    0.0f, 1.0f,      "%.2f",    kKnobLarge);
      row.Knob("drive",  &n->drive,  0.0f, 1.0f,      "%.2f");   // kKnobSmall
      row.End();
   }

   BeginAudioSection("response");         // Tier 2: panels, never hairlines
   AudioSlider("slope", &n->slope, 0.0f, 1.0f, "%.3f", AudioHalfWidth());
   ImGui::SameLine();
   AudioSlider("mix",   &n->mix,   0.0f, 1.0f, "%.3f", AudioHalfWidth());
   EndAudioSection();

   EndAudioBody();
}
```

Then add the `dynamic_cast` branch to `DrawAudioNodeBody`, and make sure
`IsAudioBodyNode` accepts the node (it keys on `IAudioSource` /
`AudioInputSlot(0)` / `INoteSource` / `NoteInputSlot(0)`).

## Conventions, and the instrument each comes from

These are how hardware and every commercial plugin solve the same problems.
Follow them; if one has to be broken, say why in the code and in the spec.

**Knob by default; vertical fader only for a compared set.** `row.Fader(...)`
exists for the Mixer's channel gains and Gain's single channel — places where
reading the *balance* between several same-meaning levels matters more than
setting any one precisely. A lone fader on an unrelated param gets none of
that benefit and breaks the row rhythm. Both widgets go through `ModKnob`
(its `style` argument), so a fader keeps the identical pin / typed-edit /
expression / undo behaviour.

**Knobs carry hierarchy through size.** `kKnobLarge` (56) for the one or two
params that define what the node is doing; `kKnobSmall` (40) for the rest. A
row of identical dials has no hierarchy and reads as a spreadsheet. Cells
bottom-align on the row's largest control, so every caption shares one
baseline.

**A knob's caption is its param name, never a live number.** Hardware doesn't
print a value on the cap. The value goes to the readout strip on hover, and is
reachable exactly via double-click / right-click / hover-and-type.

**Readout strip, not tooltips.** One fixed location per node: idle stat left,
hovered param's name + value right. It never moves and never covers the control
being adjusted. **Never call `ImGui::SetTooltip` between `ed::Begin()` and
`ed::End()`** — the canvas transform is applied to it and it lands offset from
the cursor, growing with zoom. If a tooltip is truly unavoidable, wrap it in
`ed::Suspend()` / `ed::Resume()`.

**The idle stat is never empty.** An empty strip renders as a blank input
field, which is exactly the "big black box" complaint that started v3. Give it
something real: `"Sine - 1 voice"`, `"Grid - 8 steps"`, `"+0.0 dB"`,
`"8 in -> 1 out"`.

**Horizontal sliders: label left, value right, both inside the track**, with a
low-alpha fill from the left edge instead of a grab handle. No bright rule at
the fill edge — it lands under the label and reads as a text caret.

**Sections are panels, not hairlines.** `BeginAudioSection` draws an inset
full-width panel with a tinted header. Grouping is what turns a long param list
into a few readable clusters; it is the highest visual return per line in the
whole system.

**A visualizer is never blank at rest.** Draw the frame, the graticule/scale,
and a meaningful at-rest state — the selected waveform dim behind an `idle`
marker, meter ticks and peak-hold, the ADSR shape, the step grid's playhead. A
blank visualizer looks broken, not idle. §3f of the spec lists what each type
owes.

**Node layout must not resize when a mode changes.** Pad a shorter variant to
the taller one (Note Sequencer's Grid/Euclidean/Polyrhythm panels do this) so
switching a dropdown never moves the node under the cursor.

**Greyed out, not hidden.** A param that is meaningless in the current mode
(pulse width on a sine) uses `ImGui::BeginDisabled()` — hiding it shifts the
row.

## RT-safety — non-negotiable

- A visualizer reads `MeterRing` (SPSC, audio→main, decimated at the write
  side) **or** recomputes main-thread from already-published param values.
  Never call into an `AudioNode` from the UI thread; the audio thread never
  touches ImGui.
- `MeterRing::Write` from `ProcessBlock` only; `MeterRing::Read` from
  `CookIfNeeded` only.
- Traces refresh at ~30 Hz regardless of frame rate (`README.md` §1). A shape
  computed purely from params (ADSR, idle waveform, filter response from
  coefficients) may redraw per frame — it costs nothing and reads no shared
  state.
- Anything derived from the transport (a sequencer playhead) comes from
  `Transport::Instance()` on the main thread, the same clock the audio node
  advances from, so the two agree with no cross-thread read.

## Gotchas that have already cost time

- **A multi-column body must wrap each column in `ImGui::BeginGroup()`.**
  `BeginAudioColumn` does. ImGui returns the cursor to the *window's* content
  origin after every item and `ImGui::Indent` is measured from there, so
  pointing the four `gAudio*` globals at a column places only its first
  widget — everything after wraps back to column 0. `AudioKnobRow` and section
  backgrounds position themselves explicitly and look fine, so the symptom is
  narrow and misleading: only visualizers and in-section sliders land wrong,
  column 1 paints over column 0, and column 1's invisible buttons eat column
  0's drags. It reads as "the visualizers are dead". Spec §1k-2.
- **A `DropdownKnob`'s dropdown is never disabled, only its knob.** Greying
  the whole cell when the mode is "off" leaves no way to select another mode.
- **`ed::CanvasToScreen` hangs the app** if called from inside the node draw
  (between `ed::Begin`/`ed::End`) or before `ed::Begin`. The only safe place
  is the post-editor block — see the `INFINITE_WTDRAGTEST` hook.
- **`IsAudioBodyNode` must be checked before the `IModulator` branch** in the
  node-body dispatch. `EnvelopeNode` is an `IModulator`, and for a while it
  silently drew a generic modulator meter while `DrawEnvelopeBody` never ran.
- **`NodeSeparator` defaults to `kPreviewSize` (190)** for visual nodes. Never
  use it in an audio body — use `BeginAudioSection`.
- **An expression's `fx`/`x` suffix is `SameLine`d after the slider**, so in an
  audio body the slider must give up that width or the badges hang off the
  right edge of the cell — outside the section panel, over the next column.
  Audio bodies drop the `fx` caption entirely (the pin and the track fill
  already say "expression" twice) and keep only the clear button.
- **`ModSlider` spends the first 18px of its width on the modulation pin.** A
  half-width field sized against its text alone still overlaps, because the
  *track* is 18px narrower than the cell. `AudioSliderFloat` now clips the
  label to whatever is left of the value, so the two can never print on top of
  each other whatever the value grows to.
- **Typed edit must use a `##` label.** `ImGui::InputText(label, ...)` draws
  the caption *outside* the box to the right and adds its width to the item, so
  passing the bare param name both shrinks the field and spills text over the
  neighbouring cell — the "typing a formula breaks the UI" bug. Reserve the
  widget's full footprint afterwards so the row doesn't reflow. This was fixed
  in `ModKnob` and left broken in `ModSlider` for two revisions; if you touch
  one, check the other.
- **Expressions get `lo`/`hi` bound to the param's range.** A bare `=sin(t)`
  writes raw units and clamps to nothing on a millisecond param, which reads as
  "formulas don't work". `=lerp(lo, hi, sin(t) * 0.5 + 0.5)` is the
  full-range spelling.
- **Integer knobs accumulate; integer sliders don't.** `ModKnobInt` keeps its
  backing float across frames and re-seeds from the int only when something
  else moved the param. Re-seeding every frame (what shipped originally)
  discards every sub-step fraction, so a drag only registers when one frame's
  mouse delta crosses half a step. Round with `lroundf`, never
  `(int)(x + 0.5f)` — the latter truncates toward zero and biases every
  negative-range param.
- **`ModKnob`'s modulation pin lives in the cell margin**, drawn after the knob
  and outside its interactive rect. Don't move it back inline: it costs 18px of
  row budget per knob and baseline-aligns to a different centre than the knob.
- **Do not restyle the visual/image/geometry node library.** `ModSlider`'s
  `audioStyle` parameter defaults to `false` and every existing caller is
  unchanged. Keep it that way.
- **Clean room**: do not read, open, grep or reference
  `/Users/namansoni/BespokeSynth` or any other GPLv3 source (this includes
  Vital's source). Node taxonomy, param names and layout conventions come from
  public product documentation and shipped UI, which is how the param
  inventories in the spec were built. DSP comes from primary literature.

## Verify

```bash
open -n --env INFINITE_AUDIOUITEST=1 -a build/Infinite.app
```

Leaves the window open on a canvas with one of every audio/note node, framed.
To judge it without a screen-recording permission prompt, render it headless
instead — this writes a PNG of the whole window and exits:

```bash
INFINITE_AUDIOUITEST=1 IMAGERESYNTH_SCREENSHOT=/tmp/audioui.png ./build/Infinite.app/Contents/MacOS/Infinite
```

Add new node types to that block in `src/main.cpp`. Then check, in order:

1. Every horizontal element in the body starts and ends at the same x.
2. The node is exactly 440 or 200 wide — if wider, a row is setting the width.
3. Every knob caption in a row shares one baseline.
4. The visualizer shows something meaningful with no audio running.
5. Hovering any control fills the readout strip, and nothing floats near the
   cursor.

Interactivity cannot be checked from a screenshot — "the visualizer is in the
right place" and "dragging it moves this engine's param" are different claims,
and the second is the one that has broken. Drive it:

```bash
INFINITE_WTDRAGTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite
```

The UI fixture also puts one envelope field into the expression state and
gives another a four-digit value, so the `fx`/clear layout and the widest
label/value pair are always on screen. Add `INFINITE_AUDIOUITEST_TYPING=1` to
leave a third field open in formula-entry mode (behind its own flag: a fixture
that opens with a focused text field swallows the keyboard).

Must end `WTDRAG OK`. It scrubs the table picture and drags two envelope
handles, asserting each time that the *other* engine did not move.

Then the regression suite — UI changes here touch `ModSlider`, which every
node in the app uses:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

Baseline is 40/41. `PHASEATEST` is a known pre-existing failure (a `"Smooth"`
node-name collision); it is not a regression and should not be fixed here.

```bash
INFINITE_DSPTEST=1 ./build/Infinite.app/Contents/MacOS/Infinite
```

Must still end `DSPTEST OK`.
