# Fix: ADSR curve draws a phantom decay ramp when decay is 0 ms

## Context (confirmed against the code, not assumed)

Every ADSR editor in Infinite goes through **one** function:
`DrawEditableADSR` at [src/main.cpp:4893](src/main.cpp:4893), wrapped by
`DrawEnvelopePanel` at [src/main.cpp:5094](src/main.cpp:5094). There are
exactly four call sites, all via `DrawEnvelopePanel`:

- [src/main.cpp:5269](src/main.cpp:5269) — Wavetable amp envelope
- [src/main.cpp:5275](src/main.cpp:5275) — Wavetable pitch envelope
- [src/main.cpp:5281](src/main.cpp:5281) — Wavetable filter envelope
- [src/main.cpp:6684](src/main.cpp:6684) — Envelope node (`DrawEnvelopeBody`)

So this is a single-function fix, not a per-node sweep. There is no second
read-only ADSR renderer — comments at lines 5875 and 6261 mention a
`DrawADSRVisualizer`, but that function no longer exists; those are stale
comment references and can be left alone (or corrected in passing).

## The bug, with the arithmetic

`DrawEditableADSR` lays handles out in three fixed slots
([src/main.cpp:4917-4932](src/main.cpp:4917)):

```cpp
const float seg = (w - 12.0f) / 3.0f;
auto TimeToX = [&](float ms, float slot) {
   return origin.x + 6.0f + slot * seg + std::clamp(ms / maxTimeMs, 0.0f, 1.0f) * seg;
};
const float ax  = TimeToX(*attackMs,  0.0f);   // attack corner
const float dx  = TimeToX(*decayMs,   1.0f);   // decay/sustain corner
const float sx  = origin.x + 6.0f + 2.0f * seg; // shelf right end (fixed)
const float rx  = TimeToX(*releaseMs, 2.0f);   // tail
```

The `slot * seg` term advances x **whether or not the stage has any
duration**. Walk it with A=0, D=0, S=0, R=0:

| point | x | y |
|---|---|---|
| start | `x0` | baseY |
| attack corner | `x0 + 0` | topY |
| decay corner | `x0 + seg` | baseY (sustain 0) |
| shelf end | `x0 + 2·seg` | baseY |
| tail | `x0 + 2·seg` | baseY |

That is exactly the screenshot: vertical rise, then a **diagonal fall
spanning a full third of the box**, then a flat line with two dots on it.
The attack and release edges are correct (both are measured from a fixed
anchor, so 0 ms → zero width). **Only the decay segment is wrong**, and it
is wrong at every value, not just 0: its drawn horizontal length is
`seg + (d/max)·seg − (a/max)·seg`, which is never less than `seg`. A 0 ms
decay is drawn the same width as a 1333 ms one.

The reference screenshot the user supplied (an ADSR modulator UI with A/D/S/R
knobs at minimum) shows the correct behaviour: an instant edge, then the
level line — no ramp with visible run. **I could not run Bitwig to verify its
exact pixel treatment; treat the reference image as the spec, and if you can
check a real DAW, do.**

## What to change

Replace the fixed-slot x layout with a **cumulative** one, where each stage's
width is proportional to its own time and each handle starts where the
previous one ended. This is what every real envelope editor does, and it is
what makes a zero-length stage render as zero-length.

Suggested geometry (this is my recommendation, not the only workable one —
but adopt it wholesale unless you find a concrete problem):

```
usable      = w - 12
shelf       = usable * 0.18f          // fixed-width sustain hold, always visible
seg         = (usable - shelf) / 3    // max width of each of A, D, R
x0          = origin.x + 6
ax          = x0 + frac(attack)  * seg
dx          = ax + frac(decay)   * seg
shelfEndX   = dx + shelf
rx          = shelfEndX + frac(release) * seg
```

where `frac(ms) = clamp(ms / maxTimeMs, 0, 1)`. Max total is
`3·seg + shelf = usable`, so it can never overflow the box — no clamping
needed at the right edge.

The comment at [src/main.cpp:4913-4916](src/main.cpp:4913) warns that a shared
normalisation "would make dragging one handle move the other two." That
concern is about **parameter cross-talk**, and the cumulative layout does not
reintroduce it: dragging the decay handle changes only `*decayMs`. Downstream
handles shift *visually* because the decay stage got longer, which is correct
and expected. Rewrite that comment to say this rather than deleting it — the
next person will otherwise re-break it back to slots.

### Drag mapping must move with the layout

The drag code at [src/main.cpp:4978-4990](src/main.cpp:4978) computes `t01`
from a fixed slot origin:

```cpp
const float t01 = [&](float slot) {
   return std::clamp((m.x - origin.x - 6.0f - slot * seg) / seg, 0.0f, 1.0f);
}(sHeld == 0 ? 0.0f : (sHeld == 1 ? 1.0f : 2.0f));
```

Change it to measure from each handle's own anchor:

- handle 0 (attack): anchor `x0`
- handle 1 (decay): anchor `ax`
- handle 3 (release): anchor `shelfEndX`

Handle 2 (sustain) is y-only and needs no change. Keep the latch/axis-deferral
logic at lines 4939-4977 exactly as is — it is solving a real problem
(a press on the decay corner is ambiguous between decay and sustain) and is
independent of this change.

### The drag test recomputes this geometry and will break

`INFINITE_WTDRAGTEST` re-derives the handle positions by hand at
[src/main.cpp:16771-16780](src/main.cpp:16771):

```cpp
const float seg   = (ampW - 12.0f) / 3.0f;
const float susX  = ampA.x + 6.0f + seg + (wt->engines[0].ampDecay / 4000.0f) * seg;
const float fbSeg = ((filtB.z - filtB.x) - 12.0f) / 3.0f;
const float fbAttackX = filtB.x + 6.0f + (wt->engines[1].filterAttack / 4000.0f) * fbSeg;
```

Update both to the new formulas or the test aims at empty space and fails.
`fbAttackX` (attack, anchored at `x0`) only needs the new `seg`; `susX`
(decay corner) needs the full cumulative expression including `ampAttack`.
Phase 3 also steps by `fbSeg * 0.30f / 0.55f` at lines 16803-16804 — with the
narrower `seg` those offsets still land inside the widget, but re-check that
the resulting attack value stays under 4000 ms so the drag actually registers
a change.

The cleanest way to avoid this class of breakage permanently: extract the
layout into a small `struct AdsrLayout { float x0, ax, dx, shelfEndX, rx,
topY, baseY, span, seg; }` plus a free function that computes it from
(rect, a, d, s, r, maxTimeMs), and have both `DrawEditableADSR` and the test
block call it. That is a judgment call — do it if it doesn't balloon the
diff, otherwise just keep the two copies in sync and say so in a comment.

## Out of scope

- Do **not** change any envelope defaults. `WavetableNode.h:65-68` already
  ships sensible non-zero amp values (4 / 300 / 0.75 / 260); the all-zero
  state in the screenshot is user-set, not a bad default.
- Do **not** touch the DSP. `Envelope::Process` in
  [src/audio/AudioVoice.h](src/audio/AudioVoice.h) handles zero-length stages
  correctly already (`SegmentInc` of 0 ms jumps the level in one sample).
  This is purely a rendering/hit-testing fix.
- Do not restyle the panel, the fields, or the section header.

## Done means

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

compiles clean, then:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

`WTDRAGTEST` must still pass (it is in the driver's test list at
`.claude/skills/run-infinite-hygiene/driver.sh:85`). Note the driver has two
known pre-existing `AUDIOPARAMSWEEPTEST` baseline failures (Dynamics, and nine
Sampler params) documented at lines 174 and 188 — those are not yours.

Finally, launch the app, drop an Envelope node, drag all four handles to
zero, and confirm the curve reads as a single instant spike to a flat zero
line with no diagonal run. Then set decay to ~50 ms and confirm the ramp is
visibly short rather than a third of the box. Copy the built app to
`~/Desktop/Infinite.app` as usual.
