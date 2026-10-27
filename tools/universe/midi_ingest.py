#!/usr/bin/env python3
"""Inbox MIDI -> gated pieces.
    midi_ingest.py [--inbox ~/Infinite/training-data/inbox] [--store ~/Infinite/training-data/store] [--cap 0] [--jobs 4]
Layout: inbox/<source>/**/*.mid plus an optional SOURCE.txt with lines `family: house` and `license: CC-BY-4.0`.
A source with no licence line is `unknown`: it trains the local model only. A folder or file name containing a
family word (house, techno, trance, dnb, breaks, ambient ...) sets the family when SOURCE.txt does not.
Every file is read once (identical bytes in two sources count once). Bars come from the file's whole time-signature
map, so a song that changes meter keeps its 4/4 bars. Into the store:
  drum_groove   4/4 channel 10 bars on the 16th grid, with each hit's velocity (vel, 0-9) and its grid offset (off)
  bass_seq      4/4 bars of the bass channel (GM program 32-39, else the lowest busy channel) as 16 scale degrees
  chord_prog    one chord per bar in any meter, repeats merged, as triad numerals in the detected key plus the full
                quality (qual: maj7, 7, m7, sus4 ...) and inversion (inv) of each
  melody_contour + note_expression windows from every file, tagged with a role from the median pitch; contours
                carry their rhythm (ioi: onset gaps in 16ths, rate: notes per quarter)
Into --arcs (song_detail.py): drum_meter / bass_meter (other meters and triplet-grid bars), song_arc (per-bar curves
of the whole song) and cc_motion (controller automation). Bars are spread over the song, not taken from the start.
Chord-pack folders are left to chord_pieces.py."""
import argparse, bisect, collections, gzip, glob, hashlib, json, multiprocessing, os, re, signal, statistics, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P
import expression_pieces as E
import public_drums as D
import chord_pieces as C
import song_detail as S
import arc_analysis as A

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


def full_44(bars, div):
    """index -> bar start of every whole 4/4 bar"""
    return {i: t0 for i, (t0, L, num, den) in enumerate(bars) if (num, den) == (4, 2) and L == 4 * div}


def drum_bars(ns, bars, div, per_file):
    """one-bar windows of the drum channel in the song's 4/4 bars, with velocity and grid offset per hit. Bars with
    under 85% of hits near the 16th grid are left to song_detail.drum_meter (triplet grids). Bar 0 is skipped when the
    file has more than 3 bars (count-ins, pickups). vel: '0'-'9' per hit. off: 'a'-'q' = -8..+8 sixteenths of a
    sixteenth (half a step either way), 'i' on grid."""
    sixteenth = div / 4.0
    grid = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    vel = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    off = collections.defaultdict(lambda: [["."] * 16 for _ in D.LANE_NOTES])
    near = collections.defaultdict(lambda: [0, 0])
    four = full_44(bars, div)
    starts = [b[0] for b in bars]
    for on, _, a, ch, v, _ in ns:
        if ch != 9:
            continue
        b = bisect.bisect_right(starts, on) - 1
        if b not in four:
            continue
        for li, lane_notes in enumerate(D.LANE_NOTES):
            if a in lane_notes:
                x = (on - four[b]) / sixteenth
                s = round(x)
                if s >= 16:  # a hit a hair before the next bar line plays that bar's downbeat
                    if b + 1 not in four:
                        continue
                    b, x, s = b + 1, x - 16, 0
                k = s
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


def bass_bars(ns, bars, div, key, per_file):
    ch = bass_channel(ns)
    if ch is None or key is None:
        return
    root, mode = key
    line = [(on, a) for on, _, a, c, _, _ in ns if c == ch]
    lo = min(a for _, a in line)
    base = lo - (lo - root) % 12
    sixteenth = div / 4.0
    four = full_44(bars, div)
    starts = [b[0] for b in bars]
    low = collections.defaultdict(dict)
    for on, a in line:
        b = bisect.bisect_right(starts, on) - 1
        if b not in four:
            continue
        k = round((on - four[b]) / sixteenth)
        if k < 16 and (k not in low[b] or a < low[b][k]):
            low[b][k] = a  # lowest note on a step
    keep = []
    for b in sorted(low):
        steps = ["."] * 16
        chrom = 0
        for k, a in low[b].items():
            d = degree(a, root, mode, base)
            if d is None:
                chrom += 1
            elif d <= 9:
                steps[k] = str(d)
        if sum(c != "." for c in steps) >= 2 and chrom <= 0.25 * len(low[b]):
            keep.append("".join(steps))
    for st in spread(keep, per_file):
        yield st


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


def progressions(ns, bars, key, bass_ch, per_file=4, length=4):
    """runs of `length` distinct consecutive chords (repeats merged) in any meter, spread over the song:
    (triad numerals, qualities, inversions)"""
    root, mode = key
    runs, cur = [], []
    for c in S.chord_per_bar(ns, bars, bass_ch):
        if c is None:
            cur = []
            continue
        if cur and cur[-1][:3] == c[:3]:
            continue
        cur.append(c)
        if len(cur) == length:
            runs.append(cur)
            cur = []
    for run in spread(runs, per_file):
        yield ([numeral(r, q in S.MINORISH, root, mode) for r, q, _, _ in run], [q for _, q, _, _ in run],
               [i for _, _, i, _ in run])


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
    out, detail = [], []
    signal.signal(signal.SIGALRM, _too_slow)
    signal.alarm(5)
    try:
        tracks, div = E.parse_midi(f)
        ns = notes(tracks)
        if ns:
            tm = S.tempo_map(tracks)
            bpm = S.bpm_at(tm, 0)
            bars = S.bar_map(tracks, div, max(n[1] for n in ns))
            key = key_of(ns, div)
            bass_ch = bass_channel(ns)
            for i, (lanes, vel, off) in enumerate(drum_bars(ns, bars, div, per_file)):
                out.append({"type": "drum_groove", "src": "inbox:%s#bar%d" % (name, i), "family": fam, "lanes": lanes, "vel": vel,
                            "off": off, "swing": 0.0, "groove": None, "bpm": round(bpm), "license": lic})
            if key:
                for i, st in enumerate(bass_bars(ns, bars, div, key, per_file)):
                    out.append({"type": "bass_seq", "src": "inbox:%s#bass%d" % (name, i), "family": fam, "steps": st,
                                "root": key[0], "mode": key[1], "bpm": round(bpm), "license": lic})
                for i, (nums, qual, inv) in enumerate(progressions(ns, bars, key, bass_ch)):
                    out.append({"type": "chord_prog", "src": "inbox:%s#prog%d" % (name, i), "family": fam, "mode": key[1],
                                "style": fam, "key": key[0], "numerals": nums, "qual": qual, "inv": inv, "mood": [], "license": lic})
            for i, d in enumerate(S.drum_meter(ns, bars, div, D.LANE_NOTES, per_file)):
                d["bpm"] = round(S.bpm_at(tm, bars[d["bar"]][0]))
                detail.append(dict(d, type="drum_meter", src="inbox:%s#meter%d" % (name, i)))
            for i, d in enumerate(S.bass_meter(ns, bars, div, key, bass_ch, degree, per_file)):
                detail.append(dict(d, type="bass_meter", src="inbox:%s#mbass%d" % (name, i), root=key[0], mode=key[1]))
            mix = {}
            for i, d in enumerate(S.cc_motion(tracks, ns, div, bars, mix)):
                detail.append(dict(d, type="cc_motion", src="inbox:%s#cc%d" % (name, i)))
            detail.append(dict(S.song_arc(tracks, ns, div, bars, key, bass_ch), type="song_arc", src="inbox:%s" % name, mix=mix))
            for d in detail:
                d.update(family=fam, license=lic)
        ws = spread(list(E.windows(f, parsed=(tracks, div))), windows)
        for k, w in enumerate(ws):
            steps = [b - a for a, b in zip(w["pitches"], w["pitches"][1:])]
            nm = "inbox:%s#%d" % (name, k)
            out.append({"type": "melody_contour", "src": nm, "family": fam, "role": "bass" if statistics.median(w["pitches"]) < 55 else
                        ("lead" if statistics.median(w["pitches"]) >= 72 else "mid"), "pitches": w["pitches"], "steps": steps, "ioi": w["ioi"], "rate": w["rate"], "license": lic})
            out.append({"type": "note_expression", "src": nm, "family": fam, "vel": w["vel"], "bend": w["bend"],
                        "pressure": w["pressure"], "slide": w["slide"], "license": lic})
        for p in out + detail:
            h, s = GATES[p["type"]](p)
            p.update(id=P.digest({k: v for k, v in p.items() if k not in ("src", "family", "license")}), hard=h, soft=s)
    except Exception:
        return []
    finally:
        signal.alarm(0)
    return out + detail


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
         "melody_contour": E.gate_contour, "note_expression": E.gate_expr,
         "drum_meter": S.gate, "bass_meter": S.gate, "cc_motion": S.gate, "song_arc": S.gate}
DETAIL = {"drum_meter", "bass_meter", "cc_motion", "song_arc"}  # rows for --arcs, which no model reads yet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inbox", default="~/Infinite/training-data/inbox")
    ap.add_argument("--store", default="~/Infinite/training-data/store")
    ap.add_argument("--arcs", default="~/Infinite/training-data/arcs", help="where the song_detail rows go")
    ap.add_argument("--cap", type=int, default=0, help="max files per source (0 = all)")
    ap.add_argument("--per-file", type=int, default=4, help="drum and bass bars per file")
    ap.add_argument("--windows", type=int, default=3, help="melody windows per file")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--resume", action="store_true", help="keep inbox.jsonl.tmp from a stopped run and add the files it lacks")
    ap.add_argument("--budget", type=float, default=60, help="minutes before it stops and keeps what it has")
    a = ap.parse_args()
    inbox, store, arcs = (os.path.expanduser(x) for x in (a.inbox, a.store, a.arcs))
    os.makedirs(store, exist_ok=True)
    os.makedirs(arcs, exist_ok=True)
    deadline, t0 = time.time() + 60 * a.budget, time.time()
    counts = collections.defaultdict(lambda: [0, 0])
    fam = collections.defaultdict(collections.Counter)
    lic = collections.Counter()
    seen, nfiles = set(), 0
    out, out_d = os.path.join(store, "inbox.jsonl"), os.path.join(arcs, "detail.jsonl.gz")
    done = set()
    if a.resume and os.path.exists(out + ".tmp"):
        # a file's rows are written together, so every file named in the .tmp files is complete
        for path in (out + ".tmp", out_d + ".tmp"):
            for ln in (A.lines(path) if path.endswith(".gz.tmp") else open(path)) if os.path.exists(path) else ():
                if not ln.endswith("\n"):
                    break  # the last line of a stopped run can be cut off
                p = json.loads(ln)
                done.add(p["src"][len("inbox:"):].split("#")[0])
                seen.add(p["id"])
        sys.stderr.write("resuming: %d files, %d pieces already written\n" % (len(done), len(seen)))
    mode = "a" if a.resume else "w"
    with open(out + ".tmp", mode) as fo, gzip.open(out_d + ".tmp", mode + "t", compresslevel=6) as fd, multiprocessing.Pool(a.jobs) as pool:
        for rows in pool.imap_unordered(one_file, jobs(inbox, a.cap, a.per_file, a.windows, done), chunksize=32):
            nfiles += 1
            if nfiles % 5000 == 0:
                sys.stderr.write("%d files, %.1f min\n" % (nfiles, (time.time() - t0) / 60))
            for p in rows:
                if not p["hard"] and p["id"] in seen:
                    p["hard"] = ["duplicate of an earlier piece"]
                seen.add(p["id"])
                p["ok"] = not p["hard"]
                (fd if p["type"] in DETAIL else fo).write(json.dumps(p, separators=(",", ":")) + "\n")
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
        os.remove(out_d + ".tmp")
        print("no MIDI files under %s: kept %s as it is" % (inbox, out))
        return
    os.replace(out + ".tmp", out)
    os.replace(out_d + ".tmp", out_d)
    print("%d files in %.1f min" % (nfiles, (time.time() - t0) / 60))
    print("%-16s %8s %8s  %s" % ("type", "total", "ok", "by family (ok)"))
    for t in GATES:
        print("%-16s %8d %8d  %s" % (t, counts[t][0], counts[t][1], dict(fam[t].most_common(6))))
    print("licenses:", dict(lic))
    print("wrote", out, "and", out_d)


if __name__ == "__main__":
    main()
