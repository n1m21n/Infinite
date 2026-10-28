#!/usr/bin/env python3
"""G4: MIDI fully extracted, one row per file (store/midi_full.jsonl, local). Own parser, no dependencies.
    midi_full.py run   [--limit N] [--jobs 3] [--budget MIN]     every .mid under datasets/midi/data (symlinks followed), resumable, stop file ~/STOP_MIDI
    midi_full.py check [--n 20]                                  prints 20 files' rows beside the raw event counts re-read by a second pass
    midi_full.py counts                                          how many files carry each field
Row: src(rel), corpus, ticks_per_beat, format, tracks, seconds (tempo-map exact), notes, channels {ch: {program(s), notes, lo, hi, vel_mean, family}},
families (GM family names), n_families, max_poly,
ccs {cc: {events, distinct, first_s, last_s, rate_per_s, movement}}, bend {events, range}, program_changes,
tempo {changes, bpm_min, bpm_max, bpm_first}, meters [[ticks, num, den]], meter_changes, keysigs, track_names, emotion (EMOPIA from file name Q1-Q4)."""
import argparse, glob, json, os, struct, sys, time, collections
from concurrent.futures import ProcessPoolExecutor, as_completed
ROOT = os.path.expanduser("~/Infinite/datasets/midi/data"); OUT = os.path.expanduser("~/Infinite/training-data/store/midi_full.jsonl")
STOP = os.path.expanduser("~/STOP_MIDI")
FAM = ["piano", "chromatic perc", "organ", "guitar", "bass", "strings", "ensemble", "brass", "reed", "pipe", "synth lead", "synth pad", "synth fx",
       "ethnic", "percussive", "sfx"]
EMO = {"Q1": "high valence, high arousal", "Q2": "low valence, high arousal", "Q3": "low valence, low arousal", "Q4": "high valence, low arousal"}


def parse(b):
    if b[:4] != b"MThd": raise ValueError("not midi")
    fmt, ntrk, div = struct.unpack(">HHH", b[8:14]); pos = 8 + struct.unpack(">I", b[4:8])[0]; ev = []; meta = []; names = []
    while pos + 8 <= len(b):
        tag, ln = b[pos:pos + 4], struct.unpack(">I", b[pos + 4:pos + 8])[0]; pos += 8; d = b[pos:pos + ln]; pos += ln
        if tag != b"MTrk": continue
        i = t = run = 0
        def vlq():
            nonlocal i
            v = 0
            while i < len(d):
                c = d[i]; i += 1; v = (v << 7) | (c & 0x7f)
                if not c & 0x80: break
            return v
        while i < len(d):
            t += vlq()
            if i >= len(d): break
            s = d[i]
            if s == 0xff:
                ty = d[i + 1]; i += 2; n = vlq(); data = d[i:i + n]; i += n
                if ty == 0x51 and n == 3: meta.append((t, "tempo", int.from_bytes(data, "big")))
                elif ty == 0x58 and n >= 2: meta.append((t, "meter", (data[0], 1 << data[1])))
                elif ty == 0x59 and n == 2: meta.append((t, "key", (struct.unpack("b", data[:1])[0], data[1])))
                elif ty == 0x03: names.append(data.decode("latin1", "replace")[:40])
                continue
            if s in (0xf0, 0xf7):
                i += 1; n = vlq(); i += n; continue
            if s & 0x80: run = s; i += 1
            k = run & 0xf0; n = 1 if k in (0xc0, 0xd0) else 2; a = d[i:i + n]; i += n
            if len(a) < n: break
            ev.append((t, k, run & 0x0f, a[0], a[1] if n == 2 else 0))
    return fmt, div, ev, sorted(meta)


def seconds_fn(meta, div):
    tm = [(0, 500000)] + [(t, v) for t, k, v in meta if k == "tempo"]; tm.sort(); marks = []; sec = 0.0; pt, pv = 0, 500000
    for t, v in tm:
        sec += (t - pt) * pv / div / 1e6; marks.append((t, sec, v)); pt, pv = t, v
    def f(t):
        j = 0
        for m in marks:
            if m[0] <= t: j = marks.index(m)
            else: break
        t0, s0, v = marks[j]; return s0 + (t - t0) * v / div / 1e6
    return f, tm


def row(path):
    rel = os.path.relpath(path, ROOT); r = {"src": rel, "corpus": rel.split(os.sep)[0]}
    try:
        b = open(path, "rb").read(); fmt, div, ev, meta = parse(b)
        sec, tm = seconds_fn(meta, div); end = max([e[0] for e in ev] + [0])
        r.update(format=fmt, ticks_per_beat=div, seconds=round(sec(end), 3), events=len(ev))
        chs = collections.defaultdict(lambda: {"programs": [], "notes": 0, "lo": 128, "hi": -1, "vel": 0})
        ccs = collections.defaultdict(list); bend = []; prog_changes = 0; active = []; maxp = 0; cur = {}
        for t, k, ch, a, v in ev:
            if k == 0xc0:
                prog_changes += 1; cur[ch] = a
                if a not in chs[ch]["programs"]: chs[ch]["programs"].append(a)
            elif k == 0x90 and v > 0:
                c = chs[ch]; c["notes"] += 1; c["lo"] = min(c["lo"], a); c["hi"] = max(c["hi"], a); c["vel"] += v
                active.append(t)
            elif k == 0xb0:
                ccs[a].append((t, v))
            elif k == 0xe0:
                bend.append(a | (v << 7))
        r["notes"] = sum(c["notes"] for c in chs.values())
        # polyphony by 1/16 of a second windows would be heavy; use onsets within 1 tick-beat/16
        if active:
            w = max(1, div // 4); cnt = collections.Counter(t // w for t in active); maxp = max(cnt.values())
        r["max_poly"] = maxp
        r["channels"] = {}
        fams = set()
        for ch, c in sorted(chs.items()):
            if not c["notes"]: continue
            prog = c["programs"][0] if c["programs"] else 0; fam = "drums" if ch == 9 else FAM[prog // 8]
            fams.add(fam)
            r["channels"][str(ch)] = {"programs": c["programs"][:6], "notes": c["notes"], "lo": c["lo"], "hi": c["hi"], "vel_mean": round(c["vel"] / c["notes"], 1), "family": fam}
        r["families"] = sorted(fams); r["n_families"] = len(fams)
        r["ccs"] = {}
        for cc, xs in sorted(ccs.items()):
            xs.sort(); vals = [v for _, v in xs]; t0, t1 = sec(xs[0][0]), sec(xs[-1][0])
            r["ccs"][str(cc)] = {"events": len(xs), "distinct": len(set(vals)), "first_s": round(t0, 2), "last_s": round(t1, 2),
                                 "rate_per_s": round(len(xs) / max(t1 - t0, 1.0), 3), "movement": sum(abs(y - x) for x, y in zip(vals, vals[1:]))}
        r["bend"] = {"events": len(bend), "range": [min(bend), max(bend)]} if bend else {"events": 0}
        r["program_changes"] = prog_changes
        bpms = [60e6 / v for _, v in tm]
        r["tempo"] = {"changes": len(tm) - 1 if len(tm) > 1 else 0, "bpm_min": round(min(bpms), 2), "bpm_max": round(max(bpms), 2), "bpm_first": round(bpms[0], 2)}
        ms = [[t, *v] for t, k, v in meta if k == "meter"]; r["meters"] = ms[:8]; r["meter_changes"] = max(0, len(ms) - 1)
        r["keysigs"] = [[t, *v] for t, k, v in meta if k == "key"][:4]
        for q in EMO:
            if os.path.basename(path).startswith(q) or "/%s_" % q in path: r["emotion"] = EMO[q]
    except Exception as e:
        r["error"] = "%s: %s" % (type(e).__name__, str(e)[:80])
    return r


def files():
    out = []
    for dp, dn, fn in os.walk(ROOT, followlinks=True):
        out += [os.path.join(dp, f) for f in fn if f.lower().endswith((".mid", ".midi")) and "__MACOSX" not in dp]
    return sorted(out)


def cmd_run(a):
    have = set()
    if os.path.exists(OUT):
        for ln in open(OUT):
            try: have.add(json.loads(ln)["src"])
            except ValueError: pass
    todo = [p for p in files() if os.path.relpath(p, ROOT) not in have][: a.limit or None]; t0 = time.time(); n = 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True); print("%d done, %d to do" % (len(have), len(todo)), flush=True)
    with open(OUT, "a") as f, ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(row, todo, chunksize=64):
            f.write(json.dumps(r, separators=(",", ":")) + "\n"); n += 1
            if n % 5000 == 0: f.flush(); print("%d rows, %.0f s" % (n, time.time() - t0), flush=True)
            if os.path.exists(STOP) or (a.budget and time.time() - t0 > a.budget * 60): break
    print("wrote %d rows, %.0f s" % (n, time.time() - t0))


def cmd_counts(_a):
    c = collections.Counter(); n = 0
    for ln in open(OUT):
        r = json.loads(ln); n += 1
        for k, v in r.items():
            if v not in (None, [], {}, 0): c[k] += 1
        for cc in r.get("ccs", {}): c["cc%s" % cc] += 1
        if r.get("meter_changes"): c["meter_changes>0"] += 1
        if any(m[2:] != [4, 4] for m in r.get("meters", [])[:1]): c["non_4/4_first"] += 1
        if r.get("tempo", {}).get("changes"): c["tempo_changes>0"] += 1
    print("%d files" % n)
    for k, v in sorted(c.items(), key=lambda x: -x[1])[:60]: print("  %-18s %d" % (k, v))


def cmd_check(a):
    rows = [json.loads(l) for l in open(OUT)]; pick = rows[:: max(1, len(rows) // a.n)][: a.n]; bad = 0
    for r in pick:
        p = os.path.join(ROOT, r["src"])
        if "error" in r: print("ERR", r["src"], r["error"]); bad += 1; continue
        fmt, div, ev, meta = parse(open(p, "rb").read())
        n_on = sum(1 for e in ev if e[1] == 0x90 and e[4] > 0); n_cc = sum(1 for e in ev if e[1] == 0xb0)
        ok = n_on == r["notes"] and n_cc == sum(v["events"] for v in r["ccs"].values())
        bad += not ok
        print("%s %-60s notes %5d cc %5d fams %s %s" % ("ok " if ok else "BAD", r["src"][-60:], r["notes"], n_cc, r["families"], ("meters " + str(r["meters"][:2])) if r["meters"] else ""))
    print("%d / %d consistent" % (len(pick) - bad, len(pick)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["run", "check", "counts"]); ap.add_argument("--limit", type=int, default=0); ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--budget", type=float, default=0); ap.add_argument("--n", type=int, default=20); a = ap.parse_args()
    {"run": cmd_run, "check": cmd_check, "counts": cmd_counts}[a.cmd](a)


if __name__ == "__main__":
    main()
