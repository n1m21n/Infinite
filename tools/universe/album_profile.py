#!/usr/bin/env python3
"""Per-track audio profile of the owner's own releases (n1m21n-room site data) = the owner's measured characteristics.
    album_profile.py run [--seconds 600] [--resume]     v1 + v2 features per track -> ~/Infinite/training-data/albums/n1m21n_tracks.jsonl
    album_profile.py report                              per-album and overall table vs the library corpus (audio/features.jsonl)
Tracks come from site/public/data/music.json (src relative to site/public). Full tracks, one process, so it can run beside the
audio pass. Statistics only; nothing leaves the machine."""
import argparse, json, os, sys, time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_features as af, audio_features2 as af2

ROOM = os.path.expanduser("~/music videos/n1m21n-room/site/public")
OUT = os.path.expanduser("~/Infinite/training-data/albums/n1m21n_tracks.jsonl")


def _as_songs():
    """the library tagger reads a filename to decide loop vs one-shot; whole songs are loops, so tempo, key and groove run"""
    t1, t2 = af.tags, af2.labels
    af.tags = lambda *a, **k: {**t1(*a, **k), "loop": True}
    af2.labels = lambda *a, **k: {**t2(*a, **k), "loop": True}


def tracks():
    d = json.load(open(os.path.join(ROOM, "data", "music.json")))
    for al in d["albums"]:
        for t in al["tracks"]:
            yield al, t


def run(a):
    _as_songs()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    done = set()
    if a.resume and os.path.exists(OUT):
        done = {json.loads(l)["src"] for l in open(OUT) if l.endswith("\n")}
    t0, n = time.time(), 0
    with open(OUT, "a" if a.resume else "w") as fo:
        for al, t in tracks():
            p = os.path.join(ROOM, t["src"])
            if p in done or not os.path.exists(p):
                continue
            r1 = af.analyse(p, ROOM, a.seconds)
            r2 = af2.analyse(p, ROOM, a.seconds)
            if not r1 or not r2:
                continue
            row = {"album": al["title"], "kind": al["kind"], "released": al.get("released"), "genre": al.get("genre"),
                   "n": t["n"], "title": t["title"], "dur": t.get("dur"), **{k: v for k, v in r1.items() if k not in ("src", "bands")},
                   **{k: v for k, v in r2.items() if k not in ("src", "mfcc_d") and (k not in r1 or r1[k] is None or k == "rolloff") and v is not None}, "src": p}
            fo.write(json.dumps(row, separators=(",", ":")) + "\n"); fo.flush()
            n += 1
            print("%3d %-28s %-28s %.0fs" % (n, al["title"][:28], t["title"][:28], time.time() - t0), flush=True)
            if (time.time() - t0) / 60 > a.budget:
                print("budget reached; rerun with --resume"); break


def med(v):
    v = [x for x in v if x is not None]
    return float(np.median(v)) if v else None


def report(a):
    rows = [json.loads(l) for l in open(OUT)]
    lib = []
    for l in open(os.path.expanduser("~/Infinite/training-data/audio/features.jsonl")):
        j = json.loads(l)
        if "bands" in j and j.get("loop") and j.get("seconds", 0) > 20:
            lib.append(j)
    cols = [("bpm", "tempo"), ("rms", "rms"), ("crest", "crest"), ("centroid", "centroid Hz"), ("rolloff", "rolloff Hz"),
            ("low", "low share"), ("high", "high share"), ("flatness", "flatness"), ("corr", "L/R corr"), ("harm", "harmonic")]
    print("%d tracks, %d albums; library loops for reference: %d" % (len(rows), len({r["album"] for r in rows}), len(lib)))
    print("%-26s %3s " % ("album", "n") + " ".join("%10s" % c[1][:10] for c in cols) + "  minor%")
    groups = {}
    for r in rows:
        groups.setdefault(r["album"], []).append(r)
    for name, rs in sorted(groups.items(), key=lambda kv: min(r.get("released") or "" for r in kv[1])):
        mn = [r["key"].endswith("m") for r in rs if r.get("key")]
        print("%-26s %3d " % (name[:26], len(rs)) + " ".join("%10s" % ("%.3g" % med([r.get(c) for r in rs]) if med([r.get(c) for r in rs]) is not None else "-") for c, _ in cols)
              + "  %3.0f" % (100 * sum(mn) / max(1, len(mn))))
    print("%-26s %3d " % ("ALL (median)", len(rows)) + " ".join("%10s" % ("%.3g" % med([r.get(c) for r in rows]) if med([r.get(c) for r in rows]) is not None else "-") for c, _ in cols))
    print("%-26s %3d " % ("library loops (median)", len(lib)) + " ".join("%10s" % ("%.3g" % med([r.get(c) for r in lib]) if med([r.get(c) for r in lib]) is not None else "-") for c, _ in cols))
    # where the owner sits inside the library distribution: percentile of the owner's median per column
    print("\nowner median as a percentile of the library:")
    for c, lab in cols:
        v = [x.get(c) for x in lib if x.get(c) is not None]
        m = med([r.get(c) for r in rows])
        if v and m is not None:
            print("  %-11s %5.1f  (library p10 %.3g / p50 %.3g / p90 %.3g)" % (lab, 100 * np.mean(np.array(v) < m), *np.percentile(v, [10, 50, 90])))
    keys = {}
    for r in rows:
        if r.get("key"): keys[r["key"]] = keys.get(r["key"], 0) + 1
    print("\nkeys:", sorted(keys.items(), key=lambda kv: -kv[1])[:10])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("run", "report"))
    ap.add_argument("--seconds", type=float, default=600)
    ap.add_argument("--budget", type=float, default=25, help="minutes")
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()
    run(a) if a.cmd == "run" else report(a)
