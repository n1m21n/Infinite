#!/usr/bin/env python3
"""Level-3 coverage for plug-ins whose saved state embeds the preset text: render every preset file headless, no GUI.

    preset_state.py --plugin Diva --presets "/Library/Audio/Presets/u-he/Diva" [--ext h2p] [--states plugin_states_key.jsonl]
                    [--budget 20] [--limit N] [--stride/--offset] [--lead 4] [--resume] [--list]

Needs a template: the plug-in's default state from plugin_states.py (--plugin-state). A splicer per container format:

  uhe    Diva / Hive / Repro: [u64 L][text]  x2  (Diva)    or   [u64 L][u32 L-4][text]  x2  (Hive, Repro). The two blocks are
         identical copies; text = "#pgm=<name>\\n" + the .h2p file.
  tal    TAL VST3: JUCE chunk holding a <tal ...> XML document; the preset file (.pjp8, .bassline ...) is that XML.
  vital  Vital: JUCE chunk with the preset JSON ({"settings": ...}); the .vital file is that JSON.

Rows: same schema as omni_render.py (src "state:<plugin>:<rel path>", note windows, 32-band row), so recipe rows join by src."""
import argparse, base64, json, os, re, struct, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402


def load_template(path, plugin):
    best = None
    for ln in open(os.path.expanduser(path)):
        r = json.loads(ln)
        if r.get("state_b64") and plugin.lower() == r["name"].lower() and r["format"] == "vst3":
            best = r
    if best is None:
        raise SystemExit("no vst3 template state for %r in %s" % (plugin, path))
    return best


def uhe_kind(raw):
    """'u64' (Diva): [u64 L][text]; 'u64u32' (Hive, Repro): [u64 L][u32 L-4][text]"""
    return "u64" if raw[8:13] == b"#pgm=" else "u64u32"


def splice_uhe(raw, name, preset):
    kind = uhe_kind(raw)
    text = b"#pgm=" + name.encode() + b"\n" + preset.strip(b"\x00").rstrip() + b"\n\x00\x00"
    if kind == "u64":
        blk = struct.pack("<Q", len(text)) + text
    else:
        blk = struct.pack("<QI", len(text) + 4, len(text)) + text
    return blk + blk


def splice_juce(raw, preset, start_tok, end_tok):
    """VST3 JUCE chunk (u64 total | VstW .. CcnK <be32> .. <be32 chunk size> <document> .. JUCEPrivateData): swap the document for the
    preset and grow the three length fields by the difference"""
    a = raw.index(start_tok)
    b = raw.rindex(end_tok) + len(end_tok)
    d = len(preset) - (b - a)
    out = bytearray(raw[:a] + preset + raw[b:])
    struct.pack_into("<Q", out, 0, struct.unpack_from("<Q", raw, 0)[0] + d)
    c = raw.index(b"CcnK") + 4
    struct.pack_into(">I", out, c, struct.unpack_from(">I", raw, c)[0] + d)
    struct.pack_into(">I", out, a - 4, struct.unpack_from(">I", raw, a - 4)[0] + d)
    return bytes(out)


def splice_vital(raw, name, preset):
    return splice_juce(raw, preset.strip(), b"{", b"}")


def splice_tal(raw, name, preset):
    return splice_juce(raw, preset.strip(), b"<?xml", b"</tal>")


def splice_xfer(raw, name, preset):
    """Serum 2: the .SerumPreset file is the state minus its 8-byte length prefix"""
    return struct.pack("<Q", struct.unpack_from("<Q", raw, 0)[0]) + preset


SPLICERS = {"uhe": splice_uhe, "vital": splice_vital, "tal": splice_tal, "xfer": splice_xfer}


def fmt_of(plugin):
    p = plugin.lower()
    if p in ("diva", "hive", "repro-1", "repro-5", "zebra2", "zebralette", "bazille", "podolski", "repro-1"):
        return "uhe"
    if p == "vital":
        return "vital"
    if p.startswith("tal "):
        return "tal"
    if p == "serum 2":
        return "xfer"
    return None


def presets(root, ext):
    for r, _, fs in os.walk(root):
        for f in sorted(fs):
            if f.endswith("." + ext) and not f.startswith("._"):
                p = os.path.join(r, f)
                yield os.path.relpath(p, root), open(p, "rb").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--presets", required=True)
    ap.add_argument("--ext", default="h2p")
    ap.add_argument("--states", default="~/Infinite/training-data/audio/plugin_states_key.jsonl")
    ap.add_argument("--out", default="")
    ap.add_argument("--budget", type=float, default=0)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--offset", type=int, default=0)
    ap.add_argument("--lead", type=float, default=2.0)
    ap.add_argument("--notes", default="36:100:C2,48:100:C3,60:100:C4,72:100:C5,48:30:C3p,48:127:C3f")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    rp.LEAD = a.lead
    rp.NOTES = [(int(m), int(v), l) for m, v, l in (x.split(":") for x in a.notes.split(","))]
    kind = fmt_of(a.plugin)
    if kind not in SPLICERS:
        raise SystemExit("no splicer for %s yet" % a.plugin)
    tmpl = load_template(a.states, a.plugin)
    raw0 = base64.b64decode(tmpl["state_b64"] + "=" * (-len(tmpl["state_b64"]) % 4))
    out = os.path.expanduser(a.out or "~/Infinite/training-data/audio/preset_%s.jsonl" % re.sub(r"\W+", "", a.plugin.lower()))
    done = set()
    if a.resume and os.path.exists(out):
        done = {m.group(1) for m in (re.search(r'"src":"([^"]+)"', l) for l in open(out)) if m}
    t = {"instrument": True, "format": "vst3", "id": tmpl["id"], "name": tmpl["name"], "vendor": ""}
    binp = rp.infinite_bin()
    t0, n, ok, bad = time.time(), 0, 0, 0
    for i, (rel, data) in enumerate(presets(a.presets, a.ext)):
        if i % a.stride != a.offset:
            continue
        if a.list:
            print(rel, len(data))
            continue
        label = "state:%s:%s" % (a.plugin.lower(), rel)
        if label in done:
            continue
        if a.limit and n >= a.limit or a.budget and time.time() - t0 > a.budget * 60:
            break
        n += 1
        state = base64.b64encode(SPLICERS[kind](raw0, os.path.splitext(os.path.basename(rel))[0] + "." + a.ext, data)).decode()
        with tempfile.TemporaryDirectory() as tmp:
            inf = os.path.join(tmp, "p.inf")
            open(inf, "w").write(rp.patch_text(t, "", []).replace("  s plugin_name", "  s plugin_state %s\n  s plugin_name" % state, 1))
            ep, mp = rp.write_notes(tmp)
            js, wav = os.path.join(tmp, "s.json"), os.path.join(tmp, "o.wav")
            try:
                p = subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (rp.LEAD + rp.WIN * len(rp.NOTES)), "--sample-rate", str(rp.SR),
                                    "--wav", wav, "--lenient", "--notes", ep, "--note-map", mp], capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired:
                bad += 1
                print("timeout", rel, flush=True)
                continue
            if p.returncode != 0 or not os.path.exists(wav):
                bad += 1
                print("fail", rel, flush=True)
                continue
            rows = rp.measure_windows(wav, tmp, [x for _, _, x in rp.NOTES])
        with open(out, "a") as fo:
            for lab, feat in rows:
                row = {"src": label, "label": os.path.basename(rel), "plugin": tmpl["name"], "plugin_id": tmpl["id"], "note": lab,
                       "license": "plugin render, local only"}
                row.update({k: v for k, v in (feat or {}).items() if k not in ("src", "pack", "part", "bpm", "key", "loop", "license")})
                fo.write(json.dumps(row, separators=(",", ":")) + "\n")
        ok += 1
        if ok % 10 == 0:
            print("%d ok %d bad %.1fs/preset" % (ok, bad, (time.time() - t0) / max(1, n)), flush=True)
    print("done: %d ok %d bad" % (ok, bad))


if __name__ == "__main__":
    main()
