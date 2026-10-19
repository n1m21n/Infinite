// node worker/signup/test.mjs : exercises the Worker with a fake Resend (no network, no keys)
import worker, { makeToken, readToken } from "./index.js";
import assert from "node:assert/strict";
const env = { SITE_ORIGIN: "https://x.test", API_ORIGIN: "https://x.test", FROM: "a <a@x.test>", SIGNING_SECRET: "s3cret", RESEND_API_KEY: "k", AUDIENCES: '{"general":"g1","vjs":"v1"}' };
const calls = [];
globalThis.fetch = async (u, o) => { calls.push({ u, body: o && o.body && JSON.parse(o.body) }); return new Response("{}", { status: 200 }); };
const post = (b) => worker.fetch(new Request("https://x.test/api/subscribe", { method: "POST", body: JSON.stringify(b) }), env);

let r = await post({ email: "nope", audience: "vjs" }); assert.equal(r.status, 400);
r = await post({ email: "a@b.co", company: "bot" }); assert.equal(r.status, 200); assert.equal(calls.length, 0, "honeypot sends nothing");
r = await post({ email: "A@B.co", audience: "vjs" }); assert.equal(r.status, 200);
const link = new URL(calls[0].body.text.match(/https:\/\/\S+/)[0]);
assert.deepEqual(calls[0].body.to, ["a@b.co"]);
r = await worker.fetch(new Request(link), env); assert.match(await r.text(), /on the list/);
assert.equal(calls[1].u, "https://api.resend.com/audiences/v1/contacts"); assert.equal(calls[1].body.email, "a@b.co");
const t = link.searchParams.get("t");
assert.equal(await readToken("wrong", t), null, "wrong secret rejected");
assert.equal(await readToken("s3cret", t + "x"), null, "tampered rejected");
assert.equal(await readToken("s3cret", await makeToken("s3cret", { email: "a@b.co", audience: "vjs", exp: 1 })), null, "expired rejected");
console.log("ok");
