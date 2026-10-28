#!/usr/bin/env python3
"""G3: the sample library as full data. Re-measure every file of features.jsonl with detail.measure (partials, YIN f0, tuning, inharmonicity,
odd/even, formants, space, vibrato, noise, release tail, mel128) plus time curves of the whole file.
    samples_v3.py run   [--limit N] [--jobs 3] [--budget MIN] [--out features_v3.jsonl]    resumes by src; stop file ~/STOP_V3
    samples_v3.py check [--n 50]                                                            50 files measured twice must match; pitch vs the key in the file name
One-shots (not loop, <= 8 s) are measured as a note: window = file start, hold = min(1.5, 0.6 x length); loops get curves only (heavy=False
detail is for a loop meaningless, so they skip partials). Curves: 100 ms rms (dB, u8 x2), 100 ms centroid (Hz), up to 60 s of the file.
Rows are local only (features_v3.jsonl never leaves the machine)."""
import argparse, glob, json, math, os, re, sys, time
import numpy as np
from concurrent.futures import ProcessPoolExecutor, as_completed
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import audio_features as af, detail as det  # noqa: E402

ROOT = os.path.expanduser("~/Infinite/training-data/audio")
SRC = os.path.join(ROOT, "features.jsonl"); OUT = os.path.join(ROOT, "features_v3.jsonl"); STOP = os.path.expanduser("~/STOP_V3")
NOTE = re.compile(r"(?<![A-Za-z])([A-G])([#b]?)(-?[0-8])?(?![A-Za-z#])"); PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def name_midi(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    for m in NOTE.finditer(stem):
        pc = PC[m.group(1)] + (1 if m.group(2) == "#" else -1 if m.group(2) == "b" else 0)
        return (pc % 12, (int(m.group(3)) + 1) * 12 + pc if m.group(3) is not None else None)
    return None


def curves(x, rate, step=0.1, maxs=60):
    x = x[: int(maxs * rate)]; n = int(step * rate); k = len(x) // n
    if k < 2: return {}
    fr = x[: k * n].reshape(k, n); rms = 20 * np.log10(np.sqrt((fr ** 2).mean(1)) + 1e-9)
    w = np.hanning(n); sp = np.abs(np.fft.rfft(fr * w, axis=1)); f = np.fft.rfftfreq(n, 1 / rate)
    cen = (sp * f).sum(1) / (sp.sum(1) + 1e-12)
    return {"rms": [int(round(v)) for v in np.clip(rms, -90, 6)], "centroid": [int(round(v)) for v in np.clip(cen, 0, 20000)]}


def measure_file(src):
    r = {"src": src}
    try:
        st = af.decode(src, 60)
        if st is None: return dict(r, error="decode")
        x = st.mean(0); rate = af.FS; r["seconds"] = round(len(x) / rate, 3); r["curves"] = curves(x, rate)
        nm = name_midi(src); r["name_pc"] = nm[0] if nm else None; r["name_midi"] = nm[1] if nm else None
        if len(x) <= 8 * rate and len(x) >= 2 * rate:
            midi = nm[1] if nm and nm[1] is not None else 60
            hold = min(1.5, 0.6 * len(x) / rate)
            r["detail"] = det.measure(x[: int(rate * (hold + 1.0))] if False else x, rate, midi, hold=hold)
        elif len(x) < 2 * rate and len(x) > 0.2 * rate:
            pad = np.concatenate([x, np.zeros(int(2.05 * rate) - len(x))]); r["detail"] = det.measure(pad, rate, name_midi(src)[1] if nm and nm[1] else 60, hold=min(1.5, 0.6 * len(x) / rate))
        d = r.get("detail")
        if d and d.get("f0"):
            r["pitch_pc"] = int(round(12 * math.log2(d["f0"] / 440.0) + 69)) % 12
    except Exception as e:  # keep the row, say why
        r["error"] = "%s: %s" % (type(e).__name__, str(e)[:80])
    return r


def srcs():
    for ln in open(SRC):
        yield json.loads(ln)["src"]


def cmd_run(a):
    have = set()
    if os.path.exists(a.out):
        for ln in open(a.out):
            try: have.add(json.loads(ln)["src"])
            except ValueError: pass
    todo = [s for s in srcs() if s not in have][: a.limit or None]; t0 = time.time(); n = 0
    print("%d done, %d to do" % (len(have), len(todo)), flush=True)
    with open(a.out, "a") as f, ProcessPoolExecutor(a.jobs) as ex:
        it = iter(todo); live = set()
        def feed():
            for s in it:
                live.add(ex.submit(measure_file, s))
                if len(live) >= a.jobs * 4: return
        feed()
        while live:
            for fu in as_completed(list(live)):
                live.discard(fu); f.write(json.dumps(fu.result(), separators=(",", ":")) + "\n"); n += 1
                if n % 200 == 0: f.flush(); print("%d rows, %.0f s" % (n, time.time() - t0), flush=True)
                if os.path.exists(STOP) or (a.budget and time.time() - t0 > a.budget * 60): live.clear(); break
                feed(); break
    print("wrote %d rows" % n)


def cmd_check(a):
    rows = {}
    for ln in open(a.out):
        r = json.loads(ln)
        if r.get("detail"): rows[r["src"]] = r
    pick = sorted(rows)[:: max(1, len(rows) // a.n)][: a.n]
    same = sum(measure_file(s) == rows[s] for s in pick)
    print("repeat: %d / %d identical" % (same, len(pick)))
    named = [r for r in rows.values() if r.get("name_midi") is not None and r.get("pitch_pc") is not None]   # single notes only: pc-only names are loop keys / chord labels
    ok = sum(r["name_pc"] == r["pitch_pc"] for r in named)
    print("pitch class vs file name: %d / %d = %.1f%% (bar 90)" % (ok, len(named), 100 * ok / max(1, len(named))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run", "check"]); ap.add_argument("--limit", type=int, default=0); ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--budget", type=float, default=0); ap.add_argument("--out", default=OUT); ap.add_argument("--n", type=int, default=50)
    a = ap.parse_args(); {"run": cmd_run, "check": cmd_check}[a.cmd](a)


if __name__ == "__main__":
    main()
