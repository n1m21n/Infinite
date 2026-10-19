-- wrangler d1 execute infinite-signup --remote --file=schema.sql
CREATE TABLE IF NOT EXISTS subscribers (
  email      TEXT NOT NULL,
  audience   TEXT NOT NULL,                 -- artists, musicians, vjs, creative-coders, developers, general
  status     TEXT NOT NULL DEFAULT 'pending', -- pending -> confirmed -> unsubscribed
  source     TEXT,                          -- the page the form was on
  created_at TEXT NOT NULL,
  confirmed_at TEXT,
  unsubscribed_at TEXT,
  PRIMARY KEY (email, audience)
);
CREATE INDEX IF NOT EXISTS subscribers_status ON subscribers (status);
