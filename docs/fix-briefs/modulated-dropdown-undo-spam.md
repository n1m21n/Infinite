# Fix brief: modulated dropdowns push undo checkpoints and dirty the patch

Branch: `bugfix/modulated-dropdown-undo-spam` (off `main`)

## Root cause (confirmed by reading the code)

Four widget functions in `src/main.cpp` let a modulation cable drive a
discrete/enum param, and on the driven path each one calls the caller's
`onSelect` callback directly:

```cpp
if (h.driven) {
   const int drivenIdx = std::clamp((int)lroundf(h.value), 0, lastIndex);
   if (drivenIdx != current && onSelect)
      onSelect(drivenIdx);
   ...
}
```

- `DropdownButton` — `src/main.cpp:3369`, driven block ~3388-3392
- `AudioKnobRow::Dropdown` — `src/main.cpp:10877`, driven block ~10897-10900
- `AudioKnobRow::DropdownKnob` — `src/main.cpp:10977`, driven block ~10977-10980
- `AudioBareDropdown` — `src/main.cpp:13558`, driven block ~13563-13566

85 of the ~175 call sites across these four widgets write `onSelect`
lambdas that open with `PushUndoCheckpoint()` (confirmed by grep — e.g.
`src/main.cpp:20386` `DrawAudioFilterBody`, `:8683` `DrawProjectionParams`,
`:10167` `DrawPaletteParams`, `:13716` the wavetable engine, and ~80 more
across audio/note node bodies plus a handful of non-audio ones:
Projection, Palette, ColorRamp, ImageAnalyze, AudioColorRamp).

`PushUndoCheckpoint()` → `PushUndoSnapshot()` (`src/main.cpp:45594-45608`)
pushes a full `BuildPatchData()` serialization onto `gUndoStack`, clears
`gRedoStack`, and unconditionally sets `gPatchDirty = true`. Both
`PushUndoSnapshot` call sites already check
`if (gSuppressUndoCheckpoints) return;` at the top (`src/main.cpp:45596`
and `:45615`), so there is an existing suppression mechanism — it's just
never engaged on the driven path.

**Effect**: every time a modulator's value crosses an index boundary on
one of these 85 controls, the whole patch gets re-serialized on the main
thread (not the audio thread — this is ImGui draw code) and the patch is
marked dirty even though the user made no edit. Measured at ~25% of
main-thread samples under `INFINITE_MIXEDSTRESSTEST=3` on an M2
(`BuildPatchData` under these dropdown lambdas). This is a shipped defect
present since commit `4d26ecc` (2026-08-29, "Add Grain Molder node...")
and in every tagged release since v0.2.4 — not a regression, so there's no
saved-patch compatibility concern (`gUndoStack`/`gRedoStack` are
runtime-only, never serialized).

## The fix

In each of the four widget functions, wrap the driven-path `onSelect`
call in the same suppress/restore pattern already used at
`src/main.cpp:31888-31895` (`ArrangeMakeClipSourceUnique`), which is:

```cpp
const bool wasSuppressed = gSuppressUndoCheckpoints;
gSuppressUndoCheckpoints = true;
onSelect(drivenIdx);
gSuppressUndoCheckpoints = wasSuppressed;
```

(A `struct Restore { bool prev; ~Restore(){ gSuppressUndoCheckpoints = prev; } }`
guard is the pattern used at `:31888` for exception/early-return safety,
but these four sites have no early return or throw between setting and
restoring the flag, so a plain save/set/call/restore is fine and simpler —
use your judgment, but keep it consistent across all four functions.)

Do this in all four places:
1. `src/main.cpp` ~10897-10900 (`AudioKnobRow::Dropdown`)
2. `src/main.cpp` ~10977-10980 (`AudioKnobRow::DropdownKnob`)
3. `src/main.cpp` ~13563-13566 (`AudioBareDropdown`)
4. `src/main.cpp` ~3388-3392 (`DropdownButton`)

This suppresses `PushUndoCheckpoint` (via the existing
`gSuppressUndoCheckpoints` check at `:45596`) AND `gPatchDirty = true`
(same function, same guard, so it's covered for free — do not add a
second/separate guard for `gPatchDirty`, that would duplicate what
`gSuppressUndoCheckpoints` already does).

Do **not** skip calling `onSelect()` on the driven path — only suppress
the checkpoint/dirty side effects around it. The param write inside
`onSelect` must still happen; that's how a driven dropdown actually
reflects the modulator's value. All 85 affected `onSelect` lambda bodies
were checked for side effects beyond a plain param write (device reopen,
table reload, topology rebuild) and none were found — but if code review
turns up one that should still checkpoint even when driven, flag it
rather than silently special-casing it.

`ModCheckbox` (`src/main.cpp:3639`, driven block ~3659-3663) has the same
bug in a different shape. Its only caller that checkpoints is
`if (ModCheckbox("match input res", &n->matchInput)) PushUndoCheckpoint();`
at `src/main.cpp:8687` (`DrawProjectionParams`).

**Correction (reviewed 2026-09-23): do NOT suppress inside `ModCheckbox`.**
The checkpoint runs in the *caller*, after `ModCheckbox` has returned, so
setting and restoring `gSuppressUndoCheckpoints` around the driven block
inside `ModCheckbox` would have no effect. `ModCheckbox` also must keep
returning `true` when a driven value changes: the setter-style callers
described at `src/main.cpp:3655`
(`bool tmp = node->Get(); if (ModCheckbox(.., &tmp)) node->Set(tmp)`)
depend on that return value to apply the value at all.

Do this instead: let the caller tell a user click apart from a driven
change. Add an optional out-param, e.g.
`bool ModCheckbox(const char* label, bool* value, bool* outUserChanged = nullptr)`,
set only on the `ImGui::Checkbox` click path. Then change the caller at
`:8687` to checkpoint only when `userChanged` is true. The
`ImGui::Checkbox` click path only fires on real user input, so its undo
entry is taken there. Keep the existing return value's meaning unchanged
for all other callers.

## Regression test (write this too — this is why the bug shipped for a
month across 5 releases without being caught)

Add a check to the undo self-test block in `src/main.cpp` (near the
existing undo/redo assertions around `:70556-70561`, in the same
self-test harness that `INFINITE_MIXEDSTRESSTEST` or
`run-infinite-hygiene` drives) that:

1. Spawns a node with a modulatable dropdown param (e.g. an Audio Filter
   node and its "type" dropdown, or reuse whatever node the surrounding
   self-test already has handy).
2. Binds a modulator (LFO or similar) to that dropdown's param so
   `h.driven` becomes true.
3. Records `gUndoStack.size()` and `gPatchDirty` before, then drives the
   modulator through several full sweeps (enough to cross the dropdown's
   index boundaries multiple times) via however this harness already
   steps frames.
4. Asserts `gUndoStack.size()` is unchanged and `gPatchDirty` is still
   false (assuming it started false) afterward, printing OK/FAIL like the
   neighboring undo assertions at `:70559-70561` do.

Place it wherever fits the existing self-test's structure best — judge on
the ground whether it belongs inline with the undo self-test block or as
a small addition to `modulation-sweep`'s runtime fixtures. Either is fine
as long as it actually binds a modulator to a driven dropdown and checks
the stack, not just that `Undo()`/`Redo()` themselves don't grow the stack
(which is all the existing tests at `:70556-70561` check).

## Out of scope — do not touch

- The other ~90 dropdown-shaped call sites that don't embed
  `PushUndoCheckpoint` in their `onSelect` (Math, Noise, Texture, Curve,
  Light, Ramp, Shape, etc. node bodies) — they don't reproduce this bug
  today (their only checkpoint is the central popup-click handler at
  `src/main.cpp:88544-88547`, which the driven path never reaches), and
  changing them isn't needed for this fix.
- The other 10 of 11 `ModCheckbox` call sites that check
  `if (ModCheckbox(...))` for something other than `PushUndoCheckpoint` —
  don't change `ModCheckbox`'s general contract, only the suppress
  behavior on the driven path.
- Any redesign of the modulatable-dropdown feature itself. The design (a
  cable wins over the widget, exactly like a slider) is correct and
  intentional per the comment at `src/main.cpp:10874-10876` — this fix
  only removes an unwanted side effect of calling `onSelect`, it doesn't
  change what `onSelect` does or when it's called.

## Build and verify

```bash
cmake --build build -j"$(sysctl -n hw.ncpu)"
```

Confirm it compiles clean. Then run:

```bash
INFINITE_MIXEDSTRESSTEST=3 build/Infinite.app/Contents/MacOS/Infinite
```

and confirm the new self-test assertion(s) print OK, and that overall
avgMs/avgFps improve versus the baseline (avgMs=54.66, avgFps=18.3 on the
reporter's M2) — hardware will differ, so compare relative before/after
on the same machine rather than expecting to hit those exact numbers.
Also run the existing undo/redo self-tests (already covered by the same
`INFINITE_MIXEDSTRESSTEST` or `run-infinite-hygiene` invocation) to
confirm nothing else regressed — in particular the "undo/redo do not grow
their own stacks" check at `:70556-70561` and the
modulator-input-survives-undo checks nearby.

Load `invariant-interaction-audit` before finishing: the invariant this
establishes is "modulation never creates an undo entry or dirties the
patch." Sweep the four widget functions and `ModCheckbox` once more after
the edit to confirm no other branch inside them (e.g. the non-driven,
user-click path, or the `h.modulated` read-only display path) accidentally
got swept into the suppression — only the `h.driven` → `onSelect` call
should be wrapped; the popup-click path's `PushUndoCheckpoint()` at
`src/main.cpp:88546` must keep firing normally for real user edits.
