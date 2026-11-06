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
TITLE = "Infinite chat: talk to your patch with Claude, ChatGPT or Gemini"
DESC = ("Type what you want in plain words and your own Claude, ChatGPT or Gemini changes your Infinite project for you. "
        "Every change can be undone.")
e = html.escape


def icon(name):
    return chrome._svg(name)


SECTIONS = [("what", "What is it?", "chat"), ("see", "What you will see", "eye"), ("try", "Things you can say", "play"), ("how", "How it works", "bolt"),
            ("accounts", "Connect an assistant", "lock"), ("control", "You stay in control", "settings"), ("faq", "Questions", "chat"),
            ("start", "Start in 4 steps", "terminal")]


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
    agents = [("Claude", "Anthropic", "A browser page opens, you sign in, done."),
              ("ChatGPT", "OpenAI", "A browser page opens, you sign in, done."),
              ("Gemini", "Google", "A browser page opens, then you paste the short code it shows into the chat.")]
    arows = "".join(f"<tr><td><strong>{n}</strong></td><td>{v}</td><td>{s}</td></tr>" for n, v, s in agents)
    return f'''<main class="dv">
    <section class="dv-hero">
      <div class="container">
        <span class="blog-hand hand">chat</span>
        <h1 class="slab">Talk to your patch</h1>
        <p class="lead">Type what you want in plain words, like "make the notes brighter". An AI assistant you already use (Claude, ChatGPT or Gemini) changes your project for you. Every change can be undone.</p>
        <ul class="facts"><li>No coding needed</li><li>Uses your own account</li><li>One click to undo</li></ul>
        <div class="aud-cta"><a href="index.html#download" class="btn btn-primary">Download Infinite</a><a href="#start" class="btn btn-ghost">Start in 4 steps</a></div>
      </div>
    </section>
    <div class="container dv-grid">
      <nav class="dv-toc" aria-label="On this page">{toc}</nav>
      <div class="dv-main">
        <section class="dv-sec cx-stage" aria-label="The chat panel in Infinite">
          {shot("chat-window.webp", "The Infinite window: the node canvas on the left and the chat panel on the right, showing a reply with the changes it made", 1800, 1125,
                "The real app. The canvas is on the left. The chat is the panel on the right.")}
        </section>

        <section class="dv-sec" id="what"><h2>What is it?</h2>
          <p>Infinite is a program where you build music and visuals by connecting boxes called <strong>nodes</strong>. The chat is a panel inside it where you can simply <em>write</em> what you want, instead of finding the right box or knob yourself.</p>
          <p>Think of it as a helper sitting next to you. You say "slow the notes down", and it turns the right knob. You can always see what it did, and take it back.</p>
          <div class="dv-cards">
            <article class="dv-card"><h2>You ask</h2><p>Write a sentence in the box at the bottom of the chat.</p></article>
            <article class="dv-card"><h2>It changes your project</h2><p>The knobs and cables move on your canvas, live.</p></article>
            <article class="dv-card"><h2>You check, or undo</h2><p>Every reply lists what changed, with an Undo button.</p></article>
          </div>
        </section>

        <section class="dv-sec" id="see"><h2>What you will see</h2>
          <div class="cx-split">
            {shot("chat-panel.webp", "A chat reply in Infinite: the sentence 'make wavetable brighter and slow the notes', a Changed card with two small knobs, and a list of old and new values", 760, 1335,
                  "A reply. Your sentence, then the exact knobs it turned.", "cx-tall")}
            <ol class="cx-legend">
              <li><strong>Your message.</strong> The grey bubble is what you wrote. Here: "make wavetable brighter and slow the notes".</li>
              <li><strong>The Changed card.</strong> Small live knobs for every value that moved. You can turn them yourself right there.</li>
              <li><strong>The short list.</strong> Each line reads <em>old value → new value</em>, for example <code>cutoff 2000 → 8000</code>.</li>
              <li><strong>Coloured names.</strong> A node's name in the reply is a link. Click it and the canvas jumps to that node.</li>
              <li><strong>The box at the bottom.</strong> Type here. Press <code>@</code> to pick a node by name. Press <strong>+</strong> to attach a file or picture.</li>
            </ol>
          </div>
        </section>

        <section class="dv-sec" id="try"><h2>Things you can say</h2>
          <p>Start with any of these. Words that start with <code>@</code> are node names from your own project.</p>
          <div class="table-wrap"><table>
            <thead><tr><th>You type</th><th>What happens</th></tr></thead>
            <tbody>
              <tr><td><code>make @wavetable brighter</code></td><td>The assistant opens the filter on that node.</td></tr>
              <tr><td><code>@cube set size to 2</code></td><td>Runs instantly. This exact kind of sentence needs no AI.</td></tr>
              <tr><td><code>connect @a to @b</code></td><td>Draws a cable between the two nodes, instantly.</td></tr>
              <tr><td><code>make the rings feel like they are breathing</code></td><td>The assistant decides how, and shows you what it changed.</td></tr>
              <tr><td><code>/explain</code></td><td>Describes your project in plain words.</td></tr>
              <tr><td><code>/undo</code></td><td>Takes back the last change.</td></tr>
              <tr><td><code>/help</code></td><td>Lists every command.</td></tr>
            </tbody>
          </table></div>
        </section>

        <section class="dv-sec" id="how"><h2>How it works</h2>
          <ol class="cx-flow">
            <li><span>1</span><div><h3>You type a sentence</h3><p>The box helps as you go. After <code>@</code> it suggests node names, then that node's knobs, then sensible values.</p></div></li>
            <li><span>2</span><div><h3>Infinite checks it first</h3><p>If your sentence is an exact instruction, like <code>set</code>, <code>connect</code> or <code>add</code>, Infinite does it straight away. A small lightning bolt marks these.</p></div></li>
            <li><span>3</span><div><h3>Anything else goes to your assistant</h3><p>An open-ended request such as "make it calmer" is handed to Claude, ChatGPT or Gemini, running on your own computer with your own account. A small spark marks these.</p></div></li>
            <li><span>4</span><div><h3>It edits your open project</h3><p>The assistant uses the same commands as the <code>infinite</code> command line. If it makes a mistake, Infinite refuses that step instead of half-applying it.</p></div></li>
            <li><span>5</span><div><h3>You get a list of what changed</h3><p>Each reply shows the changes and an Undo button.</p></div></li>
          </ol>
        </section>

        <section class="dv-sec" id="accounts"><h2>Connect an assistant</h2>
          <p>The thinking is done by an assistant you already use. Infinite connects it to your project. It is not a new service, and Infinite has no login of its own.</p>
          <div class="table-wrap"><table>
            <thead><tr><th>Assistant</th><th>Made by</th><th>Signing in</th></tr></thead>
            <tbody>{arows}</tbody>
          </table></div>
          {shot("chat-settings.webp", "Infinite Settings, Agents and Terminal page: a row for Claude, ChatGPT and Gemini each with a Set up button, plus the model, folder and saved chats", 1100, 837,
                "Settings, Agents &amp; Terminal. One row per assistant, with a Set up button.")}
          <ul class="dev-list">
            <li><strong>You sign in on the provider's own page.</strong> Infinite never sees or stores your password.</li>
            <li><strong>It uses your plan.</strong> Usage counts against your own subscription, the same as using the assistant anywhere else.</li>
            <li><strong>No account yet?</strong> Press <em>I don't have one</em> and create it on the provider's page.</li>
            <li><strong>Setup is one button.</strong> Press <em>Set up</em> and Infinite installs what is missing and walks you through sign-in. If a step fails, it says why in one sentence.</li>
          </ul>
        </section>

        <section class="dv-sec" id="control"><h2>You stay in control</h2>
          <ul class="dev-list">
            <li><strong>Undo.</strong> Every change card has an Undo button, and <code>/undo</code> does the same.</li>
            <li><strong>Nothing hidden.</strong> If the assistant is not allowed to do something, the chat tells you in plain words.</li>
            <li><strong>It works in one folder.</strong> You choose it in Settings. By default it is the folder of your open project.</li>
            <li><strong>Your chats stay on your computer.</strong> Settings shows how many are saved, with a Clear all button.</li>
          </ul>
        </section>

        <section class="dv-sec" id="faq"><h2>Common questions</h2>
          <ul class="dev-list">
            <li><strong>Do I have to pay for Infinite or the chat?</strong> Infinite is free. The assistant is your own Claude, ChatGPT or Gemini account, so its normal plan applies.</li>
            <li><strong>Can I use Infinite without the chat?</strong> Yes. The chat is optional. Everything works with the mouse and keyboard.</li>
            <li><strong>Will it break my project?</strong> Every change can be undone, and bad steps are refused before they land.</li>
            <li><strong>Why is it sometimes slow?</strong> Exact instructions (the lightning bolt) are instant. Open-ended ones wait for your assistant's model, which depends on the provider.</li>
          </ul>
        </section>

        <section class="dv-sec" id="start"><h2>Start in 4 steps</h2>
          <ol class="dev-list">
            <li><a href="index.html#download">Download Infinite</a> and open any project.</li>
            <li>Open the chat panel on the right and press <em>Set up</em> next to Claude, ChatGPT or Gemini.</li>
            <li>Finish signing in on the page that opens in your browser.</li>
            <li>Type <code>@</code>, pick a node, and say what you want.</li>
          </ol>
          <p>Building your own tools? The <a href="developers.html">developers page</a> covers the command line, Python SDK and MCP server the chat is built on.</p>
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
    head = head.replace('<link rel="stylesheet" href="dev.css', '<link rel="stylesheet" href="chat.css?v=3">\n  <link rel="stylesheet" href="dev.css', 1)
    rest = re.sub(r'\s*<script src="audience\.js[^"]*" defer></script>', "", rest)
    return head, rest


def main():
    head, rest = head_and_tail()
    out = os.path.join(W, "chat.html")
    open(out, "w", encoding="utf-8").write(head + main_html() + "\n  " + rest)
    print("written: chat.html")


if __name__ == "__main__":
    main()
