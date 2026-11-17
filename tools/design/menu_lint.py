#!/usr/bin/env python3
"""Menu lint: right-click menus and submenus hold verbs, never value fields.

A slider, drag, input or checkbox-with-value inside a context menu (or any MenuParts::SubMenu) is a second copy of an
inspector, drawn at menu size with its label on the wrong side (owner review 2026-11-17: the clip menu's Colour
Grade). Values live in the inspector; the menu offers the verb that opens it. Colour dots (ColourChip::Dots /
TintDots) are fine. Exit 1 on any hit.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OPEN = re.compile(r'MenuParts::SubMenu\(|MenuParts::BeginContextItem\(|MenuParts::BeginPopup\("[^"]*(?:ctx|Ctx|Menu|menu)[^"]*"\)')
VALUE = re.compile(r'\b(?:ImGui::(?:SliderFloat|SliderInt|DragFloat|DragInt|InputText|InputFloat|InputInt|ColorEdit\d|ColorButton)|'
                   r'Arrange(?:SliderFloat|DragFloat|TickField)|tickField)\s*\(')
# Popups that are small forms on purpose (a dialog, not a menu): id -> why.
ALLOW = {"##arrangeRenderPopup": "render settings popover"}


def block(src, at):
    """The text of the { ... } block that follows position `at` (the if-body of the opener)."""
    i = src.find("{", at)
    if i < 0:
        return ""
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{": depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j]
    return ""


bad = []
n = 0
for f in sorted((ROOT / "src/app").rglob("*.cpp")):
    src = f.read_text(errors="ignore")
    for m in OPEN.finditer(src):
        if any(k in src[m.start():m.start() + 80] for k in ALLOW):
            continue
        n += 1
        body = block(src, m.end())
        for v in VALUE.finditer(body):
            line = src.count("\n", 0, src.find(body) + v.start()) + 1
            bad.append(f"{f.relative_to(ROOT)}:{line}: {v.group(0).rstrip('(').strip()} inside a menu: move the value to the inspector, offer a verb here")
bad = list(dict.fromkeys(bad))
print(f"menu lint: {n} menus checked, {len(bad)} value field(s) inside menus")
for b in bad:
    print(" ", b)
sys.exit(1 if bad else 0)
