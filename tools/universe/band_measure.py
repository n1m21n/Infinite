"""Centroid and low share from 32 log bands (20 Hz - 20 kHz), generator spectrum dump vs library features.jsonl.
    band_measure.py PART LIBPART spectrum.jsonl..."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.expanduser("~/Infinite/repo/tools/universe"))
import spectral_gates as sg
C = 20.0 * 1000.0 ** ((np.arange(32) + 0.5) / 32)
def m(b):
    b = np.maximum(np.array(b, float), 0); t = b.sum() or 1e-30
    return (C * b).sum() / t, b[:11].sum() / t, b[22:].sum() / t
part, libpart = sys.argv[1], sys.argv[2]
lib = [m(r["bands"]) for r in map(json.loads, open(os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")))
       if r.get("part") == libpart and r.get("loop")]
L = np.median(np.array(lib), 0)
print("library %-8s n=%4d  centroid %6.0f  low %.3f  high %.3f" % (libpart, len(lib), *L))
for f in sys.argv[3:]:
    rows = sg.load(f)
    g = [m(r["bands"]) for r in rows if r["kind"] == "clean" and r["part"] == part and r["rms"] > 0.003]
    G = np.median(np.array(g), 0)
    print("%-22s n=%4d  centroid %6.0f  low %.3f  high %.3f" % (os.path.basename(f), len(g), *G))
