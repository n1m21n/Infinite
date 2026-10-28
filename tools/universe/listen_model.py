#!/usr/bin/env python3
"""Owner listening for the model's own sounds: 10 sounds the model proposed (reach plan items) vs 10 uniform-random sounds of
the same engine, rendered now, loudness-matched, shuffled, blind. Repeat on a later model to see if it improves.
    listen_model.py build --node Analog --queue ~/Infinite/training-data/own/queue_analog_reach3.json [--tag m1]
    listen_model.py score ANSWERS.json --tag m1     share called Good for each side, and the gap"""
import argparse, json, os, random, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import own_sweeps as ow
import listen_c3 as lc

OUT = lc.OUT


def cmd_build(a):
    node = a.node; table, sw, fixed = ow.param_table(node); tags = ow.tag_of(table)
    q = json.load(open(os.path.expanduser(a.queue)))
    rng = random.Random(11)
    model = rng.sample([it for it in q["items"] if it.get("kind") == "reach"], a.n)
    nprng = np.random.RandomState(11)
    rand = [list(nprng.rand(len(sw))) for _ in range(a.n)]
    sides = [("model", it["u"]) for it in model] + [("random", u) for u in rand]
    recs = [ow.recipe(node, sw, fixed, u) for _, u in sides]
    order = list(range(len(sides))); rng.shuffle(order)
    d = os.path.join(OUT, "model_" + a.tag); os.makedirs(d, exist_ok=True); key = {}
    with tempfile.TemporaryDirectory(prefix="lm_") as tmp:
        wavs = ow.render_batch(ow.binary(), node, tags, recs, tmp)
        for n, k in enumerate(order, 1):
            if wavs[k] is None: continue
            cid = "clip_%02d" % n
            subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", wavs[k], "-t", "6", "-ac", "2", "-ar", "44100",
                            "-af", "loudnorm=I=-20:TP=-2:LRA=11", os.path.join(d, cid + ".wav")], check=True)
            key[cid] = {"side": sides[k][0], "node": node, "u": sides[k][1]}
    json.dump(key, open(os.path.join(OUT, "model_%s_key.json" % a.tag), "w"))
    open(os.path.join(d, "index.html"), "w").write(lc.PAGE.replace("__N__", str(len(key))))
    print("built %d clips in %s" % (len(key), d))


def cmd_score(a):
    key = json.load(open(os.path.join(OUT, "model_%s_key.json" % a.tag))); ans = json.load(open(a.file)); c = {"model": [0, 0], "random": [0, 0]}
    for cid, say in ans.items():
        if cid in key and say in ("good", "not"):
            c[key[cid]["side"]][1] += 1; c[key[cid]["side"]][0] += say == "good"
    for s, (g, t) in c.items(): print("%-7s called good %d of %d" % (s, g, t))
    print("model minus random: %+.2f (above 0 = the model's sounds sound better than random)" % (c["model"][0] / max(1, c["model"][1]) - c["random"][0] / max(1, c["random"][1])))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["build", "score"]); ap.add_argument("file", nargs="?")
    ap.add_argument("--node", default="Analog"); ap.add_argument("--queue"); ap.add_argument("--n", type=int, default=10); ap.add_argument("--tag", default="m1")
    a = ap.parse_args(); {"build": cmd_build, "score": cmd_score}[a.cmd](a)
