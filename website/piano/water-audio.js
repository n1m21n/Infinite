/**
 * Nocturne water sounds, synthesised live: splashes while wading, strokes while
 * swimming, a muffled underwater world. Everything runs through the piano's
 * output bus, so going under filters the music and the ambience too.
 *
 *   water sounds --> outBus --> [underwater low-pass] --> glue compressor --> limiter
 */

export function createWaterAudio(ctx, outBus, glue) {
  if (!ctx || !outBus || !glue) return null;

  const lp = ctx.createBiquadFilter();
  lp.type = 'lowpass'; lp.frequency.value = 20000; lp.Q.value = 0.7;
  const dip = ctx.createGain();
  try {
    outBus.disconnect(glue);
    outBus.connect(lp); lp.connect(dip); dip.connect(glue);
  } catch (e) {
    return null;
  }

  const bus = ctx.createGain();
  bus.gain.value = 0.9;
  bus.connect(outBus);

  // two seconds of pink-ish noise, looped or sliced for every sound below
  const len = ctx.sampleRate * 2;
  const buf = ctx.createBuffer(1, len, ctx.sampleRate);
  const d = buf.getChannelData(0);
  let b0 = 0, b1 = 0, b2 = 0;
  for (let i = 0; i < len; i++) {
    const w = Math.random() * 2 - 1;
    b0 = 0.99765 * b0 + w * 0.099046; b1 = 0.963 * b1 + w * 0.2965164; b2 = 0.57 * b2 + w * 1.0526913;
    d[i] = (b0 + b1 + b2 + w * 0.1848) * 0.22;
  }
  const noise = (loop) => {
    const s = ctx.createBufferSource();
    s.buffer = buf; s.loop = !!loop;
    return s;
  };

  const pan = (v) => {
    if (!ctx.createStereoPanner) return bus;
    const p = ctx.createStereoPanner();
    p.pan.value = v; p.connect(bus);
    return p;
  };

  /** One shaped noise burst: band, level, swell and fall in seconds. */
  function burst({ freq, q = 1, gain, attack = 0.012, fall = 0.25, sweep = 1, type = 'bandpass', at = 0, to = bus }) {
    const t0 = ctx.currentTime + at;
    const s = noise(false);
    const f = ctx.createBiquadFilter();
    f.type = type; f.Q.value = q;
    f.frequency.setValueAtTime(freq, t0);
    f.frequency.exponentialRampToValueAtTime(Math.max(40, freq * sweep), t0 + attack + fall);
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.linearRampToValueAtTime(gain, t0 + attack);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + attack + fall);
    s.connect(f); f.connect(g); g.connect(to);
    s.start(t0, Math.random() * 1.5, attack + fall + 0.05);
  }

  /** A short falling sine: the "plop" of a drop or a bubble. */
  function blip({ from, to, gain, dur, at = 0, out = bus }) {
    const t0 = ctx.currentTime + at;
    const o = ctx.createOscillator();
    o.type = 'sine';
    o.frequency.setValueAtTime(from, t0);
    o.frequency.exponentialRampToValueAtTime(to, t0 + dur);
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t0);
    g.gain.linearRampToValueAtTime(gain, t0 + 0.008);
    g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
    o.connect(g); g.connect(out);
    o.start(t0); o.stop(t0 + dur + 0.02);
  }

  /** A foot or a hand breaking the surface. `size` 0..1. */
  function splash(size, side = 0) {
    const out = pan(side);
    const j = 0.85 + Math.random() * 0.3;
    burst({ freq: 1500 * j, q: 0.9, gain: 0.10 + size * 0.2, fall: 0.18 + size * 0.3, sweep: 0.45, to: out });
    burst({ freq: 380 * j, q: 0.8, gain: 0.07 + size * 0.14, fall: 0.3 + size * 0.35, sweep: 0.6, to: out });
    burst({ freq: 5200 * j, q: 1.4, gain: 0.03 + size * 0.05, fall: 0.1 + size * 0.1, sweep: 0.6, to: out });
    const drops = 2 + Math.round(size * 5);
    for (let i = 0; i < drops; i++) {
      const f = (700 + Math.random() * 1500) * j;
      blip({ from: f, to: f * 1.9, gain: 0.015 + Math.random() * 0.02, dur: 0.05 + Math.random() * 0.07, at: 0.03 + Math.random() * (0.18 + size * 0.25), out });
    }
  }

  /** A swimming stroke: a slow swell of water sliding past. */
  function stroke(soft) {
    const out = pan(Math.random() * 0.6 - 0.3);
    const k = soft ? 0.55 : 1;
    burst({ freq: 520, q: 0.7, gain: 0.13 * k, attack: 0.22, fall: 0.55, sweep: 1.7, to: out });
    burst({ freq: 1800, q: 0.9, gain: 0.05 * k, attack: 0.2, fall: 0.35, sweep: 0.6, to: out });
    if (!soft) blip({ from: 600, to: 1400, gain: 0.02, dur: 0.07, at: 0.3, out });
  }

  /** Bubbles rising, for underwater. */
  function bubbles() {
    const out = pan(Math.random() * 1.2 - 0.6);
    const n = 1 + Math.floor(Math.random() * 4);
    for (let i = 0; i < n; i++) {
      const f = 260 + Math.random() * 520;
      blip({ from: f, to: f * (1.8 + Math.random()), gain: 0.04 + Math.random() * 0.04, dur: 0.08 + Math.random() * 0.1, at: i * (0.06 + Math.random() * 0.12), out });
    }
  }

  // continuous beds: gentle lapping at the surface, a deep hush below it
  const lapSrc = noise(true);
  const lapBand = ctx.createBiquadFilter(); lapBand.type = 'bandpass'; lapBand.frequency.value = 650; lapBand.Q.value = 0.5;
  const lap = ctx.createGain(); lap.gain.value = 0;
  const lfo = ctx.createOscillator(); lfo.frequency.value = 0.31;
  const lfoDepth = ctx.createGain(); lfoDepth.gain.value = 0.012;
  lfo.connect(lfoDepth); lfoDepth.connect(lap.gain);
  lapSrc.connect(lapBand); lapBand.connect(lap); lap.connect(bus);
  lapSrc.start(); lfo.start();

  const hushSrc = noise(true);
  const hushLow = ctx.createBiquadFilter(); hushLow.type = 'lowpass'; hushLow.frequency.value = 260; hushLow.Q.value = 0.4;
  const hush = ctx.createGain(); hush.gain.value = 0;
  hushSrc.connect(hushLow); hushLow.connect(hush); hush.connect(bus);
  hushSrc.start(0, 0.7);

  // state carried between frames
  let wasWet = false, wasUnder = false;
  let stepIn = 0, strokeIn = 0, bubbleIn = 2;
  let side = 1;

  /** Call every frame with the world's immersion state. */
  function update(s, dt) {
    const now = ctx.currentTime;
    const wet = s.depth > 0.12;
    const wade = Math.min(1, s.depth / 0.7);

    // the whole mix goes dull and distant underwater
    lp.frequency.setTargetAtTime(s.under ? 480 : 20000, now, s.under ? 0.05 : 0.09);
    dip.gain.setTargetAtTime(s.under ? 0.8 : 1, now, 0.15);
    lap.gain.setTargetAtTime(wet && !s.under ? 0.035 + 0.05 * wade : 0, now, 0.25);
    hush.gain.setTargetAtTime(s.under ? 0.5 : 0, now, 0.2);

    // crossing the waterline, and the surface
    if (wet && !wasWet) splash(0.55);
    if (s.under && !wasUnder) {
      burst({ freq: 700, q: 0.6, gain: 0.3, attack: 0.02, fall: 0.7, sweep: 0.3 });
      bubbles(); bubbleIn = 0.8;
    }
    if (!s.under && wasUnder) {
      splash(0.9);
      burst({ freq: 900, q: 0.6, gain: 0.2, attack: 0.03, fall: 0.8, sweep: 0.4 });
    }
    wasWet = wet; wasUnder = s.under;

    // steps, strokes and bubbles
    if (s.moving && wet && !s.under) {
      if (s.swim > 0.6) {
        strokeIn -= dt;
        if (strokeIn <= 0) { stroke(false); strokeIn = 1.15 + Math.random() * 0.2; }
      } else {
        stepIn -= dt;
        if (stepIn <= 0) { splash(0.25 + 0.5 * wade, side *= -1); stepIn = (s.run ? 0.3 : 0.5) + Math.random() * 0.06; }
      }
    } else {
      stepIn = 0.05;
      if (!(s.moving && s.under)) strokeIn = 0.1;
    }
    if (s.under) {
      if (s.moving) {
        strokeIn -= dt;
        if (strokeIn <= 0) { stroke(true); strokeIn = 1.5; }
      }
      bubbleIn -= dt;
      if (bubbleIn <= 0) { bubbles(); bubbleIn = 1.4 + Math.random() * 2.6; }
    }
  }

  return { update, splash, ctx };
}
