#!/usr/bin/env python3
"""Style-driven patches: a music style (tools/catalog/styles_music.json) becomes a tempo-locked, in-key patch.
    styled.py audio <style_id|all> [--seed N] [--out DIR] [--bin BIN] [--secs 8]
The style fixes BPM, key, scale, swing, the Drum Sequencer pattern, the bass figure, which synths and effects may appear (ban list first)
and how much the modulators move. Nothing here is random except inside those limits."""
import argparse, json, os, random, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import universe as u
import scene

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
KIT = os.path.join(ROOT, "assets", "drumkits", "infinite-basic")
LANES = ["kick", "snare", "chat", "ohat", "clap", "tom", "htom", "bell"]
KIT_FILES = ["01-kick", "02-snare", "03-closed-hat", "04-open-hat", "05-clap", "06-low-tom", "07-high-tom", "08-bell"]
SCALES = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 3, 5, 7, 9, 11], [0, 2, 3, 5, 7, 9, 10],
          [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10], [0, 1, 3, 5, 6, 8, 10], [0, 2, 4, 7, 9],
          [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10], [0, 2, 4, 6, 8, 10], list(range(12))]
MOD_KEYS = ("cutoff", "position", "res", "mix", "detune", "drive", "warp", "size", "tone", "depth", "rate", "feedback")


def styles():
    return {s["id"]: s for s in json.load(open(os.path.join(ROOT, "tools", "catalog", "styles_music.json")))["styles"]}


def degree_note(deg, root, scale, octave):
    iv = SCALES[scale]
    return 12 * octave + 12 + root + iv[deg % len(iv)] + 12 * (deg // len(iv))


def pick(rng, names, ban, facts, want_kind=None):
    ok = [n for n in names if n not in ban and n in facts]
    return rng.choice(ok) if ok else None


def build(style, seed, facts):
    rng = random.Random("%s:%d" % (style["id"], seed))
    bpm = rng.randint(*style["bpm"])
    root, scale = rng.randrange(12), rng.choice(style["scale"])
    ban = set(style["ban"])
    g = scene.Graph(facts, rng)
    chans = []  # (node, gain dB)

    def chain(src, kind, fx_n):
        end = src
        for _ in range(fx_n):
            n = pick(rng, style["fx"], ban, facts)
            if n is None:
                break
            nid = g.add(n, rng.randrange(1 << 30))
            g.connect(end, nid, "audio")
            end = nid
        return end

    drums = {k: v for k, v in style["drums"].items() if v}
    synth_ids = []
    if drums:
        force = {"steps": ("i", 16), "rate": ("i", 12), "swing": ("f", style["swing"]), "volume": ("f", 0.9), "run": ("b", 1)}
        for i, (lane, f) in enumerate(zip(LANES, KIT_FILES)):
            force["lane%d_path" % i] = ("s", os.path.join(KIT, f + ".wav"))
            force["lane%d_volume" % i] = ("f", 0.85 if lane != "chat" else 0.55)
            pat = drums.get(lane, "")
            for k in range(32):
                c = pat[k % 16] if pat else "."
                force["lane%d_step%d" % (i, k)] = ("f", 1.0 if c == "x" else 0.45 if c == "o" else 0.0)
        d = g.add("Drum Sequencer", rng.randrange(1 << 30), force)
        chans.append((chain(d, "audio", 0), 0.0))
    bass_pat = (style["bass"] + "." * 16)[:16]
    if any(c != "." for c in bass_pat):
        sq = g.add("Note Sequencer", rng.randrange(1 << 30), dict(
            {"steps": ("i", 16), "rateMode": ("i", 0), "rateBeats": ("f", 0.25), "gatePercent": ("f", 55)},
            **{("note%d" % k): ("i", degree_note(int(c, 36) if c != "." else 0, root, scale, 1)) for k, c in enumerate(bass_pat)},
            **{("en%d" % k): ("b", int(c != ".")) for k, c in enumerate(bass_pat)},
            **{("vel%d" % k): ("f", 0.9) for k in range(16)}))
        sy = g.add(pick(rng, style["synth"], ban, facts), rng.randrange(1 << 30))
        g.connect(sq, sy, "note")
        synth_ids.append(sy)
        chans.append((chain(sy, "audio", rng.choice([0, 1, 1, 2])), -4.0))
    if style["family"] in ("ambient", "house", "trance", "downtempo", "retro", "techno", "experimental", "bass_music") or not chans:
        ch = g.add("Chorder", rng.randrange(1 << 30), {"root": ("i", root), "scale": ("i", scale), "chordSize": ("i", rng.choice([3, 4])),
                                                         "rateBeats": ("f", 4.0), "progSteps": ("i", 4), "strumMs": ("f", 15.0)})
        notes = ch
        if style["family"] in ("trance", "retro", "house") and rng.random() < 0.6:
            ar = g.add("Arpeggiator", rng.randrange(1 << 30), {"rateMode": ("i", 0), "rateBeats": ("f", 0.25), "octaves": ("i", 2), "mode": ("i", 0)})
            g.connect(ch, ar, "note"); notes = ar
        sy = g.add(pick(rng, style["synth"], ban, facts), rng.randrange(1 << 30))
        g.connect(notes, sy, "note")
        synth_ids.append(sy)
        chans.append((chain(sy, "audio", rng.choice([1, 2])), -9.0))
    mx = g.add("Mixer", 0, {"channels": ("i", len(chans))})
    for i, (n, db) in enumerate(chans):
        g.connect(n, mx, "audio")
        g.lines.insert(g.lines.index("end", g.lines.index("node %d Utility Mixer" % mx)), "  f gain%d %g" % (i, db))
    end = mx
    if "Reverb" not in ban:
        rv = g.add("Reverb", rng.randrange(1 << 30), {"mix": ("f", 0.45 if style["family"] == "ambient" else 0.15), "decay": ("f", 3.0 if style["family"] == "ambient" else 1.2)})
        g.connect(end, rv, "audio"); end = rv
    lm = g.add("Limiter", 0)
    g.connect(end, lm, "audio")
    out = g.add("Audio Out", 0)
    g.connect(lm, out, "audio")
    k = round(style["mod"] * 3)
    targets = [(n, m["key"]) for n in synth_ids for m in facts[g.names[n]]["modulatable"] if m.get("key") and any(w in m["key"].lower() for w in MOD_KEYS)]
    rng.shuffle(targets)
    for t, key in targets[:k]:
        lf = g.add("LFO", rng.randrange(1 << 30))
        g.lines.append("mod %d %s %d 0 0 %.3f 0.5" % (t, key, lf, 0.1 + 0.5 * style["mod"] * rng.random()))
    text = g.text()
    head, _, rest = text.partition("\n")
    return "%s\ntransport %d 4 4 %d %d\n# style: %s bpm %d key %d scale %d\n%s" % (head, bpm, root, scale, style["id"], bpm, root, scale, rest), dict(bpm=bpm, root=root, scale=scale)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["audio"])
    ap.add_argument("style")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="/tmp/uni/styled")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("--secs", type=int, default=8)
    a = ap.parse_args()
    facts = u.load_facts(); scene.pools_v2(facts)
    S = styles()
    os.makedirs(a.out, exist_ok=True)
    for sid in (S if a.style == "all" else [a.style]):
        text, meta = build(S[sid], a.seed, facts)
        inf = os.path.join(a.out, "%s_%d.inf" % (sid, a.seed))
        open(inf, "w").write(text)
        v = subprocess.run([a.bin, "--validate", inf], capture_output=True, text=True)
        print(sid, meta, "validate", v.returncode, (v.stdout + v.stderr).strip()[:160])


if __name__ == "__main__":
    main()
