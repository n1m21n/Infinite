#!/usr/bin/env python3
"""Full-song detail from one parsed MIDI file: everything the 16-step 4/4 pattern rows cannot hold.
  bar_map      bar starts from every time-signature event, so a meter change mid-song keeps the bars aligned
  drum_meter   drum bars in any other meter, and 4/4 bars played on a triplet grid (shuffle, 12/8 feel, triplet rolls)
  bass_meter   bass bars in any meter other than 4/4, as scale degrees on the 16th grid
  chord_detail one chord per bar or half bar with quality (triads, 7ths, sus, 6, add9), inversion and voicing spread
  song_arc     per-bar curves over the whole song: meter, bpm, density, drums, velocity, register, parts, harmony, sections
  cc_motion    per (channel, controller) automation: bar means plus the two busiest 4-bar windows at 16th resolution
bpm and meter are lists of [unit, value] changes. A performance with no grid (recorded piano, most onsets off every grid) gets its song_arc in 2-second windows.
These rows go to ~/Infinite/training-data/arcs, not the store: no shipped model reads them yet."""
import bisect, collections, re, statistics

# GM program family (program // 8) -> part role
ROLES = ["drums", "bass", "keys", "plucks", "pads", "leads", "textures", "percussion", "foley"]
FAMILY_ROLE = ["keys", "plucks", "keys", "plucks", "bass", "pads", "pads", "leads",
               "leads", "leads", "leads", "pads", "textures", "plucks", "percussion", "foley"]
# controllers worth following: mod wheel, portamento, volume, pan, expression, sustain, portamento on, resonance,
# release, attack, cutoff, reverb send, chorus send; 128 = pitch bend
CCS = (1, 5, 7, 10, 11, 64, 65, 71, 72, 73, 74, 91, 93, 128)
KICKS, CRASHES = {35, 36}, {49, 52, 55, 57}
SECTION = re.compile(r"\b(intro|verse|pre-?chorus|chorus|refrain|bridge|breakdown|break|build-?up|build|drop|hook|"
                     r"solo|outro|coda|interlude|ending|theme|reprise)\b", re.I)
# chord templates (intervals from the root); four-note ones pay a small price so a plain triad wins a tie
QUALITIES = {"maj": (0, 4, 7), "min": (0, 3, 7), "dim": (0, 3, 6), "aug": (0, 4, 8), "sus2": (0, 2, 7), "sus4": (0, 5, 7),
             "maj7": (0, 4, 7, 11), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10), "m7b5": (0, 3, 6, 10), "dim7": (0, 3, 6, 9),
             "6": (0, 4, 7, 9), "m6": (0, 3, 7, 9), "add9": (0, 4, 7, 2), "madd9": (0, 3, 7, 2)}
MINORISH = {"min", "dim", "m7", "m7b5", "dim7", "m6", "madd9"}


def spread(items, n):
    """n items spread evenly over the list (as midi_ingest.spread)"""
    return items if len(items) <= n else [items[i * len(items) // n] for i in range(n)]


def role(ch, prog):
    return "drums" if ch == 9 else FAMILY_ROLE[prog // 8]


def bar_map(tracks, div, end):
    """[(start tick, length, num, den power)] up to `end`. A signature that lands mid-bar cuts that bar short."""
    sigs = {}
    for tr in tracks:
        for t, k, ch, a, v in tr:
            if k == 0xff and ch == 0 and 0 < a <= 32 and v <= 5:
                sigs[t] = (a, v)
    ch_ = sorted(sigs.items())
    if not ch_ or ch_[0][0] > 0:
        ch_.insert(0, (0, (4, 2)))
    bars, t, i = [], 0, 0
    while t < end and len(bars) < 3000:
        while i + 1 < len(ch_) and ch_[i + 1][0] <= t:
            i += 1
        num, den = ch_[i][1]
        L = max(1, round(num * div * 4 / 2 ** den))
        if i + 1 < len(ch_) and t < ch_[i + 1][0] < t + L:
            L = ch_[i + 1][0] - t
        bars.append((t, L, num, den))
        t += L
    return bars


def tempo_map(tracks):
    tm = sorted({t: a for tr in tracks for t, k, ch, a, v in tr if k == 0xff and ch == 1 and a > 0}.items())
    if not tm or tm[0][0] > 0:
        tm.insert(0, (0, 500000))
    return tm


def clock(tm, div):
    """(tick -> seconds, seconds -> tick) through the tempo map"""
    ts, acc = [t for t, _ in tm], [0.0]
    for (t0, u0), (t1, _) in zip(tm, tm[1:]):
        acc.append(acc[-1] + (t1 - t0) * u0 / div / 1e6)

    def sec(t):
        i = bisect.bisect_right(ts, t) - 1
        return acc[i] + (t - ts[i]) * tm[i][1] / div / 1e6

    def tick(s):
        i = bisect.bisect_right(acc, s) - 1
        return ts[i] + (s - acc[i]) * div * 1e6 / tm[i][1]
    return sec, tick


def bpm_at(tm, t):
    i = bisect.bisect_right([x for x, _ in tm], t) - 1
    return 60e6 / tm[max(0, i)][1]


def near(xs, step, tol=0.25):
    return sum(abs(x / step - round(x / step)) <= tol for x in xs)


def grid_of(rel, div):
    """'16' when 85% of the onsets sit on the 16th grid, 12 or 24 (steps per 4/4 bar) on the 8th or 16th triplet grid"""
    if not rel:
        return None
    n = 0.85 * len(rel)
    if near(rel, div / 4) >= n:
        return "16"
    if near(rel, div / 3) >= n:
        return "t8"
    if near(rel, div / 6) >= n:
        return "t16"
    return None


def cells(hits, L, n, lanes):
    """hits (rel tick, lane, velocity) -> lanes/vel/off strings of n steps, same encoding as drum_groove"""
    step = L / n
    g = [["."] * n for _ in range(lanes)]
    vel = [["."] * n for _ in range(lanes)]
    off = [["."] * n for _ in range(lanes)]
    for x, li, v in hits:
        s = round(x / step)
        if s >= n:
            continue
        vq = str(min(9, round(v / 127 * 9)))
        if g[li][s] == "." or vq > vel[li][s]:
            g[li][s], vel[li][s] = "x", vq
            off[li][s] = chr(ord("i") + max(-8, min(8, round((x / step - s) * 16))))
    has = ["x" in l for l in g]
    return tuple(["".join(l) if h else "" for l, h in zip(a, has)] for a in (g, vel, off))


def by_bar(ns, bars):
    """notes grouped by the bar their onset falls in"""
    starts = [b[0] for b in bars]
    out = collections.defaultdict(list)
    for n in ns:
        i = bisect.bisect_right(starts, n[0]) - 1
        if 0 <= i < len(bars):
            out[i].append(n)
    return out


def drum_meter(ns, bars, div, lane_notes, per_file):
    """drum bars that drum_groove skips: other meters on a 16th or triplet grid, and 4/4 bars on a triplet grid"""
    grouped = by_bar([n for n in ns if n[3] == 9], bars)
    picks = []
    for i, (t0, L, num, den) in enumerate(bars):
        hs = grouped.get(i)
        if not hs or L != round(num * div * 4 / 2 ** den):
            continue  # empty or a bar cut short by a signature change
        rel = [on - t0 for on, *_ in hs]
        g = grid_of(rel, div)
        if g is None or (g == "16" and (num, den) == (4, 2)):
            continue  # off every grid, or a plain 4/4 bar that drum_groove already holds
        per_q = {"16": 4, "t8": 3, "t16": 6}[g]
        n = round(L / (div / per_q))
        if not 2 <= n <= 96:
            continue
        hits = [(on - t0, li, v) for on, _, a, _, v, _ in hs for li, lns in enumerate(lane_notes) if a in lns]
        if hits:
            picks.append((i, "%d/%d" % (num, 2 ** den), g, n, hits, L))
    for i, meter, g, n, hits, L in spread(picks, per_file):
        lanes, vel, off = cells(hits, L, n, len(lane_notes))
        yield {"meter": meter, "grid": g, "steps_n": n, "lanes": lanes, "vel": vel, "off": off, "bar": i}


def bass_meter(ns, bars, div, key, bass_ch, degree, per_file):
    """bass bars in meters other than 4/4: lowest note per 16th as scale degrees, like bass_seq"""
    if bass_ch is None or key is None:
        return
    root, mode = key
    line = [n for n in ns if n[3] == bass_ch]
    if not line:
        return
    lo = min(n[2] for n in line)
    base = lo - (lo - root) % 12
    grouped, picks = by_bar(line, bars), []
    for i, (t0, L, num, den) in enumerate(bars):
        if (num, den) == (4, 2) or i not in grouped or L != round(num * div * 4 / 2 ** den):
            continue
        n = round(L / (div / 4))
        if not 2 <= n <= 64:
            continue
        low = {}
        for on, _, a, *_ in grouped[i]:
            s = round((on - t0) / (div / 4))
            if s < n and (s not in low or a < low[s]):
                low[s] = a
        steps, chrom = ["."] * n, 0
        for s, a in low.items():
            d = degree(a, root, mode, base)
            if d is None:
                chrom += 1
            elif d <= 9:
                steps[s] = str(d)
        if sum(c != "." for c in steps) >= 2 and chrom <= 0.25 * len(low):
            picks.append({"meter": "%d/%d" % (num, 2 ** den), "steps": "".join(steps), "bar": i})
    yield from spread(picks, per_file)


def overlaps(ns, spans):
    """for each span (start, length) of a contiguous run, the pitched notes sounding in it (a note counts in at most
    8 spans: a few broken files hold a note for the whole song)"""
    starts, out = [t for t, _ in spans], [[] for _ in spans]
    for n in ns:
        if n[3] == 9:
            continue
        j = max(0, bisect.bisect_right(starts, n[0]) - 1)
        for k in range(j, min(len(spans), j + 8)):
            if starts[k] >= n[1]:
                break
            out[k].append(n)
    return out


def pc_weights(ns, t0, t1, bass_ch):
    """duration-weighted pitch classes in [t0, t1) (the bass counts double), the lowest note, and the pitch span;
    ns is the overlaps() list for a span that holds [t0, t1)"""
    w, lo, hi = [0.0] * 12, None, None
    for on, off, a, ch, v, p in ns:
        if ch == 9 or off <= t0 or on >= t1:
            continue
        lap = (min(off, t1) - max(on, t0)) / (t1 - t0)
        w[a % 12] += lap * (2.0 if ch == bass_ch else 1.0)
        lo = a if lo is None or a < lo else lo
        hi = a if hi is None or a > hi else hi
    return w, lo, (hi - lo if lo is not None else 0)


def chord_of(w, lo):
    """(root, quality, inversion, cover) by template match; inversion = index of the lowest note's pitch class in the
    chord (0 root position, 1 first, ...), -1 when the bass is not a chord tone. None under 60% cover."""
    tot = sum(w)
    if tot <= 0:
        return None
    best, bs = None, -1.0
    for q, iv in QUALITIES.items():
        for r in range(12):
            tones = [w[(r + i) % 12] for i in iv]
            if len(iv) == 4 and min(tones) < 0.08 * tot:
                continue  # the added tone must really sound
            sc = (sum(tones) + 0.2 * w[r]) / tot - 0.04 * (len(iv) - 3)
            if sc > bs:
                best, bs = (r, q), sc
    r, q = best
    cover = sum(w[(r + i) % 12] for i in QUALITIES[q]) / tot
    if cover < 0.6:
        return None
    rel = (lo - r) % 12 if lo is not None else 0
    inv = QUALITIES[q].index(rel) if rel in QUALITIES[q] else -1
    return r, q, inv, round(cover, 2)


def chord_per_bar(ns, bars, bass_ch):
    """[(root, quality, inversion, spread) or None] per bar"""
    out = []
    for (t0, L, _, _), sub in zip(bars, overlaps(ns, [(b[0], b[1]) for b in bars])):
        w, lo, sp = pc_weights(sub, t0, t0 + L, bass_ch)
        c = chord_of(w, lo)
        out.append(None if c is None else (c[0], c[1], c[2], sp))
    return out


def chord_name(c):
    return "" if c is None else "%d%s%s" % (c[0], c[1], "/%d" % c[2] if c[2] else "")


def segments(bars, ns, div, tm):
    """the song's time units: its bars when it is played to a grid, else 2-second windows"""
    pitched = [n[0] for n in ns if n[3] != 9]
    rel = []
    starts = [b[0] for b in bars]
    for on in pitched[:4000]:
        i = bisect.bisect_right(starts, on) - 1
        rel.append(on - bars[max(0, i)][0])
    # within a tenth of a step of the 16th or the 16th-triplet grid: chance alone puts ~36% of onsets there
    on_grid = sum(min(abs(x / (div / 4) - round(x / (div / 4))), abs(x / (div / 6) - round(x / (div / 6)))) <= 0.1 for x in rel)
    clocked = not rel or on_grid >= 0.55 * len(rel)
    if clocked:
        return "bar", [(t, L) for t, L, _, _ in bars]
    sec, tick = clock(tm, div)
    end, segs, s = max(n[1] for n in ns), [], 0.0
    while len(segs) < 3000:
        a, b = round(tick(s)), round(tick(s + 2.0))
        if a >= end:
            break
        segs.append((a, max(1, b - a)))
        s += 2.0
    return "2s", segs


def song_arc(tracks, ns, div, bars, key, bass_ch):
    """per-unit curves for the whole song plus its parts list and section markers"""
    tm = tempo_map(tracks)
    unit, segs = segments(bars, ns, div, tm)
    starts = [t for t, _ in segs]
    idx = lambda t: bisect.bisect_right(starts, t) - 1
    n = len(segs)
    dens, drum, kick, crash, vsum, vcnt, parts = ([0] * n for _ in range(7))
    pitches = [[] for _ in range(n)]
    dur = [0.0] * n
    for on, off, a, ch, v, p in ns:
        i = idx(on)
        if not 0 <= i < n:
            continue
        vsum[i] += v
        vcnt[i] += 1
        parts[i] |= 1 << ROLES.index(role(ch, p))
        if ch == 9:
            drum[i] += 1
            kick[i] += a in KICKS
            crash[i] += a in CRASHES
        else:
            dens[i] += 1
            pitches[i].append(a)
            dur[i] += min(off - on, 4 * segs[i][1]) / segs[i][1]
    pcs, chords = [], []
    for (t0, L), sub in zip(segs, overlaps(ns, segs)):
        halves = [(t0, L)] if unit == "2s" else [(t0, L // 2), (t0 + L // 2, L - L // 2)]
        mask, cs = 0, []
        w_all, _, _ = pc_weights(sub, t0, t0 + L, bass_ch)
        tot = sum(w_all)
        for pc in range(12):
            if tot and w_all[pc] >= 0.05 * tot:
                mask |= 1 << pc
        pcs.append(mask)
        for h0, hl in halves:
            w, lo, sp = pc_weights(sub, h0, h0 + hl, bass_ch)
            c = chord_of(w, lo)
            cs.append(chord_name(None if c is None else (c[0], c[1], c[2], sp)))
        chords.append(cs[0] if len(set(cs)) == 1 else "|".join(cs))
    meters = [[i, "%d/%d" % (b[2], 2 ** b[3])] for i, b in enumerate(bars) if i == 0 or b[2:] != bars[i - 1][2:]]
    secs, names = [], []
    for ti, tr in enumerate(tracks):
        for t, k, ch, a, v in tr:
            if k == 0xff and ch == 6 and isinstance(a, str):
                if v == 0x03 and a and len(names) < 24:
                    names.append(a[:32])
                elif SECTION.search(a):
                    i = idx(t)
                    if 0 <= i < n and len(secs) < 64:
                        secs.append([i, a[:32].lower()])
    progs = sorted({(ch, p) for _, _, _, ch, _, p in ns})
    roles = collections.Counter(role(ch, p) for _, _, _, ch, _, p in ns)
    bpms = [round(bpm_at(tm, t)) for t, _ in segs]
    drums = {"drum": drum, "kick": kick, "crash": crash} if any(drum) else {}  # left out of drumless songs
    return dict({"unit": unit, "n": n, "meter": meters if unit == "bar" else [],
            "bpm": [[i, b] for i, b in enumerate(bpms) if i == 0 or b != bpms[i - 1]], "dens": dens,
            "vel": [round(s / c) if c else 0 for s, c in zip(vsum, vcnt)],
            "reg": [round(statistics.median(x)) if x else 0 for x in pitches],
            "span": [max(x) - min(x) if x else 0 for x in pitches],
            "poly": [round(x, 1) for x in dur], "parts": parts, "pcs": pcs, "chords": chords,
            "sections": sorted(secs), "tracks": names, "programs": [list(x) for x in progs],
            "roles": dict(roles), "key": list(key) if key else None}, **drums)


def cc_motion(tracks, ns, div, bars, statics):
    """one row per (channel, controller) the song moves: bar means and the two 4-bar windows with the most movement at
    16th resolution (64 values each). A controller set once and left goes into statics[role][cc] (the song's mix)."""
    first_prog = {}
    for on, _, _, ch, _, p in ns:
        first_prog.setdefault(ch, p)
    ev = collections.defaultdict(list)
    for tr in tracks:
        for t, k, ch, a, v in tr:
            if k == 0xb0 and a in CCS:
                ev[(ch, a)].append((t, v))
            elif k == 0xe0:
                ev[(ch, 128)].append((t, (a | (v << 7)) >> 7))  # 0..127, 64 = centre
    if not bars:
        return
    end = bars[-1][0] + bars[-1][1]
    for (ch, cc), xs in sorted(ev.items()):
        xs.sort()
        vals = {v for _, v in xs}
        base = {"ch": ch, "cc": cc, "role": role(ch, first_prog.get(ch, 0)), "program": first_prog.get(ch, 0),
                "events": len(xs)}
        if len(vals) == 1:
            statics.setdefault(base["role"], {}).setdefault(str(cc), xs[0][1])
            continue
        # value held at every 16th of every bar
        ts = [t for t, _ in xs]
        samp = []
        for t0, L, _, _ in bars:
            for s in range(max(1, round(L / (div / 4)))):
                t = t0 + s * div / 4
                if t >= end:
                    break
                i = bisect.bisect_right(ts, t) - 1
                samp.append(xs[i][1] if i >= 0 else xs[0][1])
        per_bar, k = [], 0
        for t0, L, _, _ in bars:
            m = max(1, round(L / (div / 4)))
            seg = samp[k:k + m]
            k += m
            per_bar.append(round(sum(seg) / len(seg)) if seg else 0)
        wins = []
        if len(samp) >= 64:
            tv = [abs(b - a) for a, b in zip(samp, samp[1:])]
            run = [sum(tv[i:i + 63]) for i in range(0, len(samp) - 63, 16)]
            for _ in range(2):
                if not run or max(run) == 0:
                    break
                j = max(range(len(run)), key=run.__getitem__)
                wins.append({"at": j * 16, "v": samp[j * 16:j * 16 + 64]})
                for q in range(max(0, j - 3), min(len(run), j + 4)):
                    run[q] = 0  # windows do not overlap
        yield dict(base, bars=per_bar, windows=wins)


def gate(p):
    """sanity only: these rows feed analysis, not a sampler"""
    h = []
    t = p["type"]
    if t == "song_arc" and p["n"] < 4:
        h.append("under 4 units")
    if t == "drum_meter" and sum(c == "x" for l in p["lanes"] for c in l) < 3:
        h.append("under 3 hits")
    if t == "cc_motion" and len(set(p["bars"])) == 1 and not p["windows"]:
        h.append("no movement")
    return h, []
