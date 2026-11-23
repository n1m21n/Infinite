#!/usr/bin/env python3
"""Build the web HTML versions of all three Infinite manuals from their structured sources.

Sources of truth (shared with the PDF builders; never scraped from PDFs):
  1. Node Reference Manual (/manual/):
     tools/manual/cards_base.json, tools/manual/cards.py, tools/manual/ui_chapter.py, tools/manual/shots.py
  2. Headless User Guide (/manual/headless/):
     docs/headless/GUIDE.md
  3. Field Language Manual (/manual/field/):
     docs/field/MANUAL.md

Outputs:
  website/manual/index.html            (and per-chapter pages if > 1.5 MB)
  website/manual/headless/index.html
  website/manual/field/index.html
  website/manual.css
  website/assets/manual/...            (sized WebPs derived from masters, never upscaled)
  tools/manual/slug_map.json           (slug-stability contract; build fails if an existing slug disappears)

Usage:
  python3 tools/manual/build_html.py
  python3 tools/manual/build_html.py --update-slugs
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import subprocess
import sys
from html.parser import HTMLParser
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WEB = os.path.join(ROOT, "website")
ASSETS_MAN = os.path.join(WEB, "assets", "manual")
SLUG_MAP_PATH = os.path.join(HERE, "slug_map.json")
BASE_URL = "https://infinitedevices.org"

sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "brand"))
sys.path.insert(0, os.path.join(ROOT, "tools", "website"))

import cards as new_cards  # noqa: E402
import paper_doc as P  # noqa: E402
import shots  # noqa: E402
import sync_chrome  # noqa: E402
import ui_chapter  # noqa: E402

esc = html.escape

# ---------------------------------------------------------------------------
# Helpers: slugs, git dates, asset hashes
# ---------------------------------------------------------------------------

def slugify(text: str) -> str:
    s = re.sub(r"<[^>]+>", "", html.unescape(text))
    s = re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")
    return s or "section"


class UniqueSlugger:
    """Generates deterministic, collision-free slugs within a single HTML document."""

    def __init__(self):
        self.used: set[str] = set()

    def take(self, base: str) -> str:
        s = slugify(base)
        if s not in self.used:
            self.used.add(s)
            return s
        i = 2
        while f"{s}-{i}" in self.used:
            i += 1
        out = f"{s}-{i}"
        self.used.add(out)
        return out


def git_dates(paths: list[str]) -> tuple[str, str]:
    """Return (datePublished, dateModified) in YYYY-MM-DD from git log for source files."""
    try:
        r = subprocess.run(
            ["git", "-C", ROOT, "log", "--follow", "--format=%cs", "--"] + paths,
            capture_output=True, text=True, check=True,
        )
        dates = [d.strip() for d in r.stdout.splitlines() if d.strip()]
        if dates:
            return dates[-1], dates[0]
    except Exception:
        pass
    return "2026-10-10", "2026-11-18"


def file_hash(rel_path: str) -> str:
    p = os.path.join(WEB, rel_path)
    if not os.path.exists(p):
        return "1"
    return hashlib.md5(open(p, "rb").read()).hexdigest()[:8]


# ---------------------------------------------------------------------------
# Image pipeline: derive sized WebPs from masters, never upscale
# ---------------------------------------------------------------------------

_IMG_CACHE: dict[tuple, tuple[str, int, int]] = {}


def ensure_webp(src_path: str, rel_out: str, max_w: int = 1200, max_h: int = 900, crop_box: tuple[int, int, int, int] | None = None) -> tuple[str, int, int]:
    """Convert/resize src_path to website/assets/manual/<rel_out> as WebP without upscaling.
    Returns (web_rel_from_website_root, width, height).
    """
    key = (src_path, rel_out, max_w, max_h, crop_box)
    if key in _IMG_CACHE:
        return _IMG_CACHE[key]
    out_abs = os.path.join(ASSETS_MAN, rel_out)
    os.makedirs(os.path.dirname(out_abs), exist_ok=True)
    with Image.open(src_path) as im:
        if crop_box is not None:
            im = im.crop(crop_box)
        w, h = im.size
        scale = min(1.0, max_w / w if max_w else 1.0, max_h / h if max_h else 1.0)
        nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
        if not os.path.exists(out_abs) or os.path.getmtime(out_abs) < os.path.getmtime(src_path):
            im2 = im.convert("RGBA") if im.mode in ("RGBA", "LA", "P") else im.convert("RGB")
            if (nw, nh) != (w, h):
                im2 = im2.resize((nw, nh), Image.LANCZOS)
            im2.save(out_abs, "WEBP", quality=84, method=6)
        else:
            with Image.open(out_abs) as existing:
                nw, nh = existing.size
    web_path = "assets/manual/" + rel_out.replace(os.sep, "/")
    _IMG_CACHE[key] = (web_path, nw, nh)
    return web_path, nw, nh


def copy_static_webp(src_path: str, rel_out: str) -> tuple[str, int, int]:
    """Ensure an already-WebP asset is available with its exact dimensions."""
    out_abs = os.path.join(ASSETS_MAN, rel_out)
    os.makedirs(os.path.dirname(out_abs), exist_ok=True)
    with Image.open(src_path) as im:
        w, h = im.size
        if not os.path.exists(out_abs) or os.path.getmtime(out_abs) < os.path.getmtime(src_path):
            im.save(out_abs, "WEBP", quality=86, method=6)
    return "assets/manual/" + rel_out.replace(os.sep, "/"), w, h


# ---------------------------------------------------------------------------
# Node name registry & prose cross-linker
# ---------------------------------------------------------------------------

def build_node_registry() -> tuple[dict[str, str], dict[str, str]]:
    """Return (exact_name_to_anchor, norm_name_to_anchor) for all nodes in the Node Reference Manual."""
    base = json.load(open(os.path.join(HERE, "cards_base.json"), encoding="utf-8"))
    all_chs = list(base) + list(new_cards.CHAPTERS)
    exact: dict[str, str] = {}
    norm: dict[str, str] = {}
    seen_slugs: set[str] = set()

    def reg(name: str, anchor: str):
        exact.setdefault(name, anchor)
        norm.setdefault(re.sub(r"[^a-z0-9]", "", name.lower()), anchor)

    for ch in all_chs:
        for c in ch["cards"]:
            name = c[0]
            s = "node-" + slugify(name)
            if s in seen_slugs:
                i = 2
                while f"{s}-{i}" in seen_slugs:
                    i += 1
                s = f"{s}-{i}"
            seen_slugs.add(s)
            reg(name, s)
            for part in re.split(r"\s*/\s*|\s*&\s*|,\s*", name):
                part = part.strip()
                if len(part) >= 3:
                    reg(part, s)
        for rname, _ in ch.get("rows", []):
            s = "node-" + slugify(rname)
            if s in seen_slugs:
                i = 2
                while f"{s}-{i}" in seen_slugs:
                    i += 1
                s = f"{s}-{i}"
            seen_slugs.add(s)
            reg(rname, s)
            for part in re.split(r"\s*/\s*|\s*&\s*|,\s*", rname):
                part = part.strip()
                if len(part) >= 3:
                    reg(part, s)

    # Also register gallery shots that don't have a card
    for tid, _, _, _ in shots.all_shots():
        nk = re.sub(r"[^a-z0-9]", "", tid.lower())
        if nk not in norm:
            s = "node-" + slugify(tid)
            reg(tid, s)
    return exact, norm


# Single words that are too common in English prose to auto-link unless multi-word or explicitly in node context
_AMBIGUOUS_SINGLE = {
    "Add", "And", "Any", "Array", "Audio", "Blend", "Blur", "Box", "Camera", "Chord", "Circle",
    "Clamp", "Clock", "Cloth", "Color", "Cone", "Constant", "Convert", "Crop", "Cube", "Curve",
    "Delay", "Display", "Divide", "Echo", "Edge", "Emboss", "Envelope", "Fade", "Feedback",
    "Filter", "Flatten", "Fluid", "Fog", "Fold", "Gain", "Gate", "Gear", "Glow", "Grade",
    "Grain", "Graph", "Grid", "Group", "Hold", "Hue", "Image", "Input", "Invert", "Key",
    "Knob", "Layer", "Levels", "Light", "Limiter", "Line", "Loop", "Looper", "Luma", "Map",
    "Mask", "Math", "Matrix", "Merge", "Meter", "Mirror", "Mix", "Mixer", "Modulo", "Motion",
    "Multiply", "Noise", "Normal", "Note", "Notes", "Null", "Object", "Ocean", "Offset",
    "Opacity", "Output", "Pad", "Paint", "Palette", "Pan", "Particles", "Path", "Phase",
    "Piano", "Pitch", "Pixel", "Plane", "Point", "Points", "Polygon", "Power", "Quantize",
    "Radial", "Ramp", "Random", "Range", "Reverb", "Ring", "Ripple", "Rotate", "Round",
    "Route", "Sample", "Scale", "Scatter", "Scene", "Screen", "Select", "Send", "Shape",
    "Shift", "Sine", "Sketch", "Slice", "Slider", "Smooth", "Snap", "Solo", "Sort", "Source",
    "Sphere", "Split", "Star", "State", "Step", "String", "Subtract", "Switch", "Symmetry",
    "Text", "Texture", "Threshold", "Time", "Timeline", "Timer", "Tone", "Torus", "Track",
    "Trails", "Transform", "Transpose", "Trigger", "Tube", "Twirl", "Value", "Video", "View",
    "Vignette", "Voice", "Volume", "Wave", "Waveform", "Weld", "Wire", "Wrap",
}


def link_nodes_in_html(fragment: str, exact_map: dict[str, str], prefix: str, skip_slug: str = "") -> str:
    """Link Title-Case node names in text nodes of an HTML fragment to `<prefix>#<slug>`.
    Skips inside <a>, <code>, <pre>, <h1>-<h4>, <figcaption>.
    """
    candidates = sorted(
        [k for k in exact_map if k not in _AMBIGUOUS_SINGLE and len(k) >= 4],
        key=len, reverse=True,
    )
    if not candidates:
        return fragment
    pattern = re.compile(r"\b(" + "|".join(re.escape(c) for c in candidates) + r")\b")

    # Split HTML into tags and text segments, tracking skip depth
    skip_tags = {"a", "code", "pre", "h1", "h2", "h3", "h4", "script", "style"}
    stack: list[str] = []
    out: list[str] = []
    pos = 0
    for m in re.finditer(r"<[^>]+>", fragment):
        txt = fragment[pos:m.start()]
        if txt and not any(t in skip_tags for t in stack):
            def _rep(nm):
                name = nm.group(1)
                target = exact_map.get(name)
                if not target or target == skip_slug:
                    return name
                return f'<a class="node-ref" href="{prefix}#{target}">{name}</a>'
            txt = pattern.sub(_rep, txt)
        out.append(txt)
        tag = m.group(0)
        out.append(tag)
        if tag.startswith("</"):
            tname = re.match(r"</\s*([a-zA-Z0-9]+)", tag)
            if tname and tname.group(1).lower() in stack:
                stack.remove(tname.group(1).lower())
        elif not tag.endswith("/>"):
            tname = re.match(r"<\s*([a-zA-Z0-9]+)", tag)
            if tname:
                tn = tname.group(1).lower()
                if tn in skip_tags:
                    stack.append(tn)
        pos = m.end()
    tail = fragment[pos:]
    if tail and not any(t in skip_tags for t in stack):
        tail = pattern.sub(
            lambda nm: nm.group(1) if exact_map.get(nm.group(1)) == skip_slug else f'<a class="node-ref" href="{prefix}#{exact_map[nm.group(1)]}">{nm.group(1)}</a>',
            tail,
        )
    out.append(tail)
    return "".join(out)


# ---------------------------------------------------------------------------
# Markdown -> Semantic HTML converter (for Headless & Field manuals)
# ---------------------------------------------------------------------------

def md_inline(s: str, cross_prefix: str = "../", exact_nodes: dict[str, str] | None = None) -> str:
    """Convert inline markdown (code, bold, italic, links) and auto-link external/cross-manual refs."""
    # Protect inline code first
    codes: list[str] = []

    def _save_code(m):
        codes.append(f"<code>{esc(m.group(1))}</code>")
        return f"\x00CODE{len(codes)-1}\x00"

    s = re.sub(r"`([^`]+)`", _save_code, s)

    # Markdown links [label](url)
    links: list[str] = []

    def _save_link(m):
        label, url = m.group(1), m.group(2)
        if url.startswith("http://") or url.startswith("https://"):
            links.append(f'<a href="{esc(url)}" target="_blank" rel="noopener">{esc(label)}</a>')
        else:
            links.append(f'<a href="{esc(url)}">{esc(label)}</a>')
        return f"\x00LINK{len(links)-1}\x00"

    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", _save_link, s)
    s = esc(s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"\*([^*]+)\*", r"<em>\1</em>", s)

    # Cross-link manual names in prose
    s = re.sub(
        r"\bField Language Manual\b",
        f'<a href="{cross_prefix}field/">Field Language Manual</a>',
        s,
    )
    s = re.sub(
        r"\bHeadless (?:Engine )?User Guide\b",
        f'<a href="{cross_prefix}headless/">Headless User Guide</a>',
        s,
    )
    s = re.sub(
        r"\bNode (?:Reference Manual|Field Guide)\b",
        f'<a href="{cross_prefix}">Node Reference Manual</a>',
        s,
    )

    # Restore links and code
    for i, l in enumerate(links):
        s = s.replace(f"\x00LINK{i}\x00", l)
    for i, c in enumerate(codes):
        s = s.replace(f"\x00CODE{i}\x00", c)

    if exact_nodes:
        node_prefix = "../" if cross_prefix == "../" else ""
        s = link_nodes_in_html(s, exact_nodes, node_prefix)
    return s


def anchor_btn(slug: str, label: str) -> str:
    return f'<a class="anchor-link" href="#{slug}" aria-label="Copy link to {esc(label)}" data-anchor="#{slug}">#</a>'


def convert_markdown_manual(
    md_text: str,
    slugger: UniqueSlugger,
    cross_prefix: str,
    exact_nodes: dict[str, str],
) -> tuple[str, list[dict]]:
    """Convert GUIDE.md or MANUAL.md into semantic HTML sections with TOC metadata."""
    lines = md_text.splitlines()
    out: list[str] = []
    toc: list[dict] = []
    in_section = False
    current_ch_idx = -1
    i = 0

    def close_section():
        nonlocal in_section
        if in_section:
            out.append("</section>")
            in_section = False

    while i < len(lines):
        line = lines[i]
        if line.startswith("## "):
            close_section()
            title = line[3:].strip()
            ch_id = slugger.take(title)
            current_ch_idx = len(toc)
            toc.append({"id": ch_id, "title": title, "children": []})
            out.append(f'<section class="man-chapter" id="{ch_id}" aria-labelledby="{ch_id}-h">')
            out.append(f'<h2 id="{ch_id}-h">{md_inline(title, cross_prefix)} {anchor_btn(ch_id, title)}</h2>')
            in_section = True
            i += 1
            continue

        if line.startswith("### "):
            title = line[4:].strip()
            sec_id = slugger.take(title)
            if current_ch_idx >= 0:
                toc[current_ch_idx]["children"].append({"id": sec_id, "title": title})
            out.append(f'<h3 id="{sec_id}">{md_inline(title, cross_prefix)} {anchor_btn(sec_id, title)}</h3>')
            i += 1
            continue

        if line.strip().startswith("![") and "](shot:" in line:
            m = re.match(r"^!\[([^\]]*)\]\(shot:([^)]+)\)\s*$", line.strip())
            if m:
                cap, name = m.group(1).strip(), m.group(2).strip()
                imgs = shots.find(name)[:1]
                if imgs:
                    tid, path, _, _ = imgs[0]
                    webp_rel, ww, wh = ensure_webp(path, f"nodes/{slugify(tid)}.webp", max_w=420, max_h=600)
                    fig_id = slugger.take(f"fig-{tid}")
                    out.append(
                        f'<figure class="man-fig" id="{fig_id}">'
                        f'<img src="../../{webp_rel}" width="{ww}" height="{wh}" alt="{esc(cap or tid)} node in Infinite." loading="lazy" decoding="async">'
                        f'<figcaption>{esc(cap or tid)} {anchor_btn(fig_id, cap or tid)}</figcaption></figure>'
                    )
                i += 1
                continue

        if "{{shot:" in line:
            shot_matches = re.findall(r"\{\{shot:[^|}]+\|([^}]+)\}\}", line)
            if shot_matches:
                figs = []
                for name in shot_matches:
                    imgs = shots.find(name.strip())[:1]
                    if imgs:
                        tid, path, _, _ = imgs[0]
                        webp_rel, ww, wh = ensure_webp(path, f"nodes/{slugify(tid)}.webp", max_w=320, max_h=450)
                        fig_id = slugger.take(f"fig-{tid}")
                        cap = f"{tid}: node in Infinite."
                        figs.append(
                            f'<figure class="man-fig" id="{fig_id}" style="display:inline-block;margin:0 12px 12px 0;vertical-align:top">'
                            f'<img src="../../{webp_rel}" width="{ww}" height="{wh}" alt="{esc(tid)} node in Infinite." loading="lazy" decoding="async">'
                            f'<figcaption>{esc(cap)} {anchor_btn(fig_id, cap)}</figcaption></figure>'
                        )
                if figs:
                    out.append(f'<div class="man-shot-row">{"".join(figs)}</div>')
                i += 1
                continue

        if line.startswith("```"):
            lang = line[3:].strip()
            buf = []
            i += 1
            while i < len(lines) and not lines[i].startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            cls = f' class="language-{esc(lang)}"' if lang else ""
            out.append(f'<pre><code{cls}>{esc(chr(10).join(buf))}</code></pre>')
            continue

        if line.startswith("|") and i + 1 < len(lines) and re.match(r"^\|\s*[-:]+", lines[i + 1]):
            tbl_lines = []
            while i < len(lines) and lines[i].startswith("|"):
                tbl_lines.append(lines[i])
                i += 1
            rows = [[c.strip() for c in r.strip("|").split("|")] for r in tbl_lines]
            head, body = rows[0], rows[2:]
            tbl_id = slugger.take("tbl-" + (head[0] if head else "table"))
            cap = f"{head[0]} reference" if head else "Reference table"
            th = "".join(f'<th scope="col">{md_inline(h, cross_prefix)}</th>' for h in head)
            trs = []
            for r in body:
                tds = []
                for idx, c in enumerate(r):
                    if idx == 0:
                        tds.append(f'<th scope="row">{md_inline(c, cross_prefix, exact_nodes)}</th>')
                    else:
                        tds.append(f"<td>{md_inline(c, cross_prefix, exact_nodes)}</td>")
                trs.append(f"<tr>{''.join(tds)}</tr>")
            out.append(
                f'<div class="table-wrap" id="{tbl_id}">'
                f'<table><caption>{esc(cap)} {anchor_btn(tbl_id, cap)}</caption>'
                f'<thead><tr>{th}</tr></thead><tbody>{"".join(trs)}</tbody></table></div>'
            )
            continue

        if re.match(r"^\s*[-*]\s+", line):
            items = []
            while i < len(lines) and re.match(r"^\s*[-*]\s+", lines[i]):
                items.append(re.sub(r"^\s*[-*]\s+", "", lines[i]).strip())
                i += 1
            out.append("<ul>" + "".join(f"<li>{md_inline(it, cross_prefix, exact_nodes)}</li>" for it in items) + "</ul>")
            continue

        if re.match(r"^\s*\d+\.\s+", line):
            items = []
            while i < len(lines) and re.match(r"^\s*\d+\.\s+", lines[i]):
                items.append(re.sub(r"^\s*\d+\.\s+", "", lines[i]).strip())
                i += 1
            out.append("<ol>" + "".join(f"<li>{md_inline(it, cross_prefix, exact_nodes)}</li>" for it in items) + "</ol>")
            continue

        if not line.strip():
            i += 1
            continue

        para = []
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "```", "|")) and not re.match(r"^\s*(?:[-*]|\d+\.)\s+", lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append(f'<p>{md_inline(" ".join(para), cross_prefix, exact_nodes)}</p>')

    close_section()
    return "\n".join(out), toc


# ---------------------------------------------------------------------------
# Node Reference Manual HTML builder
# ---------------------------------------------------------------------------

def build_node_manual_body(slugger: UniqueSlugger, exact_nodes: dict[str, str]) -> tuple[str, list[dict]]:
    base = json.load(open(os.path.join(HERE, "cards_base.json"), encoding="utf-8"))
    all_chs = [dict(c) for c in base] + [dict(c, intro=c["intro"], rows=[], notes=[]) for c in new_cards.CHAPTERS]

    toc: list[dict] = []
    sections_html: list[str] = []

    # Intro section
    intro_id = slugger.take("ch-overview")
    toc.append({"id": intro_id, "title": "What is Infinite?", "children": []})
    cables_html = "".join(
        f'<span class="cable-pill"><i style="background:{c}" aria-hidden="true"></i>{esc(n)}</span>'
        for n, c in P.CABLES
    )
    sections_html.append(f"""
<section class="man-chapter" id="{intro_id}" aria-labelledby="{intro_id}-h">
  <h2 id="{intro_id}-h">What even is Infinite? {anchor_btn(intro_id, 'What even is Infinite?')}</h2>
  <p>Infinite is a node-based studio for making sound and image at the same time. Instead of separate apps for music, visuals, and 3D, you drop blocks called <strong>nodes</strong> onto one canvas and connect them with cables: a synth feeding a filter, a shape feeding a kaleidoscope, a drumbeat feeding a bouncing particle field.</p>
  <p>Because everything lives on the same clock, sound and image can react to each other in real time: a kick drum can punch a shape, a camera's motion can bend a synth's pitch, a colour can become a chord. There is no "render and wait". You hear and see the result the instant you plug a cable in.</p>
  <p>This manual covers 347 spawnable node types across {len(all_chs)} node chapters, plus Chapter 29 on the app interface and a complete picture gallery. Looking to automate renders from the command line? See the <a href="headless/">Headless User Guide</a>. Want to write custom DSP, shaders, or geometry kernels? See the <a href="field/">Field Language Manual</a>.</p>
  <div class="cable-legend" aria-label="Cable colour legend">{cables_html}</div>
</section>
""")

    # Chapters 1..28
    for idx, ch in enumerate(all_chs):
        ch_num = ch["n"]
        ch_title = ch["title"]
        ch_id = slugger.take(f"ch-{ch_num:02d}-{ch_title}")
        toc_children = []
        intro_paras = ch["intro"] if isinstance(ch["intro"], list) else [ch["intro"]]

        parts = [f'<section class="man-chapter" id="{ch_id}" aria-labelledby="{ch_id}-h">']
        parts.append(f'<div class="man-kicker">Chapter {ch_num:02d}</div>')
        parts.append(f'<h2 id="{ch_id}-h">{esc(ch_title)} {anchor_btn(ch_id, ch_title)}</h2>')
        for p_i, p in enumerate(intro_paras):
            cls = ' class="man-lead"' if p_i == 0 else ""
            parts.append(f'<p{cls}>{link_nodes_in_html(esc(p), exact_nodes, "")}</p>')

        if ch_num == 20:
            parts.append('<p class="man-cross">Deep dive: <a href="field/">Read the Field Language Manual</a> for full syntax, domain transfer operators, and device building.</p>')

        if ch["cards"] or ch.get("rows"):
            sec_id = slugger.take(f"sec-{ch_num:02d}-nodes")
            parts.append(f'<h3 id="{sec_id}">Nodes in this chapter {anchor_btn(sec_id, f"Nodes in {ch_title}")}</h3>')

        for card in ch["cards"]:
            name, tag, desc, tryit, chain = card[0], card[1], card[2], card[3], card[4]
            node_id = slugger.take(f"node-{name}")
            toc_children.append({"id": node_id, "title": name})

            # Prepare node screenshot(s)
            imgs = shots.find(name)[:3]
            fig_html = ""
            if imgs:
                fig_id = slugger.take(f"fig-{name}")
                img_tags = []
                for tid, path, w, h in imgs:
                    webp_rel, ww, wh = ensure_webp(path, f"nodes/{slugify(tid)}.webp", max_w=420, max_h=600)
                    alt = f"{tid} node in Infinite showing its inputs, controls and output."
                    img_tags.append(
                        f'<img src="../{webp_rel}" width="{ww}" height="{wh}" alt="{esc(alt)}" loading="lazy" decoding="async">'
                    )
                cap_txt = f"{name}: {tag.lower()} node in Infinite."
                fig_html = (
                    f'<figure class="man-shot" id="{fig_id}">'
                    f'<div class="shot-imgs">{"".join(img_tags)}</div>'
                    f'<figcaption>{esc(cap_txt)} {anchor_btn(fig_id, cap_txt)}</figcaption>'
                    f'</figure>'
                )

            chain_html = '<span class="arrow" aria-hidden="true">&rarr;</span>'.join(
                f'<span class="chip">{link_nodes_in_html(esc(c), exact_nodes, "", skip_slug=node_id)}</span>' for c in chain
            )
            node_page_slug = slugify(name)
            node_page_link = ""
            if os.path.exists(os.path.join(WEB, "nodes", f"{node_page_slug}.html")):
                node_page_link = f'<a class="node-page-link" href="../nodes/{node_page_slug}.html">Node page &amp; patch &rarr;</a>'

            parts.append(
                f'<article class="man-node-card" id="{node_id}" aria-labelledby="{node_id}-h">'
                f'<div class="man-node-txt">'
                f'<div class="man-node-top"><h4 id="{node_id}-h">{esc(name)} {anchor_btn(node_id, name)}</h4><span class="man-tag">{esc(tag)}</span></div>'
                f'<p>{link_nodes_in_html(esc(desc), exact_nodes, "", skip_slug=node_id)}</p>'
                f'<div class="man-try"><strong>Try it</strong> {link_nodes_in_html(esc(tryit), exact_nodes, "", skip_slug=node_id)}'
                f'<div class="man-chain">{chain_html}</div></div>'
                f'{node_page_link}'
                f'</div>'
                f'{fig_html}'
                f'</article>'
            )

        for rname, rdesc in ch.get("rows", []):
            node_id = slugger.take(f"node-{rname}")
            toc_children.append({"id": node_id, "title": rname})
            imgs = shots.find(rname)[:1]
            shot_tag = ""
            if imgs:
                tid, path, _, _ = imgs[0]
                webp_rel, ww, wh = ensure_webp(path, f"nodes/{slugify(tid)}.webp", max_w=240, max_h=300)
                shot_tag = f'<img class="row-shot" src="../{webp_rel}" width="{ww}" height="{wh}" alt="{esc(tid)} node in Infinite." loading="lazy" decoding="async">'
            parts.append(
                f'<article class="man-node-row" id="{node_id}" aria-labelledby="{node_id}-h">'
                f'{shot_tag}'
                f'<div><h4 id="{node_id}-h">{esc(rname)} {anchor_btn(node_id, rname)}</h4>'
                f'<p>{link_nodes_in_html(esc(rdesc), exact_nodes, "", skip_slug=node_id)}</p></div>'
                f'</article>'
            )

        for note in ch.get("notes", []):
            parts.append(f'<p class="man-note">{link_nodes_in_html(esc(note), exact_nodes, "")}</p>')

        parts.append("</section>")
        sections_html.append("\n".join(parts))
        toc.append({"id": ch_id, "title": f"{ch_num:02d}. {ch_title}", "children": toc_children[:12]})

    # Chapter 29: UI Chapter (shared source with ui_chapter.py)
    ch29_id = slugger.take("ch-29-using-the-app")
    toc.append({"id": ch29_id, "title": "29. Using the App", "children": []})
    ch29_html = render_ui_chapter_web(slugger, exact_nodes)
    sections_html.append(
        f'<section class="man-chapter" id="{ch29_id}" aria-labelledby="{ch29_id}-h">'
        f'<div class="man-kicker">Chapter 29</div>'
        f'<h2 id="{ch29_id}-h">Using the App: Panels, Menus &amp; Settings {anchor_btn(ch29_id, "Using the App")}</h2>'
        f'{ch29_html}'
        f'</section>'
    )

    # Appendix: Gallery of Every Node
    gal_id = slugger.take("ch-gallery")
    toc.append({"id": gal_id, "title": "Gallery: Every Node", "children": []})
    cats: dict[str, list] = {}
    for tid, path, w, h in shots.all_shots():
        cats.setdefault(shots.category(tid), []).append((tid, path, w, h))
    gal_parts = [
        f'<section class="man-chapter" id="{gal_id}" aria-labelledby="{gal_id}-h">',
        '<div class="man-kicker">Appendix</div>',
        f'<h2 id="{gal_id}-h">Gallery: every node {anchor_btn(gal_id, "Gallery: every node")}</h2>',
        '<p class="man-lead">One picture of every node type, grouped by palette category. Names match Infinite RPC and patch files.</p>',
    ]
    for cat in sorted(cats):
        cat_id = slugger.take(f"gal-{cat}")
        gal_parts.append(f'<h3 id="{cat_id}">{esc(cat)} <span class="man-tag">{len(cats[cat])} nodes</span> {anchor_btn(cat_id, cat)}</h3>')
        gal_parts.append('<div class="man-gallery-grid">')
        for tid, path, _, _ in sorted(cats[cat], key=lambda x: x[0].lower()):
            webp_rel, ww, wh = ensure_webp(path, f"nodes/{slugify(tid)}.webp", max_w=280, max_h=300)
            cand_id = f"node-{slugify(tid)}"
            cell_id_attr = ""
            if cand_id not in slugger.used:
                slugger.used.add(cand_id)
                cell_id_attr = f' id="{cand_id}"'
            target_id = exact_nodes.get(tid, cand_id)
            gal_parts.append(
                f'<a class="gal-cell"{cell_id_attr} href="#{target_id}">'
                f'<img src="../{webp_rel}" width="{ww}" height="{wh}" alt="{esc(tid)} node in Infinite." loading="lazy" decoding="async">'
                f'<span class="gal-nm">{esc(tid)}</span></a>'
            )
        gal_parts.append("</div>")
    gal_parts.append("</section>")
    sections_html.append("\n".join(gal_parts))

    return "\n".join(sections_html), toc


def render_ui_chapter_web(slugger: UniqueSlugger, exact_nodes: dict[str, str]) -> str:
    """Adapt ui_chapter.html_chapter(P) output for semantic web HTML with WebP images, captions, and slugs."""
    raw = ui_chapter.html_chapter(P)
    # Strip the PDF chapter's duplicate h1 and kickers (we emit h2 for Chapter 29)
    raw = re.sub(r'<div class="kick">Chapter</div><div class="hand"[^>]*>.*?</div><h1>.*?</h1>', "", raw, count=1, flags=re.S)
    raw = raw.replace('<div style="break-before:page"></div>', "")
    # Downgrade h2 -> h3, h3 -> h4 so heading hierarchy under Chapter 29 <h2> is h2 -> h3 -> h4
    raw = re.sub(r"<h3>(.*?)</h3>", lambda m: f'<h4 id="{slugger.take(m.group(1))}">{m.group(1)}</h4>', raw, flags=re.S)
    raw = re.sub(r"<h2>(.*?)</h2>", lambda m: f'<h3 id="{slugger.take(m.group(1))}">{m.group(1)} {anchor_btn(slugify(m.group(1)), re.sub(r"<[^>]+>", "", m.group(1)))}</h3>', raw, flags=re.S)

    # Convert file:// PNGs (including ui_chapter cropped temp PNGs) to WebP in website/assets/manual/ui/
    def _rep_img(m):
        src = m.group(1)
        if src.startswith("file://"):
            src_path = src[len("file://"):]
            base = os.path.splitext(os.path.basename(src_path))[0]
            webp_rel, ww, wh = ensure_webp(src_path, f"ui/{slugify(base)}.webp", max_w=1200, max_h=900)
            return f'src="../{webp_rel}" width="{ww}" height="{wh}" loading="lazy" decoding="async"'
        return m.group(0)

    raw = re.sub(r'src="(file://[^"]+)"(?:\s+style="[^"]*")?', _rep_img, raw)

    # Add alt text from figcaption if missing
    def _add_alt(m):
        fig = m.group(0)
        cap_m = re.search(r"<figcaption>(.*?)</figcaption>", fig, flags=re.S)
        cap_txt = re.sub(r"<[^>]+>", "", cap_m.group(1)).strip() if cap_m else "Infinite app screenshot"
        fig_id = slugger.take("fig-" + cap_txt[:40])
        fig = fig.replace('<figure class="fig"', f'<figure class="man-fig" id="{fig_id}"', 1)
        if 'alt="' not in fig:
            fig = fig.replace("<img ", f'<img alt="{esc(cap_txt)}" ', 1)
        return fig

    raw = re.sub(r'<figure class="fig".*?</figure>', _add_alt, raw, flags=re.S)

    # Wrap tables in .table-wrap with <thead>, <tbody>, <th scope="col">, <th scope="row">
    def _wrap_table(m):
        tbl = m.group(0)
        tbl_id = slugger.take("tbl-ui")
        rows = re.findall(r"<tr>(.*?)</tr>", tbl, flags=re.S)
        if not rows:
            return tbl
        head_cells = re.findall(r"<th>(.*?)</th>", rows[0], flags=re.S)
        thead = "<thead><tr>" + "".join(f'<th scope="col">{c}</th>' for c in head_cells) + "</tr></thead>"
        body_rows = []
        for r in rows[1:]:
            cells = re.findall(r"<td>(.*?)</td>", r, flags=re.S)
            tds = []
            for idx, c in enumerate(cells):
                if idx == 0:
                    tds.append(f'<th scope="row">{c}</th>')
                else:
                    tds.append(f"<td>{link_nodes_in_html(c, exact_nodes, '')}</td>")
            body_rows.append("<tr>" + "".join(tds) + "</tr>")
        cap = head_cells[0] + " reference" if head_cells else "Interface reference"
        return f'<div class="table-wrap" id="{tbl_id}"><table><caption>{esc(cap)}</caption>{thead}<tbody>{"".join(body_rows)}</tbody></table></div>'

    raw = re.sub(r'<table class="t">.*?</table>', _wrap_table, raw, flags=re.S)
    return raw


# ---------------------------------------------------------------------------
# Page shell: SEO, JSON-LD, Critical CSS, TOC, Prev/Next
# ---------------------------------------------------------------------------

CRITICAL_CSS = """
.skip-link{position:absolute;left:-9999px;top:0;background:var(--accent-ink);color:var(--on-accent);padding:10px 16px;border-radius:999px;font-weight:600;z-index:200}
.skip-link:focus{left:16px;top:16px}
.man-shell{max-width:1240px;margin:0 auto;padding:24px 16px 64px;display:grid;grid-template-columns:260px minmax(0,1fr);gap:32px;align-items:start}
@media(max-width:960px){.man-shell{grid-template-columns:minmax(0,1fr);gap:16px}}
.man-toc{position:sticky;top:84px;max-height:calc(100vh - 104px);overflow-y:auto;background:var(--surface-1);border-radius:var(--r-md,24px);padding:18px;box-shadow:var(--shadow-1)}
@media(max-width:960px){.man-toc{position:static;max-height:none}}
.man-main{min-width:0}
.man-hero{background:var(--surface-1);border-radius:var(--r-lg,24px);padding:28px;margin-bottom:24px;box-shadow:var(--shadow-1);display:grid;grid-template-columns:minmax(0,1fr) auto;gap:24px;align-items:center}
@media(max-width:720px){.man-hero{grid-template-columns:minmax(0,1fr);padding:20px}}
.man-hero h1{margin:6px 0 12px;font-size:clamp(2rem,4vw,3.2rem);line-height:1.05;letter-spacing:-0.035em}
"""


def add_chapter_prev_next(body_html: str, toc: list[dict]) -> str:
    """Inject previous/next chapter navigation at the end of each <section class="man-chapter">."""
    if len(toc) < 2:
        return body_html
    nav_by_id: dict[str, str] = {}
    for idx, item in enumerate(toc):
        links = []
        if idx > 0:
            prev = toc[idx - 1]
            links.append(f'<a class="chap-prev" href="#{prev["id"]}">&larr; {esc(prev["title"])}</a>')
        links.append('<a class="chap-top" href="#top">&uarr; Top</a>')
        if idx + 1 < len(toc):
            nxt = toc[idx + 1]
            links.append(f'<a class="chap-next" href="#{nxt["id"]}">{esc(nxt["title"])} &rarr;</a>')
        nav_by_id[item["id"]] = f'<nav class="chap-nav" aria-label="Chapter navigation">{"".join(links)}</nav>'

    # Match each top-level chapter section's closing tag in order
    idx_iter = iter(toc)
    def _rep(m):
        try:
            item = next(idx_iter)
            return nav_by_id.get(item["id"], "") + m.group(0)
        except StopIteration:
            return m.group(0)
    return re.sub(r"</section>", _rep, body_html)


def render_page(
    *,
    rel_file: str,
    rel_url: str,
    root_prefix: str,
    title: str,
    description: str,
    h1: str,
    kicker: str,
    lead: str,
    hero_art_src: str,
    hero_art_w: int,
    hero_art_h: int,
    hero_art_alt: str,
    pdf_href: str,
    pdf_label: str,
    body_html: str,
    toc: list[dict],
    breadcrumbs: list[tuple[str, str]],
    date_pub: str,
    date_mod: str,
    og_image_url: str,
    other_manuals: list[tuple[str, str]],
) -> str:
    canonical = f"{BASE_URL}/{rel_url}"
    assert len(title) <= 60, f"Title too long ({len(title)}): {title}"
    assert len(description) <= 155, f"Description too long ({len(description)}): {description}"

    tech_ld = json.dumps({
        "@context": "https://schema.org",
        "@type": "TechArticle",
        "headline": title,
        "description": description,
        "url": canonical,
        "image": og_image_url,
        "author": {"@type": "Person", "name": "Naman Soni", "url": "https://github.com/n1m21n"},
        "publisher": {"@type": "Organization", "name": "Infinite", "url": f"{BASE_URL}/"},
        "datePublished": date_pub,
        "dateModified": date_mod,
        "version": "2.0.0",
        "inLanguage": "en",
        "isAccessibleForFree": True,
        "about": {
            "@type": "SoftwareApplication",
            "name": "Infinite",
            "url": f"{BASE_URL}/",
            "applicationCategory": "MultimediaApplication",
            "operatingSystem": "macOS, Windows, Linux",
        },
        "mainEntityOfPage": canonical,
    }, ensure_ascii=False)

    bc_ld = json.dumps({
        "@context": "https://schema.org",
        "@type": "BreadcrumbList",
        "itemListElement": [
            {"@type": "ListItem", "position": i, "name": name, "item": url}
            for i, (name, url) in enumerate(breadcrumbs, 1)
        ],
    }, ensure_ascii=False)

    toc_items = []
    for item in toc:
        sub = ""
        if item["children"]:
            sub = "<ul class=\"toc-sub\">" + "".join(
                f'<li><a href="#{c["id"]}">{esc(c["title"])}</a></li>' for c in item["children"]
            ) + "</ul>"
        toc_items.append(f'<li><a href="#{item["id"]}">{esc(item["title"])}</a>{sub}</li>')

    cross_links = "".join(
        f'<a class="btn btn-ghost" href="{href}">{esc(lbl)}</a>' for href, lbl in other_manuals
    )

    hero_img_html = (
        f'<img class="man-hero-art" src="{hero_art_src}" width="{hero_art_w}" height="{hero_art_h}" '
        f'alt="{esc(hero_art_alt)}" decoding="async">'
    )
    body_with_nav = add_chapter_prev_next(body_html, toc)
    site_header = sync_chrome.header_html(rel_file)
    site_footer = sync_chrome.footer_html(rel_file)
    ext_links = (
        '<div class="man-ext-links">'
        '<a href="https://github.com/n1m21n/Infinite/releases" target="_blank" rel="noopener">Release notes</a>'
        '<a href="https://github.com/n1m21n/Infinite" target="_blank" rel="noopener">GitHub repository</a>'
        '<a href="https://www.npmjs.com/package/infinite-engine" target="_blank" rel="noopener">npm package</a>'
        '<a href="https://github.com/n1m21n/Infinite/tree/main/tools/mcp" target="_blank" rel="noopener">MCP server</a>'
        '<a href="https://github.com/n1m21n/Infinite/blob/main/LICENSE" target="_blank" rel="noopener">MIT / GPLv3 licences</a>'
        '</div>'
    )

    return f"""<!DOCTYPE html>
<html lang="en" data-mode="paper">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
  <meta name="theme-color" content="#FBFAF6">
  <script>window.INFINITE_DEFAULT_MODE = 'paper';</script>
  <script src="{root_prefix}theme.js?v=2"></script>
  <title>{esc(title)}</title>
  <meta name="description" content="{esc(description)}">
  <link rel="canonical" href="{canonical}">
  <meta property="og:title" content="{esc(title)}">
  <meta property="og:description" content="{esc(description)}">
  <meta property="og:image" content="{og_image_url}">
  <meta property="og:url" content="{canonical}">
  <meta property="og:type" content="article">
  <meta property="og:site_name" content="Infinite">
  <meta name="twitter:card" content="summary_large_image">
  <meta name="twitter:title" content="{esc(title)}">
  <meta name="twitter:description" content="{esc(description)}">
  <meta name="twitter:image" content="{og_image_url}">
  <link rel="alternate" type="text/plain" href="{root_prefix}llms.txt" title="Plain-text summary for language models">
  <link rel="icon" href="{root_prefix}assets/favicon.svg" type="image/svg+xml">
  <link rel="icon" href="{root_prefix}assets/favicon-32.png" sizes="32x32">
  <link rel="apple-touch-icon" href="{root_prefix}assets/apple-touch-icon.png">
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Caveat:wght@600;700&family=Geist:wght@400;500;600;700;800&family=Geist+Mono:wght@400;500;600&display=swap" rel="stylesheet">
  <style>{CRITICAL_CSS}</style>
  <link rel="stylesheet" href="{root_prefix}brand.css?v={file_hash('brand.css')}">
  <link rel="stylesheet" href="{root_prefix}style.css?v={file_hash('style.css')}">
  <link rel="stylesheet" href="{root_prefix}glass.css?v={file_hash('glass.css')}">
  <link rel="stylesheet" href="{root_prefix}system.css?v={file_hash('system.css')}">
  <link rel="stylesheet" href="{root_prefix}scale.css?v={file_hash('scale.css')}">
  <link rel="stylesheet" href="{root_prefix}search.css?v={file_hash('search.css')}">
  <link rel="stylesheet" href="{root_prefix}manual.css?v={file_hash('manual.css')}">
  <script type="application/ld+json">{tech_ld}</script>
  <script type="application/ld+json">{bc_ld}</script>
</head>
<body class="manual-page">
  <a class="skip-link" href="#main-content">Skip to content</a>
  {site_header}

  <div class="man-shell" id="top">
    <aside class="man-toc" aria-label="Manual contents">
      <details class="toc-details" open>
        <summary class="toc-summary">Contents</summary>
        <nav aria-label="Table of contents">
          <ol class="toc-list">{"".join(toc_items)}</ol>
        </nav>
      </details>
      <a class="back-top" href="#top">&uarr; Back to top</a>
    </aside>

    <main class="man-main" id="main-content">
      <article>
        <header class="man-hero">
          <div>
            <div class="man-kicker">{esc(kicker)}</div>
            <h1>{esc(h1)}</h1>
            <p class="man-lead">{esc(lead)}</p>
            <div class="man-hero-actions">
              <a class="btn btn-primary" href="{pdf_href}" download>{esc(pdf_label)}</a>
              {cross_links}
            </div>
          </div>
          {hero_img_html}
        </header>
        {body_with_nav}
        <nav class="man-pager" aria-label="Other Infinite manuals">
          <a class="btn btn-ghost" href="#top">&uarr; Back to top</a>
          {cross_links}
        </nav>
        {ext_links}
      </article>
    </main>
  </div>

  {site_footer}
  <script src="{root_prefix}site.js?v=21"></script>
  <script src="{root_prefix}search.js?v={file_hash('search.js')}"></script>
  <script>
  (function(){{
    if (window.innerWidth <= 960) {{
      var d = document.querySelector('.toc-details');
      if (d) d.removeAttribute('open');
    }}
    document.addEventListener('click', function(e) {{
      var a = e.target.closest && e.target.closest('.anchor-link');
      if (!a) return;
      var url = location.href.split('#')[0] + a.getAttribute('href');
      if (navigator.clipboard) navigator.clipboard.writeText(url).catch(function(){{}});
    }});
  }})();
  </script>
</body>
</html>
"""


# ---------------------------------------------------------------------------
# CSS for website/manual.css (uses brand.css & system.css tokens)
# ---------------------------------------------------------------------------

MANUAL_CSS = """/* Generated by tools/manual/build_html.py. Uses brand.css & system.css tokens. */
body.manual-page { overflow-x: clip; font-family: var(--font-sans); background: var(--ground); color: var(--ink); }
.man-kicker { font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: var(--size-micro, 12px); letter-spacing: 0; color: var(--signal); }
.man-lead { font-size: var(--size-lead, 1.15rem); line-height: 1.5; color: var(--ink-2); max-width: 68ch; margin: 0 0 16px; }
.man-hero-actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 16px; }
.man-hero-art { width: 180px; height: auto; border-radius: var(--r-md, 16px); display: block; }
@media (max-width: 720px) { .man-hero-art { width: 120px; } }

.toc-summary { font-weight: 700; font-size: 1rem; cursor: pointer; list-style: none; display: flex; justify-content: space-between; align-items: center; }
.toc-list { list-style: none; padding: 0; margin: 12px 0 0; display: grid; gap: 6px; font-size: 0.9rem; }
.toc-list a { color: var(--ink-2); text-decoration: none; display: block; padding: 3px 0; }
.toc-list a:hover, .toc-list a:focus-visible { color: var(--signal); text-decoration: underline; }
.toc-sub { list-style: none; padding-left: 12px; margin: 4px 0 6px; border-left: 1px solid var(--line); display: grid; gap: 3px; font-size: 0.82rem; }
.back-top { display: inline-block; margin-top: 14px; font-size: 0.85rem; font-family: var(--font-sans, 'Geist', system-ui, sans-serif); color: var(--signal); text-decoration: none; }

.man-chapter { background: var(--surface-1); border-radius: var(--r-lg, 24px); padding: 28px; margin-bottom: 24px; box-shadow: var(--shadow-1); }
@media (max-width: 720px) { .man-chapter { padding: 18px; } }
.man-chapter h2 { font-size: var(--size-h2, clamp(1.5rem, 2.8vw, 2.2rem)); letter-spacing: -0.025em; margin: 6px 0 14px; scroll-margin-top: 90px; }
.man-chapter h3 { font-size: var(--size-h3, clamp(1.15rem, 1.8vw, 1.45rem)); margin: 24px 0 12px; color: var(--ink); scroll-margin-top: 90px; }
.man-chapter p { max-width: 70ch; line-height: 1.6; margin: 0 0 12px; }
.man-chapter ul, .man-chapter ol { max-width: 70ch; line-height: 1.6; margin: 0 0 14px; padding-left: 22px; }

.anchor-link { opacity: 0.25; margin-left: 6px; text-decoration: none; font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: 0.8em; color: var(--signal); transition: opacity var(--dur-micro, 125ms) ease; }
h2:hover .anchor-link, h3:hover .anchor-link, h4:hover .anchor-link, figcaption:hover .anchor-link, caption:hover .anchor-link, .anchor-link:focus-visible { opacity: 1; }
a:focus-visible, button:focus-visible, summary:focus-visible { outline: 2px solid var(--signal); outline-offset: 2px; border-radius: 4px; }

.cable-legend { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 16px; }
.cable-pill { display: inline-flex; align-items: center; gap: 8px; padding: 6px 12px; border-radius: var(--r-pill, 999px); background: var(--surface-3); font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: 0.85rem; }
.cable-pill i { display: inline-block; width: 18px; height: 5px; border-radius: 3px; }

.man-node-card { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 20px; align-items: start; background: var(--surface-3); border-radius: var(--r-md, 20px); padding: 20px; margin: 0 0 14px; scroll-margin-top: 90px; }
@media (max-width: 720px) { .man-node-card { grid-template-columns: minmax(0, 1fr); } }
.man-node-top { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; flex-wrap: wrap; margin-bottom: 8px; }
.man-node-top h4 { margin: 0; font-size: 1.15rem; scroll-margin-top: 90px; }
.man-tag { font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: var(--size-micro, 12px); letter-spacing: 0; color: var(--ink-3); }
.man-try { border-top: 1px solid var(--line); padding-top: 10px; margin-top: 10px; font-size: 0.94rem; color: var(--ink-2); }
.man-try strong { font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: var(--size-micro, 12px); letter-spacing: 0; color: var(--signal); margin-right: 6px; }
.man-chain { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; margin-top: 8px; }
.man-chain .chip { font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: 0.82rem; padding: 4px 10px; border-radius: var(--r-pill, 999px); background: var(--deep); color: var(--ink); }
.node-page-link { display: inline-block; margin-top: 10px; font-size: 0.88rem; font-weight: 600; color: var(--signal); text-decoration: none; }
.node-page-link:hover { text-decoration: underline; }

.man-shot { margin: 0; background: var(--deep); border-radius: var(--r-md, 16px); padding: 10px; max-width: 240px; }
.shot-imgs { display: flex; flex-wrap: wrap; gap: 8px; justify-content: center; }
.man-shot img { display: block; max-width: 100%; height: auto; border-radius: 8px; }
.man-shot figcaption { font-size: 0.8rem; color: var(--ink-2); margin-top: 8px; line-height: 1.4; text-align: center; }

.man-node-row { display: flex; gap: 14px; align-items: flex-start; padding: 14px 0; border-top: 1px solid var(--line); scroll-margin-top: 90px; }
.man-node-row .row-shot { width: 88px; height: auto; border-radius: 8px; flex: none; }
.man-node-row h4 { margin: 0 0 4px; font-size: 1rem; }
.man-node-row p { margin: 0; font-size: 0.94rem; color: var(--ink-2); }

.man-fig { margin: 0 0 18px; background: var(--deep); border-radius: var(--r-md, 16px); padding: 12px; }
.man-fig img { display: block; max-width: 100%; height: auto; border-radius: 8px; }
.man-fig figcaption { font-size: 0.85rem; color: var(--ink-2); margin-top: 8px; }

.table-wrap { width: 100%; overflow-x: auto; -webkit-overflow-scrolling: touch; margin: 0 0 18px; border-radius: var(--r-md, 14px); background: var(--surface-3); }
.table-wrap table { width: 100%; border-collapse: collapse; font-size: 0.92rem; min-width: 480px; }
.table-wrap caption { text-align: left; padding: 10px 14px; font-weight: 600; font-size: 0.9rem; color: var(--ink-2); }
.table-wrap th, .table-wrap td { padding: 10px 14px; text-align: left; border-top: 1px solid var(--line); vertical-align: top; }
.table-wrap thead th { font-family: var(--font-sans, 'Geist', system-ui, sans-serif); font-size: var(--size-micro, 12px); letter-spacing: 0; color: var(--signal); background: var(--deep); border-top: none; }

.man-chapter pre { background: #151930; color: #EEF1FA; border-radius: var(--r-md, 14px); padding: 14px 18px; overflow-x: auto; margin: 0 0 16px; font-size: 0.88rem; line-height: 1.55; }
.man-chapter code { font-family: var(--font-mono); font-size: 0.88em; background: var(--deep); padding: 2px 6px; border-radius: 6px; }
.man-chapter pre code { background: none; padding: 0; font-size: inherit; }

.man-gallery-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px; margin: 12px 0 24px; }
.gal-cell { display: flex; flex-direction: column; align-items: center; gap: 8px; padding: 10px; background: var(--surface-3); border-radius: var(--r-sm, 12px); text-decoration: none; color: var(--ink); }
.gal-cell:hover { background: var(--deep); }
.gal-cell img { max-width: 100%; height: auto; border-radius: 6px; }
.gal-nm { font-size: 0.82rem; font-weight: 600; text-align: center; }

.node-ref { color: var(--signal); text-decoration: underline; text-decoration-thickness: 1px; text-underline-offset: 2px; }
.man-pager { display: flex; flex-wrap: wrap; gap: 10px; padding: 16px 0; }
.chap-nav { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 10px; margin-top: 24px; padding-top: 14px; border-top: 1px solid var(--line); font-size: 0.9rem; }
.chap-nav a { color: var(--signal); text-decoration: none; font-weight: 500; }
.chap-nav a:hover { text-decoration: underline; }
.man-ext-links { display: flex; flex-wrap: wrap; gap: 16px; padding: 12px 0 0; font-size: 0.88rem; color: var(--ink-2); border-top: 1px solid var(--line); }
.man-ext-links a { color: var(--ink-2); text-decoration: underline; text-underline-offset: 2px; }
.man-ext-links a:hover { color: var(--signal); }
"""


# ---------------------------------------------------------------------------
# Slug extraction & stability check
# ---------------------------------------------------------------------------

class _IdExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids: list[str] = []

    def handle_starttag(self, tag, attrs):
        for k, v in attrs:
            if k == "id" and v:
                self.ids.append(v)


def extract_ids(html_text: str) -> list[str]:
    p = _IdExtractor()
    p.feed(html_text)
    return sorted(set(p.ids))


def verify_slug_stability(new_map: dict[str, list[str]], update: bool) -> None:
    if update or not os.path.exists(SLUG_MAP_PATH):
        with open(SLUG_MAP_PATH, "w", encoding="utf-8") as f:
            json.dump(new_map, f, indent=2, sort_keys=True)
        print(f"wrote {SLUG_MAP_PATH} ({sum(len(v) for v in new_map.values())} slugs)")
        return
    old_map = json.load(open(SLUG_MAP_PATH, encoding="utf-8"))
    missing = []
    for page, old_ids in old_map.items():
        cur = set(new_map.get(page, []))
        for oid in old_ids:
            if oid not in cur:
                missing.append((page, oid))
    if missing:
        for page, oid in missing[:20]:
            print(f"ERROR: missing slug {page}#{oid}", file=sys.stderr)
        raise SystemExit(f"Slug stability check failed: {len(missing)} existing slug(s) disappeared.")
    # Merge any newly added slugs while preserving stability
    merged = {p: sorted(set(old_map.get(p, [])) | set(new_map.get(p, []))) for p in set(old_map) | set(new_map)}
    with open(SLUG_MAP_PATH, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2, sort_keys=True)
    print(f"slug-stability OK: 0 missing across {len(new_map)} pages ({sum(len(v) for v in merged.values())} slugs)")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update-slugs", action="store_true", help="Reset slug_map.json to current slugs")
    args = ap.parse_args()

    # Write manual.css first so file_hash("manual.css") is accurate
    with open(os.path.join(WEB, "manual.css"), "w", encoding="utf-8") as f:
        f.write(MANUAL_CSS)

    exact_nodes, _ = build_node_registry()

    # Hero 3D art (WebP)
    art_node, art_nw, art_nh = copy_static_webp(os.path.join(ROOT, "website", "assets", "3d", "node_hero.webp"), "hero-node.webp")
    art_patch, art_pw, art_ph = copy_static_webp(os.path.join(ROOT, "website", "assets", "3d", "patch.webp"), "hero-patch.webp")
    art_field, art_fw, art_fh = copy_static_webp(os.path.join(ROOT, "website", "assets", "3d", "node_field_pixel.webp"), "hero-field.webp")

    # 1. Node Reference Manual (/manual/)
    slugger_main = UniqueSlugger()
    node_body, node_toc = build_node_manual_body(slugger_main, exact_nodes)
    pub_m, mod_m = git_dates(["tools/manual/build.py", "tools/manual/cards.py", "tools/manual/cards_base.json", "tools/manual/ui_chapter.py"])
    node_html = render_page(
        rel_file="manual/index.html",
        rel_url="manual/",
        root_prefix="../",
        title="Infinite Node Reference Manual: 347 Nodes | Infinite",
        description="Complete reference for all 347 spawnable nodes, UI panels, shortcuts, and patch recipes in Infinite.",
        h1="The Node Field Guide",
        kicker="Manual / Node Reference",
        lead="Every node in Infinite's palette: what it does, how to patch it, and a real screenshot of the node itself.",
        hero_art_src=f"../{art_node}",
        hero_art_w=art_nw,
        hero_art_h=art_nh,
        hero_art_alt="3D render of the Infinite Audio Filter node with patched cables.",
        pdf_href="../assets/Infinite_Node_Reference_Manual.pdf",
        pdf_label="Download PDF",
        body_html=node_body,
        toc=node_toc,
        breadcrumbs=[("Infinite", f"{BASE_URL}/"), ("Manual", f"{BASE_URL}/manual/")],
        date_pub=pub_m,
        date_mod=mod_m,
        og_image_url=f"{BASE_URL}/{art_node}",
        other_manuals=[("headless/", "Headless User Guide"), ("field/", "Field Language Manual")],
    )
    os.makedirs(os.path.join(WEB, "manual"), exist_ok=True)
    main_out = os.path.join(WEB, "manual", "index.html")
    with open(main_out, "w", encoding="utf-8") as f:
        f.write(node_html)
    main_kb = os.path.getsize(main_out) / 1024
    print(f"wrote {main_out} ({main_kb:.1f} KB)")

    # 2. Headless User Guide (/manual/headless/)
    slugger_hl = UniqueSlugger()
    hl_md = open(os.path.join(ROOT, "docs", "headless", "GUIDE.md"), encoding="utf-8").read()
    _, hl_rest = hl_md.split("\n", 1)
    hl_intro, hl_body_md = hl_rest.split("\n## ", 1)
    hl_intro_html = f'<section class="man-chapter" id="overview"><h2>Overview {anchor_btn("overview", "Overview")}</h2>{convert_markdown_manual(hl_intro, slugger_hl, "../", exact_nodes)[0]}</section>'
    hl_sections_html, hl_toc = convert_markdown_manual("## " + hl_body_md, slugger_hl, "../", exact_nodes)
    hl_toc.insert(0, {"id": "overview", "title": "Overview", "children": []})
    pub_h, mod_h = git_dates(["docs/headless/GUIDE.md"])
    hl_html = render_page(
        rel_file="manual/headless/index.html",
        rel_url="manual/headless/",
        root_prefix="../../",
        title="Infinite Headless Engine User Guide | Infinite",
        description="Render pictures, video, and audio from plain-text Infinite patches using the headless CLI and control port.",
        h1="The Headless Engine",
        kicker="Manual / Headless CLI",
        lead="Render pictures, video, and sound from a plain text file. Same Infinite engine underneath, driven from the command line.",
        hero_art_src=f"../../{art_patch}",
        hero_art_w=art_pw,
        hero_art_h=art_ph,
        hero_art_alt="3D render of two Infinite nodes connected by a modulation cable.",
        pdf_href="../../assets/Infinite_Headless_User_Guide.pdf",
        pdf_label="Download PDF",
        body_html=hl_intro_html + "\n" + hl_sections_html,
        toc=hl_toc,
        breadcrumbs=[("Infinite", f"{BASE_URL}/"), ("Manual", f"{BASE_URL}/manual/"), ("Headless Guide", f"{BASE_URL}/manual/headless/")],
        date_pub=pub_h,
        date_mod=mod_h,
        og_image_url=f"{BASE_URL}/{art_patch}",
        other_manuals=[("../", "Node Reference Manual"), ("../field/", "Field Language Manual")],
    )
    os.makedirs(os.path.join(WEB, "manual", "headless"), exist_ok=True)
    hl_out = os.path.join(WEB, "manual", "headless", "index.html")
    with open(hl_out, "w", encoding="utf-8") as f:
        f.write(hl_html)
    print(f"wrote {hl_out} ({os.path.getsize(hl_out) / 1024:.1f} KB)")

    # 3. Field Language Manual (/manual/field/)
    slugger_fl = UniqueSlugger()
    fl_md = open(os.path.join(ROOT, "docs", "field", "MANUAL.md"), encoding="utf-8").read()
    _, fl_rest = fl_md.split("\n", 1)
    fl_intro, fl_body_md = fl_rest.split("\n## ", 1)
    fl_intro_html = f'<section class="man-chapter" id="overview"><h2>What Field is {anchor_btn("overview", "What Field is")}</h2>{convert_markdown_manual(fl_intro, slugger_fl, "../", exact_nodes)[0]}</section>'
    fl_sections_html, fl_toc = convert_markdown_manual("## " + fl_body_md, slugger_fl, "../", exact_nodes)
    fl_toc.insert(0, {"id": "overview", "title": "What Field is", "children": []})
    pub_f, mod_f = git_dates(["docs/field/MANUAL.md"])
    fl_html = render_page(
        rel_file="manual/field/index.html",
        rel_url="manual/field/",
        root_prefix="../../",
        title="Infinite Field Language Manual | Infinite",
        description="Write custom DSP, shaders, and geometry kernels in Infinite's Field language across five execution domains.",
        h1="The Field Language Manual",
        kicker="Manual / Field Language",
        lead="Everything you need to write real Field kernels and build custom devices across graph, frame, element, pixel, and sample domains.",
        hero_art_src=f"../../{art_field}",
        hero_art_w=art_fw,
        hero_art_h=art_fh,
        hero_art_alt="3D render of the Infinite Field Pixel programmable shader node.",
        pdf_href="../../assets/Field_Language_Manual.pdf",
        pdf_label="Download PDF",
        body_html=fl_intro_html + "\n" + fl_sections_html,
        toc=fl_toc,
        breadcrumbs=[("Infinite", f"{BASE_URL}/"), ("Manual", f"{BASE_URL}/manual/"), ("Field Manual", f"{BASE_URL}/manual/field/")],
        date_pub=pub_f,
        date_mod=mod_f,
        og_image_url=f"{BASE_URL}/{art_field}",
        other_manuals=[("../", "Node Reference Manual"), ("../headless/", "Headless User Guide")],
    )
    os.makedirs(os.path.join(WEB, "manual", "field"), exist_ok=True)
    fl_out = os.path.join(WEB, "manual", "field", "index.html")
    with open(fl_out, "w", encoding="utf-8") as f:
        f.write(fl_html)
    print(f"wrote {fl_out} ({os.path.getsize(fl_out) / 1024:.1f} KB)")

    # Verify slug stability
    slug_map = {
        "manual/index.html": extract_ids(node_html),
        "manual/headless/index.html": extract_ids(hl_html),
        "manual/field/index.html": extract_ids(fl_html),
    }
    verify_slug_stability(slug_map, args.update_slugs)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "website", "check_links.py"), "--internal-only"], check=True)


if __name__ == "__main__":
    main()
