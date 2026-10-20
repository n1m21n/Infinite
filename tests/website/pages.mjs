// One page list for the browser audits (ui_audit, ui_scale, ui_type): every hand-built page plus one sample of each
// generated family (a blog post, a library page, an embed). Pass pages on the command line to audit others.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../website');
const TOP = ['index.html', 'library.html', 'philosophy.html', 'benchmarks.html', 'artists.html', 'musicians.html', 'vjs.html',
  'creative-coders.html', 'developers.html', 'extensions.html', 'artworks.html', 'blog/index.html', 'embeds/index.html'];
const first = (dir) => {
  const d = path.join(ROOT, dir);
  const f = fs.existsSync(d) ? fs.readdirSync(d).filter((x) => x.endsWith('.html') && x !== 'index.html'
    && !/noindex/.test(fs.readFileSync(path.join(d, x), 'utf8').slice(0, 4000))).sort()[0] : null;
  return f ? dir + '/' + f : null;
};
export const DEFAULT_PAGES = [...TOP, first('blog'), first('library'), first('embeds')].filter(Boolean);

export function pageList(argv) {
  const pages = argv.length ? argv : DEFAULT_PAGES;
  for (const p of pages) if (!fs.existsSync(path.join(ROOT, p))) console.error(`missing page: ${p}`);
  return pages.filter((p) => fs.existsSync(path.join(ROOT, p)));
}
