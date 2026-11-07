#!/usr/bin/env bash
# Switch the license Worker and the buy page from Paddle sandbox to live.
# Run only after Paddle shows infinitedevices.org as Approved.
# Needs ~/.paddle-live.env (PADDLE_API_KEY) and ~/.paddle-live-webhook.env (PADDLE_WEBHOOK_SECRET).
set -euo pipefail
cd "$(dirname "$0")"

LIVE_PRICE="pri_01m6tx5hh9r03f9cn6qpqpsy8z"
LIVE_TOKEN="live_c4ef74c24d882b8308fe57a96ae"

set -a; . ~/.paddle-live.env; . ~/.paddle-live-webhook.env; set +a
[[ "$PADDLE_API_KEY" == pdl_live_* ]] || { echo "live key missing"; exit 1; }

sed -i '' -E \
  -e "s#^PADDLE_API = .*#PADDLE_API = \"https://api.paddle.com\"#" \
  -e "s#^PRICE_IDS = .*#PRICE_IDS = \"$LIVE_PRICE\"#" wrangler.toml

cat > ../../website/paddle.json <<EOF
{
  "environment": "live",
  "token": "$LIVE_TOKEN",
  "priceId": "$LIVE_PRICE",
  "launchCode": "LAUNCH79",
  "regular": 99,
  "launch": 79,
  "released": false
}
EOF

printf '%s' "$PADDLE_API_KEY"        | npx wrangler secret put PADDLE_API_KEY
printf '%s' "$PADDLE_WEBHOOK_SECRET" | npx wrangler secret put PADDLE_WEBHOOK_SECRET
npx wrangler deploy

cd ../..
git diff --stat
echo "Worker is live. Commit and push website/paddle.json and wrangler.toml to publish the site."
