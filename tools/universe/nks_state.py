#!/usr/bin/env python3
"""NKS (.nksf) reader and splicer: Arturia V-collection, Pigments, Massive X, Hive/Repro/Diva NKS, SubLab and more.

An .nksf is a RIFF file: NISI (msgpack metadata: author, bank, types, modes = the sound's tags), NICA (controller map), PLID (plug-in id)
and PCHK, the plug-in's own state chunk. Every vendor wraps that chunk a little differently from what the host saves, so a plug-in is
probed once: for two different presets, try each candidate splice into the template state (plugin_states.py) and keep the first one whose
renders differ from each other and from the default. The winner is written to nks_variants.json and preset_state.py uses it.

    nks_state.py --probe "Massive X" --presets "/Library/Application Support/Native Instruments/Massive X/Presets"
"""
import argparse, base64, json, os, re, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

VARIANTS_PATH = os.path.join(HERE, "nks_variants.json")


def chunks(d):
    o = 12
    while o + 8 <= len(d):
        t, n = d[o:o + 4], struct.unpack("<I", d[o + 4:o + 8])[0]
        yield t, d[o + 8:o + 8 + n]
        o += 8 + n + (n & 1)


def mp(b, o=0):
    """minimal msgpack reader (maps, arrays, str, bin, ints, floats, bool, nil): returns (value, next offset)"""
    c = b[o]
    o += 1
    if c <= 0x7f:
        return c, o
    if c >= 0xe0:
        return c - 256, o
    if 0xa0 <= c <= 0xbf:
        n = c & 0x1f
        return b[o:o + n].decode("utf8", "replace"), o + n
    if 0x90 <= c <= 0x9f or c in (0xdc, 0xdd):
        if c in (0xdc, 0xdd):
            w = 2 if c == 0xdc else 4
            n, o = int.from_bytes(b[o:o + w], "big"), o + w
        else:
            n = c & 0xf
        out = []
        for _ in range(n):
            v, o = mp(b, o)
            out.append(v)
        return out, o
    if 0x80 <= c <= 0x8f or c in (0xde, 0xdf):
        if c in (0xde, 0xdf):
            w = 2 if c == 0xde else 4
            n, o = int.from_bytes(b[o:o + w], "big"), o + w
        else:
            n = c & 0xf
        out = {}
        for _ in range(n):
            k, o = mp(b, o)
            v, o = mp(b, o)
            out[k if isinstance(k, str) else str(k)] = v
        return out, o
    fixed = {0xc0: None, 0xc2: False, 0xc3: True}
    if c in fixed:
        return fixed[c], o
    sizes = {0xcc: (1, False), 0xcd: (2, False), 0xce: (4, False), 0xcf: (8, False), 0xd0: (1, True), 0xd1: (2, True), 0xd2: (4, True), 0xd3: (8, True)}
    if c in sizes:
        w, sg = sizes[c]
        return int.from_bytes(b[o:o + w], "big", signed=sg), o + w
    if c == 0xca:
        return struct.unpack(">f", b[o:o + 4])[0], o + 4
    if c == 0xcb:
        return struct.unpack(">d", b[o:o + 8])[0], o + 8
    if c in (0xd9, 0xda, 0xdb, 0xc4, 0xc5, 0xc6):
        w = {0xd9: 1, 0xda: 2, 0xdb: 4, 0xc4: 1, 0xc5: 2, 0xc6: 4}[c]
        n, o = int.from_bytes(b[o:o + w], "big"), o + w
        v = b[o:o + n]
        return (v.decode("utf8", "replace") if c in (0xd9, 0xda, 0xdb) else v.hex()), o + n
    raise ValueError("msgpack type %#x" % c)


def read_nksf(path):
    """-> (meta dict, plugin id dict, PCHK bytes); path may also be the file's bytes"""
    d = path if isinstance(path, bytes) else open(path, "rb").read()
    meta, plid, pchk = {}, {}, b""
    for t, body in chunks(d):
        try:
            if t == b"NISI":
                meta = mp(body, 4)[0]
            elif t == b"PLID":
                plid = mp(body, 4)[0]
        except (ValueError, IndexError):
            pass
        if t == b"PCHK":
            pchk = body
    return meta, plid, pchk


def tags_of(meta):
    """NISI -> {Author, Bank, Type, Mode...}: types/modes are lists of [major, minor]"""
    out = {}
    for k in ("author", "bankchain", "deviceType", "name", "vendor"):
        if meta.get(k):
            out[k] = meta[k]
    for k in ("types", "modes"):
        if meta.get(k):
            out[k] = [" / ".join(x) if isinstance(x, list) else str(x) for x in meta[k]]
    return out


# ------------------------------------------------------------------ candidate splices
def zlib_anchor(b):
    m = re.search(rb"\x78[\x9c\x01\xda\x5e]", b)
    return m.start() if m else -1


def v_raw(tmpl, pchk):
    return pchk


def v_skip12(tmpl, pchk):
    return pchk[12:]


def v_u64len_skip12(tmpl, pchk):
    return struct.pack("<Q", len(pchk) - 12) + pchk[12:]


def v_zlib_block(tmpl, pchk):
    """keep the template's prefix up to its size fields, take the preset's zlib block, keep the template tail"""
    a, b = zlib_anchor(tmpl), zlib_anchor(pchk)
    if a < 8 or b < 8:
        return None
    first = struct.unpack("<I", tmpl[a - 8:a - 4])[0]
    tail = tmpl[a - 8 + 8 + first:]
    new = tmpl[:a - 8] + pchk[b - 8:]
    new += tail
    if len(tmpl) >= 8:
        new = struct.pack("<Q", len(new) - 16) + new[8:]
    return new


def v_juce(tmpl, pchk):
    """template is a JUCE VstW chunk: the preset's PCHK carries the same VstW chunk after a 12-byte NI header"""
    i = pchk.find(b"VstW")
    if i < 0:
        return None
    body = pchk[i:]
    return struct.pack("<Q", len(body) - 0) + body


VARIANTS = {"raw": v_raw, "skip12": v_skip12, "u64len_skip12": v_u64len_skip12, "zlib_block": v_zlib_block, "juce": v_juce}


def load_variants():
    try:
        return json.load(open(VARIANTS_PATH))
    except (OSError, ValueError):
        return {}


def splice_nks(plugin, tmpl_raw, pchk):
    v = load_variants().get(plugin.lower())
    if not v:
        return None
    return VARIANTS[v](tmpl_raw, pchk)


# ------------------------------------------------------------------ probe
def probe(plugin, presets, states, n_try=2):
    import render_pass as rp
    rp.LEAD = 2.0
    rp.NOTES = [(48, 100, "C3")]
    tm = [json.loads(l) for l in open(os.path.expanduser(states))]
    tm = [r for r in tm if r["name"].lower() == plugin.lower() and r.get("state_b64") and r["format"] == "vst3"][0]
    raw0 = base64.b64decode(tm["state_b64"] + "=" * (-len(tm["state_b64"]) % 4))
    files = []
    for r, _, fs in os.walk(presets):
        files += [os.path.join(r, f) for f in sorted(fs) if f.endswith(".nksf") and not f.startswith("._")]
    pick = [files[len(files) // 7], files[len(files) // 2], files[-1 - len(files) // 5]][:n_try + 1]
    t = {"instrument": True, "format": "vst3", "id": tm["id"], "name": tm["name"], "vendor": ""}
    binp = rp.infinite_bin()

    def feat(state):
        with tempfile.TemporaryDirectory() as tmp:
            inf = os.path.join(tmp, "p.inf")
            open(inf, "w").write(rp.patch_text(t, "", []).replace("  s plugin_name", "  s plugin_state %s\n  s plugin_name" % base64.b64encode(state).decode(), 1))
            ep, mpth = rp.write_notes(tmp)
            wav = os.path.join(tmp, "o.wav")
            subprocess.run([binp, "--audio-summary", inf, os.path.join(tmp, "s.json"), "--duration", "6", "--sample-rate", "48000", "--wav", wav,
                            "--lenient", "--notes", ep, "--note-map", mpth], capture_output=True, timeout=120)
            if not os.path.exists(wav):
                return None
            rows = rp.measure_windows(wav, tmp, ["C3"])
            f = rows[0][1] or {}
            return (f.get("rms"), f.get("centroid"), f.get("attack_ms"), tuple((f.get("spectrum") or f.get("bands") or [])[:8]))
    base = feat(raw0)
    chunks_ = [read_nksf(p)[2] for p in pick]
    for name, fn in VARIANTS.items():
        try:
            outs = []
            for pc in chunks_:
                st = fn(raw0, pc)
                outs.append(feat(st) if st else None)
        except Exception as e:  # noqa: BLE001
            print(name, "error", e)
            continue
        ok = all(o is not None for o in outs) and len(set(outs)) == len(outs) and all(o != base for o in outs)
        print(name, "OK" if ok else "no", outs[0][:3] if outs[0] else None)
        if ok:
            v = load_variants()
            v[plugin.lower()] = name
            json.dump(v, open(VARIANTS_PATH, "w"), indent=1, sort_keys=True)
            return name
    print("no variant worked for", plugin)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe")
    ap.add_argument("--presets")
    ap.add_argument("--states", default="~/Infinite/training-data/audio/plugin_states_key.jsonl")
    a = ap.parse_args()
    if a.probe:
        probe(a.probe, a.presets, a.states)


if __name__ == "__main__":
    main()
