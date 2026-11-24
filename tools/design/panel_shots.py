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
# name -> INFINITE_OPENPANELS value. Settings tabs need "settingswin" to open the window; the demo hooks give the
# Performance and History panels rows to show. No showcase patch: its previews animate on wall time, so two runs of
# one binary would never match; an empty canvas leaves only the panel chrome, which is what a layout sweep touches.
PANELS = {
    "settingsgeneral": "settingswin,settingsgeneral", "settingsglobals": "settingswin,settingsglobals",
    "settingsplugins": "settingswin,settingsplugins", "settingsreference": "settingswin,settingsreference",
    "settingsterminal": "settingswin,settingsterminal", "settingsdaw": "settingswin,settingsdaw",
    "settingsfield": "settingswin,settingsfield", "settings3d": "settingswin,settingstab=10",
    "arrange": "arrange", "mixer": "mixer", "drums": "drums", "library": "library", "perf": "perfdemo",
    "modmatrix": "modmatrix", "curveeditor": "curveeditor", "pianoroll": "pianoroll", "history": "historydemo",
    "view2d": "view2d", "view3d": "view3d", "terminal": "terminal",
}


def shoot(out, binary, panels):
    os.makedirs(out, exist_ok=True)
    for p in panels:
        shot = os.path.join(out, p + ".png")
        env = dict(os.environ, INFINITE_OPENPANELS=PANELS.get(p, p), INFINITE_NO_UPDATE_CHECK="1",
                   IMAGERESYNTH_SCREENSHOT=shot, INFINITE_SCREENSHOT_FRAME="30")
        try:
            subprocess.run([binary], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120, check=False)
        except subprocess.TimeoutExpired:
            print(f"{p}: timed out")
            continue
        print(f"{p}: {'ok' if os.path.exists(shot) else 'no shot'}", flush=True)


# Two shoots of one binary differ by mean ~0.25 and <= 0.02% of pixels off by >24 (the canvas grid's sub-pixel
# shimmer, measured 2026-11-24 over all panels). A moved row or a changed gap shifts whole lines of text, far past this.
MAX_BIG = 0.0005


def diff(cand, gold, diff_out):
    from PIL import Image, ImageChops
    a, b = Image.open(cand).convert("RGB"), Image.open(gold).convert("RGB")
    if a.size != b.size:
        return False, 1.0, 255.0
    d = ImageChops.difference(a, b).convert("L")
    d.paste(0, (0, 0, d.size[0], 64))   # the menu bar shows live fps
    hist = d.histogram()
    n = a.size[0] * a.size[1]
    big = sum(hist[25:]) / n
    ok = big <= MAX_BIG
    if not ok and diff_out:
        d.point(lambda v: 255 if v > 24 else 0).save(diff_out)
    return ok, big, sum(i * c for i, c in enumerate(hist)) / n


def compare(before, after, diff_out):
    os.makedirs(diff_out, exist_ok=True)
    bad = 0
    for name in sorted(os.listdir(before)):
        if not name.endswith(".png"):
            continue
        a = os.path.join(after, name)
        if not os.path.exists(a):
            print(f"{name}: missing in {after}"); bad += 1; continue
        ok, big, mean = diff(a, os.path.join(before, name), os.path.join(diff_out, name))
        print(f"{name}: {'same' if ok else 'DIFF'}  mean {mean:.3f}, pixels >24: {big*100:.4f}%")
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
