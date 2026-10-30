#!/usr/bin/env python3
"""What each knob of a note effect does to a phrase (PLAN-2026-10-30 item 6). The note twin of fx_sweeps.py.

    note_fx.py run   --fx Arpeggiator [--banks 12]   render: 6 fixed phrases through the node, one knob moved at a time
    note_fx.py score [--fx Arpeggiator,Humanizer]    knob -> what changes, held out by phrase (bar 90% on reliable cells)
    note_fx.py list                                   the Notes nodes this covers

Route: phrase -> Keyboard -> <note node> -> sine Oscillator -> Audio Out, rendered headless (the phrase enters at the Keyboard,
so it passes through the node). The sine has one spectral peak per note, so notes are read back from the audio. The envelope
(2 ms attack, 120 ms decay to a 0.3 sustain) makes every note start a clear rise, even a repeat of the same pitch.
Design per bank, as in fx_sweeps.py: one base (every knob drawn in its range) plus one move of each knob.
Measures, per render, from the audio:
  onsets (count)   register (mean MIDI pitch)   span (p90 - p10 pitch)   poly (pitches per onset)
  ioi_cv (sd / mean of the gaps between onsets: rhythm)   grid (mean ms from the nearest 1/16 at 120 bpm: timing)
  level (RMS dB while sounding: velocity)   vel_sd (sd of the per-onset peak, dB)   duty (share of time sounding: length)
  cents (mean distance of each note from the nearest semitone: bends and glides)
Rows: ~/Infinite/training-data/audio/notefx_<node>.jsonl. Result: docs/plans/data-universe/feel/note_fx_map.json and .md."""
import argparse, hashlib, json, os, sys, tempfile, time
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402
import fx_sweeps as fxs  # noqa: E402

SR = fxs.SR
MAP = os.path.join(os.path.dirname(os.path.dirname(HERE)), "docs/plans/data-universe/feel/note_fx_map")
FEATS = ["onsets", "register", "span", "poly", "ioi_cv", "grid", "level", "vel_sd", "duty", "cents"]
FX = ["Arpeggiator", "Gate", "Glide", "Humanizer", "Note Echo", "Note Filter", "Note Stack", "Note Strum", "Note Transpose",
      "Pitch Bend", "Quantizer", "Velocity Curve"]
# note-node knobs left at their default: the global scale is app state, not the node's
fxs.FX_FIXED.setdefault("useGlobalScale", 0)
# (name, "Oscillator", notes as (start s, midi, velocity 0-1, length s)), all inside fxs.PLAY seconds; pitches >= 55 so
# neighbouring semitones stay apart in an 85 ms window
PHRASES = [("chord", "Oscillator", [(0.0, m, 0.8, 1.0) for m in (60, 64, 67)] + [(1.2, m, 0.8, 1.0) for m in (65, 69, 72)]),
           ("line", "Oscillator", [(i * 0.25, m, 0.8, 0.2) for i, m in enumerate((67, 69, 71, 72, 74, 72, 71, 69))]),
           ("offgrid", "Oscillator", [(t, m, 0.8, 0.18) for t, m in zip((0.0, 0.27, 0.48, 0.80, 1.02, 1.31, 1.49, 1.77), (62, 64, 67, 69, 72, 74, 76, 79))]),
           ("ramp", "Oscillator", [(i * 0.125, 72, 0.2 + 0.8 * i / 11, 0.1) for i in range(12)]),
           ("wide", "Oscillator", [(i * 0.5, 55 if i % 2 == 0 else 84, 0.8, 0.4) for i in range(4)]),
           ("dyads", "Oscillator", [(t, m, 0.7, 0.5) for t, pr in zip((0.0, 0.6, 1.2, 1.8), ((64, 71), (62, 69), (60, 67), (59, 66))) for m in pr])]
SYNTH = ["  i waveform 0", "  f ampAttack 2", "  f ampDecay 120", "  f ampSustain 0.3", "  f ampRelease 20", "  f volume 0.8"]


def out_path(fx):
    return os.path.join(fxs.OUT_DIR, "notefx_%s.jsonl" % fx.lower().replace(" ", "_"))


def patch(fx, tags, rec, part):
    L = ["infinite-patch 1", "node 1 Notes Keyboard", "  id keys", "end", "node 2 Notes " + fx, "  id nfx"]
    for k, v in rec.items():
        tg = tags.get(k, "f")
        L.append("  %s %s %s" % (tg, k, int(float(v)) if tg in ("i", "b") else repr(float(v))))
    L += ["end", "node 3 Synths Oscillator", "  id syn"] + SYNTH + ["end", "node 4 Utility Audio Out", "  id out", "end",
          "note nfx 0 keys", "note syn 0 nfx", "aud out 0 syn"]
    return "\n".join(L) + "\n"


def measure(L, R, part=None):
    """The ten numbers in the docstring, from the mono mix after the lead-in."""
    pre = 0.1  # start before the first note so its rise is seen
    x = ((L + R) / 2)[int((osw.LEAD - pre) * SR):]
    hop, win = 120, 1024  # 2.5 ms
    n = (len(x) - win) // hop
    if n < 10:
        return {k: 0.0 for k in FEATS}
    fr = np.lib.stride_tricks.as_strided(x, (n, win), (x.strides[0] * hop, x.strides[0])) * np.hanning(win)
    env = 10 * np.log10(np.mean(fr ** 2, 1) + 1e-12)
    top = env.max(); sounding = env > top - 35
    # an onset is a rise of >= 6 dB within 10 ms that ends loud; it sits where the rise is steepest
    rise = np.zeros(n); rise[4:] = env[4:] - env[:-4]
    slope = np.zeros(n); slope[1:-1] = env[2:] - env[:-2]
    on, i = [], 4
    while i < n:
        if rise[i] >= 6 and env[i] > top - 30:
            j = i
            while j + 1 < n and rise[j + 1] >= 6:
                j += 1
            k = max(i - 4, 1) + int(np.argmax(slope[max(i - 4, 1):j + 1]))
            if not on or k - on[-1] >= 10:
                on.append(k)
            i = j + 1
        else:
            i += 1
    t_on = np.array([i * hop / SR + win / 2 / SR - pre for i in on])  # frame centres: a step reads as starting at its middle
    pitches, polys, cents, peaks = [], [], [], []
    w = np.hanning(4096)
    for i, t in zip(on, t_on):
        s = int((t + pre + 0.01) * SR)
        seg = x[s:s + 4096]
        if len(seg) < 4096:
            continue
        sp = np.abs(np.fft.rfft(seg * w, 32768)); f = np.fft.rfftfreq(32768, 1 / SR)
        band = (f > 100) & (f < 5000); spb = np.where(band, sp, 0)
        lim = spb.max() * 10 ** (-15 / 20)
        idx = [k for k in range(1, len(spb) - 1) if spb[k] > lim and spb[k] >= spb[k - 1] and spb[k] > spb[k + 1]]
        notes = set()
        for k in idx:
            a, b, c = np.log(spb[k - 1] + 1e-12), np.log(spb[k]), np.log(spb[k + 1] + 1e-12)
            d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
            m = 69 + 12 * np.log2((k + d) * SR / 32768 / 440)
            if round(m) not in notes:
                notes.add(round(m)); cents.append(abs(m - round(m)) * 100)
        if notes:
            pitches += sorted(notes); polys.append(len(notes))
        peaks.append(env[i:i + 12].max())
    ioi = np.diff(t_on)
    g = 0.125
    return {"onsets": float(len(t_on)),
            "register": round(float(np.mean(pitches)), 3) if pitches else 0.0,
            "span": round(float(np.percentile(pitches, 90) - np.percentile(pitches, 10)), 3) if pitches else 0.0,
            "poly": round(float(np.mean(polys)), 3) if polys else 0.0,
            "ioi_cv": round(float(ioi.std() / ioi.mean()), 4) if len(ioi) > 1 and ioi.mean() > 0 else 0.0,
            "grid": round(float(np.mean(np.abs(t_on - g * np.round(t_on / g))) * 1000), 3) if len(t_on) else 0.0,
            "level": round(float(10 * np.log10(np.mean(10 ** (env[sounding] / 10)))), 3),
            "vel_sd": round(float(np.std(peaks)), 3) if len(peaks) > 1 else 0.0,
            "duty": round(float(sounding[on[0]:].mean()), 4) if on else 0.0,
            "cents": round(float(np.mean(cents)), 3) if cents else 0.0}


def cmd_run(a):
    binp = osw.binary(); out = out_path(a.fx)
    tags, sw, fixed = fxs.table(a.fx)
    have = {json.loads(ln)["id"] for ln in open(out)} if os.path.exists(out) else set()
    t0, made = time.time(), 0
    f = open(out, "a")
    for bank in range(a.banks):
        if os.path.exists(fxs.STOP):
            break
        items = []
        for design, u in fxs.bank_moves(sw, bank):
            rec = dict(fixed)
            for p, v in zip(sw, u):
                val = osw.to_value(p, v); rec[p["key"]] = val if p["tag"] in ("i", "b") else float("%.6g" % val)
            items.append((design, u, dict(sorted(rec.items()))))
        for part in PHRASES:
            todo = [(d, u, r, hashlib.sha1(("nfx" + a.fx + part[0] + json.dumps(r, sort_keys=True)).encode()).hexdigest()[:16]) for d, u, r in items]
            todo = [t for t in todo if t[3] not in have]
            if not todo:
                continue
            with tempfile.TemporaryDirectory() as tmp:
                wavs = fxs.render(binp, a.fx, tags, [t[2] for t in todo], part, tmp, make=patch, node="keys")
                for (d, u, r, i), w in zip(todo, wavs):
                    if not w:
                        continue
                    L, R = fxs.read_wav(w)
                    f.write(json.dumps({"id": i, "fx": a.fx, "bank": bank, "design": d, "part": part[0], "knobs": [round(v, 5) for v in u],
                                        "keys": [p["key"] for p in sw], "recipe": r, "m": measure(L, R)}) + "\n")
                    made += 1
            f.flush()
        print("  bank %d: %d renders, %d s" % (bank, made, time.time() - t0), flush=True)


def cmd_score(a):
    res = json.load(open(MAP + ".json")) if os.path.exists(MAP + ".json") else {}
    for fx in (a.fx.split(",") if a.fx else FX):
        r = fxs.score(fx, feats=FEATS, parts=PHRASES, path=out_path(fx))
        if r:
            res[fx] = r; print(fx, r["rows"], "rows", r["pairs"], "pairs, held-out acc", r["held_out_acc"], "on", r["scored"], flush=True)
    json.dump(res, open(MAP + ".json", "w"), indent=1)
    L = ["# What each note-effect knob does (note_fx.py; held out by phrase, bar 90%)", "",
         "Phrase -> Keyboard -> node -> sine Oscillator, read back from the audio. Turning the knob up moves these measures "
         "(agree = share of clear changes in that direction).", ""]
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
