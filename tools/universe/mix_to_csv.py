#!/usr/bin/env python3
"""Write measured finished-track targets (mix_audio_table.json) into feel/dataset/4_Mix.csv as 'genre mix target' rows with evidence M.
Idempotent: drops earlier 'measured master:' rows first. Where a folder maps to a prior genre id (MAP), that prior row's evidence is
replaced with a pointer to the measured row and its numbers are overwritten."""
import csv, json, os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "docs/plans/data-universe/feel")
CSV = os.path.join(D, "dataset", "4_Mix.csv")
MAP = {"House": "house", "Classic House": "house", "Jazz": "jazz", "Jazz House": "jazz_house", "Organic House": "organic_house", "ambience": "ambient"}
t = json.load(open(os.path.join(D, "mix_audio_table.json")))
rows = list(csv.reader(open(CSV))); head, body = rows[0], [r for r in rows[1:] if not r[0].startswith("measured master:")]
def numtxt(lufs, crest, sm, b, n, scope):
    return ("LUFS %.1f; crest %.1f dB; side/mid %.2f; bands sub %.2f bass %.2f lowmid %.2f mid %.2f himid %.2f high %.2f (share of energy); n=%d %s"
            % (lufs, crest, sm, b["sub"], b["bass"], b["lowmid"], b["mid"], b["himid"], b["high"], n, scope))
new = [["measured master: all finished tracks", "genre mix target", "all", numtxt(t["lufs"][1], t["crest_db"][1], t["side_mid"][1], {k: v[1] for k, v in t["bands"].items()}, t["n"], "owner library tracks"),
        "", "", "", "0", "", "master chain: limiter to %.1f LUFS; keep crest >= %.1f dB" % (t["lufs"][1], t["crest_db"][1]), "all", "M (mix_audio.py, WD drive finished tracks; median)"]]
for g, v in t["by_genre"].items():
    new.append(["measured master: " + g, "genre mix target", MAP.get(g, g), numtxt(v["lufs"], v["crest_db"], v["side_mid"], v["bands"], v["n"], "tracks in folder '%s'" % g), "", "", "", "0", "",
                "master chain: limiter to %.1f LUFS; keep crest >= %.1f dB; width side/mid %.2f" % (v["lufs"], v["crest_db"], v["side_mid"]), "all", "M (mix_audio.py, n=%d, folder median)" % v["n"]])
for r in body:
    gid = r[2]
    if r[1] == "genre mix target" and gid in {MAP[g] for g in t["by_genre"] if g in MAP}:
        r[-1] = "R prior, superseded by measured master rows for this genre (see 'measured master:')"
csv.writer(open(CSV, "w", newline="")).writerows([head] + new + body)
print(len(new), "measured rows written;", len(body), "others kept")
