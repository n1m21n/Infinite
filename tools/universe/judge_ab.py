#!/usr/bin/env python3
"""Blind A/B packs for the sound judge (internal/plans/generate-samples/judge.md, section 6 pack 0).
    judge_ab.py make  --pack 0 --pairs Piano:4 Wind:3 String:3 --same Piano:_j,_jknock:2   -> ab/pack<N>/index.html + pairs.json
    judge_ab.py score --pack 0 --picks "ABBA..." [--model v0]      owner picks vs judge picks vs hand-axes picks
Pairs are random renders from held-out presets (judge_train.held), so neither judge saw them; A/B sides are shuffled with a seed.
--same plays one preset rendered two ways (old tag vs new tag). The page shows a code to paste back; nothing is sent anywhere.
Judge pick = higher realness for the node's family set; axes pick = smaller distance (timbre axes) to the nearest library sample
of that family set (string_families). Exit 0."""
import argparse, html, json, os, random, re, shutil, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
CL = os.path.expanduser("~/Infinite/training-data/judge/clips")
AB = os.path.join(HERE, "../../internal/plans/generate-samples/ab")
WORD = {"Piano": "piano", "Wind": "wind instrument (clarinet, sax, flute, brass)", "String": "string instrument (guitar, harp, violin, cello)"}


def held(key):
    import zlib
    return (zlib.crc32(key.encode()) % 1000) < 200


def make(a):
    rnd = random.Random(int(a.pack)); out = os.path.join(AB, "pack%s" % a.pack); os.makedirs(out, exist_ok=True); pairs = []
    for spec in a.pairs:
        node, n = spec.split(":"); d = os.path.join(CL, a.tag, node)
        ids = sorted({f.split("_")[0] for f in os.listdir(d) if f.endswith(".wav") and held(f.split("_")[0])}); rnd.shuffle(ids)
        for i in range(int(n)):
            note = rnd.choice(["C3", "C4"]); x, y = ids[2 * i], ids[2 * i + 1]
            pairs.append({"node": node, "kind": "two presets", "a": [a.tag, x, note], "b": [a.tag, y, note]})
    for spec in a.same or []:
        node, tags, n = spec.split(":"); t0, t1 = tags.split(",")
        have = [{f.split("_")[0] for f in os.listdir(os.path.join(CL, t, node)) if f.endswith(".wav")} for t in (t0, t1)]
        ids = sorted(i for i in have[0] & have[1] if held(i)); rnd.shuffle(ids)
        for i in range(int(n)):
            note = rnd.choice(["C3", "C4"]); pairs.append({"node": node, "kind": "old vs new", "new": t1, "a": [t0, ids[i], note], "b": [t1, ids[i], note]})
    rnd.shuffle(pairs); rows = []
    for k, p in enumerate(pairs):
        if rnd.random() < 0.5:
            p["a"], p["b"] = p["b"], p["a"]
        for s in ("a", "b"):
            t, pid, note = p[s]; shutil.copy(os.path.join(CL, t, p["node"], "%s_%s.wav" % (pid, note)), os.path.join(out, "p%02d%s.wav" % (k + 1, s)))
        rows.append('<div class="pair"><div class="q">%d. Which is the better %s?</div>'
                    '<label><input type="radio" name="p%d" value="A" onchange="upd()"> A <audio controls preload="none" src="p%02da.wav"></audio></label>'
                    '<label><input type="radio" name="p%d" value="B" onchange="upd()"> B <audio controls preload="none" src="p%02db.wav"></audio></label></div>'
                    % (k + 1, html.escape(WORD[p["node"]]), k + 1, k + 1, k + 1, k + 1))
    json.dump(pairs, open(os.path.join(out, "pairs.json"), "w"), indent=1)
    page = """<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>A/B pack %s</title>
<style>:root{--bg:#fff;--fg:#1d1d1f;--mut:#6e6e73;--line:#e5e5ea}@media (prefers-color-scheme:dark){:root{--bg:#161618;--fg:#f2f2f7;--mut:#a1a1a6;--line:#2c2c2e}}
body{background:var(--bg);color:var(--fg);font:15px/1.4 -apple-system,system-ui,sans-serif;max-width:640px;margin:24px auto;padding:0 16px}
.pair{border-top:1px solid var(--line);padding:12px 0}.q{margin-bottom:6px}label{display:flex;align-items:center;gap:8px;margin:4px 0}audio{height:32px;flex:1;min-width:0}
#code{font:600 18px ui-monospace,monospace;padding:12px 0;border-top:1px solid var(--line)}p{color:var(--mut)}</style>
<h2>A/B pack %s</h2><p>Headphones. Pick the one you would keep. Paste the code at the bottom back into the chat.</p>%s
<div id="code">Code: </div><script>function upd(){let s="";for(let i=1;i<=%d;i++){const c=document.querySelector('input[name=p'+i+']:checked');s+=c?c.value:"-"}
document.getElementById("code").textContent="Code: "+s}upd()</script>""" % (a.pack, a.pack, "\n".join(rows), len(pairs))
    if a.embed:
        import base64
        for k in range(len(pairs)):
            for sd in ("a", "b"):
                f = "p%02d%s.wav" % (k + 1, sd)
                page = page.replace('src="%s"' % f, 'src="data:audio/wav;base64,%s"' % base64.b64encode(open(os.path.join(out, f), "rb").read()).decode())
    open(os.path.join(out, "index.html"), "w").write(page); print("wrote %s (%d pairs)" % (out, len(pairs)))


def axes_dist(node, clips):
    """Nearest-family-sample distance on the timbre axes for each (tag, preset, note); None when the row is missing."""
    import sound_space as ss, string_families as sf
    fams = {"Piano": ["piano"], "Wind": list(sf.WIND), "String": [f for f in sf.FAM if f != "piano"]}[node]
    pats = {**sf.FAM, **sf.WIND}; keep = [i for i, k in enumerate(ss.AX) if k not in ("loudness", "movement", "density")]
    X, names = ss.sample_matrix(limit=10 ** 6); X = X[:, keep]
    sel = np.array([any(re.search(pats[f], n.lower()) for f in fams) for n in names]); Xf = X[sel]
    rows = {}
    for t in {c[0] for c in clips}:
        for ln in open(os.path.expanduser("~/Infinite/training-data/audio/own_%s%s.jsonl" % (node.lower(), t))):
            r = json.loads(ln); v = ss.vec_from_row(r)
            if v is not None:
                rows[(t, r["preset_id"], r["note"])] = v[keep]
    allm = np.vstack([Xf] + [v[None] for v in rows.values()]); mu, sd = np.nanmean(allm, 0), np.nanstd(allm, 0) + 1e-9
    Z = np.nan_to_num((Xf - mu) / sd)
    return [None if c not in rows else float(np.sqrt(((np.nan_to_num((rows[c] - mu) / sd) - Z) ** 2).sum(1)).min()) for c in clips]


def score(a):
    out = os.path.join(AB, "pack%s" % a.pack); pairs = json.load(open(os.path.join(out, "pairs.json")))
    picks = a.picks.strip().upper(); assert len(picks) == len(pairs), "need %d picks" % len(pairs)
    import torch, judge_train as jt, judge_data as jd
    ck = torch.load(os.path.join(jt.MD, a.model + ".pt"), weights_only=False)
    net = jt.Net(len(ck["tgt"]), len(ck["fams"])).to(jt.DEV); net.load_state_dict(ck["state"])
    pose = {"Piano": ["piano"], "Wind": list(jd.sf.WIND), "String": [f for f in jd.sf.FAM if f != "piano"]}
    res = []
    for k, p in enumerate(pairs):
        mels = []
        for s in ("a", "b"):
            r = jd.af.read_pcm(os.path.join(out, "p%02d%s.wav" % (k + 1, s)), 10); x = jd.onset_cut(r[0]); mels.append(jd.features(x)[0])
        _, _, pr, _ = jt.predict(net, np.stack(mels)); c = [ck["fams"].index(f) for f in pose[p["node"]]]
        real = pr[:, c].max(1); d = axes_dist(p["node"], [tuple(p["a"]), tuple(p["b"])])
        jp = "A" if real[0] > real[1] else "B"; xp = None if None in d else ("A" if d[0] < d[1] else "B")
        res.append({"pair": k + 1, "node": p["node"], "kind": p["kind"], "owner": picks[k], "judge": jp, "axes": xp,
                    "new_side": (None if "new" not in p else "A" if p["a"][0] == p["new"] else "B")})
    v = [r for r in res if r["owner"] in "AB"]
    summ = {"judge_agree": round(np.mean([r["judge"] == r["owner"] for r in v]), 3),
            "axes_agree": round(np.mean([r["axes"] == r["owner"] for r in v if r["axes"]]), 3), "n": len(v),
            "new_preferred": [r["owner"] == r["new_side"] for r in v if r["new_side"]]}
    json.dump({"pairs": res, "summary": summ}, open(os.path.join(out, "score_%s.json" % a.model), "w"), indent=1)
    for r in res:
        print(r)
    print(summ)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["make", "score"]); ap.add_argument("--pack", default="0")
    ap.add_argument("--pairs", nargs="*", default=[]); ap.add_argument("--same", nargs="*"); ap.add_argument("--tag", default="_j")
    ap.add_argument("--picks", default=""); ap.add_argument("--model", default="v0"); ap.add_argument("--embed", action="store_true")
    a = ap.parse_args()
    return make(a) if a.cmd == "make" else score(a)


if __name__ == "__main__":
    sys.exit(main() or 0)
