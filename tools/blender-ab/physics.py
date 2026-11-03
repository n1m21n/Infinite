#!/usr/bin/env python3
"""Blender vs Infinite physics A/B: one scene, both simulations, scored on motion.

Each case is a small scene. Blender builds it, steps its rigid body, cloth or
particle simulation frame by frame and writes the truth (blender_physics.py);
the same scene becomes an Infinite patch (Rigid Bodies, Cloth or Particle
System) stepped headless with --frame --geometry, which writes the node's
world-space vertices or points at every frame. The two tracks are compared
over time, not as pictures.

  python3 tools/blender-ab/physics.py --out DIR [case ...]    # all cases by default
  python3 tools/blender-ab/physics.py --out DIR --set-baseline
  python3 tools/blender-ab/physics.py --list

Blender's truth is reused while a case's scene is unchanged (scene.hash).
"""
import argparse, hashlib, json, math, os, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
INFINITE = os.path.join(REPO, "build-dev/Infinite.app/Contents/MacOS/Infinite")
BASELINE = os.path.join(HERE, "baseline_physics.json")
# A case passes while its error stays within this much (relative) of its baseline.
SLACK = 0.10

G = (0, 0, -9.8)


def tilted(deg):
    """Gravity leaning toward +X by deg: the same as a ground tilted by deg, without a slope."""
    a = math.radians(deg)
    return (9.8 * math.sin(a), 0, -9.8 * math.cos(a))


BODY = {"friction": 0.5, "bounce": 0.5, "linear_damping": 0.04, "angular_damping": 0.1}
CASES = {
    # A cube dropped from 2 m: fall time, bounce heights, rest.
    "rb_drop": {"kind": "rigid", "duration": 3, "gravity": G, "ground": 0, "curve": "height",
                "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 2)}], "body": BODY},
    # A cube resting under gravity leaning 30 degrees, friction 0.3 < tan 30: it slides,
    # a = g (sin 30 - 0.3 cos 30) = 2.35 m/s^2.
    "rb_slide": {"kind": "rigid", "duration": 2, "gravity": tilted(30), "ground": 0,
                 "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 0.25)}],
                 "body": dict(BODY, friction=0.3, bounce=0.0)},
    # Friction 0.8 > tan 30: static friction holds it.
    "rb_stick": {"kind": "rigid", "duration": 2, "gravity": tilted(30), "ground": 0,
                 "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 0.25)}],
                 "body": dict(BODY, friction=0.8, bounce=0.0)},
    # A sphere under gravity leaning 15 degrees rolls: a = 5/7 g sin 15 = 1.81 m/s^2.
    "rb_roll": {"kind": "rigid", "duration": 2, "gravity": tilted(15), "ground": 0,
                "objects": [{"shape": "sphere", "size": 0.5, "location": (0, 0, 0.25)}],
                "body": dict(BODY, bounce=0.0)},
    # A cube turned 30 degrees about two axes lands on a corner and tips flat.
    "rb_tilted": {"kind": "rigid", "duration": 3, "gravity": G, "ground": 0,
                  "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 1), "rotation": (30, 20, 0)}],
                  "body": dict(BODY, bounce=0.2)},
    # The same drop with a convex hull collider on both sides.
    "rb_tilted_hull": {"kind": "rigid", "duration": 3, "gravity": G, "ground": 0,
                       "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 1), "rotation": (30, 20, 0)}],
                       "body": dict(BODY, bounce=0.2, collider="hull")},
    # Four cubes stacked a hair apart: they must settle and stay standing.
    "rb_stack": {"kind": "rigid", "duration": 3, "gravity": G, "ground": 0,
                 "objects": [{"shape": "cube", "size": 0.5, "location": (0, 0, 0.26 + 0.51 * k)} for k in range(4)],
                 "body": dict(BODY, bounce=0.0)},
    # The same stack with each cube 0.2 m further out: it topples.
    "rb_topple": {"kind": "rigid", "duration": 3, "gravity": G, "ground": 0,
                  "objects": [{"shape": "cube", "size": 0.5, "location": (0.2 * k, 0, 0.26 + 0.51 * k)} for k in range(4)],
                  "body": dict(BODY, bounce=0.0)},
    # A 2 m sheet pinned at its four corners sags into a hammock.
    "cloth_hammock": {"kind": "cloth", "duration": 3, "gravity": G, "ground": None,
                      "objects": [{"shape": "grid", "size": 2, "subdivisions": 20, "location": (0, 0, 1)}],
                      "cloth": {"pin": "corners"}},
    # A sheet tipped 10 degrees, pinned along its high edge, swings down and hangs.
    "cloth_swing": {"kind": "cloth", "duration": 3, "gravity": G, "ground": None,
                    "objects": [{"shape": "grid", "size": 2, "subdivisions": 20, "location": (0, 0, 1), "rotation": (10, 0, 0)}],
                    "cloth": {"pin": "top"}},
    # A free sheet falls 1 m onto the ground.
    "cloth_drop": {"kind": "cloth", "duration": 2, "gravity": G, "ground": 0,
                   "objects": [{"shape": "grid", "size": 2, "subdivisions": 20, "location": (0, 0, 1)}],
                   "cloth": {"pin": "none"}},
    # A straight-up stream at 6 m/s, 100 a second: the column rises to v^2 / 2g = 1.84 m.
    "pt_stream": {"kind": "particles", "duration": 2, "gravity": G,
                  "particles": {"rate": 100, "velocity": (0, 0, 6), "location": (0, 0, 0)}},
}


def yup(p):
    return (p[0], p[2], -p[1])


def patch(scene, case):
    """The scene as an Infinite patch: Model 3D (Blender's geo.glb) into the sim node, or a
    Particle System. Node 1 is always the sim node, the one --geometry reads.

    Units, Blender -> Infinite:
      rigid damping d (Bullet: v *= (1 - d) per second) -> d' with exp(-10 d') = 1 - d, since
        Infinite multiplies by 1 / (1 + 10 d' dt) every 1/120 s step;
      friction and bounce as they are (the Blender ground is 1, and Bullet multiplies);
      cloth air damping a (Blender: drag force 0.01 a v on each vertex of mass m) ->
        damping (1 - exp(-0.01 a / m / 120)) per 1/120 s step, Infinite mass 1 (its gravity
        is divided by mass);
      particles: speed along the launch direction, no randomness."""
    gx, gy, gz = yup(scene.get("gravity", G))
    kind = scene["kind"]
    lines = ["infinite-patch 1"]
    if kind == "rigid":
        b = scene["body"]
        shape = scene["objects"][0]["shape"]
        collider = {"hull": 2}.get(b.get("collider"), 1 if shape == "sphere" else 0)
        lines += ["node 1 3D Rigid Bodies",
                  "  i collider %d" % collider, "  f mass %g" % b.get("mass", 1.0),
                  "  f friction %g" % b["friction"], "  f bounce %g" % b["bounce"],
                  "  f linearDamping %g" % (-math.log(1 - b["linear_damping"]) / 10),
                  "  f angularDamping %g" % (-math.log(1 - b["angular_damping"]) / 10),
                  "  f gravityX %g" % gx, "  f gravityY %g" % gy, "  f gravityZ %g" % gz,
                  "  b groundEnabled %d" % (scene.get("ground") is not None),
                  "  f groundHeight %g" % (scene.get("ground") or 0.0), "end"]
    elif kind == "cloth":
        c = scene["cloth"]
        drag = 0.01 * c.get("air_damping", 1.0) / c.get("vertex_mass", 0.3)
        lines += ["node 1 3D Cloth",
                  "  i pinMode %d" % {"none": 0, "top": 1, "corners": 2, "edges": 3}[c.get("pin", "none")],
                  "  f stiffness 1", "  i iterations 20", "  f mass 1",
                  "  f damping %g" % (1 - math.exp(-drag / 120)),
                  "  f gravityX %g" % gx, "  f gravityY %g" % gy, "  f gravityZ %g" % gz,
                  "  b groundEnabled %d" % (scene.get("ground") is not None),
                  "  f groundHeight %g" % (scene.get("ground") or 0.0), "end"]
    else:
        p = scene["particles"]
        v = yup(p["velocity"])
        speed = math.sqrt(sum(x * x for x in v))
        lines += ["node 1 3D Particle System",
                  "  i maxParticles 100000", "  f emitRate %g" % p["rate"], "  i emitShape 0",
                  "  f emitRadius 0", "  f lifetime 1000", "  f lifetimeRandom 0",
                  "  f initialSpeed %g" % speed, "  f speedRandom 0", "  f spread 0",
                  "  f dirX %g" % (v[0] / speed), "  f dirY %g" % (v[1] / speed), "  f dirZ %g" % (v[2] / speed),
                  "  f gravityX %g" % gx, "  f gravityY %g" % gy, "  f gravityZ %g" % gz,
                  "  f drag 0", "  f mass 1", "end"]
        return "\n".join(lines) + "\n"
    lines += ["node 2 3D Model 3D", "  s path %s" % os.path.join(case, "geo.glb"),
              "  b normalize 0", "  b recenter 0", "end", "geo 1 0 2"]
    return "\n".join(lines) + "\n"


def run_blender(case, scene):
    h = hashlib.sha1((json.dumps(scene, sort_keys=True) + open(os.path.join(HERE, "blender_physics.py")).read()).encode()).hexdigest()
    hp = os.path.join(case, "scene.hash")
    if os.path.exists(os.path.join(case, "truth.jsonl")) and os.path.exists(hp) and open(hp).read() == h:
        return
    json.dump(scene, open(os.path.join(case, "scene.json"), "w"), indent=1)
    r = subprocess.run([BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_physics.py"), "--", case],
                       capture_output=True, text=True)
    if "BLENDER_PHYSICS_DONE" not in r.stdout:
        sys.exit("blender failed on %s:\n%s" % (case, (r.stdout + r.stderr)[-3000:]))
    open(hp, "w").write(h)


def run_infinite(case, scene):
    fps = scene.get("fps", 30)
    n = int(round(scene["duration"] * fps)) + 1
    open(os.path.join(case, "inf.inf"), "w").write(patch(scene, case))
    out = os.path.join(case, "inf.jsonl")
    if os.path.exists(out):
        os.remove(out)
    times = ",".join("%.6f" % (k / fps) for k in range(n))
    r = subprocess.run([INFINITE, "--frame", os.path.join(case, "inf.inf"), times, os.path.join(case, "frames") + "/",
                        "--node", "1", "--geometry", out, "--fps", str(fps), "--no-audio"],
                       capture_output=True, text=True, timeout=600)
    if not os.path.exists(out):
        sys.exit("infinite failed on %s:\n%s" % (case, (r.stdout + r.stderr)[-3000:]))


def load(path):
    return [json.loads(l) for l in open(path)]


def kabsch(a, b):
    """The rotation taking point set a (centred) onto b (centred)."""
    u, _, vt = np.linalg.svd(a.T @ b)
    d = np.sign(np.linalg.det(vt.T @ u.T))
    return vt.T @ np.diag([1, 1, d]) @ u.T


def quat_mat(q):
    w, x, y, z = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def angle(r):
    return math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(r) - 1) / 2))))


def score_rigid(truth, inf, scene):
    # Which Infinite vertex belongs to which body: the Blender body whose rest box holds it.
    tv0 = np.array(truth[0]["v"]).reshape(-1, 3)
    boxes, k = [], 0
    for b in truth[0]["bodies"]:
        pts = tv0[k:k + b["n"]]
        k += b["n"]
        boxes.append((pts.min(0) - 1e-3, pts.max(0) + 1e-3))
    iv0 = np.array(inf[0]["v"]).reshape(-1, 3)
    owner = np.full(len(iv0), -1)
    for j, (lo, hi) in enumerate(boxes):
        owner[np.all((iv0 >= lo) & (iv0 <= hi), axis=1)] = j
    nb = len(boxes)
    t = np.array([l["time"] for l in truth])
    tp = np.array([[b["p"] for b in l["bodies"]] for l in truth])           # frames x bodies x 3
    ip = np.zeros_like(tp)
    rot_err = np.zeros((len(truth), nb))
    for f, l in enumerate(inf[:len(truth)]):
        v = np.array(l["v"]).reshape(-1, 3)
        for j in range(nb):
            pts = v[owner == j]
            ip[f, j] = pts.mean(0)
            r_inf = kabsch(iv0[owner == j] - iv0[owner == j].mean(0), pts - ip[f, j])
            q0, qf = truth[0]["bodies"][j]["q"], truth[f]["bodies"][j]["q"]
            r_bl = quat_mat(qf) @ quat_mat(q0).T
            rot_err[f, j] = angle(r_inf.T @ r_bl)
    d = np.linalg.norm(ip - tp, axis=2)
    res = {"pos_rms": float(np.sqrt((d ** 2).mean())), "pos_final": float(d[-1].mean()),
           "rot_rms_deg": float(np.sqrt((rot_err ** 2).mean())), "rot_final_deg": float(rot_err[-1].mean()),
           "error": float(np.sqrt((d ** 2).mean()))}
    # The quantity a viewer reads: height for a drop, travel for a slide or roll.
    travel = np.abs(tp[:, :, 0] - tp[0, :, 0]).max()
    axis = {"height": 1, "x": 0}.get(scene.get("curve"), 0 if travel > 0.1 else 1)
    curves = {"blender": tp[:, :, axis].mean(1), "infinite": ip[:, :, axis].mean(1)}
    res["curve"] = "x (m)" if axis == 0 else "height (m)"
    res["blender_final"] = [round(float(x), 3) for x in tp[-1].mean(0)]
    res["infinite_final"] = [round(float(x), 3) for x in ip[-1].mean(0)]
    return res, t, curves


def score_cloth(truth, inf, scene):
    tv0 = np.array(truth[0]["v"]).reshape(-1, 3)
    iv0 = np.array(inf[0]["v"]).reshape(-1, 3)
    # Each Infinite vertex follows the Blender vertex nearest to it at rest.
    match = np.argmin(((iv0[:, None, :] - tv0[None, :, :]) ** 2).sum(2), axis=1)
    t = np.array([l["time"] for l in truth])
    errs, lo_b, lo_i = [], [], []
    for f in range(len(truth)):
        tv = np.array(truth[f]["v"]).reshape(-1, 3)
        iv = np.array(inf[f]["v"]).reshape(-1, 3)
        errs.append(np.linalg.norm(iv - tv[match], axis=1).mean())
        lo_b.append(tv[:, 1].min())
        lo_i.append(iv[:, 1].min())
    errs = np.array(errs)
    res = {"vertex_mean": float(errs.mean()), "vertex_final": float(errs[-1]), "error": float(errs.mean()),
           "curve": "lowest point (m)", "blender_final": round(float(lo_b[-1]), 3), "infinite_final": round(float(lo_i[-1]), 3)}
    return res, t, {"blender": np.array(lo_b), "infinite": np.array(lo_i)}


def score_particles(truth, inf, scene):
    t = np.array([l["time"] for l in truth])
    mb, mi, cb, ci, hb, hi = [], [], [], [], [], []
    for f in range(len(truth)):
        pb = np.array(truth[f].get("p", [])).reshape(-1, 3)
        pi = np.array(inf[f].get("p", [])).reshape(-1, 3)
        cb.append(len(pb)); ci.append(len(pi))
        mb.append(pb[:, 1].mean() if len(pb) else 0.0); mi.append(pi[:, 1].mean() if len(pi) else 0.0)
        hb.append(pb[:, 1].max() if len(pb) else 0.0); hi.append(pi[:, 1].max() if len(pi) else 0.0)
    mb, mi = np.array(mb), np.array(mi)
    res = {"mean_height_rms": float(np.sqrt(((mb - mi) ** 2).mean())),
           "peak_blender": round(float(max(hb)), 3), "peak_infinite": round(float(max(hi)), 3),
           "count_blender": cb[-1], "count_infinite": ci[-1], "error": float(np.sqrt(((mb - mi) ** 2).mean())),
           "curve": "mean height (m)", "blender_final": round(float(mb[-1]), 3), "infinite_final": round(float(mi[-1]), 3)}
    return res, t, {"blender": mb, "infinite": mi}


def plot(name, res, t, curves, path):
    W, H, pad = 640, 300, 44
    img = Image.new("RGB", (W, H), (250, 250, 248))
    d = ImageDraw.Draw(img)
    ys = np.concatenate([curves["blender"], curves["infinite"]])
    lo, hi = float(ys.min()), float(ys.max())
    if hi - lo < 1e-6:
        hi = lo + 1
    span = hi - lo
    lo -= span * 0.05; hi += span * 0.05
    def xy(tt, yy):
        return (pad + (W - 2 * pad) * tt / max(t[-1], 1e-6), H - pad - (H - 2 * pad) * (yy - lo) / (hi - lo))
    d.rectangle([pad, pad, W - pad, H - pad], outline=(200, 200, 200))
    for key, col in (("blender", (232, 125, 13)), ("infinite", (40, 90, 200))):
        d.line([xy(tt, yy) for tt, yy in zip(t, curves[key])], fill=col, width=2)
    d.text((pad, 8), "%s   %s vs time (s)   error %.4f" % (name, res["curve"], res["error"]), fill=(20, 20, 20))
    d.text((pad, 22), "Blender", fill=(232, 125, 13)); d.text((pad + 60, 22), "Infinite", fill=(40, 90, 200))
    d.text((4, pad), "%.2f" % hi, fill=(90, 90, 90)); d.text((4, H - pad - 10), "%.2f" % lo, fill=(90, 90, 90))
    d.text((W - pad - 20, H - pad + 6), "%.1f" % t[-1], fill=(90, 90, 90))
    img.save(path)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/tmp/blender-ab-physics")
    ap.add_argument("--set-baseline", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("cases", nargs="*")
    a = ap.parse_args()
    if a.list:
        print("\n".join(CASES))
        return 0
    names = a.cases or list(CASES)
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    results, boards, failed = [], [], []
    for name in names:
        scene = dict(CASES[name], fps=CASES[name].get("fps", 30))
        case = os.path.join(a.out, name)
        os.makedirs(case, exist_ok=True)
        run_blender(case, scene)
        run_infinite(case, scene)
        truth, inf = load(os.path.join(case, "truth.jsonl")), load(os.path.join(case, "inf.jsonl"))
        if len(inf) < len(truth):
            sys.exit("%s: Infinite wrote %d frames, Blender %d" % (name, len(inf), len(truth)))
        res, t, curves = {"rigid": score_rigid, "cloth": score_cloth, "particles": score_particles}[scene["kind"]](truth, inf, scene)
        res["case"] = name
        boards.append(plot(name, res, t, curves, os.path.join(case, "curve.png")))
        held = base.get(name)
        res["baseline"] = held
        if held is not None and res["error"] > held * (1 + SLACK) + 1e-4:
            failed.append(name)
        results.append(res)
        print(json.dumps({k: (round(v, 4) if isinstance(v, float) else v) for k, v in res.items()}))
    with open(os.path.join(a.out, "results.jsonl"), "w") as f:
        for r in results:
            f.write(json.dumps(r) + "\n")
    if boards:
        cols = 2
        rows = (len(boards) + cols - 1) // cols
        sheet = Image.new("RGB", (640 * cols, 300 * rows), (255, 255, 255))
        for k, b in enumerate(boards):
            sheet.paste(b, ((k % cols) * 640, (k // cols) * 300))
        sheet.save(os.path.join(a.out, "board.png"))
    if a.set_baseline:
        for r in results:
            base[r["case"]] = round(r["error"], 4)
        json.dump(base, open(BASELINE, "w"), indent=1, sort_keys=True)
        print("baseline written:", BASELINE)
        return 0
    if failed:
        print("FAILED (over baseline):", ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
