// Render whole mixes with the live generator (website wasm) at a given mood and energy, one WAV per job.
//   node wasm_render_mood.js <gen dir> <jobs.json>
// jobs.json: [{style, part, seed, bars, mood, energy, out}] (part: SampleStyles::Part, 0 = the whole mix), mood / energy in -1..1 (the node's own dots); tone and space at rest, variation 0.5.
// Prints one JSON line per job: {out, frames, bpm, label}. WAV: 32-bit float, stereo, 48 kHz.
const path = require('path'), fs = require('fs');
const dir = path.resolve(process.argv[2]), jobs = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const M0 = require(path.join(dir, 'infinite_gen_wasm.js'));

function wav(file, pcm) {
  const h = Buffer.alloc(44), n = pcm.length * 4;
  h.write('RIFF', 0); h.writeUInt32LE(36 + n, 4); h.write('WAVE', 8); h.write('fmt ', 12); h.writeUInt32LE(16, 16);
  h.writeUInt16LE(3, 20); h.writeUInt16LE(2, 22); h.writeUInt32LE(48000, 24); h.writeUInt32LE(48000 * 8, 28); h.writeUInt16LE(8, 32);
  h.writeUInt16LE(32, 34); h.write('data', 36); h.writeUInt32LE(n, 40);
  fs.writeFileSync(file, Buffer.concat([h, Buffer.from(pcm.buffer)]));
}

M0({ locateFile: f => path.join(dir, f), print: () => {}, printErr: () => {} }).then(M => {
  for (const j of jobs) {
    M._gen_set_shape(0, j.energy, 0, j.mood, 0.5, 0, 0);
    const frames = M._gen_run(j.style, j.part || 0, j.seed, j.bars, -1, -1);
    if (frames) wav(j.out, new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), frames * 2).slice());
    console.log(JSON.stringify({ out: j.out, frames, bpm: M._gen_bpm(), label: M.UTF8ToString(M._gen_label()) }));
  }
  process.exit(0);
});
