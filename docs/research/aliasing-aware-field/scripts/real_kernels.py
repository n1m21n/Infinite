#!/usr/bin/env python3
"""Phase 6 kernel list (README section 17): every kernel phase 5 scored (status ok) that has a nonlinear op,
one "<set>/<label>" per line on stdout. --subsample applies the pre-registered fallback (all of L, 60 each
of G and D, seed 20261123)."""
import csv, os, random, sys

sys.path.insert(0, os.path.dirname(__file__))
from score_heldout import is_nonlinear  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), "..")
rows = list(csv.DictReader(open(os.path.join(ROOT, "data", "phase5-heldout.csv"), encoding="utf-8")))
keep = []
for r in rows:
    if r["status"] != "ok":
        continue
    code = open(os.path.join(ROOT, "corpus", "heldout", r["set"], r["label"] + ".field"), encoding="utf-8").read()
    if is_nonlinear(code):
        keep.append(f'{r["set"]}/{r["label"]}')
if "--subsample" in sys.argv:
    rng = random.Random(20261123)
    out = [k for k in keep if k.startswith("L/")]
    for s in ("G", "D"):
        pool = [k for k in keep if k.startswith(s + "/")]
        out += sorted(rng.sample(pool, min(60, len(pool))))
    keep = out
print("\n".join(keep))
