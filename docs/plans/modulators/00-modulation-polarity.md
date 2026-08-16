# Prompt: per-binding modulation polarity

In /Users/namansoni/infinte. Read this whole file before writing code.

## Goal

A modulator cable stays contractually **0..1** (`IModulator::Value01()`,
`src/core/Modulation.h:14-21`). Do **not** change that interface, do not
rename it, do not make it unbounded — an earlier proposal to do so
(`docs/plans/data-type-modernization/04-modulator-bipolar-contract.md`,
"Option B") is rejected; see `docs/plans/modulators/README.md` for why.

Instead, add a **polarity mode to each modulation binding**, so the same
0..1 source can either take over a parameter outright (today) or swing it
around wherever its knob is sitting.

## Verified current state

- `IModulator::Value01()` — `src/core/Modulation.h:14-21`, documented 0..1.
  21 implementers, all clamp or are designed to stay in range. The two
  deliberate exceptions are `InvertNode` (`src/nodes/ModulatorNodes.cpp`,
  see its comment at `ModulatorNodes.h:341-342`) and `RangeToRangeNode`
  with `clampOutput=false` (`ModulatorNodes.h:236`).
- The single apply loop is `src/main.cpp:28366-28370`:
  ```cpp
  // Defensive backstop: Value01() is documented to return 0..1,
  const float v01 = std::min(1.0f, std::max(0.0f, modulator->Value01()));
  *ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;
  ```
- `ParamRef` (`src/core/Modulation.h:25-33`) carries `float* value`,
  `minValue`, `maxValue`, re-registered every frame while nodes draw.
- `Modulation::Source` (`src/core/Modulation.h`) currently holds
  `{ nodeIndex, outputIndex }`. `Bind()` / `Unbind()` / `UnbindAllFor()`
  and `Links()` are the whole API.
- Patch format: `mod <dstIndex> <dstParam> <srcIndex> <srcOutput>`
  — written at `src/core/Patch.cpp:211-213`, parsed at
  `src/core/Patch.cpp:344-349`, `ModRecord` in `src/core/Patch.h:63`,
  documented at `src/core/Patch.h:30`.
- **Important, already verified**: the parser builds a fresh
  `std::istringstream` per line (see the comment at
  `src/core/Patch.cpp:296-302` explaining exactly this for the `flags`
  line, which already appends an optional trailing token). So appending
  extra tokens to the `mod` line is backward-compatible: an old patch
  missing them leaves the variables at their initialised defaults via
  C++11 failed-extraction behaviour, and cannot corrupt later lines.
  **Follow the `flags` precedent exactly** — initialise before `>>`.

## What to build

### 1. Binding polarity

Extend `Modulation::Source` with:

```cpp
enum Polarity { kAbsolute = 0, kBipolar };
int polarity = kAbsolute;
float depth = 1.0f;   // only meaningful when polarity == kBipolar
```

`kAbsolute` **must** be the default so every existing patch and every
new cable behaves exactly as it does today.

Rewrite the apply loop (`src/main.cpp:28366-28370`):

- `kAbsolute`: `*ref.value = ref.minValue + (ref.maxValue - ref.minValue) * v01;`
  (unchanged).
- `kBipolar`: the parameter's own value at the moment of binding is the
  centre. `*ref.value = centre + (v01 - 0.5f) * 2.0f * depth * (ref.maxValue - ref.minValue)`,
  then clamp to `[minValue, maxValue]`.

**The hard part is `centre`.** `*ref.value` is being overwritten every
frame by the modulation itself, so it cannot be read back as the centre —
that would feed back and drift. You must capture the parameter's
unmodulated value **once, when the binding is created** (in
`Modulation::Bind()`, reading through the current frame's `ParamRef`), and
store it in `Source` as `float centre`. Persist it. On unbind, write
`centre` back into the parameter so the knob returns to where the user
left it rather than freezing at whatever the LFO happened to be at.

If capturing at `Bind()` time turns out not to have access to the
parameter registry, the fallback is to capture lazily on the first apply
after a binding appears — but say in your summary which you did.

### 2. Persist it

`ModRecord` gains `polarity`, `depth`, `centre`. Write them as trailing
tokens on the existing `mod` line:

```
mod <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre>
```

Update the format comment at `src/core/Patch.h:30`. Parse defensively per
the `flags` precedent — initialise all three to their defaults
(`0`, `1.0f`, `0.0f`) before extracting.

### 3. UI

Wherever a modulated parameter's context menu / right-click currently
offers "unbind" (find it — it's near `DisconnectLinkById` and the param
pin handling around `src/main.cpp:12586+` and `26146-26180`), add:

- a polarity toggle (Absolute / Bipolar),
- a depth slider, shown only in Bipolar mode, range -1..1 (negative
  inverts, matching `ModDepthNode`'s convention at `ModulatorNodes.h:304`).

Keep it minimal — this is a context menu, not a panel.

Also: the collapsed `"mod"` tag drawn at `src/main.cpp:26173` should read
`"mod±"` (or similar) when any binding on that node is bipolar, so the
mode is visible without opening anything.

### 4. Separately: drop the defensive clamp

Second, smaller change, do it as its own commit. In `kAbsolute` mode,
stop force-clamping `v01` to 0..1 before the affine map, so a deliberately
out-of-range source (`InvertNode`, `RangeToRangeNode` with
`clampOutput=false`) extrapolates past the destination's nominal range
instead of being silently flattened. This is Option A from
`docs/plans/data-type-modernization/04-modulator-bipolar-contract.md`.

Then update the "out of contract" amber-border warning
(`src/main.cpp:12566-12583`) — its comment currently frames out-of-range
as a defect. Reword it to describe intentional extrapolation. Keep the
amber border and the 0/1 hairlines; they're still the right affordance.

## Do not

- Do not change `Value01()`'s name, signature, or 0..1 contract.
- Do not touch the 21 implementers.
- Do not add a polarity flag to individual modulator nodes. Polarity is a
  property of the connection, not the source — that's the whole point.

## Exit criteria

1. `cmake --build build -j"$(sysctl -n hw.ncpu)"` clean.
2. Load 2–3 existing saved patches that use LFO/Invert/Pattern bindings.
   Their behaviour must be **bit-identical** to before (they load as
   `kAbsolute`). Say in your summary which patches you tested.
3. Save a patch with a bipolar binding, quit, reload — polarity, depth and
   centre all survive.
4. Open that saved patch in a build **without** this change (stash it) and
   confirm it still loads without error, just ignoring the extra tokens.
5. Bind an LFO to a filter cutoff in bipolar mode at depth 0.5; the cutoff
   must swing symmetrically around the knob's position, and unbinding must
   return the knob to exactly that position.
6. Run the modulator self-test fixtures in `src/main.cpp` (around
   `24049-24064`, `24761`, `28731`) — no new FAILs.
7. Then run the project hygiene sweep (`/run-infinite-hygiene`).
