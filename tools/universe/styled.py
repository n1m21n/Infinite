#!/usr/bin/env python3
"""Style-driven patches: a music style (tools/catalog/styles_music.json) becomes a tempo-locked, in-key patch.
    styled.py audio <style_id|all> [--seed N] [--out DIR] [--bin BIN] [--secs 8]
The style fixes BPM, key, scale, swing, the Drum Sequencer pattern, the bass figure, which synths and effects may appear (ban list first)
and how much the modulators move. Nothing here is random except inside those limits."""
import argparse, json, os, random, shutil, subprocess, sys
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


def build(style, seed, facts, g=None):
    rng = random.Random("%s:%d" % (style["id"], seed))
    bpm = rng.randint(*style["bpm"])
    root, scale = rng.randrange(12), rng.choice(style["scale"])
    ban = set(style["ban"])
    own = g is None
    g = g or scene.Graph(facts, rng)
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
    meta = dict(bpm=bpm, root=root, scale=scale, limiter=lm)
    if not own:
        return g, meta
    text = g.text()
    head, _, rest = text.partition("\n")
    return "%s\ntransport %d 4 4 %d %d\n# style: %s bpm %d key %d scale %d\n%s" % (head, bpm, root, scale, style["id"], bpm, root, scale, rest), meta


# ---------------------------------------------------------------- pictures
NO_FILE = {"Image Source", "Video", "Slideshow", "Video In", "Texture", "Shadertoy"}  # need a file or device: would render black


def art_styles():
    d = json.load(open(os.path.join(ROOT, "tools", "catalog", "styles_art.json")))["styles"]
    sys.path.insert(0, os.path.join(ROOT, "tools", "spread"))
    import art_taxonomy
    out = {}
    for s in d:
        if s["taxonomy"] == "spread":
            tx = art_taxonomy.TAXONOMY[s["id"]]
            s = dict(s, name=tx["name"], palette=tx["palette"])
        out[s["id"]] = s
    return out


def rgb(h):
    h = h.lstrip("#")
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]


WORDS = ["signal", "after hours", "static", "drift", "night shift", "afterglow", "hum", "low tide", "feedback", "neon rain", "loop", "dusk", "phase", "slow burn", "echo", "transit",
         "0x1F", "halflight", "pulse", "no sleep", "mono", "far field", "carrier", "late bus", "deep end", "glass", "orbit", "tape", "bloom", "ghost note"]


def paint(g, nid, pal, rng):
    """Replace every colour parameter of a node with palette colours (background = darkest first). A Text node also gets words, never the default."""
    if g.names[nid].split(":")[0] == "Text":
        g.lines.insert(g.lines.index("end", g.lines.index("node %d %s Text" % (nid, g.facts[g.names[nid]]["category"]))), "  s text %s" % rng.choice(WORDS))
    lum = sorted(pal, key=lambda c: sum(rgb(c)))
    ks = [r["key"] for r in g.facts[g.names[nid]]["rows"] if r["tag"] == "c"]
    if not ks:
        return
    for i, k in enumerate(ks):
        low = k.lower()
        c = lum[0] if ("bg" in low or "low" in low or "shadow" in low) else lum[-1] if ("high" in low or "fill" in low) else pal[(i + rng.randrange(len(pal))) % len(pal)]
        if k.startswith("stopColor"):
            c = pal[int(k[9:]) % len(pal)] if lum else c
        g.lines.insert(g.lines.index("end", g.lines.index("node %d %s %s" % (nid, g.facts[g.names[nid]]["category"], g.names[nid].split(":")[0]))),
                       "  c %s %.4f %.4f %.4f" % ((k,) + tuple(rgb(c))))


def build_visual(style, seed, facts, g=None, audio_out=None):
    rng = random.Random("%s:v:%d" % (style["id"], seed))
    own = g is None
    g = g or scene.Graph(facts, rng)
    ban = set(style["ban"])
    pal = style["palette"]
    f = facts
    srcs = [n for n in style["sources"] if n not in ban and n not in NO_FILE and n in f]
    flat = [n for n in srcs if f[n]["category"] == "Source"]
    geo = [n for n in srcs if f[n]["category"] == "3D" and not any(i["kind"] == "geometry" for i in f[n]["inputs"])]
    ops = [n for n in srcs if f[n]["category"] == "3D" and any(i["kind"] == "geometry" for i in f[n]["inputs"])]
    fx = [n for n in style["fx"] if n not in ban and n in f]
    comp = [n for n in style["comp"] if n not in ban and n in f and n != "Blend"]
    layers = []
    bg = g.add(rng.choice([n for n in ("Noise", "Ramp") if n not in ban] or ["Noise"]), rng.randrange(1 << 30))
    paint(g, bg, pal, rng)
    layers.append(bg)
    for li in range(3 if style["motion"] < 0.6 else 4):
        if geo and (not flat or rng.random() < 0.5):
            cur = g.add(rng.choice(geo), rng.randrange(1 << 30))
            for o in ops[:1]:
                op = g.add(o, rng.randrange(1 << 30)); g.connect(cur, op); cur = op
            r3 = g.add("Render 3D", rng.randrange(1 << 30)); g.connect(cur, r3); cur = r3; paint(g, r3, pal, rng)
        else:
            cur = g.add(rng.choice(flat or ["Noise"]), rng.randrange(1 << 30))
            paint(g, cur, pal, rng)
        if fx and rng.random() < 0.6:
            n = g.add(rng.choice(fx), rng.randrange(1 << 30)); g.connect(cur, n); cur = n
        layers.append(cur)
    cur = layers[0]
    for nxt in layers[1:]:
        b = g.add("Blend", rng.randrange(1 << 30)); g.connect(cur, b); g.connect(nxt, b); cur = b
    for pool, k in ((fx, rng.choice([1, 2])), (comp, rng.choice([1, 2]))):
        for _ in range(k if pool else 0):
            n = g.add(rng.choice(pool), rng.randrange(1 << 30)); g.connect(cur, n); cur = n
            paint(g, n, pal, rng)
    for fin in ("vignette", "bloom"):
        if fin in f and fin not in ban and rng.random() < 0.7:
            n = g.add(fin, rng.randrange(1 << 30)); g.connect(cur, n); cur = n
    out = g.add("Output", 0)
    g.connect(cur, out)
    if audio_out:
        g.lines.append("aud %d 1 %d" % (out, audio_out))
    k = round(style["motion"] * 4)
    targets = [(n, m["key"]) for n in list(g.names) for m in f[g.names[n]]["modulatable"]
               if m.get("key") and f[g.names[n]]["category"] in ("Source", "Effects", "Compositing") and not m.get("bool") and not m.get("step")]
    rng.shuffle(targets)
    for t, key in targets[:k]:
        lf = g.add("LFO", rng.randrange(1 << 30))
        g.lines.append("mod %d %s %d 0 0 %.3f 0.5" % (t, key, lf, 0.15 + 0.5 * style["motion"] * rng.random()))
    if own:
        return g.text(), {}
    return g, {}


def best_visual(style, seed, facts, binary, d, tries=6):
    """Try several wirings of the style, keep the picture with the most going on (luminance spread, edges, colour variety)."""
    import library
    best, bt = -1, None
    for k in range(tries):
        text, meta = build_visual(style, seed * 10 + k, facts)
        inf = os.path.join(d, "try.inf"); open(inf, "w").write(text)
        td = os.path.join(d, "try"); os.makedirs(td, exist_ok=True)
        subprocess.run([binary, "--frame", inf, "0", td + "/", "--size", "180x320"], capture_output=True, timeout=120)
        pngs = [x for x in os.listdir(td) if x.endswith(".png")]
        sc = library.interest(os.path.join(td, pngs[0])) if pngs else -1
        for x in pngs:
            os.remove(os.path.join(td, x))
        if sc > best:
            best, bt = sc, text
    shutil.rmtree(os.path.join(d, "try"), ignore_errors=True)
    os.remove(os.path.join(d, "try.inf"))
    return bt, {}

def bpm_ok(est, band):
    """Engine's own tempo estimate, allowed to be half / double (it often locks to the beat's subdivision)."""
    return bool(est) and any(band[0] * 0.93 <= est * k <= band[1] * 1.07 for k in (0.5, 1, 2))


def batch(n, out, binary, workers, secs, seed):
    from concurrent.futures import ThreadPoolExecutor
    sys.path.insert(0, os.path.join(ROOT, "tools", "catalog"))
    import catalog
    sys.path.insert(0, HERE)
    facts = u.load_facts(); scene.pools_v2(facts)
    M, A = styles(), art_styles()
    rr = random.Random(seed)
    mids, aids = sorted(M), sorted(A)
    jobs = []
    for i in range(n):
        kind = ["A", "V", "AV"][i % 3]
        a = rr.choice(aids)
        m = rr.choice([x for x in A[a]["pairs"] if x in M]) if kind == "AV" else rr.choice(mids)
        jobs.append((i, kind, m, a))
    os.makedirs(out, exist_ok=True)

    def one(job):
        i, kind, mid, aid = job
        folder = {"A": "audio", "V": "visual", "AV": "audiovisual"}[kind]
        eid = "s%04d" % (seed * 1000 + i)
        d = os.path.join(out, folder, eid); os.makedirs(d, exist_ok=True)
        s = seed * 1000 + i
        if kind == "A":
            text, meta = build(M[mid], s, facts)
        else:
            g = scene.Graph(facts, random.Random(s))
            if kind == "AV":
                g, meta = build(M[mid], s, facts, g)
                g, _ = build_visual(A[aid], s, facts, g, meta["limiter"])
                text = g.text()
                head, _, rest = text.partition("\n")
                text = "%s\ntransport %d 4 4 %d %d\n%s" % (head, meta["bpm"], meta["root"], meta["scale"], rest)
            else:
                text, meta = best_visual(A[aid], s, facts, binary, d)
        inf = os.path.join(d, "patch.inf")
        open(inf, "w").write(text)
        if subprocess.run([binary, "--validate", inf], capture_output=True).returncode:
            shutil.rmtree(d); return None
        made = catalog.render_media(binary, inf, kind, d, secs)
        if not all(made.get(k) for k, ks in {"wav": ("A", "AV"), "png": ("V", "AV"), "mp4": ("V", "AV")}.items() if kind in ks):
            shutil.rmtree(d); return None
        am = made.get("metrics") or {}
        row = {"id": "%s/%s" % (folder, eid), "kind": kind, "genome": "styled", "nodes": sorted(set(l.split(" ", 3)[3] for l in text.splitlines() if l.startswith("node "))),
               "tags": [{"A": "audio", "V": "visual", "AV": "audiovisual"}[kind]], "class": "ok", "interest": None,
               "audio": {k: am[k] for k in ("integrated_lufs", "sample_peak_dbfs", "onset_count_estimate", "silence_ratio") if k in am},
               "style": {"music": mid if kind != "V" else None, "art": aid if kind != "A" else None}}
        if kind in ("V", "AV"):
            import library
            row["interest"] = library.interest(os.path.join(d, "poster.png"))
            from PIL import Image, ImageStat
            row["lum"] = round(ImageStat.Stat(Image.open(os.path.join(d, "poster.png")).convert("L")).mean[0], 1)
        if kind in ("A", "AV") and not bpm_ok(am.get("bpm_estimate"), M[mid]["bpm"]) and M[mid]["family"] not in ("ambient", "experimental"):
            shutil.rmtree(d); return ("tempo", mid)
        if not catalog.passes(row):
            shutil.rmtree(d); return ("gate", row["style"])
        json.dump(row, open(os.path.join(d, "meta.json"), "w"), indent=1)
        return row

    kept, dropped = [], []
    with ThreadPoolExecutor(workers) as ex, open(os.path.join(out, "index.jsonl"), "a") as idx:
        for r in ex.map(one, jobs):
            if isinstance(r, dict):
                kept.append(r); idx.write(json.dumps(r) + "\n"); idx.flush()
            elif r:
                dropped.append(r)
    print("kept %d of %d; dropped %s" % (len(kept), n, dropped))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["audio", "batch"])
    ap.add_argument("--n", type=int, default=36)
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("style")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="/tmp/uni/styled")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    ap.add_argument("--secs", type=int, default=8)
    a = ap.parse_args()
    facts = u.load_facts(); scene.pools_v2(facts)
    if a.kind == "batch":
        return batch(a.n, a.out, a.bin, a.workers, a.secs, a.seed)
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
