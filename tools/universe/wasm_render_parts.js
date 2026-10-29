// Render labelled parts with the live generator (SampleStyles via the website wasm) and stream the audio to stdout.
//   node wasm_render_parts.js <gen dir> <first seed> <seeds per style> [parts, comma list of Part numbers]
// Per render: one header line {style, id, part, seed, bpm, frames, label} then frames*2 float32 (interleaved stereo, 48 kHz).
// Dots at rest, variation 0.5, 2 bars, card key/scale: the generator as a visitor first hears it.
const path = require('path'), fs = require('fs');
const out = b => { for (let o = 0; o < b.length;) o += fs.writeSync(1, b, o); };  // sync: process.exit drops queued pipe writes
const dir = path.resolve(process.argv[2] || path.join(__dirname, '../../website/gen'));
const first = parseInt(process.argv[3] || '9000', 10), seeds = parseInt(process.argv[4] || '2', 10);
const parts = (process.argv[5] || '1,2,3,4,5,6,7,8,9,10,11,12,13,14,15').split(',').map(Number);
const M0 = require(path.join(dir, 'infinite_gen_wasm.js'));
M0({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  if (M._gen_set_shape) M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 0);
  const n = M._gen_style_count();
  for (let style = 0; style < n; style++) {
    const id = M.UTF8ToString(M._gen_style_id(style));
    for (const part of parts)
      for (let k = 0; k < seeds; k++) {
        const seed = first + style * 1009 + part * 101 + k;
        const frames = M._gen_run(style, part, seed, 2, -1, -1);
        if (!frames) continue;
        const pcm = Buffer.from(new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), frames * 2).slice().buffer);
        out(Buffer.from(JSON.stringify({ style, id, part, seed, bpm: M._gen_bpm(), frames, label: M.UTF8ToString(M._gen_label()) }) + '\n'));
        out(pcm);
      }
  }
  process.exit(0);
});
