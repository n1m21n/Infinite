#!/usr/bin/env node
/* Website typography, spacing, size and motion audit with screenshots.
   What:  loads each page at phone and desktop size against a static server of website/, saves viewport screenshots down the page, and measures text and layout.
   Usage: cd tests/website && node ui_type.mjs [outdir] [page.html ...]     (CHROME=/path/to/chrome to override)
   Checks per page and size:
     font     visible text under 12px, or body copy under 14px on phones
     leading  line-height under 1.15x for multi-line text, or over 2x
     measure  paragraphs wider than 80 characters per line
     clip     text cut off by its own box (scrollWidth > clientWidth with overflow hidden and no ellipsis)
     edge     visible text closer than 8px to the screen edge
     gap      sibling cards or controls whose gap is not a multiple of 4px (reported as a count per page, not a failure)
     motion   infinite animations running on text, animation durations over 6s, and whether prefers-reduced-motion stops them
   Exit code: 0 clean, 1 problems found. */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import puppeteer from 'puppeteer-core';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../website');
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.webp': 'image/webp', '.woff2': 'font/woff2' };
const args = process.argv.slice(2);
const OUT = args[0] && !args[0].endsWith('.html') ? path.resolve(args.shift()) : path.resolve('ui_type_shots');
const SIZES = [{ name: 'phone', width: 375, height: 812, mobile: true }, { name: 'desktop', width: 1280, height: 800, mobile: false }];
const PAGES = args.length ? args : ['index.html', 'library.html', 'philosophy.html', 'artists.html', 'musicians.html', 'vjs.html', 'creative-coders.html', 'developers.html'];

const server = http.createServer((req, res) => {
  let p = decodeURIComponent(req.url.split('?')[0]); if (p.endsWith('/')) p += 'index.html';
  const f = path.join(ROOT, p);
  if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': MIME[path.extname(f)] || 'application/octet-stream' }); fs.createReadStream(f).pipe(res);
});
await new Promise(r => server.listen(0, r));
const base = 'http://localhost:' + server.address().port + '/';
fs.mkdirSync(OUT, { recursive: true });

const MEASURE_FN = (phone) => {
  const out = { font: [], leading: [], measure: [], clip: [], edge: [], gaps: 0, motion: [] };
  const vis = (e, cs, r) => cs.display !== 'none' && cs.visibility !== 'hidden' && +cs.opacity > 0.1 && r.width > 4 && r.height > 4;
  const inScroll = (e) => { for (let a = e.parentElement; a && a !== document.body; a = a.parentElement) { const o = getComputedStyle(a).overflowX; if ((o === 'auto' || o === 'scroll') && a.scrollWidth > a.clientWidth + 2) return true; } return false; };
  const skip = (e) => inScroll(e) || e.closest('.visually-hidden, canvas, svg, [aria-hidden="true"], .ax-stage, .vstage, .dock, script, style, .sr-only');
  const seen = new Set();
  const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let n; (n = walker.nextNode());) {
    const t = n.textContent.trim(); if (t.length < 2) continue;
    const e = n.parentElement; if (!e || seen.has(e) || skip(e)) continue; seen.add(e);
    const cs = getComputedStyle(e), rg = document.createRange(); rg.selectNodeContents(n); const r = rg.getBoundingClientRect();
    if (!vis(e, cs, r)) continue;
    const fs = parseFloat(cs.fontSize), lh = cs.lineHeight === 'normal' ? 1.2 : parseFloat(cs.lineHeight) / fs;
    const label = e.tagName.toLowerCase() + (typeof e.className === 'string' && e.className ? '.' + e.className.trim().split(/\s+/)[0] : '') + ' "' + t.slice(0, 22) + '"';
    if (fs < 12) out.font.push(`${label} is ${fs.toFixed(1)}px`);
    else if (phone && fs < 14 && t.length > 60) out.font.push(`${label} body copy is ${fs.toFixed(1)}px on a phone`);
    const lines = Math.round(r.height / (fs * Math.max(lh, 1.15)));
    if (lines >= 2 && !/^(hero-word|hand)$/.test(e.className.toString().split(' ')[0]) && (lh < 1.15 || lh > 2)) out.leading.push(`${label} line-height ${lh.toFixed(2)}`);
    if (lines >= 2 && e.tagName === 'P') { const cpl = t.length / lines; if (cpl > 80) out.measure.push(`${label} ~${cpl.toFixed(0)} characters per line`); }
    if ((r.left >= 0 && r.left < 8) || (r.right <= innerWidth && r.right > innerWidth - 8) || (r.left < 0 && r.right > 0 && r.right < innerWidth)) { if (!e.closest('.dv-toc, .reel, .ax-reel, .strip, .ax-strip, [class*="scroll"], nav')) out.edge.push(`${label} at ${r.left.toFixed(0)}..${r.right.toFixed(0)}`); }
    if ((cs.overflow === 'hidden' || cs.overflowX === 'hidden') && cs.textOverflow !== 'ellipsis' && e.scrollWidth > e.clientWidth + 2 && e.clientWidth > 0 && cs.whiteSpace === 'nowrap') out.clip.push(label);
  }
  document.querySelectorAll('.container > *, main section > *').forEach(e => {
    const n = e.nextElementSibling; if (!n) return; const a = e.getBoundingClientRect(), b = n.getBoundingClientRect(); const g = b.top - a.bottom;
    if (g > 0 && g < 160 && Math.abs(g % 4) > 0.6 && Math.abs(g % 4) < 3.4) out.gaps++;
  });
  document.getAnimations().forEach(a => {
    const t = a.effect && a.effect.target, tm = a.effect && a.effect.getComputedTiming(); if (!t || !tm) return;
    if (tm.iterations === Infinity && t.textContent && t.textContent.trim().length > 1 && !t.querySelector('*') && !t.closest('.marquee, .ticker')) out.motion.push(`infinite animation on text <${t.tagName.toLowerCase()}> "${t.textContent.trim().slice(0, 18)}"`);
    if (typeof tm.duration === 'number' && tm.duration > 6000 && tm.iterations !== Infinity) out.motion.push(`animation ${(tm.duration / 1000).toFixed(1)}s on <${t.tagName.toLowerCase()}>`);
  });
  out.running = document.getAnimations().filter(a => a.playState === 'running').length;
  return out;
};

const browser = await puppeteer.launch({ executablePath: CHROME, headless: 'new', args: ['--no-sandbox', '--use-gl=swiftshader', '--enable-unsafe-swiftshader'] });
let problems = 0; const report = (m) => { problems++; console.log('  ✗ ' + m); };
for (const pg of PAGES) for (const sz of SIZES) {
  const page = await browser.newPage();
  await page.setViewport({ width: sz.width, height: sz.height, isMobile: sz.mobile, hasTouch: sz.mobile, deviceScaleFactor: 1 });
  await page.goto(base + pg, { waitUntil: 'networkidle2', timeout: 30000 }).catch(() => {});
  await page.evaluate(() => { document.documentElement.style.scrollBehavior = 'auto'; return document.fonts.ready; });
  await new Promise(r => setTimeout(r, 900));
  const H = await page.evaluate(() => document.documentElement.scrollHeight);
  const stops = Math.min(6, Math.max(1, Math.ceil(H / sz.height)));
  const tag = pg.replace(/\W+/g, '_').replace(/_html$/, '');
  console.log(`${pg} [${sz.name}] ${H}px tall`);
  const agg = { font: new Set(), leading: new Set(), measure: new Set(), clip: new Set(), edge: new Set(), motion: new Set(), gaps: 0 };
  for (let i = 0; i < stops; i++) {
    const y = Math.round(i * Math.max(0, H - sz.height) / Math.max(1, stops - 1));
    await page.evaluate(y => window.scrollTo(0, y), y); await new Promise(r => setTimeout(r, 450));
    await page.screenshot({ path: path.join(OUT, `${tag}-${sz.name}-${i + 1}.png`) });
    const m = await page.evaluate(MEASURE_FN, sz.mobile);
    for (const k of ['font', 'leading', 'measure', 'clip', 'edge', 'motion']) m[k].forEach(x => agg[k].add(x));
    agg.gaps = Math.max(agg.gaps, m.gaps);
  }
  for (const k of ['font', 'leading', 'measure', 'clip', 'edge', 'motion']) for (const x of agg[k]) report(`${k}: ${x}`);
  if (agg.gaps) console.log(`  · ${agg.gaps} section gaps off the 4px grid (info)`);
  await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]); await page.reload({ waitUntil: 'networkidle2' }).catch(() => {}); await new Promise(r => setTimeout(r, 800));
  const run = await page.evaluate(() => document.getAnimations().filter(a => a.playState === 'running' && a.effect && a.effect.getComputedTiming().iterations === Infinity && !(a.effect.target && a.effect.target.closest('canvas'))).length);
  if (run > 3) report(`motion: ${run} infinite animations still running under prefers-reduced-motion`);
  await page.close();
}
await browser.close(); server.close();
console.log(problems ? `\n${problems} problem(s). Screenshots in ${OUT}` : `\nall clean. Screenshots in ${OUT}`);
process.exit(problems ? 1 : 0);
