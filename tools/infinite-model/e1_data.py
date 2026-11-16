#!/usr/bin/env python3
"""E1 data pipeline: "which knob did that" on the Effects family (internal/plans/infinite-model, section 8).

For every swept Effects param: 3-4 values x 10 source pictures. Each example is one headless render pair:
  before  = Source -> Effect (defaults) -> Output
  after   = the same patch with one param moved
and the label is the move itself: `<type>.<param> up|down`. Nothing is written by a model or by hand.

Facts per render come from `--frame` (mean/min/max luma, mean rgb, black/white %, alpha, 16-bin luma histogram)
plus one measured fact from the PNG, `detail` (mean gradient magnitude): global stats alone cannot see a blur or a
pixelate (plan section 11, "add one measured fact"). Each row keeps the raw facts (classifier arm, ablation) and
the bucketed change ("luma up, large") the language-model arms read.

Stages (each resumable; renders are reused from their sidecars):
  render   write patches, render them headless          build/e1/renders/
  rows     facts -> rows.jsonl (rows with no measurable change dropped)
  split    held-out types (4 filters) and held-out params (~15 % inside seen types), fixed and sha256-hashed

Usage:
  tools/infinite-model/e1_data.py [--bin PATH] [--out build/e1] [--coverage coverage.json] [--procs 3]
                                  [--budget MIN] [--stage render|rows|split|all]
"""
import argparse, hashlib, json, os, random, subprocess, sys, time
import concurrent.futures as cf
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "headless-coverage"))
from sweep import fill_filter_menus  # noqa: E402  filter menus have no options in --describe

FAMILY = "Effects"
SKIP_TYPES = {"Resynthesize"}  # stateful (generations over time): not a before -> after of one move
SIZE = "128x128"
TIME = "0.5"
SPLIT_SEED = 20261115
CLASSES = {"effective", "conditional", "mod-only"}
ICON = os.path.join(ROOT, "assets", "icon_1024.png")

# Ten source pictures from existing Source nodes: soft, hard-edged, textured, text, photo-like, gradients.
SOURCES = {
    "formula": "Source Formula",
    "noise": "Source Noise",
    "image": f"Source Image Source\n  s path {ICON}",
    "ramp": "Source Ramp",
    "star": "Source Star",
    "ring": "Source Ring",
    "gear": "Source Gear",
    "text": "Source Text",
    "shadertoy": "Source Shadertoy",
    "blob": "Source Blob",
}

# fact -> (scale it is read on, bucket edges on that scale). Signed facts get up/down; `tones` is unsigned.
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
    "detail": (1.0, (0.004, 0.015, 0.05)),
    "tones": (1.0, (0.03, 0.1, 0.3)),
}
LEVELS = ("none", "small", "medium", "large")


def git_commit():
    try:
        return subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], capture_output=True,
                              text=True).stdout.strip()
    except OSError:
        return "?"


# ---- params and values ---------------------------------------------------------------------------
def swept_params(t, cov):
    """Rows of `t` worth asking about: a range or options, a panel label, and (with a coverage file) effective."""
    keep = None
    if cov is not None:
        row = cov.get(t["type"])
        keep = {p["key"] for p in (row or {}).get("params", []) if p.get("class") in CLASSES}
    out = []
    for r in t["rows"]:
        if r["kind"] in ("text", "color") or r["key"] == "headroom":
            continue
        if r.get("min") is None or r.get("max") is None or r["max"] <= r["min"]:
            continue
        if keep is not None and r["key"] not in keep:
            continue
        out.append(r)
    return out


def values(r):
    """3-4 (value text, direction) pairs that differ from the default."""
    d = float(r.get("default") or 0)
    lo, hi = float(r["min"]), float(r["max"])
    if r.get("options"):
        n = len(r["options"])
        di = int(round(d))
        picks = [k for k in range(n) if k != di][:4]
        return [(str(k), "up" if k > di else "down") for k in picks]
    if r["kind"] == "bool":
        return [("0", "down")] if d >= 0.5 else [("1", "up")]
    out = []
    for f in (0.1, 0.35, 0.65, 0.9):
        v = lo + (hi - lo) * f
        if r["kind"] == "int" or r.get("step") == 1:
            v = round(v)
        if abs(v - d) < (hi - lo) * 0.04:
            continue
        txt = str(int(v)) if r["kind"] == "int" else f"{v:.6g}"
        if txt not in [o[0] for o in out]:
            out.append((txt, "up" if v > d else "down"))
    return out[:4]


def patch(source_spec, ftype, extra=""):
    return ("infinite-patch 1\n"
            f"node 1 {source_spec}\nend\n"
            f"node 2 {FAMILY} {ftype}\n{extra}end\n"
            "node 3 Utility Output\nend\n"
            "cable 2 0 1\ncable 3 0 2\n")


# ---- rendering ---------------------------------------------------------------------------------
def run_batch(binary, lines, timeout_pad=60):
    """lines: [(patch, outdir/)]. One process; restarts after a crash at the first unfinished line."""
    while lines:
        first, rest = lines[0], lines[1:]
        lst = first[1].rstrip("/") + ".batch.txt"
        with open(lst, "w") as f:
            for p, o in rest:
                f.write(f"{p}\t{o}\n")
        cmd = [binary, "--frame", first[0], TIME, first[1], "--size", SIZE, "--lenient", "--isolated",
               "--timeout", str(30 + 3 * len(lines)), "--batch", lst]
        try:
            subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_pad + 4 * len(lines))
        except subprocess.TimeoutExpired:
            pass
        os.remove(lst)
        left = [x for x in lines if not os.path.exists(x[1].rstrip("/") + ".json")]
        if left and left[0] == first:
            left = left[1:]  # the first job itself failed: skip it, keep going
        lines = left


def stage_render(a, types):
    jobs = []
    for t in types:
        params = swept_params(t, a.cov)
        for sname, spec in SOURCES.items():
            d = os.path.join(a.out, "renders", t["type"].replace(" ", "_"), sname)
            os.makedirs(d, exist_ok=True)
            todo = [("before", patch(spec, t["type"]))]
            for r in params:
                for k, (v, _) in enumerate(values(r)):
                    todo.append((f"{r['key']}_{k}", patch(spec, t["type"], f"  {r['tag']} {r['key']} {v}\n")))
            for jid, text in todo:
                pf = os.path.join(d, jid + ".inf")
                with open(pf, "w") as f:
                    f.write(text)
                if not os.path.exists(os.path.join(d, jid + ".json")):
                    jobs.append((pf, os.path.join(d, jid) + "/"))
    print(f"[render] {len(jobs)} renders to do", flush=True)
    chunks = [jobs[i:i + a.chunk] for i in range(0, len(jobs), a.chunk)]
    deadline = time.time() + a.budget * 60 if a.budget else None
    t0, done = time.time(), 0
    with cf.ThreadPoolExecutor(a.procs) as ex:
        futs = {}
        for c in chunks:
            futs[ex.submit(lambda c=c: None if deadline and time.time() > deadline else run_batch(a.bin, c))] = len(c)
        for f in cf.as_completed(futs):
            done += futs[f]
            el = time.time() - t0
            print(f"  {done}/{len(jobs)}  {el:.0f} s", flush=True)


# ---- facts ---------------------------------------------------------------------------------------
def facts_of(sidecar):
    try:
        s = json.load(open(sidecar))["frame_stats"][0]
    except (OSError, ValueError, KeyError, IndexError):
        return None
    h = np.asarray(s["luma_histogram"], np.float64)
    centers = (np.arange(16) + 0.5) / 16
    mu = float((h * centers).sum() / max(h.sum(), 1e-9))
    spread = float(np.sqrt((h * (centers - mu) ** 2).sum() / max(h.sum(), 1e-9)))
    img = np.asarray(Image.open(s["file"]).convert("L"), np.float32) / 255.0
    detail = float((np.abs(np.diff(img, axis=0)).mean() + np.abs(np.diff(img, axis=1)).mean()) / 2)
    r, g, b = s["mean_rgb"]
    return {"luma": s["mean_luma"], "contrast": s["max_luma"] - s["min_luma"], "spread": spread,
            "red": r, "green": g, "blue": b, "warmth": r - b, "black": s["black_percent"],
            "white": s["white_percent"], "alpha": s["alpha_coverage"], "detail": detail,
            "hist": [round(x, 5) for x in h.tolist()], "file": s["file"]}


def bucket(before, after):
    """{fact: "up large" | "down small" | ...} for facts that moved; {} when nothing measurably changed."""
    out = {}
    for k, (scale, edges) in FACTS.items():
        if k == "tones":
            dv = float(np.abs(np.asarray(after["hist"]) - np.asarray(before["hist"])).sum() / 2)
        else:
            dv = (after[k] - before[k]) / scale
        lvl = sum(abs(dv) >= e for e in edges)
        if lvl:
            out[k] = (LEVELS[lvl] if k == "tones" else ("up " if dv > 0 else "down ") + LEVELS[lvl])
    return out


def describe_change(b):
    return "; ".join(f"{k} {v}" for k, v in b.items()) or "no change"


def stage_rows(a, types):
    commit = git_commit()
    n_all = n_kept = 0
    with open(os.path.join(a.out, "rows.jsonl"), "w") as fo:
        for t in types:
            for sname, spec in SOURCES.items():
                d = os.path.join(a.out, "renders", t["type"].replace(" ", "_"), sname)
                before = facts_of(os.path.join(d, "before.json"))
                if before is None:
                    continue
                for r in swept_params(t, a.cov):
                    for k, (v, direction) in enumerate(values(r)):
                        after = facts_of(os.path.join(d, f"{r['key']}_{k}.json"))
                        if after is None:
                            continue
                        n_all += 1
                        b = bucket(before, after)
                        if not b:
                            continue
                        n_kept += 1
                        row = {"id": f"{t['type']}|{sname}|{r['key']}|{k}", "type": t["type"], "param": r["key"],
                               "label": r.get("label") or r["key"], "value": v, "default": r.get("default"),
                               "direction": direction, "answer": f"{t['type']}.{r['key']} {direction}",
                               "source": sname, "patch": patch(spec, t["type"]), "change": b,
                               "change_text": describe_change(b), "before": before, "after": after,
                               "before_png": before["file"], "after_png": after["file"],
                               "commit": commit}
                        fo.write(json.dumps(row) + "\n")
    print(f"[rows] {n_kept} rows with a measurable change, of {n_all} rendered moves "
          f"({n_all - n_kept} dropped: no fact moved)")


# ---- split ---------------------------------------------------------------------------------------
def stage_split(a, types):
    rows = [json.loads(l) for l in open(os.path.join(a.out, "rows.jsonl"))]
    rng = random.Random(SPLIT_SEED)
    tnames = sorted({r["type"] for r in rows})
    held_types = sorted(rng.sample(tnames, 4))
    seen = [t for t in tnames if t not in held_types]
    held_params = []
    for t in seen:
        ps = sorted({r["param"] for r in rows if r["type"] == t})
        if len(ps) >= 3:  # a type keeps at least two seen params
            held_params += [f"{t}.{p}" for p in rng.sample(ps, max(1, round(len(ps) * 0.15)))]
    held_params.sort()
    # in-distribution test: two whole sources, seen params only
    test_sources = sorted(rng.sample(sorted(SOURCES), 2))
    split = {"seed": SPLIT_SEED, "held_types": held_types, "held_params": held_params, "test_sources": test_sources,
             "ids": {"train": [], "valid": [], "test_seen": [], "test_params": [], "test_types": []}}
    for r in rows:
        tp = f"{r['type']}.{r['param']}"
        if r["type"] in held_types:
            k = "test_types"
        elif tp in held_params:
            k = "test_params"
        elif r["source"] in test_sources:
            k = "test_seen"
        else:
            k = "valid" if rng.random() < 0.08 else "train"
        split["ids"][k].append(r["id"])
    body = json.dumps(split["ids"], sort_keys=True).encode()
    split["sha256"] = hashlib.sha256(body).hexdigest()
    with open(os.path.join(a.out, "split.json"), "w") as f:
        json.dump(split, f, indent=1)
    print("[split] " + ", ".join(f"{k} {len(v)}" for k, v in split["ids"].items()))
    print(f"[split] held-out types {held_types}; {len(held_params)} held-out params; sha256 {split['sha256'][:16]}")


def main():
    global FAMILY
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/e1"))
    ap.add_argument("--describe", help="describe.json (default OUT/describe.json, asked from the app if missing)")
    ap.add_argument("--coverage", help="coverage.json: only effective/conditional params (the claimed family)")
    ap.add_argument("--procs", type=int, default=3)
    ap.add_argument("--chunk", type=int, default=150)
    ap.add_argument("--budget", type=float, help="minutes; stop starting chunks after this (re-run resumes)")
    ap.add_argument("--stage", default="all", choices=["render", "rows", "split", "all"])
    ap.add_argument("--family", default=FAMILY, help="node category to sweep (E1 itself is Effects)")
    ap.add_argument("--types", help="comma list: only these types of the family")
    a = ap.parse_args()
    FAMILY = a.family
    only = set(a.types.split(",")) if a.types else None
    os.makedirs(a.out, exist_ok=True)
    schema = a.describe or os.path.join(a.out, "describe.json")
    if not os.path.exists(schema):
        subprocess.run([a.bin, "--describe", "--json", schema], capture_output=True)
    types = [fill_filter_menus(t) for t in json.load(open(schema))["types"]
             if t["category"] == FAMILY and t["type"] not in SKIP_TYPES and (only is None or t["type"] in only)]
    a.cov = {r["type"]: r for r in json.load(open(a.coverage))} if a.coverage else None
    if a.stage in ("render", "all"):
        stage_render(a, types)
    if a.stage in ("rows", "all"):
        stage_rows(a, types)
    if a.stage in ("split", "all"):
        stage_split(a, types)


if __name__ == "__main__":
    main()
