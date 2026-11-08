-- npx wrangler d1 execute infinite_licenses --remote --file=schema.sql
CREATE TABLE IF NOT EXISTS licenses (
  id          TEXT PRIMARY KEY,            -- INF-XXXX-XXXX, shown to the buyer, used for revocation
  txn_id      TEXT NOT NULL UNIQUE,        -- Paddle transaction: one license per purchase, retries are safe
  customer_id TEXT NOT NULL,
  email       TEXT NOT NULL,
  name        TEXT,
  seats       INTEGER NOT NULL,
  major       INTEGER NOT NULL,            -- updates covered: this major version
  token       TEXT NOT NULL,               -- the signed key (same text as license.inflic)
  status      TEXT NOT NULL DEFAULT 'active',  -- active | revoked
  env         TEXT NOT NULL,               -- sandbox | live
  created_at  TEXT NOT NULL,
  emailed_at  TEXT,
  resent_at   TEXT,
  revoked_at  TEXT,
  download_emailed_at TEXT                 -- launch-day download mail sent (notify-preorders)
);
-- Existing table: npx wrangler d1 execute infinite_licenses --remote --command "ALTER TABLE licenses ADD COLUMN download_emailed_at TEXT"
CREATE INDEX IF NOT EXISTS licenses_email ON licenses (email);
