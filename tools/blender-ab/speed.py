#!/usr/bin/env python3
"""Speed ruler (docs/plans/realtime-engine/SPEED_PLAN.md, block S0). Times are warm: the launch and scene build are
reported apart from the samples and frames.

  speed.py warm   [scenes] [--repeat 2]     S0.1/S0.2: scene build ms, ms per settle pass (16 spp), ms per live frame
                                            (frames 2..31 of one held view, at the 67 % live scale), one launch each
  speed.py equal  [scenes] [--engines settle,pt] [--size 960x540]
                                            S0.4: seconds until FLIP against the engine's own 1024 spp frame is 0.01
  speed.py cycles [scenes] [--spp 1024]     S0.3: Cycles with adaptive sampling off, Blender's own render time
  speed.py table                            the markdown table of the last results (for AUTORUN)

Scenes: judge_table, ref_interior, carpet_pile (the speed set, 1920x1080). INFINITE_BIN picks the binary.
Results land in ~/.cache/infinite-sweep/speed/results.json, merged per scene and step.
"""
import argparse
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
os.environ.setdefault("INFINITE_BIN", os.path.join(REPO, "build/Infinite.app/Contents/MacOS/Infinite"))
import harness  # noqa: E402

OUT = os.path.expanduser("~/.cache/infinite-sweep/speed")
RESULTS = os.path.join(OUT, "results.json")
SCENES = ("judge_table", "ref_interior", "carpet_pile")
SIZE = (1920, 1080)
BOUNCES = 8
FEATURES = {"live": "7", "settle": "15", "pt": None}
LADDER = (16, 32, 64, 128, 256, 512)
FLIP_EQUAL = 0.01


def scene_json(name, size, samples=1024):
    if name == "judge_table":
        import judge_table as JT
        sc = JT.judge_table("flaws", size=size, samples=samples)
    else:
        from cases import CASES
        sc = json.loads(json.dumps(CASES[name]))
        sc["size"], sc["samples"] = list(size), samples
    sc["supersample"] = 1  # one picture per export: the engines' own samples, no jittered averaging on top
    return sc


def case_dir(name):
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    return d


def blender(name, size, spp, adaptive=False, tag=None):
    """Cycles render of the scene into <case>/cycles_<spp>.png (also writes the glbs Infinite loads); Blender's own
    render seconds (render_init to render_complete) and the wall seconds."""
    d = case_dir(name)
    sc = scene_json(name, size, spp)
    sc["adaptive"], sc["bounces"] = adaptive, BOUNCES
    for o in sc["objects"]:  # the generated textures, as harness.main makes them
        m = o.get("material", {})
        for tex in m.get("maps", {}).values():
            harness.make_texture(d, tex)
        ly = m.get("layer")
        while ly:
            if ly.get("mask"):
                harness.make_texture(d, ly["mask"])
            ly = ly.get("layer")
        if m.get("shade", {}).get("tex") == "image":
            harness.make_texture(d, m["shade"].get("image", "blotches"))
    open(os.path.join(d, "scene.json"), "w").write(json.dumps(sc, sort_keys=True, indent=1))
    t0 = time.time()
    _, log = harness.run([harness.BLENDER, "-b", "--factory-startup", "--python", os.path.join(HERE, "blender_scene.py"), "--", d], 3600)
    wall = time.time() - t0
    if "SCENEDONE" not in log:
        sys.exit("blender failed on %s:\n%s" % (name, log[-3000:]))
    m = re.findall(r"CYCLESTIME ([0-9.]+)", log)
    os.replace(os.path.join(d, "truth.png"), os.path.join(d, (tag or "cycles_%d" % spp) + ".png"))
    return float(m[-1]) if m else None, wall


def patch_text(name, engine, spp, size):
    d = case_dir(name)
    if name == "carpet_pile":
        p = os.path.join(d, "carpet.inf")
        subprocess.run([sys.executable, os.path.join(HERE, "scale", "carpet_pile.py"), p, "200000", str(size[1])], check=True)
        t = open(p).read()
        t = re.sub(r"  f width \d+\n  f height \d+", "  f width %d\n  f height %d" % size, t)
        t = re.sub(r"  i ptSamples \d+\n  i ptBounces \d+", "  i ptSamples %d\n  i ptBounces %d" % (spp, BOUNCES), t)
        if engine == "pt":
            t = t.replace("  i ptSamples", "  i engine 1\n  i ptSamples", 1)
        return t
    if not os.path.exists(os.path.join(d, "obj_0.glb")):
        print("%s: no meshes yet, a 16 spp Cycles run writes them" % name, flush=True)
        blender(name, size, 16)
    return harness.to_patch(scene_json(name, size), d, engine=engine, pt_samples=spp, pt_bounces=BOUNCES)


def infinite(name, engine, spp, size, tag, extra_env=None):
    """One launch: the picture at <case>/<tag>.png, wall seconds and the ENGINE_PROF lines."""
    d = case_dir(name)
    pf = os.path.join(d, tag + ".inf")
    open(pf, "w").write(patch_text(name, engine, spp, size))
    fd = os.path.join(d, "f")
    os.makedirs(fd, exist_ok=True)
    for f in os.listdir(fd):
        os.remove(os.path.join(fd, f))
    env = dict(os.environ)
    env.pop("INFINITE_LIVE_ENGINE", None)
    if FEATURES[engine]:
        env["INFINITE_LIVE_ENGINE"] = FEATURES[engine]
    env["INFINITE_ENGINE_PROF"] = "1"
    env.update(extra_env or {})
    t0 = time.time()
    r = subprocess.run([harness.INFINITE, "--frame", pf, "0", fd + "/", "--size", "%dx%d" % size],
                       capture_output=True, text=True, timeout=3600, env=env)
    wall = time.time() - t0
    pngs = sorted(f for f in os.listdir(fd) if f.endswith(".png"))
    if not pngs:
        sys.exit("infinite failed (%s %s):\n%s" % (name, tag, (r.stdout + r.stderr)[-2000:]))
    os.replace(os.path.join(fd, pngs[-1]), os.path.join(d, tag + ".png"))
    return wall, [l for l in (r.stdout + r.stderr).splitlines() if l.startswith("ENGINE_PROF")]


def num(line, key):
    m = re.search(r"\b%s=([-0-9.]+)" % key, line)
    return float(m.group(1)) if m else None


def after_build(prof):
    """The lines after the last scene build: a patch's models load in the background, so the first cooks see an
    empty scene and trace it too."""
    idx = [i for i, l in enumerate(prof) if l.startswith("ENGINE_PROF scene")]
    return prof[idx[-1]:] if idx else prof


def warm_once(name, size):
    _, prof = infinite(name, "settle", 64, size, "warm_settle", {"INFINITE_ENGINE_STOP": "0"})
    prof = after_build(prof)
    build = [num(l, "total") for l in prof if l.startswith("ENGINE_PROF scene")]
    passes = [(num(l, "n"), num(l, "ms")) for l in prof if l.startswith("ENGINE_PROF settle spp=")]
    full = [ms for n, ms in passes[1:] if n == 16]  # the first pass also clears and allocates
    _, prof = infinite(name, "live", 1, size, "warm_live", {"INFINITE_ENGINE_UPSCALE": "67"})
    prof = after_build(prof)
    frames = []
    for l in prof:
        if l.startswith("ENGINE_PROF live frame="):
            k = int(num(l, "frame"))
            if k == 1 and frames:
                break  # a second warm-up block (a later cook) starts again at 1
            frames.append((k, num(l, "ms")))
    live = [ms for k, ms in frames if 2 <= k <= 31]
    return {"build_ms": round(build[0], 1) if build else None,
            "settle_pass_ms": round(statistics.median(full), 2) if full else None,
            "settle_msamples_s": round(16 * size[0] * size[1] / (statistics.median(full) / 1000.0) / 1e6, 1) if full else None,
            "live_frame_ms": round(statistics.median(live), 2) if live else None,
            "live_frames": len(live)}


def merge(name, step, value):
    os.makedirs(OUT, exist_ok=True)
    allr = json.load(open(RESULTS)) if os.path.exists(RESULTS) else {}
    allr.setdefault(name, {})[step] = value
    json.dump(allr, open(RESULTS, "w"), indent=1, sort_keys=True)


def cmd_warm(a):
    for name in a.scenes:
        runs = [warm_once(name, SIZE) for _ in range(a.repeat)]
        r = dict(runs[-1])
        for k in ("build_ms", "settle_pass_ms", "live_frame_ms"):
            v = [x[k] for x in runs if x[k]]
            if len(v) > 1:
                r[k + "_spread"] = round((max(v) - min(v)) / statistics.mean(v), 3)
        r["size"] = list(SIZE)
        merge(name, "warm", r)
        print("warm %-12s %s" % (name, json.dumps(r)), flush=True)


def flip(ref, img):
    import flip_evaluator as F
    _, mean, _ = F.evaluate(harness.load(ref), harness.load(img), "LDR")
    return float(mean)


def cmd_equal(a):
    size = tuple(int(x) for x in a.size.split("x"))
    for name in a.scenes:
        # Keep engines not re-run in this call (same size only), so one engine can be redone alone.
        old = (json.load(open(RESULTS)) if os.path.exists(RESULTS) else {}).get(name, {}).get("equal", {})
        row = old if old.get("size") == list(size) else {"size": list(size)}
        for eng in a.engines.split(","):
            t0 = time.time()
            if eng == "cycles":
                if name == "carpet_pile":
                    continue
                row[eng] = equal_cycles(name, size, t0, a)
                merge(name, "equal", row)
                continue
            # Warm seconds for N spp = wall(N) - wall(1): launch, build and the first sample cancel out. Settle runs as
            # shipped (its adaptive stop on); the reference traces every sample.
            env = {"INFINITE_ENGINE_STOP": "0"} if eng == "settle" else {}
            infinite(name, eng, 1024, size, "eq_%s_ref" % eng, env)
            base, _ = infinite(name, eng, 1, size, "eq_%s_1" % eng)
            ref = os.path.join(case_dir(name), "eq_%s_ref.png" % eng)
            pts = []
            for spp in LADDER:
                wall, prof = infinite(name, eng, spp, size, "eq_%s_%d" % (eng, spp))
                f = flip(ref, os.path.join(case_dir(name), "eq_%s_%d.png" % (eng, spp)))
                done = [num(l, "spp") for l in after_build(prof) if l.startswith("ENGINE_PROF settle spp=")]
                pts.append({"spp": spp, "traced": int(done[-1]) if done else spp, "s": round(max(wall - base, 0.01), 2), "flip": round(f, 4)})
                print("equal %-12s %-6s %4d spp  %6.2f s  FLIP %.4f" % (name, eng, spp, wall - base, f), flush=True)
                if f <= FLIP_EQUAL or time.time() - t0 > a.budget * 60:
                    break
            row[eng] = {"points": pts, "seconds": equal_seconds(pts)}
            merge(name, "equal", row)
        if "settle" in row and "pt" in row and row["settle"]["seconds"] and row["pt"]["seconds"]:
            row["speedup"] = round(secs(row["pt"]["seconds"]) / secs(row["settle"]["seconds"]), 2)
            merge(name, "equal", row)
        if "settle" in row and "cycles" in row and row["settle"]["seconds"] and row["cycles"]["seconds"]:
            row["vs_cycles"] = round(secs(row["cycles"]["seconds"]) / secs(row["settle"]["seconds"]), 2)
            merge(name, "equal", row)
        print("equal %-12s %s" % (name, json.dumps({k: (v["seconds"] if isinstance(v, dict) else v) for k, v in row.items()})), flush=True)


def equal_cycles(name, size, t0, a):
    """Cycles' side of S0.4: adaptive off, settle's bounces; seconds = Blender's render time(N) - render time(1)."""
    blender(name, size, 1024, tag="eq_cycles_ref")
    base, _ = blender(name, size, 1, tag="eq_cycles_1")
    ref = os.path.join(case_dir(name), "eq_cycles_ref.png")
    pts = []
    for spp in LADDER:
        t, _ = blender(name, size, spp, tag="eq_cycles_%d" % spp)
        f = flip(ref, os.path.join(case_dir(name), "eq_cycles_%d.png" % spp))
        pts.append({"spp": spp, "traced": spp, "s": round(max(t - base, 0.01), 2), "flip": round(f, 4)})
        print("equal %-12s %-6s %4d spp  %6.2f s  FLIP %.4f" % (name, "cycles", spp, t - base, f), flush=True)
        if f <= FLIP_EQUAL or time.time() - t0 > a.budget * 60:
            break
    return {"points": pts, "seconds": equal_seconds(pts)}


def equal_seconds(pts):
    """Seconds at which FLIP crosses 0.01, log-log between the two ladder points around it; None if never."""
    for p, q in zip(pts, pts[1:]):
        if p["flip"] > FLIP_EQUAL >= q["flip"]:
            u = (math.log(p["flip"]) - math.log(FLIP_EQUAL)) / (math.log(p["flip"]) - math.log(q["flip"]))
            return round(math.exp(math.log(p["s"]) + u * (math.log(q["s"]) - math.log(p["s"]))), 2)
    if pts and pts[0]["flip"] <= FLIP_EQUAL:
        return pts[0]["s"]
    # Not reached within the ladder: the last two points' log-log line carried on (marked "~" in the table).
    if len(pts) >= 2 and pts[-1]["flip"] < pts[-2]["flip"]:
        p, q = pts[-2], pts[-1]
        k = math.log(q["flip"] / p["flip"]) / math.log(q["s"] / p["s"])
        return "~%.1f" % (q["s"] * (FLIP_EQUAL / q["flip"]) ** (1.0 / k))
    return None


def secs(v):
    return float(str(v).lstrip("~")) if v is not None else None


def cmd_cycles(a):
    for name in a.scenes:
        if name == "carpet_pile":
            print("cycles carpet_pile: no Blender scene (Infinite patch only)")
            continue
        t16, w16 = blender(name, SIZE, 16)
        tn, wn = blender(name, SIZE, a.spp)
        per = (tn - t16) / (a.spp - 16)
        r = {"spp": a.spp, "render_s": round(tn, 1), "wall_s": round(wn, 1), "ms_per_spp": round(per * 1000, 1),
             "setup_s": round(t16 - 16 * per, 1), "adaptive": False, "size": list(SIZE)}
        merge(name, "cycles", r)
        print("cycles %-12s %s" % (name, json.dumps(r)), flush=True)


def cmd_rescore(a):
    """Recompute each stored equal row's seconds and ratios from its points (no renders)."""
    allr = json.load(open(RESULTS))
    for name, r in allr.items():
        e = r.get("equal")
        if not e:
            continue
        for eng in ("settle", "pt", "cycles"):
            if eng in e:
                e[eng]["seconds"] = equal_seconds(e[eng]["points"])
        if e.get("settle", {}).get("seconds") and e.get("pt", {}).get("seconds"):
            e["speedup"] = round(secs(e["pt"]["seconds"]) / secs(e["settle"]["seconds"]), 2)
        if e.get("settle", {}).get("seconds") and e.get("cycles", {}).get("seconds"):
            e["vs_cycles"] = round(secs(e["cycles"]["seconds"]) / secs(e["settle"]["seconds"]), 2)
    json.dump(allr, open(RESULTS, "w"), indent=1, sort_keys=True)


def cmd_table(a):
    allr = json.load(open(RESULTS))
    print("| Scene | build ms | settle ms / 16 spp pass | Msamples/s | live ms / frame (fps) | Cycles ms / spp | settle s to 0.01 | PT s to 0.01 | PT / settle | Cycles s to 0.01 | Cycles / settle |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for name in SCENES:
        r = allr.get(name, {})
        w, c, e = r.get("warm", {}), r.get("cycles", {}), r.get("equal", {})
        lf = w.get("live_frame_ms")
        cell = lambda v: "-" if v is None else str(v)
        print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            name, cell(w.get("build_ms")), cell(w.get("settle_pass_ms")), cell(w.get("settle_msamples_s")),
            "-" if lf is None else "%s (%.1f)" % (lf, 1000.0 / lf), cell(c.get("ms_per_spp")),
            cell(e.get("settle", {}).get("seconds")), cell(e.get("pt", {}).get("seconds")), cell(e.get("speedup")),
            cell(e.get("cycles", {}).get("seconds")), cell(e.get("vs_cycles"))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("warm", "equal", "cycles", "table", "rescore"))
    ap.add_argument("scenes", nargs="*")
    ap.add_argument("--repeat", type=int, default=2)
    ap.add_argument("--engines", default="settle,pt,cycles")
    ap.add_argument("--size", default="960x540")
    ap.add_argument("--spp", type=int, default=1024)
    ap.add_argument("--budget", type=float, default=20.0, help="minutes per scene and engine for the equal ladder")
    a = ap.parse_args()
    a.scenes = a.scenes or list(SCENES)
    {"warm": cmd_warm, "equal": cmd_equal, "cycles": cmd_cycles, "table": cmd_table, "rescore": cmd_rescore}[a.cmd](a)


if __name__ == "__main__":
    main()
