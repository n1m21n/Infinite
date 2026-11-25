"""Figures and generated tables for the saturating-loops research page and paper, from data/ only.

What: figures(data_dir, mode, data_url) returns {name: (body_html, caption_html)}; numbers(data_dir) fills
      {{name}} in note.md. Same contract as Report 2's web/figures.py (tools/research/build_lab_note.py).
Why:  the research standard says every figure and table comes from data/ by a program, never by hand.
"""
import html, json, math, os

e = html.escape
TABLES = {"verdicts", "float32", "field"}
W, H = 540, 300
L, R, T, B = 64, 150, 12, 40
SETS = (("G", "phase3-G.json", "phase2-compare.json"), ("B", "phase3-B.json", "phase4-B-compare.json"))


def _j(d, f):
    return json.load(open(os.path.join(d, f), encoding="utf-8"))


def _svg(w, h, label, g):
    return f'<svg viewBox="0 0 {w} {h}" class="chart" role="img" aria-label="{e(label)}">' + "".join(g) + "</svg>"


def _link(data_url, f):
    return f'<a href="{data_url}/{f}">{e(f)}</a>'


def _fl(v, k=3):
    """A certified upper end, rounded down (rounding up would claim more than was proved)."""
    return f"{math.floor(v * 10 ** k) / 10 ** k:.{k}f}"


def _set(d, s):
    return list(_j(d, dict((a, b) for a, b, _ in SETS)[s]).values())


def _stats(rows):
    wit = [r for r in rows if r["witness"]]
    nw = [r for r in rows if not r["witness"]]
    g1 = [r for r in rows if r["G1"] == "certified"]
    return {
        "n": len(rows), "wit": len(wit), "nowit": len(nw),
        "b1a": sum(r["B1a_radius"] < 1 for r in wit), "b1b": sum(r["B1b_radius"] < 1 for r in wit),
        "b4_false": sum(r["B4"]["status"] == "certified" for r in wit),
        "g1_false": sum(r["G1"] == "certified" for r in wit),
        "g1_nowit": sum(r["G1"] == "certified" for r in nw), "g1": len(g1),
        "g2": sum(r.get("G2") == "certified" for r in g1),
        "g2_own": sum(r.get("G2") == "certified" and not r["T4"]["reused_T1"] for r in g1),
        "two_fail": sum(r["two_start"]["fail"] for r in g1),
        "field": sum(r["phase2"].get("pass", False) for r in rows),
        "f32_hold": sum(r.get("float32", {}).get("holds") is True for r in g1),
        "f32_hold_field": sum(r.get("float32", {}).get("holds") is True and not r["float32"]["from_python_only"] for r in g1),
        "g1_field": sum(r["phase2"].get("pass", False) for r in g1),
        "grows": sum(r["witness"]["kind"] == "grows" for r in wit), "sticks": sum(r["witness"]["kind"] == "sticks" for r in wit),
    }


def _lit(d):
    out = []
    for r in _j(d, "phase0-L.json").values():
        t3 = r["T3"]["status"] if isinstance(r["T3"], dict) else r["T3"]
        out.append({"name": r["name"], "witness": bool(r["witness"]), "frozen": r["frozen_check"], "B4": r["B4_linear"],
                    "T1": r["T1"], "T3": t3, "G1": "certified" if "certified" in (r["T1"], t3) else "no proof"})
    return out


def windup(d, mode, data_url):
    """Section 4.1: linearise-and-hope certifies a kernel that winds up and blows up."""
    w = _j(d, "phase5-windup.json")
    k = w["kernels"]["s122-067"]
    lo, hi, NS = -20, 20, 6000
    pw, ph = W - L - R, H - T - B
    sx = lambda i: L + pw * i / NS
    sy = lambda v: T + ph * (hi - max(lo, min(hi, math.log10(max(v, 1e-300))))) / (hi - lo)
    g = [f'<line x1="{L}" y1="{T + ph}" x2="{L + pw}" y2="{T + ph}" class="ax"/>']
    for v in range(lo, hi + 1, 10):
        g.append(f'<line x1="{L}" y1="{sy(10.0 ** v):.1f}" x2="{L + pw}" y2="{sy(10.0 ** v):.1f}" class="grid"/>'
                 f'<text x="{L - 7}" y="{sy(10.0 ** v) + 4:.1f}" text-anchor="end">1e{v}</text>')
    for i in range(0, NS + 1, 2000):
        g.append(f'<text x="{sx(i):.1f}" y="{T + ph + 17}" text-anchor="middle">{i:,}</text>')
    ser = [("start 100", "var(--accent)"), ("start 10", "var(--signal)"), ("start 1", "var(--ink-3)"),
           ("linearised, start 100", "var(--ink-2)")]
    for name, c in ser:
        ys = k["series"][name][: NS // w["step"] + 1]
        dash = ' stroke-dasharray="4 3"' if name.startswith("linear") else ""
        path = " ".join(f"{'M' if j == 0 else 'L'}{sx(j * w['step']):.1f},{sy(v):.1f}" for j, v in enumerate(ys))
        g.append(f'<path d="{path}" class="ln" style="stroke:{c}"{dash}><title>{e(name)}</title></path>')
    g.append(f'<text x="{L + pw + 8}" y="{sy(k["series"]["start 100"][NS // w["step"]]) + 4:.1f}" style="fill:var(--accent)" class="sl">tanh, start 100</text>')
    g += [f'<text x="{L + pw + 8}" y="{T + ph - 30 + 14 * i:.1f}" style="fill:{c}" class="sl">{e(n)}</text>'
          for i, (n, c) in enumerate((("tanh, start 1, 10", "var(--signal)"), ("linearised, start 100", "var(--ink-2)"), ("all decay to 0", "var(--ink-3)")))]
    g.append(f'<text x="{L + pw / 2}" y="{H - 3}" text-anchor="middle" class="axl">sample</text>'
             f'<text x="12" y="{T + ph / 2}" text-anchor="middle" class="axl" transform="rotate(-90 12 {T + ph / 2})">state norm ‖x‖ (log)</text>')
    k2 = w["kernels"]["s122-124"]
    cap = (f"Windup. Generated kernel s122-067, knobs fixed at its witness setting, no input. Its tanh sits on a path that keeps the "
           f"loop stable: small signals see slope 1 (radius {k['radius_small_signal']:.3f}), large signals see slope near 0, and the loop "
           f"that is left has radius {k['radius_cut']:.4f}. Linearise-and-hope (the saturator replaced by its slope at 0, dashed) "
           f"certifies it. Starts of 1 and 10 decay; a start of 100 grows without bound, to {k['series']['start 100'][-1]:.1e} by sample {w['n']:,} (first {NS:,} samples shown). Kernel s122-124, with a "
           f"clamp, fails the same way (radius {k2['radius_small_signal']:.3f} small, {k2['radius_cut']:.4f} cut). Data: {_link(data_url, 'phase5-windup.json')}.")
    return _svg(W, H, "State norm of a saturating kernel that linearise-and-hope calls safe", g), cap


def moog(d, mode, data_url):
    """Section 4.2: the tanh Moog, T3's certified range against the explicit filter's own limits and Hélie's 5/3."""
    t3 = {r["gmax"]: r["t3_certified_k"] for r in _j(d, "phase0-t3.json")["rows"]}
    mg = {r["gmax"]: r for r in _j(d, "phase0-moog.json")["rows"]}
    b3 = {r["gmax"]: r["oracle_first_witness_k"] for r in _j(d, "phase0-b3.json")["rows"]}
    gs = sorted(t3)
    x0, x1, y0, y1 = math.log10(0.04), math.log10(1.0), 0.0, 4.0
    pw, ph = W - L - R, H - T - B
    sx = lambda v: L + pw * (math.log10(v) - x0) / (x1 - x0)
    sy = lambda v: T + ph * (y1 - v) / (y1 - y0)
    g = [f'<line x1="{L}" y1="{T + ph}" x2="{L + pw}" y2="{T + ph}" class="ax"/>']
    for v in range(0, 5):
        g.append(f'<line x1="{L}" y1="{sy(v):.1f}" x2="{L + pw}" y2="{sy(v):.1f}" class="grid"/>'
                 f'<text x="{L - 7}" y="{sy(v) + 4:.1f}" text-anchor="end">{v}</text>')
    for v in (0.05, 0.1, 0.2, 0.4, 0.785):
        g.append(f'<text x="{sx(v):.1f}" y="{T + ph + 17}" text-anchor="middle">{v:g}</text>')
    g.append(f'<line x1="{L}" y1="{sy(5 / 3):.1f}" x2="{L + pw}" y2="{sy(5 / 3):.1f}" class="target"/>')
    ser = [("frozen limit", [mg[x]["frozen_limit_k_at_gmax"] for x in gs], "var(--ink-3)"),
           ("first witness", [b3[x] for x in gs], "var(--accent)"),
           ("linear, common P", [mg[x]["linear_cqlf_certified_k"] for x in gs], "var(--ink-2)"),
           ("tanh, T3 certified", [t3[x] for x in gs], "var(--signal)")]
    ends = []
    for lab, ys, c in ser:
        path = " ".join(f"{'M' if j == 0 else 'L'}{sx(x):.1f},{sy(v):.1f}" for j, (x, v) in enumerate(zip(gs, ys)))
        g.append(f'<path d="{path}" class="ln" style="stroke:{c}"/>')
        g += [f'<circle cx="{sx(x):.1f}" cy="{sy(v):.1f}" r="3" style="fill:{c}"><title>{e(lab)}: g ≤ {x:g}, k = {v:.3f}</title></circle>'
              for x, v in zip(gs, ys)]
        ends.append([sy(ys[-1]) + 4, lab, c])
    ends.append([sy(5 / 3) - 4, "Hélie, 5/3", "var(--ink-3)"])
    ends.sort()
    for i in range(1, len(ends)):
        ends[i][0] = max(ends[i][0], ends[i - 1][0] + 14)
    g += [f'<text x="{L + pw + 8}" y="{y:.1f}" style="fill:{c}" class="sl">{e(n)}</text>' for y, n, c in ends]
    g.append(f'<text x="{L + pw / 2}" y="{H - 3}" text-anchor="middle" class="axl">cutoff ceiling g (log)</text>'
             f'<text x="12" y="{T + ph / 2}" text-anchor="middle" class="axl" transform="rotate(-90 12 {T + ph / 2})">resonance k</text>')
    c = _j(d, "phase0-t3.json")["largest_ceiling_certifying_5_3"]
    cap = (f"H2. The explicit tanh Moog ladder (a saturator per stage), cutoff from 1e-4 up to each ceiling, resonance from 0. "
           f"T3 certifies the range below its line under any modulation; no witness exists there. Above the first-witness line "
           f"the filter self-oscillates; the frozen limit lies within 0.002 of it, under it on the plot. Hélie's hand proof (continuous time) gives k &lt; 5/3 under modulation; the explicit "
           f"filter's own limit drops below 5/3 as the cutoff rises, so 5/3 is certified only for ceilings up to g = {c:g}. "
           f"Data: {_link(data_url, 'phase0-t3.json')}, {_link(data_url, 'phase0-moog.json')}, {_link(data_url, 'phase0-b3.json')}.")
    return _svg(W, H, "Certified resonance range of the tanh Moog ladder against cutoff ceiling", g), cap


def verdicts(d, mode, data_url):
    lit = _lit(d)
    lw = [r for r in lit if r["witness"]]
    ln = [r for r in lit if not r["witness"]]
    rows = [("L, literature", len(lit), len(lw), "—", sum(r["frozen"] == "safe" for r in lw),
             sum(r["B4"] == "certified" for r in lw), sum(r["G1"] == "certified" for r in lw), len(ln),
             sum(r["G1"] == "certified" for r in ln))]
    for s in ("G", "B"):
        st = _stats(_set(d, s))
        rows.append((f"{s}{', blind' if s == 'B' else ''}", st["n"], st["wit"], st["b1a"], st["b1b"], st["b4_false"],
                     st["g1_false"], st["nowit"], st["g1_nowit"]))
    head = ("<tr><th>set</th><th>kernels</th><th>with a witness</th><th>frozen check says safe, default knobs</th>"
            "<th>… on a full grid</th><th>linearise-and-hope certifies</th><th>G1 certifies (H1)</th>"
            "<th>no witness</th><th>G1 certifies (H3)</th></tr>")
    body = "".join(f"<tr><td>{e(a)}</td><td>{n}</td><td>{w}</td><td>{f1}</td><td>{f2}</td><td>{b4}</td><td><b>{g}</b></td>"
                   f"<td>{nw}</td><td><b>{c}</b> ({100 * c / nw:.1f}%)</td></tr>" for a, n, w, f1, f2, b4, g, nw, c in rows)
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("Every scored kernel. The first four columns after “with a witness” count kernels that can blow up or stick: each "
           "number there is a wrong “safe”, except G1's, which is H1 (bar: 0). The last column is H3 (bar: 70%, on G and then on "
           "the blind set B). G1 is T1 (sector vertices) or T3 (Lur'e-Lyapunov); every G and B certificate came from T1. "
           "L is calibration; its frozen check was run on a grid only. "
           f"Data: {_link(data_url, 'phase0-L.json')}, {_link(data_url, 'phase3-G.json')}, {_link(data_url, 'phase3-B.json')}.")
    return table, cap


def field(d, mode, data_url):
    c2 = _j(d, "phase2-compare.json")
    c4 = _j(d, "phase4-B-compare.json")
    rows = []
    for s, src in (("L", c2), ("G", c2), ("B", c4)):
        g = [v for k, v in src.items() if k.startswith(s + "/")]
        ok = [v for v in g if v.get("status") == "ok"]
        rows.append((s, len(g), len(ok), sum(v["pass"] for v in ok), max(v["max_abs"] for v in ok), len(g) - len(ok)))
    head = "<tr><th>set</th><th>kernels</th><th>compiled and dumped</th><th>match Python</th><th>largest error</th><th>over the register cap</th></tr>"
    body = "".join(f"<tr><td>{s}</td><td>{n}</td><td>{k}</td><td><b>{p}</b></td><td>{m:.1e}</td><td>{x}</td></tr>" for s, n, k, p, m, x in rows)
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("The Field side. Each kernel was compiled by Infinite's interpreter, its saturators found in the IR and cut, and A, B, C, D "
           "dumped at every grid point. “Match” means the same saturator shapes, D = 0 and every entry within 1e-5 of the Python "
           "reference. L was written as Field from the published equations, not from the Python matrices. "
           f"Data: {_link(data_url, 'phase2-compare.json')}, {_link(data_url, 'phase4-B-compare.json')}.")
    return table, cap


def float32(d, mode, data_url):
    head = ("<tr><th>set</th><th>G1 certificates</th><th>matrices from Field</th><th>bound below the slack, Field</th>"
            "<th>bound below the slack, all</th></tr>")
    body = ""
    for s in ("G", "B"):
        st = _stats(_set(d, s))
        body += (f"<tr><td>{s}</td><td>{st['g1']}</td><td>{st['g1_field']}</td><td>{st['f32_hold_field']}</td>"
                 f"<td><b>{st['f32_hold']}</b></td></tr>")
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}</tbody></table></div>'
    cap = ("Float32. An entry error ε in A, B, C moves each vertex matrix by at most ε(1 + m·b(|B| + |C|) + m·b·ε) per entry, so the "
           "P-norm moves by at most √κ(P)·n times that; ten times it must fit inside the cover's slack (1 − worst cell bound). "
           "ε is the kernel's measured Field error, or the set's largest for kernels over the register cap. A failure means "
           "“not proved in Float32”, not unstable. "
           f"Data: {_link(data_url, 'phase3-G.json')}, {_link(data_url, 'phase3-B.json')}.")
    return table, cap


def figures(data_dir, mode="web", data_url="data"):
    return {name: fn(data_dir, mode, data_url) for name, fn in
            (("windup", windup), ("moog", moog), ("verdicts", verdicts), ("field", field), ("float32", float32))}


def numbers(data_dir):
    """Values the text quotes, so prose and data cannot drift: {{name}} in note.md."""
    out = {}
    lit = _lit(data_dir)
    out["n_L"] = str(len(lit))
    out["wit_L"] = str(sum(r["witness"] for r in lit))
    tot_k = len(lit)
    tot_w = sum(r["witness"] for r in lit)
    tot_f = sum(r["witness"] and r["G1"] == "certified" for r in lit)
    for s in ("G", "B"):
        rows = _set(data_dir, s)
        st = _stats(rows)
        tot_k += st["n"]; tot_w += st["wit"]; tot_f += st["g1_false"]
        out[f"wit_{s}"] = str(st["wit"])
        out[f"grows_{s}"] = str(st["grows"]); out[f"sticks_{s}"] = str(st["sticks"])
        out[f"h3_{s}"] = f"{100 * st['g1_nowit'] / st['nowit']:.1f}%"
        out[f"h3_{s}_frac"] = f"{st['g1_nowit']} of {st['nowit']}"
        out[f"h4_{s}_frac"] = f"{st['g2']} of {st['g1']}"
        out[f"g2own_{s}"] = str(st["g2_own"])
        out[f"two_fail_{s}"] = str(st["two_fail"])
        out[f"b1a_{s}"] = f"{st['b1a']} of {st['wit']}"
        out[f"b1b_{s}"] = f"{st['b1b']} of {st['wit']}"
        out[f"b4_{s}"] = str(st["b4_false"])
        out[f"field_{s}"] = str(st["field"])
        out[f"over_{s}"] = str(st["n"] - st["field"])
        out[f"f32_{s}"] = f"{st['f32_hold']} of {st['g1']}"
        out[f"n_{s}"] = str(st["n"])
        ts = sorted(r["T1"].get("s", 0) for r in rows if r["T1"].get("s") is not None)
        out[f"t_median_{s}"] = f"{ts[len(ts) // 2]:.2f} s"
        out[f"t_p95_{s}"] = f"{ts[int(0.95 * len(ts))]:.1f} s"
    out["n_kernels"] = str(tot_k)
    out["n_witness"] = str(tot_w)
    out["false_certs"] = str(tot_f)
    t3 = _j(data_dir, "phase0-t3.json")
    rows = {r["gmax"]: r["t3_certified_k"] for r in t3["rows"]}
    b3 = {r["gmax"]: r["oracle_first_witness_k"] for r in _j(data_dir, "phase0-b3.json")["rows"]}
    out["moog_t3_full"] = _fl(_j(data_dir, "phase0-L.json")["moog-tanh"]["T3_ceiling"]["value"], 4)
    out["moog_t3_low"] = _fl(rows[0.05])
    out["moog_t3_row"] = _fl(rows[0.785])
    out["moog_wit_full"] = f"{b3[0.785]:.3f}"
    out["moog_wit_low"] = f"{b3[0.05]:.3f}"
    out["helie_ceiling"] = f"{t3['largest_ceiling_certifying_5_3']:g}"
    fs = [r["float32"]["slack"] for s in ("G", "B") for r in _set(data_dir, s)
          if r["G1"] == "certified" and r.get("float32", {}).get("holds") is False]
    out["f32_fail_slack"] = f"{max(fs):.1e}"
    cn = _j(data_dir, "phase5-corners.json")
    for s in ("G", "B"):
        v = list(cn[s].values())
        out[f"corner_{s}"] = f"{sum(x['max_corner_radius'] >= 1 for x in v)} of {len(v)}"
    for s in ("G", "B"):
        rows = _set(data_dir, s)
        nw = [r for r in rows if not r["witness"]]
        m4 = [r for r in nw if r["m"] == max(x["m"] for x in nw)]
        out[f"mmax_{s}"] = str(m4[0]["m"])
        out[f"h3_mmax_{s}"] = f"{sum(r['G1'] == 'certified' for r in m4)} of {len(m4)}"
        small = [r for r in nw if r["m"] == 1]
        out[f"sin_g1_{s}"] = str(sum("sin" in r["shapes"] and r["G1"] == "certified" for r in rows))
        out[f"h3_m1_{s}"] = f"{sum(r['G1'] == 'certified' for r in small)} of {len(small)}"
    w = _j(data_dir, "phase5-windup.json")["kernels"]
    out["windup_growth"] = f"{w['s122-067']['series']['start 100'][-1]:.0e}"
    return out
