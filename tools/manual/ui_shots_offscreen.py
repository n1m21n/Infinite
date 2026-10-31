#!/usr/bin/env python3
"""Shoot the "Using the App" chapter screenshots from an off-screen Infinite, with the screen locked or not.

The app runs off-screen (IMAGERESYNTH_SCREENSHOT), saves its own backbuffer on request (INFINITE_SHOT_REQUEST),
opens panels over RPC and takes synthetic mouse and key input through the RPC "ui" method. No real mouse,
no window capture, no Accessibility permission.

  python3 tools/manual/ui_shots_offscreen.py [--app PATH] [--patch PATH] [shot ...]

Writes docs/ui_screenshots/<shot>.png at 3200x2000 (a 1600x1000 pt window, 2x). Coordinates below are points.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import node_screenshot as ns  # noqa: E402

OUT = os.path.join(ROOT, "docs", "ui_screenshots")
APP = os.path.expanduser("~/Infinite/infinte-base/build-dev/Infinite.app/Contents/MacOS/Infinite")
PATCH = os.path.join(ROOT, "docs", "showcase", "infinite-v1-showcase.inf")
TOKFILE = os.path.expanduser("~/Library/Application Support/Infinite/control_token")
PORT = 7794

# Panel rail (right edge), y per button.
RAIL_X = 1578
RAIL = {"library": 64, "viewport": 104, "modmatrix": 144, "perf": 184, "timeline": 224, "session": 264,
        "samplestudio": 304, "view3d": 344, "pianoroll": 384, "terminal": 424, "history": 938, "settings": 978}
PANELS = ["viewport", "modmatrix", "perf", "timeline", "session", "samplestudio", "pianoroll", "view3d", "terminal",
          "history", "settings", "templates", "help", "shortcuts", "showset"]


class Shooter:
    def __init__(self, rpc, req, tmp):
        self.rpc, self.req, self.tmp = rpc, req, tmp

    def ui(self, action, **kw):
        self.rpc.call("ui", action=action, **kw)
        time.sleep(0.5)

    def click(self, x, y, right=False, double=False):
        self.ui("click", x=x, y=y, button=1 if right else 0, double=double)

    def move(self, x, y):
        self.ui("move", x=x, y=y)

    def key(self, name, **mods):
        self.ui("key", key=name, **mods)

    def panel(self, name, open_=True, dock=None):
        kw = {"name": name, "open": open_}
        if dock:
            kw["dock"] = dock
        try:
            self.rpc.call("panel", **kw)
        except Exception as e:  # a panel this build does not know
            print(f"  panel {name}: {e}")
        time.sleep(0.8)

    def library_open(self):
        # The Library has no RPC panel: read its rail button, which turns coral while the panel is open.
        from PIL import Image
        path = self.grab("_probe")
        with Image.open(path) as im:
            r, g, b = im.convert("RGB").getpixel((RAIL_X * 2 - 18, RAIL["library"] * 2))
        return r > 130 and r > g + 50

    def set_library(self, want):
        if self.library_open() != want:
            self.click(RAIL_X, RAIL["library"])
            time.sleep(0.8)

    def reset(self):
        self.key("escape")
        if self.library_open():
            self.click(1240, 80)  # back to the Modules tab
            self.key("escape")
        self.key("escape")
        self.move(800, 600)
        for p in PANELS:
            self.panel(p, False)
        self.set_library(False)

    def grab(self, name):
        tmp = os.path.join(self.tmp, name + ".png")
        if os.path.exists(tmp):
            os.remove(tmp)
        with open(self.req, "w") as f:
            f.write(tmp)
        for _ in range(80):
            if os.path.exists(tmp) and not os.path.exists(self.req):
                break
            time.sleep(0.25)
        time.sleep(0.3)
        return tmp

    def shot(self, name):
        tmp = self.grab(name)
        if not os.path.exists(tmp):
            print(f"  {name}: no frame saved")
            return
        os.makedirs(OUT, exist_ok=True)
        shutil.copyfile(tmp, os.path.join(OUT, name + ".png"))
        print(f"  {name}")


# Each scene: reset, then its steps, then one shot named after the scene.
def scenes(s):
    def rail(name, then=None):
        def f():
            s.click(RAIL_X, RAIL[name])
            time.sleep(1.2)
            if then:
                then()
        return f

    def seq(*steps):
        def f():
            for st in steps:
                st()
        return f

    wait = lambda t: (lambda: time.sleep(t))  # noqa: E731
    click = lambda x, y, **k: (lambda: s.click(x, y, **k))  # noqa: E731
    move = lambda x, y: (lambda: s.move(x, y))  # noqa: E731
    key = lambda n, **m: (lambda: s.key(n, **m))  # noqa: E731
    panel = lambda n, d=None: (lambda: (s.panel(n, True, d), time.sleep(1.5)))  # noqa: E731

    return {
        "app_overview": wait(1.5),
                "menu_file": seq(click(80, 25), wait(0.8)),
        "menu_edit": seq(click(112, 25), wait(0.8)),
        "menu_menu": seq(click(166, 25), wait(0.8)),
        "menu_panels": seq(click(166, 25), wait(0.6), move(163, 98), wait(0.8)),
        "rc_canvas_palette": seq(click(900, 520, right=True), wait(0.8)),
        "rc_palette_submenu": seq(click(900, 520, right=True), wait(0.8), move(930, 600), wait(0.8)),
        "rc_node": seq(click(360, 330, right=True), wait(0.8)),
        "panel_library_big": rail("library"),
        "library_field": rail("library", seq(click(1302, 80), wait(1.0))),
        # No Samples or Media tab shots: they list the folders and files on the machine that shoots them.
        "library_plugins": rail("library", seq(click(1496, 80), wait(1.0))),
        "library_sort_dropdown": rail("library", seq(click(1280, 184), wait(0.8))),
        "library_filter_dropdown": rail("library", seq(click(1440, 184), wait(0.8))),
        "panel_viewport_cards": panel("viewport"),
        "panel_modmatrix_big": panel("modmatrix", "bottom"),
        "panel_perf": panel("perf"),
                "panel_timeline": panel("timeline", "bottom"),
        "panel_session": panel("session"),
        "panel_studio": panel("samplestudio", "bottom"),
        "panel_pianoroll": panel("pianoroll", "bottom"),
        "panel_view3d": seq(panel("view3d", "bottom"), wait(3)),
        "panel_history_big": panel("history"),
        "window_settings": panel("settings"),
        **{f"settings_{n}": seq(panel("settings"), click(480, 274 + 28.8 * i), wait(1.0))
           for i, n in enumerate(["appearance", "canvas", "audio", "general", "field", "globals", "extensions",
                                  "plugins", "terminal"])},
        "dialog_about": seq(click(166, 25), wait(0.6), click(173, 266), wait(1.2)),
        "window_shortcuts": panel("shortcuts"),
        "window_help": panel("help"),
        "window_templates": panel("templates"),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--app", default=APP)
    ap.add_argument("--patch", default=PATCH)
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--attach", help="token file of an already running off-screen app (with --req)")
    ap.add_argument("--req", help="that app's INFINITE_SHOT_REQUEST path")
    ap.add_argument("shots", nargs="*")
    a = ap.parse_args()

    tmp = tempfile.mkdtemp(prefix="ui_shots_")
    proc = None
    tokbak = None
    if a.attach:
        tok, req = open(a.attach).read().strip(), a.req
    else:
        if os.path.exists(TOKFILE):
            tokbak = os.path.join(tmp, "token.bak")
            shutil.copyfile(TOKFILE, tokbak)
        req = os.path.join(tmp, "req")
        env = dict(os.environ, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_CONTROL_PORT=str(a.port), INFINITE_NOSPLASH="1",
                   INFINITE_NO_UPDATE_CHECK="1", INFINITE_HIDE_TELEMETRY="1",
                   IMAGERESYNTH_SCREENSHOT=os.path.join(tmp, "unused.png"), INFINITE_SCREENSHOT_FRAME="999999999",
                   INFINITE_SHOT_REQUEST=req)
        log = open(os.path.join(tmp, "app.log"), "w")
        proc = subprocess.Popen([a.app, os.path.abspath(a.patch)], env=env, stdout=log, stderr=subprocess.STDOUT)
        time.sleep(15)
        tok = open(TOKFILE).read().strip()
    rpc = ns.RpcClient("127.0.0.1", a.port, tok)
    s = Shooter(rpc, req, tmp)
    # Same Studio take and playing scene as docs/showcase/shoot.sh (the Studio does not resolve a relative path).
    media = os.path.join(os.path.dirname(os.path.abspath(a.patch)), "media")
    slices = "\n".join(f"slice id={i + 1} s={i * 24406} col={(3 * i + 3) % 10 or 1} n={60 + i}" for i in range(8))
    studio = f"studio 1\nsrc path={media}/studio-take.wav\nsliceset 1 end=195254 next=9\n{slices}\n"
    cli_env = dict(os.environ, INFINITE_CONTROL_PORT=str(a.port), INFINITE_CONTROL_TOKEN=tok)
    for cmd in (["studio", "set", "text=" + studio], ["session", "scene", "1"]):
        subprocess.run([a.app, "--cli", *cmd], env=cli_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        table = scenes(s)
        for name in a.shots or list(table):
            s.reset()
            table[name]()
            s.shot(name)
        s.reset()
    finally:
        if proc:
            try:
                rpc.call("quit")
            except Exception:
                pass
            time.sleep(2)
            proc.kill()
        if tokbak:
            shutil.copyfile(tokbak, TOKFILE)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
