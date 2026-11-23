"""Figures and generated tables for the stable-modulation research page and paper, from data/ only.

What: figures(data_dir, mode, data_url) returns {name: (body_html, caption_html)}. tools/research/build_lab_note.py
      puts each where note.md has <div data-fig="name"></div> and numbers it ("Figure n", or "Table n" for the
      names in TABLES). mode is "web" or "paper"; every figure here is static SVG, the same in both, with
      per-point tooltips on the web. numbers(data_dir) fills {{name}} in note.md, so prose and data cannot drift.
Why:  the research standard says every figure and table comes from data/ by a program, never by hand.
"""
import html, json, math, os

e = html.escape
TABLES = {"h2", "verdicts", "float32"}
W, H = 540, 300
L, R, T, B = 56, 136, 12, 40


def _j(d, f):
    return json.load(open(os.path.join(d, f), encoding="utf-8"))


def _svg(w, h, label, g):
    return f'<svg viewBox="0 0 {w} {h}" class="chart" role="img" aria-label="{e(label)}">' + "".join(g) + "</svg>"


def _fl(v):
    """A certified upper end, rounded down to 4 decimals (rounding up would claim more than was proved)."""
    return f"{math.floor(v * 1e4) / 1e4:.4f}"


def _link(data_url, f):
    return f'<a href="{data_url}/{f}">{e(f)}</a>'


def _rows(d):
    """Every scored kernel (L, G, B), with where its matrices came from: the Field interpreter or Python."""
    ok = {(r["set"], r["name"]) for f in ("phase2-compare.json", "phase4-B-compare.json")
          for r in _j(d, f) if r["status"] == "ok"}
    rows = [r for f in ("phase3-L-G.json", "phase4-B.json") for r in _j(d, f)["rows"]]
    for r in rows:
        r["field"] = (r["set"], r["name"]) in ok
    return rows


def trap(d, mode, data_url):
    """Section 2's trap: the Moog ladder at g = 1.3764, k switched every sample, against k frozen."""
    t = _j(d, "phase5-trap.json")
    ser = [("alternating 0, 2.89", "switched 0, 2.89", "var(--accent)"),
           ("frozen 2.89", "held at 2.89", "var(--ink-3)"),
           ("alternating 0, 2.85", "switched 0, 2.85", "var(--signal)")]
    lo, hi = -40, 10
    L = 72
    pw, ph = W - L - R, H - T - B
    sx = lambda i: L + pw * i / t["n"]
    sy = lambda v: T + ph * (hi - max(lo, min(hi, math.log10(max(v, 1e-300))))) / (hi - lo)
    g = [f'<line x1="{L}" y1="{T + ph}" x2="{L + pw}" y2="{T + ph}" class="ax"/>']
    for v in range(lo, hi + 1, 10):
        g.append(f'<line x1="{L}" y1="{sy(10.0 ** v):.1f}" x2="{L + pw}" y2="{sy(10.0 ** v):.1f}" class="grid"/>'
                 f'<text x="{L - 7}" y="{sy(10.0 ** v) + 4:.1f}" text-anchor="end">1e{v}</text>')
    g.append(f'<line x1="{L}" y1="{sy(1):.1f}" x2="{L + pw}" y2="{sy(1):.1f}" class="target"/>')
    for i in range(0, t["n"] + 1, 5000):
        g.append(f'<text x="{sx(i):.1f}" y="{T + ph + 17}" text-anchor="middle">{i:,}</text>')
    ends = []
    for name, lab, c in ser:
        ys = t["series"][name]
        path = " ".join(f"{'M' if j == 0 else 'L'}{sx(j * t['step']):.1f},{sy(v):.1f}" for j, v in enumerate(ys))
        g.append(f'<path d="{path}" class="ln" style="stroke:{c}"/>')
        ends.append([sy(ys[-1]) + 4, lab, c])
    ends.sort()
    for i in range(1, len(ends)):
        ends[i][0] = max(ends[i][0], ends[i - 1][0] + 14)
    g += [f'<text x="{L + pw + 8}" y="{y:.1f}" style="fill:{c}" class="sl">{e(n)}</text>' for y, n, c in ends]
    g.append(f'<text x="{L + pw / 2}" y="{H - 3}" text-anchor="middle" class="axl">sample</text>'
             f'<text x="12" y="{T + ph / 2}" text-anchor="middle" class="axl" transform="rotate(-90 12 {T + ph / 2})">state norm ‖x‖ (log)</text>')
    end = {n: t["series"][n][-1] for n, _, _ in ser}
    cap = (f"The trap. The dashed line is the start, ‖x‖ = 1. A linear Moog ladder at cutoff g = {t['g']} (McClellan's counterexample) from a unit start. "
           f"With the resonance k switched between 0 and 2.89 every sample, the state grows to {end['alternating 0, 2.89']:,.0f} "
           f"in {t['n']:,} samples, though the filter is stable at every fixed k up to 4: held at 2.89 it decays. "
           "Switched between 0 and 2.85, inside the range the certifier proves, it cannot grow "
           f"(‖x‖ = {end['alternating 0, 2.85']:.0e} at the end). Data: {_link(data_url, 'phase5-trap.json')}.")
    return _svg(W, H, "State norm of a Moog ladder under switched and frozen resonance", g), cap


def h2(d, mode, data_url):
    """H2: the certifier against the four hand proofs."""
    h = _j(d, "phase1-h2.json")
    b3 = _j(d, "phase0-b3.json")["oracle"]
    rows = [("Moog ladder", "k ∈ [0, 2.88]", "≥ 2.85", f"k ∈ [0, {_fl(h['moog']['certified_kmax'])}]", f"{b3['moog']['smallest_unstable_kmax']:.4f}", h["moog"]["pass"]),
            ("Diode ladder", "k ∈ [0, 8.90]", "≥ 8.81", f"k ∈ [0, {_fl(h['diode']['certified_kmax'])}]", f"{b3['diode']['smallest_unstable_kmax']:.4f}", h["diode"]["pass"]),
            ("Sallen-Key", "k < 2", "≥ 1.98", f"k ∈ [0, {_fl(h['sallenkey']['certified_kmax'])}]", "2 (frozen)", h["sallenkey"]["pass"]),
            ("SVF", "any R ≥ R<sub>min</sub> &gt; 0", "R ∈ [R<sub>min</sub>, 10], R<sub>min</sub> = 0.01, 0.1", "both boxes", "—", h["svf"]["pass"])]
    head = "<tr><th>filter</th><th>paper, by hand</th><th>bar</th><th>certified automatically</th><th>oracle: unstable from</th><th>pass</th></tr>"
    body = "".join(f"<tr><td>{a}</td><td>{b}</td><td>{c}</td><td><b>{x}</b></td><td>{o}</td><td>{'yes' if p else 'no'}</td></tr>"
                   for a, b, c, x, o, p in rows)
    table = f'<div class="table-wrap"><table><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("H2: the certifier against McClellan's four hand proofs, the cutoff over g ∈ [1e-5, 1e3] and k bisected to 0.001. "
           "“Oracle: unstable from” is the smallest upper end of k at which the instability oracle finds a witness. "
           f"Data: {_link(data_url, 'phase1-h2.json')}, {_link(data_url, 'phase0-b3.json')}.")
    return table, cap


def _groups(rows):
    out = []
    for lab, f in (("L: literature filters", lambda r: r["set"] == "L"),
                   ("G, Field matrices", lambda r: r["set"] == "G" and r["field"]),
                   ("G, Python matrices", lambda r: r["set"] == "G" and not r["field"]),
                   ("B (blind), Field matrices", lambda r: r["set"] == "B" and r["field"]),
                   ("B (blind), Python matrices", lambda r: r["set"] == "B" and not r["field"])):
        g = [r for r in rows if f(r)]
        nw = [r for r in g if not r["witness"]]
        out.append((lab, len(g), len(g) - len(nw), len(nw), sum(r["status"] == "certified" for r in nw),
                    sum(r["status"] == "certified" for r in g if r["witness"])))
    return out


def verdicts(d, mode, data_url):
    rows = _rows(d)
    head = ("<tr><th>set</th><th>kernels</th><th>with a witness</th><th>certified with a witness (H1)</th>"
            "<th>no witness</th><th>certified (H3)</th><th>share</th></tr>")
    body = "".join(f"<tr><td>{e(a)}</td><td>{n}</td><td>{w}</td><td><b>{fw}</b></td><td>{nw}</td><td>{c}</td>"
                   f"<td>{100 * c / nw:.1f}%</td></tr>" for a, n, w, nw, c, fw in _groups(rows))
    for s in ("G", "B"):
        g = [r for r in rows if r["set"] == s]
        nw = [r for r in g if not r["witness"]]
        c = sum(r["status"] == "certified" for r in nw)
        body += (f"<tr class='rn-sum'><td>all of {s}</td><td>{len(g)}</td><td>{len(g) - len(nw)}</td>"
                 f"<td>{sum(r['status'] == 'certified' for r in g if r['witness'])}</td><td>{len(nw)}</td><td>{c}</td><td>{100 * c / len(nw):.1f}%</td></tr>")
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("H1 and H3 on every scored kernel. A witness is a product of up to 8 state matrices with spectral radius above "
           "1 + 1e-6: a proof that the kernel can blow up. H1 counts certificates on such kernels (bar: 0). H3 is the share of "
           "kernels without a witness that are certified (bar: 80%, on G and then on the blind set B). “Field matrices” were "
           "measured on Infinite's interpreter; kernels over Field's 128-register cap use the Python reference. "
           f"Data: {_link(data_url, 'phase3-L-G.json')}, {_link(data_url, 'phase4-B.json')}.")
    return table, cap


def frozen(d, mode, data_url):
    """How often the frozen check calls a kernel that can blow up safe, against the certifier."""
    f = _j(d, "phase5-frozen.json")
    bars = [("default knobs (today)", "B1a", "var(--ink-3)"), ("every point of a 7-per-knob grid", "B1b", "var(--signal)"),
            ("the certifier", "certified", "var(--accent)")]
    lw, pw, bh, gap, top = 250, 230, 18, 16, 8
    h = top + 2 * (3 * bh + gap + 20) + 34
    sx = lambda v: lw + pw * v / 100
    g = []
    for v in (0, 25, 50, 75, 100):
        g.append(f'<line x1="{sx(v):.1f}" y1="{top}" x2="{sx(v):.1f}" y2="{h - 30}" class="grid"/>'
                 f'<text x="{sx(v):.1f}" y="{h - 14}" text-anchor="middle">{v}%</text>')
    y = top
    for s, lab in (("G", "set G"), ("B", "set B (blind)")):
        n = f[s]["with_witness"]
        g.append(f'<text x="4" y="{y + 13}" class="sl">{e(lab)}: {n} kernels that can blow up</text>')
        y += 20
        for name, k, c in bars:
            v = f[s][k]
            g.append(f'<text x="{lw - 7}" y="{y + 12}" text-anchor="end">{e(name)}</text>'
                     f'<rect x="{lw}" y="{y + 2}" width="{max(1.5, sx(100 * v / n) - lw):.1f}" height="{bh - 5}" style="fill:{c}"/>'
                     f'<text x="{sx(100 * v / n) + 5:.1f}" y="{y + 12}" class="bv">{v} of {n}</text>')
            y += bh
        y += gap
    g.append(f'<text x="{lw + pw / 2}" y="{h - 1}" text-anchor="middle" class="axl">kernels with a witness called safe</text>')
    cap = ("Kernels that can blow up (they have a witness) but that a frozen-knob check calls stable. “Default knobs” is "
           "what Infinite's analysis does today: spectral radius below 1 at one setting. The grid check is far stricter than "
           f"today's and still misses {f['G']['B1b']} of {f['G']['with_witness']} on G and {f['B']['B1b']} of {f['B']['with_witness']} on B, "
           "because their witnesses need the knobs to move. The certifier calls none of them safe. "
           f"Data: {_link(data_url, 'phase5-frozen.json')}.")
    return _svg(lw + pw + 70, h, "Unstable kernels called safe by two frozen checks and by the certifier", g), cap


def growth(d, mode, data_url):
    """Every G and B kernel by its worst growth per sample, split by verdict: the picture of H1 and H3."""
    rows = [r for r in _rows(d) if r["set"] in ("G", "B")]
    cats = [("certified", lambda r: r["status"] == "certified", "var(--signal)"),
            ("no proof, no witness", lambda r: r["status"] != "certified" and not r["witness"], "var(--ink-2)"),
            ("no proof, witness", lambda r: r["status"] != "certified" and r["witness"], "var(--accent)")]
    lo, hi = 0.90, 1.15
    lw, pw, rh, top = 170, 320, 46, 10
    h = top + rh * len(cats) + 40
    sx = lambda v: lw + pw * (min(hi, max(lo, v)) - lo) / (hi - lo)
    g = [f'<line x1="{sx(1):.1f}" y1="{top - 4}" x2="{sx(1):.1f}" y2="{h - 34}" class="target"/>'
         f'<text x="{sx(1) + 4:.1f}" y="{top + 6}" class="tl">1 (no growth)</text>']
    for v in (0.90, 0.95, 1.00, 1.05, 1.10, 1.15):
        g.append(f'<line x1="{sx(v):.1f}" y1="{top}" x2="{sx(v):.1f}" y2="{h - 34}" class="grid"/>'
                 f'<text x="{sx(v):.1f}" y="{h - 18}" text-anchor="middle">{v:.2f}</text>')
    for i, (lab, f, c) in enumerate(cats):
        y0 = top + i * rh
        sel = [r for r in rows if f(r)]
        over = sum(r["growth_per_step"] > hi for r in sel)
        g.append(f'<text x="{lw - 10}" y="{y0 + rh / 2 + 1:.1f}" text-anchor="end" class="sl" style="fill:{c}">{e(lab)}</text>'
                 f'<text x="{lw - 10}" y="{y0 + rh / 2 + 15:.1f}" text-anchor="end" class="axl">{len(sel)} kernels</text>')
        for j, r in enumerate(sorted(sel, key=lambda r: r["name"])):
            jit = ((j * 0.618034) % 1 - 0.5) * (rh - 16)
            g.append(f'<circle cx="{sx(r["growth_per_step"]):.1f}" cy="{y0 + rh / 2 + jit:.1f}" r="2.6" '
                     f'style="fill:{c};fill-opacity:0.7"><title>{e(r["name"])}, n = {r["n"]}: {r["growth_per_step"]:.5f} per sample, '
                     f'{"Field" if r["field"] else "Python"} matrices</title></circle>')
        if over:
            g.append(f'<text x="{sx(hi) + 6:.1f}" y="{y0 + rh / 2 + 4:.1f}" class="axl">+{over} beyond {hi}</text>')
    g.append(f'<text x="{lw + pw / 2}" y="{h - 2}" text-anchor="middle" class="axl">worst growth per sample the oracle found</text>')
    cap = ("Every G and B kernel at the worst growth per sample the instability oracle found (the spectral radius of its "
           "worst switching product, per step). Right of 1, the kernel can blow up. No certified kernel sits there (H1). The "
           "grey row is what H3 misses: kernels with no witness and no proof, all close to 1. Points beyond 1.15 are "
           f"counted at the edge. On the web, hover a point for its kernel. Data: {_link(data_url, 'phase3-L-G.json')}, "
           f"{_link(data_url, 'phase4-B.json')}.")
    return _svg(lw + pw + 125, h, "Kernels by worst growth per sample, split by certifier verdict", g), cap


def float32(d, mode, data_url):
    rows = [r for r in _rows(d) if r["status"] == "certified"]
    head = "<tr><th>certified kernels</th><th>count</th><th>holds at every check point</th><th>proved in Float32</th><th>largest rise over float64</th></tr>"
    body = ""
    for lab, f in (("L, Field", lambda r: r["set"] == "L"),
                   ("G, Field", lambda r: r["set"] == "G" and r["field"]), ("G, Python (bound)", lambda r: r["set"] == "G" and not r["field"]),
                   ("B, Field", lambda r: r["set"] == "B" and r["field"]), ("B, Python (bound)", lambda r: r["set"] == "B" and not r["field"])):
        g = [r for r in rows if f(r)]
        meas = [r for r in g if r["f32"].get("source") == "field"]
        proved = sum(r["f32"]["holds"] for r in g)
        rise = max(r["f32"]["max_rise"] for r in g)
        body += (f"<tr><td>{e(lab)}</td><td>{len(g)}</td><td>{sum(r['f32'].get('max_norm', 0) < 1 for r in meas) if meas else 'n/a'}</td>"
                 f"<td>{proved}</td><td>{rise:.1e}{'' if meas else ' (bound)'}</td></tr>")
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("Does each certificate survive the interpreter's Float32 arithmetic? At up to 5000 check points per certificate "
           "(every LMI point and the cover points with the largest P-norm) the interpreter's own matrix was measured. "
           "“Holds”: every ‖A<sub>f32</sub>‖<sub>P</sub> &lt; 1. “Proved”: the largest rise over float64 is also below the "
           "cover's slack (1 − worst cell bound). Kernels over the register cap can't run on the interpreter, so a bound "
           "(√κ · n · 10 × the measured Float32 error) stands in for the measurement. "
           f"Data: {_link(data_url, 'phase3-L-G.json')}, {_link(data_url, 'phase4-B.json')}.")
    return table, cap


def h4(d, mode, data_url):
    """H4: the certified k range with a slew limit, against the any-speed range and the frozen limit."""
    h = _j(d, "phase3-h4.json")
    reg, exp = h["registered_g_lo_1e-5"], h["exploratory_g_lo_20Hz"]
    lw, pw, bh, gap, top = 290, 220, 18, 18, 8
    h_ = top + 2 * (5 * bh + gap) + 34
    sc = {"moog": 4.2, "diode": 19.5}
    g = []
    y = top
    for name, lab in (("moog", "Moog ladder"), ("diode", "Diode ladder")):
        r, x = reg[name], exp[name]
        sx = lambda v: lw + pw * v / sc[name]
        bars = [("any speed (H2)", r["any_speed_kmax"], "var(--signal)", ""),
                ("slew, cutoff to 0.15 Hz (registered)", r["slew_kmax"], "var(--accent)",
                 f"+{r['wider_pct']:.0f}%" if r["slew_kmax"] else f"no proof even at {r['any_speed_kmax']:.2f}"),
                ("slew, cutoff ≥ 20 Hz (exploratory)", x["slew_kmax"], "var(--cat-modulators)", f"+{x['wider_pct']:.0f}%"),
                ("frozen knobs (no modulation)", r["lti_limit"], "var(--ink-3)", "")]
        g.append(f'<text x="{lw - 7}" y="{y + 12}" text-anchor="end" class="sl">{e(lab)}</text>')
        y += bh
        for blab, v, c, note in bars:
            g.append(f'<text x="{lw - 7}" y="{y + 12}" text-anchor="end">{e(blab)}</text>'
                     + (f'<rect x="{lw}" y="{y + 2}" width="{sx(v) - lw:.1f}" height="{bh - 5}" style="fill:{c}"/>'
                        f'<text x="{sx(v) + 5:.1f}" y="{y + 12}" class="bv">{v:.2f}{" " + note if note else ""}</text>' if v else
                        f'<text x="{lw + 5}" y="{y + 12}" class="bv">{e(note)}</text>'))
            y += bh
        y += gap
    g.append(f'<text x="{(lw + pw + 110) / 2}" y="{h_ - 8}" text-anchor="middle" class="axl">largest certified resonance k (each filter on its own scale)</text>')
    cap = ("H4: the largest resonance k the certifier proves when k may move by at most its full range in 20 ms "
           "(Δk ≤ range / 960 per sample), against the any-speed range and the frozen limit. With the cutoff free down to "
           "0.15 Hz, as registered, the slew limit adds nothing (the registered bar was +25% on both filters; the diode's slew "
           "LMI fails even at its any-speed 8.90). With the cutoff held at 20 Hz or above it does, but that run was not registered. "
           f"Data: {_link(data_url, 'phase3-h4.json')}.")
    return _svg(lw + pw + 110, h_, "Certified resonance range with and without a slew limit", g), cap


def figures(data_dir, mode="web", data_url="data"):
    return {name: fn(data_dir, mode, data_url) for name, fn in
            (("trap", trap), ("h2", h2), ("frozen", frozen), ("verdicts", verdicts), ("growth", growth),
             ("float32", float32), ("h4", h4))}


def numbers(data_dir):
    """Values the text quotes, so prose and data cannot drift: {{name}} in note.md."""
    rows = _rows(data_dir)
    f = _j(data_dir, "phase5-frozen.json")
    h = _j(data_dir, "phase1-h2.json")
    hh = _j(data_dir, "phase3-h4.json")
    b3 = _j(data_dir, "phase0-b3.json")["oracle"]
    out = {}
    wit = [r for r in rows if r["witness"]]
    out["n_kernels"] = str(len(rows))
    out["n_witness"] = str(len(wit))
    out["false_certs"] = str(sum(r["status"] == "certified" for r in wit))
    for s in ("G", "B"):
        nw = [r for r in rows if r["set"] == s and not r["witness"]]
        c = sum(r["status"] == "certified" for r in nw)
        out[f"h3_{s}"] = f"{100 * c / len(nw):.1f}%"
        out[f"h3_{s}_frac"] = f"{c} of {len(nw)}"
        out[f"b1a_{s}"] = f"{f[s]['B1a']} of {f[s]['with_witness']}"
        out[f"b1b_{s}"] = f"{f[s]['B1b']} of {f[s]['with_witness']}"
        out[f"wit_{s}"] = str(f[s]["with_witness"])
    out["moog_cert"] = _fl(h["moog"]["certified_kmax"])
    out["diode_cert"] = f"{_fl(h['diode']['certified_kmax'])}"
    out["moog_unstable"] = f"{b3['moog']['smallest_unstable_kmax']:.4f}"
    out["diode_unstable"] = f"{b3['diode']['smallest_unstable_kmax']:.4f}"
    out["moog_slew20"] = f"{hh['exploratory_g_lo_20Hz']['moog']['slew_kmax']:.2f}"
    out["diode_slew20"] = f"{hh['exploratory_g_lo_20Hz']['diode']['slew_kmax']:.2f}"
    out["moog_slew20_pct"] = f"{hh['exploratory_g_lo_20Hz']['moog']['wider_pct']:.0f}%"
    out["diode_slew20_pct"] = f"{hh['exploratory_g_lo_20Hz']['diode']['wider_pct']:.0f}%"
    ts = sorted(r["certify_s"] for r in rows if r["set"] in ("G", "B"))
    out["t_median"] = f"{ts[len(ts) // 2]:.2f} s"
    out["t_p95"] = f"{ts[int(0.95 * len(ts))]:.1f} s"
    out["n_python"] = str(sum(not r["field"] for r in rows))
    return out
