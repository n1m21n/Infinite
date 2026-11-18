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
    .rel-wrap { max-width: 1120px; margin: 0 auto; padding: 120px var(--ui-sp-4, 16px) 80px; display: grid; gap: 40px; }
    .rel-head { display: grid; gap: 16px; max-width: 760px; }
    .rel-head h1 { margin: 0; font-size: var(--size-h1); font-weight: 800; letter-spacing: -0.035em; line-height: 1.05; }
    .rel-head h1 em { font-style: italic; color: var(--accent-ink); font-family: var(--font-hand, "Caveat", cursive); font-weight: 700; font-size: 1.08em; }
    .rel-lead { margin: 0; max-width: 62ch; color: var(--muted); font-size: var(--size-body); line-height: 1.65; }
    .rel-actions { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; }

    .rel-layout { display: grid; gap: 24px; align-items: start; }
    .rel-rail { display: none; }
    .rel-mobilebar { display: flex; gap: 12px; align-items: center; justify-content: space-between; }
    .rel-select { flex: 1; min-width: 0; min-height: 44px; padding: 0 14px; border-radius: 12px; border: 1px solid color-mix(in srgb, var(--ink) 16%, transparent); background: var(--card); color: var(--ink); font: 500 0.9375rem/1 var(--font-sans); }
    .rel-toggle { min-height: 44px; padding: 0 16px; border-radius: 12px; border: 1px solid color-mix(in srgb, var(--ink) 16%, transparent); background: transparent; color: var(--ink2, var(--muted)); font: 500 0.875rem/1 var(--font-sans); cursor: pointer; white-space: nowrap; }
    .rel-toggle:hover { color: var(--ink); border-color: color-mix(in srgb, var(--ink) 30%, transparent); }

    .rel-list { display: grid; gap: 12px; min-width: 0; }
    .rel-era-title { margin: 28px 0 4px; display: flex; align-items: baseline; gap: 12px; font-family: var(--font-mono); font-size: 0.75rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); scroll-margin-top: 96px; }
    .rel-era-title:first-child { margin-top: 0; }
    .rel-era-title::after { content: ""; flex: 1; height: 1px; background: color-mix(in srgb, var(--ink) 12%, transparent); }

    .rel-entry { scroll-margin-top: 96px; padding: 0; border-radius: var(--r-card, 20px); background: var(--card); border: 1px solid color-mix(in srgb, var(--ink) 12%, transparent); transition: border-color 125ms; }
    .rel-entry:hover { border-color: color-mix(in srgb, var(--ink) 24%, transparent); }
    .rel-entry.is-latest { border-color: color-mix(in srgb, var(--accent-ink) 45%, transparent); }
    .rel-entry[open] { border-color: color-mix(in srgb, var(--ink) 20%, transparent); }
    .rel-entry.is-latest[open] { border-color: color-mix(in srgb, var(--accent-ink) 45%, transparent); }

    .rel-sum { list-style: none; cursor: pointer; display: grid; grid-template-columns: 1fr auto; gap: 4px 16px; align-items: center; padding: 18px 24px; border-radius: inherit; }
    .rel-sum::-webkit-details-marker { display: none; }
    .rel-sum:focus-visible { outline: 2px solid var(--accent-ink); outline-offset: 2px; }
    .rel-entry-title-row { display: flex; flex-wrap: wrap; align-items: center; gap: 4px 12px; min-width: 0; }
    .rel-sum h2 { margin: 0; font-size: var(--size-h2, 1.5rem); font-weight: 800; letter-spacing: -0.025em; line-height: 1.2; }
    .rel-badge { display: inline-flex; align-items: center; padding: 4px 10px; border-radius: var(--r-pill, 999px); background: color-mix(in srgb, var(--accent-ink) 18%, transparent); color: var(--accent-ink); font-family: var(--font-mono); font-size: 0.75rem; }
    .rel-date { font-family: var(--font-mono); font-size: 0.8125rem; color: var(--muted); }
    .rel-chev { grid-column: 2; grid-row: 1; width: 20px; height: 20px; color: var(--muted); transition: transform 150ms; }
    .rel-entry[open] .rel-chev { transform: rotate(180deg); }
    .rel-tease { grid-column: 1 / -1; margin: 0; color: var(--muted); font-size: 0.9375rem; line-height: 1.55; display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden; }
    .rel-entry[open] .rel-tease { display: none; }

    .rel-body { display: grid; gap: 12px; margin: 0 24px; padding: 20px 0 24px; border-top: 1px solid color-mix(in srgb, var(--ink) 10%, transparent); }
    .rel-links { display: flex; flex-wrap: wrap; gap: 16px; font-family: var(--font-mono); font-size: 0.8125rem; }
    .rel-links a { color: var(--accent-ink); text-decoration: none; }
    .rel-links a:hover { text-decoration: underline; }
    .rel-body h3 { margin: 8px 0 0; font-size: var(--size-h3, 1.125rem); font-weight: 700; letter-spacing: -0.015em; color: var(--ink); }
    .rel-body h4 { margin: 4px 0 0; font-size: 1rem; font-weight: 600; color: var(--ink); }
    .rel-body p, .rel-body li { margin: 0; color: var(--ink2, var(--muted)); font-size: 0.9375rem; line-height: 1.65; }
    .rel-body strong { color: var(--ink); font-weight: 600; }
    .rel-body a { color: var(--accent-ink); }
    .rel-body ul { margin: 0; padding-left: 1.35em; display: grid; gap: 8px; }
    .rel-body li > ul { margin-top: 8px; }
    .rel-body code { font-family: var(--font-mono); font-size: 0.875em; padding: 2px 6px; border-radius: 6px; background: color-mix(in srgb, var(--ink) 8%, transparent); color: var(--ink); word-break: break-word; }
    .rel-entry:not([open]) .rel-body { display: none; }

    @media (min-width: 960px) {
      .rel-layout { grid-template-columns: 210px minmax(0, 1fr); gap: 48px; }
      .rel-mobilebar { display: none; }
      .rel-rail { display: block; position: sticky; top: 96px; max-height: calc(100vh - 120px); overflow: auto; padding-right: 8px; scrollbar-width: thin; }
      .rel-rail-top { display: flex; align-items: center; justify-content: space-between; margin-bottom: 12px; }
      .rel-rail-label { font-family: var(--font-mono); font-size: 0.75rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); }
      .rel-rail-top .rel-toggle { min-height: 32px; padding: 0 12px; font-size: 0.8125rem; }
      .rel-rail-group { margin: 0 0 14px; }
      .rel-rail-era { display: block; margin: 0 0 4px; padding: 4px 10px; font-family: var(--font-mono); font-size: 0.6875rem; letter-spacing: 0.08em; text-transform: uppercase; color: var(--muted); text-decoration: none; }
      .rel-rail-era:hover { color: var(--ink); }
      .rel-rail a.rel-ver { display: block; padding: 5px 10px; border-left: 2px solid color-mix(in srgb, var(--ink) 12%, transparent); color: var(--ink2, var(--muted)); text-decoration: none; font-family: var(--font-mono); font-size: 0.8125rem; transition: color 125ms, border-color 125ms, background 125ms; }
      .rel-rail a.rel-ver:hover { color: var(--ink); border-left-color: color-mix(in srgb, var(--ink) 40%, transparent); }
      .rel-rail a.rel-ver[aria-current="true"] { color: var(--ink); border-left-color: var(--accent-ink); background: color-mix(in srgb, var(--ink) 6%, transparent); }
    }
    @media (max-width: 640px) {
      .rel-wrap { padding: 104px 16px 64px; gap: 28px; }
      .rel-sum { padding: 16px; }
      .rel-body { margin: 0 16px; }
      .rel-body { padding-bottom: 16px; }
    }
    @media (prefers-reduced-motion: reduce) { .rel-chev, .rel-entry { transition: none; } }
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

    era_labels = dict(era_defs)
    era_labels["v2"] = "v2.0"
    groups = []  # (era, [releases]) in release order
    for r in releases:
        e = era_of(r["tag"])
        if not groups or groups[-1][0] != e:
            groups.append((e, []))
        groups[-1][1].append(r)

    rail = []
    sel = []
    for e, rs in groups:
        lab = html.escape(era_labels[e])
        rail.append(f'<div class="rel-rail-group"><a class="rel-rail-era" href="#era-{e}">{lab}</a>'
                    + "".join(f'<a class="rel-ver" href="#{html.escape(r["tag"])}">{html.escape(r["tag"])}</a>' for r in rs)
                    + '</div>')
        sel.append(f'<optgroup label="{lab}">'
                   + "".join(f'<option value="{html.escape(r["tag"])}">{html.escape(r["tag"])}</option>' for r in rs)
                   + '</optgroup>')

    chev = ('<svg class="rel-chev" aria-hidden="true" viewBox="0 0 20 20" fill="none" stroke="currentColor" '
            'stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M5 8l5 5 5-5"/></svg>')

    def teaser(body_html: str) -> str:
        m = re.search(r"<p>(.*?)</p>", body_html, re.S) or re.search(r"<li>(.*?)</li>", body_html, re.S)
        if not m:
            return ""
        t = html.unescape(re.sub(r"<[^>]+>", "", m.group(1))).strip()
        t = re.sub(r"\s+", " ", t)
        if len(t) > 220:
            t = t[:220].rsplit(" ", 1)[0] + "\u2026"
        return html.escape(t)

    cards = []
    for e, rs in groups:
        n = len(rs)
        cards.append(f'      <div class="rel-era-title" id="era-{e}">{html.escape(era_labels[e])} &middot; {n} release{"s" if n != 1 else ""}</div>')
        for r in rs:
            idx = releases.index(r)
            tag = r["tag"]
            body_md, subtitle = strip_leading_title(r["body"], tag)
            heading = card_title(r, subtitle)
            body_html = md_to_html(body_md)
            latest_cls = " is-latest" if idx == 0 else ""
            badge = '<span class="rel-badge">Latest</span>' if idx == 0 else ""
            open_attr = " open" if idx < 2 else ""
            date_str = fmt_date(r["published_at"])
            date_iso = r["published_at"][:10]
            gh_url = html.escape(r["url"], quote=True)
            tag_esc = html.escape(tag)
            tz = teaser(body_html)
            tz_html = f'\n          <p class="rel-tease">{tz}</p>' if tz else ""
            cards.append(f"""      <details class="rel-entry{latest_cls}" id="{tag_esc}" data-era="{e}"{open_attr}>
        <summary class="rel-sum">
          <div class="rel-entry-title-row">
            <h2 id="{tag_esc}-h">{html.escape(heading)}</h2>
            {badge}
            <time class="rel-date" datetime="{date_iso}">{html.escape(date_str)}</time>
          </div>
          {chev}{tz_html}
        </summary>
        <div class="rel-body">
          <div class="rel-links"><a href="{gh_url}" target="_blank" rel="noopener">GitHub release &rarr;</a><a href="#{tag_esc}">Permalink</a></div>
{body_html}
        </div>
      </details>""")

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

      <div class="rel-layout">
        <aside class="rel-rail" aria-label="Jump to version">
          <div class="rel-rail-top"><span class="rel-rail-label">Versions</span><button type="button" class="rel-toggle" data-toggle>Expand all</button></div>
          {"".join(rail)}
        </aside>
        <div class="rel-mobilebar">
          <select class="rel-select" aria-label="Jump to version"><option value="">Jump to version&hellip;</option>{"".join(sel)}</select>
          <button type="button" class="rel-toggle" data-toggle>Expand all</button>
        </div>
        <section class="rel-list" aria-label="All releases">
{chr(10).join(cards)}
        </section>
      </div>
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
      var cards = Array.from(document.querySelectorAll('.rel-entry'));
      var toggles = Array.from(document.querySelectorAll('[data-toggle]'));
      var vers = Array.from(document.querySelectorAll('.rel-ver'));
      var select = document.querySelector('.rel-select');
      function setLabel() {{
        var all = cards.every(function (c) {{ return c.open; }});
        toggles.forEach(function (t) {{ t.textContent = all ? 'Collapse all' : 'Expand all'; }});
      }}
      toggles.forEach(function (t) {{
        t.addEventListener('click', function () {{
          var all = cards.every(function (c) {{ return c.open; }});
          cards.forEach(function (c) {{ c.open = !all; }});
          setLabel();
        }});
      }});
      cards.forEach(function (c) {{ c.addEventListener('toggle', setLabel); }});
      function openHash() {{
        var id = decodeURIComponent(location.hash.slice(1));
        if (!id) return;
        var el = document.getElementById(id);
        if (el && el.classList.contains('rel-entry')) {{
          el.open = true;
          el.scrollIntoView();
        }}
      }}
      window.addEventListener('hashchange', openHash);
      if (select) select.addEventListener('change', function () {{
        if (select.value) location.hash = select.value;
      }});
      if ('IntersectionObserver' in window) {{
        var current = null;
        var io = new IntersectionObserver(function (entries) {{
          entries.forEach(function (en) {{
            if (!en.isIntersecting) return;
            current = en.target.id;
            vers.forEach(function (a) {{
              a.setAttribute('aria-current', a.getAttribute('href') === '#' + current ? 'true' : 'false');
            }});
            if (select && document.activeElement !== select) select.value = current;
          }});
        }}, {{ rootMargin: '-15% 0px -75% 0px' }});
        cards.forEach(function (c) {{ io.observe(c); }});
      }}
      setLabel();
      openHash();
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
