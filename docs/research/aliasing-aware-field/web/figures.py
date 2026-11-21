"""Figures for the aliasing-aware-field web lab note, generated from data/ only.

What: figures(data_dir) returns {name: html}; tools/research/build_lab_note.py puts each one where the note
      has <div data-fig="name"></div>. Colours are brand CSS variables, so the charts follow Paper/Midnight.
Why:  the research standard says every figure comes from data/ by a program, never by hand.
"""
import csv, html, json, math, os, re

e = html.escape
W, H = 640, 340
L, R, T, B = 56, 150, 16, 40          # plot margins (right margin holds the series labels)
SERIES = ["var(--accent)", "var(--signal)", "var(--cat-modulators)", "var(--ink-2)"]


def _rows(path):
    return list(csv.DictReader(open(path, encoding="utf-8")))


def _axes(ymin, ymax, yticks, xlabels, ylabel, xlabel):
    pw, ph = W - L - R, H - T - B
    sx = lambda i: L + pw * i / (len(xlabels) - 1)
    sy = lambda v: T + ph * (ymax - max(ymin, min(ymax, v))) / (ymax - ymin)
    g = [f'<line x1="{L}" y1="{T + ph}" x2="{L + pw}" y2="{T + ph}" class="ax"/>']
    for v in yticks:
        g.append(f'<line x1="{L}" y1="{sy(v):.1f}" x2="{L + pw}" y2="{sy(v):.1f}" class="grid"/>'
                 f'<text x="{L - 8}" y="{sy(v) + 4:.1f}" text-anchor="end">{v}</text>')
    for i, lab in enumerate(xlabels):
        g.append(f'<text x="{sx(i):.1f}" y="{T + ph + 18}" text-anchor="middle">{lab}</text>')
    g.append(f'<text x="{L + pw / 2}" y="{H - 4}" text-anchor="middle" class="axl">{e(xlabel)}</text>')
    g.append(f'<text x="14" y="{T + ph / 2}" text-anchor="middle" class="axl" transform="rotate(-90 14 {T + ph / 2})">{e(ylabel)}</text>')
    return g, sx, sy


def slopes(d):
    """Phase 0: alias vs oversampling factor, one kernel per smoothness class, with the -80 dB target."""
    rows = _rows(os.path.join(d, "phase0-baseline.csv"))
    pick = [("comparator square + dry", "jump (6)"), ("hard clip drive 1", "kink (12)"),
            ("rational x/(1+|x|) drive 2", "C1 (18)"), ("tanh drive 4", "analytic (cliff)")]
    factors = [1, 2, 4, 8, 16, 32]
    g, sx, sy = _axes(-140, 0, [0, -20, -40, -60, -80, -100, -120, -140], [f"{f}x" for f in factors],
                      "alias below 20 kHz (dB)", "oversampling factor")
    g.append(f'<line x1="{L}" y1="{sy(-80):.1f}" x2="{W - R}" y2="{sy(-80):.1f}" class="target"/>'
             f'<text x="{W - R - 4}" y="{sy(-80) - 6:.1f}" text-anchor="end" class="tl">target -80 dB</text>')
    for k, (label, name) in enumerate(pick):
        pts = sorted((int(r["factor"]), float(r["alias_db"])) for r in rows if r["label"] == label)
        path = " ".join(f"{'M' if i == 0 else 'L'}{sx(factors.index(f)):.1f},{sy(v):.1f}" for i, (f, v) in enumerate(pts))
        c = SERIES[k]
        g.append(f'<path d="{path}" class="ln" style="stroke:{c}"/>')
        g += [f'<circle cx="{sx(factors.index(f)):.1f}" cy="{sy(v):.1f}" r="3" style="fill:{c}"/>' for f, v in pts]
        f, v = pts[-1]
        g.append(f'<text x="{sx(5) + 8:.1f}" y="{sy(v) + 4:.1f}" style="fill:{c}" class="sl">{e(name)}</text>')
    svg = f'<svg viewBox="0 0 {W} {H}" class="chart" role="img" aria-label="Alias against oversampling factor for four smoothness classes">' + "".join(g) + "</svg>"
    return (f'<figure class="rn-fig">{svg}<figcaption>Measured alias against oversampling factor for one kernel of each '
            f'smoothness class (jump: comparator; kink: hard clip; C1: x/(1+|x|); analytic: tanh at drive 4). '
            f'Labels give the slope in dB per doubling that the smoothness law predicts. Data: '
            f'<a href="aliasing-aware-field/data/phase0-baseline.csv">phase0-baseline.csv</a>.</figcaption></figure>')


def explorer(d):
    """Phase 1: pick any kernel; measured vs predicted alias at each factor. Data embedded, drawn by JS."""
    rows = _rows(os.path.join(d, "phase1-predictions.csv"))
    ker = {}
    for r in rows:
        k = ker.setdefault(r["label"], {"c": r["class"], "m": [], "p": [], "s": r["status"]})
        k["m"].append(round(float(r["alias_db"]), 1))
        try:
            k["p"].append(round(float(r["predicted_db"]), 1))
        except ValueError:
            k["p"].append(None)
    data = [{"n": n, **v} for n, v in ker.items()]
    opts = "".join(f'<option value="{i}"{" selected" if k["n"] == "tanh drive 4" else ""}>{e(k["n"])} ({e(k["c"])})</option>'
                   for i, k in enumerate(data))
    return f"""<figure class="rn-fig rn-explorer">
<label class="rn-pick mono">kernel <select id="rn-kernel">{opts}</select></label>
<svg viewBox="0 0 {W} {H}" class="chart" id="rn-ex" role="img" aria-label="Measured and predicted alias for the chosen kernel"></svg>
<p class="rn-readout mono" id="rn-read" aria-live="polite"></p>
<noscript><p>The chart needs JavaScript. The same numbers are in the CSV linked below.</p></noscript>
<figcaption>Every kernel in the calibration corpus (47). Solid: alias measured from the real Field Effect node, rendered at each factor
and decimated. Dashed: the phase-1 prediction, computed from the program without rendering. Both are floored at -100 dB for
scoring. Data: <a href="aliasing-aware-field/data/phase1-predictions.csv">phase1-predictions.csv</a>.</figcaption>
<script type="application/json" id="rn-data">{json.dumps(data, separators=(",", ":"))}</script>
</figure>"""


def h2(d):
    """Phase 2a: CPU saving per planned kernel against the pre-registered 25% bar."""
    rx = re.compile(r"^FIELDRATE (.+?)\s{2,}(\w+)\s+(\d+)x\s+(\d+) (\S+)\s+(\d+)\s+(\d+)%")
    rows = []
    for ln in open(os.path.join(d, "phase2-h2-run.log"), encoding="utf-8"):
        m = rx.match(ln.rstrip())
        if m:
            rows.append((m.group(1).strip(), m.group(2), int(m.group(7))))
    agg = re.search(r"aggregate saving ([\d.]+)%", open(os.path.join(d, "phase2-h2-run.log"), encoding="utf-8").read()).group(1)
    rows.sort(key=lambda r: -r[2])
    bh, lw, pw = 17, 210, 360
    h = len(rows) * bh + 46
    sx = lambda v: lw + pw * v / 50
    g = []
    for v in (0, 10, 20, 30, 40, 50):
        g.append(f'<line x1="{sx(v):.1f}" y1="8" x2="{sx(v):.1f}" y2="{h - 30}" class="grid"/><text x="{sx(v):.1f}" y="{h - 14}" text-anchor="middle">{v}%</text>')
    for i, (name, cls, s) in enumerate(rows):
        y = 10 + i * bh
        g.append(f'<text x="{lw - 8}" y="{y + 11}" text-anchor="end">{e(name)}</text>'
                 f'<rect x="{lw}" y="{y + 2}" width="{max(1.5, sx(s) - lw):.1f}" height="{bh - 5}" class="bar"/>'
                 f'<text x="{sx(s) + 5:.1f}" y="{y + 11}" class="bv">{s}%</text>')
    g.append(f'<line x1="{sx(25):.1f}" y1="4" x2="{sx(25):.1f}" y2="{h - 30}" class="target"/>'
             f'<text x="{sx(25) + 4:.1f}" y="{h - 34}" class="tl">bar 25%</text>'
             f'<line x1="{sx(float(agg)):.1f}" y1="4" x2="{sx(float(agg)):.1f}" y2="{h - 30}" class="agg"/>'
             f'<text x="{sx(float(agg)) - 4:.1f}" y="{h - 34}" text-anchor="end" class="al">aggregate {agg}%</text>')
    svg = f'<svg viewBox="0 0 {lw + pw + 50} {h}" class="chart" role="img" aria-label="CPU saving per kernel against the 25 percent bar">' + "".join(g) + "</svg>"
    return (f'<figure class="rn-fig">{svg}<figcaption>CPU saved by the per-segment plan against oversampling the whole kernel at the factor it needs, '
            f'for the {len(rows)} kernels that needed oversampling and had a factor within 32x. A plan that missed -80 dB when measured is charged '
            f'the whole-kernel cost (0%). The aggregate is cost-weighted. Data: '
            f'<a href="aliasing-aware-field/data/phase2-h2-run.log">phase2-h2-run.log</a>.</figcaption></figure>')


def figures(data_dir):
    return {"slopes": slopes(data_dir), "explorer": explorer(data_dir), "h2": h2(data_dir)}


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
    for(var v=0;v>=-160;v-=20){g+='<line x1="'+L+'" y1="'+sy(v)+'" x2="'+(L+pw)+'" y2="'+sy(v)+'" class="grid"/><text x="'+(L-8)+'" y="'+(sy(v)+4)+'" text-anchor="end">'+v+'</text>'}
    for(var i=0;i<6;i++){g+='<text x="'+sx(i)+'" y="'+(T+ph+18)+'" text-anchor="middle">'+F[i]+'x</text>'}
    g+='<line x1="'+L+'" y1="'+sy(-100)+'" x2="'+(L+pw)+'" y2="'+sy(-100)+'" class="floor"/><text x="'+(L+pw-4)+'" y="'+(sy(-100)+14)+'" text-anchor="end" class="tl">scoring floor -100 dB</text>';
    g+='<line x1="'+L+'" y1="'+sy(-80)+'" x2="'+(L+pw)+'" y2="'+sy(-80)+'" class="target"/><text x="'+(L+pw-4)+'" y="'+(sy(-80)-6)+'" text-anchor="end" class="tl">target -80 dB</text>';
    g+='<path d="'+path(k.m)+'" class="ln" style="stroke:var(--accent)"/>';
    var hasP=k.p.some(function(x){return x!==null});
    if(hasP) g+='<path d="'+path(k.p)+'" class="ln dash" style="stroke:var(--signal)"/>';
    for(i=0;i<6;i++){g+='<circle cx="'+sx(i)+'" cy="'+sy(k.m[i])+'" r="3.2" style="fill:var(--accent)"/>'}
    g+='<text x="'+(L+pw+10)+'" y="'+(T+14)+'" class="sl" style="fill:var(--accent)">measured</text>';
    if(hasP) g+='<text x="'+(L+pw+10)+'" y="'+(T+32)+'" class="sl" style="fill:var(--signal)">predicted</text>';
    g+='<text x="'+(L+pw/2)+'" y="'+(H-4)+'" text-anchor="middle" class="axl">oversampling factor</text>';
    g+='<text x="14" y="'+(T+ph/2)+'" text-anchor="middle" class="axl" transform="rotate(-90 14 '+(T+ph/2)+')">alias below 20 kHz (dB)</text>';
    svg.innerHTML=g;
    rd.textContent='class '+k.c+' · factor for -80 dB: measured '+need(k.m)+(hasP?', predicted '+need(k.p):', no prediction ('+k.s+')');
  }
  sel.addEventListener('change',draw); draw();
})();
""" % (W, H, L, R, T, B)
