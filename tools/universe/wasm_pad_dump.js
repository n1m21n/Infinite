// The "pads frozen" gate (close doc section 1): the patch text and decimated pcm of 102 pad takes (34 styles x 3 seeds),
// written to a JSON file; compare two builds' dumps with `node wasm_pad_dump.js --cmp A.json B.json`.
//   node tools/universe/wasm_pad_dump.js build-wasm pads.json [part=2]
const path = require('path'), fs = require('fs');
if (process.argv[2] === '--cmp') {
  const A = JSON.parse(fs.readFileSync(process.argv[3])), B = JSON.parse(fs.readFileSync(process.argv[4]));
  let same = 0, diff = [];
  for (const k of Object.keys(A)) (JSON.stringify(A[k]) === JSON.stringify(B[k]) ? same++ : diff.push(k));
  console.log(`${same} / ${Object.keys(A).length} identical` + (diff.length ? `; differ: ${diff.slice(0, 10).join(' ')}` : ''));
  process.exit(diff.length ? 1 : 0);
}
const d = process.argv[2], out = process.argv[3], part = +(process.argv[4] || 2);
require(path.resolve(d, 'infinite_gen_wasm.js'))({ locateFile: f => path.resolve(d, f), print: () => {}, printErr: () => {} }).then(M => {
  M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 1);
  const r = {};
  for (let st = 0; st < M._gen_style_count(); st++) for (const seed of [9001, 9002, 9003]) {
    const fr = M._gen_run(st, part, seed, 2, -1, -1);
    const pcm = fr ? Array.from(new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), fr * 2).filter((_, i) => i % 997 == 0)).map(v => +v.toFixed(6)) : [];
    r[st + ':' + seed] = { patch: M.UTF8ToString(M._gen_patch()), pcm };
  }
  fs.writeFileSync(out, JSON.stringify(r)); process.exit(0);
});
