#!/usr/bin/env python3
"""Regenerate src/core/AgentDocs.h: the manuals `infinite docs <topic>` prints to an agent.

Sources: the Node Field Guide cards (tools/manual/cards_base.json + cards.py), the manual's Using the App chapter
(tools/manual/ui_chapter.py: every panel, menu, column and control), the Field manual
(docs/field/MANUAL.md), the headless guide (docs/headless/GUIDE.md), the reference pages in
docs/reference/, and the map and craft skills in docs/ai-skills/. Edit those, then run this; never edit the header by hand.

  tools/gen-agent-docs.py           rewrite the header
  tools/gen-agent-docs.py --check   exit 1 if it is stale (CI)
"""
import html, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "src/core/AgentDocs.h")
CHUNK = 12000  # MSVC rejects a single string literal over ~16 KB; adjacent raw literals concatenate

sys.path.insert(0, os.path.join(ROOT, "tools/manual"))
import cards  # noqa: E402


def node_guide():
    with open(os.path.join(ROOT, "tools/manual/cards_base.json")) as f:
        chapters = json.load(f) + cards.CHAPTERS
    out = ["# Node Field Guide", "", "One card per node: what it does, a thing to try, and a chain it fits in. "
           "`infinite describe <type>` has the exact params and pins.", ""]
    for ch in chapters:
        out.append("## %s" % ch["title"])
        intro = ch["intro"] if isinstance(ch["intro"], str) else " ".join(ch["intro"])
        out += [intro, ""]
        for card in ch["cards"]:
            name, tag, text, tip, chain = card[:5]
            out.append("### %s" % name)
            out.append("%s. %s" % (tag, text))
            out.append("Try: %s" % tip)
            if chain:
                out.append("Chain: %s" % " -> ".join(chain))
            out.append("")
    return "\n".join(out)


def ui_guide():
    """The manual's Using the App chapter as text: each picture becomes its caption, each table a list."""
    import ui_chapter
    ui_chapter.fig = lambda name, caption, box=None, width="100%": "<p>Picture: %s</p>" % caption
    h = ui_chapter.html_chapter(None)
    h = re.sub(r'<div class="kick">.*?</div><div class="hand"[^>]*>.*?</div>', "", h)
    h = re.sub(r"<h1>(.*?)</h1>", r"\n# \1\n", h)
    h = re.sub(r"<h2>(.*?)</h2>", r"\n## \1\n", h)
    h = re.sub(r"<h3>(.*?)</h3>", r"\n### \1\n", h)
    h = re.sub(r"<tr>(?:<th>.*?</th>)+</tr>", "", h)
    h = re.sub(r"<tr><td><b>(.*?)</b></td><td>(.*?)</td></tr>", r"\n- **\1**: \2", h)
    h = re.sub(r"</?(?:b|strong)>", "**", h)
    h = re.sub(r"</?(?:code)>", "`", h)
    h = re.sub(r"</?i>", "", h)
    h = re.sub(r"</p>|</table>|<br>|</pre>", "\n", h)
    h = re.sub(r"<[^>]+>", "", h)
    h = html.unescape(h)
    lines = [re.sub(r"[ \t]+", " ", l).strip() for l in h.split("\n")]
    out, blank = [], True
    for l in lines:
        if not l:
            if not blank:
                out.append("")
            blank = True
            continue
        if l.startswith("#") and not blank:
            out.append("")
        out.append(l)
        blank = False
    intro = ("Every panel, menu, column, button and dropdown of the app, from the manual's Using the App chapter. "
             "Act through verbs, not clicks: `infinite panel <name|settings/place>` opens a panel or a Settings page, "
             "`infinite settings [key [value]]` reads and changes settings, and timeline, session, notes, perf, studio, "
             "view3d-add and node cover what the panels' buttons do. `infinite docs panels` names the verb for each panel.")
    text = "\n".join(out).strip()
    text = text[:text.index("\n## Take your own screenshots")]  # for whoever rebuilds the manual, not for using the app
    first = text.index("\n") if text.startswith("#") else 0
    return text[:first] + "\n\n" + intro + "\n" + text[first:] + "\n"


def read(rel):
    with open(os.path.join(ROOT, rel)) as f:
        return f.read()


def literal(name, text):
    parts = []
    while text:
        cut = len(text) if len(text) <= CHUNK else text.rfind("\n", 0, CHUNK) + 1 or CHUNK
        parts.append(text[:cut])
        text = text[cut:]
    assert all(")AGENTDOC\"" not in p for p in parts)
    body = "\n".join('R"AGENTDOC(%s)AGENTDOC"' % p for p in parts)
    return "   inline const char* %s =\n%s;\n" % (name, body)


def render():
    docs = [
        ("kNodeGuide", node_guide()),
        ("kUiGuide", ui_guide()),
        ("kFieldManual", read("docs/field/MANUAL.md")),
        ("kHeadlessGuide", read("docs/headless/GUIDE.md")),
        ("kConnectionRules", read("docs/reference/connection-rules.md")),
        ("kPatchFormat", read("docs/reference/patch-format.md")),
        ("kGeometryDomains", read("docs/reference/geometry-domains.md")),
        # Skills every agent gets on setup (CliSetup::Skills)
        ("kSkillMap", read("docs/ai-skills/map/SKILL.md")),
        ("kSkillVisualCraft", read("docs/ai-skills/visual-craft/SKILL.md")),
        ("kSkillSoundCraft", read("docs/ai-skills/sound-craft/SKILL.md")),
        ("kSkillFilmCraft", read("docs/ai-skills/film-craft/SKILL.md")),
    ]
    head = ("#pragma once\n// Generated by tools/gen-agent-docs.py from the manuals and docs/reference. Do not edit.\n"
            "// `infinite docs <topic>` prints these to the agents.\n\nnamespace AgentDocs\n{\n")
    return head + "\n".join(literal(n, t) for n, t in docs) + "}\n"


def main():
    text = render()
    if "--check" in sys.argv:
        old = open(HEADER).read() if os.path.exists(HEADER) else ""
        if old != text:
            print("src/core/AgentDocs.h is stale: run tools/gen-agent-docs.py")
            return 1
        return 0
    with open(HEADER, "w") as f:
        f.write(text)
    print("wrote", os.path.relpath(HEADER, ROOT), len(text), "bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
