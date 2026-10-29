#!/usr/bin/env python3
"""Note gate: generator lines (16-step Note Sequencers with >= 3 pitches) vs POP909 MELODY bars (16th grid, not a bank source).
Per bar: notes, mean |interval|, step share (<= 2 st), repeat share, range, leap share (> 7 st), downbeat-onset share.
Score = sum over axes of |gen median - ref median| / ref IQR (lower is closer to how real melodies move).
    node wasm_part_patches.js build-wasm 3 1,3,6,9,10 > new.jsonl   (and the same for the old build)
    ~/Infinite/ml-venv/bin/python3 -I line_gate.py old.jsonl new.jsonl 1,3,6,9,10"""
import glob, json, os, re, struct, sys, random
import numpy as np

def tracks(path):
    b = open(path, "rb").read(); div = struct.unpack(">H", b[12:14])[0]; pos = 14; out = []
    while pos + 8 <= len(b):
        tag, ln = b[pos:pos+4], struct.unpack(">I", b[pos+4:pos+8])[0]; pos += 8
        if tag == b"MTrk":
            d, i, t, run, name, ns = b[pos:pos+ln], 0, 0, 0, "", []
            def vlq():
                nonlocal i
                v = 0
                while True:
                    c = d[i]; i += 1; v = (v << 7) | (c & 0x7f)
                    if not c & 0x80: return v
            while i < len(d):
                t += vlq(); s = d[i]
                if s == 0xff:
                    ty = d[i+1]; i += 2; n = vlq()
                    if ty == 3: name = d[i:i+n].decode("latin1")
                    i += n; continue
                if s in (0xf0, 0xf7): i += 1; n = vlq(); i += n; continue
                if s & 0x80: run = s; i += 1
                k = run & 0xf0; n = 1 if k in (0xc0, 0xd0) else 2; a = d[i:i+n]; i += n
                if k == 0x90 and len(a) == 2 and a[1] > 0: ns.append((t, a[0]))
            out.append((name, ns))
        pos += ln
    return out, div

def stats(cells):
    """cells: 16 pitches or None."""
    ps = [p for p in cells if p is not None]
    if len(ps) < 3: return None
    iv = np.abs(np.diff(ps))
    return [len(ps), iv.mean(), (iv <= 2).mean() - (iv == 0).mean(), (iv == 0).mean(), max(ps) - min(ps), (iv > 7).mean(),
            np.mean([cells[i] is not None for i in (0, 4, 8, 12)])]

def ref_bars(n=4000):
    fs = sorted(glob.glob(os.path.expanduser("~/Infinite/datasets/midi/data/pop909_songs/POP909/*/[0-9]*.mid")))
    random.Random(1).shuffle(fs); rows = []
    for f in fs[:300]:
        trs, div = tracks(f)
        for name, ns in trs:
            if name.upper() != "MELODY": continue
            bars = {}
            for t, p in ns:
                s = int(round(t / (div / 4))); c = bars.setdefault(s // 16, [None] * 16)
                c[s % 16] = max(p, c[s % 16] or 0)
            rows += [r for r in (stats(c) for c in bars.values()) if r]
    return np.array(rows[:n])

def gen_bars(path, parts):
    rows = {}
    for line in open(path):
        r = json.loads(line)
        if r["part"] not in parts: continue
        for node in re.split(r"\nnode ", r["patch"]):
            if not node.split("\n")[0].endswith("Note Sequencer"): continue
            kv = dict(re.findall(r"^\s+\w (\S+) (\S+)$", node, re.M))
            if kv.get("steps") != "16": continue
            cells = [int(kv["note%d" % k]) if kv.get("en%d" % k) == "1" else None for k in range(16)]
            if len(set(p for p in cells if p is not None)) < 3: continue
            s = stats(cells)
            if s: rows.setdefault(r["part"], []).append(s)
    return {k: np.array(v) for k, v in rows.items()}

AX = ["notes", "|int|", "steps", "repeats", "range", "leaps", "downbeats"]
ref = ref_bars(); rm = np.median(ref, 0); iqr = np.subtract(*np.percentile(ref, [75, 25], 0)); iqr[iqr == 0] = 0.1
print("ref POP909 melody bars %d  medians %s" % (len(ref), " ".join("%s %.2f" % a for a in zip(AX, rm))))
for path in sys.argv[1:-1]:
    g = gen_bars(path, set(map(int, sys.argv[-1].split(","))))
    for part, x in sorted(g.items()):
        z = (np.median(x, 0) - rm) / iqr
        print("%-28s part %2d n %4d  score %.2f | %s" % (os.path.basename(path), part, len(x), np.abs(z).sum(), " ".join("%s %+.1f" % a for a in zip(AX, z))))
