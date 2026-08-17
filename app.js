/**
 * Infinite — Web Application Logic
 * Cosmic Nebula Canvas, Interactive Metaballs, 4 Capability Canvas Animations,
 * Minimal Audio Player, and 3 Interactive Node Ticker Tapes.
 */

// ==========================================================================
// 1. Cosmic Dust & Nebula Canvas (Visible on Warm Paper Background)
// ==========================================================================
const cosmicCanvas = document.getElementById('cosmic-canvas');
const cosmicCtx = cosmicCanvas ? cosmicCanvas.getContext('2d') : null;
let cWidth, cHeight;
let cosmicStars = [];
let cosmicNebulae = [];

function resizeCosmicCanvas() {
  if (!cosmicCanvas) return;
  cWidth = cosmicCanvas.width = window.innerWidth;
  cHeight = cosmicCanvas.height = window.innerHeight;
  initCosmos();
}

function initCosmos() {
  cosmicStars = [];
  cosmicNebulae = [];

  const starCount = Math.floor(Math.min(cWidth, 1400) / 14);
  for (let i = 0; i < starCount; i++) {
    cosmicStars.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      size: Math.random() * 2 + 1,
      alpha: Math.random() * 0.25 + 0.08,
      pulseSpeed: 0.008 + Math.random() * 0.015,
      pulseOffset: Math.random() * Math.PI * 2,
      vx: (Math.random() - 0.5) * 0.2,
      vy: (Math.random() - 0.5) * 0.2,
      color: ['#2b2621', '#c2593f', '#d97736', '#4d7c67', '#6b6b99'][Math.floor(Math.random() * 5)]
    });
  }

  // Soft drifting pastel nebula patches
  for (let i = 0; i < 5; i++) {
    cosmicNebulae.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      radius: Math.random() * 220 + 200,
      vx: (Math.random() - 0.5) * 0.12,
      vy: (Math.random() - 0.5) * 0.12,
      color: ['rgba(194, 89, 63, 0.04)', 'rgba(217, 119, 54, 0.04)', 'rgba(77, 124, 103, 0.04)', 'rgba(107, 107, 153, 0.04)'][i % 4]
    });
  }
}

let cosmicTime = 0;
function animateCosmos() {
  if (!cosmicCtx) return;
  cosmicTime++;
  cosmicCtx.clearRect(0, 0, cWidth, cHeight);

  // Draw soft drifting nebula clusters
  cosmicNebulae.forEach(n => {
    n.x += n.vx;
    n.y += n.vy;
    if (n.x < -n.radius) n.x = cWidth + n.radius;
    if (n.x > cWidth + n.radius) n.x = -n.radius;
    if (n.y < -n.radius) n.y = cHeight + n.radius;
    if (n.y > cHeight + n.radius) n.y = -n.radius;

    const grad = cosmicCtx.createRadialGradient(n.x, n.y, 0, n.x, n.y, n.radius);
    grad.addColorStop(0, n.color);
    grad.addColorStop(1, 'transparent');

    cosmicCtx.fillStyle = grad;
    cosmicCtx.beginPath();
    cosmicCtx.arc(n.x, n.y, n.radius, 0, Math.PI * 2);
    cosmicCtx.fill();
  });

  // Draw twinkling star & ink dust
  cosmicStars.forEach(s => {
    s.x += s.vx;
    s.y += s.vy;
    if (s.x < 0) s.x = cWidth;
    if (s.x > cWidth) s.x = 0;
    if (s.y < 0) s.y = cHeight;
    if (s.y > cHeight) s.y = 0;

    const pulse = Math.sin(cosmicTime * s.pulseSpeed + s.pulseOffset) * 0.3 + 0.7;
    cosmicCtx.globalAlpha = s.alpha * pulse;
    cosmicCtx.fillStyle = s.color;
    cosmicCtx.beginPath();
    cosmicCtx.arc(s.x, s.y, s.size, 0, Math.PI * 2);
    cosmicCtx.fill();
  });
  cosmicCtx.globalAlpha = 1.0;

  requestAnimationFrame(animateCosmos);
}


// ==========================================================================
// 2. Interactive Metaballs Canvas Simulation
// Keywords: Sound, Music, Art, Geometry, Generative, Node-Based, Real-Time
// ==========================================================================
const metaCanvas = document.getElementById('metaballs-canvas');
const metaCtx = metaCanvas ? metaCanvas.getContext('2d') : null;

const METABALL_WORDS = [
  { text: 'Sound', color: '#c2593f', r: 46 },
  { text: 'Music', color: '#d97736', r: 44 },
  { text: 'Art', color: '#b8860b', r: 42 },
  { text: 'Geometry', color: '#4d7c67', r: 48 },
  { text: 'Generative', color: '#6b6b99', r: 52 },
  { text: 'Node-Based', color: '#c2593f', r: 50 },
  { text: 'Real-Time', color: '#d97736', r: 48 }
];

let metaballs = [];
let mouseX = -1000, mouseY = -1000;
let isMouseDown = false;

function initMetaballs() {
  if (!metaCanvas) return;
  const rect = metaCanvas.getBoundingClientRect();
  metaCanvas.width = rect.width * window.devicePixelRatio || 900;
  metaCanvas.height = rect.height * window.devicePixelRatio || 380;
  const w = metaCanvas.width;
  const h = metaCanvas.height;

  metaballs = METABALL_WORDS.map((item, idx) => ({
    text: item.text,
    color: item.color,
    radius: item.r * (window.devicePixelRatio || 1) * 0.85,
    x: (w / (METABALL_WORDS.length + 1)) * (idx + 1) + (Math.random() - 0.5) * 40,
    y: h / 2 + (Math.random() - 0.5) * 80,
    vx: (Math.random() - 0.5) * 0.6,
    vy: (Math.random() - 0.5) * 0.6,
    baseRadius: item.r * (window.devicePixelRatio || 1) * 0.85
  }));
}

function animateMetaballs() {
  if (!metaCtx || !metaCanvas) return;
  const w = metaCanvas.width;
  const h = metaCanvas.height;
  metaCtx.clearRect(0, 0, w, h);

  // Soft organic connections between nearby metaballs
  for (let i = 0; i < metaballs.length; i++) {
    for (let j = i + 1; j < metaballs.length; j++) {
      const b1 = metaballs[i];
      const b2 = metaballs[j];
      const dx = b2.x - b1.x;
      const dy = b2.y - b1.y;
      const dist = Math.hypot(dx, dy);
      const maxDist = (b1.radius + b2.radius) * 1.5;

      if (dist < maxDist) {
        const factor = 1 - dist / maxDist;
        metaCtx.beginPath();
        metaCtx.moveTo(b1.x, b1.y);
        metaCtx.lineTo(b2.x, b2.y);
        metaCtx.strokeStyle = b1.color;
        metaCtx.globalAlpha = factor * 0.25;
        metaCtx.lineWidth = factor * 30;
        metaCtx.lineCap = 'round';
        metaCtx.stroke();
        metaCtx.globalAlpha = 1.0;
      }
    }
  }

  // Update & draw each metaball circle
  metaballs.forEach(b => {
    // Mouse interaction
    const mdx = mouseX - b.x;
    const mdy = mouseY - b.y;
    const mdist = Math.hypot(mdx, mdy);
    if (mdist < 140 && mdist > 0) {
      const force = (1 - mdist / 140) * (isMouseDown ? 2.5 : -1.2);
      b.vx += (mdx / mdist) * force * 0.1;
      b.vy += (mdy / mdist) * force * 0.1;
    }

    // Motion & damping
    b.x += b.vx;
    b.y += b.vy;
    b.vx *= 0.985;
    b.vy *= 0.985;

    // Gentle float impulses
    b.vx += (Math.random() - 0.5) * 0.08;
    b.vy += (Math.random() - 0.5) * 0.08;

    // Bounce off walls
    const padding = b.radius + 10;
    if (b.x < padding) { b.x = padding; b.vx *= -1; }
    if (b.x > w - padding) { b.x = w - padding; b.vx *= -1; }
    if (b.y < padding) { b.y = padding; b.vy *= -1; }
    if (b.y > h - padding) { b.y = h - padding; b.vy *= -1; }

    // Draw soft pastel metaball body
    metaCtx.beginPath();
    metaCtx.arc(b.x, b.y, b.radius, 0, Math.PI * 2);
    metaCtx.fillStyle = '#ffffff';
    metaCtx.shadowColor = b.color;
    metaCtx.shadowBlur = 16;
    metaCtx.fill();
    metaCtx.shadowBlur = 0;

    metaCtx.lineWidth = 2.5;
    metaCtx.strokeStyle = b.color;
    metaCtx.stroke();

    // Draw keyword label
    metaCtx.font = `600 ${14 * (window.devicePixelRatio || 1)}px Inter, sans-serif`;
    metaCtx.fillStyle = '#1f1d1a';
    metaCtx.textAlign = 'center';
    metaCtx.textBaseline = 'middle';
    metaCtx.fillText(b.text, b.x, b.y);
  });

  requestAnimationFrame(animateMetaballs);
}

if (metaCanvas) {
  metaCanvas.addEventListener('mousemove', (e) => {
    const rect = metaCanvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    mouseX = (e.clientX - rect.left) * dpr;
    mouseY = (e.clientY - rect.top) * dpr;
  });
  metaCanvas.addEventListener('mouseleave', () => {
    mouseX = -1000;
    mouseY = -1000;
  });
  metaCanvas.addEventListener('mousedown', () => { isMouseDown = true; });
  window.addEventListener('mouseup', () => { isMouseDown = false; });
}


// ==========================================================================
// 3. Four Capability Mini-Canvas Animations
// ==========================================================================

// Animation 1: Formula Interference Waves
function initWavesAnimation() {
  const canvas = document.getElementById('anim-waves');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
  }
  resize();

  function draw() {
    t += 0.03;
    const w = canvas.width;
    const h = canvas.height;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const step = 8;
    ctx.lineWidth = 1.2;

    for (let y = 0; y < h; y += step) {
      ctx.beginPath();
      ctx.strokeStyle = `rgba(217, 119, 54, ${0.35 + Math.sin(y * 0.05 + t) * 0.25})`;
      for (let x = 0; x < w; x += 4) {
        const d1 = Math.hypot(x - w * 0.35, y - h * 0.5);
        const d2 = Math.hypot(x - w * 0.65, y - h * 0.5);
        const wave = Math.sin(d1 * 0.08 - t * 2) + Math.sin(d2 * 0.08 - t * 2);
        const py = y + wave * 6;
        if (x === 0) ctx.moveTo(x, py);
        else ctx.lineTo(x, py);
      }
      ctx.stroke();
    }
    requestAnimationFrame(draw);
  }
  draw();
}

// Animation 2: 3D Wavetable Movement
function initWavetableAnimation() {
  const canvas = document.getElementById('anim-wavetable');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
  }
  resize();

  function draw() {
    t += 0.025;
    const w = canvas.width;
    const h = canvas.height;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const frames = 10;
    const sliceWidth = w * 0.7;

    for (let f = 0; f < frames; f++) {
      const depth = f / frames;
      const ox = w * 0.15 + (f - frames / 2) * 4;
      const oy = h * 0.28 + f * 10;
      const alpha = 0.2 + depth * 0.7;

      ctx.beginPath();
      ctx.strokeStyle = f === Math.floor((Math.sin(t) * 0.5 + 0.5) * (frames - 1)) 
        ? '#c2593f' 
        : `rgba(233, 196, 106, ${alpha})`;
      ctx.lineWidth = f === Math.floor((Math.sin(t) * 0.5 + 0.5) * (frames - 1)) ? 2.5 : 1.2;

      for (let x = 0; x <= sliceWidth; x += 3) {
        const u = x / sliceWidth;
        const morph = Math.sin(u * Math.PI * (2 + f * 0.5) + t * 2) * Math.cos(u * Math.PI + f * 0.3);
        const y = oy - morph * 22;
        if (x === 0) ctx.moveTo(ox + x, y);
        else ctx.lineTo(ox + x, y);
      }
      ctx.stroke();
    }
    requestAnimationFrame(draw);
  }
  draw();
}

// Animation 3: Particle Cloud Simulation
function initParticlesAnimation() {
  const canvas = document.getElementById('anim-particles');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  function resize() {
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
  }
  resize();

  const particles = [];
  for (let i = 0; i < 90; i++) {
    particles.push({
      angle: Math.random() * Math.PI * 2,
      radius: Math.random() * 55 + 10,
      speed: 0.015 + Math.random() * 0.02,
      yOffset: (Math.random() - 0.5) * 40,
      size: Math.random() * 2 + 1,
      color: ['#81b29a', '#b8b8d1', '#e9c46a'][Math.floor(Math.random() * 3)]
    });
  }

  let t = 0;
  function draw() {
    t += 0.02;
    const w = canvas.width;
    const h = canvas.height;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const cx = w / 2;
    const cy = h / 2;

    particles.forEach(p => {
      p.angle += p.speed;
      const x = cx + Math.cos(p.angle) * p.radius;
      const y = cy + Math.sin(p.angle) * (p.radius * 0.45) + Math.sin(t + p.radius) * 8;

      ctx.beginPath();
      ctx.arc(x, y, p.size, 0, Math.PI * 2);
      ctx.fillStyle = p.color;
      ctx.fill();
    });

    requestAnimationFrame(draw);
  }
  draw();
}

// Animation 4: 4 Modulation Knobs with Pulsing Cables
function initKnobsAnimation() {
  const canvas = document.getElementById('anim-knobs');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  let t = 0;

  function resize() {
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
  }
  resize();

  function draw() {
    t += 0.03;
    const w = canvas.width;
    const h = canvas.height;
    ctx.fillStyle = '#0f1218';
    ctx.fillRect(0, 0, w, h);

    const knobPositions = [
      { x: w * 0.25, y: h * 0.38, speed: 1.0, color: '#e07a5f' },
      { x: w * 0.75, y: h * 0.38, speed: -1.3, color: '#f4a261' },
      { x: w * 0.38, y: h * 0.72, speed: 0.8, color: '#81b29a' },
      { x: w * 0.68, y: h * 0.72, speed: -0.9, color: '#b8b8d1' }
    ];

    // Draw patch cables between knobs
    ctx.lineWidth = 1.8;
    ctx.beginPath();
    ctx.strokeStyle = 'rgba(244, 162, 97, 0.4)';
    ctx.moveTo(knobPositions[0].x, knobPositions[0].y);
    ctx.bezierCurveTo(w * 0.5, h * 0.1, w * 0.5, h * 0.6, knobPositions[1].x, knobPositions[1].y);
    ctx.stroke();

    ctx.beginPath();
    ctx.strokeStyle = 'rgba(129, 178, 154, 0.4)';
    ctx.moveTo(knobPositions[2].x, knobPositions[2].y);
    ctx.bezierCurveTo(w * 0.4, h * 0.95, w * 0.6, h * 0.95, knobPositions[3].x, knobPositions[3].y);
    ctx.stroke();

    // Draw the 4 rotary knobs
    knobPositions.forEach((k, idx) => {
      const angle = t * k.speed + idx * 1.5;
      const r = 22;

      // Outer ring
      ctx.beginPath();
      ctx.arc(k.x, k.y, r, 0, Math.PI * 2);
      ctx.fillStyle = '#1c212a';
      ctx.fill();
      ctx.lineWidth = 2;
      ctx.strokeStyle = k.color;
      ctx.stroke();

      // Indicator pointer line
      const px = k.x + Math.cos(angle) * (r * 0.75);
      const py = k.y + Math.sin(angle) * (r * 0.75);
      ctx.beginPath();
      ctx.moveTo(k.x, k.y);
      ctx.lineTo(px, py);
      ctx.lineWidth = 2.5;
      ctx.strokeStyle = '#ffffff';
      ctx.stroke();
    });

    requestAnimationFrame(draw);
  }
  draw();
}


// ==========================================================================
// 4. Minimal Soundscape Audio Player
// ==========================================================================
function initMinimalAudioPlayer() {
  const audio = document.getElementById('m-audio-element');
  const playBtn = document.getElementById('minimal-play-btn');
  const playIcon = document.getElementById('m-play-icon');
  const pauseIcon = document.getElementById('m-pause-icon');
  const progressContainer = document.getElementById('m-progress-container');
  const progressFill = document.getElementById('m-progress-fill');
  const timeText = document.getElementById('m-track-time');

  if (!audio || !playBtn) return;

  function togglePlay() {
    if (audio.paused) {
      audio.play().then(() => {
        playIcon.style.display = 'none';
        pauseIcon.style.display = 'block';
      }).catch(() => {});
    } else {
      audio.pause();
      playIcon.style.display = 'block';
      pauseIcon.style.display = 'none';
    }
  }

  playBtn.addEventListener('click', togglePlay);

  audio.addEventListener('timeupdate', () => {
    if (audio.duration) {
      const pct = (audio.currentTime / audio.duration) * 100;
      if (progressFill) progressFill.style.width = `${pct}%`;

      const curM = Math.floor(audio.currentTime / 60);
      const curS = Math.floor(audio.currentTime % 60).toString().padStart(2, '0');
      const durM = Math.floor(audio.duration / 60);
      const durS = Math.floor(audio.duration % 60).toString().padStart(2, '0');
      if (timeText) timeText.textContent = `${curM}:${curS} / ${durM}:${durS}`;
    }
  });

  audio.addEventListener('ended', () => {
    playIcon.style.display = 'block';
    pauseIcon.style.display = 'none';
    if (progressFill) progressFill.style.width = '0%';
  });

  if (progressContainer) {
    progressContainer.addEventListener('click', (e) => {
      const rect = progressContainer.getBoundingClientRect();
      const clickX = e.clientX - rect.left;
      if (audio.duration) {
        audio.currentTime = (clickX / rect.width) * audio.duration;
      }
    });
  }
}


// ==========================================================================
// 5. 3 Continuous Ticker Tapes & Interactive Node Modal
// ==========================================================================
const ALL_NODES = [
  { name: 'Wavetable Synth', cat: 'Audio', desc: 'Multi-frame morphing oscillator with 12 factory tables, unison detune, and sub-oscillators.' },
  { name: 'Cloth Simulation', cat: '3D Physics', desc: 'Position-Based Dynamics (PBD) soft-body solver with wind forces and collision pinning.' },
  { name: 'PaulStretch', cat: 'Audio DSP', desc: 'Spectral FFT extreme time-stretching up to 50x for ambient atmospheric soundscapes.' },
  { name: 'Formula GLSL', cat: 'Shaders', desc: 'Live GLSL 150 fragment shader editor with 16 presets and live uniform CV inputs.' },
  { name: 'Remove Background', cat: 'AI Vision', desc: 'On-device Apple Vision neural segmentation with zero network latency.' },
  { name: 'Ocean Surface', cat: '3D & Physics', desc: 'Procedural Gerstner wave simulation with foam, choppiness, and wind vectors.' },
  { name: 'Modal Resonator', cat: 'Physical Modeling', desc: 'Acoustic simulation of struck bells, metal plates, tubes, and glass chimes.' },
  { name: 'Audio Analyze', cat: 'Modulation', desc: 'Extracts 8-band FFT spectrum and transient onset energy to drive visual shaders.' },
  { name: 'Granular Engine', cat: 'Audio', desc: 'Real-time granular cloud texture engine with live playhead scrubbing and grain spray.' },
  { name: 'Instance on Points', cat: '3D Geometry', desc: 'Scatter tens of thousands of 3D meshes in a single GPU instancing draw call.' },
  { name: 'Syphon In / Out', cat: 'Video I/O', desc: 'Zero-copy GPU video streaming between Infinite, OBS, Resolume, and TouchDesigner.' },
  { name: 'Color Curves', cat: 'Color Grading', desc: 'Photoshop-style cubic spline color grading across Master, RGB, and Luma channels.' },
  { name: 'LUT 3D', cat: 'Color', desc: 'Industry-standard .cube 3D lookup tables for cinematic film emulation.' },
  { name: 'Arpeggiator', cat: 'MIDI & Notes', desc: 'Tempo-synced pattern arpeggiator with Up, Down, Random, and multi-octave ranges.' },
  { name: 'Bouncing Balls', cat: 'MIDI', desc: 'Physics-based gravity simulation generating polyphonic notes mapped to musical scales.' },
  { name: 'Drum Sequencer', cat: 'Audio', desc: '8-track step sequencer with lane mutes, swing timing, and sample pattern chaining.' },
  { name: 'AU & VST3 Host', cat: 'Plugin', desc: 'Host third-party instrument and effect plugins with native GUI windows and mapped knobs.' },
  { name: 'Bloom & Glow', cat: '2D Effects', desc: 'Multi-pass thresholded anamorphic light bloom and diffuse optical glow.' },
  { name: 'Poisson Scattering', cat: '3D Geometry', desc: 'Distribute points smoothly across complex 3D meshes without clustering.' },
  { name: 'XY Motion Pad', cat: 'Modulation', desc: 'Record expressive 2D mouse gestures with spring physics and looping playback.' }
];

function initTickerTapes() {
  const t1 = document.getElementById('ticker-1');
  const t2 = document.getElementById('ticker-2');
  const t3 = document.getElementById('ticker-3');

  if (!t1 || !t2 || !t3) return;

  // Split nodes into 3 groups
  const g1 = ALL_NODES.slice(0, 7);
  const g2 = ALL_NODES.slice(7, 14);
  const g3 = ALL_NODES.slice(14);

  function createPillsHtml(group) {
    // Duplicate 3x for infinite seamless looping
    const tri = [...group, ...group, ...group];
    return tri.map(n => `
      <div class="ticker-pill" data-name="${escapeHtml(n.name)}" data-cat="${escapeHtml(n.cat)}" data-desc="${escapeHtml(n.desc)}">
        ${escapeHtml(n.name)}
      </div>
    `).join('');
  }

  t1.innerHTML = createPillsHtml(g1);
  t2.innerHTML = createPillsHtml(g2);
  t3.innerHTML = createPillsHtml(g3);

  // Click on pill to open modal
  document.querySelectorAll('.ticker-pill').forEach(pill => {
    pill.addEventListener('click', () => {
      openNodeModal(pill.dataset.name, pill.dataset.cat, pill.dataset.desc);
    });
  });
}

function openNodeModal(name, cat, desc) {
  const modal = document.getElementById('node-modal');
  const nameEl = document.getElementById('modal-name');
  const catEl = document.getElementById('modal-cat');
  const descEl = document.getElementById('modal-desc');

  if (nameEl) nameEl.textContent = name;
  if (catEl) catEl.textContent = cat;
  if (descEl) descEl.textContent = desc;

  if (modal) {
    modal.classList.add('active');
    modal.setAttribute('aria-hidden', 'false');
  }
}

function initModal() {
  const modal = document.getElementById('node-modal');
  const closeBtn = document.getElementById('modal-close-btn');

  if (closeBtn && modal) {
    closeBtn.addEventListener('click', () => {
      modal.classList.remove('active');
      modal.setAttribute('aria-hidden', 'true');
    });
  }

  if (modal) {
    modal.addEventListener('click', (e) => {
      if (e.target === modal) {
        modal.classList.remove('active');
        modal.setAttribute('aria-hidden', 'true');
      }
    });
  }
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, m => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[m]);
}

// Copy inline helpers
function initCopyButtons() {
  document.querySelectorAll('.copy-inline-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const text = btn.dataset.code;
      if (!text) return;
      navigator.clipboard.writeText(text).then(() => {
        const original = btn.innerText;
        btn.innerText = 'Copied!';
        btn.style.color = 'var(--pastel-terracotta)';
        setTimeout(() => {
          btn.innerText = original;
          btn.style.color = '';
        }, 2000);
      });
    });
  });
}

// DOM Ready initialization
document.addEventListener('DOMContentLoaded', () => {
  window.addEventListener('resize', resizeCosmicCanvas);
  resizeCosmicCanvas();
  requestAnimationFrame(animateCosmos);

  initMetaballs();
  requestAnimationFrame(animateMetaballs);

  initWavesAnimation();
  initWavetableAnimation();
  initParticlesAnimation();
  initKnobsAnimation();

  initMinimalAudioPlayer();
  initTickerTapes();
  initModal();
  initCopyButtons();
});
