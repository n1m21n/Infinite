"""Figures and generated tables for the aliasing-aware-field research page and paper, from data/ only.

What: figures(data_dir, mode, data_url) returns {name: (body_html, caption_html)}. tools/research/build_lab_note.py
      puts each where note.md has <div data-fig="name"></div>, numbers it ("Figure n") and wraps it. mode is "web"
      (interactive explorer, audio players) or "paper" (static versions for the PDF). Colours are brand CSS variables.
Why:  the research standard says every figure and table comes from data/ by a program, never by hand.
"""
import csv, html, json, math, os, re, statistics

e = html.escape
W, H = 520, 300
L, R, T, B = 50, 112, 12, 40          # plot margins (right margin holds the series labels)
SERIES = ["var(--accent)", "var(--signal)", "var(--cat-modulators)", "var(--ink-2)"]
FACTORS = [1, 2, 4, 8, 16, 32]


def _rows(path):
    return list(csv.DictReader(open(path, encoding="utf-8")))


def _svg(w, h, label, g):
    return f'<svg viewBox="0 0 {w} {h}" class="chart" role="img" aria-label="{e(label)}">' + "".join(g) + "</svg>"


def _axes(ymin, ymax, yticks, xlabels, ylabel, xlabel, w=W, h=H, r=R):
    pw, ph = w - L - r, h - T - B
    sx = lambda i: L + pw * i / (len(xlabels) - 1)
    sy = lambda v: T + ph * (ymax - max(ymin, min(ymax, v))) / (ymax - ymin)
    g = [f'<line x1="{L}" y1="{T + ph}" x2="{L + pw}" y2="{T + ph}" class="ax"/>']
    for v in yticks:
        g.append(f'<line x1="{L}" y1="{sy(v):.1f}" x2="{L + pw}" y2="{sy(v):.1f}" class="grid"/>'
                 f'<text x="{L - 7}" y="{sy(v) + 4:.1f}" text-anchor="end">{v}</text>')
    for i, lab in enumerate(xlabels):
        g.append(f'<text x="{sx(i):.1f}" y="{T + ph + 17}" text-anchor="middle">{lab}</text>')
    g.append(f'<text x="{L + pw / 2}" y="{h - 3}" text-anchor="middle" class="axl">{e(xlabel)}</text>')
    g.append(f'<text x="12" y="{T + ph / 2}" text-anchor="middle" class="axl" transform="rotate(-90 12 {T + ph / 2})">{e(ylabel)}</text>')
    return g, sx, sy


def _link(data_url, f):
    return f'<a href="{data_url}/{f}">{e(f)}</a>'


def slopes(d, mode, data_url):
    """Phase 0: alias vs oversampling factor, one kernel per smoothness class, with the -80 dB target."""
    rows = _rows(os.path.join(d, "phase0-baseline.csv"))
    pick = [("comparator square + dry", "jump (6)"), ("hard clip drive 1", "kink (12)"),
            ("rational x/(1+|x|) drive 2", "C1 (18)"), ("tanh drive 4", "analytic")]
    g, sx, sy = _axes(-140, 0, [0, -20, -40, -60, -80, -100, -120, -140], [f"{f}x" for f in FACTORS],
                      "alias below 20 kHz (dB)", "oversampling factor")
    g.append(f'<line x1="{L}" y1="{sy(-80):.1f}" x2="{W - R}" y2="{sy(-80):.1f}" class="target"/>'
             f'<text x="{L + 6}" y="{sy(-80) - 5:.1f}" class="tl">target −80 dB</text>')
    ends = []
    for k, (label, name) in enumerate(pick):
        pts = sorted((int(r["factor"]), float(r["alias_db"])) for r in rows if r["label"] == label)
        path = " ".join(f"{'M' if i == 0 else 'L'}{sx(FACTORS.index(f)):.1f},{sy(v):.1f}" for i, (f, v) in enumerate(pts))
        c = SERIES[k]
        g.append(f'<path d="{path}" class="ln" style="stroke:{c}"/>')
        g += [f'<circle cx="{sx(FACTORS.index(f)):.1f}" cy="{sy(v):.1f}" r="3" style="fill:{c}"/>' for f, v in pts]
        ends.append([sy(pts[-1][1]) + 4, name, c])
    ends.sort()
    for i in range(1, len(ends)):                       # keep end labels at least one line apart
        ends[i][0] = max(ends[i][0], ends[i - 1][0] + 14)
    g += [f'<text x="{sx(5) + 8:.1f}" y="{y:.1f}" style="fill:{c}" class="sl">{e(n)}</text>' for y, n, c in ends]
    cap = ("Measured alias against oversampling factor for one kernel of each smoothness class (jump: comparator; "
           "kink: hard clip; C1: x/(1+|x|); analytic: tanh at drive 4). The number after each class is the slope, in dB "
           f"per doubling, that the smoothness law predicts. Data: {_link(data_url, 'phase0-baseline.csv')}.")
    return _svg(W, H, "Alias against oversampling factor for four smoothness classes", g), cap


def scatter(d, mode, data_url):
    """Phase 1: predicted against measured alias, every (kernel, factor) pair, both floored at -100 dB as scored."""
    # as scored for H1: every predicted ("ok") nonlinear kernel; the 3 linear controls are left out
    rows = [r for r in _rows(os.path.join(d, "phase1-predictions.csv")) if r["status"] == "ok" and r["class"] != "linear"]
    memoryless = {"poly", "analytic", "kink", "chain"}
    s, w, h, lo = 300, 380, 360, -100
    pl, pt = 56, 12
    sc = lambda v: (max(lo, min(0, v)) - lo) / -lo * s
    X, Y = lambda v: pl + sc(v), lambda v: pt + s - sc(v)
    g = [f'<line x1="{pl}" y1="{pt + s}" x2="{pl + s}" y2="{pt + s}" class="ax"/>']
    for v in range(-100, 1, 20):
        g.append(f'<line x1="{X(v):.1f}" y1="{pt}" x2="{X(v):.1f}" y2="{pt + s}" class="grid"/>'
                 f'<line x1="{pl}" y1="{Y(v):.1f}" x2="{pl + s}" y2="{Y(v):.1f}" class="grid"/>'
                 f'<text x="{X(v):.1f}" y="{pt + s + 17}" text-anchor="middle">{v}</text>'
                 f'<text x="{pl - 7}" y="{Y(v) + 4:.1f}" text-anchor="end">{v}</text>')
    g.append(f'<line x1="{X(lo)}" y1="{Y(lo)}" x2="{X(0)}" y2="{Y(0)}" class="target"/>')
    n_mem = n_inf = 0
    for r in sorted(rows, key=lambda r: r["class"] not in memoryless):   # informative points drawn on top
        m, p = float(r["alias_db"]), float(r["predicted_db"])
        mem = r["class"] in memoryless
        n_mem += mem; n_inf += not mem
        g.append(f'<circle cx="{X(m):.1f}" cy="{Y(p):.1f}" r="{2.6 if mem else 3.2}" '
                 f'style="fill:{"var(--ink-3)" if mem else "var(--accent)"};fill-opacity:{0.55 if mem else 0.9}"/>')
    g.append(f'<text x="{pl + s / 2}" y="{h - 3}" text-anchor="middle" class="axl">measured alias (dB)</text>'
             f'<text x="12" y="{pt + s / 2}" text-anchor="middle" class="axl" transform="rotate(-90 12 {pt + s / 2})">predicted alias (dB)</text>'
             f'<circle cx="{pl + 12}" cy="{pt + 14}" r="3.2" style="fill:var(--accent)"/><text x="{pl + 21}" y="{pt + 18}" class="sl">informative ({n_inf})</text>'
             f'<circle cx="{pl + 12}" cy="{pt + 32}" r="2.6" style="fill:var(--ink-3)"/><text x="{pl + 21}" y="{pt + 36}" class="sl">memoryless, exact ({n_mem})</text>')
    cap = (f"Predicted against measured alias for the {len(rows) // 6} predicted nonlinear kernels at six factors ({len(rows)} points, "
           "both sides floored at −100 dB, as scored for H1). On the diagonal the prediction is exact. Grey points are memoryless kernels (poly, analytic, kink, chain), "
           "for which the prediction is exact by construction (Section 3.3); the orange points (state, phase accumulator, jump tail, preset) are the informative ones. "
           f"Data: {_link(data_url, 'phase1-predictions.csv')}.")
    return _svg(w, h, "Predicted against measured alias", g).replace('class="chart"', 'class="chart sq"', 1), cap


def explorer(d, mode, data_url):
    """Phase 1: one kernel at a time, measured vs predicted per factor. Web: a picker; paper: four chosen kernels."""
    rows = _rows(os.path.join(d, "phase1-predictions.csv"))
    ker = {}
    for r in rows:
        k = ker.setdefault(r["label"], {"c": r["class"], "m": [], "p": [], "s": r["status"]})
        k["m"].append(round(float(r["alias_db"]), 1))
        try:
            k["p"].append(round(float(r["predicted_db"]), 1))
        except ValueError:
            k["p"].append(None)
    cap = ("Measured (solid) and predicted (dashed) alias per factor. Measured: the real Field Effect node rendered at "
           "each factor and decimated. Predicted: the phase-1 pass, from the program without rendering. "
           f"Data: {_link(data_url, 'phase1-predictions.csv')}.")
    if mode == "paper":
        pick = ["tanh feedback 0.7", "comparator square + dry", "osc naive triangle 1.75k", "preset Tube Overdrive"]
        cells = []
        for name in pick:
            k = ker[name]
            w, h = 250, 170
            g, sx, sy = _axes(-140, 0, [0, -40, -80, -120], [f"{f}x" for f in FACTORS], "dB", name, w, h, 10)
            g.append(f'<line x1="{L}" y1="{sy(-80):.1f}" x2="{w - 10}" y2="{sy(-80):.1f}" class="target"/>')
            pm = " ".join(f"{'M' if i == 0 else 'L'}{sx(i):.1f},{sy(v):.1f}" for i, v in enumerate(k["m"]))
            pp = " ".join(f"{'M' if i == 0 else 'L'}{sx(i):.1f},{sy(v):.1f}" for i, v in enumerate(k["p"]) if v is not None)
            g.append(f'<path d="{pm}" class="ln" style="stroke:var(--accent)"/><path d="{pp}" class="ln dash" style="stroke:var(--signal)"/>')
            cells.append(_svg(w, h, name, g))
        return '<div class="rn-grid2">' + "".join(cells) + "</div>", cap.replace("per factor.", "per factor for four kernels where the prediction is not exact by construction.")
    data = [{"n": n, **v} for n, v in ker.items()]
    opts = "".join(f'<option value="{i}"{" selected" if k["n"] == "tanh feedback 0.7" else ""}>{e(k["n"])} ({e(k["c"])})</option>'
                   for i, k in enumerate(data))
    body = f"""<label class="rn-pick">Kernel <select id="rn-kernel">{opts}</select></label>
<svg viewBox="0 0 {W} {H}" class="chart" id="rn-ex" role="img" aria-label="Measured and predicted alias for the chosen kernel"></svg>
<p class="rn-readout" id="rn-read" aria-live="polite"></p>
<noscript><p>The chart needs JavaScript. The same numbers are in the CSV linked below.</p></noscript>
<script type="application/json" id="rn-data">{json.dumps(data, separators=(",", ":"))}</script>"""
    return body, cap.replace("per factor.", f"per factor, for any of the {len(data)} corpus kernels.")


def _h2_rows(d):
    rx = re.compile(r"^FIELDRATE (.+?)\s{2,}(\w+)\s+(\d+)x\s+(\d+) (\S+)\s+(\d+)\s+(\d+)%")
    text = open(os.path.join(d, "phase2-h2-run.log"), encoding="utf-8").read()
    rows = [(m.group(1).strip(), m.group(2), int(m.group(7))) for m in map(rx.match, text.splitlines()) if m]
    agg = re.search(r"aggregate saving ([\d.]+)%", text).group(1)
    return rows, agg


def h2(d, mode, data_url):
    """Phase 2a: CPU saving per planned kernel against the pre-registered 25% bar."""
    rows, agg = _h2_rows(d)
    rows.sort(key=lambda r: -r[2])
    bh, lw, pw, top = 17, 262, 250, 26
    h = len(rows) * bh + top + 34
    sx = lambda v: lw + pw * v / 50
    g = []
    for v in (0, 10, 20, 30, 40, 50):
        g.append(f'<line x1="{sx(v):.1f}" y1="{top - 4}" x2="{sx(v):.1f}" y2="{h - 30}" class="grid"/><text x="{sx(v):.1f}" y="{h - 14}" text-anchor="middle">{v}%</text>')
    for i, (name, cls, s) in enumerate(rows):
        y = top + i * bh
        g.append(f'<text x="{lw - 7}" y="{y + 11}" text-anchor="end">{e(name)}</text>'
                 f'<rect x="{lw}" y="{y + 2}" width="{max(1.5, sx(s) - lw):.1f}" height="{bh - 5}" class="bar"/>'
                 f'<text x="{sx(s) + 4:.1f}" y="{y + 11}" class="bv">{s}%</text>')
    a = float(agg)
    g.append(f'<line x1="{sx(25):.1f}" y1="{top - 8}" x2="{sx(25):.1f}" y2="{h - 30}" class="target"/>'
             f'<text x="{sx(25) + 4:.1f}" y="{top - 12}" class="tl">bar 25%</text>'
             f'<line x1="{sx(a):.1f}" y1="{top - 8}" x2="{sx(a):.1f}" y2="{h - 30}" class="agg"/>'
             f'<text x="{sx(a) - 4:.1f}" y="{top - 12}" text-anchor="end" class="al">aggregate {agg}%</text>')
    cap = (f"CPU saved by the per-segment plan against oversampling the whole kernel at the factor it needs, for the {len(rows)} "
           "kernels that needed oversampling and had a factor within 32x. A plan that missed −80 dB when measured is charged "
           f"the whole-kernel cost (0%). The aggregate is cost-weighted. Data: {_link(data_url, 'phase2-h2-run.log')}.")
    return _svg(lw + pw + 44, h, "CPU saving per kernel against the 25 percent bar", g), cap


def cost_repeats(d, mode, data_url):
    """Table: the cost model re-measured in independent runs, and the H2 verdict under each."""
    p = os.path.join(d, "phase2-cost-repeats.csv")
    rows = _rows(p)
    agg = [float(r["aggregate_pct"]) for r in rows]
    med = [float(r["median_pct"]) for r in rows]
    n = len(agg)
    mean, sd = statistics.mean(agg), statistics.stdev(agg) if n > 1 else 0.0
    tcrit = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26, 11: 2.23, 12: 2.20}.get(n, 2.0)
    half = tcrit * sd / math.sqrt(n) if n > 1 else 0.0
    head = "<tr><th>run</th><th>ns / instr</th><th>up 2/4/8x</th><th>down 2/4/8x</th><th>aggregate saving</th><th>median</th><th>tanh x3 beats 8x</th></tr>"
    body = "".join(f"<tr><td>{e(r['run'])}</td><td>{float(r['per_instr']):.2f}</td><td>{e(r['up'])}</td><td>{e(r['down'])}</td>"
                   f"<td>{float(r['aggregate_pct']):.1f}%</td><td>{float(r['median_pct']):.1f}%</td><td>{e(r['chain_beats_8x'])}</td></tr>" for r in rows)
    foot = (f"<tr class='rn-sum'><td colspan='4'>mean over {n} runs (95% CI, t with {n - 1} df)</td>"
            f"<td>{mean:.1f}% ± {half:.1f}</td><td>{statistics.mean(med):.1f}%</td><td></td></tr>")
    table = f'<div class="table-wrap"><table class="rn-num"><thead>{head}</thead><tbody>{body}{foot}</tbody></table></div>'
    cap = (f"The cost model re-measured in {n} independent runs of the −O2 microbenchmark (each the best of 5 trials), and "
           f"the H2 rig re-run under each. Aggregate saving {mean:.1f}% (95% CI {mean - half:.1f} to {mean + half:.1f}) "
           f"against the 25% bar. Data: {_link(data_url, 'phase2-cost-repeats.csv')}.")
    return table, cap, (mean, half, n)


def audio(d, mode, data_url):
    """The three-stage tanh chain, as heard: four real renders of the rig's 5 kHz test tone."""
    log = open(os.path.join(d, "phase2-audio", "renders.log"), encoding="utf-8").read()
    db = {m.group(1): float(m.group(2)) for m in re.finditer(r"FIELDRATE audio (?:\S*/)?(tanh3-[\w]+)\.f32 \d+ samples, alias (-?[\d.]+) dB", log)}
    clips = [("tanh3-1x", "No oversampling (1x)"), ("tanh3-fixed8x", "Whole kernel at a fixed 8x"),
             ("tanh3-staged", "The planner's staged plan (2x | 4x | 8x | 1x)"), ("tanh3-32x", "Whole kernel at 32x (reference)")]
    if mode == "paper":
        rows = "".join(f"<tr><td>{e(t)}</td><td>{db[c]:.1f} dB</td><td>{_link(data_url, 'phase2-audio/' + c + '.wav')}</td></tr>" for c, t in clips)
        body = f'<div class="table-wrap"><table class="rn-num"><thead><tr><th>render</th><th>measured alias</th><th>audio</th></tr></thead><tbody>{rows}</tbody></table></div>'
    else:
        body = '<div class="rn-audio">' + "".join(
            f'<div class="rn-clip"><div class="rn-clip-h"><span>{e(t)}</span><span class="rn-db">{db[c]:.1f} dB</span></div>'
            f'<audio controls preload="none" src="{data_url}/phase2-audio/{c}.wav"></audio></div>' for c, t in clips) + "</div>"
    cap = ("The three-stage tanh chain on the rig's 5 kHz test tone, rendered by the real resamplers. Each clip is one "
           "steady-state period (16 384 samples at 48 kHz) repeated to 2 s, peak-normalised to −18 dBFS with 10 ms fades; "
           "the dB figure is the alias the rig measured on that render. Aliases are the faint tones that do not belong to "
           "the 5 kHz harmonic series. "
           f"Data: {_link(data_url, 'phase2-audio/renders.log')} and the .f32 and .wav files beside it.")
    return body, cap


def figures(data_dir, mode="web", data_url="data"):
    out = {}
    for name, fn in (("slopes", slopes), ("scatter", scatter), ("explorer", explorer), ("h2", h2), ("audio", audio)):
        out[name] = fn(data_dir, mode, data_url)
    t, cap, _ = cost_repeats(data_dir, mode, data_url)
    out["costs"] = (t, cap)
    return out


def numbers(data_dir):
    """Values the text quotes, so prose and data cannot drift: {{name}} in note.md."""
    _, _, (mean, half, n) = cost_repeats(data_dir, "web", "")
    return {"cost_mean": f"{mean:.1f}", "cost_ci": f"{half:.1f}", "cost_n": str(n),
            "cost_lo": f"{mean - half:.1f}", "cost_hi": f"{mean + half:.1f}"}


SCRIPT = r"""
(function(){
  var el=document.getElementById('rn-data'); if(!el) return;
  var K=JSON.parse(el.textContent), sel=document.getElementById('rn-kernel'), svg=document.getElementById('rn-ex'), rd=document.getElementById('rn-read');
  var W=%d,H=%d,L=%d,R=%d,T=%d,B=%d, F=[1,2,4,8,16,32], pw=W-L-R, ph=H-T-B, ymin=-160, ymax=10;
  function sx(i){return L+pw*i/5} function sy(v){v=Math.max(ymin,Math.min(ymax,v));return T+ph*(ymax-v)/(ymax-ymin)}
  function path(a){var s='',f=true;for(var i=0;i<a.length;i++){if(a[i]===null){f=true;continue}s+=(f?'M':'L')+sx(i).toFixed(1)+','+sy(a[i]).toFixed(1);f=false}return s}
  function need(a){if(!a)return null;for(var i=0;i<a.length;i++){if(a[i]!==null&&a[i]<=-80)return F[i]+'x'}return 'none within 32x'}
  function draw(){
    var k=K[+sel.value], g='';
    for(var v=0;v>=-160;v-=40){g+='<line x1="'+L+'" y1="'+sy(v)+'" x2="'+(L+pw)+'" y2="'+sy(v)+'" class="grid"/><text x="'+(L-7)+'" y="'+(sy(v)+4)+'" text-anchor="end">'+v+'</text>'}
    for(var i=0;i<6;i++){g+='<text x="'+sx(i)+'" y="'+(T+ph+17)+'" text-anchor="middle">'+F[i]+'x</text>'}
    g+='<line x1="'+L+'" y1="'+sy(-100)+'" x2="'+(L+pw)+'" y2="'+sy(-100)+'" class="floor"/><text x="'+(L+pw-6)+'" y="'+(sy(-100)+15)+'" text-anchor="end" class="tl">scoring floor −100</text>';
    g+='<line x1="'+L+'" y1="'+sy(-80)+'" x2="'+(L+pw)+'" y2="'+sy(-80)+'" class="target"/><text x="'+(L+pw-6)+'" y="'+(sy(-80)-5)+'" text-anchor="end" class="tl">target −80 dB</text>';
    g+='<path d="'+path(k.m)+'" class="ln" style="stroke:var(--accent)"/>';
    var hasP=k.p.some(function(x){return x!==null});
    if(hasP) g+='<path d="'+path(k.p)+'" class="ln dash" style="stroke:var(--signal)"/>';
    for(i=0;i<6;i++){g+='<circle cx="'+sx(i)+'" cy="'+sy(k.m[i])+'" r="3.2" style="fill:var(--accent)"/>'}
    g+='<text x="'+(L+pw+10)+'" y="'+(T+14)+'" class="sl" style="fill:var(--accent)">measured</text>';
    if(hasP) g+='<text x="'+(L+pw+10)+'" y="'+(T+31)+'" class="sl" style="fill:var(--signal)">predicted</text>';
    g+='<text x="'+(L+pw/2)+'" y="'+(H-3)+'" text-anchor="middle" class="axl">oversampling factor</text>';
    g+='<text x="12" y="'+(T+ph/2)+'" text-anchor="middle" class="axl" transform="rotate(-90 12 '+(T+ph/2)+')">alias below 20 kHz (dB)</text>';
    svg.innerHTML=g;
    rd.textContent='Class: '+k.c+'. Factor needed for −80 dB: measured '+need(k.m)+(hasP?', predicted '+need(k.p)+'.':'; no prediction ('+k.s+').');
  }
  sel.addEventListener('change',draw); draw();
})();
""" % (W, H, L, R, T, B)
