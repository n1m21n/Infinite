#!/usr/bin/env node
/* Website UI audit: overlapping controls, controls hidden under the dock, dropdowns that overflow, dead clicks, horizontal overflow.
   What:  loads every page at a phone and a desktop size in headless Chrome, scrolls through it, and reports problems.
   Why:   these were found by eye on a real phone (controls under the sticky dock, a dropdown cut off, icons that did nothing).
          This makes them fail a check instead.
   Usage: cd tests/website && npm install && node ui_audit.mjs [page.html ...]      (CHROME=/path/to/chrome to override)
   Checks per page and size:
     overflow   the page scrolls sideways
     covered    a visible control whose centre is hit by something else (and is not a card in a deck)
     overlap    two visible controls that overlap by more than 4 px on each side
     fixed      a fixed or sticky control that intersects the dock
     menu       an opened dropdown that leaves the viewport or covers the control that opened it
     dead       a button or link with no href that changes nothing when clicked
   Exit code: 0 clean, 1 problems found. */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../website');
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.woff2': 'font/woff2', '.mp4': 'video/mp4', '.zip': 'application/zip' };
const SIZES = [{ name: 'phone', width: 375, height: 812, mobile: true }, { name: 'desktop', width: 1280, height: 800, mobile: false }];
const DEFAULT_PAGES = ['index.html', 'library.html', 'philosophy.html', 'benchmarks.html', 'artists.html', 'musicians.html', 'vjs.html', 'creative-coders.html', 'developers.html', 'blog/index.html', 'embeds.html'];

const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

/* runs in the page: returns the list of visible controls with their boxes */
const PAGE_FN = () => {
  const SEL = 'a[href], button, input, select, textarea, summary, [role=button], [role=tab], [role=option]';
  const vis = (el) => {
    const r = el.getBoundingClientRect(); if (r.width < 2 || r.height < 2) return null;
    const cs = getComputedStyle(el); if (cs.visibility === 'hidden' || cs.display === 'none' || +cs.opacity < 0.05 || cs.pointerEvents === 'none') return null;
    for (let n = el.parentElement; n; n = n.parentElement) { const c = getComputedStyle(n); if (c.display === 'none' || c.visibility === 'hidden' || n.hidden || (+c.opacity < 0.05)) return null; }
    return r;
  };
  const skip = (el) => el.closest('.deck-card, .post-card, .lib-card, .ax-nodes, .ax-reel, .reel, .bench-row, .ph-bubbles, .pb, .sr-only, .skip-link, [aria-hidden="true"]') || el.classList.contains('skip-link') || el.matches('input[type=range]');
  return [...document.querySelectorAll(SEL)].filter(e => !skip(e)).map(e => { const r = vis(e); if (!r) return null;
    const cs = getComputedStyle(e); let fixed = false; for (let n = e; n && n !== document.body; n = n.parentElement) { const p = getComputedStyle(n).position; if (p === 'fixed' || p === 'sticky') { fixed = true; break; } }
    return { id: e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + (e.className && typeof e.className === 'string' ? '.' + e.className.trim().split(/\s+/).slice(0, 2).join('.') : '') + ' "' + (e.getAttribute('aria-label') || e.textContent.trim()).slice(0, 24) + '"',
      x: r.left, y: r.top, w: r.width, h: r.height, fixed, inline: cs.display === 'inline', inDock: !!e.closest('.dock'), inHeader: !!e.closest('.site-header') };
  }).filter(Boolean);
};

const problems = [];
const report = (page, size, kind, msg) => problems.push(`${page} [${size}] ${kind}: ${msg}`);
const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--enable-unsafe-swiftshader'] });
const pages = process.argv.slice(2).length ? process.argv.slice(2) : DEFAULT_PAGES;

for (const pg of pages) {
  if (!fs.existsSync(path.join(ROOT, pg))) continue;
  for (const sz of SIZES) {
    const page = await browser.newPage();
    await page.setViewport({ width: sz.width, height: sz.height, isMobile: sz.mobile, hasTouch: sz.mobile, deviceScaleFactor: 2 });
    await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]);
    await page.goto(base + pg, { waitUntil: 'load' }).catch(() => {});
    await new Promise(r => setTimeout(r, 700));
    const tag = `${pg}`;
    const total = await page.evaluate(() => document.documentElement.scrollHeight);
    if (await page.evaluate(() => document.documentElement.scrollWidth > innerWidth + 1)) report(tag, sz.name, 'overflow', 'page scrolls sideways');

    /* scroll through, checking controls at every stop */
    const seen = new Set();
    for (let y = 0; y <= total; y += Math.round(sz.height * 0.7)) {
      await page.evaluate((yy) => { document.documentElement.style.scrollBehavior = 'auto'; scrollTo(0, yy); }, y);
      await new Promise(r => setTimeout(r, 120));
      const items = await page.evaluate(PAGE_FN);
      const dock = await page.evaluate(() => { const d = document.querySelector('.dock-bar'); if (!d) return null; const r = d.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });
      const head = await page.evaluate(() => { const d = document.querySelector('.site-header'); if (!d) return null; const r = d.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });
      const hit = (a, b, m = 4) => a.x + a.w - m > b.x && b.x + b.w - m > a.x && a.y + a.h - m > b.y && b.y + b.h - m > a.y;
      for (const it of items) {
        if (it.fixed && !it.inDock && !it.inHeader) {
          if (dock && hit(it, dock, 2)) report(tag, sz.name, 'fixed', `${it.id} sits under the dock`);
        }
      }
      for (let i = 0; i < items.length; i++) for (let j = i + 1; j < items.length; j++) {
        const a = items[i], b = items[j]; if (a.inDock || b.inDock || a.inHeader !== b.inHeader || (a.inline && b.inline)) continue;   // content scrolling under the sticky header is not an overlap
        if (hit(a, b)) { const k = a.id + '|' + b.id; if (!seen.has(k)) { seen.add(k); report(tag, sz.name, 'overlap', `${a.id} @${Math.round(a.x)},${Math.round(a.y)} overlaps ${b.id} @${Math.round(b.x)},${Math.round(b.y)}`); } }
      }
      /* covered: the thing at a control's centre must be the control or inside it, unless it is under the sticky dock/header (scrolls away) */
      const covered = await page.evaluate(() => {
        const out = []; const SEL = 'a[href], button, summary, [role=button], [role=tab]';
        for (const e of document.querySelectorAll(SEL)) {
          if (e.closest('.deck-card, .post-card, .lib-card, .dock, .site-header, .pb, [aria-hidden="true"], .sr-only')) continue;
          const r = e.getBoundingClientRect(); if (r.width < 2 || r.height < 2 || r.bottom < 0 || r.top > innerHeight) continue;
          const cs = getComputedStyle(e); if (cs.visibility === 'hidden' || cs.pointerEvents === 'none' || +cs.opacity < 0.05) continue;
          const cx = r.left + r.width / 2, cy = r.top + r.height / 2; if (cy < 0 || cy > innerHeight || cx < 0 || cx > innerWidth) continue;
          const top = document.elementFromPoint(cx, cy); if (!top) continue;
          if (e.contains(top) || top.contains(e)) continue;
          if (top.closest('.dock, .site-header')) continue;
          out.push((e.id || e.className || e.tagName) + ' covered by ' + (top.id || top.className || top.tagName));
        }
        return out;
      });
      for (const c of covered) { const k = 'c|' + c; if (!seen.has(k)) { seen.add(k); report(tag, sz.name, 'covered', c); } }
    }

    /* look-alikes: a pill, chip or bubble with an icon that is not a control (nothing to click, but it looks like it) */
    const fake = await page.evaluate(() => [...document.querySelectorAll('[class*="pill"], [class*="chip"], [class*="bubble"], [class*="-bub"]')].filter(e => {
      if (e.closest('a, button, summary, [role=button], [role=tab], [role=option], [aria-hidden="true"], .deck-card, .post-card, .lib-card')) return false;
      if (e.querySelector('a, button, input, select, summary')) return false;
      const r = e.getBoundingClientRect(); if (r.width < 24 || r.height < 24 || getComputedStyle(e).display === 'none') return false;
      return !!e.querySelector('svg') && parseFloat(getComputedStyle(e).borderTopLeftRadius) >= 12 && /pill|chip|bubble/.test(e.className) && !/hud-pill|lib-hud|vstage|hud-info/.test(e.className) && !/^pill( |$)/.test(e.className)   /* .pill on benchmarks is a read-only score badge */;
    }).map(e => e.className.toString().slice(0, 40)));
    for (const f of fake) report(pg, sz.name, 'dead', `non-interactive pill with an icon (looks clickable): .${f}`);

    /* the end of the page must clear the dock: the last visible controls are not under it */
    await page.evaluate(() => scrollTo(0, document.documentElement.scrollHeight));
    await new Promise(r => setTimeout(r, 150));
    const endBad = await page.evaluate(() => {
      const d = document.querySelector('.dock-bar'); if (!d) return [];
      const dr = d.getBoundingClientRect(), out = [];
      for (const e of document.querySelectorAll('footer a[href], footer button')) {
        const r = e.getBoundingClientRect(); if (r.width < 2) continue;
        if (r.bottom > dr.top + 2 && r.top < dr.bottom - 2 && r.right > dr.left && r.left < dr.right) out.push(e.textContent.trim().slice(0, 20));
      }
      return out;
    });
    for (const b of endBad) report(tag, sz.name, 'covered', `footer control "${b}" is under the dock at the end of the page`);

    /* dropdowns: open each, then check it stays on screen and does not cover its own button */
    await page.evaluate(() => scrollTo(0, 0));
    const triggers = await page.$$('[aria-haspopup], .dock-stop[data-group], [aria-expanded="false"]:not(.menu-btn)');
    for (const t of triggers) {
      const ok = await t.evaluate((el) => { const r = el.getBoundingClientRect(); const cs = getComputedStyle(el); return r.width > 2 && cs.display !== 'none' && cs.visibility !== 'hidden' && !el.closest('[hidden], .nav-links'); });
      if (!ok) continue;
      try { await t.evaluate(el => el.scrollIntoView({ block: 'center' })); await t.click({ delay: 20 }); } catch (e) { continue; }
      await new Promise(r => setTimeout(r, 450));
      const res = await t.evaluate((el) => {
        const id = el.getAttribute('aria-controls');
        let menu = id ? document.getElementById(id) : null;
        if (!menu) menu = el.parentElement && el.parentElement.querySelector('[role=listbox], .lib-menu, .blog-menu, .nav-menu, .dock-sheet');
        if (!menu || menu.hidden) return null;
        const m = menu.getBoundingClientRect(), b = el.getBoundingClientRect();
        if (m.width < 2) return null;
        const label = (el.getAttribute('aria-label') || el.textContent.trim()).slice(0, 20);
        const off = m.left < -1 || m.right > innerWidth + 1 || m.bottom > innerHeight + 1 && m.top > 0 && m.height < innerHeight && m.top + m.height > innerHeight + 1;
        const cover = m.left < b.right - 4 && m.right > b.left + 4 && m.top < b.bottom - 4 && m.bottom > b.top + 4;
        return { label, off, cover, l: Math.round(m.left), r: Math.round(m.right), t: Math.round(m.top), bt: Math.round(m.bottom), vw: innerWidth, vh: innerHeight };
      });
      if (res && res.off) report(pg, sz.name, 'menu', `dropdown of "${res.label}" leaves the screen (l=${res.l} r=${res.r} t=${res.t} b=${res.bt} in ${res.vw}x${res.vh})`);
      if (res && res.cover) report(pg, sz.name, 'menu', `dropdown of "${res.label}" covers its own button`);
      await page.keyboard.press('Escape'); await page.mouse.click(2, sz.height / 2).catch(() => {});
      await new Promise(r => setTimeout(r, 200));
    }

    /* dead clicks: a button that changes nothing (no DOM change, no navigation, no download, no focus move to a field) */
    await page.reload({ waitUntil: 'load' }).catch(() => {}); await new Promise(r => setTimeout(r, 500));
    const btns = await page.$$('button:not([disabled]):not([aria-haspopup]):not([aria-controls]), [role=button]:not([aria-disabled=true]), a[href="#"], a:not([href])');
    let n = 0;
    for (const b of btns) {
      if (n++ > 40) break;
      const info = await b.evaluate((el) => {
        if (el.closest('.dock, .deck-card, .post-card, .lib-card, [aria-hidden="true"], .sr-only, .reel-ctl, form') || el.hidden) return null;
        const r = el.getBoundingClientRect(), cs = getComputedStyle(el);
        if (r.width < 2 || r.height < 2 || cs.visibility === 'hidden' || cs.display === 'none' || cs.pointerEvents === 'none') return null;
        for (let p = el.parentElement; p; p = p.parentElement) if (p.hidden || getComputedStyle(p).display === 'none') return null;
        if (el.getAttribute('aria-disabled') === 'true' || el.matches(':disabled')) return null;
        return (el.id || el.className || el.tagName) + ' "' + (el.getAttribute('aria-label') || el.textContent.trim()).slice(0, 24) + '"';
      });
      if (!info) continue;
      const changed = await b.evaluate(async (el) => {
        el.scrollIntoView({ block: 'center' });
        let hits = 0; const mo = new MutationObserver((l) => { hits += l.length; }); mo.observe(document.documentElement, { attributes: true, childList: true, subtree: true, characterData: true });
        const url = location.href, y = scrollY, f = document.activeElement; let dl = 0; const onw = () => { dl++; }; addEventListener('beforeunload', onw);
        el.click(); await new Promise(r => setTimeout(r, 450)); mo.disconnect(); removeEventListener('beforeunload', onw);
        return hits > 0 || dl > 0 || location.href !== url || Math.abs(scrollY - y) > 2 || document.activeElement !== f;
      }).catch(() => true);
      if (!changed) report(pg, sz.name, 'dead', `${info} does nothing when clicked`);
    }
    await page.close();
  }
}
await browser.close(); server.close();
if (problems.length) { console.log(problems.join('\n')); console.log(`\n${problems.length} problem(s)`); process.exit(1); }
console.log('ui audit: clean');
