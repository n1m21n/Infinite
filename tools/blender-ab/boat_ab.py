# Boat-on-ocean A/B against Cycles, end to end:
#   INFINITE_BIN=<app binary> python3 tools/blender-ab/boat_ab.py sunset|moon --out <dir> [--engine 7] [--fresh]
# 1. The B4 boat bench (static, Cycles' lights only) saves the scene as a patch and dumps it for Blender.
# 2. Cycles renders the dump once (cached in <out>/<scene>/cycles.exr; --fresh redoes it).
# 3. Infinite renders the saved patch with --frame on the chosen live engine mode.
# 4. Score: the Cycles EXR through the engine's own AgX and exposure, FLIP over the frame and over the glitter
#    band (the sea between the horizon and the boat, where the sun's path lies), plus brightness ratios.
import argparse, json, os, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"

INSET = np.array([[0.842479062253094, 0.0423282422610123, 0.0423756549057051],
                  [0.0784335999999992, 0.878468636469772, 0.0784336],
                  [0.0792237451477643, 0.0791661274605434, 0.879142973793104]])
OUTSET = np.array([[1.19687900512017, -0.0528968517574562, -0.0529716355144438],
                   [-0.0980208811401368, 1.15190312990417, -0.0980434501171241],
                   [-0.0990297440797205, -0.0989611768448433, 1.15107367264116]])


def agx(c):  # MetalDevice.mm agxTone (Metal matrices are column-major: row vector times these rows)
    x = np.clip((np.log2(np.maximum(c @ INSET, 1e-10)) + 12.47393) / 16.5, 0, 1)
    p = -246.535442
    for k in (984.731367, -1568.41656, 1268.06241, -553.234808, 130.459845, -14.9498445, 0.887512984, -0.00843336545):
        p = p * x + k
    x = np.maximum(np.clip(p, 0, 1) @ OUTSET, 0)
    return x ** 2.4


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c < 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def read_exr(path):  # Blender reads its own EXR (no OpenEXR module needed here); cached as .npy
    npy = os.path.splitext(path)[0] + ".npy"
    if not os.path.exists(npy) or os.path.getmtime(npy) < os.path.getmtime(path):
        expr = ("import bpy, numpy as np; i = bpy.data.images.load(%r); w, h = i.size; "
                "p = np.array(i.pixels[:], np.float32).reshape(h, w, 4)[::-1, :, :3]; np.save(%r, p)") % (path, npy)
        run([BLENDER, "-b", "--python-expr", expr])
    return np.load(npy)


def run(cmd, env=None, timeout=900):
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        sys.exit("failed: %s\n%s" % (" ".join(cmd), (r.stdout + r.stderr)[-2000:]))
    return r.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scene", choices=["sunset", "moon"])
    ap.add_argument("--out", required=True)
    ap.add_argument("--engine", default="7")
    ap.add_argument("--samples", default="128")
    ap.add_argument("--fresh", action="store_true")
    a = ap.parse_args()
    binp = os.environ.get("INFINITE_BIN") or sys.exit("set INFINITE_BIN")
    d = os.path.abspath(os.path.join(a.out, a.scene))
    dump, inf = os.path.join(d, "dump"), os.path.join(d, "boat.inf")
    os.makedirs(dump, exist_ok=True)

    if a.fresh or not os.path.exists(os.path.join(dump, "scene.json")) or not os.path.exists(inf):
        env = dict(os.environ, INFINITE_BENCH_B4SCALE="boat-" + a.scene, INFINITE_BENCH_B4ANIM="0",
                   INFINITE_BENCH_BOATDIR=os.path.join(ROOT, "bench/assets/boat"), INFINITE_BENCH_BOATAB="1",
                   INFINITE_BENCH_BOATDUMP=dump, INFINITE_BENCH_BOATSAVE=inf, INFINITE_BENCH_GPUTIMERS="0",
                   INFINITE_EXITAFTER="12")
        run([binp, "-ApplePersistenceIgnoreState", "YES"], env)
    sj = json.load(open(os.path.join(dump, "scene.json")))
    exr = os.path.join(d, "cycles.exr")
    if a.fresh or not os.path.exists(exr):
        run([BLENDER, "-b", "-P", os.path.join(HERE, "boat_scene.py"), "--", dump, os.path.join(d, "cycles.png"), a.samples])

    lin = read_exr(exr)
    h, w = lin.shape[:2]
    out = os.path.join(d, "e%s" % a.engine)
    os.makedirs(out, exist_ok=True)
    run([binp, "--frame", inf, "0", out + "/", "--size", "%dx%d" % (w, h)],
        dict(os.environ, INFINITE_LIVE_ENGINE=a.engine))
    pngs = sorted(p for p in os.listdir(out) if p.endswith(".png") and not p.startswith(("flip", "truth")))
    inf_img = np.asarray(Image.open(os.path.join(out, pngs[-1])).convert("RGB"), np.float32) / 255

    exp = float(sj.get("exposure", 1.0))
    tm = int(sj.get("tonemap", 3))
    t = lin * exp
    truth = srgb(agx(t) if tm == 3 else t).astype(np.float32)
    Image.fromarray((truth * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, "truth.png"))

    import flip_evaluator as flip
    err, mean, _ = flip.evaluate(truth, inf_img, "LDR", applyMagma=False)
    err = np.asarray(err, np.float32).reshape(h, w, -1)[..., 0]
    lt = truth.mean(-1); li = inf_img.mean(-1)
    res = {"case": "boat-" + a.scene, "engine": a.engine, "flip": round(float(mean), 4),
           "brightness_ratio": round(float(li.mean() / max(lt.mean(), 1e-6)), 3)}
    # Glitter: the brightest sea pixels in the reference (top 1 % of the band below the horizon), and the same
    # count of brightest pixels in ours; the overlap (IoU) says whether the path has the same shape.
    sea = np.zeros((h, w), bool)
    hz = int(sj.get("horizon_row", -1)) if "horizon_row" in sj else -1
    if hz < 0:  # horizon: the row where the reference's vertical luminance gradient peaks in the middle third
        g = np.abs(np.diff(lt[:, w // 3: 2 * w // 3].mean(1)))
        hz = int(np.argmax(g[h // 5: 4 * h // 5]) + h // 5)
    sea[hz + 2: min(h, hz + h // 4), :] = True
    k = max(1, int(sea.sum() * 0.01))
    tt = np.sort(lt[sea])[-k]; ti = np.sort(li[sea])[-k]
    mt, mi = sea & (lt >= tt), sea & (li >= ti)
    res["glitter_iou"] = round(float((mt & mi).sum() / max((mt | mi).sum(), 1)), 3)
    res["band_flip"] = round(float(err[sea].mean()), 4)
    res["band_brightness"] = round(float(li[sea].mean() / max(lt[sea].mean(), 1e-6)), 3)
    ys, xs = np.nonzero(mt); yi, xi = np.nonzero(mi)
    res["glitter_aspect_ref"] = round(float((np.ptp(ys) + 1) / (np.ptp(xs) + 1)), 2) if len(ys) else 0
    res["glitter_aspect_inf"] = round(float((np.ptp(yi) + 1) / (np.ptp(xi) + 1)), 2) if len(yi) else 0
    Image.fromarray((np.clip(err, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, "flip.png"))
    print("ABRESULT " + json.dumps(res))


if __name__ == "__main__":
    main()
