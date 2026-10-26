// Renders the website library's generated samples through the wasm build: PER takes for every part, written to
// website/samples/media/lib-<part>-<n>/sample.wav (16-bit) and listed in website/samples/data.json (old lib- items are replaced).
// usage, from build-wasm/: node ../tools/web_gen_library.js infinite_gen_wasm.js ../website/samples
const fs = require('fs'), path = require('path');
const factory = require(path.resolve(process.argv[2]));
const site = path.resolve(process.argv[3]);
const PER = 3;
const PARTS = { synths: 1, keys: 9, leads: 10, pads: 2, plucks: 3, textures: 11, bass: 6, drums: 5, kicks: 12, snares: 13, hats: 14, toms: 15, percussion: 4, fx: 7, foley: 8 };
const KEYLESS = new Set(['drums', 'percussion', 'kicks', 'snares', 'hats', 'toms', 'fx', 'foley']);
const SHORT = new Set(['kicks', 'snares', 'hats', 'toms']);
factory().then(M => {
  const nStyles = M._gen_style_count(), nScales = M._gen_scale_count();
  const dataPath = path.join(site, 'data.json'), data = JSON.parse(fs.readFileSync(dataPath));
  data.samples = data.samples.filter(x => !x.id.startsWith('lib-'));
  let made = 0;
  Object.keys(PARTS).forEach((name, pi) => {
    for (let n = 0; n < PER; n++) {
      const style = (pi * 7 + n * 11) % nStyles, seed = 1000 + pi * 37 + n * 101;
      const scale = KEYLESS.has(name) ? -1 : (pi * 3 + n * 5) % nScales;
      let frames = M._gen_run(style, PARTS[name], seed, SHORT.has(name) ? 1 : 2, -1, scale);
      if (!frames) { console.log('FAIL', name, n); continue; }
      const pcm = new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), frames * 2);
      const id = `lib-${name}-${n + 1}`, dir = path.join(site, 'media', id);
      fs.mkdirSync(dir, { recursive: true });
      const bytes = frames * 4, buf = Buffer.alloc(44 + bytes);
      buf.write('RIFF', 0); buf.writeUInt32LE(36 + bytes, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
      buf.writeUInt32LE(48000, 24); buf.writeUInt32LE(48000 * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(bytes, 40);
      for (let i = 0; i < frames * 2; i++) { const x = Math.max(-1, Math.min(1, pcm[i])); buf.writeInt16LE(Math.round(x < 0 ? x * 32768 : x * 32767), 44 + i * 2); }
      fs.writeFileSync(path.join(dir, 'sample.wav'), buf);
      const peaks = []; let mx = 0;
      for (let b = 0; b < 120; b++) { let pk = 0; for (let j = Math.floor(b * frames / 120); j < Math.floor((b + 1) * frames / 120); j++) pk = Math.max(pk, Math.abs(pcm[j * 2]), Math.abs(pcm[j * 2 + 1])); peaks.push(pk); mx = Math.max(mx, pk); }
      const label = M.UTF8ToString(M._gen_label()), comma = label.indexOf(', ');
      data.samples.push({ id, name: name + (comma < 0 ? '' : label.slice(comma)), kind: 'A', tags: ['audio'], nodes: null, family: null, peaks: peaks.map(p => Math.round(p / (mx || 1) * 1000) / 1000),
        audio: `media/${id}/sample.wav`, video: null, poster: null, download: `media/${id}/sample.wav`, patch: null, curated: false, library: null,
        part: name, style: M.UTF8ToString(M._gen_style_id(style)), key: comma < 0 ? '' : label.slice(comma + 2), bpm: Math.round(M._gen_bpm()) });
      made++;
    }
  });
  fs.writeFileSync(dataPath, JSON.stringify(data));
  console.log('LIB_DONE', made);
});
