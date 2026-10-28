#!/usr/bin/env python3
"""Binary preset / groove formats -> JSONL rows (step 2b+ of docs/plans/data-universe).
    binary_preset_reader.py ROOT [ROOT ...] --out DIR [--only agr|bwpreset|guitarrig|vitalwt] [--budget 1200]
Read only; black-box inspection of the bytes (no vendor code). One output file per kind in --out, one row per file
(per table for Vital), each with a "license" field. Python 3.9, numpy used when present (Vital shapes only).
  agr        Ableton groove (.agr): gzip XML; timing / velocity / random / quantise amounts and the per-step notes
             (time, duration, velocity) of the groove clip
  bwpreset   Bitwig preset (.bwpreset): 'BtWg' header with a text metadata block (name, creator, tags, device ...);
             the body is read as printable strings and numbers; zlib streams are inflated and mined the same way
  guitarrig  NI Guitar Rig (.ngrr / .nkp): container inflated with zlib when it holds a zlib stream; component names
             and numeric parameter values as found
  vitalwt    Vital presets (.vital): every oscillator wavetable's source components; 'Wave Source' keyframes
             (base64 float32 x 2048) become harmonic shapes in the wt_reader.py row format; audio-file sources
             and effects are counted, not decoded
Licence: "preset library, local only"."""
import argparse, base64, collections, gzip, hashlib, json, os, re, struct, sys, time, zlib
import xml.etree.ElementTree as ET

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
LIC = "preset library, local only"
EXT = {"agr": (".agr",), "bwpreset": (".bwpreset",), "guitarrig": (".ngrr", ".nkp"), "vitalwt": (".vital",)}


def num(v):
    try:
        f = float(v)
        return round(f, 5) if f == f and abs(f) < 1e12 else None
    except (TypeError, ValueError):
        return None


def base(path, kind, name=None):
    return {"src": path, "kind": kind, "name": name or os.path.splitext(os.path.basename(path))[0],
            "category": os.path.basename(os.path.dirname(path)), "license": LIC}


def strings(b, lo=4):
    return [m.group().decode("latin-1") for m in re.finditer(rb"[\x20-\x7e]{%d,}" % lo, b)]


def inflate_all(b):
    """every zlib stream found in b (by header scan), inflated; yields (offset, bytes)"""
    for m in re.finditer(rb"\x78[\x01\x5e\x9c\xda]", b):
        try:
            d = zlib.decompressobj().decompress(b[m.start():])
        except zlib.error:
            continue
        if len(d) > 16:
            yield m.start(), d


# ---------------------------------------------------------------- Ableton groove
AMT = ("QuantizationAmount", "TimingAmount", "RandomAmount", "VelocityAmount")


def agr_binary(b):
    """magic ab1e5678: a type schema (UTF-16 names) then the data. The groove's notes are fixed 25-byte records after
    the LAST 'MidiNoteEvent' name: f64 time, f64 duration, f32 velocity, f32 off-velocity, u8 enabled. After the
    notes come four f32: quantise, timing, random, velocity amounts (in the schema's order), then 00 00 00 00 01."""
    k = b.rfind(b"MidiNoteEvent")
    if k < 0:
        return None
    i, notes = k + 13, []
    while i + 25 <= len(b):
        t, d, v, o, en = struct.unpack_from("<ddffB", b, i)
        if not (0 <= v <= 127 and en in (0, 1) and -1000 < t < 5000 and 0 < d < 1000):
            break
        notes.append([round(t, 5), round(d, 5), round(v, 3)])
        i += 25
    amounts = {}
    m = re.search(rb"(.{16})\x00\x00\x00\x00\x01", b[i:i + 120], re.S)
    if m:
        vals = struct.unpack("<4f", m.group(1))
        if all(0 <= x <= 100 for x in vals):
            amounts = {n[:-6].lower(): round(x, 3) for n, x in zip(AMT, vals)}
    return amounts, notes


def agr(path):
    """Ableton groove: two layouts in one library, plain XML (Live 10 era) and the binary 'ab1e5678' form"""
    raw = open(path, "rb").read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    r = base(path, "agr")
    if raw[:4] == b"\xab\x1eVx":
        got = agr_binary(raw)
        if not got:
            return None
        r["amounts"], r["steps"] = got
    else:
        root = ET.fromstring(raw)
        g = root if root.tag == "Groove" else next((e for e in root.iter("Groove")), None)
        if g is None:
            return None
        r["amounts"] = {n[:-6].lower(): num(g.find(n).get("Value")) for n in AMT if g.find(n) is not None}
        r["steps"] = [[num(e.get("Time")), num(e.get("Duration")), num(e.get("Velocity"))] for e in g.iter("MidiNoteEvent")]
    st = r["steps"]
    if not st:
        return None
    ts = [x[0] for x in st]
    r["notes"] = len(st)
    r["length_beats"] = round(max(ts) + 0.25, 3)
    r["velocity_mean"] = round(sum(x[2] for x in st) / len(st), 2)
    r["velocity_range"] = [round(min(x[2] for x in st), 2), round(max(x[2] for x in st), 2)]
    # timing offset of each note from its nearest 1/16 grid slot (beats): the swing / push / drag the groove carries
    r["offgrid"] = [round(t - round(t * 4) / 4, 4) for t in ts]
    return r


# ---------------------------------------------------------------- Bitwig preset
def bw_meta(b, i):
    """the 'meta' record list of a type-2 file: repeated u32 1, u32 key length, key, tag byte, value;
    tag 0x08 string (u32 length, bytes), 0x03 int32, 0x19 string list (u32 count, then length + bytes each)"""
    meta = {}
    while i + 8 <= len(b) and b[i:i + 4] == b"\x00\x00\x00\x01":
        kl = struct.unpack_from(">I", b, i + 4)[0]
        if not 1 <= kl <= 64:
            break
        key, tag, i = b[i + 8:i + 8 + kl].decode("latin-1"), b[i + 8 + kl], i + 9 + kl
        if tag == 0x08:
            n = struct.unpack_from(">I", b, i)[0]
            meta[key], i = b[i + 4:i + 4 + n].decode("utf-8", "replace"), i + 4 + n
        elif tag == 0x03:
            meta[key], i = struct.unpack_from(">i", b, i)[0], i + 4
        elif tag == 0x19:
            cnt, i, items = struct.unpack_from(">I", b, i)[0], i + 4, []
            for _ in range(cnt):
                n = struct.unpack_from(">I", b, i)[0]
                items.append(b[i + 4:i + 4 + n].decode("utf-8", "replace"))
                i += 4 + n
            meta[key] = items
        else:
            break
    return meta, i


BW_REAL = re.compile(rb"\x08\x00\x00\x00([\x01-\x40])([A-Za-z][A-Za-z0-9_ ./+-]*)\x00\x00\x01\x36\x07(.{8})", re.S)
BW_INT = re.compile(rb"\x08\x00\x00\x00([\x01-\x40])([A-Za-z][A-Za-z0-9_ ./+-]*)\x00\x00\x01/\x05(.)", re.S)


def bwpreset(path):
    """'BtWg' + 4 digits version + 4 digits kind + ... Kind 0002 (the 154 factory device defaults): metadata records
    (device, category, creator, tags ...) then an object tree from which every named real / integer parameter is read
    (name -> value). Kind 0004 (user and factory content presets): the body is high entropy (about 7.9 bits per
    byte, no zlib / lzma / bzip2 stream at any offset), so only the header, the file name and the folder (device)
    are kept and the row says why the parameters are missing."""
    b = open(path, "rb").read()
    if b[:4] != b"BtWg":
        return None
    r = base(path, "bwpreset")
    head = b[:42].decode("latin-1")
    r["format"] = {"version": head[4:8], "kind": head[8:12], "head_a": head[12:16], "head_b": head[16:24]}
    r["bytes"] = len(b)
    if head[8:12] == "0002":
        k = b.find(b"meta")
        meta, i = bw_meta(b, k + 4) if k > 0 else ({}, 0)
        r["meta"] = {m: v for m, v in meta.items() if v not in ("", [], None)}
        body = b[i:]
        params = {}
        for m in BW_REAL.finditer(body):
            v = struct.unpack(">d", m.group(3))[0]
            if v == v and abs(v) < 1e12:
                params.setdefault(m.group(2).decode(), round(v, 6))
        for m in BW_INT.finditer(body):
            params.setdefault(m.group(2).decode(), m.group(3)[0])
        r["params"] = params
        if r["meta"].get("device_name"):
            r["name"] = r["meta"]["device_name"]
        if not params:
            r["note"] = "no named parameters found"
    else:
        r["note"] = "body not decodable: high entropy, no known compression stream"
    return r


# ---------------------------------------------------------------- Guitar Rig / NI soundinfo
def guitarrig(path):
    """NI .ngrr (Guitar Rig rack preset) and .nkp (Kontakt effect / filter preset) embed an XML <soundinfo> block
    (name, vendor, author, banks, components, attribute tags); the rest is NI's private binary chain and is not read."""
    b = open(path, "rb").read()
    i = b.find(b"<?xml")
    kind = "guitarrig" if path.lower().endswith(".ngrr") else "nkp"
    root = None
    if i >= 0:
        m = re.compile(rb"</[A-Za-z0-9_-]*database-info>").search(b, i)
        j = m.end() if m else b.find(b"</soundinfo>", i) + 12
        try:
            root = ET.fromstring(b[i:j].decode("utf-8", "replace").encode("utf-8"))
        except ET.ParseError:
            root = None
    si = None if root is None else (root if root.tag == "soundinfo" else root.find("soundinfo"))
    if si is None:  # Kontakt effect presets without a soundinfo block: only the file name and folder tree carry meaning
        r = base(path, kind)
        r["bytes"] = len(b)
        r["folders"] = path.split(os.sep)[-4:-1]
        r["note"] = "no soundinfo block; binary body not read"
        return r
    props = {e.tag: (e.text or "").strip() for e in si.find("properties") or []}
    r = base(path, kind, props.get("name"))
    r["vendor"], r["author"] = props.get("vendor", ""), props.get("author", "")
    r["banks"] = [e.text for e in si.iter("bank") if e.text]
    r["components"] = [e.text for e in si.iter("component") if e.text]
    r["tags"] = [v.text for a in si.iter("attribute") for v in a.findall("value") if v.text]
    r["tag_paths"] = [(u.text or "").replace("\t", "/").replace("\n", "/") for u in si.iter("user-set")]
    r["bytes"] = len(b)
    return r


# ---------------------------------------------------------------- Vital wavetables
try:
    import numpy as np
except ImportError:
    np = None


def harmonic_shape(x):
    n = len(x)
    if np is not None:
        h = np.abs(np.fft.rfft(np.asarray(x, dtype=np.float64)))[1:65].tolist()
    else:
        import cmath, math
        h = [abs(sum(x[i] * cmath.exp(-2j * math.pi * k * i / n) for i in range(n))) for k in range(1, 65)]
    m = max(h) or 1.0
    h = [v / m for v in h]
    odd, even = sum(h[0::2]), sum(h[1::2])
    return {"harm": [round(v, 4) for v in h], "odd_even": round(min(1000.0, odd / max(1e-9, even)), 3),
            "bright": round(sum((k + 1) * v for k, v in enumerate(h)) / max(1e-9, sum(h)), 2)}


def vitalwt(path, seen=None):
    """list of rows, one per distinct wavetable of the preset"""
    j = json.load(open(path, encoding="utf-8", errors="replace"))
    rows = []
    for wt in j.get("settings", {}).get("wavetables", []):
        if not isinstance(wt, dict):
            continue
        comps = [c for g in wt.get("groups", []) for c in g.get("components", [])]
        types = collections.Counter(c.get("type", "?") for c in comps)
        waves, digest = [], hashlib.sha1()
        for c in comps:
            if c.get("type") != "Wave Source":
                continue
            for k in c.get("keyframes", []):
                try:
                    raw = base64.b64decode(k["wave_data"])
                    digest.update(raw)
                    x = struct.unpack("<%df" % (len(raw) // 4), raw[:len(raw) // 4 * 4])
                except (KeyError, ValueError, struct.error):
                    continue
                if len(x) >= 128:
                    waves.append((k.get("position", 0), x))
        if not waves:
            if seen is not None:
                seen["no wave source"] += 1
            continue
        key = digest.hexdigest()
        if seen is not None and key in seen:
            seen["duplicate"] += 1
            continue
        if seen is not None:
            seen[key] = 1
        pick = waves if len(waves) <= 16 else [waves[i * (len(waves) - 1) // 15] for i in range(16)]
        r = base(path, "vitalwt", wt.get("name") or os.path.splitext(os.path.basename(path))[0])
        r.update({"preset": j.get("preset_name"), "author": wt.get("author"), "components": dict(types), "frames": len(waves),
                  "length": len(waves[0][1]), "shapes": [dict(harmonic_shape(x), frame=int(pos)) for pos, x in pick]})
        rows.append(r)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", nargs="+", required=True)
    ap.add_argument("--out", required=True, help="directory; one <kind>.jsonl per kind")
    ap.add_argument("--only", choices=sorted(EXT))
    ap.add_argument("--budget", type=float, default=1200, help="seconds")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    kinds = [a.only] if a.only else sorted(EXT)
    t0, n, bad = time.time(), collections.Counter(), collections.Counter()
    seen = collections.Counter()
    fos = {k: open(os.path.join(out, k + ".jsonl"), "w") for k in kinds}
    ext_kind = {e: k for k in kinds for e in EXT[k]}
    stop = False
    for root in a.root:
        for d, subs, fs in os.walk(os.path.expanduser(root)):
            subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
            if time.time() - t0 > a.budget:
                print("budget hit", file=sys.stderr)
                stop = True
                break
            for f in sorted(fs):
                k = ext_kind.get(os.path.splitext(f)[1].lower())
                if not k or f.startswith("._"):
                    continue
                p = os.path.join(d, f)
                try:
                    r = vitalwt(p, seen) if k == "vitalwt" else globals()[k](p)
                except Exception as e:  # one odd file must not stop a long walk
                    bad[k + ": " + type(e).__name__] += 1
                    continue
                for x in (r if isinstance(r, list) else [r] if r else []):
                    fos[k].write(json.dumps(x, separators=(",", ":")) + "\n")
                    n[k] += 1
                if not r and k != "vitalwt":
                    bad[k + ": no data"] += 1
        if stop:
            break
    for fo in fos.values():
        fo.close()
    print("rows:", dict(n), "in %.0f s" % (time.time() - t0))
    print("unread:", dict(bad), "vital skipped:", {k: v for k, v in seen.items() if isinstance(k, str) and len(k) < 20})
    print("wrote", out)


if __name__ == "__main__":
    main()
