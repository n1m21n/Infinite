#!/usr/bin/env node
/* Screenshot one part of a page, exactly: the tool for the review loop (audit > screenshot > fix > screenshot).
   Usage: cd tests/website && node shoot.mjs <page.html> [--w 375|1280] [--h 812] [--mode midnight|mist|paper]
            [--at "css selector"] [--full] [--out file.png]
   --at   scrolls the first match into view and crops to it (plus 24px); without it, the viewport at the top of the page.
   --full the whole page (tall pages are capped at 12000px).
   Reveal animations are finished first and motion is reduced, so shots are stable and comparable run to run. */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer-core';
import { ROOT } from './pages.mjs';

const a = process.argv.slice(2); const opt = (k, d) => { const i = a.indexOf('--' + k); return i >= 0 ? a[i + 1] : d; };
const pg = a.find((x) => x.endsWith('.html') || x.endsWith('/')) || 'index.html';
const W = +opt('w', 1280), H = +opt('h', W < 700 ? 812 : 800), mode = opt('mode', ''), at = opt('at', ''), full = a.includes('--full');
const out = path.resolve(opt('out', `${pg.replace(/\W+/g, '_')}-${W}${mode ? '-' + mode : ''}.png`));
const CHROME = process.env.CHROME || process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.woff2': 'font/woff2', '.mp4': 'video/mp4' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--hide-scrollbars'] });
const page = await browser.newPage();
await page.setViewport({ width: W, height: H, isMobile: W < 700, hasTouch: W < 700, deviceScaleFactor: 2 });
await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]);
await page.goto(`http://127.0.0.1:${server.address().port}/${pg}`, { waitUntil: 'networkidle2', timeout: 30000 }).catch(() => {});
await page.evaluate((m) => { document.documentElement.style.scrollBehavior = 'auto'; if (m) document.documentElement.setAttribute('data-mode', m);
  document.querySelectorAll('.reveal, .kt-slab, .kt-draw, [data-count]').forEach((e) => e.classList.add('in', 'is-in', 'visible')); return document.fonts.ready; }, mode);
await new Promise((r) => setTimeout(r, 600));
if (at) {
  const el = await page.$(at); if (!el) { console.error('no match for ' + at); process.exit(1); }
  await el.evaluate((e) => e.scrollIntoView({ block: 'start' })); await new Promise((r) => setTimeout(r, 400));
  const b = await el.evaluate((e) => { const r = e.getBoundingClientRect(); return { x: r.left + scrollX, y: r.top + scrollY, width: r.width, height: r.height }; });
  await page.screenshot({ path: out, clip: { x: Math.max(0, b.x - 24), y: Math.max(0, b.y - 24), width: Math.min(W, b.width + 48), height: Math.min(4000, b.height + 48) } });
} else if (full) {
  await page.evaluate(async () => { for (let y = 0; y < document.documentElement.scrollHeight; y += 600) { window.scrollTo(0, y); await new Promise((r) => setTimeout(r, 60)); } window.scrollTo(0, 0); });
  const h = Math.min(12000, await page.evaluate(() => document.documentElement.scrollHeight));
  await page.setViewport({ width: W, height: h, isMobile: W < 700, hasTouch: W < 700, deviceScaleFactor: 1 }); await new Promise((r) => setTimeout(r, 500));
  await page.screenshot({ path: out });
} else await page.screenshot({ path: out });
console.log(out);
await browser.close(); server.close();
