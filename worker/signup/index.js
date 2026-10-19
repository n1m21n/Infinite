// Infinite sign-up Worker: collects emails into Cloudflare D1. No third-party service is needed to collect.
//   POST /api/subscribe     {email, audience, source, turnstile, company} -> row saved as 'pending'
//   GET  /api/confirm?t=    signed link -> 'confirmed'      (only used once a sender is set up: RESEND_API_KEY)
//   GET  /api/unsubscribe?t= signed link -> 'unsubscribed'  (put this link in every email)
// With RESEND_API_KEY set, subscribe also emails the confirmation link (double opt-in). Without it, rows stay
// 'pending' and a later script sends the confirmation to them. Export: see README.
// Secrets: SIGNING_SECRET, TURNSTILE_SECRET (optional), RESEND_API_KEY (optional). Binding: DB (D1).
// Vars: SITE_ORIGIN, API_ORIGIN, FROM

const TOKEN_TTL_MS = 48 * 3600 * 1000;
const PERSONAS = ["artists", "musicians", "vjs", "creative-coders", "developers", "general"];
const EMAIL = /^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,}$/;

const b64 = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const unb64 = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));

async function hmacKey(secret) {
  return crypto.subtle.importKey("raw", new TextEncoder().encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
}

export async function makeToken(secret, payload) {
  const body = b64(new TextEncoder().encode(JSON.stringify(payload)));
  const sig = await crypto.subtle.sign("HMAC", await hmacKey(secret), new TextEncoder().encode(body));
  return body + "." + b64(sig);
}

export async function readToken(secret, token, now = Date.now()) {
  const [body, sig] = String(token || "").split(".");
  if (!body || !sig) return null;
  let ok = false;
  try { ok = await crypto.subtle.verify("HMAC", await hmacKey(secret), unb64(sig), new TextEncoder().encode(body)); } catch { return null; }
  if (!ok) return null;
  const p = JSON.parse(new TextDecoder().decode(unb64(body)));
  return p.exp === undefined || p.exp > now ? p : null;   // unsubscribe tokens carry no expiry
}

const cors = (env) => ({ "Access-Control-Allow-Origin": env.SITE_ORIGIN, "Access-Control-Allow-Methods": "POST, OPTIONS", "Access-Control-Allow-Headers": "Content-Type", "Vary": "Origin" });
const json = (env, status, obj) => new Response(JSON.stringify(obj), { status, headers: { "Content-Type": "application/json", ...cors(env) } });
const page = (title, text) => new Response(`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${title}</title><body style="font:18px/1.5 system-ui,sans-serif;max-width:32em;margin:15vh auto;padding:0 20px"><h1>${title}</h1><p>${text}</p><p><a href="/">Back to Infinite</a></p>`, { status: 200, headers: { "Content-Type": "text/html; charset=utf-8" } });

async function turnstileOk(env, token, ip) {
  if (!env.TURNSTILE_SECRET) return true; // not configured: honeypot only
  const r = await fetch("https://challenges.cloudflare.com/turnstile/v0/siteverify", { method: "POST", body: new URLSearchParams({ secret: env.TURNSTILE_SECRET, response: token || "", remoteip: ip || "" }) });
  return r.ok && (await r.json()).success === true;
}

async function subscribe(request, env) {
  let b;
  try { b = await request.json(); } catch { return json(env, 400, { ok: false, error: "bad request" }); }
  if (b.company) return json(env, 200, { ok: true });                       // honeypot: pretend it worked
  const email = String(b.email || "").trim().toLowerCase();
  const audience = PERSONAS.includes(b.audience) ? b.audience : "general";
  if (!EMAIL.test(email)) return json(env, 400, { ok: false, error: "That does not look like an email address." });
  if (!(await turnstileOk(env, b.turnstile, request.headers.get("CF-Connecting-IP")))) return json(env, 400, { ok: false, error: "Check failed. Please try again." });
  const source = String(b.source || "").slice(0, 80);
  await env.DB.prepare("INSERT INTO subscribers (email, audience, status, source, created_at) VALUES (?, ?, 'pending', ?, ?) ON CONFLICT (email, audience) DO NOTHING")
    .bind(email, audience, source, new Date().toISOString()).run();
  if (env.RESEND_API_KEY) {                                                 // double opt-in as soon as a sender exists
    const t = await makeToken(env.SIGNING_SECRET, { email, audience, exp: Date.now() + TOKEN_TTL_MS });
    const r = await fetch("https://api.resend.com/emails", {
      method: "POST", headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
      body: JSON.stringify({ from: env.FROM, to: [email], subject: "Confirm your email for Infinite", text: confirmText(env, t) }),
    });
    if (!r.ok) return json(env, 200, { ok: true, message: "You are on the list. We will email you once to confirm." });
    return json(env, 200, { ok: true, message: "Check your inbox and press the link to confirm." });
  }
  return json(env, 200, { ok: true, message: "You are on the list. We will email you once to confirm before sending anything else." });
}

export const confirmText = (env, t) => `Confirm that you want occasional news about Infinite (a note when something ships, nothing else):\n\n${env.API_ORIGIN}/api/confirm?t=${encodeURIComponent(t)}\n\nThe link works for 48 hours. If you did not ask for this, ignore this email.\n\nInfinite, by Naman Soni (n1m21n)\nhttps://infinitedevices.org`;
export const unsubscribeLink = async (env, email, audience) => `${env.API_ORIGIN}/api/unsubscribe?t=${encodeURIComponent(await makeToken(env.SIGNING_SECRET, { email, audience, act: "unsub" }))}`;

async function setStatus(env, url, status, col, act) {
  const p = await readToken(env.SIGNING_SECRET, url.searchParams.get("t"));
  if (!p || (act && p.act !== act) || (!act && p.act)) return null;
  await env.DB.prepare(`UPDATE subscribers SET status = ?, ${col} = ? WHERE email = ? AND audience = ?`).bind(status, new Date().toISOString(), p.email, p.audience).run();
  return p;
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors(env) });
    if (url.pathname === "/api/subscribe" && request.method === "POST") return subscribe(request, env);
    if (url.pathname === "/api/confirm" && request.method === "GET")
      return (await setStatus(env, url, "confirmed", "confirmed_at")) ? page("You are on the list", "Thank you. You will get one email when something ships, and every email has an unsubscribe link.") : page("That link has expired", "Sign up again from the site and we will send a new one.");
    if (url.pathname === "/api/unsubscribe" && request.method === "GET")
      return (await setStatus(env, url, "unsubscribed", "unsubscribed_at", "unsub")) ? page("You are unsubscribed", "We will not email you again.") : page("That link is not valid", "Reply to any of our emails and we will remove you by hand.");
    return new Response("Not found", { status: 404 });
  },
};
