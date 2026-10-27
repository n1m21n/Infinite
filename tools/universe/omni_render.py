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
    ap.add_argument("--notes", default="C3", help="comma list of rp.NOTES labels to render (C3 = first pass; C2,C4 = second)")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    rp.LEAD = a.lead
    rp.NOTES = [x for x in rp.NOTES if x[2] in a.notes.split(",")]
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
    for i, (src, patch) in enumerate(patches(a.lib)):
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
        n += 1
        with tempfile.TemporaryDirectory() as tmp:
            inf = os.path.join(tmp, "p.inf")
            open(inf, "w").write(rp.patch_text(t, "", []).replace("  s plugin_name", "  s plugin_state %s\n  s plugin_name" % make_state(txml, ttail, patch), 1))
            ep, mp = rp.write_notes(tmp)
            js, wav = os.path.join(tmp, "s.json"), os.path.join(tmp, "o.wav")
            try:
                p = subprocess.run([binp, "--audio-summary", inf, js, "--duration", "%g" % (rp.LEAD + rp.WIN * len(rp.NOTES)), "--sample-rate", str(rp.SR),
                                    "--wav", wav, "--lenient", "--notes", ep, "--note-map", mp], capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired:
                bad += 1
                print("timeout", src, flush=True)
                continue
            if p.returncode != 0 or not os.path.exists(wav):
                bad += 1
                print("fail", src, (p.stderr.strip().splitlines() or ["rc %d" % p.returncode])[-1][:100], flush=True)
                continue
            rows = rp.measure_windows(wav, tmp, [x for _, _, x in rp.NOTES])
        tg = tags(patch)
        with open(out, "a") as fo:
            for lab, feat in rows:
                row = {"src": label, "label": os.path.basename(src), "plugin": kv.get("plugin_name"), "plugin_id": kv["plugin_id"], "note": lab,
                       "tags": tg, "license": "plugin render, local only"}
                row.update({k: v for k, v in (feat or {}).items() if k not in ("src", "pack", "part", "bpm", "key", "loop")})
                fo.write(json.dumps(row, separators=(",", ":")) + "\n")
        ok += 1
        if ok % 10 == 0:
            print("%d ok %d bad %.1fs/patch" % (ok, bad, (time.time() - t0) / max(1, n)), flush=True)
    print("done: %d ok %d bad" % (ok, bad))


if __name__ == "__main__":
    main()
