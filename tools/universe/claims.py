#!/usr/bin/env python3
"""Claims registry (EXECUTION.md "Claims registry"): frozen test sets, stored predictions, scores.

    claims.py freeze  C1|C2|C3 [--engines oscillator,analog]   draw and write the test set once; refuses to overwrite
    claims.py predict C2 --model <name> --file preds.json     store predictions (a hash and time) before any render
    claims.py score   C2 --file preds.json                    measured change vs the stored prediction -> pass / miss
    claims.py table                                           today's state of C1-C7 (for HANDOFF.md)
    claims.py check                                           the frozen file's hashes still match; no test id in a training split

State lives in ~/Infinite/training-data/eval/splits/claims.json (frozen sets) and eval/predictions/. Draws are seeded and depend
only on names, so a rerun of freeze on the same data gives the same set; once written a set is never replaced."""
import argparse, glob, hashlib, json, os, random, sys, time, zlib
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sound_data as sd  # noqa: E402

ROOT = os.path.expanduser("~/Infinite/training-data")
SPLITS = os.path.join(ROOT, "eval/splits/claims.json")
PREDS = os.path.join(ROOT, "eval/predictions")
AUDIO = os.path.join(ROOT, "audio")
REGISTRY = {
    "C1": ("understands each characteristic axis", "R2 per axis mean >= 0.6 on the group split", "3, 4"),
    "C2": ("knows what each ingredient does", "direction right >= 90%; size r >= 0.7 on 200 moves", "4"),
    "C3": ("knows what a good sound is", "AUC >= 0.8 on 1,000 map + 1,000 random; owner agrees >= 30 / 40", "5"),
    "C4": ("understands characteristics in words", "AUC >= 0.75 per word; >= 70% of 50 made sounds", "5, 6"),
    "C5": ("understands moods and emotions", "r >= 0.8 per dimension; >= 80% in region", "P6"),
    "C6": ("is creative", "N < 0.1 at c = 0; N and R rise with c", "5, 6"),
    "C7": ("can make a sound", "closer than the nearest render on 300 targets", "6"),
}


def load():
    return json.load(open(SPLITS)) if os.path.exists(SPLITS) else {}


def save(d):
    os.makedirs(os.path.dirname(SPLITS), exist_ok=True)
    json.dump(d, open(SPLITS, "w"), indent=1, sort_keys=True)


def sha(obj):
    return hashlib.sha1(json.dumps(obj, sort_keys=True).encode()).hexdigest()[:16]


def own_files(engines):
    fs = sorted(glob.glob(os.path.join(AUDIO, "own_*.jsonl")))
    return [f for f in fs if not engines or os.path.basename(f)[4:-6] in engines]


def own_rows(path):
    """{preset_id: first row (the one with recipe / knobs)}"""
    out = {}
    for ln in open(path):
        if '"knobs"' in ln:
            r = json.loads(ln); out[r["preset_id"]] = r
    return out


def freeze_c1(engines):
    out = {}
    for f in own_files(engines):
        d = sd.load(f); s = sd.split(d)
        out[os.path.basename(f)] = {"group": sorted(d["preset_id"][s["group"]].tolist()), "random": sorted(d["preset_id"][s["random"]].tolist())}
    dv = os.path.join(AUDIO, "diva_m1.jsonl")
    if os.path.exists(dv):
        d = sd.load(dv); s = sd.split(d)
        out["diva_m1.jsonl"] = {"group": sorted(d["preset_id"][s["group"]].tolist())}
    return out


def freeze_c2(engines, n=100, seed=7):
    """Moves inside held-out banks: (variant, base) where the variant differs from its base in exactly 1 knob (single) or 2-3 (combined)."""
    out = {}
    for f in own_files(engines):
        d = sd.load(f); held = set(d["preset_id"][sd.split(d)["group"]].tolist()); rows = own_rows(f)
        banks = {}
        for pid in held:
            r = rows.get(pid)
            if r:
                banks.setdefault(r["tags"]["bank"], []).append(r)
        singles, combos = [], []
        for rs in banks.values():
            bases = [r for r in rs if r["tags"]["design"] == "base"]
            for v in rs:
                if v["tags"]["design"] == "base":
                    continue
                best = min(bases, key=lambda b: sum(b["recipe"][k] != v["recipe"][k] for k in v["recipe"]), default=None)
                if best is None:
                    continue
                diff = [k for k in v["recipe"] if best["recipe"][k] != v["recipe"][k]]
                (singles if len(diff) == 1 else combos if len(diff) > 1 else []).append({"variant": v["preset_id"], "base": best["preset_id"], "keys": diff})
        rng = random.Random("%s/%d" % (f, seed)); rng.shuffle(singles); rng.shuffle(combos)
        out[os.path.basename(f)] = {"single": singles[:n], "combined": combos[:n]}
    return out


def freeze_c3(engines, n=1000, seed=7):
    """Random renders: base-design presets of held-out banks. Map rows: Diva held-out banks, then the sample library by seeded draw."""
    out = {"random": {}, "map": {}}
    for f in own_files(engines):
        d = sd.load(f); held = d["preset_id"][sd.split(d)["group"]].tolist(); rows = own_rows(f)
        ids = sorted(p for p in held if rows.get(p, {}).get("tags", {}).get("design") == "base")
        random.Random("%s/%d" % (f, seed)).shuffle(ids); out["random"][os.path.basename(f)] = ids[:n]
    dv = os.path.join(AUDIO, "diva_m1.jsonl")
    if os.path.exists(dv):
        d = sd.load(dv); ids = sorted(d["preset_id"][sd.split(d)["group"]].tolist()); random.Random(seed).shuffle(ids)
        out["map"]["diva_m1.jsonl"] = ids[:n]
    ft = os.path.join(AUDIO, "features.jsonl")
    if os.path.exists(ft):
        srcs = []
        for ln in open(ft):
            if zlib.crc32(ln[:200].encode()) % 5 == 0:  # held-out fifth by name, stable as the file grows
                try:
                    srcs.append(json.loads(ln)["src"])
                except ValueError:
                    pass
        random.Random(seed).shuffle(srcs); out["map"]["features.jsonl"] = srcs[:n]
    for k, v in out["random"].items():          # one key per engine so an engine added later can be frozen on its own
        out["random:" + k] = v
    return out


FREEZE = {"C1": freeze_c1, "C2": freeze_c2, "C3": freeze_c3}


def cmd_freeze(a):
    d = load(); eng = [e for e in a.engines.split(",") if e]
    new = FREEZE[a.claim](eng)
    c = d.setdefault(a.claim, {"frozen": time.strftime("%Y-%m-%d %H:%M"), "sets": {}})
    added = 0
    for k, v in new.items():
        if k in c["sets"]:
            print("kept   %s %s (frozen %s)" % (a.claim, k, c["sets"][k]["sha"]))
            continue
        c["sets"][k] = {"sha": sha(v), "data": v}; added += 1
        print("frozen %s %s sha %s" % (a.claim, k, c["sets"][k]["sha"]))
    save(d); print("%d new sets" % added)


def cmd_check(_a):
    ok = True
    for c, v in load().items():
        for k, s in v["sets"].items():
            if sha(s["data"]) != s["sha"]:
                print("BAD  %s %s: hash changed" % (c, k)); ok = False
    print("claims.json: %s" % ("all hashes match" if ok else "CHANGED"))
    return 0 if ok else 1


def cmd_predict(a):
    os.makedirs(PREDS, exist_ok=True)
    body = json.load(open(a.file)); rec = {"claim": a.claim, "model": a.model, "time": time.strftime("%Y-%m-%d %H:%M:%S"), "pred": body, "sha": sha(body)}
    out = os.path.join(PREDS, "%s_%s_%s.json" % (a.claim, a.model, rec["sha"])); json.dump(rec, open(out, "w")); print("stored", out)


def axis_values(row):
    import characteristics as ch
    r = ch.from_row(row)
    return {k: v["mean"] for k, v in (r["axes"] or {}).items() if "mean" in v}      # a silent or failed render has no axes: no moves from it


def cmd_score(a):
    """C2: pred = {variant_id: {axis: predicted change}}; measured change = variant minus base, both at window C3."""
    assert a.claim == "C2", "only C2 is scored from this file; C1 / C3 use p3_characteristics and critic.py"
    rec = json.load(open(a.file)); pred = rec.get("pred", rec); frozen = load()["C2"]["sets"]
    dirs, P, M = [], [], []
    for f, sets in frozen.items():
        rows = {}
        for ln in open(os.path.join(AUDIO, f)):
            r = json.loads(ln)
            if r["note"] == "C3":
                rows[r["preset_id"]] = r
        for m in sets["data"]["single"] + sets["data"]["combined"]:
            if m["variant"] not in pred or m["variant"] not in rows or m["base"] not in rows:
                continue
            va, ba = axis_values(rows[m["variant"]]), axis_values(rows[m["base"]])
            for ax, p in pred[m["variant"]].items():
                if ax in va and ax in ba:
                    ch = va[ax] - ba[ax]; sc = np.std([v for v in (va[ax], ba[ax])]) or 1
                    if abs(ch) > 1e-6:
                        dirs.append((ch > 0) == (p > 0)); P.append(p); M.append(ch)
    if not dirs:
        print("no scored moves"); return 1
    r = float(np.corrcoef(P, M)[0, 1]) if len(P) > 2 else float("nan")
    print("C2: %d axis moves, direction right %.1f%% (bar 90), size r %.3f (bar 0.7) -> %s" %
          (len(dirs), 100 * np.mean(dirs), r, "PASS" if np.mean(dirs) >= 0.9 and r >= 0.7 else "MISS"))
    big = np.abs(np.array(M)) >= 0.5 * np.std(M)     # exploratory (not the frozen bar): moves that changed the axis noticeably
    print("    exploratory: moves with |change| >= 0.5 sd (%d): direction right %.1f%%, size r %.3f" %
          (big.sum(), 100 * np.mean(np.array(dirs)[big]), float(np.corrcoef(np.array(P)[big], np.array(M)[big])[0, 1])))


def cmd_table(_a):
    d = load()
    print("| # | claim | bar | step | frozen | test set |\n|---|---|---|---|---|---|")
    for c, (name, bar, step) in REGISTRY.items():
        v = d.get(c)
        print("| %s | %s | %s | %s | %s | %s |" % (c, name, bar, step, v["frozen"] if v else "no",
              ", ".join("%s %s" % (k, s["sha"]) for k, s in v["sets"].items()) if v else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    f = sp.add_parser("freeze"); f.add_argument("claim", choices=list(FREEZE)); f.add_argument("--engines", default="")
    p = sp.add_parser("predict"); p.add_argument("claim"); p.add_argument("--model", required=True); p.add_argument("--file", required=True)
    s = sp.add_parser("score"); s.add_argument("claim"); s.add_argument("--file", required=True)
    sp.add_parser("table"); sp.add_parser("check")
    a = ap.parse_args()
    sys.exit({"freeze": cmd_freeze, "predict": cmd_predict, "score": cmd_score, "table": cmd_table, "check": cmd_check}[a.cmd](a) or 0)


if __name__ == "__main__":
    main()
