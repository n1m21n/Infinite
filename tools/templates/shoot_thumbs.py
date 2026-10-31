#!/usr/bin/env python3
"""Writes assets/templates/thumbs/<slug>.png: a screenshot of the template's own node graph.
    python3 tools/templates/shoot_thumbs.py [--bin path/to/Infinite] [slug ...]
One off-screen Infinite per template (own HOME, so the shared control token is never touched): open the patch,
`view fit`, save the app's own frame (INFINITE_SHOT_REQUEST), cut the canvas out of the window, scale it to 320 wide
and centre it on the canvas colour in a 320 x 320 tile. Run gen_templates.py first. One build at a time on the 8 GB
Mac: wait for `scripts/ibuild.py status` to say the lock is free."""
import json, os, socket, subprocess, sys, tempfile, time
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TPL = os.path.join(ROOT, "assets", "templates")
DEFAULT_BIN = os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite")
PORT = 7796

def rpc(method, params, token):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=20)
    s.sendall((json.dumps({"jsonrpc": "2.0", "id": 1, "method": method, "params": params, "token": token}) + "\n").encode())
    d = b""
    while not d.endswith(b"\n"):
        d += s.recv(4096)
    s.close()
    return json.loads(d)

def shoot(app, slug, params):
    tmp = tempfile.mkdtemp(prefix="thumb_")
    home = os.path.join(tmp, "home"); os.makedirs(home)
    req = os.path.join(tmp, "req")
    env = dict(os.environ, HOME=home, INFINITE_NO_SINGLE_INSTANCE="1", INFINITE_CONTROL_PORT=str(PORT), INFINITE_NOSPLASH="1",
               INFINITE_NO_UPDATE_CHECK="1", INFINITE_HIDE_TELEMETRY="1", IMAGERESYNTH_SCREENSHOT=os.path.join(tmp, "unused.png"),
               INFINITE_SCREENSHOT_FRAME="999999999", INFINITE_SHOT_REQUEST=req)
    p = subprocess.Popen([app, os.path.join(TPL, slug + ".inf")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        tok = None
        for _ in range(90):
            time.sleep(1)
            for r, _, fs in os.walk(home):
                if "control_token" in fs:
                    tok = open(os.path.join(r, "control_token")).read().strip()
            if tok:
                break
        time.sleep(6)
        c = lambda *a: subprocess.run([app, "--cli", *a], env=dict(env, INFINITE_CONTROL_TOKEN=tok), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        c("view", "params", "on" if params else "off")
        c("view", "fit")
        time.sleep(3)
        shot = os.path.join(tmp, "shot.png")
        open(req, "w").write(shot + "\n")
        for _ in range(60):
            if not os.path.exists(req):
                break
            time.sleep(0.5)
        assert os.path.exists(shot), slug + ": no frame saved"
        rpc("quit", {}, tok)
        return shot, tmp
    finally:
        time.sleep(1)
        p.kill()

def tile(shot, out):
    from PIL import Image
    im = Image.open(shot).convert("RGB")
    w, h = im.size
    canvas = im.crop((0, int(h * 0.05), int(w * 0.965), h))      # below the top bar, left of the side rail
    bg = canvas.getpixel((canvas.width - 4, canvas.height - 4))
    from PIL import ImageChops
    box = ImageChops.difference(canvas, Image.new("RGB", canvas.size, bg)).point(lambda v: 255 if v > 12 else 0).getbbox()
    if box:                                                        # crop to the graph, a little air around it
        m = int(max(box[2] - box[0], box[3] - box[1]) * 0.05)
        canvas = canvas.crop((max(0, box[0] - m), max(0, box[1] - m), min(canvas.width, box[2] + m), min(canvas.height, box[3] + m)))
    canvas = canvas.resize((320, max(1, round(320 * canvas.height / canvas.width)))) if canvas.width >= canvas.height else canvas.resize((max(1, round(320 * canvas.width / canvas.height)), 320))
    t = Image.new("RGB", (320, 320), bg)
    t.paste(canvas, ((320 - canvas.width) // 2, (320 - canvas.height) // 2))
    t.save(out)

def main():
    args = sys.argv[1:]
    app = DEFAULT_BIN
    if "--bin" in args:
        i = args.index("--bin"); app = args[i + 1]; del args[i:i + 2]
    slugs = args or [l.split("|")[0] for l in open(os.path.join(TPL, "index.txt")) if l.strip() and not l.startswith("#")]
    for slug in slugs:
        try:
            shot, tmp = shoot(app, slug, True)
        except Exception:      # a dev build asserts when it draws some node bodies (Ramp, Color Ramp, Audio Analyze): nodes without params
            try:
                shot, tmp = shoot(app, slug, False)
                print(slug, "(params off)")
            except Exception:
                print(slug, "KEPT the old thumbnail: the app died while drawing this graph")
                continue
        tile(shot, os.path.join(TPL, "thumbs", slug + ".png"))
        print(slug)
main()
