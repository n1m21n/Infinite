#!/usr/bin/env python3
"""Menu grounding: each right-click menu holds the verbs its reference app puts in the same menu.

One reference app per menu (lint-plan Block E, E4). For each verb that app offers on a right click, the table names
the Infinite row that does it, or None when Infinite has no such row yet (a gap). A mapped row that is gone from the
menu's source fails, so a grounded verb cannot quietly drop out. Gaps are printed, never fail: closing one is either
a fix (an existing command with its existing key, put in the menu) or a new feature that needs the owner's yes.
"""
import pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
P = "src/app/panels/"

# root -> (source file, reference app, {reference verb: Infinite row or None})
GROUND = {
    "##nodecontext": ("src/app/frame/StagePopupsA.cpp", "TouchDesigner node", {
        "Rename": "Rename", "Duplicate": "Duplicate", "Bypass": "Bypass Node", "Delete": "Delete Node",
        "Viewer": "Show viewport", "Help": "Help", "Copy": "Copy", "Cut": "Cut"}),
    "##sessionslotctx": (P + "ArrangeSessionGrid.cpp", "Ableton Session slot", {
        "Insert clip": "Add Clip", "Paste": "Paste", "Stop": "Stop Track", "Launch": "Launch Row in Group"}),
    "##sessionscenectx": (P + "ArrangeSessionGrid.cpp", "Ableton Session scene", {
        "Launch": "Launch Scene", "Insert Scene": "Insert Scene Above", "Duplicate": "Duplicate Scene",
        "Delete": "Delete Scene", "Stop": "Stop Scene", "Follow action": "Then Play Next Scene"}),
    "##sessiontrackctx": (P + "ArrangeSessionGrid.cpp", "Ableton Session track", {
        "Insert Audio Track": "Add Audio Track", "Group Tracks": "Group This Track", "Ungroup": "Ungroup Track",
        "Stop": "Stop Track", "Delete": "Delete Track", "Rename": "Rename Track", "Duplicate": "Duplicate Track"}),
    "##arrangeclipctx": (P + "ArrangePanel.cpp", "Ableton Arrangement / Premiere clip", {
        "Rename": "Rename", "Colour": "Colour Tint", "Copy": "Copy", "Duplicate": "Duplicate",
        "Split": "Split at Playhead", "Group": "Group", "Ungroup": "Ungroup", "Delete": "Delete",
        "Enable": "Active", "Blend mode": "Blend Mode", "Clip properties": "Clip Settings", "Cut": "Cut"}),
    "##arrangelanectx": (P + "ArrangePanel.cpp", "Ableton Arrangement track", {
        "Rename": "Rename Track", "Delete": "Delete Track", "Insert Audio Track": "Add Audio Track",
        "Group Tracks": "Group Selected", "Freeze / Bounce": "Render Track", "Duplicate": "Duplicate Track"}),
    "##arrangegroupctx": (P + "ArrangePanel.cpp", "Ableton Arrangement group", {
        "Rename": "Rename Group", "Duplicate": "Duplicate Group", "Deactivate": "Toggle Enabled",
        "Bounce": "Render Group", "Ungroup": "Ungroup (Keep Tracks)"}),
    "##arrangemarkerctx": (P + "ArrangePanel.cpp", "Ableton locator / Premiere marker", {
        "Rename": "Rename", "Colour": "Colour", "Delete": "Delete Marker"}),
    "##arrangeemptyspacectx": (P + "ArrangePanel.cpp", "Ableton Arrangement empty space", {
        "Paste": "Paste at Playhead", "Insert Audio Track": "Add Audio Track", "Insert Video Track": "Add Video Track"}),
    "##rowmenu": (P + "View2DPanel.cpp", "Photoshop layer", {
        "Duplicate Layer": "Duplicate Layer", "Delete Layer": "Delete Layer", "Rename": "Rename",
        "Group from Layers": "Group Layers", "Ungroup": "Ungroup", "Merge Down": "Merge Down",
        "Merge Visible": "Merge Visible", "Flatten Image": "Flatten Image", "Rasterize Layer": "Rasterize Layer",
        "Add Layer Mask": "Add Layer Mask", "Vector Mask": "Add Vector Mask", "Clipping Mask": "Clipping mask",
        "Blending Options": "Blend mode", "Lock": "Lock layer", "Copy": "Copy", "Paste": "Paste"}),
    "##v3dcontext": (P + "View3DPanel.cpp", "Blender object", {
        "Duplicate": "Duplicate", "Copy": "Copy", "Paste": "Paste", "Rename": "Rename", "Hide": "Hide",
        "Delete": "Remove", "Frame selected": "Frame", "Group": "Group", "Ungroup": "Ungroup", "Add": "Add",
        "Select": "Select"}),
    "##studioWaveMenu": (P + "SampleStudioPanel.cpp", "Ableton Simpler slice", {
        "Split": "Split here", "Join": "Join", "Copy": "Copy", "Paste": "Paste at playhead", "Delete": "Delete",
        "Preview": "Play slice", "Reverse": "Reverse", "Colour": "Colour", "Zoom to selection": "Zoom to selection",
        "Slice to Drum Rack": "Convert to node"}),
    "##curveMenu": (P + "CurveEditorPanel.cpp", "Ableton envelope / Blender graph", {
        "Interpolation": "Handles", "Quantise": "Quantise to Grid", "Delete": "Delete", "Normalise": "Normalise",
        "Copy": "Copy", "Paste": "Paste"}),
    "##mixerpanelctx": (P + "MixerPanel.cpp", "Ableton mixer", {
        "Insert return track": "New send FX", "Solo off": "Clear solo", "Snapshot": "Store snapshot"}),
    "##fpNodeMenu": (P + "FieldPatcherPanel.cpp", "TouchDesigner node", {
        "Duplicate": "Duplicate", "Copy": "Copy", "Cut": "Cut", "Delete": "Delete", "Disconnect": "Disconnect",
        "Rename": "Rename"}),
    "##fpLinkMenu": (P + "FieldPatcherPanel.cpp", "TouchDesigner wire", {"Delete": "Delete Wire"}),
}


def main():
    bad, gaps = [], []
    for root, (src, app, verbs) in GROUND.items():
        text = (ROOT / src).read_text(errors="ignore")
        if f'"{root}"' not in text:
            bad.append(f"{root}: not opened in {src} any more")
        for verb, row in verbs.items():
            if row is None:
                gaps.append(f"{root} ({app}): no row for {verb}")
            elif f'"{row}"' not in text:
                bad.append(f'{root} ({app}): row "{row}" for {verb} is gone from {src}')
    for g in gaps:
        print("menu_grounding: gap", g)
    for b in bad:
        print("menu_grounding: FAIL", b)
    if bad:
        return 1
    mapped = sum(1 for _, _, v in GROUND.values() for r in v.values() if r)
    print(f"menu_grounding: OK ({len(GROUND)} menus, {mapped} verbs grounded, {len(gaps)} gaps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
