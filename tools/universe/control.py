#!/usr/bin/env python3
"""Step 5 of the review: is a model-chosen round better than the same number of random renders?   (system python builds the queue; ml-venv scores)

    control.py queue --node Analog --round r01     random queue with the SAME size and the same kind mix as the model-chosen one (one-knob / pair / fresh),
                                                   points drawn with the same edge-biased draw, base = the renders that existed before the round
    control.py files --node Analog --round r01 --exclude r01,reach1,reach2
                                                   writes ~/Infinite/training-data/audio/pre_<engine>.jsonl (rows not in those rounds = the base)
                                                   and extra_<engine>_chosen.jsonl (rows of the chosen round(s)); the random rows are rendered with
                                                   INF_OUT_TAG=_ctl into own_<engine>_ctl.jsonl, then split out as extra_<engine>_random.jsonl by `control.py split`
    Compare: p3_characteristics.py pre_<e>.jsonl --extra extra_<e>_chosen.jsonl   vs   --extra extra_<e>_random.jsonl   (same test banks, same count)."""
import argparse, json, os, random, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as o  # noqa: E402

AUD = o.OUT_DIR


def ids_of(node, sw, fixed, rounds):
    s = set()
    for r in rounds:
        q = json.load(open(os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (node.lower(), r))))
        s |= {o.rid(node, o.recipe(node, sw, fixed, it["u"])) for it in q["items"]}
    return s


def cmd_queue(a):
    import numpy as np, sound_data as sd
    table, sw, fixed = o.param_table(a.node)
    q = json.load(open(os.path.join(o.CACHE_DIR, "queue_%s_%s.json" % (a.node.lower(), a.round))))
    drop = ids_of(a.node, sw, fixed, a.exclude.split(",")) if a.exclude else set()
    d = sd.load(o.out_path(a.node)); keep = [i for i, p in enumerate(d["preset_id"]) if d["ok"][i, 0] and p not in drop]
    rendered = [list(map(float, d["knobs"][i])) for i in keep]
    rng = random.Random("control/%s/%s" % (a.node, a.round)); draw = lambda: rng.betavariate(.5, .5) if rng.random() < .5 else rng.random()
    kinds = [it["kind"] for it in q["items"] if it["design"] == "propose"]; n_u = sum(it["design"] == "uniform" for it in q["items"])
    items = []
    for k in kinds:
        if k == "fresh": u = [draw() for _ in sw]
        else:
            u = list(rng.choice(rendered))
            for i in rng.sample(range(len(sw)), 1 if k == "one" else rng.choice((2, 3))): u[i] = draw()
        items.append({"u": [round(x, 4) for x in u], "design": "random", "kind": k})
    for _ in range(n_u): items.append({"u": [round(rng.random(), 4) for _ in sw], "design": "random", "kind": "fresh"})
    out = os.path.join(o.CACHE_DIR, "queue_%s_%sctl.json" % (a.node.lower(), a.round)); json.dump({"engine": a.node, "round": a.round + "ctl", "items": items}, open(out, "w"))
    print("wrote %s: %d items (base %d renders)" % (out, len(items), len(rendered)))


def cmd_files(a):
    table, sw, fixed = o.param_table(a.node); sel = ids_of(a.node, sw, fixed, [a.round]); gone = ids_of(a.node, sw, fixed, a.exclude.split(",")) - sel; src = o.out_path(a.node); e = a.node.lower()
    pre = open(os.path.join(AUD, "pre_%s.jsonl" % e), "w"); ch = open(os.path.join(AUD, "extra_%s_chosen.jsonl" % e), "w"); n = [0, 0]
    for ln in open(src):
        pid = json.loads(ln)["preset_id"]
        if pid in gone: continue                      # later rounds (reach...) belong to neither arm
        (ch if pid in sel else pre).write(ln); n[pid in sel] += 1
    print("pre_%s: %d lines, chosen: %d lines" % (e, n[0], n[1]))


def cmd_split(a):
    e = a.node.lower(); src = os.path.join(AUD, "own_%sctl.jsonl" % e) if False else os.path.join(AUD, "own_%s_ctl.jsonl" % e)
    with open(os.path.join(AUD, "extra_%s_random.jsonl" % e), "w") as f:
        n = sum(f.write(ln) or 1 for ln in open(src))
    print("extra_%s_random: %d lines" % (e, n))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["queue", "files", "split"]); ap.add_argument("--node", required=True)
    ap.add_argument("--round", default="r01"); ap.add_argument("--rounds", default="r01,reach1,reach2"); ap.add_argument("--exclude", default="r01,reach1,reach2")
    a = ap.parse_args(); {"queue": cmd_queue, "files": cmd_files, "split": cmd_split}[a.cmd](a)
