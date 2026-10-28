#!/usr/bin/env python3
"""C3 owner listening half: the critic's 20 top and 20 bottom picks of the frozen C3 test set, as 40 blind clips.
    listen_c3.py pick      score the frozen C3 test set, write eval/listen/c3_picks.json (the key, never shown to the listener)
    listen_c3.py build     cut each pick to a 6 s clip, shuffle, write eval/listen/c3/ (clip_01..40.wav + index.html)
    listen_c3.py score FILE  agreement of the owner's answers (the page's downloaded JSON) with the critic's side
Pass bar (EXECUTION.md C3): the owner agrees with the critic on >= 30 of 40."""
import argparse, json, os, random, subprocess, sys, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sound_space as ss
import claims as cl

OUT = os.path.expanduser("~/Infinite/training-data/eval/listen")


def pool(engines):
    """Held-out C3 test rows with their critic score and a way to hear them: (kind, id/src, vector)."""
    fz = cl.load()["C3"]
    eng = [e for e in engines.split(",") if e]
    items, tr_rand = [], []
    for f in cl.own_files(eng):
        name = os.path.basename(f); X, I, D = ss.own_matrix(f)
        held = set(fz["sets"].get("random", {}).get("data", {}).get(name, []))
        for x, i, dsn in zip(X, I, D):
            if i in held: items.append(("random", "%s:%s" % (name, i), x))
            elif dsn in ("base", "uniform", "propose"): tr_rand.append(x)
    ids = set(fz["sets"]["map"]["data"].get("diva_m1.jsonl", []))
    held_src = set(fz["sets"]["map"]["data"].get("features.jsonl", []))
    srcs = []
    for ln in open(os.path.join(ss.AUDIO, "features.jsonl")):
        r = json.loads(ln)
        if r["src"] in held_src and os.path.exists(r["src"]):
            v = ss.vec_from_row(r)
            if v is not None: items.append(("sample", r["src"], v))
    dm, _, _ = ss.diva_matrix_excluding(ids), 0, 0
    trs, _ = ss.sample_matrix(None, 4000)
    train_map = np.vstack([m for m in (dm, trs) if len(m)])
    z = ss.prep(train_map, np.array(tr_rand))
    f = ss.logit(z(np.vstack([train_map, tr_rand])), np.r_[np.ones(len(train_map)), np.zeros(len(tr_rand))])
    s = f(z(np.array([it[2] for it in items])))
    return [(k, ref, float(sc)) for (k, ref, _), sc in zip(items, s)]


def cmd_pick(a):
    P = [t for t in pool(a.engines) if t[0] == "sample"]  # real sounds only: top vs bottom with no synth-vs-sample cue
    P.sort(key=lambda t: -t[2])
    top, bot = P[:20], P[-20:]
    os.makedirs(OUT, exist_ok=True)
    json.dump({"top": top, "bottom": bot, "pool": len(P)}, open(os.path.join(OUT, "c3_picks.json"), "w"), indent=1)
    for nm, grp in (("top", top), ("bottom", bot)):
        mix = {k: sum(1 for g in grp if g[0] == k) for k in ("sample", "random")}
        print(nm, mix, "score range %.2f..%.2f" % (grp[-1][2], grp[0][2]))


PAGE = """<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Sound check</title><style>body{font:16px system-ui;max-width:640px;margin:24px auto;padding:0 16px}
.r{display:flex;gap:8px;align-items:center;margin:10px 0;padding:8px;border:1px solid #8884;border-radius:8px;flex-wrap:wrap}
audio{flex:1;min-width:220px}button{padding:6px 12px;border-radius:6px;border:1px solid #888;background:none;color:inherit;font:inherit}
button.on{background:#2a7;color:#fff}button.no.on{background:#c54}</style>
<h2>Is this a good sound?</h2><p>40 clips, 6 s each, volume matched. Judge the sound itself: would you want it in your music? Answer for each, then press Save.</p>
<div id=l></div><p><button onclick=save()>Save answers</button> <span id=m></span></p>
<script>const N=__N__,A={};const l=document.getElementById('l');
for(let i=1;i<=N;i++){const id='clip_'+String(i).padStart(2,'0');const d=document.createElement('div');d.className='r';
d.innerHTML='<b>'+i+'</b><audio controls preload=none src="'+id+'.wav"></audio><button data-v=good>Good</button><button class=no data-v=not>Not good</button>';
d.querySelectorAll('button').forEach(b=>b.onclick=()=>{A[id]=b.dataset.v;d.querySelectorAll('button').forEach(x=>x.classList.toggle('on',x===b))});l.append(d)}
function save(){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(A)],{type:'application/json'}));a.download='c3_answers.json';a.click();
document.getElementById('m').textContent=Object.keys(A).length+' of '+N+' answered'}</script>"""


def cmd_build(a):
    pk = json.load(open(os.path.join(OUT, "c3_picks.json")))
    items = [("top", t) for t in pk["top"]] + [("bottom", t) for t in pk["bottom"]]
    random.Random(7).shuffle(items)
    d = os.path.join(OUT, "c3"); os.makedirs(d, exist_ok=True); key = {}
    for n, (side, (kind, src, sc)) in enumerate(items, 1):
        cid = "clip_%02d" % n
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-t", "6", "-ac", "2", "-ar", "44100",
                        "-af", "loudnorm=I=-20:TP=-2:LRA=11", os.path.join(d, cid + ".wav")], check=True)
        key[cid] = {"side": side, "kind": kind, "src": src, "score": sc}
    json.dump(key, open(os.path.join(OUT, "c3_key.json"), "w"), indent=1)
    open(os.path.join(d, "index.html"), "w").write(PAGE.replace("__N__", str(len(items))))
    print("built %d clips in %s (key in c3_key.json, not shown)" % (len(items), d))


def cmd_score(a):
    key = json.load(open(os.path.join(OUT, "c3_key.json")))
    ans = json.load(open(a.file))
    agree = n = 0
    by = {}
    for cid, say in ans.items():
        if say not in ("good", "not"): continue
        k = key[cid]; n += 1
        hit = (say == "good") == (k["side"] == "top"); agree += hit
        b = by.setdefault(k["kind"], [0, 0]); b[0] += hit; b[1] += 1
    print("owner agrees with the critic on %d of %d (bar 30 of 40) -> %s" % (agree, n, "PASS" if agree >= 30 and n == 40 else "MISS/incomplete"))
    for k, (h, t) in by.items(): print("  %-7s %d of %d" % (k, h, t))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["pick", "build", "score"]); ap.add_argument("file", nargs="?")
    ap.add_argument("--engines", default="oscillator,analog")
    a = ap.parse_args()
    {"pick": cmd_pick, "build": cmd_build, "score": cmd_score}[a.cmd](a)
