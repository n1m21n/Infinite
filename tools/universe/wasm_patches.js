// Dump the live generator's patches (SampleStyles via the website wasm) as JSONL, for note-side tests.
//   node wasm_patches.js <gen dir> [seeds per style, default 10] > patches.jsonl
// One row per style x seed, full mix (part 0), 2 bars, card key/scale, no dots: {style, id, seed, bpm, patch}.
const path = require('path');
const dir = path.resolve(process.argv[2] || path.join(__dirname, '../../website/gen'));
const seeds = parseInt(process.argv[3] || '10', 10);
const M0 = require(path.join(dir, 'infinite_gen_wasm.js'));
M0({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  if (M._gen_set_shape) M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 0);
  const n = M._gen_style_count();
  for (let style = 0; style < n; style++) {
    const id = M.UTF8ToString(M._gen_style_id(style));
    for (let k = 0; k < seeds; k++) {
      const seed = 5000 + style * 101 + k;
      if (!M._gen_run(style, 0, seed, 2, -1, -1)) continue;
      process.stdout.write(JSON.stringify({ style, id, seed, bpm: M._gen_bpm(), patch: M.UTF8ToString(M._gen_patch()) }) + '\n');
    }
  }
  process.exit(0);
});
