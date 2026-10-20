"""Shared helpers for the SEO and distribution system (stdlib only, Python 3.9).

What: post front matter parsing, a small Markdown renderer, paths, the ledger and the env file loader.
Why:  every script (build, lint, publish, radar, check) reads the same posts and the same ledger.
"""
from __future__ import annotations
import datetime, html, json, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SEO = os.path.join(ROOT, "tools", "seo")
POSTS = os.path.join(SEO, "posts")
WEB = os.path.join(ROOT, "website")
BLOG = os.path.join(WEB, "blog")
LEDGER = os.path.join(SEO, "ledger.json")
BASE = "https://infinitedevices.org/"
ENV_FILE = os.path.expanduser("~/.config/infinite-seo.env")   # secrets live outside the repo
UA = "InfiniteSEO/1.0 (+https://infinitedevices.org)"


def img_size_attrs(src):
    """' width="W" height="H"' for a local PNG, JPEG or WebP so the browser reserves the space (no layout shift)."""
    import struct
    if src.startswith(BASE):
        path = os.path.join(WEB, src[len(BASE):])  # an absolute link to this site's own file
    elif src.startswith("/"):
        path = os.path.join(WEB, src.lstrip("/"))
    else:
        path = os.path.normpath(os.path.join(BLOG, src)) if not src.startswith("http") else None
    try:
        with open(path, "rb") as f:
            d = f.read(65536)
        if d[:8] == b"\x89PNG\r\n\x1a\n":
            w, h = struct.unpack(">II", d[16:24])
        elif d[:4] == b"RIFF" and d[8:12] == b"WEBP":
            if d[12:16] == b"VP8X":
                w = 1 + int.from_bytes(d[24:27], "little"); h = 1 + int.from_bytes(d[27:30], "little")
            elif d[12:16] == b"VP8 ":
                w, h = struct.unpack("<HH", d[26:30]); w &= 0x3FFF; h &= 0x3FFF
            else:
                b = int.from_bytes(d[21:25], "little"); w = (b & 0x3FFF) + 1; h = ((b >> 14) & 0x3FFF) + 1
        elif d[:2] == b"\xff\xd8":
            i = 2
            while i < len(d) - 9:
                if d[i] != 0xFF:
                    i += 1; continue
                m = d[i + 1]
                if m in (0xC0, 0xC1, 0xC2):
                    h, w = struct.unpack(">HH", d[i + 5:i + 9]); break
                i += 2 + struct.unpack(">H", d[i + 2:i + 4])[0]
            else:
                return ""
        else:
            return ""
        if "assets/nodes/" in src:
            w, h = w // 2, h // 2   # 2x captures of the app: CSS pixels are half the file
        return f' width="{w}" height="{h}"'
    except (OSError, struct.error, TypeError):
        return ""


def load_env():
    """Read KEY=VALUE lines from ~/.config/infinite-seo.env into os.environ (never overrides real env)."""
    if os.path.exists(ENV_FILE):
        for line in open(ENV_FILE):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))


def parse_front(text):
    """--- key: value --- block, then the body. Lists are written [a, b]."""
    m = re.match(r"^---\n(.*?)\n---\n?(.*)$", text, re.S)
    if not m:
        raise ValueError("post has no front matter")
    meta = {}
    for line in m.group(1).splitlines():
        if not line.strip() or line.startswith("#") or ":" not in line:
            continue
        k, v = line.split(":", 1)
        v = v.strip()
        if v.startswith("[") and v.endswith("]"):
            meta[k.strip()] = [x.strip().strip('"').strip("'") for x in v[1:-1].split(",") if x.strip()]
        else:
            meta[k.strip()] = v.strip('"').strip("'")
    return meta, m.group(2).strip()


def load_posts(include_drafts=False):
    out = []
    for f in sorted(os.listdir(POSTS)):
        if not f.endswith(".md"):
            continue
        meta, body = parse_front(open(os.path.join(POSTS, f)).read())
        meta["slug"] = f[:-3]
        meta.setdefault("status", "draft")
        meta.setdefault("category", "product")
        meta.setdefault("tags", [])
        meta["body"] = body
        meta["url"] = f"{BASE}blog/{meta['slug']}.html"
        meta["words"] = len(re.findall(r"\w+", body))
        meta["minutes"] = max(1, round(meta["words"] / 220))
        if not include_drafts and meta.get("date", "") > today():
            continue  # scheduled: appears on its date
        if include_drafts or meta["status"] == "published":
            out.append(meta)
    out.sort(key=lambda p: (p.get("date", ""), int(p.get("weight", 0))), reverse=True)
    return out


def ledger():
    if os.path.exists(LEDGER):
        return json.load(open(LEDGER))
    return {"posts": {}, "outreach": {}}


def save_ledger(d):
    json.dump(d, open(LEDGER, "w"), indent=1, sort_keys=True, ensure_ascii=False)
    open(LEDGER, "a").write("\n")


def today():
    return datetime.date.today().isoformat()


# ---------------------------------------------------------------- Markdown
def slugify(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def inline(s):
    s = html.escape(s, quote=False)
    codes = []
    def stash(m):
        codes.append(m.group(1)); return f"\x00{len(codes)-1}\x00"
    s = re.sub(r"`([^`]+)`", stash, s)
    s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", lambda m: f'<img src="{m.group(2)}" alt="{html.escape(m.group(1))}"{img_size_attrs(m.group(2))} loading="lazy" decoding="async">', s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: f'<a href="{m.group(2)}"' + ('' if m.group(2).startswith(("/", "#", BASE, "../", "index", "library")) else ' rel="noopener"') + f'>{m.group(1)}</a>', s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![*\w])\*([^*\n]+)\*(?!\w)", r"<em>\1</em>", s)
    return re.sub(r"\x00(\d+)\x00", lambda m: f"<code>{html.escape(codes[int(m.group(1))], quote=False)}</code>", s)


UL_RE = re.compile(r"^\s*[-*] ")
OL_RE = re.compile(r"^\s*\d+\. ")


def render_md(body):
    """Returns (html, headings[(level, id, text)]). Supports #-headings, lists, fences, quotes, tables, images."""
    lines, out, heads, i = body.split("\n"), [], [], 0
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("```"):
            lang, i, buf = ln[3:].strip(), i + 1, []
            while i < len(lines) and not lines[i].startswith("```"):
                buf.append(lines[i]); i += 1
            i += 1
            cls = f' class="language-{html.escape(lang)}"' if lang else ""
            out.append("<pre><code" + cls + ">" + html.escape("\n".join(buf), quote=False) + "</code></pre>")
        elif re.match(r"^#{2,4} ", ln):
            lvl = len(ln) - len(ln.lstrip("#")); txt = ln[lvl + 1:].strip()
            hid = slugify(re.sub(r"[`*]", "", txt)); heads.append((lvl, hid, re.sub(r"[`*]", "", txt)))
            out.append(f'<h{lvl} id="{hid}">{inline(txt)}</h{lvl}>'); i += 1
        elif re.match(r"^\s*[-*] ", ln):
            items = []
            while i < len(lines) and re.match(r"^\s*[-*] ", lines[i]):
                items.append("<li>" + inline(UL_RE.sub("", lines[i])) + "</li>"); i += 1
            out.append("<ul>" + "".join(items) + "</ul>")
        elif re.match(r"^\s*\d+\. ", ln):
            items = []
            while i < len(lines) and re.match(r"^\s*\d+\. ", lines[i]):
                items.append("<li>" + inline(OL_RE.sub("", lines[i])) + "</li>"); i += 1
            out.append("<ol>" + "".join(items) + "</ol>")
        elif ln.startswith(">"):
            buf = []
            while i < len(lines) and lines[i].startswith(">"):
                buf.append(lines[i].lstrip("> ")); i += 1
            out.append(f"<blockquote><p>{inline(' '.join(buf))}</p></blockquote>")
        elif "|" in ln and i + 1 < len(lines) and re.match(r"^\s*\|?[\s:|-]+\|[\s:|-]*$", lines[i + 1]):
            rows = []
            while i < len(lines) and "|" in lines[i]:
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")]); i += 1
            head, rest = rows[0], rows[2:]
            t = "<div class=\"table-wrap\"><table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr></thead><tbody>"
            t += "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in rest) + "</tbody></table></div>"
            out.append(t)
        elif re.match(r"^---+$", ln.strip()):
            out.append("<hr>"); i += 1
        elif ln.strip().startswith(("<figure", "<div", "<details")):
            tag = re.match(r"^<([a-z0-9]+)", ln.strip()).group(1)
            end_tag = f"</{tag}>"
            buf = []
            while i < len(lines):
                buf.append(lines[i])
                if end_tag in lines[i]:
                    i += 1
                    break
                i += 1
            out.append("\n".join(buf))
        elif not ln.strip():
            i += 1
        else:
            buf = []
            while i < len(lines) and lines[i].strip() and not re.match(r"^(#{2,4} |```|>|\s*[-*] |\s*\d+\. |<(?:figure|div|details))", lines[i]):
                buf.append(lines[i].strip()); i += 1
            para = " ".join(buf)
            m = re.fullmatch(r"!\[([^\]]*)\]\(([^)\s]+)\)", para)
            if m:
                alt = html.escape(m.group(1))
                cap = f"<figcaption>{alt}</figcaption>" if alt else ""
                out.append(f'<figure><img src="{m.group(2)}" alt="{alt}"{img_size_attrs(m.group(2))} loading="lazy" decoding="async">{cap}</figure>')
            else:
                out.append(f"<p>{inline(para)}</p>")
    return "\n".join(out), heads
