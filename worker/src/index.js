// Quota + licence gate in front of the sample generator.
// POST /generate  {recipe, key?}  ->  200 {ok, left, pro}  |  429 {error:'quota', limit, pro:false}
// Licence keys are checked against the Lemon Squeezy licence API (public endpoint, no API key needed).

const DAY = 86400;

function cors(env, extra) {
  return Object.assign({
    'access-control-allow-origin': env.ALLOW_ORIGIN || '*',
    'access-control-allow-headers': 'content-type',
    'access-control-allow-methods': 'POST, OPTIONS',
    'content-type': 'application/json'
  }, extra);
}
const reply = (env, status, body) => new Response(JSON.stringify(body), { status, headers: cors(env) });

async function isPro(env, key) {
  if (!key || typeof key !== 'string' || key.length > 100) return false;
  const cached = await env.QUOTA.get('lic:' + key);
  if (cached) return cached === '1';
  const r = await fetch('https://api.lemonsqueezy.com/v1/licenses/validate', {
    method: 'POST',
    headers: { accept: 'application/json', 'content-type': 'application/x-www-form-urlencoded' },
    body: 'license_key=' + encodeURIComponent(key)
  });
  const j = await r.json().catch(() => ({}));
  const ok = !!(j && j.valid);
  await env.QUOTA.put('lic:' + key, ok ? '1' : '0', { expirationTtl: ok ? 3600 : 300 });
  return ok;
}

export default {
  async fetch(req, env) {
    if (req.method === 'OPTIONS') return new Response(null, { headers: cors(env) });
    const url = new URL(req.url);
    if (url.pathname !== '/generate' || req.method !== 'POST') return reply(env, 404, { error: 'not found' });
    let body; try { body = await req.json(); } catch (e) { return reply(env, 400, { error: 'bad json' }); }

    const pro = await isPro(env, body.key);
    const limit = +(pro ? env.PRO_PER_DAY : env.FREE_PER_DAY);
    const who = pro ? 'k:' + body.key : 'ip:' + (req.headers.get('cf-connecting-ip') || 'unknown');
    const slot = 'q:' + who + ':' + new Date().toISOString().slice(0, 10);
    const used = +(await env.QUOTA.get(slot)) || 0;
    if (used >= limit) return reply(env, 429, { error: 'quota', limit, pro });
    await env.QUOTA.put(slot, String(used + 1), { expirationTtl: 2 * DAY });

    // TODO render: run the generator engine for body.recipe and return the wav / patch here.
    return reply(env, 200, { ok: true, pro, left: limit - used - 1 });
  }
};
