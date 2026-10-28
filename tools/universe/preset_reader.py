#!/usr/bin/env python3
"""Sound-designer presets -> knob rows: what real patches set every knob to, tagged with genre, mood and type.
    preset_reader.py [ROOT ...] [--out ~/Infinite/training-data/presets/presets.jsonl]
Reads (read only):
  Spectrasonics  Omnisphere / Keyscape / Trilian patches and multis: loose .prt_omn / .mlt_omn files and the
                 .db libraries, which are an XML file table (<FileSystem> name / offset / size) followed by the files.
                 Each patch is XML; numbers are IEEE floats written as 8 hex digits, 0..1 normalised. Its ENTRYDESCR
                 carries the library's own tags: Genre=, Mood=, Type=, Complexity=, Description=.
  Ableton        .adv device presets and .adg racks: gzip XML; every <Manual Value=...> of every device parameter.
  u-he           Diva / Hive / Zebra / Repro .h2p: text, a /*@Meta*/ block (Author, Description, Usage, Categories)
                 then `#cm=MODULE` sections of key=value knobs.
  Alchemy        .acp (Logic's Alchemy synth): text, `<section>` lines then `Key = value`; Attribs = numeric library
                 attribute ids (genre / timbre / articulation, names not stored) kept as tags; folders give the category.
  Logic          .pst plug-in settings: a big-endian float array after an 'EMAGPPST' header; the plug-in is the
                 folder name and the knobs are indexed (Logic does not store their names).
One row per patch: src, engine, library, category (folder inside the library), name, tags {genre, mood, type, ...},
modules {MODULE#i: {param: value}} with every numeric attribute. Licence: "preset library, local only"."""
import argparse, collections, glob, gzip, html, json, os, re, struct, sys, time

SKIP = {".Spotlight-V100", ".Trashes", ".fseventsd", ".TemporaryItems", ".DocumentRevisions-V100"}
SPECTRA = (".prt_omn", ".mlt_omn", ".prt_key", ".prt_trl", ".mlt_trl", ".mlt_key")
HEX = re.compile(r"^[0-9a-f]{1,8}$")
TAG = re.compile(r"<([A-Za-z_][A-Za-z0-9_]*)((?:\s+[A-Za-z_][A-Za-z0-9_]*=\"[^\"]*\")*)\s*/?>")
ATTR = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=\"([^\"]*)\"")
# modules whose knobs say how the sound is made; the rest (UI state, slice sequences, mod matrix rows) are skipped
KEEP = {"OSC", "FILTER", "AENVPARAMS", "FENVPARAMS", "LFO", "MODENVPARAMS", "DIST", "WAVESHAPER", "ARP", "VOX", "HARM",
        "GRAN", "UNISON", "EQ2", "EQ12", "EFFMODULE", "SYNTHENG", "VOICE", "AMP", "PITCH", "PART"}


def hexf(v):
    """'3f800000' -> 1.0; '0' -> 0.0; anything else None"""
    if not HEX.match(v):
        return None
    f = struct.unpack(">f", bytes.fromhex(v.rjust(8, "0")))[0]
    return round(f, 5) if f == f and abs(f) < 1e9 else None


def tags_of(descr):
    out = collections.defaultdict(list)
    for kv in descr.split(";"):
        if "=" in kv:
            k, v = kv.split("=", 1)
            k = k.strip().lower().replace(" ", "_")
            if k in ("genre", "mood", "type", "complexity", "description", "osc_type", "author", "category", "timbre", "articulation"):
                out[k].append(v.strip())
    return {k: (v if len(v) > 1 or k in ("genre", "mood", "type") else v[0]) for k, v in out.items()}


def spectra_patch(text, src, engine, library, category, name):
    mods, n = {}, collections.Counter()
    tags = {}
    for m in TAG.finditer(text):
        t, body = m[1], m[2]
        if t == "ENTRYDESCR":
            a = dict(ATTR.findall(body))
            tags = tags_of(html.unescape(a.get("ATTRIB_VALUE_DATA", "")))
            name = a.get("name", name)
            library = a.get("library", library)
            continue
        if t not in KEEP:
            continue
        a = ATTR.findall(body)
        vals = {}
        for k, v in a:
            f = hexf(v)
            if f is not None:
                vals[k] = f
            elif t == "EFFMODULE" and k == "Type":
                vals[k] = v
        if t == "EFFMODULE" and vals.get("Type") in (None, "No Effect"):
            continue
        if vals:
            mods["%s#%d" % (t, n[t])] = vals
            n[t] += 1
    return {"src": src, "engine": engine, "library": library, "category": category, "name": name, "tags": tags,
            "modules": mods, "license": "preset library, local only"}


def spectra_db(path, engine):
    b = open(path, "rb").read()
    i = b.find(b"</FileSystem>")
    if not b.startswith(b"<FileSystem>") or i < 0:
        return
    base = i + len(b"</FileSystem>")
    stack = []
    for m in re.finditer(r"<DIR name=\"([^\"]*)\">|</DIR>|<FILE name=\"([^\"]*)\" offset=\"(\d+)\" size=\"(\d+)\"/>",
                         b[:i].decode("latin-1")):
        if m[0].startswith("<DIR"):
            stack.append(html.unescape(m[1]))
        elif m[0] == "</DIR>":
            stack and stack.pop()
        elif m[2].lower().endswith(SPECTRA):
            o, s = int(m[3]), int(m[4])
            text = b[base + o:base + o + s].decode("latin-1")
            name = html.unescape(os.path.splitext(m[2])[0])
            yield spectra_patch(text, "%s#%s/%s" % (path, "/".join(stack), name), engine,
                                os.path.splitext(os.path.basename(path))[0], "/".join(stack), name)


def ableton(path):
    try:
        x = gzip.open(path).read().decode("utf-8", "replace")
    except OSError:
        return None
    mods, n = {}, collections.Counter()
    # each device is an element whose children hold <Name>... <Manual Value="..."/>; walk parameter by parameter
    # devices nest (a drum rack holds Simplers), so scan opening tags and cut each body at its own closing tag
    for dev in re.finditer(r"<(Operator|InstrumentVector|UltraAnalog|OriginalSimpler|MultiSampler|Drift|Meld|Tension|Collision|"
                           r"AutoFilter|Reverb|Delay|Echo|Chorus2|Saturator|Compressor2|Eq8|Redux2|Phaser|PhaserNew|"
                           r"Flanger|AutoPan|Corpus|Roar|GlueCompressor|DrumBuss|Erosion|Vocoder|Overdrive|Pedal|"
                           r"FilterDelay|GrainDelay|Hybrid|Spectral[A-Za-z]*|Shifter|ChannelEq|Amp|Cabinet|"
                           r"DrumGroupDevice|InstrumentGroupDevice|AudioEffectGroupDevice)\b[^>/]*>", x):
        t = dev[1]
        end = x.find("</%s>" % t, dev.end())
        body = x[dev.end():end if end > 0 else len(x)]
        if t.endswith("GroupDevice"):  # the rack's own macros only; its chains are scanned as their own devices
            body = re.split(r"<(?:Branches|DrumBranches|ReturnBranches)>", body)[0]
        vals = {}
        # Live 9-11 put <ArrangerAutomation> between <LomId/> and <Manual/>; Live 12 puts <LomIdView/> there
        for p in re.finditer(r"<([A-Za-z0-9_.]+)>\s*<LomId[^>]*/>(?:\s*<LomIdView[^>]*/>)?(?:\s*<ArrangerAutomation>.*?</ArrangerAutomation>)?"
                             r"\s*<Manual Value=\"([^\"]*)\"", body, re.S):
            try:
                vals[p[1]] = round(float(p[2]), 5)
            except ValueError:
                vals[p[1]] = {"true": 1.0, "false": 0.0}.get(p[2].lower())
        vals = {k: v for k, v in vals.items() if v is not None}
        if vals:
            mods["%s#%d" % (t, n[t])] = vals
            n[t] += 1
    if not mods:
        return None
    rel = path.split(os.sep)
    return {"src": path, "engine": "ableton", "library": rel[3] if len(rel) > 3 else "", "category": rel[-2],
            "name": os.path.splitext(rel[-1])[0], "tags": {}, "modules": mods, "license": "preset library, local only"}


def uhe(path):
    try:
        t = open(path, encoding="latin-1").read()
    except OSError:
        return None
    tags = {}
    m = re.search(r"/\*@Meta(.*?)\*/", t, re.S)
    if m:
        for k, v in re.findall(r"(\w+):\s*'([^']*)'", m[1]):
            tags[k.lower()] = v
        if "categories" in tags:
            tags["type"] = [c for c in re.split(r"[,;]", tags.pop("categories")) if c]
    am = re.search(r"#AM=(\w+)", t)
    mods, cur, n = {}, None, collections.Counter()
    for ln in t.splitlines():
        if ln.startswith("#cm="):
            name = ln[4:].strip()
            cur = "%s#%d" % (name, n[name])
            n[name] += 1
            mods[cur] = {}
        elif cur and "=" in ln and not ln.startswith(("#", "$$$$")):
            k, v = ln.split("=", 1)
            try:
                mods[cur][k.strip()] = round(float(v), 5)
            except ValueError:
                pass
    mods = {k: v for k, v in mods.items() if v}
    if not mods:
        return None
    return {"src": path, "engine": "u-he " + (am[1].lower() if am else "?"), "library": path.split(os.sep)[3] if len(path.split(os.sep)) > 3 else "",
            "category": os.path.basename(os.path.dirname(path)), "name": os.path.splitext(os.path.basename(path))[0],
            "tags": tags, "modules": mods, "license": "preset library, local only"}


def alchemy(path):
    try:
        t = open(path, encoding="latin-1").read()
    except OSError:
        return None
    mods, cur, n, head = {}, "HEAD", collections.Counter(), {}
    for ln in t.splitlines():
        ln = ln.strip()
        m = re.match(r"^<([A-Za-z0-9_]+)>$", ln)
        if m:
            cur = "%s#%d" % (m[1], n[m[1]])
            n[m[1]] += 1
            continue
        if " = " in ln:
            k, v = ln.split(" = ", 1)
            try:
                mods.setdefault(cur, {})[k] = round(float(v), 5)
            except ValueError:
                head[k] = v
    mods.pop("HEAD", None)
    mods = {k: v for k, v in mods.items() if v}
    if not mods:
        return None
    parts = path.split(os.sep)
    i = parts.index("Alchemy") + 1 if "Alchemy" in parts else len(parts) - 2
    cat = "/".join(parts[i:-1])
    tags = {"author": head.get("Author", ""), "attribs": [a for a in head.get("Attribs", "").split(",") if a],
            "type": [parts[i]] if i < len(parts) - 1 else []}
    return {"src": path, "engine": "alchemy", "library": parts[i] if i < len(parts) - 1 else "", "category": cat,
            "name": head.get("Name", os.path.splitext(parts[-1])[0]), "tags": tags, "modules": mods,
            "license": "preset library, local only"}


def logic_pst(path):
    b = open(path, "rb").read()
    i = b.find(b"EMAGPPST")
    if i < 0 or len(b) < i + 24:
        return None
    body = b[i + 16:]
    vals = [round(v, 5) for (v,) in struct.iter_unpack(">f", body[:len(body) // 4 * 4]) if v == v and abs(v) < 1e7]
    if not vals:
        return None
    parts = path.split(os.sep)
    plug = parts[parts.index("Plug-In Settings") + 1] if "Plug-In Settings" in parts else os.path.basename(os.path.dirname(path))
    return {"src": path, "engine": "logic " + plug, "library": "Logic", "category": plug,
            "name": os.path.splitext(parts[-1])[0], "tags": {}, "modules": {plug + "#0": {str(k): v for k, v in enumerate(vals)}},
            "license": "preset library, local only"}


def walk(roots):
    for root in roots:
        for d, subs, fs in os.walk(root):
            subs[:] = sorted(s for s in subs if s not in SKIP and not s.startswith("."))
            for f in sorted(fs):
                if f.startswith("._"):
                    continue
                p = os.path.join(d, f)
                low = f.lower()
                engine = "keyscape" if "keyscape" in p.lower() else "trilian" if "trilian" in p.lower() else "omnisphere"
                if low.endswith(".db") and ("/Patches/" in p or "/Multis/" in p):  # Soundsources .db hold audio
                    yield from spectra_db(p, engine)
                elif low.endswith(SPECTRA):
                    try:
                        yield spectra_patch(open(p, encoding="latin-1").read(), p, engine,
                                            os.path.basename(d), os.path.basename(d), os.path.splitext(f)[0])
                    except OSError:
                        continue
                elif low.endswith((".adv", ".adg")):
                    r = ableton(p)
                    if r:
                        yield r
                elif low.endswith((".h2p", ".h2pv")):
                    r = uhe(p)
                    if r:
                        yield r
                elif low.endswith(".acp"):
                    r = alchemy(p)
                    if r:
                        yield r
                elif low.endswith(".pst"):
                    r = logic_pst(p)
                    if r:
                        yield r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="*", default=["/Volumes/Wd-Black"])
    ap.add_argument("--out", default="~/Infinite/training-data/presets/presets.jsonl")
    a = ap.parse_args()
    out = os.path.expanduser(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    t0 = time.time()
    n = collections.Counter()
    mods = collections.Counter()
    tagged = collections.Counter()
    seen = set()
    with open(out, "w") as fo:
        for r in walk(a.roots):
            key = (r["engine"], r["library"], r["name"], len(r["modules"]))
            if key in seen:  # the same factory patch in a library and its update folder
                n["duplicate"] += 1
                continue
            seen.add(key)
            fo.write(json.dumps(r, separators=(",", ":")) + "\n")
            n[r["engine"]] += 1
            for m in r["modules"]:
                mods[r["engine"], m.split("#")[0]] += 1
            for k in r["tags"]:
                tagged[r["engine"], k] += 1
    print("patches:", dict(n), "in %.0f s" % (time.time() - t0))
    print("tagged:", dict(tagged))
    print("modules:", dict(mods.most_common(30)))
    print("wrote", out)


if __name__ == "__main__":
    main()
