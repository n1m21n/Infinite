"""Blender side of the Curve Editor bezier A/B (docs/plans/curve-editor/README.md M5).

Builds F-curves from the cases in fcurve.py, lets Blender solve the auto handles, and writes the keys (with the
handles Blender settled on) and the curve's value at every frame as plain text:

    case <name> <first frame> <last frame>
    key <frame> <value> <handle type> <left x> <left y> <right x> <right y>
    sample <frame> <value>

Run by fcurve.py:  Blender -b --factory-startup --python blender_fcurve.py -- cases.json out.txt
"""
import json
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
cases = json.load(open(argv[0]))
out = open(argv[1], "w")


def fcurves_of(action):
    # Blender 4.4+ keeps F-curves in slotted channel bags; older builds on the action.
    if hasattr(action, "fcurves") and action.fcurves is not None and len(action.fcurves) > 0:
        return list(action.fcurves)
    found = []
    for layer in getattr(action, "layers", []):
        for strip in layer.strips:
            for bag in getattr(strip, "channelbags", []):
                found.extend(bag.fcurves)
    return found


for case in cases:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    obj = bpy.data.objects.new("probe", None)
    bpy.context.scene.collection.objects.link(obj)
    for k in case["keys"]:
        obj.location[0] = k["v"]
        obj.keyframe_insert("location", index=0, frame=k["f"])
    fc = fcurves_of(obj.animation_data.action)[0]
    fc.auto_smoothing = "NONE"  # the classic auto handles; Continuous Acceleration is not matched (plan §13)
    for kp, k in zip(fc.keyframe_points, case["keys"]):
        kp.interpolation = "BEZIER"
        kp.handle_left_type = k.get("h", "AUTO_CLAMPED")
        kp.handle_right_type = k.get("h", "AUTO_CLAMPED")
    fc.update()
    for kp, k in zip(fc.keyframe_points, case["keys"]):
        if "l" in k: # free handles: set after the types, Blender keeps them as given
            kp.handle_left = (kp.co[0] + k["l"][0], kp.co[1] + k["l"][1])
            kp.handle_right = (kp.co[0] + k["r"][0], kp.co[1] + k["r"][1])
    fc.update()
    f0, f1 = case["keys"][0]["f"], case["keys"][-1]["f"]
    out.write("case %s %d %d\n" % (case["name"], f0, f1))
    for kp in fc.keyframe_points:
        out.write("key %.9g %.9g %s %.9g %.9g %.9g %.9g\n" % (
            kp.co[0], kp.co[1], kp.handle_left_type,
            kp.handle_left[0] - kp.co[0], kp.handle_left[1] - kp.co[1],
            kp.handle_right[0] - kp.co[0], kp.handle_right[1] - kp.co[1]))
    steps = case.get("steps", 1)
    for i in range((f1 - f0) * steps + 1):
        f = f0 + i / steps
        out.write("sample %.9g %.9g\n" % (f, fc.evaluate(f)))
out.close()
