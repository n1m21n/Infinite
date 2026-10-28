#!/usr/bin/env python3
"""What the full-detail rows say: how songs move over time, how controllers move, what odd meters and triplets hold.
    arc_analysis.py [--arcs ~/Infinite/training-data/arcs/detail.jsonl.gz] [--store ~/Infinite/training-data/store/inbox.jsonl]
Streams the rows once and prints:
  coverage      rows by type, units (bar / 2 s), meters, triplet grids
  time          energy curves over song position (tenths) by genre: note rate, drum hits, velocity, register, parts
  sections      verse -> chorus / build -> drop deltas, from section markers
  tempo         songs with tempo changes, how far they move, endings that slow down
  controllers   share of songs moving each CC per role, motion shape (ramp / swell / cycle / steps), period, depth
  mix           set-once values per role (volume, pan, reverb and chorus sends)
  chords        quality and inversion shares by genre
Genre comes from the folder (adl_piano_genres/<Genre>, groove's file names) or the family."""
import argparse, collections, gzip, json, os, re, statistics

CC_NAME = {1: "mod wheel", 5: "portamento time", 7: "volume", 10: "pan", 11: "expression", 64: "sustain", 65: "portamento",
           71: "resonance", 72: "release", 73: "attack", 74: "cutoff", 91: "reverb send", 93: "chorus send", 128: "pitch bend"}
# controller -> the Generate Samples knob roles its motion informs
CC_KNOBS = {1: "timbre: fmDepth, warpAmount, position, oscMix, detune, drive", 7: "level", 11: "level",
            10: "pan", 71: "resonance", 74: "brightness: cutoffs, tones", 72: "release", 73: "attack",
            91: "space: Reverb mix/size", 93: "width: Chorus mix/depth", 5: "glide", 65: "glide", 64: "sustain", 128: "pitch"}


def genre(src, fam):
    m = re.match(r"inbox:adl_piano_genres/extracted_midis/adl-piano-midi/([^/]+)/", src)
    if m:
        return m[1].lower()
    m = re.match(r"inbox:groove_drumset/.*/\d+_([a-z]+)", src)
    if m:
        return "groove:" + m[1]
    return fam or "unlabeled"


def tenths(xs):
    """mean of xs in each tenth of the song"""
    n = len(xs)
    return [statistics.fmean(xs[i * n // 10:max(i * n // 10 + 1, (i + 1) * n // 10)]) for i in range(10)]


def shape(v):
    """ramp / swell / cycle / steps / wobble for one 64-step window"""
    lo, hi = min(v), max(v)
    if hi - lo < 4:
        return "flat"
    d = [b - a for a, b in zip(v, v[1:])]
    moves = sum(x != 0 for x in d)
    if moves <= 6:
        return "steps"
    up, dn = sum(x for x in d if x > 0), -sum(x for x in d if x < 0)
    if min(up, dn) < 0.15 * max(up, dn):
        return "ramp up" if up > dn else "ramp down"
    turns = sum(1 for a, b in zip(d, d[1:]) if a * b < 0)
    return "cycle" if turns >= 4 else "swell"


def period(v):
    """the cycle length (16ths) of one window: the highest autocorrelation peak after the curve first decorrelates,
    or None when it never repeats (r under 0.3)"""
    m = statistics.fmean(v)
    x = [a - m for a in v]
    den = sum(a * a for a in x)
    if den <= 0:
        return None
    r = {L: sum(x[i] * x[i + L] for i in range(len(x) - L)) / den for L in range(1, 33)}
    gone = next((L for L in r if r[L] < 0), None)
    if gone is None:
        return None
    best = max(range(gone, 33), key=r.get)
    return best if r[best] > 0.3 else None


def lines(path):
    """the complete lines of a gzip file, also one still being written or cut off by a crash"""
    try:
        for ln in gzip.open(path, "rt"):
            if ln.endswith("\n"):
                yield ln
    except EOFError:
        return


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arcs", default="~/Infinite/training-data/arcs/detail.jsonl.gz")
    ap.add_argument("--store", default="~/Infinite/training-data/store/inbox.jsonl")
    a = ap.parse_args()
    types, units, meters, grids = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    curves = collections.defaultdict(lambda: collections.defaultdict(list))  # genre -> feature -> [tenths]
    sec_d = collections.defaultdict(list)
    tempo_files = tempo_moved = 0
    tempo_span, rit = [], 0
    songs_by_role = collections.Counter()
    cc_songs = collections.defaultdict(set)  # (cc, role) -> songs
    shapes = collections.defaultdict(collections.Counter)
    periods = collections.defaultdict(list)
    depths = collections.defaultdict(list)
    mix = collections.defaultdict(list)
    for ln in lines(os.path.expanduser(a.arcs)):
        r = json.loads(ln)
        t = r["type"]
        types[t, r["ok"]] += 1
        if not r["ok"]:
            continue
        if t == "drum_meter":
            meters[r["meter"]] += 1
            grids[r["grid"], r["meter"] == "4/4"] += 1
        elif t == "bass_meter":
            meters["bass " + r["meter"]] += 1
        elif t == "cc_motion":
            cc_songs[r["cc"], r["role"]].add(r["src"].split("#")[0])
            for w in r["windows"]:
                shapes[r["cc"]][shape(w["v"])] += 1
                p = period(w["v"])
                if p:
                    periods[r["cc"]].append(p)
                depths[r["cc"]].append(max(w["v"]) - min(w["v"]))
        elif t == "song_arc":
            units[r["unit"]] += 1
            for ro in r["roles"]:
                songs_by_role[ro] += 1
            for ro, ccs in r.get("mix", {}).items():
                for cc, v in ccs.items():
                    mix[int(cc), ro].append(v)
            if r["unit"] == "bar":
                for m in r["meter"]:
                    meters["song " + m[1]] += 1
                tempo_files += 1
                b = [x[1] for x in r["bpm"]]
                if len(b) > 1:
                    tempo_moved += 1
                    tempo_span.append(max(b) / max(1, min(b)))
                    if b[-1] < 0.9 * b[0]:
                        rit += 1
            if r["n"] >= 20:
                g = genre(r["src"], r["family"])
                n = r["n"]
                parts = [bin(x).count("1") for x in r["parts"]]
                drum = r.get("drum") or [0] * n
                for name, xs in (("notes", r["dens"]), ("drum", drum), ("vel", r["vel"]), ("reg", r["reg"]), ("parts", parts)):
                    curves[g][name].append(tenths(xs))
                    curves["all " + r["unit"]][name].append(tenths(xs))
                # section deltas: mean of the 4 units after a marker vs the 4 before
                for i, word in r["sections"]:
                    w = re.search(r"(chorus|verse|drop|build|bridge|breakdown|break|intro|outro|solo)", word)
                    if w and 4 <= i <= n - 4:
                        en = lambda j: r["vel"][j] * (r["dens"][j] + drum[j])
                        before = statistics.fmean(en(j) for j in range(i - 4, i))
                        after = statistics.fmean(en(j) for j in range(i, i + 4))
                        if before > 0:
                            sec_d[w[1]].append(after / before)

    print("== coverage")
    for (t, ok), c in sorted(types.items()):
        print("  %-12s %-5s %9d" % (t, "ok" if ok else "hard", c))
    print("  song units:", dict(units))
    print("  meters (songs = signature changes):", dict(meters.most_common(16)))
    print("  triplet / other-meter drum bars:", {("%s in %s" % (g, "4/4" if f else "other")): c for (g, f), c in grids.items()})

    print("\n== time: song position in tenths (start -> end), genres with >= 200 songs")
    for g, fs in sorted(curves.items(), key=lambda kv: -len(kv[1]["notes"])):
        if len(fs["notes"]) < 200:
            continue
        print("  %s (%d songs)" % (g, len(fs["notes"])))
        for name, cs in fs.items():
            mean = [statistics.fmean(c[i] for c in cs) for i in range(10)]
            top = max(mean) or 1
            if name in ("notes", "drum"):  # % of the song's busiest tenth
                cells = " ".join("%4d" % round(100 * m / top) for m in mean)
            else:  # velocity 0-127, median pitch, parts playing
                cells = " ".join(("%4.1f" if name == "parts" else "%4d") % (m if name == "parts" else round(m)) for m in mean)
            print("    %-6s %s   peak at %d/10" % (name, cells, mean.index(max(mean)) + 1))

    print("\n== sections: energy (velocity x hits) of the 4 units after the marker / the 4 before")
    for w, xs in sorted(sec_d.items(), key=lambda kv: -len(kv[1])):
        if len(xs) >= 20:
            print("  %-10s n=%5d  median x%.2f  p25 x%.2f  p75 x%.2f" % (w, len(xs), pct(xs, .5), pct(xs, .25), pct(xs, .75)))

    print("\n== tempo: %d of %d clocked songs change tempo (%.1f%%); max/min ratio p50 %.2f p90 %.2f; %d slow down >10%% by the end" % (
        tempo_moved, tempo_files, 100 * tempo_moved / max(1, tempo_files), pct(tempo_span, .5), pct(tempo_span, .9), rit))

    print("\n== controllers: songs moving each CC (share of songs with that role), window shape, period, depth (0-127)")
    for cc in sorted({c for c, _ in cc_songs}):
        per_role = {ro: len(s) for (c, ro), s in cc_songs.items() if c == cc}
        tops = sorted(per_role.items(), key=lambda kv: -kv[1])[:4]
        share = " ".join("%s %.1f%%" % (ro, 100 * n / max(1, songs_by_role[ro])) for ro, n in tops)
        sh = shapes[cc]
        tot = sum(sh.values()) or 1
        print("  %3d %-15s %s" % (cc, CC_NAME.get(cc, ""), share))
        print("      shapes %s | period p50 %s 16ths | depth p25/p50/p75 %s/%s/%s | knobs: %s" % (
            " ".join("%s %d%%" % (k, 100 * v / tot) for k, v in sh.most_common(5)),
            pct(periods[cc], .5) if periods[cc] else "-", pct(depths[cc], .25), pct(depths[cc], .5), pct(depths[cc], .75),
            CC_KNOBS.get(cc, "")))

    print("\n== mix: set-once values per role, p25/p50/p75 (songs)")
    for cc in (7, 10, 91, 93):
        cells = []
        for ro in ("drums", "bass", "keys", "plucks", "pads", "leads"):
            xs = mix.get((cc, ro), [])
            if len(xs) >= 50:
                cells.append("%s %d/%d/%d" % (ro, pct(xs, .25), pct(xs, .5), pct(xs, .75)))
        print("  %-12s %s" % (CC_NAME[cc], "  ".join(cells)))

    print("\n== chords: quality and inversion shares by genre (chord_prog bars)")
    q = collections.defaultdict(collections.Counter)
    inv = collections.defaultdict(collections.Counter)
    for ln in open(os.path.expanduser(a.store)):
        if '"chord_prog"' not in ln or '"qual"' not in ln:
            continue
        r = json.loads(ln)
        if not r["ok"]:
            continue
        g = genre(r["src"], r["family"])
        for x in r["qual"]:
            q[g][x] += 1
            q["all"][x] += 1
        for x in r["inv"]:
            inv[g][x] += 1
            inv["all"][x] += 1
    for g in sorted(q, key=lambda g: -sum(q[g].values()))[:12]:
        tot = sum(q[g].values())
        sev = sum(v for k, v in q[g].items() if k in ("maj7", "7", "m7", "m7b5", "dim7", "6", "m6", "add9", "madd9"))
        it = sum(inv[g].values())
        print("  %-14s n=%7d  4-note %4.1f%%  top %s | root pos %.0f%% 1st %.0f%% 2nd %.0f%% slash %.0f%%" % (
            g, tot, 100 * sev / tot, " ".join("%s %d%%" % (k, 100 * v / tot) for k, v in q[g].most_common(5)),
            100 * inv[g][0] / it, 100 * inv[g][1] / it, 100 * inv[g][2] / it, 100 * inv[g][-1] / it))


if __name__ == "__main__":
    main()
