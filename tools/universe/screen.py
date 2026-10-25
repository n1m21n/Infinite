#!/usr/bin/env python3
"""Screen every code-node component alone in a tiny rig (about 0.5 s each), then weight the scene sampler by the result.
    screen.py --bin <Infinite> [--seeds 2] [--workers 2] -> component_screen.json  {type: {ok, n, sec}}
Compose only parts that already work: a 30-node patch of screened parts fails far less often than one of unscreened parts."""
import argparse, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u, scene, visual

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "component_screen.json")


def rig(typ, pool, seed, P, VP):
    """(genome, kind) putting one component between a fixed source and the output."""
    saw = P["S"].index("Field Synth:saw")
    if pool == "S":
        return "N0.%d>S%d.%d>O" % (seed, P["S"].index(typ), seed), "A"
    if pool == "E":
        return "N0.%d>S%d.%d>E%d.%d>O" % (seed, saw, seed, P["E"].index(typ), seed), "A"
    if pool == "N":
        return "N%d.%d>S%d.%d>O" % (P["N"].index(typ), seed, saw, seed), "A"
    return "I%d.%d>O" % (VP["I"].index(typ), seed), "V"


def run_one(a):
    typ, pool, seed, facts, P, VP, B = a
    code, kind = rig(typ, pool, seed, P, VP)
    path = "/tmp/screen_%s_%d.inf" % (typ.replace(":", "_").replace(" ", ""), seed)
    t0 = time.time()
    try:
        if kind == "A":
            open(path, "w").write(u.compile_inf(code, facts, P))
            cp = subprocess.run([B, "--audio-summary", path, path + ".json", "--duration", "3"], capture_output=True, text=True, timeout=40)
            j = json.loads(u.last_json(cp.stdout)); ok = bool(j.get("ok")) and u.classify({"ok": True, "a": j["audio_summary"]}) in ("ok", "clipped")
        else:
            open(path, "w").write(visual.compile_visual(code, facts, VP))
            cp = subprocess.run([B, "--frame", path, "0", path + ".d/", "--size", "160x90"], capture_output=True, text=True, timeout=40)
            j = json.loads(u.last_json(cp.stdout)); ok = bool(j.get("ok")) and visual.classify_frames(j["frame_stats"]) == "ok"
    except Exception:
        ok = False
    for p in (path, path + ".json"):
        os.path.exists(p) and os.remove(p)
    return typ, ok, time.time() - t0


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--bin", default=u.DEFAULT_BIN); ap.add_argument("--seeds", type=int, default=2); ap.add_argument("--workers", type=int, default=2)
    a = ap.parse_args()
    facts = u.load_facts(); P = scene.pools_v2(facts); VP = visual.vpools(facts)
    jobs = [(t, pool, s, facts, P, VP, a.bin) for pool, src in (("S", P["S"]), ("E", P["E"]), ("N", P["N"]), ("I", VP["I"])) for t in src if ":" in t for s in range(1, a.seeds + 1)]
    res = {}
    with ThreadPoolExecutor(a.workers) as ex:
        for typ, ok, sec in ex.map(run_one, jobs):
            r = res.setdefault(typ, {"ok": 0, "n": 0, "sec": 0.0}); r["n"] += 1; r["ok"] += ok; r["sec"] = round(r["sec"] + sec, 2)
    for r in res.values():
        r["sec"] = round(r["sec"] / r["n"], 2)
    json.dump(res, open(OUT, "w"), indent=1)
    dead = sorted(t for t, r in res.items() if r["ok"] == 0)
    print("screened %d components; %d never ok: %s" % (len(res), len(dead), dead))


if __name__ == "__main__":
    main()
