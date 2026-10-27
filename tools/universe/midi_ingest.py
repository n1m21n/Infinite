#!/usr/bin/env python3
"""Inbox MIDI -> gated pieces.
    midi_ingest.py [--inbox ~/Infinite/training-data/inbox] [--store ~/Infinite/training-data/store] [--cap 0] [--jobs 4]
Layout: inbox/<source>/**/*.mid plus an optional SOURCE.txt with lines `family: house` and `license: CC-BY-4.0`.
A source with no licence line is `unknown`: it trains the local model only. A folder or file name containing a
family word (house, techno, trance, dnb, breaks, ambient ...) sets the family when SOURCE.txt does not.
Every file is read once (identical bytes in two sources count once). From each 4/4 file:
  drum_groove   channel 10 bars, with each hit's velocity (vel, 0-9) and its distance from the 16th grid (off)
  bass_seq      the bass channel (GM program 32-39, else the lowest busy channel) as 16 scale degrees per bar
  chord_prog    one triad per bar from every pitched channel, repeats merged, as numerals in the detected key
and from every file: melody_contour + note_expression windows, tagged with a role from the median pitch.
Bars are spread over the song, not taken from the start. Chord-pack folders are left to chord_pieces.py."""
import argparse, collections, glob, hashlib, json, multiprocessing, os, re, signal, statistics, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P
import expression_pieces as E
import public_drums as D
import chord_pieces as C

FAMILY_WORDS = {"house": "house", "techno": "techno", "trance": "trance", "dnb": "drum_and_bass", "drum_and_bass": "drum_and_bass",
                "jungle": "drum_and_bass", "breaks": "breaks", "breakbeat": "breaks", "ambient": "ambient", "downtempo": "downtempo",
                "dubstep": "bass_music", "bass": "bass_music", "synthwave": "retro", "electro": "retro", "lofi": "downtempo",
                "hiphop": "hiphop", "trap": "hiphop", "disco": "house", "garage": "house", "idm": "experimental"}
# chord packs: their file names carry the progression, chord_pieces.py reads them
SKIP_SOURCES = {"chord_progressions", "chords_and_scales", "modal_chord_progressions"}
# Krumhansl-Kessler key profiles
KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]
SCALE = {"Major": [0, 2, 4, 5, 7, 9, 11], "Minor": [0, 2, 3, 5, 7, 8, 10]}
ROMAN = ["i", "ii", "iii", "iv", "v", "vi", "vii"]


def source_meta(folder):
    meta = {"family": None, "license": "unknown"}
    f = os.path.join(folder, "SOURCE.txt")
    if os.path.exists(f):
        for ln in open(f, errors="replace"):
            m = re.match(r"\s*(family|license|licence)\s*:\s*(.+?)\s*$", ln, re.I)
            if m:
                meta["family" if m[1].lower() == "family" else "license"] = m[2]
    return meta


def guess_family(path, meta):
    if meta["family"]:
        return meta["family"]
    toks = set(re.split(r"[^a-z0-9]+", path.lower()))  # whole words only: "bass" must not match "bassoon" or "contrabass"
    for w, fam in FAMILY_WORDS.items():
        if w in toks:
            return fam
    return "unlabeled"


def meta(tracks):
    sig, tempo = (4, 2), 500000
    for ev in tracks:
        for t, k, ch, a, v in ev:
            if k == 0xff and ch == 0 and t == 0 and sig == (4, 2):
                sig = (a, v)
            if k == 0xff and ch == 1 and tempo == 500000:
                tempo = a
    return sig, 60e6 / max(1, tempo)


def spread(items, n):
    """n items spread evenly over the list, so a song gives verses and choruses, not its intro n times"""
    return items if len(items) <= n else [items[i * len(items) // n] for i in range(n)]


def notes(tracks):
    """(on, off, pitch, channel, velocity, program) for every pitched or drum note, all tracks merged."""
    ev = sorted((e for tr in tracks for e in tr if e[1] in (0x80, 0x90, 0xc0)), key=lambda e: e[0])
    prog, held, out = [0] * 16, {}, []
    for t, k, ch, a, v in ev:
        if k == 0xc0:
            prog[ch] = a
        elif k == 0x90 and v > 0:
            held.setdefault((ch, a), []).append((t, v, prog[ch]))
        elif (ch, a) in held and held[(ch, a)]:
            t0, v0, p0 = held[(ch, a)].pop(0)
            out.append((t0, max(t, t0 + 1), a, ch, v0, p0))
    return out


def key_of(ns, div):
    """(root pitch class, "Major" | "Minor") by Krumhansl-Kessler on duration-weighted pitch classes."""
    w = [0.0] * 12
    for on, off, a, ch, v, p in ns:
        if ch != 9:
            w[a % 12] += min(off - on, div) / div
    if sum(w) <= 0:
        return None

    def corr(prof, r):
        x = [w[(r + i) % 12] for i in range(12)]
        mx, mp = sum(x) / 12, sum(prof) / 12
        num = sum((a - mx) * (b - mp) for a, b in zip(x, prof))
        den = (sum((a - mx) ** 2 for a in x) * sum((b - mp) ** 2 for b in prof)) ** 0.5
        return num / den if den else 0.0
    return max(((r, m) for r in range(12) for m in ("Major", "Minor")),
               key=lambda rm: corr(KK_MAJOR if rm[1] == "Major" else KK_MINOR, rm[0]))


def drum_bars(ns, div, per_file):
    """one-bar windows of the drum channel with velocity and grid offset per hit. Bars with under 85% of hits near
    the 16th grid are dropped (triplets: their lanes would be wrong). Bar 0 is skipped when the file has more than
    3 bars (count-ins, pickups). vel: '0'-'9' per hit. off: 'a'-'q' = -8..+8 sixteenths of a sixteenth (half a step either way), 'i' on grid."""
    sixteenth = div / 4.0
    grid = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    vel = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    off = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    near = collections.defaultdict(lambda: [0, 0])
    for on, _, a, ch, v, _ in ns:
        if ch != 9:
            continue
        for li, lane_notes in enumerate(D.LANE_NOTES):
            if a in lane_notes:
                x = on / sixteenth
                s = round(x)
                b, k = s // 16, s % 16
                vq = str(min(9, round(v / 127 * 9)))
                if grid[b][li][k] == "." or vq > vel[b][li][k]:  # two hits on one cell: keep the louder
                    grid[b][li][k], vel[b][li][k] = "x", vq
                    off[b][li][k] = chr(ord("i") + max(-8, min(8, round((x - s) * 16))))
                near[b][0] += abs(x - s) <= 0.25
                near[b][1] += 1
    keep = []
    for b in sorted(grid):
        if b == 0 and len(grid) > 3:
            continue
        n, tot = near[b]
        if tot and n / tot < 0.85:
            continue
        lanes = ["".join(l) if "x" in l else "" for l in grid[b]]
        if any(lanes):
            keep.append(b)
    for b in spread(keep, per_file):
        has = ["x" in l for l in grid[b]]
        yield (["".join(l) if h else "" for l, h in zip(grid[b], has)], ["".join(l) if h else "" for l, h in zip(vel[b], has)],
               ["".join(l) if h else "" for l, h in zip(off[b], has)])


def bass_channel(ns):
    by = collections.defaultdict(list)
    for on, off, a, ch, v, p in ns:
        if ch != 9:
            by[ch].append((a, p))
    gm = [ch for ch, xs in by.items() if len(xs) >= 32 and statistics.mode(p for _, p in xs) in range(32, 40)]
    if gm:
        return max(gm, key=lambda ch: len(by[ch]))
    low = [ch for ch, xs in by.items() if len(xs) >= 32 and statistics.median(a for a, _ in xs) < 50]
    return min(low, key=lambda ch: statistics.median(a for a, _ in by[ch])) if low else None


def degree(pitch, root, mode, base):
    """scale degree from `base` (the tonic at or under the line's lowest note), 7 = an octave up; None if chromatic"""
    rel = pitch - base
    pc = (pitch - root) % 12
    sc = SCALE[mode]
    return sc.index(pc) + 7 * (rel // 12) if pc in sc else None


def bass_bars(ns, div, key, per_file):
    ch = bass_channel(ns)
    if ch is None or key is None:
        return
    root, mode = key
    line = [(on, a) for on, _, a, c, _, _ in ns if c == ch]
    lo = min(a for _, a in line)
    base = lo - (lo - root) % 12
    sixteenth = div / 4.0
    bars = collections.defaultdict(dict)
    for on, a in line:
        s = round(on / sixteenth)
        b, k = s // 16, s % 16
        if k not in bars[b] or a < bars[b][k]:
            bars[b][k] = a  # lowest note on a step
    keep = []
    for b in sorted(bars):
        steps = ["."] * 16
        chrom = 0
        for k, a in bars[b].items():
            d = degree(a, root, mode, base)
            if d is None:
                chrom += 1
            elif d <= 9:
                steps[k] = str(d)
        if sum(c != "." for c in steps) >= 2 and chrom <= 0.25 * len(bars[b]):
            keep.append("".join(steps))
    for st in spread(keep, per_file):
        yield st


def chord_bars(ns, div, key, bass_ch):
    """one triad per bar by template match on duration-weighted pitch classes (the bass counts double for the root);
    None where the best triad covers under 60% of the bar's weight"""
    root, mode = key
    bar = div * 4
    w = collections.defaultdict(lambda: [0.0] * 12)
    for on, off, a, ch, v, p in ns:
        if ch == 9:
            continue
        for b in range(on // bar, min(off - 1, on + 4 * bar) // bar + 1):
            lap = min(off, (b + 1) * bar) - max(on, b * bar)
            if lap > 0:
                w[b][a % 12] += lap / bar * (2.0 if ch == bass_ch else 1.0)
    out = []
    for b in range(max(w) + 1 if w else 0):
        x = w.get(b)
        tot = sum(x) if x else 0.0
        if tot <= 0:
            out.append(None)
            continue
        best = max(((r, q) for r in range(12) for q in (4, 3)), key=lambda rq: x[rq[0]] * 1.2 + x[(rq[0] + rq[1]) % 12] + x[(rq[0] + 7) % 12])
        r, q = best
        cover = (x[r] + x[(r + q) % 12] + x[(r + 7) % 12]) / tot
        out.append((r, q == 3) if cover >= 0.6 else None)
    return out


def numeral(r, minor, root, mode):
    pc = (r - root) % 12
    sc = SCALE[mode]
    if pc in sc:
        acc, d = "", sc.index(pc)
    elif (pc + 1) % 12 in sc:
        acc, d = "b", sc.index((pc + 1) % 12)
    else:
        acc, d = "#", sc.index((pc - 1) % 12)
    return acc + (ROMAN[d] if minor else ROMAN[d].upper())


def progressions(ns, div, key, bass_ch, per_file=2, length=4):
    """runs of `length` distinct consecutive chords (repeats merged), spread over the song"""
    root, mode = key
    runs, cur = [], []
    for c in chord_bars(ns, div, key, bass_ch):
        if c is None:
            cur = []
            continue
        if cur and cur[-1] == c:
            continue
        cur.append(c)
        if len(cur) == length:
            runs.append(cur)
            cur = []
    for run in spread(runs, per_file):
        yield [numeral(r, m, root, mode) for r, m in run]


def pick(files, cap):
    """cap files per source, chosen by hash of the path so a rerun picks the same ones"""
    if not cap or len(files) <= cap:
        return files
    return sorted(files, key=lambda f: hashlib.sha1(f.encode()).hexdigest())[:cap]


def _too_slow(*_):
    raise TimeoutError


def one_file(job):
    """every piece one file gives, ungated; [] when unreadable or over 5 s (a few broken files hold notes for hours)"""
    f, name, fam, lic, per_file, windows = job
    out = []
    signal.signal(signal.SIGALRM, _too_slow)
    signal.alarm(5)
    try:
        tracks, div = E.parse_midi(f)
        sig, bpm = meta(tracks)
        ns = notes(tracks)
        if sig == (4, 2) and ns:
            for i, (lanes, vel, off) in enumerate(drum_bars(ns, div, per_file)):
                out.append({"type": "drum_groove", "src": "inbox:%s#bar%d" % (name, i), "family": fam, "lanes": lanes, "vel": vel,
                            "off": off, "swing": 0.0, "groove": None, "bpm": round(bpm), "license": lic})
            key = key_of(ns, div)
            if key:
                for i, st in enumerate(bass_bars(ns, div, key, per_file)):
                    out.append({"type": "bass_seq", "src": "inbox:%s#bass%d" % (name, i), "family": fam, "steps": st,
                                "root": key[0], "mode": key[1], "bpm": round(bpm), "license": lic})
                for i, nums in enumerate(progressions(ns, div, key, bass_channel(ns))):
                    out.append({"type": "chord_prog", "src": "inbox:%s#prog%d" % (name, i), "family": fam, "mode": key[1],
                                "style": fam, "key": key[0], "numerals": nums, "mood": [], "license": lic})
        ws = spread(list(E.windows(f, parsed=(tracks, div))), windows)
        for k, w in enumerate(ws):
            steps = [b - a for a, b in zip(w["pitches"], w["pitches"][1:])]
            nm = "inbox:%s#%d" % (name, k)
            out.append({"type": "melody_contour", "src": nm, "family": fam, "role": "bass" if statistics.median(w["pitches"]) < 55 else
                        ("lead" if statistics.median(w["pitches"]) >= 72 else "mid"), "pitches": w["pitches"], "steps": steps, "license": lic})
            out.append({"type": "note_expression", "src": nm, "family": fam, "vel": w["vel"], "bend": w["bend"],
                        "pressure": w["pressure"], "slide": w["slide"], "license": lic})
        for p in out:
            h, s = GATES[p["type"]](p)
            p.update(id=P.digest({k: v for k, v in p.items() if k not in ("src", "family", "license")}), hard=h, soft=s)
    except Exception:
        return []
    finally:
        signal.alarm(0)
    return out


def jobs(inbox, cap, per_file, windows, done=frozenset()):
    """(path, name, family, licence, ...) for every file, each distinct file once; names in done are skipped"""
    seen = set()
    for src in sorted(d for d in glob.glob(os.path.join(inbox, "*")) if os.path.isdir(d)):
        if os.path.basename(src) in SKIP_SOURCES:
            continue
        smeta = source_meta(src)
        files = sorted(f for f in glob.glob(os.path.realpath(src) + "/**/*.mid*", recursive=True) if os.path.isfile(f))
        files = pick([f for f in files if os.path.getsize(f) < 2_000_000], cap)
        sys.stderr.write("%s: %d files\n" % (os.path.basename(src), len(files)))
        for f in files:
            with open(f, "rb") as fh:
                h = hashlib.md5(fh.read()).digest()
            if h in seen:
                continue
            seen.add(h)
            name = os.path.basename(src) + "/" + os.path.relpath(f, os.path.realpath(src))
            if name in done:
                continue
            yield f, name, guess_family(name, smeta), smeta["license"], per_file, windows


def gate_chord_inbox(p):
    h, s = C.gate_chord(p)
    if not h and len(set(p["numerals"])) < 3:
        h.append("under 3 distinct chords")  # a two-chord vamp says little about where harmony goes
    return h, s


GATES = {"drum_groove": P.gate_drum, "bass_seq": P.gate_bass, "chord_prog": gate_chord_inbox,
         "melody_contour": E.gate_contour, "note_expression": E.gate_expr}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inbox", default="~/Infinite/training-data/inbox")
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--cap", type=int, default=0, help="max files per source (0 = all)")
    ap.add_argument("--per-file", type=int, default=4, help="drum and bass bars per file")
    ap.add_argument("--windows", type=int, default=3, help="melody windows per file")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--resume", action="store_true", help="keep inbox.jsonl.tmp from a stopped run and add the files it lacks")
    ap.add_argument("--budget", type=float, default=60, help="minutes before it stops and keeps what it has")
    a = ap.parse_args()
    inbox, store = os.path.expanduser(a.inbox), os.path.expanduser(a.store)
    os.makedirs(store, exist_ok=True)
    deadline, t0 = time.time() + 60 * a.budget, time.time()
    counts = collections.defaultdict(lambda: [0, 0])
    fam = collections.defaultdict(collections.Counter)
    lic = collections.Counter()
    seen, nfiles = set(), 0
    out = os.path.join(store, "inbox.jsonl")
    done = set()
    if a.resume and os.path.exists(out + ".tmp"):
        # a file's rows are written together, so every file named in the .tmp is complete
        for ln in open(out + ".tmp"):
            p = json.loads(ln)
            done.add(p["src"][len("inbox:"):].split("#")[0])
            seen.add(p["id"])
        sys.stderr.write("resuming: %d files, %d pieces already written\n" % (len(done), len(seen)))
    with open(out + ".tmp", "a" if a.resume else "w") as fo, multiprocessing.Pool(a.jobs) as pool:
        for rows in pool.imap_unordered(one_file, jobs(inbox, a.cap, a.per_file, a.windows, done), chunksize=32):
            nfiles += 1
            if nfiles % 5000 == 0:
                sys.stderr.write("%d files, %.1f min\n" % (nfiles, (time.time() - t0) / 60))
            for p in rows:
                if not p["hard"] and p["id"] in seen:
                    p["hard"] = ["duplicate of an earlier piece"]
                seen.add(p["id"])
                p["ok"] = not p["hard"]
                fo.write(json.dumps(p, separators=(",", ":")) + "\n")
                counts[p["type"]][0] += 1
                counts[p["type"]][1] += p["ok"]
                fam[p["type"]][p["family"]] += p["ok"]
                lic[p["license"]] += 1
            if time.time() > deadline:
                sys.stderr.write("time budget reached, stopping\n")
                pool.terminate()
                break
    if nfiles == 0 and not a.resume and os.path.exists(out):
        # the raw MIDI is deleted once it has been read: keep the pieces already extracted from it
        os.remove(out + ".tmp")
        print("no MIDI files under %s: kept %s as it is" % (inbox, out))
        return
    os.replace(out + ".tmp", out)
    print("%d files in %.1f min" % (nfiles, (time.time() - t0) / 60))
    print("%-16s %8s %8s  %s" % ("type", "total", "ok", "by family (ok)"))
    for t in GATES:
        print("%-16s %8d %8d  %s" % (t, counts[t][0], counts[t][1], dict(fam[t].most_common(6))))
    print("licenses:", dict(lic))
    print("wrote", out)


if __name__ == "__main__":
    main()
