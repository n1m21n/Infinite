// Runs Infinite's Generate Samples engine (wasm) off the page's thread. Loaded lazily on the first generate press.
var V = '?v=4'; // bump with every rebuild: the worker, glue, wasm and data are all cached by the browser
importScripts('infinite_gen_wasm.js' + V);

var mod = null;
function ready() {
  if (!mod) mod = InfiniteGen({ locateFile: function (f, dir) { return dir + f + V; } });
  return mod;
}

onmessage = function (e) {
  var m = e.data;
  ready().then(function (M) {
    var t0 = Date.now();
    if (m.cmd === 'info') {
      var styles = [], scales = [], i;
      for (i = 0; i < M._gen_style_count(); i++) styles.push(M.UTF8ToString(M._gen_style_id(i)));
      for (i = 0; i < M._gen_scale_count(); i++) scales.push(M.UTF8ToString(M._gen_scale_name(i)));
      postMessage({ id: m.id, styles: styles, scales: scales });
      return;
    }
    var frames = M._gen_run(m.style, m.part, m.seed, m.bars, m.key, m.scale);
    if (!frames) { postMessage({ id: m.id, error: 'no loop' }); return; }
    var pcm = new Float32Array(M.HEAPF32.buffer, M._gen_pcm(), frames * 2).slice();
    postMessage({ id: m.id, frames: frames, rate: 48000, pcm: pcm, label: M.UTF8ToString(M._gen_label()), patch: M.UTF8ToString(M._gen_patch()), bpm: M._gen_bpm(), attempt: M._gen_attempt(), ms: Date.now() - t0 }, [pcm.buffer]);
  }).catch(function (err) { postMessage({ id: m.id, error: String(err) }); });
};
