#!/usr/bin/env python3
"""Builds website/chat.html: the Infinite chat page.

What: takes the shell (head, header, footer, dock) from website/developers.html so the page shares the site's chrome,
      and writes its own <main>: hero, an HTML drawing of the chat window, what it does, how it works, the two speed
      routes, how it understands the canvas, the three agents and the account sign-in, and what stays under your control.
Why:  one page that explains the chat the way the app really behaves. Every claim is read from docs/plans/agent-collab/README.md
      and src/app/panels/ChatPanel.cpp; there are no timings on the page because the model's speed is the provider's.
Usage: python3 tools/website/gen_chat.py && python3 tools/website/sync_chrome.py
Exit codes: 0 written.
"""
import html, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sync_chrome as chrome

W = chrome.W
BASE = "https://infinitedevices.org/"
TITLE = "Infinite agents & terminal: talk to your patch with Claude, ChatGPT or Gemini"
DESC = ("Type what you want in plain words and your own Claude, ChatGPT or Gemini changes your Infinite project for you. "
        "Every change can be undone.")
e = html.escape


def icon(name):
    return chrome._svg(name)


SECTIONS = [("try", "Things you can say", "play"), ("accounts", "Connect an assistant", "lock"), ("control", "You stay in control", "settings"),
            ("start", "Start in 3 steps", "terminal")]


def chip(kind, text):
    return f'<span class="cx-chip cx-{kind}">{e(text)}</span>'


def mock():
    """The chat window, drawn in HTML from the real layout: header pill, user lines, a change card, folded steps, agent text, composer."""
    return f'''<div class="cx" role="img" aria-label="An illustration of the Infinite chat: a sentence, the change it made, and the composer">
          <div class="cx-bar"><span class="cx-title">Chat</span><span class="cx-pill"><i class="cx-av cx-av-claude"></i>Claude <b>Sonnet 5.5</b></span></div>
          <div class="cx-body">
            <div class="cx-user">{chip("node", "@oscillator")} modulate {chip("param", "frequency")} with {chip("node", "@lfo")} from 200 to 800</div>
            <div class="cx-card">
              <div class="cx-card-h"><span class="cx-bolt">{icon("bolt")}</span>Ran directly</div>
              <div class="cx-card-b">{chip("node", "@lfo")} → {chip("mod", "@oscillator.frequency")} <span class="cx-mono">200 – 800</span></div>
              <div class="cx-card-f"><code>infinite mod lfo oscillator frequency 200 800</code><span class="cx-undo">Undo</span></div>
            </div>
            <div class="cx-user">make the rings feel like they are breathing</div>
            <div class="cx-steps">▸ 3 steps</div>
            <div class="cx-agent">Slowed {chip("param", "@rings.speed")} and put an {chip("node", "@lfo")} on {chip("param", "@rings.width")}, so the rings swell and settle about once a second.
              <div class="cx-list"><span>Changed</span> {chip("param", "speed")} <s>1</s> → <b>0.2</b> · {chip("param", "width")} modulated <span class="cx-undo">Undo</span></div>
            </div>
          </div>
          <div class="cx-comp"><span class="cx-badge cx-badge-a">{icon("spark")}</span><span class="cx-ph">Message, or @ for a node</span><span class="cx-send">{icon("send")}</span></div>
        </div>'''


def shot(src, alt, w, h, cap, cls=""):
    return (f'<figure class="cx-shot {cls}"><img src="assets/chat/{src}" alt="{e(alt)}" width="{w}" height="{h}" loading="lazy" decoding="async">'
            f'<figcaption>{cap}</figcaption></figure>')


def main_html():
    toc = "".join(f'<a href="#{i}">{icon(ic)}<span>{e(t)}</span></a>' for i, t, ic in SECTIONS)
    agents = [("Claude", "Anthropic", "Sign in on the page that opens."),
              ("ChatGPT", "OpenAI", "Sign in on the page that opens."),
              ("Gemini", "Google", "Sign in, then paste the short code into the chat.")]
    arows = "".join(f"<tr><td><strong>{n}</strong></td><td>{v}</td><td>{s}</td></tr>" for n, v, s in agents)
    return f'''<main class="dv">
    <section class="dv-hero">
      <div class="container">
        <span class="blog-hand hand">agents</span>
        <h1 class="slab">Agents &amp; terminal</h1>
        <p class="lead">Say what you want in plain words. Claude, ChatGPT or Gemini, on your own account, changes your project. Every change has an Undo.</p>
        <div class="aud-cta"><a href="index.html#download" class="btn btn-primary">Download Infinite</a><a href="#start" class="btn btn-ghost">Start in 3 steps</a></div>
      </div>
    </section>
    <div class="container dv-grid">
      <nav class="dv-toc" aria-label="On this page">{toc}</nav>
      <div class="dv-main">
        <section class="dv-sec cx-stage" aria-label="The chat panel in Infinite">
          {shot("chat-window.webp", "The Infinite window: the node canvas on the left and the chat panel on the right, showing a reply with the changes it made", 1800, 1125,
                "Canvas on the left, chat on the right.")}
        </section>

        <section class="dv-sec" id="try"><h2>Things you can say</h2>
          <div class="table-wrap"><table>
            <thead><tr><th>You type</th><th>What happens</th></tr></thead>
            <tbody>
              <tr><td><code>@cube set size to 2</code></td><td>Runs instantly, no AI needed. <code>@</code> picks a node.</td></tr>
              <tr><td><code>connect @a to @b</code></td><td>Draws the cable, instantly.</td></tr>
              <tr><td><code>make the rings feel like they are breathing</code></td><td>Your assistant decides how, then lists what it changed.</td></tr>
              <tr><td><code>/undo</code></td><td>Takes back the last change.</td></tr>
            </tbody>
          </table></div>
        </section>

        <section class="dv-sec" id="accounts"><h2>Connect an assistant</h2>
          <div class="table-wrap"><table>
            <thead><tr><th>Assistant</th><th>Made by</th><th>Signing in</th></tr></thead>
            <tbody>{arows}</tbody>
          </table></div>
          <p>You sign in on the provider's own page, so Infinite never sees your password. Usage counts against your own plan.</p>
        </section>

        <section class="dv-sec" id="control"><h2>You stay in control</h2>
          <ul class="dev-list">
            <li><strong>Undo.</strong> Every change card has an Undo button.</li>
            <li><strong>One folder.</strong> The assistant only works in the folder you choose in Settings.</li>
            <li><strong>Local chats.</strong> Your chats stay on your computer.</li>
            <li><strong>Optional.</strong> Everything also works with the mouse and keyboard.</li>
          </ul>
        </section>

        <section class="dv-sec" id="start"><h2>Start in 3 steps</h2>
          <ol class="dev-list">
            <li><a href="index.html#download">Download Infinite</a> and open any project.</li>
            <li>In the chat panel, press <em>Set up</em> next to your assistant and sign in.</li>
            <li>Type <code>@</code>, pick a node, and say what you want.</li>
          </ol>
          <p>Building your own tools? See the <a href="developers.html">developers page</a>.</p>
        </section>
      </div>
    </div>
  </main>
'''


def head_and_tail():
    s = open(os.path.join(W, "developers.html"), encoding="utf-8").read()
    head, rest = s[:s.find('<main class="dv">')], s[s.find("<footer"):]
    url = BASE + "chat.html"
    head = re.sub(r"<title>.*?</title>", f"<title>{e(TITLE)}</title>", head, flags=re.S)
    head = re.sub(r'(<meta name="description" content=")[^"]*"', lambda m: m.group(1) + e(DESC) + '"', head)
    head = re.sub(r'(<link rel="canonical" href=")[^"]*"', lambda m: m.group(1) + url + '"', head)
    head = re.sub(r'(<meta property="og:title" content=")[^"]*"', lambda m: m.group(1) + e(TITLE) + '"', head)
    head = re.sub(r'(<meta property="og:description" content=")[^"]*"', lambda m: m.group(1) + e(DESC) + '"', head)
    head = re.sub(r'(<meta property="og:url" content=")[^"]*"', lambda m: m.group(1) + url + '"', head)
    head = re.sub(r'(<meta name="twitter:title" content=")[^"]*"', lambda m: m.group(1) + e(TITLE) + '"', head)
    head = re.sub(r'(<meta name="twitter:description" content=")[^"]*"', lambda m: m.group(1) + e(DESC) + '"', head)
    ld = ('<script type="application/ld+json">{"@context": "https://schema.org", "@type": "WebPage", "name": "%s", "url": "%s", '
          '"description": "%s", "isPartOf": {"@type": "WebSite", "name": "Infinite", "url": "%s"}}</script>' % (TITLE, url, DESC, BASE))
    head = re.sub(r'<script type="application/ld\+json">.*?</script>', lambda m: ld, head, count=1, flags=re.S)
    head = head.replace('<link rel="stylesheet" href="dev.css', '<link rel="stylesheet" href="chat.css?v=1c1480ea">\n  <link rel="stylesheet" href="dev.css', 1)
    rest = re.sub(r'\s*<script src="audience\.js[^"]*" defer></script>', "", rest)
    return head, rest


def main():
    head, rest = head_and_tail()
    out = os.path.join(W, "chat.html")
    open(out, "w", encoding="utf-8").write(head + main_html() + "\n  " + rest)
    print("written: chat.html")


if __name__ == "__main__":
    main()
