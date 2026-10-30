// Renders the parity set through the wasm build: 5 styles x 8 parts at seed 1 -> <dir>/<style>-<part>-1.wav (float32 stereo).
// usage: node parity_dump.js <infinite_gen_wasm.js> <outdir>
const fs = require('fs');
const factory = require(require('path').resolve(process.argv[2]));
const out = process.argv[3];
const STYLES = [0, 7, 14, 21, 28], PARTS = [0, 1, 2, 3, 5, 6, 9, 10];
const PART_NAMES = { 0: 'all', 1: 'synths', 2: 'pads', 3: 'plucks', 5: 'drums', 6: 'bass', 9: 'keys', 10: 'leads' };
factory().then(M => {
  fs.mkdirSync(out, { recursive: true });
  for (const s of STYLES) for (const p of PARTS) {
    const n = M._gen_run(s, p, 1, 2, -1, -1);
    if (!n) { console.log('FAIL', s, p); continue; }
    const pcm = new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), n * 2);
    const hdr = Buffer.alloc(44), bytes = n * 8;
    hdr.write('RIFF', 0); hdr.writeUInt32LE(36 + bytes, 4); hdr.write('WAVEfmt ', 8); hdr.writeUInt32LE(16, 16);
    hdr.writeUInt16LE(3, 20); hdr.writeUInt16LE(2, 22); hdr.writeUInt32LE(48000, 24); hdr.writeUInt32LE(48000 * 8, 28);
    hdr.writeUInt16LE(8, 32); hdr.writeUInt16LE(32, 34); hdr.write('data', 36); hdr.writeUInt32LE(bytes, 40);
    fs.writeFileSync(`${out}/${M.UTF8ToString(M._gen_style_id(s))}-${PART_NAMES[p]}-1.wav`, Buffer.concat([hdr, Buffer.from(pcm.buffer, pcm.byteOffset, bytes)]));
  }
  console.log('PARITY_DUMP_DONE');
});
