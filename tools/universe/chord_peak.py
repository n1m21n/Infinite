#!/usr/bin/env python3
"""Headroom check: an ff 4-note chord held 2 s on one instrument node at default parameters; prints peak dBFS.
    chord_peak.py --node Piano [--bin dev] [--set k=v,...]   (exit 1 if the peak is above -1 dBFS)"""
import argparse, os, subprocess, sys, tempfile, math, wave, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import phrase_render as pr  # noqa: E402

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", default="Piano"); ap.add_argument("--bin", default="dev")
    ap.add_argument("--set", default=""); ap.add_argument("--vel", type=float, default=1.0); a = ap.parse_args()
    pr.phrase = lambda: ([(0.2, m, a.vel, 2.0) for m in (48, 55, 64, 72)], 3.5)
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "c.wav")
        sys.argv = ["x", "--node", a.node, "--out", out, "--bin", a.bin, "--set", a.set]
        pr.main()
        w = wave.open(out); n = w.getnframes(); raw = w.readframes(n); ch, sw = w.getnchannels(), w.getsampwidth()
        if sw == 2: vals = struct.unpack("<%dh" % (n * ch), raw); pk = max(abs(x) for x in vals) / 32768
        elif sw == 3: pk = max(abs(int.from_bytes(raw[i:i+3], "little", signed=True)) for i in range(0, len(raw), 3)) / 8388608
        else: vals = struct.unpack("<%df" % (n * ch), raw); pk = max(abs(x) for x in vals)
    db = 20 * math.log10(max(pk, 1e-9)); print("%s ff chord peak %.2f dBFS" % (a.node, db)); return 1 if db > -1 else 0
if __name__ == "__main__": sys.exit(main())
