# Prompt: new node — Curve (modulator transfer curve)

In /Users/namansoni/infinte. Read this whole file before writing code.

## Goal

A shaper node with one job: **remap a modulation signal through an
editable curve**. Input 0..1 on the x axis, output 0..1 on the y axis,
with draggable control points. Exponential response on a filter sweep,
an S-curve to make an LFO snappier at the extremes, a stepped staircase,
an inverted ramp — all things sliders can't express.

This is the third of the three shaper nodes (Mod Depth, Envelope, Curve).

## The key insight: the curve editor already exists

`CurvesNode` (`src/nodes/CurvesNode.h`, `src/nodes/CurvesNode.cpp`) is the
Photoshop-style per-channel curve node in the **Color** category. It
already implements everything hard about this:

- `struct Point { float x, y; }`, kept sorted by x
- `AddPoint` / `MovePoint` / `RemovePoint` / `ResetChannel` — with the
  endpoint and neighbour-crossing rules already correct
  (`CurvesNode.h:47-50`)
- `float Evaluate(int channel, float x) const` (`CurvesNode.h:54`) — the
  spline evaluator, already shared between the shader LUT and the widget
- serialization of the point list — find it in `CurvesNode.cpp`'s
  `VisitParams`

And the interactive widget is `DrawCurveEditor` (`src/main.cpp:3898-3992`),
drawn from `DrawCurvesParams` (`src/main.cpp:3994-3999`).

**Reuse this. Do not write a second spline implementation.**

The cleanest route is to factor the point-list + evaluator out of
`CurvesNode` into a small shared struct (e.g. `struct CurveShape` in a new
`src/core/CurveShape.h`) that both nodes own, and generalise
`DrawCurveEditor` to take a `CurveShape&` plus a colour instead of a
`CurvesNode*`. If that refactor turns out to be more invasive than it
looks, the acceptable fallback is for the new node to hold its own
`std::vector<CurvesNode::Point>` and call a lifted copy of `Evaluate` —
but try the shared struct first and say which you ended up doing.

Critically: `CurvesNode`'s own behaviour, its LUT texture, and its
`mLutDirty` invalidation (`CurvesNode.h:56`) must be **unchanged** by any
refactor. That caching is load-bearing — see the `new-effect-node` skill's
caching bug trap.

## What to build

`class ModCurveNode : public INode, public IModulator` in
`src/nodes/ModulatorNodes.h` / `.cpp`. Structural template:
`ModDepthNode` (`ModulatorNodes.h:286-310`).

```cpp
IModulator* input = nullptr;
IModulator** ModulatorInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
int ModulatorInputCount() const override { return 1; }
const char* InputLabel(int) const override { return "in"; }
float constantIn = 0.5f;
float mix = 1.0f;   // 0 = curve bypassed (straight line), 1 = fully applied
```

`Value01()`:

```cpp
const float in = input ? input->Value01() : constantIn;
const float x  = std::min(1.0f, std::max(0.0f, in));   // clamp for lookup only
const float y  = curve.Evaluate(x);
return in + (y - in) * mix;
```

Note the input is clamped **for the curve lookup** (the curve is only
defined on 0..1) but `mix` interpolates against the unclamped `in`, so an
out-of-range source degrades sensibly rather than snapping.

Three controls total: the curve, `mix`, `constantIn`. Add nothing else —
no presets dropdown, no per-axis range, no smoothing. If it needs range
remapping the user already has `RangeToRange`.

## UI

- The curve editor is the node's body and should be the dominant element,
  square-ish, as large as the node width allows. Read the
  **`audio-node-ui` skill** for the body/section conventions.
- Draw a **0.5 crosshair gridline** on both axes. Under bipolar bindings
  (`00-modulation-polarity.md`) the centre is where "no modulation" lives,
  so it needs to be visible — this is what makes the curve readable as a
  bipolar transfer function.
- Draw a **live dot on the curve at the current input x**, so you can see
  where the signal is sitting as it moves. This is the single detail that
  makes the node feel alive rather than static, and `CurvesNode`'s editor
  has no equivalent because an image node has no "current value".
- Double-click a point to remove it, click empty space to add, drag to
  move — whatever `DrawCurveEditor` already does; inherit it.
- Right-click the editor → reset to a straight line.

## Wiring checklist

- `REGISTER_NODE(ModCurveNode, Curve, "Modulators");` near
  `src/main.cpp:2323`. **Check for a name clash first** — `CurveNode`
  already exists as a 3D geometry source (`src/nodes/CurveNode.h:14`), and
  `Curves` exists in Color (`src/main.cpp:2245`). If "Curve" is taken in
  the palette, use `Mod Curve`.
- `DrawModCurveParams` next to `DrawModDepthParams` (`src/main.cpp:3481`),
  dispatched from the `dynamic_cast` chain near `src/main.cpp:26220`.
- Help text in **both** description lists (near `src/main.cpp:12701` and
  `13130`).

## Exit criteria

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` clean.
2. `CurvesNode` in the Color category behaves identically to before —
   spot-check an image patch that uses it, and confirm its LUT still
   updates when you drag a point (i.e. `mLutDirty` invalidation survived).
3. LFO → Curve → filter cutoff: reshaping the curve visibly changes the
   sweep's contour in real time.
4. The live dot tracks the input.
5. Add several points, save, quit, reload — the exact curve comes back.
6. `mix = 0` is a true bypass (output == input).
7. Spawn / wire / delete during playback — no crash, no dangling cable.
8. Run `/audio-node-sweep`, then `/run-infinite-hygiene`.
