import { STEPS } from "./steps.js";
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

const esc = (v) => String(v).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
const ASSET = "https://github.com/n1m21n/Infinite/releases/latest/download/";
const SITE = "https://infinitedevices.org";

// Builds the key email: Midnight brand card (docs/brand/BRAND.md), table layout and inline styles for mail clients.
export function keyEmail(env, lic, shipped = false) {
  const released = env.RELEASED === "1";
  const first = lic.name ? lic.name.split(" ")[0] : "";
  const hello = shipped ? `The Infinite plugin is out${first ? ", " + first : ""}.` : `Thank you for buying Infinite${first ? ", " + first : ""}.`;
  const sub = shipped ? "Thank you for pre-ordering. Here is your download." : released ? "Your key is below. Install, then paste it in." : "The plugin is still being finished. Your key is saved and works; we email the download the day it ships.";
  const page = env.DOWNLOAD_URL || `${SITE}/download.html`;
  const md = (t) => esc(t).replace(/\*\*(.+?)\*\*/g, "<b>$1</b>").replace(/`(.+?)`/g, `<span style="font-family:'Geist Mono',ui-monospace,Menlo,Consolas,monospace;font-size:13px;">$1</span>`);
  const plain = (t) => t.replace(/\*\*(.+?)\*\*|`(.+?)`/g, (_, a, b) => a || b);
  const OS = [["macOS", STEPS.mac], ["Windows", STEPS.windows], ["Linux", STEPS.linux]];
  const subject = shipped ? "Infinite is ready to download" : released ? "Your Infinite licence" : "Your Infinite licence (pre-order)";
  const text = [
    hello, sub, "",
    ...(released ? ["1. Download", `  macOS    ${ASSET}Infinite.pkg`, `  Windows  ${ASSET}Infinite-Setup-windows-x64.exe`, `  Linux    ${ASSET}Infinite-linux-x86_64.tar.gz`, ""]
      : ["1. Download", "  We email the link the day it ships.", ""]),
    "2. Open the installer (not code-signed yet, so your computer asks once)",
    ...OS.flatMap(([n, st]) => [`  ${n}`, ...st.map((t) => `   - ${plain(t)}`)]),
    `  Step by step: ${page}`, "",
    "3. Your key", "", lic.token, "", "  Also attached as license.inflic.", "",
    "4. Play",
    ...STEPS.play.map((t, i) => `  ${i + 1}. ${plain(t)}`), "",
    `Licence ${lic.id} · ${lic.seats} computers · every update for life`,
    `Lost this email? ${SITE}/buy.html#resend`, "Questions: reply to this email.",
  ].join("\n");
  const C = { ground: "#0D1020", card: "#151930", well: "#20263F", line: "#2A3052", ink: "#EEF1FA", ink2: "#A3AACB", ink3: "#8890B1", ember: "#FF6B35", signal: "#A7B1FF" };
  const font = "font-family:Geist,Inter,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;";
  const mono = "font-family:'Geist Mono',ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;";
  const btn = (href, label, primary) => `<a href="${href}" style="${font}display:inline-block;padding:12px 18px;margin:0 8px 8px 0;border-radius:10px;font-size:15px;font-weight:600;text-decoration:none;${primary ? `background:${C.ember};color:${C.card};` : `background:${C.well};color:${C.ink};border:1px solid ${C.line};`}">${label}</a>`;
  const row = (inner, pad = "0 32px 24px") => `<tr><td style="padding:${pad};">${inner}</td></tr>`;
  const label = (t) => `<p style="${font}margin:0 0 12px;font-size:13px;font-weight:600;letter-spacing:0.06em;text-transform:uppercase;color:${C.ink3};">${t}</p>`;
  const list = (items, mark) => `<table role="presentation" cellpadding="0" cellspacing="0" style="margin:0 0 12px;">${items.map((t, i) => `<tr><td valign="top" style="${font}padding:0 12px 8px 0;font-size:15px;font-weight:700;color:${C.ember};">${mark || i + 1}</td><td style="${font}padding:0 0 8px;font-size:15px;line-height:1.5;color:${C.ink};">${md(t)}</td></tr>`).join("")}</table>`;
  const html = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="dark"><title>${esc(subject)}</title></head>
<body style="margin:0;padding:0;background:${C.ground};">
<div style="display:none;max-height:0;overflow:hidden;">${esc(sub)}</div>
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:${C.ground};"><tr><td align="center" style="padding:32px 12px;">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="max-width:560px;background:${C.card};border:1px solid ${C.line};border-radius:24px;">
${row(`<img src="${SITE}/assets/icon-96.png" width="40" height="40" alt="Infinite" style="display:block;border:0;border-radius:10px;">`, "32px 32px 16px")}
${row(`<h1 style="${font}margin:0 0 8px;font-size:26px;line-height:1.2;font-weight:700;letter-spacing:-0.02em;color:${C.ink};">${esc(hello)}</h1><p style="${font}margin:0;font-size:15px;line-height:1.5;color:${C.ink2};">${esc(sub)}</p>`)}
${row(`${label("1 · Download")}${released ? `${btn(ASSET + "Infinite.pkg", "macOS", true)}${btn(ASSET + "Infinite-Setup-windows-x64.exe", "Windows", false)}${btn(ASSET + "Infinite-linux-x86_64.tar.gz", "Linux", false)}` : `<p style="${font}margin:0;font-size:15px;line-height:1.5;color:${C.ink};">We email the download link the day it ships.</p>`}`)}
${row(`${label("2 · Open the installer")}<p style="${font}margin:0 0 12px;font-size:13px;line-height:1.5;color:${C.ink3};">Not code-signed yet, so your computer asks once. <a href="${page}" style="color:${C.signal};">Step by step</a></p>${OS.map(([n, st]) => `<p style="${font}margin:0 0 6px;font-size:13px;font-weight:600;color:${C.ink2};">${n}</p>${list(st, "•")}`).join("")}`)}
${row(`${label("3 · Your key")}
<div style="${mono}padding:14px 16px;border-radius:16px;background:${C.ground};border:1px solid ${C.line};font-size:12px;line-height:1.5;color:${C.ink};word-break:break-all;">${esc(lic.token)}</div>
<p style="${font}margin:8px 0 0;font-size:13px;line-height:1.5;color:${C.ink3};">Also attached as license.inflic.</p>`)}
${row(`${label("4 · Play")}${list(STEPS.play)}`)}
${row(`<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="border-top:1px solid ${C.line};"><tr><td style="${font}padding-top:16px;font-size:13px;line-height:1.6;color:${C.ink3};">Licence <span style="${mono}color:${C.ink2};">${esc(lic.id)}</span> · ${esc(lic.seats)} computers · every update for life<br><a href="${SITE}/buy.html#resend" style="color:${C.signal};">Lost this email?</a> · Questions: reply to this email.</td></tr></table>`, "0 32px 32px")}
</table></td></tr></table></body></html>`;
  return { subject, text, html };
}

async function sendKeyEmail(env, lic, shipped = false) {
  if (!env.RESEND_API_KEY) return false;
  const { subject, text, html } = keyEmail(env, lic, shipped);
  const r = await fetch("https://api.resend.com/emails", {
    method: "POST", headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
    body: JSON.stringify({ from: env.FROM, to: [lic.email], subject, text, html,
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
