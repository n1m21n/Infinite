---
name: shortcuts-sweep
description: Cross-checks every keyboard shortcut in Infinite against three things at once - that each row of the in-app shortcuts window has a real handler, that no binding exists which the window never tells anyone about, and that no handler is gated on Cmd alone (which makes it dead on Windows). Static, needs no build. Use when asked "do the shortcuts work", "check the shortcuts on both versions", "is the shortcut list accurate", after adding or changing any keyboard binding, after editing the shortcuts help window, or when a user reports a key that does nothing on Windows.
---

Paths below are relative to the repo root (`/Users/namansoni/infinte`).

## Run this first

```bash
python3 .claude/skills/shortcuts-sweep/check.py
```

Reads `src/main.cpp` only - no build, no launch, runs in under a second.
`--quiet` prints just the verdict line. Exit code 0 when clean.

## The three questions it answers

Infinite has two independent descriptions of its keyboard, and nothing keeps
them in step:

1. **The contract**: `static const ShortcutEntry kShortcuts[]` inside
   `DrawShortcutsWindow` (`src/main.cpp` ~22675) - what the app *tells the
   user* it does. This is the only shortcut documentation users ever see.
2. **The implementation**: `ImGui::IsKeyPressed(ImGuiKey_...)` call sites,
   mostly in the editor block around `main.cpp` ~44800-45500.

The check compares them in both directions, and separately audits the
modifier gates:

| Report | Meaning | Severity |
|---|---|---|
| documented but no handler | the help window promises a key that does nothing | fail |
| handled but undocumented | a real binding no user can discover | fail |
| Super without Ctrl | macOS-only gate - dead on Windows | fail |
| dialog-local keys | allowlisted in `check.py` (`LOCAL_KEYS`) | informational |
| gesture rows | "Scroll Wheel", "Drag Canvas" - not keys at all | informational |

## Why the modifier check matters more than it looks

The app's convention, at `main.cpp` ~44799, is one line:

```cpp
const bool cmdOrCtrl = io.KeyCtrl || io.KeySuper;
```

Every Cmd-family binding is gated on that, which is exactly why Cmd+S on
macOS and Ctrl+S on Windows are the same code path and the same commit. A
handler that reaches for `io.KeySuper` or `ImGuiMod_Super` on its own is a
binding that silently does not exist on Windows, and no macOS test run can
detect it - which is the whole reason this check is static and greps for the
pattern rather than pressing keys. `MODKEY` (`main.cpp` ~46) is the display
half of the same convention: `"Ctrl"` on Windows, `"Cmd"` elsewhere.

`check.py`'s `SUPER_ALLOW` list holds the known-correct exceptions, matched by
line *content* so it survives edits elsewhere in the file. Adding to it is a
deliberate act; do it with the reason in the comment, the way the existing
entries do.

## Known finding as of this skill being written

**Shift+P opens the docked performance matrix (`main.cpp` ~44946) and is
absent from the shortcuts window**, while its sibling Shift+M (modulation
matrix) is listed. Either add the row or remove the binding - the check stays
red until one of those happens, which is the intended behaviour of a gate.

## What this does not prove

- **That the handler does the right thing.** The check proves a key is wired
  to *something*. Whether Shift+B actually bypasses the selection is a
  behavioural question - `INFINITE_BYPASSTEST`, `INFINITE_UNDOTEST`,
  `INFINITE_GROUPTEST`, `INFINITE_SELECTTEST` and `INFINITE_HIDETEST` cover
  those actions, and `run-infinite-hygiene`'s suite runs them all.
- **That a key is reachable.** Several actions are also driven by
  `gRequestSelectAll` / `gRequestAddComment` / `gRequestAddNode` request
  flags so the menu and fixtures can trigger the same path without synthesised
  input. A binding could be shadowed by a focused text field (`io.WantTextInput`
  gating), a modal, or a popup that owns the keyboard, and this check would
  still call it handled.
- **Anything about real Windows key delivery.** GLFW→ImGui key mapping,
  dead keys, and non-US layouts are outside what source inspection can see.
  Only a run on Windows answers that; see `pillar-parity-audit`.
- **Chords and sequences.** Only single-key `IsKeyPressed` calls are parsed;
  a shortcut built from a modifier state machine would read as unhandled.

## When a real key is added

1. Add the handler next to its family in `main.cpp` (Cmd-family under
   `cmdOrCtrl`, canvas toggles under `shiftOnly`), gated on `!typing`.
2. Add the `kShortcuts` row in the same commit, using `MODKEY` for anything
   Cmd/Ctrl.
3. Re-run `check.py`; it should stay clean without touching either allowlist.
