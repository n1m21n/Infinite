#!/usr/bin/env python3
"""F5: judge atlas rows, pick a diverse high-scoring set, publish them as .inf files.
    judge.py atlas1.jsonl atlas2.jsonl ... --n 200 --out lib/ --bin <Infinite>
Steps: score (cheap measured features) -> greedy farthest-point selection with a per-node cap ->
compile -> lay out (tools/patch-layout.py) -> --validate -> re-probe for determinism -> write .inf + manifest.jsonl.
The genome goes in a `# genome:` comment so a patch can be traced back to the universe."""
import argparse, json, math, os, random, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u
import visual as v
import combined as c

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def kind_of(code):
    if "##" in code:
        return "combined"
    return "visual" if code.lstrip("[ ").startswith("I") else "audio"


def compile_any(code, facts, P, VP):
    k = kind_of(code)
    if k == "combined":
        return c.compile_combined(code, facts, P, VP)
    return v.compile_visual(code, facts, VP) if k == "visual" else u.compile_inf(code, facts, P)


def band(x, lo, hi, soft):
    return 1.0 if lo <= x <= hi else max(0.0, 1 - (lo - x if x < lo else x - hi) / soft)


def audio_score(a):
    l = a.get("integrated_lufs")
    if l is None:
        return 0.0
    s = band(l, -30, -10, 15) * (1 - min(1, a.get("clipped_percent", 0) / 5))
    s *= band(a.get("silence_ratio", 1), 0, 0.6, 0.4)
    s *= 0.6 + 0.4 * min(1, a.get("onset_count_estimate", 0) / 8)
    return s


def visual_score(r):
    st = r.get("stats") or []
    if not st:
        return 0.0
    contrast = max(s["max_luma"] - s["min_luma"] for s in st)
    mean = sum(s["mean_luma"] for s in st) / len(st)
    return band(mean, 0.05, 0.75, 0.3) * min(1, contrast / 0.5) * (1.0 if r.get("moves") else 0.7)


def score(r):
    k = kind_of(r["code"])
    if k == "audio":
        return audio_score(r.get("a", {}))
    if k == "visual":
        return visual_score(r)
    return math.sqrt(audio_score(r.get("a", {})) * visual_score(r))


def vec(r):
    a, st = r.get("a", {}), r.get("stats") or [{}]
    return [(a.get("integrated_lufs") or -60) / 30, a.get("silence_ratio", 1), math.log1p(a.get("onset_count_estimate", 0)) / 4,
            math.log((a.get("loudest_band_hz") or 100) + 1) / 10, a.get("stereo_correlation", 1) if isinstance(a.get("stereo_correlation"), (int, float)) else 1,
            st[0].get("mean_luma", 0), st[0].get("black_percent", 0) / 100, 1.0 if r.get("moves") else 0.0]


def dist(r1, r2):
    d = math.dist(vec(r1), vec(r2))
    n1, n2 = set(r1["names"]), set(r2["names"])
    return d + 1.5 * (1 - len(n1 & n2) / max(1, len(n1 | n2)))


def select(rows, n, cap_share=0.08, floor=0.35, seed=1):
    rows = [r for r in rows if r["class"] == "ok" and score(r) >= floor]
    rows.sort(key=score, reverse=True)
    seen, uniq = set(), []
    for r in rows:
        if r["code"] not in seen:
            seen.add(r["code"]); uniq.append(r)
    chosen, use = [], {}
    cap = max(3, int(cap_share * n))
    pool = uniq[: n * 12]
    random.Random(seed).shuffle(pool)
    mind = [1e9] * len(pool)
    while pool and len(chosen) < n:
        j = max(range(len(pool)), key=lambda i: mind[i] * (0.5 + score(pool[i])))
        r = pool.pop(j); mind.pop(j)
        if any(use.get(x, 0) >= cap for x in set(r["names"])):
            continue
        chosen.append(r)
        for x in set(r["names"]):
            use[x] = use.get(x, 0) + 1
        mind = [min(m, dist(r, p)) for m, p in zip(mind, pool)]
    return chosen


def publish(r, i, outdir, facts, P, VP, binary):
    text = compile_any(r["code"], facts, P, VP)
    name = "u%04d" % i
    path = os.path.join(outdir, name + ".inf")
    open(path, "w").write(text)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "patch-layout.py"), path], capture_output=True)
    body = open(path).read()
    open(path, "w").write("# genome: %s\n%s" % (r["code"], body))
    val = subprocess.run([binary, "--validate", path], capture_output=True, text=True)
    if val.returncode:
        os.remove(path)
        return None
    k = kind_of(r["code"])
    again = (v.probe(r["code"], facts, VP, binary, tempfile.gettempdir()) if k == "visual"
             else c.probe(r["code"], facts, P, VP, binary, tempfile.gettempdir()) if k == "combined"
             else u.probe(r["code"], facts, P, binary, 4, tempfile.gettempdir()))
    stable = again["class"] == "ok" or again["class"] == r["class"]
    return {"file": name + ".inf", "kind": k, "genome": r["code"], "score": round(score(r), 3), "nodes": r["names"],
            "class": r["class"], "recheck": again["class"], "stable": stable}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atlas", nargs="+")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--out", default="library")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    a = ap.parse_args()
    facts = u.load_facts(); P, VP = u.pools(facts), v.vpools(facts)
    rows = []
    for p in a.atlas:
        for l in open(p):
            r = json.loads(l)
            if r.get("names") and not r["class"].startswith(("error", "classify")):
                rows.append(r)
            elif not r.get("names") and "code" in r:
                try:
                    r["names"] = (u.node_names(r["code"], P) if kind_of(r["code"]) == "audio" else v.u.node_names(r["code"], VP)); rows.append(r)
                except Exception:
                    pass
    picked = select(rows, a.n)
    print("candidates %d  picked %d" % (len(rows), len(picked)))
    os.makedirs(a.out, exist_ok=True)
    kept = 0
    with open(os.path.join(a.out, "manifest.jsonl"), "w") as mf:
        for i, r in enumerate(picked):
            m = publish(r, i, a.out, facts, P, VP, a.bin)
            if m and m["stable"]:
                mf.write(json.dumps(m) + "\n"); kept += 1
            elif m:
                os.remove(os.path.join(a.out, m["file"]))
    print("published %d of %d (validated, laid out, stable on re-probe) -> %s" % (kept, len(picked), a.out))


if __name__ == "__main__":
    main()
