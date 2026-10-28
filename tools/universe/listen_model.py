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


MIDI = os.path.expanduser("~/Infinite/datasets/midi/data/bach_chorales/valid/chorale_valid_068.mid")


def midi_events(path, secs=12.0):
    """First `secs` seconds of a MIDI file as (time, pitch, velocity 0-1, length) note events."""
    import mido
    t = 0.0; on = {}; ev = []
    for m in mido.MidiFile(path):
        t += m.time
        if t > secs: break
        if m.type == "note_on" and m.velocity > 0: on[m.note] = (t, m.velocity)
        elif m.type in ("note_off", "note_on") and m.note in on:
            t0, v = on.pop(m.note); ev.append((t0, m.note, v / 127.0, max(0.05, t - t0)))
    return sorted(ev)


def context_patch(node, tags, rec):
    """The engine settings played through a Reverb (a bare held note through a dry synth is not a fair test)."""
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Synths " + node, "  id syn"]
    for k, v in rec.items():
        tg = tags.get(k, "f")
        L.append("  %s %s %s" % (tg, k, int(v) if tg in ("i", "b") else repr(float(v))))
    L += ["end", "node 3 AudioEffects Reverb", "  id rev", "end", "node 4 Utility Audio Out", "  id out", "end",
          "aud rev audio syn", "aud out audio rev", "note syn 0 keys"]
    return "\n".join(L) + "\n"


def render_context(binp, node, tags, recs, tmp, dur=12):
    ev = midi_events(MIDI, dur)
    types = {"p%d" % n: {"node": "syn", "pitch": n, "velocity_from": "v", "length_from": "len"} for n in {e[1] for e in ev}}
    ep, mp_ = os.path.join(tmp, "events.json"), os.path.join(tmp, "map.json")
    json.dump([{"t": round(t, 4), "type": "p%d" % n, "v": round(v, 4), "len": round(l, 4)} for t, n, v, l in ev], open(ep, "w")); json.dump(types, open(mp_, "w"))
    out = []
    for k, rec in enumerate(recs):
        inf, js, wav = (os.path.join(tmp, "c%d.%s" % (k, e)) for e in ("inf", "json", "wav"))
        open(inf, "w").write(context_patch(node, tags, rec))
        subprocess.run([binp, "--audio-summary", inf, js, "--duration", str(dur), "--sample-rate", "44100", "--wav", wav, "--lenient",
                        "--notes", ep, "--note-map", mp_], capture_output=True, timeout=180)
        out.append(wav if os.path.exists(wav) and os.path.getsize(wav) > 1000 else None)
    return out


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
        wavs = render_context(ow.binary(), node, tags, recs, tmp) if a.context else ow.render_batch(ow.binary(), node, tags, recs, tmp)
        for n, k in enumerate(order, 1):
            if wavs[k] is None: continue
            cid = "clip_%02d" % n
            subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", wavs[k], "-t", "6", "-ac", "2", "-ar", "44100",
                            "-af", "loudnorm=I=-20:TP=-2:LRA=11", os.path.join(d, cid + ".wav")], check=True)
            key[cid] = {"side": sides[k][0], "node": node, "u": sides[k][1]}
    if a.context:  # the site generator's own samples, as the reference bar
        import glob
        for src in sorted(glob.glob(os.path.expanduser("~/Infinite/patch-universe-library/samples/audio/*/sample.wav")))[:a.site]:
            cid = "clip_%02d" % (len(key) + 1)
            subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-t", "6", "-ac", "2", "-ar", "44100",
                            "-af", "loudnorm=I=-20:TP=-2:LRA=11", os.path.join(d, cid + ".wav")], check=True)
            key[cid] = {"side": "site", "src": src}
    ids = sorted(key); perm = ids[:]; random.Random(5).shuffle(perm)  # site clips were appended last: shuffle every clip
    for c in ids: os.rename(os.path.join(d, c + ".wav"), os.path.join(d, c + ".tmp"))
    key = {n: key[o] for n, o in zip(ids, perm)}
    for n, o in zip(ids, perm): os.rename(os.path.join(d, o + ".tmp"), os.path.join(d, n + ".wav"))
    json.dump(key, open(os.path.join(OUT, "model_%s_key.json" % a.tag), "w"))
    open(os.path.join(d, "index.html"), "w").write(lc.PAGE.replace("__N__", str(len(key))))
    print("built %d clips in %s" % (len(key), d))


def cmd_score(a):
    key = json.load(open(os.path.join(OUT, "model_%s_key.json" % a.tag))); ans = json.load(open(a.file)); c = {"model": [0, 0], "random": [0, 0], "site": [0, 0]}
    for cid, say in ans.items():
        if cid in key and say in ("good", "not"):
            c[key[cid]["side"]][1] += 1; c[key[cid]["side"]][0] += say == "good"
    for s, (g, t) in c.items(): print("%-7s called good %d of %d" % (s, g, t))
    print("model minus random: %+.2f (above 0 = the model's sounds sound better than random)" % (c["model"][0] / max(1, c["model"][1]) - c["random"][0] / max(1, c["random"][1])))


if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd", choices=["build", "score"]); ap.add_argument("file", nargs="?")
    ap.add_argument("--node", default="Analog"); ap.add_argument("--queue"); ap.add_argument("--n", type=int, default=10); ap.add_argument("--tag", default="m1"); ap.add_argument("--context", action="store_true"); ap.add_argument("--site", type=int, default=4)
    a = ap.parse_args(); {"build": cmd_build, "score": cmd_score}[a.cmd](a)
