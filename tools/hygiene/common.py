"""Shared plumbing for the website hygiene checks: page list, local server, one browser probe per page and
viewport, brand values read from docs/brand/brand.json, and the finding report.

Python 3.9, Playwright. Every check is a function (page_record) -> [Finding]; each script in this folder runs one
family, run_all.py runs them all. Brand values are never typed into a check: they come from brand.json.
"""
from __future__ import annotations
import argparse, functools, glob, http.server, json, os, socketserver, sys, threading
from collections import defaultdict

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WEB = os.path.join(ROOT, "website")
CHROME = os.environ.get("CHROME_PATH") or next(iter(glob.glob("/opt/pw-browsers/chromium-*/chrome-linux/chrome")), None)
VIEWPORTS = {"phone": (390, 844), "tablet": (768, 1024), "desktop": (1440, 900)}
SKIP_DIRS = ("library/", "assets/")  # generated per-patch pages are covered by --all


def brand():
    return json.load(open(os.path.join(ROOT, "docs", "brand", "brand.json"), encoding="utf-8"))


def pages(all_pages=False):
    out = []
    for dp, _, fs in os.walk(WEB):
        for f in fs:
            if f.endswith(".html"):
                rel = os.path.relpath(os.path.join(dp, f), WEB).replace(os.sep, "/")
                if rel.startswith(("assets/", "node_modules/")) or (not all_pages and rel.startswith(("library/", "community/"))):
                    continue
                if "noindex" in open(os.path.join(dp, f), encoding="utf-8", errors="ignore").read(4000):
                    continue   # private or unfinished pages are not part of the public site
                out.append(rel)
    return sorted(out)


class Finding:
    def __init__(self, rule, severity, where, detail):
        self.rule, self.severity, self.where, self.detail = rule, severity, where, detail


class Report:
    """Collapses repeats: one line per (rule, where, detail) with the pages and viewports that show it."""
    def __init__(self):
        self.items = defaultdict(set)
        self.sev = {}

    def add(self, f, page, vp):
        k = (f.rule, f.where, f.detail)
        self.items[k].add("%s@%s" % (page, vp))
        self.sev[k] = f.severity

    def print(self, limit=60):
        rows = sorted(self.items.items(), key=lambda kv: (self.sev[kv[0]] != "error", kv[0][0], -len(kv[1])))
        for (rule, where, detail), at in rows[:limit]:
            pg = sorted({a.split("@")[0] for a in at})
            vps = sorted({a.split("@")[1] for a in at})
            print("%-5s %-22s %s | %s | %d page(s) %s e.g. %s" % (self.sev[(rule, where, detail)].upper(), rule, where, detail, len(pg), "/".join(vps), pg[0]))
        if len(rows) > limit:
            print("... %d more (use --limit 0)" % (len(rows) - limit))
        e = sum(1 for k in self.items if self.sev[k] == "error"); w = len(self.items) - e
        print("\n%d error rule(s), %d warning(s)" % (e, w))
        return e


PROBE_JS = r"""
() => {
  const out = {w: innerWidth, sw: document.documentElement.scrollWidth, els: [], imgs: []};
  const sel = (el) => {
    let s = el.tagName.toLowerCase();
    if (el.id) s += '#' + el.id;
    const c = (el.getAttribute('class') || '').trim().split(/\s+/).filter(Boolean).slice(0, 2);
    if (c.length) s += '.' + c.join('.');
    return s;
  };
  const px = (v) => parseFloat(v) || 0;
  const all = document.body.querySelectorAll('*');
  let n = 0;
  const idx = new Map();
  all.forEach((el) => {
    if (['SCRIPT', 'STYLE', 'NOSCRIPT', 'PATH', 'CIRCLE', 'RECT', 'LINE', 'G', 'DEFS', 'POLYGON', 'POLYLINE', 'ELLIPSE', 'TSPAN', 'USE', 'STOP', 'LINEARGRADIENT', 'RADIALGRADIENT', 'CLIPPATH', 'MASK', 'FILTER', 'SYMBOL'].includes(el.tagName.toUpperCase())) return;
    if (el.closest('svg') && el.tagName.toLowerCase() !== 'svg') return;
    const r = el.getBoundingClientRect();
    if (r.width < 1 || r.height < 1) return;
    const cs = getComputedStyle(el);
    if (cs.visibility === 'hidden' || cs.display === 'none') return;
    if (el.closest('[hidden], .reveal:not(.in)') && false) return;
    let own = '';
    el.childNodes.forEach((c) => { if (c.nodeType === 3) own += c.textContent; });
    own = own.replace(/\s+/g, ' ').trim();
    idx.set(el, n);
    out.els.push({
      s: sel(el), t: el.tagName.toLowerCase(), p: el.parentElement ? sel(el.parentElement) : '',
      i: n, par: el.parentElement && idx.has(el.parentElement) ? idx.get(el.parentElement) : -1,
      x: r.left + scrollX, y: r.top + scrollY, w: r.width, h: r.height,
      pad: [px(cs.paddingTop), px(cs.paddingRight), px(cs.paddingBottom), px(cs.paddingLeft)],
      mar: [px(cs.marginTop), px(cs.marginRight), px(cs.marginBottom), px(cs.marginLeft)],
      gap: [px(cs.rowGap), px(cs.columnGap)], disp: cs.display, pos: cs.position,
      rad: cs.borderTopLeftRadius, bw: px(cs.borderTopWidth) + px(cs.borderRightWidth) + px(cs.borderBottomWidth) + px(cs.borderLeftWidth),
      bc: cs.borderTopColor, bg: cs.backgroundColor, sh: cs.boxShadow,
      ff: cs.fontFamily, fs: px(cs.fontSize), fw: cs.fontWeight, lh: cs.lineHeight, ls: cs.letterSpacing, tt: cs.textTransform,
      td: cs.transitionDuration, te: cs.transitionTimingFunction, ad: cs.animationDuration, ae: cs.animationTimingFunction,
      own: own.slice(0, 160), nown: own.length, cls: el.getAttribute('class') || '',
      ov: cs.overflowX, sec: (function(x){ return x ? ((x.id ? '#' + x.id + ' ' : '') + x.className) : ''; })(el.closest('section')), link: el.tagName === 'A' || el.tagName === 'BUTTON',
      inpar: !!el.closest('p, li, h1, h2, h3, figcaption'),
    });
    n++;
  });
  document.querySelectorAll('img').forEach((im) => {
    const r = im.getBoundingClientRect();
    out.imgs.push({src: im.currentSrc || im.src, nw: im.naturalWidth, nh: im.naturalHeight, dw: r.width, dh: r.height,
      wa: im.hasAttribute('width') && im.hasAttribute('height'), lazy: im.loading === 'lazy', alt: im.hasAttribute('alt'),
      of: getComputedStyle(im).objectFit, top: r.top + scrollY, vis: r.width > 0 && r.height > 0});
  });
  return out;
}
"""

LOAD_JS = r"""
() => new Promise((res) => {
  const o = {lcp: 0, cls: 0};
  try {
    new PerformanceObserver((l) => { for (const e of l.getEntries()) o.lcp = e.startTime; }).observe({type: 'largest-contentful-paint', buffered: true});
    new PerformanceObserver((l) => { for (const e of l.getEntries()) if (!e.hadRecentInput) o.cls += e.value; }).observe({type: 'layout-shift', buffered: true});
  } catch (e) {}
  setTimeout(() => { const n = performance.getEntriesByType('navigation')[0] || {}; o.dcl = n.domContentLoadedEventEnd || 0; o.load = n.loadEventEnd || 0; res(o); }, 1200);
});
"""


class _Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def serve():
    h = functools.partial(_Quiet, directory=WEB)
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), h)
    srv.daemon_threads = True
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, "http://127.0.0.1:%d/" % srv.server_address[1]


def parser(desc):
    ap = argparse.ArgumentParser(description=desc)
    ap.add_argument("--page", action="append", help="only this page (repeatable), e.g. index.html")
    ap.add_argument("--viewport", action="append", choices=list(VIEWPORTS), help="default: phone and desktop")
    ap.add_argument("--all", action="store_true", help="include the generated library pages")
    ap.add_argument("--limit", type=int, default=60, help="rows to print, 0 for all")
    ap.add_argument("--json", help="also write the findings to this file")
    return ap


def run(checks, desc, want_load=False):
    """checks: functions (rec) -> [Finding]. rec = dict(page, vp, probe, load, bytes, reqs)."""
    from playwright.sync_api import sync_playwright
    args = parser(desc).parse_args()
    vps = args.viewport or ["phone", "desktop"]
    pl = args.page or pages(args.all)
    srv, base = serve()
    rep = Report()
    with sync_playwright() as p:
        b = p.chromium.launch(executable_path=CHROME)
        for vp in vps:
            w, h = VIEWPORTS[vp]
            ctx = b.new_context(viewport={"width": w, "height": h}, device_scale_factor=2 if vp == "phone" else 1, reduced_motion="no-preference")
            for rel in pl:
                pg = ctx.new_page(); pg.set_default_timeout(20000)
                sizes = {}
                def _size(r, s=sizes):
                    try:
                        s[r.url] = len(r.body()) if r.ok else 0
                    except Exception:
                        pass
                pg.on("response", _size)
                try:
                    pg.goto(base + rel, wait_until="load")
                    pg.add_style_tag(content=".reveal{opacity:1!important;transform:none!important}")
                    pg.wait_for_timeout(300)
                    load = pg.evaluate(LOAD_JS) if want_load else {}
                    probe = pg.evaluate(PROBE_JS)
                except Exception as ex:
                    rep.add(Finding("page-load", "error", rel, str(ex)[:80]), rel, vp)
                    pg.close(); continue
                rec = {"page": rel, "vp": vp, "w": w, "probe": probe, "load": load, "sizes": sizes, "base": base}
                for c in checks:
                    for f in c(rec):
                        rep.add(f, rel, vp)
                pg.close()
            ctx.close()
        b.close()
    srv.shutdown()
    if args.json:
        json.dump([{"rule": k[0], "sev": rep.sev[k], "where": k[1], "detail": k[2], "at": sorted(v)} for k, v in rep.items.items()], open(args.json, "w"), indent=1)
    errs = rep.print(args.limit or 10 ** 9)
    return 1 if errs else 0
