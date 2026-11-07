#!/usr/bin/env python3
"""Render one instrument node playing a short fixed phrase, for listening to DSP changes (never one-shots).
    phrase_render.py --node Piano --out a.wav [--bin dev|release|<path>] [--set hardness=0.7,tone=0.6]
The phrase (about 12 s): a velocity ladder on one key, fast repeats, a legato line, a chord held and released, a low
forte note left to ring, written as a Standard MIDI File and played through the MIDI File node (the way a patch should lay
down notes, chords and expression; --via events uses the headless note list instead). The project tempo rules the file:
BPM below is the headless default. Default parameters come from `Infinite --describe`; --set overrides them. Exit 0."""
import argparse, json, os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import own_sweeps as osw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
BINS = {"dev": os.path.join(ROOT, "build-dev/Infinite.app/Contents/MacOS/Infinite"),
        "release": os.path.join(ROOT, "build/Infinite.app/Contents/MacOS/Infinite")}


def phrase():
    """[(t s, midi, velocity 0-1, length s)]"""
    ev, t = [], 0.3
    for v in (0.25, 0.5, 0.75, 1.0):  # velocity ladder on C4
        ev.append((t, 60, v, 0.45)); t += 0.6
    t += 0.2
    for k in range(6):  # repeats on E4
        ev.append((t, 64, 0.62 + 0.04 * (k % 2), 0.12)); t += 0.16
    t += 0.4
    for m, v in zip((60, 62, 64, 67, 72), (0.55, 0.6, 0.65, 0.7, 0.75)):  # legato line, notes overlap 60 ms
        ev.append((t, m, v, 0.41)); t += 0.35
    t += 0.5
    for m in (48, 55, 64):  # chord held, then released
        ev.append((t, m, 0.7, 1.6))
    t += 2.2
    ev.append((t, 36, 0.95, 2.5)); t += 3.2  # low forte, long ring
    return ev, t


BPM, PPQ = 120.0, 480


def write_mid(ev, path):
    """Type-0 SMF, one track: note on/off with velocity, times in project beats."""
    msgs = []
    for t, m, v, ln in ev:
        on, off = round(t * BPM / 60 * PPQ), round((t + ln) * BPM / 60 * PPQ)
        msgs += [(on, 1, bytes([0x90, m, max(1, min(127, round(v * 127)))])), (off, 0, bytes([0x80, m, 64]))]
    msgs.sort(key=lambda x: (x[0], x[1]))
    def vlq(n):
        b = [n & 0x7F]; n >>= 7
        while n:
            b.insert(0, (n & 0x7F) | 0x80); n >>= 7
        return bytes(b)
    trk, last = b"", 0
    for tick, _, data in msgs:
        trk += vlq(tick - last) + data; last = tick
    trk += b"\x00\xff\x2f\x00"
    open(path, "wb").write(b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big") + (1).to_bytes(2, "big") + PPQ.to_bytes(2, "big")
                           + b"MTrk" + len(trk).to_bytes(4, "big") + trk)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--node", default="Piano"); ap.add_argument("--out", required=True)
    ap.add_argument("--bin", default="dev"); ap.add_argument("--set", default=""); ap.add_argument("--via", default="midi", choices=["midi", "events"])
    a = ap.parse_args()
    binp = BINS.get(a.bin, a.bin); os.environ.setdefault("INFINITE_BIN", binp)
    t, sw, fixed = osw.param_table(a.node)
    rec = dict(fixed); rec.update({p["key"]: p["default"] for p in sw})
    for kv in filter(None, a.set.split(",")):
        k, v = kv.split("="); rec[k] = float(v)
    tags = osw.tag_of(t)
    ev, dur = phrase()
    with tempfile.TemporaryDirectory() as d:
        inf, js = os.path.join(d, "p.inf"), os.path.join(d, "s.json")
        txt = osw.patch_text(a.node, tags, dict(sorted(rec.items())))
        cmd = [binp, "--audio-summary", inf, js, "--duration", "%g" % dur, "--sample-rate", "48000", "--wav", os.path.abspath(a.out), "--lenient"]
        if a.via == "midi":
            mid = os.path.join(d, "phrase.mid"); write_mid(ev, mid)
            txt = txt.replace("node 1 Notes Keyboard\n  id keys\n", "node 1 Notes MIDI File\n  id keys\n  s path %s\n  b loop 0\n" % mid)
        else:
            types = {"n%d" % m: {"node": "syn", "pitch": m, "velocity_from": "v", "length_from": "len"} for m in {e[1] for e in ev}}
            evs = [{"t": round(tt, 4), "type": "n%d" % m, "v": v, "len": ln} for tt, m, v, ln in ev]
            ep, mp = os.path.join(d, "events.json"), os.path.join(d, "map.json")
            json.dump(evs, open(ep, "w")); json.dump(types, open(mp, "w")); cmd += ["--notes", ep, "--note-map", mp]
        open(inf, "w").write(txt)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if not os.path.exists(a.out):
        print(r.stdout[-2000:], r.stderr[-2000:]); return 1
    print("wrote %s (%.1f s, %d notes)" % (a.out, dur, len(ev)))


if __name__ == "__main__":
    sys.exit(main() or 0)
