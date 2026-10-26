// Where each part's energy sits: runs the wasm generator over N seeds per part and reports the long-term spectrum.
//   centroid  power-weighted mean frequency, 20 Hz - 20 kHz
//   peak      the strongest bin of the averaged spectrum above 25 Hz (a kick's or tom's fundamental)
//   sub40     share of power under 40 Hz: below what small speakers and most ears hold on to
// usage, from build-wasm/: node ../tools/web_gen_freq.js infinite_gen_wasm.js [N]
const path = require('path');
const factory = require(path.resolve(process.argv[2]));
const N = +(process.argv[3] || 12), W = 8192;
const PARTS = { all: 0, synths: 1, pads: 2, plucks: 3, percussion: 4, drums: 5, bass: 6, fx: 7, foley: 8, keys: 9, leads: 10, textures: 11, kicks: 12, snares: 13, hats: 14, toms: 15 };
// Typical ranges (Hz) for the main energy of the sound, from mixing references: [centroid lo, hi]; peak is checked for kicks and toms.
const TARGET = { kicks: [40, 250], snares: [900, 4500], toms: [150, 600], hats: [5000, 10000], bass: [60, 900], percussion: [500, 5000] };
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const a = -2 * Math.PI / len, wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < len / 2; k++) {
      const u = i + k, v = u + len / 2, xr = re[v] * cr - im[v] * ci, xi = re[v] * ci + im[v] * cr;
      re[v] = re[u] - xr; im[v] = im[u] - xi; re[u] += xr; im[u] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
factory().then(M => {
  const ns = M._gen_style_count(), hann = new Float64Array(W).map((_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / W));
  console.log('part        centroid   peak   sub40   target centroid');
  for (const [name, pi] of Object.entries(PARTS)) {
    const avg = new Float64Array(W / 2); let cs = [];
    for (let s = 0; s < N; s++) {
      const f = M._gen_run((s * 5) % ns, pi, 700 + s * 17, 2, -1, -1);
      if (f < W) { console.log('  ', name, 'seed', s, 'failed/empty render', f); continue; }
      const p = new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), f * 2), mono = new Float64Array(f);
      for (let i = 0; i < f; i++) mono[i] = 0.5 * (p[i * 2] + p[i * 2 + 1]);
      const one = new Float64Array(W / 2); let w = 0;
      for (let o = 0; o + W <= f; o += W / 2) {
        const re = new Float64Array(W), im = new Float64Array(W);
        for (let i = 0; i < W; i++) re[i] = mono[o + i] * hann[i];
        fft(re, im); for (let k = 0; k < W / 2; k++) one[k] += re[k] * re[k] + im[k] * im[k]; w++;
      }
      let t = 0, c = 0; for (let k = 1; k < W / 2; k++) { const hz = k * 48000 / W; if (hz < 20 || hz > 20000) continue; t += one[k]; c += one[k] * hz; }
      cs.push(c / t); const norm = one.map(v => v / t); for (let k = 0; k < W / 2; k++) avg[k] += norm[k];
    }
    let pk = 0, pv = 0, sub = 0, tot = 0;
    for (let k = 1; k < W / 2; k++) { const hz = k * 48000 / W; if (hz < 20) continue; tot += avg[k]; if (hz < 40) sub += avg[k]; if (hz > 25 && avg[k] > pv) { pv = avg[k]; pk = hz; } }
    if (process.env.VERBOSE) console.log('  ', name, cs.map(Math.round).join(' '));
    cs.sort((a, b) => a - b); const med = cs[cs.length >> 1], tg = TARGET[name];
    console.log(name.padEnd(11), String(Math.round(med)).padStart(7), String(Math.round(pk)).padStart(7), (100 * sub / tot).toFixed(1).padStart(6) + '%', tg ? `  ${tg[0]}-${tg[1]} ${med >= tg[0] && med <= tg[1] ? 'ok' : 'OUT'}` : '');
  }
});
