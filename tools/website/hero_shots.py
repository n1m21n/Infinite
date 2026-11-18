#!/usr/bin/env python3
"""Hero, showcase and README shots for the 2.0 website and README, from a release app copy.

Each shot is one app launch driven by the app's own env hooks (no UI scripting), written to docs/screenshots/v2/<id>.png
and checked against the UI rule: at least 3200 x 2000 px (the 2x backbuffer of a 1600 x 1000 pt window). A shot that
misses the rule fails the run; nothing is copied into the website or README.

Usage (one process at a time; the app is a COPY with its own HOME and port):
  python3 tools/website/hero_shots.py --app /scratch/shotapp/Infinite.app --home /scratch/home --port 7796 [--only id,id]

Scenes:
  hero-panels     node canvas with the modulation matrix, viewport and performance panels open
  node-graph      the Showcase patch on the canvas, fitted
  showcase-frames the Showcase patch, panels closed (the README's four-frame strip is cut from this)
  mod-matrix      modulation matrix panel
  performance     performance panel
  arrange         arrange (timeline) panel
  settings        settings window, general tab
  gallery-dark    UI element sheet, dark (website UI section)
  gallery-light   UI element sheet, light
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "docs-shots"))
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
import common as C  # noqa: E402
from PIL import Image  # noqa: E402

OUT = os.path.join(ROOT, "docs", "screenshots", "v2")
SHOWCASE = os.path.join(ROOT, "docs", "showcase", "infinite-v1-showcase.inf")
UI_MIN = (3200, 2000)

# id -> env for the app. The panel tokens follow capture.py's SCENES; the showcase patch loads through INFINITE_LOADPATCH.
SCENES = {
    "hero-panels":     {"INFINITE_LOADPATCH": SHOWCASE, "INFINITE_OPENPANELS": "dark,modmatrix,viewport,perfdemo"},
    "node-graph":      {"INFINITE_LOADPATCH": SHOWCASE, "INFINITE_OPENPANELS": "dark"},
    "showcase-frames": {"INFINITE_LOADPATCH": SHOWCASE, "INFINITE_OPENPANELS": "dark"},
    "mod-matrix":      {"INFINITE_OPENPANELS": "dark,modmatrix"},
    "performance":     {"INFINITE_OPENPANELS": "dark,perfdemo"},
    "arrange":         {"INFINITE_OPENPANELS": "dark,arrangedemo"},
    "3d-view":          {"INFINITE_LOADPATCH": SHOWCASE, "INFINITE_OPENPANELS": "dark,view3d"},
    "mixer":           {"INFINITE_LOADPATCH": SHOWCASE, "INFINITE_OPENPANELS": "dark,mixer"},
    "settings":        {"INFINITE_OPENPANELS": "dark,settingswin,settingsgeneral"},
    "gallery-dark":    {"INFINITE_UIGALLERY": "dark"},
    "gallery-light":   {"INFINITE_UIGALLERY": "light"},
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", required=True, help="a COPY of Infinite.app (never the gate's or the manuals lane's binary)")
    ap.add_argument("--home", required=True, help="scratch HOME for this app's settings and token")
    ap.add_argument("--port", type=int, default=7796)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    if not os.path.exists(SHOWCASE):
        sys.exit("missing showcase patch " + SHOWCASE)
    binary = os.path.join(a.app, "Contents", "MacOS", "Infinite")
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(os.path.join(a.home, "Desktop"), exist_ok=True)
    want = [s for s in (a.only.split(",") if a.only else SCENES)]
    fails = []
    for sid in want:
        env = dict(os.environ, HOME=a.home, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_NO_UPDATE_CHECK="1",
                   INFINITE_NOSPLASH="1", INFINITE_HIDE_TELEMETRY="1", INFINITE_CONTROL_PORT=str(a.port),
                   IMAGERESYNTH_SCREENSHOT=os.path.join(OUT, sid + ".raw.png"), INFINITE_SCREENSHOT_FRAME="40")
        env.update(SCENES[sid])
        raw = env["IMAGERESYNTH_SCREENSHOT"]
        if os.path.exists(raw):
            os.remove(raw)
        try:
            subprocess.run([binary, "-ApplePersistenceIgnoreState", "YES"], env=env, timeout=120,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        if not os.path.exists(raw):
            fails.append((sid, "no image")); print("FAIL", sid, "no image", flush=True); continue
        w, h = Image.open(raw).size
        if w < UI_MIN[0] or h < UI_MIN[1]:
            os.remove(raw)
            fails.append((sid, f"{w}x{h} below {UI_MIN[0]}x{UI_MIN[1]}"))
            print("FAIL", sid, f"{w}x{h}", flush=True); continue
        os.replace(raw, os.path.join(OUT, sid + ".png"))
        print("ok  ", sid, f"{w}x{h}", flush=True)
    print("failed:", fails or "none")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
