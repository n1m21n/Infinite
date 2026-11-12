/**
 * Nocturne page controller: one dock of icon buttons over the world.
 * Wires window.InfinitePianoEngine and window.InfiniteAmbientEngine to the UI
 * and to NocturneWorld (world.js).
 */
import { NocturneWorld } from './world.js';

const $ = (id) => document.getElementById(id);
const body = document.body;

const SCENES = {
  midnight_grove: { crickets: 45, leaves: 35, fire: 28, water_drops: 22 },
  rainy_vinyl: { rain: 58, vinyl: 42, thunder: 26, noise: 20 },
  pebble_cove: { pebbles: 54, waterfall: 42, soothing: 32, leaves: 22 },
  cozy_cafe: { cafe: 52, rain: 36, vinyl: 30 },
  deep_zen: { soothing: 56, water_drops: 36, noise: 24, crickets: 28 },
  clear: {}
};
const CHANNELS = [
  ['rain', 'Rain'], ['thunder', 'Thunder'], ['leaves', 'Wind'], ['crickets', 'Crickets'],
  ['fire', 'Fire'], ['water_drops', 'Drops'], ['waterfall', 'Falls'], ['pebbles', 'Pebbles'],
  ['vinyl', 'Vinyl'], ['cafe', 'Café'], ['soothing', 'Pad'], ['noise', 'Noise']
];
const KEY_NAMES = ['C', 'C♯', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];
const SCALE_NAMES = { auto: 'auto', dorian: 'dorian', minor: 'aeolian', major: 'major', lydian: 'lydian', 'harmonic minor': 'harmonic minor', maj_pent: 'pentatonic' };

const make = (Ref) => {
  if (!Ref) return null;
  if (typeof Ref !== 'function') return Ref;
  try { return new Ref(); } catch (e) { return Ref(); }
};
const piano = make(window.InfinitePianoEngine);
const ambient = make(window.InfiniteAmbientEngine);
const call = (obj, fn, ...args) => (obj && typeof obj[fn] === 'function' ? obj[fn](...args) : undefined);

let world = null;
let playing = false;
let started = false;
let levels = { ...SCENES.midnight_grove };

/* ---------- world ---------- */

try {
  world = new NocturneWorld($('world'), {
    onProgress: (p) => { $('load').textContent = `Loading ${Math.round(p * 100)}%`; },
    onReady: () => { $('load').classList.add('is-done'); }
  });
  world.onInteract(() => { if (started) call(piano, 'triggerManualNote', 62 + [0, 3, 7, 10, 12][Math.floor(Math.random() * 5)], 0.5, 2.2); });
  window.nocturne = world;
} catch (err) {
  console.warn('Nocturne: 3D view unavailable', err);
  $('load').textContent = 'Sound only on this device';
}

/* ---------- toast ---------- */

let toastTimer = 0;
function toast(text, ms = 2600) {
  const el = $('toast');
  el.hidden = true;
  void el.offsetWidth;
  el.textContent = text;
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.hidden = true; }, ms);
}

/* ---------- transport ---------- */

function setPlayingUI(on) {
  playing = on;
  body.classList.toggle('is-playing', on);
  $('play-icon').setAttribute('href', on ? '#i-pause' : '#i-play');
  $('btn-play').setAttribute('aria-label', on ? 'Pause' : 'Play');
  $('btn-play').dataset.tip = on ? 'Pause' : 'Play';
}

function pushLevels() {
  for (const [ch] of CHANNELS) call(ambient, 'setChannelLevel', ch, (levels[ch] || 0) / 100);
}

async function play() {
  if (playing) return;
  setPlayingUI(true);
  try {
    await call(piano, 'start');
    const ctx = call(piano, 'getAudioContext');
    if (ctx) call(ambient, 'attachAudioContext', ctx, call(piano, 'getSpaceInputNode') || null, call(piano, 'getOutputNode') || null);
    await call(ambient, 'start');
    if (!started) { started = true; pushLevels(); }
  } catch (err) {
    console.warn('Nocturne: audio could not start', err);
    setPlayingUI(false);
    toast('Audio could not start');
  }
}

function pause() {
  if (!playing) return;
  if (piano && typeof piano.pause === 'function') piano.pause(); else call(piano, 'stop');
  call(ambient, 'pause');
  setPlayingUI(false);
}

$('btn-play').addEventListener('click', () => (playing ? pause() : play()));

/* ---------- intro ---------- */

function enter(withSound) {
  body.classList.remove('is-intro');
  if (withSound) play();
  const touch = window.matchMedia('(hover: none)').matches;
  setTimeout(() => toast(touch ? 'Drag to look · tap to walk' : 'Drag to look · click to walk', 4200), 900);
}
$('btn-enter').addEventListener('click', () => enter(true));
$('btn-look').addEventListener('click', () => enter(false));

/* ---------- key ribbon ---------- */

const ribbon = $('ribbon');
const rctx = ribbon.getContext('2d');
const glow = new Float32Array(88);
let ribbonW = 0;
const css = getComputedStyle(document.documentElement);
const signal = css.getPropertyValue('--signal').trim();
const ink3 = css.getPropertyValue('--ink-3').trim();

function sizeRibbon() {
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  ribbonW = ribbon.clientWidth;
  ribbon.width = Math.round(ribbonW * dpr);
  ribbon.height = Math.round(28 * dpr);
  rctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}
new ResizeObserver(sizeRibbon).observe(ribbon);

let lastT = performance.now();
function drawRibbon(now) {
  const dt = Math.min(0.1, (now - lastT) / 1000);
  lastT = now;
  if (ribbonW) {
    rctx.clearRect(0, 0, ribbonW, 28);
    const step = ribbonW / 88;
    for (let i = 0; i < 88; i++) {
      const g = glow[i];
      const h = 4 + g * 22;
      rctx.globalAlpha = 0.28 + g * 0.72;
      rctx.fillStyle = g > 0.02 ? signal : ink3;
      rctx.beginPath();
      rctx.roundRect(i * step + step * 0.2, 28 - h, Math.max(1.5, step * 0.6), h, 1.5);
      rctx.fill();
      glow[i] = g * Math.exp(-dt * 2.2);
    }
    rctx.globalAlpha = 1;
  }
  requestAnimationFrame(drawRibbon);
}
requestAnimationFrame(drawRibbon);

ribbon.addEventListener('pointerdown', (e) => {
  const r = ribbon.getBoundingClientRect();
  const midi = 21 + Math.max(0, Math.min(87, Math.floor(((e.clientX - r.left) / r.width) * 88)));
  if (started) call(piano, 'triggerManualNote', midi, 0.72, 2.2);
  glow[midi - 21] = 1;
});

/* ---------- engine callbacks ---------- */

call(piano, 'onNote', (n) => {
  if (!n) return;
  if (n.midi >= 21 && n.midi <= 108) glow[n.midi - 21] = Math.max(glow[n.midi - 21], 0.35 + (n.velocity || 0.5) * 0.65);
  if (world) world.pulseNote(n);
});
call(piano, 'onBarChange', (bar) => {
  if (!bar) return;
  if (bar.keyName && bar.scaleName) $('hud-key').textContent = `${bar.keyName} ${String(bar.scaleName).toLowerCase()}`;
  if (bar.chordName) $('hud-chord').textContent = bar.chordName;
  if (bar.bpm) $('hud-bpm').textContent = `${Math.round(bar.bpm)} BPM`;
});
call(piano, 'onWasmUpdate', (w) => {
  if (!w) return;
  if (w.patch) $('patch-code').textContent = w.patch;
  if (w.why) $('patch-meta').textContent = `${w.label || 'PianoNode'} · ${w.why}`;
});
call(ambient, 'onThunderStrike', (v) => { if (world) world.lightning(v || 0.85); });

setInterval(() => {
  if (!world) return;
  const s = call(ambient, 'getActiveSummary');
  const w = s && s.weatherState;
  if (w) world.setWeather({ rain: playing ? w.rain : 0, wind: w.wind });
}, 500);

/* ---------- popovers ---------- */

const popButtons = [...document.querySelectorAll('[data-pop]')];
function closePops(except) {
  for (const b of popButtons) {
    const p = $(b.dataset.pop);
    if (p === except) continue;
    p.hidden = true;
    b.setAttribute('aria-expanded', 'false');
  }
}
for (const b of popButtons) {
  b.addEventListener('click', () => {
    const p = $(b.dataset.pop);
    const open = p.hidden;
    closePops(open ? p : null);
    p.hidden = !open;
    b.setAttribute('aria-expanded', String(open));
    if (open) {
      const r = b.getBoundingClientRect();     // sit above its own button, kept on screen
      const half = p.offsetWidth / 2, pad = 16;
      p.style.left = `${Math.max(half + pad, Math.min(window.innerWidth - half - pad, r.left + r.width / 2))}px`;
    }
  });
}
document.addEventListener('pointerdown', (e) => {
  if (!e.target.closest('.pop, [data-pop]')) closePops();
});

/* ---------- radio chips ---------- */

function radios(selector, onPick) {
  const items = [...document.querySelectorAll(selector)];
  for (const el of items) {
    el.addEventListener('click', () => {
      for (const o of items) o.setAttribute('aria-checked', String(o === el));
      onPick(el);
    });
  }
}

radios('[data-model]', (el) => call(piano, 'setPianoModel', el.dataset.model));
radios('[data-noise]', (el) => call(ambient, 'setNoiseType', el.dataset.noise));

/* ---------- feel ---------- */

function slider(input, onInput) {
  const out = input.parentElement.querySelector('output');
  const paint = () => {
    const min = +input.min || 0, max = +input.max || 100;
    input.style.setProperty('--fill', `${((input.value - min) / (max - min)) * 100}%`);
    if (out) out.textContent = input.value;
  };
  input.addEventListener('input', () => { paint(); onInput(+input.value / 100); });
  paint();
  return paint;
}
slider($('s-smooth'), (v) => call(piano, 'setSmoothness', v));
slider($('s-soothe'), (v) => call(piano, 'setSoothingness', v));
slider($('s-space'), (v) => { call(piano, 'setSpace', v); call(ambient, 'setGlobalSpace', v); });
slider($('s-range'), (v) => call(piano, 'setNoteRangeWidth', v));

function harmony() {
  const k = parseInt($('sel-key').value, 10), sc = $('sel-scale').value;
  call(piano, 'setHarmonyPreference', k, sc);
  $('hud-key').textContent = `${k >= 0 ? KEY_NAMES[k % 12] : 'Auto'} ${SCALE_NAMES[sc] || sc}`;
}
$('sel-key').addEventListener('change', harmony);
$('sel-scale').addEventListener('change', harmony);

/* ---------- ambience ---------- */

const mix = {};
for (const [ch, label] of CHANNELS) {
  const row = document.createElement('label');
  row.className = 'row';
  const name = document.createElement('span');
  name.textContent = label;
  const input = document.createElement('input');
  input.type = 'range'; input.min = 0; input.max = 100; input.value = levels[ch] || 0;
  row.append(name, input);
  $('mix-grid').append(row);
  mix[ch] = { input, paint: slider(input, (v) => { levels[ch] = v * 100; call(ambient, 'setChannelLevel', ch, v); }) };
}
radios('[data-scene]', (el) => {
  levels = { ...SCENES[el.dataset.scene] };
  for (const [ch] of CHANNELS) { mix[ch].input.value = levels[ch] || 0; mix[ch].paint(); }
  pushLevels();
});

/* ---------- like, next, sky ---------- */

$('btn-like').addEventListener('click', () => {
  call(piano, 'recordFeedback', 1.0, 'user_love');
  $('like-icon').setAttribute('href', '#i-star-fill');
  $('btn-like').classList.add('is-on');
  toast('More like this');
  setTimeout(() => { $('like-icon').setAttribute('href', '#i-star'); $('btn-like').classList.remove('is-on'); }, 1800);
});
$('btn-next').addEventListener('click', () => { call(piano, 'evolveNextPhrase'); toast('New phrase'); });

$('btn-sky').addEventListener('click', () => {
  if (!world) return;
  const next = world.getMode() === 'midnight' ? 'sunset' : 'midnight';
  world.setMode(next);
  const other = next === 'midnight' ? 'Sunset' : 'Midnight';
  $('sky-icon').setAttribute('href', next === 'midnight' ? '#i-sun' : '#i-moon');
  $('btn-sky').dataset.tip = other;
  $('btn-sky').setAttribute('aria-label', `Switch to ${other.toLowerCase()}`);
});

/* ---------- hide, patch, keys ---------- */

function setZen(on) {
  body.classList.toggle('is-zen', on);
  $('btn-hide').setAttribute('aria-pressed', String(on));
  $('btn-hide').setAttribute('aria-label', on ? 'Show controls' : 'Hide controls');
  $('btn-hide').dataset.tip = on ? 'Show' : 'Hide';
  $('btn-hide').querySelector('use').setAttribute('href', on ? '#i-eye-off' : '#i-eye');
  if (on) closePops();
}
$('btn-hide').addEventListener('click', () => setZen(!body.classList.contains('is-zen')));

const patch = $('patch');
$('btn-patch').addEventListener('click', () => {
  const text = call(piano, 'getCurrentPatchText');
  if (text) $('patch-code').textContent = text;
  patch.showModal();
});
$('btn-patch-close').addEventListener('click', () => patch.close());
patch.addEventListener('click', (e) => { if (e.target === patch) patch.close(); });
$('btn-download').addEventListener('click', () => {
  const text = call(piano, 'getCurrentPatchText') || $('patch-code').textContent;
  const url = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
  const a = document.createElement('a');
  a.href = url; a.download = `infinite-nocturne-${Date.now()}.inf`;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
});

window.addEventListener('keydown', (e) => {
  const tag = e.target.tagName;
  if (tag === 'INPUT' || tag === 'SELECT' || e.metaKey || e.ctrlKey || patch.open) return;
  if (e.code === 'Escape') { closePops(); setZen(false); }
  if (body.classList.contains('is-intro')) return;
  if (e.code === 'Space' && tag !== 'BUTTON') { e.preventDefault(); playing ? pause() : play(); }
  if (e.code === 'KeyH') setZen(!body.classList.contains('is-zen'));
});
