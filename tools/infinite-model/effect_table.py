#!/usr/bin/env python3
"""Measured effect table for the MCP tools (knob finder, node effects, match check).

Reads the E1 renders (build/e1/renders: every swept Effects param, 3-4 values, 10 source pictures) and writes one
compact JSON the npm MCP server ships: for each node type, what adding it at defaults does, and for each param and
value what moving it does, as fact tokens ("detail down") with how often (share of sources) and how much (median
level). Moves with no measurable change are kept and marked, because "this knob does nothing here" is an answer too.

  python tools/infinite-model/effect_table.py [--bin APP] [--out build/e1]
      -> tools/npm/infinite-engine/lib/data/effects-table.json
"""
import argparse, collections, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import e1_data  # noqa: E402
from e1_data import SOURCES, swept_params, values, run_batch  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

DEST = os.path.join(ROOT, "tools", "npm", "infinite-engine", "lib", "data", "effects-table.json")


# Facts and buckets, mirrored exactly by tools/npm/infinite-engine/lib/measure.js; change both together.
# fact -> (scale, bucket edges on that scale). `detail` is compared as a log ratio, `tones` as histogram distance.
FACTS = {
    "luma": (1.0, (0.01, 0.04, 0.12)),
    "contrast": (1.0, (0.02, 0.08, 0.2)),
    "spread": (1.0, (0.01, 0.03, 0.08)),
    "red": (1.0, (0.01, 0.04, 0.12)),
    "green": (1.0, (0.01, 0.04, 0.12)),
    "blue": (1.0, (0.01, 0.04, 0.12)),
    "warmth": (1.0, (0.01, 0.04, 0.12)),
    "black": (100.0, (0.01, 0.05, 0.15)),
    "white": (100.0, (0.01, 0.05, 0.15)),
    "alpha": (100.0, (0.01, 0.05, 0.15)),
    "detail": (1.0, (0.1, 0.3, 0.8)),
    "tones": (1.0, (0.03, 0.1, 0.3)),
}
LEVELS = ("none", "small", "medium", "large")


def facts_px(png):
    """Facts of the picture as seen: RGBA composited over black. (The --frame stats read straight colour and ignore
    alpha, so a blurred alpha-edged shape reads as brighter, not softer.) detail = RMS luma gradient, which a blur
    lowers; mean |gradient| barely moves because a blurred edge keeps its total step."""
    a = np.asarray(Image.open(png).convert("RGBA"), np.float64) / 255.0
    rgb = a[..., :3] * a[..., 3:4]
    y = rgb @ np.array([0.2126, 0.7152, 0.0722])
    h = np.bincount(np.minimum((y * 16).astype(int), 15).ravel(), minlength=16) / y.size
    centers = (np.arange(16) + 0.5) / 16
    mu = float((h * centers).sum())
    q = np.sort(y.ravel())
    r, g, b = (float(rgb[..., i].mean()) for i in range(3))
    gx, gy = np.diff(y, axis=1)[:-1, :], np.diff(y, axis=0)[:, :-1]
    return {"luma": float(y.mean()), "contrast": float(q[int(0.98 * (q.size - 1))] - q[int(0.02 * (q.size - 1))]),
            "spread": float(np.sqrt((h * (centers - mu) ** 2).sum())), "red": r, "green": g, "blue": b,
            "warmth": r - b, "black": float((y < 0.02).mean() * 100), "white": float((y > 0.98).mean() * 100),
            "alpha": float(a[..., 3].mean() * 100), "detail": float(np.sqrt((gx ** 2 + gy ** 2).mean())),
            "hist": h.tolist()}


def bucket(before, after):
    """{fact: "up large" | "down small" | ...} for facts that moved; {} when nothing measurably changed."""
    out = {}
    for k, (scale, edges) in FACTS.items():
        if k == "tones":
            dv = float(np.abs(np.asarray(after["hist"]) - np.asarray(before["hist"])).sum() / 2)
        elif k == "detail":
            dv = float(np.log((after[k] + 1e-3) / (before[k] + 1e-3)))
        else:
            dv = (after[k] - before[k]) / scale
        lvl = sum(abs(dv) >= e for e in edges)
        if lvl:
            out[k] = LEVELS[lvl] if k == "tones" else ("up " if dv > 0 else "down ") + LEVELS[lvl]
    return out


def facts_of(sidecar):
    try:
        return facts_px(json.load(open(sidecar))["frame_stats"][0]["file"])
    except (OSError, ValueError, KeyError, IndexError):
        return None


def source_only(a):
    """Source -> Output renders, so 'adding node X' has a measured before."""
    d = os.path.join(a.out, "renders", "_source")
    os.makedirs(d, exist_ok=True)
    jobs = []
    for s, spec in SOURCES.items():
        pf = os.path.join(d, s + ".inf")
        with open(pf, "w") as f:
            f.write(f"infinite-patch 1\nnode 1 {spec}\nend\nnode 3 Utility Output\nend\ncable 3 0 1\n")
        if not os.path.exists(os.path.join(d, s + ".json")):
            jobs.append((pf, os.path.join(d, s) + "/"))
    if jobs:
        run_batch(a.bin, jobs)
    return {s: facts_of(os.path.join(d, s + ".json")) for s in SOURCES}


def tokens(b):
    out = {}
    for f, v in b.items():
        w = v.split()
        out[f if f == "tones" else f"{f} {w[0]}"] = LEVELS.index(w[-1])
    return out


def summarise(changes):
    """[bucket dict per source] -> {token: [share, level]} sorted by share, plus how many sources showed no change."""
    n = len(changes)
    cnt, lv = collections.Counter(), collections.defaultdict(list)
    for b in changes:
        for t, l in tokens(b).items():
            cnt[t] += 1
            lv[t].append(l)
    eff = {t: [round(k / n, 2), LEVELS[sorted(lv[t])[len(lv[t]) // 2]]] for t, k in cnt.most_common()}
    return eff, sum(1 for b in changes if not b)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/e1"))
    ap.add_argument("--dest", default=DEST)
    a = ap.parse_args()
    from sweep import fill_filter_menus
    types = [fill_filter_menus(t) for t in json.load(open(os.path.join(a.out, "describe.json")))["types"]
             if t["category"] == e1_data.FAMILY and t["type"] not in e1_data.SKIP_TYPES]
    src = source_only(a)
    table = {"version": 1, "family": e1_data.FAMILY, "commit": e1_data.git_commit(), "size": e1_data.SIZE,
             "time": float(e1_data.TIME), "sources": sorted(SOURCES),
             "levels": {k: list(e) for k, (_, e) in FACTS.items()}, "scales": {k: s for k, (s, _) in FACTS.items()},
             "types": {}}
    n_moves = 0
    for t in types:
        tdir = os.path.join(a.out, "renders", t["type"].replace(" ", "_"))
        before = {s: facts_of(os.path.join(tdir, s, "before.json")) for s in SOURCES}
        add = [bucket(src[s], before[s]) for s in SOURCES if src[s] and before[s]]
        if not add:
            continue
        add_eff, add_none = summarise(add)
        params = {}
        for r in swept_params(t, None):  # params with no renders (not claimed) are skipped below
            moves = []
            for k, (v, d) in enumerate(values(r)):
                ch = []
                for s in SOURCES:
                    af = facts_of(os.path.join(tdir, s, f"{r['key']}_{k}.json"))
                    if af and before[s]:
                        ch.append(bucket(before[s], af))
                if not ch:
                    continue
                eff, none = summarise(ch)
                m = {"value": v, "direction": d, "patch_line": f"{r['tag']} {r['key']} {v}", "effect": eff,
                     "no_change": round(none / len(ch), 2)}
                if r.get("options"):
                    m["option"] = r["options"][int(float(v))] if int(float(v)) < len(r["options"]) else v
                moves.append(m)
                n_moves += 1
            if moves:
                p = {"label": r.get("label") or r["key"], "kind": r["kind"], "tag": r["tag"],
                     "default": r.get("default"), "min": r["min"], "max": r["max"], "moves": moves}
                if r.get("options"):
                    p["options"] = r["options"]
                params[r["key"]] = p
        table["types"][t["type"]] = {"node_line": f"{e1_data.FAMILY} {t['type']}", "add": add_eff,
                                     "add_no_change": round(add_none / len(add), 2), "params": params}
    os.makedirs(os.path.dirname(a.dest), exist_ok=True)
    with open(a.dest, "w") as f:
        json.dump(table, f, separators=(",", ":"))
    print(f"[table] {len(table['types'])} types, {n_moves} moves -> {a.dest} ({os.path.getsize(a.dest) // 1024} KB)")


if __name__ == "__main__":
    main()
