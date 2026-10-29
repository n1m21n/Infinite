// node part_patches.js <gen dir> <seeds> <parts csv>: one JSON row per style x part x seed with the patch text.
const path = require('path');
const dir = path.resolve(process.argv[2]); const seeds = +process.argv[3]; const parts = process.argv[4].split(',').map(Number);
require(path.join(dir, 'infinite_gen_wasm.js'))({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  if (M._gen_set_shape) M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 0);
  for (let style = 0; style < M._gen_style_count(); style++)
    for (const part of parts)
      for (let k = 0; k < seeds; k++) {
        const seed = 9000 + style * 101 + k;
        if (!M._gen_run(style, part, seed, 2, -1, -1)) continue;
        process.stdout.write(JSON.stringify({ style, part, seed, patch: M.UTF8ToString(M._gen_patch()) }) + '\n');
      }
  process.exit(0);
});
