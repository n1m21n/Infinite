"""Independent spot check of the H2 certificates: 200k random (g, k) points per filter, never seen by
the certifier, must all have ||A||_P < 1. Also simulates 2^16 samples of the worst random switching
found. Writes data/phase1-spotcheck.json."""
import json, math, pathlib, sys
import numpy as np
from stabmod import discretise, FILTERS
from certify import certify, Box, pnorm_factory

OUT = pathlib.Path(__file__).resolve().parent.parent / "data"
rng = np.random.default_rng(20261123)
G = (1e-5, 1e3)
res = {}
for name, kmax in [("moog", 2.88485), ("diode", 8.8965), ("sallenkey", 1.99902)]:
    r = certify(lambda g, p: discretise(FILTERS[name](p), g), Box([G[0], 0], [G[1], kmax], log=[0]))
    P = np.array(r["P"]); pn = pnorm_factory(P)
    gs = np.exp(rng.uniform(math.log(G[0]), math.log(G[1]), 200000))
    ks = rng.uniform(0, kmax, 200000)
    ks[:20000] = kmax  # the edge where it is tightest
    worst = max(pn(discretise(FILTERS[name](k), g)) for g, k in zip(gs, ks))
    # simulate random switching from a random start, in the P-norm
    x = rng.standard_normal(FILTERS[name](0).shape[0]); n0 = float(np.sqrt(x @ P @ x)); peak = n0
    for i in range(1 << 16):
        x = discretise(FILTERS[name](ks[i]), gs[i]) @ x
        peak = max(peak, float(np.sqrt(x @ P @ x)))
    res[name] = {"kmax": kmax, "status": r["status"], "max_pnorm_random_200k": worst, "sim_peak_over_start": peak / n0}
    print(name, res[name], flush=True)
(OUT / "phase1-spotcheck.json").write_text(json.dumps(res, indent=1))
