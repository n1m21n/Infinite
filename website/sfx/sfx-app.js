// Infinite SFX & Ambient Soundscape Maker
// Genuinely backed by Infinite's C++ Generate Samples engine (infinite_gen_wasm.wasm) via ../gen/gen-worker.js
(function () {
  'use strict';

  const $ = (id) => document.getElementById(id);

  const PRESETS = [
    { id: 'scifi',    label: 'Sci-fi',    part: 7,  styles: ['neurofunk', 'psytrance', 'idm'],         bars: 2, loop: false, tone:  0.25, energy:  0.35, space:  0.35, mood:  0.10, variation: 0.60 },
    { id: 'glitch',   label: 'Glitch',    part: 7,  styles: ['glitch', 'electro', 'idm'],              bars: 1, loop: false, tone:  0.35, energy:  0.55, space:  0.10, mood:  0.20, variation: 0.72 },
    { id: 'impact',   label: 'Impact',    part: 7,  styles: ['industrial_techno', 'dubstep'],          bars: 1, loop: false, tone: -0.35, energy:  0.65, space:  0.40, mood: -0.45, variation: 0.55 },
    { id: 'foley',    label: 'Foley',     part: 8,  styles: ['downtempo', 'lofi_beats', 'ambient'],    bars: 2, loop: false, tone:  0.00, energy: -0.10, space:  0.15, mood:  0.00, variation: 0.55 },
    { id: 'perc',     label: 'Metal',     part: 4,  styles: ['minimal_techno', 'idm', 'dub_techno'],   bars: 1, loop: false, tone:  0.20, energy:  0.30, space:  0.25, mood: -0.10, variation: 0.60 },
    { id: 'chime',    label: 'Chime',     part: 3,  styles: ['ambient', 'vaporwave', 'downtempo'],     bars: 1, loop: false, tone:  0.40, energy: -0.20, space:  0.45, mood:  0.45, variation: 0.45 },
    { id: 'drone',    label: 'Drone',     part: 11, styles: ['dark_ambient', 'industrial_techno'],     bars: 4, loop: true,  tone: -0.35, energy: -0.35, space:  0.70, mood: -0.60, variation: 0.50 },
    { id: 'space',    label: 'Space',     part: 11, styles: ['ambient', 'dub_techno'],                 bars: 4, loop: true,  tone:  0.10, energy: -0.40, space:  0.80, mood:  0.15, variation: 0.55 },
    { id: 'lofi',     label: 'Lo-fi',     part: 11, styles: ['lofi_beats', 'vaporwave', 'downtempo'],  bars: 4, loop: true,  tone: -0.15, energy: -0.25, space:  0.45, mood:  0.20, variation: 0.50 },
    { id: 'pad',      label: 'Pad',       part: 2,  styles: ['ambient', 'neo_classical', 'dub_techno'],bars: 4, loop: true,  tone:  0.05, energy: -0.30, space:  0.75, mood:  0.30, variation: 0.48 }
  ];

  const PARTS = [
    [7,  'FX'],
    [8,  'Foley'],
    [11, 'Textures'],
    [2,  'Pads'],
    [4,  'Percussion'],
    [3,  'Plucks'],
    [12, 'Kicks'],
    [13, 'Snares']
  ];

  const IC_PLAY = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><path d="M6.5 4.8v10.4a.6.6 0 0 0 .9.5l8.2-5.2a.6.6 0 0 0 0-1L7.4 4.3a.6.6 0 0 0-.9.5Z"/></svg>';
  const IC_STOP = '<svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true"><rect x="5" y="5" width="10" height="10" rx="2"/></svg>';

  const CAT_COLOUR = {
    Synths: 'var(--cat-synths)',
    AudioEffects: 'var(--cat-audioeffects)',
    Notes: 'var(--cat-notes)',
    Utility: 'var(--cat-utility)',
    Modulators: 'var(--cat-modulators)',
    Macros: 'var(--cat-macros)'
  };

  const state = {
    preset: PRESETS[0].id,
    styleIdx: -1,
    part: PRESETS[0].part,
    bars: PRESETS[0].bars,
    tone: PRESETS[0].tone,
    energy: PRESETS[0].energy,
    space: PRESETS[0].space,
    mood: PRESETS[0].mood,
    variation: PRESETS[0].variation,
    loop: PRESETS[0].loop,
    reverse: false,
    fade: true,
    trimA: 0,
    trimB: 1
  };

  let engineInfo = null;
  let worker = null;
  let jobId = 0;
  const jobs = {};
  let inflight = false;
  let queued = null;

  let take = null;
  let takes = [];
  let takeCounter = 0;

  // Web Audio playback
  let ac = null;
  let gainNode = null;
  let analyser = null;
  let freqData = null;
  let srcNode = null;
  let srcGain = null;
  let curBuf = null;
  let playing = false;
  let startedAt = 0;

  const canvas = $('sfx-canvas');
  const ctx = canvas.getContext('2d');
  const statusEl = $('sfx-status');
  const hudPill = $('sfx-hud');
  const hudText = $('sfx-hud-text');
  const hudMeta = $('sfx-hud-meta');
  const playBtn = $('btn-play');
  const genBtn = $('btn-gen');
  const playheadEl = $('sfx-playhead');
  const cutLeftEl = $('sfx-cut-l');
  const cutRightEl = $('sfx-cut-r');
  const handleAEl = $('sfx-handle-a');
  const handleBEl = $('sfx-handle-b');
  const waveWrap = $('sfx-wave-wrap');

  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const fmt = (v) => (v >= 0 ? '+' : '') + v.toFixed(2);
  const niceStyle = (s) => String(s || '').replace(/_/g, ' ');

  // ---------------- Worker bridge ----------------
  function askWorker(msg) {
    if (!worker) {
      worker = new Worker('../gen/gen-worker.js?v=9');
      worker.onmessage = (e) => {
        const j = jobs[e.data.id];
        delete jobs[e.data.id];
        if (j) {
          if (e.data.error) j[1](new Error(e.data.error));
          else j[0](e.data);
        }
      };
      worker.onerror = (err) => {
        Object.keys(jobs).forEach((k) => { jobs[k][1](err); delete jobs[k]; });
        worker = null;
      };
    }
    return new Promise((resolve, reject) => {
      msg.id = ++jobId;
      jobs[msg.id] = [resolve, reject];
      worker.postMessage(msg);
    });
  }

  function ensureEngine() {
    if (engineInfo) return Promise.resolve(engineInfo);
    setBusy(true, 'Loading...');
    return askWorker({ cmd: 'info' }).then((info) => {
      engineInfo = info;
      return info;
    });
  }

  function styleIdxByNames(names) {
    if (!engineInfo || !engineInfo.styles.length) return 0;
    const matches = [];
    names.forEach((n) => {
      const idx = engineInfo.styles.indexOf(n);
      if (idx >= 0) matches.push(idx);
    });
    if (matches.length) return matches[Math.floor(Math.random() * matches.length)];
    return Math.floor(Math.random() * engineInfo.styles.length);
  }

  function setBusy(busy, text) {
    genBtn.disabled = busy;
    hudPill.classList.toggle('is-busy', busy);
    if (text) hudText.textContent = text;
  }

  function buildJob(pinned) {
    let styleIdx = styleIdxByNames(['neurofunk', 'psytrance', 'idm']);
    if (pinned && take) {
      styleIdx = take.styleIdx;
    }
    const seed = (pinned && take) ? take.seed : Math.floor(Math.random() * 99999) + 1;
    return {
      cmd: 'run',
      style: styleIdx,
      part: 7,
      seed: seed,
      bars: state.bars,
      key: -1,
      scale: -1,
      tone: state.tone,
      energy: state.energy,
      space: state.space,
      mood: state.mood,
      variation: state.variation,
      bpm: 0,
      pinned: !!(pinned && take)
    };
  }

  function runRender(job, autoPlay) {
    if (inflight) {
      queued = { job, autoPlay };
      return;
    }
    inflight = true;
    setBusy(true, 'Making...');
    const t0 = performance.now();
    askWorker(job).then((r) => {
      const ms = Math.round(performance.now() - t0);
      const seed = job.pinned ? job.seed : job.seed + r.attempt * 7919;
      const styleName = engineInfo && engineInfo.styles[job.style] ? niceStyle(engineInfo.styles[job.style]) : 'infinite';
      const newTake = {
        n: ++takeCounter,
        styleIdx: job.style,
        styleName,
        part: job.part,
        seed,
        bars: job.bars,
        pcm: r.pcm,
        rate: r.rate,
        frames: r.frames,
        bpm: Math.round(r.bpm || 120),
        label: r.label,
        patch: r.patch,
        graph: parsePatch(r.patch),
        ms
      };
      if (!job.pinned) {
        state.trimA = 0;
        state.trimB = 1;
      }
      const prev = take;
      take = newTake;
      takes.unshift(newTake);
      if (takes.length > 10) takes.pop();
      renderTakes();
      updateTrimUI();
      hudText.textContent = `#${newTake.n} · ${(newTake.frames / newTake.rate).toFixed(1)}s`;
      if (autoPlay || playing) {
        swapAudio(prev, autoPlay);
      } else {
        drawWaveform();
      }
    }).catch(() => {
      hudText.textContent = take ? `#${take.n} · ${(take.frames / take.rate).toFixed(1)}s` : 'Try again';
    }).finally(() => {
      inflight = false;
      if (queued) {
        const next = queued;
        queued = null;
        runRender(next.job, next.autoPlay);
      } else {
        setBusy(false);
      }
    });
  }

  function generate(autoPlay = true) {
    ensureEngine().then(() => runRender(buildJob(false), autoPlay));
  }

  function reshape() {
    if (!take) { generate(true); return; }
    ensureEngine().then(() => runRender(buildJob(true), playing));
  }

  // ---------------- Audio playback & editing ----------------
  function wakeAudio() {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return false;
    if (!ac) {
      ac = new AC();
      gainNode = ac.createGain();
      analyser = ac.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.8;
      freqData = new Uint8Array(analyser.frequencyBinCount);
      gainNode.connect(analyser);
      analyser.connect(ac.destination);
    }
    if (ac.state === 'suspended') ac.resume();
    return true;
  }

  function getEditedStereo(t) {
    const n = t.frames;
    const a = Math.floor(state.trimA * n);
    const b = Math.max(a + 128, Math.floor(state.trimB * n));
    const len = b - a;
    const L = new Float32Array(len);
    const R = new Float32Array(len);
    for (let i = 0; i < len; i++) {
      const s = state.reverse ? (b - 1 - i) : (a + i);
      L[i] = t.pcm[s * 2];
      R[i] = t.pcm[s * 2 + 1];
    }
    const fadeN = state.fade
      ? Math.min(len >> 1, Math.floor(len * 0.08))
      : Math.min(len >> 2, Math.floor(t.rate * 0.004));
    for (let i = 0; i < fadeN; i++) {
      const g = (i / fadeN) * (i / fadeN);
      L[i] *= g; R[i] *= g;
      L[len - 1 - i] *= g; R[len - 1 - i] *= g;
    }
    return { L, R, len, rate: t.rate };
  }

  function makeAudioBuffer(t) {
    const ed = getEditedStereo(t);
    const buf = ac.createBuffer(2, ed.len, ed.rate);
    buf.getChannelData(0).set(ed.L);
    buf.getChannelData(1).set(ed.R);
    return buf;
  }

  function currentPhase() {
    if (!playing || !curBuf || !ac) return 0;
    const elapsed = ac.currentTime - startedAt;
    if (state.loop) return (elapsed % curBuf.duration) / curBuf.duration;
    return clamp(elapsed / curBuf.duration, 0, 1);
  }

  function startPlayback(atFrac) {
    if (!take || !wakeAudio()) return;
    const oldSrc = srcNode;
    const oldGain = srcGain;
    const now = ac.currentTime;
    const xfade = 0.035;

    curBuf = makeAudioBuffer(take);
    const src = ac.createBufferSource();
    src.buffer = curBuf;
    src.loop = state.loop;
    const g = ac.createGain();
    g.gain.setValueAtTime(oldSrc ? 0 : 1, now);
    if (oldSrc) g.gain.linearRampToValueAtTime(1, now + xfade);
    src.connect(g);
    g.connect(gainNode);

    const offset = clamp(atFrac || 0, 0, 0.99) * curBuf.duration;
    src.start(now, offset);
    startedAt = now - offset;
    srcNode = src;
    srcGain = g;
    playing = true;
    updatePlayBtn();

    src.onended = () => {
      if (srcNode === src && !state.loop) {
        playing = false;
        srcNode = null;
        playheadEl.hidden = true;
        updatePlayBtn();
      }
    };

    if (oldSrc) {
      try {
        oldGain.gain.setValueAtTime(oldGain.gain.value, now);
        oldGain.gain.linearRampToValueAtTime(0, now + xfade);
        oldSrc.stop(now + xfade + 0.01);
      } catch (_) {}
    }
  }

  function stopPlayback() {
    playing = false;
    if (srcNode) {
      try { srcNode.stop(); } catch (_) {}
      srcNode = null;
    }
    playheadEl.hidden = true;
    updatePlayBtn();
  }

  function swapAudio(prev, forcePlay) {
    if (!wakeAudio()) return;
    const keep = (prev && playing && state.loop) ? currentPhase() : 0;
    if (playing || forcePlay) startPlayback(keep);
    else curBuf = makeAudioBuffer(take);
  }

  function updatePlayBtn() {
    playBtn.innerHTML = playing ? IC_STOP : IC_PLAY;
    playBtn.classList.toggle('is-active', playing);
    playBtn.setAttribute('aria-label', playing ? 'Stop' : 'Play');
  }

  // ---------------- Waveform + live FFT rendering ----------------
  function drawWaveform() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(320, Math.floor(rect.width * dpr));
    const h = Math.max(180, Math.floor(rect.height * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }

    const styles = getComputedStyle(document.documentElement);
    const deep = styles.getPropertyValue('--deep').trim() || '#0D1020';
    const accent = styles.getPropertyValue('--accent').trim() || '#FF6B35';
    const signal = styles.getPropertyValue('--signal').trim() || '#A7B1FF';
    const ink3 = styles.getPropertyValue('--ink-3').trim() || '#8890B1';

    ctx.fillStyle = deep;
    ctx.fillRect(0, 0, w, h);

    // Subtle studio grid
    ctx.strokeStyle = 'rgba(167, 177, 255, 0.08)';
    ctx.lineWidth = 1;
    for (let i = 1; i < 8; i++) {
      const gx = (w / 8) * i;
      ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, h); ctx.stroke();
    }
    ctx.beginPath(); ctx.moveTo(0, h * 0.5); ctx.lineTo(w, h * 0.5); ctx.stroke();

    // Live FFT spectrum glow behind the waveform while playing
    if (playing && analyser && freqData) {
      analyser.getByteFrequencyData(freqData);
      const bins = 64;
      const bw = w / bins;
      for (let i = 0; i < bins; i++) {
        const mag = freqData[i] / 255;
        const bh = mag * h * 0.44;
        ctx.fillStyle = 'rgba(167, 177, 255, 0.12)';
        ctx.fillRect(i * bw, h * 0.5 - bh, bw - 1, bh * 2);
      }
    }

    if (!take) {
      ctx.fillStyle = ink3;
      ctx.font = `500 ${Math.round(13 * dpr)}px 'Geist Mono', monospace`;
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText('Press Generate to synthesise with Infinite WASM', w * 0.5, h * 0.5);
      return;
    }

    const pcm = take.pcm;
    const frames = take.frames;
    const cols = Math.floor(w / (2 * dpr));
    const step = Math.max(1, Math.floor(frames / cols));
    const mid = h * 0.5;
    const amp = h * 0.41;

    for (let c = 0; c < cols; c++) {
      const frac = c / cols;
      const inTrim = frac >= state.trimA && frac <= state.trimB;
      const start = Math.floor(frac * frames);
      let peakL = 0, peakR = 0;
      for (let i = 0; i < step; i += Math.max(1, step >> 4)) {
        const idx = Math.min(frames - 1, start + i) * 2;
        const l = Math.abs(pcm[idx] || 0);
        const r = Math.abs(pcm[idx + 1] || 0);
        if (l > peakL) peakL = l;
        if (r > peakR) peakR = r;
      }
      const x = (c / cols) * w;
      const bw = Math.max(1.5 * dpr, (w / cols) * 0.68);
      const hL = Math.max(1.5 * dpr, peakL * amp);
      const hR = Math.max(1.5 * dpr, peakR * amp);

      ctx.fillStyle = inTrim ? signal : 'rgba(136, 144, 177, 0.26)';
      ctx.fillRect(x, mid - hL, bw, hL);
      ctx.globalAlpha = inTrim ? 0.62 : 0.24;
      ctx.fillRect(x, mid, bw, hR);
      ctx.globalAlpha = 1;
    }

    // Playhead update
    if (playing && curBuf) {
      const p = currentPhase();
      const pos = state.trimA + p * (state.trimB - state.trimA);
      playheadEl.hidden = false;
      playheadEl.style.left = (pos * 100).toFixed(3) + '%';
    }
  }

  function animLoop() {
    if (playing) drawWaveform();
    requestAnimationFrame(animLoop);
  }
  requestAnimationFrame(animLoop);

  // ---------------- Trim handles ----------------
  function updateTrimUI() {
    cutLeftEl.style.left = '0%';
    cutLeftEl.style.width = (state.trimA * 100).toFixed(2) + '%';
    cutRightEl.style.left = (state.trimB * 100).toFixed(2) + '%';
    cutRightEl.style.width = ((1 - state.trimB) * 100).toFixed(2) + '%';
    handleAEl.style.left = (state.trimA * 100).toFixed(2) + '%';
    handleBEl.style.left = (state.trimB * 100).toFixed(2) + '%';
    drawWaveform();
  }

  function bindTrimHandle(el, which) {
    el.addEventListener('pointerdown', (e) => {
      if (!take) return;
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const rect = waveWrap.getBoundingClientRect();
        const x = clamp((ev.clientX - rect.left) / rect.width, 0, 1);
        if (which === 'a') state.trimA = Math.min(x, state.trimB - 0.04);
        else state.trimB = Math.max(x, state.trimA + 0.04);
        updateTrimUI();
      };
      const up = () => {
        el.removeEventListener('pointermove', move);
        el.removeEventListener('pointerup', up);
        if (playing) startPlayback(0);
      };
      el.addEventListener('pointermove', move);
      el.addEventListener('pointerup', up);
    });
  }
  bindTrimHandle(handleAEl, 'a');
  bindTrimHandle(handleBEl, 'b');

  // ---------------- 2D Feel Pad (Energy x Mood) ----------------
  const padEl = $('sfx-pad');
  const padDot = $('sfx-pad-dot');

  function paintPad() {
    const px = ((state.energy + 1) * 0.5) * 100;
    const py = ((1 - state.mood) * 0.5) * 100;
    padDot.style.left = px.toFixed(1) + '%';
    padDot.style.top = py.toFixed(1) + '%';
    $('val-feel').textContent = `E ${fmt(state.energy)} · M ${fmt(state.mood)}`;
  }

  let padTimer = null;
  function updatePadFromPointer(e) {
    const r = padEl.getBoundingClientRect();
    const nx = clamp((e.clientX - r.left) / r.width, 0, 1);
    const ny = clamp((e.clientY - r.top) / r.height, 0, 1);
    state.energy = +(nx * 2 - 1).toFixed(2);
    state.mood = +((1 - ny) * 2 - 1).toFixed(2);
    paintPad();
    clearTimeout(padTimer);
    padTimer = setTimeout(reshape, 90);
  }

  padEl.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    padEl.classList.add('is-drag');
    padEl.setPointerCapture(e.pointerId);
    updatePadFromPointer(e);
    const move = (ev) => updatePadFromPointer(ev);
    const up = () => {
      padEl.classList.remove('is-drag');
      padEl.removeEventListener('pointermove', move);
      padEl.removeEventListener('pointerup', up);
      clearTimeout(padTimer);
      reshape();
    };
    padEl.addEventListener('pointermove', move);
    padEl.addEventListener('pointerup', up);
  });

  // ---------------- Patch parser & SVG node graph ----------------
  function parsePatch(txt) {
    const nodes = {};
    const edges = [];
    let cur = null;
    String(txt || '').split('\n').forEach((ln) => {
      let m;
      if ((m = /^node (\d+) (\S+) (.+)$/.exec(ln))) {
        cur = nodes[m[1]] = { id: +m[1], cat: m[2], type: m[3], p: {} };
        return;
      }
      if (ln === 'end') { cur = null; return; }
      if (cur && (m = /^  (\w) (\S+) (.*)$/.exec(ln))) {
        cur.p[m[2]] = m[3];
        return;
      }
      if ((m = /^(aud|note) (\d+) (\d+) (\d+)$/.exec(ln))) {
        edges.push({ kind: m[1], dst: +m[2], src: +m[4] });
      }
    });
    return { nodes, edges };
  }

  function renderPatchGraph(graph) {
    const card = $('sfx-patch-card');
    const host = $('sfx-patch-svg-host');
    const ids = Object.keys(graph.nodes).map(Number);
    if (!ids.length) { card.hidden = true; return; }
    card.hidden = false;

    // Topological column assignment
    const col = {};
    ids.forEach((id) => { col[id] = 0; });
    for (let pass = 0; pass < ids.length; pass++) {
      graph.edges.forEach((e) => {
        if (col[e.src] != null && col[e.dst] != null) {
          col[e.dst] = Math.max(col[e.dst], col[e.src] + 1);
        }
      });
    }
    const cols = {};
    ids.forEach((id) => {
      const c = col[id];
      (cols[c] = cols[c] || []).push(id);
    });

    const W = 138, H = 44, GX = 38, GY = 14, PAD = 12;
    const pos = {};
    let maxCol = 0, maxRow = 0;
    Object.keys(cols).forEach((ck) => {
      const c = +ck;
      if (c > maxCol) maxCol = c;
      cols[c].forEach((id, r) => {
        if (r > maxRow) maxRow = r;
        pos[id] = { x: PAD + c * (W + GX), y: PAD + r * (H + GY) };
      });
    });

    const svgW = Math.max(480, PAD * 2 + (maxCol + 1) * W + maxCol * GX);
    const svgH = Math.max(88, PAD * 2 + (maxRow + 1) * H + maxRow * GY);

    const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

    const edgeSvg = graph.edges.map((e) => {
      const a = pos[e.src], b = pos[e.dst];
      if (!a || !b) return '';
      const x1 = a.x + W, y1 = a.y + H * 0.5;
      const x2 = b.x, y2 = b.y + H * 0.5;
      const cx = (x1 + x2) * 0.5;
      return `<path class="${e.kind}" d="M${x1},${y1} C${cx},${y1} ${cx},${y2} ${x2},${y2}"/>`;
    }).join('');

    const nodeSvg = ids.map((id) => {
      const nd = graph.nodes[id];
      const p = pos[id];
      const stroke = CAT_COLOUR[nd.cat] || 'var(--signal)';
      const name = nd.type.length > 16 ? nd.type.slice(0, 15) + '…' : nd.type;
      return `<g class="nd" transform="translate(${p.x},${p.y})">
        <rect width="${W}" height="${H}" rx="10" stroke="${stroke}"/>
        <text class="c" x="10" y="16">${esc(nd.cat)}</text>
        <text x="10" y="33">${esc(name)}</text>
      </g>`;
    }).join('');

    host.innerHTML = `<svg class="st-patch-svg" viewBox="0 0 ${svgW} ${svgH}" width="${svgW}" height="${svgH}" role="img" aria-label="Infinite DSP node graph for this sound">${edgeSvg}${nodeSvg}</svg>`;
    $('sfx-patch-meta').textContent = `${ids.length} nodes · ${graph.edges.length} cables · openable in Infinite`;
  }

  // ---------------- Take history ----------------
  function renderTakes() {
    const strip = $('sfx-takes');
    if (!takes.length) { strip.hidden = true; return; }
    strip.hidden = false;
    strip.innerHTML = takes.map((t, idx) =>
      `<button type="button" class="st-take${t === take ? ' is-active' : ''}" data-take="${idx}">#${t.n} · ${(t.frames / t.rate).toFixed(1)}s</button>`
    ).join('');
    strip.querySelectorAll('.st-take').forEach((btn) => {
      btn.addEventListener('click', () => {
        const picked = takes[+btn.dataset.take];
        if (!picked) return;
        const prev = take;
        take = picked;
        state.trimA = 0; state.trimB = 1;
        renderTakes();
        updateTrimUI();
        hudText.textContent = `#${take.n} · ${(take.frames / take.rate).toFixed(1)}s`;
        hudMeta.textContent = `${(take.frames / take.rate).toFixed(2)}s · ${take.rate / 1000} kHz`;
        swapAudio(prev, true);
      });
    });
  }

  // ---------------- WAV encoder (48kHz 16-bit Stereo) ----------------
  function encodeStereoWav(t) {
    const ed = getEditedStereo(t);
    const numSamples = ed.len * 2;
    const ab = new ArrayBuffer(44 + numSamples * 2);
    const dv = new DataView(ab);
    const ws = (o, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(o + i, s.charCodeAt(i)); };
    ws(0, 'RIFF');
    dv.setUint32(4, 36 + numSamples * 2, true);
    ws(8, 'WAVE');
    ws(12, 'fmt ');
    dv.setUint32(16, 16, true);
    dv.setUint16(20, 1, true);
    dv.setUint16(22, 2, true);
    dv.setUint32(24, ed.rate, true);
    dv.setUint32(28, ed.rate * 4, true);
    dv.setUint16(32, 4, true);
    dv.setUint16(34, 16, true);
    ws(36, 'data');
    dv.setUint32(40, numSamples * 2, true);
    let off = 44;
    for (let i = 0; i < ed.len; i++) {
      const l = clamp(ed.L[i], -1, 1);
      const r = clamp(ed.R[i], -1, 1);
      dv.setInt16(off, l < 0 ? l * 0x8000 : l * 0x7fff, true); off += 2;
      dv.setInt16(off, r < 0 ? r * 0x8000 : r * 0x7fff, true); off += 2;
    }
    return new Blob([ab], { type: 'audio/wav' });
  }

  // ---------------- UI wiring ----------------
  function setSliderFill(el) {
    if (!el) return;
    const min = parseFloat(el.min) || 0, max = parseFloat(el.max) || 1, v = parseFloat(el.value);
    el.style.setProperty('--p', (((v - min) / (max - min || 1)) * 100).toFixed(2) + '%');
  }

  function syncControlsToState() {
    $('ctrl-tone').value = state.tone;
    $('val-tone').textContent = fmt(state.tone);
    $('ctrl-space').value = state.space;
    $('val-space').textContent = fmt(state.space);
    $('ctrl-var').value = state.variation;
    $('val-var').textContent = state.variation.toFixed(2);
    setSliderFill($('ctrl-tone'));
    setSliderFill($('ctrl-space'));
    setSliderFill($('ctrl-var'));
    document.querySelectorAll('#bars-chips .st-chip').forEach((b) =>
      b.classList.toggle('is-active', +b.dataset.bars === state.bars)
    );
    $('btn-loop').classList.toggle('is-active', state.loop);
    $('btn-loop').setAttribute('aria-pressed', String(state.loop));
    paintPad();
  }

  let sliderTimer = null;
  [['ctrl-tone', 'val-tone', 'tone', true], ['ctrl-space', 'val-space', 'space', true], ['ctrl-var', 'val-var', 'variation', false]].forEach(([id, vid, key, signed]) => {
    $(id).addEventListener('input', (e) => {
      state[key] = parseFloat(e.target.value);
      $(vid).textContent = signed ? fmt(state[key]) : state[key].toFixed(2);
      setSliderFill(e.target);
      clearTimeout(sliderTimer);
      sliderTimer = setTimeout(reshape, 110);
    });
  });

  document.querySelectorAll('#bars-chips .st-chip').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('#bars-chips .st-chip').forEach((b) => b.classList.remove('is-active'));
      btn.classList.add('is-active');
      state.bars = parseInt(btn.dataset.bars, 10);
      generate(true);
    });
  });

  $('btn-loop').addEventListener('click', () => {
    state.loop = !state.loop;
    $('btn-loop').classList.toggle('is-active', state.loop);
    $('btn-loop').setAttribute('aria-pressed', String(state.loop));
    if (srcNode) srcNode.loop = state.loop;
  });

  $('btn-rev').addEventListener('click', () => {
    state.reverse = !state.reverse;
    $('btn-rev').classList.toggle('is-active', state.reverse);
    $('btn-rev').setAttribute('aria-pressed', String(state.reverse));
    if (take && playing) startPlayback(0);
    drawWaveform();
  });

  $('btn-fade').addEventListener('click', () => {
    state.fade = !state.fade;
    $('btn-fade').classList.toggle('is-active', state.fade);
    $('btn-fade').setAttribute('aria-pressed', String(state.fade));
    if (take && playing) startPlayback(0);
  });

  $('btn-gen').addEventListener('click', () => generate(true));
  $('btn-play').addEventListener('click', () => {
    if (!take) { generate(true); return; }
    if (playing) stopPlayback();
    else startPlayback(0);
  });

  $('btn-wav').addEventListener('click', () => {
    if (!take) { generate(false); return; }
    const blob = encodeStereoWav(take);
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `infinite-sfx-${take.n}-${take.seed}.wav`;
    a.click();
    statusEl.textContent = `Downloaded 48 kHz stereo WAV (${(blob.size / 1024).toFixed(0)} KB).`;
  });

  $('btn-inf').addEventListener('click', () => {
    if (!take || !take.patch) { generate(false); return; }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([take.patch], { type: 'text/plain' }));
    a.download = `infinite-sfx-${take.n}-${take.seed}.inf`;
    a.click();
    statusEl.textContent = `Downloaded Infinite patch — open directly in Infinite.`;
  });

  window.addEventListener('keydown', (e) => {
    if (['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName)) return;
    if (e.code === 'Space') { e.preventDefault(); $('btn-play').click(); }
    if (e.key === 'g' || e.key === 'G') { e.preventDefault(); generate(true); }
  });

  window.addEventListener('resize', drawWaveform);

  syncControlsToState();
  updateTrimUI();
  drawWaveform();
  // Pre-warm WASM info and render an initial silent take so the waveform & node graph are immediately live
  ensureEngine().then(() => runRender(buildJob(false), false));
})();
