#!/usr/bin/env python3
"""Offline shots of the 2D View for hand-checking every element (no window capture, works with the screen locked).

Launches an off-screen Infinite (INFINITE_SHOT_REQUEST), opens the 2D View docked at the bottom, adds the patch's
first nodes as layers, then runs "steps" given on the command line and saves one PNG per `shot`:

  python3 tools/ui/shoot_view2d.py OUTDIR  shot:base  click:42,300  shot:move  key:v  shot:x ...

Steps: shot:NAME | click:X,Y | rclick:X,Y | move:X,Y | key:NAME[+shift|+cmd] | wait:SEC | panel:NAME | edit:ACTION
       tool:NAME (hover the canvas, press the tool's key) | wheel:X,Y,DY | dock:bottom|top | dclick:X,Y | type:TEXT
Coordinates are points (window 1600x1000, shots are 2x). Also writes NAME_2d.png, the panel's lower half cropped.
"""
import os, shutil, subprocess, sys, tempfile, time
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import node_screenshot as ns  # noqa: E402

APP = os.path.join(ROOT, "build-dev", "Infinite.app", "Contents", "MacOS", "Infinite")
PATCH = os.path.join(ROOT, "docs", "showcase", "infinite-v1-showcase.inf")
TOKFILE = os.path.expanduser("~/Library/Application Support/Infinite/control_token")
PORT = 7795
TOOLKEYS = {"move": "v", "marquee": "m", "lasso": "l", "wand": "w", "crop": "c", "eyedropper": "i", "brush": "b",
            "eraser": "e", "fill": "g", "pen": "p", "text": "t", "direct": "a", "shape": "u", "hand": "h", "zoom": "z"}


def main():
    out = sys.argv[1]
    steps = sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="v2d_")
    tokbak = None
    if os.path.exists(TOKFILE):
        tokbak = os.path.join(tmp, "token.bak")
        shutil.copyfile(TOKFILE, tokbak)
    req = os.path.join(tmp, "req")
    env = dict(os.environ, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_CONTROL_PORT=str(PORT), INFINITE_NOSPLASH="1",
               INFINITE_NO_UPDATE_CHECK="1", INFINITE_HIDE_TELEMETRY="1",
               IMAGERESYNTH_SCREENSHOT=os.path.join(tmp, "unused.png"), INFINITE_SCREENSHOT_FRAME="999999999",
               INFINITE_SHOT_REQUEST=req, HOME=os.environ["HOME"])
    proc = subprocess.Popen([APP, PATCH], env=env, stdout=open(os.path.join(tmp, "log"), "w"), stderr=subprocess.STDOUT)
    time.sleep(15)
    rpc = ns.RpcClient("127.0.0.1", PORT, open(TOKFILE).read().strip())

    def ui(action, **kw):
        rpc.call("ui", action=action, **kw)
        time.sleep(0.5)

    def grab(name):
        path = os.path.join(out, name + ".png")
        if os.path.exists(path):
            os.remove(path)
        open(req, "w").write(path)
        for _ in range(80):
            if os.path.exists(path) and not os.path.exists(req):
                break
            time.sleep(0.25)
        time.sleep(0.3)
        try:
            from PIL import Image
            with Image.open(path) as im:
                w, h = im.size
                im.crop((0, h // 2, w, h)).save(os.path.join(out, name + "_2d.png"))
        except Exception as e:
            print("crop failed", e)
        print("shot", name)

    try:
        rpc.call("panel", name="view2d", open=True, dock="bottom")
        time.sleep(1.5)
        for st in steps:
            kind, _, arg = st.partition(":")
            if kind == "shot":
                grab(arg)
            elif kind == "type":
                ui("text", text=arg)
            elif kind == "dclick":
                x, y = (float(v) for v in arg.split(","))
                ui("click", x=x, y=y, button=0, double=True)
            elif kind in ("click", "rclick", "move"):
                x, y = (float(v) for v in arg.split(","))
                if kind == "move":
                    ui("move", x=x, y=y)
                else:
                    ui("click", x=x, y=y, button=1 if kind == "rclick" else 0, double=False)
            elif kind == "tool":
                ui("move", x=800, y=760)
                ui("key", key=TOOLKEYS[arg])
            elif kind == "wheel":
                x, y, dy = (float(v) for v in arg.split(","))
                ui("wheel", x=x, y=y, dx=0.0, dy=dy)
            elif kind == "key":
                parts = arg.split("+")
                ui("key", key=parts[0], **{m: True for m in parts[1:]})
            elif kind == "wait":
                time.sleep(float(arg))
            elif kind == "dock":
                rpc.call("panel", name="view2d", open=True, dock=arg)
                time.sleep(1.5)
            elif kind == "panel":
                rpc.call("panel", name=arg, open=True)
                time.sleep(1.0)
            elif kind == "edit":
                rpc.call("edit", action=arg.split("|")[0], nodes=[int(v) for v in arg.split("|")[1:]])
                time.sleep(1.0)
    finally:
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
