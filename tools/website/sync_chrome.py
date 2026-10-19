#!/usr/bin/env python3
"""Writes the one top bar menu and the one footer into every page of the website.

What: the menu (three groups: Learn, Create, Build, plus Download on phones) and the footer are defined here
      once. Every page that has `<header class="site-header">` gets the same menu with paths made relative to
      its folder, the current page marked `aria-current="page"`, and the same footer.
Why:  the pages are hand-written and had drifted: Embeds appeared on one page, Blog on three, Benchmarks on none,
      and the footer listed Blog twice. One definition cannot drift.
Usage: python3 tools/website/sync_chrome.py            rewrite the pages
       python3 tools/website/sync_chrome.py --check    exit 1 if any page differs (for the gate)
       from sync_chrome import header_html, footer_html   (gen_embeds.py builds its own page with these)
Exit codes: 0 in sync (or written), 1 a page differs under --check.
"""
import json, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = os.path.join(ROOT, "website")
GH = "https://github.com/n1m21n/Infinite"
CSS_VERSION = "89"   # style.css cache key; bump when the menu or footer CSS changes
JS_VERSION = "13"    # site.js cache key; bump when the menu script changes

# (group id, label, [(page path, label, hint, external)])
GROUPS = [
    ("for", "Who it's for", [
        ("artists.html", "Artists", "Images that move"),
        ("musicians.html", "Musicians", "Synths, effects and plugins"),
        ("vjs.html", "VJs", "Live visuals on stage"),
        ("creative-coders.html", "Creative coders", "Field, sketches and shaders"),
        ("developers.html", "Developers", "Headless CLI, Python SDK, MCP"),
    ]),
    ("explore", "Explore", [
        ("library.html", "Library", "Templates, patches and Field devices"),
        ("artworks.html", "Art gallery", "Made with Infinite"),
        ("embeds/", "Embeds", "Audio-reactive widgets for any page"),
        ("extensions.html", "Extensions", "Hand, face and pose tracking"),
        ("benchmarks.html", "Benchmarks", "Measured against other tools"),
    ]),
    ("learn", "Learn", [
        ("index.html#learn", "Get started", "Make a first sound and picture"),
        ("philosophy.html", "Philosophy", "Why Infinite is built this way"),
        ("blog/", "Blog", "Guides and how things work"),
        ("assets/Infinite_Node_Reference_Manual.pdf", "Node reference", "Every node, PDF"),
        ("assets/Field_Language_Manual.pdf", "Field language", "Write what a node does, PDF"),
        ("assets/Infinite_Headless_User_Guide.pdf", "Headless guide", "Run Infinite without a window, PDF"),
    ]),
]

FOOTER_COLS = [
    ("Who it's for", [
        ("artists.html", "Artists"), ("musicians.html", "Musicians"), ("vjs.html", "VJs"),
        ("creative-coders.html", "Creative coders"), ("developers.html", "Developers"),
    ]),
    ("Explore", [
        ("library.html", "Library"), ("artworks.html", "Art gallery"), ("embeds/", "Embeds"), ("extensions.html", "Extensions"),
        ("benchmarks.html", "Benchmarks"), ("assets/Infinite_Benchmark_Report.pdf", "Benchmark report (PDF)"),
        ("index.html#listen", "Listen"), ("index.html#nodes", "Node library"),
    ]),
    ("Learn", [
        ("index.html#learn", "Get started"), ("philosophy.html", "Philosophy"), ("blog/", "Blog"),
        ("assets/Infinite_Node_Reference_Manual.pdf", "Node reference manual (PDF)"),
        ("assets/Field_Language_Manual.pdf", "Field language manual (PDF)"),
        ("assets/Infinite_Headless_User_Guide.pdf", "Headless guide (PDF)"),
    ]),
    ("Open source", [
        (GH, "GitHub"), (GH + "/releases", "Releases"), (GH + "/tree/main/tools/mcp", "MCP server"),
        (GH + "/tree/main/tools/python_sdk", "Python SDK"), (GH + "/blob/main/AGENT.md", "Agent guide"),
        (GH + "/blob/main/ARCHITECTURE.md", "Architecture guide"), (GH + "/blob/main/LICENSE", "License"),
        ("https://discord.gg/7cpQfCxnx", "Discord"), ("https://www.youtube.com/watch?v=vXRjrDhSq24", "YouTube tutorial"),
        ("https://paypal.me/namansoniii", "Donate"),
    ]),
]

SOCIAL = [
    ("Instagram", "https://www.instagram.com/infinite.devices/",
     '<rect x="3" y="3" width="18" height="18" rx="5"/><circle cx="12" cy="12" r="4.2"/><circle cx="17.2" cy="6.8" r="1" fill="currentColor" stroke="none"/>'),
    ("YouTube", "https://www.youtube.com/@infinite.devices",
     '<rect x="2.5" y="5.5" width="19" height="13" rx="4"/><path d="M10 9.5v5l4.5-2.5z" fill="currentColor" stroke="none"/>'),
]


ICONS = [  # name, url, svg inner markup, filled glyph (brand marks) or stroked (line icons)
    ("GitHub", GH, '<path d="M12 .5C5.73.5.5 5.73.5 12c0 5.02 3.26 9.28 7.78 10.78.57.1.78-.25.78-.55 0-.27-.01-1.160-.02-2.1-3.17.69-3.84-1.35-3.84-1.35-.52-1.32-1.27-1.67-1.27-1.67-1.04-.71.08-.7.08-.7 1.15.08 1.75 1.18 1.750 1.18 1.02 1.75 2.68 1.24 3.33.95.1-.74.4-1.24.72-1.53-2.53-.29-5.19-1.27-5.19-5.63 0-1.24.44-2.26 1.17-3.06-.12-.29-.51-1.45.11-3.02 0 0 .96-.31 3.14 1.17a10.9 10.9 0 0 1 2.86-.39c.97 0 1.95.13 2.86.39 2.18-1.48 3.14-1.17 3.14-1.17.62 1.57.23 2.73.11 3.02.73.8 1.17 1.82 1.17 3.06 0 4.370-2.67 5.34-5.21 5.62.41.36.77 1.06.77 2.14 0 1.55-.01 2.79-.01 3.17 0 .3.2.66.79.55C20.24 21.27 23.5 17.01 23.5 12 23.5 5.73 18.27.5 12 .5z"/>', True),
    ("Discord", "https://discord.gg/7cpQfCxnx", '<path d="M20.317 4.492c-1.53-.69-3.17-1.2-4.885-1.49a.075.075 0 0 0-.079.036c-.211.375-.444.864-.608 1.25a18.27 18.27 0 0 0-5.487 0 12.64 12.64 0 0 0-.617-1.25.077.077 0 0 0-.079-.036A19.736 19.736 0 0 0 3.677 4.492a.07.07 0 0 0-.032.027C.533 9.093-.32 13.555.099 17.961a.082.082 0 0 0 .031.056 19.9 19.9 0 0 0 5.993 3.03.078.078 0 0 0 .084-.028c.462-.63.874-1.295 1.226-1.994a.076.076 0 0 0-.041-.106 13.107 13.107 0 0 1-1.872-.892.077.077 0 0 1-.008-.128 10.2 10.2 0 0 0 .372-.292.074.074 0 0 1 .077-.01c3.928 1.793 8.18 1.793 12.062 0a.074.074 0 0 1 .078.01c.12.098.246.198.373.292a.077.077 0 0 1-.006.127 12.299 12.299 0 0 1-1.873.892.076.076 0 0 0-.041.107c.36.698.772 1.362 1.225 1.993a.076.076 0 0 0 .084.028 19.839 19.839 0 0 0 6.002-3.030.077.077 0 0 0 .032-.055c.5-5.094-.838-9.52-3.549-13.442a.061.061 0 0 0-.031-.028zM8.02 15.278c-1.182 0-2.157-1.069-2.157-2.38 0-1.312.956-2.38 2.157-2.38 1.21 0 2.176 1.077 2.157 2.38 0 1.312-.956 2.38-2.157 2.38zm7.975 0c-1.183 0-2.157-1.069-2.157-2.38 0-1.312.955-2.38 2.157-2.38 1.21 0 2.176 1.077 2.157 2.38 0 1.312-.946 2.38-2.157 2.38z"/>', True),
] + [(n, u, d, False) for n, u, d in SOCIAL]


def icon_links(indent, label_fmt):
    """The round icon row used by the footer and the phone sheet: one definition."""
    out = []
    for n, u, d, filled in ICONS:
        attrs = 'fill="currentColor"' if filled else 'fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"'
        out.append(f'{indent}<a href="{u}" target="_blank" rel="noopener" aria-label="{label_fmt.format(n=n)}" title="{n}"><svg viewBox="0 0 24 24" {attrs} aria-hidden="true">{d}</svg></a>')
    return "\n".join(out)


def depth_prefix(rel):
    return "../" * rel.count("/")


def href(path, prefix):
    return path if path.startswith("http") else prefix + path


def link_attrs(path):
    a = ""
    if path.startswith("http"):
        a = ' target="_blank" rel="noopener"'
    elif path.endswith(".pdf"):
        a = ' target="_blank" rel="noopener"'
    return a


def is_current(path, rel):
    if path.endswith("/"):
        return rel == path + "index.html" or rel.startswith(path)
    return rel == path


def nav_html(rel):
    p = depth_prefix(rel)
    out = ['<nav class="nav-links" id="nav-links" aria-label="Main">']
    for gid, label, items in GROUPS:
        cur = any(is_current(i[0], rel) for i in items)
        out.append(f'      <div class="nav-group"{" data-current" if cur else ""}>')
        out.append(f'        <button type="button" class="nav-trigger" aria-expanded="false" aria-controls="nav-menu-{gid}">{label}</button>')
        out.append(f'        <div class="nav-menu" id="nav-menu-{gid}">')
        for path, text, hint in items:
            c = ' aria-current="page"' if is_current(path, rel) else ""
            out.append(f'          <a href="{href(path, p)}"{link_attrs(path)}{c}>{text}</a>')
        out.append("        </div>")
        out.append("      </div>")
    out.append('      <div class="nav-sheet-social">')
    out.append(icon_links("        ", "{n}"))
    out.append("      </div>")
    out.append("    </nav>")
    return "\n".join(out)


AUDIENCE_PAGES = {"artists.html": "artists", "musicians.html": "musicians", "vjs.html": "vjs",
                  "creative-coders.html": "creative-coders", "developers.html": "developers"}


def signup_html(rel):
    """The email sign-up box. Only drawn when website/signup.json exists ({"endpoint": ..., "turnstile_site_key": ...}),
    so the site never ships a form that does nothing. The Worker is worker/signup/."""
    path = os.path.join(W, "signup.json")
    if not os.path.exists(path):
        return ""
    cfg = json.load(open(path, encoding="utf-8"))
    p = depth_prefix(rel)
    aud = AUDIENCE_PAGES.get(rel, "general")
    ts = f' data-turnstile="{cfg["turnstile_site_key"]}"' if cfg.get("turnstile_site_key") else ""
    return f'''
          <form class="signup" data-signup data-endpoint="{cfg["endpoint"]}" data-audience="{aud}"{ts} novalidate>
            <label for="signup-email" class="signup-label">One email when something ships</label>
            <div class="signup-row">
              <input id="signup-email" name="email" type="email" inputmode="email" autocomplete="email" placeholder="you@example.com" required>
              <input name="company" class="signup-hp" tabindex="-1" autocomplete="off" aria-hidden="true">
              <button type="submit" class="btn btn-primary btn-sm">Sign up</button>
            </div>
            <p class="signup-note" role="status" aria-live="polite">We email only to confirm and when something ships. Unsubscribe in one click.</p>
          </form>
          <script src="{p}signup.js?v=1" defer></script>'''


def footer_html(rel):
    p = depth_prefix(rel)
    cols = []
    for title, items in FOOTER_COLS:
        links = "\n".join(
            f'          <a href="{href(path, p)}"{link_attrs(path)}>{text}</a>' for path, text in items)
        cols.append(f'        <div class="footer-col">\n          <p class="footer-h">{title}</p>\n{links}\n        </div>')
    social = icon_links("            ", "Infinite on {n}")
    return f'''<footer class="site-footer">
    <div class="container">
      <p class="footer-line perch">A DAW for <em class="hand">you</em><span class="dot-accent">.</span></p>

      <div class="footer-grid">
        <div class="footer-brand">
          <div class="brand">
            <img src="{p}assets/icon-96.png" alt="" class="brand-icon" width="30" height="30" loading="lazy" decoding="async">
            <span class="brand-name">Infinite</span>
          </div>
          <p>Visuals, music and code on one infinite canvas.</p>{signup_html(rel)}
          <div class="footer-social">
{social}
          </div>
        </div>
{chr(10).join(cols)}
      </div>
      <p class="footer-bottom mono">© 2026 Infinite, made by <a href="https://github.com/n1m21n" target="_blank" rel="noopener">Naman Soni (n1m21n)</a>. MIT source, GPLv3 binary by default (bundled VST3 hosting).</p>
    </div>
  </footer>'''


def header_html(rel, brand_href=None):
    """The whole <header>, for pages generated from scratch. Existing pages keep their own actions block."""
    p = depth_prefix(rel)
    idx = open(os.path.join(W, "index.html"), encoding="utf-8").read()
    h = re.search(r'<header class="site-header".*?</header>', idx, re.S).group(0)
    h = re.sub(r'(href|src)="(?!https?:|#|mailto:)([^"]*)"', lambda m: f'{m.group(1)}="{p}{m.group(2)}"', h)
    h = h.replace('href="#top"', f'href="{brand_href or p + "index.html"}"')
    h = h.replace('href="#download"', f'href="{p}index.html#download"')
    return re.sub(r'<nav class="nav-links".*?</nav>', lambda m: nav_html(rel), h, count=1, flags=re.S)


def pages():
    for dp, dn, fn in os.walk(W):
        dn[:] = [d for d in dn if d not in ("vendor", "assets")]
        for f in fn:
            if f.endswith(".html"):
                yield os.path.relpath(os.path.join(dp, f), W).replace(os.sep, "/")


def sync(rel, text):
    s = text
    if '<header class="site-header"' not in s:
        return s
    s = re.sub(r'<nav class="nav-links" id="nav-links".*?</nav>', lambda m: nav_html(rel), s, count=1, flags=re.S)
    if '<footer class="site-footer">' in s:
        s = re.sub(r'<footer class="site-footer">.*?</footer>', lambda m: footer_html(rel), s, count=1, flags=re.S)
    s = re.sub(r'(style\.css\?v=)\d+', r"\g<1>" + CSS_VERSION, s)
    s = re.sub(r'(site\.js\?v=)\d+', r"\g<1>" + JS_VERSION, s)
    # the brand link goes home from any page
    if rel != "index.html":
        s = re.sub(r'(<a href=")(?:\.\./)*#top(" class="brand")', lambda m: m.group(1) + depth_prefix(rel) + "index.html" + m.group(2), s)
    return s


def main():
    check = "--check" in sys.argv
    bad = []
    for rel in sorted(pages()):
        path = os.path.join(W, rel)
        old = open(path, encoding="utf-8").read()
        new = sync(rel, old)
        if new != old:
            bad.append(rel)
            if not check:
                open(path, "w", encoding="utf-8").write(new)
    print(("differ: " if check else "written: ") + (", ".join(bad) or "none"))
    return 1 if (check and bad) else 0


if __name__ == "__main__":
    sys.exit(main())
