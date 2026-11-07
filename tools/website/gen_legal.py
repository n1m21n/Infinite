#!/usr/bin/env python3
"""Writes website/terms.html, refund.html and privacy.html from the frame of buy.html (head, menu, footer, scripts).
Edit the text here, run `python3 tools/website/gen_legal.py && python3 tools/website/sync_chrome.py`. Paddle's domain review
needs all three reachable from navigation; the footer links come from sync_chrome.py."""
import re, pathlib
W = pathlib.Path(__file__).resolve().parents[2] / "website"
UPDATED = "7 November 2026"
SELLER = "Infinite Devices (run by Naman Soni, n1m21n)"
MAIL = "hello@infinitedevices.org"

CSS = """
    .legal { max-width: 760px; margin: 0 auto; padding: 128px var(--ui-sp-4, 16px) 72px; display: grid; gap: var(--ui-sp-3, 14px); }
    .legal h1 { margin: 0; font-size: var(--size-h1); font-weight: 800; letter-spacing: -0.035em; line-height: 1.05; }
    .legal h2 { margin: var(--ui-sp-4, 24px) 0 0; font-size: var(--size-h3, 1.25rem); letter-spacing: -0.02em; }
    .legal p, .legal li { color: var(--muted); line-height: 1.65; margin: 0; }
    .legal ul { margin: 0; padding-left: 1.2em; display: grid; gap: 6px; }
    .legal a { color: var(--accent-ink); }
    .legal .when { font: 500 var(--size-micro-mono, .75rem)/1 'Geist Mono', ui-monospace, monospace; letter-spacing: .12em; text-transform: uppercase; color: var(--muted); }
"""

PAGES = {
"terms.html": ("Terms of Sale and Use", "Terms for buying and using the Infinite plugin: license, payment, support, liability.", f"""
<p>These terms cover the paid Infinite plugin (VST3 and AU), its license key, and updates, sold by {SELLER}, "we". By buying or using it you agree to them.</p>
<h2>What these terms cover</h2>
<p>The Infinite source code is open source under its own licenses, and nothing here limits your rights under those. These terms cover the paid, signed plugin build, the license key service, updates and support.</p>
<h2>Your license</h2>
<ul>
<li>One purchase gives you a personal, non-exclusive, perpetual license for the plugin on up to 2 computers you own or use.</li>
<li>It includes every update within the major version you bought (1.x for a 1.x purchase). A later major version may be sold separately.</li>
<li>Music, video and other work you make with Infinite belongs to you. You may release it commercially.</li>
<li>Do not share, publish or resell your license key or license file.</li>
</ul>
<h2>Buying</h2>
<ul>
<li>Our order process is run by Paddle.com, our reseller and Merchant of Record. Paddle handles payment, tax and receipts. Paddle's own buyer terms apply to the payment.</li>
<li>Prices are shown at checkout and include any tax Paddle adds for your country.</li>
<li>Your license key is emailed to the address you used at checkout. If it does not arrive, ask for it again on the <a href="buy.html#resend">buy page</a> or write to <a href="mailto:{MAIL}">{MAIL}</a>.</li>
</ul>
<h2>Pre-orders</h2>
<p>If the plugin has not shipped when you buy, it is a pre-order. Your key is valid and saved from the day you buy, and we email the download when it is ready. You may cancel for a full refund at any time before it ships. See the <a href="refund.html">refund policy</a>.</p>
<h2>Refunds</h2>
<p>See the <a href="refund.html">refund policy</a>. A refunded license is switched off.</p>
<h2>Support</h2>
<p>Write to <a href="mailto:{MAIL}">{MAIL}</a> or ask on <a href="https://discord.gg/GMeQTyJqkH">Discord</a>. We aim to reply within three working days.</p>
<h2>No warranty and limits on liability</h2>
<p>The plugin is provided as is. We work to keep it reliable, but we do not promise it is error free or fits a particular purpose, and you should keep backups of your projects. To the extent the law allows, our total liability to you for anything connected to the plugin is limited to the price you paid, and we are not liable for lost profit, lost data or indirect loss. Nothing here limits rights you have under consumer law that cannot be excluded.</p>
<h2>Ending the license</h2>
<p>We may switch off a key that is refunded, charged back, or shared in breach of these terms. Your own work is never affected.</p>
<h2>Changes and law</h2>
<p>We may update these terms for future sales; the version in force when you bought applies to your purchase. These terms are governed by the laws of India, without taking away protections you have under the law where you live.</p>
<h2>Contact</h2>
<p>{SELLER}, <a href="mailto:{MAIL}">{MAIL}</a>.</p>
"""),
"refund.html": ("Refund Policy", "Refunds for the Infinite plugin: 14 days, full refund before it ships, how to ask.", f"""
<p>We want you to be happy with Infinite. If it is not right for you, you can get your money back.</p>
<h2>Your refund window</h2>
<ul>
<li><b>Before the plugin ships (pre-order):</b> full refund at any time.</li>
<li><b>After it ships:</b> full refund within 14 days of the day we email you the download link, or 14 days after you buy if that is later.</li>
</ul>
<h2>How to ask</h2>
<p>Email <a href="mailto:{MAIL}">{MAIL}</a> from the address you bought with, or send the receipt Paddle emailed you. Give the order number if you have it. We do not ask you to explain why.</p>
<h2>What happens next</h2>
<ul>
<li>We approve it within three working days and Paddle refunds the original payment method. Banks can take 5 to 10 days to show it.</li>
<li>The refund includes tax that was charged.</li>
<li>Your license key is switched off when the refund goes through.</li>
</ul>
<h2>After the window</h2>
<p>Write to us anyway if something is wrong. If the plugin does not work as described and we cannot fix it, we will refund you.</p>
<h2>Your legal rights</h2>
<p>This policy adds to, and does not reduce, rights you have under the consumer law where you live, including any cooling-off period. You can also ask Paddle directly at paddle.net.</p>
"""),
"privacy.html": ("Privacy Policy", "What Infinite collects when you visit, sign up or buy, who processes it, and how to ask us to delete it.", f"""
<p>{SELLER} runs infinitedevices.org and sells the Infinite plugin. This page says what we collect and why. We do not sell personal data.</p>
<h2>What we collect</h2>
<ul>
<li><b>When you buy:</b> Paddle takes your payment and collects your name, email, country and payment details as Merchant of Record under its own <a href="https://www.paddle.com/legal/privacy">privacy policy</a>. We receive your name, email, country and the order number. We never see your card.</li>
<li><b>Your license:</b> we store your email, name, order number and license ID so we can send and re-send your key and switch it off if refunded.</li>
<li><b>Email list:</b> if you sign up on the site, we store your email, the page you signed up on, and the time. We email you to confirm and when something ships.</li>
<li><b>Visits:</b> the site uses Google Analytics to count visits and see which pages are read. It uses cookies and gives Google your IP address and device details.</li>
<li><b>Messages:</b> anything you send us by email or Discord.</li>
</ul>
<h2>Why we use it</h2>
<ul>
<li>To deliver and support what you bought (contract).</li>
<li>To email you about Infinite if you signed up (consent, withdraw any time).</li>
<li>To keep the site working and learn what to improve (legitimate interest).</li>
<li>To keep tax and accounting records the law requires (legal obligation).</li>
</ul>
<h2>Who handles it</h2>
<p>Paddle (payments, tax, receipts), Cloudflare (hosting rules, database for licenses and sign-ups), Resend (sending email), GitHub (hosting the site pages), Google (analytics). They process data for us under their own terms and may do so outside your country.</p>
<h2>The plugin</h2>
<p>The plugin works offline and needs no account. Your license key is checked on your computer. We will update this page before adding anything that sends data from the plugin to us.</p>
<h2>How long we keep it</h2>
<p>License and order records for as long as you own the license plus the period tax law requires. Email list entries until you unsubscribe. Messages for up to two years.</p>
<h2>Your rights</h2>
<p>You can ask us to show, correct, export or delete the data we hold about you, and to stop emailing you. Every email has a one-click unsubscribe. Write to <a href="mailto:{MAIL}">{MAIL}</a>; we answer within 30 days. You can also complain to your local data protection authority. Deleting license records means we can no longer re-send your key.</p>
<h2>Children</h2>
<p>Infinite is not aimed at children under 13 and we do not knowingly collect their data.</p>
<h2>Changes</h2>
<p>If we change this page in a way that matters, we change the date above and, if you are on the email list, tell you.</p>
<h2>Contact</h2>
<p>{SELLER}, <a href="mailto:{MAIL}">{MAIL}</a>.</p>
"""),
}

tpl = (W / "buy.html").read_text()
main_re = re.compile(r"  <main>.*?</main>", re.S)
for name, (title, desc, body) in PAGES.items():
    h = tpl
    h = re.sub(r"<title>.*?</title>", f"<title>{title} | Infinite</title>", h)
    h = re.sub(r'(<meta name="description" content=")[^"]*', lambda m: m.group(1) + desc, h)
    h = h.replace("https://infinitedevices.org/buy.html", f"https://infinitedevices.org/{name}")
    h = h.replace('  <meta name="robots" content="noindex">\n', "")
    h = re.sub(r"  <style>.*?</style>", "  <style>" + CSS + "  </style>", h, count=1, flags=re.S)
    h = re.sub(r'  <script src="https://cdn.paddle.com[^\n]*\n  <script src="buy.js"[^\n]*\n', "", h)
    main = f'  <main>\n    <article class="legal">\n      <h1 class="perch">{title}</h1>\n      <p class="when">Updated {UPDATED}</p>\n{body}    </article>\n  </main>'
    h = main_re.sub(lambda m: main, h, count=1)
    (W / name).write_text(h)
    print("wrote", name)
