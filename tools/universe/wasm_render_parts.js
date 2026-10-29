// Render labelled parts with the live generator (SampleStyles via the website wasm) and stream the audio to stdout.
//   node wasm_render_parts.js <gen dir> <first seed> <seeds per style> [parts, comma list of Part numbers] [shaped]
// shaped: every render gets its own dots (tone, energy, space, mood uniform in -1..1) and variation (0..1), drawn from the seed.
// Per render: one header line {style, id, part, seed, bpm, frames, label} then frames*2 float32 (interleaved stereo, 48 kHz).
// Otherwise dots at rest, variation 0.5: the generator as a visitor first hears it. Always 2 bars, card key/scale.
const path = require('path'), fs = require('fs');
const out = b => { for (let o = 0; o < b.length;) o += fs.writeSync(1, b, o); };  // sync: process.exit drops queued pipe writes
const dir = path.resolve(process.argv[2] || path.join(__dirname, '../../website/gen'));
const first = parseInt(process.argv[3] || '9000', 10), seeds = parseInt(process.argv[4] || '2', 10);
const parts = (process.argv[5] || '1,2,3,4,5,6,7,8,9,10,11,12,13,14,15').split(',').map(Number), shaped = process.argv[6] === 'shaped';
const rand = s => () => { s = (s + 0x6D2B79F5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
const M0 = require(path.join(dir, 'infinite_gen_wasm.js'));
M0({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  if (M._gen_set_shape) M._gen_set_shape(0, 0, 0, 0, 0.5, 0, 0);
  const n = M._gen_style_count();
  for (let style = 0; style < n; style++) {
    const id = M.UTF8ToString(M._gen_style_id(style));
    for (const part of parts)
      for (let k = 0; k < seeds; k++) {
        const seed = first + style * 1009 + part * 101 + k;
        let shape = null;
        if (shaped) { const r = rand(seed); shape = { tone: 2 * r() - 1, energy: 2 * r() - 1, space: 2 * r() - 1, mood: 2 * r() - 1, variation: r() };
                      M._gen_set_shape(shape.tone, shape.energy, shape.space, shape.mood, shape.variation, 0, 0); }
        const frames = M._gen_run(style, part, seed, 2, -1, -1);
        if (!frames) continue;
        const pcm = Buffer.from(new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), frames * 2).slice().buffer);
        out(Buffer.from(JSON.stringify({ style, id, part, seed, bpm: M._gen_bpm(), frames, shape, label: M.UTF8ToString(M._gen_label()) }) + '\n'));
        out(pcm);
      }
  }
  process.exit(0);
});
