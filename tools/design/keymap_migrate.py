#!/usr/bin/env python3
"""Move raw ImGui key checks onto Keymap (owner 2026-11-24, internal/plans/ui-control-catalogue.md section 7).

One-off migration, kept as the record of which call site became which Act. For every (file, line, key) below it
rewrites `ImGui::IsKeyPressed(ImGuiKey_K[, rep])` to `Keymap::KeyPressed(Keymap::Act::A, rep)` (or to a focus
helper: Cancel, Enter, Confirm, Up/Down/Left/Right). The modifier gating at the call site is left exactly as it
was, so behaviour does not change; the Act's Mod in Keymap.h documents the chord. ImGui's default repeat=true is
written out where the call site relied on it. Fails loudly if a line no longer holds the key it expects.

    python3 tools/design/keymap_migrate.py [--dry]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# helpers outside the table (modal / list / field focus)
H = {"Escape": "Cancel", "Enter": "Enter", "UpArrow": "Up", "DownArrow": "Down", "LeftArrow": "Left", "RightArrow": "Right"}

# file -> {line: [(key, Act-or-helper), ...]}; a bare helper name (in H values) means Keymap::<helper>(rep)
SITES = {
 "src/app/panels/ArrangePanel.cpp": {
  916: [("Escape", "Cancel")], 941: [("A", "SelectAll")], 949: [("C", "Copy")], 951: [("X", "Cut")],
  956: [("V", "Paste")], 965: [("D", "Duplicate")], 970: [("E", "Split")], 972: [("G", "Ungroup")],
  977: [("G", "Group")], 982: [("R", "Rename")], 984: [("J", "TlAddVideoTrack")], 986: [("K", "TlAddAudioTrack")],
  989: [("0", "TlToggleEnabled"), ("Keypad0", "TlToggleEnabledPad")],
  991: [("Delete", "Delete"), ("Backspace", "DeleteAlt")], 998: [("M", "TlMarker")],
  1001: [("A", "ToolSelect")], 1006: [("T", "TlToolTrim")], 1011: [("R", "TlToolRange")],
  1016: [("B", "ToolBlade")], 1021: [("Z", "TlToolZoom")], 1026: [("H", "ToolHand")],
  1031: [("P", "TlToolPencil")], 1038: [("F", "FitViewF")], 1040: [("Equal", "ZoomIn")],
  1042: [("Minus", "ZoomOut")], 1044: [("L", "TlLoop")], 1046: [("I", "TlLoopIn")], 1048: [("O", "TlLoopOut")],
  1050: [("LeftArrow", "TlPrevMarker")], 1052: [("RightArrow", "TlNextMarker")],
  1054: [("LeftArrow", "TlNudgeLeft")], 1056: [("RightArrow", "TlNudgeRight")],
  1058: [("Home", "TlStart")], 1060: [("End", "TlEnd")], 1062: [("Escape", "Cancel")],
  1893: [("Escape", "Cancel")], 3534: [("Escape", "Cancel")],
 },
 "src/app/frame/StageLinks.cpp": {
  688: [("Z", "Undo")], 690: [("Z", "Redo")], 691: [("Y", "RedoAlt")], 698: [("A", "SelectAll")],
  743: [("LeftArrow", "Left")], 744: [("RightArrow", "Right")], 745: [("UpArrow", "Up")], 746: [("DownArrow", "Down")],
  779: [("Escape", "Cancel")], 885: [("Enter+KeypadEnter", "Confirm")], 901: [("H", "NodeHelp")],
  907: [("H", "NodeHelp")], 912: [("F", "FitViewF")], 916: [("U", "UngroupU")],
 },
 "src/app/panels/SampleStudioPanel.cpp": {
  2125: [("Z", "Undo")], 2127: [("Z", "Redo")], 2129: [("E", "Split")], 2131: [("C", "Copy")], 2133: [("X", "Cut")],
  2138: [("V", "Paste")], 2140: [("D", "Duplicate")], 2142: [("Space", "TogglePlay")], 2144: [("A", "SelectAllCmd")],
  2155: [("Delete", "Delete"), ("Backspace", "DeleteAlt")], 2159: [("Escape", "Cancel")],
  2167: [("Z", "SsZoomSelection")], 2169: [("Equal", "ZoomIn"), ("KeypadAdd", "ZoomInPad")],
  2171: [("Minus", "ZoomOut"), ("KeypadSubtract", "ZoomOutPad")],
  2174: [("LeftArrow", "SsPrevSlice")], 2176: [("RightArrow", "SsNextSlice")],
 },
 "src/app/panels/View3DPanel.cpp": {
  5539: [("Escape", "Cancel")], 5556: [("Backspace", "DeleteAlt")],
  6212: [("UpArrow", "Up"), ("DownArrow", "Down")], 6216: [("DownArrow", "Down")],
  6355: [("Delete", "Delete"), ("Backspace", "DeleteAlt"), ("X", "DeleteX")], 6357: [("C", "Copy")],
  6359: [("D", "Duplicate"), ("D", "Duplicate")], 6363: [("V", "Paste")],
 },
 "src/app/panels/CurveEditorPanel.cpp": {
  1424: [("G", "CeSnap")], 1426: [("F", "FitViewF")], 1428: [("Q", "CeQuantise")], 1430: [("N", "CeNext")],
  1435: [("Escape", "Cancel")], 1443: [("Delete", "Delete"), ("Backspace", "DeleteAlt")],
  1448: [("1", "CeInterp1")], 1449: [("2", "CeInterp2")], 1450: [("3", "CeInterp3")], 1451: [("4", "CeInterp4")],
  1452: [("5", "CeInterp5")], 1461: [("C", "Copy")], 1463: [("V", "Paste")], 1465: [("A", "SelectAllCmd")],
  1993: [("Escape", "Cancel")],
 },
 "src/app/panels/FieldPatcherPanel.cpp": {
  764: [("Enter", "Enter")], 854: [("Escape", "Cancel")], 1099: [("C", "Copy")], 1101: [("X", "Cut")],
  1108: [("V", "Paste")], 1110: [("D", "Duplicate")], 1118: [("A", "SelectAllCmd")],
  1120: [("Delete", "Delete"), ("Backspace", "DeleteAlt")], 1137: [("Escape", "Cancel")],
  1139: [("F", "FitViewF")], 1140: [("Y", "FitView")], 1754: [("Escape", "Cancel")],
 },
 "src/app/panels/PerfPanel.cpp": {
  2008: [("Escape", "Cancel")], 2083: [("Escape", "Cancel")], 2333: [("Delete", "Delete"), ("Backspace", "DeleteAlt")],
  2335: [("C", "Copy")], 2337: [("X", "Cut")], 2342: [("V", "Paste")], 2344: [("D", "Duplicate")],
  2346: [("A", "SelectAllCmd")], 2348: [("Escape", "Cancel")],
 },
 "src/app/frame/StagePresent.cpp": {
  35: [("0", "Settings"), ("Equal", "UiScaleUp")], 36: [("Minus", "UiScaleDown")], 38: [("0", "Settings")],
  40: [("Equal", "UiScaleUp"), ("Minus", "UiScaleDown")], 43: [("Equal", "UiScaleUp")], 51: [("S", "Save")],
  53: [("O", "Open")], 59: [("N", "New")],
 },
 "src/app/panels/ChatPanel.cpp": {
  2158: [("UpArrow", "Up"), ("DownArrow", "Down")], 2160: [("UpArrow", "Up")], 2806: [("Escape", "Cancel")],
  4246: [("Escape", "Cancel")], 5036: [("V", "Paste")], 5056: [("Escape", "Cancel")],
 },
 "src/app/panels/FindOverlay.cpp": {
  96: [("F", "Find")], 141: [("Enter+KeypadEnter", "Confirm")], 146: [("DownArrow", "Down")],
  147: [("UpArrow", "Up")], 158: [("Escape", "Cancel")],
 },
 "src/app/panels/View2DPanel.cpp": {1856: [("Delete", "Delete"), ("Backspace", "DeleteAlt")]},
 "src/app/panels/TerminalPanel.cpp": {828: [("Enter", "Enter")], 834: [("Escape", "Cancel")], 1387: [("Escape", "Cancel")]},
 "src/app/frame/StagePopupsB.cpp": {193: [("Escape", "Cancel")], 290: [("Enter", "Enter")], 539: [("Escape", "Cancel")]},
 "src/app/frame/StageFloating.cpp": {262: [("Escape", "Cancel")], 292: [("Escape", "Cancel")], 365: [("Escape", "Cancel")]},
 "src/app/bodies/NoteBodies.cpp": {731: [("Minus", "OctaveDown"), ("Equal", "OctaveUp")], 733: [("Equal", "OctaveUp")]},
 "src/app/panels/ArrangeSessionGrid.cpp": {708: [("Escape", "Cancel")], 1039: [("Escape", "Cancel")]},
 "src/app/bodies/PreviewBodies.cpp": {211: [("Enter+KeypadEnter", "Confirm")]},
 "src/app/panels/MixerPanel.cpp": {1258: [("R", "Rename")]},
 "src/app/frame/CanvasLink.cpp": {274: [("Escape", "Cancel")]},
}

HELPERS = set(H.values()) | {"Confirm"}
CALL = r"ImGui::IsKeyPressed\(\s*ImGuiKey_{key}\s*(?:,\s*(true|false)\s*)?\)"


def rewrite_line(line, key, act):
    if key == "Enter+KeypadEnter":
        rx = re.compile(r"ImGui::IsKeyPressed\(ImGuiKey_Enter(?:,\s*(true|false))?\)\s*\|\|\s*"
                        r"ImGui::IsKeyPressed\(ImGuiKey_KeypadEnter(?:,\s*(?:true|false))?\)")
    else:
        rx = re.compile(CALL.format(key=re.escape(key)))
    m = rx.search(line)
    if not m:
        return None
    rep = m.group(1) or "true"   # ImGui's default is repeat = true
    if act in HELPERS:
        new = f"Keymap::{act}({rep})"
    else:
        new = f"Keymap::KeyPressed(Keymap::Act::{act}{', true' if rep == 'true' else ''})"
    return line[:m.start()] + new + line[m.end():]


def ensure_include(text):
    if "app/ui/design/Keymap.h" in text:
        return text
    lines = text.split("\n")
    first = next(i for i, l in enumerate(lines) if l.startswith("#include"))
    lines.insert(first + 1, '#include "app/ui/design/Keymap.h"')
    return "\n".join(lines)


def main():
    dry = "--dry" in sys.argv
    total = 0
    acts = set()
    for rel, sites in SITES.items():
        p = os.path.join(ROOT, rel)
        lines = open(p, encoding="utf-8").read().split("\n")
        for ln, pairs in sites.items():
            for key, act in pairs:
                new = rewrite_line(lines[ln - 1], key, act)
                if new is None:
                    sys.exit(f"{rel}:{ln}: no IsKeyPressed(ImGuiKey_{key}) on this line:\n  {lines[ln - 1].strip()}")
                lines[ln - 1] = new
                total += 1
                if act not in HELPERS:
                    acts.add(act)
        if not dry:
            open(p, "w", encoding="utf-8").write(ensure_include("\n".join(lines)))
    print(f"{total} key checks moved onto Keymap; acts used: {' '.join(sorted(acts))}")


if __name__ == "__main__":
    main()
