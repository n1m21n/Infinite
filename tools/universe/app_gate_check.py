"""The gate the app compiles today (GenerateSamplesCritic.inc VerifyAudioSpectrum, 8 thresholds) on every clean take.
    app_gate_check.py spectrum.jsonl..."""
import os, re, sys
sys.path.insert(0, os.path.expanduser("~/Infinite/repo/tools/universe"))
import spectral_gates as sg
inc = open(os.path.expanduser("~/Infinite/repo/src/app/graph/GenerateSamplesCritic.inc")).read()
rows = re.findall(r"\{ ([-0-9., f]+) \}, // (\w+)\n", inc.split("kPartSpectralGates[16]")[1].split("};")[0] + "\n")
G = {name: [float(v.strip().rstrip("f")) for v in vals.split(",")] for vals, name in rows}
def ok(r, g):
    b = [max(0.0, x) for x in r["bands"]]; t = sum(b)
    if r["rms"] <= 1e-7 or t <= 1e-12: return False
    lo, mi, hi = sum(b[:11]) / t, sum(b[11:22]) / t, sum(b[22:]) / t
    wh = max(b[12:]) / t; c = r["corr"] if r["corr"] == r["corr"] else -1.0
    return g[0] <= lo <= g[1] and mi >= g[2] and g[3] <= hi <= g[4] and r["peak"] / r["rms"] >= g[5] and r["silence"] <= g[6] and wh <= g[7] and c >= -0.40
for path in sys.argv[1:]:
    rows = [r for r in sg.load(path) if r["kind"] == "clean" and r["rms"] > 0.003]
    out = []
    for part in sg.PART_NAMES:
        rs = [r for r in rows if ("full" if r["part"] == "all" else r["part"]) == part]
        out.append("%s %d/%d" % (part, sum(not ok(r, G[part]) for r in rs), len(rs)))
    print(os.path.basename(path), " | ".join(out))
