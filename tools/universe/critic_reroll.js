// How often does Generate Samples redraw a take, and how much slower is a take? Compares two engine builds on the same cards.
//   node critic_reroll.js <gen dir> <first seed> <seeds per style> [shaped]
// Prints one JSON line: renders, redraws per attempt count, failed renders, ms per render. Parts 1-15 (every part the critic judges).
const path = require('path');
const dir = path.resolve(process.argv[2]), first = +process.argv[3], seeds = +process.argv[4], shaped = process.argv[5] === 'shaped';
const rand = s => () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
require(path.join(dir, 'infinite_gen_wasm.js'))({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 0);
  const attempts = [0, 0, 0, 0], byPart = {}; let failed = 0, n = 0; const t0 = Date.now();
  for (let style = 0; style < M._gen_style_count(); style++)
    for (let part = 1; part <= 15; part++)
      for (let k = 0; k < seeds; k++) {
        const seed = first + style * 1009 + part * 101 + k;
        if (shaped) { const r = rand(seed); M._gen_set_shape(2 * r() - 1, 2 * r() - 1, 2 * r() - 1, 2 * r() - 1, r(), 0, 0); }
        n++;
        if (!M._gen_run(style, part, seed, 2, -1, -1)) { failed++; continue; }
        const a = M._gen_attempt(); attempts[a]++;
        byPart[part] = byPart[part] || [0, 0]; byPart[part][0]++; byPart[part][1] += a > 0;
      }
  console.log(JSON.stringify({ dir: path.basename(dir), shaped, renders: n, failed, attempts, redrawn_share: +((n - failed - attempts[0]) / (n - failed)).toFixed(4),
    redrawn_by_part: Object.fromEntries(Object.entries(byPart).map(([p, v]) => [p, +(v[1] / v[0]).toFixed(3)])), ms_per_render: Math.round((Date.now() - t0) / n) }));
  process.exit(0);
});
