"""Figure data for the Lab Note (phase 5). Reads data/phase1-G.json and the corpus; changes no verdict.

phase5-windup.json: the two kernels that linearise-and-hope (B4) certifies but that blow up. Each is run at
its witness's knob setting, zero input, 20 000 samples: the real saturating kernel from starts of size 1,
10 and 100 (one fixed direction), and the linearised kernel (saturators replaced by slope 1) from 100.
Every 100th sample's state norm is stored.

phase5-corners.json (exploratory, after B was scored): for every kernel with no witness whose T1 LMI is
infeasible outright (t = -inf), the largest spectral radius over the sector corners (each saturator's gain at
an end of its sector) on a 5-point knob grid. A radius >= 1 means a corner is unstable at a fixed knob setting,
so no sector-only certificate can exist, although the real saturator never holds that gain.

    python phase5_figdata.py
"""
import itertools, json, pathlib
import numpy as np
from satloops import SHAPES
from gen_sat import lure

HERE = pathlib.Path(__file__).resolve().parent.parent
N, STEP = 20000, 100

def run(L, th, x0, linear):
    A, B, C = L.ABC(*th)
    f = [(lambda v: v) if linear else SHAPES[s]["f"] for s in L.shapes]
    x, out = x0.copy(), [float(np.linalg.norm(x0))]
    for k in range(1, N + 1):
        y = C @ x
        x = A @ x + B @ np.array([fi(v) for fi, v in zip(f, y)])
        if k % STEP == 0:
            out.append(float(np.linalg.norm(x)) if np.all(np.isfinite(x)) else float("inf"))
            if not np.isfinite(out[-1]) or out[-1] > 1e300:
                out[-1] = 1e300
                out += [1e300] * (N // STEP - len(out) + 1)
                break
    return out

def corners():
    res = {}
    for s, cf in (("G", "G.json"), ("B", "B.json")):
        rows = json.loads((HERE / "data" / f"phase3-{s}.json").read_text())
        specs = {x["id"]: x for x in json.loads((HERE / "corpus" / cf).read_text())}
        out = {}
        for k, r in rows.items():
            if r["witness"] or r["G1"] == "certified" or "t=-inf" not in (r["T1"].get("reason") or ""):
                continue
            L, worst = lure(specs[k]), 0.0
            for th in itertools.product(*[np.linspace(0, 1, 5)] * specs[k]["d"]):
                A, B, C = L.ABC(*th)
                for dl in itertools.product(*[SHAPES[sh]["sector"] for sh in L.shapes]):
                    worst = max(worst, float(max(abs(np.linalg.eigvals(A + B @ np.diag(dl) @ C)))))
            out[k] = {"n": specs[k]["n"], "m": len(specs[k]["sats"]), "max_corner_radius": worst}
        res[s] = out
        print(s, len(out), "infeasible, corner radius >= 1:", sum(v["max_corner_radius"] >= 1 for v in out.values()))
    (HERE / "data" / "phase5-corners.json").write_text(json.dumps(res, indent=1))

def main():
    p1 = json.loads((HERE / "data" / "phase1-G.json").read_text())
    specs = {s["id"]: s for s in json.loads((HERE / "corpus" / "G.json").read_text())}
    res = {"n": N, "step": STEP, "kernels": {}}
    for kid in ("s122-067", "s122-124"):
        spec, r = specs[kid], p1[kid]
        assert r["B4"]["status"] == "certified" and r["witness"] and r["G1"] != "certified"
        L = lure(spec)
        th = np.array(r["witness"]["seq"][0], float)
        A, B, C = L.ABC(*th)
        lin = A + B @ C
        cut = A
        u = np.random.default_rng(1).normal(size=spec["n"]); u /= np.linalg.norm(u)
        res["kernels"][kid] = {
            "shapes": L.shapes, "kinds": [s["kind"] for s in spec["sats"]], "theta": th.tolist(),
            "radius_small_signal": float(max(abs(np.linalg.eigvals(lin)))),
            "radius_cut": float(max(abs(np.linalg.eigvals(cut)))),
            "series": {f"start {a:g}": run(L, th, a * u, False) for a in (1, 10, 100)}
                      | {"linearised, start 100": run(L, th, 100 * u, True)},
        }
    out = HERE / "data" / "phase5-windup.json"
    out.write_text(json.dumps(res))
    for k, v in res["kernels"].items():
        print(k, v["shapes"], f"rho small {v['radius_small_signal']:.4f} cut {v['radius_cut']:.4f}",
              {n: f"{s[-1]:.3g}" for n, s in v["series"].items()})

if __name__ == "__main__":
    main()
    corners()
