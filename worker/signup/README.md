# Sign-up Worker (Cloudflare Worker + D1)

Collects email addresses into a Cloudflare D1 database. Nothing outside Cloudflare is needed to collect. Tested with `node worker/signup/test.mjs` (in-memory database, no keys).

| Route | What it does |
|---|---|
| `POST /api/subscribe` | Saves `email`, audience page and source page as `pending`. Duplicates are ignored. Honeypot field and optional Turnstile block bots. |
| `GET /api/confirm?t=` | Signed link from the confirmation email: `pending` becomes `confirmed`. Used once a sender exists. |
| `GET /api/unsubscribe?t=` | Signed link for the bottom of every email: becomes `unsubscribed`. |

Saved per person: address, audience, source page, timestamps, status. No IP, no tracking. Until a sender is set up (`RESEND_API_KEY`), nobody is emailed, so rows stay `pending`; the form tells people we will email once to confirm. Do not send any newsletter to `pending` rows: send them the confirmation first (`tools/seo/RULES.md`).

## Turn it on (Naman, in the Cloudflare account)

1. `cd worker/signup`, `npx wrangler d1 create infinite_signups`, paste the `database_id` into `wrangler.toml`, then `npx wrangler d1 execute infinite-signup --remote --file=schema.sql`.
2. `npx wrangler secret put SIGNING_SECRET` (any long random string). Optional: a free Turnstile widget for infinitedevices.org, then `npx wrangler secret put TURNSTILE_SECRET`.
3. Route it at `infinitedevices.org/api/*` (uncomment `routes` in `wrangler.toml`; the domain's DNS record must be proxied by Cloudflare), then `npx wrangler deploy`.
4. Create `website/signup.json`: `{"endpoint": "/api/subscribe", "turnstile_site_key": "<site key, optional>"}`, run `python3 tools/website/sync_chrome.py`, commit. The footer box only appears once that file exists, so the site never shows a dead form.

## Reading and exporting the list

```
npx wrangler d1 execute infinite_signups --remote --command "SELECT audience, status, COUNT(*) FROM subscribers GROUP BY 1, 2"
npx wrangler d1 export infinite_signups --remote --output list.sql
```

## Later: a sender
Any sender works through its API. Set `RESEND_API_KEY` (and a verified `FROM`) and subscribe emails the confirmation immediately; or send confirmations to the `pending` rows in a batch. Check the sender's daily send limit first.
