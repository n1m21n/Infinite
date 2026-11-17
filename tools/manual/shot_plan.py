#!/usr/bin/env python3
"""Generate the 2.0 shot plan: every screenshot the manuals need, in capture order, with its output file, the source
it comes from, the minimum pixel size, and whether a capture recipe exists yet.

  python3 tools/manual/shot_plan.py            # writes docs/release/shot-plan-2.0.json and prints the counts
  python3 tools/manual/shot_plan.py --check    # fails if a planned file already exists but is below its pixel rule

Order (priority): 1 = chapter 29 gaps (UI: menus, rail, settings, dialogs, combos, popups); 2 = node shots, in the
NodeVocab order, closed (c), params open (o), first dropdown open (d, only where the node has one).

Sources (all read from the tree, nothing is launched):
  nodes     src/core/chat/NodeVocab.gen.h (377 registered types) minus the five IsUserSpawnable() hides = 372
  popups    literal OpenPopup / BeginPopup / BeginCombo / BareCombo ids under src/app
  settings  FormParts::Section headings in src/app/panels/SettingsWindow.cpp and the tab list
  rail      the DrawPanelRail entries in src/app/frame/StageMenuBar.cpp
"""
import json
import os
import re
import sys
import datetime
import glob

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shot_config as cfg  # noqa: E402

ROOT = cfg.ROOT
PLAN = os.path.join(ROOT, "docs", "release", "shot-plan-2.0.json")
HIDDEN = {"Delete Selected", "Transform Selected", "Extrude Selected", "Group", "Field Graph"}  # IsUserSpawnable() false

def slug(s):
    return "".join(c if c.isalnum() else "_" for c in s).strip("_").lower()


def nodes():
    t = open(os.path.join(ROOT, "src", "core", "chat", "NodeVocab.gen.h")).read()
    names = re.findall(r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"', t)
    out = [(c, n) for n, c in names if n not in HIDDEN]
    return out


def source_strings():
    """Literal popup, combo and section ids under src/app, with the file each was found in."""
    popups, combos = {}, {}
    for f in glob.glob(os.path.join(ROOT, "src", "app", "**", "*.cpp"), recursive=True) + \
            glob.glob(os.path.join(ROOT, "src", "app", "**", "*.h"), recursive=True):
        rel = os.path.relpath(f, ROOT)
        txt = open(f, errors="ignore").read()
        for m in re.finditer(r'(?:BeginPopup\w*|OpenPopup)\(\s*"([^"]+)"', txt):
            popups.setdefault(m.group(1), rel)
        for m in re.finditer(r'(?:BeginCombo|BareCombo|Combo)\(\s*(?:T\(|L\()?"([^"]+)"', txt):
            combos.setdefault(m.group(1), rel)
    return popups, combos


def settings_sections():
    txt = open(os.path.join(ROOT, "src", "app", "panels", "SettingsWindow.cpp")).read()
    return re.findall(r'FormParts::Section\(T\("([^"]+)"\)\)', txt)


SETTINGS_TABS = ["Appearance", "Canvas & Workspace", "Audio", "General & Performance", "Field Language",
                 "Expression Globals", "Extensions", "Plugins", "Agents & Terminal", "Infinite as VST3/AU", "3D View", "Keys"]

RAIL = ["library", "viewport", "modmatrix", "perf", "mixer", "arrange", "session", "studio", "piano", "drums",
        "view3d", "view2d", "curve", "field", "history", "settings", "terminal"]

MENUS = [  # chapter 29 menu gaps (menu bar: File, Edit, Menu, Help)
    ("menu_help", "Help menu open (All shortcuts, Help / module reference, UI Debugger, UI Style Editor, Collect diagnostics, About, Check for updates)"),
    ("menu_file_recent", "File > Open Recent submenu"),
    ("menu_file_template", "File > Template library (item and where it opens)"),
    ("menu_file_saveas_media", "File > Save with media... and Save As..."),
    ("menu_file_sharepatch", "File > Share Patch..."),
    ("menu_menu_settings", "Menu > Settings..."),
    ("menu_menu_showset", "Menu > Show Set submenu"),
    ("menu_menu_showlock", "Menu > Show lock (on and off)"),
    ("menu_menu_patchnav", "Menu > Next patch / Previous patch"),
    ("menu_menu_panic", "Menu > Panic and Resume after panic"),
    ("menu_menu_preshow", "Menu > Pre-show check"),
    ("menu_panels_mixer", "Menu > Panels > Mixer"),
    ("menu_panels_drums", "Menu > Panels > Drums"),
    ("menu_panels_view2d", "Menu > Panels > 2D View"),
    ("menu_panels_curve", "Menu > Panels > Curve Editor"),
    ("menu_panels_fieldpatcher", "Menu > Panels > Field Patcher"),
    ("menu_panels_terminal", "Menu > Panels > Terminal"),
]

DIALOGS = [
    ("dialog_unsaved", "Unsaved changes dialog (New, Open, Quit with edits)"),
    ("dialog_recover", "Autosave recovery dialog"),
    ("dialog_collect_diagnostics", "Collect diagnostics (Include the open patch, Collect, Cancel)"),
    ("dialog_check_updates", "Check for updates (latest / available results, Check, Website, Close)"),
    ("dialog_share_patch", "Share Patch dialog"),
    ("dialog_preshow", "Pre-show check window"),
    ("dialog_terminal_quit", "Terminal quit confirmation"),
]

RAIL_TITLES = {"mixer": "Mixer", "drums": "Drums", "view2d": "2D View", "curve": "Curve editor", "field": "Field Patcher",
               "terminal": "Terminal"}


def build():
    popups, combos = source_strings()
    shots = []

    def add(pri, group, sid, out, kind, min_px, source, status, note=""):
        shots.append(dict(priority=pri, group=group, id=sid, file=out, kind=kind, min_px=min_px,
                          source=source, status=status, note=note))

    ui_dir = os.path.join("docs", "ui_screenshots")
    # Priority 1: chapter 29 gaps.
    for sid, what in MENUS:
        add(1, "menus", sid, f"{ui_dir}/{sid}.png", "ui", [3200, 2000], what, "needs-recipe")
    for key in RAIL:
        if key in RAIL_TITLES:
            add(1, "rail", f"panel_{key}", f"{ui_dir}/panel_{key}.png", "ui", [3200, 2000],
                f"rail tile: {RAIL_TITLES[key]}", "needs-recipe")
    for tab in SETTINGS_TABS:
        s = slug(tab)
        if tab in ("Infinite as VST3/AU", "3D View", "Keys"):
            add(1, "settings-tabs", f"settings_{s}", f"{ui_dir}/settings_{s}.png", "ui", [3200, 2000],
                f"Settings tab: {tab}", "needs-recipe")
    for sec in settings_sections():
        add(1, "settings-cards", f"settings_card_{slug(sec)}", f"{ui_dir}/settings_card_{slug(sec)}.png", "zoom",
            [250, 0], f"Settings section card: {sec} (scroll to it in the Settings window)", "needs-recipe")
    for sid, what in DIALOGS:
        add(1, "dialogs", sid, f"{ui_dir}/{sid}.png", "ui", [3200, 2000], what, "needs-recipe")
    for label, where in sorted(combos.items()):
        add(1, "combos", f"combo_{slug(label)}", f"{ui_dir}/combo_{slug(label)}.png", "zoom", [250, 0],
            f"dropdown '{label}' ({where})", "needs-recipe")
    for pid, where in sorted(popups.items()):
        add(1, "popups", f"popup_{slug(pid.lstrip('#'))}", f"{ui_dir}/popup_{slug(pid.lstrip('#'))}.png", "zoom",
            [250, 0], f"popup/context menu '{pid}' ({where})", "needs-recipe")

    # Priority 2: nodes, closed / params open / first dropdown.
    spawn = nodes()
    for cat, name in spawn:
        base = slug(f"{cat}__{name}")
        held = name == "Output"
        add(2, "nodes", f"node/{cat}/{name}/c", f"docs/shots/framed/{slug(f'node/{cat}/{name}/c')}.png", "zoom",
            [250, 0], "node closed", "planned")
        add(2, "nodes", f"node/{cat}/{name}/o", f"docs/shots/framed/{slug(f'node/{cat}/{name}/o')}.png", "zoom",
            [250, 0], "node params open", "blocked" if held else "planned",
            "Desktop export path visible until the app blanks it" if held else "")
        add(2, "nodes", f"node/{cat}/{name}/d", f"docs/shots/framed/{slug(f'node/{cat}/{name}/d')}.png", "zoom",
            [250, 0], "first dropdown open (only where the node has one; decided at capture)",
            "blocked" if held else "conditional", "Desktop export path visible until the app blanks it" if held else "")

    counts = {}
    for s in shots:
        counts[(s["priority"], s["group"])] = counts.get((s["priority"], s["group"]), 0) + 1
    return shots, counts, len(spawn), popups, combos


def main():
    check = "--check" in sys.argv
    shots, counts, nspawn, popups, combos = build()
    if check:
        bad = []
        for s in shots:
            p = os.path.join(ROOT, s["file"])
            if os.path.exists(p) and s["min_px"][0] and not cfg.check_pixels(p, s["kind"])[0]:
                bad.append(s["file"])
        print("pixel check:", "ok" if not bad else "FAIL " + ", ".join(bad))
        return 1 if bad else 0
    plan = {
        "generated": datetime.date.today().isoformat(),
        "app": "release 2.0 build (infinte-base)",
        "rules": {"ui_min_px": list(cfg.UI_MIN), "zoom_min_width_px": cfg.ZOOM_MIN_WIDTH},
        "capture_config": cfg.CAPTURE_CONFIG,
        "counts": {"spawnable_nodes": nspawn, "popups_literal": len(popups), "combos": len(combos),
                   "settings_sections": len(settings_sections()), "shots_total": len(shots),
                   "by_group": {f"p{p}/{g}": n for (p, g), n in sorted(counts.items())}},
        "shots": shots,
    }
    os.makedirs(os.path.dirname(PLAN), exist_ok=True)
    with open(PLAN, "w") as f:
        json.dump(plan, f, indent=1)
    print(json.dumps(plan["counts"], indent=1))
    print("wrote", os.path.relpath(PLAN, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
