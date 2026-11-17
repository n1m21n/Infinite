"""Capture node, panel, dialog and dropdown screenshots from a built Infinite.app using its own env hooks
(no UI scripting), frame them to the brand Paper standard and record them in docs/shots/manifest.json.

Library use: run.py. Direct: python3 tools/docs-shots/capture.py --limit 5
"""
import argparse
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "brand"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "manual"))
import common as C  # noqa: E402
import frame_shot  # noqa: E402
import shot_config as cfg  # noqa: E402

# Nodes whose default shows a personal path (capture config, shot_config.CAPTURE_CONFIG). Their panel shots (o, d)
# are held back until the app blanks the path; --allow-personal-paths overrides for a local, non-release run.
HELD_PERSONAL_PATH = {"Output": "export path (Desktop default, src/app/Startup.cpp)"}

# id -> (title, OPENPANELS tokens, extra env). Whole-window shots of panels, windows and dialogs.
SCENES = {
    "panel/library":   ("Node library", "library", {}),
    "panel/modmatrix": ("Modulation matrix", "modmatrix", {}),
    "panel/perf":      ("Performance panel", "perfdemo", {}),
    "panel/viewport":  ("Viewport panel", "viewport", {}),
    "panel/history":   ("History panel", "historydemo", {}),
    "panel/arrange":   ("Arrange panel", "arrangedemo", {}),
    "window/settings": ("Settings window", "settingswin,settingsgeneral", {}),
    "window/shortcuts": ("Shortcuts window", "shortcutswin", {}),
    "window/help":     ("Help window", "helpwin", {}),
    "dialog/unsaved":  ("Unsaved changes dialog", "", {"INFINITE_OPENDIALOG": "unsaved"}),
    "dialog/about":    ("About dialog", "", {"INFINITE_OPENDIALOG": "about"}),
    "dialog/recover":  ("Autosave recovery dialog", "", {"INFINITE_OPENDIALOG": "recover"}),
}


PORT = None          # --port / INFINITE_SHOT_PORT: control port for this run (unset = the app's default)
FORCE = False        # --force: shoot again even when the manifest says the shot is done
ALLOW_PERSONAL = False


def run_app(app, env_extra, out, timeout=120):
    env = dict(os.environ, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_NO_UPDATE_CHECK="1", INFINITE_NOSPLASH="1",
               INFINITE_HIDE_TELEMETRY="1", IMAGERESYNTH_SCREENSHOT=out, INFINITE_SCREENSHOT_FRAME="40")
    if PORT:
        env["INFINITE_CONTROL_PORT"] = str(PORT)
    env.update(env_extra)
    if os.path.exists(out):
        os.remove(out)
    try:
        subprocess.run([C.app_binary(app)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        pass
    return os.path.exists(out)


def list_nodes(app):
    out = subprocess.run([C.app_binary(app)], env=dict(os.environ, INFINITE_NODELIST="1", INFINITE_EXITAFTER="5",
                         INFINITE_NO_SINGLE_INSTANCE="1"), capture_output=True, text=True, timeout=60).stdout
    nodes = []
    for ln in out.splitlines():
        if ln.startswith("NODELIST "):
            cat, name = ln.split(" ", 1)[1].strip().split("|", 1)
            nodes.append((cat, name))
    return nodes


def crop_node(p):
    """Crop the full-window gallery capture to the node (same rule as tools/design/gallery/shoot.py)."""
    im = Image.open(p).convert("RGB")
    a = np.asarray(im).astype(int)
    reg = a[110:1990, 10:3080]
    bg = np.median(reg.reshape(-1, 3), axis=0)
    m = np.abs(reg - bg).sum(axis=2) > 60
    ys = np.where(m.sum(axis=1) > 8)[0]
    xs = np.where(m.sum(axis=0) > 8)[0]
    if len(ys) == 0:
        return False
    im.crop((xs[0] + 4, ys[0] + 104, xs[-1] + 16, ys[-1] + 116)).save(p)
    return True


def finish(raw, sid, meta, manifest, mode="paper", ratio="none"):
    """Frame `raw` onto the brand ground, write the PDF-sized JPEG and record the manifest entry."""
    base = os.path.join(C.SHOTS, "framed", C.slug(sid))
    os.makedirs(os.path.dirname(base), exist_ok=True)
    img, w = frame_shot.frame(raw, mode, ratio, 20)
    img.save(base + ".png", optimize=True)
    pdf = base + ".pdf.jpg"
    small = img.copy()
    small.thumbnail((1100, 1400), Image.LANCZOS)
    small.save(pdf, quality=86)
    rel = lambda p: os.path.relpath(p, C.ROOT)
    manifest["shots"][sid] = dict(meta, raw=rel(raw), framed=rel(base + ".png"), pdf=rel(pdf), width=img.width, height=img.height)


def done_already(manifest, sid):
    """Resume: a shot counts as done when the manifest has it and its framed file is still on disk."""
    s = manifest["shots"].get(sid)
    return bool(s and s.get("framed") and os.path.exists(os.path.join(C.ROOT, s["framed"])) and not FORCE)


def shoot_node(app, cat, name, state, theme, manifest, dd=None):
    sid = f"node/{cat}/{name}/{state}"
    if state in ("o", "d") and name in HELD_PERSONAL_PATH and not ALLOW_PERSONAL:
        print(f"held {sid}: shows a personal path ({HELD_PERSONAL_PATH[name]}); blank it in a release build first")
        return None
    if done_already(manifest, sid):
        return sid
    raw = os.path.join(C.SHOTS, "raw", C.slug(f"{cat}__{name}__{state}") + ".png")
    os.makedirs(os.path.dirname(raw), exist_ok=True)
    env = {"INFINITE_NODEGALLERY": f"{cat}|{name}", "INFINITE_GALLERYGRID": "1,100,100", "INFINITE_OPENPANELS": theme}
    if state in ("o", "d"):
        env["INFINITE_GALLERYPARAMS"] = "1"
    if state == "d":
        env["INFINITE_GALLERYOPENDD"] = str(dd or 0)
    if not run_app(app, env, raw, 120) or not crop_node(raw):
        return None
    try:
        ok, w, h, msg = cfg.check_pixels(raw, "zoom")   # source image, not the framed copy
        if not ok:
            print("FAIL", sid, msg)
            return None
    except Exception as e:
        print("FAIL", sid, e)
        return None
    finish(raw, sid, dict(kind="node", category=cat, name=name, state=state, theme=theme, source_px=[w, h]), manifest)
    return sid


def shoot_scene(app, sid, theme, manifest):
    title, panels, extra = SCENES[sid]
    if done_already(manifest, sid):
        return sid
    raw = os.path.join(C.SHOTS, "raw", C.slug(sid) + ".png")
    os.makedirs(os.path.dirname(raw), exist_ok=True)
    env = dict(extra, INFINITE_OPENPANELS=",".join(x for x in (theme, panels) if x))
    if not run_app(app, env, raw, 120):
        return None
    try:
        cfg.assert_pixels(raw, "ui")
    except AssertionError as e:
        print("FAIL", e)
        return None
    finish(raw, sid, dict(kind=sid.split("/")[0], name=title, theme=theme, state="-", category="-"), manifest, ratio="16:9")
    return sid


def shoot_uigallery(app, theme, manifest):
    sid = f"ui/gallery-{theme}"
    raw = os.path.join(C.SHOTS, "raw", C.slug(sid) + ".png")
    if not run_app(app, {"INFINITE_UIGALLERY": theme}, raw, 120):
        return None
    finish(raw, sid, dict(kind="ui", name=f"UI element sheet ({theme})", theme=theme, state="-", category="-"), manifest, ratio="16:9")
    return sid


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", default=C.DEFAULT_APP)
    ap.add_argument("--theme", default="dark", choices=["dark", "light"])
    ap.add_argument("--limit", type=int, default=0, help="only the first N nodes (smoke runs)")
    ap.add_argument("--nodes", help="comma list of 'Category|Name' to shoot instead of the full list")
    ap.add_argument("--category", action="append", help="only these categories")
    ap.add_argument("--dropdowns", action="store_true", help="also shoot each node's first dropdown open")
    ap.add_argument("--scenes", default="", help="'all' or comma list of scene ids (panel/*, window/*, dialog/*, ui)")
    ap.add_argument("--no-nodes", action="store_true")
    ap.add_argument("--force", action="store_true", help="ignore the busy-build guard (not recommended)")
    ap.add_argument("--shoot-again", action="store_true", help="re-shoot shots the manifest already has")
    ap.add_argument("--port", type=int, default=None, help="control port for the shot app (default $INFINITE_SHOT_PORT, else the app's)")
    ap.add_argument("--allow-personal-paths", action="store_true", help="shoot held nodes (Output) even with the Desktop path visible")
    a = ap.parse_args(argv)
    global PORT, FORCE, ALLOW_PERSONAL
    PORT = a.port or (int(os.environ["INFINITE_SHOT_PORT"]) if os.environ.get("INFINITE_SHOT_PORT") else None)
    FORCE, ALLOW_PERSONAL = a.shoot_again, a.allow_personal_paths

    busy = C.build_running()
    if busy and not a.force:
        raise SystemExit(f"a build is running ({', '.join(busy)}); only one build at a time on this Mac. Retry when it ends.")
    m = C.load_manifest()
    m["app"] = a.app
    m["version"] = C.app_version(a.app)
    done = fail = 0
    if not a.no_nodes:
        nodes = ([tuple(x.split("|", 1)) for x in a.nodes.split(",")] if a.nodes else list_nodes(a.app))
        if a.category:
            nodes = [n for n in nodes if n[0] in a.category]
        if a.limit:
            nodes = nodes[:a.limit]
        print(f"{len(nodes)} nodes, app {a.app}")
        for cat, name in nodes:
            for st in ("c", "o") + (("d",) if a.dropdowns else ()):
                sid = shoot_node(a.app, cat, name, st, a.theme, m)
                if st == "d" and sid:
                    o, d = m["shots"][f"node/{cat}/{name}/o"], m["shots"][sid]
                    if open(os.path.join(C.ROOT, o["raw"]), "rb").read() == open(os.path.join(C.ROOT, d["raw"]), "rb").read():
                        del m["shots"][sid]   # no dropdown on this node
                        sid = None
                done += bool(sid)
                fail += (not sid and st != "d")
                print(("ok  " if sid else "FAIL"), cat, name, st, flush=True)
                C.save_manifest(m)   # after every shot: a crash loses at most the shot in progress
    want = [x for s in a.scenes.split(",") if s for x in (list(SCENES) if s == "all" else [s])]
    for s in want:
        if s in ("ui", "ui/gallery"):
            for th in ("dark", "light"):
                done += bool(shoot_uigallery(a.app, th, m))
        elif s in SCENES:
            ok = shoot_scene(a.app, s, a.theme, m)
            done += bool(ok); fail += (not ok)
            C.save_manifest(m)
            print(("ok  " if ok else "FAIL"), s, flush=True)
        else:
            print("unknown scene", s)
    C.save_manifest(m)
    print(f"captured {done}, failed {fail}; manifest {os.path.relpath(C.MANIFEST, C.ROOT)}")
    return 1 if fail and not done else 0


if __name__ == "__main__":
    sys.exit(main())
