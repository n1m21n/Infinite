// node worker/signup/test.mjs : exercises the Worker with an in-memory D1 and a fake Resend (no network, no keys)
import worker, { makeToken, readToken, unsubscribeLink } from "./index.js";
import assert from "node:assert/strict";
const rows = new Map();
const DB = { prepare(sql) { return { bind(...a) { return { async run() {
  if (sql.startsWith("INSERT")) { const k = a[0] + "|" + a[1]; if (!rows.has(k)) rows.set(k, { email: a[0], audience: a[1], status: "pending", source: a[2] }); }
  else { const m = sql.match(/SET status = \?, (\w+)/); const r = rows.get(a[2] + "|" + a[3]); r.status = a[0]; r[m[1]] = a[1]; }
} }; } }; } };
const env = { SITE_ORIGIN: "https://x.test", API_ORIGIN: "https://x.test", FROM: "a <a@x.test>", SIGNING_SECRET: "s3cret", DB };
const sent = [];
globalThis.fetch = async (u, o) => { sent.push(JSON.parse(o.body)); return new Response("{}", { status: 200 }); };
const post = (b) => worker.fetch(new Request("https://x.test/api/subscribe", { method: "POST", body: JSON.stringify(b) }), env);
const get = (u) => worker.fetch(new Request(u), env);

assert.equal((await post({ email: "nope" })).status, 400);
assert.equal((await post({ email: "a@b.co", company: "bot" })).status, 200); assert.equal(rows.size, 0, "honeypot stores nothing");
let r = await post({ email: "A@B.co", audience: "vjs", source: "vjs.html" }); assert.equal(r.status, 200);
assert.equal(rows.get("a@b.co|vjs").status, "pending"); assert.equal(sent.length, 0, "no sender configured: nothing emailed");
await post({ email: "a@b.co", audience: "vjs" }); assert.equal(rows.size, 1, "duplicate ignored");
await post({ email: "a@b.co", audience: "nonsense" }); assert.ok(rows.has("a@b.co|general"), "unknown audience falls back");

env.RESEND_API_KEY = "k"; env.TURNSTILE_SECRET = ""; // sender configured: double opt-in email goes out
await post({ email: "c@d.co", audience: "artists" });
const link = sent[0].text.match(/https:\/\/\S+/)[0];
assert.match(await (await get(link)).text(), /on the list/); assert.equal(rows.get("c@d.co|artists").status, "confirmed");
const t = new URL(link).searchParams.get("t");
assert.equal(await readToken("wrong", t), null); assert.equal(await readToken("s3cret", t + "x"), null);
assert.equal(await readToken("s3cret", await makeToken("s3cret", { email: "x", exp: 1 })), null, "expired rejected");
assert.match(await (await get(await unsubscribeLink(env, "c@d.co", "artists"))).text(), /unsubscribed/); assert.equal(rows.get("c@d.co|artists").status, "unsubscribed");
assert.match(await (await get("https://x.test/api/unsubscribe?t=" + encodeURIComponent(t))).text(), /not valid/, "confirm token cannot unsubscribe");
console.log("ok");
