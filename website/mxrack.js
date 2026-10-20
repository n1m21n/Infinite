/* Musicians page: a four-knob rack that makes sound. Play holds a chord; Cutoff and Resonance shape a low-pass filter, Reverb mixes in a room,
   Wobble is an LFO on the cutoff. Drag a knob up or down, scroll over it, or use arrow keys. Web Audio only, nothing is loaded. */
(function () {
  'use strict';
  var rack = document.querySelector('.mx-rack'); if (!rack) return;
  var play = rack.querySelector('.mx-play'), scope = rack.querySelector('.mx-scope'), ctx2 = scope.getContext('2d');
  var V = { cut: 0.55, res: 0.25, rev: 0.4, lfo: 0.2 }, knobs = {};
  var A = null, on = false, raf = 0;

  /* sound */
  function exp(v, lo, hi) { return lo * Math.pow(hi / lo, v); }
  function build() {
    var AC = window.AudioContext || window.webkitAudioContext; if (!AC) return null;
    var c = new AC(), master = c.createGain(); master.gain.value = 0;
    var filt = c.createBiquadFilter(); filt.type = 'lowpass';
    var dry = c.createGain(), wet = c.createGain(), verb = c.createConvolver();
    var len = Math.floor(c.sampleRate * 2.6), imp = c.createBuffer(2, len, c.sampleRate);
    for (var ch = 0; ch < 2; ch++) { var d = imp.getChannelData(ch); for (var i = 0; i < len; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, 2.6); }
    verb.buffer = imp;
    var an = c.createAnalyser(); an.fftSize = 1024;
    filt.connect(dry); filt.connect(verb); verb.connect(wet); dry.connect(master); wet.connect(master); master.connect(an); an.connect(c.destination);
    var tone = c.createGain(); tone.gain.value = 0.11; tone.connect(filt);
    [110, 164.81, 220, 261.63, 329.63].forEach(function (f, i) {
      [-6, 6].forEach(function (det) { var o = c.createOscillator(); o.type = 'sawtooth'; o.frequency.value = f; o.detune.value = det + i; o.connect(tone); o.start(); });
    });
    var lfo = c.createOscillator(), lg = c.createGain(); lfo.frequency.value = 0.35; lfo.connect(lg); lg.connect(filt.detune); lfo.start();
    return { c: c, master: master, filt: filt, dry: dry, wet: wet, an: an, lg: lg, lfo: lfo };
  }
  function apply() {
    if (!A) return; var t = A.c.currentTime;
    A.filt.frequency.setTargetAtTime(exp(V.cut, 90, 9000), t, 0.03); A.filt.Q.setTargetAtTime(0.4 + V.res * 16, t, 0.03);
    A.wet.gain.setTargetAtTime(V.rev * 1.3, t, 0.05); A.dry.gain.setTargetAtTime(1 - V.rev * 0.35, t, 0.05);
    A.lg.gain.setTargetAtTime(V.lfo * 2400, t, 0.05); A.lfo.frequency.setTargetAtTime(0.25 + V.lfo * 4, t, 0.1);   // wobble in cents on the cutoff
  }
  function toggle() {
    if (!A) { A = build(); if (!A) return; apply(); }
    if (A.c.state === 'suspended') A.c.resume();
    on = !on; var t = A.c.currentTime; A.master.gain.cancelScheduledValues(t); A.master.gain.setTargetAtTime(on ? 0.9 : 0, t, on ? 0.08 : 0.25);
    play.setAttribute('aria-pressed', on ? 'true' : 'false'); rack.classList.toggle('live', on);
    play.querySelector('span').textContent = on ? 'Stop' : 'Play';
    if (!raf) raf = requestAnimationFrame(draw);
  }
  play.addEventListener('click', toggle);

  /* scope */
  function draw() {
    raf = 0; var w = scope.width, h = scope.height; ctx2.clearRect(0, 0, w, h);
    ctx2.lineWidth = 3; ctx2.lineCap = 'round'; ctx2.lineJoin = 'round'; var sc = getComputedStyle(scope).color; ctx2.strokeStyle = sc; ctx2.shadowColor = sc; ctx2.shadowBlur = 12;
    ctx2.beginPath();
    if (A && on) {
      var buf = new Uint8Array(A.an.fftSize); A.an.getByteTimeDomainData(buf);
      var start = 0; for (var s = 1; s < buf.length / 2; s++) if (buf[s - 1] < 128 && buf[s] >= 128) { start = s; break; }
      for (var i = 0; i < 512; i++) { var x = i / 511 * w, y = h / 2 + (buf[start + i] - 128) / 128 * h * 1.4; if (i) ctx2.lineTo(x, y); else ctx2.moveTo(x, y); }
    } else { ctx2.moveTo(0, h / 2); ctx2.lineTo(w, h / 2); }
    ctx2.stroke();
    if (on && !document.hidden) raf = requestAnimationFrame(draw);
  }
  draw();
  new MutationObserver(function () { if (!raf) draw(); }).observe(document.documentElement, { attributes: true, attributeFilter: ['data-mode'] });

  /* knobs */
  function paint(k) {
    var e = knobs[k], v = V[k]; e.arc.style.strokeDashoffset = (1 - v) * e.len; e.dial.style.setProperty('--a', (-135 + v * 270) + 'deg');
    e.out.textContent = Math.round(v * 100); e.dial.setAttribute('aria-valuenow', Math.round(v * 100));
  }
  Array.prototype.forEach.call(rack.querySelectorAll('.mx-knob'), function (el) {
    var k = el.getAttribute('data-k'), dial = el.querySelector('.mx-dial'), arc = el.querySelector('.mx-arc'), len = arc.getTotalLength ? arc.getTotalLength() : 120;
    arc.style.strokeDasharray = len; knobs[k] = { dial: dial, arc: arc, len: len, out: el.querySelector('output') };
    var drag = null;
    function set(v) { V[k] = Math.max(0, Math.min(1, v)); paint(k); apply(); }
    dial.addEventListener('pointerdown', function (e) { drag = { y: e.clientY, v: V[k], id: e.pointerId }; try { dial.setPointerCapture(e.pointerId); } catch (x) { } dial.classList.add('on'); });
    dial.addEventListener('pointermove', function (e) { if (drag) set(drag.v + (drag.y - e.clientY) / 160); });
    function end() { drag = null; dial.classList.remove('on'); }
    dial.addEventListener('pointerup', end); dial.addEventListener('pointercancel', end);
    dial.addEventListener('dblclick', function () { set(+el.getAttribute('data-v')); });
    dial.addEventListener('wheel', function (e) { e.preventDefault(); set(V[k] - e.deltaY / 600); }, { passive: false });
    dial.addEventListener('keydown', function (e) {
      var d = { ArrowUp: 0.04, ArrowRight: 0.04, ArrowDown: -0.04, ArrowLeft: -0.04, PageUp: 0.15, PageDown: -0.15 }[e.key];
      if (e.key === 'Home') { e.preventDefault(); set(0); } else if (e.key === 'End') { e.preventDefault(); set(1); } else if (d) { e.preventDefault(); set(V[k] + d); }
    });
    set(V[k]);
  });
  document.addEventListener('visibilitychange', function () { if (on && !raf) raf = requestAnimationFrame(draw); });
})();
