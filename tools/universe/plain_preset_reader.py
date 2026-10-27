#!/usr/bin/env python3
"""Plugin presets in plain formats -> knob rows in the preset_reader schema, plus Ableton MIDI clips for the inbox.
    plain_preset_reader.py [ROOT ...] [--out presets.jsonl] [--clips DIR] [--budget 1200]
Pure Python (runs on a laptop without numpy). Read only. Formats, by first bytes:
  .vital            JSON: every numeric `settings` value, plus author, style and macro names; wavetable names only
  .serumpreset      'XferJson\\0' + u32 length + JSON header (author, description, tags); the knob body after it is
                    zstd-compressed CBOR and is not read
  .phaseplant .multipass .snapheap   Kilohearts: u32 header words then a JSON object (metadata); the rest is binary
  .nksf             NI Komplete Kontrol RIFF 'NIKS': the NISI chunk is msgpack metadata (author, bank chain, types,
                    characters, device type, vendor); PLID names the plugin; PCHK is the plugin's private state
  .vpreset .talmod .pjunoxl .noisemakerpreset .bassline .adpreset .preset .aupreset   XML: numeric attributes of the
                    root and its children (Valhalla, TAL, Audio Damage, oeksound ...); .aupreset plist numbers
  .ffp .xlfo        FabFilter / Xfer 'FT3i': u32 version, u32 byte length, then little-endian float32 values (unnamed)
  .alc              Ableton MIDI clip, gzip XML: written as .mid into --clips with als_reader
Licence: "preset library, local only"."""
import argparse, collections, gzip, json, os, re, struct, sys, time
import xml.etree.ElementTree as ET

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
XML = (".vpreset", ".talmod", ".pjunoxl", ".noisemakerpreset", ".bassline", ".adpreset", ".preset", ".aupreset")
LIC = "preset library, local only"


def num(v):
    try:
        f = float(v)
        return round(f, 5) if f == f and abs(f) < 1e9 else None
    except (TypeError, ValueError):
        return None


def row(path, engine, mods, tags=None, name=None):
    rel = path.split(os.sep)
    return {"src": path, "engine": engine, "library": engine, "category": rel[-2], "name": name or os.path.splitext(rel[-1])[0],
            "tags": tags or {}, "modules": mods, "license": LIC}


def vital(path):
    j = json.load(open(path, encoding="utf-8", errors="replace"))
    st = j.get("settings", {})
    knobs = {k: num(v) for k, v in st.items() if not isinstance(v, (list, dict)) and num(v) is not None}
    tags = {k: j[k] for k in ("author", "preset_style", "comments") if j.get(k)}
    tags["macros"] = [j.get("macro%d" % i, "") for i in range(1, 5)]
    tags["wavetables"] = [w.get("name", "") for w in st.get("wavetables", []) if isinstance(w, dict)]
    return row(path, "vital", {"main": knobs}, tags, j.get("preset_name"))


def serum(path):
    b = open(path, "rb").read(65536)
    if not b.startswith(b"XferJson"):
        return None
    j = first_json(b[9:])
    tags = {k: j[k] for k in ("presetAuthor", "presetDescription", "tags") if j.get(k)}
    return row(path, "serum2", {}, tags, j.get("presetName"))


def first_json(b):
    i = b.find(b"{")
    if i < 0:
        return {}
    depth = 0
    for k in range(i, min(len(b), i + 200000)):
        c = b[k:k + 1]
        depth += c == b"{"
        depth -= c == b"}"
        if depth == 0:
            try:
                return json.loads(b[i:k + 1].decode("utf-8", "replace"))
            except ValueError:
                return {}
    return {}


def kilohearts(path):
    j = first_json(open(path, "rb").read(262144))
    eng = {".phaseplant": "phase plant", ".multipass": "multipass", ".snapheap": "snap heap"}[os.path.splitext(path)[1].lower()]
    tags = {k: v for k, v in j.items() if isinstance(v, (str, list)) and v}
    return row(path, eng, {}, tags)


def msgpack(b, i=0):
    """minimal msgpack decoder: returns (value, next index)"""
    t = b[i]
    if t <= 0x7F:
        return t, i + 1
    if 0x80 <= t <= 0x8F or t in (0xDE, 0xDF):
        n, i = (t & 0x0F, i + 1) if t <= 0x8F else ((struct.unpack(">H", b[i + 1:i + 3])[0], i + 3) if t == 0xDE else (struct.unpack(">I", b[i + 1:i + 5])[0], i + 5))
        d = {}
        for _ in range(n):
            k, i = msgpack(b, i)
            v, i = msgpack(b, i)
            d[str(k)] = v
        return d, i
    if 0x90 <= t <= 0x9F or t in (0xDC, 0xDD):
        n, i = (t & 0x0F, i + 1) if t <= 0x9F else ((struct.unpack(">H", b[i + 1:i + 3])[0], i + 3) if t == 0xDC else (struct.unpack(">I", b[i + 1:i + 5])[0], i + 5))
        out = []
        for _ in range(n):
            v, i = msgpack(b, i)
            out.append(v)
        return out, i
    if 0xA0 <= t <= 0xBF or t in (0xD9, 0xDA, 0xDB):
        n, i = (t & 0x1F, i + 1) if t <= 0xBF else {0xD9: (b[i + 1], i + 2), 0xDA: (struct.unpack(">H", b[i + 1:i + 3])[0], i + 3),
                                                     0xDB: (struct.unpack(">I", b[i + 1:i + 5])[0], i + 5)}[t]
        return b[i:i + n].decode("utf-8", "replace"), i + n
    if t == 0xC0:
        return None, i + 1
    if t in (0xC2, 0xC3):
        return t == 0xC3, i + 1
    if t >= 0xE0:
        return t - 256, i + 1
    fmt = {0xCA: (">f", 4), 0xCB: (">d", 8), 0xCC: (">B", 1), 0xCD: (">H", 2), 0xCE: (">I", 4), 0xCF: (">Q", 8),
           0xD0: (">b", 1), 0xD1: (">h", 2), 0xD2: (">i", 4), 0xD3: (">q", 8)}.get(t)
    if fmt:
        return struct.unpack(fmt[0], b[i + 1:i + 1 + fmt[1]])[0], i + 1 + fmt[1]
    if t in (0xC4, 0xC5, 0xC6):
        w = {0xC4: 1, 0xC5: 2, 0xC6: 4}[t]
        n = int.from_bytes(b[i + 1:i + 1 + w], "big")
        return None, i + 1 + w + n
    raise ValueError("msgpack type %x" % t)


def nksf(path):
    b = open(path, "rb").read(65536)
    if b[:4] != b"RIFF" or b[8:12] != b"NIKS":
        return None
    meta, plugin, i = {}, "", 12
    while i + 8 <= len(b):
        cid, size = b[i:i + 4], struct.unpack("<I", b[i + 4:i + 8])[0]
        body = b[i + 8:i + 8 + size]
        if cid == b"NISI":
            meta = msgpack(body, 4)[0]
        elif cid == b"PLID":
            p = msgpack(body, 4)[0]
            plugin = str(p.get("VST.magic", p)) if isinstance(p, dict) else str(p)
        elif cid == b"PCHK":
            break
        i += 8 + size + (size & 1)
    bank = meta.get("bankchain") or []
    eng = "nks " + (bank[0] if bank and bank[0] else meta.get("vendor", "?"))
    tags = {k: meta[k] for k in ("author", "bankchain", "types", "characters", "deviceType", "vendor", "comment") if meta.get(k)}
    tags["plugin_id"] = plugin
    return row(path, eng.lower(), {}, tags, meta.get("name"))


def xml_preset(path):
    root = ET.parse(path).getroot()
    mods = {}
    if path.lower().endswith(".aupreset"):  # plist: <key>name</key><real|integer>v</...>
        it = list(root.iter())
        vals = {it[k].text: num(it[k + 1].text) for k in range(len(it) - 1) if it[k].tag == "key" and it[k + 1].tag in ("real", "integer")}
        mods["main"] = {k: v for k, v in vals.items() if v is not None}
        eng = "au " + (os.path.basename(os.path.dirname(os.path.dirname(path))) or "?")
    else:
        for el in [root] + list(root)[:200]:
            vals = {k: num(v) for k, v in el.attrib.items()}
            vals = {k: v for k, v in vals.items() if v is not None}
            if vals:
                mods.setdefault(el.tag, {}).update(vals)
        eng = root.tag
    mods = {k: v for k, v in mods.items() if v}
    name = root.attrib.get("presetName") or root.attrib.get("name")
    return row(path, eng.lower(), mods, {}, name) if mods else None


def ft3i(path):
    b = open(path, "rb").read()
    if b[:4] != b"FT3i" or len(b) < 12:
        return None
    n = min((len(b) - 12) // 4, 2048)
    vals = struct.unpack("<%df" % n, b[12:12 + 4 * n])
    eng = "fabfilter " + os.path.basename(os.path.dirname(os.path.dirname(path))) if path.lower().endswith(".ffp") else "xfer lfo"
    return row(path, eng.lower(), {"main": {"p%d" % k: round(v, 5) for k, v in enumerate(vals) if v == v and abs(v) < 1e9}})


READERS = {".vital": vital, ".serumpreset": serum, ".phaseplant": kilohearts, ".multipass": kilohearts, ".snapheap": kilohearts,
           ".nksf": nksf, ".ffp": ft3i, ".xlfo": ft3i, **{e: xml_preset for e in XML}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--clips", help="write Ableton .alc clips here as .mid (needs als_reader.py next to this file)")
    ap.add_argument("--budget", type=float, default=1200, help="seconds")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if a.clips:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import als_reader
        os.makedirs(a.clips, exist_ok=True)
    t0, n, bad = time.time(), collections.Counter(), collections.Counter()
    with open(out, "w") as fo:
        for root in a.roots:
            for d, subs, fs in os.walk(root):
                subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
                if time.time() - t0 > a.budget:
                    print("budget hit", file=sys.stderr)
                    break
                for f in sorted(fs):
                    if f.startswith("._"):
                        continue
                    e = os.path.splitext(f)[1].lower()
                    p = os.path.join(d, f)
                    if e == ".alc" and a.clips:
                        try:
                            body = gzip.open(p).read().decode("utf-8", "replace")
                            notes = [x for t0_, span, ns in als_reader.clips(body) for x in ns]
                            bpm = re.search(r'<Tempo>.*?<Manual Value="([\d.]+)"', body, re.S)
                            if notes:
                                rel = re.sub(r"[^A-Za-z0-9._-]+", "_", os.path.relpath(p, root))[-150:]
                                als_reader.write_mid(os.path.join(a.clips, rel[:-4] + ".mid"), [(os.path.splitext(f)[0], notes)],
                                                     float(bpm[1]) if bpm else 120.0, 4, 4)
                                n["alc clips"] += 1
                        except (OSError, EOFError, ValueError):
                            bad[e] += 1
                        continue
                    fn = READERS.get(e)
                    if not fn:
                        continue
                    try:
                        r = fn(p)
                    except Exception:  # one odd file must not stop a 60,000-file walk
                        r = None
                    if r:
                        fo.write(json.dumps(r, separators=(",", ":")) + "\n")
                        n[r["engine"].split()[0] if r["engine"].startswith(("nks", "au", "fabfilter")) else r["engine"]] += 1
                    else:
                        bad[e] += 1
    print("rows:", dict(n.most_common()), "in %.0f s" % (time.time() - t0))
    print("unread:", dict(bad))
    print("wrote", out)


if __name__ == "__main__":
    main()
