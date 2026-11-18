#!/usr/bin/env python3
"""Builds website/releases.html from tools/website/releases.json (and optional --fetch from GitHub API).

Usage:
  python3 tools/website/gen_releases.py            generate website/releases.html from tools/website/releases.json
  python3 tools/website/gen_releases.py --fetch    refresh tools/website/releases.json via `gh api` first, then build
  python3 tools/website/gen_releases.py --check    exit 1 if website/releases.html differs from generated output
"""
from __future__ import annotations

import datetime
import html
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import sync_chrome as chrome  # noqa: E402

DATA_PATH = os.path.join(HERE, "releases.json")
OUT_PATH = os.path.join(chrome.W, "releases.html")
REPO = "n1m21n/Infinite"

TITLE = "Infinite Release Notes: v0.1 to v2.0.0 | Infinite"
DESC = "Complete Infinite release notes from v0.1 to v2.0.0: 2D and 3D views, Timeline, Session, Field, audio DSP, plugins, headless CLI, and MCP."

PAGE_CSS = """
  <style>
    .rel-wrap { max-width: 920px; margin: 0 auto; padding: 120px var(--ui-sp-4, 16px) 80px; display: grid; gap: 32px; }
    .rel-head { display: grid; gap: 16px; }
    .rel-head h1 { margin: 0; font-size: var(--size-h1); font-weight: 800; letter-spacing: -0.035em; line-height: 1.05; }
    .rel-head h1 em { font-style: italic; color: var(--accent-ink); font-family: var(--font-hand, "Caveat", cursive); font-weight: 700; font-size: 1.08em; }
    .rel-lead { margin: 0; max-width: 62ch; color: var(--muted); font-size: var(--size-body); line-height: 1.65; }
    .rel-actions { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; }

    .rel-controls { display: grid; gap: 12px; padding: 16px; border-radius: var(--r-card, 20px); background: var(--card); border: 1px solid color-mix(in srgb, var(--ink) 12%, transparent); }
    .rel-eras { display: flex; flex-wrap: wrap; gap: 8px; }
    .rel-era { min-height: 36px; padding: 0 16px; border-radius: var(--r-pill, 999px); border: 1px solid color-mix(in srgb, var(--ink) 16%, transparent); background: transparent; color: var(--ink2, var(--muted)); font: 500 0.875rem/1 var(--font-sans); cursor: pointer; display: inline-flex; align-items: center; gap: 8px; transition: background-color 167ms var(--ease-out), color 167ms var(--ease-out), border-color 167ms var(--ease-out); }
    .rel-era:hover { color: var(--ink); border-color: color-mix(in srgb, var(--ink) 30%, transparent); }
    .rel-era[aria-pressed="true"] { background: var(--ink); color: var(--paper); border-color: var(--ink); }
    .rel-era .rel-cnt { font-family: var(--font-mono); font-size: 0.75rem; opacity: 0.72; }

    .rel-jump { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .rel-jump-label { font-family: var(--font-mono); font-size: 0.75rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); margin-right: 4px; }
    .rel-pill { padding: 4px 10px; border-radius: 8px; border: 1px solid color-mix(in srgb, var(--ink) 12%, transparent); color: var(--ink2, var(--muted)); text-decoration: none; font-family: var(--font-mono); font-size: 0.8125rem; transition: color 125ms var(--ease-out), border-color 125ms var(--ease-out), background-color 125ms var(--ease-out); }
    .rel-pill:hover { color: var(--ink); border-color: color-mix(in srgb, var(--ink) 28%, transparent); background: color-mix(in srgb, var(--ink) 6%, transparent); }

    .rel-list { display: grid; gap: 24px; }
    .rel-card { scroll-margin-top: 96px; display: grid; gap: 16px; padding: 24px; border-radius: var(--r-card, 20px); background: var(--card); border: 1px solid color-mix(in srgb, var(--ink) 12%, transparent); }
    .rel-card[hidden] { display: none; }
    .rel-card.is-latest { border-color: color-mix(in srgb, var(--accent-ink) 45%, transparent); }

    .rel-card-head { display: flex; flex-wrap: wrap; align-items: baseline; justify-content: space-between; gap: 12px; padding-bottom: 12px; border-bottom: 1px solid color-mix(in srgb, var(--ink) 10%, transparent); }
    .rel-card-title-row { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; }
    .rel-card-head h2 { margin: 0; font-size: var(--size-h2, 1.5rem); font-weight: 800; letter-spacing: -0.025em; line-height: 1.2; }
    .rel-card-head h2 a { color: inherit; text-decoration: none; }
    .rel-card-head h2 a:hover { color: var(--accent-ink); }
    .rel-badge { display: inline-flex; align-items: center; padding: 4px 10px; border-radius: var(--r-pill, 999px); background: color-mix(in srgb, var(--accent-ink) 18%, transparent); color: var(--accent-ink); font-family: var(--font-mono); font-size: 0.75rem; font-weight: 600; letter-spacing: 0.06em; text-transform: uppercase; }
    .rel-meta { display: flex; flex-wrap: wrap; align-items: center; gap: 12px; font-family: var(--font-mono); font-size: 0.8125rem; color: var(--muted); }
    .rel-meta a { color: var(--accent-ink); text-decoration: none; }
    .rel-meta a:hover { text-decoration: underline; }

    .rel-body { display: grid; gap: 12px; }
    .rel-body h3 { margin: 8px 0 0; font-size: var(--size-h3, 1.125rem); font-weight: 700; letter-spacing: -0.015em; color: var(--ink); }
    .rel-body h4 { margin: 4px 0 0; font-size: 1rem; font-weight: 600; color: var(--ink); }
    .rel-body p, .rel-body li { margin: 0; color: var(--ink2, var(--muted)); font-size: 0.9375rem; line-height: 1.65; }
    .rel-body strong { color: var(--ink); font-weight: 600; }
    .rel-body a { color: var(--accent-ink); }
    .rel-body ul { margin: 0; padding-left: 1.35em; display: grid; gap: 8px; }
    .rel-body li > ul { margin-top: 8px; }
    .rel-body code { font-family: var(--font-mono); font-size: 0.875em; padding: 2px 6px; border-radius: 6px; background: color-mix(in srgb, var(--ink) 8%, transparent); color: var(--ink); word-break: break-word; }

    @media (max-width: 640px) {
      .rel-wrap { padding: 104px 16px 64px; gap: 24px; }
      .rel-card { padding: 16px; }
    }
  </style>"""


def fetch_releases():
    raw = subprocess.check_output(
        ["gh", "api", f"repos/{REPO}/releases?per_page=100"],
        text=True,
    )
    items = json.loads(raw)
    out = []
    for r in items:
        if r.get("tag_name") == "extensions" or r.get("draft"):
            continue
        out.append({
            "tag": r["tag_name"],
            "name": r.get("name") or r["tag_name"],
            "published_at": r["published_at"],
            "url": r["html_url"],
            "body": (r.get("body") or "").replace("\r\n", "\n").strip(),
        })
    with open(DATA_PATH, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"fetched {len(out)} releases into {DATA_PATH}")


def fmt_inline(text: str) -> str:
    tokens: list[str] = []

    def hold(rendered: str) -> str:
        idx = len(tokens)
        tokens.append(rendered)
        return f"\x00T{idx}\x00"

    # Extract code spans first
    def repl_code(m: re.Match) -> str:
        return hold(f"<code>{html.escape(m.group(1))}</code>")

    s = re.sub(r"`([^`\n]+)`", repl_code, text)

    # Extract markdown links [label](url)
    def repl_md_link(m: re.Match) -> str:
        label = html.escape(m.group(1))
        url = html.escape(m.group(2), quote=True)
        return hold(f'<a href="{url}" target="_blank" rel="noopener">{label}</a>')

    s = re.sub(r"\[([^\]]+)\]\((https?://[^)\s]+)\)", repl_md_link, s)

    # Escape remaining plain text
    s = html.escape(s)

    # Auto-link bare URLs in remaining text
    def repl_bare_url(m: re.Match) -> str:
        url = m.group(1)
        trail = ""
        while url and url[-1] in ".,;:!?)":
            trail = url[-1] + trail
            url = url[:-1]
        return f'<a href="{url}" target="_blank" rel="noopener">{url}</a>{trail}'

    s = re.sub(r"(https?://[^\s<]+)", repl_bare_url, s)

    # Bold (**...**)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)

    # Restore tokens
    def restore(m: re.Match) -> str:
        return tokens[int(m.group(1))]

    return re.sub(r"\x00T(\d+)\x00", restore, s)


def strip_leading_title(body: str, tag: str) -> tuple[str, str]:
    lines = body.replace("\r\n", "\n").splitlines()
    subtitle = ""
    if lines and lines[0].strip().startswith("#"):
        first = lines[0].strip().lstrip("#").strip()
        if tag == "v0.1" and "—" in first:
            subtitle = first.split("—", 1)[1].strip()
        lines = lines[1:]
        while lines and not lines[0].strip():
            lines.pop(0)
    return "\n".join(lines), subtitle


def md_to_html(md: str) -> str:
    lines = md.splitlines()
    out: list[str] = []
    para: list[str] = []
    # list_stack holds tuples of (indent, has_open_li)
    list_stack: list[tuple[int, bool]] = []
    min_heading_level: int | None = None

    def flush_para():
        if para:
            out.append(f"<p>{fmt_inline(' '.join(para))}</p>")
            para.clear()

    def close_lists_to(target_indent: int):
        while list_stack and list_stack[-1][0] > target_indent:
            _, open_li = list_stack.pop()
            if open_li:
                out.append("</li>")
            out.append("</ul>")

    for raw in lines:
        s = raw.rstrip()
        stripped = s.strip()
        if not stripped:
            flush_para()
            close_lists_to(-1)
            continue

        hm = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if hm:
            flush_para()
            close_lists_to(-1)
            lvl = len(hm.group(1))
            if min_heading_level is None:
                min_heading_level = lvl
            # Map shallowest body heading to <h3>, deeper to <h4> (never skip heading levels under card <h2>)
            rel_lvl = 3 if lvl <= min_heading_level else 4
            out.append(f"<h{rel_lvl}>{fmt_inline(hm.group(2).strip())}</h{rel_lvl}>")
            continue

        lm = re.match(r"^(\s*)[-*]\s+(.*)$", s)
        if lm:
            flush_para()
            indent = len(lm.group(1))
            item = fmt_inline(lm.group(2).strip())
            if not list_stack or indent > list_stack[-1][0]:
                list_stack.append((indent, True))
                out.append(f"<ul>\n<li>{item}")
            else:
                close_lists_to(indent)
                if list_stack and list_stack[-1][0] == indent:
                    if list_stack[-1][1]:
                        out.append("</li>")
                    out.append(f"<li>{item}")
                    list_stack[-1] = (indent, True)
                else:
                    list_stack.append((indent, True))
                    out.append(f"<ul>\n<li>{item}")
            continue

        # Plain text line
        if list_stack:
            close_lists_to(-1)
        para.append(stripped)

    flush_para()
    close_lists_to(-1)
    return "\n".join(out)


def era_of(tag: str) -> str:
    if tag.startswith("v2."):
        return "v2"
    if tag.startswith("v1."):
        return "v1"
    if tag.startswith("v0.6"):
        return "v0.6"
    if tag.startswith("v0.5"):
        return "v0.5"
    if tag.startswith("v0.4"):
        return "v0.4"
    if tag.startswith("v0.3"):
        return "v0.3"
    return "v0.1-v0.2"


def fmt_date(iso: str) -> str:
    dt = datetime.datetime.strptime(iso[:10], "%Y-%m-%d")
    return dt.strftime("%-d %B %Y")


def card_title(r: dict, subtitle: str) -> str:
    tag = r["tag"]
    name = r["name"]
    if subtitle:
        return f"Infinite {tag} — {subtitle}"
    if name.startswith("Infinite "):
        return name
    return f"Infinite {tag}"


def build_html() -> str:
    releases = json.load(open(DATA_PATH, encoding="utf-8"))
    era_defs = [
        ("all", "All"),
        ("v2", "v2.0"),
        ("v1", "v1.x"),
        ("v0.6", "v0.6.x"),
        ("v0.5", "v0.5"),
        ("v0.4", "v0.4.x"),
        ("v0.3", "v0.3.x"),
        ("v0.1-v0.2", "v0.1 – v0.2"),
    ]
    counts = {k: 0 for k, _ in era_defs}
    counts["all"] = len(releases)
    for r in releases:
        counts[era_of(r["tag"])] += 1

    era_btns = []
    for k, label in era_defs:
        if k != "all" and counts[k] == 0:
            continue
        pressed = "true" if k == "all" else "false"
        era_btns.append(
            f'<button type="button" class="rel-era" data-era="{k}" aria-pressed="{pressed}">'
            f'{html.escape(label)} <span class="rel-cnt">{counts[k]}</span></button>'
        )

    jump_pills = [
        f'<a class="rel-pill" href="#{html.escape(r["tag"])}">{html.escape(r["tag"])}</a>'
        for r in releases
    ]

    cards = []
    for idx, r in enumerate(releases):
        tag = r["tag"]
        era = era_of(tag)
        body_md, subtitle = strip_leading_title(r["body"], tag)
        heading = card_title(r, subtitle)
        body_html = md_to_html(body_md)
        latest_cls = " is-latest" if idx == 0 else ""
        badge = '<span class="rel-badge">Latest</span>' if idx == 0 else ""
        date_str = fmt_date(r["published_at"])
        date_iso = r["published_at"][:10]
        gh_url = html.escape(r["url"], quote=True)
        tag_esc = html.escape(tag)
        cards.append(f"""      <article class="rel-card{latest_cls}" id="{tag_esc}" data-era="{era}">
        <div class="rel-card-head">
          <div class="rel-card-title-row">
            <h2 id="{tag_esc}-h"><a href="#{tag_esc}">{html.escape(heading)}</a></h2>
            {badge}
          </div>
          <div class="rel-meta">
            <time datetime="{date_iso}">{html.escape(date_str)}</time>
            <span>&middot;</span>
            <a href="{gh_url}" target="_blank" rel="noopener">GitHub release &rarr;</a>
          </div>
        </div>
        <div class="rel-body">
{body_html}
        </div>
      </article>""")

    json_ld = json.dumps({
        "@context": "https://schema.org",
        "@type": "CollectionPage",
        "name": "Infinite Release Notes",
        "url": "https://infinitedevices.org/releases.html",
        "description": DESC,
        "author": {"@type": "Person", "name": "Naman Soni", "alternateName": "n1m21n"},
        "about": {"@type": "SoftwareApplication", "name": "Infinite", "softwareVersion": releases[0]["tag"].lstrip("v")},
    }, ensure_ascii=False)

    h = f"""<!DOCTYPE html>
<html lang="en" data-mode="midnight">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#151930">
  <script>window.INFINITE_DEFAULT_MODE = 'midnight';</script>
  <script src="theme.js?v=2"></script>
  <title>{html.escape(TITLE)}</title>
  <meta name="description" content="{html.escape(DESC)}">
  <link rel="canonical" href="https://infinitedevices.org/releases.html">
  <meta property="og:title" content="{html.escape(TITLE)}">
  <meta property="og:description" content="{html.escape(DESC)}">
  <meta property="og:image" content="https://infinitedevices.org/assets/og/home.jpg">
  <meta property="og:url" content="https://infinitedevices.org/releases.html">
  <meta property="og:type" content="website">
  <meta property="og:site_name" content="Infinite">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{html.escape(TITLE)}">
  <meta name="twitter:description" content="{html.escape(DESC)}">
  <meta name="twitter:image" content="https://infinitedevices.org/assets/og/home.jpg">
  <script type="application/ld+json">{json_ld}</script>
  <link rel="alternate" type="text/plain" href="llms.txt" title="Plain-text summary for language models">
  <link rel="icon" href="assets/favicon.svg" type="image/svg+xml">
  <link rel="icon" href="assets/favicon-32.png" sizes="32x32">
  <link rel="apple-touch-icon" href="assets/apple-touch-icon.png">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Caveat:wght@600;700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <link rel="stylesheet" href="brand.css?v=07695caf">
  <link rel="stylesheet" href="style.css?v={chrome.CSS_VERSION}">
  <link rel="stylesheet" href="glass.css?v=c25dae44">
  <link rel="stylesheet" href="blog.css?v=823c9183">
  <link rel="stylesheet" href="audience.css?v=f73df30a">
{PAGE_CSS}
  <link rel="stylesheet" href="system.css?v={chrome.SYSTEM_VERSION}">
  <link rel="stylesheet" href="scale.css?v={chrome.SCALE_VERSION}">
  <link rel="stylesheet" href="neumo.css?v={chrome.NEUMO_VERSION}">
  <link rel="stylesheet" href="search.css?v={chrome.SEARCH_CSS_VERSION}">
</head>
<body class="blog-page">
  {chrome.header_html("releases.html")}

  <main>
    <div class="rel-wrap">
      <header class="rel-head">
        <h1 class="perch">Release <em>notes</em></h1>
        <p class="rel-lead">Every Infinite release from {html.escape(releases[-1]["tag"])} ({html.escape(fmt_date(releases[-1]["published_at"]))}) through {html.escape(releases[0]["tag"])} ({html.escape(fmt_date(releases[0]["published_at"]))}) — {len(releases)} versions covering the standalone DAW, VST3 &amp; AU plugin, 2D and 3D views, Timeline, Field, audio DSP, headless CLI, and MCP.</p>
        <div class="rel-actions">
          <a href="index.html#download" class="btn btn-primary btn-sm">Download {html.escape(releases[0]["tag"])}</a>
          <a href="plugin.html" class="btn btn-ghost btn-sm">VST3 &amp; AU plugin</a>
          <a href="https://github.com/n1m21n/Infinite/releases" target="_blank" rel="noopener" class="btn btn-ghost btn-sm">GitHub Releases</a>
        </div>
      </header>

      <section class="rel-controls" aria-label="Filter and jump to releases">
        <div class="rel-eras" role="group" aria-label="Filter by release series">
          {chr(10).join("          " + b for b in era_btns)}
        </div>
        <div class="rel-jump" aria-label="Jump to version">
          <span class="rel-jump-label">Jump to:</span>
          {"".join(jump_pills)}
        </div>
      </section>

      <section class="rel-list" aria-label="All releases">
{chr(10).join(cards)}
      </section>
    </div>
  </main>

  {chrome.footer_html("releases.html")}
  <script src="site.js?v={chrome.JS_VERSION}" defer></script>
  {chrome.dock_html("releases.html")}
  <script src="search.js?v={chrome.SEARCH_JS_VERSION}" defer></script>
  <script src="share.js?v={chrome.SHARE_JS_VERSION}" defer></script>
  <script src="bird.js?v={chrome.BIRD_VERSION}" defer></script>
  <script>
    (function () {{
      var buttons = Array.from(document.querySelectorAll('.rel-era'));
      var cards = Array.from(document.querySelectorAll('.rel-card'));
      function applyEra(era) {{
        buttons.forEach(function (btn) {{
          btn.setAttribute('aria-pressed', btn.getAttribute('data-era') === era ? 'true' : 'false');
        }});
        cards.forEach(function (card) {{
          card.hidden = era !== 'all' && card.getAttribute('data-era') !== era;
        }});
      }}
      buttons.forEach(function (btn) {{
        btn.addEventListener('click', function () {{
          applyEra(btn.getAttribute('data-era'));
        }});
      }});
      if (window.location.hash) {{
        var target = document.getElementById(window.location.hash.slice(1));
        if (target && target.classList.contains('rel-card')) {{
          applyEra('all');
        }}
      }}
    }})();
  </script>
</body>
</html>
"""
    # Normalize through sync_chrome so header/footer/search/share/bird placement matches every page
    return chrome.sync("releases.html", h)


def main():
    if "--fetch" in sys.argv:
        fetch_releases()
    rendered = build_html()
    if "--check" in sys.argv:
        current = open(OUT_PATH, encoding="utf-8").read() if os.path.exists(OUT_PATH) else ""
        if current != rendered:
            print("releases.html differs; run python3 tools/website/gen_releases.py")
            sys.exit(1)
        print("releases.html in sync")
        return
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(rendered)
    print(f"wrote {OUT_PATH} ({len(rendered)} bytes)")


if __name__ == "__main__":
    main()
