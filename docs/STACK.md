# Accounts and tools

Every outside service Infinite's website and outreach depend on, who holds it, and where its settings live. No passwords or keys belong in this file: it only says where they are kept.

Update this file in the same change that adds, moves or removes a service.

| What | Service | Account | What it does | Where the settings are |
|---|---|---|---|---|
| Domain name | Hostinger | Naman | `infinitedevices.org` is registered here; its nameservers point to Cloudflare | Hostinger, Domains |
| DNS and proxy | Cloudflare | `Namansoniii21@gmail.com` | DNS for `infinitedevices.org`; proxies the site and routes `/api/*` to the signup Worker | Cloudflare, Domains, DNS |
| Website hosting | GitHub Pages | repo `n1m21n/Infinite` | Serves `website/`; `.github/workflows/deploy-pages.yml` deploys every push to `main` | GitHub, repo Settings, Pages |
| Email signup | Cloudflare Worker `old-truth-b4c0` | Cloudflare | Receives the footer and audience-page form, writes to D1. Code: `worker/signup/` | Cloudflare, Workers & Pages, Settings |
| Email list | Cloudflare D1 `infinite_signups` | Cloudflare | Table `subscribers` (email, audience, status, source, dates) | Cloudflare, Storage & databases, D1, Console |
| Web analytics | Google Analytics | Naman | Page views (tag `G-ZC4TYX1WCL` in every page head) | analytics.google.com |
| Community | Discord | Naman | Support and chat | link in `tools/website/sync_chrome.py` |
| Video | YouTube `@infinite.devices` | Naman | Tutorials and films | youtube.com/@infinite.devices |
| Social | Instagram `@infinite.devices` | Naman | Visuals and updates | instagram.com/infinite.devices |
| Donations | PayPal | Naman | Donate link in the footer | link in `tools/website/sync_chrome.py` |
| Article drafts | dev.to | Naman | `tools/spread/syndicate.py` creates drafts only; nothing is posted automatically | `tools/spread/README.md` |

## Not set up yet

- **Confirmation emails.** Signups stay `pending`. To send a confirm link, create a Resend account, verify `infinitedevices.org`, and add a `RESEND_API_KEY` secret to the Worker (see `worker/signup/README.md`).
- **Bot check.** A Cloudflare Turnstile widget would stop automated signups. Add its site key to `website/signup.json` and its secret to the Worker as `TURNSTILE_SECRET`.

## Signup Worker settings

These live in the Cloudflare dashboard, not in git (the database id is in `worker/signup/wrangler.toml`):

- Binding `DB` points at D1 `infinite_signups`.
- Variables `SITE_ORIGIN` and `API_ORIGIN` are `https://infinitedevices.org`.
- Secret `SIGNING_SECRET` signs confirm and unsubscribe links. Changing it invalidates links already sent.
- Route `infinitedevices.org/api/*` on the `infinitedevices.org` zone.

## Everyday tasks

- **See who signed up:** Cloudflare, D1, `infinite_signups`, Console, `SELECT * FROM subscribers;`
- **Count by audience:** `SELECT audience, status, COUNT(*) FROM subscribers GROUP BY 1, 2;`
- **Remove someone:** `DELETE FROM subscribers WHERE email = '...';`
- **Turn the form off:** delete `website/signup.json` and run `python3 tools/website/sync_chrome.py`.
- **Rotate the signing secret:** edit `SIGNING_SECRET` in the Worker settings, then deploy.
