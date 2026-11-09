#!/usr/bin/env python3
"""Curve Editor bezier keys against Blender's F-curve (docs/plans/curve-editor/README.md M5, Blender half).

The same keys go through Blender (blender_fcurve.py) and Infinite's MSEG (MSEGTEST with INFINITE_CURVEBLENDER set);
Infinite reports the worst value gap over every frame and fails above 1e-3.

    python3 tools/blender-ab/fcurve.py            # needs /Applications/Blender.app and build-dev/Infinite.app
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
BLENDER = os.environ.get("BLENDER", "/Applications/Blender.app/Contents/MacOS/Blender")
APP = ROOT / "build-dev/Infinite.app/Contents/MacOS/Infinite"


def k(f, v, h="AUTO_CLAMPED", l=None, r=None):
    d = {"f": f, "v": v, "h": h}
    if l is not None:
        d["l"], d["r"] = l, r
    return d


CASES = [
    {"name": "auto_clamped_wave", "keys": [k(1, 0.0), k(13, 0.8), k(25, 0.3), k(49, 1.0), k(61, 0.0)]},
    {"name": "auto_clamped_ramp", "keys": [k(1, 0.1), k(10, 0.3), k(20, 0.6), k(40, 0.9)]},
    {"name": "auto_uneven", "keys": [k(1, 0.2, "AUTO"), k(5, 0.9, "AUTO"), k(30, 0.4, "AUTO"), k(36, 0.6, "AUTO"), k(60, 0.1, "AUTO")]},
    {"name": "vector", "keys": [k(1, 0.0, "VECTOR"), k(24, 1.0, "VECTOR"), k(48, 0.25, "VECTOR")]},
    {"name": "free_overlong", "keys": [k(1, 0.0, "FREE", [-5, 0], [30, 0.0]), k(25, 1.0, "FREE", [-30, 0.2], [6, 0.0]),
                                       k(49, 0.5, "FREE", [-4, 0.3], [4, -0.3])]},
    {"name": "aligned_ease", "keys": [k(1, 0.0, "ALIGNED", [-8, 0], [8, 0]), k(30, 1.0, "ALIGNED", [-20, 0], [20, 0])],
     "steps": 4},
]


def main():
    if not pathlib.Path(BLENDER).exists():
        print("fcurve A/B: no Blender at", BLENDER, "- skipped")
        return 0
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="curve-ab-"))
    cases, truth = tmp / "cases.json", tmp / "blender.txt"
    cases.write_text(json.dumps(CASES))
    subprocess.run([BLENDER, "-b", "--factory-startup", "--python", str(ROOT / "tools/blender-ab/blender_fcurve.py"), "--",
                    str(cases), str(truth)], check=True, capture_output=True)
    env = dict(os.environ, INFINITE_MSEGTEST="1", INFINITE_CURVEBLENDER=str(truth))
    log = tmp / "infinite.txt"
    subprocess.run(["script", "-q", str(log), str(APP)], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, timeout=300)
    text = log.read_text(errors="replace")
    lines = [ln for ln in text.splitlines() if "BLENDER" in ln]
    print("\n".join(lines))
    return 0 if lines and all("FAIL" not in ln for ln in lines) else 1


if __name__ == "__main__":
    sys.exit(main())
