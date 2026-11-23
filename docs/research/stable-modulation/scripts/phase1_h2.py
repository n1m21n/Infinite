"""Phase 1, H2: the certifier against McClellan's four hand proofs (DAFx26, Props. 4.2, 4.4, 4.6, 4.9).

Cutoff g in [1e-5, 1e3] (log axis): about 0.15 Hz to 0.4997 fs at 48 kHz, i.e. every cutoff a knob can
reach. The open ends g -> 0 and g -> inf are left out: there A -> +I / -I and nothing decays.
Bisection to 0.001 on the upper end of k (Moog, diode, Sallen-Key); the SVF is scored on whole boxes.
Bars (README H2): Moog >= 2.85, diode >= 8.81, Sallen-Key >= 1.98, SVF certifies R in [Rmin, 10] for
Rmin in {0.01, 0.1}; never past the oracle's smallest unstable value (2.8856 / 8.9009, phase 0).
Writes data/phase1-h2.json.
"""
import json, sys, time, pathlib
from stabmod import discretise, FILTERS
from certify import certify, Box

OUT = pathlib.Path(__file__).resolve().parent.parent / (sys.argv[1] if len(sys.argv) > 1 else "data")
G = (1e-5, 1e3)

def run(name, box):
    t0 = time.time()
    r = certify(lambda g, p: discretise(FILTERS[name](p), g), box)
    r["seconds"] = time.time() - t0
    return r

def bisect(name, lo, hi, tol=1e-3):
    trace = []
    r = run(name, Box([G[0], 0], [G[1], lo], log=[0]))
    trace.append({"kmax": lo, **{k: r.get(k) for k in ("status", "reason", "cells", "seconds", "kappa")}})
    if r["status"] != "certified":
        return None, trace
    while hi - lo > tol:
        mid = round((lo + hi) / 2, 5)
        r = run(name, Box([G[0], 0], [G[1], mid], log=[0]))
        trace.append({"kmax": mid, **{k: r.get(k) for k in ("status", "reason", "cells", "seconds", "kappa")}})
        print(f"  {name} kmax={mid}: {r['status']} ({r['seconds']:.1f}s, {r.get('cells')} cells)", flush=True)
        lo, hi = (mid, hi) if r["status"] == "certified" else (lo, mid)
    return lo, trace

res = {"g_box": G}
for name, lo, hi, bar, paper, unstable in [
        ("moog", 2.5, 2.8856, 2.85, 2.88, 2.8856),
        ("diode", 8.0, 8.9009, 8.81, 8.90, 8.9009),
        ("sallenkey", 1.5, 2.0, 1.98, "any k < 2", 2.0)]:
    k, trace = bisect(name, lo, hi)
    res[name] = {"certified_kmax": k, "bar": bar, "paper": paper, "never_past": unstable,
                 "pass": bool(k is not None and k >= bar and k < unstable), "trace": trace}
    print(name, "certified up to", k, "bar", bar, "pass", res[name]["pass"], flush=True)
svf = {}
for rmin in (0.01, 0.1):
    r = run("svf", Box([G[0], rmin], [G[1], 10.0], log=[0, 1]))
    svf[str(rmin)] = {k: r.get(k) for k in ("status", "reason", "cells", "seconds", "kappa", "P")}
    print("svf R in", [rmin, 10], r["status"], f"{r['seconds']:.1f}s", flush=True)
res["svf"] = {"boxes": svf, "pass": all(v["status"] == "certified" for v in svf.values())}
(OUT / "phase1-h2.json").write_text(json.dumps(res, indent=1))
print("wrote", OUT / "phase1-h2.json")
