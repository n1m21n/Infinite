#!/usr/bin/env node
/* Website scale + art-overlap test.
   What:  (1) art vs text: moving art (WebGL canvases, the philosophy bubbles) must not sit under headlines, captions or controls;
          (2) device scale: pills, buttons and type stay inside the sizes website/scale.css sets for phone and desktop.
   Why:   the artists blob ran under its headline and bubbles, the philosophy figure under its caption, and phone buttons were too big.
   Usage: cd tests/website && npm install && node ui_scale.mjs [page.html ...]
   Limits (px): phone pill 32-38, phone button 36-42, desktop pill 36-44, desktop button 40-48; button label 12-15 on phones;
                text under 12 is flagged; a phone control wider than 70% of the screen is flagged unless it is a form field.
   Exit code: 0 clean, 1 problems. */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';
import { PNG } from 'pngjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../website');
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.woff2': 'font/woff2' };
const SIZES = [{ name: 'phone', width: 375, height: 812, mobile: true }, { name: 'desktop', width: 1280, height: 800, mobile: false }];
const DEFAULT_PAGES = ['index.html', 'library.html', 'philosophy.html', 'benchmarks.html', 'artists.html', 'musicians.html', 'vjs.html', 'creative-coders.html', 'developers.html', 'blog/index.html'];
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;
const problems = [];
const report = (pg, sz, kind, msg) => problems.push(`${pg} [${sz}] ${kind}: ${msg}`);
const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--enable-unsafe-swiftshader', '--use-gl=angle', '--use-angle=swiftshader'] });
const pages = process.argv.slice(2).length ? process.argv.slice(2) : DEFAULT_PAGES;

/* in page: text/control boxes that sit over a canvas */
const OVER_FN = () => {
  const out = [];
  const vis = (e) => { for (let n = e; n && n !== document.body; n = n.parentElement) { const c = getComputedStyle(n); if (c.display === 'none' || c.visibility === 'hidden' || +c.opacity < 0.05) return false; } return true; };
  document.querySelectorAll('canvas').forEach((cv, ci) => {
    const r = cv.getBoundingClientRect(); if (r.width < 80 || r.height < 80 || !vis(cv)) return;
    if (r.bottom < 0 || r.top > innerHeight) return;
    if (r.width >= innerWidth * 0.95 && r.height >= innerHeight * 0.9) return;   // a full-screen backdrop (home hero) is meant to sit behind the text
    cv.dataset.ci = ci;
    const host = cv.parentElement; const boxes = [];
    host.querySelectorAll('h1,h2,h3,p,a,button,summary,label,figcaption,span,output,input').forEach(e => {
      if (e.contains(cv) || !vis(e) || e.closest('.dock, .site-header, .visually-hidden, .sr-only')) return;
      const glass = /blur/.test(getComputedStyle(e).backdropFilter || getComputedStyle(e).webkitBackdropFilter || ''); if (glass) return;   // a glass label over art is deliberate
      const hasText = [...e.childNodes].some(n => n.nodeType === 3 && n.textContent.trim()) || /^(A|BUTTON|SUMMARY|INPUT)$/.test(e.tagName);
      if (!hasText) return;
      let b = e.getBoundingClientRect(); if (/^(P|H1|H2|H3|LABEL|SPAN|FIGCAPTION|OUTPUT)$/.test(e.tagName)) { const rg = document.createRange(); rg.selectNodeContents(e); b = rg.getBoundingClientRect(); } if (b.width < 8 || b.height < 8) return;
      const x0 = Math.max(b.left, r.left), y0 = Math.max(b.top, r.top), x1 = Math.min(b.right, r.right), y1 = Math.min(b.bottom, r.bottom);
      if (x1 - x0 < 6 || y1 - y0 < 6) return;
      boxes.push({ name: e.tagName.toLowerCase() + (e.className && typeof e.className === 'string' ? '.' + e.className.trim().split(/\s+/)[0] : '') + ' "' + (e.textContent || '').trim().slice(0, 18) + '"', x: x0, y: y0, w: x1 - x0, h: y1 - y0 });
    });
    out.push({ ci, rect: { sy: scrollY, sx: scrollX, x: r.left, y: r.top, w: r.width, h: r.height }, boxes });
  });
  return out;
};
const HIDE = `*{visibility:hidden!important} canvas{visibility:visible!important}`;
const lum = (d, i) => (0.2126 * d[i] + 0.7152 * d[i + 1] + 0.0722 * d[i + 2]) / 255;

async function canvasCheck(page, pg, sz) {
  const groups = await page.evaluate(OVER_FN);
  for (const g of groups) {
    if (!g.boxes.length) continue;
    const stops = sz.mobile ? [[0.5, 0.5], [0.2, 0.3], [0.8, 0.7]] : [[0.5, 0.5], [0.8, 0.4], [0.3, 0.6]];
    for (const [fx, fy] of stops) {
      await page.mouse.move(g.rect.x + g.rect.w * fx, g.rect.y + g.rect.h * fy); await page.mouse.down(); await page.mouse.up();
      await new Promise(r => setTimeout(r, 900));
      await page.addStyleTag({ content: HIDE });
      const png = PNG.sync.read(Buffer.from(await page.screenshot({ clip: { x: g.rect.x + g.rect.sx, y: g.rect.y + g.rect.sy, width: g.rect.w, height: g.rect.h } })));
      if (process.env.DEBUG) fs.writeFileSync(process.env.DEBUG + `/${pg.replace(/\W/g, '_')}-${sz.name}-${fx}.png`, PNG.sync.write(png));
      const sc = png.width / g.rect.w, d = png.data;
      const ls = []; for (let i = 0; i < d.length; i += 4 * 17) ls.push(lum(d, i)); ls.sort((a, b) => a - b); const med = ls[ls.length >> 1];
      for (const b of g.boxes) {
        let n = 0, ink = 0;
        for (let y = Math.floor((b.y - g.rect.y) * sc); y < Math.ceil((b.y + b.h - g.rect.y) * sc); y += 2) for (let x = Math.floor((b.x - g.rect.x) * sc); x < Math.ceil((b.x + b.w - g.rect.x) * sc); x += 2) {
          if (x < 0 || y < 0 || x >= png.width || y >= png.height) continue; n++; if (Math.abs(lum(d, (y * png.width + x) * 4) - med) > 0.22) ink++;
        }
        if (n && ink / n > 0.06) report(pg, sz.name, 'art-over-text', `${b.name} is ${(100 * ink / n).toFixed(0)}% covered by canvas art (pointer ${fx},${fy})`);
      }
      await page.evaluate(() => { [...document.querySelectorAll('style')].filter(s => s.textContent.startsWith('*{visibility:hidden')).forEach(s => s.remove()); });
    }
    await page.evaluate(() => { [...document.querySelectorAll('style')].filter(s => s.textContent.startsWith('*{visibility:hidden')).forEach(s => s.remove()); });
  }
}

/* DOM art (philosophy): bubbles against the caption and title of the scene on screen */
const BUBBLE_FN = () => {
  const op = (e) => { let o = 1; for (let n = e; n && n !== document.body; n = n.parentElement) o *= +getComputedStyle(n).opacity; return o; };
  const arts = [...document.querySelectorAll('.pb, .ph-lobe-a, .ph-lobe-b')].filter(e => op(e) > 0.3).map(e => ({ e, r: e.getBoundingClientRect() })).filter(a => a.r.width > 4);
  const texts = [...document.querySelectorAll('.ph-cap h2, .ph-cap p, .ph-title, .ph-end a, .ph-end button, .ph-nav button')].filter(e => op(e) > 0.5).map(e => ({ n: e.className || e.tagName, t: e.textContent.trim().slice(0, 16), r: e.getBoundingClientRect() })).filter(a => a.r.width > 4);
  const bad = [];
  for (const a of arts) for (const t of texts) { const r = a.r, q = t.r, m = 6; if (r.left + m < q.right && q.left + m < r.right && r.top + m < q.bottom && q.top + m < r.bottom) bad.push(`${t.n} "${t.t}" under a bubble`); }
  return [...new Set(bad)];
};

/* sizes */
const SCALE_FN = (phone) => {
  const out = [];
  const inDock = (e) => e.closest('.dock, .site-header');
  document.querySelectorAll('a[href], button, summary, [role=tab]').forEach(e => {
    const r = e.getBoundingClientRect(); if (r.width < 8 || r.height < 8 || inDock(e)) return;
    const cs = getComputedStyle(e); if (cs.display === 'none' || cs.visibility === 'hidden' || +cs.opacity < 0.05 || e.matches('.btn-dl') || e.closest('.pb, .ph-bubbles, .play-btn, .yt-play, [aria-hidden="true"], .dv-sec, .deck-card, .lib-card, .post-card, .ax-node, .ax-shot, .reel')) return;
    const rad = parseFloat(cs.borderTopLeftRadius); if (!(rad >= r.height / 2 - 1) || r.height > 52 || r.height < 24) return;   // taller ones are cards (download tiles), not controls   // pill-shaped controls only
    const label = (e.textContent || '').trim(); const iconOnly = !label || (r.width - r.height) < 6;
    const name = e.tagName.toLowerCase() + (e.className && typeof e.className === 'string' ? '.' + e.className.trim().split(/\s+/)[0] : '') + ' "' + label.slice(0, 14) + '"';
    const big = e.matches('.btn, .btn-primary, .ax-ghost, .mx-play, .signup-row .btn');
    out.push({ name, h: r.height, w: r.width, fs: parseFloat(cs.fontSize), big, iconOnly });
  });
  return out;
};

for (const pg of pages) {
  if (!fs.existsSync(path.join(ROOT, pg))) continue;
  for (const sz of SIZES) {
    const page = await browser.newPage();
    await page.setViewport({ width: sz.width, height: sz.height, isMobile: sz.mobile, hasTouch: false, deviceScaleFactor: 1 });
    await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]);
    await page.goto(base + pg, { waitUntil: 'load' }).catch(() => {});
    await new Promise(r => setTimeout(r, 900));
    const total = await page.evaluate(() => document.documentElement.scrollHeight);
    const seen = new Set();
    for (let y = 0; y <= total; y += Math.round(sz.height * 0.6)) {
      await page.evaluate((yy) => { document.documentElement.style.scrollBehavior = 'auto'; scrollTo(0, yy); }, y);
      await new Promise(r => setTimeout(r, 160));
      for (const m of await page.evaluate(BUBBLE_FN)) if (!seen.has(m)) { seen.add(m); report(pg, sz.name, 'art-over-text', m); }
      for (const it of await page.evaluate(SCALE_FN, sz.mobile)) {
        const key = it.name; if (seen.has(key)) continue; seen.add(key);
        const [pmin, pmax] = sz.mobile ? [32, 38] : [36, 44], [bmin, bmax] = sz.mobile ? [36, 42] : [40, 48];
        const [lo, hi] = it.iconOnly ? [pmin, pmax + 6] : it.big ? [bmin, bmax] : [pmin, pmax];
        if (it.h < lo - 0.5 || it.h > hi + 0.5) report(pg, sz.name, 'scale', `${it.name} is ${it.h.toFixed(0)}px tall (want ${lo}-${hi})`);
        if (sz.mobile && !it.iconOnly && (it.fs < 12 || it.fs > 15.5)) report(pg, sz.name, 'scale', `${it.name} label is ${it.fs}px (want 12-15)`);
        if (sz.mobile && it.w > sz.width * 0.7 && !/signup|email|btn-dl/i.test(it.name)) report(pg, sz.name, 'scale', `${it.name} is ${it.w.toFixed(0)}px wide on a ${sz.width}px screen`);
      }
      await canvasCheck(page, pg, sz);
    }
    await page.close();
  }
}
await browser.close(); server.close();
const uniq = [...new Set(problems)];
if (uniq.length) { console.log(uniq.join('\n')); console.log(`\n${uniq.length} problem(s)`); process.exit(1); }
console.log('ui scale: clean');
