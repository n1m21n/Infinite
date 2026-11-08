#!/usr/bin/env node
// Launch day: email every pre-order buyer the download link (docs/plans/vst-au/README.md 3.8).
//   node tools/license/notify_preorders.mjs          dry run: how many live buyers are still waiting
//   node tools/license/notify_preorders.mjs --send   send, 40 per request, until none are left
// Needs ADMIN_TOKEN (env or ~/.infinite-admin.env), the Worker deployed with RELEASED = "1".
// Safe to re-run: each key is marked download_emailed_at and never mailed twice.
import { readFileSync } from "node:fs";
import { homedir } from "node:os";

const send = process.argv.includes("--send");
const base = process.env.LICENSE_API || "https://infinitedevices.org";
let token = process.env.ADMIN_TOKEN;
if (!token) {
  try { token = /ADMIN_TOKEN=(\S+)/.exec(readFileSync(`${homedir()}/.infinite-admin.env`, "utf8"))?.[1]; } catch {}
}
if (!token) { console.error("ADMIN_TOKEN missing (env or ~/.infinite-admin.env)"); process.exit(2); }

const call = async (body) => {
  const r = await fetch(`${base}/api/license/notify-preorders`, {
    method: "POST", headers: { Authorization: `Bearer ${token}`, "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const j = await r.json().catch(() => ({}));
  if (!r.ok) { console.error(`HTTP ${r.status}`, j.error || ""); process.exit(1); }
  return j;
};

if (!send) {
  const j = await call({});
  console.log(`waiting: ${j.pending}   released: ${j.released}   (dry run; --send to mail)`);
  process.exit(0);
}
let total = 0;
for (;;) {
  const j = await call({ send: true });
  for (const id of j.sent) console.log("sent", id);
  for (const id of j.failed) console.log("FAILED", id);
  total += j.sent.length;
  if (j.left === 0) break;
  if (j.sent.length === 0) { console.error(`${j.left} left but none sent; stopping`); process.exit(1); }
}
console.log(`done: ${total} sent`);
