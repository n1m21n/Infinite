#!/usr/bin/env python3
"""Whole-app screenshots of the panels a UI migration touches, from any binary, and a pixel diff of two runs.
Used by ui-formalisation Blocks B/C: shoot with the build before the sweep, shoot again after, compare.

  panel_shots.py shoot OUT_DIR [--binary PATH] [--panels a,b,...]
  panel_shots.py compare BEFORE_DIR AFTER_DIR [--diff-out DIR]

Panels are INFINITE_OPENPANELS names (src/app/Startup.cpp, SettingsWindow.cpp). One app launch per panel, one at a
time (the 8 GB machine). compare uses golden.py's tolerance (top 64 px, the live menu bar, ignored); run two
baseline shoots and compare them first to see each panel's noise floor (animated panels never match exactly).
"""
import argparse, os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import golden  # noqa: E402

ROOT = golden.ROOT
PANELS = ["settingsgeneral", "settingsglobals", "settingsplugins", "settingsreference", "settingsterminal", "settingsdaw",
          "arrange", "mixer", "drums", "library", "perf", "modmatrix", "curveeditor", "pianoroll", "history",
          "view2d", "view3d", "terminal"]


def shoot(out, binary, panels):
    os.makedirs(out, exist_ok=True)
    for p in panels:
        shot = os.path.join(out, p + ".png")
        env = dict(os.environ, INFINITE_OPENPANELS=p, INFINITE_NO_UPDATE_CHECK="1", IMAGERESYNTH_SCREENSHOT=shot,
                   INFINITE_SHOWCASE="1")
        try:
            subprocess.run([binary], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120, check=False)
        except subprocess.TimeoutExpired:
            print(f"{p}: timed out")
            continue
        print(f"{p}: {'ok' if os.path.exists(shot) else 'no shot'}", flush=True)


def compare(before, after, diff_out):
    os.makedirs(diff_out, exist_ok=True)
    bad = 0
    for name in sorted(os.listdir(before)):
        if not name.endswith(".png"):
            continue
        a = os.path.join(after, name)
        if not os.path.exists(a):
            print(f"{name}: missing in {after}"); bad += 1; continue
        ok, msg = golden.diff_images(a, os.path.join(before, name), os.path.join(diff_out, name))
        print(f"{name}: {'same' if ok else 'DIFF'} {msg}")
        bad += 0 if ok else 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("shoot"); s.add_argument("out"); s.add_argument("--binary", default=golden.default_binary())
    s.add_argument("--panels", default=",".join(PANELS))
    c = sub.add_parser("compare"); c.add_argument("before"); c.add_argument("after"); c.add_argument("--diff-out", default=None)
    a = ap.parse_args()
    if a.cmd == "shoot":
        shoot(a.out, a.binary, [p for p in a.panels.split(",") if p])
        return 0
    return compare(a.before, a.after, a.diff_out or os.path.join(a.after, "diff"))


if __name__ == "__main__":
    sys.exit(main())
