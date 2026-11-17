#!/usr/bin/env python3
"""Menu-alive coverage: every right-click menu in src/ is walked by MENUALIVETEST, or is exempt with a reason.

A right-click root is a popup id that is named as one (ctx, context, a dock menu, a row/wave/node/link menu) or is
opened within a few lines of a right mouse click. Each must appear as a root in src/app/selftest/MenuAliveTests.cpp
(the menu table) or in EXEMPT below. A new right-click menu without a walk fails here, so no menu ships with rows
that do nothing. Exit 1 on any miss. Plan: internal/plans/ui-system/lint-plan.md (Block E, E2).
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST = ROOT / "src/app/selftest/MenuAliveTests.cpp"
NAMED = re.compile(r'(?:ctx|context|DockMenu|dockmenu|rowmenu|WaveMenu|NodeMenu|LinkMenu|curveMenu)$')
OPEN = re.compile(r'(?:OpenPopup|BeginPopup)\("(##[A-Za-z0-9_]+)"')
RIGHT = re.compile(r'ImGuiMouseButton_Right|IsMouseClicked\(1|IsItemClicked\(1|IsMouseReleased\(1|Show(?:Node|Link|Background)ContextMenu')
# Right-click popups the walk does not cover: id -> why.
EXEMPT = {
    "##pageMenu": "page chips: PAGEMENUTEST walks them",
    "##studioNodeMenu": "toolbar chip picker (left click), not a right-click menu",
    "##studioSliceMenu": "toolbar chip menu (left click)",
    "##commentedit": "comment editor form, not a menu",
    "##colorpick": "colour picker popover",
    "##dropdown": "value picker",
    "##fpPick": "add-box search picker (right-click on empty canvas), a search list, not a verb menu",
    "##metronomePopup": "metronome settings popover (a level slider), not a verb menu",
}


def roots():
    found = {}
    for p in (ROOT / "src").rglob("*.cpp"):
        lines = p.read_text(errors="ignore").splitlines()
        for i, line in enumerate(lines):
            for m in OPEN.finditer(line):
                pid = m.group(1)
                if NAMED.search(pid) or (m.group(0).startswith("OpenPopup") and any(RIGHT.search(l) for l in lines[max(0, i - 8):i + 1])):
                    found.setdefault(pid, f"{p.relative_to(ROOT)}:{i + 1}")
    return found


def main():
    walked = set(re.findall(r'"(##[A-Za-z0-9_]+)"', TEST.read_text()))
    miss = [(pid, at) for pid, at in sorted(roots().items()) if pid not in walked and pid not in EXEMPT]
    for pid, at in miss:
        print(f"menu_alive_coverage: {pid} ({at}) is a right-click menu with no MENUALIVETEST row and no exemption")
    stale = [pid for pid in EXEMPT if pid in walked]
    for pid in stale:
        print(f"menu_alive_coverage: {pid} is walked and exempt at once; drop the exemption")
    if miss or stale:
        return 1
    print(f"menu_alive_coverage: OK ({len(walked & set(roots()))} right-click menus walked, {len(EXEMPT)} exempt)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
