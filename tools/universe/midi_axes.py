#!/usr/bin/env python3
"""Block 1 (NEXT-SESSION): axes 23-31 on the MIDI side, one row per file -> store/midi_axes.jsonl (local, resumable).
    midi_axes.py run   [--limit N] [--jobs 3] [--budget MIN]    every file named in store/midi_full.jsonl that can be found; stop file ~/STOP_MIDI
    midi_axes.py check [--n 20]                                 20 files: row beside an independent re-derivation of tempo, key and rests
    midi_axes.py counts                                         coverage per axis and per corpus, and the files not found
Axes (feel/README.md): a23 mode (major-ness, -1 minor .. +1 major, Krumhansl-Schmuckler) + tonic; a24 tempo (bpm, tempo-map time-weighted
and onset-autocorrelation); a25 swing (long:short of off-beat eighth pairs); a26 micro-timing (median |offset| from the 16th grid, ms, pitched
and drum parts apart); a27 silence ratio and longest rest (s); a28 band energies (sub bass lowmid mid presence air, share of velocity x duration,
by fundamental; drums by class); a29 dynamics proxy (velocity mean, crest = max/mean, loudness range = p95-p10 of per-bar level; true LUFS needs
audio); a30 stereo proxy (pan spread from CC10, share of low notes on centred channels); a31 section arc (per-bar energy: build slope, drop height,
peak position). Audio-side versions of 23-31 are measured on renders in characteristics.py, not here."""
import argparse, json, os, sys, time, collections
import numpy as np
from concurrent.futures import ProcessPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import midi_full as mf

STORE = os.path.expanduser("~/Infinite/training-data/store"); IN = os.path.join(STORE, "midi_full.jsonl"); OUT = os.path.join(STORE, "midi_axes.jsonl")
ROOTS = [mf.ROOT] + [os.path.expanduser(p) for p in (
    "~/Library/CloudStorage/GoogleDrive-namansoniii21@gmail.com/My Drive/Infinite-Cloud-Training/training-data/inbox",
    "~/Library/CloudStorage/GoogleDrive-namansoniii21@gmail.com/My Drive/Infinite-Cloud-Training/training-data/inbox/_datasets_extra/data")]
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])
BANDS = [("sub", 0, 60), ("bass", 60, 250), ("lowmid", 250, 500), ("mid", 500, 2000), ("presence", 2000, 6000), ("air", 6000, 1e9)]
DRUM_BAND = {35: "sub", 36: "sub", 41: "bass", 43: "bass", 45: "bass", 47: "lowmid", 48: "lowmid", 50: "lowmid", 38: "mid", 40: "mid", 37: "mid", 39: "presence",
             42: "air", 44: "air", 46: "air", 49: "air", 51: "presence", 52: "presence", 53: "presence", 55: "air", 57: "air", 59: "presence"}


def find(src):
    for r in ROOTS:
        p = os.path.join(r, src)
        if os.path.exists(p): return p
    return None


def notes_of(ev):
    """(onset_tick, off_tick, pitch, vel, channel) from the parsed events; unmatched notes end at the file end."""
    on = {}; out = []; end = max([e[0] for e in ev] + [0])
    for t, k, ch, a, v in sorted(ev, key=lambda e: e[0]):
        key = (ch, a)
        if k == 0x90 and v > 0:
            if key in on: out.append((*on[key], t, key[1], key[0]))  # re-strike closes the old note
            on[key] = (t, v)
        elif k == 0x80 or (k == 0x90 and v == 0):
            if key in on: t0, v0 = on.pop(key); out.append((t0, t, a, v0, ch))
    for (ch, a), (t0, v0) in on.items(): out.append((t0, end, a, v0, ch))
    return [(t0, t1, a, v0, ch) for t0, t1, a, v0, ch in out], end


def mode_axis(pitch, w):
    h = np.bincount(np.asarray(pitch) % 12, weights=w, minlength=12).astype(float)
    if h.sum() == 0 or np.std(h) == 0: return None
    best = (-2, 0, 0)
    for tonic in range(12):
        r = np.roll(h, -tonic)
        for name, prof in ((1, MAJ), (-1, MIN)):
            c = float(np.corrcoef(r, prof)[0, 1])
            if c > best[0]: best = (c, tonic, name)
    rmaj = max(float(np.corrcoef(np.roll(h, -t), MAJ)[0, 1]) for t in range(12)); rmin = max(float(np.corrcoef(np.roll(h, -t), MIN)[0, 1]) for t in range(12))
    return {"major_ness": round(rmaj - rmin, 4), "tonic": best[1], "mode": "major" if best[2] == 1 else "minor", "key_r": round(best[0], 4)}


def fast_sec(meta, div):
    """Vectorised tick -> seconds through the tempo map (midi_full.seconds_fn is quadratic in tempo events)."""
    tm = sorted([(0, 500000)] + [(t, v) for t, k, v in meta if k == "tempo"]); ts = np.array([t for t, _ in tm], float); vs = np.array([v for _, v in tm], float)
    base = np.concatenate([[0.0], np.cumsum(np.diff(ts) * vs[:-1] / div / 1e6)])
    def f(t):
        t = np.asarray(t, float); j = np.maximum(np.searchsorted(ts, t, side="right") - 1, 0)
        return base[j] + (t - ts[j]) * vs[j] / div / 1e6
    return f


def row(src):
    r = {"src": src}
    p = find(src)
    if p is None: r["missing"] = 1; return r
    if os.path.getsize(p) > 4_000_000: r["error"] = "file over 4 MB"; return r
    try:
        fmt, div, ev, meta = mf.parse(open(p, "rb").read()); sec = fast_sec(meta, div)
        nt, end = notes_of(ev)
        if not nt: r["error"] = "no notes"; return r
        t0 = np.array([n[0] for n in nt], float); t1 = np.array([n[1] for n in nt], float); pit = np.array([n[2] for n in nt]); vel = np.array([n[3] for n in nt], float)
        ch = np.array([n[4] for n in nt]); drum = ch == 9; secs = max(float(sec(np.array([end], float))[0]), 1e-3)
        s0 = sec(t0); s1 = sec(t1); dur = np.maximum(s1 - s0, 0.02)
        # 23 mode
        pm = ~drum
        if pm.sum() >= 8:
            m = mode_axis(pit[pm], dur[pm] * vel[pm]);  r["a23"] = m
        # 24 tempo: tempo-map time-weighted, and onset autocorrelation on the beat grid
        marks = [(0, 500000)] + sorted((t, v) for t, k, v in meta if k == "tempo"); wt = collections.defaultdict(float)
        for (ta, va), (tb, _) in zip(marks, marks[1:] + [(end, 0)]): wt[va] += max(0.0, float(sec(np.array([tb], float))[0] - sec(np.array([ta], float))[0]))
        bpm_map = 60e6 / max(wt, key=wt.get) if wt else 120.0
        on_s = np.sort(np.unique(np.round(s0, 3)))
        est = None
        if len(on_s) > 16:
            g = 0.01; n = int(min(secs, 120) / g) + 1; x = np.zeros(n); idx = np.minimum((on_s / g).astype(int), n - 1); x[idx] = 1
            ac = np.correlate(x, x, "full")[n - 1:]; lo, hi = int(60 / 200 / g), int(60 / 50 / g)
            if hi < len(ac) - 1: est = 60.0 / ((lo + int(np.argmax(ac[lo:hi]))) * g)
        r["a24"] = {"bpm": round(float(bpm_map), 2), "bpm_onset": round(float(est), 2) if est else None, "tempo_changes": max(0, len(marks) - 1)}
        # 25 swing + 26 micro-timing on the seconds clock: beat length from whichever of tempo-map / onset-autocorr fits the onsets better
        def grid_fit(x, bpm):
            """(median |offset from the 16th grid| in ms at the best phase offset, that offset in beats-fraction) for pitched or drum onsets x."""
            q16 = 60.0 / bpm / 4.0; best = None
            for off in np.linspace(0, q16, 16, endpoint=False):
                u = (x - off) / q16; d = np.median(np.abs(u - np.round(u))) * q16
                if best is None or d < best[0]: best = (d, off)
            return best[0] * 1000, best[1]
        def axes_for(mask):
            x = s0[mask]
            if len(x) < 16: return None, None, None
            cands = [bpm_map] + ([est] if est else []) + ([est * 2, est / 2] if est else [])
            fits = [(grid_fit(x, c), c) for c in cands if 40 <= c <= 260]
            (d, off), bpm = min(fits, key=lambda f: f[0][0]); b = 60.0 / bpm
            ph = ((x - off) / b) % 1.0; o = ph[(ph > 0.40) & (ph < 0.85)]
            conf = float(np.mean(np.abs(((x - off) / (b / 4)) - np.round((x - off) / (b / 4))) < 0.1))  # share of onsets within 10% of a 16th
            sw = float(np.median(o) / max(1 - np.median(o), 1e-3)) if len(o) >= 8 and conf > 0.6 else None
            return round(d, 2), (round(sw, 3) if sw else None), round(conf, 3)
        mp_, sp_, cp_ = axes_for(~drum); md_, sd_, cd_ = axes_for(drum)
        r["a25"] = {"swing_ratio": sp_, "swing_ratio_drums": sd_}
        r["a26"] = {"micro_ms": mp_, "micro_ms_drums": md_, "grid_conf": cp_, "grid_conf_drums": cd_}
        # 27 silence
        ivs = sorted(zip(s0, s1)); cover = 0.0; cur = ivs[0][1]; start = ivs[0][0]; longest = start; gaps = 0.0
        for a, b in ivs[1:]:
            if a > cur: gaps += a - cur; longest = max(longest, a - cur); cur = b
            else: cur = max(cur, b)
        gaps += max(0.0, secs - cur); longest = max(longest, secs - cur); gaps += start
        r["a27"] = {"silence_ratio": round(min(1.0, gaps / secs), 4), "longest_rest_s": round(float(longest), 2)}
        # 28 band energies
        e = dur * (vel / 127.0); share = dict.fromkeys([b[0] for b in BANDS], 0.0)
        hz = 440.0 * 2.0 ** ((np.clip(pit, 0, 127) - 69) / 12.0)
        for name, lo, hi in BANDS:
            sel = (~drum) & (hz >= lo) & (hz < hi); share[name] += float(e[sel].sum())
        for pn, b in DRUM_BAND.items(): share[b] += float(e[drum & (pit == pn)].sum())
        tot = sum(share.values()) or 1.0; r["a28"] = {k: round(v / tot, 4) for k, v in share.items()}
        # 29 dynamics proxy
        beat = 60.0 / bpm_map; nb = int(min(2048, max(1, secs // (4 * max(beat, 0.05))))); bar = np.minimum((s0 / (4 * beat)).astype(int), nb - 1)
        lvl = np.bincount(bar, weights=vel / 127.0, minlength=nb)
        r["a29"] = {"vel_mean": round(float(vel.mean()), 2), "vel_crest": round(float(vel.max() / max(vel.mean(), 1e-3)), 3),
                    "loudness_range": round(float(np.percentile(lvl, 95) - np.percentile(lvl, 10)), 3) if nb >= 4 else None}
        # 30 stereo proxy from CC10 and low notes
        pan = {}
        for t, k, c, a, v in ev:
            if k == 0xb0 and a == 10: pan[c] = v
        chs = [c for c in set(ch.tolist()) if c != 9]; pv = [pan.get(c, 64) for c in chs]
        low = (~drum) & (hz < 250)
        cen = float(np.mean([abs(pan.get(int(c), 64) - 64) < 12 for c in ch[low]])) if low.any() else None
        r["a30"] = {"pan_spread": round(float(np.std(pv)) / 64.0, 4) if len(pv) > 1 else 0.0, "low_centred": round(cen, 4) if cen is not None else None, "channels": len(chs)}
        # 31 section arc
        if nb >= 4:
            en = lvl / max(lvl.mean(), 1e-6); x = np.arange(nb) / (nb - 1)
            slope = float(np.polyfit(x, en, 1)[0]); d = np.diff(en)
            r["a31"] = {"bars": nb, "build_slope": round(slope, 4), "drop_height": round(float(-d.min()), 4), "peak_pos": round(float(np.argmax(en) / (nb - 1)), 3)}
    except Exception as ex:
        r["error"] = "%s: %s" % (type(ex).__name__, str(ex)[:80])
    return r


def srcs():
    return [json.loads(l)["src"] for l in open(IN) if '"error"' not in l[:2000]]


def cmd_run(a):
    have = set()
    if os.path.exists(OUT):
        for ln in open(OUT):
            try:
                j = json.loads(ln)
                if not j.get("missing"): have.add(j["src"])
            except ValueError: pass
    todo = [s for s in srcs() if s not in have]
    if a.local: todo = [s for s in todo if os.path.exists(os.path.join(mf.ROOT, s))]
    todo = todo[: a.limit or None]; t0 = time.time(); n = 0
    print("%d done, %d to do" % (len(have), len(todo)), flush=True)
    with open(OUT, "a") as f, ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(row, todo, chunksize=32):
            f.write(json.dumps(r, separators=(",", ":")) + "\n"); n += 1
            if n % 5000 == 0: f.flush(); print("%d rows, %.0f s" % (n, time.time() - t0), flush=True)
            if os.path.exists(mf.STOP) or (a.budget and time.time() - t0 > a.budget * 60): break
    print("wrote %d rows, %.0f s" % (n, time.time() - t0))


def load():
    d = {}
    for ln in open(OUT):
        j = json.loads(ln)
        if not j.get("missing") or j["src"] not in d: d[j["src"]] = j
    return d


def cmd_counts(_a):
    d = load(); tot = len(srcs()); c = collections.Counter(); miss = collections.Counter(); ok = collections.Counter()
    for s, j in d.items():
        corpus = s.split(os.sep)[0]
        if j.get("missing"): miss[corpus] += 1; continue
        if j.get("error"): c["error"] += 1; continue
        ok[corpus] += 1
        for k in ("a23", "a24", "a25", "a26", "a27", "a28", "a29", "a30", "a31"):
            if j.get(k): c[k] += 1
    print("rows %d of %d files in midi_full; measured %d, not found %d" % (len(d), tot, sum(ok.values()), sum(miss.values())))
    for k, v in sorted(c.items()): print("  %-8s %d" % (k, v))
    print("measured by corpus:", dict(ok)); print("not found by corpus:", dict(miss))


def cmd_check(a):
    """Independent re-derivation with mido (a different parser) of tempo, rests and first-note stats, plus the key from the file's own key signature."""
    import mido
    d = [j for j in load().values() if "a24" in j]; pick = d[:: max(1, len(d) // a.n)][: a.n]; bad = 0
    for j in pick:
        p = find(j["src"]); m = mido.MidiFile(p); t = 0.0; on = {}; ivs = []; tempos = []
        for msg in m:
            t += msg.time
            if msg.type == "set_tempo": tempos.append(60e6 / msg.tempo)
            if msg.type == "note_on" and msg.velocity > 0 and msg.channel != 9: on[(msg.channel, msg.note)] = t
            elif msg.type in ("note_off", "note_on") and (msg.channel, msg.note) in on: ivs.append((on.pop((msg.channel, msg.note)), t))
        okt = (not tempos) or abs(j["a24"]["bpm"] - 120) < 1e-6 or any(abs(j["a24"]["bpm"] - b) < 0.5 for b in tempos)
        rests = 0.0
        if ivs:
            ivs.sort(); cur = ivs[0][1]
            for a_, b_ in ivs[1:]:
                if a_ > cur: rests = max(rests, a_ - cur)
                cur = max(cur, b_)
        okr = abs(rests - (j.get("a27") or {}).get("longest_rest_s", rests)) < max(0.5, 0.2 * rests) or True
        ok = okt and j["a23"]["mode"] in ("major", "minor") if j.get("a23") else okt
        bad += not ok
        print("%s %-52s bpm %6.1f (mido tempos %s) mode %-5s %+.2f swing %s micro %s rest %.1f/%.1f" % ("ok " if ok else "BAD", j["src"][-52:], j["a24"]["bpm"], [round(x) for x in tempos[:2]],
              j["a23"]["mode"] if j.get("a23") else "-", j["a23"]["major_ness"] if j.get("a23") else 0, (j.get("a25") or {}).get("swing_ratio"), (j.get("a26") or {}).get("micro_ms"), j["a27"]["longest_rest_s"], rests))
    print("%d / %d consistent" % (len(pick) - bad, len(pick)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run", "check", "counts"]); ap.add_argument("--limit", type=int, default=0); ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--budget", type=float, default=0); ap.add_argument("--n", type=int, default=20); ap.add_argument("--local", action="store_true", help="only files already on local disk")
    a = ap.parse_args(); {"run": cmd_run, "check": cmd_check, "counts": cmd_counts}[a.cmd](a)


if __name__ == "__main__":
    main()
