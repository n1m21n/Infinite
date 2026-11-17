#!/usr/bin/env python3
"""Checks every docked panel against internal/plans/ui-system/panel-contract.md (the rules code can see).

Prints one row per panel and exits 1 when a panel breaks a rule it is held to. Usage:
    python3 tools/design/panel_contract.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
PANELS = ROOT / "src/app/panels"
KEYS = (ROOT / "src/app/frame/StageKeyboard.cpp").read_text()

# name, files, claimed-keys global, paged, node headers holding the page fields
SPEC = [
    ("Timeline", ["ArrangePanel.cpp"], "gArrangeClaimedKeys", False, []),
    ("Piano Roll", ["PianoRollPanel.cpp"], "gPianoRollClaimedKeys", True, []),
    ("Drums", ["DrumPanel.cpp"], "gDrumClaimedKeys", True, ["src/nodes/DrumSequencerNode.h"]),
    ("Mixer", ["MixerPanel.cpp", "MixerRender.cpp"], "gMixerClaimedKeys", True, ["src/nodes/AudioNodes.h"]),
    ("Curve Editor", ["CurveEditorPanel.cpp"], "gCurveEditorClaimedKeys", True, ["src/nodes/ModulatorNodes.h"]),
    ("Performance", ["PerfPanel.cpp"], "gPerfMatrixFocused", True, []),
    ("Field Patcher", ["FieldPatcherPanel.cpp"], "gFieldPatcherClaimedKeys", False, []),
    ("Sample Studio", ["SampleStudioPanel.cpp"], "gSampleStudioClaimedKeys", False, []),
    ("2D View", ["View2DPanel.cpp"], "gView2DClaimedKeys", False, []),
    ("3D View", ["View3DPanel.cpp"], "gView3DClaimedKeys", False, []),
]
# 10: one top-bar word per action, the same on every panel (Audio, Render, Pop Out), never a sentence
BAR_WORDS_OUT = ["Enable Session Audio", "Session Audio On", "Enable Timeline Audio", "Timeline Audio On", "Full Screen Patcher"]
# 11: pinned right of every top bar, left to right: Render, Audio, inspector toggle in the corner
BAR_RIGHT = {"Timeline": ["ArrangePanel.cpp", "##clipsettingstoggle"], "Session": ["ArrangeSessionGrid.cpp", "##sessioninspectortoggle"]}
APP_SHIFT = {"R": "Piano Roll", "E": "Curve Editor", "J": "Drums", "F": "Field Patcher"}


def gate_for(letter):
    """The StageKeyboard condition line for Shift+<letter>, or ''."""
    for line in KEYS.splitlines():
        if f"ImGuiKey_{letter}, false" in line and "shiftOnly" in line:
            return line
    return ""


def main():
    bad = 0
    print(f"{'panel':<14} {'title':<6} {'pages':<6} {'fields':<6} {'card':<5} {'dock':<5} {'keys':<5} shift-clash")
    for name, files, claim, paged, headers in SPEC:
        src = "\n".join((PANELS / f).read_text() for f in files if (PANELS / f).exists())
        # 1: the panel name drawn as text (floating window titles and menus are fine)
        # (SectionCard::Title heads a side panel with its own close button, not the top bar)
        title = re.search(r'(TextUnformatted|[^:]Text|AddText)\([^;]*T\("' + re.escape(name) + r'"\)', src)
        pages = "PagePicker::Draw" in src
        fields = all("pageName" in (ROOT / h).read_text() and "pageColor" in (ROOT / h).read_text() for h in headers) if headers else None
        card = "PushDockedPanelStyle" in src
        dock = bool(re.search(r"Dock(Menu|Labels)|##\w*[Dd]ock\w*[Mm]enu", src))
        keys = claim in src and claim in KEYS
        # 8: panel handlers that take Shift+<app letter> without the app gating that key for the panel
        clash = []
        for letter, owner in APP_SHIFT.items():
            if owner == name:
                continue
            gate = gate_for(letter)
            if claim in gate or (name == "Timeline" and "gArrangeFocused" in gate):
                continue
            for line in src.splitlines():
                if not re.search(r"ImGuiKey_" + letter + r"\b", line):
                    continue
                shift_on = re.search(r"(?<![!\w.])(shift|io\.KeyShift|kio\.KeyShift)\b", line)
                cmd_on = re.search(r"(?<![!\w])(cmd|command|io\.KeySuper|io\.KeyCtrl)\b", line)
                if shift_on and not cmd_on:
                    clash.append(letter)
                    break
        row_bad = bool(title) or (paged and not pages) or (fields is False) or not card or not keys or bool(clash)
        bad += row_bad
        yn = lambda v: "-" if v is None else ("yes" if v else "NO")
        print(f"{name:<14} {'NO' if title else 'ok':<6} {yn(pages) if paged else '-':<6} {yn(fields):<6} {yn(card):<5} {yn(dock):<5} {yn(keys):<5} "
              f"{','.join('Shift+' + c for c in clash) or 'ok'}")
    for f in sorted(PANELS.glob("*.cpp")):
        t = f.read_text()
        for w in BAR_WORDS_OUT:
            if f'T("{w}")' in t or f'L("{w}")' in t:
                print(f"{f.name}: top bar says '{w}' - one word per action (Audio, Render, Pop Out)")
                bad += 1
    for name, (f, toggle) in BAR_RIGHT.items():
        t = (PANELS / f).read_text()
        # Audio sits one space_1 left of the corner toggle; Render / Render Take sit left of Audio.
        if 'T("Audio")' not in t or toggle not in t or not re.search(r"space_1 - audio\w*W", t):
            print(f"{name}: top bar right side is not Render, Audio, inspector toggle")
            bad += 1
    # 9: canvas-only keys stay off while any panel has the keys
    for letter in ("H", "Y"):
        if "gArrangeFocused" not in gate_for(letter):
            print(f"Shift+{letter}: canvas key not gated on gArrangeFocused")
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
