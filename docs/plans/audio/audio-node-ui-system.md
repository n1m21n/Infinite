# Audio node UI system

Written during P2.7, revised twice since. **v3 is current and is what the
tree implements** — read §1/§2/§7 as v3; §3 (visualizer catalogue), §5
(param density tiers), §6/§6a (cable compatibility) and §8 (Oscillator param
inventory) are unchanged since v1 and still authoritative. This is the
reference P3b–e sessions follow when adding a new audio node's body/params/
menu — not a proposal, a spec. Verified against the live tree
(`src/main.cpp`, `GraphNode.h`, `INode.h`, `AudioNodes.h/.cpp`,
`NoteNodes.h/.cpp`); line numbers drift, function/field names do not.

The full critique that produced v3, with the verified defect list and the
benchmark comparison behind each decision, is in
[`audio-node-ui-review.md`](audio-node-ui-review.md). Read it before
changing anything here.

## 1. Layout grammar (v3 — width is a scope, sections are panels)

v1 hid Tier 2 behind `GraphNode::showAdvancedParams` and packed Tier 1 into
a narrow 2-column grid. v2 corrected that into a wide horizontal rack strip
with nothing hidden, which was right in intent and wrong in mechanics:
`kAudioNodeWidth` was a constant helpers *mentioned* rather than obeyed, so
the visualizer drew at 440, `NodeSeparator` at `kPreviewSize` (190) and the
knob row at whatever N knobs happened to add up to (~556) — three widths in
one body, with the node taking the widest and nothing sharing an edge.

**v3's rule: the body width is a scope, and every helper derives from it.**

```
BeginAudioBody(nodeIndex, category, width, idleStat)
   ... visualizer / knob rows / sections ...
EndAudioBody()
```

`BeginAudioBody` sets `gAudioBodyW`/`gAudioBodyX` and the content column
(`gAudioContentX`/`gAudioContentW`, which a section narrows by its padding).
Rows are laid out **to fit** that column and can never set it. Two
sanctioned widths, nothing else:

| Constant | Value | Use |
|---|---|---|
| `kAudioNodeWidth` | 440 | a node with a real param set |
| `kAudioNarrowWidth` | 200 | ≤2 params (Gain, Splitter, Audio Out) |

Regions, top to bottom, fixed order:

1. **Title / category** — unchanged, drawn by the existing node-header code
   before the body dispatch runs. Not part of this grammar.
2. **Readout strip** — drawn by `BeginAudioBody`. Idle stat on the left
   (always non-empty: an empty strip reads as a blank input field), the
   hovered/dragged param's name and value on the right. One fixed location
   per node that never moves and never covers the control being adjusted.
   This replaces v2's cursor tooltip entirely — see §2.
3. **Visualizer** — exactly one, full body width, ~22–60px tall depending on
   type. Always visible, and **never blank at rest** (§3f below). For a node
   whose most natural "visualizer" is itself an editable readout (Note
   Sequencer's step grid), that control *is* the visualizer and takes this
   slot — see §3.
4. **Knob rows** — Tier 1 from the node's param inventory (§5), via
   `AudioKnobRow`. Cell width is `contentWidth / cellCount`, so a row is
   exactly the content width by construction. Prefer **two rows of four**
   over one row of six or more. Every control in a row is a knob or a
   `Dropdown` cell of the same pitch — v1's knob-for-some/slider-for-others
   Tier 1 was the thing that read as inconsistent, and v2's
   `DropdownButton` (which drew its label to the *right* of the button)
   broke the row's rhythm.
5. **Tier 2, always visible** — no expand toggle of any kind, but grouped
   into `BeginAudioSection`/`EndAudioSection` panels rather than separated
   by hairlines. Fine-tuning/precision params stay sliders — a slider reads
   an exact number better than a knob at a glance, which is the point of
   this tier — via `AudioSlider`/`AudioSliderInt` at `AudioFullWidth()` or
   `AudioHalfWidth()`.
6. **Output pin** — unchanged, drawn by the existing per-node output-pin
   code after the body returns.

A one-param node (Gain) still gets the full grammar, scaled down — see §7.

**Knob sizing carries hierarchy.** `kKnobLarge` (56) for the one or two
params that define what the node is doing right now; `kKnobSmall` (40) for
the rest. A row of identical dials has no hierarchy and reads as a
spreadsheet, which is what v2's single six-across row became. Cells
bottom-align on the row's largest control so every caption shares one
baseline.

**Dispatch.** `IsAudioBodyNode` gates the body, and it is checked **before**
the `IModulator` meter branch in `main.cpp`'s node-body chain — `EnvelopeNode`
implements `IModulator` (its output is a modulator value, see
`audio-graph-semantics.md` §6) and would otherwise silently draw a generic
modulator meter instead of its own body. Any future audio node that also
implements `IModulator` inherits the fix.

Audio nodes do **not** use the existing eye-toggle (`GraphNode::showParams`)
/ `DrawXxxParams` dispatch path visual nodes use: that path draws nothing
until the eye is clicked, which would hide Tier 1 params by default — the
opposite of "always visible". The eye toggle and its row, and the node
context menu's old "Show/Hide advanced params" entry, are both skipped
entirely for audio nodes — there is no hideable state left to toggle.
`GraphNode::showAdvancedParams` itself is left in place, unused by any
current audio node, purely so old saved patches/clipboard copies with that
field still deserialize; nothing reads it as a visibility gate any more.

### 1k-2. A column is a layout scope, and ImGui does not know that (v5)

`BeginAudioColumn` must call `ImGui::BeginGroup()` after it positions the
cursor, and `EndAudioColumn` must call `EndGroup()`. This is not tidiness.

ImGui returns the cursor to the **window's** content origin after every item,
and `ImGui::Indent` — which `BeginAudioSection` uses — is measured from that
same window origin. Neither knows a column exists. Pointing
`gAudioBodyX`/`gAudioContentX` at a column and calling `SetCursorScreenPos`
therefore only places the *first* widget; everything after it wraps back to
column 0.

The symptom is specific and was live in the shipped Wavetable for two
revisions: `AudioKnobRow` and the section panel backgrounds position
themselves explicitly from `gAudioContentX`, so those looked correct — but
every visualizer and every slider inside a section drew at column 0. Engine B
painted over engine A, so engine A's picture showed engine B's table, and
engine B's invisible buttons swallowed engine A's drags. It reads as "the
visualizers are dead", not as "the columns are wrong".

`BeginGroup` sets `DC.GroupOffset`/`DC.Indent` to the current cursor x, so
both wrapping and `Indent` become relative to the column. `EndAudioColumn`
reads the cursor for its height *before* `EndGroup`, which emits the group's
own `ItemSize` and moves it.

Regression cover: `INFINITE_WTDRAGTEST=1`.

### 1k. The third width, and why it exists (v4)

`kAudioWideWidth` was added for Wavetable and is not a licence for a wide
node in general. It exists because that node is genuinely two engines, and the
two alternatives both failed:

- **Stacked vertically at 440** — near 1600px tall, unreadable, and it forced
  an A/B tab.
- **The A/B tab** — the hidden engine's controls were not drawn, so both
  engines' params landed on the *same* `gParamCounter` indices. A modulator
  patched to engine A's attack drove engine B's the moment the tab flipped.
  Parameter identity in this codebase is draw order, so **anything that hides
  a param behind a mode aliases it onto whatever draws in its place.**

Two columns fixes the height and the aliasing together, because every param of
both engines is drawn every frame. If a future node needs a mode that hides
params, it must either reserve their indices explicitly or not hide them.

Columns come from `BeginAudioColumns(n)` / `BeginAudioColumn(i)` /
`EndAudioColumn()` / `EndAudioColumns()`, which just re-point
`gAudioBodyX/W` and `gAudioContentX/W` at the column — the "width is a scope"
rule doing exactly what it was written for. Every existing helper works inside
a column with no changes.

## 2. Widget set (v3)

| Widget | When to use | Backing call |
|---|---|---|
| Knob | Tier 1 continuous or int param | `row.Knob(label, &v, min, max, fmt, kKnobLarge\|kKnobSmall)` / `row.KnobInt(...)` inside an `AudioKnobRow` |
| Dropdown cell | Tier 1 enum param (waveform, pattern) | `row.Dropdown(label, options, current, onSelect)` — same cell pitch as a knob, caption on the knob caption baseline |
| Vertical fader | A **set** of same-meaning levels compared against each other — Mixer's channel gains, Gain's single channel | `row.Fader(label, &v, min, max, fmt, height)` inside an `AudioKnobRow` |
| Compact toggle | Tier 1 bool param | `ImGui::Checkbox`; no wrapper needed |
| Tier 2 slider | Tier 2 param | `AudioSlider`/`AudioSliderInt` at `AudioHalfWidth()` (two-per-row) or `AudioFullWidth()` |
| Section panel | Grouping Tier 2 params | `BeginAudioSection(label)` … `EndAudioSection()` |
| Visualizer | Exactly one per node type, see §3 | Custom per catalogue entry, drawn straight to the node's `ImDrawList` at `gAudioBodyW` (no ImGui plot widget — matches the hand-rolled spectrum bars `DrawAudioAnalyzeParams` already uses) |

**One knob size: `kKnobStd` (50).** v3 used `kKnobLarge`/`kKnobSmall` on the
theory that size carries hierarchy. In practice mixed sizes in one body read as
an inconsistency, not a ranking — the first thing a fresh pair of eyes asked
was "why are all the knobs different sizes". `kKnobLarge`/`kKnobSmall` are now
aliases of `kKnobStd` so existing call sites keep working. Hierarchy comes from
grouping, ordering and section headers instead.

**Knob by default; fader only for a compared set.** The fader was added when
the Mixer's eight-knob grid proved unreadable: eight identical dials with
eight identical captions can only be read one cell at a time, so the *balance*
between channels — the only thing a mixer exists to show — was invisible. A
column of faders is read as a shape in one glance, which is why every mixing
desk uses them there and knobs everywhere else. Do not reach for a fader for a
lone unrelated param; a single fader carries none of that benefit and breaks
the row rhythm knobs establish. `ModKnob` drives both widgets (its `style`
argument), so faders keep the identical pin / typed-edit / expression / undo
behaviour knobs have.

**A knob shows its param *name*, not a permanent live value.** Real hardware
doesn't print a number on the knob cap either — the exact value goes to the
node's readout strip on hover/drag, and is always reachable precisely via
double-click/right-click-to-type or by watching the fill arc while dragging
(same typed-edit/expression machinery `ModSlider` already had — see below).
This replaces v1's behavior, which showed the value permanently and never
the param name, so two knobs next to each other were indistinguishable
without hovering.

**A horizontal slider puts its label left and its value right, both inside
the track**, with a low-alpha fill from the left edge instead of a grab
handle. `AudioSliderFloat` styles `ImGui::SliderFloat`'s grab fully
transparent and empties its format string, then draws over it — so all
interaction (drag, clamp, Ctrl+click, and the
`IsItemActivated`/`IsItemHovered` surface `ModSlider`'s surrounding logic
depends on) is still ImGui's and only the pixels are ours. Do **not** draw a
bright rule at the fill edge: it lands under the label and reads as a text
caret.

**No tooltips inside the node editor canvas.** A tooltip submitted between
`ed::Begin()` and `ed::End()` inherits the canvas transform and lands offset
from the cursor by an amount that grows with zoom and pan — this was a real
shipped bug in v2's knob. If a tooltip is genuinely unavoidable, wrap it in
`ed::Suspend()`/`ed::Resume()` (as the drag-rejection reason now does).
Otherwise use the readout strip.

**The modulation pin lives in the knob's cell margin, not in the row.** v2
put a 14px pin before each knob on the same line: 18px of row budget per
knob (108px on a six-control row, a third of why that row overflowed) and a
line of dots floating above the knob caps, because a 14px pin and a 70px
knob baseline-align to different centres. v3 draws the knob first and tucks
the pin in immediately to its left afterwards, outside the knob's own
interactive rect so a drag from the pin starts a link rather than turning
the knob. Same pin id, same `ed::BeginPin`/`EndPin`, same `Modulation`
registration — only the position changed.

**Modulation is not reimplemented for the knob either.** `ModKnob` is a
line-for-line copy of `ModSlider` with `KnobFloat` swapping in for
`SliderFloat` in each branch (interactive/modulated/expression/typing) —
same pin id, same `Modulation::RegisterParam`/`IsModulated`/`HasExpression`
calls, same double-click-to-type and hover-hotkey handling. `KnobFloat`
itself is built on `ImGui::InvisibleButton` (vertical-drag to change value,
Shift for fine control) rather than any ImGui slider, but still leaves the
normal `IsItemHovered`/`IsItemActive`/`IsItemActivated` query surface behind
it — that's what let `ModKnob` reuse `ModSlider`'s surrounding logic
unchanged instead of re-deriving it.

**What a visual modulator can and cannot already do**, verified against
`main.cpp`'s `QueryNewLink` handling: a modulator dragged onto a `ModSlider`
pin (`GraphNode::IsParamPin`) is accepted unconditionally
(`valid = srcIsModulator`) *before* `IsInputSlotCompatible` ever runs — so
per-frame modulation of any audio node's `ModSlider`-backed param (frequency,
gain, detune, …) already works today, through the same
`Modulation::Instance()` apply pass every visual node uses (`main.cpp`, the
"apply modulation and expressions, then cook" block), landing in the node's
plain field before `CookIfNeeded` pushes it into `ParamMailbox`. This is
exactly the TouchDesigner-style mechanism `cook-rate-decision.md` describes
— it did not need new plumbing, because every audio node param that goes
through `ModSlider` gets it automatically.

What is **not** available, and correctly rejected: a visual modulator into an
audio/note *signal* pin (`AudioInputSlot`/`NoteInputSlot`) — that pin only
accepts a real audio/note source, never a modulator's normalised value. This
is the closed door §6 documents and gives a reason for.

## 3. Visualizer catalogue

One line per node type this phase or the next 3–4 sessions will touch. A new
audio node not on this list should get the closest match, then be added.

| Node | Visualizer | Source |
|---|---|---|
| Wavetable | Two: the table's frames as a receding stack with the current `position` picked out (primary), above a waveform trace of the output | Frames come from the immutable `Wavetable` bank on the main thread, so they redraw per frame like the ADSR curve; the trace is a `MeterRing` drained in `CookIfNeeded` |
| MIDI Notes | Three-octave keyboard showing which notes are held | Two `std::atomic<uint64_t>` held-key words published by `ProcessBlock` |
| Gain | Level meter (single bar, current linear level) | Same `MeterRing` pattern, one value not a trace |
| Audio Filter (P3c) | Frequency response curve | Computed on the main thread from the filter's own coefficients (`TptSvf`/`Biquad` `Process` fed a swept impulse, or the closed-form magnitude response) — never by calling into the live `AudioNode` |
| Dynamics (P3c) | Gain-reduction meter | `MeterRing` of the detector's output |
| Envelope (P3a) | ADSR shape (attack/decay/sustain/release as a static curve, redrawn only when the params change) | Computed directly from the four param values, no audio-thread data needed |
| Delay / Reverb (P3c) | Wet/dry level meter (a full IR/tap view is not worth the draw cost per §1 of `README.md`) | `MeterRing` |
| Scope (P3d) | Its own three view modes (waveform/spectrum/meter) — this node's *entire purpose* is being the one node that gets a bigger visualizer | `MeterRing`, decimated, at the 30 Hz cap `README.md` §1 sets |
| Mixer | Per-channel vertical meters inside each fader strip, plus the summed level bar | Per-slot `std::atomic<float>` peaks published by `ProcessBlock` |
| Everything else with no natural trace | No visualizer — a stat line instead (voice count, active notes), same as `GeometryNode`'s "N triangles" pattern | n/a |

### 3g. A visualizer that maps to one param should set it (v4)

If the picture *is* a parameter — the wavetable frame stack is `position`, the
ADSR curve is its four times and levels — the picture is the control. Dragging
the wavetable display scrubs `position`; the envelope curve has four draggable
handles. Setting those values only through a slider underneath is indirection
for its own sake, and it is what made the first Wavetable pass feel like a
readout rather than an instrument. The sliders stay, for typing exact values
and for their modulation pins.

Handle-drag rules that cost time to get right:
- Latch the handle on press and keep it for the whole gesture, or a fast drag
  jumps between handles mid-move.
- Give each time segment its own third of the width. Normalising all three
  against a shared total (what a read-only ADSR draw does) means dragging one
  handle visibly moves the other two.
- `PushUndoCheckpoint()` on `IsItemActivated()`, once per gesture.

### 3f. A visualizer is never blank (v3)

A visualizer that shows nothing at rest looks broken, not idle. Every entry
above draws its frame, its graticule/scale, and a meaningful at-rest state:

- **Waveform scope**: grid, ±0.9 rails and a centre zero line always; when
  the ring is empty, two dim cycles of the *currently selected waveform*,
  computed main-thread from the published param (`IdleWaveShape`), plus an
  `idle` marker. Same category of main-thread recomputation §3 already
  permits for the Envelope's ADSR curve — no audio-thread read.
- **Level meter**: ticks at −24/−12/−6/−3/0 dBFS, a decaying peak-hold
  marker, and traffic-light fill (green ≤ −6, amber ≤ −1, red above).
- **ADSR**: filled under the curve and stroked over it, with node dots at
  the four breakpoints.
- **Step grid**: cells stretch to the content width, and the currently
  firing step is outlined — derived main-thread from `Transport::Beats()`,
  the same clock `AudioNoteSequencerNode` advances from, so the two agree
  without any cross-thread read.

Every visualizer that reads audio-thread data goes through `MeterRing`
(SPSC, audio→main, decimated at the write side) or a main-thread
recomputation from already-published param values — never a direct call into
an `AudioNode`. `MeterRing::Write` is called from `ProcessBlock` only;
`MeterRing::Read` is called from `CookIfNeeded` only. Neither thread crosses
into the other's territory, and the audio thread never touches ImGui.

Redraw cadence: the ring is drained every `CookIfNeeded` call (already
frame-memoized, cheap — `MeterRing::Read` is a bounded pop, no allocation),
but the node's cached trace array is only overwritten when at least 1/30s
has elapsed since the last refresh (checked via `ImGui::GetTime()`), so the
line actually redrawn on screen updates at ~30 Hz regardless of app frame
rate — this is the §1 drawing-rules cap `README.md` sets for scopes.

## 4. Right-click menu policy for audio nodes

Baseline (unchanged from every other node): Rename-if-group, Help,
Ungroup-if-grouped.

**Suppressed for audio nodes:**
- "Open in viewport panel" / "Open in new window" / "Close window" — already
  gated off by `CanShowInViewportPanel`, which returns `false` for anything
  where `dynamic_cast<IAudioSource*>(n) || n->AudioInputSlot(0) != nullptr`.
  No new gating needed; this phase adds no audio-node exception here.
- "Show/Hide viewport" (the mini-3D-viewport toggle) — already gated on
  `IGeometrySource`, which no audio node implements. No change needed.

**Replaced:**
- "Hide/Show params" (`GraphNode::showParams`) is meaningless for an audio
  node — Tier 1 is never hidden, and there is no full-params-off state.
  Audio nodes get "Show advanced params" / "Hide advanced params" instead,
  which toggles `GraphNode::showAdvancedParams` — the same flag the
  in-node expand affordance toggles, so the menu and the inline chevron
  always agree.

**No audio-specific additions** in this phase — bypass, delete, copy/paste
etc. are unchanged and already node-type-agnostic.

## 5. Param density policy

**Tier 1 (collapsed grid, always visible):** the smallest set of controls a
user reaches for on every single instance of this node type — the ones that
define what sound it's making right now, not how it's shaped in detail.
Rule of thumb: if you'd expect to see it on a synth's front panel with the
lid closed, it's Tier 1. 4–6 controls, hard cap — more than 6 stops being a
"compact grid" and becomes exactly the sparse-vs-dense problem this phase
exists to fix in the other direction.

**Tier 2 (behind the expand affordance):** everything else a serious user of
that instrument category would expect to exist, reachable in one click.
Fine-tuning, secondary modes (FM section, sync, unison spread), and anything
that's "set once when patching, rarely touched per-note" belongs here.

**What never appears on a node, regardless of tier:** a control that
duplicates a *different* node's whole job. See §8's Oscillator writeup for
the worked example (no per-oscillator filter or envelope) — the rule
generalizes: if a param would make one node quietly re-implement another
node the plan already put in the graph for that job, it doesn't belong here.
Patch the dedicated node in instead.

## 6. Cable-type compatibility matrix (user-facing)

Verified against `IsInputSlotCompatible` (`main.cpp`) at the point this
phase landed:

| Source ↓ / Pin → | Image | Geometry | Modulator | Audio | Note |
|---|---|---|---|---|---|
| Image node | yes | no | no | no | no |
| `IGeometrySource` | no | yes | no | no | no |
| `IModulator` | no | no | yes | **no** | no |
| `IAudioSource` | no | no | no | **yes** | no |
| Note source | no | no | no | no | **yes** |

Plus single-type specialised pins: Camera, Light, Environment (HDRI-only),
Palette (`IPaletteSource`), and the Material/Displacement texture slots —
unchanged by this phase.

**Two rules are deliberate, and now explained where the refusal happens
instead of only here:**

- **Audio and Note are strictly closed.** An audio signal pin only ever
  accepts another audio signal; a note pin only ever accepts another note
  stream. No implicit coercion (no auto-inserted converter node) exists for
  either direction.
- **A visual modulator cannot cable into an audio *signal* pin** (an
  `AudioInputSlot`, the blue-tinted cable — not a param's `ModSlider` pin,
  see §2's clarification: modulator→param already works). This is
  intentional: a modulator publishes a normalised control value, not a
  sample stream: it's an amplitude-vs-time or LFO-shaped 0..1 signal read
  once per render frame, not 44,100+ audio-rate values a second. Feeding
  that into a signal input would be nonsensical.

Every refused drag between an audio/note pin and something incompatible now
shows a **reason**, not just the red reject flash — see §6a. Recommendation
adopted for P2.7 (rather than deferring to whenever an audio-rate
modulator-to-param mechanism exists): a silent refusal costs real user time
today, as it did when a first Oscillator→Gain attempt on the bare
unlabelled pin looked identical to a rejected drag. Explaining the refusal
now costs nothing structural to change later.

### 6a. How refusal reasons surface

Two independent, additive fixes:

1. **Every audio/note input pin now carries a label.** `GainNode` and
   `AudioOutputNode` override `InputLabel(int)` to return `"audio"` instead
   of the `INode` default `nullptr`, matching the convention `OutputNode`
   already set (`"in"` / `"audio"`). A labelled pin is not itself an
   explanation, but it stops the pin from reading as decoration — the
   reported failure mode ("could not work out how to connect Oscillator →
   Gain") was specifically that the pin looked like nothing, not that the
   rule was wrong.
2. **A refused drag shows why, via tooltip at the cursor**, the frame
   `ed::RejectNewItem` fires. The reason is derived from the same facts
   `IsInputSlotCompatible`/the `QueryNewLink` dispatch already computed for
   that frame (audio↔note pin type mismatch, modulator→audio/note signal
   pin, or the generic "wrong pin type" case for every other rejected
   combination) — no parallel classification logic, just surfacing the
   verdict that already exists.

## 7. Density scaling — the one-param case (v3)

Gain has exactly one param (`gainDb`). Under this grammar it gets the
**narrow body** (`kAudioNarrowWidth`, 200px): readout strip showing its
current dB, a level meter at that width, and a **1-cell** `AudioKnobRow`
with one large knob centred in it. No sections, no expand affordance — if
Tier 1 already contains every param the node has, there is nothing to put
behind one.

v2 said exactly this and then drew Gain's meter at the full 440, forcing a
440px node around a single knob — the spec contradicting itself in the one
place it had explicitly warned against. The narrow width exists so the rule
is enforced by construction rather than by remembering it. Splitter (no
params) and Audio Out (none) take the narrow body too, with only a readout
strip.

## 8. Reference Wavetable — param placement

Full inventory sourced from public synth documentation and shipped UI
(Arturia Pigments, Bitwig Polymer, Kilohearts Phase Plant, Ableton Wavetable,
Serum's published manual). No BespokeSynth or Vital source consultation, per
the standing clean-room rule; the DSP comes from primary literature and the
tables are generated from first-principles Fourier series (see
`src/audio/Wavetable.cpp`).

This node replaced the P2 Oscillator outright. The Oscillator's param set,
its free-running/note-driven split and its 2-op FM survive inside each
engine; what it could not do — morph a table, layer two engines, or shape
either one over time — is the reason for the replacement.

**Global (in the `master` panel, below both engines):** mix A↔B, volume,
frequency (greyed out once a note cable drives pitch), glide.

**Per engine, top to bottom:**

| Band | Contents |
|---|---|
| header, one line | on/off checkbox, table dropdown, fine, `oct ±n` dropdown, `semi ±n` dropdown |
| visualizer | the table's frames, draggable — this is `position`'s primary control |
| knob row 1 | volume, position, unison, detune |
| knob row 2 | filter (dropdown over knob), warp (dropdown over knob), phase, pan |
| panel | filter / warp / voice — resonance, warp ratio, stereo width, phase random |
| panel ×3 | amp, pitch and filter envelopes: curve left, fields right |

Two rows of four, not four rows of four. The four-row version put `filter`
and its type selector on separate rows from `warp` and its selector, so each
knob read as belonging to whatever sat above it; `AudioKnobRow::DropdownKnob`
puts the selector directly over its knob and the pair reads as the one
decision it is. The row's `headerRowH` drops the plain knobs beside them so
every cap and caption still shares one baseline.

The **dropdown of a DropdownKnob is never disabled**, only its knob. The knob
is meaningless exactly when the mode is "off", and greying the whole cell
would leave no way to select any other mode — the trap the first version
shipped with.

Envelope fields sit **to the right of the curve**, not under it. Under it,
each envelope was ~150px tall and the third one pushed the node past any
reasonable height. `DrawEnvelopePanel` sizes the curve to exactly the height
of the field stack beside it, so the panel has one bottom edge. The three
curves are drawn in three colours — blue, amber, green — so stacked panels are
told apart by their trace rather than by reading three headers.

**Node-wide controls (mix A↔B, volume, freq, glide) and the output scope go
in a `master` panel at the *bottom*.** They were above the engines first,
which put four knobs and a scope between the node's title and the first thing
the node is about. None of them is removable — `mix` is the reason there are
two engines, `glide` is per-voice pitch, `freq` is the free-running voice's
pitch — but their prominence was wrong, so they sit under the pair they apply
to.

**A/B as two side-by-side columns** (`BeginAudioColumns`, §1k), not tabs. Both
engines draw every frame, which is what keeps their parameter indices distinct
— with tabs, a modulator patched to one engine's attack would follow the tab
switch onto the other's. Splitting them into two cabled nodes would instead
let the two engines' voice allocations drift apart under voice stealing, which
is the one thing a layer pair must not do.

**Deliberately not on this node**, per §5's rule against duplicating another
node's job:
- **No per-engine LFO.** That is the **Envelope**/modulator path, which
  already drives *any* param through the `ModKnob` pin §2 describes.

### 8a. The per-engine filter (v5) — a reversal, and why

v3 said flatly: *no per-engine filter, `README.md` §3 consolidates every
filter into the dedicated Audio Filter node.* That is now wrong, and the node
carries an eleven-type filter (LP/HP at 12/24/36 dB, BP at 12/24, notch at
12/24) plus its own filter envelope per engine.

The rule it broke was the right rule for a *chain*: a filter that sits after
the synth is one filter, and it belongs to the node whose whole job is
filtering. It is the wrong rule for a *layer pair*. The two engines exist so
that one can be the body and the other the top, and a downstream filter node
sees only their sum — it cannot open on the top layer while the body stays
dark, which is the specific thing a two-engine patch is built to do. The same
argument that put the amp and pitch envelopes on the node (below) applies
verbatim: the shaping is per engine and per voice, and neither of those can be
expressed by anything downstream, because a downstream node sees one signal
for the whole node rather than one per sounding voice.

Slope is spelled into the type name ("lp 24" is one menu entry, not a shape
plus a pole count) because that is how it is labelled on hardware and in every
plugin filter menu, and because not every shape gets every slope — a 36 dB
bandpass is six poles of skirt around an already-narrow band, i.e. a resonant
sine with extra steps. See `src/audio/SynthModes.h`.

The **Audio Filter** node keeps its job unchanged: filtering a signal *chain*,
including this node's output. What changed is that the synth also shapes its
own two layers before they are summed.

### 8b. Warp is a mode list, not a knob per algorithm (v5)

v3's `warp` was a single bipolar phase-bend. The node now has sixteen warp
modes behind one dropdown and one depth knob: off, FM/AM/RM/PD (reading the
*other* engine), hard sync, bend ±, asym ±, flip, mirror, quantize, rectify,
odd only, even only.

One mode selector plus one depth rather than a knob per algorithm, because the
modes are alternatives that are never combined, and fifteen permanently-zero
knobs is precisely what a mode list exists to avoid (§5). The contract that
makes the dropdown safe to sweep: **at zero depth every mode is bit-identical
to "off"**, enforced by an `amount <= 0` early-out in both halves of the warp
and checked by `INFINITE_DSPTEST=1` ("wavetable warp neutral at zero depth").
Without it, selecting a mode would change the sound before its depth knob had
been touched.

The amp and pitch envelopes *are* on the node, which reverses the Oscillator's
"no per-oscillator envelope" rule — deliberately. That rule was right when
there was one oscillator and one envelope: a node-local envelope would have
been a worse copy of the Envelope node patched permanently in. It is wrong for
a two-engine node, because the whole point of the pair is that each engine is
shaped *differently* over time, and per-voice envelopes cannot be expressed by
a downstream modulator at all — a modulator sees one value for the whole node,
not one per sounding voice. The Envelope node remains the right tool for
shaping anything downstream of the synth.

### 8c. Verifying interactivity (v5)

A screenshot proves a visualizer is in the right place. It cannot prove that
dragging it moves *this* engine's parameter, and that is the half that broke.
`INFINITE_WTDRAGTEST=1` drives a synthetic mouse over one Wavetable and
checks three gestures, each asserting the other engine did not move:

1. scrub engine A's table picture → `a.position` follows, `b.position` frozen;
2. drag engine A's amp sustain handle vertically → `a.ampSustain` rises;
3. drag engine B's *filter* envelope attack horizontally → `b.filterAttack`
   rises, `a.filterAttack` frozen.

Two things to know before adding a case to it:

- **Item rects captured inside `ed::Begin`/`ed::End` are canvas-space**, and
  the synthetic mouse speaks screen pixels. `ed::CanvasToScreen` **hangs** the
  app when called from inside the node draw or from before `ed::Begin`; the
  one place it is safe is the post-editor block, which is where the rects are
  converted. Never convert at capture time.
- **The ADSR corner test resolves on movement, not on the press.** The
  decay/sustain corner carries decay along x and sustain along y, and a press
  *on* the corner has zero offset in both, so deciding from the press offset
  always resolved to decay — dragging straight down from the corner moved
  nothing. `DrawEditableADSR` now defers the axis choice until the pointer has
  moved 3px, and writes nothing until it has.

## 9. Checking a node by eye

```bash
open -n --env INFINITE_AUDIOUITEST=1 -a build/Infinite.app
```

Spawns one of every audio/note node the grammar covers, wired into a working
chain, and frames the lot. Unlike the other `INFINITE_*` fixtures it does not
exit — it leaves the window open on a populated canvas, so a layout change
can be judged against every node body at once. Add new node types to that
block in `main.cpp` as P3b–e lands them.

Acceptance test for any new audio node, in order:

1. Every horizontal element in the body starts at the same x and ends at the
   same x. If anything is ragged, something is not deriving from
   `gAudioBodyW`/`gAudioContentW`.
2. The node is exactly `kAudioNodeWidth` or `kAudioNarrowWidth` wide. If it
   is wider, a row is setting the width instead of fitting it.
3. Every knob caption in a row shares one baseline.
4. The visualizer shows something meaningful with no audio running.
5. Hovering any control puts its value in the readout strip, and nothing
   floats near the cursor.
