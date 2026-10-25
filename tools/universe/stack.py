#!/usr/bin/env python3
"""Visual style = shapes stacked in layers. A style card lists layers; each layer is one Sketch node whose script is an
arrangement sampler plus a mark, the layers are composited with Blend nodes, then a finish chain and Output.
    stack.py sheet bauhaus,op_art,swiss --seeds 8 --out /tmp/uni/sheet     # contact sheet (needs the built binary)
Card layer keys: mark, arr, n (lo,hi), size (lo,hi as fraction of the short side), col (palette indices), fill (0/1), line (px, 0 = none),
rot ('none' | 'snap' | 'free' | 'radial'), blend (Blend mode), mix, spin (turns per minute, 0 = still), text (words), gap (0..1 jitter)."""
import json, os, random, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import universe as u
import scene

W, H = 1080, 1920

PRELUDE = r"""
let _s = SEED;
function rnd() { _s = (_s * 1664525 + 1013904223) % 4294967296; return _s / 4294967296; }
function ri(a, b) { return a + rnd() * (b - a); }
function pick(a) { return a[floor(rnd() * a.length)]; }
let S = 1;
function pal(i) { return PAL[((i % PAL.length) + PAL.length) % PAL.length]; }
function place(arr, n) {
  const P = [];
  if (arr === 'grid') { const c = max(1, round(sqrt(n * width / height))), r = max(1, ceil(n / c));
    for (let j = 0; j < r; j++) for (let i = 0; i < c; i++) P.push([(i + 0.5) / c * width, (j + 0.5) / r * height, 1 / max(c, r * width / height)]); }
  else if (arr === 'radial') { const R = S * 0.42; for (let i = 0; i < n; i++) { const a = i / n * TAU; P.push([width / 2 + R * cos(a), height / 2 + R * sin(a), 1 / 5]); } }
  else if (arr === 'concentric') { for (let i = 0; i < n; i++) P.push([width / 2, height / 2, (i + 1) / n]); }
  else if (arr === 'spiral') { for (let i = 0; i < n; i++) { const a = i * 2.39996, r = sqrt((i + 0.5) / n) * S * 0.52; P.push([width / 2 + r * cos(a), height / 2 + r * sin(a), 0.2 + 0.8 * i / n]); } }
  else if (arr === 'thirds') { const xs = [width / 3, width * 2 / 3, width * 0.382, width * 0.618], ys = [height / 3, height * 2 / 3, height * 0.382, height * 0.618];
    for (let i = 0; i < n; i++) P.push([pick(xs) + ri(-0.03, 0.03) * width, pick(ys) + ri(-0.03, 0.03) * height, 0.3 + rnd()]); }
  else if (arr === 'axis') { for (let i = 0; i < ceil(n / 2); i++) { const x = ri(0.05, 0.48) * width, y = ri(0.05, 0.95) * height, s = 0.3 + rnd(); P.push([x, y, s]); P.push([width - x, y, s]); } }
  else if (arr === 'diagonal') { for (let i = 0; i < n; i++) { const t = (i + 0.5) / n; P.push([width * (0.1 + 0.8 * t) + ri(-0.03, 0.03) * width, height * (0.9 - 0.8 * t) + ri(-0.03, 0.03) * height, 0.4 + 0.6 * sin(t * PI)]); } }
  else if (arr === 'bands') { const k = max(2, round(sqrt(n))); for (let i = 0; i < n; i++) { const b = i % k; P.push([ri(0.05, 0.95) * width, (b + 0.5) / k * height, 0.3 + rnd()]); } }
  else if (arr === 'stripes') { for (let i = 0; i < n; i++) P.push([width / 2, (i + 0.5) / n * height, (i + 0.5) / n]); }
  else { const min2 = S * 0.8 / sqrt(max(n, 1)); let tries = 0;   // scatter: poisson-ish, rejects close points
    while (P.length < n && tries++ < n * 40) { const x = ri(0.04, 0.96) * width, y = ri(0.04, 0.96) * height; let ok = true;
      for (const q of P) if (dist(x, y, q[0], q[1]) < min2) { ok = false; break; }
      if (ok) P.push([x, y, 0.3 + rnd()]); } }
  return P;
}
function mark(m, s, line, filled) {
  if (m === 'circle') circle(0, 0, s / 2);
  else if (m === 'ring') { noFill(); strokeWeight(max(line, s * 0.08)); circle(0, 0, s / 2); }
  else if (m === 'square') rect(-s / 2, -s / 2, s, s);
  else if (m === 'bar') rect(-s * 0.9, -s * 0.07, s * 1.8, s * 0.14);
  else if (m === 'rule') rect(-s * 0.9, -s * 0.012, s * 1.8, s * 0.024);
  else if (m === 'tri') triangle(0, -s * 0.58, -s * 0.5, s * 0.29, s * 0.5, s * 0.29);
  else if (m === 'half') { beginShape(); for (let i = 0; i <= 24; i++) { const a = PI + PI * i / 24; vertex(cos(a) * s / 2, sin(a) * s / 2 + s * 0.25); } endShape(1); }
  else if (m === 'quarter') { beginShape(); vertex(-s / 2, s / 2); for (let i = 0; i <= 24; i++) { const a = -PI / 2 * i / 24; vertex(-s / 2 + cos(a) * s, s / 2 + sin(a) * s); } endShape(1); }
  else if (m === 'cross') { rect(-s * 0.5, -s * 0.12, s, s * 0.24); rect(-s * 0.12, -s * 0.5, s * 0.24, s); }
  else if (m === 'line') { stroke(CUR); strokeWeight(max(1, line)); line(-s / 2, 0, s / 2, 0); }
  else if (m === 'dot') circle(0, 0, max(2, s * 0.08));
  else circle(0, 0, s / 2);
}
"""


def hexrgb(h):
    h = h.lstrip("#")
    return [round(int(h[i:i + 2], 16) / 255, 4) for i in (0, 2, 4)]


def layer_js(card, L, seed):
    pal = [hexrgb(c) for c in card["palette"]]
    cols = L.get("col") or list(range(len(pal)))
    code = PRELUDE.replace("SEED", str(seed)).replace("PAL", json.dumps(pal))
    bg = L.get("bg")
    out = [code, "const COLS = %s;" % json.dumps(cols), "let CUR = [0, 0, 0];"]
    if L.get("text"):
        out.append("const WORDS = %s;" % json.dumps(L["text"]))
    out += ["", "function draw(t) {", "  S = min(width, height);", "  _s = %d;" % seed,
            "  background(%s);" % ("PAL_BG" if False else ("%s, 1" % ",".join(map(str, pal[bg])) if bg is not None else "0, 0, 0, 0"))]
    n_lo, n_hi = L.get("n", (8, 20))
    s_lo, s_hi = L.get("size", (0.08, 0.3))
    arr, mk, rot, line = L.get("arr", "scatter"), L.get("mark", "circle"), L.get("rot", "none"), L.get("line", 0)
    spin = L.get("spin", 0)
    out += ["  const n = floor(ri(%d, %d + 1));" % (n_lo, n_hi), "  const P = place('%s', n);" % arr,
            "  for (let i = 0; i < P.length; i++) {", "    const p = P[i];",
            "    const s = S * (%.4f + (%.4f - %.4f) * (%s)) ;" % (s_lo, s_hi, s_lo, "p[2] * (arr_scale)" if False else ("p[2]" if arr in ("concentric", "spiral", "diagonal", "thirds", "bands", "axis", "grid", "stripes") else "rnd()")),
            "    const c = pal(pick(COLS)); CUR = c;",
            "    push(); translate(p[0], p[1]);"]
    if rot == "snap":
        out.append("    rotate(floor(rnd() * 4) * PI / 2);")
    elif rot == "free":
        out.append("    rotate(rnd() * TAU);")
    elif rot == "radial":
        out.append("    rotate(atan2(p[1] - height / 2, p[0] - width / 2) + PI / 2);")
    if spin:
        out.append("    rotate(t * %.4f * TAU / 60 * (i %% 2 ? 1 : -1));" % spin)
    if L.get("fill", 1):
        out.append("    fill(c);")
    else:
        out.append("    noFill();")
    if line:
        out.append("    stroke(c); strokeWeight(%s);" % line)
    else:
        out.append("    noStroke();")
    if arr == "stripes":
        out += ["    pop(); push();", "    stroke(c); strokeWeight(%s);" % (line or 4), "    noFill();",
                "    beginShape(); for (let x = 0; x <= width; x += 12) vertex(x, p[1] + sin(x / width * TAU * %.2f + p[2] * %.2f + t * 0.3) * S * %.3f); endShape(0);" %
                (L.get("freq", 2), L.get("warp", 6), L.get("amp", 0.04))]
    elif arr == "concentric":
        out += ["    noFill(); stroke(c); strokeWeight(%s);" % (line or 6), "    circle(0, 0, S * 1.05 * p[2] * (1 + 0.04 * sin(t + i)));"]
    elif L.get("text"):
        out += ["    textSize(s * %.3f);" % L.get("tscale", 0.9), "    text(pick(WORDS), min(0, width * 0.5 - p[0]), 0);"]
    else:
        out.append("    mark('%s', s, %s, true);" % (mk, line or 0))
    out += ["    pop();", "  }", "}"]
    return "\n".join(out)


def sketch_node(g, facts, code):
    nid = g.add("Sketch", 0, force={"code": code.replace("%%", "%"), "width": W, "height": H})
    return nid


def compile_card(card, seed, facts):
    rng = random.Random("%s:%d" % (card["id"], seed))
    g = scene.Graph(facts, rng)
    cur = None
    for i, L in enumerate(card["layers"]):
        nid = g.add("Sketch", 0)
        s = g.lines.index("node %d %s Sketch" % (nid, facts["Sketch"]["category"]))
        e = g.lines.index("end", s)
        body = [l for l in g.lines[s + 1:e] if not l.startswith(("  s code ", "  f width ", "  f height "))]
        code = layer_js(card, dict(L, bg=card["bg"] if i == 0 else None), rng.randrange(1, 1 << 30)).replace("\n", "\\n")
        g.lines[s + 1:e] = body + ["  s code " + code, "  f width %d" % W, "  f height %d" % H]
        if cur is None:
            cur = nid
        else:
            b = g.add("Blend", 0, force={"mode": ("i", L.get("blend", 0)), "mix": ("f", L.get("mix", 1.0))})
            g.connect(cur, b); g.connect(nid, b); cur = b
    for fx in card.get("finish", []):
        name, force = (fx, {}) if isinstance(fx, str) else fx
        force = {k: (("f", v) if isinstance(v, float) else ("i", v)) for k, v in force.items()}
        n = g.add(name, 0, force=force); g.connect(cur, n); cur = n
    out = g.add("Output", 0)
    g.connect(cur, out)
    return g.text()


# ---------------------------------------------------------------- cards (first three; the rest follow visual-system.md)
CARDS = {
    "bauhaus": dict(id="bauhaus", meaning="Primary colour, pure geometry and right angles: a school teaching that form follows function and every shape is a building block.", palette=["#F2EBDD", "#D8301F", "#F2B705", "#1B3B8F", "#111111"], bg=0, layers=[
        dict(mark="bar", arr="diagonal", n=(2, 4), size=(0.2, 0.5), col=[4], rot="snap"),
        dict(mark="circle", arr="thirds", n=(2, 4), size=(0.25, 0.55), col=[1, 2, 3]),
        dict(mark="square", arr="thirds", n=(2, 4), size=(0.15, 0.4), col=[1, 2, 3, 4], rot="snap"),
        dict(mark="quarter", arr="axis", n=(2, 4), size=(0.2, 0.45), col=[1, 3, 4], rot="snap"),
        dict(mark="tri", arr="scatter", n=(1, 3), size=(0.12, 0.3), col=[2, 4], rot="snap"),
    ]),
    "op_art": dict(id="op_art", meaning="Two tones and dense repetition that fool the eye: the picture is the vibration, not the object.", palette=["#F4F4F0", "#0A0A0A", "#E0301E"], bg=0, layers=[
        dict(arr="stripes", n=(30, 46), col=[1], line=7, freq=2.5, warp=5, amp=0.035),
        dict(arr="concentric", n=(18, 28), col=[1], line=4, mark="ring", blend=0),
        dict(arr="concentric", n=(7, 10), col=[2, 0], line=8, mark="ring", mix=0.9),
    ]),
    "swiss": dict(id="swiss", meaning="A grid, one accent colour and type as image: order and neutrality so the message leads.", palette=["#F5F5F2", "#111111", "#E4261A"], bg=0, layers=[
        dict(mark="rule", arr="bands", n=(3, 5), size=(0.4, 0.9), col=[1]),
        dict(mark="circle", arr="thirds", n=(1, 2), size=(0.3, 0.6), col=[2]),
        dict(text=["Aa", "No.", "07", "grid", "form"], arr="thirds", n=(2, 3), size=(0.1, 0.22), col=[1], tscale=0.9),
        dict(mark="square", arr="grid", n=(8, 14), size=(0.02, 0.05), col=[1]),
    ]),
}


def sheet(names, seeds, out, binary):
    os.makedirs(out, exist_ok=True)
    facts = u.load_facts()
    from PIL import Image
    tiles = []
    for nm in names:
        row = []
        for s in range(seeds):
            inf = os.path.join(out, "%s_%d.inf" % (nm, s))
            open(inf, "w").write(compile_card(CARDS[nm], s, facts))
            d = os.path.join(out, "%s_%d" % (nm, s)); os.makedirs(d, exist_ok=True)
            rc = subprocess.run([binary, "--frame", inf, "0", d + "/", "--size", "270x480"], capture_output=True, text=True, timeout=120)
            pngs = [f for f in os.listdir(d) if f.endswith(".png")]
            row.append(os.path.join(d, pngs[0]) if pngs else None)
        tiles.append(row)
    sh = Image.new("RGB", (270 * seeds, 480 * len(names)), (30, 30, 30))
    for y, row in enumerate(tiles):
        for x, p in enumerate(row):
            if p:
                sh.paste(Image.open(p).convert("RGB").resize((270, 480)), (x * 270, y * 480))
    sh.save(os.path.join(out, "sheet.png"))
    print("sheet ->", os.path.join(out, "sheet.png"))


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["sheet", "one"])
    ap.add_argument("cards")
    ap.add_argument("--seeds", type=int, default=8)
    ap.add_argument("--out", default="/tmp/uni/sheet")
    ap.add_argument("--bin", default=u.DEFAULT_BIN)
    a = ap.parse_args()
    sheet(a.cards.split(","), a.seeds, a.out, a.bin)
