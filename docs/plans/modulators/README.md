# Modulator work — plan index

Five pieces of work, in dependency order. Each numbered file is a
self-contained prompt: paste it into a fresh Claude Code session, it
contains its own verified file:line references and exit criteria.

**Do 00 first.** It settles what a modulator cable actually carries. 01 and
04 both assume the answer.

| # | File | What it is |
|---|------|-----------|
| 00 | [00-modulation-polarity.md](00-modulation-polarity.md) | Settle the bipolar/unipolar question. Adds a per-binding polarity mode. |
| 01 | [01-pattern-step-sequencer.md](01-pattern-step-sequencer.md) | Rebuild Pattern's UI as draggable vertical bars, bipolar-aware, bar-length-aware. |
| 02 | [02-cv-to-pitch.md](02-cv-to-pitch.md) | New node: quantize a modulator to semitones / a scale. |
| 03 | [03-envelope-to-shaper.md](03-envelope-to-shaper.md) | Convert Modulators/Envelope from note-driven to a mod-in → mod-out ADSR shaper. |
| 04 | [04-curve-shaper.md](04-curve-shaper.md) | New node: interactive transfer curve on a modulator signal. |

02, 03 and 04 are the three "shaper" nodes that sit between a mod source
and a mod destination, alongside the existing Mod Depth:

```
source  →  [ Mod Depth | Envelope (shaper) | Curve | CV to Pitch ]  →  destination
```

---

## The polarity question — recommendation

Short version: **keep the wire at 0..1, and put polarity on the *binding*,
not on the signal.**

The confusion is real, but it isn't actually about the range of the number
on the cable. Two different things get called "bipolar":

1. **The shape of a source** — a sine that swings either side of a centre.
   This is already expressible in 0..1: it swings around 0.5. Nothing is
   lost. `LFONode` already does exactly this.

2. **The effect on the destination** — "push the cutoff *up and down from
   where I left the knob*". This is what's actually missing, and *changing
   the wire's range does not fix it*, because today a binding **overrides**
   the destination outright:

   ```cpp
   // main.cpp:28369
   const float v01 = std::min(1.0f, std::max(0.0f, modulator->Value01()));
   *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
   ```

   The knob's own position is discarded the instant a cable lands. That's
   why `ModDepthNode` has to do the awkward "collapse toward 0.5" trick —
   see its comment at `src/nodes/ModulatorNodes.h:276-285`, which spells
   this out. Depth-toward-centre is a workaround for a missing binding
   mode, not a design.

So: making `Value01()` return -1..1 would touch 21 implementers, break
every saved patch's physical behaviour, and *still* leave you unable to
say "±3 semitones around where the knob is". It solves the wrong problem.

### What to do instead

Give each binding a **polarity mode** (see 00 for the full prompt):

- **Absolute** (today's behaviour, the default, what every existing patch
  loads as): `dst = min + (max-min) * v01`.
- **Bipolar / relative**: `dst = knob + (v01 - 0.5) * 2 * depth * (max-min)`,
  clamped to the destination's range. The knob stays live and meaningful;
  the modulator adds and subtracts around it. This is the standard synth
  mod-matrix model and it's what "bipolar" actually means to a musician.

0..1 on the wire stays the universal contract — any source into any
destination, no adapters, no per-node polarity flags to keep in sync. The
consistency you liked is preserved. Bipolarity becomes a property of *how
the connection behaves*, which is where it belongs and where it's
per-connection adjustable.

Everything else follows cleanly from this:

- Pattern's bars can render bipolar (centre line, bars growing up **and**
  down) while still *storing* 0..1 — no serialization change, and a
  negative-looking bar genuinely subtracts once the binding is in bipolar
  mode (01).
- Curve's editor gets a 0.5 gridline and reads as a transfer function
  around centre (04).
- Mod Depth's "collapse toward 0.5" hack stops being a hack: in bipolar
  mode it becomes an honest depth control.

### Also worth doing, cheaply

`docs/plans/data-type-modernization/04-modulator-bipolar-contract.md`
already proposed removing the defensive clamp at `main.cpp:28369` so that
an intentionally out-of-range source (`InvertNode`, or `RangeToRangeNode`
with `clampOutput=false`) can extrapolate past a destination's nominal
range instead of being silently flattened. That's folded into 00 as a
second step — it's small and independent.

That older doc's **Option B** (renaming `Value01()` to `Value()` and going
unbounded across all implementers) is explicitly **rejected** by this plan.
If a future session proposes it, point it here.
