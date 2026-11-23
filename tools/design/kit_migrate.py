#!/usr/bin/env python3
"""Move raw ImGui calls onto the design kit's parts (owner 2026-11-24, internal/plans/ui-control-catalogue.md
section 4). One-off migration kept as the record. Each rewrite is to a forwarder with the same arguments, so
behaviour does not change; the kit file is now the one place each part's look can change.

    InvisibleButton(          -> UiInteract::Hit(
    SetKeyboardFocusHere(     -> UiInteract::FocusHere(
    IsWindowFocused(          -> UiInteract::WindowFocused(
    IsItemClicked(Right)      -> MenuParts::ContextClicked()
    IsMouseClicked(Right / 1) -> MenuParts::RightPressed()
    Selectable / BeginTable / BeginListBox -> ListRow::...
    Image( / X->AddImage[Rounded|Quad]( -> Thumb::Image / Add / AddRounded / AddQuad(X, ...)

Files under src/app/ui/design/ are the kit itself and are left alone.

    python3 tools/design/kit_migrate.py [--dry]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KIT = "src/app/ui/design/"

DL = r"((?:ImGui::GetWindowDrawList\(\)|ImGui::GetForegroundDrawList\(\)|ImGui::GetBackgroundDrawList\(\)|\w+))->"
RULES = [
    (re.compile(r"ImGui::InvisibleButton\("), "UiInteract::Hit(", "UiInteract.h"),
    (re.compile(r"ImGui::SetKeyboardFocusHere\("), "UiInteract::FocusHere(", "UiInteract.h"),
    (re.compile(r"ImGui::IsWindowFocused\("), "UiInteract::WindowFocused(", "UiInteract.h"),
    (re.compile(r"ImGui::IsItemClicked\(\s*ImGuiMouseButton_Right\s*\)"), "MenuParts::ContextClicked()", "components/MenuParts.h"),
    (re.compile(r"ImGui::IsMouseClicked\(\s*(?:ImGuiMouseButton_Right|1)\s*\)"), "MenuParts::RightPressed()", "components/MenuParts.h"),
    (re.compile(r"ImGui::Selectable\("), "ListRow::Selectable(", "components/ListRow.h"),
    (re.compile(r"ImGui::BeginTable\("), "ListRow::BeginTable(", "components/ListRow.h"),
    (re.compile(r"ImGui::BeginListBox\("), "ListRow::BeginListBox(", "components/ListRow.h"),
    (re.compile(r"ImGui::Image\("), "Thumb::Image(", "components/Thumb.h"),
    (re.compile(DL + r"AddImageRounded\("), r"Thumb::AddRounded(\1, ", "components/Thumb.h"),
    (re.compile(DL + r"AddImageQuad\("), r"Thumb::AddQuad(\1, ", "components/Thumb.h"),
    (re.compile(DL + r"AddImage\("), r"Thumb::Add(\1, ", "components/Thumb.h"),
]


def code_part(line):
    i = line.find("//")
    return (line, "") if i < 0 else (line[:i], line[i:])


def ensure_includes(text, incs):
    lines = text.split("\n")
    have = set(l for l in lines if l.startswith("#include"))
    first = next((i for i, l in enumerate(lines) if l.startswith("#include")), 0)
    for inc in sorted(incs):
        s = f'#include "app/ui/design/{inc}"'
        if s not in have:
            lines.insert(first + 1, s)
    return "\n".join(lines)


def main():
    dry = "--dry" in sys.argv
    totals = {}
    for base, _, files in os.walk(os.path.join(ROOT, "src/app")):
        for f in files:
            if not f.endswith((".cpp", ".h")):
                continue
            p = os.path.join(base, f)
            rel = os.path.relpath(p, ROOT)
            if rel.startswith(KIT):
                continue
            text = open(p, encoding="utf-8").read()
            out, incs = [], set()
            for line in text.split("\n"):
                code, comment = code_part(line)
                for rx, rep, inc in RULES:
                    code, n = rx.subn(rep, code)
                    if n:
                        incs.add(inc)
                        totals[rep.split("(")[0]] = totals.get(rep.split("(")[0], 0) + n
                out.append(code + comment)
            new = "\n".join(out)
            if new != text and not dry:
                open(p, "w", encoding="utf-8").write(ensure_includes(new, incs))
    for k, v in sorted(totals.items()):
        print(f"{v:5d}  {k}")
    print(f"{sum(totals.values())} calls moved onto the kit")


if __name__ == "__main__":
    main()
