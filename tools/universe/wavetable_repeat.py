#!/usr/bin/env python3
"""How often a Wavetable knob move disagrees with itself (PLAN-2026-10-30 item 14, the ceiling on knob direction).
100 one-knob pairs from own_wavetable.jsonl (the pairs knob_direction.py scores) are rendered again with both oscillators'
start phase turned by 0.37 of a cycle, a change no listener hears. For every axis the stored move changes clearly
(|change| >= 0.3 spread, knob_direction.py's eff), the re-rendered move must change it the same way. The disagreement rate
is the share that flips: with 20% flipping, 90% direction accuracy cannot be reached on these characteristics.
    wavetable_repeat.py [--n 100]
Writes docs/plans/data-universe/feel/wavetable_repeat.json."""
import argparse, json, os, sys, tempfile
os.environ.setdefault("INF_STIM", "C3")  # only the window knob_direction.py reads
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o, p3_characteristics as p3, sound_data as sd  # noqa: E402
OUT = os.path.join(HERE, "../../docs/plans/data-universe/feel/wavetable_repeat.json")
NODE, EFF, TURN = "Wavetable", 0.3, 0.37
PHASE = ("a.phase", "b.phase")


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--n", type=int, default=100); a = ap.parse_args()
    table, sw, fixed = o.param_table(NODE); tags = o.tag_of(table); keys = [p["key"] for p in sw]
    d = sd.load(o.out_path(NODE)); by = o.rows_of(o.out_path(NODE))
    keep = np.flatnonzero(d["ok"][:, 0])
    V = {i: p3.vec(d["mel"][i, 0].astype(float) / 2 - 120) for i in keep}
    means = [j for j, n in enumerate(p3.COLS) if n.endswith(".mean")]
    spread = np.nanstd(np.array([V[i][means] for i in keep]), 0) + 1e-9
    groups = {}
    for i in keep:
        groups.setdefault(d["group"][i], []).append(i)
    P = []
    for ix in groups.values():
        K = d["knobs"][ix]
        for x in range(len(ix)):
            diff = np.abs(K - K[x]) > 0.005
            for y in np.flatnonzero(diff.sum(1) == 1):
                j = int(np.flatnonzero(diff[y])[0])
                if y > x and keys[j] not in PHASE and "phaseRandomize" not in keys[j]:
                    P.append((ix[x], ix[y], j))
    P = [P[k] for k in np.random.RandomState(0).choice(len(P), min(a.n, len(P)), replace=False)]
    turned = []
    for x, y, j in P:
        for i in (x, y):
            rec = dict(by[d["preset_id"][i]][0]["recipe"])
            for k in PHASE:
                rec[k] = (float(rec.get(k, 0)) + TURN) % 1.0
            turned.append(rec)
    with tempfile.TemporaryDirectory(prefix="wt_rep_") as tmp:
        wavs = o.render_batch(o.binary(), NODE, tags, turned, tmp)
        W = []
        for k, (rec, wav) in enumerate(zip(turned, wavs)):
            meta = {"src": "repeat", "id": "rep%d" % k, "label": "repeat", "design": "repeat", "bank": "repeat", "rec": rec, "knobs": [], "ph": ""}
            rows = o.rows_for(NODE, sw, fixed, tags, wav, tmp, meta) if wav else None
            W.append(p3.vec(o.mel_of(rows[0]).reshape(sd.MEL_SHAPE) / 2 - 120) if rows and rows[0].get("mel") else None)
    clear = flip = same_clear = 0; self_moves = []; per_knob = {}
    for (x, y, j), wx, wy in zip(P, W[0::2], W[1::2]):
        if wx is None or wy is None:
            continue
        m0 = np.nan_to_num((V[y][means] - V[x][means]) / spread); m1 = np.nan_to_num((wy[means] - wx[means]) / spread)
        self_moves += list(np.abs(np.nan_to_num((wx[means] - V[x][means]) / spread)))
        c = np.abs(m0) >= EFF; f = c & (np.sign(m0) != np.sign(m1))
        clear += int(c.sum()); flip += int(f.sum()); same_clear += int((c & (np.abs(m1) >= EFF)).sum())
        k = per_knob.setdefault(keys[j], [0, 0]); k[0] += int(c.sum()); k[1] += int(f.sum())
    res = {"pairs": len(P), "rendered": sum(w is not None for w in W) // 2, "clear_axis_moves": clear, "flips": flip,
           "disagreement": round(flip / clear, 3) if clear else None, "still_clear": round(same_clear / clear, 3) if clear else None,
           "self_change_median_spread": round(float(np.median(self_moves)), 3) if self_moves else None,
           "self_change_over_eff": round(float(np.mean(np.array(self_moves) >= EFF)), 3) if self_moves else None,
           "per_knob": {k: {"clear": v[0], "flip": round(v[1] / v[0], 3) if v[0] else None} for k, v in sorted(per_knob.items())}}
    json.dump(res, open(OUT, "w"), indent=1)
    print(json.dumps({k: v for k, v in res.items() if k != "per_knob"}))


if __name__ == "__main__":
    main()
