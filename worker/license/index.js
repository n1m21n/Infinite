// Infinite license Worker: Paddle webhook -> signed license -> email. Offline-verifiable keys (Ed25519).
//   POST /api/paddle/webhook   Paddle notification. transaction.completed issues a license; a refund adjustment revokes it.
//   GET  /api/license/revoked  {"revoked":[ids]}  optional online check for the plugin
//   POST /api/license/resend   {email} -> re-emails every active key on file for that address (always answers ok)
//   POST /api/license/notify-preorders  Bearer ADMIN_TOKEN, {send?} -> launch-day download mail to every pre-order buyer
//                              not yet told (dry run unless send; 40 per call, answers {pending, sent, left}).
//                              tools/license/notify_preorders.mjs drives it.
// Secrets: PADDLE_WEBHOOK_SECRET, PADDLE_API_KEY, LICENSE_PRIVATE_KEY (Ed25519, PKCS8 base64), RESEND_API_KEY (optional), ADMIN_TOKEN
// Binding: DB (D1). Vars: PADDLE_API, PRICE_IDS (comma list), FROM, SEATS, MAJOR, RELEASED ("1" once downloads exist), DOWNLOAD_URL

const enc = new TextEncoder();
const b64u = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const unb64 = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));
const hex = (buf) => [...new Uint8Array(buf)].map((x) => x.toString(16).padStart(2, "0")).join("");
const EMAIL = /^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,}$/;
const json = (status, obj) => new Response(JSON.stringify(obj), { status, headers: { "Content-Type": "application/json" } });

export async function verifyPaddle(secret, header, raw, now = Date.now()) {
  const parts = Object.fromEntries(String(header || "").split(";").map((p) => p.split("=")));
  if (!parts.ts || !parts.h1 || Math.abs(now / 1000 - Number(parts.ts)) > 300) return false;
  const key = await crypto.subtle.importKey("raw", enc.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const mac = hex(await crypto.subtle.sign("HMAC", key, enc.encode(parts.ts + ":" + raw)));
  return mac.length === parts.h1.length && [...mac].reduce((d, c, i) => d | (c.charCodeAt(0) ^ parts.h1.charCodeAt(i)), 0) === 0;
}

export function licenseId() {
  const a = crypto.getRandomValues(new Uint8Array(8));
  const s = [...a].map((x) => "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"[x % 32]).join("");
  return `INF-${s.slice(0, 4)}-${s.slice(4)}`;
}

export async function signLicense(env, payload) {
  const key = await crypto.subtle.importKey("pkcs8", unb64(env.LICENSE_PRIVATE_KEY), { name: "Ed25519" }, false, ["sign"]);
  const body = b64u(enc.encode(JSON.stringify(payload)));
  return `INF1.${body}.${b64u(await crypto.subtle.sign("Ed25519", key, enc.encode("INF1." + body)))}`;
}

async function paddleGet(env, path) {
  const r = await fetch(env.PADDLE_API + path, { headers: { Authorization: `Bearer ${env.PADDLE_API_KEY}` } });
  if (!r.ok) throw new Error(`paddle ${path} ${r.status}`);
  return (await r.json()).data;
}

async function sendKeyEmail(env, lic, shipped = false) {
  if (!env.RESEND_API_KEY) return false;
  const released = env.RELEASED === "1";
  const text = [
    shipped ? `The Infinite DAW plugin is out${lic.name ? ", " + lic.name.split(" ")[0] : ""}. Thank you for pre-ordering.`
            : `Thank you for buying Infinite${lic.name ? ", " + lic.name.split(" ")[0] : ""}.`, "",
    `Your license ID: ${lic.id}`,
    `Seats: ${lic.seats} computers · Updates: all of version ${lic.major}.x`, "",
    "Your license file is attached (license.inflic). In the plugin, open Settings > License and drop the file in, or paste this key:", "",
    lic.token, "",
    released ? `Download: ${env.DOWNLOAD_URL}\nInstall: https://infinitedevices.org/plugin` : "The plugin is still being finished. Your key already works and is saved; we will email the download link the day it ships.", "",
    "Lost this email? Ask for it again at https://infinitedevices.org/buy#resend", "Questions: reply to this email.",
  ].join("\n");
  const r = await fetch("https://api.resend.com/emails", {
    method: "POST", headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
    body: JSON.stringify({ from: env.FROM, to: [lic.email], subject: shipped ? "Infinite is ready to download" : released ? "Your Infinite license" : "Your Infinite license (pre-order)", text,
      attachments: [{ filename: "license.inflic", content: btoa(lic.token) }] }),
  });
  return r.ok;
}

async function issue(env, txn) {
  const priceIds = String(env.PRICE_IDS || "").split(",").filter(Boolean);
  if (!txn.items?.some((i) => priceIds.includes(i.price?.id))) return { ignored: "not an Infinite price" };
  const existing = await env.DB.prepare("SELECT * FROM licenses WHERE txn_id = ?").bind(txn.id).first();
  let lic = existing;
  if (!lic) {
    const cust = await paddleGet(env, `/customers/${txn.customer_id}`);
    if (!EMAIL.test(cust.email || "")) throw new Error("customer has no usable email");
    const id = licenseId(), seats = Number(env.SEATS || 2), major = Number(env.MAJOR || 1), iat = Math.floor(Date.now() / 1000);
    const token = await signLicense(env, { v: 1, id, email: cust.email, txn: txn.id, seats, major, iat });
    lic = { id, txn_id: txn.id, customer_id: txn.customer_id, email: cust.email, name: cust.name || "", seats, major, token, status: "active", env: env.PADDLE_API.includes("sandbox") ? "sandbox" : "live" };
    await env.DB.prepare("INSERT INTO licenses (id, txn_id, customer_id, email, name, seats, major, token, status, env, created_at) VALUES (?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT (txn_id) DO NOTHING")
      .bind(lic.id, lic.txn_id, lic.customer_id, lic.email, lic.name, lic.seats, lic.major, lic.token, lic.status, lic.env, new Date().toISOString()).run();
  }
  if (!lic.emailed_at && (await sendKeyEmail(env, lic))) await env.DB.prepare("UPDATE licenses SET emailed_at = ? WHERE txn_id = ?").bind(new Date().toISOString(), txn.id).run();
  return { issued: lic.id };
}

async function webhook(request, env) {
  const raw = await request.text();
  if (!(await verifyPaddle(env.PADDLE_WEBHOOK_SECRET, request.headers.get("Paddle-Signature"), raw))) return json(401, { ok: false });
  const ev = JSON.parse(raw);
  if (ev.event_type === "transaction.completed") return json(200, { ok: true, ...(await issue(env, ev.data)) });
  if (ev.event_type === "adjustment.updated" && ev.data.action === "refund" && ev.data.status === "approved") {
    await env.DB.prepare("UPDATE licenses SET status = 'revoked', revoked_at = ? WHERE txn_id = ? AND status = 'active'").bind(new Date().toISOString(), ev.data.transaction_id).run();
    return json(200, { ok: true, revoked: ev.data.transaction_id });
  }
  return json(200, { ok: true, ignored: ev.event_type });
}

async function resend(request, env) {
  let b; try { b = await request.json(); } catch { return json(400, { ok: false }); }
  const email = String(b.email || "").trim().toLowerCase();
  if (EMAIL.test(email)) {
    const { results } = await env.DB.prepare("SELECT * FROM licenses WHERE lower(email) = ? AND status = 'active' AND (resent_at IS NULL OR resent_at < ?)")
      .bind(email, new Date(Date.now() - 10 * 60000).toISOString()).all();
    for (const lic of results || []) { await sendKeyEmail(env, lic); await env.DB.prepare("UPDATE licenses SET resent_at = ? WHERE id = ?").bind(new Date().toISOString(), lic.id).run(); }
  }
  return json(200, { ok: true });   // same answer whether or not the address has a license
}

const BATCH = 40;   // stays under the Worker's subrequest cap (one mail = one fetch)
async function notifyPreorders(request, env) {
  const auth = request.headers.get("Authorization") || "";
  if (!env.ADMIN_TOKEN || auth !== `Bearer ${env.ADMIN_TOKEN}`) return json(401, { ok: false });
  let b = {}; try { b = await request.json(); } catch {}
  const pending = "FROM licenses WHERE status = 'active' AND env = 'live' AND download_emailed_at IS NULL";
  const count = async () => (await env.DB.prepare(`SELECT COUNT(*) AS n ${pending}`).first()).n;
  if (!b.send) return json(200, { ok: true, dryRun: true, pending: await count(), released: env.RELEASED === "1" });
  if (env.RELEASED !== "1" || !env.DOWNLOAD_URL) return json(409, { ok: false, error: "set RELEASED=1 and DOWNLOAD_URL first" });
  const { results } = await env.DB.prepare(`SELECT * ${pending} ORDER BY created_at LIMIT ?`).bind(BATCH).all();
  const sent = [], failed = [];
  for (const lic of results || []) {
    if (await sendKeyEmail(env, lic, true)) {
      await env.DB.prepare("UPDATE licenses SET download_emailed_at = ? WHERE id = ?").bind(new Date().toISOString(), lic.id).run();
      sent.push(lic.id);
    } else failed.push(lic.id);
  }
  return json(200, { ok: true, sent, failed, left: await count() });
}

export default {
  async fetch(request, env) {
    const { pathname } = new URL(request.url);
    try {
      if (request.method === "POST" && pathname === "/api/paddle/webhook") return await webhook(request, env);
      if (request.method === "POST" && pathname === "/api/license/resend") return await resend(request, env);
      if (request.method === "POST" && pathname === "/api/license/notify-preorders") return await notifyPreorders(request, env);
      if (request.method === "GET" && pathname === "/api/license/revoked") {
        const { results } = await env.DB.prepare("SELECT id FROM licenses WHERE status = 'revoked'").all();
        return new Response(JSON.stringify({ revoked: (results || []).map((r) => r.id) }), { headers: { "Content-Type": "application/json", "Cache-Control": "public, max-age=3600", "Access-Control-Allow-Origin": "*" } });
      }
    } catch (e) { console.error(e.message); return json(500, { ok: false }); }   // 500 makes Paddle retry
    return json(404, { ok: false });
  },
};
