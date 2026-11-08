// node worker/license/test.mjs : Paddle signature, issuing, idempotence, refund, resend, signature check. In-memory D1, fake Paddle/Resend.
import worker, { verifyPaddle } from "./index.js";
import assert from "node:assert/strict";
import { generateKeyPairSync, createPublicKey, verify } from "node:crypto";
import { createHmac } from "node:crypto";

const { publicKey, privateKey } = generateKeyPairSync("ed25519");
const rows = new Map();
const notifyPending = () => [...rows.values()].filter((r) => r.status === "active" && r.env === "live" && !r.download_emailed_at);
const DB = { prepare(sql) { const mk = (a) => ({
  async first() {
    if (sql.includes("COUNT(*)")) return { n: notifyPending().length };
    return rows.get(a[0]) || null;
  },
  async all() {
    if (sql.includes("download_emailed_at IS NULL")) return { results: notifyPending().slice(0, a[0]) };
    if (sql.includes("status = 'revoked'")) return { results: [...rows.values()].filter((r) => r.status === "revoked") };
    return { results: [...rows.values()].filter((r) => r.email.toLowerCase() === a[0] && r.status === "active" && (!r.resent_at || r.resent_at < a[1])) };
  },
  async run() {
    if (sql.startsWith("INSERT")) { if (!rows.has(a[1])) rows.set(a[1], { id: a[0], txn_id: a[1], customer_id: a[2], email: a[3], name: a[4], seats: a[5], major: a[6], token: a[7], status: a[8], env: a[9] }); }
    else if (sql.includes("download_emailed_at")) [...rows.values()].find((r) => r.id === a[1]).download_emailed_at = a[0];
    else if (sql.includes("emailed_at")) rows.get(a[1]).emailed_at = a[0];
    else if (sql.includes("resent_at")) [...rows.values()].find((r) => r.id === a[1]).resent_at = a[0];
    else if (sql.includes("'revoked'")) { const r = rows.get(a[1]); if (r?.status === "active") { r.status = "revoked"; r.revoked_at = a[0]; } }
  } });
  return { bind: (...a) => mk(a), ...mk([]) }; } };
const env = { DB, PADDLE_API: "https://sandbox-api.paddle.com", PRICE_IDS: "pri_ok", FROM: "x <x@x.test>", SEATS: "2", MAJOR: "1", RELEASED: "0",
  ADMIN_TOKEN: "adm", PADDLE_WEBHOOK_SECRET: "whsec", PADDLE_API_KEY: "k", RESEND_API_KEY: "r", LICENSE_PRIVATE_KEY: privateKey.export({ type: "pkcs8", format: "der" }).toString("base64") };
const mails = []; let customerCalls = 0;
globalThis.fetch = async (u, o) => {
  if (u.includes("/customers/")) { customerCalls++; return new Response(JSON.stringify({ data: { email: "Buyer@Example.com", name: "Ada Lovelace" } })); }
  if (u.includes("resend.com")) { mails.push(JSON.parse(o.body)); return new Response("{}"); }
  throw new Error("unexpected fetch " + u);
};
const send = (ev, { ts = Math.floor(Date.now() / 1000), secret = "whsec" } = {}) => {
  const raw = JSON.stringify(ev);
  const h1 = createHmac("sha256", secret).update(ts + ":" + raw).digest("hex");
  return worker.fetch(new Request("https://x.test/api/paddle/webhook", { method: "POST", headers: { "Paddle-Signature": `ts=${ts};h1=${h1}` }, body: raw }), env);
};
const txn = { event_type: "transaction.completed", data: { id: "txn_1", customer_id: "ctm_1", items: [{ price: { id: "pri_ok" } }] } };

assert.equal((await send(txn, { secret: "wrong" })).status, 401, "bad signature rejected");
assert.equal((await send(txn, { ts: 1000 })).status, 401, "stale timestamp rejected");
assert.equal((await send({ ...txn, data: { ...txn.data, items: [{ price: { id: "pri_other" } }] } })).status, 200); assert.equal(rows.size, 0, "other products ignored");

let r = await (await send(txn)).json(); assert.match(r.issued, /^INF-[A-Z0-9]{4}-[A-Z0-9]{4}$/);
assert.equal(rows.size, 1); assert.equal(mails.length, 1);
const lic = rows.get("txn_1");
assert.equal(lic.email, "Buyer@Example.com"); assert.match(mails[0].subject, /pre-order/); assert.match(mails[0].text, /still being finished/);
assert.equal(Buffer.from(mails[0].attachments[0].content, "base64").toString(), lic.token, "attachment is the key");

const [tag, body, sig] = lic.token.split(".");   // the plugin's offline check, done here with node's own crypto
assert.equal(tag, "INF1");
assert.ok(verify(null, Buffer.from(tag + "." + body), createPublicKey(publicKey.export({ type: "spki", format: "pem" })), Buffer.from(sig, "base64url")), "signature verifies with the public key");
const p = JSON.parse(Buffer.from(body, "base64url")); assert.deepEqual([p.id, p.txn, p.seats, p.major], [lic.id, "txn_1", 2, 1]);
const tampered = tag + "." + Buffer.from(JSON.stringify({ ...p, seats: 99 })).toString("base64url");
assert.ok(!verify(null, Buffer.from(tampered), createPublicKey(publicKey.export({ type: "spki", format: "pem" })), Buffer.from(sig, "base64url")), "tampering breaks it");

await send(txn); assert.equal(rows.size, 1, "retry creates no second license"); assert.equal(mails.length, 1, "retry sends no second mail"); assert.equal(customerCalls, 1);

const resend = (email) => worker.fetch(new Request("https://x.test/api/license/resend", { method: "POST", body: JSON.stringify({ email }) }), env);
assert.equal((await resend("nobody@x.co")).status, 200); assert.equal(mails.length, 1, "unknown address: same answer, no mail");
await resend("buyer@example.com"); assert.equal(mails.length, 2, "resend mails the key");
await resend("buyer@example.com"); assert.equal(mails.length, 2, "resend rate-limited");

env.RELEASED = "1"; env.DOWNLOAD_URL = "https://x.test/dl"; rows.set("txn_2", { ...lic, id: "INF-AAAA-BBBB", txn_id: "txn_2", resent_at: null, email: "z@z.co", emailed_at: null }); rows.get("txn_2").emailed_at = null;
await resend("z@z.co"); assert.match(mails.at(-1).text, /Download: https:\/\/x.test\/dl/);

await send({ event_type: "adjustment.updated", data: { action: "refund", status: "pending_approval", transaction_id: "txn_1" } }); assert.equal(rows.get("txn_1").status, "active", "pending refund keeps the key");
await send({ event_type: "adjustment.updated", data: { action: "refund", status: "approved", transaction_id: "txn_1" } }); assert.equal(rows.get("txn_1").status, "revoked");
const rev = await (await worker.fetch(new Request("https://x.test/api/license/revoked"), env)).json(); assert.deepEqual(rev.revoked, [lic.id]);
assert.equal(await verifyPaddle("a", "", "{}"), false);
// Launch-day pre-order mail: guarded, dry run by default, refuses before release, sends once per live active key.
const notify = (body, token = "adm") => worker.fetch(new Request("https://x.test/api/license/notify-preorders",
  { method: "POST", headers: { Authorization: `Bearer ${token}` }, body: JSON.stringify(body) }), env).then(async (r) => ({ status: r.status, ...(await r.json()) }));
for (let i = 0; i < 45; i++) rows.set(`txn_p${i}`, { ...lic, id: `INF-P${i}`, txn_id: `txn_p${i}`, email: `p${i}@p.co`, status: "active", env: "live", download_emailed_at: null, created_at: String(i).padStart(3, "0") });
rows.get("txn_p3").status = "revoked"; rows.get("txn_p4").env = "sandbox";
assert.equal((await notify({}, "nope")).status, 401, "notify needs the admin token");
let n = await notify({}); assert.equal(n.dryRun, true); assert.equal(n.pending, 43, "live active only"); const before = mails.length;
env.RELEASED = "0"; assert.equal((await notify({ send: true })).status, 409, "no send before release"); env.RELEASED = "1";
assert.equal(mails.length, before, "dry run and refusal send nothing");
n = await notify({ send: true }); assert.equal(n.sent.length, 40); assert.equal(n.left, 3);
n = await notify({ send: true }); assert.equal(n.sent.length, 3); assert.equal(n.left, 0);
n = await notify({ send: true }); assert.equal(n.sent.length, 0, "second pass sends nothing");
assert.equal(mails.length - before, 43); assert.match(mails.at(-1).subject, /ready to download/); assert.match(mails.at(-1).text, /Download: https:\/\/x.test\/dl/);
assert.ok(!mails.some((m) => m.to[0] === "p3@p.co" || m.to[0] === "p4@p.co"), "revoked and sandbox keys skipped");
console.log("license worker: all checks passed");
