---
name: infinite-design-system
description: "How every Infinite UI element looks, moves and responds: tokens.json, role tokens, components/, Infinite Glyphs, UiAnim, the CI ratchet and goldens. Use BEFORE any colour, icon, widget style, animation, top bar, timeline chrome or panel restyle; for \"make the UI look like X\", \"new icon\", \"change accent\"."
---

## When to use (full scope)

Any change to how the UI *looks or feels*: colours, radii, spacing, type, icons, cursors, hover/press/on states, animation, tooltips, toasts, empty states, menus, tables, the top bar, the timeline header, panel chrome, node frames, and the rendering of node controls (knob, slider, checkbox, dropdown, mod dot). Also when adding a new kind of control, a new icon, or a new screen surface.

Not for *where* a control sits inside a node body: that is `node-ui-pillars` + `audio-node-ui`, and they win on layout.

Status: **planned** (2026-10-08). The spec, inventory and block plan are in `docs/plans/iconography/README.md`. Until block 1 lands, the folder below does not exist yet; follow the plan, and update this skill as each piece is built.

## The one rule

A call site never names a colour, radius, size or duration. It calls a **component**. Components read **role tokens**. Role tokens derive from **base tokens** in `tokens.json` (and from the 6 theme colours in `CategoryColors::UiTheme`).

```
tokens.json ──build_tokens.py──▶ Tokens.gen.h ──▶ components/* ──▶ every panel, node body, chrome
art/icons/src/*.svg ──build_glyphs.py──▶ font + Glyphs.gen.h ──▶ components/IconTile, Pin, ...
```

## Where things live

| Path | Holds |
|---|---|
| `src/app/ui/design/tokens.json` | Base + role tokens (the only place values are written) |
| `src/app/ui/design/*.gen.h` | Generated; never edit |
| `src/app/ui/design/UiAnim.*` | The only animation timing code |
| `src/app/ui/design/components/` | One file per component class (ids C*, V*, N*, A*, G* in the plan's section 0/0b) |
| `art/icons/src/` | Icon SVG masters, 20 px grid (and 16 px redraws) |
| `src/app/ui/design/components/PinDot.h` | Every pin: `Cable`, `Param` (idle/modulated/expression/prediction), `Swatch`; sizes `tok::pin_r_*`, colours `tok::pin_*` |
| `src/app/ui/design/components/ActionButton.h` | Text buttons: `Draw(label,size,Kind)`; kinds Plain, Primary, Selected, Record, Learn, Go, Solo. `PushSelectedButtonColors`/`PushPrimaryButtonStyle` set the scoped kind |
| `components/EmptyState.h` | Dimmed message + caption hint centred in a list/panel with nothing to show (mod matrix, Samples, Plugins) |
| `components/ColourChip.h` | Rounded colour field (replaces the raw colour button) |
| `components/Switch.h`, `GlyphToggle.h`, `VFader.h` | Switch (Settings/panels, `FormParts::Switch`), bare-glyph header toggle, vertical fader drawing |
| `tools/design/` | `build_tokens.py`, `build_glyphs.py`, `lint_icons.py`, `inventory.py`, `ratchet.json` |

## Colour roles (G14)

Fixed meaning, one colour per role; the accent never reuses a role colour.

| Role | Colour | Note |
|---|---|---|
| Record | red | |
| Solo | yellow | collides with favourite (gold) |
| Learn | amber | collides with modulation |
| Modulation | amber | |
| Expression | violet | |
| Prediction | green | |
| Go / running | green | |
| Favourite | gold | |

Known collisions are listed, not yet resolved; pair the colour with a glyph or label so colour is never the only cue.

## Recipes

| Change | Do |
|---|---|
| Colour / radius / spacing / timing | Edit `tokens.json`, run `build_tokens.py` |
| New icon | Draw SVG on the grid (plan section 2), `lint_icons.py`, `build_glyphs.py`, review the contact sheet |
| Restyle one control class everywhere | Edit its file in `components/`; update its golden in the same commit |
| New control class | New component file + gallery entry + golden + a row in the plan's section 0 |
| A node needs a new look | Never style it in the body file; extend the component |

## Before you commit

1. `python3 tools/design/inventory.py`: counts for literal colours and raw widgets went down or stayed the same (ratchet).
2. Generated headers match their sources.
3. Gallery goldens: changed only where you meant to change them, in light and dark.
4. Motion: nothing over 200 ms, values never animate, reduce-motion turns eases off (plan section 4b). Every `motion_ms` token is a note value at 120 BPM (1/16 = 125, 1/16T = 83, 1/8T = 167, 1/16D = 188); `tools/design/motion_lint.py` enforces it, laws in BRAND.md §8 / `tools/brand/motion.py`.
5. Three platforms: no `_WIN32` / `__APPLE__` in components (`windows-parity`, `linux-parity`).
6. Node bodies touched: run the `node-ui-pillars` checklist too.

## Popups and canvas zoom (decided 2026-10-09)

- Popups are chrome, not canvas content: constant on-screen size at any canvas zoom (only the global UI scale changes them). There is no zoom plumbing; do not add one.
- One component: `MenuParts` (24 pt rows, `BeginPopup`, `Choice`, `Item`). Dropdown lists are 160-280 px wide, other canvas menus cap at 320 px; every popup caps its height to the window and scrolls.
- Helper or reason text for a disabled row goes in a tooltip, never in the row (it widens the menu).
- No canvas level of detail: zooming out never hides or collapses a node's params (removed 2026-10-10). Only the node's own eye toggle does.
- A cursor restore as the last call before `EndGroup` must assign `window->DC.CursorPos` directly, not `SetCursorScreenPos` (ImGui "extend boundaries" error).
