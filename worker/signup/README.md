# Sign-up Worker (Cloudflare Worker + Resend)

Double opt-in email list with no database. `POST /api/subscribe` sends a confirmation email through Resend; the signed link in it (`GET /api/confirm`) adds the address to that audience's Resend list. Unsubscribe is Resend's own link in every broadcast. Tested with `node worker/signup/test.mjs` (fake Resend, no keys).

## Turn it on (Naman creates the accounts)

1. **Resend:** add and verify the domain `infinitedevices.org` (DNS records), create one Audience per persona (general, artists, musicians, vjs, creative-coders, developers), create an API key.
2. **Cloudflare:** `cd worker/signup`, `npx wrangler secret put RESEND_API_KEY`, `SIGNING_SECRET` (any long random string), `TURNSTILE_SECRET` (from a free Turnstile widget for infinitedevices.org).
3. Paste the six audience ids into `AUDIENCES` in `wrangler.toml`; set `FROM` to an address on the verified domain. Route the Worker at `infinitedevices.org/api/*` (the DNS record for the site must be proxied by Cloudflare), then `npx wrangler deploy`.
4. Create `website/signup.json`: `{"endpoint": "/api/subscribe", "turnstile_site_key": "<site key>"}`, run `python3 tools/website/sync_chrome.py`, commit. The footer sign-up box only appears once that file exists, so the site never shows a dead form.

## Rules
Collect only the address and the audience page it came from. No address is saved until the link is pressed. Nothing is sent except the confirmation and broadcasts you write and send yourself (`tools/seo/RULES.md`). The free Resend tier limits sends per day and per month; check the current limits before a first broadcast.
