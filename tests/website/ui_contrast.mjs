#!/usr/bin/env node
/* Website contrast audit: text that is hard to read against what is really behind it.
   What:  loads every page in each mode (midnight, mist, paper) at a phone and a desktop size, and for every visible piece of
          text composites the backgrounds of all its ancestors (translucent bars, glass, tinted cards) to get the colour
          actually behind it, then measures WCAG contrast against the text colour (with opacity applied).
   Why:   a sticky bar fell back to a paper colour on a midnight page and its links went pale lavender on near-white. Token
          checks cannot see that: only the composited result can.
   Usage: cd tests/website && node ui_contrast.mjs [page.html ...]      (CHROME=/path/to/chrome to override)
   Rules: body text 4.5:1, large text (24px, or 18.7px bold) 3:1. Text over an image, video or canvas is skipped (unknown
          ground; ui_scale covers art over text). Disabled controls are skipped.
   Exit code: 0 clean, 1 problems found. */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import puppeteer from 'puppeteer-core';
import { ROOT, pageList } from './pages.mjs';

const CHROME = process.env.CHROME || process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.woff2': 'font/woff2', '.mp4': 'video/mp4' };
const SIZES = [{ name: 'phone', width: 375, height: 812, mobile: true }, { name: 'desktop', width: 1280, height: 800, mobile: false }];
const MODES = ['midnight', 'mist', 'paper'];

const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

/* runs in the page: every text element with too little contrast against its composited ground */
const PAGE_FN = () => {
  const cv = document.createElement('canvas'); cv.width = cv.height = 1;
  const cx = cv.getContext('2d', { willReadFrequently: true });
  const rgba = (c) => {   // any CSS colour to [r,g,b,a] via the canvas (handles color(srgb), oklch, color-mix results)
    if (!c || c === 'transparent') return [0, 0, 0, 0];
    cx.clearRect(0, 0, 1, 1); cx.fillStyle = '#000'; cx.fillStyle = c; cx.fillRect(0, 0, 1, 1);
    const d = cx.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255];
  };
  const over = (top, bot) => { const a = top[3] + bot[3] * (1 - top[3]); if (!a) return [0, 0, 0, 0];
    return [0, 1, 2].map((i) => (top[i] * top[3] + bot[i] * bot[3] * (1 - top[3])) / a).concat(a); };
  const lum = (c) => { const f = (v) => { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * f(c[0]) + 0.7152 * f(c[1]) + 0.0722 * f(c[2]); };
  const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05); };
  const out = []; const seen = new Set();
  for (const el of document.querySelectorAll('body *')) {
    if (['SCRIPT', 'STYLE', 'SVG', 'NOSCRIPT', 'TEMPLATE', 'CANVAS', 'VIDEO', 'IMG', 'OPTION'].includes(el.tagName.toUpperCase())) continue;
    let own = ''; el.childNodes.forEach((c) => { if (c.nodeType === 3) own += c.textContent; });
    own = own.replace(/\s+/g, ' ').trim(); if (!own || !/[A-Za-z0-9]/.test(own)) continue;
    const r = el.getBoundingClientRect(); if (r.width < 3 || r.height < 3) continue;
    if (el.checkVisibility && !el.checkVisibility({ opacityProperty: true, visibilityProperty: true, contentVisibilityAuto: true })) continue;
    if (el.closest('[aria-hidden="true"], .sr-only, .visually-hidden, [disabled], [aria-disabled="true"], .reveal:not(.in)')) continue;
    const cs = getComputedStyle(el);
    let fg = rgba(cs.color), op = 1, unknown = false, layers = [];
    for (let a = el; a; a = a.parentElement) {
      const s = getComputedStyle(a); op *= +s.opacity;
      if (s.backgroundImage !== 'none' && !/^linear-gradient\(|^radial-gradient\(/.test(s.backgroundImage)) unknown = true;
      if (a !== el && a.querySelector(':scope > canvas, :scope > video, :scope > img.bg, :scope > .pfield-canvas')) {
        const m = a.querySelector(':scope > canvas, :scope > video, :scope > .pfield-canvas'); if (m) { const mr = m.getBoundingClientRect();
          if (mr.left <= r.left + r.width / 2 && mr.right >= r.left + r.width / 2 && mr.top <= r.top + r.height / 2 && mr.bottom >= r.top + r.height / 2 && getComputedStyle(m).opacity > 0.3 && !m.classList.contains('pfield-canvas')) unknown = true; }
      }
      if (/gradient\(/.test(s.backgroundImage)) {   // a gradient fill: average its stops (good enough for a button or a scrim)
        const stops = (s.backgroundImage.match(/(rgba?|color)\([^()]*(\([^()]*\)[^()]*)*\)/g) || []).map(rgba);
        if (stops.length) { const g = [0, 1, 2, 3].map((i) => stops.reduce((t, c) => t + c[i], 0) / stops.length); layers.push(g); if (g[3] >= 0.999) break; }
      }
      const bg = rgba(s.backgroundColor); if (bg[3] > 0) layers.push(bg);
      if (bg[3] >= 0.999) break;
    }
    if (unknown || op < 0.05) continue;
    let ground = [255, 255, 255, 1];   // the canvas under html
    for (let i = layers.length - 1; i >= 0; i--) ground = over(layers[i], ground);
    fg = [fg[0], fg[1], fg[2], fg[3] * op];
    const text = over(fg, ground);
    const k = ratio(text, ground);
    const fs = parseFloat(cs.fontSize), bold = +cs.fontWeight >= 600;
    const need = fs >= 24 || (fs >= 18.66 && bold) ? 3 : 4.5;
    if (k + 0.05 < need) {
      const key = (el.className || el.tagName) + '|' + own.slice(0, 24);
      if (seen.has(key)) continue; seen.add(key);
      const hex = (c) => '#' + c.slice(0, 3).map((v) => Math.round(v).toString(16).padStart(2, '0')).join('');
      out.push(`${el.tagName.toLowerCase()}${el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/)[0] : ''} "${own.slice(0, 30)}" ${k.toFixed(2)}:1 (needs ${need}) text ${hex(text)} on ${hex(ground)}`);
    }
  }
  return out;
};

const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--hide-scrollbars'] });
let problems = 0;
for (const p of pageList(process.argv.slice(2))) {
  for (const sz of SIZES) {
    const page = await browser.newPage();
    await page.setViewport({ width: sz.width, height: sz.height, isMobile: sz.mobile, hasTouch: sz.mobile });
    await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]);
    try { await page.goto(base + p, { waitUntil: 'networkidle2', timeout: 30000 }); } catch { /* slow third-party embeds */ }
    await page.evaluate(() => document.querySelectorAll('.reveal').forEach((e) => e.classList.add('in')));
    for (const mode of MODES) {
      await page.evaluate((m) => document.documentElement.setAttribute('data-mode', m), mode);
      await new Promise((r) => setTimeout(r, 120));
      const bad = await page.evaluate(PAGE_FN);
      for (const b of bad) { console.log(`${p} [${sz.name} ${mode}] ${b}`); problems++; }
    }
    await page.close();
  }
}
await browser.close(); server.close();
console.log(problems ? `\n${problems} problem(s)` : 'contrast audit: clean');
process.exit(problems ? 1 : 0);
