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
| `tools/design/` | `build_tokens.py`, `build_glyphs.py`, `lint_icons.py`, `inventory.py`, `ratchet.json` |

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
4. Motion: nothing over 200 ms, values never animate, reduce-motion turns eases off (plan section 4b).
5. Three platforms: no `_WIN32` / `__APPLE__` in components (`windows-parity`, `linux-parity`).
6. Node bodies touched: run the `node-ui-pillars` checklist too.
