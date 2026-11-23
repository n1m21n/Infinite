"""For L filters with a resonance-like knob: the oracle at T3's certified ceiling (H1: must find nothing),
the frozen limit on that knob, and the oracle's first witness by bisection between the ceiling and the
box top. Reads and extends data/phase0-L.json."""
import json, pathlib, sys, itertools, time, warnings
import numpy as np
warnings.filterwarnings("ignore")
from satloops import oracle
import lit
from phase0_L import grid, SEED

path = pathlib.Path(__file__).resolve().parent.parent / "data" / "phase0-L.json"
R = json.loads(path.read_text())
want = set(sys.argv[1:])
for name, S, lo, hi, log_axes, cells, rax, affine in lit.L:
    if rax is None or (want and name not in want):
        continue
    t0 = time.time()
    c = R[name]["T3_ceiling"]["value"]
    box = lambda v: tuple(v if j == rax else h for j, h in enumerate(hi))
    orc = lambda v: oracle(S, lo, box(v), log_axes, np.random.default_rng(SEED))
    at_c = orc(c)
    fro = lambda v: max(float(max(abs(np.linalg.eigvals(S.linear(th))))) for th in grid(lo, box(v), log_axes)) < 1
    a, b = lo[rax], hi[rax]
    while b - a > (hi[rax] - lo[rax]) / 512:
        m = (a + b) / 2
        a, b = (m, b) if fro(m) else (a, m)
    frozen = a
    a, b = c, hi[rax]
    first = None
    while b - a > (hi[rax] - lo[rax]) / 256:
        m = (a + b) / 2
        w = orc(m)
        if w:
            b, first = m, w
        else:
            a = m
    R[name]["ceiling_check"] = {"oracle_at_T3_ceiling": None if at_c is None else at_c["kind"],
                                "frozen_limit": frozen, "first_witness_at": b,
                                "first_witness_kind": first["kind"] if first else None}
    print(f"{name:12s} T3 {c:.4f}  oracle@T3 {R[name]['ceiling_check']['oracle_at_T3_ceiling']}  frozen {frozen:.4f}  first witness <= {b:.4f}  {time.time()-t0:.0f}s", flush=True)
    path.write_text(json.dumps(R, indent=1, default=float))
