#!/usr/bin/env python3
"""Build per-genre drum kits for the Drum Sequencer from a local sample drive, as a user library.

  build_kits.py --source /Volumes/Wd-Black            write kits to the user library (default --out below)
  build_kits.py --source DIR --out DIR --per-genre 3  choose the output folder and kits per genre
  build_kits.py --check                               fail if any file under assets/ or a built app bundle
                                                      is (or links to) a file on the source drive

Licence rule: the drive's packs are commercial. Their sounds may be used on this machine but never copied
into the repo, the app bundle, the website or a release. So a kit is a JSON file of absolute paths
(lane -> path, gain_db, tune); the audio is never copied. The shipped app keeps assets/drumkits/infinite-basic.

Output layout (what DrumSequencerNode reads):
  <out>/<genre-slug>/<kit-name>.json   {"name", "genre", "lanes": [8 x {"role", "path", "gain_db", "tune"}]}
  <out>/index.json                     every role found per genre and the fallbacks used (the report)
  <out>/pins.json  (optional, hand)    {"<genre>": {"<role>": "/abs/path.wav"}} forces a file for a role

Lanes follow DrumPatterns::LaneRole: kick, snare, closed hat, open hat, clap / rim, low tom / conga,
high tom / conga, bell / ride.
"""
import argparse, json, os, pathlib, re, struct, sys, wave

try:
    import aifc
except ImportError:      # removed in Python 3.13; AIFF files are then skipped
    aifc = None
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_OUT = pathlib.Path.home() / "Library/Application Support/Infinite/kits"
DEFAULT_SOURCE = "/Volumes/Wd-Black"

GENRES = ["Latin & Caribbean", "Electronic", "Rock & Pop", "Brazil", "Funk & Breaks",
          "Middle East & North Africa", "Hip Hop", "Africa & World", "India", "Jazz"]

# Pack folder name (lower-case substring) -> genres it serves. Only these packs are scanned.
PACKS = [
    ("classic jungle",              ["Funk & Breaks", "Electronic"]),
    ("star power drums",            ["Funk & Breaks", "Electronic"]),
    ("drum.and.bass",               ["Funk & Breaks", "Electronic"]),
    ("flex_dnb",                    ["Funk & Breaks", "Electronic"]),
    ("tr-909",                      ["Electronic"]),
    ("dub techno drum kits",        ["Electronic"]),
    ("raw underground house",       ["Electronic"]),
    ("deeper house",                ["Electronic"]),
    ("sexy deep house drums",       ["Electronic"]),
    ("old school house and garage", ["Electronic"]),
    ("90s deep house & garage",     ["Electronic"]),
    ("acid house",                  ["Electronic"]),
    ("swing house mechanics",       ["Electronic", "Funk & Breaks"]),
    ("dark techno",                 ["Electronic"]),
    ("tape and vinyl drums",        ["Hip Hop", "Funk & Breaks", "Jazz", "Rock & Pop"]),
    ("60s soul",                    ["Hip Hop", "Funk & Breaks", "Jazz", "Rock & Pop"]),
    ("soul analogue",               ["Hip Hop", "Funk & Breaks", "Jazz", "Rock & Pop"]),
    ("soulful vinyl",               ["Hip Hop", "Jazz"]),
    ("downtempo",                   ["Hip Hop"]),
    ("kb6_drum_samples",            ["Hip Hop", "Rock & Pop"]),
    ("vengeance samples",           ["Electronic", "Rock & Pop", "Hip Hop"]),
    ("ashram afro",                 ["Africa & World", "Latin & Caribbean", "Brazil", "Middle East & North Africa", "India"]),
    ("afro-latin percussion",       ["Latin & Caribbean", "Brazil", "Africa & World", "Middle East & North Africa", "India"]),
    ("afro melodic house",          ["Africa & World", "Electronic"]),
    ("afro house session",          ["Africa & World", "Latin & Caribbean"]),
    ("organic house",               ["Africa & World", "Middle East & North Africa", "India"]),
    ("discovery series cuba",       ["Latin & Caribbean", "Brazil"]),
]

# Role words, matched against path tokens (file name first, then folders). Order matters: first hit wins.
ROLE_WORDS = [
    ("open hat",   [("open", "hat"), ("open", "hh"), ("ohh",), ("oh",), ("openhat",), ("ophat",)]),
    ("closed hat", [("closed", "hat"), ("chh",), ("ch",), ("hihat",), ("hh",), ("hat",), ("hats",), ("closedhat",)]),
    ("ride",       [("ride",)]),
    ("crash",      [("crash",), ("cymbal",)]),
    ("kick",       [("kick",), ("kicks",), ("kik",), ("bd",), ("bassdrum",)]),
    ("snare",      [("snare",), ("snares",), ("snr",), ("sd",)]),
    ("clap",       [("clap",), ("claps",), ("clp",)]),
    ("rim",        [("rim",), ("rimshot",), ("sidestick",), ("stick",)]),
    ("conga",      [("conga",), ("congas",), ("tumba",), ("quinto",), ("bongo",), ("bongos",), ("djembe",), ("tabla",),
                    ("darbuka",), ("doumbek",), ("dhol",)]),
    ("tom",        [("tom",), ("toms",), ("floortom",)]),
    ("bell",       [("cowbell",), ("bell",), ("bells",), ("agogo",), ("campana",), ("gankogui",)]),
    ("shaker",     [("shaker",), ("shakers",), ("shkr",), ("maraca",), ("maracas",), ("cabasa",), ("tambourine",), ("tamb",)]),
    ("perc",       [("perc",), ("percussion",), ("block",), ("clave",), ("claves",), ("guiro",), ("timbale",), ("timbales",)]),
]
SKIP_WORDS = {"loop", "loops", "break", "breaks", "fill", "fills", "groove", "grooves", "top", "toploops", "full",
              "construction", "kits_loops", "midi"}

# Loudness target per role, RMS dBFS over the first 150 ms (cuts, never boosts much: lane volume tops at 1.0).
TARGET = {"kick": -14, "snare": -16, "closed hat": -24, "open hat": -22, "clap": -17, "rim": -19, "conga": -18,
          "tom": -17, "bell": -20, "ride": -23, "crash": -22, "shaker": -24, "perc": -20}

PERC_GENRES = {"Latin & Caribbean", "Brazil", "Africa & World", "Middle East & North Africa", "India"}
# Lane -> roles to try in order, per genre family.
LANES_KIT = [["kick"], ["snare"], ["closed hat"], ["open hat", "closed hat"], ["clap", "rim", "snare"],
             ["tom", "conga", "perc"], ["tom", "conga", "perc"], ["bell", "ride", "perc", "shaker"]]
LANES_PERC = [["kick"], ["snare", "rim"], ["closed hat", "shaker"], ["open hat", "shaker"], ["clap", "rim"],
              ["conga", "tom", "perc"], ["conga", "tom", "perc"], ["bell", "perc", "ride"]]
BORROW = {"Latin & Caribbean": ["Funk & Breaks", "Hip Hop"], "Brazil": ["Funk & Breaks", "Hip Hop"],
          "Africa & World": ["Hip Hop"], "Middle East & North Africa": ["Hip Hop"], "India": ["Hip Hop"],
          "Jazz": ["Hip Hop", "Funk & Breaks"]}
LANE_ROLE = ["kick", "snare", "closed hat", "open hat", "clap / rim", "low tom / conga", "high tom / conga", "bell / ride"]


def tokens(text):
    return [t for t in re.split(r"[^a-z0-9]+", text.lower()) if t]


def role_of(path):
    """Role from the file name, then from its folders (nearest first)."""
    parts = [path.stem] + [p for p in reversed(path.parent.parts[-4:])]
    for part in parts:
        toks = tokens(part)
        joined = " ".join(toks)
        for role, pats in ROLE_WORDS:
            for pat in pats:
                if len(pat) == 1 and pat[0] in toks:
                    return role
                if len(pat) > 1 and all(w in toks for w in pat):
                    return role
                if len(pat) == 1 and len(pat[0]) >= 4 and pat[0] in joined.replace(" ", ""):
                    return role
    return None


def read_audio(path, max_sec=2.0):
    """Mono float32 + rate, or None. PCM 16/24/32 and float32 WAV, AIFF when aifc exists."""
    try:
        if path.suffix.lower() == ".wav":
            with open(path, "rb") as f:
                data = f.read()
            if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
                return None
            pos, fmt, raw = 12, None, None
            while pos + 8 <= len(data):
                cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
                body = data[pos + 8:pos + 8 + size]
                if cid == b"fmt ":
                    tag, ch, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
                    if tag == 0xFFFE and len(body) >= 26:
                        tag = struct.unpack("<H", body[24:26])[0]
                    fmt = (tag, ch, rate, bits)
                elif cid == b"data":
                    raw = body
                pos += 8 + size + (size & 1)
            if not fmt or raw is None:
                return None
            tag, ch, rate, bits = fmt
            if ch < 1 or rate <= 0 or len(raw) / (ch * bits // 8) / rate > max_sec:
                return None
            if tag == 3 and bits == 32:
                x = np.frombuffer(raw[:len(raw) // 4 * 4], "<f4")
            elif tag == 1 and bits == 16:
                x = np.frombuffer(raw[:len(raw) // 2 * 2], "<i2") / 32768.0
            elif tag == 1 and bits == 24:
                b = np.frombuffer(raw[:len(raw) // 3 * 3], np.uint8).reshape(-1, 3)
                x = ((b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16)) << 8 >> 8) / 8388608.0
            elif tag == 1 and bits == 32:
                x = np.frombuffer(raw[:len(raw) // 4 * 4], "<i4") / 2147483648.0
            else:
                return None
            x = x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1)
            return x.astype(np.float32), rate
        if aifc and path.suffix.lower() in (".aif", ".aiff"):
            with aifc.open(str(path)) as a:
                ch, w, rate, n = a.getnchannels(), a.getsampwidth(), a.getframerate(), a.getnframes()
                if n / rate > max_sec or w != 2:
                    return None
                x = np.frombuffer(a.readframes(n), ">i2") / 32768.0
            return x[:len(x) // ch * ch].reshape(-1, ch).mean(axis=1).astype(np.float32), rate
    except Exception:
        return None
    return None


def measure(x, rate):
    """(rms dBFS over the first 150 ms, spectral centroid Hz, decay seconds to -30 dB)."""
    head = x[:max(1, int(0.15 * rate))]
    rms = float(np.sqrt(np.mean(head * head)) + 1e-9)
    seg = x[:min(len(x), 8192)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) if len(seg) > 16 else np.zeros(2)
    freqs = np.fft.rfftfreq(len(seg), 1.0 / rate) if len(seg) > 16 else np.zeros(2)
    centroid = float((spec * freqs).sum() / (spec.sum() + 1e-9))
    env = np.abs(x)
    peak = float(env.max() + 1e-9)
    above = np.nonzero(env > peak * 0.0316)[0]
    decay = float(above[-1] / rate) if len(above) else 0.0
    return 20 * np.log10(rms), centroid, decay


def plausible(role, centroid, decay):
    """Spectral check on the name-given role; drops obvious mislabels (a 'kick' that is all hiss)."""
    if role == "kick":
        return centroid < 2500
    if role in ("closed hat", "shaker"):
        return centroid > 2500 and decay < 1.0
    if role == "open hat":
        return centroid > 2500
    if role in ("ride", "crash"):
        return centroid > 2000
    return True


def scan(source, packs):
    found = []   # (genres, pack, role, path)
    for name in sorted(os.listdir(source)):
        low = name.lower()
        genres = next((g for key, g in packs if key in low), None)
        if not genres:
            continue
        for dirpath, dirnames, filenames in os.walk(os.path.join(source, name)):
            dirnames[:] = [d for d in dirnames if not (set(tokens(d)) & SKIP_WORDS) and not d.startswith(".")]
            for fn in filenames:
                p = pathlib.Path(dirpath) / fn
                if fn.startswith("._") or p.suffix.lower() not in (".wav", ".aif", ".aiff"):
                    continue
                if set(tokens(p.stem)) & SKIP_WORDS:
                    continue
                role = role_of(p)
                if role:
                    found.append((genres, name, role, p))
    return found


def build(source, out, per_genre, verbose):
    pins = {}
    pin_file = out / "pins.json"
    if pin_file.exists():
        pins = json.loads(pin_file.read_text())
    cands = scan(source, PACKS)
    if verbose:
        print(f"scan: {len(cands)} named one-shot candidates")
    # Measure lazily: per (genre, pack, role) keep the first few plausible files.
    pool = {}   # genre -> pack -> role -> [(path, rms, centroid)]
    cache = {}
    for genres, pack, role, p in cands:
        slot_full = all(len(pool.get(g, {}).get(pack, {}).get(role, [])) >= 4 for g in genres)
        if slot_full:
            continue
        if p not in cache:
            a = read_audio(p)
            cache[p] = measure(*a) if a else None
        m = cache[p]
        if not m or not plausible(role, m[1], m[2]):
            continue
        for g in genres:
            lst = pool.setdefault(g, {}).setdefault(pack, {}).setdefault(role, [])
            if len(lst) < 4:
                lst.append((str(p), m[0], m[1]))

    index = {"source": str(source), "genres": {}}
    out.mkdir(parents=True, exist_ok=True)
    for genre in GENRES:
        slug = re.sub(r"[^a-z0-9]+", "-", genre.lower()).strip("-")
        gdir = out / slug
        gdir.mkdir(exist_ok=True)
        for old in gdir.glob("*.json"):
            old.unlink()
        packs = pool.get(genre, {})
        # Rank packs by how many lane roles they cover on their own.
        lanes = LANES_PERC if genre in PERC_GENRES else LANES_KIT
        def cover(pk):
            return sum(any(r in packs[pk] for r in want) for want in lanes)
        ranked = sorted(packs, key=lambda pk: -cover(pk))
        # Fewer packs than kits: the extra kits are variants of the best packs (other files per role).
        ranked = [ranked[i % len(ranked)] for i in range(per_genre)] if ranked else []
        if genre == "Jazz":
            lanes = lanes[:7] + [["ride", "bell", "perc"]]
        all_roles = {}
        for pk in packs:
            for r, lst in packs[pk].items():
                all_roles.setdefault(r, []).extend(lst)
        report = {"roles": {r: len(v) for r, v in sorted(all_roles.items())}, "kits": []}
        # A role the genre's packs lack (no kick in a hand-percussion pack) is borrowed from the nearest
        # acoustic genre, then any genre; listed as a fallback in the report.
        for donor in BORROW.get(genre, []) + GENRES:
            for pk2, roles2 in pool.get(donor, {}).items():
                for r, lst in roles2.items():
                    if r not in all_roles:
                        all_roles[r] = list(lst)
        for k, pk in enumerate(ranked):
            kit_lanes, fallbacks = [], []
            for lane, want in enumerate(lanes):
                pick, src_role = None, None
                pinned = pins.get(genre, {}).get(LANE_ROLE[lane])
                if pinned:
                    pick, src_role = (pinned, TARGET.get(want[0], -18), 0.0), want[0]
                for r in want:
                    if pick:
                        break
                    lst = packs[pk].get(r) or all_roles.get(r)
                    if lst:
                        # Low lane takes the darker file, high lane the brighter (toms / congas).
                        ordered = sorted(lst, key=lambda e: e[2])
                        pick = ordered[k % len(ordered) // 2] if lane == 5 else ordered[-1 - k % len(ordered) // 2] if lane == 6 \
                            else lst[k % len(lst)]
                        src_role = r
                        if r != want[0] or not packs[pk].get(r):
                            fallbacks.append(f"{LANE_ROLE[lane]}: {r}" + ("" if packs[pk].get(r) else " (other pack)"))
                if not pick:
                    kit_lanes.append({"role": LANE_ROLE[lane], "path": "", "gain_db": 0.0, "tune": 0.0})
                    fallbacks.append(f"{LANE_ROLE[lane]}: none (bundled kit)")
                    continue
                gain = max(-18.0, min(1.9, TARGET[src_role] - pick[1]))
                kit_lanes.append({"role": LANE_ROLE[lane], "path": pick[0], "gain_db": round(gain, 1), "tune": 0.0})
            name = f"{k + 1:02d} {re.sub(r'[._]+', ' ', pk).strip()}" + (f" v{k // len(set(ranked)) + 1}" if ranked.count(pk) > 1 else "")
            (gdir / f"{k + 1:02d}.json").write_text(json.dumps(
                {"name": name, "genre": genre, "lanes": kit_lanes}, indent=1) + "\n")
            report["kits"].append({"name": name, "fallbacks": fallbacks})
        index["genres"][genre] = report
        if verbose:
            print(f"{genre}: {len(report['kits'])} kits, roles {report['roles']}")
            for kit in report["kits"]:
                print(f"   {kit['name']}" + (f"  fallbacks: {', '.join(kit['fallbacks'])}" if kit["fallbacks"] else ""))
    (out / "index.json").write_text(json.dumps(index, indent=1) + "\n")
    return index


def check(source):
    """Fail if anything under assets/ or a built app's Resources is on (or links to) the source drive."""
    bad = []
    src = os.path.realpath(source)
    roots = [ROOT / "assets", ROOT / "website"] + [p / "Contents/Resources" for p in ROOT.glob("build*/Infinite.app")]
    for r in roots:
        if not r.exists():
            continue
        for p in r.rglob("*"):
            real = os.path.realpath(p)
            if real.startswith(src + os.sep) or real == src:
                bad.append(str(p))
            elif p.suffix == ".json" and p.stat().st_size < 2_000_000 and source in p.read_text(errors="ignore"):
                bad.append(f"{p} (names {source})")
    for b in bad:
        print(f"DRUMKITS: {b} comes from {source}: commercial samples never enter the repo, bundle or a release")
    print(f"drumkits check: {len(bad)} problem(s)")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--per-genre", type=int, default=3)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()
    if a.check:
        return check(a.source)
    out = pathlib.Path(os.path.expanduser(a.out)).resolve()
    if str(out).startswith(str(ROOT)):
        print("refusing: --out is inside the repo; kits are a local user library only")
        return 2
    if not os.path.isdir(a.source):
        print(f"source not found: {a.source}")
        return 2
    build(a.source, out, a.per_genre, not a.quiet)
    print(f"kits written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
