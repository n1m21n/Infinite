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
DESC = ("Infinite chat is built into the app. Say what you want, and your own Claude, ChatGPT or Gemini account edits the patch "
        "you have open, with every change one undo away.")
e = html.escape


def icon(name):
    return chrome._svg(name)


SECTIONS = [("what", "What it does", "chat"), ("how", "How it works", "play"), ("speed", "Speed", "bolt"),
            ("understands", "How it understands", "eye"), ("accounts", "Your accounts", "lock"), ("control", "You stay in control", "settings"),
            ("start", "Get started", "terminal")]


def chip(kind, text):
    return f'<span class="cx-chip cx-{kind}">{e(text)}</span>'


def mock():
    """The chat window, drawn in HTML from the real layout: header pill, user lines, a change card, folded steps, agent text, composer."""
    return f'''<div class="cx" role="img" aria-label="An illustration of the Infinite chat: a sentence, the change it made, and the composer">
          <div class="cx-bar"><span class="cx-title">Chat</span><span class="cx-pill"><i class="cx-av cx-av-claude"></i>Claude <b>Sonnet</b></span></div>
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


def main_html():
    toc = "".join(f'<a href="#{i}">{icon(ic)}<span>{e(t)}</span></a>' for i, t, ic in SECTIONS)
    agents = [("Claude", "Anthropic", "Claude Code", "A browser page, then you are in."),
              ("ChatGPT", "OpenAI", "Codex", "A browser page, then you are in."),
              ("Gemini", "Google", "Antigravity", "A browser page, then paste the short code it shows into the chat.")]
    arows = "".join(f"<tr><td><strong>{n}</strong></td><td>{v}</td><td>{c}</td><td>{s}</td></tr>" for n, v, c, s in agents)
    return f'''<main class="dv">
    <section class="dv-hero">
      <div class="container">
        <span class="blog-hand hand">chat</span>
        <h1 class="slab">Talk to your patch</h1>
        <p class="lead">Infinite has a chat built in. Say what you want, and the Claude, ChatGPT or Gemini you already pay for edits the patch you have open. Every change is one undo away.</p>
        <ul class="facts"><li>Claude, ChatGPT, Gemini</li><li>Your own account</li><li>Direct commands skip the model</li></ul>
        <div class="aud-cta"><a href="index.html#download" class="btn btn-primary">Download Infinite</a><a href="#how" class="btn btn-ghost">How it works</a></div>
      </div>
    </section>
    <div class="container dv-grid">
      <nav class="dv-toc" aria-label="On this page">{toc}</nav>
      <div class="dv-main">
        <section class="dv-sec cx-stage" aria-label="The chat window">
          {mock()}
          <p class="cx-note">An illustration drawn from the real chat. Node names are examples.</p>
        </section>

        <div class="dv-cards">
          <article class="dv-card"><h2>It edits the real patch</h2><p>Changes land on the canvas you have open, live. The chat calls the same commands as the <code>infinite</code> command line, so the two cannot drift apart.</p></article>
          <article class="dv-card"><h2>Every name is a link</h2><p>Nodes and parameters in a reply are chips. Click one and the canvas selects it, frames it and flashes the control.</p></article>
          <article class="dv-card"><h2>No new account</h2><p>You sign in to the agent with the provider, in your browser. Infinite has no login of its own and no servers in the middle.</p></article>
        </div>

        <section class="dv-sec" id="what"><h2>What it does</h2>
          <ul class="dev-list">
            <li><strong>Builds and changes patches.</strong> Ask for a sound, a look or a wiring change in plain words. The agent adds nodes, sets parameters and connects cables in your open window.</li>
            <li><strong>Runs short commands itself.</strong> Sentences like <code>@cube set size to 2</code> or <code>connect @a to @b</code> run at once, without waiting for a model.</li>
            <li><strong>Shows what it made.</strong> Pictures, audio files and code blocks in a reply appear in the chat. Pictures open full size on a click.</li>
            <li><strong>Explains and checks.</strong> <code>/explain</code> describes your patch, <code>/check</code> validates it and its code, and <code>/docs</code> answers from the manuals, so a question about Field or a node never needs a web search.</li>
            <li><strong>Has slash commands.</strong> <code>/help</code> lists them. <code>@claude</code>, <code>@chatgpt</code> and <code>@gemini</code> choose who answers, <code>/new</code> starts a fresh chat and <code>/undo</code> takes back the last change.</li>
            <li><strong>Takes files and pictures.</strong> Press + or paste from the clipboard. Attached pictures open full size on a click.</li>
            <li><strong>Lists every change.</strong> Each reply carries only this turn's changes, with live knobs for the values it set, links to the node or parameter, and Undo. A generated sample can be opened in a Sampler.</li>
            <li><strong>Floats where you need it.</strong> Chat, Viewport, Modulation Matrix and Performance can each pop out into their own window.</li>
            <li><strong>Keeps your history.</strong> Chats are saved on your computer, searchable, and cleared with one button in Settings.</li>
          </ul>
        </section>

        <section class="dv-sec" id="how"><h2>How it works</h2>
          <ol class="cx-flow">
            <li><span>1</span><div><h3>You type a sentence</h3><p>The composer knows which slot comes next, so it suggests nodes after <code>@</code>, that node's parameters after it, and valid values after <code>to</code>.</p></div></li>
            <li><span>2</span><div><h3>Infinite reads it first</h3><p>A short grammar checks whether the line is an exact edit. If it parses fully, it runs directly (the <em>direct</em> route). Anything else goes to your agent (the <em>agent</em> route).</p></div></li>
            <li><span>3</span><div><h3>Your agent runs on your computer</h3><p>Claude Code, Codex or Antigravity starts as a local process, in the folder you choose, with a short briefing on how to edit this window. It talks to its own provider over your account.</p></div></li>
            <li><span>4</span><div><h3>Every write goes through one gate</h3><p>Agent edits are made with the same commands as the <code>infinite</code> command line, inside the window you have open, so a bad edit is refused by the engine and not half-applied.</p></div></li>
            <li><span>5</span><div><h3>You see exactly what changed</h3><p>A change card lists what moved and keeps an Undo. A direct edit also shows the command line it equals. Tool calls fold into one quiet row you can open.</p></div></li>
          </ol>
        </section>

        <section class="dv-sec" id="speed"><h2>Speed</h2>
          <p>There are two routes, and the difference is whether a model has to think.</p>
          <div class="table-wrap"><table>
            <thead><tr><th></th><th>Direct</th><th>Agent</th></tr></thead>
            <tbody>
              <tr><td><strong>Marked</strong></td><td>A bolt</td><td>A spark</td></tr>
              <tr><td><strong>When</strong></td><td>The sentence is an exact edit: set, modulate, connect, add, remove, bypass, undo</td><td>Anything open-ended: "make the rings feel like breathing"</td></tr>
              <tr><td><strong>Who runs it</strong></td><td>Infinite itself, inside the app</td><td>Your Claude, ChatGPT or Gemini</td></tr>
              <tr><td><strong>Waiting on a model</strong></td><td>None</td><td>Yes, at the speed your provider's model answers</td></tr>
              <tr><td><strong>Undo</strong></td><td>One step, even for several clauses joined with <em>and</em></td><td>One undo per change</td></tr>
            </tbody>
          </table></div>
          <p>Infinite does not speed up a provider's model, and the page does not pretend to. It removes the waiting you do not need: exact edits never reach a model, and the agent route starts with the window's contents in hand, so the agent does not spend turns finding out what is on the canvas.</p>
        </section>

        <section class="dv-sec" id="understands"><h2>How it understands</h2>
          <p>The chat lives inside the app, so it does not guess from a screenshot. It keeps a live catalog of the window and refreshes it on every change to the graph.</p>
          <ul class="dev-list">
            <li><strong>Nodes.</strong> Titles, types and positions, so <code>@oscillator</code> means the node you mean, even when two share a title.</li>
            <li><strong>Parameters.</strong> For each node: every control, its label, range, step and enum values, and whether it can be modulated.</li>
            <li><strong>References.</strong> A name you type becomes an exact reference, so an edit lands on the right node and the right control.</li>
            <li><strong>Plain replies.</strong> Agents are asked to write nodes as links, changes as <em>old → new</em>, and to end with a short list of what changed.</li>
          </ul>
        </section>

        <section class="dv-sec" id="accounts"><h2>Frontier models, through your accounts</h2>
          <p>The intelligence comes from the agents you already use. Infinite connects them to your canvas; it does not resell them and does not sit between you and the provider.</p>
          <div class="table-wrap"><table>
            <thead><tr><th>Agent</th><th>From</th><th>Runs as</th><th>Signing in</th></tr></thead>
            <tbody>{arows}</tbody>
          </table></div>
          <ul class="dev-list">
            <li><strong>Sign-in happens in your browser</strong>, on the provider's own page. Infinite never asks for, sees or stores your password.</li>
            <li><strong>Your plan and your limits.</strong> Usage counts against your own subscription or key, the same as using the agent anywhere else.</li>
            <li><strong>One setup card.</strong> A first run checks the computer, installs what is missing into Infinite's own folder, connects the agent and signs you in, all from the chat. A failed step says why in one sentence.</li>
            <li><strong>One model picker.</strong> The chat shows the models of the agent that is answering, and Settings holds a default for each of the three. Each reply shows its token count, and Settings shows usage per agent with a Reset.</li>
            <li><strong>Add more than one.</strong> When two agents work at once, the chat shows who is doing what.</li>
          </ul>
        </section>

        <section class="dv-sec" id="control"><h2>You stay in control</h2>
          <ul class="dev-list">
            <li><strong>Undo everything.</strong> Every change card carries an Undo, and a direct line is one undo step even when it joins several clauses with <em>and</em>.</li>
            <li><strong>Refusals are shown.</strong> If an agent tries something it is not allowed to do, the chat says so in plain words instead of failing silently.</li>
            <li><strong>You choose the folder.</strong> The agent works in the folder you set in Settings, or the open patch's folder.</li>
            <li><strong>Local history.</strong> Saved chats stay on your computer, and Settings shows how many there are with a Clear all button.</li>
            <li><strong>Prefer a terminal?</strong> The terminal button opens a real terminal on the right, beside the chat.</li>
          </ul>
        </section>

        <section class="dv-sec" id="start"><h2>Get started</h2>
          <ol class="dev-list">
            <li><a href="index.html#download">Download Infinite</a> and open a patch.</li>
            <li>Open the chat panel and pick Claude, ChatGPT or Gemini. No account yet? Press <em>I don't have one</em> and make it on the provider's page.</li>
            <li>Infinite installs what is missing by itself: no terminal, no password. Finish signing in in the browser.</li>
            <li>Press one of the three ideas, or type <code>@</code>, pick a node and say what you want.</li>
          </ol>
          <p>Building tools of your own? The <a href="developers.html">developers page</a> covers the command line, Python SDK and MCP server that the chat is built on.</p>
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
    head = head.replace('<link rel="stylesheet" href="dev.css', '<link rel="stylesheet" href="chat.css?v=2">\n  <link rel="stylesheet" href="dev.css', 1)
    rest = re.sub(r'\s*<script src="audience\.js[^"]*" defer></script>', "", rest)
    return head, rest


def main():
    head, rest = head_and_tail()
    out = os.path.join(W, "chat.html")
    open(out, "w", encoding="utf-8").write(head + main_html() + "\n  " + rest)
    print("written: chat.html")


if __name__ == "__main__":
    main()
