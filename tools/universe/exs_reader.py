#!/usr/bin/env python3
"""Logic EXS24 / Sampler instruments -> key maps: which recorded sample plays which notes and velocities.
    exs_reader.py [ROOT ...] [--out ~/Infinite/training-data/samplers/exs.jsonl]
An .exs is a run of little-endian chunks (read only): 4-byte signature (low nibble of byte 3 = kind: 0 instrument,
1 zone, 2 group, 3 sample, 4 params), 4-byte size (bit 15 is a flag), 8 bytes, 'JBOS'/'TBOS', 64-byte name, then
`size` bytes of data. Zone data: root key @1, fine tune @2 (signed), pan @3, key low @6, key high @7, velocity low @9,
high @10, sample start/end @12/@16, loop start/end @20/@24, loop on = bit 0 of @0? (zone opts), group @88, sample @92.
Sample chunks carry the folder the audio was recorded into @80 and the full file name @336.
Each sample is resolved to a file on the drive by name, first next to the .exs, then under the pack's top folder,
then under Logic's 'EXS Factory Samples'. One row per instrument: src, name, category, zones [{root, lo, hi, vlo, vhi,
fine, group, file}], resolved (share of zones with audio found). Licence: "sample library, local only"."""
import argparse, collections, json, os, struct, sys, time

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
AUDIO = (".wav", ".aif", ".aiff", ".caf", ".flac")


def chunks(b):
    i = 0
    while i + 84 <= len(b):
        kind = b[i + 3] & 0x0F
        size = struct.unpack("<I", b[i + 4:i + 8])[0] & ~0x8000
        if size > len(b):
            return
        yield kind, b[i + 20:i + 84].split(b"\0")[0].decode("latin-1"), b[i + 84:i + 84 + size]
        i += 84 + size


def read_exs(path):
    b = open(path, "rb").read()
    if b[16:20] not in (b"JBOS", b"TBOS", b"SOBT", b"SOBJ"):
        return None
    zones, samples, groups = [], [], 0
    for kind, name, d in chunks(b):
        if kind == 1 and len(d) >= 96:
            lo, hi = d[6], d[7]
            zones.append({"root": d[1], "fine": struct.unpack("b", d[2:3])[0], "lo": min(lo, hi), "hi": max(lo, hi),
                          "vlo": d[9] or 1, "vhi": d[10] or 127, "loop": bool(d[0] & 1),
                          "group": struct.unpack("<i", d[88:92])[0], "sample": struct.unpack("<i", d[92:96])[0]})
        elif kind == 2:
            groups += 1
        elif kind == 3:
            # the header name is cut to 64 bytes (with a #hash); the full file name sits at 336, its folder at 80
            full = d[336:592].split(b"\0")[0].decode("latin-1") if len(d) > 336 else ""
            samples.append((full or name, d[80:336].split(b"\0")[0].decode("latin-1")))
    for z in zones:
        s = z.pop("sample")
        z["name"], z["folder"] = samples[s] if 0 <= s < len(samples) else ("", "")
    return zones, groups


def index(roots):
    """audio basename (lower) -> [paths] under each root"""
    idx = collections.defaultdict(list)
    for r in roots:
        for d, subs, fs in os.walk(r):
            subs[:] = [s for s in subs if s not in SKIP and not s.startswith(".")]
            for f in fs:
                if f.lower().endswith(AUDIO) and not f.startswith("._"):
                    idx[f.lower()].append(os.path.join(d, f))
    return idx


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--factory", nargs="*", default=["/Volumes/Wd-Black/Library/Application Support/Logic/EXS Factory Samples",
                    "/Volumes/Wd-Black/Library/Application Support/GarageBand/Instrument Library/Sampler/Sampler Files"])
    ap.add_argument("--out", default="~/Infinite/training-data/samplers/exs.jsonl")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    t0 = time.time()
    found = []
    for root in a.roots:
        for d, subs, fs in os.walk(root):
            subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
            found += [os.path.join(d, f) for f in sorted(fs) if f.lower().endswith(".exs") and not f.startswith("._")]
    print("%d .exs in %.0f s" % (len(found), time.time() - t0))
    tops = sorted({os.path.join("/Volumes/Wd-Black", os.path.relpath(p, "/Volumes/Wd-Black").split(os.sep)[0]) for p in found})
    tops = [t for t in tops if not t.endswith("/Library")] + [f for f in a.factory if os.path.isdir(f)]
    idx = index(tops)
    print("indexed %d audio names under %d folders in %.0f s" % (len(idx), len(tops), time.time() - t0))
    n = collections.Counter()
    with open(out, "w") as fo:
        for p in found:
            try:
                r = read_exs(p)
            except (OSError, struct.error, IndexError):
                r = None
            if not r or not r[0]:
                n["unreadable"] += 1
                continue
            zones, groups = r
            here = os.path.dirname(p)
            hit = 0
            for z in zones:
                cands = idx.get(z["name"].lower(), [])
                # prefer the copy whose folder matches the recorded folder tail, then the one nearest the .exs
                tail = z.pop("folder").rstrip("/").split("/")[-1].lower()
                best = sorted(cands, key=lambda c: (os.path.basename(os.path.dirname(c)).lower() != tail,
                                                    not c.startswith(os.path.dirname(here))))
                z["file"] = best[0] if best else None
                hit += bool(best)
            rel = p.split(os.sep)
            row = {"src": p, "name": os.path.splitext(rel[-1])[0], "category": "/".join(rel[-3:-1]), "groups": groups,
                   "zones": zones, "resolved": round(hit / len(zones), 3), "keys": [min(z["lo"] for z in zones), max(z["hi"] for z in zones)],
                   "vel_layers": len({(z["vlo"], z["vhi"]) for z in zones}), "license": "sample library, local only"}
            fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            n["instruments"] += 1
            n["zones"] += len(zones)
            n["zones_with_audio"] += hit
            n["fully_resolved"] += hit == len(zones)
    print(dict(n), "in %.0f s" % (time.time() - t0))
    print("wrote", out)


if __name__ == "__main__":
    main()
