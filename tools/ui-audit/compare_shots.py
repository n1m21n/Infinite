#!/usr/bin/env python3
"""Before / after shot sheets: the same scenes from two built apps, dark and light, side by side.

    python3 tools/ui-audit/compare_shots.py --before <old Infinite.app> --after <new Infinite.app> --out <dir>

Each scene is one full-window capture through the app's own review hooks (INFINITE_OPENPANELS, IMAGERESYNTH_SCREENSHOT,
the same route tools/docs-shots/capture.py uses), so the menu bar and the startup patch's node bodies are in every shot.
Writes <out>/<scene>-<theme>.png (before | after | difference) and <out>/report.md with the share of changed pixels.
Never builds; refuses nothing, so run it only when no build is running (8 GB Mac).
"""
import argparse, os, subprocess, sys, tempfile

from PIL import Image, ImageChops, ImageDraw

SCENES = {
    "canvas":   "",                               # menu bar + node bodies of the startup patch
    "view2d":   "view2d",
    "view3d":   "view3d",
    "settings": "settingswin,settingsgeneral",
    "mixer":    "mixer",
}


def binary(app):
    return os.path.join(app, "Contents/MacOS/Infinite") if app.endswith(".app") else app


def shoot(app, theme, panels, out, timeout=120):
    env = dict(os.environ, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_NO_UPDATE_CHECK="1", INFINITE_NOSPLASH="1",
               INFINITE_HIDE_TELEMETRY="1", IMAGERESYNTH_SCREENSHOT=out, INFINITE_SCREENSHOT_FRAME="40",
               INFINITE_OPENPANELS=",".join(x for x in (theme, panels) if x), HOME=tempfile.mkdtemp(prefix="shots-home-"))
    if os.path.exists(out): os.remove(out)
    try:
        subprocess.run([binary(app)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        pass
    return os.path.exists(out)


def sheet(a, b, out, title):
    A, B = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if A.size != B.size: B = B.resize(A.size)
    d = ImageChops.difference(A, B).convert("L").point(lambda v: 255 if v > 24 else 0)
    changed = sum(d.histogram()[255:]) / (A.width * A.height)
    red = Image.new("RGB", A.size, (230, 40, 40))
    diff = Image.composite(red, A.point(lambda v: v // 3), d)
    s = 0.5
    w, h = int(A.width * s), int(A.height * s)
    S = Image.new("RGB", (w * 3 + 40, h + 60), (24, 26, 32))
    for k, (im, lab) in enumerate(((A, "before 988c668d"), (B, "after"), (diff, "changed pixels (red)"))):
        S.paste(im.resize((w, h)), (10 + k * (w + 10), 50))
        ImageDraw.Draw(S).text((10 + k * (w + 10), 15), f"{title}: {lab}", fill=(235, 235, 235))
    S.save(out)
    return changed, d.getbbox()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", required=True)
    ap.add_argument("--after", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scenes", default=",".join(SCENES))
    a = ap.parse_args()
    raw = os.path.join(a.out, "raw")
    os.makedirs(raw, exist_ok=True)
    rows = []
    for sc in a.scenes.split(","):
        for theme in ("dark", "light"):
            pa, pb = (os.path.join(raw, f"{sc}-{theme}-{w}.png") for w in ("before", "after"))
            ok = shoot(a.before, theme, SCENES[sc], pa) and shoot(a.after, theme, SCENES[sc], pb)
            if not ok:
                rows.append((sc, theme, "no capture", "")); print(sc, theme, "no capture"); continue
            frac, box = sheet(pa, pb, os.path.join(a.out, f"{sc}-{theme}.png"), f"{sc} ({theme})")
            rows.append((sc, theme, f"{frac * 100:.2f}%", str(box or "-")))
            print(sc, theme, f"{frac * 100:.2f}% changed", box)
    with open(os.path.join(a.out, "report.md"), "w") as f:
        f.write("| Scene | Theme | Changed pixels | Changed area (x0, y0, x1, y1) |\n|---|---|---|---|\n")
        for r in rows: f.write("| %s | %s | %s | %s |\n" % r)
    return 0


if __name__ == "__main__":
    sys.exit(main())
