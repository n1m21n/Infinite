// Infinite sign-up Worker: double opt-in email list on Resend, no database.
//   POST /api/subscribe   {email, audience, turnstile, company}  -> sends a confirmation email
//   GET  /api/confirm?t=  -> verifies the signed link, adds the contact to that audience's Resend list
// Unsubscribe is Resend's own link in every broadcast ({{{RESEND_UNSUBSCRIBE_URL}}}).
// Secrets (wrangler secret put): RESEND_API_KEY, SIGNING_SECRET, TURNSTILE_SECRET
// Vars (wrangler.toml): SITE_ORIGIN, API_ORIGIN, FROM, AUDIENCES (JSON: persona -> Resend audience id)

const TOKEN_TTL_MS = 48 * 3600 * 1000;
const PERSONAS = ["artists", "musicians", "vjs", "creative-coders", "developers", "general"];
const EMAIL = /^[^\s@]{1,64}@[^\s@]{1,190}\.[^\s@]{2,}$/;

const b64 = (buf) => btoa(String.fromCharCode(...new Uint8Array(buf))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
const unb64 = (s) => Uint8Array.from(atob(s.replace(/-/g, "+").replace(/_/g, "/")), (c) => c.charCodeAt(0));

async function hmac(secret, data) {
  const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign", "verify"]);
  return { key, sign: (d) => crypto.subtle.sign("HMAC", key, new TextEncoder().encode(d)) };
}

export async function makeToken(secret, payload) {
  const body = b64(new TextEncoder().encode(JSON.stringify(payload)));
  const h = await hmac(secret, body);
  return body + "." + b64(await h.sign(body));
}

export async function readToken(secret, token, now = Date.now()) {
  const [body, sig] = String(token || "").split(".");
  if (!body || !sig) return null;
  const h = await hmac(secret, body);
  let ok = false;
  try { ok = await crypto.subtle.verify("HMAC", h.key, unb64(sig), new TextEncoder().encode(body)); } catch { return null; }
  if (!ok) return null;
  const p = JSON.parse(new TextDecoder().decode(unb64(body)));
  return p.exp > now ? p : null;
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
  const t = await makeToken(env.SIGNING_SECRET, { email, audience, exp: Date.now() + TOKEN_TTL_MS });
  const link = `${env.API_ORIGIN}/api/confirm?t=${encodeURIComponent(t)}`;
  const r = await fetch("https://api.resend.com/emails", {
    method: "POST", headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
    body: JSON.stringify({ from: env.FROM, to: [email], subject: "Confirm your email for Infinite",
      text: `Confirm that you want occasional news about Infinite (a note when something ships, nothing else):\n\n${link}\n\nThe link works for 48 hours. If you did not ask for this, ignore this email and nothing is saved.\n\nInfinite, by Naman Soni (n1m21n)\nhttps://infinitedevices.org` }),
  });
  if (!r.ok) return json(env, 502, { ok: false, error: "We could not send the email. Please try again later." });
  return json(env, 200, { ok: true });                                      // same answer whether or not the address is already listed
}

async function confirm(url, env) {
  const p = await readToken(env.SIGNING_SECRET, url.searchParams.get("t"));
  if (!p) return page("That link has expired", "Sign up again from the site and we will send a new one.");
  const ids = JSON.parse(env.AUDIENCES || "{}");
  const id = ids[p.audience] || ids.general;
  if (!id) return page("Something is not set up yet", "Please try again later.");
  const r = await fetch(`https://api.resend.com/audiences/${id}/contacts`, {
    method: "POST", headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
    body: JSON.stringify({ email: p.email, unsubscribed: false }),
  });
  if (!r.ok) return page("Something went wrong", "Please try the link again in a minute.");
  return page("You are on the list", "Thank you. You will get one email when something ships, and every email has an unsubscribe link.");
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors(env) });
    if (url.pathname === "/api/subscribe" && request.method === "POST") return subscribe(request, env);
    if (url.pathname === "/api/confirm" && request.method === "GET") return confirm(url, env);
    return new Response("Not found", { status: 404 });
  },
};
