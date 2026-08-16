# Range to Range — drop the input range, make the output range unbounded

## What's wrong today

`RangeToRangeNode` (declared `src/nodes/ModulatorNodes.h:231-258`, evaluated
`src/nodes/ModulatorNodes.cpp:215-223`, drawn `src/main.cpp:3641-3652`) exposes
four range params:

```cpp
ModSlider("in low",   &n->inLow,   -4.0f, 4.0f);
ModSlider("in high",  &n->inHigh,  -4.0f, 4.0f);
ModSlider("out low",  &n->outLow,  -4.0f, 4.0f);
ModSlider("out high", &n->outHigh, -4.0f, 4.0f);
```

Two problems:

1. **The input range is dead weight.** Every modulator in this app produces a
   normalised `Value01()`. The only sources that leave 0..1 are the ones that do
   it deliberately (`InvertNode`, an unclamped `RangeToRangeNode` — see
   `docs/plans/modulators/00-modulation-polarity.md` §4), and for those the
   right behaviour is to *extrapolate*, not to be rescaled by a hand-set input
   window. `in low` / `in high` are four extra widgets that exist only to be
   left at their defaults.

2. **The output range is capped at ±4 and cannot be typed past it.** The user's
   actual ask — map 0..1 onto **-100.55 .. 120.65** — is unreachable. Dragging
   is capped by `SliderFloat`, and typed entry is capped too: the typed-value
   path in `ModSlider` hard-clamps at `src/main.cpp:1123`:

   ```cpp
   *value = std::clamp(parsed, std::min(minV, maxV), std::max(minV, maxV));
   ```

   So even double-clicking the field and typing `120.65` silently snaps to
   `4.000`. There is currently **no** widget in the app that accepts an
   arbitrary-magnitude float on a modulatable param.

## Target behaviour

The node becomes: *"take the incoming 0..1 signal and stretch it onto any two
numbers I type."*

- Input is assumed normalised 0..1. No `in low` / `in high` controls at all.
- `out low` and `out high` accept **any** float, positive or negative, with
  decimals, typed or dragged. No hard cap.
- `clamp to out range` stays, still defaulting on, still clamping to
  `[min(outLow,outHigh), max(outLow,outHigh)]` — so an inverted range
  (`out low` 120.65, `out high` -100.55) keeps working as a flip.
- An input that legitimately leaves 0..1 extrapolates past the output range
  when `clamp to out range` is off. That is the existing contract and must not
  regress.

## Work

### 1. Trim the node

`src/nodes/ModulatorNodes.h:249-257` — delete `inLow` / `inHigh` and their two
`v.Float(...)` lines from `VisitParams`. Patch load is name-keyed
(`src/core/Patch.cpp:123-128`: missing key leaves the default, unknown key in
the file is ignored), so old `.patch` files load cleanly without a migration.

`src/nodes/ModulatorNodes.cpp:215-223` becomes:

```cpp
float RangeToRangeNode::Value01()
{
   // Input is contractually normalised, so t is the input itself. Deliberately
   // NOT clamped to 0..1: InvertNode and an unclamped RangeToRangeNode upstream
   // are allowed to leave the contract, and extrapolating past the output range
   // is the whole point of clampOutput being switchable.
   const float t = input ? input->Value01() : constantIn;
   const float r = outLow + (outHigh - outLow) * t;
   if (!clampOutput) return r;
   return std::min(std::max(outLow, outHigh), std::max(std::min(outLow, outHigh), r));
}
```

`src/main.cpp:3641-3652` — drop the two `in low` / `in high` `ModSlider` calls.

### 2. Add an unbounded numeric mode to `ModSlider`

This is the substantive part. Do **not** write a bespoke widget for this node —
`ModSlider` (`src/main.cpp:991`) owns the modulation pin, the typed-edit /
expression field, undo checkpoints, and the hotkey path. A parallel widget would
have to reimplement all of it and would be the third near-copy of that code.

Extend the existing signature:

```cpp
bool ModSlider(const char* label, float* value, float minV, float maxV,
               const char* fmt = "%.3f", float width = kParamWidth,
               bool audioStyle = false, bool unbounded = false);
```

`unbounded == true` changes exactly two things:

- **The widget.** Swap `ImGui::SliderFloat` for `ImGui::DragFloat` with
  `v_min == v_max == 0.0f` (ImGui's "no limits" form). Scale the drag speed to
  the current magnitude so the same gesture is usable at 0.5 and at 120.65:
  `const float speed = std::max(0.01f, std::fabs(*value) * 0.01f);`
- **The typed-entry clamp.** At `src/main.cpp:1123`, skip the `std::clamp` and
  assign `parsed` directly. This is the line that currently makes typing
  `120.65` impossible.

Everything else — pin drawing, `ParamRef` registration, expression handling,
`PushUndoCheckpoint`, `%.3f` formatting — stays shared and untouched.

Call it as:

```cpp
ModSlider("out low",  &n->outLow,  -4.0f, 4.0f, "%.3f", kParamWidth, false, true);
ModSlider("out high", &n->outHigh, -4.0f, 4.0f, "%.3f", kParamWidth, false, true);
```

### 3. Decide what `minV`/`maxV` mean once the param is unbounded

`minV`/`maxV` are not only slider bounds — they are stored on `ParamRef`
(`src/main.cpp:1001-1002`) and consumed by the modulation apply pass:

- `src/main.cpp:28936` — a `kAbsolute` binding sets
  `*value = minValue + (maxValue - minValue) * v01`
- `src/main.cpp:28931-28932` — a `kBipolar` binding swings by
  `depth * (maxValue - minValue)` and clamps into `[minValue, maxValue]`
- `src/main.cpp:28958-28965` — expressions bind `lo`/`hi` to them and clamp the
  result into that window

So they cannot simply be dropped. **Keep passing `-4.0f, 4.0f` and treat it as a
declared *soft* range**: typing and dragging are unbounded, but wiring a cable
into `out low`'s pin sweeps -4..4, and `=lerp(lo, hi, ...)` means -4..4.

Document that in a comment at the call site, because it is a real and visible
asymmetry: a user who has typed `120.65` and then patches an LFO onto that pin
will see the value jump down into ±4. Two things follow:

- This is the correct trade for a first pass — the alternative (deriving the
  soft range from the current value) makes the modulation depth depend on
  edit order, which is worse.
- If the asymmetry proves annoying in use, the follow-up is to widen the soft
  range for these two params only, not to make it dynamic.

### 4. Known trap: param indices shift

Modulation bindings persist **positionally**, by param index —
`src/core/Patch.cpp:212` writes `mod <dstIndex> <dstParam>`, and `dstParam` is
`gParamCounter`'s value at draw time (`src/main.cpp:995`). Removing two
`ModSlider` calls renumbers this node's params: `out low` moves 3→1, `out high`
4→2, `clamp` 5→3.

Consequence: an existing patch with a modulator wired to `out high` will, after
this change, reattach that cable to `in low`'s old slot — silently, to the wrong
control. Decide explicitly and say which you did in the commit message:

- **Accept it** (recommended — this node is rarely modulated, and the app is
  pre-release), or
- Keep `inLow`/`inHigh` as hidden fields drawn behind an `if (false)` to hold
  their indices. Ugly, and it strands two dead pins.

## Verify

Machine-checkable exit criterion — the node must survive the existing harness:

```bash
.claude/skills/run-infinite-hygiene/driver.sh
```

That covers patch save/load round-tripping and the full node-type sweep, so a
broken `VisitParams` or a crash on delete shows up there.

Beyond that, check by hand in the running app:

1. Spawn Range to Range. Confirm only `in (no cable)`, `out low`, `out high`,
   `clamp to out range` are present.
2. Double-click `out low`, type `-100.55`. It must read `-100.550`, not `-4.000`.
   Same for `out high` = `120.65`.
3. Wire an LFO into `in`. The node's value readout
   (`src/main.cpp:12897`, `ImGui::Text("%.3f", value)`) must sweep between
   `-100.550` and `120.650`.
4. Save, quit, reload. Both numbers must come back exactly.
5. Set `out low` 120.65 / `out high` -100.55 (inverted) and confirm the sweep
   flips and still clamps to the same two numbers.
6. Turn `clamp to out range` off, feed it an unclamped source that leaves 0..1,
   and confirm the output extrapolates past `out high` rather than flattening.

**Expected, not a bug:** the node's preview graph will now permanently draw its
amber "out of 0..1 contract" border and hairlines (`src/main.cpp:12883-12895`).
A node whose whole job is to output -100.55..120.65 *is* out of contract; the
warning is telling the truth. Leave it.

## Out of scope

- Changing `MathNode` / `InvertNode` / any other modulator's ranges. If the
  unbounded `ModSlider` mode turns out to be wanted elsewhere, that is a
  separate pass — this change only adds the capability and uses it on two params.
- A unit suffix / display-format control on the output. Ask for it separately if
  the raw `%.3f` reads badly at these magnitudes.
