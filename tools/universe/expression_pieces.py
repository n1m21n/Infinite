#!/usr/bin/env python3
"""Pitch, glide and note-expression pieces, gated.
    expression_pieces.py [--midi DIR ...] [--out expression_pieces.jsonl]
  glide_params   glide / glideMs / pitchBend / bendSemitones / slide knobs of every node in the hand-made patches,
                 checked against the node's own min/max in describe_all.json
  melody_contour 16-note windows of a melody as semitone steps (MIDI files: monophonic top line per channel)
  note_expression per-note velocity, pitch-bend and pressure/CC74 series of a window (only where the file has them)
MIDI input is the personal files patches point at (~/Downloads/*.mid); they are third-party songs, so they are tagged
local-only and never go into anything shipped."""
import argparse, collections, glob, json, math, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P
import patch_pieces as PP

HERE = os.path.dirname(os.path.abspath(__file__))
KEYS = re.compile(r"^(glide|glideMs|pitchBend|bendSemitones|slide)$")


def ranges():
    out = {}
    for t in json.load(open(os.path.join(HERE, "describe_all.json")))["types"]:
        for r in t["rows"]:
            out[(t["type"], r["key"])] = (r["min"], r["max"])
    return out


def glide_pieces():
    R = ranges()
    for kind, f, t in PP.sources():
        for n in PP.parse(t):
            vals = {k: v for k, v in n["p"].items() if KEYS.match(k) and not isinstance(v, str)}
            if vals:
                yield {"type": "glide_params", "src": "%s:%s#%d" % (kind, os.path.relpath(f, PP.HOME), n["id"]),
                       "family": kind, "node": n["type"], "values": vals,
                       "limits": {k: R.get((n["type"], k)) for k in vals}}


def gate_glide(p):
    h, s = [], []
    for k, v in p["values"].items():
        lo, hi = (p["limits"].get(k) or (None, None))
        if not math.isfinite(v): h.append("%s not finite" % k)
        elif lo is not None and v < lo: h.append("%s below min" % k)
        elif hi is not None and v > hi: h.append("%s above max" % k)
    if all(v == 0 for v in p["values"].values()): s.append("every value at 0 (default)")
    return h, s


# ---- MIDI with bend, pressure and CC74 --------------------------------------------------------

def parse_midi(path):
    b = open(path, "rb").read()
    if b[:4] != b"MThd":
        raise ValueError("not midi")
    div = struct.unpack(">H", b[12:14])[0]
    pos, tracks = 14, []
    while pos + 8 <= len(b):
        tag, ln = b[pos:pos + 4], struct.unpack(">I", b[pos + 4:pos + 8])[0]
        pos += 8
        if tag == b"MTrk":
            tracks.append(_events(b[pos:pos + ln]))
        pos += ln
    return tracks, div


def _events(d):
    i, t, run, ev = 0, 0, 0, []

    def vlq():
        nonlocal i
        v = 0
        while True:
            c = d[i]; i += 1
            v = (v << 7) | (c & 0x7f)
            if not c & 0x80:
                return v
    while i < len(d):
        t += vlq()
        s = d[i]
        if s == 0xff:
            mt = d[i + 1]
            i += 2
            n = vlq()  # vlq() advances i itself, so read it before adding
            if mt == 0x58 and n >= 2:
                ev.append((t, 0xff, 0, d[i], d[i + 1]))  # time signature: numerator, log2(denominator)
            elif mt == 0x51 and n == 3:
                ev.append((t, 0xff, 1, int.from_bytes(d[i:i + 3], "big"), 0))  # tempo: microseconds per quarter
            i += n
            continue
        if s in (0xf0, 0xf7):
            i += 1
            n = vlq()
            i += n
            continue
        if s & 0x80:
            run = s; i += 1
        k, ch = run & 0xf0, run & 0x0f
        n = 1 if k in (0xc0, 0xd0) else 2
        a = d[i:i + n]; i += n
        if len(a) < n:
            break
        ev.append((t, k, ch, a[0], a[1] if n == 2 else 0))
    return ev


def windows(path, size=16, parsed=None):
    tracks, div = parsed or parse_midi(path)
    for ti, ev in enumerate(tracks):
        for ch in sorted({e[2] for e in ev if e[1] == 0x90 and e[2] != 9}):
            ons = [(t, a, v) for t, k, c, a, v in ev if k == 0x90 and c == ch and v > 0]
            if len(ons) < size:
                continue
            top = {}
            for t, a, v in ons:  # monophonic top line: highest pitch per onset
                if t not in top or a > top[t][0]:
                    top[t] = (a, v)
            seq = sorted(top.items())
            bends = [(t, ((a | (v << 7)) - 8192) / 8192.0) for t, k, c, a, v in ev if k == 0xe0 and c == ch]
            press = [(t, a / 127.0) for t, k, c, a, v in ev if k == 0xd0 and c == ch]
            slide = [(t, v / 127.0) for t, k, c, a, v in ev if k == 0xb0 and c == ch and a == 74]
            for w in range(0, len(seq) - size + 1, size):
                chunk = seq[w:w + size]
                t0, t1 = chunk[0][0], chunk[-1][0] + div
                inside = lambda xs: [round(x, 3) for t, x in xs if t0 <= t < t1]
                yield {"pitches": [c[1][0] for c in chunk], "vel": [c[1][1] for c in chunk],
                       "bend": inside(bends), "pressure": inside(press), "slide": inside(slide),
                       "track": ti, "channel": ch, "bar_pos": round(t0 / (div * 4.0), 2)}


def midi_pieces(dirs):
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(os.path.expanduser(d), "*.mid*"))):
            try:
                ws = list(windows(f))
            except Exception:
                continue
            for k, w in enumerate(ws):
                name = "midi:%s#%d" % (os.path.basename(f), k)
                steps = [b - a for a, b in zip(w["pitches"], w["pitches"][1:])]
                yield {"type": "melody_contour", "src": name, "family": "local", "pitches": w["pitches"], "steps": steps,
                       "license": "third-party, local only"}
                yield {"type": "note_expression", "src": name, "family": "local", "vel": w["vel"], "bend": w["bend"],
                       "pressure": w["pressure"], "slide": w["slide"], "license": "third-party, local only"}


def gate_contour(p):
    h, s = [], []
    P_ = p["pitches"]
    if max(P_) - min(P_) > 36: h.append("range over 36 semitones")
    if max(abs(x) for x in p["steps"]) > 24: h.append("a leap over 24 semitones")
    if len(set(P_)) < 3: h.append("fewer than 3 distinct pitches")
    if sum(abs(x) > 7 for x in p["steps"]) / len(p["steps"]) > 0.4: s.append("over 40% leaps larger than a fifth")
    if sum(x == 0 for x in p["steps"]) / len(p["steps"]) > 0.6: s.append("over 60% repeated notes")
    return h, s


def gate_expr(p):
    h, s = [], []
    if any(not 1 <= v <= 127 for v in p["vel"]): h.append("velocity outside 1..127")
    if any(abs(x) > 1.0 for x in p["bend"]): h.append("bend outside -1..1")
    if len(set(p["vel"])) == 1: s.append("flat velocity")
    if not p["bend"] and not p["pressure"] and not p["slide"]: s.append("no bend, pressure or slide in the file")
    return h, s


GATES = {"glide_params": gate_glide, "melody_contour": gate_contour, "note_expression": gate_expr}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--midi", action="append", default=["~/Downloads"])
    ap.add_argument("--out", default=os.path.join(HERE, "expression_pieces.jsonl"))
    a = ap.parse_args()
    rows, seen = [], set()
    for p in [*glide_pieces(), *midi_pieces(a.midi)]:
        h, s = GATES[p["type"]](p)
        key = P.digest({k: v for k, v in p.items() if k not in ("src", "family")})
        if not h and key in seen:
            h = ["duplicate of an earlier piece"]
        seen.add(key)
        p.update(id=key, hard=h, soft=s, ok=not h)
        rows.append(p)
    with open(a.out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print("%-16s %6s %6s %6s  %s" % ("type", "total", "ok", "warn", "hard fails"))
    for t in GATES:
        r = [x for x in rows if x["type"] == t]
        why = collections.Counter(x for q in r for x in q["hard"])
        print("%-16s %6d %6d %6d  %s" % (t, len(r), sum(q["ok"] for q in r), sum(q["ok"] and bool(q["soft"]) for q in r),
                                         "; ".join("%s x%d" % kv for kv in why.most_common(4)) or "-"))
    print("warnings:", dict(collections.Counter(x for q in rows if q["ok"] for x in q["soft"]).most_common(6)))
    e = [q for q in rows if q["type"] == "note_expression" and q["ok"]]
    print("expression windows with bend:", sum(bool(q["bend"]) for q in e), " pressure:", sum(bool(q["pressure"]) for q in e),
          " slide(CC74):", sum(bool(q["slide"]) for q in e), " of", len(e))
    g = collections.Counter(q["node"] for q in rows if q["type"] == "glide_params" and q["ok"])
    print("glide ok by node:", dict(g.most_common(8)))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
