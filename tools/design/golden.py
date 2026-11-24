#!/usr/bin/env python3
"""Render the UI golden pages in light and dark and diff against goldens.
usage:
  golden.py [--update] [--binary PATH] [--render-out DIR] [--pages gallery,bodies,...]
  golden.py --compare CAND_DIR GOLD_DIR

Pages (ui-formalisation section 7): the component gallery (INFINITE_UIGALLERY), six node bodies with their params
open (the Block C pilots and one per family), Settings, Mixer and the Drums panel (a compact inspector strip).
Goldens live at tests/ui-golden/<platform>/<page>-{light,dark}.png (macos, windows, linux). Text rasterization
differs per OS, so each platform keeps its own goldens; a page with no golden on a platform is skipped there.
The top 64 px (the live menu bar) is ignored. Tolerance: the gallery draws on a flat window, so mean abs channel
diff 0.05 and at most 0.02 % of pixels changed by > 24; pages over the node canvas carry its grid's sub-pixel
shimmer (two runs of one binary: mean ~0.25, <= 0.02 % > 24 - panel_shots.py), so mean 0.5 and 0.05 %.
Exit 1 on any page past its tolerance. Blocking in the hygiene harness (run-infinite-hygiene, ui group).
"""
import argparse, os, platform, subprocess, sys, tempfile
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PLAT = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(platform.system(), platform.system().lower())


def default_binary():
    if os.environ.get("INFINITE_BIN"):
        return os.environ["INFINITE_BIN"]
    if platform.system() == "Darwin":
        return os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
    if platform.system() == "Windows":
        return os.path.join(ROOT, "build/Release/Infinite.exe")
    for cand in ("build-linux/Infinite", "build/Infinite"):
        p = os.path.join(ROOT, cand)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, "build-linux/Infinite")


# page -> (env, tolerance). Themes: the gallery picks its own (INFINITE_UIGALLERY=light|dark); the others take the
# theme as the first INFINITE_OPENPANELS word.
PAGES = {
    "gallery": (lambda mode: {"INFINITE_UIGALLERY": mode}, "flat"),
    "bodies": (lambda mode: {"INFINITE_NODEGALLERY": "*|Shape,Audio Filter,Chorus,Oscillator,Arpeggiator,Cube",
                             "INFINITE_GALLERYGRID": "3,520,760", "INFINITE_GALLERYPARAMS": "1",
                             "INFINITE_OPENPANELS": mode, "INFINITE_SCREENSHOT_FRAME": "40"}, "canvas"),
    "settings": (lambda mode: {"INFINITE_OPENPANELS": mode + ",settingswin,settingsgeneral"}, "canvas"),
    # The Mixer and Drums pages spawn their node first: without one they show only an empty state.
    "mixer": (lambda mode: {"INFINITE_NODEGALLERY": "*|Mixer,Oscillator,Chorus", "INFINITE_GALLERYGRID": "3,520,760",
                            "INFINITE_OPENPANELS": mode + ",mixer", "INFINITE_SCREENSHOT_FRAME": "40"}, "canvas"),
    "drums": (lambda mode: {"INFINITE_NODEGALLERY": "*|Drum Sequencer", "INFINITE_GALLERYGRID": "3,520,760",
                            "INFINITE_OPENPANELS": mode + ",drums", "INFINITE_SCREENSHOT_FRAME": "40"}, "live"),
}
# live: the fixture's transport plays, so the Drum Sequencer's playhead column (node and panel) sits on a wall-time
# step - about 0.2 % of pixels run to run. A moved control or row shifts far more than 0.4 %.
TOL = {"flat": (0.05, 0.0002), "canvas": (0.5, 0.0005), "live": (0.5, 0.004)}


def diff_images(cand_path, gold_path, diff_out, tol="flat"):
    a, b = Image.open(cand_path).convert("RGB"), Image.open(gold_path).convert("RGB")
    if a.size != b.size:
        return False, f"size {a.size} != golden {b.size}"
    d = ImageChops.difference(a, b).convert("L")
    d.paste(0, (0, 0, d.size[0], 64))   # the menu bar shows live values (fps, beat); the gallery sits below it
    hist = d.histogram()
    n = a.size[0] * a.size[1]
    mean = sum(i * c for i, c in enumerate(hist)) / n
    big = sum(hist[25:]) / n
    ok = mean <= TOL[tol][0] and big <= TOL[tol][1]
    if not ok and diff_out:
        d.point(lambda v: 255 if v > 24 else 0).save(diff_out)
    return ok, f"mean diff {mean:.4f}, pixels >24: {big*100:.4f}%  {'ok' if ok else 'DIFFERS'}"


def compare_dirs(cand_dir, gold_dir):
    bad = 0
    for page, (_, tol) in PAGES.items():
        for mode in ("light", "dark"):
            name = f"{page}-{mode}"
            cand = os.path.join(cand_dir, name + ".png")
            gold = os.path.join(gold_dir, name + ".png")
            if not os.path.exists(gold):
                if page == "gallery":
                    print(f"{name}: missing golden {gold}"); bad += 1
                continue
            if not os.path.exists(cand):
                print(f"{name}: missing candidate {cand}"); bad += 1; continue
            ok, msg = diff_images(cand, gold, os.path.join(cand_dir, f"diff-{name}.png"), tol)
            print(f"{name}: {msg}")
            if not ok:
                bad += 1
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--update", action="store_true", help="Write/overwrite goldens for this platform")
    ap.add_argument("--binary", default=None, help="Path to Infinite binary")
    ap.add_argument("--render-out", default=None, help="Also save rendered PNGs into this directory (CI artifacts)")
    ap.add_argument("--pages", default="", help="Comma list of pages (default: all): " + ",".join(PAGES))
    ap.add_argument("--compare", nargs=2, metavar=("CAND_DIR", "GOLD_DIR"), help="Compare two directories without rendering")
    args = ap.parse_args()

    if args.compare:
        return compare_dirs(args.compare[0], args.compare[1])

    gdir = os.path.join(ROOT, "tests", "ui-golden", PLAT)
    exe = args.binary or default_binary()
    if args.update:
        os.makedirs(gdir, exist_ok=True)
    if args.render_out:
        os.makedirs(args.render_out, exist_ok=True)

    pages = [p for p in (args.pages.split(",") if args.pages else PAGES) if p]
    bad = 0
    tmp_dir = tempfile.mkdtemp()
    for page in pages:
        envf, tol = PAGES[page]
        for mode in ("light", "dark"):
            name = f"{page}-{mode}"
            gold = os.path.join(gdir, name + ".png")
            if not args.update and not args.render_out and not os.path.exists(gold):
                continue   # no golden for this page on this platform: nothing to compare, skip the launch
            out = os.path.join(args.render_out or tmp_dir, name + ".png")
            env = dict(os.environ, INFINITE_NO_UPDATE_CHECK="1", INFINITE_SCREENSHOT_FRAME="20",
                       IMAGERESYNTH_SCREENSHOT=out)
            env.update(envf(mode))
            subprocess.run([exe], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
                           timeout=180)
            if not os.path.exists(out):
                print(f"{name}: render failed"); bad += 1; continue
            if args.update:
                Image.open(out).convert("RGB").save(gold, optimize=True)
                print(f"{name}: golden written ({gold})")
                continue
            if not os.path.exists(gold):
                print(f"{name}: no committed golden at {gold}; rendered to {out}")
                continue
            ok, msg = diff_images(out, gold, os.path.join(os.path.dirname(out), f"diff-{name}.png"), tol)
            print(f"{name}: {msg}")
            if not ok:
                print("  diff image:", os.path.join(os.path.dirname(out), f"diff-{name}.png"))
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
