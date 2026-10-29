#!/usr/bin/env python3
"""What each knob of an audio effect does to a sound (STATUS-2026-10-29.md section 1, step 2).

    fx_sweeps.py run   --fx Reverb [--banks 12]      render: 8 fixed parts through the effect, one knob moved at a time
    fx_sweeps.py score [--fx Reverb,Drive]           knob -> what changes, held out by part (bar 90% on reliable cells)
    fx_sweeps.py list                                 the AudioEffects nodes this covers

Parts: 8 fixed sounds made by Infinite's own synths at their defaults (no samples, so nothing to license): sub, bass, pad
chord, lead line, pluck, bell, hats, metal hit. Each recipe of the effect is rendered with every part (one batch per part).
Design per bank: one base (every knob drawn in its range) plus one move of each knob (> 15% of its range away), so every
pair differs in exactly one knob.
Measures, per render, on the whole file (dB or log units, so a change is comparable across parts):
  level (RMS dB)   bright (spectral centroid, log2 Hz)   low (share < 200 Hz, dB)   air (share > 5 kHz, dB)
  tail (energy in the 1.6 s after the last note ends vs while playing, dB: space and decay)
  width (side / mid, dB)   grit (spectral flatness of the played part, dB: distortion, noise)   crest (peak / RMS, dB: squash)
  wobble (sd of the 20 ms envelope while notes hold: tremolo, chorus)
Rows: ~/Infinite/training-data/audio/fx_<effect>.jsonl (one per recipe x part; ids are content hashes, so runs resume).
Result: docs/plans/data-universe/feel/fx_map.json and fx_map.md."""
import argparse, hashlib, json, os, random, subprocess, sys, tempfile, time, wave
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402

SR = 48000
OUT_DIR = os.path.expanduser("~/Infinite/training-data/audio")
MAP = os.path.join(os.path.dirname(os.path.dirname(HERE)), "docs/plans/data-universe/feel/fx_map")
STOP = os.path.expanduser("~/STOP_OWN")
PLAY, TAIL = 2.4, 1.6  # seconds of notes, then silence for the tail
FEATS = ["level", "bright", "low", "air", "tail", "width", "grit", "crest", "wobble"]
FX = ["Audio Filter", "Bitcrush", "Chorus", "Convolution Reverb", "Cycle Shaper", "Delay", "Drive", "Dynamics", "EQ", "Flanger",
      "Formant Filter", "Frequency Shifter", "Limiter", "Phaser", "Pitch Shifter", "Resonator Bank", "Reverb", "Ring Mod", "Saturator",
      "Shape Resonator", "Spec Blur", "Spectrum Slide", "Stereo", "Stutter", "Transient Shaper", "Tremolo", "Wavetable Shaper"]
# effect knobs left at their default: they pick a file, sync to the transport, or bypass
FX_FIXED = {"bypass": 0, "sync": 0}
# (name, synth, notes as (start s, midi, velocity 0-1, length s)); synths at their defaults
E8 = [i * 0.3 for i in range(8)]
PARTS = [("sub", "Oscillator", [(0.0, 36, 0.8, 2.2)]),
         ("bass", "Analog", [(t, 36 + (7 if i % 4 == 3 else 0), 0.8, 0.22) for i, t in enumerate(E8)]),
         ("pad", "String", [(0.0, m, 0.6, 2.2) for m in (60, 64, 67)]),
         ("lead", "Wavetable", [(i * 0.4, m, 0.8, 0.35) for i, m in enumerate((72, 74, 76, 79, 76, 74))]),
         ("pluck", "String", [(t, 60 + (0, 7, 12, 7)[i % 4], 0.8, 0.12) for i, t in enumerate(E8)]),
         ("bell", "FM", [(0.0, 72, 0.8, 0.5), (1.2, 79, 0.8, 0.5)]),
         ("hats", "Metallic", [(i * 0.15, 84, 0.7 if i % 2 else 0.9, 0.05) for i in range(16)]),
         ("metal", "Metallic", [(0.0, 48, 1.0, 0.3), (1.2, 48, 1.0, 0.3)])]


def fx_out(fx):
    return os.path.join(OUT_DIR, "fx_%s.jsonl" % fx.lower().replace(" ", "_"))


def table(fx):
    t = osw.describe(fx)
    sw, fixed = [], {}
    for r in t["rows"]:
        k, tag, df, lo, hi = r["key"], r["tag"], r["default"], r["min"], r["max"]
        if k in FX_FIXED or tag == "s" or lo is None or hi is None or hi == lo:
            fixed[k] = FX_FIXED.get(k, df)
        else:
            kind = "bool" if tag == "b" else "enum" if tag == "i" else \
                "log" if osw.FREQ_HINT.search(k) and lo > 0 else "time" if osw.TIME_HINT.search(k) and lo >= 0 and hi >= 100 else "lin"
            sw.append({"key": k, "tag": tag, "default": df, "lo": float(lo), "hi": float(hi), "kind": kind})
    return {r["key"]: r["tag"] for r in t["rows"]}, sw, fixed


def bank_moves(sw, bank):
    """One base, then each knob moved once (> 15% of its range away)."""
    rng = random.Random("fx%d" % bank)
    base = [rng.random() for _ in sw]
    out = [("base", base)]
    for i, p in enumerate(sw):
        u = list(base)
        for _ in range(50):
            u[i] = rng.random()
            if abs(u[i] - base[i]) > 0.15 and osw.to_value(p, u[i]) != osw.to_value(p, base[i]):
                break
        out.append(("k%d" % i, u))
    return out


def patch(fx, tags, rec, part):
    _, synth, _ = part
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Synths " + synth, "  id syn", "  f volume 0.8", "end",
         "node 3 AudioEffects " + fx, "  id fx"]
    for k, v in rec.items():
        tg = tags.get(k, "f")
        if tg == "s":
            L.append("  s %s %s" % (k, str(v).replace("\\", "\\\\").replace("\n", "\\n")))
        else:
            L.append("  %s %s %s" % (tg, k, int(float(v)) if tg in ("i", "b") else repr(float(v))))
    L += ["end", "node 4 Utility Audio Out", "  id out", "end", "note syn 0 keys", "aud fx 0 syn", "aud out 0 fx"]
    return "\n".join(L) + "\n"


def notes_files(d, part):
    _, _, notes = part
    types = {"n%d" % i: {"node": "syn", "pitch": m, "velocity_from": "v", "length_from": "len"} for i, (_, m, _, _) in enumerate(notes)}
    evs = [{"t": osw.LEAD + t, "type": "n%d" % i, "v": v, "len": ln} for i, (t, _, v, ln) in enumerate(notes)]
    ep, mp_ = os.path.join(d, "events.json"), os.path.join(d, "map.json")
    json.dump(evs, open(ep, "w")); json.dump(types, open(mp_, "w"))
    return ep, mp_


def render(binp, fx, tags, recs, part, tmp, timeout=300):
    """One Infinite process per part renders every recipe (--batch). A wav path or None per recipe."""
    ep, mp_ = notes_files(tmp, part)
    dur = osw.LEAD + PLAY + TAIL
    jobs = []
    for k, rec in enumerate(recs):
        inf, js, wav = (os.path.join(tmp, "%s%d.%s" % (n, k, e)) for n, e in (("p", "inf"), ("s", "json"), ("o", "wav")))
        open(inf, "w").write(patch(fx, tags, rec, part)); jobs.append((inf, js, wav))
    res, todo = [None] * len(jobs), list(range(len(jobs)))
    while todo:
        lst = os.path.join(tmp, "batch.txt")
        open(lst, "w").write("".join("%s\t%s\t%s\n" % jobs[k] for k in todo[1:]))
        inf, js, wav = jobs[todo[0]]
        try:
            subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % dur, "--sample-rate", str(SR), "--wav", wav, "--lenient",
                            "--notes", ep, "--note-map", mp_, "--batch", lst, "--timeout", "%d" % timeout], capture_output=True, text=True, timeout=timeout + 30)
        except subprocess.TimeoutExpired:
            pass
        left = []
        for k in todo:
            w = jobs[k][2]
            if left or not os.path.exists(w) or os.path.getsize(w) < 1000:
                left.append(k); continue
            res[k] = w
        todo = left[1:]
    return res


def read_wav(path):
    with wave.open(path) as w:
        n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth(); raw = w.readframes(n)
    x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768 if sw == 2 else \
        (np.frombuffer(raw, np.int32).astype(np.float64) / 2 ** 31 if sw == 4 else None)
    if x is None:  # 24-bit
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3); x = ((b[:, 0].astype(np.int32) << 8 | b[:, 1].astype(np.int32) << 16 | b[:, 2].astype(np.int32) << 24) >> 8) / 2 ** 23
    x = x.reshape(-1, ch)
    return (x[:, 0], x[:, 1]) if ch > 1 else (x[:, 0], x[:, 0])


def measure(L, R, part):
    """The nine numbers in the docstring. Times from the part's own notes."""
    db = lambda e: 10 * np.log10(e + 1e-12)
    s0 = int(osw.LEAD * SR); last = osw.LEAD + max(t + ln for t, _, _, ln in part[2])
    s1 = int(min(osw.LEAD + PLAY, last + 0.05) * SR); s2 = int((last + 0.1) * SR)
    M, S = (L + R) / 2, (L - R) / 2
    play, tail = M[s0:s1], M[s2:]
    e_play = np.mean(play ** 2)
    spec = np.abs(np.fft.rfft(play * np.hanning(len(play)))) ** 2; fr = np.fft.rfftfreq(len(play), 1 / SR)
    tot = spec.sum() + 1e-18
    # flatness over 8192-sample frames, so one long FFT's fine bins do not swamp it
    fl = []
    for i in range(0, len(play) - 8192, 4096):
        p = np.abs(np.fft.rfft(play[i:i + 8192] * np.hanning(8192)))[4:2000] ** 2 + 1e-18
        fl.append(np.exp(np.mean(np.log(p))) / np.mean(p))
    env = np.sqrt(np.convolve(play ** 2, np.ones(960) / 960, "valid")[::480] + 1e-12)
    held = env[len(env) // 5: -len(env) // 5] if len(env) > 10 else env
    return {"level": round(float(db(e_play)), 3),
            "bright": round(float(np.log2((spec * fr).sum() / tot + 1)), 4),
            "low": round(float(db(spec[fr < 200].sum() / tot)), 3),
            "air": round(float(db(spec[fr > 5000].sum() / tot)), 3),
            "tail": round(float(db(np.mean(tail ** 2)) - db(e_play)), 3),
            "width": round(float(db(np.mean(S[s0:s1] ** 2)) - db(e_play)), 3),
            "grit": round(float(db(np.mean(fl))) if fl else 0.0, 3),
            "crest": round(float(db(np.max(play ** 2)) - db(e_play)), 3),
            "wobble": round(float(np.std(20 * np.log10(held)) if len(held) > 2 else 0.0), 3)}


def cmd_run(a):
    binp = osw.binary(); out = fx_out(a.fx)
    tags, sw, fixed = table(a.fx)
    have = set()
    if os.path.exists(out):
        have = {json.loads(ln)["id"] for ln in open(out)}
    t0, made = time.time(), 0
    f = open(out, "a")
    for bank in range(a.banks):
        if os.path.exists(STOP):
            break
        items = []
        for design, u in bank_moves(sw, bank):
            rec = dict(fixed)
            for p, x in zip(sw, u):
                v = osw.to_value(p, x); rec[p["key"]] = v if p["tag"] in ("i", "b") else float("%.6g" % v)
            items.append((design, u, dict(sorted(rec.items()))))
        for part in PARTS:
            todo = [(d, u, r, hashlib.sha1((a.fx + part[0] + json.dumps(r, sort_keys=True)).encode()).hexdigest()[:16]) for d, u, r in items]
            todo = [t for t in todo if t[3] not in have]
            if not todo:
                continue
            with tempfile.TemporaryDirectory() as tmp:
                wavs = render(binp, a.fx, tags, [t[2] for t in todo], part, tmp)
                for (d, u, r, i), w in zip(todo, wavs):
                    if not w:
                        continue
                    L, R = read_wav(w)
                    f.write(json.dumps({"id": i, "fx": a.fx, "bank": bank, "design": d, "part": part[0], "knobs": [round(x, 5) for x in u],
                                        "keys": [p["key"] for p in sw], "recipe": r, "m": measure(L, R, part)}) + "\n")
                    made += 1
            f.flush()
        print("  bank %d: %d renders, %d s" % (bank, made, time.time() - t0), flush=True)


def score(fx, tau=0.8, eff=0.5, need=6):
    """Pairs within a bank and part that differ in one knob. A (knob, measure) cell is reliable when >= tau of its training pairs
    with a clear change (|change| >= eff sd of that measure) moved it the same way per unit of knob. Held out by part: train on
    seven parts, test on the eighth, so a mapping holds for a sound it never saw."""
    path = fx_out(fx)
    if not os.path.exists(path):
        return None
    rows = [json.loads(ln) for ln in open(path)]
    keys = rows[0]["keys"]
    M = np.array([[r["m"][k] for k in FEATS] for r in rows]); sd = M.std(0) + 1e-9
    by = {}
    for r, m in zip(rows, M):
        by.setdefault((r["bank"], r["part"]), {})[r["design"]] = (np.array(r["knobs"]), m)
    P = []  # (part, knob, dk, change in sd units)
    for (b, part), d in by.items():
        if "base" not in d:
            continue
        u0, m0 = d["base"]
        for name, (u, m) in d.items():
            if name == "base":
                continue
            j = int(name[1:]); dk = u[j] - u0[j]
            if dk != 0:
                P.append((part, j, dk, (m - m0) / sd))
    hit = tot = 0; per = {}
    for held in [p[0] for p in PARTS]:
        tr = [p for p in P if p[0] != held]; te = [p for p in P if p[0] == held]
        for j in range(len(keys)):
            s = np.array([np.sign(p[2]) * np.sign(p[3]) * (np.abs(p[3]) >= eff) for p in tr if p[1] == j])
            if not len(s):
                continue
            n = (s != 0).sum(0); up = (s > 0).sum(0)
            ok = (n >= need) & (np.maximum(up, n - up) >= tau * np.maximum(n, 1))
            for p in te:
                if p[1] != j:
                    continue
                for a in np.flatnonzero(ok & (np.abs(p[3]) >= eff)):
                    pred = 1 if up[a] * 2 >= n[a] else -1
                    tot += 1; hit += int(np.sign(p[2]) * pred == np.sign(p[3][a]))
    # the map itself, from every pair: per knob, the measures it moves reliably, with the median change for a full turn up
    for j, k in enumerate(keys):
        ps = [p for p in P if p[1] == j]
        if not ps:
            continue
        s = np.array([np.sign(p[2]) * np.sign(p[3]) * (np.abs(p[3]) >= eff) for p in ps]); n = (s != 0).sum(0); up = (s > 0).sum(0)
        slope = np.median(np.array([p[3] / p[2] for p in ps]), 0) * sd  # in the measure's own units per full knob turn
        cells = []
        for a in range(len(FEATS)):
            if n[a] >= need and max(up[a], n[a] - up[a]) >= tau * n[a]:
                cells.append({"measure": FEATS[a], "dir": "up" if up[a] * 2 >= n[a] else "down", "agree": round(max(up[a], n[a] - up[a]) / n[a], 2),
                              "median_per_turn": round(float(slope[a]), 3)})
        per[k] = {"pairs": len(ps), "moves": cells}
    return {"rows": len(rows), "pairs": len(P), "held_out_acc": round(hit / tot, 3) if tot else None, "scored": tot, "knobs": per}


def cmd_score(a):
    res = json.load(open(MAP + ".json")) if os.path.exists(MAP + ".json") else {}
    for fx in (a.fx.split(",") if a.fx else FX):
        r = score(fx)
        if r:
            res[fx] = r; print(fx, r["rows"], "rows", r["pairs"], "pairs, held-out acc", r["held_out_acc"], "on", r["scored"], flush=True)
    json.dump(res, open(MAP + ".json", "w"), indent=1)
    L = ["# What each effect knob does (fx_sweeps.py; held out by part, bar 90%)", "",
         "Turning the knob up moves these measures (agree = share of clear changes in that direction).", ""]
    for fx, r in sorted(res.items()):
        L += ["## %s: held-out %s on %d moves" % (fx, r["held_out_acc"], r["scored"]), "", "| Knob | Moves (up/down, agree, median change per full turn) |", "|---|---|"]
        for k, c in r["knobs"].items():
            L.append("| %s | %s |" % (k, ", ".join("%s %s (%.2f, %+.2f)" % (m["measure"], m["dir"], m["agree"], m["median_per_turn"]) for m in c["moves"]) or "nothing reliable"))
        L.append("")
    open(MAP + ".md", "w").write("\n".join(L))


def main():
    ap = argparse.ArgumentParser(); sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run"); r.add_argument("--fx", required=True); r.add_argument("--banks", type=int, default=12)
    s = sub.add_parser("score"); s.add_argument("--fx", default="")
    sub.add_parser("list")
    a = ap.parse_args()
    if a.cmd == "run":
        cmd_run(a)
    elif a.cmd == "score":
        cmd_score(a)
    else:
        print("\n".join(FX))


if __name__ == "__main__":
    sys.exit(main())
