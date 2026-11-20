#!/usr/bin/env python3
"""look.py - look at a 3D patch quickly, headless, one Infinite process per call.

  look.py passes PATCH [--passes color,normal,albedo,id,light,depth,ao]
      One sheet of render passes. "light" = colour / albedo (what the lighting alone does).
  look.py crop PATCH X Y W H [--scale S] [--verify]
      Render only the region (X,Y,W,H) of the full frame, S times sharper, by narrowing the
      camera (fov + lens shift) and scaling the DoF aperture so the blur matches.
      --verify also renders the full frame and compares the same region.
  look.py sweep PATCH node.key V1 V2 ...
      One render per value, labelled with mean luma and saturation.
  look.py ab PATCH_A PATCH_B   |   look.py ab PATCH --set node.key=V [--set ...]
      Side by side plus a x4 difference image and the mean difference.

Common: --set node.key=V (applied to every variant), --size WxH, --time T, --out DIR,
--bin PATH. Pass INFINITE_LIVE_ENGINE etc. in the environment as usual.
Prints one JSON summary line; the sheet path is in it.
"""
import argparse, json, math, os, re, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageChops, ImageStat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN_DEFAULT = os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")
PASSES = {"color": 0, "depth": 1, "normal": 3, "position": 4, "emission": 7, "shadow": 8,
          "ao": 9, "env": 10, "albedo": 11, "mask": 12, "id": 13}
RENDER_TYPES = ("3D View", "Render 3D")


# ---------- patch text ----------
class Patch:
    def __init__(self, text):
        self.lines = text.split("\n")

    def text(self):
        return "\n".join(self.lines)

    def nodes(self):
        """[(start, end, type, id)] with end = index of the 'end' line."""
        out, i = [], 0
        while i < len(self.lines):
            m = re.match(r"node \S+ \S+ (.+)$", self.lines[i])
            if m:
                j, nid = i + 1, None
                while j < len(self.lines) and self.lines[j] != "end":
                    mm = re.match(r"\s+id (\S+)", self.lines[j])
                    if mm: nid = mm.group(1)
                    j += 1
                out.append((i, j, m.group(1).strip(), nid))
                i = j
            i += 1
        return out

    def find(self, ref):
        for n in self.nodes():
            if n[3] == ref or self.lines[n[0]].split()[1] == ref:
                return n
        sys.exit(f"look: no node '{ref}'")

    def first(self, types):
        for t in types:
            for n in self.nodes():
                if n[2] == t:
                    return n
        sys.exit(f"look: no {' / '.join(types)} node")

    def get(self, ref, key, default=None):
        s, e, _, _ = self.find(ref)
        for k in range(s + 1, e):
            p = self.lines[k].split()
            if len(p) >= 3 and p[1] == key:
                return p[2:]
        return default

    def set(self, ref, key, value, tag):
        s, e, _, _ = self.find(ref)
        line = f"  {tag} {key} {value}"
        for k in range(s + 1, e):
            p = self.lines[k].split()
            if len(p) >= 2 and p[0] in "fibcs" and p[1] == key:
                self.lines[k] = line
                return
        self.lines.insert(e, line)


_TAGS = {}
def tag_of(binp, ntype, key):
    if ntype not in _TAGS:
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            path = f.name
        subprocess.run([binp, "--describe", ntype, "--json", path], capture_output=True)
        try:
            d = json.load(open(path))
            _TAGS[ntype] = {p["key"]: p["tag"] for p in d["types"][0]["params"]}
        except Exception:
            _TAGS[ntype] = {}
        os.unlink(path)
    t = _TAGS[ntype].get(key)
    if t is None:
        sys.exit(f"look: {ntype} has no key '{key}'")
    return t


def apply_set(patch, binp, spec):
    ref_key, value = spec.split("=", 1)
    ref, key = ref_key.split(".", 1)
    ntype = patch.find(ref)[2]
    patch.set(ref, key, value, tag_of(binp, ntype, key))


# ---------- rendering ----------
def render_all(binp, variants, outdir, t, size):
    """variants: [(name, Patch)] -> {name: PIL image or None}, one process."""
    os.makedirs(outdir, exist_ok=True)
    jobs = []
    for name, p in variants:
        pf = os.path.join(outdir, f"{name}.inf")
        open(pf, "w").write(p.text())
        jobs.append((name, pf, os.path.join(outdir, name) + "/"))
    cmd = [binp, "--frame", jobs[0][1], str(t), jobs[0][2], "--lenient"]
    if size: cmd += ["--size", size]
    if len(jobs) > 1:
        bl = os.path.join(outdir, "batch.txt")
        open(bl, "w").write("".join(f"{pf}\t{od}\n" for _, pf, od in jobs[1:]))
        cmd += ["--batch", bl]
    r = subprocess.run(cmd, capture_output=True, text=True)
    status = r.stdout.strip().split("\n")[-1] if r.stdout.strip() else ""
    imgs = {}
    for name, _, od in jobs:
        pngs = sorted(f for f in os.listdir(od) if f.endswith(".png")) if os.path.isdir(od) else []
        imgs[name] = Image.open(os.path.join(od, pngs[0])).convert("RGB") if pngs else None
    if all(v is None for v in imgs.values()):
        sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:])
        sys.exit(f"look: render failed (exit {r.returncode})")
    return imgs, status


def stats(img):
    s = ImageStat.Stat(img.convert("L"))
    hsv = ImageStat.Stat(img.convert("HSV"))
    return {"luma": round(s.mean[0] / 255, 4), "sat": round(hsv.mean[1] / 255, 4)}


def sheet(cells, path, cols=None):
    """cells: [(label, image)] -> one labelled grid."""
    cells = [(l, im) for l, im in cells if im is not None]
    cols = cols or min(4, len(cells))
    w = max(im.width for _, im in cells); h = max(im.height for _, im in cells)
    rows = math.ceil(len(cells) / cols)
    out = Image.new("RGB", (cols * w, rows * (h + 18)), (24, 24, 24))
    d = ImageDraw.Draw(out)
    for i, (label, im) in enumerate(cells):
        x, y = (i % cols) * w, (i // cols) * (h + 18)
        out.paste(im, (x, y + 18))
        d.text((x + 4, y + 3), label, fill=(230, 230, 230))
    out.save(path)
    return path


def light_pass(color, albedo):
    """colour / albedo, display-referred approximation of the lighting alone."""
    import numpy as np
    c = np.asarray(color, dtype=np.float32) / 255.0
    a = np.asarray(albedo, dtype=np.float32) / 255.0
    l = c / np.maximum(a, 0.04)
    l = l / (1.0 + l)                      # keep >1 visible instead of clipping
    return Image.fromarray((np.clip(l, 0, 1) * 255).astype("uint8"))


# ---------- crop maths ----------
def crop_patch(patch, binp, cam, rnd, x0, y0, cw, ch, scale):
    W = float(patch.get(rnd, "width", ["1920"])[0]); H = float(patch.get(rnd, "height", ["1080"])[0])
    fov = float(patch.get(cam, "fov", ["45"])[0])
    shx = float(patch.get(cam, "shiftX", ["0"])[0]); shy = float(patch.get(cam, "shiftY", ["0"])[0])
    a_full, a_crop = W / H, cw / ch
    sx0 = 2 * shx * max(1, 1 / a_full); sy0 = 2 * shy * max(1, a_full)
    cx = (x0 + cw / 2) / W * 2 - 1; cy = 1 - (y0 + ch / 2) / H * 2
    sx = (cx + sx0) * W / cw; sy = (cy + sy0) * H / ch
    fov2 = 2 * math.degrees(math.atan(math.tan(math.radians(fov) / 2) * ch / H))
    p = Patch(patch.text())
    p.set(cam, "fov", f"{fov2:.6f}", "f")
    p.set(cam, "shiftX", f"{sx / (2 * max(1, 1 / a_crop)):.6f}", "f")
    p.set(cam, "shiftY", f"{sy / (2 * max(1, a_crop)):.6f}", "f")
    p.set(rnd, "width", str(int(round(cw * scale))), "f")
    p.set(rnd, "height", str(int(round(ch * scale))), "f")
    ap = patch.get(rnd, "dofAperture")
    if ap:
        p.set(rnd, "dofAperture", f"{float(ap[0]) * H / ch:.6f}", "f")
    return p


# ---------- commands ----------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["passes", "crop", "sweep", "ab"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--passes", default="color,normal,albedo,light,id")
    ap.add_argument("--scale", type=float, default=2.0)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--size")
    ap.add_argument("--time", default="0")
    ap.add_argument("--out")
    ap.add_argument("--bin", default=BIN_DEFAULT)
    a = ap.parse_args()
    if not a.args:
        ap.error("PATCH missing")
    base = Patch(open(a.args[0]).read())
    for s in a.set:
        apply_set(base, a.bin, s)
    rnd = base.first(RENDER_TYPES)[3] or base.lines[base.first(RENDER_TYPES)[0]].split()[1]
    out = a.out or tempfile.mkdtemp(prefix=f"look-{a.cmd}-")
    os.makedirs(out, exist_ok=True)
    res = {"cmd": a.cmd, "out": out}

    if a.cmd == "passes":
        names = a.passes.split(",")
        need = sorted({n for n in names if n != "light"} | ({"color", "albedo"} if "light" in names else set()))
        variants = []
        for n in need:
            p = Patch(base.text()); p.set(rnd, "renderPass", PASSES[n], "i"); variants.append((n, p))
        imgs, _ = render_all(a.bin, variants, out, a.time, a.size)
        if "light" in names and imgs.get("color") and imgs.get("albedo"):
            imgs["light"] = light_pass(imgs["color"], imgs["albedo"])
        res["stats"] = {n: stats(imgs[n]) for n in names if imgs.get(n)}
        res["sheet"] = sheet([(n, imgs.get(n)) for n in names], os.path.join(out, "passes.png"), cols=min(3, len(names)))

    elif a.cmd == "crop":
        x0, y0, cw, ch = map(float, a.args[1:5])
        cam = base.first(("Camera",)); cam = cam[3] or base.lines[cam[0]].split()[1]
        variants = [("crop", crop_patch(base, a.bin, cam, rnd, x0, y0, cw, ch, a.scale))]
        if a.verify:
            full = Patch(base.text())
            W = float(base.get(rnd, "width", ["1920"])[0]); H = float(base.get(rnd, "height", ["1080"])[0])
            full.set(rnd, "width", str(int(W * a.scale)), "f"); full.set(rnd, "height", str(int(H * a.scale)), "f")
            variants.append(("full", full))
        imgs, _ = render_all(a.bin, variants, out, a.time, None)
        cells = [("crop", imgs["crop"])]
        if a.verify and imgs.get("full"):
            s = a.scale
            ref = imgs["full"].crop((int(x0 * s), int(y0 * s), int((x0 + cw) * s), int((y0 + ch) * s))).resize(imgs["crop"].size)
            diff = ImageChops.difference(imgs["crop"], ref)
            res["mean_diff"] = round(sum(ImageStat.Stat(diff).mean) / 3 / 255, 4)
            cells += [("full, same region", ref), ("diff x4", diff.point(lambda v: min(255, v * 4)))]
        res["sheet"] = sheet(cells, os.path.join(out, "crop.png"))

    elif a.cmd == "sweep":
        spec, values = a.args[1], a.args[2:]
        ref, key = spec.split(".", 1)
        tag = tag_of(a.bin, base.find(ref)[2], key)
        variants = []
        for i, v in enumerate(values):
            p = Patch(base.text()); p.set(ref, key, v, tag); variants.append((f"v{i}", p))
        imgs, _ = render_all(a.bin, variants, out, a.time, a.size)
        cells, res["stats"] = [], {}
        for i, v in enumerate(values):
            im = imgs.get(f"v{i}")
            st = stats(im) if im else None
            res["stats"][v] = st
            cells.append((f"{key}={v}  L{st['luma']:.2f} S{st['sat']:.2f}" if st else f"{key}={v} FAILED", im))
        res["sheet"] = sheet(cells, os.path.join(out, "sweep.png"))

    elif a.cmd == "ab":
        if len(a.args) >= 2:
            bpatch = Patch(open(a.args[1]).read())
            for s in a.set:
                apply_set(bpatch, a.bin, s)
            apatch = base
        else:   # A = file as is, B = file + --set
            apatch = Patch(open(a.args[0]).read()); bpatch = base
        imgs, _ = render_all(a.bin, [("A", apatch), ("B", bpatch)], out, a.time, a.size)
        A, B = imgs["A"], imgs["B"]
        if A is None or B is None:
            sys.exit("look: one side failed to render")
        if A.size != B.size: B = B.resize(A.size)
        diff = ImageChops.difference(A, B)
        res["mean_diff"] = round(sum(ImageStat.Stat(diff).mean) / 3 / 255, 4)
        res["stats"] = {"A": stats(A), "B": stats(B)}
        res["sheet"] = sheet([("A", A), ("B", B), (f"diff x4  mean {res['mean_diff']}", diff.point(lambda v: min(255, v * 4)))],
                             os.path.join(out, "ab.png"), cols=3)
    print(json.dumps(res))


if __name__ == "__main__":
    main()
