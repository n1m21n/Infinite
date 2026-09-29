#!/usr/bin/env python3
"""Node census: every registered node type, auto-wired and auto-checked, headless.

Why: edge cases per node cannot be listed by hand for ~300 types. This spawns
each type alone, feeds every input slot a canonical source of its kind
(image: Formula, geometry: Cube, audio: Oscillator, notes: Random Note
Generator), routes the first output somewhere observable (image -> Output,
geometry -> Render 3D -> Output, audio/notes -> Output's audio pin, modulator
-> a Shape's rotation), and records:

  validate   the strict validator accepts the auto-wired patch
  render     --frame and --render succeed (hardware sources must refuse, rc 4)
  black      the frame is black at both probe times
  nan        NaN pixels or samples
  size       output size changes between frames (breaks video encoders)
  determin.  two --frame runs are identical
  clock      --render (one frame per tick) vs --frame (many frames per tick)
             at the same time; a wall-clock node differs (H.264 noise ~0.5,
             so only large gaps are real until the PNG-sequence path exists)
  audio      silent / clipping (peak > 1.0) with default params

Usage:
  scripts/node_census.py [--bin PATH] [--out DIR] [--jobs N] [--baseline FILE] [Type ...]

With --baseline, prints only rows whose issues differ from the baseline file
(a previous census.json), so a new node or a regression stands out. Exit code
1 when anything new appears.
"""
import argparse, concurrent.futures as cf, json, os, subprocess, sys
import numpy as np
from PIL import Image

FEED = {
    "image": "Source Formula",
    "geometry": "3D Cube",
    "audio": "Synths Oscillator",
    "note": "Notes Random Note Generator",
    "camera": "3D Camera",
    "light": "3D Light",
    "environment": "3D HDRI",
    "palette": "Modulators Palette",
}
TAG = {"image": "cable", "geometry": "geo", "audio": "aud", "note": "note",
       "camera": "geo", "light": "geo", "environment": "cable", "palette": "geo"}
# Where a 3D-side output plugs in on a Render 3D (Set Vertex Color for a palette).
RENDER_SLOT = {"camera": 4, "light": 5, "environment": 8}
SHAPE_POS_X = 5  # Shape modulatable index for "pos x": visible for any shape, never its size


def build(t):
    lines = ["infinite-patch 1", f"node 1 {t['category']} {t['type']}\nend"]
    links, nid = [], [100]

    def add(spec, extra=""):
        nid[0] += 1
        lines.append(f"node {nid[0]} {spec}\n{extra}end")
        return nid[0]

    for s in t["inputs"]:
        k = s["kind"]
        if k in FEED:
            src = add(FEED[k])
            links.append(f"{TAG[k]} 1 {s['slot']} {src}" + (" 0" if k == "note" else ""))
    kind = t["outputs"][0]["kind"] if t["outputs"] else "none"
    out = add("Utility Output")
    if kind == "image":
        links.append(f"cable {out} 0 1")
    elif kind == "geometry":
        r = add("3D Render 3D", "  f width 512\n  f height 512\n")
        links += [f"geo {r} 0 1", f"cable {out} 0 {r}"]
    elif kind == "audio":
        sh = add("Source Shape")
        links += [f"cable {out} 0 {sh}", f"aud {out} 1 1"]
    elif kind == "note":
        osc, sh = add("Synths Oscillator"), add("Source Shape")
        links += [f"note {osc} 0 1 0", f"cable {out} 0 {sh}", f"aud {out} 1 {osc}"]
    elif kind in RENDER_SLOT:
        cube = add("3D Cube")
        r = add("3D Render 3D", "  f width 512\n  f height 512\n")
        links += [f"geo {r} 0 {cube}", f"{TAG[kind]} {r} {RENDER_SLOT[kind]} 1", f"cable {out} 0 {r}"]
    elif kind == "palette":
        cube = add("3D Cube")
        vc = add("3D Set Vertex Color")
        r = add("3D Render 3D", "  f width 512\n  f height 512\n")
        links += [f"geo {vc} 0 {cube}", f"geo {vc} 2 1", f"geo {r} 0 {vc}", f"cable {out} 0 {r}"]
    else:
        sh = add("Source Shape")
        links += [f"cable {out} 0 {sh}", f"mod {sh} {SHAPE_POS_X} 1 0 0 1 0"]
    return "\n".join(lines + links) + "\n", kind


class Census:
    def __init__(self, binary, out):
        self.bin, self.out = binary, out

    def run(self, args, timeout=60):
        try:
            p = subprocess.run([self.bin] + args, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return -9, {"errors": [{"code": "TIMEOUT"}]}
        last = (p.stdout.strip().splitlines() or ["{}"])[-1]
        try:
            return p.returncode, json.loads(last)
        except ValueError:
            return p.returncode, {"errors": [{"code": "NOJSON", "message": (p.stderr or p.stdout)[-200:]}]}

    @staticmethod
    def codes(j):
        return ",".join(e.get("code", "?") for e in j.get("errors", []))

    def probe(self, t):
        try:
            return self._probe(t)
        except Exception as e:  # a census bug must not hide the other 296 rows
            return {"type": t["type"], "cat": t["category"], "issues": ["census-crash:" + str(e)[:80]]}

    def _probe(self, t):
        safe = "".join(c if c.isalnum() else "_" for c in t["type"])
        d = os.path.join(self.out, safe)
        os.makedirs(d, exist_ok=True)
        patch, kind = build(t)
        pf = os.path.join(d, "p.inf")
        with open(pf, "w") as f:
            f.write(patch)
        r = {"type": t["type"], "cat": t["category"], "out": kind, "issues": []}

        rc, j = self.run(["--validate", pf])
        if not j.get("ok"):
            r["issues"].append("validate:" + self.codes(j))
            return r
        rc, j = self.run(["--frame", pf, "0.5,1.5", d + "/a/"])
        if rc == 4:
            r["issues"].append("hardware-refused")  # expected for live inputs
            return r
        if rc != 0:
            r["issues"].append(f"frame:{self.codes(j)}")
            return r
        self.run(["--frame", pf, "0.5,1.5", d + "/b/"])
        img = lambda p: np.asarray(Image.open(p).convert("RGBA"), dtype=np.float32)
        a0, a1, b1 = img(d + "/a/frame_00015.png"), img(d + "/a/frame_00045.png"), img(d + "/b/frame_00045.png")
        if a0.shape != a1.shape:
            r["issues"].append(f"size-changes {a0.shape[1]}x{a0.shape[0]}->{a1.shape[1]}x{a1.shape[0]}")
            a0 = a1
        if a1.shape != b1.shape:
            r["issues"].append("size-nondeterministic")
            b1 = a1
        if np.isnan(a1).any():
            r["issues"].append("nan-pixels")
        if a0[..., :3].max() < 2 and a1[..., :3].max() < 2:
            r["issues"].append("black")
        r["animates"] = bool(np.abs(a0 - a1).mean() > 0.05)
        if np.abs(a1 - b1).mean() > 0.01:
            r["issues"].append("nondeterministic")

        mov = os.path.join(d, "r.mov")
        rc, j = self.run(["--render", pf, mov, "--duration", "1.6"], timeout=120)
        if rc != 0:
            r["issues"].append("render:" + self.codes(j))
            return r
        probe = subprocess.run(["ffprobe", "-v", "error", "-show_format", mov], capture_output=True)
        if probe.returncode != 0:
            r["issues"].append("render-ok-but-file-unreadable")
            return r
        png = os.path.join(d, "r45.png")
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", mov, "-vf", r"select=eq(n\,45)", "-vframes", "1", png])
        if os.path.exists(png):
            rv = img(png)
            if rv.shape == a1.shape:
                r["render_vs_frame"] = round(float(np.abs(rv[..., :3] - a1[..., :3]).mean()), 2)
                if r["render_vs_frame"] > 3.0:
                    r["issues"].append("clock-suspect")
        if kind in ("audio", "note"):
            raw = os.path.join(d, "a.raw")
            subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", mov, "-ac", "1", "-f", "f32le", raw])
            x = np.fromfile(raw, dtype=np.float32) if os.path.exists(raw) else np.zeros(0, np.float32)
            if np.isnan(x).any():
                r["issues"].append("audio-nan")
            peak = float(np.abs(x).max()) if len(x) else 0.0
            r["peak"] = round(peak, 3)
            if peak < 1e-4:
                r["issues"].append("silent")
            elif peak > 1.0:
                r["issues"].append("clips")
        return r


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(root, "build/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--out", default=os.path.join(root, "build/census"))
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--baseline")
    ap.add_argument("types", nargs="*")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    schema = os.path.join(a.out, "describe.json")
    subprocess.run([a.bin, "--describe", "--json", schema], capture_output=True)
    types = json.load(open(schema))["types"]
    if a.types:
        types = [t for t in types if t["type"] in a.types]

    c = Census(a.bin, a.out)
    rows = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(c.probe, types):
            rows.append(r)
            if not a.baseline:
                print(json.dumps(r), flush=True)
    with open(os.path.join(a.out, "census.json"), "w") as f:
        json.dump(rows, f, indent=1)

    if a.baseline:
        base = {r["type"]: sorted(r["issues"]) for r in json.load(open(a.baseline))}
        new = [r for r in rows if base.get(r["type"]) != sorted(r["issues"])]
        for r in new:
            print(f"{r['type']:28} was {base.get(r['type'], 'NEW TYPE')} now {r['issues']}")
        print(f"{len(rows)} types, {len(new)} changed vs baseline")
        sys.exit(1 if new else 0)
    bad = [r for r in rows if r["issues"]]
    print(f"{len(rows)} types, {len(rows) - len(bad)} clean", file=sys.stderr)


if __name__ == "__main__":
    main()
