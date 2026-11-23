"""Phase 5: data for the Lab Note's first figure, the trap (README section 2): the Moog ladder at g = 1.3764
(McClellan 2026, Prop. 4.7), state norm over 20 000 samples from a unit start, for
  - k alternating 0, 2.89 every sample (the paper's counterexample: grows),
  - k frozen at 2.89 (stable at every fixed setting: decays),
  - k alternating 0, 2.85 (inside the certified range: cannot grow).
The start is the eigenvector of the worst two-step product (the slowest-growing start would hide the growth).
Writes data/phase5-trap.json (every 100th sample).

Also B1 on the scored witnesses: for every G and B kernel with a witness in data/phase3-L-G.json and
data/phase4-B.json, the phase 1 frozen checks (B1a: radius < 1 at the box midpoint; B1b: radius < 1 at every
point of a 7-per-axis grid), on the Python matrices. Writes data/phase5-frozen.json.
"""
import json, pathlib, sys
import numpy as np
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from stabmod import discretise, moog_Ac, radius  # noqa: E402
from gen_kernels import A_from_spec, frozen_grid  # noqa: E402

G, N = 1.3764, 20000
A = lambda k: discretise(moog_Ac(k), G)  # noqa: E731
w, V = np.linalg.eig(A(2.89) @ A(0.0))
x0 = np.real(V[:, int(np.argmax(np.abs(w)))]); x0 /= np.linalg.norm(x0)
out = {"g": G, "n": N, "step": 100, "series": {}}
for label, ks in (("alternating 0, 2.89", (0.0, 2.89)), ("frozen 2.89", (2.89, 2.89)), ("alternating 0, 2.85", (0.0, 2.85))):
    x, ys = x0.copy(), []
    for i in range(N + 1):
        if i % 100 == 0:
            ys.append(float(np.linalg.norm(x)))
        x = A(ks[i % 2]) @ x
    out["series"][label] = ys
(HERE.parent / "data" / "phase5-trap.json").write_text(json.dumps(out))
print({k: (v[0], v[-1]) for k, v in out["series"].items()})

ROOT = HERE.parent
specs = {s["id"]: s for c in ("G", "B") for s in json.loads((ROOT / "corpus" / f"{c}.json").read_text())}
fro = {}
for f, st in (("phase3-L-G.json", "G"), ("phase4-B.json", "B")):
    W = [r for r in json.loads((ROOT / "data" / f).read_text())["rows"] if r["set"] == st and r["witness"]]
    rows = []
    for r in W:
        s = specs[r["name"]]
        rows.append({"name": r["name"], "status": r["status"], "witness_len": len(r["witness_seq"]),
                     "frozen_default_says_stable": bool(radius(A_from_spec(s, tuple([0.5] * s["d"]))) < 1),
                     "frozen_grid_says_stable": bool(max(radius(A_from_spec(s, th)) for th in frozen_grid(s["d"])) < 1)})
    fro[st] = {"with_witness": len(rows), "B1a": sum(x["frozen_default_says_stable"] for x in rows),
               "B1b": sum(x["frozen_grid_says_stable"] for x in rows),
               "certified": sum(x["status"] == "certified" for x in rows),
               "length_1": sum(x["witness_len"] == 1 for x in rows), "rows": rows}
(ROOT / "data" / "phase5-frozen.json").write_text(json.dumps(fro, indent=1))
print({k: {a: b for a, b in v.items() if a != "rows"} for k, v in fro.items()})
