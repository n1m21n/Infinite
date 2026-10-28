#!/usr/bin/env python3
"""Level-3 coverage for plug-ins whose saved state embeds the preset text: render every preset file headless, no GUI.

    preset_state.py --plugin Diva --presets "/Library/Audio/Presets/u-he/Diva" [--ext h2p] [--states plugin_states_key.jsonl]
                    [--budget 20] [--limit N] [--stride/--offset] [--lead 4] [--batch 25] [--resume] [--list]

Needs a template: the plug-in's default state from plugin_states.py (--plugin-state). A splicer per container format:

  uhe    Diva / Hive / Repro: [u64 L][text]  x2  (Diva)    or   [u64 L][u32 L-4][text]  x2  (Hive, Repro). The two blocks are
         identical copies; text = "#pgm=<name>\\n" + the .h2p file.
  tal    TAL VST3: JUCE chunk holding a <tal ...> XML document; the preset file (.pjp8, .bassline ...) is that XML.
  vital  Vital: JUCE chunk with the preset JSON ({"settings": ...}); the .vital file is that JSON.

Rows: row v2 from render_pass.state_rows (knobs from the host after the state loads, preset_id = hash of the preset file)."""
import argparse, base64, json, os, re, struct, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402
import nks_state  # noqa: E402


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


def fmt_of(plugin, ext=""):
    if ext == "nksf":
        return "nks"
    p = plugin.lower()
    if p in ("diva", "hive", "repro-1", "repro-5", "zebra2", "zebralette", "bazille", "podolski", "repro-1"):
        return "uhe"
    if p == "vital":
        return "vital"
    if p.startswith("tal"):
        return "tal"
    if p == "serum 2":
        return "xfer"
    return None


def presets_in_order(root, order):
    """a coreset_order.py list: farthest-first, so any prefix covers the library"""
    for rel in open(order).read().splitlines():
        p = os.path.join(root, rel)
        if os.path.isfile(p):
            yield rel, open(p, "rb").read()


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
    ap.add_argument("--order", default="", help="coreset_order.py list: render in this order (stride/offset then pick every Nth of it)")
    ap.add_argument("--states", default="~/Infinite/training-data/audio/plugin_states_key.jsonl")
    ap.add_argument("--out", default="")
    ap.add_argument("--budget", type=float, default=0)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--stride", type=int, default=1)
    ap.add_argument("--offset", type=int, default=0)
    ap.add_argument("--lead", type=float, default=2.0)
    ap.add_argument("--notes", default=rp.STIMULUS)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--batch", type=int, default=rp.BATCH, help="presets per Infinite process")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    rp.LEAD = a.lead
    rp.NOTES = [(int(m), int(v), l) for m, v, l in (x.split(":") for x in a.notes.split(","))]
    kind = fmt_of(a.plugin, a.ext)
    if kind == "nks":
        SPLICERS["nks"] = lambda raw, name, data: nks_state.splice_nks(a.plugin, raw, nks_state.read_nksf(data)[2])
    if kind not in SPLICERS:
        raise SystemExit("no splicer for %s yet" % a.plugin)
    tmpl = load_template(a.states, a.plugin)
    raw0 = base64.b64decode(tmpl["state_b64"] + "=" * (-len(tmpl["state_b64"]) % 4))
    out = os.path.expanduser(a.out or "~/Infinite/training-data/audio/preset_%s.jsonl" % re.sub(r"\W+", "", a.plugin.lower()))
    done = set()
    if a.resume and os.path.exists(out):
        done = {json.loads(l)["src"] for l in open(out) if l.strip()}  # parsed, so names with escaped bytes match too
    t = {"instrument": True, "format": "vst3", "id": tmpl["id"], "name": tmpl["name"], "vendor": ""}
    binp = rp.infinite_bin()
    t0, n, ok, bad = time.time(), 0, 0, 0
    pend = []

    def flush():
        nonlocal ok, bad
        with tempfile.TemporaryDirectory() as tmp:
            got = rp.render_states(binp, t, [x[3] for x in pend], tmp)
        for (label, rel, data, _), (win, params) in zip(pend, got):
            if win is None:
                bad += 1
                print("fail", rel, params, flush=True)
                continue
            tg = nks_state.tags_of(nks_state.read_nksf(data)[0]) if kind == "nks" else {}
            with open(out, "a") as fo:
                for row in rp.state_rows(t, 3, label, os.path.basename(rel), data, win, params, tg):
                    fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            ok += 1
        pend.clear()
        print("%d ok %d bad %.2fs/preset" % (ok, bad, (time.time() - t0) / max(1, n)), flush=True)

    for i, (rel, data) in enumerate(presets_in_order(a.presets, a.order) if a.order else presets(a.presets, a.ext)):
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
        new = SPLICERS[kind](raw0, os.path.splitext(os.path.basename(rel))[0] + "." + a.ext, data)
        if new is None:
            print("skip (no splice)", rel, flush=True)
            continue
        pend.append((label, rel, data, base64.b64encode(new).decode()))
        if len(pend) >= a.batch:
            flush()
    if pend:
        flush()
    print("done: %d ok %d bad" % (ok, bad))


if __name__ == "__main__":
    main()
