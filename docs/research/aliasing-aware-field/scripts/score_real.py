#!/usr/bin/env python3
"""Phase 6 scorer (README section 17): H5 on kernel-clip pairs.

Inputs (data/ by default, or the directory given): phase6-measure.csv (needed factor per pair, from the
interleaved-copy oracle), phase6-p1.csv (P1 per kernel), phase6-p6.csv (P6 per pair, optional) and
phase5-heldout.csv (P0 = factor_pred, one 5001 Hz tone at 0.5). Factor 0 means "none within 32x" and ranks
above every factor. Pairs the oracle calls silent are left out and counted. A pair P6 declines falls back
to P0's factor and is counted.
  enough: predicted >= needed     exact: predicted == needed
H5 bars: P6 enough on >= 90% of pairs, exact on >= 70%, enough on strictly more pairs than P0; kill if P6
enough on under 70%."""
import csv, os, sys
from collections import defaultdict

DATA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "data")
FACTORS = [1, 2, 4, 8, 16, 32]


def rank(f):
    return 64 if f == 0 else f


def read(name):
    p = os.path.join(DATA, name)
    return list(csv.DictReader(open(p, encoding="utf-8"))) if os.path.exists(p) else None


meas = read("phase6-measure.csv")
p1 = {(r["set"], r["label"]): r for r in (read("phase6-p1.csv") or [])}
p6 = {(r["set"], r["label"], r["clip"]): r for r in (read("phase6-p6.csv") or [])}
p0 = {(r["set"], r["label"]): int(r["factor_pred"]) for r in read("phase5-heldout.csv") if r["status"] == "ok"}

pairs, silent = [], 0
for r in meas:
    if r["status"] != "ok":
        silent += 1
        continue
    k = (r["set"], r["label"])
    e = {"set": r["set"], "label": r["label"], "clip": r["clip"], "source": r["clip"].rsplit("-", 1)[0],
         "level": r["clip"].rsplit("-", 1)[1], "need": int(r["factor_meas"]),
         "meas": {f: float(r[f"meas_{f}"]) for f in FACTORS}, "P0": p0[k], "rule A": 8}
    if k in p1 and p1[k]["status"] == "ok":
        e["P1"] = int(p1[k]["p1_factor"])
    if p6:
        q = p6.get((r["set"], r["label"], r["clip"]))
        if q is not None and q["status"] == "ok":
            e["P6"], e["p6_fallback"] = int(q["factor_p6"]), 0
        else:
            e["P6"], e["p6_fallback"] = p0[k], 1
    pairs.append(e)

n = len(pairs)
kernels = len({(e["set"], e["label"]) for e in pairs})
print(f"pairs {n} ({kernels} kernels x clips); silent, left out: {silent}")
need_hist = defaultdict(int)
for e in pairs:
    need_hist[e["need"]] += 1
print("needed factor: " + ", ".join(f"{'none' if f == 0 else str(f) + 'x'} {need_hist[f]}" for f in FACTORS + [0]))


def counts(key, rows):
    rows = [e for e in rows if key in e]
    en = sum(rank(e[key]) >= rank(e["need"]) for e in rows)
    ex = sum(e[key] == e["need"] for e in rows)
    return len(rows), en, ex


preds = ["P0", "rule A", "P1"] + (["P6"] if p6 else [])
print("\npredictor   pairs  enough        exact")
for key in preds:
    m, en, ex = counts(key, pairs)
    if m:
        print(f"{key:10s} {m:6d}  {en:5d} ({100 * en / m:5.1f}%)  {ex:5d} ({100 * ex / m:5.1f}%)")

for group in ("source", "level"):
    print(f"\nenough by {group}: " + "  ".join(preds))
    vals = sorted({e[group] for e in pairs})
    for v in vals:
        rows = [e for e in pairs if e[group] == v]
        cells = []
        for key in preds:
            m, en, ex = counts(key, rows)
            cells.append(f"{100 * en / m:5.1f}% ({100 * ex / m:4.1f}% exact)" if m else "-")
        print(f"  {v:8s} " + "  ".join(cells))

if p6:
    m, en, ex = counts("P6", pairs)
    _, en0, _ = counts("P0", pairs)
    fb = sum(e["p6_fallback"] for e in pairs)
    print(f"\nP6 declined, fell back to P0: {fb} of {n} pairs")
    b1, b2, b3 = en >= 0.9 * m, ex >= 0.7 * m, en > en0
    kill = en < 0.7 * m
    print(f"H5 enough {100 * en / m:.1f}% (bar 90%) {'PASS' if b1 else 'FAIL'}; exact {100 * ex / m:.1f}% (bar 70%) "
          f"{'PASS' if b2 else 'FAIL'}; enough {en} vs P0 {en0} {'PASS' if b3 else 'FAIL'}"
          f"  => H5 {'PASS' if b1 and b2 and b3 else 'FAIL'}{'; KILL (under 70% enough)' if kill else ''}")
    short = [e for e in pairs if rank(e["P6"]) < rank(e["need"])]
    over = [e for e in pairs if e["need"] != 0 and rank(e["P6"]) > rank(e["need"])]
    print(f"P6 short on {len(short)} pairs; over on {len(over)}")
    worst = sorted(short, key=lambda e: -e["meas"][e["P6"]] if e["P6"] else 0)[:12]
    for e in worst:
        got = e["meas"][e["P6"]] if e["P6"] else float("nan")
        print(f"  {e['set']}/{e['label']:30s} {e['clip']:9s} P6 {e['P6']}x needs "
              f"{'none' if e['need'] == 0 else str(e['need']) + 'x'}: {got:.1f} dB at P6's factor ({got + 80:+.1f} dB over)")
    # CPU: oversampling cost ~ factor; pairs where both are finite.
    fin = [e for e in pairs if e["need"] and e["P6"]]
    if fin:
        print(f"CPU on the {len(fin)} pairs with a finite need and P6 factor: P6 {sum(e['P6'] for e in fin)} vs need "
              f"{sum(e['need'] for e in fin)} vs P0 {sum(e['P0'] or 32 for e in fin)} vs rule A {8 * len(fin)} (sum of factors)")
