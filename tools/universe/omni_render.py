#!/usr/bin/env python3
"""Level-3 coverage: render every Omnisphere patch headless, no GUI.

    omni_render.py --template omni_test_1.inf --lib "<.../Omnisphere/Settings Library/Patches>" [--budget 20] [--resume] [--list]

Omnisphere's saved plug-in state is a 32-byte header + the multi XML (<SynthMaster>, 8 x <SynthSubEngine>) + 12 zero bytes. Each
sub-engine body is exactly a patch file's content without the <AmberPart> wrapper, and the factory patches sit in `<name>.db` files as
an XML directory (<FILE name offset size>) followed by the plain .prt_omn bytes. So: take one saved state as the template, swap
sub-engine 0's body for the patch, fix the two length fields, base64 it into the Keyboard -> Plugin -> Audio Out patch render_pass.py
uses, render C2 / C3 / C4 and measure (32-band row). The patch's own tags (Genre, Mood, Type, Author, Complexity) ride on every row as
labels. Statistics only: the WAV is deleted. Rows go to ~/Infinite/training-data/audio/state_renders.jsonl; --resume skips done src."""
import argparse, base64, json, os, re, struct, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render_pass as rp  # noqa: E402
from state_render import read_node  # noqa: E402

OPEN, CLOSE = b"<SynthSubEngine >\n", b"</SynthSubEngine>"


def parse_state(b64):
    s = base64.b64decode(b64 + "=" * (-len(b64) % 4))
    tot, magic, _, _, _, xl = struct.unpack("<QIIIIQ", s[:32])
    return s[32:32 + xl], s[32 + xl:]


def make_state(tmpl_xml, tmpl_tail, patch):
    """patch = .prt_omn bytes; returns the base64 state with sub-engine 0 replaced"""
    body = patch.strip()
    if body.startswith(b"<AmberPart"):
        body = body[body.index(b"\n") + 1:]
    if body.endswith(b"</AmberPart>"):
        body = body[:-len(b"</AmberPart>")]
    a = tmpl_xml.index(OPEN) + len(OPEN)
    b = tmpl_xml.index(CLOSE)
    xml = tmpl_xml[:a] + body.rstrip() + b"\n " + tmpl_xml[b:]
    s = struct.pack("<QIIIIQ", len(xml) + len(tmpl_tail) + 16, 999999999, 0, 1, 0, len(xml)) + xml + tmpl_tail
    return base64.b64encode(s).decode()


def make_multi_state(multi, tmpl_tail):
    """a factory multi (.mlt_omn) is a whole <SynthMaster> document: it replaces the template's XML outright"""
    xml = multi.strip()
    if not xml.startswith(b"<SynthMaster"):
        return None
    xml += b"\n"
    s = struct.pack("<QIIIIQ", len(xml) + len(tmpl_tail) + 16, 999999999, 0, 1, 0, len(xml)) + xml + tmpl_tail
    return base64.b64encode(s).decode()


def multis(lib):
    """yield (src, bytes) for every loose .mlt_omn under lib (the Multis folder)"""
    for root, _, fs in os.walk(lib):
        for f in sorted(fs):
            if f.endswith(".mlt_omn") and not f.startswith("._"):
                yield "omni-multi:%s" % os.path.relpath(os.path.join(root, f), lib), open(os.path.join(root, f), "rb").read()


def patches(lib):
    """yield (src, bytes) for every patch under lib: .db containers and loose .prt_omn"""
    for root, _, fs in os.walk(lib):
        for f in sorted(fs):
            p = os.path.join(root, f)
            if f.startswith("._"):
                continue
            rel = os.path.relpath(p, lib)
            if f.endswith(".db"):
                db = open(p, "rb").read()
                end = db.find(b"</FileSystem>")
                if end < 0:
                    continue
                end += len(b"</FileSystem>\n")
                dirs, out = [], []
                for m in re.finditer(rb'<DIR name="([^"]*)">|</DIR>|<FILE name="([^"]+)" offset="(\d+)" size="(\d+)"', db[:end]):
                    if m.group(1) is not None:
                        dirs.append(m.group(1).decode("utf8", "replace"))
                    elif m.group(2) is None:
                        if dirs:
                            dirs.pop()
                    elif m.group(2).endswith(b".prt_omn"):
                        o, n = int(m.group(3)), int(m.group(4))
                        out.append(("omni:%s#%s/%s" % (rel, "/".join(dirs), m.group(2).decode("utf8", "replace")), db[end + o:end + o + n]))
                yield from out
            elif f.endswith(".prt_omn"):
                yield "omni:%s" % rel, open(p, "rb").read()


def tags(patch):
    m = re.search(rb'<ENTRYDESCR[^>]*?ATTRIB_VALUE_DATA="([^"]*)"', patch)
    d = {}
    if m:
        for kv in m.group(1).decode("utf8", "replace").split(";"):
            if "=" in kv:
                k, v = kv.split("=", 1)
                d.setdefault(k, []).append(v)
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", required=True)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--out", default="~/Infinite/training-data/audio/state_renders.jsonl")
    ap.add_argument("--budget", type=float, default=0, help="minutes; 0 = unlimited")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--stride", type=int, default=1, help="render every Nth patch (shard with --offset)")
    ap.add_argument("--offset", type=int, default=0)
    ap.add_argument("--lead", type=float, default=8.0, help="seconds of silence while the samples stream in (Omnisphere loads async)")
    ap.add_argument("--notes", default=rp.STIMULUS, help="midi:velocity:label list; default = the shared stimulus (render_pass.STIMULUS)")
    ap.add_argument("--multis", action="store_true", help="--lib is the Multis folder: render each .mlt_omn as a whole multi")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--batch", type=int, default=rp.BATCH, help="patches per Infinite process")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    rp.LEAD = a.lead
    rp.NOTES = [(int(m), int(v), l) for m, v, l in (x.split(":") for x in a.notes.split(","))]
    out = os.path.expanduser(a.out)
    done = set()
    if a.resume and os.path.exists(out):
        for ln in open(out):
            m = re.search(r'"src":"([^"]+)"', ln)
            if m:
                done.add(m.group(1))
    kv = read_node(a.template)
    txml, ttail = parse_state(kv["plugin_state"])
    t = {"instrument": True, "format": kv["plugin_format"], "id": kv["plugin_id"], "name": kv.get("plugin_name", ""), "vendor": ""}
    binp = rp.infinite_bin()
    t0, n, ok, bad = time.time(), 0, 0, 0
    pend = []

    def flush():
        nonlocal ok, bad
        with tempfile.TemporaryDirectory() as tmp:
            got = rp.render_states(binp, t, [x[2] for x in pend], tmp)
        for (src, patch, _), (win, params) in zip(pend, got):
            if win is None:
                bad += 1
                print("fail", src, params, flush=True)
                continue
            with open(out, "a") as fo:
                for row in rp.state_rows(t, 3, "state:" + src, os.path.basename(src), patch if isinstance(patch, bytes) else patch.encode(), win, params, tags(patch)):
                    fo.write(json.dumps(row, separators=(",", ":")) + "\n")
            ok += 1
        pend.clear()
        print("%d ok %d bad %.2fs/patch" % (ok, bad, (time.time() - t0) / max(1, n)), flush=True)

    for i, (src, patch) in enumerate(multis(a.lib) if a.multis else patches(a.lib)):
        if i % a.stride != a.offset:
            continue
        if a.list:
            print(src, len(patch), tags(patch).get("Type"))
            continue
        label = "state:" + src
        if label in done:
            continue
        if a.limit and n >= a.limit or a.budget and time.time() - t0 > a.budget * 60:
            break
        state = make_multi_state(patch, ttail) if a.multis else make_state(txml, ttail, patch)
        if state is None:
            print("skip (not a SynthMaster document)", src, flush=True)
            continue
        n += 1
        pend.append((src, patch, state))
        if len(pend) >= a.batch:
            flush()
    if pend:
        flush()
    print("done: %d ok %d bad" % (ok, bad))


if __name__ == "__main__":
    main()
