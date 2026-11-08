# License Worker (Cloudflare Worker + D1 + Paddle)

Paddle takes the payment. This Worker turns a completed payment into a signed license key and emails it. Tested with `node worker/license/test.mjs` (in-memory D1, fake Paddle and Resend, no keys).

| Route | What it does |
|---|---|
| `POST /api/paddle/webhook` | Checks Paddle's signature. `transaction.completed` for a price in `PRICE_IDS` makes one license (retries are safe). An approved refund `adjustment.updated` revokes it. |
| `GET /api/license/revoked` | `{"revoked":[ids]}` for the plugin's optional online check. |
| `POST /api/license/resend` | `{email}` re-emails active keys for that address, at most every 10 minutes. Always answers ok. |
| `POST /api/license/notify-preorders` | Bearer `ADMIN_TOKEN`. Dry run by default (`{pending}`); `{send:true}` mails the download link to live active keys with `download_emailed_at IS NULL`, 40 per call. Refuses until `RELEASED = "1"`. |

## The key
`INF1.<payload>.<signature>`: Ed25519 over the payload `{v, id, email, txn, seats, major, iat}`. The plugin verifies it offline with the public key in `PUBLIC_KEY.txt` (32 bytes, hex). The buyer gets it as text and as an attached `license.inflic`.
The private key lives only in the Worker secret `LICENSE_PRIVATE_KEY` and a 0600 backup at `~/.infinite-license-private-key.b64`. Lose both and old keys still verify, but no new ones can be signed with this public key.

## Where things live
Paddle (sandbox now): product, $99 price, discount `LAUNCH79` ($20 off), webhook destination, client token. `website/paddle.json` holds the environment, client token, price id and launch code the buy page uses.

## Going live
1. Paddle live account approved, then create the same product, price and discount there.
2. `wrangler.toml`: `PADDLE_API` to `https://api.paddle.com`, `PRICE_IDS` to the live price id.
3. Live API key and webhook destination, then `wrangler secret put PADDLE_API_KEY` and `PADDLE_WEBHOOK_SECRET`.
4. `website/paddle.json`: `"environment": "live"`, live client token and price id.
5. When downloads exist: `RELEASED = "1"` and `"released": true` in `paddle.json`, deploy, then `node tools/license/notify_preorders.mjs` (dry run) and `--send` on the owner's go.
   One-time setup: the `download_emailed_at` ALTER in `schema.sql`, and `wrangler secret put ADMIN_TOKEN` (same value in `~/.infinite-admin.env`, 0600).
Sandbox and live use separate D1 rows (`env` column), so test purchases never mix with real ones.

## Sending mail
`RESEND_API_KEY` (verified `infinitedevices.org` sender) makes the key email go out. Without it the license is still stored; rows with `emailed_at IS NULL` get their mail on the next resend request.
