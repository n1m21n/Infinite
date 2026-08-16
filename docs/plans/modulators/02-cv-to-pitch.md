# Prompt: new node — CV to Pitch

In /Users/namansoni/infinte. Read this whole file, then the
**`new-audio-node` skill**, before writing code. This is a Modulators-category
node with no audio-thread counterpart, so most of that skill's two-object
rule doesn't apply — but its wiring checklist (registration site, help
text, palette, save/load, teardown) does, and the exit criterion is the
same sweep.

## Goal

A shaper node that sits between a mod source and a mod destination and
**quantizes a continuous modulation signal to semitone steps** over a
user-chosen range. Turn a smooth LFO into a stepped arpeggio-like contour;
turn a Random node into notes of a scale rather than microtonal drift.

```
LFO → [CV to Pitch: −12..+12 st, minor scale] → synth pitch / filter / anything
```

## Contract — read this carefully

Modulator cables carry **0..1** (`IModulator::Value01()`,
`src/core/Modulation.h:14-21`). This node does **not** break that: it
outputs 0..1 like everything else. What it does is *quantize where within
that 0..1 the value is allowed to land*, so that the span maps onto whole
semitones.

Concretely, with `rangeLow = -12`, `rangeHigh = +12` (25 semitones
inclusive):

```
semitoneCount = rangeHigh - rangeLow + 1          // 25
idx           = round(v01 * (semitoneCount - 1))  // 0..24, snapped
semitone      = rangeLow + idx                    // -12..+12, for display
out01         = idx / (semitoneCount - 1)         // back to 0..1, quantized
```

So downstream, `out01` is still 0..1 but only ever takes 25 discrete
values. Bound to a destination whose own range happens to be ±12
semitones, each step is exactly one semitone. Bound to a filter cutoff,
you get 25 stepped values — also useful. **The node displays the semitone
number** (`+7 st`) so the musical intent is legible even when the
destination isn't a pitch parameter.

Do not invent a second modulator output type or a "semitone cable". One
contract, one cable.

## What to build

`class CVToPitchNode : public INode, public IModulator` in
`src/nodes/ModulatorNodes.h` / `.cpp`, alongside the other pure-modulator
shapers. Follow `ModDepthNode` (`ModulatorNodes.h:286-310`) as the exact
structural template — it is the closest existing node: one modulator
input slot, a `constantIn` fallback, no texture, no cook.

Params (six — keep it to this, per the project's node-minimalism rule):

| Param | Type | Range | Notes |
|---|---|---|---|
| `constantIn` | float | 0..1 | used when nothing is patched, like every other shaper |
| `rangeLow` | int | −48..48 | semitone mapped to input 0.0 |
| `rangeHigh` | int | −48..48 | semitone mapped to input 1.0; clamp so `high > low` |
| `scale` | int | `MusicTime::ScaleType` | `13` = `kChromatic` = every semitone = "off". Same convention as `NoteFilterNode` (`src/nodes/NoteNodes.h:200`) |
| `root` | int | 0..11 | 0 = C. Only meaningful when `scale != kChromatic` |
| `glideMs` | float | 0..2000 | one-pole smoothing toward the new step. 0 = hard steps. Mirrors `NoteToCVNode::glideMs` (`NoteNodes.h:164`) |

When `scale != kChromatic`, snap `semitone` to the nearest degree of that
scale relative to `root` **before** converting back to `out01`. Reuse
whatever `MusicTime` helper `NoteFilterNode` uses for its scale gate —
find it in `src/nodes/NoteNodes.cpp`; do not write a second scale table.

Note `glideMs` interacts with quantization: glide smooths the *output*,
so with glide > 0 the output passes through non-quantized values in
transit. That's correct and desirable (it's portamento). Do not quantize
after the glide.

## UI

Body draws: the standard modulator meter (`DrawModulatorMeter`,
`src/main.cpp:12532`) plus a **large semitone readout** — the current
snapped value as `+7 st` / `−3 st` / `0 st`. That readout is the point of
the node; make it the visually dominant element, not a text line under
six sliders. See the **`audio-node-ui` skill** for the readout/typography
conventions.

## Wiring checklist

- `REGISTER_NODE(CVToPitchNode, CV to Pitch, "Modulators");` next to the
  others at `src/main.cpp:2323-2324`.
- A `DrawCVToPitchParams` function next to `DrawModDepthParams`
  (`src/main.cpp:3481`), dispatched from the `dynamic_cast` chain around
  `src/main.cpp:26220`.
- Help text in **both** places — `src/main.cpp:12701`-ish and `13130`-ish
  lists. Grep for a neighbouring modulator's description string to find
  both.
- Add to `src/CMakeLists.txt` only if you create new files; putting it in
  `ModulatorNodes.h/.cpp` avoids that.

## Exit criteria

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` clean.
2. LFO → CV to Pitch → a synth's pitch/detune param, range −12..+12,
   chromatic: audibly steps through semitones, no glide artefacts.
3. Switch scale to minor: only scale degrees are produced.
4. All six params survive save → quit → reload.
5. Spawn, wire, and delete the node while the transport is running — no
   crash, no dangling cable.
6. Run `/audio-node-sweep` (covers 4 and 5 mechanically) and then
   `/run-infinite-hygiene`.
