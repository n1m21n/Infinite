# Node polish 2, Block D: accessibility and consistency audit

Scope note: **keyboard work here is not accessibility done.** Shortcut rebinding and screen-reader support are out of scope and not started.

| # | Item | Result | Evidence / where |
|---|---|---|---|
| 1 | Keyboard and focus | Popups and non-canvas panels get ImGui keyboard nav (Tab, arrows, Enter, Space) with a visible focus ring; the canvas keeps its own node keyboard model; Esc closes popups and the new find bar; focus ring hides on mouse click | `StageFramePump.cpp` nav block; NAVTEST, KBCURSORTEST, KBDISCRETETEST, TEXTFOCUSTEST, FINDTEST in `--group ui`. Walked by code and fixtures, not by hand through every dialog |
| 2 | Contrast | `tools/design/contrast.py`: 42 role pairs, both themes, text 4.5:1, graphics 3:1. 10 light-theme pairs failed; fixed in `tokens.json` (curve value text/idle/live/active border, pin idle/mod, action learn/solo, favourite). 0 failing, 0 baselined | runs in `driver.sh --group ui` |
| 3 | Tooltips | One delay (`tok::motion_tooltip_delay`, 500 ms: `HoverDelayNormal` set per frame + own rest timer in `HelpTip`), one max width (24 em), one style. Help tips stay opt-in (Settings > Help tooltips). No param label was renamed in this block, so node-help coverage needs no re-check | `ParamWidgets.cpp` `HelpTip`; diagnostics tooltips (`SetTooltip`, ~40) still show always by design |
| 4 | Colour never the only cue | Record, Solo, Learn are `ActionButton` kinds that always carry a text label ("Record"/"Stop", "Solo", "Learn"); Learn ring is a ring (and pulses), modulation is a dot; favourite is a star glyph. Collisions are resolved by shape/label, not hue | `ActionButton.h`, `StateRing.h`, `PinDot.h` |
| 5 | Motion | All `motion_ms` tokens <= 200 (tooltip delay is a wait); every `UiAnim` call uses a token or a literal <= 200; **reduce-motion was defined but never switched on**: now Settings > Reduce motion (saved) or `INFINITE_REDUCEMOTION=1`, and the Learn pulse and indeterminate bar go still. Values never animate (by review; `UiAnim` is only used for chrome). OS "reduce motion" preference is not read yet | `tools/design/motion_lint.py`, `UiAnim.cpp`, `StageFramePump.cpp` |
| 6 | Dark + light sheets, one command | `tools/design/gallery/run_sheets.sh [OUT] [Cat...]` | |
| 6b | Goldens | Component-gallery goldens (both themes) now run inside `driver.sh --group ui` with the contrast, motion and ratchet lints. Node-family shots stay contact sheets reviewed by eye: node content (live data, previews) is too volatile for pixel diffs | `tests/ui-golden/macos/`, `golden.py` |
| 7 | Compact density | **No.** The UI-scale slider (0.5 to 2.0) already shrinks everything uniformly; a second density would fork spacing and hit sizes in every component, double the golden surface, and break `hit_min` 24. Revisit only if users ask for denser chrome at normal text size | decision |

Not done: OS reduce-motion read (three platform sides), keyboard walk of every dialog by hand, visual check of tooltips and the Reduce motion switch.
