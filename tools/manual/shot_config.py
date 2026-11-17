"""Shared capture settings for the manual screenshot tools (ui_shots_offscreen.py and tools/docs-shots/capture.py).

Every value has a default that matches the old hardcoded behaviour. Override with an environment variable or the
matching command-line flag, so a second copy of the app can run on another control port beside tests:

  INFINITE_SHOT_APP        Infinite.app to shoot (default: the build-dev or INFINITE_APP path each tool already used)
  INFINITE_SHOT_PORT       control (RPC) port the shot app listens on (default 7794)
  INFINITE_SHOT_TOKEN_DIR  the app's settings folder, where control_token and instances/ are written
                           (default ~/Library/Application Support/Infinite)
  INFINITE_SHOT_OUT        where UI shots and their manifest go (default docs/ui_screenshots)

Rules that every capture is checked against (see check_pixels):
  UI scene shots (window, panel, dialog, menu, settings)   at least 3200 x 2000 px (the 2x backbuffer of a 1600 x 1000 pt window)
  zoomed shots of one control or node                       at least 250 px wide in the source image
"""
import json
import os
import shutil
import tempfile
import time

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

DEFAULT_PORT = 7794
DEFAULT_TOKEN_DIR = os.path.expanduser("~/Library/Application Support/Infinite")
DEFAULT_OUT = os.path.join(ROOT, "docs", "ui_screenshots")

UI_MIN = (3200, 2000)
ZOOM_MIN_WIDTH = 250

# Output node export path (src/app/Startup.cpp, the exportPath default under the Desktop folder). The app writes its
# default into the Output node's export field, so the capture config asks for it to be blanked. The app has no
# environment switch for it yet: until a release build blanks it, Output node params shots are held back (see
# tools/manual/shot_plan.py, status "blocked").
CAPTURE_CONFIG = {
    "output_export_path": "",
    "blank_output_export_path": True,
    "output_export_path_source": "src/app/Startup.cpp (exportPath default under the Desktop folder)",
}


def app_path(arg=None, default=None):
    """--app, else INFINITE_SHOT_APP, else INFINITE_APP, else the tool's own default (each tool keeps its old one)."""
    return arg or os.environ.get("INFINITE_SHOT_APP") or os.environ.get("INFINITE_APP") or default


def port(arg=None):
    if arg is not None:
        return int(arg)
    return int(os.environ.get("INFINITE_SHOT_PORT") or DEFAULT_PORT)


def token_dir(arg=None):
    return os.path.expanduser(arg or os.environ.get("INFINITE_SHOT_TOKEN_DIR") or DEFAULT_TOKEN_DIR)


def out_dir(arg=None):
    return os.path.abspath(arg or os.environ.get("INFINITE_SHOT_OUT") or DEFAULT_OUT)


def read_token(pid, tdir, port_no):
    """Token of the app this tool launched. Each running app writes instances/<pid>.json with its port and token,
    so a second app on another port is reached without reading the shared control_token file (which the newest
    app overwrites). Falls back to control_token for an app started outside this tool."""
    inst = os.path.join(tdir, "instances", f"{pid}.json")
    for _ in range(40):
        if os.path.exists(inst):
            try:
                d = json.load(open(inst))
                if int(d.get("port", -1)) == int(port_no) and d.get("token"):
                    return d["token"]
            except (ValueError, OSError):
                pass
        time.sleep(0.25)
    p = os.path.join(tdir, "control_token")
    return open(p).read().strip() if os.path.exists(p) else ""


def check_pixels(path, kind):
    """Return (ok, width, height, message) for a source image. kind: 'ui' or 'zoom'."""
    with Image.open(path) as im:
        w, h = im.size
    if kind == "ui":
        ok = w >= UI_MIN[0] and h >= UI_MIN[1]
        msg = f"ui shot {w}x{h}, need >= {UI_MIN[0]}x{UI_MIN[1]}"
    elif kind == "zoom":
        ok = w >= ZOOM_MIN_WIDTH
        msg = f"zoom shot {w}x{h} px wide, need >= {ZOOM_MIN_WIDTH}"
    else:
        raise ValueError(kind)
    return ok, w, h, ("ok: " if ok else "FAIL: ") + msg


def assert_pixels(path, kind):
    """Raise AssertionError (and remove the file, so a resume will shoot it again) when the rule is broken."""
    ok, w, h, msg = check_pixels(path, kind)
    if not ok:
        try:
            os.remove(path)
        except OSError:
            pass
        raise AssertionError(f"{os.path.basename(path)}: {msg}")
    return w, h


class Manifest:
    """Resumable record of finished shots. Written after every shot (atomic replace), so a crash or reboot loses at
    most the shot in progress. An entry counts as done only when its file still exists and passes the pixel rule."""

    def __init__(self, out):
        self.path = os.path.join(out, "manifest.json")
        self.data = {"shots": {}}
        if os.path.exists(self.path):
            try:
                self.data = json.load(open(self.path))
            except ValueError:
                pass

    def done(self, name, kind):
        e = self.data["shots"].get(name)
        if not e:
            return False
        f = os.path.join(os.path.dirname(self.path), e["file"])
        return os.path.exists(f) and check_pixels(f, kind)[0]

    def record(self, name, file, kind, app=""):
        w, h = Image.open(file).size
        self.data["shots"][name] = {"file": os.path.basename(file), "kind": kind, "width": w, "height": h, "app": app}
        self.save()

    def save(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(self.path), suffix=".json")
        with os.fdopen(fd, "w") as f:
            json.dump(self.data, f, indent=1, sort_keys=True)
        shutil.move(tmp, self.path)


def pixel_report(paths):
    """Print the pixel size of every file (used after each capture batch)."""
    rows = []
    for p in paths:
        with Image.open(p) as im:
            rows.append((os.path.relpath(p, ROOT), im.size[0], im.size[1]))
    for r in rows:
        print(f"{r[1]:>6} x {r[2]:<6} {r[0]}")
    return rows
