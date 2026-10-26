#!/usr/bin/env python3
"""Public drum MIDI -> drum_groove pieces, gated like the card grooves.
    public_drums.py --groove DIR --egmd DIR [--per-file 2] [--cap-egmd 3000]
Groove MIDI (Magenta, CC BY 4.0) and E-GMD (Magenta, CC BY 4.0): general-MIDI drum files with a csv.
Each 4/4 file gives up to --per-file one-bar windows (the first bar is skipped: it is often a count-in),
quantised to sixteenths into the card's eight lanes. Statistics are what we keep; the MIDI is never shipped."""
import argparse, collections, csv, glob, json, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces as P

LANE_NOTES = [(35, 36), (37, 38, 40), (42, 44), (46,), (39,), (41, 43, 45), (47, 48, 50), (56, 53, 54)]  # kick..bell


def bars(path, per_file):
    notes, div = P.midi_notes(path)
    if not notes:
        return
    sixteenth = div / 4.0
    grid = collections.defaultdict(lambda: [["."] * 16 for _ in LANE_NOTES])
    for t, n in notes:
        for li, ns in enumerate(LANE_NOTES):
            if n in ns:
                s = round(t / sixteenth)
                grid[s // 16][li][s % 16] = "x"
    got = 0
    for b in sorted(grid):
        if b == 0:
            continue
        lanes = ["".join(l) if "x" in l else "" for l in grid[b]]
        if any(lanes):
            yield lanes
            got += 1
            if got >= per_file:
                return


def load(root, csv_name, tag, per_file, cap):
    path = glob.glob(os.path.join(root, "**", csv_name), recursive=True)[0]
    rows = [r for r in csv.DictReader(open(path)) if r["time_signature"] == "4-4"]
    random.Random(7).shuffle(rows)
    base = os.path.dirname(path)
    for r in rows[:cap]:
        f = os.path.join(base, r["midi_filename"])
        if not os.path.exists(f):
            continue
        style = r["style"].split("/")[0]
        try:
            for lanes in bars(f, per_file):
                yield {"type": "drum_groove", "src": tag + ":" + r["midi_filename"], "family": style,
                       "lanes": lanes, "swing": 0.0, "groove": None, "bpm": float(r["bpm"])}
        except Exception:
            continue


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--groove"); ap.add_argument("--egmd")
    ap.add_argument("--per-file", type=int, default=2)
    ap.add_argument("--cap-egmd", type=int, default=3000)
    ap.add_argument("--out", default="public_drums.jsonl")
    a = ap.parse_args()
    srcs = []
    if a.groove: srcs.append(load(a.groove, "info.csv", "groove", a.per_file, 10 ** 9))
    if a.egmd: srcs.append(load(a.egmd, "e-gmd-v1.0.0.csv", "egmd", a.per_file, a.cap_egmd))
    seen, rows = set(), []
    for it in srcs:
        for p in it:
            h, s = P.gate_drum(p)
            key = P.digest(p["lanes"])
            if not h and key in seen:
                h = ["duplicate of an earlier piece"]
            seen.add(key)
            p.update(id=key, hard=h, soft=s, ok=not h, license="CC-BY-4.0")
            rows.append(p)
    with open(a.out, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    for tag in ("groove", "egmd"):
        r = [x for x in rows if x["src"].startswith(tag)]
        if not r: continue
        why = collections.Counter(x for q in r for x in q["hard"])
        print("%-7s pieces %6d  ok %6d (%.0f%%)  hard: %s" % (tag, len(r), sum(q["ok"] for q in r),
              100.0 * sum(q["ok"] for q in r) / len(r), "; ".join("%s x%d" % kv for kv in why.most_common()) or "-"))
        print("   soft:", dict(collections.Counter(x for q in r for x in q["soft"]).most_common(4)))
        print("   styles:", dict(collections.Counter(q["family"] for q in r if q["ok"]).most_common(6)))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
