#!/usr/bin/env python3
"""Ableton sets -> per-track mixer rows (fader dB, pan, send dB, device list, role guess) -> role table.
    mix_als.py run [ROOT ...] [--out ~/Infinite/training-data/mix/als_mix.jsonl]   one row per track, resumable
    mix_als.py table                                                              per-role medians -> mix_table.json
Read only. Sets under 'my music' are the owner's (licence owner); the rest are pack demo sets (local only)."""
import argparse, glob, gzip, json, math, os, re, statistics as st, sys
OUT = os.path.expanduser("~/Infinite/training-data/mix/als_mix.jsonl")
TABLE = os.path.expanduser("~/Infinite/repo/docs/plans/data-universe/feel/mix_table.json")
ROOTS = ["/Volumes/Wd-Black/my music", "/Volumes/Wd-Black/Ghost Syndicate Endorphin Future Garage Sample Pack",
         "/Volumes/Wd-Black/sm white label - 90s deep house & garage - sampler formats Project",
         "/Volumes/Wd-Black/random unreleased"]
ROLES = [("kick", r"kick|bd\b|bass ?drum"), ("snare", r"snare|clap|snr|rim"), ("hat", r"hat|hh\b|cymbal|ride|shaker"),
         ("perc", r"perc|conga|bongo|tom|drum|beat|loop"), ("sub", r"sub\b|808"), ("bass", r"bass|reese"),
         ("vocal", r"vox|vocal|voice|acap|sing"), ("pad", r"pad|atmos|ambien|texture|drone|string|choir"),
         ("lead", r"lead|pluck|arp|synth|melody|stab"), ("keys", r"piano|keys|rhodes|organ|chord|wurli"),
         ("fx", r"fx|riser|impact|sweep|noise|foley|vinyl|crash")]
DEV = re.compile(r"<(OriginalSimpler|MultiSampler|Operator|InstrumentVector|UltraAnalog|Drift|Meld|Tension|Collision|"
                 r"DrumGroupDevice|AuPluginDevice|PluginDevice|Reverb|Delay|Echo|Compressor2|Eq8|Saturator|Limiter|"
                 r"GlueCompressor|AutoFilter|Chorus2|Utility|StereoGain|Roar|Overdrive)\b")
TR = re.compile(r"<(MidiTrack|AudioTrack|GroupTrack|ReturnTrack)\b[^>]*?Id=\"(\d+)\"[^>]*>(.*?)</\1>", re.S)

def db(x):
    return round(20 * math.log10(max(x, 1e-6)), 1)

def val(tag, body):
    m = re.search(r"<%s>.*?<Manual Value=\"([-\d.e]+)\"" % tag, body, re.S)
    return float(m[1]) if m else None

def role(name, devs):
    n = name.lower()
    for r, rx in ROLES:
        if re.search(rx, n):
            return r
    return "drums" if "DrumGroupDevice" in devs else ""

def rows_of(path):
    s = gzip.open(path, "rt", errors="replace").read()
    own = "/my music/" in path
    bpm = re.search(r"<Tempo>.*?<Manual Value=\"([\d.]+)\"", s, re.S)
    out = []
    for m in TR.finditer(s):
        kind, tid, b = m[1], m[2], m[3]
        mx = re.search(r"<Mixer>(.*?)</Mixer>", b, re.S)
        if not mx:
            continue
        mx = mx[1]
        nm = re.search(r'<EffectiveName Value="([^"]*)"', b) or re.search(r'<UserName Value="([^"]+)"', b)
        name = nm[1] if nm else ""
        devs = sorted(set(DEV.findall(b)))
        sends = [db(float(x)) for x in re.findall(r"<TrackSendHolder[^>]*>.*?<Manual Value=\"([-\d.e]+)\"", mx, re.S)]
        on = re.search(r"<On>.*?<Manual Value=\"(true|false)\"", mx, re.S)
        vol, pan = val("Volume", mx), val("Pan", mx)
        if vol is None:
            continue
        out.append({"set": path, "own": own, "bpm": float(bpm[1]) if bpm else None, "kind": kind, "name": name,
                    "role": role(name, devs) if kind != "ReturnTrack" else "return", "vol_db": db(vol),
                    "pan": pan, "sends_db": sends, "on": (on[1] == "true") if on else True, "devices": devs})
    return out

def run(roots, out):
    done = set()
    if os.path.exists(out):
        done = {json.loads(l)["set"] for l in open(out)}
    files = sorted({p for r in roots for p in glob.glob(r + "/**/*.als", recursive=True) if "/._" not in p and "Backup" not in p})
    todo = [f for f in files if f not in done]
    print(len(files), "sets,", len(todo), "to do", flush=True)
    with open(out, "a") as o:
        for i, f in enumerate(todo):
            try:
                for r in rows_of(f):
                    o.write(json.dumps(r) + "\n")
                o.flush()
            except Exception as e:
                print("skip", f, e)
            if i % 25 == 0:
                print(i, "/", len(todo), flush=True)

def table():
    rows = [json.loads(l) for l in open(OUT)]
    rows = [r for r in rows if r["on"] and r["role"] and r["kind"] != "GroupTrack"]
    t = {}
    for rl in sorted({r["role"] for r in rows}):
        g = [r for r in rows if r["role"] == rl]
        q = lambda xs, p: round(st.quantiles(xs, n=20)[p], 1) if len(xs) > 20 else (round(st.median(xs), 1) if xs else None)
        pans = [abs(r["pan"]) for r in g if r["pan"] is not None]
        s1 = [r["sends_db"][0] for r in g if r["sends_db"] and r["sends_db"][0] > -60]
        t[rl] = {"n": len(g), "sets": len({r["set"] for r in g}), "vol_db_median": round(st.median(r["vol_db"] for r in g), 1),
                 "vol_db_p25": q([r["vol_db"] for r in g], 4), "vol_db_p75": q([r["vol_db"] for r in g], 14),
                 "abs_pan_median": round(st.median(pans), 2) if pans else None,
                 "send_a_active_frac": round(len(s1) / len(g), 2), "send_a_db_median": round(st.median(s1), 1) if s1 else None}
    json.dump(t, open(TABLE, "w"), indent=1)
    for k, v in t.items():
        print(k, v)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("cmd"); ap.add_argument("roots", nargs="*"); ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    run(a.roots or ROOTS, a.out) if a.cmd == "run" else table()
