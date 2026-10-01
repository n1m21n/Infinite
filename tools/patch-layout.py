#!/usr/bin/env python3
"""patch-layout.py - give every node of an .inf patch a `pos` so nothing spawns stacked at 0,0.

    python3 tools/patch-layout.py in.inf [out.inf]      (default: rewrite in place)

Layout: three horizontal bands (Picture, Sound, Modulation), each laid out left->right by wiring depth
(sources left, outputs right), nodes in a column stacked top->bottom with a gap, sizes from SIZES (the
real GUI sizes vary; the table is deliberately generous). Comment nodes are not part of the graph: put
`# near <id>` on the line before a `node ... Comment` block and it is placed just above that node;
`# band <Picture|Sound|Modulation>` places it as the band's header. Any node that already has a `pos`
is left where it is only with --keep. Wire lines and everything else pass through untouched."""
import re, sys

GAP_X, GAP_Y, BAND_GAP, NOTE_GAP = 160, 110, 260, 36
SIZES = {  # (w, h) canvas units, generous
    "Wavetable": (1000, 1260), "Analog": (700, 900), "Metallic": (460, 560), "Oscillator": (700, 900),
    "Mixer": (740, 580), "Reverb": (460, 480), "Delay": (460, 480), "Chorus": (460, 480), "Limiter": (460, 420),
    "Drum Sequencer": (900, 900), "MPC": (900, 900), "Sampler": (700, 700), "Dynamics": (500, 520), "EQ": (620, 560),
    "Random Note Generator": (320, 300), "Arpeggiator": (340, 320), "Note Sequencer": (900, 600), "Chorder": (340, 320),
    "Audio Analyze": (250, 560), "LFO": (250, 340), "Envelope": (260, 340), "Random": (250, 300), "Pattern": (420, 420),
    "FieldPixel": (340, 380), "Output": (340, 340), "Audio Out": (300, 220),
    "Render 3D": (360, 420), "Material": (320, 520), "Camera": (300, 320), "Light": (300, 320),
}
CAT_DEFAULT = {"Source": (300, 300), "Effects": (280, 240), "Compositing": (280, 240), "3D": (300, 320),
               "Utility": (320, 280), "Modulators": (250, 320), "Notes": (330, 300), "Synths": (700, 800),
               "AudioEffects": (460, 480), "Macros": (260, 260), "Prediction": (360, 360)}
BAND_OF = {"Source": "Picture", "Effects": "Picture", "Compositing": "Picture", "3D": "Picture",
           "Notes": "Sound", "Synths": "Sound", "AudioEffects": "Sound", "Modulators": "Modulation",
           "Macros": "Modulation", "Prediction": "Modulation"}
ORDER = ["Picture", "Sound", "Modulation"]
WIRE = ("cable", "geo", "aud", "note")


def main():
    keep = "--keep" in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    src = args[0]
    dst = args[1] if len(args) > 1 else src
    lines = open(src).read().split("\n")
    head, blocks, tail, hints = [], [], [], []
    i = 0
    while i < len(lines) and not lines[i].startswith("node "):
        head.append(lines[i]); i += 1
    cur_hint = None
    while i < len(lines):
        l = lines[i]
        if l.startswith("node "):
            blk = [l]; i += 1
            while i < len(lines) and lines[i] != "end":
                blk.append(lines[i]); i += 1
            blk.append("end"); i += 1
            blocks.append((blk, cur_hint)); cur_hint = None
        elif l.startswith("# near ") or l.startswith("# band "):
            cur_hint = l[2:].strip(); i += 1
        elif l.startswith(("cable ", "geo ", "aud ", "note ", "mod ", "expr ", "glob ", "pal ")):
            tail.append(l); i += 1
        else:
            tail.append(l); i += 1
    nodes = {}
    order = []
    for blk, hint in blocks:
        m = re.match(r"node (\S+) (\S+) (.*)", blk[0])
        idx, cat, typ = m.groups()
        nid = idx
        for b in blk:
            mm = re.match(r"\s+id (\w+)", b)
            if mm: nid = mm.group(1)
        d = dict(idx=idx, cat=cat, typ=typ, id=nid, blk=blk, hint=hint)
        if typ == "Comment":
            for b in blk:
                mw = re.match(r"\s+f (width|height) ([\d.]+)", b)
                if mw: d[mw.group(1)] = float(mw.group(2))
            d["w"], d["h"] = d.get("width", 260), d.get("height", 140)
        else:
            d["w"], d["h"] = SIZES.get(typ, CAT_DEFAULT.get(cat, (300, 300)))
        nodes[idx] = d; nodes[nid] = d; order.append(d)
    # wires -> edges (src -> dst)
    edges = []
    for l in tail:
        p = l.split()
        if p and p[0] in WIRE and len(p) >= 4 and p[1] in nodes and p[3] in nodes:
            edges.append((nodes[p[3]]["idx"], nodes[p[1]]["idx"]))
    for d in order:
        if d["typ"] == "Comment": d["band"] = None; continue
        d["band"] = BAND_OF.get(d["cat"], "Picture")
        if d["cat"] == "Utility" and d["typ"] not in ("Output", "Syphon Out", "Field Graph", "Projection", "Viewport"):
            d["band"] = "Sound"
    # depth within a band by longest path (in-band edges only)
    real = [d for d in order if d["typ"] != "Comment"]
    depth = {d["idx"]: 0 for d in real}
    for _ in range(len(real)):
        ch = False
        for s, t in edges:
            if nodes[s]["band"] == nodes[t]["band"] and depth[t] < depth[s] + 1:
                depth[t] = depth[s] + 1; ch = True
        if not ch: break
    pos = {}
    y0 = 0.0
    bands_y = {}
    for band in ORDER:
        mem = [d for d in real if d["band"] == band]
        if not mem: continue
        hdr = next((d for d, _ in [(c, 0) for c in order if c["typ"] == "Comment" and c["hint"] == f"band {band}"]), None)
        top = y0 + (hdr["h"] + NOTE_GAP if hdr else 0)
        cols = {}
        if band == "Modulation":  # a row: modulators have no useful depth, one column each
            for k, d in enumerate(mem): depth[d["idx"]] = k
        for d in mem: cols.setdefault(depth[d["idx"]], []).append(d)
        x = 0.0; band_h = 0.0
        for k in sorted(cols):
            y = top
            notes = [c["w"] for d in cols[k] for c in order if c["typ"] == "Comment" and c["hint"] == f"near {d['id']}"]
            cw = max([d["w"] for d in cols[k]] + notes)
            for d in cols[k]:
                # leave room above a node for its comment
                note = next((c for c in order if c["typ"] == "Comment" and c["hint"] == f"near {d['id']}"), None)
                if note: y += note["h"] + NOTE_GAP
                pos[d["idx"]] = (x, y)
                if note: pos[note["idx"]] = (x, y - note["h"] - NOTE_GAP)
                y += d["h"] + GAP_Y
            band_h = max(band_h, y - GAP_Y - top)
            x += cw + GAP_X
        if hdr: pos[hdr["idx"]] = (0.0, y0)
        y0 = top + band_h + BAND_GAP
    for c in order:  # unplaced comments (missing target): park top-left
        if c["typ"] == "Comment" and c["idx"] not in pos: pos[c["idx"]] = (-c["w"] - GAP_X, 0.0)
    out = list(head)
    for d in order:
        blk = [b for b in d["blk"] if keep is False and not re.match(r"\s+pos ", b) or keep]
        x, y = pos[d["idx"]]
        k = next((j for j, b in enumerate(blk) if re.match(r"\s+id ", b)), 0)
        blk.insert(k + 1, f"  pos {x:.0f} {y:.0f}")
        out += blk
    out += tail
    open(dst, "w").write("\n".join(out).rstrip("\n") + "\n")
    print(f"laid out {len(real)} nodes + {len(order) - len(real)} comments -> {dst}")


main()
